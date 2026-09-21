/*
 * Win32 → RoboMouse translation (Goal 7.11).
 *
 * Maps WM_MOUSE* without exposing Win32 types through the public API.
 */

#ifndef ROBO_WINDOW_MOUSE_WIN32_H
#define ROBO_WINDOW_MOUSE_WIN32_H

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32

#include "RoboWindow/RoboWindowMouse.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

MouseButton MouseWin32ButtonFromMsg(unsigned msg,
                                                     uintptr_t wparam);

/**
 * Translate WM_MOUSE* / wheel messages into observe_mouse for @p windowId.
 * Returns RoboOk for handled or safely ignored messages.
 */
Result MouseWin32HandleMessage(WindowId windowId,
                                              void *hwnd,
                                              unsigned msg,
                                              uintptr_t wparam,
                                              intptr_t lparam);

#ifdef __cplusplus
}
#endif

#endif /* RoboWindowHasWin32 */

#endif /* ROBO_WINDOW_MOUSE_WIN32_H */
