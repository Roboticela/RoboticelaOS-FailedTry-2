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
    robo_font_generic_family_t generic;
    char *language; /* NULL = default mapping */
    char **families;
    uint32_t family_count;
} mapping_t;

struct robo_font_generic_config {
    uint32_t cookie;
    uint32_t refs;
    uint64_t generation;
    mapping_t *entries;
    uint32_t entry_count;
    uint32_t entry_cap;
};

typedef struct {
    uint64_t resolver_generation;
    uint64_t registry_generation;
    uint64_t config_generation;
    robo_font_generic_family_t generic;
    uint32_t lang_hash;
    uint32_t flags;
    uint8_t resolved;
    uint8_t used_related;
    robo_font_generic_family_t effective;
    robo_font_generic_layer_t layer;
    uint32_t family_count;
    char families_blob[ROBO_FONT_GENERIC_MAX_FAMILY_BYTES];
    uint16_t family_offs[ROBO_FONT_GENERIC_MAX_FAMILIES_PER_MAPPING];
    uint64_t lru_tick;
} generic_cache_entry_t;

struct robo_font_generic_resolver {
    uint32_t cookie;
    robo_font_registry_t *registry;
    robo_font_generic_config_t *platform;
    robo_font_generic_config_t *runtime;
    robo_font_generic_config_t *application;
    uint32_t flags;
    uint64_t generation;
    generic_cache_entry_t *cache;
    uint32_t cache_cap;
    uint32_t cache_count;
    uint64_t lru_tick;
    robo_font_generic_cache_stats_t stats;
};

/* ---- helpers ---- */

static uint32_t hash_bytes(const void *p, size_t n)
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

static uint32_t hash_cstr(const char *s, size_t len)
{
    if (!s) {
        return 0u;
    }
    if (len == 0u) {
        len = strlen(s);
    }
    return hash_bytes(s, len);
}

static int utf8_casefold_eq(const char *a, size_t al, const char *b, size_t bl)
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
    if (robo_unicode_casefold_compare_utf8(a, al, b, bl, ROBO_UNICODE_REPLACE, &cmp) !=
        ROBO_OK) {
        return 0;
    }
    return cmp == 0;
}

static char *dup_n(const char *s, size_t n)
{
    char *o;
    if (!s) {
        return NULL;
    }
    if (n == 0u) {
        n = strlen(s);
    }
    o = (char *)robo_malloc(n + 1u);
    if (!o) {
        return NULL;
    }
    memcpy(o, s, n);
    o[n] = '\0';
    return o;
}

static void free_mapping(mapping_t *m)
{
    uint32_t i;
    if (!m) {
        return;
    }
    if (m->families) {
        for (i = 0u; i < m->family_count; i++) {
            robo_free(m->families[i]);
        }
        robo_free(m->families);
        m->families = NULL;
    }
    robo_free(m->language);
    m->language = NULL;
    m->family_count = 0u;
}

static void bump_config_gen(robo_font_generic_config_t *cfg)
{
    cfg->generation++;
    if (cfg->generation == 0u) {
        cfg->generation = 1u;
    }
}

/* ---- name table ---- */

static const char *const k_names[ROBO_FONT_GENERIC_COUNT] = {
    "none",           "serif",          "sans-serif",    "monospace",
    "cursive",        "fantasy",        "system-ui",     "emoji",
    "math",           "ui-serif",       "ui-sans-serif", "ui-monospace",
    "ui-rounded"};

const char *robo_font_generic_family_name(robo_font_generic_family_t g)
{
    if ((unsigned)g >= (unsigned)ROBO_FONT_GENERIC_COUNT) {
        return "none";
    }
    return k_names[g];
}

int robo_font_generic_family_is_valid(robo_font_generic_family_t g)
{
    return g > ROBO_FONT_GENERIC_NONE && g < ROBO_FONT_GENERIC_COUNT;
}

robo_result_t robo_font_generic_family_from_name(const char *name,
                                                 size_t name_len,
                                                 robo_font_generic_family_t *out)
{
    unsigned i;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = ROBO_FONT_GENERIC_UNKNOWN;
    if (!name) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (name_len == 0u) {
        name_len = strlen(name);
    }
    for (i = 1u; i < (unsigned)ROBO_FONT_GENERIC_COUNT; i++) {
        if (utf8_casefold_eq(name, name_len, k_names[i], 0u)) {
            *out = (robo_font_generic_family_t)i;
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

robo_font_generic_family_t robo_font_generic_related(robo_font_generic_family_t g)
{
    switch (g) {
    case ROBO_FONT_GENERIC_UI_SERIF:
        return ROBO_FONT_GENERIC_SERIF;
    case ROBO_FONT_GENERIC_UI_SANS_SERIF:
    case ROBO_FONT_GENERIC_UI_ROUNDED:
        return ROBO_FONT_GENERIC_SANS_SERIF;
    case ROBO_FONT_GENERIC_UI_MONOSPACE:
        return ROBO_FONT_GENERIC_MONOSPACE;
    default:
        return ROBO_FONT_GENERIC_UNKNOWN;
    }
}

/* ---- config ---- */

robo_result_t robo_font_generic_config_create(robo_font_generic_config_t **out)
{
    robo_font_generic_config_t *c;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    c = (robo_font_generic_config_t *)robo_calloc(1, sizeof(*c));
    if (!c) {
        return ROBO_OUT_OF_MEMORY;
    }
    c->cookie = CONFIG_COOKIE;
    c->refs = 1u;
    c->generation = 1u;
    *out = c;
    return ROBO_OK;
}

robo_result_t robo_font_generic_config_create_empty_platform(
    robo_font_generic_config_t **out)
{
    return robo_font_generic_config_create(out);
}

void robo_font_generic_config_retain(robo_font_generic_config_t *cfg)
{
    if (!cfg || cfg->cookie != CONFIG_COOKIE) {
        return;
    }
    cfg->refs++;
}

void robo_font_generic_config_release(robo_font_generic_config_t *cfg)
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
    for (i = 0u; i < cfg->entry_count; i++) {
        free_mapping(&cfg->entries[i]);
    }
    robo_free(cfg->entries);
    cfg->cookie = 0u;
    robo_free(cfg);
}

uint64_t robo_font_generic_config_generation(const robo_font_generic_config_t *cfg)
{
    return cfg && cfg->cookie == CONFIG_COOKIE ? cfg->generation : 0u;
}

uint32_t robo_font_generic_config_mapping_count(const robo_font_generic_config_t *cfg)
{
    return cfg && cfg->cookie == CONFIG_COOKIE ? cfg->entry_count : 0u;
}

static int find_entry(const robo_font_generic_config_t *cfg,
                      robo_font_generic_family_t g,
                      const char *lang,
                      size_t lang_len,
                      uint32_t *out_index)
{
    uint32_t i;
    int want_lang = lang && (lang_len > 0u || lang[0] != '\0');
    if (want_lang && lang_len == 0u) {
        lang_len = strlen(lang);
    }
    for (i = 0u; i < cfg->entry_count; i++) {
        const mapping_t *m = &cfg->entries[i];
        if (m->generic != g) {
            continue;
        }
        if (!want_lang) {
            if (!m->language) {
                if (out_index) {
                    *out_index = i;
                }
                return 1;
            }
        } else if (m->language &&
                   utf8_casefold_eq(m->language, 0u, lang, lang_len)) {
            if (out_index) {
                *out_index = i;
            }
            return 1;
        }
    }
    return 0;
}

static robo_result_t ensure_entry_cap(robo_font_generic_config_t *cfg, uint32_t need)
{
    mapping_t *n;
    uint32_t cap;
    if (need <= cfg->entry_cap) {
        return ROBO_OK;
    }
    if (need > ROBO_FONT_GENERIC_MAX_MAPPINGS) {
        return ROBO_INVALID_ARGUMENT;
    }
    cap = cfg->entry_cap ? cfg->entry_cap * 2u : 8u;
    if (cap < need) {
        cap = need;
    }
    if (cap > ROBO_FONT_GENERIC_MAX_MAPPINGS) {
        cap = ROBO_FONT_GENERIC_MAX_MAPPINGS;
    }
    n = (mapping_t *)robo_realloc(cfg->entries, (size_t)cap * sizeof(mapping_t));
    if (!n) {
        return ROBO_OUT_OF_MEMORY;
    }
    cfg->entries = n;
    cfg->entry_cap = cap;
    return ROBO_OK;
}

static robo_result_t set_mapping_inner(robo_font_generic_config_t *cfg,
                                       robo_font_generic_family_t generic,
                                       const char *language,
                                       size_t language_len,
                                       const char *const *families,
                                       uint32_t family_count)
{
    uint32_t idx = 0u;
    mapping_t *m;
    char **owned = NULL;
    uint32_t i;
    robo_result_t res;

    if (!cfg || cfg->cookie != CONFIG_COOKIE) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_font_generic_family_is_valid(generic)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (family_count > ROBO_FONT_GENERIC_MAX_FAMILIES_PER_MAPPING) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (family_count != 0u && !families) {
        return ROBO_INVALID_ARGUMENT;
    }

    if (family_count > 0u) {
        owned = (char **)robo_calloc(family_count, sizeof(char *));
        if (!owned) {
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < family_count; i++) {
            size_t n;
            if (!families[i]) {
                res = ROBO_INVALID_ARGUMENT;
                goto fail_owned;
            }
            n = strlen(families[i]);
            if (n == 0u || n >= ROBO_FONT_GENERIC_MAX_FAMILY_BYTES) {
                res = ROBO_INVALID_ARGUMENT;
                goto fail_owned;
            }
            owned[i] = dup_n(families[i], n);
            if (!owned[i]) {
                res = ROBO_OUT_OF_MEMORY;
                goto fail_owned;
            }
        }
    }

    if (find_entry(cfg, generic, language, language_len, &idx)) {
        m = &cfg->entries[idx];
        free_mapping(m);
        m->generic = generic;
        if (language && (language_len > 0u || language[0])) {
            if (language_len == 0u) {
                language_len = strlen(language);
            }
            m->language = dup_n(language, language_len);
            if (!m->language) {
                res = ROBO_OUT_OF_MEMORY;
                goto fail_owned;
            }
        }
        m->families = owned;
        m->family_count = family_count;
        bump_config_gen(cfg);
        return ROBO_OK;
    }

    if (family_count == 0u && !(language && language[0])) {
        /* Clearing a missing default mapping is a no-op success. */
        if (owned) {
            robo_free(owned);
        }
        bump_config_gen(cfg);
        return ROBO_OK;
    }

    res = ensure_entry_cap(cfg, cfg->entry_count + 1u);
    if (res != ROBO_OK) {
        goto fail_owned;
    }
    m = &cfg->entries[cfg->entry_count];
    memset(m, 0, sizeof(*m));
    m->generic = generic;
    if (language && (language_len > 0u || language[0])) {
        if (language_len == 0u) {
            language_len = strlen(language);
        }
        m->language = dup_n(language, language_len);
        if (!m->language) {
            res = ROBO_OUT_OF_MEMORY;
            goto fail_owned;
        }
    }
    m->families = owned;
    m->family_count = family_count;
    cfg->entry_count++;
    bump_config_gen(cfg);
    return ROBO_OK;

fail_owned:
    if (owned) {
        for (i = 0u; i < family_count; i++) {
            robo_free(owned[i]);
        }
        robo_free(owned);
    }
    return res;
}

robo_result_t robo_font_generic_config_set_mapping(
    robo_font_generic_config_t *cfg,
    robo_font_generic_family_t generic,
    const char *const *families,
    uint32_t family_count)
{
    return set_mapping_inner(cfg, generic, NULL, 0u, families, family_count);
}

robo_result_t robo_font_generic_config_set_mapping_for_language(
    robo_font_generic_config_t *cfg,
    robo_font_generic_family_t generic,
    const char *language,
    size_t language_len,
    const char *const *families,
    uint32_t family_count)
{
    if (!language || (language_len == 0u && language[0] == '\0')) {
        return ROBO_INVALID_ARGUMENT;
    }
    return set_mapping_inner(cfg, generic, language, language_len, families,
                             family_count);
}

robo_result_t robo_font_generic_config_clear(robo_font_generic_config_t *cfg)
{
    uint32_t i;
    if (!cfg || cfg->cookie != CONFIG_COOKIE) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < cfg->entry_count; i++) {
        free_mapping(&cfg->entries[i]);
    }
    cfg->entry_count = 0u;
    bump_config_gen(cfg);
    return ROBO_OK;
}

robo_result_t robo_font_generic_config_get_mapping(
    const robo_font_generic_config_t *cfg,
    robo_font_generic_family_t generic,
    const char *language,
    size_t language_len,
    const char *const **out_families,
    uint32_t *out_count)
{
    uint32_t idx = 0u;
    if (!cfg || cfg->cookie != CONFIG_COOKIE || !out_families || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_families = NULL;
    *out_count = 0u;
    if (!robo_font_generic_family_is_valid(generic)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!find_entry(cfg, generic, language, language_len, &idx)) {
        return ROBO_NOT_FOUND;
    }
    *out_families = (const char *const *)cfg->entries[idx].families;
    *out_count = cfg->entries[idx].family_count;
    return ROBO_OK;
}

/* ---- lookup across layers ---- */

static const mapping_t *lookup_in_config(const robo_font_generic_config_t *cfg,
                                         robo_font_generic_family_t g,
                                         const char *lang,
                                         size_t lang_len)
{
    uint32_t idx = 0u;
    if (!cfg) {
        return NULL;
    }
    if (lang && (lang_len > 0u || lang[0])) {
        if (find_entry(cfg, g, lang, lang_len, &idx)) {
            return &cfg->entries[idx];
        }
    }
    if (find_entry(cfg, g, NULL, 0u, &idx)) {
        return &cfg->entries[idx];
    }
    return NULL;
}

static void combined_config_generation(const robo_font_generic_resolver_t *r,
                                       uint64_t *out_gen)
{
    uint64_t g = r->generation;
    if (r->application) {
        g ^= robo_font_generic_config_generation(r->application) * 0x9E3779B97u;
    }
    if (r->runtime) {
        g ^= robo_font_generic_config_generation(r->runtime) * 0xC2B2AE3Du;
    }
    if (r->platform) {
        g ^= robo_font_generic_config_generation(r->platform) * 0x165667B1u;
    }
    *out_gen = g;
}

static int pick_mapping(const robo_font_generic_resolver_t *r,
                        robo_font_generic_family_t g,
                        const char *lang,
                        size_t lang_len,
                        const mapping_t **out_map,
                        robo_font_generic_layer_t *out_layer)
{
    const mapping_t *m;
    m = lookup_in_config(r->application, g, lang, lang_len);
    if (m && m->family_count > 0u) {
        *out_map = m;
        *out_layer = ROBO_FONT_GENERIC_LAYER_APPLICATION;
        return 1;
    }
    m = lookup_in_config(r->runtime, g, lang, lang_len);
    if (m && m->family_count > 0u) {
        *out_map = m;
        *out_layer = ROBO_FONT_GENERIC_LAYER_RUNTIME;
        return 1;
    }
    m = lookup_in_config(r->platform, g, lang, lang_len);
    if (m && m->family_count > 0u) {
        *out_map = m;
        *out_layer = ROBO_FONT_GENERIC_LAYER_PLATFORM;
        return 1;
    }
    *out_map = NULL;
    *out_layer = ROBO_FONT_GENERIC_LAYER_NONE;
    return 0;
}

/* ---- result ---- */

void robo_font_generic_result_init(robo_font_generic_result_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->cookie = ROBO_FONT_GENERIC_RESULT_COOKIE;
    out->requested = ROBO_FONT_GENERIC_UNKNOWN;
    out->effective = ROBO_FONT_GENERIC_UNKNOWN;
    out->layer = ROBO_FONT_GENERIC_LAYER_NONE;
}

void robo_font_generic_result_clear(robo_font_generic_result_t *res)
{
    uint32_t i;
    if (!res) {
        return;
    }
    if (res->cookie == ROBO_FONT_GENERIC_RESULT_COOKIE && res->families) {
        for (i = 0u; i < res->family_count; i++) {
            robo_free(res->families[i]);
        }
        robo_free(res->families);
    }
    robo_font_generic_result_init(res);
}

static robo_result_t fill_result_from_mapping(robo_font_generic_result_t *out,
                                              const mapping_t *m,
                                              robo_font_generic_family_t requested,
                                              robo_font_generic_family_t effective,
                                              robo_font_generic_layer_t layer,
                                              uint8_t used_related,
                                              uint64_t cfg_gen,
                                              uint64_t reg_gen)
{
    uint32_t i;
    char **arr;
    if (!m || m->family_count == 0u) {
        out->resolved = 0;
        out->requested = requested;
        out->effective = effective;
        out->layer = ROBO_FONT_GENERIC_LAYER_NONE;
        out->used_related = used_related;
        out->config_generation = cfg_gen;
        out->registry_generation = reg_gen;
        return ROBO_OK;
    }
    arr = (char **)robo_calloc(m->family_count, sizeof(char *));
    if (!arr) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < m->family_count; i++) {
        arr[i] = dup_n(m->families[i], 0u);
        if (!arr[i]) {
            uint32_t j;
            for (j = 0u; j < i; j++) {
                robo_free(arr[j]);
            }
            robo_free(arr);
            return ROBO_OUT_OF_MEMORY;
        }
    }
    out->resolved = 1;
    out->requested = requested;
    out->effective = effective;
    out->layer = layer;
    out->used_related = used_related;
    out->families = arr;
    out->family_count = m->family_count;
    out->config_generation = cfg_gen;
    out->registry_generation = reg_gen;
    return ROBO_OK;
}

/* ---- cache ---- */

void robo_font_generic_resolver_cache_clear(robo_font_generic_resolver_t *r)
{
    uint32_t i;
    if (!r || r->cookie != RESOLVER_COOKIE || !r->cache) {
        return;
    }
    for (i = 0u; i < r->cache_cap; i++) {
        memset(&r->cache[i], 0, sizeof(r->cache[i]));
    }
    r->cache_count = 0u;
    r->stats.invalidations++;
    r->stats.occupied = 0u;
}

void robo_font_generic_resolver_cache_stats(const robo_font_generic_resolver_t *r,
                                            robo_font_generic_cache_stats_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return;
    }
    *out = r->stats;
    out->capacity = r->cache_cap;
    out->occupied = r->cache_count;
}

static void cache_store(robo_font_generic_resolver_t *r,
                        uint64_t cfg_gen,
                        uint64_t reg_gen,
                        const robo_font_generic_request_t *req,
                        const robo_font_generic_result_t *res)
{
    generic_cache_entry_t *slot = NULL;
    uint32_t i;
    uint64_t oldest = UINT64_MAX;
    size_t blob = 0u;
    uint32_t n;

    if (!r->cache || r->cache_cap == 0u || !res->resolved) {
        return;
    }
    n = res->family_count;
    if (n > ROBO_FONT_GENERIC_MAX_FAMILIES_PER_MAPPING) {
        n = ROBO_FONT_GENERIC_MAX_FAMILIES_PER_MAPPING;
    }
    for (i = 0u; i < n; i++) {
        size_t len = strlen(res->families[i]) + 1u;
        if (blob + len > ROBO_FONT_GENERIC_MAX_FAMILY_BYTES) {
            return; /* too large to cache */
        }
        blob += len;
    }

    for (i = 0u; i < r->cache_cap; i++) {
        if (r->cache[i].family_count == 0u && !r->cache[i].resolved) {
            slot = &r->cache[i];
            break;
        }
        if (r->cache[i].lru_tick < oldest) {
            oldest = r->cache[i].lru_tick;
            slot = &r->cache[i];
        }
    }
    if (!slot) {
        return;
    }
    if (slot->family_count == 0u && !slot->resolved) {
        r->cache_count++;
    }
    memset(slot, 0, sizeof(*slot));
    slot->resolver_generation = r->generation;
    slot->registry_generation = reg_gen;
    slot->config_generation = cfg_gen;
    slot->generic = req->generic;
    slot->lang_hash = hash_cstr(req->language, req->language_len);
    slot->flags = req->flags | r->flags;
    slot->resolved = res->resolved;
    slot->used_related = res->used_related;
    slot->effective = res->effective;
    slot->layer = res->layer;
    slot->family_count = n;
    blob = 0u;
    for (i = 0u; i < n; i++) {
        size_t len = strlen(res->families[i]) + 1u;
        slot->family_offs[i] = (uint16_t)blob;
        memcpy(slot->families_blob + blob, res->families[i], len);
        blob += len;
    }
    slot->lru_tick = ++r->lru_tick;
    r->stats.inserts++;
    r->stats.occupied = r->cache_count;
}

static generic_cache_entry_t *cache_lookup(robo_font_generic_resolver_t *r,
                                           uint64_t cfg_gen,
                                           uint64_t reg_gen,
                                           const robo_font_generic_request_t *req)
{
    uint32_t i;
    uint32_t lang_hash = hash_cstr(req->language, req->language_len);
    uint32_t flags = req->flags | r->flags;
    if (!r->cache) {
        return NULL;
    }
    for (i = 0u; i < r->cache_cap; i++) {
        generic_cache_entry_t *e = &r->cache[i];
        if (!e->resolved && e->family_count == 0u) {
            continue;
        }
        if (e->resolver_generation == r->generation &&
            e->config_generation == cfg_gen && e->registry_generation == reg_gen &&
            e->generic == req->generic && e->lang_hash == lang_hash &&
            e->flags == flags) {
            e->lru_tick = ++r->lru_tick;
            return e;
        }
    }
    return NULL;
}

static robo_result_t result_from_cache(robo_font_generic_result_t *out,
                                       const generic_cache_entry_t *e,
                                       robo_font_generic_family_t requested)
{
    uint32_t i;
    char **arr;
    if (!e->resolved) {
        out->resolved = 0;
        out->from_cache = 1;
        out->requested = requested;
        out->effective = e->effective;
        out->layer = e->layer;
        out->used_related = e->used_related;
        out->config_generation = e->config_generation;
        out->registry_generation = e->registry_generation;
        return ROBO_OK;
    }
    arr = (char **)robo_calloc(e->family_count, sizeof(char *));
    if (!arr) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < e->family_count; i++) {
        const char *s = e->families_blob + e->family_offs[i];
        arr[i] = dup_n(s, 0u);
        if (!arr[i]) {
            uint32_t j;
            for (j = 0u; j < i; j++) {
                robo_free(arr[j]);
            }
            robo_free(arr);
            return ROBO_OUT_OF_MEMORY;
        }
    }
    out->resolved = 1;
    out->from_cache = 1;
    out->used_related = e->used_related;
    out->requested = requested;
    out->effective = e->effective;
    out->layer = e->layer;
    out->families = arr;
    out->family_count = e->family_count;
    out->config_generation = e->config_generation;
    out->registry_generation = e->registry_generation;
    return ROBO_OK;
}

/* ---- resolver ---- */

void robo_font_generic_resolver_options_init(
    robo_font_generic_resolver_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->flags = ROBO_FONT_GENERIC_FLAG_SKIP_MISSING;
    opts->cache_capacity = ROBO_FONT_GENERIC_CACHE_DEFAULT;
}

robo_result_t robo_font_generic_resolver_create(
    robo_font_registry_t *registry,
    const robo_font_generic_resolver_options_t *opts,
    robo_font_generic_resolver_t **out)
{
    robo_font_generic_resolver_t *r;
    robo_font_generic_resolver_options_t local;
    uint32_t cap;

    if (!registry || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (!opts) {
        robo_font_generic_resolver_options_init(&local);
        opts = &local;
    }
    cap = opts->cache_capacity ? opts->cache_capacity : ROBO_FONT_GENERIC_CACHE_DEFAULT;
    if (cap > ROBO_FONT_GENERIC_CACHE_HARD) {
        cap = ROBO_FONT_GENERIC_CACHE_HARD;
    }

    r = (robo_font_generic_resolver_t *)robo_calloc(1, sizeof(*r));
    if (!r) {
        return ROBO_OUT_OF_MEMORY;
    }
    r->cookie = RESOLVER_COOKIE;
    r->registry = registry;
    r->flags = opts->flags;
    r->generation = 1u;
    r->cache_cap = cap;
    if (cap > 0u) {
        r->cache = (generic_cache_entry_t *)robo_calloc(cap, sizeof(*r->cache));
        if (!r->cache) {
            robo_free(r);
            return ROBO_OUT_OF_MEMORY;
        }
    }
    r->stats.capacity = cap;

    if (opts->platform) {
        robo_font_generic_config_retain((robo_font_generic_config_t *)opts->platform);
        r->platform = (robo_font_generic_config_t *)opts->platform;
    }
    if (opts->runtime) {
        robo_font_generic_config_retain((robo_font_generic_config_t *)opts->runtime);
        r->runtime = (robo_font_generic_config_t *)opts->runtime;
    }
    if (opts->application) {
        robo_font_generic_config_retain((robo_font_generic_config_t *)opts->application);
        r->application = (robo_font_generic_config_t *)opts->application;
    }

    *out = r;
    return ROBO_OK;
}

void robo_font_generic_resolver_destroy(robo_font_generic_resolver_t *r)
{
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return;
    }
    if (r->platform) {
        robo_font_generic_config_release(r->platform);
    }
    if (r->runtime) {
        robo_font_generic_config_release(r->runtime);
    }
    if (r->application) {
        robo_font_generic_config_release(r->application);
    }
    robo_free(r->cache);
    r->cookie = 0u;
    robo_free(r);
}

static robo_result_t set_layer(robo_font_generic_resolver_t *r,
                               robo_font_generic_config_t **slot,
                               robo_font_generic_config_t *cfg)
{
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (cfg && cfg->cookie != CONFIG_COOKIE) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (*slot) {
        robo_font_generic_config_release(*slot);
        *slot = NULL;
    }
    if (cfg) {
        robo_font_generic_config_retain(cfg);
        *slot = cfg;
    }
    r->generation++;
    if (r->generation == 0u) {
        r->generation = 1u;
    }
    robo_font_generic_resolver_cache_clear(r);
    return ROBO_OK;
}

robo_result_t robo_font_generic_resolver_set_platform(
    robo_font_generic_resolver_t *r,
    robo_font_generic_config_t *cfg)
{
    return set_layer(r, &r->platform, cfg);
}

robo_result_t robo_font_generic_resolver_set_runtime(
    robo_font_generic_resolver_t *r,
    robo_font_generic_config_t *cfg)
{
    return set_layer(r, &r->runtime, cfg);
}

robo_result_t robo_font_generic_resolver_set_application(
    robo_font_generic_resolver_t *r,
    robo_font_generic_config_t *cfg)
{
    return set_layer(r, &r->application, cfg);
}

uint64_t robo_font_generic_resolver_generation(
    const robo_font_generic_resolver_t *r)
{
    return r && r->cookie == RESOLVER_COOKIE ? r->generation : 0u;
}

void robo_font_generic_request_init(robo_font_generic_request_t *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
    req->generic = ROBO_FONT_GENERIC_UNKNOWN;
}

robo_result_t robo_font_generic_request_validate(
    const robo_font_generic_request_t *req)
{
    if (!req) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_font_generic_family_is_valid(req->generic)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

robo_result_t robo_font_generic_resolve(robo_font_generic_resolver_t *r,
                                        const robo_font_generic_request_t *req,
                                        robo_font_generic_result_t *out)
{
    const mapping_t *m = NULL;
    robo_font_generic_layer_t layer = ROBO_FONT_GENERIC_LAYER_NONE;
    robo_font_generic_family_t effective;
    uint8_t used_related = 0;
    uint64_t cfg_gen = 0u;
    uint64_t reg_gen = 0u;
    uint32_t flags;
    int use_cache;
    generic_cache_entry_t *hit;
    robo_result_t res;

    if (!r || r->cookie != RESOLVER_COOKIE || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_font_generic_request_validate(req);
    if (res != ROBO_OK) {
        return res;
    }

    if (out->cookie == ROBO_FONT_GENERIC_RESULT_COOKIE) {
        robo_font_generic_result_clear(out);
    } else {
        robo_font_generic_result_init(out);
    }

    flags = req->flags | r->flags;
    use_cache = (flags & ROBO_FONT_GENERIC_FLAG_NO_CACHE) == 0 && r->cache_cap > 0u;
    combined_config_generation(r, &cfg_gen);
    reg_gen = robo_font_registry_generation(r->registry);

    if (use_cache) {
        hit = cache_lookup(r, cfg_gen, reg_gen, req);
        if (hit) {
            r->stats.hits++;
            return result_from_cache(out, hit, req->generic);
        }
        r->stats.misses++;
    }

    effective = req->generic;
    if (!pick_mapping(r, effective, req->language, req->language_len, &m, &layer)) {
        if ((flags & ROBO_FONT_GENERIC_FLAG_ALLOW_RELATED) != 0u) {
            robo_font_generic_family_t rel = robo_font_generic_related(req->generic);
            if (rel != ROBO_FONT_GENERIC_UNKNOWN &&
                pick_mapping(r, rel, req->language, req->language_len, &m, &layer)) {
                effective = rel;
                used_related = 1;
                layer = ROBO_FONT_GENERIC_LAYER_RELATED;
            }
        }
    }

    if (!m || m->family_count == 0u) {
        out->resolved = 0;
        out->requested = req->generic;
        out->effective = req->generic;
        out->layer = ROBO_FONT_GENERIC_LAYER_NONE;
        out->config_generation = cfg_gen;
        out->registry_generation = reg_gen;
        if (use_cache) {
            /* Cache unresolved briefly via store path only for resolved —
             * still ok to skip. */
        }
        return ROBO_OK;
    }

    res = fill_result_from_mapping(out, m, req->generic, effective, layer,
                                   used_related, cfg_gen, reg_gen);
    if (res != ROBO_OK) {
        return res;
    }
    if (use_cache) {
        cache_store(r, cfg_gen, reg_gen, req, out);
    }
    return ROBO_OK;
}

robo_result_t robo_font_generic_resolve_select(
    robo_font_generic_resolver_t *r,
    const robo_font_generic_request_t *req,
    const robo_font_select_request_t *style,
    robo_font_generic_result_t *generic_out,
    robo_font_selection_t *selection_out)
{
    robo_font_generic_result_t local;
    robo_font_generic_result_t *gres;
    robo_font_select_request_t sreq;
    uint32_t i;
    uint32_t flags;
    robo_result_t res;

    if (!r || !req || !style || !selection_out) {
        return ROBO_INVALID_ARGUMENT;
    }

    gres = generic_out ? generic_out : &local;
    if (!generic_out) {
        robo_font_generic_result_init(&local);
    }

    res = robo_font_generic_resolve(r, req, gres);
    if (res != ROBO_OK) {
        if (!generic_out) {
            robo_font_generic_result_clear(&local);
        }
        return res;
    }

    if (selection_out->cookie == ROBO_FONT_SELECTION_COOKIE) {
        robo_font_selection_clear(selection_out);
    } else {
        robo_font_selection_init(selection_out);
    }

    if (!gres->resolved || gres->family_count == 0u) {
        if (!generic_out) {
            robo_font_generic_result_clear(&local);
        }
        return ROBO_OK;
    }

    flags = req->flags | r->flags;
    sreq = *style;

    for (i = 0u; i < gres->family_count; i++) {
        robo_font_face_t **faces = NULL;
        uint32_t face_n = 0u;
        robo_font_selection_t sel;

        res = robo_font_registry_collect_faces_for_family(
            r->registry, gres->families[i], &faces, &face_n);
        if (res != ROBO_OK) {
            if (!generic_out) {
                robo_font_generic_result_clear(&local);
            }
            return res;
        }
        if (face_n == 0u) {
            robo_free(faces);
            if ((flags & ROBO_FONT_GENERIC_FLAG_SKIP_MISSING) != 0u) {
                continue;
            }
            /* No faces and not skipping — still try select (will miss). */
        }

        sreq.family = gres->families[i];
        sreq.family_len = 0u;
        robo_font_selection_init(&sel);
        res = robo_font_select(&sreq, faces, face_n, &sel);
        robo_free(faces);
        if (res != ROBO_OK) {
            robo_font_selection_clear(&sel);
            if (!generic_out) {
                robo_font_generic_result_clear(&local);
            }
            return res;
        }
        if (sel.found) {
            *selection_out = sel;
            /* ownership transferred */
            if (!generic_out) {
                robo_font_generic_result_clear(&local);
            }
            return ROBO_OK;
        }
        robo_font_selection_clear(&sel);
    }

    if (!generic_out) {
        robo_font_generic_result_clear(&local);
    }
    return ROBO_OK;
}

robo_result_t robo_font_generic_resolve_fallback(
    robo_font_generic_resolver_t *r,
    robo_font_fallback_resolver_t *fallback,
    const robo_font_generic_request_t *req,
    const robo_font_select_request_t *style,
    const uint32_t *codepoints,
    uint32_t codepoint_count,
    robo_font_generic_result_t *generic_out,
    robo_font_selection_t *selection_out,
    robo_font_fallback_result_t *fallback_out)
{
    robo_font_generic_result_t local_g;
    robo_font_selection_t local_s;
    robo_font_generic_result_t *gres;
    robo_font_selection_t *sel;
    robo_font_fallback_request_t freq;
    robo_result_t res;
    int own_g = 0;
    int own_s = 0;

    if (!r || !fallback || !req || !style || !codepoints || codepoint_count == 0u ||
        !fallback_out) {
        return ROBO_INVALID_ARGUMENT;
    }

    gres = generic_out ? generic_out : &local_g;
    sel = selection_out ? selection_out : &local_s;
    if (!generic_out) {
        robo_font_generic_result_init(&local_g);
        own_g = 1;
    }
    if (!selection_out) {
        robo_font_selection_init(&local_s);
        own_s = 1;
    }

    res = robo_font_generic_resolve_select(r, req, style, gres, sel);
    if (res != ROBO_OK) {
        goto done;
    }
    if (!sel->found || !sel->face) {
        /* No primary from generic — explicit unresolved for fallback path. */
        if (fallback_out->cookie == ROBO_FONT_FALLBACK_RESULT_COOKIE) {
            robo_font_fallback_result_clear(fallback_out);
        } else {
            robo_font_fallback_result_init(fallback_out);
        }
        res = ROBO_OK;
        goto done;
    }

    robo_font_fallback_request_init(&freq);
    freq.primary = sel->face;
    freq.codepoints = codepoints;
    freq.codepoint_count = codepoint_count;
    freq.weight = style->weight;
    freq.stretch = style->stretch;
    freq.style = style->style;
    freq.has_oblique_angle = style->has_oblique_angle;
    freq.oblique_angle_millidegrees = style->oblique_angle_millidegrees;
    freq.language = req->language;
    freq.language_len = req->language_len;
    freq.has_script = req->has_script;
    freq.script = req->script;
    if (gres->resolved && gres->family_count > 0u) {
        freq.preferred_families = (const char *const *)gres->families;
        freq.preferred_family_count = gres->family_count;
    }

    res = robo_font_fallback_resolve(fallback, &freq, fallback_out);

done:
    if (own_g) {
        robo_font_generic_result_clear(&local_g);
    }
    if (own_s) {
        robo_font_selection_clear(&local_s);
    }
    return res;
}

robo_result_t robo_font_generic_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_generic_config_t *cfg = NULL;
    robo_font_generic_family_t g = ROBO_FONT_GENERIC_UNKNOWN;
    const char *safe_name = "FuzzFamily";
    const char *names[1];
    char buf[64];
    size_t i;

    if (!data || size == 0u) {
        return ROBO_OK;
    }

    names[0] = safe_name;

    (void)robo_font_generic_config_create(&cfg);
    if (!cfg) {
        return ROBO_OK;
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
        (void)robo_font_generic_family_from_name(buf, take, &g);
        if (robo_font_generic_family_is_valid(g)) {
            names[0] = buf;
            (void)robo_font_generic_config_set_mapping(cfg, g, names, 1u);
        }
        /* Pathological large count — must reject without reading past names[]. */
        (void)robo_font_generic_config_set_mapping(
            cfg, ROBO_FONT_GENERIC_SANS_SERIF, names,
            ROBO_FONT_GENERIC_MAX_FAMILIES_PER_MAPPING + 1u);
        (void)robo_font_generic_related((robo_font_generic_family_t)(data[0] % 16u));
        (void)robo_font_generic_family_name((robo_font_generic_family_t)(data[0] % 16u));
        names[0] = safe_name;
        (void)robo_font_generic_config_set_mapping_for_language(
            cfg, ROBO_FONT_GENERIC_SERIF, "xx", 2u, names, 1u);
        /* Unusual Unicode family name — bounded length, must not crash. */
        {
            static const char unicode_name[] = "\xD9\x81\xD9\x88\xD9\x86\xD8\xAA";
            const char *u[1];
            u[0] = unicode_name;
            (void)robo_font_generic_config_set_mapping(
                cfg, ROBO_FONT_GENERIC_CURSIVE, u, 1u);
        }
    }
    (void)robo_font_generic_config_clear(cfg);
    robo_font_generic_config_release(cfg);
    return ROBO_OK;
}
