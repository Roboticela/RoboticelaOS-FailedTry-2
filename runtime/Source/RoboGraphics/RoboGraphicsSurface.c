#include "RoboGraphics/RoboGraphicsSurface.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_gfx_surface {
    uint32_t refcount;
    robo_gfx_surface_id_t id;
    robo_gfx_surface_ownership_t ownership;
    robo_gfx_surface_access_t access;
    uint32_t width;
    uint32_t height;
    robo_gfx_pixel_format_t format;
    size_t stride_bytes;
    uint8_t *pixels;
    int pixels_owned;
};

static uint64_t g_surface_id_seq = 1u;

static int format_cpu_creatable(robo_gfx_pixel_format_t format)
{
    return robo_gfx_pixel_format_is_implemented(format);
}

void robo_gfx_surface_desc_init(robo_gfx_surface_desc_t *desc)
{
    if (!desc) {
        return;
    }
    memset(desc, 0, sizeof(*desc));
    desc->format = ROBO_GFX_FORMAT_RGBA8;
    desc->access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
}

static robo_result_t validate_desc(const robo_gfx_surface_desc_t *desc, size_t *out_stride)
{
    size_t min_stride = 0u;
    robo_result_t res;

    if (!desc || !out_stride) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (desc->width == 0u || desc->height == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (desc->width > ROBO_GFX_MAX_SURFACE_DIM ||
        desc->height > ROBO_GFX_MAX_SURFACE_DIM) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_pixel_format_is_known(desc->format)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!format_cpu_creatable(desc->format)) {
        return ROBO_NOT_SUPPORTED;
    }
    res = robo_gfx_pixel_format_min_stride(desc->format, desc->width, &min_stride);
    if (res != ROBO_OK) {
        return res;
    }
    if (desc->stride_bytes == 0u) {
        *out_stride = min_stride;
    } else if (desc->stride_bytes < min_stride) {
        return ROBO_INVALID_ARGUMENT;
    } else {
        *out_stride = desc->stride_bytes;
    }
    if (desc->height > (SIZE_MAX / *out_stride)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

robo_result_t robo_gfx_surface_create(const robo_gfx_surface_desc_t *desc,
                                      robo_gfx_surface_t **out)
{
    robo_gfx_surface_t *s;
    size_t stride = 0u;
    size_t bytes;
    robo_result_t res;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    res = validate_desc(desc, &stride);
    if (res != ROBO_OK) {
        return res;
    }
    bytes = stride * (size_t)desc->height;
    s = (robo_gfx_surface_t *)robo_calloc(1, sizeof(*s));
    if (!s) {
        return ROBO_OUT_OF_MEMORY;
    }
    s->pixels = (uint8_t *)robo_calloc(1, bytes);
    if (!s->pixels) {
        robo_free(s);
        return ROBO_OUT_OF_MEMORY;
    }
    s->refcount = 1u;
    s->id = g_surface_id_seq++;
    if (s->id == ROBO_GFX_SURFACE_ID_NONE) {
        s->id = g_surface_id_seq++;
    }
    s->ownership = ROBO_GFX_SURFACE_OWNED;
    s->access = desc->access ? desc->access : ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    s->width = desc->width;
    s->height = desc->height;
    s->format = desc->format;
    s->stride_bytes = stride;
    s->pixels_owned = 1;
    *out = s;
    return ROBO_OK;
}

robo_result_t robo_gfx_surface_create_borrowed(const robo_gfx_surface_desc_t *desc,
                                               void *pixels,
                                               robo_gfx_surface_t **out)
{
    robo_gfx_surface_t *s;
    size_t stride = 0u;
    robo_result_t res;

    if (!out || !pixels) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    res = validate_desc(desc, &stride);
    if (res != ROBO_OK) {
        return res;
    }
    s = (robo_gfx_surface_t *)robo_calloc(1, sizeof(*s));
    if (!s) {
        return ROBO_OUT_OF_MEMORY;
    }
    s->refcount = 1u;
    s->id = g_surface_id_seq++;
    if (s->id == ROBO_GFX_SURFACE_ID_NONE) {
        s->id = g_surface_id_seq++;
    }
    s->ownership = ROBO_GFX_SURFACE_BORROWED;
    s->access = desc->access ? desc->access : ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    s->width = desc->width;
    s->height = desc->height;
    s->format = desc->format;
    s->stride_bytes = stride;
    s->pixels = (uint8_t *)pixels;
    s->pixels_owned = 0;
    *out = s;
    return ROBO_OK;
}

void robo_gfx_surface_retain(robo_gfx_surface_t *surface)
{
    if (!surface) {
        return;
    }
    surface->refcount++;
}

void robo_gfx_surface_release(robo_gfx_surface_t *surface)
{
    if (!surface) {
        return;
    }
    if (surface->refcount == 0u) {
        return;
    }
    surface->refcount--;
    if (surface->refcount != 0u) {
        return;
    }
    if (surface->pixels_owned && surface->pixels) {
        robo_free(surface->pixels);
    }
    robo_free(surface);
}

robo_gfx_surface_id_t robo_gfx_surface_id(const robo_gfx_surface_t *surface)
{
    return surface ? surface->id : ROBO_GFX_SURFACE_ID_NONE;
}

robo_gfx_surface_ownership_t robo_gfx_surface_ownership(
    const robo_gfx_surface_t *surface)
{
    return surface ? surface->ownership : (robo_gfx_surface_ownership_t)0;
}

uint32_t robo_gfx_surface_width(const robo_gfx_surface_t *surface)
{
    return surface ? surface->width : 0u;
}

uint32_t robo_gfx_surface_height(const robo_gfx_surface_t *surface)
{
    return surface ? surface->height : 0u;
}

robo_gfx_size_t robo_gfx_surface_size(const robo_gfx_surface_t *surface)
{
    if (!surface) {
        return robo_gfx_size(0, 0);
    }
    return robo_gfx_size((int32_t)surface->width, (int32_t)surface->height);
}

robo_gfx_pixel_format_t robo_gfx_surface_format(const robo_gfx_surface_t *surface)
{
    return surface ? surface->format : ROBO_GFX_FORMAT_UNKNOWN;
}

size_t robo_gfx_surface_stride(const robo_gfx_surface_t *surface)
{
    return surface ? surface->stride_bytes : 0u;
}

robo_gfx_surface_access_t robo_gfx_surface_access(const robo_gfx_surface_t *surface)
{
    return surface ? surface->access : (robo_gfx_surface_access_t)0;
}

uint8_t *robo_gfx_surface_pixels(robo_gfx_surface_t *surface)
{
    return surface ? surface->pixels : NULL;
}

const uint8_t *robo_gfx_surface_pixels_const(const robo_gfx_surface_t *surface)
{
    return surface ? surface->pixels : NULL;
}

size_t robo_gfx_surface_byte_size(const robo_gfx_surface_t *surface)
{
    if (!surface) {
        return 0u;
    }
    return surface->stride_bytes * (size_t)surface->height;
}

uint8_t *robo_gfx_surface_row(robo_gfx_surface_t *surface, uint32_t y)
{
    if (!surface || !surface->pixels || y >= surface->height) {
        return NULL;
    }
    return surface->pixels + (size_t)y * surface->stride_bytes;
}

robo_result_t robo_gfx_surface_clear_rgba8(robo_gfx_surface_t *surface,
                                           robo_gfx_color_rgba8_t color)
{
    uint32_t y;
    uint32_t x;
    uint8_t *row;
    robo_gfx_pixel_format_info_t info;

    if (!surface || !surface->pixels) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_pixel_format_info(surface->format, &info) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (surface->format == ROBO_GFX_FORMAT_RGBA8) {
        for (y = 0u; y < surface->height; y++) {
            row = robo_gfx_surface_row(surface, y);
            for (x = 0u; x < surface->width; x++) {
                uint8_t *px = row + (size_t)x * 4u;
                px[0] = color.r;
                px[1] = color.g;
                px[2] = color.b;
                px[3] = color.a;
            }
        }
        return ROBO_OK;
    }
    if (surface->format == ROBO_GFX_FORMAT_BGRA8) {
        for (y = 0u; y < surface->height; y++) {
            row = robo_gfx_surface_row(surface, y);
            for (x = 0u; x < surface->width; x++) {
                uint8_t *px = row + (size_t)x * 4u;
                px[0] = color.b;
                px[1] = color.g;
                px[2] = color.r;
                px[3] = color.a;
            }
        }
        return ROBO_OK;
    }
    if (surface->format == ROBO_GFX_FORMAT_GRAY8) {
        uint8_t g = robo_gfx_color_rgba8_luminance8(color);
        for (y = 0u; y < surface->height; y++) {
            row = robo_gfx_surface_row(surface, y);
            memset(row, (int)g, surface->width);
        }
        return ROBO_OK;
    }
    if (surface->format == ROBO_GFX_FORMAT_ALPHA8) {
        for (y = 0u; y < surface->height; y++) {
            row = robo_gfx_surface_row(surface, y);
            memset(row, (int)color.a, surface->width);
        }
        return ROBO_OK;
    }
    if (surface->format == ROBO_GFX_FORMAT_RGB8) {
        for (y = 0u; y < surface->height; y++) {
            row = robo_gfx_surface_row(surface, y);
            for (x = 0u; x < surface->width; x++) {
                uint8_t *px = row + (size_t)x * 3u;
                px[0] = color.r;
                px[1] = color.g;
                px[2] = color.b;
            }
        }
        return ROBO_OK;
    }
    return ROBO_NOT_SUPPORTED;
}
