/*
 * Unicode Sentence Boundaries — UAX #29 (Unicode 16.0.0).
 */

#include "RoboUnicode/RoboUnicodeSentence.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

const char *UnicodeSentenceUcdVersionString(void)
{
    return RoboSentenceUcdVersion;
}

static int SbIsIgnore(UnicodeSentenceBreak p)
{
    return p == RoboUSbExtend || p == RoboUSbFormat;
}

static int SbIsParasep(UnicodeSentenceBreak p)
{
    return p == RoboUSbSep || p == RoboUSbCr || p == RoboUSbLf;
}

static int SbIsSaterm(UnicodeSentenceBreak p)
{
    return p == RoboUSbSterm || p == RoboUSbAterm;
}

/*
 * UAX #29: skip Extend|Format except after sot / ParaSep.
 * Returns 1 if @p index (Extend|Format) should be skipped for rule matching.
 */
static int SbShouldSkipIgnore(const Uchar32 *cps, size_t index)
{
    size_t j = index;
    UnicodeSentenceBreak p;

    while (j > 0u) {
        j--;
        p = SentenceBreakProperty(cps[j]);
        if (SbIsParasep(p)) {
            return 0; /* after ParaSep — do not ignore */
        }
        if (SbIsIgnore(p)) {
            continue;
        }
        return 1; /* preceded by real content — ignore */
    }
    return 0; /* after sot — do not ignore */
}

/* Previous rule-relevant index strictly before @p pos, or (size_t)-1. */
static size_t SbPrev(const Uchar32 *cps, size_t pos)
{
    size_t i;
    if (pos == 0u) {
        return (size_t)-1;
    }
    i = pos;
    do {
        i--;
        if (!SbIsIgnore(SentenceBreakProperty(cps[i]))) {
            return i;
        }
        if (!SbShouldSkipIgnore(cps, i)) {
            return i;
        }
    } while (i > 0u);
    return (size_t)-1;
}

/* Next rule-relevant index at or after @p pos, or len if none. */
static size_t SbNext(const Uchar32 *cps, size_t len, size_t pos)
{
    size_t i = pos;
    while (i < len) {
        UnicodeSentenceBreak p = SentenceBreakProperty(cps[i]);
        if (!SbIsIgnore(p)) {
            return i;
        }
        if (!SbShouldSkipIgnore(cps, i)) {
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
static size_t SbMatchSatermCloseSp(const Uchar32 *cps, size_t end)
{
    size_t i = SbPrev(cps, end);
    UnicodeSentenceBreak p;

    if (i == (size_t)-1) {
        return (size_t)-1;
    }

    /* Sp* */
    while (i != (size_t)-1) {
        p = SentenceBreakProperty(cps[i]);
        if (p != RoboUSbSp) {
            break;
        }
        i = SbPrev(cps, i);
    }
    if (i == (size_t)-1) {
        return (size_t)-1;
    }

    /* Close* */
    while (i != (size_t)-1) {
        p = SentenceBreakProperty(cps[i]);
        if (p != RoboUSbClose) {
            break;
        }
        i = SbPrev(cps, i);
    }
    if (i == (size_t)-1) {
        return (size_t)-1;
    }

    p = SentenceBreakProperty(cps[i]);
    if (!SbIsSaterm(p)) {
        return (size_t)-1;
    }
    return i;
}

/* Like above but requires ATerm specifically (SB8). */
static size_t SbMatchAtermCloseSp(const Uchar32 *cps, size_t end)
{
    size_t a = SbMatchSatermCloseSp(cps, end);
    if (a == (size_t)-1) {
        return (size_t)-1;
    }
    if (SentenceBreakProperty(cps[a]) != RoboUSbAterm) {
        return (size_t)-1;
    }
    return a;
}

/* Match SATerm Close* ending before @p end (no Sp). Returns SATerm index. */
static size_t SbMatchSatermClose(const Uchar32 *cps, size_t end)
{
    size_t i = SbPrev(cps, end);
    UnicodeSentenceBreak p;

    if (i == (size_t)-1) {
        return (size_t)-1;
    }

    p = SentenceBreakProperty(cps[i]);
    if (p == RoboUSbSp || SbIsParasep(p)) {
        return (size_t)-1;
    }

    while (i != (size_t)-1) {
        p = SentenceBreakProperty(cps[i]);
        if (p != RoboUSbClose) {
            break;
        }
        i = SbPrev(cps, i);
    }
    if (i == (size_t)-1) {
        return (size_t)-1;
    }
    p = SentenceBreakProperty(cps[i]);
    if (!SbIsSaterm(p)) {
        return (size_t)-1;
    }
    return i;
}

/* SATerm Close* Sp* ParaSep? ending before @p end (SB11 left side). */
static int SbMatchSb11(const Uchar32 *cps, size_t end)
{
    size_t i = SbPrev(cps, end);
    UnicodeSentenceBreak p;

    if (i == (size_t)-1) {
        return 0;
    }

    p = SentenceBreakProperty(cps[i]);
    if (SbIsParasep(p)) {
        /* Optional ParaSep: require SATerm Close* Sp* immediately before it. */
        return SbMatchSatermCloseSp(cps, i) != (size_t)-1;
    }

    return SbMatchSatermCloseSp(cps, end) != (size_t)-1;
}

/* SB8 right: ( ¬(OLetter | Upper | Lower | ParaSep | SATerm) )* Lower */
static int SbMatchSb8Right(const Uchar32 *cps, size_t len, size_t index)
{
    size_t j = index;
    UnicodeSentenceBreak p;

    while (j < len) {
        j = SbNext(cps, len, j);
        if (j >= len) {
            return 0;
        }
        p = SentenceBreakProperty(cps[j]);
        if (p == RoboUSbLower) {
            return 1;
        }
        if (p == RoboUSbOletter || p == RoboUSbUpper || p == RoboUSbLower ||
            SbIsParasep(p) || SbIsSaterm(p)) {
            return 0;
        }
        j++;
    }
    return 0;
}

int IsUnicodeSentenceBreak(const Uchar32 *cps, size_t len, size_t index)
{
    UnicodeSentenceBreak lb;
    UnicodeSentenceBreak rb;
    size_t li;
    size_t p1;

    if (index == 0u) {
        return 1; /* SB1 */
    }
    if (index >= len || !cps) {
        return 1; /* SB2 */
    }

    lb = SentenceBreakProperty(cps[index - 1u]);
    rb = SentenceBreakProperty(cps[index]);

    /* SB3 */
    if (lb == RoboUSbCr && rb == RoboUSbLf) {
        return 0;
    }
    /* SB4 */
    if (SbIsParasep(lb)) {
        return 1;
    }
    /* SB5 */
    if (SbIsIgnore(rb)) {
        return 0;
    }

    li = SbPrev(cps, index);
    if (li == (size_t)-1) {
        /* Only ignores between sot and here — keep with following via SB998. */
        return 0;
    }
    lb = SentenceBreakProperty(cps[li]);
    rb = SentenceBreakProperty(cps[index]);

    /* SB6: ATerm × Numeric */
    if (lb == RoboUSbAterm && rb == RoboUSbNumeric) {
        return 0;
    }

    /* SB7: (Upper | Lower) ATerm × Upper */
    if (rb == RoboUSbUpper && lb == RoboUSbAterm) {
        p1 = SbPrev(cps, li);
        if (p1 != (size_t)-1) {
            UnicodeSentenceBreak pp = SentenceBreakProperty(cps[p1]);
            if (pp == RoboUSbUpper || pp == RoboUSbLower) {
                return 0;
            }
        }
    }

    /* SB8: ATerm Close* Sp* × (...)* Lower */
    if (SbMatchAtermCloseSp(cps, index) != (size_t)-1) {
        if (SbMatchSb8Right(cps, len, index)) {
            return 0;
        }
    }

    /* SB8a: SATerm Close* Sp* × (SContinue | SATerm) */
    if (SbMatchSatermCloseSp(cps, index) != (size_t)-1) {
        if (rb == RoboUSbScontinue || SbIsSaterm(rb)) {
            return 0;
        }
    }

    /* SB9: SATerm Close* × (Close | Sp | ParaSep) */
    if (SbMatchSatermClose(cps, index) != (size_t)-1) {
        if (rb == RoboUSbClose || rb == RoboUSbSp || SbIsParasep(rb)) {
            return 0;
        }
    }

    /* SB10: SATerm Close* Sp* × (Sp | ParaSep) */
    if (SbMatchSatermCloseSp(cps, index) != (size_t)-1) {
        if (rb == RoboUSbSp || SbIsParasep(rb)) {
            return 0;
        }
    }

    /* SB11: SATerm Close* Sp* ParaSep? ÷ */
    if (SbMatchSb11(cps, index)) {
        return 1;
    }

    /* SB998 */
    return 0;
}

static Result SentenceDecodeAt(const char *src,
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

static Result SentenceNextSegment(const char *src,
                                                size_t srcLen,
                                                size_t byteOffset,
                                                UnicodeMode mode,
                                                size_t *sentenceStart,
                                                size_t *sentenceEnd)
{
    Uchar32 stackCps[128];
    size_t stackEnds[128];
    Uchar32 *cps = stackCps;
    size_t *cpEnds = stackEnds;
    size_t cap = 128u;
    size_t n = 0;
    size_t off = byteOffset;
    Result r;

    if (byteOffset >= srcLen) {
        *sentenceStart = srcLen;
        *sentenceEnd = srcLen;
        return RoboOk;
    }
    *sentenceStart = byteOffset;

    for (;;) {
        Uchar32 cp = 0;
        size_t bend = off;
        size_t peekN;
        size_t peekOff;
        size_t i;

        if (off >= srcLen) {
            break;
        }
        r = SentenceDecodeAt(src, srcLen, off, mode, &cp, &bend);
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
            /*
             * SB8 may need look-ahead past the candidate boundary until Lower
             * or a stopping class. Peek a window of following scalars.
             */
            Uchar32 peekStack[256];
            Uchar32 *peek = peekStack;
            size_t peekCap = 256u;
            int heapPeek = 0;
            const size_t lookAhead = 64u;

            if (n + 1u + lookAhead > peekCap) {
                peekCap = n + 1u + lookAhead + 32u;
                peek = (Uchar32 *)Malloc(peekCap * sizeof(Uchar32));
                if (!peek) {
                    if (cps != stackCps) {
                        RoboFree(cps);
                    }
                    if (cpEnds != stackEnds) {
                        RoboFree(cpEnds);
                    }
                    return RoboOutOfMemory;
                }
                heapPeek = 1;
            }
            for (i = 0; i < n; i++) {
                peek[i] = cps[i];
            }
            peek[n] = cp;
            peekN = n + 1u;
            peekOff = bend;
            while (peekN < n + 1u + lookAhead && peekOff < srcLen) {
                Uchar32 pcp = 0;
                size_t pend = peekOff;
                r = SentenceDecodeAt(src, srcLen, peekOff, mode, &pcp, &pend);
                if (r != RoboOk) {
                    if (heapPeek) {
                        RoboFree(peek);
                    }
                    if (cps != stackCps) {
                        RoboFree(cps);
                    }
                    if (cpEnds != stackEnds) {
                        RoboFree(cpEnds);
                    }
                    return r;
                }
                if (peekN >= peekCap) {
                    break;
                }
                peek[peekN++] = pcp;
                peekOff = pend;
            }
            if (IsUnicodeSentenceBreak(peek, peekN, n)) {
                if (heapPeek) {
                    RoboFree(peek);
                }
                break;
            }
            if (heapPeek) {
                RoboFree(peek);
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
            memcpy(nc, cps, n * sizeof(Uchar32));
            memcpy(ne, cpEnds, n * sizeof(size_t));
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

    *sentenceEnd = (n == 0u) ? byteOffset : cpEnds[n - 1u];
    if (cps != stackCps) {
        RoboFree(cps);
    }
    if (cpEnds != stackEnds) {
        RoboFree(cpEnds);
    }
    return RoboOk;
}

Result UnicodeSentenceIterInit(UnicodeSentenceIter *it,
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
    it->sentenceIndex = 0;
    it->valid = 1;
    return RoboOk;
}

Result UnicodeSentenceIterNext(UnicodeSentenceIter *it,
                                              size_t *sentenceStart,
                                              size_t *sentenceEnd)
{
    Result r;

    if (!it || !sentenceStart || !sentenceEnd) {
        return RoboInvalidArgument;
    }
    if (!it->valid) {
        return RoboParseError;
    }
    r = SentenceNextSegment(
        it->src, it->srcLen, it->byteOffset, it->mode, sentenceStart, sentenceEnd);
    if (r != RoboOk) {
        it->valid = 0;
        return r;
    }
    it->byteOffset = *sentenceEnd;
    if (*sentenceStart < it->srcLen && *sentenceStart != *sentenceEnd) {
        it->sentenceIndex++;
    }
    return RoboOk;
}

Result UnicodeSentenceNext(const char *src,
                                         size_t srcLen,
                                         size_t byteOffset,
                                         UnicodeMode mode,
                                         size_t *sentenceStart,
                                         size_t *sentenceEnd)
{
    if (byteOffset > srcLen) {
        return RoboInvalidArgument;
    }
    return SentenceNextSegment(
        src, srcLen, byteOffset, mode, sentenceStart, sentenceEnd);
}

Result UnicodeSentencePrev(const char *src,
                                         size_t srcLen,
                                         size_t byteOffset,
                                         UnicodeMode mode,
                                         size_t *sentenceStart,
                                         size_t *sentenceEnd)
{
    UnicodeSentenceIter it;
    size_t ss = 0;
    size_t se = 0;
    size_t lastSs = 0;
    size_t lastSe = 0;

    if (!sentenceStart || !sentenceEnd) {
        return RoboInvalidArgument;
    }
    if (byteOffset > srcLen) {
        return RoboInvalidArgument;
    }
    if (byteOffset == 0u) {
        *sentenceStart = 0;
        *sentenceEnd = 0;
        return RoboOk;
    }

    UnicodeSentenceIterInit(&it, src, srcLen, mode);
    while (it.byteOffset < byteOffset && it.valid) {
        lastSs = ss;
        lastSe = se;
        if (UnicodeSentenceIterNext(&it, &ss, &se) != RoboOk) {
            return RoboParseError;
        }
        if (se >= byteOffset) {
            if (ss < byteOffset) {
                *sentenceStart = ss;
                *sentenceEnd = se;
            } else {
                *sentenceStart = lastSs;
                *sentenceEnd = lastSe;
            }
            return RoboOk;
        }
    }
    *sentenceStart = lastSs;
    *sentenceEnd = lastSe;
    return RoboOk;
}

Result UnicodeSentenceCountUtf8(const char *src,
                                               size_t srcLen,
                                               UnicodeMode mode,
                                               size_t *count)
{
    UnicodeSentenceIter it;
    size_t ss = 0;
    size_t se = 0;
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

    UnicodeSentenceIterInit(&it, src, srcLen, mode);
    while (it.byteOffset < srcLen && it.valid) {
        r = UnicodeSentenceIterNext(&it, &ss, &se);
        if (r != RoboOk) {
            return r;
        }
        if (ss >= srcLen && se >= srcLen) {
            break;
        }
        n++;
    }
    *count = n;
    return RoboOk;
}
