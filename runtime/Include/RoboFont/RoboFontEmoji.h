/*
 * RoboFontEmoji — emoji font awareness & selection foundations (Goal 5.17).
 *
 * Identifies emoji-capable fonts, exposes color/monochrome/bitmap/vector
 * capability metadata, variation-selector queries, and sequence-request
 * foundations. Integrates with Coverage, Selector, Fallback, and the
 * generic `emoji` family (Goal 5.16).
 *
 * Does NOT implement:
 *   emoji / ZWJ shaping, presentation resolution, COLR paint execution,
 *   bitmap/SVG rasterization, or platform emoji font discovery.
 */

#ifndef ROBO_FONT_EMOJI_H
#define ROBO_FONT_EMOJI_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontCoverage.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontFallback.h"
#include "RoboFont/RoboFontGenericFamily.h"
#include "RoboFont/RoboFontRegistry.h"
#include "RoboFont/RoboFontSelection.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboUnicode/RoboUnicodeEmoji.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_FONT_EMOJI_VS_TEXT 0xFE0Eu
#define ROBO_FONT_EMOJI_VS_EMOJI 0xFE0Fu
#define ROBO_FONT_EMOJI_ZWJ 0x200Du
#define ROBO_FONT_EMOJI_KEYCAP 0x20E3u
#define ROBO_FONT_EMOJI_MAX_SEQUENCE 64u
#define ROBO_FONT_EMOJI_MAX_CANDIDATES 256u

/** Text vs emoji variation selectors (U+FE0E / U+FE0F). */
#define ROBO_FONT_EMOJI_HAS_VS_TEXT 1u
#define ROBO_FONT_EMOJI_HAS_VS_EMOJI 2u

/* ---- Kind / capabilities ---- */

/**
 * Classification of how a font relates to emoji.
 * Name hints alone never produce COLOR / MONOCHROME / CONFIGURED.
 */
typedef enum robo_font_emoji_kind {
    ROBO_FONT_EMOJI_KIND_NONE = 0,
    /** A few emoji scalars exist; not treated as an emoji font. */
    ROBO_FONT_EMOJI_KIND_INCIDENTAL = 1,
    /** Meaningful emoji coverage without color glyph tables. */
    ROBO_FONT_EMOJI_KIND_MONOCHROME = 2,
    /** Color glyph tables present with emoji-related coverage/signals. */
    ROBO_FONT_EMOJI_KIND_COLOR = 3,
    /** Caller/config explicitly marked this family/face as emoji. */
    ROBO_FONT_EMOJI_KIND_CONFIGURED = 4
} robo_font_emoji_kind_t;

typedef enum robo_font_emoji_capability {
    ROBO_FONT_EMOJI_CAP_NONE = 0,
    ROBO_FONT_EMOJI_CAP_COVERAGE = 1u << 0,
    ROBO_FONT_EMOJI_CAP_MONOCHROME = 1u << 1,
    ROBO_FONT_EMOJI_CAP_COLOR = 1u << 2,
    ROBO_FONT_EMOJI_CAP_VECTOR_COLOR = 1u << 3, /* COLR/CPAL */
    ROBO_FONT_EMOJI_CAP_BITMAP = 1u << 4,       /* sbix / CBDT+CBLC */
    ROBO_FONT_EMOJI_CAP_SVG = 1u << 5,
    ROBO_FONT_EMOJI_CAP_OUTLINE = 1u << 6,      /* glyf/CFF present */
    ROBO_FONT_EMOJI_CAP_UVS = 1u << 7,
    ROBO_FONT_EMOJI_CAP_VS_TEXT = 1u << 8,      /* FE0E record exists */
    ROBO_FONT_EMOJI_CAP_VS_EMOJI = 1u << 9,     /* FE0F record exists */
    ROBO_FONT_EMOJI_CAP_NAME_HINT = 1u << 10,   /* weak; never authoritative */
    ROBO_FONT_EMOJI_CAP_CONFIGURED = 1u << 11
} robo_font_emoji_capability_t;

typedef enum robo_font_emoji_color_preference {
    ROBO_FONT_EMOJI_COLOR_ANY = 0,
    ROBO_FONT_EMOJI_COLOR_PREFER_COLOR = 1,
    ROBO_FONT_EMOJI_COLOR_PREFER_MONO = 2,
    ROBO_FONT_EMOJI_COLOR_REQUIRE_COLOR = 3,
    ROBO_FONT_EMOJI_COLOR_REQUIRE_MONO = 4
} robo_font_emoji_color_preference_t;

/**
 * Conservative sequence-support answer.
 * Never claims true ZWJ/shaping support in Goal 5.17.
 */
typedef enum robo_font_emoji_sequence_support {
    ROBO_FONT_EMOJI_SEQ_SUPPORT_NONE = 0,
    /** Every scalar has a usable cmap mapping (components only). */
    ROBO_FONT_EMOJI_SEQ_SUPPORT_COMPONENTS = 1,
    /** Components + relevant VS mapping when the sequence includes VS. */
    ROBO_FONT_EMOJI_SEQ_SUPPORT_COMPONENTS_AND_VS = 2,
    /**
     * True single-glyph sequence support cannot be determined without
     * shaping — returned when RGI kind is ZWJ/FLAG/KEYCAP/etc.
     */
    ROBO_FONT_EMOJI_SEQ_SUPPORT_UNKNOWN = 3
} robo_font_emoji_sequence_support_t;

typedef struct robo_font_emoji_info robo_font_emoji_info_t;

typedef struct robo_font_emoji_analyze_options {
    /**
     * Treat the face as explicitly configured emoji (e.g. listed under
     * generic family `emoji`). Sets CAP_CONFIGURED / KIND_CONFIGURED.
     */
    int configured_as_emoji;
    /** Minimum usable emoji scalars for MONOCHROME without color tables. */
    uint32_t mono_emoji_scalar_threshold; /* 0 → default 8 */
    /** Minimum usable emoji scalars to count as INCIDENTAL vs NONE. */
    uint32_t incidental_emoji_scalar_threshold; /* 0 → default 1 */
} robo_font_emoji_analyze_options_t;

void robo_font_emoji_analyze_options_init(robo_font_emoji_analyze_options_t *opt);

/**
 * Build immutable emoji capability info.
 * @p sfnt may be NULL (no color-table detection).
 * @p cov may be NULL (no coverage/UVS stats).
 * @p family_name / @p full_name are optional borrowed UTF-8 (name hint only).
 */
robo_result_t robo_font_emoji_info_create(
    const robo_font_sfnt_t *sfnt,
    const robo_font_coverage_t *cov,
    const char *family_name,
    const char *full_name,
    const robo_font_emoji_analyze_options_t *opt,
    robo_font_emoji_info_t **out);

/** Analyze a face (ensures coverage; parses SFNT tables via face helpers). */
robo_result_t robo_font_emoji_info_create_from_face(
    robo_font_face_t *face,
    const robo_font_emoji_analyze_options_t *opt,
    robo_font_emoji_info_t **out);

void robo_font_emoji_info_retain(robo_font_emoji_info_t *info);
void robo_font_emoji_info_release(robo_font_emoji_info_t *info);

robo_font_emoji_kind_t robo_font_emoji_info_kind(const robo_font_emoji_info_t *info);
uint32_t robo_font_emoji_info_capabilities(const robo_font_emoji_info_t *info);
uint32_t robo_font_emoji_info_emoji_scalars(const robo_font_emoji_info_t *info);

int robo_font_emoji_info_supports_emoji(const robo_font_emoji_info_t *info);
int robo_font_emoji_info_supports_color_emoji(const robo_font_emoji_info_t *info);
int robo_font_emoji_info_supports_monochrome_emoji(const robo_font_emoji_info_t *info);
int robo_font_emoji_info_supports_variation_selectors(
    const robo_font_emoji_info_t *info);
int robo_font_emoji_info_is_classified_emoji(const robo_font_emoji_info_t *info);

const char *robo_font_emoji_kind_name(robo_font_emoji_kind_t kind);

/* ---- Coverage / VS queries ---- */

int robo_font_emoji_covers_codepoint(const robo_font_emoji_info_t *info,
                                     uint32_t code_point);

/**
 * Query UVS for (@p base, @p vs). Requires coverage with a cmap map.
 * Returns ROBO_NOT_SUPPORTED if UVS data unavailable.
 */
robo_result_t robo_font_emoji_query_uvs(const robo_font_emoji_info_t *info,
                                        uint32_t base_code_point,
                                        uint32_t variation_selector,
                                        robo_font_cmap_uvs_result_t *out);

/**
 * Presentation probe for a base emoji scalar (does not synthesize glyphs).
 * Checks coverage and optional FE0E/FE0F UVS records.
 */
typedef struct robo_font_emoji_presentation_probe {
    int base_covered;
    int has_vs_text_mapping;  /* FE0E DEFAULT or NON_DEFAULT */
    int has_vs_emoji_mapping; /* FE0F DEFAULT or NON_DEFAULT */
    robo_font_cmap_uvs_status_t vs_text_status;
    robo_font_cmap_uvs_status_t vs_emoji_status;
} robo_font_emoji_presentation_probe_t;

robo_result_t robo_font_emoji_probe_presentation(const robo_font_emoji_info_t *info,
                                                 uint32_t base_code_point,
                                                 robo_font_emoji_presentation_probe_t *out);

/* ---- Sequence foundation (no shaping) ---- */

typedef struct robo_font_emoji_sequence {
    const uint32_t *codepoints; /* borrowed */
    uint32_t count;
    /** Filled by classify helpers; ROBO_EMOJI_SEQ_NONE if unclassified. */
    robo_unicode_emoji_seq_kind_t rgi_kind;
    uint8_t has_vs_text;
    uint8_t has_vs_emoji;
    uint8_t has_zwj;
    uint8_t has_modifier;
    uint8_t has_regional_indicator;
    uint8_t has_keycap;
} robo_font_emoji_sequence_t;

void robo_font_emoji_sequence_init(robo_font_emoji_sequence_t *seq);
robo_result_t robo_font_emoji_sequence_set(robo_font_emoji_sequence_t *seq,
                                           const uint32_t *codepoints,
                                           uint32_t count);
/** Classify via RoboUnicode RGI data + scan VS/ZWJ/modifiers. */
robo_result_t robo_font_emoji_sequence_analyze(robo_font_emoji_sequence_t *seq);

robo_font_emoji_sequence_support_t robo_font_emoji_info_supports_sequence(
    const robo_font_emoji_info_t *info,
    const robo_font_emoji_sequence_t *seq);

int robo_font_emoji_content_is_emoji_related(const uint32_t *codepoints,
                                             uint32_t count);

/* ---- Selection ---- */

typedef struct robo_font_emoji_select_request {
    /**
     * Sequence / code points to cover. Optional (count 0) → rank by
     * emoji capability alone.
     */
    const uint32_t *codepoints;
    uint32_t codepoint_count;

    robo_font_emoji_color_preference_t color_preference;

    /** Style request for RoboFontSelector (family field ignored). */
    robo_font_weight_t weight;
    robo_font_stretch_t stretch;
    robo_font_style_t style;

    /**
     * Optional explicit preferred families (UTF-8), ahead of generic emoji
     * mapping. Borrowed.
     */
    const char *const *preferred_families;
    uint32_t preferred_family_count;

    /** Optional color-only preferred families. */
    const char *const *color_families;
    uint32_t color_family_count;

    /** Optional monochrome preferred families. */
    const char *const *mono_families;
    uint32_t mono_family_count;

    /**
     * When 1, resolve ROBO_FONT_GENERIC_EMOJI via @p generics and append
     * those families (after explicit preferred lists).
     */
    int use_generic_emoji;
} robo_font_emoji_select_request_t;

typedef struct robo_font_emoji_selection {
    uint32_t cookie;
    uint8_t found;
    robo_font_face_t *face; /* retained when found */
    robo_font_face_id_t face_id;
    robo_font_emoji_kind_t kind;
    uint32_t capabilities;
    uint32_t score;
    char family[128];
} robo_font_emoji_selection_t;

#define ROBO_FONT_EMOJI_SELECTION_COOKIE 0x454D4F31u /* 'EMO1' */

void robo_font_emoji_select_request_init(robo_font_emoji_select_request_t *req);
void robo_font_emoji_selection_init(robo_font_emoji_selection_t *out);
void robo_font_emoji_selection_clear(robo_font_emoji_selection_t *out);

/**
 * Select best emoji-capable face from the registry.
 * Ranking (deterministic):
 *   configured/classified → coverage → VS → color preference →
 *   family priority → face_id.
 * Does not invent fonts when nothing matches.
 */
robo_result_t robo_font_emoji_select(
    robo_font_registry_t *registry,
    robo_font_generic_resolver_t *generics, /* optional; for use_generic_emoji */
    const robo_font_emoji_select_request_t *req,
    robo_font_emoji_selection_t *out);

/**
 * When primary lacks emoji-related coverage, resolve via emoji select then
 * RoboFontFallback with emoji families as preferred.
 */
robo_result_t robo_font_emoji_resolve_fallback(
    robo_font_registry_t *registry,
    robo_font_generic_resolver_t *generics,
    robo_font_fallback_resolver_t *fallback,
    robo_font_face_t *primary,
    const robo_font_emoji_select_request_t *req,
    robo_font_emoji_selection_t *emoji_out, /* optional */
    robo_font_fallback_result_t *fallback_out);

/* ---- Face lazy cache ---- */

/** Lazy, idempotent; immutable after success. */
robo_result_t robo_font_face_parse_emoji(robo_font_face_t *face);
const robo_font_emoji_info_t *robo_font_face_emoji(const robo_font_face_t *face);

/**
 * Ensure SFNT is parsed on the face (needed for color-table detection).
 * Public so emoji analysis can share the face cache.
 */
robo_result_t robo_font_face_parse_sfnt(robo_font_face_t *face);
const robo_font_sfnt_t *robo_font_face_sfnt(const robo_font_face_t *face);

/* ---- Fuzz ---- */

robo_result_t robo_font_emoji_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_EMOJI_H */
