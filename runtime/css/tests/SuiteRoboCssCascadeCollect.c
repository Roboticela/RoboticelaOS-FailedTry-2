/*
 * RoboCSS cascade collection (Phase 9.5) — css.cascade.collect.*
 *
 * Collection is where the cascade meets everything else: origin lists, selector
 * matching (9.4), the property table, the inline `style` attribute. Every test
 * here is about what ends up in the candidate array and what ends up in the
 * counters — and the counters are the point. This build can be *incomplete* in
 * half a dozen ways, and each of them has a name on the result so that a caller
 * never has to guess whether an empty array means "nothing applies" or "this
 * build could not look".
 *
 * The negative answers are what these tests are for: an undecided selector, an
 * at-rule block, an unparsed rule, a destroyed sheet. Each one must be *counted*
 * and must not silently become a non-match.
 */

#include "RoboCssTestSupport.h"

/* The inline-style stub (css_inline_stub_t) and the fixture that installs it
 * live in RoboCssTestSupport.h: the resolve suite needs the same seam, and two
 * stubs for one callback interface is exactly the drift the shared fixtures
 * exist to prevent. */

/** The number of candidates declaring @p name, by name. */
static size_t css_cascade_named_count(const robo_css_cascade_t *cascade, const char *name)
{
    robo_css_slice_t wanted = robo_css_slice_from_text(name);
    size_t index;
    size_t total = 0u;

    for (index = 0u; index < robo_css_cascade_count(cascade); index++) {
        const robo_css_cascade_candidate_t *candidate = robo_css_cascade_at(cascade, index);

        if (robo_css_slice_equals_ci(robo_css_declaration_name(candidate->declaration),
                                    wanted)) {
            total++;
        }
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* What applies                                                        */
/* ------------------------------------------------------------------ */

static void test_css_cascade_collect_matches_only_the_element(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;
    robo_css_element_ref_t paragraph;

    css_fixture();
    css_cascade_begin(&fixture);
    div = css_host_add(&fixture.host, "div", "app", NULL);
    paragraph = css_host_add(&fixture.host, "p", NULL, NULL);
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: red }\np { color: blue }");

    css_cascade_collect(&fixture, div, &cascade, NULL);
    /* Both rules were looked at; one matched. A cascade that reported the
     * scanned count as matched would hide a selector bug in plain sight. */
    ROBO_CHECK_EQ_INT(cascade.rules_scanned, 2u);
    ROBO_CHECK_EQ_INT(cascade.rules_matched, 1u);
    ROBO_CHECK_EQ_INT(cascade.rules_undecided, 0u);
    ROBO_CHECK_EQ_INT(cascade.unsupported, 0);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 1u);
    ROBO_CHECK_EQ_INT(cascade.items[0].selector_index, 0u);
    ROBO_CHECK_EQ_INT(cascade.items[0].rule_index, 0u);
    ROBO_CHECK_EQ_INT(cascade.items[0].origin, ROBO_CSS_ORIGIN_AUTHOR);
    ROBO_CHECK_EQ_INT(cascade.items[0].level, ROBO_CSS_CASCADE_LEVEL_AUTHOR);
    /* `color` with no property table installed resolves to no identity, and the
     * name is still the author's — which is why both questions exist. */
    ROBO_CHECK_EQ_INT(cascade.items[0].property, ROBO_CSS_PROPERTY_ID_NONE);
    {
        char name[16];

        css_cascade_candidate_name(&cascade, 0u, name, sizeof(name));
        ROBO_CHECK_STR_EQ(name, "color");
        /* An out-of-range index and a NULL buffer are tolerated rather than
         * dereferenced, like every other accessor in the subsystem. */
        ROBO_CHECK_EQ_INT(robo_css_cascade_at(&cascade, 99u) == NULL, 1);
        css_cascade_candidate_name(&cascade, 99u, name, sizeof(name));
        ROBO_CHECK_STR_EQ(name, "");
        css_cascade_candidate_name(&cascade, 0u, NULL, 0u);
    }
    robo_css_cascade_dispose(&cascade);

    css_cascade_collect(&fixture, paragraph, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 1u);
    ROBO_CHECK_EQ_INT(cascade.items[0].rule_index, 1u);
    robo_css_cascade_dispose(&cascade);

    css_cascade_end(&fixture);
}

static void test_css_cascade_collect_order_across_origins(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    /* Added weakest last on purpose: the context's lists decide the order, not
     * the order the caller happened to create the sheets in. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: author }");
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_USER, "div { color: user }");
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_RUNTIME, "div { color: runtime }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(cascade.sheets_scanned, 3u);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 3u);
    /* Cascade order: ascending level, so runtime first and author last. */
    ROBO_CHECK_EQ_INT(cascade.items[0].origin, ROBO_CSS_ORIGIN_RUNTIME);
    ROBO_CHECK_EQ_INT(cascade.items[1].origin, ROBO_CSS_ORIGIN_USER);
    ROBO_CHECK_EQ_INT(cascade.items[2].origin, ROBO_CSS_ORIGIN_AUTHOR);
    ROBO_CHECK_EQ_INT(cascade.items[0].order, 0u);
    ROBO_CHECK_EQ_INT(cascade.items[1].order, 1u);
    ROBO_CHECK_EQ_INT(cascade.items[2].order, 2u);
    ROBO_CHECK_EQ_INT(cascade.items[0].sheet_index, 0u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "author");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_collect_important_reorders(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    /* One rule, one sheet, one selector — the case where document order decides
     * without specificity, and where `!important` has to move the declaration
     * past every normal one in the same block. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: first; color: second !important; color: third }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 3u);
    ROBO_CHECK_EQ_INT(cascade.items[0].important, 0);
    ROBO_CHECK_EQ_INT(cascade.items[1].important, 0);
    ROBO_CHECK_EQ_INT(cascade.items[2].important, 1);
    ROBO_CHECK_EQ_INT(cascade.items[2].level, ROBO_CSS_CASCADE_LEVEL_AUTHOR_IMPORTANT);
    /* The two normal ones kept their document order. */
    ROBO_CHECK_EQ_INT(cascade.items[0].declaration_index, 0u);
    ROBO_CHECK_EQ_INT(cascade.items[1].declaration_index, 2u);
    ROBO_CHECK_EQ_INT(cascade.items[1].order < cascade.items[0].order, 0);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "second");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_collect_two_sheets_in_one_origin(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    /* Within one origin, the context's insertion order is the cascade order —
     * there is nothing else to order two author sheets by without a document
     * position, which is why the list is ordered rather than sorted. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: one }");
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: two }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 2u);
    ROBO_CHECK_EQ_INT(cascade.items[0].sheet_index, 0u);
    ROBO_CHECK_EQ_INT(cascade.items[1].sheet_index, 1u);
    ROBO_CHECK_EQ_INT(cascade.items[0].level, cascade.items[1].level);
    ROBO_CHECK_EQ_INT(cascade.items[0].sheet != cascade.items[1].sheet, 1);
    /* Two different selectors, one level: the cascade can place them but cannot
     * say which wins, and it must say *that* rather than pick the later one. */
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY);
    ROBO_CHECK_STR_EQ(text, "");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The inline style attribute                                          */
/* ------------------------------------------------------------------ */

static void test_css_cascade_collect_inline_is_last_and_strong(void)
{
    css_cascade_fixture_t fixture;
    css_inline_stub_t stub;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: author; padding: 4px }");
    css_cascade_inline(&fixture, &stub, "color: inline");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(stub.calls, 1);
    ROBO_CHECK_EQ_INT(stub.last_element, 1u);
    ROBO_CHECK_EQ_INT(cascade.inline_checked, 1);
    ROBO_CHECK_EQ_INT(cascade.inline_present, 1);
    ROBO_CHECK_EQ_INT(cascade.has_inline_error, 0);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 3u);
    /* The inline declaration is met last, and it is above the author level. */
    ROBO_CHECK_EQ_INT(cascade.items[2].origin, ROBO_CSS_ORIGIN_INLINE);
    ROBO_CHECK_EQ_INT(cascade.items[2].level, ROBO_CSS_CASCADE_LEVEL_INLINE);
    ROBO_CHECK_EQ_INT(cascade.items[2].sheet, ROBO_CSS_STYLESHEET_ID_NONE);
    ROBO_CHECK_EQ_INT(cascade.items[2].sheet_index, ROBO_CSS_CASCADE_NO_INDEX);
    ROBO_CHECK_EQ_INT(cascade.items[2].rule_index, ROBO_CSS_CASCADE_NO_INDEX);
    ROBO_CHECK_EQ_INT(cascade.items[2].selector_index, ROBO_CSS_CASCADE_NO_INDEX);
    ROBO_CHECK_EQ_INT(cascade.items[2].declaration_index, 0u);
    ROBO_CHECK_EQ_INT(cascade.unsupported, 0);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "inline");
    /* The sheet's other declaration is still there and uncontested. */
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "padding", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "4px");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_collect_inline_absent_and_empty(void)
{
    css_cascade_fixture_t fixture;
    css_inline_stub_t stub;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: red }");

    /* No `style` attribute at all: nothing was declared, and nothing is wrong. */
    css_cascade_inline(&fixture, &stub, NULL);
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(cascade.inline_checked, 1);
    ROBO_CHECK_EQ_INT(cascade.inline_present, 0);
    ROBO_CHECK_EQ_INT(cascade.has_inline_error, 0);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&cascade.inline_declarations), 0u);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 1u);
    ROBO_CHECK_EQ_INT(cascade.unsupported, 0);
    robo_css_cascade_dispose(&cascade);

    /* `style=""`: there *is* an attribute, and it declares nothing. The two are
     * different facts, which is why `inline_present` exists at all. */
    css_cascade_inline(&fixture, &stub, "");
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(cascade.inline_present, 1);
    ROBO_CHECK_EQ_INT(cascade.has_inline_error, 0);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&cascade.inline_declarations), 0u);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 1u);
    robo_css_cascade_dispose(&cascade);

    /* With no provider installed at all, the boundary was never crossed. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_inline_style(fixture.context, NULL, NULL,
                                                              NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_inline_userdata(fixture.context) == NULL, 1);
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(cascade.inline_checked, 0);
    ROBO_CHECK_EQ_INT(cascade.items[0].origin, ROBO_CSS_ORIGIN_AUTHOR);
    robo_css_cascade_dispose(&cascade);

    css_cascade_end(&fixture);
}

static void test_css_cascade_collect_inline_problem_is_reported_not_fatal(void)
{
    css_cascade_fixture_t fixture;
    css_inline_stub_t stub;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_error_t error = ROBO_CSS_CLEARED;

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: red }");
    /* A malformed inline declaration is recovered from, exactly as a malformed
     * one inside a rule is (9.3): the valid declarations are collected, and the
     * problem is reported through the call's error record. */
    css_cascade_inline(&fixture, &stub, "color red; background: blue");

    css_cascade_collect(&fixture, 1u, &cascade, &error);
    ROBO_CHECK_EQ_INT(cascade.has_inline_error, 1);
    ROBO_CHECK_EQ_INT(cascade.inline_error.kind != ROBO_CSS_ERROR_KIND_NONE, 1);
    ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 1);
    ROBO_CHECK_EQ_INT(error.kind, cascade.inline_error.kind);
    ROBO_CHECK_EQ_INT(cascade.declarations_ignored, 1u);
    /* Two candidates: the rule's `color` and the inline `background`. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 2u);
    ROBO_CHECK_EQ_INT(css_cascade_named_count(&cascade, "background"), 1u);
    /* Recovered, so the answer is complete — an incomplete answer is a different
     * fact from a CSS problem, and the two fields say so separately. */
    ROBO_CHECK_EQ_INT(cascade.unsupported, 0);

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_collect_inline_host_failure_incomplete(void)
{
    css_cascade_fixture_t fixture;
    css_inline_stub_t stub;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_error_t error = ROBO_CSS_CLEARED;

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: red }");
    css_cascade_inline(&fixture, &stub, NULL);
    stub.failure = ROBO_NOT_SUPPORTED;

    css_cascade_collect(&fixture, 1u, &cascade, &error);
    /* The sheets' answer is real, so the call succeeds — and the answer is a
     * lower bound, so `unsupported` says so. Treating a host that cannot answer
     * as "the element has no inline declarations" would be the silent wrong-rule
     * bug the whole three-outcome discipline exists to prevent. */
    ROBO_CHECK_EQ_INT(cascade.unsupported, 1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_is_complete(&cascade), 0);
    ROBO_CHECK_EQ_INT(cascade.has_inline_error, 1);
    ROBO_CHECK_EQ_INT(cascade.inline_error.code, ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 1u);
    ROBO_CHECK_EQ_INT(cascade.items[0].origin, ROBO_CSS_ORIGIN_AUTHOR);

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* What could not be used                                              */
/* ------------------------------------------------------------------ */

static void test_css_cascade_collect_rule_without_selectors(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    /* Added by hand: the prelude exists as component values (9.3) but was never
     * turned into selectors, so the rule's reach is unknown. A cascade that
     * treated that as "does not apply" would silently drop the rule. */
    sheet = css_parse_sheet("div { color: red }", NULL);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_set_origin(sheet, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(fixture.context,
                                                      ROBO_CSS_ORIGIN_AUTHOR, sheet, NULL),
                      ROBO_OK);

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(cascade.rules_scanned, 1u);
    ROBO_CHECK_EQ_INT(cascade.rules_matched, 0u);
    ROBO_CHECK_EQ_INT(cascade.rules_without_selectors, 1u);
    ROBO_CHECK_EQ_INT(cascade.unsupported, 1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 0u);
    robo_css_cascade_dispose(&cascade);

    /* …and the same sheet with its selectors parsed applies normally, which is
     * what makes the count above a statement about the input rather than a
     * permanent limitation. */
    {
        robo_css_error_t rule_error = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_style_rule_parse_selectors(
                              (robo_css_style_rule_t *)robo_css_rule_style(
                                  robo_css_stylesheet_rule(sheet, 0u)),
                              robo_css_stylesheet_source(sheet), &rule_error, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_error_is_error(&rule_error), 0);
    }
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(cascade.rules_without_selectors, 0u);
    ROBO_CHECK_EQ_INT(cascade.unsupported, 0);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 1u);
    robo_css_cascade_dispose(&cascade);

    css_cascade_end(&fixture);
}

static void test_css_cascade_collect_at_rules_contribute_nothing(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    /* Nothing evaluates `@media`, so its block cannot contribute — but the rules
     * inside it *would* apply in a complete build, so the block is counted and
     * the answer is marked incomplete. `@charset` hides nothing, so it is counted
     * without making the answer a lower bound. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "@charset \"utf-8\";\n"
                      "@media screen { div { color: red } }\n"
                      "div { color: blue }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(cascade.at_rules_skipped, 2u);
    ROBO_CHECK_EQ_INT(cascade.at_rules_with_blocks, 1u);
    ROBO_CHECK_EQ_INT(cascade.rules_scanned, 1u);
    ROBO_CHECK_EQ_INT(cascade.rules_matched, 1u);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 1u);
    ROBO_CHECK_EQ_INT(cascade.unsupported, 1);
    /* And which declaration was collected proves the block's was not: the only
     * candidate is the top-level rule's. */
    ROBO_CHECK_EQ_INT(cascade.items[0].declaration_index, 0u);

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_collect_undecided_selector_is_reported(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    /* `:hover` parses and matches nothing *decidedly*, so the rule is a plain
     * non-match; `:hover` combined with something that does hold leaves the whole
     * rule undecided, and that must not be counted as a negative. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div:hover { color: hover }\n"
                      "div { color: plain }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(cascade.rules_scanned, 2u);
    ROBO_CHECK_EQ_INT(cascade.rules_matched, 1u);
    ROBO_CHECK_EQ_INT(cascade.rules_undecided, 1u);
    ROBO_CHECK_EQ_INT(cascade.unsupported, 1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 1u);
    ROBO_CHECK_EQ_INT(cascade.items[0].selector_index, 0u);

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_collect_without_a_host(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: red }");
    /* A context with sheets and no host can answer nothing at all — every rule
     * that has selectors is undecided rather than non-matching. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_element_host(fixture.context, NULL, NULL),
                      ROBO_OK);

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(cascade.rules_scanned, 1u);
    ROBO_CHECK_EQ_INT(cascade.rules_undecided, 1u);
    ROBO_CHECK_EQ_INT(cascade.unsupported, 1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 0u);

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_collect_destroyed_sheet(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    sheet = css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: red }");
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    /* A context refuses a dead id when it is added, so this is only reachable by
     * destroying a sheet the context still references — and the honest answer is
     * that the sheet is gone, not that it declared nothing. */
    ROBO_CHECK_EQ_INT(cascade.sheets_unavailable, 1u);
    ROBO_CHECK_EQ_INT(cascade.sheets_scanned, 0u);
    ROBO_CHECK_EQ_INT(cascade.unsupported, 1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 0u);

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_collect_origin_mismatch(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    /* The sheet records AUTHOR (that is how css_parse_sheet builds it) and the
     * context lists it as USER. The context's list is what places a sheet in the
     * cascade, so the level follows the list — and the disagreement is counted
     * rather than smoothed over. */
    sheet = css_parse_sheet("div { color: red }", NULL);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_origin(sheet), ROBO_CSS_ORIGIN_AUTHOR);
    {
        robo_css_error_t rule_error = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_style_rule_parse_selectors(
                              (robo_css_style_rule_t *)robo_css_rule_style(
                                  robo_css_stylesheet_rule(sheet, 0u)),
                              robo_css_stylesheet_source(sheet), &rule_error, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_error_is_error(&rule_error), 0);
    }
    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(fixture.context,
                                                      ROBO_CSS_ORIGIN_USER, sheet, NULL),
                      ROBO_OK);

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(cascade.sheets_origin_mismatch, 1u);
    ROBO_CHECK_EQ_INT(cascade.sheets_scanned, 1u);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 1u);
    ROBO_CHECK_EQ_INT(cascade.items[0].origin, ROBO_CSS_ORIGIN_USER);
    ROBO_CHECK_EQ_INT(cascade.items[0].level, ROBO_CSS_CASCADE_LEVEL_USER);
    /* A mismatch is a caller mistake, not an incomplete answer. */
    ROBO_CHECK_EQ_INT(cascade.unsupported, 0);

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

static void test_css_cascade_collect_replaces_and_releases(void)
{
    css_cascade_fixture_t fixture;
    css_inline_stub_t stub;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    int we_enabled;
    size_t baseline;

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: red; padding: 4px }");
    css_cascade_inline(&fixture, &stub, "color: inline; margin: 1px");

    we_enabled = css_allocator_ensure_enabled();
    baseline = robo_mem_debug_live_count();
    {
        int round;

        for (round = 0; round < 8; round++) {
            /* Collecting into a record that already holds a cascade replaces it:
             * the counters start from zero and the arrays are reused, because
             * the collection begins with an init. */
            css_cascade_collect(&fixture, 1u, &cascade, NULL);
            ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 4u);
            ROBO_CHECK_EQ_INT(cascade.sheets_scanned, 1u);
            ROBO_CHECK_EQ_INT(cascade.rules_matched, 1u);
            ROBO_CHECK_EQ_INT(cascade.inline_present, 1);
            robo_css_cascade_dispose(&cascade);
            ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
        }
        /* And one last collection, disposed by the caller below, proves the
         * inline declaration list is released with the rest. */
        css_cascade_collect(&fixture, 1u, &cascade, NULL);
        ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count() > (int)baseline, 1);
        robo_css_cascade_dispose(&cascade);
    }
    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    /* Idempotent: disposing twice releases nothing twice. */
    robo_css_cascade_dispose(&cascade);
    css_allocator_restore(we_enabled);

    css_cascade_end(&fixture);
}

void robo_test_register_css_cascade_collect(void);

void robo_test_register_css_cascade_collect(void)
{
    robo_test_register("css.cascade.collect.matches_only_the_element",
                       test_css_cascade_collect_matches_only_the_element);
    robo_test_register("css.cascade.collect.order_across_origins",
                       test_css_cascade_collect_order_across_origins);
    robo_test_register("css.cascade.collect.important_reorders",
                       test_css_cascade_collect_important_reorders);
    robo_test_register("css.cascade.collect.two_sheets_in_one_origin",
                       test_css_cascade_collect_two_sheets_in_one_origin);
    robo_test_register("css.cascade.collect.inline_is_last_and_strong",
                       test_css_cascade_collect_inline_is_last_and_strong);
    robo_test_register("css.cascade.collect.inline_absent_and_empty",
                       test_css_cascade_collect_inline_absent_and_empty);
    robo_test_register("css.cascade.collect.inline_problem_is_reported_not_fatal",
                       test_css_cascade_collect_inline_problem_is_reported_not_fatal);
    robo_test_register("css.cascade.collect.inline_host_failure_incomplete",
                       test_css_cascade_collect_inline_host_failure_incomplete);
    robo_test_register("css.cascade.collect.rule_without_selectors",
                       test_css_cascade_collect_rule_without_selectors);
    robo_test_register("css.cascade.collect.at_rules_contribute_nothing",
                       test_css_cascade_collect_at_rules_contribute_nothing);
    robo_test_register("css.cascade.collect.undecided_selector_is_reported",
                       test_css_cascade_collect_undecided_selector_is_reported);
    robo_test_register("css.cascade.collect.without_a_host",
                       test_css_cascade_collect_without_a_host);
    robo_test_register("css.cascade.collect.destroyed_sheet",
                       test_css_cascade_collect_destroyed_sheet);
    robo_test_register("css.cascade.collect.origin_mismatch",
                       test_css_cascade_collect_origin_mismatch);
    robo_test_register("css.cascade.collect.replaces_and_releases",
                       test_css_cascade_collect_replaces_and_releases);
}
