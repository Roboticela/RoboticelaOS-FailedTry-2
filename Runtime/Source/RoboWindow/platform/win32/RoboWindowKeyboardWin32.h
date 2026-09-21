/*
 * Win32 keyboard translation helpers (Goal 7.10) — host builds only.
 * Not a public API. Maps VK / scan codes → Robo physical/logical keys.
 */

#ifndef ROBO_WINDOW_KEYBOARD_WIN32_H
#define ROBO_WINDOW_KEYBOARD_WIN32_H

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32

#include "RoboWindow/RoboWindowKeyboard.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

LogicalKey KeyboardWin32LogicalFromVk(unsigned vk);
PhysicalKey KeyboardWin32PhysicalFromScan(unsigned scan,
                                                           int extended);
KeyModifierFlags KeyboardWin32QueryModifiers(void);

/**
 * Translate WM_(SYS)KEYDOWN/UP into observe_key for @p windowId.
 * Returns RoboOk even for unknown keys (logged); never crashes.
 */
Result KeyboardWin32HandleMessage(WindowId windowId,
                                                 unsigned msg,
                                                 uintptr_t wparam,
                                                 intptr_t lparam);

#ifdef __cplusplus
}
#endif

#endif /* RoboWindowHasWin32 */

#endif /* ROBO_WINDOW_KEYBOARD_WIN32_H */
