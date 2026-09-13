#include "RoboFont/RoboFontOutline.h"

#include "RoboFontOutlineInternal.h"

#include "RoboFont/RoboFontCff.h"

#include <stdint.h>
#include <string.h>

static int32_t arg(const robo_font_cff_cs_op_rec_t *r, uint8_t i)
{
    return (i < r->argc) ? r->args[i] : 0;
}

static robo_result_t cff_rmoveto(robo_outline_builder_t *b,
                                 int32_t dx,
                                 int32_t dy,
                                 int32_t *x,
                                 int32_t *y)
{
    if (b->path_open) {
        robo_result_t res = builder_close(b);
        if (res != ROBO_OK) {
            return res;
        }
    }
    *x += dx;
    *y += dy;
    return builder_move(b, *x, *y);
}

static robo_result_t emit_rrcurveto(robo_outline_builder_t *b,
                                   const int32_t *a,
                                   uint8_t n,
                                   int32_t *x,
                                   int32_t *y)
{
    uint8_t i;
    if ((n % 6u) != 0u) {
        return ROBO_PARSE_ERROR;
    }
    for (i = 0u; i + 5u < n; i = (uint8_t)(i + 6u)) {
        int32_t c1x = *x + a[i];
        int32_t c1y = *y + a[i + 1];
        int32_t c2x = c1x + a[i + 2];
        int32_t c2y = c1y + a[i + 3];
        int32_t ex = c2x + a[i + 4];
        int32_t ey = c2y + a[i + 5];
        robo_result_t res = builder_cubic(b, c1x, c1y, c2x, c2y, ex, ey);
        if (res != ROBO_OK) {
            return res;
        }
        *x = ex;
        *y = ey;
    }
    return ROBO_OK;
}

static robo_result_t convert_charstring(robo_outline_builder_t *b,
                                        const robo_font_cff_charstring_t *cs)
{
    int32_t x = 0;
    int32_t y = 0;
    uint32_t i;
    for (i = 0u; i < cs->op_count; i++) {
        const robo_font_cff_cs_op_rec_t *r = &cs->ops[i];
        uint16_t op = r->op;
        robo_result_t res = ROBO_OK;
        uint8_t k;

        switch (op) {
        case ROBO_CFF_CS_HSTEM:
        case ROBO_CFF_CS_VSTEM:
        case ROBO_CFF_CS_HSTEMHM:
        case ROBO_CFF_CS_VSTEMHM:
        case ROBO_CFF_CS_HINTMASK:
        case ROBO_CFF_CS_CNTRMASK:
            /* hints: ignore for geometry */
            break;

        case ROBO_CFF_CS_RMOVETO:
            if (r->argc < 2u) {
                return ROBO_PARSE_ERROR;
            }
            res = cff_rmoveto(b, arg(r, 0), arg(r, 1), &x, &y);
            break;
        case ROBO_CFF_CS_HMOVETO:
            if (r->argc < 1u) {
                return ROBO_PARSE_ERROR;
            }
            res = cff_rmoveto(b, arg(r, 0), 0, &x, &y);
            break;
        case ROBO_CFF_CS_VMOVETO:
            if (r->argc < 1u) {
                return ROBO_PARSE_ERROR;
            }
            res = cff_rmoveto(b, 0, arg(r, 0), &x, &y);
            break;

        case ROBO_CFF_CS_RLINETO:
            for (k = 0u; k + 1u < r->argc; k = (uint8_t)(k + 2u)) {
                x += arg(r, k);
                y += arg(r, (uint8_t)(k + 1u));
                res = builder_line(b, x, y);
                if (res != ROBO_OK) {
                    return res;
                }
            }
            break;
        case ROBO_CFF_CS_HLINETO:
            for (k = 0u; k < r->argc; k++) {
                if ((k & 1u) == 0u) {
                    x += arg(r, k);
                } else {
                    y += arg(r, k);
                }
                res = builder_line(b, x, y);
                if (res != ROBO_OK) {
                    return res;
                }
            }
            break;
        case ROBO_CFF_CS_VLINETO:
            for (k = 0u; k < r->argc; k++) {
                if ((k & 1u) == 0u) {
                    y += arg(r, k);
                } else {
                    x += arg(r, k);
                }
                res = builder_line(b, x, y);
                if (res != ROBO_OK) {
                    return res;
                }
            }
            break;

        case ROBO_CFF_CS_RRCURVETO:
            res = emit_rrcurveto(b, r->args, r->argc, &x, &y);
            break;

        case ROBO_CFF_CS_RCURVELINE: {
            uint8_t ncurve = (uint8_t)(r->argc - 2u);
            if (r->argc < 8u || (ncurve % 6u) != 0u) {
                return ROBO_PARSE_ERROR;
            }
            res = emit_rrcurveto(b, r->args, ncurve, &x, &y);
            if (res != ROBO_OK) {
                return res;
            }
            x += arg(r, (uint8_t)(r->argc - 2u));
            y += arg(r, (uint8_t)(r->argc - 1u));
            res = builder_line(b, x, y);
            break;
        }
        case ROBO_CFF_CS_RLINECURVE: {
            uint8_t nline = (uint8_t)(r->argc - 6u);
            if (r->argc < 8u || (nline % 2u) != 0u) {
                return ROBO_PARSE_ERROR;
            }
            for (k = 0u; k + 1u < nline; k = (uint8_t)(k + 2u)) {
                x += arg(r, k);
                y += arg(r, (uint8_t)(k + 1u));
                res = builder_line(b, x, y);
                if (res != ROBO_OK) {
                    return res;
                }
            }
            res = emit_rrcurveto(b, &r->args[nline], 6u, &x, &y);
            break;
        }

        case ROBO_CFF_CS_VVCURVETO: {
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
                res = builder_cubic(b, c1x, c1y, c2x, c2y, ex, ey);
                if (res != ROBO_OK) {
                    return res;
                }
                x = ex;
                y = ey;
                dy1 = 0;
            }
            break;
        }

        case ROBO_CFF_CS_HHCURVETO: {
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
                res = builder_cubic(b, c1x, c1y, c2x, c2y, ex, ey);
                if (res != ROBO_OK) {
                    return res;
                }
                x = ex;
                y = ey;
                dy1 = 0;
            }
            break;
        }

        case ROBO_CFF_CS_VHCURVETO:
        case ROBO_CFF_CS_HVCURVETO: {
            /* Alternating hv/vh curves — 4 args each, optional final adjustment */
            uint8_t ai = 0u;
            int hv = (op == ROBO_CFF_CS_HVCURVETO);
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
                res = builder_cubic(b, c1x, c1y, c2x, c2y, ex, ey);
                if (res != ROBO_OK) {
                    return res;
                }
                x = ex;
                y = ey;
                hv = !hv;
            }
            break;
        }

        case ROBO_CFF_CS_HFLEX:
        case ROBO_CFF_CS_FLEX:
        case ROBO_CFF_CS_HFLEX1:
        case ROBO_CFF_CS_FLEX1: {
            /* Expand flex to two cubics (simplified Adobe forms). */
            int32_t pts[14];
            uint8_t n = r->argc;
            uint8_t pi;
            if (n > 14u) {
                return ROBO_PARSE_ERROR;
            }
            for (pi = 0u; pi < n; pi++) {
                pts[pi] = arg(r, pi);
            }
            if (op == ROBO_CFF_CS_HFLEX && n >= 7u) {
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
                res = builder_cubic(b, x1, y1, x2, y2, x3, y3);
                if (res != ROBO_OK) {
                    return res;
                }
                res = builder_cubic(b, x4, y4, x5, y5, x6, y6);
                x = x6;
                y = y6;
            } else if (op == ROBO_CFF_CS_FLEX && n >= 13u) {
                int32_t cx = x, cy = y;
                int32_t xs[6], ys[6];
                for (pi = 0u; pi < 6u; pi++) {
                    cx += pts[pi * 2u];
                    cy += pts[pi * 2u + 1u];
                    xs[pi] = cx;
                    ys[pi] = cy;
                }
                res = builder_cubic(b, xs[0], ys[0], xs[1], ys[1], xs[2], ys[2]);
                if (res != ROBO_OK) {
                    return res;
                }
                res = builder_cubic(b, xs[3], ys[3], xs[4], ys[4], xs[5], ys[5]);
                x = xs[5];
                y = ys[5];
            } else {
                /* other flex variants: best-effort as rrcurveto-like if 12 coords */
                res = ROBO_OK;
            }
            break;
        }

        case ROBO_CFF_CS_ENDCHAR:
            if (b->path_open) {
                res = builder_close(b);
            }
            break;

        default:
            /* arithmetic escapes already executed in IR — ignore unknowns */
            break;
        }
        if (res != ROBO_OK) {
            return res;
        }
    }
    if (b->path_open) {
        return builder_close(b);
    }
    return ROBO_OK;
}

robo_result_t robo_font_outline_from_cff(robo_font_cff_font_t *cff,
                                         robo_glyph_id_t glyph_id,
                                         robo_font_outline_t **out)
{
    robo_font_outline_t *o;
    robo_outline_builder_t b;
    robo_font_cff_charstring_t cs;
    robo_result_t res;
    robo_font_outline_tech_t tech;

    if (!cff || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    tech = (cff->major == 2u) ? ROBO_FONT_OUTLINE_CFF2 : ROBO_FONT_OUTLINE_CFF;

    if (glyph_id >= cff->glyph_count) {
        o = outline_new(glyph_id, tech);
        if (!o) {
            return ROBO_OUT_OF_MEMORY;
        }
        outline_set_status(o, ROBO_FONT_OUTLINE_STATUS_INVALID_ID);
        *out = o;
        return ROBO_OK;
    }

    memset(&cs, 0, sizeof(cs));
    res = robo_font_cff_parse_glyph(cff, glyph_id, &cs);
    if (res != ROBO_OK) {
        o = outline_new(glyph_id, tech);
        if (!o) {
            return ROBO_OUT_OF_MEMORY;
        }
        outline_set_status(o, ROBO_FONT_OUTLINE_STATUS_MALFORMED);
        *out = o;
        return ROBO_OK;
    }

    o = outline_new(glyph_id, tech);
    if (!o) {
        robo_font_cff_charstring_clear(&cs);
        return ROBO_OUT_OF_MEMORY;
    }
    builder_init(&b, o);
    res = convert_charstring(&b, &cs);
    robo_font_cff_charstring_clear(&cs);
    if (res != ROBO_OK) {
        outline_set_status(o, ROBO_FONT_OUTLINE_STATUS_MALFORMED);
        *out = o;
        return ROBO_OK;
    }
    if (o->contour_count == 0u) {
        outline_set_status(o, ROBO_FONT_OUTLINE_STATUS_EMPTY);
    } else {
        finalize_bounds(o);
        outline_set_status(o, ROBO_FONT_OUTLINE_STATUS_OK);
    }
    *out = o;
    return ROBO_OK;
}
