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
typedef enum UnicodeLineBreakOp {
    RoboLineBreakProhibited = 0, /* × — must not break here */
    RoboLineBreakAllowed = 1,    /* ÷ — soft / optional opportunity */
    RoboLineBreakMandatory = 2   /* ! — hard break (BK/CR/LF/NL/eot) */
} UnicodeLineBreakOp;

const char *UnicodeLineUcdVersionString(void);

/** Raw Line_Break property (before LB1). Prefer resolve() for algorithms. */
UnicodeLineBreakClass UnicodeLineBreakClassRaw(Uchar32 cp);

/** Line_Break after LB1 default resolution (AI/SG/XX→AL, SA→CM|AL, CJ→NS). */
UnicodeLineBreakClass MakeUnicodeLineBreakClass(Uchar32 cp);

/**
 * Opportunity before code point @p index in @p cps[0..len).
 * index 0 → PROHIBITED (LB2). index >= len → MANDATORY (LB3).
 */
UnicodeLineBreakOp UnicodeLineBreakOpportunity(
    const Uchar32 *cps, size_t len, size_t index);

/** 1 if opportunity is ALLOWED or MANDATORY (matches LineBreakTest ÷). */
int UnicodeLineBreak(const Uchar32 *cps, size_t len, size_t index);

/**
 * UTF-8 line-break iterator.
 *
 * Each segment is a UTF-8 byte range [lineStart, lineEnd) ending at the next
 * ALLOWED or MANDATORY opportunity (or eot). Distinct from grapheme/word/sentence.
 *
 * bytes → code points → graphemes → words → sentences → line-break opportunities
 */
typedef struct UnicodeLineIter {
    const char *src;
    size_t srcLen;
    UnicodeMode mode;
    size_t byteOffset;
    size_t lineIndex;
    int valid;
} UnicodeLineIter;

Result UnicodeLineIterInit(UnicodeLineIter *it,
                                          const char *src,
                                          size_t srcLen,
                                          UnicodeMode mode);

/**
 * Advance to the next line segment ending at a break opportunity.
 * Sets *lineStart / *lineEnd (half-open byte offsets) and *op for the break
 * at lineEnd (MANDATORY/ALLOWED), or PROHIBITED at eot with empty advance.
 */
Result UnicodeLineIterNext(UnicodeLineIter *it,
                                          size_t *lineStart,
                                          size_t *lineEnd,
                                          UnicodeLineBreakOp *op);

Result UnicodeLineBreakNext(const char *src,
                                           size_t srcLen,
                                           size_t byteOffset,
                                           UnicodeMode mode,
                                           size_t *lineStart,
                                           size_t *lineEnd,
                                           UnicodeLineBreakOp *op);

Result UnicodeLineBreakPrev(const char *src,
                                           size_t srcLen,
                                           size_t byteOffset,
                                           UnicodeMode mode,
                                           size_t *lineStart,
                                           size_t *lineEnd,
                                           UnicodeLineBreakOp *op);

Result UnicodeLineBreakCountUtf8(const char *src,
                                                 size_t srcLen,
                                                 UnicodeMode mode,
                                                 size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_LINE_H */
