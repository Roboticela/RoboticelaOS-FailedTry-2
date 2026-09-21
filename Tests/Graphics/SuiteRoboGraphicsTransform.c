/*
 * RoboGraphics Goal 6.9 — 2D affine transforms.
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <stdint.h>

static int NearlyEq(float a, float b, float eps)
{
    float d = a - b;
    if (d < 0.0f) {
        d = -d;
    }
    return d <= eps;
}

static void ExpectPointI(GfxPoint p, int32_t x, int32_t y)
{
    RoboCheckEqInt(p.x, x);
    RoboCheckEqInt(p.y, y);
}

static void ExpectPointF(GfxPointF p, float x, float y, float eps)
{
    RoboCheck(NearlyEq(p.x, x, eps));
    RoboCheck(NearlyEq(p.y, y, eps));
}

static void TestXformIdentity(void)
{
    GfxAffine id;
    GfxRect r, out;
    GfxPath *path = NULL;
    GfxPath *mapped = NULL;
    GfxPathCommand c;

    GfxAffineIdentity(&id);
    RoboCheck(GfxAffineIsIdentity(&id));
    RoboCheck(GfxAffineIsInvertible(&id));

    ExpectPointF(GfxAffineMapPointF(&id, MakeGfxPointF(3.5f, -2.0f)),
                   3.5f, -2.0f, 0.0f);
    ExpectPointI(GfxAffineMapPointI(&id, MakeGfxPoint(7, 9)), 7, 9);

    r = MakeGfxRect(2, 3, 4, 5);
    RoboCheckEqInt(GfxAffineMapRect(&id, r, &out), RoboOk);
    RoboCheck(GfxRectEqual(r, out));

    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(1, 2)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(3, 4)),
                      RoboOk);
    RoboCheckEqInt(GfxPathTransformed(path, &id, &mapped), RoboOk);
    RoboCheckEqInt((int)GfxPathCommandCount(mapped), 2);
    RoboCheckEqInt(GfxPathCommandAt(mapped, 0u, &c), RoboOk);
    ExpectPointI(c.point, 1, 2);
    RoboCheckEqInt(GfxPathCommandAt(path, 0u, &c), RoboOk);
    ExpectPointI(c.point, 1, 2); /* source unchanged */

    GfxPathRelease(mapped);
    GfxPathRelease(path);
}

static void TestXformTranslate(void)
{
    GfxAffine t;
    GfxAffine inv;
    GfxPointF p;

    GfxAffineTranslate(&t, 10.0f, -5.0f);
    p = GfxAffineMapPointF(&t, MakeGfxPointF(1.0f, 2.0f));
    ExpectPointF(p, 11.0f, -3.0f, 0.0f);

    GfxAffineTranslate(&t, 0.0f, 0.0f);
    RoboCheck(GfxAffineIsIdentity(&t));

    GfxAffineTranslate(&t, -100.0f, 200.0f);
    RoboCheckEqInt(GfxAffineInvert(&t, &inv), RoboOk);
    p = GfxAffineMapPointF(
        &inv, GfxAffineMapPointF(&t, MakeGfxPointF(4.0f, 6.0f)));
    ExpectPointF(p, 4.0f, 6.0f, 1.0e-5f);
}

static void TestXformScale(void)
{
    GfxAffine s;
    GfxAffine inv;
    GfxPointF p;
    GfxRect r, out;

    GfxAffineScale(&s, 2.0f, 3.0f);
    p = GfxAffineMapPointF(&s, MakeGfxPointF(4.0f, 5.0f));
    ExpectPointF(p, 8.0f, 15.0f, 0.0f);

    GfxAffineScale(&s, 1.0f, 1.0f);
    RoboCheck(GfxAffineIsIdentity(&s));

    /* Reflection */
    GfxAffineScale(&s, -1.0f, 1.0f);
    ExpectPointI(GfxAffineMapPointI(&s, MakeGfxPoint(5, 3)), -5,
                   3);
    r = MakeGfxRect(1, 2, 4, 3); /* [1,5)×[2,5) */
    RoboCheckEqInt(GfxAffineMapRect(&s, r, &out), RoboOk);
    RoboCheckEqInt(out.x, -5);
    RoboCheckEqInt(out.y, 2);
    RoboCheckEqInt(out.width, 4);
    RoboCheckEqInt(out.height, 3);

    /* Degenerate scale — singular */
    GfxAffineScale(&s, 0.0f, 2.0f);
    RoboCheck(!GfxAffineIsInvertible(&s));
    RoboCheckEqInt(GfxAffineInvert(&s, &inv), RoboInvalidArgument);
    p = GfxAffineMapPointF(&s, MakeGfxPointF(7.0f, 3.0f));
    ExpectPointF(p, 0.0f, 6.0f, 0.0f);

    GfxAffineScale(&s, 2.0f, 4.0f);
    RoboCheckEqInt(GfxAffineInvert(&s, &inv), RoboOk);
    p = GfxAffineMapPointF(
        &inv, GfxAffineMapPointF(&s, MakeGfxPointF(3.0f, 8.0f)));
    ExpectPointF(p, 3.0f, 8.0f, 1.0e-5f);
}

static void TestXformRotate(void)
{
    GfxAffine r;
    GfxAffine inv;
    GfxPoint p;
    GfxPointF pf;

    GfxAffineRotateDeg(&r, 0.0f);
    RoboCheck(GfxAffineIsIdentity(&r));

    GfxAffineRotateDeg(&r, 90.0f);
    p = GfxAffineMapPointI(&r, MakeGfxPoint(10, 0));
    ExpectPointI(p, 0, 10); /* (x,y)→(−y,x) */

    GfxAffineRotateDeg(&r, 180.0f);
    p = GfxAffineMapPointI(&r, MakeGfxPoint(4, 5));
    ExpectPointI(p, -4, -5);

    GfxAffineRotateDeg(&r, 270.0f);
    p = GfxAffineMapPointI(&r, MakeGfxPoint(10, 0));
    ExpectPointI(p, 0, -10); /* same as −90°: (x,y)→(y,−x) */

    GfxAffineRotateDeg(&r, -90.0f);
    p = GfxAffineMapPointI(&r, MakeGfxPoint(10, 0));
    ExpectPointI(p, 0, -10);

    GfxAffineRotateDeg(&r, 360.0f);
    RoboCheck(GfxAffineIsIdentity(&r));

    GfxAffineRotateDeg(&r, 45.0f);
    RoboCheckEqInt(GfxAffineInvert(&r, &inv), RoboOk);
    pf = GfxAffineMapPointF(
        &inv, GfxAffineMapPointF(&r, MakeGfxPointF(2.0f, 3.0f)));
    ExpectPointF(pf, 2.0f, 3.0f, 1.0e-4f);
}

static void TestXformShear(void)
{
    GfxAffine s;
    GfxAffine inv;
    GfxPointF p;

    GfxAffineShear(&s, 2.0f, 0.0f); /* horizontal */
    p = GfxAffineMapPointF(&s, MakeGfxPointF(1.0f, 3.0f));
    ExpectPointF(p, 7.0f, 3.0f, 0.0f); /* x + 2*y */

    GfxAffineShear(&s, 0.0f, -1.5f); /* vertical */
    p = GfxAffineMapPointF(&s, MakeGfxPointF(2.0f, 4.0f));
    ExpectPointF(p, 2.0f, 1.0f, 1.0e-5f); /* y - 1.5*x */

    GfxAffineShear(&s, 0.0f, 0.0f);
    RoboCheck(GfxAffineIsIdentity(&s));

    GfxAffineShear(&s, 1.0f, 0.5f);
    RoboCheckEqInt(GfxAffineInvert(&s, &inv), RoboOk);
    p = GfxAffineMapPointF(
        &inv, GfxAffineMapPointF(&s, MakeGfxPointF(3.0f, 5.0f)));
    ExpectPointF(p, 3.0f, 5.0f, 1.0e-4f);
}

static void TestXformComposeOrder(void)
{
    GfxAffine T, S, TS, ST;
    GfxPointF p;
    GfxAffine dst;

    /*
     * Existing Geometry smoke: multiply(translate, scale) = T*S
     * applies scale then translate.
     */
    GfxAffineTranslate(&T, 10.0f, 20.0f);
    GfxAffineScale(&S, 2.0f, 3.0f);
    RoboCheckEqInt(GfxAffineCompose(&T, &S, &TS), RoboOk);
    p = GfxAffineMapPointF(&TS, MakeGfxPointF(1.0f, 1.0f));
    ExpectPointF(p, 12.0f, 23.0f, 1.0e-5f);

    RoboCheckEqInt(GfxAffineCompose(&S, &T, &ST), RoboOk);
    p = GfxAffineMapPointF(&ST, MakeGfxPointF(1.0f, 1.0f));
    /* scale(translate(1,1)) = scale(11,21) = (22, 63) */
    ExpectPointF(p, 22.0f, 63.0f, 1.0e-5f);

    dst = T;
    RoboCheckEqInt(GfxAffinePreConcat(&dst, &S), RoboOk);
    RoboCheck(GfxAffineEqual(&dst, &TS));

    dst = T;
    RoboCheckEqInt(GfxAffinePostConcat(&dst, &S), RoboOk);
    RoboCheck(GfxAffineEqual(&dst, &ST));
}

static void TestXformComposeMixed(void)
{
    GfxAffine R, T, RT;
    GfxPoint p;

    GfxAffineRotateDeg(&R, 90.0f);
    GfxAffineTranslate(&T, 5.0f, 0.0f);
    /* RT = R*T: translate then rotate */
    RoboCheckEqInt(GfxAffineCompose(&R, &T, &RT), RoboOk);
    p = GfxAffineMapPointI(&RT, MakeGfxPoint(1, 0));
    /* translate → (6,0); rotate 90 → (0,6) */
    ExpectPointI(p, 0, 6);
}

static void TestXformRect(void)
{
    GfxAffine m;
    GfxRect r, out;

    r = MakeGfxRect(0, 0, 10, 4);

    GfxAffineTranslate(&m, 3.0f, 5.0f);
    RoboCheckEqInt(GfxAffineMapRect(&m, r, &out), RoboOk);
    RoboCheckEqInt(out.x, 3);
    RoboCheckEqInt(out.y, 5);
    RoboCheckEqInt(out.width, 10);
    RoboCheckEqInt(out.height, 4);

    GfxAffineScale(&m, 2.0f, 2.0f);
    RoboCheckEqInt(GfxAffineMapRect(&m, r, &out), RoboOk);
    RoboCheckEqInt(out.x, 0);
    RoboCheckEqInt(out.y, 0);
    RoboCheckEqInt(out.width, 20);
    RoboCheckEqInt(out.height, 8);

    /* 90° rotation of [0,10)×[0,4) corners → AABB */
    GfxAffineRotateDeg(&m, 90.0f);
    RoboCheckEqInt(GfxAffineMapRect(&m, r, &out), RoboOk);
    /* corners (0,0)(10,0)(0,4)(10,4) → (0,0)(0,10)(-4,0)(-4,10) */
    RoboCheckEqInt(out.x, -4);
    RoboCheckEqInt(out.y, 0);
    RoboCheckEqInt(out.width, 4);
    RoboCheckEqInt(out.height, 10);

    GfxAffineShear(&m, 1.0f, 0.0f);
    RoboCheckEqInt(GfxAffineMapRect(&m, r, &out), RoboOk);
    /* (0,0)(10,0)(0,4)(10,4) → (0,0)(10,0)(4,4)(14,4) → [0,14)×[0,4) */
    RoboCheckEqInt(out.x, 0);
    RoboCheckEqInt(out.y, 0);
    RoboCheckEqInt(out.width, 14);
    RoboCheckEqInt(out.height, 4);

    /* Empty */
    RoboCheckEqInt(
        GfxAffineMapRect(&m, GfxRectEmpty(), &out), RoboOk);
    RoboCheck(GfxRectIsEmpty(out));
}

static void TestXformPath(void)
{
    GfxAffine m;
    GfxPath *path = NULL;
    GfxPath *out = NULL;
    GfxPathCommand c;
    GfxPathId id;

    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);
    id = GetGfxPathId(path);
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(0, 0)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(10, 0)),
                      RoboOk);
    RoboCheckEqInt(GfxPathClose(path), RoboOk);
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(1, 1)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(2, 3)),
                      RoboOk);

    GfxAffineTranslate(&m, 5.0f, 7.0f);
    RoboCheckEqInt(GfxPathTransformed(path, &m, &out), RoboOk);

    RoboCheckEqInt((int)GfxPathCommandCount(out), 5);
    RoboCheckEqInt(GfxPathCommandAt(out, 0u, &c), RoboOk);
    RoboCheckEqInt(c.verb, RoboGfxPathMoveTo);
    ExpectPointI(c.point, 5, 7);
    RoboCheckEqInt(GfxPathCommandAt(out, 1u, &c), RoboOk);
    ExpectPointI(c.point, 15, 7);
    RoboCheckEqInt(GfxPathCommandAt(out, 2u, &c), RoboOk);
    RoboCheckEqInt(c.verb, RoboGfxPathClose);
    RoboCheckEqInt(GfxPathCommandAt(out, 3u, &c), RoboOk);
    ExpectPointI(c.point, 6, 8);
    RoboCheckEqInt(GfxPathCommandAt(out, 4u, &c), RoboOk);
    ExpectPointI(c.point, 7, 10);

    /* Source unchanged */
    RoboCheckEqInt(GfxPathCommandAt(path, 0u, &c), RoboOk);
    ExpectPointI(c.point, 0, 0);
    RoboCheck(GetGfxPathId(path) == id);

    /* In-place */
    RoboCheckEqInt(GfxPathTransform(path, &m), RoboOk);
    RoboCheckEqInt(GfxPathCommandAt(path, 0u, &c), RoboOk);
    ExpectPointI(c.point, 5, 7);
    RoboCheckEqInt(GfxPathCommandAt(path, 2u, &c), RoboOk);
    RoboCheckEqInt(c.verb, RoboGfxPathClose);

    GfxPathRelease(out);
    GfxPathRelease(path);
}

static void TestXformInverseSingular(void)
{
    GfxAffine m;
    GfxAffine inv;
    GfxAffine id;

    GfxAffineSet(&m, 1.0f, 2.0f, 3.0f, 2.0f, 4.0f, 5.0f); /* det=0 */
    RoboCheck(!GfxAffineIsInvertible(&m));
    RoboCheckEqInt(GfxAffineInvert(&m, &inv), RoboInvalidArgument);

    GfxAffineIdentity(&id);
    RoboCheckEqInt(GfxAffineInvert(&id, &inv), RoboOk);
    RoboCheck(GfxAffineIsIdentity(&inv));
}

static void TestXformNumeric(void)
{
    GfxAffine m;
    GfxPointF p;
    GfxRect out;

    GfxAffineTranslate(&m, -1.0e6f, 1.0e6f);
    p = GfxAffineMapPointF(&m, MakeGfxPointF(1.0e6f, -1.0e6f));
    ExpectPointF(p, 0.0f, 0.0f, 1.0f);

    GfxAffineScale(&m, -2.0f, -3.0f);
    p = GfxAffineMapPointF(&m, MakeGfxPointF(-4.0f, 5.0f));
    ExpectPointF(p, 8.0f, -15.0f, 0.0f);

    RoboCheckEqInt(GfxAffineMapRect(&m, MakeGfxRect(1, 1, 0, 5),
                                               &out),
                      RoboOk);
    RoboCheck(GfxRectIsEmpty(out));

    RoboCheckEqInt(GfxAffineInvert(NULL, &m), RoboInvalidArgument);
    RoboCheckEqInt(GfxPathTransformed(NULL, &m, NULL),
                      RoboInvalidArgument);
}

void TestRegisterGraphicsTransform(void)
{
    TestRegister("graphics.xform.identity", TestXformIdentity);
    TestRegister("graphics.xform.translate", TestXformTranslate);
    TestRegister("graphics.xform.scale", TestXformScale);
    TestRegister("graphics.xform.rotate", TestXformRotate);
    TestRegister("graphics.xform.shear", TestXformShear);
    TestRegister("graphics.xform.compose", TestXformComposeOrder);
    TestRegister("graphics.xform.compose_mixed",
                       TestXformComposeMixed);
    TestRegister("graphics.xform.rect", TestXformRect);
    TestRegister("graphics.xform.path", TestXformPath);
    TestRegister("graphics.xform.inverse", TestXformInverseSingular);
    TestRegister("graphics.xform.numeric", TestXformNumeric);
}
