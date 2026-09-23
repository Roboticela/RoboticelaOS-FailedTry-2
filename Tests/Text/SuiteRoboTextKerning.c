/*
 * RoboText's kerning layer (Phase 12.13) — `text.kern.*`.
 *
 * ## The claim this suite has to make, and the face it makes it with
 *
 * Kerning in this module is **GPOS pair adjustment** and nothing else: no per-glyph table, no `AV` special
 * case, no sum of character widths. So the evidence is of two kinds, and the second kind is the one that
 * matters:
 *
 *   **the policy**   one feature state and one source choice, converted into a feature set. That half is
 *                    data, and it is checked as data: the default, the caller's word outranking it, and the
 *                    legacy `kern` table's rule of *fallback and never addition*.
 *
 *   **the face's answer**   the shipped pack is Inter, which carries `kern` under `latn` **and** under
 *                    `DFLT`, with the real pairs in a class matrix behind an extension lookup (lookup 1,
 *                    `PairPos` format 2, coverage past 64 KiB). That last detail is why this suite exists in
 *                    the shape it does: a positioning engine that reads a resolved coverage offset as 16 bits,
 *                    or that stops after a subtable whose coverage misses, produces *no* kerning on this face
 *                    while passing every synthetic test. The assertions here are therefore on the difference
 *                    between the same text with `kern` on and off, never on a particular pair's value — the
 *                    font is the only authority for what a pair is, and this suite's job is to see the
 *                    mechanism reach the pen.
 *
 * `AVATAR`, `To`, `Yo` and `WA` are the classic pairs and Inter kerns several of them; `text.kern.pairs`
 * asserts that *some* of the eight it tries pull in, and that every one of them is accounted for in the
 * report. Nothing here knows which pair Inter kerns, which is exactly the point.
 *
 * ## Measurement is the reason the phase exists
 *
 * `RoboTextKerningReport::advance` and `text.kern.measurement` line the three numbers the phase insists
 * agree: the sum of the glyphs' final advances, the run's advance, and the measurement the run set reports —
 * and beside them the per-glyph identity `advanceX == units→device(advanceUnits) + advanceAdjustX`, which is
 * what makes "the font's part" a fact rather than an inference.
 */

#include "RoboTextTestSupport.h"

#include "RoboText/RoboTextKerning.h"
#include "RoboText/RoboTextRuns.h"

#include <string.h>

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
    return result;
}

/** The single glyph run of a text this suite knows is one run. */
static const RoboTextGlyphRun *OnlyRun(const RoboTextShapeResult *result)
{
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(result), 1);
    return RoboTextShapeResultGlyphRun(result, 0u);
}

/** Shape @p text twice — once with the default policy, once with `kern` off — and answer the two runs. */
static void ShapeKernedAndPlain(const TextFixture *fixture,
                                const char *text,
                                RoboTextShapeResult **outKerned,
                                RoboTextShapeResult **outPlain)
{
    RoboTextShapeOptions options;

    *outKerned = ShapeWith(fixture, NULL, text);
    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "kern", RoboTextFeatureOff, 0u),
                   (int)RoboOk);
    *outPlain = ShapeWith(fixture, &options, text);
}

/* ------------------------------------------------------------------ */
/* The policy                                                          */
/* ------------------------------------------------------------------ */

static void TestKernPolicy(void)
{
    RoboTextKerningPolicy policy;
    RoboTextKerningPolicy other;
    RoboTextFeatureSet set;
    char line[128];
    size_t length = 0u;

    RoboCheckEqInt((int)RoboTextKerningLegacyCount, 2);

    /* Nothing stated is a state of its own, and it is not the default. */
    RoboTextKerningPolicyInit(&policy);
    RoboCheckEqInt((int)policy.kern, (int)RoboTextFeatureDefault);
    RoboCheckEqInt((int)policy.legacy, (int)RoboTextKerningLegacyNever);
    RoboCheck(RoboTextKerningPolicyIsValid(&policy));
    RoboCheckEqInt((int)RoboTextKerningPolicyState(NULL), (int)RoboTextFeatureDefault);
    RoboCheckEqInt((int)RoboTextKerningPolicyLegacy(NULL), (int)RoboTextKerningLegacyNever);

    /* The module's default: kerning on, no legacy fallback. */
    RoboTextKerningPolicyDefault(&policy);
    RoboCheckEqInt((int)policy.kern, (int)RoboTextFeatureOn);

    /* Setting, reading back, and the refusal cases. */
    RoboCheckEqInt((int)RoboTextKerningPolicySet(&policy, RoboTextFeatureOff), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextKerningPolicyState(&policy), (int)RoboTextFeatureOff);
    RoboCheckEqInt((int)RoboTextKerningPolicySet(&policy, (RoboTextFeatureState)7), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextKerningPolicySet(NULL, RoboTextFeatureOn), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextKerningPolicySetLegacy(&policy, RoboTextKerningLegacyIfNoGposKern),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextKerningPolicyLegacy(&policy), (int)RoboTextKerningLegacyIfNoGposKern);
    RoboCheckEqInt((int)RoboTextKerningPolicySetLegacy(&policy, (RoboTextKerningLegacy)9),
                   (int)RoboInvalidArgument);

    /* Equality is field-for-field, and a policy that is not one of ours is not valid. */
    other = policy;
    RoboCheck(RoboTextKerningPolicyEqual(&policy, &other));
    other.kern = RoboTextFeatureOn;
    RoboCheck(!RoboTextKerningPolicyEqual(&policy, &other));
    RoboCheck(RoboTextKerningPolicyEqual(NULL, NULL));
    RoboTextKerningPolicyInit(&other);
    RoboCheck(RoboTextKerningPolicyEqual(&other, &other));
    other.kern = (RoboTextFeatureState)42;
    RoboCheck(!RoboTextKerningPolicyIsValid(&other));

    /* The dump names both fields. */
    RoboTextKerningPolicyDefault(&policy);
    RoboCheckEqInt((int)RoboTextKerningPolicyWrite(&policy, line, sizeof(line), &length), (int)RoboOk);
    RoboCheck(length != 0u);
    RoboCheck(strstr(line, "kern=on") != NULL);
    RoboCheck(strstr(line, "legacy=never") != NULL);
    /*
     * Complete-or-absent: a buffer that cannot hold the whole line is refused, and the caller's bytes are left
     * **untouched** rather than holding a prefix of the answer. Seeded, so "unchanged" is a fact a check can
     * see — an assertion that the buffer is empty would pass for a writer that cleared it. 
     */
    memcpy(line, "sentinel", 9u);
    RoboCheckEqInt((int)RoboTextKerningPolicyWrite(&policy, line, 4u, &length), (int)RoboNotSupported);
    RoboCheckStrEq(line, "sentinel");

    /* Applying: over a set, and under a caller's explicit word. */
    RoboTextFeatureSetInit(&set);
    RoboCheckEqInt((int)RoboTextKerningPolicyApply(&policy, &set), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFeatureSetCount(&set), 1);
    {
        const RoboTextFeatureSetting *setting = RoboTextFeatureSetFind(&set, "kern");

        RoboCheck(setting != NULL);
        RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOn);
    }
    /* The caller's `kern off` survives a later policy pass, which is the whole point of the "if unset" door. */
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&set, "kern", RoboTextFeatureOff, 0u), (int)RoboOk);
    RoboTextKerningPolicyApplyIfUnset(&policy, &set);
    RoboCheckEqInt((int)RoboTextFeatureSetFind(&set, "kern")->state, (int)RoboTextFeatureOff);
    RoboCheckEqInt((int)RoboTextFeatureSetCount(&set), 1);

    /* A policy that states nothing writes nothing. */
    RoboTextKerningPolicyInit(&policy);
    RoboTextFeatureSetInit(&set);
    RoboTextKerningPolicyApplyIfUnset(&policy, &set);
    RoboCheckEqInt((int)RoboTextFeatureSetCount(&set), 0);
    RoboCheckEqInt((int)RoboTextKerningPolicyApply(&policy, &set), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFeatureSetCount(&set), 0);

    /* And the run set installs the default policy's `kern`, so a caller that never mentions kerning gets it. */
    {
        TextFixture fixture = TextFixtureOpen();
        RoboTextShapeOptions options;
        RoboTextRunSet *runs = NULL;

        RoboTextShapeOptionsInit(&options);
        RoboCheckEqInt((int)RoboTextRunSetCreate(View("AV"), &fixture.font, &options, &runs, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextRunSetTextRunCount(runs), 1);
        {
            const RoboTextFeatureSetting *setting =
                RoboTextFeatureSetFind(&RoboTextRunSetTextRun(runs, 0u)->features, "kern");

            RoboCheck(setting != NULL);
            RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOn);
        }
        RoboTextRunSetDestroy(runs);
        TextFixtureClose(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* The resolution                                                      */
/* ------------------------------------------------------------------ */

static void TestKernResolution(void)
{
    TextFixture fixture = TextFixtureOpenRole(RoboFontSystemRoleUIText, RoboFontWeightNormal, 16.0f);
    RoboTextKerningRequest request;
    RoboTextKerningPolicy policy;
    RoboTextKerningResolution resolution;
    RoboTextFeatureSet features;
    RoboTextFeatureSet off;
    char line[192];
    size_t length = 0u;

    memset(&request, 0, sizeof(request));
    request.font = &fixture.font;
    request.script = RoboUScriptLatin;
    request.direction = RoboTextDirectionLtr;
    RoboTextKerningPolicyDefault(&policy);

    /* The shipped face carries `kern` for Latin, and its lookups are what the plan will run. */
    RoboCheckEqInt((int)RoboTextKerningResolve(&request, &policy, &resolution, NULL), (int)RoboOk);
    RoboCheck(resolution.enabled);
    RoboCheckEqInt(resolution.fontHasKernFeature, 1);
    RoboCheck(resolution.lookupCount > 0u);
    RoboCheck(resolution.applies);
    RoboCheckEqInt((int)resolution.source, (int)RoboTextKerningSourceGpos);

    /* Inter carries no legacy `kern` table: the fallback has nothing to fall back to, and says so. */
    RoboCheckEqInt(resolution.legacyTableAvailable, 0);

    /* The dump reads as the facts above, and is complete-or-absent. */
    RoboCheckEqInt((int)RoboTextKerningResolutionWrite(&resolution, line, sizeof(line), &length), (int)RoboOk);
    RoboCheck(strstr(line, "kerning=on") != NULL);
    RoboCheck(strstr(line, "source=gpos") != NULL);
    RoboCheck(strstr(line, "face-kern=1") != NULL);
    memcpy(line, "sentinel", 9u);
    RoboCheckEqInt((int)RoboTextKerningResolutionWrite(&resolution, line, 4u, &length),
                   (int)RoboNotSupported);
    RoboCheckStrEq(line, "sentinel");

    /*
     * A caller that turns kerning off has not changed the face. That is the reason the resolution asks two
     * questions, and this is where the difference shows: `enabled` and `applies` answer one, `face-kern`
     * the other.
     */
    RoboTextFeatureSetInit(&features);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "kern", RoboTextFeatureOff, 0u), (int)RoboOk);
    request.features = &features;
    RoboCheckEqInt((int)RoboTextKerningResolve(&request, &policy, &resolution, NULL), (int)RoboOk);
    RoboCheckEqInt(resolution.enabled, 0);
    RoboCheckEqInt(resolution.applies, 0);
    RoboCheckEqInt(resolution.lookupCount, 0);
    RoboCheckEqInt((int)resolution.source, (int)RoboTextKerningSourceNone);
    RoboCheckEqInt(resolution.fontHasKernFeature, 1);

    /* The same through the policy, which is the door a stylesheet's `font-kerning: none` uses. */
    RoboTextFeatureSetInit(&off);
    RoboTextKerningPolicyInit(&policy);
    RoboCheckEqInt((int)RoboTextKerningPolicySet(&policy, RoboTextFeatureOff), (int)RoboOk);
    request.features = &off;
    RoboCheckEqInt((int)RoboTextKerningResolve(&request, &policy, &resolution, NULL), (int)RoboOk);
    RoboCheckEqInt(resolution.enabled, 0);
    RoboCheckEqInt(resolution.fontHasKernFeature, 1);

    /* And a policy that asks for the legacy fallback is answered honestly: it is not there. */
    RoboTextKerningPolicyDefault(&policy);
    RoboCheckEqInt((int)RoboTextKerningPolicySetLegacy(&policy, RoboTextKerningLegacyIfNoGposKern),
                   (int)RoboOk);
    request.features = NULL;
    RoboCheckEqInt((int)RoboTextKerningResolve(&request, &policy, &resolution, NULL), (int)RoboOk);
    RoboCheckEqInt((int)resolution.source, (int)RoboTextKerningSourceGpos);
    RoboCheckEqInt(resolution.legacyTableAvailable, 0);

    /* A request with no face is refused rather than answered as "no kerning". */
    memset(&request, 0, sizeof(request));
    RoboCheckEqInt((int)RoboTextKerningResolve(&request, &policy, &resolution, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextKerningResolve(NULL, &policy, &resolution, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextKerningResolve(&request, NULL, NULL, NULL), (int)RoboInvalidArgument);

    /* The names are the vocabulary a dump and a caller share. */
    RoboCheckStrEq(RoboTextKerningSourceName(RoboTextKerningSourceNone), "none");
    RoboCheckStrEq(RoboTextKerningSourceName(RoboTextKerningSourceGpos), "gpos");
    RoboCheckStrEq(RoboTextKerningSourceName(RoboTextKerningSourceLegacyKern), "kern-table");
    RoboCheck(RoboTextKerningSourceIsKnown(RoboTextKerningSourceGpos));
    RoboCheck(!RoboTextKerningSourceIsKnown((RoboTextKerningSource)9));

    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The face's answer, end to end                                       */
/* ------------------------------------------------------------------ */

static void TestKernPairs(void)
{
    TextFixture fixture = TextFixtureOpenRole(RoboFontSystemRoleUIText, RoboFontWeightNormal, 16.0f);
    static const char *const kPairs[] = {"AV", "VA", "To", "Yo", "WA", "Ta", "Ty", "LT"};
    int kerned = 0;

    for (size_t index = 0u; index < sizeof(kPairs) / sizeof(kPairs[0]); index++) {
        RoboTextShapeResult *on = NULL;
        RoboTextShapeResult *off = NULL;
        const RoboTextGlyphRun *onRun;
        const RoboTextGlyphRun *offRun;
        RoboTextKerningReport report;
        float onWidth;
        float offWidth;

        ShapeKernedAndPlain(&fixture, kPairs[index], &on, &off);
        onRun = OnlyRun(on);
        offRun = OnlyRun(off);

        /* Same glyphs, same ids: kerning moves the pen and nothing else. */
        RoboCheckEqInt((int)RoboTextGlyphRunCount(onRun), (int)RoboTextGlyphRunCount(offRun));
        RoboCheckEqInt((int)RoboTextGlyphRunPositioned(offRun), 0);

        onWidth = RoboTextGlyphRunAdvance(onRun);
        offWidth = RoboTextGlyphRunAdvance(offRun);
        RoboCheck(offWidth >= onWidth);

        /* The report is the difference, stated in the same units: what the font moved, summed. */
        RoboCheckEqInt((int)RoboTextKerningReportForRun(onRun, &report), (int)RoboOk);
        RoboCheckEqInt((int)report.glyphs, (int)RoboTextGlyphRunCount(onRun));
        if (offWidth > onWidth) {
            kerned++;
            RoboCheckEqInt((int)report.adjustedAdvances > 0, 1);
            RoboCheck(report.advanceSum < 0.0f);
            RoboCheck(report.advanceMin <= report.advanceSum);
            RoboCheck(report.advanceSum == onWidth - offWidth);
            RoboCheckEqInt((int)report.positionedGlyphs, (int)RoboTextGlyphRunPositioned(onRun));
        } else {
            /* The font said nothing about this pair: no advance adjustment, and the report agrees. */
            RoboCheckEqInt((int)report.adjustedAdvances, 0);
        }

        /* Determinism: the same text positions identically, kerning included. */
        {
            RoboTextShapeResult *again = ShapeWith(&fixture, NULL, kPairs[index]);
            const RoboTextGlyphRun *second = OnlyRun(again);

            RoboCheckEqInt((int)RoboTextGlyphRunCount(second), (int)RoboTextGlyphRunCount(onRun));
            for (size_t glyph = 0u; glyph < RoboTextGlyphRunCount(onRun); glyph++) {
                RoboCheck(RoboTextGlyphEqual(RoboTextGlyphRunGlyph(onRun, (RoboTextGlyphIndex)glyph),
                                             RoboTextGlyphRunGlyph(second, (RoboTextGlyphIndex)glyph)));
            }
            RoboTextShapeResultDestroy(again);
        }

        /* The adjustments are readable one at a time, in run order, and the count is the positioned count. */
        if (RoboTextGlyphRunPositioned(onRun) != 0u) {
            RoboTextKerningAdjustment first;
            RoboTextKerningAdjustment last;

            RoboCheckEqInt((int)RoboTextKerningAdjustmentAt(onRun, 0u, &first, NULL), (int)RoboOk);
            RoboCheck(first.glyphId != 0xFFFFu);
            RoboCheckEqInt((int)first.pairIndex, (int)(first.glyphIndex + 1u));
            RoboCheckEqInt((int)RoboTextKerningAdjustmentAt(onRun, report.positionedGlyphs - 1u, &last,
                                                            NULL),
                           (int)RoboOk);
            RoboCheckEqInt((int)RoboTextKerningAdjustmentAt(onRun, report.positionedGlyphs, &last, NULL),
                           (int)RoboNotFound);
        } else {
            RoboTextKerningAdjustment none;

            RoboCheckEqInt((int)RoboTextKerningAdjustmentAt(onRun, 0u, &none, NULL), (int)RoboNotFound);
        }

        RoboTextShapeResultDestroy(on);
        RoboTextShapeResultDestroy(off);
    }

    /*
     * At least one of the eight is kerned by any real UI face. Inter kerns most of them; the assertion says
     * "the mechanism reached the pen" rather than "this face kerns this pair", because the second claim
     * belongs to the font and would make the suite a copy of Inter's table.
     */
    RoboCheck(kerned > 0);

    TextFixtureClose(&fixture);
}

static void TestKernDisabled(void)
{
    TextFixture fixture = TextFixtureOpenRole(RoboFontSystemRoleUIText, RoboFontWeightNormal, 16.0f);
    RoboTextShapeOptions options;
    RoboTextShapeResult *off = NULL;
    const RoboTextGlyphRun *run;
    float unit = 0.0f;

    RoboCheckEqInt((int)RoboTextFontUnitsToDevice(&fixture.font, 1.0, &unit, NULL), (int)RoboOk);
    RoboCheck(unit > 0.0f);

    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "kern", RoboTextFeatureOff, 0u),
                   (int)RoboOk);
    off = ShapeWith(&fixture, &options, "AVATAR Typography");
    run = OnlyRun(off);

    /* With `kern` off, every glyph's advance is exactly its own `hmtx` advance: the pen is the face's. */
    for (size_t index = 0u; index < RoboTextGlyphRunCount(run); index++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)index);

        RoboCheckEqInt((int)glyph->advanceAdjustX, 0);
        RoboCheckEqInt((int)glyph->advanceAdjustY, 0);
        RoboCheckEqInt((int)glyph->offsetAdjustX, 0);
        RoboCheck(glyph->advanceX == (float)glyph->advanceUnits * unit);
    }
    {
        RoboTextKerningReport report;

        RoboCheckEqInt((int)RoboTextKerningReportForRun(run, &report), (int)RoboOk);
        RoboCheckEqInt((int)report.adjustedAdvances, 0);
        RoboCheckEqInt((int)report.advanceSum, 0);
    }

    /* And the same text with the default policy carries the font's kerning somewhere in it. */
    {
        RoboTextShapeResult *on = ShapeWith(&fixture, NULL, "AVATAR Typography");
        const RoboTextGlyphRun *onRun = OnlyRun(on);
        RoboTextKerningReport report;

        RoboCheckEqInt((int)RoboTextGlyphRunCount(onRun), (int)RoboTextGlyphRunCount(run));
        RoboCheckEqInt((int)RoboTextKerningReportForRun(onRun, &report), (int)RoboOk);
        RoboCheck(report.adjustedAdvances > 0u);
        RoboCheck(RoboTextGlyphRunAdvance(onRun) < RoboTextGlyphRunAdvance(run));
        RoboTextShapeResultDestroy(on);
    }

    RoboTextShapeResultDestroy(off);
    TextFixtureClose(&fixture);
}

static void TestKernMeasurement(void)
{
    TextFixture fixture = TextFixtureOpenRole(RoboFontSystemRoleUIText, RoboFontWeightNormal, 18.0f);
    RoboTextShapeResult *result = ShapeWith(&fixture, NULL, "AVATAR Typography");
    const RoboTextGlyphRun *run = OnlyRun(result);
    const RoboTextMeasurement *measurement = RoboTextShapeResultMeasurement(result);
    RoboTextKerningReport report;
    float unit = 0.0f;
    float pen = 0.0f;

    RoboCheckEqInt((int)RoboTextFontUnitsToDevice(&fixture.font, 1.0, &unit, NULL), (int)RoboOk);
    RoboCheck(RoboTextGlyphRunPositioned(run) > 0u);

    /*
     * The per-glyph identity the phase is really about: the advance a glyph carries is the face's own advance
     * plus the font's adjustment, and nothing else ever touches it. A kerning bug that scaled a value record
     * twice, or dropped one, breaks this line for some glyph in the run.
     */
    for (size_t index = 0u; index < RoboTextGlyphRunCount(run); index++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)index);
        float own = (float)glyph->advanceUnits * unit;

        RoboCheck(glyph->advanceX == own + glyph->advanceAdjustX);
    }

    /* The pen, the run and the measurer are one number: the same sum, three ways. */
    RoboCheckEqInt((int)RoboTextGlyphRunAdvanceUpTo(run, RoboTextGlyphRunCount(run), &pen, NULL), (int)RoboOk);
    RoboCheck(pen == RoboTextGlyphRunAdvance(run));
    RoboCheck(measurement->width == RoboTextGlyphRunAdvance(run));
    RoboCheck(measurement->glyphCount == RoboTextGlyphRunCount(run));

    RoboCheckEqInt((int)RoboTextKerningReportForRun(run, &report), (int)RoboOk);
    RoboCheck(report.advance == (double)RoboTextGlyphRunAdvance(run));
    RoboCheckEqInt((int)report.glyphs, (int)RoboTextGlyphRunCount(run));

    /* The report's dump reads as its numbers, and refuses a buffer that cannot hold it. */
    {
        char line[192];
        size_t length = 0u;

        RoboCheckEqInt((int)RoboTextKerningReportWrite(&report, line, sizeof(line), &length), (int)RoboOk);
        RoboCheck(length != 0u);
        RoboCheck(strstr(line, "positioned=") != NULL);
        RoboCheck(strstr(line, "sum=") != NULL);
        memcpy(line, "sentinel", 9u);
        RoboCheckEqInt((int)RoboTextKerningReportWrite(&report, line, 4u, &length),
                       (int)RoboNotSupported);
        RoboCheckStrEq(line, "sentinel");
        RoboCheckEqInt((int)RoboTextKerningReportWrite(NULL, line, sizeof(line), &length),
                       (int)RoboInvalidArgument);
    }

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/**
 * Kerning happens **after** substitution, on the shipped pack as well as on the fixture.
 *
 * Inter's `dlig` turns `!?` into one glyph, and the interesting part is not that it ligates — 12.12's suite
 * owns that — but that the run the ligature produced is then positioned by the same stage, and that the
 * accounting still holds for a glyph whose source range covers two scalars: its advance is the *ligature's*
 * own, its adjustment is separate, and `kern` off changes the second without touching the first.
 */
static void TestKernAfterLigature(void)
{
    TextFixture fixture = TextFixtureOpenRole(RoboFontSystemRoleUIText, RoboFontWeightNormal, 16.0f);
    RoboTextShapeOptions options;
    RoboTextShapeResult *on = NULL;
    RoboTextShapeResult *off = NULL;
    const RoboTextGlyphRun *onRun;
    const RoboTextGlyphRun *offRun;
    float unit = 0.0f;

    RoboCheckEqInt((int)RoboTextFontUnitsToDevice(&fixture.font, 1.0, &unit, NULL), (int)RoboOk);

    RoboTextShapeOptionsInit(&options);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantDiscretionary, &options.ligatures);
    on = ShapeWith(&fixture, &options, "!?");
    offRun = NULL;
    RoboTextShapeOptionsInit(&options);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantDiscretionary, &options.ligatures);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "kern", RoboTextFeatureOff, 0u),
                   (int)RoboOk);
    off = ShapeWith(&fixture, &options, "!?");

    onRun = OnlyRun(on);
    offRun = OnlyRun(off);

    /* One glyph for two scalars, in both: `kern` off is not `dlig` off. */
    RoboCheckEqInt((int)RoboTextGlyphRunCount(onRun), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(offRun), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunScalarCount(onRun), 2);
    RoboCheckEqInt((int)RoboTextGlyphRunSubstituted(onRun), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunSubstituted(offRun), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(onRun, 0u)->glyphId,
                   (int)RoboTextGlyphRunGlyph(offRun, 0u)->glyphId);

    /* The advance identity holds for the ligature too, with and without kerning. */
    {
        const RoboTextGlyph *kerned = RoboTextGlyphRunGlyph(onRun, 0u);
        const RoboTextGlyph *plain = RoboTextGlyphRunGlyph(offRun, 0u);

        RoboCheck(plain->advanceX == (float)plain->advanceUnits * unit + plain->advanceAdjustX);
        RoboCheck(kerned->advanceX == (float)kerned->advanceUnits * unit + kerned->advanceAdjustX);
        RoboCheckEqInt((int)RoboTextGlyphRunPositioned(offRun), 0);
    }

    /* And a report over each run agrees with what the run says. */
    {
        RoboTextKerningReport onReport;
        RoboTextKerningReport offReport;

        RoboCheckEqInt((int)RoboTextKerningReportForRun(onRun, &onReport), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextKerningReportForRun(offRun, &offReport), (int)RoboOk);
        RoboCheckEqInt((int)onReport.glyphs, 1);
        RoboCheckEqInt((int)offReport.adjustedAdvances, 0);
        RoboCheck(offReport.advance == (double)RoboTextGlyphRunAdvance(offRun));
    }

    RoboTextShapeResultDestroy(on);
    RoboTextShapeResultDestroy(off);
    TextFixtureClose(&fixture);
}

/**
 * The phase's own example, end to end: `AVATAR` with `kern` **off** for scalars 0–2 and on for the rest.
 *
 * The mechanism is the run set's, not the engine's: a run carries one feature set, so the text must be cut
 * where the effective settings change — before any plan is resolved — and each run then gets its own plan.
 * The assertions are on the *split* (`AV` a run of its own) and on the two runs' advances, which is the only
 * evidence that separates "the range reached the plan" from "the range was stored and ignored".
 */
static void TestKernRangeOverride(void)
{
    TextFixture fixture = TextFixtureOpenRole(RoboFontSystemRoleUIText, RoboFontWeightNormal, 16.0f);
    RoboTextShapeOptions options;
    RoboTextRunSet *runs = NULL;
    RoboTextShapeResult *whole = NULL;
    RoboTextShapeResult *partial = NULL;
    RoboTextShapeResult *plain = NULL;
    RoboTextRange firstTwo = RoboTextRangeMake(0u, 2u);

    /* The door refuses what cannot be a range, and stores what can. */
    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextShapeOptionsFeatureRangeCount(&options), 0);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeatureRange(&options, RoboTextRangeMake(2u, 2u), "kern",
                                                            RoboTextFeatureOff, 0u),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeatureRange(&options, firstTwo, "kern",
                                                            RoboTextFeatureOff, 0u),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextShapeOptionsFeatureRangeCount(&options), 1);
    RoboCheck(RoboTextShapeOptionsIsValid(&options));
    {
        const RoboTextFeatureRange *range = RoboTextShapeOptionsFeatureRange(&options, 0u);

        RoboCheck(range != NULL);
        RoboCheckEqInt((int)range->range.start, 0);
        RoboCheckEqInt((int)range->range.end, 2);
        RoboCheckEqInt((int)RoboTextFeatureSetFind(&range->features, "kern")->state, (int)RoboTextFeatureOff);
    }
    /* A second setting for the same scalars joins the same entry rather than making a second one. */
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeatureRange(&options, firstTwo, "liga", RoboTextFeatureOn,
                                                            0u),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextShapeOptionsFeatureRangeCount(&options), 1);
    RoboCheck(RoboTextShapeOptionsFeatureRange(&options, 1u) == NULL);
    /* And the cap is a refusal, not a silent drop. */
    for (uint32_t index = 0u; index < RoboTextMaxFeatureRanges; index++) {
        (void)RoboTextShapeOptionsSetFeatureRange(&options, RoboTextRangeMake(100u + index, 101u + index),
                                                  "kern", RoboTextFeatureOn, 0u);
    }
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeatureRange(&options, RoboTextRangeMake(200u, 201u), "kern",
                                                            RoboTextFeatureOn, 0u),
                   (int)RoboNotSupported);

    /* The request that matters: `AV` unkerned, `ATAR` kerned, all in one text. */
    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeatureRange(&options, firstTwo, "kern", RoboTextFeatureOff,
                                                            0u),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextRunSetCreate(View("AVATAR"), &fixture.font, &options, &runs, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextRunSetTextRunCount(runs), 2);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(runs, 0u)->range.end, 2);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(runs, 1u)->range.start, 2);
    RoboCheckEqInt((int)RoboTextFeatureSetFind(&RoboTextRunSetTextRun(runs, 0u)->features, "kern")->state,
                   (int)RoboTextFeatureOff);
    RoboCheckEqInt((int)RoboTextFeatureSetFind(&RoboTextRunSetTextRun(runs, 1u)->features, "kern")->state,
                   (int)RoboTextFeatureOn);
    RoboTextRunSetDestroy(runs);

    whole = ShapeWith(&fixture, NULL, "AVATAR");
    partial = ShapeWith(&fixture, &options, "AVATAR");
    {
        RoboTextShapeOptions off;

        RoboTextShapeOptionsInit(&off);
        RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&off, "kern", RoboTextFeatureOff, 0u), (int)RoboOk);
        plain = ShapeWith(&fixture, &off, "AVATAR");
    }

    /* Two runs, four glyphs: the split reaches the result and no glyph is lost to it. */
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(partial), 2);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(RoboTextShapeResultGlyphRun(partial, 0u)), 2);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(RoboTextShapeResultGlyphRun(partial, 1u)), 4);

    /* The first run is exactly the all-off text's, and the second carries the font's kerning. */
    {
        float partialWidth = RoboTextShapeResultMeasurement(partial)->width;
        float wholeWidth = RoboTextShapeResultMeasurement(whole)->width;
        float plainWidth = RoboTextShapeResultMeasurement(plain)->width;

        /*
         * Between the two extremes, in the direction the settings say: the range kept the font's kerning out of
         * `AV` and left `ATAR` alone, so the request is wider than the all-on text and narrower than the all-off
         * one. A reader who saw `partial == whole` or `partial == plain` would know the range reached the data
         * and not the shaping.
         */
        RoboCheck(wholeWidth < partialWidth);
        RoboCheck(partialWidth < plainWidth);

        /*
         * And the split is a **shaping** boundary, which has a consequence worth stating rather than hiding:
         * the pair that straddles it (`V` of the first run against `A` of the second) is not a pair at all, so
         * the seam loses whatever adjustment the font had for it. That is why the two runs' advances are checked
         * against the same text shaped alone, instead of against the whole text's own glyphs: run 0's advance
         * must be exactly `AV` with `kern` off, and run 1's exactly `ATAR` with it on.
         */
        {
            RoboTextShapeOptions off;
            RoboTextShapeResult *avOff;
            RoboTextShapeResult *atarOn;

            RoboTextShapeOptionsInit(&off);
            RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&off, "kern", RoboTextFeatureOff, 0u),
                           (int)RoboOk);
            avOff = ShapeWith(&fixture, &off, "AV");
            atarOn = ShapeWith(&fixture, NULL, "ATAR");
            RoboCheck(RoboTextGlyphRunAdvance(RoboTextShapeResultGlyphRun(partial, 0u)) ==
                      RoboTextShapeResultMeasurement(avOff)->width);
            RoboCheck(RoboTextGlyphRunAdvance(RoboTextShapeResultGlyphRun(partial, 1u)) ==
                      RoboTextShapeResultMeasurement(atarOn)->width);
            RoboTextShapeResultDestroy(avOff);
            RoboTextShapeResultDestroy(atarOn);
        }
    }
    /* And the unkerned run's glyphs carry no adjustment at all, while the second run's do. */
    {
        RoboTextKerningReport first;
        RoboTextKerningReport second;

        RoboCheckEqInt((int)RoboTextKerningReportForRun(RoboTextShapeResultGlyphRun(partial, 0u), &first),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextKerningReportForRun(RoboTextShapeResultGlyphRun(partial, 1u), &second),
                       (int)RoboOk);
        RoboCheckEqInt((int)first.adjustedAdvances, 0);
        RoboCheck(second.adjustedAdvances > 0u);
    }

    RoboTextShapeResultDestroy(whole);
    RoboTextShapeResultDestroy(partial);
    RoboTextShapeResultDestroy(plain);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterTextKerning(void)
{
    TestRegister("text.kern.policy", TestKernPolicy);
    TestRegister("text.kern.resolution", TestKernResolution);
    TestRegister("text.kern.pairs", TestKernPairs);
    TestRegister("text.kern.disabled", TestKernDisabled);
    TestRegister("text.kern.measurement", TestKernMeasurement);
    TestRegister("text.kern.after_ligature", TestKernAfterLigature);
    TestRegister("text.kern.range_override", TestKernRangeOverride);
}
