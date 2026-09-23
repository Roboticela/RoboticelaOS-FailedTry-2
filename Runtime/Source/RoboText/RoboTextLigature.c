/*
 * Ligatures (Phase 12.12) — see `RoboTextLigature.h` for what this layer is and is not.
 *
 * Two tables and one walk. The tables are the kind↔tag mapping and the variant→policy translation, both
 * written out in full because they are the module's *vocabulary*: a reader should be able to check
 * `hlig`↔`historical` by reading, not by running. The walk is `RoboTextLigatureReportForRun`, which asks the
 * glyph run what the font produced.
 *
 * `RoboTextLigaturePolicyApplyIfUnset` is the one function with a rule worth stating twice: it exists so a
 * policy cannot silently overwrite a caller's explicit setting, and it is why the default policy can live in
 * `RoboTextShapeOptionsInit` without breaking `"liga" off`.
 */
#include "RoboText/RoboTextLigature.h"

#include "RoboTextInternal.h"

#include <string.h>

/** Biggest line a dump in this file builds before it is copied out. */
#define RoboTextLigatureLineBytes 160u

/* ------------------------------------------------------------------ */
/* Kinds                                                               */
/* ------------------------------------------------------------------ */

/** One row per kind, in `RoboTextLigatureKind` order: the name a dump prints and the tag GSUB reads. */
static const struct {
    const char *name;
    const char *tag;
} kLigatureKinds[RoboTextLigatureKindCount] = {
    {"standard", "liga"},
    {"contextual", "clig"},
    {"required", "rlig"},
    {"discretionary", "dlig"},
    {"historical", "hlig"},
};

int RoboTextLigatureKindIsKnown(RoboTextLigatureKind kind)
{
    return (int)kind >= 0 && (int)kind < (int)RoboTextLigatureKindCount;
}

const char *RoboTextLigatureKindName(RoboTextLigatureKind kind)
{
    if (!RoboTextLigatureKindIsKnown(kind)) {
        return "?";
    }
    return kLigatureKinds[(size_t)kind].name;
}

const char *RoboTextLigatureKindTag(RoboTextLigatureKind kind)
{
    if (!RoboTextLigatureKindIsKnown(kind)) {
        return "";
    }
    return kLigatureKinds[(size_t)kind].tag;
}

int RoboTextLigatureKindForTag(const char *tag, RoboTextLigatureKind *outKind)
{
    if (!tag) {
        return 0;
    }
    for (size_t index = 0u; index < RoboTextLigatureKindCount; index++) {
        if (strcmp(tag, kLigatureKinds[index].tag) == 0) {
            if (outKind) {
                *outKind = (RoboTextLigatureKind)index;
            }
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Variants                                                            */
/* ------------------------------------------------------------------ */

static const struct {
    const char *name;
    RoboTextLigatureKind kind;
    RoboTextFeatureState state;
} kLigatureVariants[RoboTextLigatureVariantCount] = {
    /* `normal` states nothing: it is the font's own answer, exactly as an unmentioned policy is. */
    {"normal", RoboTextLigatureKindCount, RoboTextFeatureDefault},
    /* `none` is not "everything off": it turns the *optional* kinds off and says nothing about `rlig`. */
    {"none", RoboTextLigatureKindCount, RoboTextFeatureOff},
    {"common-ligatures", RoboTextLigatureStandard, RoboTextFeatureOn},
    {"no-common-ligatures", RoboTextLigatureStandard, RoboTextFeatureOff},
    {"contextual", RoboTextLigatureContextual, RoboTextFeatureOn},
    {"no-contextual", RoboTextLigatureContextual, RoboTextFeatureOff},
    {"discretionary-ligatures", RoboTextLigatureDiscretionary, RoboTextFeatureOn},
    {"historical-ligatures", RoboTextLigatureHistorical, RoboTextFeatureOn},
};

int RoboTextLigatureVariantIsKnown(RoboTextLigatureVariant variant)
{
    return (int)variant >= 0 && (int)variant < (int)RoboTextLigatureVariantCount;
}

const char *RoboTextLigatureVariantName(RoboTextLigatureVariant variant)
{
    if (!RoboTextLigatureVariantIsKnown(variant)) {
        return "?";
    }
    return kLigatureVariants[(size_t)variant].name;
}

void RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariant variant, RoboTextLigaturePolicy *out)
{
    if (!out || !RoboTextLigatureVariantIsKnown(variant)) {
        return;
    }
    if (variant == RoboTextLigatureVariantNone) {
        /*
         * Every optional kind off, `rlig` untouched — and the *required* kind is untouched by name rather
         * than by luck: an Arabic or Indic face whose `rlig` a "no ligatures" declaration switched off would
         * produce wrong letters, which is a different failure from an absent `fi`.
         */
        RoboTextLigaturePolicySet(out, RoboTextLigatureStandard, RoboTextFeatureOff);
        RoboTextLigaturePolicySet(out, RoboTextLigatureContextual, RoboTextFeatureOff);
        RoboTextLigaturePolicySet(out, RoboTextLigatureDiscretionary, RoboTextFeatureOff);
        RoboTextLigaturePolicySet(out, RoboTextLigatureHistorical, RoboTextFeatureOff);
        return;
    }
    if (variant == RoboTextLigatureVariantNormal) {
        /* Nothing stated. A caller that had stated something keeps it: this variant is a no-op by design. */
        return;
    }
    RoboTextLigaturePolicySet(out, kLigatureVariants[(size_t)variant].kind,
                              kLigatureVariants[(size_t)variant].state);
}

/* ------------------------------------------------------------------ */
/* Policy                                                              */
/* ------------------------------------------------------------------ */

void RoboTextLigaturePolicyInit(RoboTextLigaturePolicy *policy)
{
    if (!policy) {
        return;
    }
    for (size_t index = 0u; index < RoboTextLigatureKindCount; index++) {
        policy->kinds[index] = RoboTextFeatureDefault;
    }
}

void RoboTextLigaturePolicyDefault(RoboTextLigaturePolicy *policy)
{
    if (!policy) {
        return;
    }
    RoboTextLigaturePolicyInit(policy);
    policy->kinds[RoboTextLigatureStandard] = RoboTextFeatureOn;
    policy->kinds[RoboTextLigatureContextual] = RoboTextFeatureOn;
    policy->kinds[RoboTextLigatureRequired] = RoboTextFeatureOn;
    policy->kinds[RoboTextLigatureDiscretionary] = RoboTextFeatureOff;
    policy->kinds[RoboTextLigatureHistorical] = RoboTextFeatureOff;
}

Result RoboTextLigaturePolicySet(RoboTextLigaturePolicy *policy,
                                 RoboTextLigatureKind kind,
                                 RoboTextFeatureState state)
{
    if (!policy || !RoboTextLigatureKindIsKnown(kind)) {
        return RoboInvalidArgument;
    }
    if (!RoboTextFeatureStateIsKnown(state)) {
        return RoboInvalidArgument;
    }
    policy->kinds[(size_t)kind] = state;
    return RoboOk;
}

RoboTextFeatureState RoboTextLigaturePolicyState(const RoboTextLigaturePolicy *policy,
                                                 RoboTextLigatureKind kind)
{
    if (!policy || !RoboTextLigatureKindIsKnown(kind)) {
        return RoboTextFeatureDefault;
    }
    return policy->kinds[(size_t)kind];
}

int RoboTextLigaturePolicyIsValid(const RoboTextLigaturePolicy *policy)
{
    if (!policy) {
        return 0;
    }
    for (size_t index = 0u; index < RoboTextLigatureKindCount; index++) {
        if (!RoboTextFeatureStateIsKnown(policy->kinds[index])) {
            return 0;
        }
    }
    return 1;
}

int RoboTextLigaturePolicyEqual(const RoboTextLigaturePolicy *a, const RoboTextLigaturePolicy *b)
{
    if (!a || !b) {
        return a == b;
    }
    for (size_t index = 0u; index < RoboTextLigatureKindCount; index++) {
        if (a->kinds[index] != b->kinds[index]) {
            return 0;
        }
    }
    return 1;
}

Result RoboTextLigaturePolicyWrite(const RoboTextLigaturePolicy *policy,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength)
{
    char line[RoboTextLigatureLineBytes];
    size_t length = 0u;
    int stated = 0;

    if (outLength) {
        *outLength = 0u;
    }
    if (!policy || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    /* Into a scratch line first: a dump that does not fit is abandoned whole rather than truncated, which
     * is the rule the module's writer exists to enforce. */
    for (size_t index = 0u; index < RoboTextLigatureKindCount; index++) {
        if (policy->kinds[index] == RoboTextFeatureDefault) {
            continue;
        }
        if (stated) {
            length = RoboTextWriterText(line, sizeof(line), length, " ");
        }
        length = RoboTextWriterText(line, sizeof(line), length, kLigatureKinds[index].tag);
        length = RoboTextWriterText(line, sizeof(line), length, "=");
        length = RoboTextWriterText(line, sizeof(line), length,
                                    RoboTextFeatureStateName(policy->kinds[index]));
        stated = 1;
        if (length == RoboTextWriterOverflow) {
            break;
        }
    }
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    if (!stated) {
        /* Everything is `Default`: say so rather than answering an empty line, which a caller would print
         * as nothing at all. */
        length = RoboTextWriterText(line, sizeof(line), 0u, "(nothing stated)");
    }
    if (length + 1u > capacity) {
        return RoboNotSupported;
    }
    memcpy(buffer, line, length);
    buffer[length] = '\0';
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

Result RoboTextLigaturePolicyApply(const RoboTextLigaturePolicy *policy, RoboTextFeatureSet *set)
{
    if (!policy || !set) {
        return RoboInvalidArgument;
    }
    for (size_t index = 0u; index < RoboTextLigatureKindCount; index++) {
        Result status;

        if (policy->kinds[index] == RoboTextFeatureDefault) {
            continue;
        }
        status = RoboTextFeatureSetSet(set, kLigatureKinds[index].tag, policy->kinds[index], 0u);
        if (status != RoboOk) {
            return status;
        }
    }
    return RoboOk;
}

void RoboTextLigaturePolicyApplyIfUnset(const RoboTextLigaturePolicy *policy, RoboTextFeatureSet *set)
{
    if (!policy || !set) {
        return;
    }
    for (size_t index = 0u; index < RoboTextLigatureKindCount; index++) {
        if (policy->kinds[index] == RoboTextFeatureDefault) {
            continue;
        }
        if (RoboTextFeatureSetFind(set, kLigatureKinds[index].tag) != NULL) {
            /* The caller named this tag: a default about five tags does not overrule a decision about
             * one. */
            continue;
        }
        (void)RoboTextFeatureSetSet(set, kLigatureKinds[index].tag, policy->kinds[index], 0u);
    }
}

/* ------------------------------------------------------------------ */
/* What the font did                                                   */
/* ------------------------------------------------------------------ */

Result RoboTextLigatureReportForRun(const RoboTextGlyphRun *run, RoboTextLigatureReport *out)
{
    if (!run || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->glyphs = RoboTextGlyphRunCount(run);
    out->scalars = RoboTextGlyphRunScalarCount(run);
    out->substitutedGlyphs = RoboTextGlyphRunSubstituted(run);
    for (size_t index = 0u; index < out->glyphs; index++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)index);
        size_t components;

        if (!glyph) {
            continue;
        }
        components = (size_t)glyph->ligatureComponents;
        if (components < 2u) {
            continue;
        }
        out->ligatureGlyphs++;
        out->absorbedScalars += (size_t)RoboTextRangeLength(glyph->source);
        if (components > out->maxComponents) {
            out->maxComponents = components;
        }
    }
    return RoboOk;
}

Result RoboTextGlyphRunLigatureAt(const RoboTextGlyphRun *run,
                                  size_t index,
                                  RoboTextGlyphIndex *outGlyphIndex,
                                  RoboTextDiag *diag)
{
    size_t seen = 0u;

    if (outGlyphIndex) {
        *outGlyphIndex = 0u;
    }
    if (!run || !outGlyphIndex) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a glyph run and an out index are required");
    }
    for (size_t position = 0u; position < RoboTextGlyphRunCount(run); position++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)position);

        if (!glyph || glyph->ligatureComponents < 2u) {
            continue;
        }
        if (seen == index) {
            *outGlyphIndex = (RoboTextGlyphIndex)position;
            return RoboOk;
        }
        seen++;
    }
    return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboNotFound,
                           "the run has no ligature glyph at that position");
}

Result RoboTextLigatureReportWrite(const RoboTextLigatureReport *report,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength)
{
    char line[RoboTextLigatureLineBytes];
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!report || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    length = RoboTextWriterText(line, sizeof(line), length, "ligatures=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)report->ligatureGlyphs);
    length = RoboTextWriterText(line, sizeof(line), length, " absorbed=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)report->absorbedScalars);
    length = RoboTextWriterText(line, sizeof(line), length, " max=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)report->maxComponents);
    length = RoboTextWriterText(line, sizeof(line), length, " glyphs=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)report->glyphs);
    length = RoboTextWriterText(line, sizeof(line), length, " scalars=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)report->scalars);
    length = RoboTextWriterText(line, sizeof(line), length, " substituted=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)report->substitutedGlyphs);
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
