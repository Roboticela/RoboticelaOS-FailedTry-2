/*
 * RoboCSS selector parsing (Phase 9.4) — see Include/RoboCSS/RoboCSSSelectorParser.h.
 *
 * The CSS Selectors grammar over the 9.2 token stream:
 *
 *   selector list  := complex (',' complex)*
 *   complex        := compound (combinator compound)*
 *   combinator     := '>' | '+' | '~' | '||' | whitespace
 *   compound       := (type | '*')? simple*
 *   simple         := '.'ident | '#'ident | '['attribute']' | ':'':'?ident('('…')')?
 *
 * Four decisions shape it.
 *
 * 1. It lexes nothing.
 *
 *    Every byte arrives as a token, and every name it records is the token's
 *    *value* — escapes resolved by 9.2's own rule, which is the only escape
 *    implementation in the subsystem. `.foo\:bar` becomes the six bytes
 *    `foo:bar` here, once, so matching compares what CSS meant and never has to
 *    know that escapes exist.
 *
 * 2. Whitespace is a combinator; a comment is not.
 *
 *    `a b` and `ab` are different selectors, so the whitespace *between* two
 *    compounds is the descendant combinator. A comment is not: a comment
 *    between `div` and `p` leaves two type selectors in one compound, which is
 *    invalid, exactly as a browser reads it. That is why this file steps over
 *    comments itself but only *counts* whitespace, rather than calling
 *    CssTokenizerSkipTrivia and losing the distinction.
 *
 * 3. Past the nesting bound, refuse rather than recurse.
 *
 *    `:not(:not(:not(…)))` is parsed by running this file again over the
 *    argument's bytes, so depth costs stack. RoboCssMaxSelectorNesting caps
 *    it, and a selector past the cap is refused — the only interaction between
 *    an adversarial stylesheet and the stack is an error message.
 *
 * 4. The arena is reserved before anything is interned.
 *
 *    A name is recorded as a slice into the list's arena, and an arena that
 *    grows moves, which would invalidate every name already recorded. Reserving
 *    an upper bound up front — twice the text's length is a true bound, since
 *    the most any escape can expand is `\0`'s two bytes into U+FFFD's three, and
 *    everything else shrinks (a quoted string loses its quotes, `#x` its `#`) —
 *    means interning never grows and every name stays put. It also takes an
 *    allocation failure out of the middle of parsing, which is where a
 *    half-built model would come from.
 *
 * Offsets are local to one level and made absolute on the way out. A level's
 * tokenizer runs over that level's text, so its token offsets start at 0; the
 * level's errors are reported at `local + baseOffset`, and a nested level
 * inherits `parent.baseOffset + argument_offset`. Every position a caller sees
 * is therefore an offset in the source the outermost call was given.
 */

#include "RoboCSS/RoboCSSSelectorParser.h"

#include "RoboCSSInternal.h"
#include "RoboCSS/RoboCSSToken.h"
#include "RoboCSS/RoboCSSTokenizer.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Parse state                                                         */
/* ------------------------------------------------------------------ */

/*
 * One level of the parse. A nested `:not()` gets its own state, its own
 * tokenizer over the argument's bytes, and its own target list — because a
 * nested selector's names belong to the list that owns it, not to its parent.
 */
typedef struct selectState {
    CssTokenizer tokenizer;
    CssSelectorList *list; /* the list being filled */
    size_t baseOffset;             /* added to a local offset for a position */
    CssError *error;        /* the first problem, or NULL */
    int failed;
    size_t depth;
} SelectState;

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

/*
 * Record the first problem and latch the failure. The first one wins because it
 * is the one that explains the rest: a list refused at `[a=1]` should not be
 * remembered as failing wherever the scan stumbled next.
 */
static void SelectFail(SelectState *state,
                        CssErrorKind kind,
                        size_t offset,
                        const char *message)
{
    CssPosition position;

    if (state->failed) {
        return;
    }
    state->failed = 1;
    if (!state->error) {
        return;
    }
    position.offset = offset + state->baseOffset;
    position.line = 0u;
    position.column = 0u;
    (void)CssErrorSet(state->error, kind, RoboParseError, &position, message);
}

/* ------------------------------------------------------------------ */
/* Token helpers                                                       */
/* ------------------------------------------------------------------ */

static int SelectPeek(SelectState *state, CssToken *out)
{
    if (CssTokenizerPeek(&state->tokenizer, out, NULL) != RoboOk) {
        SelectFail(state, RoboCssErrorKindRecoverableSyntax, 0u,
                    "the selector text could not be tokenized");
        return 0;
    }
    return 1;
}

/** Consume the next token, filling @p out when it is non-NULL. */
static int SelectAdvance(SelectState *state, CssToken *out)
{
    CssToken scratch;

    if (CssTokenizerNext(&state->tokenizer, out ? out : &scratch, NULL) !=
        RoboOk) {
        SelectFail(state, RoboCssErrorKindRecoverableSyntax, 0u,
                    "the selector text could not be tokenized");
        return 0;
    }
    return 1;
}

/*
 * Step over comments, reporting whether whitespace was seen. Both are trivia the
 * selector grammar does not keep, but only one of them is a combinator, so they
 * cannot be stepped over by the same call.
 */
static void SelectSkipComments(SelectState *state, int *sawWhitespace)
{
    CssToken token;

    for (;;) {
        if (!SelectPeek(state, &token)) {
            return;
        }
        if (token.type == RoboCssTokenWhitespace) {
            if (sawWhitespace) {
                *sawWhitespace = 1;
            }
        } else if (token.type != RoboCssTokenComment) {
            return;
        }
        if (!SelectAdvance(state, NULL)) {
            return;
        }
    }
}

static int SelectTokenEqualsCi(CssToken token, const char *ascii)
{
    return CssBytesEqualAsciiCi(token.text.bytes, token.text.length, ascii);
}

/**
 * A level's local range as an absolute one.
 *
 * Token ranges are local to the level's own text, so every range recorded on a
 * selector is lifted through this — which is what makes a parsed selector's
 * extents comparable with the error positions and with the source a caller
 * already holds, whatever nesting level produced them.
 */
static CssRange SelectRange(const SelectState *state, CssRange local)
{
    return CssRangeSpan(local.begin + state->baseOffset,
                               local.end > local.begin ? local.end - local.begin : 0u);
}

/** Non-zero when @p token is a single-character delimiter carrying @p byte. */
static int SelectIsDelim(CssToken token, char byte)
{
    return token.type == RoboCssTokenDelim && token.text.length == 1u &&
           token.text.bytes && token.text.bytes[0] == (uint8_t)byte;
}

/**
 * Intern @p token's resolved value, or record why it could not be interned.
 *
 * @p allowEmpty exists for exactly one call site: an attribute selector's
 * value, where `[a=""]` is a real comparison against an empty value. Everywhere
 * else an empty name is malformed — there is no such element, class, id or
 * attribute.
 */
static int SelectIntern(SelectState *state,
                         const CssToken *token,
                         CssSlice *out,
                         int allowEmpty)
{
    size_t length = CssTokenValueLength(token);
    uint8_t *buffer;
    size_t written = 0u;
    CssSlice slice;

    if (length == 0u) {
        if (!allowEmpty) {
            SelectFail(state, RoboCssErrorKindInvalidToken, token->range.begin,
                        "a selector name may not be empty");
            return 0;
        }
        *out = CssSelectorListIntern(state->list, NULL, 0u);
        if (CssSliceIsAbsent(*out)) {
            SelectFail(state, RoboCssErrorKindOutOfMemory, token->range.begin,
                        "could not record an empty attribute value");
            return 0;
        }
        return 1;
    }
    /* Resolving a name needs scratch for exactly one name, never for the whole
     * sheet: the resolved bytes go straight into the list's arena afterwards. */
    buffer = (uint8_t *)Malloc(length);
    if (!buffer) {
        SelectFail(state, RoboCssErrorKindOutOfMemory, token->range.begin,
                    "could not resolve a selector name");
        return 0;
    }
    if (CssTokenValueCopy(token, buffer, length, &written, NULL) != RoboOk) {
        RoboFree(buffer);
        SelectFail(state, RoboCssErrorKindInvalidToken, token->range.begin,
                    "a selector name could not be resolved");
        return 0;
    }
    slice = CssSelectorListIntern(state->list, buffer, written);
    RoboFree(buffer);
    if (CssSliceIsAbsent(slice)) {
        SelectFail(state, RoboCssErrorKindOutOfMemory, token->range.begin,
                    "could not record a selector name");
        return 0;
    }
    *out = slice;
    return 1;
}

/* ------------------------------------------------------------------ */
/* An+B                                                                */
/* ------------------------------------------------------------------ */

typedef struct anbCursor {
    const uint8_t *bytes;
    size_t length;
    size_t at;
} AnbCursor;

/* An element index above this cannot occur, and the bound keeps the arithmetic
 * in a plain int. */
#define ANB_VALUE_MAX 1000000000L

static void AnbSkipSpace(AnbCursor *cursor)
{
    while (cursor->at < cursor->length &&
           CssAsciiIsSpace(cursor->bytes[cursor->at])) {
        cursor->at++;
    }
}

static int AnbAtEnd(const AnbCursor *cursor)
{
    return cursor->at >= cursor->length;
}

static int AnbPeek(const AnbCursor *cursor)
{
    return AnbAtEnd(cursor) ? -1 : (int)cursor->bytes[cursor->at];
}

/** Match @p word exactly, ASCII case-insensitively, and step over it. */
static int AnbIsKeyword(AnbCursor *cursor, const char *word)
{
    size_t length = strlen(word);
    size_t i;

    if (cursor->length - cursor->at != length) {
        return 0;
    }
    for (i = 0u; i < length; i++) {
        if (CssAsciiLower(cursor->bytes[cursor->at + i]) != (uint8_t)word[i]) {
            return 0;
        }
    }
    cursor->at += length;
    return 1;
}

/**
 * Parse a run of digits into @p out; 0 when none were there, or too many.
 *
 * The bound is applied *before* each multiply rather than after, and that is not
 * a micro-optimisation: `long` is 32 bits on some platforms this builds for, so
 * `value * 10` on its own overflows for an eleven-digit run — which is a signed
 * overflow, i.e. undefined behaviour, rather than the "too many digits" answer
 * the caller expects. Dividing instead of multiplying keeps the test itself
 * exact: every accepted value is at most ANB_VALUE_MAX, and ANB_VALUE_MAX is
 * below LONG_MAX everywhere, so no step can overflow.
 */
static int AnbDigits(AnbCursor *cursor, long *out)
{
    long value = 0;
    size_t digits = 0u;

    while (cursor->at < cursor->length &&
           CssAsciiIsDigit(cursor->bytes[cursor->at])) {
        long digit = (long)(cursor->bytes[cursor->at] - (uint8_t)'0');

        if (value > (ANB_VALUE_MAX - digit) / 10L) {
            return 0;
        }
        value = value * 10L + digit;
        cursor->at++;
        digits++;
    }
    if (digits == 0u) {
        return 0;
    }
    *out = value;
    return 1;
}

Result CssAnPlusBParse(const char *text,
                                       size_t length,
                                       CssAnPlusB *out,
                                       CssDiag *diag)
{
    AnbCursor cursor;
    int sign = 1;
    int hadDigits;
    long value = 0;

    /* Empty text is a *parse* failure, not a caller mistake: `:nth-child()`
     * hands over whatever the author wrote between the parentheses, and an
     * empty argument is something the author can write. A missing buffer or
     * missing output is the caller's bug. */
    if (!out || !text) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "no An+B text to parse");
    }
    out->a = 0;
    out->b = 0;
    cursor.bytes = (const uint8_t *)text;
    cursor.length = length;
    cursor.at = 0u;

    AnbSkipSpace(&cursor);
    if (AnbAtEnd(&cursor)) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboParseError,
                             "empty An+B argument");
    }
    if (AnbIsKeyword(&cursor, "odd")) {
        out->a = 2;
        out->b = 1;
    } else if (AnbIsKeyword(&cursor, "even")) {
        out->a = 2;
        out->b = 0;
    } else {
        int next = AnbPeek(&cursor);

        if (next == '+' || next == '-') {
            sign = (next == '-') ? -1 : 1;
            cursor.at++;
        }
        hadDigits = AnbDigits(&cursor, &value);
        next = AnbPeek(&cursor);

        if (next == 'n' || next == 'N') {
            cursor.at++;
            /* `n` is `1n`, and a bare sign with no digits is `+n` / `-n`. */
            out->a = hadDigits ? (int)(sign * value) : sign;
            out->b = 0;
            /* An offset is optional: `2n` means `2n+0`. */
            {
                size_t before = cursor.at;
                int signChar;

                AnbSkipSpace(&cursor);
                signChar = AnbPeek(&cursor);
                if (signChar == '+' || signChar == '-') {
                    int bSign = (signChar == '-') ? -1 : 1;
                    long bValue = 0;

                    cursor.at++;
                    AnbSkipSpace(&cursor);
                    if (!AnbDigits(&cursor, &bValue)) {
                        return RoboCssDiag(diag, RoboCssDomainSelector,
                                             RoboParseError,
                                             "an An+B offset sign must be followed by digits");
                    }
                    out->b = (int)(bSign * bValue);
                } else {
                    cursor.at = before;
                }
            }
        } else if (hadDigits) {
            out->a = 0;
            out->b = (int)(sign * value);
        } else {
            return RoboCssDiag(diag, RoboCssDomainSelector, RoboParseError,
                                 "an An+B expression needs an integer, `n`, `odd` or `even`");
        }
    }

    AnbSkipSpace(&cursor);
    if (!AnbAtEnd(&cursor)) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboParseError,
                             "unexpected characters after the An+B expression");
    }
    return RoboOk;
}

int CssAnPlusBMatches(const CssAnPlusB *form, int index)
{
    int difference;
    int step;

    if (!form || index <= 0) {
        return 0;
    }
    if (form->a == 0) {
        return index == form->b;
    }
    difference = index - form->b;
    if (form->a > 0) {
        /* a·n + b = index needs n = (index - b)/a to be whole and non-negative. */
        return difference >= 0 && (difference % form->a) == 0;
    }
    /* A negative coefficient descends from b, so only indexes at or below b are
     * reachable, in steps of -a. */
    if (difference > 0) {
        return 0;
    }
    step = -form->a;
    difference = -difference;
    return (difference % step) == 0;
}

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static int SelectParseList(SelectState *state);
static int SelectParseSub(SelectState *parent,
                            CssSelectorList *out,
                            size_t argumentBegin,
                            size_t argumentLength);

/* ------------------------------------------------------------------ */
/* Attribute selectors                                                 */
/* ------------------------------------------------------------------ */

static int SelectParseAttribute(SelectState *state,
                                  CssCompoundSelector *compound)
{
    CssToken token;
    CssSimpleSelector simple = RoboCssCleared;
    CssSlice attributeName;
    CssSlice value = CssSliceEmpty();
    CssAttributeMatch match = RoboCssAttributeMatchNone;
    CssAttributeCase caseFlag = RoboCssAttributeCaseDefault;
    int valueWasString = 0;
    size_t begin;
    size_t extent;

    if (!SelectPeek(state, &token)) {
        return 0;
    }
    begin = token.range.begin;
    if (!SelectAdvance(state, NULL)) { /* consume `[` */
        return 0;
    }
    SelectSkipComments(state, NULL);

    if (!SelectPeek(state, &token) || token.type != RoboCssTokenIdent) {
        SelectFail(state, RoboCssErrorKindInvalidToken, begin,
                    "`[` must be followed by an attribute name");
        return 0;
    }
    if (!SelectIntern(state, &token, &attributeName, 0)) {
        return 0;
    }
    if (!SelectAdvance(state, NULL)) {
        return 0;
    }
    SelectSkipComments(state, NULL);

    if (!SelectPeek(state, &token)) {
        return 0;
    }
    if (token.type == RoboCssTokenRightSquare) {
        if (!SelectAdvance(state, NULL)) { /* presence only: `[disabled]` */
            return 0;
        }
    } else {
        if (token.type != RoboCssTokenDelim || token.text.length != 1u) {
            SelectFail(state, RoboCssErrorKindInvalidToken, token.range.begin,
                        "expected `]` or an attribute comparison operator");
            return 0;
        }
        switch ((char)token.text.bytes[0]) {
        case '=':
            match = RoboCssAttributeMatchEquals;
            break;
        case '~':
            match = RoboCssAttributeMatchIncludes;
            break;
        case '|':
            match = RoboCssAttributeMatchDash;
            break;
        case '^':
            match = RoboCssAttributeMatchPrefix;
            break;
        case '$':
            match = RoboCssAttributeMatchSuffix;
            break;
        case '*':
            match = RoboCssAttributeMatchSubstring;
            break;
        default:
            SelectFail(state, RoboCssErrorKindInvalidToken, token.range.begin,
                        "unknown attribute comparison operator");
            return 0;
        }
        if (!SelectAdvance(state, NULL)) {
            return 0;
        }
        if (match != RoboCssAttributeMatchEquals) {
            SelectSkipComments(state, NULL);
            if (!SelectPeek(state, &token) || !SelectIsDelim(token, '=')) {
                SelectFail(state, RoboCssErrorKindInvalidToken, begin,
                            "an attribute operator must end in `=`");
                return 0;
            }
            if (!SelectAdvance(state, NULL)) {
                return 0;
            }
        }
        SelectSkipComments(state, NULL);

        if (!SelectPeek(state, &token)) {
            return 0;
        }
        if (token.type == RoboCssTokenString) {
            valueWasString = 1;
        } else if (token.type != RoboCssTokenIdent) {
            /* An unquoted value must be an identifier, so `[a=1]` is not a
             * comparison against the string "1". */
            SelectFail(state, RoboCssErrorKindInvalidToken, token.range.begin,
                        "an unquoted attribute value must be an identifier; quote it otherwise");
            return 0;
        }
        if (!SelectIntern(state, &token, &value, 1)) {
            return 0;
        }
        if (!SelectAdvance(state, NULL)) {
            return 0;
        }
        SelectSkipComments(state, NULL);

        if (!SelectPeek(state, &token)) {
            return 0;
        }
        if (token.type == RoboCssTokenIdent &&
            (SelectTokenEqualsCi(token, "i") || SelectTokenEqualsCi(token, "s"))) {
            caseFlag = SelectTokenEqualsCi(token, "i")
                            ? RoboCssAttributeCaseInsensitive
                            : RoboCssAttributeCaseSensitive;
            if (!SelectAdvance(state, NULL)) {
                return 0;
            }
            SelectSkipComments(state, NULL);
            if (!SelectPeek(state, &token)) {
                return 0;
            }
        }
        if (token.type != RoboCssTokenRightSquare) {
            SelectFail(state, RoboCssErrorKindInvalidToken, token.range.begin,
                        "an attribute selector must end with `]`");
            return 0;
        }
        if (!SelectAdvance(state, NULL)) {
            return 0;
        }
    }

    extent = CssTokenizerOffset(&state->tokenizer) - begin;
    if (CssSimpleSelectorMake(&simple, RoboCssSimpleSelectorAttribute,
                                      CssSliceEmpty(), NULL) != RoboOk ||
        CssSimpleSelectorSetAttribute(&simple, attributeName, match, value,
                                               caseFlag, NULL) != RoboOk) {
        SelectFail(state, RoboCssErrorKindInvalidToken, begin,
                    "could not describe an attribute selector");
        return 0;
    }
    simple.range = SelectRange(state, CssRangeSpan(begin, extent));
    simple.valueWasString = valueWasString;
    if (CssCompoundSelectorAppend(compound, &simple, NULL) != RoboOk) {
        SelectFail(state, RoboCssErrorKindOutOfMemory, begin,
                    "could not record an attribute selector");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Pseudo-classes and pseudo-elements                                  */
/* ------------------------------------------------------------------ */

/**
 * Are these the name of a pseudo-element CSS once spelled with one colon?
 *
 * CSS2's `:before`, `:after`, `:first-line` and `:first-letter` are
 * pseudo-*elements* that every engine still accepts in the legacy spelling
 * (Blink's `ParsePseudoElement` exists for exactly this list and no other name),
 * and reading `:before` as a pseudo-class would make a condition nothing can
 * satisfy out of generated content. The list is closed: `:hover` is a
 * pseudo-class and stays one.
 */
static int SelectIsLegacyPseudoElement(CssSlice name)
{
    static const char *const legacy[] = {
        "before", "after", "first-line", "first-letter"
    };
    size_t i;

    for (i = 0u; i < sizeof(legacy) / sizeof(legacy[0]); i++) {
        if (CssBytesEqualAsciiCi(name.bytes, name.length, legacy[i])) {
            return 1;
        }
    }
    return 0;
}

/**
 * Collect the raw bytes between a function's parentheses.
 * Called with the `name(` token already consumed, so the cursor sits just past
 * the `(`; returns with the closing `)` consumed.
 */
static int SelectScanArgument(SelectState *state,
                                size_t *outBegin,
                                size_t *outLength)
{
    size_t begin = CssTokenizerOffset(&state->tokenizer);
    size_t end = begin;
    size_t depth = 1u;
    CssToken token;

    for (;;) {
        if (!SelectPeek(state, &token)) {
            return 0;
        }
        if (token.type == RoboCssTokenEof) {
            SelectFail(state, RoboCssErrorKindUnexpectedEof, begin,
                        "a functional pseudo-class was never closed");
            return 0;
        }
        if (token.type == RoboCssTokenLeftParen ||
            token.type == RoboCssTokenFunction) {
            depth++;
        } else if (token.type == RoboCssTokenRightParen) {
            depth--;
            if (depth == 0u) {
                end = token.range.begin;
                if (!SelectAdvance(state, NULL)) {
                    return 0;
                }
                break;
            }
        }
        if (!SelectAdvance(state, NULL)) {
            return 0;
        }
    }
    *outBegin = begin;
    *outLength = end > begin ? end - begin : 0u;
    return 1;
}

static int SelectParsePseudo(SelectState *state,
                               CssCompoundSelector *compound)
{
    CssToken token;
    CssSimpleSelector simple = RoboCssCleared;
    CssSlice name;
    CssSlice argument = CssSliceEmpty();
    size_t begin;
    size_t simpleIndex;
    size_t argumentBegin = 0u;
    size_t argumentLength = 0u;
    int isElement = 0;
    int hasArgument = 0;

    if (!SelectPeek(state, &token)) {
        return 0;
    }
    begin = token.range.begin;
    if (!SelectAdvance(state, NULL)) { /* consume `:` */
        return 0;
    }
    if (!SelectPeek(state, &token)) {
        return 0;
    }
    if (token.type == RoboCssTokenColon) {
        isElement = 1;
        if (!SelectAdvance(state, NULL)) {
            return 0;
        }
    }

    if (!SelectPeek(state, &token)) {
        return 0;
    }
    if (token.type == RoboCssTokenFunction) {
        if (!SelectIntern(state, &token, &name, 0)) {
            return 0;
        }
        if (!SelectAdvance(state, NULL)) {
            return 0;
        }
        if (!SelectScanArgument(state, &argumentBegin, &argumentLength)) {
            return 0;
        }
        hasArgument = 1;
    } else if (token.type == RoboCssTokenIdent) {
        if (!SelectIntern(state, &token, &name, 0)) {
            return 0;
        }
        if (!SelectAdvance(state, NULL)) {
            return 0;
        }
    } else {
        /* Whitespace here is refused rather than stepped over: `a: hover` is not
         * a pseudo-class in any CSS, and reading it as one would apply rules a
         * browser would drop. */
        SelectFail(state, RoboCssErrorKindInvalidToken, begin,
                    "`:` must be followed by a pseudo-class or pseudo-element name");
        return 0;
    }

    /* A functional pseudo-class takes an argument and no pseudo-element
     * does, so the legacy spelling only reaches the pseudo-element kind when
     * the name stands alone. */
    if (!isElement && !hasArgument && SelectIsLegacyPseudoElement(name)) {
        isElement = 1;
    }

    if (CssSimpleSelectorMake(
            &simple,
            isElement ? RoboCssSimpleSelectorPseudoElement
                       : RoboCssSimpleSelectorPseudoClass,
            name, NULL) != RoboOk) {
        SelectFail(state, RoboCssErrorKindInvalidToken, begin,
                    "could not build a pseudo selector");
        return 0;
    }
    simple.range = SelectRange(
        state, CssRangeSpan(begin,
                                   CssTokenizerOffset(&state->tokenizer) - begin));

    if (hasArgument) {
        const CssSource *source = CssTokenizerSource(&state->tokenizer);

        /* The raw argument is recorded like any other name: interned, so that
         * nothing a caller keeps points into a buffer it does not own. */
        argument = CssSelectorListIntern(
            state->list, CssSourceBytes(source) + argumentBegin, argumentLength);
        if (argumentLength > 0u && CssSliceIsAbsent(argument)) {
            SelectFail(state, RoboCssErrorKindOutOfMemory, begin,
                        "could not record a pseudo-class argument");
            return 0;
        }
        if (CssSimpleSelectorSetArgument(&simple, argument, NULL) != RoboOk) {
            SelectFail(state, RoboCssErrorKindInvalidToken, begin,
                        "could not record a pseudo-class argument");
            return 0;
        }
    }

    if (!isElement) {
        int isNot = CssSimpleSelectorIsPseudoNamed(&simple, "not");
        int isNth = CssSimpleSelectorIsPseudoNamed(&simple, "nth-child") ||
                     CssSimpleSelectorIsPseudoNamed(&simple, "nth-last-child");

        if ((isNot || isNth) && !hasArgument) {
            SelectFail(state, RoboCssErrorKindInvalidToken, begin,
                        "this pseudo-class requires a parenthesised argument");
            return 0;
        }
        if (isNth) {
            CssAnPlusB form;

            if (CssAnPlusBParse((const char *)CssSourceBytes(
                                             CssTokenizerSource(&state->tokenizer)) +
                                             argumentBegin,
                                         argumentLength, &form, NULL) != RoboOk) {
                SelectFail(state, RoboCssErrorKindRecoverableSyntax, begin,
                            "an An+B argument must be an integer, `odd`, `even` or `An+B`");
                return 0;
            }
            /* `simple` is a copy the compound now owns, so the parsed form is
             * recorded through the compound's own array. */
            if (CssCompoundSelectorAppend(compound, &simple, NULL) != RoboOk) {
                SelectFail(state, RoboCssErrorKindOutOfMemory, begin,
                            "could not record a pseudo selector");
                return 0;
            }
            simpleIndex = CssCompoundSelectorCount(compound) - 1u;
            if (CssSimpleSelectorSetNth(&compound->items[simpleIndex], form.a,
                                                 form.b, NULL) != RoboOk) {
                SelectFail(state, RoboCssErrorKindInvalidToken, begin,
                            "could not record an An+B argument");
                return 0;
            }
            return 1;
        }
        if (isNot) {
            CssSelectorList nested = RoboCssCleared;

            if (CssCompoundSelectorAppend(compound, &simple, NULL) != RoboOk) {
                SelectFail(state, RoboCssErrorKindOutOfMemory, begin,
                            "could not record a pseudo selector");
                return 0;
            }
            simpleIndex = CssCompoundSelectorCount(compound) - 1u;
            if (SelectParseSub(state, &nested, argumentBegin, argumentLength)) {
                /* Attaching moves the nested list in, arena included, which is
                 * why a failure here has to release it rather than drop it. */
                if (CssCompoundSelectorAttachNested(compound, simpleIndex,
                                                             &nested, NULL) == RoboOk) {
                    return 1;
                }
                CssSelectorListDispose(&nested);
                SelectFail(state, RoboCssErrorKindOutOfMemory, begin,
                            "could not record a `:not()` argument");
            }
            return 0;
        }
    }

    if (CssCompoundSelectorAppend(compound, &simple, NULL) != RoboOk) {
        SelectFail(state, RoboCssErrorKindOutOfMemory, begin,
                    "could not record a pseudo selector");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Compounds                                                           */
/* ------------------------------------------------------------------ */

/*
 * One compound selector: at most one type selector, then any number of class,
 * id, attribute and pseudo conditions. Every condition must hold of the same
 * element, which is the whole meaning of a compound.
 *
 * Leading trivia is stepped over here rather than by the caller, because a
 * compound begins in two places: the start of a complex selector, and just after
 * an explicit combinator — where `div > p` puts a whitespace token between the
 * `>` and the `p`. Whitespace in that position is not a combinator and not part
 * of the selector: it is layout in the author's file. Consuming it is what makes
 * `div>p`, `div >p`, `div> p` and `div > p` the same selector, which the
 * combinator tests require.
 */
static int SelectParseCompound(SelectState *state,
                                 CssCompoundSelector *compound)
{
    CssToken token;

    SelectSkipComments(state, NULL);
    if (!SelectPeek(state, &token)) {
        return 0;
    }

    /* A type selector or the universal, first and at most once. */
    if (token.type == RoboCssTokenIdent) {
        CssSimpleSelector simple = RoboCssCleared;
        CssSlice name;

        if (!SelectIntern(state, &token, &name, 0)) {
            return 0;
        }
        if (CssSimpleSelectorMake(&simple, RoboCssSimpleSelectorType, name,
                                          NULL) != RoboOk) {
            SelectFail(state, RoboCssErrorKindInvalidToken, token.range.begin,
                        "could not build a type selector");
            return 0;
        }
        simple.range = SelectRange(state, token.range);
        if (!SelectAdvance(state, NULL) ||
            CssCompoundSelectorAppend(compound, &simple, NULL) != RoboOk) {
            SelectFail(state, RoboCssErrorKindOutOfMemory, token.range.begin,
                        "could not record a type selector");
            return 0;
        }
    } else if (SelectIsDelim(token, '*')) {
        CssSimpleSelector simple = RoboCssCleared;

        if (CssSimpleSelectorMake(&simple, RoboCssSimpleSelectorUniversal,
                                          CssSliceEmpty(), NULL) != RoboOk) {
            SelectFail(state, RoboCssErrorKindInvalidToken, token.range.begin,
                        "could not build the universal selector");
            return 0;
        }
        simple.range = SelectRange(state, token.range);
        if (!SelectAdvance(state, NULL) ||
            CssCompoundSelectorAppend(compound, &simple, NULL) != RoboOk) {
            SelectFail(state, RoboCssErrorKindOutOfMemory, token.range.begin,
                        "could not record the universal selector");
            return 0;
        }
    }

    for (;;) {
        if (!SelectPeek(state, &token)) {
            return 0;
        }
        if (SelectIsDelim(token, '.')) {
            CssSimpleSelector simple = RoboCssCleared;
            CssSlice name;
            size_t dotBegin = token.range.begin;

            if (!SelectAdvance(state, NULL) || !SelectPeek(state, &token)) {
                return 0;
            }
            if (token.type != RoboCssTokenIdent) {
                SelectFail(state, RoboCssErrorKindInvalidToken, dotBegin,
                            "`.` must be followed by a class name");
                return 0;
            }
            if (!SelectIntern(state, &token, &name, 0)) {
                return 0;
            }
            if (CssSimpleSelectorMake(&simple, RoboCssSimpleSelectorClass,
                                              name, NULL) != RoboOk) {
                SelectFail(state, RoboCssErrorKindInvalidToken, token.range.begin,
                            "could not build a class selector");
                return 0;
            }
            /* From the `.`, not from the name: a simple selector's range is the
             * extent of the *selector*, and for `.card` that begins at the dot.
             * Otherwise a compound's derived range would start mid-selector and
             * every diagnostic built on it would point a column to the right. */
            simple.range = SelectRange(state, CssRangeSpan(
                                                   dotBegin,
                                                   token.range.end - dotBegin));
            if (!SelectAdvance(state, NULL) ||
                CssCompoundSelectorAppend(compound, &simple, NULL) != RoboOk) {
                SelectFail(state, RoboCssErrorKindOutOfMemory, token.range.begin,
                            "could not record a class selector");
                return 0;
            }
            continue;
        }
        if (token.type == RoboCssTokenHash) {
            CssSimpleSelector simple = RoboCssCleared;
            CssSlice name;

            /* `#123` is a hash token but not an id selector: only the
             * identifier-restricted form names an element. */
            if (token.hash != RoboCssHashTypeId) {
                SelectFail(state, RoboCssErrorKindInvalidToken, token.range.begin,
                            "`#` must be followed by an identifier");
                return 0;
            }
            if (!SelectIntern(state, &token, &name, 0)) {
                return 0;
            }
            if (CssSimpleSelectorMake(&simple, RoboCssSimpleSelectorId, name,
                                              NULL) != RoboOk) {
                SelectFail(state, RoboCssErrorKindInvalidToken, token.range.begin,
                            "could not build an id selector");
                return 0;
            }
            simple.range = SelectRange(state, token.range);
            if (!SelectAdvance(state, NULL) ||
                CssCompoundSelectorAppend(compound, &simple, NULL) != RoboOk) {
                SelectFail(state, RoboCssErrorKindOutOfMemory, token.range.begin,
                            "could not record an id selector");
                return 0;
            }
            continue;
        }
        if (token.type == RoboCssTokenLeftSquare) {
            if (!SelectParseAttribute(state, compound)) {
                return 0;
            }
            continue;
        }
        if (token.type == RoboCssTokenColon) {
            if (!SelectParsePseudo(state, compound)) {
                return 0;
            }
            continue;
        }
        if (token.type == RoboCssTokenIdent) {
            /* A comment separates the tokens but is not whitespace, so an
             * identifier after a type selector really is a second type selector
             * in one compound — invalid, and refused rather than merged. */
            SelectFail(state, RoboCssErrorKindRecoverableSyntax, token.range.begin,
                        "a compound selector may hold only one type selector");
            return 0;
        }
        break;
    }

    if (CssCompoundSelectorCount(compound) == 0u) {
        SelectFail(state, RoboCssErrorKindRecoverableSyntax,
                    CssTokenizerOffset(&state->tokenizer), "expected a selector");
        return 0;
    }
    CssCompoundSelectorSealRange(compound);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Complex selectors                                                   */
/* ------------------------------------------------------------------ */

static int SelectParseComplex(SelectState *state,
                                CssComplexSelector *complex)
{
    for (;;) {
        CssCompoundSelector compound = RoboCssCleared;
        CssToken token;
        int sawWhitespace = 0;

        if (!SelectParseCompound(state, &compound)) {
            CssCompoundSelectorDispose(&compound);
            return 0;
        }
        if (CssComplexSelectorAppendCompound(complex, &compound, NULL) !=
            RoboOk) {
            CssCompoundSelectorDispose(&compound);
            SelectFail(state, RoboCssErrorKindOutOfMemory, 0u,
                        "could not record a compound selector");
            return 0;
        }

        SelectSkipComments(state, &sawWhitespace);
        if (!SelectPeek(state, &token)) {
            return 0;
        }
        if (token.type == RoboCssTokenComma || token.type == RoboCssTokenEof) {
            return 1;
        }

        {
            CssCombinator combinator = RoboCssCombinatorNone;
            size_t begin = token.range.begin;

            if (SelectIsDelim(token, '>')) {
                combinator = RoboCssCombinatorChild;
            } else if (SelectIsDelim(token, '+')) {
                combinator = RoboCssCombinatorNextSibling;
            } else if (SelectIsDelim(token, '~')) {
                combinator = RoboCssCombinatorSubsequentSibling;
            } else if (SelectIsDelim(token, '|')) {
                if (!SelectAdvance(state, NULL) || !SelectPeek(state, &token)) {
                    return 0;
                }
                if (!SelectIsDelim(token, '|')) {
                    SelectFail(state, RoboCssErrorKindInvalidToken, begin,
                                "the column combinator is `||`");
                    return 0;
                }
                combinator = RoboCssCombinatorColumn;
            }

            if (combinator != RoboCssCombinatorNone) {
                if (!SelectAdvance(state, NULL)) {
                    return 0;
                }
                if (CssComplexSelectorAppendCombinator(complex, combinator,
                                                                NULL) != RoboOk) {
                    SelectFail(state, RoboCssErrorKindOutOfMemory, begin,
                                "could not record a combinator");
                    return 0;
                }
                continue;
            }
        }

        if (sawWhitespace) {
            /* The one combinator CSS spells with a space. */
            if (CssComplexSelectorAppendCombinator(
                    complex, RoboCssCombinatorDescendant, NULL) != RoboOk) {
                SelectFail(state, RoboCssErrorKindOutOfMemory, token.range.begin,
                            "could not record a descendant combinator");
                return 0;
            }
            continue;
        }
        SelectFail(state, RoboCssErrorKindRecoverableSyntax, token.range.begin,
                    "expected a combinator, `,` or the end of the selector");
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Lists                                                               */
/* ------------------------------------------------------------------ */

/**
 * Give the list the extent of its selectors, derived from them rather than
 * tracked beside them. Called on every successful exit, so the range can never
 * be left describing a list that a later failure discarded.
 */
static int SelectListSeal(SelectState *state)
{
    size_t count = CssSelectorListCount(state->list);
    const CssComplexSelector *first;
    const CssComplexSelector *last;

    if (count == 0u) {
        state->list->range = CssRangeAt(0u);
        return 1;
    }
    first = CssSelectorListAt(state->list, 0u);
    last = CssSelectorListAt(state->list, count - 1u);
    state->list->range =
        CssRangeSpan(first->range.begin, last->range.end - first->range.begin);
    return 1;
}

static int SelectParseList(SelectState *state)
{
    int sawWhitespace = 0;
    CssToken token;

    SelectSkipComments(state, &sawWhitespace);
    if (!SelectPeek(state, &token)) {
        return 0;
    }
    if (token.type == RoboCssTokenEof) {
        SelectFail(state, RoboCssErrorKindRecoverableSyntax, 0u,
                    "an empty selector list is not a selector");
        return 0;
    }

    for (;;) {
        CssComplexSelector complex = RoboCssCleared;

        if (!SelectParseComplex(state, &complex)) {
            CssComplexSelectorDispose(&complex);
            return 0;
        }
        if (!CssComplexSelectorIsWellFormed(&complex)) {
            CssComplexSelectorDispose(&complex);
            SelectFail(state, RoboCssErrorKindRecoverableSyntax, 0u,
                        "a selector ended with a trailing combinator");
            return 0;
        }
        /* A complex selector's extent is its compounds' — derived from the tree
         * rather than tracked beside it, so the two cannot disagree. */
        {
            size_t compoundCount =
                CssComplexSelectorCompoundCount(&complex);
            const CssCompoundSelector *first =
                CssComplexSelectorCompoundAt(&complex, 0u);
            const CssCompoundSelector *last =
                CssComplexSelectorCompoundAt(&complex, compoundCount - 1u);

            complex.range = CssRangeSpan(first->range.begin,
                                                last->range.end - first->range.begin);
        }
        if (CssSelectorListAppend(state->list, &complex, NULL) != RoboOk) {
            CssComplexSelectorDispose(&complex);
            SelectFail(state, RoboCssErrorKindOutOfMemory, 0u,
                        "could not record a selector");
            return 0;
        }

        SelectSkipComments(state, &sawWhitespace);
        if (!SelectPeek(state, &token)) {
            return 0;
        }
        if (token.type == RoboCssTokenEof) {
            return SelectListSeal(state);
        }
        if (token.type != RoboCssTokenComma) {
            SelectFail(state, RoboCssErrorKindRecoverableSyntax, token.range.begin,
                        "expected `,` or the end of the selector list");
            return 0;
        }
        if (!SelectAdvance(state, NULL)) {
            return 0;
        }
        SelectSkipComments(state, &sawWhitespace);
        if (!SelectPeek(state, &token)) {
            return 0;
        }
        if (token.type == RoboCssTokenEof || token.type == RoboCssTokenComma) {
            SelectFail(state, RoboCssErrorKindRecoverableSyntax, token.range.begin,
                        "`,` must be followed by a selector");
            return 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */

/**
 * Run one level: build a source over @p bytes, reserve @p out's arena, parse,
 * and hand back the result with everything temporary released.
 *
 * Twice the text's length plus a margin is a true upper bound on the resolved
 * name bytes (see the note at the top of the file), so the arena is reserved
 * once and interning never moves a name that has already been recorded.
 */
static Result SelectRun(CssSelectorList *out,
                                const uint8_t *bytes,
                                size_t length,
                                size_t baseOffset,
                                CssError *outError,
                                size_t depth,
                                CssDiag *diag)
{
    CssSource source;
    SelectState state;
    Result result;

    /* Initialised first, exactly as the 9.3 grammar initialises its output: a
     * caller may hand over an unused record, and a caller that re-parses into a
     * live list disposes it itself first (which is what
     * CssStyleRuleParseSelectors does). */
    CssSelectorListInit(out);
    if (outError) {
        CssErrorInit(outError);
    }
    CssSourceInit(&source);
    if (CssSourceSetMemory(&source, bytes, length, NULL) != RoboOk) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "the selector text could not be configured as a source");
    }
    if (CssSelectorListReserveNames(out, length * 2u + 8u, NULL) != RoboOk) {
        CssSourceDispose(&source);
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboOutOfMemory,
                             "could not reserve the selector name arena");
    }

    memset(&state, 0, sizeof(state));
    state.list = out;
    state.baseOffset = baseOffset;
    state.error = outError;
    state.depth = depth;
    if (CssTokenizerInit(&state.tokenizer, &source, NULL) != RoboOk) {
        CssSourceDispose(&source);
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "the selector text could not be tokenized");
    }

    result = SelectParseList(&state) ? RoboOk : RoboParseError;

    CssTokenizerDispose(&state.tokenizer);
    CssSourceDispose(&source);

    if (result != RoboOk) {
        /* All or nothing: a selector list with one bad selector is not a
         * selector list, and half of one would silently match a subset of what
         * the author wrote. */
        CssSelectorListDispose(out);
        if (outError && !CssErrorIsError(outError)) {
            (void)CssErrorSet(outError, RoboCssErrorKindRecoverableSyntax,
                                     RoboParseError, NULL,
                                     "the selector list could not be parsed");
        }
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboParseError,
                             "the selector list could not be parsed");
    }
    return RoboOk;
}

/*
 * One nested level. @p out's arena is fresh, so the argument's names go into the
 * nested list's own arena — which is what makes a nested list disposable exactly
 * like a top-level one, and what keeps the parent's arena from growing while its
 * own names are already recorded.
 */
static int SelectParseSub(SelectState *parent,
                            CssSelectorList *out,
                            size_t argumentBegin,
                            size_t argumentLength)
{
    const CssSource *parentSource =
        CssTokenizerSource(&parent->tokenizer);
    const uint8_t *bytes = CssSourceBytes(parentSource);
    size_t childDepth = parent->depth + 1u;
    Result result;

    if (!out) {
        SelectFail(parent, RoboCssErrorKindRecoverableSyntax, argumentBegin,
                    "a nested selector list needs somewhere to go");
        return 0;
    }
    if (!bytes) {
        SelectFail(parent, RoboCssErrorKindRecoverableSyntax, argumentBegin,
                    "a nested selector list has no source");
        return 0;
    }
    if (childDepth > RoboCssMaxSelectorNesting) {
        SelectFail(parent, RoboCssErrorKindRecoverableSyntax, argumentBegin,
                    "`:not()` nests deeper than this build parses");
        return 0;
    }
    result = SelectRun(out, bytes + argumentBegin, argumentLength,
                        parent->baseOffset + argumentBegin, parent->error,
                        childDepth, NULL);
    if (result != RoboOk) {
        /* The nested parse already recorded the precise reason in the shared
         * error record; latching the parent keeps the outer call honest without
         * overwriting the better message with a worse one. */
        parent->failed = 1;
        return 0;
    }
    return 1;
}

Result CssSelectorListParseText(CssSelectorList *out,
                                                const char *text,
                                                CssError *outError,
                                                CssDiag *diag)
{
    if (!out || !text) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "output or text is NULL");
    }
    return SelectRun(out, (const uint8_t *)text, strlen(text), 0u, outError, 0u,
                      diag);
}

Result CssSelectorListParseSlice(CssSelectorList *out,
                                                 CssSlice text,
                                                 CssError *outError,
                                                 CssDiag *diag)
{
    if (!out || CssSliceIsEmpty(text)) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "output is NULL or the selector text is empty");
    }
    return SelectRun(out, text.bytes, text.length, 0u, outError, 0u, diag);
}

Result CssSelectorListParseRange(CssSelectorList *out,
                                                 const CssSource *source,
                                                 CssRange range,
                                                 CssError *outError,
                                                 CssDiag *diag)
{
    CssSlice text;

    if (!out || !source) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "output or source is NULL");
    }
    if (CssSourceRangeSlice(source, range, &text) != RoboOk) {
        CssSelectorListInit(out);
        if (outError) {
            CssErrorInit(outError);
            (void)CssErrorSet(outError, RoboCssErrorKindMalformedRule,
                                     RoboInvalidArgument, NULL,
                                     "the selector range does not lie within the source");
        }
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "the selector range does not lie within the source");
    }
    if (CssSliceIsEmpty(text)) {
        CssSelectorListInit(out);
        if (outError) {
            (void)CssErrorSet(outError, RoboCssErrorKindRecoverableSyntax,
                                     RoboParseError, NULL,
                                     "an empty selector list is not a selector");
        }
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboParseError,
                             "an empty selector list is not a selector");
    }
    return SelectRun(out, text.bytes, text.length, range.begin, outError, 0u, diag);
}

Result CssStyleRuleParseSelectors(CssStyleRule *rule,
                                                  const CssSource *source,
                                                  CssError *outError,
                                                  CssDiag *diag)
{
    CssSelectorList parsed = RoboCssCleared;
    Result result;

    if (!rule || !source) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "rule or source is NULL");
    }
    result = CssSelectorListParseRange(&parsed, source, rule->preludeRange,
                                               outError, diag);
    if (result != RoboOk) {
        /* The rule keeps an empty selector list and is otherwise untouched:
         * Phase 9.3 already decided that malformed CSS produces a model rather
         * than a failed parse, and a rule whose prelude is not a selector list is
         * still a rule. */
        return result;
    }
    /* Move, arena included — a parsed name points into the arena it was interned
     * in, so the two have to travel together. */
    CssSelectorListDispose(&rule->selectors);
    rule->selectors = parsed;
    return RoboOk;
}
