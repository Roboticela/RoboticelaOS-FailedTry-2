/*
 * RoboGraphics Goal 6.14 — surface/framebuffer capture + 6.13 export.
 */

#include "RoboGraphics/RoboGraphicsScreenshot.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct GfxScreenshot {
    uint32_t refcount;
    GfxSurface *surface; /* owned snapshot */
};

static Result ResolveRegion(const GfxRect *bounds,
                                    const GfxCaptureOptions *opts,
                                    GfxRect *outRegion)
{
    GfxRect requested;
    GfxRect clipped;
    Result res;

    if (!bounds || !opts || !outRegion) {
        return RoboInvalidArgument;
    }

    if (opts->useFullExtent) {
        *outRegion = *bounds;
        if (GfxRectIsEmpty(*outRegion)) {
            return RoboInvalidArgument;
        }
        return RoboOk;
    }

    requested = opts->region;
    if (!GfxRectIsValid(requested)) {
        return RoboInvalidArgument;
    }

    if (opts->regionPolicy == RoboGfxCaptureRegionStrict) {
        if (GfxRectIsEmpty(requested)) {
            return RoboInvalidArgument;
        }
        if (!GfxRectEncloses(*bounds, requested)) {
            return RoboInvalidArgument;
        }
        *outRegion = requested;
        return RoboOk;
    }

    if (opts->regionPolicy != RoboGfxCaptureRegionClip) {
        return RoboInvalidArgument;
    }

    res = GfxRectClip(requested, *bounds, &clipped);
    if (res != RoboOk) {
        return res;
    }
    if (GfxRectIsEmpty(clipped)) {
        return RoboInvalidArgument;
    }
    *outRegion = clipped;
    return RoboOk;
}

static Result CopyRectToOwnedSurface(
    const GfxSurface *src,
    GfxRect region,
    GfxSurface **outSnap)
{
    GfxSurfaceDesc desc;
    GfxSurface *snap = NULL;
    Result res;

    if (!src || !outSnap) {
        return RoboInvalidArgument;
    }
    *outSnap = NULL;

    if (GfxRectIsEmpty(region) || region.width < 0 ||
        region.height < 0 || region.x < 0 || region.y < 0) {
        return RoboInvalidArgument;
    }

    GfxSurfaceDescInit(&desc);
    desc.width = (uint32_t)region.width;
    desc.height = (uint32_t)region.height;
    desc.format = GfxSurfaceFormat(src);
    desc.strideBytes = 0u;
    desc.access = RoboGfxSurfaceAccessReadWrite;

    res = GfxSurfaceCreate(&desc, &snap);
    if (res != RoboOk) {
        return res;
    }

    res = GfxSurfaceCopyRect(src, region, snap, MakeGfxPoint(0, 0));
    if (res != RoboOk) {
        GfxSurfaceRelease(snap);
        return res;
    }

    *outSnap = snap;
    return RoboOk;
}

static Result WrapFramebufferAsSurface(
    const GfxFramebuffer *fb,
    GfxSurface **out)
{
    GfxSurfaceDesc desc;
    const uint8_t *pixels;

    if (!fb || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    pixels = GfxFramebufferPixelsConst(fb);
    if (!pixels) {
        return RoboInvalidArgument;
    }

    GfxSurfaceDescInit(&desc);
    desc.width = GfxFramebufferWidth(fb);
    desc.height = GfxFramebufferHeight(fb);
    desc.format = GfxFramebufferFormat(fb);
    desc.strideBytes = GfxFramebufferStride(fb);
    desc.access = RoboGfxSurfaceAccessRead;

    /* Borrowed view is read-only for the capture copy; storage lifetime = fb. */
    return GfxSurfaceCreateBorrowed(&desc, (void *)pixels, out);
}

static Result MakeScreenshot(GfxSurface *snap,
                                     GfxScreenshot **out)
{
    GfxScreenshot *shot;

    if (!snap || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    shot = (GfxScreenshot *)RoboCalloc(1, sizeof(*shot));
    if (!shot) {
        return RoboOutOfMemory;
    }
    shot->refcount = 1u;
    shot->surface = snap;
    *out = shot;
    return RoboOk;
}

static Result CaptureFromSurface(const GfxSurface *surface,
                                          const GfxCaptureOptions *opts,
                                          GfxScreenshot **out)
{
    GfxRect bounds;
    GfxRect region;
    GfxSurface *snap = NULL;
    Result res;
    GfxCaptureOptions local;

    if (!surface || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    if (!opts) {
        GfxCaptureOptionsInit(&local);
        opts = &local;
    }

    if (!GfxPixelFormatIsImplemented(
            GfxSurfaceFormat(surface))) {
        return RoboNotSupported;
    }
    if (GfxSurfaceWidth(surface) == 0u ||
        GfxSurfaceHeight(surface) == 0u) {
        return RoboInvalidArgument;
    }
    if (!GfxSurfacePixelsConst(surface)) {
        return RoboInvalidArgument;
    }

    res = GfxSurfacePixelBounds(surface, &bounds);
    if (res != RoboOk) {
        return res;
    }
    res = ResolveRegion(&bounds, opts, &region);
    if (res != RoboOk) {
        return res;
    }
    res = CopyRectToOwnedSurface(surface, region, &snap);
    if (res != RoboOk) {
        return res;
    }
    res = MakeScreenshot(snap, out);
    if (res != RoboOk) {
        GfxSurfaceRelease(snap);
    }
    return res;
}

void GfxCaptureOptionsInit(GfxCaptureOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->regionPolicy = RoboGfxCaptureRegionClip;
    opts->useFullExtent = 1;
    opts->region = MakeGfxRect(0, 0, 0, 0);
}

Result GfxScreenshotCaptureSurface(
    const GfxSurface *surface,
    const GfxCaptureOptions *opts,
    GfxScreenshot **out)
{
    return CaptureFromSurface(surface, opts, out);
}

Result GfxScreenshotCaptureFramebuffer(
    const GfxFramebuffer *framebuffer,
    const GfxCaptureOptions *opts,
    GfxScreenshot **out)
{
    GfxSurface *view = NULL;
    Result res;

    if (!framebuffer || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    if (!GfxPixelFormatIsImplemented(
            GfxFramebufferFormat(framebuffer))) {
        return RoboNotSupported;
    }
    if (GfxFramebufferWidth(framebuffer) == 0u ||
        GfxFramebufferHeight(framebuffer) == 0u) {
        return RoboInvalidArgument;
    }

    res = WrapFramebufferAsSurface(framebuffer, &view);
    if (res != RoboOk) {
        return res;
    }
    res = CaptureFromSurface(view, opts, out);
    GfxSurfaceRelease(view);
    return res;
}

Result GfxScreenshotCapture(
    GfxCaptureSource source,
    const GfxSurface *surface,
    const GfxFramebuffer *framebuffer,
    const GfxCaptureOptions *opts,
    GfxScreenshot **out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    switch (source) {
    case RoboGfxCaptureSurface:
        if (!surface) {
            return RoboInvalidArgument;
        }
        return GfxScreenshotCaptureSurface(surface, opts, out);
    case RoboGfxCaptureFramebuffer:
        if (!framebuffer) {
            return RoboInvalidArgument;
        }
        return GfxScreenshotCaptureFramebuffer(framebuffer, opts, out);
    case RoboGfxCaptureFrontBuffer:
    case RoboGfxCaptureBackBuffer:
        /* No present/swap-chain buffers in Goal 6.14. */
        (void)opts;
        return RoboNotSupported;
    default:
        return RoboInvalidArgument;
    }
}

void GfxScreenshotRetain(GfxScreenshot *shot)
{
    if (!shot) {
        return;
    }
    shot->refcount++;
}

void GfxScreenshotRelease(GfxScreenshot *shot)
{
    if (!shot) {
        return;
    }
    if (shot->refcount == 0u) {
        return;
    }
    shot->refcount--;
    if (shot->refcount != 0u) {
        return;
    }
    if (shot->surface) {
        GfxSurfaceRelease(shot->surface);
    }
    RoboFree(shot);
}

uint32_t GfxScreenshotWidth(const GfxScreenshot *shot)
{
    return shot && shot->surface ? GfxSurfaceWidth(shot->surface) : 0u;
}

uint32_t GfxScreenshotHeight(const GfxScreenshot *shot)
{
    return shot && shot->surface ? GfxSurfaceHeight(shot->surface) : 0u;
}

GfxPixelFormat GfxScreenshotFormat(
    const GfxScreenshot *shot)
{
    return shot && shot->surface ? GfxSurfaceFormat(shot->surface)
                                 : RoboGfxFormatUnknown;
}

GfxSurface *GfxScreenshotSurface(GfxScreenshot *shot)
{
    return shot ? shot->surface : NULL;
}

Result GfxScreenshotToImage(GfxScreenshot *shot,
                                           GfxImage **out)
{
    if (!shot || !shot->surface || !out) {
        return RoboInvalidArgument;
    }
    return GfxImageCreateFromSurface(shot->surface, out);
}

Result GfxScreenshotExport(
    const GfxScreenshot *shot,
    const GfxEncodeOptions *encodeOpts,
    GfxEncodedBuffer *out)
{
    if (!shot || !shot->surface || !encodeOpts || !out) {
        return RoboInvalidArgument;
    }
    return GfxSurfaceEncode(shot->surface, encodeOpts, out);
}

Result GfxScreenshotExportToSink(
    const GfxScreenshot *shot,
    const GfxEncodeOptions *encodeOpts,
    GfxEncodeSinkFn sink,
    void *user)
{
    GfxRect bounds;
    Result res;

    if (!shot || !shot->surface || !encodeOpts || !sink) {
        return RoboInvalidArgument;
    }
    res = GfxSurfacePixelBounds(shot->surface, &bounds);
    if (res != RoboOk) {
        return res;
    }
    return GfxSurfaceEncodeToSink(shot->surface, bounds, encodeOpts,
                                           sink, user);
}

Result GfxSurfaceExportScreenshot(
    const GfxSurface *surface,
    const GfxCaptureOptions *captureOpts,
    const GfxEncodeOptions *encodeOpts,
    GfxEncodedBuffer *out)
{
    GfxScreenshot *shot = NULL;
    Result res;

    if (!encodeOpts || !out) {
        return RoboInvalidArgument;
    }
    out->data = NULL;
    out->size = 0u;
    res = GfxScreenshotCaptureSurface(surface, captureOpts, &shot);
    if (res != RoboOk) {
        return res;
    }
    res = GfxScreenshotExport(shot, encodeOpts, out);
    GfxScreenshotRelease(shot);
    return res;
}

Result GfxFramebufferExportScreenshot(
    const GfxFramebuffer *framebuffer,
    const GfxCaptureOptions *captureOpts,
    const GfxEncodeOptions *encodeOpts,
    GfxEncodedBuffer *out)
{
    GfxScreenshot *shot = NULL;
    Result res;

    if (!encodeOpts || !out) {
        return RoboInvalidArgument;
    }
    out->data = NULL;
    out->size = 0u;
    res = GfxScreenshotCaptureFramebuffer(framebuffer, captureOpts,
                                                  &shot);
    if (res != RoboOk) {
        return res;
    }
    res = GfxScreenshotExport(shot, encodeOpts, out);
    GfxScreenshotRelease(shot);
    return res;
}
