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

#define ROBO_KEY_A      0x04u
#define ROBO_KEY_ENTER  0x28u
#define ROBO_KEY_ESC    0x29u
#define ROBO_KEY_SPACE  0x2Cu

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
