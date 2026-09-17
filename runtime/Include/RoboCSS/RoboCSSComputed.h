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
 *   - The container stores component values (robo_css_value_t), not typed
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
 */

#ifndef ROBO_CSS_COMPUTED_H
#define ROBO_CSS_COMPUTED_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSProperty.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/** An opaque, caller-owned set of computed property values. */
typedef struct robo_css_computed_style robo_css_computed_style_t;

/**
 * Create an empty computed style.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL output; ROBO_OUT_OF_MEMORY
 *         when it cannot be allocated.
 */
robo_result_t robo_css_computed_style_create(robo_css_computed_style_t **out_style,
                                             robo_css_diag_t *diag);

/** Dispose @p style and every value it holds. Safe on NULL; idempotent. */
void robo_css_computed_style_destroy(robo_css_computed_style_t *style);

/**
 * Set @p property to @p value, moving the value in (@p value is left NONE).
 * An existing entry is replaced in place, keeping its position.
 * ROBO_CSS_PROPERTY_ID_NONE is refused: "no property" is not a property, and
 * silently accepting it would make an unresolved declaration look computed.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a NONE property;
 *         ROBO_OUT_OF_MEMORY when the table cannot grow.
 */
robo_result_t robo_css_computed_style_set(robo_css_computed_style_t *style,
                                          robo_css_property_id_t property,
                                          robo_css_value_t *value,
                                          robo_css_diag_t *diag);

/**
 * Borrowed value for @p property, or NULL when it is not present.
 * The pointer stays valid until that property is set again or the style is
 * disposed.
 */
const robo_css_value_t *robo_css_computed_style_get(
    const robo_css_computed_style_t *style,
    robo_css_property_id_t property);

/** Non-zero when @p property is present. */
int robo_css_computed_style_has(const robo_css_computed_style_t *style,
                                robo_css_property_id_t property);

/**
 * Remove @p property, disposing its value.
 * @return ROBO_OK; ROBO_NOT_FOUND when it was not present.
 */
robo_result_t robo_css_computed_style_remove(robo_css_computed_style_t *style,
                                             robo_css_property_id_t property,
                                             robo_css_diag_t *diag);

/** Number of properties present (0 for NULL). */
size_t robo_css_computed_style_count(const robo_css_computed_style_t *style);

/**
 * The property at @p index, in insertion order.
 * @return ROBO_OK and fills @p out_property; ROBO_NOT_FOUND when @p index is
 *         out of range; ROBO_INVALID_ARGUMENT for a NULL output.
 */
robo_result_t robo_css_computed_style_property_at(
    const robo_css_computed_style_t *style,
    size_t index,
    robo_css_property_id_t *out_property,
    robo_css_diag_t *diag);

/**
 * The value at @p index, in insertion order.
 * @return a borrowed pointer, or NULL when @p index is out of range.
 */
const robo_css_value_t *robo_css_computed_style_value_at(
    const robo_css_computed_style_t *style,
    size_t index);

/** Remove every property. Safe on NULL. */
void robo_css_computed_style_clear(robo_css_computed_style_t *style);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_COMPUTED_H */
