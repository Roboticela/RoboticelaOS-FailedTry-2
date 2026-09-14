#include "RoboGraphics/RoboGraphicsSurface.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_gfx_surface {
    uint32_t refcount;
    robo_gfx_surface_id_t id;
    robo_gfx_surface_ownership_t ownership;
    robo_gfx_surface_access_t access;
    robo_gfx_framebuffer_t *fb; /* retained; CPU backing for Goal 6.4 */
    int mapped;
    robo_gfx_surface_access_t map_access;
};

static uint64_t g_surface_id_seq = 1u;

static int access_is_valid(robo_gfx_surface_access_t access)
{
    return access == ROBO_GFX_SURFACE_ACCESS_READ ||
           access == ROBO_GFX_SURFACE_ACCESS_WRITE ||
           access == ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
}

static int access_allows(robo_gfx_surface_access_t allowed,
                         robo_gfx_surface_access_t requested)
{
    if (!access_is_valid(allowed) || !access_is_valid(requested)) {
        return 0;
    }
    if (requested == ROBO_GFX_SURFACE_ACCESS_READ_WRITE) {
        return allowed == ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    }
    if (requested == ROBO_GFX_SURFACE_ACCESS_READ) {
        return allowed == ROBO_GFX_SURFACE_ACCESS_READ ||
               allowed == ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    }
    /* WRITE */
    return allowed == ROBO_GFX_SURFACE_ACCESS_WRITE ||
           allowed == ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
}

static int access_can_write(robo_gfx_surface_access_t access)
{
    return access == ROBO_GFX_SURFACE_ACCESS_WRITE ||
           access == ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
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

static robo_result_t resolve_access(const robo_gfx_surface_desc_t *desc,
                                    robo_gfx_surface_access_t *out_access)
{
    robo_gfx_surface_access_t access;

    if (!desc || !out_access) {
        return ROBO_INVALID_ARGUMENT;
    }
    access = desc->access ? desc->access : ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    if (!access_is_valid(access)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_access = access;
    return ROBO_OK;
}

static robo_gfx_surface_t *alloc_surface_object(void)
{
    robo_gfx_surface_t *s = (robo_gfx_surface_t *)robo_calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->refcount = 1u;
    s->id = g_surface_id_seq++;
    if (s->id == ROBO_GFX_SURFACE_ID_NONE) {
        s->id = g_surface_id_seq++;
    }
    return s;
}

robo_result_t robo_gfx_surface_create(const robo_gfx_surface_desc_t *desc,
                                      robo_gfx_surface_t **out)
{
    robo_gfx_surface_t *s;
    robo_gfx_framebuffer_desc_t fb_desc;
    robo_gfx_framebuffer_t *fb = NULL;
    robo_gfx_surface_access_t access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    robo_result_t res;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (!desc) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = resolve_access(desc, &access);
    if (res != ROBO_OK) {
        return res;
    }

    robo_gfx_framebuffer_desc_init(&fb_desc);
    fb_desc.width = desc->width;
    fb_desc.height = desc->height;
    fb_desc.format = desc->format;
    fb_desc.stride_bytes = desc->stride_bytes;
    res = robo_gfx_framebuffer_create(&fb_desc, &fb);
    if (res != ROBO_OK) {
        return res;
    }

    s = alloc_surface_object();
    if (!s) {
        robo_gfx_framebuffer_release(fb);
        return ROBO_OUT_OF_MEMORY;
    }
    s->ownership = ROBO_GFX_SURFACE_OWNED;
    s->access = access;
    s->fb = fb;
    *out = s;
    return ROBO_OK;
}

robo_result_t robo_gfx_surface_create_borrowed(const robo_gfx_surface_desc_t *desc,
                                               void *pixels,
                                               robo_gfx_surface_t **out)
{
    robo_gfx_surface_t *s;
    robo_gfx_framebuffer_desc_t fb_desc;
    robo_gfx_framebuffer_t *fb = NULL;
    robo_gfx_surface_access_t access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    robo_result_t res;

    if (!out || !pixels) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (!desc) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = resolve_access(desc, &access);
    if (res != ROBO_OK) {
        return res;
    }

    robo_gfx_framebuffer_desc_init(&fb_desc);
    fb_desc.width = desc->width;
    fb_desc.height = desc->height;
    fb_desc.format = desc->format;
    fb_desc.stride_bytes = desc->stride_bytes;
    res = robo_gfx_framebuffer_create_borrowed(&fb_desc, pixels, &fb);
    if (res != ROBO_OK) {
        return res;
    }

    s = alloc_surface_object();
    if (!s) {
        robo_gfx_framebuffer_release(fb);
        return ROBO_OUT_OF_MEMORY;
    }
    s->ownership = ROBO_GFX_SURFACE_BORROWED;
    s->access = access;
    s->fb = fb;
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
    /* Ending a mapping on final release is safe; pointers become invalid. */
    surface->mapped = 0;
    surface->map_access = (robo_gfx_surface_access_t)0;
    if (surface->fb) {
        robo_gfx_framebuffer_release(surface->fb);
        surface->fb = NULL;
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
    return surface && surface->fb ? robo_gfx_framebuffer_width(surface->fb) : 0u;
}

uint32_t robo_gfx_surface_height(const robo_gfx_surface_t *surface)
{
    return surface && surface->fb ? robo_gfx_framebuffer_height(surface->fb) : 0u;
}

robo_gfx_size_t robo_gfx_surface_size(const robo_gfx_surface_t *surface)
{
    if (!surface || !surface->fb) {
        return robo_gfx_size(0, 0);
    }
    return robo_gfx_size((int32_t)robo_gfx_framebuffer_width(surface->fb),
                         (int32_t)robo_gfx_framebuffer_height(surface->fb));
}

robo_gfx_pixel_format_t robo_gfx_surface_format(const robo_gfx_surface_t *surface)
{
    return surface && surface->fb ? robo_gfx_framebuffer_format(surface->fb)
                                  : ROBO_GFX_FORMAT_UNKNOWN;
}

uint8_t robo_gfx_surface_bytes_per_pixel(const robo_gfx_surface_t *surface)
{
    return surface && surface->fb ? robo_gfx_framebuffer_bytes_per_pixel(surface->fb)
                                  : 0u;
}

size_t robo_gfx_surface_stride(const robo_gfx_surface_t *surface)
{
    return surface && surface->fb ? robo_gfx_framebuffer_stride(surface->fb) : 0u;
}

size_t robo_gfx_surface_byte_size(const robo_gfx_surface_t *surface)
{
    return surface && surface->fb ? robo_gfx_framebuffer_byte_size(surface->fb) : 0u;
}

robo_gfx_surface_access_t robo_gfx_surface_access(const robo_gfx_surface_t *surface)
{
    return surface ? surface->access : (robo_gfx_surface_access_t)0;
}

int robo_gfx_surface_is_mapped(const robo_gfx_surface_t *surface)
{
    return surface && surface->mapped ? 1 : 0;
}

robo_result_t robo_gfx_surface_map(robo_gfx_surface_t *surface,
                                   robo_gfx_surface_access_t access,
                                   robo_gfx_surface_map_t *out)
{
    const uint8_t *base;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (!surface || !surface->fb) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (surface->mapped) {
        return ROBO_BUSY;
    }
    if (!access_allows(surface->access, access)) {
        return ROBO_INVALID_ARGUMENT;
    }
    base = robo_gfx_framebuffer_pixels_const(surface->fb);
    if (!base) {
        return ROBO_INVALID_ARGUMENT;
    }

    out->pixels_const = base;
    out->pixels = access_can_write(access)
                      ? robo_gfx_framebuffer_pixels(surface->fb)
                      : NULL;
    out->width = robo_gfx_framebuffer_width(surface->fb);
    out->height = robo_gfx_framebuffer_height(surface->fb);
    out->stride_bytes = robo_gfx_framebuffer_stride(surface->fb);
    out->byte_size = robo_gfx_framebuffer_byte_size(surface->fb);
    out->format = robo_gfx_framebuffer_format(surface->fb);
    out->access = access;

    surface->mapped = 1;
    surface->map_access = access;
    return ROBO_OK;
}

robo_result_t robo_gfx_surface_unmap(robo_gfx_surface_t *surface)
{
    if (!surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!surface->mapped) {
        return ROBO_INVALID_ARGUMENT;
    }
    surface->mapped = 0;
    surface->map_access = (robo_gfx_surface_access_t)0;
    return ROBO_OK;
}

uint8_t *robo_gfx_surface_pixels(robo_gfx_surface_t *surface)
{
    if (!surface || !surface->fb || !access_can_write(surface->access)) {
        return NULL;
    }
    return robo_gfx_framebuffer_pixels(surface->fb);
}

const uint8_t *robo_gfx_surface_pixels_const(const robo_gfx_surface_t *surface)
{
    if (!surface || !surface->fb) {
        return NULL;
    }
    /* CPU backing is addressable for inspection; map() still enforces access. */
    return robo_gfx_framebuffer_pixels_const(surface->fb);
}

uint8_t *robo_gfx_surface_row(robo_gfx_surface_t *surface, uint32_t y)
{
    uint8_t *row = NULL;
    if (robo_gfx_surface_row_checked(surface, y, &row) != ROBO_OK) {
        return NULL;
    }
    return row;
}

robo_result_t robo_gfx_surface_row_checked(robo_gfx_surface_t *surface,
                                           uint32_t y,
                                           uint8_t **out_row)
{
    if (!out_row) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_row = NULL;
    if (!surface || !surface->fb || !access_can_write(surface->access)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_gfx_framebuffer_row(surface->fb, y, out_row);
}

robo_result_t robo_gfx_surface_row_const(const robo_gfx_surface_t *surface,
                                         uint32_t y,
                                         const uint8_t **out_row)
{
    if (!out_row) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_row = NULL;
    if (!surface || !surface->fb) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_gfx_framebuffer_row_const(surface->fb, y, out_row);
}

robo_result_t robo_gfx_surface_pixel_offset(const robo_gfx_surface_t *surface,
                                            uint32_t x,
                                            uint32_t y,
                                            size_t *out_offset)
{
    if (!surface || !surface->fb) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_gfx_framebuffer_pixel_offset(surface->fb, x, y, out_offset);
}

robo_result_t robo_gfx_surface_pixel(robo_gfx_surface_t *surface,
                                     uint32_t x,
                                     uint32_t y,
                                     uint8_t **out_pixel)
{
    if (!out_pixel) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_pixel = NULL;
    if (!surface || !surface->fb || !access_can_write(surface->access)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_gfx_framebuffer_pixel(surface->fb, x, y, out_pixel);
}

robo_result_t robo_gfx_surface_pixel_const(const robo_gfx_surface_t *surface,
                                           uint32_t x,
                                           uint32_t y,
                                           const uint8_t **out_pixel)
{
    if (!out_pixel) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_pixel = NULL;
    if (!surface || !surface->fb) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_gfx_framebuffer_pixel_const(surface->fb, x, y, out_pixel);
}

robo_result_t robo_gfx_surface_clear_rgba8(robo_gfx_surface_t *surface,
                                           robo_gfx_color_rgba8_t color)
{
    return robo_gfx_surface_clear(surface, color);
}

robo_gfx_framebuffer_t *robo_gfx_surface_framebuffer(robo_gfx_surface_t *surface)
{
    return surface ? surface->fb : NULL;
}
