/*
 * RoboI18N transliteration engine (Goal 4.25).
 *
 * Interprets compact tables from generate_translit.py (CLDR transforms).
 * No runtime XML/rule parsing. No Chromium/ICU linkage.
 */

#include "RoboI18N/RoboI18NTranslit.h"

#include "RoboI18N/RoboTranslitData.h"
#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboUnicode/RoboUnicodeNorm.h"

#include <string.h>

/* Static info cache filled on first query. */
static I18nTranslitIdInfo gInfos[64];
static uint8_t gInfosReady;

static const char *PoolStr(uint32_t off)
{
    if (off == 0u) {
        return "";
    }
    return TranslitStrpool + off;
}

static void EnsureInfos(void)
{
    uint32_t i;
    if (gInfosReady) {
        return;
    }
    if (TranslitIdCount > 64u) {
        /* Table larger than cache — still usable via dynamic path in find. */
    }
    for (i = 0; i < TranslitIdCount && i < 64u; i++) {
        const TranslitId *id = &TranslitIds[i];
        gInfos[i].id = PoolStr(id->idOff);
        gInfos[i].direction = id->direction;
        gInfos[i].reversible = id->reversible;
        gInfos[i].sourceScript = (UnicodeScript)id->sourceScript;
        gInfos[i].targetScript = (UnicodeScript)id->targetScript;
    }
    gInfosReady = 1;
}

uint32_t I18nTranslitIdCount(void)
{
    return TranslitIdCount;
}

const char *I18nTranslitCldrVersionString(void)
{
    return RoboTranslitDataCldrVersion;
}

static int IdMatch(const char *needle, const TranslitId *id)
{
    const char *canon = PoolStr(id->idOff);
    const char *aliases;
    if (strcmp(canon, needle) == 0) {
        return 1;
    }
    if (id->aliasOff == 0u) {
        return 0;
    }
    aliases = PoolStr(id->aliasOff);
    while (*aliases) {
        if (strcmp(aliases, needle) == 0) {
            return 1;
        }
        aliases += strlen(aliases) + 1;
    }
    return 0;
}

Result I18nTranslitFindId(const char *id, uint32_t *outIndex)
{
    uint32_t i;
    if (!id || !outIndex) {
        return RoboInvalidArgument;
    }
    for (i = 0; i < TranslitIdCount; i++) {
        if (IdMatch(id, &TranslitIds[i])) {
            *outIndex = i;
            return RoboOk;
        }
    }
    return RoboNotFound;
}

Result GetI18nTranslitIdInfo(uint32_t index,
                                         I18nTranslitIdInfo *out)
{
    if (!out || index >= TranslitIdCount) {
        return RoboInvalidArgument;
    }
    EnsureInfos();
    if (index < 64u) {
        *out = gInfos[index];
    } else {
        const TranslitId *id = &TranslitIds[index];
        out->id = PoolStr(id->idOff);
        out->direction = id->direction;
        out->reversible = id->reversible;
        out->sourceScript = (UnicodeScript)id->sourceScript;
        out->targetScript = (UnicodeScript)id->targetScript;
    }
    return RoboOk;
}

Result I18nTransliteratorCreate(uint32_t idIndex,
                                              const I18nLocale *locale,
                                              I18nTransliterator *out)
{
    (void)locale;
    if (!out || idIndex >= TranslitIdCount) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    EnsureInfos();
    out->idIndex = idIndex;
    out->info = (idIndex < 64u) ? &gInfos[idIndex] : NULL;
    out->ready = 1;
    return RoboOk;
}

Result I18nTransliteratorCreateId(const char *id,
                                                 const I18nLocale *locale,
                                                 I18nTransliterator *out)
{
    uint32_t idx = 0;
    Result r = I18nTranslitFindId(id, &idx);
    if (r != RoboOk) {
        return r;
    }
    return I18nTransliteratorCreate(idx, locale, out);
}

void I18nTransliteratorDestroy(I18nTransliterator *tr)
{
    if (!tr) {
        return;
    }
    memset(tr, 0, sizeof(*tr));
}

static int SetContains(uint16_t setId, uint32_t cp)
{
    const TranslitSet *set;
    uint32_t i;
    if (setId == 0u || setId >= TranslitSetCount) {
        return 1; /* no constraint */
    }
    set = &TranslitSets[setId];
    for (i = 0; i < set->rangeCount; i++) {
        const TranslitRange *rg =
            &TranslitRanges[set->rangeBegin + i];
        if (cp >= rg->start && cp <= rg->end) {
            return 1;
        }
    }
    return 0;
}

static int RuleMatchesB(const TranslitRule *rule,
                          const uint32_t *cps,
                          size_t len,
                          size_t pos)
{
    size_t k;
    if (rule->keyLen == 0u || pos + rule->keyLen > len) {
        return 0;
    }
    if (rule->beforeSet != 0u) {
        if (pos == 0u) {
            /* Start of text: match if set contains any Cc/Zs/punctuation —
             * check space and NUL-range. */
            if (!SetContains(rule->beforeSet, (uint32_t)' ') &&
                !SetContains(rule->beforeSet, 0u) &&
                !SetContains(rule->beforeSet, (uint32_t)'.')) {
                /* Still allow if set is a large complement-like set
                 * (rangeCount high). */
                const TranslitSet *set = &TranslitSets[rule->beforeSet];
                if (set->rangeCount < 8u) {
                    return 0;
                }
            }
        } else if (!SetContains(rule->beforeSet, cps[pos - 1u])) {
            return 0;
        }
    }
    for (k = 0; k < rule->keyLen; k++) {
        if (cps[pos + k] != TranslitCps[rule->keyOff + k]) {
            return 0;
        }
    }
    if (rule->afterSet != 0u) {
        size_t afterPos = pos + rule->keyLen;
        if (afterPos >= len) {
            /* End of text similar to start */
            const TranslitSet *set = &TranslitSets[rule->afterSet];
            if (set->rangeCount < 8u &&
                !SetContains(rule->afterSet, (uint32_t)' ') &&
                !SetContains(rule->afterSet, 0u)) {
                return 0;
            }
        } else if (!SetContains(rule->afterSet, cps[afterPos])) {
            return 0;
        }
    }
    return 1;
}

static Result NormalizeBuf(uint32_t **cps,
                                   size_t *inoutLen,
                                   size_t *inoutCap,
                                   UnicodeNormForm form)
{
    Uchar32 *tmp = NULL;
    size_t n = 0;
    Result r =
        UnicodeNormalize((const Uchar32 *)*cps, *inoutLen, form, &tmp,
                               &n);
    if (r != RoboOk) {
        return r;
    }
    RoboFree(*cps);
    *cps = (uint32_t *)tmp;
    *inoutLen = n;
    *inoutCap = n;
    return RoboOk;
}

static Result LowerBuf(uint32_t **cps, size_t *inoutLen, size_t *inoutCap)
{
    Uchar32 *tmp = NULL;
    size_t n = 0;
    Result r = UnicodeCaseMapCps((const Uchar32 *)*cps,
                                                *inoutLen, RoboUnicodeCaseLower,
                                                &tmp, &n);
    if (r != RoboOk) {
        return r;
    }
    if (!tmp) {
        *inoutLen = 0;
        return RoboOk;
    }
    RoboFree(*cps);
    *cps = (uint32_t *)tmp;
    *inoutLen = n;
    *inoutCap = n;
    return RoboOk;
}

static Result HangulDecomp(uint32_t **cps, size_t *inoutLen, size_t *inoutCap)
{
    enum {
        SBase = 0xAC00,
        LBase = 0x1100,
        VBase = 0x1161,
        TBase = 0x11A7,
        LCount = 19,
        VCount = 21,
        TCount = 28,
        NCount = VCount * TCount,
        SCount = LCount * NCount
    };
    uint32_t *out;
    size_t i, outLen = 0;
    size_t len = *inoutLen;
    size_t cap = len * 3u + 8u;
    Result r;

    if (cap > RoboI18nTranslitOutputMax) {
        cap = RoboI18nTranslitOutputMax;
    }
    out = (uint32_t *)Malloc(cap * sizeof(uint32_t));
    if (!out) {
        return RoboOutOfMemory;
    }
    for (i = 0; i < len; i++) {
        uint32_t s = (*cps)[i];
        if (s >= SBase && s < SBase + SCount) {
            uint32_t SIndex = s - SBase;
            uint32_t L = LBase + SIndex / NCount;
            uint32_t V = VBase + (SIndex % NCount) / TCount;
            uint32_t T = TBase + SIndex % TCount;
            if (outLen + 2u + (T != TBase ? 1u : 0u) > cap) {
                RoboFree(out);
                return RoboInvalidArgument;
            }
            out[outLen++] = L;
            out[outLen++] = V;
            if (T != TBase) {
                out[outLen++] = T;
            }
        } else {
            if (outLen + 1u > cap) {
                RoboFree(out);
                return RoboInvalidArgument;
            }
            out[outLen++] = s;
        }
    }
    (void)r;
    RoboFree(*cps);
    *cps = out;
    *inoutLen = outLen;
    *inoutCap = cap;
    return RoboOk;
}

static Result ApplyRules(const TranslitStage *stage,
                                 uint32_t **cps,
                                 size_t *inoutLen,
                                 size_t *inoutCap,
                                 uint32_t *steps)
{
    uint32_t *out = NULL;
    size_t outLen = 0;
    size_t pos = 0;
    size_t len = *inoutLen;
    size_t cap = *inoutCap;
    uint32_t rb = stage->ruleBegin;
    uint32_t rc = stage->ruleCount;

    out = (uint32_t *)Malloc(cap * sizeof(uint32_t));
    if (!out) {
        return RoboOutOfMemory;
    }

    while (pos < len) {
        uint32_t ri;
        int matched = 0;
        if (*steps >= RoboI18nTranslitStepMax) {
            RoboFree(out);
            return RoboInvalidArgument;
        }
        (*steps)++;
        for (ri = 0; ri < rc; ri++) {
            const TranslitRule *rule = &TranslitRules[rb + ri];
            if (RuleMatchesB(rule, *cps, len, pos)) {
                uint16_t k;
                if (outLen + rule->replLen > cap) {
                    uint32_t *grown;
                    size_t ncap = cap * 2u;
                    if (ncap < outLen + rule->replLen + 64u) {
                        ncap = outLen + rule->replLen + 64u;
                    }
                    if (ncap > RoboI18nTranslitOutputMax) {
                        ncap = RoboI18nTranslitOutputMax;
                    }
                    if (outLen + rule->replLen > ncap) {
                        RoboFree(out);
                        return RoboInvalidArgument;
                    }
                    grown = (uint32_t *)Malloc(ncap * sizeof(uint32_t));
                    if (!grown) {
                        RoboFree(out);
                        return RoboOutOfMemory;
                    }
                    memcpy(grown, out, outLen * sizeof(uint32_t));
                    RoboFree(out);
                    out = grown;
                    cap = ncap;
                }
                for (k = 0; k < rule->replLen; k++) {
                    out[outLen++] = TranslitCps[rule->replOff + k];
                }
                pos += rule->keyLen;
                matched = 1;
                break;
            }
        }
        if (!matched) {
            if (outLen + 1u > cap) {
                uint32_t *grown;
                size_t ncap = cap * 2u + 64u;
                if (ncap > RoboI18nTranslitOutputMax) {
                    ncap = RoboI18nTranslitOutputMax;
                }
                if (outLen + 1u > ncap) {
                    RoboFree(out);
                    return RoboInvalidArgument;
                }
                grown = (uint32_t *)Malloc(ncap * sizeof(uint32_t));
                if (!grown) {
                    RoboFree(out);
                    return RoboOutOfMemory;
                }
                memcpy(grown, out, outLen * sizeof(uint32_t));
                RoboFree(out);
                out = grown;
                cap = ncap;
            }
            out[outLen++] = (*cps)[pos++];
        }
    }
    RoboFree(*cps);
    *cps = out;
    *inoutLen = outLen;
    *inoutCap = cap;
    return RoboOk;
}

static Result RunPipeline(uint32_t idIndex, uint32_t **cps, size_t *inoutLen,
                                  size_t *inoutCap)
{
    const TranslitId *id = &TranslitIds[idIndex];
    uint32_t si;
    uint32_t steps = 0;
    for (si = 0; si < id->stageCount; si++) {
        const TranslitStage *st =
            &TranslitStages[id->stageBegin + si];
        Result r = RoboOk;
        switch (st->kind) {
        case RoboTranslitStageNfd:
            r = NormalizeBuf(cps, inoutLen, inoutCap, RoboUnicodeNormNfd);
            break;
        case RoboTranslitStageNfc:
            r = NormalizeBuf(cps, inoutLen, inoutCap, RoboUnicodeNormNfc);
            break;
        case RoboTranslitStageNfkd:
            r = NormalizeBuf(cps, inoutLen, inoutCap, RoboUnicodeNormNfkd);
            break;
        case RoboTranslitStageNfkc:
            r = NormalizeBuf(cps, inoutLen, inoutCap, RoboUnicodeNormNfkc);
            break;
        case RoboTranslitStageLower:
            r = LowerBuf(cps, inoutLen, inoutCap);
            break;
        case RoboTranslitStageNull:
            break;
        case RoboTranslitStageHangulDecomp:
            r = HangulDecomp(cps, inoutLen, inoutCap);
            break;
        case RoboTranslitStageRules:
            r = ApplyRules(st, cps, inoutLen, inoutCap, &steps);
            break;
        default:
            break;
        }
        if (r != RoboOk) {
            return r;
        }
    }
    return RoboOk;
}

static Result DecodeAll(const char *utf8,
                                size_t utf8Len,
                                uint32_t *cps,
                                size_t cap,
                                size_t *outN)
{
    size_t off = 0;
    size_t n = 0;
    while (off < utf8Len) {
        Uchar32 cp = 0;
        size_t next = 0;
        UnicodeStatus st =
            UnicodeUtf8Decode(utf8, utf8Len, off, RoboUnicodeStrict, &cp,
                                     &next);
        if (st != RoboUnicodeOk) {
            return RoboParseError;
        }
        if (n >= cap) {
            return RoboInvalidArgument;
        }
        cps[n++] = cp;
        off = next;
    }
    *outN = n;
    return RoboOk;
}

Result I18nTransliterate(const I18nTransliterator *tr,
                                      const char *utf8,
                                      size_t utf8Len,
                                      char *outBuf,
                                      size_t outCap,
                                      size_t *outLen,
                                      I18nTranslitMapEntry *mapOut,
                                      size_t mapCap,
                                      size_t *mapCount)
{
    uint32_t *cps = NULL;
    size_t n = 0;
    size_t cpCap;
    size_t written = 0;
    Result r;

    if (!tr || !tr->ready || (!utf8 && utf8Len) || !outBuf || !outLen) {
        return RoboInvalidArgument;
    }
    if (utf8Len > RoboI18nTranslitInputMax) {
        return RoboInvalidArgument;
    }
    if (mapCount) {
        *mapCount = 0;
    }

    cpCap = utf8Len + 64u;
    if (cpCap < 64u) {
        cpCap = 64u;
    }
    if (cpCap > RoboI18nTranslitOutputMax) {
        cpCap = RoboI18nTranslitOutputMax;
    }
    cps = (uint32_t *)Malloc(cpCap * sizeof(uint32_t));
    if (!cps) {
        return RoboOutOfMemory;
    }

    r = DecodeAll(utf8, utf8Len, cps, cpCap, &n);
    if (r != RoboOk) {
        RoboFree(cps);
        return r;
    }

    r = RunPipeline(tr->idIndex, &cps, &n, &cpCap);
    if (r != RoboOk) {
        RoboFree(cps);
        return r;
    }

    {
        UnicodeStatus st = UnicodeUtf32ToUtf8(
            (const Uchar32 *)cps, n, outBuf, outCap, &written,
            RoboUnicodeStrict);
        if (st == RoboUnicodeNoSpace) {
            RoboFree(cps);
            return RoboInvalidArgument;
        }
        if (st != RoboUnicodeOk) {
            RoboFree(cps);
            return RoboParseError;
        }
    }
    if (written > RoboI18nTranslitOutputMax) {
        RoboFree(cps);
        return RoboInvalidArgument;
    }
    *outLen = written;
    if (outCap > written) {
        outBuf[written] = '\0';
    }

    if (mapOut && mapCap > 0 && mapCount) {
        mapOut[0].srcStart = 0;
        mapOut[0].srcEnd = utf8Len;
        mapOut[0].outStart = 0;
        mapOut[0].outEnd = written;
        *mapCount = 1;
    }

    RoboFree(cps);
    return RoboOk;
}

Result I18nTransliteratorStreamReset(I18nTransliterator *tr)
{
    if (!tr || !tr->ready) {
        return RoboInvalidArgument;
    }
    tr->holdLen = 0;
    return RoboOk;
}

Result I18nTransliteratorStreamFeed(I18nTransliterator *tr,
                                                   const char *utf8,
                                                   size_t utf8Len,
                                                   char *outBuf,
                                                   size_t outCap,
                                                   size_t *outLen)
{
    uint32_t *cps = NULL;
    size_t n = 0;
    size_t cpCap;
    size_t i;
    size_t commitN;
    size_t written = 0;
    Result r;

    if (!tr || !tr->ready || !outBuf || !outLen) {
        return RoboInvalidArgument;
    }
    if (utf8Len > RoboI18nTranslitInputMax) {
        return RoboInvalidArgument;
    }
    *outLen = 0;
    cpCap = (size_t)tr->holdLen + utf8Len + 64u;
    if (cpCap > RoboI18nTranslitOutputMax) {
        cpCap = RoboI18nTranslitOutputMax;
    }
    cps = (uint32_t *)Malloc(cpCap * sizeof(uint32_t));
    if (!cps) {
        return RoboOutOfMemory;
    }
    for (i = 0; i < tr->holdLen; i++) {
        cps[i] = tr->hold[i];
    }
    n = tr->holdLen;
    r = DecodeAll(utf8, utf8Len, cps + n, cpCap - n, &i);
    if (r != RoboOk) {
        RoboFree(cps);
        return r;
    }
    n += i;

    r = RunPipeline(tr->idIndex, &cps, &n, &cpCap);
    if (r != RoboOk) {
        RoboFree(cps);
        return r;
    }

    if (n > RoboI18nTranslitStreamLookaheadMax) {
        commitN = n - RoboI18nTranslitStreamLookaheadMax;
        tr->holdLen = (uint16_t)RoboI18nTranslitStreamLookaheadMax;
        memcpy(tr->hold, cps + commitN, tr->holdLen * sizeof(uint32_t));
    } else {
        commitN = 0;
        tr->holdLen = (uint16_t)n;
        memcpy(tr->hold, cps, n * sizeof(uint32_t));
    }

    if (commitN > 0) {
        UnicodeStatus st = UnicodeUtf32ToUtf8(
            (const Uchar32 *)cps, commitN, outBuf, outCap, &written,
            RoboUnicodeStrict);
        if (st != RoboUnicodeOk) {
            RoboFree(cps);
            return (st == RoboUnicodeNoSpace) ? RoboInvalidArgument
                                                 : RoboParseError;
        }
        *outLen = written;
        if (outCap > written) {
            outBuf[written] = '\0';
        }
    }
    RoboFree(cps);
    return RoboOk;
}

Result I18nTransliteratorStreamFinish(I18nTransliterator *tr,
                                                     char *outBuf,
                                                     size_t outCap,
                                                     size_t *outLen)
{
    size_t written = 0;
    if (!tr || !tr->ready || !outBuf || !outLen) {
        return RoboInvalidArgument;
    }
    *outLen = 0;
    if (tr->holdLen == 0) {
        if (outCap > 0) {
            outBuf[0] = '\0';
        }
        return RoboOk;
    }
    {
        UnicodeStatus st = UnicodeUtf32ToUtf8(
            (const Uchar32 *)tr->hold, tr->holdLen, outBuf, outCap,
            &written, RoboUnicodeStrict);
        tr->holdLen = 0;
        if (st != RoboUnicodeOk) {
            return (st == RoboUnicodeNoSpace) ? RoboInvalidArgument
                                                 : RoboParseError;
        }
        *outLen = written;
        if (outCap > written) {
            outBuf[written] = '\0';
        }
    }
    return RoboOk;
}
