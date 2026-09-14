/*
 * RoboGraphics geometry (Goal 6.1).
 *
 * Convention (device / surface / window / display pixels):
 *   Origin at top-left of the space.
 *   +X right, +Y down.
 *   Integer rectangles are half-open: [x, x+width) × [y, y+height).
 *   Pixel (i, j) covers continuous square [i, i+1) × [j, j+1).
 *
 * Floating-point geometry uses the same axes; conversion helpers document
 * rounding. Complex clip/path algorithms belong to later goals.
 *
 * Chromium reference (not a dependency): ui/gfx Rect half-open Contains.
 */

#ifndef ROBO_GRAPHICS_GEOMETRY_H
#define ROBO_GRAPHICS_GEOMETRY_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_gfx_point {
    int32_t x;
    int32_t y;
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
 * Axis-aligned integer rectangle.
 * right = x + width, bottom = y + height (exclusive).
 */
typedef struct robo_gfx_rect {
    int32_t x;
    int32_t y;
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

robo_gfx_point_t robo_gfx_point(int32_t x, int32_t y);
robo_gfx_size_t robo_gfx_size(int32_t width, int32_t height);
robo_gfx_vector_t robo_gfx_vector(int32_t dx, int32_t dy);
robo_gfx_rect_t robo_gfx_rect(int32_t x, int32_t y, int32_t width, int32_t height);
robo_gfx_rect_t robo_gfx_rect_from_size(robo_gfx_size_t size);

robo_gfx_point_f_t robo_gfx_point_f(float x, float y);
robo_gfx_size_f_t robo_gfx_size_f(float width, float height);
robo_gfx_rect_f_t robo_gfx_rect_f(float x, float y, float width, float height);

int robo_gfx_size_is_empty(robo_gfx_size_t s);
int robo_gfx_size_is_valid(robo_gfx_size_t s); /* width>=0 && height>=0 */
int robo_gfx_rect_is_empty(robo_gfx_rect_t r);
int robo_gfx_rect_is_valid(robo_gfx_rect_t r); /* non-negative size */

int32_t robo_gfx_rect_right(robo_gfx_rect_t r);
int32_t robo_gfx_rect_bottom(robo_gfx_rect_t r);

/** Half-open: contains (x,y) iff x in [r.x, r.x+w) and y in [r.y, r.y+h). */
int robo_gfx_rect_contains_point(robo_gfx_rect_t r, robo_gfx_point_t p);

robo_gfx_point_t robo_gfx_rect_center(robo_gfx_rect_t r);

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
