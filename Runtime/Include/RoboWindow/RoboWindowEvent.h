/*
 * RoboWindow platform-independent events (Goals 7.1 / 7.9 / 7.10 / 7.11).
 *
 * Events reference WindowId — never native handles.
 * Touch / stylus / generic pointer / IME deferred.
 *
 * Dispatch boundary:
 *   native OS → backend ProcessEvents → observe_* → push
 *   → application polls via WindowEventPoll
 */

#ifndef ROBO_WINDOW_EVENT_H
#define ROBO_WINDOW_EVENT_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowKeyboard.h"
#include "RoboWindow/RoboWindowMouse.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum WindowEventType {
    RoboWindowEventNone = 0,
    RoboWindowEventCreated = 1,
    RoboWindowEventShown = 2,
    RoboWindowEventHidden = 3,
    RoboWindowEventMoved = 4,
    RoboWindowEventResized = 5,
    RoboWindowEventCloseRequested = 6,
    RoboWindowEventClosed = 7,
    RoboWindowEventFocusGained = 8,
    RoboWindowEventFocusLost = 9,
    RoboWindowEventMinimized = 10,
    RoboWindowEventMaximized = 11,
    RoboWindowEventRestored = 12,
    RoboWindowEventDisplayChanged = 13,
    RoboWindowEventScaleChanged = 14,
    RoboWindowEventKeyDown = 15,
    RoboWindowEventKeyUp = 16,
    RoboWindowEventMouseMove = 17,
    RoboWindowEventMouseButtonDown = 18,
    RoboWindowEventMouseButtonUp = 19,
    RoboWindowEventMouseWheel = 20
} WindowEventType;

typedef struct WindowEvent {
    WindowEventType type;
    WindowId windowId;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    float scale;
    DisplayId displayId;
    /** Valid when type is KEY_DOWN / KEY_UP. */
    KeyboardEvent key;
    /** Valid when type is MOUSE_MOVE / BUTTON_DOWN / BUTTON_UP / WHEEL. */
    MouseEvent mouse;
} WindowEvent;

void WindowEventInit(WindowEvent *ev);

int WindowEventTypeIsKnown(WindowEventType type);

const char *WindowEventTypeName(WindowEventType type);

/**
 * Backend / core → queue. Requires subsystem initialized.
 * Invalid type / NONE → RoboInvalidArgument.
 * Full queue → RoboBusy.
 */
Result WindowEventPush(const WindowEvent *ev);

/**
 * Consumer dequeue (FIFO). Empty → RoboNotFound.
 */
Result WindowEventPoll(WindowEvent *out);

/**
 * Peek at the next event without dequeue. Empty → RoboNotFound.
 */
Result WindowEventPeek(WindowEvent *out);

/** Non-zero when the queue has no events. */
int WindowEventQueueIsEmpty(void);

/** Drop all queued events (tests / shutdown). */
void WindowEventQueueClear(void);

size_t WindowEventQueueSize(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_EVENT_H */
