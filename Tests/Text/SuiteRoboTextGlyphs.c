/*
 * RoboText's glyphs (Phase 12.4) — `text.glyphs.*` and `text.mapping.*`.
 *
 * Every case here runs against the **shipped** system typeface pack: the face comes from the pack's UI
 * Text role through `FontSystemPackResolveRole`, so the advances asserted below are the face's own `hmtx`
 * values scaled through RoboText's one device door. A synthetic face would let all of this pass while the
 * real pack or the real scaling path was broken, which is the failure the fixture exists to prevent.
 *
 * Two assertions are the phase's contract in miniature: `advanceUnits` matches the glyph's device advance
 * once scaled, so the two are provably one number seen twice; and `missing` counts the noncharacter
 * rather than the advance being silently zero, so undrawable text cannot pass as a narrow measurement.
 */

#include "RoboTextTestSupport.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Generate the glyph run for @p text's first (and, for these samples, only) text run. */
static RoboTextGlyphRun *GenerateRun(const TextFixture *fixture, const char *text)
{
    RoboTextRunSet *set = NULL;
    RoboTextGlyphRun *run = NULL;

    RoboCheckEqInt((int)RoboTextRunSetCreate(RoboTextViewMake(text, strlen(text)), &fixture->font, NULL,
                                             &set, NULL),
                   (int)RoboOk);
    RoboCheckMsg(set != NULL && RoboTextRunSetTextRunCount(set) >= 1u,
                 "the sample did not segment into a run");
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(
                       RoboTextViewMake(text, strlen(text)), RoboTextRunSetTextRun(set, 0u), &run, NULL),
                   (int)RoboOk);
    RoboTextRunSetDestroy(set);
    RoboCheckMsg(run != NULL, "glyph run generation produced nothing");
    return run;
}

/* ------------------------------------------------------------------ */
/* Generation                                                          */
/* ------------------------------------------------------------------ */

static void TestGlyphGenerate(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextGlyphRun *run = GenerateRun(&fixture, TextSampleHello);
    double total = 0.0;

    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 18);
    RoboCheckEqInt((int)RoboTextGlyphRunScalarCount(run), 18);
    RoboCheckEqInt((int)RoboTextGlyphRunMissingGlyphs(run), 0);
    /*
     * Since 12.10 the mode is `Gsub`: the shipped pack's Inter carries a GSUB table with a record for Latin,
     * so a Latin run's ids are the cmap's result *and* whatever the face's own lookups did to it. That the
     * glyph count below is still the scalar count is the other half of the claim: nothing here invented a
     * glyph, so this face's lookups had nothing to join in "Hello, world!".
     */
    RoboCheckEqInt((int)RoboTextGlyphRunGenerationMode(run), (int)RoboTextGlyphGenerationGsub);
    RoboCheckStrEq(RoboTextGlyphGenerationModeName(RoboTextGlyphRunGenerationMode(run)), "cmap+gsub");
    RoboCheckEqInt((int)RoboTextGlyphRunScript(run), (int)RoboUScriptLatin);
    RoboCheckEqInt((int)RoboTextGlyphRunDirection(run), (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextGlyphRunBaseDirection(run), (int)RoboTextDirectionLtr);
    RoboCheck(RoboTextGlyphRunFont(run) != NULL);
    RoboCheck(RoboTextGlyphRunFont(run)->face == fixture.face);
    RoboCheckStrEq(RoboTextLanguageTag(RoboTextGlyphRunLanguage(run)), "");
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(run).start, 0);
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(run).end, 18);

    for (RoboTextGlyphIndex index = 0u; index < 18u; index++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, index);

        RoboCheck(glyph != NULL);
        RoboCheck(RoboTextGlyphIsValid(glyph));
        /* Latin text in a Latin face: every scalar has a glyph, and the id is not `.notdef`. */
        RoboCheckEqInt((int)glyph->mapped, 1);
        RoboCheck(glyph->glyphId != RoboGlyphIdNone);
        RoboCheckEqInt((int)glyph->metricsStatus, (int)RoboFontGlyphMetricsOk);
        /* Identity mapping, stated rather than implied: one glyph per scalar, source is that scalar. */
        RoboCheckEqInt((int)glyph->source.start, (int)index);
        RoboCheckEqInt((int)glyph->source.end, (int)index + 1);
        RoboCheckEqInt((int)RoboTextRangeLength(glyph->source), 1);
        RoboCheckEqInt((int)glyph->cluster, (int)index);
        /* A drawable glyph advances, and its advance is the face's own units scaled — not zero. */
        RoboCheck(glyph->advanceUnits > 0u);
        RoboCheck(glyph->advanceX > 0.0f);
        /* Nothing positions anything yet: the two vectors that would carry it are exactly zero. */
        RoboCheck(glyph->advanceY == 0.0f);
        RoboCheck(glyph->offsetX == 0.0f);
        RoboCheck(glyph->offsetY == 0.0f);
        total += (double)glyph->advanceX;
    }
    /* The recorded total is the sum of the glyphs' own advances, so a caller cannot get two answers. */
    RoboCheckEqInt((int)(RoboTextGlyphRunAdvance(run) == (float)total), 1);
    RoboCheck(RoboTextGlyphRunAdvance(run) > 0.0f);
    RoboCheck(RoboTextGlyphRunGlyph(run, 18u) == NULL);
    RoboTextGlyphRunDestroy(run);

    /* An empty run: no glyphs, no advance, and no failure. */
    {
        RoboTextRun empty;
        RoboTextView view = RoboTextViewMake("", 0u);

        memset(&empty, 0, sizeof(empty));
        empty.range = RoboTextRangeAt(0u);
        empty.font = fixture.font;
        RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &empty, &run, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 0);
        RoboCheck(RoboTextGlyphRunAdvance(run) == 0.0f);
        RoboCheckEqInt((int)RoboTextGlyphRunMissingGlyphs(run), 0);
        RoboTextGlyphRunDestroy(run);
    }
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Advances and the pen                                                */
/* ------------------------------------------------------------------ */

static void TestGlyphAdvances(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextGlyphRun *run = GenerateRun(&fixture, TextSamplePangram);
    size_t count = RoboTextGlyphRunCount(run);
    float upTo = 123.0f;
    double running = 0.0;

    RoboCheck(count == strlen(TextSamplePangram));
    /* `AdvanceUpTo(n)` is the whole run's advance, and `AdvanceUpTo(0)` is the origin. */
    RoboCheckEqInt((int)RoboTextGlyphRunAdvanceUpTo(run, 0u, &upTo, NULL), (int)RoboOk);
    RoboCheck(upTo == 0.0f);
    RoboCheckEqInt((int)RoboTextGlyphRunAdvanceUpTo(run, (RoboTextGlyphIndex)count, &upTo, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)(upTo == RoboTextGlyphRunAdvance(run)), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunAdvanceUpTo(run, (RoboTextGlyphIndex)count + 1u, &upTo, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunAdvanceUpTo(NULL, 0u, &upTo, NULL), (int)RoboInvalidArgument);

    /* Every prefix is the sum of the advances before it, monotonically: the pen a painter accumulates and
     * the positions this reports are the same arithmetic. */
    for (RoboTextGlyphIndex index = 0u; index <= count; index++) {
        RoboCheckEqInt((int)RoboTextGlyphRunAdvanceUpTo(run, index, &upTo, NULL), (int)RoboOk);
        RoboCheckEqInt((int)(upTo == (float)running), 1);
        if (index < count) {
            running += (double)RoboTextGlyphRunGlyph(run, index)->advanceX;
        }
    }

    /* And the advance is what the *size* says it is: doubling the size doubles every advance, which is the
     * property that makes the one scaling door worth having. */
    {
        TextFixture doubled = TextFixtureOpenRole(RoboFontSystemRoleUIText, (FontWeight)400u, 32.0f);
        RoboTextGlyphRun *other = GenerateRun(&doubled, TextSamplePangram);

        RoboCheckEqInt((int)RoboTextGlyphRunCount(other), (int)count);
        for (RoboTextGlyphIndex index = 0u; index < count; index++) {
            float small = RoboTextGlyphRunGlyph(run, index)->advanceX;
            float large = RoboTextGlyphRunGlyph(other, index)->advanceX;

            RoboCheck(large > small);
            RoboCheck(large - 2.0f * small < 0.02f && 2.0f * small - large < 0.02f);
        }
        RoboTextGlyphRunDestroy(other);
        TextFixtureClose(&doubled);
    }

    RoboTextGlyphRunDestroy(run);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Source mapping                                                      */
/* ------------------------------------------------------------------ */

static void TestGlyphMapping(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextGlyphRun *run = GenerateRun(&fixture, TextSampleTypography);
    RoboTextRange range;

    for (RoboTextGlyphIndex index = 0u; index < RoboTextGlyphRunCount(run); index++) {
        RoboTextGlyphIndex first = 99u;
        RoboTextGlyphIndex found = 99u;
        RoboTextGlyphIndex count = 99u;

        RoboCheckEqInt((int)RoboTextGlyphRunSourceOf(run, index, &range, NULL), (int)RoboOk);
        RoboCheckEqInt((int)range.start, (int)index);
        RoboCheckEqInt((int)range.end, (int)index + 1);

        /* Scalar → glyphs uses absolute scalar indices, and today answers exactly one glyph. */
        RoboCheckEqInt((int)RoboTextGlyphRunGlyphsForScalar(run, index, &first, &count, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)count, 1);
        RoboCheckEqInt((int)first, (int)index);

        RoboCheckEqInt((int)RoboTextGlyphRunGlyphForScalarIndex(run, index, &found, NULL), (int)RoboOk);
        RoboCheckEqInt((int)found, (int)index);
    }

    /* The refusals: past the end of the glyphs, and a scalar outside the run. */
    RoboCheckEqInt((int)RoboTextGlyphRunSourceOf(run, (RoboTextGlyphIndex)RoboTextGlyphRunCount(run),
                                                 &range, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunSourceOf(NULL, 0u, &range, NULL), (int)RoboInvalidArgument);
    {
        RoboTextGlyphIndex first = 0u;
        RoboTextGlyphIndex count = 0u;

        RoboCheckEqInt((int)RoboTextGlyphRunGlyphsForScalar(run, 999u, &first, &count, NULL),
                       (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyphForScalarIndex(run, 999u, &first, NULL),
                       (int)RoboInvalidArgument);
    }

    /* A run that does not lie in the text it is generated for is refused, in both directions. */
    {
        RoboTextRunSet *set = NULL;
        RoboTextRun bad;
        RoboTextGlyphRun *none = NULL;

        RoboCheckEqInt((int)RoboTextRunSetCreate(RoboTextViewMake("abc", 3u), &fixture.font, NULL, &set,
                                                 NULL),
                       (int)RoboOk);
        bad = *RoboTextRunSetTextRun(set, 0u);
        bad.range = RoboTextRangeMake(0u, 9u);
        RoboCheckEqInt((int)RoboTextGlyphRunGenerate(RoboTextViewMake("abc", 3u), &bad, &none, NULL),
                       (int)RoboInvalidArgument);
        RoboCheck(none == NULL);
        bad.range = RoboTextRangeMake(2u, 1u);
        RoboCheckEqInt((int)RoboTextGlyphRunGenerate(RoboTextViewMake("abc", 3u), &bad, &none, NULL),
                       (int)RoboInvalidArgument);
        RoboTextRunSetDestroy(set);
    }

    RoboTextGlyphRunDestroy(run);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Missing glyphs, and the no-face refusal                             */
/* ------------------------------------------------------------------ */

static void TestGlyphMissing(void)
{
    static const char *text = "a" TextSampleNoncharacter "b";
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = NULL;
    const RoboTextGlyphRun *missingRun;
    const RoboTextGlyph *missing;
    RoboTextGlyphIndex first = 99u;
    RoboTextGlyphIndex count = 99u;

    /*
     * `U+FDD0` is a noncharacter: **no** conforming font has a glyph for it, which is what makes this
     * assertion about RoboText rather than about which glyphs this build's face carries.
     *
     * The case is written against the **shape result** and looks the glyph up by *scalar* rather than by
     * index, because how many runs the text segments into is a fact about the noncharacter's script rather
     * than about the glyphs: if `Zzzz` is classified Common it merges into the Latin, and if it is
     * classified as its own script it does not. Both are defensible answers, neither is what this case is
     * testing, and a test that pinned the run count would be pinning the wrong thing — and did, once.
     */
    RoboCheckEqInt((int)RoboTextShape(RoboTextViewMake(text, strlen(text)), &fixture.font, NULL, &result,
                                      NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextShapeResultMeasurement(result)->scalarCount, 3);
    RoboCheckEqInt((int)RoboTextShapeResultMeasurement(result)->glyphCount, 3);

    missingRun = RoboTextShapeResultGlyphRunForScalar(result, 1u);
    RoboCheck(missingRun != NULL);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyphsForScalar(missingRun, 1u, &first, &count, NULL), (int)RoboOk);
    RoboCheckEqInt((int)count, 1);
    missing = RoboTextGlyphRunGlyph(missingRun, first);
    RoboCheck(missing != NULL);
    RoboCheckEqInt((int)missing->mapped, 0);
    /* `.notdef` is what gets drawn, and it is a drawable id (0) rather than "no glyph at all". */
    RoboCheckEqInt((int)missing->glyphId, (int)RoboGlyphIdNone);
    RoboCheckEqInt((int)missing->source.start, 1);
    RoboCheckEqInt((int)missing->source.end, 2);

    /* The mapping still holds for the scalars on either side, looked up the same way. */
    {
        const RoboTextGlyphRun *run = RoboTextShapeResultGlyphRunForScalar(result, 0u);

        RoboCheck(run != NULL);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyphsForScalar(run, 0u, &first, &count, NULL), (int)RoboOk);
        RoboCheckEqInt((int)count, 1);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, first)->mapped, 1);

        run = RoboTextShapeResultGlyphRunForScalar(result, 2u);
        RoboCheck(run != NULL);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyphsForScalar(run, 2u, &first, &count, NULL), (int)RoboOk);
        RoboCheckEqInt((int)count, 1);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, first)->mapped, 1);
    }

    /* The measurement counts the miss and still has a width: undrawable text does not pass as a narrow
     * line, which is the failure `missingGlyphs` exists to make visible. */
    RoboCheckEqInt((int)RoboTextShapeResultMeasurement(result)->missingGlyphs, 1);
    RoboCheck(RoboTextShapeResultMeasurement(result)->width > 0.0f);

    {
        RoboTextGlyphStats stats;

        RoboCheckEqInt((int)RoboTextShapeResultGetGlyphStats(result, &stats), (int)RoboOk);
        RoboCheckEqInt((int)stats.glyphRuns, (int)RoboTextShapeResultGlyphRunCount(result));
        RoboCheck(stats.glyphRuns >= 1u);
        RoboCheckEqInt((int)stats.glyphs, 3);
        RoboCheckEqInt((int)stats.missing, 1);
        /* No substitution and no positioning exist, so neither counter can be anything but zero: a
         * non-zero value here would mean the placeholder had grown behaviour nobody documented. */
        RoboCheckEqInt((int)stats.multiScalarSources, 0);
        RoboCheckEqInt((int)stats.positioned, 0);
        /* The sum of the runs' advances, accumulated the way the measurement accumulates it. */
        {
            double total = 0.0;

            for (size_t index = 0u; index < RoboTextShapeResultGlyphRunCount(result); index++) {
                total += (double)RoboTextGlyphRunAdvance(RoboTextShapeResultGlyphRun(result, index));
            }
            RoboCheck(stats.advancePx - total < 0.001);
            RoboCheck(total - stats.advancePx < 0.001);
        }
    }
    RoboTextShapeResultDestroy(result);

    /* A context with no face is refused rather than answered with an empty run: "no glyphs" and "no font"
     * are different states, and a caller that conflated them would draw a blank where it should have said
     * so. */
    {
        TextFixture none = TextFixtureNoFace();
        RoboTextRunSet *set = NULL;
        RoboTextGlyphRun *absent = NULL;
        RoboTextView view = RoboTextViewMake("abc", 3u);

        RoboCheckEqInt((int)RoboTextRunSetCreate(view, &none.font, NULL, &set, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, RoboTextRunSetTextRun(set, 0u), &absent, NULL),
                       (int)RoboNotSupported);
        RoboCheck(absent == NULL);
        RoboTextRunSetDestroy(set);
        TextFixtureClose(&none);
    }
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The dump, and determinism                                           */
/* ------------------------------------------------------------------ */

static void TestGlyphDumpAndDeterminism(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextGlyphRun *first = GenerateRun(&fixture, TextSampleHello);
    RoboTextGlyphRun *second = GenerateRun(&fixture, TextSampleHello);
    char line[192];
    size_t needed = 0u;

    /* Two generations of one text, one face, one setting are identical glyph for glyph. */
    RoboCheckEqInt((int)RoboTextGlyphRunCount(first), (int)RoboTextGlyphRunCount(second));
    for (RoboTextGlyphIndex index = 0u; index < RoboTextGlyphRunCount(first); index++) {
        RoboCheck(RoboTextGlyphEqual(RoboTextGlyphRunGlyph(first, index),
                                    RoboTextGlyphRunGlyph(second, index)));
    }
    RoboTextGlyphRunDestroy(second);

    /* The dump the visual test surface prints: a complete line, and a refusal rather than a truncation. */
    RoboCheckEqInt((int)RoboTextGlyphWrite(RoboTextGlyphRunGlyph(first, 0u), line, sizeof(line), &needed),
                   (int)RoboOk);
    RoboCheck(needed > 0u);
    RoboCheckEqInt((int)strlen(line), (int)needed);
    RoboCheck(strstr(line, "id=") == line);
    RoboCheck(strstr(line, "mapped=1") != NULL);
    RoboCheck(strstr(line, "metrics=ok") != NULL);
    RoboCheck(strstr(line, "scalar=0..1") != NULL);

    {
        char small[8];
        size_t required = 0u;

        RoboCheckEqInt((int)RoboTextGlyphWrite(RoboTextGlyphRunGlyph(first, 0u), small, sizeof(small),
                                               &required),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)required, (int)needed); /* The caller learns what it needed. */
        RoboCheckEqInt((int)small[0], 0);           /* And nothing partial was written. */
    }
    RoboCheckEqInt((int)RoboTextGlyphWrite(NULL, line, sizeof(line), &needed), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphWrite(RoboTextGlyphRunGlyph(first, 0u), line, 0u, &needed),
                   (int)RoboInvalidArgument);

    /* A null glyph is not valid, and validity is a property of the record rather than of a face. */
    {
        RoboTextGlyph glyph;

        RoboTextGlyphInit(&glyph);
        RoboCheck(!RoboTextGlyphIsValid(&glyph));
        RoboCheck(!RoboTextGlyphIsValid(NULL));
        glyph.source = RoboTextRangeMake(3u, 5u);
        RoboCheck(RoboTextGlyphIsValid(&glyph));
        glyph.source = RoboTextRangeMake(5u, 3u);
        RoboCheck(!RoboTextGlyphIsValid(&glyph));
        RoboCheck(RoboTextGlyphEqual(NULL, NULL));
        RoboCheck(!RoboTextGlyphEqual(&glyph, NULL));
    }
    RoboTextGlyphRunDestroy(first);
    TextFixtureClose(&fixture);
}

static void TestGlyphFuzz(void)
{
    uint8_t junk[192];

    for (size_t index = 0u; index < sizeof(junk); index++) {
        junk[index] = (uint8_t)(index * 53u + 3u);
    }
    RoboCheckEqInt((int)RoboTextFuzzGlyphs(junk, sizeof(junk)), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFuzzGlyphs(NULL, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFuzzGlyphs(NULL, 8u), (int)RoboInvalidArgument);

    /* Arbitrary bytes through a *real* face: the decisive case, because the cmap door is reached here and
     * a scalar index computed wrongly would read past the text. */
    {
        TextFixture fixture = TextFixtureOpen();
        RoboTextView view = RoboTextViewMake((const char *)junk, sizeof(junk));
        RoboTextRunSet *set = NULL;

        RoboCheckEqInt((int)RoboTextRunSetCreate(view, &fixture.font, NULL, &set, NULL), (int)RoboOk);
        for (size_t index = 0u; index < RoboTextRunSetTextRunCount(set); index++) {
            RoboTextGlyphRun *run = NULL;
            Result status = RoboTextGlyphRunGenerate(view, RoboTextRunSetTextRun(set, index), &run, NULL);

            /* Either it generated or it refused; never a crash and never a half-built run. */
            if (status == RoboOk) {
                RoboCheck(run != NULL);
                RoboCheckEqInt((int)RoboTextGlyphRunCount(run),
                               (int)RoboTextRangeLength(RoboTextRunSetTextRun(set, index)->range));
                RoboTextGlyphRunDestroy(run);
            } else {
                RoboCheck(run == NULL);
            }
        }
        RoboTextRunSetDestroy(set);
        TextFixtureClose(&fixture);
    }
}

void TestRegisterTextGlyphs(void)
{
    TestRegister("text.glyphs.generate", TestGlyphGenerate);
    TestRegister("text.glyphs.advances", TestGlyphAdvances);
    TestRegister("text.mapping.source", TestGlyphMapping);
    TestRegister("text.glyphs.missing", TestGlyphMissing);
    TestRegister("text.glyphs.dump_determinism", TestGlyphDumpAndDeterminism);
    TestRegister("text.glyphs.fuzz", TestGlyphFuzz);
}
