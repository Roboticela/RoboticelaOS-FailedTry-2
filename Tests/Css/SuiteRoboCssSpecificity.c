/*
 * RoboCSS specificity (Phase 9.6) — css.specificity.*
 *
 * Specificity is a small number with a large chance of being silently wrong, and
 * almost every mistake looks like a plausible answer. So the tests are built
 * around the two ways it goes wrong:
 *
 *   a component in the wrong bucket   `::before` counted as a pseudo-class,
 *                                     `:not(…)` counted as itself, a combinator
 *                                     counted as a selector
 *   a comparison in the wrong order   B compared before A, or a packed integer
 *                                     whose radix let A carry into B
 *
 * The matrix below is the specification's own table, spelled out value by value.
 * The comparison tests then pin each of the three components as the *deciding*
 * one, because a tuple comparison that stopped at the first field would pass
 * every same-A case.
 *
 * The last group is about not computing from input the parser refused: a
 * malformed selector never becomes a list, so specificity is never asked about
 * one, and asking anyway must answer zero rather than walk a broken record.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* The value and its comparison                                         */
/* ------------------------------------------------------------------ */

static void TestCssSpecificityValue(void)
{
    CssSpecificity a;
    CssSpecificity b;
    CssSpecificity zero;

    CssSpecificityInit(&zero);
    RoboCheckEqInt(CssSpecificityIsZero(&zero), 1);
    RoboCheckEqInt(CssSpecificityEqual(&zero, &zero), 1);
    RoboCheckEqInt(CssSpecificityCompare(&zero, &zero), 0);

    /* Adding builds a tuple component by component, and the components never
     * bleed into one another. */
    CssSpecificityInit(&a);
    a.id = 1u;
    CssSpecificityInit(&b);
    b.classLike = 2u;
    b.typeLike = 3u;
    CssSpecificityAdd(&a, &b);
    CssSpecificityIs(a, 1u, 2u, 3u);
    RoboCheckEqInt(CssSpecificityIsZero(&a), 0);

    /* take_max keeps the greater tuple and discards the lesser. */
    CssSpecificityTakeMax(&a, &zero);
    CssSpecificityIs(a, 1u, 2u, 3u);
    {
        CssSpecificity bigger;

        CssSpecificityInit(&bigger);
        bigger.id = 1u;
        bigger.classLike = 9u;
        bigger.typeLike = 0u;
        CssSpecificityTakeMax(&a, &bigger);
        CssSpecificityIs(a, 1u, 9u, 0u);
    }

    /* Every door tolerates NULL: "the specificity of no selector" is (0,0,0),
     * not a failure to report. */
    CssSpecificityInit(NULL);
    RoboCheckEqInt(CssSpecificityIsZero(NULL), 0);
    CssSpecificityAdd(NULL, NULL);
    CssSpecificityAdd(&a, NULL);
    CssSpecificityIs(a, 1u, 9u, 0u);
    CssSpecificityTakeMax(NULL, &a);
    CssSpecificityTakeMax(&a, NULL);
    CssSpecificityIs(a, 1u, 9u, 0u);
    RoboCheckEqInt(CssSpecificityCompare(NULL, NULL), 0);
    RoboCheckEqInt(CssSpecificityEqual(NULL, NULL), 1);
    RoboCheckEqInt(CssSpecificityEqual(&zero, NULL), 1);
    /* An absent specificity is the zero one, so a non-zero tuple is not equal to
     * nothing. */
    RoboCheckEqInt(CssSpecificityEqual(&a, NULL), 0);
}

static void TestCssSpecificityCompare(void)
{
    CssSpecificity a;
    CssSpecificity b;

    /* A decides regardless of B and C. */
    CssSpecificityInit(&a);
    CssSpecificityInit(&b);
    a.id = 1u;
    b.classLike = 100u;
    b.typeLike = 100u;
    RoboCheckEqInt(CssSpecificityCompare(&a, &b), 1);
    RoboCheckEqInt(CssSpecificityCompare(&b, &a), -1);

    /* B decides when A is equal. */
    CssSpecificityInit(&a);
    CssSpecificityInit(&b);
    a.id = 2u;
    a.classLike = 1u;
    b.id = 2u;
    b.classLike = 2u;
    b.typeLike = 50u;
    RoboCheckEqInt(CssSpecificityCompare(&a, &b), -1);
    RoboCheckEqInt(CssSpecificityCompare(&b, &a), 1);

    /* C decides when A and B are equal — the case a comparison that stopped
     * after two fields would report as a tie, where `div.card` (0,1,1) and
     * `div.card + div` (0,1,2) really differ. */
    CssSpecificityInit(&a);
    CssSpecificityInit(&b);
    a.classLike = 1u;
    a.typeLike = 1u;
    b.classLike = 1u;
    b.typeLike = 2u;
    RoboCheckEqInt(CssSpecificityCompare(&a, &b), -1);
    RoboCheckEqInt(CssSpecificityCompare(&b, &a), 1);

    /* And equality is equality in all three. */
    RoboCheckEqInt(CssSpecificityCompare(&a, &a), 0);
    RoboCheckEqInt(CssSpecificityEqual(&a, &a), 1);
    RoboCheckEqInt(CssSpecificityEqual(&a, &b), 0);
}

/* ------------------------------------------------------------------ */
/* The specification's own table                                        */
/* ------------------------------------------------------------------ */

static void TestCssSpecificityMatrix(void)
{
    static const struct {
        const char *selector;
        uint32_t id;
        uint32_t classLike;
        uint32_t typeLike;
    } cases[] = {
        {"*", 0u, 0u, 0u},
        {"div", 0u, 0u, 1u},
        {".card", 0u, 1u, 0u},
        {"#main", 1u, 0u, 0u},
        {"[disabled]", 0u, 1u, 0u},
        {":root", 0u, 1u, 0u},
        {"::before", 0u, 0u, 1u},
        {"div.card", 0u, 1u, 1u},
        {"#main.card", 1u, 1u, 0u},
        {"div#main.card", 1u, 1u, 1u},
        {"#app .panel .title", 1u, 2u, 0u},
        {"div > .card", 0u, 1u, 1u}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CssSpecificity specificity = CssSpecificityOf(cases[i].selector, 0u);

        CssSpecificityIs(specificity, cases[i].id, cases[i].classLike,
                           cases[i].typeLike);
    }
}

/* ------------------------------------------------------------------ */
/* What each kind of simple selector is worth                           */
/* ------------------------------------------------------------------ */

static void TestCssSpecificitySimpleKinds(void)
{
    CssSelectorList list;

    /* One compound holding one of every kind, so each contribution is read from
     * the same walk a real selector goes through. */
    CssSelectParse("*#a.b[c]:hover::after", &list);
    RoboCheckEqInt(CssSelectFirstCompoundCount(&list), 6u);
    CssSpecificityIs(
        CssSpecificityOfSimple(CssSelectSimple(&list, 0u, 0u, 0u),
                                       CssComplexSelectorCompoundAt(
                                           CssSelectorListAt(&list, 0u), 0u),
                                       0u),
        0u, 0u, 0u); /* * */
    CssSpecificityIs(
        CssSpecificityOfSimple(CssSelectSimple(&list, 0u, 0u, 1u),
                                       CssComplexSelectorCompoundAt(
                                           CssSelectorListAt(&list, 0u), 0u),
                                       1u),
        1u, 0u, 0u); /* #a */
    CssSpecificityIs(
        CssSpecificityOfSimple(CssSelectSimple(&list, 0u, 0u, 2u),
                                       CssComplexSelectorCompoundAt(
                                           CssSelectorListAt(&list, 0u), 0u),
                                       2u),
        0u, 1u, 0u); /* .b */
    CssSpecificityIs(
        CssSpecificityOfSimple(CssSelectSimple(&list, 0u, 0u, 3u),
                                       CssComplexSelectorCompoundAt(
                                           CssSelectorListAt(&list, 0u), 0u),
                                       3u),
        0u, 1u, 0u); /* [c] */
    CssSpecificityIs(
        CssSpecificityOfSimple(CssSelectSimple(&list, 0u, 0u, 4u),
                                       CssComplexSelectorCompoundAt(
                                           CssSelectorListAt(&list, 0u), 0u),
                                       4u),
        0u, 1u, 0u); /* :hover */
    CssSpecificityIs(
        CssSpecificityOfSimple(CssSelectSimple(&list, 0u, 0u, 5u),
                                       CssComplexSelectorCompoundAt(
                                           CssSelectorListAt(&list, 0u), 0u),
                                       5u),
        0u, 0u, 1u); /* ::after */
    /* And the compound is the sum of those: (1,3,1). */
    CssSpecificityIs(CssSpecificityOfListItem(&list, 0u), 1u, 3u, 1u);
    CssSelectorListDispose(&list);

    /* The universal selector contributes nothing wherever it sits. */
    CssSpecificityIs(CssSpecificityOf("*", 0u), 0u, 0u, 0u);
    CssSpecificityIs(CssSpecificityOf("*.card", 0u), 0u, 1u, 0u);
    CssSpecificityIs(CssSpecificityOf("*#main", 0u), 1u, 0u, 0u);
    CssSpecificityIs(CssSpecificityOf("div *", 0u), 0u, 0u, 1u);

    /* Each syntactic id counts, so a selector with several ids is worth several
     * — nothing is clamped to one. */
    CssSpecificityIs(CssSpecificityOf("#a#b#c#d", 0u), 4u, 0u, 0u);
}

static void TestCssSpecificityAttributeOperators(void)
{
    /* Every comparator is one B: how an attribute is tested is not another
     * selector, so `[a]` and `[a^="b"]` are worth the same. */
    static const char *const selectors[] = {
        "[a]", "[a=b]", "[a~=b]", "[a|=b]", "[a^=b]", "[a$=b]", "[a*=b]",
        "[type=\"button\"]", "[href^=\"/api\"]", "[data-name*=\"test\"]"
    };
    size_t i;

    for (i = 0u; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        CssSpecificityIs(CssSpecificityOf(selectors[i], 0u), 0u, 1u, 0u);
    }
}

static void TestCssSpecificityPseudoClasses(void)
{
    /* Every pseudo-class is one B, including the An+B family — the argument is
     * part of the pseudo-class rather than another selector — and including one
     * this build cannot evaluate, which is still a pseudo-class. */
    static const char *const selectors[] = {
        ":root", ":first-child", ":last-child", ":only-child",
        ":nth-child(2n+1)", ":nth-child(odd)", ":nth-last-child(2)", ":hover",
        ":focus", ":lang(en)"
    };
    size_t i;

    for (i = 0u; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        CssSpecificityIs(CssSpecificityOf(selectors[i], 0u), 0u, 1u, 0u);
    }
}

static void TestCssSpecificityPseudoElements(void)
{
    /* A pseudo-element is C, never B. The legacy one-colon spellings are the
     * same pseudo-elements and must be worth the same. */
    static const char *const selectors[] = {
        "::before", "::after", "::first-line", "::first-letter",
        ":before", ":after", ":first-line", ":first-letter"
    };
    size_t i;

    for (i = 0u; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        CssSpecificityIs(CssSpecificityOf(selectors[i], 0u), 0u, 0u, 1u);
    }
    /* One of each in a compound: (0,1,2) — the pseudo-class is B and the
     * pseudo-element is C, and neither is mistaken for the other. */
    CssSpecificityIs(CssSpecificityOf("button:hover::before", 0u), 0u, 1u, 2u);
}

/* ------------------------------------------------------------------ */
/* :not(...)                                                            */
/* ------------------------------------------------------------------ */

static void TestCssSpecificityNot(void)
{
    static const struct {
        const char *selector;
        uint32_t id;
        uint32_t classLike;
        uint32_t typeLike;
    } cases[] = {
        /* `:not()` is worth its argument, not one B of its own. */
        {":not(.a)", 0u, 1u, 0u},
        {":not(#a)", 1u, 0u, 0u},
        {":not(div)", 0u, 0u, 1u},
        {":not(div p)", 0u, 0u, 2u},
        {":not(*)", 0u, 0u, 0u},
        /* A list inside `:not()` is worth its most specific member. */
        {":not(.a, #b)", 1u, 0u, 0u},
        {":not(#b, .a)", 1u, 0u, 0u},
        {":not(p, span)", 0u, 0u, 1u},
        /* The surrounding compound still contributes normally. */
        {"div:not(.a)", 0u, 1u, 1u},
        {"#x:not(.a)", 1u, 1u, 0u},
        /* Nesting is transparent: each level is worth the level inside it. */
        {":not(:not(#x))", 1u, 0u, 0u},
        {":not(:not(:not(#x)))", 1u, 0u, 0u},
        /* Each `:not()` is worth its own argument, so two of them add: the
         * inner `.a` is one B and the `#x` is one A. */
        {":not(:not(.a)):not(#x)", 1u, 1u, 0u},
        /* A complex argument's combinators contribute nothing. */
        {":not(div > p)", 0u, 0u, 2u}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CssSpecificity specificity = CssSpecificityOf(cases[i].selector, 0u);

        CssSpecificityIs(specificity, cases[i].id, cases[i].classLike,
                           cases[i].typeLike);
    }
}

static void TestCssSpecificityNotWithoutAnArgument(void)
{
    /* A hand-built `:not` with no parsed argument addresses nothing, so it is
     * worth nothing rather than being read as a pseudo-class worth one. The
     * parser cannot produce this record; a caller can. */
    CssCompoundSelector compound;
    CssSimpleSelector simple;

    CssFixture();
    CssCompoundSelectorInit(&compound);
    CssSimpleSelectorInit(&simple);
    RoboCheckEqInt(CssSimpleSelectorMake(
                          &simple, RoboCssSimpleSelectorPseudoClass,
                          CssSliceFromText("not"), NULL),
                      RoboOk);
    RoboCheckEqInt(CssCompoundSelectorAppend(&compound, &simple, NULL),
                      RoboOk);

    CssSpecificityIs(CssSpecificityOfCompound(&compound), 0u, 0u, 0u);
    CssSpecificityIs(CssSpecificityOfSimple(&simple, &compound, 0u), 0u, 0u,
                       0u);
    /* And with no compound to resolve the argument through, still nothing. */
    CssSpecificityIs(CssSpecificityOfSimple(&simple, NULL, 0u), 0u, 0u, 0u);
    /* A different pseudo-class in the same position is one B, which is what
     * makes the line above a statement about `:not` rather than about hand-built
     * pseudo-classes. */
    RoboCheckEqInt(CssSimpleSelectorMake(&simple,
                                                    RoboCssSimpleSelectorPseudoClass,
                                                    CssSliceFromText("hover"),
                                                    NULL),
                      RoboOk);
    CssSpecificityIs(CssSpecificityOfSimple(&simple, NULL, 0u), 0u, 1u, 0u);

    CssCompoundSelectorDispose(&compound);
}

/**
 * Build @p depth levels of `:not(...)` around @p inner and leave the result in
 * @p out; @p inner is consumed and left empty.
 *
 * The parser rejects `:not()` nested deeper than RoboCssMaxSelectorNesting,
 * so a chain past that can only be built here — and it is exactly the input the
 * specificity depth bound exists for.
 */
static void CssBuildNotChain(CssSelectorList *out,
                                size_t depth,
                                CssSelectorList *inner)
{
    size_t level;

    for (level = 0u; level < depth; level++) {
        CssCompoundSelector compound;
        CssComplexSelector complex;
        CssSelectorList next;
        CssSimpleSelector simple;

        CssCompoundSelectorInit(&compound);
        CssComplexSelectorInit(&complex);
        CssSelectorListInit(&next);
        CssSimpleSelectorInit(&simple);

        RoboCheckEqInt(CssSimpleSelectorMake(
                              &simple, RoboCssSimpleSelectorPseudoClass,
                              CssSliceFromText("not"), NULL),
                          RoboOk);
        RoboCheckEqInt(CssCompoundSelectorAppend(&compound, &simple, NULL),
                          RoboOk);
        RoboCheckEqInt(CssCompoundSelectorAttachNested(&compound, 0u, inner,
                                                                   NULL),
                          RoboOk);
        RoboCheckEqInt(CssComplexSelectorAppendCompound(&complex, &compound,
                                                                    NULL),
                          RoboOk);
        RoboCheckEqInt(CssSelectorListAppend(&next, &complex, NULL), RoboOk);
        *inner = next;
    }
    *out = *inner;
    CssSelectorListInit(inner);
}

/** The specificity of `:not(:not(...#deep...))`, @p depth levels deep. */
static CssSpecificity CssSpecificityOfNotChain(size_t depth)
{
    CssSelectorList list;
    CssSelectorList seed;
    CssSpecificity specificity;

    CssSelectParse("#deep", &seed);
    CssBuildNotChain(&list, depth, &seed);
    specificity = CssSpecificityMaxOfList(&list);
    CssSelectorListDispose(&list);
    return specificity;
}

static void TestCssSpecificityNotChainDepth(void)
{
    /* Nesting is transparent, so the innermost `#deep` is what the whole chain
     * is worth — up to the parser's own cap. */
    CssSpecificityIs(CssSpecificityOfNotChain(0u), 1u, 0u, 0u);
    CssSpecificityIs(CssSpecificityOfNotChain(1u), 1u, 0u, 0u);
    CssSpecificityIs(CssSpecificityOfNotChain(4u), 1u, 0u, 0u);
    CssSpecificityIs(CssSpecificityOfNotChain(RoboCssSpecificityNestingMax),
                       1u, 0u, 0u);
    /* Past the bound the walk stops descending and counts nothing rather than
     * recursing to exhaustion. A chain this deep cannot come from the parser. */
    CssSpecificityIs(
        CssSpecificityOfNotChain(RoboCssSpecificityNestingMax + 8u), 0u, 0u, 0u);
    /* And the answer is stable, so the bound is a rule and not a race. */
    {
        CssSpecificity first =
            CssSpecificityOfNotChain((size_t)RoboCssSpecificityNestingMax * 4u);
        CssSpecificity second =
            CssSpecificityOfNotChain((size_t)RoboCssSpecificityNestingMax * 4u);

        RoboCheckEqInt(CssSpecificityEqual(&first, &second), 1);
        CssSpecificityIs(first, 0u, 0u, 0u);
    }
}

/* ------------------------------------------------------------------ */
/* Compound, complex and list                                           */
/* ------------------------------------------------------------------ */

static void TestCssSpecificityCompoundAndComplex(void)
{
    CssSelectorList list;
    const CssComplexSelector *complex;

    CssSelectParse("div#main.card[type=text]:root > span.title::before", &list);
    complex = CssSelectorListAt(&list, 0u);

    /* Compound 0: div(1 C) #main(1 A) .card(1 B) [type=text](1 B) :root(1 B). */
    CssSpecificityIs(
        CssSpecificityOfCompound(
            CssComplexSelectorCompoundAt(complex, 0u)),
        1u, 3u, 1u);
    /* Compound 1: span(1 C) .title(1 B) ::before(1 C). */
    CssSpecificityIs(
        CssSpecificityOfCompound(
            CssComplexSelectorCompoundAt(complex, 1u)),
        0u, 1u, 2u);
    /* The complex selector is their sum. */
    CssSpecificityIs(CssSpecificityOfComplex(complex), 1u, 4u, 3u);
    CssSpecificityIs(CssSpecificityOfListItem(&list, 0u), 1u, 4u, 3u);

    CssSelectorListDispose(&list);
}

static void TestCssSpecificityCombinatorsAreZero(void)
{
    CssSpecificity descendant = CssSpecificityOf("div p", 0u);
    CssSpecificity child = CssSpecificityOf("div > p", 0u);
    CssSpecificity adjacent = CssSpecificityOf("div + p", 0u);
    CssSpecificity sibling = CssSpecificityOf("div ~ p", 0u);

    CssSpecificityIs(descendant, 0u, 0u, 2u);
    /* All four are the same specificity, which is the whole statement: a
     * combinator says how to find the next compound, not what it is. */
    RoboCheckEqInt(CssSpecificityEqual(&descendant, &child), 1);
    RoboCheckEqInt(CssSpecificityEqual(&descendant, &adjacent), 1);
    RoboCheckEqInt(CssSpecificityEqual(&descendant, &sibling), 1);

    /* Whitespace around a combinator changes nothing either. */
    CssSpecificityIs(CssSpecificityOf("div  >  p", 0u), 0u, 0u, 2u);
    /* A deeper chain accumulates every compound: `#app` (A), `.panel` and
     * `.title` (B), `a` (C). */
    CssSpecificityIs(CssSpecificityOf("#app > .panel .title a", 0u), 1u, 2u, 1u);
}

static void TestCssSpecificityList(void)
{
    CssSelectorList list;

    CssSelectParse("div, .card, #main", &list);
    RoboCheckEqInt(CssSelectorListCount(&list), 3u);
    /* A specificity belongs to a selector, never to the list: the three members
     * keep three values. */
    CssSpecificityIs(CssSpecificityOfListItem(&list, 0u), 0u, 0u, 1u);
    CssSpecificityIs(CssSpecificityOfListItem(&list, 1u), 0u, 1u, 0u);
    CssSpecificityIs(CssSpecificityOfListItem(&list, 2u), 1u, 0u, 0u);
    /* An out-of-range index is nothing, not a neighbouring member. */
    CssSpecificityIs(CssSpecificityOfListItem(&list, 3u), 0u, 0u, 0u);
    CssSelectorListDispose(&list);

    CssSpecificityIs(CssSpecificityOf("div, .card, #main", 0u), 0u, 0u, 1u);
    CssSpecificityIs(CssSpecificityOf("div, .card, #main", 1u), 0u, 1u, 0u);
    CssSpecificityIs(CssSpecificityOf("div, .card, #main", 2u), 1u, 0u, 0u);
}

static void TestCssSpecificityListMax(void)
{
    CssSelectorList list;

    /* `max_of_list` is the `:not()` rule and is a different question from "the
     * member that matched" — a caller that confused the two would rank a whole
     * list by a member that never applied. */
    CssSelectParse("div, .card, #main", &list);
    CssSpecificityIs(CssSpecificityMaxOfList(&list), 1u, 0u, 0u);
    CssSelectorListDispose(&list);

    CssSelectParse("p, span.tiny", &list);
    CssSpecificityIs(CssSpecificityMaxOfList(&list), 0u, 1u, 1u);
    CssSelectorListDispose(&list);

    CssSelectParse("a", &list);
    CssSpecificityIs(CssSpecificityMaxOfList(&list), 0u, 0u, 1u);
    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Escapes, saturation and scale                                        */
/* ------------------------------------------------------------------ */

static void TestCssSpecificityEscaping(void)
{
    /* Escaping changes which bytes an identifier resolves to, never which
     * bucket it belongs in. The parser interns the resolved spelling, and this
     * measures what it produced. */
    CssSpecificityIs(CssSpecificityOf("#item\\31 23", 0u), 1u, 0u, 0u);
    CssSpecificityIs(CssSpecificityOf(".foo\\:bar", 0u), 0u, 1u, 0u);
    CssSpecificityIs(CssSpecificityOf("[data-name=\"a\\ b\"]", 0u), 0u, 1u, 0u);
    CssSpecificityIs(CssSpecificityOf("\\64 iv", 0u), 0u, 0u, 1u);
    CssSpecificityIs(CssSpecificityOf("div.foo\\:bar#item\\31 23", 0u), 1u, 1u, 1u);
}

static void TestCssSpecificitySaturation(void)
{
    CssSpecificity a;
    CssSpecificity b;

    /* A component saturates rather than wrapping. Wrapping would turn "more ids
     * than the maximum" into *fewer*, which is the one failure that inverts the
     * comparison instead of merely losing precision. */
    CssSpecificityInit(&a);
    a.id = RoboCssSpecificityComponentMax - 1u;
    CssSpecificityInit(&b);
    b.id = 8u;
    CssSpecificityAdd(&a, &b);
    RoboCheckEqInt(a.id, RoboCssSpecificityComponentMax);
    RoboCheckEqInt(a.classLike, 0u);

    CssSpecificityAdd(&a, &a);
    RoboCheckEqInt(a.id, RoboCssSpecificityComponentMax);

    /* And a saturated value is still ordered above a merely large one. */
    CssSpecificityInit(&b);
    b.id = RoboCssSpecificityComponentMax - 1u;
    RoboCheckEqInt(CssSpecificityCompare(&a, &b), 1);

    /* take_max saturates through compare, so a saturated candidate wins. */
    CssSpecificityTakeMax(&b, &a);
    RoboCheckEqInt(b.id, RoboCssSpecificityComponentMax);
}

static void TestCssSpecificityStress(void)
{
    /* A selector at the width a generated stylesheet can reach: thousands of
     * class selectors in one compound. The count must be exact — a `uint32_t`
     * accumulator does not overflow at this size, and this is the test that says
     * so — and repeated computation must agree with itself. */
    enum { COUNT = 3000 };
    char text[COUNT * 2 + 1];
    size_t at = 0u;
    size_t i;
    CssSpecificity first;
    CssSpecificity second;

    for (i = 0u; i < (size_t)COUNT; i++) {
        text[at++] = '.';
        text[at++] = 'a';
    }
    text[at] = '\0';

    first = CssSpecificityOf(text, 0u);
    CssSpecificityIs(first, 0u, (uint32_t)COUNT, 0u);

    second = CssSpecificityOf(text, 0u);
    RoboCheckEqInt(CssSpecificityEqual(&first, &second), 1);
}

/* ------------------------------------------------------------------ */
/* The doors agree, and malformed input never reaches a computation     */
/* ------------------------------------------------------------------ */

static void TestCssSpecificityLevelsAgree(void)
{
    CssSelectorList list;
    const CssComplexSelector *complex;
    CssSpecificity total;
    CssSpecificity summed;

    /* The four doors must be one walk seen from four places: the sum of the
     * simples is the compound, the sum of the compounds is the complex, and the
     * complex is the list member. A disagreement would mean one door had its own
     * idea of what counts. */
    CssSelectParse("div#main.card[type=text]:root > span::before", &list);
    complex = CssSelectorListAt(&list, 0u);
    total = CssSpecificityOfComplex(complex);
    CssSpecificityInit(&summed);

    {
        size_t compoundIndex;

        for (compoundIndex = 0u;
             compoundIndex < CssComplexSelectorCompoundCount(complex);
             compoundIndex++) {
            const CssCompoundSelector *compound =
                CssComplexSelectorCompoundAt(complex, compoundIndex);
            CssSpecificity byCompound =
                CssSpecificityOfCompound(compound);
            CssSpecificity simpleSum;
            size_t simpleIndex;

            CssSpecificityInit(&simpleSum);
            for (simpleIndex = 0u;
                 simpleIndex < CssCompoundSelectorCount(compound);
                 simpleIndex++) {
                CssSpecificity one = CssSpecificityOfSimple(
                    CssCompoundSelectorAt(compound, simpleIndex), compound,
                    simpleIndex);

                CssSpecificityAdd(&simpleSum, &one);
            }
            RoboCheckEqInt(CssSpecificityEqual(&simpleSum, &byCompound), 1);
            CssSpecificityAdd(&summed, &byCompound);
        }
    }
    RoboCheckEqInt(CssSpecificityEqual(&summed, &total), 1);
    {
        CssSpecificity byList = CssSpecificityOfListItem(&list, 0u);

        RoboCheckEqInt(CssSpecificityEqual(&total, &byList), 1);
    }
    /* And the value is the one a hand computation gives. */
    CssSpecificityIs(total, 1u, 3u, 3u);

    CssSelectorListDispose(&list);
}

static void TestCssSpecificityMalformedNeverReaches(void)
{
    /* Every form the parser refuses, including the ones 9.4's own suite only
     * exercised through a rule's prelude. A refused parse leaves an empty list,
     * so specificity is never asked about a malformed structure — and asking
     * anyway answers zero rather than walking a broken record. */
    static const char *const rejected[] = {
        "#", ".", "div >", "> div", ":not(", ":nth-child(", "[attr", "[attr=",
        "[attr>>value]", ".class#", "div,", ",", "div,,p", "input*.field", "div +"
    };
    size_t i;

    for (i = 0u; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
        CssSelectorList list = RoboCssCleared;
        CssError error;
        CssSpecificity specificity;

        CssFixture();
        CssErrorInit(&error);
        RoboCheckEqInt(CssSelectorListParseText(&list, rejected[i], &error,
                                                            NULL),
                          RoboParseError);
        RoboCheckEqInt(CssErrorIsError(&error), 1);
        RoboCheckEqInt(CssSelectorListCount(&list), 0u);

        specificity = CssSpecificityMaxOfList(&list);
        RoboCheckEqInt(CssSpecificityIsZero(&specificity), 1);
        specificity = CssSpecificityOfListItem(&list, 0u);
        RoboCheckEqInt(CssSpecificityIsZero(&specificity), 1);
        CssSelectorListDispose(&list);
    }
}

static void TestCssSpecificityApiArguments(void)
{
    /* Absent input is the zero specificity, every door, including an index that
     * addresses nothing. */
    CssSpecificityIs(CssSpecificityOfSimple(NULL, NULL, 0u), 0u, 0u, 0u);
    CssSpecificityIs(CssSpecificityOfCompound(NULL), 0u, 0u, 0u);
    CssSpecificityIs(CssSpecificityOfComplex(NULL), 0u, 0u, 0u);
    CssSpecificityIs(CssSpecificityOfListItem(NULL, 0u), 0u, 0u, 0u);
    CssSpecificityIs(CssSpecificityMaxOfList(NULL), 0u, 0u, 0u);
    /* A well-formed list with no members is the same answer. */
    {
        CssSelectorList empty = RoboCssCleared;

        CssFixture();
        CssSelectorListInit(&empty);
        CssSpecificityIs(CssSpecificityMaxOfList(&empty), 0u, 0u, 0u);
        CssSpecificityIs(CssSpecificityOfListItem(&empty, 0u), 0u, 0u, 0u);
        CssSelectorListDispose(&empty);
    }
}

void TestRegisterCssSpecificity(void);

void TestRegisterCssSpecificity(void)
{
    TestRegister("css.specificity.value", TestCssSpecificityValue);
    TestRegister("css.specificity.compare", TestCssSpecificityCompare);
    TestRegister("css.specificity.matrix", TestCssSpecificityMatrix);
    TestRegister("css.specificity.simple_kinds", TestCssSpecificitySimpleKinds);
    TestRegister("css.specificity.attribute_operators",
                       TestCssSpecificityAttributeOperators);
    TestRegister("css.specificity.pseudo_classes",
                       TestCssSpecificityPseudoClasses);
    TestRegister("css.specificity.pseudo_elements",
                       TestCssSpecificityPseudoElements);
    TestRegister("css.specificity.not", TestCssSpecificityNot);
    TestRegister("css.specificity.not_without_an_argument",
                       TestCssSpecificityNotWithoutAnArgument);
    TestRegister("css.specificity.not_chain_depth",
                       TestCssSpecificityNotChainDepth);
    TestRegister("css.specificity.compound_and_complex",
                       TestCssSpecificityCompoundAndComplex);
    TestRegister("css.specificity.combinators_are_zero",
                       TestCssSpecificityCombinatorsAreZero);
    TestRegister("css.specificity.list", TestCssSpecificityList);
    TestRegister("css.specificity.list_max", TestCssSpecificityListMax);
    TestRegister("css.specificity.escaping", TestCssSpecificityEscaping);
    TestRegister("css.specificity.saturation", TestCssSpecificitySaturation);
    TestRegister("css.specificity.stress", TestCssSpecificityStress);
    TestRegister("css.specificity.levels_agree", TestCssSpecificityLevelsAgree);
    TestRegister("css.specificity.malformed_never_reaches",
                       TestCssSpecificityMalformedNeverReaches);
    TestRegister("css.specificity.api_arguments",
                       TestCssSpecificityApiArguments);
}
