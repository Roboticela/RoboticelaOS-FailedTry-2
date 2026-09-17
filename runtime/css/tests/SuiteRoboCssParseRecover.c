/*
 * RoboCSS parser: malformed input, recovery, and consuming the real tokenizer
 * (Phase 9.3).
 *
 * The sheet/declaration/value suites pin what the grammar produces from input
 * that has a shape. This one pins what it produces from input that does not,
 * and it is the suite that decides whether a *recovering* parser is a real one:
 *
 *   - **A problem is local.** Every test here puts a valid rule after the
 *     broken construct, because "the parse did not crash" is worth almost
 *     nothing next to "the next rule is still there, in order, with its
 *     declarations". Recovery that loses the rest of the sheet is not recovery.
 *   - **A rejection is visible.** A declaration that was entry-shaped but not
 *     `name: value` is kept as an INVALID declaration rather than dropped, so
 *     the count reflects what the author wrote and not what the parser liked.
 *   - **The parser lexes nothing.** Nothing here re-reads the source as bytes:
 *     every component value is checked against a token the 9.2 tokenizer
 *     produced for the same bytes, at the same offsets. That is what makes
 *     "the parser consumes the tokenizer" a tested property instead of a
 *     structural claim about which headers are included.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Malformed rules                                                     */
/* ------------------------------------------------------------------ */

/**
 * The four shapes a broken qualified rule can take, and what each one leaves.
 *
 * They are one test rather than four because the thing being pinned is the
 * contrast: an unclosed block still *is* a rule, a prelude that never reaches a
 * `{` is not a rule at all, and an empty prelude is a rule that cannot exist.
 * A table makes that read as one decision instead of three accidents.
 */
static void test_css_parse_malformed_rule_shapes(void)
{
    static const struct {
        const char *label;
        const char *text;
        const char *prelude; /* expected prelude shape when the rule survives */
        size_t rules;
        size_t declarations; /* meaningful only when rules == 1 */
        int block_closed;    /* meaningful only when rules == 1 */
        robo_css_error_kind_t error;
    } cases[] = {
        /* The block was never closed. The rule exists, says so, and kept the
         * declaration it had read. */
        { "unclosed_block", "p { color: red;\n", "p",
          1u, 1u, 0, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF },
        /* A prelude with no `{` at all: consumed to EOF and dropped, because a
         * qualified rule without a block is not a rule. */
        { "no_block", "p color: red;\n", "",
          0u, 0u, 0, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF },
        /* `{ color: red }` with nothing before the brace. Consumed and dropped:
         * no selector list can be empty, so the rule can never exist and its
         * declarations have no owner. */
        { "empty_prelude", "{ color: red; }\n", "",
          0u, 0u, 0, ROBO_CSS_ERROR_KIND_MALFORMED_RULE },
        /* A selector that reaches its `{` and then runs out: still a rule, and
         * an empty one. The prelude survived the missing `}` and the empty
         * block is not an error of its own. */
        { "selector_only", ".foo {\n", ".,foo",
          1u, 0u, 0, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_stylesheet_id_t sheet = css_parse_sheet(cases[index].text, &error);

        ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), cases[index].rules);
        ROBO_CHECK_EQ_INT(error.kind, cases[index].error);
        /* A recovered parse still *completed*, which is the state a caller
         * branches on: PARSED means "there are rules here", not "nothing was
         * wrong". */
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_state(sheet),
                          ROBO_CSS_STYLESHEET_STATE_PARSED);

        if (cases[index].rules == 1u) {
            const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, 0u);
            char shape[64];

            ROBO_CHECK(rule != NULL);
            ROBO_CHECK_EQ_INT(robo_css_style_rule_block_closed(rule),
                              cases[index].block_closed);
            /* The prelude survived intact even though the block did not: the
             * selector text is not a casualty of the missing `}`. */
            css_prelude_shape(rule, shape, sizeof(shape));
            ROBO_CHECK_STR_EQ(shape, cases[index].prelude);
            ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(rule),
                              cases[index].declarations);
        }

        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    }
}

/** A stray `}` at the top level closes nothing, and does not stop the parse. */
static void test_css_parse_recover_stray_closing_brace(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet =
        css_parse_sheet("}\n}\np { color: red; }\n", &error);
    char shape[64];

    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    css_prelude_shape(css_sheet_style_rule(sheet, 0u), shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "p");
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(
                          css_sheet_style_rule(sheet, 0u)),
                      1u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Error recovery                                                      */
/* ------------------------------------------------------------------ */

/**
 * A rule whose block holds a broken declaration is still a rule, and the next
 * rule is still found.
 *
 * `broken { invalid }` is the case where recovery is *not* needed: the entry is
 * declaration-shaped, the parser notices the colon is missing, records it and
 * keeps going with an INVALID declaration. Nothing is lost — which is the point
 * of the test, because a recovering parser that dropped the declaration would
 * also pass a weaker "did not crash" assertion.
 */
static void test_css_parse_recover_broken_declaration_keeps_the_rule(void)
{
    static const char *text =
        "broken { invalid }\n"
        "p { color: red; }\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    const robo_css_style_rule_t *broken = css_sheet_style_rule(sheet, 0u);
    const robo_css_style_rule_t *valid = css_sheet_style_rule(sheet, 1u);
    char shape[64];

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 2u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION);

    ROBO_CHECK(broken != NULL);
    css_prelude_shape(broken, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "broken");
    ROBO_CHECK_EQ_INT(robo_css_style_rule_block_closed(broken), 1);

    /* One declaration, kept, and unusable. `is_usable` is the accessor a later
     * phase reads before it tries to cascade it, and it is 0 here. */
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(broken), 1u);
    {
        const robo_css_declaration_t *declaration =
            robo_css_style_rule_declaration(broken, 0u);

        ROBO_CHECK(declaration != NULL);
        ROBO_CHECK_EQ_INT(declaration->state, ROBO_CSS_DECLARATION_STATE_INVALID);
        ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_declaration_name(declaration),
                                               "invalid"));
        ROBO_CHECK_EQ_INT(robo_css_declaration_is_usable(declaration), 0);
        /* The name was recorded and nothing was invented for the value. */
        ROBO_CHECK_EQ_INT(robo_css_declaration_has_value(declaration), 0);
    }

    /* The valid rule after it is intact — prelude, block and declaration. */
    ROBO_CHECK(valid != NULL);
    css_prelude_shape(valid, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "p");
    ROBO_CHECK_EQ_INT(robo_css_style_rule_block_closed(valid), 1);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(valid), 1u);
    ROBO_CHECK(css_parse_declaration_is(valid, 0u, "color"));

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/**
 * A rule the parser *dropped* costs exactly that rule.
 *
 * `{ color: red; }` is discarded and its block consumed; the rule after it must
 * still be produced, with the correct prelude. This is the case where recovery
 * genuinely has to move past something, so it is the one that would fail if the
 * empty-prelude path consumed too much or too little.
 */
static void test_css_parse_recover_dropped_rule_costs_one_rule(void)
{
    static const char *text =
        "{ color: red; }\n"
        "p { color: red; }\n"
        "a { margin: 0; }\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    char shape[64];

    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_MALFORMED_RULE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 2u);

    /* Order is source order, and the dropped rule left no hole in it. */
    css_prelude_shape(css_sheet_style_rule(sheet, 0u), shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "p");
    css_prelude_shape(css_sheet_style_rule(sheet, 1u), shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "a");

    /* Each surviving rule kept its own declaration, so neither block was eaten
     * by the recovery that preceded it. */
    ROBO_CHECK(css_parse_declaration_is(css_sheet_style_rule(sheet, 0u), 0u, "color"));
    ROBO_CHECK(css_parse_declaration_is(css_sheet_style_rule(sheet, 1u), 0u, "margin"));

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/**
 * Several independent problems in one sheet, with the valid rules surviving all
 * of them and the *first* problem reported.
 *
 * Reporting the first is the contract (`robo_css_stylesheet_parse`): a caller
 * wants one position to point a user at, and which one is deterministic rather
 * than whichever the last pass happened to see.
 */
static void test_css_parse_recover_many_problems_keep_all_valid_rules(void)
{
    static const char *text =
        "}\n"
        "{ color: red; }\n"
        "a { color: red }\n"
        "b { margin: 0; }\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    char shape[64];

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 2u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX);
    /* The stray `}` is the first byte of the source, so that is where the
     * reported position points — not at a later, more interesting problem. */
    ROBO_CHECK_EQ_INT(error.position.offset, 0u);

    css_prelude_shape(css_sheet_style_rule(sheet, 0u), shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "a");
    css_prelude_shape(css_sheet_style_rule(sheet, 1u), shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "b");

    /* `a`'s declaration has no final semicolon and is still terminated by the
     * `}`; `b`'s is ordinary. */
    ROBO_CHECK(css_parse_declaration_is(css_sheet_style_rule(sheet, 0u), 0u, "color"));
    ROBO_CHECK(css_parse_declaration_is(css_sheet_style_rule(sheet, 1u), 0u, "margin"));

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/**
 * A nested rule inside a style rule's block is CSS Nesting, and this build does
 * not implement it.
 *
 * What it must *not* do is mistake the nested block for declarations. The
 * nested rule is consumed whole — braces and all — its `color: blue` is never
 * seen as its parent's, and the declaration after it is still read. That last
 * part is the assertion worth having: a skip that consumed to the next `}`
 * without balancing would end the parent one declaration early.
 */
static void test_css_parse_recover_nested_rule_is_skipped_whole(void)
{
    static const char *text =
        "p {\n"
        "    color: red;\n"
        "    .b { color: blue; }\n"
        "    margin: 0;\n"
        "}\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, 0u);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX);
    ROBO_CHECK(rule != NULL);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_block_closed(rule), 1);

    /* Two declarations, not three: `.b`'s `color: blue` was inside the skipped
     * nested rule and never reached the parent's list. */
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(rule), 2u);
    ROBO_CHECK(css_parse_declaration_is(rule, 0u, "color"));
    ROBO_CHECK(css_parse_declaration_is(rule, 1u, "margin"));

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/** An at-rule inside a style rule's block is skipped the same way. */
static void test_css_parse_recover_at_rule_inside_a_block(void)
{
    static const char *text =
        "p {\n"
        "    color: red;\n"
        "    @media screen { }\n"
        "    margin: 0;\n"
        "}\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, 0u);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(rule), 2u);
    ROBO_CHECK(css_parse_declaration_is(rule, 0u, "color"));
    ROBO_CHECK(css_parse_declaration_is(rule, 1u, "margin"));

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/**
 * Rule nesting deeper than the build's bound is flattened, and the rest of the
 * sheet is unharmed.
 *
 * The bound exists so that a hostile or generated sheet cannot drive the
 * grammar's one recursion off the stack. The failure mode a bound introduces is
 * the opposite one — a parse that gives up and loses everything after the deep
 * construct — so this test builds a chain well past the bound and then a
 * perfectly ordinary rule, and asserts both that the deep chain is still *there*
 * (each level produced, the innermost one empty) and that the ordinary rule
 * survived.
 */
static void test_css_parse_recover_rule_nesting_bound(void)
{
    enum { DEPTH = ROBO_CSS_MAX_NESTING + 8u };
    char text[DEPTH * 20u + 64u];
    size_t offset = 0u;
    size_t depth;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    const robo_css_at_rule_t *level;
    size_t built;

    /* Rule nesting in this build is *at-rule* nesting: a style rule's block is
     * a declaration list (a rule inside it is CSS Nesting, skipped), so the only
     * rule-list recursion to bound is a group block's, which is what this chain
     * is made of. */
    for (depth = 0u; depth < (size_t)DEPTH; depth++) {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "@media x{");
    }
    for (depth = 0u; depth < (size_t)DEPTH; depth++) {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "}");
    }
    (void)snprintf(text + offset, sizeof(text) - offset,
                   "\np { color: red; }\n");

    sheet = css_parse_sheet(text, &error);

    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX);
    /* The giant chain is one top-level rule; the ordinary rule is the other.
     * The bound being reached must not cost the *top-level* list anything. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 2u);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(
                          css_sheet_style_rule(sheet, 1u)),
                      1u);
    ROBO_CHECK(css_parse_declaration_is(css_sheet_style_rule(sheet, 1u), 0u, "color"));

    /* Walk the chain down. Each level up to the bound is a real at-rule with a
     * real child list; the level that would open an over-deep block is still
     * produced, still closed, and simply has no children — so the walk stops
     * there rather than running off the end of a tree that did not stop. */
    level = robo_css_rule_at(robo_css_stylesheet_rule(sheet, 0u));
    built = 0u;
    while (level) {
        ROBO_CHECK(robo_css_slice_equals_ascii(level->name, "media"));
        ROBO_CHECK_EQ_INT(level->has_block, 1);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_block_closed(level), 1);
        built++;
        if (robo_css_at_rule_rule_count(level) == 0u) {
            break;
        }
        ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(level), 1u);
        level = robo_css_rule_at(robo_css_at_rule_rule_at(level, 0u));
        ROBO_CHECK(built <= (size_t)DEPTH);
    }

    /* The chain was built down to the bound and stopped *at* it: exactly the
     * levels the bound allows, no fewer (which would lose rules) and no more
     * (which would mean no bound was applied). */
    ROBO_CHECK_EQ_INT(built, (size_t)ROBO_CSS_MAX_NESTING + 1u);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Consuming the tokenizer                                             */
/* ------------------------------------------------------------------ */

/** Collect every leaf token of a value tree into @p out, depth-first. */
static size_t css_collect_value_tokens(const robo_css_value_t *value,
                                       robo_css_token_t *out,
                                       size_t capacity)
{
    size_t count = 0u;
    size_t index;

    if (!value) {
        return 0u;
    }
    if (value->kind == ROBO_CSS_VALUE_KIND_TOKEN) {
        if (count < capacity) {
            out[count] = value->token;
        }
        return 1u;
    }
    for (index = 0u; index < value->children.count; index++) {
        size_t added = css_collect_value_tokens(&value->children.items[index],
                                                out + (count < capacity ? count : 0u),
                                                count < capacity ? capacity - count : 0u);

        count += added;
    }
    return count;
}

/**
 * Every component value the parser produced is a token the *tokenizer*
 * produced, for the same bytes.
 *
 * This is the integration property stated as an assertion rather than as a
 * claim about includes: the parse is walked to its leaves, and each leaf's type
 * and range must appear in an independent lex of the same source. A parser that
 * scanned bytes itself, or trimmed a token's range, or classified a `#06c` as
 * something other than what 9.2 called it, fails here — and nothing else in the
 * suite would notice, because the shapes would still match.
 */
static void test_css_parse_integration_values_are_the_tokenizers_tokens(void)
{
    static const char *text =
        "p { color: #06c; width: 10px; font: 12px/1.5 \"Roboto\", sans-serif; }\n"
        "@media (min-width: 30em) { a[href^=\"x\"] { margin: 0 4% -2px; } }\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    robo_css_token_t leaves[64];
    size_t leaf_count = 0u;
    size_t index;
    size_t matched = 0u;
    size_t rule_index;

    css_source_of_string(&source, text);
    ROBO_CHECK(css_lex(&source, &log) > 0u);
    ROBO_CHECK_EQ_INT(log.truncated, 0);

    for (rule_index = 0u; rule_index < robo_css_stylesheet_rule_count(sheet);
         rule_index++) {
        const robo_css_style_rule_t *rule =
            css_sheet_style_rule(sheet, rule_index);
        size_t declaration_index;

        if (rule) {
            leaf_count += css_collect_value_tokens(
                robo_css_style_rule_prelude_values(rule), leaves + leaf_count,
                sizeof(leaves) / sizeof(leaves[0]) - leaf_count);
            for (declaration_index = 0u;
                 declaration_index < robo_css_style_rule_declaration_count(rule);
                 declaration_index++) {
                leaf_count += css_collect_value_tokens(
                    robo_css_declaration_value(
                        robo_css_style_rule_declaration(rule, declaration_index)),
                    leaves + leaf_count,
                    sizeof(leaves) / sizeof(leaves[0]) - leaf_count);
            }
            continue;
        }
        /* The `@media` block: its prelude and its nested rules' leaves too. */
        {
            const robo_css_at_rule_t *at =
                robo_css_rule_at(robo_css_stylesheet_rule(sheet, rule_index));
            size_t nested;

            if (!at) {
                continue;
            }
            leaf_count += css_collect_value_tokens(
                robo_css_at_rule_prelude_values(at), leaves + leaf_count,
                sizeof(leaves) / sizeof(leaves[0]) - leaf_count);
            for (nested = 0u; nested < robo_css_at_rule_rule_count(at); nested++) {
                const robo_css_style_rule_t *inner =
                    robo_css_rule_style(robo_css_at_rule_rule_at(at, nested));
                size_t declaration_index;

                if (!inner) {
                    continue;
                }
                leaf_count += css_collect_value_tokens(
                    robo_css_style_rule_prelude_values(inner), leaves + leaf_count,
                    sizeof(leaves) / sizeof(leaves[0]) - leaf_count);
                for (declaration_index = 0u;
                     declaration_index < robo_css_style_rule_declaration_count(inner);
                     declaration_index++) {
                    leaf_count += css_collect_value_tokens(
                        robo_css_declaration_value(
                            robo_css_style_rule_declaration(inner, declaration_index)),
                        leaves + leaf_count,
                        sizeof(leaves) / sizeof(leaves[0]) - leaf_count);
                }
            }
        }
    }

    /* The fixture is large enough that a walk finding nothing would be a bug in
     * the walk rather than a fact about the parse. */
    ROBO_CHECK(leaf_count >= 20u);
    ROBO_CHECK(leaf_count <= sizeof(leaves) / sizeof(leaves[0]));

    for (index = 0u; index < leaf_count; index++) {
        size_t token_index;
        int found = 0;

        for (token_index = 0u; token_index < log.count && !found; token_index++) {
            const robo_css_token_t *token = css_token_at(&log, token_index);

            if (!token || token->type != leaves[index].type) {
                continue;
            }
            if (token->range.begin != leaves[index].range.begin ||
                token->range.end != leaves[index].range.end) {
                continue;
            }
            /* Same range *and* same classification: the hash kind and the
             * dimension unit have to agree as well, because those are the
             * decisions the tokenizer made and 9.3 must not re-make. */
            if (token->hash != leaves[index].hash ||
                token->unit.length != leaves[index].unit.length) {
                continue;
            }
            found = 1;
        }
        ROBO_CHECK(found);
        matched += (size_t)found;
    }
    ROBO_CHECK_EQ_INT(matched, leaf_count);

    /* And the numeric payload came across unchanged: `10px` is a dimension
     * whose unit is `px` and whose number is 10, read by 9.2 and merely carried
     * by 9.3. */
    {
        const robo_css_declaration_t *width = robo_css_style_rule_declaration(
            css_sheet_style_rule(sheet, 0u), 1u);
        const robo_css_value_t *value = robo_css_declaration_value(width);
        const robo_css_value_t *dimension;

        ROBO_CHECK(value != NULL);
        ROBO_CHECK_EQ_INT(value->children.count, 1u);
        dimension = &value->children.items[0];
        ROBO_CHECK_EQ_INT(dimension->kind, ROBO_CSS_VALUE_KIND_TOKEN);
        ROBO_CHECK_EQ_INT(dimension->token.type, ROBO_CSS_TOKEN_DIMENSION);
        ROBO_CHECK(robo_css_slice_equals_ascii(dimension->token.unit, "px"));
        CSS_CHECK_DOUBLE(dimension->token.number, 10.0);
    }

    robo_css_source_dispose(&source);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/**
 * Parsing the same bytes twice produces the same tree, and a second parse of
 * one sheet does not accumulate rules.
 *
 * Both halves matter for the same reason: a parser that appends to a rule list
 * it was handed rather than filling a fresh one would double every rule on a
 * reparse, and nothing in a single-parse test would ever show it.
 */
static void test_css_parse_integration_reparse_is_identical_and_does_not_accumulate(void)
{
    static const char *text =
        "p { color: red; }\n"
        "@media screen { a { margin: 0; } }\n"
        "b { padding: 1px 2px; }\n";
    robo_css_stylesheet_id_t sheet;
    robo_css_error_t first = ROBO_CSS_CLEARED;
    robo_css_error_t second = ROBO_CSS_CLEARED;
    char before[256];
    char after[256];
    size_t index;

    sheet = css_parse_sheet(text, &first);
    css_shape(robo_css_at_rule_prelude_values(
                  robo_css_rule_at(robo_css_stylesheet_rule(sheet, 1u))),
              before, sizeof(before));

    /* Reparse the same sheet. Same rules, same count, same error. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_parse(sheet, &second, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 3u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_tree_rule_count(sheet), 4u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_style_rule_count(sheet), 3u);
    ROBO_CHECK_EQ_INT(second.kind, first.kind);

    css_shape(robo_css_at_rule_prelude_values(
                  robo_css_rule_at(robo_css_stylesheet_rule(sheet, 1u))),
              after, sizeof(after));
    ROBO_CHECK_STR_EQ(after, before);

    /* And the tree is a tree, not a log: the values are rebuilt, not appended
     * to, so the third rule's value still has exactly two dimensions. */
    for (index = 0u; index < robo_css_stylesheet_rule_count(sheet); index++) {
        ROBO_CHECK(robo_css_stylesheet_rule(sheet, index) != NULL);
    }
    ROBO_CHECK_EQ_INT(
        robo_css_declaration_value(robo_css_style_rule_declaration(
            css_sheet_style_rule(sheet, 2u), 0u))->children.count,
        3u);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/**
 * Every range a parse reports is a byte range *inside* the source.
 *
 * A range that overran its source would be a slice pointing at memory the
 * caller never handed over, which is a read out of bounds waiting for the first
 * caller that trusts the range instead of the text. Checking each one against
 * the source is cheap and catches it at the only place it can be caught.
 */
static void test_css_parse_integration_ranges_stay_inside_the_source(void)
{
    static const char *text =
        "/* leading */\n"
        "@import \"theme.css\";\n"
        "p, .a > b { color: #06c; font: 12px/1.5 \"Roboto\", sans-serif; }\n"
        "input[type=\"text\"] { width: calc(100% - 20px); }\n"
        "@media screen and (min-width: 30em) { a { margin: 0; } }\n"
        "q { --x: [1 2]; }\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    const robo_css_source_t *source = robo_css_stylesheet_source(sheet);
    size_t length = strlen(text);
    size_t index;

    ROBO_CHECK(source != NULL);
    ROBO_CHECK_EQ_INT(source->length, length);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    /* Five top-level rules: `@import`, `p, .a > b`, `input[type="text"]`,
     * `@media` and `q`. The leading comment is not one. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 5u);

    for (index = 0u; index < robo_css_stylesheet_rule_count(sheet); index++) {
        const robo_css_rule_t *rule = robo_css_stylesheet_rule(sheet, index);
        const robo_css_style_rule_t *style = robo_css_rule_style(rule);
        const robo_css_at_rule_t *at = robo_css_rule_at(rule);
        size_t declaration_index;

        ROBO_CHECK(rule != NULL);
        if (style) {
            ROBO_CHECK(robo_css_source_range_is_valid(
                source, robo_css_style_rule_range(style)));
            ROBO_CHECK(robo_css_source_range_is_valid(
                source, robo_css_style_rule_prelude_range(style)));
            ROBO_CHECK(robo_css_source_range_is_valid(
                source, robo_css_style_rule_block_range(style)));
            for (declaration_index = 0u;
                 declaration_index < robo_css_style_rule_declaration_count(style);
                 declaration_index++) {
                const robo_css_declaration_t *declaration =
                    robo_css_style_rule_declaration(style, declaration_index);
                const robo_css_value_t *value = robo_css_declaration_value(declaration);

                ROBO_CHECK_EQ_INT(declaration->state,
                                  ROBO_CSS_DECLARATION_STATE_PARSED);
                ROBO_CHECK(robo_css_source_range_is_valid(source, declaration->range));
                ROBO_CHECK(robo_css_source_range_is_valid(source,
                                                          declaration->name_range));
                ROBO_CHECK(robo_css_source_range_is_valid(source,
                                                          declaration->value_range));
                ROBO_CHECK(robo_css_source_range_is_valid(source,
                                                          robo_css_value_range(value)));
                /* The declaration's own extent begins at its name and its
                 * value's extent ends it, so the two together are the whole
                 * declaration and neither is empty. */
                ROBO_CHECK_EQ_INT(declaration->range.begin,
                                  declaration->name_range.begin);
                ROBO_CHECK_EQ_INT(declaration->range.end, declaration->value_range.end);
                ROBO_CHECK(declaration->name_range.end > declaration->name_range.begin);
                ROBO_CHECK(declaration->value_range.end > declaration->value_range.begin);
            }
            continue;
        }
        ROBO_CHECK(at != NULL);
        ROBO_CHECK(robo_css_source_range_is_valid(source, robo_css_at_rule_range(at)));
        ROBO_CHECK(robo_css_source_range_is_valid(source,
                                                  robo_css_at_rule_prelude_range(at)));
        for (declaration_index = 0u;
             declaration_index < robo_css_at_rule_declaration_count(at);
             declaration_index++) {
            ROBO_CHECK(robo_css_source_range_is_valid(
                source,
                robo_css_at_rule_declaration(at, declaration_index)->range));
        }
    }

    /* The last rule's `--x: [1 2]` is a custom property, so its value is kept
     * as written — brackets and all — and its range still ends inside the
     * source even though a `]` was involved. */
    {
        const robo_css_style_rule_t *last =
            css_sheet_style_rule(sheet, robo_css_stylesheet_rule_count(sheet) - 1u);
        const robo_css_declaration_t *declaration =
            robo_css_style_rule_declaration(last, 0u);
        char shape[64];

        ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_declaration_name(declaration),
                                               "--x"));
        css_shape(robo_css_declaration_value(declaration), shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, "[1,_,2]");
    }

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_css_parse_recover(void);

void robo_test_register_css_parse_recover(void)
{
    robo_test_register("css.parse.malformed_rule_shapes",
                       test_css_parse_malformed_rule_shapes);
    robo_test_register("css.parse.recover.stray_closing_brace",
                       test_css_parse_recover_stray_closing_brace);
    robo_test_register("css.parse.recover.broken_declaration_keeps_the_rule",
                       test_css_parse_recover_broken_declaration_keeps_the_rule);
    robo_test_register("css.parse.recover.dropped_rule_costs_one_rule",
                       test_css_parse_recover_dropped_rule_costs_one_rule);
    robo_test_register("css.parse.recover.many_problems_keep_all_valid_rules",
                       test_css_parse_recover_many_problems_keep_all_valid_rules);
    robo_test_register("css.parse.recover.nested_rule_is_skipped_whole",
                       test_css_parse_recover_nested_rule_is_skipped_whole);
    robo_test_register("css.parse.recover.at_rule_inside_a_block",
                       test_css_parse_recover_at_rule_inside_a_block);
    robo_test_register("css.parse.recover.rule_nesting_bound",
                       test_css_parse_recover_rule_nesting_bound);
    robo_test_register("css.parse.integration.values_are_the_tokenizers_tokens",
                       test_css_parse_integration_values_are_the_tokenizers_tokens);
    robo_test_register("css.parse.integration.reparse_is_identical",
                       test_css_parse_integration_reparse_is_identical_and_does_not_accumulate);
    robo_test_register("css.parse.integration.ranges_stay_inside_the_source",
                       test_css_parse_integration_ranges_stay_inside_the_source);
}
