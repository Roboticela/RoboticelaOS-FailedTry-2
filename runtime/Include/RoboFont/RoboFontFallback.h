/*
 * RoboFontFallback — coverage-first font fallback (Goal 5.15).
 *
 * Chooses an alternative registered face when the primary lacks usable
 * glyphs for requested Unicode scalars. Does NOT shape, substitute,
 * position, or render text.
 *
 *   Discovery → Registry → Selection → Coverage → Fallback → (later) Shaping
 */

#ifndef ROBO_FONT_FALLBACK_H
#define ROBO_FONT_FALLBACK_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontCoverage.h"
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

#define ROBO_FONT_FALLBACK_MAX_CODEPOINTS 64u
#define ROBO_FONT_FALLBACK_MAX_PREFERRED 32u
#define ROBO_FONT_FALLBACK_MAX_FAMILY_BYTES 1024u
#define ROBO_FONT_FALLBACK_CACHE_DEFAULT 256u
#define ROBO_FONT_FALLBACK_CACHE_HARD 4096u
#define ROBO_FONT_FALLBACK_MAX_SCAN 4096u

typedef struct robo_font_fallback_resolver robo_font_fallback_resolver_t;

/* ---- Stages (chain order) ---- */

typedef enum robo_font_fallback_stage {
    ROBO_FONT_FALLBACK_STAGE_PRIMARY = 0,
    ROBO_FONT_FALLBACK_STAGE_PREFERRED = 1,
    ROBO_FONT_FALLBACK_STAGE_SCRIPT = 2,
    ROBO_FONT_FALLBACK_STAGE_GENERAL = 3,
    ROBO_FONT_FALLBACK_STAGE_LAST_RESORT = 4,
    ROBO_FONT_FALLBACK_STAGE_NONE = 5
} robo_font_fallback_stage_t;

typedef enum robo_font_fallback_flags {
    ROBO_FONT_FALLBACK_FLAGS_NONE = 0,
    /** Disable the bounded decision cache. */
    ROBO_FONT_FALLBACK_FLAG_NO_CACHE = 1,
    /**
     * Require usable coverage for every code point in the request sequence.
     * Default. Clear only for experimental partial-match probes (still
     * requires the first / base scalar).
     */
    ROBO_FONT_FALLBACK_FLAG_REQUIRE_ALL = 2
} robo_font_fallback_flags_t;

/**
 * Configurable chain / preferences. Strings are borrowed UTF-8.
 * No hard-coded language→font database — callers supply preferences.
 */
typedef struct robo_font_fallback_policy {
    const char *const *preferred_families;
    uint32_t preferred_family_count;

    /**
     * Optional families preferred when request.language is non-empty.
     * Soft signal only; never overrides coverage.
     */
    const char *const *language_preferred_families;
    uint32_t language_preferred_family_count;

    /** Optional last-resort family name (casefold match). */
    const char *last_resort_family;
    size_t last_resort_family_len; /* 0 → strlen */

    /** Optional last-resort face id (takes precedence over family). 0 = none. */
    robo_font_face_id_t last_resort_face_id;

    uint32_t flags; /* robo_font_fallback_flags_t | REQUIRE_ALL defaulted on init */
    uint32_t max_scan; /* 0 → ROBO_FONT_FALLBACK_MAX_SCAN */
    uint32_t cache_capacity; /* 0 → default; only applied at resolver create */
} robo_font_fallback_policy_t;

/**
 * Fallback request. Not a CSS type.
 * @p codepoints is a borrowed sequence foundation (no shaping).
 */
typedef struct robo_font_fallback_request {
    robo_font_face_t *primary; /* required; not retained by request */

    const uint32_t *codepoints;
    uint32_t codepoint_count; /* 1..ROBO_FONT_FALLBACK_MAX_CODEPOINTS */

    robo_font_weight_t weight;
    robo_font_stretch_t stretch;
    robo_font_style_t style;
    uint8_t has_oblique_angle;
    int32_t oblique_angle_millidegrees;

    /** Optional BCP-47 language tag (borrowed). Soft preference only. */
    const char *language;
    size_t language_len; /* 0 → strlen if language != NULL */

    /**
     * Optional script hint. If has_script=0, derived from the first scalar
     * (Script property; Common/Inherited → UNKNOWN → general stage).
     */
    uint8_t has_script;
    robo_unicode_script_t script;

    /** Per-request preferred families (merged ahead of policy list). */
    const char *const *preferred_families;
    uint32_t preferred_family_count;

    const robo_font_fallback_policy_t *policy; /* NULL → resolver default */
    uint32_t flags; /* extra request flags OR'd with policy */
} robo_font_fallback_request_t;

/**
 * Deterministic score. Coverage is a hard gate (eligible=0 if missing).
 * Lower stage_rank / distances are better. Compare with
 * robo_font_fallback_score_compare.
 *
 * Priority (after coverage):
 *   1. stage_rank
 *   2. preferred_family_rank
 *   3. language_rank
 *   4. script_rank (0 = has script, 1 = not)
 *   5. stretch_dist / style_dist / weight_dist (selection distances)
 *   6. face_id
 */
typedef struct robo_font_fallback_score {
    uint8_t eligible;
    uint8_t covers_all;
    uint8_t covers_base;
    robo_font_fallback_stage_t stage;
    uint32_t stage_rank;
    uint32_t preferred_family_rank; /* 0 = best match; large = none */
    uint32_t language_rank;
    uint32_t script_rank;
    uint32_t stretch_dist;
    uint32_t style_dist;
    uint32_t weight_dist;
    uint8_t wws_flag;
    robo_font_face_id_t face_id;
} robo_font_fallback_score_t;

typedef struct robo_font_fallback_result {
    uint32_t cookie;
    uint8_t used_primary;
    uint8_t found;
    uint8_t from_cache;
    robo_font_fallback_stage_t stage;
    robo_font_face_t *face; /* retained when found; clear releases */
    robo_font_face_id_t face_id;
    robo_font_fallback_score_t score;
    uint32_t primary_missing_count;
    uint32_t registry_generation;
} robo_font_fallback_result_t;

#define ROBO_FONT_FALLBACK_RESULT_COOKIE 0x46424B31u /* 'FBK1' */

typedef struct robo_font_fallback_cache_stats {
    uint32_t entries;
    uint32_t hits;
    uint32_t misses;
    uint32_t inserts;
    uint32_t invalidations;
} robo_font_fallback_cache_stats_t;

void robo_font_fallback_policy_init(robo_font_fallback_policy_t *policy);
void robo_font_fallback_request_init(robo_font_fallback_request_t *req);
robo_result_t robo_font_fallback_request_validate(
    const robo_font_fallback_request_t *req);

void robo_font_fallback_result_init(robo_font_fallback_result_t *out);
void robo_font_fallback_result_clear(robo_font_fallback_result_t *res);

int robo_font_fallback_score_compare(const robo_font_fallback_score_t *a,
                                     const robo_font_fallback_score_t *b);

/**
 * Create a resolver bound to @p registry (borrowed; must outlive resolver).
 * Uses registry generation for cache coherence (no exclusive invalidate hook).
 */
robo_result_t robo_font_fallback_resolver_create(
    robo_font_registry_t *registry,
    const robo_font_fallback_policy_t *default_policy,
    robo_font_fallback_resolver_t **out);

void robo_font_fallback_resolver_destroy(robo_font_fallback_resolver_t *r);

robo_result_t robo_font_fallback_resolver_set_policy(
    robo_font_fallback_resolver_t *r,
    const robo_font_fallback_policy_t *policy);

const robo_font_fallback_policy_t *robo_font_fallback_resolver_policy(
    const robo_font_fallback_resolver_t *r);

void robo_font_fallback_resolver_cache_clear(robo_font_fallback_resolver_t *r);
void robo_font_fallback_resolver_cache_stats(const robo_font_fallback_resolver_t *r,
                                             robo_font_fallback_cache_stats_t *out);

/**
 * Resolve fallback for @p req.
 * Fast path: if primary covers all required scalars → used_primary=1, no scan.
 * On no coverage anywhere: found=0, stage=NONE (explicit no-fallback).
 * Concurrent resolve calls are safe if registry is not mutated concurrently.
 */
robo_result_t robo_font_fallback_resolve(robo_font_fallback_resolver_t *r,
                                         const robo_font_fallback_request_t *req,
                                         robo_font_fallback_result_t *out);

/** Convenience: single Unicode scalar. */
robo_result_t robo_font_fallback_resolve_codepoint(
    robo_font_fallback_resolver_t *r,
    robo_font_face_t *primary,
    uint32_t code_point,
    const robo_font_fallback_request_t *style_hints, /* weight/style/lang; cps ignored */
    robo_font_fallback_result_t *out);

/**
 * True if face has usable coverage for every (or base-only) scalar per flags.
 * Ensures face coverage is parsed.
 */
robo_result_t robo_font_fallback_face_covers(robo_font_face_t *face,
                                             const uint32_t *codepoints,
                                             uint32_t count,
                                             uint32_t flags,
                                             int *out_covers_all,
                                             int *out_covers_base,
                                             uint32_t *out_missing_count);

/** Fuzz/fuzz-ready entry. */
robo_result_t robo_font_fallback_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_FALLBACK_H */
