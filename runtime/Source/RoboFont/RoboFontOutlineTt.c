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
} tt_pt_t;

static robo_result_t append_simple_contour(robo_outline_builder_t *b,
                                           const int16_t *xs,
                                           const int16_t *ys,
                                           const uint8_t *flags,
                                           uint16_t start,
                                           uint16_t end)
{
    uint16_t n = (uint16_t)(end - start + 1u);
    tt_pt_t *exp = NULL;
    uint32_t exp_n = 0u;
    uint32_t exp_cap = 0u;
    uint32_t i;
    uint32_t start_on;
    robo_result_t res = ROBO_OK;

    if (n == 0u) {
        return ROBO_OK;
    }
    exp_cap = (uint32_t)n * 2u + 2u;
    if (exp_cap > ROBO_FONT_OUTLINE_MAX_POINTS) {
        return ROBO_PARSE_ERROR;
    }
    exp = (tt_pt_t *)robo_calloc(exp_cap, sizeof(*exp));
    if (!exp) {
        return ROBO_OUT_OF_MEMORY;
    }

    /* Expand implied on-curve midpoints between consecutive off-curve points. */
    for (i = 0u; i < n; i++) {
        uint16_t idx = (uint16_t)(start + i);
        uint16_t next_i = (uint16_t)((i + 1u) % n);
        uint16_t next = (uint16_t)(start + next_i);
        uint8_t on = (uint8_t)((flags[idx] & ROBO_FONT_GLYF_FLAG_ON_CURVE) != 0u);
        uint8_t next_on = (uint8_t)((flags[next] & ROBO_FONT_GLYF_FLAG_ON_CURVE) != 0u);

        if (exp_n >= exp_cap) {
            res = ROBO_PARSE_ERROR;
            goto done;
        }
        exp[exp_n].x = xs[idx];
        exp[exp_n].y = ys[idx];
        exp[exp_n].on = on;
        exp_n++;

        if (!on && !next_on) {
            int32_t mx = ((int32_t)xs[idx] + (int32_t)xs[next]) / 2;
            int32_t my = ((int32_t)ys[idx] + (int32_t)ys[next]) / 2;
            if (mx < -32768 || mx > 32767 || my < -32768 || my > 32767) {
                res = ROBO_PARSE_ERROR;
                goto done;
            }
            if (exp_n >= exp_cap) {
                res = ROBO_PARSE_ERROR;
                goto done;
            }
            exp[exp_n].x = (int16_t)mx;
            exp[exp_n].y = (int16_t)my;
            exp[exp_n].on = 1u;
            exp_n++;
        }
    }

    if (exp_n == 0u) {
        goto done;
    }

    /* Find first on-curve point. */
    start_on = 0u;
    for (i = 0u; i < exp_n; i++) {
        if (exp[i].on) {
            start_on = i;
            break;
        }
    }
    /* If all off (degenerate after expansion shouldn't happen), treat first as on. */
    if (!exp[start_on].on) {
        exp[start_on].on = 1u;
    }

    res = builder_move(b, exp[start_on].x, exp[start_on].y);
    if (res != ROBO_OK) {
        goto done;
    }

    i = start_on;
    do {
        uint32_t ni = (i + 1u) % exp_n;
        if (exp[ni].on) {
            res = builder_line(b, exp[ni].x, exp[ni].y);
            if (res != ROBO_OK) {
                goto done;
            }
            i = ni;
        } else {
            uint32_t n2 = (ni + 1u) % exp_n;
            if (!exp[n2].on) {
                res = ROBO_PARSE_ERROR;
                goto done;
            }
            res = builder_quad(b, exp[ni].x, exp[ni].y, exp[n2].x, exp[n2].y);
            if (res != ROBO_OK) {
                goto done;
            }
            i = n2;
        }
    } while (i != start_on);

    res = builder_close(b);

done:
    robo_free(exp);
    return res;
}

static robo_result_t convert_simple(robo_outline_builder_t *b,
                                    const robo_font_glyf_glyph_t *g)
{
    uint16_t c;
    uint16_t start = 0u;
    for (c = 0u; c < g->contour_count; c++) {
        uint16_t end = g->end_pts[c];
        robo_result_t res;
        if (end < start || end >= g->point_count) {
            return ROBO_PARSE_ERROR;
        }
        res = append_simple_contour(b, g->x_coords, g->y_coords, g->flags, start, end);
        if (res != ROBO_OK) {
            return res;
        }
        start = (uint16_t)(end + 1u);
    }
    return ROBO_OK;
}

static void component_to_affine(const robo_font_glyf_component_t *comp,
                                robo_font_affine_t *m)
{
    robo_font_affine_identity(m);
    if (comp->has_transform) {
        m->xscale = comp->xscale;
        m->scale01 = comp->scale01;
        m->scale10 = comp->scale10;
        m->yscale = comp->yscale;
    }
    if ((comp->flags & ROBO_FONT_GLYF_COMP_ARGS_XY) != 0u) {
        m->dx = comp->arg1;
        m->dy = comp->arg2;
        if ((comp->flags & ROBO_FONT_GLYF_COMP_ROUND_XY) != 0u) {
            /* already integer FUnits */
        }
    }
    /* Point-matching args: unsupported → identity translation (geometry still valid). */
}

typedef struct {
    robo_font_tt_t *tt;
    uint8_t *visiting;
    uint16_t num_glyphs;
    uint32_t component_budget;
} tt_flatten_ctx_t;

static robo_result_t flatten_glyph(tt_flatten_ctx_t *ctx,
                                   robo_glyph_id_t gid,
                                   uint32_t depth,
                                   const robo_font_affine_t *parent,
                                   robo_outline_builder_t *b);

static robo_result_t flatten_glyph(tt_flatten_ctx_t *ctx,
                                   robo_glyph_id_t gid,
                                   uint32_t depth,
                                   const robo_font_affine_t *parent,
                                   robo_outline_builder_t *b)
{
    robo_font_glyf_glyph_t g;
    robo_result_t res;
    uint16_t i;

    if (depth > ROBO_FONT_OUTLINE_MAX_COMPOSITE_DEPTH) {
        return ROBO_PARSE_ERROR;
    }
    if (gid >= ctx->num_glyphs) {
        return ROBO_PARSE_ERROR;
    }
    if (ctx->visiting[gid]) {
        return ROBO_PARSE_ERROR; /* cycle */
    }

    memset(&g, 0, sizeof(g));
    res = robo_font_tt_parse_glyph(ctx->tt, gid, &g);
    if (res != ROBO_OK) {
        return res;
    }

    if (g.kind == ROBO_FONT_GLYF_EMPTY) {
        robo_font_glyf_glyph_clear(&g);
        return ROBO_OK;
    }

    if (g.kind == ROBO_FONT_GLYF_SIMPLE) {
        robo_font_affine_t saved = b->xform;
        int saved_apply = b->apply_xform;
        b->xform = *parent;
        b->apply_xform = 1;
        res = convert_simple(b, &g);
        b->xform = saved;
        b->apply_xform = saved_apply;
        robo_font_glyf_glyph_clear(&g);
        return res;
    }

    if (g.kind != ROBO_FONT_GLYF_COMPOSITE) {
        robo_font_glyf_glyph_clear(&g);
        return ROBO_PARSE_ERROR;
    }

    ctx->visiting[gid] = 1u;
    for (i = 0u; i < g.component_count; i++) {
        robo_font_affine_t local;
        robo_font_affine_t combined;
        if (ctx->component_budget == 0u) {
            res = ROBO_PARSE_ERROR;
            break;
        }
        ctx->component_budget--;
        component_to_affine(&g.components[i], &local);
        res = robo_font_affine_multiply(parent, &local, &combined);
        if (res != ROBO_OK) {
            break;
        }
        res = flatten_glyph(ctx, g.components[i].glyph_index, depth + 1u, &combined, b);
        if (res != ROBO_OK) {
            break;
        }
    }
    ctx->visiting[gid] = 0u;
    robo_font_glyf_glyph_clear(&g);
    return res;
}

robo_result_t robo_font_outline_from_tt(robo_font_tt_t *tt,
                                        robo_glyph_id_t glyph_id,
                                        robo_font_outline_t **out)
{
    robo_font_outline_t *o;
    robo_outline_builder_t b;
    tt_flatten_ctx_t ctx;
    robo_font_affine_t id;
    uint16_t ng;
    robo_result_t res;

    if (!tt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    ng = robo_font_tt_num_glyphs(tt);
    if (glyph_id >= ng) {
        o = outline_new(glyph_id, ROBO_FONT_OUTLINE_TRUETYPE);
        if (!o) {
            return ROBO_OUT_OF_MEMORY;
        }
        outline_set_status(o, ROBO_FONT_OUTLINE_STATUS_INVALID_ID);
        *out = o;
        return ROBO_OK;
    }

    o = outline_new(glyph_id, ROBO_FONT_OUTLINE_TRUETYPE);
    if (!o) {
        return ROBO_OUT_OF_MEMORY;
    }
    builder_init(&b, o);
    robo_font_affine_identity(&id);

    memset(&ctx, 0, sizeof(ctx));
    ctx.tt = tt;
    ctx.num_glyphs = ng;
    ctx.component_budget = ROBO_FONT_OUTLINE_MAX_COMPOSITE_COMPONENTS;
    ctx.visiting = (uint8_t *)robo_calloc(ng, 1u);
    if (!ctx.visiting) {
        robo_font_outline_release(o);
        return ROBO_OUT_OF_MEMORY;
    }

    res = flatten_glyph(&ctx, glyph_id, 0u, &id, &b);
    robo_free(ctx.visiting);
    if (res != ROBO_OK) {
        outline_set_status(o, res == ROBO_OUT_OF_MEMORY ? ROBO_FONT_OUTLINE_STATUS_LIMIT
                                                        : ROBO_FONT_OUTLINE_STATUS_MALFORMED);
        /* still return outline object with status for diagnostics */
        if (res == ROBO_OUT_OF_MEMORY) {
            robo_font_outline_release(o);
            return res;
        }
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
