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

static void TestCssSpecificityCascadeDecides(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    /* The type rule first, so "the later one wins" and "the more specific one
     * wins" give different answers. */
    (void)CssHostAdd(&fixture.host, "p", NULL, "title");
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "p { color: red }\n.title { color: blue }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 2u);
    /* The candidates carry the two specificities the sheet implies. */
    CssSpecificityIs(cascade.items[0].specificity, 0u, 0u, 1u);
    CssSpecificityIs(cascade.items[1].specificity, 0u, 1u, 0u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "blue");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssSpecificityCascadeOrderIsIrrelevant(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "p", NULL, "title");
    CssHostSeal(&fixture.host);
    /* The same two rules the other way round. If the answer moved with the rule
     * order, it was document order deciding and not specificity. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      ".title { color: blue }\np { color: red }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "blue");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssSpecificityCascadeSourceOrderAfterSpecificity(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, "a b");
    CssHostSeal(&fixture.host);
    /* Equal specificity, so document order is the only thing left — and it is
     * the *last* rule that must win, not the first. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      ".a { color: first }\n.b { color: last }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 2u);
    RoboCheckEqInt(CssSpecificityEqual(&cascade.items[0].specificity,
                                                 &cascade.items[1].specificity),
                      1);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "last");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Level before specificity, and importance inside a level              */
/* ------------------------------------------------------------------ */

static void TestCssSpecificityCascadeLevelOutranksSpecificity(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", "x", NULL);
    CssHostSeal(&fixture.host);
    /* A user `#x` is far more specific than an author `div`, and it still loses:
     * an author-normal declaration beats a user-normal one, so the level table
     * is consulted first and no specificity can climb out of a weaker origin. */
    CssCascadeSheet(&fixture, RoboCssOriginUser, "#x { color: user-id }");
    CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: author-type }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    /* Ascending level: user (1) then author (2). */
    RoboCheckEqInt(cascade.items[0].origin, RoboCssOriginUser);
    CssSpecificityIs(cascade.items[0].specificity, 1u, 0u, 0u);
    RoboCheckEqInt(cascade.items[1].origin, RoboCssOriginAuthor);
    CssSpecificityIs(cascade.items[1].specificity, 0u, 0u, 1u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "author-type");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssSpecificityCascadeImportant(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();

    /* normal vs normal — the more specific wins. */
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", "x", "y");
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "#x { color: by-id }\n.y { color: by-class }");
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "by-id");
    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);

    /* normal vs important — importance wins over a higher specificity. */
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", "x", "y");
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "#x { color: normal-id }\n.y { color: important-class !important }");
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "important-class");
    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);

    /* important vs important with different specificity — specificity is back
     * in charge, because the two are at the same level again. */
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", "x", "y");
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      ".y { color: important-class !important }\n"
                      "#x { color: important-id !important }");
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "important-id");
    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);

    /* important vs important with equal specificity — document order is the
     * last key, and it is still reached. */
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", "x", "y");
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      ".y { color: first !important }\n"
                      ".y { color: second !important }");
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "second");
    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The selector that matched, not the list                              */
/* ------------------------------------------------------------------ */

static void TestCssSpecificityCascadeMatchingSelectorOfAList(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();

    /* The list holds `div` (0,0,1) and `#main` (1,0,0); both match, so the rule
     * is worth (1,0,0) and beats the class rule — which is what a cascade that
     * took the *first* member's specificity would get wrong. */
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", "main", "winner");
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div, #main { color: from-list }\n.winner { color: from-class }");
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    CssSpecificityIs(cascade.items[0].specificity, 1u, 0u, 0u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "from-list");
    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);

    /* A member that does *not* match must not raise the rule either: the list's
     * maximum is (1,0,0) here, but the matching member is `div` (0,0,1), so the
     * class rule wins. Using the list's maximum would give "from-list". */
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, "winner");
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "#absent, div { color: from-list }\n.winner { color: from-class }");
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    CssSpecificityIs(cascade.items[0].specificity, 0u, 0u, 1u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "from-class");
    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);

    /* Three members that all match: the greatest wins over the greatest of the
     * competing rule, again not the first and not the last. */
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", "main", "winner");
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      ".winner, div, #main { color: greatest }\n"
                      "#main.winner { color: more }");
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    CssSpecificityIs(cascade.items[0].specificity, 1u, 0u, 0u);
    CssSpecificityIs(cascade.items[1].specificity, 1u, 1u, 0u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "more");
    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssSpecificityCascadeNotIsWorthItsArgument(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, "b");
    CssHostSeal(&fixture.host);
    /* `:not(#absent)` matches — the element has no such id — and it is worth the
     * id inside it (1,0,0), so it beats the class rule. A cascade that read
     * `:not()` as a plain pseudo-class would pick "from-class". */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      ":not(#absent) { color: from-not }\n.b { color: from-class }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    CssSpecificityIs(cascade.items[0].specificity, 1u, 0u, 0u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "from-not");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* What specificity must not resurrect                                  */
/* ------------------------------------------------------------------ */

static void TestCssSpecificityCascadeCannotMatchAPseudoElement(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", "x", NULL);
    CssHostSeal(&fixture.host);
    /* `div#x::before` is a high-specificity selector that matches no element —
     * generated content does not exist yet — so it contributes nothing at all.
     * Specificity is consulted only for rules that matched. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div#x::before { color: generated }\ndiv { color: plain }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(cascade.rulesMatched, 1u);
    RoboCheckEqInt(CssCascadeCount(&cascade), 1u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "plain");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssSpecificityCascadeInlineLevelStillWins(void)
{
    CssCascadeFixture fixture;
    CssInlineStub stub;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", "x", NULL);
    CssHostSeal(&fixture.host);
    /* The most specific selector there is, against a zero-specificity inline
     * declaration: the inline one wins, because they are not at the same level.
     * The zero specificity is a fact about the *selector* the declaration has
     * (none), not about its strength. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div#x { color: from-id }");
    CssCascadeInline(&fixture, &stub, "color: from-inline");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 2u);
    /* `div#x` is one type and one id: (1,0,1). */
    CssSpecificityIs(cascade.items[0].specificity, 1u, 0u, 1u);
    RoboCheckEqInt(CssSpecificityIsZero(&cascade.items[1].specificity), 1);
    RoboCheckEqInt(cascade.items[1].level, RoboCssCascadeLevelInline);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "from-inline");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                          */
/* ------------------------------------------------------------------ */

static void TestCssSpecificityCascadeDeterminism(void)
{
    CssCascadeFixture fixture;
    CssCascade first = RoboCssCleared;
    CssCascade second = RoboCssCleared;
    char firstText[32];
    char secondText[32];
    size_t round;

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", "x", "a b");
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: t }\n#x .a { color: i }\n.b, div { color: c }\n"
                      ".a { color: a }\n#x { color: d }");

    for (round = 0u; round < 4u; round++) {
        size_t index;

        RoboCssCascadeCollect(&fixture, 1u, &first, NULL);
        RoboCssCascadeCollect(&fixture, 1u, &second, NULL);
        RoboCheckEqInt(CssCascadeCount(&first),
                          CssCascadeCount(&second));
        for (index = 0u; index < CssCascadeCount(&first); index++) {
            const CssCascadeCandidate *a = CssCascadeAt(&first, index);
            const CssCascadeCandidate *b = CssCascadeAt(&second, index);

            RoboCheckEqInt(a->order, b->order);
            RoboCheckEqInt(a->level, b->level);
            RoboCheckEqInt(a->important, b->important);
            RoboCheckEqInt(CssSpecificityEqual(&a->specificity, &b->specificity),
                              1);
        }
        RoboCheckEqInt(CssCascadeWinnerText(&first, "color", firstText,
                                                  sizeof(firstText)),
                          CssCascadeWinnerText(&second, "color", secondText,
                                                  sizeof(secondText)));
        RoboCheckStrEq(firstText, secondText);
        CssCascadeDispose(&first);
        CssCascadeDispose(&second);
    }
    /* The winner is the id rule, which is the only decisive one here. */
    RoboCheckStrEq(firstText, "d");

    CssCascadeEnd(&fixture);
}

void TestRegisterCssSpecificityCascade(void);

void TestRegisterCssSpecificityCascade(void)
{
    TestRegister("css.specificity.cascade.decides",
                       TestCssSpecificityCascadeDecides);
    TestRegister("css.specificity.cascade.order_is_irrelevant",
                       TestCssSpecificityCascadeOrderIsIrrelevant);
    TestRegister("css.specificity.cascade.source_order_after_specificity",
                       TestCssSpecificityCascadeSourceOrderAfterSpecificity);
    TestRegister("css.specificity.cascade.level_outranks_specificity",
                       TestCssSpecificityCascadeLevelOutranksSpecificity);
    TestRegister("css.specificity.cascade.important",
                       TestCssSpecificityCascadeImportant);
    TestRegister("css.specificity.cascade.matching_selector_of_a_list",
                       TestCssSpecificityCascadeMatchingSelectorOfAList);
    TestRegister("css.specificity.cascade.not_is_worth_its_argument",
                       TestCssSpecificityCascadeNotIsWorthItsArgument);
    TestRegister("css.specificity.cascade.cannot_match_a_pseudo_element",
                       TestCssSpecificityCascadeCannotMatchAPseudoElement);
    TestRegister("css.specificity.cascade.inline_level_still_wins",
                       TestCssSpecificityCascadeInlineLevelStillWins);
    TestRegister("css.specificity.cascade.determinism",
                       TestCssSpecificityCascadeDeterminism);
}
