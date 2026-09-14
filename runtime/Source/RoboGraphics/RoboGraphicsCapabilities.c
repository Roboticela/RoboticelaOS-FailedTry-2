#include "RoboGraphics/RoboGraphicsCapabilities.h"

#include "RoboGraphics/RoboGraphicsTypes.h"

#include <string.h>

void robo_gfx_capabilities_init(robo_gfx_capabilities_t *caps)
{
    if (!caps) {
        return;
    }
    memset(caps, 0, sizeof(*caps));
}

robo_result_t robo_gfx_query_capabilities(robo_gfx_capabilities_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_gfx_capabilities_init(out);
    out->cpu_raster_available = 1;
    out->gpu_backend_available = 0;
    out->framebuffer_present_available = 0;
    out->native_window_available = 0;
    out->max_surface_width = ROBO_GFX_MAX_SURFACE_DIM;
    out->max_surface_height = ROBO_GFX_MAX_SURFACE_DIM;
    out->cpu_format_mask = (1u << ROBO_GFX_FORMAT_RGBA8) | (1u << ROBO_GFX_FORMAT_BGRA8) |
                           (1u << ROBO_GFX_FORMAT_RGB8) | (1u << ROBO_GFX_FORMAT_GRAY8) |
                           (1u << ROBO_GFX_FORMAT_ALPHA8);
    return ROBO_OK;
}

int robo_gfx_capabilities_supports_cpu_format(const robo_gfx_capabilities_t *caps,
                                              robo_gfx_pixel_format_t format)
{
    if (!caps || format == ROBO_GFX_FORMAT_UNKNOWN) {
        return 0;
    }
    if ((uint32_t)format >= 32u) {
        return 0;
    }
    return (caps->cpu_format_mask & (1u << (uint32_t)format)) != 0u;
}
