/*
 * RoboWindow keyboard foundation (Goal 7.10).
 *
 * native key → translate → observe_key → event queue → application
 *
 * Text/IME/character generation intentionally deferred.
 * Chromium KeyEvent is technical reference only.
 */

#ifndef ROBO_WINDOW_KEYBOARD_H
#define ROBO_WINDOW_KEYBOARD_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowKey.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Keyboard payload carried on KEY_DOWN / KEY_UP window events.
 * Also used for observe/injection and state updates.
 */
typedef struct robo_keyboard_event {
    robo_key_action_t action;
    robo_physical_key_t physical_key;
    robo_logical_key_t logical_key;
    robo_key_modifier_flags_t modifiers;
    int is_repeat;          /* non-zero: native auto-repeat key-down */
    uint64_t timestamp_ms;  /* 0 = unavailable */
    robo_window_id_t window_id;
} robo_keyboard_event_t;

void robo_keyboard_event_init(robo_keyboard_event_t *ev);

/**
 * Deliver a translated key event into RoboWindow.
 *
 * @p window_id: target window, or ROBO_WINDOW_ID_NONE to use the currently
 * focused live window. Destroyed / unknown → ROBO_NOT_FOUND (no crash).
 * No focused window when NONE → ROBO_NOT_FOUND (state may still update for
 * modifiers when action is coherent — see docs).
 *
 * Updates keyboard state, then pushes KEY_DOWN / KEY_UP if a live target
 * exists. Unknown physical+logical both → ROBO_OK with debug log (dropped
 * payload still updates nothing harmful).
 */
robo_result_t robo_window_observe_key(robo_window_id_t window_id,
                                      const robo_keyboard_event_t *ev);

/** Non-zero if @p physical is currently tracked as pressed. */
int robo_keyboard_is_physical_pressed(robo_physical_key_t physical);

/** Non-zero if any tracked physical key that maps to @p logical is pressed. */
int robo_keyboard_is_logical_pressed(robo_logical_key_t logical);

robo_key_modifier_flags_t robo_keyboard_modifiers(void);

int robo_keyboard_modifier_active(robo_key_modifier_flags_t flag);

/**
 * Clear all pressed-key tracking and latch-only refresh of lock bits.
 * Used on focus loss, destroy, backend/runtime shutdown.
 */
void robo_keyboard_reset_pressed_state(void);

/** Full keyboard subsystem reset (pressed + modifiers cache). */
void robo_keyboard_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_KEYBOARD_H */
