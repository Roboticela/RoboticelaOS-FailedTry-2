/*
 * RoboCSS Phase 9.1 rule/stylesheet/parser suite:
 * style rules and at-rules, the stylesheet object, and the parser cursor.
 *
 * The parse entry points are tested for what they *do not* do: return
 * RoboNotSupported, change nothing, and say why. A test asserting that they
 * parsed something would be testing a parser this phase does not have.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Rules                                                              */
/* ------------------------------------------------------------------ */

static void TestCssRuleKinds(void)
{
    RoboCheck(CssRuleKindIsKnown(RoboCssRuleKindStyle));
    RoboCheck(CssRuleKindIsKnown(RoboCssRuleKindAt));
    RoboCheck(CssRuleKindIsKnown(RoboCssRuleKindNone));
    RoboCheck(!CssRuleKindIsKnown((CssRuleKind)9));
    RoboCheckStrEq(CssRuleKindName(RoboCssRuleKindAt), "at");
    RoboCheckStrEq(CssRuleKindName((CssRuleKind)9), "unknown");
}

static void TestCssRuleStyleRule(void)
{
    CssSource source = RoboCssCleared;
    CssRule rule = RoboCssCleared;
    CssStyleRule *style;
    CssComplexSelector selector = RoboCssCleared;
    CssDeclaration declaration = RoboCssCleared;
    CssValue value = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "p { color: red; margin: 10px; }");

    CssRuleInit(&rule);
    RoboCheckEqInt(rule.kind, RoboCssRuleKindNone);
    RoboCheck(CssRuleStyle(&rule) == NULL);
    RoboCheck(CssRuleAt(&rule) == NULL);
    RoboCheckEqInt(CssRuleTreeCount(&rule), 0u);

    RoboCheckEqInt(CssRuleMakeStyle(&rule, NULL), RoboOk);
    RoboCheckEqInt(rule.kind, RoboCssRuleKindStyle);
    style = &rule.style;
    RoboCheck(CssRuleAt(&rule) == NULL);
    RoboCheckEqInt(CssRuleTreeCount(&rule), 1u);
    RoboCheck(!CssStyleRuleIsPopulated(style));

    CssComplexSelectorOf(&selector, &source, "p", NULL);
    RoboCheckEqInt(CssStyleRuleAppendSelector(style, &selector, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleRuleSelectorCount(style), 1u);

    CssDeclarationMake(&declaration, CssSliceFromText("color"),
                              RoboCssDeclarationStateParsed, NULL);
    RoboCheckEqInt(CssValueMakeList(&value, NULL), RoboOk);
    RoboCheckEqInt(CssDeclarationSetValue(&declaration, &value, NULL),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationSetImportant(&declaration, 1, NULL), RoboOk);
    RoboCheckEqInt(CssStyleRuleAppendDeclaration(style, &declaration, NULL),
                      RoboOk);
    RoboCheckEqInt(declaration.state, RoboCssDeclarationStateNone);

    CssDeclarationMake(&declaration, CssSliceFromText("margin"),
                              RoboCssDeclarationStateParsed, NULL);
    RoboCheckEqInt(CssStyleRuleAppendDeclaration(style, &declaration, NULL),
                      RoboOk);

    RoboCheckEqInt(CssStyleRuleDeclarationCount(style), 2u);
    RoboCheck(CssStyleRuleIsPopulated(style));
    RoboCheckEqInt(CssStyleRuleDeclaration(style, 0u)->important, 1);
    RoboCheckEqInt(CssStyleRuleDeclaration(style, 1u)->important, 0);
    /* Declarations stay in source order, duplicates included. */
    RoboCheck(CssSliceEqualsAscii(
                   CssDeclarationName(CssStyleRuleDeclaration(style, 1u)),
                   "margin"));
    RoboCheck(CssStyleRuleDeclaration(style, 2u) == NULL);
    RoboCheckEqInt(CssDeclarationListCount(
                          CssStyleRuleDeclarations(style)),
                      2u);
    RoboCheckEqInt(CssSelectorListCount(CssStyleRuleSelectors(style)),
                      1u);

    /* A rule with selectors and no declarations is legal CSS and stays that
     * way; emptiness is reported, not repaired. */
    {
        CssRule empty = RoboCssCleared;

        RoboCheckEqInt(CssRuleMakeStyle(&empty, NULL), RoboOk);
        RoboCheck(!CssStyleRuleIsPopulated(&empty.style));
        CssRuleDispose(&empty);
    }

    CssRuleDispose(&rule);
    RoboCheckEqInt(rule.kind, RoboCssRuleKindNone);
    RoboCheckEqInt(CssStyleRuleSelectorCount(style), 0u);
    RoboCheckEqInt(CssStyleRuleDeclarationCount(style), 0u);
    RoboCheckEqInt(CssStyleRuleSelectorCount(NULL), 0u);
    RoboCheckEqInt(CssStyleRuleDeclarationCount(NULL), 0u);
    RoboCheck(CssStyleRuleDeclaration(NULL, 0u) == NULL);
    RoboCheck(CssStyleRuleSelectors(NULL) == NULL);
    RoboCheck(CssStyleRuleDeclarations(NULL) == NULL);
    RoboCheck(!CssStyleRuleIsPopulated(NULL));
    RoboCheckEqInt(CssRuleTreeCount(NULL), 0u);
    RoboCheckEqInt(CssRuleRange(NULL).begin, 0u);
    CssRuleDispose(NULL);
    CssRuleInit(NULL);
    RoboCheckEqInt(CssRuleMakeStyle(NULL, NULL), RoboInvalidArgument);
    CssSourceDispose(&source);
}

static void TestCssRuleAtRule(void)
{
    CssSource source = RoboCssCleared;
    CssRule at = RoboCssCleared;
    CssRule nested = RoboCssCleared;
    CssRule nestedStyle = RoboCssCleared;
    CssValue value = RoboCssCleared;
    CssDeclaration declaration = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "@media print { p { color: black } }");

    CssRuleInit(&at);
    RoboCheckEqInt(CssRuleMakeAt(&at, CssSliceFromText("media"),
                                            NULL),
                      RoboOk);
    RoboCheckEqInt(at.kind, RoboCssRuleKindAt);
    RoboCheck(CssRuleStyle(&at) == NULL);
    RoboCheck(CssRuleAt(&at) != NULL);
    RoboCheck(CssSliceEqualsAscii(CssRuleAt(&at)->name, "media"));

    /* A rule cannot be added to an at-rule that has no block: `@import url(x);`
     * is not silently turned into a block so that the append can succeed. */
    CssRuleInit(&nested);
    RoboCheckEqInt(CssRuleMakeStyle(&nested, NULL), RoboOk);
    RoboCheckEqInt(CssAtRuleAppendRule(&at.at, &nested, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssAtRuleRuleCount(&at.at), 0u);

    RoboCheckEqInt(CssAtRuleSetPrelude(&at.at,
                                                   CssSliceFromText("print"), 1,
                                                   NULL),
                      RoboOk);
    RoboCheckEqInt(at.at.hasBlock, 1);
    RoboCheck(CssSliceEqualsAscii(at.at.prelude, "print"));

    CssDeclarationMake(&declaration, CssSliceFromText("color"),
                              RoboCssDeclarationStateParsed, NULL);
    RoboCheckEqInt(CssValueMakeList(&value, NULL), RoboOk);
    RoboCheckEqInt(CssDeclarationSetValue(&declaration, &value, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleRuleAppendDeclaration(&nested.style,
                                                             &declaration, NULL),
                      RoboOk);
    RoboCheckEqInt(CssAtRuleAppendRule(&at.at, &nested, NULL), RoboOk);
    RoboCheckEqInt(nested.kind, RoboCssRuleKindNone);
    RoboCheckEqInt(CssAtRuleRuleCount(&at.at), 1u);
    RoboCheckEqInt(CssAtRuleRuleCount(NULL), 0u);
    RoboCheck(CssAtRuleRuleAt(&at.at, 0u) != NULL);
    RoboCheck(CssAtRuleRuleAt(&at.at, 1u) == NULL);
    RoboCheck(CssAtRuleRuleAt(NULL, 0u) == NULL);
    /* The tree count sees through the block. */
    RoboCheckEqInt(CssRuleTreeCount(&at), 2u);

    /* A nested at-rule is allowed to nest: `@media print { @supports ... }` is
     * legal, so the model must not be one level deep. */
    {
        CssRule inner = RoboCssCleared;

        RoboCheckEqInt(CssRuleMakeAt(&inner,
                                                CssSliceFromText("supports"),
                                                NULL),
                          RoboOk);
        RoboCheckEqInt(CssAtRuleSetPrelude(&inner.at,
                                                       CssSliceFromText("display: grid"),
                                                       1, NULL),
                          RoboOk);
        RoboCheckEqInt(CssRuleMakeStyle(&nestedStyle, NULL), RoboOk);
        RoboCheckEqInt(CssAtRuleAppendRule(&inner.at, &nestedStyle, NULL),
                          RoboOk);
        RoboCheckEqInt(CssAtRuleAppendRule(&at.at, &inner, NULL), RoboOk);
    }
    RoboCheckEqInt(CssAtRuleRuleCount(&at.at), 2u);
    RoboCheckEqInt(CssRuleTreeCount(&at), 4u);

    /* Dropping the block drops the rules that lived in it: an at-rule that no
     * longer has a block must not keep orphans. */
    RoboCheckEqInt(CssAtRuleSetPrelude(&at.at, CssSliceEmpty(), 0,
                                                   NULL),
                      RoboOk);
    RoboCheckEqInt(at.at.hasBlock, 0);
    RoboCheckEqInt(CssAtRuleRuleCount(&at.at), 0u);
    RoboCheckEqInt(CssRuleTreeCount(&at), 1u);

    /* An at-rule needs a name, and a prelude needs an at-rule that has one. */
    RoboCheckEqInt(CssRuleMakeAt(&at, CssSliceEmpty(), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssRuleMakeAt(NULL, CssSliceFromText("media"),
                                            NULL),
                      RoboInvalidArgument);
    {
        CssAtRule fresh = RoboCssCleared;
        CssRule styleRule = RoboCssCleared;

        CssAtRuleInit(&fresh);
        RoboCheckEqInt(CssAtRuleSetPrelude(&fresh,
                                                       CssSliceFromText("x"), 1,
                                                       NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssAtRuleMake(&fresh, CssSliceEmpty(), NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssRuleMakeStyle(&styleRule, NULL), RoboOk);
        RoboCheckEqInt(CssAtRuleAppendRule(&fresh, &styleRule, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssAtRuleAppendRule(NULL, &styleRule, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssAtRuleAppendRule(&fresh, NULL, NULL),
                          RoboInvalidArgument);
        CssRuleDispose(&styleRule);
        CssAtRuleDispose(&fresh);
    }
    {
        CssRule candidate = RoboCssCleared;

        RoboCheckEqInt(CssRuleMakeAt(&nested,
                                                CssSliceFromText("bad"),
                                                NULL),
                          RoboOk);
        RoboCheckEqInt(CssRuleMakeStyle(&candidate, NULL), RoboOk);
        /* No block on this at-rule, so the rule is refused and left untouched:
         * it still owns whatever it owns. */
        RoboCheckEqInt(CssAtRuleAppendRule(&nested.at, &candidate, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(candidate.kind, RoboCssRuleKindStyle);
        CssRuleDispose(&candidate);
    }
    /* Making one alternative over the other releases what the previous held. */
    RoboCheckEqInt(CssRuleMakeStyle(&nested, NULL), RoboOk);
    RoboCheckEqInt(nested.kind, RoboCssRuleKindStyle);
    RoboCheckEqInt(CssRuleMakeAt(&nested, CssSliceFromText("x"),
                                            NULL),
                      RoboOk);
    RoboCheckEqInt(nested.kind, RoboCssRuleKindAt);
    RoboCheck(CssAtRuleRuleCount(&nested.at) == 0u);

    CssRuleDispose(&at);
    CssRuleDispose(&nested);
    CssSourceDispose(&source);
}

static void TestCssRuleList(void)
{
    CssSource source = RoboCssCleared;
    CssRuleList list = RoboCssCleared;
    CssRule rule = RoboCssCleared;
    CssDeclaration declaration = RoboCssCleared;
    const CssRule *flattened[8];

    CssFixture();
    CssSourceOfString(&source, "p { color: red } p { color: blue }");

    CssRuleListInit(&list);
    RoboCheck(CssRuleListIsEmpty(&list));
    RoboCheckEqInt(CssRuleListCount(&list), 0u);
    RoboCheck(CssRuleListAt(&list, 0u) == NULL);
    RoboCheckEqInt(CssRuleListTreeCount(&list), 0u);
    RoboCheckEqInt(CssRuleListStyleRuleCount(&list), 0u);

    /* An unmade rule is refused: a rule list holds real rules. */
    CssRuleInit(&rule);
    RoboCheckEqInt(CssRuleListAppend(&list, &rule, NULL),
                      RoboInvalidArgument);

    RoboCheckEqInt(CssRuleMakeStyle(&rule, NULL), RoboOk);
    CssDeclarationMake(&declaration, CssSliceFromText("color"),
                              RoboCssDeclarationStateParsed, NULL);
    RoboCheckEqInt(CssStyleRuleAppendDeclaration(&rule.style, &declaration,
                                                             NULL),
                      RoboOk);
    RoboCheckEqInt(CssRuleListAppend(&list, &rule, NULL), RoboOk);
    RoboCheckEqInt(rule.kind, RoboCssRuleKindNone);

    RoboCheckEqInt(CssRuleMakeAt(&rule, CssSliceFromText("media"),
                                            NULL),
                      RoboOk);
    RoboCheckEqInt(CssAtRuleSetPrelude(&rule.at, CssSliceEmpty(), 1,
                                                   NULL),
                      RoboOk);
    {
        CssRule inner = RoboCssCleared;

        RoboCheckEqInt(CssRuleMakeStyle(&inner, NULL), RoboOk);
        RoboCheckEqInt(CssAtRuleAppendRule(&rule.at, &inner, NULL),
                          RoboOk);
    }
    RoboCheckEqInt(CssRuleListAppend(&list, &rule, NULL), RoboOk);

    /* Two top-level rules, three counting the nested one; style rules are the
     * two, because the nested one is a style rule and the at-rule is not. */
    RoboCheckEqInt(CssRuleListCount(&list), 2u);
    RoboCheckEqInt(CssRuleListTreeCount(&list), 3u);
    RoboCheckEqInt(CssRuleListStyleRuleCount(&list), 2u);
    RoboCheckEqInt(CssRuleListAt(&list, 0u)->kind, RoboCssRuleKindStyle);
    RoboCheckEqInt(CssRuleListAt(&list, 1u)->kind, RoboCssRuleKindAt);
    RoboCheck(CssRuleListAt(&list, 2u) == NULL);

    /* Flattening is depth-first in source order, and a short buffer is a
     * measurement, not a failure. */
    RoboCheckEqInt(CssRuleListFlatten(&list, flattened, 8u), 3u);
    RoboCheckEqInt(flattened[0]->kind, RoboCssRuleKindStyle);
    RoboCheckEqInt(flattened[1]->kind, RoboCssRuleKindAt);
    RoboCheckEqInt(flattened[2]->kind, RoboCssRuleKindStyle);
    RoboCheckEqInt(CssRuleListFlatten(&list, flattened, 2u), 3u);
    RoboCheckEqInt(flattened[0]->kind, RoboCssRuleKindStyle);
    RoboCheckEqInt(flattened[1]->kind, RoboCssRuleKindAt);
    RoboCheckEqInt(CssRuleListFlatten(&list, NULL, 0u), 3u);
    RoboCheckEqInt(CssRuleListFlatten(NULL, flattened, 8u), 0u);
    /* The flat count and the tree count must agree: they measure the same
     * tree, and a disagreement would mean a rule was counted twice or lost. */
    RoboCheckEqInt(CssRuleListFlatten(&list, NULL, 0u),
                      CssRuleListTreeCount(&list));

    CssRuleListClear(&list);
    RoboCheckEqInt(CssRuleListCount(&list), 0u);
    RoboCheckEqInt(CssRuleListTreeCount(&list), 0u);
    RoboCheckEqInt(CssRuleListAppend(&list, &rule, NULL),
                      RoboInvalidArgument);
    CssRuleListDispose(&list);
    CssRuleListDispose(NULL);
    CssRuleListClear(NULL);
    CssRuleListInit(NULL);
    RoboCheck(CssRuleListIsEmpty(NULL));
    RoboCheckEqInt(CssRuleListCount(NULL), 0u);
    RoboCheck(CssRuleListAt(NULL, 0u) == NULL);
    RoboCheckEqInt(CssRuleListTreeCount(NULL), 0u);
    RoboCheckEqInt(CssRuleListStyleRuleCount(NULL), 0u);
    RoboCheckEqInt(CssRuleListAppend(NULL, &rule, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssRuleListAppend(&list, NULL, NULL),
                      RoboInvalidArgument);

    CssSourceDispose(&source);
}

/* ------------------------------------------------------------------ */
/* Stylesheets                                                        */
/* ------------------------------------------------------------------ */

static void TestCssStylesheetLifecycle(void)
{
    CssStylesheetId first = RoboCssStylesheetIdNone;
    CssStylesheetId second = RoboCssStylesheetIdNone;
    CssDiag diag;

    CssFixture();
    RoboCheckEqInt(CssStylesheetCreate(NULL, RoboCssOriginAuthor, NULL),
                      RoboInvalidArgument);

    CssDiagReset(&diag);
    RoboCheckEqInt(CssStylesheetCreate(&first, (CssOrigin)42, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboCssDomainStylesheet);
    RoboCheckEqInt(first, RoboCssStylesheetIdNone);

    RoboCheckEqInt(CssStylesheetCreate(&first, RoboCssOriginAuthor, NULL),
                      RoboOk);
    RoboCheck(first != RoboCssStylesheetIdNone);
    RoboCheck(CssStylesheetIsLive(first));
    RoboCheckEqInt(CssStylesheetOrigin(first), RoboCssOriginAuthor);
    RoboCheckEqInt(MakeCssStylesheetState(first),
                      RoboCssStylesheetStateCreated);
    RoboCheckEqInt(CssStylesheetRuleCount(first), 0u);

    /* A second sheet gets a different id, and both are addressable by slot. */
    RoboCheckEqInt(CssStylesheetCreate(&second, RoboCssOriginRuntime, NULL),
                      RoboOk);
    RoboCheck(second != first);
    RoboCheckEqInt(CssLiveStylesheetCount(), 2u);
    RoboCheckEqInt(CssStylesheetIdAt(0u), first);
    RoboCheckEqInt(CssStylesheetIdAt(1u), second);
    RoboCheckEqInt(CssStylesheetIdAt(2u), RoboCssStylesheetIdNone);

    RoboCheckEqInt(CssStylesheetDestroy(first, NULL), RoboOk);
    /* A destroyed id stays dead, and destroying it again is NOT_FOUND rather
     * than a second attempt at the same free. */
    RoboCheck(!CssStylesheetIsLive(first));
    RoboCheckEqInt(CssStylesheetDestroy(first, NULL), RoboNotFound);
    RoboCheckEqInt(CssStylesheetDestroy(RoboCssStylesheetIdNone, NULL),
                      RoboNotFound);
    RoboCheckEqInt(CssStylesheetDestroy((CssStylesheetId)99999u, NULL),
                      RoboNotFound);
    RoboCheckEqInt(CssLiveStylesheetCount(), 1u);

    /* Every accessor answers sensibly for an id that does not resolve. */
    RoboCheckEqInt(CssStylesheetOrigin(first), RoboCssOriginNone);
    RoboCheckEqInt(MakeCssStylesheetState(first), RoboCssStylesheetStateNone);
    RoboCheck(CssStylesheetName(first) == NULL);
    RoboCheck(CssStylesheetSource(first) == NULL);
    RoboCheck(CssStylesheetRules(first) == NULL);
    RoboCheck(CssStylesheetRule(first, 0u) == NULL);
    RoboCheckEqInt(CssStylesheetSourceLength(first), 0u);
    RoboCheck(!CssStylesheetOwnsSource(first));
    RoboCheckEqInt(CssStylesheetRemoveRule(first, 0u, NULL), RoboNotFound);
    RoboCheckEqInt(CssStylesheetSetState(first,
                                                    RoboCssStylesheetStateParsed,
                                                    NULL),
                      RoboNotFound);
    CssStylesheetClearRules(first); /* tolerant by contract */

    RoboCheckEqInt(CssStylesheetDestroy(second, NULL), RoboOk);
    RoboCheckEqInt(CssLiveStylesheetCount(), 0u);
}

static void TestCssStylesheetOriginStateName(void)
{
    CssStylesheetId id = RoboCssStylesheetIdNone;
    CssDiag diag;

    CssFixture();
    RoboCheckEqInt(CssStylesheetCreate(&id, RoboCssOriginNone, NULL),
                      RoboOk);
    /* A sheet whose provenance is not decided yet is legal and can be set. */
    RoboCheckEqInt(CssStylesheetOrigin(id), RoboCssOriginNone);
    RoboCheckEqInt(CssStylesheetSetOrigin(id, RoboCssOriginUser, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStylesheetOrigin(id), RoboCssOriginUser);
    RoboCheckEqInt(CssStylesheetSetOrigin(id, (CssOrigin)42, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssStylesheetOrigin(id), RoboCssOriginUser);
    RoboCheckEqInt(CssStylesheetSetOrigin((CssStylesheetId)777u,
                                                     RoboCssOriginUser, NULL),
                      RoboNotFound);

    /* A live sheet is never in the NONE state: NONE means "no stylesheet". */
    RoboCheckEqInt(CssStylesheetSetState(id,
                                                    RoboCssStylesheetStateNone,
                                                    NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssStylesheetSetState(id,
                                                    RoboCssStylesheetStateParsed,
                                                    NULL),
                      RoboOk);
    RoboCheckEqInt(MakeCssStylesheetState(id),
                      RoboCssStylesheetStateParsed);
    RoboCheck(CssStylesheetStateIsSettled(MakeCssStylesheetState(id)));

    RoboCheckStrEq(CssStylesheetName(id), "");
    RoboCheckEqInt(CssStylesheetSetName(id, "Application sheet", NULL),
                      RoboOk);
    RoboCheckStrEq(CssStylesheetName(id), "Application sheet");
    RoboCheckEqInt(CssStylesheetSetName(id, NULL, NULL), RoboOk);
    RoboCheckStrEq(CssStylesheetName(id), "");
    RoboCheckEqInt(CssStylesheetSetName((CssStylesheetId)777u, "x",
                                                   &diag),
                      RoboNotFound);
    RoboCheckEqInt(diag.domain, RoboCssDomainStylesheet);

    RoboCheckEqInt(CssStylesheetDestroy(id, NULL), RoboOk);
}

static void TestCssStylesheetSourceAttachment(void)
{
    CssStylesheetId id = RoboCssStylesheetIdNone;
    const CssSource *source;
    char buffer[64];

    CssFixture();
    RoboCheckEqInt(CssStylesheetCreate(&id, RoboCssOriginAuthor, NULL),
                      RoboOk);
    /* No source attached yet is reported as "no source", not as an empty one. */
    source = CssStylesheetSource(id);
    RoboCheck(source == NULL);

    strcpy(buffer, CSS_FIXTURE_BODY);
    RoboCheckEqInt(CssStylesheetSetSourceString(id, buffer, NULL), RoboOk);
    source = CssStylesheetSource(id);
    RoboCheck(source != NULL);
    RoboCheckEqInt(source->kind, RoboCssSourceKindString);
    RoboCheckEqInt(CssStylesheetSourceLength(id), strlen(buffer));
    RoboCheck(!CssStylesheetOwnsSource(id));

    /* Copy mode: the sheet outlives the buffer it was built from. */
    RoboCheckEqInt(CssStylesheetCopySource(id, (const uint8_t *)buffer,
                                                      strlen(buffer), NULL),
                      RoboOk);
    RoboCheck(CssStylesheetOwnsSource(id));
    buffer[0] = 'X';
    source = CssStylesheetSource(id);
    RoboCheckEqInt(source->bytes[0], (uint8_t)'b');
    RoboCheckEqInt(CssStylesheetSourceLength(id), strlen(CSS_FIXTURE_BODY));

    /* Borrowed again: the owned copy is released as the borrow replaces it. */
    RoboCheckEqInt(CssStylesheetSetSourceMemory(id,
                                                            (const uint8_t *)CSS_FIXTURE_BODY,
                                                            strlen(CSS_FIXTURE_BODY),
                                                            NULL),
                      RoboOk);
    RoboCheck(!CssStylesheetOwnsSource(id));

    RoboCheckEqInt(CssStylesheetSetSourceMemory((CssStylesheetId)777u,
                                                            NULL, 0u, NULL),
                      RoboNotFound);
    RoboCheckEqInt(CssStylesheetSetSourceMemory(id, NULL, 4u, NULL),
                      RoboInvalidArgument);

    RoboCheckEqInt(CssStylesheetDestroy(id, NULL), RoboOk);
}

static void TestCssStylesheetRuleStorage(void)
{
    CssStylesheetId id = RoboCssStylesheetIdNone;
    CssRule rule = RoboCssCleared;
    CssRule at = RoboCssCleared;
    CssDeclaration declaration = RoboCssCleared;

    CssFixture();
    RoboCheckEqInt(CssStylesheetCreate(&id, RoboCssOriginAuthor, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStylesheetCopySource(id,
                                                      (const uint8_t *)"p{color:red}",
                                                      strlen("p{color:red}"), NULL),
                      RoboOk);

    RoboCheckEqInt(CssRuleMakeStyle(&rule, NULL), RoboOk);
    CssDeclarationMake(&declaration, CssSliceFromText("color"),
                              RoboCssDeclarationStateParsed, NULL);
    RoboCheckEqInt(CssStyleRuleAppendDeclaration(&rule.style, &declaration,
                                                             NULL),
                      RoboOk);
    RoboCheckEqInt(CssStylesheetAppendRule(id, &rule, NULL), RoboOk);
    RoboCheckEqInt(rule.kind, RoboCssRuleKindNone);

    RoboCheckEqInt(CssRuleMakeAt(&at, CssSliceFromText("media"),
                                            NULL),
                      RoboOk);
    RoboCheckEqInt(CssAtRuleSetPrelude(&at.at, CssSliceEmpty(), 1,
                                                   NULL),
                      RoboOk);
    {
        CssRule inner = RoboCssCleared;

        RoboCheckEqInt(CssRuleMakeStyle(&inner, NULL), RoboOk);
        RoboCheckEqInt(CssAtRuleAppendRule(&at.at, &inner, NULL), RoboOk);
    }
    RoboCheckEqInt(CssStylesheetAppendRule(id, &at, NULL), RoboOk);

    RoboCheckEqInt(CssStylesheetRuleCount(id), 2u);
    RoboCheckEqInt(CssStylesheetTreeRuleCount(id), 3u);
    RoboCheckEqInt(CssStylesheetStyleRuleCount(id), 2u);
    RoboCheckEqInt(CssStylesheetRule(id, 0u)->kind, RoboCssRuleKindStyle);
    RoboCheckEqInt(CssStylesheetRule(id, 1u)->kind, RoboCssRuleKindAt);
    RoboCheck(CssStylesheetRule(id, 2u) == NULL);
    RoboCheckEqInt(CssStylesheetRules(id)->count, 2u);

    /* Removing from the middle keeps the rest in source order. */
    RoboCheckEqInt(CssStylesheetRemoveRule(id, 0u, NULL), RoboOk);
    RoboCheckEqInt(CssStylesheetRuleCount(id), 1u);
    RoboCheckEqInt(CssStylesheetRule(id, 0u)->kind, RoboCssRuleKindAt);
    RoboCheckEqInt(CssStylesheetRemoveRule(id, 5u, NULL), RoboNotFound);

    CssStylesheetClearRules(id);
    RoboCheckEqInt(CssStylesheetRuleCount(id), 0u);
    RoboCheckEqInt(CssStylesheetAppendRule(id, &rule, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssStylesheetAppendRule((CssStylesheetId)777u,
                                                      &rule, NULL),
                      RoboNotFound);
    RoboCheckEqInt(CssStylesheetAppendRule(id, NULL, NULL),
                      RoboInvalidArgument);

    RoboCheckEqInt(CssStylesheetDestroy(id, NULL), RoboOk);
}

static void TestCssStylesheetParseBoundary(void)
{
    CssStylesheetId id = RoboCssStylesheetIdNone;
    CssError error = RoboCssCleared;
    CssRule rule = RoboCssCleared;

    CssFixture();
    RoboCheckEqInt(CssStylesheetCreate(&id, RoboCssOriginAuthor, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStylesheetCopySource(id,
                                                      (const uint8_t *)CSS_FIXTURE_PARAGRAPH,
                                                      strlen(CSS_FIXTURE_PARAGRAPH),
                                                      NULL),
                      RoboOk);
    RoboCheckEqInt(CssRuleMakeStyle(&rule, NULL), RoboOk);
    RoboCheckEqInt(CssStylesheetAppendRule(id, &rule, NULL), RoboOk);
    RoboCheckEqInt(CssStylesheetRuleCount(id), 1u);

    /* A real parse as of 9.3, and the hand-built rule is *replaced* by the parse
     * result rather than added to: parsing twice cannot duplicate a stylesheet. */
    RoboCheckEqInt(CssStylesheetParse(id, &error, NULL), RoboOk);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheck(!CssErrorIsError(&error));
    RoboCheckEqInt(CssStylesheetRuleCount(id), 1u);
    RoboCheckEqInt(MakeCssStylesheetState(id),
                      RoboCssStylesheetStateParsed);
    RoboCheckEqInt(CssStylesheetSourceLength(id),
                      strlen(CSS_FIXTURE_PARAGRAPH));

    /* What came back is the fixture's rule: `p { color: red }`, as syntax. */
    {
        const CssRule *parsed = CssStylesheetRule(id, 0u);
        const CssStyleRule *style;

        RoboCheck(parsed != NULL);
        RoboCheckEqInt(parsed->kind, RoboCssRuleKindStyle);
        style = CssRuleStyle(parsed);
        RoboCheck(style != NULL);
        if (style) {
            /* `p {\n    color: red;\n}\n` — the prelude is one ident, the block
             * has one declaration, and the block was closed. */
            RoboCheckEqInt(CssValueChildCount(
                                  CssStyleRulePreludeValues(style)),
                              1u);
            RoboCheckEqInt(CssStyleRuleDeclarationCount(style), 1u);
            RoboCheckEqInt(style->blockClosed, 1);
            RoboCheck(CssSliceEqualsAscii(
                CssDeclarationName(CssStyleRuleDeclaration(style, 0u)),
                "color"));
            /* The prelude's range covers `p` and nothing else, and the rule's
             * range starts where its prelude does. */
            RoboCheckEqInt(CssStyleRulePreludeRange(style).begin, 0u);
            RoboCheckEqInt(CssStyleRulePreludeRange(style).end, 1u);
            RoboCheckEqInt(CssRuleRange(parsed).begin, 0u);
            RoboCheckEqInt(CssRuleRange(parsed).end,
                              strlen(CSS_FIXTURE_PARAGRAPH) - 1u);
        }
    }

    /* Idempotent: a second parse of the same source replaces, never appends. */
    RoboCheckEqInt(CssStylesheetParse(id, &error, NULL), RoboOk);
    RoboCheckEqInt(CssStylesheetRuleCount(id), 1u);
    RoboCheckEqInt(CssStylesheetTreeRuleCount(id), 1u);

    /* A clean parse leaves no error on the stylesheet, for a caller that never
     * asked: there is nothing to explain. */
    {
        CssError recorded = RoboCssCleared;

        RoboCheckEqInt(CssStylesheetLastError(id, &recorded), 0);
        RoboCheckEqInt(recorded.kind, RoboCssErrorKindNone);
    }

    /* Malformed CSS is a *recovered* parse, not a refused one: the valid rules
     * survive, the state is PARSED, and the first problem is kept both on the
     * stylesheet and in the caller's record. */
    {
        CssStylesheetId broken = RoboCssStylesheetIdNone;
        CssError problem = RoboCssCleared;
        const char *text = "broken { oops } p { color: red }";

        RoboCheckEqInt(CssStylesheetCreate(&broken, RoboCssOriginAuthor,
                                                     NULL),
                          RoboOk);
        RoboCheckEqInt(CssStylesheetCopySource(broken,
                                                          (const uint8_t *)text,
                                                          strlen(text), NULL),
                          RoboOk);
        RoboCheckEqInt(CssStylesheetParse(broken, &problem, NULL), RoboOk);
        RoboCheckEqInt(problem.kind, RoboCssErrorKindMalformedDeclaration);
        RoboCheck(CssErrorKindIsRecoverable(problem.kind));
        RoboCheckEqInt(CssStylesheetRuleCount(broken), 2u);
        RoboCheckEqInt(MakeCssStylesheetState(broken),
                          RoboCssStylesheetStateParsed);
        {
            CssError recorded = RoboCssCleared;

            RoboCheckEqInt(CssStylesheetLastError(broken, &recorded), 1);
            RoboCheckEqInt(recorded.kind, problem.kind);
        }
        RoboCheckEqInt(CssStylesheetDestroy(broken, NULL), RoboOk);
    }

    /* The error slot can be cleared, and clearing it reports "no error". */
    CssStylesheetSetLastError(id, NULL);
    RoboCheckEqInt(CssStylesheetLastError(id, &error), 0);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);

    /* A caller may attach its own explanation, and reading it back is exact. */
    {
        CssError mine = RoboCssCleared;

        CssErrorSet(&mine, RoboCssErrorKindMalformedRule, RoboParseError,
                           NULL, "hand-written explanation");
        CssStylesheetSetLastError(id, &mine);
        RoboCheckEqInt(CssStylesheetLastError(id, &error), 1);
        RoboCheckStrEq(error.message, "hand-written explanation");
        RoboCheck(CssErrorKindIsRecoverable(error.kind));
    }

    /* An id that does not resolve reports NOT_FOUND, and asks for no error. */
    RoboCheckEqInt(CssStylesheetParse((CssStylesheetId)777u,
                                                &error, NULL),
                      RoboNotFound);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);

    /* The error record is optional, and NULL is not dereferenced. */
    RoboCheckEqInt(CssStylesheetParse(id, NULL, NULL), RoboOk);

    /* An origin outside the closed vocabulary is refused, and the parse does
     * not run. */
    {
        CssStylesheetId odd = RoboCssStylesheetIdNone;

        RoboCheckEqInt(CssStylesheetCreate(&odd, RoboCssOriginAuthor, NULL),
                          RoboOk);
        RoboCheckEqInt(CssStylesheetSetOrigin(odd, (CssOrigin)99, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssStylesheetDestroy(odd, NULL), RoboOk);
    }

    RoboCheckEqInt(CssStylesheetDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(CssStylesheetLastError(id, &error), 0);
}

/* ------------------------------------------------------------------ */
/* Parser cursor                                                      */
/* ------------------------------------------------------------------ */

static void TestCssParserCursor(void)
{
    CssSource source = RoboCssCleared;
    CssParser parser = RoboCssCleared;
    CssPosition position;
    CssDiag diag;

    CssFixture();
    CssSourceOfString(&source, "a {\n  b: c;\n}\n");
    CssParserInit(&parser, &source, RoboCssOriginAuthor, NULL);
    RoboCheck(CssParserSource(&parser) == &source);
    RoboCheckEqInt(CssParserOrigin(&parser), RoboCssOriginAuthor);
    RoboCheckEqInt(CssParserOffset(&parser), 0u);
    RoboCheckEqInt(CssParserRemaining(&parser), strlen("a {\n  b: c;\n}\n"));
    RoboCheck(!CssParserAtEnd(&parser));
    RoboCheck(!CssParserHasError(&parser));
    RoboCheck(CssParserError(&parser) == NULL);

    RoboCheckEqInt(CssParserPosition(&parser, &position, NULL), RoboOk);
    RoboCheckEqInt(position.offset, 0u);
    RoboCheckEqInt(position.line, 1u);
    RoboCheckEqInt(position.column, 1u);
    RoboCheck(CssRangeIsEmpty(CssParserCursorRange(&parser)));

    /* Offset 5 is inside the second line, one scalar past its first character,
     * so the cursor is on line 2 at column 2 — columns count scalars, not the
     * bytes of the indent that produced them. */
    RoboCheckEqInt(CssParserAdvance(&parser, 5u, NULL), RoboOk);
    RoboCheckEqInt(CssParserOffset(&parser), 5u);
    RoboCheckEqInt(CssParserPosition(&parser, &position, NULL), RoboOk);
    RoboCheckEqInt(position.line, 2u);
    RoboCheckEqInt(position.column, 2u);

    RoboCheckEqInt(CssParserAdvance(&parser, 0u, NULL), RoboOk);
    RoboCheckEqInt(CssParserOffset(&parser), 5u);

    /* Seeking is absolute and refuses to go past the end... */
    RoboCheckEqInt(CssParserSeek(&parser, strlen("a {\n  b: c;\n}\n"), NULL),
                      RoboOk);
    RoboCheck(CssParserAtEnd(&parser));
    RoboCheckEqInt(CssParserRemaining(&parser), 0u);
    RoboCheckEqInt(CssParserSeek(&parser, strlen("a {\n  b: c;\n}\n") + 1u,
                                           NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssParserOffset(&parser), strlen("a {\n  b: c;\n}\n"));

    /* ...while advancing past the end lands at the end and says so, so a
     * recovery path always continues from a defined place. */
    CssDiagReset(&diag);
    RoboCheckEqInt(CssParserAdvance(&parser, 3u, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboCssDomainParser);
    RoboCheckEqInt(CssParserOffset(&parser), strlen("a {\n  b: c;\n}\n"));

    RoboCheckEqInt(CssParserReset(&parser, NULL), RoboOk);
    RoboCheckEqInt(CssParserOffset(&parser), 0u);
    RoboCheck(!CssParserAtEnd(&parser));

    RoboCheckEqInt(CssParserSeek(&parser, 99u, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssParserInit(&parser, &source, (CssOrigin)42,
                                           NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssParserInit(&parser, NULL, RoboCssOriginAuthor, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssParserInit(NULL, &source, RoboCssOriginAuthor, NULL),
                      RoboInvalidArgument);

    /* An empty source is a legal cursor: at the end from the start. */
    RoboCheckEqInt(CssParserInit(&parser, &source, RoboCssOriginAuthor,
                                           NULL),
                      RoboOk);
    CssSourceDispose(&source);
    RoboCheckEqInt(CssParserInit(&parser, &source, RoboCssOriginAuthor,
                                           NULL),
                      RoboOk);
    RoboCheck(CssParserAtEnd(&parser));
    RoboCheckEqInt(CssParserPosition(&parser, &position, NULL), RoboOk);
    RoboCheckEqInt(position.line, 1u);
    RoboCheckEqInt(position.column, 1u);

    /* A disposed parser refuses work rather than reading through a stale
     * source pointer. */
    CssParserDispose(&parser);
    RoboCheck(CssParserSource(&parser) == NULL);
    RoboCheckEqInt(CssParserOrigin(&parser), RoboCssOriginNone);
    RoboCheck(CssParserAtEnd(&parser));
    RoboCheckEqInt(CssParserRemaining(&parser), 0u);
    RoboCheckEqInt(CssParserReset(&parser, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssParserAdvance(&parser, 1u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssParserSeek(&parser, 0u, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssParserPosition(&parser, &position, NULL),
                      RoboInvalidArgument);
    CssParserDispose(&parser);
    CssParserDispose(NULL);

    RoboCheck(CssParserSource(NULL) == NULL);
    RoboCheckEqInt(CssParserOffset(NULL), 0u);
    RoboCheck(CssParserAtEnd(NULL));
    RoboCheckEqInt(CssParserRemaining(NULL), 0u);
    RoboCheck(!CssParserHasError(NULL));
    RoboCheck(CssParserError(NULL) == NULL);
    RoboCheck(!CssParserRecordError(NULL, NULL));
    RoboCheck(!CssParserFail(NULL, RoboCssErrorKindUnexpectedEof,
                                     RoboParseError, "x"));
    RoboCheck(!CssParserTakeError(NULL, NULL));
    RoboCheckEqInt(CssParserCursorRange(NULL).begin, 0u);
}

static void TestCssParserFirstError(void)
{
    CssSource source = RoboCssCleared;
    CssParser parser = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssError other = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "p {\n  color: red;\n}\n");
    RoboCheckEqInt(CssParserInit(&parser, &source, RoboCssOriginAuthor,
                                           NULL),
                      RoboOk);

    RoboCheckEqInt(CssParserSeek(&parser, 4u, NULL), RoboOk);
    RoboCheck(CssParserFail(&parser, RoboCssErrorKindMalformedDeclaration,
                                    RoboParseError, "expected a colon"));
    RoboCheck(CssParserHasError(&parser));
    RoboCheck(CssParserError(&parser) != NULL);
    RoboCheckEqInt(CssParserError(&parser)->kind,
                      RoboCssErrorKindMalformedDeclaration);
    /* The error is positioned where the cursor was, in line and column terms. */
    RoboCheckEqInt(CssParserError(&parser)->position.offset, 4u);
    RoboCheckEqInt(CssParserError(&parser)->position.line, 2u);
    RoboCheckEqInt(CssParserError(&parser)->position.column, 1u);

    /* The first error is the interesting one: later ones do not overwrite it,
     * because after a recovery path has skipped three constructs the useful
     * report is still the first problem. */
    RoboCheck(!CssParserFail(&parser, RoboCssErrorKindUnexpectedEof,
                                     RoboParseError, "later"));
    RoboCheckEqInt(CssParserError(&parser)->kind,
                      RoboCssErrorKindMalformedDeclaration);
    RoboCheckStrEq(CssParserError(&parser)->message, "expected a colon");

    /* The record can be taken, which clears it. */
    RoboCheck(CssParserTakeError(&parser, &error));
    RoboCheckEqInt(error.kind, RoboCssErrorKindMalformedDeclaration);
    RoboCheck(!CssParserHasError(&parser));
    RoboCheck(CssParserError(&parser) == NULL);
    RoboCheck(!CssParserTakeError(&parser, &error));

    /* A record with kind NONE is not an error and is not recorded: a parser
     * that accepted input has nothing to report. */
    CssErrorInit(&other);
    RoboCheck(!CssParserRecordError(&parser, &other));
    RoboCheck(!CssParserHasError(&parser));
    CssErrorSet(&other, RoboCssErrorKindMalformedRule, RoboParseError,
                       NULL, "no rule here");
    RoboCheck(CssParserRecordError(&parser, &other));
    RoboCheck(CssParserHasError(&parser));
    RoboCheckEqInt(CssParserError(&parser)->kind,
                      RoboCssErrorKindMalformedRule);

    /* Resetting clears the error as well as the cursor. */
    RoboCheckEqInt(CssParserReset(&parser, NULL), RoboOk);
    RoboCheck(!CssParserHasError(&parser));
    RoboCheckEqInt(CssParserOffset(&parser), 0u);

    /* A failed parse call still positions its error at the cursor. Offset 18 is
     * the closing brace, which is on the third line of the source. */
    RoboCheckEqInt(CssParserSeek(&parser, 18u, NULL), RoboOk);
    RoboCheck(CssParserFail(&parser, RoboCssErrorKindUnexpectedEof,
                                    RoboParseError, "unterminated block"));
    RoboCheckEqInt(CssParserError(&parser)->position.offset, 18u);
    RoboCheckEqInt(CssParserError(&parser)->position.line, 3u);
    /* Taking an error leaves the cursor where the failure happened. */
    RoboCheckEqInt(CssParserOffset(&parser), 18u);

    CssParserDispose(&parser);
    CssSourceDispose(&source);
}

static void TestCssStylesheetLifetimeBalances(void)
{
    int weEnabled;
    size_t baseline;

    CssFixture();
    weEnabled = CssAllocatorEnsureEnabled();
    baseline = MemDebugLiveCount();
    {
        int i;

        for (i = 0; i < 16; i++) {
            CssStylesheetId id = RoboCssStylesheetIdNone;
            CssRule styleRule = RoboCssCleared;
            CssRule atRule = RoboCssCleared;
            CssDeclaration declaration = RoboCssCleared;
            CssValue value = RoboCssCleared;

            RoboCheckEqInt(CssStylesheetCreate(&id, RoboCssOriginAuthor,
                                                         NULL),
                              RoboOk);
            RoboCheckEqInt(CssStylesheetCopySource(
                                  id, (const uint8_t *)CSS_FIXTURE_CLASS,
                                  strlen(CSS_FIXTURE_CLASS), NULL),
                              RoboOk);

            RoboCheckEqInt(CssRuleMakeStyle(&styleRule, NULL), RoboOk);
            CssDeclarationMake(&declaration, CssSliceFromText("padding"),
                                      RoboCssDeclarationStateParsed, NULL);
            RoboCheckEqInt(CssValueMakeList(&value, NULL), RoboOk);
            RoboCheckEqInt(CssDeclarationSetValue(&declaration, &value, NULL),
                              RoboOk);
            RoboCheckEqInt(CssStyleRuleAppendDeclaration(&styleRule.style,
                                                                     &declaration, NULL),
                              RoboOk);
            RoboCheckEqInt(CssStylesheetAppendRule(id, &styleRule, NULL),
                              RoboOk);

            RoboCheckEqInt(CssRuleMakeAt(&atRule,
                                                    CssSliceFromText("media"),
                                                    NULL),
                              RoboOk);
            RoboCheckEqInt(CssAtRuleSetPrelude(&atRule.at,
                                                           CssSliceEmpty(), 1,
                                                           NULL),
                              RoboOk);
            {
                CssRule inner = RoboCssCleared;

                RoboCheckEqInt(CssRuleMakeStyle(&inner, NULL), RoboOk);
                RoboCheckEqInt(CssAtRuleAppendRule(&atRule.at, &inner,
                                                               NULL),
                                  RoboOk);
            }
            RoboCheckEqInt(CssStylesheetAppendRule(id, &atRule, NULL),
                              RoboOk);
            RoboCheckEqInt(CssStylesheetTreeRuleCount(id), 3u);

            RoboCheckEqInt(CssStylesheetDestroy(id, NULL), RoboOk);
            RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
        }
    }
    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    CssAllocatorRestore(weEnabled);
    RoboCheckEqInt(CssLiveStylesheetCount(), 0u);
}

void TestRegisterCssSheet(void);

void TestRegisterCssSheet(void)
{
    TestRegister("css.rule.kinds", TestCssRuleKinds);
    TestRegister("css.rule.style_rule", TestCssRuleStyleRule);
    TestRegister("css.rule.at_rule", TestCssRuleAtRule);
    TestRegister("css.rule.list", TestCssRuleList);
    TestRegister("css.sheet.lifecycle", TestCssStylesheetLifecycle);
    TestRegister("css.sheet.origin_state_name",
                       TestCssStylesheetOriginStateName);
    TestRegister("css.sheet.source_attachment",
                       TestCssStylesheetSourceAttachment);
    TestRegister("css.sheet.rule_storage", TestCssStylesheetRuleStorage);
    TestRegister("css.sheet.parse_boundary", TestCssStylesheetParseBoundary);
    TestRegister("css.parser.cursor", TestCssParserCursor);
    TestRegister("css.parser.first_error", TestCssParserFirstError);
    TestRegister("css.sheet.lifetime_balances",
                       TestCssStylesheetLifetimeBalances);
}
