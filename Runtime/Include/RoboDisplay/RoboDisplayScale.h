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
 * Do not assume 96 is the physical DPI of a device. RoboDisplayDpiReference
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
#define RoboDisplayScaleMin 0.25f
/** Maximum accepted known scale (inclusive). */
#define RoboDisplayScaleMax 8.0f

/**
 * Reference DPI for effective-scale derivation (Windows/CSS).
 * Not a claim about physical monitor PPI.
 */
#define RoboDisplayDpiReference 96.0f

/** Minimum accepted known DPI (inclusive). */
#define RoboDisplayDpiMin 1.0f
/** Maximum accepted known DPI (inclusive). */
#define RoboDisplayDpiMax 1000.0f

typedef struct DisplayDpi {
    float dpiX; /* 0 = unavailable */
    float dpiY; /* 0 = unavailable */
} DisplayDpi;

void DisplayDpiInit(DisplayDpi *dpi);

/** Non-zero if @p v is finite (not NaN / ±Inf). */
int DisplayFloatIsFinite(float v);

/** Non-zero if scale is known (validated range). Zero is unavailable. */
int DisplayScaleIsKnown(float scale);

/**
 * Validate a known scale. Rejects 0, negative, NaN, Inf, out of
 * [RoboDisplayScaleMin, RoboDisplayScaleMax].
 */
Result DisplayScaleValidate(float scale);

/** Non-zero if DPI is unavailable (exactly 0). */
int DisplayDpiIsUnavailable(float dpi);

/** Non-zero if DPI is a known valid value in range. */
int DisplayDpiIsKnown(float dpi);

/**
 * Validate DPI. Zero (unavailable) is OK. Rejects negative, NaN, Inf,
 * and values outside [RoboDisplayDpiMin, RoboDisplayDpiMax].
 */
Result DisplayDpiValidate(float dpi);

/** Validate both axes (either may be unavailable independently). */
Result DisplayDpiPairValidate(const DisplayDpi *dpi);

/**
 * Derive effective scale from effective DPI: dpi / RoboDisplayDpiReference.
 * Unavailable / invalid DPI → RoboInvalidArgument.
 * Result must still pass scale_validate.
 */
Result DisplayScaleFromDpi(float dpi, float *outScale);

/**
 * Logical → physical scalar. Nearest; ties away from zero.
 * Invalid scale → RoboInvalidArgument.
 * Overflow of int32 → RoboInvalidArgument (no silent saturate).
 */
Result DisplayLogicalToPhysical(int32_t logical,
                                               float scale,
                                               int32_t *outPhysical);

Result DisplayPhysicalToLogical(int32_t physical,
                                               float scale,
                                               int32_t *outLogical);

Result DisplaySizeLogicalToPhysical(int32_t logicalW,
                                                    int32_t logicalH,
                                                    float scale,
                                                    int32_t *outPhysW,
                                                    int32_t *outPhysH);

Result DisplaySizePhysicalToLogical(int32_t physW,
                                                    int32_t physH,
                                                    float scale,
                                                    int32_t *outLogicalW,
                                                    int32_t *outLogicalH);

Result DisplayPointLogicalToPhysical(int32_t logicalX,
                                                     int32_t logicalY,
                                                     float scale,
                                                     int32_t *outPhysX,
                                                     int32_t *outPhysY);

Result DisplayPointPhysicalToLogical(int32_t physX,
                                                     int32_t physY,
                                                     float scale,
                                                     int32_t *outLogicalX,
                                                     int32_t *outLogicalY);

/**
 * Convert a rect by converting origin and size independently (documented
 * policy — not corner-based). Supports negative origins.
 */
Result DisplayRectLogicalToPhysical(const GfxRect *in,
                                                    float scale,
                                                    GfxRect *out);

Result DisplayRectPhysicalToLogical(const GfxRect *in,
                                                    float scale,
                                                    GfxRect *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_SCALE_H */
