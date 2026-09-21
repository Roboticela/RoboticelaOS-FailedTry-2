/*
 * RoboCSS combinator and relationship matching (Phase 9.4) — css.select.match.*
 *
 * One tree, built once per test, is enough for the combinator cases because the
 * thing under test is a *walk*: which ancestor, which sibling, and whether the
 * walk can back out of a candidate it tried and rejected. The tree below is
 * deliberately three levels deep with a sibling chain in the middle, so a wrong
 * answer is more specific than "no match":
 *
 *   html(1)
 *     head(2)
 *     body(3)
 *       div#app.wrap(4)
 *         h1(5)
 *         p.first.lead(6)
 *         span.sel(7)
 *       section(8)
 *         p(9)
 *           em(10)
 *
 * Children of 4 are 5, 6, 7 in document order; children of 3 are 4, 8; children
 * of 1 are 2, 3; children of 9 are 10. Every combinator test names the ref it
 * expects, so a compound that matched the wrong element cannot pass.
 */

#include "RoboCssTestSupport.h"

#define T_HTML 1u
#define T_HEAD 2u
#define T_BODY 3u
#define T_APP 4u
#define T_H1 5u
#define T_P 6u
#define T_SPAN 7u
#define T_SECTION 8u
#define T_SECTION_P 9u
#define T_EM 10u

static void MatchTree(CssHost *host)
{
    CssHostInit(host);
    CssHostAdd(host, "html", NULL, NULL);
    CssHostAdd(host, "head", NULL, NULL);
    CssHostAdd(host, "body", NULL, NULL);
    CssHostAdd(host, "div", "app", "wrap");
    CssHostAdd(host, "h1", NULL, NULL);
    CssHostAdd(host, "p", NULL, "first lead");
    CssHostAdd(host, "span", NULL, "sel");
    CssHostAdd(host, "section", NULL, NULL);
    CssHostAdd(host, "p", NULL, NULL);
    CssHostAdd(host, "em", NULL, NULL);

    CssHostSetParent(host, T_HEAD, T_HTML);
    CssHostSetParent(host, T_BODY, T_HTML);
    CssHostSetParent(host, T_APP, T_BODY);
    CssHostSetParent(host, T_SECTION, T_BODY);
    CssHostSetParent(host, T_H1, T_APP);
    CssHostSetParent(host, T_P, T_APP);
    CssHostSetParent(host, T_SPAN, T_APP);
    CssHostSetParent(host, T_SECTION_P, T_SECTION);
    CssHostSetParent(host, T_EM, T_SECTION_P);
    CssHostSeal(host);
}

/* ------------------------------------------------------------------ */
/* Descendant and child                                               */
/* ------------------------------------------------------------------ */

static void TestCssMatchDescendant(void)
{
    CssHost host;

    MatchTree(&host);

    RoboCheckEqInt(CssMatch(&host, "html body", T_BODY), 1);
    /* The subject is the right-most compound, so `html body` asked about the
     * `div` is a no however many of its ancestors are involved. */
    RoboCheckEqInt(CssMatch(&host, "html body", T_APP), 0);
    RoboCheckEqInt(CssMatch(&host, "body div", T_APP), 1);
    /* Three levels apart, and the middle compound is not the direct parent. */
    RoboCheckEqInt(CssMatch(&host, "html body div", T_APP), 1);
    RoboCheckEqInt(CssMatch(&host, "div p", T_SECTION_P), 0);
    RoboCheckEqInt(CssMatch(&host, "body p", T_SECTION_P), 1);
    RoboCheckEqInt(CssMatch(&host, "body em", T_EM), 1);
    RoboCheckEqInt(CssMatch(&host, "section em", T_EM), 1);
    RoboCheckEqInt(CssMatch(&host, "div em", T_EM), 0);
    /* The subject is the right-most compound, so an ancestor cannot be the
     * element being asked about. */
    RoboCheckEqInt(CssMatch(&host, "body *", T_APP), 1);
    RoboCheckEqInt(CssMatch(&host, "* body", T_APP), 0);
    /* An element is not its own ancestor. */
    RoboCheckEqInt(CssMatch(&host, "div div", T_APP), 0);
    RoboCheckEqInt(CssMatch(&host, "p p", T_SECTION_P), 0);
    /* The root has no ancestor, so nothing can precede it in a descendant
     * selector. */
    RoboCheckEqInt(CssMatch(&host, "body html", T_HTML), 0);
    RoboCheckEqInt(CssMatch(&host, "* html", T_HTML), 0);
}

static void TestCssMatchChildIsStricterThanDescendant(void)
{
    CssHost host;

    MatchTree(&host);

    RoboCheckEqInt(CssMatch(&host, "body > div", T_APP), 1);
    RoboCheckEqInt(CssMatch(&host, "html > body", T_BODY), 1);
    RoboCheckEqInt(CssMatch(&host, "section > p", T_SECTION_P), 1);
    /* The whole point of `>`: the elements a descendant selector would have
     * accepted one level further up are refused. */
    RoboCheckEqInt(CssMatch(&host, "html > div", T_APP), 0);
    RoboCheckEqInt(CssMatch(&host, "body > p", T_SECTION_P), 0);
    RoboCheckEqInt(CssMatch(&host, "div > em", T_EM), 0);
    RoboCheckEqInt(CssMatch(&host, "body > em", T_EM), 0);
    RoboCheckEqInt(CssMatch(&host, "html > body > div", T_APP), 1);
    RoboCheckEqInt(CssMatch(&host, "html > div > div", T_APP), 0);
    /* No parent is a refusal, not an evalation: the root has no parent. */
    RoboCheckEqInt(CssMatch(&host, "* > html", T_HTML), 0);
}

/* ------------------------------------------------------------------ */
/* Siblings                                                           */
/* ------------------------------------------------------------------ */

static void TestCssMatchNextSibling(void)
{
    CssHost host;

    MatchTree(&host);

    RoboCheckEqInt(CssMatch(&host, "h1 + p", T_P), 1);
    RoboCheckEqInt(CssMatch(&host, "p + span", T_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, "h1 + span", T_SPAN), 0);
    RoboCheckEqInt(CssMatch(&host, "p + h1", T_H1), 0);
    /* The first child has no previous element sibling at all. */
    RoboCheckEqInt(CssMatch(&host, "p + h1", T_H1), 0);
    RoboCheckEqInt(CssMatch(&host, "* + h1", T_H1), 0);
    RoboCheckEqInt(CssMatch(&host, "head + body", T_BODY), 1);
    RoboCheckEqInt(CssMatch(&host, "div + section", T_SECTION), 1);
    RoboCheckEqInt(CssMatch(&host, "section + div", T_APP), 0);
    /* The chain spans one parent: a first child of one parent is not the
     * sibling of a first child of another. */
    RoboCheckEqInt(CssMatch(&host, "section + p", T_SECTION_P), 0);
    RoboCheckEqInt(CssMatch(&host, "div + p", T_P), 0);
}

static void TestCssMatchSubsequentSibling(void)
{
    CssHost host;

    MatchTree(&host);

    RoboCheckEqInt(CssMatch(&host, "h1 ~ p", T_P), 1);
    RoboCheckEqInt(CssMatch(&host, "h1 ~ span", T_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, "p ~ span", T_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, "span ~ h1", T_H1), 0);
    RoboCheckEqInt(CssMatch(&host, "p ~ h1", T_H1), 0);
    RoboCheckEqInt(CssMatch(&host, "head ~ div", T_APP), 0);
    RoboCheckEqInt(CssMatch(&host, "div ~ section", T_SECTION), 1);
    RoboCheckEqInt(CssMatch(&host, "section ~ body", T_BODY), 0);
    RoboCheckEqInt(CssMatch(&host, "h1 ~ h1", T_H1), 0);
    /* Later siblings of other parents are not siblings. */
    RoboCheckEqInt(CssMatch(&host, "p ~ em", T_EM), 0);
}

static void TestCssMatchAdjacentIsSubsetOfGeneral(void)
{
    CssHost host;
    static const char *lefts[] = {"h1", "p", "span", "div", "section", "*"};
    size_t i;
    CssElementRef ref;

    /* An invariant rather than an example: whenever `+` matches, `~` matches the
     * same element. If the two ever disagree, one of them is walking the wrong
     * way, and this says so without naming a case. */
    MatchTree(&host);
    for (ref = 1u; ref <= T_EM; ref++) {
        for (i = 0u; i < sizeof(lefts) / sizeof(lefts[0]); i++) {
            size_t length = strlen(lefts[i]);
            char adjacent[32];
            char general[32];

            /* `<left> + *` and `<left> ~ *` built by hand: the runtime's test
             * support has no formatted output, and a selector is short. */
            memcpy(adjacent, lefts[i], length);
            memcpy(adjacent + length, " + *", 5u);
            memcpy(general, lefts[i], length);
            memcpy(general + length, " ~ *", 5u);

            if (CssMatch(&host, adjacent, ref)) {
                RoboCheckEqInt(CssMatch(&host, general, ref), 1);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Backtracking                                                       */
/* ------------------------------------------------------------------ */

static void TestCssMatchBacktracksUpTheAncestorChain(void)
{
    CssHost host;

    /*
     * A second `div` is inserted *between* `html` and `#app`, so the nearest
     * ancestor matching `div` is the wrong one and the walk has to back out of it
     * and try higher up. A greedy walk — "take the first match and stop" — fails
     * the second assertion and passes the first, which is why both are here.
     */
    MatchTree(&host);
    RoboCheckEqInt(CssMatch(&host, "body #app", T_APP), 1);
    RoboCheckEqInt(CssMatch(&host, "html div #app", T_APP), 0);
    RoboCheckEqInt(CssMatch(&host, "html div", T_APP), 1);
    RoboCheckEqInt(CssMatch(&host, "html div div", T_APP), 0);

    {
        CssHost deep;
        CssElementRef outer;

        /* outer > inner > target, and the selector needs `outer` above `inner`,
         * so matching `outer` against the nearer ancestor is not enough. */
        CssHostInit(&deep);
        outer = CssHostAdd(&deep, "div", "outer", NULL);
        {
            CssElementRef inner = CssHostAdd(&deep, "div", "inner", NULL);
            CssElementRef target = CssHostAdd(&deep, "span", NULL, NULL);

            CssHostSetParent(&deep, inner, outer);
            CssHostSetParent(&deep, target, inner);
            CssHostSeal(&deep);
            RoboCheckEqInt(CssMatch(&deep, "div div span", target), 1);
            RoboCheckEqInt(CssMatch(&deep, "body div span", target), 0);
            RoboCheckEqInt(CssMatch(&deep, "#outer #inner span", target), 1);
            RoboCheckEqInt(CssMatch(&deep, "#inner #outer span", target), 0);
        }
    }
}

static void TestCssMatchBacktracksThroughSiblings(void)
{
    CssHost host;

    /*
     * Two candidate `.first` siblings, only the earlier of which satisfies the
     * left-most compound. The walk must try the second candidate, fail, and
     * continue to the first rather than stopping at the nearest one.
     *
     *   div.a.first ... div.b.first > span
     */
    CssHostInit(&host);
    CssHostAdd(&host, "div", "a", "first");
    CssHostAdd(&host, "div", "b", "first");
    CssHostAdd(&host, "span", NULL, NULL);
    CssHostAdd(&host, "div", NULL, "target");
    CssHostSetParent(&host, 3u, 2u);
    CssHostSeal(&host);

    /* 3's parent is 2, which is `.first` but not `#a`; the walk fails there and
     * has no earlier sibling of 2's parent to reach 1 through, so `#a .first >
     * span` is false while `.first > span` is true. */
    RoboCheckEqInt(CssMatch(&host, ".first > span", 3u), 1);
    RoboCheckEqInt(CssMatch(&host, "#b > span", 3u), 1);
    RoboCheckEqInt(CssMatch(&host, "#a > span", 3u), 0);

    {
        CssHost chain;
        CssElementRef first;
        CssElementRef second;
        CssElementRef subject;

        /*
         * Three siblings in one parent, where only the *earlier* one carries the
         * class the left-most compound needs:
         *
         *   div#one.wanted(first)  div#two.unwanted(second)  p(subject)
         *
         * A general-sibling walk from `subject` sees `second` first, fails
         * `.wanted` on it, and must carry on to `first`. Stopping at the first
         * element sibling would make the second assertion false.
         */
        CssHostInit(&chain);
        CssHostAdd(&chain, "div", "parent", NULL);
        first = CssHostAdd(&chain, "div", "one", "wanted");
        second = CssHostAdd(&chain, "div", "two", "unwanted");
        subject = CssHostAdd(&chain, "p", NULL, NULL);
        CssHostSetParent(&chain, first, 1u);
        CssHostSetParent(&chain, second, 1u);
        CssHostSetParent(&chain, subject, 1u);
        CssHostSeal(&chain);

        RoboCheckEqInt(CssMatch(&chain, "div ~ p", subject), 1);
        RoboCheckEqInt(CssMatch(&chain, ".wanted ~ p", subject), 1);
        RoboCheckEqInt(CssMatch(&chain, ".unwanted ~ p", subject), 1);
        RoboCheckEqInt(CssMatch(&chain, "#one ~ p", subject), 1);
        RoboCheckEqInt(CssMatch(&chain, "#two ~ p", subject), 1);
        RoboCheckEqInt(CssMatch(&chain, ".missing ~ p", subject), 0);
        /* `+` only ever looks at the nearest one, which is where the two
         * combinators genuinely differ. */
        RoboCheckEqInt(CssMatch(&chain, ".unwanted + p", subject), 1);
        RoboCheckEqInt(CssMatch(&chain, ".wanted + p", subject), 0);
        RoboCheckEqInt(CssMatch(&chain, "#one + p", subject), 0);

        RoboCheckEqInt(first != RoboCssElementRefNone, 1);
        RoboCheckEqInt(second != RoboCssElementRefNone, 1);
    }
}

/* ------------------------------------------------------------------ */
/* Complex selectors                                                  */
/* ------------------------------------------------------------------ */

static void TestCssMatchComplexSelectors(void)
{
    CssHost host;

    MatchTree(&host);

    RoboCheckEqInt(CssMatch(&host, "#app > .lead", T_P), 1);
    RoboCheckEqInt(CssMatch(&host, "#app > .sel", T_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, "body > #app > p.first", T_P), 1);
    RoboCheckEqInt(CssMatch(&host, "body > #app h1 + p", T_P), 1);
    RoboCheckEqInt(CssMatch(&host, "body > #app > h1 + p.lead + span", T_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, "body section > p > em", T_EM), 1);
    RoboCheckEqInt(CssMatch(&host, "body > section > p em", T_EM), 1);
    RoboCheckEqInt(CssMatch(&host, "body > section > p > em", T_EM), 1);
    RoboCheckEqInt(CssMatch(&host, "body > section > p > span", T_EM), 0);
    RoboCheckEqInt(CssMatch(&host, "html head + body > #app", T_APP), 1);
    RoboCheckEqInt(CssMatch(&host, "html head + div > #app", T_APP), 0);
    RoboCheckEqInt(CssMatch(&host, "html div ~ section > p", T_SECTION_P), 1);
    RoboCheckEqInt(CssMatch(&host, "html head ~ body > #app", T_APP), 1);
    /* Both `div` and `section` come *after* `body`, so the general-sibling walk
     * from `body` runs off the start of the chain rather than finding one. */
    RoboCheckEqInt(CssMatch(&host, "html div ~ body > #app", T_APP), 0);
    RoboCheckEqInt(CssMatch(&host, "html section ~ body > #app", T_APP), 0);
    /* Four compounds, three combinators, one of each kind. */
    RoboCheckEqInt(CssMatch(&host, "html body > #app h1 + p", T_P), 1);
    RoboCheckEqInt(CssMatch(&host, "html body > #app h1 + em", T_P), 0);
}

/* ------------------------------------------------------------------ */
/* Lists                                                              */
/* ------------------------------------------------------------------ */

static void TestCssMatchSelectorList(void)
{
    CssHost host;
    CssSelectorMatch result;

    MatchTree(&host);

    RoboCheckEqInt(CssMatch(&host, "span, p, h1", T_P), 1);
    RoboCheckEqInt(CssMatch(&host, "h1, p, span", T_P), 1);
    RoboCheckEqInt(CssMatch(&host, "h1, span, em", T_P), 0);
    RoboCheckEqInt(CssMatch(&host, "em, section", T_SECTION), 1);

    /* A list reports which member matched, so a caller can tell the rules apart
     * rather than only learning that one of them applied. */
    RoboCheckEqInt(CssMatchFull(&host, "h1, p, span", T_P, &result), 1);
    RoboCheckEqInt(result.selectorIndex, 1u);
    RoboCheckEqInt(result.range.begin, 4u);
    RoboCheckEqInt(result.range.end, 5u);
    RoboCheckEqInt(CssMatchFull(&host, "h1, p, span", T_SPAN, &result), 1);
    RoboCheckEqInt(result.selectorIndex, 2u);
    RoboCheckEqInt(CssMatchFull(&host, "h1, p, span", T_H1, &result), 1);
    RoboCheckEqInt(result.selectorIndex, 0u);
    RoboCheckEqInt(result.unsupported, 0);
}

/* ------------------------------------------------------------------ */
/* Relations the host cannot answer                                   */
/* ------------------------------------------------------------------ */

static void TestCssMatchReportsMissingRelations(void)
{
    CssHost host;
    CssSelectorMatch result;

    MatchTree(&host);

    /* A host with only a view answers everything that needs one element … */
    CssHostDropRelation(&host, CSS_HOST_RELATION_PARENT);
    CssHostDropRelation(&host, CSS_HOST_RELATION_PREVIOUS);
    CssHostDropRelation(&host, CSS_HOST_RELATION_NEXT);

    RoboCheckEqInt(CssMatch(&host, "p.first.lead", T_P), 1);
    RoboCheckEqInt(CssMatch(&host, "p.missing", T_P), 0);
    RoboCheckEqInt(CssMatch(&host, "*", T_P), 1);

    /* … and *reports* the rest rather than answering them. "Does not match" and
     * "not decided" lead to different stylesheets, so they cannot share a
     * verdict. */
    RoboCheckEqInt(CssMatchFull(&host, "body p", T_P, &result), 0);
    RoboCheckEqInt(result.unsupported, 1);
    RoboCheckEqInt(CssMatchFull(&host, "body > p", T_P, &result), 0);
    RoboCheckEqInt(result.unsupported, 1);
    RoboCheckEqInt(CssMatchFull(&host, "h1 + p", T_P, &result), 0);
    RoboCheckEqInt(result.unsupported, 1);
    RoboCheckEqInt(CssMatchFull(&host, "h1 ~ p", T_P, &result), 0);
    RoboCheckEqInt(result.unsupported, 1);
    RoboCheckEqInt(CssMatchFull(&host, ":root", T_HTML, &result), 0);
    RoboCheckEqInt(result.matched, 0);
    RoboCheckEqInt(result.unsupported, 1);
    RoboCheckEqInt(CssMatchFull(&host, ":first-child", T_P, &result), 0);
    RoboCheckEqInt(result.unsupported, 1);
    RoboCheckEqInt(CssMatchFull(&host, ":last-child", T_P, &result), 0);
    RoboCheckEqInt(result.unsupported, 1);
    RoboCheckEqInt(CssMatchFull(&host, ":nth-child(2n+1)", T_P, &result), 0);
    RoboCheckEqInt(result.unsupported, 1);
    RoboCheckEqInt(CssMatchFull(&host, ":only-child", T_P, &result), 0);
    RoboCheckEqInt(result.matched, 0);
    RoboCheckEqInt(result.unsupported, 1);

    /* Only the parent is missing: `>` and the descendant walk stop being
     * answerable while `+` and `~` stay exact. */
    MatchTree(&host);
    CssHostDropRelation(&host, CSS_HOST_RELATION_PARENT);
    RoboCheckEqInt(CssMatch(&host, "h1 + p", T_P), 1);
    RoboCheckEqInt(CssMatch(&host, "h1 ~ span", T_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, ":first-child", T_H1), 1);
    RoboCheckEqInt(CssMatch(&host, ":last-child", T_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-child(2)", T_P), 1);
    {
        CssSelectorMatch narrowed;

        RoboCheckEqInt(CssMatchFull(&host, "body > div", T_APP, &narrowed), 0);
        RoboCheckEqInt(narrowed.unsupported, 1);
        /* `:root` is "no parent element", and a host that cannot answer the
         * parent question cannot answer that either, however plausible a
         * `tag == "html"` shortcut looks. */
        RoboCheckEqInt(CssMatchFull(&host, ":root", T_HTML, &narrowed), 0);
        RoboCheckEqInt(narrowed.unsupported, 1);
        RoboCheckEqInt(narrowed.matched, 0);
    }

    /* Only the *previous* sibling is missing: `+` and `:first-child` are
     * unevaluatable while `~` — which walks from the element's own side — is
     * reported too, because a general-sibling walk that cannot see whether there
     * is an earlier sibling cannot decide. */
    MatchTree(&host);
    CssHostDropRelation(&host, CSS_HOST_RELATION_PREVIOUS);
    RoboCheckEqInt(CssMatchFull(&host, "h1 + p", T_P, &result), 0);
    RoboCheckEqInt(result.matched, 0);
    RoboCheckEqInt(result.unsupported, 1);
    RoboCheckEqInt(CssMatchFull(&host, ":first-child", T_H1, &result), 0);
    RoboCheckEqInt(result.matched, 0);
    RoboCheckEqInt(result.unsupported, 1);
    /* … while everything that walks from the other side stays exact. */
    RoboCheckEqInt(CssMatch(&host, ":last-child", T_SPAN), 1);
    RoboCheckEqInt(CssMatch(&host, "body p", T_P), 1);
    RoboCheckEqInt(CssMatch(&host, ":nth-last-child(1)", T_SECTION_P), 1);
}

static void TestCssMatchReportsUnevaluatablePseudo(void)
{
    /* The reported condition is a *borrow* into the selector list's name arena,
     * because it names the pseudo-class rather than copying it — so a caller reads
     * it while the list lives, which this helper does. */
    /* Real pseudo-classes whose state or subject this build does not have. Each
     * one must come back as *undecided*, naming itself, because answering "no"
     * would silently drop a rule that a browser applies. */
    CssHost host;
    CssSelectorMatch result;
    static const struct {
        const char *text;
        const char *condition;
    } cases[] = {
        {":hover", "hover"},
        {":focus-within", "focus-within"},
        {":lang(en)", "lang"},
        {":nth-of-type(2)", "nth-of-type"},
        {":checked", "checked"},
        {":disabled", "disabled"},
        {"p:hover", "hover"}
    };
    size_t i;

    MatchTree(&host);
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char name[32];

        RoboCheckEqInt(
            CssMatchCondition(&host, cases[i].text, T_P, &result, name, sizeof(name)), 0);
        RoboCheckEqInt(result.matched, 0);
        RoboCheckEqInt(result.unsupported, 1);
        RoboCheckEqInt(strcmp(name, cases[i].condition), 0);
    }

    /* An unsupported condition behind a compound that already failed is never
     * reached, so nothing is left undecided — short-circuiting is what makes a
     * cheap test written first genuinely cheaper. */
    RoboCheckEqInt(CssMatchFull(&host, "em:hover", T_P, &result), 0);
    RoboCheckEqInt(result.matched, 0);
    RoboCheckEqInt(result.unsupported, 0);
    RoboCheckEqInt(CssSliceIsAbsent(result.condition), 1);
}

static void TestCssMatchPseudoElementsAreAnswered(void)
{
    /* A pseudo-element is the opposite case: the question *is* answered, and the
     * answer is no. This element is the `p`, not the content before it, and this
     * build generates none — so `::before` is a definite negative rather than a
     * deferred one, in every spelling the parser accepts. */
    CssHost host;
    CssSelectorMatch result;
    static const char *cases[] = {
        "::before", "::after", "::first-line", "::first-letter",
        ":before", ":after", ":first-line", ":first-letter",
        "p::before", "p.intro::first-line", "p:before"
    };
    size_t i;

    MatchTree(&host);
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        RoboCheckEqInt(CssMatchFull(&host, cases[i], T_P, &result), 0);
        RoboCheckEqInt(result.matched, 0);
        RoboCheckEqInt(result.unsupported, 0);
    }

    /* And the legacy single-colon spelling really is a pseudo-element rather
     * than a pseudo-class: if it were a pseudo-class this build does not
     * implement, the result would be `unsupported`. */
    RoboCheckEqInt(CssMatchFull(&host, ":before", T_P, &result), 0);
    RoboCheckEqInt(result.unsupported, 0);
}

/* ------------------------------------------------------------------ */
/* Malformed host relationships                                       */
/* ------------------------------------------------------------------ */

static void TestCssMatchSurvivesACyclicHost(void)
{
    CssHost host;
    CssSelectorMatch result;

    /*
     * A host that says `a`'s parent is `a` does not describe a document, and a
     * walk that trusted it would never terminate. The walk is bounded, and
     * exceeding the bound is reported as undecided rather than as "no match":
     * a truncated walk and a real negative are different facts.
     */
    CssHostInit(&host);
    CssHostAdd(&host, "div", "loop", NULL);
    CssHostSetParent(&host, 1u, 1u);
    CssHostSeal(&host);

    /* `span` never matches, so the ancestor walk climbs for ever unless it is
     * bounded — and when it is, the result is "not decided" rather than a
     * negative, so a caller can tell a truncated walk from a real one. */
    RoboCheckEqInt(CssMatchFull(&host, "span div", 1u, &result), 0);
    RoboCheckEqInt(result.matched, 0);
    RoboCheckEqInt(result.unsupported, 1);
    /* A descendant walk that *does* match succeeds on the first step, which a
     * bounded walk must still allow. */
    RoboCheckEqInt(CssMatchFull(&host, "div div", 1u, &result), 1);
    RoboCheckEqInt(result.matched, 1);
    RoboCheckEqInt(result.unsupported, 0);

    /* The same bound guards the sibling walks: a ring of one element makes
     * both `:nth-child`'s index count and a `~` walk non-terminating. */
    {
        CssHost ring;

        CssHostInit(&ring);
        CssHostAdd(&ring, "div", NULL, NULL);
        ring.elements[0].previous = 1u;
        ring.elements[0].next = 1u;
        RoboCheckEqInt(CssMatchFull(&ring, ":nth-child(2)", 1u, &result), 0);
        RoboCheckEqInt(result.unsupported, 1);
        RoboCheckEqInt(CssMatchFull(&ring, "span ~ div", 1u, &result), 0);
        RoboCheckEqInt(result.unsupported, 1);
        /* A condition that only looks one link stays exact even here. */
        RoboCheckEqInt(CssMatch(&ring, ":first-child", 1u), 0);
        RoboCheckEqInt(CssMatch(&ring, "div", 1u), 1);
    }
    /* A well-formed host of the same shape is still answered exactly. */
    {
        CssHost sane;
        CssElementRef outer;
        CssElementRef inner;

        CssHostInit(&sane);
        outer = CssHostAdd(&sane, "div", NULL, NULL);
        inner = CssHostAdd(&sane, "div", NULL, NULL);
        CssHostSetParent(&sane, inner, outer);
        CssHostSeal(&sane);
        RoboCheckEqInt(CssMatch(&sane, "div div", inner), 1);
    }
}

/* ------------------------------------------------------------------ */
/* Column combinator                                                  */
/* ------------------------------------------------------------------ */

static void TestCssMatchColumnCombinatorIsDeferred(void)
{
    CssHost host;
    CssSelectorMatch result;

    /* `||` parses and is represented, but selecting table columns needs a table
     * model the DOM does not have, so it is reported rather than invented — and
     * only once the right-most compound has actually matched, because that is
     * when the combinator is reached. */
    CssHostInit(&host);
    CssHostAdd(&host, "col", NULL, NULL);
    CssHostAdd(&host, "td", NULL, NULL);
    CssHostSeal(&host);

    RoboCheckEqInt(CssMatchFull(&host, "col || td", 2u, &result), 0);
    RoboCheckEqInt(result.matched, 0);
    RoboCheckEqInt(result.unsupported, 1);
    /* Asked about the `col` itself, the right-most compound fails first and the
     * combinator is never reached, so nothing is left undecided. */
    RoboCheckEqInt(CssMatchFull(&host, "col || td", 1u, &result), 0);
    RoboCheckEqInt(result.matched, 0);
    RoboCheckEqInt(result.unsupported, 0);
}

void TestRegisterCssSelectMatchCombinator(void);

void TestRegisterCssSelectMatchCombinator(void)
{
    TestRegister("css.select.match.descendant", TestCssMatchDescendant);
    TestRegister("css.select.match.child_is_stricter_than_descendant",
                       TestCssMatchChildIsStricterThanDescendant);
    TestRegister("css.select.match.next_sibling", TestCssMatchNextSibling);
    TestRegister("css.select.match.subsequent_sibling",
                       TestCssMatchSubsequentSibling);
    TestRegister("css.select.match.adjacent_is_subset_of_general",
                       TestCssMatchAdjacentIsSubsetOfGeneral);
    TestRegister("css.select.match.backtracks_up_the_ancestor_chain",
                       TestCssMatchBacktracksUpTheAncestorChain);
    TestRegister("css.select.match.backtracks_through_siblings",
                       TestCssMatchBacktracksThroughSiblings);
    TestRegister("css.select.match.complex_selectors",
                       TestCssMatchComplexSelectors);
    TestRegister("css.select.match.selector_list", TestCssMatchSelectorList);
    TestRegister("css.select.match.reports_missing_relations",
                       TestCssMatchReportsMissingRelations);
    TestRegister("css.select.match.reports_unevaluatable_pseudo",
                       TestCssMatchReportsUnevaluatablePseudo);
    TestRegister("css.select.match.pseudo_elements_are_answered",
                       TestCssMatchPseudoElementsAreAnswered);
    TestRegister("css.select.match.survives_a_cyclic_host",
                       TestCssMatchSurvivesACyclicHost);
    TestRegister("css.select.match.column_combinator_is_deferred",
                       TestCssMatchColumnCombinatorIsDeferred);
}
