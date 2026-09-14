#include "RoboGraphics/RoboGraphicsBackend.h"

#include "RoboGraphics/RoboGraphicsSurface.h"

static const robo_gfx_backend_info_t k_backends[] = {
    {ROBO_GFX_BACKEND_CPU_RASTER, ROBO_GFX_BACKEND_KIND_RASTER, "cpu-raster", 1},
    {ROBO_GFX_BACKEND_GPU, ROBO_GFX_BACKEND_KIND_RASTER, "gpu", 0},
    {ROBO_GFX_BACKEND_FRAMEBUFFER_PRESENT, ROBO_GFX_BACKEND_KIND_PRESENT,
     "framebuffer-present", 0},
    {ROBO_GFX_BACKEND_NATIVE_WINDOW, ROBO_GFX_BACKEND_KIND_PRESENT, "native-window",
     0},
};

size_t robo_gfx_backend_count(void)
{
    return sizeof(k_backends) / sizeof(k_backends[0]);
}

robo_result_t robo_gfx_backend_at(size_t index, robo_gfx_backend_info_t *out)
{
    if (!out || index >= robo_gfx_backend_count()) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = k_backends[index];
    return ROBO_OK;
}

robo_result_t robo_gfx_backend_find(robo_gfx_backend_id_t id,
                                    robo_gfx_backend_info_t *out)
{
    size_t i;
    if (!out || id == ROBO_GFX_BACKEND_NONE) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < robo_gfx_backend_count(); i++) {
        if (k_backends[i].id == id) {
            *out = k_backends[i];
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_gfx_present_null(robo_gfx_surface_t *surface)
{
    if (!surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_surface_pixels_const(surface)) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Headless present: validate only. */
    return ROBO_OK;
}
