/*
 * RoboLayout geometry (Phase 10.1).
 *
 * The decision this header records is that RoboLayout has **no geometry types of
 * its own**. RoboGraphics already defines canonical pixel geometry, and defining
 * a second point/size/rectangle would be the worst possible kind of duplication:
 * two types that agree today and differ subtly in a year, with paint having to
 * convert between them at the one boundary where a mistake is a visible artifact.
 *
 * So the names below are aliases, and nothing here allocates, converts or
 * computes:
 *
 *   LayoutPoint   GfxPoint    signed integer, origin top-left
 *   LayoutSize    GfxSize     width x height, <= 0 means empty
 *   LayoutRect    GfxRect     half-open [x, x+w) x [y, y+h)
 *   LayoutRectF  GfxRectF   the float form, for sub-pixel use
 *
 * The aliases exist so that a layout signature reads as layout -- `outRect`
 * cannot accidentally be understood as a device rectangle -- while an
 * implementation still passes one object to a paint phase with no conversion.
 *
 * Three facts about this geometry are load-bearing for layout and are stated
 * once, in RoboGraphicsGeometry.h, so they are not restated (and cannot drift)
 * here: the origin is top-left with +Y down; integer rectangles are half-open;
 * and a rectangle with a non-positive width or height is *empty* rather than
 * invalid. Emptiness is not an error in layout -- a box with no content has zero
 * height -- which is exactly why it is a predicate and not a failure.
 *
 * A layout result does not use these types as *inputs*: 10.1 computes no
 * coordinate (see RoboLayoutResult.h), so nothing in this module constructs a
 * rectangle except the test that proves the reuse is real.
 */

#ifndef ROBO_LAYOUT_GEOMETRY_H
#define ROBO_LAYOUT_GEOMETRY_H

#include "RoboGraphics/RoboGraphicsGeometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Layout is positioned in the graphics pixel space, unchanged. */
typedef GfxPoint LayoutPoint;
typedef GfxSize LayoutSize;
typedef GfxRect LayoutRect;
typedef GfxRectF LayoutRectF;

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_GEOMETRY_H */
