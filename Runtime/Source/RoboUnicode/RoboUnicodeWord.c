/*
 * Unicode Word Boundaries — UAX #29 (Unicode 16.0.0).
 */

#include "RoboUnicode/RoboUnicodeWord.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <string.h>

const char *UnicodeWordUcdVersionString(void)
{
    return RoboWordUcdVersion;
}

static int WbIsAhletter(UnicodeWordBreak p)
{
    return p == RoboUWbAletter || p == RoboUWbHebrewLetter;
}

static int WbIsMidnumletq(UnicodeWordBreak p)
{
    return p == RoboUWbMidnumlet || p == RoboUWbSingleQuote;
}

static int WbIsIgnore(UnicodeWordBreak p)
{
    return p == RoboUWbExtend || p == RoboUWbFormat || p == RoboUWbZwj;
}

static int WbIsSep(UnicodeWordBreak p)
{
    return p == RoboUWbNewline || p == RoboUWbCr || p == RoboUWbLf;
}

/* Previous non-ignored index strictly before @p pos, or (size_t)-1. */
static size_t WbPrev(const Uchar32 *cps, size_t pos)
{
    size_t i;
    if (pos == 0u) {
        return (size_t)-1;
    }
    i = pos;
    do {
        i--;
        if (!WbIsIgnore(WordBreakProperty(cps[i]))) {
            return i;
        }
    } while (i > 0u);
    return (size_t)-1;
}

/* Next non-ignored index at or after @p pos, or len if none. */
static size_t WbNext(const Uchar32 *cps, size_t len, size_t pos)
{
    size_t i = pos;
    while (i < len && WbIsIgnore(WordBreakProperty(cps[i]))) {
        i++;
    }
    return i;
}

static int WbRiOddBefore(const Uchar32 *cps, size_t index)
{
    size_t j;
    size_t count = 0;
    /* Count consecutive RI (skipping ignores) immediately before index. */
    j = index;
    while (j > 0u) {
        size_t p = WbPrev(cps, j);
        if (p == (size_t)-1) {
            break;
        }
        if (WordBreakProperty(cps[p]) != RoboUWbRegionalIndicator) {
            break;
        }
        count++;
        j = p;
    }
    return (count % 2u) == 1u;
}

int IsUnicodeWordBreak(const Uchar32 *cps, size_t len, size_t index)
{
    UnicodeWordBreak lb;
    UnicodeWordBreak rb;
    size_t li;
    size_t ri;
    size_t n1;
    UnicodeWordBreak p1;

    if (index == 0u) {
        return 1; /* WB1 */
    }
    if (index >= len || !cps) {
        return 1; /* WB2 */
    }

    lb = WordBreakProperty(cps[index - 1u]);
    rb = WordBreakProperty(cps[index]);

    /* WB3 */
    if (lb == RoboUWbCr && rb == RoboUWbLf) {
        return 0;
    }
    /* WB3a */
    if (WbIsSep(lb)) {
        return 1;
    }
    /* WB3b */
    if (WbIsSep(rb)) {
        return 1;
    }
    /* WB3c */
    if (lb == RoboUWbZwj && UnicodeIsExtendedPictographic(cps[index])) {
        return 0;
    }
    /* WB3d */
    if (lb == RoboUWbWsegspace && rb == RoboUWbWsegspace) {
        return 0;
    }
    /* WB4 — Any × (Extend | Format | ZWJ) except after sep (already handled) */
    if (WbIsIgnore(rb)) {
        return 0;
    }

    /* Effective left (skip ignores) and right (already non-ignore). */
    li = WbPrev(cps, index);
    if (li == (size_t)-1) {
        return 1; /* only ignores between sot and here → break (shouldn't usually happen) */
    }
    lb = WordBreakProperty(cps[li]);
    rb = WordBreakProperty(cps[index]);
    ri = index;

    /* WB5 */
    if (WbIsAhletter(lb) && WbIsAhletter(rb)) {
        return 0;
    }

    /* WB6: AHLetter × (MidLetter | MidNumLetQ) AHLetter */
    if (WbIsAhletter(lb) &&
        (rb == RoboUWbMidletter || WbIsMidnumletq(rb))) {
        n1 = WbNext(cps, len, ri + 1u);
        if (n1 < len && WbIsAhletter(WordBreakProperty(cps[n1]))) {
            return 0;
        }
    }

    /* WB7: AHLetter (MidLetter | MidNumLetQ) × AHLetter */
    if (WbIsAhletter(rb)) {
        p1 = lb;
        if (p1 == RoboUWbMidletter || WbIsMidnumletq(p1)) {
            n1 = WbPrev(cps, li);
            if (n1 != (size_t)-1 &&
                WbIsAhletter(WordBreakProperty(cps[n1]))) {
                return 0;
            }
        }
    }

    /* WB7a */
    if (lb == RoboUWbHebrewLetter && rb == RoboUWbSingleQuote) {
        return 0;
    }

    /* WB7b: Hebrew_Letter × Double_Quote Hebrew_Letter */
    if (lb == RoboUWbHebrewLetter && rb == RoboUWbDoubleQuote) {
        n1 = WbNext(cps, len, ri + 1u);
        if (n1 < len &&
            WordBreakProperty(cps[n1]) == RoboUWbHebrewLetter) {
            return 0;
        }
    }

    /* WB7c: Hebrew_Letter Double_Quote × Hebrew_Letter */
    if (rb == RoboUWbHebrewLetter && lb == RoboUWbDoubleQuote) {
        n1 = WbPrev(cps, li);
        if (n1 != (size_t)-1 &&
            WordBreakProperty(cps[n1]) == RoboUWbHebrewLetter) {
            return 0;
        }
    }

    /* WB8 */
    if (lb == RoboUWbNumeric && rb == RoboUWbNumeric) {
        return 0;
    }
    /* WB9 */
    if (WbIsAhletter(lb) && rb == RoboUWbNumeric) {
        return 0;
    }
    /* WB10 */
    if (lb == RoboUWbNumeric && WbIsAhletter(rb)) {
        return 0;
    }

    /* WB11: Numeric (MidNum | MidNumLetQ) × Numeric */
    if (rb == RoboUWbNumeric &&
        (lb == RoboUWbMidnum || WbIsMidnumletq(lb))) {
        n1 = WbPrev(cps, li);
        if (n1 != (size_t)-1 &&
            WordBreakProperty(cps[n1]) == RoboUWbNumeric) {
            return 0;
        }
    }

    /* WB12: Numeric × (MidNum | MidNumLetQ) Numeric */
    if (lb == RoboUWbNumeric &&
        (rb == RoboUWbMidnum || WbIsMidnumletq(rb))) {
        n1 = WbNext(cps, len, ri + 1u);
        if (n1 < len &&
            WordBreakProperty(cps[n1]) == RoboUWbNumeric) {
            return 0;
        }
    }

    /* WB13 */
    if (lb == RoboUWbKatakana && rb == RoboUWbKatakana) {
        return 0;
    }

    /* WB13a */
    if ((WbIsAhletter(lb) || lb == RoboUWbNumeric ||
         lb == RoboUWbKatakana || lb == RoboUWbExtendnumlet) &&
        rb == RoboUWbExtendnumlet) {
        return 0;
    }

    /* WB13b */
    if (lb == RoboUWbExtendnumlet &&
        (WbIsAhletter(rb) || rb == RoboUWbNumeric ||
         rb == RoboUWbKatakana)) {
        return 0;
    }

    /* WB15 / WB16 — RI × RI with odd RI run before */
    if (lb == RoboUWbRegionalIndicator &&
        rb == RoboUWbRegionalIndicator) {
        if (WbRiOddBefore(cps, index)) {
            return 0;
        }
    }

    /* WB999 */
    return 1;
}

static Result WordDecodeAt(const char *src,
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

static Result WordNextSegment(const char *src,
                                            size_t srcLen,
                                            size_t byteOffset,
                                            UnicodeMode mode,
                                            size_t *wordStart,
                                            size_t *wordEnd)
{
    Uchar32 stackCps[96];
    size_t stackEnds[96];
    Uchar32 *cps = stackCps;
    size_t *cpEnds = stackEnds;
    size_t cap = 96u;
    size_t n = 0;
    size_t off = byteOffset;
    Result r;

    if (byteOffset >= srcLen) {
        *wordStart = srcLen;
        *wordEnd = srcLen;
        return RoboOk;
    }
    *wordStart = byteOffset;

    for (;;) {
        Uchar32 cp = 0;
        size_t bend = off;
        size_t peekN;
        size_t peekOff;
        size_t i;

        if (off >= srcLen) {
            break;
        }
        r = WordDecodeAt(src, srcLen, off, mode, &cp, &bend);
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
             * Look-ahead: WB6/WB7/WB7b/WB11/WB12 need characters after the
             * candidate boundary. Peek additional scalars from the source.
             */
            Uchar32 peekStack[128];
            Uchar32 *peek = peekStack;
            size_t peekCap = 128u;
            int heapPeek = 0;

            if (n + 1u + 16u > peekCap) {
                peekCap = n + 1u + 32u;
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
            while (peekN < n + 1u + 16u && peekOff < srcLen) {
                Uchar32 pcp = 0;
                size_t pend = peekOff;
                r = WordDecodeAt(src, srcLen, peekOff, mode, &pcp, &pend);
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
            if (IsUnicodeWordBreak(peek, peekN, n)) {
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

    *wordEnd = (n == 0u) ? byteOffset : cpEnds[n - 1u];
    if (cps != stackCps) {
        RoboFree(cps);
    }
    if (cpEnds != stackEnds) {
        RoboFree(cpEnds);
    }
    return RoboOk;
}

Result UnicodeWordIterInit(UnicodeWordIter *it,
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
    it->wordIndex = 0;
    it->valid = 1;
    return RoboOk;
}

Result UnicodeWordIterNext(UnicodeWordIter *it,
                                          size_t *wordStart,
                                          size_t *wordEnd)
{
    Result r;

    if (!it || !wordStart || !wordEnd) {
        return RoboInvalidArgument;
    }
    if (!it->valid) {
        return RoboParseError;
    }
    r = WordNextSegment(
        it->src, it->srcLen, it->byteOffset, it->mode, wordStart, wordEnd);
    if (r != RoboOk) {
        it->valid = 0;
        return r;
    }
    it->byteOffset = *wordEnd;
    if (*wordStart < it->srcLen && *wordStart != *wordEnd) {
        it->wordIndex++;
    }
    return RoboOk;
}

Result UnicodeWordNext(const char *src,
                                     size_t srcLen,
                                     size_t byteOffset,
                                     UnicodeMode mode,
                                     size_t *wordStart,
                                     size_t *wordEnd)
{
    if (byteOffset > srcLen) {
        return RoboInvalidArgument;
    }
    return WordNextSegment(
        src, srcLen, byteOffset, mode, wordStart, wordEnd);
}

Result UnicodeWordPrev(const char *src,
                                     size_t srcLen,
                                     size_t byteOffset,
                                     UnicodeMode mode,
                                     size_t *wordStart,
                                     size_t *wordEnd)
{
    UnicodeWordIter it;
    size_t ws = 0;
    size_t we = 0;
    size_t lastWs = 0;
    size_t lastWe = 0;

    if (!wordStart || !wordEnd) {
        return RoboInvalidArgument;
    }
    if (byteOffset > srcLen) {
        return RoboInvalidArgument;
    }
    if (byteOffset == 0u) {
        *wordStart = 0;
        *wordEnd = 0;
        return RoboOk;
    }

    UnicodeWordIterInit(&it, src, srcLen, mode);
    while (it.byteOffset < byteOffset && it.valid) {
        lastWs = ws;
        lastWe = we;
        if (UnicodeWordIterNext(&it, &ws, &we) != RoboOk) {
            return RoboParseError;
        }
        if (we >= byteOffset) {
            if (ws < byteOffset) {
                *wordStart = ws;
                *wordEnd = we;
            } else {
                *wordStart = lastWs;
                *wordEnd = lastWe;
            }
            return RoboOk;
        }
    }
    *wordStart = lastWs;
    *wordEnd = lastWe;
    return RoboOk;
}

Result UnicodeWordCountUtf8(const char *src,
                                           size_t srcLen,
                                           UnicodeMode mode,
                                           size_t *count)
{
    UnicodeWordIter it;
    size_t ws = 0;
    size_t we = 0;
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

    UnicodeWordIterInit(&it, src, srcLen, mode);
    while (it.byteOffset < srcLen && it.valid) {
        r = UnicodeWordIterNext(&it, &ws, &we);
        if (r != RoboOk) {
            return r;
        }
        if (ws >= srcLen && we >= srcLen) {
            break;
        }
        n++;
    }
    *count = n;
    return RoboOk;
}
