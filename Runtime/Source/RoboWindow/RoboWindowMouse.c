#include "RoboWindow/RoboWindowMouse.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static uint8_t gPressed[RoboMouseButtonCount];
static int gHavePos;
static int32_t gPosX;
static int32_t gPosY;
static KeyModifierFlags gModifiers;
static WindowId gCaptureId = RoboWindowIdNone;

/* release_capture is defined below; used by reset helpers. */
Result WindowMouseReleaseCapture(void);

/* Click-count foundation (no gesture engine). */
static MouseButton gLastClickButton = RoboMouseButtonNone;
static uint64_t gLastClickMs;
static int32_t gLastClickX;
static int32_t gLastClickY;
static int gLastClickCount;

void MouseEventInit(MouseEvent *ev)
{
    if (!ev) {
        return;
    }
    memset(ev, 0, sizeof(*ev));
    ev->type = RoboMouseEventNone;
    ev->button = RoboMouseButtonNone;
    ev->scrollUnit = RoboMouseScrollUnitLine;
    ev->windowId = RoboWindowIdNone;
}

int MouseButtonIsKnown(MouseButton button)
{
    return button >= RoboMouseButtonLeft && button <= RoboMouseButtonX2;
}

const char *MouseButtonName(MouseButton button)
{
    switch (button) {
    case RoboMouseButtonNone:
        return "none";
    case RoboMouseButtonLeft:
        return "left";
    case RoboMouseButtonRight:
        return "right";
    case RoboMouseButtonMiddle:
        return "middle";
    case RoboMouseButtonX1:
        return "x1";
    case RoboMouseButtonX2:
        return "x2";
    case RoboMouseButtonUnknown:
        return "unknown";
    default:
        return "invalid";
    }
}

const char *MouseEventTypeName(MouseEventType type)
{
    switch (type) {
    case RoboMouseEventNone:
        return "none";
    case RoboMouseEventMove:
        return "move";
    case RoboMouseEventButtonDown:
        return "button_down";
    case RoboMouseEventButtonUp:
        return "button_up";
    case RoboMouseEventWheel:
        return "wheel";
    default:
        return "invalid";
    }
}

void MouseResetPressedState(void)
{
    memset(gPressed, 0, sizeof(gPressed));
    gHavePos = 0;
    gPosX = 0;
    gPosY = 0;
    gLastClickButton = RoboMouseButtonNone;
    gLastClickMs = 0u;
    gLastClickX = 0;
    gLastClickY = 0;
    gLastClickCount = 0;
    if (gCaptureId != RoboWindowIdNone) {
        (void)WindowMouseReleaseCapture();
    }
    LogDebug("robo_mouse: pressed state reset");
}

void MouseReset(void)
{
    if (gCaptureId != RoboWindowIdNone && WindowIsInitialized()) {
        const WindowBackendVtbl *vtbl = GetWindowBackendVtbl();
        if (vtbl && vtbl->ReleaseMouseCapture) {
            (void)vtbl->ReleaseMouseCapture(WindowBackendUserdata());
        }
    }
    memset(gPressed, 0, sizeof(gPressed));
    gHavePos = 0;
    gPosX = 0;
    gPosY = 0;
    gModifiers = RoboKeyModNone;
    gLastClickButton = RoboMouseButtonNone;
    gLastClickMs = 0u;
    gLastClickX = 0;
    gLastClickY = 0;
    gLastClickCount = 0;
    gCaptureId = RoboWindowIdNone;
    LogDebug("robo_mouse: full reset");
}

int MouseIsButtonPressed(MouseButton button)
{
    if (!MouseButtonIsKnown(button)) {
        return 0;
    }
    return gPressed[button] != 0;
}

int MouseGetPosition(MousePosition *out)
{
    if (!out || !gHavePos) {
        return 0;
    }
    out->x = gPosX;
    out->y = gPosY;
    return 1;
}

KeyModifierFlags MouseModifiers(void)
{
    return gModifiers;
}

WindowId WindowMouseCapturedId(void)
{
    if (gCaptureId != RoboWindowIdNone &&
        !WindowIdIsLive(gCaptureId)) {
        gCaptureId = RoboWindowIdNone;
    }
    return gCaptureId;
}

Result WindowMouseCapture(WindowId id)
{
    Result r;
    const WindowBackendVtbl *vtbl;
    WindowBackendCaps caps;
    WindowNativeHandle native;

    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    r = WindowThreadRequireOwner();
    if (r != RoboOk) {
        return r;
    }
    if (!WindowIdIsLive(id)) {
        return RoboNotFound;
    }

    gCaptureId = id;

    memset(&caps, 0, sizeof(caps));
    if (WindowBackendCapabilities(&caps) == RoboOk &&
        caps.mouseCapture) {
        vtbl = GetWindowBackendVtbl();
        if (vtbl && vtbl->CaptureMouse) {
            memset(&native, 0, sizeof(native));
            if (WindowGetNativeHandle(id, &native) == RoboOk) {
                r = vtbl->CaptureMouse(WindowBackendUserdata(), id,
                                        &native);
                if (r != RoboOk) {
                    gCaptureId = RoboWindowIdNone;
                    LogWarn("robo_mouse: native capture failed (%d)",
                                  (int)r);
                    return r;
                }
            }
        }
    }

    LogDebug("robo_mouse: capture id=%llu", (unsigned long long)id);
    return RoboOk;
}

Result WindowMouseReleaseCapture(void)
{
    Result r;
    const WindowBackendVtbl *vtbl;
    WindowBackendCaps caps;

    if (!WindowIsInitialized()) {
        gCaptureId = RoboWindowIdNone;
        return RoboOk;
    }
    r = WindowThreadRequireOwner();
    if (r != RoboOk) {
        return r;
    }

    memset(&caps, 0, sizeof(caps));
    if (WindowBackendCapabilities(&caps) == RoboOk &&
        caps.mouseCapture) {
        vtbl = GetWindowBackendVtbl();
        if (vtbl && vtbl->ReleaseMouseCapture) {
            (void)vtbl->ReleaseMouseCapture(WindowBackendUserdata());
        }
    }

    if (gCaptureId != RoboWindowIdNone) {
        LogDebug("robo_mouse: release capture was id=%llu",
                       (unsigned long long)gCaptureId);
    }
    gCaptureId = RoboWindowIdNone;
    return RoboOk;
}

static int ClickSlopOk(int32_t x, int32_t y)
{
    const int32_t slop = 4;
    int32_t dx = x - gLastClickX;
    int32_t dy = y - gLastClickY;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    return dx <= slop && dy <= slop;
}

static int SynthesizeClickCount(MouseButton button, int32_t x,
                                  int32_t y, uint64_t timestampMs)
{
    const uint64_t windowMs = 500u;

    if (button == RoboMouseButtonNone ||
        button == RoboMouseButtonUnknown) {
        return 0;
    }
    if (gLastClickButton == button && ClickSlopOk(x, y) &&
        timestampMs != 0u && gLastClickMs != 0u &&
        timestampMs >= gLastClickMs &&
        (timestampMs - gLastClickMs) <= windowMs) {
        gLastClickCount++;
    } else {
        gLastClickCount = 1;
    }
    gLastClickButton = button;
    gLastClickMs = timestampMs;
    gLastClickX = x;
    gLastClickY = y;
    return gLastClickCount;
}

static void UpdatePosition(MouseEvent *local)
{
    if (gHavePos) {
        if (local->deltaX == 0 && local->deltaY == 0) {
            local->deltaX = local->clientX - gPosX;
            local->deltaY = local->clientY - gPosY;
        }
    } else if (local->deltaX == 0 && local->deltaY == 0) {
        local->deltaX = 0;
        local->deltaY = 0;
    }
    gPosX = local->clientX;
    gPosY = local->clientY;
    gHavePos = 1;
}

static void UpdateStateFromEvent(MouseEvent *local)
{
    gModifiers = local->modifiers;
    UpdatePosition(local);

    if (local->type == RoboMouseEventButtonDown &&
        MouseButtonIsKnown(local->button)) {
        gPressed[local->button] = 1;
        if (local->clickCount <= 0) {
            local->clickCount = SynthesizeClickCount(
                local->button, local->clientX, local->clientY,
                local->timestampMs);
        } else {
            gLastClickButton = local->button;
            gLastClickMs = local->timestampMs;
            gLastClickX = local->clientX;
            gLastClickY = local->clientY;
            gLastClickCount = local->clickCount;
        }
    } else if (local->type == RoboMouseEventButtonUp &&
               MouseButtonIsKnown(local->button)) {
        gPressed[local->button] = 0;
    }
}

static WindowEventType MapWindowEventType(
    MouseEventType t)
{
    switch (t) {
    case RoboMouseEventMove:
        return RoboWindowEventMouseMove;
    case RoboMouseEventButtonDown:
        return RoboWindowEventMouseButtonDown;
    case RoboMouseEventButtonUp:
        return RoboWindowEventMouseButtonUp;
    case RoboMouseEventWheel:
        return RoboWindowEventMouseWheel;
    default:
        return RoboWindowEventNone;
    }
}

Result WindowObserveMouse(WindowId windowId,
                                        const MouseEvent *ev)
{
    Result r;
    WindowId target;
    WindowEvent we;
    MouseEvent local;
    WindowEventType wt;

    if (!ev) {
        return RoboInvalidArgument;
    }
    if (ev->type != RoboMouseEventMove &&
        ev->type != RoboMouseEventButtonDown &&
        ev->type != RoboMouseEventButtonUp &&
        ev->type != RoboMouseEventWheel) {
        return RoboInvalidArgument;
    }
    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    r = WindowThreadRequireOwner();
    if (r != RoboOk) {
        return r;
    }

    local = *ev;
    target = windowId;
    if (target == RoboWindowIdNone) {
        if (gCaptureId != RoboWindowIdNone &&
            WindowIdIsLive(gCaptureId)) {
            target = gCaptureId;
        } else {
            target = WindowFindFocusedId();
        }
    }

    if (local.type == RoboMouseEventButtonDown ||
        local.type == RoboMouseEventButtonUp) {
        if (local.button == RoboMouseButtonNone ||
            local.button == RoboMouseButtonUnknown ||
            !MouseButtonIsKnown(local.button)) {
            LogDebug("robo_mouse: unmapped button dropped type=%s",
                           MouseEventTypeName(local.type));
            gModifiers = local.modifiers;
            return RoboOk;
        }
    }

    if (local.type == RoboMouseEventWheel) {
        if (local.scrollX == 0 && local.scrollY == 0) {
            LogDebug("robo_mouse: zero wheel delta ignored");
            gModifiers = local.modifiers;
            UpdatePosition(&local);
            return RoboOk;
        }
    }

    UpdateStateFromEvent(&local);

    if (target == RoboWindowIdNone || !WindowIdIsLive(target)) {
        LogDebug("robo_mouse: no live target type=%s",
                       MouseEventTypeName(local.type));
        return RoboNotFound;
    }

    wt = MapWindowEventType(local.type);
    if (wt == RoboWindowEventNone) {
        return RoboInvalidArgument;
    }

    WindowEventInit(&we);
    we.type = wt;
    we.windowId = target;
    we.x = local.clientX;
    we.y = local.clientY;
    we.mouse = local;
    we.mouse.windowId = target;

    r = WindowEventPush(&we);
    if (r != RoboOk) {
        LogWarn("robo_mouse: queue push failed (%d)", (int)r);
        return r;
    }
    LogDebug(
        "robo_mouse: %s id=%llu btn=%s client=(%d,%d) delta=(%d,%d) "
        "scroll=(%d,%d) clicks=%d mods=0x%x",
        MouseEventTypeName(local.type), (unsigned long long)target,
        MouseButtonName(local.button), (int)local.clientX,
        (int)local.clientY, (int)local.deltaX, (int)local.deltaY,
        (int)local.scrollX, (int)local.scrollY, local.clickCount,
        (unsigned)local.modifiers);
    return RoboOk;
}
