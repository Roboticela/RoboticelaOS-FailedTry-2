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
/* `robo_gfx_raster_draw_image` (11.7) takes a resource, so the raster header names the image
 * type it hands to the sampler: a forward declaration would do for a pointer parameter, and the
 * include is what makes the signature checkable against the type it actually states. */
#include "RoboGraphics/RoboGraphicsImage.h"
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

/**
 * Draw @p image's @p source rectangle into @p destination, scaled and alpha-blended
 * (Phase 11.7).
 *
 * The one drawing primitive this module gained for image painting, and it is the *scaled*
 * half of Goal 6.6's `robo_gfx_surface_copy_rect`, which copies 1:1 and requires both
 * rectangles to fit their surfaces exactly. A document's `<img>` presents the opposite
 * problem: the resource has one intrinsic size and the box it was laid out into has
 * another, so something must resample. That something is this call, and it is here rather
 * than in a layer above because pixels are this module's business -- `RoboGraphicsDecode.h`
 * states the same rule for the same reason, and a caller sampling an image by hand would be
 * re-deciding the blend, the clip and the opacity that this module already owns.
 *
 * **Nearest-neighbour, stated rather than implied.** There is no filter policy in v1 -- no
 * `image-rendering`, no interpolation quality, no mip levels -- so the sample for a
 * destination pixel is the source pixel its offset maps to:
 *
 *     sx = source.x + ((x - destination.x) * source.width)  / destination.width
 *     sy = source.y + ((y - destination.y) * source.height) / destination.height
 *
 * which is exact at 1:1 (the mapping is the identity, so an unscaled draw is a copy) and
 * deterministic at every other ratio. The arithmetic is 64-bit because `destination` and
 * `source` are each bounded by `INT32_MAX` and their product is not.
 *
 * The rectangle rules are this module's own, taken from the calls beside it:
 *
 *   `source` must lie **fully inside** the image and have a non-zero area (or be empty,
 *   which is a successful no-op) -- a source rectangle that ran off the image would be a
 *   caller reading pixels that do not exist, which `robo_gfx_surface_copy_rect` refuses for
 *   the same reason;
 *   `destination` is **clipped**, not refused: the raster's current clip bounds what lands,
 *   exactly as it does for a fill, so a destination partly off the target draws the part
 *   that is on it;
 *   an empty `destination` or an empty clip is a successful no-op, and the destination is
 *   never required to fit the surface.
 *
 * Alpha is preserved through `robo_gfx_surface_get_pixel` (which converts whatever the
 * image's format is into the logical RGBA8 this module composites in) and the raster's
 * per-pixel path: a source pixel with `a == 0` leaves the target untouched (rather than
 * filling it with transparent), and the raster's **global opacity** multiplies the image's
 * own alpha before the source-over blend exactly as it multiplies a fill's.
 *
 * @return ROBO_OK, including the no-op cases above; ROBO_INVALID_ARGUMENT for NULL
 *         @p raster or @p image, a negative-extent rectangle, or a @p source that does not
 *         lie inside the image; ROBO_NOT_SUPPORTED for an image with no pixel surface
 *         (`ROBO_GFX_IMAGE_KIND_EMPTY`/`EXTERNAL`) or a zero-sized one -- a resource this
 *         call cannot read rather than a caller mistake.
 */
robo_result_t robo_gfx_raster_draw_image(robo_gfx_raster_t *raster,
                                         robo_gfx_image_t *image,
                                         robo_gfx_rect_t source,
                                         robo_gfx_rect_t destination);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_RASTER_H */
