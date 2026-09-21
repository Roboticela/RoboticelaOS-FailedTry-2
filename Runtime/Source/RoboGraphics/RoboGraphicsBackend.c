#include "RoboGraphics/RoboGraphicsBackend.h"

#include "RoboGraphics/RoboGraphicsSurface.h"

static const GfxBackendInfo kBackends[] = {
    {RoboGfxBackendCpuRaster, RoboGfxBackendKindRaster, "cpu-raster", 1},
    {RoboGfxBackendGpu, RoboGfxBackendKindRaster, "gpu", 0},
    {RoboGfxBackendFramebufferPresent, RoboGfxBackendKindPresent,
     "framebuffer-present", 0},
    {RoboGfxBackendNativeWindow, RoboGfxBackendKindPresent, "native-window",
     0},
};

size_t GfxBackendCount(void)
{
    return sizeof(kBackends) / sizeof(kBackends[0]);
}

Result GfxBackendAt(size_t index, GfxBackendInfo *out)
{
    if (!out || index >= GfxBackendCount()) {
        return RoboInvalidArgument;
    }
    *out = kBackends[index];
    return RoboOk;
}

Result GfxBackendFind(GfxBackendId id,
                                    GfxBackendInfo *out)
{
    size_t i;
    if (!out || id == RoboGfxBackendNone) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < GfxBackendCount(); i++) {
        if (kBackends[i].id == id) {
            *out = kBackends[i];
            return RoboOk;
        }
    }
    return RoboNotFound;
}

Result GfxPresentNull(GfxSurface *surface)
{
    if (!surface) {
        return RoboInvalidArgument;
    }
    if (!GfxSurfacePixelsConst(surface)) {
        return RoboInvalidArgument;
    }
    /* Headless present: validate only. */
    return RoboOk;
}
