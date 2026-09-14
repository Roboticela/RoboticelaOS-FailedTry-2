/*
 * RoboGraphics 2D affine transforms (Goal 6.9).
 *
 * Canonical type: robo_gfx_affine_t (defined in RoboGraphicsGeometry.h).
 *
 * Column-vector convention:
 *   | m00 m01 m02 |   | x |
 *   | m10 m11 m12 | × | y |
 *   |  0   0   1  |   | 1 |
 *
 * Composition: multiply(A, B, out) computes out = A * B.
 * Point mapping: (A * B) maps p as A*(B*p) — apply B first, then A.
 *
 * Angles: degrees (primary) and radians. Positive rotation is
 * counterclockwise in the mathematical plane (Chromium/Skia-compatible
 * matrix). With Y-down device coordinates this appears clockwise on screen.
 *
 * See graphics/docs/Transforms.md.
 */

#ifndef ROBO_GRAPHICS_TRANSFORM_H
#define ROBO_GRAPHICS_TRANSFORM_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsPath.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Alias: affine matrix is the canonical 2D transform. */
typedef robo_gfx_affine_t robo_gfx_transform_t;

/* ---- Construction (also: identity / translate / scale in Geometry.h) ---- */

/** Arbitrary affine from row-major visual components (same field order). */
void robo_gfx_affine_set(robo_gfx_affine_t *out,
                         float m00,
                         float m01,
                         float m02,
                         float m10,
                         float m11,
                         float m12);

/**
 * Rotation about the origin.
 * @p degrees — positive = CCW in math plane (see file header).
 * Exact matrices for multiples of 90° (…, -180, -90, 0, 90, 180, 270, …).
 */
void robo_gfx_affine_rotate_deg(robo_gfx_affine_t *out, float degrees);
void robo_gfx_affine_rotate_rad(robo_gfx_affine_t *out, float radians);

/**
 * Shear: x' = x + shx*y, y' = y + shy*x.
 * Zero shear → identity linear part.
 */
void robo_gfx_affine_shear(robo_gfx_affine_t *out, float shx, float shy);

/* ---- Queries ---- */

/** Exact identity (all fields match the identity matrix). */
int robo_gfx_affine_is_identity(const robo_gfx_affine_t *m);

/** 1 if |det(linear)| is above the invertibility epsilon. */
int robo_gfx_affine_is_invertible(const robo_gfx_affine_t *m);

/** det = m00*m11 - m01*m10 (linear 2×2). */
float robo_gfx_affine_determinant(const robo_gfx_affine_t *m);

/**
 * Exact component equality. NULL → 0.
 * Prefer for constructed matrices; float noise may differ after multiply.
 */
int robo_gfx_affine_equal(const robo_gfx_affine_t *a,
                          const robo_gfx_affine_t *b);

/* ---- Composition ---- */

/**
 * out = A * B  (same as robo_gfx_affine_multiply).
 * Applies B first, then A, when mapping points.
 */
robo_result_t robo_gfx_affine_compose(const robo_gfx_affine_t *a,
                                      const robo_gfx_affine_t *b,
                                      robo_gfx_affine_t *out);

/**
 * *dst = *dst * other  (PreConcat / apply other first when mapping).
 * NULL → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_affine_pre_concat(robo_gfx_affine_t *dst,
                                         const robo_gfx_affine_t *other);

/**
 * *dst = other * *dst  (PostConcat / apply dst first, then other).
 */
robo_result_t robo_gfx_affine_post_concat(robo_gfx_affine_t *dst,
                                          const robo_gfx_affine_t *other);

/* ---- Inverse ---- */

/**
 * Invert @p m into @p out.
 * Singular / non-invertible → ROBO_INVALID_ARGUMENT (out unchanged).
 * Identity / translation / rotation / shear / scale inverses as expected.
 */
robo_result_t robo_gfx_affine_invert(const robo_gfx_affine_t *m,
                                     robo_gfx_affine_t *out);

/* ---- Map geometry ---- */

/** Map a floating point. NULL matrix → returns @p p unchanged. */
robo_gfx_point_f_t robo_gfx_affine_map_point_f(const robo_gfx_affine_t *m,
                                               robo_gfx_point_f_t p);

/**
 * Map an integer point via float transform, then round half-away-from-zero
 * (robo_gfx_point_f_round).
 */
robo_gfx_point_t robo_gfx_affine_map_point_i(const robo_gfx_affine_t *m,
                                             robo_gfx_point_t p);

/**
 * Axis-aligned enclosing bounds of the transformed rectangle.
 * Transforms the four continuous corners of the half-open rect, then
 * rounds outward to an integer half-open rect (floor min, ceil max).
 * Empty / invalid input → empty @p out, ROBO_OK.
 * Edge overflow → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_affine_map_rect(const robo_gfx_affine_t *m,
                                       robo_gfx_rect_t r,
                                       robo_gfx_rect_t *out);

/**
 * Floating enclosing AABB of transformed half-open float rect corners.
 * Empty input → empty @p out, ROBO_OK.
 */
robo_result_t robo_gfx_affine_map_rect_f(const robo_gfx_affine_t *m,
                                         robo_gfx_rect_f_t r,
                                         robo_gfx_rect_f_t *out);

/* ---- Path integration ---- */

/**
 * Create a new path with MoveTo/LineTo points transformed; Close preserved.
 * Source path is not modified.
 * QUAD_TO / CUBIC_TO (if present) → ROBO_NOT_SUPPORTED.
 * OOM → ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_gfx_path_transformed(const robo_gfx_path_t *src,
                                        const robo_gfx_affine_t *m,
                                        robo_gfx_path_t **out);

/**
 * In-place transform of path geometry (same semantics as transformed).
 * On failure the path is left unchanged.
 */
robo_result_t robo_gfx_path_transform(robo_gfx_path_t *path,
                                      const robo_gfx_affine_t *m);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_TRANSFORM_H */
