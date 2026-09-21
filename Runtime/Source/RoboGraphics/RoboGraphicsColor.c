#include "RoboGraphics/RoboGraphicsColor.h"

GfxColorRgba8 GfxRgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    GfxColorRgba8 c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

GfxColorRgba8 GfxRgb8(uint8_t r, uint8_t g, uint8_t b)
{
    return GfxRgba8(r, g, b, 255u);
}

GfxColorRgbaF GfxRgbaF(float r, float g, float b, float a)
{
    GfxColorRgbaF c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

int GfxColorRgba8IsOpaque(GfxColorRgba8 c)
{
    return c.a == 255u;
}

int GfxColorRgba8IsTransparent(GfxColorRgba8 c)
{
    return c.a == 0u;
}

static uint8_t ClampU8FromF(float v)
{
    if (v <= 0.0f) {
        return 0u;
    }
    if (v >= 1.0f) {
        return 255u;
    }
    return (uint8_t)(v * 255.0f + 0.5f);
}

Result GfxColorRgba8ToF(GfxColorRgba8 in,
                                        GfxColorRgbaF *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->r = (float)in.r / 255.0f;
    out->g = (float)in.g / 255.0f;
    out->b = (float)in.b / 255.0f;
    out->a = (float)in.a / 255.0f;
    return RoboOk;
}

Result GfxColorRgbaFTo8(GfxColorRgbaF in,
                                         GfxColorRgba8 *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->r = ClampU8FromF(in.r);
    out->g = ClampU8FromF(in.g);
    out->b = ClampU8FromF(in.b);
    out->a = ClampU8FromF(in.a);
    return RoboOk;
}

uint32_t GfxColorRgba8PackArgb(GfxColorRgba8 c)
{
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) |
           (uint32_t)c.b;
}

GfxColorRgba8 GfxColorRgba8UnpackArgb(uint32_t argb)
{
    return GfxRgba8((uint8_t)((argb >> 16) & 0xFFu),
                          (uint8_t)((argb >> 8) & 0xFFu), (uint8_t)(argb & 0xFFu),
                          (uint8_t)((argb >> 24) & 0xFFu));
}

uint8_t GfxColorRgba8Luminance8(GfxColorRgba8 c)
{
    return (uint8_t)(((uint32_t)c.r * 30u + (uint32_t)c.g * 59u + (uint32_t)c.b * 11u) /
                     100u);
}
