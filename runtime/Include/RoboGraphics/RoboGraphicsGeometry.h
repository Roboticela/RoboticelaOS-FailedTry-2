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
 * Overflow: arithmetic helpers that can overflow return robo_result_t.
 * Unchecked right()/bottom() use int64 intermediates and saturate to
 * INT32_MIN/MAX (never signed wrap). Prefer the _checked variants.
 *
 * Floating-point geometry uses the same axes; conversion helpers document
 * rounding. Lines / paths / full transforms are later goals.
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
typedef int32_t robo_gfx_pixel_x_t;
/** Signed logical pixel coordinate along Y (downward). */
typedef int32_t robo_gfx_pixel_y_t;

typedef struct robo_gfx_point {
    robo_gfx_pixel_x_t x;
    robo_gfx_pixel_y_t y;
} robo_gfx_point_t;

typedef struct robo_gfx_size {
    int32_t width;
    int32_t height;
} robo_gfx_size_t;

typedef struct robo_gfx_vector {
    int32_t dx;
    int32_t dy;
} robo_gfx_vector_t;

/**
 * Axis-aligned integer rectangle (half-open).
 * left=x, top=y, right=x+width (exclusive), bottom=y+height (exclusive).
 * Positions may be negative; width/height < 0 ⇒ invalid; <=0 ⇒ empty.
 */
typedef struct robo_gfx_rect {
    robo_gfx_pixel_x_t x;
    robo_gfx_pixel_y_t y;
    int32_t width;
    int32_t height;
} robo_gfx_rect_t;

typedef struct robo_gfx_point_f {
    float x;
    float y;
} robo_gfx_point_f_t;

typedef struct robo_gfx_size_f {
    float width;
    float height;
} robo_gfx_size_f_t;

typedef struct robo_gfx_vector_f {
    float dx;
    float dy;
} robo_gfx_vector_f_t;

typedef struct robo_gfx_rect_f {
    float x;
    float y;
    float width;
    float height;
} robo_gfx_rect_f_t;

/**
 * 2D affine transform (column vector convention):
 *   | m00 m01 m02 |   | x |
 *   | m10 m11 m12 | × | y |
 *   |  0   0   1  |   | 1 |
 * Identity: m00=m11=1, others 0.
 */
typedef struct robo_gfx_affine {
    float m00, m01, m02;
    float m10, m11, m12;
} robo_gfx_affine_t;

/* ---- Construction ---- */

robo_gfx_point_t robo_gfx_point(robo_gfx_pixel_x_t x, robo_gfx_pixel_y_t y);
robo_gfx_size_t robo_gfx_size(int32_t width, int32_t height);
robo_gfx_vector_t robo_gfx_vector(int32_t dx, int32_t dy);
robo_gfx_rect_t robo_gfx_rect(robo_gfx_pixel_x_t x,
                              robo_gfx_pixel_y_t y,
                              int32_t width,
                              int32_t height);
robo_gfx_rect_t robo_gfx_rect_from_size(robo_gfx_size_t size);
robo_gfx_rect_t robo_gfx_rect_empty(void);

/**
 * Build from origin + size. Negative size components → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_rect_from_point_size(robo_gfx_point_t origin,
                                            robo_gfx_size_t size,
                                            robo_gfx_rect_t *out);

/**
 * Build from half-open edges [left,right) × [top,bottom).
 * right < left or bottom < top → ROBO_INVALID_ARGUMENT.
 * right == left or bottom == top → empty success.
 * Edge differences that do not fit int32 width/height → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_rect_from_ltrb(robo_gfx_pixel_x_t left,
                                      robo_gfx_pixel_y_t top,
                                      robo_gfx_pixel_x_t right,
                                      robo_gfx_pixel_y_t bottom,
                                      robo_gfx_rect_t *out);

robo_gfx_point_f_t robo_gfx_point_f(float x, float y);
robo_gfx_size_f_t robo_gfx_size_f(float width, float height);
robo_gfx_rect_f_t robo_gfx_rect_f(float x, float y, float width, float height);

/* ---- Point ---- */

int robo_gfx_point_equal(robo_gfx_point_t a, robo_gfx_point_t b);
robo_result_t robo_gfx_point_add(robo_gfx_point_t a,
                                 robo_gfx_point_t b,
                                 robo_gfx_point_t *out);
robo_result_t robo_gfx_point_sub(robo_gfx_point_t a,
                                 robo_gfx_point_t b,
                                 robo_gfx_point_t *out);
robo_result_t robo_gfx_point_translate(robo_gfx_point_t p,
                                       int32_t dx,
                                       int32_t dy,
                                       robo_gfx_point_t *out);

/* ---- Size ---- */

int robo_gfx_size_equal(robo_gfx_size_t a, robo_gfx_size_t b);
int robo_gfx_size_is_empty(robo_gfx_size_t s);
int robo_gfx_size_is_valid(robo_gfx_size_t s); /* width>=0 && height>=0 */

/**
 * Area = width * height as uint64 when both dimensions are non-negative.
 * Empty → *out_area = 0, ROBO_OK. Negative dimension → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_size_area(robo_gfx_size_t s, uint64_t *out_area);

/* ---- Rect queries ---- */

int robo_gfx_rect_equal(robo_gfx_rect_t a, robo_gfx_rect_t b);
int robo_gfx_rect_is_empty(robo_gfx_rect_t r);
int robo_gfx_rect_is_valid(robo_gfx_rect_t r); /* non-negative size */

robo_gfx_pixel_x_t robo_gfx_rect_left(robo_gfx_rect_t r);
robo_gfx_pixel_y_t robo_gfx_rect_top(robo_gfx_rect_t r);
int32_t robo_gfx_rect_width(robo_gfx_rect_t r);
int32_t robo_gfx_rect_height(robo_gfx_rect_t r);

/**
 * Exclusive right/bottom. Uses int64 math; saturates to INT32 range
 * (never signed wrap). Prefer _checked when overflow must be an error.
 */
int32_t robo_gfx_rect_right(robo_gfx_rect_t r);
int32_t robo_gfx_rect_bottom(robo_gfx_rect_t r);

robo_result_t robo_gfx_rect_right_checked(robo_gfx_rect_t r, int32_t *out);
robo_result_t robo_gfx_rect_bottom_checked(robo_gfx_rect_t r, int32_t *out);

/** Half-open: contains (x,y) iff x in [r.x, r.x+w) and y in [r.y, r.y+h). */
int robo_gfx_rect_contains_point(robo_gfx_rect_t r, robo_gfx_point_t p);

/**
 * True if every pixel of @p inner is inside @p outer (half-open).
 * Empty @p inner → true (vacuous). Invalid sizes → false.
 */
int robo_gfx_rect_encloses(robo_gfx_rect_t outer, robo_gfx_rect_t inner);

/** True if intersection is non-empty. Empty inputs do not intersect. */
int robo_gfx_rect_intersects(robo_gfx_rect_t a, robo_gfx_rect_t b);

robo_gfx_point_t robo_gfx_rect_center(robo_gfx_rect_t r);

/* ---- Rect operations (no allocation; overflow → ROBO_INVALID_ARGUMENT) ---- */

/**
 * Intersection: max(left/top), min(right/bottom). No overlap → empty @p out.
 * Either input empty → empty @p out, ROBO_OK.
 */
robo_result_t robo_gfx_rect_intersect(robo_gfx_rect_t a,
                                      robo_gfx_rect_t b,
                                      robo_gfx_rect_t *out);

/**
 * Smallest axis-aligned bounding rect containing both.
 * Empty inputs are ignored (union of one empty and one non-empty = non-empty).
 * Both empty → empty. Result edge overflow → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_rect_bounding(robo_gfx_rect_t a,
                                     robo_gfx_rect_t b,
                                     robo_gfx_rect_t *out);

robo_result_t robo_gfx_rect_translate(robo_gfx_rect_t r,
                                      int32_t dx,
                                      int32_t dy,
                                      robo_gfx_rect_t *out);

/** Same as intersect(rect, bounds). */
robo_result_t robo_gfx_rect_clip(robo_gfx_rect_t rect,
                                 robo_gfx_rect_t bounds,
                                 robo_gfx_rect_t *out);

/**
 * Shrink all sides by @p amount (negative amount = outset).
 * Over-shrink → empty success. Edge/size overflow → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_rect_inset(robo_gfx_rect_t r,
                                  int32_t amount,
                                  robo_gfx_rect_t *out);
robo_result_t robo_gfx_rect_outset(robo_gfx_rect_t r,
                                   int32_t amount,
                                   robo_gfx_rect_t *out);

/* ---- Float helpers (Goal 6.1) ---- */

/** Floor toward -inf for each component (device-pixel sampling). */
robo_gfx_point_t robo_gfx_point_f_floor(robo_gfx_point_f_t p);
/** Round half away from zero (presentation / layout snap). */
robo_gfx_point_t robo_gfx_point_f_round(robo_gfx_point_f_t p);
robo_gfx_rect_t robo_gfx_rect_f_floor(robo_gfx_rect_f_t r);
robo_gfx_rect_t robo_gfx_rect_f_round_out(robo_gfx_rect_f_t r);

void robo_gfx_affine_identity(robo_gfx_affine_t *out);
void robo_gfx_affine_translate(robo_gfx_affine_t *out, float tx, float ty);
void robo_gfx_affine_scale(robo_gfx_affine_t *out, float sx, float sy);
robo_result_t robo_gfx_affine_multiply(const robo_gfx_affine_t *a,
                                       const robo_gfx_affine_t *b,
                                       robo_gfx_affine_t *out);
robo_gfx_point_f_t robo_gfx_affine_map_point(const robo_gfx_affine_t *m,
                                             robo_gfx_point_f_t p);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_GEOMETRY_H */
