/*
 * RoboDisplay scale / DPI foundation (Goal 7.8).
 *
 * Canonical model (Chromium display::Display is technical reference only):
 *
 *   logical (DIP)  ×  scale  ≈  physical pixels
 *
 * Rounding: nearest integer; ties away from zero (deterministic).
 * scale == 0 or dpi == 0 means unavailable (not “zero DPI”).
 *
 * Do not assume 96 is the physical DPI of a device. ROBO_DISPLAY_DPI_REFERENCE
 * is only the CSS/Windows reference used to derive effective scale from
 * effective DPI when a backend reports DPI but not scale.
 *
 * Full DPI-change events and DPI-aware rasterization are deferred.
 */

#ifndef ROBO_DISPLAY_SCALE_H
#define ROBO_DISPLAY_SCALE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Minimum accepted known scale (inclusive). */
#define ROBO_DISPLAY_SCALE_MIN 0.25f
/** Maximum accepted known scale (inclusive). */
#define ROBO_DISPLAY_SCALE_MAX 8.0f

/**
 * Reference DPI for effective-scale derivation (Windows/CSS).
 * Not a claim about physical monitor PPI.
 */
#define ROBO_DISPLAY_DPI_REFERENCE 96.0f

/** Minimum accepted known DPI (inclusive). */
#define ROBO_DISPLAY_DPI_MIN 1.0f
/** Maximum accepted known DPI (inclusive). */
#define ROBO_DISPLAY_DPI_MAX 1000.0f

typedef struct robo_display_dpi {
    float dpi_x; /* 0 = unavailable */
    float dpi_y; /* 0 = unavailable */
} robo_display_dpi_t;

void robo_display_dpi_init(robo_display_dpi_t *dpi);

/** Non-zero if @p v is finite (not NaN / ±Inf). */
int robo_display_float_is_finite(float v);

/** Non-zero if scale is known (validated range). Zero is unavailable. */
int robo_display_scale_is_known(float scale);

/**
 * Validate a known scale. Rejects 0, negative, NaN, Inf, out of
 * [ROBO_DISPLAY_SCALE_MIN, ROBO_DISPLAY_SCALE_MAX].
 */
robo_result_t robo_display_scale_validate(float scale);

/** Non-zero if DPI is unavailable (exactly 0). */
int robo_display_dpi_is_unavailable(float dpi);

/** Non-zero if DPI is a known valid value in range. */
int robo_display_dpi_is_known(float dpi);

/**
 * Validate DPI. Zero (unavailable) is OK. Rejects negative, NaN, Inf,
 * and values outside [ROBO_DISPLAY_DPI_MIN, ROBO_DISPLAY_DPI_MAX].
 */
robo_result_t robo_display_dpi_validate(float dpi);

/** Validate both axes (either may be unavailable independently). */
robo_result_t robo_display_dpi_pair_validate(const robo_display_dpi_t *dpi);

/**
 * Derive effective scale from effective DPI: dpi / ROBO_DISPLAY_DPI_REFERENCE.
 * Unavailable / invalid DPI → ROBO_INVALID_ARGUMENT.
 * Result must still pass scale_validate.
 */
robo_result_t robo_display_scale_from_dpi(float dpi, float *out_scale);

/**
 * Logical → physical scalar. Nearest; ties away from zero.
 * Invalid scale → ROBO_INVALID_ARGUMENT.
 * Overflow of int32 → ROBO_INVALID_ARGUMENT (no silent saturate).
 */
robo_result_t robo_display_logical_to_physical(int32_t logical,
                                               float scale,
                                               int32_t *out_physical);

robo_result_t robo_display_physical_to_logical(int32_t physical,
                                               float scale,
                                               int32_t *out_logical);

robo_result_t robo_display_size_logical_to_physical(int32_t logical_w,
                                                    int32_t logical_h,
                                                    float scale,
                                                    int32_t *out_phys_w,
                                                    int32_t *out_phys_h);

robo_result_t robo_display_size_physical_to_logical(int32_t phys_w,
                                                    int32_t phys_h,
                                                    float scale,
                                                    int32_t *out_logical_w,
                                                    int32_t *out_logical_h);

robo_result_t robo_display_point_logical_to_physical(int32_t logical_x,
                                                     int32_t logical_y,
                                                     float scale,
                                                     int32_t *out_phys_x,
                                                     int32_t *out_phys_y);

robo_result_t robo_display_point_physical_to_logical(int32_t phys_x,
                                                     int32_t phys_y,
                                                     float scale,
                                                     int32_t *out_logical_x,
                                                     int32_t *out_logical_y);

/**
 * Convert a rect by converting origin and size independently (documented
 * policy — not corner-based). Supports negative origins.
 */
robo_result_t robo_display_rect_logical_to_physical(const robo_gfx_rect_t *in,
                                                    float scale,
                                                    robo_gfx_rect_t *out);

robo_result_t robo_display_rect_physical_to_logical(const robo_gfx_rect_t *in,
                                                    float scale,
                                                    robo_gfx_rect_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_SCALE_H */
