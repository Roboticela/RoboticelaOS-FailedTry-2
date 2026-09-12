#ifndef ROBOCOMPOSITOR_PROTOCOL_H
#define ROBOCOMPOSITOR_PROTOCOL_H

#include <stdint.h>
#include "types.h"

#define ROBO_COMP_HELLO        1u
#define ROBO_COMP_REGISTER     2u
#define ROBO_COMP_UNREGISTER   3u
#define ROBO_COMP_UPDATE       4u
#define ROBO_COMP_RAISE        5u
#define ROBO_COMP_LOWER        6u
#define ROBO_COMP_SET_Z        7u
#define ROBO_COMP_DAMAGE       8u
#define ROBO_COMP_FRAME        9u
#define ROBO_COMP_SAMPLE       10u
#define ROBO_COMP_SHUTDOWN     11u
#define ROBO_COMP_STATS        12u

typedef struct RoboCompMsg {
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
    uint32_t stride;
    uint32_t pixfmt;
    uint32_t opacity;
    uint32_t z_index;
    uint32_t visible;
    int32_t dx;
    int32_t dy;
    uint32_t dw;
    uint32_t dh;
    uint32_t extra;
    uint32_t sample_px;
} RoboCompMsg;

#endif
