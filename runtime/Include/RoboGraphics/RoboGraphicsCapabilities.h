/*
 * RoboGraphics capability query (Goal 6.1).
 *
 * Reports what this runtime build can do today vs architectural stubs.
 */

#ifndef ROBO_GRAPHICS_CAPABILITIES_H
#define ROBO_GRAPHICS_CAPABILITIES_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_gfx_capabilities {
    int cpu_raster_available;
    int gpu_backend_available;
    int framebuffer_present_available;
    int native_window_available;
    uint32_t max_surface_width;
    uint32_t max_surface_height;
    /** Bit (1u << format) for known formats with CPU create support. */
    uint32_t cpu_format_mask;
} robo_gfx_capabilities_t;

void robo_gfx_capabilities_init(robo_gfx_capabilities_t *caps);

robo_result_t robo_gfx_query_capabilities(robo_gfx_capabilities_t *out);

int robo_gfx_capabilities_supports_cpu_format(const robo_gfx_capabilities_t *caps,
                                              robo_gfx_pixel_format_t format);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_CAPABILITIES_H */
