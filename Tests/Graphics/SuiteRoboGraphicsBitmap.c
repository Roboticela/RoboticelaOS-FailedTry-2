/*
 * RoboGraphics basic bitmap operations suite (Goal 6.5).
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <string.h>

static Result MakeSurface(uint32_t w,
                                  uint32_t h,
                                  GfxPixelFormat fmt,
                                  GfxSurface **out)
{
    GfxSurfaceDesc desc;
    GfxSurfaceDescInit(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = fmt;
    return GfxSurfaceCreate(&desc, out);
}

static void TestBmpGetSetFormats(void)
{
    static const GfxPixelFormat kFormats[] = {
        RoboGfxFormatRgba8, RoboGfxFormatBgra8, RoboGfxFormatRgb8,
        RoboGfxFormatGray8, RoboGfxFormatAlpha8};
    size_t i;

    for (i = 0u; i < sizeof(kFormats) / sizeof(kFormats[0]); i++) {
        GfxSurface *s = NULL;
        GfxColorRgba8 in = GfxRgba8(10, 20, 30, 40);
        GfxColorRgba8 out;
        GfxColorRgba8 expect;

        RoboCheckEqInt(MakeSurface(3, 3, kFormats[i], &s), RoboOk);
        RoboCheckEqInt(GfxSurfaceSetPixel(s, 0u, 0u, in), RoboOk);
        RoboCheckEqInt(GfxSurfaceSetPixel(s, 2u, 2u, in), RoboOk);
        RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &out), RoboOk);

        /* Round-trip through pack/unpack semantics. */
        {
            uint8_t packed[8];
            RoboCheckEqInt(GfxPixelPack(kFormats[i], in, packed, 8), RoboOk);
            RoboCheckEqInt(
                GfxPixelUnpack(kFormats[i], packed,
                                      GfxSurfaceBytesPerPixel(s), &expect),
                RoboOk);
        }
        RoboCheckEqInt(out.r, expect.r);
        RoboCheckEqInt(out.g, expect.g);
        RoboCheckEqInt(out.b, expect.b);
        RoboCheckEqInt(out.a, expect.a);

        RoboCheckEqInt(GfxSurfaceGetPixel(s, 3u, 0u, &out),
                          RoboInvalidArgument);
        RoboCheckEqInt(GfxSurfaceSetPixel(s, 0u, 3u, in),
                          RoboInvalidArgument);
        GfxSurfaceRelease(s);
    }
}

static void TestBmpClearSmall(void)
{
    static const uint32_t kSizes[] = {1u, 2u, 3u, 4u};
    size_t i;
    uint32_t x;
    uint32_t y;

    for (i = 0u; i < sizeof(kSizes) / sizeof(kSizes[0]); i++) {
        GfxSurface *s = NULL;
        GfxColorRgba8 c;
        uint32_t n = kSizes[i];

        RoboCheckEqInt(MakeSurface(n, n, RoboGfxFormatRgba8, &s), RoboOk);
        RoboCheckEqInt(GfxSurfaceClear(s, GfxRgba8(7, 8, 9, 10)),
                          RoboOk);
        for (y = 0u; y < n; y++) {
            for (x = 0u; x < n; x++) {
                RoboCheckEqInt(GfxSurfaceGetPixel(s, x, y, &c), RoboOk);
                RoboCheckEqInt(c.r, 7);
                RoboCheckEqInt(c.g, 8);
                RoboCheckEqInt(c.b, 9);
                RoboCheckEqInt(c.a, 10);
            }
        }
        GfxSurfaceRelease(s);
    }
}

static void TestBmpFillRect(void)
{
    GfxSurface *s = NULL;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(4, 4, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxSurfaceClear(s, GfxRgba8(0, 0, 0, 0)), RoboOk);

    /* Single pixel */
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, MakeGfxRect(1, 1, 1, 1),
                                   GfxRgba8(1, 2, 3, 4)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 1u, &c), RoboOk);
    RoboCheckEqInt(c.r, 1);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 0);

    /* Center 2×2 */
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, MakeGfxRect(1, 1, 2, 2),
                                   GfxRgba8(9, 9, 9, 9)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 2u, 2u, &c), RoboOk);
    RoboCheckEqInt(c.r, 9);

    /* Full surface */
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, MakeGfxRect(0, 0, 4, 4),
                                   GfxRgba8(5, 5, 5, 5)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 3u, 3u, &c), RoboOk);
    RoboCheckEqInt(c.r, 5);

    /* Empty = no-op */
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, MakeGfxRect(0, 0, 0, 4),
                                   GfxRgba8(1, 1, 1, 1)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 5);

    /* Out of bounds */
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, MakeGfxRect(3, 3, 2, 1),
                                   GfxRgba8(1, 1, 1, 1)),
        RoboInvalidArgument);
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, MakeGfxRect(-1, 0, 1, 1),
                                   GfxRgba8(1, 1, 1, 1)),
        RoboInvalidArgument);

    GfxSurfaceRelease(s);
}

static void PaintAbcPattern(GfxSurface *s)
{
    /* 3×3 distinct colors A..I */
    static const uint8_t vals[9] = {11, 22, 33, 44, 55, 66, 77, 88, 99};
    uint32_t i;
    for (i = 0u; i < 9u; i++) {
        uint8_t v = vals[i];
        RoboCheckEqInt(
            GfxSurfaceSetPixel(s, i % 3u, i / 3u, GfxRgba8(v, v, v, 255)),
            RoboOk);
    }
}

static void TestBmpCopy(void)
{
    GfxSurface *src = NULL;
    GfxSurface *dst = NULL;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(3, 3, RoboGfxFormatRgba8, &src), RoboOk);
    RoboCheckEqInt(MakeSurface(3, 3, RoboGfxFormatRgba8, &dst), RoboOk);
    PaintAbcPattern(src);
    RoboCheckEqInt(GfxSurfaceCopy(src, dst), RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(dst, 0u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 11);
    RoboCheckEqInt(GfxSurfaceGetPixel(dst, 2u, 2u, &c), RoboOk);
    RoboCheckEqInt(c.r, 99);

    /* Partial rect */
    RoboCheckEqInt(GfxSurfaceClear(dst, GfxRgba8(0, 0, 0, 0)), RoboOk);
    RoboCheckEqInt(
        GfxSurfaceCopyRect(src, MakeGfxRect(1, 1, 2, 2), dst,
                                   MakeGfxPoint(0, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(dst, 0u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 55); /* E */
    RoboCheckEqInt(GfxSurfaceGetPixel(dst, 1u, 1u, &c), RoboOk);
    RoboCheckEqInt(c.r, 99); /* I */

    /* Format convert RGBA8 → BGRA8 */
    {
        GfxSurface *bgra = NULL;
        RoboCheckEqInt(MakeSurface(3, 3, RoboGfxFormatBgra8, &bgra), RoboOk);
        RoboCheckEqInt(GfxSurfaceCopy(src, bgra), RoboOk);
        RoboCheckEqInt(GfxSurfaceGetPixel(bgra, 1u, 0u, &c), RoboOk);
        RoboCheckEqInt(c.r, 22);
        RoboCheckEqInt(c.g, 22);
        RoboCheckEqInt(c.b, 22);
        GfxSurfaceRelease(bgra);
    }

    /* Size mismatch */
    {
        GfxSurface *small = NULL;
        RoboCheckEqInt(MakeSurface(2, 2, RoboGfxFormatRgba8, &small), RoboOk);
        RoboCheckEqInt(GfxSurfaceCopy(src, small), RoboInvalidArgument);
        GfxSurfaceRelease(small);
    }

    GfxSurfaceRelease(src);
    GfxSurfaceRelease(dst);
}

static void TestBmpOverlap(void)
{
    GfxSurface *s = NULL;
    GfxColorRgba8 c;
    uint32_t x;

    /* Horizontal row: A B C D  → scroll left by 1 within [0,0,4,1] */
    RoboCheckEqInt(MakeSurface(4, 1, RoboGfxFormatRgba8, &s), RoboOk);
    for (x = 0u; x < 4u; x++) {
        uint8_t v = (uint8_t)(10u * (x + 1u));
        RoboCheckEqInt(
            GfxSurfaceSetPixel(s, x, 0u, GfxRgba8(v, 0, 0, 255)),
            RoboOk);
    }
    /* Copy [1,0,3,1] → (0,0): B C D overwrite A B C */
    RoboCheckEqInt(
        GfxSurfaceCopyRect(s, MakeGfxRect(1, 0, 3, 1), s, MakeGfxPoint(0, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 20);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 30);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 2u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 40);
    GfxSurfaceRelease(s);

    /* Scroll right: rebuild A B C D, copy [0,0,3,1] → (1,0) */
    RoboCheckEqInt(MakeSurface(4, 1, RoboGfxFormatRgba8, &s), RoboOk);
    for (x = 0u; x < 4u; x++) {
        uint8_t v = (uint8_t)(10u * (x + 1u));
        RoboCheckEqInt(
            GfxSurfaceSetPixel(s, x, 0u, GfxRgba8(v, 0, 0, 255)),
            RoboOk);
    }
    RoboCheckEqInt(
        GfxSurfaceCopyRect(s, MakeGfxRect(0, 0, 3, 1), s, MakeGfxPoint(1, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 10);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 2u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 20);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 3u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 30);
    GfxSurfaceRelease(s);

    /* Vertical: 3×1 column pattern in 1×3 — use 2×3 for clarity */
    RoboCheckEqInt(MakeSurface(1, 4, RoboGfxFormatRgba8, &s), RoboOk);
    for (x = 0u; x < 4u; x++) {
        uint8_t v = (uint8_t)(10u * (x + 1u));
        RoboCheckEqInt(
            GfxSurfaceSetPixel(s, 0u, x, GfxRgba8(v, 0, 0, 255)),
            RoboOk);
    }
    /* Move down: [0,0,1,3] → (0,1) */
    RoboCheckEqInt(
        GfxSurfaceCopyRect(s, MakeGfxRect(0, 0, 1, 3), s, MakeGfxPoint(0, 1)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 1u, &c), RoboOk);
    RoboCheckEqInt(c.r, 10);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 3u, &c), RoboOk);
    RoboCheckEqInt(c.r, 30);
    GfxSurfaceRelease(s);

    /* Move up: rebuild, [0,1,1,3] → (0,0) */
    RoboCheckEqInt(MakeSurface(1, 4, RoboGfxFormatRgba8, &s), RoboOk);
    for (x = 0u; x < 4u; x++) {
        uint8_t v = (uint8_t)(10u * (x + 1u));
        RoboCheckEqInt(
            GfxSurfaceSetPixel(s, 0u, x, GfxRgba8(v, 0, 0, 255)),
            RoboOk);
    }
    RoboCheckEqInt(
        GfxSurfaceCopyRect(s, MakeGfxRect(0, 1, 1, 3), s, MakeGfxPoint(0, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 20);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 2u, &c), RoboOk);
    RoboCheckEqInt(c.r, 40);
    GfxSurfaceRelease(s);
}

static void TestBmpErrors(void)
{
    GfxSurface *s = NULL;
    GfxSurfaceDesc desc;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(2, 2, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(NULL, 0u, 0u, &c),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxSurfaceSetPixel(s, 0u, 0u, GfxRgba8(1, 1, 1, 1)),
                      RoboOk);

    GfxSurfaceDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessRead;
    {
        GfxSurface *ro = NULL;
        RoboCheckEqInt(GfxSurfaceCreate(&desc, &ro), RoboOk);
        RoboCheckEqInt(
            GfxSurfaceSetPixel(ro, 0u, 0u, GfxRgba8(1, 1, 1, 1)),
            RoboInvalidArgument);
        RoboCheckEqInt(GfxSurfaceClear(ro, GfxRgba8(1, 1, 1, 1)),
                          RoboInvalidArgument);
        GfxSurfaceRelease(ro);
    }

    RoboCheckEqInt(
        GfxSurfaceCopyRect(s, MakeGfxRect(0, 0, 2, 2), s,
                                   MakeGfxPoint(1, 0)),
        RoboInvalidArgument);

    GfxSurfaceRelease(s);
}

void TestRegisterGraphicsBitmap(void)
{
    TestRegister("graphics.bmp.getset", TestBmpGetSetFormats);
    TestRegister("graphics.bmp.clear", TestBmpClearSmall);
    TestRegister("graphics.bmp.fill", TestBmpFillRect);
    TestRegister("graphics.bmp.copy", TestBmpCopy);
    TestRegister("graphics.bmp.overlap", TestBmpOverlap);
    TestRegister("graphics.bmp.errors", TestBmpErrors);
}
