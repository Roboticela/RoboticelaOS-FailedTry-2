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

static void test_css_specificity_value(void)
{
    robo_css_specificity_t a;
    robo_css_specificity_t b;
    robo_css_specificity_t zero;

    robo_css_specificity_init(&zero);
    ROBO_CHECK_EQ_INT(robo_css_specificity_is_zero(&zero), 1);
    ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&zero, &zero), 1);
    ROBO_CHECK_EQ_INT(robo_css_specificity_compare(&zero, &zero), 0);

    /* Adding builds a tuple component by component, and the components never
     * bleed into one another. */
    robo_css_specificity_init(&a);
    a.id = 1u;
    robo_css_specificity_init(&b);
    b.class_like = 2u;
    b.type_like = 3u;
    robo_css_specificity_add(&a, &b);
    css_specificity_is(a, 1u, 2u, 3u);
    ROBO_CHECK_EQ_INT(robo_css_specificity_is_zero(&a), 0);

    /* take_max keeps the greater tuple and discards the lesser. */
    robo_css_specificity_take_max(&a, &zero);
    css_specificity_is(a, 1u, 2u, 3u);
    {
        robo_css_specificity_t bigger;

        robo_css_specificity_init(&bigger);
        bigger.id = 1u;
        bigger.class_like = 9u;
        bigger.type_like = 0u;
        robo_css_specificity_take_max(&a, &bigger);
        css_specificity_is(a, 1u, 9u, 0u);
    }

    /* Every door tolerates NULL: "the specificity of no selector" is (0,0,0),
     * not a failure to report. */
    robo_css_specificity_init(NULL);
    ROBO_CHECK_EQ_INT(robo_css_specificity_is_zero(NULL), 0);
    robo_css_specificity_add(NULL, NULL);
    robo_css_specificity_add(&a, NULL);
    css_specificity_is(a, 1u, 9u, 0u);
    robo_css_specificity_take_max(NULL, &a);
    robo_css_specificity_take_max(&a, NULL);
    css_specificity_is(a, 1u, 9u, 0u);
    ROBO_CHECK_EQ_INT(robo_css_specificity_compare(NULL, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_css_specificity_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&zero, NULL), 1);
    /* An absent specificity is the zero one, so a non-zero tuple is not equal to
     * nothing. */
    ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&a, NULL), 0);
}

static void test_css_specificity_compare(void)
{
    robo_css_specificity_t a;
    robo_css_specificity_t b;

    /* A decides regardless of B and C. */
    robo_css_specificity_init(&a);
    robo_css_specificity_init(&b);
    a.id = 1u;
    b.class_like = 100u;
    b.type_like = 100u;
    ROBO_CHECK_EQ_INT(robo_css_specificity_compare(&a, &b), 1);
    ROBO_CHECK_EQ_INT(robo_css_specificity_compare(&b, &a), -1);

    /* B decides when A is equal. */
    robo_css_specificity_init(&a);
    robo_css_specificity_init(&b);
    a.id = 2u;
    a.class_like = 1u;
    b.id = 2u;
    b.class_like = 2u;
    b.type_like = 50u;
    ROBO_CHECK_EQ_INT(robo_css_specificity_compare(&a, &b), -1);
    ROBO_CHECK_EQ_INT(robo_css_specificity_compare(&b, &a), 1);

    /* C decides when A and B are equal — the case a comparison that stopped
     * after two fields would report as a tie, where `div.card` (0,1,1) and
     * `div.card + div` (0,1,2) really differ. */
    robo_css_specificity_init(&a);
    robo_css_specificity_init(&b);
    a.class_like = 1u;
    a.type_like = 1u;
    b.class_like = 1u;
    b.type_like = 2u;
    ROBO_CHECK_EQ_INT(robo_css_specificity_compare(&a, &b), -1);
    ROBO_CHECK_EQ_INT(robo_css_specificity_compare(&b, &a), 1);

    /* And equality is equality in all three. */
    ROBO_CHECK_EQ_INT(robo_css_specificity_compare(&a, &a), 0);
    ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&a, &a), 1);
    ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&a, &b), 0);
}

/* ------------------------------------------------------------------ */
/* The specification's own table                                        */
/* ------------------------------------------------------------------ */

static void test_css_specificity_matrix(void)
{
    static const struct {
        const char *selector;
        uint32_t id;
        uint32_t class_like;
        uint32_t type_like;
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
        robo_css_specificity_t specificity = css_specificity_of(cases[i].selector, 0u);

        css_specificity_is(specificity, cases[i].id, cases[i].class_like,
                           cases[i].type_like);
    }
}

/* ------------------------------------------------------------------ */
/* What each kind of simple selector is worth                           */
/* ------------------------------------------------------------------ */

static void test_css_specificity_simple_kinds(void)
{
    robo_css_selector_list_t list;

    /* One compound holding one of every kind, so each contribution is read from
     * the same walk a real selector goes through. */
    css_select_parse("*#a.b[c]:hover::after", &list);
    ROBO_CHECK_EQ_INT(css_select_first_compound_count(&list), 6u);
    css_specificity_is(
        robo_css_specificity_of_simple(css_select_simple(&list, 0u, 0u, 0u),
                                       robo_css_complex_selector_compound_at(
                                           robo_css_selector_list_at(&list, 0u), 0u),
                                       0u),
        0u, 0u, 0u); /* * */
    css_specificity_is(
        robo_css_specificity_of_simple(css_select_simple(&list, 0u, 0u, 1u),
                                       robo_css_complex_selector_compound_at(
                                           robo_css_selector_list_at(&list, 0u), 0u),
                                       1u),
        1u, 0u, 0u); /* #a */
    css_specificity_is(
        robo_css_specificity_of_simple(css_select_simple(&list, 0u, 0u, 2u),
                                       robo_css_complex_selector_compound_at(
                                           robo_css_selector_list_at(&list, 0u), 0u),
                                       2u),
        0u, 1u, 0u); /* .b */
    css_specificity_is(
        robo_css_specificity_of_simple(css_select_simple(&list, 0u, 0u, 3u),
                                       robo_css_complex_selector_compound_at(
                                           robo_css_selector_list_at(&list, 0u), 0u),
                                       3u),
        0u, 1u, 0u); /* [c] */
    css_specificity_is(
        robo_css_specificity_of_simple(css_select_simple(&list, 0u, 0u, 4u),
                                       robo_css_complex_selector_compound_at(
                                           robo_css_selector_list_at(&list, 0u), 0u),
                                       4u),
        0u, 1u, 0u); /* :hover */
    css_specificity_is(
        robo_css_specificity_of_simple(css_select_simple(&list, 0u, 0u, 5u),
                                       robo_css_complex_selector_compound_at(
                                           robo_css_selector_list_at(&list, 0u), 0u),
                                       5u),
        0u, 0u, 1u); /* ::after */
    /* And the compound is the sum of those: (1,3,1). */
    css_specificity_is(robo_css_specificity_of_list_item(&list, 0u), 1u, 3u, 1u);
    robo_css_selector_list_dispose(&list);

    /* The universal selector contributes nothing wherever it sits. */
    css_specificity_is(css_specificity_of("*", 0u), 0u, 0u, 0u);
    css_specificity_is(css_specificity_of("*.card", 0u), 0u, 1u, 0u);
    css_specificity_is(css_specificity_of("*#main", 0u), 1u, 0u, 0u);
    css_specificity_is(css_specificity_of("div *", 0u), 0u, 0u, 1u);

    /* Each syntactic id counts, so a selector with several ids is worth several
     * — nothing is clamped to one. */
    css_specificity_is(css_specificity_of("#a#b#c#d", 0u), 4u, 0u, 0u);
}

static void test_css_specificity_attribute_operators(void)
{
    /* Every comparator is one B: how an attribute is tested is not another
     * selector, so `[a]` and `[a^="b"]` are worth the same. */
    static const char *const selectors[] = {
        "[a]", "[a=b]", "[a~=b]", "[a|=b]", "[a^=b]", "[a$=b]", "[a*=b]",
        "[type=\"button\"]", "[href^=\"/api\"]", "[data-name*=\"test\"]"
    };
    size_t i;

    for (i = 0u; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        css_specificity_is(css_specificity_of(selectors[i], 0u), 0u, 1u, 0u);
    }
}

static void test_css_specificity_pseudo_classes(void)
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
        css_specificity_is(css_specificity_of(selectors[i], 0u), 0u, 1u, 0u);
    }
}

static void test_css_specificity_pseudo_elements(void)
{
    /* A pseudo-element is C, never B. The legacy one-colon spellings are the
     * same pseudo-elements and must be worth the same. */
    static const char *const selectors[] = {
        "::before", "::after", "::first-line", "::first-letter",
        ":before", ":after", ":first-line", ":first-letter"
    };
    size_t i;

    for (i = 0u; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        css_specificity_is(css_specificity_of(selectors[i], 0u), 0u, 0u, 1u);
    }
    /* One of each in a compound: (0,1,2) — the pseudo-class is B and the
     * pseudo-element is C, and neither is mistaken for the other. */
    css_specificity_is(css_specificity_of("button:hover::before", 0u), 0u, 1u, 2u);
}

/* ------------------------------------------------------------------ */
/* :not(...)                                                            */
/* ------------------------------------------------------------------ */

static void test_css_specificity_not(void)
{
    static const struct {
        const char *selector;
        uint32_t id;
        uint32_t class_like;
        uint32_t type_like;
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
        robo_css_specificity_t specificity = css_specificity_of(cases[i].selector, 0u);

        css_specificity_is(specificity, cases[i].id, cases[i].class_like,
                           cases[i].type_like);
    }
}

static void test_css_specificity_not_without_an_argument(void)
{
    /* A hand-built `:not` with no parsed argument addresses nothing, so it is
     * worth nothing rather than being read as a pseudo-class worth one. The
     * parser cannot produce this record; a caller can. */
    robo_css_compound_selector_t compound;
    robo_css_simple_selector_t simple;

    css_fixture();
    robo_css_compound_selector_init(&compound);
    robo_css_simple_selector_init(&simple);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(
                          &simple, ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS,
                          robo_css_slice_from_text("not"), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, &simple, NULL),
                      ROBO_OK);

    css_specificity_is(robo_css_specificity_of_compound(&compound), 0u, 0u, 0u);
    css_specificity_is(robo_css_specificity_of_simple(&simple, &compound, 0u), 0u, 0u,
                       0u);
    /* And with no compound to resolve the argument through, still nothing. */
    css_specificity_is(robo_css_specificity_of_simple(&simple, NULL, 0u), 0u, 0u, 0u);
    /* A different pseudo-class in the same position is one B, which is what
     * makes the line above a statement about `:not` rather than about hand-built
     * pseudo-classes. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&simple,
                                                    ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS,
                                                    robo_css_slice_from_text("hover"),
                                                    NULL),
                      ROBO_OK);
    css_specificity_is(robo_css_specificity_of_simple(&simple, NULL, 0u), 0u, 1u, 0u);

    robo_css_compound_selector_dispose(&compound);
}

/**
 * Build @p depth levels of `:not(...)` around @p inner and leave the result in
 * @p out; @p inner is consumed and left empty.
 *
 * The parser rejects `:not()` nested deeper than ROBO_CSS_MAX_SELECTOR_NESTING,
 * so a chain past that can only be built here — and it is exactly the input the
 * specificity depth bound exists for.
 */
static void css_build_not_chain(robo_css_selector_list_t *out,
                                size_t depth,
                                robo_css_selector_list_t *inner)
{
    size_t level;

    for (level = 0u; level < depth; level++) {
        robo_css_compound_selector_t compound;
        robo_css_complex_selector_t complex;
        robo_css_selector_list_t next;
        robo_css_simple_selector_t simple;

        robo_css_compound_selector_init(&compound);
        robo_css_complex_selector_init(&complex);
        robo_css_selector_list_init(&next);
        robo_css_simple_selector_init(&simple);

        ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(
                              &simple, ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS,
                              robo_css_slice_from_text("not"), NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, &simple, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_compound_selector_attach_nested(&compound, 0u, inner,
                                                                   NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_compound(&complex, &compound,
                                                                    NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_selector_list_append(&next, &complex, NULL), ROBO_OK);
        *inner = next;
    }
    *out = *inner;
    robo_css_selector_list_init(inner);
}

/** The specificity of `:not(:not(...#deep...))`, @p depth levels deep. */
static robo_css_specificity_t css_specificity_of_not_chain(size_t depth)
{
    robo_css_selector_list_t list;
    robo_css_selector_list_t seed;
    robo_css_specificity_t specificity;

    css_select_parse("#deep", &seed);
    css_build_not_chain(&list, depth, &seed);
    specificity = robo_css_specificity_max_of_list(&list);
    robo_css_selector_list_dispose(&list);
    return specificity;
}

static void test_css_specificity_not_chain_depth(void)
{
    /* Nesting is transparent, so the innermost `#deep` is what the whole chain
     * is worth — up to the parser's own cap. */
    css_specificity_is(css_specificity_of_not_chain(0u), 1u, 0u, 0u);
    css_specificity_is(css_specificity_of_not_chain(1u), 1u, 0u, 0u);
    css_specificity_is(css_specificity_of_not_chain(4u), 1u, 0u, 0u);
    css_specificity_is(css_specificity_of_not_chain(ROBO_CSS_SPECIFICITY_NESTING_MAX),
                       1u, 0u, 0u);
    /* Past the bound the walk stops descending and counts nothing rather than
     * recursing to exhaustion. A chain this deep cannot come from the parser. */
    css_specificity_is(
        css_specificity_of_not_chain(ROBO_CSS_SPECIFICITY_NESTING_MAX + 8u), 0u, 0u, 0u);
    /* And the answer is stable, so the bound is a rule and not a race. */
    {
        robo_css_specificity_t first =
            css_specificity_of_not_chain((size_t)ROBO_CSS_SPECIFICITY_NESTING_MAX * 4u);
        robo_css_specificity_t second =
            css_specificity_of_not_chain((size_t)ROBO_CSS_SPECIFICITY_NESTING_MAX * 4u);

        ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&first, &second), 1);
        css_specificity_is(first, 0u, 0u, 0u);
    }
}

/* ------------------------------------------------------------------ */
/* Compound, complex and list                                           */
/* ------------------------------------------------------------------ */

static void test_css_specificity_compound_and_complex(void)
{
    robo_css_selector_list_t list;
    const robo_css_complex_selector_t *complex;

    css_select_parse("div#main.card[type=text]:root > span.title::before", &list);
    complex = robo_css_selector_list_at(&list, 0u);

    /* Compound 0: div(1 C) #main(1 A) .card(1 B) [type=text](1 B) :root(1 B). */
    css_specificity_is(
        robo_css_specificity_of_compound(
            robo_css_complex_selector_compound_at(complex, 0u)),
        1u, 3u, 1u);
    /* Compound 1: span(1 C) .title(1 B) ::before(1 C). */
    css_specificity_is(
        robo_css_specificity_of_compound(
            robo_css_complex_selector_compound_at(complex, 1u)),
        0u, 1u, 2u);
    /* The complex selector is their sum. */
    css_specificity_is(robo_css_specificity_of_complex(complex), 1u, 4u, 3u);
    css_specificity_is(robo_css_specificity_of_list_item(&list, 0u), 1u, 4u, 3u);

    robo_css_selector_list_dispose(&list);
}

static void test_css_specificity_combinators_are_zero(void)
{
    robo_css_specificity_t descendant = css_specificity_of("div p", 0u);
    robo_css_specificity_t child = css_specificity_of("div > p", 0u);
    robo_css_specificity_t adjacent = css_specificity_of("div + p", 0u);
    robo_css_specificity_t sibling = css_specificity_of("div ~ p", 0u);

    css_specificity_is(descendant, 0u, 0u, 2u);
    /* All four are the same specificity, which is the whole statement: a
     * combinator says how to find the next compound, not what it is. */
    ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&descendant, &child), 1);
    ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&descendant, &adjacent), 1);
    ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&descendant, &sibling), 1);

    /* Whitespace around a combinator changes nothing either. */
    css_specificity_is(css_specificity_of("div  >  p", 0u), 0u, 0u, 2u);
    /* A deeper chain accumulates every compound: `#app` (A), `.panel` and
     * `.title` (B), `a` (C). */
    css_specificity_is(css_specificity_of("#app > .panel .title a", 0u), 1u, 2u, 1u);
}

static void test_css_specificity_list(void)
{
    robo_css_selector_list_t list;

    css_select_parse("div, .card, #main", &list);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 3u);
    /* A specificity belongs to a selector, never to the list: the three members
     * keep three values. */
    css_specificity_is(robo_css_specificity_of_list_item(&list, 0u), 0u, 0u, 1u);
    css_specificity_is(robo_css_specificity_of_list_item(&list, 1u), 0u, 1u, 0u);
    css_specificity_is(robo_css_specificity_of_list_item(&list, 2u), 1u, 0u, 0u);
    /* An out-of-range index is nothing, not a neighbouring member. */
    css_specificity_is(robo_css_specificity_of_list_item(&list, 3u), 0u, 0u, 0u);
    robo_css_selector_list_dispose(&list);

    css_specificity_is(css_specificity_of("div, .card, #main", 0u), 0u, 0u, 1u);
    css_specificity_is(css_specificity_of("div, .card, #main", 1u), 0u, 1u, 0u);
    css_specificity_is(css_specificity_of("div, .card, #main", 2u), 1u, 0u, 0u);
}

static void test_css_specificity_list_max(void)
{
    robo_css_selector_list_t list;

    /* `max_of_list` is the `:not()` rule and is a different question from "the
     * member that matched" — a caller that confused the two would rank a whole
     * list by a member that never applied. */
    css_select_parse("div, .card, #main", &list);
    css_specificity_is(robo_css_specificity_max_of_list(&list), 1u, 0u, 0u);
    robo_css_selector_list_dispose(&list);

    css_select_parse("p, span.tiny", &list);
    css_specificity_is(robo_css_specificity_max_of_list(&list), 0u, 1u, 1u);
    robo_css_selector_list_dispose(&list);

    css_select_parse("a", &list);
    css_specificity_is(robo_css_specificity_max_of_list(&list), 0u, 0u, 1u);
    robo_css_selector_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Escapes, saturation and scale                                        */
/* ------------------------------------------------------------------ */

static void test_css_specificity_escaping(void)
{
    /* Escaping changes which bytes an identifier resolves to, never which
     * bucket it belongs in. The parser interns the resolved spelling, and this
     * measures what it produced. */
    css_specificity_is(css_specificity_of("#item\\31 23", 0u), 1u, 0u, 0u);
    css_specificity_is(css_specificity_of(".foo\\:bar", 0u), 0u, 1u, 0u);
    css_specificity_is(css_specificity_of("[data-name=\"a\\ b\"]", 0u), 0u, 1u, 0u);
    css_specificity_is(css_specificity_of("\\64 iv", 0u), 0u, 0u, 1u);
    css_specificity_is(css_specificity_of("div.foo\\:bar#item\\31 23", 0u), 1u, 1u, 1u);
}

static void test_css_specificity_saturation(void)
{
    robo_css_specificity_t a;
    robo_css_specificity_t b;

    /* A component saturates rather than wrapping. Wrapping would turn "more ids
     * than the maximum" into *fewer*, which is the one failure that inverts the
     * comparison instead of merely losing precision. */
    robo_css_specificity_init(&a);
    a.id = ROBO_CSS_SPECIFICITY_COMPONENT_MAX - 1u;
    robo_css_specificity_init(&b);
    b.id = 8u;
    robo_css_specificity_add(&a, &b);
    ROBO_CHECK_EQ_INT(a.id, ROBO_CSS_SPECIFICITY_COMPONENT_MAX);
    ROBO_CHECK_EQ_INT(a.class_like, 0u);

    robo_css_specificity_add(&a, &a);
    ROBO_CHECK_EQ_INT(a.id, ROBO_CSS_SPECIFICITY_COMPONENT_MAX);

    /* And a saturated value is still ordered above a merely large one. */
    robo_css_specificity_init(&b);
    b.id = ROBO_CSS_SPECIFICITY_COMPONENT_MAX - 1u;
    ROBO_CHECK_EQ_INT(robo_css_specificity_compare(&a, &b), 1);

    /* take_max saturates through compare, so a saturated candidate wins. */
    robo_css_specificity_take_max(&b, &a);
    ROBO_CHECK_EQ_INT(b.id, ROBO_CSS_SPECIFICITY_COMPONENT_MAX);
}

static void test_css_specificity_stress(void)
{
    /* A selector at the width a generated stylesheet can reach: thousands of
     * class selectors in one compound. The count must be exact — a `uint32_t`
     * accumulator does not overflow at this size, and this is the test that says
     * so — and repeated computation must agree with itself. */
    enum { COUNT = 3000 };
    char text[COUNT * 2 + 1];
    size_t at = 0u;
    size_t i;
    robo_css_specificity_t first;
    robo_css_specificity_t second;

    for (i = 0u; i < (size_t)COUNT; i++) {
        text[at++] = '.';
        text[at++] = 'a';
    }
    text[at] = '\0';

    first = css_specificity_of(text, 0u);
    css_specificity_is(first, 0u, (uint32_t)COUNT, 0u);

    second = css_specificity_of(text, 0u);
    ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&first, &second), 1);
}

/* ------------------------------------------------------------------ */
/* The doors agree, and malformed input never reaches a computation     */
/* ------------------------------------------------------------------ */

static void test_css_specificity_levels_agree(void)
{
    robo_css_selector_list_t list;
    const robo_css_complex_selector_t *complex;
    robo_css_specificity_t total;
    robo_css_specificity_t summed;

    /* The four doors must be one walk seen from four places: the sum of the
     * simples is the compound, the sum of the compounds is the complex, and the
     * complex is the list member. A disagreement would mean one door had its own
     * idea of what counts. */
    css_select_parse("div#main.card[type=text]:root > span::before", &list);
    complex = robo_css_selector_list_at(&list, 0u);
    total = robo_css_specificity_of_complex(complex);
    robo_css_specificity_init(&summed);

    {
        size_t compound_index;

        for (compound_index = 0u;
             compound_index < robo_css_complex_selector_compound_count(complex);
             compound_index++) {
            const robo_css_compound_selector_t *compound =
                robo_css_complex_selector_compound_at(complex, compound_index);
            robo_css_specificity_t by_compound =
                robo_css_specificity_of_compound(compound);
            robo_css_specificity_t simple_sum;
            size_t simple_index;

            robo_css_specificity_init(&simple_sum);
            for (simple_index = 0u;
                 simple_index < robo_css_compound_selector_count(compound);
                 simple_index++) {
                robo_css_specificity_t one = robo_css_specificity_of_simple(
                    robo_css_compound_selector_at(compound, simple_index), compound,
                    simple_index);

                robo_css_specificity_add(&simple_sum, &one);
            }
            ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&simple_sum, &by_compound), 1);
            robo_css_specificity_add(&summed, &by_compound);
        }
    }
    ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&summed, &total), 1);
    {
        robo_css_specificity_t by_list = robo_css_specificity_of_list_item(&list, 0u);

        ROBO_CHECK_EQ_INT(robo_css_specificity_equal(&total, &by_list), 1);
    }
    /* And the value is the one a hand computation gives. */
    css_specificity_is(total, 1u, 3u, 3u);

    robo_css_selector_list_dispose(&list);
}

static void test_css_specificity_malformed_never_reaches(void)
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
        robo_css_selector_list_t list = ROBO_CSS_CLEARED;
        robo_css_error_t error;
        robo_css_specificity_t specificity;

        css_fixture();
        robo_css_error_init(&error);
        ROBO_CHECK_EQ_INT(robo_css_selector_list_parse_text(&list, rejected[i], &error,
                                                            NULL),
                          ROBO_PARSE_ERROR);
        ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 1);
        ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 0u);

        specificity = robo_css_specificity_max_of_list(&list);
        ROBO_CHECK_EQ_INT(robo_css_specificity_is_zero(&specificity), 1);
        specificity = robo_css_specificity_of_list_item(&list, 0u);
        ROBO_CHECK_EQ_INT(robo_css_specificity_is_zero(&specificity), 1);
        robo_css_selector_list_dispose(&list);
    }
}

static void test_css_specificity_api_arguments(void)
{
    /* Absent input is the zero specificity, every door, including an index that
     * addresses nothing. */
    css_specificity_is(robo_css_specificity_of_simple(NULL, NULL, 0u), 0u, 0u, 0u);
    css_specificity_is(robo_css_specificity_of_compound(NULL), 0u, 0u, 0u);
    css_specificity_is(robo_css_specificity_of_complex(NULL), 0u, 0u, 0u);
    css_specificity_is(robo_css_specificity_of_list_item(NULL, 0u), 0u, 0u, 0u);
    css_specificity_is(robo_css_specificity_max_of_list(NULL), 0u, 0u, 0u);
    /* A well-formed list with no members is the same answer. */
    {
        robo_css_selector_list_t empty = ROBO_CSS_CLEARED;

        css_fixture();
        robo_css_selector_list_init(&empty);
        css_specificity_is(robo_css_specificity_max_of_list(&empty), 0u, 0u, 0u);
        css_specificity_is(robo_css_specificity_of_list_item(&empty, 0u), 0u, 0u, 0u);
        robo_css_selector_list_dispose(&empty);
    }
}

void robo_test_register_css_specificity(void);

void robo_test_register_css_specificity(void)
{
    robo_test_register("css.specificity.value", test_css_specificity_value);
    robo_test_register("css.specificity.compare", test_css_specificity_compare);
    robo_test_register("css.specificity.matrix", test_css_specificity_matrix);
    robo_test_register("css.specificity.simple_kinds", test_css_specificity_simple_kinds);
    robo_test_register("css.specificity.attribute_operators",
                       test_css_specificity_attribute_operators);
    robo_test_register("css.specificity.pseudo_classes",
                       test_css_specificity_pseudo_classes);
    robo_test_register("css.specificity.pseudo_elements",
                       test_css_specificity_pseudo_elements);
    robo_test_register("css.specificity.not", test_css_specificity_not);
    robo_test_register("css.specificity.not_without_an_argument",
                       test_css_specificity_not_without_an_argument);
    robo_test_register("css.specificity.not_chain_depth",
                       test_css_specificity_not_chain_depth);
    robo_test_register("css.specificity.compound_and_complex",
                       test_css_specificity_compound_and_complex);
    robo_test_register("css.specificity.combinators_are_zero",
                       test_css_specificity_combinators_are_zero);
    robo_test_register("css.specificity.list", test_css_specificity_list);
    robo_test_register("css.specificity.list_max", test_css_specificity_list_max);
    robo_test_register("css.specificity.escaping", test_css_specificity_escaping);
    robo_test_register("css.specificity.saturation", test_css_specificity_saturation);
    robo_test_register("css.specificity.stress", test_css_specificity_stress);
    robo_test_register("css.specificity.levels_agree", test_css_specificity_levels_agree);
    robo_test_register("css.specificity.malformed_never_reaches",
                       test_css_specificity_malformed_never_reaches);
    robo_test_register("css.specificity.api_arguments",
                       test_css_specificity_api_arguments);
}
