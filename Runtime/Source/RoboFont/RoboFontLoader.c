/*
 * RoboFont loader + bounded face cache (Goal 5.13).
 */

#include "RoboFont/RoboFontLoader.h"

#include "RoboFont/RoboFontDiscovery.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboFont/RoboFontValidate.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define LOADER_COOKIE 0x52464C44u /* 'RFLD' */
#define FACE_OVERHEAD_BYTES 4096u

typedef struct cacheEntry {
    FontCacheKey key;
    FontFace *face; /* retained by cache while present */
    FontLoadState state;
    Result failError;
    uint8_t failRetryable;
    uint8_t pinned;
    uint8_t inFlight;
    uint64_t approxBytes;
    uint64_t lruTick;
} CacheEntry;

struct FontCache {
    CacheEntry *entries;
    uint32_t count;
    uint32_t cap;
    uint32_t maxFaces;
    uint64_t maxBytes;
    uint64_t approxBytes;
    uint64_t lruClock;
    FontCacheStats stats;
};

struct FontLoader {
    uint32_t cookie;
    FontRegistry *registry; /* non-owning */
    FontCache cache;
    FontLoadPolicy defaultLoadPolicy;
    FontValidatePolicy defaultValidatePolicy;
};

void FontLoadRequestInit(FontLoadRequest *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
    req->loadPolicy = RoboFontLoadPolicyStandard;
    req->cachePolicy = RoboFontCachePolicyDefault;
    req->validatePolicy = RoboFontValidateNormal;
}

void FontLoadResultInit(FontLoadResult *res)
{
    if (!res) {
        return;
    }
    memset(res, 0, sizeof(*res));
    res->state = RoboFontLoadUnloaded;
    res->error = RoboOk;
}

void FontLoadResultClear(FontLoadResult *res)
{
    if (!res) {
        return;
    }
    if (res->face) {
        FontFaceRelease(res->face);
        res->face = NULL;
    }
    res->state = RoboFontLoadUnloaded;
    res->fromCache = 0u;
    res->retryable = 0u;
    res->error = RoboOk;
}

void FontLoaderConfigInit(FontLoaderConfig *cfg)
{
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->maxFaces = RoboFontCacheDefaultMaxFaces;
    cfg->maxBytes = RoboFontCacheDefaultMaxBytes;
    cfg->defaultLoadPolicy = RoboFontLoadPolicyStandard;
    cfg->defaultValidatePolicy = RoboFontValidateNormal;
}

uint64_t FontLoaderApproxFaceBytes(const FontFace *face)
{
    const FontSource *src;
    const FontCoverage *cov;
    uint64_t n;
    if (!face) {
        return 0u;
    }
    src = FontFaceSource(face);
    n = (uint64_t)FontSourceSize(src) + (uint64_t)FACE_OVERHEAD_BYTES;
    cov = FontFaceCoverage(face);
    if (cov) {
        n += FontCoverageApproxBytes(cov);
    }
    return n;
}

static CacheEntry *CacheFind(FontCache *cache,
                                 FontRegistrationId id)
{
    uint32_t i;
    if (!cache) {
        return NULL;
    }
    for (i = 0u; i < cache->count; i++) {
        if (cache->entries[i].key.registrationId == id) {
            return &cache->entries[i];
        }
    }
    return NULL;
}

static const CacheEntry *CacheFindConst(const FontCache *cache,
                                             FontRegistrationId id)
{
    uint32_t i;
    if (!cache) {
        return NULL;
    }
    for (i = 0u; i < cache->count; i++) {
        if (cache->entries[i].key.registrationId == id) {
            return &cache->entries[i];
        }
    }
    return NULL;
}

static void CacheRemoveAt(FontCache *cache, uint32_t idx)
{
    CacheEntry *e;
    if (!cache || idx >= cache->count) {
        return;
    }
    e = &cache->entries[idx];
    if (e->face) {
        if (cache->approxBytes >= e->approxBytes) {
            cache->approxBytes -= e->approxBytes;
        } else {
            cache->approxBytes = 0u;
        }
        FontFaceRelease(e->face);
        e->face = NULL;
    }
    if (idx + 1u < cache->count) {
        memmove(&cache->entries[idx], &cache->entries[idx + 1u],
                (size_t)(cache->count - idx - 1u) * sizeof(cache->entries[0]));
    }
    cache->count--;
    cache->stats.entries = cache->count;
    cache->stats.approxBytes = cache->approxBytes;
    cache->stats.evictions++;
}

static Result CacheEnsureCap(FontCache *cache, uint32_t need)
{
    CacheEntry *n;
    uint32_t ncap;
    if (need <= cache->cap) {
        return RoboOk;
    }
    ncap = cache->cap ? cache->cap * 2u : 8u;
    while (ncap < need) {
        ncap *= 2u;
    }
    if (ncap > RoboFontCacheMaxFacesHard) {
        ncap = RoboFontCacheMaxFacesHard;
    }
    if (need > ncap) {
        return RoboOutOfMemory;
    }
    n = (CacheEntry *)RoboCalloc(ncap, sizeof(*n));
    if (!n) {
        return RoboOutOfMemory;
    }
    if (cache->entries && cache->count > 0u) {
        memcpy(n, cache->entries, (size_t)cache->count * sizeof(*n));
        RoboFree(cache->entries);
    }
    cache->entries = n;
    cache->cap = ncap;
    return RoboOk;
}

Result FontCacheEvictToLimits(FontCache *cache)
{
    if (!cache) {
        return RoboInvalidArgument;
    }
    while (cache->count > cache->maxFaces ||
           (cache->maxBytes > 0u && cache->approxBytes > cache->maxBytes)) {
        uint32_t i;
        uint32_t victim = 0xFFFFFFFFu;
        uint64_t oldest = 0xFFFFFFFFFFFFFFFFull;
        for (i = 0u; i < cache->count; i++) {
            CacheEntry *e = &cache->entries[i];
            if (e->inFlight || e->pinned) {
                continue;
            }
            if (e->lruTick < oldest) {
                oldest = e->lruTick;
                victim = i;
            }
        }
        if (victim == 0xFFFFFFFFu) {
            return RoboOutOfMemory;
        }
        CacheRemoveAt(cache, victim);
    }
    return RoboOk;
}

static void CacheTouch(FontCache *cache, CacheEntry *e)
{
    cache->lruClock++;
    e->lruTick = cache->lruClock;
}

FontCache *FontLoaderCache(FontLoader *loader)
{
    return loader ? &loader->cache : NULL;
}

const FontCacheStats *GetFontCacheStats(const FontCache *cache)
{
    return cache ? &cache->stats : NULL;
}

uint32_t FontCacheEntryCount(const FontCache *cache)
{
    return cache ? cache->count : 0u;
}

Result FontCacheInvalidateRegistration(FontCache *cache,
                                                      FontRegistrationId id)
{
    uint32_t i;
    if (!cache) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < cache->count; i++) {
        if (cache->entries[i].key.registrationId == id) {
            cache->stats.invalidations++;
            CacheRemoveAt(cache, i);
            return RoboOk;
        }
    }
    return RoboNotFound;
}

Result FontCacheInvalidateSource(FontCache *cache,
                                                FontDiscoverySourceId sourceId)
{
    uint32_t i = 0u;
    uint32_t n = 0u;
    if (!cache) {
        return RoboInvalidArgument;
    }
    while (i < cache->count) {
        if (cache->entries[i].key.sourceId == sourceId) {
            cache->stats.invalidations++;
            CacheRemoveAt(cache, i);
            n++;
        } else {
            i++;
        }
    }
    return n > 0u ? RoboOk : RoboNotFound;
}

void FontCacheClear(FontCache *cache)
{
    if (!cache) {
        return;
    }
    while (cache->count > 0u) {
        cache->stats.invalidations++;
        CacheRemoveAt(cache, 0u);
    }
    cache->approxBytes = 0u;
    cache->stats.approxBytes = 0u;
    cache->stats.entries = 0u;
}

static void LoaderInvalidateHook(void *userdata,
                                   FontDiscoverySourceId sourceId,
                                   FontRegistrationId registrationId)
{
    FontLoader *loader = (FontLoader *)userdata;
    if (!loader || loader->cookie != LOADER_COOKIE) {
        return;
    }
    if (sourceId == 0u && registrationId == 0u) {
        FontCacheClear(&loader->cache);
        return;
    }
    if (registrationId != 0u) {
        (void)FontCacheInvalidateRegistration(&loader->cache, registrationId);
        return;
    }
    (void)FontCacheInvalidateSource(&loader->cache, sourceId);
}

Result FontLoaderCreate(FontRegistry *registry,
                                      const FontLoaderConfig *config,
                                      FontLoader **out)
{
    FontLoader *loader;
    FontLoaderConfig cfg;

    if (!registry || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    FontLoaderConfigInit(&cfg);
    if (config) {
        cfg = *config;
    }
    if (cfg.maxFaces == 0u) {
        cfg.maxFaces = RoboFontCacheDefaultMaxFaces;
    }
    if (cfg.maxFaces > RoboFontCacheMaxFacesHard) {
        cfg.maxFaces = RoboFontCacheMaxFacesHard;
    }
    if (cfg.maxBytes == 0u) {
        cfg.maxBytes = RoboFontCacheDefaultMaxBytes;
    }

    loader = (FontLoader *)RoboCalloc(1, sizeof(*loader));
    if (!loader) {
        return RoboOutOfMemory;
    }
    loader->cookie = LOADER_COOKIE;
    loader->registry = registry;
    loader->defaultLoadPolicy = cfg.defaultLoadPolicy;
    loader->defaultValidatePolicy = cfg.defaultValidatePolicy;
    loader->cache.maxFaces = cfg.maxFaces;
    loader->cache.maxBytes = cfg.maxBytes;
    loader->cache.stats.maxFaces = cfg.maxFaces;
    loader->cache.stats.maxBytes = cfg.maxBytes;

    FontRegistrySetInvalidateHook(registry, LoaderInvalidateHook, loader);
    *out = loader;
    return RoboOk;
}

void FontLoaderDestroy(FontLoader *loader)
{
    if (!loader) {
        return;
    }
    if (loader->cookie != LOADER_COOKIE) {
        return;
    }
    if (loader->registry) {
        FontRegistrySetInvalidateHook(loader->registry, NULL, NULL);
    }
    FontCacheClear(&loader->cache);
    RoboFree(loader->cache.entries);
    loader->cache.entries = NULL;
    loader->cookie = 0u;
    RoboFree(loader);
}

FontLoadState FontLoaderState(const FontLoader *loader,
                                              FontRegistrationId id)
{
    const CacheEntry *e;
    if (!loader || loader->cookie != LOADER_COOKIE) {
        return RoboFontLoadUnloaded;
    }
    e = CacheFindConst(&loader->cache, id);
    if (!e) {
        return RoboFontLoadUnloaded;
    }
    return e->state;
}

static int FailureIsRetryable(Result err)
{
    return err == RoboIoError || err == RoboPermissionDenied ||
           err == RoboOutOfMemory || err == RoboNotFound ||
           err == RoboBusy;
}

static Result EnsureFaceLoaded(FontFace *face,
                                        FontLoadPolicy policy,
                                        FontValidatePolicy validate)
{
    Result res;
    uint32_t ttFlags = RoboFontTtFlagsNone;
    FontValidationResult *vresult = NULL;

    if (!face) {
        return RoboInvalidArgument;
    }
    if (FontFaceFormat(face) != RoboFontFormatSfnt) {
        return RoboNotSupported;
    }

    /* Staged security validation before treating the face as trusted. */
    res = FontValidateForLoad(face, validate, &vresult);
    if (res != RoboOk) {
        if (vresult) {
            FontValidationResultRelease(vresult);
        }
        return res;
    }
    if (vresult) {
        FontValidationResultRelease(vresult);
        vresult = NULL;
    }

    if (validate == RoboFontValidateStrict ||
        validate == RoboFontValidateParanoid) {
        ttFlags |= RoboFontTtFlagsVerifyHeadChecksum;
    }

    res = FontFaceParseTruetype(face, ttFlags);
    if (res != RoboOk) {
        return res;
    }
    res = FontFaceParseMetadata(face);
    if (res != RoboOk) {
        return res;
    }
    res = FontFaceParseClassification(face);
    if (res != RoboOk) {
        return res;
    }

    if (policy >= RoboFontLoadPolicyStandard) {
        res = FontFaceParseCmap(face);
        if (res != RoboOk && res != RoboNotFound && res != RoboNotSupported) {
            return res;
        }
        /* Missing cmap is allowed for symbol fonts; continue. */
        res = FontFaceParseMetrics(face);
        if (res != RoboOk) {
            return res;
        }
    }
    if (policy >= RoboFontLoadPolicyReady) {
        res = FontFaceParseOutlines(face);
        if (res != RoboOk && res != RoboNotSupported) {
            return res;
        }
    }
    return RoboOk;
}

static Result PublishSuccess(FontLoadResult *out,
                                     CacheEntry *e,
                                     int fromCache)
{
    FontFaceRetain(e->face);
    out->face = e->face;
    out->state = RoboFontLoadLoaded;
    out->fromCache = fromCache ? 1u : 0u;
    out->retryable = 0u;
    out->error = RoboOk;
    out->key = e->key;
    return RoboOk;
}

Result FontLoaderLoad(FontLoader *loader,
                                    const FontLoadRequest *request,
                                    FontLoadResult *outResult)
{
    const FontRegistration *rec = NULL;
    CacheEntry *entry;
    FontLoadPolicy policy;
    FontValidatePolicy validate;
    FontCachePolicy cachePol;
    Result res;
    int bypass;

    if (!loader || loader->cookie != LOADER_COOKIE || !request || !outResult) {
        return RoboInvalidArgument;
    }
    FontLoadResultInit(outResult);

    if (request->registrationId == 0u) {
        outResult->state = RoboFontLoadFailed;
        outResult->error = RoboInvalidArgument;
        return RoboInvalidArgument;
    }

    policy = request->loadPolicy;
    validate = request->validatePolicy;
    cachePol = request->cachePolicy;
    if (policy == (FontLoadPolicy)0 &&
        loader->defaultLoadPolicy != RoboFontLoadPolicyMinimal) {
        /* keep explicit MINIMAL; otherwise allow default when unset via convenience */
    }
    (void)loader->defaultLoadPolicy;
    (void)loader->defaultValidatePolicy;

    res = FontRegistryFindById(loader->registry, request->registrationId, &rec);
    if (res != RoboOk || !rec || !rec->face) {
        outResult->state = RoboFontLoadFailed;
        outResult->error = RoboNotFound;
        outResult->retryable = 0u;
        return RoboNotFound;
    }
    if (request->sourceId != 0u && rec->sourceId != request->sourceId) {
        outResult->state = RoboFontLoadFailed;
        outResult->error = RoboNotFound;
        return RoboNotFound;
    }

    bypass = (cachePol == RoboFontCachePolicyBypass);

    entry = CacheFind(&loader->cache, request->registrationId);
    if (entry) {
        if (entry->inFlight) {
            /* Single-flight: refuse concurrent publish of partial face. */
            outResult->state = RoboFontLoadLoading;
            outResult->error = RoboBusy;
            outResult->retryable = 1u;
            return RoboBusy;
        }
        if (entry->state == RoboFontLoadLoaded && entry->face) {
            CacheTouch(&loader->cache, entry);
            if (cachePol == RoboFontCachePolicyPin) {
                entry->pinned = 1u;
            }
            loader->cache.stats.hits++;
            return PublishSuccess(outResult, entry, 1);
        }
        if (entry->state == RoboFontLoadFailed && !entry->failRetryable) {
            loader->cache.stats.hits++;
            outResult->state = RoboFontLoadFailed;
            outResult->error = entry->failError;
            outResult->retryable = 0u;
            outResult->key = entry->key;
            outResult->fromCache = 1u;
            return entry->failError;
        }
        /* Retryable failure or evicted — drop and reload. */
        {
            uint32_t idx = (uint32_t)(entry - loader->cache.entries);
            CacheRemoveAt(&loader->cache, idx);
            entry = NULL;
        }
    }

    loader->cache.stats.misses++;

    if (!bypass) {
        res = CacheEnsureCap(&loader->cache, loader->cache.count + 1u);
        if (res != RoboOk) {
            outResult->state = RoboFontLoadFailed;
            outResult->error = res;
            outResult->retryable = 1u;
            return res;
        }
        entry = &loader->cache.entries[loader->cache.count];
        memset(entry, 0, sizeof(*entry));
        entry->key.registrationId = rec->id;
        entry->key.sourceId = rec->sourceId;
        entry->key.faceIndex = rec->faceIndex;
        entry->state = RoboFontLoadLoading;
        entry->inFlight = 1u;
        loader->cache.count++;
        loader->cache.stats.entries = loader->cache.count;
    } else {
        entry = NULL;
    }

    res = EnsureFaceLoaded(rec->face, policy, validate);
    if (res != RoboOk) {
        int retry = FailureIsRetryable(res);
        if (entry) {
            entry->inFlight = 0u;
            if (retry) {
                uint32_t idx = (uint32_t)(entry - loader->cache.entries);
                CacheRemoveAt(&loader->cache, idx);
            } else {
                entry->state = RoboFontLoadFailed;
                entry->failError = res;
                entry->failRetryable = 0u;
                entry->face = NULL;
                CacheTouch(&loader->cache, entry);
                loader->cache.stats.inserts++;
            }
        }
        outResult->state = RoboFontLoadFailed;
        outResult->error = res;
        outResult->retryable = retry ? 1u : 0u;
        return res;
    }

    if (bypass) {
        FontFaceRetain(rec->face);
        outResult->face = rec->face;
        outResult->state = RoboFontLoadLoaded;
        outResult->fromCache = 0u;
        outResult->error = RoboOk;
        outResult->key.registrationId = rec->id;
        outResult->key.sourceId = rec->sourceId;
        outResult->key.faceIndex = rec->faceIndex;
        return RoboOk;
    }

    FontFaceRetain(rec->face);
    entry->face = rec->face;
    entry->approxBytes = FontLoaderApproxFaceBytes(rec->face);
    entry->state = RoboFontLoadLoaded;
    entry->inFlight = 0u;
    if (cachePol == RoboFontCachePolicyPin) {
        entry->pinned = 1u;
    }
    CacheTouch(&loader->cache, entry);
    loader->cache.approxBytes += entry->approxBytes;
    loader->cache.stats.approxBytes = loader->cache.approxBytes;
    loader->cache.stats.inserts++;
    (void)FontCacheEvictToLimits(&loader->cache);

    /* Eviction may have removed other entries; re-find ours. */
    entry = CacheFind(&loader->cache, request->registrationId);
    if (!entry || !entry->face) {
        /* Our new entry was somehow evicted (pinned should prevent). Re-publish. */
        FontFaceRetain(rec->face);
        outResult->face = rec->face;
        outResult->state = RoboFontLoadLoaded;
        outResult->fromCache = 0u;
        outResult->error = RoboOk;
        outResult->key.registrationId = rec->id;
        outResult->key.sourceId = rec->sourceId;
        outResult->key.faceIndex = rec->faceIndex;
        return RoboOk;
    }
    return PublishSuccess(outResult, entry, 0);
}

Result FontLoaderLoadRegistration(FontLoader *loader,
                                                 FontRegistrationId id,
                                                 FontLoadResult *out)
{
    FontLoadRequest req;
    FontLoadRequestInit(&req);
    req.registrationId = id;
    if (loader) {
        req.loadPolicy = loader->defaultLoadPolicy;
        req.validatePolicy = loader->defaultValidatePolicy;
    }
    return FontLoaderLoad(loader, &req, out);
}

Result FontLoaderFuzzParse(const uint8_t *data, size_t size)
{
    FontRegistry *reg = NULL;
    FontLoader *loader = NULL;
    FontLoaderConfig cfg;
    FontDiscoverySourceId sid = 0u;
    FontDiscoveryResult scan;
    FontLoadResult load;
    const FontRegistration *rec = NULL;
    size_t n = size;

    if (n > 65536u) {
        n = 65536u;
    }
    if (FontRegistryCreate(&reg) != RoboOk) {
        return RoboOk;
    }
    FontLoaderConfigInit(&cfg);
    cfg.maxFaces = 4u;
    cfg.maxBytes = 256u * 1024u;
    if (FontLoaderCreate(reg, &cfg, &loader) != RoboOk) {
        FontRegistryDestroy(reg);
        return RoboOk;
    }
    if (data && n > 0u) {
        (void)FontRegistryAddMemorySource(reg, "fuzz", data, n, &sid);
        (void)FontRegistryScanSource(reg, sid, 0u, &scan);
        if (FontRegistryRegistrationAt(reg, 0u, &rec) == RoboOk && rec) {
            (void)FontLoaderLoadRegistration(loader, rec->id, &load);
            FontLoadResultClear(&load);
        }
        (void)FontCacheEvictToLimits(FontLoaderCache(loader));
        FontCacheClear(FontLoaderCache(loader));
    }
    FontLoaderDestroy(loader);
    FontRegistryDestroy(reg);
    return RoboOk;
}
