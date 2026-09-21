/*
 * RoboCSS parser grammar (Phase 9.3) — see css/docs/Parser.md.
 *
 * 9.1 built the cursor and the model; 9.2 built the machine that turns bytes
 * into tokens. This is the grammar: it consumes real tokens and builds real
 * rules, declarations and component values, and it interprets none of them.
 *
 *   source → tokenizer → *this* → stylesheet / declaration list
 *
 * Five decisions shape everything below.
 *
 * 1. It consumes the tokenizer, never the bytes.
 *
 *    There is no byte-level scanning here: no peek at a byte, no re-scan of a
 *    value's text. Phase 9.2's contract — that a token's `text` and `range` can
 *    never disagree — is only worth what it costs if the consumer above it
 *    trusts it, and a parser that re-read bytes would be a second tokenizer
 *    wearing a different name.
 *
 * 2. Whitespace is a component value; comments are not.
 *
 *    A descendant combinator *is* whitespace, so a prelude that dropped it
 *    could not tell `a b` from `ab` and Phase 9.4 would have nothing to read.
 *    Whitespace is therefore preserved inside preludes and values and trimmed
 *    only at their ends (a trailing descendant combinator is not a selector,
 *    and a value's surrounding whitespace is not part of it). Comments are
 *    skipped everywhere: they are not component values in CSS Syntax, and a
 *    phantom comment node is something every later phase would have to filter.
 *
 * 3. A rule's extents are derived from its tree, not tracked beside it.
 *
 *    A prelude's range is the span of the component values that survived
 *    trimming, read off their own ranges. Tracking a range in parallel with the
 *    values is how a range and its tree end up disagreeing, and a range that
 *    disagrees with its tree is worse than no range.
 *
 * 4. Recovery is bounded, generic, and never semantic.
 *
 *    Bad declarations are skipped to the `;`, the enclosing `}`, or a balanced
 *    `{ … }` — the specification's "consume the remnants of a bad declaration"
 *    — and valid declarations after them are still parsed. A rule that cannot
 *    be built is dropped at a defined place. No recovery path consults what an
 *    at-rule *means*, because nothing here knows: `@media` is parsed exactly as
 *    `@whatever`, and an at-rule this build has never heard of is as valid as
 *    one it has.
 *
 * 5. Wrong input costs bounded stack, not correctness.
 *
 *    `calc(calc(calc(…` and `@media{@media{@media{…` both recurse, so both are
 *    bounded by RoboCssMaxNesting — one budget covering rule nesting and
 *    value nesting together. Past the bound nothing recurses and nothing fails:
 *    the construct's remaining leaves are kept flat, a recoverable error is
 *    recorded, the stream still ends at a defined place, and disposal walks
 *    exactly the tree the bound allowed to be built.
 *
 * Errors follow the rule the cursor and the tokenizer already use: the *first*
 * problem is kept, later ones are counted but not recorded over it, and every
 * problem is a value. Malformed CSS never crashes and never terminates the
 * caller.
 */

#include "RoboCSS/RoboCSSDeclaration.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSS/RoboCSSSource.h"
#include "RoboCSS/RoboCSSStyleSheet.h"
#include "RoboCSS/RoboCSSToken.h"
#include "RoboCSS/RoboCSSTokenizer.h"
#include "RoboCSS/RoboCSSValue.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

/**
 * One parse.
 *
 * The grammar owns a tokenizer rather than a `CssParser`, and that is
 * deliberate: the 9.1 cursor is a *byte* view of one source (offset, line,
 * column, seek), while this machine works entirely in tokens and never needs a
 * byte offset of its own — every offset it reports comes from a token's range.
 * Holding both would mean two cursors to keep in step, and the token model
 * already promises its ranges are right. The 9.1 cursor keeps its own callers;
 * it is simply not what a token grammar needs.
 *
 * `origin` is validated and recorded so both entry points reject the same
 * origins the cursor rejects. `recoveries` counts every problem stepped over,
 * which is what makes "how bad was this stylesheet" answerable without
 * flattening the errors into one.
 */
typedef struct CssGrammar {
    const CssSource *source;
    CssTokenizer tokenizer;
    CssOrigin origin;
    int hasError;
    CssError error;
    size_t recoveries;
} CssGrammar;

/**
 * The part of a parse that a speculative pass must not be able to leave behind.
 *
 * One pass over an at-rule's block guesses whether the block holds rules or
 * descriptors, and the guess can be wrong — `@font-face { font-family: X }`
 * read as a rule list reports a malformed rule for input that is perfectly
 * legal. Saving the error state around the guess and restoring it when the
 * second reading succeeds is what keeps that from being reported as a defect in
 * the caller's stylesheet. The tokenizer needs no such treatment because the
 * second reading runs on one of its own.
 */
typedef struct CssGrammarState {
    int hasError;
    CssError error;
    size_t recoveries;
} CssGrammarState;

/* ------------------------------------------------------------------ */
/* Tokens                                                              */
/* ------------------------------------------------------------------ */

/** Fold the tokenizer's own error into the grammar's, exactly once each. */
static void GrammarAbsorbTokenizerError(CssGrammar *g)
{
    CssError error = RoboCssCleared;

    /* Taking it clears the tokenizer's slot, so the *next* problem it finds can
     * be reported too instead of being masked by the first for the rest of the
     * parse — and this grammar still records only the first. */
    if (!CssTokenizerTakeError(&g->tokenizer, &error)) {
        return;
    }
    g->recoveries++;
    if (!g->hasError) {
        g->hasError = 1;
        g->error = error;
    }
}

/** Record a problem at @p offset as the grammar's first problem, and count it. */
static void GrammarFailAt(CssGrammar *g,
                            size_t offset,
                            CssErrorKind kind,
                            Result code,
                            const char *message)
{
    CssPosition position = CssPositionAt(offset);

    g->recoveries++;
    if (g->hasError) {
        return; /* the first problem is the one that explains the rest */
    }
    if (g->source) {
        /* Line and column are a courtesy: failing to resolve them must not fail
         * the parse, and the offset is recorded either way. */
        (void)CssSourcePosition(g->source, offset, &position);
    }
    CssErrorSet(&g->error, kind, code, &position, message);
    g->hasError = 1;
}

/** Peek the next token without consuming it. */
static Result GrammarPeek(CssGrammar *g, CssToken *out)
{
    Result result = CssTokenizerPeek(&g->tokenizer, out, NULL);

    GrammarAbsorbTokenizerError(g);
    return result;
}

/** Consume the next token. */
static Result GrammarNext(CssGrammar *g, CssToken *out)
{
    Result result = CssTokenizerNext(&g->tokenizer, out, NULL);

    GrammarAbsorbTokenizerError(g);
    return result;
}

/** Consume whitespace and comments, leaving the next real token peeked. */
static void GrammarSkipTrivia(CssGrammar *g)
{
    (void)CssTokenizerSkipTrivia(&g->tokenizer, NULL);
    GrammarAbsorbTokenizerError(g);
}

/** Byte offset the cursor currently sits at. */
static size_t GrammarOffset(const CssGrammar *g)
{
    return CssTokenizerOffset(&g->tokenizer);
}

/** Snapshot the error state. */
static CssGrammarState GrammarStateSave(const CssGrammar *g)
{
    CssGrammarState state;

    state.hasError = g->hasError;
    state.error = g->error;
    state.recoveries = g->recoveries;
    return state;
}

/** Put the error state back, discarding what the speculative pass recorded. */
static void GrammarStateRestore(CssGrammar *g,
                                  const CssGrammarState *state)
{
    g->hasError = state->hasError;
    g->error = state->error;
    g->recoveries = state->recoveries;
}

/**
 * Adopt a pass that ran on its own state: count its problems, and record the
 * first of them when this parse has nothing recorded yet.
 */
static void GrammarAdoptError(CssGrammar *g,
                                const CssError *error,
                                size_t recoveries)
{
    g->recoveries += recoveries;
    if (!g->hasError && error && CssErrorIsError(error)) {
        g->error = *error;
        g->hasError = 1;
    }
}

/** Non-zero when @p token opens a nested construct (a block or a function). */
static int TokenOpensNesting(CssTokenType type)
{
    return CssTokenTypeIsBlockStart(type) ||
           type == RoboCssTokenFunction;
}

/* ------------------------------------------------------------------ */
/* Component values                                                    */
/* ------------------------------------------------------------------ */

/** Non-zero when @p value is a component value made of one token of @p type. */
static int ValueIsTokenOfType(const CssValue *value,
                                  CssTokenType type)
{
    return value && value->kind == RoboCssValueKindToken &&
           value->token.type == type;
}

/**
 * Bytes of @p token's text that spell its *name*: the text without the
 * delimiter the token's own type contributes (`rgb(` → `rgb`, `@media` →
 * `media`).
 *
 * Raw on purpose. The 9.1 model documents a rule's name as "as written", and a
 * resolved name would have to be materialised somewhere — which, for a borrowed
 * value type, means a scratch buffer per call, so a caller could never keep the
 * name it got. An author who writes `@\6d edia` gets `\6d edia`, which is what
 * the source says; anyone who needs the resolved spelling reads it off the
 * token, and the prelude keeps the token.
 */
static CssSlice TokenNameSlice(const CssToken *token)
{
    CssSlice text = CssTokenText(token);

    if (token->type == RoboCssTokenAtKeyword) {
        if (text.length > 0u && text.bytes[0] == (uint8_t)'@') {
            text.bytes += 1u;
            text.length -= 1u;
        }
        return text;
    }
    if (token->type == RoboCssTokenFunction) {
        if (text.length > 0u && text.bytes[text.length - 1u] == (uint8_t)'(') {
            text.length -= 1u;
        }
        return text;
    }
    return text;
}

/** The span the component values in @p value cover; empty when there are none. */
static CssRange ValueSpan(const CssValue *value)
{
    CssRange range = CssRangeAt(0u);

    if (!value || value->children.count == 0u) {
        return range;
    }
    range.begin = value->children.items[0].range.begin;
    range.end = value->children.items[value->children.count - 1u].range.end;
    return range;
}

/**
 * Record @p value's own extent as the span of its children.
 *
 * A LIST built by this grammar starts with no range of its own — it is a
 * container, and the ranges live on the leaves — so a caller that asks where a
 * parsed value *is* would otherwise get 0..0 and be wrong. The call is made
 * after trimming, because trimming is the last thing that moves an end.
 */
static void ValueSpanSelf(CssValue *value)
{
    if (value) {
        value->range = ValueSpan(value);
    }
}

/** Index of the last child that is not a whitespace token, or (size_t)-1. */
static size_t ValueLastSignificant(const CssValue *value)
{
    size_t index;

    if (!value) {
        return (size_t)-1;
    }
    for (index = value->children.count; index > 0u; index--) {
        if (!ValueIsTokenOfType(&value->children.items[index - 1u],
                                    RoboCssTokenWhitespace)) {
            return index - 1u;
        }
    }
    return (size_t)-1;
}

/** Index of the last non-whitespace child before @p before, or (size_t)-1. */
static size_t ValuePrevSignificant(const CssValue *value, size_t before)
{
    size_t index;

    if (!value || before == 0u) {
        return (size_t)-1;
    }
    for (index = before; index > 0u; index--) {
        if (!ValueIsTokenOfType(&value->children.items[index - 1u],
                                    RoboCssTokenWhitespace)) {
            return index - 1u;
        }
    }
    return (size_t)-1;
}

/** Dispose children from @p keep onwards, then forget them. */
static void ValueTruncate(CssValue *value, size_t keep)
{
    size_t index;

    if (!value) {
        return;
    }
    if (keep > value->children.count) {
        keep = value->children.count;
    }
    for (index = keep; index < value->children.count; index++) {
        CssValueDispose(&value->children.items[index]);
    }
    value->children.count = keep;
}

/**
 * Drop the first @p count children, shifting the rest down and blanking the
 * tail.
 *
 * Blanking matters: the moved-from slots would otherwise still hold the
 * children that are now earlier in the array, and a later dispose would release
 * each of them twice. A blank record is NONE, which dispose is a no-op on.
 */
static void ValueDropFront(CssValue *value, size_t count)
{
    size_t index;

    if (!value || count == 0u) {
        return;
    }
    if (count > value->children.count) {
        count = value->children.count;
    }
    for (index = 0u; index + count < value->children.count; index++) {
        value->children.items[index] = value->children.items[index + count];
    }
    for (; index < value->children.count; index++) {
        CssValueInit(&value->children.items[index]);
    }
    value->children.count -= count;
}

/**
 * Trim whitespace component values from both ends of @p value.
 *
 * "Both ends" is not cosmetic. A declaration's value runs from the colon to the
 * `;`, so it starts with the space the author wrote after the colon; a prelude
 * is whatever stood before the `{`. Keeping either would put a whitespace node
 * at the head of every value and every prelude in a stylesheet, and would make
 * "this value has exactly one component" false for `red`.
 */
static void ValueTrimWhitespace(CssValue *value)
{
    size_t leading = 0u;

    if (!value) {
        return;
    }
    while (leading < value->children.count &&
           ValueIsTokenOfType(&value->children.items[leading],
                                  RoboCssTokenWhitespace)) {
        leading++;
    }
    if (leading > 0u) {
        ValueDropFront(value, leading);
    }
    ValueTruncate(value, ValueLastSignificant(value) + 1u);
}

/**
 * Detect and remove a trailing `!important`, ignoring whitespace between the
 * two tokens (which CSS permits: `red ! important` is important), and report
 * whether one was found.
 *
 * The comparison goes through the token *value* accessors rather than the raw
 * bytes, so `!\69 mportant` and `!IMPORTANT` are recognised too — the tokenizer
 * already resolved those, and re-deciding here is how two layers come to
 * disagree.
 */
static int ValueTakeImportant(CssValue *value)
{
    size_t last = ValueLastSignificant(value);
    size_t bang;

    if (last == (size_t)-1) {
        return 0;
    }
    if (value->children.items[last].kind != RoboCssValueKindToken ||
        value->children.items[last].token.type != RoboCssTokenIdent ||
        !CssTokenValueEqualsAsciiCi(&value->children.items[last].token,
                                              "important")) {
        return 0;
    }
    bang = ValuePrevSignificant(value, last);
    if (bang == (size_t)-1 ||
        !ValueIsTokenOfType(&value->children.items[bang], RoboCssTokenDelim) ||
        !CssTokenValueEqualsAscii(&value->children.items[bang].token, "!")) {
        return 0;
    }
    ValueTruncate(value, bang);
    ValueTrimWhitespace(value);
    return 1;
}

/** Append @p token to @p list as a TOKEN component value. */
static Result ValueAppendToken(CssValue *list,
                                        const CssToken *token)
{
    CssValue value = RoboCssCleared;

    if (CssValueMakeToken(&value, token, NULL) != RoboOk) {
        return RoboInvalidArgument;
    }
    return CssValueAppend(list, &value, NULL);
}

/** A fresh, empty component-value LIST in @p out. */
static Result ValueMakeList(CssValue *out)
{
    CssValueInit(out);
    if (CssValueMakeList(out, NULL) != RoboOk) {
        return RoboOutOfMemory;
    }
    return RoboOk;
}

static Result GrammarConsumeComponentValue(CssGrammar *g,
                                                     CssValue *list,
                                                     size_t depth);

/**
 * Consume the tokens up to and including the one that closes the container
 * opened before this call, appending them to @p container.
 *
 * @p closeType is the token that ends it (`)`, `]` or `}`). Only that exact
 * type ends it: a mismatched closer is an ordinary component value, which is
 * what "consume a simple block" says and what makes `[a)` a block containing
 * `a` and `)`.
 *
 * @p flat is the over-depth path: nothing recurses, bracket tokens are elided
 * and counted rather than modelled, and only the leaves are kept.
 */
static Result GrammarConsumeContainer(CssGrammar *g,
                                               CssValue *container,
                                               CssTokenType closeType,
                                               size_t depth,
                                               int flat,
                                               CssRange *range,
                                               int *outClosed)
{
    size_t nesting = 1u;

    *outClosed = 0;
    for (;;) {
        CssToken token = RoboCssCleared;
        Result result = GrammarPeek(g, &token);

        if (result != RoboOk) {
            return result;
        }
        if (token.type == RoboCssTokenEof) {
            GrammarFailAt(g, range->begin, RoboCssErrorKindUnexpectedEof,
                            RoboInvalidArgument,
                            "a block or function was not closed before the end of the source");
            return RoboOk;
        }
        if (flat) {
            if (token.type == closeType) {
                if (nesting == 1u) {
                    (void)GrammarNext(g, &token);
                    range->end = token.range.end;
                    *outClosed = 1;
                    return RoboOk;
                }
                nesting--;
                (void)GrammarNext(g, &token);
                continue;
            }
            if (TokenOpensNesting(token.type)) {
                nesting++;
                (void)GrammarNext(g, &token);
                continue;
            }
            (void)GrammarNext(g, &token);
            if (token.type == RoboCssTokenComment) {
                continue;
            }
            result = ValueAppendToken(container, &token);
            if (result != RoboOk) {
                return result;
            }
            continue;
        }
        if (token.type == closeType) {
            (void)GrammarNext(g, &token);
            range->end = token.range.end;
            *outClosed = 1;
            return RoboOk;
        }
        if (token.type == RoboCssTokenComment) {
            (void)GrammarNext(g, &token);
            continue;
        }
        result = GrammarConsumeComponentValue(g, container, depth);
        if (result != RoboOk) {
            return result;
        }
    }
}

/** Consume a simple block whose opening token was already consumed. */
static Result GrammarConsumeBlock(CssGrammar *g,
                                           CssValue *list,
                                           const CssToken *open,
                                           size_t depth)
{
    CssValue block = RoboCssCleared;
    CssTokenType closeType = RoboCssTokenNone;
    CssRange range = open->range;
    int closed = 0;
    int flat = (depth + 1u > RoboCssMaxNesting);
    Result result;

    if (!CssTokenTypeMatchingClose(open->type, &closeType)) {
        /* Not a block-start token: a caller reached here with something else,
         * which is a bug rather than a CSS problem, so keep the token instead
         * of inventing a block around it. */
        return ValueAppendToken(list, open);
    }
    if (CssValueMakeBlock(&block, open->type, range, NULL) != RoboOk) {
        return RoboOutOfMemory;
    }
    if (flat) {
        GrammarFailAt(g, open->range.begin, RoboCssErrorKindRecoverableSyntax,
                        RoboInvalidArgument,
                        "nesting is deeper than this build will build; the contents are kept flat");
    }
    result = GrammarConsumeContainer(g, &block, closeType, depth + 1u, flat, &range,
                                       &closed);
    if (result != RoboOk) {
        CssValueDispose(&block);
        return result;
    }
    block.range = range;
    /* Assigned either way, and that matters: `CssValueMakeBlock` fills
     * in the bracket a block *would* be matched with, because a hand-built
     * block has no stream to disagree with it. A parsed one does. An unclosed
     * `[3` must say it was never matched — NONE is that answer, and inheriting
     * the guessed `]` would report a block as closed that never was. */
    block.close = closed ? closeType : RoboCssTokenNone;
    result = CssValueAppend(list, &block, NULL);
    if (result != RoboOk) {
        CssValueDispose(&block);
    }
    return result;
}

/** Consume a function whose opening token was already consumed. */
static Result GrammarConsumeFunction(CssGrammar *g,
                                              CssValue *list,
                                              const CssToken *functionToken,
                                              size_t depth)
{
    CssValue function = RoboCssCleared;
    CssRange range = functionToken->range;
    int flat = (depth + 1u > RoboCssMaxNesting);
    int closed = 0;
    Result result;

    if (CssValueMakeFunction(&function, TokenNameSlice(functionToken), range,
                                    NULL) != RoboOk) {
        return RoboOutOfMemory;
    }
    if (flat) {
        GrammarFailAt(g, functionToken->range.begin,
                        RoboCssErrorKindRecoverableSyntax, RoboInvalidArgument,
                        "nesting is deeper than this build will build; the contents are kept flat");
    }
    result = GrammarConsumeContainer(g, &function, RoboCssTokenRightParen,
                                       depth + 1u, flat, &range, &closed);
    if (result != RoboOk) {
        CssValueDispose(&function);
        return result;
    }
    function.range = range;
    if (closed) {
        function.close = RoboCssTokenRightParen;
    }
    result = CssValueAppend(list, &function, NULL);
    if (result != RoboOk) {
        CssValueDispose(&function);
    }
    return result;
}

/**
 * Consume one component value and append it to @p list.
 *
 * @p depth is how many containers @p list is already inside, so a construct
 * built here sits at `depth + 1` and that is what the bound is checked against.
 * This is the only recursion in the grammar's value path.
 */
static Result GrammarConsumeComponentValue(CssGrammar *g,
                                                     CssValue *list,
                                                     size_t depth)
{
    CssToken token = RoboCssCleared;
    Result result = GrammarPeek(g, &token);

    if (result != RoboOk) {
        return result;
    }
    if (token.type == RoboCssTokenEof) {
        return RoboOk;
    }
    result = GrammarNext(g, &token);
    if (result != RoboOk) {
        return result;
    }
    if (token.type == RoboCssTokenFunction) {
        return GrammarConsumeFunction(g, list, &token, depth);
    }
    if (CssTokenTypeIsBlockStart(token.type)) {
        return GrammarConsumeBlock(g, list, &token, depth);
    }
    return ValueAppendToken(list, &token);
}

/**
 * Consume component values into @p out until one of @p stops is next, or the
 * source ends. The stop token is left unconsumed.
 *
 * Every caller needs to look at the stop token to decide what happens next —
 * `{` opens a block, `;` ends a declaration, `}` ends the enclosing block — so
 * consuming it here would take that decision away from the only code that knows
 * what to do with it.
 *
 * `}` appears in two of the three stop sets because a `}` is never a component
 * value's beginning and always means "the block you are filling just ended";
 * leaving it unconsumed is what lets the declaration-carrying loop see it and
 * close the block exactly once.
 *
 *   prelude   `{`, `}`
 *   at-rule   `;`, `{`, `}`
 *   value     `;`, `}`
 *
 * @p depth is the depth @p out itself sits at.
 */
static Result GrammarConsumeComponentValues(CssGrammar *g,
                                                      CssValue *out,
                                                      const CssTokenType *stops,
                                                      size_t stopCount,
                                                      size_t depth)
{
    for (;;) {
        CssToken token = RoboCssCleared;
        size_t index;
        Result result = GrammarPeek(g, &token);

        if (result != RoboOk) {
            return result;
        }
        if (token.type == RoboCssTokenEof) {
            return RoboOk;
        }
        for (index = 0u; index < stopCount; index++) {
            if (token.type == stops[index]) {
                return RoboOk;
            }
        }
        if (token.type == RoboCssTokenComment) {
            (void)GrammarNext(g, &token);
            continue;
        }
        result = GrammarConsumeComponentValue(g, out, depth);
        if (result != RoboOk) {
            return result;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Recovery                                                            */
/* ------------------------------------------------------------------ */

/**
 * Consume the remnants of a bad declaration: everything up to the `;` that ends
 * it, a balanced `{ … }`, or the `}` that closes the enclosing block.
 *
 * The enclosing `}` is deliberately *not* consumed. It belongs to the block the
 * caller is filling, and eating it would end that block one declaration early —
 * which is how a recovery path turns one malformed declaration into a lost
 * rule.
 */
static void GrammarSkipDeclarationRemnants(CssGrammar *g)
{
    for (;;) {
        CssToken token = RoboCssCleared;

        if (GrammarPeek(g, &token) != RoboOk) {
            return;
        }
        switch (token.type) {
        case RoboCssTokenEof:
        case RoboCssTokenRightCurly:
            return;
        case RoboCssTokenSemicolon:
            (void)GrammarNext(g, &token);
            return;
        case RoboCssTokenLeftCurly: {
            CssValue scratch = RoboCssCleared;
            CssRange range;

            /* A block inside a bad declaration is part of what is bad: consume
             * it whole, so a `;` inside it cannot end the declaration early. */
            (void)GrammarNext(g, &token);
            if (CssValueMakeBlock(&scratch, RoboCssTokenLeftCurly,
                                          token.range, NULL) != RoboOk) {
                return;
            }
            {
                int closed = 0;

                range = token.range;
                (void)GrammarConsumeContainer(g, &scratch, RoboCssTokenRightCurly,
                                                1u, 0, &range, &closed);
            }
            CssValueDispose(&scratch);
            return;
        }
        default:
            (void)GrammarNext(g, &token);
            break;
        }
    }
}

/**
 * Consume a `{ … }` block and build nothing from it, counting only braces.
 *
 * This is the over-depth path for rule nesting: the tokens still have to be
 * consumed so the parse continues at the right place, but no rule — and so no
 * recursion — is built. The `{` was consumed by the caller.
 *
 * Only braces are counted, and a `}` is the only thing that can close it. A
 * mismatched `)` or `]` inside is an ordinary token: tracking a per-type
 * bracket stack here would need storage proportional to the input to decide
 * something the caller does not act on.
 */
static void GrammarSkipBracedBlock(CssGrammar *g,
                                      int *outClosed,
                                      size_t *outEnd)
{
    size_t nesting = 1u;

    *outClosed = 0;
    *outEnd = GrammarOffset(g);
    for (;;) {
        CssToken token = RoboCssCleared;

        if (GrammarNext(g, &token) != RoboOk) {
            return;
        }
        if (token.type == RoboCssTokenEof) {
            return;
        }
        if (token.type == RoboCssTokenLeftCurly) {
            nesting++;
        } else if (token.type == RoboCssTokenRightCurly) {
            nesting--;
            if (nesting == 0u) {
                *outClosed = 1;
                *outEnd = token.range.end;
                return;
            }
        } else {
            *outEnd = token.range.end;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Declarations                                                        */
/* ------------------------------------------------------------------ */

/**
 * Consume one declaration.
 *
 * @p out is filled with a PARSED declaration, or with an INVALID one when the
 * entry was declaration-shaped but not `name: value`. The caller appends it
 * either way: the model has a state for a malformed declaration precisely so a
 * caller can see what was rejected and not merely that something was.
 *
 * @p depth is the depth of the block being filled; a value list sits at that
 * depth, so a block inside a value is one deeper.
 */
static Result GrammarConsumeDeclaration(CssGrammar *g,
                                                 CssDeclaration *out,
                                                 int *outProduced,
                                                 size_t depth)
{
    CssDeclaration declaration = RoboCssCleared;
    CssToken name = RoboCssCleared;
    CssToken token = RoboCssCleared;
    CssValue value = RoboCssCleared;
    CssRange nameRange;
    CssRange valueRange;
    CssRange declarationRange;
    CssSlice nameText;
    int important;
    Result result;

    *outProduced = 0;

    /* The caller has established that an IDENT is next. */
    result = GrammarNext(g, &name);
    if (result != RoboOk) {
        return result;
    }
    nameText = name.text;
    nameRange = name.range;

    GrammarSkipTrivia(g);
    result = GrammarPeek(g, &token);
    if (result != RoboOk) {
        return result;
    }
    if (token.type != RoboCssTokenColon) {
        GrammarFailAt(g, nameRange.begin, RoboCssErrorKindMalformedDeclaration,
                        RoboInvalidArgument,
                        "a declaration's name must be followed by a colon");
        if (CssDeclarationMake(&declaration, nameText,
                                      RoboCssDeclarationStateInvalid, NULL) != RoboOk) {
            return RoboOutOfMemory;
        }
        (void)CssDeclarationSetRanges(&declaration, nameRange, nameRange,
                                              CssRangeAt(nameRange.end), NULL);
        GrammarSkipDeclarationRemnants(g);
        *out = declaration;
        *outProduced = 1;
        return RoboOk;
    }
    (void)GrammarNext(g, &token); /* the colon */

    {
        static const CssTokenType stops[] = {
            RoboCssTokenSemicolon, RoboCssTokenRightCurly
        };

        result = ValueMakeList(&value);
        if (result != RoboOk) {
            return result;
        }
        result = GrammarConsumeComponentValues(g, &value, stops,
                                                  sizeof(stops) / sizeof(stops[0]), depth);
        if (result != RoboOk) {
            CssValueDispose(&value);
            return result;
        }
    }
    /* A value's surrounding whitespace is not part of it — `color:  red  ` and
     * `color:red` are the same declaration — but its interior whitespace is, so
     * only the ends go. */
    ValueTrimWhitespace(&value);
    /* The flag is taken off the value *before* the declaration exists, because
     * finding it is what removes it — so it is held here and applied below. */
    important = ValueTakeImportant(&value);
    ValueTrimWhitespace(&value);
    ValueSpanSelf(&value);
    valueRange = value.range;

    declarationRange.begin = nameRange.begin;
    declarationRange.end = valueRange.end > nameRange.end ? valueRange.end
                                                            : nameRange.end;

    if (CssDeclarationMake(&declaration, nameText,
                                  RoboCssDeclarationStateParsed, NULL) != RoboOk) {
        CssValueDispose(&value);
        return RoboOutOfMemory;
    }
    if (important) {
        (void)CssDeclarationSetImportant(&declaration, 1, NULL);
    }
    (void)CssDeclarationSetRanges(&declaration, declarationRange, nameRange,
                                          valueRange, NULL);
    result = CssDeclarationSetValue(&declaration, &value, NULL);
    if (result != RoboOk) {
        CssValueDispose(&value);
        CssDeclarationDispose(&declaration);
        return RoboOutOfMemory;
    }

    *out = declaration;
    *outProduced = 1;
    return RoboOk;
}

/** Record a declaration-shaped entry that has no name at all (`{ : red; }`). */
static Result GrammarRecordNamelessDeclaration(CssGrammar *g,
                                                         CssDeclarationList *out,
                                                         size_t offset)
{
    CssDeclaration declaration = RoboCssCleared;

    GrammarFailAt(g, offset, RoboCssErrorKindMalformedDeclaration,
                    RoboInvalidArgument, "a declaration was missing its property name");
    if (CssDeclarationMake(&declaration, CssSliceEmpty(),
                                  RoboCssDeclarationStateInvalid, NULL) != RoboOk) {
        return RoboOutOfMemory;
    }
    (void)CssDeclarationSetRanges(&declaration, CssRangeAt(offset),
                                          CssRangeAt(offset),
                                          CssRangeAt(offset), NULL);
    GrammarSkipDeclarationRemnants(g);
    if (CssDeclarationListAppend(out, &declaration, NULL) != RoboOk) {
        CssDeclarationDispose(&declaration);
        return RoboOutOfMemory;
    }
    return RoboOk;
}

/**
 * Consume a style rule's block contents: a list of declarations.
 *
 * Consumes the closing `}` when there is one and reports through @p outClosed
 * whether it was there — a block that ran to the end of the source is a
 * different rule from one that was closed, and only this loop knows which
 * happened. @p depth is the block's own depth.
 */
static Result GrammarConsumeDeclarationBlock(CssGrammar *g,
                                                       CssDeclarationList *out,
                                                       size_t depth,
                                                       int *outClosed,
                                                       size_t *outEnd)
{
    *outClosed = 0;
    *outEnd = GrammarOffset(g);

    for (;;) {
        CssToken token = RoboCssCleared;
        size_t before = GrammarOffset(g);
        Result result;

        result = GrammarPeek(g, &token);
        if (result != RoboOk) {
            return result;
        }
        *outEnd = GrammarOffset(g);
        if (token.type == RoboCssTokenEof) {
            return RoboOk;
        }
        if (token.type == RoboCssTokenWhitespace ||
            token.type == RoboCssTokenSemicolon ||
            token.type == RoboCssTokenComment) {
            (void)GrammarNext(g, &token);
            continue;
        }
        if (token.type == RoboCssTokenRightCurly) {
            (void)GrammarNext(g, &token);
            *outClosed = 1;
            *outEnd = token.range.end;
            return RoboOk;
        }
        if (token.type == RoboCssTokenIdent) {
            CssDeclaration declaration = RoboCssCleared;
            int produced = 0;

            result = GrammarConsumeDeclaration(g, &declaration, &produced, depth);
            if (result != RoboOk) {
                return result;
            }
            if (produced) {
                if (CssDeclarationListAppend(out, &declaration, NULL) != RoboOk) {
                    CssDeclarationDispose(&declaration);
                    return RoboOutOfMemory;
                }
            }
            GrammarSkipTrivia(g);
            if (GrammarPeek(g, &token) == RoboOk &&
                token.type == RoboCssTokenSemicolon) {
                (void)GrammarNext(g, &token);
            }
        } else if (token.type == RoboCssTokenColon) {
            result = GrammarRecordNamelessDeclaration(g, out, token.range.begin);
            if (result != RoboOk) {
                return result;
            }
        } else if (token.type == RoboCssTokenAtKeyword) {
            /* An at-rule inside a style rule's block is CSS Nesting, a later
             * phase. It is consumed whole — so its contents cannot be mistaken
             * for declarations — and reported rather than silently dropped. */
            GrammarFailAt(g, token.range.begin, RoboCssErrorKindRecoverableSyntax,
                            RoboInvalidArgument,
                            "an at-rule inside a style rule's block is CSS Nesting, a later phase");
            GrammarSkipDeclarationRemnants(g);
        } else {
            /* `.nested { … }` and anything else rule-shaped: the same
             * treatment, named for what it is rather than as a broken
             * declaration. */
            GrammarFailAt(g, token.range.begin, RoboCssErrorKindRecoverableSyntax,
                            RoboInvalidArgument,
                            "a nested rule inside a style rule's block is CSS Nesting, a later phase");
            GrammarSkipDeclarationRemnants(g);
        }
        /* Every branch above either consumed a token or returned. The guard is
         * a belt on top of that bra: a declaration block that stopped making
         * progress would spin forever, and no future edit may introduce one. */
        if (GrammarOffset(g) == before) {
            return RoboOk;
        }
    }
}

/** Parse @p source as a declaration list — the inline-style boundary. */
Result CssGrammarParseDeclarations(CssDeclarationList *outList,
                                                  const CssSource *source,
                                                  CssOrigin origin,
                                                  CssError *outError)
{
    CssGrammar grammar = RoboCssCleared;
    int closed = 0;
    size_t end = 0u;
    Result result;

    if (!outList || !source) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "declaration list or source is NULL");
        return RoboInvalidArgument;
    }
    CssDeclarationListInit(outList);
    if (!CssOriginIsKnown(origin)) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "unknown origin");
        return RoboInvalidArgument;
    }

    grammar.source = source;
    grammar.origin = origin;
    result = CssTokenizerInit(&grammar.tokenizer, source, NULL);
    if (result != RoboOk) {
        CssTokenizerDispose(&grammar.tokenizer);
        CssErrorSet(outError, RoboCssErrorKindNone, result, NULL,
                           "the tokenizer could not be initialized");
        return result;
    }

    result = GrammarConsumeDeclarationBlock(&grammar, outList, 0u, &closed, &end);
    CssTokenizerDispose(&grammar.tokenizer);

    if (result != RoboOk) {
        CssDeclarationListDispose(outList);
        CssErrorSet(outError, RoboCssErrorKindOutOfMemory, result, NULL,
                           "the declaration list could not be built");
        return result;
    }
    if (grammar.hasError) {
        CssErrorSet(outError, grammar.error.kind, grammar.error.code,
                           &grammar.error.position, grammar.error.message);
    } else {
        CssErrorInit(outError);
    }
    return RoboOk;
}

/** Parse @p source as one component-value list — an initial value (9.7). */
Result CssGrammarParseValue(CssValue *outValue,
                                           const CssSource *source,
                                           CssOrigin origin,
                                           CssError *outError)
{
    CssGrammar grammar = RoboCssCleared;
    Result result;

    if (!outValue || !source) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "value or source is NULL");
        return RoboInvalidArgument;
    }
    CssValueInit(outValue);
    if (!CssOriginIsKnown(origin)) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "unknown origin");
        return RoboInvalidArgument;
    }

    grammar.source = source;
    grammar.origin = origin;
    result = CssTokenizerInit(&grammar.tokenizer, source, NULL);
    if (result != RoboOk) {
        CssTokenizerDispose(&grammar.tokenizer);
        CssErrorSet(outError, RoboCssErrorKindNone, result, NULL,
                           "the tokenizer could not be initialized");
        return result;
    }

    /* No stops: a value runs to the end of its source, so an initial value's
     * text is exactly the bytes between its ends. */
    result = ValueMakeList(outValue);
    if (result == RoboOk) {
        result = GrammarConsumeComponentValues(&grammar, outValue, NULL, 0u, 0u);
    }
    CssTokenizerDispose(&grammar.tokenizer);

    if (result != RoboOk) {
        CssValueDispose(outValue);
        CssErrorSet(outError, RoboCssErrorKindOutOfMemory, result, NULL,
                           "the value could not be built");
        return result;
    }
    /* Same rule a declaration's value follows: surrounding whitespace is not
     * part of the value, interior whitespace is. */
    ValueTrimWhitespace(outValue);
    ValueSpanSelf(outValue);
    if (grammar.hasError) {
        CssErrorSet(outError, grammar.error.kind, grammar.error.code,
                           &grammar.error.position, grammar.error.message);
    } else {
        CssErrorInit(outError);
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Rules                                                               */
/* ------------------------------------------------------------------ */

static Result GrammarConsumeRule(CssGrammar *g,
                                          CssRule *out,
                                          int *outProduced,
                                          size_t depth);

/**
 * Consume the contents of a block that holds rules, or a whole stylesheet.
 *
 * Consumes the closing `}` when present. @p topLevel is what makes CDO and CDC
 * ignorable: at the top of a stylesheet `<!--` and `-->` are the comment tokens
 * CSS tolerates, while inside a block they are ordinary component values that
 * begin a (probably invalid) rule. @p depth is the depth of the block being
 * filled.
 */
static Result GrammarConsumeRuleList(CssGrammar *g,
                                               CssRuleList *out,
                                               size_t depth,
                                               int topLevel,
                                               int *outClosed,
                                               size_t *outEnd)
{
    *outClosed = 0;
    *outEnd = GrammarOffset(g);

    for (;;) {
        CssToken token = RoboCssCleared;
        size_t before = GrammarOffset(g);
        Result result;

        GrammarSkipTrivia(g);
        *outEnd = GrammarOffset(g);
        result = GrammarPeek(g, &token);
        if (result != RoboOk) {
            return result;
        }
        if (token.type == RoboCssTokenEof) {
            /* Reaching the end settles the question: there is no closer left to
             * find, and only a block can be allowed not to have one. */
            *outClosed = topLevel;
            return RoboOk;
        }
        if (token.type == RoboCssTokenRightCurly) {
            if (!topLevel) {
                (void)GrammarNext(g, &token);
                *outClosed = 1;
                *outEnd = token.range.end;
                return RoboOk;
            }
            /* A `}` at the top level closes nothing. It is still consumed, so a
             * stray one cannot make the loop read it forever. */
            GrammarFailAt(g, token.range.begin, RoboCssErrorKindRecoverableSyntax,
                            RoboInvalidArgument, "a closing brace with no block to close");
            (void)GrammarNext(g, &token);
            continue;
        }
        if (topLevel &&
            (token.type == RoboCssTokenCdo || token.type == RoboCssTokenCdc)) {
            (void)GrammarNext(g, &token);
            continue;
        }

        {
            CssRule rule = RoboCssCleared;
            int produced = 0;

            result = GrammarConsumeRule(g, &rule, &produced, depth);
            if (result != RoboOk) {
                return result;
            }
            if (produced) {
                if (CssRuleListAppend(out, &rule, NULL) != RoboOk) {
                    CssRuleDispose(&rule);
                    return RoboOutOfMemory;
                }
            }
        }
        if (GrammarOffset(g) == before) {
            return RoboOk;
        }
    }
}

/**
 * Read the block starting at @p blockOpen again as a *declaration* list.
 *
 * This is how `@font-face { font-family: X; src: url(x.woff) }` keeps its
 * descriptors. A block's contents are declarations for some at-rules and rules
 * for others — `@font-face` and `@page` describe one thing, `@media` and
 * `@keyframes` contain many — and an at-rule this build has never heard of
 * could be either. A rule list is tried first, because a rule is the shape that
 * carries more information (a nested prelude and a nested block, not just a
 * name and a value); when that yields nothing, the same bytes are read as
 * declarations, which is what the block was.
 *
 * The second reading runs on a tokenizer of its own over the *same* source, so
 * every offset it reports is still an offset into the caller's bytes and no
 * range has to be adjusted afterwards. It starts at offset 0 and skips forward
 * to @p blockOpen, so a prelude can never be mistaken for a descriptor.
 *
 * @p outError and @p outRecoveries receive this pass's own verdict rather
 * than writing to the caller's parse, because the caller may yet discard it.
 */
static Result GrammarRereadBlockAsDeclarations(
    const CssSource *source,
    size_t blockOpen,
    size_t depth,
    CssDeclarationList *out,
    CssError *outError,
    size_t *outRecoveries)
{
    CssGrammar scratch = RoboCssCleared;
    CssToken token = RoboCssCleared;
    int closed = 0;
    size_t end = 0u;
    Result result;

    *outRecoveries = 0u;
    result = CssTokenizerInit(&scratch.tokenizer, source, NULL);
    if (result != RoboOk) {
        return result;
    }
    /* The origin is only ever validated by the two entry points, never read by
     * the grammar, so the scratch pass does not touch it. */
    scratch.source = source;

    for (;;) {
        result = GrammarPeek(&scratch, &token);
        if (result != RoboOk) {
            break;
        }
        if (token.type == RoboCssTokenEof ||
            token.range.begin >= blockOpen) {
            break;
        }
        (void)GrammarNext(&scratch, &token);
    }
    if (token.type == RoboCssTokenLeftCurly) {
        (void)GrammarNext(&scratch, &token); /* the `{` */
        result = GrammarConsumeDeclarationBlock(&scratch, out, depth, &closed, &end);
    }
    if (result == RoboOk) {
        *outError = scratch.error;
        *outRecoveries = scratch.recoveries;
    }
    CssTokenizerDispose(&scratch.tokenizer);
    return result;
}

/**
 * Consume an at-rule; the caller has established an at-keyword is next.
 *
 * The at-rule's *name* is read and nothing is done with it: `@media`, `@supports`
 * and `@font-face` are consumed by exactly the same code. Nothing is loaded,
 * nothing is fetched, and no name is looked up in a table — because there is no
 * table.
 *
 * A block is read as a rule list first and, when that finds no rules at all, as
 * a declaration list (see GrammarRereadBlockAsDeclarations). Legal CSS fills
 * exactly one of the two — a group at-rule's block holds rules, a descriptor
 * at-rule's holds declarations — so `CssAtRuleRuleCount(at) == 0`
 * together with a non-zero declaration count is the signature of a descriptor
 * at-rule, and both being non-zero is the signature of a block that no at-rule
 * could accept, kept rather than resolved.
 */
static Result GrammarConsumeAtRule(CssGrammar *g,
                                             CssRule *out,
                                             int *outProduced,
                                             size_t depth)
{
    static const CssTokenType stops[] = {
        RoboCssTokenSemicolon, RoboCssTokenLeftCurly,
        RoboCssTokenRightCurly
    };
    CssToken keyword = RoboCssCleared;
    CssToken token = RoboCssCleared;
    CssValue prelude = RoboCssCleared;
    CssSlice name;
    CssSlice preludeText = CssSliceEmpty();
    CssRange preludeRange;
    CssRange ruleRange = CssRangeAt(0u);
    size_t blockOpen = 0u;
    int hasBlock = 0;
    int blockClosed = 0;
    Result result;

    *outProduced = 0;

    result = GrammarNext(g, &keyword);
    if (result != RoboOk) {
        return result;
    }
    name = TokenNameSlice(&keyword);
    ruleRange.begin = keyword.range.begin;
    if (CssSliceIsEmpty(name)) {
        GrammarFailAt(g, keyword.range.begin, RoboCssErrorKindMalformedRule,
                        RoboInvalidArgument, "an at-rule needs a name after its `@`");
        GrammarSkipDeclarationRemnants(g);
        return RoboOk;
    }

    result = ValueMakeList(&prelude);
    if (result != RoboOk) {
        return result;
    }
    result = GrammarConsumeComponentValues(g, &prelude, stops,
                                              sizeof(stops) / sizeof(stops[0]), depth);
    if (result != RoboOk) {
        CssValueDispose(&prelude);
        return result;
    }
    ValueTrimWhitespace(&prelude);
    ValueSpanSelf(&prelude);
    preludeRange = prelude.range;
    ruleRange.end = preludeRange.end > ruleRange.begin ? preludeRange.end
                                                          : ruleRange.begin;
    if (preludeRange.begin < preludeRange.end && g->source) {
        /* The prelude's bytes, borrowed from the source. They are the same
         * bytes the component values cover, so the two cannot disagree. */
        (void)CssSourceRangeSlice(g->source, preludeRange, &preludeText);
    }

    if (CssRuleMakeAt(out, name, NULL) != RoboOk) {
        CssValueDispose(&prelude);
        return RoboOutOfMemory;
    }
    (void)CssAtRuleSetPreludeValues(&out->at, &prelude, NULL);
    out->at.preludeRange = preludeRange;

    result = GrammarPeek(g, &token);
    if (result != RoboOk) {
        CssRuleDispose(out);
        return result;
    }
    if (token.type == RoboCssTokenSemicolon) {
        (void)GrammarNext(g, &token);
        ruleRange.end = token.range.end;
    } else if (token.type == RoboCssTokenLeftCurly) {
        hasBlock = 1;
        (void)GrammarNext(g, &token); /* the `{` */
        blockOpen = token.range.begin;
    }
    /* `hasBlock` has to be on the record before any child is added to it, so
     * the prelude and the block flag are recorded here — once, now that the
     * block's presence is known — and the children follow. */
    (void)CssAtRuleSetPrelude(&out->at, preludeText, hasBlock, NULL);
    if (hasBlock) {
        size_t BlockEnd = 0u;
        CssGrammarState beforeBlock = GrammarStateSave(g);

        if (depth + 1u > RoboCssMaxNesting) {
            GrammarFailAt(g, blockOpen, RoboCssErrorKindRecoverableSyntax,
                            RoboInvalidArgument,
                            "at-rules are nested deeper than this build will build; the block is skipped");
            GrammarSkipBracedBlock(g, &blockClosed, &BlockEnd);
        } else {
            out->at.children = (CssRuleList *)RoboCalloc(
                1u, sizeof(CssRuleList));
            if (!out->at.children) {
                CssRuleDispose(out);
                return RoboOutOfMemory;
            }
            CssRuleListInit(out->at.children);
            result = GrammarConsumeRuleList(g, out->at.children, depth + 1u, 0,
                                               &blockClosed, &BlockEnd);
            if (result != RoboOk) {
                CssRuleDispose(out);
                return result;
            }
            /* Nothing rule-shaped in the block: read the same bytes as a
             * declaration list, because that is what a descriptor at-rule's
             * block is. The cost is one more pass over a block that produced no
             * rules, and it buys `@font-face` its descriptors. */
            if (CssAtRuleRuleCount(&out->at) == 0u &&
                BlockEnd > blockOpen + 1u) {
                CssDeclarationList descriptors = RoboCssCleared;
                CssError secondReading = RoboCssCleared;
                size_t secondRecoveries = 0u;

                CssDeclarationListInit(&descriptors);
                result = GrammarRereadBlockAsDeclarations(g->source, blockOpen,
                                                             depth + 1u, &descriptors,
                                                             &secondReading,
                                                             &secondRecoveries);
                if (result == RoboOk &&
                    !CssDeclarationListIsEmpty(&descriptors)) {
                    /* The block *was* declarations, so the rule-list reading's
                     * complaint described this build's guess rather than the
                     * stylesheet. Discard it and let the successful reading
                     * speak. */
                    GrammarStateRestore(g, &beforeBlock);
                    GrammarAdoptError(g, &secondReading, secondRecoveries);
                    if (CssAtRuleTakeDeclarations(&out->at, &descriptors,
                                                          NULL) != RoboOk) {
                        CssDeclarationListDispose(&descriptors);
                        CssRuleDispose(out);
                        return RoboOutOfMemory;
                    }
                }
                CssDeclarationListDispose(&descriptors);
                if (result != RoboOk) {
                    CssRuleDispose(out);
                    return result;
                }
            }
        }
        ruleRange.end = BlockEnd > ruleRange.end ? BlockEnd : ruleRange.end;
    }
    /* No `;` and no block: the at-rule ended at the end of the input or at the
     * enclosing block's `}`, and both are legal — `@import "x"` with no final
     * semicolon is what the last line of a stylesheet looks like — so this is
     * not an error. */
    (void)CssAtRuleSetRange(&out->at, ruleRange, NULL);
    if (hasBlock) {
        (void)CssAtRuleSetBlockClosed(&out->at, blockClosed, NULL);
        if (!blockClosed) {
            GrammarFailAt(g, ruleRange.begin, RoboCssErrorKindUnexpectedEof,
                            RoboInvalidArgument,
                            "an at-rule's block was not closed before the end of the source");
        }
    }
    *outProduced = 1;
    return RoboOk;
}

/** Consume a qualified (style) rule. */
static Result GrammarConsumeStyleRule(CssGrammar *g,
                                                CssRule *out,
                                                int *outProduced,
                                                size_t depth)
{
    static const CssTokenType stops[] = {
        RoboCssTokenLeftCurly, RoboCssTokenRightCurly
    };
    CssValue prelude = RoboCssCleared;
    CssToken token = RoboCssCleared;
    CssRange preludeRange;
    CssRange blockRange = CssRangeAt(0u);
    CssRange ruleRange = CssRangeAt(0u);
    int blockClosed = 0;
    Result result;

    *outProduced = 0;

    result = ValueMakeList(&prelude);
    if (result != RoboOk) {
        return result;
    }
    result = GrammarConsumeComponentValues(g, &prelude, stops,
                                              sizeof(stops) / sizeof(stops[0]), depth);
    if (result != RoboOk) {
        CssValueDispose(&prelude);
        return result;
    }
    ValueTrimWhitespace(&prelude);
    ValueSpanSelf(&prelude);
    preludeRange = prelude.range;

    result = GrammarPeek(g, &token);
    if (result != RoboOk) {
        CssValueDispose(&prelude);
        return result;
    }
    if (token.type == RoboCssTokenEof) {
        GrammarFailAt(g, preludeRange.begin < preludeRange.end ? preludeRange.begin
                                                                   : GrammarOffset(g),
                        RoboCssErrorKindUnexpectedEof, RoboInvalidArgument,
                        "a rule's declaration block was missing before the end of the source");
        CssValueDispose(&prelude);
        return RoboOk;
    }
    if (token.type == RoboCssTokenRightCurly) {
        /* The enclosing block ended first, so this rule never got a block. The
         * `}` is left for whoever owns it. */
        GrammarFailAt(g, preludeRange.begin < preludeRange.end ? preludeRange.begin
                                                                   : token.range.begin,
                        RoboCssErrorKindMalformedRule, RoboInvalidArgument,
                        "a rule ended without a declaration block");
        CssValueDispose(&prelude);
        return RoboOk;
    }
    if (prelude.children.count == 0u) {
        /* `{ color: red }` with nothing before the brace. A qualified rule with
         * an empty prelude can never be a selector, and this is the one
         * prelude-shaped problem 9.3 can settle without a selector parser: the
         * rule is consumed and dropped, and the block's declarations with it. */
        size_t BlockEnd = 0u;

        GrammarFailAt(g, token.range.begin, RoboCssErrorKindMalformedRule,
                        RoboInvalidArgument,
                        "a rule had no selector before its declaration block");
        (void)GrammarNext(g, &token); /* the `{` */
        /* The block is consumed and discarded rather than parsed: its
         * declarations belong to a rule that cannot exist. */
        GrammarSkipBracedBlock(g, &blockClosed, &BlockEnd);
        CssValueDispose(&prelude);
        return RoboOk;
    }

    if (CssRuleMakeStyle(out, NULL) != RoboOk) {
        CssValueDispose(&prelude);
        return RoboOutOfMemory;
    }
    (void)GrammarNext(g, &token); /* the `{` */
    blockRange = token.range;
    {
        int innerClosed = 0;
        size_t BlockEnd = blockRange.end;

        if (depth + 1u > RoboCssMaxNesting) {
            /* No rule object is built for the block: its contents are consumed
             * and dropped rather than recursed into. */
            GrammarFailAt(g, blockRange.begin, RoboCssErrorKindRecoverableSyntax,
                            RoboInvalidArgument,
                            "rules are nested deeper than this build will build; the block is skipped");
            GrammarSkipBracedBlock(g, &innerClosed, &BlockEnd);
        } else {
            result = GrammarConsumeDeclarationBlock(g, &out->style.declarations,
                                                       depth + 1u, &innerClosed,
                                                       &BlockEnd);
            if (result != RoboOk) {
                CssRuleDispose(out);
                CssValueDispose(&prelude);
                return result;
            }
        }
        blockClosed = innerClosed;
        blockRange.end = BlockEnd > blockRange.end ? BlockEnd : blockRange.end;
    }

    ruleRange.begin = preludeRange.begin;
    ruleRange.end = blockRange.end;
    (void)CssStyleRuleSetPreludeValues(&out->style, &prelude, NULL);
    (void)CssStyleRuleSetRanges(&out->style, ruleRange, preludeRange,
                                         blockRange, NULL);
    (void)CssStyleRuleSetBlockClosed(&out->style, blockClosed, NULL);
    if (!blockClosed) {
        GrammarFailAt(g, ruleRange.begin, RoboCssErrorKindUnexpectedEof,
                        RoboInvalidArgument,
                        "a rule's declaration block was not closed before the end of the source");
    }
    *outProduced = 1;
    return RoboOk;
}

/**
 * Consume one rule: an at-rule when an at-keyword is next, otherwise a
 * qualified rule.
 *
 * A qualified rule whose prelude is empty, that never reaches a `{`, or whose
 * block belongs to an enclosing one is consumed and *dropped* rather than
 * produced — with the reason recorded. That is the spec's behaviour (a
 * qualified rule that is not valid in its context is discarded and its block
 * consumed), and it is what keeps an empty-prelude rule from reaching 9.4 as a
 * "selector" that can never match anything.
 */
static Result GrammarConsumeRule(CssGrammar *g,
                                          CssRule *out,
                                          int *outProduced,
                                          size_t depth)
{
    CssToken token = RoboCssCleared;

    *outProduced = 0;
    if (GrammarPeek(g, &token) != RoboOk) {
        return RoboOk;
    }
    if (token.type == RoboCssTokenAtKeyword) {
        return GrammarConsumeAtRule(g, out, outProduced, depth);
    }
    return GrammarConsumeStyleRule(g, out, outProduced, depth);
}

/** Parse @p source into @p outRules. */
Result CssGrammarParseStylesheet(CssRuleList *outRules,
                                                const CssSource *source,
                                                CssOrigin origin,
                                                CssError *outError)
{
    CssGrammar grammar = RoboCssCleared;
    int closed = 0;
    size_t end = 0u;
    Result result;

    if (!outRules || !source) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "rule list or source is NULL");
        return RoboInvalidArgument;
    }
    CssRuleListInit(outRules);
    if (!CssOriginIsKnown(origin)) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "unknown origin");
        return RoboInvalidArgument;
    }

    grammar.source = source;
    grammar.origin = origin;
    result = CssTokenizerInit(&grammar.tokenizer, source, NULL);
    if (result != RoboOk) {
        CssTokenizerDispose(&grammar.tokenizer);
        CssErrorSet(outError, RoboCssErrorKindNone, result, NULL,
                           "the tokenizer could not be initialized");
        return result;
    }

    result = GrammarConsumeRuleList(&grammar, outRules, 0u, 1, &closed, &end);
    CssTokenizerDispose(&grammar.tokenizer);

    if (result != RoboOk) {
        CssRuleListDispose(outRules);
        CssErrorSet(outError, RoboCssErrorKindOutOfMemory, result, NULL,
                           "the rule list could not be built");
        return result;
    }
    if (grammar.hasError) {
        CssErrorSet(outError, grammar.error.kind, grammar.error.code,
                           &grammar.error.position, grammar.error.message);
    } else {
        CssErrorInit(outError);
    }
    return RoboOk;
}
