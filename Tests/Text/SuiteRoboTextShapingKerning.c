/*
 * Kerning conformance (Phase 12.18) — `text.shaping.kerning.*`.
 *
 * The phase's §17, §18 and §36 over a fixture whose kerning the file can state exactly: the `kern` feature
 * carries one explicit pair, `A` against `V`, moved by **-80 font units**. At 16px over a 1000-unit em that
 * is -1.28 device pixels, and every expectation below is written from those two numbers rather than from
 * whatever the face produced — the subtraction is the thing the case exists to prove.
 *
 * ## The three claims
 *
 *   1. **the adjustment lands**, and it lands on the *first* glyph of the pair (the fixture's value record is
 *      format 1 with `xAdvance`, which belongs to the covered glyph);
 *   2. **it is separable from substitution** (§18): `AV` is two glyphs that nothing substituted, one of which
 *      the font positioned. A GSUB-only fixture cannot make that distinction and a GPOS-only one cannot show
 *      the absence, so both counts are asserted on the same run;
 *   3. **the width follows** (§36): the measured width of the run equals the sum of the glyphs' own advances,
 *      which is what makes "the pen and the measurer agree" a checkable statement rather than a hope.
 */

#include "RoboTextConformanceSupport.h"

#define KernSizePx 16.0f

/** A font unit at @ref KernSizePx over the fixture's 1000-unit em. */
static float KernUnitsToPx(float units)
{
    return units * KernSizePx / 1000.0f;
}

/** The fixture's stated pair adjustment, in font units. Kept beside the assertion that uses it. */
#define KernPairUnits (-80)

/** Options with the defaults and a declared left-to-right paragraph. */
static void KernOptions(RoboTextShapeOptions *options)
{
    RoboTextShapeOptionsInit(options);
    options->baseDirection = RoboTextDirectionLtr;
}

/* ------------------------------------------------------------------ */
/* §17 — the pair moves                                                 */
/* ------------------------------------------------------------------ */

/** `AV` is kerned by the font's own value, on the first glyph's advance. */
static void TestShapingKernPair(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;
    float plain;
    float kerned;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, KernSizePx);
    plain = KernUnitsToPx((float)ConfAdvanceOf(ConfGlyphOf('A')) + (float)ConfAdvanceOf(ConfGlyphOf('V')));
    kerned = KernUnitsToPx((float)ConfAdvanceOf(ConfGlyphOf('A')) + (float)ConfAdvanceOf(ConfGlyphOf('V')) +
                           (float)KernPairUnits);
    result = ConfShapeDefault(&face, "AV");
    ConfExpectGlyphCount(result, 2u, "text.shaping.kerning.pair", "AV");
    ConfExpectAdvance(result, kerned, "text.shaping.kerning.pair", "AV");
    {
        const RoboTextGlyph *a = ConfGlyphAt(result, 0u);
        const RoboTextGlyph *v = ConfGlyphAt(result, 1u);

        RoboCheck(a != NULL && v != NULL);
        /* The two halves of the advance are kept apart, so a diagnostic can say what the font did. */
        ConfExpectFloatClose(a->advanceAdjustX, KernUnitsToPx((float)KernPairUnits),
                             "text.shaping.kerning.pair", "adjustment on the first glyph");
        ConfExpectFloatClose(v->advanceAdjustX, 0.0f, "text.shaping.kerning.pair",
                             "adjustment on the second glyph");
        ConfExpectFloatClose(a->advanceX,
                             KernUnitsToPx((float)ConfAdvanceOf(ConfGlyphOf('A'))) +
                                 KernUnitsToPx((float)KernPairUnits),
                             "text.shaping.kerning.pair", "the first glyph's total advance");
        /* "the font said 0" and "the font said nothing" are different facts, and `positioned` is the one
         * that tells them apart — the second glyph's adjustment is zero and it is still not unpositioned. */
        RoboCheck(a->positioned != 0);
        RoboCheckEqInt((int)ConfTotalPositioned(result) >= 1, 1);
        RoboCheck(a->advanceAdjustX != 0.0f);
        RoboCheck(v->advanceAdjustX == 0.0f);
    }
    RoboTextShapeResultDestroy(result);
    /* The same two letters with the pair broken up measure the un-kerned width, which is the proof that the
     * difference came from the pair and not from the letters. */
    {
        RoboTextShapeResult *broken = ConfShapeDefault(&face, "A V");

        ConfExpectAdvance(broken, plain + KernUnitsToPx((float)ConfAdvanceOf(ConfGlyphOf(' '))),
                          "text.shaping.kerning.pair", "A V");
        RoboTextShapeResultDestroy(broken);
    }
    ConfFaceClose(&face);
}

/** A pair the font says nothing about is not moved: the adjustment is data, not a property of pairs. */
static void TestShapingKernNotAPair(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;
    float plain;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, KernSizePx);
    plain = KernUnitsToPx((float)ConfAdvanceOf(ConfGlyphOf('A')) + (float)ConfAdvanceOf(ConfGlyphOf('B')));
    result = ConfShapeDefault(&face, "AB");
    ConfExpectGlyphCount(result, 2u, "text.shaping.kerning.not_a_pair", "AB");
    ConfExpectAdvance(result, plain, "text.shaping.kerning.not_a_pair", "AB");
    RoboCheckEqInt((int)ConfTotalPositioned(result), 0);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §17 — `kern` is a feature, and `off` is the caller's word           */
/* ------------------------------------------------------------------ */

/** `"kern" off` restores the un-kerned advance, through the same door every other tag uses. */
static void TestShapingKernDisabled(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;
    float plain;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, KernSizePx);
    plain = KernUnitsToPx((float)ConfAdvanceOf(ConfGlyphOf('A')) + (float)ConfAdvanceOf(ConfGlyphOf('V')));
    KernOptions(&options);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "kern", RoboTextFeatureOff, 0u), (int)RoboOk);
    result = ConfShape(&face, "AV", &options);
    ConfExpectAdvance(result, plain, "text.shaping.kerning.disabled", "AV");
    ConfExpectGlyphCount(result, 2u, "text.shaping.kerning.disabled", "AV");
    RoboCheckEqInt((int)ConfTotalPositioned(result), 0);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §18 — positioning is not substitution                                */
/* ------------------------------------------------------------------ */

/** The same run carries no substitution and at least one positioning, and says so in its mode. */
static void TestShapingKernIsNotSubstitution(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, KernSizePx);
    result = ConfShapeDefault(&face, "AV");
    RoboCheckEqInt((int)ConfTotalSubstituted(result), 0);
    RoboCheckEqInt((int)ConfTotalLigatures(result), 0);
    RoboCheck(ConfTotalPositioned(result) >= 1u);
    {
        const RoboTextGlyphRun *run = RoboTextShapeResultGlyphRun(result, 0u);

        /* The mode is the furthest stage that ran, which is the *fact* a reader wants and the reason it is a
         * value rather than a comment: `Gpos` says the cmap mapped and the font positioned, and that no
         * substitution happened is told by the counters above. */
        RoboCheckEqInt((int)RoboTextGlyphRunGenerationMode(run), (int)RoboTextGlyphGenerationGpos);
    }
    /* The ids are the cmap's own, so the pair did not become a glyph of its own. */
    {
        uint16_t want[2];

        want[0] = ConfGlyphOf('A');
        want[1] = ConfGlyphOf('V');
        ConfExpectGlyphs(result, want, 2u, "text.shaping.kerning.is_not_substitution", "AV");
    }
    ConfExpectValid(result, "text.shaping.kerning.is_not_substitution");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §36 — measurement agrees with the shaped advances                    */
/* ------------------------------------------------------------------ */

/** The measurement's width is the run's own advance, kerned or not — one implementation, two readers. */
static void TestShapingKernMeasurement(void)
{
    ConfFontSpec spec;
    ConfFace face;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, KernSizePx);
    {
        static const char *const kTexts[] = {"AV", "AVATAR", "The quick brown fox.", "officially"};
        RoboTextShapeOptions options;

        KernOptions(&options);
        for (size_t i = 0u; i < sizeof(kTexts) / sizeof(kTexts[0]); i++) {
            RoboTextShapeResult *result = ConfShape(&face, kTexts[i], &options);
            const RoboTextMeasurement *measurement = RoboTextShapeResultMeasurement(result);

            RoboCheck(measurement != NULL);
            ConfExpectFloatClose(measurement->width, ConfTotalAdvance(result),
                                 "text.shaping.kerning.measurement", kTexts[i]);
            /* And the measure door agrees with the result's own measurement, which is the 12.15 promise. */
            {
                RoboTextMeasurement again;

                RoboCheckEqInt((int)RoboTextShapeResultMeasure(result, NULL, &again, NULL), (int)RoboOk);
                ConfExpectFloatClose(again.width, measurement->width, "text.shaping.kerning.measurement",
                                     kTexts[i]);
                RoboCheckEqInt((int)again.glyphCount, (int)measurement->glyphCount);
            }
            RoboTextShapeResultDestroy(result);
        }
    }
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §12.13 — a range boundary is a shaping boundary                      */
/* ------------------------------------------------------------------ */

/** `kern` off over the first scalar of `AV` leaves the pair un-kerned, because the boundary cuts it. */
static void TestShapingKernRangeOverride(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;
    float plain;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, KernSizePx);
    plain = KernUnitsToPx((float)ConfAdvanceOf(ConfGlyphOf('A')) + (float)ConfAdvanceOf(ConfGlyphOf('V')));
    KernOptions(&options);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeatureRange(&options, RoboTextRangeMake(0u, 1u), "kern",
                                                            RoboTextFeatureOff, 0u),
                   (int)RoboOk);
    result = ConfShape(&face, "AV", &options);
    /* The plan cannot pair a glyph whose run says `kern` off with one whose run says nothing, so the pair
     * does not apply — the honest consequence of cutting before shaping (§12.13 §9). */
    ConfExpectAdvance(result, plain, "text.shaping.kerning.range_override", "AV");
    RoboCheckEqInt((int)ConfTotalPositioned(result), 0);
    ConfExpectValid(result, "text.shaping.kerning.range_override");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §12.11/12.13 — kerning after a ligature lies on the ligature          */
/* ------------------------------------------------------------------ */

/**
 * The pair is stated against the **ligature's** glyph id, and the ligature's own id is what the pair sees.
 *
 * This is the order the pipeline promises — GSUB first, GPOS on the result — and it is observable on this
 * fixture only because the same face carries both a ligature and a pair. The fixture's pair is `A V`, so the
 * case states the negative half as well: `ffi V` does **not** move, because the font kerns `A V` and not
 * `ffi V`, which is the proof that the adjustment is the font's data and not a heuristic on the pair's shape.
 */
static void TestShapingKernAfterLigature(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;
    float plain;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, KernSizePx);
    plain = KernUnitsToPx((float)ConfAdvanceOf(face.ids.ligaFfi) + (float)ConfAdvanceOf(ConfGlyphOf('V')));
    result = ConfShapeDefault(&face, "ffiV");
    ConfExpectGlyphCount(result, 2u, "text.shaping.kerning.after_ligature", "ffiV");
    {
        const RoboTextGlyph *ligature = ConfGlyphAt(result, 0u);

        RoboCheck(ligature != NULL);
        RoboCheckEqInt((int)ligature->glyphId, (int)face.ids.ligaFfi);
        ConfExpectFloatClose(ligature->advanceX, KernUnitsToPx((float)ConfAdvanceOf(face.ids.ligaFfi)),
                             "text.shaping.kerning.after_ligature", "the ligature's own advance");
    }
    ConfExpectAdvance(result, plain, "text.shaping.kerning.after_ligature", "ffiV");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

void TestRegisterTextShapingKerning(void)
{
    TestRegister("text.shaping.kerning.pair", TestShapingKernPair);
    TestRegister("text.shaping.kerning.not_a_pair", TestShapingKernNotAPair);
    TestRegister("text.shaping.kerning.disabled", TestShapingKernDisabled);
    TestRegister("text.shaping.kerning.is_not_substitution", TestShapingKernIsNotSubstitution);
    TestRegister("text.shaping.kerning.measurement", TestShapingKernMeasurement);
    TestRegister("text.shaping.kerning.range_override", TestShapingKernRangeOverride);
    TestRegister("text.shaping.kerning.after_ligature", TestShapingKernAfterLigature);
}
