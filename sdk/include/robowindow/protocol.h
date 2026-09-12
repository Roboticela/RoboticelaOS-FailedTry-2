#ifndef ROBOWINDOW_PROTOCOL_H
#define ROBOWINDOW_PROTOCOL_H

#include <stdint.h>
#include "types.h"

#define ROBO_WIN_CREATE         1u
#define ROBO_WIN_DESTROY        2u
#define ROBO_WIN_SHOW           3u
#define ROBO_WIN_HIDE           4u
#define ROBO_WIN_MOVE           5u
#define ROBO_WIN_RESIZE         6u
#define ROBO_WIN_SET_TITLE      7u
#define ROBO_WIN_GET_SURFACE    8u
#define ROBO_WIN_REQUEST_FOCUS  9u
#define ROBO_WIN_WAIT_EVENT     10u
#define ROBO_WIN_POLL_EVENT     11u
#define ROBO_WIN_SUBMIT         12u
#define ROBO_WIN_STATS          13u
#define ROBO_WIN_SHUTDOWN       14u
#define ROBO_WIN_TEST_INJECT    15u
#define ROBO_WIN_HIT_TEST       16u
#define ROBO_WIN_GET_FOCUS      17u
#define ROBO_WIN_SET_OPACITY    18u
#define ROBO_WIN_SAMPLE_PIXEL   19u

typedef struct RoboWinMsg {
    uint32_t op;
    int32_t status;
    uint32_t window;
    uint32_t flags;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint64_t shm_id;
    uint32_t shm_bytes;
    uint32_t pixfmt;
    uint32_t stride;
    uint32_t ev_type;
    uint32_t ev_code;
    uint32_t ev_value;
    int32_t ev_x;
    int32_t ev_y;
    int32_t ev_dx;
    int32_t ev_dy;
    uint32_t ev_mods;
    uint32_t ev_buttons;
    uint32_t extra;
    uint32_t state;
    char title[32];
} RoboWinMsg;

#endif
