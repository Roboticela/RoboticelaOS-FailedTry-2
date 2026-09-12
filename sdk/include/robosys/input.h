#ifndef ROBOSYS_INPUT_H
#define ROBOSYS_INPUT_H

#include <stdint.h>

#define ROBO_INPUT_KEYBOARD 1u
#define ROBO_INPUT_MOUSE    2u

#define ROBO_KEY_DOWN     1u
#define ROBO_KEY_UP       2u
#define ROBO_MOUSE_MOVE   3u
#define ROBO_MOUSE_BUTTON 4u
#define ROBO_MOUSE_SCROLL 5u

#define ROBO_MOD_SHIFT 1u
#define ROBO_MOD_CTRL  2u
#define ROBO_MOD_ALT   4u
#define ROBO_MOD_META  8u

#define ROBO_MBUTTON_LEFT   1u
#define ROBO_MBUTTON_RIGHT  2u
#define ROBO_MBUTTON_MIDDLE 4u

/* USB HID usage codes (keyboard/keypad page). */
#define ROBO_KEY_A          0x04u
#define ROBO_KEY_B          0x05u
#define ROBO_KEY_C          0x06u
#define ROBO_KEY_D          0x07u
#define ROBO_KEY_E          0x08u
#define ROBO_KEY_F          0x09u
#define ROBO_KEY_G          0x0Au
#define ROBO_KEY_H          0x0Bu
#define ROBO_KEY_I          0x0Cu
#define ROBO_KEY_J          0x0Du
#define ROBO_KEY_K          0x0Eu
#define ROBO_KEY_L          0x0Fu
#define ROBO_KEY_M          0x10u
#define ROBO_KEY_N          0x11u
#define ROBO_KEY_O          0x12u
#define ROBO_KEY_P          0x13u
#define ROBO_KEY_Q          0x14u
#define ROBO_KEY_R          0x15u
#define ROBO_KEY_S          0x16u
#define ROBO_KEY_T          0x17u
#define ROBO_KEY_U          0x18u
#define ROBO_KEY_V          0x19u
#define ROBO_KEY_W          0x1Au
#define ROBO_KEY_X          0x1Bu
#define ROBO_KEY_Y          0x1Cu
#define ROBO_KEY_Z          0x1Du
#define ROBO_KEY_1          0x1Eu
#define ROBO_KEY_2          0x1Fu
#define ROBO_KEY_3          0x20u
#define ROBO_KEY_4          0x21u
#define ROBO_KEY_5          0x22u
#define ROBO_KEY_6          0x23u
#define ROBO_KEY_7          0x24u
#define ROBO_KEY_8          0x25u
#define ROBO_KEY_9          0x26u
#define ROBO_KEY_0          0x27u
#define ROBO_KEY_ENTER      0x28u
#define ROBO_KEY_ESC        0x29u
#define ROBO_KEY_BACKSPACE  0x2Au
#define ROBO_KEY_TAB        0x2Bu
#define ROBO_KEY_SPACE      0x2Cu
#define ROBO_KEY_MINUS      0x2Du
#define ROBO_KEY_EQUAL      0x2Eu
#define ROBO_KEY_LBRACKET   0x2Fu
#define ROBO_KEY_RBRACKET   0x30u
#define ROBO_KEY_BACKSLASH  0x31u
#define ROBO_KEY_SEMICOLON  0x33u
#define ROBO_KEY_QUOTE      0x34u
#define ROBO_KEY_GRAVE      0x35u
#define ROBO_KEY_COMMA      0x36u
#define ROBO_KEY_DOT        0x37u
#define ROBO_KEY_SLASH      0x38u

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
