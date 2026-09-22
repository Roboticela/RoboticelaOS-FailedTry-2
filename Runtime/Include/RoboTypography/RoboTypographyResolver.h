/*
 * RoboTypography resolver — role in, a loaded face and its numbers out (Phase 12.3).
 *
 * ## What it is, and what it is not
 *
 * It **binds** and **caches**; it does not parse, rasterize or shape. The chain is:
 *
 *   role ── scale ──> pack role + weight + size + leading
 *                            │
 *                    FontSystemPackResolveRole        (Phase 12.2)
 *                            │  FontSelect           (Phase 5.11)
 *                            ▼
 *                   a loaded FontFace + a TypographyStyle
 *
 * Every layer below this one already existed, and that is the point: 12.3 adds the *binding* between
 * a semantic role and the faces 12.2 installed, and nothing else. There is no second font parser
 * here, no second metrics implementation (`RoboTypographyStyleApplyFontMetrics` calls RoboFont's own
 * two doors), and no second selection rule (the pack's `FontSystemPackResolveRole` is the only
 * caller of `FontSelect` in this module).
 *
 * ## Fallback is a face choice, and it is deterministic
 *
 * Two fallbacks exist and they are different things:
 *
 *   **weight step-down.** A role asks for 600 on a family that cannot select it. The weight is
 *   snapped to the nearest weight *in the vocabulary* (`RoboTypographyNearestWeight`, ties lighter)
 *   -- deterministically, and *never* by synthesizing an emboldening. The style records the weight
 *   that was actually selected, so a caller can see the step-down rather than guess at it.
 *
 *   **coverage fallback.** Text the system face cannot draw is a *coverage* question, and it is
 *   answered by RoboFont's own fallback chain through `RoboTypographyResolveGlyphs`. That call
 *   returns the primary selection *and* the fallback decision, so a caller can paint the covered
 *   run with one and the rest with the other. Nothing is shaped.
 *
 * ## Ownership, and the one place it is worth reading twice
 *
 * A resolved `RoboTypographyStyle`'s `face` is **borrowed** from the font registry the pack
 * registered into: the resolver hands out registry-owned faces and never a face it owns itself. The
 * one exception is `RoboTypographyResolveGlyphs`, where a fallback face is **retained by the
 * `FontFallbackResult` the caller passes in** -- so that result must outlive the style it produced,
 * and the caller clears it (which releases the reference) when it is done. RoboFont's own header
 * states the same contract one layer down.
 *
 * ## Cache
 *
 * Resolution is cached **per role**, keyed by the (weight, style) the role was last resolved at. A
 * second `Resolve` of the same role with the same weight and style is a lookup rather than a scan,
 * which is what keeps a shell painting many labels from re-running selection for each one. The key
 * is (role, weight, style) rather than role alone because an override that returned the cached
 * setting would be a *wrong* answer rather than a slow one. The cache is bounded by construction --
 * one entry per defined role -- so there is no eviction policy to get wrong.
 */
#ifndef ROBO_TYPOGRAPHY_RESOLVER_H
#define ROBO_TYPOGRAPHY_RESOLVER_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFallback.h"
#include "RoboFont/RoboFontSystemPack.h"
#include "RoboTypography/RoboTypographyDiagnostics.h"
#include "RoboTypography/RoboTypographyScale.h"
#include "RoboTypography/RoboTypographyTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypographyResolver TypographyResolver;

/** What the resolver has resolved so far. */
typedef struct RoboTypographyResolverStats {
    /** Roles resolved with a face. */
    uint32_t resolved;
    /** Roles whose family could not be selected (the style is still returned, face-less). */
    uint32_t unresolved;
    /** Lookups served from the cache. */
    uint32_t cacheHits;
    /** Lookups that ran selection. */
    uint32_t cacheMisses;
    /** Cache entries written. */
    uint32_t cacheInserts;
    /** Weight requests that were stepped down to a vocabulary neighbour. */
    uint32_t weightStepDowns;
} RoboTypographyResolverStats;

/**
 * The resolver's inputs.
 *
 * A resolver is created **over** a pack rather than owning one: the pack owns the manifest, the
 * registrations and the loader cache, and two resolvers over one pack is a legitimate arrangement
 * (a shell and a document, say) with no duplicated parsing.
 */
typedef struct RoboTypographyResolverConfig {
    /** Required; borrowed and must outlive the resolver. */
    const FontSystemPack *pack;
    /**
     * Optional; borrowed. When set, `RoboTypographyResolveGlyphs` can answer coverage fallback.
     * A resolver without one still resolves roles -- it simply reports that it has no chain to ask.
     */
    FontFallbackResolver *fallback;
    /** NULL means `RoboTypographyDefaultScale`. */
    const RoboTypographyScaleEntry *scale;
    /** 0 with a NULL scale means the default scale. */
    size_t scaleCount;
    /** Reserved; pass 0. */
    uint32_t flags;
} RoboTypographyResolverConfig;

/** Fill @p config with the defaults (no pack, no fallback, the default scale). Safe on NULL. */
void RoboTypographyResolverConfigInit(RoboTypographyResolverConfig *config);

/**
 * Create a resolver over @p config->pack.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL @p config, @p out, a NULL pack, or an
 *         invalid scale entry; RoboOutOfMemory.
 */
Result RoboTypographyResolverCreate(const RoboTypographyResolverConfig *config,
                                    TypographyResolver **out,
                                    TypographyDiag *diag);

/** Destroy @p resolver. Safe on NULL. Never touches the borrowed pack or fallback. */
void RoboTypographyResolverDestroy(TypographyResolver *resolver);

/** Borrowed scale. NULL for a NULL resolver. */
const RoboTypographyScaleEntry *RoboTypographyResolverScale(
    const TypographyResolver *resolver);
size_t RoboTypographyResolverScaleCount(const TypographyResolver *resolver);

/**
 * Resolve @p role at the scale's own weight and an upright slope.
 *
 * Never fails for a missing family: a role whose face cannot be selected returns RoboOk with a
 * face-less style (`face == NULL`, `sizePx`/`lineHeightPx`/`weight` still filled from the scale), so
 * a caller can lay out and report a textless surface instead of aborting a frame. RoboNotFound is
 * reserved for a role this resolver has no scale entry for, and RoboInvalidArgument for a bad
 * argument.
 *
 * @return RoboOk and @p outStyle; RoboInvalidArgument; RoboNotFound.
 */
Result RoboTypographyResolve(TypographyResolver *resolver,
                             RoboTypographyRole role,
                             RoboTypographyStyle *outStyle,
                             TypographyDiag *diag);

/**
 * Resolve @p role at an explicit @p weight and @p style.
 *
 * @p weight 0 means the scale's own weight. The weight is snapped to the vocabulary before
 * selection, and the rounded result is carried in the returned style. The selected face may be a
 * different weight from the one asked for when the packaged family cannot select it -- the style
 * says which, and `stats.weightStepDowns` counts the cases.
 *
 * @return as @ref RoboTypographyResolve.
 */
Result RoboTypographyResolveWeight(TypographyResolver *resolver,
                                   RoboTypographyRole role,
                                   FontWeight weight,
                                   FontStyle style,
                                   RoboTypographyStyle *outStyle,
                                   TypographyDiag *diag);

/**
 * Resolve @p role, then resolve a face that **covers** @p codepoints.
 *
 * The role's own selection fills @p outStyle as `RoboTypographyResolve` would; @p outFallback then
 * receives the coverage decision, whose `face` may be a *different* face -- in which case
 * @p outStyle's `face` is updated to the covering one and is **borrowed from @p outFallback** until
 * the caller clears it. `outFallback->usedPrimary` is the "the role's own face covers this" answer,
 * and a caller that does not care can pass a single codepoint.
 *
 * A resolver with no fallback resolver bound reports RoboNotSupported rather than pretending the
 * primary covers everything (see `RoboTypographyResolverConfig.fallback`).
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument, an empty sequence or an undefined role;
 *         RoboNotFound when the role has no scale entry; RoboNotSupported when no fallback resolver
 *         is bound.
 */
Result RoboTypographyResolveGlyphs(TypographyResolver *resolver,
                                   RoboTypographyRole role,
                                   const uint32_t *codepoints,
                                   uint32_t codepointCount,
                                   RoboTypographyStyle *outStyle,
                                   FontFallbackResult *outFallback,
                                   TypographyDiag *diag);

/**
 * The document defaults as CSS, from this resolver's scale.
 *
 * The door `RoboDesktopDocument` uses: the text is added to a document as a runtime-origin sheet, so
 * the defaults are this module's and not a second copy in the document builder. See
 * `RoboTypographyBuildDocumentStyleSheet` for the format.
 */
Result RoboTypographyResolverDocumentStyleSheet(
    const TypographyResolver *resolver,
    char *buffer,
    size_t capacity,
    size_t *outLength);

/** Copy the resolver's counters. RoboInvalidArgument for NULL arguments. */
Result RoboTypographyResolverGetStats(const TypographyResolver *resolver,
                                      RoboTypographyResolverStats *out);

/** Drop every cached resolution. Statistics are not reset. Safe on NULL. */
void RoboTypographyResolverCacheClear(TypographyResolver *resolver);

/** Fuzz entry: arbitrary bytes as a role name and a weight name. Never crashes. */
Result RoboTypographyFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TYPOGRAPHY_RESOLVER_H */
