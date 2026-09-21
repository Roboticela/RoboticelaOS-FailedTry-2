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
typedef struct KeyboardEvent {
    KeyAction action;
    PhysicalKey physicalKey;
    LogicalKey logicalKey;
    KeyModifierFlags modifiers;
    int isRepeat;          /* non-zero: native auto-repeat key-down */
    uint64_t timestampMs;  /* 0 = unavailable */
    WindowId windowId;
} KeyboardEvent;

void KeyboardEventInit(KeyboardEvent *ev);

/**
 * Deliver a translated key event into RoboWindow.
 *
 * @p windowId: target window, or RoboWindowIdNone to use the currently
 * focused live window. Destroyed / unknown → RoboNotFound (no crash).
 * No focused window when NONE → RoboNotFound (state may still update for
 * modifiers when action is coherent — see docs).
 *
 * Updates keyboard state, then pushes KEY_DOWN / KEY_UP if a live target
 * exists. Unknown physical+logical both → RoboOk with debug log (dropped
 * payload still updates nothing harmful).
 */
Result WindowObserveKey(WindowId windowId,
                                      const KeyboardEvent *ev);

/** Non-zero if @p physical is currently tracked as pressed. */
int KeyboardIsPhysicalPressed(PhysicalKey physical);

/** Non-zero if any tracked physical key that maps to @p logical is pressed. */
int KeyboardIsLogicalPressed(LogicalKey logical);

KeyModifierFlags KeyboardModifiers(void);

int KeyboardModifierActive(KeyModifierFlags flag);

/**
 * Clear all pressed-key tracking and latch-only refresh of lock bits.
 * Used on focus loss, destroy, backend/runtime shutdown.
 */
void KeyboardResetPressedState(void);

/** Full keyboard subsystem reset (pressed + modifiers cache). */
void KeyboardReset(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_KEYBOARD_H */
