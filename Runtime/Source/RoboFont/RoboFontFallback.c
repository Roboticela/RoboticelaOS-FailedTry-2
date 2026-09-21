/*
 * RoboFontFallback — coverage-first resolver (Goal 5.15).
 */

#include "RoboFont/RoboFontFallback.h"

#include "RoboFont/RoboFontClassification.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define RESOLVER_COOKIE 0x5246424Bu /* 'RFBK' */

typedef struct {
    uint64_t registryGeneration;
    FontFaceId primaryId;
    uint32_t cpHash;
    uint32_t codepointCount;
    uint32_t cps[RoboFontFallbackMaxCodepoints];
    FontWeight weight;
    FontStretch stretch;
    FontStyle style;
    uint32_t langHash;
    UnicodeScript script;
    uint8_t hasScript;
    uint32_t flags;
    uint32_t PolicyHash;
    /* value */
    uint8_t usedPrimary;
    uint8_t found;
    FontFallbackStage stage;
    FontFaceId faceId;
    FontFallbackScore score;
    uint32_t primaryMissingCount;
    uint64_t lruTick;
} FallbackCacheEntry;

struct FontFallbackResolver {
    uint32_t cookie;
    FontRegistry *registry; /* borrowed */
    FontFallbackPolicy policy;
    /* owned copies of policy string lists (pointers into owned blob) */
    char **preferredOwned;
    uint32_t preferredOwnedCount;
    char **langPrefOwned;
    uint32_t langPrefOwnedCount;
    char *lastResortOwned;

    FallbackCacheEntry *cache;
    uint32_t cacheCap;
    uint32_t cacheCount;
    uint64_t lruTick;
    FontFallbackCacheStats stats;
};

/* ---- helpers ---- */

static uint32_t HashBytes(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0u; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t HashCstr(const char *s, size_t len)
{
    if (!s) {
        return 0u;
    }
    if (len == 0u) {
        len = strlen(s);
    }
    return HashBytes(s, len);
}

static int Utf8CasefoldEq(const char *a, size_t al, const char *b, size_t bl)
{
    int cmp = 1;
    if (!a || !b) {
        return 0;
    }
    if (al == 0u) {
        al = strlen(a);
    }
    if (bl == 0u) {
        bl = strlen(b);
    }
    if (UnicodeCasefoldCompareUtf8(a, al, b, bl, RoboUnicodeReplace, &cmp) !=
        RoboOk) {
        return 0;
    }
    return cmp == 0;
}

static int FamilyEq(const char *a, const char *b)
{
    if (!a || !b || !a[0] || !b[0]) {
        return 0;
    }
    return Utf8CasefoldEq(a, 0u, b, 0u);
}

static Result EnsureFaceReady(FontFace *face)
{
    Result res;
    if (!face) {
        return RoboInvalidArgument;
    }
    res = FontFaceParseClassification(face);
    if (res != RoboOk && res != RoboNotFound && res != RoboNotSupported) {
        return res;
    }
    res = FontFaceParseCoverage(face);
    if (res == RoboNotFound || res == RoboNotSupported) {
        return RoboOk; /* empty coverage treated as uncovered */
    }
    return res;
}

static int FaceCoversCp(FontFace *face, uint32_t cp)
{
    const FontCoverage *cov = FontFaceCoverage(face);
    if (!cov) {
        return 0;
    }
    return FontCoverageContains(cov, cp);
}

Result FontFallbackFaceCovers(FontFace *face,
                                             const uint32_t *codepoints,
                                             uint32_t count,
                                             uint32_t flags,
                                             int *outCoversAll,
                                             int *outCoversBase,
                                             uint32_t *outMissingCount)
{
    uint32_t i;
    uint32_t missing = 0u;
    int all = 1;
    int base = 0;
    Result res;
    int requireAll = (flags & RoboFontFallbackFlagRequireAll) != 0;

    if (!face || !codepoints || count == 0u) {
        return RoboInvalidArgument;
    }
    /* Default safety: if neither flag bit is meaningful, require all. */
    if ((flags & RoboFontFallbackFlagRequireAll) == 0u &&
        (flags & RoboFontFallbackFlagNoCache) == 0u && flags == 0u) {
        requireAll = 1;
    }

    res = EnsureFaceReady(face);
    if (res != RoboOk) {
        return res;
    }

    for (i = 0u; i < count; i++) {
        if (!FontCmapIsUnicodeScalar(codepoints[i])) {
            return RoboInvalidArgument;
        }
        if (FaceCoversCp(face, codepoints[i])) {
            if (i == 0u) {
                base = 1;
            }
        } else {
            missing++;
            all = 0;
            if (i == 0u) {
                base = 0;
            }
        }
    }

    if (!requireAll) {
        /* Sequence foundation / partial mode: base scalar is the gate. */
        all = base;
    }

    if (outCoversAll) {
        *outCoversAll = all;
    }
    if (outCoversBase) {
        *outCoversBase = base;
    }
    if (outMissingCount) {
        *outMissingCount = missing;
    }
    return RoboOk;
}

/* ---- init / validate ---- */

void FontFallbackPolicyInit(FontFallbackPolicy *policy)
{
    if (!policy) {
        return;
    }
    memset(policy, 0, sizeof(*policy));
    policy->flags = RoboFontFallbackFlagRequireAll;
    policy->maxScan = RoboFontFallbackMaxScan;
    policy->cacheCapacity = RoboFontFallbackCacheDefault;
}

void FontFallbackRequestInit(FontFallbackRequest *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
    req->weight = RoboFontWeightNormal;
    req->stretch = RoboFontStretchNormal;
    req->style = RoboFontStyleNormal;
    req->flags = RoboFontFallbackFlagRequireAll;
}

Result FontFallbackRequestValidate(
    const FontFallbackRequest *req)
{
    uint32_t i;
    if (!req || !req->primary || !req->codepoints || req->codepointCount == 0u) {
        return RoboInvalidArgument;
    }
    if (req->codepointCount > RoboFontFallbackMaxCodepoints) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < req->codepointCount; i++) {
        if (!FontCmapIsUnicodeScalar(req->codepoints[i])) {
            return RoboInvalidArgument;
        }
    }
    if (!FontWeightIsValid(req->weight) ||
        !FontStretchIsValid(req->stretch)) {
        return RoboInvalidArgument;
    }
    if (req->preferredFamilyCount > RoboFontFallbackMaxPreferred) {
        return RoboInvalidArgument;
    }
    if (req->preferredFamilyCount != 0u && !req->preferredFamilies) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

void FontFallbackResultInit(FontFallbackResult *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->cookie = RoboFontFallbackResultCookie;
    out->stage = RoboFontFallbackStageNone;
}

void FontFallbackResultClear(FontFallbackResult *res)
{
    if (!res) {
        return;
    }
    if (res->cookie == RoboFontFallbackResultCookie && res->face) {
        FontFaceRelease(res->face);
        res->face = NULL;
    }
    FontFallbackResultInit(res);
}

int FontFallbackScoreCompare(const FontFallbackScore *a,
                                     const FontFallbackScore *b)
{
    if (!a || !b) {
        return 0;
    }
    if (a->eligible != b->eligible) {
        return a->eligible ? -1 : 1;
    }
    if (!a->eligible) {
        return 0;
    }
#define CMP_U32(field)                             \
    do {                                           \
        if (a->field < b->field) {                 \
            return -1;                             \
        }                                          \
        if (a->field > b->field) {                 \
            return 1;                              \
        }                                          \
    } while (0)
    CMP_U32(stageRank);
    CMP_U32(preferredFamilyRank);
    CMP_U32(LanguageRank);
    CMP_U32(scriptRank);
    CMP_U32(stretchDist);
    CMP_U32(styleDist);
    CMP_U32(weightDist);
    if (a->faceId < b->faceId) {
        return -1;
    }
    if (a->faceId > b->faceId) {
        return 1;
    }
#undef CMP_U32
    return 0;
}

/* ---- policy ownership ---- */

static void FreeOwnedList(char **list, uint32_t n)
{
    uint32_t i;
    if (!list) {
        return;
    }
    for (i = 0u; i < n; i++) {
        RoboFree(list[i]);
    }
    RoboFree(list);
}

static Result DupCstrList(const char *const *src, uint32_t n, char ***out)
{
    char **dst;
    uint32_t i;
    *out = NULL;
    if (n == 0u) {
        return RoboOk;
    }
    if (!src) {
        return RoboInvalidArgument;
    }
    dst = (char **)RoboCalloc(n, sizeof(char *));
    if (!dst) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < n; i++) {
        size_t len;
        if (!src[i]) {
            FreeOwnedList(dst, i);
            return RoboInvalidArgument;
        }
        len = strlen(src[i]);
        if (len > RoboFontFallbackMaxFamilyBytes) {
            FreeOwnedList(dst, i);
            return RoboInvalidArgument;
        }
        dst[i] = (char *)Malloc(len + 1u);
        if (!dst[i]) {
            FreeOwnedList(dst, i);
            return RoboOutOfMemory;
        }
        memcpy(dst[i], src[i], len + 1u);
    }
    *out = dst;
    return RoboOk;
}

static uint32_t PolicyHash(const FontFallbackPolicy *p)
{
    uint32_t h = 0u;
    uint32_t i;
    if (!p) {
        return 0u;
    }
    h ^= p->flags;
    h ^= p->maxScan * 131u;
    h ^= (uint32_t)p->lastResortFaceId;
    for (i = 0u; i < p->preferredFamilyCount; i++) {
        h ^= HashCstr(p->preferredFamilies[i], 0u) * (i + 3u);
    }
    for (i = 0u; i < p->languagePreferredFamilyCount; i++) {
        h ^= HashCstr(p->languagePreferredFamilies[i], 0u) * (i + 7u);
    }
    if (p->lastResortFamily) {
        h ^= HashCstr(p->lastResortFamily, p->lastResortFamilyLen);
    }
    return h;
}

static Result ApplyPolicy(FontFallbackResolver *r,
                                  const FontFallbackPolicy *src)
{
    FontFallbackPolicy tmp;
    char **pref = NULL;
    char **lang = NULL;
    char *last = NULL;
    Result res;

    if (!src) {
        FontFallbackPolicyInit(&tmp);
        src = &tmp;
    }
    if (src->preferredFamilyCount > RoboFontFallbackMaxPreferred ||
        src->languagePreferredFamilyCount > RoboFontFallbackMaxPreferred) {
        return RoboInvalidArgument;
    }

    res = DupCstrList(src->preferredFamilies, src->preferredFamilyCount, &pref);
    if (res != RoboOk) {
        return res;
    }
    res = DupCstrList(src->languagePreferredFamilies,
                        src->languagePreferredFamilyCount, &lang);
    if (res != RoboOk) {
        FreeOwnedList(pref, src->preferredFamilyCount);
        return res;
    }
    if (src->lastResortFamily) {
        size_t len = src->lastResortFamilyLen
                         ? src->lastResortFamilyLen
                         : strlen(src->lastResortFamily);
        if (len > RoboFontFallbackMaxFamilyBytes) {
            FreeOwnedList(pref, src->preferredFamilyCount);
            FreeOwnedList(lang, src->languagePreferredFamilyCount);
            return RoboInvalidArgument;
        }
        last = (char *)Malloc(len + 1u);
        if (!last) {
            FreeOwnedList(pref, src->preferredFamilyCount);
            FreeOwnedList(lang, src->languagePreferredFamilyCount);
            return RoboOutOfMemory;
        }
        memcpy(last, src->lastResortFamily, len);
        last[len] = '\0';
    }

    FreeOwnedList(r->preferredOwned, r->preferredOwnedCount);
    FreeOwnedList(r->langPrefOwned, r->langPrefOwnedCount);
    RoboFree(r->lastResortOwned);

    r->preferredOwned = pref;
    r->preferredOwnedCount = src->preferredFamilyCount;
    r->langPrefOwned = lang;
    r->langPrefOwnedCount = src->languagePreferredFamilyCount;
    r->lastResortOwned = last;

    r->policy = *src;
    r->policy.preferredFamilies =
        (const char *const *)r->preferredOwned;
    r->policy.languagePreferredFamilies =
        (const char *const *)r->langPrefOwned;
    r->policy.lastResortFamily = r->lastResortOwned;
    r->policy.lastResortFamilyLen = 0u;
    if ((r->policy.flags & RoboFontFallbackFlagRequireAll) == 0u &&
        (src->flags & RoboFontFallbackFlagNoCache) == 0u &&
        src->flags == 0u) {
        r->policy.flags |= RoboFontFallbackFlagRequireAll;
    }
    if (r->policy.maxScan == 0u || r->policy.maxScan > RoboFontFallbackMaxScan) {
        r->policy.maxScan = RoboFontFallbackMaxScan;
    }
    return RoboOk;
}

Result FontFallbackResolverCreate(
    FontRegistry *registry,
    const FontFallbackPolicy *defaultPolicy,
    FontFallbackResolver **out)
{
    FontFallbackResolver *r;
    FontFallbackPolicy pol;
    uint32_t cap;
    Result res;

    if (!out || !registry) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (!defaultPolicy) {
        FontFallbackPolicyInit(&pol);
        defaultPolicy = &pol;
    }
    cap = defaultPolicy->cacheCapacity
              ? defaultPolicy->cacheCapacity
              : RoboFontFallbackCacheDefault;
    if (cap > RoboFontFallbackCacheHard) {
        cap = RoboFontFallbackCacheHard;
    }

    r = (FontFallbackResolver *)RoboCalloc(1u, sizeof(*r));
    if (!r) {
        return RoboOutOfMemory;
    }
    r->cookie = RESOLVER_COOKIE;
    r->registry = registry;
    r->cacheCap = cap;
    if (cap > 0u) {
        r->cache = (FallbackCacheEntry *)RoboCalloc(cap, sizeof(*r->cache));
        if (!r->cache) {
            RoboFree(r);
            return RoboOutOfMemory;
        }
    }
    res = ApplyPolicy(r, defaultPolicy);
    if (res != RoboOk) {
        RoboFree(r->cache);
        RoboFree(r);
        return res;
    }
    *out = r;
    return RoboOk;
}

void FontFallbackResolverDestroy(FontFallbackResolver *r)
{
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return;
    }
    FreeOwnedList(r->preferredOwned, r->preferredOwnedCount);
    FreeOwnedList(r->langPrefOwned, r->langPrefOwnedCount);
    RoboFree(r->lastResortOwned);
    RoboFree(r->cache);
    memset(r, 0, sizeof(*r));
    RoboFree(r);
}

Result FontFallbackResolverSetPolicy(
    FontFallbackResolver *r,
    const FontFallbackPolicy *policy)
{
    Result res;
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return RoboInvalidArgument;
    }
    res = ApplyPolicy(r, policy);
    if (res == RoboOk) {
        FontFallbackResolverCacheClear(r);
    }
    return res;
}

const FontFallbackPolicy *FontFallbackResolverPolicy(
    const FontFallbackResolver *r)
{
    return (r && r->cookie == RESOLVER_COOKIE) ? &r->policy : NULL;
}

void FontFallbackResolverCacheClear(FontFallbackResolver *r)
{
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return;
    }
    if (r->cache && r->cacheCap) {
        memset(r->cache, 0, (size_t)r->cacheCap * sizeof(*r->cache));
    }
    r->cacheCount = 0u;
    r->stats.invalidations++;
}

void FontFallbackResolverCacheStats(const FontFallbackResolver *r,
                                             FontFallbackCacheStats *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return;
    }
    *out = r->stats;
    out->entries = r->cacheCount;
}

/* ---- scoring / resolve ---- */

static const char *RegFamily(const FontRegistration *reg)
{
    return reg && reg->family[0] ? reg->family : "";
}

static uint32_t PreferredRank(const char *family,
                               const char *const *listA,
                               uint32_t na,
                               const char *const *listB,
                               uint32_t nb)
{
    uint32_t i;
    uint32_t none = 0xFFFFFFFFu;
    if (!family || !family[0]) {
        return none;
    }
    for (i = 0u; i < na; i++) {
        if (FamilyEq(family, listA[i])) {
            return i;
        }
    }
    for (i = 0u; i < nb; i++) {
        if (FamilyEq(family, listB[i])) {
            return na + i;
        }
    }
    return none;
}

static uint32_t LanguageRank(const char *family,
                              const char *language,
                              const FontFallbackPolicy *pol)
{
    uint32_t i;
    if (!language || !language[0] || !pol) {
        return 0u; /* neutral */
    }
    if (pol->languagePreferredFamilyCount == 0u) {
        return 1u; /* language asked but no configured hints */
    }
    for (i = 0u; i < pol->languagePreferredFamilyCount; i++) {
        if (FamilyEq(family, pol->languagePreferredFamilies[i])) {
            return 0u; /* best */
        }
    }
    return 2u;
}

static UnicodeScript DeriveScript(uint32_t cp)
{
    UnicodeScript sc = MakeUnicodeScript((Uchar32)cp);
    if (sc == RoboUScriptCommon || sc == RoboUScriptInherited ||
        sc == RoboUScriptUnknown) {
        /* Prefer a concrete SCX entry when present. */
        UnicodeScript scx[8];
        size_t n = UnicodeScriptExtensions((Uchar32)cp, scx, 8u);
        size_t i;
        for (i = 0u; i < n && i < 8u; i++) {
            if (scx[i] != RoboUScriptCommon && scx[i] != RoboUScriptInherited &&
                scx[i] != RoboUScriptUnknown) {
                return scx[i];
            }
        }
        return RoboUScriptUnknown;
    }
    return sc;
}

static Result ScoreCandidate(FontFallbackResolver *r,
                                     const FontFallbackRequest *req,
                                     const FontFallbackPolicy *pol,
                                     FontFace *face,
                                     const FontRegistration *reg,
                                     FontFallbackStage stage,
                                     UnicodeScript wantScript,
                                     FontFallbackScore *out)
{
    int coversAll = 0;
    int coversBase = 0;
    uint32_t missing = 0u;
    uint32_t flags;
    const FontClassification *cls;
    FontStyleDesc style;
    Result res;
    const char *family;

    (void)r;
    memset(out, 0, sizeof(*out));
    out->stage = stage;
    out->stageRank = (uint32_t)stage;
    out->faceId = GetFontFaceId(face);
    out->preferredFamilyRank = 0xFFFFFFFFu;

    flags = pol->flags | req->flags;
    if ((flags & RoboFontFallbackFlagRequireAll) == 0u) {
        flags |= RoboFontFallbackFlagRequireAll;
    }

    res = FontFallbackFaceCovers(face, req->codepoints, req->codepointCount,
                                         flags, &coversAll, &coversBase, &missing);
    if (res != RoboOk) {
        return res;
    }
    out->coversAll = (uint8_t)coversAll;
    out->coversBase = (uint8_t)coversBase;
    if (!coversAll) {
        out->eligible = 0;
        return RoboOk;
    }
    out->eligible = 1;

    family = RegFamily(reg);
    out->preferredFamilyRank =
        PreferredRank(family, req->preferredFamilies, req->preferredFamilyCount,
                       pol->preferredFamilies, pol->preferredFamilyCount);
    out->LanguageRank = LanguageRank(family, req->language, pol);

    {
        const FontCoverage *cov = FontFaceCoverage(face);
        if (wantScript != RoboUScriptUnknown && cov &&
            FontCoverageHasScript(cov, wantScript)) {
            out->scriptRank = 0u;
        } else if (wantScript == RoboUScriptUnknown) {
            out->scriptRank = 0u;
        } else {
            out->scriptRank = 1u;
        }
    }

    res = FontFaceStyle(face, &style);
    if (res != RoboOk) {
        style.weight = RoboFontWeightNormal;
        style.stretch = RoboFontStretchNormal;
        style.style = RoboFontStyleNormal;
    }
    cls = FontFaceClassification(face);
    out->wwsFlag = cls ? cls->wwsFlag : 0u;
    out->stretchDist = FontSelectStretchDist(req->stretch, style.stretch);
    out->weightDist = FontSelectWeightDist(req->weight, style.weight);
    out->styleDist = FontSelectStyleDist(
        req->style, style.style, req->hasObliqueAngle, req->obliqueAngleMillidegrees,
        cls ? cls->hasItalicAngle : 0u,
        cls ? cls->italicAngleMillidegrees : 0);
    return RoboOk;
}

static int CacheKeyEq(const FallbackCacheEntry *e,
                        uint64_t gen,
                        const FontFallbackRequest *req,
                        uint32_t cpHash,
                        uint32_t langHash,
                        uint32_t polHash,
                        UnicodeScript script)
{
    uint32_t i;
    if (e->registryGeneration != gen || e->primaryId != GetFontFaceId(req->primary) ||
        e->cpHash != cpHash || e->codepointCount != req->codepointCount ||
        e->weight != req->weight || e->stretch != req->stretch || e->style != req->style ||
        e->langHash != langHash || e->script != script || e->hasScript != req->hasScript ||
        e->flags != (req->flags | 0u) || e->PolicyHash != polHash) {
        return 0;
    }
    for (i = 0u; i < req->codepointCount; i++) {
        if (e->cps[i] != req->codepoints[i]) {
            return 0;
        }
    }
    return 1;
}

static FallbackCacheEntry *CacheLookup(FontFallbackResolver *r,
                                            uint64_t gen,
                                            const FontFallbackRequest *req,
                                            uint32_t cpHash,
                                            uint32_t langHash,
                                            uint32_t polHash,
                                            UnicodeScript script)
{
    uint32_t i;
    if (!r->cache || r->cacheCount == 0u) {
        return NULL;
    }
    for (i = 0u; i < r->cacheCap; i++) {
        if (r->cache[i].codepointCount == 0u && r->cache[i].primaryId == 0u) {
            continue;
        }
        if (CacheKeyEq(&r->cache[i], gen, req, cpHash, langHash, polHash, script)) {
            r->cache[i].lruTick = ++r->lruTick;
            return &r->cache[i];
        }
    }
    return NULL;
}

static void CacheStore(FontFallbackResolver *r,
                        uint64_t gen,
                        const FontFallbackRequest *req,
                        uint32_t cpHash,
                        uint32_t langHash,
                        uint32_t polHash,
                        UnicodeScript script,
                        const FontFallbackResult *res)
{
    FallbackCacheEntry *slot = NULL;
    uint32_t i;
    uint64_t oldest = ~(uint64_t)0;
    uint32_t oldestI = 0u;

    if (!r->cache || r->cacheCap == 0u) {
        return;
    }
    for (i = 0u; i < r->cacheCap; i++) {
        if (r->cache[i].primaryId == 0u && r->cache[i].codepointCount == 0u) {
            slot = &r->cache[i];
            break;
        }
        if (r->cache[i].lruTick < oldest) {
            oldest = r->cache[i].lruTick;
            oldestI = i;
        }
    }
    if (!slot) {
        slot = &r->cache[oldestI];
    } else if (r->cacheCount < r->cacheCap) {
        r->cacheCount++;
    }

    memset(slot, 0, sizeof(*slot));
    slot->registryGeneration = gen;
    slot->primaryId = GetFontFaceId(req->primary);
    slot->cpHash = cpHash;
    slot->codepointCount = req->codepointCount;
    memcpy(slot->cps, req->codepoints, (size_t)req->codepointCount * sizeof(uint32_t));
    slot->weight = req->weight;
    slot->stretch = req->stretch;
    slot->style = req->style;
    slot->langHash = langHash;
    slot->script = script;
    slot->hasScript = req->hasScript;
    slot->flags = req->flags;
    slot->PolicyHash = polHash;
    slot->usedPrimary = res->usedPrimary;
    slot->found = res->found;
    slot->stage = res->stage;
    slot->faceId = res->faceId;
    slot->score = res->score;
    slot->primaryMissingCount = res->primaryMissingCount;
    slot->lruTick = ++r->lruTick;
    r->stats.inserts++;
}

static int IsLastResortCandidate(const FontFallbackPolicy *pol,
                                    const FontRegistration *reg,
                                    FontFace *face)
{
    if (!pol) {
        return 0;
    }
    if (pol->lastResortFaceId != 0u &&
        GetFontFaceId(face) == pol->lastResortFaceId) {
        return 1;
    }
    if (pol->lastResortFamily && pol->lastResortFamily[0] &&
        FamilyEq(RegFamily(reg), pol->lastResortFamily)) {
        return 1;
    }
    return 0;
}

static int IsPreferredCandidate(const FontFallbackRequest *req,
                                  const FontFallbackPolicy *pol,
                                  const FontRegistration *reg)
{
    return PreferredRank(RegFamily(reg), req->preferredFamilies,
                          req->preferredFamilyCount, pol->preferredFamilies,
                          pol->preferredFamilyCount) != 0xFFFFFFFFu;
}

Result FontFallbackResolve(FontFallbackResolver *r,
                                         const FontFallbackRequest *req,
                                         FontFallbackResult *out)
{
    const FontFallbackPolicy *pol;
    uint64_t gen;
    uint32_t cpHash;
    uint32_t langHash;
    uint32_t polHash;
    UnicodeScript wantScript;
    int coversAll = 0;
    int coversBase = 0;
    uint32_t missing = 0u;
    uint32_t flags;
    FallbackCacheEntry *hit;
    FontFallbackScore bestScore;
    FontFace *bestFace = NULL;
    FontFallbackStage bestStage = RoboFontFallbackStageNone;
    uint32_t i;
    uint32_t scanned = 0u;
    uint32_t maxScan;
    Result res;
    int useCache;

    if (!r || r->cookie != RESOLVER_COOKIE || !out) {
        return RoboInvalidArgument;
    }
    res = FontFallbackRequestValidate(req);
    if (res != RoboOk) {
        return res;
    }

    if (out->cookie == RoboFontFallbackResultCookie) {
        FontFallbackResultClear(out);
    } else {
        FontFallbackResultInit(out);
    }

    pol = req->policy ? req->policy : &r->policy;
    flags = pol->flags | req->flags;
    if ((flags & RoboFontFallbackFlagRequireAll) == 0u) {
        flags |= RoboFontFallbackFlagRequireAll;
    }
    useCache = (flags & RoboFontFallbackFlagNoCache) == 0 && r->cacheCap > 0u;
    maxScan = pol->maxScan ? pol->maxScan : RoboFontFallbackMaxScan;
    if (maxScan > RoboFontFallbackMaxScan) {
        maxScan = RoboFontFallbackMaxScan;
    }

    gen = FontRegistryGeneration(r->registry);
    cpHash = HashBytes(req->codepoints, (size_t)req->codepointCount * sizeof(uint32_t));
    langHash = HashCstr(req->language, req->languageLen);
    polHash = PolicyHash(pol);
    wantScript = req->hasScript ? req->script : DeriveScript(req->codepoints[0]);

    if (useCache) {
        hit = CacheLookup(r, gen, req, cpHash, langHash, polHash, wantScript);
        if (hit) {
            const FontRegistration *reg = NULL;
            r->stats.hits++;
            out->fromCache = 1;
            out->usedPrimary = hit->usedPrimary;
            out->found = hit->found;
            out->stage = hit->stage;
            out->faceId = hit->faceId;
            out->score = hit->score;
            out->primaryMissingCount = hit->primaryMissingCount;
            out->registryGeneration = gen;
            if (hit->found && hit->faceId != 0u) {
                if (FontRegistryFindByFaceId(r->registry, hit->faceId, &reg) ==
                        RoboOk &&
                    reg && reg->face) {
                    FontFaceRetain(reg->face);
                    out->face = reg->face;
                    return RoboOk;
                }
                /* Stale face — fall through to recompute. */
                FontFallbackResolverCacheClear(r);
            } else {
                return RoboOk;
            }
        } else {
            r->stats.misses++;
        }
    }

    /* Fast path: primary coverage. */
    res = FontFallbackFaceCovers(req->primary, req->codepoints,
                                         req->codepointCount, flags, &coversAll,
                                         &coversBase, &missing);
    if (res != RoboOk) {
        return res;
    }
    out->primaryMissingCount = missing;
    if (coversAll) {
        out->usedPrimary = 1;
        out->found = 1;
        out->stage = RoboFontFallbackStagePrimary;
        out->faceId = GetFontFaceId(req->primary);
        FontFaceRetain(req->primary);
        out->face = req->primary;
        out->score.eligible = 1;
        out->score.coversAll = 1;
        out->score.coversBase = 1;
        out->score.stage = RoboFontFallbackStagePrimary;
        out->score.stageRank = 0u;
        out->score.faceId = out->faceId;
        out->registryGeneration = gen;
        if (useCache) {
            CacheStore(r, gen, req, cpHash, langHash, polHash, wantScript, out);
        }
        return RoboOk;
    }

    memset(&bestScore, 0, sizeof(bestScore));
    bestScore.eligible = 0;

    {
        uint32_t n = FontRegistryRegistrationCount(r->registry);
        for (i = 0u; i < n && scanned < maxScan; i++) {
            const FontRegistration *reg = NULL;
            FontFace *face;
            FontFallbackScore score;
            FontFallbackStage stage;

            if (FontRegistryRegistrationAt(r->registry, i, &reg) != RoboOk ||
                !reg || !reg->face) {
                continue;
            }
            face = reg->face;
            if (GetFontFaceId(face) == GetFontFaceId(req->primary)) {
                continue;
            }
            scanned++;

            if (IsLastResortCandidate(pol, reg, face)) {
                stage = RoboFontFallbackStageLastResort;
            } else if (IsPreferredCandidate(req, pol, reg)) {
                stage = RoboFontFallbackStagePreferred;
            } else {
                /* Script vs general decided after coverage/script check. */
                stage = RoboFontFallbackStageGeneral;
            }

            res = ScoreCandidate(r, req, pol, face, reg, stage, wantScript, &score);
            if (res != RoboOk) {
                continue; /* skip broken candidates */
            }
            if (!score.eligible) {
                continue;
            }

            if (stage == RoboFontFallbackStageGeneral &&
                wantScript != RoboUScriptUnknown && score.scriptRank == 0u) {
                score.stage = RoboFontFallbackStageScript;
                score.stageRank = (uint32_t)RoboFontFallbackStageScript;
            }

            if (!bestScore.eligible ||
                FontFallbackScoreCompare(&score, &bestScore) < 0) {
                bestScore = score;
                bestFace = face;
                bestStage = score.stage;
            }
        }
    }

    if (bestFace && bestScore.eligible) {
        out->found = 1;
        out->usedPrimary = 0;
        out->stage = bestStage;
        out->faceId = GetFontFaceId(bestFace);
        out->score = bestScore;
        FontFaceRetain(bestFace);
        out->face = bestFace;
    } else {
        out->found = 0;
        out->usedPrimary = 0;
        out->stage = RoboFontFallbackStageNone;
    }
    out->registryGeneration = gen;
    if (useCache) {
        CacheStore(r, gen, req, cpHash, langHash, polHash, wantScript, out);
    }
    return RoboOk;
}

Result FontFallbackResolveCodepoint(
    FontFallbackResolver *r,
    FontFace *primary,
    uint32_t codePoint,
    const FontFallbackRequest *styleHints,
    FontFallbackResult *out)
{
    FontFallbackRequest req;
    uint32_t cp = codePoint;

    FontFallbackRequestInit(&req);
    if (styleHints) {
        req = *styleHints;
    }
    req.primary = primary;
    req.codepoints = &cp;
    req.codepointCount = 1u;
    return FontFallbackResolve(r, &req, out);
}

Result FontFallbackFuzzParse(const uint8_t *data, size_t size)
{
    FontRegistry *reg = NULL;
    FontFallbackResolver *resolver = NULL;
    FontFallbackPolicy pol;
    FontFallbackRequest req;
    FontFallbackResult res;
    uint32_t cps[4];
    Result rr;

    if (!data && size > 0u) {
        return RoboInvalidArgument;
    }
    rr = FontRegistryCreate(&reg);
    if (rr != RoboOk) {
        return rr == RoboOutOfMemory ? rr : RoboOk;
    }
    FontFallbackPolicyInit(&pol);
    pol.cacheCapacity = 8u;
    pol.maxScan = 16u;
    if (size > 0u && data) {
        pol.flags = data[0] & 0x3u;
    }
    rr = FontFallbackResolverCreate(reg, &pol, &resolver);
    if (rr != RoboOk) {
        FontRegistryDestroy(reg);
        return rr == RoboOutOfMemory ? rr : RoboOk;
    }
    FontFallbackRequestInit(&req);
    cps[0] = size > 1u ? (uint32_t)data[1] : 0x41u;
    cps[1] = size > 2u ? (uint32_t)data[2] : 0x42u;
    if (cps[0] > 0x10FFFFu) {
        cps[0] = 0x41u;
    }
    if (cps[0] >= 0xD800u && cps[0] <= 0xDFFFu) {
        cps[0] = 0x41u;
    }
    req.codepoints = cps;
    req.codepointCount = 1u;
    /* No primary → validate fails safely. */
    (void)FontFallbackResolve(resolver, &req, &res);
    FontFallbackResultClear(&res);
    FontFallbackResolverCacheClear(resolver);
    FontFallbackResolverDestroy(resolver);
    FontRegistryDestroy(reg);
    return RoboOk;
}
