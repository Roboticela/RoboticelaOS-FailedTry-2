/*
 * Win32 → RoboMouse translation (Goal 7.11).
 *
 * Maps WM_MOUSE* without exposing Win32 types through the public API.
 */

#ifndef ROBO_WINDOW_MOUSE_WIN32_H
#define ROBO_WINDOW_MOUSE_WIN32_H

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32

#include "RoboWindow/RoboWindowMouse.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

robo_mouse_button_t robo_mouse_win32_button_from_msg(unsigned msg,
                                                     uintptr_t wparam);

/**
 * Translate WM_MOUSE* / wheel messages into observe_mouse for @p window_id.
 * Returns ROBO_OK for handled or safely ignored messages.
 */
robo_result_t robo_mouse_win32_handle_message(robo_window_id_t window_id,
                                              void *hwnd,
                                              unsigned msg,
                                              uintptr_t wparam,
                                              intptr_t lparam);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_HAS_WIN32 */

#endif /* ROBO_WINDOW_MOUSE_WIN32_H */
