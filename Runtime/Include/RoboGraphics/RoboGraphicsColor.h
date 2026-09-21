/*
 * RoboGraphics color foundation (Goal 6.1).
 *
 * Goal 6.1 stores unassociated (straight) alpha unless noted.
 * Goal 6.10 compositing uses the same straight storage; premul only as a
 * blend intermediate (see RoboGraphicsComposite.h / Compositing.md).
 * Full color management (sRGB ↔ linear, HDR, ICC) is later.
 *
 * Integer channels: 0 = black/transparent, 255 = full for 8-bit.
 * Float channels: [0, 1] nominal range (HDR may exceed later).
 */

#ifndef ROBO_GRAPHICS_COLOR_H
#define ROBO_GRAPHICS_COLOR_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GfxColorRgba8 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} GfxColorRgba8;

typedef struct GfxColorRgbaF {
    float r;
    float g;
    float b;
    float a;
} GfxColorRgbaF;

GfxColorRgba8 GfxRgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
GfxColorRgba8 GfxRgb8(uint8_t r, uint8_t g, uint8_t b); /* a=255 */
GfxColorRgbaF GfxRgbaF(float r, float g, float b, float a);

int GfxColorRgba8IsOpaque(GfxColorRgba8 c);
int GfxColorRgba8IsTransparent(GfxColorRgba8 c);

Result GfxColorRgba8ToF(GfxColorRgba8 in,
                                        GfxColorRgbaF *out);
Result GfxColorRgbaFTo8(GfxColorRgbaF in,
                                         GfxColorRgba8 *out);

/** Pack as 0xAARRGGBB (host endian independent construction). */
uint32_t GfxColorRgba8PackArgb(GfxColorRgba8 c);
GfxColorRgba8 GfxColorRgba8UnpackArgb(uint32_t argb);

/**
 * Deterministic 8-bit luminance for Gray8 conversion.
 * Y = (30*R + 59*G + 11*B) / 100  (integer; BT.601-like weights).
 */
uint8_t GfxColorRgba8Luminance8(GfxColorRgba8 c);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_COLOR_H */
