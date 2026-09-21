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

typedef struct GfxSurface GfxSurface;

enum {
    RoboGfxBackendNone = 0,
    RoboGfxBackendCpuRaster = 1,
    RoboGfxBackendGpu = 2,                 /* future */
    RoboGfxBackendFramebufferPresent = 3, /* QEMU / raw FB — future */
    RoboGfxBackendNativeWindow = 4        /* host OS window — future */
};

typedef enum GfxBackendKind {
    RoboGfxBackendKindRaster = 1,
    RoboGfxBackendKindPresent = 2
} GfxBackendKind;

typedef struct GfxBackendInfo {
    GfxBackendId id;
    GfxBackendKind kind;
    const char *name;
    int available; /* 1 if usable in this build/runtime */
} GfxBackendInfo;

/** Number of known backend slots (including unavailable stubs). */
size_t GfxBackendCount(void);

Result GfxBackendAt(size_t index, GfxBackendInfo *out);

Result GfxBackendFind(GfxBackendId id,
                                    GfxBackendInfo *out);

/**
 * Presentation boundary stub: accepts a finished surface without displaying.
 * Used by headless tests. Real presenters arrive with RoboWindow / QEMU.
 */
Result GfxPresentNull(GfxSurface *surface);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_BACKEND_H */
