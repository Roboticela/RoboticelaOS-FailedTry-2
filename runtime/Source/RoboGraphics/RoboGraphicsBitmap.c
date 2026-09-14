#include "RoboGraphics/RoboGraphicsBitmap.h"

#include "RoboGraphics/RoboGraphicsPixelFormat.h"

#include <stdint.h>
#include <string.h>

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

static int rect_fully_inside_u32(robo_gfx_rect_t rect,
                                 uint32_t surf_w,
                                 uint32_t surf_h)
{
    uint32_t rx;
    uint32_t ry;
    uint32_t rw;
    uint32_t rh;
    uint32_t right;
    uint32_t bottom;

    if (rect.width < 0 || rect.height < 0) {
        return 0;
    }
    if (rect.x < 0 || rect.y < 0) {
        return 0;
    }
    rx = (uint32_t)rect.x;
    ry = (uint32_t)rect.y;
    rw = (uint32_t)rect.width;
    rh = (uint32_t)rect.height;
    if (rx > surf_w || ry > surf_h) {
        return 0;
    }
    if (rw > surf_w - rx) {
        return 0;
    }
    if (rh > surf_h - ry) {
        return 0;
    }
    right = rx + rw;
    bottom = ry + rh;
    (void)right;
    (void)bottom;
    return 1;
}

static robo_result_t pack_color(robo_gfx_pixel_format_t format,
                                robo_gfx_color_rgba8_t color,
                                uint8_t *packed,
                                size_t packed_cap,
                                uint8_t *out_bpp)
{
    uint8_t bpp = 0u;
    robo_result_t res;

    res = robo_gfx_pixel_format_bytes_per_pixel(format, &bpp);
    if (res != ROBO_OK) {
        return res;
    }
    if (bpp == 0u || (size_t)bpp > packed_cap) {
        return ROBO_NOT_SUPPORTED;
    }
    res = robo_gfx_pixel_pack(format, color, packed, packed_cap);
    if (res != ROBO_OK) {
        return res;
    }
    *out_bpp = bpp;
    return ROBO_OK;
}

static int packed_bytes_uniform(const uint8_t *packed, uint8_t bpp)
{
    uint8_t i;
    for (i = 1u; i < bpp; i++) {
        if (packed[i] != packed[0]) {
            return 0;
        }
    }
    return 1;
}

static void fill_row_pattern(uint8_t *row,
                             uint32_t width,
                             const uint8_t *packed,
                             uint8_t bpp)
{
    uint32_t x;
    if (bpp == 1u) {
        memset(row, (int)packed[0], (size_t)width);
        return;
    }
    if (packed_bytes_uniform(packed, bpp)) {
        memset(row, (int)packed[0], (size_t)width * (size_t)bpp);
        return;
    }
    for (x = 0u; x < width; x++) {
        memcpy(row + (size_t)x * (size_t)bpp, packed, bpp);
    }
}

robo_result_t robo_gfx_surface_get_pixel(const robo_gfx_surface_t *surface,
                                         uint32_t x,
                                         uint32_t y,
                                         robo_gfx_color_rgba8_t *out_color)
{
    const uint8_t *px = NULL;
    uint8_t bpp;
    robo_result_t res;

    if (!out_color) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out_color, 0, sizeof(*out_color));
    if (!surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_surface_pixel_const(surface, x, y, &px);
    if (res != ROBO_OK) {
        return res;
    }
    bpp = robo_gfx_surface_bytes_per_pixel(surface);
    return robo_gfx_pixel_unpack(robo_gfx_surface_format(surface), px, bpp,
                                 out_color);
}

robo_result_t robo_gfx_surface_set_pixel(robo_gfx_surface_t *surface,
                                         uint32_t x,
                                         uint32_t y,
                                         robo_gfx_color_rgba8_t color)
{
    uint8_t *px = NULL;
    uint8_t packed[8];
    uint8_t bpp = 0u;
    robo_result_t res;

    if (!surface || !surface_can_write(surface)) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = pack_color(robo_gfx_surface_format(surface), color, packed, sizeof(packed),
                     &bpp);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_gfx_surface_pixel(surface, x, y, &px);
    if (res != ROBO_OK) {
        return res;
    }
    memcpy(px, packed, bpp);
    return ROBO_OK;
}

robo_result_t robo_gfx_surface_fill_rect(robo_gfx_surface_t *surface,
                                         robo_gfx_rect_t rect,
                                         robo_gfx_color_rgba8_t color)
{
    uint8_t packed[8];
    uint8_t bpp = 0u;
    uint32_t y;
    uint32_t x0;
    uint32_t y0;
    uint32_t w;
    uint32_t h;
    uint8_t *row = NULL;
    robo_result_t res;

    if (!surface || !surface_can_write(surface)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_rect_is_valid(rect)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_rect_is_empty(rect)) {
        return ROBO_OK;
    }
    if (!rect_fully_inside_u32(rect, robo_gfx_surface_width(surface),
                               robo_gfx_surface_height(surface))) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = pack_color(robo_gfx_surface_format(surface), color, packed, sizeof(packed),
                     &bpp);
    if (res != ROBO_OK) {
        return res;
    }

    x0 = (uint32_t)rect.x;
    y0 = (uint32_t)rect.y;
    w = (uint32_t)rect.width;
    h = (uint32_t)rect.height;
    for (y = 0u; y < h; y++) {
        res = robo_gfx_surface_row_checked(surface, y0 + y, &row);
        if (res != ROBO_OK) {
            return res;
        }
        fill_row_pattern(row + (size_t)x0 * (size_t)bpp, w, packed, bpp);
    }
    return ROBO_OK;
}

robo_result_t robo_gfx_surface_clear(robo_gfx_surface_t *surface,
                                     robo_gfx_color_rgba8_t color)
{
    robo_gfx_rect_t full;

    if (!surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_surface_width(surface) > (uint32_t)INT32_MAX ||
        robo_gfx_surface_height(surface) > (uint32_t)INT32_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    full = robo_gfx_rect(0, 0, (int32_t)robo_gfx_surface_width(surface),
                         (int32_t)robo_gfx_surface_height(surface));
    return robo_gfx_surface_fill_rect(surface, full, color);
}

static robo_result_t copy_rect_same_format(const robo_gfx_surface_t *src,
                                           uint32_t sx,
                                           uint32_t sy,
                                           uint32_t w,
                                           uint32_t h,
                                           robo_gfx_surface_t *dst,
                                           uint32_t dx,
                                           uint32_t dy)
{
    uint8_t bpp = robo_gfx_surface_bytes_per_pixel(src);
    size_t row_bytes = (size_t)w * (size_t)bpp;
    const uint8_t *src_base = robo_gfx_surface_pixels_const(src);
    uint8_t *dst_base = robo_gfx_surface_pixels(dst);
    size_t src_stride = robo_gfx_surface_stride(src);
    size_t dst_stride = robo_gfx_surface_stride(dst);
    int same_storage;
    uint32_t y;

    if (!src_base || !dst_base || bpp == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (w > (SIZE_MAX / (size_t)bpp)) {
        return ROBO_INVALID_ARGUMENT;
    }

    same_storage = (src_base == (const uint8_t *)dst_base);

    if (same_storage && sx == dx && sy == dy) {
        return ROBO_OK;
    }

    if (!same_storage) {
        for (y = 0u; y < h; y++) {
            memcpy(dst_base + (size_t)(dy + y) * dst_stride + (size_t)dx * bpp,
                   src_base + (size_t)(sy + y) * src_stride + (size_t)sx * bpp,
                   row_bytes);
        }
        return ROBO_OK;
    }

    /* Overlapping same-surface copy (Chromium ScrollCanvas-style direction). */
    if (dy > sy) {
        for (y = h; y > 0u; y--) {
            uint32_t yi = y - 1u;
            memmove(dst_base + (size_t)(dy + yi) * dst_stride + (size_t)dx * bpp,
                    src_base + (size_t)(sy + yi) * src_stride + (size_t)sx * bpp,
                    row_bytes);
        }
    } else {
        for (y = 0u; y < h; y++) {
            memmove(dst_base + (size_t)(dy + y) * dst_stride + (size_t)dx * bpp,
                    src_base + (size_t)(sy + y) * src_stride + (size_t)sx * bpp,
                    row_bytes);
        }
    }
    return ROBO_OK;
}

static robo_result_t copy_rect_convert(const robo_gfx_surface_t *src,
                                       uint32_t sx,
                                       uint32_t sy,
                                       uint32_t w,
                                       uint32_t h,
                                       robo_gfx_surface_t *dst,
                                       uint32_t dx,
                                       uint32_t dy)
{
    uint32_t y;
    uint32_t x;
    const uint8_t *sp;
    uint8_t *dp;
    uint8_t sbpp = robo_gfx_surface_bytes_per_pixel(src);
    uint8_t dbpp = robo_gfx_surface_bytes_per_pixel(dst);
    uint8_t tmp[8];
    robo_result_t res;

    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x++) {
            res = robo_gfx_surface_pixel_const(src, sx + x, sy + y, &sp);
            if (res != ROBO_OK) {
                return res;
            }
            res = robo_gfx_pixel_convert(robo_gfx_surface_format(src), sp, sbpp,
                                         robo_gfx_surface_format(dst), tmp,
                                         sizeof(tmp));
            if (res != ROBO_OK) {
                return res;
            }
            res = robo_gfx_surface_pixel(dst, dx + x, dy + y, &dp);
            if (res != ROBO_OK) {
                return res;
            }
            memcpy(dp, tmp, dbpp);
        }
    }
    return ROBO_OK;
}

robo_result_t robo_gfx_surface_copy_rect(const robo_gfx_surface_t *src,
                                         robo_gfx_rect_t src_rect,
                                         robo_gfx_surface_t *dst,
                                         robo_gfx_point_t dst_origin)
{
    uint32_t w;
    uint32_t h;
    uint32_t sx;
    uint32_t sy;
    uint32_t dx;
    uint32_t dy;
    robo_gfx_rect_t dst_rect;

    if (!src || !dst || !surface_can_write(dst)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_rect_is_valid(src_rect)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_rect_is_empty(src_rect)) {
        return ROBO_OK;
    }
    if (!rect_fully_inside_u32(src_rect, robo_gfx_surface_width(src),
                               robo_gfx_surface_height(src))) {
        return ROBO_INVALID_ARGUMENT;
    }

    w = (uint32_t)src_rect.width;
    h = (uint32_t)src_rect.height;
    sx = (uint32_t)src_rect.x;
    sy = (uint32_t)src_rect.y;

    if (dst_origin.x < 0 || dst_origin.y < 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (w > (uint32_t)INT32_MAX || h > (uint32_t)INT32_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    dst_rect = robo_gfx_rect(dst_origin.x, dst_origin.y, (int32_t)w, (int32_t)h);
    if (!rect_fully_inside_u32(dst_rect, robo_gfx_surface_width(dst),
                               robo_gfx_surface_height(dst))) {
        return ROBO_INVALID_ARGUMENT;
    }
    dx = (uint32_t)dst_origin.x;
    dy = (uint32_t)dst_origin.y;

    if (robo_gfx_surface_format(src) == robo_gfx_surface_format(dst)) {
        return copy_rect_same_format(src, sx, sy, w, h, dst, dx, dy);
    }
    return copy_rect_convert(src, sx, sy, w, h, dst, dx, dy);
}

robo_result_t robo_gfx_surface_copy(const robo_gfx_surface_t *src,
                                    robo_gfx_surface_t *dst)
{
    robo_gfx_rect_t full;

    if (!src || !dst) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_surface_width(src) != robo_gfx_surface_width(dst) ||
        robo_gfx_surface_height(src) != robo_gfx_surface_height(dst)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_surface_width(src) > (uint32_t)INT32_MAX ||
        robo_gfx_surface_height(src) > (uint32_t)INT32_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    full = robo_gfx_rect(0, 0, (int32_t)robo_gfx_surface_width(src),
                         (int32_t)robo_gfx_surface_height(src));
    return robo_gfx_surface_copy_rect(src, full, dst, robo_gfx_point(0, 0));
}
