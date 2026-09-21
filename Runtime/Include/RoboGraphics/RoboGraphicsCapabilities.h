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

typedef struct GfxCapabilities {
    int cpuRasterAvailable;
    int gpuBackendAvailable;
    int framebufferPresentAvailable;
    int nativeWindowAvailable;
    uint32_t maxSurfaceWidth;
    uint32_t maxSurfaceHeight;
    /** Bit (1u << format) for known formats with CPU create support. */
    uint32_t cpuFormatMask;
} GfxCapabilities;

void GfxCapabilitiesInit(GfxCapabilities *caps);

Result GfxQueryCapabilities(GfxCapabilities *out);

int GfxCapabilitiesSupportsCpuFormat(const GfxCapabilities *caps,
                                              GfxPixelFormat format);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_CAPABILITIES_H */
