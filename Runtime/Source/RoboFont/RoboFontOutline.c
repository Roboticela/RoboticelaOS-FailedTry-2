#include "RoboFont/RoboFontOutline.h"

#define RoboOutlineStructOnly
#include "RoboFontOutlineInternal.h"

#include "RoboFont/RoboFontSource.h"
#include "RoboMem/RoboMem.h"

#include <stdio.h>
#include <stdint.h>

#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif
#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif

void FontAffineIdentity(FontAffine *m)
{
    if (!m) {
        return;
    }
    m->xscale = 0x4000;
    m->scale01 = 0;
    m->scale10 = 0;
    m->yscale = 0x4000;
    m->dx = 0;
    m->dy = 0;
}

Result FontAffineMultiply(const FontAffine *a,
                                        const FontAffine *b,
                                        FontAffine *out)
{
    int64_t xscale, scale01, scale10, yscale, dx, dy;
    if (!a || !b || !out) {
        return RoboInvalidArgument;
    }
    /* out = a ∘ b  (apply b first, then a). F2DOT14 product / 16384 */
    xscale = ((int64_t)a->xscale * (int64_t)b->xscale +
              (int64_t)a->scale10 * (int64_t)b->scale01) /
             16384;
    scale01 = ((int64_t)a->scale01 * (int64_t)b->xscale +
               (int64_t)a->yscale * (int64_t)b->scale01) /
              16384;
    scale10 = ((int64_t)a->xscale * (int64_t)b->scale10 +
               (int64_t)a->scale10 * (int64_t)b->yscale) /
              16384;
    yscale = ((int64_t)a->scale01 * (int64_t)b->scale10 +
              (int64_t)a->yscale * (int64_t)b->yscale) /
             16384;
    dx = ((int64_t)a->xscale * (int64_t)b->dx + (int64_t)a->scale10 * (int64_t)b->dy) /
             16384 +
         (int64_t)a->dx;
    dy = ((int64_t)a->scale01 * (int64_t)b->dx + (int64_t)a->yscale * (int64_t)b->dy) /
             16384 +
         (int64_t)a->dy;
    if (xscale < -32768 || xscale > 32767 || scale01 < -32768 || scale01 > 32767 ||
        scale10 < -32768 || scale10 > 32767 || yscale < -32768 || yscale > 32767 ||
        dx < INT32_MIN || dx > INT32_MAX || dy < INT32_MIN || dy > INT32_MAX) {
        return RoboParseError;
    }
    out->xscale = (int16_t)xscale;
    out->scale01 = (int16_t)scale01;
    out->scale10 = (int16_t)scale10;
    out->yscale = (int16_t)yscale;
    out->dx = (int32_t)dx;
    out->dy = (int32_t)dy;
    return RoboOk;
}

Result FontAffineApply(const FontAffine *m,
                                     FontUnit x,
                                     FontUnit y,
                                     FontUnit *outX,
                                     FontUnit *outY)
{
    int64_t nx, ny;
    if (!m || !outX || !outY) {
        return RoboInvalidArgument;
    }
    nx = ((int64_t)m->xscale * (int64_t)x + (int64_t)m->scale10 * (int64_t)y) / 16384 +
         (int64_t)m->dx;
    ny = ((int64_t)m->scale01 * (int64_t)x + (int64_t)m->yscale * (int64_t)y) / 16384 +
         (int64_t)m->dy;
    if (nx < INT32_MIN || nx > INT32_MAX || ny < INT32_MIN || ny > INT32_MAX) {
        return RoboParseError;
    }
    *outX = (FontUnit)nx;
    *outY = (FontUnit)ny;
    return RoboOk;
}

void FontOutlineRetain(FontOutline *o)
{
    if (o) {
        o->refs++;
    }
}

void FontOutlineRelease(FontOutline *o)
{
    uint32_t i;
    if (!o) {
        return;
    }
    if (o->refs == 0u) {
        return;
    }
    o->refs--;
    if (o->refs != 0u) {
        return;
    }
    for (i = 0u; i < o->contourCount; i++) {
        RoboFree(o->contours[i].segs);
    }
    RoboFree(o->contours);
    RoboFree(o);
}

GlyphId FontOutlineGlyphId(const FontOutline *o)
{
    return o ? o->glyphId : 0u;
}

FontOutlineStatus MakeFontOutlineStatus(const FontOutline *o)
{
    return o ? o->status : RoboFontOutlineStatusUnavailable;
}

FontOutlineTech MakeFontOutlineTech(const FontOutline *o)
{
    return o ? o->tech : RoboFontOutlineUnknown;
}

uint32_t FontOutlineContourCount(const FontOutline *o)
{
    return o ? o->contourCount : 0u;
}

const FontContour *FontOutlineContour(const FontOutline *o,
                                                     uint32_t index)
{
    if (!o || index >= o->contourCount) {
        return NULL;
    }
    return &o->contours[index];
}

int FontOutlineHasBounds(const FontOutline *o)
{
    return o && o->hasBounds;
}

Result FontOutlineBounds(const FontOutline *o,
                                       FontBbox *out)
{
    if (!o || !out) {
        return RoboInvalidArgument;
    }
    if (!o->hasBounds) {
        return RoboNotFound;
    }
    *out = o->bounds;
    return RoboOk;
}

uint32_t FontOutlineSegmentCount(const FontOutline *o)
{
    return o ? o->totalSegs : 0u;
}

static const char *OpName(FontPathOp op)
{
    switch (op) {
    case RoboFontPathMove:
        return "MOVE";
    case RoboFontPathLine:
        return "LINE";
    case RoboFontPathQuad:
        return "QUAD";
    case RoboFontPathCubic:
        return "CUBIC";
    case RoboFontPathClose:
        return "CLOSE";
    default:
        return "?";
    }
}

Result FontOutlineDump(const FontOutline *o,
                                     char *buf,
                                     size_t cap,
                                     size_t *outLen)
{
    size_t n = 0u;
    uint32_t i, j;
    int wr;
    if (!o || !buf || cap == 0u) {
        return RoboInvalidArgument;
    }
    buf[0] = '\0';
    wr = snprintf(buf + n, cap - n,
                  "gid=%u status=%d tech=%d contours=%u bounds=%d,%d,%d,%d\n",
                  (unsigned)o->glyphId, (int)o->status, (int)o->tech,
                  (unsigned)o->contourCount,
                  o->hasBounds ? (int)o->bounds.xMin : 0,
                  o->hasBounds ? (int)o->bounds.yMin : 0,
                  o->hasBounds ? (int)o->bounds.xMax : 0,
                  o->hasBounds ? (int)o->bounds.yMax : 0);
    if (wr < 0 || (size_t)wr >= cap - n) {
        return RoboParseError;
    }
    n += (size_t)wr;
    for (i = 0u; i < o->contourCount; i++) {
        const FontContour *c = &o->contours[i];
        wr = snprintf(buf + n, cap - n, "C %u closed=%u segs=%u\n", (unsigned)i,
                      (unsigned)c->closed, (unsigned)c->segCount);
        if (wr < 0 || (size_t)wr >= cap - n) {
            return RoboParseError;
        }
        n += (size_t)wr;
        for (j = 0u; j < c->segCount; j++) {
            const FontPathSeg *s = &c->segs[j];
            if (s->op == RoboFontPathQuad) {
                wr = snprintf(buf + n, cap - n, "  %s %d %d %d %d\n", OpName(s->op),
                              (int)s->c1.x, (int)s->c1.y, (int)s->p.x, (int)s->p.y);
            } else if (s->op == RoboFontPathCubic) {
                wr = snprintf(buf + n, cap - n, "  %s %d %d %d %d %d %d\n",
                              OpName(s->op), (int)s->c1.x, (int)s->c1.y, (int)s->c2.x,
                              (int)s->c2.y, (int)s->p.x, (int)s->p.y);
            } else if (s->op == RoboFontPathClose) {
                wr = snprintf(buf + n, cap - n, "  %s\n", OpName(s->op));
            } else {
                wr = snprintf(buf + n, cap - n, "  %s %d %d\n", OpName(s->op),
                              (int)s->p.x, (int)s->p.y);
            }
            if (wr < 0 || (size_t)wr >= cap - n) {
                return RoboParseError;
            }
            n += (size_t)wr;
        }
    }
    if (outLen) {
        *outLen = n;
    }
    return RoboOk;
}

Result FontOutlineFromSfnt(FontSfnt *sfnt,
                                          FontTt *ttOpt,
                                          FontCffFont *cffOpt,
                                          GlyphId glyphId,
                                          FontOutline **out)
{
    FontOutlineTech tech;
    FontTt *tt = ttOpt;
    FontCffFont *cff = cffOpt;
    Result res;
    int ownTt = 0;
    int ownCff = 0;

    if (!sfnt || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    tech = FontOtDetectOutline(sfnt);
    if (tech == RoboFontOutlineTruetype) {
        if (!tt) {
            res = FontTtParse(sfnt, RoboFontTtFlagsNone, &tt);
            if (res != RoboOk) {
                return res;
            }
            ownTt = 1;
        }
        res = FontOutlineFromTt(tt, glyphId, out);
        if (ownTt) {
            FontTtRelease(tt);
        }
        return res;
    }
    if (tech == RoboFontOutlineCff || tech == RoboFontOutlineCff2) {
        if (!cff) {
            res = FontCffParse(sfnt, RoboFontCffFlagsParseAll, &cff);
            if (res != RoboOk) {
                return res;
            }
            ownCff = 1;
        }
        res = FontOutlineFromCff(cff, glyphId, out);
        if (ownCff) {
            FontCffRelease(cff);
        }
        return res;
    }
    return RoboNotSupported;
}

/* ---- Cache ---- */

struct FontOutlineCache {
    uint32_t refs;
    uint32_t count;
    uint32_t nextSlot;
    GlyphId gids[RoboFontOutlineCacheMax];
    FontOutline *outs[RoboFontOutlineCacheMax];
};

Result FontOutlineCacheCreate(FontOutlineCache **out)
{
    FontOutlineCache *c;
    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    c = (FontOutlineCache *)RoboCalloc(1u, sizeof(*c));
    if (!c) {
        return RoboOutOfMemory;
    }
    c->refs = 1u;
    *out = c;
    return RoboOk;
}

void FontOutlineCacheRetain(FontOutlineCache *c)
{
    if (c) {
        c->refs++;
    }
}

void FontOutlineCacheRelease(FontOutlineCache *c)
{
    uint32_t i;
    if (!c) {
        return;
    }
    if (c->refs == 0u) {
        return;
    }
    c->refs--;
    if (c->refs != 0u) {
        return;
    }
    for (i = 0u; i < c->count; i++) {
        FontOutlineRelease(c->outs[i]);
    }
    RoboFree(c);
}

Result FontOutlineCacheGet(FontOutlineCache *cache,
                                          FontSfnt *sfnt,
                                          FontTt *ttOpt,
                                          FontCffFont *cffOpt,
                                          GlyphId glyphId,
                                          FontOutline **out)
{
    uint32_t i;
    FontOutline *o = NULL;
    Result res;
    if (!cache || !sfnt || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    for (i = 0u; i < cache->count; i++) {
        if (cache->gids[i] == glyphId && cache->outs[i]) {
            FontOutlineRetain(cache->outs[i]);
            *out = cache->outs[i];
            return RoboOk;
        }
    }
    res = FontOutlineFromSfnt(sfnt, ttOpt, cffOpt, glyphId, &o);
    if (res != RoboOk) {
        return res;
    }
    if (cache->count < RoboFontOutlineCacheMax) {
        cache->gids[cache->count] = glyphId;
        cache->outs[cache->count] = o;
        FontOutlineRetain(o);
        cache->count++;
    } else {
        /* FIFO eviction */
        uint32_t slot = cache->nextSlot % RoboFontOutlineCacheMax;
        FontOutlineRelease(cache->outs[slot]);
        cache->gids[slot] = glyphId;
        cache->outs[slot] = o;
        FontOutlineRetain(o);
        cache->nextSlot = slot + 1u;
    }
    *out = o;
    return RoboOk;
}

Result FontOutlineFuzzParse(const uint8_t *data, size_t size)
{
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontOutline *o = NULL;
    if (!data || size == 0u) {
        return RoboOk;
    }
    if (FontSourceCreateMemoryBorrow(data, size, &src) != RoboOk) {
        return RoboOk;
    }
    if (FontSfntParse(src, 0u, RoboFontSfntFlagsNone, &sfnt) == RoboOk) {
        (void)FontOutlineFromSfnt(sfnt, NULL, NULL, 0u, &o);
        if (o) {
            FontOutlineRelease(o);
        }
        FontSfntRelease(sfnt);
    }
    FontSourceRelease(src);
    return RoboOk;
}
