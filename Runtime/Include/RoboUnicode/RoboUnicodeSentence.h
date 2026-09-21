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

const char *UnicodeSentenceUcdVersionString(void);

/**
 * Sentence break before code point @p index in @p cps[0..len).
 * @p index 0 → 1 (SB1). @p index >= len → 1 (SB2).
 */
int IsUnicodeSentenceBreak(const Uchar32 *cps, size_t len, size_t index);

/**
 * UTF-8 sentence iterator.
 *
 * Each sentence is a UTF-8 byte range [sentenceStart, sentenceEnd) (half-open).
 * A sentence may contain many words and grapheme clusters.
 *
 * Relationship: bytes → code points → graphemes → words → sentences
 * (all distinct concepts).
 */
typedef struct UnicodeSentenceIter {
    const char *src;
    size_t srcLen;
    UnicodeMode mode;
    size_t byteOffset;
    size_t sentenceIndex;
    int valid;
} UnicodeSentenceIter;

Result UnicodeSentenceIterInit(UnicodeSentenceIter *it,
                                              const char *src,
                                              size_t srcLen,
                                              UnicodeMode mode);

/**
 * Advance to the next sentence segment.
 * On success sets *sentenceStart / *sentenceEnd (byte offsets, end exclusive).
 * At end: RoboOk with sentenceStart == sentenceEnd == srcLen.
 * Invalid UTF-8 in STRICT → RoboParseError.
 */
Result UnicodeSentenceIterNext(UnicodeSentenceIter *it,
                                              size_t *sentenceStart,
                                              size_t *sentenceEnd);

Result UnicodeSentenceNext(const char *src,
                                         size_t srcLen,
                                         size_t byteOffset,
                                         UnicodeMode mode,
                                         size_t *sentenceStart,
                                         size_t *sentenceEnd);

Result UnicodeSentencePrev(const char *src,
                                         size_t srcLen,
                                         size_t byteOffset,
                                         UnicodeMode mode,
                                         size_t *sentenceStart,
                                         size_t *sentenceEnd);

Result UnicodeSentenceCountUtf8(const char *src,
                                               size_t srcLen,
                                               UnicodeMode mode,
                                               size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_SENTENCE_H */
