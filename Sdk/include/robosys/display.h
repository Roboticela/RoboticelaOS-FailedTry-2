#ifndef ROBOSYS_DISPLAY_H
#define ROBOSYS_DISPLAY_H

#include <stdint.h>

#define RoboDispClaim   1u
#define RoboDispInfo    2u
#define RoboDispPresent 3u
#define RoboDispInput   4u
#define RoboDispInject  5u
#define RoboDispRelease 6u

#define RoboDispNowait 1u

#define RoboDispRoleOutput 1u
#define RoboDispRoleInput  2u

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
    int32_t destX;
    int32_t destY;
} RoboDisplayPresent;

#endif
