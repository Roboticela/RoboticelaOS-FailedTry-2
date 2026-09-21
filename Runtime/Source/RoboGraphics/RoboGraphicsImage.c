#include "RoboGraphics/RoboGraphicsImage.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct GfxImage {
    uint32_t refcount;
    GfxImageId id;
    GfxImageKind kind;
    uint32_t width;
    uint32_t height;
    GfxPixelFormat format;
    GfxSurface *surface; /* retained when present */
};

static uint64_t gImageIdSeq = 1u;

void GfxImageDescInit(GfxImageDesc *desc)
{
    if (!desc) {
        return;
    }
    memset(desc, 0, sizeof(*desc));
    desc->kind = RoboGfxImageKindEmpty;
    desc->format = RoboGfxFormatRgba8;
}

Result GfxImageCreateEmpty(const GfxImageDesc *desc,
                                          GfxImage **out)
{
    GfxImage *img;

    if (!desc || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (desc->width == 0u || desc->height == 0u) {
        return RoboInvalidArgument;
    }
    if (!GfxPixelFormatIsKnown(desc->format)) {
        return RoboInvalidArgument;
    }
    img = (GfxImage *)RoboCalloc(1, sizeof(*img));
    if (!img) {
        return RoboOutOfMemory;
    }
    img->refcount = 1u;
    img->id = gImageIdSeq++;
    if (img->id == RoboGfxImageIdNone) {
        img->id = gImageIdSeq++;
    }
    img->kind = RoboGfxImageKindEmpty;
    img->width = desc->width;
    img->height = desc->height;
    img->format = desc->format;
    img->surface = NULL;
    *out = img;
    return RoboOk;
}

Result GfxImageCreateFromSurface(GfxSurface *surface,
                                                 GfxImage **out)
{
    GfxImage *img;

    if (!surface || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    img = (GfxImage *)RoboCalloc(1, sizeof(*img));
    if (!img) {
        return RoboOutOfMemory;
    }
    img->refcount = 1u;
    img->id = gImageIdSeq++;
    if (img->id == RoboGfxImageIdNone) {
        img->id = gImageIdSeq++;
    }
    img->kind = RoboGfxImageKindRaster;
    img->width = GfxSurfaceWidth(surface);
    img->height = GfxSurfaceHeight(surface);
    img->format = GfxSurfaceFormat(surface);
    GfxSurfaceRetain(surface);
    img->surface = surface;
    *out = img;
    return RoboOk;
}

void GfxImageRetain(GfxImage *image)
{
    if (!image) {
        return;
    }
    image->refcount++;
}

void GfxImageRelease(GfxImage *image)
{
    if (!image) {
        return;
    }
    if (image->refcount == 0u) {
        return;
    }
    image->refcount--;
    if (image->refcount != 0u) {
        return;
    }
    if (image->surface) {
        GfxSurfaceRelease(image->surface);
    }
    RoboFree(image);
}

GfxImageId GetGfxImageId(const GfxImage *image)
{
    return image ? image->id : RoboGfxImageIdNone;
}

GfxImageKind MakeGfxImageKind(const GfxImage *image)
{
    return image ? image->kind : (GfxImageKind)0;
}

uint32_t GfxImageWidth(const GfxImage *image)
{
    return image ? image->width : 0u;
}

uint32_t GfxImageHeight(const GfxImage *image)
{
    return image ? image->height : 0u;
}

GfxPixelFormat GfxImageFormat(const GfxImage *image)
{
    return image ? image->format : RoboGfxFormatUnknown;
}

GfxSurface *GfxImageSurface(GfxImage *image)
{
    return image ? image->surface : NULL;
}
