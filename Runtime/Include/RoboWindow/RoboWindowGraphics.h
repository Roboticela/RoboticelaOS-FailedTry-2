/*
 * RoboWindow ↔ RoboGraphics integration (Goal 7.14).
 *
 * Owned render surface + frame lifecycle on top of Goal 7.4 present:
 *
 *   begin_frame → draw (RoboGraphics) → end_frame → present_frame
 *        ↓                                    ↓
 *   owned surface (physical px)          backend PresentSurface
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
Result WindowEnsureRenderSurface(WindowId id);

/**
 * Borrowed pointer to the owned render surface (NULL if none / invalid id).
 * Valid until release, destroy, or successful recreate in ensure/begin_frame.
 */
GfxSurface *WindowGetRenderSurface(WindowId id);

/** Release the owned surface (no-op if none). Clears dirty + open frame. */
Result WindowReleaseRenderSurface(WindowId id);

/**
 * Preferred format for owned surfaces (default BGRA8).
 * Marks surface stale so the next ensure/begin recreates it.
 */
Result WindowSetRenderFormat(WindowId id,
                                            GfxPixelFormat format);

Result WindowGetRenderFormat(WindowId id,
                                            GfxPixelFormat *out);

/** Physical pixel size of the owned surface (0×0 if none). */
Result WindowGetRenderSize(WindowId id,
                                          GfxSize *out);

/**
 * Begin a frame: ensure surface, mark frame open, return draw target.
 * Nested begin → RoboBusy (*outSurface unchanged). Closing → invalid.
 * Writes *outSurface only on RoboOk.
 */
Result WindowBeginFrame(WindowId id,
                                      GfxSurface **outSurface);

/**
 * End a frame: mark the window dirty (full surface). Does not present.
 * Without matching begin → RoboInvalidArgument.
 */
Result WindowEndFrame(WindowId id);

/**
 * Present the owned render surface when dirty.
 * Not dirty → RoboOk (no backend call). No surface → ensure then present.
 * Clears dirty on success. Uses the same backend present path as
 * WindowPresent.
 */
Result WindowPresentFrame(WindowId id);

/** Mark the entire surface dirty (needs present). */
Result WindowInvalidate(WindowId id);

/**
 * Mark a logical client rectangle dirty.
 * Empty / invalid rect → RoboInvalidArgument.
 * Tracking is full-surface fallback if any region is dirty (simple model).
 */
Result WindowInvalidateRect(WindowId id,
                                          GfxRect rect);

/** Non-zero if a present is pending. */
int WindowIsDirty(WindowId id);

/** Clear dirty without presenting. */
Result WindowClearDirty(WindowId id);

/** Non-zero while between begin_frame and end_frame. */
int WindowFrameIsOpen(WindowId id);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_GRAPHICS_H */
