#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static WindowEvent gQueue[RoboWindowEventQueueCapacity];
static size_t gHead;
static size_t gTail;
static size_t gCount;

void WindowEventQueueReset(void)
{
    gHead = 0u;
    gTail = 0u;
    gCount = 0u;
    memset(gQueue, 0, sizeof(gQueue));
}

void WindowEventPurgeWindow(WindowId id)
{
    WindowEvent kept[RoboWindowEventQueueCapacity];
    size_t n = 0u;
    size_t i;

    if (id == RoboWindowIdNone || gCount == 0u) {
        return;
    }
    for (i = 0u; i < gCount; i++) {
        size_t idx = (gHead + i) % RoboWindowEventQueueCapacity;
        if (gQueue[idx].windowId != id) {
            kept[n++] = gQueue[idx];
        }
    }
    if (n == gCount) {
        return;
    }
    gHead = 0u;
    gTail = n;
    gCount = n;
    memset(gQueue, 0, sizeof(gQueue));
    if (n > 0u) {
        memcpy(gQueue, kept, n * sizeof(kept[0]));
    }
    LogDebug("robo_window: purged stale events for id=%llu (kept=%llu)",
                   (unsigned long long)id, (unsigned long long)n);
}

void WindowEventInit(WindowEvent *ev)
{
    if (!ev) {
        return;
    }
    memset(ev, 0, sizeof(*ev));
    ev->type = RoboWindowEventNone;
    ev->windowId = RoboWindowIdNone;
    ev->displayId = RoboDisplayIdNone;
    KeyboardEventInit(&ev->key);
    MouseEventInit(&ev->mouse);
}

int WindowEventTypeIsKnown(WindowEventType type)
{
    return type >= RoboWindowEventCreated &&
           type <= RoboWindowEventMouseWheel;
}

const char *WindowEventTypeName(WindowEventType type)
{
    switch (type) {
    case RoboWindowEventNone:
        return "none";
    case RoboWindowEventCreated:
        return "created";
    case RoboWindowEventShown:
        return "shown";
    case RoboWindowEventHidden:
        return "hidden";
    case RoboWindowEventMoved:
        return "moved";
    case RoboWindowEventResized:
        return "resized";
    case RoboWindowEventCloseRequested:
        return "close_requested";
    case RoboWindowEventClosed:
        return "closed";
    case RoboWindowEventFocusGained:
        return "focus_gained";
    case RoboWindowEventFocusLost:
        return "focus_lost";
    case RoboWindowEventMinimized:
        return "minimized";
    case RoboWindowEventMaximized:
        return "maximized";
    case RoboWindowEventRestored:
        return "restored";
    case RoboWindowEventDisplayChanged:
        return "display_changed";
    case RoboWindowEventScaleChanged:
        return "scale_changed";
    case RoboWindowEventKeyDown:
        return "key_down";
    case RoboWindowEventKeyUp:
        return "key_up";
    case RoboWindowEventMouseMove:
        return "mouse_move";
    case RoboWindowEventMouseButtonDown:
        return "mouse_button_down";
    case RoboWindowEventMouseButtonUp:
        return "mouse_button_up";
    case RoboWindowEventMouseWheel:
        return "mouse_wheel";
    default:
        return "unknown";
    }
}

Result WindowEventPush(const WindowEvent *ev)
{
    if (!ev || !WindowEventTypeIsKnown(ev->type)) {
        return RoboInvalidArgument;
    }
    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    if (gCount >= RoboWindowEventQueueCapacity) {
        LogWarn("robo_window: event queue full (type=%s)",
                      WindowEventTypeName(ev->type));
        return RoboBusy;
    }
    gQueue[gTail] = *ev;
    gTail = (gTail + 1u) % RoboWindowEventQueueCapacity;
    gCount++;
    return RoboOk;
}

Result WindowEventPoll(WindowEvent *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    if (gCount == 0u) {
        return RoboNotFound;
    }
    *out = gQueue[gHead];
    gHead = (gHead + 1u) % RoboWindowEventQueueCapacity;
    gCount--;
    return RoboOk;
}

Result WindowEventPeek(WindowEvent *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    if (gCount == 0u) {
        return RoboNotFound;
    }
    *out = gQueue[gHead];
    return RoboOk;
}

int WindowEventQueueIsEmpty(void)
{
    return gCount == 0u;
}

void WindowEventQueueClear(void)
{
    WindowEventQueueReset();
}

size_t WindowEventQueueSize(void)
{
    return gCount;
}
