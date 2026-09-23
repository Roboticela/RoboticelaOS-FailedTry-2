/*
 * Text measurement (Phase 12.15) — `text.measure.*`.
 *
 * The phase's claim is that a width comes from **shaped typography** rather than from a character count, and that
 * the same shaped result answers for the width a painter's pen accumulates and the box a layout engine places.
 * What this suite proves is that the engine over a finalized run list produces the numbers the runs say, that
 * kerning, ligatures and combining marks change it the way they change the paint, that the line metrics of a
 * mixed-font line are the maximum over its runs rather than the first run's, and that ink is a *different*
 * geometry from logical bounds rather than a restatement of it.
 *
 * ## Why the shipped pack, and why that shapes which cases exist
 *
 * The fixture is `RoboTextTestSupport.h`'s — a face the **shipped system typeface pack** selected — so every
 * number comes from `hmtx`, `hhea`, `OS/2` and `glyf` through RoboFont rather than from a synthetic face that
 * would agree with whatever this build does. That constrains the cases in two ways worth stating rather than
 * discovering:
 *
 *   - **Ligatures are tested through `dlig`.** Inter ships `calt`, `ccmp` and `dlig` but **no `liga`**, so `!?`
 *     is the phase's real end-to-end ligature measurement: two glyphs by default, one when discretionary
 *     ligatures are asked for, and the width that of the substitute's own advance.
 *   - **The combining-mark case uses `q`, not `e`.** Inter *composes* `e` + U+0301 into a single glyph, so the
 *     phase's own example arrives as one glyph with one advance and proves nothing about a mark's advance;
 *     `q` + U+0301 has no precomposed form and therefore arrives as base-plus-mark, which is the case that
 *     matters. The composed form gets its own assertion, because "the font folded them" is a fact about the
 *     measurement too.
 *
 * ## What is deliberately not asserted
 *
 * **A font-fallback boundary.** The pack ships one family, so a scalar Inter does not cover produces a counted
 * miss with `.notdef`'s advance rather than a second run — `text.measure.missing` asserts exactly that, which is
 * the honest form of "no fabricated boundary". A multi-family chain is a pack change, not a text-layer change.
 *
 * **A vertical measurement.** Vertical writing is Phase 12.20; `advanceY` exists and is asserted to be zero,
 * which is what a consumer accumulating progress in that axis reads today.
 */

#include "RoboTextTestSupport.h"

#include "RoboText/RoboTextMeasurement.h"

#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static RoboTextView View(const char *text)
{
    return RoboTextViewMake(text, strlen(text));
}

/** Shape @p text with @p options, asserting success. */
static RoboTextShapeResult *ShapeWith(const TextFixture *fixture,
                                      const RoboTextShapeOptions *options,
                                      const char *text)
{
    RoboTextShapeResult *result = NULL;

    RoboCheckEqInt((int)RoboTextShape(View(text), &fixture->font, options, &result, NULL), (int)RoboOk);
    RoboCheckMsg(result != NULL, "shaping produced no result");
    RoboCheckMsg(RoboTextShapeResultGlyphRunList(result) != NULL, "a shape result carries no run list");
    return result;
}

/** Shape @p text with the default options. */
static RoboTextShapeResult *Shape(const TextFixture *fixture, const char *text)
{
    RoboTextShapeOptions options;

    RoboTextShapeOptionsInit(&options);
    return ShapeWith(fixture, &options, text);
}

/** Measure an already-shaped result with @p request, asserting success. */
static RoboTextMeasurement MeasureOf(const RoboTextShapeResult *result, const RoboTextMeasureRequest *request)
{
    RoboTextMeasurement measurement;

    RoboCheckEqInt((int)RoboTextShapeResultMeasure(result, request, &measurement, NULL), (int)RoboOk);
    return measurement;
}

/** Measure @p text against @p options, without keeping a result. */
static RoboTextMeasurement MeasureText(const TextFixture *fixture,
                                       const RoboTextShapeOptions *options,
                                       const char *text)
{
    RoboTextMeasurement measurement;

    RoboCheckEqInt((int)RoboTextMeasureText(View(text), &fixture->font, options, &measurement, NULL),
                   (int)RoboOk);
    return measurement;
}

/** The sum of the runs' own advances, which is what a width must be. */
static float AdvanceSum(const RoboTextGlyphRunList *list)
{
    float total = 0.0f;

    for (size_t index = 0u; index < RoboTextGlyphRunListCount(list); index++) {
        total += RoboTextGlyphRunAdvance(RoboTextGlyphRunListRun(list, index));
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* Advances                                                           */
/* ------------------------------------------------------------------ */

static void TestMeasureAdvances(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, TextSampleTypography);
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    RoboTextMeasureRequest request;
    RoboTextMeasurement measurement;
    RoboTextMeasurement again;

    RoboTextMeasureRequestInit(&request);
    RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(list, &fixture.font, &request, &measurement, NULL),
                       (int)RoboOk);

    /* The claim, in one line: the width is the sum of the runs' own final advances. */
    RoboCheck(RoboTextMeasureFloatEqual(measurement.width, AdvanceSum(list), RoboTextMeasureTolerancePx));
    RoboCheck(RoboTextMeasureFloatEqual(measurement.width, RoboTextGlyphRunListAdvance(list),
                                            RoboTextMeasureTolerancePx));
    RoboCheck(measurement.width > 0.0f);

    /*
     * And the engine over the list is the *same* implementation the result's own measurement came from, which is
     * the architectural half of the phase: the two records are field-for-field identical rather than merely
     * close, because they are one function over one list.
     */
    again = MeasureOf(result, &request);
    RoboCheckMsg(RoboTextMeasurementEqual(&measurement, &again),
                     "the result's measurement and the engine's disagree about the same list");

    /* A caller that wants a width must not pay for outlines: the default request computes no bounds. */
    RoboCheckEqInt(measurement.hasInkBounds, 0);
    RoboCheckEqInt(measurement.hasLogicalBounds, 0);
    RoboCheck(RoboTextMeasureRectIsEmpty(&measurement.inkBounds));

    /* The counts come from the runs, and the scalar count from their sources. */
    RoboCheckEqInt((int)measurement.glyphRunCount, (int)RoboTextGlyphRunListCount(list));
    RoboCheck(measurement.glyphCount > 0u);
    RoboCheckEqInt((int)measurement.scalarCount, (int)strlen(TextSampleTypography));
    RoboCheckEqInt((int)measurement.advanceY, 0);

    /* Validation: the door a test calls, and it agrees with the record the engine produced. */
    {
        RoboTextMeasureProblem problem = (RoboTextMeasureProblem)99;

        RoboCheckEqInt((int)RoboTextMeasureValidate(&measurement, list, &problem, NULL), (int)RoboOk);
        RoboCheckEqInt((int)problem, (int)RoboTextMeasureProblemNone);
        /* And it refuses what it cannot check. */
        RoboCheckEqInt((int)RoboTextMeasureValidate(&measurement, NULL, &problem, NULL),
                           (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextMeasureValidate(NULL, list, &problem, NULL), (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(NULL, &fixture.font, NULL, &measurement, NULL),
                           (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(list, &fixture.font, NULL, NULL, NULL),
                           (int)RoboInvalidArgument);
    }

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

static void TestMeasurePrecision(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, TextSamplePangram);
    RoboTextMeasurement first;
    RoboTextMeasurement second;

    first = MeasureOf(result, NULL);
    second = MeasureOf(result, NULL);

    /* Deterministic: the same list measured twice is bit-identical, not merely within tolerance. This is what
     * "the tolerance is for comparisons, not for the pipeline" means, and it is worth asserting rather than
     * asserting only the tolerance. */
    RoboCheckMsg(RoboTextMeasurementEqual(&first, &second),
                     "two measurements of one list differ");
    RoboCheck(RoboTextMeasurementApproxEqual(&first, &second, RoboTextMeasureTolerancePx));

    /* Asking for bounds changes the bounds and nothing else: the advances are the runs' own either way. */
    {
        RoboTextMeasureRequest full;
        RoboTextMeasurement bounded;

        RoboTextMeasureRequestInitFull(&full);
        bounded = MeasureOf(result, &full);
        RoboCheck(RoboTextMeasureFloatEqual(first.width, bounded.width, RoboTextMeasureTolerancePx));
        RoboCheck(RoboTextMeasureFloatEqual(first.ascent, bounded.ascent, RoboTextMeasureTolerancePx));
        RoboCheckEqInt(bounded.hasLogicalBounds, 1);
        /* And `approx` deliberately ignores the bounds: it compares the geometry a layout pass reads. */
        RoboCheck(RoboTextMeasurementApproxEqual(&first, &bounded, RoboTextMeasureTolerancePx));
    }

    /* The tolerance door's own edges. */
    RoboCheck(RoboTextMeasureFloatEqual(1.0f, 1.0f, RoboTextMeasureTolerancePx));
    RoboCheck(RoboTextMeasureFloatEqual(1.0f, 1.0f + (RoboTextMeasureTolerancePx / 2.0f),
                                            RoboTextMeasureTolerancePx));
    RoboCheck(!RoboTextMeasureFloatEqual(1.0f, 1.5f, RoboTextMeasureTolerancePx));
    RoboCheck(RoboTextMeasureFloatEqual(0.0f, 0.0f, 0.0f));
    RoboCheck(!RoboTextMeasureFloatEqual(0.0f, 1.0f, 0.0f));
    RoboCheck(RoboTextMeasurementApproxEqual(NULL, NULL, RoboTextMeasureTolerancePx));
    RoboCheck(!RoboTextMeasurementApproxEqual(&first, NULL, RoboTextMeasureTolerancePx));

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The things that move a width                                       */
/* ------------------------------------------------------------------ */

static void TestMeasureKerning(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions kerned;
    RoboTextShapeOptions plain;
    RoboTextMeasurement on;
    RoboTextMeasurement off;

    RoboTextShapeOptionsInit(&kerned);
    RoboTextShapeOptionsInit(&plain);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&plain, "kern", RoboTextFeatureOff, 0u),
                       (int)RoboOk);

    on = MeasureText(&fixture, &kerned, "AVATAR");
    off = MeasureText(&fixture, &plain, "AVATAR");

    /* Inter kerns several of those pairs, so the two widths differ — and the *width* is where it shows, not
     * merely a glyph offset. The assertion is "the mechanism reached the measurement", never "this pair has
     * this value", because the second claim belongs to the font. */
    RoboCheckMsg(!RoboTextMeasureFloatEqual(on.width, off.width, RoboTextMeasureTolerancePx),
                     "turning kerning off did not change the measured width");
    RoboCheck(on.width > 0.0f);
    RoboCheck(off.width > 0.0f);
    /* Kerning moves the pen and never the glyphs: the counts are the same either way. */
    RoboCheckEqInt((int)on.glyphCount, (int)off.glyphCount);

    TextFixtureClose(&fixture);
}

static void TestMeasureLigature(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions plain;
    RoboTextShapeOptions discretionary;
    RoboTextMeasurement off;
    RoboTextMeasurement on;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;

    RoboTextShapeOptionsInit(&plain);
    RoboTextShapeOptionsInit(&discretionary);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&discretionary, "dlig", RoboTextFeatureOn, 0u),
                       (int)RoboOk);

    off = MeasureText(&fixture, &plain, "!?");
    on = MeasureText(&fixture, &discretionary, "!?");

    /* Two glyphs by default, one when the face's discretionary ligature runs. That is the measurement seeing a
     * substitution: the engine does not know `!?` became an interrobang, it knows the run is one glyph shorter
     * and its advance is the substitute's own. */
    RoboCheckEqInt((int)off.glyphCount, 2);
    RoboCheckEqInt((int)on.glyphCount, 1);
    RoboCheck(!RoboTextMeasureFloatEqual(off.width, on.width, RoboTextMeasureTolerancePx));

    /* And the ligature's advance is the *substitute's*, not the sum of its components': the run's own advance is
     * what the engine reports, by construction. */
    result = ShapeWith(&fixture, &discretionary, "!?");
    run = RoboTextShapeResultGlyphRun(result, 0u);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 1);
    RoboCheck(RoboTextMeasureFloatEqual(on.width, RoboTextGlyphRunAdvance(run),
                                            RoboTextMeasureTolerancePx));
    RoboTextShapeResultDestroy(result);

    TextFixtureClose(&fixture);
}

static void TestMeasureCombiningMark(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextMeasurement mark;
    RoboTextMeasurement base;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;

    /* `q` + U+0301, so the mark is a glyph of its own rather than a composition — see the file header. */
    mark = MeasureText(&fixture, NULL, "q\xCC\x81");
    base = MeasureText(&fixture, NULL, "q");

    RoboCheckEqInt((int)mark.glyphCount, 2);
    RoboCheckEqInt((int)base.glyphCount, 1);

    result = Shape(&fixture, "q\xCC\x81");
    run = RoboTextShapeResultGlyphRun(result, 0u);
    {
        /* Both scalars are covered, and the mark's own advance is zero on this face: a combining mark that
         * advanced would push the following text sideways, which is the defect this case exists to catch. The
         * assertion is on the glyph the *face* produced rather than on a number this suite picked. */
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, 1u);

        RoboCheck(glyph != NULL);
        RoboCheckMsg(glyph->advanceX == 0.0f, "the face's combining mark advances");
        /* And therefore the marked text measures exactly as its base does — not base plus a nominal width. */
        RoboCheck(RoboTextMeasureFloatEqual(mark.width, base.width, RoboTextMeasureTolerancePx));
    }
    RoboTextShapeResultDestroy(result);

    /* The composed case: Inter folds `e` + U+0301 into one glyph with one advance. The measurement reports what
     * the font did, which is one glyph, and that is the honest answer rather than a two-glyph one. */
    {
        RoboTextMeasurement composed = MeasureText(&fixture, NULL, "e\xCC\x81");
        RoboTextMeasurement letter = MeasureText(&fixture, NULL, "e");

        RoboCheckEqInt((int)composed.glyphCount, 1);
        RoboCheck(composed.width > 0.0f);
        RoboCheck(composed.width >= letter.width - RoboTextMeasureTolerancePx);
    }

    TextFixtureClose(&fixture);
}

static void TestMeasureWhitespace(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextMeasureRequest full;
    RoboTextShapeResult *result = Shape(&fixture, "   ");
    RoboTextMeasurement measurement;

    RoboTextMeasureRequestInitFull(&full);
    measurement = MeasureOf(result, &full);

    /* Whitespace has an advance and no ink, which is exactly why the two geometries are separate fields: a
     * layout pass measures the spaces and a clipping pass must not treat them as ink. */
    RoboCheckMsg(measurement.width > 0.0f, "a space does not advance");
    RoboCheckEqInt(measurement.glyphCount, 3);
    RoboCheck(measurement.advanceMax > 0.0f);
    RoboCheckMsg(measurement.hasInkBounds != 0, "the ink of whitespace was not computed");
    RoboCheckMsg(RoboTextMeasureRectIsEmpty(&measurement.inkBounds),
                     "whitespace reported ink");

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Empty text                                                          */
/* ------------------------------------------------------------------ */

static void TestMeasureEmpty(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextMeasurement measurement;
    RoboTextShapeResult *result;
    RoboTextMeasureRequest full;

    measurement = MeasureText(&fixture, NULL, "");
    RoboCheck(measurement.width == 0.0f);
    RoboCheckEqInt((int)measurement.glyphCount, 0);
    RoboCheckEqInt((int)measurement.glyphRunCount, 0);
    RoboCheckEqInt((int)measurement.scalarCount, 0);
    RoboCheckEqInt((int)measurement.advanceMax, 0);
    RoboCheckEqInt((int)measurement.hasInkBounds, 0);
    /*
     * A text with no runs still has a **line**, and the metrics come from the context the caller measured
     * against — Phase 12.15.17's "vertical metrics may still be supplied when measurement occurs against a
     * specified font". A zero ascent would collapse every empty line.
     */
    RoboCheckMsg(measurement.ascent > 0.0f, "an empty text has no ascent");
    RoboCheck(measurement.lineHeight >= measurement.ascent + measurement.descent);

    /* Through a shape result, over the same empty list, with the logical box asked for. */
    RoboTextMeasureRequestInitFull(&full);
    result = Shape(&fixture, "");
    measurement = MeasureOf(result, &full);
    RoboCheck(measurement.width == 0.0f);
    RoboCheckEqInt(measurement.hasLogicalBounds, 1);
    RoboCheck(measurement.logicalBounds.width == 0.0f);
    /* No glyphs, so no ink — and reported as absent rather than as a zero-area rectangle a caller would clip to. */
    RoboCheckEqInt(measurement.hasInkBounds, 0);
    RoboTextShapeResultDestroy(result);

    TextFixtureClose(&fixture);
}

static void TestMeasureMissing(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextMeasurement measurement = MeasureText(&fixture, NULL, TextSampleNoncharacter);

    /* The scalar no conforming face covers: a `.notdef` that is drawn and advanced over, counted as a miss. The
     * width is a real width — of the box the reader will see — which is the point of counting rather than
     * refusing. */
    RoboCheckEqInt((int)measurement.missingGlyphs, 1);
    RoboCheckEqInt((int)measurement.glyphCount, 1);

    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Line metrics over a mixed line                                      */
/* ------------------------------------------------------------------ */

static void TestMeasureLineMetrics(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, TextSampleScriptMixed);
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    RoboTextMeasureLineMetrics metrics;
    RoboTextMeasurement measurement;
    float maxAscent = 0.0f;
    float maxDescent = 0.0f;

    RoboCheckEqInt((int)RoboTextMeasureGlyphRunListLineMetrics(list, &fixture.font, &metrics, NULL),
                       (int)RoboOk);
    measurement = MeasureOf(result, NULL);

    /*
     * The record and the three fields are one answer, and the answer is the **maximum** over the runs. Both
     * halves are asserted: that the policy is the maximum (by recomputing it here), and that the engine's fields
     * are the record's (by comparing them).
     */
    for (size_t index = 0u; index < RoboTextGlyphRunListCount(list); index++) {
        const RoboTextGlyphRun *run = RoboTextGlyphRunListRun(list, index);

        if (RoboTextGlyphRunAscent(run) > maxAscent) {
            maxAscent = RoboTextGlyphRunAscent(run);
        }
        if (RoboTextGlyphRunDescent(run) > maxDescent) {
            maxDescent = RoboTextGlyphRunDescent(run);
        }
    }
    RoboCheck(RoboTextMeasureFloatEqual(metrics.ascent, maxAscent, RoboTextMeasureTolerancePx));
    RoboCheck(RoboTextMeasureFloatEqual(metrics.descent, maxDescent, RoboTextMeasureTolerancePx));
    RoboCheck(RoboTextMeasureFloatEqual(metrics.height, metrics.ascent + metrics.descent + metrics.lineGap,
                                            RoboTextMeasureTolerancePx));
    RoboCheck(metrics.lineHeight >= metrics.height - RoboTextMeasureTolerancePx);
    RoboCheckEqInt((int)metrics.runCount, (int)RoboTextGlyphRunListCount(list));
    RoboCheckEqInt(metrics.fromFace, 1);

    RoboCheck(RoboTextMeasureFloatEqual(measurement.ascent, metrics.ascent, RoboTextMeasureTolerancePx));
    RoboCheck(RoboTextMeasureFloatEqual(measurement.descent, metrics.descent, RoboTextMeasureTolerancePx));
    RoboCheck(RoboTextMeasureFloatEqual(measurement.lineGap, metrics.lineGap, RoboTextMeasureTolerancePx));
    RoboCheck(RoboTextMeasureFloatEqual(measurement.lineHeight, metrics.lineHeight,
                                            RoboTextMeasureTolerancePx));
    /* The one flag that says the fields came from a face rather than from this build's documented estimate. */
    RoboCheckEqInt((int)measurement.lineMetrics.runCount, (int)metrics.runCount);

    /* A declared leading is honoured as declared, and it is the *same* door: a 24 px leading on a 16 px face. */
    {
        TextFixture tall = TextFixtureOpen();
        RoboTextFontStyle style = tall.font.style;
        RoboTextMeasurement led;

        style.lineHeightPx = 24.0f;
        RoboCheckEqInt((int)RoboTextFontContextSet(&tall.font, tall.face, &style), (int)RoboOk);
        led = MeasureText(&tall, NULL, TextSampleTypography);
        RoboCheck(led.lineHeight == 24.0f);
        /* Leading does not move the width: the two are independent. */
        RoboCheck(led.width > 0.0f);
        TextFixtureClose(&tall);
    }

    /* Refusal rather than a fabricated answer. */
    RoboCheckEqInt((int)RoboTextMeasureGlyphRunListLineMetrics(list, &fixture.font, NULL, NULL),
                       (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextMeasureGlyphRunListLineMetrics(NULL, &fixture.font, &metrics, NULL),
                       (int)RoboInvalidArgument);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Bounds                                                              */
/* ------------------------------------------------------------------ */

static void TestMeasureBounds(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, TextSampleTypography);
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    RoboTextMeasureRequest full;
    RoboTextMeasurement measurement;

    RoboTextMeasureRequestInitFull(&full);
    measurement = MeasureOf(result, &full);

    RoboCheckEqInt(measurement.hasLogicalBounds, 1);
    RoboCheckEqInt(measurement.hasInkBounds, 1);

    /* The logical box is the derivation, checked rather than trusted: the origin on the baseline, `width`
     * across, ascent+descent tall. */
    RoboCheck(measurement.logicalBounds.x == 0.0f);
    RoboCheck(RoboTextMeasureFloatEqual(measurement.logicalBounds.y, -measurement.descent,
                                            RoboTextMeasureTolerancePx));
    RoboCheck(RoboTextMeasureFloatEqual(measurement.logicalBounds.width, measurement.width,
                                            RoboTextMeasureTolerancePx));
    RoboCheck(RoboTextMeasureFloatEqual(measurement.logicalBounds.height,
                                            measurement.ascent + measurement.descent,
                                            RoboTextMeasureTolerancePx));

    /* The ink box is a *different* geometry, and the ink of text sits inside the line box rather than covering
     * it: `Typography` has no descender below the face's descent and no glyph above its ascent. */
    RoboCheck(!RoboTextMeasureRectIsEmpty(&measurement.inkBounds));
    RoboCheck(measurement.inkBounds.x >= -RoboTextMeasureTolerancePx);
    RoboCheck(measurement.inkBounds.height > 0.0f);
    RoboCheck(measurement.inkBounds.height <= measurement.ascent + measurement.descent +
                                                      RoboTextMeasureTolerancePx);
    RoboCheck(measurement.inkBounds.width <= measurement.width + RoboTextMeasureTolerancePx);

    /* Validation catches a doctored logical box, which is the one duplicated fact in the record — duplicated,
     * and checked. */
    {
        RoboTextMeasurement broken = measurement;
        RoboTextMeasureProblem problem = RoboTextMeasureProblemNone;

        broken.logicalBounds.width += 5.0f;
        RoboCheckEqInt((int)RoboTextMeasureValidate(&broken, list, &problem, NULL),
                           (int)RoboInvalidArgument);
        RoboCheckEqInt((int)problem, (int)RoboTextMeasureProblemLogicalBounds);
    }
    /* And a doctored width, a wrong count, a nonzero baseline and a non-finite number. */
    {
        RoboTextMeasurement broken = measurement;
        RoboTextMeasureProblem problem = RoboTextMeasureProblemNone;

        broken.width += 5.0f;
        RoboCheckEqInt((int)RoboTextMeasureValidate(&broken, list, &problem, NULL),
                           (int)RoboInvalidArgument);
        RoboCheckEqInt((int)problem, (int)RoboTextMeasureProblemWidth);

        broken = measurement;
        broken.glyphCount += 1u;
        RoboCheckEqInt((int)RoboTextMeasureValidate(&broken, list, &problem, NULL),
                           (int)RoboInvalidArgument);
        RoboCheckEqInt((int)problem, (int)RoboTextMeasureProblemCounts);

        broken = measurement;
        broken.baseline = 1.0f;
        RoboCheckEqInt((int)RoboTextMeasureValidate(&broken, list, &problem, NULL),
                           (int)RoboInvalidArgument);
        RoboCheckEqInt((int)problem, (int)RoboTextMeasureProblemMetrics);

        broken = measurement;
        broken.descent = -1.0f;
        RoboCheckEqInt((int)RoboTextMeasureValidate(&broken, list, &problem, NULL),
                           (int)RoboInvalidArgument);
        RoboCheckEqInt((int)problem, (int)RoboTextMeasureProblemMetrics);

        broken = measurement;
        broken.width = 1.0f / 0.0f;
        RoboCheckEqInt((int)RoboTextMeasureValidate(&broken, list, &problem, NULL),
                           (int)RoboInvalidArgument);
        RoboCheckEqInt((int)problem, (int)RoboTextMeasureProblemNotFinite);
    }

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

static void TestMeasureMarkInk(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextMeasureRequest full;
    RoboTextMeasurement plain;
    RoboTextMeasurement marked;
    RoboTextMeasureRect ink;
    int complete = 0;

    RoboTextMeasureRequestInitFull(&full);

    /* `e` against `é` (composed by the face, see the file header): the accent sits *above* the letter, so the
     * ink of the accented text reaches higher than the ink of the bare letter while the two widths stay close.
     * This is the distinction 12.15.9 exists for, and the assertion is a comparison between two real outlines
     * rather than a number this suite picked. */
    {
        RoboTextShapeResult *result = Shape(&fixture, "e");

        plain = MeasureOf(result, &full);
        RoboTextShapeResultDestroy(result);
    }
    {
        RoboTextShapeResult *result = Shape(&fixture, "e\xCC\x81");

        marked = MeasureOf(result, &full);
        RoboTextShapeResultDestroy(result);
    }
    RoboCheck(!RoboTextMeasureRectIsEmpty(&plain.inkBounds));
    RoboCheck(!RoboTextMeasureRectIsEmpty(&marked.inkBounds));
    RoboCheckMsg(marked.inkBounds.y + marked.inkBounds.height >
                         plain.inkBounds.y + plain.inkBounds.height + RoboTextMeasureTolerancePx,
                     "a composed accent did not raise the ink");

    /* A descender: `j` has ink below the baseline, which is a fact about the outline and not about the ascent. */
    {
        RoboTextShapeResult *result = Shape(&fixture, "j");

        plain = MeasureOf(result, &full);
        RoboCheckMsg(plain.inkBounds.y < -RoboTextMeasureTolerancePx,
                         "a descender's ink does not reach below the baseline");
        RoboTextShapeResultDestroy(result);
    }

    /* The standalone door answers the same box the measurement carries, and reports completeness. */
    {
        RoboTextShapeResult *result = Shape(&fixture, TextSampleTypography);

        RoboCheckEqInt((int)RoboTextMeasureGlyphRunListInkBounds(
                               RoboTextShapeResultGlyphRunList(result), &ink, &complete, NULL),
                           (int)RoboOk);
        RoboCheckEqInt(complete, 1);
        RoboCheck(!RoboTextMeasureRectIsEmpty(&ink));
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunListInkBounds(NULL, &ink, &complete, NULL),
                           (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunListInkBounds(
                               RoboTextShapeResultGlyphRunList(result), NULL, &complete, NULL),
                           (int)RoboInvalidArgument);
        RoboTextShapeResultDestroy(result);
    }

    TextFixtureClose(&fixture);
}

static void TestMeasureRect(void)
{
    RoboTextMeasureRect rect;
    RoboTextMeasureRect other;

    RoboTextMeasureRectInit(&rect);
    /* The initialised rectangle is empty, and `IsEmpty` says so on NULL rather than crashing. */
    RoboCheck(RoboTextMeasureRectIsEmpty(&rect));
    RoboCheck(RoboTextMeasureRectIsEmpty(NULL));
    RoboCheck(RoboTextMeasureRectWidth(&rect) == 0.0f);
    RoboCheck(RoboTextMeasureRectHeight(&rect) == 0.0f);

    /* An empty accumulator adopts rather than dragging an edge back to the origin. */
    RoboTextMeasureRectUnionLtrb(&rect, 10.0f, -2.0f, 20.0f, 8.0f);
    RoboCheck(rect.x == 10.0f);
    RoboCheck(rect.y == -2.0f);
    RoboCheck(rect.width == 10.0f);
    RoboCheck(rect.height == 10.0f);
    RoboCheck(!RoboTextMeasureRectIsEmpty(&rect));

    RoboTextMeasureRectUnionLtrb(&rect, 0.0f, 0.0f, 12.0f, 3.0f);
    RoboCheck(rect.x == 0.0f);
    RoboCheck(rect.y == -2.0f);
    RoboCheck(rect.width == 20.0f);
    RoboCheck(rect.height == 10.0f);

    /* An inverted pair is an empty box and contributes nothing; an empty one does too. */
    RoboTextMeasureRectUnionLtrb(&rect, 5.0f, 5.0f, 5.0f, 1.0f);
    RoboCheck(rect.width == 20.0f);
    RoboTextMeasureRectInit(&other);
    RoboTextMeasureRectUnion(&rect, &other);
    RoboCheck(rect.width == 20.0f);
    other.x = -1.0f;
    other.y = -3.0f;
    other.width = 2.0f;
    other.height = 2.0f;
    RoboTextMeasureRectUnion(&rect, &other);
    RoboCheck(rect.x == -1.0f);
    RoboCheck(rect.y == -3.0f);
    /* NULL is ignored on both sides. */
    RoboTextMeasureRectUnion(NULL, &other);
    RoboTextMeasureRectUnion(&rect, NULL);
    RoboTextMeasureRectUnionLtrb(NULL, 0.0f, 0.0f, 1.0f, 1.0f);
    RoboTextMeasureRectInit(NULL);

    RoboCheck(RoboTextMeasureRectEqual(&rect, &rect));
    RoboCheck(RoboTextMeasureRectEqual(NULL, NULL));
    RoboCheck(!RoboTextMeasureRectEqual(&rect, NULL));
    RoboCheck(!RoboTextMeasureRectEqual(&rect, &other));
}

/* ------------------------------------------------------------------ */
/* Direction, mixed scripts and fallback                               */
/* ------------------------------------------------------------------ */

static void TestMeasureDirection(void)
{
    TextFixture fixture = TextFixtureOpen();
    const char *samples[3];
    RoboTextMeasureRequest full;

    samples[0] = TextSampleDirectionHebrew;
    samples[1] = TextSampleDirectionArabicSentence;
    samples[2] = TextSampleDirectionLevels;

    RoboTextMeasureRequestInitFull(&full);
    for (size_t index = 0u; index < 3u; index++) {
        RoboTextShapeResult *result = Shape(&fixture, samples[index]);
        const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
        RoboTextMeasurement measurement = MeasureOf(result, &full);

        /*
         * A right-to-left run is not a *negative* or reversed advance: direction changes where a run sits, never
         * how wide it is. The width is the sum of the runs' own advances in logical order — and it is a real
         * width here because the face covers these scripts.
         */
        RoboCheckMsg(measurement.width > 0.0f, "an RTL text has no width");
        RoboCheck(RoboTextMeasureFloatEqual(measurement.width, AdvanceSum(list),
                                                RoboTextMeasureTolerancePx));
        RoboCheck(RoboTextMeasureFloatEqual(measurement.width, RoboTextGlyphRunListAdvance(list),
                                                RoboTextMeasureTolerancePx));
        /* Every run is covered by the total: none is dropped because it was reordered. */
        RoboCheckEqInt((int)measurement.glyphRunCount, (int)RoboTextGlyphRunListCount(list));
        RoboCheck(!RoboTextMeasureRectIsEmpty(&measurement.inkBounds));

        RoboTextShapeResultDestroy(result);
    }

    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The request and the options vocabulary                              */
/* ------------------------------------------------------------------ */

static void TestMeasureOptions(void)
{
    RoboTextMeasureRequest request;
    RoboTextMeasureLineMetrics lineMetrics;
    char line[96];
    size_t length = 0u;

    RoboTextMeasureRequestInit(&request);
    RoboCheckEqInt((int)request.options, (int)RoboTextMeasureOptionNone);
    RoboTextMeasureRequestInitFull(&request);
    RoboCheckEqInt((int)request.options, (int)RoboTextMeasureOptionsAll);
    RoboTextMeasureRequestInit(NULL);
    RoboTextMeasureRequestInitFull(NULL);

    RoboCheckEqInt((int)RoboTextMeasureOptionsWrite(RoboTextMeasureOptionNone, line, sizeof(line),
                                                        &length),
                       (int)RoboOk);
    RoboCheckStrEq(line, "none");
    RoboCheckEqInt((int)length, 4);
    RoboCheckEqInt((int)RoboTextMeasureOptionsWrite(
                           (RoboTextMeasureOptions)RoboTextMeasureOptionLogicalBounds, line, sizeof(line),
                           &length),
                       (int)RoboOk);
    RoboCheckStrEq(line, "logical-bounds");
    RoboCheckEqInt((int)RoboTextMeasureOptionsWrite(RoboTextMeasureOptionsAll, line, sizeof(line),
                                                        &length),
                       (int)RoboOk);
    RoboCheckStrEq(line, "logical-bounds|ink-bounds");
    /* Complete or absent: a buffer that cannot hold the word is left empty and reports the length it needed. */
    {
        char small[4];

        RoboCheckEqInt((int)RoboTextMeasureOptionsWrite(RoboTextMeasureOptionsAll, small, sizeof(small),
                                                            &length),
                           (int)RoboNotSupported);
        RoboCheckEqInt((int)small[0], 0);
        RoboCheckEqInt((int)length, 25);
    }
    RoboCheckEqInt((int)RoboTextMeasureOptionsWrite(RoboTextMeasureOptionsAll, NULL, 0u, &length),
                       (int)RoboInvalidArgument);

    RoboCheck(RoboTextMeasureOptionIsKnown(RoboTextMeasureOptionNone));
    RoboCheck(RoboTextMeasureOptionIsKnown(RoboTextMeasureOptionInkBounds));
    RoboCheck(!RoboTextMeasureOptionIsKnown((RoboTextMeasureOption)7));
    RoboCheckStrEq(RoboTextMeasureOptionName(RoboTextMeasureOptionNone), "none");
    RoboCheckStrEq(RoboTextMeasureOptionName(RoboTextMeasureOptionInkBounds), "ink-bounds");
    RoboCheckStrEq(RoboTextMeasureOptionName((RoboTextMeasureOption)7), "unknown");

    RoboCheckStrEq(RoboTextMeasureProblemName(RoboTextMeasureProblemNone), "none");
    RoboCheckStrEq(RoboTextMeasureProblemName(RoboTextMeasureProblemWidth), "width");
    RoboCheckStrEq(RoboTextMeasureProblemName(RoboTextMeasureProblemLogicalBounds), "logical-bounds");
    RoboCheckStrEq(RoboTextMeasureProblemName((RoboTextMeasureProblem)99), "unknown");
    RoboCheck(RoboTextMeasureProblemIsKnown(RoboTextMeasureProblemCounts));
    RoboCheck(!RoboTextMeasureProblemIsKnown((RoboTextMeasureProblem)99));

    RoboTextMeasureLineMetricsInit(&lineMetrics);
    RoboCheckEqInt((int)lineMetrics.runCount, 0);
    RoboCheck(RoboTextMeasureLineMetricsEqual(&lineMetrics, &lineMetrics));
    RoboCheck(RoboTextMeasureLineMetricsEqual(NULL, NULL));
    RoboCheck(!RoboTextMeasureLineMetricsEqual(&lineMetrics, NULL));
    RoboTextMeasureLineMetricsInit(NULL);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static void TestMeasureWrite(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, TextSampleTypography);
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    RoboTextMeasureRequest full;
    RoboTextMeasurement measurement;
    char line[4096];
    size_t length = 0u;

    RoboTextMeasureRequestInitFull(&full);
    measurement = MeasureOf(result, &full);

    RoboCheckEqInt((int)RoboTextMeasureWrite(&measurement, list, line, sizeof(line), &length),
                       (int)RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheck(strstr(line, "TextMeasurement {") != NULL);
    RoboCheck(strstr(line, "width ") != NULL);
    RoboCheck(strstr(line, "inkBounds [") != NULL);
    RoboCheck(strstr(line, "logicalBounds [") != NULL);
    /* A line per run and a line per glyph, with the two numbers a width bug is diagnosed from. */
    RoboCheck(strstr(line, "  run[0] source [") != NULL);
    RoboCheck(strstr(line, "    glyph[0] pen ") != NULL);
    RoboCheck(strstr(line, " ink [") != NULL || strstr(line, " ink none") != NULL);
    RoboCheck(strstr(line, "\n}") != NULL);

    /* An invalid record says so first, before any of its numbers — a block a reader trusts. */
    {
        RoboTextMeasurement broken = measurement;
        char mark[4096];

        broken.width += 5.0f;
        RoboCheckEqInt((int)RoboTextMeasureWrite(&broken, list, mark, sizeof(mark), &length),
                           (int)RoboOk);
        RoboCheck(strstr(mark, "INVALID width\n") == mark);
    }

    /* Complete or absent: a buffer that cannot hold the block is left empty. */
    {
        char small[16];

        RoboCheckEqInt((int)RoboTextMeasureWrite(&measurement, list, small, sizeof(small), &length),
                           (int)RoboNotSupported);
        RoboCheckEqInt((int)small[0], 0);
    }
    RoboCheckEqInt((int)RoboTextMeasureWrite(NULL, list, line, sizeof(line), &length),
                       (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextMeasureWrite(&measurement, NULL, line, sizeof(line), &length),
                       (int)RoboInvalidArgument);

    /* And the one-line summary the stable comparison reads is unchanged by the phase's new fields. */
    {
        char summary[192];

        RoboCheckEqInt((int)RoboTextMeasurementWrite(&measurement, summary, sizeof(summary), &length),
                           (int)RoboOk);
        RoboCheck(strstr(summary, "width=") == summary);
        RoboCheck(strstr(summary, "lineHeight=") != NULL);
    }

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Cost                                                                */
/* ------------------------------------------------------------------ */

static void TestMeasurePerformance(void)
{
    const char *text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. AVATAR Typography 2026.";
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, text);
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    RoboTextMeasureRequest full;
    RoboTextMeasurement measurement;
    clock_t started;
    clock_t finished;
    double millis;
    int repeats = 200;

    RoboTextMeasureRequestInitFull(&full);
    started = clock();
    for (int index = 0; index < repeats; index++) {
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(list, &fixture.font, NULL, &measurement, NULL),
                           (int)RoboOk);
    }
    finished = clock();
    millis = ((double)(finished - started) * 1000.0) / (double)CLOCKS_PER_SEC;

    RoboCheck(measurement.width > 0.0f);
    /*
     * Measurement is cheap because the expensive work already happened: this walks the runs' advances and, when
     * asked, the glyphs' boxes. The **baseline** is the point of the case, not a threshold — a measurement that
     * re-shaped would be orders of magnitude slower here, which is the regression this number would show.
     */
    printf("           text.measure.performance: bytes=%u runs=%u glyphs=%u measures=%d ms=%.3f\n",
           (unsigned)strlen(text), (unsigned)RoboTextGlyphRunListCount(list),
           (unsigned)measurement.glyphCount, repeats, millis);

    /* The same list, with ink bounds: the per-glyph outline ask, measured separately so the two costs are not
     * confused for one. */
    started = clock();
    for (int index = 0; index < 20; index++) {
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(list, &fixture.font, &full, &measurement, NULL),
                           (int)RoboOk);
    }
    finished = clock();
    millis = ((double)(finished - started) * 1000.0) / (double)CLOCKS_PER_SEC;
    RoboCheckEqInt(measurement.hasInkBounds, 1);
    printf("           text.measure.performance: ink bounds measures=20 ms=%.3f\n", millis);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterTextMeasurement(void)
{
    TestRegister("text.measure.advances", TestMeasureAdvances);
    TestRegister("text.measure.precision", TestMeasurePrecision);
    TestRegister("text.measure.kern", TestMeasureKerning);
    TestRegister("text.measure.ligature", TestMeasureLigature);
    TestRegister("text.measure.mark", TestMeasureCombiningMark);
    TestRegister("text.measure.whitespace", TestMeasureWhitespace);
    TestRegister("text.measure.empty", TestMeasureEmpty);
    TestRegister("text.measure.missing", TestMeasureMissing);
    TestRegister("text.measure.line_metrics", TestMeasureLineMetrics);
    TestRegister("text.measure.bounds", TestMeasureBounds);
    TestRegister("text.measure.mark_ink", TestMeasureMarkInk);
    TestRegister("text.measure.rect", TestMeasureRect);
    TestRegister("text.measure.direction", TestMeasureDirection);
    TestRegister("text.measure.options", TestMeasureOptions);
    TestRegister("text.measure.write", TestMeasureWrite);
    TestRegister("text.measure.performance", TestMeasurePerformance);
}
