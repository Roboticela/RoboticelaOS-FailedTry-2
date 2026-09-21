/*
 * RoboGraphics surface suite (Goal 6.4).
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void TestSurfCreateFormats(void)
{
    static const GfxPixelFormat kFormats[] = {
        RoboGfxFormatRgba8, RoboGfxFormatBgra8, RoboGfxFormatRgb8,
        RoboGfxFormatGray8, RoboGfxFormatAlpha8};
    size_t i;

    for (i = 0u; i < sizeof(kFormats) / sizeof(kFormats[0]); i++) {
        GfxSurfaceDesc desc;
        GfxSurface *s = NULL;
        uint8_t bpp = 0u;
        size_t expectStride = 0u;

        GfxSurfaceDescInit(&desc);
        desc.width = 8;
        desc.height = 4;
        desc.format = kFormats[i];
        RoboCheckEqInt(GfxPixelFormatBytesPerPixel(kFormats[i], &bpp),
                          RoboOk);
        RoboCheckEqInt(
            GfxPixelFormatMinStride(kFormats[i], desc.width, &expectStride),
            RoboOk);
        RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
        RoboCheck(s != NULL);
        RoboCheckEqInt((int)GfxSurfaceWidth(s), 8);
        RoboCheckEqInt((int)GfxSurfaceHeight(s), 4);
        RoboCheckEqInt((int)GfxSurfaceFormat(s), (int)kFormats[i]);
        RoboCheckEqInt(GfxSurfaceBytesPerPixel(s), bpp);
        RoboCheckEqInt((int)GfxSurfaceStride(s), (int)expectStride);
        RoboCheckEqInt((int)GfxSurfaceByteSize(s), (int)(expectStride * 4u));
        RoboCheckEqInt((int)MakeGfxSurfaceOwnership(s),
                          (int)RoboGfxSurfaceOwned);
        RoboCheck(GfxSurfacePixelsConst(s) != NULL);
        RoboCheck(GfxSurfaceFramebuffer(s) != NULL);
        RoboCheck(GetGfxSurfaceId(s) != RoboGfxSurfaceIdNone);
        RoboCheck(!GfxSurfaceIsMapped(s));
        GfxSurfaceRelease(s);
    }
}

static void TestSurfSizes(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;

    GfxSurfaceDescInit(&desc);
    desc.width = 1;
    desc.height = 1;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheckEqInt((int)GfxSurfaceByteSize(s), 4);
    GfxSurfaceRelease(s);

    desc.width = 2;
    desc.height = 2;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheckEqInt((int)GfxSurfaceByteSize(s), 16);
    GfxSurfaceRelease(s);

    desc.width = 800;
    desc.height = 600;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheckEqInt((int)GfxSurfaceStride(s), 3200);
    RoboCheckEqInt((int)GfxSurfaceByteSize(s), 1920000);
    GfxSurfaceRelease(s);
}

static void TestSurfMap(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;
    GfxSurfaceMap map;

    GfxSurfaceDescInit(&desc);
    desc.width = 3;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);

    RoboCheckEqInt(GetGfxSurfaceMap(s, RoboGfxSurfaceAccessRead, &map),
                      RoboOk);
    RoboCheck(GfxSurfaceIsMapped(s));
    RoboCheck(map.pixels == NULL);
    RoboCheck(map.pixelsConst != NULL);
    RoboCheckEqInt((int)map.width, 3);
    RoboCheckEqInt((int)map.height, 2);
    RoboCheckEqInt((int)map.strideBytes, 12);
    RoboCheckEqInt((int)map.byteSize, 24);
    RoboCheckEqInt((int)map.format, (int)RoboGfxFormatRgba8);
    RoboCheckEqInt((int)map.access, (int)RoboGfxSurfaceAccessRead);
    RoboCheckEqInt(GetGfxSurfaceMap(s, RoboGfxSurfaceAccessWrite, &map),
                      RoboBusy);
    RoboCheckEqInt(GfxSurfaceUnmap(s), RoboOk);
    RoboCheck(!GfxSurfaceIsMapped(s));
    RoboCheckEqInt(GfxSurfaceUnmap(s), RoboInvalidArgument);

    RoboCheckEqInt(GetGfxSurfaceMap(s, RoboGfxSurfaceAccessWrite, &map),
                      RoboOk);
    RoboCheck(map.pixels != NULL);
    RoboCheck(map.pixels == map.pixelsConst);
    RoboCheckEqInt(GfxSurfaceUnmap(s), RoboOk);

    RoboCheckEqInt(
        GetGfxSurfaceMap(s, RoboGfxSurfaceAccessReadWrite, &map), RoboOk);
    RoboCheck(map.pixels != NULL);
    RoboCheckEqInt(GfxSurfaceUnmap(s), RoboOk);

    GfxSurfaceRelease(s);
}

static void TestSurfAccessModes(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;
    GfxSurfaceMap map;

    GfxSurfaceDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessRead;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheck(GfxSurfacePixels(s) == NULL);
    RoboCheckEqInt(GetGfxSurfaceMap(s, RoboGfxSurfaceAccessWrite, &map),
                      RoboInvalidArgument);
    RoboCheckEqInt(GetGfxSurfaceMap(s, RoboGfxSurfaceAccessRead, &map),
                      RoboOk);
    RoboCheckEqInt(GfxSurfaceUnmap(s), RoboOk);
    RoboCheckEqInt(GfxSurfaceClearRgba8(s, GfxRgba8(1, 2, 3, 4)),
                      RoboInvalidArgument);
    GfxSurfaceRelease(s);

    desc.access = RoboGfxSurfaceAccessWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheckEqInt(GetGfxSurfaceMap(s, RoboGfxSurfaceAccessRead, &map),
                      RoboInvalidArgument);
    RoboCheckEqInt(GetGfxSurfaceMap(s, RoboGfxSurfaceAccessWrite, &map),
                      RoboOk);
    RoboCheckEqInt(GfxSurfaceUnmap(s), RoboOk);
    GfxSurfaceRelease(s);
}

static void TestSurfPixelAddress(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;
    uint8_t *p00 = NULL;
    uint8_t *p10 = NULL;
    uint8_t *p01 = NULL;
    uint8_t *p11 = NULL;
    uint8_t *row1 = NULL;
    size_t off = 0u;
    uint8_t packed[4];

    GfxSurfaceDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);

    RoboCheckEqInt(GfxSurfacePixelOffset(s, 0u, 0u, &off), RoboOk);
    RoboCheckEqInt((int)off, 0);
    RoboCheckEqInt(GfxSurfacePixelOffset(s, 1u, 0u, &off), RoboOk);
    RoboCheckEqInt((int)off, 4);
    RoboCheckEqInt(GfxSurfacePixelOffset(s, 0u, 1u, &off), RoboOk);
    RoboCheckEqInt((int)off, 8);
    RoboCheckEqInt(GfxSurfacePixelOffset(s, 1u, 1u, &off), RoboOk);
    RoboCheckEqInt((int)off, 12);

    RoboCheckEqInt(GfxSurfaceRowChecked(s, 1u, &row1), RoboOk);
    RoboCheck(row1 == GfxSurfacePixels(s) + 8);
    RoboCheckEqInt(GfxSurfaceRowChecked(s, 2u, &row1), RoboInvalidArgument);

    RoboCheckEqInt(GfxSurfacePixel(s, 0u, 0u, &p00), RoboOk);
    RoboCheckEqInt(GfxSurfacePixel(s, 1u, 0u, &p10), RoboOk);
    RoboCheckEqInt(GfxSurfacePixel(s, 0u, 1u, &p01), RoboOk);
    RoboCheckEqInt(GfxSurfacePixel(s, 1u, 1u, &p11), RoboOk);

    RoboCheckEqInt(
        GfxPixelPack(RoboGfxFormatRgba8, GfxRgba8(10, 20, 30, 40), packed,
                            4),
        RoboOk);
    memcpy(p00, packed, 4);
    RoboCheckEqInt(
        GfxPixelPack(RoboGfxFormatRgba8, GfxRgba8(50, 60, 70, 80), packed,
                            4),
        RoboOk);
    memcpy(p10, packed, 4);
    RoboCheckEqInt(
        GfxPixelPack(RoboGfxFormatRgba8, GfxRgba8(90, 100, 110, 120),
                            packed, 4),
        RoboOk);
    memcpy(p01, packed, 4);
    RoboCheckEqInt(
        GfxPixelPack(RoboGfxFormatRgba8, GfxRgba8(130, 140, 150, 160),
                            packed, 4),
        RoboOk);
    memcpy(p11, packed, 4);

    RoboCheckEqInt(p00[0], 10);
    RoboCheckEqInt(p10[0], 50);
    RoboCheckEqInt(p01[0], 90);
    RoboCheckEqInt(p11[0], 130);
    RoboCheck(p00 + 4 == p10);
    RoboCheck(p00 + 8 == p01);
    RoboCheck(p00 + 12 == p11);

    RoboCheckEqInt(GfxSurfacePixel(s, 2u, 0u, &p00), RoboInvalidArgument);
    RoboCheckEqInt(GfxSurfacePixel(s, 0u, 2u, &p00), RoboInvalidArgument);

    GfxSurfaceRelease(s);
}

static void TestSurfClear(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;
    const uint8_t *px;

    GfxSurfaceDescInit(&desc);
    desc.width = 2;
    desc.height = 1;
    desc.format = RoboGfxFormatBgra8;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheckEqInt(GfxSurfaceClearRgba8(s, GfxRgba8(1, 2, 3, 4)),
                      RoboOk);
    px = GfxSurfacePixelsConst(s);
    RoboCheckEqInt(px[0], 3); /* B */
    RoboCheckEqInt(px[1], 2);
    RoboCheckEqInt(px[2], 1); /* R */
    RoboCheckEqInt(px[3], 4);
    RoboCheckEqInt(px[4], 3);
    GfxSurfaceRelease(s);

    desc.format = RoboGfxFormatGray8;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheckEqInt(GfxSurfaceClearRgba8(s, GfxRgba8(100, 0, 0, 255)),
                      RoboOk);
    px = GfxSurfacePixelsConst(s);
    RoboCheckEqInt(px[0], (int)GfxColorRgba8Luminance8(
                                 GfxRgba8(100, 0, 0, 255)));
    GfxSurfaceRelease(s);

    desc.format = RoboGfxFormatAlpha8;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheckEqInt(GfxSurfaceClearRgba8(s, GfxRgba8(0, 0, 0, 200)),
                      RoboOk);
    px = GfxSurfacePixelsConst(s);
    RoboCheckEqInt(px[0], 200);
    RoboCheckEqInt(px[1], 200);
    GfxSurfaceRelease(s);
}

static void TestSurfErrors(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;

    GfxSurfaceDescInit(&desc);
    desc.width = 0;
    desc.height = 1;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboInvalidArgument);
    desc.width = 1;
    desc.height = 0;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboInvalidArgument);
    desc.height = 1;
    desc.format = RoboGfxFormatUnknown;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboInvalidArgument);
    desc.format = RoboGfxFormatRgbaF32;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboNotSupported);
    desc.format = RoboGfxFormatRgba8;
    desc.width = RoboGfxMaxSurfaceDim + 1u;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboInvalidArgument);
    desc.width = 1;
    desc.strideBytes = (SIZE_MAX / 2u) + 1u;
    desc.height = 2;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboInvalidArgument);
    RoboCheck(s == NULL);
}

static void TestSurfOom(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;

    GfxSurfaceDescInit(&desc);
    desc.width = 4;
    desc.height = 4;
    desc.format = RoboGfxFormatRgba8;

    /* framebuffer object alloc fails */
    MemDebugFailNext(1);
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOutOfMemory);
    RoboCheck(s == NULL);

    /* fb object ok, pixels fail */
    MemDebugFailNext(2);
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOutOfMemory);
    RoboCheck(s == NULL);

    /* fb fully ok, surface object fails */
    MemDebugFailNext(3);
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOutOfMemory);
    RoboCheck(s == NULL);
    MemDebugFailNext(0);
}

static void TestSurfBorrowedAndLifetime(void)
{
    uint8_t storage[32];
    GfxSurfaceDesc desc;
    GfxSurface *s = NULL;
    GfxSurfaceMap map;

    memset(storage, 0xAB, sizeof(storage));
    GfxSurfaceDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    desc.strideBytes = 16;
    RoboCheckEqInt(GfxSurfaceCreateBorrowed(&desc, storage, &s), RoboOk);
    RoboCheckEqInt((int)MakeGfxSurfaceOwnership(s),
                      (int)RoboGfxSurfaceBorrowed);
    RoboCheck(GfxSurfacePixels(s) == storage);
    RoboCheckEqInt(GetGfxSurfaceMap(s, RoboGfxSurfaceAccessReadWrite, &map),
                      RoboOk);
    RoboCheck(map.pixels == storage);
    GfxSurfaceRetain(s);
    RoboCheckEqInt(GfxSurfaceUnmap(s), RoboOk);
    GfxSurfaceRelease(s);
    GfxSurfaceRelease(s);
    RoboCheckEqInt(storage[0], 0xAB);
}

void TestRegisterGraphicsSurface(void)
{
    TestRegister("graphics.surf.create", TestSurfCreateFormats);
    TestRegister("graphics.surf.sizes", TestSurfSizes);
    TestRegister("graphics.surf.map", TestSurfMap);
    TestRegister("graphics.surf.access", TestSurfAccessModes);
    TestRegister("graphics.surf.address", TestSurfPixelAddress);
    TestRegister("graphics.surf.clear", TestSurfClear);
    TestRegister("graphics.surf.errors", TestSurfErrors);
    TestRegister("graphics.surf.oom", TestSurfOom);
    TestRegister("graphics.surf.lifetime", TestSurfBorrowedAndLifetime);
}
