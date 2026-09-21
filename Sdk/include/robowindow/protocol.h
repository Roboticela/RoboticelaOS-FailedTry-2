#ifndef ROBOWINDOW_PROTOCOL_H
#define ROBOWINDOW_PROTOCOL_H

#include <stdint.h>
#include "types.h"

#define RoboWinCreate         1u
#define RoboWinDestroy        2u
#define RoboWinShow           3u
#define RoboWinHide           4u
#define RoboWinMove           5u
#define RoboWinResize         6u
#define RoboWinSetTitle      7u
#define RoboWinGetSurface    8u
#define RoboWinRequestFocus  9u
#define RoboWinWaitEvent     10u
#define RoboWinPollEvent     11u
#define RoboWinSubmit         12u
#define RoboWinStats          13u
#define RoboWinShutdown       14u
#define RoboWinTestInject    15u
#define RoboWinHitTest       16u
#define RoboWinGetFocus      17u
#define RoboWinSetOpacity    18u
#define RoboWinSamplePixel   19u

typedef struct RoboWinMsg {
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
    uint32_t pixfmt;
    uint32_t stride;
    uint32_t evType;
    uint32_t evCode;
    uint32_t evValue;
    int32_t evX;
    int32_t evY;
    int32_t evDx;
    int32_t evDy;
    uint32_t evMods;
    uint32_t evButtons;
    uint32_t extra;
    uint32_t state;
    char title[32];
} RoboWinMsg;

#endif
