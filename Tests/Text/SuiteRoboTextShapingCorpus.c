/*
 * The whole-pipeline shaping conformance corpus — `text.shaping.corpus.*` (Phase 12.23).
 *
 * ## What this file is, and what it is not
 *
 * 12.18 proved each **layer** of a shaping run against a font this repository builds, one suite per layer
 * (`text.gsub.*`, `text.gpos.*`, `text.shaping.ligature.*`, `text.shaping.kerning.*`, `text.shaping.marks.*`,
 * the `text.arabic.*` and `text.indic.*` strategies, `text.vertical.*`, `text.selection.*` and
 * `text.editing.*`). This file is the next thing: **the same claim made end to end**, from UTF-8 to a
 * measurement, with the failing *stage* readable and with the gaps stated as gaps.
 *
 * It adds no shaping. Every number it asserts is read off a door that already existed:
 *
 *   - the decode and the per-scalar classification are 12.5's (`RoboTextUnicodeDecodeAll`,
 *     `RoboTextUnicodeScript`, `RoboTextDirectionClassOf`);
 *   - the script runs are 12.6's (`RoboTextScriptRunsResolve`), the script strategies are 12.19's
 *     (`RoboTextComplexPlanResolve`, `RoboTextComplexStrategyForScript`, `RoboTextIndicRulesForScript`);
 *   - the runs, their clusters and their advances are 12.14's, and the measurement is 12.15's
 *     (`RoboTextMeasureGlyphRunList` — a door **over the list**, so "the measurement agrees with the runs" is
 *     an equality between two doors rather than a second implementation of the sum);
 *   - selection is 12.21's map and editing is 12.22's editor.
 *
 * ## The corpus policy, restated because this file depends on it
 *
 * Expected data is a **synthetic font built in C** (`RoboTextConformanceFont.h`, `RoboTextComplexFont.h`) plus
 * the fixture's own constants — never a checked-in font blob, and never an external shaper's opinion. A
 * production face carries its own opinions (Inter carries `dlig` and no `liga`), and a corpus that asserted
 * those would be asserting Inter rather than this engine. The report this file writes
 * (`Build/Text-ConformanceCorpus.txt`) is the checked-in expected data in legible form: one line per sample,
 * and a second generation must match byte for byte.
 *
 * ## What is named and not built here
 *
 * The phase's §2 asks for a whole directory tree, one folder per script and one file per layer. This build is
 * C with one fixture per font and one suite per layer, so the **content** lives here and in the suites the
 * section names cross-reference; a folder per script would be a second layout convention for the same cases.
 * The honest gaps, named rather than implied: no independent reference shaper generates this data (the fixture
 * *is* the reference), no fixture font is checked in as a `.ttf` (they are built byte by byte), and the
 * malformed matrix covers out-of-range offsets, a truncated table and a broken `cmap`/`head`/`maxp` but not an
 * extension-lookup cycle or an impossible class definition — a variant that could hang the engine is a
 * fixture-layout question for the `GSUB`/`GPOS` builders, not a patch a corpus can write blind.
 */

#include "RoboTextComplexFont.h"
#include "RoboTextConformanceFont.h"
#include "RoboTextConformanceSupport.h"

#include "RoboMem/RoboMem.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/** The size every face in this file is opened at. */
#define CorpusSizePx 16.0f

/* The corpus samples, shared by the report, the determinism case and the measurement case. */
#define CorpusLatin "Hello, RoboticelaOS 2026!"
#define CorpusGreek "\xCE\xA9\xCF\x89\xCE\xBC\xCE\xB5\xCE\xB3\xCE\xB1"            /* Ωωμεγα — 6 scalars */
#define CorpusCyrillic "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"        /* Привет — 6 scalars */
#define CorpusMixed CorpusGreek " " CorpusCyrillic " " CorpusLatin                /* 39 scalars */
#define CorpusLatinExtended "caf\xC3\xA9 cr\xC3\xA8me"                            /* 10 scalars */
#define CorpusDecomposed "cafe\xCC\x81"                                           /* 5 scalars, 4 clusters */
#define CorpusFfi "office"
#define CorpusKernPair "AV"
#define CorpusArabic "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7"                   /* مرحبا — 5 scalars */
#define CorpusArabicMixed "Hello " CorpusArabic " world"                          /* 17 scalars */
#define CorpusIndic "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7"                        /* क्ष — 3 scalars */
#define CorpusIndicWord "\xE0\xA4\x95\xE0\xA4\xBF\xE0\xA4\xA4\xE0\xA4\xBE\xE0\xA4\xAC"  /* किताब */
#define CorpusCjkColumn "\xE4\xB8\x80\xE4\xB8\x81"                                /* 一丁 — 2 scalars */
#define CorpusKanaVoicing "\xE3\x81\x82\xE3\x82\x99"                              /* あ + U+3099 */
#define CorpusBengali "\xE0\xA6\x95\xE0\xA6\xBF"                                  /* ক + ি */

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Shape @p text with the defaults every case here uses. */
static RoboTextShapeResult *CorpusShape(const ConfFace *face, const char *text)
{
    return ConfShapeDefault(face, text);
}

/** Shape @p text in a vertical mode. */
static RoboTextShapeResult *CorpusShapeVertical(const ConfFace *face, const char *text)
{
    RoboTextShapeOptions options;

    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    options.writingMode = RoboTextWritingVerticalRightToLeft;
    return ConfShape(face, text, &options);
}

/**
 * The run list's own diagnostic block, in a heap buffer sized from the door's reported need.
 *
 * A dump here is **complete or absent**: asked with a buffer too small it answers `RoboNotSupported` and
 * reports the length a complete block needs, which is what keeps a corpus from truncating its own record
 * silently. A mixed-script sample's block is thousands of lines, so the size is learned rather than guessed.
 */
static char *CorpusDumpList(const RoboTextGlyphRunList *list, size_t *outLength)
{
    size_t capacity = 4096u;

    *outLength = 0u;
    for (unsigned attempt = 0u; attempt < 10u; attempt++) {
        size_t length = 0u;
        char *buffer = (char *)RoboCalloc(capacity, 1u);

        if (buffer == NULL) {
            return NULL;
        }
        if (RoboTextGlyphRunListWrite(list, buffer, capacity, &length) == RoboOk) {
            *outLength = length;
            return buffer;
        }
        /* Absent, not truncated: the buffer was too small, so grow it and ask again. */
        RoboFree(buffer);
        capacity *= 4u;
    }
    RoboCheckMsg(0, "no buffer up to 1 MiB held the run list's block");
    return NULL;
}

/** Shape a `RoboTextBuffer` — what an editor holds — through the same door. */
static RoboTextShapeResult *CorpusShapeBuffer(const ConfFace *face, const RoboTextBuffer *buffer)
{
    RoboTextShapeOptions options;
    RoboTextShapeResult *result = NULL;

    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    RoboCheckEqInt((int)RoboTextShape(RoboTextViewMake(RoboTextBufferBytes(buffer),
                                                       RoboTextBufferByteLength(buffer)),
                                      &face->font, &options, &result, NULL),
                   (int)RoboOk);
    RoboCheckMsg(result != NULL, "shaping produced no result");
    return result;
}

/** Clusters across every run. */
static size_t CorpusClusterSum(const RoboTextShapeResult *result)
{
    size_t total = 0u;

    for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
        total += RoboTextGlyphRunClusterCount(RoboTextShapeResultGlyphRun(result, run));
    }
    return total;
}

/**
 * Assert the runs **tile** the text: the first starts at 0, each starts where the last ended.
 *
 * The one invariant that makes "the pipeline accounted for every scalar" a fact rather than a hope — the
 * source text is never touched, and a font boundary, a bidi boundary or a fallback boundary is a boundary
 * *between* runs rather than a gap in the source.
 */
static void CorpusExpectTiles(const RoboTextShapeResult *result, RoboTextScalarIndex scalars, const char *label)
{
    RoboTextScalarIndex at = 0u;
    size_t runCount = RoboTextShapeResultGlyphRunCount(result);

    for (size_t run = 0u; run < runCount; run++) {
        RoboTextRange range = RoboTextGlyphRunSourceRange(RoboTextShapeResultGlyphRun(result, run));

        if (range.start != at) {
            fprintf(stdout, "    %s: run %u starts at %u, expected %u\n", label, (unsigned)run,
                    (unsigned)range.start, (unsigned)at);
            RoboCheckMsg(0, "a run does not start where the previous one ended");
            return;
        }
        at = range.end;
    }
    RoboCheckEqInt((int)at, (int)scalars);
}

/**
 * The measurement is a door **over the list**, so this is an equality between two doors rather than a second
 * sum: the engine's own totals against the list's own, its own validation, and its glyph count.
 */
static void CorpusExpectMeasurementAgrees(const RoboTextShapeResult *result,
                                          const ConfFace *face,
                                          const char *label)
{
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    RoboTextMeasureRequest request;
    RoboTextMeasurement measurement;
    RoboTextMeasureProblem problem = RoboTextMeasureProblemNone;

    RoboTextMeasureRequestInitFull(&request);
    RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(list, &face->font, &request, &measurement, NULL), (int)RoboOk);
    RoboCheckMsg(RoboTextMeasureFloatEqual(measurement.width, RoboTextGlyphRunListAdvance(list),
                                           RoboTextMeasureTolerancePx),
                 "the measurement's width is not the list's own advance");
    RoboCheckMsg(RoboTextMeasureFloatEqual(measurement.advanceY, RoboTextGlyphRunListAdvanceY(list),
                                           RoboTextMeasureTolerancePx),
                 "the measurement's advanceY is not the list's own advanceY");
    RoboCheckMsg(RoboTextMeasureFloatEqual(measurement.width, ConfTotalAdvance(result),
                                           RoboTextMeasureTolerancePx),
                 "the measurement's width is not the runs' own advances summed");
    RoboCheckEqInt((int)measurement.glyphCount, (int)ConfTotalGlyphs(result));
    RoboCheckEqInt((int)measurement.glyphRunCount, (int)RoboTextShapeResultGlyphRunCount(result));
    RoboCheckEqInt((int)measurement.missingGlyphs, (int)ConfTotalMissing(result));
    RoboCheckEqInt((int)RoboTextMeasureValidate(&measurement, list, &problem, NULL), (int)RoboOk);
    if (problem != RoboTextMeasureProblemNone) {
        fprintf(stdout, "    %s: the measurement is invalid: %s\n", label, RoboTextMeasureProblemName(problem));
    }
    RoboCheckEqInt((int)problem, (int)RoboTextMeasureProblemNone);
}

/** The list's own validation, which is the record every consumer above this layer reads. */
static void CorpusExpectListValid(const RoboTextShapeResult *result, const char *label)
{
    RoboTextGlyphRunListProblem problem = RoboTextGlyphRunListProblemNone;

    RoboCheckEqInt((int)RoboTextGlyphRunListValidate(RoboTextShapeResultGlyphRunList(result), &problem, NULL),
                   (int)RoboOk);
    if (problem != RoboTextGlyphRunListProblemNone) {
        fprintf(stdout, "    %s: the run list is invalid: %s\n", label, RoboTextGlyphRunListProblemName(problem));
    }
    RoboCheckEqInt((int)problem, (int)RoboTextGlyphRunListProblemNone);
}

/** Scalars of a NUL-terminated sample, through the module's own counter. */
static RoboTextScalarIndex CorpusScalars(const char *text)
{
    RoboTextScalarIndex scalars = 0u;

    RoboCheckEqInt((int)RoboTextViewScalarCount(ConfView(text), &scalars, NULL), (int)RoboOk);
    return scalars;
}

/* ------------------------------------------------------------------ */
/* §1 — the layer chain, and which stage a number came from             */
/* ------------------------------------------------------------------ */

static void TestCorpusStages(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;
    Uchar32 *codePoints = NULL;
    RoboTextScalarIndex scalars = 0u;
    RoboTextScript *scripts;
    RoboTextScriptRun *scriptRuns;
    size_t scriptRunCount = 0u;

    memset(&spec, 0, sizeof(spec));
    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, CorpusSizePx);

    /*
     * The chain, one door per layer. A wrong glyph count alongside a right scalar count is the shaper; a wrong
     * scalar count is the decoder — which is the whole reason the layers are asked separately rather than
     * "shape it and count".
     */
    RoboCheckEqInt((int)RoboTextUnicodeDecodeAll(ConfView(CorpusLatin), &codePoints, &scalars, NULL), (int)RoboOk);
    RoboCheckEqInt((int)scalars, (int)strlen(CorpusLatin));   /* ASCII: one scalar per byte */
    scripts = (RoboTextScript *)RoboCalloc((size_t)scalars + 1u, sizeof(*scripts));
    scriptRuns = (RoboTextScriptRun *)RoboCalloc((size_t)scalars + 1u, sizeof(*scriptRuns));
    RoboCheck(scripts != NULL && scriptRuns != NULL);
    RoboCheckEqInt((int)RoboTextScriptRunsResolve(codePoints, (size_t)scalars, RoboUScriptLatin, scripts, scriptRuns,
                                                  (size_t)scalars + 1u, &scriptRunCount, NULL),
                   (int)RoboOk);
    {
        RoboTextScalarIndex at = 0u;

        RoboCheckMsg(scriptRunCount >= 1u, "the script resolver produced no runs");
        for (size_t index = 0u; index < scriptRunCount; index++) {
            RoboCheckEqInt((int)scriptRuns[index].range.start, (int)at);
            at = scriptRuns[index].range.end;
        }
        RoboCheckEqInt((int)at, (int)scalars);
        RoboCheckEqInt((int)scriptRuns[0].range.start, 0);
        RoboCheckEqInt((int)scriptRuns[0].script, (int)RoboUScriptLatin);
    }
    /* The per-scalar classification: a letter is Latin and left to right, a space is a neutral. */
    RoboCheckEqInt((int)RoboTextUnicodeScript('H'), (int)RoboUScriptLatin);
    RoboCheckEqInt((int)RoboTextDirectionClassOf('H'), (int)RoboTextDirectionClassLeftToRight);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(' '), (int)RoboTextDirectionClassNeutral);
    /* A Latin script has no complex plan: the layer above the font would have been skipped, and says so. */
    {
        RoboTextComplexPlan plan;

        RoboTextComplexPlanInit(&plan);
        RoboCheckEqInt((int)RoboTextComplexPlanResolve(RoboUScriptLatin, NULL, &plan, NULL), (int)RoboOk);
        RoboCheckEqInt(RoboTextComplexPlanHasStages(&plan), 0);
        RoboCheckEqInt((int)RoboTextComplexStrategyForScript(RoboUScriptLatin),
                       (int)RoboTextComplexStrategyNone);
    }

    /* Now the shaped half, asserted against the decode. */
    result = CorpusShape(&face, CorpusLatin);
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(result), 1);
    CorpusExpectTiles(result, scalars, "corpus.stages");
    RoboCheckEqInt((int)ConfTotalGlyphs(result), (int)scalars);
    RoboCheckEqInt((int)CorpusClusterSum(result), (int)scalars);
    RoboCheckEqInt((int)ConfTotalMissing(result), 0);
    RoboCheckEqInt((int)RoboTextGlyphRunScript(RoboTextShapeResultGlyphRun(result, 0u)), (int)RoboUScriptLatin);
    RoboCheckEqInt((int)RoboTextGlyphRunDirection(RoboTextShapeResultGlyphRun(result, 0u)),
                   (int)RoboTextDirectionLtr);
    CorpusExpectMeasurementAgrees(result, &face, "corpus.stages");
    CorpusExpectListValid(result, "corpus.stages");
    RoboTextShapeResultDestroy(result);

    /* A complex script: three scalars, one conjunct glyph, one cluster, and a plan with stages. */
    ConfFaceClose(&face);
    memset(&spec, 0, sizeof(spec));
    CplxSpecComplex(&spec);
    ConfFaceOpen(&face, &spec, CorpusSizePx);
    result = CorpusShape(&face, CorpusIndic);
    CorpusExpectTiles(result, 3, "corpus.stages");
    RoboCheckEqInt((int)ConfTotalGlyphs(result), 1);
    RoboCheckEqInt((int)CorpusClusterSum(result), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunScript(RoboTextShapeResultGlyphRun(result, 0u)),
                   (int)RoboUScriptDevanagari);
    CorpusExpectMeasurementAgrees(result, &face, "corpus.stages");
    {
        RoboTextComplexPlan plan;

        RoboTextComplexPlanInit(&plan);
        RoboCheckEqInt((int)RoboTextComplexPlanResolve(RoboUScriptDevanagari, NULL, &plan, NULL), (int)RoboOk);
        RoboCheckMsg(RoboTextComplexPlanHasStages(&plan) != 0, "Devanagari resolved to no stages");
        RoboCheckEqInt((int)RoboTextComplexStrategyForScript(RoboUScriptDevanagari),
                       (int)RoboTextComplexStrategyIndic);
    }
    RoboTextShapeResultDestroy(result);

    /* And Arabic: a plan, an RTL run, and five scalars that are still five scalars. */
    result = CorpusShape(&face, CorpusArabic);
    CorpusExpectTiles(result, 5, "corpus.stages");
    RoboCheckEqInt((int)RoboTextGlyphRunDirection(RoboTextShapeResultGlyphRun(result, 0u)),
                   (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextGlyphRunScript(RoboTextShapeResultGlyphRun(result, 0u)), (int)RoboUScriptArabic);
    RoboCheckEqInt((int)RoboTextComplexStrategyForScript(RoboUScriptArabic),
                   (int)RoboTextComplexStrategyArabic);
    CorpusExpectMeasurementAgrees(result, &face, "corpus.stages");
    RoboTextShapeResultDestroy(result);

    RoboTextUnicodeScalarsFree(codePoints);
    RoboFree(scriptRuns);
    RoboFree(scripts);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §2 — the script families the corpus covers                           */
/* ------------------------------------------------------------------ */

static void TestCorpusScripts(void)
{
    ConfFontSpec spec;
    ConfFace face;

    memset(&spec, 0, sizeof(spec));
    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, CorpusSizePx);

    /* Greek: its own script, left to right, one run, one cluster per letter. */
    {
        RoboTextShapeResult *result = CorpusShape(&face, CorpusGreek);

        RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(result), 1);
        CorpusExpectTiles(result, CorpusScalars(CorpusGreek), "corpus.scripts");
        RoboCheckEqInt((int)ConfTotalGlyphs(result), 6);
        RoboCheckEqInt((int)CorpusClusterSum(result), 6);
        RoboCheckEqInt((int)ConfTotalMissing(result), 0);
        RoboCheckEqInt((int)RoboTextGlyphRunScript(RoboTextShapeResultGlyphRun(result, 0u)),
                       (int)RoboUScriptGreek);
        RoboCheckEqInt((int)RoboTextGlyphRunDirection(RoboTextShapeResultGlyphRun(result, 0u)),
                       (int)RoboTextDirectionLtr);
        RoboCheckEqInt((int)RoboTextUnicodeScript(0x03A9u), (int)RoboUScriptGreek);   /* U+03A9 */
        CorpusExpectMeasurementAgrees(result, &face, "corpus.scripts");
        RoboTextShapeResultDestroy(result);
    }

    /* Cyrillic the same way. */
    {
        RoboTextShapeResult *result = CorpusShape(&face, CorpusCyrillic);

        RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(result), 1);
        CorpusExpectTiles(result, CorpusScalars(CorpusCyrillic), "corpus.scripts");
        RoboCheckEqInt((int)ConfTotalGlyphs(result), 6);
        RoboCheckEqInt((int)ConfTotalMissing(result), 0);
        RoboCheckEqInt((int)RoboTextGlyphRunScript(RoboTextShapeResultGlyphRun(result, 0u)),
                       (int)RoboUScriptCyrillic);
        RoboCheckEqInt((int)RoboTextUnicodeScript(0x041Fu), (int)RoboUScriptCyrillic);   /* U+041F */
        CorpusExpectMeasurementAgrees(result, &face, "corpus.scripts");
        RoboTextShapeResultDestroy(result);
    }

    /*
     * Three scripts in one string. The run set cuts where the script changes — the spaces between them are
     * `Common`, which is an anchor's absence and therefore a run of its own — so the claims are the tiling and
     * the script of each *letter* run rather than a count this layer would be re-deriving.
     */
    {
        RoboTextShapeResult *result = CorpusShape(&face, CorpusMixed);
        size_t runCount = RoboTextShapeResultGlyphRunCount(result);
        int sawGreek = 0;
        int sawCyrillic = 0;
        int sawLatin = 0;

        CorpusExpectTiles(result, CorpusScalars(CorpusMixed), "corpus.scripts");
        RoboCheckMsg(runCount >= 3u, "three scripts produced fewer than three runs");
        for (size_t run = 0u; run < runCount; run++) {
            RoboTextScript script = RoboTextGlyphRunScript(RoboTextShapeResultGlyphRun(result, run));

            sawGreek |= script == RoboUScriptGreek;
            sawCyrillic |= script == RoboUScriptCyrillic;
            sawLatin |= script == RoboUScriptLatin;
        }
        RoboCheckEqInt(sawGreek, 1);
        RoboCheckEqInt(sawCyrillic, 1);
        RoboCheckEqInt(sawLatin, 1);
        /* Every script's letters are covered: a mixed string is not a fallback failure. */
        RoboCheckEqInt((int)ConfTotalMissing(result), 0);
        CorpusExpectMeasurementAgrees(result, &face, "corpus.scripts");
        CorpusExpectListValid(result, "corpus.scripts");
        RoboTextShapeResultDestroy(result);
    }

    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §3 — Latin and European: the corpus table                            */
/* ------------------------------------------------------------------ */

static void TestCorpusLatinExtended(void)
{
    ConfFontSpec spec;
    ConfFace face;
    static const struct {
        const char *label;
        const char *text;
        size_t glyphs;
        size_t clusters;
    } kCases[] = {
        {"corpus.latin.basic", "Hello RoboticelaOS", 18u, 18u},
        {"corpus.latin.punctuation", "Hello, world!", 13u, 13u},
        {"corpus.latin.quoted", "\"Hello\" (Robo) [Text] {Set}", 27u, 27u},
        {"corpus.latin.digits", "1234567890 2026 1.234 10,000", 28u, 28u},
        /* Whitespace is text: every space is its own cluster and its own advance. */
        {"corpus.latin.whitespace", "A  B C", 6u, 6u},
        /* Latin Extended: a precomposed scalar is one glyph and one cluster. */
        {"corpus.latin.extended", CorpusLatinExtended, 10u, 10u},
        /* …and the decomposed spelling is five glyphs in four clusters. */
        {"corpus.latin.decomposed", CorpusDecomposed, 5u, 4u}
    };

    memset(&spec, 0, sizeof(spec));
    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, CorpusSizePx);

    for (size_t index = 0u; index < sizeof(kCases) / sizeof(kCases[0]); index++) {
        RoboTextShapeResult *result = CorpusShape(&face, kCases[index].text);

        ConfExpectGlyphCount(result, kCases[index].glyphs, kCases[index].label, kCases[index].text);
        if (CorpusClusterSum(result) != kCases[index].clusters) {
            fprintf(stdout, "    %s: clusters %u, expected %u (glyphs %u, scalars %u)\n", kCases[index].label,
                    (unsigned)CorpusClusterSum(result), (unsigned)kCases[index].clusters,
                    (unsigned)ConfTotalGlyphs(result), (unsigned)CorpusScalars(kCases[index].text));
            RoboCheckMsg(0, "cluster count differs");
        }
        CorpusExpectTiles(result, CorpusScalars(kCases[index].text), kCases[index].label);
        if (ConfTotalMissing(result) != 0u) {
            fprintf(stdout, "    %s: %u scalars the face does not cover\n", kCases[index].label,
                    (unsigned)ConfTotalMissing(result));
            RoboCheckMsg(0, "a corpus sample is not fully covered by its fixture");
        }
        CorpusExpectMeasurementAgrees(result, &face, kCases[index].label);
        RoboTextShapeResultDestroy(result);
    }

    /*
     * The decomposed case stated as its own claim, because it is the one a width-only test gets wrong: the
     * mark advances nothing, so `cafe` + U+0301 is **exactly** as wide as `cafe` — while the **ink** box still
     * reaches the mark, which is why coverage, colour and ink are three fields and not one.
     */
    {
        RoboTextShapeResult *result = CorpusShape(&face, CorpusDecomposed);
        RoboTextShapeResult *plain = CorpusShape(&face, "cafe");
        RoboTextMeasureRequest request;
        RoboTextMeasurement measured;
        RoboTextMeasurement bare;

        RoboTextMeasureRequestInitFull(&request);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(RoboTextShapeResultGlyphRunList(result), &face.font,
                                                        &request, &measured, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(RoboTextShapeResultGlyphRunList(plain), &face.font,
                                                        &request, &bare, NULL),
                       (int)RoboOk);
        RoboCheckMsg(RoboTextMeasureFloatEqual(measured.width, bare.width, RoboTextMeasureTolerancePx),
                     "a combining mark changed the width");
        /*
         * And the mark is in the **ink**, where the ink is available: a mark that vanished would make the
         * selection geometry a lie. The conformance fixture's glyphs have no outline program, so the boxes
         * this graph reports are reported rather than asserted — what is asserted is the measurement's own
         * verdict, which is `hasInkBounds` non-zero exactly when every glyph answered with a box.
         */
        if (measured.hasInkBounds != 0 && bare.hasInkBounds != 0) {
            /* The rect's `y` is its bottom edge, so its top is `y + height`. */
            RoboCheckMsg(measured.inkBounds.y + measured.inkBounds.height >=
                             bare.inkBounds.y + bare.inkBounds.height,
                         "the mark did not reach above the base's own ink");
        } else {
            fprintf(stdout, "    corpus.latin.decomposed: ink bounds not available (marked %d, bare %d)\n",
                    measured.hasInkBounds, bare.hasInkBounds);
            RoboCheckEqInt(measured.hasInkBounds, bare.hasInkBounds);
        }
        RoboTextShapeResultDestroy(plain);
        RoboTextShapeResultDestroy(result);
    }

    /* A tab is a scalar with an advance, treated like every other whitespace. */
    {
        RoboTextShapeResult *result = CorpusShape(&face, "A\tB");

        RoboCheckEqInt((int)ConfTotalGlyphs(result), 3);
        RoboCheckEqInt((int)CorpusClusterSum(result), 3);
        RoboCheckMsg(ConfTotalAdvance(result) > 0.0f, "whitespace advanced nothing");
        RoboTextShapeResultDestroy(result);
    }

    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §4 — a feature changes glyphs *and* the measurement                  */
/* ------------------------------------------------------------------ */

static void TestCorpusLigatureAndKerning(void)
{
    ConfFontSpec spec;
    ConfFace face;

    memset(&spec, 0, sizeof(spec));
    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, CorpusSizePx);

    /*
     * The ligature itself, stated on the three scalars that make it: `ffi` is one glyph over `[0,3)`, one
     * cluster, and the **provenance** the whole chain carries — which is what selection and editing read. The
     * sample is `ffi` rather than `office` so the cluster claim is about the ligature and not about the words
     * around it.
     */
    {
        RoboTextShapeResult *on = CorpusShape(&face, "ffi");
        static const uint32_t kClusters[] = {0u, 3u, 1u};

        ConfExpectClusters(on, 0u, kClusters, 1u, "corpus.ligature", "ffi");
        RoboCheckEqInt((int)ConfTotalGlyphs(on), 1);
        RoboCheckMsg(RoboTextGlyphRunLigatures(RoboTextShapeResultGlyphRun(on, 0u)) >= 1u,
                     "the default policy produced no ligature");
        {
            const RoboTextGlyph *glyph = ConfGlyphAt(on, 0u);

            RoboCheck(glyph != NULL);
            if (glyph != NULL) {
                RoboCheckEqInt((int)glyph->source.start, 0);
                RoboCheckEqInt((int)glyph->source.end, 3);
                RoboCheckMsg(glyph->ligatureComponents >= 3u, "the ligature kept no component count");
            }
        }
        CorpusExpectMeasurementAgrees(on, &face, "corpus.ligature");
        RoboTextShapeResultDestroy(on);
    }

    /*
     * And a word: `office` under the default policy is four glyphs (the ligature and three letters) where the
     * same word with `liga` turned off is six. The corpus claim is the **whole pipeline** — the feature
     * reached the pen, so the text's measured width moved with the substitution.
     */
    {
        RoboTextShapeResult *on = CorpusShape(&face, CorpusFfi);
        RoboTextShapeOptions off;
        RoboTextShapeResult *without;
        RoboTextMeasureRequest request;
        RoboTextMeasurement measuredOn;
        RoboTextMeasurement measuredOff;

        RoboCheckEqInt((int)ConfTotalGlyphs(on), 4);
        RoboCheckMsg(ConfTotalLigatures(on) >= 1u, "`office` produced no ligature");

        RoboTextShapeOptionsInit(&off);
        off.baseDirection = RoboTextDirectionLtr;
        RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantNone, &off.ligatures);
        without = ConfShape(&face, CorpusFfi, &off);
        RoboCheckEqInt((int)ConfTotalGlyphs(without), 6);
        RoboCheckEqInt((int)ConfTotalLigatures(without), 0);

        /* And the measurement moved with it: "ligatures affect width" end to end. */
        RoboTextMeasureRequestInitFull(&request);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(RoboTextShapeResultGlyphRunList(on), &face.font,
                                                        &request, &measuredOn, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(RoboTextShapeResultGlyphRunList(without), &face.font,
                                                        &request, &measuredOff, NULL),
                       (int)RoboOk);
        RoboCheckMsg(measuredOn.width < measuredOff.width,
                     "the ligature did not narrow the text, so the feature never reached the measurement");
        RoboTextShapeResultDestroy(without);
        RoboTextShapeResultDestroy(on);
    }

    /*
     * Kerning is the mirror case: the pair's advance is the font's, and `AV` is narrower than its two letters
     * measured separately. The engine's `kern` reading is 12.13's; what this states is that the correction
     * survived to the measurement — and that the pair's glyph *identities* did not move.
     */
    {
        RoboTextShapeResult *pair = CorpusShape(&face, CorpusKernPair);
        RoboTextShapeResult *first = CorpusShape(&face, "A");
        RoboTextShapeResult *second = CorpusShape(&face, "V");
        RoboTextMeasureRequest request;
        RoboTextMeasurement measuredPair;
        RoboTextMeasurement measuredFirst;
        RoboTextMeasurement measuredSecond;

        RoboTextMeasureRequestInitFull(&request);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(RoboTextShapeResultGlyphRunList(pair), &face.font,
                                                        &request, &measuredPair, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(RoboTextShapeResultGlyphRunList(first), &face.font,
                                                        &request, &measuredFirst, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(RoboTextShapeResultGlyphRunList(second), &face.font,
                                                        &request, &measuredSecond, NULL),
                       (int)RoboOk);
        RoboCheckMsg(measuredPair.width < measuredFirst.width + measuredSecond.width,
                     "the kern pair is not narrower than its two letters");
        RoboCheckEqInt((int)ConfTotalGlyphs(pair), 2);
        RoboCheckMsg(ConfTotalPositioned(pair) >= 1u, "the pair carries no GPOS positioning at all");
        CorpusExpectTiles(pair, 2, "corpus.kerning");
        CorpusExpectMeasurementAgrees(pair, &face, "corpus.kerning");
        RoboTextShapeResultDestroy(second);
        RoboTextShapeResultDestroy(first);
        RoboTextShapeResultDestroy(pair);
    }

    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §5 — the scripts this build does not shape, named rather than passed */
/* ------------------------------------------------------------------ */

static void TestCorpusUnsupportedScripts(void)
{
    static const struct {
        RoboTextScript script;
        const char *name;
        RoboTextCodePoint virama;
    } kIndic[] = {
        {RoboUScriptBengali, "bengali", 0x09CDu},
        {RoboUScriptGujarati, "gujarati", 0x0ACDu},
        {RoboUScriptGurmukhi, "gurmukhi", 0x0A4Du},
        {RoboUScriptOriya, "oriya", 0x0B4Du},
        {RoboUScriptTamil, "tamil", 0x0BCDu},
        {RoboUScriptTelugu, "telugu", 0x0C4Du},
        {RoboUScriptKannada, "kannada", 0x0CCDu},
        {RoboUScriptMalayalam, "malayalam", 0x0D4Du},
        {RoboUScriptSinhala, "sinhala", 0x0DCAu}
    };

    /*
     * The rule 12.19 states and this case enforces: an Indic script whose positional rules this build does not
     * have still has a **row**, and the row is what says so — its own virama, and every reorder array empty.
     * That is a refusal the table states rather than Devanagari's rules applied to Kannada and a green test.
     */
    for (size_t index = 0u; index < sizeof(kIndic) / sizeof(kIndic[0]); index++) {
        const RoboTextIndicScriptRules *rules = RoboTextIndicRulesForScript(kIndic[index].script);

        if (rules == NULL) {
            fprintf(stdout, "    corpus.scripts.unsupported[%s]: no rules row at all\n", kIndic[index].name);
            RoboCheckMsg(0, "an Indic script has no rules row, so the table cannot state its own gap");
            continue;
        }
        RoboCheckEqInt((int)rules->virama, (int)kIndic[index].virama);
        RoboCheckEqInt((int)rules->preBaseCount, 0);
        RoboCheckEqInt((int)rules->belowBaseCount, 0);
        RoboCheckEqInt((int)rules->postBaseCount, 0);
        RoboCheckEqInt((int)rules->aboveBaseCount, 0);
        /* The script still resolves to the Indic *strategy*: the gap is a row's data, not a missing case. */
        RoboCheckEqInt((int)RoboTextComplexStrategyForScript(kIndic[index].script),
                       (int)RoboTextComplexStrategyIndic);
    }

    /* Devanagari, the script whose rules *are* here, is the control: the same door answers non-zero counts. */
    {
        const RoboTextIndicScriptRules *rules = RoboTextIndicRulesForScript(RoboUScriptDevanagari);

        RoboCheck(rules != NULL);
        if (rules != NULL) {
            RoboCheckEqInt((int)rules->virama, 0x094Du);
            RoboCheckMsg(rules->preBaseCount > 0u, "Devanagari lost its pre-base rules");
            RoboCheckMsg(rules->belowBaseCount > 0u, "Devanagari lost its below-base rules");
            RoboCheckMsg(rules->postBaseCount > 0u, "Devanagari lost its post-base rules");
            RoboCheckMsg(rules->aboveBaseCount > 0u, "Devanagari lost its above-base rules");
        }
    }

    /*
     * The Southeast Asian strategy resolves with **no stages**: the architecture has the row and nothing to
     * run, which is the honest answer rather than a fallback to the Indic rules.
     */
    {
        static const RoboTextScript kSea[] = {RoboUScriptThai, RoboUScriptKhmer, RoboUScriptMyanmar,
                                              RoboUScriptLao};

        for (size_t index = 0u; index < sizeof(kSea) / sizeof(kSea[0]); index++) {
            RoboTextComplexPlan plan;

            RoboTextComplexPlanInit(&plan);
            RoboCheckEqInt((int)RoboTextComplexPlanResolve(kSea[index], NULL, &plan, NULL), (int)RoboOk);
            RoboCheckEqInt(RoboTextComplexPlanHasStages(&plan), 0);
            RoboCheckEqInt((int)RoboTextComplexStrategyForScript(kSea[index]),
                           (int)RoboTextComplexStrategySoutheastAsian);
        }
        /*
         * Tibetan is the control for the strategy's *declaration*: it is a Southeast Asian script this build
         * has no row for at all, so it answers `None` — the SEA strategy is a value the table assigns to four
         * scripts, not a catch-all for a region.
         */
        {
            RoboTextComplexPlan plan;

            RoboTextComplexPlanInit(&plan);
            RoboCheckEqInt((int)RoboTextComplexPlanResolve(RoboUScriptTibetan, NULL, &plan, NULL), (int)RoboOk);
            RoboCheckEqInt(RoboTextComplexPlanHasStages(&plan), 0);
            RoboCheckEqInt((int)RoboTextComplexStrategyForScript(RoboUScriptTibetan),
                           (int)RoboTextComplexStrategyNone);
        }
    }

    /* A script with no row at all answers `None` rather than borrowing one. */
    {
        static const RoboTextScript kNone[] = {RoboUScriptHan, RoboUScriptCommon, RoboUScriptInherited,
                                               RoboUScriptHebrew};

        for (size_t index = 0u; index < sizeof(kNone) / sizeof(kNone[0]); index++) {
            RoboTextComplexPlan plan;

            RoboTextComplexPlanInit(&plan);
            RoboCheckEqInt((int)RoboTextComplexPlanResolve(kNone[index], NULL, &plan, NULL), (int)RoboOk);
            RoboCheckEqInt(RoboTextComplexPlanHasStages(&plan), 0);
            RoboCheckEqInt((int)RoboTextComplexStrategyForScript(kNone[index]),
                           (int)RoboTextComplexStrategyNone);
        }
    }

    /*
     * And a text in one of the unshaped scripts is still text: it decodes, it reports its script, and it shapes
     * with whatever the face covers — a counted miss, not a refusal.
     */
    {
        ConfFontSpec spec;
        ConfFace face;
        RoboTextShapeResult *result;

        memset(&spec, 0, sizeof(spec));
        ConfSpecEuropean(&spec);
        ConfFaceOpen(&face, &spec, CorpusSizePx);
        result = CorpusShape(&face, CorpusBengali);
        CorpusExpectTiles(result, CorpusScalars(CorpusBengali), "corpus.scripts.unsupported");
        RoboCheckEqInt((int)RoboTextGlyphRunScript(RoboTextShapeResultGlyphRun(result, 0u)),
                       (int)RoboUScriptBengali);
        RoboCheckMsg(ConfTotalMissing(result) > 0u, "a face with no Bengali coverage reported no miss");
        CorpusExpectMeasurementAgrees(result, &face, "corpus.scripts.unsupported");
        RoboTextShapeResultDestroy(result);
        ConfFaceClose(&face);
    }
}

/* ------------------------------------------------------------------ */
/* §6 — bidi end to end: logical text, levels, visual order              */
/* ------------------------------------------------------------------ */

static void TestCorpusBidi(void)
{
    ConfFontSpec spec;
    ConfFace face;
    static const char *kCases[] = {
        "corpus.bidi.latin_rtl",              /* Hello مرحبا world */
        "corpus.bidi.rtl_first",              /* مرحبا hello */
        "corpus.bidi.numbers"                 /* 123 مرحبا 456 */
    };
    static const char *kTexts[] = {CorpusArabicMixed, CorpusArabic " hello", "123 " CorpusArabic " 456"};

    memset(&spec, 0, sizeof(spec));
    CplxSpecComplex(&spec);
    ConfFaceOpen(&face, &spec, CorpusSizePx);

    for (size_t index = 0u; index < sizeof(kTexts) / sizeof(kTexts[0]); index++) {
        RoboTextShapeResult *result = CorpusShape(&face, kTexts[index]);
        const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
        size_t runCount = RoboTextShapeResultGlyphRunCount(result);
        int sawRtl = 0;
        int sawLtr = 0;

        /*
         * The source is never touched: the runs still name the logical ranges, in logical order, and together
         * they cover the text. Which *runs* the neutral spaces join is UAX #9's answer and belongs to 12.8's
         * suites — what this corpus states is that the answer reached the glyph-run layer intact.
         */
        CorpusExpectTiles(result, CorpusScalars(kTexts[index]), kCases[index]);
        RoboCheckMsg(runCount >= 2u, "mixed-direction text produced a single run");
        for (size_t run = 0u; run < runCount; run++) {
            const RoboTextGlyphRun *glyphRun = RoboTextShapeResultGlyphRun(result, run);
            RoboTextBidiLevel level = RoboTextGlyphRunBidiLevel(glyphRun);

            RoboCheckEqInt(RoboTextBidiLevelIsResolved(level), 1);
            if (RoboTextGlyphRunDirection(glyphRun) == RoboTextDirectionRtl) {
                sawRtl = 1;
                RoboCheckEqInt(RoboTextBidiLevelIsRightToLeft(level), 1);
            } else {
                sawLtr = 1;
                RoboCheckEqInt(RoboTextBidiLevelIsLeftToRight(level), 1);
            }
        }
        RoboCheckEqInt(sawRtl, 1);
        RoboCheckEqInt(sawLtr, 1);
        /* The visual order is published beside the logical one, and it is a permutation of it. */
        RoboCheckEqInt((int)RoboTextGlyphRunListVisualCount(list), (int)runCount);
        {
            unsigned seen = 0u;

            for (size_t visual = 0u; visual < RoboTextGlyphRunListVisualCount(list); visual++) {
                size_t logical = (size_t)-1;

                RoboCheckEqInt((int)RoboTextGlyphRunListVisualRun(list, visual, &logical), (int)RoboOk);
                RoboCheckMsg(logical < runCount, "a visual index names no run");
                if (logical < runCount) {
                    RoboCheckMsg((seen & (1u << logical)) == 0u, "a run appears twice in the visual order");
                    seen |= 1u << (unsigned)logical;
                }
                /* The inverse door agrees, which is what a caret walk needs. */
                {
                    size_t back = (size_t)-1;

                    RoboCheckEqInt((int)RoboTextGlyphRunListVisualIndex(list, logical, &back), (int)RoboOk);
                    RoboCheckEqInt((int)back, (int)visual);
                }
            }
            RoboCheckEqInt((int)seen, (int)((1u << runCount) - 1u));
        }
        CorpusExpectMeasurementAgrees(result, &face, kCases[index]);
        CorpusExpectListValid(result, kCases[index]);
        RoboTextShapeResultDestroy(result);
    }

    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §7 — fallback: a face boundary is not a text boundary                 */
/* ------------------------------------------------------------------ */

static void TestCorpusFallback(void)
{
    ConfFontSpec primarySpec;
    ConfFontSpec secondarySpec;
    ConfFace primary;
    ConfFace secondary;
    RoboTextFontCandidate candidate;
    RoboTextFontFallback chain;
    RoboTextFontSelector *selector = NULL;
    RoboTextView view = ConfView("Hi " CorpusGreek " " CorpusCyrillic);
    RoboTextScalarIndex scalars = CorpusScalars("Hi " CorpusGreek " " CorpusCyrillic);
    /* `Hi ` is three scalars; the Greek word is six; the Cyrillic word is the last six. */
    RoboTextRange greek = RoboTextRangeMake(3u, 9u);
    RoboTextRange cyrillic = RoboTextRangeMake(10u, 16u);

    memset(&primarySpec, 0, sizeof(primarySpec));
    memset(&secondarySpec, 0, sizeof(secondarySpec));
    ConfSpecFallbackPrimary(&primarySpec);
    ConfSpecFallbackSecondary(&secondarySpec);
    ConfFaceOpen(&primary, &primarySpec, CorpusSizePx);
    ConfFaceOpen(&secondary, &secondarySpec, CorpusSizePx);

    RoboCheckEqInt((int)scalars, 16);
    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&selector, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, &primary.font, NULL), (int)RoboOk);
    memset(&candidate, 0, sizeof(candidate));
    candidate.family = "Conformance Secondary";
    candidate.weight = RoboFontWeightNormal;
    candidate.style = RoboFontStyleNormal;
    candidate.stretch = RoboFontStretchNormal;
    candidate.face = secondary.face;
    memset(&chain, 0, sizeof(chain));
    chain.entries = &candidate;
    chain.count = 1u;
    RoboCheckEqInt((int)RoboTextFontSelectorSetFallback(selector, &chain, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextFontSelectorIsUsable(selector), 1);

    /*
     * The selector decides for one code point first: an ASCII letter is the primary's, and the answer **names
     * which candidate** rather than only handing back a face.
     */
    {
        RoboTextResolvedFont resolved;

        memset(&resolved, 0, sizeof(resolved));
        RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, (RoboTextCodePoint)'H', &resolved, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)resolved.source, (int)RoboTextFontSourcePrimary);
        RoboCheckMsg(resolved.face == primary.face, "the primary did not answer an ASCII letter");
        RoboCheckEqInt(resolved.complete, 1);

        memset(&resolved, 0, sizeof(resolved));
        RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 0x03A9u, &resolved, NULL), (int)RoboOk);
        RoboCheckEqInt((int)resolved.source, (int)RoboTextFontSourceFallback);
        RoboCheckMsg(resolved.face == secondary.face, "the chain did not answer a Greek letter");
        RoboCheckEqInt((int)resolved.candidateIndex, 1);
        RoboCheckEqInt(resolved.complete, 1);
    }

    /* A whole coherent range is answered for the range, and the Greek word is complete in the chain's face. */
    {
        RoboTextResolvedFont resolved;

        memset(&resolved, 0, sizeof(resolved));
        RoboCheckEqInt((int)RoboTextFontSelectorSelectRangeView(selector, view, greek, &resolved, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)resolved.source, (int)RoboTextFontSourceFallback);
        RoboCheckMsg(resolved.face == secondary.face, "the chain answered a Greek range with another face");
        RoboCheckEqInt(resolved.complete, 1);   /* every scalar of the range, not just its base */

        memset(&resolved, 0, sizeof(resolved));
        RoboCheckEqInt((int)RoboTextFontSelectorSelectRangeView(selector, view, cyrillic, &resolved, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)resolved.source, (int)RoboTextFontSourceFallback);
        RoboCheckEqInt(resolved.complete, 1);
    }

    /*
     * And the whole string is the one case with no complete answer: the primary covers the base but not all of
     * it, which is *reported* (`complete` 0) rather than repaired by splitting the range — the split is the
     * analysis walk's job, and it happens per script run one layer up.
     */
    {
        RoboTextResolvedFont resolved;

        memset(&resolved, 0, sizeof(resolved));
        RoboCheckEqInt((int)RoboTextFontSelectorSelectRangeView(selector, view,
                                                               RoboTextRangeFull(scalars), &resolved, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)resolved.source, (int)RoboTextFontSourcePrimary);
        RoboCheckEqInt(resolved.complete, 0);
        RoboCheckMsg(resolved.face == primary.face, "an incomplete answer was not attributed to the primary");
    }

    /*
     * Then shape with the face the selector named, and assert the two things a fallback must not break: the
     * run keeps its own script, and the face really covers the text (`missing` 0, and a measurement that ties
     * out against the face's own metrics).
     */
    {
        const char *greekWord = CorpusGreek;
        RoboTextShapeResult *result = CorpusShape(&secondary, greekWord);

        RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(result), 1);
        RoboCheckEqInt((int)RoboTextGlyphRunScript(RoboTextShapeResultGlyphRun(result, 0u)),
                       (int)RoboUScriptGreek);
        RoboCheckEqInt((int)ConfTotalMissing(result), 0);
        CorpusExpectTiles(result, CorpusScalars(greekWord), "corpus.fallback");
        CorpusExpectMeasurementAgrees(result, &secondary, "corpus.fallback");
        RoboTextShapeResultDestroy(result);
    }

    RoboTextFontSelectorDestroy(selector);
    ConfFaceClose(&secondary);
    ConfFaceClose(&primary);
}

/* ------------------------------------------------------------------ */
/* §8 — vertical through the whole pipeline                             */
/* ------------------------------------------------------------------ */

static void TestCorpusVertical(void)
{
    ConfFontSpec spec;
    ConfVerticalGlyphIds ids;
    ConfFace face;
    RoboTextMeasureRequest request;
    RoboTextMeasurement measured;

    memset(&spec, 0, sizeof(spec));
    memset(&ids, 0, sizeof(ids));
    ConfSpecVertical(&spec, &ids);
    ConfFaceOpen(&face, &spec, CorpusSizePx);

    /* A column of two ideographs: the pen moves on y, the width is zero, and the run says so. */
    {
        RoboTextShapeResult *result = CorpusShapeVertical(&face, CorpusCjkColumn);
        const RoboTextGlyphRun *run = RoboTextShapeResultGlyphRun(result, 0u);

        RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(result), 1);
        RoboCheckEqInt((int)RoboTextGlyphRunWritingMode(run), (int)RoboTextWritingVerticalRightToLeft);
        RoboCheckEqInt(RoboTextGlyphRunIsVertical(run), 1);
        RoboCheckEqInt((int)RoboTextGlyphRunOrientation(run), (int)RoboTextGlyphOrientationUpright);
        /* Every glyph places on y and none on x: a column is not a rotated line. */
        for (size_t index = 0u; index < RoboTextGlyphRunCount(run); index++) {
            const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)index);

            RoboCheckEqInt(glyph->advanceX == 0.0f, 1);
            RoboCheckMsg(glyph->advanceY != 0.0f, "a vertical glyph advanced nothing on y");
        }
        CorpusExpectTiles(result, CorpusScalars(CorpusCjkColumn), "corpus.vertical");
        CorpusExpectMeasurementAgrees(result, &face, "corpus.vertical");
        RoboTextMeasureRequestInitFull(&request);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(RoboTextShapeResultGlyphRunList(result), &face.font,
                                                        &request, &measured, NULL),
                       (int)RoboOk);
        /* A vertical measurement is not a rotated rectangle: the width is zero and the advance is on y. */
        RoboCheckEqInt(measured.width == 0.0f, 1);
        RoboCheckMsg(measured.advanceY > 0.0f, "a column measured no height");
        RoboCheckMsg(RoboTextMeasureFloatEqual(measured.advanceY, RoboTextGlyphRunAdvanceY(run),
                                               RoboTextMeasureTolerancePx),
                     "the measurement's advanceY is not the run's");
        RoboTextShapeResultDestroy(result);
    }

    /* A Latin letter in the same column is sideways — the orientation, not the writing mode, decides. */
    {
        RoboTextShapeResult *result = CorpusShapeVertical(&face, "A");
        const RoboTextGlyphRun *run = RoboTextShapeResultGlyphRun(result, 0u);

        RoboCheckEqInt(RoboTextGlyphRunIsVertical(run), 1);
        RoboCheckEqInt(RoboTextGlyphOrientationIsUpright(RoboTextGlyphRunOrientation(run)), 0);
        RoboTextShapeResultDestroy(result);
    }

    /* A base and its mark are one cluster, and the column is as long as its base alone. */
    {
        RoboTextShapeResult *result = CorpusShapeVertical(&face, CorpusKanaVoicing);
        RoboTextShapeResult *base = CorpusShapeVertical(&face, "\xE3\x81\x82");
        RoboTextMeasureRequest bare;
        RoboTextMeasurement measuredMarked;
        RoboTextMeasurement measuredBare;

        RoboCheckEqInt((int)ConfTotalGlyphs(result), 2);
        RoboCheckEqInt((int)CorpusClusterSum(result), 1);
        RoboTextMeasureRequestInitFull(&bare);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(RoboTextShapeResultGlyphRunList(result), &face.font,
                                                        &bare, &measuredMarked, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(RoboTextShapeResultGlyphRunList(base), &face.font,
                                                        &bare, &measuredBare, NULL),
                       (int)RoboOk);
        /* The fixture gives the mark zero on **both** axes, so the column did not grow. */
        RoboCheckMsg(RoboTextMeasureFloatEqual(measuredMarked.advanceY, measuredBare.advanceY,
                                               RoboTextMeasureTolerancePx),
                     "a vertical mark lengthened the column");
        ConfExpectGlyphCount(base, 1u, "corpus.vertical", "the base alone");
        RoboTextShapeResultDestroy(base);
        RoboTextShapeResultDestroy(result);
    }

    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §9 — measurement, selection and editing, all over one corpus         */
/* ------------------------------------------------------------------ */

/** The selection map's queries over one `office`: the ligature, the caret and the round trip. */
static void CorpusSelectionOverLigature(const RoboTextShapeResult *result)
{
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    RoboTextSelectionPlacement placements[4];
    RoboTextSelectionMap *map = NULL;
    RoboTextCaretGeometry caret;
    RoboTextSelectionHit hit;
    RoboTextSelectionGeometry geometry;
    RoboTextSelection selection;
    size_t runCount = RoboTextGlyphRunListCount(list);
    size_t clusterCount = 0u;
    float pen = 100.0f;

    RoboCheckMsg(runCount <= 4u, "the placement array is too small for the sample");
    for (size_t run = 0u; run < runCount; run++) {
        const RoboTextGlyphRun *glyphRun = RoboTextGlyphRunListRun(list, run);

        memset(&placements[run], 0, sizeof(placements[run]));
        RoboCheckEqInt((int)RoboTextSelectionPlacementForRun(glyphRun, run, 0u, pen, 200.0f, &placements[run],
                                                             NULL),
                       (int)RoboOk);
        pen += RoboTextGlyphRunAdvance(glyphRun);
        clusterCount += RoboTextGlyphRunClusterCount(glyphRun);
    }
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, placements, runCount, NULL, &map, NULL), (int)RoboOk);
    if (map == NULL) {
        RoboCheckMsg(0, "the selection map was not built");
        return;
    }
    RoboCheckEqInt((int)RoboTextSelectionMapSourceRange(map).start, 0);
    RoboCheckEqInt((int)RoboTextSelectionMapSourceRange(map).end, 3);
    /* `ffi` under `liga`: the whole ligature is one cluster, which is the sample's point. */
    RoboCheckMsg(clusterCount == 1u, "the ligature sample is not one cluster");

    /* The caret at the cluster's start is the run's origin. */
    memset(&caret, 0, sizeof(caret));
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, RoboTextPositionMake(0u), RoboTextAffinityDownstream,
                                                  &caret, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(caret.valid, 1);
    RoboCheckMsg(RoboTextMeasureFloatEqual(caret.start.x, 100.0f, RoboTextMeasureTolerancePx),
                 "the caret at the cluster's start is not at the run's origin");

    /*
     * A point in the middle of the ligature snaps to the cluster and **says it did** — the one fact that tells
     * a caller "you cannot put a caret inside this glyph". Its y comes from the caret's own band, so the point
     * is inside the line by construction rather than by a guessed offset.
     */
    memset(&hit, 0, sizeof(hit));
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(
                       map, RoboTextSelectionPointMake(
                                caret.start.x + RoboTextGlyphRunAdvance(RoboTextGlyphRunListRun(list, 0u)) * 0.5f,
                                caret.start.y),
                       &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(hit.snapped, 1);
    RoboCheckMsg(hit.position.offset == 0u || hit.position.offset == 3u,
                 "a hit inside the ligature resolved inside it rather than to its cluster");

    /* The round trip: a caret's own point hits back to the position it was asked about. */
    memset(&hit, 0, sizeof(hit));
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map,
                                                    RoboTextSelectionPointMake(caret.start.x, caret.start.y),
                                                    &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)hit.position.offset, 0);
    RoboCheckEqInt((int)hit.runIndex, 0);

    /* Whole-text selection: one rectangle, whose inline extent is the cluster's own advance. */
    memset(&geometry, 0, sizeof(geometry));
    selection = RoboTextSelectionMake(RoboTextPositionMake(0u), RoboTextPositionMake(3u));
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, selection, &geometry, NULL), (int)RoboOk);
    RoboCheckEqInt((int)geometry.count, 1);
    RoboCheckEqInt(geometry.isEmpty, 0);
    RoboCheckMsg(RoboTextMeasureFloatEqual(geometry.rects[0].width,
                                           RoboTextGlyphRunAdvance(RoboTextGlyphRunListRun(list, 0u)), 0.05f),
                 "the selection's inline extent is not the cluster's own advance");

    /* And the map's own block names the clusters it maps through, which is what a debugger reads. */
    {
        char block[2048];
        size_t need = 0u;

        RoboCheckEqInt((int)RoboTextSelectionMapWrite(map, block, sizeof(block), &need), (int)RoboOk);
        RoboCheckMsg(strstr(block, "0..3") != NULL, "the map's block names no cluster range");
    }
    RoboTextSelectionMapDestroy(map);
}

static void TestCorpusMeasurementSelectionEditing(void)
{
    ConfFontSpec latinSpec;
    ConfFontSpec complexSpec;
    ConfFace latin;
    ConfFace complex;
    RoboTextShapeResult *samples[6];
    const ConfFace *faces[6];
    const char *labels[6];
    size_t count = 0u;
    size_t index;

    memset(&latinSpec, 0, sizeof(latinSpec));
    memset(&complexSpec, 0, sizeof(complexSpec));
    ConfSpecEuropean(&latinSpec);
    CplxSpecComplex(&complexSpec);
    ConfFaceOpen(&latin, &latinSpec, CorpusSizePx);
    ConfFaceOpen(&complex, &complexSpec, CorpusSizePx);

    /* One sample per category §21 names, each with the face that shapes it. */
    samples[count] = CorpusShape(&latin, CorpusLatin);
    faces[count] = &latin;
    labels[count++] = "corpus.measurement.latin";
    samples[count] = CorpusShape(&latin, "ffi");
    faces[count] = &latin;
    labels[count++] = "corpus.measurement.ligature";
    samples[count] = CorpusShape(&latin, CorpusKernPair);
    faces[count] = &latin;
    labels[count++] = "corpus.measurement.kerning";
    samples[count] = CorpusShape(&latin, CorpusDecomposed);
    faces[count] = &latin;
    labels[count++] = "corpus.measurement.marks";
    samples[count] = CorpusShape(&complex, CorpusArabic);
    faces[count] = &complex;
    labels[count++] = "corpus.measurement.arabic";
    samples[count] = CorpusShape(&complex, CorpusIndic);
    faces[count] = &complex;
    labels[count++] = "corpus.measurement.indic";

    for (index = 0u; index < count; index++) {
        CorpusExpectMeasurementAgrees(samples[index], faces[index], labels[index]);
        CorpusExpectTiles(samples[index],
                          RoboTextGlyphRunListSourceRange(RoboTextShapeResultGlyphRunList(samples[index])).end,
                          labels[index]);
        CorpusExpectListValid(samples[index], labels[index]);
    }

    /* The selection half, over the ligature sample — the case where logical and visual disagree. */
    CorpusSelectionOverLigature(samples[1]);

    /*
     * The editing half (§20): after a mutation the editor's own bytes shape **exactly** like the same text
     * built directly. This is a regression, not an editor test — it says an edit cannot leave the pipeline with
     * a different answer than a fresh layout of the text that edit produced.
     */
    {
        RoboTextEditor *editor = NULL;
        RoboTextEditResult edit;
        static const struct {
            const char *label;
            const char *start;
            const char *insert;
            RoboTextScalarIndex at;
            const char *expected;
            int complex;
        } kEdits[] = {
            {"corpus.editing.ligature", "ff", "i", 2u, "ffi", 0},
            {"corpus.editing.arabic", "\xD8\xA8", "\xD8\xA8", 1u, "\xD8\xA8\xD8\xA8", 1},
            {"corpus.editing.indic", "\xE0\xA4\x95", "\xE0\xA5\x8D\xE0\xA4\xB7", 1u, CorpusIndic, 1}
        };

        RoboTextEditResultInit(&edit);
        for (index = 0u; index < sizeof(kEdits) / sizeof(kEdits[0]); index++) {
            const ConfFace *face = kEdits[index].complex ? &complex : &latin;
            const RoboTextBuffer *buffer;
            RoboTextShapeResult *after;
            RoboTextShapeResult *direct;

            RoboCheckEqInt((int)RoboTextEditorCreateFrom(ConfView(kEdits[index].start), &editor, NULL),
                           (int)RoboOk);
            RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(kEdits[index].at),
                                                       RoboTextAffinityDownstream),
                           (int)RoboOk);
            RoboCheckEqInt((int)RoboTextEditorInsert(editor, kEdits[index].insert,
                                                     strlen(kEdits[index].insert), &edit, NULL),
                           (int)RoboOk);
            buffer = RoboTextEditorText(editor);
            /* The text is the expected one, so the equality below is about the pipeline and not about luck. */
            RoboCheckEqInt((int)RoboTextBufferByteLength(buffer), (int)strlen(kEdits[index].expected));
            RoboCheckEqInt(memcmp(RoboTextBufferBytes(buffer), kEdits[index].expected,
                                  strlen(kEdits[index].expected)),
                           0);
            after = CorpusShapeBuffer(face, buffer);
            direct = CorpusShape(face, kEdits[index].expected);
            if (!ConfResultsEqual(after, direct)) {
                fprintf(stdout, "    %s: the edit's text does not shape like the text it produced\n",
                        kEdits[index].label);
                RoboCheckMsg(0, "an edit changed the shaping of text that did not change");
            }
            /* And the invalidation asks for exactly the pass the equality just performed. */
            RoboCheckEqInt(edit.invalidation.changed, 1);
            RoboCheckEqInt(edit.invalidation.requiresReshape, 1);
            RoboCheckMsg(RoboTextRangeContains(edit.invalidation.shapingRange, kEdits[index].at),
                         "the invalidation does not cover the edit");
            RoboTextShapeResultDestroy(direct);
            RoboTextShapeResultDestroy(after);
            RoboTextEditorDestroy(editor);
        }

        /* A deletion is the mirror claim: `office` minus a letter reshapes like the text that is left. */
        RoboCheckEqInt((int)RoboTextEditorCreateFrom(ConfView("office"), &editor, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(2u), RoboTextAffinityDownstream),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &edit, NULL), (int)RoboOk);
        {
            RoboTextShapeResult *after = CorpusShapeBuffer(&latin, RoboTextEditorText(editor));
            RoboTextShapeResult *direct = CorpusShape(&latin, "ofice");

            if (!ConfResultsEqual(after, direct)) {
                fprintf(stdout, "    corpus.editing.delete: the deletion changed other letters' shaping\n");
                RoboCheckMsg(0, "a delete changed the shaping of text outside the edit");
            }
            RoboTextShapeResultDestroy(direct);
            RoboTextShapeResultDestroy(after);
        }
        RoboTextEditorDestroy(editor);
        RoboTextEditResultDestroy(&edit);
    }

    for (index = 0u; index < count; index++) {
        RoboTextShapeResultDestroy(samples[index]);
    }
    ConfFaceClose(&complex);
    ConfFaceClose(&latin);
}

/* ------------------------------------------------------------------ */
/* §10 — malformed fonts: rejected or ignored, never fatal, always twice */
/* ------------------------------------------------------------------ */

/** Write a big-endian 16-bit value into a byte array, refusing to run off the end. */
static void CorpusPatch16(uint8_t *bytes, size_t length, size_t at, uint16_t value, const char *what)
{
    if (at + 1u >= length) {
        fprintf(stdout, "    corpus.malformed[%s]: the patch ran past the table\n", what);
        RoboCheckMsg(0, "a malformed-font patch ran past the end of a table");
        return;
    }
    bytes[at] = (uint8_t)(value >> 8);
    bytes[at + 1u] = (uint8_t)(value & 0xFFu);
}

/** Set a table's header offset field (4, 6 or 8) past the end of the table. */
static void CorpusBreakHeaderOffset(ConfFace *face, uint32_t tag, size_t field, const char *what)
{
    uint32_t length = 0u;
    uint8_t *table = ConfFaceTableBytes(face, tag, &length);

    RoboCheckMsg(table != NULL, "the fixture has no such table");
    if (table != NULL) {
        CorpusPatch16(table, length, field, 0xFFFFu, what);
    }
}

/** Cut a table's declared length down to its version, which is what a cut-off download produces. */
static void CorpusTruncateTable(ConfFace *face, uint32_t tag, const char *what)
{
    uint16_t count = (uint16_t)(((uint16_t)face->bytes[4] << 8) | face->bytes[5]);

    for (uint16_t index = 0u; index < count; index++) {
        uint8_t *record = face->bytes + 12u + (size_t)index * 16u;
        uint32_t recordTag = ((uint32_t)record[0] << 24) | ((uint32_t)record[1] << 16) |
                             ((uint32_t)record[2] << 8) | (uint32_t)record[3];

        if (recordTag == tag) {
            record[12] = 0u;
            record[13] = 0u;
            record[14] = 0u;
            record[15] = 4u;
            return;
        }
    }
    fprintf(stdout, "    corpus.malformed[%s]: the fixture has no such table\n", what);
    RoboCheckMsg(0, "a malformed-font variant named a table the fixture does not have");
}

/** One malformed variant: a label and one stated corruption of an otherwise good font. */
typedef struct CorpusMalformedCase {
    const char *label;
    void (*patch)(ConfFace *face);
} CorpusMalformedCase;

/*
 * The variants, each **one** corruption whose expected behaviour is "reject safely or ignore the malformed
 * optional data": an offset that leaves its table, a table cut short, a `cmap` with no subtable, a face whose
 * `head` cannot scale and one whose `maxp` claims a glyph count the rest of the face disagrees with.
 */
static void CorpusBreakGsubScriptList(ConfFace *face)
{
    CorpusBreakHeaderOffset(face, 0x47535542u, 4u, "gsub-script-list");
}
static void CorpusBreakGsubFeatureList(ConfFace *face)
{
    CorpusBreakHeaderOffset(face, 0x47535542u, 6u, "gsub-feature-list");
}
static void CorpusBreakGsubLookupList(ConfFace *face)
{
    CorpusBreakHeaderOffset(face, 0x47535542u, 8u, "gsub-lookup-list");
}
static void CorpusBreakGposScriptList(ConfFace *face)
{
    CorpusBreakHeaderOffset(face, 0x47504F53u, 4u, "gpos-script-list");
}
static void CorpusBreakGposLookupList(ConfFace *face)
{
    CorpusBreakHeaderOffset(face, 0x47504F53u, 8u, "gpos-lookup-list");
}
static void CorpusBreakGdefMarkAttach(ConfFace *face)
{
    CorpusBreakHeaderOffset(face, 0x47444546u, 10u, "gdef-mark-attach");
}
static void CorpusTruncateGsub(ConfFace *face)
{
    CorpusTruncateTable(face, 0x47535542u, "gsub-truncated");
}
static void CorpusTruncateGpos(ConfFace *face)
{
    CorpusTruncateTable(face, 0x47504F53u, "gpos-truncated");
}
static void CorpusBreakCmap(ConfFace *face)
{
    uint32_t length = 0u;
    uint8_t *table = ConfFaceTableBytes(face, 0x636D6170u, &length);

    RoboCheck(table != NULL);
    if (table != NULL) {
        CorpusPatch16(table, length, 2u, 0u, "cmap-subtables");   /* numTables = 0 */
    }
}
static void CorpusBreakHeadUnits(ConfFace *face)
{
    uint32_t length = 0u;
    uint8_t *table = ConfFaceTableBytes(face, 0x68656164u, &length);

    RoboCheck(table != NULL && length >= 20u);
    if (table != NULL) {
        CorpusPatch16(table, length, 18u, 0u, "head-units-per-em");   /* unitsPerEm = 0 */
    }
}
static void CorpusBreakMaxp(ConfFace *face)
{
    uint32_t length = 0u;
    uint8_t *table = ConfFaceTableBytes(face, 0x6D617870u, &length);

    RoboCheck(table != NULL && length >= 6u);
    if (table != NULL) {
        CorpusPatch16(table, length, 4u, 1u, "maxp-num-glyphs");   /* numGlyphs = 1 */
    }
}

/**
 * Open @p face's bytes **without** asserting a usable `cmap` or metrics.
 *
 * `ConfFaceAdopt` asserts both, and two of the variants are exactly a face that has neither — so the door this
 * case needs is the tolerant one. The contract under test is "it answers": a refusal is an answer, a crash is
 * not.
 */
static void CorpusFaceOpenTolerant(ConfFace *face, float sizePx)
{
    RoboTextFontStyle style;

    face->source = NULL;
    face->face = NULL;
    if (FontSourceCreateMemoryCopy(face->bytes, face->length, &face->source) != RoboOk) {
        return;
    }
    if (FontFaceCreate(face->source, 0u, &face->face) != RoboOk) {
        FontSourceRelease(face->source);
        face->source = NULL;
        return;
    }
    /* Both are optional for the question being asked: their status is the answer, not a precondition. */
    (void)FontFaceParseCmap(face->face);
    (void)FontFaceParseMetrics(face->face);
    RoboTextFontStyleInit(&style);
    style.sizePx = sizePx;
    (void)RoboTextFontContextSet(&face->font, face->face, &style);
}

/**
 * Shape one malformed face twice and assert the two answers agree. Returns the first status so the matrix can
 * record which way each variant went: a refusal and a skip are both acceptable, an unstable answer is not.
 */
static Result CorpusMalformedOnce(const CorpusMalformedCase *broken)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeOptions options;
    RoboTextShapeResult *first = NULL;
    RoboTextShapeResult *second = NULL;
    Result firstStatus;
    Result secondStatus;

    memset(&spec, 0, sizeof(spec));
    ConfSpecEuropean(&spec);
    memset(&face, 0, sizeof(face));
    ConfFaceBuildBytes(&face, &spec);
    broken->patch(&face);
    CorpusFaceOpenTolerant(&face, CorpusSizePx);

    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    firstStatus = RoboTextShape(ConfView("HffiAV " CorpusArabic), &face.font, &options, &first, NULL);
    secondStatus = RoboTextShape(ConfView("HffiAV " CorpusArabic), &face.font, &options, &second, NULL);

    /* Deterministic under malformed input: the same request answers the same way twice. */
    if (secondStatus != firstStatus) {
        fprintf(stdout, "    corpus.malformed[%s]: %d then %d\n", broken->label, (int)firstStatus,
                (int)secondStatus);
        RoboCheckMsg(0, "a malformed face answered differently on the second attempt");
    }
    if (firstStatus == RoboOk && secondStatus == RoboOk) {
        if (!ConfResultsEqual(first, second)) {
            fprintf(stdout, "    corpus.malformed[%s]: the two results differ\n", broken->label);
            RoboCheckMsg(0, "a malformed face shaped two ways");
        }
        /* Whatever it produced must be internally consistent rather than merely non-empty. */
        CorpusExpectListValid(first, broken->label);
        /* And a run the face produced still accounts for the text, whatever it could not parse. */
        CorpusExpectTiles(first, CorpusScalars("HffiAV " CorpusArabic), broken->label);
    }
    RoboTextShapeResultDestroy(second);
    RoboTextShapeResultDestroy(first);
    ConfFaceClose(&face);
    return firstStatus;
}

static void TestCorpusMalformed(void)
{
    static const CorpusMalformedCase kCases[] = {
        {"gsub-script-list", CorpusBreakGsubScriptList},
        {"gsub-feature-list", CorpusBreakGsubFeatureList},
        {"gsub-lookup-list", CorpusBreakGsubLookupList},
        {"gsub-truncated", CorpusTruncateGsub},
        {"gpos-script-list", CorpusBreakGposScriptList},
        {"gpos-lookup-list", CorpusBreakGposLookupList},
        {"gpos-truncated", CorpusTruncateGpos},
        {"gdef-mark-attach", CorpusBreakGdefMarkAttach},
        {"cmap-subtables", CorpusBreakCmap},
        {"head-units-per-em", CorpusBreakHeadUnits},
        {"maxp-num-glyphs", CorpusBreakMaxp}
    };

    /*
     * The matrix §28 asks for. Every call is a `Result`, the promise is "reject safely or ignore malformed
     * optional data", and the two facts asserted are the two a crash or a nondeterministic answer would break:
     * the calls all return, and the second call agrees with the first. A hang cannot be asserted here — it
     * would be a hung suite rather than a red one — which is why the cycle variant is named as not built.
     */
    for (size_t index = 0u; index < sizeof(kCases) / sizeof(kCases[0]); index++) {
        Result status = CorpusMalformedOnce(&kCases[index]);

        /* One line per variant, so the record says which way each went rather than only that none crashed. */
        fprintf(stdout, "    corpus.malformed[%-18s]: status %d\n", kCases[index].label, (int)status);
        RoboCheckMsg(status >= RoboResultFirst && status <= RoboResultLast,
                     "a malformed face answered with a value outside the Result vocabulary");
    }

    /*
     * Two ways a table can be unreadable, and the contract is **not the same for both** — which is why they
     * are two blocks rather than one.
     *
     * First, the fixture's own door for a directory entry that lies: the entry that points at `GSUB` is
     * rewritten to point past the end of the source. The loader refuses the whole face rather than half-reading
     * it, because a font whose own contents lie about their location is not one to trust. What this states is
     * that the refusal is a `Result` and not a crash, and that it leaves no half-built result behind.
     */
    {
        ConfFontSpec spec;
        ConfFace face;
        RoboTextShapeOptions options;

        memset(&spec, 0, sizeof(spec));
        ConfSpecEuropean(&spec);
        memset(&face, 0, sizeof(face));
        ConfFaceBuildBytes(&face, &spec);
        if (!ConfFacePatchTableOffset(&face, 0x47535542u /* GSUB */, (uint32_t)face.length + 4096u)) {
            RoboCheckMsg(0, "the fixture has no GSUB table to point past the end");
        } else {
            RoboTextShapeResult *result = NULL;
            Result status;

            CorpusFaceOpenTolerant(&face, CorpusSizePx);
            RoboTextShapeOptionsInit(&options);
            options.baseDirection = RoboTextDirectionLtr;
            status = RoboTextShape(ConfView("ffi"), &face.font, &options, &result, NULL);
            if (status == RoboOk) {
                fprintf(stdout, "    corpus.malformed[lying-directory]: the shape was accepted\n");
            }
            RoboCheckMsg(status != RoboOk, "a font whose directory points outside itself shaped anyway");
            RoboCheckMsg(result == NULL, "a refused shape left a result behind");
            ConfFaceClose(&face);
        }
    }

    /*
     * Second, a table whose **contents** lie while its directory is honest: a `GSUB` header whose ScriptList
     * offset points past the end of its own table, which is what a cut-off download produces. The engine's
     * answer here is the first half of §28's contract — it **refuses** (`RoboParseError`) rather than shaping
     * with guesses about what the table meant. The other half, *skipping* unreadable optional data, is what the
     * absence variants show (a face with no `GSUB` at all shapes every glyph and substitutes none).
     *
     * What is asserted is the part that matters either way: the answer is a `Result` and not a crash, it names
     * a parse problem rather than a wrong glyph, it leaves no half-built result behind, and it is the same on
     * the second ask.
     */
    {
        ConfFontSpec spec;
        ConfFace face;
        RoboTextShapeOptions options;
        RoboTextShapeResult *first = NULL;
        RoboTextShapeResult *second = NULL;
        Result firstStatus;
        Result secondStatus;

        memset(&spec, 0, sizeof(spec));
        ConfSpecEuropean(&spec);
        memset(&face, 0, sizeof(face));
        ConfFaceBuildBytes(&face, &spec);
        CorpusBreakGsubScriptList(&face);
        CorpusFaceOpenTolerant(&face, CorpusSizePx);
        RoboTextShapeOptionsInit(&options);
        options.baseDirection = RoboTextDirectionLtr;
        firstStatus = RoboTextShape(ConfView("ffi"), &face.font, &options, &first, NULL);
        secondStatus = RoboTextShape(ConfView("ffi"), &face.font, &options, &second, NULL);
        fprintf(stdout, "    corpus.malformed[gsub-script-list-content]: status %d\n", (int)firstStatus);
        RoboCheckEqInt((int)firstStatus, (int)RoboParseError);
        RoboCheckEqInt((int)secondStatus, (int)firstStatus);
        RoboCheckMsg(first == NULL && second == NULL, "a refused shape left a result behind");
        RoboCheckMsg(firstStatus >= RoboResultFirst && firstStatus <= RoboResultLast,
                     "a malformed face answered with a value outside the Result vocabulary");
        RoboTextShapeResultDestroy(second);
        RoboTextShapeResultDestroy(first);
        ConfFaceClose(&face);
    }

    /*
     * And the fixture's other preset is a **GDEF-absent** face with its `GSUB` intact — the "optional table
     * missing" half of the contract, which is a different thing from a malformed one and must not be read as
     * one: the ligature still forms, and only the mark class data is gone.
     */
    {
        ConfFontSpec spec;
        ConfFace face;
        RoboTextShapeResult *result;

        RoboTextShapeOptions options;
        RoboTextDiag diag;
        Result status;

        memset(&spec, 0, sizeof(spec));
        ConfSpecMalformedGsub(&spec);
        ConfFaceOpen(&face, &spec, CorpusSizePx);
        RoboTextShapeOptionsInit(&options);
        options.baseDirection = RoboTextDirectionLtr;
        RoboTextDiagReset(&diag);
        status = RoboTextShape(ConfView("ffi"), &face.font, &options, &result, &diag);
        if (status != RoboOk) {
            fprintf(stdout, "    corpus.malformed[gdef-absent]: status %d, dom %d, message '%s'\n", (int)status,
                    (int)diag.domain, diag.message ? diag.message : "(none)");
        }
        RoboCheckEqInt((int)status, (int)RoboOk);
        result = CorpusShape(&face, "ffi");
        RoboCheckEqInt((int)ConfTotalGlyphs(result), 1);
        RoboCheckMsg(ConfTotalLigatures(result) >= 1u, "a face with no GDEF lost its ligature too");
        CorpusExpectTiles(result, 3, "corpus.malformed");
        CorpusExpectMeasurementAgrees(result, &face, "corpus.malformed");
        RoboTextShapeResultDestroy(result);
        ConfFaceClose(&face);
    }
}

/* ------------------------------------------------------------------ */
/* §11 — determinism: the same request, one hundred times               */
/* ------------------------------------------------------------------ */

#define CorpusRepetitions 100u

static void TestCorpusDeterminism(void)
{
    ConfFontSpec latinSpec;
    ConfFontSpec complexSpec;
    ConfFontSpec verticalSpec;
    ConfVerticalGlyphIds verticalIds;
    ConfFace latin;
    ConfFace complex;
    ConfFace vertical;
    struct {
        const char *label;
        const ConfFace *face;
        const char *text;
        int vertical;
    } kSamples[] = {
        {"mixed", &latin, CorpusMixed, 0},
        {"ffi", &latin, CorpusFfi, 0},
        {"latin.extended", &latin, CorpusLatinExtended, 0},
        {"arabic", &complex, CorpusArabic, 0},
        {"arabic.mixed", &complex, CorpusArabicMixed, 0},
        {"indic", &complex, CorpusIndic, 0},
        {"vertical.cjk", &vertical, CorpusCjkColumn, 1},
        {"vertical.mark", &vertical, CorpusKanaVoicing, 1}
    };
    size_t index;

    memset(&latinSpec, 0, sizeof(latinSpec));
    memset(&complexSpec, 0, sizeof(complexSpec));
    memset(&verticalSpec, 0, sizeof(verticalSpec));
    memset(&verticalIds, 0, sizeof(verticalIds));
    ConfSpecEuropean(&latinSpec);
    CplxSpecComplex(&complexSpec);
    ConfSpecVertical(&verticalSpec, &verticalIds);
    ConfFaceOpen(&latin, &latinSpec, CorpusSizePx);
    ConfFaceOpen(&complex, &complexSpec, CorpusSizePx);
    ConfFaceOpen(&vertical, &verticalSpec, CorpusSizePx);

    /*
     * A hundred passes over each sample, compared against the first. Nothing here may depend on a hash map's
     * iteration order, a pointer's value, an uninitialised field or a scheduling decision, and a hundred
     * repetitions is the cheapest way to say so. `ConfResultsEqual` covers ids, clusters, advances, vertical
     * advances, orientations, offsets and source ranges; the two dumps cover everything else the record holds.
     */
    for (index = 0u; index < sizeof(kSamples) / sizeof(kSamples[0]); index++) {
        RoboTextShapeResult *first = kSamples[index].vertical
                                         ? CorpusShapeVertical(kSamples[index].face, kSamples[index].text)
                                         : CorpusShape(kSamples[index].face, kSamples[index].text);
        size_t firstLength = 0u;
        char *firstDump = CorpusDumpList(RoboTextShapeResultGlyphRunList(first), &firstLength);

        RoboCheckMsg(firstDump != NULL && firstLength > 0u, "the first block could not be dumped");
        for (unsigned repetition = 0u; repetition < CorpusRepetitions; repetition++) {
            RoboTextShapeResult *again = kSamples[index].vertical
                                             ? CorpusShapeVertical(kSamples[index].face, kSamples[index].text)
                                             : CorpusShape(kSamples[index].face, kSamples[index].text);
            size_t length = 0u;
            char *dump = CorpusDumpList(RoboTextShapeResultGlyphRunList(again), &length);

            if (!ConfResultsEqual(first, again)) {
                fprintf(stdout, "    corpus.determinism[%s]: repetition %u differs from the first\n",
                        kSamples[index].label, repetition);
                RoboCheckMsg(0, "shaping is not deterministic");
            }
            if (dump == NULL || length != firstLength ||
                (firstDump != NULL && memcmp(dump, firstDump, firstLength) != 0)) {
                fprintf(stdout, "    corpus.determinism[%s]: repetition %u dumped differently\n",
                        kSamples[index].label, repetition);
                RoboCheckMsg(0, "the recorded block is not deterministic");
            }
            RoboFree(dump);
            RoboTextShapeResultDestroy(again);
        }
        RoboFree(firstDump);
        RoboTextShapeResultDestroy(first);
    }

    ConfFaceClose(&vertical);
    ConfFaceClose(&complex);
    ConfFaceClose(&latin);
}

/* ------------------------------------------------------------------ */
/* §12 — the corpus report, which is the checked-in expected data       */
/* ------------------------------------------------------------------ */

/** Append @p text to a report buffer, ignoring anything that would not fit. */
static void CorpusAppend(char *buffer, size_t capacity, size_t *length, const char *text)
{
    size_t need = strlen(text);

    if (*length + need >= capacity) {
        return;
    }
    memcpy(buffer + *length, text, need);
    *length += need;
    buffer[*length] = '\0';
}

/** Append @p value in decimal. */
static void CorpusAppendUint(char *buffer, size_t capacity, size_t *length, unsigned value)
{
    char scratch[24];

    snprintf(scratch, sizeof(scratch), "%u", value);
    CorpusAppend(buffer, capacity, length, scratch);
}

/** Append @p value as a fixed-point number with three decimals, so the report is byte-comparable. */
static void CorpusAppendMilli(char *buffer, size_t capacity, size_t *length, float value)
{
    char scratch[32];
    long milli = (long)(value * 1000.0f + (value < 0.0f ? -0.5f : 0.5f));

    snprintf(scratch, sizeof(scratch), "%ld.%03ld", milli / 1000, (milli % 1000 < 0 ? -milli : milli) % 1000);
    CorpusAppend(buffer, capacity, length, scratch);
}

/**
 * Write one line per sample: the label, the scalars, the runs, the glyphs, the clusters, the width, the
 * height, the misses and the scripts the runs carry.
 *
 * This is the corpus's expected data in the form a reader can diff, and the same artifact a reviewer looks at
 * after a shaping change: a moved number is the change, and the label on the line says which sample moved.
 */
static size_t CorpusReport(char *buffer, size_t capacity)
{
    ConfFontSpec latinSpec;
    ConfFontSpec complexSpec;
    ConfFontSpec verticalSpec;
    ConfVerticalGlyphIds verticalIds;
    ConfFace latin;
    ConfFace complex;
    ConfFace vertical;
    /*
     * The samples as **data**: a face kind and a text, never a face pointer — a `static const` table cannot hold
     * the address of a local, and a table that had to be built at run time would not be the one a diff reads.
     */
    enum { CorpusFaceLatin = 0, CorpusFaceComplex = 1, CorpusFaceVertical = 2 };
    static const struct {
        const char *label;
        int faceKind;
        const char *text;
        int vertical;
    } kSamples[] = {
        {"latin", CorpusFaceLatin, CorpusLatin, 0},
        {"latin.extended", CorpusFaceLatin, CorpusLatinExtended, 0},
        {"latin.decomposed", CorpusFaceLatin, CorpusDecomposed, 0},
        {"greek", CorpusFaceLatin, CorpusGreek, 0},
        {"cyrillic", CorpusFaceLatin, CorpusCyrillic, 0},
        {"mixed.scripts", CorpusFaceLatin, CorpusMixed, 0},
        {"ligature", CorpusFaceLatin, CorpusFfi, 0},
        {"kerning", CorpusFaceLatin, CorpusKernPair, 0},
        {"arabic", CorpusFaceComplex, CorpusArabic, 0},
        {"arabic.mixed", CorpusFaceComplex, CorpusArabicMixed, 0},
        {"indic", CorpusFaceComplex, CorpusIndic, 0},
        {"indic.word", CorpusFaceComplex, CorpusIndicWord, 0},
        {"vertical.cjk", CorpusFaceVertical, CorpusCjkColumn, 1},
        {"vertical.mark", CorpusFaceVertical, CorpusKanaVoicing, 1}
    };
    const ConfFace *kFaces[3];
    size_t length = 0u;

    memset(&latinSpec, 0, sizeof(latinSpec));
    memset(&complexSpec, 0, sizeof(complexSpec));
    memset(&verticalSpec, 0, sizeof(verticalSpec));
    memset(&verticalIds, 0, sizeof(verticalIds));
    ConfSpecEuropean(&latinSpec);
    CplxSpecComplex(&complexSpec);
    ConfSpecVertical(&verticalSpec, &verticalIds);
    ConfFaceOpen(&latin, &latinSpec, CorpusSizePx);
    ConfFaceOpen(&complex, &complexSpec, CorpusSizePx);
    ConfFaceOpen(&vertical, &verticalSpec, CorpusSizePx);
    kFaces[CorpusFaceLatin] = &latin;
    kFaces[CorpusFaceComplex] = &complex;
    kFaces[CorpusFaceVertical] = &vertical;

    buffer[0] = '\0';
    CorpusAppend(buffer, capacity, &length, "# RoboText shaping conformance corpus (Phase 12.23).\n");
    CorpusAppend(buffer, capacity, &length,
                 "# One line per sample: label scalars runs glyphs clusters width height missing scripts...\n");
    for (size_t index = 0u; index < sizeof(kSamples) / sizeof(kSamples[0]); index++) {
        const ConfFace *face = kFaces[kSamples[index].faceKind];
        RoboTextShapeResult *result = kSamples[index].vertical ? CorpusShapeVertical(face, kSamples[index].text)
                                                              : CorpusShape(face, kSamples[index].text);

        CorpusAppend(buffer, capacity, &length, kSamples[index].label);
        CorpusAppend(buffer, capacity, &length, " ");
        CorpusAppendUint(buffer, capacity, &length, (unsigned)CorpusScalars(kSamples[index].text));
        CorpusAppend(buffer, capacity, &length, " ");
        CorpusAppendUint(buffer, capacity, &length, (unsigned)RoboTextShapeResultGlyphRunCount(result));
        CorpusAppend(buffer, capacity, &length, " ");
        CorpusAppendUint(buffer, capacity, &length, (unsigned)ConfTotalGlyphs(result));
        CorpusAppend(buffer, capacity, &length, " ");
        CorpusAppendUint(buffer, capacity, &length, (unsigned)CorpusClusterSum(result));
        CorpusAppend(buffer, capacity, &length, " ");
        CorpusAppendMilli(buffer, capacity, &length, ConfTotalAdvance(result));
        CorpusAppend(buffer, capacity, &length, " ");
        CorpusAppendMilli(buffer, capacity, &length,
                          RoboTextGlyphRunListAdvanceY(RoboTextShapeResultGlyphRunList(result)));
        CorpusAppend(buffer, capacity, &length, " ");
        CorpusAppendUint(buffer, capacity, &length, (unsigned)ConfTotalMissing(result));
        for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
            CorpusAppend(buffer, capacity, &length, " ");
            CorpusAppend(buffer, capacity, &length,
                         RoboTextScriptName(RoboTextGlyphRunScript(RoboTextShapeResultGlyphRun(result, run))));
        }
        CorpusAppend(buffer, capacity, &length, "\n");
        RoboTextShapeResultDestroy(result);
    }

    ConfFaceClose(&vertical);
    ConfFaceClose(&complex);
    ConfFaceClose(&latin);
    return length;
}

static void TestCorpusReport(void)
{
    static char first[16384];
    static char second[16384];
    size_t firstLength = CorpusReport(first, sizeof(first));
    size_t secondLength = CorpusReport(second, sizeof(second));
    const char *path = "Build/Text-ConformanceCorpus.txt";
    FILE *file;

    /* Two generations of the same table agree byte for byte, which is what makes it diffable. */
    if (secondLength != firstLength || memcmp(second, first, firstLength) != 0) {
        fprintf(stdout, "    corpus.report: the table is not reproducible (%u vs %u bytes)\n", (unsigned)firstLength,
                (unsigned)secondLength);
        RoboCheckMsg(0, "the conformance table is not reproducible");
    }
    /* Every sample is a line, and each line names its own scalars, runs, glyphs and clusters. */
    RoboCheckMsg(strstr(first, "latin 25 1 25 25 ") != NULL, "the Latin line is missing or has moved");
    RoboCheckMsg(strstr(first, "greek 6 1 6 6 ") != NULL, "the Greek line is missing or has moved");
    RoboCheckMsg(strstr(first, "cyrillic 6 1 6 6 ") != NULL, "the Cyrillic line is missing or has moved");
    RoboCheckMsg(strstr(first, "ligature 6 ") != NULL, "the ligature line is missing");
    RoboCheckMsg(strstr(first, "indic 3 1 1 1 ") != NULL, "the Indic line is missing or has moved");
    RoboCheckMsg(strstr(first, "arabic 5 ") != NULL, "the Arabic line is missing");
    RoboCheckMsg(strstr(first, "vertical.cjk 2 ") != NULL, "the vertical line is missing");
    /* The script column is the ISO 15924 short name, which is what `RoboTextScriptName` answers. */
    RoboCheckMsg(strstr(first, "Arab") != NULL, "no line names the Arabic script");
    RoboCheckMsg(strstr(first, "Latn") != NULL, "no line names the Latin script");
    RoboCheckMsg(strstr(first, "Deva") != NULL, "no line names the Devanagari script");

    file = fopen(path, "wb");
    if (file != NULL) {
        size_t written = fwrite(first, 1u, firstLength, file);

        fclose(file);
        if (written != firstLength) {
            fprintf(stdout, "    corpus.report: wrote %u of %u bytes\n", (unsigned)written, (unsigned)firstLength);
            RoboCheckMsg(0, "the conformance table was not written in full");
        }
    } else {
        /* A missing `Build/` directory is the caller's environment, not a shaping failure. */
        fprintf(stdout, "    corpus.report: could not write %s\n", path);
    }
}

/* ------------------------------------------------------------------ */
/* §13 — performance, recorded rather than asserted                     */
/* ------------------------------------------------------------------ */

static double CorpusMillis(clock_t from, clock_t to)
{
    return ((double)(to - from) * 1000.0) / (double)CLOCKS_PER_SEC;
}

static void TestCorpusPerformance(void)
{
    ConfFontSpec latinSpec;
    ConfFontSpec complexSpec;
    ConfFace latin;
    ConfFace complex;
    enum { CorpusFaceLatin = 0, CorpusFaceComplex = 1 };
    static const struct {
        const char *label;
        int faceKind;
        const char *text;
    } kSamples[] = {
        {"latin", CorpusFaceLatin, CorpusLatin},
        {"greek", CorpusFaceLatin, CorpusGreek},
        {"ligature", CorpusFaceLatin, CorpusFfi},
        {"kerning", CorpusFaceLatin, CorpusKernPair},
        {"arabic", CorpusFaceComplex, CorpusArabic},
        {"arabic.mixed", CorpusFaceComplex, CorpusArabicMixed},
        {"indic", CorpusFaceComplex, CorpusIndic},
        {"indic.word", CorpusFaceComplex, CorpusIndicWord}
    };
    const ConfFace *kFaces[2];
    static const unsigned kIterations = 200u;
    double worst = 0.0;
    RoboTextScalarIndex totalScalars = 0u;

    memset(&latinSpec, 0, sizeof(latinSpec));
    memset(&complexSpec, 0, sizeof(complexSpec));
    ConfSpecEuropean(&latinSpec);
    CplxSpecComplex(&complexSpec);
    ConfFaceOpen(&latin, &latinSpec, CorpusSizePx);
    ConfFaceOpen(&complex, &complexSpec, CorpusSizePx);
    kFaces[CorpusFaceLatin] = &latin;
    kFaces[CorpusFaceComplex] = &complex;

    /*
     * §34's "record basic timing, detect a catastrophic regression". Recorded and printed rather than asserted
     * against a budget: a microsecond threshold in a suite that runs on a loaded machine is a flaky test, and a
     * flaky conformance suite gets muted. What **is** asserted is the shape of the work — every timed iteration
     * produced the same answer as the reference — plus one generous ceiling that only a hang or a quadratic
     * blow-up can cross.
     */
    for (size_t index = 0u; index < sizeof(kSamples) / sizeof(kSamples[0]); index++) {
        const ConfFace *face = kFaces[kSamples[index].faceKind];
        RoboTextShapeResult *reference = CorpusShape(face, kSamples[index].text);
        clock_t from = clock();
        double elapsed;

        totalScalars += CorpusScalars(kSamples[index].text) * kIterations;
        for (unsigned iteration = 0u; iteration < kIterations; iteration++) {
            RoboTextShapeResult *result = CorpusShape(face, kSamples[index].text);

            if (!ConfResultsEqual(reference, result)) {
                fprintf(stdout, "    corpus.performance[%s]: iteration %u differs\n", kSamples[index].label,
                        iteration);
                RoboCheckMsg(0, "a timed iteration differed from the reference");
            }
            RoboTextShapeResultDestroy(result);
        }
        elapsed = CorpusMillis(from, clock());
        fprintf(stdout, "    corpus.performance: %-13s %u iterations, %7.2f ms total, %6.3f ms/iteration\n",
                kSamples[index].label, kIterations, elapsed, elapsed / (double)kIterations);
        if (elapsed > worst) {
            worst = elapsed;
        }
        RoboTextShapeResultDestroy(reference);
    }

    fprintf(stdout, "    corpus.performance: %u shapes over %u scalars, worst sample %.2f ms\n",
            (unsigned)(kIterations * (sizeof(kSamples) / sizeof(kSamples[0]))), (unsigned)totalScalars, worst);
    /* Generous on purpose: the whole workload is 1,600 shapes of short text. */
    RoboCheckMsg(worst < 30000.0, "a shaping workload took longer than thirty seconds");

    ConfFaceClose(&complex);
    ConfFaceClose(&latin);
}

void TestRegisterTextShapingCorpus(void)
{
    TestRegister("text.shaping.corpus.stages", TestCorpusStages);
    TestRegister("text.shaping.corpus.scripts", TestCorpusScripts);
    TestRegister("text.shaping.corpus.latin_extended", TestCorpusLatinExtended);
    TestRegister("text.shaping.corpus.ligature_kerning", TestCorpusLigatureAndKerning);
    TestRegister("text.shaping.corpus.unsupported_scripts", TestCorpusUnsupportedScripts);
    TestRegister("text.shaping.corpus.bidi", TestCorpusBidi);
    TestRegister("text.shaping.corpus.fallback", TestCorpusFallback);
    TestRegister("text.shaping.corpus.vertical", TestCorpusVertical);
    TestRegister("text.shaping.corpus.measurement_selection_editing", TestCorpusMeasurementSelectionEditing);
    TestRegister("text.shaping.corpus.malformed", TestCorpusMalformed);
    TestRegister("text.shaping.corpus.determinism", TestCorpusDeterminism);
    TestRegister("text.shaping.corpus.report", TestCorpusReport);
    TestRegister("text.shaping.corpus.performance", TestCorpusPerformance);
}
