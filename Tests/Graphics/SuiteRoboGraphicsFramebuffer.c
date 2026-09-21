/*
 * RoboGraphics framebuffer suite (Goal 6.3).
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void TestFbCreateFormats(void)
{
    static const GfxPixelFormat kFormats[] = {
        RoboGfxFormatRgba8, RoboGfxFormatBgra8, RoboGfxFormatRgb8,
        RoboGfxFormatGray8, RoboGfxFormatAlpha8};
    size_t i;

    for (i = 0u; i < sizeof(kFormats) / sizeof(kFormats[0]); i++) {
        GfxFramebufferDesc desc;
        GfxFramebuffer *fb = NULL;
        uint8_t bpp = 0u;
        size_t expectStride = 0u;

        GfxFramebufferDescInit(&desc);
        desc.width = 8;
        desc.height = 4;
        desc.format = kFormats[i];
        RoboCheckEqInt(GfxPixelFormatBytesPerPixel(kFormats[i], &bpp),
                          RoboOk);
        RoboCheckEqInt(
            GfxPixelFormatMinStride(kFormats[i], desc.width, &expectStride),
            RoboOk);
        RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboOk);
        RoboCheck(fb != NULL);
        RoboCheckEqInt((int)GfxFramebufferWidth(fb), 8);
        RoboCheckEqInt((int)GfxFramebufferHeight(fb), 4);
        RoboCheckEqInt((int)GfxFramebufferFormat(fb), (int)kFormats[i]);
        RoboCheckEqInt(GfxFramebufferBytesPerPixel(fb), bpp);
        RoboCheckEqInt((int)GfxFramebufferStride(fb), (int)expectStride);
        RoboCheckEqInt((int)GfxFramebufferByteSize(fb),
                          (int)(expectStride * 4u));
        RoboCheckEqInt((int)MakeGfxFramebufferOwnership(fb),
                          (int)RoboGfxFramebufferOwned);
        RoboCheck(GfxFramebufferPixels(fb) != NULL);
        RoboCheck(GetGfxFramebufferId(fb) != RoboGfxFramebufferIdNone);
        GfxFramebufferRelease(fb);
    }
}

static void TestFbSizes(void)
{
    GfxFramebufferDesc desc;
    GfxFramebuffer *fb = NULL;

    GfxFramebufferDescInit(&desc);
    desc.width = 1;
    desc.height = 1;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboOk);
    RoboCheckEqInt((int)GfxFramebufferStride(fb), 4);
    RoboCheckEqInt((int)GfxFramebufferByteSize(fb), 4);
    GfxFramebufferRelease(fb);

    desc.width = 2;
    desc.height = 2;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboOk);
    RoboCheckEqInt((int)GfxFramebufferByteSize(fb), 16);
    GfxFramebufferRelease(fb);

    desc.width = 800;
    desc.height = 600;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboOk);
    RoboCheckEqInt((int)GfxFramebufferStride(fb), 3200);
    RoboCheckEqInt((int)GfxFramebufferByteSize(fb), 1920000);
    GfxFramebufferRelease(fb);
}

static void TestFbZeroInit(void)
{
    GfxFramebufferDesc desc;
    GfxFramebuffer *fb = NULL;
    const uint8_t *px;
    size_t i;
    size_t n;

    GfxFramebufferDescInit(&desc);
    desc.width = 3;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboOk);
    px = GfxFramebufferPixelsConst(fb);
    n = GfxFramebufferByteSize(fb);
    for (i = 0u; i < n; i++) {
        RoboCheckEqInt(px[i], 0);
    }
    GfxFramebufferRelease(fb);
}

static void TestFbRowAndPixel(void)
{
    GfxFramebufferDesc desc;
    GfxFramebuffer *fb = NULL;
    uint8_t *row0 = NULL;
    uint8_t *row1 = NULL;
    uint8_t *px = NULL;
    size_t off = 0u;
    const uint8_t *base;

    GfxFramebufferDescInit(&desc);
    desc.width = 4;
    desc.height = 3;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboOk);
    base = GfxFramebufferPixelsConst(fb);

    RoboCheckEqInt(GfxFramebufferRow(fb, 0u, &row0), RoboOk);
    RoboCheck(row0 == base);
    RoboCheckEqInt(GfxFramebufferRow(fb, 1u, &row1), RoboOk);
    RoboCheck(row1 == base + 16);
    RoboCheckEqInt(GfxFramebufferRow(fb, 2u, &row0), RoboOk);
    RoboCheck(row0 == base + 32);
    RoboCheckEqInt(GfxFramebufferRow(fb, 3u, &row0), RoboInvalidArgument);

    RoboCheckEqInt(GfxFramebufferPixelOffset(fb, 0u, 0u, &off), RoboOk);
    RoboCheckEqInt((int)off, 0);
    RoboCheckEqInt(GfxFramebufferPixelOffset(fb, 1u, 0u, &off), RoboOk);
    RoboCheckEqInt((int)off, 4);
    RoboCheckEqInt(GfxFramebufferPixelOffset(fb, 0u, 1u, &off), RoboOk);
    RoboCheckEqInt((int)off, 16);
    RoboCheckEqInt(GfxFramebufferPixelOffset(fb, 3u, 2u, &off), RoboOk);
    RoboCheckEqInt((int)off, 44);

    RoboCheckEqInt(GfxFramebufferPixel(fb, 3u, 2u, &px), RoboOk);
    RoboCheck(px == base + 44);
    RoboCheckEqInt(GfxFramebufferPixel(fb, 4u, 0u, &px), RoboInvalidArgument);
    RoboCheckEqInt(GfxFramebufferPixel(fb, 0u, 3u, &px), RoboInvalidArgument);

    GfxFramebufferRelease(fb);
}

static void TestFbWritePixels(void)
{
    GfxFramebufferDesc desc;
    GfxFramebuffer *fb = NULL;
    uint8_t *p00 = NULL;
    uint8_t *p10 = NULL;
    uint8_t *p01 = NULL;
    uint8_t *p11 = NULL;
    uint8_t packed[4];

    GfxFramebufferDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboOk);

    RoboCheckEqInt(GfxFramebufferPixel(fb, 0u, 0u, &p00), RoboOk);
    RoboCheckEqInt(GfxFramebufferPixel(fb, 1u, 0u, &p10), RoboOk);
    RoboCheckEqInt(GfxFramebufferPixel(fb, 0u, 1u, &p01), RoboOk);
    RoboCheckEqInt(GfxFramebufferPixel(fb, 1u, 1u, &p11), RoboOk);

    RoboCheckEqInt(
        GfxPixelPack(RoboGfxFormatRgba8, GfxRgba8(1, 2, 3, 4), packed, 4),
        RoboOk);
    memcpy(p00, packed, 4);
    RoboCheckEqInt(
        GfxPixelPack(RoboGfxFormatRgba8, GfxRgba8(5, 6, 7, 8), packed, 4),
        RoboOk);
    memcpy(p10, packed, 4);
    RoboCheckEqInt(
        GfxPixelPack(RoboGfxFormatRgba8, GfxRgba8(9, 10, 11, 12), packed, 4),
        RoboOk);
    memcpy(p01, packed, 4);
    RoboCheckEqInt(
        GfxPixelPack(RoboGfxFormatRgba8, GfxRgba8(13, 14, 15, 16), packed,
                            4),
        RoboOk);
    memcpy(p11, packed, 4);

    RoboCheckEqInt(p00[0], 1);
    RoboCheckEqInt(p10[0], 5);
    RoboCheckEqInt(p01[0], 9);
    RoboCheckEqInt(p11[0], 13);
    RoboCheck(p00 + 4 == p10);
    RoboCheck(p00 + 8 == p01);
    RoboCheck(p00 + 12 == p11);

    GfxFramebufferRelease(fb);
}

static void TestFbCustomStride(void)
{
    GfxFramebufferDesc desc;
    GfxFramebuffer *fb = NULL;
    uint8_t *row1 = NULL;

    GfxFramebufferDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    desc.strideBytes = 16; /* padded rows */
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboOk);
    RoboCheckEqInt((int)GfxFramebufferStride(fb), 16);
    RoboCheckEqInt((int)GfxFramebufferByteSize(fb), 32);
    RoboCheckEqInt(GfxFramebufferRow(fb, 1u, &row1), RoboOk);
    RoboCheck(row1 == GfxFramebufferPixels(fb) + 16);
    GfxFramebufferRelease(fb);

    desc.strideBytes = 7; /* too small for 2×RGBA8 */
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboInvalidArgument);
}

static void TestFbErrors(void)
{
    GfxFramebufferDesc desc;
    GfxFramebuffer *fb = NULL;

    GfxFramebufferDescInit(&desc);
    desc.width = 0;
    desc.height = 1;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboInvalidArgument);
    desc.width = 1;
    desc.height = 0;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboInvalidArgument);
    desc.height = 1;
    desc.format = RoboGfxFormatUnknown;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboInvalidArgument);
    desc.format = RoboGfxFormatRgbaF32;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboNotSupported);
    desc.format = RoboGfxFormatRgba8;
    desc.width = RoboGfxMaxSurfaceDim + 1u;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboInvalidArgument);
    RoboCheckEqInt(GfxFramebufferCreate(NULL, &fb), RoboInvalidArgument);
}

static void TestFbOverflow(void)
{
    GfxFramebufferDesc desc;
    GfxFramebuffer *fb = NULL;

    GfxFramebufferDescInit(&desc);
    desc.width = 1;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    desc.strideBytes = (SIZE_MAX / 2u) + 1u;
    /* validate_stride needs stride >= min (4); then buffer height*stride overflows */
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboInvalidArgument);
    RoboCheck(fb == NULL);
}

static void TestFbBorrowed(void)
{
    uint8_t storage[32];
    GfxFramebufferDesc desc;
    GfxFramebuffer *fb = NULL;

    memset(storage, 0xCD, sizeof(storage));
    GfxFramebufferDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    desc.strideBytes = 16;
    RoboCheckEqInt(GfxFramebufferCreateBorrowed(&desc, storage, &fb), RoboOk);
    RoboCheckEqInt((int)MakeGfxFramebufferOwnership(fb),
                      (int)RoboGfxFramebufferBorrowed);
    RoboCheck(GfxFramebufferPixels(fb) == storage);
    GfxFramebufferRelease(fb);
    RoboCheckEqInt(storage[0], 0xCD);
}

static void TestFbOom(void)
{
    GfxFramebufferDesc desc;
    GfxFramebuffer *fb = NULL;

    GfxFramebufferDescInit(&desc);
    desc.width = 4;
    desc.height = 4;
    desc.format = RoboGfxFormatRgba8;

    MemDebugFailNext(1);
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboOutOfMemory);
    RoboCheck(fb == NULL);

    /* Object alloc succeeds, pixel alloc fails */
    MemDebugFailNext(2);
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboOutOfMemory);
    RoboCheck(fb == NULL);
    MemDebugFailNext(0);
}

static void TestFbLifetime(void)
{
    GfxFramebufferDesc desc;
    GfxFramebuffer *fb = NULL;

    GfxFramebufferDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatGray8;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboOk);
    GfxFramebufferRetain(fb);
    GfxFramebufferRelease(fb);
    GfxFramebufferRelease(fb);
    GfxFramebufferRelease(NULL);
}

void TestRegisterGraphicsFramebuffer(void)
{
    TestRegister("graphics.fb.create", TestFbCreateFormats);
    TestRegister("graphics.fb.sizes", TestFbSizes);
    TestRegister("graphics.fb.zero", TestFbZeroInit);
    TestRegister("graphics.fb.address", TestFbRowAndPixel);
    TestRegister("graphics.fb.write", TestFbWritePixels);
    TestRegister("graphics.fb.stride", TestFbCustomStride);
    TestRegister("graphics.fb.errors", TestFbErrors);
    TestRegister("graphics.fb.overflow", TestFbOverflow);
    TestRegister("graphics.fb.borrowed", TestFbBorrowed);
    TestRegister("graphics.fb.oom", TestFbOom);
    TestRegister("graphics.fb.lifetime", TestFbLifetime);
}
