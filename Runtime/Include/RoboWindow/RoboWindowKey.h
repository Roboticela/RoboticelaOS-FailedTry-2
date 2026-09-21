/*
 * RoboWindow physical / logical key identifiers (Goal 7.10).
 *
 * Physical key  — stable hardware-oriented position (layout-independent when
 *                 the platform exposes a scan code / usage).
 * Logical key   — interpreted key identity for this event (layout-sensitive
 *                 for writing-system keys). NOT Unicode text / IME.
 *
 * Chromium DomCode / DomKey and W3C UI Events are technical reference only.
 */

#ifndef ROBO_WINDOW_KEY_H
#define ROBO_WINDOW_KEY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Physical key positions supported by the current native backends.
 * Unknown/unmapped → RoboPhysicalKeyUnknown.
 */
typedef enum PhysicalKey {
    RoboPhysicalKeyUnknown = 0,

    /* Writing-system row (US QWERTY positions; physical, not glyphs). */
    RoboPhysicalKeyA,
    RoboPhysicalKeyB,
    RoboPhysicalKeyC,
    RoboPhysicalKeyD,
    RoboPhysicalKeyE,
    RoboPhysicalKeyF,
    RoboPhysicalKeyG,
    RoboPhysicalKeyH,
    RoboPhysicalKeyI,
    RoboPhysicalKeyJ,
    RoboPhysicalKeyK,
    RoboPhysicalKeyL,
    RoboPhysicalKeyM,
    RoboPhysicalKeyN,
    RoboPhysicalKeyO,
    RoboPhysicalKeyP,
    RoboPhysicalKeyQ,
    RoboPhysicalKeyR,
    RoboPhysicalKeyS,
    RoboPhysicalKeyT,
    RoboPhysicalKeyU,
    RoboPhysicalKeyV,
    RoboPhysicalKeyW,
    RoboPhysicalKeyX,
    RoboPhysicalKeyY,
    RoboPhysicalKeyZ,

    RoboPhysicalKeyDigit1,
    RoboPhysicalKeyDigit2,
    RoboPhysicalKeyDigit3,
    RoboPhysicalKeyDigit4,
    RoboPhysicalKeyDigit5,
    RoboPhysicalKeyDigit6,
    RoboPhysicalKeyDigit7,
    RoboPhysicalKeyDigit8,
    RoboPhysicalKeyDigit9,
    RoboPhysicalKeyDigit0,

    RoboPhysicalKeyEnter,
    RoboPhysicalKeyEscape,
    RoboPhysicalKeyBackspace,
    RoboPhysicalKeyTab,
    RoboPhysicalKeySpace,

    RoboPhysicalKeyMinus,
    RoboPhysicalKeyEqual,
    RoboPhysicalKeyBracketLeft,
    RoboPhysicalKeyBracketRight,
    RoboPhysicalKeyBackslash,
    RoboPhysicalKeySemicolon,
    RoboPhysicalKeyQuote,
    RoboPhysicalKeyBackquote,
    RoboPhysicalKeyComma,
    RoboPhysicalKeyPeriod,
    RoboPhysicalKeySlash,

    RoboPhysicalKeyCapsLock,
    RoboPhysicalKeyF1,
    RoboPhysicalKeyF2,
    RoboPhysicalKeyF3,
    RoboPhysicalKeyF4,
    RoboPhysicalKeyF5,
    RoboPhysicalKeyF6,
    RoboPhysicalKeyF7,
    RoboPhysicalKeyF8,
    RoboPhysicalKeyF9,
    RoboPhysicalKeyF10,
    RoboPhysicalKeyF11,
    RoboPhysicalKeyF12,

    RoboPhysicalKeyPrintScreen,
    RoboPhysicalKeyScrollLock,
    RoboPhysicalKeyPause,

    RoboPhysicalKeyInsert,
    RoboPhysicalKeyDelete,
    RoboPhysicalKeyHome,
    RoboPhysicalKeyEnd,
    RoboPhysicalKeyPageUp,
    RoboPhysicalKeyPageDown,

    RoboPhysicalKeyArrowRight,
    RoboPhysicalKeyArrowLeft,
    RoboPhysicalKeyArrowDown,
    RoboPhysicalKeyArrowUp,

    RoboPhysicalKeyNumLock,
    RoboPhysicalKeyNumpadDivide,
    RoboPhysicalKeyNumpadMultiply,
    RoboPhysicalKeyNumpadSubtract,
    RoboPhysicalKeyNumpadAdd,
    RoboPhysicalKeyNumpadEnter,
    RoboPhysicalKeyNumpad1,
    RoboPhysicalKeyNumpad2,
    RoboPhysicalKeyNumpad3,
    RoboPhysicalKeyNumpad4,
    RoboPhysicalKeyNumpad5,
    RoboPhysicalKeyNumpad6,
    RoboPhysicalKeyNumpad7,
    RoboPhysicalKeyNumpad8,
    RoboPhysicalKeyNumpad9,
    RoboPhysicalKeyNumpad0,
    RoboPhysicalKeyNumpadDecimal,

    RoboPhysicalKeyContextMenu,
    RoboPhysicalKeyShiftLeft,
    RoboPhysicalKeyShiftRight,
    RoboPhysicalKeyControlLeft,
    RoboPhysicalKeyControlRight,
    RoboPhysicalKeyAltLeft,
    RoboPhysicalKeyAltRight,
    RoboPhysicalKeyMetaLeft,
    RoboPhysicalKeyMetaRight,

    RoboPhysicalKeyCount
} PhysicalKey;

/**
 * Logical key values (interpreted identity). Letters/digits mean the key
 * meaning under the active layout mapping from the native virtual key —
 * not a Unicode character stream.
 */
typedef enum LogicalKey {
    RoboLogicalKeyUnknown = 0,

    RoboLogicalKeyA,
    RoboLogicalKeyB,
    RoboLogicalKeyC,
    RoboLogicalKeyD,
    RoboLogicalKeyE,
    RoboLogicalKeyF,
    RoboLogicalKeyG,
    RoboLogicalKeyH,
    RoboLogicalKeyI,
    RoboLogicalKeyJ,
    RoboLogicalKeyK,
    RoboLogicalKeyL,
    RoboLogicalKeyM,
    RoboLogicalKeyN,
    RoboLogicalKeyO,
    RoboLogicalKeyP,
    RoboLogicalKeyQ,
    RoboLogicalKeyR,
    RoboLogicalKeyS,
    RoboLogicalKeyT,
    RoboLogicalKeyU,
    RoboLogicalKeyV,
    RoboLogicalKeyW,
    RoboLogicalKeyX,
    RoboLogicalKeyY,
    RoboLogicalKeyZ,

    RoboLogicalKeyDigit1,
    RoboLogicalKeyDigit2,
    RoboLogicalKeyDigit3,
    RoboLogicalKeyDigit4,
    RoboLogicalKeyDigit5,
    RoboLogicalKeyDigit6,
    RoboLogicalKeyDigit7,
    RoboLogicalKeyDigit8,
    RoboLogicalKeyDigit9,
    RoboLogicalKeyDigit0,

    RoboLogicalKeyEnter,
    RoboLogicalKeyEscape,
    RoboLogicalKeyBackspace,
    RoboLogicalKeyTab,
    RoboLogicalKeySpace,

    RoboLogicalKeyMinus,
    RoboLogicalKeyEqual,
    RoboLogicalKeyBracketLeft,
    RoboLogicalKeyBracketRight,
    RoboLogicalKeyBackslash,
    RoboLogicalKeySemicolon,
    RoboLogicalKeyQuote,
    RoboLogicalKeyBackquote,
    RoboLogicalKeyComma,
    RoboLogicalKeyPeriod,
    RoboLogicalKeySlash,

    RoboLogicalKeyCapsLock,
    RoboLogicalKeyF1,
    RoboLogicalKeyF2,
    RoboLogicalKeyF3,
    RoboLogicalKeyF4,
    RoboLogicalKeyF5,
    RoboLogicalKeyF6,
    RoboLogicalKeyF7,
    RoboLogicalKeyF8,
    RoboLogicalKeyF9,
    RoboLogicalKeyF10,
    RoboLogicalKeyF11,
    RoboLogicalKeyF12,

    RoboLogicalKeyPrintScreen,
    RoboLogicalKeyScrollLock,
    RoboLogicalKeyPause,

    RoboLogicalKeyInsert,
    RoboLogicalKeyDelete,
    RoboLogicalKeyHome,
    RoboLogicalKeyEnd,
    RoboLogicalKeyPageUp,
    RoboLogicalKeyPageDown,

    RoboLogicalKeyArrowRight,
    RoboLogicalKeyArrowLeft,
    RoboLogicalKeyArrowDown,
    RoboLogicalKeyArrowUp,

    RoboLogicalKeyNumLock,
    RoboLogicalKeyNumpadDivide,
    RoboLogicalKeyNumpadMultiply,
    RoboLogicalKeyNumpadSubtract,
    RoboLogicalKeyNumpadAdd,
    RoboLogicalKeyNumpadEnter,
    RoboLogicalKeyNumpad1,
    RoboLogicalKeyNumpad2,
    RoboLogicalKeyNumpad3,
    RoboLogicalKeyNumpad4,
    RoboLogicalKeyNumpad5,
    RoboLogicalKeyNumpad6,
    RoboLogicalKeyNumpad7,
    RoboLogicalKeyNumpad8,
    RoboLogicalKeyNumpad9,
    RoboLogicalKeyNumpad0,
    RoboLogicalKeyNumpadDecimal,

    RoboLogicalKeyContextMenu,
    RoboLogicalKeyShift,
    RoboLogicalKeyControl,
    RoboLogicalKeyAlt,
    RoboLogicalKeyMeta,
    RoboLogicalKeyShiftLeft,
    RoboLogicalKeyShiftRight,
    RoboLogicalKeyControlLeft,
    RoboLogicalKeyControlRight,
    RoboLogicalKeyAltLeft,
    RoboLogicalKeyAltRight,
    RoboLogicalKeyMetaLeft,
    RoboLogicalKeyMetaRight,

    RoboLogicalKeyCount
} LogicalKey;

typedef enum KeyAction {
    RoboKeyActionNone = 0,
    RoboKeyActionDown = 1,
    RoboKeyActionUp = 2
} KeyAction;

/** Modifier bit flags (Roboticela-owned). */
typedef uint32_t KeyModifierFlags;

#define RoboKeyModNone          ((KeyModifierFlags)0u)
#define RoboKeyModShift         ((KeyModifierFlags)(1u << 0))
#define RoboKeyModCtrl          ((KeyModifierFlags)(1u << 1))
#define RoboKeyModAlt           ((KeyModifierFlags)(1u << 2))
#define RoboKeyModMeta          ((KeyModifierFlags)(1u << 3))
#define RoboKeyModCapsLock     ((KeyModifierFlags)(1u << 4))
#define RoboKeyModNumLock      ((KeyModifierFlags)(1u << 5))
#define RoboKeyModScrollLock   ((KeyModifierFlags)(1u << 6))
#define RoboKeyModShiftLeft    ((KeyModifierFlags)(1u << 7))
#define RoboKeyModShiftRight   ((KeyModifierFlags)(1u << 8))
#define RoboKeyModCtrlLeft     ((KeyModifierFlags)(1u << 9))
#define RoboKeyModCtrlRight    ((KeyModifierFlags)(1u << 10))
#define RoboKeyModAltLeft      ((KeyModifierFlags)(1u << 11))
#define RoboKeyModAltRight     ((KeyModifierFlags)(1u << 12))
#define RoboKeyModMetaLeft     ((KeyModifierFlags)(1u << 13))
#define RoboKeyModMetaRight    ((KeyModifierFlags)(1u << 14))

int PhysicalKeyIsKnown(PhysicalKey key);
int LogicalKeyIsKnown(LogicalKey key);

const char *PhysicalKeyName(PhysicalKey key);
const char *LogicalKeyName(LogicalKey key);
const char *KeyActionName(KeyAction action);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_KEY_H */
