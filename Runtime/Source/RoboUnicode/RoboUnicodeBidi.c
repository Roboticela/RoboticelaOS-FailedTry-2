/*
 * Unicode Bidirectional Algorithm — UAX #9 (Unicode 16.0.0).
 *
 * Faithful port of Tools/ucd/bidi_ref.py (BidiCharacterTest 91707/91707).
 */

#include "RoboUnicode/RoboUnicodeBidi.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

#define RoboBidiMaxDepth 125
#define RoboBidiMaxBracketStack 63
#define RoboBidiOverrideN 0
#define RoboBidiOverrideL 1
#define RoboBidiOverrideR 2

typedef struct BidiStackEntry {
    uint8_t level;
    uint8_t overrideStatus; /* N / L / R */
    uint8_t isolate;
} BidiStackEntry;

typedef struct BidiIrs {
    size_t *indices;
    size_t count;
    uint8_t level;
    uint8_t sos; /* RoboUBcL or RoboUBcR */
    uint8_t eos;
    int prevLevel; /* -1 if none */
    int nextLevel;
    int useParaEos;
} BidiIrs;

const char *UnicodeBidiUcdVersionString(void)
{
    return RoboBidiUcdVersion;
}

UnicodeBidiClass MakeUnicodeBidiClass(Uchar32 cp)
{
    return BidiClassProperty(cp);
}

static int BidiIsRemove(UnicodeBidiClass t)
{
    return t == RoboUBcRle || t == RoboUBcLre || t == RoboUBcRlo ||
           t == RoboUBcLro || t == RoboUBcPdf || t == RoboUBcBn;
}

static int BidiIsIsolateInit(UnicodeBidiClass t)
{
    return t == RoboUBcLri || t == RoboUBcRli || t == RoboUBcFsi;
}

static int BidiIsNi(UnicodeBidiClass t)
{
    return t == RoboUBcB || t == RoboUBcS || t == RoboUBcWs ||
           t == RoboUBcOn || t == RoboUBcFsi || t == RoboUBcLri ||
           t == RoboUBcRli || t == RoboUBcPdi;
}

static int BidiIsStrongP2(UnicodeBidiClass t)
{
    return t == RoboUBcL || t == RoboUBcR || t == RoboUBcAl;
}

static int BidiLeastOddGt(int level)
{
    return ((level % 2) == 0) ? level + 1 : level + 2;
}

static int BidiLeastEvenGt(int level)
{
    return ((level % 2) == 1) ? level + 1 : level + 2;
}

static uint8_t BidiSosEosType(int levelA, int levelB)
{
    int m = levelA > levelB ? levelA : levelB;
    return ((m % 2) == 1) ? (uint8_t)RoboUBcR : (uint8_t)RoboUBcL;
}

static int BidiBracketsEqual(uint32_t a, uint32_t b)
{
    if (a == b) {
        return 1;
    }
    /* U+232A RIGHT-POINTING ANGLE BRACKET <-> U+3009 RIGHT ANGLE BRACKET */
    if ((a == 0x232Au && b == 0x3009u) || (a == 0x3009u && b == 0x232Au)) {
        return 1;
    }
    return 0;
}

static void BidiMatchingPdis(const uint8_t *types, size_t n, int *match)
{
    size_t i;
    for (i = 0; i < n; i++) {
        match[i] = -1;
    }
    for (i = 0; i < n; i++) {
        size_t j;
        int depth;
        if (!BidiIsIsolateInit((UnicodeBidiClass)types[i])) {
            continue;
        }
        depth = 1;
        for (j = i + 1u; j < n; j++) {
            UnicodeBidiClass tj = (UnicodeBidiClass)types[j];
            if (BidiIsIsolateInit(tj)) {
                depth++;
            } else if (tj == RoboUBcPdi) {
                depth--;
                if (depth == 0) {
                    match[i] = (int)j;
                    break;
                }
            }
        }
    }
}

static void BidiMatchingInitiators(const int *matchingPdi, size_t n, int *inv)
{
    size_t i;
    for (i = 0; i < n; i++) {
        inv[i] = -1;
    }
    for (i = 0; i < n; i++) {
        if (matchingPdi[i] >= 0) {
            inv[matchingPdi[i]] = (int)i;
        }
    }
}

static uint8_t BidiDetermineParagraphLevel(const uint8_t *types,
                                                   size_t start,
                                                   size_t end)
{
    size_t i = start;
    while (i < end) {
        UnicodeBidiClass t = (UnicodeBidiClass)types[i];
        if (BidiIsIsolateInit(t)) {
            int depth = 1;
            i++;
            while (i < end) {
                UnicodeBidiClass ti = (UnicodeBidiClass)types[i];
                if (BidiIsIsolateInit(ti)) {
                    depth++;
                } else if (ti == RoboUBcPdi) {
                    depth--;
                    if (depth == 0) {
                        i++;
                        break;
                    }
                }
                i++;
            }
            continue;
        }
        if (BidiIsStrongP2(t)) {
            return (t == RoboUBcR || t == RoboUBcAl) ? 1u : 0u;
        }
        i++;
    }
    return 0u;
}

uint8_t UnicodeBidiParagraphDirection(const Uchar32 *cps, size_t len)
{
    size_t i;
    uint8_t *types;
    uint8_t pl;

    if (!cps || len == 0u) {
        return 0u;
    }
    types = (uint8_t *)Malloc(len);
    if (!types) {
        /* Fallback: scan without heap for first strong (isolate-aware). */
        size_t j = 0;
        while (j < len) {
            UnicodeBidiClass t = BidiClassProperty(cps[j]);
            if (BidiIsIsolateInit(t)) {
                int depth = 1;
                j++;
                while (j < len) {
                    UnicodeBidiClass tj = BidiClassProperty(cps[j]);
                    if (BidiIsIsolateInit(tj)) {
                        depth++;
                    } else if (tj == RoboUBcPdi) {
                        depth--;
                        if (depth == 0) {
                            j++;
                            break;
                        }
                    }
                    j++;
                }
                continue;
            }
            if (BidiIsStrongP2(t)) {
                return (t == RoboUBcR || t == RoboUBcAl) ? 1u : 0u;
            }
            j++;
        }
        return 0u;
    }
    for (i = 0; i < len; i++) {
        types[i] = (uint8_t)BidiClassProperty(cps[i]);
    }
    pl = BidiDetermineParagraphLevel(types, 0, len);
    RoboFree(types);
    return pl;
}

static void BidiResolveExplicit(uint8_t *types,
                                       size_t n,
                                       uint8_t paragraphLevel,
                                       const int *matchingPdi,
                                       int16_t *levels)
{
    BidiStackEntry stack[RoboBidiMaxDepth + 2];
    size_t stackLen = 1;
    int overflowIsolate = 0;
    int overflowEmbedding = 0;
    int validIsolate = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        levels[i] = -1;
    }

    stack[0].level = paragraphLevel;
    stack[0].overrideStatus = RoboBidiOverrideN;
    stack[0].isolate = 0;

    i = 0;
    while (i < n) {
        UnicodeBidiClass t = (UnicodeBidiClass)types[i];
        BidiStackEntry *top = &stack[stackLen - 1u];
        int newLevel;

        if (t == RoboUBcRle) {
            newLevel = BidiLeastOddGt((int)top->level);
            if (newLevel <= RoboBidiMaxDepth && overflowIsolate == 0 &&
                overflowEmbedding == 0) {
                stack[stackLen].level = (uint8_t)newLevel;
                stack[stackLen].overrideStatus = RoboBidiOverrideN;
                stack[stackLen].isolate = 0;
                stackLen++;
            } else if (overflowIsolate == 0) {
                overflowEmbedding++;
            }
            i++;
            continue;
        }
        if (t == RoboUBcLre) {
            newLevel = BidiLeastEvenGt((int)top->level);
            if (newLevel <= RoboBidiMaxDepth && overflowIsolate == 0 &&
                overflowEmbedding == 0) {
                stack[stackLen].level = (uint8_t)newLevel;
                stack[stackLen].overrideStatus = RoboBidiOverrideN;
                stack[stackLen].isolate = 0;
                stackLen++;
            } else if (overflowIsolate == 0) {
                overflowEmbedding++;
            }
            i++;
            continue;
        }
        if (t == RoboUBcRlo) {
            newLevel = BidiLeastOddGt((int)top->level);
            if (newLevel <= RoboBidiMaxDepth && overflowIsolate == 0 &&
                overflowEmbedding == 0) {
                stack[stackLen].level = (uint8_t)newLevel;
                stack[stackLen].overrideStatus = RoboBidiOverrideR;
                stack[stackLen].isolate = 0;
                stackLen++;
            } else if (overflowIsolate == 0) {
                overflowEmbedding++;
            }
            i++;
            continue;
        }
        if (t == RoboUBcLro) {
            newLevel = BidiLeastEvenGt((int)top->level);
            if (newLevel <= RoboBidiMaxDepth && overflowIsolate == 0 &&
                overflowEmbedding == 0) {
                stack[stackLen].level = (uint8_t)newLevel;
                stack[stackLen].overrideStatus = RoboBidiOverrideL;
                stack[stackLen].isolate = 0;
                stackLen++;
            } else if (overflowIsolate == 0) {
                overflowEmbedding++;
            }
            i++;
            continue;
        }
        if (BidiIsIsolateInit(t)) {
            UnicodeBidiClass effective = t;
            if (t == RoboUBcFsi) {
                int pdi = matchingPdi[i];
                size_t end = (pdi >= 0) ? (size_t)pdi : n;
                uint8_t pl =
                    BidiDetermineParagraphLevel(types, i + 1u, end);
                effective = (pl == 1u) ? RoboUBcRli : RoboUBcLri;
            }
            levels[i] = (int16_t)top->level;
            if (top->overrideStatus == RoboBidiOverrideL) {
                types[i] = (uint8_t)RoboUBcL;
            } else if (top->overrideStatus == RoboBidiOverrideR) {
                types[i] = (uint8_t)RoboUBcR;
            }
            if (effective == RoboUBcRli) {
                newLevel = BidiLeastOddGt((int)top->level);
            } else {
                newLevel = BidiLeastEvenGt((int)top->level);
            }
            if (newLevel <= RoboBidiMaxDepth && overflowIsolate == 0 &&
                overflowEmbedding == 0) {
                validIsolate++;
                stack[stackLen].level = (uint8_t)newLevel;
                stack[stackLen].overrideStatus = RoboBidiOverrideN;
                stack[stackLen].isolate = 1;
                stackLen++;
            } else {
                overflowIsolate++;
            }
            i++;
            continue;
        }
        if (t == RoboUBcPdi) {
            if (overflowIsolate > 0) {
                overflowIsolate--;
            } else if (validIsolate == 0) {
                /* unmatched PDI */
            } else {
                overflowEmbedding = 0;
                while (stackLen > 0u && !stack[stackLen - 1u].isolate) {
                    stackLen--;
                }
                if (stackLen > 0u && stack[stackLen - 1u].isolate) {
                    stackLen--;
                    validIsolate--;
                }
            }
            top = &stack[stackLen - 1u];
            levels[i] = (int16_t)top->level;
            if (top->overrideStatus == RoboBidiOverrideL) {
                types[i] = (uint8_t)RoboUBcL;
            } else if (top->overrideStatus == RoboBidiOverrideR) {
                types[i] = (uint8_t)RoboUBcR;
            }
            i++;
            continue;
        }
        if (t == RoboUBcPdf) {
            if (overflowIsolate > 0) {
                /* ignore */
            } else if (overflowEmbedding > 0) {
                overflowEmbedding--;
            } else if (stackLen >= 2u && !stack[stackLen - 1u].isolate) {
                stackLen--;
            }
            i++;
            continue;
        }
        if (t == RoboUBcB) {
            levels[i] = (int16_t)paragraphLevel;
            stackLen = 1;
            stack[0].level = paragraphLevel;
            stack[0].overrideStatus = RoboBidiOverrideN;
            stack[0].isolate = 0;
            overflowIsolate = 0;
            overflowEmbedding = 0;
            validIsolate = 0;
            i++;
            continue;
        }
        if (t == RoboUBcBn) {
            i++;
            continue;
        }
        /* X6 */
        levels[i] = (int16_t)top->level;
        if (top->overrideStatus == RoboBidiOverrideL) {
            types[i] = (uint8_t)RoboUBcL;
        } else if (top->overrideStatus == RoboBidiOverrideR) {
            types[i] = (uint8_t)RoboUBcR;
        }
        i++;
    }
}

static void BidiApplyW(uint8_t *types, const size_t *indices, size_t m, uint8_t sos)
{
    size_t k;
    uint8_t *change;

    if (m == 0u) {
        return;
    }

#define T(k) (((k) == (size_t)-1) ? (sos) : types[indices[(k)]])
#define SET(k, v) (types[indices[(k)]] = (uint8_t)(v))

    /* W1 */
    for (k = 0; k < m; k++) {
        if ((UnicodeBidiClass)T(k) != RoboUBcNsm) {
            continue;
        }
        if (k == 0u) {
            SET(k, sos);
        } else {
            UnicodeBidiClass prev = (UnicodeBidiClass)T(k - 1u);
            if (BidiIsIsolateInit(prev) || prev == RoboUBcPdi) {
                SET(k, RoboUBcOn);
            } else {
                SET(k, prev);
            }
        }
    }

    /* W2 */
    for (k = 0; k < m; k++) {
        size_t j;
        uint8_t strong;
        if ((UnicodeBidiClass)T(k) != RoboUBcEn) {
            continue;
        }
        if (k == 0u) {
            strong = sos;
        } else {
            j = k - 1u;
            for (;;) {
                UnicodeBidiClass tj = (UnicodeBidiClass)T(j);
                if (tj == RoboUBcR || tj == RoboUBcL || tj == RoboUBcAl) {
                    strong = (uint8_t)tj;
                    break;
                }
                if (j == 0u) {
                    strong = sos;
                    break;
                }
                j--;
            }
        }
        if (strong == (uint8_t)RoboUBcAl) {
            SET(k, RoboUBcAn);
        }
    }

    /* W3 */
    for (k = 0; k < m; k++) {
        if ((UnicodeBidiClass)T(k) == RoboUBcAl) {
            SET(k, RoboUBcR);
        }
    }

    /* W4 */
    for (k = 1; k + 1u < m; k++) {
        UnicodeBidiClass t = (UnicodeBidiClass)T(k);
        if (t == RoboUBcEs) {
            if ((UnicodeBidiClass)T(k - 1u) == RoboUBcEn &&
                (UnicodeBidiClass)T(k + 1u) == RoboUBcEn) {
                SET(k, RoboUBcEn);
            }
        } else if (t == RoboUBcCs) {
            UnicodeBidiClass left = (UnicodeBidiClass)T(k - 1u);
            UnicodeBidiClass right = (UnicodeBidiClass)T(k + 1u);
            if (left == RoboUBcEn && right == RoboUBcEn) {
                SET(k, RoboUBcEn);
            } else if (left == RoboUBcAn && right == RoboUBcAn) {
                SET(k, RoboUBcAn);
            }
        }
    }

    /* W5 */
    change = (uint8_t *)RoboCalloc(m, 1u);
    if (change) {
        for (k = 0; k < m; k++) {
            size_t j;
            if ((UnicodeBidiClass)T(k) != RoboUBcEt) {
                continue;
            }
            j = k;
            while (j > 0u && (UnicodeBidiClass)T(j - 1u) == RoboUBcEt) {
                j--;
            }
            if (j > 0u && (UnicodeBidiClass)T(j - 1u) == RoboUBcEn) {
                change[k] = 1;
                continue;
            }
            j = k + 1u;
            while (j < m && (UnicodeBidiClass)T(j) == RoboUBcEt) {
                j++;
            }
            if (j < m && (UnicodeBidiClass)T(j) == RoboUBcEn) {
                change[k] = 1;
            }
        }
        for (k = 0; k < m; k++) {
            if (change[k]) {
                SET(k, RoboUBcEn);
            }
        }
        RoboFree(change);
    } else {
        /* OOM: still apply W5 with inline scan (slower, no alloc). */
        for (k = 0; k < m; k++) {
            size_t j;
            int hit = 0;
            if ((UnicodeBidiClass)T(k) != RoboUBcEt) {
                continue;
            }
            j = k;
            while (j > 0u && (UnicodeBidiClass)T(j - 1u) == RoboUBcEt) {
                j--;
            }
            if (j > 0u && (UnicodeBidiClass)T(j - 1u) == RoboUBcEn) {
                hit = 1;
            } else {
                j = k + 1u;
                while (j < m && (UnicodeBidiClass)T(j) == RoboUBcEt) {
                    j++;
                }
                if (j < m && (UnicodeBidiClass)T(j) == RoboUBcEn) {
                    hit = 1;
                }
            }
            if (hit) {
                SET(k, RoboUBcEn);
            }
        }
    }

    /* W6 */
    for (k = 0; k < m; k++) {
        UnicodeBidiClass t = (UnicodeBidiClass)T(k);
        if (t == RoboUBcEs || t == RoboUBcCs || t == RoboUBcEt) {
            SET(k, RoboUBcOn);
        }
    }

    /* W7 */
    for (k = 0; k < m; k++) {
        size_t j;
        uint8_t strong;
        if ((UnicodeBidiClass)T(k) != RoboUBcEn) {
            continue;
        }
        if (k == 0u) {
            strong = sos;
        } else {
            j = k - 1u;
            for (;;) {
                UnicodeBidiClass tj = (UnicodeBidiClass)T(j);
                if (tj == RoboUBcR || tj == RoboUBcL) {
                    strong = (uint8_t)tj;
                    break;
                }
                if (j == 0u) {
                    strong = sos;
                    break;
                }
                j--;
            }
        }
        if (strong == (uint8_t)RoboUBcL) {
            SET(k, RoboUBcL);
        }
    }

#undef T
#undef SET
}

static uint8_t BidiN0Strong(UnicodeBidiClass t)
{
    if (t == RoboUBcL) {
        return (uint8_t)RoboUBcL;
    }
    if (t == RoboUBcR || t == RoboUBcEn || t == RoboUBcAn) {
        return (uint8_t)RoboUBcR;
    }
    return 0xFFu; /* none */
}

static size_t BidiIdentifyBracketPairs(const Uchar32 *codepoints,
                                               const uint8_t *types,
                                               const size_t *indices,
                                               size_t m,
                                               size_t *pairOpen,
                                               size_t *pairClose,
                                               size_t pairCap)
{
    uint32_t stackCloser[RoboBidiMaxBracketStack];
    size_t stackOpen[RoboBidiMaxBracketStack];
    size_t stackLen = 0;
    size_t pairCount = 0;
    size_t ki;

    for (ki = 0; ki < m; ki++) {
        size_t pos = indices[ki];
        uint32_t pair = 0;
        int isOpen = 0;
        Uchar32 cp;

        if ((UnicodeBidiClass)types[pos] != RoboUBcOn) {
            continue;
        }
        cp = codepoints[pos];
        if (!BidiBracketLookup(cp, &pair, &isOpen)) {
            continue;
        }
        if (isOpen) {
            if (stackLen >= RoboBidiMaxBracketStack) {
                return 0; /* BD16 abort */
            }
            stackCloser[stackLen] = pair;
            stackOpen[stackLen] = pos;
            stackLen++;
        } else {
            size_t si;
            if (stackLen == 0u) {
                continue;
            }
            for (si = stackLen; si > 0u; si--) {
                size_t s = si - 1u;
                if (BidiBracketsEqual(cp, stackCloser[s])) {
                    if (pairCount < pairCap) {
                        pairOpen[pairCount] = stackOpen[s];
                        pairClose[pairCount] = pos;
                        pairCount++;
                    }
                    stackLen = s;
                    break;
                }
            }
        }
    }

    /* Sort pairs by opening position (insertion sort; m is small). */
    {
        size_t a;
        for (a = 1; a < pairCount; a++) {
            size_t o = pairOpen[a];
            size_t c = pairClose[a];
            size_t b = a;
            while (b > 0u && pairOpen[b - 1u] > o) {
                pairOpen[b] = pairOpen[b - 1u];
                pairClose[b] = pairClose[b - 1u];
                b--;
            }
            pairOpen[b] = o;
            pairClose[b] = c;
        }
    }
    return pairCount;
}

static void BidiApplyN(const Uchar32 *codepoints,
                              uint8_t *types,
                              const size_t *indices,
                              size_t m,
                              uint8_t sos,
                              uint8_t eos,
                              uint8_t embeddingLevel,
                              const uint8_t *wasNsm)
{
    size_t k;
    uint8_t e;
    size_t *pairOpen;
    size_t *pairClose;
    size_t pairCount;
    size_t pi;

    if (m == 0u) {
        return;
    }

    e = ((embeddingLevel % 2u) == 1u) ? (uint8_t)RoboUBcR
                                       : (uint8_t)RoboUBcL;

    pairOpen = (size_t *)Malloc(m * sizeof(size_t));
    pairClose = (size_t *)Malloc(m * sizeof(size_t));
    if (!pairOpen || !pairClose) {
        RoboFree(pairOpen);
        RoboFree(pairClose);
        return;
    }

    pairCount = BidiIdentifyBracketPairs(
        codepoints, types, indices, m, pairOpen, pairClose, m);

    for (pi = 0; pi < pairCount; pi++) {
        size_t openPos = pairOpen[pi];
        size_t closePos = pairClose[pi];
        uint8_t strongContent = 0xFFu;
        int hasOpposite = 0;
        int inside = 0;
        uint8_t New;
        size_t localK;

        for (k = 0; k < m; k++) {
            size_t pos = indices[k];
            uint8_t s;
            if (pos == openPos) {
                inside = 1;
                continue;
            }
            if (pos == closePos) {
                break;
            }
            if (!inside) {
                continue;
            }
            s = BidiN0Strong((UnicodeBidiClass)types[pos]);
            if (s == 0xFFu) {
                continue;
            }
            if (s == e) {
                strongContent = e;
                break;
            }
            hasOpposite = 1;
        }

        if (strongContent == e) {
            New = e;
        } else if (hasOpposite) {
            uint8_t preceding = 0xFFu;
            size_t rk;
            for (rk = m; rk > 0u; rk--) {
                size_t pos = indices[rk - 1u];
                uint8_t s;
                if (pos >= openPos) {
                    continue;
                }
                s = BidiN0Strong((UnicodeBidiClass)types[pos]);
                if (s != 0xFFu) {
                    preceding = s;
                    break;
                }
            }
            if (preceding == 0xFFu) {
                preceding = sos;
            }
            {
                uint8_t o = (e == (uint8_t)RoboUBcR) ? (uint8_t)RoboUBcL
                                                        : (uint8_t)RoboUBcR;
                if (preceding == o) {
                    New = o;
                } else {
                    New = e;
                }
            }
        } else {
            continue; /* N0d */
        }

        types[openPos] = New;
        types[closePos] = New;

        for (localK = 0; localK < m; localK++) {
            if (indices[localK] == openPos || indices[localK] == closePos) {
                size_t nk = localK + 1u;
                while (nk < m && wasNsm[indices[nk]]) {
                    types[indices[nk]] = New;
                    nk++;
                }
            }
        }
    }

    /* N1 / N2 */
    k = 0;
    while (k < m) {
        size_t end;
        uint8_t left;
        uint8_t right;
        uint8_t New;
        size_t j;
        uint8_t Left;
        uint8_t Right;

        if (!BidiIsNi((UnicodeBidiClass)types[indices[k]])) {
            k++;
            continue;
        }
        end = k;
        while (end < m &&
               BidiIsNi((UnicodeBidiClass)types[indices[end]])) {
            end++;
        }

        if (k == 0u) {
            Left = sos;
        } else {
            Left = types[indices[k - 1u]];
        }
        if (end >= m) {
            Right = eos;
        } else {
            Right = types[indices[end]];
        }
        left = BidiN0Strong((UnicodeBidiClass)Left);
        right = BidiN0Strong((UnicodeBidiClass)Right);
        if (left != 0xFFu && left == right) {
            New = left;
        } else {
            New = e;
        }
        for (j = k; j < end; j++) {
            types[indices[j]] = New;
        }
        k = end;
    }

    RoboFree(pairOpen);
    RoboFree(pairClose);
}

static void BidiApplyI(const uint8_t *types, int16_t *levels, const size_t *indices, size_t m)
{
    size_t ki;
    for (ki = 0; ki < m; ki++) {
        size_t pos = indices[ki];
        UnicodeBidiClass t = (UnicodeBidiClass)types[pos];
        int lev = (int)levels[pos];
        if ((lev % 2) == 0) {
            if (t == RoboUBcR) {
                levels[pos] = (int16_t)(lev + 1);
            } else if (t == RoboUBcAn || t == RoboUBcEn) {
                levels[pos] = (int16_t)(lev + 2);
            }
        } else {
            if (t == RoboUBcL || t == RoboUBcEn || t == RoboUBcAn) {
                levels[pos] = (int16_t)(lev + 1);
            }
        }
    }
}

static void BidiApplyL1(int16_t *levels,
                               const uint8_t *originalTypes,
                               size_t n,
                               uint8_t paragraphLevel)
{
    size_t i;
    size_t j;

    for (i = 0; i < n; i++) {
        UnicodeBidiClass t = (UnicodeBidiClass)originalTypes[i];
        if (t == RoboUBcS || t == RoboUBcB) {
            levels[i] = (int16_t)paragraphLevel;
        }
    }

    for (i = 0; i < n; i++) {
        UnicodeBidiClass t = (UnicodeBidiClass)originalTypes[i];
        if (t != RoboUBcS && t != RoboUBcB) {
            continue;
        }
        j = i;
        while (j > 0u) {
            UnicodeBidiClass tj;
            j--;
            tj = (UnicodeBidiClass)originalTypes[j];
            if (tj == RoboUBcWs || BidiIsIsolateInit(tj) ||
                tj == RoboUBcPdi) {
                levels[j] = (int16_t)paragraphLevel;
            } else {
                break;
            }
        }
    }

    j = n;
    while (j > 0u) {
        UnicodeBidiClass tj;
        j--;
        tj = (UnicodeBidiClass)originalTypes[j];
        if (tj == RoboUBcWs || BidiIsIsolateInit(tj) ||
            tj == RoboUBcPdi) {
            levels[j] = (int16_t)paragraphLevel;
        } else {
            break;
        }
    }
}

Result UnicodeBidiReorder(const uint8_t *levels,
                                        size_t len,
                                        size_t *visualToLogical,
                                        size_t *VisibleCount)
{
    size_t i;
    size_t vis;
    int MaxLevel;
    int level;

    if (!levels || !visualToLogical || !VisibleCount) {
        return RoboInvalidArgument;
    }

    vis = 0;
    for (i = 0; i < len; i++) {
        if (levels[i] != RoboBidiLevelRemoved) {
            visualToLogical[vis++] = i;
        }
    }
    *VisibleCount = vis;
    if (vis == 0u) {
        return RoboOk;
    }

    MaxLevel = 0;
    for (i = 0; i < vis; i++) {
        int lv = (int)levels[visualToLogical[i]];
        if (lv > MaxLevel) {
            MaxLevel = lv;
        }
    }

    for (level = MaxLevel; level >= 1; level--) {
        size_t p = 0;
        while (p < vis) {
            if ((int)levels[visualToLogical[p]] >= level) {
                size_t q = p;
                while (q < vis && (int)levels[visualToLogical[q]] >= level) {
                    q++;
                }
                /* reverse [p, q) */
                {
                    size_t a = p;
                    size_t b = q;
                    while (a + 1u < b) {
                        size_t tmp;
                        b--;
                        tmp = visualToLogical[a];
                        visualToLogical[a] = visualToLogical[b];
                        visualToLogical[b] = tmp;
                        a++;
                    }
                }
                p = q;
            } else {
                p++;
            }
        }
    }
    return RoboOk;
}

static Result BidiBuildIrs(const int16_t *levels,
                                         const uint8_t *originalTypes,
                                         const int *matchingPdi,
                                         const int *matchingInit,
                                         size_t n,
                                         BidiIrs **outSeqs,
                                         size_t *outSeqCount)
{
    size_t *kept = NULL;
    size_t keptCount = 0;
    size_t *runOf = NULL;
    size_t *runFirst = NULL;
    size_t *runLen = NULL;
    size_t runCount = 0;
    uint8_t *assigned = NULL;
    size_t **seqRuns = NULL;
    size_t *seqRunCounts = NULL;
    size_t seqCount = 0;
    size_t i;
    size_t ri;
    BidiIrs *seqs = NULL;
    Result rc = RoboOk;

    *outSeqs = NULL;
    *outSeqCount = 0;

    kept = (size_t *)Malloc(n * sizeof(size_t));
    runOf = (size_t *)Malloc(n * sizeof(size_t));
    if (!kept || !runOf) {
        rc = RoboOutOfMemory;
        goto done;
    }
    for (i = 0; i < n; i++) {
        runOf[i] = (size_t)-1;
        if (!BidiIsRemove((UnicodeBidiClass)originalTypes[i])) {
            kept[keptCount++] = i;
        }
    }
    if (keptCount == 0u) {
        goto done;
    }

    /* Count level runs */
    runCount = 1;
    for (i = 1; i < keptCount; i++) {
        if (levels[kept[i]] != levels[kept[i - 1u]]) {
            runCount++;
        }
    }
    runFirst = (size_t *)Malloc(runCount * sizeof(size_t));
    runLen = (size_t *)Malloc(runCount * sizeof(size_t));
    assigned = (uint8_t *)RoboCalloc(runCount, 1u);
    if (!runFirst || !runLen || !assigned) {
        rc = RoboOutOfMemory;
        goto done;
    }

    {
        size_t r = 0;
        runFirst[0] = 0;
        runLen[0] = 1;
        runOf[kept[0]] = 0;
        for (i = 1; i < keptCount; i++) {
            if (levels[kept[i]] == levels[kept[runFirst[r]]]) {
                runLen[r]++;
            } else {
                r++;
                runFirst[r] = i;
                runLen[r] = 1;
            }
            runOf[kept[i]] = r;
        }
    }

    seqRuns = (size_t **)RoboCalloc(runCount, sizeof(size_t *));
    seqRunCounts = (size_t *)RoboCalloc(runCount, sizeof(size_t));
    if (!seqRuns || !seqRunCounts) {
        rc = RoboOutOfMemory;
        goto done;
    }

    for (ri = 0; ri < runCount; ri++) {
        size_t firstKeptI;
        size_t firstChar;
        size_t *seq;
        size_t seqLen;

        if (assigned[ri]) {
            continue;
        }
        firstKeptI = runFirst[ri];
        firstChar = kept[firstKeptI];
        if ((UnicodeBidiClass)originalTypes[firstChar] == RoboUBcPdi &&
            matchingInit[firstChar] >= 0) {
            continue;
        }

        seq = (size_t *)Malloc(runCount * sizeof(size_t));
        if (!seq) {
            rc = RoboOutOfMemory;
            goto done;
        }
        seqLen = 0;
        seq[seqLen++] = ri;
        assigned[ri] = 1;

        for (;;) {
            size_t lastRun = seq[seqLen - 1u];
            size_t lastKeptI = runFirst[lastRun] + runLen[lastRun] - 1u;
            size_t lastChar = kept[lastKeptI];
            int pdi;
            size_t rj;

            if (!BidiIsIsolateInit(
                    (UnicodeBidiClass)originalTypes[lastChar]) ||
                matchingPdi[lastChar] < 0) {
                break;
            }
            pdi = matchingPdi[lastChar];
            rj = runOf[(size_t)pdi];
            if (rj == (size_t)-1 || assigned[rj]) {
                break;
            }
            if (kept[runFirst[rj]] != (size_t)pdi) {
                break;
            }
            seq[seqLen++] = rj;
            assigned[rj] = 1;
        }
        seqRuns[seqCount] = seq;
        seqRunCounts[seqCount] = seqLen;
        seqCount++;
        seq = NULL; /* owned by seqRuns */
    }

    for (ri = 0; ri < runCount; ri++) {
        size_t *seq;
        if (assigned[ri]) {
            continue;
        }
        seq = (size_t *)Malloc(sizeof(size_t));
        if (!seq) {
            rc = RoboOutOfMemory;
            goto done;
        }
        seq[0] = ri;
        assigned[ri] = 1;
        seqRuns[seqCount] = seq;
        seqRunCounts[seqCount] = 1;
        seqCount++;
    }

    seqs = (BidiIrs *)RoboCalloc(seqCount, sizeof(BidiIrs));
    if (!seqs) {
        rc = RoboOutOfMemory;
        goto done;
    }

    for (i = 0; i < seqCount; i++) {
        size_t total = 0;
        size_t s;
        size_t *indices;
        size_t w = 0;
        size_t first;
        size_t last;
        int prevLevel = -1;
        int nextLevel = -1;
        int useParaEos = 0;
        size_t j;

        for (s = 0; s < seqRunCounts[i]; s++) {
            total += runLen[seqRuns[i][s]];
        }
        indices = (size_t *)Malloc(total * sizeof(size_t));
        if (!indices) {
            rc = RoboOutOfMemory;
            /* free already built */
            {
                size_t z;
                for (z = 0; z < i; z++) {
                    RoboFree(seqs[z].indices);
                }
                RoboFree(seqs);
                seqs = NULL;
            }
            goto done;
        }
        for (s = 0; s < seqRunCounts[i]; s++) {
            size_t r = seqRuns[i][s];
            size_t t;
            for (t = 0; t < runLen[r]; t++) {
                indices[w++] = kept[runFirst[r] + t];
            }
        }
        seqs[i].indices = indices;
        seqs[i].count = total;
        seqs[i].level = (uint8_t)levels[indices[0]];

        first = indices[0];
        last = indices[total - 1u];
        for (j = first; j > 0u;) {
            j--;
            if (!BidiIsRemove((UnicodeBidiClass)originalTypes[j])) {
                prevLevel = (int)levels[j];
                break;
            }
        }
        if (BidiIsIsolateInit(
                (UnicodeBidiClass)originalTypes[last]) &&
            matchingPdi[last] < 0) {
            useParaEos = 1;
        } else {
            for (j = last + 1u; j < n; j++) {
                if (!BidiIsRemove(
                        (UnicodeBidiClass)originalTypes[j])) {
                    nextLevel = (int)levels[j];
                    break;
                }
            }
            if (nextLevel < 0) {
                useParaEos = 1;
            }
        }
        seqs[i].prevLevel = prevLevel;
        seqs[i].nextLevel = nextLevel;
        seqs[i].useParaEos = useParaEos;
    }

    *outSeqs = seqs;
    *outSeqCount = seqCount;
    seqs = NULL; /* ownership transferred */

done:
    if (seqRuns) {
        size_t z;
        for (z = 0; z < seqCount; z++) {
            RoboFree(seqRuns[z]);
        }
        /* If we failed mid-way after allocating more seqRuns entries beyond
         * seqCount that were never recorded — all recorded are freed above.
         * Also free any seq allocated during the loop that was assigned to
         * seqRuns only after success; on OOM after seq malloc but before
         * assign, free that seq — handled by not assigning. */
        RoboFree(seqRuns);
    }
    RoboFree(seqRunCounts);
    RoboFree(assigned);
    RoboFree(runFirst);
    RoboFree(runLen);
    RoboFree(runOf);
    RoboFree(kept);
    if (rc != RoboOk && seqs) {
        size_t z;
        for (z = 0; z < seqCount; z++) {
            RoboFree(seqs[z].indices);
        }
        RoboFree(seqs);
    }
    return rc;
}

static void BidiFreeIrs(BidiIrs *seqs, size_t seqCount)
{
    size_t i;
    if (!seqs) {
        return;
    }
    for (i = 0; i < seqCount; i++) {
        RoboFree(seqs[i].indices);
    }
    RoboFree(seqs);
}

static Result BidiAnalyzeCore(const Uchar32 *cps,
                                            size_t n,
                                            UnicodeBidiParagraphDir paraDir,
                                            uint8_t *levelsOut,
                                            uint8_t *paragraphLevelOut,
                                            size_t *visualToLogical,
                                            size_t *VisibleCount,
                                            int wantVisual)
{
    uint8_t *originalTypes = NULL;
    uint8_t *types = NULL;
    uint8_t *wasNsm = NULL;
    uint8_t *removed = NULL;
    int16_t *levels = NULL;
    int *matchingPdi = NULL;
    int *matchingInit = NULL;
    BidiIrs *seqs = NULL;
    size_t seqCount = 0;
    uint8_t paragraphLevel;
    size_t i;
    Result rc = RoboOk;
    size_t *visBuf = NULL;
    size_t visCount = 0;

    if ((!cps && n > 0u) || !levelsOut) {
        return RoboInvalidArgument;
    }
    if (paraDir > RoboBidiAuto) {
        return RoboInvalidArgument;
    }
    if (wantVisual && (!visualToLogical || !VisibleCount)) {
        return RoboInvalidArgument;
    }

    if (n == 0u) {
        if (paraDir == RoboBidiRtl) {
            paragraphLevel = 1u;
        } else {
            paragraphLevel = 0u;
        }
        if (paragraphLevelOut) {
            *paragraphLevelOut = paragraphLevel;
        }
        if (wantVisual) {
            *VisibleCount = 0u;
        }
        return RoboOk;
    }

    originalTypes = (uint8_t *)Malloc(n);
    types = (uint8_t *)Malloc(n);
    wasNsm = (uint8_t *)Malloc(n);
    removed = (uint8_t *)Malloc(n);
    levels = (int16_t *)Malloc(n * sizeof(int16_t));
    matchingPdi = (int *)Malloc(n * sizeof(int));
    matchingInit = (int *)Malloc(n * sizeof(int));
    if (!originalTypes || !types || !wasNsm || !removed || !levels ||
        !matchingPdi || !matchingInit) {
        rc = RoboOutOfMemory;
        goto cleanup;
    }

    for (i = 0; i < n; i++) {
        originalTypes[i] = (uint8_t)BidiClassProperty(cps[i]);
        types[i] = originalTypes[i];
        wasNsm[i] = (originalTypes[i] == (uint8_t)RoboUBcNsm) ? 1u : 0u;
        removed[i] =
            BidiIsRemove((UnicodeBidiClass)originalTypes[i]) ? 1u
                                                                              : 0u;
    }

    BidiMatchingPdis(originalTypes, n, matchingPdi);
    BidiMatchingInitiators(matchingPdi, n, matchingInit);

    if (paraDir == RoboBidiAuto) {
        paragraphLevel = BidiDetermineParagraphLevel(types, 0, n);
    } else if (paraDir == RoboBidiRtl) {
        paragraphLevel = 1u;
    } else {
        paragraphLevel = 0u;
    }

    BidiResolveExplicit(types, n, paragraphLevel, matchingPdi, levels);

    for (i = 0; i < n; i++) {
        if (removed[i] && levels[i] < 0) {
            levels[i] = 0;
        }
    }

    rc = BidiBuildIrs(
        levels, originalTypes, matchingPdi, matchingInit, n, &seqs, &seqCount);
    if (rc != RoboOk) {
        goto cleanup;
    }

    for (i = 0; i < seqCount; i++) {
        int prev = seqs[i].prevLevel;
        int nxt;
        size_t first = seqs[i].indices[0];
        size_t last = seqs[i].indices[seqs[i].count - 1u];
        if (prev < 0) {
            prev = (int)paragraphLevel;
        }
        seqs[i].sos = BidiSosEosType((int)levels[first], prev);
        if (seqs[i].useParaEos) {
            nxt = (int)paragraphLevel;
        } else {
            nxt = seqs[i].nextLevel;
            if (nxt < 0) {
                nxt = (int)paragraphLevel;
            }
        }
        seqs[i].eos = BidiSosEosType((int)levels[last], nxt);
    }

    for (i = 0; i < seqCount; i++) {
        BidiApplyW(types, seqs[i].indices, seqs[i].count, seqs[i].sos);
        BidiApplyN(cps,
                          types,
                          seqs[i].indices,
                          seqs[i].count,
                          seqs[i].sos,
                          seqs[i].eos,
                          seqs[i].level,
                          wasNsm);
        BidiApplyI(types, levels, seqs[i].indices, seqs[i].count);
    }

    BidiApplyL1(levels, originalTypes, n, paragraphLevel);

    for (i = 0; i < n; i++) {
        if (removed[i]) {
            levelsOut[i] = RoboBidiLevelRemoved;
        } else {
            levelsOut[i] = (uint8_t)levels[i];
        }
    }

    if (paragraphLevelOut) {
        *paragraphLevelOut = paragraphLevel;
    }

    if (wantVisual) {
        visBuf = visualToLogical;
        rc = UnicodeBidiReorder(levelsOut, n, visBuf, &visCount);
        if (rc != RoboOk) {
            goto cleanup;
        }
        *VisibleCount = visCount;
    }

cleanup:
    BidiFreeIrs(seqs, seqCount);
    RoboFree(originalTypes);
    RoboFree(types);
    RoboFree(wasNsm);
    RoboFree(removed);
    RoboFree(levels);
    RoboFree(matchingPdi);
    RoboFree(matchingInit);
    return rc;
}

void UnicodeBidiResultDestroy(UnicodeBidiResult *r)
{
    if (!r) {
        return;
    }
    RoboFree(r->levels);
    RoboFree(r->visualToLogical);
    RoboFree(r->byteEnds);
    r->levels = NULL;
    r->visualToLogical = NULL;
    r->byteEnds = NULL;
    r->cpCount = 0;
    r->VisibleCount = 0;
    r->paragraphLevel = 0;
}

Result UnicodeBidiAnalyze(const Uchar32 *cps,
                                        size_t len,
                                        UnicodeBidiParagraphDir paraDir,
                                        UnicodeBidiResult *out)
{
    uint8_t *levels = NULL;
    size_t *visual = NULL;
    size_t visible = 0;
    uint8_t paraLevel = 0;
    Result rc;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (!cps && len > 0u) {
        return RoboInvalidArgument;
    }

    if (len == 0u) {
        out->cpCount = 0;
        out->paragraphLevel = (paraDir == RoboBidiRtl) ? 1u : 0u;
        out->VisibleCount = 0;
        return RoboOk;
    }

    levels = (uint8_t *)Malloc(len);
    visual = (size_t *)Malloc(len * sizeof(size_t));
    if (!levels || !visual) {
        RoboFree(levels);
        RoboFree(visual);
        return RoboOutOfMemory;
    }

    rc = BidiAnalyzeCore(
        cps, len, paraDir, levels, &paraLevel, visual, &visible, 1);
    if (rc != RoboOk) {
        RoboFree(levels);
        RoboFree(visual);
        return rc;
    }

    out->cpCount = len;
    out->paragraphLevel = paraLevel;
    out->levels = levels;
    out->visualToLogical = visual;
    out->VisibleCount = visible;
    out->byteEnds = NULL;
    return RoboOk;
}

Result UnicodeBidiResolveLevels(const Uchar32 *cps,
                                               size_t len,
                                               UnicodeBidiParagraphDir paraDir,
                                               uint8_t *levelsOut,
                                               uint8_t *paragraphLevelOut)
{
    return BidiAnalyzeCore(
        cps, len, paraDir, levelsOut, paragraphLevelOut, NULL, NULL, 0);
}

Result UnicodeBidiAnalyzeUtf8(const char *src,
                                             size_t srcLen,
                                             UnicodeMode mode,
                                             UnicodeBidiParagraphDir paraDir,
                                             UnicodeBidiResult *out)
{
    Uchar32 *cps = NULL;
    size_t *byteEnds = NULL;
    size_t n = 0;
    size_t off = 0;
    size_t cap = 0;
    Result rc;
    UnicodeBidiResult tmp;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (!src && srcLen > 0u) {
        return RoboInvalidArgument;
    }

    if (srcLen == 0u) {
        return UnicodeBidiAnalyze(NULL, 0, paraDir, out);
    }

    cap = srcLen; /* upper bound: 1 CP per byte */
    cps = (Uchar32 *)Malloc(cap * sizeof(Uchar32));
    byteEnds = (size_t *)Malloc(cap * sizeof(size_t));
    if (!cps || !byteEnds) {
        RoboFree(cps);
        RoboFree(byteEnds);
        return RoboOutOfMemory;
    }

    while (off < srcLen) {
        Uchar32 cp = 0;
        size_t next = off;
        UnicodeStatus st =
            UnicodeUtf8Decode(src, srcLen, off, mode, &cp, &next);
        if (st != RoboUnicodeOk) {
            RoboFree(cps);
            RoboFree(byteEnds);
            return RoboParseError;
        }
        cps[n] = cp;
        byteEnds[n] = next;
        n++;
        off = next;
    }

    memset(&tmp, 0, sizeof(tmp));
    rc = UnicodeBidiAnalyze(cps, n, paraDir, &tmp);
    RoboFree(cps);
    if (rc != RoboOk) {
        RoboFree(byteEnds);
        return rc;
    }

    *out = tmp;
    out->byteEnds = byteEnds;
    return RoboOk;
}
