/*
 * RoboWindow runtime state (Goals 7.1 / 7.2).
 *
 * Distinguishes requested geometry from backend-reported actual values.
 * When the backend does not report actual state, core may apply requested
 * values as provisional actual after a successful request.
 */

#ifndef ROBO_WINDOW_STATE_H
#define ROBO_WINDOW_STATE_H

#include "RoboWindow/RoboWindowLifecycle.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <limits.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sentinel: position / physical metrics unknown. */
#define ROBO_WINDOW_COORD_UNKNOWN ((int32_t)INT32_MIN)

typedef struct robo_window_state {
    robo_window_lifecycle_t lifecycle;
    int32_t x; /* ROBO_WINDOW_COORD_UNKNOWN if unknown */
    int32_t y;
    int32_t width;  /* actual logical client size */
    int32_t height;
    int32_t requested_width;
    int32_t requested_height;
    int32_t requested_x; /* ROBO_WINDOW_COORD_UNKNOWN if never requested */
    int32_t requested_y;
    int32_t min_width;  /* 0 = none; mirrored from constraints */
    int32_t min_height;
    int32_t max_width;
    int32_t max_height;
    int visible;
    int focused;
    int minimized;
    int maximized;
    int fullscreen;
    float scale; /* 0 → unknown; effective display/window scale (Goal 7.8) */
    robo_display_id_t display_id;
} robo_window_state_t;

void robo_window_state_init(robo_window_state_t *st);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_STATE_H */
