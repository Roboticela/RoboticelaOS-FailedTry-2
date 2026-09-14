#include "RoboFont/RoboFontVariation.h"

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontCff.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define MODEL_COOKIE 0x56524D31u /* 'VRM1' */
#define COORDS_COOKIE 0x56434F31u /* 'VCO1' */

typedef struct robo_font_avar_segment {
    float from_coord;
    float to_coord;
} robo_font_avar_segment_t;

typedef struct robo_font_avar_axis {
    uint16_t segment_count;
    robo_font_avar_segment_t *segments; /* owned by model */
} robo_font_avar_axis_t;

struct robo_font_variation_model {
    uint32_t cookie;
    uint32_t refs;
    uint32_t capabilities;
    uint16_t axis_count;
    uint16_t instance_count;
    uint16_t instance_size;
    uint8_t has_avar;
    uint8_t has_ps_name_id;
    robo_font_variation_axis_t *axes;
    robo_font_variation_instance_t *instances;
    float *instance_coord_blob; /* instance_count * axis_count */
    robo_font_avar_axis_t *avar; /* axis_count when has_avar */
    uint16_t stat_axis_count;
    robo_font_tag_t *stat_tags;
};

struct robo_font_variation_coords {
    uint32_t cookie;
    uint32_t refs;
    uint16_t axis_count;
    float *user;
    float *normalized;
    float *adjusted;
    const robo_font_variation_model_t *model; /* retained */
};

static float fixed_to_float(int32_t v)
{
    return (float)((double)v / 65536.0);
}

static float f2dot14_to_float(int16_t v)
{
    return (float)((double)v / 16384.0);
}

static int float_ok(float v)
{
    union {
        float f;
        uint32_t u;
    } bits;
    bits.f = v;
    return ((bits.u >> 23) & 0xFFu) != 0xFFu;
}

robo_font_var_axis_kind_t robo_font_variation_axis_kind(robo_font_tag_t tag)
{
    if (tag == ROBO_FONT_TAG_WGHT || tag == ROBO_FONT_TAG_WDTH ||
        tag == ROBO_FONT_TAG_ITAL || tag == ROBO_FONT_TAG_SLNT ||
        tag == ROBO_FONT_TAG_OPSZ) {
        return ROBO_FONT_VAR_AXIS_REGISTERED;
    }
    if (tag == 0u) {
        return ROBO_FONT_VAR_AXIS_UNKNOWN;
    }
    return ROBO_FONT_VAR_AXIS_CUSTOM;
}

int robo_font_variation_tag_is_registered(robo_font_tag_t tag)
{
    return robo_font_variation_axis_kind(tag) == ROBO_FONT_VAR_AXIS_REGISTERED;
}

robo_result_t robo_font_variation_clamp_user(float min_v,
                                             float def_v,
                                             float max_v,
                                             float requested,
                                             float *out_clamped)
{
    float lo;
    float hi;
    float v;

    (void)def_v;
    if (!out_clamped || !float_ok(min_v) || !float_ok(max_v) || !float_ok(requested)) {
        return ROBO_INVALID_ARGUMENT;
    }
    lo = min_v < max_v ? min_v : max_v;
    hi = min_v < max_v ? max_v : min_v;
    v = requested;
    if (v < lo) {
        v = lo;
    } else if (v > hi) {
        v = hi;
    }
    *out_clamped = v;
    return ROBO_OK;
}

robo_result_t robo_font_variation_normalize(float min_v,
                                            float def_v,
                                            float max_v,
                                            float user,
                                            float *out_norm)
{
    float lo;
    float hi;
    float v;
    float den;

    if (!out_norm || !float_ok(min_v) || !float_ok(def_v) || !float_ok(max_v) ||
        !float_ok(user)) {
        return ROBO_INVALID_ARGUMENT;
    }
    lo = min_v < max_v ? min_v : max_v;
    hi = min_v < max_v ? max_v : min_v;
    v = user;
    if (v < lo) {
        v = lo;
    } else if (v > hi) {
        v = hi;
    }
    if (v == def_v) {
        *out_norm = 0.0f;
        return ROBO_OK;
    }
    if (v < def_v) {
        den = def_v - min_v;
        if (den == 0.0f || !float_ok(den)) {
            *out_norm = 0.0f;
            return ROBO_OK;
        }
        *out_norm = (v - def_v) / den;
        return ROBO_OK;
    }
    den = max_v - def_v;
    if (den == 0.0f || !float_ok(den)) {
        *out_norm = 0.0f;
        return ROBO_OK;
    }
    *out_norm = (v - def_v) / den;
    return ROBO_OK;
}

void robo_font_variation_request_init(robo_font_variation_request_t *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
    req->named_instance_index = 0xFFFFFFFFu;
    req->weight = ROBO_FONT_WEIGHT_NORMAL;
    req->stretch = ROBO_FONT_STRETCH_NORMAL;
    req->style = ROBO_FONT_STYLE_NORMAL;
    req->apply_style_axes = 1u;
}

void robo_font_outline_request_init(robo_font_outline_request_t *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
}

void robo_font_metrics_variation_context_init(robo_font_metrics_variation_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = ROBO_FONT_METRICS_VAR_STATIC;
}

void robo_font_metrics_variation_context_set(robo_font_metrics_variation_context_t *ctx,
                                             const robo_font_variation_coords_t *coords)
{
    if (!ctx) {
        return;
    }
    ctx->coords = coords;
    ctx->state = coords ? ROBO_FONT_METRICS_VAR_PENDING : ROBO_FONT_METRICS_VAR_STATIC;
}

static void model_free(robo_font_variation_model_t *m)
{
    uint32_t i;
    if (!m) {
        return;
    }
    if (m->instances) {
        for (i = 0u; i < m->instance_count; i++) {
            m->instances[i].user_coords = NULL;
        }
        robo_free(m->instances);
    }
    if (m->instance_coord_blob) {
        robo_free(m->instance_coord_blob);
    }
    if (m->avar) {
        for (i = 0u; i < m->axis_count; i++) {
            if (m->avar[i].segments) {
                robo_free(m->avar[i].segments);
            }
        }
        robo_free(m->avar);
    }
    if (m->axes) {
        robo_free(m->axes);
    }
    if (m->stat_tags) {
        robo_free(m->stat_tags);
    }
    memset(m, 0, sizeof(*m));
    robo_free(m);
}

void robo_font_variation_model_retain(robo_font_variation_model_t *m)
{
    if (!m || m->cookie != MODEL_COOKIE) {
        return;
    }
    m->refs++;
}

void robo_font_variation_model_release(robo_font_variation_model_t *m)
{
    if (!m || m->cookie != MODEL_COOKIE) {
        return;
    }
    if (m->refs == 0u) {
        return;
    }
    m->refs--;
    if (m->refs != 0u) {
        return;
    }
    m->cookie = 0u;
    model_free(m);
}

static void coords_free(robo_font_variation_coords_t *c)
{
    if (!c) {
        return;
    }
    if (c->model) {
        robo_font_variation_model_release((robo_font_variation_model_t *)c->model);
    }
    if (c->user) {
        robo_free(c->user);
    }
    if (c->normalized) {
        robo_free(c->normalized);
    }
    if (c->adjusted) {
        robo_free(c->adjusted);
    }
    memset(c, 0, sizeof(*c));
    robo_free(c);
}

void robo_font_variation_coords_retain(robo_font_variation_coords_t *c)
{
    if (!c || c->cookie != COORDS_COOKIE) {
        return;
    }
    c->refs++;
}

void robo_font_variation_coords_release(robo_font_variation_coords_t *c)
{
    if (!c || c->cookie != COORDS_COOKIE) {
        return;
    }
    if (c->refs == 0u) {
        return;
    }
    c->refs--;
    if (c->refs != 0u) {
        return;
    }
    c->cookie = 0u;
    coords_free(c);
}

static robo_result_t resolve_name(const robo_font_name_info_t *name_opt,
                                  uint16_t name_id,
                                  char *out,
                                  uint8_t *out_len)
{
    const robo_font_name_entry_t *entry = NULL;
    robo_font_name_lookup_kind_t kind;
    size_t n = 0u;
    robo_result_t r;

    out[0] = '\0';
    *out_len = 0u;
    if (!name_opt || name_id == 0xFFFFu) {
        return ROBO_OK;
    }
    r = robo_font_name_info_lookup(name_opt, name_id, NULL, &entry, &kind);
    if (r != ROBO_OK || !entry || entry->decode_status != ROBO_FONT_NAME_PRESENT ||
        !entry->utf8) {
        return ROBO_OK;
    }
    n = entry->utf8_len;
    if (n >= ROBO_FONT_VAR_MAX_NAME_BYTES) {
        n = ROBO_FONT_VAR_MAX_NAME_BYTES - 1u;
    }
    memcpy(out, entry->utf8, n);
    out[n] = '\0';
    *out_len = (uint8_t)n;
    (void)kind;
    return ROBO_OK;
}

static int axis_range_ok(float min_v, float def_v, float max_v)
{
    float lo;
    float hi;
    if (!float_ok(min_v) || !float_ok(def_v) || !float_ok(max_v)) {
        return 0;
    }
    lo = min_v < max_v ? min_v : max_v;
    hi = min_v < max_v ? max_v : min_v;
    return def_v >= lo && def_v <= hi;
}

static robo_result_t parse_avar(robo_font_span_t span,
                                uint16_t axis_count,
                                robo_font_avar_axis_t **out_avar)
{
    robo_font_reader_t r;
    uint16_t major;
    uint16_t minor;
    uint16_t reserved;
    uint16_t acount;
    uint16_t ai;
    robo_font_avar_axis_t *avar;
    size_t i;

    *out_avar = NULL;
    if (span.size < 8u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &major) != ROBO_OK ||
        robo_font_read_u16be(&r, &minor) != ROBO_OK ||
        robo_font_read_u16be(&r, &reserved) != ROBO_OK ||
        robo_font_read_u16be(&r, &acount) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    (void)minor;
    (void)reserved;
    if (major != 1u || acount != axis_count) {
        return ROBO_PARSE_ERROR;
    }
    avar = (robo_font_avar_axis_t *)robo_calloc(axis_count, sizeof(*avar));
    if (!avar) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (ai = 0u; ai < axis_count; ai++) {
        uint16_t nseg;
        uint16_t s;
        if (robo_font_read_u16be(&r, &nseg) != ROBO_OK) {
            goto fail;
        }
        if (nseg > ROBO_FONT_VAR_MAX_AVAR_SEGMENTS) {
            goto fail;
        }
        avar[ai].segment_count = nseg;
        if (nseg == 0u) {
            continue;
        }
        avar[ai].segments =
            (robo_font_avar_segment_t *)robo_calloc(nseg, sizeof(robo_font_avar_segment_t));
        if (!avar[ai].segments) {
            goto fail;
        }
        for (s = 0u; s < nseg; s++) {
            int16_t from_i;
            int16_t to_i;
            if (robo_font_read_i16be(&r, &from_i) != ROBO_OK ||
                robo_font_read_i16be(&r, &to_i) != ROBO_OK) {
                goto fail;
            }
            avar[ai].segments[s].from_coord = f2dot14_to_float(from_i);
            avar[ai].segments[s].to_coord = f2dot14_to_float(to_i);
            if (!float_ok(avar[ai].segments[s].from_coord) ||
                !float_ok(avar[ai].segments[s].to_coord)) {
                goto fail;
            }
            if (s > 0u &&
                avar[ai].segments[s].from_coord < avar[ai].segments[s - 1u].from_coord) {
                /* Non-monotonic — reject mapping for this axis (identity later). */
                robo_free(avar[ai].segments);
                avar[ai].segments = NULL;
                avar[ai].segment_count = 0u;
                /* Skip remaining pairs for this axis. */
                for (s = (uint16_t)(s + 1u); s < nseg; s++) {
                    int16_t junk;
                    if (robo_font_read_i16be(&r, &junk) != ROBO_OK ||
                        robo_font_read_i16be(&r, &junk) != ROBO_OK) {
                        goto fail;
                    }
                }
                break;
            }
            if (s > 0u &&
                avar[ai].segments[s].from_coord == avar[ai].segments[s - 1u].from_coord) {
                /* Duplicate fromCoord — drop axis map. */
                robo_free(avar[ai].segments);
                avar[ai].segments = NULL;
                avar[ai].segment_count = 0u;
                for (s = (uint16_t)(s + 1u); s < nseg; s++) {
                    int16_t junk;
                    if (robo_font_read_i16be(&r, &junk) != ROBO_OK ||
                        robo_font_read_i16be(&r, &junk) != ROBO_OK) {
                        goto fail;
                    }
                }
                break;
            }
        }
    }
    *out_avar = avar;
    return ROBO_OK;

fail:
    if (avar) {
        for (i = 0u; i < axis_count; i++) {
            if (avar[i].segments) {
                robo_free(avar[i].segments);
            }
        }
        robo_free(avar);
    }
    return ROBO_PARSE_ERROR;
}

static float avar_map_segments(const robo_font_avar_axis_t *ax, float value)
{
    uint16_t i;
    uint16_t n;
    const robo_font_avar_segment_t *map;

    if (!ax || ax->segment_count == 0u || !ax->segments) {
        return value;
    }
    n = ax->segment_count;
    map = ax->segments;
    if (n < 2u) {
        if (n == 1u) {
            return value - map[0].from_coord + map[0].to_coord;
        }
        return value;
    }
    for (i = 0u; i < n; i++) {
        if (value == map[i].from_coord) {
            return map[i].to_coord;
        }
    }
    if (value <= map[0].from_coord) {
        float den = map[1].from_coord - map[0].from_coord;
        if (den == 0.0f) {
            return map[0].to_coord;
        }
        return map[0].to_coord +
               (value - map[0].from_coord) * (map[1].to_coord - map[0].to_coord) / den;
    }
    if (value >= map[n - 1u].from_coord) {
        float den = map[n - 1u].from_coord - map[n - 2u].from_coord;
        if (den == 0.0f) {
            return map[n - 1u].to_coord;
        }
        return map[n - 2u].to_coord +
               (value - map[n - 2u].from_coord) *
                   (map[n - 1u].to_coord - map[n - 2u].to_coord) / den;
    }
    for (i = 1u; i < n; i++) {
        if (value <= map[i].from_coord) {
            float den = map[i].from_coord - map[i - 1u].from_coord;
            if (den == 0.0f) {
                return map[i].to_coord;
            }
            return map[i - 1u].to_coord +
                   (value - map[i - 1u].from_coord) *
                       (map[i].to_coord - map[i - 1u].to_coord) / den;
        }
    }
    return value;
}

robo_result_t robo_font_variation_avar_map(const robo_font_variation_model_t *m,
                                           uint32_t axis_index,
                                           float normalized,
                                           float *out_adjusted)
{
    if (!m || m->cookie != MODEL_COOKIE || !out_adjusted || !float_ok(normalized)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (axis_index >= m->axis_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!m->has_avar || !m->avar) {
        *out_adjusted = normalized;
        return ROBO_OK;
    }
    *out_adjusted = avar_map_segments(&m->avar[axis_index], normalized);
    if (!float_ok(*out_adjusted)) {
        *out_adjusted = normalized;
    }
    return ROBO_OK;
}

robo_result_t robo_font_variation_model_create(robo_font_sfnt_t *sfnt,
                                               const robo_font_stat_t *stat_opt,
                                               const robo_font_name_info_t *name_opt,
                                               robo_font_variation_model_t **out)
{
    robo_font_span_t fvar_span;
    robo_font_span_t avar_span;
    robo_font_reader_t r;
    robo_font_variation_model_t *m = NULL;
    uint16_t major;
    uint16_t minor;
    uint16_t axes_off;
    uint16_t reserved;
    uint16_t axis_count;
    uint16_t axis_size;
    uint16_t instance_count;
    uint16_t instance_size;
    uint16_t i;
    uint32_t j;
    robo_result_t res;
    int has_gvar = 0;
    int has_hvar = 0;
    int has_mvar = 0;
    int has_cff2 = 0;
    uint32_t caps = 0u;

    if (!out || !sfnt) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    res = robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_FVAR, &fvar_span);
    if (res != ROBO_OK) {
        return ROBO_NOT_FOUND;
    }
    if (fvar_span.size < 16u) {
        return ROBO_PARSE_ERROR;
    }

    has_gvar = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_GVAR);
    has_hvar = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_HVAR);
    has_mvar = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_MVAR);
    has_cff2 = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CFF2);

    robo_font_reader_init_span(&r, fvar_span);
    if (robo_font_read_u16be(&r, &major) != ROBO_OK ||
        robo_font_read_u16be(&r, &minor) != ROBO_OK ||
        robo_font_read_u16be(&r, &axes_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &reserved) != ROBO_OK ||
        robo_font_read_u16be(&r, &axis_count) != ROBO_OK ||
        robo_font_read_u16be(&r, &axis_size) != ROBO_OK ||
        robo_font_read_u16be(&r, &instance_count) != ROBO_OK ||
        robo_font_read_u16be(&r, &instance_size) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    (void)minor;
    (void)reserved;
    if (major != 1u || axis_count == 0u || axis_count > ROBO_FONT_VAR_MAX_AXES ||
        axis_size < 20u || instance_count > ROBO_FONT_VAR_MAX_INSTANCES) {
        return ROBO_PARSE_ERROR;
    }
    if ((size_t)axes_off + (size_t)axis_count * (size_t)axis_size > fvar_span.size) {
        return ROBO_PARSE_ERROR;
    }
    {
        size_t need = 4u + (size_t)axis_count * 4u;
        if (instance_size < need) {
            return ROBO_PARSE_ERROR;
        }
    }

    m = (robo_font_variation_model_t *)robo_calloc(1, sizeof(*m));
    if (!m) {
        return ROBO_OUT_OF_MEMORY;
    }
    m->cookie = MODEL_COOKIE;
    m->refs = 1u;
    m->axis_count = axis_count;
    m->instance_count = instance_count;
    m->instance_size = instance_size;
    m->has_ps_name_id = (instance_size >= (uint16_t)(4u + axis_count * 4u + 2u)) ? 1u : 0u;
    m->axes = (robo_font_variation_axis_t *)robo_calloc(axis_count, sizeof(*m->axes));
    if (!m->axes) {
        model_free(m);
        return ROBO_OUT_OF_MEMORY;
    }

    for (i = 0u; i < axis_count; i++) {
        size_t off = (size_t)axes_off + (size_t)i * (size_t)axis_size;
        uint32_t tag;
        int32_t min_f;
        int32_t def_f;
        int32_t max_f;
        uint16_t flags;
        uint16_t name_id;
        robo_font_reader_t ar;

        if (robo_font_reader_seek(&r, off) != ROBO_OK) {
            model_free(m);
            return ROBO_PARSE_ERROR;
        }
        ar = r;
        if (robo_font_read_u32be(&ar, &tag) != ROBO_OK ||
            robo_font_read_i32be(&ar, &min_f) != ROBO_OK ||
            robo_font_read_i32be(&ar, &def_f) != ROBO_OK ||
            robo_font_read_i32be(&ar, &max_f) != ROBO_OK ||
            robo_font_read_u16be(&ar, &flags) != ROBO_OK ||
            robo_font_read_u16be(&ar, &name_id) != ROBO_OK) {
            model_free(m);
            return ROBO_PARSE_ERROR;
        }
        for (j = 0u; j < i; j++) {
            if (m->axes[j].tag == tag) {
                model_free(m);
                return ROBO_PARSE_ERROR;
            }
        }
        m->axes[i].tag = tag;
        m->axes[i].min_value = fixed_to_float(min_f);
        m->axes[i].default_value = fixed_to_float(def_f);
        m->axes[i].max_value = fixed_to_float(max_f);
        m->axes[i].flags = flags;
        m->axes[i].name_id = name_id;
        m->axes[i].kind = robo_font_variation_axis_kind(tag);
        if (!axis_range_ok(m->axes[i].min_value, m->axes[i].default_value,
                           m->axes[i].max_value)) {
            model_free(m);
            return ROBO_PARSE_ERROR;
        }
        (void)resolve_name(name_opt, name_id, m->axes[i].name_utf8, &m->axes[i].name_len);
    }

    if (instance_count > 0u) {
        size_t inst_base = (size_t)axes_off + (size_t)axis_count * (size_t)axis_size;
        size_t blob_n = (size_t)instance_count * (size_t)axis_count;

        if (inst_base + (size_t)instance_count * (size_t)instance_size > fvar_span.size) {
            model_free(m);
            return ROBO_PARSE_ERROR;
        }
        m->instances =
            (robo_font_variation_instance_t *)robo_calloc(instance_count, sizeof(*m->instances));
        m->instance_coord_blob = (float *)robo_calloc(blob_n, sizeof(float));
        if (!m->instances || !m->instance_coord_blob) {
            model_free(m);
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < instance_count; i++) {
            size_t off = inst_base + (size_t)i * (size_t)instance_size;
            uint16_t sub_id;
            uint16_t iflags;
            uint16_t a;
            robo_font_reader_t ir;

            if (robo_font_reader_seek(&r, off) != ROBO_OK) {
                model_free(m);
                return ROBO_PARSE_ERROR;
            }
            ir = r;
            if (robo_font_read_u16be(&ir, &sub_id) != ROBO_OK ||
                robo_font_read_u16be(&ir, &iflags) != ROBO_OK) {
                model_free(m);
                return ROBO_PARSE_ERROR;
            }
            m->instances[i].index = i;
            m->instances[i].subfamily_name_id = sub_id;
            m->instances[i].flags = iflags;
            m->instances[i].postscript_name_id = 0xFFFFu;
            m->instances[i].user_coords =
                m->instance_coord_blob + (size_t)i * (size_t)axis_count;
            for (a = 0u; a < axis_count; a++) {
                int32_t cf;
                if (robo_font_read_i32be(&ir, &cf) != ROBO_OK) {
                    model_free(m);
                    return ROBO_PARSE_ERROR;
                }
                m->instances[i].user_coords[a] = fixed_to_float(cf);
                if (!float_ok(m->instances[i].user_coords[a])) {
                    model_free(m);
                    return ROBO_PARSE_ERROR;
                }
            }
            if (m->has_ps_name_id) {
                uint16_t psid;
                if (robo_font_read_u16be(&ir, &psid) == ROBO_OK) {
                    m->instances[i].postscript_name_id = psid;
                }
            }
            (void)resolve_name(name_opt, sub_id, m->instances[i].name_utf8,
                               &m->instances[i].name_len);
        }
        caps |= ROBO_FONT_VAR_CAP_NAMED_INSTANCES;
    }

    caps |= ROBO_FONT_VAR_CAP_FVAR;
    if (has_gvar) {
        caps |= ROBO_FONT_VAR_CAP_GVAR;
    }
    if (has_hvar) {
        caps |= ROBO_FONT_VAR_CAP_HVAR;
    }
    if (has_mvar) {
        caps |= ROBO_FONT_VAR_CAP_MVAR;
    }
    if (has_cff2) {
        caps |= ROBO_FONT_VAR_CAP_CFF2;
    }

    if (robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_AVAR, &avar_span) == ROBO_OK) {
        robo_font_avar_axis_t *avar = NULL;
        if (parse_avar(avar_span, axis_count, &avar) == ROBO_OK && avar) {
            m->avar = avar;
            m->has_avar = 1u;
            caps |= ROBO_FONT_VAR_CAP_AVAR;
        }
        /* Malformed avar: keep fvar, ignore avar. */
    }

    if (stat_opt && stat_opt->axes && stat_opt->design_axis_count > 0u) {
        uint16_t sc = stat_opt->design_axis_count;
        uint16_t k;
        m->stat_tags = (robo_font_tag_t *)robo_calloc(sc, sizeof(robo_font_tag_t));
        if (m->stat_tags) {
            m->stat_axis_count = sc;
            for (k = 0u; k < sc; k++) {
                m->stat_tags[k] = stat_opt->axes[k].tag;
            }
            caps |= ROBO_FONT_VAR_CAP_STAT;
        }
    }

    m->capabilities = caps;
    *out = m;
    return ROBO_OK;
}

robo_result_t robo_font_variation_model_create_from_face(robo_font_face_t *face,
                                                         robo_font_variation_model_t **out)
{
    const robo_font_sfnt_t *sfnt;
    const robo_font_name_info_t *name = NULL;
    robo_font_stat_t stat;
    robo_font_stat_t *stat_ptr = NULL;
    robo_font_ot_t *ot = NULL;
    robo_result_t res;
    robo_font_span_t span;

    if (!face || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    res = robo_font_face_parse_sfnt(face);
    if (res == ROBO_NOT_SUPPORTED || res == ROBO_PARSE_ERROR || res == ROBO_NOT_FOUND) {
        /* No usable SFNT → not a variable face. */
        return ROBO_NOT_FOUND;
    }
    if (res != ROBO_OK) {
        return res;
    }
    sfnt = robo_font_face_sfnt(face);
    if (!sfnt) {
        return ROBO_PARSE_ERROR;
    }
    (void)robo_font_face_parse_metadata(face);
    name = robo_font_face_name_info(face);

    memset(&stat, 0, sizeof(stat));
    if (robo_font_sfnt_table_bytes((robo_font_sfnt_t *)sfnt, ROBO_FONT_TAG_STAT, &span) ==
        ROBO_OK) {
        if (robo_font_stat_parse(span, &stat) == ROBO_OK) {
            stat_ptr = &stat;
        }
    }
    (void)ot;

    res = robo_font_variation_model_create((robo_font_sfnt_t *)sfnt, stat_ptr, name, out);
    if (stat_ptr) {
        robo_font_stat_clear(&stat);
    }
    return res;
}

uint32_t robo_font_variation_model_capabilities(const robo_font_variation_model_t *m)
{
    return m && m->cookie == MODEL_COOKIE ? m->capabilities : 0u;
}

int robo_font_variation_model_is_variable(const robo_font_variation_model_t *m)
{
    return m && m->cookie == MODEL_COOKIE && m->axis_count > 0u;
}

uint32_t robo_font_variation_model_axis_count(const robo_font_variation_model_t *m)
{
    return m && m->cookie == MODEL_COOKIE ? m->axis_count : 0u;
}

const robo_font_variation_axis_t *robo_font_variation_model_axis(
    const robo_font_variation_model_t *m,
    uint32_t index)
{
    if (!m || m->cookie != MODEL_COOKIE || index >= m->axis_count) {
        return NULL;
    }
    return &m->axes[index];
}

robo_result_t robo_font_variation_model_find_axis(const robo_font_variation_model_t *m,
                                                  robo_font_tag_t tag,
                                                  uint32_t *out_index,
                                                  const robo_font_variation_axis_t **out_axis)
{
    uint32_t i;
    if (!m || m->cookie != MODEL_COOKIE) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < m->axis_count; i++) {
        if (m->axes[i].tag == tag) {
            if (out_index) {
                *out_index = i;
            }
            if (out_axis) {
                *out_axis = &m->axes[i];
            }
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

uint32_t robo_font_variation_model_instance_count(const robo_font_variation_model_t *m)
{
    return m && m->cookie == MODEL_COOKIE ? m->instance_count : 0u;
}

const robo_font_variation_instance_t *robo_font_variation_model_instance(
    const robo_font_variation_model_t *m,
    uint32_t index)
{
    if (!m || m->cookie != MODEL_COOKIE || index >= m->instance_count || !m->instances) {
        return NULL;
    }
    return &m->instances[index];
}

int robo_font_variation_model_has_avar(const robo_font_variation_model_t *m)
{
    return m && m->cookie == MODEL_COOKIE && m->has_avar;
}

uint32_t robo_font_variation_model_stat_axis_count(const robo_font_variation_model_t *m)
{
    return m && m->cookie == MODEL_COOKIE ? m->stat_axis_count : 0u;
}

robo_font_tag_t robo_font_variation_model_stat_axis_tag(const robo_font_variation_model_t *m,
                                                        uint32_t index)
{
    if (!m || m->cookie != MODEL_COOKIE || index >= m->stat_axis_count || !m->stat_tags) {
        return 0u;
    }
    return m->stat_tags[index];
}

static void set_axis_user(float *user,
                          const robo_font_variation_model_t *m,
                          uint32_t axis_i,
                          float v)
{
    float clamped = m->axes[axis_i].default_value;
    (void)robo_font_variation_clamp_user(m->axes[axis_i].min_value,
                                         m->axes[axis_i].default_value,
                                         m->axes[axis_i].max_value, v, &clamped);
    user[axis_i] = clamped;
}

static robo_result_t coords_finish(robo_font_variation_coords_t *c)
{
    uint32_t i;
    for (i = 0u; i < c->axis_count; i++) {
        float n = 0.0f;
        float a = 0.0f;
        if (robo_font_variation_normalize(c->model->axes[i].min_value,
                                          c->model->axes[i].default_value,
                                          c->model->axes[i].max_value, c->user[i],
                                          &n) != ROBO_OK) {
            return ROBO_INVALID_ARGUMENT;
        }
        c->normalized[i] = n;
        if (robo_font_variation_avar_map(c->model, i, n, &a) != ROBO_OK) {
            a = n;
        }
        c->adjusted[i] = a;
    }
    return ROBO_OK;
}

robo_result_t robo_font_variation_coords_create_default(
    const robo_font_variation_model_t *model,
    robo_font_variation_coords_t **out)
{
    robo_font_variation_request_t req;
    robo_font_variation_request_init(&req);
    req.apply_style_axes = 0u;
    return robo_font_variation_coords_create(model, &req, out);
}

robo_result_t robo_font_variation_coords_create(const robo_font_variation_model_t *model,
                                                const robo_font_variation_request_t *req,
                                                robo_font_variation_coords_t **out)
{
    robo_font_variation_coords_t *c;
    uint32_t i;
    uint32_t a;

    if (!model || model->cookie != MODEL_COOKIE || !req || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    c = (robo_font_variation_coords_t *)robo_calloc(1, sizeof(*c));
    if (!c) {
        return ROBO_OUT_OF_MEMORY;
    }
    c->cookie = COORDS_COOKIE;
    c->refs = 1u;
    c->axis_count = model->axis_count;
    c->user = (float *)robo_calloc(model->axis_count, sizeof(float));
    c->normalized = (float *)robo_calloc(model->axis_count, sizeof(float));
    c->adjusted = (float *)robo_calloc(model->axis_count, sizeof(float));
    if (!c->user || !c->normalized || !c->adjusted) {
        coords_free(c);
        return ROBO_OUT_OF_MEMORY;
    }
    robo_font_variation_model_retain((robo_font_variation_model_t *)model);
    c->model = model;

    for (i = 0u; i < model->axis_count; i++) {
        c->user[i] = model->axes[i].default_value;
    }

    if (req->has_named_instance) {
        const robo_font_variation_instance_t *inst;
        if (req->named_instance_index >= model->instance_count || !model->instances) {
            coords_free(c);
            return ROBO_NOT_FOUND;
        }
        inst = &model->instances[req->named_instance_index];
        for (i = 0u; i < model->axis_count; i++) {
            set_axis_user(c->user, model, i, inst->user_coords[i]);
        }
    }

    if (req->apply_style_axes) {
        uint32_t idx;
        const robo_font_variation_axis_t *ax;
        if (robo_font_variation_model_find_axis(model, ROBO_FONT_TAG_WGHT, &idx, &ax) ==
            ROBO_OK) {
            set_axis_user(c->user, model, idx, (float)req->weight);
        }
        if (robo_font_variation_model_find_axis(model, ROBO_FONT_TAG_WDTH, &idx, &ax) ==
            ROBO_OK) {
            set_axis_user(c->user, model, idx, (float)req->stretch);
        }
        if (req->style == ROBO_FONT_STYLE_ITALIC &&
            robo_font_variation_model_find_axis(model, ROBO_FONT_TAG_ITAL, &idx, &ax) ==
                ROBO_OK) {
            set_axis_user(c->user, model, idx, ax->max_value);
        } else if (req->style == ROBO_FONT_STYLE_OBLIQUE &&
                   robo_font_variation_model_find_axis(model, ROBO_FONT_TAG_SLNT, &idx,
                                                       &ax) == ROBO_OK) {
            /* Prefer negative slant when range spans negative values. */
            float slant = ax->min_value < 0.0f ? ax->min_value : ax->max_value;
            set_axis_user(c->user, model, idx, slant);
        } else if (req->style == ROBO_FONT_STYLE_NORMAL &&
                   robo_font_variation_model_find_axis(model, ROBO_FONT_TAG_ITAL, &idx,
                                                       &ax) == ROBO_OK) {
            set_axis_user(c->user, model, idx, ax->min_value);
        }
    }

    if (req->has_optical_size || req->optical_size_auto) {
        uint32_t idx;
        const robo_font_variation_axis_t *ax;
        if (robo_font_variation_model_find_axis(model, ROBO_FONT_TAG_OPSZ, &idx, &ax) ==
            ROBO_OK) {
            float v = req->has_optical_size ? req->optical_size : ax->default_value;
            set_axis_user(c->user, model, idx, v);
        }
    }

    if (req->axes && req->axis_count > 0u) {
        for (a = 0u; a < req->axis_count; a++) {
            uint32_t idx;
            if (req->axes[a].tag == 0u || !float_ok(req->axes[a].value)) {
                continue;
            }
            if (robo_font_variation_model_find_axis(model, req->axes[a].tag, &idx, NULL) ==
                ROBO_OK) {
                set_axis_user(c->user, model, idx, req->axes[a].value);
            }
        }
    }

    if (coords_finish(c) != ROBO_OK) {
        coords_free(c);
        return ROBO_INVALID_ARGUMENT;
    }
    *out = c;
    return ROBO_OK;
}

uint32_t robo_font_variation_coords_axis_count(const robo_font_variation_coords_t *c)
{
    return c && c->cookie == COORDS_COOKIE ? c->axis_count : 0u;
}

float robo_font_variation_coords_user(const robo_font_variation_coords_t *c, uint32_t i)
{
    if (!c || c->cookie != COORDS_COOKIE || i >= c->axis_count || !c->user) {
        return 0.0f;
    }
    return c->user[i];
}

float robo_font_variation_coords_normalized(const robo_font_variation_coords_t *c,
                                            uint32_t i)
{
    if (!c || c->cookie != COORDS_COOKIE || i >= c->axis_count || !c->normalized) {
        return 0.0f;
    }
    return c->normalized[i];
}

float robo_font_variation_coords_adjusted(const robo_font_variation_coords_t *c, uint32_t i)
{
    if (!c || c->cookie != COORDS_COOKIE || i >= c->axis_count || !c->adjusted) {
        return 0.0f;
    }
    return c->adjusted[i];
}

robo_result_t robo_font_variation_coords_user_for_tag(const robo_font_variation_coords_t *c,
                                                      robo_font_tag_t tag,
                                                      float *out)
{
    uint32_t idx;
    if (!c || c->cookie != COORDS_COOKIE || !out || !c->model) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_font_variation_model_find_axis(c->model, tag, &idx, NULL) != ROBO_OK) {
        return ROBO_NOT_FOUND;
    }
    *out = c->user[idx];
    return ROBO_OK;
}

void robo_font_variation_request_from_style(const robo_font_style_desc_t *style,
                                            float optical_size,
                                            uint8_t has_optical_size,
                                            uint8_t optical_size_auto,
                                            uint32_t named_instance_index,
                                            uint8_t has_named_instance,
                                            robo_font_variation_request_t *out)
{
    robo_font_variation_request_init(out);
    if (!style || !out) {
        return;
    }
    out->apply_style_axes = 1u;
    out->weight = style->weight;
    out->stretch = style->stretch;
    out->style = style->style;
    out->axes = style->axes;
    out->axis_count = style->axis_count;
    out->has_optical_size = has_optical_size;
    out->optical_size = optical_size;
    out->optical_size_auto = optical_size_auto;
    out->has_named_instance = has_named_instance;
    out->named_instance_index = named_instance_index;
}

int robo_font_variation_can_satisfy_weight(const robo_font_variation_model_t *m,
                                           robo_font_weight_t weight)
{
    const robo_font_variation_axis_t *ax;
    float w = (float)weight;
    if (robo_font_variation_model_find_axis(m, ROBO_FONT_TAG_WGHT, NULL, &ax) != ROBO_OK) {
        return 0;
    }
    return w >= ax->min_value && w <= ax->max_value;
}

int robo_font_variation_can_satisfy_stretch(const robo_font_variation_model_t *m,
                                            robo_font_stretch_t stretch)
{
    const robo_font_variation_axis_t *ax;
    float s = (float)stretch;
    if (robo_font_variation_model_find_axis(m, ROBO_FONT_TAG_WDTH, NULL, &ax) != ROBO_OK) {
        return 0;
    }
    return s >= ax->min_value && s <= ax->max_value;
}

int robo_font_variation_can_satisfy_style(const robo_font_variation_model_t *m,
                                          robo_font_style_t style)
{
    if (style == ROBO_FONT_STYLE_ITALIC) {
        return robo_font_variation_model_find_axis(m, ROBO_FONT_TAG_ITAL, NULL, NULL) ==
               ROBO_OK;
    }
    if (style == ROBO_FONT_STYLE_OBLIQUE) {
        return robo_font_variation_model_find_axis(m, ROBO_FONT_TAG_SLNT, NULL, NULL) ==
                   ROBO_OK ||
               robo_font_variation_model_find_axis(m, ROBO_FONT_TAG_ITAL, NULL, NULL) ==
                   ROBO_OK;
    }
    return 1;
}

int robo_font_outline_variation_applied(const robo_font_outline_t *o)
{
    (void)o;
    return 0;
}

robo_result_t robo_font_variation_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_variation_model_t *model = NULL;
    robo_font_variation_coords_t *coords = NULL;
    robo_font_variation_request_t req;
    float n = 0.0f;
    float a = 0.0f;
    robo_result_t r;

    if (!data && size != 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (size == 0u) {
        return ROBO_OK;
    }
    if (size > 256u * 1024u) {
        size = 256u * 1024u;
    }
    r = robo_font_source_create_memory_copy(data, size, &src);
    if (r != ROBO_OK) {
        return ROBO_OK;
    }
    if (robo_font_sfnt_parse(src, 0u, 0u, &sfnt) != ROBO_OK) {
        robo_font_source_release(src);
        return ROBO_OK;
    }
    if (robo_font_variation_model_create(sfnt, NULL, NULL, &model) == ROBO_OK && model) {
        uint32_t i;
        (void)robo_font_variation_normalize(-1.0f, 0.0f, 1.0f,
                                            (float)(data[0] % 3u) - 1.0f, &n);
        for (i = 0u; i < robo_font_variation_model_axis_count(model) && i < 8u; i++) {
            (void)robo_font_variation_avar_map(model, i, n, &a);
        }
        robo_font_variation_request_init(&req);
        req.apply_style_axes = 1u;
        req.weight = (robo_font_weight_t)(100u + (data[0] % 9u) * 100u);
        if (robo_font_variation_coords_create(model, &req, &coords) == ROBO_OK && coords) {
            (void)robo_font_variation_coords_adjusted(coords, 0u);
            robo_font_variation_coords_release(coords);
        }
        /* Pathological named instance index */
        req.has_named_instance = 1u;
        req.named_instance_index = 0xFFFFFFFEu;
        (void)robo_font_variation_coords_create(model, &req, &coords);
        if (coords) {
            robo_font_variation_coords_release(coords);
        }
        robo_font_variation_model_release(model);
    }
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
    return ROBO_OK;
}
