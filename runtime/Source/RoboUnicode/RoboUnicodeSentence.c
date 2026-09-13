/*
 * Unicode Sentence Boundaries — UAX #29 (Unicode 16.0.0).
 */

#include "RoboUnicode/RoboUnicodeSentence.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

const char *robo_unicode_sentence_ucd_version_string(void)
{
    return ROBO_SENTENCE_UCD_VERSION;
}

static int robo_sb_is_ignore(robo_unicode_sentence_break_t p)
{
    return p == ROBO_U_SB_EXTEND || p == ROBO_U_SB_FORMAT;
}

static int robo_sb_is_parasep(robo_unicode_sentence_break_t p)
{
    return p == ROBO_U_SB_SEP || p == ROBO_U_SB_CR || p == ROBO_U_SB_LF;
}

static int robo_sb_is_saterm(robo_unicode_sentence_break_t p)
{
    return p == ROBO_U_SB_STERM || p == ROBO_U_SB_ATERM;
}

/*
 * UAX #29: skip Extend|Format except after sot / ParaSep.
 * Returns 1 if @p index (Extend|Format) should be skipped for rule matching.
 */
static int robo_sb_should_skip_ignore(const robo_uchar32_t *cps, size_t index)
{
    size_t j = index;
    robo_unicode_sentence_break_t p;

    while (j > 0u) {
        j--;
        p = robo_sentence_break_property(cps[j]);
        if (robo_sb_is_parasep(p)) {
            return 0; /* after ParaSep — do not ignore */
        }
        if (robo_sb_is_ignore(p)) {
            continue;
        }
        return 1; /* preceded by real content — ignore */
    }
    return 0; /* after sot — do not ignore */
}

/* Previous rule-relevant index strictly before @p pos, or (size_t)-1. */
static size_t robo_sb_prev(const robo_uchar32_t *cps, size_t pos)
{
    size_t i;
    if (pos == 0u) {
        return (size_t)-1;
    }
    i = pos;
    do {
        i--;
        if (!robo_sb_is_ignore(robo_sentence_break_property(cps[i]))) {
            return i;
        }
        if (!robo_sb_should_skip_ignore(cps, i)) {
            return i;
        }
    } while (i > 0u);
    return (size_t)-1;
}

/* Next rule-relevant index at or after @p pos, or len if none. */
static size_t robo_sb_next(const robo_uchar32_t *cps, size_t len, size_t pos)
{
    size_t i = pos;
    while (i < len) {
        robo_unicode_sentence_break_t p = robo_sentence_break_property(cps[i]);
        if (!robo_sb_is_ignore(p)) {
            return i;
        }
        if (!robo_sb_should_skip_ignore(cps, i)) {
            return i;
        }
        i++;
    }
    return len;
}

/*
 * Match ending before @p end: SATerm Close* Sp*  (no trailing ParaSep).
 * Returns index of the SATerm on success, or (size_t)-1.
 */
static size_t robo_sb_match_saterm_close_sp(const robo_uchar32_t *cps, size_t end)
{
    size_t i = robo_sb_prev(cps, end);
    robo_unicode_sentence_break_t p;

    if (i == (size_t)-1) {
        return (size_t)-1;
    }

    /* Sp* */
    while (i != (size_t)-1) {
        p = robo_sentence_break_property(cps[i]);
        if (p != ROBO_U_SB_SP) {
            break;
        }
        i = robo_sb_prev(cps, i);
    }
    if (i == (size_t)-1) {
        return (size_t)-1;
    }

    /* Close* */
    while (i != (size_t)-1) {
        p = robo_sentence_break_property(cps[i]);
        if (p != ROBO_U_SB_CLOSE) {
            break;
        }
        i = robo_sb_prev(cps, i);
    }
    if (i == (size_t)-1) {
        return (size_t)-1;
    }

    p = robo_sentence_break_property(cps[i]);
    if (!robo_sb_is_saterm(p)) {
        return (size_t)-1;
    }
    return i;
}

/* Like above but requires ATerm specifically (SB8). */
static size_t robo_sb_match_aterm_close_sp(const robo_uchar32_t *cps, size_t end)
{
    size_t a = robo_sb_match_saterm_close_sp(cps, end);
    if (a == (size_t)-1) {
        return (size_t)-1;
    }
    if (robo_sentence_break_property(cps[a]) != ROBO_U_SB_ATERM) {
        return (size_t)-1;
    }
    return a;
}

/* Match SATerm Close* ending before @p end (no Sp). Returns SATerm index. */
static size_t robo_sb_match_saterm_close(const robo_uchar32_t *cps, size_t end)
{
    size_t i = robo_sb_prev(cps, end);
    robo_unicode_sentence_break_t p;

    if (i == (size_t)-1) {
        return (size_t)-1;
    }

    p = robo_sentence_break_property(cps[i]);
    if (p == ROBO_U_SB_SP || robo_sb_is_parasep(p)) {
        return (size_t)-1;
    }

    while (i != (size_t)-1) {
        p = robo_sentence_break_property(cps[i]);
        if (p != ROBO_U_SB_CLOSE) {
            break;
        }
        i = robo_sb_prev(cps, i);
    }
    if (i == (size_t)-1) {
        return (size_t)-1;
    }
    p = robo_sentence_break_property(cps[i]);
    if (!robo_sb_is_saterm(p)) {
        return (size_t)-1;
    }
    return i;
}

/* SATerm Close* Sp* ParaSep? ending before @p end (SB11 left side). */
static int robo_sb_match_sb11(const robo_uchar32_t *cps, size_t end)
{
    size_t i = robo_sb_prev(cps, end);
    robo_unicode_sentence_break_t p;

    if (i == (size_t)-1) {
        return 0;
    }

    p = robo_sentence_break_property(cps[i]);
    if (robo_sb_is_parasep(p)) {
        /* Optional ParaSep: require SATerm Close* Sp* immediately before it. */
        return robo_sb_match_saterm_close_sp(cps, i) != (size_t)-1;
    }

    return robo_sb_match_saterm_close_sp(cps, end) != (size_t)-1;
}

/* SB8 right: ( ¬(OLetter | Upper | Lower | ParaSep | SATerm) )* Lower */
static int robo_sb_match_sb8_right(const robo_uchar32_t *cps, size_t len, size_t index)
{
    size_t j = index;
    robo_unicode_sentence_break_t p;

    while (j < len) {
        j = robo_sb_next(cps, len, j);
        if (j >= len) {
            return 0;
        }
        p = robo_sentence_break_property(cps[j]);
        if (p == ROBO_U_SB_LOWER) {
            return 1;
        }
        if (p == ROBO_U_SB_OLETTER || p == ROBO_U_SB_UPPER || p == ROBO_U_SB_LOWER ||
            robo_sb_is_parasep(p) || robo_sb_is_saterm(p)) {
            return 0;
        }
        j++;
    }
    return 0;
}

int robo_unicode_sentence_break(const robo_uchar32_t *cps, size_t len, size_t index)
{
    robo_unicode_sentence_break_t lb;
    robo_unicode_sentence_break_t rb;
    size_t li;
    size_t p1;

    if (index == 0u) {
        return 1; /* SB1 */
    }
    if (index >= len || !cps) {
        return 1; /* SB2 */
    }

    lb = robo_sentence_break_property(cps[index - 1u]);
    rb = robo_sentence_break_property(cps[index]);

    /* SB3 */
    if (lb == ROBO_U_SB_CR && rb == ROBO_U_SB_LF) {
        return 0;
    }
    /* SB4 */
    if (robo_sb_is_parasep(lb)) {
        return 1;
    }
    /* SB5 */
    if (robo_sb_is_ignore(rb)) {
        return 0;
    }

    li = robo_sb_prev(cps, index);
    if (li == (size_t)-1) {
        /* Only ignores between sot and here — keep with following via SB998. */
        return 0;
    }
    lb = robo_sentence_break_property(cps[li]);
    rb = robo_sentence_break_property(cps[index]);

    /* SB6: ATerm × Numeric */
    if (lb == ROBO_U_SB_ATERM && rb == ROBO_U_SB_NUMERIC) {
        return 0;
    }

    /* SB7: (Upper | Lower) ATerm × Upper */
    if (rb == ROBO_U_SB_UPPER && lb == ROBO_U_SB_ATERM) {
        p1 = robo_sb_prev(cps, li);
        if (p1 != (size_t)-1) {
            robo_unicode_sentence_break_t pp = robo_sentence_break_property(cps[p1]);
            if (pp == ROBO_U_SB_UPPER || pp == ROBO_U_SB_LOWER) {
                return 0;
            }
        }
    }

    /* SB8: ATerm Close* Sp* × (...)* Lower */
    if (robo_sb_match_aterm_close_sp(cps, index) != (size_t)-1) {
        if (robo_sb_match_sb8_right(cps, len, index)) {
            return 0;
        }
    }

    /* SB8a: SATerm Close* Sp* × (SContinue | SATerm) */
    if (robo_sb_match_saterm_close_sp(cps, index) != (size_t)-1) {
        if (rb == ROBO_U_SB_SCONTINUE || robo_sb_is_saterm(rb)) {
            return 0;
        }
    }

    /* SB9: SATerm Close* × (Close | Sp | ParaSep) */
    if (robo_sb_match_saterm_close(cps, index) != (size_t)-1) {
        if (rb == ROBO_U_SB_CLOSE || rb == ROBO_U_SB_SP || robo_sb_is_parasep(rb)) {
            return 0;
        }
    }

    /* SB10: SATerm Close* Sp* × (Sp | ParaSep) */
    if (robo_sb_match_saterm_close_sp(cps, index) != (size_t)-1) {
        if (rb == ROBO_U_SB_SP || robo_sb_is_parasep(rb)) {
            return 0;
        }
    }

    /* SB11: SATerm Close* Sp* ParaSep? ÷ */
    if (robo_sb_match_sb11(cps, index)) {
        return 1;
    }

    /* SB998 */
    return 0;
}

static robo_result_t robo_sentence_decode_at(const char *src,
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

static robo_result_t robo_sentence_next_segment(const char *src,
                                                size_t src_len,
                                                size_t byte_offset,
                                                robo_unicode_mode_t mode,
                                                size_t *sentence_start,
                                                size_t *sentence_end)
{
    robo_uchar32_t stack_cps[128];
    size_t stack_ends[128];
    robo_uchar32_t *cps = stack_cps;
    size_t *cp_ends = stack_ends;
    size_t cap = 128u;
    size_t n = 0;
    size_t off = byte_offset;
    robo_result_t r;

    if (byte_offset >= src_len) {
        *sentence_start = src_len;
        *sentence_end = src_len;
        return ROBO_OK;
    }
    *sentence_start = byte_offset;

    for (;;) {
        robo_uchar32_t cp = 0;
        size_t bend = off;
        size_t peek_n;
        size_t peek_off;
        size_t i;

        if (off >= src_len) {
            break;
        }
        r = robo_sentence_decode_at(src, src_len, off, mode, &cp, &bend);
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
             * SB8 may need look-ahead past the candidate boundary until Lower
             * or a stopping class. Peek a window of following scalars.
             */
            robo_uchar32_t peek_stack[256];
            robo_uchar32_t *peek = peek_stack;
            size_t peek_cap = 256u;
            int heap_peek = 0;
            const size_t look_ahead = 64u;

            if (n + 1u + look_ahead > peek_cap) {
                peek_cap = n + 1u + look_ahead + 32u;
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
            while (peek_n < n + 1u + look_ahead && peek_off < src_len) {
                robo_uchar32_t pcp = 0;
                size_t pend = peek_off;
                r = robo_sentence_decode_at(src, src_len, peek_off, mode, &pcp, &pend);
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
            if (robo_unicode_sentence_break(peek, peek_n, n)) {
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

    *sentence_end = (n == 0u) ? byte_offset : cp_ends[n - 1u];
    if (cps != stack_cps) {
        robo_free(cps);
    }
    if (cp_ends != stack_ends) {
        robo_free(cp_ends);
    }
    return ROBO_OK;
}

robo_result_t robo_unicode_sentence_iter_init(robo_unicode_sentence_iter_t *it,
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
    it->sentence_index = 0;
    it->valid = 1;
    return ROBO_OK;
}

robo_result_t robo_unicode_sentence_iter_next(robo_unicode_sentence_iter_t *it,
                                              size_t *sentence_start,
                                              size_t *sentence_end)
{
    robo_result_t r;

    if (!it || !sentence_start || !sentence_end) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!it->valid) {
        return ROBO_PARSE_ERROR;
    }
    r = robo_sentence_next_segment(
        it->src, it->src_len, it->byte_offset, it->mode, sentence_start, sentence_end);
    if (r != ROBO_OK) {
        it->valid = 0;
        return r;
    }
    it->byte_offset = *sentence_end;
    if (*sentence_start < it->src_len && *sentence_start != *sentence_end) {
        it->sentence_index++;
    }
    return ROBO_OK;
}

robo_result_t robo_unicode_sentence_next(const char *src,
                                         size_t src_len,
                                         size_t byte_offset,
                                         robo_unicode_mode_t mode,
                                         size_t *sentence_start,
                                         size_t *sentence_end)
{
    if (byte_offset > src_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_sentence_next_segment(
        src, src_len, byte_offset, mode, sentence_start, sentence_end);
}

robo_result_t robo_unicode_sentence_prev(const char *src,
                                         size_t src_len,
                                         size_t byte_offset,
                                         robo_unicode_mode_t mode,
                                         size_t *sentence_start,
                                         size_t *sentence_end)
{
    robo_unicode_sentence_iter_t it;
    size_t ss = 0;
    size_t se = 0;
    size_t last_ss = 0;
    size_t last_se = 0;

    if (!sentence_start || !sentence_end) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (byte_offset > src_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (byte_offset == 0u) {
        *sentence_start = 0;
        *sentence_end = 0;
        return ROBO_OK;
    }

    robo_unicode_sentence_iter_init(&it, src, src_len, mode);
    while (it.byte_offset < byte_offset && it.valid) {
        last_ss = ss;
        last_se = se;
        if (robo_unicode_sentence_iter_next(&it, &ss, &se) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (se >= byte_offset) {
            if (ss < byte_offset) {
                *sentence_start = ss;
                *sentence_end = se;
            } else {
                *sentence_start = last_ss;
                *sentence_end = last_se;
            }
            return ROBO_OK;
        }
    }
    *sentence_start = last_ss;
    *sentence_end = last_se;
    return ROBO_OK;
}

robo_result_t robo_unicode_sentence_count_utf8(const char *src,
                                               size_t src_len,
                                               robo_unicode_mode_t mode,
                                               size_t *count)
{
    robo_unicode_sentence_iter_t it;
    size_t ss = 0;
    size_t se = 0;
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

    robo_unicode_sentence_iter_init(&it, src, src_len, mode);
    while (it.byte_offset < src_len && it.valid) {
        r = robo_unicode_sentence_iter_next(&it, &ss, &se);
        if (r != ROBO_OK) {
            return r;
        }
        if (ss >= src_len && se >= src_len) {
            break;
        }
        n++;
    }
    *count = n;
    return ROBO_OK;
}
