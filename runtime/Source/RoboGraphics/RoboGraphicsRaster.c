#include "RoboGraphics/RoboGraphicsRaster.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsComposite.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboGraphics/RoboGraphicsLine.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_gfx_raster {
    robo_gfx_surface_t *target;
    robo_gfx_rect_t clip; /* always ⊆ surface bounds; may be empty */
    uint8_t opacity;
};

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

static int needs_blend(robo_gfx_color_rgba8_t color, uint8_t opacity)
{
    return !(color.a == 255u && opacity == 255u);
}

static robo_result_t fill_rect_blended(robo_gfx_surface_t *surface,
                                       robo_gfx_rect_t rect,
                                       robo_gfx_color_rgba8_t color,
                                       uint8_t opacity)
{
    int32_t x;
    int32_t y;
    int32_t x1;
    int32_t y1;
    robo_result_t res;

    if (robo_gfx_rect_is_empty(rect)) {
        return ROBO_OK;
    }
    if (!needs_blend(color, opacity)) {
        return robo_gfx_surface_fill_rect(surface, rect, color);
    }

    x1 = rect.x + rect.width;
    y1 = rect.y + rect.height;
    for (y = rect.y; y < y1; y++) {
        for (x = rect.x; x < x1; x++) {
            res = robo_gfx_surface_composite_pixel(surface, (uint32_t)x,
                                                   (uint32_t)y, color, opacity);
            if (res != ROBO_OK) {
                return res;
            }
        }
    }
    return ROBO_OK;
}

robo_result_t robo_gfx_raster_create(robo_gfx_surface_t *target,
                                     robo_gfx_raster_t **out)
{
    robo_gfx_raster_t *raster;
    robo_gfx_rect_t bounds;
    robo_result_t res;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (!target || !surface_can_write(target)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_pixel_format_is_implemented(robo_gfx_surface_format(target))) {
        return ROBO_NOT_SUPPORTED;
    }
    res = robo_gfx_surface_pixel_bounds(target, &bounds);
    if (res != ROBO_OK) {
        return res;
    }

    raster = (robo_gfx_raster_t *)robo_calloc(1, sizeof(*raster));
    if (!raster) {
        return ROBO_OUT_OF_MEMORY;
    }
    robo_gfx_surface_retain(target);
    raster->target = target;
    raster->clip = bounds;
    raster->opacity = 255u;
    *out = raster;
    return ROBO_OK;
}

void robo_gfx_raster_destroy(robo_gfx_raster_t *raster)
{
    if (!raster) {
        return;
    }
    if (raster->target) {
        robo_gfx_surface_release(raster->target);
        raster->target = NULL;
    }
    robo_free(raster);
}

robo_gfx_surface_t *robo_gfx_raster_target(const robo_gfx_raster_t *raster)
{
    return raster ? raster->target : NULL;
}

robo_result_t robo_gfx_raster_clip(const robo_gfx_raster_t *raster,
                                   robo_gfx_rect_t *out_clip)
{
    if (!raster || !out_clip) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_clip = raster->clip;
    return ROBO_OK;
}

robo_result_t robo_gfx_raster_reset_clip(robo_gfx_raster_t *raster)
{
    robo_gfx_rect_t bounds;
    robo_result_t res;

    if (!raster || !raster->target) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_surface_pixel_bounds(raster->target, &bounds);
    if (res != ROBO_OK) {
        return res;
    }
    raster->clip = bounds;
    return ROBO_OK;
}

robo_result_t robo_gfx_raster_set_clip(robo_gfx_raster_t *raster,
                                       robo_gfx_rect_t clip)
{
    robo_gfx_rect_t bounds;
    robo_gfx_rect_t effective;
    robo_result_t res;

    if (!raster || !raster->target) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_rect_is_valid(clip)) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_surface_pixel_bounds(raster->target, &bounds);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_gfx_rect_clip(clip, bounds, &effective);
    if (res != ROBO_OK) {
        return res;
    }
    raster->clip = effective;
    return ROBO_OK;
}

robo_result_t robo_gfx_raster_intersect_clip(robo_gfx_raster_t *raster,
                                             robo_gfx_rect_t clip)
{
    robo_gfx_rect_t next;
    robo_result_t res;

    if (!raster) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_rect_is_valid(clip)) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_rect_intersect(raster->clip, clip, &next);
    if (res != ROBO_OK) {
        return res;
    }
    raster->clip = next;
    return ROBO_OK;
}

void robo_gfx_raster_set_opacity(robo_gfx_raster_t *raster, uint8_t opacity)
{
    if (!raster) {
        return;
    }
    raster->opacity = opacity;
}

uint8_t robo_gfx_raster_opacity(const robo_gfx_raster_t *raster)
{
    return raster ? raster->opacity : 0u;
}

robo_result_t robo_gfx_raster_clear(robo_gfx_raster_t *raster,
                                    robo_gfx_color_rgba8_t color)
{
    if (!raster || !raster->target) {
        return ROBO_INVALID_ARGUMENT;
    }
    return fill_rect_blended(raster->target, raster->clip, color,
                             raster->opacity);
}

robo_result_t robo_gfx_raster_draw_pixel(robo_gfx_raster_t *raster,
                                         robo_gfx_point_t p,
                                         robo_gfx_color_rgba8_t color)
{
    if (!raster || !raster->target) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_rect_contains_point(raster->clip, p)) {
        return ROBO_OK;
    }
    if (!needs_blend(color, raster->opacity)) {
        return robo_gfx_surface_set_pixel(raster->target, (uint32_t)p.x,
                                          (uint32_t)p.y, color);
    }
    return robo_gfx_surface_composite_pixel(raster->target, (uint32_t)p.x,
                                            (uint32_t)p.y, color,
                                            raster->opacity);
}

robo_result_t robo_gfx_raster_fill_rect(robo_gfx_raster_t *raster,
                                        robo_gfx_rect_t rect,
                                        robo_gfx_color_rgba8_t color)
{
    robo_gfx_rect_t clipped;
    robo_result_t res;

    if (!raster || !raster->target) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_rect_is_valid(rect)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_rect_is_empty(rect) || robo_gfx_rect_is_empty(raster->clip)) {
        return ROBO_OK;
    }
    res = robo_gfx_rect_clip(rect, raster->clip, &clipped);
    if (res != ROBO_OK) {
        return res;
    }
    return fill_rect_blended(raster->target, clipped, color, raster->opacity);
}

robo_result_t robo_gfx_raster_draw_line(robo_gfx_raster_t *raster,
                                        robo_gfx_point_t start,
                                        robo_gfx_point_t end,
                                        robo_gfx_color_rgba8_t color)
{
    int composite;

    if (!raster || !raster->target) {
        return ROBO_INVALID_ARGUMENT;
    }
    composite = needs_blend(color, raster->opacity) ? 1 : 0;
    return robo_gfx_surface_draw_line_clipped(raster->target, start, end, color,
                                              raster->clip, raster->opacity,
                                              composite);
}

robo_result_t robo_gfx_raster_draw_image(robo_gfx_raster_t *raster,
                                         robo_gfx_image_t *image,
                                         robo_gfx_rect_t source,
                                         robo_gfx_rect_t destination)
{
    robo_gfx_surface_t *pixels;
    robo_gfx_rect_t visible;
    int64_t image_width;
    int64_t image_height;
    int64_t right;
    int64_t bottom;
    int32_t x;
    int32_t y;

    if (!raster || !raster->target || !image) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_rect_is_valid(source) || !robo_gfx_rect_is_valid(destination)) {
        return ROBO_INVALID_ARGUMENT;
    }
    pixels = robo_gfx_image_surface(image);
    if (!pixels) {
        /*
         * A metadata-only image (`EMPTY`/`EXTERNAL`) has nothing to sample. It is a
         * *resource* this call cannot read rather than a caller mistake, which is the
         * distinction `ROBO_NOT_SUPPORTED` exists for -- the same answer
         * `robo_gfx_raster_draw_glyph` gives a render mode it does not implement.
         */
        return ROBO_NOT_SUPPORTED;
    }
    image_width = (int64_t)robo_gfx_image_width(image);
    image_height = (int64_t)robo_gfx_image_height(image);
    if (image_width <= 0 || image_height <= 0) {
        return ROBO_NOT_SUPPORTED;
    }
    if (source.x < 0 || source.y < 0 ||
        (int64_t)source.x + (int64_t)source.width > image_width ||
        (int64_t)source.y + (int64_t)source.height > image_height) {
        /* Reading pixels that do not exist is refused rather than clamped, for the reason
         * `robo_gfx_surface_copy_rect` refuses it: a clamped source silently draws a
         * *different* picture from the one the caller asked for. */
        return ROBO_INVALID_ARGUMENT;
    }
    /* The no-ops: nothing to sample, nowhere to put it, or a clip that admits nothing. */
    if (robo_gfx_rect_is_empty(source) || robo_gfx_rect_is_empty(destination)) {
        return ROBO_OK;
    }
    if (robo_gfx_rect_is_empty(raster->clip)) {
        return ROBO_OK;
    }
    if (robo_gfx_rect_intersect(destination, raster->clip, &visible) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_rect_is_empty(visible)) {
        return ROBO_OK;
    }

    right = (int64_t)visible.x + (int64_t)visible.width;
    bottom = (int64_t)visible.y + (int64_t)visible.height;
    for (y = visible.y; (int64_t)y < bottom; y++) {
        int64_t sy = (int64_t)source.y +
                     ((int64_t)(y - destination.y) * (int64_t)source.height) /
                         (int64_t)destination.height;

        for (x = visible.x; (int64_t)x < right; x++) {
            int64_t sx = (int64_t)source.x +
                         ((int64_t)(x - destination.x) * (int64_t)source.width) /
                             (int64_t)destination.width;
            robo_gfx_color_rgba8_t color;

            if (robo_gfx_surface_get_pixel(pixels, (uint32_t)sx, (uint32_t)sy, &color) !=
                ROBO_OK) {
                /* Unreachable: the source rectangle was checked against the image's own
                 * dimensions above, and the mapping cannot leave it. Skipped rather than
                 * reported so that a future format this module learns to read cannot fail a
                 * whole draw over one pixel. */
                continue;
            }
            if (!needs_blend(color, raster->opacity)) {
                (void)robo_gfx_surface_set_pixel(raster->target, (uint32_t)x, (uint32_t)y,
                                                color);
            } else {
                (void)robo_gfx_surface_composite_pixel(raster->target, (uint32_t)x,
                                                       (uint32_t)y, color,
                                                       raster->opacity);
            }
        }
    }
    return ROBO_OK;
}

robo_result_t robo_gfx_raster_fill_path(robo_gfx_raster_t *raster,
                                        const robo_gfx_path_t *path,
                                        robo_gfx_color_rgba8_t color)
{
    (void)color;
    if (!raster || !raster->target || !path) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_NOT_SUPPORTED;
}

robo_result_t robo_gfx_raster_stroke_path(robo_gfx_raster_t *raster,
                                          const robo_gfx_path_t *path,
                                          robo_gfx_color_rgba8_t color)
{
    (void)color;
    if (!raster || !raster->target || !path) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_NOT_SUPPORTED;
}
