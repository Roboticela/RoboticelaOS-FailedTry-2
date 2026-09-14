/*
 * RoboFontValidate — font security & validation (Goal 5.20).
 *
 * Treats every font as untrusted input. Orchestrates staged structural
 * validation, centralized resource limits, checksum policy, and diagnostics.
 *
 * Individual parsers remain defensive; validation never disables
 * memory-safety bounds checks.
 *
 * Does NOT implement: shaping, rasterization, web-font download policy,
 * OS trust stores, DRM, or browser sandboxing.
 *
 * Chromium OTS is reference-only — no OTS/Chromium dependency.
 */

#ifndef ROBO_FONT_VALIDATE_H
#define ROBO_FONT_VALIDATE_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontLoader.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Levels (extend loader policies) ---- */

typedef enum robo_font_validation_level {
    /** Parsers' mandatory bounds only; skip optional probes. */
    ROBO_FONT_VALIDATION_NONE = 0,
    /** SFNT container + required table presence/consistency. */
    ROBO_FONT_VALIDATION_BASIC = 1,
    /** BASIC + checksums + TT/maxp consistency. */
    ROBO_FONT_VALIDATION_STRICT = 2,
    /** STRICT + color/variation/OT/CFF structural probes. */
    ROBO_FONT_VALIDATION_PARANOID = 3
} robo_font_validation_level_t;

typedef enum robo_font_validation_stage {
    ROBO_FONT_VALIDATION_STAGE_CONTAINER = 1,
    ROBO_FONT_VALIDATION_STAGE_METADATA = 2,
    ROBO_FONT_VALIDATION_STAGE_FEATURES = 3,
    ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR = 4
} robo_font_validation_stage_t;

typedef enum robo_font_validation_severity {
    ROBO_FONT_VALIDATION_SEVERITY_INFO = 0,
    ROBO_FONT_VALIDATION_SEVERITY_WARNING = 1,
    ROBO_FONT_VALIDATION_SEVERITY_ERROR = 2,
    ROBO_FONT_VALIDATION_SEVERITY_FATAL = 3
} robo_font_validation_severity_t;

typedef enum robo_font_checksum_policy {
    ROBO_FONT_CHECKSUM_ACCEPT = 0, /* record mismatch as info/warning */
    ROBO_FONT_CHECKSUM_WARN = 1,
    ROBO_FONT_CHECKSUM_REJECT = 2
} robo_font_checksum_policy_t;

typedef enum robo_font_validation_issue_code {
    ROBO_FONT_VALIDATION_OK = 0,
    ROBO_FONT_VALIDATION_SOURCE_TOO_LARGE = 1,
    ROBO_FONT_VALIDATION_INVALID_SIGNATURE = 2,
    ROBO_FONT_VALIDATION_TABLE_COUNT = 3,
    ROBO_FONT_VALIDATION_TABLE_OFFSET = 4,
    ROBO_FONT_VALIDATION_TABLE_OVERLAP = 5,
    ROBO_FONT_VALIDATION_DUPLICATE_TAG = 6,
    ROBO_FONT_VALIDATION_CHECKSUM_MISMATCH = 7,
    ROBO_FONT_VALIDATION_COLLECTION_FACES = 8,
    ROBO_FONT_VALIDATION_GLYPH_COUNT = 9,
    ROBO_FONT_VALIDATION_MISSING_REQUIRED = 10,
    ROBO_FONT_VALIDATION_PARSE_FAILED = 11,
    ROBO_FONT_VALIDATION_UNSUPPORTED = 12,
    ROBO_FONT_VALIDATION_COLOR_LIMIT = 13,
    ROBO_FONT_VALIDATION_VARIATION_LIMIT = 14,
    ROBO_FONT_VALIDATION_NAME_LIMIT = 15,
    ROBO_FONT_VALIDATION_UNICODE_INVALID = 16,
    ROBO_FONT_VALIDATION_WORK_BUDGET = 17,
    ROBO_FONT_VALIDATION_INTERNAL = 18
} robo_font_validation_issue_code_t;

#define ROBO_FONT_VALIDATION_MAX_ISSUES 64u

/* Hard ceilings — applications may request lower, never higher. */
#define ROBO_FONT_VALIDATION_HARD_MAX_SOURCE_SIZE ROBO_FONT_SFNT_MAX_SOURCE_SIZE
#define ROBO_FONT_VALIDATION_HARD_MAX_TABLES ROBO_FONT_SFNT_MAX_TABLES
#define ROBO_FONT_VALIDATION_HARD_MAX_FACES 256u
#define ROBO_FONT_VALIDATION_HARD_MAX_GLYPHS 65535u
#define ROBO_FONT_VALIDATION_HARD_MAX_ALLOC (16u * 1024u * 1024u)
#define ROBO_FONT_VALIDATION_HARD_MAX_WORK 1000000u

typedef struct robo_font_validation_limits {
    uint64_t max_source_size;
    uint32_t max_tables;
    uint32_t max_table_size;
    uint32_t max_collection_faces;
    uint32_t max_glyphs;
    uint32_t max_name_records;
    uint32_t max_variation_axes;
    uint32_t max_variation_instances;
    uint32_t max_color_palettes;
    uint32_t max_color_layers;
    uint32_t max_paint_depth;
    uint32_t max_svg_payload;
    uint32_t max_bitmap_payload;
    uint32_t max_recursion_depth;
    uint32_t max_allocation;
    uint32_t max_work_budget; /* abstract units consumed during probes */
} robo_font_validation_limits_t;

typedef struct robo_font_validation_policy {
    robo_font_validation_level_t level;
    robo_font_checksum_policy_t checksum_policy;
    uint8_t allow_unsupported_tables; /* 1 = advisory only */
    uint8_t reject_oversized;         /* 1 = FATAL on limit breach */
    uint8_t stop_on_fatal;            /* 1 = abort remaining stages */
    robo_font_validation_limits_t limits;
} robo_font_validation_policy_t;

typedef struct robo_font_validation_issue {
    robo_font_validation_issue_code_t code;
    robo_font_validation_severity_t severity;
    robo_font_validation_stage_t stage;
    robo_font_tag_t table_tag; /* 0 if N/A */
    uint32_t byte_offset;      /* UINT32_MAX if N/A */
    uint32_t glyph_id;         /* UINT32_MAX if N/A */
    robo_font_tag_t axis_tag;  /* 0 if N/A */
    uint8_t recoverable;       /* 1 if non-fatal / continue */
    char message[96];
} robo_font_validation_issue_t;

typedef struct robo_font_validation_result robo_font_validation_result_t;

void robo_font_validation_limits_init(robo_font_validation_limits_t *lim);
void robo_font_validation_limits_clamp(robo_font_validation_limits_t *lim);
void robo_font_validation_policy_init(robo_font_validation_policy_t *pol);
void robo_font_validation_policy_from_loader(robo_font_validate_policy_t loader_pol,
                                             robo_font_validation_policy_t *out);

robo_font_validation_level_t robo_font_validation_level_from_loader(
    robo_font_validate_policy_t loader_pol);

/** Map extended levels into loader enum (PARANOID preserved). */
robo_font_validate_policy_t robo_font_validation_level_to_loader(
    robo_font_validation_level_t level);

const char *robo_font_validation_issue_code_string(robo_font_validation_issue_code_t code);
const char *robo_font_validation_severity_string(robo_font_validation_severity_t sev);
const char *robo_font_validation_stage_string(robo_font_validation_stage_t stage);

robo_result_t robo_font_validation_result_create(robo_font_validation_result_t **out);
void robo_font_validation_result_retain(robo_font_validation_result_t *r);
void robo_font_validation_result_release(robo_font_validation_result_t *r);

int robo_font_validation_result_ok(const robo_font_validation_result_t *r);
int robo_font_validation_result_has_fatal(const robo_font_validation_result_t *r);
uint32_t robo_font_validation_result_issue_count(const robo_font_validation_result_t *r);
robo_result_t robo_font_validation_result_issue(const robo_font_validation_result_t *r,
                                                uint32_t index,
                                                robo_font_validation_issue_t *out);
robo_font_validation_stage_t robo_font_validation_result_completed_stage(
    const robo_font_validation_result_t *r);
uint32_t robo_font_validation_result_work_units(const robo_font_validation_result_t *r);

/**
 * Stage-1: validate source bytes as SFNT/TTC container (may parse SFNT).
 * On success may return a retained sfnt via out_sfnt (optional).
 */
robo_result_t robo_font_validate_source(robo_font_source_t *source,
                                        uint32_t face_index,
                                        const robo_font_validation_policy_t *policy,
                                        robo_font_validation_result_t **out_result,
                                        robo_font_sfnt_t **out_sfnt_opt);

/** Validate an already-parsed SFNT (stages 1–N per policy level). */
robo_result_t robo_font_validate_sfnt(robo_font_sfnt_t *sfnt,
                                      const robo_font_validation_policy_t *policy,
                                      robo_font_validation_result_t **out_result);

/**
 * Validate a face through staged probes. Face must already exist.
 * Does not replace parser defenses. Optional stages parse feature tables.
 */
robo_result_t robo_font_validate_face(robo_font_face_t *face,
                                      const robo_font_validation_policy_t *policy,
                                      robo_font_validation_result_t **out_result);

/**
 * Loader integration: map loader validate policy → full validate_face.
 * Returns ROBO_OK if no fatal issues (warnings allowed).
 */
robo_result_t robo_font_validate_for_load(robo_font_face_t *face,
                                          robo_font_validate_policy_t loader_pol,
                                          robo_font_validation_result_t **out_result_opt);

/** Fuzz entry — never crashes. */
robo_result_t robo_font_validate_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_VALIDATE_H */
