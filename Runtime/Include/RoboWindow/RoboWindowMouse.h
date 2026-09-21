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
typedef enum MouseButton {
    RoboMouseButtonNone = 0,
    RoboMouseButtonLeft = 1,
    RoboMouseButtonRight = 2,
    RoboMouseButtonMiddle = 3,
    RoboMouseButtonX1 = 4,
    RoboMouseButtonX2 = 5,
    RoboMouseButtonUnknown = 6
} MouseButton;

#define RoboMouseButtonCount 7

typedef enum MouseEventType {
    RoboMouseEventNone = 0,
    RoboMouseEventMove = 1,
    RoboMouseEventButtonDown = 2,
    RoboMouseEventButtonUp = 3,
    RoboMouseEventWheel = 4
} MouseEventType;

/**
 * Scroll units for wheel deltas.
 *
 * LINE: classic notch units where |120| equals one discrete wheel step
 *       (matches Win32 WHEEL_DELTA). High-resolution devices may send
 *       smaller magnitudes; do not discard precision.
 * PIXEL: reserved for backends that report pixel deltas (not used on
 *        current Win32 path; treated as LINE if mis-reported).
 */
typedef enum MouseScrollUnit {
    RoboMouseScrollUnitLine = 0,
    RoboMouseScrollUnitPixel = 1
} MouseScrollUnit;

typedef struct MousePosition {
    int32_t x; /* client / content logical coordinates */
    int32_t y;
} MousePosition;

typedef struct MouseDelta {
    int32_t dx;
    int32_t dy;
} MouseDelta;

typedef struct MouseScroll {
    int32_t deltaX; /* horizontal; sign: positive = right / away */
    int32_t deltaY; /* vertical; sign: positive = away from user / "up" */
    MouseScrollUnit unit;
} MouseScroll;

/**
 * Mouse payload on MOUSE_* window events.
 *
 * Coordinates (Goal 7.8 / 7.11):
 *   clientX/y — origin top-left of the window client area; +x right, +y down;
 *                integer logical DIPs (same space as window client size).
 *   screenX/y — origin top-left of the primary display virtual desktop in
 *                the same logical units when the backend can convert; else
 *                equal to client when conversion is unavailable (0,0 means
 *                unknown only when hasScreenCoords == 0).
 *   delta_*    — movement since the previous tracked position for this
 *                runtime (0 on first sample or after reset).
 *
 * Physical vs logical: pointer positions are logical. Convert sizes with
 * WindowGetSizePhysical / display scale helpers when framebuffer
 * pixels are required — do not treat clientX/y as physical pixels.
 */
typedef struct MouseEvent {
    MouseEventType type;
    MouseButton button; /* meaningful for button down/up */
    int32_t clientX;
    int32_t clientY;
    int32_t screenX;
    int32_t screenY;
    int hasScreenCoords; /* non-zero when screen_* are valid */
    int32_t deltaX;
    int32_t deltaY;
    int32_t scrollX;
    int32_t scrollY;
    MouseScrollUnit scrollUnit;
    int clickCount; /* 0 if unavailable; else >= 1 */
    KeyModifierFlags modifiers;
    uint64_t timestampMs; /* 0 = unavailable */
    WindowId windowId;
} MouseEvent;

void MouseEventInit(MouseEvent *ev);

int MouseButtonIsKnown(MouseButton button);
const char *MouseButtonName(MouseButton button);
const char *MouseEventTypeName(MouseEventType type);

/**
 * Deliver a translated mouse event into RoboWindow.
 *
 * @p windowId: target, or RoboWindowIdNone → focused live window, else
 * captured window if capture is active. Destroyed / unknown → RoboNotFound.
 *
 * Updates mouse state, then pushes the matching ROBO_WINDOW_EVENT_MOUSE_*.
 * Unknown button on button events → RoboOk, logged, no queue push for that
 * payload (state unchanged for unknown).
 */
Result WindowObserveMouse(WindowId windowId,
                                        const MouseEvent *ev);

/** Non-zero if @p button is tracked as pressed. */
int MouseIsButtonPressed(MouseButton button);

/** Copy last known client position; returns 0 if never observed. */
int MouseGetPosition(MousePosition *out);

KeyModifierFlags MouseModifiers(void);

/**
 * Capture foundation: route subsequent soft targeting to @p id.
 * When the backend supports native capture (caps.mouseCapture), also
 * requests OS pointer capture for that window's native handle.
 */
Result WindowMouseCapture(WindowId id);

/** Release soft + native capture if held. */
Result WindowMouseReleaseCapture(void);

/** Captured window, or RoboWindowIdNone. */
WindowId WindowMouseCapturedId(void);

/**
 * Clear pressed buttons and last position deltas; release soft capture.
 * Used on focus loss, destroy, backend/runtime shutdown.
 */
void MouseResetPressedState(void);

/** Full mouse subsystem reset. */
void MouseReset(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_MOUSE_H */
