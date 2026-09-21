/*
 * RoboI18N collation — UCA 16.0.0 + CLDR 46 tailorings (Goal 4.9).
 *
 * Algorithm: UTS #10 (Unicode Collation Algorithm).
 *   S1 NFD → S2 CE formation → S3 sort key / level compare → S4 result
 *
 * Invalid UTF-8: STRICT → RoboParseError.
 */

#include "RoboI18N/RoboI18NCollation.h"

#include "RoboI18N/RoboCollData.h"
#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboI18N/RoboUcaData.h"
#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboNormData.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeNorm.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <string.h>

struct I18nCollator {
    I18nLocale locale;
    I18nCollatorOptions options;
    const CollRemap *remaps;
    uint16_t remapCount;
    char typeName[32];
};

enum {
    CE_CAP_INIT = 256,
    KEY_LEVEL_SEP = 0x00,
    KEY_PRIMARY_SHIFT = 0 /* store primary as 2 big-endian bytes */
};

const char *I18nCollationUcaVersionString(void)
{
    return UcaVersionString();
}

const char *I18nCollationCldrVersionString(void)
{
    return CollCldrVersionString();
}

void I18nCollatorOptionsInit(I18nCollatorOptions *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->strength = RoboI18nCollationTertiary;
    out->alternate = RoboI18nCollationNonIgnorable;
    out->numeric = 0;
    out->caseFirst = 0;
}

static int FindLocaleRow(const char *lang, const CollLocale **out)
{
    uint32_t i;
    for (i = 0; i < CollLocaleCount; i++) {
        const char *id = CollStringPool + CollLocales[i].idOffset;
        if (strcmp(id, lang) == 0) {
            *out = &CollLocales[i];
            return 1;
        }
    }
    return 0;
}

static void BindTailoring(I18nCollator *c, const char *typeName)
{
    const CollLocale *row = NULL;
    const char *lang = c->locale.language[0] ? c->locale.language : "root";
    uint16_t i;
    uint16_t wantTid = 0;
    int haveType = 0;

    c->remaps = NULL;
    c->remapCount = 0;
    strncpy(c->typeName, typeName && typeName[0] ? typeName : "standard",
            sizeof(c->typeName) - 1);
    c->typeName[sizeof(c->typeName) - 1] = '\0';

    if (!FindLocaleRow(lang, &row)) {
        FindLocaleRow("root", &row);
    }
    if (!row) {
        return;
    }

    for (i = 0; i < row->typeCount; i++) {
        const CollType *t = &CollTypes[row->typeBegin + i];
        const char *n = CollStringPool + t->nameOffset;
        if (strcmp(n, c->typeName) == 0) {
            wantTid = t->typeId;
            haveType = 1;
            if (t->remapCount > 0 && t->remapOffset + t->remapCount <= CollRemapCount) {
                c->remaps = &CollRemaps[t->remapOffset];
                c->remapCount = t->remapCount;
            }
            break;
        }
    }
    if (!haveType) {
        /* fall back to locale default type name if present */
        for (i = 0; i < row->typeCount; i++) {
            const CollType *t = &CollTypes[row->typeBegin + i];
            if (t->typeId == row->defaultTypeId && t->remapCount > 0 &&
                t->remapOffset + t->remapCount <= CollRemapCount) {
                c->remaps = &CollRemaps[t->remapOffset];
                c->remapCount = t->remapCount;
                break;
            }
        }
        (void)wantTid;
    }
}

Result I18nCollatorCreate(const I18nLocale *locale,
                                        const char *collationType,
                                        const I18nCollatorOptions *options,
                                        I18nCollator **out)
{
    I18nCollator *c;
    char resolvedType[32];
    const char *typeUse;
    if (!locale || !out) {
        return RoboInvalidArgument;
    }
    c = (I18nCollator *)RoboCalloc(1, sizeof(*c));
    if (!c) {
        return RoboOutOfMemory;
    }
    c->locale = *locale;
    if (options) {
        c->options = *options;
    } else {
        I18nCollatorOptionsInit(&c->options);
    }
    typeUse = collationType;
    if (!typeUse || !typeUse[0]) {
        if (I18nLocaleResolveCollation(locale, NULL, resolvedType,
                                               sizeof(resolvedType)) == RoboOk) {
            typeUse = resolvedType;
        } else {
            typeUse = "standard";
        }
    }
    BindTailoring(c, typeUse);
    *out = c;
    return RoboOk;
}

void I18nCollatorDestroy(I18nCollator *coll)
{
    RoboFree(coll);
}

Result I18nCollatorSetOptions(I18nCollator *coll,
                                             const I18nCollatorOptions *options)
{
    if (!coll || !options) {
        return RoboInvalidArgument;
    }
    coll->options = *options;
    return RoboOk;
}

Result I18nCollatorGetOptions(const I18nCollator *coll,
                                             I18nCollatorOptions *out)
{
    if (!coll || !out) {
        return RoboInvalidArgument;
    }
    *out = coll->options;
    return RoboOk;
}

static int MapLookup(uint32_t cp, uint32_t *ceOff, uint16_t *ceCnt)
{
    uint32_t lo = 0;
    uint32_t hi = UcaMapCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        uint32_t v = UcaMap[mid].cp;
        if (v < cp) {
            lo = mid + 1u;
        } else if (v > cp) {
            hi = mid;
        } else {
            *ceOff = UcaMap[mid].ceOffset;
            *ceCnt = UcaMap[mid].ceCount;
            return 1;
        }
    }
    return 0;
}

static int RemapLookup(const I18nCollator *coll,
                        uint32_t cp,
                        uint16_t *p,
                        uint16_t *s,
                        uint16_t *t)
{
    uint16_t i;
    if (!coll || !coll->remaps) {
        return 0;
    }
    for (i = 0; i < coll->remapCount; i++) {
        if (coll->remaps[i].cp == cp) {
            *p = coll->remaps[i].primary;
            *s = coll->remaps[i].secondary;
            *t = coll->remaps[i].tertiary;
            return 1;
        }
    }
    return 0;
}

static int FindContraction(const uint32_t *cps,
                            size_t n,
                            size_t i,
                            uint32_t *ceOff,
                            uint16_t *ceCnt,
                            size_t *consumed)
{
    uint32_t first;
    uint32_t lo;
    uint32_t hi;
    size_t bestLen = 0;
    uint32_t bestOff = 0;
    uint16_t bestCnt = 0;

    if (i >= n) {
        return 0;
    }
    first = cps[i];
    /* contractions sorted by firstCp, then longer first */
    lo = 0;
    hi = UcaContractionCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (UcaContractions[mid].firstCp < first) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    while (lo < UcaContractionCount &&
           UcaContractions[lo].firstCp == first) {
        const UcaContraction *c = &UcaContractions[lo];
        size_t len = c->seqLen;
        if (i + len <= n) {
            size_t k;
            int ok = 1;
            for (k = 0; k < len; k++) {
                if (cps[i + k] != UcaContractionSeq[c->seqOffset + k]) {
                    ok = 0;
                    break;
                }
            }
            if (ok && len > bestLen) {
                bestLen = len;
                bestOff = c->ceOffset;
                bestCnt = c->ceCount;
            }
        }
        lo++;
    }
    if (bestLen == 0) {
        return 0;
    }
    *ceOff = bestOff;
    *ceCnt = bestCnt;
    *consumed = bestLen;
    return 1;
}

static void ImplicitCes(uint32_t cp, uint64_t *outCe, uint16_t *outN)
{
    uint32_t i;
    uint16_t base = 0xFBC0;
    uint32_t aaaa;
    uint32_t bbbb;

    for (i = 0; i < UcaImplicitRangeCount; i++) {
        if (cp >= UcaImplicitRanges[i].start &&
            cp <= UcaImplicitRanges[i].end) {
            base = UcaImplicitRanges[i].base;
            break;
        }
    }
    /* UTS #10 §7.1.3 implicit weight formula (core Unified Ideograph path). */
    if ((cp >= 0x4E00u && cp <= 0x9FFFu) || (cp >= 0xF900u && cp <= 0xFAFFu)) {
        aaaa = 0xFB40u + (cp >> 15);
        bbbb = (cp & 0x7FFFu) | 0x8000u;
    } else if ((cp >= 0x3400u && cp <= 0x4DBFu) || (cp >= 0x20000u && cp <= 0x2A6DFu) ||
               (cp >= 0x2A700u && cp <= 0x2B73Fu) || (cp >= 0x2B740u && cp <= 0x2B81Fu) ||
               (cp >= 0x2B820u && cp <= 0x2CEAFu) || (cp >= 0x2CEB0u && cp <= 0x2EBEFu) ||
               (cp >= 0x30000u && cp <= 0x3134Fu)) {
        aaaa = 0xFB80u + (cp >> 15);
        bbbb = (cp & 0x7FFFu) | 0x8000u;
    } else {
        aaaa = (uint32_t)base + (cp >> 15);
        bbbb = (cp & 0x7FFFu) | 0x8000u;
    }
    outCe[0] = ((uint64_t)(aaaa & 0xFFFFu) << 32) | (0x0020ULL << 16) | 0x0002ULL;
    outCe[1] = ((uint64_t)(bbbb & 0xFFFFu) << 32);
    *outN = 2;
}

static uint16_t ApplyCaseFirst(const I18nCollator *coll, uint16_t tertiary)
{
    /* Tertiary case bits in DUCET: uppercase often 0x0008 vs lowercase 0x0002.
     * caseFirst upperFirst: invert high/low for case-sensitive tertiaries. */
    if (!coll || coll->options.caseFirst == 0) {
        return tertiary;
    }
    if (coll->options.caseFirst == 1) { /* upper first */
        if (tertiary == 0x0008) {
            return 0x0002;
        }
        if (tertiary == 0x0002) {
            return 0x0008;
        }
    } else if (coll->options.caseFirst == 2) { /* lower first — DUCET defaultish */
        return tertiary;
    }
    return tertiary;
}

typedef struct {
    uint64_t *ces;
    size_t len;
    size_t cap;
} CeBuf;

static Result CeBufPush(CeBuf *b, uint64_t ce)
{
    if (b->len >= b->cap) {
        size_t ncap = b->cap ? b->cap * 2u : (size_t)CE_CAP_INIT;
        uint64_t *n = (uint64_t *)Malloc(ncap * sizeof(uint64_t));
        if (!n) {
            return RoboOutOfMemory;
        }
        if (b->ces) {
            memcpy(n, b->ces, b->len * sizeof(uint64_t));
            RoboFree(b->ces);
        }
        b->ces = n;
        b->cap = ncap;
    }
    b->ces[b->len++] = ce;
    return RoboOk;
}

static int DigitValue(uint32_t cp, int *outV)
{
    if (cp >= '0' && cp <= '9') {
        *outV = (int)(cp - '0');
        return 1;
    }
    if (UnicodeCategory((Uchar32)cp) == RoboUGcNd) {
        /* Common decimal digits: use offset within 10-block when possible. */
        uint32_t base = cp - (cp % 10u);
        if (cp >= base && cp < base + 10u) {
            *outV = (int)(cp - base);
            return 1;
        }
    }
    return 0;
}

static Result FormCes(const I18nCollator *coll,
                              const uint32_t *cps,
                              size_t n,
                              CeBuf *out)
{
    size_t i = 0;
    memset(out, 0, sizeof(*out));
    while (i < n) {
        uint32_t ceOff = 0;
        uint16_t ceCnt = 0;
        size_t consumed = 1;
        uint16_t rp, rs, rt;
        uint16_t k;

        if (coll->options.numeric) {
            int dv;
            if (DigitValue(cps[i], &dv)) {
                /* Collect run into a synthetic primary weight encoding. */
                uint64_t value = 0;
                size_t j = i;
                int overflow = 0;
                while (j < n && DigitValue(cps[j], &dv)) {
                    if (value > (UINT64_MAX - (uint64_t)dv) / 10u) {
                        overflow = 1;
                        break;
                    }
                    value = value * 10u + (uint64_t)dv;
                    j++;
                }
                if (!overflow && j > i) {
                    uint8_t bytes[8];
                    int bi;
                    for (bi = 7; bi >= 0; bi--) {
                        bytes[bi] = (uint8_t)(value & 0xFFu);
                        value >>= 8;
                    }
                    for (bi = 0; bi < 8; bi++) {
                        uint64_t ce =
                            ((uint64_t)(0xFF00u + bytes[bi]) << 32) | (0x0020ULL << 16) | 2ULL;
                        if (CeBufPush(out, ce) != RoboOk) {
                            return RoboOutOfMemory;
                        }
                    }
                    i = j;
                    continue;
                }
            }
        }

        /* Tailoring remaps are stored on NFC code points; after NFD, try compose. */
        {
            uint32_t composed = cps[i];
            size_t j = i + 1;
            while (j < n) {
                uint32_t next = NormComposePair(composed, cps[j]);
                if (next == 0) {
                    break;
                }
                composed = next;
                j++;
            }
            if (RemapLookup(coll, composed, &rp, &rs, &rt) ||
                (composed == cps[i] && RemapLookup(coll, cps[i], &rp, &rs, &rt))) {
                uint64_t ce = ((uint64_t)rp << 32) | ((uint64_t)rs << 16) | (uint64_t)rt;
                if (CeBufPush(out, ce) != RoboOk) {
                    return RoboOutOfMemory;
                }
                i = (composed != cps[i]) ? j : i + 1;
                continue;
            }
        }

        if (FindContraction(cps, n, i, &ceOff, &ceCnt, &consumed)) {
            for (k = 0; k < ceCnt; k++) {
                if (CeBufPush(out, UcaCePool[ceOff + k]) != RoboOk) {
                    return RoboOutOfMemory;
                }
            }
            i += consumed;
            continue;
        }

        if (MapLookup(cps[i], &ceOff, &ceCnt)) {
            for (k = 0; k < ceCnt; k++) {
                if (CeBufPush(out, UcaCePool[ceOff + k]) != RoboOk) {
                    return RoboOutOfMemory;
                }
            }
        } else {
            uint64_t impl[2];
            uint16_t nn = 0;
            ImplicitCes(cps[i], impl, &nn);
            for (k = 0; k < nn; k++) {
                if (CeBufPush(out, impl[k]) != RoboOk) {
                    return RoboOutOfMemory;
                }
            }
        }
        i++;
    }
    return RoboOk;
}

static Result DecodeNfd(const char *text,
                                size_t len,
                                uint32_t **outCps,
                                size_t *outN)
{
    Uchar32 *tmp = NULL;
    size_t tn = 0;
    size_t off = 0;
    size_t cap = 0;
    Result r;

    *outCps = NULL;
    *outN = 0;
    if (len == 0) {
        return RoboOk;
    }
    while (off < len) {
        Uchar32 cp;
        size_t next = 0;
        UnicodeStatus st =
            UnicodeUtf8Decode(text, len, off, RoboUnicodeStrict, &cp, &next);
        if (st != RoboUnicodeOk) {
            RoboFree(tmp);
            return RoboParseError;
        }
        if (tn >= cap) {
            size_t ncap = cap ? cap * 2u : 64u;
            Uchar32 *nbuf =
                (Uchar32 *)Malloc(ncap * sizeof(Uchar32));
            if (!nbuf) {
                RoboFree(tmp);
                return RoboOutOfMemory;
            }
            if (tmp) {
                memcpy(nbuf, tmp, tn * sizeof(Uchar32));
                RoboFree(tmp);
            }
            tmp = nbuf;
            cap = ncap;
        }
        tmp[tn++] = cp;
        off = next;
    }

    {
        Uchar32 *nfd = NULL;
        size_t nfdN = 0;
        r = UnicodeNormalizeNfd(tmp, tn, &nfd, &nfdN);
        RoboFree(tmp);
        if (r != RoboOk) {
            return r;
        }
        *outCps = (uint32_t *)nfd;
        *outN = nfdN;
    }
    return RoboOk;
}

typedef struct {
    uint16_t *w;
    size_t len;
    size_t cap;
} Wbuf;

static Result WbufPush(Wbuf *b, uint16_t v)
{
    if (b->len >= b->cap) {
        size_t ncap = b->cap ? b->cap * 2u : 128u;
        uint16_t *n = (uint16_t *)Malloc(ncap * sizeof(uint16_t));
        if (!n) {
            return RoboOutOfMemory;
        }
        if (b->w) {
            memcpy(n, b->w, b->len * sizeof(uint16_t));
            RoboFree(b->w);
        }
        b->w = n;
        b->cap = ncap;
    }
    b->w[b->len++] = v;
    return RoboOk;
}

static Result BuildLevelWeights(const I18nCollator *coll,
                                         const CeBuf *ces,
                                         int level,
                                         Wbuf *out)
{
    size_t i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < ces->len; i++) {
        uint64_t ce = ces->ces[i];
        int variable = RoboUcaCeIsVariable(ce);
        uint16_t p = RoboUcaCePrimary(ce);
        uint16_t s = RoboUcaCeSecondary(ce);
        uint16_t t = ApplyCaseFirst(coll, RoboUcaCeTertiary(ce));

        if (coll->options.alternate == RoboI18nCollationShifted && variable) {
            if (level == 0 || level == 1 || level == 2) {
                continue; /* ignorable at L1–L3 */
            }
            if (level == 3) {
                if (WbufPush(out, p ? p : 0xFFFF) != RoboOk) {
                    return RoboOutOfMemory;
                }
            }
            continue;
        }

        if (level == 0) {
            if (p != 0) {
                if (WbufPush(out, p) != RoboOk) {
                    return RoboOutOfMemory;
                }
            }
        } else if (level == 1) {
            if (s != 0) {
                if (WbufPush(out, s) != RoboOk) {
                    return RoboOutOfMemory;
                }
            }
        } else if (level == 2) {
            if (t != 0) {
                if (WbufPush(out, t) != RoboOk) {
                    return RoboOutOfMemory;
                }
            }
        } else if (level == 3) {
            /* Quaternary: non-ignorable variables already handled; others 0xFFFF */
            if (WbufPush(out, 0xFFFFu) != RoboOk) {
                return RoboOutOfMemory;
            }
        }
    }
    return RoboOk;
}

static int CmpWbuf(const Wbuf *a, const Wbuf *b)
{
    size_t n = a->len < b->len ? a->len : b->len;
    size_t i;
    for (i = 0; i < n; i++) {
        if (a->w[i] < b->w[i]) {
            return -1;
        }
        if (a->w[i] > b->w[i]) {
            return 1;
        }
    }
    if (a->len < b->len) {
        return -1;
    }
    if (a->len > b->len) {
        return 1;
    }
    return 0;
}

static int MaxLevel(I18nCollationStrength s)
{
    switch (s) {
    case RoboI18nCollationPrimary:
        return 0;
    case RoboI18nCollationSecondary:
        return 1;
    case RoboI18nCollationTertiary:
        return 2;
    case RoboI18nCollationQuaternary:
        return 3;
    case RoboI18nCollationIdentical:
        return 4;
    default:
        return 2;
    }
}

static int CmpCodepoints(const uint32_t *a, size_t an, const uint32_t *b, size_t bn)
{
    size_t n = an < bn ? an : bn;
    size_t i;
    for (i = 0; i < n; i++) {
        if (a[i] < b[i]) {
            return -1;
        }
        if (a[i] > b[i]) {
            return 1;
        }
    }
    if (an < bn) {
        return -1;
    }
    if (an > bn) {
        return 1;
    }
    return 0;
}

Result I18nCollatorCompare(const I18nCollator *coll,
                                         const char *a,
                                         size_t aLen,
                                         const char *b,
                                         size_t bLen,
                                         int *outCmp)
{
    uint32_t *ac = NULL;
    uint32_t *bc = NULL;
    size_t an = 0;
    size_t bn = 0;
    CeBuf ace;
    CeBuf bce;
    Result r;
    int level;
    int maxLv;

    if (!coll || !outCmp || (!a && aLen) || (!b && bLen)) {
        return RoboInvalidArgument;
    }
    *outCmp = 0;
    memset(&ace, 0, sizeof(ace));
    memset(&bce, 0, sizeof(bce));

    r = DecodeNfd(a, aLen, &ac, &an);
    if (r != RoboOk) {
        return r;
    }
    r = DecodeNfd(b, bLen, &bc, &bn);
    if (r != RoboOk) {
        RoboFree(ac);
        return r;
    }

    r = FormCes(coll, ac, an, &ace);
    if (r != RoboOk) {
        goto done;
    }
    r = FormCes(coll, bc, bn, &bce);
    if (r != RoboOk) {
        goto done;
    }

    maxLv = MaxLevel(coll->options.strength);
    for (level = 0; level <= maxLv && level <= 3; level++) {
        Wbuf aw;
        Wbuf bw;
        int c;
        r = BuildLevelWeights(coll, &ace, level, &aw);
        if (r != RoboOk) {
            RoboFree(aw.w);
            goto done;
        }
        r = BuildLevelWeights(coll, &bce, level, &bw);
        if (r != RoboOk) {
            RoboFree(aw.w);
            RoboFree(bw.w);
            goto done;
        }
        c = CmpWbuf(&aw, &bw);
        RoboFree(aw.w);
        RoboFree(bw.w);
        if (c != 0) {
            *outCmp = c;
            r = RoboOk;
            goto done;
        }
    }
    if (maxLv >= 4) {
        *outCmp = CmpCodepoints(ac, an, bc, bn);
    } else {
        *outCmp = 0;
    }
    r = RoboOk;

done:
    RoboFree(ac);
    RoboFree(bc);
    RoboFree(ace.ces);
    RoboFree(bce.ces);
    return r;
}

Result I18nCollatorEqual(const I18nCollator *coll,
                                       const char *a,
                                       size_t aLen,
                                       const char *b,
                                       size_t bLen,
                                       int *outEqual)
{
    int cmp = 0;
    Result r = I18nCollatorCompare(coll, a, aLen, b, bLen, &cmp);
    if (r != RoboOk) {
        return r;
    }
    if (!outEqual) {
        return RoboInvalidArgument;
    }
    *outEqual = (cmp == 0);
    return RoboOk;
}

Result I18nCollatorGetSortKey(const I18nCollator *coll,
                                              const char *text,
                                              size_t textLen,
                                              uint8_t *outKey,
                                              size_t outCap,
                                              size_t *outLen)
{
    uint32_t *cps = NULL;
    size_t n = 0;
    CeBuf ces;
    Result r;
    int level;
    int maxLv;
    size_t need = 0;
    size_t pos = 0;
    Wbuf levels[5];
    int li;

    if (!coll || !outLen || (!text && textLen)) {
        return RoboInvalidArgument;
    }
    memset(&ces, 0, sizeof(ces));
    memset(levels, 0, sizeof(levels));

    r = DecodeNfd(text, textLen, &cps, &n);
    if (r != RoboOk) {
        return r;
    }
    r = FormCes(coll, cps, n, &ces);
    if (r != RoboOk) {
        RoboFree(cps);
        return r;
    }

    maxLv = MaxLevel(coll->options.strength);
    for (level = 0; level <= maxLv && level <= 3; level++) {
        r = BuildLevelWeights(coll, &ces, level, &levels[level]);
        if (r != RoboOk) {
            goto cleanup;
        }
        need += levels[level].len * 2u + 1u; /* weights + separator */
    }
    if (maxLv >= 4) {
        need += n * 4u + 1u;
    }

    *outLen = need;
    if (!outKey) {
        r = RoboOk;
        goto cleanup;
    }
    if (outCap < need) {
        r = RoboError;
        goto cleanup;
    }

    for (level = 0; level <= maxLv && level <= 3; level++) {
        size_t i;
        for (i = 0; i < levels[level].len; i++) {
            uint16_t w = levels[level].w[i];
            outKey[pos++] = (uint8_t)((w >> 8) & 0xFFu);
            outKey[pos++] = (uint8_t)(w & 0xFFu);
        }
        outKey[pos++] = KEY_LEVEL_SEP;
    }
    if (maxLv >= 4) {
        size_t i;
        for (i = 0; i < n; i++) {
            uint32_t cp = cps[i];
            outKey[pos++] = (uint8_t)((cp >> 24) & 0xFFu);
            outKey[pos++] = (uint8_t)((cp >> 16) & 0xFFu);
            outKey[pos++] = (uint8_t)((cp >> 8) & 0xFFu);
            outKey[pos++] = (uint8_t)(cp & 0xFFu);
        }
        outKey[pos++] = KEY_LEVEL_SEP;
    }
    *outLen = pos;
    r = RoboOk;

cleanup:
    for (li = 0; li < 5; li++) {
        RoboFree(levels[li].w);
    }
    RoboFree(cps);
    RoboFree(ces.ces);
    return r;
}

Result I18nCompareUtf8Binary(const char *a,
                                            size_t aLen,
                                            const char *b,
                                            size_t bLen,
                                            int *outCmp)
{
    size_t n;
    int c;
    if (!outCmp || (!a && aLen) || (!b && bLen)) {
        return RoboInvalidArgument;
    }
    n = aLen < bLen ? aLen : bLen;
    c = 0;
    if (n) {
        c = memcmp(a, b, n);
    }
    if (c == 0) {
        if (aLen < bLen) {
            c = -1;
        } else if (aLen > bLen) {
            c = 1;
        }
    } else {
        c = c < 0 ? -1 : 1;
    }
    *outCmp = c;
    return RoboOk;
}

Result I18nCompareUtf8Codepoints(const char *a,
                                                size_t aLen,
                                                const char *b,
                                                size_t bLen,
                                                int *outCmp)
{
    size_t ao = 0;
    size_t bo = 0;
    if (!outCmp || (!a && aLen) || (!b && bLen)) {
        return RoboInvalidArgument;
    }
    *outCmp = 0;
    while (ao < aLen && bo < bLen) {
        Uchar32 ca, cb;
        size_t an = 0, bn = 0;
        if (UnicodeUtf8Decode(a, aLen, ao, RoboUnicodeStrict, &ca, &an) !=
                RoboUnicodeOk ||
            UnicodeUtf8Decode(b, bLen, bo, RoboUnicodeStrict, &cb, &bn) !=
                RoboUnicodeOk) {
            return RoboParseError;
        }
        if (ca < cb) {
            *outCmp = -1;
            return RoboOk;
        }
        if (ca > cb) {
            *outCmp = 1;
            return RoboOk;
        }
        ao = an;
        bo = bn;
    }
    if (ao < aLen || bo < bLen) {
        /* ensure remaining is well-formed */
        size_t o = ao < aLen ? ao : bo;
        const char *s = ao < aLen ? a : b;
        size_t len = ao < aLen ? aLen : bLen;
        while (o < len) {
            Uchar32 cp;
            size_t nx = 0;
            if (UnicodeUtf8Decode(s, len, o, RoboUnicodeStrict, &cp, &nx) !=
                RoboUnicodeOk) {
                return RoboParseError;
            }
            o = nx;
        }
        *outCmp = (ao < aLen) ? 1 : -1;
    }
    return RoboOk;
}
