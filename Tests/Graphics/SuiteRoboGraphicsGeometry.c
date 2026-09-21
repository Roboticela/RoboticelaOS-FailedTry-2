/*
 * RoboGraphics Goal 6.6 — pixels, points, sizes, rectangles, clipping.
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <stdint.h>

#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif
#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif

static void TestGeomPoint(void)
{
    GfxPoint a = MakeGfxPoint(3, -4);
    GfxPoint b = MakeGfxPoint(5, 6);
    GfxPoint out;

    RoboCheck(GfxPointEqual(a, MakeGfxPoint(3, -4)));
    RoboCheck(!GfxPointEqual(a, b));

    RoboCheckEqInt(GfxPointAdd(a, b, &out), RoboOk);
    RoboCheckEqInt(out.x, 8);
    RoboCheckEqInt(out.y, 2);

    RoboCheckEqInt(GfxPointSub(b, a, &out), RoboOk);
    RoboCheckEqInt(out.x, 2);
    RoboCheckEqInt(out.y, 10);

    RoboCheckEqInt(GfxPointTranslate(a, -1, 4, &out), RoboOk);
    RoboCheckEqInt(out.x, 2);
    RoboCheckEqInt(out.y, 0);

    RoboCheckEqInt(GfxPointAdd(MakeGfxPoint(INT32_MAX, 0),
                                         MakeGfxPoint(1, 0), &out),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxPointSub(MakeGfxPoint(INT32_MIN, 0),
                                         MakeGfxPoint(1, 0), &out),
                      RoboInvalidArgument);
}

static void TestGeomSize(void)
{
    uint64_t area = 99u;
    GfxSize s = MakeGfxSize(4, 5);

    RoboCheck(GfxSizeEqual(s, MakeGfxSize(4, 5)));
    RoboCheck(!GfxSizeIsEmpty(s));
    RoboCheck(GfxSizeIsValid(s));
    RoboCheck(GfxSizeIsEmpty(MakeGfxSize(0, 5)));
    RoboCheck(GfxSizeIsEmpty(MakeGfxSize(3, -1)));
    RoboCheck(!GfxSizeIsValid(MakeGfxSize(-1, 2)));

    RoboCheckEqInt(GfxSizeArea(s, &area), RoboOk);
    RoboCheckEqInt((int)area, 20);
    RoboCheckEqInt(GfxSizeArea(MakeGfxSize(0, 10), &area), RoboOk);
    RoboCheckEqInt((int)area, 0);
    RoboCheckEqInt(GfxSizeArea(MakeGfxSize(-1, 2), &area),
                      RoboInvalidArgument);
}

static void TestGeomRectConstruct(void)
{
    GfxRect r;
    GfxRect e = GfxRectEmpty();

    RoboCheck(GfxRectIsEmpty(e));
    RoboCheckEqInt(GfxRectFromPointSize(MakeGfxPoint(-20, -10),
                                                    MakeGfxSize(100, 80), &r),
                      RoboOk);
    RoboCheckEqInt(r.x, -20);
    RoboCheckEqInt(r.y, -10);
    RoboCheckEqInt(r.width, 100);
    RoboCheckEqInt(r.height, 80);

    RoboCheckEqInt(GfxRectFromLtrb(10, 20, 110, 70, &r), RoboOk);
    RoboCheckEqInt(r.x, 10);
    RoboCheckEqInt(r.y, 20);
    RoboCheckEqInt(r.width, 100);
    RoboCheckEqInt(r.height, 50);

    RoboCheckEqInt(GfxRectFromLtrb(5, 5, 5, 10, &r), RoboOk);
    RoboCheck(GfxRectIsEmpty(r));

    RoboCheckEqInt(GfxRectFromLtrb(10, 20, 5, 70, &r),
                      RoboInvalidArgument);
    RoboCheckEqInt(
        GfxRectFromPointSize(MakeGfxPoint(0, 0),
                                      MakeGfxSize(-1, 1), &r),
        RoboInvalidArgument);

    r = MakeGfxRect(0, 0, 10, 10);
    RoboCheckEqInt(GfxRectLeft(r), 0);
    RoboCheckEqInt(GfxRectTop(r), 0);
    RoboCheckEqInt(GfxRectWidth(r), 10);
    RoboCheckEqInt(GfxRectHeight(r), 10);
    RoboCheckEqInt(GfxRectRight(r), 10);
    RoboCheckEqInt(GfxRectBottom(r), 10);
}

static void TestGeomContains(void)
{
    GfxRect r = MakeGfxRect(10, 20, 100, 50);

    RoboCheck(GfxRectContainsPoint(r, MakeGfxPoint(10, 20)));
    RoboCheck(GfxRectContainsPoint(r, MakeGfxPoint(109, 69)));
    RoboCheck(!GfxRectContainsPoint(r, MakeGfxPoint(110, 20)));
    RoboCheck(!GfxRectContainsPoint(r, MakeGfxPoint(10, 70)));
    RoboCheck(!GfxRectContainsPoint(r, MakeGfxPoint(9, 20)));
    RoboCheck(!GfxRectContainsPoint(GfxRectEmpty(),
                                             MakeGfxPoint(0, 0)));

    RoboCheck(GfxRectEncloses(r, MakeGfxRect(10, 20, 1, 1)));
    RoboCheck(GfxRectEncloses(r, MakeGfxRect(10, 20, 100, 50)));
    RoboCheck(!GfxRectEncloses(r, MakeGfxRect(10, 20, 101, 50)));
    RoboCheck(GfxRectEncloses(r, GfxRectEmpty()));
}

static void TestGeomIntersectUnion(void)
{
    GfxRect out;
    GfxRect a = MakeGfxRect(0, 0, 10, 10);
    GfxRect b = MakeGfxRect(5, 5, 10, 10);
    GfxRect c = MakeGfxRect(20, 20, 5, 5);
    GfxRect d = MakeGfxRect(10, 0, 5, 5); /* edge-touching a */
    GfxRect neg = MakeGfxRect(-20, -10, 30, 25);

    RoboCheckEqInt(GfxRectIntersect(a, b, &out), RoboOk);
    RoboCheck(GfxRectEqual(out, MakeGfxRect(5, 5, 5, 5)));

    RoboCheckEqInt(GfxRectIntersect(a, a, &out), RoboOk);
    RoboCheck(GfxRectEqual(out, a));

    RoboCheckEqInt(GfxRectIntersect(a, c, &out), RoboOk);
    RoboCheck(GfxRectIsEmpty(out));
    RoboCheck(!GfxRectIntersects(a, c));

    RoboCheckEqInt(GfxRectIntersect(a, d, &out), RoboOk);
    RoboCheck(GfxRectIsEmpty(out)); /* half-open: touching ≠ overlap */

    RoboCheckEqInt(GfxRectIntersect(a, neg, &out), RoboOk);
    RoboCheck(GfxRectEqual(out, MakeGfxRect(0, 0, 10, 10)));

    RoboCheckEqInt(GfxRectBounding(a, b, &out), RoboOk);
    RoboCheck(GfxRectEqual(out, MakeGfxRect(0, 0, 15, 15)));

    RoboCheckEqInt(GfxRectBounding(a, GfxRectEmpty(), &out),
                      RoboOk);
    RoboCheck(GfxRectEqual(out, a));

    RoboCheckEqInt(
        GfxRectBounding(GfxRectEmpty(), GfxRectEmpty(),
                               &out),
        RoboOk);
    RoboCheck(GfxRectIsEmpty(out));

    RoboCheckEqInt(GfxRectBounding(neg, a, &out), RoboOk);
    RoboCheck(GfxRectEqual(out, MakeGfxRect(-20, -10, 30, 25)));
}

static void TestGeomTranslateClipInset(void)
{
    GfxRect out;
    GfxRect r = MakeGfxRect(-20, -10, 100, 80);
    GfxRect bounds = MakeGfxRect(0, 0, 50, 40);

    RoboCheckEqInt(GfxRectTranslate(r, 5, -3, &out), RoboOk);
    RoboCheck(GfxRectEqual(out, MakeGfxRect(-15, -13, 100, 80)));

    RoboCheckEqInt(GfxRectTranslate(MakeGfxRect(INT32_MAX - 1, 0, 1, 1),
                                              2, 0, &out),
                      RoboInvalidArgument);

    RoboCheckEqInt(GfxRectClip(r, bounds, &out), RoboOk);
    RoboCheck(GfxRectEqual(out, MakeGfxRect(0, 0, 50, 40)));

    RoboCheckEqInt(
        GfxRectClip(MakeGfxRect(5, 5, 10, 10), bounds, &out), RoboOk);
    RoboCheck(GfxRectEqual(out, MakeGfxRect(5, 5, 10, 10)));

    RoboCheckEqInt(
        GfxRectClip(MakeGfxRect(100, 100, 10, 10), bounds, &out),
        RoboOk);
    RoboCheck(GfxRectIsEmpty(out));

    RoboCheckEqInt(
        GfxRectClip(MakeGfxRect(-10, -10, 200, 200), bounds, &out),
        RoboOk);
    RoboCheck(GfxRectEqual(out, bounds));

    RoboCheckEqInt(GfxRectInset(MakeGfxRect(0, 0, 10, 10), 2, &out),
                      RoboOk);
    RoboCheck(GfxRectEqual(out, MakeGfxRect(2, 2, 6, 6)));

    RoboCheckEqInt(GfxRectInset(MakeGfxRect(0, 0, 4, 4), 3, &out),
                      RoboOk);
    RoboCheck(GfxRectIsEmpty(out));

    RoboCheckEqInt(GfxRectOutset(MakeGfxRect(2, 2, 4, 4), 2, &out),
                      RoboOk);
    RoboCheck(GfxRectEqual(out, MakeGfxRect(0, 0, 8, 8)));
}

static void TestGeomOverflow(void)
{
    GfxRect out;
    int32_t edge = 0;

    RoboCheckEqInt(
        GfxRectRightChecked(MakeGfxRect(INT32_MAX - 1, 0, 2, 1), &edge),
        RoboInvalidArgument);
    RoboCheckEqInt(
        GfxRectBottomChecked(MakeGfxRect(0, INT32_MAX - 1, 1, 2),
                                     &edge),
        RoboInvalidArgument);

    /* Saturating getters never wrap. */
    RoboCheckEqInt(GfxRectRight(MakeGfxRect(INT32_MAX - 1, 0, 2, 1)),
                      INT32_MAX);

    RoboCheckEqInt(
        GfxRectFromLtrb(INT32_MIN, 0, INT32_MAX, 1, &out),
        RoboInvalidArgument); /* width does not fit int32 */
}

static Result MakeSurface(uint32_t w,
                                  uint32_t h,
                                  GfxSurface **out)
{
    GfxSurfaceDesc desc;
    GfxSurfaceDescInit(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    return GfxSurfaceCreate(&desc, out);
}

static void TestGeomBitmapIntegration(void)
{
    /* 4x4 labeled pattern A..P as distinct colors (r = index+1). */
    static const uint8_t expect[4][4] = {
        {1, 2, 3, 4},
        {5, 6, 7, 8},
        {9, 10, 11, 12},
        {13, 14, 15, 16}};
    GfxSurface *s = NULL;
    GfxRect bounds;
    GfxRect clipped;
    GfxColorRgba8 c;
    uint32_t x;
    uint32_t y;
    int idx;

    RoboCheckEqInt(MakeSurface(4, 4, &s), RoboOk);
    RoboCheckEqInt(GfxSurfacePixelBounds(s, &bounds), RoboOk);
    RoboCheck(GfxRectEqual(bounds, MakeGfxRect(0, 0, 4, 4)));

    idx = 1;
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            RoboCheckEqInt(
                GfxSurfaceSetPixelAt(
                    s, MakeGfxPoint((int32_t)x, (int32_t)y),
                    GfxRgba8((uint8_t)idx, 0, 0, 255)),
                RoboOk);
            idx++;
        }
    }

    /* Interior fill */
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, MakeGfxRect(1, 1, 2, 2),
                                   GfxRgba8(100, 0, 0, 255)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixelAt(s, MakeGfxPoint(1, 1), &c),
                      RoboOk);
    RoboCheckEqInt(c.r, 100);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 1);

    /* Edge rect */
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, MakeGfxRect(0, 3, 4, 1),
                                   GfxRgba8(200, 0, 0, 255)),
        RoboOk);

    /* Empty no-op */
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, MakeGfxRect(0, 0, 0, 4),
                                   GfxRgba8(1, 2, 3, 4)),
        RoboOk);

    /* Negative geometry rejected by fill without clip */
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, MakeGfxRect(-1, 0, 2, 2),
                                   GfxRgba8(1, 1, 1, 1)),
        RoboInvalidArgument);

    /* Clip negative/partial then fill */
    RoboCheckEqInt(
        GfxRectClip(MakeGfxRect(-2, -1, 4, 3), bounds, &clipped),
        RoboOk);
    RoboCheck(GfxRectEqual(clipped, MakeGfxRect(0, 0, 2, 2)));
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, clipped, GfxRgba8(50, 0, 0, 255)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 50);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 1u, 1u, &c), RoboOk);
    RoboCheckEqInt(c.r, 50);

    /* Completely outside clip → empty fill ok */
    RoboCheckEqInt(
        GfxRectClip(MakeGfxRect(10, 10, 2, 2), bounds, &clipped),
        RoboOk);
    RoboCheck(GfxRectIsEmpty(clipped));
    RoboCheckEqInt(
        GfxSurfaceFillRect(s, clipped, GfxRgba8(9, 9, 9, 9)),
        RoboOk);

    RoboCheckEqInt(
        GfxSurfaceGetPixelAt(s, MakeGfxPoint(-1, 0), &c),
        RoboInvalidArgument);

    (void)expect;
    GfxSurfaceRelease(s);
}

static void TestGeomPatternCopy(void)
{
    GfxSurface *src = NULL;
    GfxSurface *dst = NULL;
    GfxColorRgba8 c;
    uint32_t x;
    uint32_t y;
    uint8_t v;

    RoboCheckEqInt(MakeSurface(4, 4, &src), RoboOk);
    RoboCheckEqInt(MakeSurface(4, 4, &dst), RoboOk);

    v = 1u;
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            RoboCheckEqInt(
                GfxSurfaceSetPixel(src, x, y,
                                           GfxRgba8(v, 0, 0, 255)),
                RoboOk);
            v++;
        }
    }
    RoboCheckEqInt(GfxSurfaceClear(dst, GfxRgba8(0, 0, 0, 0)),
                      RoboOk);
    RoboCheckEqInt(
        GfxSurfaceCopyRect(src, MakeGfxRect(1, 1, 2, 2), dst,
                                   MakeGfxPoint(0, 0)),
        RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(dst, 0u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 6); /* B=2 → index 6 */
    RoboCheckEqInt(GfxSurfaceGetPixel(dst, 1u, 1u, &c), RoboOk);
    RoboCheckEqInt(c.r, 11);

    GfxSurfaceRelease(src);
    GfxSurfaceRelease(dst);
}

void TestRegisterGraphicsGeometry(void)
{
    TestRegister("graphics.geom.point", TestGeomPoint);
    TestRegister("graphics.geom.size", TestGeomSize);
    TestRegister("graphics.geom.construct", TestGeomRectConstruct);
    TestRegister("graphics.geom.contains", TestGeomContains);
    TestRegister("graphics.geom.intersect", TestGeomIntersectUnion);
    TestRegister("graphics.geom.clip", TestGeomTranslateClipInset);
    TestRegister("graphics.geom.overflow", TestGeomOverflow);
    TestRegister("graphics.geom.bmp", TestGeomBitmapIntegration);
    TestRegister("graphics.geom.pattern", TestGeomPatternCopy);
}
