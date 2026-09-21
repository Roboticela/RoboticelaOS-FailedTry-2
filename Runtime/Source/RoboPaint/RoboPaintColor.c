/*
 * RoboPaint colour bridge (Phase 11.4) — see RoboPaintColor.h.
 *
 * Two records with the same shape, one copy, and a decision per kind. Nothing here reads a
 * computed style: the colour arrives already resolved by the layer that could resolve it.
 */

#include "RoboPaint/RoboPaintColor.h"

int PaintColorIsDrawable(CssColor color)
{
    return color.kind == RoboCssColorRgba && color.alpha != 0u;
}

Result PaintColorToRgba8(CssColor color,
                                        GfxColorRgba8 *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    switch (color.kind) {
    case RoboCssColorRgba:
        /* The whole conversion: four channel copies between two 8-bit sRGB records. No
         * arithmetic and no colour space to convert, so nothing can be lost or invented. */
        out->r = color.red;
        out->g = color.green;
        out->b = color.blue;
        out->a = color.alpha;
        return RoboOk;
    case RoboCssColorCurrent:
        /* Contextual, and this bridge has no element to resolve it against -- the background
         * pass does that before emitting. A distinct code rather than "invalid", because the
         * colour is legitimate and only its *place* is wrong. */
        return RoboNotSupported;
    case RoboCssColorInvalid:
        return RoboInvalidArgument;
    case RoboCssColorNone:
    default:
        return RoboNotFound;
    }
}
