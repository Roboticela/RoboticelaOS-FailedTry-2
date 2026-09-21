/*
 * RoboGraphics Goal 6.11 — CPU raster backend.
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

static void TestRasterInit(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxRect clip;
    GfxSurfaceDesc desc;

    RoboCheckEqInt(GfxRasterCreate(NULL, &r), RoboInvalidArgument);
    RoboCheckEqInt(GfxRasterCreate(NULL, NULL), RoboInvalidArgument);

    RoboCheckEqInt(MakeSurface(8, 8, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
    RoboCheck(GfxRasterTarget(r) == s);
    RoboCheckEqInt(GfxRasterOpacity(r), 255);
    RoboCheckEqInt(GfxRasterClip(r, &clip), RoboOk);
    RoboCheckEqInt(clip.x, 0);
    RoboCheckEqInt(clip.y, 0);
    RoboCheckEqInt(clip.width, 8);
    RoboCheckEqInt(clip.height, 8);
    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);

    /* zero-sized surfaces are rejected by framebuffer create */
    RoboCheckEqInt(MakeSurface(0, 0, RoboGfxFormatRgba8, &s),
                      RoboInvalidArgument);
    RoboCheck(s == NULL);

    /* read-only surface rejected */
    GfxSurfaceDescInit(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessRead;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboInvalidArgument);
    GfxSurfaceRelease(s);
}

static void TestRasterFormats(void)
{
    static const GfxPixelFormat fmts[] = {
        RoboGfxFormatRgba8, RoboGfxFormatBgra8, RoboGfxFormatRgb8,
        RoboGfxFormatGray8, RoboGfxFormatAlpha8};
    size_t i;
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxColorRgba8 c;

    for (i = 0u; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
        RoboCheckEqInt(MakeSurface(3, 3, fmts[i], &s), RoboOk);
        RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
        RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(10, 20, 30)),
                          RoboOk);
        RoboCheckEqInt(
            GfxRasterDrawPixel(r, MakeGfxPoint(1, 1),
                                       GfxRgb8(200, 100, 50)),
            RoboOk);
        RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 1u, &c), RoboOk);
        RoboCheckEqInt(c.a, 255);
        GfxRasterDestroy(r);
        GfxSurfaceRelease(s);
    }
}

static void TestRasterPixel(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(4, 4, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 255)),
                      RoboOk);

    RoboCheckEqInt(
        GfxRasterDrawPixel(r, MakeGfxPoint(1, 2),
                                   GfxRgb8(255, 0, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 2u, &c), RoboOk);
    ExpectColor(c, 255u, 0u, 0u, 255u);

    /* outside / negative → no-op */
    RoboCheckEqInt(
        GfxRasterDrawPixel(r, MakeGfxPoint(-1, 0),
                                   GfxRgb8(1, 1, 1)),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterDrawPixel(r, MakeGfxPoint(99, 0),
                                   GfxRgb8(1, 1, 1)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 255u, 255u);

    /* translucent */
    RoboCheckEqInt(
        GfxRasterDrawPixel(r, MakeGfxPoint(0, 0),
                                   GfxRgba8(255, 0, 0, 128)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    ExpectColor(c, 128u, 0u, 127u, 255u);

    /* transparent source */
    RoboCheckEqInt(
        GfxRasterDrawPixel(r, MakeGfxPoint(3, 3),
                                   GfxRgba8(255, 255, 0, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 3u, 3u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 255u, 255u);

    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);
}

static void TestRasterRect(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(8, 8, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 0)), RoboOk);

    /* 1×1 */
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(2, 3, 1, 1),
                                  GfxRgb8(10, 20, 30)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 2u, 3u, &c), RoboOk);
    ExpectColor(c, 10u, 20u, 30u, 255u);

    /* horizontal / vertical */
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(0, 0, 5, 1),
                                  GfxRgb8(255, 0, 0)),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(0, 0, 1, 5),
                                  GfxRgb8(0, 255, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    ExpectColor(c, 0u, 255u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 4u, 0u, &c), RoboOk);
    ExpectColor(c, 255u, 0u, 0u, 255u);

    /* empty / outside / negative */
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(0, 0, 0, 4),
                                  GfxRgb8(1, 1, 1)),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(-10, -10, 4, 4),
                                  GfxRgb8(9, 9, 9)),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(100, 100, 2, 2),
                                  GfxRgb8(9, 9, 9)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 1u, &c), RoboOk);
    ExpectColor(c, 0u, 255u, 0u, 255u);

    /* partial clip via negative origin */
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 0)), RoboOk);
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(-2, -2, 4, 4),
                                  GfxRgb8(128, 0, 128)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    ExpectColor(c, 128u, 0u, 128u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 1u, &c), RoboOk);
    ExpectColor(c, 128u, 0u, 128u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 2u, 2u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 0u, 255u);

    /* invalid rect */
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(0, 0, -1, 1),
                                  GfxRgb8(1, 1, 1)),
        RoboInvalidArgument);

    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);
}

static void TestRasterClip(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxColorRgba8 c;
    GfxRect clip;

    RoboCheckEqInt(MakeSurface(8, 8, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 0)), RoboOk);

    RoboCheckEqInt(GfxRasterSetClip(r, MakeGfxRect(2, 2, 3, 3)),
                      RoboOk);
    RoboCheckEqInt(GfxRasterClip(r, &clip), RoboOk);
    RoboCheckEqInt(clip.x, 2);
    RoboCheckEqInt(clip.width, 3);

    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(0, 0, 8, 8),
                                  GfxRgb8(255, 0, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 1u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 2u, 2u, &c), RoboOk);
    ExpectColor(c, 255u, 0u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 4u, 4u, &c), RoboOk);
    ExpectColor(c, 255u, 0u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 5u, 5u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 0u, 255u);

    RoboCheckEqInt(GfxRasterIntersectClip(r, MakeGfxRect(3, 3, 4, 4)),
                      RoboOk);
    RoboCheckEqInt(GfxRasterClip(r, &clip), RoboOk);
    RoboCheckEqInt(clip.x, 3);
    RoboCheckEqInt(clip.y, 3);
    RoboCheckEqInt(clip.width, 2);
    RoboCheckEqInt(clip.height, 2);

    RoboCheckEqInt(GfxRasterResetClip(r), RoboOk);
    RoboCheckEqInt(GfxRasterSetClip(r, MakeGfxRect(-5, -5, 2, 2)),
                      RoboOk);
    RoboCheckEqInt(GfxRasterClip(r, &clip), RoboOk);
    RoboCheck(GfxRectIsEmpty(clip));

    RoboCheckEqInt(GfxRasterSetClip(r, MakeGfxRect(0, 0, -1, 1)),
                      RoboInvalidArgument);

    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);
}

static void TestRasterLine(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(8, 8, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 0)), RoboOk);

    /* horizontal */
    RoboCheckEqInt(
        GfxRasterDrawLine(r, MakeGfxPoint(1, 1), MakeGfxPoint(5, 1),
                                  GfxRgb8(255, 0, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 1u, &c), RoboOk);
    ExpectColor(c, 255u, 0u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 5u, 1u, &c), RoboOk);
    ExpectColor(c, 255u, 0u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 6u, 1u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 0u, 255u);

    /* vertical + reversed */
    RoboCheckEqInt(
        GfxRasterDrawLine(r, MakeGfxPoint(3, 6), MakeGfxPoint(3, 2),
                                  GfxRgb8(0, 255, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 3u, 2u, &c), RoboOk);
    ExpectColor(c, 0u, 255u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 3u, 6u, &c), RoboOk);
    ExpectColor(c, 0u, 255u, 0u, 255u);

    /* diagonal */
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 0)), RoboOk);
    RoboCheckEqInt(
        GfxRasterDrawLine(r, MakeGfxPoint(0, 0), MakeGfxPoint(3, 3),
                                  GfxRgb8(0, 0, 255)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 2u, 2u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 255u, 255u);

    /* clipped by raster clip */
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 0)), RoboOk);
    RoboCheckEqInt(GfxRasterSetClip(r, MakeGfxRect(2, 0, 2, 8)),
                      RoboOk);
    RoboCheckEqInt(
        GfxRasterDrawLine(r, MakeGfxPoint(0, 4), MakeGfxPoint(7, 4),
                                  GfxRgb8(255, 255, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 4u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 2u, 4u, &c), RoboOk);
    ExpectColor(c, 255u, 255u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 3u, 4u, &c), RoboOk);
    ExpectColor(c, 255u, 255u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 4u, 4u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 0u, 255u);

    /* fully outside */
    RoboCheckEqInt(
        GfxRasterDrawLine(r, MakeGfxPoint(-20, -20),
                                  MakeGfxPoint(-10, -10),
                                  GfxRgb8(1, 1, 1)),
        RoboOk);

    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);
}

static void TestRasterOpacityComposite(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(4, 4, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 255)),
                      RoboOk);

    GfxRasterSetOpacity(r, 0u);
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(0, 0, 4, 4),
                                  GfxRgb8(255, 0, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 255u, 255u);

    GfxRasterSetOpacity(r, 128u);
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(0, 0, 2, 2),
                                  GfxRgb8(255, 0, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    ExpectColor(c, 128u, 0u, 127u, 255u);

    GfxRasterSetOpacity(r, 255u);
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 0)), RoboOk);
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(0, 0, 1, 1),
                                  GfxRgba8(0, 0, 255, 128)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    /* 50% blue over opaque black → (0,0,128,255) */
    ExpectColor(c, 0u, 0u, 128u, 255u);

    /* translucent over transparent destination */
    RoboCheckEqInt(GfxSurfaceSetPixel(s, 1u, 1u,
                                                 GfxRgba8(0, 0, 0, 0)),
                      RoboOk);
    RoboCheckEqInt(
        GfxRasterDrawPixel(r, MakeGfxPoint(1, 1),
                                   GfxRgba8(0, 0, 255, 128)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 1u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 255u, 128u);

    /* translucent over translucent */
    RoboCheckEqInt(
        GfxRasterDrawPixel(r, MakeGfxPoint(0, 0),
                                   GfxRgba8(255, 0, 0, 128)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    RoboCheck(c.a > 128u);
    RoboCheck(c.r > 0u);

    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);
}

static void TestRasterOrdering(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(4, 4, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 0)), RoboOk);
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(0, 0, 3, 3),
                                  GfxRgb8(255, 0, 0)),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(1, 1, 2, 2),
                                  GfxRgb8(0, 255, 0)),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterDrawPixel(r, MakeGfxPoint(1, 1),
                                   GfxRgb8(0, 0, 255)),
        RoboOk);

    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    ExpectColor(c, 255u, 0u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 2u, 2u, &c), RoboOk);
    ExpectColor(c, 0u, 255u, 0u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 1u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 255u, 255u);

    /* repeat → deterministic */
    RoboCheckEqInt(
        GfxRasterDrawPixel(r, MakeGfxPoint(1, 1),
                                   GfxRgb8(0, 0, 255)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 1u, &c), RoboOk);
    ExpectColor(c, 0u, 0u, 255u, 255u);

    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);
}

static void TestRasterPathBoundary(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxPath *path = NULL;

    RoboCheckEqInt(MakeSurface(4, 4, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(0, 0)), RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(3, 3)), RoboOk);

    RoboCheckEqInt(
        GfxRasterFillPath(r, path, GfxRgb8(1, 2, 3)),
        RoboNotSupported);
    RoboCheckEqInt(
        GfxRasterStrokePath(r, path, GfxRgb8(1, 2, 3)),
        RoboNotSupported);
    RoboCheckEqInt(GfxRasterFillPath(r, NULL, GfxRgb8(1, 2, 3)),
                      RoboInvalidArgument);

    GfxPathRelease(path);
    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);
}

static void TestRasterNumeric(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(16, 16, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 0)), RoboOk);

    RoboCheckEqInt(
        GfxRasterDrawLine(r, MakeGfxPoint(-1000, 8),
                                  MakeGfxPoint(1000, 8),
                                  GfxRgb8(255, 255, 255)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 8u, &c), RoboOk);
    ExpectColor(c, 255u, 255u, 255u, 255u);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 15u, 8u, &c), RoboOk);
    ExpectColor(c, 255u, 255u, 255u, 255u);

    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(-100, -100, 50, 50),
                                  GfxRgb8(1, 2, 3)),
        RoboOk);

    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);
}

static void TestRasterSmoke(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    uint32_t y;
    uint32_t x;
    GfxColorRgba8 c;

    RoboCheckEqInt(MakeSurface(64, 64, RoboGfxFormatBgra8, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(32, 32, 32)),
                      RoboOk);
    GfxRasterSetOpacity(r, 200u);
    RoboCheckEqInt(
        GfxRasterFillRect(r, MakeGfxRect(8, 8, 48, 48),
                                  GfxRgba8(0, 128, 255, 180)),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterDrawLine(r, MakeGfxPoint(0, 0), MakeGfxPoint(63, 63),
                                  GfxRgb8(255, 255, 255)),
        RoboOk);
    for (y = 0u; y < 64u; y += 16u) {
        for (x = 0u; x < 64u; x += 16u) {
            RoboCheckEqInt(GfxSurfaceGetPixel(s, x, y, &c), RoboOk);
            RoboCheckEqInt(c.a, 255);
        }
    }
    RoboCheckEqInt(GfxPresentNull(s), RoboOk);
    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);
}

static void TestRasterErrors(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;

    RoboCheckEqInt(GfxRasterFillRect(NULL, MakeGfxRect(0, 0, 1, 1),
                                                GfxRgb8(1, 1, 1)),
                      RoboInvalidArgument);
    RoboCheckEqInt(MakeSurface(2, 2, RoboGfxFormatRgba8, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
    RoboCheckEqInt(GfxRasterClip(r, NULL), RoboInvalidArgument);
    GfxRasterDestroy(r);
    GfxRasterDestroy(NULL);
    GfxSurfaceRelease(s);
}

void TestRegisterGraphicsRaster(void)
{
    TestRegister("graphics.raster.init", TestRasterInit);
    TestRegister("graphics.raster.formats", TestRasterFormats);
    TestRegister("graphics.raster.pixel", TestRasterPixel);
    TestRegister("graphics.raster.rect", TestRasterRect);
    TestRegister("graphics.raster.clip", TestRasterClip);
    TestRegister("graphics.raster.line", TestRasterLine);
    TestRegister("graphics.raster.opacity", TestRasterOpacityComposite);
    TestRegister("graphics.raster.order", TestRasterOrdering);
    TestRegister("graphics.raster.path", TestRasterPathBoundary);
    TestRegister("graphics.raster.numeric", TestRasterNumeric);
    TestRegister("graphics.raster.smoke", TestRasterSmoke);
    TestRegister("graphics.raster.errors", TestRasterErrors);
}
