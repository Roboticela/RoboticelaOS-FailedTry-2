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

static void TestCssCascadeResolveNothingDeclared(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    const CssCascadeCandidate *winner = NULL;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: red }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "padding", text, sizeof(text)),
                      RoboCssCascadeVerdictNone);
    RoboCheckStrEq(text, "");
    RoboCheckEqInt(CssCascadeResolveName(
                          &cascade, CssSliceFromText("padding"), &winner),
                      RoboCssCascadeVerdictNone);
    RoboCheck(winner == NULL);
    /* Asking only the question is legal, and so is asking nothing at all. */
    RoboCheckEqInt(CssCascadeResolveName(
                          &cascade, CssSliceFromText("color"), NULL),
                      RoboCssCascadeVerdictDecided);
    RoboCheckEqInt(CssCascadeResolveName(&cascade, CssSliceEmpty(), NULL),
                      RoboCssCascadeVerdictNone);
    RoboCheckEqInt(CssCascadeResolveName(NULL, CssSliceFromText("color"),
                                                   NULL),
                      RoboCssCascadeVerdictNone);

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeResolveSingleCandidateIsDecided(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: red }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "red");
    /* Property names are ASCII case-insensitive, so the query need not respell
     * what the author wrote. */
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "COLOR", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "red");
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color ", text, sizeof(text)),
                      RoboCssCascadeVerdictNone);
    /* A name that merely starts the same way is not the same name. */
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "colo", text, sizeof(text)),
                      RoboCssCascadeVerdictNone);

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* One selector: document order decides                                */
/* ------------------------------------------------------------------ */

static void TestCssCascadeResolveOneRuleTakesTheLast(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    /* CSS keeps both declarations, and the later one wins — which is the one
     * case inside a level that needs no specificity to settle, because both
     * declarations share the selector that carries it. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: one; color: two }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 2u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "two");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Same level, different selectors: specificity decides (9.6)           */
/* ------------------------------------------------------------------ */

static void TestCssCascadeResolveSameLevelRivalsAreDecided(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    const CssCascadeCandidate *winner = NULL;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, "app");
    CssHostSeal(&fixture.host);
    /* `.app` and `div` both match, at the same level, with different
     * specificities — a class selector beats a type selector. 9.5 reported this
     * as undecided because it had no specificity to consult; 9.6 supplies one,
     * so the answer is the class rule. The type rule is *later* in document
     * order, which is what makes this a statement about specificity rather than
     * about order. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      ".app { color: classy }\ndiv { color: bare }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 2u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "classy");
    /* A decided answer writes the winner out, and it is the class rule. */
    RoboCheckEqInt(CssCascadeResolveName(
                          &cascade, CssSliceFromText("color"), &winner),
                      RoboCssCascadeVerdictDecided);
    RoboCheck(winner != NULL);
    if (winner) {
        RoboCheckEqInt(winner->ruleIndex, 0u);
        CssSpecificityIs(winner->specificity, 0u, 1u, 0u);
    }

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeResolveSameSelectorTakesTheLast(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    CssStylesheetId sheet;
    char text[32];

    CssFixture();

    /* The same shape twice, with the repeated declaration on either side of the
     * rival. The *specificity* is what decides in both, and it must not move
     * with the order the declarations were written in: the class rule is (0,1,0)
     * and the type rule is (0,0,1), so the class rule wins either way. */
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, "app");
    CssHostSeal(&fixture.host);
    sheet = CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                              "div { color: a1; color: a2 }\n.app { color: b }");
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 3u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "b");
    CssCascadeDispose(&cascade);
    /* Before the fixture goes: the fixture releases what it made, so an explicit
     * destroy has to happen while the sheet is still alive. */
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssCascadeEnd(&fixture);

    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, "app");
    CssHostSeal(&fixture.host);
    sheet = CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                              "div { color: a }\n.app { color: b1; color: b2 }");
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 3u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    /* And within the winning selector, document order still picks the last
     * declaration — the other key, reached after specificity ties. */
    RoboCheckStrEq(text, "b2");
    CssCascadeDispose(&cascade);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    CssCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* A stronger level decides                                            */
/* ------------------------------------------------------------------ */

static void TestCssCascadeResolveStrongerLevelDecides(void)
{
    CssCascadeFixture fixture;
    CssInlineStub stub;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, "app");
    CssHostSeal(&fixture.host);
    /* A contested author pair and one inline declaration above it: the inline
     * one wins outright, because the level is consulted before the specificity
     * of anything below it. A resolver that stopped at the first level it met
     * would pick the class rule and be wrong. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: bare }\n.app { color: classy }");
    CssCascadeInline(&fixture, &stub, "color: inline");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 3u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "inline");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeResolveImportantBeatsNormal(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    /* `!important` is a level, not a tie-break: the important declaration wins
     * even though the normal one comes from a more specific selector, which is
     * the whole point of importance. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: plain !important }\n"
                      ".outranks { color: classy }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 1u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "plain");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeResolveOriginsAndImportance(void)
{
    CssCascadeFixture fixture;
    CssInlineStub stub;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    /* One sheet per origin, each declaring the same property, and the four
     * combinations that decide precedence once importance is in play. The point
     * is not that a winner exists but *which* one — a table with two rows
     * swapped still names a winner. */
    CssCascadeSheet(&fixture, RoboCssOriginRuntime,
                      "div { color: runtime !important }");
    CssCascadeSheet(&fixture, RoboCssOriginUser,
                      "div { color: user !important }");
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: author !important }");
    CssCascadeInline(&fixture, &stub, "color: inline !important");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 4u);
    /* Ascending level: author, inline, user, runtime. */
    RoboCheckEqInt(cascade.items[0].origin, RoboCssOriginAuthor);
    RoboCheckEqInt(cascade.items[1].origin, RoboCssOriginInline);
    RoboCheckEqInt(cascade.items[2].origin, RoboCssOriginUser);
    RoboCheckEqInt(cascade.items[3].origin, RoboCssOriginRuntime);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "runtime");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeResolveInlineAndUserImportant(void)
{
    CssCascadeFixture fixture;
    CssInlineStub stub;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    /* The two cases a plain "important reverses the origin order" rule gets
     * wrong, asserted separately so a table edit cannot satisfy one by breaking
     * the other: an inline `!important` beats an author `!important` (both are
     * author-level), and a user `!important` beats the inline one. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: author !important }");
    CssCascadeInline(&fixture, &stub, "color: inline !important");
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "inline");
    CssCascadeDispose(&cascade);

    CssCascadeSheet(&fixture, RoboCssOriginUser, "div { color: user !important }");
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "user");
    CssCascadeDispose(&cascade);

    CssCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Identity: by name, and by property id                               */
/* ------------------------------------------------------------------ */

static void TestCssCascadeResolveByPropertyId(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    CssPropertyTable *table = NULL;
    CssPropertyId color = RoboCssPropertyIdNone;
    CssPropertyId padding = RoboCssPropertyIdNone;
    const CssCascadeCandidate *winner = NULL;
    char text[32];

    CssFixture();
    RoboCheckEqInt(CssPropertyTableCreate(&table, NULL), RoboOk);
    RoboCheckEqInt(CssPropertyTableRegister(table, "color",
                                                      RoboCssPropertyFlagInherited,
                                                      &color, NULL),
                      RoboOk);
    RoboCheckEqInt(CssPropertyTableRegister(table, "padding", 0u, &padding,
                                                      NULL),
                      RoboOk);

    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    RoboCheckEqInt(CssStyleContextSetPropertyTable(fixture.context, table,
                                                                NULL),
                      RoboOk);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: red; font-weight: bold }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 2u);
    /* The context's table is what turns a name into an identity, and a name the
     * table never heard of keeps NONE — which is a result, not an error. */
    RoboCheckEqInt(cascade.items[0].property, color);
    RoboCheckEqInt(cascade.items[1].property, RoboCssPropertyIdNone);
    RoboCheckEqInt(CssCascadeCountOf(&cascade, color), 1u);
    RoboCheckEqInt(CssCascadeCountOf(&cascade, padding), 0u);
    RoboCheckEqInt(CssCascadeCountOf(&cascade, RoboCssPropertyIdNone), 0u);
    RoboCheckEqInt(CssCascadeResolveProperty(&cascade, color, &winner),
                      RoboCssCascadeVerdictDecided);
    RoboCheck(winner != NULL);
    RoboCheckEqInt(winner->property, color);
    RoboCheckEqInt(CssCascadeResolveProperty(&cascade, padding, &winner),
                      RoboCssCascadeVerdictNone);
    RoboCheck(winner == NULL);
    /* An identity of NONE is not a property: every unresolved declaration would
     * answer to it, so the question is refused rather than answered wrongly. */
    RoboCheckEqInt(CssCascadeResolveProperty(&cascade,
                                                        RoboCssPropertyIdNone, NULL),
                      RoboCssCascadeVerdictNone);
    RoboCheckEqInt(CssCascadeResolveProperty(NULL, color, NULL),
                      RoboCssCascadeVerdictNone);
    /* A query with no table still works by name: the two identities are asked
     * about separately, and neither falls back on the other. */
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "font-weight", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "bold");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
    CssPropertyTableDestroy(table);
}

static void TestCssCascadeResolveSpecificityIsCarried(void)
{
    CssCascadeFixture fixture;
    CssInlineStub stub;
    CssCascade cascade = RoboCssCleared;

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: one; padding: 2px }");
    CssCascadeInline(&fixture, &stub, "color: i1; margin: 3px");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 4u);
    /* Two declarations of one rule carry one specificity, because a specificity
     * is a property of the selector and there is only one of it. */
    CssSpecificityIs(cascade.items[0].specificity, 0u, 0u, 1u);
    CssSpecificityIs(cascade.items[1].specificity, 0u, 0u, 1u);
    RoboCheckEqInt(CssSpecificityEqual(&cascade.items[0].specificity,
                                                 &cascade.items[1].specificity),
                      1);
    /* Inline declarations carry the zero specificity, because their strength is
     * the INLINE level and no selector reaches it. */
    RoboCheckEqInt(CssSpecificityIsZero(&cascade.items[2].specificity), 1);
    RoboCheckEqInt(CssSpecificityIsZero(&cascade.items[3].specificity), 1);
    RoboCheckEqInt(cascade.items[0].level, RoboCssCascadeLevelAuthor);
    RoboCheckEqInt(cascade.items[2].level, RoboCssCascadeLevelInline);

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Two properties, two answers                                         */
/* ------------------------------------------------------------------ */

static void TestCssCascadeResolveIsPerProperty(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, "app");
    CssHostSeal(&fixture.host);
    /* One property is contested and one is not, in the same cascade: a resolver
     * that reported a single verdict for the whole element would have to be
     * wrong about one of them. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: bare; padding: 1px }\n"
                      ".app { color: classy; padding: 2px }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    /* Both properties are contested, and both resolve to the class rule — one
     * answer per property, from one specificity each. */
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "classy");
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "padding", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "2px");
    /* …and the same cascade with one of them made important is decided for that
     * one alone, which is what "per property" has to mean. */
    CssCascadeSheet(&fixture, RoboCssOriginUser,
                      "div { color: user !important }");
    CssCascadeDispose(&cascade);
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "user");
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "padding", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "2px");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

void TestRegisterCssCascadeResolve(void);

void TestRegisterCssCascadeResolve(void)
{
    TestRegister("css.cascade.resolve.nothing_declared",
                       TestCssCascadeResolveNothingDeclared);
    TestRegister("css.cascade.resolve.single_candidate_is_decided",
                       TestCssCascadeResolveSingleCandidateIsDecided);
    TestRegister("css.cascade.resolve.one_rule_takes_the_last",
                       TestCssCascadeResolveOneRuleTakesTheLast);
    TestRegister("css.cascade.resolve.same_level_rivals_are_decided",
                       TestCssCascadeResolveSameLevelRivalsAreDecided);
    TestRegister("css.cascade.resolve.same_selector_takes_the_last",
                       TestCssCascadeResolveSameSelectorTakesTheLast);
    TestRegister("css.cascade.resolve.stronger_level_decides",
                       TestCssCascadeResolveStrongerLevelDecides);
    TestRegister("css.cascade.resolve.important_beats_normal",
                       TestCssCascadeResolveImportantBeatsNormal);
    TestRegister("css.cascade.resolve.origins_and_importance",
                       TestCssCascadeResolveOriginsAndImportance);
    TestRegister("css.cascade.resolve.inline_and_user_important",
                       TestCssCascadeResolveInlineAndUserImportant);
    TestRegister("css.cascade.resolve.by_property_id",
                       TestCssCascadeResolveByPropertyId);
    TestRegister("css.cascade.resolve.specificity_is_carried",
                       TestCssCascadeResolveSpecificityIsCarried);
    TestRegister("css.cascade.resolve.is_per_property",
                       TestCssCascadeResolveIsPerProperty);
}
