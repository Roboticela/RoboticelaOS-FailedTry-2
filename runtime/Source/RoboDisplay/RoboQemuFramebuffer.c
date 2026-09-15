#include "RoboDisplay/RoboQemuFramebuffer.h"

#include "RoboBase/RoboLog.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

void robo_qemu_fb_info_init(robo_qemu_fb_info_t *info)
{
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
}

robo_gfx_pixel_format_t robo_qemu_fb_gfx_format(uint32_t pixfmt)
{
    if (pixfmt == ROBO_QEMU_PIX_BGRX) {
        return ROBO_GFX_FORMAT_BGRA8;
    }
    if (pixfmt == ROBO_QEMU_PIX_RGBX) {
        return ROBO_GFX_FORMAT_RGBA8;
    }
    return ROBO_GFX_FORMAT_UNKNOWN;
}

robo_result_t robo_qemu_fb_pixfmt_from_gfx(robo_gfx_pixel_format_t format,
                                           uint32_t *out_pixfmt)
{
    if (!out_pixfmt) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (format == ROBO_GFX_FORMAT_BGRA8) {
        *out_pixfmt = ROBO_QEMU_PIX_BGRX;
        return ROBO_OK;
    }
    if (format == ROBO_GFX_FORMAT_RGBA8) {
        *out_pixfmt = ROBO_QEMU_PIX_RGBX;
        return ROBO_OK;
    }
    return ROBO_NOT_SUPPORTED;
}

robo_result_t robo_qemu_fb_info_validate(const robo_qemu_fb_info_t *info)
{
    uint64_t need;
    uint32_t bpp_bytes;

    if (!info) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (info->headless) {
        return ROBO_OK;
    }
    if (!info->ready) {
        return ROBO_NOT_FOUND;
    }
    if (info->width == 0u || info->height == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (info->width > 8192u || info->height > 8192u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (info->bpp != 32u) {
        robo_log_warn("robo_qemu_fb: unsupported bpp=%u", (unsigned)info->bpp);
        return ROBO_NOT_SUPPORTED;
    }
    if (info->pixfmt != ROBO_QEMU_PIX_BGRX &&
        info->pixfmt != ROBO_QEMU_PIX_RGBX) {
        return ROBO_NOT_SUPPORTED;
    }
    bpp_bytes = info->bpp / 8u;
    if (info->pitch_bytes < info->width * bpp_bytes) {
        return ROBO_INVALID_ARGUMENT;
    }
    need = (uint64_t)info->pitch_bytes * (uint64_t)info->height;
    if (need == 0u || need > (uint64_t)SIZE_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (info->pixels) {
        if (info->byte_size < (size_t)need) {
            return ROBO_INVALID_ARGUMENT;
        }
    }
    return ROBO_OK;
}

static uint32_t pack_bgrx(uint8_t r, uint8_t g, uint8_t b)
{
    /* Memory B,G,R,X on LE as uint32 0xXXRRGGBB */
    return ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16) |
           (0xFFu << 24);
}

static uint32_t pack_rgbx(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r) | ((uint32_t)g << 8) | ((uint32_t)b << 16) |
           (0xFFu << 24);
}

static uint32_t convert_pixel(robo_gfx_pixel_format_t src_fmt, const uint8_t *p,
                              uint32_t dst_pixfmt)
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    if (src_fmt == ROBO_GFX_FORMAT_BGRA8) {
        b = p[0];
        g = p[1];
        r = p[2];
    } else if (src_fmt == ROBO_GFX_FORMAT_RGBA8) {
        r = p[0];
        g = p[1];
        b = p[2];
    } else if (src_fmt == ROBO_GFX_FORMAT_RGB8) {
        r = p[0];
        g = p[1];
        b = p[2];
    } else {
        return 0u;
    }
    if (dst_pixfmt == ROBO_QEMU_PIX_RGBX) {
        return pack_rgbx(r, g, b);
    }
    return pack_bgrx(r, g, b);
}

robo_result_t robo_qemu_fb_present_surface(const robo_qemu_fb_info_t *info,
                                           const robo_gfx_surface_t *surface,
                                           int32_t dest_x,
                                           int32_t dest_y)
{
    robo_result_t r;
    const uint8_t *src;
    uint8_t *dst_base;
    uint32_t sw;
    uint32_t sh;
    size_t src_stride;
    robo_gfx_pixel_format_t sfmt;
    uint8_t sbpp;
    uint32_t src_x = 0u;
    uint32_t src_y = 0u;
    uint32_t copy_w;
    uint32_t copy_h;
    uint32_t y;
    uint32_t dst_stride_px;

    r = robo_qemu_fb_info_validate(info);
    if (r != ROBO_OK) {
        return r;
    }
    if (!info->pixels || info->headless) {
        return ROBO_NOT_SUPPORTED;
    }
    if (!surface) {
        return ROBO_INVALID_ARGUMENT;
    }

    sw = robo_gfx_surface_width(surface);
    sh = robo_gfx_surface_height(surface);
    sfmt = robo_gfx_surface_format(surface);
    src_stride = robo_gfx_surface_stride(surface);
    src = robo_gfx_surface_pixels_const(surface);
    if (!src || sw == 0u || sh == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (sfmt != ROBO_GFX_FORMAT_BGRA8 && sfmt != ROBO_GFX_FORMAT_RGBA8 &&
        sfmt != ROBO_GFX_FORMAT_RGB8) {
        return ROBO_NOT_SUPPORTED;
    }
    if (robo_gfx_pixel_format_bytes_per_pixel(sfmt, &sbpp) != ROBO_OK ||
        sbpp == 0u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (src_stride < (size_t)sw * (size_t)sbpp) {
        return ROBO_INVALID_ARGUMENT;
    }

    if (dest_x < 0) {
        src_x = (uint32_t)(-dest_x);
        dest_x = 0;
    }
    if (dest_y < 0) {
        src_y = (uint32_t)(-dest_y);
        dest_y = 0;
    }
    if (src_x >= sw || src_y >= sh) {
        return ROBO_OK;
    }
    if ((uint32_t)dest_x >= info->width || (uint32_t)dest_y >= info->height) {
        return ROBO_OK;
    }
    copy_w = sw - src_x;
    copy_h = sh - src_y;
    if ((uint32_t)dest_x + copy_w > info->width) {
        copy_w = info->width - (uint32_t)dest_x;
    }
    if ((uint32_t)dest_y + copy_h > info->height) {
        copy_h = info->height - (uint32_t)dest_y;
    }

    dst_base = (uint8_t *)info->pixels;
    dst_stride_px = info->pitch_bytes / 4u;
    if (dst_stride_px == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }

    for (y = 0u; y < copy_h; y++) {
        const uint8_t *row =
            src + (size_t)(src_y + y) * src_stride + (size_t)src_x * sbpp;
        uint32_t *dst =
            (uint32_t *)(dst_base +
                         (size_t)((uint32_t)dest_y + y) * info->pitch_bytes) +
            (uint32_t)dest_x;
        uint32_t x;
        for (x = 0u; x < copy_w; x++) {
            dst[x] = convert_pixel(sfmt, row + (size_t)x * sbpp, info->pixfmt);
        }
    }
    robo_log_debug("robo_qemu_fb: present %ux%u → (%d,%d) copy=%ux%u",
                   (unsigned)sw, (unsigned)sh, (int)dest_x, (int)dest_y,
                   (unsigned)copy_w, (unsigned)copy_h);
    return ROBO_OK;
}

robo_result_t robo_qemu_fb_get_pixel(const robo_qemu_fb_info_t *info,
                                     uint32_t x,
                                     uint32_t y,
                                     uint32_t *out_px)
{
    const uint8_t *row;
    robo_result_t r;

    if (!out_px) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_qemu_fb_info_validate(info);
    if (r != ROBO_OK) {
        return r;
    }
    if (!info->pixels || x >= info->width || y >= info->height) {
        return ROBO_INVALID_ARGUMENT;
    }
    row = (const uint8_t *)info->pixels + (size_t)y * info->pitch_bytes;
    *out_px = ((const uint32_t *)row)[x];
    return ROBO_OK;
}
