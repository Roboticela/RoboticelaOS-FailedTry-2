/*
 * RoboWindow ↔ RoboGraphics integration (Goal 7.14).
 *
 * Owned render surface + frame lifecycle on top of Goal 7.4 present:
 *
 *   begin_frame → draw (RoboGraphics) → end_frame → present_frame
 *        ↓                                    ↓
 *   owned surface (physical px)          backend present_surface
 *
 * Single-buffered CPU surface. No compositor / VSync / damage tiles.
 * Chromium compositor is technical reference only — not a dependency.
 */

#ifndef ROBO_WINDOW_GRAPHICS_H
#define ROBO_WINDOW_GRAPHICS_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboWindow/RoboWindowTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Ensure the window owns a render surface matching physical client size
 * (logical size × effective scale) and preferred pixel format.
 * Recreates when stale (resize / scale / format change).
 * New surfaces are zero-filled and marked dirty.
 */
robo_result_t robo_window_ensure_render_surface(robo_window_id_t id);

/**
 * Borrowed pointer to the owned render surface (NULL if none / invalid id).
 * Valid until release, destroy, or successful recreate in ensure/begin_frame.
 */
robo_gfx_surface_t *robo_window_get_render_surface(robo_window_id_t id);

/** Release the owned surface (no-op if none). Clears dirty + open frame. */
robo_result_t robo_window_release_render_surface(robo_window_id_t id);

/**
 * Preferred format for owned surfaces (default BGRA8).
 * Marks surface stale so the next ensure/begin recreates it.
 */
robo_result_t robo_window_set_render_format(robo_window_id_t id,
                                            robo_gfx_pixel_format_t format);

robo_result_t robo_window_get_render_format(robo_window_id_t id,
                                            robo_gfx_pixel_format_t *out);

/** Physical pixel size of the owned surface (0×0 if none). */
robo_result_t robo_window_get_render_size(robo_window_id_t id,
                                          robo_gfx_size_t *out);

/**
 * Begin a frame: ensure surface, mark frame open, return draw target.
 * Nested begin → ROBO_BUSY (*out_surface unchanged). Closing → invalid.
 * Writes *out_surface only on ROBO_OK.
 */
robo_result_t robo_window_begin_frame(robo_window_id_t id,
                                      robo_gfx_surface_t **out_surface);

/**
 * End a frame: mark the window dirty (full surface). Does not present.
 * Without matching begin → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_window_end_frame(robo_window_id_t id);

/**
 * Present the owned render surface when dirty.
 * Not dirty → ROBO_OK (no backend call). No surface → ensure then present.
 * Clears dirty on success. Uses the same backend present path as
 * robo_window_present.
 */
robo_result_t robo_window_present_frame(robo_window_id_t id);

/** Mark the entire surface dirty (needs present). */
robo_result_t robo_window_invalidate(robo_window_id_t id);

/**
 * Mark a logical client rectangle dirty.
 * Empty / invalid rect → ROBO_INVALID_ARGUMENT.
 * Tracking is full-surface fallback if any region is dirty (simple model).
 */
robo_result_t robo_window_invalidate_rect(robo_window_id_t id,
                                          robo_gfx_rect_t rect);

/** Non-zero if a present is pending. */
int robo_window_is_dirty(robo_window_id_t id);

/** Clear dirty without presenting. */
robo_result_t robo_window_clear_dirty(robo_window_id_t id);

/** Non-zero while between begin_frame and end_frame. */
int robo_window_frame_is_open(robo_window_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_GRAPHICS_H */
