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
#define RoboWindowCoordUnknown ((int32_t)INT32_MIN)

typedef struct WindowState {
    WindowLifecycle lifecycle;
    int32_t x; /* RoboWindowCoordUnknown if unknown */
    int32_t y;
    int32_t width;  /* actual logical client size */
    int32_t height;
    int32_t requestedWidth;
    int32_t requestedHeight;
    int32_t requestedX; /* RoboWindowCoordUnknown if never requested */
    int32_t requestedY;
    int32_t minWidth;  /* 0 = none; mirrored from constraints */
    int32_t minHeight;
    int32_t maxWidth;
    int32_t maxHeight;
    int visible;
    int focused;
    int minimized;
    int maximized;
    int fullscreen;
    float scale; /* 0 → unknown; effective display/window scale (Goal 7.8) */
    DisplayId displayId;
} WindowState;

void WindowStateInit(WindowState *st);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_STATE_H */
