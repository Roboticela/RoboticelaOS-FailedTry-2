#ifndef ROBOSYS_DISPLAY_H
#define ROBOSYS_DISPLAY_H

#include <stdint.h>

#define ROBO_DISP_CLAIM   1u
#define ROBO_DISP_INFO    2u
#define ROBO_DISP_PRESENT 3u
#define ROBO_DISP_INPUT   4u
#define ROBO_DISP_INJECT  5u
#define ROBO_DISP_RELEASE 6u

#define ROBO_DISP_NOWAIT 1u

#define ROBO_DISP_ROLE_OUTPUT 1u
#define ROBO_DISP_ROLE_INPUT  2u

typedef struct RoboUserDisplay {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t pixfmt;
    uint32_t ready;
} RoboUserDisplay;

typedef struct RoboDisplayPresent {
    uint64_t pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixfmt;
    int32_t dest_x;
    int32_t dest_y;
} RoboDisplayPresent;

#endif
