/*
 * RoboCSS computed-style boundary (Phase 9.1).
 *
 * A computed style is the answer to "what does this property end up being for
 * this element", after the cascade, specificity, inheritance, units and
 * property-specific resolution have all had their say. Every one of those is a
 * later phase (9.5 to 9.10), and this phase computes nothing.
 *
 * What 9.1 fixes is the container the answer arrives in: an ordered set of
 * (property identity, value) pairs, with the ordering, ownership and lookup
 * rules settled so that the phases filling it do not also have to design it.
 * Values are appended by *moving* them in and are owned by the style; a lookup
 * hands back a borrowed pointer that stays valid until the property is set
 * again or the style is disposed.
 *
 * Two decisions worth stating, because they are the ones a reader will
 * question:
 *
 *   - The container stores component values (CssValue), not typed
 *     computed values (a length in pixels, a packed colour). Typed computed
 *     values are 9.8's and 9.9's design, and a container that guessed at them
 *     now would fix a representation before the phases that know what it must
 *     hold. Component values are what 9.1 *does* have: the tree the tokenizer
 *     and parser produce, preserved verbatim.
 *   - Setting a property that is already present replaces the value in place
 *     and keeps its position. The alternative — moving it to the end — would
 *     make iteration order depend on how many times a property was written,
 *     which is exactly the kind of accidental nondeterminism a deterministic
 *     test suite exists to catch.
 *
 * There is no "unset" or "inherit" or "initial" marker in this phase: those are
 * CSS-wide keywords, they are values like any other in the component-value
 * model, and giving them dedicated state here would be implementing 9.7 by
 * accident.
 *
 * ---- what 9.8 added, and why it is additive ----
 *
 * 9.8 makes an entry richer rather than different. The container still holds the
 * *specified* component value it always did — that is what
 * CssComputedStyleSet moves in, and every 9.1 accessor still answers it —
 * and an entry additionally carries, when the computation put it there:
 *
 *   name       the property's spelling, copied. Needed because a custom property
 *              and an unknown property have no id, and an entry nothing can name
 *              is an entry nothing can look up.
 *   computed   the typed computed value (RoboCSSComputedValue.h), which is the
 *              half of "specified vs computed" this phase exists to add.
 *   flags      how the value was arrived at — declared, inherited, the
 *              property's default, deferred — and whether anything is wrong
 *              with it: unresolved (`var()`), or invalid for the property.
 *
 * The flags are this container's own vocabulary rather than the inheritance
 * model's, and that is deliberate: a computed value can be *both* inherited and
 * a default (a child inheriting the parent's initial `color`), and a single
 * enumerator could not say so. A bitset can, and it lets this header stay a
 * header about computed styles rather than one that has to include the
 * inheritance model to describe its own contents.
 *
 * An entry with no computed value is not an error. It is what
 * CssComputedStyleSet produces, and it means "this property is present
 * with this specified value and nobody has computed it" — which is a real state
 * for a caller assembling a style by hand.
 */

#ifndef ROBO_CSS_COMPUTED_H
#define ROBO_CSS_COMPUTED_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSProperty.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How a computed entry was arrived at, and what is wrong with it.
 *
 * A bitset, not an enumerator, because the facts are not exclusive: a value the
 * parent supplied *and* the property's default (a root's initial colour
 * inherited by its child) is both, and an INVALID value is still, factually,
 * something the element declared. Each bit is one question answered yes.
 */
typedef enum CssComputedFlag {
    RoboCssComputedFlagNone = 0,
    /** The element's own cascade declared it. */
    RoboCssComputedFlagDeclared = 1 << 0,
    /** The parent's computed style supplied it. */
    RoboCssComputedFlagInherited = 1 << 1,
    /** The property's own initial value supplied it. */
    RoboCssComputedFlagDefault = 1 << 2,
    /** The `revert` keyword, carried unresolved for a later phase. */
    RoboCssComputedFlagDeferred = 1 << 3,
    /** A custom property (`--x`): preserved verbatim, never interpreted. */
    RoboCssComputedFlagCustom = 1 << 4,
    /** Needs a later stage: `var()`, `calc()`, `currentColor`. */
    RoboCssComputedFlagUnresolved = 1 << 5,
    /** The value means nothing for this property (RoboCSSComputedValue.h). */
    RoboCssComputedFlagInvalid = 1 << 6
} CssComputedFlag;

/** An opaque, caller-owned set of computed property values. */
typedef struct CssComputedStyle CssComputedStyle;

/**
 * Create an empty computed style.
 * @return RoboOk; RoboInvalidArgument for a NULL output; RoboOutOfMemory
 *         when it cannot be allocated.
 */
Result CssComputedStyleCreate(CssComputedStyle **outStyle,
                                             CssDiag *diag);

/** Dispose @p style and every value it holds. Safe on NULL; idempotent. */
void CssComputedStyleDestroy(CssComputedStyle *style);

/**
 * Set @p property to @p value, moving the value in (@p value is left NONE).
 * An existing entry is replaced in place, keeping its position.
 * RoboCssPropertyIdNone is refused: "no property" is not a property, and
 * silently accepting it would make an unresolved declaration look computed.
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a NONE property;
 *         RoboOutOfMemory when the table cannot grow.
 */
Result CssComputedStyleSet(CssComputedStyle *style,
                                          CssPropertyId property,
                                          CssValue *value,
                                          CssDiag *diag);

/**
 * Borrowed value for @p property, or NULL when it is not present.
 * The pointer stays valid until that property is set again or the style is
 * disposed.
 */
const CssValue *CssComputedStyleGet(
    const CssComputedStyle *style,
    CssPropertyId property);

/** Non-zero when @p property is present. */
int CssComputedStyleHas(const CssComputedStyle *style,
                                CssPropertyId property);

/**
 * Remove @p property, disposing its value.
 * @return RoboOk; RoboNotFound when it was not present.
 */
Result CssComputedStyleRemove(CssComputedStyle *style,
                                             CssPropertyId property,
                                             CssDiag *diag);

/** Number of properties present (0 for NULL). */
size_t CssComputedStyleCount(const CssComputedStyle *style);

/**
 * The property at @p index, in insertion order.
 * @return RoboOk and fills @p outProperty; RoboNotFound when @p index is
 *         out of range; RoboInvalidArgument for a NULL output.
 */
Result CssComputedStylePropertyAt(
    const CssComputedStyle *style,
    size_t index,
    CssPropertyId *outProperty,
    CssDiag *diag);

/**
 * The value at @p index, in insertion order.
 * @return a borrowed pointer, or NULL when @p index is out of range.
 */
const CssValue *CssComputedStyleValueAt(
    const CssComputedStyle *style,
    size_t index);

/** Remove every property. Safe on NULL. */
void CssComputedStyleClear(CssComputedStyle *style);

/* ------------------------------------------------------------------ */
/* The computed half of an entry (9.8)                                 */
/* ------------------------------------------------------------------ */

/**
 * Set @p property to (@p name, @p flags, @p specified, @p computed).
 *
 * The door the computation uses, and the one that fills an entry completely.
 * Both values may be NULL, meaning "nothing on that side": a state the
 * computation produces whenever a property's default is unknown to this build
 * (no specified value and no computed one, and the entry is still there, which
 * is the whole point). An existing entry is replaced in place, keeping its
 * position, exactly as CssComputedStyleSet does.
 *
 * @p name is copied and may be an absent slice for an entry identified only by
 * @p property. RoboCssPropertyIdNone is accepted when @p name is present —
 * that is what a custom property and an unknown property are — and refused when
 * it is not, because an entry with neither an id nor a name is unfindable and
 * would be a hole in the style rather than a value in it.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL style or an unfindable
 *         entry; RoboOutOfMemory when nothing could be stored (the entry is
 *         left untouched in that case).
 */
Result CssComputedStyleSetEntry(
    CssComputedStyle *style,
    CssPropertyId property,
    CssSlice name,
    uint32_t flags,
    CssValue *specified,
    CssComputedValue *computed,
    CssDiag *diag);

/**
 * Index of the entry for @p name, or (size_t)-1 when there is none.
 *
 * The comparison follows CSS's rule — ASCII case-insensitive for an ordinary
 * property name, byte-exact for a custom property — which is the same rule the
 * registry and the inheritance model use, so all three agree about what "the
 * same property" means.
 */
size_t CssComputedStyleFind(const CssComputedStyle *style,
                                    CssSlice name);

/**
 * Borrowed name of the entry at @p index, or an absent slice when it has none
 * or @p index is out of range.
 */
CssSlice CssComputedStyleNameAt(
    const CssComputedStyle *style,
    size_t index);

/** Borrowed flags of the entry at @p index; NONE when out of range. */
uint32_t CssComputedStyleFlagsAt(const CssComputedStyle *style,
                                          size_t index);

/**
 * Borrowed *computed* value of the entry at @p index, or NULL when the entry
 * has none (or @p index is out of range).
 */
const CssComputedValue *CssComputedStyleComputedAt(
    const CssComputedStyle *style,
    size_t index);

/** Borrowed computed value for @p property, or NULL. */
const CssComputedValue *CssComputedStyleComputed(
    const CssComputedStyle *style,
    CssPropertyId property);

/**
 * Borrowed computed value for @p name, or NULL.
 *
 * The lookup to prefer when a property may be a custom property or may be one
 * no table knows, because it is the one that does not need an id to have been
 * resolved.
 */
const CssComputedValue *CssComputedStyleComputedByName(
    const CssComputedStyle *style,
    CssSlice name);

/** Borrowed flags for @p name; NONE when there is no entry. */
uint32_t CssComputedStyleFlags(const CssComputedStyle *style,
                                       CssSlice name);

/**
 * Borrowed specified value for @p name, or NULL.
 *
 * The name-keyed spelling of CssComputedStyleGet, and what makes the two
 * halves of "specified vs computed" reachable the same way.
 */
const CssValue *CssComputedStyleSpecifiedByName(
    const CssComputedStyle *style,
    CssSlice name);

/** Non-zero when the entry for @p name carries @p flag. */
int CssComputedStyleHasFlag(const CssComputedStyle *style,
                                     CssSlice name,
                                     uint32_t flag);

/** Non-zero when any entry is marked INVALID but not UNRESOLVED. */
int CssComputedStyleHasInvalid(const CssComputedStyle *style);

/**
 * Number of entries that are INVALID or UNRESOLVED.
 *
 * The counter that keeps a computed style from being read as more settled than
 * it is: a `calc()` that only layout can reduce and a `color: nonsense` that is
 * wrong both make the style incomplete, and a consumer can see how much.
 */
size_t CssComputedStyleIncompleteCount(
    const CssComputedStyle *style);

/**
 * Non-zero when the resolution behind this style ran out of ancestor chain.
 *
 * A style computed for an element whose chain is longer than the walk goes (or
 * whose DOM reports a cycle among parents) is a *lower bound*: every value it
 * inherited from above the point the walk stopped could have been overridden
 * there. Recorded on the style rather than on the entries, because it is a fact
 * about the resolution and not about any one property.
 *
 * 0 does **not** mean the element declared anything — an element that declares
 * nothing at all has a complete style, because defaults are answers.
 */
int CssComputedStyleIsTruncated(const CssComputedStyle *style);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_COMPUTED_H */
