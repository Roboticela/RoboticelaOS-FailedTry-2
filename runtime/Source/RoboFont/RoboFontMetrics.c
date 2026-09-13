#include "RoboFont/RoboFontMetrics.h"

#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_font_metrics_info {
    uint32_t refs;
    robo_font_tt_t *tt; /* retained */
    robo_font_metrics_global_t global;
    robo_font_vhea_t vhea;
    uint8_t has_vhea;
    robo_font_span_t vmtx;
    uint16_t number_of_v_metrics;
};

void robo_font_metrics_clear(robo_font_metrics_t *m)
{
    if (!m) {
        return;
    }
    memset(m, 0, sizeof(*m));
}

robo_result_t robo_font_metrics_init(robo_font_metrics_t *m,
                                     uint16_t units_per_em,
                                     robo_font_unit_t ascent,
                                     robo_font_unit_t descent,
                                     robo_font_unit_t line_gap,
                                     const robo_font_bbox_t *bbox)
{
    if (!m || units_per_em == 0u || descent < 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(m, 0, sizeof(*m));
    m->units_per_em = units_per_em;
    m->ascent = ascent;
    m->descent = descent;
    m->line_gap = line_gap;
    if (bbox) {
        m->bbox = *bbox;
        m->has_bbox = 1u;
    }
    m->valid = 1u;
    return ROBO_OK;
}

robo_font_unit_t robo_font_metrics_line_height(const robo_font_metrics_t *m)
{
    if (!m || !m->valid) {
        return 0;
    }
    return m->ascent + m->descent + m->line_gap;
}

robo_result_t robo_font_units_to_pixels(robo_font_unit_t units,
                                        uint16_t units_per_em,
                                        robo_font_pixel_size_t pixel_size,
                                        float *out_pixels)
{
    if (!out_pixels || units_per_em == 0u || !(pixel_size > 0.0f)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_pixels = ((float)units * pixel_size) / (float)units_per_em;
    return ROBO_OK;
}

robo_result_t robo_font_pixels_to_units(float pixels,
                                        uint16_t units_per_em,
                                        robo_font_pixel_size_t pixel_size,
                                        robo_font_unit_t *out_units)
{
    double v;
    if (!out_units || units_per_em == 0u || !(pixel_size > 0.0f)) {
        return ROBO_INVALID_ARGUMENT;
    }
    v = ((double)pixels * (double)units_per_em) / (double)pixel_size;
    if (v >= 0.0) {
        *out_units = (robo_font_unit_t)(v + 0.5);
    } else {
        *out_units = (robo_font_unit_t)(v - 0.5);
    }
    return ROBO_OK;
}

robo_font_pixel_size_t robo_font_points_to_pixels(robo_font_point_size_t points)
{
    return points * (96.0f / 72.0f);
}

int robo_font_units_per_em_is_valid(uint16_t units_per_em)
{
    return units_per_em >= ROBO_FONT_UPEM_MIN && units_per_em <= ROBO_FONT_UPEM_MAX;
}

static double d_floor(double v)
{
    double i = (double)(long long)v;
    if (v >= 0.0 || i == v) {
        return i;
    }
    return i - 1.0;
}

static double d_ceil(double v)
{
    double i = (double)(long long)v;
    if (v <= 0.0 || i == v) {
        return i;
    }
    return i + 1.0;
}

static float apply_round(double v, robo_font_round_mode_t mode)
{
    switch (mode) {
    case ROBO_FONT_ROUND_FLOOR:
        return (float)d_floor(v);
    case ROBO_FONT_ROUND_CEIL:
        return (float)d_ceil(v);
    case ROBO_FONT_ROUND_TOWARD_ZERO:
        return (float)(long long)v;
    case ROBO_FONT_ROUND_NEAREST:
    default:
        if (v >= 0.0) {
            return (float)d_floor(v + 0.5);
        }
        return (float)d_ceil(v - 0.5);
    }
}

robo_result_t robo_font_scale_font_units(robo_font_unit_t value,
                                         uint16_t units_per_em,
                                         robo_font_pixel_size_t font_size,
                                         robo_font_round_mode_t round_mode,
                                         float *out_scaled)
{
    double scaled;
    if (!out_scaled || !robo_font_units_per_em_is_valid(units_per_em) ||
        !(font_size > 0.0f)) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Multiply before divide using double to avoid int overflow. */
    scaled = ((double)value * (double)font_size) / (double)units_per_em;
    *out_scaled = apply_round(scaled, round_mode);
    return ROBO_OK;
}

/* ---- vhea ---- */

robo_result_t robo_font_vhea_parse(robo_font_span_t span, robo_font_vhea_t *out)
{
    robo_font_reader_t r;
    int16_t reserved[4];
    int i;
    if (!out || span.size < 36u) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &out->major_version) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->minor_version) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->ascent) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->descent) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->line_gap) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->advance_height_max) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->min_top_side_bearing) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->min_bottom_side_bearing) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_max_extent) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->caret_slope_rise) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->caret_slope_run) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->caret_offset) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    for (i = 0; i < 4; i++) {
        if (robo_font_read_i16be(&r, &reserved[i]) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
    }
    if (robo_font_read_i16be(&r, &out->metric_data_format) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->number_of_v_metrics) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (out->major_version != 1u || out->metric_data_format != 0) {
        return ROBO_PARSE_ERROR;
    }
    if (out->number_of_v_metrics == 0u) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

static size_t expected_vmtx_size(uint16_t num_glyphs, uint16_t n_v_metrics)
{
    if (n_v_metrics == 0u || n_v_metrics > num_glyphs) {
        return 0u;
    }
    return (size_t)n_v_metrics * 4u + (size_t)(num_glyphs - n_v_metrics) * 2u;
}

static robo_font_unit_t mag_descender(int16_t d)
{
    return d < 0 ? (robo_font_unit_t)(-d) : (robo_font_unit_t)d;
}

static void set_opt_unit(robo_font_optional_unit_t *o,
                         robo_font_unit_t v,
                         robo_font_metric_avail_t a)
{
    o->value = v;
    o->avail = a;
}

static void set_opt_u16(robo_font_optional_u16_t *o,
                        uint16_t v,
                        robo_font_metric_avail_t a)
{
    o->value = v;
    o->avail = a;
}

static void fill_global_from_tt(robo_font_tt_t *tt, robo_font_metrics_global_t *g)
{
    const robo_font_head_t *head = robo_font_tt_head(tt);
    const robo_font_hhea_t *hhea = robo_font_tt_hhea(tt);
    const robo_font_os2_t *os2 = robo_font_tt_os2(tt);
    const robo_font_post_t *post = robo_font_tt_post(tt);
    robo_font_unit_t ascent;
    robo_font_unit_t descent;
    robo_font_unit_t gap;
    robo_font_bbox_t bb;
    int use_typo = 0;

    memset(g, 0, sizeof(*g));
    g->num_glyphs = robo_font_tt_num_glyphs(tt);
    g->number_of_h_metrics = hhea->number_of_h_metrics;

    if (robo_font_units_per_em_is_valid(head->units_per_em)) {
        g->units_per_em = head->units_per_em;
        g->upem_avail = ROBO_FONT_METRIC_PRESENT;
    } else {
        g->units_per_em = 0u;
        g->upem_avail = ROBO_FONT_METRIC_MALFORMED;
    }

    g->hhea_ascender = hhea->ascender;
    g->hhea_descender_mag = mag_descender(hhea->descender);
    g->hhea_line_gap = hhea->line_gap < 0 ? 0 : (robo_font_unit_t)hhea->line_gap;
    g->has_hhea_line = 1u;

    g->max_advance_width = hhea->advance_width_max;
    g->min_left_side_bearing = hhea->min_left_side_bearing;
    g->min_right_side_bearing = hhea->min_right_side_bearing;
    g->max_x_extent = hhea->x_max_extent;
    g->has_hhea_extrema = 1u;

    /* Caret: OT permits (1,0) when both rise and run are 0. */
    if (hhea->caret_slope_rise == 0 && hhea->caret_slope_run == 0) {
        g->caret_slope_rise = 1;
        g->caret_slope_run = 0;
        g->caret_offset = 0;
        g->caret_avail = ROBO_FONT_METRIC_FALLBACK;
    } else {
        g->caret_slope_rise = hhea->caret_slope_rise;
        g->caret_slope_run = hhea->caret_slope_run;
        g->caret_offset = hhea->caret_offset;
        g->caret_avail = ROBO_FONT_METRIC_PRESENT;
    }

    if (os2) {
        g->typo_ascender = os2->s_typo_ascender;
        g->typo_descender_mag = mag_descender(os2->s_typo_descender);
        g->typo_line_gap =
            os2->s_typo_line_gap < 0 ? 0 : (robo_font_unit_t)os2->s_typo_line_gap;
        g->has_typo_line = 1u;
        g->win_ascent = os2->us_win_ascent;
        g->win_descent = os2->us_win_descent;
        g->has_win_clip = 1u;
        use_typo = (os2->fs_selection & ROBO_FONT_OS2_USE_TYPO_METRICS) != 0u;

        set_opt_unit(&g->average_char_width, os2->x_avg_char_width,
                     ROBO_FONT_METRIC_PRESENT);
        set_opt_u16(&g->us_weight_class, os2->us_weight_class,
                    ROBO_FONT_METRIC_PRESENT);
        set_opt_u16(&g->us_width_class, os2->us_width_class,
                    ROBO_FONT_METRIC_PRESENT);
        set_opt_unit(&g->strikeout_thickness, os2->y_strikeout_size,
                     ROBO_FONT_METRIC_PRESENT);
        set_opt_unit(&g->strikeout_position, os2->y_strikeout_position,
                     ROBO_FONT_METRIC_PRESENT);
        if (os2->has_v2_metrics) {
            set_opt_unit(&g->x_height, os2->sx_height, ROBO_FONT_METRIC_PRESENT);
            set_opt_unit(&g->cap_height, os2->s_cap_height, ROBO_FONT_METRIC_PRESENT);
        } else {
            set_opt_unit(&g->x_height, 0, ROBO_FONT_METRIC_UNAVAILABLE);
            set_opt_unit(&g->cap_height, 0, ROBO_FONT_METRIC_UNAVAILABLE);
        }
    } else {
        set_opt_unit(&g->average_char_width, 0, ROBO_FONT_METRIC_UNAVAILABLE);
        set_opt_u16(&g->us_weight_class, 0, ROBO_FONT_METRIC_UNAVAILABLE);
        set_opt_u16(&g->us_width_class, 0, ROBO_FONT_METRIC_UNAVAILABLE);
        set_opt_unit(&g->strikeout_thickness, 0, ROBO_FONT_METRIC_UNAVAILABLE);
        set_opt_unit(&g->strikeout_position, 0, ROBO_FONT_METRIC_UNAVAILABLE);
        set_opt_unit(&g->x_height, 0, ROBO_FONT_METRIC_UNAVAILABLE);
        set_opt_unit(&g->cap_height, 0, ROBO_FONT_METRIC_UNAVAILABLE);
    }

    if (post) {
        set_opt_unit(&g->underline_position, post->underline_position,
                     ROBO_FONT_METRIC_PRESENT);
        set_opt_unit(&g->underline_thickness, post->underline_thickness,
                     ROBO_FONT_METRIC_PRESENT);
    } else {
        /* Spec-permitted soft defaults; clearly marked FALLBACK. */
        set_opt_unit(&g->underline_position, (robo_font_unit_t)(-(int)g->units_per_em / 5),
                     ROBO_FONT_METRIC_FALLBACK);
        set_opt_unit(&g->underline_thickness, (robo_font_unit_t)(g->units_per_em / 14),
                     ROBO_FONT_METRIC_FALLBACK);
    }

    if (head->x_min <= head->x_max && head->y_min <= head->y_max) {
        g->font_bbox.x_min = head->x_min;
        g->font_bbox.y_min = head->y_min;
        g->font_bbox.x_max = head->x_max;
        g->font_bbox.y_max = head->y_max;
        g->font_bbox_avail = ROBO_FONT_METRIC_PRESENT;
    } else {
        g->font_bbox_avail = ROBO_FONT_METRIC_MALFORMED;
    }

    if (use_typo && g->has_typo_line) {
        ascent = g->typo_ascender;
        descent = g->typo_descender_mag;
        gap = g->typo_line_gap;
        g->layout_source = ROBO_FONT_LINE_FROM_OS2_TYPO;
    } else {
        ascent = g->hhea_ascender;
        descent = g->hhea_descender_mag;
        gap = g->hhea_line_gap;
        g->layout_source = ROBO_FONT_LINE_FROM_HHEA;
    }

    bb = g->font_bbox;
    if (g->upem_avail == ROBO_FONT_METRIC_PRESENT) {
        (void)robo_font_metrics_init(
            &g->layout, g->units_per_em, ascent, descent, gap,
            g->font_bbox_avail == ROBO_FONT_METRIC_PRESENT ? &bb : NULL);
    }
}

static robo_result_t try_attach_vertical(robo_font_metrics_info_t *info)
{
    const robo_font_sfnt_t *sfnt;
    robo_font_span_t vhea_s;
    robo_font_span_t vmtx_s;
    size_t need;
    robo_result_t res;

    sfnt = robo_font_tt_sfnt(info->tt);
    if (!sfnt) {
        return ROBO_OK;
    }
    if (robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_VHEA, &vhea_s) != ROBO_OK ||
        robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_VMTX, &vmtx_s) != ROBO_OK) {
        return ROBO_OK; /* optional */
    }
    res = robo_font_vhea_parse(vhea_s, &info->vhea);
    if (res != ROBO_OK) {
        return ROBO_OK; /* leave has_vertical=0 on malformed optional tables */
    }
    if (info->vhea.number_of_v_metrics > info->global.num_glyphs) {
        return ROBO_OK;
    }
    need = expected_vmtx_size(info->global.num_glyphs, info->vhea.number_of_v_metrics);
    if (need == 0u || vmtx_s.size < need) {
        return ROBO_OK;
    }
    info->has_vhea = 1u;
    info->vmtx = vmtx_s;
    info->number_of_v_metrics = info->vhea.number_of_v_metrics;
    info->global.has_vertical = 1u;
    return ROBO_OK;
}

robo_result_t robo_font_metrics_info_create_from_tt(robo_font_tt_t *tt,
                                                    robo_font_metrics_info_t **out)
{
    robo_font_metrics_info_t *info;
    if (!tt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    info = (robo_font_metrics_info_t *)robo_calloc(1u, sizeof(*info));
    if (!info) {
        return ROBO_OUT_OF_MEMORY;
    }
    info->refs = 1u;
    robo_font_tt_retain(tt);
    info->tt = tt;
    fill_global_from_tt(tt, &info->global);
    if (info->global.upem_avail != ROBO_FONT_METRIC_PRESENT || !info->global.layout.valid) {
        robo_font_metrics_info_release(info);
        return ROBO_PARSE_ERROR;
    }
    (void)try_attach_vertical(info);
    *out = info;
    return ROBO_OK;
}

robo_result_t robo_font_metrics_info_create(robo_font_sfnt_t *sfnt,
                                            robo_font_metrics_info_t **out)
{
    robo_font_tt_t *tt = NULL;
    robo_result_t res;
    if (!sfnt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    /* CFF/CFF2 fonts share SFNT metrics tables; do not require glyf. */
    res = robo_font_tt_parse(sfnt, ROBO_FONT_TT_FLAGS_NONE, &tt);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_metrics_info_create_from_tt(tt, out);
    robo_font_tt_release(tt);
    return res;
}

void robo_font_metrics_info_retain(robo_font_metrics_info_t *info)
{
    if (!info) {
        return;
    }
    info->refs++;
}

void robo_font_metrics_info_release(robo_font_metrics_info_t *info)
{
    if (!info) {
        return;
    }
    if (info->refs == 0u) {
        return;
    }
    info->refs--;
    if (info->refs != 0u) {
        return;
    }
    if (info->tt) {
        robo_font_tt_release(info->tt);
        info->tt = NULL;
    }
    robo_free(info);
}

const robo_font_metrics_global_t *robo_font_metrics_info_global(
    const robo_font_metrics_info_t *info)
{
    return info ? &info->global : NULL;
}

robo_result_t robo_font_metrics_info_layout(const robo_font_metrics_info_t *info,
                                            robo_font_metrics_t *out)
{
    if (!info || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = info->global.layout;
    return ROBO_OK;
}

const robo_font_vhea_t *robo_font_metrics_info_vhea(
    const robo_font_metrics_info_t *info)
{
    if (!info || !info->has_vhea) {
        return NULL;
    }
    return &info->vhea;
}

static robo_result_t fill_glyph_bbox(const robo_font_tt_t *tt,
                                     robo_glyph_id_t gid,
                                     robo_font_glyph_metrics_t *gm)
{
    robo_font_glyf_glyph_t g;
    robo_result_t res;
    uint32_t len = 0u;

    gm->has_bbox = 0u;
    gm->has_rsb = 0u;
    if (robo_font_tt_glyph_length(tt, gid, &len) != ROBO_OK) {
        /* No glyf/loca (CFF path) — advances still valid. */
        return ROBO_OK;
    }
    if (len == 0u) {
        /* Empty glyph: RSB = advance - LSB (zero width). */
        gm->right_side_bearing =
            (int16_t)((int32_t)gm->advance_width - (int32_t)gm->left_side_bearing);
        gm->has_rsb = 1u;
        memset(&gm->bbox, 0, sizeof(gm->bbox));
        gm->has_bbox = 1u;
        return ROBO_OK;
    }
    memset(&g, 0, sizeof(g));
    res = robo_font_tt_parse_glyph(tt, gid, &g);
    if (res != ROBO_OK) {
        robo_font_glyf_glyph_clear(&g);
        return res;
    }
    gm->bbox.x_min = g.header.x_min;
    gm->bbox.y_min = g.header.y_min;
    gm->bbox.x_max = g.header.x_max;
    gm->bbox.y_max = g.header.y_max;
    gm->has_bbox = 1u;
    {
        int32_t width = (int32_t)g.header.x_max - (int32_t)g.header.x_min;
        int32_t rsb =
            (int32_t)gm->advance_width - (int32_t)gm->left_side_bearing - width;
        if (rsb < -32768 || rsb > 32767) {
            gm->has_rsb = 0u;
        } else {
            gm->right_side_bearing = (int16_t)rsb;
            gm->has_rsb = 1u;
        }
    }
    robo_font_glyf_glyph_clear(&g);
    return ROBO_OK;
}

robo_result_t robo_font_metrics_info_glyph(const robo_font_metrics_info_t *info,
                                           robo_glyph_id_t glyph_id,
                                           robo_font_glyph_metrics_t *out)
{
    uint16_t aw = 0u;
    int16_t lsb = 0;
    robo_result_t res;

    if (!info || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->glyph_id = glyph_id;
    if (glyph_id >= info->global.num_glyphs) {
        out->status = ROBO_FONT_GLYPH_METRICS_INVALID_ID;
        return ROBO_OK;
    }
    res = robo_font_tt_advance_width(info->tt, glyph_id, &aw);
    if (res != ROBO_OK) {
        out->status = ROBO_FONT_GLYPH_METRICS_MALFORMED;
        return ROBO_OK;
    }
    res = robo_font_tt_left_side_bearing(info->tt, glyph_id, &lsb);
    if (res != ROBO_OK) {
        out->status = ROBO_FONT_GLYPH_METRICS_MALFORMED;
        return ROBO_OK;
    }
    out->advance_width = aw;
    out->left_side_bearing = lsb;
    res = fill_glyph_bbox(info->tt, glyph_id, out);
    if (res == ROBO_PARSE_ERROR) {
        out->status = ROBO_FONT_GLYPH_METRICS_MALFORMED;
        return ROBO_OK;
    }
    if (res != ROBO_OK) {
        return res;
    }
    out->status = ROBO_FONT_GLYPH_METRICS_OK;
    return ROBO_OK;
}

robo_result_t robo_font_metrics_info_glyph_vertical(
    const robo_font_metrics_info_t *info,
    robo_glyph_id_t glyph_id,
    robo_font_vertical_glyph_metrics_t *out)
{
    robo_font_reader_t r;
    uint16_t idx;
    uint16_t ah;
    int16_t tsb;

    if (!info || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->glyph_id = glyph_id;
    if (!info->has_vhea) {
        out->status = ROBO_FONT_GLYPH_METRICS_UNAVAILABLE;
        return ROBO_OK;
    }
    if (glyph_id >= info->global.num_glyphs) {
        out->status = ROBO_FONT_GLYPH_METRICS_INVALID_ID;
        return ROBO_OK;
    }
    idx = (glyph_id < info->number_of_v_metrics)
              ? (uint16_t)glyph_id
              : (uint16_t)(info->number_of_v_metrics - 1u);
    robo_font_reader_init_span(&r, info->vmtx);
    if (robo_font_reader_seek(&r, (size_t)idx * 4u) != ROBO_OK ||
        robo_font_read_u16be(&r, &ah) != ROBO_OK ||
        robo_font_read_i16be(&r, &tsb) != ROBO_OK) {
        out->status = ROBO_FONT_GLYPH_METRICS_MALFORMED;
        return ROBO_OK;
    }
    if (glyph_id >= info->number_of_v_metrics) {
        size_t off = (size_t)info->number_of_v_metrics * 4u +
                     (size_t)(glyph_id - info->number_of_v_metrics) * 2u;
        if (robo_font_reader_seek(&r, off) != ROBO_OK ||
            robo_font_read_i16be(&r, &tsb) != ROBO_OK) {
            out->status = ROBO_FONT_GLYPH_METRICS_MALFORMED;
            return ROBO_OK;
        }
    }
    out->advance_height = ah;
    out->top_side_bearing = tsb;
    out->status = ROBO_FONT_GLYPH_METRICS_OK;
    return ROBO_OK;
}

robo_result_t robo_font_metrics_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_metrics_info_t *info = NULL;
    robo_result_t res;

    if (!data || size == 0u) {
        return ROBO_OK;
    }
    if (robo_font_source_create_memory_borrow(data, size, &src) != ROBO_OK) {
        return ROBO_OK;
    }
    res = robo_font_sfnt_parse(src, 0u, ROBO_FONT_SFNT_FLAGS_NONE, &sfnt);
    if (res == ROBO_OK) {
        (void)robo_font_metrics_info_create(sfnt, &info);
        if (info) {
            robo_font_glyph_metrics_t gm;
            robo_font_vertical_glyph_metrics_t vm;
            (void)robo_font_metrics_info_glyph(info, 0u, &gm);
            (void)robo_font_metrics_info_glyph_vertical(info, 0u, &vm);
            robo_font_metrics_info_release(info);
        }
        robo_font_sfnt_release(sfnt);
    }
    /* Also fuzz standalone vhea bytes. */
    if (size >= 36u) {
        robo_font_vhea_t vhea;
        robo_font_span_t sp;
        sp.data = data;
        sp.size = size > 64u ? 64u : size;
        (void)robo_font_vhea_parse(sp, &vhea);
    }
    robo_font_source_release(src);
    return ROBO_OK;
}
