/*
 * RoboTypography's resolver (Phase 12.3) — see RoboTypographyResolver.h.
 *
 * The whole of the module's behaviour is here, and it is four steps per call:
 *
 *   1. the **scale** gives the role its pack role, weight, size and leading;
 *   2. the **weight** is snapped to the vocabulary, and the step-down is counted rather than hidden;
 *   3. the **pack** resolves the pack role and weight to a loaded face through Phase 5's selector;
 *   4. the **face's metrics** are scaled once, so a caller placing a baseline and a caller measuring a
 *      run read one derivation instead of two.
 *
 * Two things about step 3 are worth stating in code because they are the reason it is not longer.
 *
 * **The face is borrowed, and that is not a shortcut.** `FontSelect` retains the candidate it wins
 * with, and the candidate is the *registry's own* face object (Phase 5 selection applies variable
 * axes as recorded settings rather than materialising a second face). The registry holds a reference
 * for every registration and releases it on clear, remove or destroy, so a pointer it handed out
 * stays valid for as long as the registry does -- which is the same contract `DesktopConfig.face`
 * has had since Phase 11. A resolver therefore resolves into a *temporary* `FontSelection`, copies
 * the face pointer out, and clears the selection to give back the reference it took. Nothing is kept
 * alive by this module, so there is no ownership bug to get wrong.
 *
 * **A missing family is not a failure.** A role whose family cannot be selected returns RoboOk with
 * a face-less style, because a size, a weight and a leading are exactly what a layout needs to leave
 * a hole where text would have been -- and a desktop that refused to lay out because its font was
 * missing would be a desktop that could not report the fact. The counter `unresolved` is how a caller
 * learns it, and the frame record's own `labelsUnshaped` is how a *user* sees it.
 */

#include "RoboTypography/RoboTypographyResolver.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The resolver                                                        */
/* ------------------------------------------------------------------ */

/** One role's cached resolution, keyed by the weight and slope it was resolved at. */
typedef struct TypographyCacheEntry {
    int valid;
    /** The weight the entry was resolved at; 0 means the scale's own. */
    FontWeight requestedWeight;
    FontStyle requestedStyle;
    RoboTypographyStyle style;
} TypographyCacheEntry;

struct TypographyResolver {
    /** Borrowed; must outlive the resolver. */
    const FontSystemPack *pack;
    /** Borrowed and optional. */
    FontFallbackResolver *fallback;
    /** Borrowed: either the caller's scale or `ownedScale`. */
    const RoboTypographyScaleEntry *scale;
    size_t scaleCount;
    /** The defaults copied at creation, when the caller supplied none. */
    RoboTypographyScaleEntry ownedScale[RoboTypographyScaleCount];
    int ownsScale;
    /** One entry per defined role: bounded by construction, so no eviction policy exists. */
    TypographyCacheEntry cache[RoboTypographyRoleCount];
    RoboTypographyResolverStats stats;
};

void RoboTypographyResolverConfigInit(RoboTypographyResolverConfig *config)
{
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->pack = NULL;
    config->fallback = NULL;
    config->scale = NULL;
    config->scaleCount = 0u;
    config->flags = 0u;
}

/** Non-zero when @p scale holds no duplicate role and every entry is a settable type setting. */
static int ScaleIsUsable(const RoboTypographyScaleEntry *scale, size_t count)
{
    size_t index;
    size_t other;

    for (index = 0u; index < count; index++) {
        if (!RoboTypographyScaleEntryIsValid(&scale[index])) {
            return 0;
        }
        for (other = index + 1u; other < count; other++) {
            /* A duplicate role would make the answer depend on which entry `find` reached first,
             * which is a non-determinism a caller could not see from the outside. */
            if (scale[other].role == scale[index].role) {
                return 0;
            }
        }
    }
    return 1;
}

Result RoboTypographyResolverCreate(const RoboTypographyResolverConfig *config,
                                    TypographyResolver **out,
                                    TypographyDiag *diag)
{
    TypographyResolver *resolver;

    if (!config || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (!config->pack) {
        return RoboTypographyDiag(diag, RoboTypographyDomainResolver, RoboInvalidArgument,
                                  "a resolver needs the system typeface pack to resolve against");
    }

    resolver = (TypographyResolver *)RoboCalloc(1u, sizeof(*resolver));
    if (!resolver) {
        return RoboOutOfMemory;
    }
    resolver->pack = config->pack;
    resolver->fallback = config->fallback;

    if (config->scale && config->scaleCount > 0u) {
        /* A caller's scale is borrowed, and it must be usable: a scale with an invalid entry would
         * make resolution depend on which role was asked for first. */
        if (!ScaleIsUsable(config->scale, config->scaleCount)) {
            RoboFree(resolver);
            return RoboTypographyDiag(diag, RoboTypographyDomainScale, RoboInvalidArgument,
                                      "the supplied scale has an invalid entry or a duplicate role");
        }
        resolver->scale = config->scale;
        resolver->scaleCount = config->scaleCount;
        resolver->ownsScale = 0;
    } else {
        if (RoboTypographyDefaultScale(resolver->ownedScale, RoboTypographyScaleCount, NULL) !=
            RoboOk) {
            RoboFree(resolver);
            return RoboTypographyDiag(diag, RoboTypographyDomainScale, RoboError,
                                      "the default scale could not be read");
        }
        resolver->scale = resolver->ownedScale;
        resolver->scaleCount = RoboTypographyScaleCount;
        resolver->ownsScale = 1;
    }

    *out = resolver;
    return RoboOk;
}

void RoboTypographyResolverDestroy(TypographyResolver *resolver)
{
    /* Nothing is released: every face a resolution names is the registry's, and the pack, the scale
     * and the fallback resolver are all borrowed. The call exists so that a caller's lifetime is the
     * same shape as every other module's -- and so that a future owned resource has one place to go. */
    RoboFree(resolver);
}

const RoboTypographyScaleEntry *RoboTypographyResolverScale(
    const TypographyResolver *resolver)
{
    return resolver ? resolver->scale : NULL;
}

const FontSystemPack *RoboTypographyResolverPack(const TypographyResolver *resolver)
{
    return resolver ? resolver->pack : NULL;
}

size_t RoboTypographyResolverScaleCount(const TypographyResolver *resolver)
{
    return resolver ? resolver->scaleCount : 0u;
}

void RoboTypographyResolverCacheClear(TypographyResolver *resolver)
{
    if (!resolver) {
        return;
    }
    memset(resolver->cache, 0, sizeof(resolver->cache));
}

Result RoboTypographyResolverGetStats(const TypographyResolver *resolver,
                                      RoboTypographyResolverStats *out)
{
    if (!resolver || !out) {
        return RoboInvalidArgument;
    }
    *out = resolver->stats;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

/**
 * Resolve one scale entry into @p outStyle at @p weightOverride / @p style.
 *
 * The four steps the file comment names, in order. @p weightOverride == 0 means the scale's weight;
 * an override is snapped to the vocabulary *before* selection, so a caller asking for 550 gets 600
 * from a family that cannot select 550 at all -- deterministically, and never by an emboldening.
 */
static void ResolveEntry(TypographyResolver *resolver,
                         const RoboTypographyScaleEntry *entry,
                         FontWeight weightOverride,
                         FontStyle style,
                         RoboTypographyStyle *outStyle)
{
    FontSelection selection;
    FontWeight requested;
    FontWeight selected;

    RoboTypographyStyleInit(outStyle);
    outStyle->role = entry->role;
    outStyle->fontRole = entry->fontRole;
    outStyle->sizePx = entry->sizePx;
    outStyle->lineHeightPx = entry->lineHeightPx;
    outStyle->opticalSize = entry->opticalSize;

    requested = (weightOverride != 0u) ? weightOverride : entry->weight;
    selected = requested;
    if (requested != 0u) {
        selected = RoboTypographyNearestWeight(requested);
        if (selected != requested) {
            resolver->stats.weightStepDowns++;
        }
    }
    outStyle->weight = selected;

    FontSelectionInit(&selection);
    if (FontSystemPackResolveRole(resolver->pack, entry->fontRole, selected, 0u, style,
                                  &selection) != RoboOk ||
        !selection.found || !selection.face) {
        /* No family, no eligible face, or a pack that could not answer: the style keeps its numbers
         * and has no face, which is the state a caller reports rather than aborts on. */
        resolver->stats.unresolved++;
        (void)RoboTypographyStyleApplyFontMetrics(outStyle);
        return;
    }
    outStyle->face = selection.face;
    /* The weight the *selector* chose, which is the honest answer: a family with a `wght` axis
     * carries the requested weight through as an axis setting, and a static family returns its own
     * face's weight -- and the style should say which happened. */
    if (FontWeightIsValid(selection.selected.weight)) {
        outStyle->weight = selection.selected.weight;
    }
    /* The reference `FontSelect` took goes straight back: the registry holds the face alive. */
    FontSelectionClear(&selection);

    resolver->stats.resolved++;
    (void)RoboTypographyStyleApplyFontMetrics(outStyle);
}

Result RoboTypographyResolveWeight(TypographyResolver *resolver,
                                   RoboTypographyRole role,
                                   FontWeight weight,
                                   FontStyle style,
                                   RoboTypographyStyle *outStyle,
                                   TypographyDiag *diag)
{
    const RoboTypographyScaleEntry *entry;
    TypographyCacheEntry *cached;

    if (!resolver || !outStyle) {
        return RoboInvalidArgument;
    }
    if (!RoboTypographyRoleIsValid(role)) {
        return RoboTypographyDiag(diag, RoboTypographyDomainRole, RoboInvalidArgument,
                                  "that is not a defined typography role");
    }
    entry = RoboTypographyScaleFindEntry(resolver->scale, resolver->scaleCount, role);
    if (!entry) {
        return RoboTypographyDiag(diag, RoboTypographyDomainScale, RoboNotFound,
                                  "this resolver's scale has no entry for that role");
    }

    cached = &resolver->cache[(size_t)role - 1u];
    if (cached->valid && cached->requestedWeight == weight &&
        cached->requestedStyle == style) {
        resolver->stats.cacheHits++;
        *outStyle = cached->style;
        return RoboOk;
    }
    resolver->stats.cacheMisses++;

    ResolveEntry(resolver, entry, weight, style, outStyle);
    cached->valid = 1;
    cached->requestedWeight = weight;
    cached->requestedStyle = style;
    cached->style = *outStyle;
    resolver->stats.cacheInserts++;
    return RoboOk;
}

Result RoboTypographyResolve(TypographyResolver *resolver,
                             RoboTypographyRole role,
                             RoboTypographyStyle *outStyle,
                             TypographyDiag *diag)
{
    /* The scale's own weight and an upright slope: the common case, and the one the cache is keyed
     * for, so a shell painting many labels resolves once and looks up thereafter. */
    return RoboTypographyResolveWeight(resolver, role, 0u, RoboFontStyleNormal, outStyle, diag);
}

Result RoboTypographyResolveGlyphs(TypographyResolver *resolver,
                                   RoboTypographyRole role,
                                   const uint32_t *codepoints,
                                   uint32_t codepointCount,
                                   RoboTypographyStyle *outStyle,
                                   FontFallbackResult *outFallback,
                                   TypographyDiag *diag)
{
    FontFallbackRequest request;
    RoboTypographyStyle style;
    Result status;

    if (!resolver || !codepoints || !outStyle || !outFallback || codepointCount == 0u) {
        return RoboInvalidArgument;
    }
    if (!resolver->fallback) {
        return RoboTypographyDiag(diag, RoboTypographyDomainResolver, RoboNotSupported,
                                  "this resolver has no fallback chain bound, so it cannot answer "
                                  "a coverage question");
    }

    status = RoboTypographyResolve(resolver, role, &style, diag);
    if (status != RoboOk) {
        return status;
    }
    if (!style.face) {
        /* Coverage fallback starts *from* the role's own face: with no primary there is nothing to
         * fall back from, and RoboFont's request contract requires one. The honest answer is that the
         * role's family is not installed, which is a different fact from "nothing covers this". */
        *outStyle = style;
        return RoboTypographyDiag(diag, RoboTypographyDomainResolver, RoboNotFound,
                                  "the role's family is not installed, so there is no primary face "
                                  "to fall back from");
    }

    FontFallbackRequestInit(&request);
    request.primary = style.face;
    request.codepoints = codepoints;
    request.codepointCount = codepointCount;
    request.weight = style.weight;
    request.stretch = RoboFontStretchNormal;
    request.style = RoboFontStyleNormal;

    status = FontFallbackResolve(resolver->fallback, &request, outFallback);
    if (status != RoboOk) {
        *outStyle = style;
        return status;
    }
    if (outFallback->found && outFallback->face && outFallback->face != style.face) {
        /* A different face covers the text. The style now names it, and it is borrowed from
         * @p outFallback -- so that result must outlive this style (see the header). */
        style.face = outFallback->face;
        (void)RoboTypographyStyleApplyFontMetrics(&style);
    }
    *outStyle = style;
    return RoboOk;
}

Result RoboTypographyResolverDocumentStyleSheet(
    const TypographyResolver *resolver,
    char *buffer,
    size_t capacity,
    size_t *outLength)
{
    if (!resolver) {
        return RoboInvalidArgument;
    }
    return RoboTypographyBuildDocumentStyleSheet(resolver->scale, resolver->scaleCount, buffer,
                                                 capacity, outLength);
}

/* ------------------------------------------------------------------ */
/* Fuzz                                                                */
/* ------------------------------------------------------------------ */

Result RoboTypographyFuzzParse(const uint8_t *data, size_t size)
{
    size_t offset = 0u;
    size_t tokens = 0u;

    /*
     * Split the bytes on NUL and ask each piece for a role, an optical class and a weight. The point
     * is not that anything resolves -- it is that arbitrary bytes reach the parsers without a read
     * past the end, a division by zero or a loop that does not advance. Every parser called here is
     * a pure function of a slice and its length, which is what makes that checkable at all.
     */
    while (offset <= size && tokens < 64u) {
        size_t start = offset;
        size_t length;
        RoboTypographyRole role;
        RoboTypographyOpticalSize optical;
        FontWeight weight;

        while (offset < size && data[offset] != 0u) {
            offset++;
        }
        length = offset - start;
        if (length > 0u) {
            (void)RoboTypographyRoleFromName((const char *)(const void *)(data + start), length,
                                             &role);
            (void)RoboTypographyOpticalSizeFromName(
                (const char *)(const void *)(data + start), length, &optical);
            (void)RoboTypographyWeightFromName((const char *)(const void *)(data + start), length,
                                               &weight);
            (void)RoboTypographyNearestWeight((FontWeight)(length * 97u));
        }
        tokens++;
        if (offset < size) {
            offset++; /* step over the separator, so progress is guaranteed */
        } else {
            break;
        }
    }
    return RoboOk;
}
