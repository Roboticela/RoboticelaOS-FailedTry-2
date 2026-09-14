/*
 * RoboGraphics frame metadata (Goal 6.1).
 *
 * Separates "finished raster result" from presentation. VSync / scheduling
 * belong to compositor / window goals — not here.
 */

#ifndef ROBO_GRAPHICS_FRAME_H
#define ROBO_GRAPHICS_FRAME_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboGraphics/RoboGraphicsTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_gfx_frame {
    robo_gfx_frame_id_t id;
    robo_gfx_surface_t *target; /* borrowed; not retained by value struct */
    uint32_t width;
    uint32_t height;
    robo_gfx_pixel_format_t format;
    uint64_t timestamp_ns; /* 0 = unset */
} robo_gfx_frame_t;

void robo_gfx_frame_init(robo_gfx_frame_t *frame);

/**
 * Bind frame metadata to @p surface (does not retain).
 * Assigns a process-local monotonic id when id was NONE.
 */
robo_result_t robo_gfx_frame_begin(robo_gfx_frame_t *frame,
                                   robo_gfx_surface_t *surface);

/** Mark frame finished for presentation/test consumers. */
robo_result_t robo_gfx_frame_end(robo_gfx_frame_t *frame);

/** Clear target pointer; does not release the surface. */
void robo_gfx_frame_reset(robo_gfx_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_FRAME_H */
