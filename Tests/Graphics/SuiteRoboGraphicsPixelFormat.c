/*
 * RoboGraphics pixel-format suite (Goal 6.2).
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <stddef.h>
#include <string.h>

static void TestPfMetadataTable(void)
{
    static const struct {
        GfxPixelFormat format;
        uint8_t bpp;
        uint8_t channels;
        uint8_t alpha;
        uint8_t gray;
        const char *name;
    } kRows[] = {
        {RoboGfxFormatRgba8, 4, 4, 1, 0, "RGBA8"},
        {RoboGfxFormatBgra8, 4, 4, 1, 0, "BGRA8"},
        {RoboGfxFormatRgb8, 3, 3, 0, 0, "RGB8"},
        {RoboGfxFormatGray8, 1, 1, 0, 1, "GRAY8"},
        {RoboGfxFormatAlpha8, 1, 1, 1, 0, "ALPHA8"},
    };
    size_t i;

    for (i = 0u; i < sizeof(kRows) / sizeof(kRows[0]); i++) {
        GfxPixelFormatInfo info;
        uint8_t bpp = 0u;
        RoboCheckEqInt(GetGfxPixelFormatInfo(kRows[i].format, &info), RoboOk);
        RoboCheck(GfxPixelFormatIsKnown(kRows[i].format));
        RoboCheck(GfxPixelFormatIsImplemented(kRows[i].format));
        RoboCheckEqInt(info.bytesPerPixel, kRows[i].bpp);
        RoboCheckEqInt(info.channelCount, kRows[i].channels);
        RoboCheckEqInt(info.hasAlpha, kRows[i].alpha);
        RoboCheckEqInt(info.alphaStored, kRows[i].alpha);
        RoboCheckEqInt(info.isGrayscale, kRows[i].gray);
        RoboCheckEqInt(info.channelBits, 8);
        RoboCheckEqInt(info.isNormalizedInt, 1);
        RoboCheckEqInt(info.isByteAddressable, 1);
        RoboCheckStrEq(info.name, kRows[i].name);
        RoboCheckEqInt(GfxPixelFormatBytesPerPixel(kRows[i].format, &bpp),
                          RoboOk);
        RoboCheckEqInt(bpp, kRows[i].bpp);
    }

    RoboCheck(!GfxPixelFormatIsKnown(RoboGfxFormatUnknown));
    RoboCheck(!GfxPixelFormatIsImplemented(RoboGfxFormatUnknown));
    RoboCheck(GfxPixelFormatIsKnown(RoboGfxFormatRgba16));
    RoboCheck(!GfxPixelFormatIsImplemented(RoboGfxFormatRgba16));
    RoboCheckEqInt(GfxPixelFormatBytesPerPixel(RoboGfxFormatUnknown, NULL),
                      RoboInvalidArgument);
}

static void TestPfRowAndOffset(void)
{
    size_t stride = 0u;
    size_t bytes = 0u;
    size_t off = 0u;

    RoboCheckEqInt(GfxPixelFormatRowBytes(RoboGfxFormatRgba8, 1u, &stride),
                      RoboOk);
    RoboCheckEqInt((int)stride, 4);
    RoboCheckEqInt(GfxPixelFormatRowBytes(RoboGfxFormatRgba8, 2u, &stride),
                      RoboOk);
    RoboCheckEqInt((int)stride, 8);
    RoboCheckEqInt(GfxPixelFormatRowBytes(RoboGfxFormatRgb8, 10u, &stride),
                      RoboOk);
    RoboCheckEqInt((int)stride, 30);

    RoboCheckEqInt(
        GfxPixelFormatValidateStride(RoboGfxFormatRgba8, 4u, 16u), RoboOk);
    RoboCheckEqInt(
        GfxPixelFormatValidateStride(RoboGfxFormatRgba8, 4u, 15u),
        RoboInvalidArgument);

    RoboCheckEqInt(
        GfxPixelFormatBufferBytes(RoboGfxFormatRgba8, 4u, 3u, 0u, &bytes),
        RoboOk);
    RoboCheckEqInt((int)bytes, 48);

    RoboCheckEqInt(
        GfxPixelFormatPixelOffset(RoboGfxFormatRgba8, 16u, 0u, 0u, &off),
        RoboOk);
    RoboCheckEqInt((int)off, 0);
    RoboCheckEqInt(
        GfxPixelFormatPixelOffset(RoboGfxFormatRgba8, 16u, 1u, 0u, &off),
        RoboOk);
    RoboCheckEqInt((int)off, 4);
    RoboCheckEqInt(
        GfxPixelFormatPixelOffset(RoboGfxFormatRgba8, 16u, 0u, 1u, &off),
        RoboOk);
    RoboCheckEqInt((int)off, 16);
    RoboCheckEqInt(
        GfxPixelFormatPixelOffset(RoboGfxFormatRgba8, 16u, 3u, 2u, &off),
        RoboOk);
    RoboCheckEqInt((int)off, 44);
}

static void TestPfOverflow(void)
{
    size_t stride = 0u;
    size_t bytes = 0u;
    size_t off = 0u;

    RoboCheckEqInt(GfxPixelFormatMinStride(RoboGfxFormatRgba8, 0u, &stride),
                      RoboInvalidArgument);
    /* height × stride overflow */
    RoboCheckEqInt(GfxPixelFormatBufferBytes(RoboGfxFormatRgba8, 1u, 2u,
                                                         (SIZE_MAX / 2u) + 1u, &bytes),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxPixelFormatBufferBytes(RoboGfxFormatRgba8, 4u, 0u, 0u,
                                                         &bytes),
                      RoboInvalidArgument);
    /* stride too small for x */
    RoboCheckEqInt(
        GfxPixelFormatPixelOffset(RoboGfxFormatRgba8, 4u, 1u, 0u, &off),
        RoboInvalidArgument);
}

static void TestPfRgbaBgra(void)
{
    uint8_t rgba[4];
    uint8_t bgra[4];
    GfxColorRgba8 c;
    GfxColorRgba8 out;

    c = GfxRgba8(10, 20, 30, 40);
    RoboCheckEqInt(GfxPixelPack(RoboGfxFormatRgba8, c, rgba, sizeof(rgba)),
                      RoboOk);
    RoboCheckEqInt(rgba[0], 10);
    RoboCheckEqInt(rgba[1], 20);
    RoboCheckEqInt(rgba[2], 30);
    RoboCheckEqInt(rgba[3], 40);

    RoboCheckEqInt(GfxPixelPack(RoboGfxFormatBgra8, c, bgra, sizeof(bgra)),
                      RoboOk);
    RoboCheckEqInt(bgra[0], 30);
    RoboCheckEqInt(bgra[1], 20);
    RoboCheckEqInt(bgra[2], 10);
    RoboCheckEqInt(bgra[3], 40);

    RoboCheckEqInt(GfxPixelConvert(RoboGfxFormatRgba8, rgba, 4, RoboGfxFormatBgra8,
                                             bgra, 4),
                      RoboOk);
    RoboCheckEqInt(bgra[0], 30);
    RoboCheckEqInt(bgra[2], 10);
    RoboCheckEqInt(GfxPixelUnpack(RoboGfxFormatBgra8, bgra, 4, &out), RoboOk);
    RoboCheckEqInt(out.r, 10);
    RoboCheckEqInt(out.g, 20);
    RoboCheckEqInt(out.b, 30);
    RoboCheckEqInt(out.a, 40);
}

static void TestPfRgbAlphaRules(void)
{
    uint8_t rgba[4] = {10, 20, 30, 40};
    uint8_t rgb[3];
    uint8_t rgba2[4];
    GfxColorRgba8 out;

    RoboCheckEqInt(GfxPixelConvert(RoboGfxFormatRgba8, rgba, 4, RoboGfxFormatRgb8,
                                             rgb, 3),
                      RoboOk);
    RoboCheckEqInt(rgb[0], 10);
    RoboCheckEqInt(rgb[1], 20);
    RoboCheckEqInt(rgb[2], 30);

    RoboCheckEqInt(GfxPixelConvert(RoboGfxFormatRgb8, rgb, 3, RoboGfxFormatRgba8,
                                             rgba2, 4),
                      RoboOk);
    RoboCheckEqInt(rgba2[0], 10);
    RoboCheckEqInt(rgba2[1], 20);
    RoboCheckEqInt(rgba2[2], 30);
    RoboCheckEqInt(rgba2[3], 255); /* opaque alpha */

    RoboCheckEqInt(GfxPixelUnpack(RoboGfxFormatRgb8, rgb, 3, &out), RoboOk);
    RoboCheckEqInt(out.a, 255);
}

static void TestPfGray(void)
{
    uint8_t rgba[4] = {100, 150, 200, 255};
    uint8_t gray[1];
    uint8_t rgba2[4];
    uint8_t y;

    y = GfxColorRgba8Luminance8(GfxRgba8(100, 150, 200, 255));
    RoboCheckEqInt(y, (int)((100u * 30u + 150u * 59u + 200u * 11u) / 100u));

    RoboCheckEqInt(GfxPixelConvert(RoboGfxFormatRgba8, rgba, 4, RoboGfxFormatGray8,
                                             gray, 1),
                      RoboOk);
    RoboCheckEqInt(gray[0], y);

    RoboCheckEqInt(GfxPixelConvert(RoboGfxFormatGray8, gray, 1, RoboGfxFormatRgba8,
                                             rgba2, 4),
                      RoboOk);
    RoboCheckEqInt(rgba2[0], y);
    RoboCheckEqInt(rgba2[1], y);
    RoboCheckEqInt(rgba2[2], y);
    RoboCheckEqInt(rgba2[3], 255);
}

static void TestPfAlpha8(void)
{
    uint8_t a0[1] = {0};
    uint8_t a127[1] = {127};
    uint8_t a255[1] = {255};
    uint8_t rgba[4];
    uint8_t outA[1];
    GfxColorRgba8 c;

    RoboCheckEqInt(GfxPixelUnpack(RoboGfxFormatAlpha8, a0, 1, &c), RoboOk);
    RoboCheckEqInt(c.r, 0);
    RoboCheckEqInt(c.g, 0);
    RoboCheckEqInt(c.b, 0);
    RoboCheckEqInt(c.a, 0);

    RoboCheckEqInt(GfxPixelUnpack(RoboGfxFormatAlpha8, a127, 1, &c), RoboOk);
    RoboCheckEqInt(c.a, 127);

    RoboCheckEqInt(GfxPixelUnpack(RoboGfxFormatAlpha8, a255, 1, &c), RoboOk);
    RoboCheckEqInt(c.a, 255);

    RoboCheckEqInt(GfxPixelPack(RoboGfxFormatAlpha8, GfxRgba8(1, 2, 3, 200),
                                          outA, 1),
                      RoboOk);
    RoboCheckEqInt(outA[0], 200);

    RoboCheckEqInt(GfxPixelConvert(RoboGfxFormatRgba8,
                                             (const uint8_t *)"\x01\x02\x03\x04", 4,
                                             RoboGfxFormatAlpha8, outA, 1),
                      RoboOk);
    RoboCheckEqInt(outA[0], 4);

    RoboCheckEqInt(GfxPixelConvert(RoboGfxFormatAlpha8, a127, 1, RoboGfxFormatRgba8,
                                             rgba, 4),
                      RoboOk);
    RoboCheckEqInt(rgba[0], 0);
    RoboCheckEqInt(rgba[3], 127);
}

static void TestPfInvalid(void)
{
    uint8_t buf[4];
    GfxColorRgba8 c;
    uint8_t bpp = 0u;

    RoboCheckEqInt(GetGfxPixelFormatInfo(RoboGfxFormatUnknown, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxPixelFormatBytesPerPixel((GfxPixelFormat)999, &bpp),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxPixelPack(RoboGfxFormatRgba16, GfxRgb8(1, 2, 3), buf, 8),
                      RoboNotSupported);
    RoboCheckEqInt(GfxPixelUnpack(RoboGfxFormatRgbaF32, buf, 16, &c),
                      RoboNotSupported);
    RoboCheckEqInt(GfxPixelPack(RoboGfxFormatRgba8, GfxRgb8(1, 2, 3), buf, 3),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxPixelConvert(RoboGfxFormatRgba8, buf, 4, RoboGfxFormatRgba16,
                                             buf, 8),
                      RoboNotSupported);
}

static void TestPfSurfaceAlpha8(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;
    const uint8_t *px;

    GfxSurfaceDescInit(&desc);
    desc.width = 2;
    desc.height = 1;
    desc.format = RoboGfxFormatAlpha8;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheckEqInt(GfxSurfaceClearRgba8(s, GfxRgba8(0, 0, 0, 200)), RoboOk);
    px = GfxSurfacePixelsConst(s);
    RoboCheckEqInt(px[0], 200);
    RoboCheckEqInt(px[1], 200);
    GfxSurfaceRelease(s);
}

void TestRegisterGraphicsPixelFormat(void)
{
    TestRegister("graphics.pf.meta", TestPfMetadataTable);
    TestRegister("graphics.pf.row", TestPfRowAndOffset);
    TestRegister("graphics.pf.overflow", TestPfOverflow);
    TestRegister("graphics.pf.rgba_bgra", TestPfRgbaBgra);
    TestRegister("graphics.pf.rgb_alpha", TestPfRgbAlphaRules);
    TestRegister("graphics.pf.gray", TestPfGray);
    TestRegister("graphics.pf.alpha8", TestPfAlpha8);
    TestRegister("graphics.pf.invalid", TestPfInvalid);
    TestRegister("graphics.pf.surface", TestPfSurfaceAlpha8);
}
