#include "RoboGraphics/RoboGraphicsFramebuffer.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct GfxFramebuffer {
    uint32_t refcount;
    GfxFramebufferId id;
    GfxFramebufferOwnership ownership;
    uint32_t width;
    uint32_t height;
    GfxPixelFormat format;
    uint8_t bytesPerPixel;
    size_t strideBytes;
    size_t byteSize;
    uint8_t *pixels;
    int pixelsOwned;
};

static uint64_t gFramebufferIdSeq = 1u;

void GfxFramebufferDescInit(GfxFramebufferDesc *desc)
{
    if (!desc) {
        return;
    }
    memset(desc, 0, sizeof(*desc));
    desc->format = RoboGfxFormatRgba8;
}

static Result ValidateAndResolve(const GfxFramebufferDesc *desc,
                                          size_t *outStride,
                                          uint8_t *outBpp,
                                          size_t *outBytes)
{
    size_t stride = 0u;
    size_t bytes = 0u;
    uint8_t bpp = 0u;
    Result res;

    if (!desc || !outStride || !outBpp || !outBytes) {
        return RoboInvalidArgument;
    }
    if (desc->width == 0u || desc->height == 0u) {
        return RoboInvalidArgument;
    }
    if (desc->width > RoboGfxMaxSurfaceDim ||
        desc->height > RoboGfxMaxSurfaceDim) {
        return RoboInvalidArgument;
    }
    if (!GfxPixelFormatIsImplemented(desc->format)) {
        if (GfxPixelFormatIsKnown(desc->format)) {
            return RoboNotSupported;
        }
        return RoboInvalidArgument;
    }
    res = GfxPixelFormatBytesPerPixel(desc->format, &bpp);
    if (res != RoboOk) {
        return res;
    }
    if (desc->strideBytes == 0u) {
        res = GfxPixelFormatMinStride(desc->format, desc->width, &stride);
        if (res != RoboOk) {
            return res;
        }
    } else {
        res = GfxPixelFormatValidateStride(desc->format, desc->width,
                                                    desc->strideBytes);
        if (res != RoboOk) {
            return res;
        }
        stride = desc->strideBytes;
    }
    res = GfxPixelFormatBufferBytes(desc->format, desc->width, desc->height,
                                             stride, &bytes);
    if (res != RoboOk) {
        return res;
    }
    *outStride = stride;
    *outBpp = bpp;
    *outBytes = bytes;
    return RoboOk;
}

static GfxFramebuffer *AllocFbObject(void)
{
    GfxFramebuffer *fb =
        (GfxFramebuffer *)RoboCalloc(1, sizeof(*fb));
    if (!fb) {
        return NULL;
    }
    fb->refcount = 1u;
    fb->id = gFramebufferIdSeq++;
    if (fb->id == RoboGfxFramebufferIdNone) {
        fb->id = gFramebufferIdSeq++;
    }
    return fb;
}

Result GfxFramebufferCreate(const GfxFramebufferDesc *desc,
                                          GfxFramebuffer **out)
{
    GfxFramebuffer *fb;
    size_t stride = 0u;
    size_t bytes = 0u;
    uint8_t bpp = 0u;
    Result res;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    res = ValidateAndResolve(desc, &stride, &bpp, &bytes);
    if (res != RoboOk) {
        return res;
    }
    fb = AllocFbObject();
    if (!fb) {
        return RoboOutOfMemory;
    }
    fb->pixels = (uint8_t *)RoboCalloc(1, bytes);
    if (!fb->pixels) {
        RoboFree(fb);
        return RoboOutOfMemory;
    }
    fb->ownership = RoboGfxFramebufferOwned;
    fb->width = desc->width;
    fb->height = desc->height;
    fb->format = desc->format;
    fb->bytesPerPixel = bpp;
    fb->strideBytes = stride;
    fb->byteSize = bytes;
    fb->pixelsOwned = 1;
    *out = fb;
    return RoboOk;
}

Result GfxFramebufferCreateBorrowed(
    const GfxFramebufferDesc *desc,
    void *pixels,
    GfxFramebuffer **out)
{
    GfxFramebuffer *fb;
    size_t stride = 0u;
    size_t bytes = 0u;
    uint8_t bpp = 0u;
    Result res;

    if (!out || !pixels) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    res = ValidateAndResolve(desc, &stride, &bpp, &bytes);
    if (res != RoboOk) {
        return res;
    }
    fb = AllocFbObject();
    if (!fb) {
        return RoboOutOfMemory;
    }
    fb->ownership = RoboGfxFramebufferBorrowed;
    fb->width = desc->width;
    fb->height = desc->height;
    fb->format = desc->format;
    fb->bytesPerPixel = bpp;
    fb->strideBytes = stride;
    fb->byteSize = bytes;
    fb->pixels = (uint8_t *)pixels;
    fb->pixelsOwned = 0;
    *out = fb;
    return RoboOk;
}

void GfxFramebufferRetain(GfxFramebuffer *fb)
{
    if (!fb) {
        return;
    }
    fb->refcount++;
}

void GfxFramebufferRelease(GfxFramebuffer *fb)
{
    if (!fb) {
        return;
    }
    if (fb->refcount == 0u) {
        return;
    }
    fb->refcount--;
    if (fb->refcount != 0u) {
        return;
    }
    if (fb->pixelsOwned && fb->pixels) {
        RoboFree(fb->pixels);
    }
    RoboFree(fb);
}

GfxFramebufferId GetGfxFramebufferId(const GfxFramebuffer *fb)
{
    return fb ? fb->id : RoboGfxFramebufferIdNone;
}

GfxFramebufferOwnership MakeGfxFramebufferOwnership(
    const GfxFramebuffer *fb)
{
    return fb ? fb->ownership : (GfxFramebufferOwnership)0;
}

uint32_t GfxFramebufferWidth(const GfxFramebuffer *fb)
{
    return fb ? fb->width : 0u;
}

uint32_t GfxFramebufferHeight(const GfxFramebuffer *fb)
{
    return fb ? fb->height : 0u;
}

GfxPixelFormat GfxFramebufferFormat(const GfxFramebuffer *fb)
{
    return fb ? fb->format : RoboGfxFormatUnknown;
}

uint8_t GfxFramebufferBytesPerPixel(const GfxFramebuffer *fb)
{
    return fb ? fb->bytesPerPixel : 0u;
}

size_t GfxFramebufferStride(const GfxFramebuffer *fb)
{
    return fb ? fb->strideBytes : 0u;
}

size_t GfxFramebufferByteSize(const GfxFramebuffer *fb)
{
    return fb ? fb->byteSize : 0u;
}

uint8_t *GfxFramebufferPixels(GfxFramebuffer *fb)
{
    return fb ? fb->pixels : NULL;
}

const uint8_t *GfxFramebufferPixelsConst(const GfxFramebuffer *fb)
{
    return fb ? fb->pixels : NULL;
}

Result GfxFramebufferRow(GfxFramebuffer *fb,
                                       uint32_t y,
                                       uint8_t **outRow)
{
    if (!outRow) {
        return RoboInvalidArgument;
    }
    *outRow = NULL;
    if (!fb || !fb->pixels || y >= fb->height) {
        return RoboInvalidArgument;
    }
    *outRow = fb->pixels + (size_t)y * fb->strideBytes;
    return RoboOk;
}

Result GfxFramebufferRowConst(const GfxFramebuffer *fb,
                                             uint32_t y,
                                             const uint8_t **outRow)
{
    if (!outRow) {
        return RoboInvalidArgument;
    }
    *outRow = NULL;
    if (!fb || !fb->pixels || y >= fb->height) {
        return RoboInvalidArgument;
    }
    *outRow = fb->pixels + (size_t)y * fb->strideBytes;
    return RoboOk;
}

Result GfxFramebufferPixelOffset(const GfxFramebuffer *fb,
                                                uint32_t x,
                                                uint32_t y,
                                                size_t *outOffset)
{
    if (!fb || !outOffset) {
        return RoboInvalidArgument;
    }
    if (x >= fb->width || y >= fb->height) {
        return RoboInvalidArgument;
    }
    return GfxPixelFormatPixelOffset(fb->format, fb->strideBytes, x, y,
                                              outOffset);
}

Result GfxFramebufferPixel(GfxFramebuffer *fb,
                                         uint32_t x,
                                         uint32_t y,
                                         uint8_t **outPixel)
{
    size_t off = 0u;
    Result res;

    if (!outPixel) {
        return RoboInvalidArgument;
    }
    *outPixel = NULL;
    if (!fb || !fb->pixels) {
        return RoboInvalidArgument;
    }
    res = GfxFramebufferPixelOffset(fb, x, y, &off);
    if (res != RoboOk) {
        return res;
    }
    *outPixel = fb->pixels + off;
    return RoboOk;
}

Result GfxFramebufferPixelConst(const GfxFramebuffer *fb,
                                               uint32_t x,
                                               uint32_t y,
                                               const uint8_t **outPixel)
{
    size_t off = 0u;
    Result res;

    if (!outPixel) {
        return RoboInvalidArgument;
    }
    *outPixel = NULL;
    if (!fb || !fb->pixels) {
        return RoboInvalidArgument;
    }
    res = GfxFramebufferPixelOffset(fb, x, y, &off);
    if (res != RoboOk) {
        return res;
    }
    *outPixel = fb->pixels + off;
    return RoboOk;
}
