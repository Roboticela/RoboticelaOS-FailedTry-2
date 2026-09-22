/*
 * RoboText's analysis runs (Phase 12.5) — `text.analysis.*`.
 *
 * The suite is the phase's canonical data flow made executable: text in, and out come runs that each carry
 * a real script, a real classified direction and a real selected face, with the ranges still addressable in
 * scalars. The three cases that matter most are the ones a plausible-but-wrong implementation would fail:
 *
 *   `mixed_scripts`   the Latin/Han boundary is where the font changes, and the second run's face is
 *                     *absent* rather than the primary's — the pack ships no CJK family, and the honest
 *                     record says so instead of drawing the Latin face's `.notdef` for Han text.
 *   `combining_marks` a mark whose own coverage answer is "no face" still joins its base's run, which is
 *                     the guarantee that a base and its accent are never two faces.
 *   `shape_request`   the request is populated from the run and turns back into the `RoboTextRun` the 12.4
 *                     generator consumes, so the analysis path reaches real glyphs through one pipeline.
 */

#include "RoboTextTestSupport.h"

#include <string.h>

#define SampleHan "\xE4\xB8\xAD\xE7\x95\x8C"          /* 世界 */
#define SampleHebrew "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D" /* שלום */
#define SampleMark "\xF3\xA0\x84\x80"                  /* U+E0100, a combining mark Inter does not cover */
#define SampleAcute "\xCC\x81"                         /* U+0301 */

static RoboTextView ViewOf(const char *text)
{
    return RoboTextViewMake(text, strlen(text));
}

static RoboTextFontContext NoFaceContext(void)
{
    RoboTextFontContext context;
    RoboTextFontStyle style;

    RoboTextFontContextInit(&context);
    RoboTextFontStyleInit(&style);
    RoboCheckEqInt((int)RoboTextFontContextSet(&context, NULL, &style), (int)RoboOk);
    return context;
}

/** A selector over @p face as the primary, asserting every step. */
static RoboTextFontSelector *SelectorOver(const RoboTextFontContext *primary)
{
    RoboTextFontSelector *selector = NULL;

    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&selector, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, primary, NULL), (int)RoboOk);
    return selector;
}

/* ------------------------------------------------------------------ */
/* One script                                                          */
/* ------------------------------------------------------------------ */

static void TestAnalysisSingleScript(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontSelector *selector = SelectorOver(&fixture.font);
    RoboTextAnalysis *analysis = NULL;
    const RoboTextAnalysisRun *run;
    RoboTextAnalysisStats stats;
    RoboTextShapeOptions options;

    RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf("Hello"), selector, NULL, &analysis, NULL),
                   (int)RoboOk);
    RoboCheck(analysis != NULL);
    RoboCheckEqInt((int)RoboTextAnalysisScalarCount(analysis), 5);
    RoboCheckEqInt((int)RoboTextAnalysisRunCount(analysis), 1);
    RoboCheckEqInt((int)RoboTextAnalysisScriptRunCount(analysis), 1);
    RoboCheckEqInt((int)RoboTextAnalysisBaseDirection(analysis), (int)RoboTextDirectionLtr);

    run = RoboTextAnalysisRunAt(analysis, 0u);
    RoboCheck(run != NULL);
    RoboCheckEqInt((int)run->range.start, 0);
    RoboCheckEqInt((int)run->range.end, 5);
    RoboCheckEqInt((int)run->script, (int)RoboUScriptLatin);
    RoboCheckEqInt((int)run->direction, (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)run->font.source, (int)RoboTextFontSourcePrimary);
    RoboCheck(run->font.face == fixture.face);
    RoboCheck(run->font.sizePx == 16.0f);
    RoboCheck(RoboTextAnalysisRunAt(analysis, 1u) == NULL);
    RoboCheck(RoboTextAnalysisRunAtScalar(analysis, 0u) == run);
    RoboCheck(RoboTextAnalysisRunAtScalar(analysis, 4u) == run);
    /* The end-of-text position has no run *containing* it, which is the caret convention. */
    RoboCheck(RoboTextAnalysisRunAtScalar(analysis, 5u) == NULL);

    RoboCheckEqInt((int)RoboTextAnalysisGetStats(analysis, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.runs, 1);
    RoboCheckEqInt((int)stats.primaryRuns, 1);
    RoboCheckEqInt((int)stats.fallbackRuns, 0);
    RoboCheckEqInt((int)stats.unresolvedRuns, 0);
    RoboCheckEqInt((int)stats.rtlRuns, 0);
    RoboCheckEqInt((int)stats.combiningMarks, 0);
    RoboCheckEqInt((int)stats.scalars, 5);

    /* The language is carried to the analysis and echoed, and it never decides the script. */
    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextLanguageSet(&options.language, "en-US"), (int)RoboOk);
    RoboTextAnalysisDestroy(analysis);
    analysis = NULL;
    RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf("Hello"), selector, &options, &analysis, NULL),
                   (int)RoboOk);
    RoboCheckStrEq(RoboTextLanguageTag(RoboTextAnalysisLanguage(analysis)), "en-US");
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 0u)->script, (int)RoboUScriptLatin);
    RoboCheck(RoboTextAnalysisStyle(analysis)->sizePx == 16.0f);

    RoboTextAnalysisDestroy(analysis);
    RoboTextFontSelectorDestroy(selector);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Mixed scripts, and where the font changes                           */
/* ------------------------------------------------------------------ */

static void TestAnalysisMixedScripts(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontSelector *selector = SelectorOver(&fixture.font);
    RoboTextAnalysis *analysis = NULL;
    const RoboTextAnalysisRun *first;
    const RoboTextAnalysisRun *second;
    RoboTextAnalysisStats stats;

    /* `"Hello 世界"`: the space takes the Latin carry, so the boundary is scalar 6. */
    RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf("Hello " SampleHan), selector, NULL, &analysis, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextAnalysisScalarCount(analysis), 8);
    RoboCheckEqInt((int)RoboTextAnalysisScriptRunCount(analysis), 2);
    RoboCheckEqInt((int)RoboTextAnalysisRunCount(analysis), 2);

    first = RoboTextAnalysisRunAt(analysis, 0u);
    second = RoboTextAnalysisRunAt(analysis, 1u);
    RoboCheckEqInt((int)first->range.start, 0);
    RoboCheckEqInt((int)first->range.end, 6);
    RoboCheckEqInt((int)first->script, (int)RoboUScriptLatin);
    RoboCheckEqInt((int)first->font.source, (int)RoboTextFontSourcePrimary);

    RoboCheckEqInt((int)second->range.start, 6);
    RoboCheckEqInt((int)second->range.end, 8);
    RoboCheckEqInt((int)second->script, (int)RoboUScriptHan);
    /* No CJK family is installed, so the second run has *no* face — not the Latin face's `.notdef`. */
    RoboCheckEqInt((int)second->font.source, (int)RoboTextFontSourceNone);
    RoboCheck(second->font.face == NULL);

    RoboCheckEqInt((int)RoboTextAnalysisGetStats(analysis, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.primaryRuns, 1);
    RoboCheckEqInt((int)stats.unresolvedRuns, 1);
    RoboCheckEqInt((int)stats.scriptRuns, 2);

    RoboTextAnalysisDestroy(analysis);
    analysis = NULL;

    /* The same text through a chain whose only entry is the Latin face: still two runs. */
    {
        RoboTextFontContext noFace = NoFaceContext();
        RoboTextFontCandidate candidate;
        RoboTextFontFallback chain;
        RoboTextFontSelector *fallbackSelector = SelectorOver(&noFace);

        memset(&candidate, 0, sizeof(candidate));
        candidate.family = "Inter";
        candidate.face = fixture.face;
        chain.entries = &candidate;
        chain.count = 1u;
        RoboCheckEqInt((int)RoboTextFontSelectorSetFallback(fallbackSelector, &chain, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf("Hello " SampleHan), fallbackSelector, NULL,
                                                   &analysis, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextAnalysisRunCount(analysis), 2);
        RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 0u)->font.source,
                       (int)RoboTextFontSourceFallback);
        RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 1u)->font.source,
                       (int)RoboTextFontSourceNone);
        RoboCheckEqInt((int)RoboTextAnalysisGetStats(analysis, &stats), (int)RoboOk);
        RoboCheckEqInt((int)stats.fallbackRuns, 1);
        RoboCheckEqInt((int)stats.unresolvedRuns, 1);
        RoboTextAnalysisDestroy(analysis);
        analysis = NULL;
        RoboTextFontSelectorDestroy(fallbackSelector);
    }

    /* `"Hello שלום"`: the Hebrew is strong RTL, so the direction changes even though the script does. */
    RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf("Hello " SampleHebrew), selector, NULL, &analysis,
                                               NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextAnalysisRunCount(analysis), 2);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 0u)->direction, (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 1u)->direction, (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 1u)->script, (int)RoboUScriptHebrew);
    RoboCheckEqInt((int)RoboTextAnalysisGetStats(analysis, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.rtlRuns, 1);

    RoboTextAnalysisDestroy(analysis);
    RoboTextFontSelectorDestroy(selector);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Combining marks                                                     */
/* ------------------------------------------------------------------ */

static void TestAnalysisCombiningMarks(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontSelector *selector = SelectorOver(&fixture.font);
    RoboTextAnalysis *analysis = NULL;
    RoboTextAnalysisStats stats;

    /* A covered mark stays with its base, and the run does not split. */
    RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf("e" SampleAcute), selector, NULL, &analysis, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextAnalysisRunCount(analysis), 1);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 0u)->range.end, 2);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 0u)->font.source,
                   (int)RoboTextFontSourcePrimary);
    RoboCheckEqInt((int)RoboTextAnalysisGetStats(analysis, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.combiningMarks, 1);
    RoboTextAnalysisDestroy(analysis);
    analysis = NULL;

    /*
     * The case the rule exists for: U+E0100 has no face at all, so an implementation that selected a font
     * per scalar would produce a second run with `none` — splitting a base character from its mark. The run
     * keeps the base's face instead, and the mark is still counted.
     */
    RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf("e" SampleMark), selector, NULL, &analysis, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextAnalysisRunCount(analysis), 1);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 0u)->range.end, 2);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 0u)->font.source,
                   (int)RoboTextFontSourcePrimary);
    RoboCheck(RoboTextAnalysisRunAt(analysis, 0u)->font.face == fixture.face);
    RoboCheckEqInt((int)RoboTextAnalysisGetStats(analysis, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.combiningMarks, 1);
    RoboCheckEqInt((int)stats.unresolvedRuns, 0);
    RoboTextAnalysisDestroy(analysis);
    analysis = NULL;

    /* A mark at the very start has no base to inherit, so it is selected for like any other scalar. */
    RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf(SampleMark "e"), selector, NULL, &analysis, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextAnalysisRunCount(analysis), 2);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 0u)->font.source, (int)RoboTextFontSourceNone);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 1u)->font.source,
                   (int)RoboTextFontSourcePrimary);

    RoboTextAnalysisDestroy(analysis);
    RoboTextFontSelectorDestroy(selector);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The shape request, and the bridge to real glyphs                    */
/* ------------------------------------------------------------------ */

static void TestAnalysisShapeRequest(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontSelector *selector = SelectorOver(&fixture.font);
    RoboTextAnalysis *analysis = NULL;
    RoboTextShapeRequest request;
    RoboTextShapeOptions options;
    RoboTextView view = ViewOf("Hello");
    RoboTextRun run;
    RoboTextGlyphRun *glyphs = NULL;

    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextLanguageSet(&options.language, "ur"), (int)RoboOk);
    options.baseDirection = RoboTextDirectionLtr;
    RoboCheckEqInt((int)RoboTextShapeOptionsAddFeature(&options, "kern"), (int)RoboOk);

    RoboCheckEqInt((int)RoboTextAnalysisCreate(view, selector, &options, &analysis, NULL), (int)RoboOk);
    RoboTextShapeRequestInit(&request);
    RoboCheckEqInt((int)RoboTextAnalysisShapeRequest(analysis, 0u, &request), (int)RoboOk);

    /* Real data, field by field — no placeholders. */
    RoboCheck(RoboTextShapeRequestIsUsable(&request));
    RoboCheck(request.text.bytes == view.bytes);
    RoboCheckEqInt((int)request.range.start, 0);
    RoboCheckEqInt((int)request.range.end, 5);
    RoboCheckEqInt((int)request.script, (int)RoboUScriptLatin);
    RoboCheckEqInt((int)request.direction, (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)request.baseDirection, (int)RoboTextDirectionLtr);
    RoboCheck(request.font.face == fixture.face);
    RoboCheck(request.font.style.sizePx == 16.0f);
    RoboCheckStrEq(RoboTextLanguageTag(&request.language), "ur");
    RoboCheckEqInt((int)request.featureCount, 1);
    RoboCheckStrEq(request.features[0], "kern");

    /* The bridge: the request denotes exactly the run the 12.4 generator consumes. */
    RoboCheckEqInt((int)RoboTextShapeRequestToRun(&request, &run), (int)RoboOk);
    RoboCheckEqInt((int)run.range.start, 0);
    RoboCheckEqInt((int)run.range.end, 5);
    RoboCheckEqInt((int)run.script, (int)RoboUScriptLatin);
    RoboCheck(run.font.face == fixture.face);
    RoboCheckStrEq(RoboTextLanguageTag(&run.language), "ur");
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &run, &glyphs, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(glyphs), 5);
    RoboCheckEqInt((int)RoboTextGlyphRunMissingGlyphs(glyphs), 0);
    RoboCheck(RoboTextGlyphRunAdvance(glyphs) > 0.0f);
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(glyphs).start, 0);
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(glyphs).end, 5);
    RoboTextGlyphRunDestroy(glyphs);

    /* Out-of-range and NULL arguments are refused rather than answered. */
    RoboCheckEqInt((int)RoboTextAnalysisShapeRequest(analysis, 1u, &request), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextAnalysisShapeRequest(analysis, 0u, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextAnalysisShapeRequest(NULL, 0u, &request), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextShapeRequestToRun(&request, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextShapeRequestToRun(NULL, &run), (int)RoboInvalidArgument);
    {
        RoboTextShapeRequest empty;

        RoboTextShapeRequestInit(&empty);
        RoboCheck(!RoboTextShapeRequestIsUsable(&empty));
        RoboCheck(!RoboTextShapeRequestIsUsable(NULL));
        RoboTextShapeRequestInit(NULL);
    }

    RoboTextAnalysisDestroy(analysis);
    RoboTextFontSelectorDestroy(selector);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Empty text, refusals, determinism and fuzz                          */
/* ------------------------------------------------------------------ */

static void TestAnalysisEmptyRefusalsDeterminism(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontSelector *selector = SelectorOver(&fixture.font);
    RoboTextAnalysis *analysis = NULL;
    RoboTextAnalysisStats stats;

    /* No text is a fact about a document: an empty analysis, and every door answers. */
    RoboCheckEqInt((int)RoboTextAnalysisCreate(RoboTextViewMake(NULL, 0u), selector, NULL, &analysis, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextAnalysisScalarCount(analysis), 0);
    RoboCheckEqInt((int)RoboTextAnalysisRunCount(analysis), 0);
    RoboCheckEqInt((int)RoboTextAnalysisScriptRunCount(analysis), 0);
    RoboCheck(RoboTextAnalysisRunAt(analysis, 0u) == NULL);
    RoboCheck(RoboTextAnalysisRunAtScalar(analysis, 0u) == NULL);
    RoboCheckEqInt((int)RoboTextAnalysisBaseDirection(analysis), (int)RoboTextDirectionUnknown);
    RoboCheckEqInt((int)RoboTextAnalysisScriptHint(analysis), (int)RoboTextScriptCommon);
    RoboCheckEqInt((int)RoboTextAnalysisGetStats(analysis, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.runs, 0);
    RoboTextAnalysisDestroy(analysis);
    analysis = NULL;

    /* Refusals. */
    RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf("A"), NULL, NULL, &analysis, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf("A"), selector, NULL, NULL, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextAnalysisCreate(RoboTextViewMake(NULL, 3u), selector, NULL, &analysis, NULL),
                   (int)RoboInvalidArgument);
    {
        RoboTextShapeOptions broken;

        RoboTextShapeOptionsInit(&broken);
        broken.featureCount = RoboTextMaxFeatures + 1u;
        RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf("A"), selector, &broken, &analysis, NULL),
                       (int)RoboInvalidArgument);
    }
    /* Every accessor answers for a NULL analysis rather than crashing. */
    RoboCheckEqInt((int)RoboTextAnalysisRunCount(NULL), 0);
    RoboCheck(RoboTextAnalysisRunAt(NULL, 0u) == NULL);
    RoboCheck(RoboTextAnalysisRunAtScalar(NULL, 0u) == NULL);
    RoboCheck(RoboTextAnalysisScriptRun(NULL, 0u) == NULL);
    RoboCheckEqInt((int)RoboTextAnalysisScriptRunCount(NULL), 0);
    RoboCheckEqInt((int)RoboTextAnalysisScalarCount(NULL), 0);
    RoboCheckEqInt((int)RoboTextAnalysisBaseDirection(NULL), (int)RoboTextDirectionUnknown);
    RoboCheckStrEq(RoboTextLanguageTag(RoboTextAnalysisLanguage(NULL)), "");
    RoboCheck(RoboTextAnalysisStyle(NULL) != NULL);
    RoboCheckEqInt((int)RoboTextAnalysisGetStats(NULL, &stats), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextAnalysisGetStats(NULL, NULL), (int)RoboInvalidArgument);
    RoboTextAnalysisDestroy(NULL);

    /*
     * Determinism: two analyses of the same text under the same configuration produce the same runs, run
     * for run and field for field — including the selected face. The phase requires it for visual
     * regression, and the comparison is over the resolved *identity* rather than a pointer so that it holds
     * across two selectors.
     */
    {
        static const char *text = "Hello " SampleHan " " SampleHebrew;
        RoboTextAnalysis *first = NULL;
        RoboTextAnalysis *second = NULL;

        RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf(text), selector, NULL, &first, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextAnalysisCreate(ViewOf(text), selector, NULL, &second, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextAnalysisRunCount(first), (int)RoboTextAnalysisRunCount(second));
        for (size_t index = 0u; index < RoboTextAnalysisRunCount(first); index++) {
            const RoboTextAnalysisRun *a = RoboTextAnalysisRunAt(first, index);
            const RoboTextAnalysisRun *b = RoboTextAnalysisRunAt(second, index);

            RoboCheck(RoboTextRangeEqual(&a->range, &b->range));
            RoboCheckEqInt((int)a->script, (int)b->script);
            RoboCheckEqInt((int)a->direction, (int)b->direction);
            RoboCheck(RoboTextResolvedFontEqual(&a->font, &b->font));
            RoboCheckEqInt(RoboTextResolvedFontCompare(&a->font, &b->font), 0);
        }
        RoboTextAnalysisDestroy(first);
        RoboTextAnalysisDestroy(second);
    }

    /* Fuzz: arbitrary bytes never crash the decoder, the resolver, the selector or the run walk. */
    {
        uint8_t junk[256];

        for (size_t index = 0u; index < sizeof(junk); index++) {
            junk[index] = (uint8_t)(index * 29u + 3u);
        }
        RoboCheckEqInt((int)RoboTextFuzzAnalysis(junk, sizeof(junk)), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextFuzzAnalysis(NULL, 0u), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextFuzzAnalysis(NULL, 4u), (int)RoboInvalidArgument);
    }

    RoboTextFontSelectorDestroy(selector);
    TextFixtureClose(&fixture);
}

void TestRegisterTextAnalysis(void)
{
    TestRegister("text.analysis.single_script", TestAnalysisSingleScript);
    TestRegister("text.analysis.mixed_scripts", TestAnalysisMixedScripts);
    TestRegister("text.analysis.combining_marks", TestAnalysisCombiningMarks);
    TestRegister("text.analysis.shape_request", TestAnalysisShapeRequest);
    TestRegister("text.analysis.empty_refusals_determinism", TestAnalysisEmptyRefusalsDeterminism);
}
