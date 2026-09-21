/*
 * RoboCSS specificity (Phase 9.6).
 *
 * The cascade sorts competing declarations by three things, and 9.5 owned two of
 * them: origin × importance (the *level*) and document order. This header owns
 * the third — specificity — which is the only one that is a property of the
 * selector rather than of where a declaration was written.
 *
 *   selector        (9.4 representation, unchanged)
 *       ↓
 *   specificity     (here) — three counts, summed over the selector
 *       ↓
 *   cascade resolve (9.5, now consuming this instead of refusing to decide)
 *
 * Three integers, not one
 *
 * CSS defines specificity as three components compared as a tuple:
 *
 *   (A, B, C)
 *     A  the number of id selectors
 *     B  the number of class selectors, attribute selectors and pseudo-classes
 *     C  the number of type selectors and pseudo-elements
 *
 * The comparison is lexicographic: A first, then B, then C. That is why this is
 * three `uint32_t` fields and not a single packed integer. A packed integer has
 * to pick a radix, and any radix below the maximum a component can reach silently
 * carries: `(0, 2, 0)` and `(1, 0, -something)` become the same number, and the
 * "something" is why a wrong answer would look exactly like a right one. The
 * three-field form cannot make that mistake, and `CssSpecificityCompare`
 * is the single place the ordering is written down.
 *
 * Saturation, not wrap
 *
 * A component saturates at RoboCssSpecificityComponentMax rather than
 * wrapping. The bound is not reachable from a parsed stylesheet — it would take
 * 2^32 id selectors in one selector — but a generated or hand-built selector is
 * not bounded by anything, and `uint32_t` arithmetic that wrapped would turn a
 * selector with more ids than the maximum into one with *fewer*, which is the
 * one failure mode that inverts the answer. Saturation keeps "more" meaning
 * "more", so the order can be wrong only in the region where the counts already
 * exceed what a document can hold.
 *
 * Nothing here allocates, walks the DOM, or mutates a selector. A specificity is
 * a pure function of the selector structure, so the same selector always yields
 * the same value and no cache is needed to keep it that way. (A cache was
 * considered — see css/docs/Specificity.md — and deliberately not added: it would
 * give the selector records a mutable field whose invalidation rules the parser
 * and every hand-builder would then have to honour, for a computation that is
 * three additions per simple selector.)
 *
 * The one recursion
 *
 * `:not(...)` is the only construct whose specificity comes from somewhere else:
 * its own name contributes nothing, and its contribution is the specificity of
 * the *most specific selector in its argument list* (Selectors 4). The argument
 * is the owned, parsed selector list 9.4 attached to the compound, so this never
 * reparses text. The recursion is bounded by
 * RoboCssSpecificityNestingMax, which exists only so that a hand-built
 * structure deeper than the parser's own nesting cap costs bounded stack; a
 * parsed selector cannot reach it, because the parser rejects `:not()` nested
 * deeper than RoboCssMaxSelectorNesting (8) long before this runs.
 *
 * What is deliberately absent
 *
 * `:is()`, `:where()`, `:has()`, `@layer`, and the `of S` clause of
 * `:nth-child()`. None of them parses in this build, so none of them can reach
 * specificity, and inventing rules for selectors that cannot exist would be
 * inventing answers. `:not()` is supported because 9.4 parsed its argument; a
 * pseudo-class with no parsed argument contributes what its own name is worth and
 * nothing more.
 */

#ifndef ROBO_CSS_SPECIFICITY_H
#define ROBO_CSS_SPECIFICITY_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSSelector.h"
#include "RoboCSS/RoboCSSTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The value one component saturates at.
 *
 * `UINT32_MAX`, so the bound is the largest number the field can hold rather than
 * a smaller "surely enough" number — a false saturation would be as wrong as a
 * wrap, in the other direction.
 */
#define RoboCssSpecificityComponentMax ((uint32_t)0xFFFFFFFFu)

/**
 * How deep a nested `:not()` argument may recurse before its specificity is not
 * counted.
 *
 * A parsed selector cannot reach this: the selector parser caps `:not()` nesting
 * at RoboCssMaxSelectorNesting (8). The bound is here for a hand-built
 * structure, and the response past it is to *stop descending* — the contribution
 * of everything deeper is zero rather than a guess. That is a wrong specificity
 * for an input the parser cannot produce, which is the right trade for a
 * computation that must never exhaust the stack.
 */
#define RoboCssSpecificityNestingMax 32u

/**
 * A selector's specificity, as CSS's three-component tuple.
 *
 * Field order is comparison order: `id` first, then `classLike`, then
 * `typeLike`. A caller that wants to order two of these should call
 * CssSpecificityCompare rather than compare fields, because the tuple
 * order is the whole definition and a hand-written comparison that started with
 * the wrong field would be plausible-looking and wrong.
 */
typedef struct CssSpecificity {
    /** A: the number of id selectors. */
    uint32_t id;
    /**
     * B: class selectors, attribute selectors and pseudo-classes — including the
     * `:not()` whose contribution is its argument's, not one of its own.
     */
    uint32_t classLike;
    /** C: type selectors and pseudo-elements. */
    uint32_t typeLike;
} CssSpecificity;

/** Set @p out to (0, 0, 0) — the specificity of no selector. Safe on NULL. */
void CssSpecificityInit(CssSpecificity *out);

/** Non-zero when @p specificity is (0, 0, 0). 0 for NULL. */
int CssSpecificityIsZero(const CssSpecificity *specificity);

/**
 * @p acc += @p add, each component separately and saturating at
 * RoboCssSpecificityComponentMax.
 *
 * Saturating rather than wrapping is what keeps "more selectors" meaning "more
 * specific" even past the bound. A NULL argument is ignored.
 */
void CssSpecificityAdd(CssSpecificity *acc,
                              const CssSpecificity *add);

/**
 * @p acc = the component-wise maximum of @p acc and @p candidate, saturating.
 *
 * This is the `:not()` rule's one primitive: a list's specificity is its most
 * specific member's. A NULL argument is ignored.
 */
void CssSpecificityTakeMax(CssSpecificity *acc,
                                   const CssSpecificity *candidate);

/**
 * Order two specificities: -1 when @p a is weaker, 0 when equal, 1 when stronger.
 *
 * Lexicographic over (id, classLike, typeLike). A NULL argument is treated as
 * the zero specificity, so the function is total; two NULLs compare equal.
 *
 * This is the only ordering in the subsystem. A caller must not compare the three
 * fields itself — the tuple order is the definition, and 9.5's cascade consumed
 * this function rather than re-deriving it.
 */
int CssSpecificityCompare(const CssSpecificity *a,
                                 const CssSpecificity *b);

/** Non-zero when @p a and @p b are the same tuple. 0 for NULL unless both NULL. */
int CssSpecificityEqual(const CssSpecificity *a,
                               const CssSpecificity *b);

/*
 * Computation.
 *
 * Four doors, one per level of the 9.1 selector model, each taking the structure
 * that owns the level below it — exactly the shape the 9.4 matcher uses, so a
 * caller that can match a selector can measure it with the same records and no
 * translation.
 *
 * All four are *total*: an absent or incoherent input yields the zero
 * specificity rather than failing, because "the specificity of no selector" is a
 * real value and a crash on a malformed record is not. Nothing here can produce
 * an error a caller would have to handle, which is why none of them returns a
 * result code.
 */

/**
 * The specificity of one simple selector.
 *
 * @p compound and @p simpleIndex locate @p simple inside the compound that owns
 * it, which is how a `:not()` argument is reached; they may be NULL/0 for a
 * simple selector that cannot have one, in which case a `:not()` whose argument
 * is not reachable contributes zero (its own name is worth nothing).
 *
 * Contributions: TYPE → C; UNIVERSAL → nothing; CLASS, ATTRIBUTE → B; ID → A;
 * PSEUDO_ELEMENT → C; PSEUDO_CLASS → B, except `:not()`, which contributes the
 * specificity of the most specific selector in its argument list.
 */
CssSpecificity CssSpecificityOfSimple(
    const CssSimpleSelector *simple,
    const CssCompoundSelector *compound,
    size_t simpleIndex);

/**
 * The specificity of a whole compound selector: the component-wise sum of its
 * simple selectors. `div#main.card` is (1, 1, 1); `div.card` is (0, 1, 1).
 */
CssSpecificity CssSpecificityOfCompound(
    const CssCompoundSelector *compound);

/**
 * The specificity of a whole complex selector: the sum of its compounds.
 * Combinators contribute nothing — `div > .card` and `div .card` are the same
 * specificity — so this does not read the combinator array at all.
 */
CssSpecificity CssSpecificityOfComplex(
    const CssComplexSelector *complex);

/**
 * The specificity of one complex selector of a selector list, addressed by index.
 *
 * A specificity belongs to a *selector*, never to a list: `div, .card, #main` has
 * three of them, and the cascade must use the one whose selector actually matched
 * the element. This is the door the cascade uses, with the index the matcher
 * reported. An out-of-range index yields zero.
 */
CssSpecificity CssSpecificityOfListItem(
    const CssSelectorList *list,
    size_t index);

/**
 * The specificity of the most specific selector in @p list.
 *
 * This is the `:not()` rule (and, later, `:is()`'s), not the cascade's: a rule
 * whose selector list has several members is worth the specificity of the member
 * that *matched*, which is a different question and belongs to whoever did the
 * matching. An empty or absent list yields zero.
 */
CssSpecificity CssSpecificityMaxOfList(
    const CssSelectorList *list);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_SPECIFICITY_H */
