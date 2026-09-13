#include "RoboFont/RoboFontOutline.h"

#define ROBO_OUTLINE_STRUCT_ONLY
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

void robo_font_affine_identity(robo_font_affine_t *m)
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

robo_result_t robo_font_affine_multiply(const robo_font_affine_t *a,
                                        const robo_font_affine_t *b,
                                        robo_font_affine_t *out)
{
    int64_t xscale, scale01, scale10, yscale, dx, dy;
    if (!a || !b || !out) {
        return ROBO_INVALID_ARGUMENT;
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
        return ROBO_PARSE_ERROR;
    }
    out->xscale = (int16_t)xscale;
    out->scale01 = (int16_t)scale01;
    out->scale10 = (int16_t)scale10;
    out->yscale = (int16_t)yscale;
    out->dx = (int32_t)dx;
    out->dy = (int32_t)dy;
    return ROBO_OK;
}

robo_result_t robo_font_affine_apply(const robo_font_affine_t *m,
                                     robo_font_unit_t x,
                                     robo_font_unit_t y,
                                     robo_font_unit_t *out_x,
                                     robo_font_unit_t *out_y)
{
    int64_t nx, ny;
    if (!m || !out_x || !out_y) {
        return ROBO_INVALID_ARGUMENT;
    }
    nx = ((int64_t)m->xscale * (int64_t)x + (int64_t)m->scale10 * (int64_t)y) / 16384 +
         (int64_t)m->dx;
    ny = ((int64_t)m->scale01 * (int64_t)x + (int64_t)m->yscale * (int64_t)y) / 16384 +
         (int64_t)m->dy;
    if (nx < INT32_MIN || nx > INT32_MAX || ny < INT32_MIN || ny > INT32_MAX) {
        return ROBO_PARSE_ERROR;
    }
    *out_x = (robo_font_unit_t)nx;
    *out_y = (robo_font_unit_t)ny;
    return ROBO_OK;
}

void robo_font_outline_retain(robo_font_outline_t *o)
{
    if (o) {
        o->refs++;
    }
}

void robo_font_outline_release(robo_font_outline_t *o)
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
    for (i = 0u; i < o->contour_count; i++) {
        robo_free(o->contours[i].segs);
    }
    robo_free(o->contours);
    robo_free(o);
}

robo_glyph_id_t robo_font_outline_glyph_id(const robo_font_outline_t *o)
{
    return o ? o->glyph_id : 0u;
}

robo_font_outline_status_t robo_font_outline_status(const robo_font_outline_t *o)
{
    return o ? o->status : ROBO_FONT_OUTLINE_STATUS_UNAVAILABLE;
}

robo_font_outline_tech_t robo_font_outline_tech(const robo_font_outline_t *o)
{
    return o ? o->tech : ROBO_FONT_OUTLINE_UNKNOWN;
}

uint32_t robo_font_outline_contour_count(const robo_font_outline_t *o)
{
    return o ? o->contour_count : 0u;
}

const robo_font_contour_t *robo_font_outline_contour(const robo_font_outline_t *o,
                                                     uint32_t index)
{
    if (!o || index >= o->contour_count) {
        return NULL;
    }
    return &o->contours[index];
}

int robo_font_outline_has_bounds(const robo_font_outline_t *o)
{
    return o && o->has_bounds;
}

robo_result_t robo_font_outline_bounds(const robo_font_outline_t *o,
                                       robo_font_bbox_t *out)
{
    if (!o || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!o->has_bounds) {
        return ROBO_NOT_FOUND;
    }
    *out = o->bounds;
    return ROBO_OK;
}

uint32_t robo_font_outline_segment_count(const robo_font_outline_t *o)
{
    return o ? o->total_segs : 0u;
}

static const char *op_name(robo_font_path_op_t op)
{
    switch (op) {
    case ROBO_FONT_PATH_MOVE:
        return "MOVE";
    case ROBO_FONT_PATH_LINE:
        return "LINE";
    case ROBO_FONT_PATH_QUAD:
        return "QUAD";
    case ROBO_FONT_PATH_CUBIC:
        return "CUBIC";
    case ROBO_FONT_PATH_CLOSE:
        return "CLOSE";
    default:
        return "?";
    }
}

robo_result_t robo_font_outline_dump(const robo_font_outline_t *o,
                                     char *buf,
                                     size_t cap,
                                     size_t *out_len)
{
    size_t n = 0u;
    uint32_t i, j;
    int wr;
    if (!o || !buf || cap == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    buf[0] = '\0';
    wr = snprintf(buf + n, cap - n,
                  "gid=%u status=%d tech=%d contours=%u bounds=%d,%d,%d,%d\n",
                  (unsigned)o->glyph_id, (int)o->status, (int)o->tech,
                  (unsigned)o->contour_count,
                  o->has_bounds ? (int)o->bounds.x_min : 0,
                  o->has_bounds ? (int)o->bounds.y_min : 0,
                  o->has_bounds ? (int)o->bounds.x_max : 0,
                  o->has_bounds ? (int)o->bounds.y_max : 0);
    if (wr < 0 || (size_t)wr >= cap - n) {
        return ROBO_PARSE_ERROR;
    }
    n += (size_t)wr;
    for (i = 0u; i < o->contour_count; i++) {
        const robo_font_contour_t *c = &o->contours[i];
        wr = snprintf(buf + n, cap - n, "C %u closed=%u segs=%u\n", (unsigned)i,
                      (unsigned)c->closed, (unsigned)c->seg_count);
        if (wr < 0 || (size_t)wr >= cap - n) {
            return ROBO_PARSE_ERROR;
        }
        n += (size_t)wr;
        for (j = 0u; j < c->seg_count; j++) {
            const robo_font_path_seg_t *s = &c->segs[j];
            if (s->op == ROBO_FONT_PATH_QUAD) {
                wr = snprintf(buf + n, cap - n, "  %s %d %d %d %d\n", op_name(s->op),
                              (int)s->c1.x, (int)s->c1.y, (int)s->p.x, (int)s->p.y);
            } else if (s->op == ROBO_FONT_PATH_CUBIC) {
                wr = snprintf(buf + n, cap - n, "  %s %d %d %d %d %d %d\n",
                              op_name(s->op), (int)s->c1.x, (int)s->c1.y, (int)s->c2.x,
                              (int)s->c2.y, (int)s->p.x, (int)s->p.y);
            } else if (s->op == ROBO_FONT_PATH_CLOSE) {
                wr = snprintf(buf + n, cap - n, "  %s\n", op_name(s->op));
            } else {
                wr = snprintf(buf + n, cap - n, "  %s %d %d\n", op_name(s->op),
                              (int)s->p.x, (int)s->p.y);
            }
            if (wr < 0 || (size_t)wr >= cap - n) {
                return ROBO_PARSE_ERROR;
            }
            n += (size_t)wr;
        }
    }
    if (out_len) {
        *out_len = n;
    }
    return ROBO_OK;
}

robo_result_t robo_font_outline_from_sfnt(robo_font_sfnt_t *sfnt,
                                          robo_font_tt_t *tt_opt,
                                          robo_font_cff_font_t *cff_opt,
                                          robo_glyph_id_t glyph_id,
                                          robo_font_outline_t **out)
{
    robo_font_outline_tech_t tech;
    robo_font_tt_t *tt = tt_opt;
    robo_font_cff_font_t *cff = cff_opt;
    robo_result_t res;
    int own_tt = 0;
    int own_cff = 0;

    if (!sfnt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    tech = robo_font_ot_detect_outline(sfnt);
    if (tech == ROBO_FONT_OUTLINE_TRUETYPE) {
        if (!tt) {
            res = robo_font_tt_parse(sfnt, ROBO_FONT_TT_FLAGS_NONE, &tt);
            if (res != ROBO_OK) {
                return res;
            }
            own_tt = 1;
        }
        res = robo_font_outline_from_tt(tt, glyph_id, out);
        if (own_tt) {
            robo_font_tt_release(tt);
        }
        return res;
    }
    if (tech == ROBO_FONT_OUTLINE_CFF || tech == ROBO_FONT_OUTLINE_CFF2) {
        if (!cff) {
            res = robo_font_cff_parse(sfnt, ROBO_FONT_CFF_FLAGS_PARSE_ALL, &cff);
            if (res != ROBO_OK) {
                return res;
            }
            own_cff = 1;
        }
        res = robo_font_outline_from_cff(cff, glyph_id, out);
        if (own_cff) {
            robo_font_cff_release(cff);
        }
        return res;
    }
    return ROBO_NOT_SUPPORTED;
}

/* ---- Cache ---- */

struct robo_font_outline_cache {
    uint32_t refs;
    uint32_t count;
    uint32_t next_slot;
    robo_glyph_id_t gids[ROBO_FONT_OUTLINE_CACHE_MAX];
    robo_font_outline_t *outs[ROBO_FONT_OUTLINE_CACHE_MAX];
};

robo_result_t robo_font_outline_cache_create(robo_font_outline_cache_t **out)
{
    robo_font_outline_cache_t *c;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    c = (robo_font_outline_cache_t *)robo_calloc(1u, sizeof(*c));
    if (!c) {
        return ROBO_OUT_OF_MEMORY;
    }
    c->refs = 1u;
    *out = c;
    return ROBO_OK;
}

void robo_font_outline_cache_retain(robo_font_outline_cache_t *c)
{
    if (c) {
        c->refs++;
    }
}

void robo_font_outline_cache_release(robo_font_outline_cache_t *c)
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
        robo_font_outline_release(c->outs[i]);
    }
    robo_free(c);
}

robo_result_t robo_font_outline_cache_get(robo_font_outline_cache_t *cache,
                                          robo_font_sfnt_t *sfnt,
                                          robo_font_tt_t *tt_opt,
                                          robo_font_cff_font_t *cff_opt,
                                          robo_glyph_id_t glyph_id,
                                          robo_font_outline_t **out)
{
    uint32_t i;
    robo_font_outline_t *o = NULL;
    robo_result_t res;
    if (!cache || !sfnt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    for (i = 0u; i < cache->count; i++) {
        if (cache->gids[i] == glyph_id && cache->outs[i]) {
            robo_font_outline_retain(cache->outs[i]);
            *out = cache->outs[i];
            return ROBO_OK;
        }
    }
    res = robo_font_outline_from_sfnt(sfnt, tt_opt, cff_opt, glyph_id, &o);
    if (res != ROBO_OK) {
        return res;
    }
    if (cache->count < ROBO_FONT_OUTLINE_CACHE_MAX) {
        cache->gids[cache->count] = glyph_id;
        cache->outs[cache->count] = o;
        robo_font_outline_retain(o);
        cache->count++;
    } else {
        /* FIFO eviction */
        uint32_t slot = cache->next_slot % ROBO_FONT_OUTLINE_CACHE_MAX;
        robo_font_outline_release(cache->outs[slot]);
        cache->gids[slot] = glyph_id;
        cache->outs[slot] = o;
        robo_font_outline_retain(o);
        cache->next_slot = slot + 1u;
    }
    *out = o;
    return ROBO_OK;
}

robo_result_t robo_font_outline_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_outline_t *o = NULL;
    if (!data || size == 0u) {
        return ROBO_OK;
    }
    if (robo_font_source_create_memory_borrow(data, size, &src) != ROBO_OK) {
        return ROBO_OK;
    }
    if (robo_font_sfnt_parse(src, 0u, ROBO_FONT_SFNT_FLAGS_NONE, &sfnt) == ROBO_OK) {
        (void)robo_font_outline_from_sfnt(sfnt, NULL, NULL, 0u, &o);
        if (o) {
            robo_font_outline_release(o);
        }
        robo_font_sfnt_release(sfnt);
    }
    robo_font_source_release(src);
    return ROBO_OK;
}
