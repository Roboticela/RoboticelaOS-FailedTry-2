/*
 * RoboCSS specificity in the cascade (Phase 9.6) — css.specificity.cascade.*
 *
 * The unit suite proves a specificity is computed correctly. This one proves the
 * cascade *uses* it, which is a different claim: 9.5 shipped a working cascade
 * that reported every same-level contest as undecided, and the way that defect
 * could survive 9.6 is a cascade that still never consulted the new value.
 *
 * So every test here is written as a case where a rule that would have won on
 * document order loses on specificity, or vice versa — never as a case where
 * both answers agree. Where the ordering keys could be confused for one another
 * (level vs specificity, specificity vs document order) there is a test that
 * pins which one won.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Specificity decides, and document order does not                     */
/* ------------------------------------------------------------------ */

static void test_css_specificity_cascade_decides(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    /* The type rule first, so "the later one wins" and "the more specific one
     * wins" give different answers. */
    (void)css_host_add(&fixture.host, "p", NULL, "title");
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "p { color: red }\n.title { color: blue }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 2u);
    /* The candidates carry the two specificities the sheet implies. */
    css_specificity_is(cascade.items[0].specificity, 0u, 0u, 1u);
    css_specificity_is(cascade.items[1].specificity, 0u, 1u, 0u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "blue");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_specificity_cascade_order_is_irrelevant(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "p", NULL, "title");
    css_host_seal(&fixture.host);
    /* The same two rules the other way round. If the answer moved with the rule
     * order, it was document order deciding and not specificity. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      ".title { color: blue }\np { color: red }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "blue");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_specificity_cascade_source_order_after_specificity(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, "a b");
    css_host_seal(&fixture.host);
    /* Equal specificity, so document order is the only thing left — and it is
     * the *last* rule that must win, not the first. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      ".a { color: first }\n.b { color: last }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 2u);
    ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&cascade.items[0].specificity,
                                                 &cascade.items[1].specificity),
                      1);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "last");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Level before specificity, and importance inside a level              */
/* ------------------------------------------------------------------ */

static void test_css_specificity_cascade_level_outranks_specificity(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", "x", NULL);
    css_host_seal(&fixture.host);
    /* A user `#x` is far more specific than an author `div`, and it still loses:
     * an author-normal declaration beats a user-normal one, so the level table
     * is consulted first and no specificity can climb out of a weaker origin. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_USER, "#x { color: user-id }");
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "div { color: author-type }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    /* Ascending level: user (1) then author (2). */
    ROBO_CHECK_EQ_INT(cascade.items[0].origin, ROBO_CSS_ORIGIN_USER);
    css_specificity_is(cascade.items[0].specificity, 1u, 0u, 0u);
    ROBO_CHECK_EQ_INT(cascade.items[1].origin, ROBO_CSS_ORIGIN_AUTHOR);
    css_specificity_is(cascade.items[1].specificity, 0u, 0u, 1u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "author-type");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_specificity_cascade_important(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();

    /* normal vs normal — the more specific wins. */
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", "x", "y");
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#x { color: by-id }\n.y { color: by-class }");
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "by-id");
    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);

    /* normal vs important — importance wins over a higher specificity. */
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", "x", "y");
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#x { color: normal-id }\n.y { color: important-class !important }");
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "important-class");
    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);

    /* important vs important with different specificity — specificity is back
     * in charge, because the two are at the same level again. */
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", "x", "y");
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      ".y { color: important-class !important }\n"
                      "#x { color: important-id !important }");
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "important-id");
    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);

    /* important vs important with equal specificity — document order is the
     * last key, and it is still reached. */
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", "x", "y");
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      ".y { color: first !important }\n"
                      ".y { color: second !important }");
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "second");
    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The selector that matched, not the list                              */
/* ------------------------------------------------------------------ */

static void test_css_specificity_cascade_matching_selector_of_a_list(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();

    /* The list holds `div` (0,0,1) and `#main` (1,0,0); both match, so the rule
     * is worth (1,0,0) and beats the class rule — which is what a cascade that
     * took the *first* member's specificity would get wrong. */
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", "main", "winner");
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div, #main { color: from-list }\n.winner { color: from-class }");
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    css_specificity_is(cascade.items[0].specificity, 1u, 0u, 0u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "from-list");
    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);

    /* A member that does *not* match must not raise the rule either: the list's
     * maximum is (1,0,0) here, but the matching member is `div` (0,0,1), so the
     * class rule wins. Using the list's maximum would give "from-list". */
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, "winner");
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#absent, div { color: from-list }\n.winner { color: from-class }");
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    css_specificity_is(cascade.items[0].specificity, 0u, 0u, 1u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "from-class");
    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);

    /* Three members that all match: the greatest wins over the greatest of the
     * competing rule, again not the first and not the last. */
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", "main", "winner");
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      ".winner, div, #main { color: greatest }\n"
                      "#main.winner { color: more }");
    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    css_specificity_is(cascade.items[0].specificity, 1u, 0u, 0u);
    css_specificity_is(cascade.items[1].specificity, 1u, 1u, 0u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "more");
    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_specificity_cascade_not_is_worth_its_argument(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", NULL, "b");
    css_host_seal(&fixture.host);
    /* `:not(#absent)` matches — the element has no such id — and it is worth the
     * id inside it (1,0,0), so it beats the class rule. A cascade that read
     * `:not()` as a plain pseudo-class would pick "from-class". */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      ":not(#absent) { color: from-not }\n.b { color: from-class }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    css_specificity_is(cascade.items[0].specificity, 1u, 0u, 0u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "from-not");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* What specificity must not resurrect                                  */
/* ------------------------------------------------------------------ */

static void test_css_specificity_cascade_cannot_match_a_pseudo_element(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", "x", NULL);
    css_host_seal(&fixture.host);
    /* `div#x::before` is a high-specificity selector that matches no element —
     * generated content does not exist yet — so it contributes nothing at all.
     * Specificity is consulted only for rules that matched. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div#x::before { color: generated }\ndiv { color: plain }");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(cascade.rules_matched, 1u);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 1u);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "plain");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

static void test_css_specificity_cascade_inline_level_still_wins(void)
{
    css_cascade_fixture_t fixture;
    css_inline_stub_t stub;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    char text[32];

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", "x", NULL);
    css_host_seal(&fixture.host);
    /* The most specific selector there is, against a zero-specificity inline
     * declaration: the inline one wins, because they are not at the same level.
     * The zero specificity is a fact about the *selector* the declaration has
     * (none), not about its strength. */
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div#x { color: from-id }");
    css_cascade_inline(&fixture, &stub, "color: from-inline");

    css_cascade_collect(&fixture, 1u, &cascade, NULL);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 2u);
    /* `div#x` is one type and one id: (1,0,1). */
    css_specificity_is(cascade.items[0].specificity, 1u, 0u, 1u);
    ROBO_CHECK_EQ_INT(robo_css_specificity_is_zero(&cascade.items[1].specificity), 1);
    ROBO_CHECK_EQ_INT(cascade.items[1].level, ROBO_CSS_CASCADE_LEVEL_INLINE);
    ROBO_CHECK_EQ_INT(css_cascade_winner_text(&cascade, "color", text, sizeof(text)),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    ROBO_CHECK_STR_EQ(text, "from-inline");

    robo_css_cascade_dispose(&cascade);
    css_cascade_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                          */
/* ------------------------------------------------------------------ */

static void test_css_specificity_cascade_determinism(void)
{
    css_cascade_fixture_t fixture;
    robo_css_cascade_t first = ROBO_CSS_CLEARED;
    robo_css_cascade_t second = ROBO_CSS_CLEARED;
    char first_text[32];
    char second_text[32];
    size_t round;

    css_fixture();
    css_cascade_begin(&fixture);
    (void)css_host_add(&fixture.host, "div", "x", "a b");
    css_host_seal(&fixture.host);
    css_cascade_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "div { color: t }\n#x .a { color: i }\n.b, div { color: c }\n"
                      ".a { color: a }\n#x { color: d }");

    for (round = 0u; round < 4u; round++) {
        size_t index;

        css_cascade_collect(&fixture, 1u, &first, NULL);
        css_cascade_collect(&fixture, 1u, &second, NULL);
        ROBO_CHECK_EQ_INT(robo_css_cascade_count(&first),
                          robo_css_cascade_count(&second));
        for (index = 0u; index < robo_css_cascade_count(&first); index++) {
            const robo_css_cascade_candidate_t *a = robo_css_cascade_at(&first, index);
            const robo_css_cascade_candidate_t *b = robo_css_cascade_at(&second, index);

            ROBO_CHECK_EQ_INT(a->order, b->order);
            ROBO_CHECK_EQ_INT(a->level, b->level);
            ROBO_CHECK_EQ_INT(a->important, b->important);
            ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&a->specificity, &b->specificity),
                              1);
        }
        ROBO_CHECK_EQ_INT(css_cascade_winner_text(&first, "color", first_text,
                                                  sizeof(first_text)),
                          css_cascade_winner_text(&second, "color", second_text,
                                                  sizeof(second_text)));
        ROBO_CHECK_STR_EQ(first_text, second_text);
        robo_css_cascade_dispose(&first);
        robo_css_cascade_dispose(&second);
    }
    /* The winner is the id rule, which is the only decisive one here. */
    ROBO_CHECK_STR_EQ(first_text, "d");

    css_cascade_end(&fixture);
}

void robo_test_register_css_specificity_cascade(void);

void robo_test_register_css_specificity_cascade(void)
{
    robo_test_register("css.specificity.cascade.decides",
                       test_css_specificity_cascade_decides);
    robo_test_register("css.specificity.cascade.order_is_irrelevant",
                       test_css_specificity_cascade_order_is_irrelevant);
    robo_test_register("css.specificity.cascade.source_order_after_specificity",
                       test_css_specificity_cascade_source_order_after_specificity);
    robo_test_register("css.specificity.cascade.level_outranks_specificity",
                       test_css_specificity_cascade_level_outranks_specificity);
    robo_test_register("css.specificity.cascade.important",
                       test_css_specificity_cascade_important);
    robo_test_register("css.specificity.cascade.matching_selector_of_a_list",
                       test_css_specificity_cascade_matching_selector_of_a_list);
    robo_test_register("css.specificity.cascade.not_is_worth_its_argument",
                       test_css_specificity_cascade_not_is_worth_its_argument);
    robo_test_register("css.specificity.cascade.cannot_match_a_pseudo_element",
                       test_css_specificity_cascade_cannot_match_a_pseudo_element);
    robo_test_register("css.specificity.cascade.inline_level_still_wins",
                       test_css_specificity_cascade_inline_level_still_wins);
    robo_test_register("css.specificity.cascade.determinism",
                       test_css_specificity_cascade_determinism);
}
