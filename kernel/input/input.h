#ifndef ROBO_INPUT_H
#define ROBO_INPUT_H

#include <stdint.h>

#define ROBO_INPUT_KEYBOARD   1u
#define ROBO_INPUT_MOUSE      2u
#define ROBO_INPUT_TOUCHPAD   3u
#define ROBO_INPUT_TOUCHSCREEN 4u
#define ROBO_INPUT_GAMEPAD    5u
#define ROBO_INPUT_TABLET     6u

#define ROBO_KEY_DOWN   1u
#define ROBO_KEY_UP     2u
#define ROBO_MOUSE_MOVE 3u
#define ROBO_MOUSE_BUTTON 4u
#define ROBO_MOUSE_SCROLL 5u

#define ROBO_MOD_SHIFT 1u
#define ROBO_MOD_CTRL  2u
#define ROBO_MOD_ALT   4u
#define ROBO_MOD_META  8u

#define ROBO_MBUTTON_LEFT   1u
#define ROBO_MBUTTON_RIGHT  2u
#define ROBO_MBUTTON_MIDDLE 4u

#define ROBO_KEY_A 0x04
#define ROBO_KEY_ENTER 0x28
#define ROBO_KEY_ESC 0x29
#define ROBO_KEY_SPACE 0x2C
#define ROBO_KEY_LSHIFT 0xE1
#define ROBO_KEY_LCTRL 0xE0
#define ROBO_KEY_UNKNOWN 0x00

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

int robo_input_init(void);
int robo_input_push(const RoboInputEvent *ev);
int robo_input_pop(RoboInputEvent *ev);
unsigned robo_input_overflows(void);
unsigned robo_input_queued(void);
int robo_input_selftest(void);
int robo_input_keyboard_ready(void);
int robo_input_mouse_ready(void);
void robo_input_set_keyboard_ready(int v);
void robo_input_set_mouse_ready(int v);

#endif
