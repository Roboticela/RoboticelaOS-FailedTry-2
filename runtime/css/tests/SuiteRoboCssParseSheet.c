/*
 * RoboCSS parser: stylesheets, rules and at-rules (Phase 9.3).
 *
 * What this suite pins is the *structure* level of the grammar: how many rules
 * a sheet has, in what order, what each one's prelude is, where each one's
 * bytes are, and what an at-rule is when the parser has never heard of it.
 *
 * Two properties run through nearly every test here and are worth stating once:
 *
 *   - **Order is source order, and nothing is reordered.** A stylesheet's rules
 *     are the cascade's input (9.5), so a parser that "helpfully" sorted or
 *     deduplicated would be implementing the cascade by accident.
 *   - **An at-rule is syntax and nothing else.** `@media` and `@nonsense` go
 *     through exactly the same code, and the assertions say so: the name is
 *     compared without a table, and the block is a rule list because that is the
 *     only structure a block can have before something knows what it means.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Stylesheets                                                         */
/* ------------------------------------------------------------------ */

static void test_css_parse_empty_sheets(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;

    /* Empty and whitespace-only are both a legal stylesheet with no rules — and
     * both are *clean*: nothing was wrong, there was simply nothing. */
    sheet = css_parse_sheet("", &error);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 0u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_state(sheet),
                      ROBO_CSS_STYLESHEET_STATE_PARSED);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);

    sheet = css_parse_sheet("  \n\t\r\n  ", &error);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 0u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);

    /* A stylesheet that is only a comment is also empty of rules. Comments are
     * not component values, so the comment does not become one. */
    sheet = css_parse_sheet("/* nothing here */\n", &error);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 0u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);

    /* A stylesheet with no attached source parses to no rules rather than
     * failing: a parse reads bytes, and there are none. */
    {
        robo_css_stylesheet_id_t bare = ROBO_CSS_STYLESHEET_ID_NONE;

        css_fixture();
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&bare, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_parse(bare, &error, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(bare), 0u);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(bare, NULL), ROBO_OK);
    }
}

static void test_css_parse_one_rule(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet("p { color: red; }\n", &error);
    const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, 0u);
    char shape[256];

    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_tree_rule_count(sheet), 1u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_style_rule_count(sheet), 1u);
    ROBO_CHECK(rule != NULL);

    css_prelude_shape(rule, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "p");
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(rule), 1u);
    ROBO_CHECK_EQ_INT(rule->block_closed, 1);

    /* The rule's extent is its prelude through its closing brace, and the block
     * extent starts at the `{` — `p { color: red; }` is 17 bytes, 0..16, with
     * the trailing LF at 17 outside it. Neither end is a byte the rule does not
     * contain: `end` is one past the `}`. */
    ROBO_CHECK_EQ_INT(robo_css_style_rule_range(rule).begin, 0u);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_prelude_range(rule).begin, 0u);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_prelude_range(rule).end, 1u);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_block_range(rule).begin, 2u);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_block_range(rule).end, 17u);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_range(rule).end, 17u);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_parse_many_rules_in_order(void)
{
    static const char *text =
        "h1 { color: red; }\n"
        "p { margin: 0; }\n"
        "a { text-decoration: none; }\n"
        ".button { padding: 10px; }\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    /* `.button` is two component values — a DELIM and an IDENT — and the shape
     * convention joins children with `,`, so the class selector reads as
     * `.,button`. A shape that said `.button` would be a shape that had folded
     * the delimiter into the ident, which is selector *parsing*. */
    static const char *expected[] = { "h1", "p", "a", ".,button" };
    size_t index;

    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 4u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_style_rule_count(sheet), 4u);

    /* Source order, every time. A sheet that reordered its rules would hand the
     * cascade a different input, and a selector list's order is author-visible. */
    for (index = 0u; index < 4u; index++) {
        const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, index);
        char shape[256];

        ROBO_CHECK(rule != NULL);
        css_prelude_shape(rule, shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, expected[index]);
        ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(rule), 1u);
    }
    ROBO_CHECK(css_sheet_style_rule(sheet, 4u) == NULL);

    /* Ranges ascend and do not overlap: rule N's start is past rule N-1's end. */
    for (index = 1u; index < 4u; index++) {
        const robo_css_style_rule_t *previous = css_sheet_style_rule(sheet, index - 1u);
        const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, index);

        ROBO_CHECK(robo_css_style_rule_range(rule).begin >
                   robo_css_style_rule_range(previous).end);
    }

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_parse_comments_and_cdo_cdc(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    char shape[256];

    /* A comment between rules is trivia, like whitespace: it does not become a
     * rule, and it does not become part of the rule that follows it. */
    sheet = css_parse_sheet("/* one */ p { color: red; } /* two */\n", &error);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    css_prelude_shape(css_sheet_style_rule(sheet, 0u), shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "p");
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);

    /* CDO and CDC at the top of a stylesheet are the HTML comment tokens CSS
     * tolerates, and they are ignored rather than becoming a rule's prelude. */
    sheet = css_parse_sheet("<!--\np { color: red; }\n-->\n", &error);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    css_prelude_shape(css_sheet_style_rule(sheet, 0u), shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "p");
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Style rule preludes                                                 */
/* ------------------------------------------------------------------ */

static void test_css_parse_prelude_is_component_values(void)
{
    static const char *text =
        ".button:hover > span[data-role=\"x\"] , #id + a::before { color: red; }\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, 0u);
    char shape[512];

    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    ROBO_CHECK(rule != NULL);

    /* Every byte of the prelude is accounted for as component values: no
     * selector has been interpreted, no pseudo-class recognised, and no
     * combinator folded away. This *is* what 9.4 will read. */
    css_prelude_shape(rule, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape,
                      ".,button,:,hover,_,>,_,span,[data-role,=,\"x\"],_,,,_,#id,_,+,_,a,:,:,before");

    /* And nothing was promoted into selectors: that is 9.4's job. */
    ROBO_CHECK_EQ_INT(robo_css_style_rule_selector_count(rule), 0u);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_parse_descendant_combinator_needs_whitespace(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    char descendant[256];
    char compound[256];

    /* The reason whitespace is a component value. `a b` and `ab` differ by one
     * node here, and by a completely different meaning to 9.4 — a descendant
     * combinator versus a single type selector. A prelude that dropped
     * whitespace could not tell an author's `a b` from their `ab`. */
    sheet = css_parse_sheet("a b { color: red; }\n", &error);
    css_prelude_shape(css_sheet_style_rule(sheet, 0u), descendant, sizeof(descendant));
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);

    sheet = css_parse_sheet("a,b { color: red; }\n", &error);
    css_prelude_shape(css_sheet_style_rule(sheet, 0u), compound, sizeof(compound));
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);

    ROBO_CHECK_STR_EQ(descendant, "a,_,b");
    ROBO_CHECK_STR_EQ(compound, "a,,,b");
    ROBO_CHECK(strcmp(descendant, compound) != 0);
}

static void test_css_parse_empty_block_is_a_rule(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(".empty {\n}\n", &error);
    const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, 0u);
    char shape[256];

    /* A rule with a prelude and no declarations is legal CSS and stays a rule:
     * "has selectors and no declarations" is a "worth keeping" test, not a
     * validity test. */
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    ROBO_CHECK(rule != NULL);
    css_prelude_shape(rule, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, ".,empty");
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(rule), 0u);
    ROBO_CHECK_EQ_INT(rule->block_closed, 1);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_is_populated(rule), 0);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* At-rules                                                            */
/* ------------------------------------------------------------------ */

static void test_css_parse_statement_at_rule(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet("@import \"theme.css\";\n", &error);
    const robo_css_rule_t *rule = robo_css_stylesheet_rule(sheet, 0u);
    const robo_css_at_rule_t *at = robo_css_rule_at(rule);

    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    ROBO_CHECK_EQ_INT(rule->kind, ROBO_CSS_RULE_KIND_AT);
    ROBO_CHECK(at != NULL);

    /* The name is what followed the `@`, without it. */
    ROBO_CHECK(robo_css_slice_equals_ascii(at->name, "import"));
    /* No block, so nothing was loaded and no children exist. The prelude is the
     * string token, and it stayed a token. */
    ROBO_CHECK_EQ_INT(at->has_block, 0);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(at), 0u);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_block_closed(at), 0);
    ROBO_CHECK(robo_css_slice_equals_ascii(at->prelude, "\"theme.css\""));
    {
        char shape[256];

        css_shape(robo_css_at_rule_prelude_values(at), shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, "\"theme.css\"");
    }
    /* Whole-rule extent includes the `;`. */
    ROBO_CHECK_EQ_INT(robo_css_at_rule_range(at).begin, 0u);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_range(at).end, strlen("@import \"theme.css\";"));

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_parse_block_at_rule_holds_rules(void)
{
    static const char *text =
        "@media screen {\n"
        "    p { color: red; }\n"
        "    .x { margin: 0; }\n"
        "}\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    const robo_css_at_rule_t *at = robo_css_rule_at(robo_css_stylesheet_rule(sheet, 0u));
    char shape[256];

    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    ROBO_CHECK(at != NULL);
    ROBO_CHECK(robo_css_slice_equals_ascii(at->name, "media"));
    ROBO_CHECK_EQ_INT(at->has_block, 1);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_block_closed(at), 1);

    /* The prelude is `screen`, trimmed — the newline after `@media` is not part
     * of it — and the block's contents are rules, not component values. */
    css_shape(robo_css_at_rule_prelude_values(at), shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "screen");

    ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(at), 2u);
    css_prelude_shape(robo_css_rule_style(robo_css_at_rule_rule_at(at, 0u)), shape,
                      sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "p");
    css_prelude_shape(robo_css_rule_style(robo_css_at_rule_rule_at(at, 1u)), shape,
                      sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, ".,x");

    /* The sheet's counts see through the block; its top-level count does not. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_tree_rule_count(sheet), 3u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_style_rule_count(sheet), 2u);

    /* The whole-rule extent covers the block, so it ends at the final `}`. */
    ROBO_CHECK_EQ_INT(robo_css_at_rule_range(at).end, strlen(text) - 1u);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_parse_at_rules_are_not_interpreted(void)
{
    static const char *text =
        "@supports (display: grid) { p { color: red; } }\n"
        "@font-face { font-family: X; src: url(x.woff); }\n"
        "@keyframes spin { from { opacity: 0; } }\n"
        "@totally-made-up foo { p { color: red; } }\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    static const char *names[] = { "supports", "font-face", "keyframes",
                                   "totally-made-up" };
    size_t index;

    /* There is no at-rule table and there is no at-rule semantics: the four
     * names go through the same code, and the one this build has never heard of
     * is not treated differently from the three it has. `@font-face` loaded
     * nothing; `@keyframes` evaluated nothing; the made-up name was not refused.
     *
     * What the code *does* do without a table is notice what a block contains,
     * because that is a property of the bytes and not of a name: three of these
     * blocks hold rules, and `@font-face`'s holds declarations. */
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 4u);
    for (index = 0u; index < 4u; index++) {
        const robo_css_at_rule_t *at =
            robo_css_rule_at(robo_css_stylesheet_rule(sheet, index));
        const int is_descriptors = index == 1u;

        ROBO_CHECK(at != NULL);
        ROBO_CHECK(robo_css_slice_equals_ascii(at->name, names[index]));
        ROBO_CHECK_EQ_INT(at->has_block, 1);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_block_closed(at), 1);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(at), is_descriptors ? 0u : 1u);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_declaration_count(at),
                          is_descriptors ? 2u : 0u);
    }

    /* `@font-face`'s two descriptors are its block's declarations, in source
     * order, with their values preserved as text and as component values —
     * nothing was resolved, and `X` was not looked up as a font. */
    {
        const robo_css_at_rule_t *font_face =
            robo_css_rule_at(robo_css_stylesheet_rule(sheet, 1u));
        char shape[256];

        ROBO_CHECK(robo_css_slice_equals_ascii(
            robo_css_declaration_name(robo_css_at_rule_declaration(font_face, 0u)),
            "font-family"));
        ROBO_CHECK(robo_css_slice_equals_ascii(
            robo_css_declaration_name(robo_css_at_rule_declaration(font_face, 1u)),
            "src"));
        css_shape(robo_css_declaration_value(robo_css_at_rule_declaration(font_face, 0u)),
                  shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, "X");
        css_shape(robo_css_declaration_value(robo_css_at_rule_declaration(font_face, 1u)),
                  shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, "url(x.woff)");
        ROBO_CHECK(robo_css_at_rule_declaration(font_face, 2u) == NULL);
    }

    /* Three group blocks, each with one rule, plus the four at-rules. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_tree_rule_count(sheet), 7u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_style_rule_count(sheet), 3u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_parse_nested_at_rule_blocks(void)
{
    static const char *text =
        "@media screen {\n"
        "    @supports (display: grid) {\n"
        "        p { color: red; }\n"
        "    }\n"
        "}\n"
        "a { color: blue; }\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    const robo_css_at_rule_t *outer = robo_css_rule_at(robo_css_stylesheet_rule(sheet, 0u));
    const robo_css_at_rule_t *inner;

    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK(outer != NULL);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(outer), 1u);

    inner = robo_css_rule_at(robo_css_at_rule_rule_at(outer, 0u));
    ROBO_CHECK(inner != NULL);
    ROBO_CHECK(robo_css_slice_equals_ascii(inner->name, "supports"));
    ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(inner), 1u);
    ROBO_CHECK(robo_css_slice_equals_ascii(
        robo_css_declaration_name(robo_css_style_rule_declaration(
            robo_css_rule_style(robo_css_at_rule_rule_at(inner, 0u)), 0u)),
        "color"));

    /* Four rules in the tree: the two at-rules, the `p` inside them and the `a`
     * that followed the whole thing. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_tree_rule_count(sheet), 4u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 2u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_parse_at_rule_without_final_semicolon(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    const robo_css_at_rule_t *at;
    char shape[256];

    /* No `;` and no block, at the end of the input: a statement at-rule whose
     * terminator was omitted. Every browser accepts this, so it is not an
     * error — the prelude is simply where the input stopped. */
    sheet = css_parse_sheet("@import \"theme.css\"", &error);
    at = robo_css_rule_at(robo_css_stylesheet_rule(sheet, 0u));
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    ROBO_CHECK(robo_css_slice_equals_ascii(at->name, "import"));
    ROBO_CHECK_EQ_INT(at->has_block, 0);
    css_shape(robo_css_at_rule_prelude_values(at), shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "\"theme.css\"");
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);

    /* An at-rule with no prelude at all: `@foo;` is a name and a `;`. */
    sheet = css_parse_sheet("@foo;\n", &error);
    at = robo_css_rule_at(robo_css_stylesheet_rule(sheet, 0u));
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK(robo_css_slice_equals_ascii(at->name, "foo"));
    ROBO_CHECK_EQ_INT(at->has_block, 0);
    ROBO_CHECK_EQ_INT(robo_css_value_child_count(robo_css_at_rule_prelude_values(at)), 0u);
    ROBO_CHECK(robo_css_slice_is_empty(at->prelude));
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_parse_block_at_rule_unclosed(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet("@media screen {\n p { color: red; }\n",
                                                    &error);
    const robo_css_at_rule_t *at = robo_css_rule_at(robo_css_stylesheet_rule(sheet, 0u));

    /* A block that ran to the end of the input: the at-rule and its rules are
     * real, and the fact that the `}` was never written is recorded rather than
     * guessed at. A closed block and an unclosed one are different rules. */
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF);
    ROBO_CHECK(robo_css_error_kind_is_recoverable(error.kind));
    ROBO_CHECK(at != NULL);
    ROBO_CHECK_EQ_INT(at->has_block, 1);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_block_closed(at), 0);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(at), 1u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Ownership                                                           */
/* ------------------------------------------------------------------ */

static void test_css_parse_sheet_lifetime(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    size_t baseline;
    int we_enabled = css_allocator_ensure_enabled();

    baseline = robo_mem_debug_live_count();
    /* Parsing a sheet allocates a whole tree and must give every byte of it
     * back: the sheet owns its rules, an at-rule owns its children, a rule owns
     * its prelude values and its declarations, and a declaration owns its value.
     * Four levels of ownership, one measurement. */
    {
        static const char *text =
            "@media screen { p { color: red; } }\n"
            "a.b > c { margin: calc(1px + 2px) 3px !important; }\n";
        size_t round;

        for (round = 0u; round < 4u; round++) {
            sheet = css_parse_sheet(text, &error);
            ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
            ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 2u);
            ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
        }
    }

    /* A parse that replaces a sheet's rules releases the ones it replaced —
     * parsing twice is idempotent, not additive, and not a leak. */
    sheet = css_parse_sheet("p { color: red; }\n", &error);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_parse(sheet, &error, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_parse(sheet, &error, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);

    css_allocator_restore(we_enabled);
}

/* ------------------------------------------------------------------ */

void robo_test_register_css_parse_sheet(void);

void robo_test_register_css_parse_sheet(void)
{
    robo_test_register("css.parse.sheet.empty", test_css_parse_empty_sheets);
    robo_test_register("css.parse.sheet.one_rule", test_css_parse_one_rule);
    robo_test_register("css.parse.sheet.many_rules_in_order",
                       test_css_parse_many_rules_in_order);
    robo_test_register("css.parse.sheet.comments_and_cdo_cdc",
                       test_css_parse_comments_and_cdo_cdc);
    robo_test_register("css.parse.rule.prelude_is_component_values",
                       test_css_parse_prelude_is_component_values);
    robo_test_register("css.parse.rule.descendant_combinator_needs_whitespace",
                       test_css_parse_descendant_combinator_needs_whitespace);
    robo_test_register("css.parse.rule.empty_block_is_a_rule",
                       test_css_parse_empty_block_is_a_rule);
    robo_test_register("css.parse.at_rule.statement", test_css_parse_statement_at_rule);
    robo_test_register("css.parse.at_rule.block_holds_rules",
                       test_css_parse_block_at_rule_holds_rules);
    robo_test_register("css.parse.at_rule.not_interpreted",
                       test_css_parse_at_rules_are_not_interpreted);
    robo_test_register("css.parse.at_rule.nested_blocks",
                       test_css_parse_nested_at_rule_blocks);
    robo_test_register("css.parse.at_rule.optional_semicolon",
                       test_css_parse_at_rule_without_final_semicolon);
    robo_test_register("css.parse.at_rule.unclosed_block",
                       test_css_parse_block_at_rule_unclosed);
    robo_test_register("css.parse.sheet.lifetime", test_css_parse_sheet_lifetime);
}
