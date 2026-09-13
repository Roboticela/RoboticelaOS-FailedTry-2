/*
 * RoboUnicodeWord — Unicode Word Segmentation (Goal 3.8).
 *
 * UAX #29 Word Boundaries (Unicode 16.0.0).
 * Default Unicode word segmentation — NOT dictionary/linguistic segmentation.
 * Does NOT implement sentence/line breaking, bidi, or shaping.
 */

#ifndef ROBO_UNICODE_WORD_H
#define ROBO_UNICODE_WORD_H

#include "RoboBase/RoboError.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboWordData.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *robo_unicode_word_ucd_version_string(void);

/**
 * Word break before code point @p index in @p cps[0..len).
 * @p index 0 → 1 (WB1). @p index >= len → 1 (WB2).
 * Internal engine — reusable for future UTF-16 adapters.
 */
int robo_unicode_word_break(const robo_uchar32_t *cps, size_t len, size_t index);

/**
 * UTF-8 word iterator.
 *
 * Each word segment is a UTF-8 byte range [word_start, word_end) (half-open).
 * A word segment may contain multiple grapheme clusters and code points.
 *
 * Relationship: bytes → code points → grapheme clusters → word boundaries
 * (all distinct concepts).
 */
typedef struct robo_unicode_word_iter {
    const char *src;
    size_t src_len;
    robo_unicode_mode_t mode;
    size_t byte_offset;
    size_t word_index;
    int valid;
} robo_unicode_word_iter_t;

robo_result_t robo_unicode_word_iter_init(robo_unicode_word_iter_t *it,
                                          const char *src,
                                          size_t src_len,
                                          robo_unicode_mode_t mode);

/**
 * Advance to the next word segment.
 * On success sets *word_start / *word_end (byte offsets, end exclusive).
 * At end: ROBO_OK with word_start == word_end == src_len.
 * Invalid UTF-8 in STRICT → ROBO_PARSE_ERROR.
 */
robo_result_t robo_unicode_word_iter_next(robo_unicode_word_iter_t *it,
                                          size_t *word_start,
                                          size_t *word_end);

robo_result_t robo_unicode_word_next(const char *src,
                                     size_t src_len,
                                     size_t byte_offset,
                                     robo_unicode_mode_t mode,
                                     size_t *word_start,
                                     size_t *word_end);

robo_result_t robo_unicode_word_prev(const char *src,
                                     size_t src_len,
                                     size_t byte_offset,
                                     robo_unicode_mode_t mode,
                                     size_t *word_start,
                                     size_t *word_end);

robo_result_t robo_unicode_word_count_utf8(const char *src,
                                           size_t src_len,
                                           robo_unicode_mode_t mode,
                                           size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_WORD_H */
