/*
 * Kerning (Phase 12.13) — see `RoboTextKerning.h` for what this layer is and is not.
 *
 * Three pieces, and none of them is a kerning algorithm.
 *
 *   **the policy**   one feature state and one source choice, converted into a `RoboTextFeatureSet`. The
 *                    conversion is the only way a policy reaches an engine, which is why `font-kerning: none`
 *                    cannot end up as a special case in a paint layer.
 *   **the resolution** the face is asked **twice**, deliberately: once with the engine's own defaults (what
 *                    does this face offer?) and once with the caller's settings (what applies?). One
 *                    resolution could not answer both, because a caller that turns kerning off has not changed
 *                    what the face carries — and a diagnostic that conflated the two would report a face as
 *                    kernless when it was merely not asked.
 *   **the report**   a walk over the run's glyphs, reading the adjustment each one already carries. Nothing is
 *                    re-shaped to produce it, so a report costs one pass over an array the caller owns.
 */
#include "RoboText/RoboTextKerning.h"

#include "RoboFont/RoboFontSfnt.h"
#include "RoboText/RoboTextGpos.h"
#include "RoboText/RoboTextOtLayout.h"
#include "RoboTextInternal.h"

#include <string.h>

/** Biggest line a dump in this file builds before it is copied out. */
#define RoboTextKerningLineBytes 192u

/* ------------------------------------------------------------------ */
/* Policy                                                              */
/* ------------------------------------------------------------------ */

void RoboTextKerningPolicyInit(RoboTextKerningPolicy *policy)
{
    if (!policy) {
        return;
    }
    policy->kern = RoboTextFeatureDefault;
    policy->legacy = RoboTextKerningLegacyNever;
}

void RoboTextKerningPolicyDefault(RoboTextKerningPolicy *policy)
{
    if (!policy) {
        return;
    }
    /*
     * Kerning **on**: the phase's answer for ordinary document and UI text, and the same answer the GPOS
     * plan's script defaults give for `kern`. Stated as data so a caller can disagree in one place, and so
     * that "the default is on" is a fact a test can read rather than an assumption about an engine.
     */
    policy->kern = RoboTextFeatureOn;
    policy->legacy = RoboTextKerningLegacyNever;
}

Result RoboTextKerningPolicySet(RoboTextKerningPolicy *policy, RoboTextFeatureState kern)
{
    if (!policy || !RoboTextFeatureStateIsKnown(kern)) {
        return RoboInvalidArgument;
    }
    policy->kern = kern;
    return RoboOk;
}

RoboTextFeatureState RoboTextKerningPolicyState(const RoboTextKerningPolicy *policy)
{
    return policy ? policy->kern : RoboTextFeatureDefault;
}

RoboTextKerningLegacy RoboTextKerningPolicyLegacy(const RoboTextKerningPolicy *policy)
{
    return policy ? policy->legacy : RoboTextKerningLegacyNever;
}

Result RoboTextKerningPolicySetLegacy(RoboTextKerningPolicy *policy, RoboTextKerningLegacy legacy)
{
    if (!policy || (int)legacy < 0 || (int)legacy >= (int)RoboTextKerningLegacyCount) {
        return RoboInvalidArgument;
    }
    policy->legacy = legacy;
    return RoboOk;
}

int RoboTextKerningPolicyIsValid(const RoboTextKerningPolicy *policy)
{
    if (!policy) {
        return 0;
    }
    return RoboTextFeatureStateIsKnown(policy->kern) && (int)policy->legacy >= 0 &&
           (int)policy->legacy < (int)RoboTextKerningLegacyCount;
}

int RoboTextKerningPolicyEqual(const RoboTextKerningPolicy *a, const RoboTextKerningPolicy *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->kern == b->kern && a->legacy == b->legacy;
}

const char *RoboTextKerningSourceName(RoboTextKerningSource source)
{
    switch (source) {
        case RoboTextKerningSourceNone:
            return "none";
        case RoboTextKerningSourceGpos:
            return "gpos";
        case RoboTextKerningSourceLegacyKern:
            return "kern-table";
        default:
            break;
    }
    return "?";
}

int RoboTextKerningSourceIsKnown(RoboTextKerningSource source)
{
    return (int)source >= 0 && (int)source < (int)RoboTextKerningSourceCount;
}

static const char *KerningLegacyName(RoboTextKerningLegacy legacy)
{
    switch (legacy) {
        case RoboTextKerningLegacyNever:
            return "never";
        case RoboTextKerningLegacyIfNoGposKern:
            return "if-no-gpos-kern";
        default:
            break;
    }
    return "?";
}

Result RoboTextKerningPolicyWrite(const RoboTextKerningPolicy *policy,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength)
{
    char line[RoboTextKerningLineBytes];
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!policy || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    length = RoboTextWriterText(line, sizeof(line), length, RoboTextKerningFeatureTag);
    length = RoboTextWriterText(line, sizeof(line), length, "=");
    length = RoboTextWriterText(line, sizeof(line), length, RoboTextFeatureStateName(policy->kern));
    length = RoboTextWriterText(line, sizeof(line), length, " legacy=");
    length = RoboTextWriterText(line, sizeof(line), length, KerningLegacyName(policy->legacy));
    if (length == RoboTextWriterOverflow || length + 1u > capacity) {
        return RoboNotSupported;
    }
    memcpy(buffer, line, length);
    buffer[length] = '\0';
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

Result RoboTextKerningPolicyApply(const RoboTextKerningPolicy *policy, RoboTextFeatureSet *set)
{
    if (!policy || !set) {
        return RoboInvalidArgument;
    }
    if (policy->kern == RoboTextFeatureDefault) {
        /* "Nothing stated" is not a state a feature set can hold, and writing `On` in its place would be this
         * layer answering for the font. Nothing to write is not a failure. */
        return RoboOk;
    }
    return RoboTextFeatureSetSet(set, RoboTextKerningFeatureTag, policy->kern, 0u);
}

void RoboTextKerningPolicyApplyIfUnset(const RoboTextKerningPolicy *policy, RoboTextFeatureSet *set)
{
    if (!policy || !set || policy->kern == RoboTextFeatureDefault) {
        return;
    }
    /*
     * The caller's own word wins: `"kern" off` is a decision about this one tag, and a policy is a default
     * about it. Asking first is what makes the two layers composable instead of order-dependent.
     */
    if (RoboTextFeatureSetFind(set, RoboTextKerningFeatureTag)) {
        return;
    }
    (void)RoboTextFeatureSetSet(set, RoboTextKerningFeatureTag, policy->kern, 0u);
}

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

/** The `kern` feature of @p plan, or NULL when the plan did not enable it. */
static const RoboTextOtFeature *KerningFeatureOf(const RoboTextOtPlan *plan)
{
    if (!plan) {
        return NULL;
    }
    for (size_t index = 0u; index < plan->featureCount; index++) {
        /* Compared through the four-character spelling rather than by re-packing the constant: the plan's
         * `tag` is already the font's own bytes, and one spelling of `kern` in this module is the point. */
        if (strcmp(plan->features[index].tag, RoboTextKerningFeatureTag) == 0) {
            return &plan->features[index];
        }
    }
    return NULL;
}

/** Resolve the face's plan for @p features (NULL means the engine's own defaults). */
static Result KerningPlan(const RoboTextKerningRequest *request,
                          const RoboTextFeatureSet *features,
                          RoboTextOtPlan *out,
                          RoboTextDiag *diag)
{
    const FontOtLayout *gpos = NULL;
    Result status = RoboTextFontGpos(request->font, &gpos, diag);

    if (status != RoboOk) {
        return status;
    }
    return RoboTextGposPlanResolve(gpos, request->script, request->language, features, out, diag);
}

/** Whether the face ships a legacy `kern` table — a fact about the file, and nothing more. */
static int KerningLegacyTablePresent(const RoboTextKerningRequest *request)
{
    const FontFace *face = request->font ? request->font->face : NULL;
    const FontSfnt *sfnt = NULL;

    if (!face) {
        return 0;
    }
    if (FontFaceParseSfnt((FontFace *)face) != RoboOk) {
        return 0;
    }
    sfnt = FontFaceSfnt(face);
    return sfnt ? FontSfntHasTable(sfnt, RoboFontTagKern) : 0;
}

Result RoboTextKerningResolve(const RoboTextKerningRequest *request,
                              const RoboTextKerningPolicy *policy,
                              RoboTextKerningResolution *out,
                              RoboTextDiag *diag)
{
    RoboTextOtPlan effective;
    RoboTextOtPlan offered;
    RoboTextFeatureSet settings;
    const RoboTextOtFeature *feature;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainGpos, RoboInvalidArgument,
                               "an out resolution is required");
    }
    memset(out, 0, sizeof(*out));
    if (!request || !request->font || !request->font->face) {
        return RoboTextDiagMsg(diag, RoboTextDomainGpos, RoboInvalidArgument,
                               "a kerning request with a face is required");
    }
    if (policy && !RoboTextKerningPolicyIsValid(policy)) {
        return RoboTextDiagMsg(diag, RoboTextDomainGpos, RoboInvalidArgument,
                               "the kerning policy is not usable");
    }
    out->source = RoboTextKerningSourceNone;

    /*
     * Two questions, two resolutions. The *offer* is asked with the engine's own defaults, so a caller that
     * turned kerning off still learns whether the face carries it; what *applies* is asked with the caller's
     * settings, layer by layer — the policy first, because its whole job is to supply the tags the caller did
     * not state.
     */
    if (KerningPlan(request, NULL, &offered, diag) != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainGpos, RoboError,
                               "the face's positioning plan could not be resolved");
    }
    feature = KerningFeatureOf(&offered);
    out->fontHasKernFeature = feature ? 1 : 0;

    if (request->features) {
        settings = *request->features;
    } else {
        RoboTextFeatureSetInit(&settings);
    }
    RoboTextKerningPolicyApplyIfUnset(policy, &settings);

    if (KerningPlan(request, &settings, &effective, diag) != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainGpos, RoboError,
                               "the face's positioning plan could not be resolved");
    }
    feature = KerningFeatureOf(&effective);
    out->lookupCount = feature ? (size_t)feature->lookupIndexCount : 0u;

    /* `Off` is the only state that means "no": an unstated tag gets the engine's default, which includes
     * `kern`, and that is precisely what makes `font-kerning: auto` different from `font-kerning: none`. */
    out->enabled = (RoboTextKerningPolicyState(policy) == RoboTextFeatureOff) ? 0 : 1;
    {
        const RoboTextFeatureSetting *stated =
            RoboTextFeatureSetFind(&settings, RoboTextKerningFeatureTag);

        if (stated && stated->state == RoboTextFeatureOff) {
            out->enabled = 0;
        }
    }

    out->legacyTableAvailable = KerningLegacyTablePresent(request);
    if (!out->enabled) {
        return RoboOk;
    }
    if (out->lookupCount != 0u) {
        out->applies = 1;
        out->source = RoboTextKerningSourceGpos;
        return RoboOk;
    }
    /*
     * Nothing in GPOS answers. The legacy `kern` table would be the fallback — and it is a *fallback* rather
     * than an addition, which is the phase's non-double-counting rule — but no build yet reads that table, so
     * the honest answer is "the face has one and this build cannot use it" rather than a silently unkerned
     * run. `legacyTableAvailable` is where that shows.
     */
    return RoboOk;
}

Result RoboTextKerningResolutionWrite(const RoboTextKerningResolution *resolution,
                                      char *buffer,
                                      size_t capacity,
                                      size_t *outLength)
{
    char line[RoboTextKerningLineBytes];
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!resolution || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    length = RoboTextWriterText(line, sizeof(line), length, "kerning=");
    length = RoboTextWriterText(line, sizeof(line), length, resolution->enabled ? "on" : "off");
    length = RoboTextWriterText(line, sizeof(line), length, " source=");
    length = RoboTextWriterText(line, sizeof(line), length,
                                RoboTextKerningSourceName(resolution->source));
    length = RoboTextWriterText(line, sizeof(line), length, " lookups=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)resolution->lookupCount);
    length = RoboTextWriterText(line, sizeof(line), length, " face-kern=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length,
                                    (uint32_t)(resolution->fontHasKernFeature ? 1u : 0u));
    length = RoboTextWriterText(line, sizeof(line), length, " kern-table=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length,
                                    (uint32_t)(resolution->legacyTableAvailable ? 1u : 0u));
    if (length == RoboTextWriterOverflow || length + 1u > capacity) {
        return RoboNotSupported;
    }
    memcpy(buffer, line, length);
    buffer[length] = '\0';
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* What the font did                                                   */
/* ------------------------------------------------------------------ */

Result RoboTextKerningReportForRun(const RoboTextGlyphRun *run, RoboTextKerningReport *out)
{
    size_t count;
    int haveMin = 0;

    if (!run || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    count = RoboTextGlyphRunCount(run);
    out->glyphs = count;
    out->advance = RoboTextGlyphRunAdvance(run);
    for (size_t index = 0u; index < count; index++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)index);

        if (!glyph || !glyph->positioned) {
            continue;
        }
        out->positionedGlyphs++;
        if (glyph->advanceAdjustX == 0.0f && glyph->advanceAdjustY == 0.0f) {
            /* The font positioned this glyph and said nothing about its advance: a placement record, or a
             * pair whose value is zero. Counted as positioned, and not as an adjusted pair. */
            continue;
        }
        out->adjustedAdvances++;
        out->advanceSum += glyph->advanceAdjustX;
        if (!haveMin || glyph->advanceAdjustX < out->advanceMin) {
            out->advanceMin = glyph->advanceAdjustX;
            haveMin = 1;
        }
        if (glyph->positioned && index + 1u < count) {
            const RoboTextGlyph *next = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)(index + 1u));

            if (next && next->positioned) {
                out->adjustedPairs++;
            }
        }
    }
    return RoboOk;
}

Result RoboTextKerningReportWrite(const RoboTextKerningReport *report,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength)
{
    char line[RoboTextKerningLineBytes];
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!report || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    length = RoboTextWriterText(line, sizeof(line), length, "positioned=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)report->positionedGlyphs);
    length = RoboTextWriterText(line, sizeof(line), length, " advances=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)report->adjustedAdvances);
    length = RoboTextWriterText(line, sizeof(line), length, " pairs=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)report->adjustedPairs);
    length = RoboTextWriterText(line, sizeof(line), length, " sum=");
    length = RoboTextWriterFloat(line, sizeof(line), length, report->advanceSum);
    length = RoboTextWriterText(line, sizeof(line), length, " min=");
    length = RoboTextWriterFloat(line, sizeof(line), length, report->advanceMin);
    length = RoboTextWriterText(line, sizeof(line), length, " glyphs=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)report->glyphs);
    length = RoboTextWriterText(line, sizeof(line), length, " advance=");
    length = RoboTextWriterFloat(line, sizeof(line), length, (float)report->advance);
    if (length == RoboTextWriterOverflow || length + 1u > capacity) {
        return RoboNotSupported;
    }
    memcpy(buffer, line, length);
    buffer[length] = '\0';
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

Result RoboTextKerningAdjustmentAt(const RoboTextGlyphRun *run,
                                   size_t index,
                                   RoboTextKerningAdjustment *outAdjustment,
                                   RoboTextDiag *diag)
{
    size_t seen = 0u;
    size_t count;

    if (outAdjustment) {
        memset(outAdjustment, 0, sizeof(*outAdjustment));
    }
    if (!run || !outAdjustment) {
        return RoboTextDiagMsg(diag, RoboTextDomainGpos, RoboInvalidArgument,
                               "a glyph run and an out adjustment are required");
    }
    count = RoboTextGlyphRunCount(run);
    for (size_t position = 0u; position < count; position++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)position);
        const RoboTextGlyph *pair;

        if (!glyph || !glyph->positioned) {
            continue;
        }
        if (seen != index) {
            seen++;
            continue;
        }
        /*
         * The neighbour a pair record is stated against: the next glyph in the run. A glyph with nothing after
         * it answers with *itself*, which is the honest record — only a single adjustment can have moved it,
         * and there is no second glyph to name.
         */
        pair = (position + 1u < count) ? RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)(position + 1u))
                                       : glyph;
        outAdjustment->glyphIndex = (RoboTextGlyphIndex)position;
        outAdjustment->pairIndex = (RoboTextGlyphIndex)((position + 1u < count) ? position + 1u : position);
        outAdjustment->glyphId = (uint16_t)glyph->glyphId;
        outAdjustment->pairGlyphId = (uint16_t)pair->glyphId;
        outAdjustment->source = glyph->source;
        outAdjustment->xPlacement = glyph->offsetAdjustX;
        outAdjustment->yPlacement = glyph->offsetAdjustY;
        outAdjustment->xAdvance = glyph->advanceAdjustX;
        outAdjustment->yAdvance = glyph->advanceAdjustY;
        return RoboOk;
    }
    return RoboTextDiagMsg(diag, RoboTextDomainGpos, RoboNotFound,
                           "the run has no positioned glyph at that position");
}
