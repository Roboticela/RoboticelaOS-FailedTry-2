#include "RoboGraphics/RoboGraphicsTransform.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif
#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif

/* Absolute |det| below this is treated as singular. */
#define RoboGfxAffineDetEps 1.0e-12f

static const float kPi = 3.14159265358979323846f;
static const float kTwoPi = 6.28318530717958647692f;

static float AbsfLocal(float v)
{
    return v < 0.0f ? -v : v;
}

/* Freestanding-safe float→int (no libm), matching Geometry.c. */
static int32_t FloorFToI32(float v)
{
    int32_t i = (int32_t)v;
    if ((float)i > v) {
        i -= 1;
    }
    return i;
}

static int32_t CeilFToI32(float v)
{
    int32_t i = (int32_t)v;
    if ((float)i < v) {
        i += 1;
    }
    return i;
}

static float RoundNearestF(float v)
{
    if (v >= 0.0f) {
        return (float)FloorFToI32(v + 0.5f);
    }
    return (float)CeilFToI32(v - 0.5f);
}

/**
 * Freestanding sin/cos — Taylor on [-π/2, π/2] after 2π reduction.
 * Adequate for affine foundation; not a full libm replacement.
 */
static float ReduceAngle(float x)
{
    float n;
    float r;

    n = x / kTwoPi;
    n = RoundNearestF(n);
    r = x - n * kTwoPi;
    if (r > kPi) {
        r -= kTwoPi;
    } else if (r < -kPi) {
        r += kTwoPi;
    }
    return r;
}

static float SinPoly(float y)
{
    /* sin(y) ≈ y − y³/6 + y⁵/120 − y⁷/5040, |y| ≤ π/2 */
    float y2 = y * y;
    return y * (1.0f - y2 * (1.0f / 6.0f -
                             y2 * (1.0f / 120.0f - y2 * (1.0f / 5040.0f))));
}

static float Sinf(float x)
{
    float y = ReduceAngle(x);

    if (y > (kPi * 0.5f)) {
        y = kPi - y;
    } else if (y < -(kPi * 0.5f)) {
        y = -kPi - y;
    }
    return SinPoly(y);
}

static float Cosf(float x)
{
    return Sinf(x + kPi * 0.5f);
}

void GfxAffineSet(GfxAffine *out,
                         float m00,
                         float m01,
                         float m02,
                         float m10,
                         float m11,
                         float m12)
{
    if (!out) {
        return;
    }
    out->m00 = m00;
    out->m01 = m01;
    out->m02 = m02;
    out->m10 = m10;
    out->m11 = m11;
    out->m12 = m12;
}

static int NearMultipleOf90(float degrees, float *outNorm)
{
    float n = degrees;

    while (n > 180.0f) {
        n -= 360.0f;
    }
    while (n < -180.0f) {
        n += 360.0f;
    }
    if (AbsfLocal(n) < 1.0e-4f) {
        *outNorm = 0.0f;
        return 1;
    }
    if (AbsfLocal(n - 90.0f) < 1.0e-4f) {
        *outNorm = 90.0f;
        return 1;
    }
    if (AbsfLocal(n + 90.0f) < 1.0e-4f) {
        *outNorm = -90.0f;
        return 1;
    }
    if (AbsfLocal(n - 180.0f) < 1.0e-4f || AbsfLocal(n + 180.0f) < 1.0e-4f) {
        *outNorm = 180.0f;
        return 1;
    }
    return 0;
}

void GfxAffineRotateDeg(GfxAffine *out, float degrees)
{
    float norm;

    if (!out) {
        return;
    }
    if (NearMultipleOf90(degrees, &norm)) {
        if (norm == 0.0f) {
            GfxAffineIdentity(out);
        } else if (norm == 90.0f) {
            GfxAffineSet(out, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        } else if (norm == -90.0f) {
            GfxAffineSet(out, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f);
        } else {
            GfxAffineSet(out, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f);
        }
        return;
    }
    GfxAffineRotateRad(out, degrees * (kPi / 180.0f));
}

void GfxAffineRotateRad(GfxAffine *out, float radians)
{
    float c;
    float s;

    if (!out) {
        return;
    }
    c = Cosf(radians);
    s = Sinf(radians);
    GfxAffineSet(out, c, -s, 0.0f, s, c, 0.0f);
}

void GfxAffineShear(GfxAffine *out, float shx, float shy)
{
    if (!out) {
        return;
    }
    GfxAffineSet(out, 1.0f, shx, 0.0f, shy, 1.0f, 0.0f);
}

int GfxAffineIsIdentity(const GfxAffine *m)
{
    if (!m) {
        return 0;
    }
    return m->m00 == 1.0f && m->m01 == 0.0f && m->m02 == 0.0f &&
           m->m10 == 0.0f && m->m11 == 1.0f && m->m12 == 0.0f;
}

float GfxAffineDeterminant(const GfxAffine *m)
{
    if (!m) {
        return 0.0f;
    }
    return m->m00 * m->m11 - m->m01 * m->m10;
}

int GfxAffineIsInvertible(const GfxAffine *m)
{
    if (!m) {
        return 0;
    }
    return AbsfLocal(GfxAffineDeterminant(m)) > RoboGfxAffineDetEps;
}

int GfxAffineEqual(const GfxAffine *a, const GfxAffine *b)
{
    if (!a || !b) {
        return 0;
    }
    return a->m00 == b->m00 && a->m01 == b->m01 && a->m02 == b->m02 &&
           a->m10 == b->m10 && a->m11 == b->m11 && a->m12 == b->m12;
}

Result GfxAffineCompose(const GfxAffine *a,
                                      const GfxAffine *b,
                                      GfxAffine *out)
{
    return GfxAffineMultiply(a, b, out);
}

Result GfxAffinePreConcat(GfxAffine *dst,
                                         const GfxAffine *other)
{
    GfxAffine tmp;
    Result res;

    if (!dst || !other) {
        return RoboInvalidArgument;
    }
    res = GfxAffineMultiply(dst, other, &tmp);
    if (res != RoboOk) {
        return res;
    }
    *dst = tmp;
    return RoboOk;
}

Result GfxAffinePostConcat(GfxAffine *dst,
                                          const GfxAffine *other)
{
    GfxAffine tmp;
    Result res;

    if (!dst || !other) {
        return RoboInvalidArgument;
    }
    res = GfxAffineMultiply(other, dst, &tmp);
    if (res != RoboOk) {
        return res;
    }
    *dst = tmp;
    return RoboOk;
}

Result GfxAffineInvert(const GfxAffine *m,
                                     GfxAffine *out)
{
    float det;
    float inv;
    GfxAffine r;

    if (!m || !out) {
        return RoboInvalidArgument;
    }
    det = GfxAffineDeterminant(m);
    if (AbsfLocal(det) <= RoboGfxAffineDetEps) {
        return RoboInvalidArgument;
    }
    inv = 1.0f / det;
    /*
     * Inverse of [A t; 0 1] is [A^-1, -A^-1 t; 0 1].
     * A^-1 = (1/det) |  m11 -m01 |
     *                | -m10  m00 |
     */
    r.m00 = m->m11 * inv;
    r.m01 = -m->m01 * inv;
    r.m10 = -m->m10 * inv;
    r.m11 = m->m00 * inv;
    r.m02 = -(r.m00 * m->m02 + r.m01 * m->m12);
    r.m12 = -(r.m10 * m->m02 + r.m11 * m->m12);
    *out = r;
    return RoboOk;
}

GfxPointF GfxAffineMapPointF(const GfxAffine *m,
                                               GfxPointF p)
{
    return GfxAffineMapPoint(m, p);
}

GfxPoint GfxAffineMapPointI(const GfxAffine *m,
                                             GfxPoint p)
{
    GfxPointF pf;

    pf = MakeGfxPointF((float)p.x, (float)p.y);
    pf = GfxAffineMapPoint(m, pf);
    return GfxPointFRound(pf);
}

Result GfxAffineMapRectF(const GfxAffine *m,
                                         GfxRectF r,
                                         GfxRectF *out)
{
    GfxPointF c0, c1, c2, c3;
    float minX, minY, maxX, maxY;

    if (!m || !out) {
        return RoboInvalidArgument;
    }
    if (r.width <= 0.0f || r.height <= 0.0f) {
        *out = MakeGfxRectF(0.0f, 0.0f, 0.0f, 0.0f);
        return RoboOk;
    }

    c0 = GfxAffineMapPoint(m, MakeGfxPointF(r.x, r.y));
    c1 = GfxAffineMapPoint(m, MakeGfxPointF(r.x + r.width, r.y));
    c2 = GfxAffineMapPoint(m, MakeGfxPointF(r.x, r.y + r.height));
    c3 = GfxAffineMapPoint(
        m, MakeGfxPointF(r.x + r.width, r.y + r.height));

    minX = maxX = c0.x;
    minY = maxY = c0.y;
    if (c1.x < minX) {
        minX = c1.x;
    }
    if (c1.x > maxX) {
        maxX = c1.x;
    }
    if (c1.y < minY) {
        minY = c1.y;
    }
    if (c1.y > maxY) {
        maxY = c1.y;
    }
    if (c2.x < minX) {
        minX = c2.x;
    }
    if (c2.x > maxX) {
        maxX = c2.x;
    }
    if (c2.y < minY) {
        minY = c2.y;
    }
    if (c2.y > maxY) {
        maxY = c2.y;
    }
    if (c3.x < minX) {
        minX = c3.x;
    }
    if (c3.x > maxX) {
        maxX = c3.x;
    }
    if (c3.y < minY) {
        minY = c3.y;
    }
    if (c3.y > maxY) {
        maxY = c3.y;
    }

    *out = MakeGfxRectF(minX, minY, maxX - minX, maxY - minY);
    return RoboOk;
}

Result GfxAffineMapRect(const GfxAffine *m,
                                       GfxRect r,
                                       GfxRect *out)
{
    GfxRectF rf;
    GfxRectF mapped;
    Result res;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;

    if (!m || !out) {
        return RoboInvalidArgument;
    }
    *out = GfxRectEmpty();
    if (!GfxRectIsValid(r) || GfxRectIsEmpty(r)) {
        return RoboOk;
    }

    rf = MakeGfxRectF((float)r.x, (float)r.y, (float)r.width,
                         (float)r.height);
    res = GfxAffineMapRectF(m, rf, &mapped);
    if (res != RoboOk) {
        return res;
    }

    left = FloorFToI32(mapped.x);
    top = FloorFToI32(mapped.y);
    right = CeilFToI32(mapped.x + mapped.width);
    bottom = CeilFToI32(mapped.y + mapped.height);
    return GfxRectFromLtrb(left, top, right, bottom, out);
}

static Result AppendTransformedCmd(GfxPath *dst,
                                            const GfxAffine *m,
                                            const GfxPathCommand *cmd)
{
    GfxPoint p;

    switch (cmd->verb) {
    case RoboGfxPathMoveTo:
        p = GfxAffineMapPointI(m, cmd->point);
        return GfxPathMoveTo(dst, p);
    case RoboGfxPathLineTo:
        p = GfxAffineMapPointI(m, cmd->point);
        return GfxPathLineTo(dst, p);
    case RoboGfxPathClose:
        return GfxPathClose(dst);
    case RoboGfxPathQuadTo:
    case RoboGfxPathCubicTo:
        return RoboNotSupported;
    default:
        return RoboInvalidArgument;
    }
}

Result GfxPathTransformed(const GfxPath *src,
                                        const GfxAffine *m,
                                        GfxPath **out)
{
    GfxPath *dst = NULL;
    GfxPathIter it;
    GfxPathCommand cmd;
    Result res;

    if (!src || !m || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    res = GfxPathCreate(&dst);
    if (res != RoboOk) {
        return res;
    }

    GfxPathIterBegin(src, &it);
    while (GfxPathIterNext(&it, &cmd)) {
        res = AppendTransformedCmd(dst, m, &cmd);
        if (res != RoboOk) {
            GfxPathRelease(dst);
            return res;
        }
    }

    *out = dst;
    return RoboOk;
}

Result GfxPathTransform(GfxPath *path,
                                      const GfxAffine *m)
{
    GfxPathCommand *snapshot = NULL;
    size_t n;
    size_t i;
    Result res;

    if (!path || !m) {
        return RoboInvalidArgument;
    }

    n = GfxPathCommandCount(path);
    if (n == 0u) {
        return RoboOk;
    }

    /* Reject unsupported verbs before mutating. */
    for (i = 0u; i < n; i++) {
        GfxPathCommand c;
        res = GfxPathCommandAt(path, i, &c);
        if (res != RoboOk) {
            return res;
        }
        if (c.verb == RoboGfxPathQuadTo ||
            c.verb == RoboGfxPathCubicTo) {
            return RoboNotSupported;
        }
    }

    snapshot = (GfxPathCommand *)RoboCalloc(
        n, sizeof(GfxPathCommand));
    if (!snapshot) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < n; i++) {
        res = GfxPathCommandAt(path, i, &snapshot[i]);
        if (res != RoboOk) {
            RoboFree(snapshot);
            return res;
        }
    }

    GfxPathReset(path);
    for (i = 0u; i < n; i++) {
        res = AppendTransformedCmd(path, m, &snapshot[i]);
        if (res != RoboOk) {
            /*
             * Restore original commands so failure does not corrupt the path.
             */
            GfxPathReset(path);
            for (i = 0u; i < n; i++) {
                Result r2;
                switch (snapshot[i].verb) {
                case RoboGfxPathMoveTo:
                    r2 = GfxPathMoveTo(path, snapshot[i].point);
                    break;
                case RoboGfxPathLineTo:
                    r2 = GfxPathLineTo(path, snapshot[i].point);
                    break;
                case RoboGfxPathClose:
                    r2 = GfxPathClose(path);
                    break;
                default:
                    r2 = RoboInvalidArgument;
                    break;
                }
                (void)r2;
            }
            RoboFree(snapshot);
            return res;
        }
    }

    RoboFree(snapshot);
    return RoboOk;
}
