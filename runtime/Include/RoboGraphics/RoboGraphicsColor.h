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

typedef struct robo_gfx_color_rgba8 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} robo_gfx_color_rgba8_t;

typedef struct robo_gfx_color_rgba_f {
    float r;
    float g;
    float b;
    float a;
} robo_gfx_color_rgba_f_t;

robo_gfx_color_rgba8_t robo_gfx_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
robo_gfx_color_rgba8_t robo_gfx_rgb8(uint8_t r, uint8_t g, uint8_t b); /* a=255 */
robo_gfx_color_rgba_f_t robo_gfx_rgba_f(float r, float g, float b, float a);

int robo_gfx_color_rgba8_is_opaque(robo_gfx_color_rgba8_t c);
int robo_gfx_color_rgba8_is_transparent(robo_gfx_color_rgba8_t c);

robo_result_t robo_gfx_color_rgba8_to_f(robo_gfx_color_rgba8_t in,
                                        robo_gfx_color_rgba_f_t *out);
robo_result_t robo_gfx_color_rgba_f_to_8(robo_gfx_color_rgba_f_t in,
                                         robo_gfx_color_rgba8_t *out);

/** Pack as 0xAARRGGBB (host endian independent construction). */
uint32_t robo_gfx_color_rgba8_pack_argb(robo_gfx_color_rgba8_t c);
robo_gfx_color_rgba8_t robo_gfx_color_rgba8_unpack_argb(uint32_t argb);

/**
 * Deterministic 8-bit luminance for Gray8 conversion.
 * Y = (30*R + 59*G + 11*B) / 100  (integer; BT.601-like weights).
 */
uint8_t robo_gfx_color_rgba8_luminance8(robo_gfx_color_rgba8_t c);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_COLOR_H */
