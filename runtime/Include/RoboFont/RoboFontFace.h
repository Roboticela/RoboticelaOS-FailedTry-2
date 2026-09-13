/*
 * RoboFont face — underlying font resource identity (Goals 5.1–5.3).
 *
 * A face represents font bytes + collection index + format classification.
 * It does NOT include paint size or variation instance state — that is
 * robo_font_instance_t.
 *
 * Optional TrueType table parse (Goal 5.3) via robo_font_face_parse_truetype.
 *
 * Ownership: face retains the source (and optional TT/SFNT objects). Destroy via release.
 */

#ifndef ROBO_FONT_FACE_H
#define ROBO_FONT_FACE_H

#include "RoboBase/RoboError.h"
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
    ROBO_FONT_FORMAT_SFNT = 1,      /* TrueType / OpenType container (detected magic) */
    ROBO_FONT_FORMAT_WOFF = 2,      /* detected; not decoded in 5.1 */
    ROBO_FONT_FORMAT_WOFF2 = 3,     /* detected; not decoded in 5.1 */
    ROBO_FONT_FORMAT_UNSUPPORTED = 4
} robo_font_format_t;

typedef struct robo_font_face robo_font_face_t;
typedef struct robo_font_tt robo_font_tt_t;

/**
 * Create a face from @p source.
 * @p face_index is the collection member index (0 for single fonts; TTC later).
 * Retains @p source. Does not parse tables.
 */
robo_result_t robo_font_face_create(robo_font_source_t *source,
                                    uint32_t face_index,
                                    robo_font_face_t **out);

void robo_font_face_retain(robo_font_face_t *face);
void robo_font_face_release(robo_font_face_t *face);

robo_font_face_id_t robo_font_face_id(const robo_font_face_t *face);
robo_font_format_t robo_font_face_format(const robo_font_face_t *face);
uint32_t robo_font_face_index(const robo_font_face_t *face);

/** Borrowed source (retained by face). NULL if face NULL. */
const robo_font_source_t *robo_font_face_source(const robo_font_face_t *face);

/**
 * Declared style of the face when known.
 * After successful parse_truetype, may reflect OS/2 / macStyle.
 */
robo_result_t robo_font_face_style(const robo_font_face_t *face,
                                   robo_font_style_desc_t *out);

/**
 * Glyph count when known from maxp (after parse_truetype).
 * Otherwise ROBO_NOT_SUPPORTED.
 */
robo_result_t robo_font_face_glyph_count(const robo_font_face_t *face,
                                         uint32_t *out_count);

/**
 * Parse SFNT + core TrueType tables for this face (idempotent).
 * @p tt_flags passed to robo_font_tt_parse (e.g. REQUIRE_GLYF).
 */
robo_result_t robo_font_face_parse_truetype(robo_font_face_t *face, uint32_t tt_flags);

/** Borrowed TrueType parse (NULL until parse_truetype succeeds). */
const robo_font_tt_t *robo_font_face_truetype(const robo_font_face_t *face);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_FACE_H */
