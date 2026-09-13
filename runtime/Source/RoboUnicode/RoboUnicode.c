/*
 * RoboUnicode — Goal 3.3 UTF-8 / UTF-16 / UTF-32 encode & decode.
 *
 * Algorithms follow Unicode Standard 16.0.0 Chapter 3 (encoding forms).
 * Independent implementation; not derived from Chromium/ICU sources.
 */

#include "RoboUnicode/RoboUnicode.h"

#include <string.h>

const char *robo_unicode_version_string(void)
{
    return ROBO_UNICODE_VERSION_STRING;
}

int robo_unicode_is_surrogate(robo_uchar32_t cp)
{
    return cp >= 0xD800u && cp <= 0xDFFFu;
}

int robo_unicode_is_scalar(robo_uchar32_t cp)
{
    return cp <= ROBO_UCHAR32_MAX && !robo_unicode_is_surrogate(cp);
}

size_t robo_unicode_utf8_encode_len(robo_uchar32_t cp)
{
    if (!robo_unicode_is_scalar(cp)) {
        return 0;
    }
    if (cp <= 0x7Fu) {
        return 1;
    }
    if (cp <= 0x7FFu) {
        return 2;
    }
    if (cp <= 0xFFFFu) {
        return 3;
    }
    return 4;
}

robo_unicode_status_t robo_unicode_utf8_encode(robo_uchar32_t cp,
                                               char *out,
                                               size_t out_cap,
                                               size_t *written)
{
    size_t n = robo_unicode_utf8_encode_len(cp);
    unsigned char *o;

    if (written) {
        *written = 0;
    }
    if (n == 0) {
        return ROBO_UNICODE_OUT_OF_RANGE;
    }
    if (!out || out_cap < n) {
        if (written) {
            *written = n;
        }
        return ROBO_UNICODE_NO_SPACE;
    }
    o = (unsigned char *)out;
    if (n == 1) {
        o[0] = (unsigned char)cp;
    } else if (n == 2) {
        o[0] = (unsigned char)(0xC0u | (cp >> 6));
        o[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
    } else if (n == 3) {
        o[0] = (unsigned char)(0xE0u | (cp >> 12));
        o[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        o[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
    } else {
        o[0] = (unsigned char)(0xF0u | (cp >> 18));
        o[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
        o[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        o[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
    }
    if (written) {
        *written = n;
    }
    return ROBO_UNICODE_OK;
}

/*
 * Well-formed UTF-8 ranges (Unicode Table 3-7 style).
 * Returns expected sequence length from lead byte, or 0 if lead is invalid.
 */
static size_t robo_utf8_seq_len(unsigned char lead)
{
    if (lead <= 0x7Fu) {
        return 1;
    }
    if (lead >= 0xC2u && lead <= 0xDFu) {
        return 2;
    }
    if (lead >= 0xE0u && lead <= 0xEFu) {
        return 3;
    }
    if (lead >= 0xF0u && lead <= 0xF4u) {
        return 4;
    }
    return 0;
}

static int robo_utf8_cont_ok(unsigned char c)
{
    return (c & 0xC0u) == 0x80u;
}

static robo_unicode_status_t robo_utf8_decode_strict(const unsigned char *s,
                                                     size_t len,
                                                     size_t offset,
                                                     robo_uchar32_t *out_cp,
                                                     size_t *out_next)
{
    unsigned char c0;
    size_t n;
    robo_uchar32_t cp;

    if (offset >= len) {
        return ROBO_UNICODE_TRUNCATED;
    }
    c0 = s[offset];
    n = robo_utf8_seq_len(c0);
    if (n == 0) {
        return ROBO_UNICODE_INVALID;
    }
    if (len - offset < n) {
        return ROBO_UNICODE_TRUNCATED;
    }

    if (n == 1) {
        cp = c0;
    } else if (n == 2) {
        if (!robo_utf8_cont_ok(s[offset + 1])) {
            return ROBO_UNICODE_INVALID;
        }
        cp = ((robo_uchar32_t)(c0 & 0x1Fu) << 6) | (s[offset + 1] & 0x3Fu);
    } else if (n == 3) {
        unsigned char c1 = s[offset + 1];
        unsigned char c2 = s[offset + 2];
        if (!robo_utf8_cont_ok(c1) || !robo_utf8_cont_ok(c2)) {
            return ROBO_UNICODE_INVALID;
        }
        /* E0 A0-BF / E1-EC 80-BF / ED 80-9F / EE-EF 80-BF */
        if (c0 == 0xE0u && c1 < 0xA0u) {
            return ROBO_UNICODE_INVALID; /* overlong */
        }
        if (c0 == 0xEDu && c1 >= 0xA0u) {
            return ROBO_UNICODE_INVALID; /* UTF-16 surrogates */
        }
        cp = ((robo_uchar32_t)(c0 & 0x0Fu) << 12) |
             ((robo_uchar32_t)(c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
    } else {
        unsigned char c1 = s[offset + 1];
        unsigned char c2 = s[offset + 2];
        unsigned char c3 = s[offset + 3];
        if (!robo_utf8_cont_ok(c1) || !robo_utf8_cont_ok(c2) ||
            !robo_utf8_cont_ok(c3)) {
            return ROBO_UNICODE_INVALID;
        }
        /* F0 90-BF / F1-F3 80-BF / F4 80-8F */
        if (c0 == 0xF0u && c1 < 0x90u) {
            return ROBO_UNICODE_INVALID; /* overlong */
        }
        if (c0 == 0xF4u && c1 >= 0x90u) {
            return ROBO_UNICODE_INVALID; /* > U+10FFFF */
        }
        cp = ((robo_uchar32_t)(c0 & 0x07u) << 18) |
             ((robo_uchar32_t)(c1 & 0x3Fu) << 12) |
             ((robo_uchar32_t)(c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
    }

    if (!robo_unicode_is_scalar(cp)) {
        return ROBO_UNICODE_INVALID;
    }
    if (out_cp) {
        *out_cp = cp;
    }
    if (out_next) {
        *out_next = offset + n;
    }
    return ROBO_UNICODE_OK;
}

/*
 * REPLACE: on ill-formed input, emit U+FFFD and consume the first byte of the
 * ill-formed subsequence (simple, well-defined policy for Goal 3.3).
 */
robo_unicode_status_t robo_unicode_utf8_decode(const char *src,
                                               size_t len,
                                               size_t offset,
                                               robo_unicode_mode_t mode,
                                               robo_uchar32_t *out_cp,
                                               size_t *out_next)
{
    robo_unicode_status_t st;

    if (!src && len > 0) {
        return ROBO_UNICODE_INVALID;
    }
    st = robo_utf8_decode_strict((const unsigned char *)src, len, offset, out_cp,
                                 out_next);
    if (st == ROBO_UNICODE_OK || mode == ROBO_UNICODE_STRICT) {
        return st;
    }
    if (offset >= len) {
        return ROBO_UNICODE_TRUNCATED;
    }
    /* REPLACE: one U+FFFD per consumed ill-formed lead byte. */
    if (out_cp) {
        *out_cp = ROBO_UCHAR32_REPLACEMENT;
    }
    if (out_next) {
        *out_next = offset + 1;
    }
    return ROBO_UNICODE_OK;
}

robo_unicode_status_t robo_unicode_utf8_next(const char *src,
                                             size_t len,
                                             size_t offset,
                                             robo_unicode_mode_t mode,
                                             robo_uchar32_t *out_cp,
                                             size_t *out_next)
{
    return robo_unicode_utf8_decode(src, len, offset, mode, out_cp, out_next);
}

robo_unicode_status_t robo_unicode_utf8_prev(const char *src,
                                             size_t len,
                                             size_t offset,
                                             robo_unicode_mode_t mode,
                                             robo_uchar32_t *out_cp,
                                             size_t *out_start)
{
    size_t i;
    size_t start;
    const unsigned char *s = (const unsigned char *)src;
    robo_unicode_status_t st;
    size_t next;

    if ((!src && len > 0) || offset > len || offset == 0) {
        return ROBO_UNICODE_INVALID;
    }
    i = offset;
    /* Skip continuation bytes to find a lead (max 3 steps back). */
    do {
        i--;
        if ((s[i] & 0xC0u) != 0x80u) {
            break;
        }
    } while (i > 0 && offset - i < 4);

    start = i;
    st = robo_unicode_utf8_decode(src, len, start, mode, out_cp, &next);
    if (st != ROBO_UNICODE_OK) {
        return st;
    }
    if (next != offset && mode == ROBO_UNICODE_STRICT) {
        /* Misaligned offset (landed mid-sequence). */
        return ROBO_UNICODE_INVALID;
    }
    if (out_start) {
        *out_start = start;
    }
    return ROBO_UNICODE_OK;
}

int robo_unicode_utf8_validate(const char *src, size_t len)
{
    size_t off = 0;
    int ok = 1;

    if (!src && len > 0) {
        return 0;
    }
    while (off < len) {
        size_t next = 0;
        if (robo_unicode_utf8_decode(src, len, off, ROBO_UNICODE_STRICT, NULL,
                                     &next) != ROBO_UNICODE_OK) {
            ok = 0;
            break;
        }
        off = next;
    }
    return ok;
}

size_t robo_unicode_utf8_count(const char *src,
                               size_t len,
                               robo_unicode_mode_t mode,
                               int *ok)
{
    size_t off = 0;
    size_t count = 0;

    if (ok) {
        *ok = 1;
    }
    if (!src && len > 0) {
        if (ok) {
            *ok = 0;
        }
        return 0;
    }
    while (off < len) {
        size_t next = 0;
        robo_unicode_status_t st =
            robo_unicode_utf8_decode(src, len, off, mode, NULL, &next);
        if (st != ROBO_UNICODE_OK) {
            if (ok) {
                *ok = 0;
            }
            return mode == ROBO_UNICODE_STRICT ? 0 : count;
        }
        count++;
        off = next;
    }
    return count;
}

size_t robo_unicode_utf16_encode_len(robo_uchar32_t cp)
{
    if (!robo_unicode_is_scalar(cp)) {
        return 0;
    }
    return cp <= 0xFFFFu ? 1u : 2u;
}

robo_unicode_status_t robo_unicode_utf16_encode(robo_uchar32_t cp,
                                                robo_uchar16_t *out,
                                                size_t out_cap,
                                                size_t *written)
{
    size_t n = robo_unicode_utf16_encode_len(cp);

    if (written) {
        *written = 0;
    }
    if (n == 0) {
        return ROBO_UNICODE_OUT_OF_RANGE;
    }
    if (!out || out_cap < n) {
        if (written) {
            *written = n;
        }
        return ROBO_UNICODE_NO_SPACE;
    }
    if (n == 1) {
        out[0] = (robo_uchar16_t)cp;
    } else {
        robo_uchar32_t v = cp - 0x10000u;
        out[0] = (robo_uchar16_t)(0xD800u + (v >> 10));
        out[1] = (robo_uchar16_t)(0xDC00u + (v & 0x3FFu));
    }
    if (written) {
        *written = n;
    }
    return ROBO_UNICODE_OK;
}

robo_unicode_status_t robo_unicode_utf16_decode(const robo_uchar16_t *src,
                                                size_t len,
                                                size_t offset,
                                                robo_unicode_mode_t mode,
                                                robo_uchar32_t *out_cp,
                                                size_t *out_next)
{
    robo_uchar16_t w1;

    if ((!src && len > 0) || offset >= len) {
        return ROBO_UNICODE_TRUNCATED;
    }
    w1 = src[offset];
    if (w1 < 0xD800u || w1 > 0xDFFFu) {
        if (out_cp) {
            *out_cp = w1;
        }
        if (out_next) {
            *out_next = offset + 1;
        }
        return ROBO_UNICODE_OK;
    }
    if (w1 >= 0xDC00u) {
        /* Lone trailing surrogate */
        if (mode == ROBO_UNICODE_REPLACE) {
            if (out_cp) {
                *out_cp = ROBO_UCHAR32_REPLACEMENT;
            }
            if (out_next) {
                *out_next = offset + 1;
            }
            return ROBO_UNICODE_OK;
        }
        return ROBO_UNICODE_INVALID;
    }
    /* High surrogate */
    if (offset + 1 >= len) {
        if (mode == ROBO_UNICODE_REPLACE) {
            if (out_cp) {
                *out_cp = ROBO_UCHAR32_REPLACEMENT;
            }
            if (out_next) {
                *out_next = offset + 1;
            }
            return ROBO_UNICODE_OK;
        }
        return ROBO_UNICODE_TRUNCATED;
    }
    {
        robo_uchar16_t w2 = src[offset + 1];
        if (w2 < 0xDC00u || w2 > 0xDFFFu) {
            if (mode == ROBO_UNICODE_REPLACE) {
                if (out_cp) {
                    *out_cp = ROBO_UCHAR32_REPLACEMENT;
                }
                if (out_next) {
                    *out_next = offset + 1;
                }
                return ROBO_UNICODE_OK;
            }
            return ROBO_UNICODE_INVALID;
        }
        if (out_cp) {
            *out_cp = 0x10000u + (((robo_uchar32_t)(w1 - 0xD800u) << 10) |
                                  (robo_uchar32_t)(w2 - 0xDC00u));
        }
        if (out_next) {
            *out_next = offset + 2;
        }
        return ROBO_UNICODE_OK;
    }
}

robo_unicode_status_t robo_unicode_utf16_next(const robo_uchar16_t *src,
                                              size_t len,
                                              size_t offset,
                                              robo_unicode_mode_t mode,
                                              robo_uchar32_t *out_cp,
                                              size_t *out_next)
{
    return robo_unicode_utf16_decode(src, len, offset, mode, out_cp, out_next);
}

int robo_unicode_utf16_validate(const robo_uchar16_t *src, size_t len)
{
    size_t off = 0;
    if (!src && len > 0) {
        return 0;
    }
    while (off < len) {
        size_t next = 0;
        if (robo_unicode_utf16_decode(src, len, off, ROBO_UNICODE_STRICT, NULL,
                                      &next) != ROBO_UNICODE_OK) {
            return 0;
        }
        off = next;
    }
    return 1;
}

robo_unicode_status_t robo_unicode_utf32_encode(robo_uchar32_t cp,
                                                robo_uchar32_t *out,
                                                size_t out_cap,
                                                size_t *written)
{
    if (written) {
        *written = 0;
    }
    if (!robo_unicode_is_scalar(cp)) {
        return ROBO_UNICODE_OUT_OF_RANGE;
    }
    if (!out || out_cap < 1) {
        if (written) {
            *written = 1;
        }
        return ROBO_UNICODE_NO_SPACE;
    }
    out[0] = cp;
    if (written) {
        *written = 1;
    }
    return ROBO_UNICODE_OK;
}

robo_unicode_status_t robo_unicode_utf32_decode(const robo_uchar32_t *src,
                                                size_t len,
                                                size_t offset,
                                                robo_unicode_mode_t mode,
                                                robo_uchar32_t *out_cp,
                                                size_t *out_next)
{
    robo_uchar32_t cp;

    if ((!src && len > 0) || offset >= len) {
        return ROBO_UNICODE_TRUNCATED;
    }
    cp = src[offset];
    if (!robo_unicode_is_scalar(cp)) {
        if (mode == ROBO_UNICODE_REPLACE) {
            if (out_cp) {
                *out_cp = ROBO_UCHAR32_REPLACEMENT;
            }
            if (out_next) {
                *out_next = offset + 1;
            }
            return ROBO_UNICODE_OK;
        }
        return ROBO_UNICODE_INVALID;
    }
    if (out_cp) {
        *out_cp = cp;
    }
    if (out_next) {
        *out_next = offset + 1;
    }
    return ROBO_UNICODE_OK;
}

int robo_unicode_utf32_validate(const robo_uchar32_t *src, size_t len)
{
    size_t i;
    if (!src && len > 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (!robo_unicode_is_scalar(src[i])) {
            return 0;
        }
    }
    return 1;
}

robo_unicode_status_t robo_unicode_utf8_to_utf16(const char *src,
                                                 size_t src_len,
                                                 robo_uchar16_t *dst,
                                                 size_t dst_cap,
                                                 size_t *dst_len,
                                                 robo_unicode_mode_t mode)
{
    size_t off = 0;
    size_t out = 0;

    if (dst_len) {
        *dst_len = 0;
    }
    if (!src && src_len > 0) {
        return ROBO_UNICODE_INVALID;
    }
    while (off < src_len) {
        robo_uchar32_t cp = 0;
        size_t next = 0;
        size_t w = 0;
        robo_unicode_status_t st =
            robo_unicode_utf8_decode(src, src_len, off, mode, &cp, &next);
        if (st != ROBO_UNICODE_OK) {
            return st;
        }
        st = robo_unicode_utf16_encode(cp, dst ? dst + out : NULL,
                                       dst ? dst_cap - out : 0, &w);
        if (st == ROBO_UNICODE_NO_SPACE || !dst) {
            out += w ? w : robo_unicode_utf16_encode_len(cp);
            off = next;
            continue;
        }
        if (st != ROBO_UNICODE_OK) {
            return st;
        }
        out += w;
        off = next;
    }
    if (dst_len) {
        *dst_len = out;
    }
    if (!dst) {
        return ROBO_UNICODE_OK;
    }
    return out <= dst_cap ? ROBO_UNICODE_OK : ROBO_UNICODE_NO_SPACE;
}

robo_unicode_status_t robo_unicode_utf16_to_utf8(const robo_uchar16_t *src,
                                                 size_t src_len,
                                                 char *dst,
                                                 size_t dst_cap,
                                                 size_t *dst_len,
                                                 robo_unicode_mode_t mode)
{
    size_t off = 0;
    size_t out = 0;

    if (dst_len) {
        *dst_len = 0;
    }
    if (!src && src_len > 0) {
        return ROBO_UNICODE_INVALID;
    }
    while (off < src_len) {
        robo_uchar32_t cp = 0;
        size_t next = 0;
        size_t w = 0;
        robo_unicode_status_t st =
            robo_unicode_utf16_decode(src, src_len, off, mode, &cp, &next);
        if (st != ROBO_UNICODE_OK) {
            return st;
        }
        st = robo_unicode_utf8_encode(cp, dst ? dst + out : NULL,
                                      dst ? dst_cap - out : 0, &w);
        if (st == ROBO_UNICODE_NO_SPACE || !dst) {
            out += w ? w : robo_unicode_utf8_encode_len(cp);
            off = next;
            continue;
        }
        if (st != ROBO_UNICODE_OK) {
            return st;
        }
        out += w;
        off = next;
    }
    if (dst_len) {
        *dst_len = out;
    }
    if (!dst) {
        return ROBO_UNICODE_OK;
    }
    return out <= dst_cap ? ROBO_UNICODE_OK : ROBO_UNICODE_NO_SPACE;
}

robo_unicode_status_t robo_unicode_utf8_to_utf32(const char *src,
                                                 size_t src_len,
                                                 robo_uchar32_t *dst,
                                                 size_t dst_cap,
                                                 size_t *dst_len,
                                                 robo_unicode_mode_t mode)
{
    size_t off = 0;
    size_t out = 0;

    if (dst_len) {
        *dst_len = 0;
    }
    if (!src && src_len > 0) {
        return ROBO_UNICODE_INVALID;
    }
    while (off < src_len) {
        robo_uchar32_t cp = 0;
        size_t next = 0;
        robo_unicode_status_t st =
            robo_unicode_utf8_decode(src, src_len, off, mode, &cp, &next);
        if (st != ROBO_UNICODE_OK) {
            return st;
        }
        if (dst && out < dst_cap) {
            dst[out] = cp;
        }
        out++;
        off = next;
    }
    if (dst_len) {
        *dst_len = out;
    }
    if (!dst) {
        return ROBO_UNICODE_OK;
    }
    return out <= dst_cap ? ROBO_UNICODE_OK : ROBO_UNICODE_NO_SPACE;
}

robo_unicode_status_t robo_unicode_utf32_to_utf8(const robo_uchar32_t *src,
                                                 size_t src_len,
                                                 char *dst,
                                                 size_t dst_cap,
                                                 size_t *dst_len,
                                                 robo_unicode_mode_t mode)
{
    size_t i;
    size_t out = 0;

    if (dst_len) {
        *dst_len = 0;
    }
    if (!src && src_len > 0) {
        return ROBO_UNICODE_INVALID;
    }
    for (i = 0; i < src_len; i++) {
        robo_uchar32_t cp = src[i];
        size_t w = 0;
        robo_unicode_status_t st;
        if (!robo_unicode_is_scalar(cp)) {
            if (mode == ROBO_UNICODE_STRICT) {
                return ROBO_UNICODE_INVALID;
            }
            cp = ROBO_UCHAR32_REPLACEMENT;
        }
        st = robo_unicode_utf8_encode(cp, dst ? dst + out : NULL,
                                      dst ? dst_cap - out : 0, &w);
        if (st == ROBO_UNICODE_NO_SPACE || !dst) {
            out += w ? w : robo_unicode_utf8_encode_len(cp);
            continue;
        }
        if (st != ROBO_UNICODE_OK) {
            return st;
        }
        out += w;
    }
    if (dst_len) {
        *dst_len = out;
    }
    if (!dst) {
        return ROBO_UNICODE_OK;
    }
    return out <= dst_cap ? ROBO_UNICODE_OK : ROBO_UNICODE_NO_SPACE;
}
