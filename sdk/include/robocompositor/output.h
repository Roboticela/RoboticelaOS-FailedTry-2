#ifndef ROBOCOMPOSITOR_OUTPUT_H
#define ROBOCOMPOSITOR_OUTPUT_H

#include <stdint.h>

/*
 * Output abstraction. v1 has one output (the UEFI framebuffer via
 * SYS_DISPLAY PRESENT). Additional outputs can be added without
 * changing the layer list.
 *
 * Presentation is a copy into the scanout buffer. There is no hardware
 * page flip; the offscreen back buffer is the double-buffer.
 */

typedef struct RoboOutput {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixfmt;
    uint32_t ready;
    int (*present)(struct RoboOutput *out, const uint32_t *pixels,
                   uint32_t width, uint32_t height, uint32_t stride,
                   int32_t dest_x, int32_t dest_y);
} RoboOutput;

int robo_output_present(RoboOutput *out, const uint32_t *pixels,
                        uint32_t width, uint32_t height, uint32_t stride,
                        int32_t dest_x, int32_t dest_y);
int robo_output_open_display(RoboOutput *out);

#endif
