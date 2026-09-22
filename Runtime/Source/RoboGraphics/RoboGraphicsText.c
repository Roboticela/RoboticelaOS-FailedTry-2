#include "RoboGraphics/RoboGraphicsText.h"

#include "RoboGraphics/RoboGraphicsLine.h"

#include <string.h>

enum { RoboGfxCurveSubdiv = 8 };

void GfxGlyphDrawInit(GfxGlyphDraw *draw)
{
    if (!draw) {
        return;
    }
    memset(draw, 0, sizeof(*draw));
    draw->mode = RoboGfxGlyphModeOutlineStroke;
    draw->color = GfxRgb8(0u, 0u, 0u);
}

static int FloatOk(float v)
{
    /* Reject NaN and Inf without relying on isfinite (MSVC / freestanding). */
    return (v == v) && (v <= 3.402823466e+38f) && (v >= -3.402823466e+38f);
}

Result GfxFontScale(uint16_t unitsPerEm,
                                  float sizePx,
                                  float *outScale)
{
    if (!outScale) {
        return RoboInvalidArgument;
    }
    *outScale = 0.0f;
    if (unitsPerEm == 0u || !FloatOk(sizePx) || sizePx <= 0.0f) {
        return RoboInvalidArgument;
    }
    *outScale = sizePx / (float)unitsPerEm;
    if (!FloatOk(*outScale)) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

Result GfxFontPointToDevice(FontUnit fontX,
                                           FontUnit fontY,
                                           uint16_t unitsPerEm,
                                           float sizePx,
                                           GfxPointF origin,
                                           GfxPointF *outDevice)
{
    float scale = 0.0f;
    Result res;

    if (!outDevice || !FloatOk(origin.x) || !FloatOk(origin.y)) {
        return RoboInvalidArgument;
    }
    res = GfxFontScale(unitsPerEm, sizePx, &scale);
    if (res != RoboOk) {
        return res;
    }
    outDevice->x = origin.x + (float)fontX * scale;
    outDevice->y = origin.y - (float)fontY * scale;
    if (!FloatOk(outDevice->x) || !FloatOk(outDevice->y)) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

GfxPoint GfxGlyphSnapPoint(GfxPointF p)
{
    return GfxPointFRound(p);
}

static Result FaceUpem(FontFace *face, uint16_t *outUpem)
{
    FontMetrics layout;
    Result res;

    if (!face || !outUpem) {
        return RoboInvalidArgument;
    }
    *outUpem = 0u;
    res = FontFaceParseMetrics(face);
    if (res != RoboOk) {
        return res;
    }
    res = FontFaceLayoutMetrics(face, &layout);
    if (res != RoboOk) {
        return res;
    }
    if (!layout.valid || layout.unitsPerEm == 0u) {
        return RoboInvalidArgument;
    }
    *outUpem = layout.unitsPerEm;
    return RoboOk;
}

Result GetGfxGlyphMetricsDevice(FontFace *face,
                                           GlyphId glyphId,
                                           float sizePx,
                                           GfxGlyphMetricsDevice *out)
{
    FontGlyphMetrics gm;
    uint16_t upem = 0u;
    float scale = 0.0f;
    Result res;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (!face) {
        return RoboInvalidArgument;
    }

    res = FaceUpem(face, &upem);
    if (res != RoboOk) {
        return res;
    }
    res = GfxFontScale(upem, sizePx, &scale);
    if (res != RoboOk) {
        return res;
    }

    res = FontFaceGlyphMetrics(face, glyphId, &gm);
    if (res != RoboOk) {
        return res;
    }
    if (gm.status == RoboFontGlyphMetricsInvalidId) {
        return RoboInvalidArgument;
    }
    if (gm.status != RoboFontGlyphMetricsOk) {
        return RoboNotSupported;
    }

    out->advanceWidth = (float)gm.advanceWidth * scale;
    out->leftSideBearing = (float)gm.leftSideBearing * scale;
    if (gm.hasBbox) {
        float x0 = (float)gm.bbox.xMin * scale;
        float x1 = (float)gm.bbox.xMax * scale;
        float yTop = -(float)gm.bbox.yMax * scale;    /* above → smaller y */
        float yBot = -(float)gm.bbox.yMin * scale;    /* below → larger y */
        out->bounds.x = x0;
        out->bounds.y = yTop;
        out->bounds.width = x1 - x0;
        out->bounds.height = yBot - yTop;
        out->hasBounds = 1u;
        out->ascent = (gm.bbox.yMax > 0) ? (float)gm.bbox.yMax * scale : 0.0f;
        out->descent = (gm.bbox.yMin < 0) ? (float)(-gm.bbox.yMin) * scale
                                           : 0.0f;
    }
    return RoboOk;
}

static Result MapSegPoint(FontUnit fx,
                                   FontUnit fy,
                                   uint16_t upem,
                                   float sizePx,
                                   GfxPointF origin,
                                   GfxPoint *outI)
{
    GfxPointF d;
    Result res =
        GfxFontPointToDevice(fx, fy, upem, sizePx, origin, &d);
    if (res != RoboOk) {
        return res;
    }
    *outI = GfxGlyphSnapPoint(d);
    return RoboOk;
}

static Result EmitLine(GfxPath *path, GfxPoint p)
{
    if (!GfxPathHasOpenContour(path)) {
        return GfxPathMoveTo(path, p);
    }
    return GfxPathLineTo(path, p);
}

static Result FlattenQuad(GfxPath *path,
                                  GfxPoint p0,
                                  GfxPoint c1,
                                  GfxPoint p1)
{
    int i;
    Result res;

    for (i = 1; i <= RoboGfxCurveSubdiv; i++) {
        float t = (float)i / (float)RoboGfxCurveSubdiv;
        float u = 1.0f - t;
        float x = u * u * (float)p0.x + 2.0f * u * t * (float)c1.x +
                  t * t * (float)p1.x;
        float y = u * u * (float)p0.y + 2.0f * u * t * (float)c1.y +
                  t * t * (float)p1.y;
        res = EmitLine(path, GfxGlyphSnapPoint(MakeGfxPointF(x, y)));
        if (res != RoboOk) {
            return res;
        }
    }
    return RoboOk;
}

static Result FlattenCubic(GfxPath *path,
                                   GfxPoint p0,
                                   GfxPoint c1,
                                   GfxPoint c2,
                                   GfxPoint p1)
{
    int i;
    Result res;

    for (i = 1; i <= RoboGfxCurveSubdiv; i++) {
        float t = (float)i / (float)RoboGfxCurveSubdiv;
        float u = 1.0f - t;
        float uu = u * u;
        float tt = t * t;
        float x = uu * u * (float)p0.x + 3.0f * uu * t * (float)c1.x +
                  3.0f * u * tt * (float)c2.x + tt * t * (float)p1.x;
        float y = uu * u * (float)p0.y + 3.0f * uu * t * (float)c1.y +
                  3.0f * u * tt * (float)c2.y + tt * t * (float)p1.y;
        res = EmitLine(path, GfxGlyphSnapPoint(MakeGfxPointF(x, y)));
        if (res != RoboOk) {
            return res;
        }
    }
    return RoboOk;
}

Result GfxOutlineToPath(const FontOutline *outline,
                                       uint16_t unitsPerEm,
                                       float sizePx,
                                       GfxPointF origin,
                                       GfxPath *path)
{
    uint32_t ci;
    uint32_t cc;
    GfxPoint cur;
    int haveCur = 0;
    float scale = 0.0f;
    Result res;

    if (!outline || !path) {
        return RoboInvalidArgument;
    }
    res = GfxFontScale(unitsPerEm, sizePx, &scale);
    if (res != RoboOk) {
        return res;
    }
    if (!FloatOk(origin.x) || !FloatOk(origin.y)) {
        return RoboInvalidArgument;
    }

    GfxPathReset(path);

    switch (MakeFontOutlineStatus(outline)) {
    case RoboFontOutlineStatusOk:
        break;
    case RoboFontOutlineStatusEmpty:
        return RoboOk;
    case RoboFontOutlineStatusInvalidId:
        return RoboInvalidArgument;
    case RoboFontOutlineStatusMalformed:
        return RoboInvalidArgument;
    case RoboFontOutlineStatusUnavailable:
    case RoboFontOutlineStatusUnsupported:
        return RoboNotSupported;
    case RoboFontOutlineStatusLimit:
    case RoboFontOutlineStatusOverflow:
        return RoboInvalidArgument;
    default:
        return RoboInvalidArgument;
    }

    cc = FontOutlineContourCount(outline);
    for (ci = 0u; ci < cc; ci++) {
        const FontContour *contour = FontOutlineContour(outline, ci);
        uint32_t si;

        if (!contour) {
            return RoboInvalidArgument;
        }
        haveCur = 0;
        for (si = 0u; si < contour->segCount; si++) {
            const FontPathSeg *seg = &contour->segs[si];
            GfxPoint p;
            GfxPoint c1;
            GfxPoint c2;

            switch (seg->op) {
            case RoboFontPathMove:
                res = MapSegPoint(seg->p.x, seg->p.y, unitsPerEm, sizePx,
                                    origin, &p);
                if (res != RoboOk) {
                    return res;
                }
                res = GfxPathMoveTo(path, p);
                if (res != RoboOk) {
                    return res;
                }
                cur = p;
                haveCur = 1;
                break;
            case RoboFontPathLine:
                res = MapSegPoint(seg->p.x, seg->p.y, unitsPerEm, sizePx,
                                    origin, &p);
                if (res != RoboOk) {
                    return res;
                }
                res = EmitLine(path, p);
                if (res != RoboOk) {
                    return res;
                }
                cur = p;
                haveCur = 1;
                break;
            case RoboFontPathQuad:
                if (!haveCur) {
                    return RoboInvalidArgument;
                }
                res = MapSegPoint(seg->c1.x, seg->c1.y, unitsPerEm, sizePx,
                                    origin, &c1);
                if (res != RoboOk) {
                    return res;
                }
                res = MapSegPoint(seg->p.x, seg->p.y, unitsPerEm, sizePx,
                                    origin, &p);
                if (res != RoboOk) {
                    return res;
                }
                res = FlattenQuad(path, cur, c1, p);
                if (res != RoboOk) {
                    return res;
                }
                cur = p;
                break;
            case RoboFontPathCubic:
                if (!haveCur) {
                    return RoboInvalidArgument;
                }
                res = MapSegPoint(seg->c1.x, seg->c1.y, unitsPerEm, sizePx,
                                    origin, &c1);
                if (res != RoboOk) {
                    return res;
                }
                res = MapSegPoint(seg->c2.x, seg->c2.y, unitsPerEm, sizePx,
                                    origin, &c2);
                if (res != RoboOk) {
                    return res;
                }
                res = MapSegPoint(seg->p.x, seg->p.y, unitsPerEm, sizePx,
                                    origin, &p);
                if (res != RoboOk) {
                    return res;
                }
                res = FlattenCubic(path, cur, c1, c2, p);
                if (res != RoboOk) {
                    return res;
                }
                cur = p;
                break;
            case RoboFontPathClose:
                res = GfxPathClose(path);
                if (res != RoboOk) {
                    return res;
                }
                haveCur = 0;
                break;
            default:
                return RoboNotSupported;
            }
        }
        if (contour->closed && GfxPathHasOpenContour(path)) {
            res = GfxPathClose(path);
            if (res != RoboOk) {
                return res;
            }
        }
    }
    return RoboOk;
}

Result GfxGlyphOutlineToPath(FontFace *face,
                                             GlyphId glyphId,
                                             float sizePx,
                                             GfxPointF origin,
                                             GfxPath *path)
{
    FontOutline *outline = NULL;
    uint16_t upem = 0u;
    Result res;

    if (!face || !path) {
        return RoboInvalidArgument;
    }
    res = FaceUpem(face, &upem);
    if (res != RoboOk) {
        return res;
    }
    res = FontFaceParseOutlines(face);
    if (res != RoboOk) {
        return res;
    }
    res = FontFaceGlyphOutline(face, glyphId, &outline);
    if (res != RoboOk) {
        return res;
    }
    res = GfxOutlineToPath(outline, upem, sizePx, origin, path);
    FontOutlineRelease(outline);
    return res;
}

static Result StrokePath(GfxRaster *raster,
                                 const GfxPath *path,
                                 GfxColorRgba8 color)
{
    GfxPathIter iter;
    GfxPathCommand cmd;
    GfxPoint subStart;
    GfxPoint cur;
    int haveCur = 0;
    int haveSub = 0;
    Result res;

    GfxPathIterBegin(path, &iter);
    while (GfxPathIterNext(&iter, &cmd)) {
        switch (cmd.verb) {
        case RoboGfxPathMoveTo:
            cur = cmd.point;
            subStart = cmd.point;
            haveCur = 1;
            haveSub = 1;
            break;
        case RoboGfxPathLineTo:
            if (!haveCur) {
                return RoboInvalidArgument;
            }
            res = GfxRasterDrawLine(raster, cur, cmd.point, color);
            if (res != RoboOk) {
                return res;
            }
            cur = cmd.point;
            break;
        case RoboGfxPathClose:
            if (haveCur && haveSub) {
                res = GfxRasterDrawLine(raster, cur, subStart, color);
                if (res != RoboOk) {
                    return res;
                }
                cur = subStart;
            }
            haveCur = 0;
            haveSub = 0;
            break;
        default:
            return RoboNotSupported;
        }
    }
    return RoboOk;
}

/*
 * The coverage path (Phase 12.1).
 *
 * Three stages, and the middle one is the whole of the new module: this file turns a
 * face + glyph id into an outline and the raster's clip, `RoboGraphicsGlyph.c` turns
 * that outline into coverage in device pixels, and `GfxRasterDrawGlyphMask` spends the
 * coverage through the module's existing source-over blend.
 *
 * The outline is **released before the mask is drawn**, so nothing here outlives its
 * borrow of the face. The clip is the raster's own, which is what makes "clipping is
 * the graphics layer's rule" hold for glyphs exactly as it does for a fill: the caller
 * never passes one in twice.
 */
Result GfxRasterDrawGlyphCoverage(GfxRaster *raster,
                                  const GfxGlyphDraw *draw,
                                  const GfxGlyphRasterParams *params)
{
    FontOutline *outline = NULL;
    GfxGlyphMask *mask = NULL;
    GfxGlyphRasterStats stats;
    GfxCoverageGamma gamma;
    GfxRect clip;
    uint16_t upem = 0u;
    Result res;

    if (!raster || !draw || !draw->face) {
        return RoboInvalidArgument;
    }
    if (!GfxRasterTarget(raster)) {
        return RoboInvalidArgument;
    }
    res = FaceUpem(draw->face, &upem);
    if (res != RoboOk) {
        return res;
    }
    res = FontFaceParseOutlines(draw->face);
    if (res != RoboOk) {
        return res;
    }
    res = FontFaceGlyphOutline(draw->face, draw->glyphId, &outline);
    if (res != RoboOk) {
        return res;
    }
    if (GfxRasterClip(raster, &clip) != RoboOk) {
        FontOutlineRelease(outline);
        return RoboInvalidArgument;
    }
    memset(&stats, 0, sizeof(stats));
    res = GfxGlyphMaskRasterizeOutline(outline, upem, draw->sizePx, draw->origin, &clip,
                                       params, &mask, &stats);
    FontOutlineRelease(outline);
    if (res != RoboOk) {
        return res;
    }

    /* An unrecognized model is the linear one here as well as in the table: a caller's bad
     * configuration must not be able to draw a *darker* pixel than the glyph's own colour. */
    gamma = params ? params->gamma : RoboGfxCoverageGammaDefault;
    if (gamma != RoboGfxCoverageGammaLinear && gamma != RoboGfxCoverageGammaDarkOnLight &&
        gamma != RoboGfxCoverageGammaLightOnDark) {
        gamma = RoboGfxCoverageGammaDefault;
    }
    res = GfxRasterDrawGlyphMask(raster, mask, draw->color, gamma);
    GfxGlyphMaskRelease(mask);
    return res;
}

Result GfxRasterDrawGlyph(GfxRaster *raster,
                                         const GfxGlyphDraw *draw)
{
    GfxPath *path = NULL;
    GfxRect pathBounds;
    GfxRect clip;
    GfxRect hit;
    GfxGlyphRenderMode mode;
    Result res;

    if (!raster || !draw || !draw->face) {
        return RoboInvalidArgument;
    }
    if (!GfxRasterTarget(raster)) {
        return RoboInvalidArgument;
    }

    mode = draw->mode;
    if (mode == 0) {
        mode = RoboGfxGlyphModeOutlineStroke;
    }
    if (mode == RoboGfxGlyphModeAlpha) {
        /* Phase 12.1: the same door, the coverage path behind it. The arguments were
         * checked above, so this is a second door onto one implementation rather than
         * a second set of rules. */
        return GfxRasterDrawGlyphCoverage(raster, draw, NULL);
    }
    if (mode != RoboGfxGlyphModeOutlineStroke) {
        return RoboNotSupported;
    }

    res = GfxPathCreate(&path);
    if (res != RoboOk) {
        return res;
    }

    res = GfxGlyphOutlineToPath(draw->face, draw->glyphId, draw->sizePx,
                                         draw->origin, path);
    if (res != RoboOk) {
        GfxPathRelease(path);
        return res;
    }

    if (GfxPathIsEmpty(path)) {
        GfxPathRelease(path);
        return RoboOk;
    }

    res = GfxPathBounds(path, &pathBounds);
    if (res != RoboOk) {
        GfxPathRelease(path);
        return res;
    }
    res = GfxRasterClip(raster, &clip);
    if (res != RoboOk) {
        GfxPathRelease(path);
        return res;
    }
    res = GfxRectIntersect(pathBounds, clip, &hit);
    if (res != RoboOk) {
        GfxPathRelease(path);
        return res;
    }
    if (GfxRectIsEmpty(hit)) {
        GfxPathRelease(path);
        return RoboOk;
    }

    res = StrokePath(raster, path, draw->color);
    GfxPathRelease(path);
    return res;
}
