/*
 * RoboFont character-to-glyph mapping (Goal 5.6).
 *
 * Builds an immutable lookup map from the SFNT `cmap` table.
 * Does NOT implement font fallback, shaping, or text segmentation.
 *
 * Structural encoding-record discovery may also use Goal 5.4
 * `robo_font_cmap_parse`; this module adds full format bodies + lookup.
 */

#ifndef ROBO_FONT_CMAP_H
#define ROBO_FONT_CMAP_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_FONT_CMAP_MAX_SEGMENTS 8192u
#define ROBO_FONT_CMAP_MAX_GROUPS 65536u
#define ROBO_FONT_CMAP_MAX_GLYPH_ARRAY (1u << 20)
#define ROBO_FONT_CMAP_MAX_UVS_SELECTORS 256u
#define ROBO_FONT_CMAP_MAX_UVS_RANGES 8192u
#define ROBO_FONT_CMAP_MAX_UVS_MAPPINGS 65536u

/* ---- Lookup result ---- */

typedef enum robo_font_cmap_lookup_status {
    ROBO_FONT_CMAP_MAPPED = 0,
    ROBO_FONT_CMAP_UNMAPPED = 1,
    ROBO_FONT_CMAP_INVALID_CODE_POINT = 2,
    ROBO_FONT_CMAP_INVALID_FONT = 3
} robo_font_cmap_lookup_status_t;

typedef struct robo_font_cmap_lookup_result {
    robo_font_cmap_lookup_status_t status;
    robo_glyph_id_t glyph_id; /* valid only when status == MAPPED */
} robo_font_cmap_lookup_result_t;

typedef enum robo_font_cmap_uvs_status {
    ROBO_FONT_CMAP_UVS_NONE = 0,           /* no format-14 / no matching VS */
    ROBO_FONT_CMAP_UVS_DEFAULT = 1,        /* use default cmap for base */
    ROBO_FONT_CMAP_UVS_NON_DEFAULT = 2,    /* explicit glyph for (base, VS) */
    ROBO_FONT_CMAP_UVS_UNSUPPORTED = 3,    /* VS present but no UVS for base */
    ROBO_FONT_CMAP_UVS_INVALID_INPUT = 4
} robo_font_cmap_uvs_status_t;

typedef struct robo_font_cmap_uvs_result {
    robo_font_cmap_uvs_status_t status;
    uint32_t base_code_point;
    uint32_t variation_selector;
    robo_glyph_id_t glyph_id; /* NON_DEFAULT only */
} robo_font_cmap_uvs_result_t;

/* ---- Immutable map ---- */

typedef struct robo_font_cmap_map robo_font_cmap_map_t;

/**
 * Build a cmap map from an SFNT font.
 * @p num_glyphs is maxp.numGlyphs (GID validation). 0 is rejected.
 * Retains @p sfnt.
 */
robo_result_t robo_font_cmap_map_create(robo_font_sfnt_t *sfnt,
                                        uint32_t num_glyphs,
                                        robo_font_cmap_map_t **out);

/** Build from a raw cmap table span (tests / fuzz). Does not retain SFNT. */
robo_result_t robo_font_cmap_map_create_from_span(robo_font_span_t cmap_table,
                                                  uint32_t num_glyphs,
                                                  robo_font_cmap_map_t **out);

void robo_font_cmap_map_retain(robo_font_cmap_map_t *map);
void robo_font_cmap_map_release(robo_font_cmap_map_t *map);

uint32_t robo_font_cmap_map_num_glyphs(const robo_font_cmap_map_t *map);
uint16_t robo_font_cmap_map_selected_format(const robo_font_cmap_map_t *map);
uint16_t robo_font_cmap_map_selected_platform(const robo_font_cmap_map_t *map);
uint16_t robo_font_cmap_map_selected_encoding(const robo_font_cmap_map_t *map);
int robo_font_cmap_map_has_uvs(const robo_font_cmap_map_t *map);

/** True if @p cp is a Unicode scalar value (not surrogate, ≤ U+10FFFF). */
int robo_font_cmap_is_unicode_scalar(uint32_t code_point);

/**
 * Lookup Unicode code point → glyph ID.
 * Thread-safe for concurrent readers after map creation.
 */
robo_result_t robo_font_cmap_lookup(const robo_font_cmap_map_t *map,
                                    uint32_t code_point,
                                    robo_font_cmap_lookup_result_t *out);

/**
 * Convenience: returns glyph ID when mapped; otherwise 0 and sets @p out_status
 * if non-NULL. Prefer robo_font_cmap_lookup when status matters.
 */
robo_glyph_id_t robo_font_cmap_lookup_glyph(const robo_font_cmap_map_t *map,
                                            uint32_t code_point,
                                            robo_font_cmap_lookup_status_t *out_status);

/** Format-14 UVS foundation. */
robo_result_t robo_font_cmap_lookup_uvs(const robo_font_cmap_map_t *map,
                                        uint32_t base_code_point,
                                        uint32_t variation_selector,
                                        robo_font_cmap_uvs_result_t *out);

/**
 * Read maxp.numGlyphs from SFNT without a full TrueType parse.
 * Supports maxp versions 0.5 and 1.0.
 */
robo_result_t robo_font_sfnt_num_glyphs(const robo_font_sfnt_t *sfnt,
                                        uint32_t *out_count);

/** Fuzz entry: parse arbitrary SFNT or raw cmap-ish bytes safely. */
robo_result_t robo_font_cmap_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_CMAP_H */
