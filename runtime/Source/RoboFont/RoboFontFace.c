#include "RoboFont/RoboFontFace.h"

#include "RoboFont/RoboFontCff.h"
#include "RoboFont/RoboFontClassification.h"
#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontCoverage.h"
#include "RoboFont/RoboFontEmoji.h"
#include "RoboFont/RoboFontName.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboFont/RoboFontVariation.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_font_face {
    uint32_t refs;
    robo_font_face_id_t id;
    robo_font_source_t *source;
    uint32_t face_index;
    robo_font_format_t format;
    robo_font_style_desc_t style;
    robo_font_sfnt_t *sfnt;
    robo_font_tt_t *tt;
    robo_font_cmap_map_t *cmap;
    robo_font_coverage_t *coverage;
    int coverage_building; /* concurrent lazy-init: ROBO_BUSY while set */
    robo_font_emoji_info_t *emoji;
    int emoji_building;
    robo_font_variation_model_t *variation;
    int variation_building;
    int variation_absent; /* 1 if parse found no fvar */
    robo_font_metrics_info_t *metrics;
    robo_font_cff_font_t *cff;
    robo_font_outline_cache_t *outline_cache;
    robo_font_name_info_t *name_info;
    robo_font_classification_info_t *classification;
};

static uint64_t g_next_face_id = 1u;

static robo_font_format_t detect_format(const uint8_t *b, size_t n)
{
    if (!b || n < 4u) {
        return ROBO_FONT_FORMAT_UNKNOWN;
    }
    if ((b[0] == 0x00u && b[1] == 0x01u && b[2] == 0x00u && b[3] == 0x00u) ||
        (b[0] == 't' && b[1] == 'r' && b[2] == 'u' && b[3] == 'e') ||
        (b[0] == 'O' && b[1] == 'T' && b[2] == 'T' && b[3] == 'O') ||
        (b[0] == 't' && b[1] == 't' && b[2] == 'c' && b[3] == 'f')) {
        return ROBO_FONT_FORMAT_SFNT;
    }
    if (n >= 4u && b[0] == 'w' && b[1] == 'O' && b[2] == 'F' && b[3] == 'F') {
        return ROBO_FONT_FORMAT_WOFF;
    }
    if (n >= 4u && b[0] == 'w' && b[1] == 'O' && b[2] == 'F' && b[3] == '2') {
        return ROBO_FONT_FORMAT_WOFF2;
    }
    return ROBO_FONT_FORMAT_UNKNOWN;
}

robo_result_t robo_font_face_create(robo_font_source_t *source,
                                    uint32_t face_index,
                                    robo_font_face_t **out)
{
    robo_font_face_t *f;
    const uint8_t *bytes;
    size_t size;

    if (!out || !source) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    bytes = robo_font_source_bytes(source);
    size = robo_font_source_size(source);
    if (!bytes || size == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }

    f = (robo_font_face_t *)robo_calloc(1, sizeof(*f));
    if (!f) {
        return ROBO_OUT_OF_MEMORY;
    }
    f->refs = 1u;
    f->id = g_next_face_id++;
    if (f->id == 0u) {
        f->id = g_next_face_id++;
    }
    robo_font_source_retain(source);
    f->source = source;
    f->face_index = face_index;
    f->format = detect_format(bytes, size);
    robo_font_style_desc_init(&f->style);
    *out = f;
    return ROBO_OK;
}

void robo_font_face_retain(robo_font_face_t *face)
{
    if (!face) {
        return;
    }
    face->refs++;
}

void robo_font_face_release(robo_font_face_t *face)
{
    if (!face) {
        return;
    }
    if (face->refs == 0u) {
        return;
    }
    face->refs--;
    if (face->refs != 0u) {
        return;
    }
    if (face->metrics) {
        robo_font_metrics_info_release(face->metrics);
        face->metrics = NULL;
    }
    if (face->classification) {
        robo_font_classification_info_release(face->classification);
        face->classification = NULL;
    }
    if (face->name_info) {
        robo_font_name_info_release(face->name_info);
        face->name_info = NULL;
    }
    if (face->outline_cache) {
        robo_font_outline_cache_release(face->outline_cache);
        face->outline_cache = NULL;
    }
    if (face->cff) {
        robo_font_cff_release(face->cff);
        face->cff = NULL;
    }
    if (face->coverage) {
        robo_font_coverage_release(face->coverage);
        face->coverage = NULL;
    }
    if (face->emoji) {
        robo_font_emoji_info_release(face->emoji);
        face->emoji = NULL;
    }
    if (face->variation) {
        robo_font_variation_model_release(face->variation);
        face->variation = NULL;
    }
    if (face->cmap) {
        robo_font_cmap_map_release(face->cmap);
        face->cmap = NULL;
    }
    if (face->tt) {
        robo_font_tt_release(face->tt);
        face->tt = NULL;
    }
    if (face->sfnt) {
        robo_font_sfnt_release(face->sfnt);
        face->sfnt = NULL;
    }
    robo_font_source_release(face->source);
    face->source = NULL;
    robo_free(face);
}

robo_font_face_id_t robo_font_face_id(const robo_font_face_t *face)
{
    return face ? face->id : 0u;
}

robo_font_format_t robo_font_face_format(const robo_font_face_t *face)
{
    return face ? face->format : ROBO_FONT_FORMAT_UNKNOWN;
}

uint32_t robo_font_face_index(const robo_font_face_t *face)
{
    return face ? face->face_index : 0u;
}

const robo_font_source_t *robo_font_face_source(const robo_font_face_t *face)
{
    return face ? face->source : NULL;
}

robo_result_t robo_font_face_style(const robo_font_face_t *face,
                                   robo_font_style_desc_t *out)
{
    if (!face || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = face->style;
    return ROBO_OK;
}

robo_result_t robo_font_face_glyph_count(const robo_font_face_t *face,
                                         uint32_t *out_count)
{
    if (!face || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    if (face->tt) {
        *out_count = robo_font_tt_num_glyphs(face->tt);
        return ROBO_OK;
    }
    if (face->metrics) {
        const robo_font_metrics_global_t *g = robo_font_metrics_info_global(face->metrics);
        if (g) {
            *out_count = g->num_glyphs;
            return ROBO_OK;
        }
    }
    if (face->cmap) {
        *out_count = robo_font_cmap_map_num_glyphs(face->cmap);
        return ROBO_OK;
    }
    if (face->sfnt) {
        return robo_font_sfnt_num_glyphs(face->sfnt, out_count);
    }
    return ROBO_NOT_SUPPORTED;
}

static robo_result_t face_ensure_sfnt(robo_font_face_t *face)
{
    robo_font_sfnt_t *sfnt = NULL;
    robo_result_t res;
    if (face->sfnt) {
        return ROBO_OK;
    }
    if (face->format != ROBO_FONT_FORMAT_SFNT) {
        return ROBO_NOT_SUPPORTED;
    }
    res = robo_font_sfnt_parse(face->source, face->face_index, ROBO_FONT_SFNT_FLAGS_NONE,
                               &sfnt);
    if (res != ROBO_OK) {
        return res;
    }
    face->sfnt = sfnt;
    return ROBO_OK;
}

robo_result_t robo_font_face_parse_truetype(robo_font_face_t *face, uint32_t tt_flags)
{
    robo_font_tt_t *tt = NULL;
    robo_result_t res;

    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (face->tt) {
        return ROBO_OK;
    }
    res = face_ensure_sfnt(face);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_tt_parse(face->sfnt, tt_flags, &tt);
    if (res != ROBO_OK) {
        return res;
    }

    face->tt = tt;
    (void)robo_font_tt_style_desc(tt, &face->style);
    /* Classification is built lazily via parse_classification. */
    return ROBO_OK;
}

robo_result_t robo_font_face_parse_classification(robo_font_face_t *face)
{
    robo_font_ot_t *ot = NULL;
    const robo_font_stat_t *stat = NULL;
    const robo_font_metadata_t *meta = NULL;
    robo_font_classification_info_t *info = NULL;
    const robo_font_classification_t *cls;
    robo_result_t res;

    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (face->classification) {
        return ROBO_OK;
    }
    res = robo_font_face_parse_truetype(face, ROBO_FONT_TT_FLAGS_NONE);
    if (res != ROBO_OK) {
        return res;
    }
    if (robo_font_ot_parse(face->sfnt, ROBO_FONT_OT_FLAGS_PARSE_EXTRA, &ot) == ROBO_OK &&
        ot) {
        stat = robo_font_ot_stat(ot);
    }
    if (!face->name_info) {
        (void)robo_font_face_parse_metadata(face);
    }
    meta = robo_font_face_metadata(face);
    res = robo_font_classification_info_create(face->tt, stat, meta, &info);
    if (ot) {
        robo_font_ot_release(ot);
    }
    if (res != ROBO_OK) {
        return res;
    }
    cls = robo_font_classification_info_get(info);
    if (cls) {
        face->style = cls->style_desc;
    }
    face->classification = info;
    return ROBO_OK;
}

const robo_font_classification_t *robo_font_face_classification(
    const robo_font_face_t *face)
{
    return face && face->classification
               ? robo_font_classification_info_get(face->classification)
               : NULL;
}

const robo_font_tt_t *robo_font_face_truetype(const robo_font_face_t *face)
{
    return face ? face->tt : NULL;
}

robo_result_t robo_font_face_parse_cmap(robo_font_face_t *face)
{
    uint32_t num_glyphs = 0u;
    robo_font_cmap_map_t *map = NULL;
    robo_result_t res;

    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (face->cmap) {
        return ROBO_OK;
    }
    res = face_ensure_sfnt(face);
    if (res != ROBO_OK) {
        return res;
    }
    if (face->tt) {
        num_glyphs = robo_font_tt_num_glyphs(face->tt);
    } else {
        res = robo_font_sfnt_num_glyphs(face->sfnt, &num_glyphs);
        if (res != ROBO_OK) {
            return res;
        }
    }
    res = robo_font_cmap_map_create(face->sfnt, num_glyphs, &map);
    if (res != ROBO_OK) {
        return res;
    }
    face->cmap = map;
    return ROBO_OK;
}

const robo_font_cmap_map_t *robo_font_face_cmap(const robo_font_face_t *face)
{
    return face ? face->cmap : NULL;
}

robo_result_t robo_font_face_parse_coverage(robo_font_face_t *face)
{
    robo_font_coverage_t *cov = NULL;
    robo_result_t res;

    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (face->coverage) {
        return ROBO_OK;
    }
    if (face->coverage_building) {
        return ROBO_BUSY;
    }
    face->coverage_building = 1;
    res = robo_font_face_parse_cmap(face);
    if (res != ROBO_OK) {
        face->coverage_building = 0;
        return res;
    }
    res = robo_font_coverage_create_from_cmap(face->cmap, NULL, &cov);
    if (res != ROBO_OK) {
        face->coverage_building = 0;
        return res;
    }
    face->coverage = cov;
    face->coverage_building = 0;
    return ROBO_OK;
}

const robo_font_coverage_t *robo_font_face_coverage(const robo_font_face_t *face)
{
    return face ? face->coverage : NULL;
}

robo_result_t robo_font_face_parse_sfnt(robo_font_face_t *face)
{
    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    return face_ensure_sfnt(face);
}

const robo_font_sfnt_t *robo_font_face_sfnt(const robo_font_face_t *face)
{
    return face ? face->sfnt : NULL;
}

robo_result_t robo_font_face_parse_emoji(robo_font_face_t *face)
{
    robo_font_emoji_info_t *info = NULL;
    robo_result_t res;

    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (face->emoji) {
        return ROBO_OK;
    }
    if (face->emoji_building) {
        return ROBO_BUSY;
    }
    face->emoji_building = 1;
    res = robo_font_emoji_info_create_from_face(face, NULL, &info);
    if (res != ROBO_OK) {
        face->emoji_building = 0;
        return res;
    }
    face->emoji = info;
    face->emoji_building = 0;
    return ROBO_OK;
}

const robo_font_emoji_info_t *robo_font_face_emoji(const robo_font_face_t *face)
{
    return face ? face->emoji : NULL;
}

robo_result_t robo_font_face_parse_variation(robo_font_face_t *face)
{
    robo_font_variation_model_t *model = NULL;
    robo_result_t res;

    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (face->variation) {
        return ROBO_OK;
    }
    if (face->variation_absent) {
        return ROBO_NOT_FOUND;
    }
    if (face->variation_building) {
        return ROBO_BUSY;
    }
    face->variation_building = 1;
    res = robo_font_variation_model_create_from_face(face, &model);
    if (res == ROBO_NOT_FOUND) {
        face->variation_absent = 1;
        face->variation_building = 0;
        return ROBO_NOT_FOUND;
    }
    if (res != ROBO_OK) {
        face->variation_building = 0;
        return res;
    }
    face->variation = model;
    face->variation_building = 0;
    return ROBO_OK;
}

const robo_font_variation_model_t *robo_font_face_variation(const robo_font_face_t *face)
{
    return face ? face->variation : NULL;
}

robo_result_t robo_font_face_lookup_glyph(const robo_font_face_t *face,
                                          uint32_t code_point,
                                          robo_font_cmap_lookup_result_t *out)
{
    if (!face || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!face->cmap) {
        return ROBO_NOT_SUPPORTED;
    }
    return robo_font_cmap_lookup(face->cmap, code_point, out);
}

robo_result_t robo_font_face_parse_metrics(robo_font_face_t *face)
{
    robo_font_metrics_info_t *info = NULL;
    robo_result_t res;

    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (face->metrics) {
        return ROBO_OK;
    }
    res = face_ensure_sfnt(face);
    if (res != ROBO_OK) {
        return res;
    }
    /* Ensure TT parse so metrics and face->tt stay coherent. */
    if (!face->tt) {
        res = robo_font_face_parse_truetype(face, ROBO_FONT_TT_FLAGS_NONE);
        if (res != ROBO_OK) {
            return res;
        }
    }
    res = robo_font_metrics_info_create_from_tt(face->tt, &info);
    if (res != ROBO_OK) {
        return res;
    }
    face->metrics = info;
    return ROBO_OK;
}

const robo_font_metrics_info_t *robo_font_face_metrics_info(const robo_font_face_t *face)
{
    return face ? face->metrics : NULL;
}

robo_result_t robo_font_face_layout_metrics(const robo_font_face_t *face,
                                            robo_font_metrics_t *out)
{
    if (!face || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!face->metrics) {
        return ROBO_NOT_SUPPORTED;
    }
    return robo_font_metrics_info_layout(face->metrics, out);
}

robo_result_t robo_font_face_glyph_metrics(const robo_font_face_t *face,
                                           robo_glyph_id_t glyph_id,
                                           robo_font_glyph_metrics_t *out)
{
    if (!face || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!face->metrics) {
        return ROBO_NOT_SUPPORTED;
    }
    return robo_font_metrics_info_glyph(face->metrics, glyph_id, out);
}

robo_result_t robo_font_face_parse_outlines(robo_font_face_t *face)
{
    robo_font_outline_tech_t tech;
    robo_result_t res;

    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (face->outline_cache) {
        return ROBO_OK;
    }
    res = face_ensure_sfnt(face);
    if (res != ROBO_OK) {
        return res;
    }
    tech = robo_font_ot_detect_outline(face->sfnt);
    if (tech == ROBO_FONT_OUTLINE_TRUETYPE) {
        if (!face->tt) {
            res = robo_font_face_parse_truetype(face, ROBO_FONT_TT_FLAGS_NONE);
            if (res != ROBO_OK) {
                return res;
            }
        }
    } else if (tech == ROBO_FONT_OUTLINE_CFF || tech == ROBO_FONT_OUTLINE_CFF2) {
        if (!face->cff) {
            res = robo_font_cff_parse(face->sfnt, ROBO_FONT_CFF_FLAGS_PARSE_ALL, &face->cff);
            if (res != ROBO_OK) {
                return res;
            }
        }
    } else {
        return ROBO_NOT_SUPPORTED;
    }
    res = robo_font_outline_cache_create(&face->outline_cache);
    return res;
}

robo_result_t robo_font_face_glyph_outline(robo_font_face_t *face,
                                           robo_glyph_id_t glyph_id,
                                           robo_font_outline_t **out)
{
    if (!face || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (!face->outline_cache || !face->sfnt) {
        return ROBO_NOT_SUPPORTED;
    }
    return robo_font_outline_cache_get(face->outline_cache, face->sfnt, face->tt,
                                       face->cff, glyph_id, out);
}

robo_result_t robo_font_face_glyph_outline_at(robo_font_face_t *face,
                                              const robo_font_outline_request_t *req,
                                              robo_font_outline_t **out)
{
    if (!face || !req || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Goal 5.18: accept coords but return static default outline. */
    (void)req->coords;
    return robo_font_face_glyph_outline(face, req->glyph_id, out);
}

robo_result_t robo_font_face_parse_metadata(robo_font_face_t *face)
{
    robo_result_t res;
    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (face->name_info) {
        return ROBO_OK;
    }
    res = face_ensure_sfnt(face);
    if (res != ROBO_OK) {
        return res;
    }
    /* Prefer TT name table when available (already validated). */
    if (!face->tt) {
        (void)robo_font_face_parse_truetype(face, ROBO_FONT_TT_FLAGS_NONE);
    }
    return robo_font_name_info_create(face->sfnt, face->tt, NULL, &face->name_info);
}

const robo_font_name_info_t *robo_font_face_name_info(const robo_font_face_t *face)
{
    return face ? face->name_info : NULL;
}

const robo_font_metadata_t *robo_font_face_metadata(const robo_font_face_t *face)
{
    return face && face->name_info ? robo_font_name_info_metadata(face->name_info) : NULL;
}
