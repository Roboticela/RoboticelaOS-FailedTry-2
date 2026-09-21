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

const char *UnicodeWordUcdVersionString(void);

/**
 * Word break before code point @p index in @p cps[0..len).
 * @p index 0 → 1 (WB1). @p index >= len → 1 (WB2).
 * Internal engine — reusable for future UTF-16 adapters.
 */
int IsUnicodeWordBreak(const Uchar32 *cps, size_t len, size_t index);

/**
 * UTF-8 word iterator.
 *
 * Each word segment is a UTF-8 byte range [wordStart, wordEnd) (half-open).
 * A word segment may contain multiple grapheme clusters and code points.
 *
 * Relationship: bytes → code points → grapheme clusters → word boundaries
 * (all distinct concepts).
 */
typedef struct UnicodeWordIter {
    const char *src;
    size_t srcLen;
    UnicodeMode mode;
    size_t byteOffset;
    size_t wordIndex;
    int valid;
} UnicodeWordIter;

Result UnicodeWordIterInit(UnicodeWordIter *it,
                                          const char *src,
                                          size_t srcLen,
                                          UnicodeMode mode);

/**
 * Advance to the next word segment.
 * On success sets *wordStart / *wordEnd (byte offsets, end exclusive).
 * At end: RoboOk with wordStart == wordEnd == srcLen.
 * Invalid UTF-8 in STRICT → RoboParseError.
 */
Result UnicodeWordIterNext(UnicodeWordIter *it,
                                          size_t *wordStart,
                                          size_t *wordEnd);

Result UnicodeWordNext(const char *src,
                                     size_t srcLen,
                                     size_t byteOffset,
                                     UnicodeMode mode,
                                     size_t *wordStart,
                                     size_t *wordEnd);

Result UnicodeWordPrev(const char *src,
                                     size_t srcLen,
                                     size_t byteOffset,
                                     UnicodeMode mode,
                                     size_t *wordStart,
                                     size_t *wordEnd);

Result UnicodeWordCountUtf8(const char *src,
                                           size_t srcLen,
                                           UnicodeMode mode,
                                           size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_WORD_H */
