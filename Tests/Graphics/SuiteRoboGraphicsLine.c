/*
 * RoboGraphics Goal 6.7 — one-pixel line rasterization.
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <stdint.h>
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
    desc.access = RoboGfxSurfaceAccessReadWrite;
    return GfxSurfaceCreate(&desc, out);
}

static int PixelIs(GfxSurface *s,
                    uint32_t x,
                    uint32_t y,
                    uint8_t r,
                    uint8_t g,
                    uint8_t b,
                    uint8_t a)
{
    GfxColorRgba8 c;
    if (GfxSurfaceGetPixel(s, x, y, &c) != RoboOk) {
        return 0;
    }
    return c.r == r && c.g == g && c.b == b && c.a == a;
}

static int PixelMarked(GfxSurface *s, uint32_t x, uint32_t y)
{
    return PixelIs(s, x, y, 255, 0, 0, 255);
}

static int PixelClear(GfxSurface *s, uint32_t x, uint32_t y)
{
    return PixelIs(s, x, y, 0, 0, 0, 0);
}

static void ClearBlack(GfxSurface *s)
{
    RoboCheckEqInt(GfxSurfaceClear(s, GfxRgba8(0, 0, 0, 0)),
                      RoboOk);
}

static const GfxColorRgba8 kRed = {255, 0, 0, 255};

static void ExpectOnly(GfxSurface *s,
                        const int32_t *pts,
                        unsigned n)
{
    uint32_t w = GfxSurfaceWidth(s);
    uint32_t h = GfxSurfaceHeight(s);
    uint32_t x;
    uint32_t y;
    unsigned i;

    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x++) {
            int want = 0;
            for (i = 0u; i < n; i++) {
                if (pts[i * 2] == (int32_t)x && pts[i * 2 + 1] == (int32_t)y) {
                    want = 1;
                    break;
                }
            }
            if (want) {
                RoboCheck(PixelMarked(s, x, y));
            } else {
                RoboCheck(PixelClear(s, x, y));
            }
        }
    }
}

static void TestLineSingle(void)
{
    GfxSurface *s = NULL;
    static const int32_t pts[] = {2, 2};

    RoboCheckEqInt(MakeSurface(5, 5, RoboGfxFormatRgba8, &s), RoboOk);
    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(2, 2), MakeGfxPoint(2, 2),
                                   kRed),
        RoboOk);
    ExpectOnly(s, pts, 1u);
    GfxSurfaceRelease(s);
}

static void TestLineHorizontal(void)
{
    GfxSurface *s = NULL;
    static const int32_t ltr[] = {2, 5, 3, 5, 4, 5, 5, 5, 6, 5, 7, 5, 8, 5};
    static const int32_t rtl[] = {2, 5, 3, 5, 4, 5, 5, 5, 6, 5, 7, 5, 8, 5};

    RoboCheckEqInt(MakeSurface(12, 10, RoboGfxFormatRgba8, &s), RoboOk);
    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(2, 5), MakeGfxPoint(8, 5),
                                   kRed),
        RoboOk);
    ExpectOnly(s, ltr, 7u);

    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(8, 5), MakeGfxPoint(2, 5),
                                   kRed),
        RoboOk);
    ExpectOnly(s, rtl, 7u);
    GfxSurfaceRelease(s);
}

static void TestLineVertical(void)
{
    GfxSurface *s = NULL;
    static const int32_t pts[] = {5, 2, 5, 3, 5, 4, 5, 5, 5, 6, 5, 7, 5, 8};

    RoboCheckEqInt(MakeSurface(10, 12, RoboGfxFormatRgba8, &s), RoboOk);
    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(5, 2), MakeGfxPoint(5, 8),
                                   kRed),
        RoboOk);
    ExpectOnly(s, pts, 7u);

    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(5, 8), MakeGfxPoint(5, 2),
                                   kRed),
        RoboOk);
    ExpectOnly(s, pts, 7u);
    GfxSurfaceRelease(s);
}

static void TestLineDiagonal(void)
{
    GfxSurface *s = NULL;
    static const int32_t d1[] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5};
    static const int32_t d2[] = {0, 5, 1, 4, 2, 3, 3, 2, 4, 1, 5, 0};

    RoboCheckEqInt(MakeSurface(6, 6, RoboGfxFormatRgba8, &s), RoboOk);

    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(0, 0), MakeGfxPoint(5, 5),
                                   kRed),
        RoboOk);
    ExpectOnly(s, d1, 6u);

    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(5, 5), MakeGfxPoint(0, 0),
                                   kRed),
        RoboOk);
    ExpectOnly(s, d1, 6u);

    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(0, 5), MakeGfxPoint(5, 0),
                                   kRed),
        RoboOk);
    ExpectOnly(s, d2, 6u);

    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(5, 0), MakeGfxPoint(0, 5),
                                   kRed),
        RoboOk);
    ExpectOnly(s, d2, 6u);

    GfxSurfaceRelease(s);
}

static void TestLineShallowSteep(void)
{
    GfxSurface *s = NULL;
    /* (0,0)->(4,2): (0,0)(1,0)(2,1)(3,1)(4,2) */
    static const int32_t shallow[] = {0, 0, 1, 0, 2, 1, 3, 1, 4, 2};
    /* (0,0)->(2,4): (0,0)(0,1)(1,2)(1,3)(2,4) */
    static const int32_t steep[] = {0, 0, 0, 1, 1, 2, 1, 3, 2, 4};

    RoboCheckEqInt(MakeSurface(6, 6, RoboGfxFormatRgba8, &s), RoboOk);

    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(0, 0), MakeGfxPoint(4, 2),
                                   kRed),
        RoboOk);
    ExpectOnly(s, shallow, 5u);

    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(4, 2), MakeGfxPoint(0, 0),
                                   kRed),
        RoboOk);
    ExpectOnly(s, shallow, 5u);

    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(0, 0), MakeGfxPoint(2, 4),
                                   kRed),
        RoboOk);
    ExpectOnly(s, steep, 5u);

    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(2, 4), MakeGfxPoint(0, 0),
                                   kRed),
        RoboOk);
    ExpectOnly(s, steep, 5u);

    GfxSurfaceRelease(s);
}

static void TestLineClip(void)
{
    GfxSurface *s = NULL;
    uint32_t x;
    uint32_t y;
    unsigned marked;

    RoboCheckEqInt(MakeSurface(8, 8, RoboGfxFormatRgba8, &s), RoboOk);

    /* Completely outside */
    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(-20, -20),
                                   MakeGfxPoint(-5, -5), kRed),
        RoboOk);
    for (y = 0u; y < 8u; y++) {
        for (x = 0u; x < 8u; x++) {
            RoboCheck(PixelClear(s, x, y));
        }
    }

    /* Horizontal crossing: (-10,3) → (20,3) → pixels (0..7, 3) */
    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(-10, 3),
                                   MakeGfxPoint(20, 3), kRed),
        RoboOk);
    marked = 0u;
    for (x = 0u; x < 8u; x++) {
        RoboCheck(PixelMarked(s, x, 3u));
        marked++;
        for (y = 0u; y < 8u; y++) {
            if (y != 3u) {
                RoboCheck(PixelClear(s, x, y));
            }
        }
    }
    RoboCheckEqInt((int)marked, 8);

    /* Partial diagonal from outside */
    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(-2, -2),
                                   MakeGfxPoint(3, 3), kRed),
        RoboOk);
    RoboCheck(PixelMarked(s, 0u, 0u));
    RoboCheck(PixelMarked(s, 3u, 3u));

    /* Edges / corners */
    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(0, 0), MakeGfxPoint(7, 0),
                                   kRed),
        RoboOk);
    RoboCheck(PixelMarked(s, 0u, 0u));
    RoboCheck(PixelMarked(s, 7u, 0u));

    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(0, 0), MakeGfxPoint(0, 7),
                                   kRed),
        RoboOk);
    RoboCheck(PixelMarked(s, 0u, 7u));

    ClearBlack(s);
    RoboCheckEqInt(
        GfxSurfaceDrawLine(s, MakeGfxPoint(7, 7), MakeGfxPoint(0, 7),
                                   kRed),
        RoboOk);
    RoboCheck(PixelMarked(s, 7u, 7u));

    GfxSurfaceRelease(s);
}

static void TestLineFormats(void)
{
    static const GfxPixelFormat formats[] = {
        RoboGfxFormatRgba8, RoboGfxFormatBgra8, RoboGfxFormatRgb8,
        RoboGfxFormatGray8, RoboGfxFormatAlpha8};
    size_t i;
    GfxColorRgba8 ink = GfxRgba8(10, 20, 30, 200);
    GfxColorRgba8 out;

    for (i = 0u; i < sizeof(formats) / sizeof(formats[0]); i++) {
        GfxSurface *s = NULL;
        uint8_t packed[8];
        GfxColorRgba8 expect;

        RoboCheckEqInt(MakeSurface(4, 4, formats[i], &s), RoboOk);
        RoboCheckEqInt(GfxSurfaceClear(s, GfxRgba8(0, 0, 0, 0)),
                          RoboOk);
        RoboCheckEqInt(
            GfxSurfaceDrawLine(s, MakeGfxPoint(0, 1),
                                       MakeGfxPoint(3, 1), ink),
            RoboOk);
        RoboCheckEqInt(GfxPixelPack(formats[i], ink, packed, 8),
                          RoboOk);
        RoboCheckEqInt(
            GfxPixelUnpack(formats[i], packed,
                                  GfxSurfaceBytesPerPixel(s), &expect),
            RoboOk);
        RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 1u, &out), RoboOk);
        RoboCheckEqInt(out.r, expect.r);
        RoboCheckEqInt(out.g, expect.g);
        RoboCheckEqInt(out.b, expect.b);
        RoboCheckEqInt(out.a, expect.a);
        RoboCheckEqInt(GfxSurfaceGetPixel(s, 3u, 1u, &out), RoboOk);
        RoboCheckEqInt(out.r, expect.r);
        GfxSurfaceRelease(s);
    }
}

static void TestLineSegAndErrors(void)
{
    GfxSurface *s = NULL;
    GfxSurfaceDesc desc;
    GfxLine line =
        MakeGfxLine(MakeGfxPoint(0, 0), MakeGfxPoint(2, 0));

    RoboCheckEqInt(MakeSurface(4, 4, RoboGfxFormatRgba8, &s), RoboOk);
    ClearBlack(s);
    RoboCheckEqInt(GfxSurfaceDrawLineSeg(s, line, kRed), RoboOk);
    RoboCheck(PixelMarked(s, 0u, 0u));
    RoboCheck(PixelMarked(s, 2u, 0u));

    RoboCheckEqInt(
        GfxSurfaceDrawLine(NULL, MakeGfxPoint(0, 0),
                                   MakeGfxPoint(1, 1), kRed),
        RoboInvalidArgument);

    GfxSurfaceDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessRead;
    {
        GfxSurface *ro = NULL;
        RoboCheckEqInt(GfxSurfaceCreate(&desc, &ro), RoboOk);
        RoboCheckEqInt(
            GfxSurfaceDrawLine(ro, MakeGfxPoint(0, 0),
                                       MakeGfxPoint(1, 0), kRed),
            RoboInvalidArgument);
        GfxSurfaceRelease(ro);
    }

    GfxSurfaceRelease(s);
}

static void TestLineSmokeMany(void)
{
    GfxSurface *s = NULL;
    int i;

    RoboCheckEqInt(MakeSurface(64, 64, RoboGfxFormatRgba8, &s), RoboOk);
    ClearBlack(s);
    for (i = 0; i < 2000; i++) {
        int32_t x0 = (int32_t)(i % 64);
        int32_t y0 = (int32_t)((i / 3) % 64);
        int32_t x1 = (int32_t)((i * 7) % 64);
        int32_t y1 = (int32_t)((i * 11) % 64);
        RoboCheckEqInt(
            GfxSurfaceDrawLine(s, MakeGfxPoint(x0, y0),
                                       MakeGfxPoint(x1, y1), kRed),
            RoboOk);
    }
    /* Spot-check a few pixels remain valid / addressable. */
    {
        GfxColorRgba8 c;
        RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
        RoboCheckEqInt(GfxSurfaceGetPixel(s, 63u, 63u, &c), RoboOk);
    }
    GfxSurfaceRelease(s);
}

void TestRegisterGraphicsLine(void)
{
    TestRegister("graphics.line.single", TestLineSingle);
    TestRegister("graphics.line.horizontal", TestLineHorizontal);
    TestRegister("graphics.line.vertical", TestLineVertical);
    TestRegister("graphics.line.diagonal", TestLineDiagonal);
    TestRegister("graphics.line.slope", TestLineShallowSteep);
    TestRegister("graphics.line.clip", TestLineClip);
    TestRegister("graphics.line.formats", TestLineFormats);
    TestRegister("graphics.line.errors", TestLineSegAndErrors);
    TestRegister("graphics.line.smoke", TestLineSmokeMany);
}
