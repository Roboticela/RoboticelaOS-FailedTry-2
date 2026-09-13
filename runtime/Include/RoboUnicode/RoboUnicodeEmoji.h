/*
 * RoboUnicodeEmoji — UTS #51 emoji properties & RGI sequence matching (Goal 3.13).
 *
 * Code-point properties come from RoboUnicodeProps / UCD.
 * Sequence recognition uses generated RGI tries from emoji-sequences.txt and
 * emoji-zwj-sequences.txt (Emoji 16.0 / Unicode 16.0.0).
 *
 * This is NOT font rendering, color glyphs, or shaping.
 * Emoji sequences are NOT automatically identical to grapheme clusters
 * (UAX #29 remains authoritative for EGCs).
 */

#ifndef ROBO_UNICODE_EMOJI_H
#define ROBO_UNICODE_EMOJI_H

#include "RoboBase/RoboError.h"
#include "RoboUnicode/RoboEmojiData.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *robo_unicode_emoji_ucd_version_string(void);
const char *robo_unicode_emoji_version_string(void);

/* Code-point emoji properties: see RoboUnicodeProps.h
 * (is_emoji, is_emoji_presentation, is_emoji_modifier, …). */

/**
 * Classify an exact code-point sequence as an RGI emoji sequence.
 * Returns ROBO_EMOJI_SEQ_NONE if the full sequence is not an official RGI entry
 * (partial / incomplete sequences are NONE).
 */
robo_unicode_emoji_seq_kind_t robo_unicode_emoji_sequence_classify(
    const robo_uchar32_t *cps, size_t len);

/**
 * Longest RGI emoji sequence match starting at UTF-8 byte @p offset.
 *
 * On success with a match: *out_end is exclusive end byte; *out_kind is set.
 * On success with no match: *out_end == offset, *out_kind == NONE, returns ROBO_OK.
 * Invalid UTF-8 (STRICT) → ROBO_PARSE_ERROR.
 */
robo_result_t robo_unicode_emoji_sequence_match(const char *src,
                                                size_t src_len,
                                                size_t offset,
                                                robo_unicode_mode_t mode,
                                                size_t *out_end,
                                                robo_unicode_emoji_seq_kind_t *out_kind);

/**
 * Find the next RGI emoji sequence at or after @p offset.
 * Skips non-matching scalars one at a time.
 * At end of input: ROBO_OK with *out_start == *out_end == src_len, kind NONE.
 */
robo_result_t robo_unicode_emoji_sequence_next(const char *src,
                                               size_t src_len,
                                               size_t offset,
                                               robo_unicode_mode_t mode,
                                               size_t *out_start,
                                               size_t *out_end,
                                               robo_unicode_emoji_seq_kind_t *out_kind);

const char *robo_unicode_emoji_seq_kind_name(robo_unicode_emoji_seq_kind_t k);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_EMOJI_H */
