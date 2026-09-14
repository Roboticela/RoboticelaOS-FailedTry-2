/*
 * RoboGraphics image abstraction (Goal 6.1).
 *
 * Images are format-agnostic containers. Decoding (PNG/JPEG/…) is later.
 * Goal 6.1: raster images backed by a surface, or metadata-only placeholders.
 */

#ifndef ROBO_GRAPHICS_IMAGE_H
#define ROBO_GRAPHICS_IMAGE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboGraphics/RoboGraphicsTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_gfx_image_kind {
    ROBO_GFX_IMAGE_KIND_RASTER = 1,
    ROBO_GFX_IMAGE_KIND_EXTERNAL = 2, /* metadata / future GPU / foreign buffer */
    ROBO_GFX_IMAGE_KIND_EMPTY = 3
} robo_gfx_image_kind_t;

typedef struct robo_gfx_image_desc {
    uint32_t width;
    uint32_t height;
    robo_gfx_pixel_format_t format;
    robo_gfx_image_kind_t kind;
} robo_gfx_image_desc_t;

typedef struct robo_gfx_image robo_gfx_image_t;

void robo_gfx_image_desc_init(robo_gfx_image_desc_t *desc);

/** Create an empty metadata image (no pixels). */
robo_result_t robo_gfx_image_create_empty(const robo_gfx_image_desc_t *desc,
                                          robo_gfx_image_t **out);

/** Create a raster image that retains @p surface. */
robo_result_t robo_gfx_image_create_from_surface(robo_gfx_surface_t *surface,
                                                 robo_gfx_image_t **out);

void robo_gfx_image_retain(robo_gfx_image_t *image);
void robo_gfx_image_release(robo_gfx_image_t *image);

robo_gfx_image_id_t robo_gfx_image_id(const robo_gfx_image_t *image);
robo_gfx_image_kind_t robo_gfx_image_kind(const robo_gfx_image_t *image);
uint32_t robo_gfx_image_width(const robo_gfx_image_t *image);
uint32_t robo_gfx_image_height(const robo_gfx_image_t *image);
robo_gfx_pixel_format_t robo_gfx_image_format(const robo_gfx_image_t *image);

/**
 * Borrowed surface pointer for raster images (retained by the image).
 * NULL for empty/external without surface.
 */
robo_gfx_surface_t *robo_gfx_image_surface(robo_gfx_image_t *image);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_IMAGE_H */
