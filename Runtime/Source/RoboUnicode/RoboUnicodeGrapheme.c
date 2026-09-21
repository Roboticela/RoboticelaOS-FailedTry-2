/*
 * Extended Grapheme Cluster boundaries — UAX #29 (Unicode 16.0.0).
 */

#include "RoboUnicode/RoboUnicodeGrapheme.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <string.h>

const char *UnicodeGraphemeUcdVersionString(void)
{
    return RoboGraphemeUcdVersion;
}

static int GraphemeIsExtPict(Uchar32 cp)
{
    return UnicodeIsExtendedPictographic(cp);
}

static int GraphemeIncbIsExtendOrLinker(UnicodeIncb v)
{
    return v == RoboUIncbExtend || v == RoboUIncbLinker;
}

static int GraphemeGb9cNoBreak(const Uchar32 *cps, size_t index)
{
    size_t j;
    size_t k;

    if (index == 0 || GraphemeIncb(cps[index]) != RoboUIncbConsonant) {
        return 0;
    }
    for (j = index - 1u; ; j--) {
        UnicodeIncb v = GraphemeIncb(cps[j]);
        if (!GraphemeIncbIsExtendOrLinker(v)) {
            return 0;
        }
        if (v == RoboUIncbLinker) {
            k = j;
            while (k > 0 &&
                   GraphemeIncbIsExtendOrLinker(GraphemeIncb(cps[k - 1u]))) {
                k--;
            }
            if (k > 0 && GraphemeIncb(cps[k - 1u]) == RoboUIncbConsonant) {
                return 1;
            }
        }
        if (j == 0u) {
            return 0;
        }
    }
}

static int GraphemeGb11NoBreak(const Uchar32 *cps, size_t index)
{
    size_t j;

    if (!GraphemeIsExtPict(cps[index]) || index == 0) {
        return 0;
    }
    j = index - 1u;
    if (GraphemeBreakProperty(cps[j]) != RoboUGbZwj) {
        return 0;
    }
    if (j == 0) {
        return 0;
    }
    j--;
    while (j > 0 && GraphemeBreakProperty(cps[j]) == RoboUGbExtend) {
        j--;
    }
    return GraphemeIsExtPict(cps[j]);
}

static int GraphemeGbRiNoBreak(const Uchar32 *cps, size_t index)
{
    size_t j;
    size_t riRun;

    if (index == 0) {
        return 0;
    }
    if (GraphemeBreakProperty(cps[index - 1u]) != RoboUGbRegionalIndicator ||
        GraphemeBreakProperty(cps[index]) != RoboUGbRegionalIndicator) {
        return 0;
    }
    j = index - 1u;
    while (j > 0 && GraphemeBreakProperty(cps[j - 1u]) == RoboUGbRegionalIndicator) {
        j--;
    }
    riRun = index - j;
    return (riRun % 2u) == 1u;
}

int IsUnicodeGraphemeBreak(const Uchar32 *cps, size_t len, size_t index)
{
    UnicodeGraphemeBreak l;
    UnicodeGraphemeBreak r;

    if (index == 0u) {
        return 1;
    }
    if (index >= len || !cps) {
        return 1;
    }

    l = GraphemeBreakProperty(cps[index - 1u]);
    r = GraphemeBreakProperty(cps[index]);

    if (l == RoboUGbCr && r == RoboUGbLf) {
        return 0;
    }
    if (l == RoboUGbControl || l == RoboUGbCr || l == RoboUGbLf) {
        return 1;
    }
    if (r == RoboUGbControl || r == RoboUGbCr || r == RoboUGbLf) {
        return 1;
    }
    if (l == RoboUGbL &&
        (r == RoboUGbL || r == RoboUGbV || r == RoboUGbLv || r == RoboUGbLvt)) {
        return 0;
    }
    if ((l == RoboUGbLv || l == RoboUGbV) && (r == RoboUGbV || r == RoboUGbT)) {
        return 0;
    }
    if ((l == RoboUGbLvt || l == RoboUGbT) && r == RoboUGbT) {
        return 0;
    }
    if (r == RoboUGbExtend || r == RoboUGbZwj) {
        return 0;
    }
    if (r == RoboUGbSpacingmark) {
        return 0;
    }
    if (l == RoboUGbPrepend) {
        return 0;
    }
    if (GraphemeGb9cNoBreak(cps, index)) {
        return 0;
    }
    if (GraphemeGb11NoBreak(cps, index)) {
        return 0;
    }
    if (GraphemeGbRiNoBreak(cps, index)) {
        return 0;
    }
    return 1;
}

static Result GraphemeDecodeAt(const char *src,
                                             size_t srcLen,
                                             size_t offset,
                                             UnicodeMode mode,
                                             Uchar32 *cp,
                                             size_t *byteEnd)
{
    size_t next = offset;
    UnicodeStatus st =
        UnicodeUtf8Decode(src, srcLen, offset, mode, cp, &next);
    if (st != RoboUnicodeOk) {
        return RoboParseError;
    }
    *byteEnd = next;
    return RoboOk;
}

static Result GraphemeNextCluster(const char *src,
                                                size_t srcLen,
                                                size_t byteOffset,
                                                UnicodeMode mode,
                                                size_t *clusterStart,
                                                size_t *clusterEnd)
{
    Uchar32 stackCps[48];
    size_t stackEnds[48];
    Uchar32 *cps = stackCps;
    size_t *cpEnds = stackEnds;
    size_t cap = 48u;
    size_t n = 0;
    size_t off = byteOffset;
    Result r;

    if (byteOffset >= srcLen) {
        *clusterStart = srcLen;
        *clusterEnd = srcLen;
        return RoboOk;
    }

    *clusterStart = byteOffset;

    for (;;) {
        Uchar32 cp = 0;
        size_t bend = off;

        if (off >= srcLen) {
            break;
        }
        r = GraphemeDecodeAt(src, srcLen, off, mode, &cp, &bend);
        if (r != RoboOk) {
            if (cps != stackCps) {
                RoboFree(cps);
            }
            if (cpEnds != stackEnds) {
                RoboFree(cpEnds);
            }
            return r;
        }
        if (n > 0u) {
            cps[n] = cp;
            if (IsUnicodeGraphemeBreak(cps, n + 1u, n)) {
                break;
            }
        }
        if (n >= cap) {
            size_t ncap = cap * 2u;
            Uchar32 *nc =
                (Uchar32 *)Malloc(ncap * sizeof(Uchar32));
            size_t *ne = (size_t *)Malloc(ncap * sizeof(size_t));
            if (!nc || !ne) {
                RoboFree(nc);
                RoboFree(ne);
                if (cps != stackCps) {
                    RoboFree(cps);
                }
                if (cpEnds != stackEnds) {
                    RoboFree(cpEnds);
                }
                return RoboOutOfMemory;
            }
            if (n) {
                memcpy(nc, cps, n * sizeof(Uchar32));
                memcpy(ne, cpEnds, n * sizeof(size_t));
            }
            if (cps != stackCps) {
                RoboFree(cps);
            }
            if (cpEnds != stackEnds) {
                RoboFree(cpEnds);
            }
            cps = nc;
            cpEnds = ne;
            cap = ncap;
        }
        cps[n] = cp;
        cpEnds[n] = bend;
        n++;
        off = bend;
    }

    if (n == 0u) {
        *clusterEnd = byteOffset;
    } else {
        *clusterEnd = cpEnds[n - 1u];
    }
    if (cps != stackCps) {
        RoboFree(cps);
    }
    if (cpEnds != stackEnds) {
        RoboFree(cpEnds);
    }
    return RoboOk;
}

Result UnicodeGraphemeIterInit(UnicodeGraphemeIter *it,
                                              const char *src,
                                              size_t srcLen,
                                              UnicodeMode mode)
{
    if (!it) {
        return RoboInvalidArgument;
    }
    it->src = src;
    it->srcLen = srcLen;
    it->mode = mode;
    it->byteOffset = 0;
    it->clusterIndex = 0;
    it->valid = 1;
    return RoboOk;
}

Result UnicodeGraphemeIterNext(UnicodeGraphemeIter *it,
                                              size_t *clusterStart,
                                              size_t *clusterEnd)
{
    Result r;

    if (!it || !clusterStart || !clusterEnd) {
        return RoboInvalidArgument;
    }
    if (!it->valid) {
        return RoboParseError;
    }

    r = GraphemeNextCluster(
        it->src, it->srcLen, it->byteOffset, it->mode, clusterStart, clusterEnd);
    if (r != RoboOk) {
        it->valid = 0;
        return r;
    }
    it->byteOffset = *clusterEnd;
    if (*clusterStart < it->srcLen || *clusterEnd <= it->srcLen) {
        if (*clusterStart != *clusterEnd || *clusterStart < it->srcLen) {
            it->clusterIndex++;
        }
    }
    return RoboOk;
}

Result UnicodeGraphemeIterPrev(const char *src,
                                              size_t srcLen,
                                              size_t byteOffset,
                                              UnicodeMode mode,
                                              size_t *clusterStart,
                                              size_t *clusterEnd)
{
    UnicodeGraphemeIter it;
    size_t cs = 0;
    size_t ce = 0;
    size_t lastCs = 0;
    size_t lastCe = 0;

    if (!clusterStart || !clusterEnd) {
        return RoboInvalidArgument;
    }
    if (byteOffset > srcLen) {
        return RoboInvalidArgument;
    }
    if (byteOffset == 0u) {
        *clusterStart = 0;
        *clusterEnd = 0;
        return RoboOk;
    }

    UnicodeGraphemeIterInit(&it, src, srcLen, mode);
    while (it.byteOffset < byteOffset && it.valid) {
        lastCs = cs;
        lastCe = ce;
        if (UnicodeGraphemeIterNext(&it, &cs, &ce) != RoboOk) {
            return RoboParseError;
        }
        if (ce >= byteOffset) {
            if (cs < byteOffset) {
                *clusterStart = cs;
                *clusterEnd = ce;
            } else {
                *clusterStart = lastCs;
                *clusterEnd = lastCe;
            }
            return RoboOk;
        }
    }
    *clusterStart = lastCs;
    *clusterEnd = lastCe;
    return RoboOk;
}

Result UnicodeGraphemeCountUtf8(const char *src,
                                               size_t srcLen,
                                               UnicodeMode mode,
                                               size_t *count)
{
    UnicodeGraphemeIter it;
    size_t cs = 0;
    size_t ce = 0;
    size_t n = 0;
    Result r;

    if (!count) {
        return RoboInvalidArgument;
    }
    *count = 0;
    if (srcLen == 0u) {
        return RoboOk;
    }
    if (!src) {
        return RoboInvalidArgument;
    }

    UnicodeGraphemeIterInit(&it, src, srcLen, mode);
    while (it.byteOffset < srcLen && it.valid) {
        r = UnicodeGraphemeIterNext(&it, &cs, &ce);
        if (r != RoboOk) {
            return r;
        }
        if (cs >= srcLen && ce >= srcLen) {
            break;
        }
        n++;
    }
    *count = n;
    return RoboOk;
}

Result UnicodeGraphemeNext(const char *src,
                                         size_t srcLen,
                                         size_t byteOffset,
                                         UnicodeMode mode,
                                         size_t *clusterStart,
                                         size_t *clusterEnd)
{
    if (byteOffset > srcLen) {
        return RoboInvalidArgument;
    }
    return GraphemeNextCluster(
        src, srcLen, byteOffset, mode, clusterStart, clusterEnd);
}

Result UnicodeGraphemePrev(const char *src,
                                         size_t srcLen,
                                         size_t byteOffset,
                                         UnicodeMode mode,
                                         size_t *clusterStart,
                                         size_t *clusterEnd)
{
    return UnicodeGraphemeIterPrev(
        src, srcLen, byteOffset, mode, clusterStart, clusterEnd);
}
