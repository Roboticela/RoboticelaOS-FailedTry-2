/* Internal outline builder (Goal 5.8). Included by outline .c files only.
 * Define RoboOutlineStructOnly before include to get the opaque struct
 * without static builder helpers (for RoboFontOutline.c accessors).
 */
#ifndef ROBO_FONT_OUTLINE_INTERNAL_H
#define ROBO_FONT_OUTLINE_INTERNAL_H

#include "RoboFont/RoboFontOutline.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct FontOutline {
    uint32_t refs;
    GlyphId glyphId;
    FontOutlineStatus status;
    FontOutlineTech tech;
    FontBbox bounds;
    uint8_t hasBounds;
    uint32_t contourCount;
    uint32_t contourCap;
    FontContour *contours;
    uint32_t totalSegs;
};

#ifndef RoboOutlineStructOnly

typedef struct {
    FontOutline *o;
    FontContour *cur;
    uint32_t curSegCap;
    FontUnit cx, cy;
    FontUnit sx, sy;
    int pathOpen;
    FontAffine xform;
    int applyXform;
} OutlineBuilder;

static __attribute__((unused)) double DSqrt(double x)
{
    double g;
    int i;
    if (x <= 0.0) {
        return 0.0;
    }
    g = x > 1.0 ? x : 1.0;
    for (i = 0; i < 16; i++) {
        g = 0.5 * (g + x / g);
    }
    return g;
}

static __attribute__((unused)) void BuilderInit(OutlineBuilder *b, FontOutline *o)
{
    memset(b, 0, sizeof(*b));
    b->o = o;
    FontAffineIdentity(&b->xform);
}

static __attribute__((unused)) Result MapPt(OutlineBuilder *b,
                            FontUnit x,
                            FontUnit y,
                            FontPoint *out)
{
    if (b->applyXform) {
        return FontAffineApply(&b->xform, x, y, &out->x, &out->y);
    }
    out->x = x;
    out->y = y;
    return RoboOk;
}

static __attribute__((unused)) void BboxInclude(FontBbox *bb,
                         int *init,
                         FontUnit x,
                         FontUnit y)
{
    if (!*init) {
        bb->xMin = bb->xMax = x;
        bb->yMin = bb->yMax = y;
        *init = 1;
        return;
    }
    if (x < bb->xMin) {
        bb->xMin = x;
    }
    if (x > bb->xMax) {
        bb->xMax = x;
    }
    if (y < bb->yMin) {
        bb->yMin = y;
    }
    if (y > bb->yMax) {
        bb->yMax = y;
    }
}

static __attribute__((unused)) void QuadExtrema1d(FontUnit p0,
                            FontUnit p1,
                            FontUnit p2,
                            FontUnit *lo,
                            FontUnit *hi)
{
    double a = (double)p0 - 2.0 * (double)p1 + (double)p2;
    double t, v;
    *lo = p0 < p2 ? p0 : p2;
    *hi = p0 > p2 ? p0 : p2;
    if (a > -1e-12 && a < 1e-12) {
        return;
    }
    t = ((double)p0 - (double)p1) / a;
    if (t > 0.0 && t < 1.0) {
        v = (1.0 - t) * (1.0 - t) * (double)p0 + 2.0 * (1.0 - t) * t * (double)p1 +
            t * t * (double)p2;
        if (v < (double)*lo) {
            *lo = (FontUnit)v;
        }
        if (v > (double)*hi) {
            *hi = (FontUnit)v;
        }
    }
}

static __attribute__((unused)) void CubicExtrema1d(FontUnit p0,
                             FontUnit p1,
                             FontUnit p2,
                             FontUnit p3,
                             FontUnit *lo,
                             FontUnit *hi)
{
    double a, b, c, disc, t, v;
    int i;
    *lo = p0 < p3 ? p0 : p3;
    *hi = p0 > p3 ? p0 : p3;
    a = -3.0 * (double)p0 + 9.0 * (double)p1 - 9.0 * (double)p2 + 3.0 * (double)p3;
    b = 6.0 * (double)p0 - 12.0 * (double)p1 + 6.0 * (double)p2;
    c = -3.0 * (double)p0 + 3.0 * (double)p1;
    if (a > -1e-12 && a < 1e-12) {
        if (b > -1e-12 && b < 1e-12) {
            return;
        }
        t = -c / b;
        if (t > 0.0 && t < 1.0) {
            double u = 1.0 - t;
            v = u * u * u * (double)p0 + 3.0 * u * u * t * (double)p1 +
                3.0 * u * t * t * (double)p2 + t * t * t * (double)p3;
            if (v < (double)*lo) {
                *lo = (FontUnit)v;
            }
            if (v > (double)*hi) {
                *hi = (FontUnit)v;
            }
        }
        return;
    }
    disc = b * b - 4.0 * a * c;
    if (disc < 0.0) {
        return;
    }
    for (i = 0; i < 2; i++) {
        double s = (i == 0) ? 1.0 : -1.0;
        t = (-b + s * DSqrt(disc)) / (2.0 * a);
        if (t > 0.0 && t < 1.0) {
            double u = 1.0 - t;
            v = u * u * u * (double)p0 + 3.0 * u * u * t * (double)p1 +
                3.0 * u * t * t * (double)p2 + t * t * t * (double)p3;
            if (v < (double)*lo) {
                *lo = (FontUnit)v;
            }
            if (v > (double)*hi) {
                *hi = (FontUnit)v;
            }
        }
    }
}

static __attribute__((unused)) void IncludeSegBounds(FontBbox *bb,
                               int *init,
                               const FontPathSeg *seg,
                               FontUnit prevX,
                               FontUnit prevY)
{
    FontUnit xa, xb, ya, yb;
    switch (seg->op) {
    case RoboFontPathMove:
    case RoboFontPathLine:
        BboxInclude(bb, init, seg->p.x, seg->p.y);
        break;
    case RoboFontPathQuad:
        QuadExtrema1d(prevX, seg->c1.x, seg->p.x, &xa, &xb);
        QuadExtrema1d(prevY, seg->c1.y, seg->p.y, &ya, &yb);
        BboxInclude(bb, init, xa, ya);
        BboxInclude(bb, init, xb, yb);
        BboxInclude(bb, init, seg->p.x, seg->p.y);
        break;
    case RoboFontPathCubic:
        CubicExtrema1d(prevX, seg->c1.x, seg->c2.x, seg->p.x, &xa, &xb);
        CubicExtrema1d(prevY, seg->c1.y, seg->c2.y, seg->p.y, &ya, &yb);
        BboxInclude(bb, init, xa, ya);
        BboxInclude(bb, init, xb, yb);
        BboxInclude(bb, init, seg->p.x, seg->p.y);
        break;
    default:
        break;
    }
}

static __attribute__((unused)) Result EnsureContourCap(FontOutline *o)
{
    uint32_t ncap;
    FontContour *n;
    if (o->contourCount < o->contourCap) {
        return RoboOk;
    }
    ncap = o->contourCap ? o->contourCap * 2u : 4u;
    if (ncap > RoboFontOutlineMaxContours) {
        ncap = RoboFontOutlineMaxContours;
    }
    if (o->contourCount >= ncap) {
        return RoboParseError;
    }
    n = (FontContour *)RoboCalloc(ncap, sizeof(*n));
    if (!n) {
        return RoboOutOfMemory;
    }
    if (o->contours) {
        memcpy(n, o->contours, (size_t)o->contourCount * sizeof(*n));
        RoboFree(o->contours);
    }
    o->contours = n;
    o->contourCap = ncap;
    return RoboOk;
}

static __attribute__((unused)) Result EmitSeg(OutlineBuilder *b, FontPathSeg seg)
{
    FontContour *c;
    if (!b->cur) {
        return RoboParseError;
    }
    if (b->o->totalSegs >= RoboFontOutlineMaxSegments) {
        return RoboParseError;
    }
    c = b->cur;
    if (c->segCount >= b->curSegCap) {
        uint32_t na = b->curSegCap ? b->curSegCap * 2u : 8u;
        FontPathSeg *ns;
        if (na > RoboFontOutlineMaxSegments) {
            na = RoboFontOutlineMaxSegments;
        }
        if (c->segCount >= na) {
            return RoboParseError;
        }
        ns = (FontPathSeg *)RoboCalloc(na, sizeof(*ns));
        if (!ns) {
            return RoboOutOfMemory;
        }
        if (c->segs) {
            memcpy(ns, c->segs, (size_t)c->segCount * sizeof(*ns));
            RoboFree(c->segs);
        }
        c->segs = ns;
        b->curSegCap = na;
    }
    c->segs[c->segCount++] = seg;
    b->o->totalSegs++;
    return RoboOk;
}

static __attribute__((unused)) Result BeginContour(OutlineBuilder *b)
{
    Result res = EnsureContourCap(b->o);
    if (res != RoboOk) {
        return res;
    }
    if (b->o->contourCount >= RoboFontOutlineMaxContours) {
        return RoboParseError;
    }
    b->cur = &b->o->contours[b->o->contourCount++];
    memset(b->cur, 0, sizeof(*b->cur));
    b->curSegCap = 0u;
    b->pathOpen = 0;
    return RoboOk;
}

static __attribute__((unused)) Result BuilderMove(OutlineBuilder *b,
                                  FontUnit x,
                                  FontUnit y)
{
    FontPathSeg seg;
    FontPoint pt;
    Result res = BeginContour(b);
    if (res != RoboOk) {
        return res;
    }
    res = MapPt(b, x, y, &pt);
    if (res != RoboOk) {
        return res;
    }
    memset(&seg, 0, sizeof(seg));
    seg.op = RoboFontPathMove;
    seg.p = pt;
    res = EmitSeg(b, seg);
    if (res != RoboOk) {
        return res;
    }
    b->cx = pt.x;
    b->cy = pt.y;
    b->sx = pt.x;
    b->sy = pt.y;
    b->pathOpen = 1;
    return RoboOk;
}

static __attribute__((unused)) Result BuilderLine(OutlineBuilder *b,
                                  FontUnit x,
                                  FontUnit y)
{
    FontPathSeg seg;
    FontPoint pt;
    Result res;
    if (!b->pathOpen) {
        return BuilderMove(b, x, y);
    }
    res = MapPt(b, x, y, &pt);
    if (res != RoboOk) {
        return res;
    }
    memset(&seg, 0, sizeof(seg));
    seg.op = RoboFontPathLine;
    seg.p = pt;
    res = EmitSeg(b, seg);
    if (res != RoboOk) {
        return res;
    }
    b->cx = pt.x;
    b->cy = pt.y;
    return RoboOk;
}

static __attribute__((unused)) Result BuilderQuad(OutlineBuilder *b,
                                  FontUnit cx,
                                  FontUnit cy,
                                  FontUnit x,
                                  FontUnit y)
{
    FontPathSeg seg;
    FontPoint c1, p;
    Result res;
    if (!b->pathOpen) {
        return RoboParseError;
    }
    res = MapPt(b, cx, cy, &c1);
    if (res != RoboOk) {
        return res;
    }
    res = MapPt(b, x, y, &p);
    if (res != RoboOk) {
        return res;
    }
    memset(&seg, 0, sizeof(seg));
    seg.op = RoboFontPathQuad;
    seg.c1 = c1;
    seg.p = p;
    res = EmitSeg(b, seg);
    if (res != RoboOk) {
        return res;
    }
    b->cx = p.x;
    b->cy = p.y;
    return RoboOk;
}

static __attribute__((unused)) Result BuilderCubic(OutlineBuilder *b,
                                   FontUnit c1x,
                                   FontUnit c1y,
                                   FontUnit c2x,
                                   FontUnit c2y,
                                   FontUnit x,
                                   FontUnit y)
{
    FontPathSeg seg;
    FontPoint c1, c2, p;
    Result res;
    if (!b->pathOpen) {
        return RoboParseError;
    }
    res = MapPt(b, c1x, c1y, &c1);
    if (res != RoboOk) {
        return res;
    }
    res = MapPt(b, c2x, c2y, &c2);
    if (res != RoboOk) {
        return res;
    }
    res = MapPt(b, x, y, &p);
    if (res != RoboOk) {
        return res;
    }
    memset(&seg, 0, sizeof(seg));
    seg.op = RoboFontPathCubic;
    seg.c1 = c1;
    seg.c2 = c2;
    seg.p = p;
    res = EmitSeg(b, seg);
    if (res != RoboOk) {
        return res;
    }
    b->cx = p.x;
    b->cy = p.y;
    return RoboOk;
}

static __attribute__((unused)) Result BuilderClose(OutlineBuilder *b)
{
    FontPathSeg seg;
    Result res;
    if (!b->pathOpen || !b->cur) {
        return RoboOk;
    }
    if (b->cx != b->sx || b->cy != b->sy) {
        memset(&seg, 0, sizeof(seg));
        seg.op = RoboFontPathLine;
        seg.p.x = b->sx;
        seg.p.y = b->sy;
        res = EmitSeg(b, seg);
        if (res != RoboOk) {
            return res;
        }
        b->cx = b->sx;
        b->cy = b->sy;
    }
    memset(&seg, 0, sizeof(seg));
    seg.op = RoboFontPathClose;
    res = EmitSeg(b, seg);
    if (res != RoboOk) {
        return res;
    }
    b->cur->closed = 1u;
    b->pathOpen = 0;
    return RoboOk;
}

static __attribute__((unused)) void FinalizeBounds(FontOutline *o)
{
    uint32_t i, j;
    FontBbox gbb;
    int ginit = 0;
    for (i = 0u; i < o->contourCount; i++) {
        FontContour *c = &o->contours[i];
        FontBbox cbb;
        int cinit = 0;
        FontUnit px = 0, py = 0;
        int have = 0;
        for (j = 0u; j < c->segCount; j++) {
            const FontPathSeg *s = &c->segs[j];
            if (s->op == RoboFontPathMove) {
                px = s->p.x;
                py = s->p.y;
                have = 1;
                BboxInclude(&cbb, &cinit, px, py);
                continue;
            }
            if (!have) {
                continue;
            }
            IncludeSegBounds(&cbb, &cinit, s, px, py);
            if (s->op != RoboFontPathClose) {
                px = s->p.x;
                py = s->p.y;
            }
        }
        if (cinit) {
            c->bounds = cbb;
            c->hasBounds = 1u;
            BboxInclude(&gbb, &ginit, cbb.xMin, cbb.yMin);
            BboxInclude(&gbb, &ginit, cbb.xMax, cbb.yMax);
        }
    }
    if (ginit) {
        o->bounds = gbb;
        o->hasBounds = 1u;
    }
}

static __attribute__((unused)) FontOutline *OutlineNew(GlyphId gid,
                                        FontOutlineTech tech)
{
    FontOutline *o = (FontOutline *)RoboCalloc(1u, sizeof(*o));
    if (!o) {
        return NULL;
    }
    o->refs = 1u;
    o->glyphId = gid;
    o->tech = tech;
    o->status = RoboFontOutlineStatusOk;
    return o;
}

static __attribute__((unused)) void OutlineSetStatus(FontOutline *o, FontOutlineStatus s)
{
    o->status = s;
    if (s == RoboFontOutlineStatusEmpty) {
        o->hasBounds = 0u;
    }
}

#endif /* !RoboOutlineStructOnly */

#endif /* ROBO_FONT_OUTLINE_INTERNAL_H */
