#ifndef ROBOWINDOW_TYPES_H
#define ROBOWINDOW_TYPES_H

#include <stdint.h>
#include <robosys/syscall.h>
#include <robosys/input.h>

#define ROBO_WINDOW_ABI_MAJOR 1u
#define ROBO_WINDOW_ABI_MINOR 0u

typedef uint32_t RoboWindowID;

#define ROBO_WINDOW_VISIBLE    (1u << 0)
#define ROBO_WINDOW_RESIZABLE  (1u << 1)
#define ROBO_WINDOW_MOVABLE    (1u << 2)
#define ROBO_WINDOW_FOCUSABLE  (1u << 3)
#define ROBO_WINDOW_BORDERLESS (1u << 4)

#define ROBO_WIN_CREATED   1u
#define ROBO_WIN_VISIBLE   2u
#define ROBO_WIN_HIDDEN    3u
#define ROBO_WIN_DESTROYED 4u

#define ROBO_WIN_EV_CREATED         1u
#define ROBO_WIN_EV_DESTROYED       2u
#define ROBO_WIN_EV_SHOWN           3u
#define ROBO_WIN_EV_HIDDEN          4u
#define ROBO_WIN_EV_MOVED           5u
#define ROBO_WIN_EV_RESIZED         6u
#define ROBO_WIN_EV_FOCUS_GAINED    7u
#define ROBO_WIN_EV_FOCUS_LOST      8u
#define ROBO_WIN_EV_CLOSE           9u
#define ROBO_WIN_EV_KEY_DOWN        10u
#define ROBO_WIN_EV_KEY_UP          11u
#define ROBO_WIN_EV_MOUSE_MOVE      12u
#define ROBO_WIN_EV_MOUSE_BUTTON    13u
#define ROBO_WIN_EV_MOUSE_SCROLL    14u

typedef struct RoboWindowEvent {
    uint32_t type;
    uint32_t window;
    uint32_t code;
    uint32_t value;
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    uint32_t modifiers;
    uint32_t buttons;
} RoboWindowEvent;

typedef struct RoboWindowSurface {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixfmt;
    uint32_t *pixels;
    uint64_t shm_id;
    uint32_t shm_bytes;
} RoboWindowSurface;

#endif
