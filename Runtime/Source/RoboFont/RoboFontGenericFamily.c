/*
 * RoboFontGenericFamily — semantic generic font families (Goal 5.16).
 */

#include "RoboFont/RoboFontGenericFamily.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeCase.h"

#include <string.h>

#define CONFIG_COOKIE 0x47434647u   /* 'GCFG' */
#define RESOLVER_COOKIE 0x47524553u /* 'GRES' */

typedef struct {
    FontGenericFamily generic;
    char *language; /* NULL = default mapping */
    char **families;
    uint32_t familyCount;
} Mapping;

struct FontGenericConfig {
    uint32_t cookie;
    uint32_t refs;
    uint64_t generation;
    Mapping *entries;
    uint32_t entryCount;
    uint32_t entryCap;
};

typedef struct {
    uint64_t resolverGeneration;
    uint64_t registryGeneration;
    uint64_t configGeneration;
    FontGenericFamily generic;
    uint32_t langHash;
    uint32_t flags;
    uint8_t resolved;
    uint8_t usedRelated;
    FontGenericFamily effective;
    FontGenericLayer layer;
    uint32_t familyCount;
    char familiesBlob[RoboFontGenericMaxFamilyBytes];
    uint16_t familyOffs[RoboFontGenericMaxFamiliesPerMapping];
    uint64_t lruTick;
} GenericCacheEntry;

struct FontGenericResolver {
    uint32_t cookie;
    FontRegistry *registry;
    FontGenericConfig *platform;
    FontGenericConfig *runtime;
    FontGenericConfig *application;
    uint32_t flags;
    uint64_t generation;
    GenericCacheEntry *cache;
    uint32_t cacheCap;
    uint32_t cacheCount;
    uint64_t lruTick;
    FontGenericCacheStats stats;
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
    int cmp = 0;
    if (!a || !b) {
        return a == b;
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

static char *DupN(const char *s, size_t n)
{
    char *o;
    if (!s) {
        return NULL;
    }
    if (n == 0u) {
        n = strlen(s);
    }
    o = (char *)Malloc(n + 1u);
    if (!o) {
        return NULL;
    }
    memcpy(o, s, n);
    o[n] = '\0';
    return o;
}

static void FreeMapping(Mapping *m)
{
    uint32_t i;
    if (!m) {
        return;
    }
    if (m->families) {
        for (i = 0u; i < m->familyCount; i++) {
            RoboFree(m->families[i]);
        }
        RoboFree(m->families);
        m->families = NULL;
    }
    RoboFree(m->language);
    m->language = NULL;
    m->familyCount = 0u;
}

static void BumpConfigGen(FontGenericConfig *cfg)
{
    cfg->generation++;
    if (cfg->generation == 0u) {
        cfg->generation = 1u;
    }
}

/* ---- name table ---- */

static const char *const kNames[RoboFontGenericCount] = {
    "none",           "serif",          "sans-serif",    "monospace",
    "cursive",        "fantasy",        "system-ui",     "emoji",
    "math",           "ui-serif",       "ui-sans-serif", "ui-monospace",
    "ui-rounded"};

const char *FontGenericFamilyName(FontGenericFamily g)
{
    if ((unsigned)g >= (unsigned)RoboFontGenericCount) {
        return "none";
    }
    return kNames[g];
}

int FontGenericFamilyIsValid(FontGenericFamily g)
{
    return g > RoboFontGenericNone && g < RoboFontGenericCount;
}

Result FontGenericFamilyFromName(const char *name,
                                                 size_t nameLen,
                                                 FontGenericFamily *out)
{
    unsigned i;
    if (!out) {
        return RoboInvalidArgument;
    }
    *out = RoboFontGenericUnknown;
    if (!name) {
        return RoboInvalidArgument;
    }
    if (nameLen == 0u) {
        nameLen = strlen(name);
    }
    for (i = 1u; i < (unsigned)RoboFontGenericCount; i++) {
        if (Utf8CasefoldEq(name, nameLen, kNames[i], 0u)) {
            *out = (FontGenericFamily)i;
            return RoboOk;
        }
    }
    return RoboNotFound;
}

FontGenericFamily FontGenericRelated(FontGenericFamily g)
{
    switch (g) {
    case RoboFontGenericUiSerif:
        return RoboFontGenericSerif;
    case RoboFontGenericUiSansSerif:
    case RoboFontGenericUiRounded:
        return RoboFontGenericSansSerif;
    case RoboFontGenericUiMonospace:
        return RoboFontGenericMonospace;
    default:
        return RoboFontGenericUnknown;
    }
}

/* ---- config ---- */

Result FontGenericConfigCreate(FontGenericConfig **out)
{
    FontGenericConfig *c;
    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    c = (FontGenericConfig *)RoboCalloc(1, sizeof(*c));
    if (!c) {
        return RoboOutOfMemory;
    }
    c->cookie = CONFIG_COOKIE;
    c->refs = 1u;
    c->generation = 1u;
    *out = c;
    return RoboOk;
}

Result FontGenericConfigCreateEmptyPlatform(
    FontGenericConfig **out)
{
    return FontGenericConfigCreate(out);
}

void FontGenericConfigRetain(FontGenericConfig *cfg)
{
    if (!cfg || cfg->cookie != CONFIG_COOKIE) {
        return;
    }
    cfg->refs++;
}

void FontGenericConfigRelease(FontGenericConfig *cfg)
{
    uint32_t i;
    if (!cfg || cfg->cookie != CONFIG_COOKIE) {
        return;
    }
    if (cfg->refs == 0u) {
        return;
    }
    cfg->refs--;
    if (cfg->refs != 0u) {
        return;
    }
    for (i = 0u; i < cfg->entryCount; i++) {
        FreeMapping(&cfg->entries[i]);
    }
    RoboFree(cfg->entries);
    cfg->cookie = 0u;
    RoboFree(cfg);
}

uint64_t FontGenericConfigGeneration(const FontGenericConfig *cfg)
{
    return cfg && cfg->cookie == CONFIG_COOKIE ? cfg->generation : 0u;
}

uint32_t FontGenericConfigMappingCount(const FontGenericConfig *cfg)
{
    return cfg && cfg->cookie == CONFIG_COOKIE ? cfg->entryCount : 0u;
}

static int FindEntry(const FontGenericConfig *cfg,
                      FontGenericFamily g,
                      const char *lang,
                      size_t langLen,
                      uint32_t *outIndex)
{
    uint32_t i;
    int wantLang = lang && (langLen > 0u || lang[0] != '\0');
    if (wantLang && langLen == 0u) {
        langLen = strlen(lang);
    }
    for (i = 0u; i < cfg->entryCount; i++) {
        const Mapping *m = &cfg->entries[i];
        if (m->generic != g) {
            continue;
        }
        if (!wantLang) {
            if (!m->language) {
                if (outIndex) {
                    *outIndex = i;
                }
                return 1;
            }
        } else if (m->language &&
                   Utf8CasefoldEq(m->language, 0u, lang, langLen)) {
            if (outIndex) {
                *outIndex = i;
            }
            return 1;
        }
    }
    return 0;
}

static Result EnsureEntryCap(FontGenericConfig *cfg, uint32_t need)
{
    Mapping *n;
    uint32_t cap;
    if (need <= cfg->entryCap) {
        return RoboOk;
    }
    if (need > RoboFontGenericMaxMappings) {
        return RoboInvalidArgument;
    }
    cap = cfg->entryCap ? cfg->entryCap * 2u : 8u;
    if (cap < need) {
        cap = need;
    }
    if (cap > RoboFontGenericMaxMappings) {
        cap = RoboFontGenericMaxMappings;
    }
    n = (Mapping *)Realloc(cfg->entries, (size_t)cap * sizeof(Mapping));
    if (!n) {
        return RoboOutOfMemory;
    }
    cfg->entries = n;
    cfg->entryCap = cap;
    return RoboOk;
}

static Result SetMappingInner(FontGenericConfig *cfg,
                                       FontGenericFamily generic,
                                       const char *language,
                                       size_t languageLen,
                                       const char *const *families,
                                       uint32_t familyCount)
{
    uint32_t idx = 0u;
    Mapping *m;
    char **owned = NULL;
    uint32_t i;
    Result res;

    if (!cfg || cfg->cookie != CONFIG_COOKIE) {
        return RoboInvalidArgument;
    }
    if (!FontGenericFamilyIsValid(generic)) {
        return RoboInvalidArgument;
    }
    if (familyCount > RoboFontGenericMaxFamiliesPerMapping) {
        return RoboInvalidArgument;
    }
    if (familyCount != 0u && !families) {
        return RoboInvalidArgument;
    }

    if (familyCount > 0u) {
        owned = (char **)RoboCalloc(familyCount, sizeof(char *));
        if (!owned) {
            return RoboOutOfMemory;
        }
        for (i = 0u; i < familyCount; i++) {
            size_t n;
            if (!families[i]) {
                res = RoboInvalidArgument;
                goto failOwned;
            }
            n = strlen(families[i]);
            if (n == 0u || n >= RoboFontGenericMaxFamilyBytes) {
                res = RoboInvalidArgument;
                goto failOwned;
            }
            owned[i] = DupN(families[i], n);
            if (!owned[i]) {
                res = RoboOutOfMemory;
                goto failOwned;
            }
        }
    }

    if (FindEntry(cfg, generic, language, languageLen, &idx)) {
        m = &cfg->entries[idx];
        FreeMapping(m);
        m->generic = generic;
        if (language && (languageLen > 0u || language[0])) {
            if (languageLen == 0u) {
                languageLen = strlen(language);
            }
            m->language = DupN(language, languageLen);
            if (!m->language) {
                res = RoboOutOfMemory;
                goto failOwned;
            }
        }
        m->families = owned;
        m->familyCount = familyCount;
        BumpConfigGen(cfg);
        return RoboOk;
    }

    if (familyCount == 0u && !(language && language[0])) {
        /* Clearing a missing default mapping is a no-op success. */
        if (owned) {
            RoboFree(owned);
        }
        BumpConfigGen(cfg);
        return RoboOk;
    }

    res = EnsureEntryCap(cfg, cfg->entryCount + 1u);
    if (res != RoboOk) {
        goto failOwned;
    }
    m = &cfg->entries[cfg->entryCount];
    memset(m, 0, sizeof(*m));
    m->generic = generic;
    if (language && (languageLen > 0u || language[0])) {
        if (languageLen == 0u) {
            languageLen = strlen(language);
        }
        m->language = DupN(language, languageLen);
        if (!m->language) {
            res = RoboOutOfMemory;
            goto failOwned;
        }
    }
    m->families = owned;
    m->familyCount = familyCount;
    cfg->entryCount++;
    BumpConfigGen(cfg);
    return RoboOk;

failOwned:
    if (owned) {
        for (i = 0u; i < familyCount; i++) {
            RoboFree(owned[i]);
        }
        RoboFree(owned);
    }
    return res;
}

Result FontGenericConfigSetMapping(
    FontGenericConfig *cfg,
    FontGenericFamily generic,
    const char *const *families,
    uint32_t familyCount)
{
    return SetMappingInner(cfg, generic, NULL, 0u, families, familyCount);
}

Result FontGenericConfigSetMappingForLanguage(
    FontGenericConfig *cfg,
    FontGenericFamily generic,
    const char *language,
    size_t languageLen,
    const char *const *families,
    uint32_t familyCount)
{
    if (!language || (languageLen == 0u && language[0] == '\0')) {
        return RoboInvalidArgument;
    }
    return SetMappingInner(cfg, generic, language, languageLen, families,
                             familyCount);
}

Result FontGenericConfigClear(FontGenericConfig *cfg)
{
    uint32_t i;
    if (!cfg || cfg->cookie != CONFIG_COOKIE) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < cfg->entryCount; i++) {
        FreeMapping(&cfg->entries[i]);
    }
    cfg->entryCount = 0u;
    BumpConfigGen(cfg);
    return RoboOk;
}

Result FontGenericConfigGetMapping(
    const FontGenericConfig *cfg,
    FontGenericFamily generic,
    const char *language,
    size_t languageLen,
    const char *const **outFamilies,
    uint32_t *outCount)
{
    uint32_t idx = 0u;
    if (!cfg || cfg->cookie != CONFIG_COOKIE || !outFamilies || !outCount) {
        return RoboInvalidArgument;
    }
    *outFamilies = NULL;
    *outCount = 0u;
    if (!FontGenericFamilyIsValid(generic)) {
        return RoboInvalidArgument;
    }
    if (!FindEntry(cfg, generic, language, languageLen, &idx)) {
        return RoboNotFound;
    }
    *outFamilies = (const char *const *)cfg->entries[idx].families;
    *outCount = cfg->entries[idx].familyCount;
    return RoboOk;
}

/* ---- lookup across layers ---- */

static const Mapping *LookupInConfig(const FontGenericConfig *cfg,
                                         FontGenericFamily g,
                                         const char *lang,
                                         size_t langLen)
{
    uint32_t idx = 0u;
    if (!cfg) {
        return NULL;
    }
    if (lang && (langLen > 0u || lang[0])) {
        if (FindEntry(cfg, g, lang, langLen, &idx)) {
            return &cfg->entries[idx];
        }
    }
    if (FindEntry(cfg, g, NULL, 0u, &idx)) {
        return &cfg->entries[idx];
    }
    return NULL;
}

static void CombinedConfigGeneration(const FontGenericResolver *r,
                                       uint64_t *outGen)
{
    uint64_t g = r->generation;
    if (r->application) {
        g ^= FontGenericConfigGeneration(r->application) * 0x9E3779B97u;
    }
    if (r->runtime) {
        g ^= FontGenericConfigGeneration(r->runtime) * 0xC2B2AE3Du;
    }
    if (r->platform) {
        g ^= FontGenericConfigGeneration(r->platform) * 0x165667B1u;
    }
    *outGen = g;
}

static int PickMapping(const FontGenericResolver *r,
                        FontGenericFamily g,
                        const char *lang,
                        size_t langLen,
                        const Mapping **outMap,
                        FontGenericLayer *outLayer)
{
    const Mapping *m;
    m = LookupInConfig(r->application, g, lang, langLen);
    if (m && m->familyCount > 0u) {
        *outMap = m;
        *outLayer = RoboFontGenericLayerApplication;
        return 1;
    }
    m = LookupInConfig(r->runtime, g, lang, langLen);
    if (m && m->familyCount > 0u) {
        *outMap = m;
        *outLayer = RoboFontGenericLayerRuntime;
        return 1;
    }
    m = LookupInConfig(r->platform, g, lang, langLen);
    if (m && m->familyCount > 0u) {
        *outMap = m;
        *outLayer = RoboFontGenericLayerPlatform;
        return 1;
    }
    *outMap = NULL;
    *outLayer = RoboFontGenericLayerNone;
    return 0;
}

/* ---- result ---- */

void FontGenericResultInit(FontGenericResult *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->cookie = RoboFontGenericResultCookie;
    out->requested = RoboFontGenericUnknown;
    out->effective = RoboFontGenericUnknown;
    out->layer = RoboFontGenericLayerNone;
}

void FontGenericResultClear(FontGenericResult *res)
{
    uint32_t i;
    if (!res) {
        return;
    }
    if (res->cookie == RoboFontGenericResultCookie && res->families) {
        for (i = 0u; i < res->familyCount; i++) {
            RoboFree(res->families[i]);
        }
        RoboFree(res->families);
    }
    FontGenericResultInit(res);
}

static Result FillResultFromMapping(FontGenericResult *out,
                                              const Mapping *m,
                                              FontGenericFamily requested,
                                              FontGenericFamily effective,
                                              FontGenericLayer layer,
                                              uint8_t usedRelated,
                                              uint64_t cfgGen,
                                              uint64_t regGen)
{
    uint32_t i;
    char **arr;
    if (!m || m->familyCount == 0u) {
        out->resolved = 0;
        out->requested = requested;
        out->effective = effective;
        out->layer = RoboFontGenericLayerNone;
        out->usedRelated = usedRelated;
        out->configGeneration = cfgGen;
        out->registryGeneration = regGen;
        return RoboOk;
    }
    arr = (char **)RoboCalloc(m->familyCount, sizeof(char *));
    if (!arr) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < m->familyCount; i++) {
        arr[i] = DupN(m->families[i], 0u);
        if (!arr[i]) {
            uint32_t j;
            for (j = 0u; j < i; j++) {
                RoboFree(arr[j]);
            }
            RoboFree(arr);
            return RoboOutOfMemory;
        }
    }
    out->resolved = 1;
    out->requested = requested;
    out->effective = effective;
    out->layer = layer;
    out->usedRelated = usedRelated;
    out->families = arr;
    out->familyCount = m->familyCount;
    out->configGeneration = cfgGen;
    out->registryGeneration = regGen;
    return RoboOk;
}

/* ---- cache ---- */

void FontGenericResolverCacheClear(FontGenericResolver *r)
{
    uint32_t i;
    if (!r || r->cookie != RESOLVER_COOKIE || !r->cache) {
        return;
    }
    for (i = 0u; i < r->cacheCap; i++) {
        memset(&r->cache[i], 0, sizeof(r->cache[i]));
    }
    r->cacheCount = 0u;
    r->stats.invalidations++;
    r->stats.occupied = 0u;
}

void FontGenericResolverCacheStats(const FontGenericResolver *r,
                                            FontGenericCacheStats *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return;
    }
    *out = r->stats;
    out->capacity = r->cacheCap;
    out->occupied = r->cacheCount;
}

static void CacheStore(FontGenericResolver *r,
                        uint64_t cfgGen,
                        uint64_t regGen,
                        const FontGenericRequest *req,
                        const FontGenericResult *res)
{
    GenericCacheEntry *slot = NULL;
    uint32_t i;
    uint64_t oldest = UINT64_MAX;
    size_t blob = 0u;
    uint32_t n;

    if (!r->cache || r->cacheCap == 0u || !res->resolved) {
        return;
    }
    n = res->familyCount;
    if (n > RoboFontGenericMaxFamiliesPerMapping) {
        n = RoboFontGenericMaxFamiliesPerMapping;
    }
    for (i = 0u; i < n; i++) {
        size_t len = strlen(res->families[i]) + 1u;
        if (blob + len > RoboFontGenericMaxFamilyBytes) {
            return; /* too large to cache */
        }
        blob += len;
    }

    for (i = 0u; i < r->cacheCap; i++) {
        if (r->cache[i].familyCount == 0u && !r->cache[i].resolved) {
            slot = &r->cache[i];
            break;
        }
        if (r->cache[i].lruTick < oldest) {
            oldest = r->cache[i].lruTick;
            slot = &r->cache[i];
        }
    }
    if (!slot) {
        return;
    }
    if (slot->familyCount == 0u && !slot->resolved) {
        r->cacheCount++;
    }
    memset(slot, 0, sizeof(*slot));
    slot->resolverGeneration = r->generation;
    slot->registryGeneration = regGen;
    slot->configGeneration = cfgGen;
    slot->generic = req->generic;
    slot->langHash = HashCstr(req->language, req->languageLen);
    slot->flags = req->flags | r->flags;
    slot->resolved = res->resolved;
    slot->usedRelated = res->usedRelated;
    slot->effective = res->effective;
    slot->layer = res->layer;
    slot->familyCount = n;
    blob = 0u;
    for (i = 0u; i < n; i++) {
        size_t len = strlen(res->families[i]) + 1u;
        slot->familyOffs[i] = (uint16_t)blob;
        memcpy(slot->familiesBlob + blob, res->families[i], len);
        blob += len;
    }
    slot->lruTick = ++r->lruTick;
    r->stats.inserts++;
    r->stats.occupied = r->cacheCount;
}

static GenericCacheEntry *CacheLookup(FontGenericResolver *r,
                                           uint64_t cfgGen,
                                           uint64_t regGen,
                                           const FontGenericRequest *req)
{
    uint32_t i;
    uint32_t langHash = HashCstr(req->language, req->languageLen);
    uint32_t flags = req->flags | r->flags;
    if (!r->cache) {
        return NULL;
    }
    for (i = 0u; i < r->cacheCap; i++) {
        GenericCacheEntry *e = &r->cache[i];
        if (!e->resolved && e->familyCount == 0u) {
            continue;
        }
        if (e->resolverGeneration == r->generation &&
            e->configGeneration == cfgGen && e->registryGeneration == regGen &&
            e->generic == req->generic && e->langHash == langHash &&
            e->flags == flags) {
            e->lruTick = ++r->lruTick;
            return e;
        }
    }
    return NULL;
}

static Result ResultFromCache(FontGenericResult *out,
                                       const GenericCacheEntry *e,
                                       FontGenericFamily requested)
{
    uint32_t i;
    char **arr;
    if (!e->resolved) {
        out->resolved = 0;
        out->fromCache = 1;
        out->requested = requested;
        out->effective = e->effective;
        out->layer = e->layer;
        out->usedRelated = e->usedRelated;
        out->configGeneration = e->configGeneration;
        out->registryGeneration = e->registryGeneration;
        return RoboOk;
    }
    arr = (char **)RoboCalloc(e->familyCount, sizeof(char *));
    if (!arr) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < e->familyCount; i++) {
        const char *s = e->familiesBlob + e->familyOffs[i];
        arr[i] = DupN(s, 0u);
        if (!arr[i]) {
            uint32_t j;
            for (j = 0u; j < i; j++) {
                RoboFree(arr[j]);
            }
            RoboFree(arr);
            return RoboOutOfMemory;
        }
    }
    out->resolved = 1;
    out->fromCache = 1;
    out->usedRelated = e->usedRelated;
    out->requested = requested;
    out->effective = e->effective;
    out->layer = e->layer;
    out->families = arr;
    out->familyCount = e->familyCount;
    out->configGeneration = e->configGeneration;
    out->registryGeneration = e->registryGeneration;
    return RoboOk;
}

/* ---- resolver ---- */

void FontGenericResolverOptionsInit(
    FontGenericResolverOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->flags = RoboFontGenericFlagSkipMissing;
    opts->cacheCapacity = RoboFontGenericCacheDefault;
}

Result FontGenericResolverCreate(
    FontRegistry *registry,
    const FontGenericResolverOptions *opts,
    FontGenericResolver **out)
{
    FontGenericResolver *r;
    FontGenericResolverOptions local;
    uint32_t cap;

    if (!registry || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (!opts) {
        FontGenericResolverOptionsInit(&local);
        opts = &local;
    }
    cap = opts->cacheCapacity ? opts->cacheCapacity : RoboFontGenericCacheDefault;
    if (cap > RoboFontGenericCacheHard) {
        cap = RoboFontGenericCacheHard;
    }

    r = (FontGenericResolver *)RoboCalloc(1, sizeof(*r));
    if (!r) {
        return RoboOutOfMemory;
    }
    r->cookie = RESOLVER_COOKIE;
    r->registry = registry;
    r->flags = opts->flags;
    r->generation = 1u;
    r->cacheCap = cap;
    if (cap > 0u) {
        r->cache = (GenericCacheEntry *)RoboCalloc(cap, sizeof(*r->cache));
        if (!r->cache) {
            RoboFree(r);
            return RoboOutOfMemory;
        }
    }
    r->stats.capacity = cap;

    if (opts->platform) {
        FontGenericConfigRetain((FontGenericConfig *)opts->platform);
        r->platform = (FontGenericConfig *)opts->platform;
    }
    if (opts->runtime) {
        FontGenericConfigRetain((FontGenericConfig *)opts->runtime);
        r->runtime = (FontGenericConfig *)opts->runtime;
    }
    if (opts->application) {
        FontGenericConfigRetain((FontGenericConfig *)opts->application);
        r->application = (FontGenericConfig *)opts->application;
    }

    *out = r;
    return RoboOk;
}

void FontGenericResolverDestroy(FontGenericResolver *r)
{
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return;
    }
    if (r->platform) {
        FontGenericConfigRelease(r->platform);
    }
    if (r->runtime) {
        FontGenericConfigRelease(r->runtime);
    }
    if (r->application) {
        FontGenericConfigRelease(r->application);
    }
    RoboFree(r->cache);
    r->cookie = 0u;
    RoboFree(r);
}

static Result SetLayer(FontGenericResolver *r,
                               FontGenericConfig **slot,
                               FontGenericConfig *cfg)
{
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return RoboInvalidArgument;
    }
    if (cfg && cfg->cookie != CONFIG_COOKIE) {
        return RoboInvalidArgument;
    }
    if (*slot) {
        FontGenericConfigRelease(*slot);
        *slot = NULL;
    }
    if (cfg) {
        FontGenericConfigRetain(cfg);
        *slot = cfg;
    }
    r->generation++;
    if (r->generation == 0u) {
        r->generation = 1u;
    }
    FontGenericResolverCacheClear(r);
    return RoboOk;
}

Result FontGenericResolverSetPlatform(
    FontGenericResolver *r,
    FontGenericConfig *cfg)
{
    return SetLayer(r, &r->platform, cfg);
}

Result FontGenericResolverSetRuntime(
    FontGenericResolver *r,
    FontGenericConfig *cfg)
{
    return SetLayer(r, &r->runtime, cfg);
}

Result FontGenericResolverSetApplication(
    FontGenericResolver *r,
    FontGenericConfig *cfg)
{
    return SetLayer(r, &r->application, cfg);
}

uint64_t FontGenericResolverGeneration(
    const FontGenericResolver *r)
{
    return r && r->cookie == RESOLVER_COOKIE ? r->generation : 0u;
}

void FontGenericRequestInit(FontGenericRequest *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
    req->generic = RoboFontGenericUnknown;
}

Result FontGenericRequestValidate(
    const FontGenericRequest *req)
{
    if (!req) {
        return RoboInvalidArgument;
    }
    if (!FontGenericFamilyIsValid(req->generic)) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

Result FontGenericResolve(FontGenericResolver *r,
                                        const FontGenericRequest *req,
                                        FontGenericResult *out)
{
    const Mapping *m = NULL;
    FontGenericLayer layer = RoboFontGenericLayerNone;
    FontGenericFamily effective;
    uint8_t usedRelated = 0;
    uint64_t cfgGen = 0u;
    uint64_t regGen = 0u;
    uint32_t flags;
    int useCache;
    GenericCacheEntry *hit;
    Result res;

    if (!r || r->cookie != RESOLVER_COOKIE || !out) {
        return RoboInvalidArgument;
    }
    res = FontGenericRequestValidate(req);
    if (res != RoboOk) {
        return res;
    }

    if (out->cookie == RoboFontGenericResultCookie) {
        FontGenericResultClear(out);
    } else {
        FontGenericResultInit(out);
    }

    flags = req->flags | r->flags;
    useCache = (flags & RoboFontGenericFlagNoCache) == 0 && r->cacheCap > 0u;
    CombinedConfigGeneration(r, &cfgGen);
    regGen = FontRegistryGeneration(r->registry);

    if (useCache) {
        hit = CacheLookup(r, cfgGen, regGen, req);
        if (hit) {
            r->stats.hits++;
            return ResultFromCache(out, hit, req->generic);
        }
        r->stats.misses++;
    }

    effective = req->generic;
    if (!PickMapping(r, effective, req->language, req->languageLen, &m, &layer)) {
        if ((flags & RoboFontGenericFlagAllowRelated) != 0u) {
            FontGenericFamily rel = FontGenericRelated(req->generic);
            if (rel != RoboFontGenericUnknown &&
                PickMapping(r, rel, req->language, req->languageLen, &m, &layer)) {
                effective = rel;
                usedRelated = 1;
                layer = RoboFontGenericLayerRelated;
            }
        }
    }

    if (!m || m->familyCount == 0u) {
        out->resolved = 0;
        out->requested = req->generic;
        out->effective = req->generic;
        out->layer = RoboFontGenericLayerNone;
        out->configGeneration = cfgGen;
        out->registryGeneration = regGen;
        if (useCache) {
            /* Cache unresolved briefly via store path only for resolved —
             * still ok to skip. */
        }
        return RoboOk;
    }

    res = FillResultFromMapping(out, m, req->generic, effective, layer,
                                   usedRelated, cfgGen, regGen);
    if (res != RoboOk) {
        return res;
    }
    if (useCache) {
        CacheStore(r, cfgGen, regGen, req, out);
    }
    return RoboOk;
}

Result FontGenericResolveSelect(
    FontGenericResolver *r,
    const FontGenericRequest *req,
    const FontSelectRequest *style,
    FontGenericResult *genericOut,
    FontSelection *selectionOut)
{
    FontGenericResult local;
    FontGenericResult *gres;
    FontSelectRequest sreq;
    uint32_t i;
    uint32_t flags;
    Result res;

    if (!r || !req || !style || !selectionOut) {
        return RoboInvalidArgument;
    }

    gres = genericOut ? genericOut : &local;
    if (!genericOut) {
        FontGenericResultInit(&local);
    }

    res = FontGenericResolve(r, req, gres);
    if (res != RoboOk) {
        if (!genericOut) {
            FontGenericResultClear(&local);
        }
        return res;
    }

    if (selectionOut->cookie == RoboFontSelectionCookie) {
        FontSelectionClear(selectionOut);
    } else {
        FontSelectionInit(selectionOut);
    }

    if (!gres->resolved || gres->familyCount == 0u) {
        if (!genericOut) {
            FontGenericResultClear(&local);
        }
        return RoboOk;
    }

    flags = req->flags | r->flags;
    sreq = *style;

    for (i = 0u; i < gres->familyCount; i++) {
        FontFace **faces = NULL;
        uint32_t faceN = 0u;
        FontSelection sel;

        res = FontRegistryCollectFacesForFamily(
            r->registry, gres->families[i], &faces, &faceN);
        if (res != RoboOk) {
            if (!genericOut) {
                FontGenericResultClear(&local);
            }
            return res;
        }
        if (faceN == 0u) {
            RoboFree(faces);
            if ((flags & RoboFontGenericFlagSkipMissing) != 0u) {
                continue;
            }
            /* No faces and not skipping — still try select (will miss). */
        }

        sreq.family = gres->families[i];
        sreq.familyLen = 0u;
        FontSelectionInit(&sel);
        res = FontSelect(&sreq, faces, faceN, &sel);
        RoboFree(faces);
        if (res != RoboOk) {
            FontSelectionClear(&sel);
            if (!genericOut) {
                FontGenericResultClear(&local);
            }
            return res;
        }
        if (sel.found) {
            *selectionOut = sel;
            /* ownership transferred */
            if (!genericOut) {
                FontGenericResultClear(&local);
            }
            return RoboOk;
        }
        FontSelectionClear(&sel);
    }

    if (!genericOut) {
        FontGenericResultClear(&local);
    }
    return RoboOk;
}

Result FontGenericResolveFallback(
    FontGenericResolver *r,
    FontFallbackResolver *fallback,
    const FontGenericRequest *req,
    const FontSelectRequest *style,
    const uint32_t *codepoints,
    uint32_t codepointCount,
    FontGenericResult *genericOut,
    FontSelection *selectionOut,
    FontFallbackResult *fallbackOut)
{
    FontGenericResult localG;
    FontSelection localS;
    FontGenericResult *gres;
    FontSelection *sel;
    FontFallbackRequest freq;
    Result res;
    int ownG = 0;
    int ownS = 0;

    if (!r || !fallback || !req || !style || !codepoints || codepointCount == 0u ||
        !fallbackOut) {
        return RoboInvalidArgument;
    }

    gres = genericOut ? genericOut : &localG;
    sel = selectionOut ? selectionOut : &localS;
    if (!genericOut) {
        FontGenericResultInit(&localG);
        ownG = 1;
    }
    if (!selectionOut) {
        FontSelectionInit(&localS);
        ownS = 1;
    }

    res = FontGenericResolveSelect(r, req, style, gres, sel);
    if (res != RoboOk) {
        goto done;
    }
    if (!sel->found || !sel->face) {
        /* No primary from generic — explicit unresolved for fallback path. */
        if (fallbackOut->cookie == RoboFontFallbackResultCookie) {
            FontFallbackResultClear(fallbackOut);
        } else {
            FontFallbackResultInit(fallbackOut);
        }
        res = RoboOk;
        goto done;
    }

    FontFallbackRequestInit(&freq);
    freq.primary = sel->face;
    freq.codepoints = codepoints;
    freq.codepointCount = codepointCount;
    freq.weight = style->weight;
    freq.stretch = style->stretch;
    freq.style = style->style;
    freq.hasObliqueAngle = style->hasObliqueAngle;
    freq.obliqueAngleMillidegrees = style->obliqueAngleMillidegrees;
    freq.language = req->language;
    freq.languageLen = req->languageLen;
    freq.hasScript = req->hasScript;
    freq.script = req->script;
    if (gres->resolved && gres->familyCount > 0u) {
        freq.preferredFamilies = (const char *const *)gres->families;
        freq.preferredFamilyCount = gres->familyCount;
    }

    res = FontFallbackResolve(fallback, &freq, fallbackOut);

done:
    if (ownG) {
        FontGenericResultClear(&localG);
    }
    if (ownS) {
        FontSelectionClear(&localS);
    }
    return res;
}

Result FontGenericFuzzParse(const uint8_t *data, size_t size)
{
    FontGenericConfig *cfg = NULL;
    FontGenericFamily g = RoboFontGenericUnknown;
    const char *safeName = "FuzzFamily";
    const char *names[1];
    char buf[64];
    size_t i;

    if (!data || size == 0u) {
        return RoboOk;
    }

    names[0] = safeName;

    (void)FontGenericConfigCreate(&cfg);
    if (!cfg) {
        return RoboOk;
    }

    {
        size_t take = size < 63u ? size : 63u;
        memcpy(buf, data, take);
        buf[take] = '\0';
        for (i = 0u; i < take; i++) {
            if (buf[i] == '\0') {
                buf[i] = 'x';
            }
        }
        (void)FontGenericFamilyFromName(buf, take, &g);
        if (FontGenericFamilyIsValid(g)) {
            names[0] = buf;
            (void)FontGenericConfigSetMapping(cfg, g, names, 1u);
        }
        /* Pathological large count — must reject without reading past names[]. */
        (void)FontGenericConfigSetMapping(
            cfg, RoboFontGenericSansSerif, names,
            RoboFontGenericMaxFamiliesPerMapping + 1u);
        (void)FontGenericRelated((FontGenericFamily)(data[0] % 16u));
        (void)FontGenericFamilyName((FontGenericFamily)(data[0] % 16u));
        names[0] = safeName;
        (void)FontGenericConfigSetMappingForLanguage(
            cfg, RoboFontGenericSerif, "xx", 2u, names, 1u);
        /* Unusual Unicode family name — bounded length, must not crash. */
        {
            static const char unicodeName[] = "\xD9\x81\xD9\x88\xD9\x86\xD8\xAA";
            const char *u[1];
            u[0] = unicodeName;
            (void)FontGenericConfigSetMapping(
                cfg, RoboFontGenericCursive, u, 1u);
        }
    }
    (void)FontGenericConfigClear(cfg);
    FontGenericConfigRelease(cfg);
    return RoboOk;
}
