#include "RoboWindow/RoboWindowPresent.h"

#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackend.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static Result ValidateFormatAndSize(uint32_t width,
                                              uint32_t height,
                                              GfxPixelFormat format,
                                              size_t strideBytes,
                                              const WindowBackendCaps *caps)
{
    GfxPixelFormatInfo info;
    Result r;
    size_t minStride = 0u;

    if (width == 0u || height == 0u) {
        return RoboInvalidArgument;
    }
    if (width > RoboGfxMaxSurfaceDim || height > RoboGfxMaxSurfaceDim) {
        return RoboInvalidArgument;
    }
    r = GetGfxPixelFormatInfo(format, &info);
    if (r != RoboOk) {
        return RoboInvalidArgument;
    }
    if (!info.isImplemented) {
        return RoboNotSupported;
    }
    r = GfxPixelFormatValidateStride(format, width, strideBytes);
    if (r != RoboOk) {
        return r;
    }
    r = GfxPixelFormatMinStride(format, width, &minStride);
    if (r != RoboOk) {
        return r;
    }
    (void)minStride;

    if (caps) {
        if (caps->presentFormatMask != 0u) {
            if ((caps->presentFormatMask &
                 RoboWindowPresentFmtBit(format)) == 0u) {
                return RoboNotSupported;
            }
        } else if (caps->present) {
            /* present enabled but empty mask → nothing presentable */
            return RoboNotSupported;
        }
        if (caps->presentMaxWidth != 0u && width > caps->presentMaxWidth) {
            return RoboNotSupported;
        }
        if (caps->presentMaxHeight != 0u && height > caps->presentMaxHeight) {
            return RoboNotSupported;
        }
    }
    return RoboOk;
}

Result WindowPresentValidateFramebuffer(
    const GfxFramebuffer *fb,
    const WindowBackendCaps *caps)
{
    const uint8_t *pixels;

    if (!fb) {
        return RoboInvalidArgument;
    }
    pixels = GfxFramebufferPixelsConst(fb);
    if (!pixels) {
        return RoboInvalidArgument;
    }
    return ValidateFormatAndSize(GfxFramebufferWidth(fb),
                                    GfxFramebufferHeight(fb),
                                    GfxFramebufferFormat(fb),
                                    GfxFramebufferStride(fb),
                                    caps);
}

Result WindowPresentValidateSurface(
    const GfxSurface *surface,
    const WindowBackendCaps *caps)
{
    const uint8_t *pixels;
    GfxFramebuffer *fb;

    if (!surface) {
        return RoboInvalidArgument;
    }
    /* Require readable CPU pixels for Goal 7.4 synchronous present. */
    pixels = GfxSurfacePixelsConst(surface);
    if (!pixels) {
        return RoboInvalidArgument;
    }
    fb = GfxSurfaceFramebuffer((GfxSurface *)surface);
    if (!fb) {
        return RoboInvalidArgument;
    }
    return ValidateFormatAndSize(GfxSurfaceWidth(surface),
                                    GfxSurfaceHeight(surface),
                                    GfxSurfaceFormat(surface),
                                    GfxSurfaceStride(surface),
                                    caps);
}

int WindowPresentFormatSupported(GfxPixelFormat format)
{
    WindowBackendCaps caps;

    if (WindowBackendCapabilities(&caps) != RoboOk || !caps.present) {
        return 0;
    }
    if (caps.presentFormatMask == 0u) {
        return 0;
    }
    return (caps.presentFormatMask & RoboWindowPresentFmtBit(format)) != 0u;
}

Result WindowPresentFramebuffer(WindowId id,
                                              GfxFramebuffer *fb)
{
    GfxSurfaceDesc desc;
    GfxSurface *surf = NULL;
    WindowBackendCaps caps;
    Result r;

    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    r = WindowThreadRequireOwner();
    if (r != RoboOk) {
        return r;
    }
    if (!fb) {
        return RoboInvalidArgument;
    }
    if (!WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    if (WindowBackendCapabilities(&caps) != RoboOk || !caps.present) {
        return RoboNotSupported;
    }
    r = WindowPresentValidateFramebuffer(fb, &caps);
    if (r != RoboOk) {
        return r;
    }

    GfxSurfaceDescInit(&desc);
    desc.width = GfxFramebufferWidth(fb);
    desc.height = GfxFramebufferHeight(fb);
    desc.format = GfxFramebufferFormat(fb);
    desc.strideBytes = GfxFramebufferStride(fb);
    desc.access = RoboGfxSurfaceAccessRead;
    r = GfxSurfaceCreateBorrowed(
        &desc, (void *)GfxFramebufferPixels(fb), &surf);
    if (r != RoboOk) {
        return r;
    }
    r = WindowPresent(id, surf);
    GfxSurfaceRelease(surf);
    return r;
}
