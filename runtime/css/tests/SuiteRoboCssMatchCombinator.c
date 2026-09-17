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

static void match_tree(css_host_t *host)
{
    css_host_init(host);
    css_host_add(host, "html", NULL, NULL);
    css_host_add(host, "head", NULL, NULL);
    css_host_add(host, "body", NULL, NULL);
    css_host_add(host, "div", "app", "wrap");
    css_host_add(host, "h1", NULL, NULL);
    css_host_add(host, "p", NULL, "first lead");
    css_host_add(host, "span", NULL, "sel");
    css_host_add(host, "section", NULL, NULL);
    css_host_add(host, "p", NULL, NULL);
    css_host_add(host, "em", NULL, NULL);

    css_host_set_parent(host, T_HEAD, T_HTML);
    css_host_set_parent(host, T_BODY, T_HTML);
    css_host_set_parent(host, T_APP, T_BODY);
    css_host_set_parent(host, T_SECTION, T_BODY);
    css_host_set_parent(host, T_H1, T_APP);
    css_host_set_parent(host, T_P, T_APP);
    css_host_set_parent(host, T_SPAN, T_APP);
    css_host_set_parent(host, T_SECTION_P, T_SECTION);
    css_host_set_parent(host, T_EM, T_SECTION_P);
    css_host_seal(host);
}

/* ------------------------------------------------------------------ */
/* Descendant and child                                               */
/* ------------------------------------------------------------------ */

static void test_css_match_descendant(void)
{
    css_host_t host;

    match_tree(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "html body", T_BODY), 1);
    /* The subject is the right-most compound, so `html body` asked about the
     * `div` is a no however many of its ancestors are involved. */
    ROBO_CHECK_EQ_INT(css_match(&host, "html body", T_APP), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "body div", T_APP), 1);
    /* Three levels apart, and the middle compound is not the direct parent. */
    ROBO_CHECK_EQ_INT(css_match(&host, "html body div", T_APP), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "div p", T_SECTION_P), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "body p", T_SECTION_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "body em", T_EM), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "section em", T_EM), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "div em", T_EM), 0);
    /* The subject is the right-most compound, so an ancestor cannot be the
     * element being asked about. */
    ROBO_CHECK_EQ_INT(css_match(&host, "body *", T_APP), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "* body", T_APP), 0);
    /* An element is not its own ancestor. */
    ROBO_CHECK_EQ_INT(css_match(&host, "div div", T_APP), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "p p", T_SECTION_P), 0);
    /* The root has no ancestor, so nothing can precede it in a descendant
     * selector. */
    ROBO_CHECK_EQ_INT(css_match(&host, "body html", T_HTML), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "* html", T_HTML), 0);
}

static void test_css_match_child_is_stricter_than_descendant(void)
{
    css_host_t host;

    match_tree(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "body > div", T_APP), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "html > body", T_BODY), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "section > p", T_SECTION_P), 1);
    /* The whole point of `>`: the elements a descendant selector would have
     * accepted one level further up are refused. */
    ROBO_CHECK_EQ_INT(css_match(&host, "html > div", T_APP), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "body > p", T_SECTION_P), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "div > em", T_EM), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "body > em", T_EM), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "html > body > div", T_APP), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "html > div > div", T_APP), 0);
    /* No parent is a refusal, not an evalation: the root has no parent. */
    ROBO_CHECK_EQ_INT(css_match(&host, "* > html", T_HTML), 0);
}

/* ------------------------------------------------------------------ */
/* Siblings                                                           */
/* ------------------------------------------------------------------ */

static void test_css_match_next_sibling(void)
{
    css_host_t host;

    match_tree(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "h1 + p", T_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "p + span", T_SPAN), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "h1 + span", T_SPAN), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "p + h1", T_H1), 0);
    /* The first child has no previous element sibling at all. */
    ROBO_CHECK_EQ_INT(css_match(&host, "p + h1", T_H1), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "* + h1", T_H1), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "head + body", T_BODY), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "div + section", T_SECTION), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "section + div", T_APP), 0);
    /* The chain spans one parent: a first child of one parent is not the
     * sibling of a first child of another. */
    ROBO_CHECK_EQ_INT(css_match(&host, "section + p", T_SECTION_P), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "div + p", T_P), 0);
}

static void test_css_match_subsequent_sibling(void)
{
    css_host_t host;

    match_tree(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "h1 ~ p", T_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "h1 ~ span", T_SPAN), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "p ~ span", T_SPAN), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "span ~ h1", T_H1), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "p ~ h1", T_H1), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "head ~ div", T_APP), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "div ~ section", T_SECTION), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "section ~ body", T_BODY), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "h1 ~ h1", T_H1), 0);
    /* Later siblings of other parents are not siblings. */
    ROBO_CHECK_EQ_INT(css_match(&host, "p ~ em", T_EM), 0);
}

static void test_css_match_adjacent_is_subset_of_general(void)
{
    css_host_t host;
    static const char *lefts[] = {"h1", "p", "span", "div", "section", "*"};
    size_t i;
    robo_css_element_ref_t ref;

    /* An invariant rather than an example: whenever `+` matches, `~` matches the
     * same element. If the two ever disagree, one of them is walking the wrong
     * way, and this says so without naming a case. */
    match_tree(&host);
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

            if (css_match(&host, adjacent, ref)) {
                ROBO_CHECK_EQ_INT(css_match(&host, general, ref), 1);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Backtracking                                                       */
/* ------------------------------------------------------------------ */

static void test_css_match_backtracks_up_the_ancestor_chain(void)
{
    css_host_t host;

    /*
     * A second `div` is inserted *between* `html` and `#app`, so the nearest
     * ancestor matching `div` is the wrong one and the walk has to back out of it
     * and try higher up. A greedy walk — "take the first match and stop" — fails
     * the second assertion and passes the first, which is why both are here.
     */
    match_tree(&host);
    ROBO_CHECK_EQ_INT(css_match(&host, "body #app", T_APP), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "html div #app", T_APP), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "html div", T_APP), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "html div div", T_APP), 0);

    {
        css_host_t deep;
        robo_css_element_ref_t outer;

        /* outer > inner > target, and the selector needs `outer` above `inner`,
         * so matching `outer` against the nearer ancestor is not enough. */
        css_host_init(&deep);
        outer = css_host_add(&deep, "div", "outer", NULL);
        {
            robo_css_element_ref_t inner = css_host_add(&deep, "div", "inner", NULL);
            robo_css_element_ref_t target = css_host_add(&deep, "span", NULL, NULL);

            css_host_set_parent(&deep, inner, outer);
            css_host_set_parent(&deep, target, inner);
            css_host_seal(&deep);
            ROBO_CHECK_EQ_INT(css_match(&deep, "div div span", target), 1);
            ROBO_CHECK_EQ_INT(css_match(&deep, "body div span", target), 0);
            ROBO_CHECK_EQ_INT(css_match(&deep, "#outer #inner span", target), 1);
            ROBO_CHECK_EQ_INT(css_match(&deep, "#inner #outer span", target), 0);
        }
    }
}

static void test_css_match_backtracks_through_siblings(void)
{
    css_host_t host;

    /*
     * Two candidate `.first` siblings, only the earlier of which satisfies the
     * left-most compound. The walk must try the second candidate, fail, and
     * continue to the first rather than stopping at the nearest one.
     *
     *   div.a.first ... div.b.first > span
     */
    css_host_init(&host);
    css_host_add(&host, "div", "a", "first");
    css_host_add(&host, "div", "b", "first");
    css_host_add(&host, "span", NULL, NULL);
    css_host_add(&host, "div", NULL, "target");
    css_host_set_parent(&host, 3u, 2u);
    css_host_seal(&host);

    /* 3's parent is 2, which is `.first` but not `#a`; the walk fails there and
     * has no earlier sibling of 2's parent to reach 1 through, so `#a .first >
     * span` is false while `.first > span` is true. */
    ROBO_CHECK_EQ_INT(css_match(&host, ".first > span", 3u), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "#b > span", 3u), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "#a > span", 3u), 0);

    {
        css_host_t chain;
        robo_css_element_ref_t first;
        robo_css_element_ref_t second;
        robo_css_element_ref_t subject;

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
        css_host_init(&chain);
        css_host_add(&chain, "div", "parent", NULL);
        first = css_host_add(&chain, "div", "one", "wanted");
        second = css_host_add(&chain, "div", "two", "unwanted");
        subject = css_host_add(&chain, "p", NULL, NULL);
        css_host_set_parent(&chain, first, 1u);
        css_host_set_parent(&chain, second, 1u);
        css_host_set_parent(&chain, subject, 1u);
        css_host_seal(&chain);

        ROBO_CHECK_EQ_INT(css_match(&chain, "div ~ p", subject), 1);
        ROBO_CHECK_EQ_INT(css_match(&chain, ".wanted ~ p", subject), 1);
        ROBO_CHECK_EQ_INT(css_match(&chain, ".unwanted ~ p", subject), 1);
        ROBO_CHECK_EQ_INT(css_match(&chain, "#one ~ p", subject), 1);
        ROBO_CHECK_EQ_INT(css_match(&chain, "#two ~ p", subject), 1);
        ROBO_CHECK_EQ_INT(css_match(&chain, ".missing ~ p", subject), 0);
        /* `+` only ever looks at the nearest one, which is where the two
         * combinators genuinely differ. */
        ROBO_CHECK_EQ_INT(css_match(&chain, ".unwanted + p", subject), 1);
        ROBO_CHECK_EQ_INT(css_match(&chain, ".wanted + p", subject), 0);
        ROBO_CHECK_EQ_INT(css_match(&chain, "#one + p", subject), 0);

        ROBO_CHECK_EQ_INT(first != ROBO_CSS_ELEMENT_REF_NONE, 1);
        ROBO_CHECK_EQ_INT(second != ROBO_CSS_ELEMENT_REF_NONE, 1);
    }
}

/* ------------------------------------------------------------------ */
/* Complex selectors                                                  */
/* ------------------------------------------------------------------ */

static void test_css_match_complex_selectors(void)
{
    css_host_t host;

    match_tree(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "#app > .lead", T_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "#app > .sel", T_SPAN), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "body > #app > p.first", T_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "body > #app h1 + p", T_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "body > #app > h1 + p.lead + span", T_SPAN), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "body section > p > em", T_EM), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "body > section > p em", T_EM), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "body > section > p > em", T_EM), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "body > section > p > span", T_EM), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "html head + body > #app", T_APP), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "html head + div > #app", T_APP), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "html div ~ section > p", T_SECTION_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "html head ~ body > #app", T_APP), 1);
    /* Both `div` and `section` come *after* `body`, so the general-sibling walk
     * from `body` runs off the start of the chain rather than finding one. */
    ROBO_CHECK_EQ_INT(css_match(&host, "html div ~ body > #app", T_APP), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "html section ~ body > #app", T_APP), 0);
    /* Four compounds, three combinators, one of each kind. */
    ROBO_CHECK_EQ_INT(css_match(&host, "html body > #app h1 + p", T_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "html body > #app h1 + em", T_P), 0);
}

/* ------------------------------------------------------------------ */
/* Lists                                                              */
/* ------------------------------------------------------------------ */

static void test_css_match_selector_list(void)
{
    css_host_t host;
    robo_css_selector_match_t result;

    match_tree(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "span, p, h1", T_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "h1, p, span", T_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "h1, span, em", T_P), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "em, section", T_SECTION), 1);

    /* A list reports which member matched, so a caller can tell the rules apart
     * rather than only learning that one of them applied. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, "h1, p, span", T_P, &result), 1);
    ROBO_CHECK_EQ_INT(result.selector_index, 1u);
    ROBO_CHECK_EQ_INT(result.range.begin, 4u);
    ROBO_CHECK_EQ_INT(result.range.end, 5u);
    ROBO_CHECK_EQ_INT(css_match_full(&host, "h1, p, span", T_SPAN, &result), 1);
    ROBO_CHECK_EQ_INT(result.selector_index, 2u);
    ROBO_CHECK_EQ_INT(css_match_full(&host, "h1, p, span", T_H1, &result), 1);
    ROBO_CHECK_EQ_INT(result.selector_index, 0u);
    ROBO_CHECK_EQ_INT(result.unsupported, 0);
}

/* ------------------------------------------------------------------ */
/* Relations the host cannot answer                                   */
/* ------------------------------------------------------------------ */

static void test_css_match_reports_missing_relations(void)
{
    css_host_t host;
    robo_css_selector_match_t result;

    match_tree(&host);

    /* A host with only a view answers everything that needs one element … */
    css_host_drop_relation(&host, CSS_HOST_RELATION_PARENT);
    css_host_drop_relation(&host, CSS_HOST_RELATION_PREVIOUS);
    css_host_drop_relation(&host, CSS_HOST_RELATION_NEXT);

    ROBO_CHECK_EQ_INT(css_match(&host, "p.first.lead", T_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "p.missing", T_P), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "*", T_P), 1);

    /* … and *reports* the rest rather than answering them. "Does not match" and
     * "not decided" lead to different stylesheets, so they cannot share a
     * verdict. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, "body p", T_P, &result), 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    ROBO_CHECK_EQ_INT(css_match_full(&host, "body > p", T_P, &result), 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    ROBO_CHECK_EQ_INT(css_match_full(&host, "h1 + p", T_P, &result), 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    ROBO_CHECK_EQ_INT(css_match_full(&host, "h1 ~ p", T_P, &result), 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    ROBO_CHECK_EQ_INT(css_match_full(&host, ":root", T_HTML, &result), 0);
    ROBO_CHECK_EQ_INT(result.matched, 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    ROBO_CHECK_EQ_INT(css_match_full(&host, ":first-child", T_P, &result), 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    ROBO_CHECK_EQ_INT(css_match_full(&host, ":last-child", T_P, &result), 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    ROBO_CHECK_EQ_INT(css_match_full(&host, ":nth-child(2n+1)", T_P, &result), 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    ROBO_CHECK_EQ_INT(css_match_full(&host, ":only-child", T_P, &result), 0);
    ROBO_CHECK_EQ_INT(result.matched, 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);

    /* Only the parent is missing: `>` and the descendant walk stop being
     * answerable while `+` and `~` stay exact. */
    match_tree(&host);
    css_host_drop_relation(&host, CSS_HOST_RELATION_PARENT);
    ROBO_CHECK_EQ_INT(css_match(&host, "h1 + p", T_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "h1 ~ span", T_SPAN), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, ":first-child", T_H1), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, ":last-child", T_SPAN), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, ":nth-child(2)", T_P), 1);
    {
        robo_css_selector_match_t narrowed;

        ROBO_CHECK_EQ_INT(css_match_full(&host, "body > div", T_APP, &narrowed), 0);
        ROBO_CHECK_EQ_INT(narrowed.unsupported, 1);
        /* `:root` is "no parent element", and a host that cannot answer the
         * parent question cannot answer that either, however plausible a
         * `tag == "html"` shortcut looks. */
        ROBO_CHECK_EQ_INT(css_match_full(&host, ":root", T_HTML, &narrowed), 0);
        ROBO_CHECK_EQ_INT(narrowed.unsupported, 1);
        ROBO_CHECK_EQ_INT(narrowed.matched, 0);
    }

    /* Only the *previous* sibling is missing: `+` and `:first-child` are
     * unevaluatable while `~` — which walks from the element's own side — is
     * reported too, because a general-sibling walk that cannot see whether there
     * is an earlier sibling cannot decide. */
    match_tree(&host);
    css_host_drop_relation(&host, CSS_HOST_RELATION_PREVIOUS);
    ROBO_CHECK_EQ_INT(css_match_full(&host, "h1 + p", T_P, &result), 0);
    ROBO_CHECK_EQ_INT(result.matched, 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    ROBO_CHECK_EQ_INT(css_match_full(&host, ":first-child", T_H1, &result), 0);
    ROBO_CHECK_EQ_INT(result.matched, 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    /* … while everything that walks from the other side stays exact. */
    ROBO_CHECK_EQ_INT(css_match(&host, ":last-child", T_SPAN), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "body p", T_P), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, ":nth-last-child(1)", T_SECTION_P), 1);
}

static void test_css_match_reports_unevaluatable_pseudo(void)
{
    /* The reported condition is a *borrow* into the selector list's name arena,
     * because it names the pseudo-class rather than copying it — so a caller reads
     * it while the list lives, which this helper does. */
    /* Real pseudo-classes whose state or subject this build does not have. Each
     * one must come back as *undecided*, naming itself, because answering "no"
     * would silently drop a rule that a browser applies. */
    css_host_t host;
    robo_css_selector_match_t result;
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

    match_tree(&host);
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char name[32];

        ROBO_CHECK_EQ_INT(
            css_match_condition(&host, cases[i].text, T_P, &result, name, sizeof(name)), 0);
        ROBO_CHECK_EQ_INT(result.matched, 0);
        ROBO_CHECK_EQ_INT(result.unsupported, 1);
        ROBO_CHECK_EQ_INT(strcmp(name, cases[i].condition), 0);
    }

    /* An unsupported condition behind a compound that already failed is never
     * reached, so nothing is left undecided — short-circuiting is what makes a
     * cheap test written first genuinely cheaper. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, "em:hover", T_P, &result), 0);
    ROBO_CHECK_EQ_INT(result.matched, 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 0);
    ROBO_CHECK_EQ_INT(robo_css_slice_is_absent(result.condition), 1);
}

static void test_css_match_pseudo_elements_are_answered(void)
{
    /* A pseudo-element is the opposite case: the question *is* answered, and the
     * answer is no. This element is the `p`, not the content before it, and this
     * build generates none — so `::before` is a definite negative rather than a
     * deferred one, in every spelling the parser accepts. */
    css_host_t host;
    robo_css_selector_match_t result;
    static const char *cases[] = {
        "::before", "::after", "::first-line", "::first-letter",
        ":before", ":after", ":first-line", ":first-letter",
        "p::before", "p.intro::first-line", "p:before"
    };
    size_t i;

    match_tree(&host);
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ROBO_CHECK_EQ_INT(css_match_full(&host, cases[i], T_P, &result), 0);
        ROBO_CHECK_EQ_INT(result.matched, 0);
        ROBO_CHECK_EQ_INT(result.unsupported, 0);
    }

    /* And the legacy single-colon spelling really is a pseudo-element rather
     * than a pseudo-class: if it were a pseudo-class this build does not
     * implement, the result would be `unsupported`. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, ":before", T_P, &result), 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 0);
}

/* ------------------------------------------------------------------ */
/* Malformed host relationships                                       */
/* ------------------------------------------------------------------ */

static void test_css_match_survives_a_cyclic_host(void)
{
    css_host_t host;
    robo_css_selector_match_t result;

    /*
     * A host that says `a`'s parent is `a` does not describe a document, and a
     * walk that trusted it would never terminate. The walk is bounded, and
     * exceeding the bound is reported as undecided rather than as "no match":
     * a truncated walk and a real negative are different facts.
     */
    css_host_init(&host);
    css_host_add(&host, "div", "loop", NULL);
    css_host_set_parent(&host, 1u, 1u);
    css_host_seal(&host);

    /* `span` never matches, so the ancestor walk climbs for ever unless it is
     * bounded — and when it is, the result is "not decided" rather than a
     * negative, so a caller can tell a truncated walk from a real one. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, "span div", 1u, &result), 0);
    ROBO_CHECK_EQ_INT(result.matched, 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    /* A descendant walk that *does* match succeeds on the first step, which a
     * bounded walk must still allow. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, "div div", 1u, &result), 1);
    ROBO_CHECK_EQ_INT(result.matched, 1);
    ROBO_CHECK_EQ_INT(result.unsupported, 0);

    /* The same bound guards the sibling walks: a ring of one element makes
     * both `:nth-child`'s index count and a `~` walk non-terminating. */
    {
        css_host_t ring;

        css_host_init(&ring);
        css_host_add(&ring, "div", NULL, NULL);
        ring.elements[0].previous = 1u;
        ring.elements[0].next = 1u;
        ROBO_CHECK_EQ_INT(css_match_full(&ring, ":nth-child(2)", 1u, &result), 0);
        ROBO_CHECK_EQ_INT(result.unsupported, 1);
        ROBO_CHECK_EQ_INT(css_match_full(&ring, "span ~ div", 1u, &result), 0);
        ROBO_CHECK_EQ_INT(result.unsupported, 1);
        /* A condition that only looks one link stays exact even here. */
        ROBO_CHECK_EQ_INT(css_match(&ring, ":first-child", 1u), 0);
        ROBO_CHECK_EQ_INT(css_match(&ring, "div", 1u), 1);
    }
    /* A well-formed host of the same shape is still answered exactly. */
    {
        css_host_t sane;
        robo_css_element_ref_t outer;
        robo_css_element_ref_t inner;

        css_host_init(&sane);
        outer = css_host_add(&sane, "div", NULL, NULL);
        inner = css_host_add(&sane, "div", NULL, NULL);
        css_host_set_parent(&sane, inner, outer);
        css_host_seal(&sane);
        ROBO_CHECK_EQ_INT(css_match(&sane, "div div", inner), 1);
    }
}

/* ------------------------------------------------------------------ */
/* Column combinator                                                  */
/* ------------------------------------------------------------------ */

static void test_css_match_column_combinator_is_deferred(void)
{
    css_host_t host;
    robo_css_selector_match_t result;

    /* `||` parses and is represented, but selecting table columns needs a table
     * model the DOM does not have, so it is reported rather than invented — and
     * only once the right-most compound has actually matched, because that is
     * when the combinator is reached. */
    css_host_init(&host);
    css_host_add(&host, "col", NULL, NULL);
    css_host_add(&host, "td", NULL, NULL);
    css_host_seal(&host);

    ROBO_CHECK_EQ_INT(css_match_full(&host, "col || td", 2u, &result), 0);
    ROBO_CHECK_EQ_INT(result.matched, 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    /* Asked about the `col` itself, the right-most compound fails first and the
     * combinator is never reached, so nothing is left undecided. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, "col || td", 1u, &result), 0);
    ROBO_CHECK_EQ_INT(result.matched, 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 0);
}

void robo_test_register_css_select_match_combinator(void);

void robo_test_register_css_select_match_combinator(void)
{
    robo_test_register("css.select.match.descendant", test_css_match_descendant);
    robo_test_register("css.select.match.child_is_stricter_than_descendant",
                       test_css_match_child_is_stricter_than_descendant);
    robo_test_register("css.select.match.next_sibling", test_css_match_next_sibling);
    robo_test_register("css.select.match.subsequent_sibling",
                       test_css_match_subsequent_sibling);
    robo_test_register("css.select.match.adjacent_is_subset_of_general",
                       test_css_match_adjacent_is_subset_of_general);
    robo_test_register("css.select.match.backtracks_up_the_ancestor_chain",
                       test_css_match_backtracks_up_the_ancestor_chain);
    robo_test_register("css.select.match.backtracks_through_siblings",
                       test_css_match_backtracks_through_siblings);
    robo_test_register("css.select.match.complex_selectors",
                       test_css_match_complex_selectors);
    robo_test_register("css.select.match.selector_list", test_css_match_selector_list);
    robo_test_register("css.select.match.reports_missing_relations",
                       test_css_match_reports_missing_relations);
    robo_test_register("css.select.match.reports_unevaluatable_pseudo",
                       test_css_match_reports_unevaluatable_pseudo);
    robo_test_register("css.select.match.pseudo_elements_are_answered",
                       test_css_match_pseudo_elements_are_answered);
    robo_test_register("css.select.match.survives_a_cyclic_host",
                       test_css_match_survives_a_cyclic_host);
    robo_test_register("css.select.match.column_combinator_is_deferred",
                       test_css_match_column_combinator_is_deferred);
}
