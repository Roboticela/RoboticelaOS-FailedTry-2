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
 *    bounded by ROBO_CSS_MAX_NESTING — one budget covering rule nesting and
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
 * The grammar owns a tokenizer rather than a `robo_css_parser_t`, and that is
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
typedef struct robo_css_grammar {
    const robo_css_source_t *source;
    robo_css_tokenizer_t tokenizer;
    robo_css_origin_t origin;
    int has_error;
    robo_css_error_t error;
    size_t recoveries;
} robo_css_grammar_t;

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
typedef struct robo_css_grammar_state {
    int has_error;
    robo_css_error_t error;
    size_t recoveries;
} robo_css_grammar_state_t;

/* ------------------------------------------------------------------ */
/* Tokens                                                              */
/* ------------------------------------------------------------------ */

/** Fold the tokenizer's own error into the grammar's, exactly once each. */
static void grammar_absorb_tokenizer_error(robo_css_grammar_t *g)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;

    /* Taking it clears the tokenizer's slot, so the *next* problem it finds can
     * be reported too instead of being masked by the first for the rest of the
     * parse — and this grammar still records only the first. */
    if (!robo_css_tokenizer_take_error(&g->tokenizer, &error)) {
        return;
    }
    g->recoveries++;
    if (!g->has_error) {
        g->has_error = 1;
        g->error = error;
    }
}

/** Record a problem at @p offset as the grammar's first problem, and count it. */
static void grammar_fail_at(robo_css_grammar_t *g,
                            size_t offset,
                            robo_css_error_kind_t kind,
                            robo_result_t code,
                            const char *message)
{
    robo_css_position_t position = robo_css_position_at(offset);

    g->recoveries++;
    if (g->has_error) {
        return; /* the first problem is the one that explains the rest */
    }
    if (g->source) {
        /* Line and column are a courtesy: failing to resolve them must not fail
         * the parse, and the offset is recorded either way. */
        (void)robo_css_source_position(g->source, offset, &position);
    }
    robo_css_error_set(&g->error, kind, code, &position, message);
    g->has_error = 1;
}

/** Peek the next token without consuming it. */
static robo_result_t grammar_peek(robo_css_grammar_t *g, robo_css_token_t *out)
{
    robo_result_t result = robo_css_tokenizer_peek(&g->tokenizer, out, NULL);

    grammar_absorb_tokenizer_error(g);
    return result;
}

/** Consume the next token. */
static robo_result_t grammar_next(robo_css_grammar_t *g, robo_css_token_t *out)
{
    robo_result_t result = robo_css_tokenizer_next(&g->tokenizer, out, NULL);

    grammar_absorb_tokenizer_error(g);
    return result;
}

/** Consume whitespace and comments, leaving the next real token peeked. */
static void grammar_skip_trivia(robo_css_grammar_t *g)
{
    (void)robo_css_tokenizer_skip_trivia(&g->tokenizer, NULL);
    grammar_absorb_tokenizer_error(g);
}

/** Byte offset the cursor currently sits at. */
static size_t grammar_offset(const robo_css_grammar_t *g)
{
    return robo_css_tokenizer_offset(&g->tokenizer);
}

/** Snapshot the error state. */
static robo_css_grammar_state_t grammar_state_save(const robo_css_grammar_t *g)
{
    robo_css_grammar_state_t state;

    state.has_error = g->has_error;
    state.error = g->error;
    state.recoveries = g->recoveries;
    return state;
}

/** Put the error state back, discarding what the speculative pass recorded. */
static void grammar_state_restore(robo_css_grammar_t *g,
                                  const robo_css_grammar_state_t *state)
{
    g->has_error = state->has_error;
    g->error = state->error;
    g->recoveries = state->recoveries;
}

/**
 * Adopt a pass that ran on its own state: count its problems, and record the
 * first of them when this parse has nothing recorded yet.
 */
static void grammar_adopt_error(robo_css_grammar_t *g,
                                const robo_css_error_t *error,
                                size_t recoveries)
{
    g->recoveries += recoveries;
    if (!g->has_error && error && robo_css_error_is_error(error)) {
        g->error = *error;
        g->has_error = 1;
    }
}

/** Non-zero when @p token opens a nested construct (a block or a function). */
static int token_opens_nesting(robo_css_token_type_t type)
{
    return robo_css_token_type_is_block_start(type) ||
           type == ROBO_CSS_TOKEN_FUNCTION;
}

/* ------------------------------------------------------------------ */
/* Component values                                                    */
/* ------------------------------------------------------------------ */

/** Non-zero when @p value is a component value made of one token of @p type. */
static int value_is_token_of_type(const robo_css_value_t *value,
                                  robo_css_token_type_t type)
{
    return value && value->kind == ROBO_CSS_VALUE_KIND_TOKEN &&
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
static robo_css_slice_t token_name_slice(const robo_css_token_t *token)
{
    robo_css_slice_t text = robo_css_token_text(token);

    if (token->type == ROBO_CSS_TOKEN_AT_KEYWORD) {
        if (text.length > 0u && text.bytes[0] == (uint8_t)'@') {
            text.bytes += 1u;
            text.length -= 1u;
        }
        return text;
    }
    if (token->type == ROBO_CSS_TOKEN_FUNCTION) {
        if (text.length > 0u && text.bytes[text.length - 1u] == (uint8_t)'(') {
            text.length -= 1u;
        }
        return text;
    }
    return text;
}

/** The span the component values in @p value cover; empty when there are none. */
static robo_css_range_t value_span(const robo_css_value_t *value)
{
    robo_css_range_t range = robo_css_range_at(0u);

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
static void value_span_self(robo_css_value_t *value)
{
    if (value) {
        value->range = value_span(value);
    }
}

/** Index of the last child that is not a whitespace token, or (size_t)-1. */
static size_t value_last_significant(const robo_css_value_t *value)
{
    size_t index;

    if (!value) {
        return (size_t)-1;
    }
    for (index = value->children.count; index > 0u; index--) {
        if (!value_is_token_of_type(&value->children.items[index - 1u],
                                    ROBO_CSS_TOKEN_WHITESPACE)) {
            return index - 1u;
        }
    }
    return (size_t)-1;
}

/** Index of the last non-whitespace child before @p before, or (size_t)-1. */
static size_t value_prev_significant(const robo_css_value_t *value, size_t before)
{
    size_t index;

    if (!value || before == 0u) {
        return (size_t)-1;
    }
    for (index = before; index > 0u; index--) {
        if (!value_is_token_of_type(&value->children.items[index - 1u],
                                    ROBO_CSS_TOKEN_WHITESPACE)) {
            return index - 1u;
        }
    }
    return (size_t)-1;
}

/** Dispose children from @p keep onwards, then forget them. */
static void value_truncate(robo_css_value_t *value, size_t keep)
{
    size_t index;

    if (!value) {
        return;
    }
    if (keep > value->children.count) {
        keep = value->children.count;
    }
    for (index = keep; index < value->children.count; index++) {
        robo_css_value_dispose(&value->children.items[index]);
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
static void value_drop_front(robo_css_value_t *value, size_t count)
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
        robo_css_value_init(&value->children.items[index]);
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
static void value_trim_whitespace(robo_css_value_t *value)
{
    size_t leading = 0u;

    if (!value) {
        return;
    }
    while (leading < value->children.count &&
           value_is_token_of_type(&value->children.items[leading],
                                  ROBO_CSS_TOKEN_WHITESPACE)) {
        leading++;
    }
    if (leading > 0u) {
        value_drop_front(value, leading);
    }
    value_truncate(value, value_last_significant(value) + 1u);
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
static int value_take_important(robo_css_value_t *value)
{
    size_t last = value_last_significant(value);
    size_t bang;

    if (last == (size_t)-1) {
        return 0;
    }
    if (value->children.items[last].kind != ROBO_CSS_VALUE_KIND_TOKEN ||
        value->children.items[last].token.type != ROBO_CSS_TOKEN_IDENT ||
        !robo_css_token_value_equals_ascii_ci(&value->children.items[last].token,
                                              "important")) {
        return 0;
    }
    bang = value_prev_significant(value, last);
    if (bang == (size_t)-1 ||
        !value_is_token_of_type(&value->children.items[bang], ROBO_CSS_TOKEN_DELIM) ||
        !robo_css_token_value_equals_ascii(&value->children.items[bang].token, "!")) {
        return 0;
    }
    value_truncate(value, bang);
    value_trim_whitespace(value);
    return 1;
}

/** Append @p token to @p list as a TOKEN component value. */
static robo_result_t value_append_token(robo_css_value_t *list,
                                        const robo_css_token_t *token)
{
    robo_css_value_t value = ROBO_CSS_CLEARED;

    if (robo_css_value_make_token(&value, token, NULL) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_css_value_append(list, &value, NULL);
}

/** A fresh, empty component-value LIST in @p out. */
static robo_result_t value_make_list(robo_css_value_t *out)
{
    robo_css_value_init(out);
    if (robo_css_value_make_list(out, NULL) != ROBO_OK) {
        return ROBO_OUT_OF_MEMORY;
    }
    return ROBO_OK;
}

static robo_result_t grammar_consume_component_value(robo_css_grammar_t *g,
                                                     robo_css_value_t *list,
                                                     size_t depth);

/**
 * Consume the tokens up to and including the one that closes the container
 * opened before this call, appending them to @p container.
 *
 * @p close_type is the token that ends it (`)`, `]` or `}`). Only that exact
 * type ends it: a mismatched closer is an ordinary component value, which is
 * what "consume a simple block" says and what makes `[a)` a block containing
 * `a` and `)`.
 *
 * @p flat is the over-depth path: nothing recurses, bracket tokens are elided
 * and counted rather than modelled, and only the leaves are kept.
 */
static robo_result_t grammar_consume_container(robo_css_grammar_t *g,
                                               robo_css_value_t *container,
                                               robo_css_token_type_t close_type,
                                               size_t depth,
                                               int flat,
                                               robo_css_range_t *range,
                                               int *out_closed)
{
    size_t nesting = 1u;

    *out_closed = 0;
    for (;;) {
        robo_css_token_t token = ROBO_CSS_CLEARED;
        robo_result_t result = grammar_peek(g, &token);

        if (result != ROBO_OK) {
            return result;
        }
        if (token.type == ROBO_CSS_TOKEN_EOF) {
            grammar_fail_at(g, range->begin, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF,
                            ROBO_INVALID_ARGUMENT,
                            "a block or function was not closed before the end of the source");
            return ROBO_OK;
        }
        if (flat) {
            if (token.type == close_type) {
                if (nesting == 1u) {
                    (void)grammar_next(g, &token);
                    range->end = token.range.end;
                    *out_closed = 1;
                    return ROBO_OK;
                }
                nesting--;
                (void)grammar_next(g, &token);
                continue;
            }
            if (token_opens_nesting(token.type)) {
                nesting++;
                (void)grammar_next(g, &token);
                continue;
            }
            (void)grammar_next(g, &token);
            if (token.type == ROBO_CSS_TOKEN_COMMENT) {
                continue;
            }
            result = value_append_token(container, &token);
            if (result != ROBO_OK) {
                return result;
            }
            continue;
        }
        if (token.type == close_type) {
            (void)grammar_next(g, &token);
            range->end = token.range.end;
            *out_closed = 1;
            return ROBO_OK;
        }
        if (token.type == ROBO_CSS_TOKEN_COMMENT) {
            (void)grammar_next(g, &token);
            continue;
        }
        result = grammar_consume_component_value(g, container, depth);
        if (result != ROBO_OK) {
            return result;
        }
    }
}

/** Consume a simple block whose opening token was already consumed. */
static robo_result_t grammar_consume_block(robo_css_grammar_t *g,
                                           robo_css_value_t *list,
                                           const robo_css_token_t *open,
                                           size_t depth)
{
    robo_css_value_t block = ROBO_CSS_CLEARED;
    robo_css_token_type_t close_type = ROBO_CSS_TOKEN_NONE;
    robo_css_range_t range = open->range;
    int closed = 0;
    int flat = (depth + 1u > ROBO_CSS_MAX_NESTING);
    robo_result_t result;

    if (!robo_css_token_type_matching_close(open->type, &close_type)) {
        /* Not a block-start token: a caller reached here with something else,
         * which is a bug rather than a CSS problem, so keep the token instead
         * of inventing a block around it. */
        return value_append_token(list, open);
    }
    if (robo_css_value_make_block(&block, open->type, range, NULL) != ROBO_OK) {
        return ROBO_OUT_OF_MEMORY;
    }
    if (flat) {
        grammar_fail_at(g, open->range.begin, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX,
                        ROBO_INVALID_ARGUMENT,
                        "nesting is deeper than this build will build; the contents are kept flat");
    }
    result = grammar_consume_container(g, &block, close_type, depth + 1u, flat, &range,
                                       &closed);
    if (result != ROBO_OK) {
        robo_css_value_dispose(&block);
        return result;
    }
    block.range = range;
    /* Assigned either way, and that matters: `robo_css_value_make_block` fills
     * in the bracket a block *would* be matched with, because a hand-built
     * block has no stream to disagree with it. A parsed one does. An unclosed
     * `[3` must say it was never matched — NONE is that answer, and inheriting
     * the guessed `]` would report a block as closed that never was. */
    block.close = closed ? close_type : ROBO_CSS_TOKEN_NONE;
    result = robo_css_value_append(list, &block, NULL);
    if (result != ROBO_OK) {
        robo_css_value_dispose(&block);
    }
    return result;
}

/** Consume a function whose opening token was already consumed. */
static robo_result_t grammar_consume_function(robo_css_grammar_t *g,
                                              robo_css_value_t *list,
                                              const robo_css_token_t *function_token,
                                              size_t depth)
{
    robo_css_value_t function = ROBO_CSS_CLEARED;
    robo_css_range_t range = function_token->range;
    int flat = (depth + 1u > ROBO_CSS_MAX_NESTING);
    int closed = 0;
    robo_result_t result;

    if (robo_css_value_make_function(&function, token_name_slice(function_token), range,
                                    NULL) != ROBO_OK) {
        return ROBO_OUT_OF_MEMORY;
    }
    if (flat) {
        grammar_fail_at(g, function_token->range.begin,
                        ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX, ROBO_INVALID_ARGUMENT,
                        "nesting is deeper than this build will build; the contents are kept flat");
    }
    result = grammar_consume_container(g, &function, ROBO_CSS_TOKEN_RIGHT_PAREN,
                                       depth + 1u, flat, &range, &closed);
    if (result != ROBO_OK) {
        robo_css_value_dispose(&function);
        return result;
    }
    function.range = range;
    if (closed) {
        function.close = ROBO_CSS_TOKEN_RIGHT_PAREN;
    }
    result = robo_css_value_append(list, &function, NULL);
    if (result != ROBO_OK) {
        robo_css_value_dispose(&function);
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
static robo_result_t grammar_consume_component_value(robo_css_grammar_t *g,
                                                     robo_css_value_t *list,
                                                     size_t depth)
{
    robo_css_token_t token = ROBO_CSS_CLEARED;
    robo_result_t result = grammar_peek(g, &token);

    if (result != ROBO_OK) {
        return result;
    }
    if (token.type == ROBO_CSS_TOKEN_EOF) {
        return ROBO_OK;
    }
    result = grammar_next(g, &token);
    if (result != ROBO_OK) {
        return result;
    }
    if (token.type == ROBO_CSS_TOKEN_FUNCTION) {
        return grammar_consume_function(g, list, &token, depth);
    }
    if (robo_css_token_type_is_block_start(token.type)) {
        return grammar_consume_block(g, list, &token, depth);
    }
    return value_append_token(list, &token);
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
static robo_result_t grammar_consume_component_values(robo_css_grammar_t *g,
                                                      robo_css_value_t *out,
                                                      const robo_css_token_type_t *stops,
                                                      size_t stop_count,
                                                      size_t depth)
{
    for (;;) {
        robo_css_token_t token = ROBO_CSS_CLEARED;
        size_t index;
        robo_result_t result = grammar_peek(g, &token);

        if (result != ROBO_OK) {
            return result;
        }
        if (token.type == ROBO_CSS_TOKEN_EOF) {
            return ROBO_OK;
        }
        for (index = 0u; index < stop_count; index++) {
            if (token.type == stops[index]) {
                return ROBO_OK;
            }
        }
        if (token.type == ROBO_CSS_TOKEN_COMMENT) {
            (void)grammar_next(g, &token);
            continue;
        }
        result = grammar_consume_component_value(g, out, depth);
        if (result != ROBO_OK) {
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
static void grammar_skip_declaration_remnants(robo_css_grammar_t *g)
{
    for (;;) {
        robo_css_token_t token = ROBO_CSS_CLEARED;

        if (grammar_peek(g, &token) != ROBO_OK) {
            return;
        }
        switch (token.type) {
        case ROBO_CSS_TOKEN_EOF:
        case ROBO_CSS_TOKEN_RIGHT_CURLY:
            return;
        case ROBO_CSS_TOKEN_SEMICOLON:
            (void)grammar_next(g, &token);
            return;
        case ROBO_CSS_TOKEN_LEFT_CURLY: {
            robo_css_value_t scratch = ROBO_CSS_CLEARED;
            robo_css_range_t range;

            /* A block inside a bad declaration is part of what is bad: consume
             * it whole, so a `;` inside it cannot end the declaration early. */
            (void)grammar_next(g, &token);
            if (robo_css_value_make_block(&scratch, ROBO_CSS_TOKEN_LEFT_CURLY,
                                          token.range, NULL) != ROBO_OK) {
                return;
            }
            {
                int closed = 0;

                range = token.range;
                (void)grammar_consume_container(g, &scratch, ROBO_CSS_TOKEN_RIGHT_CURLY,
                                                1u, 0, &range, &closed);
            }
            robo_css_value_dispose(&scratch);
            return;
        }
        default:
            (void)grammar_next(g, &token);
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
static void grammar_skip_braced_block(robo_css_grammar_t *g,
                                      int *out_closed,
                                      size_t *out_end)
{
    size_t nesting = 1u;

    *out_closed = 0;
    *out_end = grammar_offset(g);
    for (;;) {
        robo_css_token_t token = ROBO_CSS_CLEARED;

        if (grammar_next(g, &token) != ROBO_OK) {
            return;
        }
        if (token.type == ROBO_CSS_TOKEN_EOF) {
            return;
        }
        if (token.type == ROBO_CSS_TOKEN_LEFT_CURLY) {
            nesting++;
        } else if (token.type == ROBO_CSS_TOKEN_RIGHT_CURLY) {
            nesting--;
            if (nesting == 0u) {
                *out_closed = 1;
                *out_end = token.range.end;
                return;
            }
        } else {
            *out_end = token.range.end;
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
static robo_result_t grammar_consume_declaration(robo_css_grammar_t *g,
                                                 robo_css_declaration_t *out,
                                                 int *out_produced,
                                                 size_t depth)
{
    robo_css_declaration_t declaration = ROBO_CSS_CLEARED;
    robo_css_token_t name = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    robo_css_value_t value = ROBO_CSS_CLEARED;
    robo_css_range_t name_range;
    robo_css_range_t value_range;
    robo_css_range_t declaration_range;
    robo_css_slice_t name_text;
    int important;
    robo_result_t result;

    *out_produced = 0;

    /* The caller has established that an IDENT is next. */
    result = grammar_next(g, &name);
    if (result != ROBO_OK) {
        return result;
    }
    name_text = name.text;
    name_range = name.range;

    grammar_skip_trivia(g);
    result = grammar_peek(g, &token);
    if (result != ROBO_OK) {
        return result;
    }
    if (token.type != ROBO_CSS_TOKEN_COLON) {
        grammar_fail_at(g, name_range.begin, ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION,
                        ROBO_INVALID_ARGUMENT,
                        "a declaration's name must be followed by a colon");
        if (robo_css_declaration_make(&declaration, name_text,
                                      ROBO_CSS_DECLARATION_STATE_INVALID, NULL) != ROBO_OK) {
            return ROBO_OUT_OF_MEMORY;
        }
        (void)robo_css_declaration_set_ranges(&declaration, name_range, name_range,
                                              robo_css_range_at(name_range.end), NULL);
        grammar_skip_declaration_remnants(g);
        *out = declaration;
        *out_produced = 1;
        return ROBO_OK;
    }
    (void)grammar_next(g, &token); /* the colon */

    {
        static const robo_css_token_type_t stops[] = {
            ROBO_CSS_TOKEN_SEMICOLON, ROBO_CSS_TOKEN_RIGHT_CURLY
        };

        result = value_make_list(&value);
        if (result != ROBO_OK) {
            return result;
        }
        result = grammar_consume_component_values(g, &value, stops,
                                                  sizeof(stops) / sizeof(stops[0]), depth);
        if (result != ROBO_OK) {
            robo_css_value_dispose(&value);
            return result;
        }
    }
    /* A value's surrounding whitespace is not part of it — `color:  red  ` and
     * `color:red` are the same declaration — but its interior whitespace is, so
     * only the ends go. */
    value_trim_whitespace(&value);
    /* The flag is taken off the value *before* the declaration exists, because
     * finding it is what removes it — so it is held here and applied below. */
    important = value_take_important(&value);
    value_trim_whitespace(&value);
    value_span_self(&value);
    value_range = value.range;

    declaration_range.begin = name_range.begin;
    declaration_range.end = value_range.end > name_range.end ? value_range.end
                                                            : name_range.end;

    if (robo_css_declaration_make(&declaration, name_text,
                                  ROBO_CSS_DECLARATION_STATE_PARSED, NULL) != ROBO_OK) {
        robo_css_value_dispose(&value);
        return ROBO_OUT_OF_MEMORY;
    }
    if (important) {
        (void)robo_css_declaration_set_important(&declaration, 1, NULL);
    }
    (void)robo_css_declaration_set_ranges(&declaration, declaration_range, name_range,
                                          value_range, NULL);
    result = robo_css_declaration_set_value(&declaration, &value, NULL);
    if (result != ROBO_OK) {
        robo_css_value_dispose(&value);
        robo_css_declaration_dispose(&declaration);
        return ROBO_OUT_OF_MEMORY;
    }

    *out = declaration;
    *out_produced = 1;
    return ROBO_OK;
}

/** Record a declaration-shaped entry that has no name at all (`{ : red; }`). */
static robo_result_t grammar_record_nameless_declaration(robo_css_grammar_t *g,
                                                         robo_css_declaration_list_t *out,
                                                         size_t offset)
{
    robo_css_declaration_t declaration = ROBO_CSS_CLEARED;

    grammar_fail_at(g, offset, ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION,
                    ROBO_INVALID_ARGUMENT, "a declaration was missing its property name");
    if (robo_css_declaration_make(&declaration, robo_css_slice_empty(),
                                  ROBO_CSS_DECLARATION_STATE_INVALID, NULL) != ROBO_OK) {
        return ROBO_OUT_OF_MEMORY;
    }
    (void)robo_css_declaration_set_ranges(&declaration, robo_css_range_at(offset),
                                          robo_css_range_at(offset),
                                          robo_css_range_at(offset), NULL);
    grammar_skip_declaration_remnants(g);
    if (robo_css_declaration_list_append(out, &declaration, NULL) != ROBO_OK) {
        robo_css_declaration_dispose(&declaration);
        return ROBO_OUT_OF_MEMORY;
    }
    return ROBO_OK;
}

/**
 * Consume a style rule's block contents: a list of declarations.
 *
 * Consumes the closing `}` when there is one and reports through @p out_closed
 * whether it was there — a block that ran to the end of the source is a
 * different rule from one that was closed, and only this loop knows which
 * happened. @p depth is the block's own depth.
 */
static robo_result_t grammar_consume_declaration_block(robo_css_grammar_t *g,
                                                       robo_css_declaration_list_t *out,
                                                       size_t depth,
                                                       int *out_closed,
                                                       size_t *out_end)
{
    *out_closed = 0;
    *out_end = grammar_offset(g);

    for (;;) {
        robo_css_token_t token = ROBO_CSS_CLEARED;
        size_t before = grammar_offset(g);
        robo_result_t result;

        result = grammar_peek(g, &token);
        if (result != ROBO_OK) {
            return result;
        }
        *out_end = grammar_offset(g);
        if (token.type == ROBO_CSS_TOKEN_EOF) {
            return ROBO_OK;
        }
        if (token.type == ROBO_CSS_TOKEN_WHITESPACE ||
            token.type == ROBO_CSS_TOKEN_SEMICOLON ||
            token.type == ROBO_CSS_TOKEN_COMMENT) {
            (void)grammar_next(g, &token);
            continue;
        }
        if (token.type == ROBO_CSS_TOKEN_RIGHT_CURLY) {
            (void)grammar_next(g, &token);
            *out_closed = 1;
            *out_end = token.range.end;
            return ROBO_OK;
        }
        if (token.type == ROBO_CSS_TOKEN_IDENT) {
            robo_css_declaration_t declaration = ROBO_CSS_CLEARED;
            int produced = 0;

            result = grammar_consume_declaration(g, &declaration, &produced, depth);
            if (result != ROBO_OK) {
                return result;
            }
            if (produced) {
                if (robo_css_declaration_list_append(out, &declaration, NULL) != ROBO_OK) {
                    robo_css_declaration_dispose(&declaration);
                    return ROBO_OUT_OF_MEMORY;
                }
            }
            grammar_skip_trivia(g);
            if (grammar_peek(g, &token) == ROBO_OK &&
                token.type == ROBO_CSS_TOKEN_SEMICOLON) {
                (void)grammar_next(g, &token);
            }
        } else if (token.type == ROBO_CSS_TOKEN_COLON) {
            result = grammar_record_nameless_declaration(g, out, token.range.begin);
            if (result != ROBO_OK) {
                return result;
            }
        } else if (token.type == ROBO_CSS_TOKEN_AT_KEYWORD) {
            /* An at-rule inside a style rule's block is CSS Nesting, a later
             * phase. It is consumed whole — so its contents cannot be mistaken
             * for declarations — and reported rather than silently dropped. */
            grammar_fail_at(g, token.range.begin, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX,
                            ROBO_INVALID_ARGUMENT,
                            "an at-rule inside a style rule's block is CSS Nesting, a later phase");
            grammar_skip_declaration_remnants(g);
        } else {
            /* `.nested { … }` and anything else rule-shaped: the same
             * treatment, named for what it is rather than as a broken
             * declaration. */
            grammar_fail_at(g, token.range.begin, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX,
                            ROBO_INVALID_ARGUMENT,
                            "a nested rule inside a style rule's block is CSS Nesting, a later phase");
            grammar_skip_declaration_remnants(g);
        }
        /* Every branch above either consumed a token or returned. The guard is
         * a belt on top of that bra: a declaration block that stopped making
         * progress would spin forever, and no future edit may introduce one. */
        if (grammar_offset(g) == before) {
            return ROBO_OK;
        }
    }
}

/** Parse @p source as a declaration list — the inline-style boundary. */
robo_result_t robo_css_grammar_parse_declarations(robo_css_declaration_list_t *out_list,
                                                  const robo_css_source_t *source,
                                                  robo_css_origin_t origin,
                                                  robo_css_error_t *out_error)
{
    robo_css_grammar_t grammar = ROBO_CSS_CLEARED;
    int closed = 0;
    size_t end = 0u;
    robo_result_t result;

    if (!out_list || !source) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "declaration list or source is NULL");
        return ROBO_INVALID_ARGUMENT;
    }
    robo_css_declaration_list_init(out_list);
    if (!robo_css_origin_is_known(origin)) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "unknown origin");
        return ROBO_INVALID_ARGUMENT;
    }

    grammar.source = source;
    grammar.origin = origin;
    result = robo_css_tokenizer_init(&grammar.tokenizer, source, NULL);
    if (result != ROBO_OK) {
        robo_css_tokenizer_dispose(&grammar.tokenizer);
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, result, NULL,
                           "the tokenizer could not be initialized");
        return result;
    }

    result = grammar_consume_declaration_block(&grammar, out_list, 0u, &closed, &end);
    robo_css_tokenizer_dispose(&grammar.tokenizer);

    if (result != ROBO_OK) {
        robo_css_declaration_list_dispose(out_list);
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, result, NULL,
                           "the declaration list could not be built");
        return result;
    }
    if (grammar.has_error) {
        robo_css_error_set(out_error, grammar.error.kind, grammar.error.code,
                           &grammar.error.position, grammar.error.message);
    } else {
        robo_css_error_init(out_error);
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Rules                                                               */
/* ------------------------------------------------------------------ */

static robo_result_t grammar_consume_rule(robo_css_grammar_t *g,
                                          robo_css_rule_t *out,
                                          int *out_produced,
                                          size_t depth);

/**
 * Consume the contents of a block that holds rules, or a whole stylesheet.
 *
 * Consumes the closing `}` when present. @p top_level is what makes CDO and CDC
 * ignorable: at the top of a stylesheet `<!--` and `-->` are the comment tokens
 * CSS tolerates, while inside a block they are ordinary component values that
 * begin a (probably invalid) rule. @p depth is the depth of the block being
 * filled.
 */
static robo_result_t grammar_consume_rule_list(robo_css_grammar_t *g,
                                               robo_css_rule_list_t *out,
                                               size_t depth,
                                               int top_level,
                                               int *out_closed,
                                               size_t *out_end)
{
    *out_closed = 0;
    *out_end = grammar_offset(g);

    for (;;) {
        robo_css_token_t token = ROBO_CSS_CLEARED;
        size_t before = grammar_offset(g);
        robo_result_t result;

        grammar_skip_trivia(g);
        *out_end = grammar_offset(g);
        result = grammar_peek(g, &token);
        if (result != ROBO_OK) {
            return result;
        }
        if (token.type == ROBO_CSS_TOKEN_EOF) {
            /* Reaching the end settles the question: there is no closer left to
             * find, and only a block can be allowed not to have one. */
            *out_closed = top_level;
            return ROBO_OK;
        }
        if (token.type == ROBO_CSS_TOKEN_RIGHT_CURLY) {
            if (!top_level) {
                (void)grammar_next(g, &token);
                *out_closed = 1;
                *out_end = token.range.end;
                return ROBO_OK;
            }
            /* A `}` at the top level closes nothing. It is still consumed, so a
             * stray one cannot make the loop read it forever. */
            grammar_fail_at(g, token.range.begin, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX,
                            ROBO_INVALID_ARGUMENT, "a closing brace with no block to close");
            (void)grammar_next(g, &token);
            continue;
        }
        if (top_level &&
            (token.type == ROBO_CSS_TOKEN_CDO || token.type == ROBO_CSS_TOKEN_CDC)) {
            (void)grammar_next(g, &token);
            continue;
        }

        {
            robo_css_rule_t rule = ROBO_CSS_CLEARED;
            int produced = 0;

            result = grammar_consume_rule(g, &rule, &produced, depth);
            if (result != ROBO_OK) {
                return result;
            }
            if (produced) {
                if (robo_css_rule_list_append(out, &rule, NULL) != ROBO_OK) {
                    robo_css_rule_dispose(&rule);
                    return ROBO_OUT_OF_MEMORY;
                }
            }
        }
        if (grammar_offset(g) == before) {
            return ROBO_OK;
        }
    }
}

/**
 * Read the block starting at @p block_open again as a *declaration* list.
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
 * to @p block_open, so a prelude can never be mistaken for a descriptor.
 *
 * @p out_error and @p out_recoveries receive this pass's own verdict rather
 * than writing to the caller's parse, because the caller may yet discard it.
 */
static robo_result_t grammar_reread_block_as_declarations(
    const robo_css_source_t *source,
    size_t block_open,
    size_t depth,
    robo_css_declaration_list_t *out,
    robo_css_error_t *out_error,
    size_t *out_recoveries)
{
    robo_css_grammar_t scratch = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    int closed = 0;
    size_t end = 0u;
    robo_result_t result;

    *out_recoveries = 0u;
    result = robo_css_tokenizer_init(&scratch.tokenizer, source, NULL);
    if (result != ROBO_OK) {
        return result;
    }
    /* The origin is only ever validated by the two entry points, never read by
     * the grammar, so the scratch pass does not touch it. */
    scratch.source = source;

    for (;;) {
        result = grammar_peek(&scratch, &token);
        if (result != ROBO_OK) {
            break;
        }
        if (token.type == ROBO_CSS_TOKEN_EOF ||
            token.range.begin >= block_open) {
            break;
        }
        (void)grammar_next(&scratch, &token);
    }
    if (token.type == ROBO_CSS_TOKEN_LEFT_CURLY) {
        (void)grammar_next(&scratch, &token); /* the `{` */
        result = grammar_consume_declaration_block(&scratch, out, depth, &closed, &end);
    }
    if (result == ROBO_OK) {
        *out_error = scratch.error;
        *out_recoveries = scratch.recoveries;
    }
    robo_css_tokenizer_dispose(&scratch.tokenizer);
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
 * a declaration list (see grammar_reread_block_as_declarations). Legal CSS fills
 * exactly one of the two — a group at-rule's block holds rules, a descriptor
 * at-rule's holds declarations — so `robo_css_at_rule_rule_count(at) == 0`
 * together with a non-zero declaration count is the signature of a descriptor
 * at-rule, and both being non-zero is the signature of a block that no at-rule
 * could accept, kept rather than resolved.
 */
static robo_result_t grammar_consume_at_rule(robo_css_grammar_t *g,
                                             robo_css_rule_t *out,
                                             int *out_produced,
                                             size_t depth)
{
    static const robo_css_token_type_t stops[] = {
        ROBO_CSS_TOKEN_SEMICOLON, ROBO_CSS_TOKEN_LEFT_CURLY,
        ROBO_CSS_TOKEN_RIGHT_CURLY
    };
    robo_css_token_t keyword = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    robo_css_value_t prelude = ROBO_CSS_CLEARED;
    robo_css_slice_t name;
    robo_css_slice_t prelude_text = robo_css_slice_empty();
    robo_css_range_t prelude_range;
    robo_css_range_t rule_range = robo_css_range_at(0u);
    size_t block_open = 0u;
    int has_block = 0;
    int block_closed = 0;
    robo_result_t result;

    *out_produced = 0;

    result = grammar_next(g, &keyword);
    if (result != ROBO_OK) {
        return result;
    }
    name = token_name_slice(&keyword);
    rule_range.begin = keyword.range.begin;
    if (robo_css_slice_is_empty(name)) {
        grammar_fail_at(g, keyword.range.begin, ROBO_CSS_ERROR_KIND_MALFORMED_RULE,
                        ROBO_INVALID_ARGUMENT, "an at-rule needs a name after its `@`");
        grammar_skip_declaration_remnants(g);
        return ROBO_OK;
    }

    result = value_make_list(&prelude);
    if (result != ROBO_OK) {
        return result;
    }
    result = grammar_consume_component_values(g, &prelude, stops,
                                              sizeof(stops) / sizeof(stops[0]), depth);
    if (result != ROBO_OK) {
        robo_css_value_dispose(&prelude);
        return result;
    }
    value_trim_whitespace(&prelude);
    value_span_self(&prelude);
    prelude_range = prelude.range;
    rule_range.end = prelude_range.end > rule_range.begin ? prelude_range.end
                                                          : rule_range.begin;
    if (prelude_range.begin < prelude_range.end && g->source) {
        /* The prelude's bytes, borrowed from the source. They are the same
         * bytes the component values cover, so the two cannot disagree. */
        (void)robo_css_source_range_slice(g->source, prelude_range, &prelude_text);
    }

    if (robo_css_rule_make_at(out, name, NULL) != ROBO_OK) {
        robo_css_value_dispose(&prelude);
        return ROBO_OUT_OF_MEMORY;
    }
    (void)robo_css_at_rule_set_prelude_values(&out->at, &prelude, NULL);
    out->at.prelude_range = prelude_range;

    result = grammar_peek(g, &token);
    if (result != ROBO_OK) {
        robo_css_rule_dispose(out);
        return result;
    }
    if (token.type == ROBO_CSS_TOKEN_SEMICOLON) {
        (void)grammar_next(g, &token);
        rule_range.end = token.range.end;
    } else if (token.type == ROBO_CSS_TOKEN_LEFT_CURLY) {
        has_block = 1;
        (void)grammar_next(g, &token); /* the `{` */
        block_open = token.range.begin;
    }
    /* `has_block` has to be on the record before any child is added to it, so
     * the prelude and the block flag are recorded here — once, now that the
     * block's presence is known — and the children follow. */
    (void)robo_css_at_rule_set_prelude(&out->at, prelude_text, has_block, NULL);
    if (has_block) {
        size_t block_end = 0u;
        robo_css_grammar_state_t before_block = grammar_state_save(g);

        if (depth + 1u > ROBO_CSS_MAX_NESTING) {
            grammar_fail_at(g, block_open, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX,
                            ROBO_INVALID_ARGUMENT,
                            "at-rules are nested deeper than this build will build; the block is skipped");
            grammar_skip_braced_block(g, &block_closed, &block_end);
        } else {
            out->at.children = (robo_css_rule_list_t *)robo_calloc(
                1u, sizeof(robo_css_rule_list_t));
            if (!out->at.children) {
                robo_css_rule_dispose(out);
                return ROBO_OUT_OF_MEMORY;
            }
            robo_css_rule_list_init(out->at.children);
            result = grammar_consume_rule_list(g, out->at.children, depth + 1u, 0,
                                               &block_closed, &block_end);
            if (result != ROBO_OK) {
                robo_css_rule_dispose(out);
                return result;
            }
            /* Nothing rule-shaped in the block: read the same bytes as a
             * declaration list, because that is what a descriptor at-rule's
             * block is. The cost is one more pass over a block that produced no
             * rules, and it buys `@font-face` its descriptors. */
            if (robo_css_at_rule_rule_count(&out->at) == 0u &&
                block_end > block_open + 1u) {
                robo_css_declaration_list_t descriptors = ROBO_CSS_CLEARED;
                robo_css_error_t second_reading = ROBO_CSS_CLEARED;
                size_t second_recoveries = 0u;

                robo_css_declaration_list_init(&descriptors);
                result = grammar_reread_block_as_declarations(g->source, block_open,
                                                             depth + 1u, &descriptors,
                                                             &second_reading,
                                                             &second_recoveries);
                if (result == ROBO_OK &&
                    !robo_css_declaration_list_is_empty(&descriptors)) {
                    /* The block *was* declarations, so the rule-list reading's
                     * complaint described this build's guess rather than the
                     * stylesheet. Discard it and let the successful reading
                     * speak. */
                    grammar_state_restore(g, &before_block);
                    grammar_adopt_error(g, &second_reading, second_recoveries);
                    if (robo_css_at_rule_take_declarations(&out->at, &descriptors,
                                                          NULL) != ROBO_OK) {
                        robo_css_declaration_list_dispose(&descriptors);
                        robo_css_rule_dispose(out);
                        return ROBO_OUT_OF_MEMORY;
                    }
                }
                robo_css_declaration_list_dispose(&descriptors);
                if (result != ROBO_OK) {
                    robo_css_rule_dispose(out);
                    return result;
                }
            }
        }
        rule_range.end = block_end > rule_range.end ? block_end : rule_range.end;
    }
    /* No `;` and no block: the at-rule ended at the end of the input or at the
     * enclosing block's `}`, and both are legal — `@import "x"` with no final
     * semicolon is what the last line of a stylesheet looks like — so this is
     * not an error. */
    (void)robo_css_at_rule_set_range(&out->at, rule_range, NULL);
    if (has_block) {
        (void)robo_css_at_rule_set_block_closed(&out->at, block_closed, NULL);
        if (!block_closed) {
            grammar_fail_at(g, rule_range.begin, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF,
                            ROBO_INVALID_ARGUMENT,
                            "an at-rule's block was not closed before the end of the source");
        }
    }
    *out_produced = 1;
    return ROBO_OK;
}

/** Consume a qualified (style) rule. */
static robo_result_t grammar_consume_style_rule(robo_css_grammar_t *g,
                                                robo_css_rule_t *out,
                                                int *out_produced,
                                                size_t depth)
{
    static const robo_css_token_type_t stops[] = {
        ROBO_CSS_TOKEN_LEFT_CURLY, ROBO_CSS_TOKEN_RIGHT_CURLY
    };
    robo_css_value_t prelude = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    robo_css_range_t prelude_range;
    robo_css_range_t block_range = robo_css_range_at(0u);
    robo_css_range_t rule_range = robo_css_range_at(0u);
    int block_closed = 0;
    robo_result_t result;

    *out_produced = 0;

    result = value_make_list(&prelude);
    if (result != ROBO_OK) {
        return result;
    }
    result = grammar_consume_component_values(g, &prelude, stops,
                                              sizeof(stops) / sizeof(stops[0]), depth);
    if (result != ROBO_OK) {
        robo_css_value_dispose(&prelude);
        return result;
    }
    value_trim_whitespace(&prelude);
    value_span_self(&prelude);
    prelude_range = prelude.range;

    result = grammar_peek(g, &token);
    if (result != ROBO_OK) {
        robo_css_value_dispose(&prelude);
        return result;
    }
    if (token.type == ROBO_CSS_TOKEN_EOF) {
        grammar_fail_at(g, prelude_range.begin < prelude_range.end ? prelude_range.begin
                                                                   : grammar_offset(g),
                        ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF, ROBO_INVALID_ARGUMENT,
                        "a rule's declaration block was missing before the end of the source");
        robo_css_value_dispose(&prelude);
        return ROBO_OK;
    }
    if (token.type == ROBO_CSS_TOKEN_RIGHT_CURLY) {
        /* The enclosing block ended first, so this rule never got a block. The
         * `}` is left for whoever owns it. */
        grammar_fail_at(g, prelude_range.begin < prelude_range.end ? prelude_range.begin
                                                                   : token.range.begin,
                        ROBO_CSS_ERROR_KIND_MALFORMED_RULE, ROBO_INVALID_ARGUMENT,
                        "a rule ended without a declaration block");
        robo_css_value_dispose(&prelude);
        return ROBO_OK;
    }
    if (prelude.children.count == 0u) {
        /* `{ color: red }` with nothing before the brace. A qualified rule with
         * an empty prelude can never be a selector, and this is the one
         * prelude-shaped problem 9.3 can settle without a selector parser: the
         * rule is consumed and dropped, and the block's declarations with it. */
        size_t block_end = 0u;

        grammar_fail_at(g, token.range.begin, ROBO_CSS_ERROR_KIND_MALFORMED_RULE,
                        ROBO_INVALID_ARGUMENT,
                        "a rule had no selector before its declaration block");
        (void)grammar_next(g, &token); /* the `{` */
        /* The block is consumed and discarded rather than parsed: its
         * declarations belong to a rule that cannot exist. */
        grammar_skip_braced_block(g, &block_closed, &block_end);
        robo_css_value_dispose(&prelude);
        return ROBO_OK;
    }

    if (robo_css_rule_make_style(out, NULL) != ROBO_OK) {
        robo_css_value_dispose(&prelude);
        return ROBO_OUT_OF_MEMORY;
    }
    (void)grammar_next(g, &token); /* the `{` */
    block_range = token.range;
    {
        int inner_closed = 0;
        size_t block_end = block_range.end;

        if (depth + 1u > ROBO_CSS_MAX_NESTING) {
            /* No rule object is built for the block: its contents are consumed
             * and dropped rather than recursed into. */
            grammar_fail_at(g, block_range.begin, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX,
                            ROBO_INVALID_ARGUMENT,
                            "rules are nested deeper than this build will build; the block is skipped");
            grammar_skip_braced_block(g, &inner_closed, &block_end);
        } else {
            result = grammar_consume_declaration_block(g, &out->style.declarations,
                                                       depth + 1u, &inner_closed,
                                                       &block_end);
            if (result != ROBO_OK) {
                robo_css_rule_dispose(out);
                robo_css_value_dispose(&prelude);
                return result;
            }
        }
        block_closed = inner_closed;
        block_range.end = block_end > block_range.end ? block_end : block_range.end;
    }

    rule_range.begin = prelude_range.begin;
    rule_range.end = block_range.end;
    (void)robo_css_style_rule_set_prelude_values(&out->style, &prelude, NULL);
    (void)robo_css_style_rule_set_ranges(&out->style, rule_range, prelude_range,
                                         block_range, NULL);
    (void)robo_css_style_rule_set_block_closed(&out->style, block_closed, NULL);
    if (!block_closed) {
        grammar_fail_at(g, rule_range.begin, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF,
                        ROBO_INVALID_ARGUMENT,
                        "a rule's declaration block was not closed before the end of the source");
    }
    *out_produced = 1;
    return ROBO_OK;
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
static robo_result_t grammar_consume_rule(robo_css_grammar_t *g,
                                          robo_css_rule_t *out,
                                          int *out_produced,
                                          size_t depth)
{
    robo_css_token_t token = ROBO_CSS_CLEARED;

    *out_produced = 0;
    if (grammar_peek(g, &token) != ROBO_OK) {
        return ROBO_OK;
    }
    if (token.type == ROBO_CSS_TOKEN_AT_KEYWORD) {
        return grammar_consume_at_rule(g, out, out_produced, depth);
    }
    return grammar_consume_style_rule(g, out, out_produced, depth);
}

/** Parse @p source into @p out_rules. */
robo_result_t robo_css_grammar_parse_stylesheet(robo_css_rule_list_t *out_rules,
                                                const robo_css_source_t *source,
                                                robo_css_origin_t origin,
                                                robo_css_error_t *out_error)
{
    robo_css_grammar_t grammar = ROBO_CSS_CLEARED;
    int closed = 0;
    size_t end = 0u;
    robo_result_t result;

    if (!out_rules || !source) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "rule list or source is NULL");
        return ROBO_INVALID_ARGUMENT;
    }
    robo_css_rule_list_init(out_rules);
    if (!robo_css_origin_is_known(origin)) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "unknown origin");
        return ROBO_INVALID_ARGUMENT;
    }

    grammar.source = source;
    grammar.origin = origin;
    result = robo_css_tokenizer_init(&grammar.tokenizer, source, NULL);
    if (result != ROBO_OK) {
        robo_css_tokenizer_dispose(&grammar.tokenizer);
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, result, NULL,
                           "the tokenizer could not be initialized");
        return result;
    }

    result = grammar_consume_rule_list(&grammar, out_rules, 0u, 1, &closed, &end);
    robo_css_tokenizer_dispose(&grammar.tokenizer);

    if (result != ROBO_OK) {
        robo_css_rule_list_dispose(out_rules);
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, result, NULL,
                           "the rule list could not be built");
        return result;
    }
    if (grammar.has_error) {
        robo_css_error_set(out_error, grammar.error.kind, grammar.error.code,
                           &grammar.error.position, grammar.error.message);
    } else {
        robo_css_error_init(out_error);
    }
    return ROBO_OK;
}
