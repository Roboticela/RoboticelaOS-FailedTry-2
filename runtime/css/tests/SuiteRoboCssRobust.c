/*
 * RoboCSS Phase 9.1 robustness suite:
 * empty, whitespace-only, large, incomplete and malformed input; deep and wide
 * structures; repeated creation and destruction; and the Unicode matrix.
 *
 * Phase 9.1 has no parser, so "malformed CSS" cannot fail here the way it will
 * in 9.3. What it can do — and what these tests check — is be *representable*:
 * every one of these inputs is carried by the source model, described by its
 * positions, and released without leaving an allocation behind.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSStyle.h"

/** A large but reasonable stylesheet, assembled once. */
static char css_big_source[32768];
static size_t css_big_length;

static void css_build_big_source(void)
{
    static const char rule[] = "p { color: red; margin: 10px; }\n";
    size_t rule_length = sizeof(rule) - 1u;

    if (css_big_length > 0u) {
        return;
    }
    while (css_big_length + rule_length + 1u < sizeof(css_big_source)) {
        memcpy(css_big_source + css_big_length, rule, rule_length);
        css_big_length += rule_length;
    }
    css_big_source[css_big_length] = '\0';
}

static void test_css_robust_empty_and_whitespace(void)
{
    static const char *inputs[] = {"", " ", "\n", "\r\n", "\t\f\r ", "/* nothing */"};
    size_t i;

    css_fixture();
    for (i = 0u; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        robo_css_source_t source = ROBO_CSS_CLEARED;
        robo_css_parser_t parser = ROBO_CSS_CLEARED;
        robo_css_token_t token = ROBO_CSS_CLEARED;

        css_source_of_string(&source, inputs[i]);
        ROBO_CHECK_EQ_INT(robo_css_source_length(&source), strlen(inputs[i]));
        ROBO_CHECK(robo_css_source_is_valid_utf8(&source));
        /* Every one of these is a legal place to put a cursor, whether or not
         * there is anything to read there. */
        ROBO_CHECK_EQ_INT(robo_css_parser_init(&parser, &source, ROBO_CSS_ORIGIN_AUTHOR,
                                               NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_parser_offset(&parser), 0u);
        ROBO_CHECK_EQ_INT(robo_css_parser_remaining(&parser), strlen(inputs[i]));
        ROBO_CHECK_EQ_INT(robo_css_token_make_eof(&token, &source, strlen(inputs[i]),
                                                  NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_EOF);
        robo_css_parser_dispose(&parser);
        robo_css_source_dispose(&source);
    }

    /* An empty source is not the same as no source at all, and the model keeps
     * the difference visible. */
    {
        robo_css_source_t empty = ROBO_CSS_CLEARED;
        robo_css_source_t unset = ROBO_CSS_CLEARED;

        robo_css_source_init(&unset);
        robo_css_source_init(&empty);
        ROBO_CHECK_EQ_INT(robo_css_source_set_empty(&empty, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_source_is_configured(&empty), 1);
        ROBO_CHECK_EQ_INT(robo_css_source_is_configured(&unset), 0);
        ROBO_CHECK(robo_css_source_is_empty(&empty));
        ROBO_CHECK(robo_css_source_is_empty(&unset));
    }
}

static void test_css_robust_large_source(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_position_t position;
    size_t expected_breaks = 0u;
    size_t i;

    css_fixture();
    css_build_big_source();
    ROBO_CHECK(css_big_length > 16000u);
    ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, (const uint8_t *)css_big_source,
                                                 css_big_length, NULL),
                      ROBO_OK);

    for (i = 0u; i < css_big_length; i++) {
        if (css_big_source[i] == '\n') {
            expected_breaks++;
        }
    }
    ROBO_CHECK_EQ_INT(robo_css_source_line_count(&source), expected_breaks + 1u);
    ROBO_CHECK_EQ_INT(robo_css_source_scalar_count(&source), css_big_length);
    ROBO_CHECK(robo_css_source_is_valid_utf8(&source));

    /* A position at the very end is still a position, and resolving several
     * positions over a 30 KB source stays linear and total. */
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, css_big_length, &position),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, (uint32_t)(expected_breaks + 1u));
    ROBO_CHECK_EQ_INT(position.column, 1u);
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 0u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 1u);

    /* Copying the whole thing owns an equal number of bytes and releases them. */
    {
        int we_enabled = css_allocator_ensure_enabled();
        size_t baseline = robo_mem_debug_live_count();
        robo_css_source_t copy = ROBO_CSS_CLEARED;

        robo_css_source_init(&copy);
        ROBO_CHECK_EQ_INT(robo_css_source_copy(&copy, (const uint8_t *)css_big_source,
                                               css_big_length, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_css_source_is_owned(&copy));
        ROBO_CHECK_EQ_INT(robo_css_source_length(&copy), css_big_length);
        ROBO_CHECK_EQ_INT(robo_css_source_bytes(&copy)[css_big_length - 1u],
                          (uint8_t)'\n');
        robo_css_source_dispose(&copy);
        ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
        css_allocator_restore(we_enabled);
    }

    robo_css_source_dispose(&source);
}

static void test_css_robust_incomplete_and_malformed(void)
{
    static const char *inputs[] = {
        "p {",
        "p { color:",
        "p { color: red",
        "p { color: red;",
        "p { color: red; } @media",
        "@media print {",
        "div > > p {",
        "a { ] }",
        "[unclosed",
        "/* unterminated comment",
        "\"unterminated string",
        "} } }",
        ";;;",
        "::",
        "p { color: red !important",
        "\xEF\xBB\xBFp { color: red }",
        "p { content: \"\x01\x02\"; }"
    };
    size_t i;

    css_fixture();
    for (i = 0u; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        robo_css_source_t source = ROBO_CSS_CLEARED;
        robo_css_slice_t whole;
        size_t j;
        int same = 1;

        /* Malformed input is representable: the bytes are kept exactly, the
         * length is exact, and every offset is a position. Nothing here claims
         * to have understood the CSS, which is the point. */
        css_source_of_string(&source, inputs[i]);
        ROBO_CHECK_EQ_INT(robo_css_source_length(&source), strlen(inputs[i]));
        whole = robo_css_source_slice(&source);
        ROBO_CHECK_EQ_INT(whole.length, strlen(inputs[i]));
        for (j = 0u; j < strlen(inputs[i]); j++) {
            if (whole.bytes[j] != (uint8_t)inputs[i][j]) {
                same = 0;
            }
        }
        ROBO_CHECK(same);
        for (j = 0u; j <= strlen(inputs[i]); j++) {
            robo_css_position_t position;

            ROBO_CHECK_EQ_INT(robo_css_source_position(&source, j, &position), ROBO_OK);
            ROBO_CHECK_EQ_INT(position.offset, j);
            ROBO_CHECK(position.line >= 1u);
            ROBO_CHECK(position.column >= 1u);
        }
        robo_css_source_dispose(&source);
    }

    /* Malformed markup through the declaration parser: no crash, no leak, and a
     * defined answer. A `}` ends a declaration list, so this text is a
     * declaration list that ends immediately — the same thing a browser does
     * with a stray brace inside a `style` attribute, because there is one
     * declaration-list algorithm and this is it. */
    {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text("}}}::;;", 7u,
                                                               ROBO_CSS_ORIGIN_INLINE,
                                                               &list, &error),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 0u);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        robo_css_declaration_list_dispose(&list);
    }

    /* The recovery boundaries, exercised through the parser rather than
     * described: a run of declarations with three different problems in it
     * still yields every declaration that was fine, in source order, and the
     * *first* problem is the one reported. */
    {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;
        const char *text =
            "a: 1; b c; : 2; d: 4; e: calc(1px + 2px); f: 6";

        ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(
                              text, strlen(text), ROBO_CSS_ORIGIN_INLINE, &list, &error),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 6u);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 1u)->state,
                          ROBO_CSS_DECLARATION_STATE_INVALID);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 2u)->state,
                          ROBO_CSS_DECLARATION_STATE_INVALID);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 5u)->state,
                          ROBO_CSS_DECLARATION_STATE_PARSED);
        robo_css_declaration_list_dispose(&list);
    }
}

static void test_css_robust_unicode_through_models(void)
{
    size_t i;

    css_fixture();
    for (i = 0u; i < CSS_SAMPLE_COUNT; i++) {
        static const char prefix[] = "/* ";
        static const char suffix[] = " */\np { color: red; }\n";
        char rule[512];
        size_t rule_length = 0u;
        robo_css_source_t source = ROBO_CSS_CLEARED;
        robo_css_stylesheet_id_t sheet = ROBO_CSS_STYLESHEET_ID_NONE;
        robo_css_token_t token = ROBO_CSS_CLEARED;
        robo_css_declaration_t declaration = ROBO_CSS_CLEARED;
        robo_css_selector_list_t selectors = ROBO_CSS_CLEARED;
        robo_css_value_t value = ROBO_CSS_CLEARED;

        memcpy(rule, prefix, sizeof(prefix) - 1u);
        rule_length += sizeof(prefix) - 1u;
        memcpy(rule + rule_length, css_samples[i].text, strlen(css_samples[i].text));
        rule_length += strlen(css_samples[i].text);
        memcpy(rule + rule_length, suffix, sizeof(suffix) - 1u);
        rule_length += sizeof(suffix) - 1u;
        rule[rule_length] = '\0';

        css_source_of_string(&source, rule);

        /* A token over the non-ASCII span keeps those exact bytes. */
        ROBO_CHECK_EQ_INT(robo_css_token_make(&token, ROBO_CSS_TOKEN_IDENT, &source,
                                              robo_css_range_span(sizeof(prefix) - 1u,
                                                                  strlen(css_samples[i].text)),
                                              NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_token_text(&token).length,
                          strlen(css_samples[i].text));
        ROBO_CHECK(memcmp(robo_css_token_text(&token).bytes, css_samples[i].text,
                          strlen(css_samples[i].text)) == 0);

        /* A declaration named in that script, and a selector class too. */
        robo_css_declaration_make(&declaration, robo_css_slice_from_text(css_samples[i].text),
                                  ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
        ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_declaration_name(&declaration),
                                               css_samples[i].text));
        ROBO_CHECK_EQ_INT(robo_css_value_make_list(&value, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_declaration_set_value(&declaration, &value, NULL),
                          ROBO_OK);
        robo_css_selector_list_init(&selectors);
        {
            robo_css_complex_selector_t complex = ROBO_CSS_CLEARED;

            css_complex_selector_of(&complex, &source, "p", css_samples[i].text);
            ROBO_CHECK_EQ_INT(robo_css_selector_list_append(&selectors, &complex, NULL),
                              ROBO_OK);
        }
        ROBO_CHECK_EQ_INT(
            robo_css_compound_selector_count(
                robo_css_complex_selector_compound_at(
                    robo_css_selector_list_at(&selectors, 0u), 0u)),
            2u);
        ROBO_CHECK(robo_css_slice_equals_ascii(
                       robo_css_compound_selector_at(
                           robo_css_complex_selector_compound_at(
                               robo_css_selector_list_at(&selectors, 0u), 0u),
                           1u)
                           ->name,
                       css_samples[i].text));
        robo_css_selector_list_dispose(&selectors);

        /* And a stylesheet that owns the whole thing, so the bytes outlive the
         * buffer the caller stops tracking. */
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&sheet, ROBO_CSS_ORIGIN_AUTHOR,
                                                     NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_copy_source(sheet,
                                                          (const uint8_t *)rule,
                                                          rule_length, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_source_length(sheet), rule_length);
        {
            robo_css_rule_t style_rule = ROBO_CSS_CLEARED;

            ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&style_rule, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_style_rule_append_declaration(&style_rule.style,
                                                                     &declaration, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_stylesheet_append_rule(sheet, &style_rule, NULL),
                              ROBO_OK);
        }
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);

        robo_css_source_dispose(&source);
    }
}

static void test_css_robust_repeated_churn(void)
{
    int we_enabled;
    size_t baseline;
    int i;

    css_fixture();
    we_enabled = css_allocator_ensure_enabled();
    baseline = robo_mem_debug_live_count();

    /* Repeated stylesheet lifetimes must return to the same live count, which
     * is what "reusable slots" has to mean in practice. */
    for (i = 0; i < (int)ROBO_CSS_MAX_STYLESHEETS * 3; i++) {
        robo_css_stylesheet_id_t id = ROBO_CSS_STYLESHEET_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&id, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                          ROBO_OK);
        robo_css_stylesheet_set_name(id, "churn", NULL);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_copy_source(
                              id, (const uint8_t *)CSS_FIXTURE_ATTRIBUTE,
                              strlen(CSS_FIXTURE_ATTRIBUTE), NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(id, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    }
    ROBO_CHECK_EQ_INT(robo_css_live_stylesheet_count(), 0u);
    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);

    /* The same for style contexts and property tables, which own their own
     * storage rather than living in module slots. */
    for (i = 0; i < 64; i++) {
        robo_css_style_context_t *context = NULL;
        robo_css_property_table_t *table = NULL;
        robo_css_property_id_t id = ROBO_CSS_PROPERTY_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_css_property_table_create(&table, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "color", 0u, &id,
                                                           NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_style_context_create(&context, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_style_context_set_property_table(context, table,
                                                                     NULL),
                          ROBO_OK);
        robo_css_style_context_destroy(context);
        robo_css_property_table_destroy(table);
        ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    }

    css_allocator_restore(we_enabled);
}

static void test_css_robust_deep_rule_nesting(void)
{
    robo_css_rule_t root = ROBO_CSS_CLEARED;
    robo_css_rule_t wrapper = ROBO_CSS_CLEARED;
    const robo_css_rule_t *flattened[128];
    int depth = 64;
    int i;

    css_fixture();
    /* Built inside-out, because a rule is moved into its parent: appending the
     * next level into a rule that has already been moved away would append
     * into a rule that no longer exists. Two locals and a value assignment keep
     * exactly one owner at every step. */
    ROBO_CHECK_EQ_INT(robo_css_rule_make_at(&root, robo_css_slice_from_text("media"),
                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_set_prelude(&root.at, robo_css_slice_empty(), 1,
                                                   NULL),
                      ROBO_OK);
    for (i = 1; i < depth; i++) {
        ROBO_CHECK_EQ_INT(robo_css_rule_make_at(&wrapper,
                                                robo_css_slice_from_text("supports"),
                                                NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_set_prelude(&wrapper.at,
                                                       robo_css_slice_empty(), 1, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(&wrapper.at, &root, NULL),
                          ROBO_OK);
        root = wrapper;
        robo_css_rule_init(&wrapper);
    }
    {
        robo_css_rule_t leaf = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&leaf, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(&root.at, &leaf, NULL), ROBO_OK);
    }

    /* 64 at-rules plus the style rule at the bottom, counted through the tree
     * and again through a depth-first flatten: two measurements of the same
     * tree that must agree. */
    ROBO_CHECK_EQ_INT(robo_css_rule_tree_count(&root), (size_t)depth + 1u);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_flatten(root.at.children, flattened, 128u),
                      (size_t)depth);
    ROBO_CHECK_EQ_INT(flattened[0]->kind, ROBO_CSS_RULE_KIND_AT);
    ROBO_CHECK_EQ_INT(flattened[depth - 1]->kind, ROBO_CSS_RULE_KIND_STYLE);

    robo_css_rule_dispose(&root);
    ROBO_CHECK_EQ_INT(robo_css_rule_tree_count(&root), 0u);
}

static void test_css_robust_wide_structures(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    int we_enabled;
    size_t baseline;

    css_fixture();
    css_source_of_string(&source, "4px");
    we_enabled = css_allocator_ensure_enabled();
    baseline = robo_mem_debug_live_count();

    /* 512 top-level rules with 4 declarations each: wide, shallow, and released
     * by one dispose. */
    {
        robo_css_rule_list_t rules = ROBO_CSS_CLEARED;
        int i;

        robo_css_rule_list_init(&rules);
        for (i = 0; i < 512; i++) {
            robo_css_rule_t rule = ROBO_CSS_CLEARED;
            int j;

            ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&rule, NULL), ROBO_OK);
            for (j = 0; j < 4; j++) {
                robo_css_declaration_t declaration = ROBO_CSS_CLEARED;
                robo_css_value_t value = ROBO_CSS_CLEARED;

                robo_css_declaration_make(&declaration,
                                          robo_css_slice_from_text("color"),
                                          ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
                css_value_token_at(&value, &source, "4px", NULL);
                ROBO_CHECK_EQ_INT(robo_css_declaration_set_value(&declaration, &value,
                                                                 NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_css_style_rule_append_declaration(&rule.style,
                                                                         &declaration,
                                                                         NULL),
                                  ROBO_OK);
            }
            ROBO_CHECK_EQ_INT(robo_css_rule_list_append(&rules, &rule, NULL), ROBO_OK);
        }
        ROBO_CHECK_EQ_INT(robo_css_rule_list_count(&rules), 512u);
        ROBO_CHECK_EQ_INT(robo_css_rule_list_style_rule_count(&rules), 512u);
        ROBO_CHECK_EQ_INT(robo_css_rule_list_tree_count(&rules), 512u);
        ROBO_CHECK_EQ_INT(robo_css_rule_list_flatten(&rules, NULL, 0u), 512u);
        robo_css_rule_list_dispose(&rules);
        ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    }

    /* 256 alternatives in one selector list: the width a real stylesheet has. */
    {
        robo_css_selector_list_t selectors = ROBO_CSS_CLEARED;
        int i;

        robo_css_selector_list_init(&selectors);
        for (i = 0; i < 256; i++) {
            robo_css_complex_selector_t complex = ROBO_CSS_CLEARED;

            css_complex_selector_of(&complex, &source, "p", "x");
            ROBO_CHECK_EQ_INT(robo_css_selector_list_append(&selectors, &complex, NULL),
                              ROBO_OK);
        }
        ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&selectors), 256u);
        ROBO_CHECK(robo_css_selector_list_is_well_formed(&selectors));
        robo_css_selector_list_dispose(&selectors);
        ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    }

    css_allocator_restore(we_enabled);
}

static void test_css_robust_deep_value_trees(void)
{
    robo_css_value_t root = ROBO_CSS_CLEARED;
    robo_css_value_t wrapper = ROBO_CSS_CLEARED;
    int depth = 64;
    int i;

    css_fixture();
    /* Inside-out again, for the same reason rules are: an appended value is
     * moved, so the container that is appended into must still be the owner. */
    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&root, NULL), ROBO_OK);
    for (i = 1; i < depth; i++) {
        ROBO_CHECK_EQ_INT(robo_css_value_make_function(&wrapper,
                                                       robo_css_slice_from_text("f"),
                                                       robo_css_range_at(0u), NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_value_append(&wrapper, &root, NULL), ROBO_OK);
        root = wrapper;
        robo_css_value_init(&wrapper);
    }
    ROBO_CHECK_EQ_INT(robo_css_value_depth(&root), (size_t)depth);
    ROBO_CHECK_EQ_INT(robo_css_value_tree_count(&root), (size_t)depth);
    ROBO_CHECK_EQ_INT(robo_css_value_child_count(&root), 1u);

    /* Disposal recurses as deep as the tree is; 64 frames is far above what a
     * parser will allow and far below a stack budget. */
    robo_css_value_dispose(&root);
    ROBO_CHECK_EQ_INT(robo_css_value_depth(&root), 0u);
    ROBO_CHECK_EQ_INT(robo_css_value_tree_count(&root), 0u);
}

void robo_test_register_css_robust(void);

void robo_test_register_css_robust(void)
{
    robo_test_register("css.robust.empty_and_whitespace",
                       test_css_robust_empty_and_whitespace);
    robo_test_register("css.robust.large_source", test_css_robust_large_source);
    robo_test_register("css.robust.incomplete_and_malformed",
                       test_css_robust_incomplete_and_malformed);
    robo_test_register("css.robust.unicode_through_models",
                       test_css_robust_unicode_through_models);
    robo_test_register("css.robust.repeated_churn", test_css_robust_repeated_churn);
    robo_test_register("css.robust.deep_rule_nesting",
                       test_css_robust_deep_rule_nesting);
    robo_test_register("css.robust.wide_structures", test_css_robust_wide_structures);
    robo_test_register("css.robust.deep_value_trees", test_css_robust_deep_value_trees);
}
