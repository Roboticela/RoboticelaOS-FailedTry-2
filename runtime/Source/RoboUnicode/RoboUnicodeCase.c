/*
 * Unicode case mapping / case folding — UCD 16.0.0 Default Case Algorithms.
 */

#include "RoboUnicode/RoboUnicodeCase.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

const char *robo_unicode_case_ucd_version_string(void)
{
    return ROBO_CASE_UCD_VERSION;
}

robo_uchar32_t robo_unicode_simple_toupper(robo_uchar32_t cp)
{
    return (robo_uchar32_t)robo_case_simple_upper(cp);
}

robo_uchar32_t robo_unicode_simple_tolower(robo_uchar32_t cp)
{
    return (robo_uchar32_t)robo_case_simple_lower(cp);
}

robo_uchar32_t robo_unicode_simple_totitle(robo_uchar32_t cp)
{
    return (robo_uchar32_t)robo_case_simple_title(cp);
}

robo_uchar32_t robo_unicode_simple_casefold(robo_uchar32_t cp)
{
    return (robo_uchar32_t)robo_case_simple_fold(cp);
}

size_t robo_unicode_case_map(robo_uchar32_t cp,
                             robo_unicode_case_form_t form,
                             robo_uchar32_t *out,
                             size_t out_cap)
{
    uint32_t tmp[ROBO_CASE_MAX_MAP];
    size_t n = 0;
    size_t i;

    if (!out || out_cap == 0u) {
        return 0;
    }

    switch (form) {
    case ROBO_UNICODE_CASE_SIMPLE_UPPER:
        out[0] = robo_unicode_simple_toupper(cp);
        return 1;
    case ROBO_UNICODE_CASE_SIMPLE_LOWER:
        out[0] = robo_unicode_simple_tolower(cp);
        return 1;
    case ROBO_UNICODE_CASE_SIMPLE_TITLE:
        out[0] = robo_unicode_simple_totitle(cp);
        return 1;
    case ROBO_UNICODE_CASE_SIMPLE_FOLD:
        out[0] = robo_unicode_simple_casefold(cp);
        return 1;
    case ROBO_UNICODE_CASE_UPPER:
        n = robo_case_full_upper(cp, tmp, ROBO_CASE_MAX_MAP);
        break;
    case ROBO_UNICODE_CASE_LOWER:
        n = robo_case_full_lower(cp, tmp, ROBO_CASE_MAX_MAP);
        break;
    case ROBO_UNICODE_CASE_TITLE:
        n = robo_case_full_title(cp, tmp, ROBO_CASE_MAX_MAP);
        break;
    case ROBO_UNICODE_CASE_FOLD:
        n = robo_case_full_fold(cp, tmp, ROBO_CASE_MAX_MAP);
        break;
    default:
        out[0] = cp;
        return 1;
    }
    if (n == 0u || n > out_cap) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        out[i] = (robo_uchar32_t)tmp[i];
    }
    return n;
}

size_t robo_unicode_to_upper(robo_uchar32_t cp, robo_uchar32_t *out, size_t out_cap)
{
    return robo_unicode_case_map(cp, ROBO_UNICODE_CASE_UPPER, out, out_cap);
}

size_t robo_unicode_to_lower(robo_uchar32_t cp, robo_uchar32_t *out, size_t out_cap)
{
    return robo_unicode_case_map(cp, ROBO_UNICODE_CASE_LOWER, out, out_cap);
}

size_t robo_unicode_to_title(robo_uchar32_t cp, robo_uchar32_t *out, size_t out_cap)
{
    return robo_unicode_case_map(cp, ROBO_UNICODE_CASE_TITLE, out, out_cap);
}

size_t robo_unicode_casefold(robo_uchar32_t cp, robo_uchar32_t *out, size_t out_cap)
{
    return robo_unicode_case_map(cp, ROBO_UNICODE_CASE_FOLD, out, out_cap);
}

/*
 * Final_Sigma (Unicode Default Case Algorithms):
 * C is a Final_Sigma context if:
 *   - C is preceded by a sequence matching \p{cased}(\p{case_ignorable})*
 *   - and C is not followed by a sequence matching (\p{case_ignorable})*\p{cased}
 */
static int robo_case_is_final_sigma_context(const robo_uchar32_t *cps,
                                            size_t len,
                                            size_t index)
{
    size_t i;
    int saw_cased;

    if (index >= len || cps[index] != 0x03A3u) {
        return 0;
    }

    /* Look behind: ... Cased Case_Ignorable*  (immediately before index) */
    saw_cased = 0;
    i = index;
    while (i > 0u) {
        i--;
        if (robo_case_is_case_ignorable(cps[i])) {
            continue;
        }
        if (robo_case_is_cased(cps[i])) {
            saw_cased = 1;
        }
        break;
    }
    if (!saw_cased) {
        return 0;
    }

    /* Look ahead: Case_Ignorable* Cased → NOT Final_Sigma */
    i = index + 1u;
    while (i < len) {
        if (robo_case_is_case_ignorable(cps[i])) {
            i++;
            continue;
        }
        if (robo_case_is_cased(cps[i])) {
            return 0;
        }
        break;
    }
    return 1;
}

robo_result_t robo_unicode_case_map_cps(const robo_uchar32_t *src,
                                        size_t src_len,
                                        robo_unicode_case_form_t form,
                                        robo_uchar32_t **out,
                                        size_t *out_len)
{
    robo_uchar32_t *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    size_t i;
    robo_uchar32_t mapped[ROBO_CASE_MAX_MAP];

    if (!out || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    *out_len = 0;
    if (src_len > 0u && !src) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (src_len == 0u) {
        return ROBO_OK;
    }

    cap = src_len;
    buf = (robo_uchar32_t *)robo_malloc(cap * sizeof(robo_uchar32_t));
    if (!buf) {
        return ROBO_OUT_OF_MEMORY;
    }

    for (i = 0; i < src_len; i++) {
        size_t n;
        size_t j;
        if (form == ROBO_UNICODE_CASE_LOWER && src[i] == 0x03A3u &&
            robo_case_is_final_sigma_context(src, src_len, i)) {
            n = robo_case_final_sigma_lower((uint32_t *)mapped, ROBO_CASE_MAX_MAP);
        } else {
            n = robo_unicode_case_map(src[i], form, mapped, ROBO_CASE_MAX_MAP);
        }
        if (n == 0u) {
            robo_free(buf);
            return ROBO_ERROR;
        }
        if (len + n > cap) {
            size_t ncap = cap * 2u;
            robo_uchar32_t *nb;
            while (ncap < len + n) {
                if (ncap > ((size_t)-1) / 2u) {
                    robo_free(buf);
                    return ROBO_OUT_OF_MEMORY;
                }
                ncap *= 2u;
            }
            nb = (robo_uchar32_t *)robo_malloc(ncap * sizeof(robo_uchar32_t));
            if (!nb) {
                robo_free(buf);
                return ROBO_OUT_OF_MEMORY;
            }
            memcpy(nb, buf, len * sizeof(robo_uchar32_t));
            robo_free(buf);
            buf = nb;
            cap = ncap;
        }
        for (j = 0; j < n; j++) {
            buf[len++] = mapped[j];
        }
    }

    *out = buf;
    *out_len = len;
    return ROBO_OK;
}

static robo_result_t robo_case_utf8_encode_cps(const robo_uchar32_t *cps,
                                               size_t cps_len,
                                               char **out,
                                               size_t *out_len)
{
    size_t need = 0;
    size_t i;
    size_t w = 0;
    char *utf8;

    for (i = 0; i < cps_len; i++) {
        size_t n = robo_unicode_utf8_encode_len(cps[i]);
        if (n == 0u) {
            n = robo_unicode_utf8_encode_len(ROBO_UCHAR32_REPLACEMENT);
        }
        need += n;
    }
    utf8 = (char *)robo_malloc(need + 1u);
    if (!utf8) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0; i < cps_len; i++) {
        size_t written = 0;
        robo_unicode_status_t st =
            robo_unicode_utf8_encode(cps[i], utf8 + w, need + 1u - w, &written);
        if (st != ROBO_UNICODE_OK) {
            st = robo_unicode_utf8_encode(
                ROBO_UCHAR32_REPLACEMENT, utf8 + w, need + 1u - w, &written);
            if (st != ROBO_UNICODE_OK) {
                robo_free(utf8);
                return ROBO_ERROR;
            }
        }
        w += written;
    }
    utf8[w] = '\0';
    *out = utf8;
    *out_len = w;
    return ROBO_OK;
}

robo_result_t robo_unicode_case_map_utf8(const char *src,
                                         size_t src_len,
                                         robo_unicode_case_form_t form,
                                         robo_unicode_mode_t mode,
                                         char **out,
                                         size_t *out_len)
{
    robo_uchar32_t *cps = NULL;
    size_t cps_cap = 0;
    size_t cps_len = 0;
    size_t off = 0;
    robo_uchar32_t *mapped = NULL;
    size_t mapped_len = 0;
    robo_result_t r;

    if (!out || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    *out_len = 0;
    if (src_len > 0u && !src) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (src_len == 0u) {
        char *empty = (char *)robo_malloc(1u);
        if (!empty) {
            return ROBO_OUT_OF_MEMORY;
        }
        empty[0] = '\0';
        *out = empty;
        *out_len = 0;
        return ROBO_OK;
    }

    /* Fast path: simple forms on pure ASCII for upper/lower/fold */
    if (form == ROBO_UNICODE_CASE_SIMPLE_UPPER ||
        form == ROBO_UNICODE_CASE_SIMPLE_LOWER ||
        form == ROBO_UNICODE_CASE_SIMPLE_FOLD ||
        form == ROBO_UNICODE_CASE_UPPER ||
        form == ROBO_UNICODE_CASE_LOWER ||
        form == ROBO_UNICODE_CASE_FOLD) {
        size_t i;
        int ascii = 1;
        for (i = 0; i < src_len; i++) {
            if ((unsigned char)src[i] >= 0x80u) {
                ascii = 0;
                break;
            }
        }
        if (ascii && (form == ROBO_UNICODE_CASE_SIMPLE_UPPER ||
                      form == ROBO_UNICODE_CASE_UPPER ||
                      form == ROBO_UNICODE_CASE_SIMPLE_LOWER ||
                      form == ROBO_UNICODE_CASE_LOWER ||
                      form == ROBO_UNICODE_CASE_SIMPLE_FOLD ||
                      form == ROBO_UNICODE_CASE_FOLD)) {
            char *buf = (char *)robo_malloc(src_len + 1u);
            if (!buf) {
                return ROBO_OUT_OF_MEMORY;
            }
            for (i = 0; i < src_len; i++) {
                unsigned char ch = (unsigned char)src[i];
                if (form == ROBO_UNICODE_CASE_SIMPLE_UPPER ||
                    form == ROBO_UNICODE_CASE_UPPER) {
                    if (ch >= 'a' && ch <= 'z') {
                        ch = (unsigned char)(ch - 32u);
                    }
                } else {
                    /* lower / fold ASCII: A-Z → a-z */
                    if (ch >= 'A' && ch <= 'Z') {
                        ch = (unsigned char)(ch + 32u);
                    }
                }
                buf[i] = (char)ch;
            }
            buf[src_len] = '\0';
            *out = buf;
            *out_len = src_len;
            return ROBO_OK;
        }
    }

    cps_cap = src_len;
    cps = (robo_uchar32_t *)robo_malloc(cps_cap * sizeof(robo_uchar32_t));
    if (!cps) {
        return ROBO_OUT_OF_MEMORY;
    }

    while (off < src_len) {
        robo_uchar32_t cp = 0;
        size_t next = off;
        robo_unicode_status_t st =
            robo_unicode_utf8_decode(src, src_len, off, mode, &cp, &next);
        if (st != ROBO_UNICODE_OK) {
            robo_free(cps);
            return ROBO_PARSE_ERROR;
        }
        if (cps_len >= cps_cap) {
            size_t ncap = cps_cap * 2u;
            robo_uchar32_t *nb =
                (robo_uchar32_t *)robo_malloc(ncap * sizeof(robo_uchar32_t));
            if (!nb) {
                robo_free(cps);
                return ROBO_OUT_OF_MEMORY;
            }
            memcpy(nb, cps, cps_len * sizeof(robo_uchar32_t));
            robo_free(cps);
            cps = nb;
            cps_cap = ncap;
        }
        cps[cps_len++] = cp;
        off = next;
    }

    r = robo_unicode_case_map_cps(cps, cps_len, form, &mapped, &mapped_len);
    robo_free(cps);
    if (r != ROBO_OK) {
        return r;
    }

    if (!mapped || mapped_len == 0u) {
        char *empty = (char *)robo_malloc(1u);
        if (mapped) {
            robo_free(mapped);
        }
        if (!empty) {
            return ROBO_OUT_OF_MEMORY;
        }
        empty[0] = '\0';
        *out = empty;
        *out_len = 0;
        return ROBO_OK;
    }

    r = robo_case_utf8_encode_cps(mapped, mapped_len, out, out_len);
    robo_free(mapped);
    return r;
}

robo_result_t robo_unicode_to_upper_utf8(const char *src, size_t src_len,
                                         robo_unicode_mode_t mode,
                                         char **out, size_t *out_len)
{
    return robo_unicode_case_map_utf8(
        src, src_len, ROBO_UNICODE_CASE_UPPER, mode, out, out_len);
}

robo_result_t robo_unicode_to_lower_utf8(const char *src, size_t src_len,
                                         robo_unicode_mode_t mode,
                                         char **out, size_t *out_len)
{
    return robo_unicode_case_map_utf8(
        src, src_len, ROBO_UNICODE_CASE_LOWER, mode, out, out_len);
}

robo_result_t robo_unicode_to_title_utf8(const char *src, size_t src_len,
                                         robo_unicode_mode_t mode,
                                         char **out, size_t *out_len)
{
    return robo_unicode_case_map_utf8(
        src, src_len, ROBO_UNICODE_CASE_TITLE, mode, out, out_len);
}

robo_result_t robo_unicode_casefold_utf8(const char *src, size_t src_len,
                                         robo_unicode_mode_t mode,
                                         char **out, size_t *out_len)
{
    return robo_unicode_case_map_utf8(
        src, src_len, ROBO_UNICODE_CASE_FOLD, mode, out, out_len);
}

robo_result_t robo_unicode_casefold_compare_utf8(const char *a, size_t a_len,
                                                 const char *b, size_t b_len,
                                                 robo_unicode_mode_t mode,
                                                 int *cmp)
{
    char *fa = NULL;
    char *fb = NULL;
    size_t fa_len = 0;
    size_t fb_len = 0;
    size_t n;
    robo_result_t r;

    if (!cmp) {
        return ROBO_INVALID_ARGUMENT;
    }
    *cmp = 0;

    r = robo_unicode_casefold_utf8(a, a_len, mode, &fa, &fa_len);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_unicode_casefold_utf8(b, b_len, mode, &fb, &fb_len);
    if (r != ROBO_OK) {
        robo_free(fa);
        return r;
    }

    n = fa_len < fb_len ? fa_len : fb_len;
    if (n > 0u) {
        *cmp = memcmp(fa, fb, n);
    }
    if (*cmp == 0) {
        if (fa_len < fb_len) {
            *cmp = -1;
        } else if (fa_len > fb_len) {
            *cmp = 1;
        }
    }
    robo_free(fa);
    robo_free(fb);
    return ROBO_OK;
}
