/*
 * RoboCSS Phase 9.1 rule/stylesheet/parser suite:
 * style rules and at-rules, the stylesheet object, and the parser cursor.
 *
 * The parse entry points are tested for what they *do not* do: return
 * ROBO_NOT_SUPPORTED, change nothing, and say why. A test asserting that they
 * parsed something would be testing a parser this phase does not have.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Rules                                                              */
/* ------------------------------------------------------------------ */

static void test_css_rule_kinds(void)
{
    ROBO_CHECK(robo_css_rule_kind_is_known(ROBO_CSS_RULE_KIND_STYLE));
    ROBO_CHECK(robo_css_rule_kind_is_known(ROBO_CSS_RULE_KIND_AT));
    ROBO_CHECK(robo_css_rule_kind_is_known(ROBO_CSS_RULE_KIND_NONE));
    ROBO_CHECK(!robo_css_rule_kind_is_known((robo_css_rule_kind_t)9));
    ROBO_CHECK_STR_EQ(robo_css_rule_kind_name(ROBO_CSS_RULE_KIND_AT), "at");
    ROBO_CHECK_STR_EQ(robo_css_rule_kind_name((robo_css_rule_kind_t)9), "unknown");
}

static void test_css_rule_style_rule(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_rule_t rule = ROBO_CSS_CLEARED;
    robo_css_style_rule_t *style;
    robo_css_complex_selector_t selector = ROBO_CSS_CLEARED;
    robo_css_declaration_t declaration = ROBO_CSS_CLEARED;
    robo_css_value_t value = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "p { color: red; margin: 10px; }");

    robo_css_rule_init(&rule);
    ROBO_CHECK_EQ_INT(rule.kind, ROBO_CSS_RULE_KIND_NONE);
    ROBO_CHECK(robo_css_rule_style(&rule) == NULL);
    ROBO_CHECK(robo_css_rule_at(&rule) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_rule_tree_count(&rule), 0u);

    ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&rule, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(rule.kind, ROBO_CSS_RULE_KIND_STYLE);
    style = &rule.style;
    ROBO_CHECK(robo_css_rule_at(&rule) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_rule_tree_count(&rule), 1u);
    ROBO_CHECK(!robo_css_style_rule_is_populated(style));

    css_complex_selector_of(&selector, &source, "p", NULL);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_append_selector(style, &selector, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_selector_count(style), 1u);

    robo_css_declaration_make(&declaration, robo_css_slice_from_text("color"),
                              ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&value, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_set_value(&declaration, &value, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_set_important(&declaration, 1, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_append_declaration(style, &declaration, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(declaration.state, ROBO_CSS_DECLARATION_STATE_NONE);

    robo_css_declaration_make(&declaration, robo_css_slice_from_text("margin"),
                              ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_append_declaration(style, &declaration, NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(style), 2u);
    ROBO_CHECK(robo_css_style_rule_is_populated(style));
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration(style, 0u)->important, 1);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration(style, 1u)->important, 0);
    /* Declarations stay in source order, duplicates included. */
    ROBO_CHECK(robo_css_slice_equals_ascii(
                   robo_css_declaration_name(robo_css_style_rule_declaration(style, 1u)),
                   "margin"));
    ROBO_CHECK(robo_css_style_rule_declaration(style, 2u) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(
                          robo_css_style_rule_declarations(style)),
                      2u);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(robo_css_style_rule_selectors(style)),
                      1u);

    /* A rule with selectors and no declarations is legal CSS and stays that
     * way; emptiness is reported, not repaired. */
    {
        robo_css_rule_t empty = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&empty, NULL), ROBO_OK);
        ROBO_CHECK(!robo_css_style_rule_is_populated(&empty.style));
        robo_css_rule_dispose(&empty);
    }

    robo_css_rule_dispose(&rule);
    ROBO_CHECK_EQ_INT(rule.kind, ROBO_CSS_RULE_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_selector_count(style), 0u);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(style), 0u);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_selector_count(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(NULL), 0u);
    ROBO_CHECK(robo_css_style_rule_declaration(NULL, 0u) == NULL);
    ROBO_CHECK(robo_css_style_rule_selectors(NULL) == NULL);
    ROBO_CHECK(robo_css_style_rule_declarations(NULL) == NULL);
    ROBO_CHECK(!robo_css_style_rule_is_populated(NULL));
    ROBO_CHECK_EQ_INT(robo_css_rule_tree_count(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_rule_range(NULL).begin, 0u);
    robo_css_rule_dispose(NULL);
    robo_css_rule_init(NULL);
    ROBO_CHECK_EQ_INT(robo_css_rule_make_style(NULL, NULL), ROBO_INVALID_ARGUMENT);
    robo_css_source_dispose(&source);
}

static void test_css_rule_at_rule(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_rule_t at = ROBO_CSS_CLEARED;
    robo_css_rule_t nested = ROBO_CSS_CLEARED;
    robo_css_rule_t nested_style = ROBO_CSS_CLEARED;
    robo_css_value_t value = ROBO_CSS_CLEARED;
    robo_css_declaration_t declaration = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "@media print { p { color: black } }");

    robo_css_rule_init(&at);
    ROBO_CHECK_EQ_INT(robo_css_rule_make_at(&at, robo_css_slice_from_text("media"),
                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at.kind, ROBO_CSS_RULE_KIND_AT);
    ROBO_CHECK(robo_css_rule_style(&at) == NULL);
    ROBO_CHECK(robo_css_rule_at(&at) != NULL);
    ROBO_CHECK(robo_css_slice_equals_ascii(robo_css_rule_at(&at)->name, "media"));

    /* A rule cannot be added to an at-rule that has no block: `@import url(x);`
     * is not silently turned into a block so that the append can succeed. */
    robo_css_rule_init(&nested);
    ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&nested, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(&at.at, &nested, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(&at.at), 0u);

    ROBO_CHECK_EQ_INT(robo_css_at_rule_set_prelude(&at.at,
                                                   robo_css_slice_from_text("print"), 1,
                                                   NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at.at.has_block, 1);
    ROBO_CHECK(robo_css_slice_equals_ascii(at.at.prelude, "print"));

    robo_css_declaration_make(&declaration, robo_css_slice_from_text("color"),
                              ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&value, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_set_value(&declaration, &value, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_append_declaration(&nested.style,
                                                             &declaration, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(&at.at, &nested, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(nested.kind, ROBO_CSS_RULE_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(&at.at), 1u);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(NULL), 0u);
    ROBO_CHECK(robo_css_at_rule_rule_at(&at.at, 0u) != NULL);
    ROBO_CHECK(robo_css_at_rule_rule_at(&at.at, 1u) == NULL);
    ROBO_CHECK(robo_css_at_rule_rule_at(NULL, 0u) == NULL);
    /* The tree count sees through the block. */
    ROBO_CHECK_EQ_INT(robo_css_rule_tree_count(&at), 2u);

    /* A nested at-rule is allowed to nest: `@media print { @supports ... }` is
     * legal, so the model must not be one level deep. */
    {
        robo_css_rule_t inner = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_rule_make_at(&inner,
                                                robo_css_slice_from_text("supports"),
                                                NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_set_prelude(&inner.at,
                                                       robo_css_slice_from_text("display: grid"),
                                                       1, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&nested_style, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(&inner.at, &nested_style, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(&at.at, &inner, NULL), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(&at.at), 2u);
    ROBO_CHECK_EQ_INT(robo_css_rule_tree_count(&at), 4u);

    /* Dropping the block drops the rules that lived in it: an at-rule that no
     * longer has a block must not keep orphans. */
    ROBO_CHECK_EQ_INT(robo_css_at_rule_set_prelude(&at.at, robo_css_slice_empty(), 0,
                                                   NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at.at.has_block, 0);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(&at.at), 0u);
    ROBO_CHECK_EQ_INT(robo_css_rule_tree_count(&at), 1u);

    /* An at-rule needs a name, and a prelude needs an at-rule that has one. */
    ROBO_CHECK_EQ_INT(robo_css_rule_make_at(&at, robo_css_slice_empty(), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_rule_make_at(NULL, robo_css_slice_from_text("media"),
                                            NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        robo_css_at_rule_t fresh = ROBO_CSS_CLEARED;
        robo_css_rule_t style_rule = ROBO_CSS_CLEARED;

        robo_css_at_rule_init(&fresh);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_set_prelude(&fresh,
                                                       robo_css_slice_from_text("x"), 1,
                                                       NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_make(&fresh, robo_css_slice_empty(), NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&style_rule, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(&fresh, &style_rule, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(NULL, &style_rule, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(&fresh, NULL, NULL),
                          ROBO_INVALID_ARGUMENT);
        robo_css_rule_dispose(&style_rule);
        robo_css_at_rule_dispose(&fresh);
    }
    {
        robo_css_rule_t candidate = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_rule_make_at(&nested,
                                                robo_css_slice_from_text("bad"),
                                                NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&candidate, NULL), ROBO_OK);
        /* No block on this at-rule, so the rule is refused and left untouched:
         * it still owns whatever it owns. */
        ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(&nested.at, &candidate, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(candidate.kind, ROBO_CSS_RULE_KIND_STYLE);
        robo_css_rule_dispose(&candidate);
    }
    /* Making one alternative over the other releases what the previous held. */
    ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&nested, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(nested.kind, ROBO_CSS_RULE_KIND_STYLE);
    ROBO_CHECK_EQ_INT(robo_css_rule_make_at(&nested, robo_css_slice_from_text("x"),
                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(nested.kind, ROBO_CSS_RULE_KIND_AT);
    ROBO_CHECK(robo_css_at_rule_rule_count(&nested.at) == 0u);

    robo_css_rule_dispose(&at);
    robo_css_rule_dispose(&nested);
    robo_css_source_dispose(&source);
}

static void test_css_rule_list(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_rule_list_t list = ROBO_CSS_CLEARED;
    robo_css_rule_t rule = ROBO_CSS_CLEARED;
    robo_css_declaration_t declaration = ROBO_CSS_CLEARED;
    const robo_css_rule_t *flattened[8];

    css_fixture();
    css_source_of_string(&source, "p { color: red } p { color: blue }");

    robo_css_rule_list_init(&list);
    ROBO_CHECK(robo_css_rule_list_is_empty(&list));
    ROBO_CHECK_EQ_INT(robo_css_rule_list_count(&list), 0u);
    ROBO_CHECK(robo_css_rule_list_at(&list, 0u) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_tree_count(&list), 0u);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_style_rule_count(&list), 0u);

    /* An unmade rule is refused: a rule list holds real rules. */
    robo_css_rule_init(&rule);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_append(&list, &rule, NULL),
                      ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&rule, NULL), ROBO_OK);
    robo_css_declaration_make(&declaration, robo_css_slice_from_text("color"),
                              ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_append_declaration(&rule.style, &declaration,
                                                             NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_append(&list, &rule, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(rule.kind, ROBO_CSS_RULE_KIND_NONE);

    ROBO_CHECK_EQ_INT(robo_css_rule_make_at(&rule, robo_css_slice_from_text("media"),
                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_set_prelude(&rule.at, robo_css_slice_empty(), 1,
                                                   NULL),
                      ROBO_OK);
    {
        robo_css_rule_t inner = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&inner, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(&rule.at, &inner, NULL),
                          ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_css_rule_list_append(&list, &rule, NULL), ROBO_OK);

    /* Two top-level rules, three counting the nested one; style rules are the
     * two, because the nested one is a style rule and the at-rule is not. */
    ROBO_CHECK_EQ_INT(robo_css_rule_list_count(&list), 2u);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_tree_count(&list), 3u);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_style_rule_count(&list), 2u);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_at(&list, 0u)->kind, ROBO_CSS_RULE_KIND_STYLE);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_at(&list, 1u)->kind, ROBO_CSS_RULE_KIND_AT);
    ROBO_CHECK(robo_css_rule_list_at(&list, 2u) == NULL);

    /* Flattening is depth-first in source order, and a short buffer is a
     * measurement, not a failure. */
    ROBO_CHECK_EQ_INT(robo_css_rule_list_flatten(&list, flattened, 8u), 3u);
    ROBO_CHECK_EQ_INT(flattened[0]->kind, ROBO_CSS_RULE_KIND_STYLE);
    ROBO_CHECK_EQ_INT(flattened[1]->kind, ROBO_CSS_RULE_KIND_AT);
    ROBO_CHECK_EQ_INT(flattened[2]->kind, ROBO_CSS_RULE_KIND_STYLE);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_flatten(&list, flattened, 2u), 3u);
    ROBO_CHECK_EQ_INT(flattened[0]->kind, ROBO_CSS_RULE_KIND_STYLE);
    ROBO_CHECK_EQ_INT(flattened[1]->kind, ROBO_CSS_RULE_KIND_AT);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_flatten(&list, NULL, 0u), 3u);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_flatten(NULL, flattened, 8u), 0u);
    /* The flat count and the tree count must agree: they measure the same
     * tree, and a disagreement would mean a rule was counted twice or lost. */
    ROBO_CHECK_EQ_INT(robo_css_rule_list_flatten(&list, NULL, 0u),
                      robo_css_rule_list_tree_count(&list));

    robo_css_rule_list_clear(&list);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_count(&list), 0u);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_tree_count(&list), 0u);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_append(&list, &rule, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_css_rule_list_dispose(&list);
    robo_css_rule_list_dispose(NULL);
    robo_css_rule_list_clear(NULL);
    robo_css_rule_list_init(NULL);
    ROBO_CHECK(robo_css_rule_list_is_empty(NULL));
    ROBO_CHECK_EQ_INT(robo_css_rule_list_count(NULL), 0u);
    ROBO_CHECK(robo_css_rule_list_at(NULL, 0u) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_tree_count(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_style_rule_count(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_append(NULL, &rule, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_append(&list, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_source_dispose(&source);
}

/* ------------------------------------------------------------------ */
/* Stylesheets                                                        */
/* ------------------------------------------------------------------ */

static void test_css_stylesheet_lifecycle(void)
{
    robo_css_stylesheet_id_t first = ROBO_CSS_STYLESHEET_ID_NONE;
    robo_css_stylesheet_id_t second = ROBO_CSS_STYLESHEET_ID_NONE;
    robo_css_diag_t diag;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(NULL, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&first, (robo_css_origin_t)42, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_STYLESHEET);
    ROBO_CHECK_EQ_INT(first, ROBO_CSS_STYLESHEET_ID_NONE);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&first, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_OK);
    ROBO_CHECK(first != ROBO_CSS_STYLESHEET_ID_NONE);
    ROBO_CHECK(robo_css_stylesheet_is_live(first));
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_origin(first), ROBO_CSS_ORIGIN_AUTHOR);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_state(first),
                      ROBO_CSS_STYLESHEET_STATE_CREATED);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(first), 0u);

    /* A second sheet gets a different id, and both are addressable by slot. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&second, ROBO_CSS_ORIGIN_RUNTIME, NULL),
                      ROBO_OK);
    ROBO_CHECK(second != first);
    ROBO_CHECK_EQ_INT(robo_css_live_stylesheet_count(), 2u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_id_at(0u), first);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_id_at(1u), second);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_id_at(2u), ROBO_CSS_STYLESHEET_ID_NONE);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(first, NULL), ROBO_OK);
    /* A destroyed id stays dead, and destroying it again is NOT_FOUND rather
     * than a second attempt at the same free. */
    ROBO_CHECK(!robo_css_stylesheet_is_live(first));
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(first, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(ROBO_CSS_STYLESHEET_ID_NONE, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy((robo_css_stylesheet_id_t)99999u, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_css_live_stylesheet_count(), 1u);

    /* Every accessor answers sensibly for an id that does not resolve. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_origin(first), ROBO_CSS_ORIGIN_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_state(first), ROBO_CSS_STYLESHEET_STATE_NONE);
    ROBO_CHECK(robo_css_stylesheet_name(first) == NULL);
    ROBO_CHECK(robo_css_stylesheet_source(first) == NULL);
    ROBO_CHECK(robo_css_stylesheet_rules(first) == NULL);
    ROBO_CHECK(robo_css_stylesheet_rule(first, 0u) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_source_length(first), 0u);
    ROBO_CHECK(!robo_css_stylesheet_owns_source(first));
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_remove_rule(first, 0u, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_state(first,
                                                    ROBO_CSS_STYLESHEET_STATE_PARSED,
                                                    NULL),
                      ROBO_NOT_FOUND);
    robo_css_stylesheet_clear_rules(first); /* tolerant by contract */

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(second, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_live_stylesheet_count(), 0u);
}

static void test_css_stylesheet_origin_state_name(void)
{
    robo_css_stylesheet_id_t id = ROBO_CSS_STYLESHEET_ID_NONE;
    robo_css_diag_t diag;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&id, ROBO_CSS_ORIGIN_NONE, NULL),
                      ROBO_OK);
    /* A sheet whose provenance is not decided yet is legal and can be set. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_origin(id), ROBO_CSS_ORIGIN_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_origin(id, ROBO_CSS_ORIGIN_USER, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_origin(id), ROBO_CSS_ORIGIN_USER);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_origin(id, (robo_css_origin_t)42, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_origin(id), ROBO_CSS_ORIGIN_USER);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_origin((robo_css_stylesheet_id_t)777u,
                                                     ROBO_CSS_ORIGIN_USER, NULL),
                      ROBO_NOT_FOUND);

    /* A live sheet is never in the NONE state: NONE means "no stylesheet". */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_state(id,
                                                    ROBO_CSS_STYLESHEET_STATE_NONE,
                                                    NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_state(id,
                                                    ROBO_CSS_STYLESHEET_STATE_PARSED,
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_state(id),
                      ROBO_CSS_STYLESHEET_STATE_PARSED);
    ROBO_CHECK(robo_css_stylesheet_state_is_settled(robo_css_stylesheet_state(id)));

    ROBO_CHECK_STR_EQ(robo_css_stylesheet_name(id), "");
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_name(id, "Application sheet", NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_css_stylesheet_name(id), "Application sheet");
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_name(id, NULL, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_css_stylesheet_name(id), "");
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_name((robo_css_stylesheet_id_t)777u, "x",
                                                   &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_STYLESHEET);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(id, NULL), ROBO_OK);
}

static void test_css_stylesheet_source_attachment(void)
{
    robo_css_stylesheet_id_t id = ROBO_CSS_STYLESHEET_ID_NONE;
    const robo_css_source_t *source;
    char buffer[64];

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&id, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_OK);
    /* No source attached yet is reported as "no source", not as an empty one. */
    source = robo_css_stylesheet_source(id);
    ROBO_CHECK(source == NULL);

    strcpy(buffer, CSS_FIXTURE_BODY);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_source_string(id, buffer, NULL), ROBO_OK);
    source = robo_css_stylesheet_source(id);
    ROBO_CHECK(source != NULL);
    ROBO_CHECK_EQ_INT(source->kind, ROBO_CSS_SOURCE_KIND_STRING);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_source_length(id), strlen(buffer));
    ROBO_CHECK(!robo_css_stylesheet_owns_source(id));

    /* Copy mode: the sheet outlives the buffer it was built from. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_copy_source(id, (const uint8_t *)buffer,
                                                      strlen(buffer), NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_stylesheet_owns_source(id));
    buffer[0] = 'X';
    source = robo_css_stylesheet_source(id);
    ROBO_CHECK_EQ_INT(source->bytes[0], (uint8_t)'b');
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_source_length(id), strlen(CSS_FIXTURE_BODY));

    /* Borrowed again: the owned copy is released as the borrow replaces it. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_source_memory(id,
                                                            (const uint8_t *)CSS_FIXTURE_BODY,
                                                            strlen(CSS_FIXTURE_BODY),
                                                            NULL),
                      ROBO_OK);
    ROBO_CHECK(!robo_css_stylesheet_owns_source(id));

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_source_memory((robo_css_stylesheet_id_t)777u,
                                                            NULL, 0u, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_source_memory(id, NULL, 4u, NULL),
                      ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(id, NULL), ROBO_OK);
}

static void test_css_stylesheet_rule_storage(void)
{
    robo_css_stylesheet_id_t id = ROBO_CSS_STYLESHEET_ID_NONE;
    robo_css_rule_t rule = ROBO_CSS_CLEARED;
    robo_css_rule_t at = ROBO_CSS_CLEARED;
    robo_css_declaration_t declaration = ROBO_CSS_CLEARED;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&id, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_copy_source(id,
                                                      (const uint8_t *)"p{color:red}",
                                                      strlen("p{color:red}"), NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&rule, NULL), ROBO_OK);
    robo_css_declaration_make(&declaration, robo_css_slice_from_text("color"),
                              ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_append_declaration(&rule.style, &declaration,
                                                             NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_append_rule(id, &rule, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(rule.kind, ROBO_CSS_RULE_KIND_NONE);

    ROBO_CHECK_EQ_INT(robo_css_rule_make_at(&at, robo_css_slice_from_text("media"),
                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_at_rule_set_prelude(&at.at, robo_css_slice_empty(), 1,
                                                   NULL),
                      ROBO_OK);
    {
        robo_css_rule_t inner = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&inner, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(&at.at, &inner, NULL), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_append_rule(id, &at, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(id), 2u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_tree_rule_count(id), 3u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_style_rule_count(id), 2u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule(id, 0u)->kind, ROBO_CSS_RULE_KIND_STYLE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule(id, 1u)->kind, ROBO_CSS_RULE_KIND_AT);
    ROBO_CHECK(robo_css_stylesheet_rule(id, 2u) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rules(id)->count, 2u);

    /* Removing from the middle keeps the rest in source order. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_remove_rule(id, 0u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(id), 1u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule(id, 0u)->kind, ROBO_CSS_RULE_KIND_AT);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_remove_rule(id, 5u, NULL), ROBO_NOT_FOUND);

    robo_css_stylesheet_clear_rules(id);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(id), 0u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_append_rule(id, &rule, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_append_rule((robo_css_stylesheet_id_t)777u,
                                                      &rule, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_append_rule(id, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(id, NULL), ROBO_OK);
}

static void test_css_stylesheet_parse_boundary(void)
{
    robo_css_stylesheet_id_t id = ROBO_CSS_STYLESHEET_ID_NONE;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_rule_t rule = ROBO_CSS_CLEARED;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&id, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_copy_source(id,
                                                      (const uint8_t *)CSS_FIXTURE_PARAGRAPH,
                                                      strlen(CSS_FIXTURE_PARAGRAPH),
                                                      NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&rule, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_append_rule(id, &rule, NULL), ROBO_OK);

    /* The parser does not exist yet, and this is what that looks like from the
     * outside: not supported, nothing changed, and a reason recorded. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_parse(id, &error, NULL), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_UNSUPPORTED);
    ROBO_CHECK_EQ_INT(error.code, ROBO_NOT_SUPPORTED);
    ROBO_CHECK(robo_css_error_kind_is_fatal(error.kind));
    ROBO_CHECK(error.message != NULL);
    ROBO_CHECK(strstr(error.message, "9.3") != NULL);

    /* Nothing was touched: the rules are the ones that were appended by hand,
     * the state has not advanced, and the source is intact. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(id), 1u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_state(id),
                      ROBO_CSS_STYLESHEET_STATE_CREATED);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_source_length(id),
                      strlen(CSS_FIXTURE_PARAGRAPH));

    /* The reason survives on the stylesheet, for a caller that did not ask for
     * it at the call site. */
    {
        robo_css_error_t recorded = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_stylesheet_last_error(id, &recorded), 1);
        ROBO_CHECK_EQ_INT(recorded.kind, ROBO_CSS_ERROR_KIND_UNSUPPORTED);
        ROBO_CHECK_EQ_INT(recorded.code, ROBO_NOT_SUPPORTED);
        ROBO_CHECK(robo_css_error_is_error(&recorded));
    }

    /* The error slot can be cleared, and clearing it reports "no error". */
    robo_css_stylesheet_set_last_error(id, NULL);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_last_error(id, &error), 0);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);

    /* A caller may attach its own explanation, and reading it back is exact. */
    {
        robo_css_error_t mine = ROBO_CSS_CLEARED;

        robo_css_error_set(&mine, ROBO_CSS_ERROR_KIND_MALFORMED_RULE, ROBO_PARSE_ERROR,
                           NULL, "hand-written explanation");
        robo_css_stylesheet_set_last_error(id, &mine);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_last_error(id, &error), 1);
        ROBO_CHECK_STR_EQ(error.message, "hand-written explanation");
        ROBO_CHECK(robo_css_error_kind_is_recoverable(error.kind));
    }

    /* An id that does not resolve reports NOT_FOUND, and asks for no error. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_parse((robo_css_stylesheet_id_t)777u,
                                                &error, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);

    /* The error record is optional, and NULL is not dereferenced. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_parse(id, NULL, NULL), ROBO_NOT_SUPPORTED);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_last_error(id, &error), 0);
}

/* ------------------------------------------------------------------ */
/* Parser cursor                                                      */
/* ------------------------------------------------------------------ */

static void test_css_parser_cursor(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_parser_t parser = ROBO_CSS_CLEARED;
    robo_css_position_t position;
    robo_css_diag_t diag;

    css_fixture();
    css_source_of_string(&source, "a {\n  b: c;\n}\n");
    robo_css_parser_init(&parser, &source, ROBO_CSS_ORIGIN_AUTHOR, NULL);
    ROBO_CHECK(robo_css_parser_source(&parser) == &source);
    ROBO_CHECK_EQ_INT(robo_css_parser_origin(&parser), ROBO_CSS_ORIGIN_AUTHOR);
    ROBO_CHECK_EQ_INT(robo_css_parser_offset(&parser), 0u);
    ROBO_CHECK_EQ_INT(robo_css_parser_remaining(&parser), strlen("a {\n  b: c;\n}\n"));
    ROBO_CHECK(!robo_css_parser_at_end(&parser));
    ROBO_CHECK(!robo_css_parser_has_error(&parser));
    ROBO_CHECK(robo_css_parser_error(&parser) == NULL);

    ROBO_CHECK_EQ_INT(robo_css_parser_position(&parser, &position, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.offset, 0u);
    ROBO_CHECK_EQ_INT(position.line, 1u);
    ROBO_CHECK_EQ_INT(position.column, 1u);
    ROBO_CHECK(robo_css_range_is_empty(robo_css_parser_cursor_range(&parser)));

    /* Offset 5 is inside the second line, one scalar past its first character,
     * so the cursor is on line 2 at column 2 — columns count scalars, not the
     * bytes of the indent that produced them. */
    ROBO_CHECK_EQ_INT(robo_css_parser_advance(&parser, 5u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_parser_offset(&parser), 5u);
    ROBO_CHECK_EQ_INT(robo_css_parser_position(&parser, &position, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 2u);
    ROBO_CHECK_EQ_INT(position.column, 2u);

    ROBO_CHECK_EQ_INT(robo_css_parser_advance(&parser, 0u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_parser_offset(&parser), 5u);

    /* Seeking is absolute and refuses to go past the end... */
    ROBO_CHECK_EQ_INT(robo_css_parser_seek(&parser, strlen("a {\n  b: c;\n}\n"), NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_parser_at_end(&parser));
    ROBO_CHECK_EQ_INT(robo_css_parser_remaining(&parser), 0u);
    ROBO_CHECK_EQ_INT(robo_css_parser_seek(&parser, strlen("a {\n  b: c;\n}\n") + 1u,
                                           NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_parser_offset(&parser), strlen("a {\n  b: c;\n}\n"));

    /* ...while advancing past the end lands at the end and says so, so a
     * recovery path always continues from a defined place. */
    robo_css_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_css_parser_advance(&parser, 3u, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_PARSER);
    ROBO_CHECK_EQ_INT(robo_css_parser_offset(&parser), strlen("a {\n  b: c;\n}\n"));

    ROBO_CHECK_EQ_INT(robo_css_parser_reset(&parser, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_parser_offset(&parser), 0u);
    ROBO_CHECK(!robo_css_parser_at_end(&parser));

    ROBO_CHECK_EQ_INT(robo_css_parser_seek(&parser, 99u, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_parser_init(&parser, &source, (robo_css_origin_t)42,
                                           NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_parser_init(&parser, NULL, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_parser_init(NULL, &source, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* An empty source is a legal cursor: at the end from the start. */
    ROBO_CHECK_EQ_INT(robo_css_parser_init(&parser, &source, ROBO_CSS_ORIGIN_AUTHOR,
                                           NULL),
                      ROBO_OK);
    robo_css_source_dispose(&source);
    ROBO_CHECK_EQ_INT(robo_css_parser_init(&parser, &source, ROBO_CSS_ORIGIN_AUTHOR,
                                           NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_parser_at_end(&parser));
    ROBO_CHECK_EQ_INT(robo_css_parser_position(&parser, &position, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 1u);
    ROBO_CHECK_EQ_INT(position.column, 1u);

    /* A disposed parser refuses work rather than reading through a stale
     * source pointer. */
    robo_css_parser_dispose(&parser);
    ROBO_CHECK(robo_css_parser_source(&parser) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_parser_origin(&parser), ROBO_CSS_ORIGIN_NONE);
    ROBO_CHECK(robo_css_parser_at_end(&parser));
    ROBO_CHECK_EQ_INT(robo_css_parser_remaining(&parser), 0u);
    ROBO_CHECK_EQ_INT(robo_css_parser_reset(&parser, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_parser_advance(&parser, 1u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_parser_seek(&parser, 0u, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_parser_position(&parser, &position, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_css_parser_dispose(&parser);
    robo_css_parser_dispose(NULL);

    ROBO_CHECK(robo_css_parser_source(NULL) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_parser_offset(NULL), 0u);
    ROBO_CHECK(robo_css_parser_at_end(NULL));
    ROBO_CHECK_EQ_INT(robo_css_parser_remaining(NULL), 0u);
    ROBO_CHECK(!robo_css_parser_has_error(NULL));
    ROBO_CHECK(robo_css_parser_error(NULL) == NULL);
    ROBO_CHECK(!robo_css_parser_record_error(NULL, NULL));
    ROBO_CHECK(!robo_css_parser_fail(NULL, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF,
                                     ROBO_PARSE_ERROR, "x"));
    ROBO_CHECK(!robo_css_parser_take_error(NULL, NULL));
    ROBO_CHECK_EQ_INT(robo_css_parser_cursor_range(NULL).begin, 0u);
}

static void test_css_parser_first_error(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_parser_t parser = ROBO_CSS_CLEARED;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_error_t other = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "p {\n  color: red;\n}\n");
    ROBO_CHECK_EQ_INT(robo_css_parser_init(&parser, &source, ROBO_CSS_ORIGIN_AUTHOR,
                                           NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_parser_seek(&parser, 4u, NULL), ROBO_OK);
    ROBO_CHECK(robo_css_parser_fail(&parser, ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION,
                                    ROBO_PARSE_ERROR, "expected a colon"));
    ROBO_CHECK(robo_css_parser_has_error(&parser));
    ROBO_CHECK(robo_css_parser_error(&parser) != NULL);
    ROBO_CHECK_EQ_INT(robo_css_parser_error(&parser)->kind,
                      ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION);
    /* The error is positioned where the cursor was, in line and column terms. */
    ROBO_CHECK_EQ_INT(robo_css_parser_error(&parser)->position.offset, 4u);
    ROBO_CHECK_EQ_INT(robo_css_parser_error(&parser)->position.line, 2u);
    ROBO_CHECK_EQ_INT(robo_css_parser_error(&parser)->position.column, 1u);

    /* The first error is the interesting one: later ones do not overwrite it,
     * because after a recovery path has skipped three constructs the useful
     * report is still the first problem. */
    ROBO_CHECK(!robo_css_parser_fail(&parser, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF,
                                     ROBO_PARSE_ERROR, "later"));
    ROBO_CHECK_EQ_INT(robo_css_parser_error(&parser)->kind,
                      ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION);
    ROBO_CHECK_STR_EQ(robo_css_parser_error(&parser)->message, "expected a colon");

    /* The record can be taken, which clears it. */
    ROBO_CHECK(robo_css_parser_take_error(&parser, &error));
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION);
    ROBO_CHECK(!robo_css_parser_has_error(&parser));
    ROBO_CHECK(robo_css_parser_error(&parser) == NULL);
    ROBO_CHECK(!robo_css_parser_take_error(&parser, &error));

    /* A record with kind NONE is not an error and is not recorded: a parser
     * that accepted input has nothing to report. */
    robo_css_error_init(&other);
    ROBO_CHECK(!robo_css_parser_record_error(&parser, &other));
    ROBO_CHECK(!robo_css_parser_has_error(&parser));
    robo_css_error_set(&other, ROBO_CSS_ERROR_KIND_MALFORMED_RULE, ROBO_PARSE_ERROR,
                       NULL, "no rule here");
    ROBO_CHECK(robo_css_parser_record_error(&parser, &other));
    ROBO_CHECK(robo_css_parser_has_error(&parser));
    ROBO_CHECK_EQ_INT(robo_css_parser_error(&parser)->kind,
                      ROBO_CSS_ERROR_KIND_MALFORMED_RULE);

    /* Resetting clears the error as well as the cursor. */
    ROBO_CHECK_EQ_INT(robo_css_parser_reset(&parser, NULL), ROBO_OK);
    ROBO_CHECK(!robo_css_parser_has_error(&parser));
    ROBO_CHECK_EQ_INT(robo_css_parser_offset(&parser), 0u);

    /* A failed parse call still positions its error at the cursor. Offset 18 is
     * the closing brace, which is on the third line of the source. */
    ROBO_CHECK_EQ_INT(robo_css_parser_seek(&parser, 18u, NULL), ROBO_OK);
    ROBO_CHECK(robo_css_parser_fail(&parser, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF,
                                    ROBO_PARSE_ERROR, "unterminated block"));
    ROBO_CHECK_EQ_INT(robo_css_parser_error(&parser)->position.offset, 18u);
    ROBO_CHECK_EQ_INT(robo_css_parser_error(&parser)->position.line, 3u);
    /* Taking an error leaves the cursor where the failure happened. */
    ROBO_CHECK_EQ_INT(robo_css_parser_offset(&parser), 18u);

    robo_css_parser_dispose(&parser);
    robo_css_source_dispose(&source);
}

static void test_css_stylesheet_lifetime_balances(void)
{
    int we_enabled;
    size_t baseline;

    css_fixture();
    we_enabled = css_allocator_ensure_enabled();
    baseline = robo_mem_debug_live_count();
    {
        int i;

        for (i = 0; i < 16; i++) {
            robo_css_stylesheet_id_t id = ROBO_CSS_STYLESHEET_ID_NONE;
            robo_css_rule_t style_rule = ROBO_CSS_CLEARED;
            robo_css_rule_t at_rule = ROBO_CSS_CLEARED;
            robo_css_declaration_t declaration = ROBO_CSS_CLEARED;
            robo_css_value_t value = ROBO_CSS_CLEARED;

            ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&id, ROBO_CSS_ORIGIN_AUTHOR,
                                                         NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_stylesheet_copy_source(
                                  id, (const uint8_t *)CSS_FIXTURE_CLASS,
                                  strlen(CSS_FIXTURE_CLASS), NULL),
                              ROBO_OK);

            ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&style_rule, NULL), ROBO_OK);
            robo_css_declaration_make(&declaration, robo_css_slice_from_text("padding"),
                                      ROBO_CSS_DECLARATION_STATE_PARSED, NULL);
            ROBO_CHECK_EQ_INT(robo_css_value_make_list(&value, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_declaration_set_value(&declaration, &value, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_style_rule_append_declaration(&style_rule.style,
                                                                     &declaration, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_stylesheet_append_rule(id, &style_rule, NULL),
                              ROBO_OK);

            ROBO_CHECK_EQ_INT(robo_css_rule_make_at(&at_rule,
                                                    robo_css_slice_from_text("media"),
                                                    NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_at_rule_set_prelude(&at_rule.at,
                                                           robo_css_slice_empty(), 1,
                                                           NULL),
                              ROBO_OK);
            {
                robo_css_rule_t inner = ROBO_CSS_CLEARED;

                ROBO_CHECK_EQ_INT(robo_css_rule_make_style(&inner, NULL), ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_css_at_rule_append_rule(&at_rule.at, &inner,
                                                               NULL),
                                  ROBO_OK);
            }
            ROBO_CHECK_EQ_INT(robo_css_stylesheet_append_rule(id, &at_rule, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_stylesheet_tree_rule_count(id), 3u);

            ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(id, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
        }
    }
    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    css_allocator_restore(we_enabled);
    ROBO_CHECK_EQ_INT(robo_css_live_stylesheet_count(), 0u);
}

void robo_test_register_css_sheet(void);

void robo_test_register_css_sheet(void)
{
    robo_test_register("css.rule.kinds", test_css_rule_kinds);
    robo_test_register("css.rule.style_rule", test_css_rule_style_rule);
    robo_test_register("css.rule.at_rule", test_css_rule_at_rule);
    robo_test_register("css.rule.list", test_css_rule_list);
    robo_test_register("css.sheet.lifecycle", test_css_stylesheet_lifecycle);
    robo_test_register("css.sheet.origin_state_name",
                       test_css_stylesheet_origin_state_name);
    robo_test_register("css.sheet.source_attachment",
                       test_css_stylesheet_source_attachment);
    robo_test_register("css.sheet.rule_storage", test_css_stylesheet_rule_storage);
    robo_test_register("css.sheet.parse_boundary", test_css_stylesheet_parse_boundary);
    robo_test_register("css.parser.cursor", test_css_parser_cursor);
    robo_test_register("css.parser.first_error", test_css_parser_first_error);
    robo_test_register("css.sheet.lifetime_balances",
                       test_css_stylesheet_lifetime_balances);
}
