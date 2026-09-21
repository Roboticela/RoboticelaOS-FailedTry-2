#include "RoboGraphics/RoboGraphicsRaster.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsComposite.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboGraphics/RoboGraphicsLine.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct GfxRaster {
    GfxSurface *target;
    GfxRect clip; /* always ⊆ surface bounds; may be empty */
    uint8_t opacity;
};

static int SurfaceCanWrite(const GfxSurface *surface)
{
    GfxSurfaceAccess a;

    if (!surface) {
        return 0;
    }
    a = MakeGfxSurfaceAccess(surface);
    return a == RoboGfxSurfaceAccessWrite ||
           a == RoboGfxSurfaceAccessReadWrite;
}

static int NeedsBlend(GfxColorRgba8 color, uint8_t opacity)
{
    return !(color.a == 255u && opacity == 255u);
}

static Result FillRectBlended(GfxSurface *surface,
                                       GfxRect rect,
                                       GfxColorRgba8 color,
                                       uint8_t opacity)
{
    int32_t x;
    int32_t y;
    int32_t x1;
    int32_t y1;
    Result res;

    if (GfxRectIsEmpty(rect)) {
        return RoboOk;
    }
    if (!NeedsBlend(color, opacity)) {
        return GfxSurfaceFillRect(surface, rect, color);
    }

    x1 = rect.x + rect.width;
    y1 = rect.y + rect.height;
    for (y = rect.y; y < y1; y++) {
        for (x = rect.x; x < x1; x++) {
            res = GfxSurfaceCompositePixel(surface, (uint32_t)x,
                                                   (uint32_t)y, color, opacity);
            if (res != RoboOk) {
                return res;
            }
        }
    }
    return RoboOk;
}

Result GfxRasterCreate(GfxSurface *target,
                                     GfxRaster **out)
{
    GfxRaster *raster;
    GfxRect bounds;
    Result res;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (!target || !SurfaceCanWrite(target)) {
        return RoboInvalidArgument;
    }
    if (!GfxPixelFormatIsImplemented(GfxSurfaceFormat(target))) {
        return RoboNotSupported;
    }
    res = GfxSurfacePixelBounds(target, &bounds);
    if (res != RoboOk) {
        return res;
    }

    raster = (GfxRaster *)RoboCalloc(1, sizeof(*raster));
    if (!raster) {
        return RoboOutOfMemory;
    }
    GfxSurfaceRetain(target);
    raster->target = target;
    raster->clip = bounds;
    raster->opacity = 255u;
    *out = raster;
    return RoboOk;
}

void GfxRasterDestroy(GfxRaster *raster)
{
    if (!raster) {
        return;
    }
    if (raster->target) {
        GfxSurfaceRelease(raster->target);
        raster->target = NULL;
    }
    RoboFree(raster);
}

GfxSurface *GfxRasterTarget(const GfxRaster *raster)
{
    return raster ? raster->target : NULL;
}

Result GfxRasterClip(const GfxRaster *raster,
                                   GfxRect *outClip)
{
    if (!raster || !outClip) {
        return RoboInvalidArgument;
    }
    *outClip = raster->clip;
    return RoboOk;
}

Result GfxRasterResetClip(GfxRaster *raster)
{
    GfxRect bounds;
    Result res;

    if (!raster || !raster->target) {
        return RoboInvalidArgument;
    }
    res = GfxSurfacePixelBounds(raster->target, &bounds);
    if (res != RoboOk) {
        return res;
    }
    raster->clip = bounds;
    return RoboOk;
}

Result GfxRasterSetClip(GfxRaster *raster,
                                       GfxRect clip)
{
    GfxRect bounds;
    GfxRect effective;
    Result res;

    if (!raster || !raster->target) {
        return RoboInvalidArgument;
    }
    if (!GfxRectIsValid(clip)) {
        return RoboInvalidArgument;
    }
    res = GfxSurfacePixelBounds(raster->target, &bounds);
    if (res != RoboOk) {
        return res;
    }
    res = GfxRectClip(clip, bounds, &effective);
    if (res != RoboOk) {
        return res;
    }
    raster->clip = effective;
    return RoboOk;
}

Result GfxRasterIntersectClip(GfxRaster *raster,
                                             GfxRect clip)
{
    GfxRect next;
    Result res;

    if (!raster) {
        return RoboInvalidArgument;
    }
    if (!GfxRectIsValid(clip)) {
        return RoboInvalidArgument;
    }
    res = GfxRectIntersect(raster->clip, clip, &next);
    if (res != RoboOk) {
        return res;
    }
    raster->clip = next;
    return RoboOk;
}

void GfxRasterSetOpacity(GfxRaster *raster, uint8_t opacity)
{
    if (!raster) {
        return;
    }
    raster->opacity = opacity;
}

uint8_t GfxRasterOpacity(const GfxRaster *raster)
{
    return raster ? raster->opacity : 0u;
}

Result GfxRasterClear(GfxRaster *raster,
                                    GfxColorRgba8 color)
{
    if (!raster || !raster->target) {
        return RoboInvalidArgument;
    }
    return FillRectBlended(raster->target, raster->clip, color,
                             raster->opacity);
}

Result GfxRasterDrawPixel(GfxRaster *raster,
                                         GfxPoint p,
                                         GfxColorRgba8 color)
{
    if (!raster || !raster->target) {
        return RoboInvalidArgument;
    }
    if (!GfxRectContainsPoint(raster->clip, p)) {
        return RoboOk;
    }
    if (!NeedsBlend(color, raster->opacity)) {
        return GfxSurfaceSetPixel(raster->target, (uint32_t)p.x,
                                          (uint32_t)p.y, color);
    }
    return GfxSurfaceCompositePixel(raster->target, (uint32_t)p.x,
                                            (uint32_t)p.y, color,
                                            raster->opacity);
}

Result GfxRasterFillRect(GfxRaster *raster,
                                        GfxRect rect,
                                        GfxColorRgba8 color)
{
    GfxRect clipped;
    Result res;

    if (!raster || !raster->target) {
        return RoboInvalidArgument;
    }
    if (!GfxRectIsValid(rect)) {
        return RoboInvalidArgument;
    }
    if (GfxRectIsEmpty(rect) || GfxRectIsEmpty(raster->clip)) {
        return RoboOk;
    }
    res = GfxRectClip(rect, raster->clip, &clipped);
    if (res != RoboOk) {
        return res;
    }
    return FillRectBlended(raster->target, clipped, color, raster->opacity);
}

Result GfxRasterDrawLine(GfxRaster *raster,
                                        GfxPoint start,
                                        GfxPoint end,
                                        GfxColorRgba8 color)
{
    int composite;

    if (!raster || !raster->target) {
        return RoboInvalidArgument;
    }
    composite = NeedsBlend(color, raster->opacity) ? 1 : 0;
    return GfxSurfaceDrawLineClipped(raster->target, start, end, color,
                                              raster->clip, raster->opacity,
                                              composite);
}

Result GfxRasterDrawImage(GfxRaster *raster,
                                         GfxImage *image,
                                         GfxRect source,
                                         GfxRect destination)
{
    GfxSurface *pixels;
    GfxRect visible;
    int64_t imageWidth;
    int64_t imageHeight;
    int64_t right;
    int64_t bottom;
    int32_t x;
    int32_t y;

    if (!raster || !raster->target || !image) {
        return RoboInvalidArgument;
    }
    if (!GfxRectIsValid(source) || !GfxRectIsValid(destination)) {
        return RoboInvalidArgument;
    }
    pixels = GfxImageSurface(image);
    if (!pixels) {
        /*
         * A metadata-only image (`EMPTY`/`EXTERNAL`) has nothing to sample. It is a
         * *resource* this call cannot read rather than a caller mistake, which is the
         * distinction `RoboNotSupported` exists for -- the same answer
         * `GfxRasterDrawGlyph` gives a render mode it does not implement.
         */
        return RoboNotSupported;
    }
    imageWidth = (int64_t)GfxImageWidth(image);
    imageHeight = (int64_t)GfxImageHeight(image);
    if (imageWidth <= 0 || imageHeight <= 0) {
        return RoboNotSupported;
    }
    if (source.x < 0 || source.y < 0 ||
        (int64_t)source.x + (int64_t)source.width > imageWidth ||
        (int64_t)source.y + (int64_t)source.height > imageHeight) {
        /* Reading pixels that do not exist is refused rather than clamped, for the reason
         * `GfxSurfaceCopyRect` refuses it: a clamped source silently draws a
         * *different* picture from the one the caller asked for. */
        return RoboInvalidArgument;
    }
    /* The no-ops: nothing to sample, nowhere to put it, or a clip that admits nothing. */
    if (GfxRectIsEmpty(source) || GfxRectIsEmpty(destination)) {
        return RoboOk;
    }
    if (GfxRectIsEmpty(raster->clip)) {
        return RoboOk;
    }
    if (GfxRectIntersect(destination, raster->clip, &visible) != RoboOk) {
        return RoboInvalidArgument;
    }
    if (GfxRectIsEmpty(visible)) {
        return RoboOk;
    }

    right = (int64_t)visible.x + (int64_t)visible.width;
    bottom = (int64_t)visible.y + (int64_t)visible.height;
    for (y = visible.y; (int64_t)y < bottom; y++) {
        int64_t sy = (int64_t)source.y +
                     ((int64_t)(y - destination.y) * (int64_t)source.height) /
                         (int64_t)destination.height;

        for (x = visible.x; (int64_t)x < right; x++) {
            int64_t sx = (int64_t)source.x +
                         ((int64_t)(x - destination.x) * (int64_t)source.width) /
                             (int64_t)destination.width;
            GfxColorRgba8 color;

            if (GfxSurfaceGetPixel(pixels, (uint32_t)sx, (uint32_t)sy, &color) !=
                RoboOk) {
                /* Unreachable: the source rectangle was checked against the image's own
                 * dimensions above, and the mapping cannot leave it. Skipped rather than
                 * reported so that a future format this module learns to read cannot fail a
                 * whole draw over one pixel. */
                continue;
            }
            if (!NeedsBlend(color, raster->opacity)) {
                (void)GfxSurfaceSetPixel(raster->target, (uint32_t)x, (uint32_t)y,
                                                color);
            } else {
                (void)GfxSurfaceCompositePixel(raster->target, (uint32_t)x,
                                                       (uint32_t)y, color,
                                                       raster->opacity);
            }
        }
    }
    return RoboOk;
}

Result GfxRasterFillPath(GfxRaster *raster,
                                        const GfxPath *path,
                                        GfxColorRgba8 color)
{
    (void)color;
    if (!raster || !raster->target || !path) {
        return RoboInvalidArgument;
    }
    return RoboNotSupported;
}

Result GfxRasterStrokePath(GfxRaster *raster,
                                          const GfxPath *path,
                                          GfxColorRgba8 color)
{
    (void)color;
    if (!raster || !raster->target || !path) {
        return RoboInvalidArgument;
    }
    return RoboNotSupported;
}
