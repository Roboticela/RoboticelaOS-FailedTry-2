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
const char *robo_unicode_grapheme_ucd_version_string(void);

/**
 * Extended Grapheme Cluster break before code point @p index in @p cps[0..len).
 * @p index 0 always returns 1 (GB1). @p index >= len returns 1 (GB2 boundary).
 * Internal engine — reusable for future UTF-16 adapters.
 */
int robo_unicode_grapheme_break(const robo_uchar32_t *cps,
                                size_t len,
                                size_t index);

/**
 * UTF-8 grapheme iterator.
 *
 * Tracks byte offsets into the source buffer. Each cluster is a byte range
 * [cluster_start, cluster_end) — half-open, consistent with C slice conventions.
 *
 * cluster_index counts extended grapheme clusters from the start of the string.
 * grapheme_count is NOT the same as code-point count or byte length.
 */
typedef struct robo_unicode_grapheme_iter {
    const char *src;
    size_t src_len;
    robo_unicode_mode_t mode;
    size_t byte_offset;      /* start byte of current cluster */
    size_t cluster_index;    /* index of current cluster (0-based) */
    int valid;               /* 0 after error or end iteration */
} robo_unicode_grapheme_iter_t;

robo_result_t robo_unicode_grapheme_iter_init(robo_unicode_grapheme_iter_t *it,
                                              const char *src,
                                              size_t src_len,
                                              robo_unicode_mode_t mode);

/**
 * Advance to the next grapheme cluster.
 * On success sets *cluster_start, *cluster_end (byte offsets, end exclusive).
 * At end: ROBO_OK with cluster_start == cluster_end == src_len.
 * Invalid UTF-8 in STRICT → ROBO_PARSE_ERROR, it->valid = 0.
 */
robo_result_t robo_unicode_grapheme_iter_next(robo_unicode_grapheme_iter_t *it,
                                              size_t *cluster_start,
                                              size_t *cluster_end);

/**
 * Move backward one cluster from @p byte_offset (any byte within a cluster).
 * Sets *cluster_start / *cluster_end for the cluster containing or ending at offset.
 * @p byte_offset 0 returns cluster [0,0) at start.
 */
robo_result_t robo_unicode_grapheme_iter_prev(const char *src,
                                              size_t src_len,
                                              size_t byte_offset,
                                              robo_unicode_mode_t mode,
                                              size_t *cluster_start,
                                              size_t *cluster_end);

/** Count extended grapheme clusters in UTF-8. STRICT → ROBO_PARSE_ERROR on ill-formed input. */
robo_result_t robo_unicode_grapheme_count_utf8(const char *src,
                                               size_t src_len,
                                               robo_unicode_mode_t mode,
                                               size_t *count);

/** Convenience: next cluster byte range after @p byte_offset (start of search). */
robo_result_t robo_unicode_grapheme_next(const char *src,
                                         size_t src_len,
                                         size_t byte_offset,
                                         robo_unicode_mode_t mode,
                                         size_t *cluster_start,
                                         size_t *cluster_end);

/** Convenience: previous cluster byte range before @p byte_offset. */
robo_result_t robo_unicode_grapheme_prev(const char *src,
                                         size_t src_len,
                                         size_t byte_offset,
                                         robo_unicode_mode_t mode,
                                         size_t *cluster_start,
                                         size_t *cluster_end);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_GRAPHEME_H */
