/*
 * Unicode case mapping / case folding — UCD 16.0.0 Default Case Algorithms.
 */

#include "RoboUnicode/RoboUnicodeCase.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

const char *UnicodeCaseUcdVersionString(void)
{
    return RoboCaseUcdVersion;
}

Uchar32 UnicodeSimpleToupper(Uchar32 cp)
{
    return (Uchar32)CaseSimpleUpper(cp);
}

Uchar32 UnicodeSimpleTolower(Uchar32 cp)
{
    return (Uchar32)CaseSimpleLower(cp);
}

Uchar32 UnicodeSimpleTotitle(Uchar32 cp)
{
    return (Uchar32)CaseSimpleTitle(cp);
}

Uchar32 UnicodeSimpleCasefold(Uchar32 cp)
{
    return (Uchar32)CaseSimpleFold(cp);
}

size_t UnicodeCaseMap(Uchar32 cp,
                             UnicodeCaseForm form,
                             Uchar32 *out,
                             size_t outCap)
{
    uint32_t tmp[RoboCaseMaxMap];
    size_t n = 0;
    size_t i;

    if (!out || outCap == 0u) {
        return 0;
    }

    switch (form) {
    case RoboUnicodeCaseSimpleUpper:
        out[0] = UnicodeSimpleToupper(cp);
        return 1;
    case RoboUnicodeCaseSimpleLower:
        out[0] = UnicodeSimpleTolower(cp);
        return 1;
    case RoboUnicodeCaseSimpleTitle:
        out[0] = UnicodeSimpleTotitle(cp);
        return 1;
    case RoboUnicodeCaseSimpleFold:
        out[0] = UnicodeSimpleCasefold(cp);
        return 1;
    case RoboUnicodeCaseUpper:
        n = CaseFullUpper(cp, tmp, RoboCaseMaxMap);
        break;
    case RoboUnicodeCaseLower:
        n = CaseFullLower(cp, tmp, RoboCaseMaxMap);
        break;
    case RoboUnicodeCaseTitle:
        n = CaseFullTitle(cp, tmp, RoboCaseMaxMap);
        break;
    case RoboUnicodeCaseFold:
        n = CaseFullFold(cp, tmp, RoboCaseMaxMap);
        break;
    default:
        out[0] = cp;
        return 1;
    }
    if (n == 0u || n > outCap) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        out[i] = (Uchar32)tmp[i];
    }
    return n;
}

size_t UnicodeToUpper(Uchar32 cp, Uchar32 *out, size_t outCap)
{
    return UnicodeCaseMap(cp, RoboUnicodeCaseUpper, out, outCap);
}

size_t UnicodeToLower(Uchar32 cp, Uchar32 *out, size_t outCap)
{
    return UnicodeCaseMap(cp, RoboUnicodeCaseLower, out, outCap);
}

size_t UnicodeToTitle(Uchar32 cp, Uchar32 *out, size_t outCap)
{
    return UnicodeCaseMap(cp, RoboUnicodeCaseTitle, out, outCap);
}

size_t UnicodeCasefold(Uchar32 cp, Uchar32 *out, size_t outCap)
{
    return UnicodeCaseMap(cp, RoboUnicodeCaseFold, out, outCap);
}

/*
 * Final_Sigma (Unicode Default Case Algorithms):
 * C is a Final_Sigma context if:
 *   - C is preceded by a sequence matching \p{cased}(\p{case_ignorable})*
 *   - and C is not followed by a sequence matching (\p{case_ignorable})*\p{cased}
 */
static int CaseIsFinalSigmaContext(const Uchar32 *cps,
                                            size_t len,
                                            size_t index)
{
    size_t i;
    int sawCased;

    if (index >= len || cps[index] != 0x03A3u) {
        return 0;
    }

    /* Look behind: ... Cased Case_Ignorable*  (immediately before index) */
    sawCased = 0;
    i = index;
    while (i > 0u) {
        i--;
        if (CaseIsCaseIgnorable(cps[i])) {
            continue;
        }
        if (CaseIsCased(cps[i])) {
            sawCased = 1;
        }
        break;
    }
    if (!sawCased) {
        return 0;
    }

    /* Look ahead: Case_Ignorable* Cased → NOT Final_Sigma */
    i = index + 1u;
    while (i < len) {
        if (CaseIsCaseIgnorable(cps[i])) {
            i++;
            continue;
        }
        if (CaseIsCased(cps[i])) {
            return 0;
        }
        break;
    }
    return 1;
}

Result UnicodeCaseMapCps(const Uchar32 *src,
                                        size_t srcLen,
                                        UnicodeCaseForm form,
                                        Uchar32 **out,
                                        size_t *outLen)
{
    Uchar32 *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    size_t i;
    Uchar32 mapped[RoboCaseMaxMap];

    if (!out || !outLen) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    *outLen = 0;
    if (srcLen > 0u && !src) {
        return RoboInvalidArgument;
    }
    if (srcLen == 0u) {
        return RoboOk;
    }

    cap = srcLen;
    buf = (Uchar32 *)Malloc(cap * sizeof(Uchar32));
    if (!buf) {
        return RoboOutOfMemory;
    }

    for (i = 0; i < srcLen; i++) {
        size_t n;
        size_t j;
        if (form == RoboUnicodeCaseLower && src[i] == 0x03A3u &&
            CaseIsFinalSigmaContext(src, srcLen, i)) {
            n = CaseFinalSigmaLower((uint32_t *)mapped, RoboCaseMaxMap);
        } else {
            n = UnicodeCaseMap(src[i], form, mapped, RoboCaseMaxMap);
        }
        if (n == 0u) {
            RoboFree(buf);
            return RoboError;
        }
        if (len + n > cap) {
            size_t ncap = cap * 2u;
            Uchar32 *nb;
            while (ncap < len + n) {
                if (ncap > ((size_t)-1) / 2u) {
                    RoboFree(buf);
                    return RoboOutOfMemory;
                }
                ncap *= 2u;
            }
            nb = (Uchar32 *)Malloc(ncap * sizeof(Uchar32));
            if (!nb) {
                RoboFree(buf);
                return RoboOutOfMemory;
            }
            memcpy(nb, buf, len * sizeof(Uchar32));
            RoboFree(buf);
            buf = nb;
            cap = ncap;
        }
        for (j = 0; j < n; j++) {
            buf[len++] = mapped[j];
        }
    }

    *out = buf;
    *outLen = len;
    return RoboOk;
}

static Result CaseUtf8EncodeCps(const Uchar32 *cps,
                                               size_t cpsLen,
                                               char **out,
                                               size_t *outLen)
{
    size_t need = 0;
    size_t i;
    size_t w = 0;
    char *utf8;

    for (i = 0; i < cpsLen; i++) {
        size_t n = UnicodeUtf8EncodeLen(cps[i]);
        if (n == 0u) {
            n = UnicodeUtf8EncodeLen(RoboUchar32Replacement);
        }
        need += n;
    }
    utf8 = (char *)Malloc(need + 1u);
    if (!utf8) {
        return RoboOutOfMemory;
    }
    for (i = 0; i < cpsLen; i++) {
        size_t written = 0;
        UnicodeStatus st =
            UnicodeUtf8Encode(cps[i], utf8 + w, need + 1u - w, &written);
        if (st != RoboUnicodeOk) {
            st = UnicodeUtf8Encode(
                RoboUchar32Replacement, utf8 + w, need + 1u - w, &written);
            if (st != RoboUnicodeOk) {
                RoboFree(utf8);
                return RoboError;
            }
        }
        w += written;
    }
    utf8[w] = '\0';
    *out = utf8;
    *outLen = w;
    return RoboOk;
}

Result UnicodeCaseMapUtf8(const char *src,
                                         size_t srcLen,
                                         UnicodeCaseForm form,
                                         UnicodeMode mode,
                                         char **out,
                                         size_t *outLen)
{
    Uchar32 *cps = NULL;
    size_t cpsCap = 0;
    size_t cpsLen = 0;
    size_t off = 0;
    Uchar32 *mapped = NULL;
    size_t mappedLen = 0;
    Result r;

    if (!out || !outLen) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    *outLen = 0;
    if (srcLen > 0u && !src) {
        return RoboInvalidArgument;
    }
    if (srcLen == 0u) {
        char *empty = (char *)Malloc(1u);
        if (!empty) {
            return RoboOutOfMemory;
        }
        empty[0] = '\0';
        *out = empty;
        *outLen = 0;
        return RoboOk;
    }

    /* Fast path: simple forms on pure ASCII for upper/lower/fold */
    if (form == RoboUnicodeCaseSimpleUpper ||
        form == RoboUnicodeCaseSimpleLower ||
        form == RoboUnicodeCaseSimpleFold ||
        form == RoboUnicodeCaseUpper ||
        form == RoboUnicodeCaseLower ||
        form == RoboUnicodeCaseFold) {
        size_t i;
        int ascii = 1;
        for (i = 0; i < srcLen; i++) {
            if ((unsigned char)src[i] >= 0x80u) {
                ascii = 0;
                break;
            }
        }
        if (ascii && (form == RoboUnicodeCaseSimpleUpper ||
                      form == RoboUnicodeCaseUpper ||
                      form == RoboUnicodeCaseSimpleLower ||
                      form == RoboUnicodeCaseLower ||
                      form == RoboUnicodeCaseSimpleFold ||
                      form == RoboUnicodeCaseFold)) {
            char *buf = (char *)Malloc(srcLen + 1u);
            if (!buf) {
                return RoboOutOfMemory;
            }
            for (i = 0; i < srcLen; i++) {
                unsigned char ch = (unsigned char)src[i];
                if (form == RoboUnicodeCaseSimpleUpper ||
                    form == RoboUnicodeCaseUpper) {
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
            buf[srcLen] = '\0';
            *out = buf;
            *outLen = srcLen;
            return RoboOk;
        }
    }

    cpsCap = srcLen;
    cps = (Uchar32 *)Malloc(cpsCap * sizeof(Uchar32));
    if (!cps) {
        return RoboOutOfMemory;
    }

    while (off < srcLen) {
        Uchar32 cp = 0;
        size_t next = off;
        UnicodeStatus st =
            UnicodeUtf8Decode(src, srcLen, off, mode, &cp, &next);
        if (st != RoboUnicodeOk) {
            RoboFree(cps);
            return RoboParseError;
        }
        if (cpsLen >= cpsCap) {
            size_t ncap = cpsCap * 2u;
            Uchar32 *nb =
                (Uchar32 *)Malloc(ncap * sizeof(Uchar32));
            if (!nb) {
                RoboFree(cps);
                return RoboOutOfMemory;
            }
            memcpy(nb, cps, cpsLen * sizeof(Uchar32));
            RoboFree(cps);
            cps = nb;
            cpsCap = ncap;
        }
        cps[cpsLen++] = cp;
        off = next;
    }

    r = UnicodeCaseMapCps(cps, cpsLen, form, &mapped, &mappedLen);
    RoboFree(cps);
    if (r != RoboOk) {
        return r;
    }

    if (!mapped || mappedLen == 0u) {
        char *empty = (char *)Malloc(1u);
        if (mapped) {
            RoboFree(mapped);
        }
        if (!empty) {
            return RoboOutOfMemory;
        }
        empty[0] = '\0';
        *out = empty;
        *outLen = 0;
        return RoboOk;
    }

    r = CaseUtf8EncodeCps(mapped, mappedLen, out, outLen);
    RoboFree(mapped);
    return r;
}

Result UnicodeToUpperUtf8(const char *src, size_t srcLen,
                                         UnicodeMode mode,
                                         char **out, size_t *outLen)
{
    return UnicodeCaseMapUtf8(
        src, srcLen, RoboUnicodeCaseUpper, mode, out, outLen);
}

Result UnicodeToLowerUtf8(const char *src, size_t srcLen,
                                         UnicodeMode mode,
                                         char **out, size_t *outLen)
{
    return UnicodeCaseMapUtf8(
        src, srcLen, RoboUnicodeCaseLower, mode, out, outLen);
}

Result UnicodeToTitleUtf8(const char *src, size_t srcLen,
                                         UnicodeMode mode,
                                         char **out, size_t *outLen)
{
    return UnicodeCaseMapUtf8(
        src, srcLen, RoboUnicodeCaseTitle, mode, out, outLen);
}

Result UnicodeCasefoldUtf8(const char *src, size_t srcLen,
                                         UnicodeMode mode,
                                         char **out, size_t *outLen)
{
    return UnicodeCaseMapUtf8(
        src, srcLen, RoboUnicodeCaseFold, mode, out, outLen);
}

Result UnicodeCasefoldCompareUtf8(const char *a, size_t aLen,
                                                 const char *b, size_t bLen,
                                                 UnicodeMode mode,
                                                 int *cmp)
{
    char *fa = NULL;
    char *fb = NULL;
    size_t faLen = 0;
    size_t fbLen = 0;
    size_t n;
    Result r;

    if (!cmp) {
        return RoboInvalidArgument;
    }
    *cmp = 0;

    r = UnicodeCasefoldUtf8(a, aLen, mode, &fa, &faLen);
    if (r != RoboOk) {
        return r;
    }
    r = UnicodeCasefoldUtf8(b, bLen, mode, &fb, &fbLen);
    if (r != RoboOk) {
        RoboFree(fa);
        return r;
    }

    n = faLen < fbLen ? faLen : fbLen;
    if (n > 0u) {
        *cmp = memcmp(fa, fb, n);
    }
    if (*cmp == 0) {
        if (faLen < fbLen) {
            *cmp = -1;
        } else if (faLen > fbLen) {
            *cmp = 1;
        }
    }
    RoboFree(fa);
    RoboFree(fb);
    return RoboOk;
}
