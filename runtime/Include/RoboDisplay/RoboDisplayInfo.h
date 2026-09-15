/*
 * RoboDisplay info model (Goals 7.3 / 7.8).
 *
 * Inspired by Chromium display::Display concepts (bounds/work_area/scale)
 * as technical reference only — no Chromium dependency.
 *
 * Geometry uses RoboGraphics rects: top-left origin, half-open, signed
 * coordinates (multi-monitor origins may be negative).
 *
 * Logical bounds are in density-independent pixels (DIP).
 * resolution_*_px are physical pixels of the current mode.
 * scale: physical ≈ logical * scale (0 = unavailable).
 * dpi_*: effective DPI (0 = unavailable). See RoboDisplayScale.h.
 */

#ifndef ROBO_DISPLAY_INFO_H
#define ROBO_DISPLAY_INFO_H

#include "RoboBase/RoboError.h"
#include "RoboDisplay/RoboDisplayScale.h"
#include "RoboDisplay/RoboDisplayTypes.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_display_mode {
    int32_t width_px;
    int32_t height_px;
    float refresh_hz; /* 0 = unknown; may be fractional e.g. 59.94 */
} robo_display_mode_t;

typedef struct robo_display_info {
    robo_display_id_t id;
    char name[ROBO_DISPLAY_NAME_MAX]; /* UTF-8; empty if unknown */
    robo_gfx_rect_t bounds;           /* logical DIP, screen space */
    robo_gfx_rect_t work_area;        /* logical DIP usable area */
    int32_t resolution_width_px;      /* physical; 0 = unknown */
    int32_t resolution_height_px;
    float refresh_hz; /* 0 = unknown */
    float scale;      /* 0 = unavailable; else e.g. 1.0, 1.5, 2.0 */
    float dpi_x;      /* 0 = unavailable */
    float dpi_y;
    robo_display_orientation_t orientation;
    int is_primary;
    robo_display_connection_t connection;
    int32_t physical_width_mm; /* 0 = unknown */
    int32_t physical_height_mm;
} robo_display_info_t;

void robo_display_info_init(robo_display_info_t *info);

void robo_display_mode_init(robo_display_mode_t *mode);

/** Validate refresh_hz: NaN/negative → invalid; 0 allowed as unknown. */
robo_result_t robo_display_refresh_hz_validate(float hz);

/**
 * Resolve effective scale for @p info:
 *   1. known info->scale
 *   2. else derive from dpi_x via ROBO_DISPLAY_DPI_REFERENCE
 *   3. else ROBO_NOT_FOUND (unavailable)
 */
robo_result_t robo_display_info_effective_scale(const robo_display_info_t *info,
                                                float *out_scale);

/** Copy dpi_x/dpi_y; either may be unavailable (0). */
robo_result_t robo_display_info_get_dpi(const robo_display_info_t *info,
                                        robo_display_dpi_t *out);

/**
 * Logical bounds → physical pixel rect using effective scale.
 * Unavailable scale → ROBO_NOT_FOUND.
 */
robo_result_t robo_display_info_physical_bounds(const robo_display_info_t *info,
                                                robo_gfx_rect_t *out);

robo_result_t robo_display_info_physical_work_area(
    const robo_display_info_t *info,
    robo_gfx_rect_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_INFO_H */
