#include "RoboGraphics/RoboGraphicsGeometry.h"

#include <stddef.h>

#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif
#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif

static int I32Add(int32_t a, int32_t b, int32_t *out)
{
    int64_t s = (int64_t)a + (int64_t)b;
    if (s > INT32_MAX || s < INT32_MIN) {
        return 0;
    }
    *out = (int32_t)s;
    return 1;
}

static int I32Sub(int32_t a, int32_t b, int32_t *out)
{
    int64_t d = (int64_t)a - (int64_t)b;
    if (d > INT32_MAX || d < INT32_MIN) {
        return 0;
    }
    *out = (int32_t)d;
    return 1;
}

static int32_t I32Saturate(int64_t v)
{
    if (v > INT32_MAX) {
        return INT32_MAX;
    }
    if (v < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)v;
}

static int RectEdges(GfxRect r,
                      int64_t *left,
                      int64_t *top,
                      int64_t *right,
                      int64_t *bottom)
{
    if (!GfxRectIsValid(r)) {
        return 0;
    }
    *left = (int64_t)r.x;
    *top = (int64_t)r.y;
    *right = (int64_t)r.x + (int64_t)r.width;
    *bottom = (int64_t)r.y + (int64_t)r.height;
    return 1;
}

static Result RectFromEdges(int64_t left,
                                     int64_t top,
                                     int64_t right,
                                     int64_t bottom,
                                     GfxRect *out)
{
    int64_t w;
    int64_t h;

    if (!out) {
        return RoboInvalidArgument;
    }
    if (right < left || bottom < top) {
        return RoboInvalidArgument;
    }
    w = right - left;
    h = bottom - top;
    if (w > INT32_MAX || h > INT32_MAX) {
        return RoboInvalidArgument;
    }
    if (left > INT32_MAX || left < INT32_MIN || top > INT32_MAX ||
        top < INT32_MIN) {
        return RoboInvalidArgument;
    }
    out->x = (int32_t)left;
    out->y = (int32_t)top;
    out->width = (int32_t)w;
    out->height = (int32_t)h;
    return RoboOk;
}

GfxPoint MakeGfxPoint(GfxPixelX x, GfxPixelY y)
{
    GfxPoint p;
    p.x = x;
    p.y = y;
    return p;
}

GfxSize MakeGfxSize(int32_t width, int32_t height)
{
    GfxSize s;
    s.width = width;
    s.height = height;
    return s;
}

GfxVector MakeGfxVector(int32_t dx, int32_t dy)
{
    GfxVector v;
    v.dx = dx;
    v.dy = dy;
    return v;
}

GfxRect MakeGfxRect(GfxPixelX x,
                              GfxPixelY y,
                              int32_t width,
                              int32_t height)
{
    GfxRect r;
    r.x = x;
    r.y = y;
    r.width = width;
    r.height = height;
    return r;
}

GfxRect GfxRectFromSize(GfxSize size)
{
    return MakeGfxRect(0, 0, size.width, size.height);
}

GfxRect GfxRectEmpty(void)
{
    return MakeGfxRect(0, 0, 0, 0);
}

Result GfxRectFromPointSize(GfxPoint origin,
                                            GfxSize size,
                                            GfxRect *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!GfxSizeIsValid(size)) {
        return RoboInvalidArgument;
    }
    *out = MakeGfxRect(origin.x, origin.y, size.width, size.height);
    return RoboOk;
}

Result GfxRectFromLtrb(GfxPixelX left,
                                      GfxPixelY top,
                                      GfxPixelX right,
                                      GfxPixelY bottom,
                                      GfxRect *out)
{
    return RectFromEdges((int64_t)left, (int64_t)top, (int64_t)right,
                           (int64_t)bottom, out);
}

GfxPointF MakeGfxPointF(float x, float y)
{
    GfxPointF p;
    p.x = x;
    p.y = y;
    return p;
}

GfxSizeF MakeGfxSizeF(float width, float height)
{
    GfxSizeF s;
    s.width = width;
    s.height = height;
    return s;
}

GfxRectF MakeGfxRectF(float x, float y, float width, float height)
{
    GfxRectF r;
    r.x = x;
    r.y = y;
    r.width = width;
    r.height = height;
    return r;
}

int GfxPointEqual(GfxPoint a, GfxPoint b)
{
    return a.x == b.x && a.y == b.y;
}

Result GfxPointAdd(GfxPoint a,
                                 GfxPoint b,
                                 GfxPoint *out)
{
    int32_t x;
    int32_t y;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!I32Add(a.x, b.x, &x) || !I32Add(a.y, b.y, &y)) {
        return RoboInvalidArgument;
    }
    out->x = x;
    out->y = y;
    return RoboOk;
}

Result GfxPointSub(GfxPoint a,
                                 GfxPoint b,
                                 GfxPoint *out)
{
    int32_t x;
    int32_t y;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!I32Sub(a.x, b.x, &x) || !I32Sub(a.y, b.y, &y)) {
        return RoboInvalidArgument;
    }
    out->x = x;
    out->y = y;
    return RoboOk;
}

Result GfxPointTranslate(GfxPoint p,
                                       int32_t dx,
                                       int32_t dy,
                                       GfxPoint *out)
{
    return GfxPointAdd(p, MakeGfxPoint(dx, dy), out);
}

int GfxSizeEqual(GfxSize a, GfxSize b)
{
    return a.width == b.width && a.height == b.height;
}

int GfxSizeIsEmpty(GfxSize s)
{
    return s.width <= 0 || s.height <= 0;
}

int GfxSizeIsValid(GfxSize s)
{
    return s.width >= 0 && s.height >= 0;
}

Result GfxSizeArea(GfxSize s, uint64_t *outArea)
{
    if (!outArea) {
        return RoboInvalidArgument;
    }
    *outArea = 0u;
    if (!GfxSizeIsValid(s)) {
        return RoboInvalidArgument;
    }
    if (GfxSizeIsEmpty(s)) {
        return RoboOk;
    }
    *outArea = (uint64_t)(uint32_t)s.width * (uint64_t)(uint32_t)s.height;
    return RoboOk;
}

int GfxRectEqual(GfxRect a, GfxRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

int GfxRectIsEmpty(GfxRect r)
{
    return r.width <= 0 || r.height <= 0;
}

int GfxRectIsValid(GfxRect r)
{
    return r.width >= 0 && r.height >= 0;
}

GfxPixelX GfxRectLeft(GfxRect r)
{
    return r.x;
}

GfxPixelY GfxRectTop(GfxRect r)
{
    return r.y;
}

int32_t GfxRectWidth(GfxRect r)
{
    return r.width;
}

int32_t GfxRectHeight(GfxRect r)
{
    return r.height;
}

int32_t GfxRectRight(GfxRect r)
{
    return I32Saturate((int64_t)r.x + (int64_t)r.width);
}

int32_t GfxRectBottom(GfxRect r)
{
    return I32Saturate((int64_t)r.y + (int64_t)r.height);
}

Result GfxRectRightChecked(GfxRect r, int32_t *out)
{
    int32_t v;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!I32Add(r.x, r.width, &v)) {
        return RoboInvalidArgument;
    }
    *out = v;
    return RoboOk;
}

Result GfxRectBottomChecked(GfxRect r, int32_t *out)
{
    int32_t v;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!I32Add(r.y, r.height, &v)) {
        return RoboInvalidArgument;
    }
    *out = v;
    return RoboOk;
}

int GfxRectContainsPoint(GfxRect r, GfxPoint p)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (GfxRectIsEmpty(r) || !RectEdges(r, &left, &top, &right, &bottom)) {
        return 0;
    }
    return (int64_t)p.x >= left && (int64_t)p.x < right && (int64_t)p.y >= top &&
           (int64_t)p.y < bottom;
}

int GfxRectEncloses(GfxRect outer, GfxRect inner)
{
    int64_t ol, ot, oright, ob;
    int64_t il, it, ir, ib;

    if (!RectEdges(outer, &ol, &ot, &oright, &ob)) {
        return 0;
    }
    if (!RectEdges(inner, &il, &it, &ir, &ib)) {
        return 0;
    }
    if (GfxRectIsEmpty(inner)) {
        return 1;
    }
    if (GfxRectIsEmpty(outer)) {
        return 0;
    }
    return il >= ol && it >= ot && ir <= oright && ib <= ob;
}

int GfxRectIntersects(GfxRect a, GfxRect b)
{
    GfxRect tmp;
    if (GfxRectIntersect(a, b, &tmp) != RoboOk) {
        return 0;
    }
    return !GfxRectIsEmpty(tmp);
}

GfxPoint GfxRectCenter(GfxRect r)
{
    return MakeGfxPoint(r.x + r.width / 2, r.y + r.height / 2);
}

Result GfxRectIntersect(GfxRect a,
                                      GfxRect b,
                                      GfxRect *out)
{
    int64_t al, at, ar, ab;
    int64_t bl, bt, br, bb;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = GfxRectEmpty();
    if (GfxRectIsEmpty(a) || GfxRectIsEmpty(b)) {
        return RoboOk;
    }
    if (!RectEdges(a, &al, &at, &ar, &ab) || !RectEdges(b, &bl, &bt, &br, &bb)) {
        return RoboInvalidArgument;
    }
    left = al > bl ? al : bl;
    top = at > bt ? at : bt;
    right = ar < br ? ar : br;
    bottom = ab < bb ? ab : bb;
    if (right <= left || bottom <= top) {
        *out = GfxRectEmpty();
        return RoboOk;
    }
    return RectFromEdges(left, top, right, bottom, out);
}

Result GfxRectBounding(GfxRect a,
                                     GfxRect b,
                                     GfxRect *out)
{
    int64_t al, at, ar, ab;
    int64_t bl, bt, br, bb;
    int aEmpty;
    int bEmpty;

    if (!out) {
        return RoboInvalidArgument;
    }
    aEmpty = GfxRectIsEmpty(a);
    bEmpty = GfxRectIsEmpty(b);
    if (aEmpty && bEmpty) {
        *out = GfxRectEmpty();
        return RoboOk;
    }
    if (aEmpty) {
        if (!GfxRectIsValid(b)) {
            return RoboInvalidArgument;
        }
        *out = b;
        return RoboOk;
    }
    if (bEmpty) {
        if (!GfxRectIsValid(a)) {
            return RoboInvalidArgument;
        }
        *out = a;
        return RoboOk;
    }
    if (!RectEdges(a, &al, &at, &ar, &ab) || !RectEdges(b, &bl, &bt, &br, &bb)) {
        return RoboInvalidArgument;
    }
    return RectFromEdges(al < bl ? al : bl, at < bt ? at : bt,
                           ar > br ? ar : br, ab > bb ? ab : bb, out);
}

Result GfxRectTranslate(GfxRect r,
                                      int32_t dx,
                                      int32_t dy,
                                      GfxRect *out)
{
    int32_t nx;
    int32_t ny;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!GfxRectIsValid(r)) {
        return RoboInvalidArgument;
    }
    if (!I32Add(r.x, dx, &nx) || !I32Add(r.y, dy, &ny)) {
        return RoboInvalidArgument;
    }
    *out = MakeGfxRect(nx, ny, r.width, r.height);
    return RoboOk;
}

Result GfxRectClip(GfxRect rect,
                                 GfxRect bounds,
                                 GfxRect *out)
{
    return GfxRectIntersect(rect, bounds, out);
}

Result GfxRectInset(GfxRect r,
                                  int32_t amount,
                                  GfxRect *out)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (!out) {
        return RoboInvalidArgument;
    }
    if (!GfxRectIsValid(r)) {
        return RoboInvalidArgument;
    }
    if (GfxRectIsEmpty(r)) {
        *out = r;
        return RoboOk;
    }
    if (!RectEdges(r, &left, &top, &right, &bottom)) {
        return RoboInvalidArgument;
    }
    left += (int64_t)amount;
    top += (int64_t)amount;
    right -= (int64_t)amount;
    bottom -= (int64_t)amount;
    if (right <= left || bottom <= top) {
        *out = GfxRectEmpty();
        return RoboOk;
    }
    return RectFromEdges(left, top, right, bottom, out);
}

Result GfxRectOutset(GfxRect r,
                                   int32_t amount,
                                   GfxRect *out)
{
    if (amount == INT32_MIN) {
        return RoboInvalidArgument;
    }
    return GfxRectInset(r, -amount, out);
}

/* Freestanding-safe float→int helpers (no libm). */
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

static int32_t RoundFToI32(float v)
{
    if (v >= 0.0f) {
        return FloorFToI32(v + 0.5f);
    }
    return CeilFToI32(v - 0.5f);
}

GfxPoint GfxPointFFloor(GfxPointF p)
{
    return MakeGfxPoint(FloorFToI32(p.x), FloorFToI32(p.y));
}

GfxPoint GfxPointFRound(GfxPointF p)
{
    return MakeGfxPoint(RoundFToI32(p.x), RoundFToI32(p.y));
}

GfxRect GfxRectFFloor(GfxRectF r)
{
    int32_t x0 = FloorFToI32(r.x);
    int32_t y0 = FloorFToI32(r.y);
    int32_t x1 = FloorFToI32(r.x + r.width);
    int32_t y1 = FloorFToI32(r.y + r.height);
    return MakeGfxRect(x0, y0, x1 - x0, y1 - y0);
}

GfxRect GfxRectFRoundOut(GfxRectF r)
{
    int32_t x0 = FloorFToI32(r.x);
    int32_t y0 = FloorFToI32(r.y);
    int32_t x1 = CeilFToI32(r.x + r.width);
    int32_t y1 = CeilFToI32(r.y + r.height);
    return MakeGfxRect(x0, y0, x1 - x0, y1 - y0);
}

void GfxAffineIdentity(GfxAffine *out)
{
    if (!out) {
        return;
    }
    out->m00 = 1.0f;
    out->m01 = 0.0f;
    out->m02 = 0.0f;
    out->m10 = 0.0f;
    out->m11 = 1.0f;
    out->m12 = 0.0f;
}

void GfxAffineTranslate(GfxAffine *out, float tx, float ty)
{
    GfxAffineIdentity(out);
    if (out) {
        out->m02 = tx;
        out->m12 = ty;
    }
}

void GfxAffineScale(GfxAffine *out, float sx, float sy)
{
    GfxAffineIdentity(out);
    if (out) {
        out->m00 = sx;
        out->m11 = sy;
    }
}

Result GfxAffineMultiply(const GfxAffine *a,
                                       const GfxAffine *b,
                                       GfxAffine *out)
{
    GfxAffine r;
    if (!a || !b || !out) {
        return RoboInvalidArgument;
    }
    r.m00 = a->m00 * b->m00 + a->m01 * b->m10;
    r.m01 = a->m00 * b->m01 + a->m01 * b->m11;
    r.m02 = a->m00 * b->m02 + a->m01 * b->m12 + a->m02;
    r.m10 = a->m10 * b->m00 + a->m11 * b->m10;
    r.m11 = a->m10 * b->m01 + a->m11 * b->m11;
    r.m12 = a->m10 * b->m02 + a->m11 * b->m12 + a->m12;
    *out = r;
    return RoboOk;
}

GfxPointF GfxAffineMapPoint(const GfxAffine *m,
                                             GfxPointF p)
{
    GfxPointF o;
    if (!m) {
        return p;
    }
    o.x = m->m00 * p.x + m->m01 * p.y + m->m02;
    o.y = m->m10 * p.x + m->m11 * p.y + m->m12;
    return o;
}
