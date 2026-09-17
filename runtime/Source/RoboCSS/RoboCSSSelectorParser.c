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
 *    robo_css_tokenizer_skip_trivia and losing the distinction.
 *
 * 3. Past the nesting bound, refuse rather than recurse.
 *
 *    `:not(:not(:not(…)))` is parsed by running this file again over the
 *    argument's bytes, so depth costs stack. ROBO_CSS_MAX_SELECTOR_NESTING caps
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
 * level's errors are reported at `local + base_offset`, and a nested level
 * inherits `parent.base_offset + argument_offset`. Every position a caller sees
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
typedef struct select_state {
    robo_css_tokenizer_t tokenizer;
    robo_css_selector_list_t *list; /* the list being filled */
    size_t base_offset;             /* added to a local offset for a position */
    robo_css_error_t *error;        /* the first problem, or NULL */
    int failed;
    size_t depth;
} select_state_t;

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

/*
 * Record the first problem and latch the failure. The first one wins because it
 * is the one that explains the rest: a list refused at `[a=1]` should not be
 * remembered as failing wherever the scan stumbled next.
 */
static void select_fail(select_state_t *state,
                        robo_css_error_kind_t kind,
                        size_t offset,
                        const char *message)
{
    robo_css_position_t position;

    if (state->failed) {
        return;
    }
    state->failed = 1;
    if (!state->error) {
        return;
    }
    position.offset = offset + state->base_offset;
    position.line = 0u;
    position.column = 0u;
    (void)robo_css_error_set(state->error, kind, ROBO_PARSE_ERROR, &position, message);
}

/* ------------------------------------------------------------------ */
/* Token helpers                                                       */
/* ------------------------------------------------------------------ */

static int select_peek(select_state_t *state, robo_css_token_t *out)
{
    if (robo_css_tokenizer_peek(&state->tokenizer, out, NULL) != ROBO_OK) {
        select_fail(state, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, 0u,
                    "the selector text could not be tokenized");
        return 0;
    }
    return 1;
}

/** Consume the next token, filling @p out when it is non-NULL. */
static int select_advance(select_state_t *state, robo_css_token_t *out)
{
    robo_css_token_t scratch;

    if (robo_css_tokenizer_next(&state->tokenizer, out ? out : &scratch, NULL) !=
        ROBO_OK) {
        select_fail(state, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, 0u,
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
static void select_skip_comments(select_state_t *state, int *saw_whitespace)
{
    robo_css_token_t token;

    for (;;) {
        if (!select_peek(state, &token)) {
            return;
        }
        if (token.type == ROBO_CSS_TOKEN_WHITESPACE) {
            if (saw_whitespace) {
                *saw_whitespace = 1;
            }
        } else if (token.type != ROBO_CSS_TOKEN_COMMENT) {
            return;
        }
        if (!select_advance(state, NULL)) {
            return;
        }
    }
}

static int select_token_equals_ci(robo_css_token_t token, const char *ascii)
{
    return robo_css_bytes_equal_ascii_ci(token.text.bytes, token.text.length, ascii);
}

/**
 * A level's local range as an absolute one.
 *
 * Token ranges are local to the level's own text, so every range recorded on a
 * selector is lifted through this — which is what makes a parsed selector's
 * extents comparable with the error positions and with the source a caller
 * already holds, whatever nesting level produced them.
 */
static robo_css_range_t select_range(const select_state_t *state, robo_css_range_t local)
{
    return robo_css_range_span(local.begin + state->base_offset,
                               local.end > local.begin ? local.end - local.begin : 0u);
}

/** Non-zero when @p token is a single-character delimiter carrying @p byte. */
static int select_is_delim(robo_css_token_t token, char byte)
{
    return token.type == ROBO_CSS_TOKEN_DELIM && token.text.length == 1u &&
           token.text.bytes && token.text.bytes[0] == (uint8_t)byte;
}

/**
 * Intern @p token's resolved value, or record why it could not be interned.
 *
 * @p allow_empty exists for exactly one call site: an attribute selector's
 * value, where `[a=""]` is a real comparison against an empty value. Everywhere
 * else an empty name is malformed — there is no such element, class, id or
 * attribute.
 */
static int select_intern(select_state_t *state,
                         const robo_css_token_t *token,
                         robo_css_slice_t *out,
                         int allow_empty)
{
    size_t length = robo_css_token_value_length(token);
    uint8_t *buffer;
    size_t written = 0u;
    robo_css_slice_t slice;

    if (length == 0u) {
        if (!allow_empty) {
            select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, token->range.begin,
                        "a selector name may not be empty");
            return 0;
        }
        *out = robo_css_selector_list_intern(state->list, NULL, 0u);
        if (robo_css_slice_is_absent(*out)) {
            select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, token->range.begin,
                        "could not record an empty attribute value");
            return 0;
        }
        return 1;
    }
    /* Resolving a name needs scratch for exactly one name, never for the whole
     * sheet: the resolved bytes go straight into the list's arena afterwards. */
    buffer = (uint8_t *)robo_malloc(length);
    if (!buffer) {
        select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, token->range.begin,
                    "could not resolve a selector name");
        return 0;
    }
    if (robo_css_token_value_copy(token, buffer, length, &written, NULL) != ROBO_OK) {
        robo_free(buffer);
        select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, token->range.begin,
                    "a selector name could not be resolved");
        return 0;
    }
    slice = robo_css_selector_list_intern(state->list, buffer, written);
    robo_free(buffer);
    if (robo_css_slice_is_absent(slice)) {
        select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, token->range.begin,
                    "could not record a selector name");
        return 0;
    }
    *out = slice;
    return 1;
}

/* ------------------------------------------------------------------ */
/* An+B                                                                */
/* ------------------------------------------------------------------ */

typedef struct anb_cursor {
    const uint8_t *bytes;
    size_t length;
    size_t at;
} anb_cursor_t;

/* An element index above this cannot occur, and the bound keeps the arithmetic
 * in a plain int. */
#define ANB_VALUE_MAX 1000000000L

static void anb_skip_space(anb_cursor_t *cursor)
{
    while (cursor->at < cursor->length &&
           robo_css_ascii_is_space(cursor->bytes[cursor->at])) {
        cursor->at++;
    }
}

static int anb_at_end(const anb_cursor_t *cursor)
{
    return cursor->at >= cursor->length;
}

static int anb_peek(const anb_cursor_t *cursor)
{
    return anb_at_end(cursor) ? -1 : (int)cursor->bytes[cursor->at];
}

/** Match @p word exactly, ASCII case-insensitively, and step over it. */
static int anb_is_keyword(anb_cursor_t *cursor, const char *word)
{
    size_t length = strlen(word);
    size_t i;

    if (cursor->length - cursor->at != length) {
        return 0;
    }
    for (i = 0u; i < length; i++) {
        if (robo_css_ascii_lower(cursor->bytes[cursor->at + i]) != (uint8_t)word[i]) {
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
static int anb_digits(anb_cursor_t *cursor, long *out)
{
    long value = 0;
    size_t digits = 0u;

    while (cursor->at < cursor->length &&
           robo_css_ascii_is_digit(cursor->bytes[cursor->at])) {
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

robo_result_t robo_css_an_plus_b_parse(const char *text,
                                       size_t length,
                                       robo_css_an_plus_b_t *out,
                                       robo_css_diag_t *diag)
{
    anb_cursor_t cursor;
    int sign = 1;
    int had_digits;
    long value = 0;

    /* Empty text is a *parse* failure, not a caller mistake: `:nth-child()`
     * hands over whatever the author wrote between the parentheses, and an
     * empty argument is something the author can write. A missing buffer or
     * missing output is the caller's bug. */
    if (!out || !text) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "no An+B text to parse");
    }
    out->a = 0;
    out->b = 0;
    cursor.bytes = (const uint8_t *)text;
    cursor.length = length;
    cursor.at = 0u;

    anb_skip_space(&cursor);
    if (anb_at_end(&cursor)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_PARSE_ERROR,
                             "empty An+B argument");
    }
    if (anb_is_keyword(&cursor, "odd")) {
        out->a = 2;
        out->b = 1;
    } else if (anb_is_keyword(&cursor, "even")) {
        out->a = 2;
        out->b = 0;
    } else {
        int next = anb_peek(&cursor);

        if (next == '+' || next == '-') {
            sign = (next == '-') ? -1 : 1;
            cursor.at++;
        }
        had_digits = anb_digits(&cursor, &value);
        next = anb_peek(&cursor);

        if (next == 'n' || next == 'N') {
            cursor.at++;
            /* `n` is `1n`, and a bare sign with no digits is `+n` / `-n`. */
            out->a = had_digits ? (int)(sign * value) : sign;
            out->b = 0;
            /* An offset is optional: `2n` means `2n+0`. */
            {
                size_t before = cursor.at;
                int sign_char;

                anb_skip_space(&cursor);
                sign_char = anb_peek(&cursor);
                if (sign_char == '+' || sign_char == '-') {
                    int b_sign = (sign_char == '-') ? -1 : 1;
                    long b_value = 0;

                    cursor.at++;
                    anb_skip_space(&cursor);
                    if (!anb_digits(&cursor, &b_value)) {
                        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR,
                                             ROBO_PARSE_ERROR,
                                             "an An+B offset sign must be followed by digits");
                    }
                    out->b = (int)(b_sign * b_value);
                } else {
                    cursor.at = before;
                }
            }
        } else if (had_digits) {
            out->a = 0;
            out->b = (int)(sign * value);
        } else {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_PARSE_ERROR,
                                 "an An+B expression needs an integer, `n`, `odd` or `even`");
        }
    }

    anb_skip_space(&cursor);
    if (!anb_at_end(&cursor)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_PARSE_ERROR,
                             "unexpected characters after the An+B expression");
    }
    return ROBO_OK;
}

int robo_css_an_plus_b_matches(const robo_css_an_plus_b_t *form, int index)
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

static int select_parse_list(select_state_t *state);
static int select_parse_sub(select_state_t *parent,
                            robo_css_selector_list_t *out,
                            size_t argument_begin,
                            size_t argument_length);

/* ------------------------------------------------------------------ */
/* Attribute selectors                                                 */
/* ------------------------------------------------------------------ */

static int select_parse_attribute(select_state_t *state,
                                  robo_css_compound_selector_t *compound)
{
    robo_css_token_t token;
    robo_css_simple_selector_t simple = ROBO_CSS_CLEARED;
    robo_css_slice_t attribute_name;
    robo_css_slice_t value = robo_css_slice_empty();
    robo_css_attribute_match_t match = ROBO_CSS_ATTRIBUTE_MATCH_NONE;
    robo_css_attribute_case_t case_flag = ROBO_CSS_ATTRIBUTE_CASE_DEFAULT;
    int value_was_string = 0;
    size_t begin;
    size_t extent;

    if (!select_peek(state, &token)) {
        return 0;
    }
    begin = token.range.begin;
    if (!select_advance(state, NULL)) { /* consume `[` */
        return 0;
    }
    select_skip_comments(state, NULL);

    if (!select_peek(state, &token) || token.type != ROBO_CSS_TOKEN_IDENT) {
        select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, begin,
                    "`[` must be followed by an attribute name");
        return 0;
    }
    if (!select_intern(state, &token, &attribute_name, 0)) {
        return 0;
    }
    if (!select_advance(state, NULL)) {
        return 0;
    }
    select_skip_comments(state, NULL);

    if (!select_peek(state, &token)) {
        return 0;
    }
    if (token.type == ROBO_CSS_TOKEN_RIGHT_SQUARE) {
        if (!select_advance(state, NULL)) { /* presence only: `[disabled]` */
            return 0;
        }
    } else {
        if (token.type != ROBO_CSS_TOKEN_DELIM || token.text.length != 1u) {
            select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, token.range.begin,
                        "expected `]` or an attribute comparison operator");
            return 0;
        }
        switch ((char)token.text.bytes[0]) {
        case '=':
            match = ROBO_CSS_ATTRIBUTE_MATCH_EQUALS;
            break;
        case '~':
            match = ROBO_CSS_ATTRIBUTE_MATCH_INCLUDES;
            break;
        case '|':
            match = ROBO_CSS_ATTRIBUTE_MATCH_DASH;
            break;
        case '^':
            match = ROBO_CSS_ATTRIBUTE_MATCH_PREFIX;
            break;
        case '$':
            match = ROBO_CSS_ATTRIBUTE_MATCH_SUFFIX;
            break;
        case '*':
            match = ROBO_CSS_ATTRIBUTE_MATCH_SUBSTRING;
            break;
        default:
            select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, token.range.begin,
                        "unknown attribute comparison operator");
            return 0;
        }
        if (!select_advance(state, NULL)) {
            return 0;
        }
        if (match != ROBO_CSS_ATTRIBUTE_MATCH_EQUALS) {
            select_skip_comments(state, NULL);
            if (!select_peek(state, &token) || !select_is_delim(token, '=')) {
                select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, begin,
                            "an attribute operator must end in `=`");
                return 0;
            }
            if (!select_advance(state, NULL)) {
                return 0;
            }
        }
        select_skip_comments(state, NULL);

        if (!select_peek(state, &token)) {
            return 0;
        }
        if (token.type == ROBO_CSS_TOKEN_STRING) {
            value_was_string = 1;
        } else if (token.type != ROBO_CSS_TOKEN_IDENT) {
            /* An unquoted value must be an identifier, so `[a=1]` is not a
             * comparison against the string "1". */
            select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, token.range.begin,
                        "an unquoted attribute value must be an identifier; quote it otherwise");
            return 0;
        }
        if (!select_intern(state, &token, &value, 1)) {
            return 0;
        }
        if (!select_advance(state, NULL)) {
            return 0;
        }
        select_skip_comments(state, NULL);

        if (!select_peek(state, &token)) {
            return 0;
        }
        if (token.type == ROBO_CSS_TOKEN_IDENT &&
            (select_token_equals_ci(token, "i") || select_token_equals_ci(token, "s"))) {
            case_flag = select_token_equals_ci(token, "i")
                            ? ROBO_CSS_ATTRIBUTE_CASE_INSENSITIVE
                            : ROBO_CSS_ATTRIBUTE_CASE_SENSITIVE;
            if (!select_advance(state, NULL)) {
                return 0;
            }
            select_skip_comments(state, NULL);
            if (!select_peek(state, &token)) {
                return 0;
            }
        }
        if (token.type != ROBO_CSS_TOKEN_RIGHT_SQUARE) {
            select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, token.range.begin,
                        "an attribute selector must end with `]`");
            return 0;
        }
        if (!select_advance(state, NULL)) {
            return 0;
        }
    }

    extent = robo_css_tokenizer_offset(&state->tokenizer) - begin;
    if (robo_css_simple_selector_make(&simple, ROBO_CSS_SIMPLE_SELECTOR_ATTRIBUTE,
                                      robo_css_slice_empty(), NULL) != ROBO_OK ||
        robo_css_simple_selector_set_attribute(&simple, attribute_name, match, value,
                                               case_flag, NULL) != ROBO_OK) {
        select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, begin,
                    "could not describe an attribute selector");
        return 0;
    }
    simple.range = select_range(state, robo_css_range_span(begin, extent));
    simple.value_was_string = value_was_string;
    if (robo_css_compound_selector_append(compound, &simple, NULL) != ROBO_OK) {
        select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, begin,
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
static int select_is_legacy_pseudo_element(robo_css_slice_t name)
{
    static const char *const legacy[] = {
        "before", "after", "first-line", "first-letter"
    };
    size_t i;

    for (i = 0u; i < sizeof(legacy) / sizeof(legacy[0]); i++) {
        if (robo_css_bytes_equal_ascii_ci(name.bytes, name.length, legacy[i])) {
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
static int select_scan_argument(select_state_t *state,
                                size_t *out_begin,
                                size_t *out_length)
{
    size_t begin = robo_css_tokenizer_offset(&state->tokenizer);
    size_t end = begin;
    size_t depth = 1u;
    robo_css_token_t token;

    for (;;) {
        if (!select_peek(state, &token)) {
            return 0;
        }
        if (token.type == ROBO_CSS_TOKEN_EOF) {
            select_fail(state, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF, begin,
                        "a functional pseudo-class was never closed");
            return 0;
        }
        if (token.type == ROBO_CSS_TOKEN_LEFT_PAREN ||
            token.type == ROBO_CSS_TOKEN_FUNCTION) {
            depth++;
        } else if (token.type == ROBO_CSS_TOKEN_RIGHT_PAREN) {
            depth--;
            if (depth == 0u) {
                end = token.range.begin;
                if (!select_advance(state, NULL)) {
                    return 0;
                }
                break;
            }
        }
        if (!select_advance(state, NULL)) {
            return 0;
        }
    }
    *out_begin = begin;
    *out_length = end > begin ? end - begin : 0u;
    return 1;
}

static int select_parse_pseudo(select_state_t *state,
                               robo_css_compound_selector_t *compound)
{
    robo_css_token_t token;
    robo_css_simple_selector_t simple = ROBO_CSS_CLEARED;
    robo_css_slice_t name;
    robo_css_slice_t argument = robo_css_slice_empty();
    size_t begin;
    size_t simple_index;
    size_t argument_begin = 0u;
    size_t argument_length = 0u;
    int is_element = 0;
    int has_argument = 0;

    if (!select_peek(state, &token)) {
        return 0;
    }
    begin = token.range.begin;
    if (!select_advance(state, NULL)) { /* consume `:` */
        return 0;
    }
    if (!select_peek(state, &token)) {
        return 0;
    }
    if (token.type == ROBO_CSS_TOKEN_COLON) {
        is_element = 1;
        if (!select_advance(state, NULL)) {
            return 0;
        }
    }

    if (!select_peek(state, &token)) {
        return 0;
    }
    if (token.type == ROBO_CSS_TOKEN_FUNCTION) {
        if (!select_intern(state, &token, &name, 0)) {
            return 0;
        }
        if (!select_advance(state, NULL)) {
            return 0;
        }
        if (!select_scan_argument(state, &argument_begin, &argument_length)) {
            return 0;
        }
        has_argument = 1;
    } else if (token.type == ROBO_CSS_TOKEN_IDENT) {
        if (!select_intern(state, &token, &name, 0)) {
            return 0;
        }
        if (!select_advance(state, NULL)) {
            return 0;
        }
    } else {
        /* Whitespace here is refused rather than stepped over: `a: hover` is not
         * a pseudo-class in any CSS, and reading it as one would apply rules a
         * browser would drop. */
        select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, begin,
                    "`:` must be followed by a pseudo-class or pseudo-element name");
        return 0;
    }

    /* A functional pseudo-class takes an argument and no pseudo-element
     * does, so the legacy spelling only reaches the pseudo-element kind when
     * the name stands alone. */
    if (!is_element && !has_argument && select_is_legacy_pseudo_element(name)) {
        is_element = 1;
    }

    if (robo_css_simple_selector_make(
            &simple,
            is_element ? ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT
                       : ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS,
            name, NULL) != ROBO_OK) {
        select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, begin,
                    "could not build a pseudo selector");
        return 0;
    }
    simple.range = select_range(
        state, robo_css_range_span(begin,
                                   robo_css_tokenizer_offset(&state->tokenizer) - begin));

    if (has_argument) {
        const robo_css_source_t *source = robo_css_tokenizer_source(&state->tokenizer);

        /* The raw argument is recorded like any other name: interned, so that
         * nothing a caller keeps points into a buffer it does not own. */
        argument = robo_css_selector_list_intern(
            state->list, robo_css_source_bytes(source) + argument_begin, argument_length);
        if (argument_length > 0u && robo_css_slice_is_absent(argument)) {
            select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, begin,
                        "could not record a pseudo-class argument");
            return 0;
        }
        if (robo_css_simple_selector_set_argument(&simple, argument, NULL) != ROBO_OK) {
            select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, begin,
                        "could not record a pseudo-class argument");
            return 0;
        }
    }

    if (!is_element) {
        int is_not = robo_css_simple_selector_is_pseudo_named(&simple, "not");
        int is_nth = robo_css_simple_selector_is_pseudo_named(&simple, "nth-child") ||
                     robo_css_simple_selector_is_pseudo_named(&simple, "nth-last-child");

        if ((is_not || is_nth) && !has_argument) {
            select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, begin,
                        "this pseudo-class requires a parenthesised argument");
            return 0;
        }
        if (is_nth) {
            robo_css_an_plus_b_t form;

            if (robo_css_an_plus_b_parse((const char *)robo_css_source_bytes(
                                             robo_css_tokenizer_source(&state->tokenizer)) +
                                             argument_begin,
                                         argument_length, &form, NULL) != ROBO_OK) {
                select_fail(state, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, begin,
                            "an An+B argument must be an integer, `odd`, `even` or `An+B`");
                return 0;
            }
            /* `simple` is a copy the compound now owns, so the parsed form is
             * recorded through the compound's own array. */
            if (robo_css_compound_selector_append(compound, &simple, NULL) != ROBO_OK) {
                select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, begin,
                            "could not record a pseudo selector");
                return 0;
            }
            simple_index = robo_css_compound_selector_count(compound) - 1u;
            if (robo_css_simple_selector_set_nth(&compound->items[simple_index], form.a,
                                                 form.b, NULL) != ROBO_OK) {
                select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, begin,
                            "could not record an An+B argument");
                return 0;
            }
            return 1;
        }
        if (is_not) {
            robo_css_selector_list_t nested = ROBO_CSS_CLEARED;

            if (robo_css_compound_selector_append(compound, &simple, NULL) != ROBO_OK) {
                select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, begin,
                            "could not record a pseudo selector");
                return 0;
            }
            simple_index = robo_css_compound_selector_count(compound) - 1u;
            if (select_parse_sub(state, &nested, argument_begin, argument_length)) {
                /* Attaching moves the nested list in, arena included, which is
                 * why a failure here has to release it rather than drop it. */
                if (robo_css_compound_selector_attach_nested(compound, simple_index,
                                                             &nested, NULL) == ROBO_OK) {
                    return 1;
                }
                robo_css_selector_list_dispose(&nested);
                select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, begin,
                            "could not record a `:not()` argument");
            }
            return 0;
        }
    }

    if (robo_css_compound_selector_append(compound, &simple, NULL) != ROBO_OK) {
        select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, begin,
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
static int select_parse_compound(select_state_t *state,
                                 robo_css_compound_selector_t *compound)
{
    robo_css_token_t token;

    select_skip_comments(state, NULL);
    if (!select_peek(state, &token)) {
        return 0;
    }

    /* A type selector or the universal, first and at most once. */
    if (token.type == ROBO_CSS_TOKEN_IDENT) {
        robo_css_simple_selector_t simple = ROBO_CSS_CLEARED;
        robo_css_slice_t name;

        if (!select_intern(state, &token, &name, 0)) {
            return 0;
        }
        if (robo_css_simple_selector_make(&simple, ROBO_CSS_SIMPLE_SELECTOR_TYPE, name,
                                          NULL) != ROBO_OK) {
            select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, token.range.begin,
                        "could not build a type selector");
            return 0;
        }
        simple.range = select_range(state, token.range);
        if (!select_advance(state, NULL) ||
            robo_css_compound_selector_append(compound, &simple, NULL) != ROBO_OK) {
            select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, token.range.begin,
                        "could not record a type selector");
            return 0;
        }
    } else if (select_is_delim(token, '*')) {
        robo_css_simple_selector_t simple = ROBO_CSS_CLEARED;

        if (robo_css_simple_selector_make(&simple, ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL,
                                          robo_css_slice_empty(), NULL) != ROBO_OK) {
            select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, token.range.begin,
                        "could not build the universal selector");
            return 0;
        }
        simple.range = select_range(state, token.range);
        if (!select_advance(state, NULL) ||
            robo_css_compound_selector_append(compound, &simple, NULL) != ROBO_OK) {
            select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, token.range.begin,
                        "could not record the universal selector");
            return 0;
        }
    }

    for (;;) {
        if (!select_peek(state, &token)) {
            return 0;
        }
        if (select_is_delim(token, '.')) {
            robo_css_simple_selector_t simple = ROBO_CSS_CLEARED;
            robo_css_slice_t name;
            size_t dot_begin = token.range.begin;

            if (!select_advance(state, NULL) || !select_peek(state, &token)) {
                return 0;
            }
            if (token.type != ROBO_CSS_TOKEN_IDENT) {
                select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, dot_begin,
                            "`.` must be followed by a class name");
                return 0;
            }
            if (!select_intern(state, &token, &name, 0)) {
                return 0;
            }
            if (robo_css_simple_selector_make(&simple, ROBO_CSS_SIMPLE_SELECTOR_CLASS,
                                              name, NULL) != ROBO_OK) {
                select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, token.range.begin,
                            "could not build a class selector");
                return 0;
            }
            /* From the `.`, not from the name: a simple selector's range is the
             * extent of the *selector*, and for `.card` that begins at the dot.
             * Otherwise a compound's derived range would start mid-selector and
             * every diagnostic built on it would point a column to the right. */
            simple.range = select_range(state, robo_css_range_span(
                                                   dot_begin,
                                                   token.range.end - dot_begin));
            if (!select_advance(state, NULL) ||
                robo_css_compound_selector_append(compound, &simple, NULL) != ROBO_OK) {
                select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, token.range.begin,
                            "could not record a class selector");
                return 0;
            }
            continue;
        }
        if (token.type == ROBO_CSS_TOKEN_HASH) {
            robo_css_simple_selector_t simple = ROBO_CSS_CLEARED;
            robo_css_slice_t name;

            /* `#123` is a hash token but not an id selector: only the
             * identifier-restricted form names an element. */
            if (token.hash != ROBO_CSS_HASH_TYPE_ID) {
                select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, token.range.begin,
                            "`#` must be followed by an identifier");
                return 0;
            }
            if (!select_intern(state, &token, &name, 0)) {
                return 0;
            }
            if (robo_css_simple_selector_make(&simple, ROBO_CSS_SIMPLE_SELECTOR_ID, name,
                                              NULL) != ROBO_OK) {
                select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, token.range.begin,
                            "could not build an id selector");
                return 0;
            }
            simple.range = select_range(state, token.range);
            if (!select_advance(state, NULL) ||
                robo_css_compound_selector_append(compound, &simple, NULL) != ROBO_OK) {
                select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, token.range.begin,
                            "could not record an id selector");
                return 0;
            }
            continue;
        }
        if (token.type == ROBO_CSS_TOKEN_LEFT_SQUARE) {
            if (!select_parse_attribute(state, compound)) {
                return 0;
            }
            continue;
        }
        if (token.type == ROBO_CSS_TOKEN_COLON) {
            if (!select_parse_pseudo(state, compound)) {
                return 0;
            }
            continue;
        }
        if (token.type == ROBO_CSS_TOKEN_IDENT) {
            /* A comment separates the tokens but is not whitespace, so an
             * identifier after a type selector really is a second type selector
             * in one compound — invalid, and refused rather than merged. */
            select_fail(state, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, token.range.begin,
                        "a compound selector may hold only one type selector");
            return 0;
        }
        break;
    }

    if (robo_css_compound_selector_count(compound) == 0u) {
        select_fail(state, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX,
                    robo_css_tokenizer_offset(&state->tokenizer), "expected a selector");
        return 0;
    }
    robo_css_compound_selector_seal_range(compound);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Complex selectors                                                   */
/* ------------------------------------------------------------------ */

static int select_parse_complex(select_state_t *state,
                                robo_css_complex_selector_t *complex)
{
    for (;;) {
        robo_css_compound_selector_t compound = ROBO_CSS_CLEARED;
        robo_css_token_t token;
        int saw_whitespace = 0;

        if (!select_parse_compound(state, &compound)) {
            robo_css_compound_selector_dispose(&compound);
            return 0;
        }
        if (robo_css_complex_selector_append_compound(complex, &compound, NULL) !=
            ROBO_OK) {
            robo_css_compound_selector_dispose(&compound);
            select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, 0u,
                        "could not record a compound selector");
            return 0;
        }

        select_skip_comments(state, &saw_whitespace);
        if (!select_peek(state, &token)) {
            return 0;
        }
        if (token.type == ROBO_CSS_TOKEN_COMMA || token.type == ROBO_CSS_TOKEN_EOF) {
            return 1;
        }

        {
            robo_css_combinator_t combinator = ROBO_CSS_COMBINATOR_NONE;
            size_t begin = token.range.begin;

            if (select_is_delim(token, '>')) {
                combinator = ROBO_CSS_COMBINATOR_CHILD;
            } else if (select_is_delim(token, '+')) {
                combinator = ROBO_CSS_COMBINATOR_NEXT_SIBLING;
            } else if (select_is_delim(token, '~')) {
                combinator = ROBO_CSS_COMBINATOR_SUBSEQUENT_SIBLING;
            } else if (select_is_delim(token, '|')) {
                if (!select_advance(state, NULL) || !select_peek(state, &token)) {
                    return 0;
                }
                if (!select_is_delim(token, '|')) {
                    select_fail(state, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, begin,
                                "the column combinator is `||`");
                    return 0;
                }
                combinator = ROBO_CSS_COMBINATOR_COLUMN;
            }

            if (combinator != ROBO_CSS_COMBINATOR_NONE) {
                if (!select_advance(state, NULL)) {
                    return 0;
                }
                if (robo_css_complex_selector_append_combinator(complex, combinator,
                                                                NULL) != ROBO_OK) {
                    select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, begin,
                                "could not record a combinator");
                    return 0;
                }
                continue;
            }
        }

        if (saw_whitespace) {
            /* The one combinator CSS spells with a space. */
            if (robo_css_complex_selector_append_combinator(
                    complex, ROBO_CSS_COMBINATOR_DESCENDANT, NULL) != ROBO_OK) {
                select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, token.range.begin,
                            "could not record a descendant combinator");
                return 0;
            }
            continue;
        }
        select_fail(state, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, token.range.begin,
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
static int select_list_seal(select_state_t *state)
{
    size_t count = robo_css_selector_list_count(state->list);
    const robo_css_complex_selector_t *first;
    const robo_css_complex_selector_t *last;

    if (count == 0u) {
        state->list->range = robo_css_range_at(0u);
        return 1;
    }
    first = robo_css_selector_list_at(state->list, 0u);
    last = robo_css_selector_list_at(state->list, count - 1u);
    state->list->range =
        robo_css_range_span(first->range.begin, last->range.end - first->range.begin);
    return 1;
}

static int select_parse_list(select_state_t *state)
{
    int saw_whitespace = 0;
    robo_css_token_t token;

    select_skip_comments(state, &saw_whitespace);
    if (!select_peek(state, &token)) {
        return 0;
    }
    if (token.type == ROBO_CSS_TOKEN_EOF) {
        select_fail(state, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, 0u,
                    "an empty selector list is not a selector");
        return 0;
    }

    for (;;) {
        robo_css_complex_selector_t complex = ROBO_CSS_CLEARED;

        if (!select_parse_complex(state, &complex)) {
            robo_css_complex_selector_dispose(&complex);
            return 0;
        }
        if (!robo_css_complex_selector_is_well_formed(&complex)) {
            robo_css_complex_selector_dispose(&complex);
            select_fail(state, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, 0u,
                        "a selector ended with a trailing combinator");
            return 0;
        }
        /* A complex selector's extent is its compounds' — derived from the tree
         * rather than tracked beside it, so the two cannot disagree. */
        {
            size_t compound_count =
                robo_css_complex_selector_compound_count(&complex);
            const robo_css_compound_selector_t *first =
                robo_css_complex_selector_compound_at(&complex, 0u);
            const robo_css_compound_selector_t *last =
                robo_css_complex_selector_compound_at(&complex, compound_count - 1u);

            complex.range = robo_css_range_span(first->range.begin,
                                                last->range.end - first->range.begin);
        }
        if (robo_css_selector_list_append(state->list, &complex, NULL) != ROBO_OK) {
            robo_css_complex_selector_dispose(&complex);
            select_fail(state, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, 0u,
                        "could not record a selector");
            return 0;
        }

        select_skip_comments(state, &saw_whitespace);
        if (!select_peek(state, &token)) {
            return 0;
        }
        if (token.type == ROBO_CSS_TOKEN_EOF) {
            return select_list_seal(state);
        }
        if (token.type != ROBO_CSS_TOKEN_COMMA) {
            select_fail(state, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, token.range.begin,
                        "expected `,` or the end of the selector list");
            return 0;
        }
        if (!select_advance(state, NULL)) {
            return 0;
        }
        select_skip_comments(state, &saw_whitespace);
        if (!select_peek(state, &token)) {
            return 0;
        }
        if (token.type == ROBO_CSS_TOKEN_EOF || token.type == ROBO_CSS_TOKEN_COMMA) {
            select_fail(state, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, token.range.begin,
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
static robo_result_t select_run(robo_css_selector_list_t *out,
                                const uint8_t *bytes,
                                size_t length,
                                size_t base_offset,
                                robo_css_error_t *out_error,
                                size_t depth,
                                robo_css_diag_t *diag)
{
    robo_css_source_t source;
    select_state_t state;
    robo_result_t result;

    /* Initialised first, exactly as the 9.3 grammar initialises its output: a
     * caller may hand over an unused record, and a caller that re-parses into a
     * live list disposes it itself first (which is what
     * robo_css_style_rule_parse_selectors does). */
    robo_css_selector_list_init(out);
    if (out_error) {
        robo_css_error_init(out_error);
    }
    robo_css_source_init(&source);
    if (robo_css_source_set_memory(&source, bytes, length, NULL) != ROBO_OK) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "the selector text could not be configured as a source");
    }
    if (robo_css_selector_list_reserve_names(out, length * 2u + 8u, NULL) != ROBO_OK) {
        robo_css_source_dispose(&source);
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_OUT_OF_MEMORY,
                             "could not reserve the selector name arena");
    }

    memset(&state, 0, sizeof(state));
    state.list = out;
    state.base_offset = base_offset;
    state.error = out_error;
    state.depth = depth;
    if (robo_css_tokenizer_init(&state.tokenizer, &source, NULL) != ROBO_OK) {
        robo_css_source_dispose(&source);
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "the selector text could not be tokenized");
    }

    result = select_parse_list(&state) ? ROBO_OK : ROBO_PARSE_ERROR;

    robo_css_tokenizer_dispose(&state.tokenizer);
    robo_css_source_dispose(&source);

    if (result != ROBO_OK) {
        /* All or nothing: a selector list with one bad selector is not a
         * selector list, and half of one would silently match a subset of what
         * the author wrote. */
        robo_css_selector_list_dispose(out);
        if (out_error && !robo_css_error_is_error(out_error)) {
            (void)robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX,
                                     ROBO_PARSE_ERROR, NULL,
                                     "the selector list could not be parsed");
        }
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_PARSE_ERROR,
                             "the selector list could not be parsed");
    }
    return ROBO_OK;
}

/*
 * One nested level. @p out's arena is fresh, so the argument's names go into the
 * nested list's own arena — which is what makes a nested list disposable exactly
 * like a top-level one, and what keeps the parent's arena from growing while its
 * own names are already recorded.
 */
static int select_parse_sub(select_state_t *parent,
                            robo_css_selector_list_t *out,
                            size_t argument_begin,
                            size_t argument_length)
{
    const robo_css_source_t *parent_source =
        robo_css_tokenizer_source(&parent->tokenizer);
    const uint8_t *bytes = robo_css_source_bytes(parent_source);
    size_t child_depth = parent->depth + 1u;
    robo_result_t result;

    if (!out) {
        select_fail(parent, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, argument_begin,
                    "a nested selector list needs somewhere to go");
        return 0;
    }
    if (!bytes) {
        select_fail(parent, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, argument_begin,
                    "a nested selector list has no source");
        return 0;
    }
    if (child_depth > ROBO_CSS_MAX_SELECTOR_NESTING) {
        select_fail(parent, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, argument_begin,
                    "`:not()` nests deeper than this build parses");
        return 0;
    }
    result = select_run(out, bytes + argument_begin, argument_length,
                        parent->base_offset + argument_begin, parent->error,
                        child_depth, NULL);
    if (result != ROBO_OK) {
        /* The nested parse already recorded the precise reason in the shared
         * error record; latching the parent keeps the outer call honest without
         * overwriting the better message with a worse one. */
        parent->failed = 1;
        return 0;
    }
    return 1;
}

robo_result_t robo_css_selector_list_parse_text(robo_css_selector_list_t *out,
                                                const char *text,
                                                robo_css_error_t *out_error,
                                                robo_css_diag_t *diag)
{
    if (!out || !text) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "output or text is NULL");
    }
    return select_run(out, (const uint8_t *)text, strlen(text), 0u, out_error, 0u,
                      diag);
}

robo_result_t robo_css_selector_list_parse_slice(robo_css_selector_list_t *out,
                                                 robo_css_slice_t text,
                                                 robo_css_error_t *out_error,
                                                 robo_css_diag_t *diag)
{
    if (!out || robo_css_slice_is_empty(text)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "output is NULL or the selector text is empty");
    }
    return select_run(out, text.bytes, text.length, 0u, out_error, 0u, diag);
}

robo_result_t robo_css_selector_list_parse_range(robo_css_selector_list_t *out,
                                                 const robo_css_source_t *source,
                                                 robo_css_range_t range,
                                                 robo_css_error_t *out_error,
                                                 robo_css_diag_t *diag)
{
    robo_css_slice_t text;

    if (!out || !source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "output or source is NULL");
    }
    if (robo_css_source_range_slice(source, range, &text) != ROBO_OK) {
        robo_css_selector_list_init(out);
        if (out_error) {
            robo_css_error_init(out_error);
            (void)robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_MALFORMED_RULE,
                                     ROBO_INVALID_ARGUMENT, NULL,
                                     "the selector range does not lie within the source");
        }
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "the selector range does not lie within the source");
    }
    if (robo_css_slice_is_empty(text)) {
        robo_css_selector_list_init(out);
        if (out_error) {
            (void)robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX,
                                     ROBO_PARSE_ERROR, NULL,
                                     "an empty selector list is not a selector");
        }
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_PARSE_ERROR,
                             "an empty selector list is not a selector");
    }
    return select_run(out, text.bytes, text.length, range.begin, out_error, 0u, diag);
}

robo_result_t robo_css_style_rule_parse_selectors(robo_css_style_rule_t *rule,
                                                  const robo_css_source_t *source,
                                                  robo_css_error_t *out_error,
                                                  robo_css_diag_t *diag)
{
    robo_css_selector_list_t parsed = ROBO_CSS_CLEARED;
    robo_result_t result;

    if (!rule || !source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "rule or source is NULL");
    }
    result = robo_css_selector_list_parse_range(&parsed, source, rule->prelude_range,
                                               out_error, diag);
    if (result != ROBO_OK) {
        /* The rule keeps an empty selector list and is otherwise untouched:
         * Phase 9.3 already decided that malformed CSS produces a model rather
         * than a failed parse, and a rule whose prelude is not a selector list is
         * still a rule. */
        return result;
    }
    /* Move, arena included — a parsed name points into the arena it was interned
     * in, so the two have to travel together. */
    robo_css_selector_list_dispose(&rule->selectors);
    rule->selectors = parsed;
    return ROBO_OK;
}
