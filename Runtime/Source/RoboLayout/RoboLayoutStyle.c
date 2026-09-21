/*
 * RoboLayout's CSS seam (Phase 10.1) — see RoboLayoutStyle.h.
 */

#include "RoboLayout/RoboLayoutStyle.h"

#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSDisplay.h"

#include <string.h>

/** The one property name RoboLayout knows. Spelled here and nowhere else. */
#define RoboLayoutPropertyDisplay "display"

void LayoutStyleSourceInit(LayoutStyleSource *source)
{
    if (!source) {
        return;
    }
    memset(source, 0, sizeof(*source));
}

int LayoutStyleSourceIsUsable(const LayoutStyleSource *source)
{
    return source && source->ComputedStyleFor != NULL;
}

const CssComputedStyle *LayoutStyleSourceStyle(
    const LayoutStyleSource *source,
    const HtmlNodeHandle *element)
{
    if (!LayoutStyleSourceIsUsable(source) || !element) {
        return NULL;
    }
    return source->ComputedStyleFor(source->userdata, element);
}

CssDisplay LayoutStyleDisplay(const CssComputedStyle *style)
{
    const CssComputedValue *value;

    /*
     * No style at all, or no entry for `display`. Both are answered with the
     * property's initial value, and both are the *same* answer on purpose: 9.8's
     * container holds the entries an element has an answer for, and a
     * non-inherited property nothing declared is answered by its initial value
     * rather than by an entry. So "nothing styled this element" and "nothing
     * declared display on it" really are one case.
     */
    if (!style) {
        return RoboCssDisplayInline;
    }
    value = CssComputedStyleComputedByName(
        style, CssSliceFromText(RoboLayoutPropertyDisplay));
    if (!value) {
        return RoboCssDisplayInline;
    }
    /*
     * The kind is checked before the value is read, and it is not a formality.
     * `CssComputedValueDisplay` answers NONE for any other kind, and NONE
     * is also a real display value -- `display: none`. Reading the accessor
     * without the kind would turn a value that is not a display at all into a
     * removed box, which is the single most damaging reading a box-tree builder
     * could make: the element would silently vanish from layout.
     */
    if (value->kind != RoboCssComputedValueDisplay) {
        return RoboCssDisplayInline;
    }
    return CssComputedValueDisplay(value);
}
