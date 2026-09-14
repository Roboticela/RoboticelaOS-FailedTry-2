#include "RoboGraphics/RoboGraphicsRaster.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsComposite.h"
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
