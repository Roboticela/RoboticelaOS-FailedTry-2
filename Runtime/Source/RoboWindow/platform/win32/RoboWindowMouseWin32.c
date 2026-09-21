/*
 * Win32 → RoboMouse translation (Goal 7.11).
 */

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "RoboWindowMouseWin32.h"
#include "RoboWindowKeyboardWin32.h"

#include "RoboBase/RoboLog.h"

#include <windows.h>

MouseButton MouseWin32ButtonFromMsg(unsigned msg,
                                                     uintptr_t wparam)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        return RoboMouseButtonLeft;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        return RoboMouseButtonRight;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        return RoboMouseButtonMiddle;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK: {
        WORD xb = GET_XBUTTON_WPARAM((WPARAM)wparam);
        if (xb == XBUTTON1) {
            return RoboMouseButtonX1;
        }
        if (xb == XBUTTON2) {
            return RoboMouseButtonX2;
        }
        return RoboMouseButtonUnknown;
    }
    default:
        return RoboMouseButtonNone;
    }
}

static void FillCoords(HWND hwnd, LPARAM lparam, MouseEvent *ev)
{
    POINT pt;
    pt.x = (LONG)(short)LOWORD(lparam);
    pt.y = (LONG)(short)HIWORD(lparam);
    ev->clientX = (int32_t)pt.x;
    ev->clientY = (int32_t)pt.y;
    if (ClientToScreen(hwnd, &pt)) {
        ev->screenX = (int32_t)pt.x;
        ev->screenY = (int32_t)pt.y;
        ev->hasScreenCoords = 1;
    } else {
        ev->hasScreenCoords = 0;
    }
}

static void FillWheelCoords(HWND hwnd, WPARAM wparam, LPARAM lparam,
                              MouseEvent *ev)
{
    POINT pt;
    pt.x = (LONG)(short)LOWORD(lparam);
    pt.y = (LONG)(short)HIWORD(lparam);
    ev->screenX = (int32_t)pt.x;
    ev->screenY = (int32_t)pt.y;
    ev->hasScreenCoords = 1;
    if (ScreenToClient(hwnd, &pt)) {
        ev->clientX = (int32_t)pt.x;
        ev->clientY = (int32_t)pt.y;
    } else {
        ev->clientX = 0;
        ev->clientY = 0;
    }
    (void)wparam;
}

static int AnyButtonDown(void)
{
    return MouseIsButtonPressed(RoboMouseButtonLeft) ||
           MouseIsButtonPressed(RoboMouseButtonRight) ||
           MouseIsButtonPressed(RoboMouseButtonMiddle) ||
           MouseIsButtonPressed(RoboMouseButtonX1) ||
           MouseIsButtonPressed(RoboMouseButtonX2);
}

Result MouseWin32HandleMessage(WindowId windowId,
                                              void *hwndVoid,
                                              unsigned msg,
                                              uintptr_t wparam,
                                              intptr_t lparam)
{
    HWND hwnd = (HWND)hwndVoid;
    MouseEvent ev;
    MouseButton btn;
    int isDown = 0;
    int isDbl = 0;

    if (!hwnd || windowId == RoboWindowIdNone) {
        return RoboInvalidArgument;
    }

    MouseEventInit(&ev);
    ev.modifiers = KeyboardWin32QueryModifiers();
    ev.timestampMs = (uint64_t)GetMessageTime();
    ev.windowId = windowId;

    switch (msg) {
    case WM_MOUSEMOVE:
        FillCoords(hwnd, (LPARAM)lparam, &ev);
        ev.type = RoboMouseEventMove;
        return WindowObserveMouse(windowId, &ev);

    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDBLCLK:
        isDbl = 1;
        /* fall through as button down */
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
        isDown = 1;
        /* fall through */
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
        btn = MouseWin32ButtonFromMsg(msg, wparam);
        FillCoords(hwnd, (LPARAM)lparam, &ev);
        ev.button = btn;
        if (isDown || isDbl) {
            ev.type = RoboMouseEventButtonDown;
            if (isDbl) {
                ev.clickCount = 2;
            }
            /* Ensure button-up outside client still arrives. */
            (void)SetCapture(hwnd);
        } else {
            ev.type = RoboMouseEventButtonUp;
        }
        {
            Result r = WindowObserveMouse(windowId, &ev);
            if (!isDown && !isDbl && !AnyButtonDown()) {
                if (GetCapture() == hwnd) {
                    (void)ReleaseCapture();
                }
            }
            if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP ||
                msg == WM_XBUTTONDBLCLK) {
                /* Caller should return TRUE to indicate handled. */
            }
            return r;
        }

    case WM_MOUSEWHEEL:
        FillWheelCoords(hwnd, (WPARAM)wparam, (LPARAM)lparam, &ev);
        ev.type = RoboMouseEventWheel;
        ev.scrollY = (int32_t)GET_WHEEL_DELTA_WPARAM((WPARAM)wparam);
        ev.scrollX = 0;
        ev.scrollUnit = RoboMouseScrollUnitLine;
        return WindowObserveMouse(windowId, &ev);

    case WM_MOUSEHWHEEL:
        FillWheelCoords(hwnd, (WPARAM)wparam, (LPARAM)lparam, &ev);
        ev.type = RoboMouseEventWheel;
        ev.scrollX = (int32_t)GET_WHEEL_DELTA_WPARAM((WPARAM)wparam);
        ev.scrollY = 0;
        ev.scrollUnit = RoboMouseScrollUnitLine;
        return WindowObserveMouse(windowId, &ev);

    default:
        return RoboOk;
    }
}

#endif /* RoboWindowHasWin32 */
