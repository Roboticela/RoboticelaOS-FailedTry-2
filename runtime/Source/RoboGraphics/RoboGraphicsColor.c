#include "RoboGraphics/RoboGraphicsColor.h"

robo_gfx_color_rgba8_t robo_gfx_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    robo_gfx_color_rgba8_t c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

robo_gfx_color_rgba8_t robo_gfx_rgb8(uint8_t r, uint8_t g, uint8_t b)
{
    return robo_gfx_rgba8(r, g, b, 255u);
}

robo_gfx_color_rgba_f_t robo_gfx_rgba_f(float r, float g, float b, float a)
{
    robo_gfx_color_rgba_f_t c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

int robo_gfx_color_rgba8_is_opaque(robo_gfx_color_rgba8_t c)
{
    return c.a == 255u;
}

int robo_gfx_color_rgba8_is_transparent(robo_gfx_color_rgba8_t c)
{
    return c.a == 0u;
}

static uint8_t clamp_u8_from_f(float v)
{
    if (v <= 0.0f) {
        return 0u;
    }
    if (v >= 1.0f) {
        return 255u;
    }
    return (uint8_t)(v * 255.0f + 0.5f);
}

robo_result_t robo_gfx_color_rgba8_to_f(robo_gfx_color_rgba8_t in,
                                        robo_gfx_color_rgba_f_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->r = (float)in.r / 255.0f;
    out->g = (float)in.g / 255.0f;
    out->b = (float)in.b / 255.0f;
    out->a = (float)in.a / 255.0f;
    return ROBO_OK;
}

robo_result_t robo_gfx_color_rgba_f_to_8(robo_gfx_color_rgba_f_t in,
                                         robo_gfx_color_rgba8_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->r = clamp_u8_from_f(in.r);
    out->g = clamp_u8_from_f(in.g);
    out->b = clamp_u8_from_f(in.b);
    out->a = clamp_u8_from_f(in.a);
    return ROBO_OK;
}

uint32_t robo_gfx_color_rgba8_pack_argb(robo_gfx_color_rgba8_t c)
{
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) |
           (uint32_t)c.b;
}

robo_gfx_color_rgba8_t robo_gfx_color_rgba8_unpack_argb(uint32_t argb)
{
    return robo_gfx_rgba8((uint8_t)((argb >> 16) & 0xFFu),
                          (uint8_t)((argb >> 8) & 0xFFu), (uint8_t)(argb & 0xFFu),
                          (uint8_t)((argb >> 24) & 0xFFu));
}

uint8_t robo_gfx_color_rgba8_luminance8(robo_gfx_color_rgba8_t c)
{
    return (uint8_t)(((uint32_t)c.r * 30u + (uint32_t)c.g * 59u + (uint32_t)c.b * 11u) /
                     100u);
}
