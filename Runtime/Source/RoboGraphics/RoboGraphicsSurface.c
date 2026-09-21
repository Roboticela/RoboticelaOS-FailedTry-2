#include "RoboGraphics/RoboGraphicsSurface.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct GfxSurface {
    uint32_t refcount;
    GfxSurfaceId id;
    GfxSurfaceOwnership ownership;
    GfxSurfaceAccess access;
    GfxFramebuffer *fb; /* retained; CPU backing for Goal 6.4 */
    int mapped;
    GfxSurfaceAccess mapAccess;
};

static uint64_t gSurfaceIdSeq = 1u;

static int AccessIsValid(GfxSurfaceAccess access)
{
    return access == RoboGfxSurfaceAccessRead ||
           access == RoboGfxSurfaceAccessWrite ||
           access == RoboGfxSurfaceAccessReadWrite;
}

static int AccessAllows(GfxSurfaceAccess allowed,
                         GfxSurfaceAccess requested)
{
    if (!AccessIsValid(allowed) || !AccessIsValid(requested)) {
        return 0;
    }
    if (requested == RoboGfxSurfaceAccessReadWrite) {
        return allowed == RoboGfxSurfaceAccessReadWrite;
    }
    if (requested == RoboGfxSurfaceAccessRead) {
        return allowed == RoboGfxSurfaceAccessRead ||
               allowed == RoboGfxSurfaceAccessReadWrite;
    }
    /* WRITE */
    return allowed == RoboGfxSurfaceAccessWrite ||
           allowed == RoboGfxSurfaceAccessReadWrite;
}

static int AccessCanWrite(GfxSurfaceAccess access)
{
    return access == RoboGfxSurfaceAccessWrite ||
           access == RoboGfxSurfaceAccessReadWrite;
}

void GfxSurfaceDescInit(GfxSurfaceDesc *desc)
{
    if (!desc) {
        return;
    }
    memset(desc, 0, sizeof(*desc));
    desc->format = RoboGfxFormatRgba8;
    desc->access = RoboGfxSurfaceAccessReadWrite;
}

static Result ResolveAccess(const GfxSurfaceDesc *desc,
                                    GfxSurfaceAccess *outAccess)
{
    GfxSurfaceAccess access;

    if (!desc || !outAccess) {
        return RoboInvalidArgument;
    }
    access = desc->access ? desc->access : RoboGfxSurfaceAccessReadWrite;
    if (!AccessIsValid(access)) {
        return RoboInvalidArgument;
    }
    *outAccess = access;
    return RoboOk;
}

static GfxSurface *AllocSurfaceObject(void)
{
    GfxSurface *s = (GfxSurface *)RoboCalloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->refcount = 1u;
    s->id = gSurfaceIdSeq++;
    if (s->id == RoboGfxSurfaceIdNone) {
        s->id = gSurfaceIdSeq++;
    }
    return s;
}

Result GfxSurfaceCreate(const GfxSurfaceDesc *desc,
                                      GfxSurface **out)
{
    GfxSurface *s;
    GfxFramebufferDesc fbDesc;
    GfxFramebuffer *fb = NULL;
    GfxSurfaceAccess access = RoboGfxSurfaceAccessReadWrite;
    Result res;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (!desc) {
        return RoboInvalidArgument;
    }
    res = ResolveAccess(desc, &access);
    if (res != RoboOk) {
        return res;
    }

    GfxFramebufferDescInit(&fbDesc);
    fbDesc.width = desc->width;
    fbDesc.height = desc->height;
    fbDesc.format = desc->format;
    fbDesc.strideBytes = desc->strideBytes;
    res = GfxFramebufferCreate(&fbDesc, &fb);
    if (res != RoboOk) {
        return res;
    }

    s = AllocSurfaceObject();
    if (!s) {
        GfxFramebufferRelease(fb);
        return RoboOutOfMemory;
    }
    s->ownership = RoboGfxSurfaceOwned;
    s->access = access;
    s->fb = fb;
    *out = s;
    return RoboOk;
}

Result GfxSurfaceCreateBorrowed(const GfxSurfaceDesc *desc,
                                               void *pixels,
                                               GfxSurface **out)
{
    GfxSurface *s;
    GfxFramebufferDesc fbDesc;
    GfxFramebuffer *fb = NULL;
    GfxSurfaceAccess access = RoboGfxSurfaceAccessReadWrite;
    Result res;

    if (!out || !pixels) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (!desc) {
        return RoboInvalidArgument;
    }
    res = ResolveAccess(desc, &access);
    if (res != RoboOk) {
        return res;
    }

    GfxFramebufferDescInit(&fbDesc);
    fbDesc.width = desc->width;
    fbDesc.height = desc->height;
    fbDesc.format = desc->format;
    fbDesc.strideBytes = desc->strideBytes;
    res = GfxFramebufferCreateBorrowed(&fbDesc, pixels, &fb);
    if (res != RoboOk) {
        return res;
    }

    s = AllocSurfaceObject();
    if (!s) {
        GfxFramebufferRelease(fb);
        return RoboOutOfMemory;
    }
    s->ownership = RoboGfxSurfaceBorrowed;
    s->access = access;
    s->fb = fb;
    *out = s;
    return RoboOk;
}

void GfxSurfaceRetain(GfxSurface *surface)
{
    if (!surface) {
        return;
    }
    surface->refcount++;
}

void GfxSurfaceRelease(GfxSurface *surface)
{
    if (!surface) {
        return;
    }
    if (surface->refcount == 0u) {
        return;
    }
    surface->refcount--;
    if (surface->refcount != 0u) {
        return;
    }
    /* Ending a mapping on final release is safe; pointers become invalid. */
    surface->mapped = 0;
    surface->mapAccess = (GfxSurfaceAccess)0;
    if (surface->fb) {
        GfxFramebufferRelease(surface->fb);
        surface->fb = NULL;
    }
    RoboFree(surface);
}

GfxSurfaceId GetGfxSurfaceId(const GfxSurface *surface)
{
    return surface ? surface->id : RoboGfxSurfaceIdNone;
}

GfxSurfaceOwnership MakeGfxSurfaceOwnership(
    const GfxSurface *surface)
{
    return surface ? surface->ownership : (GfxSurfaceOwnership)0;
}

uint32_t GfxSurfaceWidth(const GfxSurface *surface)
{
    return surface && surface->fb ? GfxFramebufferWidth(surface->fb) : 0u;
}

uint32_t GfxSurfaceHeight(const GfxSurface *surface)
{
    return surface && surface->fb ? GfxFramebufferHeight(surface->fb) : 0u;
}

GfxSize GfxSurfaceSize(const GfxSurface *surface)
{
    if (!surface || !surface->fb) {
        return MakeGfxSize(0, 0);
    }
    return MakeGfxSize((int32_t)GfxFramebufferWidth(surface->fb),
                         (int32_t)GfxFramebufferHeight(surface->fb));
}

GfxPixelFormat GfxSurfaceFormat(const GfxSurface *surface)
{
    return surface && surface->fb ? GfxFramebufferFormat(surface->fb)
                                  : RoboGfxFormatUnknown;
}

uint8_t GfxSurfaceBytesPerPixel(const GfxSurface *surface)
{
    return surface && surface->fb ? GfxFramebufferBytesPerPixel(surface->fb)
                                  : 0u;
}

size_t GfxSurfaceStride(const GfxSurface *surface)
{
    return surface && surface->fb ? GfxFramebufferStride(surface->fb) : 0u;
}

size_t GfxSurfaceByteSize(const GfxSurface *surface)
{
    return surface && surface->fb ? GfxFramebufferByteSize(surface->fb) : 0u;
}

GfxSurfaceAccess MakeGfxSurfaceAccess(const GfxSurface *surface)
{
    return surface ? surface->access : (GfxSurfaceAccess)0;
}

int GfxSurfaceIsMapped(const GfxSurface *surface)
{
    return surface && surface->mapped ? 1 : 0;
}

Result GetGfxSurfaceMap(GfxSurface *surface,
                                   GfxSurfaceAccess access,
                                   GfxSurfaceMap *out)
{
    const uint8_t *base;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (!surface || !surface->fb) {
        return RoboInvalidArgument;
    }
    if (surface->mapped) {
        return RoboBusy;
    }
    if (!AccessAllows(surface->access, access)) {
        return RoboInvalidArgument;
    }
    base = GfxFramebufferPixelsConst(surface->fb);
    if (!base) {
        return RoboInvalidArgument;
    }

    out->pixelsConst = base;
    out->pixels = AccessCanWrite(access)
                      ? GfxFramebufferPixels(surface->fb)
                      : NULL;
    out->width = GfxFramebufferWidth(surface->fb);
    out->height = GfxFramebufferHeight(surface->fb);
    out->strideBytes = GfxFramebufferStride(surface->fb);
    out->byteSize = GfxFramebufferByteSize(surface->fb);
    out->format = GfxFramebufferFormat(surface->fb);
    out->access = access;

    surface->mapped = 1;
    surface->mapAccess = access;
    return RoboOk;
}

Result GfxSurfaceUnmap(GfxSurface *surface)
{
    if (!surface) {
        return RoboInvalidArgument;
    }
    if (!surface->mapped) {
        return RoboInvalidArgument;
    }
    surface->mapped = 0;
    surface->mapAccess = (GfxSurfaceAccess)0;
    return RoboOk;
}

uint8_t *GfxSurfacePixels(GfxSurface *surface)
{
    if (!surface || !surface->fb || !AccessCanWrite(surface->access)) {
        return NULL;
    }
    return GfxFramebufferPixels(surface->fb);
}

const uint8_t *GfxSurfacePixelsConst(const GfxSurface *surface)
{
    if (!surface || !surface->fb) {
        return NULL;
    }
    /* CPU backing is addressable for inspection; map() still enforces access. */
    return GfxFramebufferPixelsConst(surface->fb);
}

uint8_t *GfxSurfaceRow(GfxSurface *surface, uint32_t y)
{
    uint8_t *row = NULL;
    if (GfxSurfaceRowChecked(surface, y, &row) != RoboOk) {
        return NULL;
    }
    return row;
}

Result GfxSurfaceRowChecked(GfxSurface *surface,
                                           uint32_t y,
                                           uint8_t **outRow)
{
    if (!outRow) {
        return RoboInvalidArgument;
    }
    *outRow = NULL;
    if (!surface || !surface->fb || !AccessCanWrite(surface->access)) {
        return RoboInvalidArgument;
    }
    return GfxFramebufferRow(surface->fb, y, outRow);
}

Result GfxSurfaceRowConst(const GfxSurface *surface,
                                         uint32_t y,
                                         const uint8_t **outRow)
{
    if (!outRow) {
        return RoboInvalidArgument;
    }
    *outRow = NULL;
    if (!surface || !surface->fb) {
        return RoboInvalidArgument;
    }
    return GfxFramebufferRowConst(surface->fb, y, outRow);
}

Result GfxSurfacePixelOffset(const GfxSurface *surface,
                                            uint32_t x,
                                            uint32_t y,
                                            size_t *outOffset)
{
    if (!surface || !surface->fb) {
        return RoboInvalidArgument;
    }
    return GfxFramebufferPixelOffset(surface->fb, x, y, outOffset);
}

Result GfxSurfacePixel(GfxSurface *surface,
                                     uint32_t x,
                                     uint32_t y,
                                     uint8_t **outPixel)
{
    if (!outPixel) {
        return RoboInvalidArgument;
    }
    *outPixel = NULL;
    if (!surface || !surface->fb || !AccessCanWrite(surface->access)) {
        return RoboInvalidArgument;
    }
    return GfxFramebufferPixel(surface->fb, x, y, outPixel);
}

Result GfxSurfacePixelConst(const GfxSurface *surface,
                                           uint32_t x,
                                           uint32_t y,
                                           const uint8_t **outPixel)
{
    if (!outPixel) {
        return RoboInvalidArgument;
    }
    *outPixel = NULL;
    if (!surface || !surface->fb) {
        return RoboInvalidArgument;
    }
    return GfxFramebufferPixelConst(surface->fb, x, y, outPixel);
}

Result GfxSurfaceClearRgba8(GfxSurface *surface,
                                           GfxColorRgba8 color)
{
    return GfxSurfaceClear(surface, color);
}

GfxFramebuffer *GfxSurfaceFramebuffer(GfxSurface *surface)
{
    return surface ? surface->fb : NULL;
}
