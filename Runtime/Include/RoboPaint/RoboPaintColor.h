/*
 * RoboPaint colour bridge (Phase 11.4).
 *
 * One direction, one record, and no second colour system: RoboCSS owns a colour
 * (`CssColor`, four 8-bit sRGB channels), RoboGraphics owns a colour
 * (`GfxColorRgba8`, four 8-bit channels), and this module's only job is to carry the
 * first into the second so a background can reach a surface. There is no arithmetic here
 * beyond the channel copies -- both records are 8-bit sRGB with straight alpha, so the
 * conversion is a field-by-field copy and loses nothing.
 *
 * The four CSS kinds are decided rather than flattened, because three of them are not
 * colours a surface can take:
 *
 *   RGBA      draws -- the channels copy straight across;
 *   CURRENT   `currentColor`, and **refused**, because only the layer that owns an element's
 *             `color` can resolve it and this bridge has no element. The background pass
 *             resolves it before emitting (`RoboPaintContext.c`); a background that arrives
 *             here still contextual is one whose element declared no usable `color`, which is
 *             a legitimate conclusion and not something to invent an answer for;
 *   NONE      nothing was declared, so there is nothing to draw;
 *   INVALID   it was meant to be a colour and was not, so it is a mistake rather than an
 *             absence.
 *
 * A caller that only wants "does this put a pixel down?" asks
 * `PaintColorIsDrawable`, which is exactly a resolved colour with a non-zero alpha --
 * the same predicate the background pass used to decide whether to emit the record.
 */
#ifndef ROBO_PAINT_COLOR_H
#define ROBO_PAINT_COLOR_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSColor.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboPaint/RoboPaintDiagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Non-zero when @p color can reach a surface as-is: a resolved colour with a non-zero alpha.
 *
 * `currentColor` is not drawable here (it has no channel set to carry), `none` is not (there
 * was no colour), `invalid` is not (it was not a colour), and a fully transparent colour is
 * not -- a fill with `alpha == 0` occupies no pixel. All four are answers rather than
 * failures, which is why this is a predicate and not a status.
 */
int PaintColorIsDrawable(CssColor color);

/**
 * The RoboGraphics colour @p color names.
 *
 * The channels copy field for field, so an `rgba(51, 102, 204, 128)` background arrives at a
 * surface as the same four numbers -- there is no precision to lose between two 8-bit sRGB
 * records, and none is invented.
 *
 * @return RoboOk and @p out when @p color is a resolved colour;
 *         RoboInvalidArgument for NULL @p out or an `invalid` colour;
 *         RoboNotFound for `none`;
 *         RoboNotSupported for `currentColor` (see the header comment).
 */
Result PaintColorToRgba8(CssColor color,
                                        GfxColorRgba8 *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_COLOR_H */
