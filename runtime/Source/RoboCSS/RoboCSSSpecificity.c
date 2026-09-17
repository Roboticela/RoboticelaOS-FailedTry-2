/*
 * RoboCSS specificity (Phase 9.6) — see Include/RoboCSS/RoboCSSSpecificity.h.
 *
 * Three counts, added and compared as a tuple, computed from the 9.4 selector
 * records. No allocation, no caching, no DOM: every function here is a pure
 * function of the selector it is handed, which is what makes the whole subsystem
 * testable against a hand-built selector as well as a parsed one.
 *
 * The shape of the walk is the shape of the selector: a list holds complex
 * selectors, a complex holds compounds and combinators, a compound holds simple
 * selectors. Specificity sums *down* that tree — a list's member is worth its
 * complex selector's total, a complex selector is worth the sum of its
 * compounds', a compound is worth the sum of its simples' — with one place where
 * it does *not* sum: `:not()`, whose own name is worth nothing and whose value is
 * the maximum over its argument list, which is itself a selector list.
 *
 * The recursion therefore exists only through `:not()`, and it descends through
 * a structure the parser capped at eight levels. The depth parameter is the
 * defensive stop for a structure the parser could not have produced.
 */

#include "RoboCSS/RoboCSSSpecificity.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The value                                                           */
/* ------------------------------------------------------------------ */

void robo_css_specificity_init(robo_css_specificity_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

int robo_css_specificity_is_zero(const robo_css_specificity_t *specificity)
{
    if (!specificity) {
        return 0;
    }
    return specificity->id == 0u && specificity->class_like == 0u &&
           specificity->type_like == 0u;
}

/** One component added saturating, never wrapping. */
static uint32_t specificity_component_add(uint32_t value, uint32_t add)
{
    if (add > ROBO_CSS_SPECIFICITY_COMPONENT_MAX - value) {
        return ROBO_CSS_SPECIFICITY_COMPONENT_MAX;
    }
    return value + add;
}

void robo_css_specificity_add(robo_css_specificity_t *acc,
                              const robo_css_specificity_t *add)
{
    if (!acc || !add) {
        return;
    }
    acc->id = specificity_component_add(acc->id, add->id);
    acc->class_like = specificity_component_add(acc->class_like, add->class_like);
    acc->type_like = specificity_component_add(acc->type_like, add->type_like);
}

void robo_css_specificity_take_max(robo_css_specificity_t *acc,
                                   const robo_css_specificity_t *candidate)
{
    if (!acc || !candidate) {
        return;
    }
    if (robo_css_specificity_compare(candidate, acc) > 0) {
        *acc = *candidate;
    }
}

int robo_css_specificity_compare(const robo_css_specificity_t *a,
                                 const robo_css_specificity_t *b)
{
    /*
     * The tuple order, written once. An absent specificity is the zero one, so
     * the function is total and a NULL does not need a special case at every
     * call site.
     */
    if (!a && !b) {
        return 0;
    }
    if (!a) {
        return robo_css_specificity_is_zero(b) ? 0 : -1;
    }
    if (!b) {
        return robo_css_specificity_is_zero(a) ? 0 : 1;
    }
    if (a->id != b->id) {
        return a->id < b->id ? -1 : 1;
    }
    if (a->class_like != b->class_like) {
        return a->class_like < b->class_like ? -1 : 1;
    }
    if (a->type_like != b->type_like) {
        return a->type_like < b->type_like ? -1 : 1;
    }
    return 0;
}

int robo_css_specificity_equal(const robo_css_specificity_t *a,
                               const robo_css_specificity_t *b)
{
    return robo_css_specificity_compare(a, b) == 0;
}

/* ------------------------------------------------------------------ */
/* Computation                                                         */
/* ------------------------------------------------------------------ */

/* Declared before use: a compound's `:not()` needs a list, and a list needs a
 * complex selector. The cycle through `:not()` is real and is the only one. */
static robo_css_specificity_t specificity_of_complex_depth(
    const robo_css_complex_selector_t *complex,
    size_t depth);
static robo_css_specificity_t specificity_of_compound_depth(
    const robo_css_compound_selector_t *compound,
    size_t depth);

/** The maximum specificity over a selector list, which is `:not()`'s value. */
static robo_css_specificity_t specificity_max_of_list_depth(
    const robo_css_selector_list_t *list,
    size_t depth)
{
    robo_css_specificity_t best;
    size_t count;
    size_t i;

    robo_css_specificity_init(&best);
    if (!list) {
        return best;
    }
    count = robo_css_selector_list_count(list);
    for (i = 0u; i < count; i++) {
        robo_css_specificity_t candidate =
            specificity_of_complex_depth(robo_css_selector_list_at(list, i), depth);

        robo_css_specificity_take_max(&best, &candidate);
    }
    return best;
}

/** One simple selector's contribution, with the compound for `:not()`. */
static robo_css_specificity_t specificity_of_simple_depth(
    const robo_css_simple_selector_t *simple,
    const robo_css_compound_selector_t *compound,
    size_t simple_index,
    size_t depth)
{
    robo_css_specificity_t out;
    const robo_css_selector_list_t *argument;

    robo_css_specificity_init(&out);
    if (!simple) {
        return out;
    }
    switch (simple->kind) {
    case ROBO_CSS_SIMPLE_SELECTOR_TYPE:
        out.type_like = 1u;
        return out;
    case ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL:
        /* `*` is worth nothing at all, and `*.card` is worth exactly `.card`. */
        return out;
    case ROBO_CSS_SIMPLE_SELECTOR_CLASS:
    case ROBO_CSS_SIMPLE_SELECTOR_ATTRIBUTE:
        /* Every attribute comparator is one B — `[a]`, `[a=b]` and `[a^=b]` are
         * worth the same, because the comparison is how the attribute is tested
         * rather than an extra selector. */
        out.class_like = 1u;
        return out;
    case ROBO_CSS_SIMPLE_SELECTOR_ID:
        out.id = 1u;
        return out;
    case ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT:
        /* `::before` is C, never B: counting it as a pseudo-class is the classic
         * specificity mistake, and it is why the two kinds are separate. */
        out.type_like = 1u;
        return out;
    case ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS:
        if (!robo_css_simple_selector_is_pseudo_named(simple, "not")) {
            /* `:root`, `:first-child`, `:nth-child(…)` and an unknown
             * pseudo-class all contribute one B; an An+B argument is part of the
             * pseudo-class, not another selector. */
            out.class_like = 1u;
            return out;
        }
        /*
         * `:not()` is the exception: its name is worth nothing and its value is
         * the most specific selector inside it. The argument is the parsed list
         * 9.4 attached to the compound, so no text is reparsed — and a `:not()`
         * whose argument is not reachable (a hand-built one, a copy whose index
         * was cleared) contributes nothing rather than being read as a
         * pseudo-class worth one.
         */
        argument = compound ? robo_css_compound_selector_argument_of(compound, simple_index)
                            : NULL;
        if (!argument) {
            return out;
        }
        if (depth >= ROBO_CSS_SPECIFICITY_NESTING_MAX) {
            /* Deeper than anything the parser can build. Descending no further
             * costs at most the contribution of a structure that cannot exist,
             * and it is what keeps the stack bounded. */
            return out;
        }
        return specificity_max_of_list_depth(argument, depth + 1u);
    default:
        /* NONE and an unknown kind: no selector, no specificity. */
        return out;
    }
}

static robo_css_specificity_t specificity_of_compound_depth(
    const robo_css_compound_selector_t *compound,
    size_t depth)
{
    robo_css_specificity_t out;
    size_t count;
    size_t i;

    robo_css_specificity_init(&out);
    if (!compound) {
        return out;
    }
    count = robo_css_compound_selector_count(compound);
    for (i = 0u; i < count; i++) {
        const robo_css_simple_selector_t *simple =
            robo_css_compound_selector_at(compound, i);
        robo_css_specificity_t contribution =
            specificity_of_simple_depth(simple, compound, i, depth);

        robo_css_specificity_add(&out, &contribution);
    }
    return out;
}

static robo_css_specificity_t specificity_of_complex_depth(
    const robo_css_complex_selector_t *complex,
    size_t depth)
{
    robo_css_specificity_t out;
    size_t count;
    size_t i;

    robo_css_specificity_init(&out);
    if (!complex) {
        return out;
    }
    count = robo_css_complex_selector_compound_count(complex);
    for (i = 0u; i < count; i++) {
        robo_css_specificity_t contribution =
            specificity_of_compound_depth(robo_css_complex_selector_compound_at(complex, i),
                                          depth);

        robo_css_specificity_add(&out, &contribution);
    }
    /* The combinators are deliberately not read: `div > .card` and `div .card`
     * have the same specificity, because a combinator says how to find the next
     * compound rather than what it is. */
    return out;
}

robo_css_specificity_t robo_css_specificity_of_simple(
    const robo_css_simple_selector_t *simple,
    const robo_css_compound_selector_t *compound,
    size_t simple_index)
{
    return specificity_of_simple_depth(simple, compound, simple_index, 0u);
}

robo_css_specificity_t robo_css_specificity_of_compound(
    const robo_css_compound_selector_t *compound)
{
    return specificity_of_compound_depth(compound, 0u);
}

robo_css_specificity_t robo_css_specificity_of_complex(
    const robo_css_complex_selector_t *complex)
{
    return specificity_of_complex_depth(complex, 0u);
}

robo_css_specificity_t robo_css_specificity_of_list_item(
    const robo_css_selector_list_t *list,
    size_t index)
{
    return specificity_of_complex_depth(robo_css_selector_list_at(list, index), 0u);
}

robo_css_specificity_t robo_css_specificity_max_of_list(
    const robo_css_selector_list_t *list)
{
    return specificity_max_of_list_depth(list, 0u);
}
