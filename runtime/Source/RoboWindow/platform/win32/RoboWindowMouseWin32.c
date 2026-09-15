/*
 * Win32 → RoboMouse translation (Goal 7.11).
 */

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32

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

robo_mouse_button_t robo_mouse_win32_button_from_msg(unsigned msg,
                                                     uintptr_t wparam)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        return ROBO_MOUSE_BUTTON_LEFT;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        return ROBO_MOUSE_BUTTON_RIGHT;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        return ROBO_MOUSE_BUTTON_MIDDLE;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK: {
        WORD xb = GET_XBUTTON_WPARAM((WPARAM)wparam);
        if (xb == XBUTTON1) {
            return ROBO_MOUSE_BUTTON_X1;
        }
        if (xb == XBUTTON2) {
            return ROBO_MOUSE_BUTTON_X2;
        }
        return ROBO_MOUSE_BUTTON_UNKNOWN;
    }
    default:
        return ROBO_MOUSE_BUTTON_NONE;
    }
}

static void fill_coords(HWND hwnd, LPARAM lparam, robo_mouse_event_t *ev)
{
    POINT pt;
    pt.x = (LONG)(short)LOWORD(lparam);
    pt.y = (LONG)(short)HIWORD(lparam);
    ev->client_x = (int32_t)pt.x;
    ev->client_y = (int32_t)pt.y;
    if (ClientToScreen(hwnd, &pt)) {
        ev->screen_x = (int32_t)pt.x;
        ev->screen_y = (int32_t)pt.y;
        ev->has_screen_coords = 1;
    } else {
        ev->has_screen_coords = 0;
    }
}

static void fill_wheel_coords(HWND hwnd, WPARAM wparam, LPARAM lparam,
                              robo_mouse_event_t *ev)
{
    POINT pt;
    pt.x = (LONG)(short)LOWORD(lparam);
    pt.y = (LONG)(short)HIWORD(lparam);
    ev->screen_x = (int32_t)pt.x;
    ev->screen_y = (int32_t)pt.y;
    ev->has_screen_coords = 1;
    if (ScreenToClient(hwnd, &pt)) {
        ev->client_x = (int32_t)pt.x;
        ev->client_y = (int32_t)pt.y;
    } else {
        ev->client_x = 0;
        ev->client_y = 0;
    }
    (void)wparam;
}

static int any_button_down(void)
{
    return robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_LEFT) ||
           robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_RIGHT) ||
           robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_MIDDLE) ||
           robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_X1) ||
           robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_X2);
}

robo_result_t robo_mouse_win32_handle_message(robo_window_id_t window_id,
                                              void *hwnd_void,
                                              unsigned msg,
                                              uintptr_t wparam,
                                              intptr_t lparam)
{
    HWND hwnd = (HWND)hwnd_void;
    robo_mouse_event_t ev;
    robo_mouse_button_t btn;
    int is_down = 0;
    int is_dbl = 0;

    if (!hwnd || window_id == ROBO_WINDOW_ID_NONE) {
        return ROBO_INVALID_ARGUMENT;
    }

    robo_mouse_event_init(&ev);
    ev.modifiers = robo_keyboard_win32_query_modifiers();
    ev.timestamp_ms = (uint64_t)GetMessageTime();
    ev.window_id = window_id;

    switch (msg) {
    case WM_MOUSEMOVE:
        fill_coords(hwnd, (LPARAM)lparam, &ev);
        ev.type = ROBO_MOUSE_EVENT_MOVE;
        return robo_window_observe_mouse(window_id, &ev);

    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDBLCLK:
        is_dbl = 1;
        /* fall through as button down */
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
        is_down = 1;
        /* fall through */
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
        btn = robo_mouse_win32_button_from_msg(msg, wparam);
        fill_coords(hwnd, (LPARAM)lparam, &ev);
        ev.button = btn;
        if (is_down || is_dbl) {
            ev.type = ROBO_MOUSE_EVENT_BUTTON_DOWN;
            if (is_dbl) {
                ev.click_count = 2;
            }
            /* Ensure button-up outside client still arrives. */
            (void)SetCapture(hwnd);
        } else {
            ev.type = ROBO_MOUSE_EVENT_BUTTON_UP;
        }
        {
            robo_result_t r = robo_window_observe_mouse(window_id, &ev);
            if (!is_down && !is_dbl && !any_button_down()) {
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
        fill_wheel_coords(hwnd, (WPARAM)wparam, (LPARAM)lparam, &ev);
        ev.type = ROBO_MOUSE_EVENT_WHEEL;
        ev.scroll_y = (int32_t)GET_WHEEL_DELTA_WPARAM((WPARAM)wparam);
        ev.scroll_x = 0;
        ev.scroll_unit = ROBO_MOUSE_SCROLL_UNIT_LINE;
        return robo_window_observe_mouse(window_id, &ev);

    case WM_MOUSEHWHEEL:
        fill_wheel_coords(hwnd, (WPARAM)wparam, (LPARAM)lparam, &ev);
        ev.type = ROBO_MOUSE_EVENT_WHEEL;
        ev.scroll_x = (int32_t)GET_WHEEL_DELTA_WPARAM((WPARAM)wparam);
        ev.scroll_y = 0;
        ev.scroll_unit = ROBO_MOUSE_SCROLL_UNIT_LINE;
        return robo_window_observe_mouse(window_id, &ev);

    default:
        return ROBO_OK;
    }
}

#endif /* ROBO_WINDOW_HAS_WIN32 */
