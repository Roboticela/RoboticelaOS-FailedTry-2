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

#define RoboFontFallbackMaxCodepoints 64u
#define RoboFontFallbackMaxPreferred 32u
#define RoboFontFallbackMaxFamilyBytes 1024u
#define RoboFontFallbackCacheDefault 256u
#define RoboFontFallbackCacheHard 4096u
#define RoboFontFallbackMaxScan 4096u

typedef struct FontFallbackResolver FontFallbackResolver;

/* ---- Stages (chain order) ---- */

typedef enum FontFallbackStage {
    RoboFontFallbackStagePrimary = 0,
    RoboFontFallbackStagePreferred = 1,
    RoboFontFallbackStageScript = 2,
    RoboFontFallbackStageGeneral = 3,
    RoboFontFallbackStageLastResort = 4,
    RoboFontFallbackStageNone = 5
} FontFallbackStage;

typedef enum FontFallbackFlags {
    RoboFontFallbackFlagsNone = 0,
    /** Disable the bounded decision cache. */
    RoboFontFallbackFlagNoCache = 1,
    /**
     * Require usable coverage for every code point in the request sequence.
     * Default. Clear only for experimental partial-match probes (still
     * requires the first / base scalar).
     */
    RoboFontFallbackFlagRequireAll = 2
} FontFallbackFlags;

/**
 * Configurable chain / preferences. Strings are borrowed UTF-8.
 * No hard-coded language→font database — callers supply preferences.
 */
typedef struct FontFallbackPolicy {
    const char *const *preferredFamilies;
    uint32_t preferredFamilyCount;

    /**
     * Optional families preferred when request.language is non-empty.
     * Soft signal only; never overrides coverage.
     */
    const char *const *languagePreferredFamilies;
    uint32_t languagePreferredFamilyCount;

    /** Optional last-resort family name (casefold match). */
    const char *lastResortFamily;
    size_t lastResortFamilyLen; /* 0 → strlen */

    /** Optional last-resort face id (takes precedence over family). 0 = none. */
    FontFaceId lastResortFaceId;

    uint32_t flags; /* FontFallbackFlags | REQUIRE_ALL defaulted on init */
    uint32_t maxScan; /* 0 → RoboFontFallbackMaxScan */
    uint32_t cacheCapacity; /* 0 → default; only applied at resolver create */
} FontFallbackPolicy;

/**
 * Fallback request. Not a CSS type.
 * @p codepoints is a borrowed sequence foundation (no shaping).
 */
typedef struct FontFallbackRequest {
    FontFace *primary; /* required; not retained by request */

    const uint32_t *codepoints;
    uint32_t codepointCount; /* 1..RoboFontFallbackMaxCodepoints */

    FontWeight weight;
    FontStretch stretch;
    FontStyle style;
    uint8_t hasObliqueAngle;
    int32_t obliqueAngleMillidegrees;

    /** Optional BCP-47 language tag (borrowed). Soft preference only. */
    const char *language;
    size_t languageLen; /* 0 → strlen if language != NULL */

    /**
     * Optional script hint. If hasScript=0, derived from the first scalar
     * (Script property; Common/Inherited → UNKNOWN → general stage).
     */
    uint8_t hasScript;
    UnicodeScript script;

    /** Per-request preferred families (merged ahead of policy list). */
    const char *const *preferredFamilies;
    uint32_t preferredFamilyCount;

    const FontFallbackPolicy *policy; /* NULL → resolver default */
    uint32_t flags; /* extra request flags OR'd with policy */
} FontFallbackRequest;

/**
 * Deterministic score. Coverage is a hard gate (eligible=0 if missing).
 * Lower stageRank / distances are better. Compare with
 * FontFallbackScoreCompare.
 *
 * Priority (after coverage):
 *   1. stageRank
 *   2. preferredFamilyRank
 *   3. LanguageRank
 *   4. scriptRank (0 = has script, 1 = not)
 *   5. stretchDist / styleDist / weightDist (selection distances)
 *   6. faceId
 */
typedef struct FontFallbackScore {
    uint8_t eligible;
    uint8_t coversAll;
    uint8_t coversBase;
    FontFallbackStage stage;
    uint32_t stageRank;
    uint32_t preferredFamilyRank; /* 0 = best match; large = none */
    uint32_t LanguageRank;
    uint32_t scriptRank;
    uint32_t stretchDist;
    uint32_t styleDist;
    uint32_t weightDist;
    uint8_t wwsFlag;
    FontFaceId faceId;
} FontFallbackScore;

typedef struct FontFallbackResult {
    uint32_t cookie;
    uint8_t usedPrimary;
    uint8_t found;
    uint8_t fromCache;
    FontFallbackStage stage;
    FontFace *face; /* retained when found; clear releases */
    FontFaceId faceId;
    FontFallbackScore score;
    uint32_t primaryMissingCount;
    uint32_t registryGeneration;
} FontFallbackResult;

#define RoboFontFallbackResultCookie 0x46424B31u /* 'FBK1' */

typedef struct FontFallbackCacheStats {
    uint32_t entries;
    uint32_t hits;
    uint32_t misses;
    uint32_t inserts;
    uint32_t invalidations;
} FontFallbackCacheStats;

void FontFallbackPolicyInit(FontFallbackPolicy *policy);
void FontFallbackRequestInit(FontFallbackRequest *req);
Result FontFallbackRequestValidate(
    const FontFallbackRequest *req);

void FontFallbackResultInit(FontFallbackResult *out);
void FontFallbackResultClear(FontFallbackResult *res);

int FontFallbackScoreCompare(const FontFallbackScore *a,
                                     const FontFallbackScore *b);

/**
 * Create a resolver bound to @p registry (borrowed; must outlive resolver).
 * Uses registry generation for cache coherence (no exclusive invalidate hook).
 */
Result FontFallbackResolverCreate(
    FontRegistry *registry,
    const FontFallbackPolicy *defaultPolicy,
    FontFallbackResolver **out);

void FontFallbackResolverDestroy(FontFallbackResolver *r);

Result FontFallbackResolverSetPolicy(
    FontFallbackResolver *r,
    const FontFallbackPolicy *policy);

const FontFallbackPolicy *FontFallbackResolverPolicy(
    const FontFallbackResolver *r);

void FontFallbackResolverCacheClear(FontFallbackResolver *r);
void FontFallbackResolverCacheStats(const FontFallbackResolver *r,
                                             FontFallbackCacheStats *out);

/**
 * Resolve fallback for @p req.
 * Fast path: if primary covers all required scalars → usedPrimary=1, no scan.
 * On no coverage anywhere: found=0, stage=NONE (explicit no-fallback).
 * Concurrent resolve calls are safe if registry is not mutated concurrently.
 */
Result FontFallbackResolve(FontFallbackResolver *r,
                                         const FontFallbackRequest *req,
                                         FontFallbackResult *out);

/** Convenience: single Unicode scalar. */
Result FontFallbackResolveCodepoint(
    FontFallbackResolver *r,
    FontFace *primary,
    uint32_t codePoint,
    const FontFallbackRequest *styleHints, /* weight/style/lang; cps ignored */
    FontFallbackResult *out);

/**
 * True if face has usable coverage for every (or base-only) scalar per flags.
 * Ensures face coverage is parsed.
 */
Result FontFallbackFaceCovers(FontFace *face,
                                             const uint32_t *codepoints,
                                             uint32_t count,
                                             uint32_t flags,
                                             int *outCoversAll,
                                             int *outCoversBase,
                                             uint32_t *outMissingCount);

/** Fuzz/fuzz-ready entry. */
Result FontFallbackFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_FALLBACK_H */
