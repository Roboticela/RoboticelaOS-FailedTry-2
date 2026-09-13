/* Internal outline builder (Goal 5.8). Included by outline .c files only.
 * Define ROBO_OUTLINE_STRUCT_ONLY before include to get the opaque struct
 * without static builder helpers (for RoboFontOutline.c accessors).
 */
#ifndef ROBO_FONT_OUTLINE_INTERNAL_H
#define ROBO_FONT_OUTLINE_INTERNAL_H

#include "RoboFont/RoboFontOutline.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_font_outline {
    uint32_t refs;
    robo_glyph_id_t glyph_id;
    robo_font_outline_status_t status;
    robo_font_outline_tech_t tech;
    robo_font_bbox_t bounds;
    uint8_t has_bounds;
    uint32_t contour_count;
    uint32_t contour_cap;
    robo_font_contour_t *contours;
    uint32_t total_segs;
};

#ifndef ROBO_OUTLINE_STRUCT_ONLY

typedef struct {
    robo_font_outline_t *o;
    robo_font_contour_t *cur;
    uint32_t cur_seg_cap;
    robo_font_unit_t cx, cy;
    robo_font_unit_t sx, sy;
    int path_open;
    robo_font_affine_t xform;
    int apply_xform;
} robo_outline_builder_t;

static __attribute__((unused)) double robo_d_sqrt(double x)
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

static __attribute__((unused)) void builder_init(robo_outline_builder_t *b, robo_font_outline_t *o)
{
    memset(b, 0, sizeof(*b));
    b->o = o;
    robo_font_affine_identity(&b->xform);
}

static __attribute__((unused)) robo_result_t map_pt(robo_outline_builder_t *b,
                            robo_font_unit_t x,
                            robo_font_unit_t y,
                            robo_font_point_t *out)
{
    if (b->apply_xform) {
        return robo_font_affine_apply(&b->xform, x, y, &out->x, &out->y);
    }
    out->x = x;
    out->y = y;
    return ROBO_OK;
}

static __attribute__((unused)) void bbox_include(robo_font_bbox_t *bb,
                         int *init,
                         robo_font_unit_t x,
                         robo_font_unit_t y)
{
    if (!*init) {
        bb->x_min = bb->x_max = x;
        bb->y_min = bb->y_max = y;
        *init = 1;
        return;
    }
    if (x < bb->x_min) {
        bb->x_min = x;
    }
    if (x > bb->x_max) {
        bb->x_max = x;
    }
    if (y < bb->y_min) {
        bb->y_min = y;
    }
    if (y > bb->y_max) {
        bb->y_max = y;
    }
}

static __attribute__((unused)) void quad_extrema_1d(robo_font_unit_t p0,
                            robo_font_unit_t p1,
                            robo_font_unit_t p2,
                            robo_font_unit_t *lo,
                            robo_font_unit_t *hi)
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
            *lo = (robo_font_unit_t)v;
        }
        if (v > (double)*hi) {
            *hi = (robo_font_unit_t)v;
        }
    }
}

static __attribute__((unused)) void cubic_extrema_1d(robo_font_unit_t p0,
                             robo_font_unit_t p1,
                             robo_font_unit_t p2,
                             robo_font_unit_t p3,
                             robo_font_unit_t *lo,
                             robo_font_unit_t *hi)
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
                *lo = (robo_font_unit_t)v;
            }
            if (v > (double)*hi) {
                *hi = (robo_font_unit_t)v;
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
        t = (-b + s * robo_d_sqrt(disc)) / (2.0 * a);
        if (t > 0.0 && t < 1.0) {
            double u = 1.0 - t;
            v = u * u * u * (double)p0 + 3.0 * u * u * t * (double)p1 +
                3.0 * u * t * t * (double)p2 + t * t * t * (double)p3;
            if (v < (double)*lo) {
                *lo = (robo_font_unit_t)v;
            }
            if (v > (double)*hi) {
                *hi = (robo_font_unit_t)v;
            }
        }
    }
}

static __attribute__((unused)) void include_seg_bounds(robo_font_bbox_t *bb,
                               int *init,
                               const robo_font_path_seg_t *seg,
                               robo_font_unit_t prev_x,
                               robo_font_unit_t prev_y)
{
    robo_font_unit_t xa, xb, ya, yb;
    switch (seg->op) {
    case ROBO_FONT_PATH_MOVE:
    case ROBO_FONT_PATH_LINE:
        bbox_include(bb, init, seg->p.x, seg->p.y);
        break;
    case ROBO_FONT_PATH_QUAD:
        quad_extrema_1d(prev_x, seg->c1.x, seg->p.x, &xa, &xb);
        quad_extrema_1d(prev_y, seg->c1.y, seg->p.y, &ya, &yb);
        bbox_include(bb, init, xa, ya);
        bbox_include(bb, init, xb, yb);
        bbox_include(bb, init, seg->p.x, seg->p.y);
        break;
    case ROBO_FONT_PATH_CUBIC:
        cubic_extrema_1d(prev_x, seg->c1.x, seg->c2.x, seg->p.x, &xa, &xb);
        cubic_extrema_1d(prev_y, seg->c1.y, seg->c2.y, seg->p.y, &ya, &yb);
        bbox_include(bb, init, xa, ya);
        bbox_include(bb, init, xb, yb);
        bbox_include(bb, init, seg->p.x, seg->p.y);
        break;
    default:
        break;
    }
}

static __attribute__((unused)) robo_result_t ensure_contour_cap(robo_font_outline_t *o)
{
    uint32_t ncap;
    robo_font_contour_t *n;
    if (o->contour_count < o->contour_cap) {
        return ROBO_OK;
    }
    ncap = o->contour_cap ? o->contour_cap * 2u : 4u;
    if (ncap > ROBO_FONT_OUTLINE_MAX_CONTOURS) {
        ncap = ROBO_FONT_OUTLINE_MAX_CONTOURS;
    }
    if (o->contour_count >= ncap) {
        return ROBO_PARSE_ERROR;
    }
    n = (robo_font_contour_t *)robo_calloc(ncap, sizeof(*n));
    if (!n) {
        return ROBO_OUT_OF_MEMORY;
    }
    if (o->contours) {
        memcpy(n, o->contours, (size_t)o->contour_count * sizeof(*n));
        robo_free(o->contours);
    }
    o->contours = n;
    o->contour_cap = ncap;
    return ROBO_OK;
}

static __attribute__((unused)) robo_result_t emit_seg(robo_outline_builder_t *b, robo_font_path_seg_t seg)
{
    robo_font_contour_t *c;
    if (!b->cur) {
        return ROBO_PARSE_ERROR;
    }
    if (b->o->total_segs >= ROBO_FONT_OUTLINE_MAX_SEGMENTS) {
        return ROBO_PARSE_ERROR;
    }
    c = b->cur;
    if (c->seg_count >= b->cur_seg_cap) {
        uint32_t na = b->cur_seg_cap ? b->cur_seg_cap * 2u : 8u;
        robo_font_path_seg_t *ns;
        if (na > ROBO_FONT_OUTLINE_MAX_SEGMENTS) {
            na = ROBO_FONT_OUTLINE_MAX_SEGMENTS;
        }
        if (c->seg_count >= na) {
            return ROBO_PARSE_ERROR;
        }
        ns = (robo_font_path_seg_t *)robo_calloc(na, sizeof(*ns));
        if (!ns) {
            return ROBO_OUT_OF_MEMORY;
        }
        if (c->segs) {
            memcpy(ns, c->segs, (size_t)c->seg_count * sizeof(*ns));
            robo_free(c->segs);
        }
        c->segs = ns;
        b->cur_seg_cap = na;
    }
    c->segs[c->seg_count++] = seg;
    b->o->total_segs++;
    return ROBO_OK;
}

static __attribute__((unused)) robo_result_t begin_contour(robo_outline_builder_t *b)
{
    robo_result_t res = ensure_contour_cap(b->o);
    if (res != ROBO_OK) {
        return res;
    }
    if (b->o->contour_count >= ROBO_FONT_OUTLINE_MAX_CONTOURS) {
        return ROBO_PARSE_ERROR;
    }
    b->cur = &b->o->contours[b->o->contour_count++];
    memset(b->cur, 0, sizeof(*b->cur));
    b->cur_seg_cap = 0u;
    b->path_open = 0;
    return ROBO_OK;
}

static __attribute__((unused)) robo_result_t builder_move(robo_outline_builder_t *b,
                                  robo_font_unit_t x,
                                  robo_font_unit_t y)
{
    robo_font_path_seg_t seg;
    robo_font_point_t pt;
    robo_result_t res = begin_contour(b);
    if (res != ROBO_OK) {
        return res;
    }
    res = map_pt(b, x, y, &pt);
    if (res != ROBO_OK) {
        return res;
    }
    memset(&seg, 0, sizeof(seg));
    seg.op = ROBO_FONT_PATH_MOVE;
    seg.p = pt;
    res = emit_seg(b, seg);
    if (res != ROBO_OK) {
        return res;
    }
    b->cx = pt.x;
    b->cy = pt.y;
    b->sx = pt.x;
    b->sy = pt.y;
    b->path_open = 1;
    return ROBO_OK;
}

static __attribute__((unused)) robo_result_t builder_line(robo_outline_builder_t *b,
                                  robo_font_unit_t x,
                                  robo_font_unit_t y)
{
    robo_font_path_seg_t seg;
    robo_font_point_t pt;
    robo_result_t res;
    if (!b->path_open) {
        return builder_move(b, x, y);
    }
    res = map_pt(b, x, y, &pt);
    if (res != ROBO_OK) {
        return res;
    }
    memset(&seg, 0, sizeof(seg));
    seg.op = ROBO_FONT_PATH_LINE;
    seg.p = pt;
    res = emit_seg(b, seg);
    if (res != ROBO_OK) {
        return res;
    }
    b->cx = pt.x;
    b->cy = pt.y;
    return ROBO_OK;
}

static __attribute__((unused)) robo_result_t builder_quad(robo_outline_builder_t *b,
                                  robo_font_unit_t cx,
                                  robo_font_unit_t cy,
                                  robo_font_unit_t x,
                                  robo_font_unit_t y)
{
    robo_font_path_seg_t seg;
    robo_font_point_t c1, p;
    robo_result_t res;
    if (!b->path_open) {
        return ROBO_PARSE_ERROR;
    }
    res = map_pt(b, cx, cy, &c1);
    if (res != ROBO_OK) {
        return res;
    }
    res = map_pt(b, x, y, &p);
    if (res != ROBO_OK) {
        return res;
    }
    memset(&seg, 0, sizeof(seg));
    seg.op = ROBO_FONT_PATH_QUAD;
    seg.c1 = c1;
    seg.p = p;
    res = emit_seg(b, seg);
    if (res != ROBO_OK) {
        return res;
    }
    b->cx = p.x;
    b->cy = p.y;
    return ROBO_OK;
}

static __attribute__((unused)) robo_result_t builder_cubic(robo_outline_builder_t *b,
                                   robo_font_unit_t c1x,
                                   robo_font_unit_t c1y,
                                   robo_font_unit_t c2x,
                                   robo_font_unit_t c2y,
                                   robo_font_unit_t x,
                                   robo_font_unit_t y)
{
    robo_font_path_seg_t seg;
    robo_font_point_t c1, c2, p;
    robo_result_t res;
    if (!b->path_open) {
        return ROBO_PARSE_ERROR;
    }
    res = map_pt(b, c1x, c1y, &c1);
    if (res != ROBO_OK) {
        return res;
    }
    res = map_pt(b, c2x, c2y, &c2);
    if (res != ROBO_OK) {
        return res;
    }
    res = map_pt(b, x, y, &p);
    if (res != ROBO_OK) {
        return res;
    }
    memset(&seg, 0, sizeof(seg));
    seg.op = ROBO_FONT_PATH_CUBIC;
    seg.c1 = c1;
    seg.c2 = c2;
    seg.p = p;
    res = emit_seg(b, seg);
    if (res != ROBO_OK) {
        return res;
    }
    b->cx = p.x;
    b->cy = p.y;
    return ROBO_OK;
}

static __attribute__((unused)) robo_result_t builder_close(robo_outline_builder_t *b)
{
    robo_font_path_seg_t seg;
    robo_result_t res;
    if (!b->path_open || !b->cur) {
        return ROBO_OK;
    }
    if (b->cx != b->sx || b->cy != b->sy) {
        memset(&seg, 0, sizeof(seg));
        seg.op = ROBO_FONT_PATH_LINE;
        seg.p.x = b->sx;
        seg.p.y = b->sy;
        res = emit_seg(b, seg);
        if (res != ROBO_OK) {
            return res;
        }
        b->cx = b->sx;
        b->cy = b->sy;
    }
    memset(&seg, 0, sizeof(seg));
    seg.op = ROBO_FONT_PATH_CLOSE;
    res = emit_seg(b, seg);
    if (res != ROBO_OK) {
        return res;
    }
    b->cur->closed = 1u;
    b->path_open = 0;
    return ROBO_OK;
}

static __attribute__((unused)) void finalize_bounds(robo_font_outline_t *o)
{
    uint32_t i, j;
    robo_font_bbox_t gbb;
    int ginit = 0;
    for (i = 0u; i < o->contour_count; i++) {
        robo_font_contour_t *c = &o->contours[i];
        robo_font_bbox_t cbb;
        int cinit = 0;
        robo_font_unit_t px = 0, py = 0;
        int have = 0;
        for (j = 0u; j < c->seg_count; j++) {
            const robo_font_path_seg_t *s = &c->segs[j];
            if (s->op == ROBO_FONT_PATH_MOVE) {
                px = s->p.x;
                py = s->p.y;
                have = 1;
                bbox_include(&cbb, &cinit, px, py);
                continue;
            }
            if (!have) {
                continue;
            }
            include_seg_bounds(&cbb, &cinit, s, px, py);
            if (s->op != ROBO_FONT_PATH_CLOSE) {
                px = s->p.x;
                py = s->p.y;
            }
        }
        if (cinit) {
            c->bounds = cbb;
            c->has_bounds = 1u;
            bbox_include(&gbb, &ginit, cbb.x_min, cbb.y_min);
            bbox_include(&gbb, &ginit, cbb.x_max, cbb.y_max);
        }
    }
    if (ginit) {
        o->bounds = gbb;
        o->has_bounds = 1u;
    }
}

static __attribute__((unused)) robo_font_outline_t *outline_new(robo_glyph_id_t gid,
                                        robo_font_outline_tech_t tech)
{
    robo_font_outline_t *o = (robo_font_outline_t *)robo_calloc(1u, sizeof(*o));
    if (!o) {
        return NULL;
    }
    o->refs = 1u;
    o->glyph_id = gid;
    o->tech = tech;
    o->status = ROBO_FONT_OUTLINE_STATUS_OK;
    return o;
}

static __attribute__((unused)) void outline_set_status(robo_font_outline_t *o, robo_font_outline_status_t s)
{
    o->status = s;
    if (s == ROBO_FONT_OUTLINE_STATUS_EMPTY) {
        o->has_bounds = 0u;
    }
}

#endif /* !ROBO_OUTLINE_STRUCT_ONLY */

#endif /* ROBO_FONT_OUTLINE_INTERNAL_H */
