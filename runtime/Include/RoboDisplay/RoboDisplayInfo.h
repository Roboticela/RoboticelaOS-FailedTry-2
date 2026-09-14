/*
 * RoboDisplay info model (Goal 7.3).
 *
 * Inspired by Chromium display::Display concepts (bounds/work_area/scale)
 * as technical reference only — no Chromium dependency.
 *
 * Geometry uses RoboGraphics rects: top-left origin, half-open, signed
 * coordinates (multi-monitor origins may be negative).
 *
 * Logical bounds are in density-independent pixels (DIP).
 * resolution_*_px are physical pixels of the current mode.
 * scale: approximately physical ≈ logical * scale.
 */

#ifndef ROBO_DISPLAY_INFO_H
#define ROBO_DISPLAY_INFO_H

#include "RoboBase/RoboError.h"
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
    float refresh_hz;                 /* 0 = unknown */
    float scale;                      /* 0 = unknown; else e.g. 1.0, 1.5, 2.0 */
    float dpi_x;                      /* 0 = unknown */
    float dpi_y;
    robo_display_orientation_t orientation;
    int is_primary;
    robo_display_connection_t connection;
    int32_t physical_width_mm;  /* 0 = unknown */
    int32_t physical_height_mm;
} robo_display_info_t;

void robo_display_info_init(robo_display_info_t *info);

void robo_display_mode_init(robo_display_mode_t *mode);

/** Validate refresh_hz: NaN/negative → invalid; 0 allowed as unknown. */
robo_result_t robo_display_refresh_hz_validate(float hz);

/**
 * Approximate physical size from logical size and scale.
 * scale<=0 → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_display_logical_to_physical(int32_t logical,
                                               float scale,
                                               int32_t *out_physical);

robo_result_t robo_display_physical_to_logical(int32_t physical,
                                               float scale,
                                               int32_t *out_logical);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_INFO_H */
