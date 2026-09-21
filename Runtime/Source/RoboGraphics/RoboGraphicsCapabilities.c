#include "RoboGraphics/RoboGraphicsCapabilities.h"

#include "RoboGraphics/RoboGraphicsTypes.h"

#include <string.h>

void GfxCapabilitiesInit(GfxCapabilities *caps)
{
    if (!caps) {
        return;
    }
    memset(caps, 0, sizeof(*caps));
}

Result GfxQueryCapabilities(GfxCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    GfxCapabilitiesInit(out);
    out->cpuRasterAvailable = 1;
    out->gpuBackendAvailable = 0;
    out->framebufferPresentAvailable = 0;
    out->nativeWindowAvailable = 0;
    out->maxSurfaceWidth = RoboGfxMaxSurfaceDim;
    out->maxSurfaceHeight = RoboGfxMaxSurfaceDim;
    out->cpuFormatMask = (1u << RoboGfxFormatRgba8) | (1u << RoboGfxFormatBgra8) |
                           (1u << RoboGfxFormatRgb8) | (1u << RoboGfxFormatGray8) |
                           (1u << RoboGfxFormatAlpha8);
    return RoboOk;
}

int GfxCapabilitiesSupportsCpuFormat(const GfxCapabilities *caps,
                                              GfxPixelFormat format)
{
    if (!caps || format == RoboGfxFormatUnknown) {
        return 0;
    }
    if ((uint32_t)format >= 32u) {
        return 0;
    }
    return (caps->cpuFormatMask & (1u << (uint32_t)format)) != 0u;
}
