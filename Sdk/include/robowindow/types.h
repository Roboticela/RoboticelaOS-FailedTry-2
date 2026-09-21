#ifndef ROBOWINDOW_TYPES_H
#define ROBOWINDOW_TYPES_H

#include <stdint.h>
#include <robosys/syscall.h>
#include <robosys/input.h>

#define RoboWindowAbiMajor 1u
#define RoboWindowAbiMinor 0u

typedef uint32_t RoboWindowID;

#define RoboWindowVisible    (1u << 0)
#define RoboWindowResizable  (1u << 1)
#define RoboWindowMovable    (1u << 2)
#define RoboWindowFocusable  (1u << 3)
#define RoboWindowBorderless (1u << 4)

#define RoboWinCreated   1u
#define RoboWinVisible   2u
#define RoboWinHidden    3u
#define RoboWinDestroyed 4u

#define RoboWinEvCreated         1u
#define RoboWinEvDestroyed       2u
#define RoboWinEvShown           3u
#define RoboWinEvHidden          4u
#define RoboWinEvMoved           5u
#define RoboWinEvResized         6u
#define RoboWinEvFocusGained    7u
#define RoboWinEvFocusLost      8u
#define RoboWinEvClose           9u
#define RoboWinEvKeyDown        10u
#define RoboWinEvKeyUp          11u
#define RoboWinEvMouseMove      12u
#define RoboWinEvMouseButton    13u
#define RoboWinEvMouseScroll    14u

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
    uint64_t shmId;
    uint32_t shmBytes;
} RoboWindowSurface;

#endif
