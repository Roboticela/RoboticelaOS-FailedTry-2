/*
 * Win32 keyboard translation helpers (Goal 7.10) — host builds only.
 * Not a public API. Maps VK / scan codes → Robo physical/logical keys.
 */

#ifndef ROBO_WINDOW_KEYBOARD_WIN32_H
#define ROBO_WINDOW_KEYBOARD_WIN32_H

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32

#include "RoboWindow/RoboWindowKeyboard.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

robo_logical_key_t robo_keyboard_win32_logical_from_vk(unsigned vk);
robo_physical_key_t robo_keyboard_win32_physical_from_scan(unsigned scan,
                                                           int extended);
robo_key_modifier_flags_t robo_keyboard_win32_query_modifiers(void);

/**
 * Translate WM_(SYS)KEYDOWN/UP into observe_key for @p window_id.
 * Returns ROBO_OK even for unknown keys (logged); never crashes.
 */
robo_result_t robo_keyboard_win32_handle_message(robo_window_id_t window_id,
                                                 unsigned msg,
                                                 uintptr_t wparam,
                                                 intptr_t lparam);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_HAS_WIN32 */

#endif /* ROBO_WINDOW_KEYBOARD_WIN32_H */
