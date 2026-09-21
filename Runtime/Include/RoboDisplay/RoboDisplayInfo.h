/*
 * RoboDisplay info model (Goals 7.3 / 7.8).
 *
 * Inspired by Chromium display::Display concepts (bounds/workArea/scale)
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

typedef struct DisplayMode {
    int32_t widthPx;
    int32_t heightPx;
    float refreshHz; /* 0 = unknown; may be fractional e.g. 59.94 */
} DisplayMode;

typedef struct DisplayInfo {
    DisplayId id;
    char name[RoboDisplayNameMax]; /* UTF-8; empty if unknown */
    GfxRect bounds;           /* logical DIP, screen space */
    GfxRect workArea;        /* logical DIP usable area */
    int32_t resolutionWidthPx;      /* physical; 0 = unknown */
    int32_t resolutionHeightPx;
    float refreshHz; /* 0 = unknown */
    float scale;      /* 0 = unavailable; else e.g. 1.0, 1.5, 2.0 */
    float dpiX;      /* 0 = unavailable */
    float dpiY;
    DisplayOrientation orientation;
    int isPrimary;
    DisplayConnection connection;
    int32_t physicalWidthMm; /* 0 = unknown */
    int32_t physicalHeightMm;
} DisplayInfo;

void DisplayInfoInit(DisplayInfo *info);

void DisplayModeInit(DisplayMode *mode);

/** Validate refreshHz: NaN/negative → invalid; 0 allowed as unknown. */
Result DisplayRefreshHzValidate(float hz);

/**
 * Resolve effective scale for @p info:
 *   1. known info->scale
 *   2. else derive from dpiX via RoboDisplayDpiReference
 *   3. else RoboNotFound (unavailable)
 */
Result DisplayInfoEffectiveScale(const DisplayInfo *info,
                                                float *outScale);

/** Copy dpiX/dpiY; either may be unavailable (0). */
Result DisplayInfoGetDpi(const DisplayInfo *info,
                                        DisplayDpi *out);

/**
 * Logical bounds → physical pixel rect using effective scale.
 * Unavailable scale → RoboNotFound.
 */
Result DisplayInfoPhysicalBounds(const DisplayInfo *info,
                                                GfxRect *out);

Result DisplayInfoPhysicalWorkArea(
    const DisplayInfo *info,
    GfxRect *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_INFO_H */
