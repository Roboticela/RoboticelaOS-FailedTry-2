#include "RoboGraphics/RoboGraphicsText.h"

#include "RoboGraphics/RoboGraphicsLine.h"

#include <string.h>

enum { ROBO_GFX_CURVE_SUBDIV = 8 };

void robo_gfx_glyph_draw_init(robo_gfx_glyph_draw_t *draw)
{
    if (!draw) {
        return;
    }
    memset(draw, 0, sizeof(*draw));
    draw->mode = ROBO_GFX_GLYPH_MODE_OUTLINE_STROKE;
    draw->color = robo_gfx_rgb8(0u, 0u, 0u);
}

static int float_ok(float v)
{
    /* Reject NaN and Inf without relying on isfinite (MSVC / freestanding). */
    return (v == v) && (v <= 3.402823466e+38f) && (v >= -3.402823466e+38f);
}

robo_result_t robo_gfx_font_scale(uint16_t units_per_em,
                                  float size_px,
                                  float *out_scale)
{
    if (!out_scale) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_scale = 0.0f;
    if (units_per_em == 0u || !float_ok(size_px) || size_px <= 0.0f) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_scale = size_px / (float)units_per_em;
    if (!float_ok(*out_scale)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

robo_result_t robo_gfx_font_point_to_device(robo_font_unit_t font_x,
                                           robo_font_unit_t font_y,
                                           uint16_t units_per_em,
                                           float size_px,
                                           robo_gfx_point_f_t origin,
                                           robo_gfx_point_f_t *out_device)
{
    float scale = 0.0f;
    robo_result_t res;

    if (!out_device || !float_ok(origin.x) || !float_ok(origin.y)) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_font_scale(units_per_em, size_px, &scale);
    if (res != ROBO_OK) {
        return res;
    }
    out_device->x = origin.x + (float)font_x * scale;
    out_device->y = origin.y - (float)font_y * scale;
    if (!float_ok(out_device->x) || !float_ok(out_device->y)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

robo_gfx_point_t robo_gfx_glyph_snap_point(robo_gfx_point_f_t p)
{
    return robo_gfx_point_f_round(p);
}

static robo_result_t face_upem(robo_font_face_t *face, uint16_t *out_upem)
{
    robo_font_metrics_t layout;
    robo_result_t res;

    if (!face || !out_upem) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_upem = 0u;
    res = robo_font_face_parse_metrics(face);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_face_layout_metrics(face, &layout);
    if (res != ROBO_OK) {
        return res;
    }
    if (!layout.valid || layout.units_per_em == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_upem = layout.units_per_em;
    return ROBO_OK;
}

robo_result_t robo_gfx_glyph_metrics_device(robo_font_face_t *face,
                                           robo_glyph_id_t glyph_id,
                                           float size_px,
                                           robo_gfx_glyph_metrics_device_t *out)
{
    robo_font_glyph_metrics_t gm;
    uint16_t upem = 0u;
    float scale = 0.0f;
    robo_result_t res;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }

    res = face_upem(face, &upem);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_gfx_font_scale(upem, size_px, &scale);
    if (res != ROBO_OK) {
        return res;
    }

    res = robo_font_face_glyph_metrics(face, glyph_id, &gm);
    if (res != ROBO_OK) {
        return res;
    }
    if (gm.status == ROBO_FONT_GLYPH_METRICS_INVALID_ID) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (gm.status != ROBO_FONT_GLYPH_METRICS_OK) {
        return ROBO_NOT_SUPPORTED;
    }

    out->advance_width = (float)gm.advance_width * scale;
    out->left_side_bearing = (float)gm.left_side_bearing * scale;
    if (gm.has_bbox) {
        float x0 = (float)gm.bbox.x_min * scale;
        float x1 = (float)gm.bbox.x_max * scale;
        float y_top = -(float)gm.bbox.y_max * scale;    /* above → smaller y */
        float y_bot = -(float)gm.bbox.y_min * scale;    /* below → larger y */
        out->bounds.x = x0;
        out->bounds.y = y_top;
        out->bounds.width = x1 - x0;
        out->bounds.height = y_bot - y_top;
        out->has_bounds = 1u;
        out->ascent = (gm.bbox.y_max > 0) ? (float)gm.bbox.y_max * scale : 0.0f;
        out->descent = (gm.bbox.y_min < 0) ? (float)(-gm.bbox.y_min) * scale
                                           : 0.0f;
    }
    return ROBO_OK;
}

static robo_result_t map_seg_point(robo_font_unit_t fx,
                                   robo_font_unit_t fy,
                                   uint16_t upem,
                                   float size_px,
                                   robo_gfx_point_f_t origin,
                                   robo_gfx_point_t *out_i)
{
    robo_gfx_point_f_t d;
    robo_result_t res =
        robo_gfx_font_point_to_device(fx, fy, upem, size_px, origin, &d);
    if (res != ROBO_OK) {
        return res;
    }
    *out_i = robo_gfx_glyph_snap_point(d);
    return ROBO_OK;
}

static robo_result_t emit_line(robo_gfx_path_t *path, robo_gfx_point_t p)
{
    if (!robo_gfx_path_has_open_contour(path)) {
        return robo_gfx_path_move_to(path, p);
    }
    return robo_gfx_path_line_to(path, p);
}

static robo_result_t flatten_quad(robo_gfx_path_t *path,
                                  robo_gfx_point_t p0,
                                  robo_gfx_point_t c1,
                                  robo_gfx_point_t p1)
{
    int i;
    robo_result_t res;

    for (i = 1; i <= ROBO_GFX_CURVE_SUBDIV; i++) {
        float t = (float)i / (float)ROBO_GFX_CURVE_SUBDIV;
        float u = 1.0f - t;
        float x = u * u * (float)p0.x + 2.0f * u * t * (float)c1.x +
                  t * t * (float)p1.x;
        float y = u * u * (float)p0.y + 2.0f * u * t * (float)c1.y +
                  t * t * (float)p1.y;
        res = emit_line(path, robo_gfx_glyph_snap_point(robo_gfx_point_f(x, y)));
        if (res != ROBO_OK) {
            return res;
        }
    }
    return ROBO_OK;
}

static robo_result_t flatten_cubic(robo_gfx_path_t *path,
                                   robo_gfx_point_t p0,
                                   robo_gfx_point_t c1,
                                   robo_gfx_point_t c2,
                                   robo_gfx_point_t p1)
{
    int i;
    robo_result_t res;

    for (i = 1; i <= ROBO_GFX_CURVE_SUBDIV; i++) {
        float t = (float)i / (float)ROBO_GFX_CURVE_SUBDIV;
        float u = 1.0f - t;
        float uu = u * u;
        float tt = t * t;
        float x = uu * u * (float)p0.x + 3.0f * uu * t * (float)c1.x +
                  3.0f * u * tt * (float)c2.x + tt * t * (float)p1.x;
        float y = uu * u * (float)p0.y + 3.0f * uu * t * (float)c1.y +
                  3.0f * u * tt * (float)c2.y + tt * t * (float)p1.y;
        res = emit_line(path, robo_gfx_glyph_snap_point(robo_gfx_point_f(x, y)));
        if (res != ROBO_OK) {
            return res;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_gfx_outline_to_path(const robo_font_outline_t *outline,
                                       uint16_t units_per_em,
                                       float size_px,
                                       robo_gfx_point_f_t origin,
                                       robo_gfx_path_t *path)
{
    uint32_t ci;
    uint32_t cc;
    robo_gfx_point_t cur;
    int have_cur = 0;
    float scale = 0.0f;
    robo_result_t res;

    if (!outline || !path) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_font_scale(units_per_em, size_px, &scale);
    if (res != ROBO_OK) {
        return res;
    }
    if (!float_ok(origin.x) || !float_ok(origin.y)) {
        return ROBO_INVALID_ARGUMENT;
    }

    robo_gfx_path_reset(path);

    switch (robo_font_outline_status(outline)) {
    case ROBO_FONT_OUTLINE_STATUS_OK:
        break;
    case ROBO_FONT_OUTLINE_STATUS_EMPTY:
        return ROBO_OK;
    case ROBO_FONT_OUTLINE_STATUS_INVALID_ID:
        return ROBO_INVALID_ARGUMENT;
    case ROBO_FONT_OUTLINE_STATUS_MALFORMED:
        return ROBO_INVALID_ARGUMENT;
    case ROBO_FONT_OUTLINE_STATUS_UNAVAILABLE:
    case ROBO_FONT_OUTLINE_STATUS_UNSUPPORTED:
        return ROBO_NOT_SUPPORTED;
    case ROBO_FONT_OUTLINE_STATUS_LIMIT:
    case ROBO_FONT_OUTLINE_STATUS_OVERFLOW:
        return ROBO_INVALID_ARGUMENT;
    default:
        return ROBO_INVALID_ARGUMENT;
    }

    cc = robo_font_outline_contour_count(outline);
    for (ci = 0u; ci < cc; ci++) {
        const robo_font_contour_t *contour = robo_font_outline_contour(outline, ci);
        uint32_t si;

        if (!contour) {
            return ROBO_INVALID_ARGUMENT;
        }
        have_cur = 0;
        for (si = 0u; si < contour->seg_count; si++) {
            const robo_font_path_seg_t *seg = &contour->segs[si];
            robo_gfx_point_t p;
            robo_gfx_point_t c1;
            robo_gfx_point_t c2;

            switch (seg->op) {
            case ROBO_FONT_PATH_MOVE:
                res = map_seg_point(seg->p.x, seg->p.y, units_per_em, size_px,
                                    origin, &p);
                if (res != ROBO_OK) {
                    return res;
                }
                res = robo_gfx_path_move_to(path, p);
                if (res != ROBO_OK) {
                    return res;
                }
                cur = p;
                have_cur = 1;
                break;
            case ROBO_FONT_PATH_LINE:
                res = map_seg_point(seg->p.x, seg->p.y, units_per_em, size_px,
                                    origin, &p);
                if (res != ROBO_OK) {
                    return res;
                }
                res = emit_line(path, p);
                if (res != ROBO_OK) {
                    return res;
                }
                cur = p;
                have_cur = 1;
                break;
            case ROBO_FONT_PATH_QUAD:
                if (!have_cur) {
                    return ROBO_INVALID_ARGUMENT;
                }
                res = map_seg_point(seg->c1.x, seg->c1.y, units_per_em, size_px,
                                    origin, &c1);
                if (res != ROBO_OK) {
                    return res;
                }
                res = map_seg_point(seg->p.x, seg->p.y, units_per_em, size_px,
                                    origin, &p);
                if (res != ROBO_OK) {
                    return res;
                }
                res = flatten_quad(path, cur, c1, p);
                if (res != ROBO_OK) {
                    return res;
                }
                cur = p;
                break;
            case ROBO_FONT_PATH_CUBIC:
                if (!have_cur) {
                    return ROBO_INVALID_ARGUMENT;
                }
                res = map_seg_point(seg->c1.x, seg->c1.y, units_per_em, size_px,
                                    origin, &c1);
                if (res != ROBO_OK) {
                    return res;
                }
                res = map_seg_point(seg->c2.x, seg->c2.y, units_per_em, size_px,
                                    origin, &c2);
                if (res != ROBO_OK) {
                    return res;
                }
                res = map_seg_point(seg->p.x, seg->p.y, units_per_em, size_px,
                                    origin, &p);
                if (res != ROBO_OK) {
                    return res;
                }
                res = flatten_cubic(path, cur, c1, c2, p);
                if (res != ROBO_OK) {
                    return res;
                }
                cur = p;
                break;
            case ROBO_FONT_PATH_CLOSE:
                res = robo_gfx_path_close(path);
                if (res != ROBO_OK) {
                    return res;
                }
                have_cur = 0;
                break;
            default:
                return ROBO_NOT_SUPPORTED;
            }
        }
        if (contour->closed && robo_gfx_path_has_open_contour(path)) {
            res = robo_gfx_path_close(path);
            if (res != ROBO_OK) {
                return res;
            }
        }
    }
    return ROBO_OK;
}

robo_result_t robo_gfx_glyph_outline_to_path(robo_font_face_t *face,
                                             robo_glyph_id_t glyph_id,
                                             float size_px,
                                             robo_gfx_point_f_t origin,
                                             robo_gfx_path_t *path)
{
    robo_font_outline_t *outline = NULL;
    uint16_t upem = 0u;
    robo_result_t res;

    if (!face || !path) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = face_upem(face, &upem);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_face_parse_outlines(face);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_face_glyph_outline(face, glyph_id, &outline);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_gfx_outline_to_path(outline, upem, size_px, origin, path);
    robo_font_outline_release(outline);
    return res;
}

static robo_result_t stroke_path(robo_gfx_raster_t *raster,
                                 const robo_gfx_path_t *path,
                                 robo_gfx_color_rgba8_t color)
{
    robo_gfx_path_iter_t iter;
    robo_gfx_path_command_t cmd;
    robo_gfx_point_t sub_start;
    robo_gfx_point_t cur;
    int have_cur = 0;
    int have_sub = 0;
    robo_result_t res;

    robo_gfx_path_iter_begin(path, &iter);
    while (robo_gfx_path_iter_next(&iter, &cmd)) {
        switch (cmd.verb) {
        case ROBO_GFX_PATH_MOVE_TO:
            cur = cmd.point;
            sub_start = cmd.point;
            have_cur = 1;
            have_sub = 1;
            break;
        case ROBO_GFX_PATH_LINE_TO:
            if (!have_cur) {
                return ROBO_INVALID_ARGUMENT;
            }
            res = robo_gfx_raster_draw_line(raster, cur, cmd.point, color);
            if (res != ROBO_OK) {
                return res;
            }
            cur = cmd.point;
            break;
        case ROBO_GFX_PATH_CLOSE:
            if (have_cur && have_sub) {
                res = robo_gfx_raster_draw_line(raster, cur, sub_start, color);
                if (res != ROBO_OK) {
                    return res;
                }
                cur = sub_start;
            }
            have_cur = 0;
            have_sub = 0;
            break;
        default:
            return ROBO_NOT_SUPPORTED;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_gfx_raster_draw_glyph(robo_gfx_raster_t *raster,
                                         const robo_gfx_glyph_draw_t *draw)
{
    robo_gfx_path_t *path = NULL;
    robo_gfx_rect_t path_bounds;
    robo_gfx_rect_t clip;
    robo_gfx_rect_t hit;
    robo_gfx_glyph_render_mode_t mode;
    robo_result_t res;

    if (!raster || !draw || !draw->face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_raster_target(raster)) {
        return ROBO_INVALID_ARGUMENT;
    }

    mode = draw->mode;
    if (mode == 0) {
        mode = ROBO_GFX_GLYPH_MODE_OUTLINE_STROKE;
    }
    if (mode != ROBO_GFX_GLYPH_MODE_OUTLINE_STROKE) {
        return ROBO_NOT_SUPPORTED;
    }

    res = robo_gfx_path_create(&path);
    if (res != ROBO_OK) {
        return res;
    }

    res = robo_gfx_glyph_outline_to_path(draw->face, draw->glyph_id, draw->size_px,
                                         draw->origin, path);
    if (res != ROBO_OK) {
        robo_gfx_path_release(path);
        return res;
    }

    if (robo_gfx_path_is_empty(path)) {
        robo_gfx_path_release(path);
        return ROBO_OK;
    }

    res = robo_gfx_path_bounds(path, &path_bounds);
    if (res != ROBO_OK) {
        robo_gfx_path_release(path);
        return res;
    }
    res = robo_gfx_raster_clip(raster, &clip);
    if (res != ROBO_OK) {
        robo_gfx_path_release(path);
        return res;
    }
    res = robo_gfx_rect_intersect(path_bounds, clip, &hit);
    if (res != ROBO_OK) {
        robo_gfx_path_release(path);
        return res;
    }
    if (robo_gfx_rect_is_empty(hit)) {
        robo_gfx_path_release(path);
        return ROBO_OK;
    }

    res = stroke_path(raster, path, draw->color);
    robo_gfx_path_release(path);
    return res;
}
