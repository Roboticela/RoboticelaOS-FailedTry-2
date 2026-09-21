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
typedef enum UnicodeBidiParagraphDir {
    RoboBidiLtr = 0,  /* force level 0 */
    RoboBidiRtl = 1,  /* force level 1 */
    RoboBidiAuto = 2  /* P2/P3 first strong */
} UnicodeBidiParagraphDir;

/**
 * Analysis result for one paragraph (code-point sequence).
 *
 * Ownership: levels and visualToLogical are RoboMem-allocated;
 * call UnicodeBidiResultDestroy().
 *
 * levels[i] is the resolved embedding level for cps[i].
 * Characters removed from reordering (BN / explicit controls after X9)
 * have levels[i] == RoboBidiLevelRemoved (0xFF).
 *
 * visualToLogical[v] = logical index of the v-th visible character
 * after L2 reordering (length = VisibleCount).
 *
 * Mapping: logical code-point index ↔ UTF-8 byte ranges via the caller's
 * decode offsets (analyze_utf8 fills byteEnds[] optionally).
 */
#define RoboBidiLevelRemoved ((uint8_t)0xFFu)

typedef struct UnicodeBidiResult {
    size_t cpCount;
    uint8_t paragraphLevel; /* 0 or 1 */
    uint8_t *levels;         /* length cpCount; RoboBidiLevelRemoved if X9-removed */
    size_t *visualToLogical;
    size_t VisibleCount;
    size_t *byteEnds; /* optional UTF-8 end offsets per CP; may be NULL */
} UnicodeBidiResult;

const char *UnicodeBidiUcdVersionString(void);

/** Bidi_Class after data lookup (no resolution). */
UnicodeBidiClass MakeUnicodeBidiClass(Uchar32 cp);

/**
 * P2/P3: first-strong paragraph level for @p cps, or 0 if none.
 * Does not allocate.
 */
uint8_t UnicodeBidiParagraphDirection(const Uchar32 *cps, size_t len);

/**
 * Full UBA analysis on a code-point array.
 * @p paraDir: LTR/RTL/AUTO.
 * On success fills @p out (must destroy).
 */
Result UnicodeBidiAnalyze(const Uchar32 *cps,
                                        size_t len,
                                        UnicodeBidiParagraphDir paraDir,
                                        UnicodeBidiResult *out);

/**
 * UTF-8 convenience: decode then analyze.
 * STRICT → RoboParseError on bad UTF-8.
 * Fills byteEnds[i] = exclusive UTF-8 end offset of code point i.
 */
Result UnicodeBidiAnalyzeUtf8(const char *src,
                                             size_t srcLen,
                                             UnicodeMode mode,
                                             UnicodeBidiParagraphDir paraDir,
                                             UnicodeBidiResult *out);

/** Free buffers in @p r (safe on zeroed/partial). */
void UnicodeBidiResultDestroy(UnicodeBidiResult *r);

/**
 * Resolve levels only (same as analyze without building visual map).
 * @p levelsOut must have room for @p len bytes.
 * Removed positions get RoboBidiLevelRemoved.
 * @p paragraphLevelOut may be NULL.
 */
Result UnicodeBidiResolveLevels(const Uchar32 *cps,
                                               size_t len,
                                               UnicodeBidiParagraphDir paraDir,
                                               uint8_t *levelsOut,
                                               uint8_t *paragraphLevelOut);

/**
 * L2 reorder: given levels[0..len), write visualToLogical[0..vis).
 * Skips RoboBidiLevelRemoved. @p visualToLogical capacity >= len.
 * Sets *VisibleCount.
 */
Result UnicodeBidiReorder(const uint8_t *levels,
                                        size_t len,
                                        size_t *visualToLogical,
                                        size_t *VisibleCount);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_BIDI_H */
