/*
 * RoboFontCoverage — Unicode character coverage analysis (Goal 5.14).
 *
 * Answers character→glyph availability questions from a font's cmap.
 * Does NOT implement fallback, shaping, language support, or rendering.
 *
 * Coverage ≠ shaping support ≠ language support ≠ fallback suitability.
 */

#ifndef ROBO_FONT_COVERAGE_H
#define ROBO_FONT_COVERAGE_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_font_face robo_font_face_t;

/* ---- Limits ---- */

#define ROBO_FONT_COVERAGE_MAX_RANGES 65536u
#define ROBO_FONT_COVERAGE_MAX_SCRIPTS 128u
#define ROBO_FONT_COVERAGE_MAX_VISIT 1048576u /* 2^20 scalars during build */

/* ---- Query status ---- */

typedef enum robo_font_coverage_status {
    /** No cmap mapping for this scalar. */
    ROBO_FONT_COVERAGE_UNSUPPORTED = 0,
    /** Mapped in cmap to glyph ID 0 (`.notdef`). */
    ROBO_FONT_COVERAGE_MAPPED_NOTDEF = 1,
    /** Mapped to a non-zero glyph ID within numGlyphs. */
    ROBO_FONT_COVERAGE_MAPPED_USABLE = 2,
    /** cmap claimed a mapping but glyph ID is out of range / malformed. */
    ROBO_FONT_COVERAGE_INVALID_MAPPING = 3,
    /** Input is not a Unicode scalar value. */
    ROBO_FONT_COVERAGE_INVALID_CODE_POINT = 4,
    /** Face/map has no usable primary cmap. */
    ROBO_FONT_COVERAGE_NO_CMAP = 5
} robo_font_coverage_status_t;

/**
 * Outline emptiness is not part of default coverage.
 * When probed via face outlines: UNKNOWN if outlines unavailable.
 */
typedef enum robo_font_coverage_outline_hint {
    ROBO_FONT_COVERAGE_OUTLINE_UNKNOWN = 0,
    ROBO_FONT_COVERAGE_OUTLINE_NONEMPTY = 1,
    ROBO_FONT_COVERAGE_OUTLINE_EMPTY = 2
} robo_font_coverage_outline_hint_t;

typedef struct robo_font_coverage_query_result {
    robo_font_coverage_status_t status;
    robo_glyph_id_t glyph_id; /* valid for MAPPED_* */
    robo_font_coverage_outline_hint_t outline_hint;
} robo_font_coverage_query_result_t;

/** Contiguous usable coverage (MAPPED_USABLE only). Inclusive bounds. */
typedef struct robo_font_coverage_range {
    uint32_t first;
    uint32_t last;
} robo_font_coverage_range_t;

typedef struct robo_font_coverage_stats {
    uint32_t mapped_scalars;       /* includes explicit .notdef mappings */
    uint32_t usable_mappings;      /* GID > 0 */
    uint32_t notdef_mappings;      /* explicit GID 0 */
    uint32_t invalid_mappings;     /* malformed GIDs observed at build */
    uint32_t range_count;
    uint32_t script_count;         /* content scripts (excludes Common/Inherited) */
    uint32_t emoji_scalars;        /* usable mappings with Emoji property */
    uint32_t min_code_point;       /* among usable; 0xFFFFFFFF if none */
    uint32_t max_code_point;       /* among usable; 0 if none */
    uint32_t category_bits;        /* bit i set → general category i seen (usable) */
    int has_uvs;
    int has_common;                /* Script or SCX includes Common */
    int has_inherited;             /* Script or SCX includes Inherited */
    int truncated;                 /* visit/range limits hit during build */
} robo_font_coverage_stats_t;

typedef struct robo_font_coverage_options {
    int analyze_scripts;    /* default 1 */
    int analyze_categories; /* default 1 */
    int analyze_emoji;      /* default 1 */
    uint32_t max_ranges;    /* 0 → ROBO_FONT_COVERAGE_MAX_RANGES */
    uint32_t max_visit;     /* 0 → ROBO_FONT_COVERAGE_MAX_VISIT */
} robo_font_coverage_options_t;

typedef struct robo_font_coverage robo_font_coverage_t;

void robo_font_coverage_options_init(robo_font_coverage_options_t *opt);

/**
 * Build immutable coverage from a cmap map. Retains @p map.
 * Does not load glyph outlines.
 */
robo_result_t robo_font_coverage_create_from_cmap(
    const robo_font_cmap_map_t *map,
    const robo_font_coverage_options_t *opt,
    robo_font_coverage_t **out);

/** Ensure face cmap, then build/attach coverage (lazy, idempotent). */
robo_result_t robo_font_coverage_create_from_face(
    robo_font_face_t *face,
    const robo_font_coverage_options_t *opt,
    robo_font_coverage_t **out);

void robo_font_coverage_retain(robo_font_coverage_t *cov);
void robo_font_coverage_release(robo_font_coverage_t *cov);

const robo_font_cmap_map_t *robo_font_coverage_cmap(const robo_font_coverage_t *cov);
const robo_font_coverage_stats_t *robo_font_coverage_stats(
    const robo_font_coverage_t *cov);

uint32_t robo_font_coverage_range_count(const robo_font_coverage_t *cov);
const robo_font_coverage_range_t *robo_font_coverage_range(
    const robo_font_coverage_t *cov, uint32_t index);

uint32_t robo_font_coverage_script_count(const robo_font_coverage_t *cov);
robo_unicode_script_t robo_font_coverage_script(const robo_font_coverage_t *cov,
                                                uint32_t index);
/** 1 if content-script list or Common/Inherited flags include @p script. */
int robo_font_coverage_has_script(const robo_font_coverage_t *cov,
                                  robo_unicode_script_t script);

/**
 * Exact membership for a usable glyph (MAPPED_USABLE).
 * Invalid scalars → 0. Does not treat .notdef as covered.
 */
int robo_font_coverage_contains(const robo_font_coverage_t *cov,
                                uint32_t code_point);

/**
 * Full query: unsupported / notdef / usable / invalid / no cmap.
 * Thread-safe for concurrent readers after construction.
 */
robo_result_t robo_font_coverage_query(const robo_font_coverage_t *cov,
                                       uint32_t code_point,
                                       robo_font_coverage_query_result_t *out);

/**
 * Glyph ID when MAPPED_USABLE; otherwise returns ROBO_NOT_FOUND and sets
 * *out_gid = 0. Never invents arbitrary glyph IDs.
 */
robo_result_t robo_font_coverage_glyph(const robo_font_coverage_t *cov,
                                       uint32_t code_point,
                                       robo_glyph_id_t *out_gid);

/** UVS coverage wrapper (format 14). */
robo_result_t robo_font_coverage_query_uvs(const robo_font_coverage_t *cov,
                                           uint32_t base_code_point,
                                           uint32_t variation_selector,
                                           robo_font_cmap_uvs_result_t *out);

/**
 * Optional: refine outline_hint using face outlines (loads one glyph outline).
 * Empty outline ≠ unsupported — still a valid mapping.
 */
robo_result_t robo_font_coverage_probe_outline(robo_font_face_t *face,
                                               robo_glyph_id_t glyph_id,
                                               robo_font_coverage_outline_hint_t *out);

/** Approximate heap bytes for cache accounting. */
uint64_t robo_font_coverage_approx_bytes(const robo_font_coverage_t *cov);

/** Fuzz/fuzz-ready: build coverage from arbitrary bytes without crashing. */
robo_result_t robo_font_coverage_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_COVERAGE_H */
