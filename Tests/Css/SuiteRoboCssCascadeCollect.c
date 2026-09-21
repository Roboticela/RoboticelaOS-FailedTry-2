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

/* The inline-style stub (CssInlineStub) and the fixture that installs it
 * live in RoboCssTestSupport.h: the resolve suite needs the same seam, and two
 * stubs for one callback interface is exactly the drift the shared fixtures
 * exist to prevent. */

/** The number of candidates declaring @p name, by name. */
static size_t CssCascadeNamedCount(const CssCascade *cascade, const char *name)
{
    CssSlice wanted = CssSliceFromText(name);
    size_t index;
    size_t total = 0u;

    for (index = 0u; index < CssCascadeCount(cascade); index++) {
        const CssCascadeCandidate *candidate = CssCascadeAt(cascade, index);

        if (CssSliceEqualsCi(CssDeclarationName(candidate->declaration),
                                    wanted)) {
            total++;
        }
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* What applies                                                        */
/* ------------------------------------------------------------------ */

static void TestCssCascadeCollectMatchesOnlyTheElement(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssCascadeBegin(&fixture);
    div = CssHostAdd(&fixture.host, "div", "app", NULL);
    paragraph = CssHostAdd(&fixture.host, "p", NULL, NULL);
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: red }\np { color: blue }");

    RoboCssCascadeCollect(&fixture, div, &cascade, NULL);
    /* Both rules were looked at; one matched. A cascade that reported the
     * scanned count as matched would hide a selector bug in plain sight. */
    RoboCheckEqInt(cascade.rulesScanned, 2u);
    RoboCheckEqInt(cascade.rulesMatched, 1u);
    RoboCheckEqInt(cascade.rulesUndecided, 0u);
    RoboCheckEqInt(cascade.unsupported, 0);
    RoboCheckEqInt(CssCascadeCount(&cascade), 1u);
    RoboCheckEqInt(cascade.items[0].selectorIndex, 0u);
    RoboCheckEqInt(cascade.items[0].ruleIndex, 0u);
    RoboCheckEqInt(cascade.items[0].origin, RoboCssOriginAuthor);
    RoboCheckEqInt(cascade.items[0].level, RoboCssCascadeLevelAuthor);
    /* `color` with no property table installed resolves to no identity, and the
     * name is still the author's — which is why both questions exist. */
    RoboCheckEqInt(cascade.items[0].property, RoboCssPropertyIdNone);
    {
        char name[16];

        CssCascadeCandidateName(&cascade, 0u, name, sizeof(name));
        RoboCheckStrEq(name, "color");
        /* An out-of-range index and a NULL buffer are tolerated rather than
         * dereferenced, like every other accessor in the subsystem. */
        RoboCheckEqInt(CssCascadeAt(&cascade, 99u) == NULL, 1);
        CssCascadeCandidateName(&cascade, 99u, name, sizeof(name));
        RoboCheckStrEq(name, "");
        CssCascadeCandidateName(&cascade, 0u, NULL, 0u);
    }
    CssCascadeDispose(&cascade);

    RoboCssCascadeCollect(&fixture, paragraph, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 1u);
    RoboCheckEqInt(cascade.items[0].ruleIndex, 1u);
    CssCascadeDispose(&cascade);

    CssCascadeEnd(&fixture);
}

static void TestCssCascadeCollectOrderAcrossOrigins(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    /* Added weakest last on purpose: the context's lists decide the order, not
     * the order the caller happened to create the sheets in. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: author }");
    CssCascadeSheet(&fixture, RoboCssOriginUser, "div { color: user }");
    CssCascadeSheet(&fixture, RoboCssOriginRuntime, "div { color: runtime }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(cascade.sheetsScanned, 3u);
    RoboCheckEqInt(CssCascadeCount(&cascade), 3u);
    /* Cascade order: ascending level, so runtime first and author last. */
    RoboCheckEqInt(cascade.items[0].origin, RoboCssOriginRuntime);
    RoboCheckEqInt(cascade.items[1].origin, RoboCssOriginUser);
    RoboCheckEqInt(cascade.items[2].origin, RoboCssOriginAuthor);
    RoboCheckEqInt(cascade.items[0].order, 0u);
    RoboCheckEqInt(cascade.items[1].order, 1u);
    RoboCheckEqInt(cascade.items[2].order, 2u);
    RoboCheckEqInt(cascade.items[0].sheetIndex, 0u);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "author");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeCollectImportantReorders(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    /* One rule, one sheet, one selector — the case where document order decides
     * without specificity, and where `!important` has to move the declaration
     * past every normal one in the same block. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: first; color: second !important; color: third }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 3u);
    RoboCheckEqInt(cascade.items[0].important, 0);
    RoboCheckEqInt(cascade.items[1].important, 0);
    RoboCheckEqInt(cascade.items[2].important, 1);
    RoboCheckEqInt(cascade.items[2].level, RoboCssCascadeLevelAuthorImportant);
    /* The two normal ones kept their document order. */
    RoboCheckEqInt(cascade.items[0].declarationIndex, 0u);
    RoboCheckEqInt(cascade.items[1].declarationIndex, 2u);
    RoboCheckEqInt(cascade.items[1].order < cascade.items[0].order, 0);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "second");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeCollectTwoSheetsInOneOrigin(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    /* Within one origin, the context's insertion order is the cascade order —
     * there is nothing else to order two author sheets by without a document
     * position, which is why the list is ordered rather than sorted. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: one }");
    CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: two }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(CssCascadeCount(&cascade), 2u);
    RoboCheckEqInt(cascade.items[0].sheetIndex, 0u);
    RoboCheckEqInt(cascade.items[1].sheetIndex, 1u);
    RoboCheckEqInt(cascade.items[0].level, cascade.items[1].level);
    RoboCheckEqInt(cascade.items[0].sheet != cascade.items[1].sheet, 1);
    /* Two rules of equal specificity at one level: the third key is reached, and
     * it is document order — so the second sheet's declaration wins. The
     * specificities are equal, which is what makes this a statement about sheet
     * order rather than about specificity. */
    RoboCheckEqInt(CssSpecificityEqual(&cascade.items[0].specificity,
                                                 &cascade.items[1].specificity),
                      1);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "two");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The inline style attribute                                          */
/* ------------------------------------------------------------------ */

static void TestCssCascadeCollectInlineIsLastAndStrong(void)
{
    CssCascadeFixture fixture;
    CssInlineStub stub;
    CssCascade cascade = RoboCssCleared;
    char text[32];

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div { color: author; padding: 4px }");
    CssCascadeInline(&fixture, &stub, "color: inline");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(stub.calls, 1);
    RoboCheckEqInt(stub.lastElement, 1u);
    RoboCheckEqInt(cascade.inlineChecked, 1);
    RoboCheckEqInt(cascade.inlinePresent, 1);
    RoboCheckEqInt(cascade.hasInlineError, 0);
    RoboCheckEqInt(CssCascadeCount(&cascade), 3u);
    /* The inline declaration is met last, and it is above the author level. */
    RoboCheckEqInt(cascade.items[2].origin, RoboCssOriginInline);
    RoboCheckEqInt(cascade.items[2].level, RoboCssCascadeLevelInline);
    RoboCheckEqInt(cascade.items[2].sheet, RoboCssStylesheetIdNone);
    RoboCheckEqInt(cascade.items[2].sheetIndex, RoboCssCascadeNoIndex);
    RoboCheckEqInt(cascade.items[2].ruleIndex, RoboCssCascadeNoIndex);
    RoboCheckEqInt(cascade.items[2].selectorIndex, RoboCssCascadeNoIndex);
    RoboCheckEqInt(cascade.items[2].declarationIndex, 0u);
    RoboCheckEqInt(cascade.unsupported, 0);
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "color", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "inline");
    /* The sheet's other declaration is still there and uncontested. */
    RoboCheckEqInt(CssCascadeWinnerText(&cascade, "padding", text, sizeof(text)),
                      RoboCssCascadeVerdictDecided);
    RoboCheckStrEq(text, "4px");

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeCollectInlineAbsentAndEmpty(void)
{
    CssCascadeFixture fixture;
    CssInlineStub stub;
    CssCascade cascade = RoboCssCleared;

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: red }");

    /* No `style` attribute at all: nothing was declared, and nothing is wrong. */
    CssCascadeInline(&fixture, &stub, NULL);
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(cascade.inlineChecked, 1);
    RoboCheckEqInt(cascade.inlinePresent, 0);
    RoboCheckEqInt(cascade.hasInlineError, 0);
    RoboCheckEqInt(CssDeclarationListCount(&cascade.inlineDeclarations), 0u);
    RoboCheckEqInt(CssCascadeCount(&cascade), 1u);
    RoboCheckEqInt(cascade.unsupported, 0);
    CssCascadeDispose(&cascade);

    /* `style=""`: there *is* an attribute, and it declares nothing. The two are
     * different facts, which is why `inlinePresent` exists at all. */
    CssCascadeInline(&fixture, &stub, "");
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(cascade.inlinePresent, 1);
    RoboCheckEqInt(cascade.hasInlineError, 0);
    RoboCheckEqInt(CssDeclarationListCount(&cascade.inlineDeclarations), 0u);
    RoboCheckEqInt(CssCascadeCount(&cascade), 1u);
    CssCascadeDispose(&cascade);

    /* With no provider installed at all, the boundary was never crossed. */
    RoboCheckEqInt(CssStyleContextSetInlineStyle(fixture.context, NULL, NULL,
                                                              NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleContextInlineUserdata(fixture.context) == NULL, 1);
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(cascade.inlineChecked, 0);
    RoboCheckEqInt(cascade.items[0].origin, RoboCssOriginAuthor);
    CssCascadeDispose(&cascade);

    CssCascadeEnd(&fixture);
}

static void TestCssCascadeCollectInlineProblemIsReportedNotFatal(void)
{
    CssCascadeFixture fixture;
    CssInlineStub stub;
    CssCascade cascade = RoboCssCleared;
    CssError error = RoboCssCleared;

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: red }");
    /* A malformed inline declaration is recovered from, exactly as a malformed
     * one inside a rule is (9.3): the valid declarations are collected, and the
     * problem is reported through the call's error record. */
    CssCascadeInline(&fixture, &stub, "color red; background: blue");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, &error);
    RoboCheckEqInt(cascade.hasInlineError, 1);
    RoboCheckEqInt(cascade.inlineError.kind != RoboCssErrorKindNone, 1);
    RoboCheckEqInt(CssErrorIsError(&error), 1);
    RoboCheckEqInt(error.kind, cascade.inlineError.kind);
    RoboCheckEqInt(cascade.declarationsIgnored, 1u);
    /* Two candidates: the rule's `color` and the inline `background`. */
    RoboCheckEqInt(CssCascadeCount(&cascade), 2u);
    RoboCheckEqInt(CssCascadeNamedCount(&cascade, "background"), 1u);
    /* Recovered, so the answer is complete — an incomplete answer is a different
     * fact from a CSS problem, and the two fields say so separately. */
    RoboCheckEqInt(cascade.unsupported, 0);

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeCollectInlineHostFailureIncomplete(void)
{
    CssCascadeFixture fixture;
    CssInlineStub stub;
    CssCascade cascade = RoboCssCleared;
    CssError error = RoboCssCleared;

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: red }");
    CssCascadeInline(&fixture, &stub, NULL);
    stub.failure = RoboNotSupported;

    RoboCssCascadeCollect(&fixture, 1u, &cascade, &error);
    /* The sheets' answer is real, so the call succeeds — and the answer is a
     * lower bound, so `unsupported` says so. Treating a host that cannot answer
     * as "the element has no inline declarations" would be the silent wrong-rule
     * bug the whole three-outcome discipline exists to prevent. */
    RoboCheckEqInt(cascade.unsupported, 1);
    RoboCheckEqInt(CssCascadeIsComplete(&cascade), 0);
    RoboCheckEqInt(cascade.hasInlineError, 1);
    RoboCheckEqInt(cascade.inlineError.code, RoboNotSupported);
    RoboCheckEqInt(CssErrorIsError(&error), 1);
    RoboCheckEqInt(CssCascadeCount(&cascade), 1u);
    RoboCheckEqInt(cascade.items[0].origin, RoboCssOriginAuthor);

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* What could not be used                                              */
/* ------------------------------------------------------------------ */

static void TestCssCascadeCollectRuleWithoutSelectors(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    CssStylesheetId sheet;

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    /* Added by hand: the prelude exists as component values (9.3) but was never
     * turned into selectors, so the rule's reach is unknown. A cascade that
     * treated that as "does not apply" would silently drop the rule. */
    sheet = CssParseSheet("div { color: red }", NULL);
    RoboCheckEqInt(CssStylesheetSetOrigin(sheet, RoboCssOriginAuthor, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleContextAddSheet(fixture.context,
                                                      RoboCssOriginAuthor, sheet, NULL),
                      RoboOk);

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(cascade.rulesScanned, 1u);
    RoboCheckEqInt(cascade.rulesMatched, 0u);
    RoboCheckEqInt(cascade.rulesWithoutSelectors, 1u);
    RoboCheckEqInt(cascade.unsupported, 1);
    RoboCheckEqInt(CssCascadeCount(&cascade), 0u);
    CssCascadeDispose(&cascade);

    /* …and the same sheet with its selectors parsed applies normally, which is
     * what makes the count above a statement about the input rather than a
     * permanent limitation. */
    {
        CssError ruleError = RoboCssCleared;

        RoboCheckEqInt(CssStyleRuleParseSelectors(
                              (CssStyleRule *)CssRuleStyle(
                                  CssStylesheetRule(sheet, 0u)),
                              CssStylesheetSource(sheet), &ruleError, NULL),
                          RoboOk);
        RoboCheckEqInt(CssErrorIsError(&ruleError), 0);
    }
    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(cascade.rulesWithoutSelectors, 0u);
    RoboCheckEqInt(cascade.unsupported, 0);
    RoboCheckEqInt(CssCascadeCount(&cascade), 1u);
    CssCascadeDispose(&cascade);

    CssCascadeEnd(&fixture);
}

static void TestCssCascadeCollectAtRulesContributeNothing(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    /* Nothing evaluates `@media`, so its block cannot contribute — but the rules
     * inside it *would* apply in a complete build, so the block is counted and
     * the answer is marked incomplete. `@charset` hides nothing, so it is counted
     * without making the answer a lower bound. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "@charset \"utf-8\";\n"
                      "@media screen { div { color: red } }\n"
                      "div { color: blue }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(cascade.atRulesSkipped, 2u);
    RoboCheckEqInt(cascade.atRulesWithBlocks, 1u);
    RoboCheckEqInt(cascade.rulesScanned, 1u);
    RoboCheckEqInt(cascade.rulesMatched, 1u);
    RoboCheckEqInt(CssCascadeCount(&cascade), 1u);
    RoboCheckEqInt(cascade.unsupported, 1);
    /* And which declaration was collected proves the block's was not: the only
     * candidate is the top-level rule's. */
    RoboCheckEqInt(cascade.items[0].declarationIndex, 0u);

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeCollectUndecidedSelectorIsReported(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    /* `:hover` parses and matches nothing *decidedly*, so the rule is a plain
     * non-match; `:hover` combined with something that does hold leaves the whole
     * rule undecided, and that must not be counted as a negative. */
    CssCascadeSheet(&fixture, RoboCssOriginAuthor,
                      "div:hover { color: hover }\n"
                      "div { color: plain }");

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(cascade.rulesScanned, 2u);
    RoboCheckEqInt(cascade.rulesMatched, 1u);
    RoboCheckEqInt(cascade.rulesUndecided, 1u);
    RoboCheckEqInt(cascade.unsupported, 1);
    RoboCheckEqInt(CssCascadeCount(&cascade), 1u);
    RoboCheckEqInt(cascade.items[0].selectorIndex, 0u);

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeCollectWithoutAHost(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: red }");
    /* A context with sheets and no host can answer nothing at all — every rule
     * that has selectors is undecided rather than non-matching. */
    RoboCheckEqInt(CssStyleContextSetElementHost(fixture.context, NULL, NULL),
                      RoboOk);

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(cascade.rulesScanned, 1u);
    RoboCheckEqInt(cascade.rulesUndecided, 1u);
    RoboCheckEqInt(cascade.unsupported, 1);
    RoboCheckEqInt(CssCascadeCount(&cascade), 0u);

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeCollectDestroyedSheet(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    CssStylesheetId sheet;

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    sheet = CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: red }");
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    /* A context refuses a dead id when it is added, so this is only reachable by
     * destroying a sheet the context still references — and the honest answer is
     * that the sheet is gone, not that it declared nothing. */
    RoboCheckEqInt(cascade.sheetsUnavailable, 1u);
    RoboCheckEqInt(cascade.sheetsScanned, 0u);
    RoboCheckEqInt(cascade.unsupported, 1);
    RoboCheckEqInt(CssCascadeCount(&cascade), 0u);

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

static void TestCssCascadeCollectOriginMismatch(void)
{
    CssCascadeFixture fixture;
    CssCascade cascade = RoboCssCleared;
    CssStylesheetId sheet;

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    /* The sheet records AUTHOR (that is how CssParseSheet builds it) and the
     * context lists it as USER. The context's list is what places a sheet in the
     * cascade, so the level follows the list — and the disagreement is counted
     * rather than smoothed over. */
    sheet = CssParseSheet("div { color: red }", NULL);
    RoboCheckEqInt(CssStylesheetOrigin(sheet), RoboCssOriginAuthor);
    {
        CssError ruleError = RoboCssCleared;

        RoboCheckEqInt(CssStyleRuleParseSelectors(
                              (CssStyleRule *)CssRuleStyle(
                                  CssStylesheetRule(sheet, 0u)),
                              CssStylesheetSource(sheet), &ruleError, NULL),
                          RoboOk);
        RoboCheckEqInt(CssErrorIsError(&ruleError), 0);
    }
    RoboCheckEqInt(CssStyleContextAddSheet(fixture.context,
                                                      RoboCssOriginUser, sheet, NULL),
                      RoboOk);

    RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
    RoboCheckEqInt(cascade.sheetsOriginMismatch, 1u);
    RoboCheckEqInt(cascade.sheetsScanned, 1u);
    RoboCheckEqInt(CssCascadeCount(&cascade), 1u);
    RoboCheckEqInt(cascade.items[0].origin, RoboCssOriginUser);
    RoboCheckEqInt(cascade.items[0].level, RoboCssCascadeLevelUser);
    /* A mismatch is a caller mistake, not an incomplete answer. */
    RoboCheckEqInt(cascade.unsupported, 0);

    CssCascadeDispose(&cascade);
    CssCascadeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

static void TestCssCascadeCollectReplacesAndReleases(void)
{
    CssCascadeFixture fixture;
    CssInlineStub stub;
    CssCascade cascade = RoboCssCleared;
    int weEnabled;
    size_t baseline;

    CssFixture();
    CssCascadeBegin(&fixture);
    (void)CssHostAdd(&fixture.host, "div", NULL, NULL);
    CssHostSeal(&fixture.host);
    CssCascadeSheet(&fixture, RoboCssOriginAuthor, "div { color: red; padding: 4px }");
    CssCascadeInline(&fixture, &stub, "color: inline; margin: 1px");

    weEnabled = CssAllocatorEnsureEnabled();
    baseline = MemDebugLiveCount();
    {
        int round;

        for (round = 0; round < 8; round++) {
            /* Collecting into a record that already holds a cascade replaces it:
             * the counters start from zero and the arrays are reused, because
             * the collection begins with an init. */
            RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
            RoboCheckEqInt(CssCascadeCount(&cascade), 4u);
            RoboCheckEqInt(cascade.sheetsScanned, 1u);
            RoboCheckEqInt(cascade.rulesMatched, 1u);
            RoboCheckEqInt(cascade.inlinePresent, 1);
            CssCascadeDispose(&cascade);
            RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
        }
        /* And one last collection, disposed by the caller below, proves the
         * inline declaration list is released with the rest. */
        RoboCssCascadeCollect(&fixture, 1u, &cascade, NULL);
#if RoboMemDebug
        /* Strictly greater than the baseline is only observable where the
         * allocator accounts for its live blocks; a Release build has no
         * tracker, so this is asserted in Debug and the collection above still
         * runs in both. */
        RoboCheckEqInt((int)MemDebugLiveCount() > (int)baseline, 1);
#endif
        CssCascadeDispose(&cascade);
    }
    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    /* Idempotent: disposing twice releases nothing twice. */
    CssCascadeDispose(&cascade);
    CssAllocatorRestore(weEnabled);

    CssCascadeEnd(&fixture);
}

void TestRegisterCssCascadeCollect(void);

void TestRegisterCssCascadeCollect(void)
{
    TestRegister("css.cascade.collect.matches_only_the_element",
                       TestCssCascadeCollectMatchesOnlyTheElement);
    TestRegister("css.cascade.collect.order_across_origins",
                       TestCssCascadeCollectOrderAcrossOrigins);
    TestRegister("css.cascade.collect.important_reorders",
                       TestCssCascadeCollectImportantReorders);
    TestRegister("css.cascade.collect.two_sheets_in_one_origin",
                       TestCssCascadeCollectTwoSheetsInOneOrigin);
    TestRegister("css.cascade.collect.inline_is_last_and_strong",
                       TestCssCascadeCollectInlineIsLastAndStrong);
    TestRegister("css.cascade.collect.inline_absent_and_empty",
                       TestCssCascadeCollectInlineAbsentAndEmpty);
    TestRegister("css.cascade.collect.inline_problem_is_reported_not_fatal",
                       TestCssCascadeCollectInlineProblemIsReportedNotFatal);
    TestRegister("css.cascade.collect.inline_host_failure_incomplete",
                       TestCssCascadeCollectInlineHostFailureIncomplete);
    TestRegister("css.cascade.collect.rule_without_selectors",
                       TestCssCascadeCollectRuleWithoutSelectors);
    TestRegister("css.cascade.collect.at_rules_contribute_nothing",
                       TestCssCascadeCollectAtRulesContributeNothing);
    TestRegister("css.cascade.collect.undecided_selector_is_reported",
                       TestCssCascadeCollectUndecidedSelectorIsReported);
    TestRegister("css.cascade.collect.without_a_host",
                       TestCssCascadeCollectWithoutAHost);
    TestRegister("css.cascade.collect.destroyed_sheet",
                       TestCssCascadeCollectDestroyedSheet);
    TestRegister("css.cascade.collect.origin_mismatch",
                       TestCssCascadeCollectOriginMismatch);
    TestRegister("css.cascade.collect.replaces_and_releases",
                       TestCssCascadeCollectReplacesAndReleases);
}
