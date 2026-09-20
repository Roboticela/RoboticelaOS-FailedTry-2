/*
 * RoboPaint colour bridge (Phase 11.4) — see RoboPaintColor.h.
 *
 * Two records with the same shape, one copy, and a decision per kind. Nothing here reads a
 * computed style: the colour arrives already resolved by the layer that could resolve it.
 */

#include "RoboPaint/RoboPaintColor.h"

int robo_paint_color_is_drawable(robo_css_color_t color)
{
    return color.kind == ROBO_CSS_COLOR_RGBA && color.alpha != 0u;
}

robo_result_t robo_paint_color_to_rgba8(robo_css_color_t color,
                                        robo_gfx_color_rgba8_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    switch (color.kind) {
    case ROBO_CSS_COLOR_RGBA:
        /* The whole conversion: four channel copies between two 8-bit sRGB records. No
         * arithmetic and no colour space to convert, so nothing can be lost or invented. */
        out->r = color.red;
        out->g = color.green;
        out->b = color.blue;
        out->a = color.alpha;
        return ROBO_OK;
    case ROBO_CSS_COLOR_CURRENT:
        /* Contextual, and this bridge has no element to resolve it against -- the background
         * pass does that before emitting. A distinct code rather than "invalid", because the
         * colour is legitimate and only its *place* is wrong. */
        return ROBO_NOT_SUPPORTED;
    case ROBO_CSS_COLOR_INVALID:
        return ROBO_INVALID_ARGUMENT;
    case ROBO_CSS_COLOR_NONE:
    default:
        return ROBO_NOT_FOUND;
    }
}
