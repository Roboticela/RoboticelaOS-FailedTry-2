#ifndef ROBOSYS_INPUT_H
#define ROBOSYS_INPUT_H

#include <stdint.h>

#define RoboInputKeyboard 1u
#define RoboInputMouse    2u

#define RoboKeyDown     1u
#define RoboKeyUp       2u
#define RoboMouseMove   3u
#define RoboMouseButton 4u
#define RoboMouseScroll 5u

#define RoboModShift 1u
#define RoboModCtrl  2u
#define RoboModAlt   4u
#define RoboModMeta  8u

#define RoboMbuttonLeft   1u
#define RoboMbuttonRight  2u
#define RoboMbuttonMiddle 4u

/* USB HID usage codes (keyboard/keypad page). */
#define RoboKeyA          0x04u
#define RoboKeyB          0x05u
#define RoboKeyC          0x06u
#define RoboKeyD          0x07u
#define RoboKeyE          0x08u
#define RoboKeyF          0x09u
#define RoboKeyG          0x0Au
#define RoboKeyH          0x0Bu
#define RoboKeyI          0x0Cu
#define RoboKeyJ          0x0Du
#define RoboKeyK          0x0Eu
#define RoboKeyL          0x0Fu
#define RoboKeyM          0x10u
#define RoboKeyN          0x11u
#define RoboKeyO          0x12u
#define RoboKeyP          0x13u
#define RoboKeyQ          0x14u
#define RoboKeyR          0x15u
#define RoboKeyS          0x16u
#define RoboKeyT          0x17u
#define RoboKeyU          0x18u
#define RoboKeyV          0x19u
#define RoboKeyW          0x1Au
#define RoboKeyX          0x1Bu
#define RoboKeyY          0x1Cu
#define RoboKeyZ          0x1Du
#define RoboKey1          0x1Eu
#define RoboKey2          0x1Fu
#define RoboKey3          0x20u
#define RoboKey4          0x21u
#define RoboKey5          0x22u
#define RoboKey6          0x23u
#define RoboKey7          0x24u
#define RoboKey8          0x25u
#define RoboKey9          0x26u
#define RoboKey0          0x27u
#define RoboKeyEnter      0x28u
#define RoboKeyEsc        0x29u
#define RoboKeyBackspace  0x2Au
#define RoboKeyTab        0x2Bu
#define RoboKeySpace      0x2Cu
#define RoboKeyMinus      0x2Du
#define RoboKeyEqual      0x2Eu
#define RoboKeyLbracket   0x2Fu
#define RoboKeyRbracket   0x30u
#define RoboKeyBackslash  0x31u
#define RoboKeySemicolon  0x33u
#define RoboKeyQuote      0x34u
#define RoboKeyGrave      0x35u
#define RoboKeyComma      0x36u
#define RoboKeyDot        0x37u
#define RoboKeySlash      0x38u

typedef struct RoboInputEvent {
    uint32_t type;
    uint32_t device;
    uint32_t code;
    uint32_t value;
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    uint32_t modifiers;
    uint32_t buttons;
    uint64_t timestamp;
} RoboInputEvent;

#endif
