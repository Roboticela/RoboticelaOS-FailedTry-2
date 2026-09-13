/*
 * Unicode Normalization Forms — UAX #15 / Unicode Standard Ch. 3.
 * Data: generated RoboNormData (UCD 16.0.0). Hangul is algorithmic.
 */

#include "RoboUnicode/RoboUnicodeNorm.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboNormData.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <string.h>

enum {
    ROBO_HANGUL_SBASE = 0xAC00,
    ROBO_HANGUL_LBASE = 0x1100,
    ROBO_HANGUL_VBASE = 0x1161,
    ROBO_HANGUL_TBASE = 0x11A7,
    ROBO_HANGUL_LCOUNT = 19,
    ROBO_HANGUL_VCOUNT = 21,
    ROBO_HANGUL_TCOUNT = 28,
    ROBO_HANGUL_NCOUNT = ROBO_HANGUL_VCOUNT * ROBO_HANGUL_TCOUNT,
    ROBO_HANGUL_SCOUNT = ROBO_HANGUL_LCOUNT * ROBO_HANGUL_NCOUNT
};

typedef struct robo_norm_buf {
    robo_uchar32_t *data;
    size_t len;
    size_t cap;
} robo_norm_buf_t;

const char *robo_unicode_norm_ucd_version_string(void)
{
    return ROBO_NORM_UCD_VERSION;
}

static int robo_norm_is_ascii_utf8(const char *src, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if ((unsigned char)src[i] >= 0x80u) {
            return 0;
        }
    }
    return 1;
}

static robo_result_t robo_norm_buf_reserve(robo_norm_buf_t *b, size_t need)
{
    robo_uchar32_t *n;
    size_t ncap;
    if (need <= b->cap) {
        return ROBO_OK;
    }
    ncap = b->cap ? b->cap : 8u;
    while (ncap < need) {
        if (ncap > ((size_t)-1) / 2u) {
            return ROBO_OUT_OF_MEMORY;
        }
        ncap *= 2u;
    }
    n = (robo_uchar32_t *)robo_malloc(ncap * sizeof(robo_uchar32_t));
    if (!n) {
        return ROBO_OUT_OF_MEMORY;
    }
    if (b->data && b->len) {
        memcpy(n, b->data, b->len * sizeof(robo_uchar32_t));
    }
    if (b->data) {
        robo_free(b->data);
    }
    b->data = n;
    b->cap = ncap;
    return ROBO_OK;
}

static robo_result_t robo_norm_buf_push(robo_norm_buf_t *b, robo_uchar32_t cp)
{
    robo_result_t r = robo_norm_buf_reserve(b, b->len + 1u);
    if (r != ROBO_OK) {
        return r;
    }
    b->data[b->len++] = cp;
    return ROBO_OK;
}

static int robo_norm_hangul_decompose(robo_uchar32_t cp,
                                      robo_uchar32_t out[3],
                                      size_t *out_len)
{
    unsigned sindex;
    unsigned tindex;
    if (cp < (robo_uchar32_t)ROBO_HANGUL_SBASE ||
        cp >= (robo_uchar32_t)(ROBO_HANGUL_SBASE + ROBO_HANGUL_SCOUNT)) {
        return 0;
    }
    sindex = (unsigned)(cp - ROBO_HANGUL_SBASE);
    out[0] = (robo_uchar32_t)(ROBO_HANGUL_LBASE + sindex / ROBO_HANGUL_NCOUNT);
    out[1] = (robo_uchar32_t)(ROBO_HANGUL_VBASE +
                              (sindex % ROBO_HANGUL_NCOUNT) / ROBO_HANGUL_TCOUNT);
    tindex = sindex % ROBO_HANGUL_TCOUNT;
    if (tindex == 0u) {
        *out_len = 2u;
    } else {
        out[2] = (robo_uchar32_t)(ROBO_HANGUL_TBASE + tindex);
        *out_len = 3u;
    }
    return 1;
}

static uint32_t robo_norm_hangul_compose(uint32_t a, uint32_t b)
{
    if (a >= (uint32_t)ROBO_HANGUL_LBASE &&
        a < (uint32_t)(ROBO_HANGUL_LBASE + ROBO_HANGUL_LCOUNT) &&
        b >= (uint32_t)ROBO_HANGUL_VBASE &&
        b < (uint32_t)(ROBO_HANGUL_VBASE + ROBO_HANGUL_VCOUNT)) {
        unsigned li = (unsigned)(a - ROBO_HANGUL_LBASE);
        unsigned vi = (unsigned)(b - ROBO_HANGUL_VBASE);
        return (uint32_t)(ROBO_HANGUL_SBASE +
                          (li * ROBO_HANGUL_VCOUNT + vi) * ROBO_HANGUL_TCOUNT);
    }
    if (a >= (uint32_t)ROBO_HANGUL_SBASE &&
        a < (uint32_t)(ROBO_HANGUL_SBASE + ROBO_HANGUL_SCOUNT) &&
        ((a - (uint32_t)ROBO_HANGUL_SBASE) % (uint32_t)ROBO_HANGUL_TCOUNT) == 0u &&
        b > (uint32_t)ROBO_HANGUL_TBASE &&
        b < (uint32_t)(ROBO_HANGUL_TBASE + ROBO_HANGUL_TCOUNT)) {
        return a + (b - (uint32_t)ROBO_HANGUL_TBASE);
    }
    return 0u;
}

static uint32_t robo_norm_try_compose(uint32_t a, uint32_t b)
{
    uint32_t h = robo_norm_hangul_compose(a, b);
    if (h != 0u) {
        return h;
    }
    return robo_norm_compose_pair(a, b);
}

static robo_result_t robo_norm_decompose_cp(robo_uchar32_t cp,
                                            int compatibility,
                                            robo_norm_buf_t *out)
{
    robo_uchar32_t hangul[3];
    size_t hlen = 0;
    size_t i;
    size_t map_len = 0;
    const uint32_t *map;

    if (robo_norm_hangul_decompose(cp, hangul, &hlen)) {
        for (i = 0; i < hlen; i++) {
            robo_result_t r = robo_norm_decompose_cp(hangul[i], compatibility, out);
            if (r != ROBO_OK) {
                return r;
            }
        }
        return ROBO_OK;
    }

    if (compatibility) {
        map = robo_norm_lookup_compat(cp, &map_len);
        if (map && map_len > 0u) {
            for (i = 0; i < map_len; i++) {
                robo_result_t r =
                    robo_norm_decompose_cp((robo_uchar32_t)map[i], 1, out);
                if (r != ROBO_OK) {
                    return r;
                }
            }
            return ROBO_OK;
        }
    }

    map = robo_norm_lookup_canon(cp, &map_len);
    if (map && map_len > 0u) {
        for (i = 0; i < map_len; i++) {
            robo_result_t r =
                robo_norm_decompose_cp((robo_uchar32_t)map[i], compatibility, out);
            if (r != ROBO_OK) {
                return r;
            }
        }
        return ROBO_OK;
    }

    return robo_norm_buf_push(out, cp);
}

static void robo_norm_canonical_order(robo_uchar32_t *buf, size_t len)
{
    size_t i;
    int swapped;
    if (len < 2u) {
        return;
    }
    do {
        swapped = 0;
        for (i = 1; i < len; i++) {
            unsigned ccc_b = robo_unicode_combining_class(buf[i]);
            unsigned ccc_a;
            if (ccc_b == 0u) {
                continue;
            }
            ccc_a = robo_unicode_combining_class(buf[i - 1u]);
            if (ccc_a > ccc_b) {
                robo_uchar32_t tmp = buf[i - 1u];
                buf[i - 1u] = buf[i];
                buf[i] = tmp;
                swapped = 1;
            }
        }
    } while (swapped);
}

/*
 * Canonical Composition Algorithm (Unicode Standard).
 * C is blocked from starter S if some B between them has CCC(B)==0 or
 * CCC(B) >= CCC(C). Sequential form: try compose when last_cc == 0
 * (adjacent to starter) or last_cc < cc (not blocked by prior mark).
 */
static void robo_norm_compose(robo_uchar32_t *buf, size_t *len_io)
{
    size_t len = *len_io;
    size_t out = 0;
    size_t i;
    size_t last_starter = (size_t)-1;
    unsigned last_cc = 0;

    for (i = 0; i < len; i++) {
        robo_uchar32_t c = buf[i];
        unsigned cc = robo_unicode_combining_class(c);
        int combined = 0;

        if (last_starter != (size_t)-1) {
            int unblocked = (last_cc == 0u) || (cc > last_cc);
            if (unblocked) {
                uint32_t comp = robo_norm_try_compose(buf[last_starter], c);
                if (comp != 0u) {
                    buf[last_starter] = (robo_uchar32_t)comp;
                    combined = 1;
                }
            }
        }

        if (!combined) {
            buf[out] = c;
            if (cc == 0u) {
                last_starter = out;
                last_cc = 0u;
            } else {
                last_cc = cc;
            }
            out++;
        }
    }
    *len_io = out;
}

static robo_result_t robo_norm_utf8_dup_ascii(const char *src,
                                              size_t src_len,
                                              char **out,
                                              size_t *out_len)
{
    char *buf = (char *)robo_malloc(src_len + 1u);
    if (!buf) {
        return ROBO_OUT_OF_MEMORY;
    }
    if (src_len) {
        memcpy(buf, src, src_len);
    }
    buf[src_len] = '\0';
    *out = buf;
    *out_len = src_len;
    return ROBO_OK;
}

robo_result_t robo_unicode_normalize(const robo_uchar32_t *src,
                                     size_t src_len,
                                     robo_unicode_norm_form_t form,
                                     robo_uchar32_t **out,
                                     size_t *out_len)
{
    robo_norm_buf_t buf;
    size_t i;
    int compatibility;
    int compose;
    robo_result_t r;

    if (!out || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    *out_len = 0;
    if (src_len > 0u && !src) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (form > ROBO_UNICODE_NORM_NFKD) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (src_len == 0u) {
        return ROBO_OK;
    }

    compatibility =
        (form == ROBO_UNICODE_NORM_NFKC || form == ROBO_UNICODE_NORM_NFKD);
    compose =
        (form == ROBO_UNICODE_NORM_NFC || form == ROBO_UNICODE_NORM_NFKC);

    memset(&buf, 0, sizeof(buf));
    if (robo_norm_buf_reserve(&buf, src_len) != ROBO_OK) {
        return ROBO_OUT_OF_MEMORY;
    }

    for (i = 0; i < src_len; i++) {
        r = robo_norm_decompose_cp(src[i], compatibility, &buf);
        if (r != ROBO_OK) {
            robo_free(buf.data);
            return r;
        }
    }

    robo_norm_canonical_order(buf.data, buf.len);
    if (compose) {
        robo_norm_compose(buf.data, &buf.len);
    }

    *out = buf.data;
    *out_len = buf.len;
    return ROBO_OK;
}

robo_result_t robo_unicode_normalize_nfc(const robo_uchar32_t *src,
                                         size_t src_len,
                                         robo_uchar32_t **out,
                                         size_t *out_len)
{
    return robo_unicode_normalize(src, src_len, ROBO_UNICODE_NORM_NFC, out, out_len);
}

robo_result_t robo_unicode_normalize_nfd(const robo_uchar32_t *src,
                                         size_t src_len,
                                         robo_uchar32_t **out,
                                         size_t *out_len)
{
    return robo_unicode_normalize(src, src_len, ROBO_UNICODE_NORM_NFD, out, out_len);
}

robo_result_t robo_unicode_normalize_nfkc(const robo_uchar32_t *src,
                                          size_t src_len,
                                          robo_uchar32_t **out,
                                          size_t *out_len)
{
    return robo_unicode_normalize(src, src_len, ROBO_UNICODE_NORM_NFKC, out, out_len);
}

robo_result_t robo_unicode_normalize_nfkd(const robo_uchar32_t *src,
                                          size_t src_len,
                                          robo_uchar32_t **out,
                                          size_t *out_len)
{
    return robo_unicode_normalize(src, src_len, ROBO_UNICODE_NORM_NFKD, out, out_len);
}

robo_result_t robo_unicode_normalize_utf8(const char *src,
                                          size_t src_len,
                                          robo_unicode_norm_form_t form,
                                          robo_unicode_mode_t mode,
                                          char **out,
                                          size_t *out_len)
{
    robo_norm_buf_t cps;
    robo_uchar32_t *norm = NULL;
    size_t norm_len = 0;
    size_t off = 0;
    size_t utf8_need = 0;
    size_t w = 0;
    size_t i;
    char *utf8;
    robo_result_t r;

    if (!out || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    *out_len = 0;
    if (src_len > 0u && !src) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (form > ROBO_UNICODE_NORM_NFKD) {
        return ROBO_INVALID_ARGUMENT;
    }

    if (src_len == 0u) {
        return robo_norm_utf8_dup_ascii("", 0, out, out_len);
    }

    /* All four forms are identity on pure ASCII. */
    if (robo_norm_is_ascii_utf8(src, src_len)) {
        return robo_norm_utf8_dup_ascii(src, src_len, out, out_len);
    }

    memset(&cps, 0, sizeof(cps));
    while (off < src_len) {
        robo_uchar32_t cp = 0;
        size_t next = off;
        robo_unicode_status_t st =
            robo_unicode_utf8_decode(src, src_len, off, mode, &cp, &next);
        if (st != ROBO_UNICODE_OK) {
            if (cps.data) {
                robo_free(cps.data);
            }
            return ROBO_PARSE_ERROR;
        }
        r = robo_norm_buf_push(&cps, cp);
        if (r != ROBO_OK) {
            if (cps.data) {
                robo_free(cps.data);
            }
            return r;
        }
        off = next;
    }

    r = robo_unicode_normalize(cps.data, cps.len, form, &norm, &norm_len);
    robo_free(cps.data);
    if (r != ROBO_OK) {
        return r;
    }

    for (i = 0; i < norm_len; i++) {
        size_t n = robo_unicode_utf8_encode_len(norm[i]);
        if (n == 0u) {
            n = robo_unicode_utf8_encode_len(ROBO_UCHAR32_REPLACEMENT);
        }
        utf8_need += n;
    }

    utf8 = (char *)robo_malloc(utf8_need + 1u);
    if (!utf8) {
        if (norm) {
            robo_free(norm);
        }
        return ROBO_OUT_OF_MEMORY;
    }

    for (i = 0; i < norm_len; i++) {
        size_t written = 0;
        robo_unicode_status_t st =
            robo_unicode_utf8_encode(norm[i], utf8 + w, utf8_need + 1u - w, &written);
        if (st != ROBO_UNICODE_OK) {
            st = robo_unicode_utf8_encode(ROBO_UCHAR32_REPLACEMENT,
                                          utf8 + w,
                                          utf8_need + 1u - w,
                                          &written);
            if (st != ROBO_UNICODE_OK) {
                robo_free(utf8);
                robo_free(norm);
                return ROBO_ERROR;
            }
        }
        w += written;
    }
    utf8[w] = '\0';
    if (norm) {
        robo_free(norm);
    }
    *out = utf8;
    *out_len = w;
    return ROBO_OK;
}

robo_result_t robo_unicode_normalize_nfc_utf8(const char *src,
                                              size_t src_len,
                                              robo_unicode_mode_t mode,
                                              char **out,
                                              size_t *out_len)
{
    return robo_unicode_normalize_utf8(
        src, src_len, ROBO_UNICODE_NORM_NFC, mode, out, out_len);
}

robo_result_t robo_unicode_normalize_nfd_utf8(const char *src,
                                              size_t src_len,
                                              robo_unicode_mode_t mode,
                                              char **out,
                                              size_t *out_len)
{
    return robo_unicode_normalize_utf8(
        src, src_len, ROBO_UNICODE_NORM_NFD, mode, out, out_len);
}

robo_result_t robo_unicode_normalize_nfkc_utf8(const char *src,
                                               size_t src_len,
                                               robo_unicode_mode_t mode,
                                               char **out,
                                               size_t *out_len)
{
    return robo_unicode_normalize_utf8(
        src, src_len, ROBO_UNICODE_NORM_NFKC, mode, out, out_len);
}

robo_result_t robo_unicode_normalize_nfkd_utf8(const char *src,
                                               size_t src_len,
                                               robo_unicode_mode_t mode,
                                               char **out,
                                               size_t *out_len)
{
    return robo_unicode_normalize_utf8(
        src, src_len, ROBO_UNICODE_NORM_NFKD, mode, out, out_len);
}
