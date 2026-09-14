#include "RoboFont/RoboFontSelection.h"

#include "RoboFont/RoboFontVariation.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

static int select_float_ok(float v)
{
    union {
        float f;
        uint32_t u;
    } bits;
    bits.f = v;
    return ((bits.u >> 23) & 0xFFu) != 0xFFu;
}

void robo_font_select_request_init(robo_font_select_request_t *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
    req->weight = ROBO_FONT_WEIGHT_NORMAL;
    req->stretch = ROBO_FONT_STRETCH_NORMAL;
    req->style = ROBO_FONT_STYLE_NORMAL;
    req->flags = ROBO_FONT_SELECT_FLAG_ALLOW_FULL_NAME;
}

robo_result_t robo_font_select_request_validate(const robo_font_select_request_t *req)
{
    size_t flen;

    if (!req) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!req->family) {
        return ROBO_INVALID_ARGUMENT;
    }
    flen = req->family_len != 0u ? req->family_len : strlen(req->family);
    if (flen == 0u || flen > ROBO_FONT_SELECT_MAX_FAMILY_BYTES) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (req->alias_count > ROBO_FONT_SELECT_MAX_ALIASES) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (req->alias_count != 0u && !req->aliases) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_font_weight_is_valid(req->weight)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_font_stretch_is_valid(req->stretch)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (req->style != ROBO_FONT_STYLE_NORMAL && req->style != ROBO_FONT_STYLE_ITALIC &&
        req->style != ROBO_FONT_STYLE_OBLIQUE && req->style != ROBO_FONT_STYLE_UNKNOWN) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (req->axis_count > ROBO_FONT_STYLE_MAX_AXES) {
        return ROBO_INVALID_ARGUMENT;
    }
    {
        uint32_t i;
        for (i = 0u; i < req->axis_count; i++) {
            if (req->axes[i].tag == 0u || !select_float_ok(req->axes[i].value)) {
                return ROBO_INVALID_ARGUMENT;
            }
        }
    }
    if (req->has_optical_size && !select_float_ok(req->optical_size)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

void robo_font_selection_init(robo_font_selection_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    robo_font_style_desc_init(&out->requested);
    robo_font_style_desc_init(&out->selected);
    out->cookie = ROBO_FONT_SELECTION_COOKIE;
}

void robo_font_selection_clear(robo_font_selection_t *sel)
{
    if (!sel) {
        return;
    }
    if (sel->cookie == ROBO_FONT_SELECTION_COOKIE && sel->face) {
        robo_font_face_release(sel->face);
        sel->face = NULL;
    }
    robo_font_selection_init(sel);
}

uint32_t robo_font_select_stretch_dist(robo_font_stretch_t requested,
                                       robo_font_stretch_t candidate)
{
    int d;
    uint32_t wrong = 0u;

    d = (int)candidate - (int)requested;
    if (d < 0) {
        d = -d;
    }
    if (requested < ROBO_FONT_STRETCH_NORMAL) {
        /* Prefer condensed (≤ requested) before expanded. */
        if (candidate > requested) {
            wrong = 1u;
        }
    } else if (requested > ROBO_FONT_STRETCH_NORMAL) {
        if (candidate < requested) {
            wrong = 1u;
        }
    }
    return wrong * 10000u + (uint32_t)d;
}

uint32_t robo_font_select_weight_dist(robo_font_weight_t requested,
                                      robo_font_weight_t candidate)
{
    int d;
    uint32_t wrong = 0u;

    d = (int)candidate - (int)requested;
    if (d < 0) {
        d = -d;
    }
    if (requested < 400u) {
        if (candidate > requested) {
            wrong = 1u;
        }
    } else if (requested > 500u) {
        if (candidate < requested) {
            wrong = 1u;
        }
    } else {
        /* 400..500: prefer lighter (≤ requested) first. */
        if (candidate > requested) {
            wrong = 1u;
        }
    }
    return wrong * 10000u + (uint32_t)d;
}

static uint32_t abs_i32(int32_t v)
{
    if (v < 0) {
        return (uint32_t)(-(int64_t)v);
    }
    return (uint32_t)v;
}

uint32_t robo_font_select_style_dist(robo_font_style_t requested,
                                     robo_font_style_t candidate,
                                     uint8_t req_has_angle,
                                     int32_t req_angle_mdeg,
                                     uint8_t cand_has_angle,
                                     int32_t cand_angle_mdeg)
{
    uint32_t angle_pen = 0u;

    if (req_has_angle && cand_has_angle) {
        angle_pen = abs_i32(req_angle_mdeg - cand_angle_mdeg) / 100u; /* ~0.1 deg units */
        if (angle_pen > 5000u) {
            angle_pen = 5000u;
        }
    } else if (req_has_angle != cand_has_angle) {
        angle_pen = 50u;
    }

    if (requested == ROBO_FONT_STYLE_UNKNOWN) {
        requested = ROBO_FONT_STYLE_NORMAL;
    }
    if (candidate == ROBO_FONT_STYLE_UNKNOWN) {
        candidate = ROBO_FONT_STYLE_NORMAL;
    }

    if (requested == ROBO_FONT_STYLE_NORMAL) {
        if (candidate == ROBO_FONT_STYLE_NORMAL) {
            return 0u;
        }
        if (candidate == ROBO_FONT_STYLE_OBLIQUE) {
            return 200u + angle_pen;
        }
        return 300u; /* italic */
    }
    if (requested == ROBO_FONT_STYLE_ITALIC) {
        if (candidate == ROBO_FONT_STYLE_ITALIC) {
            return 0u;
        }
        if (candidate == ROBO_FONT_STYLE_OBLIQUE) {
            return 100u + angle_pen;
        }
        return 1000u; /* normal */
    }
    /* OBLIQUE request */
    if (candidate == ROBO_FONT_STYLE_OBLIQUE) {
        return angle_pen;
    }
    if (candidate == ROBO_FONT_STYLE_ITALIC) {
        return 150u;
    }
    return 1000u;
}

int robo_font_match_score_compare(const robo_font_match_score_t *a,
                                  const robo_font_match_score_t *b)
{
    if (!a || !b) {
        return a ? -1 : (b ? 1 : 0);
    }
    if (a->eligible != b->eligible) {
        return a->eligible ? -1 : 1; /* eligible better */
    }
    if (!a->eligible) {
        return (a->face_id < b->face_id) ? -1 : (a->face_id > b->face_id) ? 1 : 0;
    }
    if (a->family_rank != b->family_rank) {
        return (a->family_rank < b->family_rank) ? -1 : 1;
    }
    if (a->stretch_dist != b->stretch_dist) {
        return (a->stretch_dist < b->stretch_dist) ? -1 : 1;
    }
    if (a->style_dist != b->style_dist) {
        return (a->style_dist < b->style_dist) ? -1 : 1;
    }
    if (a->weight_dist != b->weight_dist) {
        return (a->weight_dist < b->weight_dist) ? -1 : 1;
    }
    /* More exact dimension bits are better (higher exact_bits). */
    if (a->exact_bits != b->exact_bits) {
        return (a->exact_bits > b->exact_bits) ? -1 : 1;
    }
    if (a->wws_flag != b->wws_flag) {
        return (a->wws_flag > b->wws_flag) ? -1 : 1;
    }
    if (a->face_id != b->face_id) {
        return (a->face_id < b->face_id) ? -1 : 1;
    }
    return 0;
}

static size_t req_family_len(const robo_font_select_request_t *req)
{
    if (req->family_len != 0u) {
        return req->family_len;
    }
    return strlen(req->family);
}

static int utf8_casefold_eq(const char *a, size_t al, const char *b, size_t bl)
{
    int cmp = 1;
    if (!a || !b) {
        return 0;
    }
    if (robo_unicode_casefold_compare_utf8(a, al, b, bl, ROBO_UNICODE_REPLACE, &cmp) !=
        ROBO_OK) {
        return 0;
    }
    return cmp == 0;
}

static int field_matches(const robo_font_meta_field_t *f, const char *name, size_t nlen)
{
    if (!f || f->avail != ROBO_FONT_NAME_PRESENT || !f->utf8) {
        return 0;
    }
    return utf8_casefold_eq(f->utf8, f->utf8_len, name, nlen);
}

static int match_name_against_request(const robo_font_meta_field_t *f,
                                      const robo_font_select_request_t *req,
                                      int *out_via_alias)
{
    uint32_t i;
    size_t flen = req_family_len(req);

    *out_via_alias = 0;
    if (field_matches(f, req->family, flen)) {
        return 1;
    }
    for (i = 0u; i < req->alias_count; i++) {
        const char *al = req->aliases[i];
        size_t alen;
        if (!al) {
            continue;
        }
        alen = strlen(al);
        if (alen == 0u || alen > ROBO_FONT_SELECT_MAX_FAMILY_BYTES) {
            continue;
        }
        if (field_matches(f, al, alen)) {
            *out_via_alias = 1;
            return 1;
        }
    }
    return 0;
}

static uint32_t family_kind_rank(robo_font_select_family_kind_t kind, uint32_t flags)
{
    int prefer_wws = (flags & ROBO_FONT_SELECT_FLAG_PREFER_WWS) != 0;

    switch (kind) {
    case ROBO_FONT_SELECT_FAMILY_TYPOGRAPHIC:
        return prefer_wws ? 1u : 0u;
    case ROBO_FONT_SELECT_FAMILY_WWS:
        return prefer_wws ? 0u : 1u;
    case ROBO_FONT_SELECT_FAMILY_LEGACY:
        return 2u;
    case ROBO_FONT_SELECT_FAMILY_FULL:
        return 3u;
    case ROBO_FONT_SELECT_FAMILY_POSTSCRIPT:
        return 4u;
    case ROBO_FONT_SELECT_FAMILY_ALIAS:
        return 5u;
    default:
        return 100u;
    }
}

static void try_family(const robo_font_meta_field_t *f,
                       robo_font_select_family_kind_t kind,
                       const robo_font_select_request_t *req,
                       robo_font_select_family_kind_t *best_kind,
                       uint32_t *best_rank,
                       int *found)
{
    int via_alias = 0;
    uint32_t rank;
    robo_font_select_family_kind_t use_kind;

    if (!match_name_against_request(f, req, &via_alias)) {
        return;
    }
    use_kind = via_alias ? ROBO_FONT_SELECT_FAMILY_ALIAS : kind;
    /* Alias on a stronger field still uses that field's kind for ranking when
     * the primary family string matched via alias — keep field kind unless
     * only alias path: we still prefer typographic/WWS/legacy kinds. */
    if (!via_alias) {
        use_kind = kind;
    } else {
        /* Alias matched this field: rank as the field kind (better than generic alias). */
        use_kind = kind;
    }
    rank = family_kind_rank(use_kind, req->flags);
    if (!*found || rank < *best_rank) {
        *found = 1;
        *best_rank = rank;
        *best_kind = use_kind;
    }
}

static robo_result_t resolve_family(const robo_font_select_request_t *req,
                                    const robo_font_metadata_t *meta,
                                    robo_font_select_family_kind_t *out_kind,
                                    uint32_t *out_rank)
{
    int found = 0;
    robo_font_select_family_kind_t kind = ROBO_FONT_SELECT_FAMILY_NONE;
    uint32_t rank = 100u;

    if (!meta) {
        *out_kind = ROBO_FONT_SELECT_FAMILY_NONE;
        *out_rank = 100u;
        return ROBO_OK;
    }

    try_family(&meta->typographic_family_name, ROBO_FONT_SELECT_FAMILY_TYPOGRAPHIC, req,
               &kind, &rank, &found);
    try_family(&meta->wws_family_name, ROBO_FONT_SELECT_FAMILY_WWS, req, &kind, &rank,
               &found);
    try_family(&meta->family_name, ROBO_FONT_SELECT_FAMILY_LEGACY, req, &kind, &rank,
               &found);
    if ((req->flags & ROBO_FONT_SELECT_FLAG_ALLOW_FULL_NAME) != 0u) {
        try_family(&meta->full_name, ROBO_FONT_SELECT_FAMILY_FULL, req, &kind, &rank,
                   &found);
    }
    if ((req->flags & ROBO_FONT_SELECT_FLAG_ALLOW_POSTSCRIPT) != 0u) {
        try_family(&meta->postscript_name, ROBO_FONT_SELECT_FAMILY_POSTSCRIPT, req, &kind,
                   &rank, &found);
    }

    *out_kind = found ? kind : ROBO_FONT_SELECT_FAMILY_NONE;
    *out_rank = found ? rank : 100u;
    return ROBO_OK;
}

robo_result_t robo_font_select_score_face(const robo_font_select_request_t *req,
                                          robo_font_face_t *face,
                                          robo_font_match_score_t *out_score)
{
    const robo_font_metadata_t *meta;
    const robo_font_classification_t *cls;
    robo_font_select_family_kind_t fkind;
    uint32_t frank;
    robo_result_t res;

    if (!req || !face || !out_score) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out_score, 0, sizeof(*out_score));
    out_score->face_id = robo_font_face_id(face);

    res = robo_font_select_request_validate(req);
    if (res != ROBO_OK) {
        return res;
    }

    res = robo_font_face_parse_metadata(face);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_face_parse_classification(face);
    if (res != ROBO_OK) {
        return res;
    }

    meta = robo_font_face_metadata(face);
    cls = robo_font_face_classification(face);
    if (!cls) {
        return ROBO_PARSE_ERROR;
    }

    (void)resolve_family(req, meta, &fkind, &frank);
    if (fkind == ROBO_FONT_SELECT_FAMILY_NONE) {
        out_score->eligible = 0u;
        return ROBO_OK;
    }

    out_score->eligible = 1u;
    out_score->family_kind = fkind;
    out_score->family_rank = frank;
    out_score->stretch_dist =
        robo_font_select_stretch_dist(req->stretch, cls->stretch);
    out_score->style_dist = robo_font_select_style_dist(
        req->style, cls->style, req->has_oblique_angle, req->oblique_angle_millidegrees,
        cls->has_italic_angle, cls->italic_angle_millidegrees);
    out_score->weight_dist = robo_font_select_weight_dist(req->weight, cls->weight);
    out_score->exact_bits = 0u;
    if (req->stretch == cls->stretch) {
        out_score->exact_bits |= 1u;
    }
    if (req->style == cls->style ||
        (req->style == ROBO_FONT_STYLE_UNKNOWN && cls->style == ROBO_FONT_STYLE_NORMAL)) {
        out_score->exact_bits |= 2u;
    }
    if (req->weight == cls->weight) {
        out_score->exact_bits |= 4u;
    }

    /* Variable-font axes can satisfy weight/stretch/style without another face. */
    {
        const robo_font_variation_model_t *vm = NULL;
        if (robo_font_face_parse_variation(face) == ROBO_OK) {
            vm = robo_font_face_variation(face);
        }
        if (vm) {
            if (robo_font_variation_can_satisfy_weight(vm, req->weight)) {
                out_score->weight_dist = 0u;
                out_score->exact_bits |= 4u;
            }
            if (robo_font_variation_can_satisfy_stretch(vm, req->stretch)) {
                out_score->stretch_dist = 0u;
                out_score->exact_bits |= 1u;
            }
            if (req->style != ROBO_FONT_STYLE_NORMAL &&
                robo_font_variation_can_satisfy_style(vm, req->style)) {
                out_score->style_dist = 0u;
                out_score->exact_bits |= 2u;
            }
            if (req->has_named_instance &&
                req->named_instance_index <
                    robo_font_variation_model_instance_count(vm)) {
                /* Prefer faces that expose the requested named instance. */
                out_score->exact_bits |= 8u;
            }
        }
    }

    out_score->wws_flag = cls->wws_flag;
    return ROBO_OK;
}

robo_result_t robo_font_select(const robo_font_select_request_t *req,
                               robo_font_face_t *const *candidates,
                               uint32_t candidate_count,
                               robo_font_selection_t *out)
{
    uint32_t i;
    robo_font_face_t *best_face = NULL;
    robo_font_match_score_t best;
    uint8_t have_best = 0u;
    robo_result_t res;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Accept zeroed or previously initialized out; never free a garbage face. */
    if (out->cookie == ROBO_FONT_SELECTION_COOKIE) {
        robo_font_selection_clear(out);
    } else {
        robo_font_selection_init(out);
    }

    res = robo_font_select_request_validate(req);
    if (res != ROBO_OK) {
        return res;
    }
    if (candidate_count > ROBO_FONT_SELECT_MAX_CANDIDATES) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (candidate_count != 0u && !candidates) {
        return ROBO_INVALID_ARGUMENT;
    }

    out->requested.weight = req->weight;
    out->requested.stretch = req->stretch;
    out->requested.style =
        (req->style == ROBO_FONT_STYLE_UNKNOWN) ? ROBO_FONT_STYLE_NORMAL : req->style;
    out->requested.axis_count = req->axis_count;
    if (req->axis_count > 0u) {
        memcpy(out->requested.axes, req->axes,
               sizeof(robo_font_axis_setting_t) * req->axis_count);
    }

    memset(&best, 0, sizeof(best));

    for (i = 0u; i < candidate_count; i++) {
        robo_font_face_t *face = candidates[i];
        robo_font_match_score_t score;

        if (!face) {
            continue;
        }
        res = robo_font_select_score_face(req, face, &score);
        if (res != ROBO_OK) {
            /* Skip malformed candidates rather than failing the whole select. */
            continue;
        }
        if (!score.eligible) {
            continue;
        }
        if (!have_best || robo_font_match_score_compare(&score, &best) < 0) {
            best = score;
            best_face = face;
            have_best = 1u;
        }
    }

    if (!have_best || !best_face) {
        out->found = 0u;
        return ROBO_OK;
    }

    {
        const robo_font_classification_t *cls =
            robo_font_face_classification(best_face);
        robo_font_face_retain(best_face);
        out->found = 1u;
        out->face = best_face;
        out->face_id = best.face_id;
        out->score = best;
        out->family_kind = best.family_kind;
        if (cls) {
            out->selected = cls->style_desc;
            /* When axes satisfy the request, surface the requested numerics. */
            if (robo_font_face_variation(best_face)) {
                const robo_font_variation_model_t *vm =
                    robo_font_face_variation(best_face);
                if (robo_font_variation_can_satisfy_weight(vm, req->weight)) {
                    out->selected.weight = req->weight;
                }
                if (robo_font_variation_can_satisfy_stretch(vm, req->stretch)) {
                    out->selected.stretch = req->stretch;
                }
                if (req->style != ROBO_FONT_STYLE_NORMAL &&
                    robo_font_variation_can_satisfy_style(vm, req->style)) {
                    out->selected.style = req->style;
                }
            }
        }
    }
    return ROBO_OK;
}

robo_result_t robo_font_selection_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_select_request_t req;
    robo_font_selection_t sel;
    robo_font_match_score_t score;
    char family[64];
    size_t n;
    size_t i;

    robo_font_select_request_init(&req);
    robo_font_selection_init(&sel);
    memset(&score, 0, sizeof(score));

    if (!data) {
        return ROBO_INVALID_ARGUMENT;
    }
    n = size < sizeof(family) - 1u ? size : sizeof(family) - 1u;
    for (i = 0u; i < n; i++) {
        uint8_t c = data[i];
        if (c < 0x20u || c > 0x7Eu) {
            c = (uint8_t)('A' + (c % 26u));
        }
        family[i] = (char)c;
    }
    family[n] = '\0';
    if (n == 0u) {
        family[0] = 'X';
        family[1] = '\0';
    }
    req.family = family;
    if (size >= 3u) {
        req.weight = (robo_font_weight_t)(1u + (data[0] % 1000u));
        req.stretch = (robo_font_stretch_t)(50u + (data[1] % 151u));
        req.style = (robo_font_style_t)(data[2] % 3u);
    }
    if (size >= 8u) {
        req.has_oblique_angle = 1u;
        req.oblique_angle_millidegrees =
            (int32_t)(((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                      ((uint32_t)data[6] << 8) | data[7]);
    }
    (void)robo_font_select_request_validate(&req);
    (void)robo_font_select(&req, NULL, 0u, &sel);
    robo_font_selection_clear(&sel);

    /* Distance helpers with extreme values. */
    (void)robo_font_select_stretch_dist(50u, 200u);
    (void)robo_font_select_weight_dist(1u, 1000u);
    (void)robo_font_select_style_dist(ROBO_FONT_STYLE_ITALIC, ROBO_FONT_STYLE_OBLIQUE, 1u,
                                      -12000, 1u, 9000);
    (void)robo_font_match_score_compare(&score, &score);
    return ROBO_OK;
}
