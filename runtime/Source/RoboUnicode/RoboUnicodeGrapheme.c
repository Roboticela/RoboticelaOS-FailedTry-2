/*
 * Extended Grapheme Cluster boundaries — UAX #29 (Unicode 16.0.0).
 */

#include "RoboUnicode/RoboUnicodeGrapheme.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <string.h>

const char *robo_unicode_grapheme_ucd_version_string(void)
{
    return ROBO_GRAPHEME_UCD_VERSION;
}

static int robo_grapheme_is_ext_pict(robo_uchar32_t cp)
{
    return robo_unicode_is_extended_pictographic(cp);
}

static int robo_grapheme_incb_is_extend_or_linker(robo_unicode_incb_t v)
{
    return v == ROBO_U_INCB_EXTEND || v == ROBO_U_INCB_LINKER;
}

static int robo_grapheme_gb9c_no_break(const robo_uchar32_t *cps, size_t index)
{
    size_t j;
    size_t k;

    if (index == 0 || robo_grapheme_incb(cps[index]) != ROBO_U_INCB_CONSONANT) {
        return 0;
    }
    for (j = index - 1u; ; j--) {
        robo_unicode_incb_t v = robo_grapheme_incb(cps[j]);
        if (!robo_grapheme_incb_is_extend_or_linker(v)) {
            return 0;
        }
        if (v == ROBO_U_INCB_LINKER) {
            k = j;
            while (k > 0 &&
                   robo_grapheme_incb_is_extend_or_linker(robo_grapheme_incb(cps[k - 1u]))) {
                k--;
            }
            if (k > 0 && robo_grapheme_incb(cps[k - 1u]) == ROBO_U_INCB_CONSONANT) {
                return 1;
            }
        }
        if (j == 0u) {
            return 0;
        }
    }
}

static int robo_grapheme_gb11_no_break(const robo_uchar32_t *cps, size_t index)
{
    size_t j;

    if (!robo_grapheme_is_ext_pict(cps[index]) || index == 0) {
        return 0;
    }
    j = index - 1u;
    if (robo_grapheme_break_property(cps[j]) != ROBO_U_GB_ZWJ) {
        return 0;
    }
    if (j == 0) {
        return 0;
    }
    j--;
    while (j > 0 && robo_grapheme_break_property(cps[j]) == ROBO_U_GB_EXTEND) {
        j--;
    }
    return robo_grapheme_is_ext_pict(cps[j]);
}

static int robo_grapheme_gb_ri_no_break(const robo_uchar32_t *cps, size_t index)
{
    size_t j;
    size_t ri_run;

    if (index == 0) {
        return 0;
    }
    if (robo_grapheme_break_property(cps[index - 1u]) != ROBO_U_GB_REGIONAL_INDICATOR ||
        robo_grapheme_break_property(cps[index]) != ROBO_U_GB_REGIONAL_INDICATOR) {
        return 0;
    }
    j = index - 1u;
    while (j > 0 && robo_grapheme_break_property(cps[j - 1u]) == ROBO_U_GB_REGIONAL_INDICATOR) {
        j--;
    }
    ri_run = index - j;
    return (ri_run % 2u) == 1u;
}

int robo_unicode_grapheme_break(const robo_uchar32_t *cps, size_t len, size_t index)
{
    robo_unicode_grapheme_break_t l;
    robo_unicode_grapheme_break_t r;

    if (index == 0u) {
        return 1;
    }
    if (index >= len || !cps) {
        return 1;
    }

    l = robo_grapheme_break_property(cps[index - 1u]);
    r = robo_grapheme_break_property(cps[index]);

    if (l == ROBO_U_GB_CR && r == ROBO_U_GB_LF) {
        return 0;
    }
    if (l == ROBO_U_GB_CONTROL || l == ROBO_U_GB_CR || l == ROBO_U_GB_LF) {
        return 1;
    }
    if (r == ROBO_U_GB_CONTROL || r == ROBO_U_GB_CR || r == ROBO_U_GB_LF) {
        return 1;
    }
    if (l == ROBO_U_GB_L &&
        (r == ROBO_U_GB_L || r == ROBO_U_GB_V || r == ROBO_U_GB_LV || r == ROBO_U_GB_LVT)) {
        return 0;
    }
    if ((l == ROBO_U_GB_LV || l == ROBO_U_GB_V) && (r == ROBO_U_GB_V || r == ROBO_U_GB_T)) {
        return 0;
    }
    if ((l == ROBO_U_GB_LVT || l == ROBO_U_GB_T) && r == ROBO_U_GB_T) {
        return 0;
    }
    if (r == ROBO_U_GB_EXTEND || r == ROBO_U_GB_ZWJ) {
        return 0;
    }
    if (r == ROBO_U_GB_SPACINGMARK) {
        return 0;
    }
    if (l == ROBO_U_GB_PREPEND) {
        return 0;
    }
    if (robo_grapheme_gb9c_no_break(cps, index)) {
        return 0;
    }
    if (robo_grapheme_gb11_no_break(cps, index)) {
        return 0;
    }
    if (robo_grapheme_gb_ri_no_break(cps, index)) {
        return 0;
    }
    return 1;
}

static robo_result_t robo_grapheme_decode_at(const char *src,
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

static robo_result_t robo_grapheme_next_cluster(const char *src,
                                                size_t src_len,
                                                size_t byte_offset,
                                                robo_unicode_mode_t mode,
                                                size_t *cluster_start,
                                                size_t *cluster_end)
{
    robo_uchar32_t stack_cps[48];
    size_t stack_ends[48];
    robo_uchar32_t *cps = stack_cps;
    size_t *cp_ends = stack_ends;
    size_t cap = 48u;
    size_t n = 0;
    size_t off = byte_offset;
    robo_result_t r;

    if (byte_offset >= src_len) {
        *cluster_start = src_len;
        *cluster_end = src_len;
        return ROBO_OK;
    }

    *cluster_start = byte_offset;

    for (;;) {
        robo_uchar32_t cp = 0;
        size_t bend = off;

        if (off >= src_len) {
            break;
        }
        r = robo_grapheme_decode_at(src, src_len, off, mode, &cp, &bend);
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
            cps[n] = cp;
            if (robo_unicode_grapheme_break(cps, n + 1u, n)) {
                break;
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
            if (n) {
                memcpy(nc, cps, n * sizeof(robo_uchar32_t));
                memcpy(ne, cp_ends, n * sizeof(size_t));
            }
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

    if (n == 0u) {
        *cluster_end = byte_offset;
    } else {
        *cluster_end = cp_ends[n - 1u];
    }
    if (cps != stack_cps) {
        robo_free(cps);
    }
    if (cp_ends != stack_ends) {
        robo_free(cp_ends);
    }
    return ROBO_OK;
}

robo_result_t robo_unicode_grapheme_iter_init(robo_unicode_grapheme_iter_t *it,
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
    it->cluster_index = 0;
    it->valid = 1;
    return ROBO_OK;
}

robo_result_t robo_unicode_grapheme_iter_next(robo_unicode_grapheme_iter_t *it,
                                              size_t *cluster_start,
                                              size_t *cluster_end)
{
    robo_result_t r;

    if (!it || !cluster_start || !cluster_end) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!it->valid) {
        return ROBO_PARSE_ERROR;
    }

    r = robo_grapheme_next_cluster(
        it->src, it->src_len, it->byte_offset, it->mode, cluster_start, cluster_end);
    if (r != ROBO_OK) {
        it->valid = 0;
        return r;
    }
    it->byte_offset = *cluster_end;
    if (*cluster_start < it->src_len || *cluster_end <= it->src_len) {
        if (*cluster_start != *cluster_end || *cluster_start < it->src_len) {
            it->cluster_index++;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_unicode_grapheme_iter_prev(const char *src,
                                              size_t src_len,
                                              size_t byte_offset,
                                              robo_unicode_mode_t mode,
                                              size_t *cluster_start,
                                              size_t *cluster_end)
{
    robo_unicode_grapheme_iter_t it;
    size_t cs = 0;
    size_t ce = 0;
    size_t last_cs = 0;
    size_t last_ce = 0;

    if (!cluster_start || !cluster_end) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (byte_offset > src_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (byte_offset == 0u) {
        *cluster_start = 0;
        *cluster_end = 0;
        return ROBO_OK;
    }

    robo_unicode_grapheme_iter_init(&it, src, src_len, mode);
    while (it.byte_offset < byte_offset && it.valid) {
        last_cs = cs;
        last_ce = ce;
        if (robo_unicode_grapheme_iter_next(&it, &cs, &ce) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (ce >= byte_offset) {
            if (cs < byte_offset) {
                *cluster_start = cs;
                *cluster_end = ce;
            } else {
                *cluster_start = last_cs;
                *cluster_end = last_ce;
            }
            return ROBO_OK;
        }
    }
    *cluster_start = last_cs;
    *cluster_end = last_ce;
    return ROBO_OK;
}

robo_result_t robo_unicode_grapheme_count_utf8(const char *src,
                                               size_t src_len,
                                               robo_unicode_mode_t mode,
                                               size_t *count)
{
    robo_unicode_grapheme_iter_t it;
    size_t cs = 0;
    size_t ce = 0;
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

    robo_unicode_grapheme_iter_init(&it, src, src_len, mode);
    while (it.byte_offset < src_len && it.valid) {
        r = robo_unicode_grapheme_iter_next(&it, &cs, &ce);
        if (r != ROBO_OK) {
            return r;
        }
        if (cs >= src_len && ce >= src_len) {
            break;
        }
        n++;
    }
    *count = n;
    return ROBO_OK;
}

robo_result_t robo_unicode_grapheme_next(const char *src,
                                         size_t src_len,
                                         size_t byte_offset,
                                         robo_unicode_mode_t mode,
                                         size_t *cluster_start,
                                         size_t *cluster_end)
{
    if (byte_offset > src_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_grapheme_next_cluster(
        src, src_len, byte_offset, mode, cluster_start, cluster_end);
}

robo_result_t robo_unicode_grapheme_prev(const char *src,
                                         size_t src_len,
                                         size_t byte_offset,
                                         robo_unicode_mode_t mode,
                                         size_t *cluster_start,
                                         size_t *cluster_end)
{
    return robo_unicode_grapheme_iter_prev(
        src, src_len, byte_offset, mode, cluster_start, cluster_end);
}
