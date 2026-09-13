/*
 * RoboUnicodeBidi — Unicode Bidirectional Algorithm (Goal 3.11).
 *
 * UAX #9 UBA (Unicode 16.0.0).
 * Produces paragraph direction, embedding levels, and visual order.
 * NOT Arabic shaping, fonts, glyph rasterization, or CSS unicode-bidi.
 */

#ifndef ROBO_UNICODE_BIDI_H
#define ROBO_UNICODE_BIDI_H

#include "RoboBase/RoboError.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboBidiData.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Paragraph base direction request. */
typedef enum robo_unicode_bidi_paragraph_dir {
    ROBO_BIDI_LTR = 0,  /* force level 0 */
    ROBO_BIDI_RTL = 1,  /* force level 1 */
    ROBO_BIDI_AUTO = 2  /* P2/P3 first strong */
} robo_unicode_bidi_paragraph_dir_t;

/**
 * Analysis result for one paragraph (code-point sequence).
 *
 * Ownership: levels and visual_to_logical are RoboMem-allocated;
 * call robo_unicode_bidi_result_destroy().
 *
 * levels[i] is the resolved embedding level for cps[i].
 * Characters removed from reordering (BN / explicit controls after X9)
 * have levels[i] == ROBO_BIDI_LEVEL_REMOVED (0xFF).
 *
 * visual_to_logical[v] = logical index of the v-th visible character
 * after L2 reordering (length = visible_count).
 *
 * Mapping: logical code-point index ↔ UTF-8 byte ranges via the caller's
 * decode offsets (analyze_utf8 fills byte_ends[] optionally).
 */
#define ROBO_BIDI_LEVEL_REMOVED ((uint8_t)0xFFu)

typedef struct robo_unicode_bidi_result {
    size_t cp_count;
    uint8_t paragraph_level; /* 0 or 1 */
    uint8_t *levels;         /* length cp_count; ROBO_BIDI_LEVEL_REMOVED if X9-removed */
    size_t *visual_to_logical;
    size_t visible_count;
    size_t *byte_ends; /* optional UTF-8 end offsets per CP; may be NULL */
} robo_unicode_bidi_result_t;

const char *robo_unicode_bidi_ucd_version_string(void);

/** Bidi_Class after data lookup (no resolution). */
robo_unicode_bidi_class_t robo_unicode_bidi_class(robo_uchar32_t cp);

/**
 * P2/P3: first-strong paragraph level for @p cps, or 0 if none.
 * Does not allocate.
 */
uint8_t robo_unicode_bidi_paragraph_direction(const robo_uchar32_t *cps, size_t len);

/**
 * Full UBA analysis on a code-point array.
 * @p para_dir: LTR/RTL/AUTO.
 * On success fills @p out (must destroy).
 */
robo_result_t robo_unicode_bidi_analyze(const robo_uchar32_t *cps,
                                        size_t len,
                                        robo_unicode_bidi_paragraph_dir_t para_dir,
                                        robo_unicode_bidi_result_t *out);

/**
 * UTF-8 convenience: decode then analyze.
 * STRICT → ROBO_PARSE_ERROR on bad UTF-8.
 * Fills byte_ends[i] = exclusive UTF-8 end offset of code point i.
 */
robo_result_t robo_unicode_bidi_analyze_utf8(const char *src,
                                             size_t src_len,
                                             robo_unicode_mode_t mode,
                                             robo_unicode_bidi_paragraph_dir_t para_dir,
                                             robo_unicode_bidi_result_t *out);

/** Free buffers in @p r (safe on zeroed/partial). */
void robo_unicode_bidi_result_destroy(robo_unicode_bidi_result_t *r);

/**
 * Resolve levels only (same as analyze without building visual map).
 * @p levels_out must have room for @p len bytes.
 * Removed positions get ROBO_BIDI_LEVEL_REMOVED.
 * @p paragraph_level_out may be NULL.
 */
robo_result_t robo_unicode_bidi_resolve_levels(const robo_uchar32_t *cps,
                                               size_t len,
                                               robo_unicode_bidi_paragraph_dir_t para_dir,
                                               uint8_t *levels_out,
                                               uint8_t *paragraph_level_out);

/**
 * L2 reorder: given levels[0..len), write visual_to_logical[0..vis).
 * Skips ROBO_BIDI_LEVEL_REMOVED. @p visual_to_logical capacity >= len.
 * Sets *visible_count.
 */
robo_result_t robo_unicode_bidi_reorder(const uint8_t *levels,
                                        size_t len,
                                        size_t *visual_to_logical,
                                        size_t *visible_count);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_BIDI_H */
