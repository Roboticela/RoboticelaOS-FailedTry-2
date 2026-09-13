/*
 * RoboUnicodeSentence — Unicode Sentence Segmentation (Goal 3.9).
 *
 * UAX #29 Sentence Boundaries (Unicode 16.0.0).
 * Default Unicode sentence segmentation — NOT NLP / linguistic parsing.
 * Does NOT implement line breaking, bidi, shaping, or locale tailorings.
 */

#ifndef ROBO_UNICODE_SENTENCE_H
#define ROBO_UNICODE_SENTENCE_H

#include "RoboBase/RoboError.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboSentenceData.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *robo_unicode_sentence_ucd_version_string(void);

/**
 * Sentence break before code point @p index in @p cps[0..len).
 * @p index 0 → 1 (SB1). @p index >= len → 1 (SB2).
 */
int robo_unicode_sentence_break(const robo_uchar32_t *cps, size_t len, size_t index);

/**
 * UTF-8 sentence iterator.
 *
 * Each sentence is a UTF-8 byte range [sentence_start, sentence_end) (half-open).
 * A sentence may contain many words and grapheme clusters.
 *
 * Relationship: bytes → code points → graphemes → words → sentences
 * (all distinct concepts).
 */
typedef struct robo_unicode_sentence_iter {
    const char *src;
    size_t src_len;
    robo_unicode_mode_t mode;
    size_t byte_offset;
    size_t sentence_index;
    int valid;
} robo_unicode_sentence_iter_t;

robo_result_t robo_unicode_sentence_iter_init(robo_unicode_sentence_iter_t *it,
                                              const char *src,
                                              size_t src_len,
                                              robo_unicode_mode_t mode);

/**
 * Advance to the next sentence segment.
 * On success sets *sentence_start / *sentence_end (byte offsets, end exclusive).
 * At end: ROBO_OK with sentence_start == sentence_end == src_len.
 * Invalid UTF-8 in STRICT → ROBO_PARSE_ERROR.
 */
robo_result_t robo_unicode_sentence_iter_next(robo_unicode_sentence_iter_t *it,
                                              size_t *sentence_start,
                                              size_t *sentence_end);

robo_result_t robo_unicode_sentence_next(const char *src,
                                         size_t src_len,
                                         size_t byte_offset,
                                         robo_unicode_mode_t mode,
                                         size_t *sentence_start,
                                         size_t *sentence_end);

robo_result_t robo_unicode_sentence_prev(const char *src,
                                         size_t src_len,
                                         size_t byte_offset,
                                         robo_unicode_mode_t mode,
                                         size_t *sentence_start,
                                         size_t *sentence_end);

robo_result_t robo_unicode_sentence_count_utf8(const char *src,
                                               size_t src_len,
                                               robo_unicode_mode_t mode,
                                               size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_SENTENCE_H */
