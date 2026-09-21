/*
 * RoboWindow ↔ RoboDisplay association + scale/DPI queries (Goals 7.3 / 7.8).
 *
 * Display ownership stays in RoboDisplay. Windows store displayId and may
 * query effective scale/DPI from the associated display or window state.
 *
 * Dynamic DPI-change events are deferred (Goal 7.9+).
 */

#ifndef ROBO_WINDOW_DISPLAY_H
#define ROBO_WINDOW_DISPLAY_H

#include "RoboBase/RoboError.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayScale.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboWindow/RoboWindowTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Copy associated display info. No association → RoboNotFound.
 */
Result WindowGetAssociatedDisplay(WindowId windowId,
                                                 DisplayInfo *out);

/**
 * Associate (or clear with NONE). Unknown non-NONE id → RoboNotFound.
 * When associating a display with known scale and no create-time preference,
 * updates window state.scale from the display (Goal 7.8).
 */
Result WindowSetAssociatedDisplay(WindowId windowId,
                                                 DisplayId displayId);

/**
 * Effective window scale (Goal 7.8).
 * Resolution order:
 *   1. known state.scale
 *   2. associated display effective scale
 *   3. RoboNotFound if unavailable
 */
Result WindowGetScale(WindowId id, float *outScale);

/**
 * DPI from associated display. No association → RoboNotFound.
 * Axes may be individually unavailable (0).
 */
Result WindowGetDpi(WindowId id, DisplayDpi *out);

/**
 * Logical client size → physical pixels using effective window scale.
 */
Result WindowGetSizePhysical(WindowId id,
                                            GfxSize *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_DISPLAY_H */
