/*
 * RoboWindow mouse foundation (Goal 7.11).
 *
 * native mouse → translate → observe_mouse → event queue → application
 *
 * Distinct from keyboard, touch, stylus, and generic pointer APIs.
 * Chromium WebMouseEvent / ui::MouseEvent are technical reference only.
 */

#ifndef ROBO_WINDOW_MOUSE_H
#define ROBO_WINDOW_MOUSE_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowKey.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Stable Roboticela mouse button identifiers (not native button codes). */
typedef enum robo_mouse_button {
    ROBO_MOUSE_BUTTON_NONE = 0,
    ROBO_MOUSE_BUTTON_LEFT = 1,
    ROBO_MOUSE_BUTTON_RIGHT = 2,
    ROBO_MOUSE_BUTTON_MIDDLE = 3,
    ROBO_MOUSE_BUTTON_X1 = 4,
    ROBO_MOUSE_BUTTON_X2 = 5,
    ROBO_MOUSE_BUTTON_UNKNOWN = 6
} robo_mouse_button_t;

#define ROBO_MOUSE_BUTTON_COUNT 7

typedef enum robo_mouse_event_type {
    ROBO_MOUSE_EVENT_NONE = 0,
    ROBO_MOUSE_EVENT_MOVE = 1,
    ROBO_MOUSE_EVENT_BUTTON_DOWN = 2,
    ROBO_MOUSE_EVENT_BUTTON_UP = 3,
    ROBO_MOUSE_EVENT_WHEEL = 4
} robo_mouse_event_type_t;

/**
 * Scroll units for wheel deltas.
 *
 * LINE: classic notch units where |120| equals one discrete wheel step
 *       (matches Win32 WHEEL_DELTA). High-resolution devices may send
 *       smaller magnitudes; do not discard precision.
 * PIXEL: reserved for backends that report pixel deltas (not used on
 *        current Win32 path; treated as LINE if mis-reported).
 */
typedef enum robo_mouse_scroll_unit {
    ROBO_MOUSE_SCROLL_UNIT_LINE = 0,
    ROBO_MOUSE_SCROLL_UNIT_PIXEL = 1
} robo_mouse_scroll_unit_t;

typedef struct robo_mouse_position {
    int32_t x; /* client / content logical coordinates */
    int32_t y;
} robo_mouse_position_t;

typedef struct robo_mouse_delta {
    int32_t dx;
    int32_t dy;
} robo_mouse_delta_t;

typedef struct robo_mouse_scroll {
    int32_t delta_x; /* horizontal; sign: positive = right / away */
    int32_t delta_y; /* vertical; sign: positive = away from user / "up" */
    robo_mouse_scroll_unit_t unit;
} robo_mouse_scroll_t;

/**
 * Mouse payload on MOUSE_* window events.
 *
 * Coordinates (Goal 7.8 / 7.11):
 *   client_x/y — origin top-left of the window client area; +x right, +y down;
 *                integer logical DIPs (same space as window client size).
 *   screen_x/y — origin top-left of the primary display virtual desktop in
 *                the same logical units when the backend can convert; else
 *                equal to client when conversion is unavailable (0,0 means
 *                unknown only when has_screen_coords == 0).
 *   delta_*    — movement since the previous tracked position for this
 *                runtime (0 on first sample or after reset).
 *
 * Physical vs logical: pointer positions are logical. Convert sizes with
 * robo_window_get_size_physical / display scale helpers when framebuffer
 * pixels are required — do not treat client_x/y as physical pixels.
 */
typedef struct robo_mouse_event {
    robo_mouse_event_type_t type;
    robo_mouse_button_t button; /* meaningful for button down/up */
    int32_t client_x;
    int32_t client_y;
    int32_t screen_x;
    int32_t screen_y;
    int has_screen_coords; /* non-zero when screen_* are valid */
    int32_t delta_x;
    int32_t delta_y;
    int32_t scroll_x;
    int32_t scroll_y;
    robo_mouse_scroll_unit_t scroll_unit;
    int click_count; /* 0 if unavailable; else >= 1 */
    robo_key_modifier_flags_t modifiers;
    uint64_t timestamp_ms; /* 0 = unavailable */
    robo_window_id_t window_id;
} robo_mouse_event_t;

void robo_mouse_event_init(robo_mouse_event_t *ev);

int robo_mouse_button_is_known(robo_mouse_button_t button);
const char *robo_mouse_button_name(robo_mouse_button_t button);
const char *robo_mouse_event_type_name(robo_mouse_event_type_t type);

/**
 * Deliver a translated mouse event into RoboWindow.
 *
 * @p window_id: target, or ROBO_WINDOW_ID_NONE → focused live window, else
 * captured window if capture is active. Destroyed / unknown → ROBO_NOT_FOUND.
 *
 * Updates mouse state, then pushes the matching ROBO_WINDOW_EVENT_MOUSE_*.
 * Unknown button on button events → ROBO_OK, logged, no queue push for that
 * payload (state unchanged for unknown).
 */
robo_result_t robo_window_observe_mouse(robo_window_id_t window_id,
                                        const robo_mouse_event_t *ev);

/** Non-zero if @p button is tracked as pressed. */
int robo_mouse_is_button_pressed(robo_mouse_button_t button);

/** Copy last known client position; returns 0 if never observed. */
int robo_mouse_get_position(robo_mouse_position_t *out);

robo_key_modifier_flags_t robo_mouse_modifiers(void);

/**
 * Capture foundation: route subsequent soft targeting to @p id.
 * When the backend supports native capture (caps.mouse_capture), also
 * requests OS pointer capture for that window's native handle.
 */
robo_result_t robo_window_mouse_capture(robo_window_id_t id);

/** Release soft + native capture if held. */
robo_result_t robo_window_mouse_release_capture(void);

/** Captured window, or ROBO_WINDOW_ID_NONE. */
robo_window_id_t robo_window_mouse_captured_id(void);

/**
 * Clear pressed buttons and last position deltas; release soft capture.
 * Used on focus loss, destroy, backend/runtime shutdown.
 */
void robo_mouse_reset_pressed_state(void);

/** Full mouse subsystem reset. */
void robo_mouse_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_MOUSE_H */
