/*
 * RoboGraphics backend abstraction (Goal 6.1).
 *
 * Identifiers + enumeration only. No Vulkan/GL/D3D/Metal/Skia.
 * Presentation is a separate concern from raster backends.
 */

#ifndef ROBO_GRAPHICS_BACKEND_H
#define ROBO_GRAPHICS_BACKEND_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_gfx_surface robo_gfx_surface_t;

enum {
    ROBO_GFX_BACKEND_NONE = 0,
    ROBO_GFX_BACKEND_CPU_RASTER = 1,
    ROBO_GFX_BACKEND_GPU = 2,                 /* future */
    ROBO_GFX_BACKEND_FRAMEBUFFER_PRESENT = 3, /* QEMU / raw FB — future */
    ROBO_GFX_BACKEND_NATIVE_WINDOW = 4        /* host OS window — future */
};

typedef enum robo_gfx_backend_kind {
    ROBO_GFX_BACKEND_KIND_RASTER = 1,
    ROBO_GFX_BACKEND_KIND_PRESENT = 2
} robo_gfx_backend_kind_t;

typedef struct robo_gfx_backend_info {
    robo_gfx_backend_id_t id;
    robo_gfx_backend_kind_t kind;
    const char *name;
    int available; /* 1 if usable in this build/runtime */
} robo_gfx_backend_info_t;

/** Number of known backend slots (including unavailable stubs). */
size_t robo_gfx_backend_count(void);

robo_result_t robo_gfx_backend_at(size_t index, robo_gfx_backend_info_t *out);

robo_result_t robo_gfx_backend_find(robo_gfx_backend_id_t id,
                                    robo_gfx_backend_info_t *out);

/**
 * Presentation boundary stub: accepts a finished surface without displaying.
 * Used by headless tests. Real presenters arrive with RoboWindow / QEMU.
 */
robo_result_t robo_gfx_present_null(robo_gfx_surface_t *surface);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_BACKEND_H */
