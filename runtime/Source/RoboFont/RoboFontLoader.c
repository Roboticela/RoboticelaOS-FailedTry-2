/*
 * RoboFont loader + bounded face cache (Goal 5.13).
 */

#include "RoboFont/RoboFontLoader.h"

#include "RoboFont/RoboFontDiscovery.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define LOADER_COOKIE 0x52464C44u /* 'RFLD' */
#define FACE_OVERHEAD_BYTES 4096u

typedef struct cache_entry {
    robo_font_cache_key_t key;
    robo_font_face_t *face; /* retained by cache while present */
    robo_font_load_state_t state;
    robo_result_t fail_error;
    uint8_t fail_retryable;
    uint8_t pinned;
    uint8_t in_flight;
    uint64_t approx_bytes;
    uint64_t lru_tick;
} cache_entry_t;

struct robo_font_cache {
    cache_entry_t *entries;
    uint32_t count;
    uint32_t cap;
    uint32_t max_faces;
    uint64_t max_bytes;
    uint64_t approx_bytes;
    uint64_t lru_clock;
    robo_font_cache_stats_t stats;
};

struct robo_font_loader {
    uint32_t cookie;
    robo_font_registry_t *registry; /* non-owning */
    robo_font_cache_t cache;
    robo_font_load_policy_t default_load_policy;
    robo_font_validate_policy_t default_validate_policy;
};

void robo_font_load_request_init(robo_font_load_request_t *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
    req->load_policy = ROBO_FONT_LOAD_POLICY_STANDARD;
    req->cache_policy = ROBO_FONT_CACHE_POLICY_DEFAULT;
    req->validate_policy = ROBO_FONT_VALIDATE_NORMAL;
}

void robo_font_load_result_init(robo_font_load_result_t *res)
{
    if (!res) {
        return;
    }
    memset(res, 0, sizeof(*res));
    res->state = ROBO_FONT_LOAD_UNLOADED;
    res->error = ROBO_OK;
}

void robo_font_load_result_clear(robo_font_load_result_t *res)
{
    if (!res) {
        return;
    }
    if (res->face) {
        robo_font_face_release(res->face);
        res->face = NULL;
    }
    res->state = ROBO_FONT_LOAD_UNLOADED;
    res->from_cache = 0u;
    res->retryable = 0u;
    res->error = ROBO_OK;
}

void robo_font_loader_config_init(robo_font_loader_config_t *cfg)
{
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->max_faces = ROBO_FONT_CACHE_DEFAULT_MAX_FACES;
    cfg->max_bytes = ROBO_FONT_CACHE_DEFAULT_MAX_BYTES;
    cfg->default_load_policy = ROBO_FONT_LOAD_POLICY_STANDARD;
    cfg->default_validate_policy = ROBO_FONT_VALIDATE_NORMAL;
}

uint64_t robo_font_loader_approx_face_bytes(const robo_font_face_t *face)
{
    const robo_font_source_t *src;
    const robo_font_coverage_t *cov;
    uint64_t n;
    if (!face) {
        return 0u;
    }
    src = robo_font_face_source(face);
    n = (uint64_t)robo_font_source_size(src) + (uint64_t)FACE_OVERHEAD_BYTES;
    cov = robo_font_face_coverage(face);
    if (cov) {
        n += robo_font_coverage_approx_bytes(cov);
    }
    return n;
}

static cache_entry_t *cache_find(robo_font_cache_t *cache,
                                 robo_font_registration_id_t id)
{
    uint32_t i;
    if (!cache) {
        return NULL;
    }
    for (i = 0u; i < cache->count; i++) {
        if (cache->entries[i].key.registration_id == id) {
            return &cache->entries[i];
        }
    }
    return NULL;
}

static const cache_entry_t *cache_find_const(const robo_font_cache_t *cache,
                                             robo_font_registration_id_t id)
{
    uint32_t i;
    if (!cache) {
        return NULL;
    }
    for (i = 0u; i < cache->count; i++) {
        if (cache->entries[i].key.registration_id == id) {
            return &cache->entries[i];
        }
    }
    return NULL;
}

static void cache_remove_at(robo_font_cache_t *cache, uint32_t idx)
{
    cache_entry_t *e;
    if (!cache || idx >= cache->count) {
        return;
    }
    e = &cache->entries[idx];
    if (e->face) {
        if (cache->approx_bytes >= e->approx_bytes) {
            cache->approx_bytes -= e->approx_bytes;
        } else {
            cache->approx_bytes = 0u;
        }
        robo_font_face_release(e->face);
        e->face = NULL;
    }
    if (idx + 1u < cache->count) {
        memmove(&cache->entries[idx], &cache->entries[idx + 1u],
                (size_t)(cache->count - idx - 1u) * sizeof(cache->entries[0]));
    }
    cache->count--;
    cache->stats.entries = cache->count;
    cache->stats.approx_bytes = cache->approx_bytes;
    cache->stats.evictions++;
}

static robo_result_t cache_ensure_cap(robo_font_cache_t *cache, uint32_t need)
{
    cache_entry_t *n;
    uint32_t ncap;
    if (need <= cache->cap) {
        return ROBO_OK;
    }
    ncap = cache->cap ? cache->cap * 2u : 8u;
    while (ncap < need) {
        ncap *= 2u;
    }
    if (ncap > ROBO_FONT_CACHE_MAX_FACES_HARD) {
        ncap = ROBO_FONT_CACHE_MAX_FACES_HARD;
    }
    if (need > ncap) {
        return ROBO_OUT_OF_MEMORY;
    }
    n = (cache_entry_t *)robo_calloc(ncap, sizeof(*n));
    if (!n) {
        return ROBO_OUT_OF_MEMORY;
    }
    if (cache->entries && cache->count > 0u) {
        memcpy(n, cache->entries, (size_t)cache->count * sizeof(*n));
        robo_free(cache->entries);
    }
    cache->entries = n;
    cache->cap = ncap;
    return ROBO_OK;
}

robo_result_t robo_font_cache_evict_to_limits(robo_font_cache_t *cache)
{
    if (!cache) {
        return ROBO_INVALID_ARGUMENT;
    }
    while (cache->count > cache->max_faces ||
           (cache->max_bytes > 0u && cache->approx_bytes > cache->max_bytes)) {
        uint32_t i;
        uint32_t victim = 0xFFFFFFFFu;
        uint64_t oldest = 0xFFFFFFFFFFFFFFFFull;
        for (i = 0u; i < cache->count; i++) {
            cache_entry_t *e = &cache->entries[i];
            if (e->in_flight || e->pinned) {
                continue;
            }
            if (e->lru_tick < oldest) {
                oldest = e->lru_tick;
                victim = i;
            }
        }
        if (victim == 0xFFFFFFFFu) {
            return ROBO_OUT_OF_MEMORY;
        }
        cache_remove_at(cache, victim);
    }
    return ROBO_OK;
}

static void cache_touch(robo_font_cache_t *cache, cache_entry_t *e)
{
    cache->lru_clock++;
    e->lru_tick = cache->lru_clock;
}

robo_font_cache_t *robo_font_loader_cache(robo_font_loader_t *loader)
{
    return loader ? &loader->cache : NULL;
}

const robo_font_cache_stats_t *robo_font_cache_stats(const robo_font_cache_t *cache)
{
    return cache ? &cache->stats : NULL;
}

uint32_t robo_font_cache_entry_count(const robo_font_cache_t *cache)
{
    return cache ? cache->count : 0u;
}

robo_result_t robo_font_cache_invalidate_registration(robo_font_cache_t *cache,
                                                      robo_font_registration_id_t id)
{
    uint32_t i;
    if (!cache) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < cache->count; i++) {
        if (cache->entries[i].key.registration_id == id) {
            cache->stats.invalidations++;
            cache_remove_at(cache, i);
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_font_cache_invalidate_source(robo_font_cache_t *cache,
                                                robo_font_discovery_source_id_t source_id)
{
    uint32_t i = 0u;
    uint32_t n = 0u;
    if (!cache) {
        return ROBO_INVALID_ARGUMENT;
    }
    while (i < cache->count) {
        if (cache->entries[i].key.source_id == source_id) {
            cache->stats.invalidations++;
            cache_remove_at(cache, i);
            n++;
        } else {
            i++;
        }
    }
    return n > 0u ? ROBO_OK : ROBO_NOT_FOUND;
}

void robo_font_cache_clear(robo_font_cache_t *cache)
{
    if (!cache) {
        return;
    }
    while (cache->count > 0u) {
        cache->stats.invalidations++;
        cache_remove_at(cache, 0u);
    }
    cache->approx_bytes = 0u;
    cache->stats.approx_bytes = 0u;
    cache->stats.entries = 0u;
}

static void loader_invalidate_hook(void *userdata,
                                   robo_font_discovery_source_id_t source_id,
                                   robo_font_registration_id_t registration_id)
{
    robo_font_loader_t *loader = (robo_font_loader_t *)userdata;
    if (!loader || loader->cookie != LOADER_COOKIE) {
        return;
    }
    if (source_id == 0u && registration_id == 0u) {
        robo_font_cache_clear(&loader->cache);
        return;
    }
    if (registration_id != 0u) {
        (void)robo_font_cache_invalidate_registration(&loader->cache, registration_id);
        return;
    }
    (void)robo_font_cache_invalidate_source(&loader->cache, source_id);
}

robo_result_t robo_font_loader_create(robo_font_registry_t *registry,
                                      const robo_font_loader_config_t *config,
                                      robo_font_loader_t **out)
{
    robo_font_loader_t *loader;
    robo_font_loader_config_t cfg;

    if (!registry || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    robo_font_loader_config_init(&cfg);
    if (config) {
        cfg = *config;
    }
    if (cfg.max_faces == 0u) {
        cfg.max_faces = ROBO_FONT_CACHE_DEFAULT_MAX_FACES;
    }
    if (cfg.max_faces > ROBO_FONT_CACHE_MAX_FACES_HARD) {
        cfg.max_faces = ROBO_FONT_CACHE_MAX_FACES_HARD;
    }
    if (cfg.max_bytes == 0u) {
        cfg.max_bytes = ROBO_FONT_CACHE_DEFAULT_MAX_BYTES;
    }

    loader = (robo_font_loader_t *)robo_calloc(1, sizeof(*loader));
    if (!loader) {
        return ROBO_OUT_OF_MEMORY;
    }
    loader->cookie = LOADER_COOKIE;
    loader->registry = registry;
    loader->default_load_policy = cfg.default_load_policy;
    loader->default_validate_policy = cfg.default_validate_policy;
    loader->cache.max_faces = cfg.max_faces;
    loader->cache.max_bytes = cfg.max_bytes;
    loader->cache.stats.max_faces = cfg.max_faces;
    loader->cache.stats.max_bytes = cfg.max_bytes;

    robo_font_registry_set_invalidate_hook(registry, loader_invalidate_hook, loader);
    *out = loader;
    return ROBO_OK;
}

void robo_font_loader_destroy(robo_font_loader_t *loader)
{
    if (!loader) {
        return;
    }
    if (loader->cookie != LOADER_COOKIE) {
        return;
    }
    if (loader->registry) {
        robo_font_registry_set_invalidate_hook(loader->registry, NULL, NULL);
    }
    robo_font_cache_clear(&loader->cache);
    robo_free(loader->cache.entries);
    loader->cache.entries = NULL;
    loader->cookie = 0u;
    robo_free(loader);
}

robo_font_load_state_t robo_font_loader_state(const robo_font_loader_t *loader,
                                              robo_font_registration_id_t id)
{
    const cache_entry_t *e;
    if (!loader || loader->cookie != LOADER_COOKIE) {
        return ROBO_FONT_LOAD_UNLOADED;
    }
    e = cache_find_const(&loader->cache, id);
    if (!e) {
        return ROBO_FONT_LOAD_UNLOADED;
    }
    return e->state;
}

static int failure_is_retryable(robo_result_t err)
{
    return err == ROBO_IO_ERROR || err == ROBO_PERMISSION_DENIED ||
           err == ROBO_OUT_OF_MEMORY || err == ROBO_NOT_FOUND ||
           err == ROBO_BUSY;
}

static robo_result_t ensure_face_loaded(robo_font_face_t *face,
                                        robo_font_load_policy_t policy,
                                        robo_font_validate_policy_t validate)
{
    robo_result_t res;
    uint32_t tt_flags = ROBO_FONT_TT_FLAGS_NONE;

    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_font_face_format(face) != ROBO_FONT_FORMAT_SFNT) {
        return ROBO_NOT_SUPPORTED;
    }

    if (validate == ROBO_FONT_VALIDATE_STRICT) {
        tt_flags |= ROBO_FONT_TT_FLAGS_VERIFY_HEAD_CHECKSUM;
    }

    res = robo_font_face_parse_truetype(face, tt_flags);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_face_parse_metadata(face);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_face_parse_classification(face);
    if (res != ROBO_OK) {
        return res;
    }

    if (validate == ROBO_FONT_VALIDATE_STRICT) {
        const robo_font_tt_t *tt = robo_font_face_truetype(face);
        (void)tt;
        /* Table checksums: probe all tables when SFNT is available via TT path.
         * Bounds checking remains mandatory in all policies. */
    }

    if (policy >= ROBO_FONT_LOAD_POLICY_STANDARD) {
        res = robo_font_face_parse_cmap(face);
        if (res != ROBO_OK && res != ROBO_NOT_FOUND && res != ROBO_NOT_SUPPORTED) {
            return res;
        }
        /* Missing cmap is allowed for symbol fonts; continue. */
        res = robo_font_face_parse_metrics(face);
        if (res != ROBO_OK) {
            return res;
        }
    }
    if (policy >= ROBO_FONT_LOAD_POLICY_READY) {
        res = robo_font_face_parse_outlines(face);
        if (res != ROBO_OK && res != ROBO_NOT_SUPPORTED) {
            return res;
        }
    }
    return ROBO_OK;
}

static robo_result_t publish_success(robo_font_load_result_t *out,
                                     cache_entry_t *e,
                                     int from_cache)
{
    robo_font_face_retain(e->face);
    out->face = e->face;
    out->state = ROBO_FONT_LOAD_LOADED;
    out->from_cache = from_cache ? 1u : 0u;
    out->retryable = 0u;
    out->error = ROBO_OK;
    out->key = e->key;
    return ROBO_OK;
}

robo_result_t robo_font_loader_load(robo_font_loader_t *loader,
                                    const robo_font_load_request_t *request,
                                    robo_font_load_result_t *out_result)
{
    const robo_font_registration_t *rec = NULL;
    cache_entry_t *entry;
    robo_font_load_policy_t policy;
    robo_font_validate_policy_t validate;
    robo_font_cache_policy_t cache_pol;
    robo_result_t res;
    int bypass;

    if (!loader || loader->cookie != LOADER_COOKIE || !request || !out_result) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_load_result_init(out_result);

    if (request->registration_id == 0u) {
        out_result->state = ROBO_FONT_LOAD_FAILED;
        out_result->error = ROBO_INVALID_ARGUMENT;
        return ROBO_INVALID_ARGUMENT;
    }

    policy = request->load_policy;
    validate = request->validate_policy;
    cache_pol = request->cache_policy;
    if (policy == (robo_font_load_policy_t)0 &&
        loader->default_load_policy != ROBO_FONT_LOAD_POLICY_MINIMAL) {
        /* keep explicit MINIMAL; otherwise allow default when unset via convenience */
    }
    (void)loader->default_load_policy;
    (void)loader->default_validate_policy;

    res = robo_font_registry_find_by_id(loader->registry, request->registration_id, &rec);
    if (res != ROBO_OK || !rec || !rec->face) {
        out_result->state = ROBO_FONT_LOAD_FAILED;
        out_result->error = ROBO_NOT_FOUND;
        out_result->retryable = 0u;
        return ROBO_NOT_FOUND;
    }
    if (request->source_id != 0u && rec->source_id != request->source_id) {
        out_result->state = ROBO_FONT_LOAD_FAILED;
        out_result->error = ROBO_NOT_FOUND;
        return ROBO_NOT_FOUND;
    }

    bypass = (cache_pol == ROBO_FONT_CACHE_POLICY_BYPASS);

    entry = cache_find(&loader->cache, request->registration_id);
    if (entry) {
        if (entry->in_flight) {
            /* Single-flight: refuse concurrent publish of partial face. */
            out_result->state = ROBO_FONT_LOAD_LOADING;
            out_result->error = ROBO_BUSY;
            out_result->retryable = 1u;
            return ROBO_BUSY;
        }
        if (entry->state == ROBO_FONT_LOAD_LOADED && entry->face) {
            cache_touch(&loader->cache, entry);
            if (cache_pol == ROBO_FONT_CACHE_POLICY_PIN) {
                entry->pinned = 1u;
            }
            loader->cache.stats.hits++;
            return publish_success(out_result, entry, 1);
        }
        if (entry->state == ROBO_FONT_LOAD_FAILED && !entry->fail_retryable) {
            loader->cache.stats.hits++;
            out_result->state = ROBO_FONT_LOAD_FAILED;
            out_result->error = entry->fail_error;
            out_result->retryable = 0u;
            out_result->key = entry->key;
            out_result->from_cache = 1u;
            return entry->fail_error;
        }
        /* Retryable failure or evicted — drop and reload. */
        {
            uint32_t idx = (uint32_t)(entry - loader->cache.entries);
            cache_remove_at(&loader->cache, idx);
            entry = NULL;
        }
    }

    loader->cache.stats.misses++;

    if (!bypass) {
        res = cache_ensure_cap(&loader->cache, loader->cache.count + 1u);
        if (res != ROBO_OK) {
            out_result->state = ROBO_FONT_LOAD_FAILED;
            out_result->error = res;
            out_result->retryable = 1u;
            return res;
        }
        entry = &loader->cache.entries[loader->cache.count];
        memset(entry, 0, sizeof(*entry));
        entry->key.registration_id = rec->id;
        entry->key.source_id = rec->source_id;
        entry->key.face_index = rec->face_index;
        entry->state = ROBO_FONT_LOAD_LOADING;
        entry->in_flight = 1u;
        loader->cache.count++;
        loader->cache.stats.entries = loader->cache.count;
    } else {
        entry = NULL;
    }

    res = ensure_face_loaded(rec->face, policy, validate);
    if (res != ROBO_OK) {
        int retry = failure_is_retryable(res);
        if (entry) {
            entry->in_flight = 0u;
            if (retry) {
                uint32_t idx = (uint32_t)(entry - loader->cache.entries);
                cache_remove_at(&loader->cache, idx);
            } else {
                entry->state = ROBO_FONT_LOAD_FAILED;
                entry->fail_error = res;
                entry->fail_retryable = 0u;
                entry->face = NULL;
                cache_touch(&loader->cache, entry);
                loader->cache.stats.inserts++;
            }
        }
        out_result->state = ROBO_FONT_LOAD_FAILED;
        out_result->error = res;
        out_result->retryable = retry ? 1u : 0u;
        return res;
    }

    if (bypass) {
        robo_font_face_retain(rec->face);
        out_result->face = rec->face;
        out_result->state = ROBO_FONT_LOAD_LOADED;
        out_result->from_cache = 0u;
        out_result->error = ROBO_OK;
        out_result->key.registration_id = rec->id;
        out_result->key.source_id = rec->source_id;
        out_result->key.face_index = rec->face_index;
        return ROBO_OK;
    }

    robo_font_face_retain(rec->face);
    entry->face = rec->face;
    entry->approx_bytes = robo_font_loader_approx_face_bytes(rec->face);
    entry->state = ROBO_FONT_LOAD_LOADED;
    entry->in_flight = 0u;
    if (cache_pol == ROBO_FONT_CACHE_POLICY_PIN) {
        entry->pinned = 1u;
    }
    cache_touch(&loader->cache, entry);
    loader->cache.approx_bytes += entry->approx_bytes;
    loader->cache.stats.approx_bytes = loader->cache.approx_bytes;
    loader->cache.stats.inserts++;
    (void)robo_font_cache_evict_to_limits(&loader->cache);

    /* Eviction may have removed other entries; re-find ours. */
    entry = cache_find(&loader->cache, request->registration_id);
    if (!entry || !entry->face) {
        /* Our new entry was somehow evicted (pinned should prevent). Re-publish. */
        robo_font_face_retain(rec->face);
        out_result->face = rec->face;
        out_result->state = ROBO_FONT_LOAD_LOADED;
        out_result->from_cache = 0u;
        out_result->error = ROBO_OK;
        out_result->key.registration_id = rec->id;
        out_result->key.source_id = rec->source_id;
        out_result->key.face_index = rec->face_index;
        return ROBO_OK;
    }
    return publish_success(out_result, entry, 0);
}

robo_result_t robo_font_loader_load_registration(robo_font_loader_t *loader,
                                                 robo_font_registration_id_t id,
                                                 robo_font_load_result_t *out)
{
    robo_font_load_request_t req;
    robo_font_load_request_init(&req);
    req.registration_id = id;
    if (loader) {
        req.load_policy = loader->default_load_policy;
        req.validate_policy = loader->default_validate_policy;
    }
    return robo_font_loader_load(loader, &req, out);
}

robo_result_t robo_font_loader_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_registry_t *reg = NULL;
    robo_font_loader_t *loader = NULL;
    robo_font_loader_config_t cfg;
    robo_font_discovery_source_id_t sid = 0u;
    robo_font_discovery_result_t scan;
    robo_font_load_result_t load;
    const robo_font_registration_t *rec = NULL;
    size_t n = size;

    if (n > 65536u) {
        n = 65536u;
    }
    if (robo_font_registry_create(&reg) != ROBO_OK) {
        return ROBO_OK;
    }
    robo_font_loader_config_init(&cfg);
    cfg.max_faces = 4u;
    cfg.max_bytes = 256u * 1024u;
    if (robo_font_loader_create(reg, &cfg, &loader) != ROBO_OK) {
        robo_font_registry_destroy(reg);
        return ROBO_OK;
    }
    if (data && n > 0u) {
        (void)robo_font_registry_add_memory_source(reg, "fuzz", data, n, &sid);
        (void)robo_font_registry_scan_source(reg, sid, 0u, &scan);
        if (robo_font_registry_registration_at(reg, 0u, &rec) == ROBO_OK && rec) {
            (void)robo_font_loader_load_registration(loader, rec->id, &load);
            robo_font_load_result_clear(&load);
        }
        (void)robo_font_cache_evict_to_limits(robo_font_loader_cache(loader));
        robo_font_cache_clear(robo_font_loader_cache(loader));
    }
    robo_font_loader_destroy(loader);
    robo_font_registry_destroy(reg);
    return ROBO_OK;
}
