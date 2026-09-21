#ifndef ROBOCOMPOSITOR_TYPES_H
#define ROBOCOMPOSITOR_TYPES_H

#include <stdint.h>

#define RoboCompositorAbiMajor 1u
#define RoboCompositorAbiMinor 0u

/*
 * Stored uint32 pixel, little-endian memory:
 *
 *   pixfmt 0  BGRX  byte0=R  byte1=G  byte2=B  byte3=A
 *   pixfmt 1  RGBX  byte0=B  byte1=G  byte2=R  byte3=A
 *
 * Matches RoboGraphics/UEFI packing (RoboPixBgrx / RoboPixRgbx)
 * with alpha in the high byte. Logical ARGB is 0xAARRGGBB.
 */
#define RoboComposePixBgrx 0u
#define RoboComposePixRgbx 1u

#define RoboComposeMaxLayers 64u
#define RoboComposeMaxW      2048u
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
