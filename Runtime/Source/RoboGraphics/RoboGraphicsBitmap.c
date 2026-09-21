#include "RoboGraphics/RoboGraphicsBitmap.h"

#include "RoboGraphics/RoboGraphicsPixelFormat.h"

#include <stdint.h>
#include <string.h>

#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif

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

Result GfxSurfacePixelBounds(const GfxSurface *surface,
                                            GfxRect *outBounds)
{
    uint32_t w;
    uint32_t h;

    if (!surface || !outBounds) {
        return RoboInvalidArgument;
    }
    w = GfxSurfaceWidth(surface);
    h = GfxSurfaceHeight(surface);
    if (w > (uint32_t)INT32_MAX || h > (uint32_t)INT32_MAX) {
        return RoboInvalidArgument;
    }
    *outBounds = MakeGfxRect(0, 0, (int32_t)w, (int32_t)h);
    return RoboOk;
}

static int RectInsideSurface(GfxRect rect,
                               const GfxSurface *surface)
{
    GfxRect bounds;

    if (GfxSurfacePixelBounds(surface, &bounds) != RoboOk) {
        return 0;
    }
    return GfxRectEncloses(bounds, rect);
}

static Result PackColor(GfxPixelFormat format,
                                GfxColorRgba8 color,
                                uint8_t *packed,
                                size_t packedCap,
                                uint8_t *outBpp)
{
    uint8_t bpp = 0u;
    Result res;

    res = GfxPixelFormatBytesPerPixel(format, &bpp);
    if (res != RoboOk) {
        return res;
    }
    if (bpp == 0u || (size_t)bpp > packedCap) {
        return RoboNotSupported;
    }
    res = GfxPixelPack(format, color, packed, packedCap);
    if (res != RoboOk) {
        return res;
    }
    *outBpp = bpp;
    return RoboOk;
}

static int PackedBytesUniform(const uint8_t *packed, uint8_t bpp)
{
    uint8_t i;
    for (i = 1u; i < bpp; i++) {
        if (packed[i] != packed[0]) {
            return 0;
        }
    }
    return 1;
}

static void FillRowPattern(uint8_t *row,
                             uint32_t width,
                             const uint8_t *packed,
                             uint8_t bpp)
{
    uint32_t x;
    if (bpp == 1u) {
        memset(row, (int)packed[0], (size_t)width);
        return;
    }
    if (PackedBytesUniform(packed, bpp)) {
        memset(row, (int)packed[0], (size_t)width * (size_t)bpp);
        return;
    }
    for (x = 0u; x < width; x++) {
        memcpy(row + (size_t)x * (size_t)bpp, packed, bpp);
    }
}

Result GfxSurfaceGetPixel(const GfxSurface *surface,
                                         uint32_t x,
                                         uint32_t y,
                                         GfxColorRgba8 *outColor)
{
    const uint8_t *px = NULL;
    uint8_t bpp;
    Result res;

    if (!outColor) {
        return RoboInvalidArgument;
    }
    memset(outColor, 0, sizeof(*outColor));
    if (!surface) {
        return RoboInvalidArgument;
    }
    res = GfxSurfacePixelConst(surface, x, y, &px);
    if (res != RoboOk) {
        return res;
    }
    bpp = GfxSurfaceBytesPerPixel(surface);
    return GfxPixelUnpack(GfxSurfaceFormat(surface), px, bpp,
                                 outColor);
}

Result GfxSurfaceSetPixel(GfxSurface *surface,
                                         uint32_t x,
                                         uint32_t y,
                                         GfxColorRgba8 color)
{
    uint8_t *px = NULL;
    uint8_t packed[8];
    uint8_t bpp = 0u;
    Result res;

    if (!surface || !SurfaceCanWrite(surface)) {
        return RoboInvalidArgument;
    }
    res = PackColor(GfxSurfaceFormat(surface), color, packed, sizeof(packed),
                     &bpp);
    if (res != RoboOk) {
        return res;
    }
    res = GfxSurfacePixel(surface, x, y, &px);
    if (res != RoboOk) {
        return res;
    }
    memcpy(px, packed, bpp);
    return RoboOk;
}

Result GfxSurfaceGetPixelAt(const GfxSurface *surface,
                                            GfxPoint p,
                                            GfxColorRgba8 *outColor)
{
    if (p.x < 0 || p.y < 0) {
        return RoboInvalidArgument;
    }
    return GfxSurfaceGetPixel(surface, (uint32_t)p.x, (uint32_t)p.y,
                                      outColor);
}

Result GfxSurfaceSetPixelAt(GfxSurface *surface,
                                            GfxPoint p,
                                            GfxColorRgba8 color)
{
    if (p.x < 0 || p.y < 0) {
        return RoboInvalidArgument;
    }
    return GfxSurfaceSetPixel(surface, (uint32_t)p.x, (uint32_t)p.y,
                                      color);
}

Result GfxSurfaceFillRect(GfxSurface *surface,
                                         GfxRect rect,
                                         GfxColorRgba8 color)
{
    uint8_t packed[8];
    uint8_t bpp = 0u;
    uint32_t y;
    uint32_t x0;
    uint32_t y0;
    uint32_t w;
    uint32_t h;
    uint8_t *row = NULL;
    Result res;

    if (!surface || !SurfaceCanWrite(surface)) {
        return RoboInvalidArgument;
    }
    if (!GfxRectIsValid(rect)) {
        return RoboInvalidArgument;
    }
    if (GfxRectIsEmpty(rect)) {
        return RoboOk;
    }
    if (!RectInsideSurface(rect, surface)) {
        return RoboInvalidArgument;
    }
    res = PackColor(GfxSurfaceFormat(surface), color, packed, sizeof(packed),
                     &bpp);
    if (res != RoboOk) {
        return res;
    }

    x0 = (uint32_t)rect.x;
    y0 = (uint32_t)rect.y;
    w = (uint32_t)rect.width;
    h = (uint32_t)rect.height;
    for (y = 0u; y < h; y++) {
        res = GfxSurfaceRowChecked(surface, y0 + y, &row);
        if (res != RoboOk) {
            return res;
        }
        FillRowPattern(row + (size_t)x0 * (size_t)bpp, w, packed, bpp);
    }
    return RoboOk;
}

Result GfxSurfaceClear(GfxSurface *surface,
                                     GfxColorRgba8 color)
{
    GfxRect full;
    Result res;

    if (!surface) {
        return RoboInvalidArgument;
    }
    res = GfxSurfacePixelBounds(surface, &full);
    if (res != RoboOk) {
        return res;
    }
    return GfxSurfaceFillRect(surface, full, color);
}

static Result CopyRectSameFormat(const GfxSurface *src,
                                           uint32_t sx,
                                           uint32_t sy,
                                           uint32_t w,
                                           uint32_t h,
                                           GfxSurface *dst,
                                           uint32_t dx,
                                           uint32_t dy)
{
    uint8_t bpp = GfxSurfaceBytesPerPixel(src);
    size_t rowBytes = (size_t)w * (size_t)bpp;
    const uint8_t *srcBase = GfxSurfacePixelsConst(src);
    uint8_t *dstBase = GfxSurfacePixels(dst);
    size_t srcStride = GfxSurfaceStride(src);
    size_t dstStride = GfxSurfaceStride(dst);
    int sameStorage;
    uint32_t y;

    if (!srcBase || !dstBase || bpp == 0u) {
        return RoboInvalidArgument;
    }
    if (w > (SIZE_MAX / (size_t)bpp)) {
        return RoboInvalidArgument;
    }

    sameStorage = (srcBase == (const uint8_t *)dstBase);

    if (sameStorage && sx == dx && sy == dy) {
        return RoboOk;
    }

    if (!sameStorage) {
        for (y = 0u; y < h; y++) {
            memcpy(dstBase + (size_t)(dy + y) * dstStride + (size_t)dx * bpp,
                   srcBase + (size_t)(sy + y) * srcStride + (size_t)sx * bpp,
                   rowBytes);
        }
        return RoboOk;
    }

    /* Overlapping same-surface copy (direction-aware row copies / memmove). */
    if (dy > sy) {
        for (y = h; y > 0u; y--) {
            uint32_t yi = y - 1u;
            memmove(dstBase + (size_t)(dy + yi) * dstStride + (size_t)dx * bpp,
                    srcBase + (size_t)(sy + yi) * srcStride + (size_t)sx * bpp,
                    rowBytes);
        }
    } else {
        for (y = 0u; y < h; y++) {
            memmove(dstBase + (size_t)(dy + y) * dstStride + (size_t)dx * bpp,
                    srcBase + (size_t)(sy + y) * srcStride + (size_t)sx * bpp,
                    rowBytes);
        }
    }
    return RoboOk;
}

static Result CopyRectConvert(const GfxSurface *src,
                                       uint32_t sx,
                                       uint32_t sy,
                                       uint32_t w,
                                       uint32_t h,
                                       GfxSurface *dst,
                                       uint32_t dx,
                                       uint32_t dy)
{
    uint32_t y;
    uint32_t x;
    const uint8_t *sp;
    uint8_t *dp;
    uint8_t sbpp = GfxSurfaceBytesPerPixel(src);
    uint8_t dbpp = GfxSurfaceBytesPerPixel(dst);
    uint8_t tmp[8];
    Result res;

    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x++) {
            res = GfxSurfacePixelConst(src, sx + x, sy + y, &sp);
            if (res != RoboOk) {
                return res;
            }
            res = GfxPixelConvert(GfxSurfaceFormat(src), sp, sbpp,
                                         GfxSurfaceFormat(dst), tmp,
                                         sizeof(tmp));
            if (res != RoboOk) {
                return res;
            }
            res = GfxSurfacePixel(dst, dx + x, dy + y, &dp);
            if (res != RoboOk) {
                return res;
            }
            memcpy(dp, tmp, dbpp);
        }
    }
    return RoboOk;
}

Result GfxSurfaceCopyRect(const GfxSurface *src,
                                         GfxRect srcRect,
                                         GfxSurface *dst,
                                         GfxPoint dstOrigin)
{
    uint32_t w;
    uint32_t h;
    uint32_t sx;
    uint32_t sy;
    uint32_t dx;
    uint32_t dy;
    GfxRect dstRect;

    if (!src || !dst || !SurfaceCanWrite(dst)) {
        return RoboInvalidArgument;
    }
    if (!GfxRectIsValid(srcRect)) {
        return RoboInvalidArgument;
    }
    if (GfxRectIsEmpty(srcRect)) {
        return RoboOk;
    }
    if (!RectInsideSurface(srcRect, src)) {
        return RoboInvalidArgument;
    }

    w = (uint32_t)srcRect.width;
    h = (uint32_t)srcRect.height;
    sx = (uint32_t)srcRect.x;
    sy = (uint32_t)srcRect.y;

    if (dstOrigin.x < 0 || dstOrigin.y < 0) {
        return RoboInvalidArgument;
    }
    dstRect = MakeGfxRect(dstOrigin.x, dstOrigin.y, (int32_t)w, (int32_t)h);
    if (!RectInsideSurface(dstRect, dst)) {
        return RoboInvalidArgument;
    }
    dx = (uint32_t)dstOrigin.x;
    dy = (uint32_t)dstOrigin.y;

    if (GfxSurfaceFormat(src) == GfxSurfaceFormat(dst)) {
        return CopyRectSameFormat(src, sx, sy, w, h, dst, dx, dy);
    }
    return CopyRectConvert(src, sx, sy, w, h, dst, dx, dy);
}

Result GfxSurfaceCopy(const GfxSurface *src,
                                    GfxSurface *dst)
{
    GfxRect full;
    Result res;

    if (!src || !dst) {
        return RoboInvalidArgument;
    }
    if (GfxSurfaceWidth(src) != GfxSurfaceWidth(dst) ||
        GfxSurfaceHeight(src) != GfxSurfaceHeight(dst)) {
        return RoboInvalidArgument;
    }
    res = GfxSurfacePixelBounds(src, &full);
    if (res != RoboOk) {
        return res;
    }
    return GfxSurfaceCopyRect(src, full, dst, MakeGfxPoint(0, 0));
}
