/*
 * RoboLayout's CSS seam (Phase 10.1) — see RoboLayoutStyle.h.
 */

#include "RoboLayout/RoboLayoutStyle.h"

#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSDisplay.h"

#include <string.h>

/** The one property name RoboLayout knows. Spelled here and nowhere else. */
#define ROBO_LAYOUT_PROPERTY_DISPLAY "display"

void robo_layout_style_source_init(robo_layout_style_source_t *source)
{
    if (!source) {
        return;
    }
    memset(source, 0, sizeof(*source));
}

int robo_layout_style_source_is_usable(const robo_layout_style_source_t *source)
{
    return source && source->computed_style_for != NULL;
}

const robo_css_computed_style_t *robo_layout_style_source_style(
    const robo_layout_style_source_t *source,
    const robo_html_node_handle_t *element)
{
    if (!robo_layout_style_source_is_usable(source) || !element) {
        return NULL;
    }
    return source->computed_style_for(source->userdata, element);
}

robo_css_display_t robo_layout_style_display(const robo_css_computed_style_t *style)
{
    const robo_css_computed_value_t *value;

    /*
     * No style at all, or no entry for `display`. Both are answered with the
     * property's initial value, and both are the *same* answer on purpose: 9.8's
     * container holds the entries an element has an answer for, and a
     * non-inherited property nothing declared is answered by its initial value
     * rather than by an entry. So "nothing styled this element" and "nothing
     * declared display on it" really are one case.
     */
    if (!style) {
        return ROBO_CSS_DISPLAY_INLINE;
    }
    value = robo_css_computed_style_computed_by_name(
        style, robo_css_slice_from_text(ROBO_LAYOUT_PROPERTY_DISPLAY));
    if (!value) {
        return ROBO_CSS_DISPLAY_INLINE;
    }
    /*
     * The kind is checked before the value is read, and it is not a formality.
     * `robo_css_computed_value_display` answers NONE for any other kind, and NONE
     * is also a real display value -- `display: none`. Reading the accessor
     * without the kind would turn a value that is not a display at all into a
     * removed box, which is the single most damaging reading a box-tree builder
     * could make: the element would silently vanish from layout.
     */
    if (value->kind != ROBO_CSS_COMPUTED_VALUE_DISPLAY) {
        return ROBO_CSS_DISPLAY_INLINE;
    }
    return robo_css_computed_value_display(value);
}
