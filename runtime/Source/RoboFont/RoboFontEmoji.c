/*
 * RoboFontEmoji — emoji font awareness & selection foundations (Goal 5.17).
 */

#include "RoboFont/RoboFontEmoji.h"

#include "RoboFont/RoboFontName.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeCase.h"

#include <string.h>

#define INFO_COOKIE 0x45494E46u /* 'EINF' */

struct robo_font_emoji_info {
    uint32_t cookie;
    uint32_t refs;
    robo_font_emoji_kind_t kind;
    uint32_t capabilities;
    uint32_t emoji_scalars;
    /* Borrowed, retained: coverage for queries (may be NULL). */
    const robo_font_coverage_t *coverage;
};

static int name_has_emoji_hint(const char *s)
{
    size_t n;
    size_t i;
    if (!s || !s[0]) {
        return 0;
    }
    n = strlen(s);
    /* Case-insensitive ASCII substring "emoji". */
    for (i = 0u; i + 5u <= n; i++) {
        char a0 = s[i];
        char a1 = s[i + 1u];
        char a2 = s[i + 2u];
        char a3 = s[i + 3u];
        char a4 = s[i + 4u];
        if ((a0 == 'e' || a0 == 'E') && (a1 == 'm' || a1 == 'M') &&
            (a2 == 'o' || a2 == 'O') && (a3 == 'j' || a3 == 'J') &&
            (a4 == 'i' || a4 == 'I')) {
            return 1;
        }
    }
    return 0;
}

static int uvs_status_is_mapping(robo_font_cmap_uvs_status_t st)
{
    return st == ROBO_FONT_CMAP_UVS_DEFAULT || st == ROBO_FONT_CMAP_UVS_NON_DEFAULT;
}

void robo_font_emoji_analyze_options_init(robo_font_emoji_analyze_options_t *opt)
{
    if (!opt) {
        return;
    }
    memset(opt, 0, sizeof(*opt));
    opt->mono_emoji_scalar_threshold = 8u;
    opt->incidental_emoji_scalar_threshold = 1u;
}

robo_result_t robo_font_emoji_info_create(
    const robo_font_sfnt_t *sfnt,
    const robo_font_coverage_t *cov,
    const char *family_name,
    const char *full_name,
    const robo_font_emoji_analyze_options_t *opt,
    robo_font_emoji_info_t **out)
{
    robo_font_emoji_analyze_options_t local;
    robo_font_emoji_info_t *info;
    uint32_t caps = 0u;
    uint32_t emoji_scalars = 0u;
    int has_colr = 0;
    int has_cpal = 0;
    int has_sbix = 0;
    int has_cbdt = 0;
    int has_cblc = 0;
    int has_svg = 0;
    int has_glyf = 0;
    int has_cff = 0;
    int name_hint = 0;
    uint32_t mono_th;
    uint32_t inc_th;
    robo_font_emoji_kind_t kind = ROBO_FONT_EMOJI_KIND_NONE;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    if (!opt) {
        robo_font_emoji_analyze_options_init(&local);
        opt = &local;
    }
    mono_th = opt->mono_emoji_scalar_threshold ? opt->mono_emoji_scalar_threshold : 8u;
    inc_th = opt->incidental_emoji_scalar_threshold
                 ? opt->incidental_emoji_scalar_threshold
                 : 1u;

    if (cov) {
        const robo_font_coverage_stats_t *st = robo_font_coverage_stats(cov);
        if (st) {
            emoji_scalars = st->emoji_scalars;
            if (st->emoji_scalars > 0u) {
                caps |= ROBO_FONT_EMOJI_CAP_COVERAGE;
            }
            if (st->has_uvs) {
                caps |= ROBO_FONT_EMOJI_CAP_UVS;
                /* Probe whether FE0E / FE0F selector records exist at all. */
                {
                    robo_font_cmap_uvs_result_t u;
                    /* Use U+0020 as a harmless base; status NONE vs others
                     * distinguishes missing selector vs unsupported base. */
                    if (robo_font_coverage_query_uvs(cov, 0x20u, ROBO_FONT_EMOJI_VS_TEXT,
                                                     &u) == ROBO_OK) {
                        if (u.status != ROBO_FONT_CMAP_UVS_NONE) {
                            caps |= ROBO_FONT_EMOJI_CAP_VS_TEXT;
                        }
                    }
                    if (robo_font_coverage_query_uvs(cov, 0x20u, ROBO_FONT_EMOJI_VS_EMOJI,
                                                     &u) == ROBO_OK) {
                        if (u.status != ROBO_FONT_CMAP_UVS_NONE) {
                            caps |= ROBO_FONT_EMOJI_CAP_VS_EMOJI;
                        }
                    }
                }
            }
        }
        robo_font_coverage_retain((robo_font_coverage_t *)cov);
    }

    if (sfnt) {
        has_colr = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_COLR);
        has_cpal = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CPAL);
        has_sbix = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_SBIX);
        has_cbdt = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CBDT);
        has_cblc = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CBLC);
        has_svg = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_SVG);
        has_glyf = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_GLYF);
        has_cff = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CFF) ||
                  robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CFF2);

        if (has_colr && has_cpal) {
            caps |= ROBO_FONT_EMOJI_CAP_VECTOR_COLOR | ROBO_FONT_EMOJI_CAP_COLOR;
        }
        if (has_sbix || (has_cbdt && has_cblc) || has_cbdt || has_cblc) {
            caps |= ROBO_FONT_EMOJI_CAP_BITMAP | ROBO_FONT_EMOJI_CAP_COLOR;
        }
        if (has_svg) {
            caps |= ROBO_FONT_EMOJI_CAP_SVG | ROBO_FONT_EMOJI_CAP_COLOR;
        }
        if (has_glyf || has_cff) {
            caps |= ROBO_FONT_EMOJI_CAP_OUTLINE;
        }
    }

    name_hint = name_has_emoji_hint(family_name) || name_has_emoji_hint(full_name);
    if (name_hint) {
        caps |= ROBO_FONT_EMOJI_CAP_NAME_HINT;
    }

    if (opt->configured_as_emoji) {
        caps |= ROBO_FONT_EMOJI_CAP_CONFIGURED;
        kind = ROBO_FONT_EMOJI_KIND_CONFIGURED;
        if ((caps & ROBO_FONT_EMOJI_CAP_COLOR) == 0u &&
            (caps & ROBO_FONT_EMOJI_CAP_COVERAGE) != 0u) {
            caps |= ROBO_FONT_EMOJI_CAP_MONOCHROME;
        }
    } else if ((caps & ROBO_FONT_EMOJI_CAP_COLOR) != 0u &&
               ((caps & ROBO_FONT_EMOJI_CAP_COVERAGE) != 0u || name_hint)) {
        /* Color tables + (emoji coverage or name hint). Name alone with color
         * tables is accepted only with coverage OR configured. Prefer coverage. */
        if ((caps & ROBO_FONT_EMOJI_CAP_COVERAGE) != 0u) {
            kind = ROBO_FONT_EMOJI_KIND_COLOR;
        } else {
            /* Color tables but no emoji scalars — not classified as emoji font. */
            kind = ROBO_FONT_EMOJI_KIND_NONE;
            caps &= (uint32_t)~(ROBO_FONT_EMOJI_CAP_NAME_HINT);
        }
    } else if (emoji_scalars >= mono_th) {
        kind = ROBO_FONT_EMOJI_KIND_MONOCHROME;
        caps |= ROBO_FONT_EMOJI_CAP_MONOCHROME;
    } else if (emoji_scalars >= inc_th) {
        kind = ROBO_FONT_EMOJI_KIND_INCIDENTAL;
        /* Name hint alone cannot promote INCIDENTAL → MONOCHROME. */
        (void)name_hint;
    } else {
        kind = ROBO_FONT_EMOJI_KIND_NONE;
        /* Strip name hint from "supports" story when no coverage. */
        if ((caps & ROBO_FONT_EMOJI_CAP_COVERAGE) == 0u) {
            caps &= (uint32_t)~(ROBO_FONT_EMOJI_CAP_NAME_HINT);
        }
    }

    info = (robo_font_emoji_info_t *)robo_calloc(1, sizeof(*info));
    if (!info) {
        if (cov) {
            robo_font_coverage_release((robo_font_coverage_t *)cov);
        }
        return ROBO_OUT_OF_MEMORY;
    }
    info->cookie = INFO_COOKIE;
    info->refs = 1u;
    info->kind = kind;
    info->capabilities = caps;
    info->emoji_scalars = emoji_scalars;
    info->coverage = cov;
    *out = info;
    return ROBO_OK;
}

robo_result_t robo_font_emoji_info_create_from_face(
    robo_font_face_t *face,
    const robo_font_emoji_analyze_options_t *opt,
    robo_font_emoji_info_t **out)
{
    robo_result_t res;
    const robo_font_sfnt_t *sfnt = NULL;
    const robo_font_coverage_t *cov = NULL;
    const char *family = NULL;
    const char *full = NULL;
    const robo_font_metadata_t *meta = NULL;

    if (!face || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    res = robo_font_face_parse_sfnt(face);
    if (res != ROBO_OK && res != ROBO_NOT_SUPPORTED) {
        return res;
    }
    sfnt = robo_font_face_sfnt(face);

    res = robo_font_face_parse_coverage(face);
    if (res == ROBO_OK) {
        cov = robo_font_face_coverage(face);
    }

    if (robo_font_face_parse_metadata(face) == ROBO_OK) {
        meta = robo_font_face_metadata(face);
        if (meta) {
            if (meta->family_name.utf8 && meta->family_name.utf8_len) {
                family = meta->family_name.utf8;
            }
            if (meta->full_name.utf8 && meta->full_name.utf8_len) {
                full = meta->full_name.utf8;
            }
        }
    }

    return robo_font_emoji_info_create(sfnt, cov, family, full, opt, out);
}

void robo_font_emoji_info_retain(robo_font_emoji_info_t *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return;
    }
    info->refs++;
}

void robo_font_emoji_info_release(robo_font_emoji_info_t *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return;
    }
    if (info->refs == 0u) {
        return;
    }
    info->refs--;
    if (info->refs != 0u) {
        return;
    }
    if (info->coverage) {
        robo_font_coverage_release((robo_font_coverage_t *)info->coverage);
    }
    info->cookie = 0u;
    robo_free(info);
}

robo_font_emoji_kind_t robo_font_emoji_info_kind(const robo_font_emoji_info_t *info)
{
    return info && info->cookie == INFO_COOKIE ? info->kind : ROBO_FONT_EMOJI_KIND_NONE;
}

uint32_t robo_font_emoji_info_capabilities(const robo_font_emoji_info_t *info)
{
    return info && info->cookie == INFO_COOKIE ? info->capabilities : 0u;
}

uint32_t robo_font_emoji_info_emoji_scalars(const robo_font_emoji_info_t *info)
{
    return info && info->cookie == INFO_COOKIE ? info->emoji_scalars : 0u;
}

int robo_font_emoji_info_supports_emoji(const robo_font_emoji_info_t *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return 0;
    }
    return info->kind == ROBO_FONT_EMOJI_KIND_MONOCHROME ||
           info->kind == ROBO_FONT_EMOJI_KIND_COLOR ||
           info->kind == ROBO_FONT_EMOJI_KIND_CONFIGURED ||
           (info->kind == ROBO_FONT_EMOJI_KIND_INCIDENTAL &&
            (info->capabilities & ROBO_FONT_EMOJI_CAP_COVERAGE) != 0u);
}

int robo_font_emoji_info_supports_color_emoji(const robo_font_emoji_info_t *info)
{
    return info && info->cookie == INFO_COOKIE &&
           (info->capabilities & ROBO_FONT_EMOJI_CAP_COLOR) != 0u &&
           robo_font_emoji_info_supports_emoji(info);
}

int robo_font_emoji_info_supports_monochrome_emoji(const robo_font_emoji_info_t *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return 0;
    }
    if (!robo_font_emoji_info_supports_emoji(info)) {
        return 0;
    }
    /* Outline/mono emoji: coverage without requiring color tables. */
    return (info->capabilities & ROBO_FONT_EMOJI_CAP_COVERAGE) != 0u &&
           ((info->capabilities & ROBO_FONT_EMOJI_CAP_COLOR) == 0u ||
            (info->capabilities & ROBO_FONT_EMOJI_CAP_MONOCHROME) != 0u ||
            (info->capabilities & ROBO_FONT_EMOJI_CAP_OUTLINE) != 0u);
}

int robo_font_emoji_info_supports_variation_selectors(
    const robo_font_emoji_info_t *info)
{
    return info && info->cookie == INFO_COOKIE &&
           (info->capabilities & ROBO_FONT_EMOJI_CAP_UVS) != 0u;
}

int robo_font_emoji_info_is_classified_emoji(const robo_font_emoji_info_t *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return 0;
    }
    return info->kind == ROBO_FONT_EMOJI_KIND_MONOCHROME ||
           info->kind == ROBO_FONT_EMOJI_KIND_COLOR ||
           info->kind == ROBO_FONT_EMOJI_KIND_CONFIGURED;
}

const char *robo_font_emoji_kind_name(robo_font_emoji_kind_t kind)
{
    switch (kind) {
    case ROBO_FONT_EMOJI_KIND_NONE:
        return "none";
    case ROBO_FONT_EMOJI_KIND_INCIDENTAL:
        return "incidental";
    case ROBO_FONT_EMOJI_KIND_MONOCHROME:
        return "monochrome";
    case ROBO_FONT_EMOJI_KIND_COLOR:
        return "color";
    case ROBO_FONT_EMOJI_KIND_CONFIGURED:
        return "configured";
    default:
        return "unknown";
    }
}

int robo_font_emoji_covers_codepoint(const robo_font_emoji_info_t *info,
                                     uint32_t code_point)
{
    if (!info || info->cookie != INFO_COOKIE || !info->coverage) {
        return 0;
    }
    return robo_font_coverage_contains(info->coverage, code_point);
}

robo_result_t robo_font_emoji_query_uvs(const robo_font_emoji_info_t *info,
                                        uint32_t base_code_point,
                                        uint32_t variation_selector,
                                        robo_font_cmap_uvs_result_t *out)
{
    if (!info || info->cookie != INFO_COOKIE || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!info->coverage) {
        return ROBO_NOT_SUPPORTED;
    }
    return robo_font_coverage_query_uvs(info->coverage, base_code_point,
                                        variation_selector, out);
}

robo_result_t robo_font_emoji_probe_presentation(const robo_font_emoji_info_t *info,
                                                 uint32_t base_code_point,
                                                 robo_font_emoji_presentation_probe_t *out)
{
    robo_font_cmap_uvs_result_t u;
    if (!info || info->cookie != INFO_COOKIE || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->base_covered = robo_font_emoji_covers_codepoint(info, base_code_point);
    out->vs_text_status = ROBO_FONT_CMAP_UVS_NONE;
    out->vs_emoji_status = ROBO_FONT_CMAP_UVS_NONE;
    if (!info->coverage) {
        return ROBO_OK;
    }
    if (robo_font_coverage_query_uvs(info->coverage, base_code_point,
                                     ROBO_FONT_EMOJI_VS_TEXT, &u) == ROBO_OK) {
        out->vs_text_status = u.status;
        out->has_vs_text_mapping = uvs_status_is_mapping(u.status);
    }
    if (robo_font_coverage_query_uvs(info->coverage, base_code_point,
                                     ROBO_FONT_EMOJI_VS_EMOJI, &u) == ROBO_OK) {
        out->vs_emoji_status = u.status;
        out->has_vs_emoji_mapping = uvs_status_is_mapping(u.status);
    }
    return ROBO_OK;
}

void robo_font_emoji_sequence_init(robo_font_emoji_sequence_t *seq)
{
    if (!seq) {
        return;
    }
    memset(seq, 0, sizeof(*seq));
    seq->rgi_kind = ROBO_EMOJI_SEQ_NONE;
}

robo_result_t robo_font_emoji_sequence_set(robo_font_emoji_sequence_t *seq,
                                           const uint32_t *codepoints,
                                           uint32_t count)
{
    if (!seq) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (count > ROBO_FONT_EMOJI_MAX_SEQUENCE) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (count != 0u && !codepoints) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_emoji_sequence_init(seq);
    seq->codepoints = codepoints;
    seq->count = count;
    return ROBO_OK;
}

robo_result_t robo_font_emoji_sequence_analyze(robo_font_emoji_sequence_t *seq)
{
    uint32_t i;
    if (!seq) {
        return ROBO_INVALID_ARGUMENT;
    }
    seq->has_vs_text = 0;
    seq->has_vs_emoji = 0;
    seq->has_zwj = 0;
    seq->has_modifier = 0;
    seq->has_regional_indicator = 0;
    seq->has_keycap = 0;
    seq->rgi_kind = ROBO_EMOJI_SEQ_NONE;

    if (!seq->codepoints || seq->count == 0u) {
        return ROBO_OK;
    }

    for (i = 0u; i < seq->count; i++) {
        uint32_t cp = seq->codepoints[i];
        if (cp == ROBO_FONT_EMOJI_VS_TEXT) {
            seq->has_vs_text = 1;
        } else if (cp == ROBO_FONT_EMOJI_VS_EMOJI) {
            seq->has_vs_emoji = 1;
        } else if (cp == ROBO_FONT_EMOJI_ZWJ) {
            seq->has_zwj = 1;
        } else if (cp == ROBO_FONT_EMOJI_KEYCAP) {
            seq->has_keycap = 1;
        } else if (robo_unicode_is_emoji_modifier(cp)) {
            seq->has_modifier = 1;
        } else if (robo_unicode_is_regional_indicator(cp)) {
            seq->has_regional_indicator = 1;
        }
    }

    seq->rgi_kind = robo_unicode_emoji_sequence_classify(
        (const robo_uchar32_t *)seq->codepoints, (size_t)seq->count);
    return ROBO_OK;
}

robo_font_emoji_sequence_support_t robo_font_emoji_info_supports_sequence(
    const robo_font_emoji_info_t *info,
    const robo_font_emoji_sequence_t *seq)
{
    uint32_t i;
    int all = 1;
    int need_vs = 0;
    int vs_ok = 1;

    if (!info || info->cookie != INFO_COOKIE || !seq || !seq->codepoints ||
        seq->count == 0u) {
        return ROBO_FONT_EMOJI_SEQ_SUPPORT_NONE;
    }

    /* Multi-codepoint RGI kinds that require shaping → UNKNOWN even if
     * components exist. */
    if (seq->rgi_kind == ROBO_EMOJI_SEQ_ZWJ || seq->rgi_kind == ROBO_EMOJI_SEQ_FLAG ||
        seq->rgi_kind == ROBO_EMOJI_SEQ_KEYCAP || seq->rgi_kind == ROBO_EMOJI_SEQ_TAG ||
        (seq->count > 1u && seq->has_zwj)) {
        /* Still verify components for callers that want a soft signal. */
        for (i = 0u; i < seq->count; i++) {
            uint32_t cp = seq->codepoints[i];
            if (cp == ROBO_FONT_EMOJI_ZWJ || cp == ROBO_FONT_EMOJI_VS_TEXT ||
                cp == ROBO_FONT_EMOJI_VS_EMOJI) {
                continue;
            }
            if (!robo_font_emoji_covers_codepoint(info, cp)) {
                return ROBO_FONT_EMOJI_SEQ_SUPPORT_NONE;
            }
        }
        return ROBO_FONT_EMOJI_SEQ_SUPPORT_UNKNOWN;
    }

    for (i = 0u; i < seq->count; i++) {
        uint32_t cp = seq->codepoints[i];
        if (cp == ROBO_FONT_EMOJI_VS_TEXT || cp == ROBO_FONT_EMOJI_VS_EMOJI) {
            need_vs = 1;
            continue;
        }
        if (cp == ROBO_FONT_EMOJI_ZWJ) {
            continue;
        }
        if (!robo_font_emoji_covers_codepoint(info, cp)) {
            all = 0;
            break;
        }
    }
    if (!all) {
        return ROBO_FONT_EMOJI_SEQ_SUPPORT_NONE;
    }

    if (need_vs && info->coverage) {
        /* For base+VS pairs, check the first base against each VS in sequence. */
        uint32_t base = 0u;
        int have_base = 0;
        for (i = 0u; i < seq->count; i++) {
            uint32_t cp = seq->codepoints[i];
            if (cp == ROBO_FONT_EMOJI_VS_TEXT || cp == ROBO_FONT_EMOJI_VS_EMOJI) {
                if (have_base) {
                    robo_font_cmap_uvs_result_t u;
                    if (robo_font_coverage_query_uvs(info->coverage, base, cp, &u) !=
                            ROBO_OK ||
                        !uvs_status_is_mapping(u.status)) {
                        vs_ok = 0;
                    }
                }
            } else if (cp != ROBO_FONT_EMOJI_ZWJ) {
                base = cp;
                have_base = 1;
            }
        }
        if (vs_ok) {
            return ROBO_FONT_EMOJI_SEQ_SUPPORT_COMPONENTS_AND_VS;
        }
        return ROBO_FONT_EMOJI_SEQ_SUPPORT_COMPONENTS;
    }

    return ROBO_FONT_EMOJI_SEQ_SUPPORT_COMPONENTS;
}

int robo_font_emoji_content_is_emoji_related(const uint32_t *codepoints,
                                             uint32_t count)
{
    uint32_t i;
    if (!codepoints || count == 0u) {
        return 0;
    }
    for (i = 0u; i < count; i++) {
        uint32_t cp = codepoints[i];
        if (robo_unicode_is_emoji(cp) || robo_unicode_is_emoji_presentation(cp) ||
            robo_unicode_is_extended_pictographic(cp) ||
            robo_unicode_is_emoji_modifier(cp) ||
            robo_unicode_is_emoji_component(cp) ||
            robo_unicode_is_regional_indicator(cp) || cp == ROBO_FONT_EMOJI_ZWJ ||
            cp == ROBO_FONT_EMOJI_VS_TEXT || cp == ROBO_FONT_EMOJI_VS_EMOJI ||
            cp == ROBO_FONT_EMOJI_KEYCAP) {
            return 1;
        }
    }
    return 0;
}

/* ---- Selection helpers ---- */

void robo_font_emoji_select_request_init(robo_font_emoji_select_request_t *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
    req->color_preference = ROBO_FONT_EMOJI_COLOR_ANY;
    req->weight = 400;
    req->stretch = ROBO_FONT_STRETCH_NORMAL;
    req->style = ROBO_FONT_STYLE_NORMAL;
    req->use_generic_emoji = 1;
}

void robo_font_emoji_selection_init(robo_font_emoji_selection_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->cookie = ROBO_FONT_EMOJI_SELECTION_COOKIE;
}

void robo_font_emoji_selection_clear(robo_font_emoji_selection_t *out)
{
    if (!out) {
        return;
    }
    if (out->cookie == ROBO_FONT_EMOJI_SELECTION_COOKIE && out->face) {
        robo_font_face_release(out->face);
    }
    memset(out, 0, sizeof(*out));
    out->cookie = ROBO_FONT_EMOJI_SELECTION_COOKIE;
}

static int family_list_contains(const char *const *list, uint32_t n, const char *fam)
{
    uint32_t i;
    int cmp = 0;
    if (!fam || !list) {
        return 0;
    }
    for (i = 0u; i < n; i++) {
        if (!list[i]) {
            continue;
        }
        if (robo_unicode_casefold_compare_utf8(list[i], 0u, fam, 0u, ROBO_UNICODE_REPLACE,
                                               &cmp) == ROBO_OK &&
            cmp == 0) {
            return 1;
        }
    }
    return 0;
}

static int color_pref_ok(robo_font_emoji_color_preference_t pref,
                         const robo_font_emoji_info_t *info)
{
    int is_color = robo_font_emoji_info_supports_color_emoji(info);
    int is_mono = robo_font_emoji_info_supports_monochrome_emoji(info);
    switch (pref) {
    case ROBO_FONT_EMOJI_COLOR_ANY:
        return 1;
    case ROBO_FONT_EMOJI_COLOR_PREFER_COLOR:
    case ROBO_FONT_EMOJI_COLOR_PREFER_MONO:
        return 1; /* preference only */
    case ROBO_FONT_EMOJI_COLOR_REQUIRE_COLOR:
        return is_color;
    case ROBO_FONT_EMOJI_COLOR_REQUIRE_MONO:
        return is_mono && !is_color;
    default:
        return 1;
    }
}

static uint32_t score_candidate(const robo_font_emoji_info_t *info,
                                const robo_font_emoji_select_request_t *req,
                                uint32_t fam_prio,
                                int configured_family,
                                int in_color_list,
                                int in_mono_list,
                                robo_font_face_id_t face_id)
{
    uint32_t score = 0u;
    uint32_t i;
    int covered = 1;
    robo_font_emoji_kind_t kind = robo_font_emoji_info_kind(info);

    if (configured_family || kind == ROBO_FONT_EMOJI_KIND_CONFIGURED) {
        score += 1000000u;
    }
    if (kind == ROBO_FONT_EMOJI_KIND_COLOR) {
        score += 500000u;
    } else if (kind == ROBO_FONT_EMOJI_KIND_MONOCHROME) {
        score += 400000u;
    } else if (kind == ROBO_FONT_EMOJI_KIND_INCIDENTAL) {
        score += 50000u;
    }

    if (req->codepoints && req->codepoint_count > 0u) {
        for (i = 0u; i < req->codepoint_count; i++) {
            uint32_t cp = req->codepoints[i];
            if (cp == ROBO_FONT_EMOJI_ZWJ || cp == ROBO_FONT_EMOJI_VS_TEXT ||
                cp == ROBO_FONT_EMOJI_VS_EMOJI) {
                continue;
            }
            if (robo_font_emoji_covers_codepoint(info, cp)) {
                score += 1000u;
            } else {
                covered = 0;
            }
        }
        if (!covered && req->codepoint_count > 0u) {
            /* Missing required coverage — heavily penalize but keep for ranking. */
            score = score / 10u;
        } else if (covered) {
            score += 200000u;
        }
    }

    if (robo_font_emoji_info_supports_variation_selectors(info)) {
        score += 5000u;
    }

    if (req->color_preference == ROBO_FONT_EMOJI_COLOR_PREFER_COLOR ||
        req->color_preference == ROBO_FONT_EMOJI_COLOR_REQUIRE_COLOR) {
        if (robo_font_emoji_info_supports_color_emoji(info)) {
            score += 80000u;
        }
        if (in_color_list) {
            score += 40000u;
        }
    } else if (req->color_preference == ROBO_FONT_EMOJI_COLOR_PREFER_MONO ||
               req->color_preference == ROBO_FONT_EMOJI_COLOR_REQUIRE_MONO) {
        if (robo_font_emoji_info_supports_monochrome_emoji(info) &&
            !robo_font_emoji_info_supports_color_emoji(info)) {
            score += 80000u;
        }
        if (in_mono_list) {
            score += 40000u;
        }
    }

    if (fam_prio != 0xFFFFFFFFu) {
        score += 30000u - (fam_prio < 30000u ? fam_prio : 29999u);
    }

    /* Deterministic tie-break: lower face_id slightly preferred. */
    if (face_id < 10000u) {
        score += (uint32_t)(10000u - (uint32_t)face_id);
    }
    return score;
}

typedef struct {
    char names[ROBO_FONT_EMOJI_MAX_CANDIDATES][128];
    uint32_t count;
} emoji_family_list_t;

static void family_list_add(emoji_family_list_t *dst, const char *name)
{
    uint32_t i;
    size_t n;
    if (!dst || !name || !name[0] || dst->count >= ROBO_FONT_EMOJI_MAX_CANDIDATES) {
        return;
    }
    n = strlen(name);
    if (n >= 128u) {
        n = 127u;
    }
    for (i = 0u; i < dst->count; i++) {
        int cmp = 0;
        if (robo_unicode_casefold_compare_utf8(dst->names[i], 0u, name, 0u,
                                               ROBO_UNICODE_REPLACE, &cmp) == ROBO_OK &&
            cmp == 0) {
            return;
        }
    }
    memcpy(dst->names[dst->count], name, n);
    dst->names[dst->count][n] = '\0';
    dst->count++;
}

static void family_list_add_many(emoji_family_list_t *dst, const char *const *src,
                                 uint32_t n)
{
    uint32_t i;
    if (!src) {
        return;
    }
    for (i = 0u; i < n; i++) {
        family_list_add(dst, src[i]);
    }
}

robo_result_t robo_font_emoji_select(
    robo_font_registry_t *registry,
    robo_font_generic_resolver_t *generics,
    const robo_font_emoji_select_request_t *req,
    robo_font_emoji_selection_t *out)
{
    emoji_family_list_t families;
    robo_font_generic_result_t gres;
    uint32_t reg_n;
    uint32_t i;
    uint32_t best_score = 0u;
    int have_best = 0;
    const robo_font_registration_t *best_rec = NULL;
    robo_font_emoji_info_t *best_info = NULL;
    robo_font_emoji_analyze_options_t aopt;

    if (!registry || !req || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (req->codepoint_count > ROBO_FONT_EMOJI_MAX_SEQUENCE) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (req->codepoint_count != 0u && !req->codepoints) {
        return ROBO_INVALID_ARGUMENT;
    }

    if (out->cookie == ROBO_FONT_EMOJI_SELECTION_COOKIE) {
        robo_font_emoji_selection_clear(out);
    } else {
        robo_font_emoji_selection_init(out);
    }

    memset(&families, 0, sizeof(families));
    family_list_add_many(&families, req->preferred_families, req->preferred_family_count);
    family_list_add_many(&families, req->color_families, req->color_family_count);
    family_list_add_many(&families, req->mono_families, req->mono_family_count);

    if (req->use_generic_emoji && generics) {
        robo_font_generic_request_t greq;
        robo_font_generic_request_init(&greq);
        greq.generic = ROBO_FONT_GENERIC_EMOJI;
        robo_font_generic_result_init(&gres);
        if (robo_font_generic_resolve(generics, &greq, &gres) == ROBO_OK &&
            gres.resolved) {
            for (i = 0u; i < gres.family_count; i++) {
                family_list_add(&families, gres.families[i]);
            }
        }
        robo_font_generic_result_clear(&gres);
    }

    reg_n = robo_font_registry_registration_count(registry);
    for (i = 0u; i < reg_n; i++) {
        const robo_font_registration_t *rec = NULL;
        robo_font_emoji_info_t *info = NULL;
        int configured = 0;
        uint32_t fam_prio;
        uint32_t score;
        int in_color;
        int in_mono;

        if (robo_font_registry_registration_at(registry, i, &rec) != ROBO_OK || !rec ||
            !rec->face) {
            continue;
        }

        configured = 0;
        {
            uint32_t fi;
            for (fi = 0u; fi < families.count; fi++) {
                int cmp = 0;
                if (robo_unicode_casefold_compare_utf8(families.names[fi], 0u, rec->family,
                                                       0u, ROBO_UNICODE_REPLACE,
                                                       &cmp) == ROBO_OK &&
                    cmp == 0) {
                    configured = 1;
                    break;
                }
            }
        }

        robo_font_emoji_analyze_options_init(&aopt);
        aopt.configured_as_emoji = configured;

        if (robo_font_emoji_info_create_from_face(rec->face, &aopt, &info) != ROBO_OK ||
            !info) {
            continue;
        }

        if (!robo_font_emoji_info_supports_emoji(info) && !configured) {
            robo_font_emoji_info_release(info);
            continue;
        }
        if (!color_pref_ok(req->color_preference, info)) {
            robo_font_emoji_info_release(info);
            continue;
        }

        fam_prio = 0xFFFFFFFFu;
        {
            uint32_t fi;
            for (fi = 0u; fi < families.count; fi++) {
                int cmp = 0;
                if (robo_unicode_casefold_compare_utf8(families.names[fi], 0u, rec->family,
                                                       0u, ROBO_UNICODE_REPLACE,
                                                       &cmp) == ROBO_OK &&
                    cmp == 0) {
                    fam_prio = fi;
                    break;
                }
            }
        }
        in_color =
            family_list_contains(req->color_families, req->color_family_count, rec->family);
        in_mono =
            family_list_contains(req->mono_families, req->mono_family_count, rec->family);

        score = score_candidate(info, req, fam_prio, configured, in_color, in_mono,
                                rec->face_id);

        /* If preferred families were configured, skip faces outside the list
         * unless no preferred list exists. */
        if (families.count > 0u && fam_prio == 0xFFFFFFFFu) {
            /* Still allow classified emoji fonts as soft fallback candidates
             * with lower score — only if request has codepoints to cover. */
            if (!req->codepoints || req->codepoint_count == 0u) {
                robo_font_emoji_info_release(info);
                continue;
            }
            if (!robo_font_emoji_info_is_classified_emoji(info)) {
                robo_font_emoji_info_release(info);
                continue;
            }
        }

        if (!have_best || score > best_score ||
            (score == best_score && best_rec && rec->face_id < best_rec->face_id)) {
            if (best_info) {
                robo_font_emoji_info_release(best_info);
            }
            best_info = info;
            best_rec = rec;
            best_score = score;
            have_best = 1;
            info = NULL; /* ownership transferred to best_info */
        }
        if (info) {
            robo_font_emoji_info_release(info);
        }
    }

    if (!have_best || !best_rec || !best_rec->face) {
        if (best_info) {
            robo_font_emoji_info_release(best_info);
        }
        out->found = 0;
        return ROBO_OK;
    }

    robo_font_face_retain(best_rec->face);
    out->found = 1;
    out->face = best_rec->face;
    out->face_id = best_rec->face_id;
    out->kind = robo_font_emoji_info_kind(best_info);
    out->capabilities = robo_font_emoji_info_capabilities(best_info);
    out->score = best_score;
    {
        size_t n = strlen(best_rec->family);
        if (n >= sizeof(out->family)) {
            n = sizeof(out->family) - 1u;
        }
        memcpy(out->family, best_rec->family, n);
        out->family[n] = '\0';
    }
    robo_font_emoji_info_release(best_info);
    return ROBO_OK;
}

robo_result_t robo_font_emoji_resolve_fallback(
    robo_font_registry_t *registry,
    robo_font_generic_resolver_t *generics,
    robo_font_fallback_resolver_t *fallback,
    robo_font_face_t *primary,
    const robo_font_emoji_select_request_t *req,
    robo_font_emoji_selection_t *emoji_out,
    robo_font_fallback_result_t *fallback_out)
{
    robo_font_emoji_selection_t local_sel;
    robo_font_emoji_selection_t *sel = emoji_out;
    robo_font_fallback_request_t freq;
    robo_font_generic_result_t gres;
    const char *preferred[ROBO_FONT_FALLBACK_MAX_PREFERRED];
    uint32_t pref_n = 0u;
    robo_result_t res;
    int own_sel = 0;

    if (!registry || !fallback || !primary || !req || !fallback_out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_font_emoji_content_is_emoji_related(req->codepoints, req->codepoint_count)) {
        /* Not emoji-related — let normal fallback handle without emoji policy. */
        robo_font_fallback_request_init(&freq);
        freq.primary = primary;
        freq.codepoints = req->codepoints;
        freq.codepoint_count = req->codepoint_count;
        freq.weight = req->weight;
        freq.stretch = req->stretch;
        freq.style = req->style;
        return robo_font_fallback_resolve(fallback, &freq, fallback_out);
    }

    if (!sel) {
        robo_font_emoji_selection_init(&local_sel);
        sel = &local_sel;
        own_sel = 1;
    }

    res = robo_font_emoji_select(registry, generics, req, sel);
    if (res != ROBO_OK) {
        if (own_sel) {
            robo_font_emoji_selection_clear(&local_sel);
        }
        return res;
    }

    /* Build preferred family list for fallback. */
    if (req->preferred_families) {
        uint32_t i;
        for (i = 0u; i < req->preferred_family_count &&
                     pref_n < ROBO_FONT_FALLBACK_MAX_PREFERRED;
             i++) {
            preferred[pref_n++] = req->preferred_families[i];
        }
    }
    if (generics && req->use_generic_emoji) {
        robo_font_generic_request_t greq;
        robo_font_generic_request_init(&greq);
        greq.generic = ROBO_FONT_GENERIC_EMOJI;
        robo_font_generic_result_init(&gres);
        if (robo_font_generic_resolve(generics, &greq, &gres) == ROBO_OK && gres.resolved) {
            uint32_t i;
            for (i = 0u; i < gres.family_count && pref_n < ROBO_FONT_FALLBACK_MAX_PREFERRED;
                 i++) {
                preferred[pref_n++] = gres.families[i];
            }
        }
        /* Keep gres alive until fallback returns — families borrowed. */
        robo_font_fallback_request_init(&freq);
        freq.primary = primary;
        freq.codepoints = req->codepoints;
        freq.codepoint_count = req->codepoint_count;
        freq.weight = req->weight;
        freq.stretch = req->stretch;
        freq.style = req->style;
        freq.preferred_families = preferred;
        freq.preferred_family_count = pref_n;
        res = robo_font_fallback_resolve(fallback, &freq, fallback_out);
        robo_font_generic_result_clear(&gres);
    } else {
        robo_font_fallback_request_init(&freq);
        freq.primary = primary;
        freq.codepoints = req->codepoints;
        freq.codepoint_count = req->codepoint_count;
        freq.weight = req->weight;
        freq.stretch = req->stretch;
        freq.style = req->style;
        freq.preferred_families = preferred;
        freq.preferred_family_count = pref_n;
        res = robo_font_fallback_resolve(fallback, &freq, fallback_out);
    }

    if (own_sel) {
        robo_font_emoji_selection_clear(&local_sel);
    }
    return res;
}

robo_result_t robo_font_emoji_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_emoji_sequence_t seq;
    uint32_t cps[ROBO_FONT_EMOJI_MAX_SEQUENCE];
    uint32_t n = 0u;
    size_t i;
    robo_font_emoji_info_t *info = NULL;
    robo_font_emoji_analyze_options_t opt;
    robo_font_emoji_presentation_probe_t probe;
    robo_font_emoji_select_request_t req;

    if (!data || size == 0u) {
        return ROBO_OK;
    }

    /* Build a bounded codepoint sequence from input bytes. */
    for (i = 0u; i + 3u < size && n < ROBO_FONT_EMOJI_MAX_SEQUENCE; i += 3u) {
        uint32_t cp = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1u] << 8) |
                      (uint32_t)data[i + 2u];
        cp &= 0x1FFFFFu;
        if (cp > 0x10FFFFu) {
            cp = 0xFFFD;
        }
        cps[n++] = cp;
    }

    (void)robo_font_emoji_sequence_set(&seq, cps, n);
    (void)robo_font_emoji_sequence_analyze(&seq);
    (void)robo_font_emoji_content_is_emoji_related(cps, n);
    (void)robo_font_emoji_kind_name((robo_font_emoji_kind_t)(data[0] % 5u));

    robo_font_emoji_analyze_options_init(&opt);
    opt.configured_as_emoji = (data[0] & 1u) != 0u;
    (void)robo_font_emoji_info_create(NULL, NULL, "FuzzEmoji", NULL, &opt, &info);
    if (info) {
        (void)robo_font_emoji_info_supports_emoji(info);
        (void)robo_font_emoji_info_supports_color_emoji(info);
        (void)robo_font_emoji_info_supports_monochrome_emoji(info);
        (void)robo_font_emoji_info_supports_sequence(info, &seq);
        (void)robo_font_emoji_probe_presentation(info, n ? cps[0] : 0x1F600u, &probe);
        robo_font_emoji_info_release(info);
    }

    /* Pathological large sequence count. */
    (void)robo_font_emoji_sequence_set(&seq, cps, ROBO_FONT_EMOJI_MAX_SEQUENCE + 1u);

    robo_font_emoji_select_request_init(&req);
    req.codepoints = cps;
    req.codepoint_count = n > ROBO_FONT_EMOJI_MAX_SEQUENCE ? 0u : n;
    req.color_preference =
        (robo_font_emoji_color_preference_t)(data[0] % 5u);
    (void)req;

    (void)robo_font_ot_fuzz_parse(data, size);
    return ROBO_OK;
}
