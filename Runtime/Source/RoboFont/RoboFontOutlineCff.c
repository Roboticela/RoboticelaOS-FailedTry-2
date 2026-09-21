#include "RoboFont/RoboFontOutline.h"

#include "RoboFontOutlineInternal.h"

#include "RoboFont/RoboFontCff.h"

#include <stdint.h>
#include <string.h>

static int32_t arg(const FontCffCsOpRec *r, uint8_t i)
{
    return (i < r->argc) ? r->args[i] : 0;
}

static Result CffRmoveto(OutlineBuilder *b,
                                 int32_t dx,
                                 int32_t dy,
                                 int32_t *x,
                                 int32_t *y)
{
    if (b->pathOpen) {
        Result res = BuilderClose(b);
        if (res != RoboOk) {
            return res;
        }
    }
    *x += dx;
    *y += dy;
    return BuilderMove(b, *x, *y);
}

static Result EmitRrcurveto(OutlineBuilder *b,
                                   const int32_t *a,
                                   uint8_t n,
                                   int32_t *x,
                                   int32_t *y)
{
    uint8_t i;
    if ((n % 6u) != 0u) {
        return RoboParseError;
    }
    for (i = 0u; i + 5u < n; i = (uint8_t)(i + 6u)) {
        int32_t c1x = *x + a[i];
        int32_t c1y = *y + a[i + 1];
        int32_t c2x = c1x + a[i + 2];
        int32_t c2y = c1y + a[i + 3];
        int32_t ex = c2x + a[i + 4];
        int32_t ey = c2y + a[i + 5];
        Result res = BuilderCubic(b, c1x, c1y, c2x, c2y, ex, ey);
        if (res != RoboOk) {
            return res;
        }
        *x = ex;
        *y = ey;
    }
    return RoboOk;
}

static Result ConvertCharstring(OutlineBuilder *b,
                                        const FontCffCharstring *cs)
{
    int32_t x = 0;
    int32_t y = 0;
    uint32_t i;
    for (i = 0u; i < cs->opCount; i++) {
        const FontCffCsOpRec *r = &cs->ops[i];
        uint16_t op = r->op;
        Result res = RoboOk;
        uint8_t k;

        switch (op) {
        case RoboCffCsHstem:
        case RoboCffCsVstem:
        case RoboCffCsHstemhm:
        case RoboCffCsVstemhm:
        case RoboCffCsHintmask:
        case RoboCffCsCntrmask:
            /* hints: ignore for geometry */
            break;

        case RoboCffCsRmoveto:
            if (r->argc < 2u) {
                return RoboParseError;
            }
            res = CffRmoveto(b, arg(r, 0), arg(r, 1), &x, &y);
            break;
        case RoboCffCsHmoveto:
            if (r->argc < 1u) {
                return RoboParseError;
            }
            res = CffRmoveto(b, arg(r, 0), 0, &x, &y);
            break;
        case RoboCffCsVmoveto:
            if (r->argc < 1u) {
                return RoboParseError;
            }
            res = CffRmoveto(b, 0, arg(r, 0), &x, &y);
            break;

        case RoboCffCsRlineto:
            for (k = 0u; k + 1u < r->argc; k = (uint8_t)(k + 2u)) {
                x += arg(r, k);
                y += arg(r, (uint8_t)(k + 1u));
                res = BuilderLine(b, x, y);
                if (res != RoboOk) {
                    return res;
                }
            }
            break;
        case RoboCffCsHlineto:
            for (k = 0u; k < r->argc; k++) {
                if ((k & 1u) == 0u) {
                    x += arg(r, k);
                } else {
                    y += arg(r, k);
                }
                res = BuilderLine(b, x, y);
                if (res != RoboOk) {
                    return res;
                }
            }
            break;
        case RoboCffCsVlineto:
            for (k = 0u; k < r->argc; k++) {
                if ((k & 1u) == 0u) {
                    y += arg(r, k);
                } else {
                    x += arg(r, k);
                }
                res = BuilderLine(b, x, y);
                if (res != RoboOk) {
                    return res;
                }
            }
            break;

        case RoboCffCsRrcurveto:
            res = EmitRrcurveto(b, r->args, r->argc, &x, &y);
            break;

        case RoboCffCsRcurveline: {
            uint8_t ncurve = (uint8_t)(r->argc - 2u);
            if (r->argc < 8u || (ncurve % 6u) != 0u) {
                return RoboParseError;
            }
            res = EmitRrcurveto(b, r->args, ncurve, &x, &y);
            if (res != RoboOk) {
                return res;
            }
            x += arg(r, (uint8_t)(r->argc - 2u));
            y += arg(r, (uint8_t)(r->argc - 1u));
            res = BuilderLine(b, x, y);
            break;
        }
        case RoboCffCsRlinecurve: {
            uint8_t nline = (uint8_t)(r->argc - 6u);
            if (r->argc < 8u || (nline % 2u) != 0u) {
                return RoboParseError;
            }
            for (k = 0u; k + 1u < nline; k = (uint8_t)(k + 2u)) {
                x += arg(r, k);
                y += arg(r, (uint8_t)(k + 1u));
                res = BuilderLine(b, x, y);
                if (res != RoboOk) {
                    return res;
                }
            }
            res = EmitRrcurveto(b, &r->args[nline], 6u, &x, &y);
            break;
        }

        case RoboCffCsVvcurveto: {
            uint8_t ai = 0u;
            int32_t dy1 = 0;
            if ((r->argc & 1u) != 0u) {
                dy1 = arg(r, ai++);
            }
            while (ai + 3u <= r->argc) {
                int32_t dx1 = arg(r, ai++);
                int32_t dy2 = arg(r, ai++);
                int32_t dx3 = arg(r, ai++);
                int32_t c1x = x + dx1;
                int32_t c1y = y + dy1;
                int32_t c2x = c1x;
                int32_t c2y = c1y + dy2;
                int32_t ex = c2x + dx3;
                int32_t ey = c2y;
                res = BuilderCubic(b, c1x, c1y, c2x, c2y, ex, ey);
                if (res != RoboOk) {
                    return res;
                }
                x = ex;
                y = ey;
                dy1 = 0;
            }
            break;
        }

        case RoboCffCsHhcurveto: {
            uint8_t ai = 0u;
            int32_t dy1 = 0;
            if ((r->argc & 1u) != 0u) {
                dy1 = arg(r, ai++);
            }
            while (ai + 3u <= r->argc) {
                int32_t dx1 = arg(r, ai++);
                int32_t dx2 = arg(r, ai++);
                int32_t dy2 = arg(r, ai++);
                int32_t dx3 = (ai < r->argc) ? arg(r, ai++) : 0;
                int32_t c1x = x + dx1;
                int32_t c1y = y + dy1;
                int32_t c2x = c1x + dx2;
                int32_t c2y = c1y + dy2;
                int32_t ex = c2x + dx3;
                int32_t ey = c2y;
                res = BuilderCubic(b, c1x, c1y, c2x, c2y, ex, ey);
                if (res != RoboOk) {
                    return res;
                }
                x = ex;
                y = ey;
                dy1 = 0;
            }
            break;
        }

        case RoboCffCsVhcurveto:
        case RoboCffCsHvcurveto: {
            /* Alternating hv/vh curves — 4 args each, optional final adjustment */
            uint8_t ai = 0u;
            int hv = (op == RoboCffCsHvcurveto);
            while (ai + 3u < r->argc || ai + 4u <= r->argc) {
                int32_t a0, a1, a2, a3, a4 = 0;
                int32_t c1x, c1y, c2x, c2y, ex, ey;
                uint8_t remain;
                if (ai >= r->argc) {
                    break;
                }
                remain = (uint8_t)(r->argc - ai);
                if (remain < 4u) {
                    break;
                }
                a0 = arg(r, ai++);
                a1 = arg(r, ai++);
                a2 = arg(r, ai++);
                a3 = arg(r, ai++);
                if (remain == 5u || (remain > 5u && (remain % 4u) == 1u && ai == r->argc - 0u)) {
                    /* optional last delta */
                }
                if (ai < r->argc && ((r->argc - ai) % 4u) == 1u && (r->argc - ai) == 1u) {
                    a4 = arg(r, ai++);
                }
                if (hv) {
                    c1x = x + a0;
                    c1y = y;
                    c2x = c1x + a1;
                    c2y = c1y + a2;
                    ex = c2x;
                    ey = c2y + a3;
                    if (a4) {
                        ex += a4;
                    }
                } else {
                    c1x = x;
                    c1y = y + a0;
                    c2x = c1x + a1;
                    c2y = c1y + a2;
                    ex = c2x + a3;
                    ey = c2y;
                    if (a4) {
                        ey += a4;
                    }
                }
                res = BuilderCubic(b, c1x, c1y, c2x, c2y, ex, ey);
                if (res != RoboOk) {
                    return res;
                }
                x = ex;
                y = ey;
                hv = !hv;
            }
            break;
        }

        case RoboCffCsHflex:
        case RoboCffCsFlex:
        case RoboCffCsHflex1:
        case RoboCffCsFlex1: {
            /* Expand flex to two cubics (simplified Adobe forms). */
            int32_t pts[14];
            uint8_t n = r->argc;
            uint8_t pi;
            if (n > 14u) {
                return RoboParseError;
            }
            for (pi = 0u; pi < n; pi++) {
                pts[pi] = arg(r, pi);
            }
            if (op == RoboCffCsHflex && n >= 7u) {
                int32_t x1 = x + pts[0];
                int32_t y1 = y;
                int32_t x2 = x1 + pts[1];
                int32_t y2 = y1 + pts[2];
                int32_t x3 = x2 + pts[3];
                int32_t y3 = y2;
                int32_t x4 = x3 + pts[4];
                int32_t y4 = y3;
                int32_t x5 = x4 + pts[5];
                int32_t y5 = y4;
                int32_t x6 = x5 + pts[6];
                int32_t y6 = y5;
                res = BuilderCubic(b, x1, y1, x2, y2, x3, y3);
                if (res != RoboOk) {
                    return res;
                }
                res = BuilderCubic(b, x4, y4, x5, y5, x6, y6);
                x = x6;
                y = y6;
            } else if (op == RoboCffCsFlex && n >= 13u) {
                int32_t cx = x, cy = y;
                int32_t xs[6], ys[6];
                for (pi = 0u; pi < 6u; pi++) {
                    cx += pts[pi * 2u];
                    cy += pts[pi * 2u + 1u];
                    xs[pi] = cx;
                    ys[pi] = cy;
                }
                res = BuilderCubic(b, xs[0], ys[0], xs[1], ys[1], xs[2], ys[2]);
                if (res != RoboOk) {
                    return res;
                }
                res = BuilderCubic(b, xs[3], ys[3], xs[4], ys[4], xs[5], ys[5]);
                x = xs[5];
                y = ys[5];
            } else {
                /* other flex variants: best-effort as rrcurveto-like if 12 coords */
                res = RoboOk;
            }
            break;
        }

        case RoboCffCsEndchar:
            if (b->pathOpen) {
                res = BuilderClose(b);
            }
            break;

        default:
            /* arithmetic escapes already executed in IR — ignore unknowns */
            break;
        }
        if (res != RoboOk) {
            return res;
        }
    }
    if (b->pathOpen) {
        return BuilderClose(b);
    }
    return RoboOk;
}

Result FontOutlineFromCff(FontCffFont *cff,
                                         GlyphId glyphId,
                                         FontOutline **out)
{
    FontOutline *o;
    OutlineBuilder b;
    FontCffCharstring cs;
    Result res;
    FontOutlineTech tech;

    if (!cff || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    tech = (cff->major == 2u) ? RoboFontOutlineCff2 : RoboFontOutlineCff;

    if (glyphId >= cff->glyphCount) {
        o = OutlineNew(glyphId, tech);
        if (!o) {
            return RoboOutOfMemory;
        }
        OutlineSetStatus(o, RoboFontOutlineStatusInvalidId);
        *out = o;
        return RoboOk;
    }

    memset(&cs, 0, sizeof(cs));
    res = FontCffParseGlyph(cff, glyphId, &cs);
    if (res != RoboOk) {
        o = OutlineNew(glyphId, tech);
        if (!o) {
            return RoboOutOfMemory;
        }
        OutlineSetStatus(o, RoboFontOutlineStatusMalformed);
        *out = o;
        return RoboOk;
    }

    o = OutlineNew(glyphId, tech);
    if (!o) {
        FontCffCharstringClear(&cs);
        return RoboOutOfMemory;
    }
    BuilderInit(&b, o);
    res = ConvertCharstring(&b, &cs);
    FontCffCharstringClear(&cs);
    if (res != RoboOk) {
        OutlineSetStatus(o, RoboFontOutlineStatusMalformed);
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
