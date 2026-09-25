#include "RoboDisplay/RoboQemuFramebuffer.h"

#include "RoboBase/RoboLog.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

void QemuFbInfoInit(QemuFbInfo *info)
{
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
}

GfxPixelFormat QemuFbGfxFormat(uint32_t pixfmt)
{
    if (pixfmt == RoboQemuPixBgrx) {
        return RoboGfxFormatBgra8;
    }
    if (pixfmt == RoboQemuPixRgbx) {
        return RoboGfxFormatRgba8;
    }
    return RoboGfxFormatUnknown;
}

Result QemuFbPixfmtFromGfx(GfxPixelFormat format,
                                           uint32_t *outPixfmt)
{
    if (!outPixfmt) {
        return RoboInvalidArgument;
    }
    if (format == RoboGfxFormatBgra8) {
        *outPixfmt = RoboQemuPixBgrx;
        return RoboOk;
    }
    if (format == RoboGfxFormatRgba8) {
        *outPixfmt = RoboQemuPixRgbx;
        return RoboOk;
    }
    return RoboNotSupported;
}

Result QemuFbInfoValidate(const QemuFbInfo *info)
{
    uint64_t need;
    uint32_t bppBytes;

    if (!info) {
        return RoboInvalidArgument;
    }
    if (info->headless) {
        return RoboOk;
    }
    if (!info->ready) {
        return RoboNotFound;
    }
    if (info->width == 0u || info->height == 0u) {
        return RoboInvalidArgument;
    }
    if (info->width > 8192u || info->height > 8192u) {
        return RoboInvalidArgument;
    }
    if (info->bpp != 32u) {
        LogWarn("robo_qemu_fb: unsupported bpp=%u", (unsigned)info->bpp);
        return RoboNotSupported;
    }
    if (info->pixfmt != RoboQemuPixBgrx &&
        info->pixfmt != RoboQemuPixRgbx) {
        return RoboNotSupported;
    }
    bppBytes = info->bpp / 8u;
    if (info->pitchBytes < info->width * bppBytes) {
        return RoboInvalidArgument;
    }
    need = (uint64_t)info->pitchBytes * (uint64_t)info->height;
    if (need == 0u || need > (uint64_t)SIZE_MAX) {
        return RoboInvalidArgument;
    }
    if (info->pixels) {
        if (info->byteSize < (size_t)need) {
            return RoboInvalidArgument;
        }
    }
    return RoboOk;
}

static uint32_t PackBgrx(uint8_t r, uint8_t g, uint8_t b)
{
    /* Memory B,G,R,X on LE as uint32 0xXXRRGGBB */
    return ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16) |
           (0xFFu << 24);
}

static uint32_t PackRgbx(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r) | ((uint32_t)g << 8) | ((uint32_t)b << 16) |
           (0xFFu << 24);
}

static uint32_t ConvertPixel(GfxPixelFormat srcFmt, const uint8_t *p,
                              uint32_t dstPixfmt)
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    if (srcFmt == RoboGfxFormatBgra8) {
        b = p[0];
        g = p[1];
        r = p[2];
    } else if (srcFmt == RoboGfxFormatRgba8) {
        r = p[0];
        g = p[1];
        b = p[2];
    } else if (srcFmt == RoboGfxFormatRgb8) {
        r = p[0];
        g = p[1];
        b = p[2];
    } else {
        return 0u;
    }
    if (dstPixfmt == RoboQemuPixRgbx) {
        return PackRgbx(r, g, b);
    }
    return PackBgrx(r, g, b);
}

Result QemuFbPresentSurface(const QemuFbInfo *info,
                                           const GfxSurface *surface,
                                           int32_t destX,
                                           int32_t destY)
{
    Result r;
    const uint8_t *src;
    uint8_t *dstBase;
    uint32_t sw;
    uint32_t sh;
    size_t srcStride;
    GfxPixelFormat sfmt;
    uint8_t sbpp;
    uint32_t srcX = 0u;
    uint32_t srcY = 0u;
    uint32_t copyW;
    uint32_t copyH;
    uint32_t y;
    uint32_t dstStridePx;

    r = QemuFbInfoValidate(info);
    if (r != RoboOk) {
        return r;
    }
    if (!info->pixels || info->headless) {
        return RoboNotSupported;
    }
    if (!surface) {
        return RoboInvalidArgument;
    }

    sw = GfxSurfaceWidth(surface);
    sh = GfxSurfaceHeight(surface);
    sfmt = GfxSurfaceFormat(surface);
    srcStride = GfxSurfaceStride(surface);
    src = GfxSurfacePixelsConst(surface);
    if (!src || sw == 0u || sh == 0u) {
        return RoboInvalidArgument;
    }
    if (sfmt != RoboGfxFormatBgra8 && sfmt != RoboGfxFormatRgba8 &&
        sfmt != RoboGfxFormatRgb8) {
        return RoboNotSupported;
    }
    if (GfxPixelFormatBytesPerPixel(sfmt, &sbpp) != RoboOk ||
        sbpp == 0u) {
        return RoboNotSupported;
    }
    if (srcStride < (size_t)sw * (size_t)sbpp) {
        return RoboInvalidArgument;
    }

    if (destX < 0) {
        srcX = (uint32_t)(-destX);
        destX = 0;
    }
    if (destY < 0) {
        srcY = (uint32_t)(-destY);
        destY = 0;
    }
    if (srcX >= sw || srcY >= sh) {
        return RoboOk;
    }
    if ((uint32_t)destX >= info->width || (uint32_t)destY >= info->height) {
        return RoboOk;
    }
    copyW = sw - srcX;
    copyH = sh - srcY;
    if ((uint32_t)destX + copyW > info->width) {
        copyW = info->width - (uint32_t)destX;
    }
    if ((uint32_t)destY + copyH > info->height) {
        copyH = info->height - (uint32_t)destY;
    }

    dstBase = (uint8_t *)info->pixels;
    dstStridePx = info->pitchBytes / 4u;
    if (dstStridePx == 0u) {
        return RoboInvalidArgument;
    }

    for (y = 0u; y < copyH; y++) {
        const uint8_t *row =
            src + (size_t)(srcY + y) * srcStride + (size_t)srcX * sbpp;
        uint32_t *dst =
            (uint32_t *)(dstBase +
                         (size_t)((uint32_t)destY + y) * info->pitchBytes) +
            (uint32_t)destX;
        uint32_t x;
        for (x = 0u; x < copyW; x++) {
            dst[x] = ConvertPixel(sfmt, row + (size_t)x * sbpp, info->pixfmt);
        }
    }
    LogDebug("robo_qemu_fb: present %ux%u → (%d,%d) copy=%ux%u",
                   (unsigned)sw, (unsigned)sh, (int)destX, (int)destY,
                   (unsigned)copyW, (unsigned)copyH);
    return RoboOk;
}

Result QemuFbPresentSurfaceRegion(const QemuFbInfo *info,
                                                 const GfxSurface *surface,
                                                 GfxRect srcRect)
{
    Result r;
    const uint8_t *src;
    uint8_t *dstBase;
    uint32_t sw;
    uint32_t sh;
    size_t srcStride;
    GfxPixelFormat sfmt;
    uint8_t sbpp;
    GfxRect clipped;
    uint32_t copyW;
    uint32_t copyH;
    uint32_t y;
    uint32_t dstStridePx;
    int32_t destX;
    int32_t destY;

    r = QemuFbInfoValidate(info);
    if (r != RoboOk) {
        return r;
    }
    if (!info->pixels || info->headless) {
        return RoboNotSupported;
    }
    if (!surface) {
        return RoboInvalidArgument;
    }
    if (srcRect.width <= 0 || srcRect.height <= 0) {
        return RoboOk;
    }

    sw = GfxSurfaceWidth(surface);
    sh = GfxSurfaceHeight(surface);
    sfmt = GfxSurfaceFormat(surface);
    srcStride = GfxSurfaceStride(surface);
    src = GfxSurfacePixelsConst(surface);
    if (!src || sw == 0u || sh == 0u) {
        return RoboInvalidArgument;
    }
    if (sfmt != RoboGfxFormatBgra8 && sfmt != RoboGfxFormatRgba8 &&
        sfmt != RoboGfxFormatRgb8) {
        return RoboNotSupported;
    }
    if (GfxPixelFormatBytesPerPixel(sfmt, &sbpp) != RoboOk || sbpp == 0u) {
        return RoboNotSupported;
    }
    clipped = GfxRectEmpty();
    if (GfxRectClip(srcRect, MakeGfxRect(0, 0, (int32_t)sw, (int32_t)sh),
                           &clipped) != RoboOk ||
        clipped.width <= 0 || clipped.height <= 0) {
        return RoboOk;
    }

    destX = clipped.x;
    destY = clipped.y;
    copyW = (uint32_t)clipped.width;
    copyH = (uint32_t)clipped.height;
    if ((uint32_t)destX >= info->width || (uint32_t)destY >= info->height) {
        return RoboOk;
    }
    if ((uint32_t)destX + copyW > info->width) {
        copyW = info->width - (uint32_t)destX;
    }
    if ((uint32_t)destY + copyH > info->height) {
        copyH = info->height - (uint32_t)destY;
    }

    dstBase = (uint8_t *)info->pixels;
    dstStridePx = info->pitchBytes / 4u;
    if (dstStridePx == 0u) {
        return RoboInvalidArgument;
    }

    for (y = 0u; y < copyH; y++) {
        const uint8_t *row = src + (size_t)(clipped.y + (int32_t)y) * srcStride +
                             (size_t)clipped.x * sbpp;
        uint32_t *dst =
            (uint32_t *)(dstBase +
                         (size_t)((uint32_t)destY + y) * info->pitchBytes) +
            (uint32_t)destX;
        uint32_t x;
        for (x = 0u; x < copyW; x++) {
            dst[x] = ConvertPixel(sfmt, row + (size_t)x * sbpp, info->pixfmt);
        }
    }
    return RoboOk;
}

Result QemuFbGetPixel(const QemuFbInfo *info,
                                     uint32_t x,
                                     uint32_t y,
                                     uint32_t *outPx)
{
    const uint8_t *row;
    Result r;

    if (!outPx) {
        return RoboInvalidArgument;
    }
    r = QemuFbInfoValidate(info);
    if (r != RoboOk) {
        return r;
    }
    if (!info->pixels || x >= info->width || y >= info->height) {
        return RoboInvalidArgument;
    }
    row = (const uint8_t *)info->pixels + (size_t)y * info->pitchBytes;
    *outPx = ((const uint32_t *)row)[x];
    return RoboOk;
}
