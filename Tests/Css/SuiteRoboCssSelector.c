/*
 * RoboCSS Phase 9.1 selector suite:
 * the simple/compound/complex/list structure and its vocabulary.
 *
 * Nothing here matches anything. The assertions are about what a selector
 * *records* — which is the whole contract this phase offers — and about the
 * ownership rules the 9.4 matcher and the 9.6 specificity calculation will be
 * written against.
 */

#include "RoboCssTestSupport.h"

static void TestCssSelectorVocabulary(void)
{
    CssSimpleSelectorKind kind;
    CssAttributeMatch match;
    CssAttributeCase flag;
    CssCombinator combinator;

    for (kind = RoboCssSimpleSelectorNone;
         kind < (CssSimpleSelectorKind)RoboCssSimpleSelectorKindCount;
         kind = (CssSimpleSelectorKind)(kind + 1)) {
        RoboCheck(CssSimpleSelectorKindIsKnown(kind));
        RoboCheck(strcmp(CssSimpleSelectorKindName(kind), "unknown") != 0);
    }
    RoboCheck(!CssSimpleSelectorKindIsKnown(
        (CssSimpleSelectorKind)77));
    RoboCheckStrEq(CssSimpleSelectorKindName(
                          (CssSimpleSelectorKind)77),
                      "unknown");

    /* All seven comparison operators CSS defines, plus "no comparison". */
    for (match = RoboCssAttributeMatchNone;
         match < (CssAttributeMatch)RoboCssAttributeMatchCount;
         match = (CssAttributeMatch)(match + 1)) {
        RoboCheck(CssAttributeMatchIsKnown(match));
        RoboCheck(strcmp(CssAttributeMatchName(match), "unknown") != 0);
    }
    RoboCheckEqInt((int)RoboCssAttributeMatchCount, 7);
    RoboCheckStrEq(CssAttributeMatchName(RoboCssAttributeMatchIncludes),
                      "includes");
    RoboCheckStrEq(CssAttributeMatchName(RoboCssAttributeMatchDash),
                      "dash");
    RoboCheck(!CssAttributeMatchIsKnown((CssAttributeMatch)42));

    for (flag = RoboCssAttributeCaseDefault;
         flag < (CssAttributeCase)RoboCssAttributeCaseCount;
         flag = (CssAttributeCase)(flag + 1)) {
        RoboCheck(CssAttributeCaseIsKnown(flag));
        RoboCheck(strcmp(CssAttributeCaseName(flag), "unknown") != 0);
    }
    RoboCheckStrEq(CssAttributeCaseName(
                          RoboCssAttributeCaseInsensitive),
                      "insensitive");
    RoboCheck(!CssAttributeCaseIsKnown((CssAttributeCase)42));

    /* Five combinators plus "none"; the column combinator is included because
     * modern CSS has it and a model that omitted it would need a redesign. */
    for (combinator = RoboCssCombinatorNone;
         combinator < (CssCombinator)RoboCssCombinatorCount;
         combinator = (CssCombinator)(combinator + 1)) {
        RoboCheck(CssCombinatorIsKnown(combinator));
        RoboCheck(strcmp(CssCombinatorName(combinator), "unknown") != 0);
    }
    RoboCheckEqInt((int)RoboCssCombinatorCount, 6);
    RoboCheckStrEq(CssCombinatorName(RoboCssCombinatorNextSibling),
                      "next-sibling");
    RoboCheckStrEq(CssCombinatorName(RoboCssCombinatorColumn), "column");
    RoboCheck(!CssCombinatorIsKnown((CssCombinator)42));
}

static void TestCssSelectorSimple(void)
{
    CssSimpleSelector selector = RoboCssCleared;
    CssDiag diag;

    CssFixture();
    CssSimpleSelectorInit(&selector);
    RoboCheck(!CssSimpleSelectorIsSet(&selector));
    RoboCheckEqInt(selector.kind, RoboCssSimpleSelectorNone);

    RoboCheckEqInt(CssSimpleSelectorMake(&selector,
                                                    RoboCssSimpleSelectorType,
                                                    CssSliceFromText("div"),
                                                    NULL),
                      RoboOk);
    RoboCheck(CssSimpleSelectorIsSet(&selector));
    RoboCheckEqInt(selector.kind, RoboCssSimpleSelectorType);
    RoboCheck(CssSliceEqualsAscii(selector.name, "div"));

    /* Making a selector again replaces the previous one rather than stacking. */
    RoboCheckEqInt(CssSimpleSelectorMake(&selector,
                                                    RoboCssSimpleSelectorClass,
                                                    CssSliceFromText("card"),
                                                    NULL),
                      RoboOk);
    RoboCheckEqInt(selector.kind, RoboCssSimpleSelectorClass);
    RoboCheck(CssSliceEqualsAscii(selector.name, "card"));

    /* `*` carries no name; the kinds that name something require one. */
    RoboCheckEqInt(CssSimpleSelectorMake(&selector,
                                                    RoboCssSimpleSelectorUniversal,
                                                    CssSliceEmpty(), NULL),
                      RoboOk);
    RoboCheckEqInt(selector.kind, RoboCssSimpleSelectorUniversal);
    RoboCheckEqInt(CssSimpleSelectorMake(&selector,
                                                    RoboCssSimpleSelectorId,
                                                    CssSliceEmpty(), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(selector.kind, RoboCssSimpleSelectorUniversal);

    CssDiagReset(&diag);
    RoboCheckEqInt(CssSimpleSelectorMake(&selector,
                                                    RoboCssSimpleSelectorNone,
                                                    CssSliceFromText("x"), &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboCssDomainSelector);
    RoboCheckEqInt(CssSimpleSelectorMake(&selector,
                                                    (CssSimpleSelectorKind)77,
                                                    CssSliceFromText("x"), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSimpleSelectorMake(NULL,
                                                    RoboCssSimpleSelectorType,
                                                    CssSliceFromText("x"), NULL),
                      RoboInvalidArgument);

    CssSimpleSelectorDispose(&selector);
    RoboCheck(!CssSimpleSelectorIsSet(&selector));
    CssSimpleSelectorDispose(NULL);
    RoboCheck(!CssSimpleSelectorIsSet(NULL));
    CssSimpleSelectorInit(NULL);
}

static void TestCssSelectorAttribute(void)
{
    CssSimpleSelector selector = RoboCssCleared;

    CssFixture();
    RoboCheckEqInt(CssSimpleSelectorMake(&selector,
                                                    RoboCssSimpleSelectorAttribute,
                                                    CssSliceEmpty(), NULL),
                      RoboOk);

    /* Presence: `[disabled]` compares against nothing. */
    RoboCheckEqInt(CssSimpleSelectorSetAttribute(
                          &selector, CssSliceFromText("disabled"),
                          RoboCssAttributeMatchNone, CssSliceEmpty(),
                          RoboCssAttributeCaseDefault, NULL),
                      RoboOk);
    RoboCheckEqInt(selector.match, RoboCssAttributeMatchNone);
    RoboCheck(CssSliceIsAbsent(selector.attributeValue));

    /* `[type=text i]`: a comparison, its value, and its case flag. */
    RoboCheckEqInt(CssSimpleSelectorSetAttribute(
                          &selector, CssSliceFromText("type"),
                          RoboCssAttributeMatchEquals,
                          CssSliceFromText("text"),
                          RoboCssAttributeCaseInsensitive, NULL),
                      RoboOk);
    RoboCheckEqInt(selector.match, RoboCssAttributeMatchEquals);
    RoboCheck(CssSliceEqualsAscii(selector.attribute, "type"));
    RoboCheck(CssSliceEqualsAscii(selector.attributeValue, "text"));
    RoboCheckEqInt(selector.caseFlag, RoboCssAttributeCaseInsensitive);
    selector.valueWasString = 1;
    RoboCheckEqInt(selector.valueWasString, 1);

    /* `[a=""]` is not `[a]`: this comparison has a value that is present and
     * empty, and it is recorded as an empty slice — not as an absent one. */
    RoboCheckEqInt(CssSimpleSelectorSetAttribute(
                          &selector, CssSliceFromText("a"),
                          RoboCssAttributeMatchEquals,
                          CssSliceFromBytes((const uint8_t *)"", 0u),
                          RoboCssAttributeCaseDefault, NULL),
                      RoboOk);
    RoboCheckEqInt(selector.match, RoboCssAttributeMatchEquals);
    RoboCheck(CssSliceIsEmpty(selector.attributeValue));
    RoboCheck(!CssSliceIsAbsent(selector.attributeValue));
    RoboCheckEqInt(selector.attributeValue.length, 0u);

    /* Each rejection below must leave the record it already described alone,
     * so the checks that follow each one ask what the selector still says. */
    RoboCheckEqInt(CssSimpleSelectorSetAttribute(
                          &selector, CssSliceEmpty(),
                          RoboCssAttributeMatchNone, CssSliceEmpty(),
                          RoboCssAttributeCaseDefault, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSimpleSelectorSetAttribute(
                          &selector, CssSliceFromText("a"),
                          (CssAttributeMatch)42, CssSliceEmpty(),
                          RoboCssAttributeCaseDefault, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSimpleSelectorSetAttribute(
                          &selector, CssSliceFromText("a"),
                          RoboCssAttributeMatchEquals,
                          CssSliceFromBytes((const uint8_t *)"", 0u),
                          (CssAttributeCase)42, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(selector.caseFlag, RoboCssAttributeCaseDefault);

    /* A comparison with no value at all is refused: absent is not empty, and
     * only the first form may ever appear in a selector with an operator. */
    RoboCheckEqInt(CssSimpleSelectorSetAttribute(
                          &selector, CssSliceFromText("a"),
                          RoboCssAttributeMatchPrefix, CssSliceEmpty(),
                          RoboCssAttributeCaseDefault, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(selector.match, RoboCssAttributeMatchEquals);

    /* Presence is the absence of a comparison, so `[a]` carries no value. */
    RoboCheckEqInt(CssSimpleSelectorSetAttribute(
                          &selector, CssSliceFromText("a"),
                          RoboCssAttributeMatchNone, CssSliceEmpty(),
                          RoboCssAttributeCaseDefault, NULL),
                      RoboOk);
    RoboCheckEqInt(selector.match, RoboCssAttributeMatchNone);
    RoboCheck(CssSliceIsAbsent(selector.attributeValue));

    /* Setting an attribute on a non-attribute selector is refused, and the
     * record it already described is untouched. */
    {
        CssSimpleSelector typeSelector = RoboCssCleared;

        RoboCheckEqInt(CssSimpleSelectorMake(&typeSelector,
                                                        RoboCssSimpleSelectorType,
                                                        CssSliceFromText("input"),
                                                        NULL),
                          RoboOk);
        RoboCheckEqInt(CssSimpleSelectorSetAttribute(
                              &typeSelector, CssSliceFromText("type"),
                              RoboCssAttributeMatchEquals,
                              CssSliceFromText("text"),
                              RoboCssAttributeCaseDefault, NULL),
                          RoboInvalidArgument);
        RoboCheck(CssSliceIsEmpty(typeSelector.attribute));
        RoboCheck(CssSliceEqualsAscii(typeSelector.name, "input"));
    }

    RoboCheckEqInt(CssSimpleSelectorSetAttribute(
                          NULL, CssSliceFromText("a"),
                          RoboCssAttributeMatchNone, CssSliceEmpty(),
                          RoboCssAttributeCaseDefault, NULL),
                      RoboInvalidArgument);

    CssSimpleSelectorDispose(&selector);
}

static void TestCssSelectorPseudoArgument(void)
{
    CssSimpleSelector selector = RoboCssCleared;

    CssFixture();
    RoboCheckEqInt(CssSimpleSelectorMake(&selector,
                                                    RoboCssSimpleSelectorPseudoClass,
                                                    CssSliceFromText("nth-child"),
                                                    NULL),
                      RoboOk);
    /* The argument stays one raw slice: whether it is an An+B expression or a
     * selector list depends on which pseudo-class it is, and splitting it here
     * would be parsing. */
    RoboCheckEqInt(CssSimpleSelectorSetArgument(
                          &selector, CssSliceFromText("2n+1"), NULL),
                      RoboOk);
    RoboCheck(CssSliceEqualsAscii(selector.argument, "2n+1"));
    RoboCheckEqInt(CssSimpleSelectorSetArgument(&selector,
                                                            CssSliceEmpty(),
                                                            NULL),
                      RoboOk);
    RoboCheck(CssSliceIsEmpty(selector.argument));

    RoboCheckEqInt(CssSimpleSelectorMake(&selector,
                                                    RoboCssSimpleSelectorPseudoElement,
                                                    CssSliceFromText("marker"),
                                                    NULL),
                      RoboOk);
    RoboCheckEqInt(CssSimpleSelectorSetArgument(
                          &selector, CssSliceFromText("x"), NULL),
                      RoboOk);

    RoboCheckEqInt(CssSimpleSelectorMake(&selector,
                                                    RoboCssSimpleSelectorClass,
                                                    CssSliceFromText("card"),
                                                    NULL),
                      RoboOk);
    RoboCheckEqInt(CssSimpleSelectorSetArgument(
                          &selector, CssSliceFromText("x"), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSimpleSelectorSetArgument(NULL,
                                                            CssSliceEmpty(),
                                                            NULL),
                      RoboInvalidArgument);

    CssSimpleSelectorDispose(&selector);
}

static void TestCssSelectorCompound(void)
{
    CssSimpleSelector simple = RoboCssCleared;
    CssCompoundSelector compound = RoboCssCleared;

    CssFixture();
    CssCompoundSelectorInit(&compound);
    RoboCheck(!CssCompoundSelectorIsSet(&compound));
    RoboCheckEqInt(CssCompoundSelectorCount(&compound), 0u);
    RoboCheck(CssCompoundSelectorAt(&compound, 0u) == NULL);

    RoboCheckEqInt(CssSimpleSelectorMake(&simple,
                                                    RoboCssSimpleSelectorType,
                                                    CssSliceFromText("input"),
                                                    NULL),
                      RoboOk);
    simple.range = CssRangeSpan(0u, 5u);
    RoboCheckEqInt(CssCompoundSelectorAppend(&compound, &simple, NULL),
                      RoboOk);
    RoboCheckEqInt(CssSimpleSelectorMake(&simple,
                                                    RoboCssSimpleSelectorAttribute,
                                                    CssSliceEmpty(), NULL),
                      RoboOk);
    simple.range = CssRangeSpan(5u, 14u);
    RoboCheckEqInt(CssSimpleSelectorSetAttribute(
                          &simple, CssSliceFromText("type"),
                          RoboCssAttributeMatchEquals,
                          CssSliceFromText("text"),
                          RoboCssAttributeCaseDefault, NULL),
                      RoboOk);
    RoboCheckEqInt(CssSimpleSelectorMake(&simple,
                                                    RoboCssSimpleSelectorPseudoClass,
                                                    CssSliceFromText("disabled"),
                                                    NULL),
                      RoboOk);
    simple.range = CssRangeSpan(20u, 9u);
    RoboCheckEqInt(CssCompoundSelectorAppend(&compound, &simple, NULL),
                      RoboOk);

    /* Order is preserved as written, and the compound seals a range that spans
     * its members rather than one it guessed. */
    RoboCheckEqInt(CssCompoundSelectorCount(&compound), 2u);
    RoboCheck(CssCompoundSelectorIsSet(&compound));
    RoboCheckEqInt(CssCompoundSelectorAt(&compound, 0u)->kind,
                      RoboCssSimpleSelectorType);
    RoboCheckEqInt(CssCompoundSelectorAt(&compound, 1u)->kind,
                      RoboCssSimpleSelectorPseudoClass);
    RoboCheck(CssCompoundSelectorAt(&compound, 2u) == NULL);
    CssCompoundSelectorSealRange(&compound);
    RoboCheckEqInt(compound.range.begin, 0u);
    RoboCheckEqInt(compound.range.end, 29u);

    /* A simple selector is copied in, so the caller's record is still usable. */
    RoboCheck(CssSimpleSelectorIsSet(&simple));
    RoboCheckEqInt(CssCompoundSelectorAppend(&compound, &simple, NULL),
                      RoboOk);
    RoboCheckEqInt(CssCompoundSelectorCount(&compound), 3u);

    /* An unset simple selector is refused. */
    {
        CssSimpleSelector unset = RoboCssCleared;

        CssSimpleSelectorInit(&unset);
        RoboCheckEqInt(CssCompoundSelectorAppend(&compound, &unset, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssCompoundSelectorCount(&compound), 3u);
    }
    RoboCheckEqInt(CssCompoundSelectorAppend(NULL, &simple, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssCompoundSelectorAppend(&compound, NULL, NULL),
                      RoboInvalidArgument);

    CssCompoundSelectorDispose(&compound);
    RoboCheckEqInt(CssCompoundSelectorCount(&compound), 0u);
    CssCompoundSelectorSealRange(&compound);
    RoboCheckEqInt(compound.range.begin, 0u);
    CssCompoundSelectorDispose(NULL);
    CssCompoundSelectorInit(NULL);
    RoboCheck(!CssCompoundSelectorIsSet(NULL));
    RoboCheckEqInt(CssCompoundSelectorCount(NULL), 0u);
    RoboCheck(CssCompoundSelectorAt(NULL, 0u) == NULL);
}

static void TestCssSelectorComplex(void)
{
    CssSource source = RoboCssCleared;
    CssComplexSelector complex = RoboCssCleared;
    CssCompoundSelector compound = RoboCssCleared;
    CssSimpleSelector simple = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "form > input + label");
    CssComplexSelectorInit(&complex);
    RoboCheckEqInt(CssComplexSelectorCompoundCount(&complex), 0u);
    RoboCheck(!CssComplexSelectorIsWellFormed(&complex));

    /* Build `form > input + label` the way the parser will: compound,
     * combinator, compound, combinator, compound. */
    {
        static const char *tags[] = {"form", "input", "label"};
        static const CssCombinator combinators[] = {
            RoboCssCombinatorChild, RoboCssCombinatorNextSibling};
        int i;

        for (i = 0; i < 3; i++) {
            CssCompoundSelectorInit(&compound);
            RoboCheckEqInt(CssSimpleSelectorMake(&simple,
                                                            RoboCssSimpleSelectorType,
                                                            CssSliceFromText(tags[i]),
                                                            NULL),
                              RoboOk);
            RoboCheckEqInt(CssCompoundSelectorAppend(&compound, &simple,
                                                                NULL),
                              RoboOk);
            CssCompoundSelectorSealRange(&compound);
            /* The compound is moved in: the caller's record is empty afterwards. */
            RoboCheckEqInt(CssComplexSelectorAppendCompound(&complex,
                                                                       &compound,
                                                                       NULL),
                              RoboOk);
            RoboCheckEqInt(CssCompoundSelectorCount(&compound), 0u);
            if (i < 2) {
                RoboCheckEqInt(CssComplexSelectorAppendCombinator(
                                      &complex, combinators[i], NULL),
                                  RoboOk);
            }
        }
    }
    RoboCheckEqInt(CssComplexSelectorCompoundCount(&complex), 3u);
    RoboCheckEqInt(CssComplexSelectorCombinatorCount(&complex), 2u);
    RoboCheck(CssComplexSelectorIsWellFormed(&complex));
    RoboCheckEqInt(CssComplexSelectorCombinatorAt(&complex, 0u),
                      RoboCssCombinatorChild);
    RoboCheckEqInt(CssComplexSelectorCombinatorAt(&complex, 1u),
                      RoboCssCombinatorNextSibling);
    RoboCheckEqInt(CssComplexSelectorCombinatorAt(&complex, 2u),
                      RoboCssCombinatorNone);
    RoboCheck(CssComplexSelectorCompoundAt(&complex, 2u) != NULL);
    RoboCheck(CssComplexSelectorCompoundAt(&complex, 3u) == NULL);

    RoboCheckEqInt(CssComplexSelectorAppendCombinator(
                          &complex, RoboCssCombinatorNone, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssComplexSelectorAppendCombinator(
                          &complex, (CssCombinator)42, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssComplexSelectorAppendCombinator(NULL,
                                                                  RoboCssCombinatorChild,
                                                                  NULL),
                      RoboInvalidArgument);

    /* An empty compound is refused: a selector with no conditions matches
     * everything, which is what `*` is for and must be spelled as such. */
    {
        CssCompoundSelector empty = RoboCssCleared;

        CssCompoundSelectorInit(&empty);
        RoboCheckEqInt(CssComplexSelectorAppendCompound(&complex, &empty,
                                                                    NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssComplexSelectorCompoundCount(&complex), 3u);
    }
    RoboCheckEqInt(CssComplexSelectorAppendCompound(NULL, &compound, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssComplexSelectorAppendCompound(&complex, NULL, NULL),
                      RoboInvalidArgument);

    /* One compound and no combinator is well formed; one compound and one
     * combinator is not, and the check says so rather than being skipped. */
    {
        CssComplexSelector single = RoboCssCleared;

        CssComplexSelectorInit(&single);
        CssCompoundSelectorInit(&compound);
        RoboCheckEqInt(CssSimpleSelectorMake(&simple,
                                                        RoboCssSimpleSelectorUniversal,
                                                        CssSliceEmpty(), NULL),
                          RoboOk);
        RoboCheckEqInt(CssCompoundSelectorAppend(&compound, &simple, NULL),
                          RoboOk);
        RoboCheckEqInt(CssComplexSelectorAppendCompound(&single, &compound,
                                                                    NULL),
                          RoboOk);
        RoboCheck(CssComplexSelectorIsWellFormed(&single));
        RoboCheckEqInt(CssComplexSelectorAppendCombinator(
                              &single, RoboCssCombinatorDescendant, NULL),
                          RoboOk);
        RoboCheck(!CssComplexSelectorIsWellFormed(&single));
        CssComplexSelectorDispose(&single);
    }

    CssComplexSelectorDispose(&complex);
    RoboCheckEqInt(CssComplexSelectorCompoundCount(&complex), 0u);
    CssComplexSelectorDispose(NULL);
    CssComplexSelectorInit(NULL);
    RoboCheckEqInt(CssComplexSelectorCompoundCount(NULL), 0u);
    RoboCheckEqInt(CssComplexSelectorCombinatorCount(NULL), 0u);
    RoboCheck(CssComplexSelectorCompoundAt(NULL, 0u) == NULL);
    RoboCheck(!CssComplexSelectorIsWellFormed(NULL));
    CssSourceDispose(&source);
}

static void TestCssSelectorList(void)
{
    CssSource source = RoboCssCleared;
    CssSelectorList list = RoboCssCleared;
    CssComplexSelector complex = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "h1, h2, h3");
    CssSelectorListInit(&list);
    RoboCheck(!CssSelectorListIsSet(&list));
    RoboCheckEqInt(CssSelectorListCount(&list), 0u);
    RoboCheck(CssSelectorListAt(&list, 0u) == NULL);

    /* Two entries, second more specific than the first: a selector list is what
     * a comma-separated rule owns, and 9.6 will hang a specificity off each. */
    CssComplexSelectorOf(&complex, &source, "h1", NULL);
    RoboCheckEqInt(CssSelectorListAppend(&list, &complex, NULL), RoboOk);
    RoboCheckEqInt(CssComplexSelectorCompoundCount(&complex), 0u);
    CssComplexSelectorOf(&complex, &source, "h2", "title");
    RoboCheckEqInt(CssSelectorListAppend(&list, &complex, NULL), RoboOk);

    RoboCheckEqInt(CssSelectorListCount(&list), 2u);
    RoboCheck(CssSelectorListIsSet(&list));
    RoboCheck(CssSelectorListIsWellFormed(&list));
    RoboCheckEqInt(CssSelectorListAt(&list, 0u)->compoundCount, 1u);
    RoboCheckEqInt(CssSelectorListAt(&list, 1u)->compoundCount, 1u);
    RoboCheckEqInt(
        CssCompoundSelectorCount(
            CssComplexSelectorCompoundAt(CssSelectorListAt(&list, 0u),
                                                  0u)),
        1u);
    RoboCheckEqInt(
        CssCompoundSelectorCount(
            CssComplexSelectorCompoundAt(CssSelectorListAt(&list, 1u),
                                                  0u)),
        2u);
    RoboCheck(CssSelectorListAt(&list, 2u) == NULL);

    {
        CssComplexSelector empty = RoboCssCleared;

        CssComplexSelectorInit(&empty);
        RoboCheckEqInt(CssSelectorListAppend(&list, &empty, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssSelectorListCount(&list), 2u);
    }
    RoboCheckEqInt(CssSelectorListAppend(NULL, &complex, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSelectorListAppend(&list, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheck(!CssSelectorListIsWellFormed(NULL));
    RoboCheck(!CssSelectorListIsSet(NULL));
    RoboCheckEqInt(CssSelectorListCount(NULL), 0u);
    RoboCheck(CssSelectorListAt(NULL, 0u) == NULL);

    CssSelectorListDispose(&list);
    RoboCheckEqInt(CssSelectorListCount(&list), 0u);
    CssSelectorListDispose(NULL);
    CssSelectorListInit(NULL);
    CssSourceDispose(&source);
}

static void TestCssSelectorLifetimeBalances(void)
{
    int weEnabled;
    size_t baseline;

    CssFixture();
    weEnabled = CssAllocatorEnsureEnabled();
    baseline = MemDebugLiveCount();
    {
        int i;

        for (i = 0; i < 24; i++) {
            CssSelectorList list = RoboCssCleared;
            CssComplexSelector complex = RoboCssCleared;
            CssCompoundSelector compound = RoboCssCleared;
            CssSimpleSelector simple = RoboCssCleared;
            int j;

            CssSelectorListInit(&list);
            for (j = 0; j < 3; j++) {
                CssCompoundSelectorInit(&compound);
                RoboCheckEqInt(CssSimpleSelectorMake(
                                      &simple, RoboCssSimpleSelectorType,
                                      CssSliceFromText("p"), NULL),
                                  RoboOk);
                RoboCheckEqInt(CssCompoundSelectorAppend(&compound, &simple,
                                                                    NULL),
                                  RoboOk);
                RoboCheckEqInt(CssSimpleSelectorMake(
                                      &simple, RoboCssSimpleSelectorClass,
                                      CssSliceFromText("note"), NULL),
                                  RoboOk);
                RoboCheckEqInt(CssCompoundSelectorAppend(&compound, &simple,
                                                                    NULL),
                                  RoboOk);
                CssComplexSelectorInit(&complex);
                RoboCheckEqInt(CssComplexSelectorAppendCompound(&complex,
                                                                            &compound,
                                                                            NULL),
                                  RoboOk);
                RoboCheckEqInt(CssSelectorListAppend(&list, &complex, NULL),
                                  RoboOk);
            }
            RoboCheck(CssSelectorListIsWellFormed(&list));
            RoboCheckEqInt(CssSelectorListCount(&list), 3u);
            CssSelectorListDispose(&list);
            RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
        }
    }
    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    CssAllocatorRestore(weEnabled);
}

void TestRegisterCssSelector(void);

void TestRegisterCssSelector(void)
{
    TestRegister("css.selector.vocabulary", TestCssSelectorVocabulary);
    TestRegister("css.selector.simple", TestCssSelectorSimple);
    TestRegister("css.selector.attribute", TestCssSelectorAttribute);
    TestRegister("css.selector.pseudo_argument",
                       TestCssSelectorPseudoArgument);
    TestRegister("css.selector.compound", TestCssSelectorCompound);
    TestRegister("css.selector.complex", TestCssSelectorComplex);
    TestRegister("css.selector.list", TestCssSelectorList);
    TestRegister("css.selector.lifetime_balances",
                       TestCssSelectorLifetimeBalances);
}
