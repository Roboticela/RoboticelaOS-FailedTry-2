#include "RoboGraphics/RoboGraphicsComposite.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

uint8_t GfxMulDiv255(uint8_t a, uint8_t b)
{
    return (uint8_t)(((uint16_t)a * (uint16_t)b + 127u) / 255u);
}

GfxColorRgba8 GfxColorPremultiply(GfxColorRgba8 straight)
{
    GfxColorRgba8 p;

    if (straight.a == 0u) {
        return GfxRgba8(0u, 0u, 0u, 0u);
    }
    if (straight.a == 255u) {
        return straight;
    }
    p.r = GfxMulDiv255(straight.r, straight.a);
    p.g = GfxMulDiv255(straight.g, straight.a);
    p.b = GfxMulDiv255(straight.b, straight.a);
    p.a = straight.a;
    return p;
}

GfxColorRgba8 GfxColorUnpremultiply(GfxColorRgba8 premul)
{
    GfxColorRgba8 s;
    uint16_t a;

    if (premul.a == 0u) {
        return GfxRgba8(0u, 0u, 0u, 0u);
    }
    if (premul.a == 255u) {
        return premul;
    }
    a = (uint16_t)premul.a;
    /* round: (c * 255 + a/2) / a */
    s.r = (uint8_t)(((uint16_t)premul.r * 255u + (a / 2u)) / a);
    s.g = (uint8_t)(((uint16_t)premul.g * 255u + (a / 2u)) / a);
    s.b = (uint8_t)(((uint16_t)premul.b * 255u + (a / 2u)) / a);
    s.a = premul.a;
    return s;
}

GfxColorRgba8 GfxCompositeSrcOver(GfxColorRgba8 src,
                                                   GfxColorRgba8 dst)
{
    return GfxCompositeSrcOverOpacity(src, dst, 255u);
}

GfxColorRgba8 GfxCompositeSrcOverOpacity(
    GfxColorRgba8 src,
    GfxColorRgba8 dst,
    uint8_t opacity)
{
    uint8_t sa;
    uint8_t invSa;
    GfxColorRgba8 sp;
    GfxColorRgba8 dp;
    GfxColorRgba8 outP;
    uint16_t tmp;

    if (opacity == 0u) {
        return dst;
    }

    sa = (opacity == 255u) ? src.a : GfxMulDiv255(src.a, opacity);
    if (sa == 0u) {
        return dst;
    }
    if (sa == 255u) {
        return GfxRgba8(src.r, src.g, src.b, 255u);
    }

    /* Premultiply with effective source alpha (RGB stay straight until here). */
    sp.r = GfxMulDiv255(src.r, sa);
    sp.g = GfxMulDiv255(src.g, sa);
    sp.b = GfxMulDiv255(src.b, sa);
    sp.a = sa;

    dp = GfxColorPremultiply(dst);

    invSa = (uint8_t)(255u - sa);

    /*
     * Premultiplied source-over:
     *   outP = src_p + dst_p * (1 - sa)
     * Keep intermediate precision in uint16 before /255.
     */
    tmp = (uint16_t)sp.a + (uint16_t)GfxMulDiv255(dp.a, invSa);
    outP.a = (tmp > 255u) ? 255u : (uint8_t)tmp;

    tmp = (uint16_t)sp.r + (uint16_t)GfxMulDiv255(dp.r, invSa);
    outP.r = (tmp > 255u) ? 255u : (uint8_t)tmp;

    tmp = (uint16_t)sp.g + (uint16_t)GfxMulDiv255(dp.g, invSa);
    outP.g = (tmp > 255u) ? 255u : (uint8_t)tmp;

    tmp = (uint16_t)sp.b + (uint16_t)GfxMulDiv255(dp.b, invSa);
    outP.b = (tmp > 255u) ? 255u : (uint8_t)tmp;

    return GfxColorUnpremultiply(outP);
}

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

static int SurfaceCanRead(const GfxSurface *surface)
{
    GfxSurfaceAccess a;

    if (!surface) {
        return 0;
    }
    a = MakeGfxSurfaceAccess(surface);
    return a == RoboGfxSurfaceAccessRead ||
           a == RoboGfxSurfaceAccessReadWrite;
}

Result GfxSurfaceCompositePixel(GfxSurface *dst,
                                               uint32_t x,
                                               uint32_t y,
                                               GfxColorRgba8 src,
                                               uint8_t opacity)
{
    GfxColorRgba8 dstC;
    GfxColorRgba8 out;
    Result res;

    if (!dst || !SurfaceCanWrite(dst)) {
        return RoboInvalidArgument;
    }
    if (!GfxPixelFormatIsImplemented(GfxSurfaceFormat(dst))) {
        return RoboNotSupported;
    }
    res = GfxSurfaceGetPixel(dst, x, y, &dstC);
    if (res != RoboOk) {
        return res;
    }
    out = GfxCompositeSrcOverOpacity(src, dstC, opacity);
    return GfxSurfaceSetPixel(dst, x, y, out);
}

/**
 * Clip composite geometry into unsigned in-bounds regions.
 * Returns 0 if nothing to draw; 1 and fills sx,sy,dx,dy,w,h on success.
 */
static int ClipCompositeRegion(const GfxSurface *src,
                                 GfxRect srcRect,
                                 const GfxSurface *dst,
                                 GfxPoint dstOrigin,
                                 uint32_t *outSx,
                                 uint32_t *outSy,
                                 uint32_t *outDx,
                                 uint32_t *outDy,
                                 uint32_t *outW,
                                 uint32_t *outH)
{
    GfxRect srcBounds;
    GfxRect dstBounds;
    GfxRect srcClip;
    GfxRect dstPlace;
    GfxRect dstClip;
    int32_t adjX;
    int32_t adjY;

    if (GfxSurfacePixelBounds(src, &srcBounds) != RoboOk ||
        GfxSurfacePixelBounds(dst, &dstBounds) != RoboOk) {
        return 0;
    }
    if (!GfxRectIsValid(srcRect) || GfxRectIsEmpty(srcRect)) {
        return 0;
    }
    if (GfxRectIntersect(srcRect, srcBounds, &srcClip) != RoboOk ||
        GfxRectIsEmpty(srcClip)) {
        return 0;
    }

    adjX = srcClip.x - srcRect.x;
    adjY = srcClip.y - srcRect.y;
    dstPlace = MakeGfxRect(dstOrigin.x + adjX, dstOrigin.y + adjY,
                              srcClip.width, srcClip.height);
    if (!GfxRectIsValid(dstPlace) ||
        GfxRectIsEmpty(dstPlace)) {
        return 0;
    }
    if (GfxRectIntersect(dstPlace, dstBounds, &dstClip) != RoboOk ||
        GfxRectIsEmpty(dstClip)) {
        return 0;
    }

    *outSx = (uint32_t)(srcClip.x + (dstClip.x - dstPlace.x));
    *outSy = (uint32_t)(srcClip.y + (dstClip.y - dstPlace.y));
    *outDx = (uint32_t)dstClip.x;
    *outDy = (uint32_t)dstClip.y;
    *outW = (uint32_t)dstClip.width;
    *outH = (uint32_t)dstClip.height;
    return 1;
}

static Result CompositeLoop(const GfxSurface *src,
                                    uint32_t sx,
                                    uint32_t sy,
                                    GfxSurface *dst,
                                    uint32_t dx,
                                    uint32_t dy,
                                    uint32_t w,
                                    uint32_t h,
                                    uint8_t opacity,
                                    const GfxColorRgba8 *srcSnapshot)
{
    uint32_t y;
    uint32_t x;
    GfxColorRgba8 sc;
    GfxColorRgba8 dc;
    GfxColorRgba8 out;
    Result res;

    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x++) {
            if (srcSnapshot) {
                sc = srcSnapshot[(size_t)y * (size_t)w + (size_t)x];
            } else {
                res = GfxSurfaceGetPixel(src, sx + x, sy + y, &sc);
                if (res != RoboOk) {
                    return res;
                }
            }
            res = GfxSurfaceGetPixel(dst, dx + x, dy + y, &dc);
            if (res != RoboOk) {
                return res;
            }
            out = GfxCompositeSrcOverOpacity(sc, dc, opacity);
            res = GfxSurfaceSetPixel(dst, dx + x, dy + y, out);
            if (res != RoboOk) {
                return res;
            }
        }
    }
    return RoboOk;
}

Result GfxSurfaceCompositeRect(const GfxSurface *src,
                                              GfxRect srcRect,
                                              GfxSurface *dst,
                                              GfxPoint dstOrigin,
                                              uint8_t opacity)
{
    uint32_t sx, sy, dx, dy, w, h;
    const uint8_t *srcPx;
    uint8_t *dstPx;
    int sameStorage;
    GfxColorRgba8 *snapshot = NULL;
    size_t n;
    size_t i;
    uint32_t y;
    uint32_t x;
    Result res;

    if (!src || !dst || !SurfaceCanWrite(dst) || !SurfaceCanRead(src)) {
        return RoboInvalidArgument;
    }
    if (!GfxPixelFormatIsImplemented(GfxSurfaceFormat(src)) ||
        !GfxPixelFormatIsImplemented(GfxSurfaceFormat(dst))) {
        return RoboNotSupported;
    }
    if (!GfxRectIsValid(srcRect)) {
        return RoboInvalidArgument;
    }
    if (GfxRectIsEmpty(srcRect)) {
        return RoboOk;
    }
    if (!ClipCompositeRegion(src, srcRect, dst, dstOrigin, &sx, &sy, &dx,
                               &dy, &w, &h)) {
        return RoboOk;
    }

    srcPx = GfxSurfacePixelsConst(src);
    dstPx = GfxSurfacePixels(dst);
    sameStorage = (srcPx != NULL && dstPx != NULL &&
                    srcPx == (const uint8_t *)dstPx);

    if (sameStorage) {
        if (w == 0u || h == 0u) {
            return RoboOk;
        }
        if (h > (SIZE_MAX / (size_t)w)) {
            return RoboInvalidArgument;
        }
        n = (size_t)w * (size_t)h;
        snapshot = (GfxColorRgba8 *)RoboCalloc(
            n, sizeof(GfxColorRgba8));
        if (!snapshot) {
            return RoboOutOfMemory;
        }
        i = 0u;
        for (y = 0u; y < h; y++) {
            for (x = 0u; x < w; x++) {
                res = GfxSurfaceGetPixel(src, sx + x, sy + y,
                                                 &snapshot[i]);
                if (res != RoboOk) {
                    RoboFree(snapshot);
                    return res;
                }
                i++;
            }
        }
    }

    res = CompositeLoop(src, sx, sy, dst, dx, dy, w, h, opacity, snapshot);
    RoboFree(snapshot);
    return res;
}

Result GfxSurfaceComposite(const GfxSurface *src,
                                         GfxSurface *dst,
                                         GfxPoint dstOrigin,
                                         uint8_t opacity)
{
    GfxRect full;
    Result res;

    if (!src) {
        return RoboInvalidArgument;
    }
    res = GfxSurfacePixelBounds(src, &full);
    if (res != RoboOk) {
        return res;
    }
    return GfxSurfaceCompositeRect(src, full, dst, dstOrigin, opacity);
}
