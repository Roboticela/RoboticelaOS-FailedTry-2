/*
 * RoboText's conformance, vocabulary and font context (Phase 12.4) — `text.conformance.*`,
 * `text.vocabulary.*` and `text.font.*`.
 *
 * The conformance file is where the phase's **honesty** is tested rather than its arithmetic, and that is
 * a deliberate split: the shape and glyph suites prove the numbers are right, and this one proves the
 * build is not claiming anything it does not do. `RoboTextGetCapabilities` is read field by field, with
 * the model flags asserted present and every algorithm flag asserted **absent**; a later goal that
 * implements GSUB flips one flag here in the same change, and this suite is the thing that notices when
 * somebody flips one without implementing it.
 *
 * The font-context cases are the other half: the one scaling door, the metrics record, and the three ways
 * a face can have no advance — none of which may be reported as zero.
 */

#include "RoboTextTestSupport.h"

/* ------------------------------------------------------------------ */
/* Version and capabilities                                            */
/* ------------------------------------------------------------------ */

static void TestConformanceVersion(void)
{
    int32_t major = -1;
    int32_t minor = -1;
    int32_t patch = -1;

    RoboCheckStrEq(RoboTextVersionString(), "1.7.0");
    RoboCheckEqInt((int)RoboTextGetVersion(&major, &minor, &patch), (int)RoboOk);
    RoboCheckEqInt((int)major, RoboTextVersionMajor);
    RoboCheckEqInt((int)minor, RoboTextVersionMinor);
    RoboCheckEqInt((int)patch, RoboTextVersionPatch);
    RoboCheckEqInt((int)RoboTextGetVersion(NULL, &minor, &patch), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGetVersion(&major, NULL, &patch), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGetVersion(&major, &minor, NULL), (int)RoboInvalidArgument);
    /* The version string and the components are the same fact. */
    RoboCheckEqInt((int)major, 1);
    /* 12.5 added the Unicode/font integration, 12.6 the script-extension facts and the contextual script
     * resolver, 12.7 the direction stage and 12.8 the bidirectional stage — none of which changed the run
     * or glyph record *shapes*, which is exactly the condition the version header names for a minor move
     * rather than a major one. 12.8 gave the run and glyph records a level *field*, which is why the
     * record layouts moved without the API's shape changing. */
    /* 12.10 added the substitution engine and 12.12 the ligature layer, both additively: the glyph and
     * shape records grew a ligature field and kept their shape, which is the condition the version header
     * names for a minor move rather than a major one. 12.11 added the positioning engine and 12.13 the
     * kerning layer, the second of which gave `RoboTextGlyph` the font's own part of its two vectors — a field
     * beside the totals rather than a reshaped record, so the minor moves once more and the major stays. */
    RoboCheckEqInt((int)minor, 7);
    RoboCheckEqInt((int)patch, 0);
}

static void TestConformanceCapabilities(void)
{
    RoboTextCapabilities capabilities;

    RoboTextCapabilitiesInit(&capabilities);
    /* A zeroed record claims nothing, which is what an uninitialised one should say. */
    RoboCheckEqInt(capabilities.buffer, 0);
    RoboCheckEqInt(capabilities.substitution, 0);
    RoboCheckEqInt(capabilities.ligaturePolicy, 0);
    RoboCheckEqInt(capabilities.positioning, 0);
    RoboCheckEqInt(capabilities.kerning, 0);
    RoboCheckEqInt(capabilities.kerningPolicy, 0);
    RoboCheckEqInt(capabilities.kerningReport, 0);
    RoboTextCapabilitiesInit(NULL); /* Safe. */
    RoboCheckEqInt((int)RoboTextGetCapabilities(NULL), (int)RoboInvalidArgument);

    RoboCheckEqInt((int)RoboTextGetCapabilities(&capabilities), (int)RoboOk);

    /* The model: everything 12.4 establishes. Each of these is exercised by a suite above. */
    RoboCheckEqInt(capabilities.buffer, 1);
    RoboCheckEqInt(capabilities.indexing, 1);
    RoboCheckEqInt(capabilities.ranges, 1);
    RoboCheckEqInt(capabilities.fontContext, 1);
    RoboCheckEqInt(capabilities.scriptRuns, 1);
    RoboCheckEqInt(capabilities.directionRuns, 1);
    RoboCheckEqInt(capabilities.textRuns, 1);
    RoboCheckEqInt(capabilities.glyphs, 1);
    RoboCheckEqInt(capabilities.glyphRuns, 1);
    RoboCheckEqInt(capabilities.sourceMapping, 1);
    RoboCheckEqInt(capabilities.measurement, 1);
    RoboCheckEqInt(capabilities.shapeResult, 1);

    /*
     * The Unicode/font integration (12.5). Each of these is exercised by a suite below, and each is
     * reported here so that a reader of the capability record learns what the build does with Unicode and
     * fonts rather than only what it does with runs.
     */
    RoboCheckEqInt(capabilities.unicodeIntegration, 1);
    RoboCheckEqInt(capabilities.codePoints, 1);
    RoboCheckEqInt(capabilities.combiningMarks, 1);
    RoboCheckEqInt(capabilities.scriptResolver, 1);
    RoboCheckEqInt(capabilities.fontCoverage, 1);
    RoboCheckEqInt(capabilities.fontSelection, 1);
    RoboCheckEqInt(capabilities.coverageCache, 1);
    RoboCheckEqInt(capabilities.analysisRuns, 1);
    RoboCheckEqInt(capabilities.shapeRequest, 1);

    /*
     * The script stage (12.6): `Script_Extensions` read as its own property, the two-sided neutrality rule,
     * and the run dump. Each is exercised by a case in `text.script.*` below.
     */
    RoboCheckEqInt(capabilities.scriptExtensions, 1);
    RoboCheckEqInt(capabilities.contextualScriptResolution, 1);
    RoboCheckEqInt(capabilities.scriptRunDiagnostics, 1);

    /*
     * The direction stage (12.7): the paragraph direction (P2/P3), the per-span context, the class
     * projection that keeps `EN` apart from `ES`, and the run dump. Each is exercised by a case in
     * `text.direction.*`. These stay **1** after 12.8 and mean what they meant: the classification,
     * whose `RoboTextDirectionUnknown` is still a real state on the direction runs.
     */
    RoboCheckEqInt(capabilities.paragraphDirection, 1);
    RoboCheckEqInt(capabilities.directionContext, 1);
    RoboCheckEqInt(capabilities.directionClasses, 1);
    RoboCheckEqInt(capabilities.directionRunDiagnostics, 1);

    /*
     * The bidi stage (12.8): the flag 12.7 declared absent, and its six companions. Each is exercised by a
     * case in `text.bidi.*` — the paragraph base and its four sources, the levels and X9's removals, the
     * level runs and L2's visual order, the isolates, L4's mirroring, and the paragraph dump — so a `1`
     * here is a claim the suite can contradict rather than a sentence in a comment.
     */
    RoboCheckEqInt(capabilities.bidiAlgorithm, 1);
    RoboCheckEqInt(capabilities.bidiParagraphs, 1);
    RoboCheckEqInt(capabilities.bidiLevels, 1);
    RoboCheckEqInt(capabilities.bidiVisualOrder, 1);
    RoboCheckEqInt(capabilities.bidiIsolates, 1);
    RoboCheckEqInt(capabilities.bidiMirroring, 1);
    RoboCheckEqInt(capabilities.bidiDiagnostics, 1);

    /*
     * The generator is no longer the placeholder: 12.10 put a substitution pass between the cmap and the
     * glyph run, and the flag answers 0 from then on. A caller that read it as 1 in 12.9 learns from this
     * field, and only from this field, that the claim changed — which is why it is still here.
     */
    RoboCheckEqInt(capabilities.glyphGenerationIsPlaceholder, 0);

    /*
     * The two algorithms this build *does* have beyond the model, each named with the suite that exercises
     * it: substitution (12.10, every `text.gsub.*` case) and ligatures with the policy and provenance that
     * go with it (12.12, `text.ligature.*`). `bidiAlgorithm` joined them in 12.8.
     */
    RoboCheckEqInt(capabilities.substitution, 1);
    RoboCheckEqInt(capabilities.ligatures, 1);
    RoboCheckEqInt(capabilities.ligaturePolicy, 1);
    RoboCheckEqInt(capabilities.ligatureProvenance, 1);

    /*
     * Positioning (12.11) and kerning (12.13), the two that left the absent list. `positioning` is a claim
     * `text.gpos.*` contradicts if it is wrong — the pair formats, the value records and the shipped pack's
     * kerning reaching the pen — and `kerning`/`kerningPolicy`/`kerningReport` are 12.13's three: the feature
     * as a policy, the resolution of which source answers, and the per-glyph record of what the font moved.
     * They are separate flags because they are separate claims: a build could position and not read `kern`,
     * or read `kern` and not report it.
     */
    RoboCheckEqInt(capabilities.positioning, 1);
    RoboCheckEqInt(capabilities.kerning, 1);
    RoboCheckEqInt(capabilities.kerningPolicy, 1);
    RoboCheckEqInt(capabilities.kerningReport, 1);

    /*
     * And every algorithm this phase deliberately does **not** implement, each one a row in the phase plan
     * rather than an oversight: complex scripts 12.19, vertical text 12.20, and variable-axis application
     * unowned until RoboFont interpolates outlines. A `1` here without the implementation would be the exact
     * failure the phase's boundary section forbids. `bidiAlgorithm` left this list in 12.8,
     * `substitution`/`ligatures` in 12.10 and 12.12, and `positioning`/`kerning` in 12.11 and 12.13 — each
     * is asserted present above.
     */
    RoboCheckEqInt(capabilities.complexScripts, 0);
    RoboCheckEqInt(capabilities.verticalText, 0);
    RoboCheckEqInt(capabilities.variableAxisApplication, 0);

    /* The generation mode vocabulary agrees with the flag: one mode is known and it is the placeholder. */
    RoboCheck(RoboTextGlyphGenerationModeIsKnown(RoboTextGlyphGenerationCmapPerScalar));
    RoboCheck(RoboTextGlyphGenerationModeIsKnown(RoboTextGlyphGenerationNone));
    RoboCheck(!RoboTextGlyphGenerationModeIsKnown((RoboTextGlyphGenerationMode)7));
    RoboCheckStrEq(RoboTextGlyphGenerationModeName(RoboTextGlyphGenerationCmapPerScalar),
                   "cmap-per-scalar");
    RoboCheckStrEq(RoboTextGlyphGenerationModeName(RoboTextGlyphGenerationNone), "none");
    RoboCheckStrEq(RoboTextGlyphGenerationModeName((RoboTextGlyphGenerationMode)7), "none");
}

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestVocabulary(void)
{
    /* Direction: three values, distinct names, and only one of them needs reordering. */
    RoboCheckEqInt((int)RoboTextDirectionCount, 3);
    RoboCheck(RoboTextDirectionIsKnown(RoboTextDirectionUnknown));
    RoboCheck(RoboTextDirectionIsKnown(RoboTextDirectionLtr));
    RoboCheck(RoboTextDirectionIsKnown(RoboTextDirectionRtl));
    RoboCheck(!RoboTextDirectionIsKnown((RoboTextDirection)3));
    RoboCheckStrEq(RoboTextDirectionName(RoboTextDirectionLtr), "ltr");
    RoboCheckStrEq(RoboTextDirectionName(RoboTextDirectionRtl), "rtl");
    RoboCheckStrEq(RoboTextDirectionName(RoboTextDirectionUnknown), "unknown");
    RoboCheckStrEq(RoboTextDirectionName((RoboTextDirection)9), "unknown");
    RoboCheck(RoboTextDirectionIsRightToLeft(RoboTextDirectionRtl));
    RoboCheck(!RoboTextDirectionIsRightToLeft(RoboTextDirectionLtr));
    /* `Unknown` is not LTR: the whole point is that a neutral-only text has no resolved direction. */
    RoboCheck(!RoboTextDirectionIsRightToLeft(RoboTextDirectionUnknown));

    /* Optical size: five classes, in the order the typography scale uses them. */
    RoboCheckEqInt((int)RoboTextOpticalSizeCount, 5);
    for (int index = 0; index < (int)RoboTextOpticalSizeCount; index++) {
        RoboCheck(RoboTextOpticalSizeIsKnown((RoboTextOpticalSize)index));
        RoboCheck(RoboTextOpticalSizeName((RoboTextOpticalSize)index) != NULL);
        RoboCheck(strcmp(RoboTextOpticalSizeName((RoboTextOpticalSize)index), "unknown") != 0);
    }
    RoboCheckEqInt((int)RoboTextOpticalSmall, 0);
    RoboCheckEqInt((int)RoboTextOpticalBody, 1);
    RoboCheckEqInt((int)RoboTextOpticalUI, 2);
    RoboCheckEqInt((int)RoboTextOpticalDisplay, 3);
    RoboCheckEqInt((int)RoboTextOpticalLargeDisplay, 4);
    RoboCheckStrEq(RoboTextOpticalSizeName(RoboTextOpticalLargeDisplay), "large-display");
    RoboCheck(!RoboTextOpticalSizeIsKnown((RoboTextOpticalSize)5));
    RoboCheck(!RoboTextOpticalSizeIsKnown((RoboTextOpticalSize)-1));
    RoboCheckStrEq(RoboTextOpticalSizeName((RoboTextOpticalSize)99), "unknown");

    /* Scripts come from RoboUnicode, so all this layer asserts is that the two names it re-exports are
     * the neutral ones and that a real script's short name resolves. */
    RoboCheck(RoboTextScriptIsNeutral(RoboTextScriptCommon));
    RoboCheck(RoboTextScriptIsNeutral(RoboTextScriptInherited));
    RoboCheck(!RoboTextScriptIsNeutral(RoboUScriptLatin));
    RoboCheck(!RoboTextScriptIsNeutral(RoboUScriptHebrew));
    RoboCheckStrEq(RoboTextScriptName(RoboUScriptLatin), "Latn");
    RoboCheckStrEq(RoboTextScriptName(RoboUScriptHebrew), "Hebr");
    RoboCheck(RoboTextScriptName((RoboTextScript)0xFFFF) != NULL); /* Never NULL, whatever it is given. */
}

/* ------------------------------------------------------------------ */
/* The font style record and the context                               */
/* ------------------------------------------------------------------ */

static void TestFontStyleRecord(void)
{
    RoboTextFontStyle style;
    RoboTextFontStyle other;
    char line[128];
    size_t needed = 0u;

    RoboTextFontStyleInit(&style);
    /* The CSS initial values, spelled once: 16px, weight 400, upright, normal stretch, no tracking. */
    RoboCheck(style.sizePx == RoboTextInitialFontSizePx);
    RoboCheckEqInt((int)style.weight, 400);
    RoboCheckEqInt((int)style.style, (int)RoboFontStyleNormal);
    RoboCheckEqInt((int)style.stretch, (int)RoboFontStretchNormal);
    RoboCheck(style.lineHeightPx == RoboTextLineHeightFromFontMetrics);
    RoboCheck(style.letterSpacingPx == 0.0f);
    RoboCheckEqInt((int)style.opticalSize, (int)RoboTextOpticalBody);
    RoboCheck(RoboTextFontStyleIsValid(&style));
    RoboTextFontStyleInit(NULL); /* Safe. */

    other = style;
    RoboCheck(RoboTextFontStyleEqual(&style, &other));
    other.sizePx = 17.0f;
    RoboCheck(!RoboTextFontStyleEqual(&style, &other));
    RoboCheck(RoboTextFontStyleEqual(NULL, NULL));
    RoboCheck(!RoboTextFontStyleEqual(&style, NULL));

    /* The validation refuses exactly the settings this build cannot draw, and nothing else. */
    other = style;
    other.sizePx = 0.0f;
    RoboCheck(!RoboTextFontStyleIsValid(&other));
    other.sizePx = -1.0f;
    RoboCheck(!RoboTextFontStyleIsValid(&other));
    other.sizePx = 16.0f;
    other.weight = (FontWeight)0u;
    RoboCheck(!RoboTextFontStyleIsValid(&other));
    other.weight = (FontWeight)1000u;
    RoboCheck(!RoboTextFontStyleIsValid(&other));
    other.weight = (FontWeight)400u;
    other.lineHeightPx = -1.0f;
    RoboCheck(!RoboTextFontStyleIsValid(&other));
    other.lineHeightPx = 0.0f;
    RoboCheck(RoboTextFontStyleIsValid(&other));
    other.opticalSize = (RoboTextOpticalSize)9;
    RoboCheck(!RoboTextFontStyleIsValid(&other));
    RoboCheck(!RoboTextFontStyleIsValid(NULL));

    /* The dump is complete or absent, never truncated. */
    RoboCheckEqInt((int)RoboTextFontStyleWrite(&style, line, sizeof(line), &needed), (int)RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)needed);
    RoboCheck(strstr(line, "size=16.0") == line);
    RoboCheck(strstr(line, "weight=400") != NULL);
    RoboCheck(strstr(line, "style=normal") != NULL);
    {
        char small[6];

        RoboCheckEqInt((int)RoboTextFontStyleWrite(&style, small, sizeof(small), &needed),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)small[0], 0);
    }
    RoboCheckEqInt((int)RoboTextFontStyleWrite(NULL, line, sizeof(line), &needed),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontStyleWrite(&style, line, 0u, &needed), (int)RoboInvalidArgument);
}

static void TestFontContext(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontContext context;
    RoboTextFontStyle style;

    RoboTextFontContextInit(&context);
    RoboCheck(RoboTextFontContextFace(&context) == NULL);
    RoboCheck(!RoboTextFontContextIsUsable(&context)); /* No face: not usable, and not an error. */
    RoboCheck(RoboTextFontContextIsUsable(&fixture.font));
    RoboCheck(RoboTextFontContextFace(&fixture.font) == fixture.face);

    /* A context owns nothing and copies cleanly: the two compare equal field for field. */
    context = fixture.font;
    RoboCheck(RoboTextFontContextEqual(&context, &fixture.font));
    RoboCheckEqInt(RoboTextFontContextCompare(&context, &fixture.font), 0);
    RoboCheck(RoboTextFontContextEqual(NULL, NULL));
    RoboCheck(!RoboTextFontContextEqual(&context, NULL));
    RoboCheck(RoboTextFontContextCompare(NULL, &context) < 0);
    RoboCheck(RoboTextFontContextCompare(&context, NULL) > 0);

    /* A different setting is a different context, and the order is total rather than partial. */
    style = RoboTextFontContextStyle(&fixture.font);
    style.sizePx += 1.0f;
    RoboCheckEqInt((int)RoboTextFontContextSet(&context, fixture.face, &style), (int)RoboOk);
    RoboCheck(!RoboTextFontContextEqual(&context, &fixture.font));
    RoboCheck(RoboTextFontContextCompare(&context, &fixture.font) > 0);
    RoboCheck(RoboTextFontContextCompare(&fixture.font, &context) < 0);

    /* And an invalid setting is refused *before* the face is stored, so a context is never left holding a
     * face it cannot draw. */
    style.sizePx = 0.0f;
    RoboCheckEqInt((int)RoboTextFontContextSet(&context, fixture.face, &style), (int)RoboInvalidArgument);
    RoboCheck(RoboTextFontContextFace(&context) == fixture.face); /* Unchanged, not cleared. */
    RoboCheckEqInt((int)RoboTextFontContextSet(NULL, fixture.face, &style), (int)RoboInvalidArgument);

    /* A default style comes back for an absent context rather than a crash. */
    {
        RoboTextFontStyle initial = RoboTextFontContextStyle(NULL);

        RoboCheck(initial.sizePx == RoboTextInitialFontSizePx);
        RoboCheckEqInt((int)initial.weight, 400);
    }
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The one scaling door, and the metrics                               */
/* ------------------------------------------------------------------ */

static void TestFontScalingAndMetrics(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontMetrics metrics;
    float pixels = 0.0f;

    RoboCheckEqInt((int)RoboTextFontContextMetrics(&fixture.font, &metrics, NULL), (int)RoboOk);
    /* The shipped face declares its own head/hhea, so this is a measurement rather than the estimate — and
     * the estimate is only used for a face that has nothing, which a test asserts below. */
    RoboCheckEqInt(metrics.fromFace, 1);
    RoboCheck(metrics.unitsPerEm > 0u);
    RoboCheck(metrics.ascentPx > 0.0f);
    RoboCheck(metrics.descentPx >= 0.0f);
    RoboCheck(metrics.lineGapPx >= 0.0f);
    /* The record's own invariant: the three parts are the line extent. */
    RoboCheck(metrics.lineHeightPx == metrics.ascentPx + metrics.descentPx + metrics.lineGapPx);

    /*
     * The one scaling door: `upem` font units is exactly one em, which is exactly the context's size. Any
     * other arithmetic here would make a glyph's advance in this layer differ from the same glyph's advance
     * one layer down, which is the disagreement the phase exists to remove.
     */
    RoboCheckEqInt(
        (int)RoboTextFontUnitsToDevice(&fixture.font, (double)metrics.unitsPerEm, &pixels, NULL),
        (int)RoboOk);
    RoboCheck(pixels - fixture.font.style.sizePx < 0.001f);
    RoboCheck(fixture.font.style.sizePx - pixels < 0.001f);

    /* Half an em, and a negative value: the door is arithmetic rather than a lookup, and a negative
     * offset is a thing positioning will need (12.11). */
    RoboCheckEqInt(
        (int)RoboTextFontUnitsToDevice(&fixture.font, (double)metrics.unitsPerEm / 2.0, &pixels, NULL),
        (int)RoboOk);
    RoboCheck(pixels - fixture.font.style.sizePx / 2.0f < 0.001f);
    RoboCheckEqInt((int)RoboTextFontUnitsToDevice(&fixture.font, -100.0, &pixels, NULL), (int)RoboOk);
    RoboCheck(pixels < 0.0f);

    RoboCheckEqInt((int)RoboTextFontUnitsToDevice(NULL, 1.0, &pixels, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontUnitsToDevice(&fixture.font, 1.0, NULL, NULL),
                   (int)RoboInvalidArgument);
    {
        TextFixture none = TextFixtureNoFace();
        RoboTextFontMetrics absent;

        /* No face is a refusal rather than a division by a zero `upem`. The out record is a throwaway: a
         * refusing call clears the record it was handed, so reusing `metrics` here would erase the
         * measurement the cases below compare against (which is exactly what happened once). */
        RoboCheckEqInt((int)RoboTextFontUnitsToDevice(&none.font, 100.0, &pixels, NULL),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)RoboTextFontContextMetrics(&none.font, &absent, NULL),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)absent.fromFace, 0);
        RoboCheckEqInt((int)absent.unitsPerEm, 0);
        TextFixtureClose(&none);
    }
    RoboCheckEqInt((int)RoboTextFontContextMetrics(NULL, &metrics, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontContextMetrics(&fixture.font, NULL, NULL), (int)RoboInvalidArgument);

    /* A declared leading overrides the face's line extent but leaves the three magnitudes alone, so the
     * record stays internally consistent while reporting the leading the caller asked for. */
    {
        RoboTextFontStyle style = RoboTextFontContextStyle(&fixture.font);
        RoboTextFontMetrics tall;

        style.lineHeightPx = 40.0f;
        RoboCheckEqInt((int)RoboTextFontContextSet(&fixture.font, fixture.face, &style), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextFontContextMetrics(&fixture.font, &tall, NULL), (int)RoboOk);
        RoboCheck(tall.lineHeightPx == 40.0f);
        RoboCheck(tall.ascentPx == metrics.ascentPx);
        RoboCheck(tall.descentPx == metrics.descentPx);
        RoboCheck(tall.unitsPerEm == metrics.unitsPerEm);
    }

    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The glyph doors, including the three ways there is no advance       */
/* ------------------------------------------------------------------ */

static void TestFontGlyphDoors(void)
{
    TextFixture fixture = TextFixtureOpen();
    GlyphId glyph = RoboGlyphIdNone;
    int mapped = -1;
    FontGlyphMetrics metrics;
    FontGlyphMetricsStatus status = RoboFontGlyphMetricsUnavailable;
    float advance = 0.0f;

    /* A scalar the face has. */
    RoboCheckEqInt((int)RoboTextFontLookupGlyph(&fixture.font, 'A', &glyph, &mapped, NULL), (int)RoboOk);
    RoboCheckEqInt(mapped, 1);
    RoboCheck(glyph != RoboGlyphIdNone);
    { /* The face's own advance for `A`, which the double-lookup check below compares against. */
        float letterAdvance = 0.0f;

        RoboCheckEqInt((int)RoboTextFontGlyphAdvance(&fixture.font, glyph, &letterAdvance, NULL, NULL),
                       (int)RoboOk);
        RoboCheck(letterAdvance > 0.0f);
        RoboCheckEqInt((int)RoboTextFontGlyphMetrics(&fixture.font, glyph, &metrics, &advance, NULL),
                       (int)RoboOk);
        RoboCheck(advance == letterAdvance);
        RoboCheckEqInt((int)metrics.status, (int)RoboFontGlyphMetricsOk);
        RoboCheckEqInt((int)metrics.glyphId, (int)glyph);
    }
    /* The same lookup twice is the same answer: the cmap parse is lazy and idempotent, not rebuilt. */
    {
        GlyphId again = RoboGlyphIdNone;
        int mappedAgain = 0;

        RoboCheckEqInt((int)RoboTextFontLookupGlyph(&fixture.font, 'A', &again, &mappedAgain, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)again, (int)glyph);
        RoboCheckEqInt(mappedAgain, 1);
    }

    /* A noncharacter is unmapped and is *not* an error: the answer is `mapped == 0`, and the caller counts
     * it. */
    RoboCheckEqInt((int)RoboTextFontLookupGlyph(&fixture.font, 0xFDD0u, &glyph, &mapped, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(mapped, 0);
    RoboCheckEqInt((int)glyph, (int)RoboGlyphIdNone);
    /* A lone surrogate is refused at the door rather than reaching the cmap and coming back as a font
     * problem. */
    RoboCheckEqInt((int)RoboTextFontLookupGlyph(&fixture.font, 0xD800u, &glyph, &mapped, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontLookupGlyph(&fixture.font, 'A', NULL, &mapped, NULL),
                   (int)RoboInvalidArgument);

    /* The metrics door reports the status beside the number, which is the property that keeps a missing
     * table from reading as a zero advance. `.notdef` is a glyph like any other. */
    RoboCheckEqInt((int)RoboTextFontGlyphMetrics(&fixture.font, RoboGlyphIdNone, &metrics, &advance,
                                                 NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)metrics.status, (int)RoboFontGlyphMetricsOk);
    RoboCheck(advance > 0.0f);
    {
        GlyphId a = RoboGlyphIdNone;
        float aAdvance = 0.0f;
        FontGlyphMetricsStatus aStatus = RoboFontGlyphMetricsUnavailable;
        FontGlyphMetrics aMetrics;

        RoboCheckEqInt((int)RoboTextFontLookupGlyph(&fixture.font, 'A', &a, NULL, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextFontGlyphAdvance(&fixture.font, a, &aAdvance, &aStatus, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)aStatus, (int)RoboFontGlyphMetricsOk);
        /* The thin door and the full one report the same advance for the same glyph: two doors, one
         * answer, which is what stops a caller picking the one that happens to say zero. */
        RoboCheckEqInt((int)RoboTextFontGlyphMetrics(&fixture.font, a, &aMetrics, NULL, NULL), (int)RoboOk);
        {
            float full = 0.0f;

            RoboCheckEqInt((int)RoboTextFontGlyphMetrics(&fixture.font, a, &aMetrics, &full, NULL),
                           (int)RoboOk);
            RoboCheck(full == aAdvance);
        }
        /* And `.notdef` is a different glyph from `A`, so the two advances above were not the same read
         * twice. */
        RoboCheck(a != RoboGlyphIdNone);
    }

    /* No face: every glyph door refuses rather than answering with nothing. */
    {
        TextFixture none = TextFixtureNoFace();

        RoboCheckEqInt((int)RoboTextFontLookupGlyph(&none.font, 'A', &glyph, &mapped, NULL),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)RoboTextFontGlyphMetrics(&none.font, 1u, &metrics, &advance, NULL),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)RoboTextFontGlyphAdvance(&none.font, 1u, &advance, &status, NULL),
                       (int)RoboNotSupported);
        TextFixtureClose(&none);
    }
    RoboCheckEqInt((int)RoboTextFontGlyphAdvance(&fixture.font, 1u, &advance, &status, NULL), (int)RoboOk);
    RoboCheckEqInt((int)status, (int)RoboFontGlyphMetricsOk);
    RoboCheckEqInt((int)RoboTextFontGlyphAdvance(NULL, 1u, &advance, &status, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontGlyphAdvance(&fixture.font, 1u, NULL, &status, NULL),
                   (int)RoboInvalidArgument);

    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Determinism under repetition                                        */
/* ------------------------------------------------------------------ */

static void TestConformanceRepetition(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextMeasurement first;
    RoboTextMeasurement again;

    /*
     * Repeated measurement of one text at one setting is bit-for-bit stable — the property the phase's
     * screenshot tests depend on, and the one a cache with stale state would break. Nothing here caches
     * anything, and after this phase the *only* thing that may is the font registry below it.
     */
    RoboCheckEqInt((int)RoboTextMeasureText(RoboTextViewMake(TextSampleHello, 18u), &fixture.font, NULL,
                                            &first, NULL),
                   (int)RoboOk);
    for (int round = 0; round < 8; round++) {
        RoboCheckEqInt((int)RoboTextMeasureText(RoboTextViewMake(TextSampleHello, 18u), &fixture.font,
                                                NULL, &again, NULL),
                       (int)RoboOk);
        RoboCheck(RoboTextMeasurementEqual(&first, &again));
        RoboCheck(first.width == again.width);
    }

    /* And two independent contexts over the same face and setting measure identically: the result depends
     * on the text and the setting, not on which object asked. */
    {
        RoboTextFontContext other;

        RoboTextFontContextInit(&other);
        RoboCheckEqInt((int)RoboTextFontContextSet(&other, fixture.face, &fixture.font.style), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextMeasureText(RoboTextViewMake(TextSampleHello, 18u), &other, NULL,
                                                &again, NULL),
                       (int)RoboOk);
        RoboCheck(RoboTextMeasurementEqual(&first, &again));
    }

    TextFixtureClose(&fixture);
}

void TestRegisterTextConformance(void)
{
    TestRegister("text.conformance.version", TestConformanceVersion);
    TestRegister("text.conformance.capabilities", TestConformanceCapabilities);
    TestRegister("text.vocabulary.enums", TestVocabulary);
    TestRegister("text.font.style_record", TestFontStyleRecord);
    TestRegister("text.font.context", TestFontContext);
    TestRegister("text.font.scaling_metrics", TestFontScalingAndMetrics);
    TestRegister("text.font.glyph_doors", TestFontGlyphDoors);
    TestRegister("text.conformance.repetition", TestConformanceRepetition);
}
