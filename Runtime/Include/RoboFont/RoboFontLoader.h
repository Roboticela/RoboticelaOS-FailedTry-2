/*
 * RoboFont loader & cache — on-demand face initialization (Goal 5.13).
 *
 * discover/register (5.12) → load (this) → cache → select (5.11)
 *
 * Discovery stays lightweight (identity + metadata snapshot). Loading parses
 * SFNT/TT/name/classification and optionally cmap/metrics. Glyph outlines stay
 * lazy via face APIs. No selection, fallback, shaping, or rasterization.
 *
 * Forbidden: Chromium / Skia font caches.
 */

#ifndef ROBO_FONT_LOADER_H
#define ROBO_FONT_LOADER_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontRegistry.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboFontCacheDefaultMaxFaces 64u
#define RoboFontCacheDefaultMaxBytes (32u * 1024u * 1024u)
#define RoboFontCacheMaxFacesHard 4096u

typedef struct FontLoader FontLoader;
typedef struct FontCache FontCache;

typedef enum FontLoadState {
    RoboFontLoadUnloaded = 0,
    RoboFontLoadLoading = 1,
    RoboFontLoadLoaded = 2,
    RoboFontLoadFailed = 3,
    RoboFontLoadEvicted = 4
} FontLoadState;

typedef enum FontLoadPolicy {
    /** SFNT + TT + metadata + classification (no cmap/metrics/outlines). */
    RoboFontLoadPolicyMinimal = 0,
    /** MINIMAL + cmap + metrics. Default. Outlines remain lazy. */
    RoboFontLoadPolicyStandard = 1,
    /** STANDARD + outline cache object (glyphs still decoded on demand). */
    RoboFontLoadPolicyReady = 2
} FontLoadPolicy;

typedef enum FontValidatePolicy {
    RoboFontValidateNormal = 0,
    /** Extra checksums / consistency probes. Bounds always on. */
    RoboFontValidateStrict = 1,
    /** Same bounds as NORMAL; skips optional checksum probes. */
    RoboFontValidateTrusted = 2,
    /** STRICT + color/variation/OT structural probes (Goal 5.20). */
    RoboFontValidateParanoid = 3
} FontValidatePolicy;

typedef enum FontCachePolicy {
    RoboFontCachePolicyDefault = 0,
    /** Load but do not insert into the cache. */
    RoboFontCachePolicyBypass = 1,
    /** Prefer keeping this entry (LRU touch + higher residency). */
    RoboFontCachePolicyPin = 2
} FontCachePolicy;

/**
 * Cache key — never a raw pointer.
 * registrationId uniquely identifies a registry entry for this process.
 */
typedef struct FontCacheKey {
    FontRegistrationId registrationId;
    FontDiscoverySourceId sourceId;
    uint32_t faceIndex;
} FontCacheKey;

typedef struct FontLoadRequest {
    FontRegistrationId registrationId;
    FontDiscoverySourceId sourceId; /* optional hint; 0 = ignore */
    FontLoadPolicy loadPolicy;
    FontCachePolicy cachePolicy;
    FontValidatePolicy validatePolicy;
    uint32_t reserved;
} FontLoadRequest;

typedef struct FontLoadResult {
    FontLoadState state;
    uint8_t fromCache;
    uint8_t retryable; /* 1 if caller may retry later */
    Result error;
    FontFace *face; /* retained for caller; release with face_release */
    FontCacheKey key;
} FontLoadResult;

typedef struct FontCacheStats {
    uint32_t entries;
    uint32_t hits;
    uint32_t misses;
    uint32_t inserts;
    uint32_t evictions;
    uint32_t invalidations;
    uint64_t approxBytes;
    uint32_t maxFaces;
    uint64_t maxBytes;
} FontCacheStats;

typedef struct FontLoaderConfig {
    uint32_t maxFaces;  /* 0 → default */
    uint64_t maxBytes;  /* 0 → default; approximate source+overhead budget */
    FontLoadPolicy defaultLoadPolicy;
    FontValidatePolicy defaultValidatePolicy;
} FontLoaderConfig;

void FontLoadRequestInit(FontLoadRequest *req);
void FontLoadResultInit(FontLoadResult *res);
void FontLoadResultClear(FontLoadResult *res);
void FontLoaderConfigInit(FontLoaderConfig *cfg);

/**
 * Create a loader bound to @p registry (non-owning).
 * Registers an invalidate hook on the registry.
 */
Result FontLoaderCreate(FontRegistry *registry,
                                      const FontLoaderConfig *config,
                                      FontLoader **out);

void FontLoaderDestroy(FontLoader *loader);

FontCache *FontLoaderCache(FontLoader *loader);
const FontCacheStats *GetFontCacheStats(const FontCache *cache);

/**
 * Load (or cache-hit) a registered face.
 * On success, result.face is retained for the caller.
 * Concurrent same-key loads: single-flight; second waiter shares the result
 * when the runtime is single-threaded. Multi-thread: external sync required
 * (same as registry); in-flight table prevents publishing partial faces.
 */
Result FontLoaderLoad(FontLoader *loader,
                                    const FontLoadRequest *request,
                                    FontLoadResult *outResult);

/** Convenience: STANDARD load by registration id. */
Result FontLoaderLoadRegistration(FontLoader *loader,
                                                 FontRegistrationId id,
                                                 FontLoadResult *out);

FontLoadState FontLoaderState(const FontLoader *loader,
                                              FontRegistrationId id);

/* ---- Cache control ---- */

Result FontCacheInvalidateRegistration(FontCache *cache,
                                                      FontRegistrationId id);
Result FontCacheInvalidateSource(FontCache *cache,
                                                FontDiscoverySourceId sourceId);
void FontCacheClear(FontCache *cache);

/** Evict until under limits; skips pinned / in-flight entries. */
Result FontCacheEvictToLimits(FontCache *cache);

uint32_t FontCacheEntryCount(const FontCache *cache);

/** Approx bytes attributed to a face (source size + fixed overhead). */
uint64_t FontLoaderApproxFaceBytes(const FontFace *face);

/** Fuzz entry — never crashes. */
Result FontLoaderFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_LOADER_H */
