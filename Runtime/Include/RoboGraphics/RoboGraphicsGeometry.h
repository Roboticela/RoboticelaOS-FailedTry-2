/*
 * RoboGraphics geometry (Goals 6.1 / 6.6).
 *
 * Canonical pixel coordinates (device / surface / window / display):
 *   Origin at top-left.
 *   +X right, +Y down.
 *   Integer rectangles are half-open: [x, x+width) × [y, y+height).
 *   Pixel (i, j) covers continuous square [i, i+1) × [j, j+1).
 *
 * Geometry vs storage:
 *   Point/Rect use signed logical coordinates (negative allowed).
 *   Framebuffer/surface memory indices are non-negative and validated
 *   before pixel access.
 *
 * Empty size/rect: width <= 0 OR height <= 0.
 *
 * Overflow: arithmetic helpers that can overflow return Result.
 * Unchecked right()/bottom() use int64 intermediates and saturate to
 * INT32_MIN/MAX (never signed wrap). Prefer the _checked variants.
 *
 * Floating-point geometry uses the same axes; conversion helpers document
 * rounding. Path command model is Goal 6.8; full transforms / curves later.
 *
 * Chromium reference only (not a dependency): ui/gfx half-open Rect.
 */

#ifndef ROBO_GRAPHICS_GEOMETRY_H
#define ROBO_GRAPHICS_GEOMETRY_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Signed logical pixel coordinate along X (rightward). */
typedef int32_t GfxPixelX;
/** Signed logical pixel coordinate along Y (downward). */
typedef int32_t GfxPixelY;

typedef struct GfxPoint {
    GfxPixelX x;
    GfxPixelY y;
} GfxPoint;

typedef struct GfxSize {
    int32_t width;
    int32_t height;
} GfxSize;

typedef struct GfxVector {
    int32_t dx;
    int32_t dy;
} GfxVector;

/**
 * Axis-aligned integer rectangle (half-open).
 * left=x, top=y, right=x+width (exclusive), bottom=y+height (exclusive).
 * Positions may be negative; width/height < 0 ⇒ invalid; <=0 ⇒ empty.
 */
typedef struct GfxRect {
    GfxPixelX x;
    GfxPixelY y;
    int32_t width;
    int32_t height;
} GfxRect;

typedef struct GfxPointF {
    float x;
    float y;
} GfxPointF;

typedef struct GfxSizeF {
    float width;
    float height;
} GfxSizeF;

typedef struct GfxVectorF {
    float dx;
    float dy;
} GfxVectorF;

typedef struct GfxRectF {
    float x;
    float y;
    float width;
    float height;
} GfxRectF;

/**
 * 2D affine transform (column vector convention):
 *   | m00 m01 m02 |   | x |
 *   | m10 m11 m12 | × | y |
 *   |  0   0   1  |   | 1 |
 * Identity: m00=m11=1, others 0.
 *
 * multiply(A,B,out) → out = A*B; maps p as A*(B*p) (B first, then A).
 * Full transform API (rotate/shear/inverse/map_rect/path): RoboGraphicsTransform.h
 */
typedef struct GfxAffine {
    float m00, m01, m02;
    float m10, m11, m12;
} GfxAffine;

/* ---- Construction ---- */

GfxPoint MakeGfxPoint(GfxPixelX x, GfxPixelY y);
GfxSize MakeGfxSize(int32_t width, int32_t height);
GfxVector MakeGfxVector(int32_t dx, int32_t dy);
GfxRect MakeGfxRect(GfxPixelX x,
                              GfxPixelY y,
                              int32_t width,
                              int32_t height);
GfxRect GfxRectFromSize(GfxSize size);
GfxRect GfxRectEmpty(void);

/**
 * Build from origin + size. Negative size components → RoboInvalidArgument.
 */
Result GfxRectFromPointSize(GfxPoint origin,
                                            GfxSize size,
                                            GfxRect *out);

/**
 * Build from half-open edges [left,right) × [top,bottom).
 * right < left or bottom < top → RoboInvalidArgument.
 * right == left or bottom == top → empty success.
 * Edge differences that do not fit int32 width/height → RoboInvalidArgument.
 */
Result GfxRectFromLtrb(GfxPixelX left,
                                      GfxPixelY top,
                                      GfxPixelX right,
                                      GfxPixelY bottom,
                                      GfxRect *out);

GfxPointF MakeGfxPointF(float x, float y);
GfxSizeF MakeGfxSizeF(float width, float height);
GfxRectF MakeGfxRectF(float x, float y, float width, float height);

/* ---- Point ---- */

int GfxPointEqual(GfxPoint a, GfxPoint b);
Result GfxPointAdd(GfxPoint a,
                                 GfxPoint b,
                                 GfxPoint *out);
Result GfxPointSub(GfxPoint a,
                                 GfxPoint b,
                                 GfxPoint *out);
Result GfxPointTranslate(GfxPoint p,
                                       int32_t dx,
                                       int32_t dy,
                                       GfxPoint *out);

/* ---- Size ---- */

int GfxSizeEqual(GfxSize a, GfxSize b);
int GfxSizeIsEmpty(GfxSize s);
int GfxSizeIsValid(GfxSize s); /* width>=0 && height>=0 */

/**
 * Area = width * height as uint64 when both dimensions are non-negative.
 * Empty → *outArea = 0, RoboOk. Negative dimension → RoboInvalidArgument.
 */
Result GfxSizeArea(GfxSize s, uint64_t *outArea);

/* ---- Rect queries ---- */

int GfxRectEqual(GfxRect a, GfxRect b);
int GfxRectIsEmpty(GfxRect r);
int GfxRectIsValid(GfxRect r); /* non-negative size */

GfxPixelX GfxRectLeft(GfxRect r);
GfxPixelY GfxRectTop(GfxRect r);
int32_t GfxRectWidth(GfxRect r);
int32_t GfxRectHeight(GfxRect r);

/**
 * Exclusive right/bottom. Uses int64 math; saturates to INT32 range
 * (never signed wrap). Prefer _checked when overflow must be an error.
 */
int32_t GfxRectRight(GfxRect r);
int32_t GfxRectBottom(GfxRect r);

Result GfxRectRightChecked(GfxRect r, int32_t *out);
Result GfxRectBottomChecked(GfxRect r, int32_t *out);

/** Half-open: contains (x,y) iff x in [r.x, r.x+w) and y in [r.y, r.y+h). */
int GfxRectContainsPoint(GfxRect r, GfxPoint p);

/**
 * True if every pixel of @p inner is inside @p outer (half-open).
 * Empty @p inner → true (vacuous). Invalid sizes → false.
 */
int GfxRectEncloses(GfxRect outer, GfxRect inner);

/** True if intersection is non-empty. Empty inputs do not intersect. */
int GfxRectIntersects(GfxRect a, GfxRect b);

GfxPoint GfxRectCenter(GfxRect r);

/* ---- Rect operations (no allocation; overflow → RoboInvalidArgument) ---- */

/**
 * Intersection: max(left/top), min(right/bottom). No overlap → empty @p out.
 * Either input empty → empty @p out, RoboOk.
 */
Result GfxRectIntersect(GfxRect a,
                                      GfxRect b,
                                      GfxRect *out);

/**
 * Smallest axis-aligned bounding rect containing both.
 * Empty inputs are ignored (union of one empty and one non-empty = non-empty).
 * Both empty → empty. Result edge overflow → RoboInvalidArgument.
 */
Result GfxRectBounding(GfxRect a,
                                     GfxRect b,
                                     GfxRect *out);

Result GfxRectTranslate(GfxRect r,
                                      int32_t dx,
                                      int32_t dy,
                                      GfxRect *out);

/** Same as intersect(rect, bounds). */
Result GfxRectClip(GfxRect rect,
                                 GfxRect bounds,
                                 GfxRect *out);

/**
 * Shrink all sides by @p amount (negative amount = outset).
 * Over-shrink → empty success. Edge/size overflow → RoboInvalidArgument.
 */
Result GfxRectInset(GfxRect r,
                                  int32_t amount,
                                  GfxRect *out);
Result GfxRectOutset(GfxRect r,
                                   int32_t amount,
                                   GfxRect *out);

/* ---- Float helpers (Goal 6.1) ---- */

/** Floor toward -inf for each component (device-pixel sampling). */
GfxPoint GfxPointFFloor(GfxPointF p);
/** Round half away from zero (presentation / layout snap). */
GfxPoint GfxPointFRound(GfxPointF p);
GfxRect GfxRectFFloor(GfxRectF r);
GfxRect GfxRectFRoundOut(GfxRectF r);

void GfxAffineIdentity(GfxAffine *out);
void GfxAffineTranslate(GfxAffine *out, float tx, float ty);
void GfxAffineScale(GfxAffine *out, float sx, float sy);
Result GfxAffineMultiply(const GfxAffine *a,
                                       const GfxAffine *b,
                                       GfxAffine *out);
GfxPointF GfxAffineMapPoint(const GfxAffine *m,
                                             GfxPointF p);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_GEOMETRY_H */
