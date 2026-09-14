/*
 * RoboFontGenericFamily — semantic generic font families (Goal 5.16).
 *
 * Maps semantic categories (serif, sans-serif, system-ui, …) to configured
 * concrete family names. Does NOT guess from installed fonts, shape text,
 * select emoji presentation, or perform MATH layout.
 *
 *   Specific family → Generic family → Configured candidates
 *        → Selection (5.11) → Fallback (5.15)
 *
 * Precedence (first non-empty mapping wins):
 *   1. application config
 *   2. runtime config
 *   3. platform default config
 *   4. optional related-generic policy (ui-* → base), then unresolved
 */

#ifndef ROBO_FONT_GENERIC_FAMILY_H
#define ROBO_FONT_GENERIC_FAMILY_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFallback.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontRegistry.h"
#include "RoboFont/RoboFontSelection.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_FONT_GENERIC_MAX_FAMILIES_PER_MAPPING 64u
#define ROBO_FONT_GENERIC_MAX_FAMILY_BYTES 1024u
#define ROBO_FONT_GENERIC_MAX_MAPPINGS 256u
#define ROBO_FONT_GENERIC_CACHE_DEFAULT 128u
#define ROBO_FONT_GENERIC_CACHE_HARD 1024u

/* Family IDs live in RoboFontStyle.h (robo_font_generic_family_t). */

/** Which configuration layer supplied the winning mapping. */
typedef enum robo_font_generic_layer {
    ROBO_FONT_GENERIC_LAYER_NONE = 0,
    ROBO_FONT_GENERIC_LAYER_APPLICATION = 1,
    ROBO_FONT_GENERIC_LAYER_RUNTIME = 2,
    ROBO_FONT_GENERIC_LAYER_PLATFORM = 3,
    ROBO_FONT_GENERIC_LAYER_RELATED = 4
} robo_font_generic_layer_t;

typedef enum robo_font_generic_flags {
    ROBO_FONT_GENERIC_FLAGS_NONE = 0,
    /**
     * If the requested generic has no mapping, try a related base generic
     * (ui-serif→serif, ui-sans-serif→sans-serif, ui-monospace→monospace,
     * ui-rounded→sans-serif). system-ui / emoji / math never auto-relate.
     */
    ROBO_FONT_GENERIC_FLAG_ALLOW_RELATED = 1,
    /** Skip the bounded resolution cache. */
    ROBO_FONT_GENERIC_FLAG_NO_CACHE = 2,
    /**
     * When collecting faces / selecting, skip concrete families that have
     * zero registered faces (continue to next priority entry).
     */
    ROBO_FONT_GENERIC_FLAG_SKIP_MISSING = 4
} robo_font_generic_flags_t;

typedef struct robo_font_generic_config robo_font_generic_config_t;
typedef struct robo_font_generic_resolver robo_font_generic_resolver_t;

/**
 * Resolution request. Not a CSS type.
 * Optional language/script are soft filters for language-scoped mappings.
 */
typedef struct robo_font_generic_request {
    robo_font_generic_family_t generic;
    /** Optional BCP-47 language (borrowed). Soft — only matches configured
     * language-scoped mappings; never invents fonts. */
    const char *language;
    size_t language_len; /* 0 → strlen */
    uint8_t has_script;
    robo_unicode_script_t script; /* reserved foundation; unused for ranking */
    uint32_t flags; /* OR'd with resolver defaults */
} robo_font_generic_request_t;

/**
 * Resolved concrete family list (owned by result; clear releases).
 * Families are UTF-8 names in configured priority order.
 */
typedef struct robo_font_generic_result {
    uint32_t cookie;
    uint8_t resolved;
    uint8_t from_cache;
    uint8_t used_related;
    robo_font_generic_family_t requested;
    robo_font_generic_family_t effective; /* may differ if related used */
    robo_font_generic_layer_t layer;
    char **families; /* owned array of owned strings */
    uint32_t family_count;
    uint64_t config_generation;
    uint64_t registry_generation;
} robo_font_generic_result_t;

#define ROBO_FONT_GENERIC_RESULT_COOKIE 0x47454E31u /* 'GEN1' */

typedef struct robo_font_generic_cache_stats {
    uint64_t hits;
    uint64_t misses;
    uint64_t inserts;
    uint64_t invalidations;
    uint32_t capacity;
    uint32_t occupied;
} robo_font_generic_cache_stats_t;

typedef struct robo_font_generic_resolver_options {
    const robo_font_generic_config_t *platform;     /* optional retained */
    const robo_font_generic_config_t *runtime;      /* optional retained */
    const robo_font_generic_config_t *application;  /* optional retained */
    uint32_t flags; /* default resolver flags */
    uint32_t cache_capacity; /* 0 → default */
} robo_font_generic_resolver_options_t;

/* ---- name interchange ---- */

const char *robo_font_generic_family_name(robo_font_generic_family_t g);
robo_result_t robo_font_generic_family_from_name(const char *name,
                                                 size_t name_len,
                                                 robo_font_generic_family_t *out);
int robo_font_generic_family_is_valid(robo_font_generic_family_t g);

/** Related base for ui-* when ALLOW_RELATED; else UNKNOWN. */
robo_font_generic_family_t robo_font_generic_related(robo_font_generic_family_t g);

/* ---- immutable-style config (generation bumps on mutation) ---- */

robo_result_t robo_font_generic_config_create(robo_font_generic_config_t **out);
void robo_font_generic_config_retain(robo_font_generic_config_t *cfg);
void robo_font_generic_config_release(robo_font_generic_config_t *cfg);

uint64_t robo_font_generic_config_generation(const robo_font_generic_config_t *cfg);
uint32_t robo_font_generic_config_mapping_count(const robo_font_generic_config_t *cfg);

/**
 * Set/replace the default mapping for @p generic (language = NULL).
 * @p families may be empty to clear. Duplicates are kept in order (first wins
 * for presence, list remains deterministic). Invalid generic → error.
 */
robo_result_t robo_font_generic_config_set_mapping(
    robo_font_generic_config_t *cfg,
    robo_font_generic_family_t generic,
    const char *const *families,
    uint32_t family_count);

/**
 * Language-scoped mapping. When resolving with a matching language, this
 * overrides the default mapping for the same generic. Empty language clears.
 */
robo_result_t robo_font_generic_config_set_mapping_for_language(
    robo_font_generic_config_t *cfg,
    robo_font_generic_family_t generic,
    const char *language,
    size_t language_len,
    const char *const *families,
    uint32_t family_count);

/** Clear all mappings; bumps generation. */
robo_result_t robo_font_generic_config_clear(robo_font_generic_config_t *cfg);

/**
 * Inspect mapping. @p out_families borrows config storage (valid until next
 * mutation of this config). language NULL queries the default mapping.
 */
robo_result_t robo_font_generic_config_get_mapping(
    const robo_font_generic_config_t *cfg,
    robo_font_generic_family_t generic,
    const char *language,
    size_t language_len,
    const char *const **out_families,
    uint32_t *out_count);

/* ---- resolver ---- */

void robo_font_generic_resolver_options_init(
    robo_font_generic_resolver_options_t *opts);

robo_result_t robo_font_generic_resolver_create(
    robo_font_registry_t *registry,
    const robo_font_generic_resolver_options_t *opts,
    robo_font_generic_resolver_t **out);

void robo_font_generic_resolver_destroy(robo_font_generic_resolver_t *r);

/**
 * Atomic layer replacement: retains @p cfg (NULL clears layer).
 * Bumps resolver generation and clears the decision cache.
 */
robo_result_t robo_font_generic_resolver_set_platform(
    robo_font_generic_resolver_t *r,
    robo_font_generic_config_t *cfg);
robo_result_t robo_font_generic_resolver_set_runtime(
    robo_font_generic_resolver_t *r,
    robo_font_generic_config_t *cfg);
robo_result_t robo_font_generic_resolver_set_application(
    robo_font_generic_resolver_t *r,
    robo_font_generic_config_t *cfg);

uint64_t robo_font_generic_resolver_generation(
    const robo_font_generic_resolver_t *r);

void robo_font_generic_request_init(robo_font_generic_request_t *req);
robo_result_t robo_font_generic_request_validate(
    const robo_font_generic_request_t *req);

void robo_font_generic_result_init(robo_font_generic_result_t *out);
void robo_font_generic_result_clear(robo_font_generic_result_t *res);

/**
 * Resolve generic → ordered concrete family names.
 * Empty mapping → resolved=0 (does not invent a family).
 */
robo_result_t robo_font_generic_resolve(robo_font_generic_resolver_t *r,
                                        const robo_font_generic_request_t *req,
                                        robo_font_generic_result_t *out);

/**
 * Resolve then select via RoboFontSelector.
 * Walks configured families in order; first family with an eligible face wins.
 * @p style provides weight/stretch/style (family field ignored).
 */
robo_result_t robo_font_generic_resolve_select(
    robo_font_generic_resolver_t *r,
    const robo_font_generic_request_t *req,
    const robo_font_select_request_t *style,
    robo_font_generic_result_t *generic_out, /* optional */
    robo_font_selection_t *selection_out);

/**
 * Resolve + select primary, then run Goal 5.15 fallback with the resolved
 * family list as preferred_families. Requires a fallback resolver bound to
 * the same registry.
 */
robo_result_t robo_font_generic_resolve_fallback(
    robo_font_generic_resolver_t *r,
    robo_font_fallback_resolver_t *fallback,
    const robo_font_generic_request_t *req,
    const robo_font_select_request_t *style,
    const uint32_t *codepoints,
    uint32_t codepoint_count,
    robo_font_generic_result_t *generic_out, /* optional */
    robo_font_selection_t *selection_out,    /* optional primary selection */
    robo_font_fallback_result_t *fallback_out);

void robo_font_generic_resolver_cache_clear(robo_font_generic_resolver_t *r);
void robo_font_generic_resolver_cache_stats(
    const robo_font_generic_resolver_t *r,
    robo_font_generic_cache_stats_t *out);

/** Empty portable platform defaults (no hard-coded fonts). */
robo_result_t robo_font_generic_config_create_empty_platform(
    robo_font_generic_config_t **out);

/** Fuzz entry — never crashes. */
robo_result_t robo_font_generic_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_GENERIC_FAMILY_H */
