#include "RoboGraphics/RoboGraphicsFramebuffer.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_gfx_framebuffer {
    uint32_t refcount;
    robo_gfx_framebuffer_id_t id;
    robo_gfx_framebuffer_ownership_t ownership;
    uint32_t width;
    uint32_t height;
    robo_gfx_pixel_format_t format;
    uint8_t bytes_per_pixel;
    size_t stride_bytes;
    size_t byte_size;
    uint8_t *pixels;
    int pixels_owned;
};

static uint64_t g_framebuffer_id_seq = 1u;

void robo_gfx_framebuffer_desc_init(robo_gfx_framebuffer_desc_t *desc)
{
    if (!desc) {
        return;
    }
    memset(desc, 0, sizeof(*desc));
    desc->format = ROBO_GFX_FORMAT_RGBA8;
}

static robo_result_t validate_and_resolve(const robo_gfx_framebuffer_desc_t *desc,
                                          size_t *out_stride,
                                          uint8_t *out_bpp,
                                          size_t *out_bytes)
{
    size_t stride = 0u;
    size_t bytes = 0u;
    uint8_t bpp = 0u;
    robo_result_t res;

    if (!desc || !out_stride || !out_bpp || !out_bytes) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (desc->width == 0u || desc->height == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (desc->width > ROBO_GFX_MAX_SURFACE_DIM ||
        desc->height > ROBO_GFX_MAX_SURFACE_DIM) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_pixel_format_is_implemented(desc->format)) {
        if (robo_gfx_pixel_format_is_known(desc->format)) {
            return ROBO_NOT_SUPPORTED;
        }
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_pixel_format_bytes_per_pixel(desc->format, &bpp);
    if (res != ROBO_OK) {
        return res;
    }
    if (desc->stride_bytes == 0u) {
        res = robo_gfx_pixel_format_min_stride(desc->format, desc->width, &stride);
        if (res != ROBO_OK) {
            return res;
        }
    } else {
        res = robo_gfx_pixel_format_validate_stride(desc->format, desc->width,
                                                    desc->stride_bytes);
        if (res != ROBO_OK) {
            return res;
        }
        stride = desc->stride_bytes;
    }
    res = robo_gfx_pixel_format_buffer_bytes(desc->format, desc->width, desc->height,
                                             stride, &bytes);
    if (res != ROBO_OK) {
        return res;
    }
    *out_stride = stride;
    *out_bpp = bpp;
    *out_bytes = bytes;
    return ROBO_OK;
}

static robo_gfx_framebuffer_t *alloc_fb_object(void)
{
    robo_gfx_framebuffer_t *fb =
        (robo_gfx_framebuffer_t *)robo_calloc(1, sizeof(*fb));
    if (!fb) {
        return NULL;
    }
    fb->refcount = 1u;
    fb->id = g_framebuffer_id_seq++;
    if (fb->id == ROBO_GFX_FRAMEBUFFER_ID_NONE) {
        fb->id = g_framebuffer_id_seq++;
    }
    return fb;
}

robo_result_t robo_gfx_framebuffer_create(const robo_gfx_framebuffer_desc_t *desc,
                                          robo_gfx_framebuffer_t **out)
{
    robo_gfx_framebuffer_t *fb;
    size_t stride = 0u;
    size_t bytes = 0u;
    uint8_t bpp = 0u;
    robo_result_t res;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    res = validate_and_resolve(desc, &stride, &bpp, &bytes);
    if (res != ROBO_OK) {
        return res;
    }
    fb = alloc_fb_object();
    if (!fb) {
        return ROBO_OUT_OF_MEMORY;
    }
    fb->pixels = (uint8_t *)robo_calloc(1, bytes);
    if (!fb->pixels) {
        robo_free(fb);
        return ROBO_OUT_OF_MEMORY;
    }
    fb->ownership = ROBO_GFX_FRAMEBUFFER_OWNED;
    fb->width = desc->width;
    fb->height = desc->height;
    fb->format = desc->format;
    fb->bytes_per_pixel = bpp;
    fb->stride_bytes = stride;
    fb->byte_size = bytes;
    fb->pixels_owned = 1;
    *out = fb;
    return ROBO_OK;
}

robo_result_t robo_gfx_framebuffer_create_borrowed(
    const robo_gfx_framebuffer_desc_t *desc,
    void *pixels,
    robo_gfx_framebuffer_t **out)
{
    robo_gfx_framebuffer_t *fb;
    size_t stride = 0u;
    size_t bytes = 0u;
    uint8_t bpp = 0u;
    robo_result_t res;

    if (!out || !pixels) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    res = validate_and_resolve(desc, &stride, &bpp, &bytes);
    if (res != ROBO_OK) {
        return res;
    }
    fb = alloc_fb_object();
    if (!fb) {
        return ROBO_OUT_OF_MEMORY;
    }
    fb->ownership = ROBO_GFX_FRAMEBUFFER_BORROWED;
    fb->width = desc->width;
    fb->height = desc->height;
    fb->format = desc->format;
    fb->bytes_per_pixel = bpp;
    fb->stride_bytes = stride;
    fb->byte_size = bytes;
    fb->pixels = (uint8_t *)pixels;
    fb->pixels_owned = 0;
    *out = fb;
    return ROBO_OK;
}

void robo_gfx_framebuffer_retain(robo_gfx_framebuffer_t *fb)
{
    if (!fb) {
        return;
    }
    fb->refcount++;
}

void robo_gfx_framebuffer_release(robo_gfx_framebuffer_t *fb)
{
    if (!fb) {
        return;
    }
    if (fb->refcount == 0u) {
        return;
    }
    fb->refcount--;
    if (fb->refcount != 0u) {
        return;
    }
    if (fb->pixels_owned && fb->pixels) {
        robo_free(fb->pixels);
    }
    robo_free(fb);
}

robo_gfx_framebuffer_id_t robo_gfx_framebuffer_id(const robo_gfx_framebuffer_t *fb)
{
    return fb ? fb->id : ROBO_GFX_FRAMEBUFFER_ID_NONE;
}

robo_gfx_framebuffer_ownership_t robo_gfx_framebuffer_ownership(
    const robo_gfx_framebuffer_t *fb)
{
    return fb ? fb->ownership : (robo_gfx_framebuffer_ownership_t)0;
}

uint32_t robo_gfx_framebuffer_width(const robo_gfx_framebuffer_t *fb)
{
    return fb ? fb->width : 0u;
}

uint32_t robo_gfx_framebuffer_height(const robo_gfx_framebuffer_t *fb)
{
    return fb ? fb->height : 0u;
}

robo_gfx_pixel_format_t robo_gfx_framebuffer_format(const robo_gfx_framebuffer_t *fb)
{
    return fb ? fb->format : ROBO_GFX_FORMAT_UNKNOWN;
}

uint8_t robo_gfx_framebuffer_bytes_per_pixel(const robo_gfx_framebuffer_t *fb)
{
    return fb ? fb->bytes_per_pixel : 0u;
}

size_t robo_gfx_framebuffer_stride(const robo_gfx_framebuffer_t *fb)
{
    return fb ? fb->stride_bytes : 0u;
}

size_t robo_gfx_framebuffer_byte_size(const robo_gfx_framebuffer_t *fb)
{
    return fb ? fb->byte_size : 0u;
}

uint8_t *robo_gfx_framebuffer_pixels(robo_gfx_framebuffer_t *fb)
{
    return fb ? fb->pixels : NULL;
}

const uint8_t *robo_gfx_framebuffer_pixels_const(const robo_gfx_framebuffer_t *fb)
{
    return fb ? fb->pixels : NULL;
}

robo_result_t robo_gfx_framebuffer_row(robo_gfx_framebuffer_t *fb,
                                       uint32_t y,
                                       uint8_t **out_row)
{
    if (!out_row) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_row = NULL;
    if (!fb || !fb->pixels || y >= fb->height) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_row = fb->pixels + (size_t)y * fb->stride_bytes;
    return ROBO_OK;
}

robo_result_t robo_gfx_framebuffer_row_const(const robo_gfx_framebuffer_t *fb,
                                             uint32_t y,
                                             const uint8_t **out_row)
{
    if (!out_row) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_row = NULL;
    if (!fb || !fb->pixels || y >= fb->height) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_row = fb->pixels + (size_t)y * fb->stride_bytes;
    return ROBO_OK;
}

robo_result_t robo_gfx_framebuffer_pixel_offset(const robo_gfx_framebuffer_t *fb,
                                                uint32_t x,
                                                uint32_t y,
                                                size_t *out_offset)
{
    if (!fb || !out_offset) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (x >= fb->width || y >= fb->height) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_gfx_pixel_format_pixel_offset(fb->format, fb->stride_bytes, x, y,
                                              out_offset);
}

robo_result_t robo_gfx_framebuffer_pixel(robo_gfx_framebuffer_t *fb,
                                         uint32_t x,
                                         uint32_t y,
                                         uint8_t **out_pixel)
{
    size_t off = 0u;
    robo_result_t res;

    if (!out_pixel) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_pixel = NULL;
    if (!fb || !fb->pixels) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_framebuffer_pixel_offset(fb, x, y, &off);
    if (res != ROBO_OK) {
        return res;
    }
    *out_pixel = fb->pixels + off;
    return ROBO_OK;
}

robo_result_t robo_gfx_framebuffer_pixel_const(const robo_gfx_framebuffer_t *fb,
                                               uint32_t x,
                                               uint32_t y,
                                               const uint8_t **out_pixel)
{
    size_t off = 0u;
    robo_result_t res;

    if (!out_pixel) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_pixel = NULL;
    if (!fb || !fb->pixels) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_framebuffer_pixel_offset(fb, x, y, &off);
    if (res != ROBO_OK) {
        return res;
    }
    *out_pixel = fb->pixels + off;
    return ROBO_OK;
}
