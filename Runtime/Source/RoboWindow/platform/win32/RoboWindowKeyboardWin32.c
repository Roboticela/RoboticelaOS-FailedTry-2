/*
 * Win32 → RoboKeyboard translation (Goal 7.10).
 *
 * Maps virtual-key (logical) and scan codes (physical) without exposing
 * Win32 types through the public API. Text/IME not handled.
 */

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "RoboWindowKeyboardWin32.h"

#include "RoboBase/RoboLog.h"

#include <windows.h>

LogicalKey KeyboardWin32LogicalFromVk(unsigned vk)
{
    switch (vk) {
    case 'A':
        return RoboLogicalKeyA;
    case 'B':
        return RoboLogicalKeyB;
    case 'C':
        return RoboLogicalKeyC;
    case 'D':
        return RoboLogicalKeyD;
    case 'E':
        return RoboLogicalKeyE;
    case 'F':
        return RoboLogicalKeyF;
    case 'G':
        return RoboLogicalKeyG;
    case 'H':
        return RoboLogicalKeyH;
    case 'I':
        return RoboLogicalKeyI;
    case 'J':
        return RoboLogicalKeyJ;
    case 'K':
        return RoboLogicalKeyK;
    case 'L':
        return RoboLogicalKeyL;
    case 'M':
        return RoboLogicalKeyM;
    case 'N':
        return RoboLogicalKeyN;
    case 'O':
        return RoboLogicalKeyO;
    case 'P':
        return RoboLogicalKeyP;
    case 'Q':
        return RoboLogicalKeyQ;
    case 'R':
        return RoboLogicalKeyR;
    case 'S':
        return RoboLogicalKeyS;
    case 'T':
        return RoboLogicalKeyT;
    case 'U':
        return RoboLogicalKeyU;
    case 'V':
        return RoboLogicalKeyV;
    case 'W':
        return RoboLogicalKeyW;
    case 'X':
        return RoboLogicalKeyX;
    case 'Y':
        return RoboLogicalKeyY;
    case 'Z':
        return RoboLogicalKeyZ;
    case '1':
        return RoboLogicalKeyDigit1;
    case '2':
        return RoboLogicalKeyDigit2;
    case '3':
        return RoboLogicalKeyDigit3;
    case '4':
        return RoboLogicalKeyDigit4;
    case '5':
        return RoboLogicalKeyDigit5;
    case '6':
        return RoboLogicalKeyDigit6;
    case '7':
        return RoboLogicalKeyDigit7;
    case '8':
        return RoboLogicalKeyDigit8;
    case '9':
        return RoboLogicalKeyDigit9;
    case '0':
        return RoboLogicalKeyDigit0;
    case VK_RETURN:
        return RoboLogicalKeyEnter;
    case VK_ESCAPE:
        return RoboLogicalKeyEscape;
    case VK_BACK:
        return RoboLogicalKeyBackspace;
    case VK_TAB:
        return RoboLogicalKeyTab;
    case VK_SPACE:
        return RoboLogicalKeySpace;
    case VK_OEM_MINUS:
        return RoboLogicalKeyMinus;
    case VK_OEM_PLUS:
        return RoboLogicalKeyEqual;
    case VK_OEM_4:
        return RoboLogicalKeyBracketLeft;
    case VK_OEM_6:
        return RoboLogicalKeyBracketRight;
    case VK_OEM_5:
        return RoboLogicalKeyBackslash;
    case VK_OEM_1:
        return RoboLogicalKeySemicolon;
    case VK_OEM_7:
        return RoboLogicalKeyQuote;
    case VK_OEM_3:
        return RoboLogicalKeyBackquote;
    case VK_OEM_COMMA:
        return RoboLogicalKeyComma;
    case VK_OEM_PERIOD:
        return RoboLogicalKeyPeriod;
    case VK_OEM_2:
        return RoboLogicalKeySlash;
    case VK_CAPITAL:
        return RoboLogicalKeyCapsLock;
    case VK_F1:
        return RoboLogicalKeyF1;
    case VK_F2:
        return RoboLogicalKeyF2;
    case VK_F3:
        return RoboLogicalKeyF3;
    case VK_F4:
        return RoboLogicalKeyF4;
    case VK_F5:
        return RoboLogicalKeyF5;
    case VK_F6:
        return RoboLogicalKeyF6;
    case VK_F7:
        return RoboLogicalKeyF7;
    case VK_F8:
        return RoboLogicalKeyF8;
    case VK_F9:
        return RoboLogicalKeyF9;
    case VK_F10:
        return RoboLogicalKeyF10;
    case VK_F11:
        return RoboLogicalKeyF11;
    case VK_F12:
        return RoboLogicalKeyF12;
    case VK_SNAPSHOT:
        return RoboLogicalKeyPrintScreen;
    case VK_SCROLL:
        return RoboLogicalKeyScrollLock;
    case VK_PAUSE:
        return RoboLogicalKeyPause;
    case VK_INSERT:
        return RoboLogicalKeyInsert;
    case VK_DELETE:
        return RoboLogicalKeyDelete;
    case VK_HOME:
        return RoboLogicalKeyHome;
    case VK_END:
        return RoboLogicalKeyEnd;
    case VK_PRIOR:
        return RoboLogicalKeyPageUp;
    case VK_NEXT:
        return RoboLogicalKeyPageDown;
    case VK_RIGHT:
        return RoboLogicalKeyArrowRight;
    case VK_LEFT:
        return RoboLogicalKeyArrowLeft;
    case VK_DOWN:
        return RoboLogicalKeyArrowDown;
    case VK_UP:
        return RoboLogicalKeyArrowUp;
    case VK_NUMLOCK:
        return RoboLogicalKeyNumLock;
    case VK_DIVIDE:
        return RoboLogicalKeyNumpadDivide;
    case VK_MULTIPLY:
        return RoboLogicalKeyNumpadMultiply;
    case VK_SUBTRACT:
        return RoboLogicalKeyNumpadSubtract;
    case VK_ADD:
        return RoboLogicalKeyNumpadAdd;
    case VK_NUMPAD1:
        return RoboLogicalKeyNumpad1;
    case VK_NUMPAD2:
        return RoboLogicalKeyNumpad2;
    case VK_NUMPAD3:
        return RoboLogicalKeyNumpad3;
    case VK_NUMPAD4:
        return RoboLogicalKeyNumpad4;
    case VK_NUMPAD5:
        return RoboLogicalKeyNumpad5;
    case VK_NUMPAD6:
        return RoboLogicalKeyNumpad6;
    case VK_NUMPAD7:
        return RoboLogicalKeyNumpad7;
    case VK_NUMPAD8:
        return RoboLogicalKeyNumpad8;
    case VK_NUMPAD9:
        return RoboLogicalKeyNumpad9;
    case VK_NUMPAD0:
        return RoboLogicalKeyNumpad0;
    case VK_DECIMAL:
        return RoboLogicalKeyNumpadDecimal;
    case VK_APPS:
        return RoboLogicalKeyContextMenu;
    case VK_SHIFT:
        return RoboLogicalKeyShift;
    case VK_LSHIFT:
        return RoboLogicalKeyShiftLeft;
    case VK_RSHIFT:
        return RoboLogicalKeyShiftRight;
    case VK_CONTROL:
        return RoboLogicalKeyControl;
    case VK_LCONTROL:
        return RoboLogicalKeyControlLeft;
    case VK_RCONTROL:
        return RoboLogicalKeyControlRight;
    case VK_MENU:
        return RoboLogicalKeyAlt;
    case VK_LMENU:
        return RoboLogicalKeyAltLeft;
    case VK_RMENU:
        return RoboLogicalKeyAltRight;
    case VK_LWIN:
        return RoboLogicalKeyMetaLeft;
    case VK_RWIN:
        return RoboLogicalKeyMetaRight;
    default:
        return RoboLogicalKeyUnknown;
    }
}

PhysicalKey KeyboardWin32PhysicalFromScan(unsigned scan,
                                                           int extended)
{
    /* Set 1 make codes (common PC/AT). Extended bit distinguishes numpad. */
    if (extended) {
        switch (scan) {
        case 0x1C:
            return RoboPhysicalKeyNumpadEnter;
        case 0x35:
            return RoboPhysicalKeyNumpadDivide;
        case 0x37:
            return RoboPhysicalKeyPrintScreen;
        case 0x38:
            return RoboPhysicalKeyAltRight;
        case 0x1D:
            return RoboPhysicalKeyControlRight;
        case 0x47:
            return RoboPhysicalKeyHome;
        case 0x48:
            return RoboPhysicalKeyArrowUp;
        case 0x49:
            return RoboPhysicalKeyPageUp;
        case 0x4B:
            return RoboPhysicalKeyArrowLeft;
        case 0x4D:
            return RoboPhysicalKeyArrowRight;
        case 0x4F:
            return RoboPhysicalKeyEnd;
        case 0x50:
            return RoboPhysicalKeyArrowDown;
        case 0x51:
            return RoboPhysicalKeyPageDown;
        case 0x52:
            return RoboPhysicalKeyInsert;
        case 0x53:
            return RoboPhysicalKeyDelete;
        case 0x5B:
            return RoboPhysicalKeyMetaLeft;
        case 0x5C:
            return RoboPhysicalKeyMetaRight;
        case 0x5D:
            return RoboPhysicalKeyContextMenu;
        default:
            break;
        }
    }

    switch (scan) {
    case 0x01:
        return RoboPhysicalKeyEscape;
    case 0x02:
        return RoboPhysicalKeyDigit1;
    case 0x03:
        return RoboPhysicalKeyDigit2;
    case 0x04:
        return RoboPhysicalKeyDigit3;
    case 0x05:
        return RoboPhysicalKeyDigit4;
    case 0x06:
        return RoboPhysicalKeyDigit5;
    case 0x07:
        return RoboPhysicalKeyDigit6;
    case 0x08:
        return RoboPhysicalKeyDigit7;
    case 0x09:
        return RoboPhysicalKeyDigit8;
    case 0x0A:
        return RoboPhysicalKeyDigit9;
    case 0x0B:
        return RoboPhysicalKeyDigit0;
    case 0x0C:
        return RoboPhysicalKeyMinus;
    case 0x0D:
        return RoboPhysicalKeyEqual;
    case 0x0E:
        return RoboPhysicalKeyBackspace;
    case 0x0F:
        return RoboPhysicalKeyTab;
    case 0x10:
        return RoboPhysicalKeyQ;
    case 0x11:
        return RoboPhysicalKeyW;
    case 0x12:
        return RoboPhysicalKeyE;
    case 0x13:
        return RoboPhysicalKeyR;
    case 0x14:
        return RoboPhysicalKeyT;
    case 0x15:
        return RoboPhysicalKeyY;
    case 0x16:
        return RoboPhysicalKeyU;
    case 0x17:
        return RoboPhysicalKeyI;
    case 0x18:
        return RoboPhysicalKeyO;
    case 0x19:
        return RoboPhysicalKeyP;
    case 0x1A:
        return RoboPhysicalKeyBracketLeft;
    case 0x1B:
        return RoboPhysicalKeyBracketRight;
    case 0x1C:
        return RoboPhysicalKeyEnter;
    case 0x1D:
        return RoboPhysicalKeyControlLeft;
    case 0x1E:
        return RoboPhysicalKeyA;
    case 0x1F:
        return RoboPhysicalKeyS;
    case 0x20:
        return RoboPhysicalKeyD;
    case 0x21:
        return RoboPhysicalKeyF;
    case 0x22:
        return RoboPhysicalKeyG;
    case 0x23:
        return RoboPhysicalKeyH;
    case 0x24:
        return RoboPhysicalKeyJ;
    case 0x25:
        return RoboPhysicalKeyK;
    case 0x26:
        return RoboPhysicalKeyL;
    case 0x27:
        return RoboPhysicalKeySemicolon;
    case 0x28:
        return RoboPhysicalKeyQuote;
    case 0x29:
        return RoboPhysicalKeyBackquote;
    case 0x2A:
        return RoboPhysicalKeyShiftLeft;
    case 0x2B:
        return RoboPhysicalKeyBackslash;
    case 0x2C:
        return RoboPhysicalKeyZ;
    case 0x2D:
        return RoboPhysicalKeyX;
    case 0x2E:
        return RoboPhysicalKeyC;
    case 0x2F:
        return RoboPhysicalKeyV;
    case 0x30:
        return RoboPhysicalKeyB;
    case 0x31:
        return RoboPhysicalKeyN;
    case 0x32:
        return RoboPhysicalKeyM;
    case 0x33:
        return RoboPhysicalKeyComma;
    case 0x34:
        return RoboPhysicalKeyPeriod;
    case 0x35:
        return RoboPhysicalKeySlash;
    case 0x36:
        return RoboPhysicalKeyShiftRight;
    case 0x37:
        return RoboPhysicalKeyNumpadMultiply;
    case 0x38:
        return RoboPhysicalKeyAltLeft;
    case 0x39:
        return RoboPhysicalKeySpace;
    case 0x3A:
        return RoboPhysicalKeyCapsLock;
    case 0x3B:
        return RoboPhysicalKeyF1;
    case 0x3C:
        return RoboPhysicalKeyF2;
    case 0x3D:
        return RoboPhysicalKeyF3;
    case 0x3E:
        return RoboPhysicalKeyF4;
    case 0x3F:
        return RoboPhysicalKeyF5;
    case 0x40:
        return RoboPhysicalKeyF6;
    case 0x41:
        return RoboPhysicalKeyF7;
    case 0x42:
        return RoboPhysicalKeyF8;
    case 0x43:
        return RoboPhysicalKeyF9;
    case 0x44:
        return RoboPhysicalKeyF10;
    case 0x45:
        return RoboPhysicalKeyNumLock;
    case 0x46:
        return RoboPhysicalKeyScrollLock;
    case 0x47:
        return RoboPhysicalKeyNumpad7;
    case 0x48:
        return RoboPhysicalKeyNumpad8;
    case 0x49:
        return RoboPhysicalKeyNumpad9;
    case 0x4A:
        return RoboPhysicalKeyNumpadSubtract;
    case 0x4B:
        return RoboPhysicalKeyNumpad4;
    case 0x4C:
        return RoboPhysicalKeyNumpad5;
    case 0x4D:
        return RoboPhysicalKeyNumpad6;
    case 0x4E:
        return RoboPhysicalKeyNumpadAdd;
    case 0x4F:
        return RoboPhysicalKeyNumpad1;
    case 0x50:
        return RoboPhysicalKeyNumpad2;
    case 0x51:
        return RoboPhysicalKeyNumpad3;
    case 0x52:
        return RoboPhysicalKeyNumpad0;
    case 0x53:
        return RoboPhysicalKeyNumpadDecimal;
    case 0x57:
        return RoboPhysicalKeyF11;
    case 0x58:
        return RoboPhysicalKeyF12;
    default:
        return RoboPhysicalKeyUnknown;
    }
}

KeyModifierFlags KeyboardWin32QueryModifiers(void)
{
    KeyModifierFlags m = RoboKeyModNone;

    if (GetKeyState(VK_SHIFT) & 0x8000) {
        m |= RoboKeyModShift;
    }
    if (GetKeyState(VK_LSHIFT) & 0x8000) {
        m |= RoboKeyModShiftLeft | RoboKeyModShift;
    }
    if (GetKeyState(VK_RSHIFT) & 0x8000) {
        m |= RoboKeyModShiftRight | RoboKeyModShift;
    }
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        m |= RoboKeyModCtrl;
    }
    if (GetKeyState(VK_LCONTROL) & 0x8000) {
        m |= RoboKeyModCtrlLeft | RoboKeyModCtrl;
    }
    if (GetKeyState(VK_RCONTROL) & 0x8000) {
        m |= RoboKeyModCtrlRight | RoboKeyModCtrl;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
        m |= RoboKeyModAlt;
    }
    if (GetKeyState(VK_LMENU) & 0x8000) {
        m |= RoboKeyModAltLeft | RoboKeyModAlt;
    }
    if (GetKeyState(VK_RMENU) & 0x8000) {
        m |= RoboKeyModAltRight | RoboKeyModAlt;
    }
    if (GetKeyState(VK_LWIN) & 0x8000) {
        m |= RoboKeyModMetaLeft | RoboKeyModMeta;
    }
    if (GetKeyState(VK_RWIN) & 0x8000) {
        m |= RoboKeyModMetaRight | RoboKeyModMeta;
    }
    if (GetKeyState(VK_CAPITAL) & 0x0001) {
        m |= RoboKeyModCapsLock;
    }
    if (GetKeyState(VK_NUMLOCK) & 0x0001) {
        m |= RoboKeyModNumLock;
    }
    if (GetKeyState(VK_SCROLL) & 0x0001) {
        m |= RoboKeyModScrollLock;
    }
    return m;
}

Result KeyboardWin32HandleMessage(WindowId windowId,
                                                 unsigned msg,
                                                 uintptr_t wparam,
                                                 intptr_t lparam)
{
    KeyboardEvent ev;
    unsigned vk;
    unsigned scan;
    int extended;
    int isUp;
    int isRepeat;
    UINT mapped;

    isUp = (msg == WM_KEYUP || msg == WM_SYSKEYUP);
    if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN && !isUp) {
        return RoboOk;
    }

    vk = (unsigned)wparam;
    scan = (unsigned)((lparam >> 16) & 0xFFu);
    extended = ((lparam >> 24) & 1) != 0;
    isRepeat = !isUp && (((lparam >> 30) & 1) != 0);

    /* Distinguishing left/right modifiers via MapVirtualKey when possible. */
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) {
        mapped = MapVirtualKeyW((UINT)scan, MAPVK_VSC_TO_VK_EX);
        if (mapped != 0u) {
            vk = mapped;
        }
    }

    KeyboardEventInit(&ev);
    ev.action = isUp ? RoboKeyActionUp : RoboKeyActionDown;
    ev.logicalKey = KeyboardWin32LogicalFromVk(vk);
    if (vk == VK_RETURN && extended) {
        ev.logicalKey = RoboLogicalKeyNumpadEnter;
    }
    ev.physicalKey = KeyboardWin32PhysicalFromScan(scan, extended);
    if (ev.physicalKey == RoboPhysicalKeyUnknown &&
        ev.logicalKey != RoboLogicalKeyUnknown) {
        /* Fallback: derive a coarse physical from logical for tracking. */
        /* leave UNKNOWN — state still tracks logical */
    }
    ev.modifiers = KeyboardWin32QueryModifiers();
    ev.isRepeat = isRepeat ? 1 : 0;
    ev.timestampMs = GetTickCount64();
    ev.windowId = windowId;

    return WindowObserveKey(windowId, &ev);
}

#endif /* RoboWindowHasWin32 */
