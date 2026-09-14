#include "RoboGraphics/RoboGraphicsImage.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_gfx_image {
    uint32_t refcount;
    robo_gfx_image_id_t id;
    robo_gfx_image_kind_t kind;
    uint32_t width;
    uint32_t height;
    robo_gfx_pixel_format_t format;
    robo_gfx_surface_t *surface; /* retained when present */
};

static uint64_t g_image_id_seq = 1u;

void robo_gfx_image_desc_init(robo_gfx_image_desc_t *desc)
{
    if (!desc) {
        return;
    }
    memset(desc, 0, sizeof(*desc));
    desc->kind = ROBO_GFX_IMAGE_KIND_EMPTY;
    desc->format = ROBO_GFX_FORMAT_RGBA8;
}

robo_result_t robo_gfx_image_create_empty(const robo_gfx_image_desc_t *desc,
                                          robo_gfx_image_t **out)
{
    robo_gfx_image_t *img;

    if (!desc || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (desc->width == 0u || desc->height == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_pixel_format_is_known(desc->format)) {
        return ROBO_INVALID_ARGUMENT;
    }
    img = (robo_gfx_image_t *)robo_calloc(1, sizeof(*img));
    if (!img) {
        return ROBO_OUT_OF_MEMORY;
    }
    img->refcount = 1u;
    img->id = g_image_id_seq++;
    if (img->id == ROBO_GFX_IMAGE_ID_NONE) {
        img->id = g_image_id_seq++;
    }
    img->kind = ROBO_GFX_IMAGE_KIND_EMPTY;
    img->width = desc->width;
    img->height = desc->height;
    img->format = desc->format;
    img->surface = NULL;
    *out = img;
    return ROBO_OK;
}

robo_result_t robo_gfx_image_create_from_surface(robo_gfx_surface_t *surface,
                                                 robo_gfx_image_t **out)
{
    robo_gfx_image_t *img;

    if (!surface || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    img = (robo_gfx_image_t *)robo_calloc(1, sizeof(*img));
    if (!img) {
        return ROBO_OUT_OF_MEMORY;
    }
    img->refcount = 1u;
    img->id = g_image_id_seq++;
    if (img->id == ROBO_GFX_IMAGE_ID_NONE) {
        img->id = g_image_id_seq++;
    }
    img->kind = ROBO_GFX_IMAGE_KIND_RASTER;
    img->width = robo_gfx_surface_width(surface);
    img->height = robo_gfx_surface_height(surface);
    img->format = robo_gfx_surface_format(surface);
    robo_gfx_surface_retain(surface);
    img->surface = surface;
    *out = img;
    return ROBO_OK;
}

void robo_gfx_image_retain(robo_gfx_image_t *image)
{
    if (!image) {
        return;
    }
    image->refcount++;
}

void robo_gfx_image_release(robo_gfx_image_t *image)
{
    if (!image) {
        return;
    }
    if (image->refcount == 0u) {
        return;
    }
    image->refcount--;
    if (image->refcount != 0u) {
        return;
    }
    if (image->surface) {
        robo_gfx_surface_release(image->surface);
    }
    robo_free(image);
}

robo_gfx_image_id_t robo_gfx_image_id(const robo_gfx_image_t *image)
{
    return image ? image->id : ROBO_GFX_IMAGE_ID_NONE;
}

robo_gfx_image_kind_t robo_gfx_image_kind(const robo_gfx_image_t *image)
{
    return image ? image->kind : (robo_gfx_image_kind_t)0;
}

uint32_t robo_gfx_image_width(const robo_gfx_image_t *image)
{
    return image ? image->width : 0u;
}

uint32_t robo_gfx_image_height(const robo_gfx_image_t *image)
{
    return image ? image->height : 0u;
}

robo_gfx_pixel_format_t robo_gfx_image_format(const robo_gfx_image_t *image)
{
    return image ? image->format : ROBO_GFX_FORMAT_UNKNOWN;
}

robo_gfx_surface_t *robo_gfx_image_surface(robo_gfx_image_t *image)
{
    return image ? image->surface : NULL;
}
