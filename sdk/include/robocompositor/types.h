#ifndef ROBOCOMPOSITOR_TYPES_H
#define ROBOCOMPOSITOR_TYPES_H

#include <stdint.h>

#define ROBO_COMPOSITOR_ABI_MAJOR 1u
#define ROBO_COMPOSITOR_ABI_MINOR 0u

/*
 * Stored uint32 pixel, little-endian memory:
 *
 *   pixfmt 0  BGRX  byte0=R  byte1=G  byte2=B  byte3=A
 *   pixfmt 1  RGBX  byte0=B  byte1=G  byte2=R  byte3=A
 *
 * Matches RoboGraphics/UEFI packing (ROBO_PIX_BGRX / ROBO_PIX_RGBX)
 * with alpha in the high byte. Logical ARGB is 0xAARRGGBB.
 */
#define ROBO_COMPOSE_PIX_BGRX 0u
#define ROBO_COMPOSE_PIX_RGBX 1u

#define ROBO_COMPOSE_MAX_LAYERS 64u
#define ROBO_COMPOSE_MAX_W      2048u
#define ROBO_COMPOSE_MAX_H      2048u

typedef uint32_t RoboLayerID;

typedef struct RoboDamageRect {
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
    uint8_t empty;
} RoboDamageRect;

#endif
