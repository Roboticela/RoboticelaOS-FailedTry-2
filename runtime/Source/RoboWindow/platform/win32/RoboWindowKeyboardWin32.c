/*
 * Win32 → RoboKeyboard translation (Goal 7.10).
 *
 * Maps virtual-key (logical) and scan codes (physical) without exposing
 * Win32 types through the public API. Text/IME not handled.
 */

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "RoboWindowKeyboardWin32.h"

#include "RoboBase/RoboLog.h"

#include <windows.h>

robo_logical_key_t robo_keyboard_win32_logical_from_vk(unsigned vk)
{
    switch (vk) {
    case 'A':
        return ROBO_LOGICAL_KEY_A;
    case 'B':
        return ROBO_LOGICAL_KEY_B;
    case 'C':
        return ROBO_LOGICAL_KEY_C;
    case 'D':
        return ROBO_LOGICAL_KEY_D;
    case 'E':
        return ROBO_LOGICAL_KEY_E;
    case 'F':
        return ROBO_LOGICAL_KEY_F;
    case 'G':
        return ROBO_LOGICAL_KEY_G;
    case 'H':
        return ROBO_LOGICAL_KEY_H;
    case 'I':
        return ROBO_LOGICAL_KEY_I;
    case 'J':
        return ROBO_LOGICAL_KEY_J;
    case 'K':
        return ROBO_LOGICAL_KEY_K;
    case 'L':
        return ROBO_LOGICAL_KEY_L;
    case 'M':
        return ROBO_LOGICAL_KEY_M;
    case 'N':
        return ROBO_LOGICAL_KEY_N;
    case 'O':
        return ROBO_LOGICAL_KEY_O;
    case 'P':
        return ROBO_LOGICAL_KEY_P;
    case 'Q':
        return ROBO_LOGICAL_KEY_Q;
    case 'R':
        return ROBO_LOGICAL_KEY_R;
    case 'S':
        return ROBO_LOGICAL_KEY_S;
    case 'T':
        return ROBO_LOGICAL_KEY_T;
    case 'U':
        return ROBO_LOGICAL_KEY_U;
    case 'V':
        return ROBO_LOGICAL_KEY_V;
    case 'W':
        return ROBO_LOGICAL_KEY_W;
    case 'X':
        return ROBO_LOGICAL_KEY_X;
    case 'Y':
        return ROBO_LOGICAL_KEY_Y;
    case 'Z':
        return ROBO_LOGICAL_KEY_Z;
    case '1':
        return ROBO_LOGICAL_KEY_DIGIT_1;
    case '2':
        return ROBO_LOGICAL_KEY_DIGIT_2;
    case '3':
        return ROBO_LOGICAL_KEY_DIGIT_3;
    case '4':
        return ROBO_LOGICAL_KEY_DIGIT_4;
    case '5':
        return ROBO_LOGICAL_KEY_DIGIT_5;
    case '6':
        return ROBO_LOGICAL_KEY_DIGIT_6;
    case '7':
        return ROBO_LOGICAL_KEY_DIGIT_7;
    case '8':
        return ROBO_LOGICAL_KEY_DIGIT_8;
    case '9':
        return ROBO_LOGICAL_KEY_DIGIT_9;
    case '0':
        return ROBO_LOGICAL_KEY_DIGIT_0;
    case VK_RETURN:
        return ROBO_LOGICAL_KEY_ENTER;
    case VK_ESCAPE:
        return ROBO_LOGICAL_KEY_ESCAPE;
    case VK_BACK:
        return ROBO_LOGICAL_KEY_BACKSPACE;
    case VK_TAB:
        return ROBO_LOGICAL_KEY_TAB;
    case VK_SPACE:
        return ROBO_LOGICAL_KEY_SPACE;
    case VK_OEM_MINUS:
        return ROBO_LOGICAL_KEY_MINUS;
    case VK_OEM_PLUS:
        return ROBO_LOGICAL_KEY_EQUAL;
    case VK_OEM_4:
        return ROBO_LOGICAL_KEY_BRACKET_LEFT;
    case VK_OEM_6:
        return ROBO_LOGICAL_KEY_BRACKET_RIGHT;
    case VK_OEM_5:
        return ROBO_LOGICAL_KEY_BACKSLASH;
    case VK_OEM_1:
        return ROBO_LOGICAL_KEY_SEMICOLON;
    case VK_OEM_7:
        return ROBO_LOGICAL_KEY_QUOTE;
    case VK_OEM_3:
        return ROBO_LOGICAL_KEY_BACKQUOTE;
    case VK_OEM_COMMA:
        return ROBO_LOGICAL_KEY_COMMA;
    case VK_OEM_PERIOD:
        return ROBO_LOGICAL_KEY_PERIOD;
    case VK_OEM_2:
        return ROBO_LOGICAL_KEY_SLASH;
    case VK_CAPITAL:
        return ROBO_LOGICAL_KEY_CAPS_LOCK;
    case VK_F1:
        return ROBO_LOGICAL_KEY_F1;
    case VK_F2:
        return ROBO_LOGICAL_KEY_F2;
    case VK_F3:
        return ROBO_LOGICAL_KEY_F3;
    case VK_F4:
        return ROBO_LOGICAL_KEY_F4;
    case VK_F5:
        return ROBO_LOGICAL_KEY_F5;
    case VK_F6:
        return ROBO_LOGICAL_KEY_F6;
    case VK_F7:
        return ROBO_LOGICAL_KEY_F7;
    case VK_F8:
        return ROBO_LOGICAL_KEY_F8;
    case VK_F9:
        return ROBO_LOGICAL_KEY_F9;
    case VK_F10:
        return ROBO_LOGICAL_KEY_F10;
    case VK_F11:
        return ROBO_LOGICAL_KEY_F11;
    case VK_F12:
        return ROBO_LOGICAL_KEY_F12;
    case VK_SNAPSHOT:
        return ROBO_LOGICAL_KEY_PRINT_SCREEN;
    case VK_SCROLL:
        return ROBO_LOGICAL_KEY_SCROLL_LOCK;
    case VK_PAUSE:
        return ROBO_LOGICAL_KEY_PAUSE;
    case VK_INSERT:
        return ROBO_LOGICAL_KEY_INSERT;
    case VK_DELETE:
        return ROBO_LOGICAL_KEY_DELETE;
    case VK_HOME:
        return ROBO_LOGICAL_KEY_HOME;
    case VK_END:
        return ROBO_LOGICAL_KEY_END;
    case VK_PRIOR:
        return ROBO_LOGICAL_KEY_PAGE_UP;
    case VK_NEXT:
        return ROBO_LOGICAL_KEY_PAGE_DOWN;
    case VK_RIGHT:
        return ROBO_LOGICAL_KEY_ARROW_RIGHT;
    case VK_LEFT:
        return ROBO_LOGICAL_KEY_ARROW_LEFT;
    case VK_DOWN:
        return ROBO_LOGICAL_KEY_ARROW_DOWN;
    case VK_UP:
        return ROBO_LOGICAL_KEY_ARROW_UP;
    case VK_NUMLOCK:
        return ROBO_LOGICAL_KEY_NUM_LOCK;
    case VK_DIVIDE:
        return ROBO_LOGICAL_KEY_NUMPAD_DIVIDE;
    case VK_MULTIPLY:
        return ROBO_LOGICAL_KEY_NUMPAD_MULTIPLY;
    case VK_SUBTRACT:
        return ROBO_LOGICAL_KEY_NUMPAD_SUBTRACT;
    case VK_ADD:
        return ROBO_LOGICAL_KEY_NUMPAD_ADD;
    case VK_NUMPAD1:
        return ROBO_LOGICAL_KEY_NUMPAD_1;
    case VK_NUMPAD2:
        return ROBO_LOGICAL_KEY_NUMPAD_2;
    case VK_NUMPAD3:
        return ROBO_LOGICAL_KEY_NUMPAD_3;
    case VK_NUMPAD4:
        return ROBO_LOGICAL_KEY_NUMPAD_4;
    case VK_NUMPAD5:
        return ROBO_LOGICAL_KEY_NUMPAD_5;
    case VK_NUMPAD6:
        return ROBO_LOGICAL_KEY_NUMPAD_6;
    case VK_NUMPAD7:
        return ROBO_LOGICAL_KEY_NUMPAD_7;
    case VK_NUMPAD8:
        return ROBO_LOGICAL_KEY_NUMPAD_8;
    case VK_NUMPAD9:
        return ROBO_LOGICAL_KEY_NUMPAD_9;
    case VK_NUMPAD0:
        return ROBO_LOGICAL_KEY_NUMPAD_0;
    case VK_DECIMAL:
        return ROBO_LOGICAL_KEY_NUMPAD_DECIMAL;
    case VK_APPS:
        return ROBO_LOGICAL_KEY_CONTEXT_MENU;
    case VK_SHIFT:
        return ROBO_LOGICAL_KEY_SHIFT;
    case VK_LSHIFT:
        return ROBO_LOGICAL_KEY_SHIFT_LEFT;
    case VK_RSHIFT:
        return ROBO_LOGICAL_KEY_SHIFT_RIGHT;
    case VK_CONTROL:
        return ROBO_LOGICAL_KEY_CONTROL;
    case VK_LCONTROL:
        return ROBO_LOGICAL_KEY_CONTROL_LEFT;
    case VK_RCONTROL:
        return ROBO_LOGICAL_KEY_CONTROL_RIGHT;
    case VK_MENU:
        return ROBO_LOGICAL_KEY_ALT;
    case VK_LMENU:
        return ROBO_LOGICAL_KEY_ALT_LEFT;
    case VK_RMENU:
        return ROBO_LOGICAL_KEY_ALT_RIGHT;
    case VK_LWIN:
        return ROBO_LOGICAL_KEY_META_LEFT;
    case VK_RWIN:
        return ROBO_LOGICAL_KEY_META_RIGHT;
    default:
        return ROBO_LOGICAL_KEY_UNKNOWN;
    }
}

robo_physical_key_t robo_keyboard_win32_physical_from_scan(unsigned scan,
                                                           int extended)
{
    /* Set 1 make codes (common PC/AT). Extended bit distinguishes numpad. */
    if (extended) {
        switch (scan) {
        case 0x1C:
            return ROBO_PHYSICAL_KEY_NUMPAD_ENTER;
        case 0x35:
            return ROBO_PHYSICAL_KEY_NUMPAD_DIVIDE;
        case 0x37:
            return ROBO_PHYSICAL_KEY_PRINT_SCREEN;
        case 0x38:
            return ROBO_PHYSICAL_KEY_ALT_RIGHT;
        case 0x1D:
            return ROBO_PHYSICAL_KEY_CONTROL_RIGHT;
        case 0x47:
            return ROBO_PHYSICAL_KEY_HOME;
        case 0x48:
            return ROBO_PHYSICAL_KEY_ARROW_UP;
        case 0x49:
            return ROBO_PHYSICAL_KEY_PAGE_UP;
        case 0x4B:
            return ROBO_PHYSICAL_KEY_ARROW_LEFT;
        case 0x4D:
            return ROBO_PHYSICAL_KEY_ARROW_RIGHT;
        case 0x4F:
            return ROBO_PHYSICAL_KEY_END;
        case 0x50:
            return ROBO_PHYSICAL_KEY_ARROW_DOWN;
        case 0x51:
            return ROBO_PHYSICAL_KEY_PAGE_DOWN;
        case 0x52:
            return ROBO_PHYSICAL_KEY_INSERT;
        case 0x53:
            return ROBO_PHYSICAL_KEY_DELETE;
        case 0x5B:
            return ROBO_PHYSICAL_KEY_META_LEFT;
        case 0x5C:
            return ROBO_PHYSICAL_KEY_META_RIGHT;
        case 0x5D:
            return ROBO_PHYSICAL_KEY_CONTEXT_MENU;
        default:
            break;
        }
    }

    switch (scan) {
    case 0x01:
        return ROBO_PHYSICAL_KEY_ESCAPE;
    case 0x02:
        return ROBO_PHYSICAL_KEY_DIGIT_1;
    case 0x03:
        return ROBO_PHYSICAL_KEY_DIGIT_2;
    case 0x04:
        return ROBO_PHYSICAL_KEY_DIGIT_3;
    case 0x05:
        return ROBO_PHYSICAL_KEY_DIGIT_4;
    case 0x06:
        return ROBO_PHYSICAL_KEY_DIGIT_5;
    case 0x07:
        return ROBO_PHYSICAL_KEY_DIGIT_6;
    case 0x08:
        return ROBO_PHYSICAL_KEY_DIGIT_7;
    case 0x09:
        return ROBO_PHYSICAL_KEY_DIGIT_8;
    case 0x0A:
        return ROBO_PHYSICAL_KEY_DIGIT_9;
    case 0x0B:
        return ROBO_PHYSICAL_KEY_DIGIT_0;
    case 0x0C:
        return ROBO_PHYSICAL_KEY_MINUS;
    case 0x0D:
        return ROBO_PHYSICAL_KEY_EQUAL;
    case 0x0E:
        return ROBO_PHYSICAL_KEY_BACKSPACE;
    case 0x0F:
        return ROBO_PHYSICAL_KEY_TAB;
    case 0x10:
        return ROBO_PHYSICAL_KEY_Q;
    case 0x11:
        return ROBO_PHYSICAL_KEY_W;
    case 0x12:
        return ROBO_PHYSICAL_KEY_E;
    case 0x13:
        return ROBO_PHYSICAL_KEY_R;
    case 0x14:
        return ROBO_PHYSICAL_KEY_T;
    case 0x15:
        return ROBO_PHYSICAL_KEY_Y;
    case 0x16:
        return ROBO_PHYSICAL_KEY_U;
    case 0x17:
        return ROBO_PHYSICAL_KEY_I;
    case 0x18:
        return ROBO_PHYSICAL_KEY_O;
    case 0x19:
        return ROBO_PHYSICAL_KEY_P;
    case 0x1A:
        return ROBO_PHYSICAL_KEY_BRACKET_LEFT;
    case 0x1B:
        return ROBO_PHYSICAL_KEY_BRACKET_RIGHT;
    case 0x1C:
        return ROBO_PHYSICAL_KEY_ENTER;
    case 0x1D:
        return ROBO_PHYSICAL_KEY_CONTROL_LEFT;
    case 0x1E:
        return ROBO_PHYSICAL_KEY_A;
    case 0x1F:
        return ROBO_PHYSICAL_KEY_S;
    case 0x20:
        return ROBO_PHYSICAL_KEY_D;
    case 0x21:
        return ROBO_PHYSICAL_KEY_F;
    case 0x22:
        return ROBO_PHYSICAL_KEY_G;
    case 0x23:
        return ROBO_PHYSICAL_KEY_H;
    case 0x24:
        return ROBO_PHYSICAL_KEY_J;
    case 0x25:
        return ROBO_PHYSICAL_KEY_K;
    case 0x26:
        return ROBO_PHYSICAL_KEY_L;
    case 0x27:
        return ROBO_PHYSICAL_KEY_SEMICOLON;
    case 0x28:
        return ROBO_PHYSICAL_KEY_QUOTE;
    case 0x29:
        return ROBO_PHYSICAL_KEY_BACKQUOTE;
    case 0x2A:
        return ROBO_PHYSICAL_KEY_SHIFT_LEFT;
    case 0x2B:
        return ROBO_PHYSICAL_KEY_BACKSLASH;
    case 0x2C:
        return ROBO_PHYSICAL_KEY_Z;
    case 0x2D:
        return ROBO_PHYSICAL_KEY_X;
    case 0x2E:
        return ROBO_PHYSICAL_KEY_C;
    case 0x2F:
        return ROBO_PHYSICAL_KEY_V;
    case 0x30:
        return ROBO_PHYSICAL_KEY_B;
    case 0x31:
        return ROBO_PHYSICAL_KEY_N;
    case 0x32:
        return ROBO_PHYSICAL_KEY_M;
    case 0x33:
        return ROBO_PHYSICAL_KEY_COMMA;
    case 0x34:
        return ROBO_PHYSICAL_KEY_PERIOD;
    case 0x35:
        return ROBO_PHYSICAL_KEY_SLASH;
    case 0x36:
        return ROBO_PHYSICAL_KEY_SHIFT_RIGHT;
    case 0x37:
        return ROBO_PHYSICAL_KEY_NUMPAD_MULTIPLY;
    case 0x38:
        return ROBO_PHYSICAL_KEY_ALT_LEFT;
    case 0x39:
        return ROBO_PHYSICAL_KEY_SPACE;
    case 0x3A:
        return ROBO_PHYSICAL_KEY_CAPS_LOCK;
    case 0x3B:
        return ROBO_PHYSICAL_KEY_F1;
    case 0x3C:
        return ROBO_PHYSICAL_KEY_F2;
    case 0x3D:
        return ROBO_PHYSICAL_KEY_F3;
    case 0x3E:
        return ROBO_PHYSICAL_KEY_F4;
    case 0x3F:
        return ROBO_PHYSICAL_KEY_F5;
    case 0x40:
        return ROBO_PHYSICAL_KEY_F6;
    case 0x41:
        return ROBO_PHYSICAL_KEY_F7;
    case 0x42:
        return ROBO_PHYSICAL_KEY_F8;
    case 0x43:
        return ROBO_PHYSICAL_KEY_F9;
    case 0x44:
        return ROBO_PHYSICAL_KEY_F10;
    case 0x45:
        return ROBO_PHYSICAL_KEY_NUM_LOCK;
    case 0x46:
        return ROBO_PHYSICAL_KEY_SCROLL_LOCK;
    case 0x47:
        return ROBO_PHYSICAL_KEY_NUMPAD_7;
    case 0x48:
        return ROBO_PHYSICAL_KEY_NUMPAD_8;
    case 0x49:
        return ROBO_PHYSICAL_KEY_NUMPAD_9;
    case 0x4A:
        return ROBO_PHYSICAL_KEY_NUMPAD_SUBTRACT;
    case 0x4B:
        return ROBO_PHYSICAL_KEY_NUMPAD_4;
    case 0x4C:
        return ROBO_PHYSICAL_KEY_NUMPAD_5;
    case 0x4D:
        return ROBO_PHYSICAL_KEY_NUMPAD_6;
    case 0x4E:
        return ROBO_PHYSICAL_KEY_NUMPAD_ADD;
    case 0x4F:
        return ROBO_PHYSICAL_KEY_NUMPAD_1;
    case 0x50:
        return ROBO_PHYSICAL_KEY_NUMPAD_2;
    case 0x51:
        return ROBO_PHYSICAL_KEY_NUMPAD_3;
    case 0x52:
        return ROBO_PHYSICAL_KEY_NUMPAD_0;
    case 0x53:
        return ROBO_PHYSICAL_KEY_NUMPAD_DECIMAL;
    case 0x57:
        return ROBO_PHYSICAL_KEY_F11;
    case 0x58:
        return ROBO_PHYSICAL_KEY_F12;
    default:
        return ROBO_PHYSICAL_KEY_UNKNOWN;
    }
}

robo_key_modifier_flags_t robo_keyboard_win32_query_modifiers(void)
{
    robo_key_modifier_flags_t m = ROBO_KEY_MOD_NONE;

    if (GetKeyState(VK_SHIFT) & 0x8000) {
        m |= ROBO_KEY_MOD_SHIFT;
    }
    if (GetKeyState(VK_LSHIFT) & 0x8000) {
        m |= ROBO_KEY_MOD_SHIFT_LEFT | ROBO_KEY_MOD_SHIFT;
    }
    if (GetKeyState(VK_RSHIFT) & 0x8000) {
        m |= ROBO_KEY_MOD_SHIFT_RIGHT | ROBO_KEY_MOD_SHIFT;
    }
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        m |= ROBO_KEY_MOD_CTRL;
    }
    if (GetKeyState(VK_LCONTROL) & 0x8000) {
        m |= ROBO_KEY_MOD_CTRL_LEFT | ROBO_KEY_MOD_CTRL;
    }
    if (GetKeyState(VK_RCONTROL) & 0x8000) {
        m |= ROBO_KEY_MOD_CTRL_RIGHT | ROBO_KEY_MOD_CTRL;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
        m |= ROBO_KEY_MOD_ALT;
    }
    if (GetKeyState(VK_LMENU) & 0x8000) {
        m |= ROBO_KEY_MOD_ALT_LEFT | ROBO_KEY_MOD_ALT;
    }
    if (GetKeyState(VK_RMENU) & 0x8000) {
        m |= ROBO_KEY_MOD_ALT_RIGHT | ROBO_KEY_MOD_ALT;
    }
    if (GetKeyState(VK_LWIN) & 0x8000) {
        m |= ROBO_KEY_MOD_META_LEFT | ROBO_KEY_MOD_META;
    }
    if (GetKeyState(VK_RWIN) & 0x8000) {
        m |= ROBO_KEY_MOD_META_RIGHT | ROBO_KEY_MOD_META;
    }
    if (GetKeyState(VK_CAPITAL) & 0x0001) {
        m |= ROBO_KEY_MOD_CAPS_LOCK;
    }
    if (GetKeyState(VK_NUMLOCK) & 0x0001) {
        m |= ROBO_KEY_MOD_NUM_LOCK;
    }
    if (GetKeyState(VK_SCROLL) & 0x0001) {
        m |= ROBO_KEY_MOD_SCROLL_LOCK;
    }
    return m;
}

robo_result_t robo_keyboard_win32_handle_message(robo_window_id_t window_id,
                                                 unsigned msg,
                                                 uintptr_t wparam,
                                                 intptr_t lparam)
{
    robo_keyboard_event_t ev;
    unsigned vk;
    unsigned scan;
    int extended;
    int is_up;
    int is_repeat;
    UINT mapped;

    is_up = (msg == WM_KEYUP || msg == WM_SYSKEYUP);
    if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN && !is_up) {
        return ROBO_OK;
    }

    vk = (unsigned)wparam;
    scan = (unsigned)((lparam >> 16) & 0xFFu);
    extended = ((lparam >> 24) & 1) != 0;
    is_repeat = !is_up && (((lparam >> 30) & 1) != 0);

    /* Distinguishing left/right modifiers via MapVirtualKey when possible. */
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) {
        mapped = MapVirtualKeyW((UINT)scan, MAPVK_VSC_TO_VK_EX);
        if (mapped != 0u) {
            vk = mapped;
        }
    }

    robo_keyboard_event_init(&ev);
    ev.action = is_up ? ROBO_KEY_ACTION_UP : ROBO_KEY_ACTION_DOWN;
    ev.logical_key = robo_keyboard_win32_logical_from_vk(vk);
    if (vk == VK_RETURN && extended) {
        ev.logical_key = ROBO_LOGICAL_KEY_NUMPAD_ENTER;
    }
    ev.physical_key = robo_keyboard_win32_physical_from_scan(scan, extended);
    if (ev.physical_key == ROBO_PHYSICAL_KEY_UNKNOWN &&
        ev.logical_key != ROBO_LOGICAL_KEY_UNKNOWN) {
        /* Fallback: derive a coarse physical from logical for tracking. */
        /* leave UNKNOWN — state still tracks logical */
    }
    ev.modifiers = robo_keyboard_win32_query_modifiers();
    ev.is_repeat = is_repeat ? 1 : 0;
    ev.timestamp_ms = GetTickCount64();
    ev.window_id = window_id;

    return robo_window_observe_key(window_id, &ev);
}

#endif /* ROBO_WINDOW_HAS_WIN32 */
