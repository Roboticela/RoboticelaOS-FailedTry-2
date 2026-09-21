#ifndef ROBOCOMPOSITOR_PROTOCOL_H
#define ROBOCOMPOSITOR_PROTOCOL_H

#include <stdint.h>
#include "types.h"

#define RoboCompHello        1u
#define RoboCompRegister     2u
#define RoboCompUnregister   3u
#define RoboCompUpdate       4u
#define RoboCompRaise        5u
#define RoboCompLower        6u
#define RoboCompSetZ        7u
#define RoboCompDamage       8u
#define RoboCompFrame        9u
#define RoboCompSample       10u
#define RoboCompShutdown     11u
#define RoboCompStats        12u

typedef struct RoboCompMsg {
    uint32_t op;
    int32_t status;
    uint32_t window;
    uint32_t flags;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint64_t shmId;
    uint32_t shmBytes;
    uint32_t stride;
    uint32_t pixfmt;
    uint32_t opacity;
    uint32_t zIndex;
    uint32_t visible;
    int32_t dx;
    int32_t dy;
    uint32_t dw;
    uint32_t dh;
    uint32_t extra;
    uint32_t samplePx;
} RoboCompMsg;

#endif
