/*
 * RoboWindow ↔ RoboDisplay bridge (Goal 7.3).
 *
 * Display ownership lives in RoboDisplay. After window init, the architecture
 * placeholder backend is registered so create/primary paths keep working.
 * No host display enumeration.
 */

#ifndef ROBO_WINDOW_DISPLAY_H
#define ROBO_WINDOW_DISPLAY_H

#include "RoboDisplay/RoboDisplay.h"
#include "RoboWindow/RoboWindowTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Copy info for the display currently associated with @p window_id.
 * Unknown / none association → ROBO_NOT_FOUND.
 * Does not probe native window→monitor mapping (later phase).
 */
robo_result_t robo_window_get_associated_display(robo_window_id_t window_id,
                                                 robo_display_info_t *out);

/**
 * Associate @p display_id with the window (logical only).
 * ROBO_DISPLAY_ID_NONE clears the association.
 * Unknown non-NONE id → ROBO_NOT_FOUND.
 */
robo_result_t robo_window_set_associated_display(robo_window_id_t window_id,
                                                 robo_display_id_t display_id);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_DISPLAY_H */
