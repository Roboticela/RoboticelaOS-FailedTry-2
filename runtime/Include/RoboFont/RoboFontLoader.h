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

#define ROBO_FONT_CACHE_DEFAULT_MAX_FACES 64u
#define ROBO_FONT_CACHE_DEFAULT_MAX_BYTES (32u * 1024u * 1024u)
#define ROBO_FONT_CACHE_MAX_FACES_HARD 4096u

typedef struct robo_font_loader robo_font_loader_t;
typedef struct robo_font_cache robo_font_cache_t;

typedef enum robo_font_load_state {
    ROBO_FONT_LOAD_UNLOADED = 0,
    ROBO_FONT_LOAD_LOADING = 1,
    ROBO_FONT_LOAD_LOADED = 2,
    ROBO_FONT_LOAD_FAILED = 3,
    ROBO_FONT_LOAD_EVICTED = 4
} robo_font_load_state_t;

typedef enum robo_font_load_policy {
    /** SFNT + TT + metadata + classification (no cmap/metrics/outlines). */
    ROBO_FONT_LOAD_POLICY_MINIMAL = 0,
    /** MINIMAL + cmap + metrics. Default. Outlines remain lazy. */
    ROBO_FONT_LOAD_POLICY_STANDARD = 1,
    /** STANDARD + outline cache object (glyphs still decoded on demand). */
    ROBO_FONT_LOAD_POLICY_READY = 2
} robo_font_load_policy_t;

typedef enum robo_font_validate_policy {
    ROBO_FONT_VALIDATE_NORMAL = 0,
    /** Extra checksums / consistency probes. Bounds always on. */
    ROBO_FONT_VALIDATE_STRICT = 1,
    /** Same bounds as NORMAL; skips optional checksum probes. */
    ROBO_FONT_VALIDATE_TRUSTED = 2,
    /** STRICT + color/variation/OT structural probes (Goal 5.20). */
    ROBO_FONT_VALIDATE_PARANOID = 3
} robo_font_validate_policy_t;

typedef enum robo_font_cache_policy {
    ROBO_FONT_CACHE_POLICY_DEFAULT = 0,
    /** Load but do not insert into the cache. */
    ROBO_FONT_CACHE_POLICY_BYPASS = 1,
    /** Prefer keeping this entry (LRU touch + higher residency). */
    ROBO_FONT_CACHE_POLICY_PIN = 2
} robo_font_cache_policy_t;

/**
 * Cache key — never a raw pointer.
 * registration_id uniquely identifies a registry entry for this process.
 */
typedef struct robo_font_cache_key {
    robo_font_registration_id_t registration_id;
    robo_font_discovery_source_id_t source_id;
    uint32_t face_index;
} robo_font_cache_key_t;

typedef struct robo_font_load_request {
    robo_font_registration_id_t registration_id;
    robo_font_discovery_source_id_t source_id; /* optional hint; 0 = ignore */
    robo_font_load_policy_t load_policy;
    robo_font_cache_policy_t cache_policy;
    robo_font_validate_policy_t validate_policy;
    uint32_t reserved;
} robo_font_load_request_t;

typedef struct robo_font_load_result {
    robo_font_load_state_t state;
    uint8_t from_cache;
    uint8_t retryable; /* 1 if caller may retry later */
    robo_result_t error;
    robo_font_face_t *face; /* retained for caller; release with face_release */
    robo_font_cache_key_t key;
} robo_font_load_result_t;

typedef struct robo_font_cache_stats {
    uint32_t entries;
    uint32_t hits;
    uint32_t misses;
    uint32_t inserts;
    uint32_t evictions;
    uint32_t invalidations;
    uint64_t approx_bytes;
    uint32_t max_faces;
    uint64_t max_bytes;
} robo_font_cache_stats_t;

typedef struct robo_font_loader_config {
    uint32_t max_faces;  /* 0 → default */
    uint64_t max_bytes;  /* 0 → default; approximate source+overhead budget */
    robo_font_load_policy_t default_load_policy;
    robo_font_validate_policy_t default_validate_policy;
} robo_font_loader_config_t;

void robo_font_load_request_init(robo_font_load_request_t *req);
void robo_font_load_result_init(robo_font_load_result_t *res);
void robo_font_load_result_clear(robo_font_load_result_t *res);
void robo_font_loader_config_init(robo_font_loader_config_t *cfg);

/**
 * Create a loader bound to @p registry (non-owning).
 * Registers an invalidate hook on the registry.
 */
robo_result_t robo_font_loader_create(robo_font_registry_t *registry,
                                      const robo_font_loader_config_t *config,
                                      robo_font_loader_t **out);

void robo_font_loader_destroy(robo_font_loader_t *loader);

robo_font_cache_t *robo_font_loader_cache(robo_font_loader_t *loader);
const robo_font_cache_stats_t *robo_font_cache_stats(const robo_font_cache_t *cache);

/**
 * Load (or cache-hit) a registered face.
 * On success, result.face is retained for the caller.
 * Concurrent same-key loads: single-flight; second waiter shares the result
 * when the runtime is single-threaded. Multi-thread: external sync required
 * (same as registry); in-flight table prevents publishing partial faces.
 */
robo_result_t robo_font_loader_load(robo_font_loader_t *loader,
                                    const robo_font_load_request_t *request,
                                    robo_font_load_result_t *out_result);

/** Convenience: STANDARD load by registration id. */
robo_result_t robo_font_loader_load_registration(robo_font_loader_t *loader,
                                                 robo_font_registration_id_t id,
                                                 robo_font_load_result_t *out);

robo_font_load_state_t robo_font_loader_state(const robo_font_loader_t *loader,
                                              robo_font_registration_id_t id);

/* ---- Cache control ---- */

robo_result_t robo_font_cache_invalidate_registration(robo_font_cache_t *cache,
                                                      robo_font_registration_id_t id);
robo_result_t robo_font_cache_invalidate_source(robo_font_cache_t *cache,
                                                robo_font_discovery_source_id_t source_id);
void robo_font_cache_clear(robo_font_cache_t *cache);

/** Evict until under limits; skips pinned / in-flight entries. */
robo_result_t robo_font_cache_evict_to_limits(robo_font_cache_t *cache);

uint32_t robo_font_cache_entry_count(const robo_font_cache_t *cache);

/** Approx bytes attributed to a face (source size + fixed overhead). */
uint64_t robo_font_loader_approx_face_bytes(const robo_font_face_t *face);

/** Fuzz entry — never crashes. */
robo_result_t robo_font_loader_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_LOADER_H */
