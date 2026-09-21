/*
 * RoboCSS structural pseudo-class matching (Phase 9.4) — css.select.match.*
 *
 * The structural set is where "matching" stops being a property of one element
 * and becomes a property of its position among its *element* siblings, so these
 * tests are written against positions that are counted in the fixture rather
 * than in the test: the tree below puts six children under one parent and
 * nothing under another, which is what makes `:nth-child(3)` a statement with a
 * single right answer.
 *
 *   div#list (1)
 *     p.a     (2)   child #1
 *     p.b     (3)   child #2
 *     span    (4)   child #3
 *     p.c     (5)   child #4
 *     p.d     (6)   child #5
 *     div     (7)   child #6   (the nesting case)
 *       p     (8)   the only child of 7
 *   div#solo (9)
 *     p       (10)  the only child of 9
 *   p         (11)  a root element with no parent at all
 */

#include "RoboCssTestSupport.h"

#define S_LIST 1u
#define S_PA 2u
#define S_PB 3u
#define S_SPAN 4u
#define S_PC 5u
#define S_PD 6u
#define S_NEST 7u
#define S_NEST_P 8u
#define S_SOLO 9u
#define S_ONLY 10u
#define S_ROOT 11u

static void StructuralTree(CssHost *host)
{
    CssHostInit(host);
    CssHostAdd(host, "div", "list", NULL);
    CssHostAdd(host, "p", NULL, "a");
    CssHostAdd(host, "p", NULL, "b");
    CssHostAdd(host, "span", NULL, NULL);
    CssHostAdd(host, "p", NULL, "c");
    CssHostAdd(host, "p", NULL, "d");
    CssHostAdd(host, "div", NULL, NULL);
    CssHostAdd(host, "p", NULL, NULL);
    CssHostAdd(host, "div", "solo", NULL);
    CssHostAdd(host, "p", NULL, NULL);
    CssHostAdd(host, "p", "orphan", NULL);

    CssHostSetParent(host, S_PA, S_LIST);
    CssHostSetParent(host, S_PB, S_LIST);
    CssHostSetParent(host, S_SPAN, S_LIST);
    CssHostSetParent(host, S_PC, S_LIST);
    CssHostSetParent(host, S_PD, S_LIST);
    CssHostSetParent(host, S_NEST, S_LIST);
    CssHostSetParent(host, S_NEST_P, S_NEST);
    CssHostSetParent(host, S_ONLY, S_SOLO);
    CssHostSeal(host);
}

/* ------------------------------------------------------------------ */
/* :root                                                              */
/* ------------------------------------------------------------------ */

static void TestCssMatchRoot(void)
{
    CssHost host;
    CssElementRef ref;

    StructuralTree(&host);

    /* `:root` is the element with no parent — a fact about the element, not
     * about its tag, so a document whose root is not `html` still works. */
    RoboCheckEqInt(CssMatch(&host, ":root", S_LIST), 1);
    RoboCheckEqInt(CssMatch(&host, ":root", S_SOLO), 1);
    RoboCheckEqInt(CssMatch(&host, ":root", S_ROOT), 1);
    RoboCheckEqInt(CssMatch(&host, ":root", S_PA), 0);
    RoboCheckEqInt(CssMatch(&host, ":root", S_NEST_P), 0);
    /* A root element is also the first and last child of nothing, which is a
     * consequence rather than a special case. */
    RoboCheckEqInt(CssMatch(&host, ":first-child", S_LIST), 1);
    RoboCheckEqInt(CssMatch(&host, ":last-child", S_LIST), 1);
    RoboCheckEqInt(CssMatch(&host, ":only-child", S_LIST), 1);
    RoboCheckEqInt(CssMatch(&host, ":first-child", S_ROOT), 1);
    for (ref = 1u; ref <= S_ROOT; ref++) {
        RoboCheckEqInt(CssMatch(&host, "div:root", ref), ref == S_LIST || ref == S_SOLO);
    }
}

/* ------------------------------------------------------------------ */
/* :first-child, :last-child, :only-child                             */
/* ------------------------------------------------------------------ */

static void TestCssMatchChildPositions(void)
{
    CssHost host;

    StructuralTree(&host);

    RoboCheckEqInt(CssMatch(&host, ":first-child", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, ":first-child", S_PB), 0);
    RoboCheckEqInt(CssMatch(&host, ":first-child", S_NEST), 0);
    RoboCheckEqInt(CssMatch(&host, "p:first-child", S_PA), 1);
    /* `span` is the third child and the first `span`; the two are different
     * questions and only the first is `:first-child`. */
    RoboCheckEqInt(CssMatch(&host, ":first-child", S_SPAN), 0);
    RoboCheckEqInt(CssMatch(&host, "span:first-child", S_SPAN), 0);

    RoboCheckEqInt(CssMatch(&host, ":last-child", S_NEST), 1);
    RoboCheckEqInt(CssMatch(&host, ":last-child", S_PD), 0);
    RoboCheckEqInt(CssMatch(&host, ":last-child", S_PA), 0);
    RoboCheckEqInt(CssMatch(&host, ":last-child", S_NEST_P), 1);
    RoboCheckEqInt(CssMatch(&host, ":last-child", S_ONLY), 1);

    RoboCheckEqInt(CssMatch(&host, ":only-child", S_ONLY), 1);
    RoboCheckEqInt(CssMatch(&host, ":only-child", S_NEST_P), 1);
    RoboCheckEqInt(CssMatch(&host, ":only-child", S_PA), 0);
    RoboCheckEqInt(CssMatch(&host, ":only-child", S_NEST), 0);
    RoboCheckEqInt(CssMatch(&host, "p:only-child", S_ONLY), 1);
    RoboCheckEqInt(CssMatch(&host, "span:only-child", S_ONLY), 0);

    /* Both at once is exactly "only", for every element in the fixture. */
    {
        CssElementRef ref;

        for (ref = 1u; ref <= S_ROOT; ref++) {
            int both = CssMatch(&host, ":first-child", ref) &&
                       CssMatch(&host, ":last-child", ref);

            RoboCheckEqInt(both, CssMatch(&host, ":only-child", ref));
        }
    }
}

/* ------------------------------------------------------------------ */
/* :nth-child and :nth-last-child                                     */
/* ------------------------------------------------------------------ */

static void TestCssMatchNthChild(void)
{
    CssHost host;

    StructuralTree(&host);

    /* Absolute positions count elements, so `span` occupies the third position
     * even though it is not a `p`. */
    RoboCheckEqInt(CssMatch(&host, ":nth-child(1)", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(1)", S_PB), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(3)", S_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(6)", S_NEST), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(7)", S_PA), 0);
    RoboCheckEqInt(CssMatch(&host, "p:nth-child(2)", S_PB), 1);
    RoboCheckEqInt(CssMatch(&host, "span:nth-child(2)", S_PB), 0);

    RoboCheckEqInt(CssMatch(&host, ":nth-child(odd)", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(odd)", S_PB), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(odd)", S_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(odd)", S_PC), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(even)", S_PB), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(even)", S_PD), 0);
    /* 2n is the even positions: 2, 4 and 6, which are the second, fourth and
     * sixth children — so `span` (third) and `.d` (fifth) are both out. */
    RoboCheckEqInt(CssMatch(&host, ":nth-child(2n)", S_PB), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(2n)", S_PC), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(2n)", S_NEST), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(2n)", S_SPAN), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(2n)", S_PD), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(2n+1)", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(2n+1)", S_PB), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(3n)", S_PB), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(3n)", S_SPAN), 1);
    /* `n+4` is everything from the fourth child onwards. */
    RoboCheckEqInt(CssMatch(&host, ":nth-child(n+4)", S_SPAN), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(n+4)", S_PC), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(n+4)", S_NEST), 1);
    /* `-n+2` is the first two. */
    RoboCheckEqInt(CssMatch(&host, ":nth-child(-n+2)", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(-n+2)", S_PB), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(-n+2)", S_SPAN), 0);
    /* A single-child parent answers 1 and nothing else. */
    RoboCheckEqInt(CssMatch(&host, ":nth-child(1)", S_ONLY), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(2)", S_ONLY), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(odd)", S_ONLY), 1);
}

static void TestCssMatchNthLastChild(void)
{
    CssHost host;

    StructuralTree(&host);

    /* Counted from the end: six children means `:nth-last-child(1)` is the sixth
     * position, which is the element `:last-child` names. */
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(1)", S_NEST), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(1)", S_PD), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(2)", S_PD), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(6)", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(7)", S_PA), 0);
    /* From the back, a child of six has back-index 7 - front-index: `.d` is the
     * fifth child, so it is the second from the end and therefore even. */
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(odd)", S_NEST), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(odd)", S_PB), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(odd)", S_PD), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(odd)", S_PA), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(even)", S_PB), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(even)", S_PD), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(2n)", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(2n)", S_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(3n)", S_PB), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(3n)", S_PC), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(-n+2)", S_NEST), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(-n+2)", S_PD), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(-n+2)", S_PC), 0);

    /* Six children make the two directions mirror images: position 4 from the
     * front is position 3 from the back, and 1 from the back is 6 from the
     * front. An off-by-one in either walk breaks this pair. */
    RoboCheckEqInt(CssMatch(&host, ":nth-child(4)", S_PC), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(4)", S_PC), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(3)", S_PC), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(1)", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(6)", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(6)", S_PA), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(1)", S_PA), 0);
}

/* ------------------------------------------------------------------ */
/* :not                                                               */
/* ------------------------------------------------------------------ */

static void TestCssMatchNot(void)
{
    CssHost host;

    StructuralTree(&host);

    RoboCheckEqInt(CssMatch(&host, ":not(p)", S_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, ":not(p)", S_PA), 0);
    RoboCheckEqInt(CssMatch(&host, ":not(.a)", S_PA), 0);
    RoboCheckEqInt(CssMatch(&host, ":not(.a)", S_PB), 1);
    RoboCheckEqInt(CssMatch(&host, "p:not(.a)", S_PA), 0);
    RoboCheckEqInt(CssMatch(&host, "p:not(.a)", S_PB), 1);
    /* Negation of a compound, and negation inside a compound beside other
     * conditions: the whole negation has to hold, not one of its parts. */
    RoboCheckEqInt(CssMatch(&host, "p:not(.a):not(.b)", S_PB), 0);
    RoboCheckEqInt(CssMatch(&host, "p:not(.a):not(.b)", S_PC), 1);
    RoboCheckEqInt(CssMatch(&host, ":not(div)", S_LIST), 0);
    RoboCheckEqInt(CssMatch(&host, ":not(#list)", S_LIST), 0);
    RoboCheckEqInt(CssMatch(&host, ":not(*) ", S_PA), 0);

    /* `:not(:not(x))` is double negation, and only evaluates if the inner list
     * really is parsed rather than refunded to the source. */
    RoboCheckEqInt(CssMatch(&host, ":not(:not(p))", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, ":not(:not(p))", S_SPAN), 0);
    RoboCheckEqInt(CssMatch(&host, ":not(:not(.a))", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, ":not(:not(.a))", S_PB), 0);

    /* A list argument: matches when the element satisfies *none* of them. */
    RoboCheckEqInt(CssMatch(&host, ":not(.a, .b)", S_PC), 1);
    RoboCheckEqInt(CssMatch(&host, ":not(.a, .b)", S_PA), 0);
    RoboCheckEqInt(CssMatch(&host, ":not(.a, .b)", S_PB), 0);
    RoboCheckEqInt(CssMatch(&host, ":not(span, div)", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, ":not(span, div)", S_SPAN), 0);
    RoboCheckEqInt(CssMatch(&host, ":not(span, div)", S_LIST), 0);

    /* A complex argument: the inner list is a full selector list, combinators
     * included, so `:not(div > p)` negates the whole relationship. */
    RoboCheckEqInt(CssMatch(&host, ":not(div > p)", S_PA), 0);
    RoboCheckEqInt(CssMatch(&host, ":not(div > p)", S_ROOT), 1);
    RoboCheckEqInt(CssMatch(&host, ":not(#list > span)", S_SPAN), 0);
    RoboCheckEqInt(CssMatch(&host, ":not(#solo > p)", S_ONLY), 0);
    RoboCheckEqInt(CssMatch(&host, ":not(#list > p)", S_ONLY), 1);

    /* The falsity of a negation is a decided answer: `:not(:hover)` must not
     * come back as "matches" when `:hover` cannot be evaluated. */
    {
        CssSelectorMatch result;

        RoboCheckEqInt(CssMatchFull(&host, ":not(:hover)", S_PA, &result), 0);
        RoboCheckEqInt(result.matched, 0);
        RoboCheckEqInt(result.unsupported, 1);
        RoboCheckEqInt(CssMatchFull(&host, ":not(:hover, p)", S_PA, &result), 0);
        RoboCheckEqInt(result.unsupported, 1);
        /* … and the same inside a compound, beside conditions that do hold. */
        RoboCheckEqInt(CssMatchFull(&host, "p:not(:hover)", S_PA, &result), 0);
        RoboCheckEqInt(result.unsupported, 1);
    }
}

/* ------------------------------------------------------------------ */
/* Structural pseudo-classes beside combinators                        */
/* ------------------------------------------------------------------ */

static void TestCssMatchStructuralWithCombinators(void)
{
    CssHost host;

    StructuralTree(&host);

    RoboCheckEqInt(CssMatch(&host, "#list > :first-child", S_PA), 1);
    RoboCheckEqInt(CssMatch(&host, "#list > :first-child", S_PB), 0);
    RoboCheckEqInt(CssMatch(&host, "#list > :last-child", S_NEST), 1);
    RoboCheckEqInt(CssMatch(&host, ":first-child > p", S_NEST_P), 0);
    /* `#nest` is the sixth of six children, so it is nobody's only child — while
     * the root `#solo` has no sibling at all and so is one. */
    RoboCheckEqInt(CssMatch(&host, ":only-child > p", S_NEST_P), 0);
    RoboCheckEqInt(CssMatch(&host, ":only-child > p", S_ONLY), 1);
    RoboCheckEqInt(CssMatch(&host, "#solo > :only-child", S_ONLY), 1);
    RoboCheckEqInt(CssMatch(&host, "#list > p:not(.a)", S_PB), 1);
    RoboCheckEqInt(CssMatch(&host, "#list > p:not(.a)", S_PA), 0);
    /* Structural conditions compose with sibling combinators, which is where a
     * wrong sibling index shows up as a wrong element rather than as a missing
     * one. */
    RoboCheckEqInt(CssMatch(&host, ":first-child + p", S_PB), 1);
    /* The fourth child is `.c`, so the `p` after it is `.d` — and `#nest` is a
     * `div` four positions later, which `+` must not accept. */
    RoboCheckEqInt(CssMatch(&host, ":nth-child(4) + p", S_PD), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(4) + p", S_NEST), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(4) + div", S_NEST), 0);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(5) + div", S_NEST), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(3) + p", S_PD), 0);
    RoboCheckEqInt(CssMatch(&host, ":first-child ~ .d", S_PD), 1);
    RoboCheckEqInt(CssMatch(&host, ":last-child ~ .d", S_PD), 0);
    RoboCheckEqInt(CssMatch(&host, "p:first-child ~ span", S_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, "span ~ p:last-child", S_NEST), 0);
}

/* ------------------------------------------------------------------ */
/* Invariants                                                         */
/* ------------------------------------------------------------------ */

static void TestCssMatchStructuralInvariants(void)
{
    CssHost host;
    CssElementRef ref;

    /*
     * Relationships that must hold for every element in the fixture, whichever
     * it is. These catch an index that is off by one in *either* direction, which
     * a hand-written example can easily miss.
     */
    StructuralTree(&host);

    for (ref = 1u; ref <= S_ROOT; ref++) {
        int first = CssMatch(&host, ":first-child", ref);
        int last = CssMatch(&host, ":last-child", ref);
        int nth1 = CssMatch(&host, ":nth-child(1)", ref);
        int nthl1 = CssMatch(&host, ":nth-last-child(1)", ref);

        /* `:nth-child(1)` and `:first-child` are the same question. */
        RoboCheckEqInt(nth1, first);
        RoboCheckEqInt(nthl1, last);
        /* `:only-child` is the conjunction, which is what makes it redundant
         * rather than a third rule. */
        RoboCheckEqInt(CssMatch(&host, ":only-child", ref), first && last);
        /* `odd` and `even` partition. */
        RoboCheckEqInt(CssMatch(&host, ":nth-child(odd)", ref) +
                              CssMatch(&host, ":nth-child(even)", ref),
                          1);
        /* `-n+1` is the first child and nothing else. */
        RoboCheckEqInt(CssMatch(&host, ":nth-child(-n+1)", ref), first);
        /* `n` is every child, so it is the universal selector here. */
        RoboCheckEqInt(CssMatch(&host, ":nth-child(n)", ref), 1);
        /* The two directions are mirror images around a two-child parent; for
         * one child they coincide, which is the case this fixture can prove. */
        if (first && last) {
            RoboCheckEqInt(CssMatch(&host, ":nth-child(2)", ref), 0);
            RoboCheckEqInt(CssMatch(&host, ":nth-last-child(2)", ref), 0);
        }
    }
}

void TestRegisterCssSelectMatchStructural(void);

void TestRegisterCssSelectMatchStructural(void)
{
    TestRegister("css.select.match.root", TestCssMatchRoot);
    TestRegister("css.select.match.child_positions", TestCssMatchChildPositions);
    TestRegister("css.select.match.nth_child", TestCssMatchNthChild);
    TestRegister("css.select.match.nth_last_child", TestCssMatchNthLastChild);
    TestRegister("css.select.match.not", TestCssMatchNot);
    TestRegister("css.select.match.structural_with_combinators",
                       TestCssMatchStructuralWithCombinators);
    TestRegister("css.select.match.structural_invariants",
                       TestCssMatchStructuralInvariants);
}
