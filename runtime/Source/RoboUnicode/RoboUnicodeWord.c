/*
 * Unicode Word Boundaries — UAX #29 (Unicode 16.0.0).
 */

#include "RoboUnicode/RoboUnicodeWord.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <string.h>

const char *robo_unicode_word_ucd_version_string(void)
{
    return ROBO_WORD_UCD_VERSION;
}

static int robo_wb_is_ahletter(robo_unicode_word_break_t p)
{
    return p == ROBO_U_WB_ALETTER || p == ROBO_U_WB_HEBREW_LETTER;
}

static int robo_wb_is_midnumletq(robo_unicode_word_break_t p)
{
    return p == ROBO_U_WB_MIDNUMLET || p == ROBO_U_WB_SINGLE_QUOTE;
}

static int robo_wb_is_ignore(robo_unicode_word_break_t p)
{
    return p == ROBO_U_WB_EXTEND || p == ROBO_U_WB_FORMAT || p == ROBO_U_WB_ZWJ;
}

static int robo_wb_is_sep(robo_unicode_word_break_t p)
{
    return p == ROBO_U_WB_NEWLINE || p == ROBO_U_WB_CR || p == ROBO_U_WB_LF;
}

/* Previous non-ignored index strictly before @p pos, or (size_t)-1. */
static size_t robo_wb_prev(const robo_uchar32_t *cps, size_t pos)
{
    size_t i;
    if (pos == 0u) {
        return (size_t)-1;
    }
    i = pos;
    do {
        i--;
        if (!robo_wb_is_ignore(robo_word_break_property(cps[i]))) {
            return i;
        }
    } while (i > 0u);
    return (size_t)-1;
}

/* Next non-ignored index at or after @p pos, or len if none. */
static size_t robo_wb_next(const robo_uchar32_t *cps, size_t len, size_t pos)
{
    size_t i = pos;
    while (i < len && robo_wb_is_ignore(robo_word_break_property(cps[i]))) {
        i++;
    }
    return i;
}

static int robo_wb_ri_odd_before(const robo_uchar32_t *cps, size_t index)
{
    size_t j;
    size_t count = 0;
    /* Count consecutive RI (skipping ignores) immediately before index. */
    j = index;
    while (j > 0u) {
        size_t p = robo_wb_prev(cps, j);
        if (p == (size_t)-1) {
            break;
        }
        if (robo_word_break_property(cps[p]) != ROBO_U_WB_REGIONAL_INDICATOR) {
            break;
        }
        count++;
        j = p;
    }
    return (count % 2u) == 1u;
}

int robo_unicode_word_break(const robo_uchar32_t *cps, size_t len, size_t index)
{
    robo_unicode_word_break_t lb;
    robo_unicode_word_break_t rb;
    size_t li;
    size_t ri;
    size_t n1;
    robo_unicode_word_break_t p1;

    if (index == 0u) {
        return 1; /* WB1 */
    }
    if (index >= len || !cps) {
        return 1; /* WB2 */
    }

    lb = robo_word_break_property(cps[index - 1u]);
    rb = robo_word_break_property(cps[index]);

    /* WB3 */
    if (lb == ROBO_U_WB_CR && rb == ROBO_U_WB_LF) {
        return 0;
    }
    /* WB3a */
    if (robo_wb_is_sep(lb)) {
        return 1;
    }
    /* WB3b */
    if (robo_wb_is_sep(rb)) {
        return 1;
    }
    /* WB3c */
    if (lb == ROBO_U_WB_ZWJ && robo_unicode_is_extended_pictographic(cps[index])) {
        return 0;
    }
    /* WB3d */
    if (lb == ROBO_U_WB_WSEGSPACE && rb == ROBO_U_WB_WSEGSPACE) {
        return 0;
    }
    /* WB4 — Any × (Extend | Format | ZWJ) except after sep (already handled) */
    if (robo_wb_is_ignore(rb)) {
        return 0;
    }

    /* Effective left (skip ignores) and right (already non-ignore). */
    li = robo_wb_prev(cps, index);
    if (li == (size_t)-1) {
        return 1; /* only ignores between sot and here → break (shouldn't usually happen) */
    }
    lb = robo_word_break_property(cps[li]);
    rb = robo_word_break_property(cps[index]);
    ri = index;

    /* WB5 */
    if (robo_wb_is_ahletter(lb) && robo_wb_is_ahletter(rb)) {
        return 0;
    }

    /* WB6: AHLetter × (MidLetter | MidNumLetQ) AHLetter */
    if (robo_wb_is_ahletter(lb) &&
        (rb == ROBO_U_WB_MIDLETTER || robo_wb_is_midnumletq(rb))) {
        n1 = robo_wb_next(cps, len, ri + 1u);
        if (n1 < len && robo_wb_is_ahletter(robo_word_break_property(cps[n1]))) {
            return 0;
        }
    }

    /* WB7: AHLetter (MidLetter | MidNumLetQ) × AHLetter */
    if (robo_wb_is_ahletter(rb)) {
        p1 = lb;
        if (p1 == ROBO_U_WB_MIDLETTER || robo_wb_is_midnumletq(p1)) {
            n1 = robo_wb_prev(cps, li);
            if (n1 != (size_t)-1 &&
                robo_wb_is_ahletter(robo_word_break_property(cps[n1]))) {
                return 0;
            }
        }
    }

    /* WB7a */
    if (lb == ROBO_U_WB_HEBREW_LETTER && rb == ROBO_U_WB_SINGLE_QUOTE) {
        return 0;
    }

    /* WB7b: Hebrew_Letter × Double_Quote Hebrew_Letter */
    if (lb == ROBO_U_WB_HEBREW_LETTER && rb == ROBO_U_WB_DOUBLE_QUOTE) {
        n1 = robo_wb_next(cps, len, ri + 1u);
        if (n1 < len &&
            robo_word_break_property(cps[n1]) == ROBO_U_WB_HEBREW_LETTER) {
            return 0;
        }
    }

    /* WB7c: Hebrew_Letter Double_Quote × Hebrew_Letter */
    if (rb == ROBO_U_WB_HEBREW_LETTER && lb == ROBO_U_WB_DOUBLE_QUOTE) {
        n1 = robo_wb_prev(cps, li);
        if (n1 != (size_t)-1 &&
            robo_word_break_property(cps[n1]) == ROBO_U_WB_HEBREW_LETTER) {
            return 0;
        }
    }

    /* WB8 */
    if (lb == ROBO_U_WB_NUMERIC && rb == ROBO_U_WB_NUMERIC) {
        return 0;
    }
    /* WB9 */
    if (robo_wb_is_ahletter(lb) && rb == ROBO_U_WB_NUMERIC) {
        return 0;
    }
    /* WB10 */
    if (lb == ROBO_U_WB_NUMERIC && robo_wb_is_ahletter(rb)) {
        return 0;
    }

    /* WB11: Numeric (MidNum | MidNumLetQ) × Numeric */
    if (rb == ROBO_U_WB_NUMERIC &&
        (lb == ROBO_U_WB_MIDNUM || robo_wb_is_midnumletq(lb))) {
        n1 = robo_wb_prev(cps, li);
        if (n1 != (size_t)-1 &&
            robo_word_break_property(cps[n1]) == ROBO_U_WB_NUMERIC) {
            return 0;
        }
    }

    /* WB12: Numeric × (MidNum | MidNumLetQ) Numeric */
    if (lb == ROBO_U_WB_NUMERIC &&
        (rb == ROBO_U_WB_MIDNUM || robo_wb_is_midnumletq(rb))) {
        n1 = robo_wb_next(cps, len, ri + 1u);
        if (n1 < len &&
            robo_word_break_property(cps[n1]) == ROBO_U_WB_NUMERIC) {
            return 0;
        }
    }

    /* WB13 */
    if (lb == ROBO_U_WB_KATAKANA && rb == ROBO_U_WB_KATAKANA) {
        return 0;
    }

    /* WB13a */
    if ((robo_wb_is_ahletter(lb) || lb == ROBO_U_WB_NUMERIC ||
         lb == ROBO_U_WB_KATAKANA || lb == ROBO_U_WB_EXTENDNUMLET) &&
        rb == ROBO_U_WB_EXTENDNUMLET) {
        return 0;
    }

    /* WB13b */
    if (lb == ROBO_U_WB_EXTENDNUMLET &&
        (robo_wb_is_ahletter(rb) || rb == ROBO_U_WB_NUMERIC ||
         rb == ROBO_U_WB_KATAKANA)) {
        return 0;
    }

    /* WB15 / WB16 — RI × RI with odd RI run before */
    if (lb == ROBO_U_WB_REGIONAL_INDICATOR &&
        rb == ROBO_U_WB_REGIONAL_INDICATOR) {
        if (robo_wb_ri_odd_before(cps, index)) {
            return 0;
        }
    }

    /* WB999 */
    return 1;
}

static robo_result_t robo_word_decode_at(const char *src,
                                         size_t src_len,
                                         size_t offset,
                                         robo_unicode_mode_t mode,
                                         robo_uchar32_t *cp,
                                         size_t *byte_end)
{
    size_t next = offset;
    robo_unicode_status_t st =
        robo_unicode_utf8_decode(src, src_len, offset, mode, cp, &next);
    if (st != ROBO_UNICODE_OK) {
        return ROBO_PARSE_ERROR;
    }
    *byte_end = next;
    return ROBO_OK;
}

static robo_result_t robo_word_next_segment(const char *src,
                                            size_t src_len,
                                            size_t byte_offset,
                                            robo_unicode_mode_t mode,
                                            size_t *word_start,
                                            size_t *word_end)
{
    robo_uchar32_t stack_cps[96];
    size_t stack_ends[96];
    robo_uchar32_t *cps = stack_cps;
    size_t *cp_ends = stack_ends;
    size_t cap = 96u;
    size_t n = 0;
    size_t off = byte_offset;
    robo_result_t r;

    if (byte_offset >= src_len) {
        *word_start = src_len;
        *word_end = src_len;
        return ROBO_OK;
    }
    *word_start = byte_offset;

    for (;;) {
        robo_uchar32_t cp = 0;
        size_t bend = off;
        size_t peek_n;
        size_t peek_off;
        size_t i;

        if (off >= src_len) {
            break;
        }
        r = robo_word_decode_at(src, src_len, off, mode, &cp, &bend);
        if (r != ROBO_OK) {
            if (cps != stack_cps) {
                robo_free(cps);
            }
            if (cp_ends != stack_ends) {
                robo_free(cp_ends);
            }
            return r;
        }

        if (n > 0u) {
            /*
             * Look-ahead: WB6/WB7/WB7b/WB11/WB12 need characters after the
             * candidate boundary. Peek additional scalars from the source.
             */
            robo_uchar32_t peek_stack[128];
            robo_uchar32_t *peek = peek_stack;
            size_t peek_cap = 128u;
            int heap_peek = 0;

            if (n + 1u + 16u > peek_cap) {
                peek_cap = n + 1u + 32u;
                peek = (robo_uchar32_t *)robo_malloc(peek_cap * sizeof(robo_uchar32_t));
                if (!peek) {
                    if (cps != stack_cps) {
                        robo_free(cps);
                    }
                    if (cp_ends != stack_ends) {
                        robo_free(cp_ends);
                    }
                    return ROBO_OUT_OF_MEMORY;
                }
                heap_peek = 1;
            }
            for (i = 0; i < n; i++) {
                peek[i] = cps[i];
            }
            peek[n] = cp;
            peek_n = n + 1u;
            peek_off = bend;
            while (peek_n < n + 1u + 16u && peek_off < src_len) {
                robo_uchar32_t pcp = 0;
                size_t pend = peek_off;
                r = robo_word_decode_at(src, src_len, peek_off, mode, &pcp, &pend);
                if (r != ROBO_OK) {
                    if (heap_peek) {
                        robo_free(peek);
                    }
                    if (cps != stack_cps) {
                        robo_free(cps);
                    }
                    if (cp_ends != stack_ends) {
                        robo_free(cp_ends);
                    }
                    return r;
                }
                if (peek_n >= peek_cap) {
                    break;
                }
                peek[peek_n++] = pcp;
                peek_off = pend;
            }
            if (robo_unicode_word_break(peek, peek_n, n)) {
                if (heap_peek) {
                    robo_free(peek);
                }
                break;
            }
            if (heap_peek) {
                robo_free(peek);
            }
        }

        if (n >= cap) {
            size_t ncap = cap * 2u;
            robo_uchar32_t *nc =
                (robo_uchar32_t *)robo_malloc(ncap * sizeof(robo_uchar32_t));
            size_t *ne = (size_t *)robo_malloc(ncap * sizeof(size_t));
            if (!nc || !ne) {
                robo_free(nc);
                robo_free(ne);
                if (cps != stack_cps) {
                    robo_free(cps);
                }
                if (cp_ends != stack_ends) {
                    robo_free(cp_ends);
                }
                return ROBO_OUT_OF_MEMORY;
            }
            memcpy(nc, cps, n * sizeof(robo_uchar32_t));
            memcpy(ne, cp_ends, n * sizeof(size_t));
            if (cps != stack_cps) {
                robo_free(cps);
            }
            if (cp_ends != stack_ends) {
                robo_free(cp_ends);
            }
            cps = nc;
            cp_ends = ne;
            cap = ncap;
        }
        cps[n] = cp;
        cp_ends[n] = bend;
        n++;
        off = bend;
    }

    *word_end = (n == 0u) ? byte_offset : cp_ends[n - 1u];
    if (cps != stack_cps) {
        robo_free(cps);
    }
    if (cp_ends != stack_ends) {
        robo_free(cp_ends);
    }
    return ROBO_OK;
}

robo_result_t robo_unicode_word_iter_init(robo_unicode_word_iter_t *it,
                                          const char *src,
                                          size_t src_len,
                                          robo_unicode_mode_t mode)
{
    if (!it) {
        return ROBO_INVALID_ARGUMENT;
    }
    it->src = src;
    it->src_len = src_len;
    it->mode = mode;
    it->byte_offset = 0;
    it->word_index = 0;
    it->valid = 1;
    return ROBO_OK;
}

robo_result_t robo_unicode_word_iter_next(robo_unicode_word_iter_t *it,
                                          size_t *word_start,
                                          size_t *word_end)
{
    robo_result_t r;

    if (!it || !word_start || !word_end) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!it->valid) {
        return ROBO_PARSE_ERROR;
    }
    r = robo_word_next_segment(
        it->src, it->src_len, it->byte_offset, it->mode, word_start, word_end);
    if (r != ROBO_OK) {
        it->valid = 0;
        return r;
    }
    it->byte_offset = *word_end;
    if (*word_start < it->src_len && *word_start != *word_end) {
        it->word_index++;
    }
    return ROBO_OK;
}

robo_result_t robo_unicode_word_next(const char *src,
                                     size_t src_len,
                                     size_t byte_offset,
                                     robo_unicode_mode_t mode,
                                     size_t *word_start,
                                     size_t *word_end)
{
    if (byte_offset > src_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_word_next_segment(
        src, src_len, byte_offset, mode, word_start, word_end);
}

robo_result_t robo_unicode_word_prev(const char *src,
                                     size_t src_len,
                                     size_t byte_offset,
                                     robo_unicode_mode_t mode,
                                     size_t *word_start,
                                     size_t *word_end)
{
    robo_unicode_word_iter_t it;
    size_t ws = 0;
    size_t we = 0;
    size_t last_ws = 0;
    size_t last_we = 0;

    if (!word_start || !word_end) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (byte_offset > src_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (byte_offset == 0u) {
        *word_start = 0;
        *word_end = 0;
        return ROBO_OK;
    }

    robo_unicode_word_iter_init(&it, src, src_len, mode);
    while (it.byte_offset < byte_offset && it.valid) {
        last_ws = ws;
        last_we = we;
        if (robo_unicode_word_iter_next(&it, &ws, &we) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (we >= byte_offset) {
            if (ws < byte_offset) {
                *word_start = ws;
                *word_end = we;
            } else {
                *word_start = last_ws;
                *word_end = last_we;
            }
            return ROBO_OK;
        }
    }
    *word_start = last_ws;
    *word_end = last_we;
    return ROBO_OK;
}

robo_result_t robo_unicode_word_count_utf8(const char *src,
                                           size_t src_len,
                                           robo_unicode_mode_t mode,
                                           size_t *count)
{
    robo_unicode_word_iter_t it;
    size_t ws = 0;
    size_t we = 0;
    size_t n = 0;
    robo_result_t r;

    if (!count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *count = 0;
    if (src_len == 0u) {
        return ROBO_OK;
    }
    if (!src) {
        return ROBO_INVALID_ARGUMENT;
    }

    robo_unicode_word_iter_init(&it, src, src_len, mode);
    while (it.byte_offset < src_len && it.valid) {
        r = robo_unicode_word_iter_next(&it, &ws, &we);
        if (r != ROBO_OK) {
            return r;
        }
        if (ws >= src_len && we >= src_len) {
            break;
        }
        n++;
    }
    *count = n;
    return ROBO_OK;
}
