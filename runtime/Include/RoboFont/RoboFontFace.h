/*
 * RoboFont face — underlying font resource identity (Goals 5.1–5.14).
 *
 * Optional TT / cmap / coverage / metrics / outlines / metadata / classification
 * via parse_* APIs. Ownership: face retains source and optional backends.
 */

#ifndef ROBO_FONT_FACE_H
#define ROBO_FONT_FACE_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontClassification.h"
#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontCoverage.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboFont/RoboFontName.h"
#include "RoboFont/RoboFontOutline.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_font_format {
    ROBO_FONT_FORMAT_UNKNOWN = 0,
    ROBO_FONT_FORMAT_SFNT = 1,
    ROBO_FONT_FORMAT_WOFF = 2,
    ROBO_FONT_FORMAT_WOFF2 = 3,
    ROBO_FONT_FORMAT_UNSUPPORTED = 4
} robo_font_format_t;

typedef struct robo_font_face robo_font_face_t;
typedef struct robo_font_tt robo_font_tt_t;
typedef struct robo_font_sfnt robo_font_sfnt_t;
typedef struct robo_font_emoji_info robo_font_emoji_info_t;
typedef struct robo_font_variation_model robo_font_variation_model_t;
typedef struct robo_font_color_info robo_font_color_info_t;
typedef struct robo_font_outline_request robo_font_outline_request_t;

robo_result_t robo_font_face_create(robo_font_source_t *source,
                                    uint32_t face_index,
                                    robo_font_face_t **out);

void robo_font_face_retain(robo_font_face_t *face);
void robo_font_face_release(robo_font_face_t *face);

robo_font_face_id_t robo_font_face_id(const robo_font_face_t *face);
robo_font_format_t robo_font_face_format(const robo_font_face_t *face);
uint32_t robo_font_face_index(const robo_font_face_t *face);

const robo_font_source_t *robo_font_face_source(const robo_font_face_t *face);

robo_result_t robo_font_face_style(const robo_font_face_t *face,
                                   robo_font_style_desc_t *out);

robo_result_t robo_font_face_glyph_count(const robo_font_face_t *face,
                                         uint32_t *out_count);

robo_result_t robo_font_face_parse_truetype(robo_font_face_t *face, uint32_t tt_flags);
const robo_font_tt_t *robo_font_face_truetype(const robo_font_face_t *face);

robo_result_t robo_font_face_parse_cmap(robo_font_face_t *face);
const robo_font_cmap_map_t *robo_font_face_cmap(const robo_font_face_t *face);
robo_result_t robo_font_face_lookup_glyph(const robo_font_face_t *face,
                                          uint32_t code_point,
                                          robo_font_cmap_lookup_result_t *out);

/** Lazy cmap-backed coverage (Goal 5.14). Idempotent; immutable after build. */
robo_result_t robo_font_face_parse_coverage(robo_font_face_t *face);
const robo_font_coverage_t *robo_font_face_coverage(const robo_font_face_t *face);

/** Ensure SFNT container is parsed (shared by metrics/emoji/…). */
robo_result_t robo_font_face_parse_sfnt(robo_font_face_t *face);
const robo_font_sfnt_t *robo_font_face_sfnt(const robo_font_face_t *face);

/** Lazy emoji capability analysis (Goal 5.17). Idempotent; immutable after build. */
robo_result_t robo_font_face_parse_emoji(robo_font_face_t *face);
const robo_font_emoji_info_t *robo_font_face_emoji(const robo_font_face_t *face);

/** Lazy variation model (Goal 5.18). Idempotent; ROBO_NOT_FOUND if no fvar. */
robo_result_t robo_font_face_parse_variation(robo_font_face_t *face);
const robo_font_variation_model_t *robo_font_face_variation(const robo_font_face_t *face);

/** Lazy color-font info (Goal 5.19). Idempotent; ROBO_NOT_FOUND if no color tables. */
robo_result_t robo_font_face_parse_color(robo_font_face_t *face);
const robo_font_color_info_t *robo_font_face_color(const robo_font_face_t *face);

robo_result_t robo_font_face_parse_metrics(robo_font_face_t *face);
const robo_font_metrics_info_t *robo_font_face_metrics_info(const robo_font_face_t *face);
robo_result_t robo_font_face_layout_metrics(const robo_font_face_t *face,
                                            robo_font_metrics_t *out);
robo_result_t robo_font_face_glyph_metrics(const robo_font_face_t *face,
                                           robo_glyph_id_t glyph_id,
                                           robo_font_glyph_metrics_t *out);

robo_result_t robo_font_face_parse_outlines(robo_font_face_t *face);
robo_result_t robo_font_face_glyph_outline(robo_font_face_t *face,
                                           robo_glyph_id_t glyph_id,
                                           robo_font_outline_t **out);

/**
 * Variation-aware outline request (Goal 5.18 foundation).
 * Accepts coordinates but returns the static default outline; interpolation
 * is not executed yet (robo_font_outline_variation_applied → 0).
 */
robo_result_t robo_font_face_glyph_outline_at(robo_font_face_t *face,
                                              const robo_font_outline_request_t *req,
                                              robo_font_outline_t **out);

robo_result_t robo_font_face_parse_metadata(robo_font_face_t *face);
const robo_font_name_info_t *robo_font_face_name_info(const robo_font_face_t *face);
const robo_font_metadata_t *robo_font_face_metadata(const robo_font_face_t *face);

robo_result_t robo_font_face_parse_classification(robo_font_face_t *face);
const robo_font_classification_t *robo_font_face_classification(
    const robo_font_face_t *face);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_FACE_H */
