/*
 * RoboGraphics 2D affine transforms (Goal 6.9).
 *
 * Canonical type: GfxAffine (defined in RoboGraphicsGeometry.h).
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
typedef GfxAffine GfxTransform;

/* ---- Construction (also: identity / translate / scale in Geometry.h) ---- */

/** Arbitrary affine from row-major visual components (same field order). */
void GfxAffineSet(GfxAffine *out,
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
void GfxAffineRotateDeg(GfxAffine *out, float degrees);
void GfxAffineRotateRad(GfxAffine *out, float radians);

/**
 * Shear: x' = x + shx*y, y' = y + shy*x.
 * Zero shear → identity linear part.
 */
void GfxAffineShear(GfxAffine *out, float shx, float shy);

/* ---- Queries ---- */

/** Exact identity (all fields match the identity matrix). */
int GfxAffineIsIdentity(const GfxAffine *m);

/** 1 if |det(linear)| is above the invertibility epsilon. */
int GfxAffineIsInvertible(const GfxAffine *m);

/** det = m00*m11 - m01*m10 (linear 2×2). */
float GfxAffineDeterminant(const GfxAffine *m);

/**
 * Exact component equality. NULL → 0.
 * Prefer for constructed matrices; float noise may differ after multiply.
 */
int GfxAffineEqual(const GfxAffine *a,
                          const GfxAffine *b);

/* ---- Composition ---- */

/**
 * out = A * B  (same as GfxAffineMultiply).
 * Applies B first, then A, when mapping points.
 */
Result GfxAffineCompose(const GfxAffine *a,
                                      const GfxAffine *b,
                                      GfxAffine *out);

/**
 * *dst = *dst * other  (PreConcat / apply other first when mapping).
 * NULL → RoboInvalidArgument.
 */
Result GfxAffinePreConcat(GfxAffine *dst,
                                         const GfxAffine *other);

/**
 * *dst = other * *dst  (PostConcat / apply dst first, then other).
 */
Result GfxAffinePostConcat(GfxAffine *dst,
                                          const GfxAffine *other);

/* ---- Inverse ---- */

/**
 * Invert @p m into @p out.
 * Singular / non-invertible → RoboInvalidArgument (out unchanged).
 * Identity / translation / rotation / shear / scale inverses as expected.
 */
Result GfxAffineInvert(const GfxAffine *m,
                                     GfxAffine *out);

/* ---- Map geometry ---- */

/** Map a floating point. NULL matrix → returns @p p unchanged. */
GfxPointF GfxAffineMapPointF(const GfxAffine *m,
                                               GfxPointF p);

/**
 * Map an integer point via float transform, then round half-away-from-zero
 * (GfxPointFRound).
 */
GfxPoint GfxAffineMapPointI(const GfxAffine *m,
                                             GfxPoint p);

/**
 * Axis-aligned enclosing bounds of the transformed rectangle.
 * Transforms the four continuous corners of the half-open rect, then
 * rounds outward to an integer half-open rect (floor min, ceil max).
 * Empty / invalid input → empty @p out, RoboOk.
 * Edge overflow → RoboInvalidArgument.
 */
Result GfxAffineMapRect(const GfxAffine *m,
                                       GfxRect r,
                                       GfxRect *out);

/**
 * Floating enclosing AABB of transformed half-open float rect corners.
 * Empty input → empty @p out, RoboOk.
 */
Result GfxAffineMapRectF(const GfxAffine *m,
                                         GfxRectF r,
                                         GfxRectF *out);

/* ---- Path integration ---- */

/**
 * Create a new path with MoveTo/LineTo points transformed; Close preserved.
 * Source path is not modified.
 * QUAD_TO / CUBIC_TO (if present) → RoboNotSupported.
 * OOM → RoboOutOfMemory.
 */
Result GfxPathTransformed(const GfxPath *src,
                                        const GfxAffine *m,
                                        GfxPath **out);

/**
 * In-place transform of path geometry (same semantics as transformed).
 * On failure the path is left unchanged.
 */
Result GfxPathTransform(GfxPath *path,
                                      const GfxAffine *m);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_TRANSFORM_H */
