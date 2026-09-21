/*
 * RoboCSS matcher levels (Phase 9.4) — css.select.levels.*
 *
 * RoboCSSMatcher.h exposes four entry points, one per structural level of a
 * selector: a simple selector, a compound, a complex selector, and a list.
 * Every other matcher suite reaches the levels through the *list* entry, which
 * means the three lower doors could be broken, unreachable, or subtly
 * inconsistent with the list and the whole suite would still be green.
 *
 * So this suite is about the doors themselves, and about the two things that
 * only exist because there are four of them:
 *
 *   the levels compose    A complex selector of one compound *is* that
 *                         compound, and a one-selector list *is* that selector.
 *                         Where the levels disagree, a caller that reached for
 *                         one door instead of another would get a different
 *                         stylesheet — so `levels_agree` walks a table of
 *                         (selector, element) pairs and requires the three
 *                         levels to say the same thing.
 *
 *   the levels differ    Where they are *not* interchangeable, the difference is
 *                         deliberate and is pinned here: an ill-formed complex
 *                         selector is refused (-1) by the complex door and
 *                         skipped by the list door, because a rule list is a
 *                         document and a hand-held complex selector is a
 *                         question about one record.
 *
 * The other half is the argument contract of the lower doors. The compound door
 * takes a compound and nothing else; the simple door takes a compound and an
 * index as well, which is how a functional pseudo-class's parsed argument is
 * addressed. Omitted, `:not(...)` must be reported unevaluatable rather than
 * read as "matches" — the failure that would apply exactly the rules the author
 * negated away.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/** The first compound of the first complex selector of @p list. */
static const CssCompoundSelector *FirstCompound(
    const CssSelectorList *list)
{
    return CssComplexSelectorCompoundAt(CssSelectorListAt(list, 0u), 0u);
}

/**
 * Read the reported `condition` before the owning list is released, because that
 * one field borrows from the list's name arena while every other field is a
 * value.
 */
static void ConditionOf(const CssSelectorMatch *result,
                         char *out,
                         size_t capacity)
{
    CssSelectText(result->condition, out, capacity);
}

/* ------------------------------------------------------------------ */
/* The simple door                                                    */
/* ------------------------------------------------------------------ */

/**
 * The simple door answers for one simple selector at a time, so a compound's
 * parts can be asked about independently — which is what makes the compound
 * rule ("all of them hold") checkable rather than assumed.
 */
static void TestCssLevelsSimpleKinds(void)
{
    CssHost host;
    CssSelectorList list = RoboCssCleared;
    const CssCompoundSelector *compound;
    CssElementRef card;
    CssElementRef plain;
    CssSelectorMatch out;
    size_t i;

    CssHostInit(&host);
    card = CssHostAdd(&host, "div", "app", "card primary");
    plain = CssHostAdd(&host, "span", NULL, NULL);
    CssHostSeal(&host);

    CssSelectParse("div.card#app", &list);
    compound = FirstCompound(&list);
    RoboCheckEqInt(CssCompoundSelectorCount(compound), 3u);

    /* Each simple selector is true of the card ... */
    for (i = 0u; i < 3u; i++) {
        CssSelectorMatchInit(&out);
        RoboCheckEqInt(CssSimpleSelectorMatches(
                              CssCompoundSelectorAt(compound, i), compound, i,
                              &host.host, card, &out, NULL),
                          1);
        RoboCheckEqInt(out.matched, 1);
        RoboCheckEqInt(out.unsupported, 0);
    }
    /* ... and none of them is true of the span. */
    for (i = 0u; i < 3u; i++) {
        CssSelectorMatchInit(&out);
        RoboCheckEqInt(CssSimpleSelectorMatches(
                              CssCompoundSelectorAt(compound, i), compound, i,
                              &host.host, plain, &out, NULL),
                          0);
        RoboCheckEqInt(out.matched, 0);
        /* A decided "no" is not an unevaluatable one. */
        RoboCheckEqInt(out.unsupported, 0);
    }

    CssSelectorListDispose(&list);
}

static void TestCssLevelsSimpleUniversal(void)
{
    CssHost host;
    CssSelectorList list = RoboCssCleared;
    const CssCompoundSelector *compound;
    CssSelectorMatch out;

    CssHostInit(&host);
    CssHostAdd(&host, "div", NULL, NULL);
    CssHostSeal(&host);

    CssSelectParse("*", &list);
    compound = FirstCompound(&list);
    RoboCheckEqInt(CssCompoundSelectorAt(compound, 0u)->kind,
                      (int)RoboCssSimpleSelectorUniversal);
    RoboCheckEqInt(CssSimpleSelectorMatches(
                          CssCompoundSelectorAt(compound, 0u), compound, 0u,
                          &host.host, 1u, &out, NULL),
                      1);

    CssSelectorListDispose(&list);
}

/**
 * The simple door's argument contract, and the reason it takes a compound and an
 * index at all.
 *
 * `:nth-child()` carries everything it needs in its own record, so it is
 * answerable without the compound. `:not(...)` does not: its argument is a
 * parsed selector list owned by the *compound*, and the index is how the simple
 * selector finds it. Handed neither, `:not(p)` must be unevaluatable — never
 * true, because a `:not()` read as "matches" applies precisely the rules the
 * author wrote to exclude.
 */
static void TestCssLevelsSimplePseudoArguments(void)
{
    CssHost host;
    CssSelectorList list = RoboCssCleared;
    const CssCompoundSelector *compound;
    CssSelectorMatch out;
    CssElementRef container;
    CssElementRef p;
    CssElementRef span;
    char condition[32];

    /* Two real siblings: `:nth-child()` counts a parent's children, and the
     * fixture has to have a parent for the question to have an answer. */
    CssHostInit(&host);
    container = CssHostAdd(&host, "div", NULL, NULL);
    p = CssHostAdd(&host, "p", NULL, NULL);
    span = CssHostAdd(&host, "span", NULL, NULL);
    CssHostSetParent(&host, p, container);
    CssHostSetParent(&host, span, container);
    CssHostSeal(&host);

    /* `:nth-child(2)` needs no compound: `true` for the second element, and
     * false for the first — a decided answer either way. */
    CssSelectParse(":nth-child(2)", &list);
    compound = FirstCompound(&list);
    RoboCheckEqInt(CssSimpleSelectorMatches(
                          CssCompoundSelectorAt(compound, 0u), NULL, 0u,
                          &host.host, span, &out, NULL),
                      1);
    RoboCheckEqInt(out.unsupported, 0);
    RoboCheckEqInt(CssSimpleSelectorMatches(
                          CssCompoundSelectorAt(compound, 0u), compound, 0u,
                          &host.host, p, &out, NULL),
                      0);
    RoboCheckEqInt(out.unsupported, 0);
    CssSelectorListDispose(&list);

    /* A `:nth-child()` whose argument was never parsed is a different matter: the
     * condition exists, the record just does not say which child. */
    {
        CssSimpleSelector handMade;
        CssSimpleSelector *made = &handMade;

        CssSimpleSelectorInit(made);
        RoboCheckEqInt(CssSimpleSelectorMake(
                              made, RoboCssSimpleSelectorPseudoClass,
                              CssSliceFromText("nth-child"), NULL),
                          RoboOk);
        RoboCheckEqInt(CssSimpleSelectorHasNth(made), 0);
        RoboCheckEqInt(CssSimpleSelectorMatches(made, NULL, 0u, &host.host,
                                                          span, &out, NULL),
                          0);
        RoboCheckEqInt(out.unsupported, 1);
        ConditionOf(&out, condition, sizeof(condition));
        RoboCheckStrEq(condition, "nth-child");
        CssSimpleSelectorDispose(made);
    }

    /* `:not(p)`: decided when the compound is handed over, unevaluatable when it
     * is not. The two must not collapse into one answer. */
    CssSelectParse(":not(p)", &list);
    compound = FirstCompound(&list);
    RoboCheckEqInt(CssSimpleSelectorMatches(
                          CssCompoundSelectorAt(compound, 0u), compound, 0u,
                          &host.host, span, &out, NULL),
                      1);
    RoboCheckEqInt(out.unsupported, 0);
    RoboCheckEqInt(CssSimpleSelectorMatches(
                          CssCompoundSelectorAt(compound, 0u), compound, 0u,
                          &host.host, p, &out, NULL),
                      0);
    RoboCheckEqInt(out.unsupported, 0);
    /* Without the compound the argument cannot be found, and the honest answer is
     * "not decided" — for the p as much as for the span, because the negation of
     * an unknown is unknown. */
    RoboCheckEqInt(CssSimpleSelectorMatches(
                          CssCompoundSelectorAt(compound, 0u), NULL, 0u,
                          &host.host, span, &out, NULL),
                      0);
    RoboCheckEqInt(out.unsupported, 1);
    ConditionOf(&out, condition, sizeof(condition));
    RoboCheckStrEq(condition, "not");
    RoboCheckEqInt(CssSimpleSelectorMatches(
                          CssCompoundSelectorAt(compound, 0u), NULL, 0u,
                          &host.host, p, &out, NULL),
                      0);
    RoboCheckEqInt(out.unsupported, 1);
    CssSelectorListDispose(&list);
}

static void TestCssLevelsSimpleArguments(void)
{
    CssHost host;
    CssSelectorList list = RoboCssCleared;
    const CssCompoundSelector *compound;
    CssSelectorMatch out;
    CssElementHost unusable;

    CssHostInit(&host);
    CssHostAdd(&host, "div", NULL, NULL);
    CssHostSeal(&host);
    CssSelectParse("div", &list);
    compound = FirstCompound(&list);

    /* No simple selector is a caller bug, not a selector that fails to match. */
    RoboCheckEqInt(CssSimpleSelectorMatches(NULL, compound, 0u, &host.host,
                                                      1u, &out, NULL),
                      -1);
    /* No host, or a host that can describe nothing, is the same kind of bug — and
     * it is distinguished from the `0` that means "did not match". */
    RoboCheckEqInt(CssSimpleSelectorMatches(
                          CssCompoundSelectorAt(compound, 0u), compound, 0u,
                          NULL, 1u, &out, NULL),
                      -1);
    CssElementHostInit(&unusable);
    RoboCheckEqInt(CssSimpleSelectorMatches(
                          CssCompoundSelectorAt(compound, 0u), compound, 0u,
                          &unusable, 1u, &out, NULL),
                      -1);
    /* `no element` is not an error: no element satisfies nothing. */
    RoboCheckEqInt(CssSimpleSelectorMatches(
                          CssCompoundSelectorAt(compound, 0u), compound, 0u,
                          &host.host, RoboCssElementRefNone, &out, NULL),
                      0);
    RoboCheckEqInt(out.matched, 0);
    RoboCheckEqInt(out.unsupported, 0);
    /* An element the host cannot describe is a candidate that matches nothing. */
    RoboCheckEqInt(CssSimpleSelectorMatches(
                          CssCompoundSelectorAt(compound, 0u), compound, 0u,
                          &host.host, 99u, &out, NULL),
                      0);

    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* The compound door                                                  */
/* ------------------------------------------------------------------ */

static void TestCssLevelsCompoundRequiresEverySimple(void)
{
    CssHost host;
    CssSelectorList list = RoboCssCleared;
    const CssCompoundSelector *compound;
    CssSelectorMatch out;
    CssElementRef all;
    CssElementRef typeOnly;

    CssHostInit(&host);
    all = CssHostAdd(&host, "div", "app", "card");
    typeOnly = CssHostAdd(&host, "div", NULL, NULL);
    CssHostSeal(&host);

    CssSelectParse("div#app.card", &list);
    compound = FirstCompound(&list);
    RoboCheckEqInt(CssCompoundSelectorMatches(compound, &host.host, all,
                                                        &out, NULL),
                      1);
    RoboCheckEqInt(out.matched, 1);
    /* Only the type selector holds of the second element, and a compound is the
     * conjunction of its simple selectors, so it does not match. */
    RoboCheckEqInt(CssCompoundSelectorMatches(compound, &host.host, typeOnly,
                                                        &out, NULL),
                      0);
    RoboCheckEqInt(out.matched, 0);
    RoboCheckEqInt(out.unsupported, 0);

    CssSelectorListDispose(&list);
}

/**
 * A compound short-circuits on its first failing simple selector, which is
 * observable rather than merely efficient.
 *
 * `p:hover` against a div fails on the type test, so the unevaluatable `:hover`
 * is never reached and the compound is *decided* false. `div:hover` against the
 * same div passes the type test, reaches `:hover`, and is *undecided*. Same
 * pseudo-class, same element, opposite epistemic state — which is the only way
 * to see the short-circuit from outside, and the reason the ordering of a
 * compound's simple selectors is a documented property rather than an
 * implementation detail.
 */
static void TestCssLevelsCompoundShortCircuitsOnTheFirstFailure(void)
{
    CssHost host;
    CssSelectorList list = RoboCssCleared;
    const CssCompoundSelector *compound;
    CssSelectorMatch out;
    CssElementRef div;
    char condition[32];

    CssHostInit(&host);
    div = CssHostAdd(&host, "div", NULL, NULL);
    CssHostSeal(&host);

    /* The type test fails first, so the compound is decided false. */
    CssSelectParse("p:hover", &list);
    compound = FirstCompound(&list);
    RoboCheckEqInt(CssCompoundSelectorMatches(compound, &host.host, div,
                                                        &out, NULL),
                      0);
    RoboCheckEqInt(out.matched, 0);
    RoboCheckEqInt(out.unsupported, 0);
    CssSelectorListDispose(&list);

    /* The type test passes, so the unevaluatable pseudo-class is reached. */
    CssSelectParse("div:hover", &list);
    compound = FirstCompound(&list);
    RoboCheckEqInt(CssCompoundSelectorMatches(compound, &host.host, div,
                                                        &out, NULL),
                      0);
    RoboCheckEqInt(out.matched, 0);
    RoboCheckEqInt(out.unsupported, 1);
    ConditionOf(&out, condition, sizeof(condition));
    RoboCheckStrEq(condition, "hover");
    CssSelectorListDispose(&list);

    /* And the same short-circuit inside a list: the decided false comes first, so
     * nothing about `:hover` is reported at all. */
    RoboCheckEqInt(CssMatchCondition(&host, "p:hover, span", div, &out, condition,
                                          sizeof(condition)),
                      0);
    RoboCheckEqInt(out.unsupported, 0);
}

static void TestCssLevelsCompoundEmptyAndNull(void)
{
    CssHost host;
    CssSelectorList list = RoboCssCleared;
    CssCompoundSelector empty;
    CssSelectorMatch out;
    CssElementHost unusable;

    CssHostInit(&host);
    CssHostAdd(&host, "div", NULL, NULL);
    CssHostSeal(&host);

    /* A compound with no simple selectors is true of nothing, not of everything:
     * the empty conjunction is the one place where the mathematical reading is
     * the wrong one for a selector. */
    CssCompoundSelectorInit(&empty);
    RoboCheckEqInt(CssCompoundSelectorIsSet(&empty), 0);
    RoboCheckEqInt(CssCompoundSelectorMatches(&empty, &host.host, 1u, &out,
                                                        NULL),
                      0);
    RoboCheckEqInt(out.matched, 0);

    RoboCheckEqInt(CssCompoundSelectorMatches(NULL, &host.host, 1u, &out,
                                                        NULL),
                      -1);
    RoboCheckEqInt(CssCompoundSelectorMatches(&empty, NULL, 1u, &out, NULL),
                      -1);
    CssElementHostInit(&unusable);
    RoboCheckEqInt(CssCompoundSelectorMatches(&empty, &unusable, 1u, &out,
                                                        NULL),
                      -1);

    CssSelectParse("div", &list);
    RoboCheckEqInt(CssCompoundSelectorMatches(FirstCompound(&list),
                                                        &host.host,
                                                        RoboCssElementRefNone,
                                                        &out, NULL),
                      0);
    RoboCheckEqInt(out.unsupported, 0);
    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* The complex door                                                   */
/* ------------------------------------------------------------------ */

/**
 * Append a `tag` compound, followed by @p trailingCombinators `>`s, to @p list.
 *
 * @p trailingCombinators > 0 produces a complex selector that is structurally
 * incoherent by construction — one compound and more combinators than it can
 * have — which no parsed selector can be, and which the complex door must refuse
 * and the list door must skip.
 */
static Result BuildComplex(CssSelectorList *list,
                                   const char *tag,
                                   size_t trailingCombinators,
                                   CssDiag *diag)
{
    CssComplexSelector complex;
    CssCompoundSelector compound;
    CssSimpleSelector simple;
    Result result;
    size_t i;

    CssComplexSelectorInit(&complex);
    CssCompoundSelectorInit(&compound);
    CssSimpleSelectorInit(&simple);
    result = CssSimpleSelectorMake(&simple, RoboCssSimpleSelectorType,
                                           CssSliceFromText(tag), diag);
    if (result == RoboOk) {
        result = CssCompoundSelectorAppend(&compound, &simple, diag);
    }
    if (result == RoboOk) {
        result = CssComplexSelectorAppendCompound(&complex, &compound, diag);
    }
    for (i = 0u; result == RoboOk && i < trailingCombinators; i++) {
        result = CssComplexSelectorAppendCombinator(
            &complex, RoboCssCombinatorChild, diag);
    }
    if (result == RoboOk) {
        result = CssSelectorListAppend(list, &complex, diag);
    }

    CssCompoundSelectorDispose(&compound);
    CssComplexSelectorDispose(&complex);
    return result;
}

/**
 * A complex selector of one compound is that compound — not "equivalent to", the
 * same question with the same answer, because the walk at index 0 *is* the
 * compound test. A caller choosing between the two doors must not be choosing
 * between two verdicts.
 */
static void TestCssLevelsComplexOfOneCompoundIsThatCompound(void)
{
    CssHost host;
    CssSelectorList list = RoboCssCleared;
    const CssComplexSelector *complex;
    const CssCompoundSelector *compound;
    CssSelectorMatch atComplex;
    CssSelectorMatch atCompound;
    int complexResult;
    int compoundResult;

    CssHostInit(&host);
    CssHostAdd(&host, "div", "app", "card");
    CssHostAdd(&host, "span", NULL, NULL);
    CssHostSeal(&host);

    CssSelectParse("div.card", &list);
    complex = CssSelectorListAt(&list, 0u);
    RoboCheckEqInt(CssComplexSelectorCompoundCount(complex), 1u);
    RoboCheckEqInt(CssComplexSelectorCombinatorCount(complex), 0u);
    compound = CssComplexSelectorCompoundAt(complex, 0u);

    complexResult = CssComplexSelectorMatches(complex, &host.host, 1u,
                                                      &atComplex, NULL);
    compoundResult = CssCompoundSelectorMatches(compound, &host.host, 1u,
                                                        &atCompound, NULL);
    RoboCheckEqInt(complexResult, 1);
    RoboCheckEqInt(compoundResult, complexResult);
    RoboCheckEqInt(atCompound.matched, atComplex.matched);
    RoboCheckEqInt(atCompound.unsupported, atComplex.unsupported);
    /* A matching complex selector reports its own extent. */
    RoboCheckEqInt(atComplex.range.begin, complex->range.begin);
    RoboCheckEqInt(atComplex.range.end, complex->range.end);

    complexResult = CssComplexSelectorMatches(complex, &host.host, 2u,
                                                      &atComplex, NULL);
    compoundResult = CssCompoundSelectorMatches(compound, &host.host, 2u,
                                                        &atCompound, NULL);
    RoboCheckEqInt(complexResult, 0);
    RoboCheckEqInt(compoundResult, complexResult);
    /* Nothing matched, so no range is claimed. */
    RoboCheckEqInt(atComplex.range.begin, 0u);
    RoboCheckEqInt(atComplex.range.end, 0u);

    CssSelectorListDispose(&list);
}

/**
 * A compound count that does not fit the combinator count is a broken *record*,
 * not a selector that fails to match: the complex door refuses it with -1 and
 * leaves the caller in no doubt.
 *
 * The list door cannot do that — a rule list is a document, and one malformed
 * record in it must not cancel the rest — so it skips the member instead. That
 * is the one place the two doors deliberately differ, and it is pinned here so
 * that neither can be "fixed" into the other's behavior by accident.
 */
static void TestCssLevelsIllFormedComplexIsRefusedAndSkipped(void)
{
    CssHost host;
    CssSelectorList list = RoboCssCleared;
    CssSelectorMatch out;
    CssElementRef div;

    CssHostInit(&host);
    div = CssHostAdd(&host, "div", NULL, NULL);
    CssHostAdd(&host, "span", NULL, NULL);
    CssHostSeal(&host);

    /* A standalone incoherent complex selector: one compound, two combinators. */
    CssSelectorListInit(&list);
    RoboCheckEqInt(BuildComplex(&list, "div", 2, NULL), RoboOk);
    {
        const CssComplexSelector *broken = CssSelectorListAt(&list, 0u);

        RoboCheckEqInt(CssComplexSelectorCompoundCount(broken), 1u);
        RoboCheckEqInt(CssComplexSelectorCombinatorCount(broken), 2u);
        RoboCheckEqInt(CssComplexSelectorIsWellFormed(broken), 0);
        /* Refused even though the single compound *would* have matched: the
         * record is broken, which is not the same as a selector that fails. */
        RoboCheckEqInt(CssComplexSelectorMatches(broken, &host.host, div,
                                                           &out, NULL),
                          -1);
    }
    CssSelectorListDispose(&list);

    /*
     * The same shape through a list: the broken member is skipped and the valid
     * member beside it still decides.
     *
     * The list has to be built by hand rather than parsed, and that is itself
     * the finding: the 9.4 parser refuses `div +` outright — a selector list is
     * all-or-nothing — so a list that *parsed* can never contain a complex
     * selector this door would reject. The guard therefore protects the list
     * door against hand-built input, which is exactly what this is.
     */
    (void)CssSelectParseRejected("div +");

    CssSelectorListInit(&list);
    RoboCheckEqInt(BuildComplex(&list, "div", 1, NULL), RoboOk);
    RoboCheckEqInt(BuildComplex(&list, "span", 0, NULL), RoboOk);
    RoboCheckEqInt(CssSelectorListCount(&list), 2u);
    RoboCheckEqInt(CssSelectorListIsWellFormed(&list), 0);
    RoboCheckEqInt(CssSelectorListMatches(&list, &host.host, 2u, &out, NULL),
                      1);
    RoboCheckEqInt(out.selectorIndex, 1u);
    RoboCheckEqInt(CssSelectorListMatches(&list, &host.host, div, &out, NULL),
                      0);
    RoboCheckEqInt(out.unsupported, 0);
    CssSelectorListDispose(&list);
}

static void TestCssLevelsComplexArguments(void)
{
    CssHost host;
    CssSelectorList list = RoboCssCleared;
    CssSelectorMatch out;
    CssElementHost unusable;

    CssHostInit(&host);
    CssHostAdd(&host, "div", NULL, NULL);
    CssHostSeal(&host);
    CssSelectParse("div", &list);

    RoboCheckEqInt(CssComplexSelectorMatches(NULL, &host.host, 1u, &out, NULL),
                      -1);
    RoboCheckEqInt(CssComplexSelectorMatches(
                          CssSelectorListAt(&list, 0u), NULL, 1u, &out, NULL),
                      -1);
    CssElementHostInit(&unusable);
    RoboCheckEqInt(CssComplexSelectorMatches(
                          CssSelectorListAt(&list, 0u), &unusable, 1u, &out,
                          NULL),
                      -1);
    RoboCheckEqInt(CssComplexSelectorMatches(
                          CssSelectorListAt(&list, 0u), &host.host,
                          RoboCssElementRefNone, &out, NULL),
                      0);
    RoboCheckEqInt(out.unsupported, 0);

    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Invariants                                                         */
/* ------------------------------------------------------------------ */

/**
 * A child relation is strictly narrower than a descendant relation: everything a
 * `>` matches, a space matches too, and something exists that only the space
 * matches. Both halves matter — an engine where `>` silently behaved like a
 * space would pass the implication alone.
 */
static void TestCssLevelsChildIsStricterThanDescendant(void)
{
    CssHost host;
    CssElementRef outer;
    CssElementRef middle;
    CssElementRef inner;

    CssHostInit(&host);
    outer = CssHostAdd(&host, "div", NULL, NULL);
    middle = CssHostAdd(&host, "section", NULL, NULL);
    inner = CssHostAdd(&host, "p", NULL, NULL);
    CssHostSetParent(&host, middle, outer);
    CssHostSetParent(&host, inner, middle);
    CssHostSeal(&host);

    /* The p's parent is a section, so `div > p` fails where `div p` succeeds —
     * the narrow relation, and the case that separates the two. */
    RoboCheckEqInt(CssMatch(&host, "div p", inner), 1);
    RoboCheckEqInt(CssMatch(&host, "div > p", inner), 0);
    /* Stated as the general implication over every element in the fixture. */
    {
        CssElementRef ref;

        for (ref = 1u; ref <= 3u; ref++) {
            if (CssMatch(&host, "div > p, div > section", ref)) {
                RoboCheckEqInt(CssMatch(&host, "div p, div section", ref), 1);
            }
        }
    }

    CssSelectorList list = RoboCssCleared;
    const CssComplexSelector *complex;

    CssSelectParse("div p", &list);
    complex = CssSelectorListAt(&list, 0u);
    RoboCheckEqInt(CssComplexSelectorCombinatorAt(complex, 0u),
                      (int)RoboCssCombinatorDescendant);
    {
        CssSelectorMatch out;

        CssSelectorMatchInit(&out);
        RoboCheckEqInt(CssComplexSelectorMatches(complex, &host.host, inner,
                                                           &out, NULL),
                          1);
    }
    CssSelectorListDispose(&list);
}

/**
 * A relation the host cannot answer makes the selector undecided, never false.
 *
 * A host that supplies only a view can still answer type, class, id and
 * attribute selectors, so it is a real host rather than a broken one — and
 * against it `h1 + p` has no answer. Reading the missing callback as "there is
 * no previous sibling" would turn every `+` and `>` selector into a silent
 * non-match, which is how a stylesheet loses rules without anyone noticing.
 */
static void TestCssLevelsUnavailableRelationIsUnsupported(void)
{
    CssHost host;
    CssSelectorMatch out;
    CssElementRef container;
    CssElementRef h1;
    CssElementRef p;
    char condition[32];

    CssHostInit(&host);
    container = CssHostAdd(&host, "div", NULL, NULL);
    h1 = CssHostAdd(&host, "h1", NULL, NULL);
    p = CssHostAdd(&host, "p", NULL, NULL);
    CssHostSetParent(&host, h1, container);
    CssHostSetParent(&host, p, container);
    CssHostSeal(&host);

    /* First: the relations are all there, and the selectors are decided. */
    RoboCheckEqInt(CssMatchFull(&host, "h1 + p", p, &out), 1);
    RoboCheckEqInt(out.unsupported, 0);
    RoboCheckEqInt(CssMatchFull(&host, "h1 ~ p", p, &out), 1);
    RoboCheckEqInt(out.unsupported, 0);
    RoboCheckEqInt(CssMatchFull(&host, "body p", p, &out), 0);
    RoboCheckEqInt(out.unsupported, 0);

    /* The relations the selectors have not asked for yet stay answerable, so the
     * only thing each case changes is the one callback under test. */
    CssHostDropRelation(&host, CSS_HOST_RELATION_PREVIOUS);
    condition[0] = '\0';
    RoboCheckEqInt(CssMatchCondition(&host, "h1 + p", p, &out, condition,
                                          sizeof(condition)),
                      0);
    RoboCheckEqInt(out.unsupported, 1);
    /* The reason is the relation, not a pseudo-class, so no condition is named. */
    RoboCheckStrEq(condition, "");
    RoboCheckEqInt(CssMatchFull(&host, "h1 ~ p", p, &out), 0);
    RoboCheckEqInt(out.unsupported, 1);
    /* `:first-child` is exactly "there is no previous sibling", so a host that
     * cannot say whether there is one cannot answer this either — however
     * plausible a "just say yes" shortcut looks. */
    RoboCheckEqInt(CssMatchFull(&host, ":first-child", p, &out), 0);
    RoboCheckEqInt(out.unsupported, 1);
    /* A selector that needs no relation at all is untouched. */
    RoboCheckEqInt(CssMatchFull(&host, "p", p, &out), 1);
    RoboCheckEqInt(out.unsupported, 0);

    /* Now the parent, on a fresh host. */
    CssHostInit(&host);
    h1 = CssHostAdd(&host, "h1", NULL, NULL);
    p = CssHostAdd(&host, "p", NULL, NULL);
    CssHostSetParent(&host, p, h1);
    CssHostSeal(&host);
    CssHostDropRelation(&host, CSS_HOST_RELATION_PARENT);
    RoboCheckEqInt(CssMatchFull(&host, "body p", p, &out), 0);
    RoboCheckEqInt(out.unsupported, 1);
    RoboCheckEqInt(CssMatchFull(&host, "body > p", p, &out), 0);
    RoboCheckEqInt(out.unsupported, 1);
    /* `:root` is "there is no parent element", and a host with no parent callback
     * says NONE for the same reason a document element does. The two must not be
     * conflated, or `:root` matches every element of such a host. */
    RoboCheckEqInt(CssMatchFull(&host, ":root", h1, &out), 0);
    RoboCheckEqInt(out.unsupported, 1);
    RoboCheckEqInt(CssMatchFull(&host, "*", h1, &out), 1);
    RoboCheckEqInt(out.unsupported, 0);

    /* And the next-sibling callback, which nothing above has needed yet. */
    CssHostInit(&host);
    container = CssHostAdd(&host, "div", NULL, NULL);
    h1 = CssHostAdd(&host, "h1", NULL, NULL);
    p = CssHostAdd(&host, "p", NULL, NULL);
    CssHostSetParent(&host, h1, container);
    CssHostSetParent(&host, p, container);
    CssHostSeal(&host);
    CssHostDropRelation(&host, CSS_HOST_RELATION_NEXT);
    RoboCheckEqInt(CssMatchFull(&host, ":last-child", h1, &out), 0);
    RoboCheckEqInt(out.unsupported, 1);
    RoboCheckEqInt(CssMatchFull(&host, ":only-child", h1, &out), 0);
    RoboCheckEqInt(out.unsupported, 1);
    /* `:first-child` asks the previous sibling, which is still there. */
    RoboCheckEqInt(CssMatchFull(&host, ":first-child", h1, &out), 1);
    RoboCheckEqInt(out.unsupported, 0);
}

/**
 * A host whose relations form a cycle is not a document, and the walk must end
 * anyway — with an unevaluatable verdict, not a wrong one and not a hang.
 *
 * The DOM cannot produce this, so the only way to know the bound is real is to
 * hand the matcher a host that does. `a b` is the selector of choice because the
 * rightmost compound matches immediately and the walk then searches ancestors
 * that never satisfy the left one, which is precisely how the cycle is entered.
 */
static void TestCssLevelsACyclicHostIsBounded(void)
{
    CssHost host;
    CssElementRef p;
    CssElementRef span;
    CssSelectorMatch out;

    CssHostInit(&host);
    p = CssHostAdd(&host, "p", NULL, NULL);
    span = CssHostAdd(&host, "span", NULL, NULL);
    /* p's parent is span and span's parent is p: a two-element cycle. Neither
     * carries class `x`, so the ancestor search never succeeds. */
    CssHostSetParent(&host, p, span);
    CssHostSetParent(&host, span, p);
    CssHostSeal(&host);

    RoboCheckEqInt(CssMatchFull(&host, ".x p", p, &out), 0);
    RoboCheckEqInt(out.unsupported, 1);
    /* The `>` forms are the opposite story, and deliberately so: a child chain
     * recurses once per compound rather than walking, so it is bounded by the
     * *selector's* length and terminates on its own even here. Both answers are
     * "no", but only one of them is undecided — and a caller that could not tell
     * them apart would report the same thing for a malformed host and for a
     * stylesheet that simply does not apply. */
    RoboCheckEqInt(CssMatchFull(&host, ".x > p", p, &out), 0);
    RoboCheckEqInt(out.unsupported, 0);
    RoboCheckEqInt(CssMatchFull(&host, ".x > .x > p", p, &out), 0);
    RoboCheckEqInt(out.unsupported, 0);

    /* A selector the cycle *can* satisfy is still answered, so the bound is a
     * guard against nonsense rather than a blanket refusal. */
    RoboCheckEqInt(CssMatchFull(&host, "span p", p, &out), 1);
    RoboCheckEqInt(out.unsupported, 0);
    RoboCheckEqInt(CssMatchFull(&host, "span > p", p, &out), 1);
    RoboCheckEqInt(out.unsupported, 0);
}

/**
 * The levels agree.
 *
 * This is the test that would catch a divergence the per-level suites cannot:
 * they each pin one door's behavior, so a change to the shared walk that moves
 * two doors in *different* directions shows up here and nowhere else. The
 * selector table is deliberately wide — every combinator, every structural
 * pseudo-class, a functional one, and the attribute operators — and every one of
 * them is asked about every element in the fixture.
 */
static void CheckLevelsAgree(CssHost *host,
                               const char *text,
                               CssElementRef ref)
{
    CssSelectorList list = RoboCssCleared;
    const CssComplexSelector *complex;
    CssSelectorMatch atList;
    CssSelectorMatch atComplex;
    int listResult;
    int complexResult;

    CssSelectParse(text, &list);
    RoboCheckEqInt(CssSelectorListCount(&list), 1u);
    complex = CssSelectorListAt(&list, 0u);
    RoboCheckEqInt(CssComplexSelectorIsWellFormed(complex), 1);

    CssSelectorMatchInit(&atList);
    listResult = CssSelectorListMatches(&list, &host->host, ref, &atList, NULL);
    CssSelectorMatchInit(&atComplex);
    complexResult = CssComplexSelectorMatches(complex, &host->host, ref,
                                                      &atComplex, NULL);

    RoboCheckEqInt(listResult, complexResult);
    RoboCheckEqInt(atList.matched, atComplex.matched);
    RoboCheckEqInt(atList.unsupported, atComplex.unsupported);
    if (atComplex.matched) {
        RoboCheckEqInt(atList.selectorIndex, 0u);
        RoboCheckEqInt(atList.range.begin, atComplex.range.begin);
        RoboCheckEqInt(atList.range.end, atComplex.range.end);
    }

    /* And where the complex selector is a single compound, the compound door
     * agrees with both. */
    if (CssComplexSelectorCompoundCount(complex) == 1u) {
        const CssCompoundSelector *compound =
            CssComplexSelectorCompoundAt(complex, 0u);
        CssSelectorMatch atCompound;

        CssSelectorMatchInit(&atCompound);
        RoboCheckEqInt(CssCompoundSelectorMatches(compound, &host->host, ref,
                                                            &atCompound, NULL),
                          complexResult);
        RoboCheckEqInt(atCompound.matched, atComplex.matched);
        RoboCheckEqInt(atCompound.unsupported, atComplex.unsupported);
    }

    CssSelectorListDispose(&list);
}

static void TestCssLevelsAgree(void)
{
    static const char *const selectors[] = {
        "div",           "*",            ".card",        "#app",
        "div.card",      "div#app",      "div p",        "div > p",
        "p + span",      "p ~ span",     ":root",        ":first-child",
        ":last-child",   ":only-child",  ":nth-child(2)", ":nth-last-child(1)",
        ":not(p)",       "[data-x]",     "[data-x=\"1\"]", "div.card#app",
        "body div p",    "p:hover",      "div :first-child", "div > p + span",
    };
    CssHost host;
    size_t s;
    CssElementRef ref;

    CssHostInit(&host);
    CssHostAdd(&host, "html", NULL, NULL);                 /* 1, the root   */
    CssHostAdd(&host, "body", NULL, NULL);                 /* 2             */
    CssHostAdd(&host, "div", "app", "card");               /* 3             */
    CssHostAdd(&host, "p", NULL, NULL);                    /* 4             */
    CssHostAdd(&host, "span", NULL, NULL);                 /* 5             */
    CssHostAdd(&host, "p", "solo", NULL);                  /* 6             */
    CssHostSetParent(&host, 2u, 1u);
    CssHostSetParent(&host, 3u, 2u);
    CssHostSetParent(&host, 4u, 3u);
    CssHostSetParent(&host, 5u, 3u);
    CssHostSetParent(&host, 6u, 3u);
    CssHostAttribute(&host, 4u, "data-x", "1");
    CssHostSeal(&host);

    for (s = 0u; s < sizeof(selectors) / sizeof(selectors[0]); s++) {
        for (ref = 1u; ref <= 6u; ref++) {
            CheckLevelsAgree(&host, selectors[s], ref);
        }
        /* And the null element, which every level must treat as "nothing". */
        CheckLevelsAgree(&host, selectors[s], RoboCssElementRefNone);
    }
}

void TestRegisterCssSelectMatchLevels(void);

void TestRegisterCssSelectMatchLevels(void)
{
    TestRegister("css.select.levels.simple_kinds", TestCssLevelsSimpleKinds);
    TestRegister("css.select.levels.simple_universal",
                       TestCssLevelsSimpleUniversal);
    TestRegister("css.select.levels.simple_pseudo_arguments",
                       TestCssLevelsSimplePseudoArguments);
    TestRegister("css.select.levels.simple_arguments",
                       TestCssLevelsSimpleArguments);
    TestRegister("css.select.levels.compound_requires_every_simple",
                       TestCssLevelsCompoundRequiresEverySimple);
    TestRegister("css.select.levels.compound_short_circuits_on_the_first_failure",
                       TestCssLevelsCompoundShortCircuitsOnTheFirstFailure);
    TestRegister("css.select.levels.compound_empty_and_null",
                       TestCssLevelsCompoundEmptyAndNull);
    TestRegister("css.select.levels.complex_of_one_compound_is_that_compound",
                       TestCssLevelsComplexOfOneCompoundIsThatCompound);
    TestRegister("css.select.levels.ill_formed_complex_is_refused_and_skipped",
                       TestCssLevelsIllFormedComplexIsRefusedAndSkipped);
    TestRegister("css.select.levels.complex_arguments",
                       TestCssLevelsComplexArguments);
    TestRegister("css.select.levels.child_is_stricter_than_descendant",
                       TestCssLevelsChildIsStricterThanDescendant);
    TestRegister("css.select.levels.unavailable_relation_is_unsupported",
                       TestCssLevelsUnavailableRelationIsUnsupported);
    TestRegister("css.select.levels.a_cyclic_host_is_bounded",
                       TestCssLevelsACyclicHostIsBounded);
    TestRegister("css.select.levels.levels_agree", TestCssLevelsAgree);
}
