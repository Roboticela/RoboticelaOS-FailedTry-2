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

const char *UnicodeEmojiUcdVersionString(void);
const char *UnicodeEmojiVersionString(void);

/* Code-point emoji properties: see RoboUnicodeProps.h
 * (is_emoji, is_emoji_presentation, is_emoji_modifier, …). */

/**
 * Classify an exact code-point sequence as an RGI emoji sequence.
 * Returns RoboEmojiSeqNone if the full sequence is not an official RGI entry
 * (partial / incomplete sequences are NONE).
 */
UnicodeEmojiSeqKind UnicodeEmojiSequenceClassify(
    const Uchar32 *cps, size_t len);

/**
 * Longest RGI emoji sequence match starting at UTF-8 byte @p offset.
 *
 * On success with a match: *outEnd is exclusive end byte; *outKind is set.
 * On success with no match: *outEnd == offset, *outKind == NONE, returns RoboOk.
 * Invalid UTF-8 (STRICT) → RoboParseError.
 */
Result UnicodeEmojiSequenceMatch(const char *src,
                                                size_t srcLen,
                                                size_t offset,
                                                UnicodeMode mode,
                                                size_t *outEnd,
                                                UnicodeEmojiSeqKind *outKind);

/**
 * Find the next RGI emoji sequence at or after @p offset.
 * Skips non-matching scalars one at a time.
 * At end of input: RoboOk with *outStart == *outEnd == srcLen, kind NONE.
 */
Result UnicodeEmojiSequenceNext(const char *src,
                                               size_t srcLen,
                                               size_t offset,
                                               UnicodeMode mode,
                                               size_t *outStart,
                                               size_t *outEnd,
                                               UnicodeEmojiSeqKind *outKind);

const char *UnicodeEmojiSeqKindName(UnicodeEmojiSeqKind k);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_EMOJI_H */
