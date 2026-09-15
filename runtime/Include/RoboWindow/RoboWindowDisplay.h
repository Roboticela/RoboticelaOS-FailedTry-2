/*
 * RoboWindow ↔ RoboDisplay association + scale/DPI queries (Goals 7.3 / 7.8).
 *
 * Display ownership stays in RoboDisplay. Windows store display_id and may
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
 * Copy associated display info. No association → ROBO_NOT_FOUND.
 */
robo_result_t robo_window_get_associated_display(robo_window_id_t window_id,
                                                 robo_display_info_t *out);

/**
 * Associate (or clear with NONE). Unknown non-NONE id → ROBO_NOT_FOUND.
 * When associating a display with known scale and no create-time preference,
 * updates window state.scale from the display (Goal 7.8).
 */
robo_result_t robo_window_set_associated_display(robo_window_id_t window_id,
                                                 robo_display_id_t display_id);

/**
 * Effective window scale (Goal 7.8).
 * Resolution order:
 *   1. known state.scale
 *   2. associated display effective scale
 *   3. ROBO_NOT_FOUND if unavailable
 */
robo_result_t robo_window_get_scale(robo_window_id_t id, float *out_scale);

/**
 * DPI from associated display. No association → ROBO_NOT_FOUND.
 * Axes may be individually unavailable (0).
 */
robo_result_t robo_window_get_dpi(robo_window_id_t id, robo_display_dpi_t *out);

/**
 * Logical client size → physical pixels using effective window scale.
 */
robo_result_t robo_window_get_size_physical(robo_window_id_t id,
                                            robo_gfx_size_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_DISPLAY_H */
