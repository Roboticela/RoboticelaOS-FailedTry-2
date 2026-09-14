/*
 * RoboFontConformance — conformance architecture helpers (Goal 5.21).
 *
 * Provides category IDs, golden-data serialization, and diagnostic formatting
 * for the RoboFont regression boundary. Does not replace per-goal suites;
 * orchestrates deterministic expected-value checks.
 *
 * Chromium/OTS/Skia are reference-only — no external font-test dependency.
 */

#ifndef ROBO_FONT_CONFORMANCE_H
#define ROBO_FONT_CONFORMANCE_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Categories (map to --filter font.conform.<name>) ---- */

typedef enum robo_font_conform_category {
    ROBO_FONT_CONFORM_SFNT = 1,
    ROBO_FONT_CONFORM_TRUETYPE = 2,
    ROBO_FONT_CONFORM_OPENTYPE = 3,
    ROBO_FONT_CONFORM_CFF = 4,
    ROBO_FONT_CONFORM_CMAP = 5,
    ROBO_FONT_CONFORM_METRICS = 6,
    ROBO_FONT_CONFORM_NAMES = 7,
    ROBO_FONT_CONFORM_STYLE = 8,
    ROBO_FONT_CONFORM_SELECTION = 9,
    ROBO_FONT_CONFORM_DISCOVERY = 10,
    ROBO_FONT_CONFORM_LOADING = 11,
    ROBO_FONT_CONFORM_COVERAGE = 12,
    ROBO_FONT_CONFORM_FALLBACK = 13,
    ROBO_FONT_CONFORM_GENERIC = 14,
    ROBO_FONT_CONFORM_EMOJI = 15,
    ROBO_FONT_CONFORM_VARIATION = 16,
    ROBO_FONT_CONFORM_COLOR = 17,
    ROBO_FONT_CONFORM_SECURITY = 18,
    ROBO_FONT_CONFORM_LIMITS = 19,
    ROBO_FONT_CONFORM_CROSS_TABLE = 20,
    ROBO_FONT_CONFORM_GOLDEN = 21,
    ROBO_FONT_CONFORM_PROPERTY = 22,
    ROBO_FONT_CONFORM_FUZZ = 23,
    ROBO_FONT_CONFORM_BENCH = 24
} robo_font_conform_category_t;

typedef enum robo_font_conform_kind {
    ROBO_FONT_CONFORM_KIND_UNIT = 0,
    ROBO_FONT_CONFORM_KIND_STRUCTURAL = 1,
    ROBO_FONT_CONFORM_KIND_BEHAVIORAL = 2,
    ROBO_FONT_CONFORM_KIND_MALFORMED = 3,
    ROBO_FONT_CONFORM_KIND_SECURITY = 4,
    ROBO_FONT_CONFORM_KIND_REGRESSION = 5,
    ROBO_FONT_CONFORM_KIND_GOLDEN = 6,
    ROBO_FONT_CONFORM_KIND_PROPERTY = 7,
    ROBO_FONT_CONFORM_KIND_FUZZ = 8,
    ROBO_FONT_CONFORM_KIND_BENCH = 9
} robo_font_conform_kind_t;

#define ROBO_FONT_CONFORM_MAX_DIAGNOSTICS 16u
#define ROBO_FONT_GOLDEN_MAX 512u

typedef struct robo_font_conform_case {
    const char *id;          /* e.g. "font.conform.cmap" */
    robo_font_conform_category_t category;
    robo_font_conform_kind_t kind;
    const char *fixture;     /* corpus relative path or "synthetic:<name>" */
    const char *purpose;
} robo_font_conform_case_t;

typedef struct robo_font_conform_diagnostic {
    char fixture[64];
    char table_tag[8]; /* four-char + NUL, or empty */
    uint32_t glyph_id; /* UINT32_MAX if N/A */
    uint32_t code_point; /* UINT32_MAX if N/A */
    robo_font_tag_t axis_tag; /* 0 if N/A */
    char expected[96];
    char actual[96];
    char message[128];
} robo_font_conform_diagnostic_t;

typedef struct robo_font_conform_result {
    int ok; /* 1 = pass */
    uint32_t diagnostic_count;
    robo_font_conform_diagnostic_t diagnostics[ROBO_FONT_CONFORM_MAX_DIAGNOSTICS];
} robo_font_conform_result_t;

typedef struct robo_font_golden_data {
    char bytes[ROBO_FONT_GOLDEN_MAX];
    size_t length;
} robo_font_golden_data_t;

const char *robo_font_conform_category_string(robo_font_conform_category_t cat);
const char *robo_font_conform_kind_string(robo_font_conform_kind_t kind);

void robo_font_conform_result_init(robo_font_conform_result_t *r);

/** Append a mismatch diagnostic (truncated safely). Returns ROBO_OK or ROBO_ERROR if full. */
robo_result_t robo_font_conform_result_add(
    robo_font_conform_result_t *r,
    const char *fixture,
    const char *table_tag,
    uint32_t glyph_id,
    uint32_t code_point,
    robo_font_tag_t axis_tag,
    const char *expected,
    const char *actual,
    const char *message);

void robo_font_golden_init(robo_font_golden_data_t *g);
robo_result_t robo_font_golden_append(robo_font_golden_data_t *g, const char *text);
robo_result_t robo_font_golden_append_u32(robo_font_golden_data_t *g, const char *key,
                                          uint32_t value);
robo_result_t robo_font_golden_append_i32(robo_font_golden_data_t *g, const char *key,
                                          int32_t value);
robo_result_t robo_font_golden_append_f32(robo_font_golden_data_t *g, const char *key,
                                          float value);
robo_result_t robo_font_golden_append_str(robo_font_golden_data_t *g, const char *key,
                                          const char *value);

/** Exact string compare of golden payloads (deterministic). */
int robo_font_golden_equals(const robo_font_golden_data_t *a,
                            const robo_font_golden_data_t *b);
int robo_font_golden_equals_cstr(const robo_font_golden_data_t *g, const char *expected);

/**
 * Fuzz smoke for conformance harness — exercises validate + cmap parse
 * entry points with a bounded budget. Never crashes on malformed input.
 */
robo_result_t robo_font_conform_fuzz_smoke(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_CONFORMANCE_H */
