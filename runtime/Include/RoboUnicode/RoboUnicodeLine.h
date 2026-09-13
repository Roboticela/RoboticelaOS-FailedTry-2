/*
 * RoboUnicodeLine — Unicode Line Breaking (Goal 3.10).
 *
 * UAX #14 Line Breaking (Unicode 16.0.0).
 * Determines legal line-break opportunities — NOT visual layout, fonts,
 * glyph metrics, hyphenation, or CSS white-space / word-break.
 */

#ifndef ROBO_UNICODE_LINE_H
#define ROBO_UNICODE_LINE_H

#include "RoboBase/RoboError.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboLineData.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Break opportunity kind before a code-point index.
 * Mandatory vs allowed are distinct — do not collapse to a boolean.
 */
typedef enum robo_unicode_line_break_op {
    ROBO_LINE_BREAK_PROHIBITED = 0, /* × — must not break here */
    ROBO_LINE_BREAK_ALLOWED = 1,    /* ÷ — soft / optional opportunity */
    ROBO_LINE_BREAK_MANDATORY = 2   /* ! — hard break (BK/CR/LF/NL/eot) */
} robo_unicode_line_break_op_t;

const char *robo_unicode_line_ucd_version_string(void);

/** Raw Line_Break property (before LB1). Prefer resolve() for algorithms. */
robo_unicode_line_break_class_t robo_unicode_line_break_class_raw(robo_uchar32_t cp);

/** Line_Break after LB1 default resolution (AI/SG/XX→AL, SA→CM|AL, CJ→NS). */
robo_unicode_line_break_class_t robo_unicode_line_break_class(robo_uchar32_t cp);

/**
 * Opportunity before code point @p index in @p cps[0..len).
 * index 0 → PROHIBITED (LB2). index >= len → MANDATORY (LB3).
 */
robo_unicode_line_break_op_t robo_unicode_line_break_opportunity(
    const robo_uchar32_t *cps, size_t len, size_t index);

/** 1 if opportunity is ALLOWED or MANDATORY (matches LineBreakTest ÷). */
int robo_unicode_line_break(const robo_uchar32_t *cps, size_t len, size_t index);

/**
 * UTF-8 line-break iterator.
 *
 * Each segment is a UTF-8 byte range [line_start, line_end) ending at the next
 * ALLOWED or MANDATORY opportunity (or eot). Distinct from grapheme/word/sentence.
 *
 * bytes → code points → graphemes → words → sentences → line-break opportunities
 */
typedef struct robo_unicode_line_iter {
    const char *src;
    size_t src_len;
    robo_unicode_mode_t mode;
    size_t byte_offset;
    size_t line_index;
    int valid;
} robo_unicode_line_iter_t;

robo_result_t robo_unicode_line_iter_init(robo_unicode_line_iter_t *it,
                                          const char *src,
                                          size_t src_len,
                                          robo_unicode_mode_t mode);

/**
 * Advance to the next line segment ending at a break opportunity.
 * Sets *line_start / *line_end (half-open byte offsets) and *op for the break
 * at line_end (MANDATORY/ALLOWED), or PROHIBITED at eot with empty advance.
 */
robo_result_t robo_unicode_line_iter_next(robo_unicode_line_iter_t *it,
                                          size_t *line_start,
                                          size_t *line_end,
                                          robo_unicode_line_break_op_t *op);

robo_result_t robo_unicode_line_break_next(const char *src,
                                           size_t src_len,
                                           size_t byte_offset,
                                           robo_unicode_mode_t mode,
                                           size_t *line_start,
                                           size_t *line_end,
                                           robo_unicode_line_break_op_t *op);

robo_result_t robo_unicode_line_break_prev(const char *src,
                                           size_t src_len,
                                           size_t byte_offset,
                                           robo_unicode_mode_t mode,
                                           size_t *line_start,
                                           size_t *line_end,
                                           robo_unicode_line_break_op_t *op);

robo_result_t robo_unicode_line_break_count_utf8(const char *src,
                                                 size_t src_len,
                                                 robo_unicode_mode_t mode,
                                                 size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_LINE_H */
