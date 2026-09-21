/*
 * RoboGraphics Goal 6.10 — source-over alpha compositing.
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

static int ColorEq(GfxColorRgba8 a, GfxColorRgba8 b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void ExpectColor(GfxColorRgba8 got,
                         uint8_t r,
                         uint8_t g,
                         uint8_t b,
                         uint8_t a)
{
    RoboCheckEqInt(got.r, r);
    RoboCheckEqInt(got.g, g);
    RoboCheckEqInt(got.b, b);
    RoboCheckEqInt(got.a, a);
}

static void TestCompMulDiv255(void)
{
    RoboCheckEqInt(GfxMulDiv255(0u, 255u), 0);
    RoboCheckEqInt(GfxMulDiv255(255u, 255u), 255);
    RoboCheckEqInt(GfxMulDiv255(255u, 128u), 128);
    RoboCheckEqInt(GfxMulDiv255(128u, 128u), 64);
    RoboCheckEqInt(GfxMulDiv255(1u, 1u), 0); /* rounds down */
}

static void TestCompPremulRoundtrip(void)
{
    GfxColorRgba8 s = GfxRgba8(200u, 100u, 50u, 255u);
    GfxColorRgba8 p = GfxColorPremultiply(s);
    GfxColorRgba8 u = GfxColorUnpremultiply(p);

    RoboCheck(ColorEq(s, p));
    RoboCheck(ColorEq(s, u));

    s = GfxRgba8(255u, 0u, 0u, 0u);
    p = GfxColorPremultiply(s);
    ExpectColor(p, 0u, 0u, 0u, 0u);

    s = GfxRgba8(255u, 128u, 64u, 128u);
    p = GfxColorPremultiply(s);
    ExpectColor(p, 128u, 64u, 32u, 128u);
    u = GfxColorUnpremultiply(p);
    ExpectColor(u, 255u, 128u, 64u, 128u);
}

static void TestCompSrcOverBasics(void)
{
    GfxColorRgba8 src;
    GfxColorRgba8 dst;
    GfxColorRgba8 out;

    dst = GfxRgba8(10u, 20u, 30u, 255u);
    src = GfxRgba8(255u, 0u, 0u, 0u);
    out = GfxCompositeSrcOver(src, dst);
    RoboCheck(ColorEq(out, dst));

    src = GfxRgba8(1u, 2u, 3u, 255u);
    out = GfxCompositeSrcOver(src, dst);
    RoboCheck(ColorEq(out, src));

    dst = GfxRgba8(0u, 0u, 0u, 0u);
    src = GfxRgba8(255u, 0u, 0u, 128u);
    out = GfxCompositeSrcOver(src, dst);
    ExpectColor(out, 255u, 0u, 0u, 128u);

    /* 50% red over opaque white → (255,127,127,255) */
    src = GfxRgba8(255u, 0u, 0u, 128u);
    dst = GfxRgba8(255u, 255u, 255u, 255u);
    out = GfxCompositeSrcOver(src, dst);
    ExpectColor(out, 255u, 127u, 127u, 255u);

    /* identical translucent colors */
    src = GfxRgba8(80u, 80u, 80u, 200u);
    dst = src;
    out = GfxCompositeSrcOver(src, dst);
    RoboCheckEqInt(out.a, (int)(200u + GfxMulDiv255(200u, 55u)));
}

static void TestCompOpacity(void)
{
    GfxColorRgba8 src = GfxRgba8(255u, 0u, 0u, 255u);
    GfxColorRgba8 dst = GfxRgba8(0u, 0u, 255u, 255u);
    GfxColorRgba8 out;

    out = GfxCompositeSrcOverOpacity(src, dst, 0u);
    RoboCheck(ColorEq(out, dst));

    out = GfxCompositeSrcOverOpacity(src, dst, 255u);
    RoboCheck(ColorEq(out, src));

    /* 50% global on opaque red over blue */
    out = GfxCompositeSrcOverOpacity(src, dst, 128u);
    ExpectColor(out, 128u, 0u, 127u, 255u);

    /* translucent source × global */
    src = GfxRgba8(255u, 255u, 0u, 128u);
    dst = GfxRgba8(0u, 0u, 0u, 255u);
    out = GfxCompositeSrcOverOpacity(src, dst, 128u);
    /* sa = mul(128,128)=64 */
    RoboCheckEqInt(out.a, 255);
    RoboCheck(out.r > 0u);
    RoboCheck(out.g > 0u);
}

static void TestCompPixelSurface(void)
{
    GfxSurface *s = NULL;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(4, 4, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxSurfaceClear(s, GfxRgb8(0u, 0u, 255u)),
                      RoboOk);
    RoboCheckEqInt(
        GfxSurfaceCompositePixel(s, 1u, 1u,
                                         GfxRgba8(255u, 0u, 0u, 128u),
                                         255u),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 1u, &c), RoboOk);
    /* 50% red over opaque blue → (128, 0, 127, 255) */
    ExpectColor(c, 128u, 0u, 127u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 255u, 255u);

    RoboCheckEqInt(
        GfxSurfaceCompositePixel(s, 99u, 0u, c, 255u),
        RoboInvalidArgument);
    GfxSurfaceRelease(s);
}

static void TestCompFormats(void)
{
    static const GfxPixelFormat fmts[] = {
        RoboGfxFormatRgba8, RoboGfxFormatBgra8, RoboGfxFormatRgb8,
        RoboGfxFormatGray8, RoboGfxFormatAlpha8};
    size_t i;
    GfxSurface *dst = NULL;
    GfxSurface *src = NULL;
    GfxColorRgba8 c;

    for (i = 0u; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
        RoboCheckEqInt(MakeSurface(3, 3, fmts[i], &dst), RoboOk);
        RoboCheckEqInt(MakeSurface(2, 2, RoboGfxFormatRgba8, &src),
                          RoboOk);
        if (fmts[i] == RoboGfxFormatAlpha8) {
            RoboCheckEqInt(
                GfxSurfaceClear(dst, GfxRgba8(0u, 0u, 0u, 0u)),
                RoboOk);
        } else {
            RoboCheckEqInt(
                GfxSurfaceClear(dst, GfxRgba8(0u, 0u, 0u, 255u)),
                RoboOk);
        }
        RoboCheckEqInt(
            GfxSurfaceClear(src, GfxRgba8(255u, 0u, 0u, 128u)),
            RoboOk);
        RoboCheckEqInt(
            GfxSurfaceComposite(src, dst, MakeGfxPoint(0, 0), 255u),
            RoboOk);
        RoboCheckEqInt(GfxSurfaceGetPixel(dst, 0u, 0u, &c), RoboOk);
        if (fmts[i] == RoboGfxFormatAlpha8) {
            /* RGBA src unpacks; pack to ALPHA8 keeps result A (128 over 0). */
            RoboCheckEqInt(c.a, 128);
        } else if (fmts[i] == RoboGfxFormatGray8) {
            RoboCheckEqInt(c.a, 255);
        } else if (fmts[i] == RoboGfxFormatRgb8) {
            RoboCheckEqInt(c.a, 255);
            RoboCheck(c.r > 0u);
        } else {
            /* 50% red over opaque black → (128,0,0,255) */
            RoboCheckEqInt(c.a, 255);
            RoboCheckEqInt(c.r, 128);
            RoboCheckEqInt(c.g, 0);
            RoboCheckEqInt(c.b, 0);
        }
        GfxSurfaceRelease(src);
        GfxSurfaceRelease(dst);
        src = NULL;
        dst = NULL;
    }
}

static void TestCompBounds(void)
{
    GfxSurface *dst = NULL;
    GfxSurface *src = NULL;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(8, 8, RoboGfxFormatRgba8, &dst), RoboOk);
    RoboCheckEqInt(MakeSurface(4, 4, RoboGfxFormatRgba8, &src), RoboOk);
    RoboCheckEqInt(GfxSurfaceClear(dst, GfxRgb8(0u, 255u, 0u)),
                      RoboOk);
    RoboCheckEqInt(
        GfxSurfaceClear(src, GfxRgba8(255u, 0u, 0u, 255u)),
        RoboOk);

    /* Fully outside */
    RoboCheckEqInt(
        GfxSurfaceComposite(src, dst, MakeGfxPoint(100, 100), 255u),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(dst, 0u, 0u, &c), RoboOk);
    ExpectColor(c, 0u, 255u, 0u, 255u);

    /* Negative origin — partial clip */
    RoboCheckEqInt(
        GfxSurfaceComposite(src, dst, MakeGfxPoint(-2, -2), 255u),
        RoboOk);
    /* Source (2,2) lands at dest (0,0) */
    RoboCheckEqInt(GfxSurfaceGetPixel(dst, 0u, 0u, &c), RoboOk);
    ExpectColor(c, 255u, 0u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(dst, 7u, 7u, &c), RoboOk);
    ExpectColor(c, 0u, 255u, 0u, 255u);

    /* Zero-sized */
    RoboCheckEqInt(
        GfxSurfaceCompositeRect(src, MakeGfxRect(0, 0, 0, 4), dst,
                                        MakeGfxPoint(0, 0), 255u),
        RoboOk);

    GfxSurfaceRelease(src);
    GfxSurfaceRelease(dst);
}

static void TestCompRectBlit(void)
{
    GfxSurface *dst = NULL;
    GfxSurface *src = NULL;
    GfxColorRgba8 c;
    uint32_t x;

    RoboCheckEqInt(MakeSurface(10, 6, RoboGfxFormatRgba8, &dst),
                      RoboOk);
    RoboCheckEqInt(MakeSurface(5, 3, RoboGfxFormatRgba8, &src), RoboOk);
    RoboCheckEqInt(GfxSurfaceClear(dst, GfxRgb8(255u, 255u, 255u)),
                      RoboOk);
    RoboCheckEqInt(
        GfxSurfaceClear(src, GfxRgba8(0u, 0u, 255u, 128u)),
        RoboOk);

    RoboCheckEqInt(
        GfxSurfaceCompositeRect(src, MakeGfxRect(0, 0, 5, 1), dst,
                                        MakeGfxPoint(2, 2), 255u),
        RoboOk);
    for (x = 2u; x < 7u; x++) {
        RoboCheckEqInt(GfxSurfaceGetPixel(dst, x, 2u, &c), RoboOk);
        ExpectColor(c, 127u, 127u, 255u, 255u);
    }
    RoboCheckEqInt(GfxSurfaceGetPixel(dst, 1u, 2u, &c), RoboOk);
    ExpectColor(c, 255u, 255u, 255u, 255u);

    /* Vertical span onto transparent destination */
    RoboCheckEqInt(
        GfxSurfaceClear(dst, GfxRgba8(0u, 0u, 0u, 0u)), RoboOk);
    RoboCheckEqInt(
        GfxSurfaceCompositeRect(src, MakeGfxRect(0, 0, 1, 3), dst,
                                        MakeGfxPoint(0, 0), 255u),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(dst, 0u, 0u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 255u, 128u);
    RoboCheckEqInt(GfxSurfaceGetPixel(dst, 0u, 2u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 255u, 128u);

    GfxSurfaceRelease(src);
    GfxSurfaceRelease(dst);
}

static void TestCompSelf(void)
{
    GfxSurface *s = NULL;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(4, 4, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxSurfaceClear(s, GfxRgb8(0u, 0u, 255u)),
                      RoboOk);
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, MakeGfxRect(0, 0, 2, 2),
                                   GfxRgba8(255u, 0u, 0u, 128u)),
        RoboOk);

    /* Overlapping self-composite: red square over itself shifted */
    RoboCheckEqInt(
        GfxSurfaceCompositeRect(s, MakeGfxRect(0, 0, 2, 2), s,
                                        MakeGfxPoint(1, 1), 255u),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 1u, &c), RoboOk);
    /* Must have blended, not garbage */
    RoboCheck(c.a > 0u);
    RoboCheck(c.r > 0u);

    /* Non-overlapping regions on same surface */
    RoboCheckEqInt(GfxSurfaceClear(s, GfxRgb8(0u, 255u, 0u)),
                      RoboOk);
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, MakeGfxRect(0, 0, 1, 1),
                                   GfxRgba8(255u, 0u, 0u, 255u)),
        RoboOk);
    RoboCheckEqInt(
        GfxSurfaceCompositeRect(s, MakeGfxRect(0, 0, 1, 1), s,
                                        MakeGfxPoint(3, 3), 255u),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 3u, 3u, &c), RoboOk);
    ExpectColor(c, 255u, 0u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    ExpectColor(c, 255u, 0u, 0u, 255u);

    GfxSurfaceRelease(s);
}

static void TestCompTranslucentOverTranslucent(void)
{
    GfxColorRgba8 src = GfxRgba8(255u, 0u, 0u, 128u);
    GfxColorRgba8 dst = GfxRgba8(0u, 0u, 255u, 128u);
    GfxColorRgba8 out = GfxCompositeSrcOver(src, dst);

    /* Both a=128 → outA = 128 + mul(128,127) = 128+64 = 192 */
    RoboCheckEqInt(out.a, 192);
    RoboCheck(out.r > out.b);
}

static void TestCompSmoke(void)
{
    GfxSurface *dst = NULL;
    GfxSurface *src = NULL;
    uint32_t y;
    uint32_t x;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(64, 64, RoboGfxFormatRgba8, &dst),
                      RoboOk);
    RoboCheckEqInt(MakeSurface(32, 32, RoboGfxFormatBgra8, &src),
                      RoboOk);
    RoboCheckEqInt(GfxSurfaceClear(dst, GfxRgb8(255u, 255u, 255u)),
                      RoboOk);
    RoboCheckEqInt(
        GfxSurfaceClear(src, GfxRgba8(0u, 128u, 255u, 64u)),
        RoboOk);
    RoboCheckEqInt(
        GfxSurfaceComposite(src, dst, MakeGfxPoint(16, 16), 200u),
        RoboOk);
    for (y = 16u; y < 48u; y += 8u) {
        for (x = 16u; x < 48u; x += 8u) {
            RoboCheckEqInt(GfxSurfaceGetPixel(dst, x, y, &c),
                              RoboOk);
            RoboCheckEqInt(c.a, 255);
        }
    }
    GfxSurfaceRelease(src);
    GfxSurfaceRelease(dst);
}

static void TestCompErrors(void)
{
    GfxSurface *s = NULL;

    RoboCheckEqInt(
        GfxSurfaceCompositeRect(NULL, MakeGfxRect(0, 0, 1, 1), NULL,
                                        MakeGfxPoint(0, 0), 255u),
        RoboInvalidArgument);
    RoboCheckEqInt(MakeSurface(2, 2, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(
        GfxSurfaceCompositeRect(s, MakeGfxRect(0, 0, -1, 1), s,
                                        MakeGfxPoint(0, 0), 255u),
        RoboInvalidArgument);
    GfxSurfaceRelease(s);
}

void TestRegisterGraphicsComposite(void)
{
    TestRegister("graphics.comp.mul", TestCompMulDiv255);
    TestRegister("graphics.comp.premul", TestCompPremulRoundtrip);
    TestRegister("graphics.comp.src_over", TestCompSrcOverBasics);
    TestRegister("graphics.comp.opacity", TestCompOpacity);
    TestRegister("graphics.comp.pixel", TestCompPixelSurface);
    TestRegister("graphics.comp.formats", TestCompFormats);
    TestRegister("graphics.comp.bounds", TestCompBounds);
    TestRegister("graphics.comp.rect", TestCompRectBlit);
    TestRegister("graphics.comp.self", TestCompSelf);
    TestRegister("graphics.comp.translucent",
                       TestCompTranslucentOverTranslucent);
    TestRegister("graphics.comp.smoke", TestCompSmoke);
    TestRegister("graphics.comp.errors", TestCompErrors);
}
