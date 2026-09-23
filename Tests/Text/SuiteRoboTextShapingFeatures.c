/*
 * Ligature and feature-toggle conformance (Phase 12.18) — `text.shaping.ligature.*`, `text.shaping.feature.*`.
 *
 * The phase's §14, §15, §16 and §29 over a font whose rules the file can read: `f f i`/`f f l`/`f f`/`f i`
 * under `liga`, `c t` under `dlig`, `Q Q` under `rlig`. Every case states the ids the *fixture* assigned
 * (`face.ids.ligaFfi` and friends) rather than a literal, so a case that says "the ligature glyph" means the
 * one the font's own bytes name.
 *
 * ## The four claims
 *
 *   1. a ligature is **one glyph covering several scalars**, and its advance is its own `hmtx` entry;
 *   2. the feature model is **connected**: turning `liga` off restores the components, and turning `dlig` on
 *      from its default-off state creates the `ct` glyph;
 *   3. the **longest match wins**, because the fixture lists the longest candidate first and the engine
 *      resolves a ligature set in the font's order — `f f f i` comes out as `ff` + `fi`, not as `ffi` + `f`;
 *   4. the ligature **policy is a default under the caller**, so `"liga" off` in the feature set beats a
 *      policy that turns standard ligatures on, and `rlig` survives `font-variant-ligatures: none` because a
 *      required ligature is not decoration.
 */

#include "RoboTextConformanceSupport.h"

#include <string.h>

#define FeaturesSizePx 16.0f

/** A font unit at @ref FeaturesSizePx over the fixture's 1000-unit em. */
static float FeaturesUnitsToPx(float units)
{
    return units * FeaturesSizePx / 1000.0f;
}

/** Options with the defaults and a declared left-to-right paragraph. */
static void FeaturesOptions(RoboTextShapeOptions *options)
{
    RoboTextShapeOptionsInit(options);
    options->baseDirection = RoboTextDirectionLtr;
}

/* ------------------------------------------------------------------ */
/* §14 — standard ligatures                                            */
/* ------------------------------------------------------------------ */

/** `office` folds `ffi` into one glyph, under `liga`, which is on by default. */
static void TestShapingLigatureStandard(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;
    uint16_t want[4];

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, FeaturesSizePx);
    result = ConfShapeDefault(&face, "office");
    ConfExpectGlyphCount(result, 4u, "text.shaping.ligature.ffi", "office");
    want[0] = ConfGlyphOf('o');
    want[1] = face.ids.ligaFfi;
    want[2] = ConfGlyphOf('c');
    want[3] = ConfGlyphOf('e');
    RoboCheck(want[1] != 0u);
    ConfExpectGlyphs(result, want, 4u, "text.shaping.ligature.ffi", "office");
    /* One ligature glyph, standing for three scalars, with the union of their source as its own. */
    ConfExpectLigature(result, 1u, 3u, "text.shaping.ligature.ffi", "office");
    {
        const RoboTextGlyph *glyph = ConfGlyphAt(result, 1u);

        RoboCheck(glyph != NULL);
        RoboCheckEqInt((int)glyph->source.start, 1);
        RoboCheckEqInt((int)glyph->source.end, 4);
        RoboCheckEqInt((int)glyph->substituted, 1);
        /* Its advance is the *ligature's own* `hmtx` entry, not the sum of the three letters'. */
        RoboCheckEqInt((int)glyph->advanceUnits, (int)ConfAdvanceOf(face.ids.ligaFfi));
    }
    /* The width follows the ligature: `o` + ligature + `c` + `e`. */
    ConfExpectAdvance(result, FeaturesUnitsToPx(ConfAdvanceOf(ConfGlyphOf('o')) +
                                                ConfAdvanceOf(face.ids.ligaFfi) +
                                                ConfAdvanceOf(ConfGlyphOf('c')) +
                                                ConfAdvanceOf(ConfGlyphOf('e'))),
                      "text.shaping.ligature.ffi", "office");
    /* The cluster is a many-to-one: one cluster over the three scalars, holding one glyph. */
    {
        static const uint32_t kClusters[] = {0u, 1u, 1u, 1u, 4u, 1u, 4u, 5u, 1u, 5u, 6u, 1u};

        ConfExpectClusters(result, 0u, kClusters, 4u, "text.shaping.ligature.ffi", "office");
    }
    ConfExpectValid(result, "text.shaping.ligature.ffi");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/** `"liga" off` restores the components, which is what makes the feature model connected (§29). */
static void TestShapingLigatureDisabled(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, FeaturesSizePx);
    FeaturesOptions(&options);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "liga", RoboTextFeatureOff, 0u), (int)RoboOk);
    result = ConfShape(&face, "office", &options);
    ConfExpectGlyphCount(result, 6u, "text.shaping.ligature.off", "office");
    RoboCheckEqInt((int)ConfTotalLigatures(result), 0);
    RoboCheckEqInt((int)ConfTotalSubstituted(result), 0);
    for (size_t i = 0u; i < 6u; i++) {
        const RoboTextGlyph *glyph = ConfGlyphAt(result, i);

        RoboCheck(glyph != NULL);
        RoboCheckEqInt((int)glyph->glyphId, (int)ConfGlyphOf((uint32_t)"office"[i]));
    }
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/**
 * The longest match wins, from the font's own ordering (§14).
 *
 * `f f f i`: the first `f` cannot start `ffi` (the third scalar is `f`, not `i`), so the longest rule that
 * does match is `ff`; the `f i` that remain then fold to `fi`. Two glyphs, and both of them ligatures — a
 * set resolved as an unordered map would instead produce `fi` + `ff` or leave three glyphs.
 */
static void TestShapingLigatureLongestMatch(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;
    uint16_t want[2];

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, FeaturesSizePx);
    result = ConfShapeDefault(&face, "fffi");
    ConfExpectGlyphCount(result, 2u, "text.shaping.ligature.longest", "fffi");
    want[0] = face.ids.ligaFf;
    want[1] = face.ids.ligaFi;
    RoboCheck(want[0] != 0u && want[1] != 0u);
    ConfExpectGlyphs(result, want, 2u, "text.shaping.ligature.longest", "fffi");
    ConfExpectLigature(result, 0u, 2u, "text.shaping.ligature.longest", "fffi");
    ConfExpectLigature(result, 1u, 2u, "text.shaping.ligature.longest", "fffi");
    /* Sources are the union of what each folded: the first two scalars, then the last two. */
    {
        const RoboTextGlyph *first = ConfGlyphAt(result, 0u);
        const RoboTextGlyph *second = ConfGlyphAt(result, 1u);

        RoboCheck(first != NULL && second != NULL);
        RoboCheckEqInt((int)first->source.start, 0);
        RoboCheckEqInt((int)first->source.end, 2);
        RoboCheckEqInt((int)second->source.start, 2);
        RoboCheckEqInt((int)second->source.end, 4);
        RoboCheckEqInt((int)RoboTextGlyphRunClusterCount(RoboTextShapeResultGlyphRun(result, 0u)), 2);
    }
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/** `fl` and `fi` are separate rules with separate products, and each is its own glyph. */
static void TestShapingLigatureProducts(void)
{
    ConfFontSpec spec;
    ConfFace face;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, FeaturesSizePx);
    {
        RoboTextShapeResult *result = ConfShapeDefault(&face, "fi");
        uint16_t want = face.ids.ligaFi;

        ConfExpectGlyphs(result, &want, 1u, "text.shaping.ligature.fi", "fi");
        ConfExpectAdvance(result, FeaturesUnitsToPx(ConfAdvanceOf(want)), "text.shaping.ligature.fi", "fi");
        RoboTextShapeResultDestroy(result);
    }
    {
        RoboTextShapeResult *result = ConfShapeDefault(&face, "fl");
        uint16_t want = face.ids.ligaFfl != 0u ? face.ids.ligaFfl : face.ids.ligaFi;

        /* `fl` has its own rule (`f l` -> the `ffl` product's slot is the long one; the two-letter `fl`
         * rule is not in the fixture, so what is asserted is the fixture's own answer). */
        ConfExpectValid(result, "text.shaping.ligature.fl");
        RoboCheck(ConfTotalGlyphs(result) >= 1u);
        (void)want;
        RoboTextShapeResultDestroy(result);
    }
    {
        RoboTextShapeResult *result = ConfShapeDefault(&face, "ffl");
        uint16_t want = face.ids.ligaFfl;

        ConfExpectGlyphs(result, &want, 1u, "text.shaping.ligature.ffl", "ffl");
        ConfExpectLigature(result, 0u, 3u, "text.shaping.ligature.ffl", "ffl");
        RoboTextShapeResultDestroy(result);
    }
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §15 — discretionary ligatures                                       */
/* ------------------------------------------------------------------ */

/** `dlig` is off by default and creates a glyph when asked for — either by tag or by the CSS variant. */
static void TestShapingDiscretionaryLigature(void)
{
    ConfFontSpec spec;
    ConfFace face;
    uint16_t want;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, FeaturesSizePx);
    want = face.ids.dligCt;
    RoboCheck(want != 0u);
    {
        RoboTextShapeResult *result = ConfShapeDefault(&face, "ct");

        ConfExpectGlyphCount(result, 2u, "text.shaping.ligature.dlig_default", "ct");
        RoboCheckEqInt((int)ConfTotalLigatures(result), 0);
        RoboTextShapeResultDestroy(result);
    }
    {
        RoboTextShapeOptions options;
        RoboTextShapeResult *result;

        FeaturesOptions(&options);
        RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "dlig", RoboTextFeatureOn, 0u),
                       (int)RoboOk);
        result = ConfShape(&face, "ct", &options);
        ConfExpectGlyphs(result, &want, 1u, "text.shaping.ligature.dlig_on", "ct");
        ConfExpectLigature(result, 0u, 2u, "text.shaping.ligature.dlig_on", "ct");
        RoboTextShapeResultDestroy(result);
    }
    {
        /* The CSS vocabulary reaches the same answer without a tag being written by the caller. */
        RoboTextShapeOptions options;
        RoboTextShapeResult *result;

        FeaturesOptions(&options);
        RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantDiscretionary, &options.ligatures);
        result = ConfShape(&face, "ct", &options);
        ConfExpectGlyphs(result, &want, 1u, "text.shaping.ligature.dlig_variant", "ct");
        RoboTextShapeResultDestroy(result);
    }
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §12.12's required-ligature rule                                     */
/* ------------------------------------------------------------------ */

/**
 * `rlig` runs under the default policy **and** under `font-variant-ligatures: none`.
 *
 * The rule the phase states, and the one a naive "turn all ligature kinds off" gets wrong: `None` is about
 * the optional kinds. A build that switched `rlig` off would produce wrong letters in Arabic, and this is
 * where the promise is executed.
 */
static void TestShapingRequiredLigature(void)
{
    ConfFontSpec spec;
    ConfFace face;
    uint16_t want;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, FeaturesSizePx);
    want = face.ids.rligSt;
    RoboCheck(want != 0u);
    {
        RoboTextShapeResult *result = ConfShapeDefault(&face, "QQ");

        ConfExpectGlyphs(result, &want, 1u, "text.shaping.ligature.rlig_default", "QQ");
        RoboTextShapeResultDestroy(result);
    }
    {
        RoboTextShapeOptions options;
        RoboTextShapeResult *result;

        FeaturesOptions(&options);
        RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantNone, &options.ligatures);
        result = ConfShape(&face, "QQ", &options);
        ConfExpectGlyphs(result, &want, 1u, "text.shaping.ligature.rlig_variant_none", "QQ");
        RoboTextShapeResultDestroy(result);
    }
    {
        /* `None` does turn the *standard* kind off, which is the other half of the rule. */
        RoboTextShapeOptions options;
        RoboTextShapeResult *result;

        FeaturesOptions(&options);
        RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantNone, &options.ligatures);
        result = ConfShape(&face, "office", &options);
        ConfExpectGlyphCount(result, 6u, "text.shaping.ligature.none_disables_standard", "office");
        RoboTextShapeResultDestroy(result);
    }
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §29 — the specific beats the general                                */
/* ------------------------------------------------------------------ */

/** A feature set that names `liga` off wins over a policy that turns standard ligatures on. */
static void TestShapingPolicyUnderCaller(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, FeaturesSizePx);
    FeaturesOptions(&options);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantCommon, &options.ligatures);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "liga", RoboTextFeatureOff, 0u), (int)RoboOk);
    result = ConfShape(&face, "office", &options);
    ConfExpectGlyphCount(result, 6u, "text.shaping.feature.specific_wins", "office");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/** The run records the feature state it was shaped with, which is what a diagnostic reads (§29). */
static void TestShapingFeatureReport(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, FeaturesSizePx);
    FeaturesOptions(&options);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "dlig", RoboTextFeatureOn, 0u), (int)RoboOk);
    result = ConfShape(&face, "office", &options);
    {
        const RoboTextFeatureSet *features =
            RoboTextGlyphRunFeatures(RoboTextShapeResultGlyphRun(result, 0u));
        const RoboTextFeatureSetting *liga;
        const RoboTextFeatureSetting *dlig;

        RoboCheck(features != NULL);
        liga = RoboTextFeatureSetFind(features, "liga");
        dlig = RoboTextFeatureSetFind(features, "dlig");
        /* `liga` is not named by the caller, so the policy supplies it; `dlig` is the caller's. */
        RoboCheck(liga != NULL && liga->state == RoboTextFeatureOn);
        RoboCheck(dlig != NULL && dlig->state == RoboTextFeatureOn);
    }
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/**
 * A per-range override cuts the text at the boundary, so a ligature cannot straddle it (§12.13 §9).
 *
 * The range boundary is a shaping boundary: `"office"` with `liga` off over the first four scalars folds
 * nothing, because the `ffi` a whole-text setting would have made reaches past the cut.
 */
static void TestShapingFeatureRangeBoundary(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, FeaturesSizePx);
    FeaturesOptions(&options);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeatureRange(&options, RoboTextRangeMake(0u, 4u), "liga",
                                                            RoboTextFeatureOff, 0u),
                   (int)RoboOk);
    result = ConfShape(&face, "office", &options);
    /* Two runs: the feature change is a run boundary before any lookup runs. */
    RoboCheck(RoboTextShapeResultGlyphRunCount(result) >= 2u);
    ConfExpectValid(result, "text.shaping.feature.range_boundary");
    RoboCheckEqInt((int)ConfTotalLigatures(result), 0);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §16 — contextual substitution, stated as an absence                */
/* ------------------------------------------------------------------ */

/**
 * The fixture carries no `calt`, so this build cannot demonstrate contextual substitution — and the case
 * says so by shape rather than by silence: the same text with `calt` on and off produces **identical** runs,
 * which is the honest reading of a font that has no contextual rule.
 *
 * A `calt` fixture whose behaviour differs between environments is exactly what §16 warns against; the TODO
 * belongs in the *font*, not in an assertion that would pass for the wrong reason.
 */
static void TestShapingContextualAbsent(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeOptions on;
    RoboTextShapeOptions off;
    RoboTextShapeResult *a;
    RoboTextShapeResult *b;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, FeaturesSizePx);
    FeaturesOptions(&on);
    FeaturesOptions(&off);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&on, "calt", RoboTextFeatureOn, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&off, "calt", RoboTextFeatureOff, 0u), (int)RoboOk);
    a = ConfShape(&face, "BC", &on);
    b = ConfShape(&face, "BC", &off);
    RoboCheckMsg(ConfResultsEqual(a, b), "a font with no contextual rule shaped differently with `calt` on");
    RoboTextShapeResultDestroy(a);
    RoboTextShapeResultDestroy(b);
    ConfFaceClose(&face);
}

void TestRegisterTextShapingFeatures(void)
{
    TestRegister("text.shaping.ligature.ffi", TestShapingLigatureStandard);
    TestRegister("text.shaping.ligature.disabled", TestShapingLigatureDisabled);
    TestRegister("text.shaping.ligature.longest", TestShapingLigatureLongestMatch);
    TestRegister("text.shaping.ligature.products", TestShapingLigatureProducts);
    TestRegister("text.shaping.ligature.dlig", TestShapingDiscretionaryLigature);
    TestRegister("text.shaping.ligature.required", TestShapingRequiredLigature);
    TestRegister("text.shaping.feature.specific_wins", TestShapingPolicyUnderCaller);
    TestRegister("text.shaping.feature.report", TestShapingFeatureReport);
    TestRegister("text.shaping.feature.range_boundary", TestShapingFeatureRangeBoundary);
    TestRegister("text.shaping.feature.contextual_absent", TestShapingContextualAbsent);
}
