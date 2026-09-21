/*
 * RoboUnicode — Goal 3.3 UTF-8 / UTF-16 / UTF-32 encode & decode.
 *
 * Algorithms follow Unicode Standard 16.0.0 Chapter 3 (encoding forms).
 * Independent implementation; not derived from Chromium/ICU sources.
 */

#include "RoboUnicode/RoboUnicode.h"

#include <string.h>

const char *UnicodeVersionString(void)
{
    return RoboUnicodeVersionString;
}

int UnicodeIsSurrogate(Uchar32 cp)
{
    return cp >= 0xD800u && cp <= 0xDFFFu;
}

int UnicodeIsScalar(Uchar32 cp)
{
    return cp <= RoboUchar32Max && !UnicodeIsSurrogate(cp);
}

size_t UnicodeUtf8EncodeLen(Uchar32 cp)
{
    if (!UnicodeIsScalar(cp)) {
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

UnicodeStatus UnicodeUtf8Encode(Uchar32 cp,
                                               char *out,
                                               size_t outCap,
                                               size_t *written)
{
    size_t n = UnicodeUtf8EncodeLen(cp);
    unsigned char *o;

    if (written) {
        *written = 0;
    }
    if (n == 0) {
        return RoboUnicodeOutOfRange;
    }
    if (!out || outCap < n) {
        if (written) {
            *written = n;
        }
        return RoboUnicodeNoSpace;
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
    return RoboUnicodeOk;
}

/*
 * Well-formed UTF-8 ranges (Unicode Table 3-7 style).
 * Returns expected sequence length from lead byte, or 0 if lead is invalid.
 */
static size_t Utf8SeqLen(unsigned char lead)
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

static int Utf8ContOk(unsigned char c)
{
    return (c & 0xC0u) == 0x80u;
}

static UnicodeStatus Utf8DecodeStrict(const unsigned char *s,
                                                     size_t len,
                                                     size_t offset,
                                                     Uchar32 *outCp,
                                                     size_t *outNext)
{
    unsigned char c0;
    size_t n;
    Uchar32 cp;

    if (offset >= len) {
        return RoboUnicodeTruncated;
    }
    c0 = s[offset];
    n = Utf8SeqLen(c0);
    if (n == 0) {
        return RoboUnicodeInvalid;
    }
    if (len - offset < n) {
        return RoboUnicodeTruncated;
    }

    if (n == 1) {
        cp = c0;
    } else if (n == 2) {
        if (!Utf8ContOk(s[offset + 1])) {
            return RoboUnicodeInvalid;
        }
        cp = ((Uchar32)(c0 & 0x1Fu) << 6) | (s[offset + 1] & 0x3Fu);
    } else if (n == 3) {
        unsigned char c1 = s[offset + 1];
        unsigned char c2 = s[offset + 2];
        if (!Utf8ContOk(c1) || !Utf8ContOk(c2)) {
            return RoboUnicodeInvalid;
        }
        /* E0 A0-BF / E1-EC 80-BF / ED 80-9F / EE-EF 80-BF */
        if (c0 == 0xE0u && c1 < 0xA0u) {
            return RoboUnicodeInvalid; /* overlong */
        }
        if (c0 == 0xEDu && c1 >= 0xA0u) {
            return RoboUnicodeInvalid; /* UTF-16 surrogates */
        }
        cp = ((Uchar32)(c0 & 0x0Fu) << 12) |
             ((Uchar32)(c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
    } else {
        unsigned char c1 = s[offset + 1];
        unsigned char c2 = s[offset + 2];
        unsigned char c3 = s[offset + 3];
        if (!Utf8ContOk(c1) || !Utf8ContOk(c2) ||
            !Utf8ContOk(c3)) {
            return RoboUnicodeInvalid;
        }
        /* F0 90-BF / F1-F3 80-BF / F4 80-8F */
        if (c0 == 0xF0u && c1 < 0x90u) {
            return RoboUnicodeInvalid; /* overlong */
        }
        if (c0 == 0xF4u && c1 >= 0x90u) {
            return RoboUnicodeInvalid; /* > U+10FFFF */
        }
        cp = ((Uchar32)(c0 & 0x07u) << 18) |
             ((Uchar32)(c1 & 0x3Fu) << 12) |
             ((Uchar32)(c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
    }

    if (!UnicodeIsScalar(cp)) {
        return RoboUnicodeInvalid;
    }
    if (outCp) {
        *outCp = cp;
    }
    if (outNext) {
        *outNext = offset + n;
    }
    return RoboUnicodeOk;
}

/*
 * REPLACE: on ill-formed input, emit U+FFFD and consume the first byte of the
 * ill-formed subsequence (simple, well-defined policy for Goal 3.3).
 */
UnicodeStatus UnicodeUtf8Decode(const char *src,
                                               size_t len,
                                               size_t offset,
                                               UnicodeMode mode,
                                               Uchar32 *outCp,
                                               size_t *outNext)
{
    UnicodeStatus st;

    if (!src && len > 0) {
        return RoboUnicodeInvalid;
    }
    st = Utf8DecodeStrict((const unsigned char *)src, len, offset, outCp,
                                 outNext);
    if (st == RoboUnicodeOk || mode == RoboUnicodeStrict) {
        return st;
    }
    if (offset >= len) {
        return RoboUnicodeTruncated;
    }
    /* REPLACE: one U+FFFD per consumed ill-formed lead byte. */
    if (outCp) {
        *outCp = RoboUchar32Replacement;
    }
    if (outNext) {
        *outNext = offset + 1;
    }
    return RoboUnicodeOk;
}

UnicodeStatus UnicodeUtf8Next(const char *src,
                                             size_t len,
                                             size_t offset,
                                             UnicodeMode mode,
                                             Uchar32 *outCp,
                                             size_t *outNext)
{
    return UnicodeUtf8Decode(src, len, offset, mode, outCp, outNext);
}

UnicodeStatus UnicodeUtf8Prev(const char *src,
                                             size_t len,
                                             size_t offset,
                                             UnicodeMode mode,
                                             Uchar32 *outCp,
                                             size_t *outStart)
{
    size_t i;
    size_t start;
    const unsigned char *s = (const unsigned char *)src;
    UnicodeStatus st;
    size_t next;

    if ((!src && len > 0) || offset > len || offset == 0) {
        return RoboUnicodeInvalid;
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
    st = UnicodeUtf8Decode(src, len, start, mode, outCp, &next);
    if (st != RoboUnicodeOk) {
        return st;
    }
    if (next != offset && mode == RoboUnicodeStrict) {
        /* Misaligned offset (landed mid-sequence). */
        return RoboUnicodeInvalid;
    }
    if (outStart) {
        *outStart = start;
    }
    return RoboUnicodeOk;
}

int UnicodeUtf8Validate(const char *src, size_t len)
{
    size_t off = 0;
    int ok = 1;

    if (!src && len > 0) {
        return 0;
    }
    while (off < len) {
        size_t next = 0;
        if (UnicodeUtf8Decode(src, len, off, RoboUnicodeStrict, NULL,
                                     &next) != RoboUnicodeOk) {
            ok = 0;
            break;
        }
        off = next;
    }
    return ok;
}

size_t UnicodeUtf8Count(const char *src,
                               size_t len,
                               UnicodeMode mode,
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
        UnicodeStatus st =
            UnicodeUtf8Decode(src, len, off, mode, NULL, &next);
        if (st != RoboUnicodeOk) {
            if (ok) {
                *ok = 0;
            }
            return mode == RoboUnicodeStrict ? 0 : count;
        }
        count++;
        off = next;
    }
    return count;
}

size_t UnicodeUtf16EncodeLen(Uchar32 cp)
{
    if (!UnicodeIsScalar(cp)) {
        return 0;
    }
    return cp <= 0xFFFFu ? 1u : 2u;
}

UnicodeStatus UnicodeUtf16Encode(Uchar32 cp,
                                                Uchar16 *out,
                                                size_t outCap,
                                                size_t *written)
{
    size_t n = UnicodeUtf16EncodeLen(cp);

    if (written) {
        *written = 0;
    }
    if (n == 0) {
        return RoboUnicodeOutOfRange;
    }
    if (!out || outCap < n) {
        if (written) {
            *written = n;
        }
        return RoboUnicodeNoSpace;
    }
    if (n == 1) {
        out[0] = (Uchar16)cp;
    } else {
        Uchar32 v = cp - 0x10000u;
        out[0] = (Uchar16)(0xD800u + (v >> 10));
        out[1] = (Uchar16)(0xDC00u + (v & 0x3FFu));
    }
    if (written) {
        *written = n;
    }
    return RoboUnicodeOk;
}

UnicodeStatus UnicodeUtf16Decode(const Uchar16 *src,
                                                size_t len,
                                                size_t offset,
                                                UnicodeMode mode,
                                                Uchar32 *outCp,
                                                size_t *outNext)
{
    Uchar16 w1;

    if ((!src && len > 0) || offset >= len) {
        return RoboUnicodeTruncated;
    }
    w1 = src[offset];
    if (w1 < 0xD800u || w1 > 0xDFFFu) {
        if (outCp) {
            *outCp = w1;
        }
        if (outNext) {
            *outNext = offset + 1;
        }
        return RoboUnicodeOk;
    }
    if (w1 >= 0xDC00u) {
        /* Lone trailing surrogate */
        if (mode == RoboUnicodeReplace) {
            if (outCp) {
                *outCp = RoboUchar32Replacement;
            }
            if (outNext) {
                *outNext = offset + 1;
            }
            return RoboUnicodeOk;
        }
        return RoboUnicodeInvalid;
    }
    /* High surrogate */
    if (offset + 1 >= len) {
        if (mode == RoboUnicodeReplace) {
            if (outCp) {
                *outCp = RoboUchar32Replacement;
            }
            if (outNext) {
                *outNext = offset + 1;
            }
            return RoboUnicodeOk;
        }
        return RoboUnicodeTruncated;
    }
    {
        Uchar16 w2 = src[offset + 1];
        if (w2 < 0xDC00u || w2 > 0xDFFFu) {
            if (mode == RoboUnicodeReplace) {
                if (outCp) {
                    *outCp = RoboUchar32Replacement;
                }
                if (outNext) {
                    *outNext = offset + 1;
                }
                return RoboUnicodeOk;
            }
            return RoboUnicodeInvalid;
        }
        if (outCp) {
            *outCp = 0x10000u + (((Uchar32)(w1 - 0xD800u) << 10) |
                                  (Uchar32)(w2 - 0xDC00u));
        }
        if (outNext) {
            *outNext = offset + 2;
        }
        return RoboUnicodeOk;
    }
}

UnicodeStatus UnicodeUtf16Next(const Uchar16 *src,
                                              size_t len,
                                              size_t offset,
                                              UnicodeMode mode,
                                              Uchar32 *outCp,
                                              size_t *outNext)
{
    return UnicodeUtf16Decode(src, len, offset, mode, outCp, outNext);
}

int UnicodeUtf16Validate(const Uchar16 *src, size_t len)
{
    size_t off = 0;
    if (!src && len > 0) {
        return 0;
    }
    while (off < len) {
        size_t next = 0;
        if (UnicodeUtf16Decode(src, len, off, RoboUnicodeStrict, NULL,
                                      &next) != RoboUnicodeOk) {
            return 0;
        }
        off = next;
    }
    return 1;
}

UnicodeStatus UnicodeUtf32Encode(Uchar32 cp,
                                                Uchar32 *out,
                                                size_t outCap,
                                                size_t *written)
{
    if (written) {
        *written = 0;
    }
    if (!UnicodeIsScalar(cp)) {
        return RoboUnicodeOutOfRange;
    }
    if (!out || outCap < 1) {
        if (written) {
            *written = 1;
        }
        return RoboUnicodeNoSpace;
    }
    out[0] = cp;
    if (written) {
        *written = 1;
    }
    return RoboUnicodeOk;
}

UnicodeStatus UnicodeUtf32Decode(const Uchar32 *src,
                                                size_t len,
                                                size_t offset,
                                                UnicodeMode mode,
                                                Uchar32 *outCp,
                                                size_t *outNext)
{
    Uchar32 cp;

    if ((!src && len > 0) || offset >= len) {
        return RoboUnicodeTruncated;
    }
    cp = src[offset];
    if (!UnicodeIsScalar(cp)) {
        if (mode == RoboUnicodeReplace) {
            if (outCp) {
                *outCp = RoboUchar32Replacement;
            }
            if (outNext) {
                *outNext = offset + 1;
            }
            return RoboUnicodeOk;
        }
        return RoboUnicodeInvalid;
    }
    if (outCp) {
        *outCp = cp;
    }
    if (outNext) {
        *outNext = offset + 1;
    }
    return RoboUnicodeOk;
}

int UnicodeUtf32Validate(const Uchar32 *src, size_t len)
{
    size_t i;
    if (!src && len > 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (!UnicodeIsScalar(src[i])) {
            return 0;
        }
    }
    return 1;
}

UnicodeStatus UnicodeUtf8ToUtf16(const char *src,
                                                 size_t srcLen,
                                                 Uchar16 *dst,
                                                 size_t dstCap,
                                                 size_t *dstLen,
                                                 UnicodeMode mode)
{
    size_t off = 0;
    size_t out = 0;

    if (dstLen) {
        *dstLen = 0;
    }
    if (!src && srcLen > 0) {
        return RoboUnicodeInvalid;
    }
    while (off < srcLen) {
        Uchar32 cp = 0;
        size_t next = 0;
        size_t w = 0;
        UnicodeStatus st =
            UnicodeUtf8Decode(src, srcLen, off, mode, &cp, &next);
        if (st != RoboUnicodeOk) {
            return st;
        }
        st = UnicodeUtf16Encode(cp, dst ? dst + out : NULL,
                                       dst ? dstCap - out : 0, &w);
        if (st == RoboUnicodeNoSpace || !dst) {
            out += w ? w : UnicodeUtf16EncodeLen(cp);
            off = next;
            continue;
        }
        if (st != RoboUnicodeOk) {
            return st;
        }
        out += w;
        off = next;
    }
    if (dstLen) {
        *dstLen = out;
    }
    if (!dst) {
        return RoboUnicodeOk;
    }
    return out <= dstCap ? RoboUnicodeOk : RoboUnicodeNoSpace;
}

UnicodeStatus UnicodeUtf16ToUtf8(const Uchar16 *src,
                                                 size_t srcLen,
                                                 char *dst,
                                                 size_t dstCap,
                                                 size_t *dstLen,
                                                 UnicodeMode mode)
{
    size_t off = 0;
    size_t out = 0;

    if (dstLen) {
        *dstLen = 0;
    }
    if (!src && srcLen > 0) {
        return RoboUnicodeInvalid;
    }
    while (off < srcLen) {
        Uchar32 cp = 0;
        size_t next = 0;
        size_t w = 0;
        UnicodeStatus st =
            UnicodeUtf16Decode(src, srcLen, off, mode, &cp, &next);
        if (st != RoboUnicodeOk) {
            return st;
        }
        st = UnicodeUtf8Encode(cp, dst ? dst + out : NULL,
                                      dst ? dstCap - out : 0, &w);
        if (st == RoboUnicodeNoSpace || !dst) {
            out += w ? w : UnicodeUtf8EncodeLen(cp);
            off = next;
            continue;
        }
        if (st != RoboUnicodeOk) {
            return st;
        }
        out += w;
        off = next;
    }
    if (dstLen) {
        *dstLen = out;
    }
    if (!dst) {
        return RoboUnicodeOk;
    }
    return out <= dstCap ? RoboUnicodeOk : RoboUnicodeNoSpace;
}

UnicodeStatus UnicodeUtf8ToUtf32(const char *src,
                                                 size_t srcLen,
                                                 Uchar32 *dst,
                                                 size_t dstCap,
                                                 size_t *dstLen,
                                                 UnicodeMode mode)
{
    size_t off = 0;
    size_t out = 0;

    if (dstLen) {
        *dstLen = 0;
    }
    if (!src && srcLen > 0) {
        return RoboUnicodeInvalid;
    }
    while (off < srcLen) {
        Uchar32 cp = 0;
        size_t next = 0;
        UnicodeStatus st =
            UnicodeUtf8Decode(src, srcLen, off, mode, &cp, &next);
        if (st != RoboUnicodeOk) {
            return st;
        }
        if (dst && out < dstCap) {
            dst[out] = cp;
        }
        out++;
        off = next;
    }
    if (dstLen) {
        *dstLen = out;
    }
    if (!dst) {
        return RoboUnicodeOk;
    }
    return out <= dstCap ? RoboUnicodeOk : RoboUnicodeNoSpace;
}

UnicodeStatus UnicodeUtf32ToUtf8(const Uchar32 *src,
                                                 size_t srcLen,
                                                 char *dst,
                                                 size_t dstCap,
                                                 size_t *dstLen,
                                                 UnicodeMode mode)
{
    size_t i;
    size_t out = 0;

    if (dstLen) {
        *dstLen = 0;
    }
    if (!src && srcLen > 0) {
        return RoboUnicodeInvalid;
    }
    for (i = 0; i < srcLen; i++) {
        Uchar32 cp = src[i];
        size_t w = 0;
        UnicodeStatus st;
        if (!UnicodeIsScalar(cp)) {
            if (mode == RoboUnicodeStrict) {
                return RoboUnicodeInvalid;
            }
            cp = RoboUchar32Replacement;
        }
        st = UnicodeUtf8Encode(cp, dst ? dst + out : NULL,
                                      dst ? dstCap - out : 0, &w);
        if (st == RoboUnicodeNoSpace || !dst) {
            out += w ? w : UnicodeUtf8EncodeLen(cp);
            continue;
        }
        if (st != RoboUnicodeOk) {
            return st;
        }
        out += w;
    }
    if (dstLen) {
        *dstLen = out;
    }
    if (!dst) {
        return RoboUnicodeOk;
    }
    return out <= dstCap ? RoboUnicodeOk : RoboUnicodeNoSpace;
}
