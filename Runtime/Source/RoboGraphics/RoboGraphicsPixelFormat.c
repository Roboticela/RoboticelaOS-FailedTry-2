#include "RoboGraphics/RoboGraphicsPixelFormat.h"

#include <stdint.h>

Result GetGfxPixelFormatInfo(GfxPixelFormat format,
                                         GfxPixelFormatInfo *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->format = format;
    out->bytesPerPixel = 0u;
    out->channelCount = 0u;
    out->channelBits = 0u;
    out->hasAlpha = 0u;
    out->alphaStored = 0u;
    out->isGrayscale = 0u;
    out->isFloat = 0u;
    out->isNormalizedInt = 0u;
    out->isByteAddressable = 0u;
    out->isImplemented = 0u;
    out->order = RoboGfxChannelsUnknown;
    out->name = "unknown";

    switch (format) {
    case RoboGfxFormatRgba8:
        out->bytesPerPixel = 4u;
        out->channelCount = 4u;
        out->channelBits = 8u;
        out->hasAlpha = 1u;
        out->alphaStored = 1u;
        out->isNormalizedInt = 1u;
        out->isByteAddressable = 1u;
        out->isImplemented = 1u;
        out->order = RoboGfxChannelsRgba;
        out->name = "RGBA8";
        return RoboOk;
    case RoboGfxFormatBgra8:
        out->bytesPerPixel = 4u;
        out->channelCount = 4u;
        out->channelBits = 8u;
        out->hasAlpha = 1u;
        out->alphaStored = 1u;
        out->isNormalizedInt = 1u;
        out->isByteAddressable = 1u;
        out->isImplemented = 1u;
        out->order = RoboGfxChannelsBgra;
        out->name = "BGRA8";
        return RoboOk;
    case RoboGfxFormatRgb8:
        out->bytesPerPixel = 3u;
        out->channelCount = 3u;
        out->channelBits = 8u;
        out->hasAlpha = 0u;
        out->alphaStored = 0u;
        out->isNormalizedInt = 1u;
        out->isByteAddressable = 1u;
        out->isImplemented = 1u;
        out->order = RoboGfxChannelsRgb;
        out->name = "RGB8";
        return RoboOk;
    case RoboGfxFormatGray8:
        out->bytesPerPixel = 1u;
        out->channelCount = 1u;
        out->channelBits = 8u;
        out->hasAlpha = 0u;
        out->alphaStored = 0u;
        out->isGrayscale = 1u;
        out->isNormalizedInt = 1u;
        out->isByteAddressable = 1u;
        out->isImplemented = 1u;
        out->order = RoboGfxChannelsGray;
        out->name = "GRAY8";
        return RoboOk;
    case RoboGfxFormatAlpha8:
        out->bytesPerPixel = 1u;
        out->channelCount = 1u;
        out->channelBits = 8u;
        out->hasAlpha = 1u;
        out->alphaStored = 1u;
        out->isNormalizedInt = 1u;
        out->isByteAddressable = 1u;
        out->isImplemented = 1u;
        out->order = RoboGfxChannelsAlpha;
        out->name = "ALPHA8";
        return RoboOk;
    case RoboGfxFormatRgba16:
        out->bytesPerPixel = 8u;
        out->channelCount = 4u;
        out->channelBits = 16u;
        out->hasAlpha = 1u;
        out->alphaStored = 1u;
        out->isNormalizedInt = 1u;
        out->isByteAddressable = 1u;
        out->isImplemented = 0u;
        out->order = RoboGfxChannelsRgba;
        out->name = "RGBA16";
        return RoboOk;
    case RoboGfxFormatRgbaF32:
        out->bytesPerPixel = 16u;
        out->channelCount = 4u;
        out->channelBits = 32u;
        out->hasAlpha = 1u;
        out->alphaStored = 1u;
        out->isFloat = 1u;
        out->isNormalizedInt = 0u;
        out->isByteAddressable = 1u;
        out->isImplemented = 0u;
        out->order = RoboGfxChannelsRgba;
        out->name = "RGBA_F32";
        return RoboOk;
    case RoboGfxFormatUnknown:
    default:
        return RoboInvalidArgument;
    }
}

int GfxPixelFormatIsKnown(GfxPixelFormat format)
{
    GfxPixelFormatInfo info;
    return GetGfxPixelFormatInfo(format, &info) == RoboOk;
}

int GfxPixelFormatIsImplemented(GfxPixelFormat format)
{
    GfxPixelFormatInfo info;
    if (GetGfxPixelFormatInfo(format, &info) != RoboOk) {
        return 0;
    }
    return info.isImplemented != 0u;
}

Result GfxPixelFormatBytesPerPixel(GfxPixelFormat format,
                                                    uint8_t *outBpp)
{
    GfxPixelFormatInfo info;
    Result res;

    if (!outBpp) {
        return RoboInvalidArgument;
    }
    res = GetGfxPixelFormatInfo(format, &info);
    if (res != RoboOk) {
        return res;
    }
    if (info.bytesPerPixel == 0u) {
        return RoboNotSupported;
    }
    *outBpp = info.bytesPerPixel;
    return RoboOk;
}

Result GfxPixelFormatMinStride(GfxPixelFormat format,
                                               uint32_t width,
                                               size_t *outStride)
{
    uint8_t bpp = 0u;
    Result res;

    if (!outStride || width == 0u) {
        return RoboInvalidArgument;
    }
    res = GfxPixelFormatBytesPerPixel(format, &bpp);
    if (res != RoboOk) {
        return res;
    }
    if (width > (SIZE_MAX / (size_t)bpp)) {
        return RoboInvalidArgument;
    }
    *outStride = (size_t)width * (size_t)bpp;
    return RoboOk;
}

Result GfxPixelFormatRowBytes(GfxPixelFormat format,
                                              uint32_t width,
                                              size_t *outRowBytes)
{
    return GfxPixelFormatMinStride(format, width, outRowBytes);
}

Result GfxPixelFormatValidateStride(GfxPixelFormat format,
                                                    uint32_t width,
                                                    size_t strideBytes)
{
    size_t minStride = 0u;
    Result res;

    res = GfxPixelFormatMinStride(format, width, &minStride);
    if (res != RoboOk) {
        return res;
    }
    if (strideBytes < minStride) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

Result GfxPixelFormatBufferBytes(GfxPixelFormat format,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 size_t strideBytes,
                                                 size_t *outBytes)
{
    size_t stride = 0u;
    Result res;

    if (!outBytes || height == 0u) {
        return RoboInvalidArgument;
    }
    if (strideBytes == 0u) {
        res = GfxPixelFormatMinStride(format, width, &stride);
        if (res != RoboOk) {
            return res;
        }
    } else {
        res = GfxPixelFormatValidateStride(format, width, strideBytes);
        if (res != RoboOk) {
            return res;
        }
        stride = strideBytes;
    }
    if (height > (SIZE_MAX / stride)) {
        return RoboInvalidArgument;
    }
    *outBytes = stride * (size_t)height;
    return RoboOk;
}

Result GfxPixelFormatPixelOffset(GfxPixelFormat format,
                                                 size_t strideBytes,
                                                 uint32_t x,
                                                 uint32_t y,
                                                 size_t *outOffset)
{
    uint8_t bpp = 0u;
    size_t rowOff;
    size_t pixOff;
    Result res;

    if (!outOffset || strideBytes == 0u) {
        return RoboInvalidArgument;
    }
    res = GfxPixelFormatBytesPerPixel(format, &bpp);
    if (res != RoboOk) {
        return res;
    }
    if ((size_t)bpp > strideBytes) {
        return RoboInvalidArgument;
    }
    /* Pixel must fit in the row: x*bpp + bpp <= stride */
    if (x > ((strideBytes - (size_t)bpp) / (size_t)bpp)) {
        return RoboInvalidArgument;
    }
    if (y > 0u && y > (SIZE_MAX / strideBytes)) {
        return RoboInvalidArgument;
    }
    rowOff = (size_t)y * strideBytes;
    pixOff = (size_t)x * (size_t)bpp;
    if (pixOff > SIZE_MAX - rowOff) {
        return RoboInvalidArgument;
    }
    *outOffset = rowOff + pixOff;
    return RoboOk;
}

Result GfxPixelPack(GfxPixelFormat format,
                                  GfxColorRgba8 color,
                                  uint8_t *dst,
                                  size_t dstCap)
{
    GfxPixelFormatInfo info;

    if (!dst) {
        return RoboInvalidArgument;
    }
    if (GetGfxPixelFormatInfo(format, &info) != RoboOk) {
        return RoboInvalidArgument;
    }
    if (!info.isImplemented) {
        return RoboNotSupported;
    }
    if (dstCap < info.bytesPerPixel) {
        return RoboInvalidArgument;
    }

    switch (format) {
    case RoboGfxFormatRgba8:
        dst[0] = color.r;
        dst[1] = color.g;
        dst[2] = color.b;
        dst[3] = color.a;
        return RoboOk;
    case RoboGfxFormatBgra8:
        dst[0] = color.b;
        dst[1] = color.g;
        dst[2] = color.r;
        dst[3] = color.a;
        return RoboOk;
    case RoboGfxFormatRgb8:
        dst[0] = color.r;
        dst[1] = color.g;
        dst[2] = color.b;
        return RoboOk;
    case RoboGfxFormatGray8:
        dst[0] = GfxColorRgba8Luminance8(color);
        return RoboOk;
    case RoboGfxFormatAlpha8:
        dst[0] = color.a;
        return RoboOk;
    default:
        return RoboNotSupported;
    }
}

Result GfxPixelUnpack(GfxPixelFormat format,
                                    const uint8_t *src,
                                    size_t srcLen,
                                    GfxColorRgba8 *outColor)
{
    GfxPixelFormatInfo info;

    if (!src || !outColor) {
        return RoboInvalidArgument;
    }
    if (GetGfxPixelFormatInfo(format, &info) != RoboOk) {
        return RoboInvalidArgument;
    }
    if (!info.isImplemented) {
        return RoboNotSupported;
    }
    if (srcLen < info.bytesPerPixel) {
        return RoboInvalidArgument;
    }

    switch (format) {
    case RoboGfxFormatRgba8:
        *outColor = GfxRgba8(src[0], src[1], src[2], src[3]);
        return RoboOk;
    case RoboGfxFormatBgra8:
        *outColor = GfxRgba8(src[2], src[1], src[0], src[3]);
        return RoboOk;
    case RoboGfxFormatRgb8:
        *outColor = GfxRgba8(src[0], src[1], src[2], 255u);
        return RoboOk;
    case RoboGfxFormatGray8:
        *outColor = GfxRgba8(src[0], src[0], src[0], 255u);
        return RoboOk;
    case RoboGfxFormatAlpha8:
        *outColor = GfxRgba8(0u, 0u, 0u, src[0]);
        return RoboOk;
    default:
        return RoboNotSupported;
    }
}

Result GfxPixelConvert(GfxPixelFormat srcFormat,
                                     const uint8_t *src,
                                     size_t srcLen,
                                     GfxPixelFormat dstFormat,
                                     uint8_t *dst,
                                     size_t dstCap)
{
    GfxColorRgba8 color;
    Result res;

    res = GfxPixelUnpack(srcFormat, src, srcLen, &color);
    if (res != RoboOk) {
        return res;
    }
    return GfxPixelPack(dstFormat, color, dst, dstCap);
}
