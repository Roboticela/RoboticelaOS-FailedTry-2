#include "RoboFont/RoboFontOutline.h"

#include "RoboFontOutlineInternal.h"

#include "RoboFont/RoboFontTt.h"

#include <stdint.h>

#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif
#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif

typedef struct {
    int16_t x, y;
    uint8_t on;
} TtPt;

static Result AppendSimpleContour(OutlineBuilder *b,
                                           const int16_t *xs,
                                           const int16_t *ys,
                                           const uint8_t *flags,
                                           uint16_t start,
                                           uint16_t end)
{
    uint16_t n = (uint16_t)(end - start + 1u);
    TtPt *exp = NULL;
    uint32_t expN = 0u;
    uint32_t expCap = 0u;
    uint32_t i;
    uint32_t startOn;
    Result res = RoboOk;

    if (n == 0u) {
        return RoboOk;
    }
    expCap = (uint32_t)n * 2u + 2u;
    if (expCap > RoboFontOutlineMaxPoints) {
        return RoboParseError;
    }
    exp = (TtPt *)RoboCalloc(expCap, sizeof(*exp));
    if (!exp) {
        return RoboOutOfMemory;
    }

    /* Expand implied on-curve midpoints between consecutive off-curve points. */
    for (i = 0u; i < n; i++) {
        uint16_t idx = (uint16_t)(start + i);
        uint16_t nextI = (uint16_t)((i + 1u) % n);
        uint16_t next = (uint16_t)(start + nextI);
        uint8_t on = (uint8_t)((flags[idx] & RoboFontGlyfFlagOnCurve) != 0u);
        uint8_t nextOn = (uint8_t)((flags[next] & RoboFontGlyfFlagOnCurve) != 0u);

        if (expN >= expCap) {
            res = RoboParseError;
            goto done;
        }
        exp[expN].x = xs[idx];
        exp[expN].y = ys[idx];
        exp[expN].on = on;
        expN++;

        if (!on && !nextOn) {
            int32_t mx = ((int32_t)xs[idx] + (int32_t)xs[next]) / 2;
            int32_t my = ((int32_t)ys[idx] + (int32_t)ys[next]) / 2;
            if (mx < -32768 || mx > 32767 || my < -32768 || my > 32767) {
                res = RoboParseError;
                goto done;
            }
            if (expN >= expCap) {
                res = RoboParseError;
                goto done;
            }
            exp[expN].x = (int16_t)mx;
            exp[expN].y = (int16_t)my;
            exp[expN].on = 1u;
            expN++;
        }
    }

    if (expN == 0u) {
        goto done;
    }

    /* Find first on-curve point. */
    startOn = 0u;
    for (i = 0u; i < expN; i++) {
        if (exp[i].on) {
            startOn = i;
            break;
        }
    }
    /* If all off (degenerate after expansion shouldn't happen), treat first as on. */
    if (!exp[startOn].on) {
        exp[startOn].on = 1u;
    }

    res = BuilderMove(b, exp[startOn].x, exp[startOn].y);
    if (res != RoboOk) {
        goto done;
    }

    i = startOn;
    do {
        uint32_t ni = (i + 1u) % expN;
        if (exp[ni].on) {
            res = BuilderLine(b, exp[ni].x, exp[ni].y);
            if (res != RoboOk) {
                goto done;
            }
            i = ni;
        } else {
            uint32_t n2 = (ni + 1u) % expN;
            if (!exp[n2].on) {
                res = RoboParseError;
                goto done;
            }
            res = BuilderQuad(b, exp[ni].x, exp[ni].y, exp[n2].x, exp[n2].y);
            if (res != RoboOk) {
                goto done;
            }
            i = n2;
        }
    } while (i != startOn);

    res = BuilderClose(b);

done:
    RoboFree(exp);
    return res;
}

static Result ConvertSimple(OutlineBuilder *b,
                                    const FontGlyfGlyph *g)
{
    uint16_t c;
    uint16_t start = 0u;
    for (c = 0u; c < g->contourCount; c++) {
        uint16_t end = g->endPts[c];
        Result res;
        if (end < start || end >= g->pointCount) {
            return RoboParseError;
        }
        res = AppendSimpleContour(b, g->xCoords, g->yCoords, g->flags, start, end);
        if (res != RoboOk) {
            return res;
        }
        start = (uint16_t)(end + 1u);
    }
    return RoboOk;
}

static void ComponentToAffine(const FontGlyfComponent *comp,
                                FontAffine *m)
{
    FontAffineIdentity(m);
    if (comp->hasTransform) {
        m->xscale = comp->xscale;
        m->scale01 = comp->scale01;
        m->scale10 = comp->scale10;
        m->yscale = comp->yscale;
    }
    if ((comp->flags & RoboFontGlyfCompArgsXy) != 0u) {
        m->dx = comp->arg1;
        m->dy = comp->arg2;
        if ((comp->flags & RoboFontGlyfCompRoundXy) != 0u) {
            /* already integer FUnits */
        }
    }
    /* Point-matching args: unsupported → identity translation (geometry still valid). */
}

typedef struct {
    FontTt *tt;
    uint8_t *visiting;
    uint16_t numGlyphs;
    uint32_t componentBudget;
} TtFlattenCtx;

static Result FlattenGlyph(TtFlattenCtx *ctx,
                                   GlyphId gid,
                                   uint32_t depth,
                                   const FontAffine *parent,
                                   OutlineBuilder *b);

static Result FlattenGlyph(TtFlattenCtx *ctx,
                                   GlyphId gid,
                                   uint32_t depth,
                                   const FontAffine *parent,
                                   OutlineBuilder *b)
{
    FontGlyfGlyph g;
    Result res;
    uint16_t i;

    if (depth > RoboFontOutlineMaxCompositeDepth) {
        return RoboParseError;
    }
    if (gid >= ctx->numGlyphs) {
        return RoboParseError;
    }
    if (ctx->visiting[gid]) {
        return RoboParseError; /* cycle */
    }

    memset(&g, 0, sizeof(g));
    res = FontTtParseGlyph(ctx->tt, gid, &g);
    if (res != RoboOk) {
        return res;
    }

    if (g.kind == RoboFontGlyfEmpty) {
        FontGlyfGlyphClear(&g);
        return RoboOk;
    }

    if (g.kind == RoboFontGlyfSimple) {
        FontAffine saved = b->xform;
        int savedApply = b->applyXform;
        b->xform = *parent;
        b->applyXform = 1;
        res = ConvertSimple(b, &g);
        b->xform = saved;
        b->applyXform = savedApply;
        FontGlyfGlyphClear(&g);
        return res;
    }

    if (g.kind != RoboFontGlyfComposite) {
        FontGlyfGlyphClear(&g);
        return RoboParseError;
    }

    ctx->visiting[gid] = 1u;
    for (i = 0u; i < g.componentCount; i++) {
        FontAffine local;
        FontAffine combined;
        if (ctx->componentBudget == 0u) {
            res = RoboParseError;
            break;
        }
        ctx->componentBudget--;
        ComponentToAffine(&g.components[i], &local);
        res = FontAffineMultiply(parent, &local, &combined);
        if (res != RoboOk) {
            break;
        }
        res = FlattenGlyph(ctx, g.components[i].glyphIndex, depth + 1u, &combined, b);
        if (res != RoboOk) {
            break;
        }
    }
    ctx->visiting[gid] = 0u;
    FontGlyfGlyphClear(&g);
    return res;
}

Result FontOutlineFromTt(FontTt *tt,
                                        GlyphId glyphId,
                                        FontOutline **out)
{
    FontOutline *o;
    OutlineBuilder b;
    TtFlattenCtx ctx;
    FontAffine id;
    uint16_t ng;
    Result res;

    if (!tt || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    ng = FontTtNumGlyphs(tt);
    if (glyphId >= ng) {
        o = OutlineNew(glyphId, RoboFontOutlineTruetype);
        if (!o) {
            return RoboOutOfMemory;
        }
        OutlineSetStatus(o, RoboFontOutlineStatusInvalidId);
        *out = o;
        return RoboOk;
    }

    o = OutlineNew(glyphId, RoboFontOutlineTruetype);
    if (!o) {
        return RoboOutOfMemory;
    }
    BuilderInit(&b, o);
    FontAffineIdentity(&id);

    memset(&ctx, 0, sizeof(ctx));
    ctx.tt = tt;
    ctx.numGlyphs = ng;
    ctx.componentBudget = RoboFontOutlineMaxCompositeComponents;
    ctx.visiting = (uint8_t *)RoboCalloc(ng, 1u);
    if (!ctx.visiting) {
        FontOutlineRelease(o);
        return RoboOutOfMemory;
    }

    res = FlattenGlyph(&ctx, glyphId, 0u, &id, &b);
    RoboFree(ctx.visiting);
    if (res != RoboOk) {
        OutlineSetStatus(o, res == RoboOutOfMemory ? RoboFontOutlineStatusLimit
                                                        : RoboFontOutlineStatusMalformed);
        /* still return outline object with status for diagnostics */
        if (res == RoboOutOfMemory) {
            FontOutlineRelease(o);
            return res;
        }
        *out = o;
        return RoboOk;
    }

    if (o->contourCount == 0u) {
        OutlineSetStatus(o, RoboFontOutlineStatusEmpty);
    } else {
        FinalizeBounds(o);
        OutlineSetStatus(o, RoboFontOutlineStatusOk);
    }
    *out = o;
    return RoboOk;
}
