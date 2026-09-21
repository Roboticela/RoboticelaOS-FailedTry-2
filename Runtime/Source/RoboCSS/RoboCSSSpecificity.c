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

void CssSpecificityInit(CssSpecificity *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

int CssSpecificityIsZero(const CssSpecificity *specificity)
{
    if (!specificity) {
        return 0;
    }
    return specificity->id == 0u && specificity->classLike == 0u &&
           specificity->typeLike == 0u;
}

/** One component added saturating, never wrapping. */
static uint32_t SpecificityComponentAdd(uint32_t value, uint32_t add)
{
    if (add > RoboCssSpecificityComponentMax - value) {
        return RoboCssSpecificityComponentMax;
    }
    return value + add;
}

void CssSpecificityAdd(CssSpecificity *acc,
                              const CssSpecificity *add)
{
    if (!acc || !add) {
        return;
    }
    acc->id = SpecificityComponentAdd(acc->id, add->id);
    acc->classLike = SpecificityComponentAdd(acc->classLike, add->classLike);
    acc->typeLike = SpecificityComponentAdd(acc->typeLike, add->typeLike);
}

void CssSpecificityTakeMax(CssSpecificity *acc,
                                   const CssSpecificity *candidate)
{
    if (!acc || !candidate) {
        return;
    }
    if (CssSpecificityCompare(candidate, acc) > 0) {
        *acc = *candidate;
    }
}

int CssSpecificityCompare(const CssSpecificity *a,
                                 const CssSpecificity *b)
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
        return CssSpecificityIsZero(b) ? 0 : -1;
    }
    if (!b) {
        return CssSpecificityIsZero(a) ? 0 : 1;
    }
    if (a->id != b->id) {
        return a->id < b->id ? -1 : 1;
    }
    if (a->classLike != b->classLike) {
        return a->classLike < b->classLike ? -1 : 1;
    }
    if (a->typeLike != b->typeLike) {
        return a->typeLike < b->typeLike ? -1 : 1;
    }
    return 0;
}

int CssSpecificityEqual(const CssSpecificity *a,
                               const CssSpecificity *b)
{
    return CssSpecificityCompare(a, b) == 0;
}

/* ------------------------------------------------------------------ */
/* Computation                                                         */
/* ------------------------------------------------------------------ */

/* Declared before use: a compound's `:not()` needs a list, and a list needs a
 * complex selector. The cycle through `:not()` is real and is the only one. */
static CssSpecificity SpecificityOfComplexDepth(
    const CssComplexSelector *complex,
    size_t depth);
static CssSpecificity SpecificityOfCompoundDepth(
    const CssCompoundSelector *compound,
    size_t depth);

/** The maximum specificity over a selector list, which is `:not()`'s value. */
static CssSpecificity SpecificityMaxOfListDepth(
    const CssSelectorList *list,
    size_t depth)
{
    CssSpecificity best;
    size_t count;
    size_t i;

    CssSpecificityInit(&best);
    if (!list) {
        return best;
    }
    count = CssSelectorListCount(list);
    for (i = 0u; i < count; i++) {
        CssSpecificity candidate =
            SpecificityOfComplexDepth(CssSelectorListAt(list, i), depth);

        CssSpecificityTakeMax(&best, &candidate);
    }
    return best;
}

/** One simple selector's contribution, with the compound for `:not()`. */
static CssSpecificity SpecificityOfSimpleDepth(
    const CssSimpleSelector *simple,
    const CssCompoundSelector *compound,
    size_t simpleIndex,
    size_t depth)
{
    CssSpecificity out;
    const CssSelectorList *argument;

    CssSpecificityInit(&out);
    if (!simple) {
        return out;
    }
    switch (simple->kind) {
    case RoboCssSimpleSelectorType:
        out.typeLike = 1u;
        return out;
    case RoboCssSimpleSelectorUniversal:
        /* `*` is worth nothing at all, and `*.card` is worth exactly `.card`. */
        return out;
    case RoboCssSimpleSelectorClass:
    case RoboCssSimpleSelectorAttribute:
        /* Every attribute comparator is one B — `[a]`, `[a=b]` and `[a^=b]` are
         * worth the same, because the comparison is how the attribute is tested
         * rather than an extra selector. */
        out.classLike = 1u;
        return out;
    case RoboCssSimpleSelectorId:
        out.id = 1u;
        return out;
    case RoboCssSimpleSelectorPseudoElement:
        /* `::before` is C, never B: counting it as a pseudo-class is the classic
         * specificity mistake, and it is why the two kinds are separate. */
        out.typeLike = 1u;
        return out;
    case RoboCssSimpleSelectorPseudoClass:
        if (!CssSimpleSelectorIsPseudoNamed(simple, "not")) {
            /* `:root`, `:first-child`, `:nth-child(…)` and an unknown
             * pseudo-class all contribute one B; an An+B argument is part of the
             * pseudo-class, not another selector. */
            out.classLike = 1u;
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
        argument = compound ? CssCompoundSelectorArgumentOf(compound, simpleIndex)
                            : NULL;
        if (!argument) {
            return out;
        }
        if (depth >= RoboCssSpecificityNestingMax) {
            /* Deeper than anything the parser can build. Descending no further
             * costs at most the contribution of a structure that cannot exist,
             * and it is what keeps the stack bounded. */
            return out;
        }
        return SpecificityMaxOfListDepth(argument, depth + 1u);
    default:
        /* NONE and an unknown kind: no selector, no specificity. */
        return out;
    }
}

static CssSpecificity SpecificityOfCompoundDepth(
    const CssCompoundSelector *compound,
    size_t depth)
{
    CssSpecificity out;
    size_t count;
    size_t i;

    CssSpecificityInit(&out);
    if (!compound) {
        return out;
    }
    count = CssCompoundSelectorCount(compound);
    for (i = 0u; i < count; i++) {
        const CssSimpleSelector *simple =
            CssCompoundSelectorAt(compound, i);
        CssSpecificity contribution =
            SpecificityOfSimpleDepth(simple, compound, i, depth);

        CssSpecificityAdd(&out, &contribution);
    }
    return out;
}

static CssSpecificity SpecificityOfComplexDepth(
    const CssComplexSelector *complex,
    size_t depth)
{
    CssSpecificity out;
    size_t count;
    size_t i;

    CssSpecificityInit(&out);
    if (!complex) {
        return out;
    }
    count = CssComplexSelectorCompoundCount(complex);
    for (i = 0u; i < count; i++) {
        CssSpecificity contribution =
            SpecificityOfCompoundDepth(CssComplexSelectorCompoundAt(complex, i),
                                          depth);

        CssSpecificityAdd(&out, &contribution);
    }
    /* The combinators are deliberately not read: `div > .card` and `div .card`
     * have the same specificity, because a combinator says how to find the next
     * compound rather than what it is. */
    return out;
}

CssSpecificity CssSpecificityOfSimple(
    const CssSimpleSelector *simple,
    const CssCompoundSelector *compound,
    size_t simpleIndex)
{
    return SpecificityOfSimpleDepth(simple, compound, simpleIndex, 0u);
}

CssSpecificity CssSpecificityOfCompound(
    const CssCompoundSelector *compound)
{
    return SpecificityOfCompoundDepth(compound, 0u);
}

CssSpecificity CssSpecificityOfComplex(
    const CssComplexSelector *complex)
{
    return SpecificityOfComplexDepth(complex, 0u);
}

CssSpecificity CssSpecificityOfListItem(
    const CssSelectorList *list,
    size_t index)
{
    return SpecificityOfComplexDepth(CssSelectorListAt(list, index), 0u);
}

CssSpecificity CssSpecificityMaxOfList(
    const CssSelectorList *list)
{
    return SpecificityMaxOfListDepth(list, 0u);
}
