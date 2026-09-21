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
    RoboHangulSbase = 0xAC00,
    RoboHangulLbase = 0x1100,
    RoboHangulVbase = 0x1161,
    RoboHangulTbase = 0x11A7,
    RoboHangulLcount = 19,
    RoboHangulVcount = 21,
    RoboHangulTcount = 28,
    RoboHangulNcount = RoboHangulVcount * RoboHangulTcount,
    RoboHangulScount = RoboHangulLcount * RoboHangulNcount
};

typedef struct NormBuf {
    Uchar32 *data;
    size_t len;
    size_t cap;
} NormBuf;

const char *UnicodeNormUcdVersionString(void)
{
    return RoboNormUcdVersion;
}

static int NormIsAsciiUtf8(const char *src, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if ((unsigned char)src[i] >= 0x80u) {
            return 0;
        }
    }
    return 1;
}

static Result NormBufReserve(NormBuf *b, size_t need)
{
    Uchar32 *n;
    size_t ncap;
    if (need <= b->cap) {
        return RoboOk;
    }
    ncap = b->cap ? b->cap : 8u;
    while (ncap < need) {
        if (ncap > ((size_t)-1) / 2u) {
            return RoboOutOfMemory;
        }
        ncap *= 2u;
    }
    n = (Uchar32 *)Malloc(ncap * sizeof(Uchar32));
    if (!n) {
        return RoboOutOfMemory;
    }
    if (b->data && b->len) {
        memcpy(n, b->data, b->len * sizeof(Uchar32));
    }
    if (b->data) {
        RoboFree(b->data);
    }
    b->data = n;
    b->cap = ncap;
    return RoboOk;
}

static Result NormBufPush(NormBuf *b, Uchar32 cp)
{
    Result r = NormBufReserve(b, b->len + 1u);
    if (r != RoboOk) {
        return r;
    }
    b->data[b->len++] = cp;
    return RoboOk;
}

static int NormHangulDecompose(Uchar32 cp,
                                      Uchar32 out[3],
                                      size_t *outLen)
{
    unsigned sindex;
    unsigned tindex;
    if (cp < (Uchar32)RoboHangulSbase ||
        cp >= (Uchar32)(RoboHangulSbase + RoboHangulScount)) {
        return 0;
    }
    sindex = (unsigned)(cp - RoboHangulSbase);
    out[0] = (Uchar32)(RoboHangulLbase + sindex / RoboHangulNcount);
    out[1] = (Uchar32)(RoboHangulVbase +
                              (sindex % RoboHangulNcount) / RoboHangulTcount);
    tindex = sindex % RoboHangulTcount;
    if (tindex == 0u) {
        *outLen = 2u;
    } else {
        out[2] = (Uchar32)(RoboHangulTbase + tindex);
        *outLen = 3u;
    }
    return 1;
}

static uint32_t NormHangulCompose(uint32_t a, uint32_t b)
{
    if (a >= (uint32_t)RoboHangulLbase &&
        a < (uint32_t)(RoboHangulLbase + RoboHangulLcount) &&
        b >= (uint32_t)RoboHangulVbase &&
        b < (uint32_t)(RoboHangulVbase + RoboHangulVcount)) {
        unsigned li = (unsigned)(a - RoboHangulLbase);
        unsigned vi = (unsigned)(b - RoboHangulVbase);
        return (uint32_t)(RoboHangulSbase +
                          (li * RoboHangulVcount + vi) * RoboHangulTcount);
    }
    if (a >= (uint32_t)RoboHangulSbase &&
        a < (uint32_t)(RoboHangulSbase + RoboHangulScount) &&
        ((a - (uint32_t)RoboHangulSbase) % (uint32_t)RoboHangulTcount) == 0u &&
        b > (uint32_t)RoboHangulTbase &&
        b < (uint32_t)(RoboHangulTbase + RoboHangulTcount)) {
        return a + (b - (uint32_t)RoboHangulTbase);
    }
    return 0u;
}

static uint32_t NormTryCompose(uint32_t a, uint32_t b)
{
    uint32_t h = NormHangulCompose(a, b);
    if (h != 0u) {
        return h;
    }
    return NormComposePair(a, b);
}

static Result NormDecomposeCp(Uchar32 cp,
                                            int compatibility,
                                            NormBuf *out)
{
    Uchar32 hangul[3];
    size_t hlen = 0;
    size_t i;
    size_t mapLen = 0;
    const uint32_t *map;

    if (NormHangulDecompose(cp, hangul, &hlen)) {
        for (i = 0; i < hlen; i++) {
            Result r = NormDecomposeCp(hangul[i], compatibility, out);
            if (r != RoboOk) {
                return r;
            }
        }
        return RoboOk;
    }

    if (compatibility) {
        map = NormLookupCompat(cp, &mapLen);
        if (map && mapLen > 0u) {
            for (i = 0; i < mapLen; i++) {
                Result r =
                    NormDecomposeCp((Uchar32)map[i], 1, out);
                if (r != RoboOk) {
                    return r;
                }
            }
            return RoboOk;
        }
    }

    map = NormLookupCanon(cp, &mapLen);
    if (map && mapLen > 0u) {
        for (i = 0; i < mapLen; i++) {
            Result r =
                NormDecomposeCp((Uchar32)map[i], compatibility, out);
            if (r != RoboOk) {
                return r;
            }
        }
        return RoboOk;
    }

    return NormBufPush(out, cp);
}

static void NormCanonicalOrder(Uchar32 *buf, size_t len)
{
    size_t i;
    int swapped;
    if (len < 2u) {
        return;
    }
    do {
        swapped = 0;
        for (i = 1; i < len; i++) {
            unsigned cccB = UnicodeCombiningClass(buf[i]);
            unsigned cccA;
            if (cccB == 0u) {
                continue;
            }
            cccA = UnicodeCombiningClass(buf[i - 1u]);
            if (cccA > cccB) {
                Uchar32 tmp = buf[i - 1u];
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
 * CCC(B) >= CCC(C). Sequential form: try compose when lastCc == 0
 * (adjacent to starter) or lastCc < cc (not blocked by prior mark).
 */
static void NormCompose(Uchar32 *buf, size_t *lenIo)
{
    size_t len = *lenIo;
    size_t out = 0;
    size_t i;
    size_t lastStarter = (size_t)-1;
    unsigned lastCc = 0;

    for (i = 0; i < len; i++) {
        Uchar32 c = buf[i];
        unsigned cc = UnicodeCombiningClass(c);
        int combined = 0;

        if (lastStarter != (size_t)-1) {
            int unblocked = (lastCc == 0u) || (cc > lastCc);
            if (unblocked) {
                uint32_t comp = NormTryCompose(buf[lastStarter], c);
                if (comp != 0u) {
                    buf[lastStarter] = (Uchar32)comp;
                    combined = 1;
                }
            }
        }

        if (!combined) {
            buf[out] = c;
            if (cc == 0u) {
                lastStarter = out;
                lastCc = 0u;
            } else {
                lastCc = cc;
            }
            out++;
        }
    }
    *lenIo = out;
}

static Result NormUtf8DupAscii(const char *src,
                                              size_t srcLen,
                                              char **out,
                                              size_t *outLen)
{
    char *buf = (char *)Malloc(srcLen + 1u);
    if (!buf) {
        return RoboOutOfMemory;
    }
    if (srcLen) {
        memcpy(buf, src, srcLen);
    }
    buf[srcLen] = '\0';
    *out = buf;
    *outLen = srcLen;
    return RoboOk;
}

Result UnicodeNormalize(const Uchar32 *src,
                                     size_t srcLen,
                                     UnicodeNormForm form,
                                     Uchar32 **out,
                                     size_t *outLen)
{
    NormBuf buf;
    size_t i;
    int compatibility;
    int compose;
    Result r;

    if (!out || !outLen) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    *outLen = 0;
    if (srcLen > 0u && !src) {
        return RoboInvalidArgument;
    }
    if (form > RoboUnicodeNormNfkd) {
        return RoboInvalidArgument;
    }
    if (srcLen == 0u) {
        return RoboOk;
    }

    compatibility =
        (form == RoboUnicodeNormNfkc || form == RoboUnicodeNormNfkd);
    compose =
        (form == RoboUnicodeNormNfc || form == RoboUnicodeNormNfkc);

    memset(&buf, 0, sizeof(buf));
    if (NormBufReserve(&buf, srcLen) != RoboOk) {
        return RoboOutOfMemory;
    }

    for (i = 0; i < srcLen; i++) {
        r = NormDecomposeCp(src[i], compatibility, &buf);
        if (r != RoboOk) {
            RoboFree(buf.data);
            return r;
        }
    }

    NormCanonicalOrder(buf.data, buf.len);
    if (compose) {
        NormCompose(buf.data, &buf.len);
    }

    *out = buf.data;
    *outLen = buf.len;
    return RoboOk;
}

Result UnicodeNormalizeNfc(const Uchar32 *src,
                                         size_t srcLen,
                                         Uchar32 **out,
                                         size_t *outLen)
{
    return UnicodeNormalize(src, srcLen, RoboUnicodeNormNfc, out, outLen);
}

Result UnicodeNormalizeNfd(const Uchar32 *src,
                                         size_t srcLen,
                                         Uchar32 **out,
                                         size_t *outLen)
{
    return UnicodeNormalize(src, srcLen, RoboUnicodeNormNfd, out, outLen);
}

Result UnicodeNormalizeNfkc(const Uchar32 *src,
                                          size_t srcLen,
                                          Uchar32 **out,
                                          size_t *outLen)
{
    return UnicodeNormalize(src, srcLen, RoboUnicodeNormNfkc, out, outLen);
}

Result UnicodeNormalizeNfkd(const Uchar32 *src,
                                          size_t srcLen,
                                          Uchar32 **out,
                                          size_t *outLen)
{
    return UnicodeNormalize(src, srcLen, RoboUnicodeNormNfkd, out, outLen);
}

Result UnicodeNormalizeUtf8(const char *src,
                                          size_t srcLen,
                                          UnicodeNormForm form,
                                          UnicodeMode mode,
                                          char **out,
                                          size_t *outLen)
{
    NormBuf cps;
    Uchar32 *norm = NULL;
    size_t normLen = 0;
    size_t off = 0;
    size_t utf8Need = 0;
    size_t w = 0;
    size_t i;
    char *utf8;
    Result r;

    if (!out || !outLen) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    *outLen = 0;
    if (srcLen > 0u && !src) {
        return RoboInvalidArgument;
    }
    if (form > RoboUnicodeNormNfkd) {
        return RoboInvalidArgument;
    }

    if (srcLen == 0u) {
        return NormUtf8DupAscii("", 0, out, outLen);
    }

    /* All four forms are identity on pure ASCII. */
    if (NormIsAsciiUtf8(src, srcLen)) {
        return NormUtf8DupAscii(src, srcLen, out, outLen);
    }

    memset(&cps, 0, sizeof(cps));
    while (off < srcLen) {
        Uchar32 cp = 0;
        size_t next = off;
        UnicodeStatus st =
            UnicodeUtf8Decode(src, srcLen, off, mode, &cp, &next);
        if (st != RoboUnicodeOk) {
            if (cps.data) {
                RoboFree(cps.data);
            }
            return RoboParseError;
        }
        r = NormBufPush(&cps, cp);
        if (r != RoboOk) {
            if (cps.data) {
                RoboFree(cps.data);
            }
            return r;
        }
        off = next;
    }

    r = UnicodeNormalize(cps.data, cps.len, form, &norm, &normLen);
    RoboFree(cps.data);
    if (r != RoboOk) {
        return r;
    }

    for (i = 0; i < normLen; i++) {
        size_t n = UnicodeUtf8EncodeLen(norm[i]);
        if (n == 0u) {
            n = UnicodeUtf8EncodeLen(RoboUchar32Replacement);
        }
        utf8Need += n;
    }

    utf8 = (char *)Malloc(utf8Need + 1u);
    if (!utf8) {
        if (norm) {
            RoboFree(norm);
        }
        return RoboOutOfMemory;
    }

    for (i = 0; i < normLen; i++) {
        size_t written = 0;
        UnicodeStatus st =
            UnicodeUtf8Encode(norm[i], utf8 + w, utf8Need + 1u - w, &written);
        if (st != RoboUnicodeOk) {
            st = UnicodeUtf8Encode(RoboUchar32Replacement,
                                          utf8 + w,
                                          utf8Need + 1u - w,
                                          &written);
            if (st != RoboUnicodeOk) {
                RoboFree(utf8);
                RoboFree(norm);
                return RoboError;
            }
        }
        w += written;
    }
    utf8[w] = '\0';
    if (norm) {
        RoboFree(norm);
    }
    *out = utf8;
    *outLen = w;
    return RoboOk;
}

Result UnicodeNormalizeNfcUtf8(const char *src,
                                              size_t srcLen,
                                              UnicodeMode mode,
                                              char **out,
                                              size_t *outLen)
{
    return UnicodeNormalizeUtf8(
        src, srcLen, RoboUnicodeNormNfc, mode, out, outLen);
}

Result UnicodeNormalizeNfdUtf8(const char *src,
                                              size_t srcLen,
                                              UnicodeMode mode,
                                              char **out,
                                              size_t *outLen)
{
    return UnicodeNormalizeUtf8(
        src, srcLen, RoboUnicodeNormNfd, mode, out, outLen);
}

Result UnicodeNormalizeNfkcUtf8(const char *src,
                                               size_t srcLen,
                                               UnicodeMode mode,
                                               char **out,
                                               size_t *outLen)
{
    return UnicodeNormalizeUtf8(
        src, srcLen, RoboUnicodeNormNfkc, mode, out, outLen);
}

Result UnicodeNormalizeNfkdUtf8(const char *src,
                                               size_t srcLen,
                                               UnicodeMode mode,
                                               char **out,
                                               size_t *outLen)
{
    return UnicodeNormalizeUtf8(
        src, srcLen, RoboUnicodeNormNfkd, mode, out, outLen);
}
