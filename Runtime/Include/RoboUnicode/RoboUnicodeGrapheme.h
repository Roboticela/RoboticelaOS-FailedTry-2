/*
 * RoboUnicodeGrapheme — Extended Grapheme Cluster segmentation (Goal 3.6).
 *
 * UAX #29 Extended Grapheme Clusters (Unicode 16.0.0).
 * Does NOT implement word/sentence/line breaking, bidi, or shaping.
 */

#ifndef ROBO_UNICODE_GRAPHEME_H
#define ROBO_UNICODE_GRAPHEME_H

#include "RoboBase/RoboError.h"
#include "RoboUnicode/RoboGraphemeData.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Same UCD pin as encoding / normalization. */
const char *UnicodeGraphemeUcdVersionString(void);

/**
 * Extended Grapheme Cluster break before code point @p index in @p cps[0..len).
 * @p index 0 always returns 1 (GB1). @p index >= len returns 1 (GB2 boundary).
 * Internal engine — reusable for future UTF-16 adapters.
 */
int IsUnicodeGraphemeBreak(const Uchar32 *cps,
                                size_t len,
                                size_t index);

/**
 * UTF-8 grapheme iterator.
 *
 * Tracks byte offsets into the source buffer. Each cluster is a byte range
 * [clusterStart, clusterEnd) — half-open, consistent with C slice conventions.
 *
 * clusterIndex counts extended grapheme clusters from the start of the string.
 * grapheme_count is NOT the same as code-point count or byte length.
 */
typedef struct UnicodeGraphemeIter {
    const char *src;
    size_t srcLen;
    UnicodeMode mode;
    size_t byteOffset;      /* start byte of current cluster */
    size_t clusterIndex;    /* index of current cluster (0-based) */
    int valid;               /* 0 after error or end iteration */
} UnicodeGraphemeIter;

Result UnicodeGraphemeIterInit(UnicodeGraphemeIter *it,
                                              const char *src,
                                              size_t srcLen,
                                              UnicodeMode mode);

/**
 * Advance to the next grapheme cluster.
 * On success sets *clusterStart, *clusterEnd (byte offsets, end exclusive).
 * At end: RoboOk with clusterStart == clusterEnd == srcLen.
 * Invalid UTF-8 in STRICT → RoboParseError, it->valid = 0.
 */
Result UnicodeGraphemeIterNext(UnicodeGraphemeIter *it,
                                              size_t *clusterStart,
                                              size_t *clusterEnd);

/**
 * Move backward one cluster from @p byteOffset (any byte within a cluster).
 * Sets *clusterStart / *clusterEnd for the cluster containing or ending at offset.
 * @p byteOffset 0 returns cluster [0,0) at start.
 */
Result UnicodeGraphemeIterPrev(const char *src,
                                              size_t srcLen,
                                              size_t byteOffset,
                                              UnicodeMode mode,
                                              size_t *clusterStart,
                                              size_t *clusterEnd);

/** Count extended grapheme clusters in UTF-8. STRICT → RoboParseError on ill-formed input. */
Result UnicodeGraphemeCountUtf8(const char *src,
                                               size_t srcLen,
                                               UnicodeMode mode,
                                               size_t *count);

/** Convenience: next cluster byte range after @p byteOffset (start of search). */
Result UnicodeGraphemeNext(const char *src,
                                         size_t srcLen,
                                         size_t byteOffset,
                                         UnicodeMode mode,
                                         size_t *clusterStart,
                                         size_t *clusterEnd);

/** Convenience: previous cluster byte range before @p byteOffset. */
Result UnicodeGraphemePrev(const char *src,
                                         size_t srcLen,
                                         size_t byteOffset,
                                         UnicodeMode mode,
                                         size_t *clusterStart,
                                         size_t *clusterEnd);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_GRAPHEME_H */
