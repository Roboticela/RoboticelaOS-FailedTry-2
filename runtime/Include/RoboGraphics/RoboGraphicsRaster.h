/*
 * RoboGraphics CPU raster backend (Goal 6.11).
 *
 * Immediate-mode CPU drawing into an existing write-capable surface.
 * Destination ownership stays with the surface; the raster context holds a
 * retain and mutable clip / opacity state only.
 *
 * Compositing: source-over (Goal 6.10). Global opacity multiplies source
 * alpha before blend. Opaque source + opacity 255 may use direct replace
 * (equivalent to src-over).
 *
 * Ordering: each draw mutates the destination immediately — no hidden
 * command buffer. Later draws overwrite / blend over earlier ones.
 *
 * Paths: fill/stroke entry points exist as the integration boundary and
 * return ROBO_NOT_SUPPORTED until a later goal.
 *
 * See graphics/docs/Raster.md.
 */

#ifndef ROBO_GRAPHICS_RASTER_H
#define ROBO_GRAPHICS_RASTER_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsPath.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_gfx_raster robo_gfx_raster_t;

/**
 * Bind a CPU raster context to @p target.
 * Target must be non-NULL, write-capable, implemented format, and have
 * dimensions representable as int32 pixel bounds.
 * Retains @p target until destroy.
 */
robo_result_t robo_gfx_raster_create(robo_gfx_surface_t *target,
                                     robo_gfx_raster_t **out);

/** Release the context and its retain on the target. NULL-safe. */
void robo_gfx_raster_destroy(robo_gfx_raster_t *raster);

robo_gfx_surface_t *robo_gfx_raster_target(const robo_gfx_raster_t *raster);

/**
 * Effective clip (half-open), always ⊆ surface pixel bounds.
 * May be empty.
 */
robo_result_t robo_gfx_raster_clip(const robo_gfx_raster_t *raster,
                                   robo_gfx_rect_t *out_clip);

/** Reset clip to the full surface pixel bounds. */
robo_result_t robo_gfx_raster_reset_clip(robo_gfx_raster_t *raster);

/**
 * Set clip to intersection(@p clip, surface bounds).
 * Invalid @p clip → ROBO_INVALID_ARGUMENT.
 * Empty intersection → empty clip (success).
 */
robo_result_t robo_gfx_raster_set_clip(robo_gfx_raster_t *raster,
                                       robo_gfx_rect_t clip);

/**
 * Further intersect the current clip with @p clip.
 * Invalid @p clip → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_raster_intersect_clip(robo_gfx_raster_t *raster,
                                             robo_gfx_rect_t clip);

/** Global source opacity (0 = invisible, 255 = full). Default 255. */
void robo_gfx_raster_set_opacity(robo_gfx_raster_t *raster, uint8_t opacity);
uint8_t robo_gfx_raster_opacity(const robo_gfx_raster_t *raster);

/**
 * Fill the current clip with @p color (source-over + opacity).
 * Empty clip → ROBO_OK no-op.
 */
robo_result_t robo_gfx_raster_clear(robo_gfx_raster_t *raster,
                                    robo_gfx_color_rgba8_t color);

/**
 * Composite one pixel. Outside clip / negative → ROBO_OK no-op.
 */
robo_result_t robo_gfx_raster_draw_pixel(robo_gfx_raster_t *raster,
                                         robo_gfx_point_t p,
                                         robo_gfx_color_rgba8_t color);

/**
 * Fill @p rect (half-open) clipped to the current clip.
 * Empty / fully outside → ROBO_OK no-op. Invalid rect → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_raster_fill_rect(robo_gfx_raster_t *raster,
                                        robo_gfx_rect_t rect,
                                        robo_gfx_color_rgba8_t color);

/**
 * One-pixel Bresenham line (Goal 6.7 algorithm) clipped to the current clip.
 * Uses source-over + opacity. Fully outside → ROBO_OK no-op.
 */
robo_result_t robo_gfx_raster_draw_line(robo_gfx_raster_t *raster,
                                        robo_gfx_point_t start,
                                        robo_gfx_point_t end,
                                        robo_gfx_color_rgba8_t color);

/**
 * Path fill boundary — not implemented in Goal 6.11.
 * Validates arguments then returns ROBO_NOT_SUPPORTED.
 */
robo_result_t robo_gfx_raster_fill_path(robo_gfx_raster_t *raster,
                                        const robo_gfx_path_t *path,
                                        robo_gfx_color_rgba8_t color);

/**
 * Path stroke boundary — not implemented in Goal 6.11.
 * Validates arguments then returns ROBO_NOT_SUPPORTED.
 */
robo_result_t robo_gfx_raster_stroke_path(robo_gfx_raster_t *raster,
                                          const robo_gfx_path_t *path,
                                          robo_gfx_color_rgba8_t color);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_RASTER_H */
