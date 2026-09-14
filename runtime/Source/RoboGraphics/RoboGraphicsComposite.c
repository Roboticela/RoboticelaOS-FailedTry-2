#include "RoboGraphics/RoboGraphicsComposite.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

uint8_t robo_gfx_mul_div255(uint8_t a, uint8_t b)
{
    return (uint8_t)(((uint16_t)a * (uint16_t)b + 127u) / 255u);
}

robo_gfx_color_rgba8_t robo_gfx_color_premultiply(robo_gfx_color_rgba8_t straight)
{
    robo_gfx_color_rgba8_t p;

    if (straight.a == 0u) {
        return robo_gfx_rgba8(0u, 0u, 0u, 0u);
    }
    if (straight.a == 255u) {
        return straight;
    }
    p.r = robo_gfx_mul_div255(straight.r, straight.a);
    p.g = robo_gfx_mul_div255(straight.g, straight.a);
    p.b = robo_gfx_mul_div255(straight.b, straight.a);
    p.a = straight.a;
    return p;
}

robo_gfx_color_rgba8_t robo_gfx_color_unpremultiply(robo_gfx_color_rgba8_t premul)
{
    robo_gfx_color_rgba8_t s;
    uint16_t a;

    if (premul.a == 0u) {
        return robo_gfx_rgba8(0u, 0u, 0u, 0u);
    }
    if (premul.a == 255u) {
        return premul;
    }
    a = (uint16_t)premul.a;
    /* round: (c * 255 + a/2) / a */
    s.r = (uint8_t)(((uint16_t)premul.r * 255u + (a / 2u)) / a);
    s.g = (uint8_t)(((uint16_t)premul.g * 255u + (a / 2u)) / a);
    s.b = (uint8_t)(((uint16_t)premul.b * 255u + (a / 2u)) / a);
    s.a = premul.a;
    return s;
}

robo_gfx_color_rgba8_t robo_gfx_composite_src_over(robo_gfx_color_rgba8_t src,
                                                   robo_gfx_color_rgba8_t dst)
{
    return robo_gfx_composite_src_over_opacity(src, dst, 255u);
}

robo_gfx_color_rgba8_t robo_gfx_composite_src_over_opacity(
    robo_gfx_color_rgba8_t src,
    robo_gfx_color_rgba8_t dst,
    uint8_t opacity)
{
    uint8_t sa;
    uint8_t inv_sa;
    robo_gfx_color_rgba8_t sp;
    robo_gfx_color_rgba8_t dp;
    robo_gfx_color_rgba8_t out_p;
    uint16_t tmp;

    if (opacity == 0u) {
        return dst;
    }

    sa = (opacity == 255u) ? src.a : robo_gfx_mul_div255(src.a, opacity);
    if (sa == 0u) {
        return dst;
    }
    if (sa == 255u) {
        return robo_gfx_rgba8(src.r, src.g, src.b, 255u);
    }

    /* Premultiply with effective source alpha (RGB stay straight until here). */
    sp.r = robo_gfx_mul_div255(src.r, sa);
    sp.g = robo_gfx_mul_div255(src.g, sa);
    sp.b = robo_gfx_mul_div255(src.b, sa);
    sp.a = sa;

    dp = robo_gfx_color_premultiply(dst);

    inv_sa = (uint8_t)(255u - sa);

    /*
     * Premultiplied source-over:
     *   out_p = src_p + dst_p * (1 - sa)
     * Keep intermediate precision in uint16 before /255.
     */
    tmp = (uint16_t)sp.a + (uint16_t)robo_gfx_mul_div255(dp.a, inv_sa);
    out_p.a = (tmp > 255u) ? 255u : (uint8_t)tmp;

    tmp = (uint16_t)sp.r + (uint16_t)robo_gfx_mul_div255(dp.r, inv_sa);
    out_p.r = (tmp > 255u) ? 255u : (uint8_t)tmp;

    tmp = (uint16_t)sp.g + (uint16_t)robo_gfx_mul_div255(dp.g, inv_sa);
    out_p.g = (tmp > 255u) ? 255u : (uint8_t)tmp;

    tmp = (uint16_t)sp.b + (uint16_t)robo_gfx_mul_div255(dp.b, inv_sa);
    out_p.b = (tmp > 255u) ? 255u : (uint8_t)tmp;

    return robo_gfx_color_unpremultiply(out_p);
}

static int surface_can_write(const robo_gfx_surface_t *surface)
{
    robo_gfx_surface_access_t a;

    if (!surface) {
        return 0;
    }
    a = robo_gfx_surface_access(surface);
    return a == ROBO_GFX_SURFACE_ACCESS_WRITE ||
           a == ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
}

static int surface_can_read(const robo_gfx_surface_t *surface)
{
    robo_gfx_surface_access_t a;

    if (!surface) {
        return 0;
    }
    a = robo_gfx_surface_access(surface);
    return a == ROBO_GFX_SURFACE_ACCESS_READ ||
           a == ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
}

robo_result_t robo_gfx_surface_composite_pixel(robo_gfx_surface_t *dst,
                                               uint32_t x,
                                               uint32_t y,
                                               robo_gfx_color_rgba8_t src,
                                               uint8_t opacity)
{
    robo_gfx_color_rgba8_t dst_c;
    robo_gfx_color_rgba8_t out;
    robo_result_t res;

    if (!dst || !surface_can_write(dst)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_pixel_format_is_implemented(robo_gfx_surface_format(dst))) {
        return ROBO_NOT_SUPPORTED;
    }
    res = robo_gfx_surface_get_pixel(dst, x, y, &dst_c);
    if (res != ROBO_OK) {
        return res;
    }
    out = robo_gfx_composite_src_over_opacity(src, dst_c, opacity);
    return robo_gfx_surface_set_pixel(dst, x, y, out);
}

/**
 * Clip composite geometry into unsigned in-bounds regions.
 * Returns 0 if nothing to draw; 1 and fills sx,sy,dx,dy,w,h on success.
 */
static int clip_composite_region(const robo_gfx_surface_t *src,
                                 robo_gfx_rect_t src_rect,
                                 const robo_gfx_surface_t *dst,
                                 robo_gfx_point_t dst_origin,
                                 uint32_t *out_sx,
                                 uint32_t *out_sy,
                                 uint32_t *out_dx,
                                 uint32_t *out_dy,
                                 uint32_t *out_w,
                                 uint32_t *out_h)
{
    robo_gfx_rect_t src_bounds;
    robo_gfx_rect_t dst_bounds;
    robo_gfx_rect_t src_clip;
    robo_gfx_rect_t dst_place;
    robo_gfx_rect_t dst_clip;
    int32_t adj_x;
    int32_t adj_y;

    if (robo_gfx_surface_pixel_bounds(src, &src_bounds) != ROBO_OK ||
        robo_gfx_surface_pixel_bounds(dst, &dst_bounds) != ROBO_OK) {
        return 0;
    }
    if (!robo_gfx_rect_is_valid(src_rect) || robo_gfx_rect_is_empty(src_rect)) {
        return 0;
    }
    if (robo_gfx_rect_intersect(src_rect, src_bounds, &src_clip) != ROBO_OK ||
        robo_gfx_rect_is_empty(src_clip)) {
        return 0;
    }

    adj_x = src_clip.x - src_rect.x;
    adj_y = src_clip.y - src_rect.y;
    dst_place = robo_gfx_rect(dst_origin.x + adj_x, dst_origin.y + adj_y,
                              src_clip.width, src_clip.height);
    if (!robo_gfx_rect_is_valid(dst_place) ||
        robo_gfx_rect_is_empty(dst_place)) {
        return 0;
    }
    if (robo_gfx_rect_intersect(dst_place, dst_bounds, &dst_clip) != ROBO_OK ||
        robo_gfx_rect_is_empty(dst_clip)) {
        return 0;
    }

    *out_sx = (uint32_t)(src_clip.x + (dst_clip.x - dst_place.x));
    *out_sy = (uint32_t)(src_clip.y + (dst_clip.y - dst_place.y));
    *out_dx = (uint32_t)dst_clip.x;
    *out_dy = (uint32_t)dst_clip.y;
    *out_w = (uint32_t)dst_clip.width;
    *out_h = (uint32_t)dst_clip.height;
    return 1;
}

static robo_result_t composite_loop(const robo_gfx_surface_t *src,
                                    uint32_t sx,
                                    uint32_t sy,
                                    robo_gfx_surface_t *dst,
                                    uint32_t dx,
                                    uint32_t dy,
                                    uint32_t w,
                                    uint32_t h,
                                    uint8_t opacity,
                                    const robo_gfx_color_rgba8_t *src_snapshot)
{
    uint32_t y;
    uint32_t x;
    robo_gfx_color_rgba8_t sc;
    robo_gfx_color_rgba8_t dc;
    robo_gfx_color_rgba8_t out;
    robo_result_t res;

    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x++) {
            if (src_snapshot) {
                sc = src_snapshot[(size_t)y * (size_t)w + (size_t)x];
            } else {
                res = robo_gfx_surface_get_pixel(src, sx + x, sy + y, &sc);
                if (res != ROBO_OK) {
                    return res;
                }
            }
            res = robo_gfx_surface_get_pixel(dst, dx + x, dy + y, &dc);
            if (res != ROBO_OK) {
                return res;
            }
            out = robo_gfx_composite_src_over_opacity(sc, dc, opacity);
            res = robo_gfx_surface_set_pixel(dst, dx + x, dy + y, out);
            if (res != ROBO_OK) {
                return res;
            }
        }
    }
    return ROBO_OK;
}

robo_result_t robo_gfx_surface_composite_rect(const robo_gfx_surface_t *src,
                                              robo_gfx_rect_t src_rect,
                                              robo_gfx_surface_t *dst,
                                              robo_gfx_point_t dst_origin,
                                              uint8_t opacity)
{
    uint32_t sx, sy, dx, dy, w, h;
    const uint8_t *src_px;
    uint8_t *dst_px;
    int same_storage;
    robo_gfx_color_rgba8_t *snapshot = NULL;
    size_t n;
    size_t i;
    uint32_t y;
    uint32_t x;
    robo_result_t res;

    if (!src || !dst || !surface_can_write(dst) || !surface_can_read(src)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_pixel_format_is_implemented(robo_gfx_surface_format(src)) ||
        !robo_gfx_pixel_format_is_implemented(robo_gfx_surface_format(dst))) {
        return ROBO_NOT_SUPPORTED;
    }
    if (!robo_gfx_rect_is_valid(src_rect)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_rect_is_empty(src_rect)) {
        return ROBO_OK;
    }
    if (!clip_composite_region(src, src_rect, dst, dst_origin, &sx, &sy, &dx,
                               &dy, &w, &h)) {
        return ROBO_OK;
    }

    src_px = robo_gfx_surface_pixels_const(src);
    dst_px = robo_gfx_surface_pixels(dst);
    same_storage = (src_px != NULL && dst_px != NULL &&
                    src_px == (const uint8_t *)dst_px);

    if (same_storage) {
        if (w == 0u || h == 0u) {
            return ROBO_OK;
        }
        if (h > (SIZE_MAX / (size_t)w)) {
            return ROBO_INVALID_ARGUMENT;
        }
        n = (size_t)w * (size_t)h;
        snapshot = (robo_gfx_color_rgba8_t *)robo_calloc(
            n, sizeof(robo_gfx_color_rgba8_t));
        if (!snapshot) {
            return ROBO_OUT_OF_MEMORY;
        }
        i = 0u;
        for (y = 0u; y < h; y++) {
            for (x = 0u; x < w; x++) {
                res = robo_gfx_surface_get_pixel(src, sx + x, sy + y,
                                                 &snapshot[i]);
                if (res != ROBO_OK) {
                    robo_free(snapshot);
                    return res;
                }
                i++;
            }
        }
    }

    res = composite_loop(src, sx, sy, dst, dx, dy, w, h, opacity, snapshot);
    robo_free(snapshot);
    return res;
}

robo_result_t robo_gfx_surface_composite(const robo_gfx_surface_t *src,
                                         robo_gfx_surface_t *dst,
                                         robo_gfx_point_t dst_origin,
                                         uint8_t opacity)
{
    robo_gfx_rect_t full;
    robo_result_t res;

    if (!src) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_surface_pixel_bounds(src, &full);
    if (res != ROBO_OK) {
        return res;
    }
    return robo_gfx_surface_composite_rect(src, full, dst, dst_origin, opacity);
}
