/*
 * RoboCSS cascade resolution (Phase 9.5) — css.cascade.resolve.*
 *
 * Resolution is the one place in this phase where a wrong answer would be
 * invisible: everything it decides is a single candidate, and every mistake
 * looks like a plausible declaration. So the tests are written around the
 * *distinction* the phase is built on — decided versus undecided — and around
 * the cases where that distinction flips.
 *
 * The interesting group is the middle one. Two candidates at one level from two
 * selectors are undecided (specificity is 9.6). Two from the *same* selector are
 * decided by document order, because a specificity is a property of a selector
 * and there is only one of them. And an undecided pair stops being undecided the
 * moment a stronger level joins in, which is the case a one-pass resolver gets
 * wrong by remembering the first verdict it reached.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* The three verdicts                                                  */
/* ------------------------------------------------------------------ */

static void test_css_cascade_resolve_nothing_declared(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    const robo_css_cascade_candidate_t *winner = NULL;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: red }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "padding", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_NONE);
    ROBO_CHECK_STR_EQ(text, "");
    ROBO_CHECK_EQ_INT(robo_css_cascade_resolve_name(
                          &cascade, robo_css_slice_from_text("padding"), &winner),
                      ROBO_CSS_CASCADE_VERDICT_NONE);
    ROBO_CHECK(winner == NULL);
    /* Asking only the question is legal, and so is asking nothing at all. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_resolve_name(
                          &cascade, robo_css_slice_from_text("color"), NULL),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_EQ_INT(robo_css_cascade_resolve_name(&cascade, robo_css_slice_empty(), NULL),
                      ROBO_CSS_CASCADE_VERDICT_NONE);
    ROBO_CHECK_EQ_INT(robo_css_cascade_resolve_name(NULL, robo_css_slice_from_text("color"),
                                                   NULL),
                      ROBO_CSS_CASCADE_VERDICT_NONE);

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_resolve_single_candidate_is_decided(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: red }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "red");
    /* Property names are ASCII case-insensitive, so the query need not respell
     * what the author wrote. */
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "COLOR", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "red");
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color ", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_NONE);
    /* A name that merely starts the same way is not the same name. */
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "colo", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_NONE);

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* One selector: document order decides                                */
/* ------------------------------------------------------------------ */

static void test_css_cascade_resolve_one_rule_takes_the_last(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    /* CSS keeps both declarations, and the later one wins — which is the one
     * case inside a level that needs no specificity to settle, because both
     * declarations share the selector that carries it. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: one; color: two }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 2u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "two");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Same level, different selectors: not this phase's question           */
/* ------------------------------------------------------------------ */

static void test_css_cascade_resolve_same_level_rivals_are_undecided(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    const robo_css_cascade_candidate_t *winner = NULL;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, "app");
    css_host_seal(&fixture.host);
    /* `.app` and `div` both match, at the same level, with different
     * specificities — a class selector beats a type selector, which is exactly
     * what 9.6 will compute and what this phase refuses to invent. Answering
     * "the later one wins" would be right only by coincidence. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: bare }\n.app { color: classy }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 2u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY);
    ROBO_CHECK_STR_EQ(text, "");
    /* Nothing is written out for an undecided answer, so a caller cannot read a
     * winner that was not decided. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_resolve_name(
                          &cascade, robo_css_slice_from_text("color"), &winner),
                      ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY);
    ROBO_CHECK(winner == NULL);

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_resolve_undecided_pair_is_order_independent(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    char text[32];

    css_fixture();

    /* The same shape twice, with the repeated declaration on either side of the
     * rival. The answers must match, which is what pins the resolver to "one
     * undecided rival is enough" rather than to whichever declaration it happens
     * to meet last. */
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, "app");
    css_host_seal(&fixture.host);
    sheet = css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                              "div { color: a1; color: a2 }\n.app { color: b }");
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 3u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY);
    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);

    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, "app");
    css_host_seal(&fixture.host);
    sheet = css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                              "div { color: a }\n.app { color: b1; color: b2 }");
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 3u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY);
    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* A stronger level decides                                            */
/* ------------------------------------------------------------------ */

static void test_css_cascade_resolve_stronger_level_decides(void)
{
    css_cascade_fixture_t fixture;
    css_inline_stub_t stub;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, "app");
    css_host_seal(&fixture.host);
    /* An undecided pair at the author level, and one inline declaration above
     * it: the inline one wins outright, so the unresolved author pair never
     * mattered. A resolver that kept the first verdict it reached would report
     * undecided here and be wrong. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: bare }\n.app { color: classy }");
    css_cascade_inline(&fixture, &stub, "color: inline");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 3u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "inline");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_resolve_important_beats_normal(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    /* `!important` is a level, not a tie-break: the important declaration wins
     * even though the normal one comes from a selector that would outrank it in
     * 9.6, which is the whole point of importance. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: plain !important }\n"
                      ".outranks { color: classy }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 1u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "plain");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_resolve_origins_and_importance(void)
{
    css_cascade_fixture_t fixture;
    css_inline_stub_t stub;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    /* One sheet per origin, each declaring the same property, and the four
     * combinations that decide precedence once importance is in play. The point
     * is not that a winner exists but *which* one — a table with two rows
     * swapped still names a winner. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_RUNTIME,
                      "div { color: runtime !important }");
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_USER,
                      "div { color: user !important }");
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: author !important }");
    css_cascade_inline(&fixture, &stub, "color: inline !important");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 4u);
    /* Ascending level: author, inline, user, runtime. */
    ROBO_CHECK_EQ_INT(cascade.items[0].origin, ROBO_CSS_ORIGIN_AUTHOR);
    ROBO_CHECK_EQ_INT(cascade.items[1].origin, ROBO_CSS_ORIGIN_INLINE);
    ROBO_CHECK_EQ_INT(cascade.items[2].origin, ROBO_CSS_ORIGIN_USER);
    ROBO_CHECK_EQ_INT(cascade.items[3].origin, ROBO_CSS_ORIGIN_RUNTIME);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "runtime");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_cascade_resolve_inline_and_user_important(void)
{
    css_cascade_fixture_t fixture;
    css_inline_stub_t stub;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    /* The two cases a plain "important reverses the origin order" rule gets
     * wrong, asserted separately so a table edit cannot satisfy one by breaking
     * the other: an inline `!important` beats an author `!important` (both are
     * author-level), and a user `!important` beats the inline one. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: author !important }");
    css_cascade_inline(&fixture, &stub, "color: inline !important");
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "inline");
    robo_css_cascade_dispose(&cascade);

    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_USER, "div { color: user !important }");
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "user");
    robo_css_cascade_dispose(&cascade);

    css_cascade_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Identity: by name, and by property id                               */
/* ------------------------------------------------------------------ */

static void test_css_cascade_resolve_by_property_id(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_property_table_t *table = NULL;
    robo_css_property_id_t color = ROBO_CSS_PROPERTY_ID_NONE;
    robo_css_property_id_t padding = ROBO_CSS_PROPERTY_ID_NONE;
    const robo_css_cascade_candidate_t *winner = NULL;
    char text[32];

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_table_create(&table, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "color",
                                                      ROBO_CSS_PROPERTY_FLAG_INHERITED,
                                                      &color, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "padding", 0u, &padding,
                                                      NULL),
                      ROBO_OK);

    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_property_table(fixture.context, table,
                                                                NULL),
                      ROBO_OK);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: red; font-weight: bold }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 2u);
    /* The context's table is what turns a name into an identity, and a name the
     * table never heard of keeps NONE — which is a result, not an error. */
    ROBO_CHECK_EQ_INT(cascade.items[0].property, color);
    ROBO_CHECK_EQ_INT(cascade.items[1].property, ROBO_CSS_PROPERTY_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count_of(&cascade, color), 1u);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count_of(&cascade, padding), 0u);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count_of(&cascade, ROBO_CSS_PROPERTY_ID_NONE), 0u);
    ROBO_CHECK_EQ_INT(robo_css_cascade_resolve_property(&cascade, color, &winner),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK(winner != NULL);
    ROBO_CHECK_EQ_INT(winner->property, color);
    ROBO_CHECK_EQ_INT(robo_css_cascade_resolve_property(&cascade, padding, &winner),
                      ROBO_CSS_CASCADE_VERDICT_NONE);
    ROBO_CHECK(winner == NULL);
    /* An identity of NONE is not a property: every unresolved declaration would
     * answer to it, so the question is refused rather than answered wrongly. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_resolve_property(&cascade,
                                                        ROBO_CSS_PROPERTY_ID_NONE, NULL),
                      ROBO_CSS_CASCADE_VERDICT_NONE);
    ROBO_CHECK_EQ_INT(robo_css_cascade_resolve_property(NULL, color, NULL),
                      ROBO_CSS_CASCADE_VERDICT_NONE);
    /* A query with no table still works by name: the two identities are asked
     * about separately, and neither falls back on the other. */
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "font-weight", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "bold");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
    robo_css_property_table_destroy(table);
}

static void test_css_cascade_resolve_provenance_identity(void)
{
    css_cascade_fixture_t fixture;
    css_inline_stub_t stub;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, NULL);
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: one; padding: 2px }");
    css_cascade_inline(&fixture, &stub, "color: i1; margin: 3px");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 4u);
    /* Two declarations of one rule: same selector, so same specificity. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_candidate_same_provenance(
                          robo_css_cascade_at(&cascade, 0u),
                          robo_css_cascade_at(&cascade, 1u)),
                      1);
    /* A rule and the style attribute are not comparable. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_candidate_same_provenance(
                          robo_css_cascade_at(&cascade, 0u),
                          robo_css_cascade_at(&cascade, 2u)),
                      0);
    /* Two inline declarations are one attribute and one implicit selector. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_candidate_same_provenance(
                          robo_css_cascade_at(&cascade, 2u),
                          robo_css_cascade_at(&cascade, 3u)),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_candidate_same_provenance(NULL,
                                                                robo_css_cascade_at(&cascade,
                                                                                    0u)),
                      0);

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Two properties, two answers                                         */
/* ------------------------------------------------------------------ */

static void test_css_cascade_resolve_is_per_property(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, "app");
    css_host_seal(&fixture.host);
    /* One property is contested and one is not, in the same cascade: a resolver
     * that reported a single verdict for the whole element would have to be
     * wrong about one of them. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: bare; padding: 1px }\n"
                      ".app { color: classy; padding: 2px }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "padding", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY);
    /*…and the same cascade with one of them made important is decided for that
     * one alone, which is what "per property" has to mean. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_USER,
                      "div { color: user !important }");
    robo_css_cascade_dispose(&cascade);
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "user");
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "padding", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY);

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

void robo_test_register_css_cascade_resolve(void);

void robo_test_register_css_cascade_resolve(void)
{
    robo_test_register("css.cascade.resolve.nothing_declared",
                       test_css_cascade_resolve_nothing_declared);
    robo_test_register("css.cascade.resolve.single_candidate_is_decided",
                       test_css_cascade_resolve_single_candidate_is_decided);
    robo_test_register("css.cascade.resolve.one_rule_takes_the_last",
                       test_css_cascade_resolve_one_rule_takes_the_last);
    robo_test_register("css.cascade.resolve.same_level_rivals_are_undecided",
                       test_css_cascade_resolve_same_level_rivals_are_undecided);
    robo_test_register("css.cascade.resolve.undecided_pair_is_order_independent",
                       test_css_cascade_resolve_undecided_pair_is_order_independent);
    robo_test_register("css.cascade.resolve.stronger_level_decides",
                       test_css_cascade_resolve_stronger_level_decides);
    robo_test_register("css.cascade.resolve.important_beats_normal",
                       test_css_cascade_resolve_important_beats_normal);
    robo_test_register("css.cascade.resolve.origins_and_importance",
                       test_css_cascade_resolve_origins_and_importance);
    robo_test_register("css.cascade.resolve.inline_and_user_important",
                       test_css_cascade_resolve_inline_and_user_important);
    robo_test_register("css.cascade.resolve.by_property_id",
                       test_css_cascade_resolve_by_property_id);
    robo_test_register("css.cascade.resolve.provenance_identity",
                       test_css_cascade_resolve_provenance_identity);
    robo_test_register("css.cascade.resolve.is_per_property",
                       test_css_cascade_resolve_is_per_property);
}
