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
static const robo_css_compound_selector_t *first_compound(
    const robo_css_selector_list_t *list)
{
    return robo_css_complex_selector_compound_at(robo_css_selector_list_at(list, 0u), 0u);
}

/**
 * Read the reported `condition` before the owning list is released, because that
 * one field borrows from the list's name arena while every other field is a
 * value.
 */
static void condition_of(const robo_css_selector_match_t *result,
                         char *out,
                         size_t capacity)
{
    css_select_text(result->condition, out, capacity);
}

/* ------------------------------------------------------------------ */
/* The simple door                                                    */
/* ------------------------------------------------------------------ */

/**
 * The simple door answers for one simple selector at a time, so a compound's
 * parts can be asked about independently — which is what makes the compound
 * rule ("all of them hold") checkable rather than assumed.
 */
static void test_css_levels_simple_kinds(void)
{
    css_host_t host;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    const robo_css_compound_selector_t *compound;
    robo_css_element_ref_t card;
    robo_css_element_ref_t plain;
    robo_css_selector_match_t out;
    size_t i;

    css_host_init(&host);
    card = css_host_add(&host, "div", "app", "card primary");
    plain = css_host_add(&host, "span", NULL, NULL);
    css_host_seal(&host);

    css_select_parse("div.card#app", &list);
    compound = first_compound(&list);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(compound), 3u);

    /* Each simple selector is true of the card ... */
    for (i = 0u; i < 3u; i++) {
        robo_css_selector_match_init(&out);
        ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                              robo_css_compound_selector_at(compound, i), compound, i,
                              &host.host, card, &out, NULL),
                          1);
        ROBO_CHECK_EQ_INT(out.matched, 1);
        ROBO_CHECK_EQ_INT(out.unsupported, 0);
    }
    /* ... and none of them is true of the span. */
    for (i = 0u; i < 3u; i++) {
        robo_css_selector_match_init(&out);
        ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                              robo_css_compound_selector_at(compound, i), compound, i,
                              &host.host, plain, &out, NULL),
                          0);
        ROBO_CHECK_EQ_INT(out.matched, 0);
        /* A decided "no" is not an unevaluatable one. */
        ROBO_CHECK_EQ_INT(out.unsupported, 0);
    }

    robo_css_selector_list_dispose(&list);
}

static void test_css_levels_simple_universal(void)
{
    css_host_t host;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    const robo_css_compound_selector_t *compound;
    robo_css_selector_match_t out;

    css_host_init(&host);
    css_host_add(&host, "div", NULL, NULL);
    css_host_seal(&host);

    css_select_parse("*", &list);
    compound = first_compound(&list);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_at(compound, 0u)->kind,
                      (int)ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                          robo_css_compound_selector_at(compound, 0u), compound, 0u,
                          &host.host, 1u, &out, NULL),
                      1);

    robo_css_selector_list_dispose(&list);
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
static void test_css_levels_simple_pseudo_arguments(void)
{
    css_host_t host;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    const robo_css_compound_selector_t *compound;
    robo_css_selector_match_t out;
    robo_css_element_ref_t container;
    robo_css_element_ref_t p;
    robo_css_element_ref_t span;
    char condition[32];

    /* Two real siblings: `:nth-child()` counts a parent's children, and the
     * fixture has to have a parent for the question to have an answer. */
    css_host_init(&host);
    container = css_host_add(&host, "div", NULL, NULL);
    p = css_host_add(&host, "p", NULL, NULL);
    span = css_host_add(&host, "span", NULL, NULL);
    css_host_set_parent(&host, p, container);
    css_host_set_parent(&host, span, container);
    css_host_seal(&host);

    /* `:nth-child(2)` needs no compound: `true` for the second element, and
     * false for the first — a decided answer either way. */
    css_select_parse(":nth-child(2)", &list);
    compound = first_compound(&list);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                          robo_css_compound_selector_at(compound, 0u), NULL, 0u,
                          &host.host, span, &out, NULL),
                      1);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                          robo_css_compound_selector_at(compound, 0u), compound, 0u,
                          &host.host, p, &out, NULL),
                      0);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
    robo_css_selector_list_dispose(&list);

    /* A `:nth-child()` whose argument was never parsed is a different matter: the
     * condition exists, the record just does not say which child. */
    {
        robo_css_simple_selector_t hand_made;
        robo_css_simple_selector_t *made = &hand_made;

        robo_css_simple_selector_init(made);
        ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(
                              made, ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS,
                              robo_css_slice_from_text("nth-child"), NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_simple_selector_has_nth(made), 0);
        ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(made, NULL, 0u, &host.host,
                                                          span, &out, NULL),
                          0);
        ROBO_CHECK_EQ_INT(out.unsupported, 1);
        condition_of(&out, condition, sizeof(condition));
        ROBO_CHECK_STR_EQ(condition, "nth-child");
        robo_css_simple_selector_dispose(made);
    }

    /* `:not(p)`: decided when the compound is handed over, unevaluatable when it
     * is not. The two must not collapse into one answer. */
    css_select_parse(":not(p)", &list);
    compound = first_compound(&list);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                          robo_css_compound_selector_at(compound, 0u), compound, 0u,
                          &host.host, span, &out, NULL),
                      1);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                          robo_css_compound_selector_at(compound, 0u), compound, 0u,
                          &host.host, p, &out, NULL),
                      0);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
    /* Without the compound the argument cannot be found, and the honest answer is
     * "not decided" — for the p as much as for the span, because the negation of
     * an unknown is unknown. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                          robo_css_compound_selector_at(compound, 0u), NULL, 0u,
                          &host.host, span, &out, NULL),
                      0);
    ROBO_CHECK_EQ_INT(out.unsupported, 1);
    condition_of(&out, condition, sizeof(condition));
    ROBO_CHECK_STR_EQ(condition, "not");
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                          robo_css_compound_selector_at(compound, 0u), NULL, 0u,
                          &host.host, p, &out, NULL),
                      0);
    ROBO_CHECK_EQ_INT(out.unsupported, 1);
    robo_css_selector_list_dispose(&list);
}

static void test_css_levels_simple_arguments(void)
{
    css_host_t host;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    const robo_css_compound_selector_t *compound;
    robo_css_selector_match_t out;
    robo_css_element_host_t unusable;

    css_host_init(&host);
    css_host_add(&host, "div", NULL, NULL);
    css_host_seal(&host);
    css_select_parse("div", &list);
    compound = first_compound(&list);

    /* No simple selector is a caller bug, not a selector that fails to match. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(NULL, compound, 0u, &host.host,
                                                      1u, &out, NULL),
                      -1);
    /* No host, or a host that can describe nothing, is the same kind of bug — and
     * it is distinguished from the `0` that means "did not match". */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                          robo_css_compound_selector_at(compound, 0u), compound, 0u,
                          NULL, 1u, &out, NULL),
                      -1);
    robo_css_element_host_init(&unusable);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                          robo_css_compound_selector_at(compound, 0u), compound, 0u,
                          &unusable, 1u, &out, NULL),
                      -1);
    /* `no element` is not an error: no element satisfies nothing. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                          robo_css_compound_selector_at(compound, 0u), compound, 0u,
                          &host.host, ROBO_CSS_ELEMENT_REF_NONE, &out, NULL),
                      0);
    ROBO_CHECK_EQ_INT(out.matched, 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
    /* An element the host cannot describe is a candidate that matches nothing. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_matches(
                          robo_css_compound_selector_at(compound, 0u), compound, 0u,
                          &host.host, 99u, &out, NULL),
                      0);

    robo_css_selector_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* The compound door                                                  */
/* ------------------------------------------------------------------ */

static void test_css_levels_compound_requires_every_simple(void)
{
    css_host_t host;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    const robo_css_compound_selector_t *compound;
    robo_css_selector_match_t out;
    robo_css_element_ref_t all;
    robo_css_element_ref_t type_only;

    css_host_init(&host);
    all = css_host_add(&host, "div", "app", "card");
    type_only = css_host_add(&host, "div", NULL, NULL);
    css_host_seal(&host);

    css_select_parse("div#app.card", &list);
    compound = first_compound(&list);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_matches(compound, &host.host, all,
                                                        &out, NULL),
                      1);
    ROBO_CHECK_EQ_INT(out.matched, 1);
    /* Only the type selector holds of the second element, and a compound is the
     * conjunction of its simple selectors, so it does not match. */
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_matches(compound, &host.host, type_only,
                                                        &out, NULL),
                      0);
    ROBO_CHECK_EQ_INT(out.matched, 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);

    robo_css_selector_list_dispose(&list);
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
static void test_css_levels_compound_short_circuits_on_the_first_failure(void)
{
    css_host_t host;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    const robo_css_compound_selector_t *compound;
    robo_css_selector_match_t out;
    robo_css_element_ref_t div;
    char condition[32];

    css_host_init(&host);
    div = css_host_add(&host, "div", NULL, NULL);
    css_host_seal(&host);

    /* The type test fails first, so the compound is decided false. */
    css_select_parse("p:hover", &list);
    compound = first_compound(&list);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_matches(compound, &host.host, div,
                                                        &out, NULL),
                      0);
    ROBO_CHECK_EQ_INT(out.matched, 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
    robo_css_selector_list_dispose(&list);

    /* The type test passes, so the unevaluatable pseudo-class is reached. */
    css_select_parse("div:hover", &list);
    compound = first_compound(&list);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_matches(compound, &host.host, div,
                                                        &out, NULL),
                      0);
    ROBO_CHECK_EQ_INT(out.matched, 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 1);
    condition_of(&out, condition, sizeof(condition));
    ROBO_CHECK_STR_EQ(condition, "hover");
    robo_css_selector_list_dispose(&list);

    /* And the same short-circuit inside a list: the decided false comes first, so
     * nothing about `:hover` is reported at all. */
    ROBO_CHECK_EQ_INT(css_match_condition(&host, "p:hover, span", div, &out, condition,
                                          sizeof(condition)),
                      0);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
}

static void test_css_levels_compound_empty_and_null(void)
{
    css_host_t host;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    robo_css_compound_selector_t empty;
    robo_css_selector_match_t out;
    robo_css_element_host_t unusable;

    css_host_init(&host);
    css_host_add(&host, "div", NULL, NULL);
    css_host_seal(&host);

    /* A compound with no simple selectors is true of nothing, not of everything:
     * the empty conjunction is the one place where the mathematical reading is
     * the wrong one for a selector. */
    robo_css_compound_selector_init(&empty);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_is_set(&empty), 0);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_matches(&empty, &host.host, 1u, &out,
                                                        NULL),
                      0);
    ROBO_CHECK_EQ_INT(out.matched, 0);

    ROBO_CHECK_EQ_INT(robo_css_compound_selector_matches(NULL, &host.host, 1u, &out,
                                                        NULL),
                      -1);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_matches(&empty, NULL, 1u, &out, NULL),
                      -1);
    robo_css_element_host_init(&unusable);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_matches(&empty, &unusable, 1u, &out,
                                                        NULL),
                      -1);

    css_select_parse("div", &list);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_matches(first_compound(&list),
                                                        &host.host,
                                                        ROBO_CSS_ELEMENT_REF_NONE,
                                                        &out, NULL),
                      0);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
    robo_css_selector_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* The complex door                                                   */
/* ------------------------------------------------------------------ */

/**
 * Append a `tag` compound, followed by @p trailing_combinators `>`s, to @p list.
 *
 * @p trailing_combinators > 0 produces a complex selector that is structurally
 * incoherent by construction — one compound and more combinators than it can
 * have — which no parsed selector can be, and which the complex door must refuse
 * and the list door must skip.
 */
static robo_result_t build_complex(robo_css_selector_list_t *list,
                                   const char *tag,
                                   size_t trailing_combinators,
                                   robo_css_diag_t *diag)
{
    robo_css_complex_selector_t complex;
    robo_css_compound_selector_t compound;
    robo_css_simple_selector_t simple;
    robo_result_t result;
    size_t i;

    robo_css_complex_selector_init(&complex);
    robo_css_compound_selector_init(&compound);
    robo_css_simple_selector_init(&simple);
    result = robo_css_simple_selector_make(&simple, ROBO_CSS_SIMPLE_SELECTOR_TYPE,
                                           robo_css_slice_from_text(tag), diag);
    if (result == ROBO_OK) {
        result = robo_css_compound_selector_append(&compound, &simple, diag);
    }
    if (result == ROBO_OK) {
        result = robo_css_complex_selector_append_compound(&complex, &compound, diag);
    }
    for (i = 0u; result == ROBO_OK && i < trailing_combinators; i++) {
        result = robo_css_complex_selector_append_combinator(
            &complex, ROBO_CSS_COMBINATOR_CHILD, diag);
    }
    if (result == ROBO_OK) {
        result = robo_css_selector_list_append(list, &complex, diag);
    }

    robo_css_compound_selector_dispose(&compound);
    robo_css_complex_selector_dispose(&complex);
    return result;
}

/**
 * A complex selector of one compound is that compound — not "equivalent to", the
 * same question with the same answer, because the walk at index 0 *is* the
 * compound test. A caller choosing between the two doors must not be choosing
 * between two verdicts.
 */
static void test_css_levels_complex_of_one_compound_is_that_compound(void)
{
    css_host_t host;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    const robo_css_complex_selector_t *complex;
    const robo_css_compound_selector_t *compound;
    robo_css_selector_match_t at_complex;
    robo_css_selector_match_t at_compound;
    int complex_result;
    int compound_result;

    css_host_init(&host);
    css_host_add(&host, "div", "app", "card");
    css_host_add(&host, "span", NULL, NULL);
    css_host_seal(&host);

    css_select_parse("div.card", &list);
    complex = robo_css_selector_list_at(&list, 0u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(complex), 1u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_count(complex), 0u);
    compound = robo_css_complex_selector_compound_at(complex, 0u);

    complex_result = robo_css_complex_selector_matches(complex, &host.host, 1u,
                                                      &at_complex, NULL);
    compound_result = robo_css_compound_selector_matches(compound, &host.host, 1u,
                                                        &at_compound, NULL);
    ROBO_CHECK_EQ_INT(complex_result, 1);
    ROBO_CHECK_EQ_INT(compound_result, complex_result);
    ROBO_CHECK_EQ_INT(at_compound.matched, at_complex.matched);
    ROBO_CHECK_EQ_INT(at_compound.unsupported, at_complex.unsupported);
    /* A matching complex selector reports its own extent. */
    ROBO_CHECK_EQ_INT(at_complex.range.begin, complex->range.begin);
    ROBO_CHECK_EQ_INT(at_complex.range.end, complex->range.end);

    complex_result = robo_css_complex_selector_matches(complex, &host.host, 2u,
                                                      &at_complex, NULL);
    compound_result = robo_css_compound_selector_matches(compound, &host.host, 2u,
                                                        &at_compound, NULL);
    ROBO_CHECK_EQ_INT(complex_result, 0);
    ROBO_CHECK_EQ_INT(compound_result, complex_result);
    /* Nothing matched, so no range is claimed. */
    ROBO_CHECK_EQ_INT(at_complex.range.begin, 0u);
    ROBO_CHECK_EQ_INT(at_complex.range.end, 0u);

    robo_css_selector_list_dispose(&list);
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
static void test_css_levels_ill_formed_complex_is_refused_and_skipped(void)
{
    css_host_t host;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    robo_css_selector_match_t out;
    robo_css_element_ref_t div;

    css_host_init(&host);
    div = css_host_add(&host, "div", NULL, NULL);
    css_host_add(&host, "span", NULL, NULL);
    css_host_seal(&host);

    /* A standalone incoherent complex selector: one compound, two combinators. */
    robo_css_selector_list_init(&list);
    ROBO_CHECK_EQ_INT(build_complex(&list, "div", 2, NULL), ROBO_OK);
    {
        const robo_css_complex_selector_t *broken = robo_css_selector_list_at(&list, 0u);

        ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(broken), 1u);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_count(broken), 2u);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_is_well_formed(broken), 0);
        /* Refused even though the single compound *would* have matched: the
         * record is broken, which is not the same as a selector that fails. */
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_matches(broken, &host.host, div,
                                                           &out, NULL),
                          -1);
    }
    robo_css_selector_list_dispose(&list);

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
    (void)css_select_parse_rejected("div +");

    robo_css_selector_list_init(&list);
    ROBO_CHECK_EQ_INT(build_complex(&list, "div", 1, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(build_complex(&list, "span", 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 2u);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_is_well_formed(&list), 0);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_matches(&list, &host.host, 2u, &out, NULL),
                      1);
    ROBO_CHECK_EQ_INT(out.selector_index, 1u);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_matches(&list, &host.host, div, &out, NULL),
                      0);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
    robo_css_selector_list_dispose(&list);
}

static void test_css_levels_complex_arguments(void)
{
    css_host_t host;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    robo_css_selector_match_t out;
    robo_css_element_host_t unusable;

    css_host_init(&host);
    css_host_add(&host, "div", NULL, NULL);
    css_host_seal(&host);
    css_select_parse("div", &list);

    ROBO_CHECK_EQ_INT(robo_css_complex_selector_matches(NULL, &host.host, 1u, &out, NULL),
                      -1);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_matches(
                          robo_css_selector_list_at(&list, 0u), NULL, 1u, &out, NULL),
                      -1);
    robo_css_element_host_init(&unusable);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_matches(
                          robo_css_selector_list_at(&list, 0u), &unusable, 1u, &out,
                          NULL),
                      -1);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_matches(
                          robo_css_selector_list_at(&list, 0u), &host.host,
                          ROBO_CSS_ELEMENT_REF_NONE, &out, NULL),
                      0);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);

    robo_css_selector_list_dispose(&list);
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
static void test_css_levels_child_is_stricter_than_descendant(void)
{
    css_host_t host;
    robo_css_element_ref_t outer;
    robo_css_element_ref_t middle;
    robo_css_element_ref_t inner;

    css_host_init(&host);
    outer = css_host_add(&host, "div", NULL, NULL);
    middle = css_host_add(&host, "section", NULL, NULL);
    inner = css_host_add(&host, "p", NULL, NULL);
    css_host_set_parent(&host, middle, outer);
    css_host_set_parent(&host, inner, middle);
    css_host_seal(&host);

    /* The p's parent is a section, so `div > p` fails where `div p` succeeds —
     * the narrow relation, and the case that separates the two. */
    ROBO_CHECK_EQ_INT(css_match(&host, "div p", inner), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "div > p", inner), 0);
    /* Stated as the general implication over every element in the fixture. */
    {
        robo_css_element_ref_t ref;

        for (ref = 1u; ref <= 3u; ref++) {
            if (css_match(&host, "div > p, div > section", ref)) {
                ROBO_CHECK_EQ_INT(css_match(&host, "div p, div section", ref), 1);
            }
        }
    }

    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    const robo_css_complex_selector_t *complex;

    css_select_parse("div p", &list);
    complex = robo_css_selector_list_at(&list, 0u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(complex, 0u),
                      (int)ROBO_CSS_COMBINATOR_DESCENDANT);
    {
        robo_css_selector_match_t out;

        robo_css_selector_match_init(&out);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_matches(complex, &host.host, inner,
                                                           &out, NULL),
                          1);
    }
    robo_css_selector_list_dispose(&list);
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
static void test_css_levels_unavailable_relation_is_unsupported(void)
{
    css_host_t host;
    robo_css_selector_match_t out;
    robo_css_element_ref_t container;
    robo_css_element_ref_t h1;
    robo_css_element_ref_t p;
    char condition[32];

    css_host_init(&host);
    container = css_host_add(&host, "div", NULL, NULL);
    h1 = css_host_add(&host, "h1", NULL, NULL);
    p = css_host_add(&host, "p", NULL, NULL);
    css_host_set_parent(&host, h1, container);
    css_host_set_parent(&host, p, container);
    css_host_seal(&host);

    /* First: the relations are all there, and the selectors are decided. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, "h1 + p", p, &out), 1);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
    ROBO_CHECK_EQ_INT(css_match_full(&host, "h1 ~ p", p, &out), 1);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
    ROBO_CHECK_EQ_INT(css_match_full(&host, "body p", p, &out), 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);

    /* The relations the selectors have not asked for yet stay answerable, so the
     * only thing each case changes is the one callback under test. */
    css_host_drop_relation(&host, CSS_HOST_RELATION_PREVIOUS);
    condition[0] = '\0';
    ROBO_CHECK_EQ_INT(css_match_condition(&host, "h1 + p", p, &out, condition,
                                          sizeof(condition)),
                      0);
    ROBO_CHECK_EQ_INT(out.unsupported, 1);
    /* The reason is the relation, not a pseudo-class, so no condition is named. */
    ROBO_CHECK_STR_EQ(condition, "");
    ROBO_CHECK_EQ_INT(css_match_full(&host, "h1 ~ p", p, &out), 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 1);
    /* `:first-child` is exactly "there is no previous sibling", so a host that
     * cannot say whether there is one cannot answer this either — however
     * plausible a "just say yes" shortcut looks. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, ":first-child", p, &out), 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 1);
    /* A selector that needs no relation at all is untouched. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, "p", p, &out), 1);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);

    /* Now the parent, on a fresh host. */
    css_host_init(&host);
    h1 = css_host_add(&host, "h1", NULL, NULL);
    p = css_host_add(&host, "p", NULL, NULL);
    css_host_set_parent(&host, p, h1);
    css_host_seal(&host);
    css_host_drop_relation(&host, CSS_HOST_RELATION_PARENT);
    ROBO_CHECK_EQ_INT(css_match_full(&host, "body p", p, &out), 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 1);
    ROBO_CHECK_EQ_INT(css_match_full(&host, "body > p", p, &out), 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 1);
    /* `:root` is "there is no parent element", and a host with no parent callback
     * says NONE for the same reason a document element does. The two must not be
     * conflated, or `:root` matches every element of such a host. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, ":root", h1, &out), 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 1);
    ROBO_CHECK_EQ_INT(css_match_full(&host, "*", h1, &out), 1);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);

    /* And the next-sibling callback, which nothing above has needed yet. */
    css_host_init(&host);
    container = css_host_add(&host, "div", NULL, NULL);
    h1 = css_host_add(&host, "h1", NULL, NULL);
    p = css_host_add(&host, "p", NULL, NULL);
    css_host_set_parent(&host, h1, container);
    css_host_set_parent(&host, p, container);
    css_host_seal(&host);
    css_host_drop_relation(&host, CSS_HOST_RELATION_NEXT);
    ROBO_CHECK_EQ_INT(css_match_full(&host, ":last-child", h1, &out), 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 1);
    ROBO_CHECK_EQ_INT(css_match_full(&host, ":only-child", h1, &out), 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 1);
    /* `:first-child` asks the previous sibling, which is still there. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, ":first-child", h1, &out), 1);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
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
static void test_css_levels_a_cyclic_host_is_bounded(void)
{
    css_host_t host;
    robo_css_element_ref_t p;
    robo_css_element_ref_t span;
    robo_css_selector_match_t out;

    css_host_init(&host);
    p = css_host_add(&host, "p", NULL, NULL);
    span = css_host_add(&host, "span", NULL, NULL);
    /* p's parent is span and span's parent is p: a two-element cycle. Neither
     * carries class `x`, so the ancestor search never succeeds. */
    css_host_set_parent(&host, p, span);
    css_host_set_parent(&host, span, p);
    css_host_seal(&host);

    ROBO_CHECK_EQ_INT(css_match_full(&host, ".x p", p, &out), 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 1);
    /* The `>` forms are the opposite story, and deliberately so: a child chain
     * recurses once per compound rather than walking, so it is bounded by the
     * *selector's* length and terminates on its own even here. Both answers are
     * "no", but only one of them is undecided — and a caller that could not tell
     * them apart would report the same thing for a malformed host and for a
     * stylesheet that simply does not apply. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, ".x > p", p, &out), 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
    ROBO_CHECK_EQ_INT(css_match_full(&host, ".x > .x > p", p, &out), 0);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);

    /* A selector the cycle *can* satisfy is still answered, so the bound is a
     * guard against nonsense rather than a blanket refusal. */
    ROBO_CHECK_EQ_INT(css_match_full(&host, "span p", p, &out), 1);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
    ROBO_CHECK_EQ_INT(css_match_full(&host, "span > p", p, &out), 1);
    ROBO_CHECK_EQ_INT(out.unsupported, 0);
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
static void check_levels_agree(css_host_t *host,
                               const char *text,
                               robo_css_element_ref_t ref)
{
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    const robo_css_complex_selector_t *complex;
    robo_css_selector_match_t at_list;
    robo_css_selector_match_t at_complex;
    int list_result;
    int complex_result;

    css_select_parse(text, &list);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 1u);
    complex = robo_css_selector_list_at(&list, 0u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_is_well_formed(complex), 1);

    robo_css_selector_match_init(&at_list);
    list_result = robo_css_selector_list_matches(&list, &host->host, ref, &at_list, NULL);
    robo_css_selector_match_init(&at_complex);
    complex_result = robo_css_complex_selector_matches(complex, &host->host, ref,
                                                      &at_complex, NULL);

    ROBO_CHECK_EQ_INT(list_result, complex_result);
    ROBO_CHECK_EQ_INT(at_list.matched, at_complex.matched);
    ROBO_CHECK_EQ_INT(at_list.unsupported, at_complex.unsupported);
    if (at_complex.matched) {
        ROBO_CHECK_EQ_INT(at_list.selector_index, 0u);
        ROBO_CHECK_EQ_INT(at_list.range.begin, at_complex.range.begin);
        ROBO_CHECK_EQ_INT(at_list.range.end, at_complex.range.end);
    }

    /* And where the complex selector is a single compound, the compound door
     * agrees with both. */
    if (robo_css_complex_selector_compound_count(complex) == 1u) {
        const robo_css_compound_selector_t *compound =
            robo_css_complex_selector_compound_at(complex, 0u);
        robo_css_selector_match_t at_compound;

        robo_css_selector_match_init(&at_compound);
        ROBO_CHECK_EQ_INT(robo_css_compound_selector_matches(compound, &host->host, ref,
                                                            &at_compound, NULL),
                          complex_result);
        ROBO_CHECK_EQ_INT(at_compound.matched, at_complex.matched);
        ROBO_CHECK_EQ_INT(at_compound.unsupported, at_complex.unsupported);
    }

    robo_css_selector_list_dispose(&list);
}

static void test_css_levels_agree(void)
{
    static const char *const selectors[] = {
        "div",           "*",            ".card",        "#app",
        "div.card",      "div#app",      "div p",        "div > p",
        "p + span",      "p ~ span",     ":root",        ":first-child",
        ":last-child",   ":only-child",  ":nth-child(2)", ":nth-last-child(1)",
        ":not(p)",       "[data-x]",     "[data-x=\"1\"]", "div.card#app",
        "body div p",    "p:hover",      "div :first-child", "div > p + span",
    };
    css_host_t host;
    size_t s;
    robo_css_element_ref_t ref;

    css_host_init(&host);
    css_host_add(&host, "html", NULL, NULL);                 /* 1, the root   */
    css_host_add(&host, "body", NULL, NULL);                 /* 2             */
    css_host_add(&host, "div", "app", "card");               /* 3             */
    css_host_add(&host, "p", NULL, NULL);                    /* 4             */
    css_host_add(&host, "span", NULL, NULL);                 /* 5             */
    css_host_add(&host, "p", "solo", NULL);                  /* 6             */
    css_host_set_parent(&host, 2u, 1u);
    css_host_set_parent(&host, 3u, 2u);
    css_host_set_parent(&host, 4u, 3u);
    css_host_set_parent(&host, 5u, 3u);
    css_host_set_parent(&host, 6u, 3u);
    css_host_attribute(&host, 4u, "data-x", "1");
    css_host_seal(&host);

    for (s = 0u; s < sizeof(selectors) / sizeof(selectors[0]); s++) {
        for (ref = 1u; ref <= 6u; ref++) {
            check_levels_agree(&host, selectors[s], ref);
        }
        /* And the null element, which every level must treat as "nothing". */
        check_levels_agree(&host, selectors[s], ROBO_CSS_ELEMENT_REF_NONE);
    }
}

void robo_test_register_css_select_match_levels(void);

void robo_test_register_css_select_match_levels(void)
{
    robo_test_register("css.select.levels.simple_kinds", test_css_levels_simple_kinds);
    robo_test_register("css.select.levels.simple_universal",
                       test_css_levels_simple_universal);
    robo_test_register("css.select.levels.simple_pseudo_arguments",
                       test_css_levels_simple_pseudo_arguments);
    robo_test_register("css.select.levels.simple_arguments",
                       test_css_levels_simple_arguments);
    robo_test_register("css.select.levels.compound_requires_every_simple",
                       test_css_levels_compound_requires_every_simple);
    robo_test_register("css.select.levels.compound_short_circuits_on_the_first_failure",
                       test_css_levels_compound_short_circuits_on_the_first_failure);
    robo_test_register("css.select.levels.compound_empty_and_null",
                       test_css_levels_compound_empty_and_null);
    robo_test_register("css.select.levels.complex_of_one_compound_is_that_compound",
                       test_css_levels_complex_of_one_compound_is_that_compound);
    robo_test_register("css.select.levels.ill_formed_complex_is_refused_and_skipped",
                       test_css_levels_ill_formed_complex_is_refused_and_skipped);
    robo_test_register("css.select.levels.complex_arguments",
                       test_css_levels_complex_arguments);
    robo_test_register("css.select.levels.child_is_stricter_than_descendant",
                       test_css_levels_child_is_stricter_than_descendant);
    robo_test_register("css.select.levels.unavailable_relation_is_unsupported",
                       test_css_levels_unavailable_relation_is_unsupported);
    robo_test_register("css.select.levels.a_cyclic_host_is_bounded",
                       test_css_levels_a_cyclic_host_is_bounded);
    robo_test_register("css.select.levels.levels_agree", test_css_levels_agree);
}
