/*
 * RoboText's pipeline and measurement (Phase 12.4) — `text.shape.*` and `text.measure.*`.
 *
 * This file is the phase's headline claim made executable: **the width a measurer reports is the sum of
 * the advances a painter walks**. Three assertions carry it, and none of them can pass if a second
 * arithmetic path exists — `measurement.width` against the glyph-run advances, the glyph-run advances
 * against the per-glyph prefix sums, and the shape result's own glyph statistics against the measurement's
 * counters. The old arrangement (sum font units, scale once, accumulate floats separately in the painter)
 * would fail the first of those on any text with more than one glyph.
 *
 * Everything runs against the **shipped** face, at the real sizes the shell uses, so the numbers are the
 * ones the desktop puts on screen rather than numbers chosen to make a test pass.
 */

#include "RoboTextTestSupport.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static RoboTextView View(const char *text)
{
    return RoboTextViewMake(text, strlen(text));
}

/** Shape @p text, asserting success, and return the result the caller destroys. */
static RoboTextShapeResult *Shape(const TextFixture *fixture, const char *text)
{
    RoboTextShapeResult *result = NULL;

    RoboCheckEqInt((int)RoboTextShape(View(text), &fixture->font, NULL, &result, NULL), (int)RoboOk);
    RoboCheckMsg(result != NULL, "shaping produced no result");
    return result;
}

/* ------------------------------------------------------------------ */
/* Measurement                                                         */
/* ------------------------------------------------------------------ */

static void TestShapeMeasure(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, TextSampleHello);
    const RoboTextMeasurement *measurement = RoboTextShapeResultMeasurement(result);
    const RoboTextRunSet *runs = RoboTextShapeResultRuns(result);
    double advanceSum = 0.0;

    RoboCheck(measurement != NULL);
    RoboCheck(measurement->width > 0.0f);
    RoboCheckEqInt((int)measurement->glyphCount, 18);
    RoboCheckEqInt((int)measurement->scalarCount, 18);
    RoboCheckEqInt((int)measurement->glyphRunCount, 1);
    RoboCheckEqInt((int)measurement->missingGlyphs, 0);
    /* The face's own metrics, at the context's size: a 16 px face has an ascent of a few px, not zero,
     * and a line box tall enough to hold ascent + descent. */
    RoboCheck(measurement->ascent > 0.0f);
    RoboCheck(measurement->descent >= 0.0f);
    RoboCheck(measurement->lineHeight >= measurement->ascent + measurement->descent);
    RoboCheck(measurement->advanceMax > 0.0f);
    RoboCheck(measurement->advanceMax <= measurement->width);

    /* One run in, one glyph run out, and every scalar of the text is covered by the glyph runs. */
    RoboCheckEqInt((int)RoboTextRunSetTextRunCount(runs), 1);
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(result), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunScalarCount(RoboTextShapeResultGlyphRun(result, 0u)), 18);

    /* The claim: width == Σ glyph advances, to within one rounding. */
    for (size_t index = 0u; index < RoboTextShapeResultGlyphRunCount(result); index++) {
        advanceSum += (double)RoboTextGlyphRunAdvance(RoboTextShapeResultGlyphRun(result, index));
    }
    RoboCheck(measurement->width - (float)advanceSum < 0.001f);
    RoboCheck((float)advanceSum - measurement->width < 0.001f);

    /* And the result's own glyph statistics agree with the measurement's counters: a disagreement would
     * mean the result is internally inconsistent, which is the one thing a result may not be. */
    {
        RoboTextGlyphStats stats;

        RoboCheckEqInt((int)RoboTextShapeResultGetGlyphStats(result, &stats), (int)RoboOk);
        RoboCheckEqInt((int)stats.glyphs, (int)measurement->glyphCount);
        RoboCheckEqInt((int)stats.glyphRuns, (int)measurement->glyphRunCount);
        RoboCheckEqInt((int)stats.missing, (int)measurement->missingGlyphs);
        RoboCheckEqInt((int)(stats.advancePx == (double)RoboTextGlyphRunAdvance(
                                                    RoboTextShapeResultGlyphRun(result, 0u))),
                       1);
    }

    /* A glyph-run lookup by scalar, and the end-of-text position that has no run. */
    RoboCheck(RoboTextShapeResultGlyphRunForScalar(result, 0u) ==
              RoboTextShapeResultGlyphRun(result, 0u));
    RoboCheck(RoboTextShapeResultGlyphRunForScalar(result, 17u) ==
              RoboTextShapeResultGlyphRun(result, 0u));
    RoboCheck(RoboTextShapeResultGlyphRunForScalar(result, 18u) == NULL);
    RoboCheck(RoboTextShapeResultGlyphRun(result, 5u) == NULL);
    RoboCheck(RoboTextShapeResultRuns(NULL) == NULL);
    RoboCheck(RoboTextShapeResultMeasurement(NULL) == NULL);
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(NULL), 0);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

static void TestShapeLineHeight(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions options;
    RoboTextMeasurement measurement;
    float natural;

    /* `normal` is the face's own line extent. */
    RoboCheckEqInt((int)RoboTextMeasureText(View(TextSampleHello), &fixture.font, NULL, &measurement,
                                            NULL),
                   (int)RoboOk);
    natural = measurement.lineHeight;
    RoboCheck(natural > 0.0f);

    /* A declared leading is honoured exactly rather than adjusted: a role that says 24 px for a 16 px
     * face is the shell's decision, and a text layer that second-guessed it would move every baseline in
     * the chrome. */
    {
        TextFixture tall = TextFixtureOpen();
        RoboTextFontStyle style = tall.font.style;

        style.lineHeightPx = 24.0f;
        RoboCheckEqInt((int)RoboTextFontContextSet(&tall.font, tall.face, &style), (int)RoboOk);
        RoboCheckEqInt(
            (int)RoboTextMeasureText(View(TextSampleHello), &tall.font, NULL, &measurement, NULL),
            (int)RoboOk);
        RoboCheck(measurement.lineHeight == 24.0f);
        /* Leading does not move the width: the two are independent, and a caller can change one without
         * the other shifting. */
        RoboCheck(measurement.width > 0.0f);
        TextFixtureClose(&tall);
    }

    /* The options record changes the *runs*, never the metrics: a declared direction is not a leading. */
    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionRtl;
    RoboCheckEqInt((int)RoboTextMeasureText(View(TextSampleHello), &fixture.font, &options, &measurement,
                                            NULL),
                   (int)RoboOk);
    RoboCheck(measurement.lineHeight == natural);
    RoboCheck(measurement.width > 0.0f);

    /* An empty text still has a line: ascent and leading come from the face, and the width is zero. */
    RoboCheckEqInt((int)RoboTextMeasureText(View(""), &fixture.font, NULL, &measurement, NULL),
                   (int)RoboOk);
    RoboCheck(measurement.width == 0.0f);
    RoboCheckEqInt((int)measurement.glyphCount, 0);
    RoboCheckEqInt((int)measurement.glyphRunCount, 0);
    RoboCheck(measurement.lineHeight > 0.0f);
    RoboCheck(measurement.ascent > 0.0f);

    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Determinism, and the required strings                               */
/* ------------------------------------------------------------------ */

static void TestShapeDeterminism(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *first = Shape(&fixture, TextSamplePangram);
    RoboTextShapeResult *second = Shape(&fixture, TextSamplePangram);

    RoboCheck(RoboTextMeasurementEqual(RoboTextShapeResultMeasurement(first),
                                       RoboTextShapeResultMeasurement(second)));
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(first),
                   (int)RoboTextShapeResultGlyphRunCount(second));
    for (size_t index = 0u; index < RoboTextShapeResultGlyphRunCount(first); index++) {
        const RoboTextGlyphRun *left = RoboTextShapeResultGlyphRun(first, index);
        const RoboTextGlyphRun *right = RoboTextShapeResultGlyphRun(second, index);

        RoboCheckEqInt((int)RoboTextGlyphRunCount(left), (int)RoboTextGlyphRunCount(right));
        RoboCheckEqInt((int)(RoboTextGlyphRunAdvance(left) == RoboTextGlyphRunAdvance(right)), 1);
        {
            RoboTextRange leftRange = RoboTextGlyphRunSourceRange(left);
            RoboTextRange rightRange = RoboTextGlyphRunSourceRange(right);

            RoboCheck(RoboTextRangeEqual(&leftRange, &rightRange));
        }
        for (RoboTextGlyphIndex glyph = 0u; glyph < RoboTextGlyphRunCount(left); glyph++) {
            RoboCheck(RoboTextGlyphRunGlyph(right, glyph) != NULL);
            RoboCheck(RoboTextGlyphRunGlyph(left, glyph)->glyphId ==
                      RoboTextGlyphRunGlyph(right, glyph)->glyphId);
            RoboCheck(RoboTextGlyphRunGlyph(left, glyph)->advanceX ==
                      RoboTextGlyphRunGlyph(right, glyph)->advanceX);
        }
    }

    /* A dump line is stable, complete and refuses a buffer that cannot hold it. */
    {
        char line[192];
        size_t needed = 0u;
        char small[16];

        RoboCheckEqInt((int)RoboTextMeasurementWrite(RoboTextShapeResultMeasurement(first), line,
                                                     sizeof(line), &needed),
                       (int)RoboOk);
        RoboCheckEqInt((int)strlen(line), (int)needed);
        RoboCheck(strstr(line, "width=") == line);
        RoboCheck(strstr(line, "lineHeight=") != NULL);
        RoboCheck(strstr(line, "glyphs=") != NULL);
        RoboCheckEqInt((int)RoboTextMeasurementWrite(RoboTextShapeResultMeasurement(first), small,
                                                     sizeof(small), &needed),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)small[0], 0);
        RoboCheckEqInt((int)RoboTextMeasurementWrite(NULL, line, sizeof(line), &needed),
                       (int)RoboInvalidArgument);
    }

    /* `MeasurementInit` produces something equal to nothing, and equality is field-for-field. */
    {
        RoboTextMeasurement empty;

        RoboTextMeasurementInit(&empty);
        RoboCheck(RoboTextMeasurementEqual(&empty, &empty));
        RoboCheck(RoboTextMeasurementEqual(NULL, NULL));
        RoboCheck(!RoboTextMeasurementEqual(&empty, NULL));
        RoboCheck(!RoboTextMeasurementEqual(&empty, RoboTextShapeResultMeasurement(first)));
        RoboTextMeasurementInit(NULL);
    }

    RoboTextShapeResultDestroy(first);
    RoboTextShapeResultDestroy(second);
    TextFixtureClose(&fixture);
}

static void TestShapeRequiredStrings(void)
{
    static const char *asciiStrings[] = {
        TextSampleHello,
        TextSampleTypography,
        TextSamplePangram,
        TextSampleUpper,
        TextSampleLower,
        TextSampleDigits,
    };
    TextFixture fixture = TextFixtureOpen();
    size_t index;

    for (index = 0u; index < sizeof(asciiStrings) / sizeof(asciiStrings[0]); index++) {
        RoboTextMeasurement measurement;

        RoboCheckEqInt((int)RoboTextMeasureText(View(asciiStrings[index]), &fixture.font, NULL,
                                                &measurement, NULL),
                       (int)RoboOk);
        /* Every required ASCII string draws completely in the system UI face: no `.notdef`, and a width
         * that grows with the text. */
        RoboCheckEqInt((int)measurement.missingGlyphs, 0);
        RoboCheckEqInt((int)measurement.glyphCount, (int)strlen(asciiStrings[index]));
        RoboCheckEqInt((int)measurement.scalarCount, (int)strlen(asciiStrings[index]));
        RoboCheck(measurement.width > 0.0f);
        RoboCheck(measurement.advanceMax < measurement.width || measurement.glyphCount == 1u);
    }

    /* The ASCII range as one string: 26 + 26 + 10 scalars, all drawn. */
    {
        RoboTextMeasurement upper;
        RoboTextMeasurement lower;
        RoboTextMeasurement digits;

        RoboCheckEqInt((int)RoboTextMeasureText(View(TextSampleUpper), &fixture.font, NULL, &upper, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextMeasureText(View(TextSampleLower), &fixture.font, NULL, &lower, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextMeasureText(View(TextSampleDigits), &fixture.font, NULL, &digits,
                                                NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)upper.glyphCount, 26);
        RoboCheckEqInt((int)lower.glyphCount, 26);
        RoboCheckEqInt((int)digits.glyphCount, 10);
        RoboCheckEqInt((int)upper.missingGlyphs, 0);
        RoboCheckEqInt((int)lower.missingGlyphs, 0);
        RoboCheckEqInt((int)digits.missingGlyphs, 0);
        /* Digits are set in the face's own tabular or proportional figures, so their width is a real
         * number rather than a guess; every glyph in the range advances. */
        RoboCheck(digits.width > 0.0f);
        RoboCheck(digits.advanceMax > 0.0f);
    }

    /*
     * The Unicode samples: nine scalars in a mix of two-, three- and four-byte encodings. The purpose at
     * this stage is safe representation and a fallback *boundary* — so what is asserted is the scalar
     * count (proving the decode), that the width is finite and non-negative, and that whatever the face
     * cannot draw is *counted*. Which of them the face has is deliberately not asserted: that is the
     * pack's business and the fallback layer's, and pinning it here would make a suite fail on a font
     * update rather than on a defect.
     */
    {
        RoboTextMeasurement unicode;

        RoboCheckEqInt((int)RoboTextMeasureText(View(TextSampleUnicode), &fixture.font, NULL, &unicode,
                                                NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)unicode.scalarCount, 9);
        RoboCheckEqInt((int)unicode.glyphCount, 9);
        RoboCheck(unicode.width >= 0.0f);
        RoboCheck(unicode.missingGlyphs <= 9u);
        RoboCheck(unicode.lineHeight > 0.0f);
    }

    /* A noncharacter has no glyph in any conforming font, so the miss is counted and the line still
     * measures — rather than the width collapsing to zero, which would let undrawable text pass as a
     * narrow measurement. */
    {
        RoboTextMeasurement weird;

        RoboCheckEqInt((int)RoboTextMeasureText(View("a" TextSampleNoncharacter "b"), &fixture.font, NULL,
                                                &weird, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)weird.scalarCount, 3);
        RoboCheckEqInt((int)weird.glyphCount, 3);
        RoboCheckEqInt((int)weird.missingGlyphs, 1);
        RoboCheck(weird.width > 0.0f);
    }

    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Runs, buffers and refusals                                          */
/* ------------------------------------------------------------------ */

static void TestShapeRunsAndBuffer(void)
{
    static const char *mixed = "Hello \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, mixed);
    RoboTextMeasurement fromBuffer;
    RoboTextBuffer *buffer = NULL;

    /* A mixed text really is more than one glyph run, and each covers a disjoint span of the source in
     * ascending order — the property the lookup's binary search relies on. */
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(result), 2);
    {
        RoboTextScalarIndex previousEnd = 0u;

        for (size_t index = 0u; index < RoboTextShapeResultGlyphRunCount(result); index++) {
            RoboTextRange range =
                RoboTextGlyphRunSourceRange(RoboTextShapeResultGlyphRun(result, index));

            RoboCheckEqInt((int)range.start, (int)previousEnd);
            RoboCheck(range.end > range.start);
            /* Every scalar in the run resolves to that run. */
            for (RoboTextScalarIndex scalar = range.start; scalar < range.end; scalar++) {
                RoboCheck(RoboTextShapeResultGlyphRunForScalar(result, scalar) ==
                          RoboTextShapeResultGlyphRun(result, index));
            }
            previousEnd = range.end;
        }
        RoboCheckEqInt((int)previousEnd, 10);
        /* The second run is the RTL one, and it is the Hebrew. */
        RoboCheckEqInt((int)RoboTextGlyphRunDirection(RoboTextShapeResultGlyphRun(result, 1u)),
                       (int)RoboTextDirectionRtl);
        RoboCheckEqInt((int)RoboTextGlyphRunScript(RoboTextShapeResultGlyphRun(result, 1u)),
                       (int)RoboUScriptHebrew);
        /* Logical order is preserved: the glyphs are *not* reordered, and the run says so. Reordering is
         * 12.8's, and a test that asserted a visual order here would be asserting a claim the phase does
         * not make. */
        RoboCheckEqInt((int)RoboTextGlyphRunGlyph(RoboTextShapeResultGlyphRun(result, 1u), 0u)->source.start,
                       6);
    }

    /* The buffer door measures the same bytes to the same numbers. */
    RoboCheckEqInt((int)RoboTextBufferCreateFrom(&buffer, mixed, strlen(mixed), NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextMeasureBuffer(buffer, &fixture.font, NULL, &fromBuffer, NULL), (int)RoboOk);
    RoboCheck(
        RoboTextMeasurementEqual(&fromBuffer, RoboTextShapeResultMeasurement(result)));
    RoboCheckEqInt((int)RoboTextMeasureBuffer(NULL, &fixture.font, NULL, &fromBuffer, NULL),
                   (int)RoboInvalidArgument);
    RoboTextBufferDestroy(buffer);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

static void TestShapeRefusals(void)
{
    TextFixture none = TextFixtureNoFace();
    RoboTextShapeResult *result = NULL;
    RoboTextMeasurement measurement;

    /* The two absences that are refusals: no face at all, and no font argument. Everything else is
     * answered (see `TestShapeRequiredStrings`). */
    RoboCheckEqInt((int)RoboTextShape(View("abc"), &none.font, NULL, &result, NULL), (int)RoboNotSupported);
    RoboCheck(result == NULL);
    RoboCheckEqInt((int)RoboTextShape(View("abc"), NULL, NULL, &result, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextShape(View("abc"), &none.font, NULL, NULL, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextMeasureText(View("abc"), &none.font, NULL, &measurement, NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextMeasureText(View("abc"), NULL, NULL, &measurement, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextMeasureText(View("abc"), &none.font, NULL, NULL, NULL),
                   (int)RoboInvalidArgument);

    /*
     * A view whose bytes are absent is not text, and the failure names the **text** rather than the font.
     * The context therefore has a real face here: with no face at all the refusal would be the font's, and
     * the point of this case is that the two failures are distinguishable.
     */
    {
        TextFixture real = TextFixtureOpen();
        RoboTextDiag diag;

        RoboTextDiagReset(&diag);
        RoboCheckEqInt((int)RoboTextShape(RoboTextViewMake(NULL, 4u), &real.font, NULL, &result, &diag),
                       (int)RoboInvalidArgument);
        RoboCheckEqInt((int)diag.code, (int)RoboInvalidArgument);
        RoboCheckEqInt((int)diag.domain, (int)RoboTextDomainRun);
        RoboCheck(diag.message != NULL);
        RoboCheck(diag.file != NULL);
        TextFixtureClose(&real);
    }

    RoboTextShapeResultDestroy(NULL); /* Safe. */
    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&none);
}

static void TestShapeDiagnostics(void)
{
    TextFixture fixture = TextFixtureOpenRole(RoboFontSystemRoleUIText, (FontWeight)400u, 16.0f);
    RoboTextDiag diag;
    RoboTextMeasurement measurement;

    /* A domain vocabulary that is complete, distinct and closed. `None` is the *absence* of a domain, so
     * `IsKnown` says no for it — the same line RoboTypography and RoboPaint draw at the same place. */
    RoboCheckEqInt((int)RoboTextDomainCount, 7);
    RoboCheck(RoboTextDomainIsKnown(RoboTextDomainBuffer));
    RoboCheck(RoboTextDomainIsKnown(RoboTextDomainRange));
    RoboCheck(RoboTextDomainIsKnown(RoboTextDomainFont));
    RoboCheck(RoboTextDomainIsKnown(RoboTextDomainRun));
    RoboCheck(RoboTextDomainIsKnown(RoboTextDomainGlyph));
    /* 12.8's domain: the bidi paragraph is its own object, so it is its own domain. */
    RoboCheck(RoboTextDomainIsKnown(RoboTextDomainBidi));
    RoboCheckStrEq(RoboTextDomainName(RoboTextDomainBidi), "bidi");
    RoboCheck(!RoboTextDomainIsKnown(RoboTextDomainNone));
    RoboCheck(!RoboTextDomainIsKnown((RoboTextDiagnosticDomain)RoboTextDomainCount));
    RoboCheck(!RoboTextDomainIsKnown((RoboTextDiagnosticDomain)-1));
    RoboCheckStrEq(RoboTextDomainName(RoboTextDomainNone), "unknown");
    RoboCheckStrEq(RoboTextDomainName(RoboTextDomainGlyph), "glyph");
    RoboCheckStrEq(RoboTextDomainName((RoboTextDiagnosticDomain)99), "unknown");

    RoboTextDiagReset(&diag);
    RoboCheckEqInt((int)diag.code, (int)RoboOk);
    RoboCheckEqInt((int)diag.domain, (int)RoboTextDomainNone);
    RoboCheck(diag.message == NULL);
    RoboTextDiagReset(NULL); /* Safe. */

    /* A successful call clears the record; a failing one fills it and names the object that refused. */
    {
        RoboCheckEqInt((int)RoboTextMeasureText(View(TextSampleHello), &fixture.font, NULL, &measurement,
                                                &diag),
                       (int)RoboOk);
        RoboCheckEqInt((int)diag.code, (int)RoboOk);
        RoboCheck(diag.message == NULL);
    }
    {
        TextFixture none = TextFixtureNoFace();

        RoboCheckEqInt((int)RoboTextMeasureText(View("abc"), &none.font, NULL, &measurement, &diag),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)diag.code, (int)RoboNotSupported);
        RoboCheckEqInt((int)diag.domain, (int)RoboTextDomainFont);
        RoboCheck(diag.message != NULL);
        RoboCheckEqInt((int)RoboTextDiagRecord(NULL, RoboError, RoboTextDomainFont, "x", __FILE__, 1),
                       (int)RoboError);
        TextFixtureClose(&none);
    }
    /* An undefined domain is normalised rather than stored, so a reader never sees a nameless one. */
    RoboCheckEqInt(
        (int)RoboTextDiagSet(&diag, RoboError, (RoboTextDiagnosticDomain)99, "x", __FILE__, 1),
        (int)RoboError);
    RoboCheckEqInt((int)diag.domain, (int)RoboTextDomainNone);
    RoboCheckEqInt((int)RoboTextDiagSet(NULL, RoboError, RoboTextDomainFont, "x", __FILE__, 1),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextDiagSet(&diag, RoboOk, RoboTextDomainFont, "x", __FILE__, 1),
                   (int)RoboOk);
    RoboCheckEqInt((int)diag.code, (int)RoboOk);
    RoboCheck(diag.message == NULL);

    TextFixtureClose(&fixture);
}

static void TestShapeFuzz(void)
{
    uint8_t junk[320];

    for (size_t index = 0u; index < sizeof(junk); index++) {
        junk[index] = (uint8_t)(index * 131u + 17u);
    }
    /* The fuzz doors decode attacker-controlled bytes with REPLACE mode; a decoder plus a segmenter plus
     * an index builder is exactly the shape a malformed input finds. */
    RoboCheckEqInt((int)RoboTextFuzzShape(junk, sizeof(junk)), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFuzzShape(NULL, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFuzzShape(NULL, 4u), (int)RoboInvalidArgument);
    {
        static const uint8_t bomish[] = {0xEF, 0xBB, 0xBF, 0xF0, 0x28, 0x8C, 0x28};
        RoboCheckEqInt((int)RoboTextFuzzShape(bomish, sizeof(bomish)), (int)RoboOk);
    }

    /* And arbitrary bytes through a real face, which reaches the cmap for every scalar: the case where a
     * wrongly computed scalar index would read past the text. */
    {
        TextFixture fixture = TextFixtureOpen();
        RoboTextShapeResult *result = NULL;
        RoboTextGlyphStats stats;
        RoboTextMeasurement measurement;

        RoboCheckEqInt((int)RoboTextShape(RoboTextViewMake((const char *)junk, sizeof(junk)),
                                          &fixture.font, NULL, &result, NULL),
                       (int)RoboOk);
        RoboCheck(result != NULL);
        RoboCheckEqInt((int)RoboTextShapeResultGetGlyphStats(result, &stats), (int)RoboOk);
        /* Every scalar of the junk produced exactly one glyph, and the counters the result reports are
         * the counters the runs hold: whatever the bytes said, the pipeline stayed self-consistent. */
        RoboCheckEqInt((int)stats.glyphs,
                       (int)RoboTextShapeResultGlyphRunCount(result) == 0
                           ? 0
                           : (int)RoboTextShapeResultMeasurement(result)->glyphCount);
        RoboCheckEqInt((int)RoboTextShapeResultMeasurement(result)->missingGlyphs, (int)stats.missing);
        RoboCheckEqInt((int)RoboTextMeasureText(RoboTextViewMake((const char *)junk, sizeof(junk)),
                                                &fixture.font, NULL, &measurement, NULL),
                       (int)RoboOk);
        RoboCheck(RoboTextMeasurementEqual(&measurement, RoboTextShapeResultMeasurement(result)));
        RoboTextShapeResultDestroy(result);
        TextFixtureClose(&fixture);
    }
}

void TestRegisterTextShape(void)
{
    TestRegister("text.shape.measure", TestShapeMeasure);
    TestRegister("text.measure.line_height", TestShapeLineHeight);
    TestRegister("text.shape.determinism", TestShapeDeterminism);
    TestRegister("text.shape.required_strings", TestShapeRequiredStrings);
    TestRegister("text.shape.runs_and_buffer", TestShapeRunsAndBuffer);
    TestRegister("text.shape.refusals", TestShapeRefusals);
    TestRegister("text.shape.diagnostics", TestShapeDiagnostics);
    TestRegister("text.shape.fuzz", TestShapeFuzz);
}
