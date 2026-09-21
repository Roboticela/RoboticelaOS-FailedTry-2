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

#define RoboFontGenericMaxFamiliesPerMapping 64u
#define RoboFontGenericMaxFamilyBytes 1024u
#define RoboFontGenericMaxMappings 256u
#define RoboFontGenericCacheDefault 128u
#define RoboFontGenericCacheHard 1024u

/* Family IDs live in RoboFontStyle.h (FontGenericFamily). */

/** Which configuration layer supplied the winning mapping. */
typedef enum FontGenericLayer {
    RoboFontGenericLayerNone = 0,
    RoboFontGenericLayerApplication = 1,
    RoboFontGenericLayerRuntime = 2,
    RoboFontGenericLayerPlatform = 3,
    RoboFontGenericLayerRelated = 4
} FontGenericLayer;

typedef enum FontGenericFlags {
    RoboFontGenericFlagsNone = 0,
    /**
     * If the requested generic has no mapping, try a related base generic
     * (ui-serif→serif, ui-sans-serif→sans-serif, ui-monospace→monospace,
     * ui-rounded→sans-serif). system-ui / emoji / math never auto-relate.
     */
    RoboFontGenericFlagAllowRelated = 1,
    /** Skip the bounded resolution cache. */
    RoboFontGenericFlagNoCache = 2,
    /**
     * When collecting faces / selecting, skip concrete families that have
     * zero registered faces (continue to next priority entry).
     */
    RoboFontGenericFlagSkipMissing = 4
} FontGenericFlags;

typedef struct FontGenericConfig FontGenericConfig;
typedef struct FontGenericResolver FontGenericResolver;

/**
 * Resolution request. Not a CSS type.
 * Optional language/script are soft filters for language-scoped mappings.
 */
typedef struct FontGenericRequest {
    FontGenericFamily generic;
    /** Optional BCP-47 language (borrowed). Soft — only matches configured
     * language-scoped mappings; never invents fonts. */
    const char *language;
    size_t languageLen; /* 0 → strlen */
    uint8_t hasScript;
    UnicodeScript script; /* reserved foundation; unused for ranking */
    uint32_t flags; /* OR'd with resolver defaults */
} FontGenericRequest;

/**
 * Resolved concrete family list (owned by result; clear releases).
 * Families are UTF-8 names in configured priority order.
 */
typedef struct FontGenericResult {
    uint32_t cookie;
    uint8_t resolved;
    uint8_t fromCache;
    uint8_t usedRelated;
    FontGenericFamily requested;
    FontGenericFamily effective; /* may differ if related used */
    FontGenericLayer layer;
    char **families; /* owned array of owned strings */
    uint32_t familyCount;
    uint64_t configGeneration;
    uint64_t registryGeneration;
} FontGenericResult;

#define RoboFontGenericResultCookie 0x47454E31u /* 'GEN1' */

typedef struct FontGenericCacheStats {
    uint64_t hits;
    uint64_t misses;
    uint64_t inserts;
    uint64_t invalidations;
    uint32_t capacity;
    uint32_t occupied;
} FontGenericCacheStats;

typedef struct FontGenericResolverOptions {
    const FontGenericConfig *platform;     /* optional retained */
    const FontGenericConfig *runtime;      /* optional retained */
    const FontGenericConfig *application;  /* optional retained */
    uint32_t flags; /* default resolver flags */
    uint32_t cacheCapacity; /* 0 → default */
} FontGenericResolverOptions;

/* ---- name interchange ---- */

const char *FontGenericFamilyName(FontGenericFamily g);
Result FontGenericFamilyFromName(const char *name,
                                                 size_t nameLen,
                                                 FontGenericFamily *out);
int FontGenericFamilyIsValid(FontGenericFamily g);

/** Related base for ui-* when ALLOW_RELATED; else UNKNOWN. */
FontGenericFamily FontGenericRelated(FontGenericFamily g);

/* ---- immutable-style config (generation bumps on mutation) ---- */

Result FontGenericConfigCreate(FontGenericConfig **out);
void FontGenericConfigRetain(FontGenericConfig *cfg);
void FontGenericConfigRelease(FontGenericConfig *cfg);

uint64_t FontGenericConfigGeneration(const FontGenericConfig *cfg);
uint32_t FontGenericConfigMappingCount(const FontGenericConfig *cfg);

/**
 * Set/replace the default mapping for @p generic (language = NULL).
 * @p families may be empty to clear. Duplicates are kept in order (first wins
 * for presence, list remains deterministic). Invalid generic → error.
 */
Result FontGenericConfigSetMapping(
    FontGenericConfig *cfg,
    FontGenericFamily generic,
    const char *const *families,
    uint32_t familyCount);

/**
 * Language-scoped mapping. When resolving with a matching language, this
 * overrides the default mapping for the same generic. Empty language clears.
 */
Result FontGenericConfigSetMappingForLanguage(
    FontGenericConfig *cfg,
    FontGenericFamily generic,
    const char *language,
    size_t languageLen,
    const char *const *families,
    uint32_t familyCount);

/** Clear all mappings; bumps generation. */
Result FontGenericConfigClear(FontGenericConfig *cfg);

/**
 * Inspect mapping. @p outFamilies borrows config storage (valid until next
 * mutation of this config). language NULL queries the default mapping.
 */
Result FontGenericConfigGetMapping(
    const FontGenericConfig *cfg,
    FontGenericFamily generic,
    const char *language,
    size_t languageLen,
    const char *const **outFamilies,
    uint32_t *outCount);

/* ---- resolver ---- */

void FontGenericResolverOptionsInit(
    FontGenericResolverOptions *opts);

Result FontGenericResolverCreate(
    FontRegistry *registry,
    const FontGenericResolverOptions *opts,
    FontGenericResolver **out);

void FontGenericResolverDestroy(FontGenericResolver *r);

/**
 * Atomic layer replacement: retains @p cfg (NULL clears layer).
 * Bumps resolver generation and clears the decision cache.
 */
Result FontGenericResolverSetPlatform(
    FontGenericResolver *r,
    FontGenericConfig *cfg);
Result FontGenericResolverSetRuntime(
    FontGenericResolver *r,
    FontGenericConfig *cfg);
Result FontGenericResolverSetApplication(
    FontGenericResolver *r,
    FontGenericConfig *cfg);

uint64_t FontGenericResolverGeneration(
    const FontGenericResolver *r);

void FontGenericRequestInit(FontGenericRequest *req);
Result FontGenericRequestValidate(
    const FontGenericRequest *req);

void FontGenericResultInit(FontGenericResult *out);
void FontGenericResultClear(FontGenericResult *res);

/**
 * Resolve generic → ordered concrete family names.
 * Empty mapping → resolved=0 (does not invent a family).
 */
Result FontGenericResolve(FontGenericResolver *r,
                                        const FontGenericRequest *req,
                                        FontGenericResult *out);

/**
 * Resolve then select via RoboFontSelector.
 * Walks configured families in order; first family with an eligible face wins.
 * @p style provides weight/stretch/style (family field ignored).
 */
Result FontGenericResolveSelect(
    FontGenericResolver *r,
    const FontGenericRequest *req,
    const FontSelectRequest *style,
    FontGenericResult *genericOut, /* optional */
    FontSelection *selectionOut);

/**
 * Resolve + select primary, then run Goal 5.15 fallback with the resolved
 * family list as preferredFamilies. Requires a fallback resolver bound to
 * the same registry.
 */
Result FontGenericResolveFallback(
    FontGenericResolver *r,
    FontFallbackResolver *fallback,
    const FontGenericRequest *req,
    const FontSelectRequest *style,
    const uint32_t *codepoints,
    uint32_t codepointCount,
    FontGenericResult *genericOut, /* optional */
    FontSelection *selectionOut,    /* optional primary selection */
    FontFallbackResult *fallbackOut);

void FontGenericResolverCacheClear(FontGenericResolver *r);
void FontGenericResolverCacheStats(
    const FontGenericResolver *r,
    FontGenericCacheStats *out);

/** Empty portable platform defaults (no hard-coded fonts). */
Result FontGenericConfigCreateEmptyPlatform(
    FontGenericConfig **out);

/** Fuzz entry — never crashes. */
Result FontGenericFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_GENERIC_FAMILY_H */
