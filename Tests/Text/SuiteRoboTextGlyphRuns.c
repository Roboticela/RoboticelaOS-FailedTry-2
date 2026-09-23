/*
 * Glyph runs (Phase 12.14) — `text.glyphrun.*`.
 *
 * The phase finalizes `RoboTextGlyphRun` as the contract between shaping and everything above it, and adds
 * `RoboTextGlyphRunList` as the record of a whole shaped text. What this suite proves is that the four records
 * a run now derives from its glyph array — **clusters**, **flags**, **metrics** and **totals** — agree with the
 * glyphs, and that the list's two orders, its keys and its merge are what the phase says.
 *
 * ## Why every case runs through the shipped pack
 *
 * The fixture is `RoboTextTestSupport.h`'s, which binds a face the **shipped system typeface pack** selected,
 * so every number here comes from `hmtx`, `hhea` and `OS/2` through RoboFont. A synthetic face could pass while
 * every advance and every ascent was wrong. The one exception is the pair of runs the merge case builds by hand
 * — that case needs two runs over *one* view, which shaping a text cannot produce.
 *
 * ## What is deliberately not asserted here, and why
 *
 * **A font-fallback boundary.** The pack ships one family, so a scalar Inter does not cover produces a run with
 * `.notdef` and a *counted miss* rather than a second run — `text.glyphrun.fallback` asserts exactly that, which
 * is the honest form of \"no fabricated boundary\". A real chain is a pack change (assets + manifest + licence),
 * not a text-layer change, and Phase 12.5 records the same limitation.
 *
 * **Reordering.** The visual order is 12.8's answer, read by `RoboTextGlyphRunListOrderVisual`; this suite
 * asserts that the *list* publishes it correctly, not that the algorithm is right — `text.bidi.*` owns that.
 */

#include "RoboTextTestSupport.h"

#include "RoboText/RoboTextRunList.h"

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
    /* Every result carries a list, including one over empty text: the phase's \"complete, not on demand\". */
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

/** The single glyph run of a text this suite knows is one run. */
static const RoboTextGlyphRun *OnlyRun(const RoboTextShapeResult *result)
{
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(result), 1);
    return RoboTextShapeResultGlyphRun(result, 0u);
}

/** A float comparison with a stated tolerance: the phase's numbers are sums of `hmtx` values. */
static void Near(float actual, float expected, float tolerance, const char *what)
{
    float difference = actual - expected;

    if (difference < 0.0f) {
        difference = -difference;
    }
    if (difference > tolerance) {
        TestFail(__FILE__, __LINE__, "%s: %g is not within %g of %g", what,
                 (double)actual, (double)tolerance, (double)expected);
    }
}

/** The first glyph of @p run carrying @p flag, or -1. */
static int FirstGlyphWithFlag(const RoboTextGlyphRun *run, RoboTextGlyphFlag flag)
{
    for (size_t index = 0u; index < RoboTextGlyphRunCount(run); index++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)index);

        if (glyph && (glyph->flags & (RoboTextGlyphFlags)flag) != 0u) {
            return (int)index;
        }
    }
    return -1;
}

/**
 * A run built by hand over @p view's @p range: what the merge and split-reason cases need.
 *
 * The record has no constructor in this build because nothing outside shaping ever needs to make one — except a
 * test that wants **two runs over one view**, which is the only way to produce two contiguous runs with keys
 * that *should* be equal and see whether the merge does it.
 */
static RoboTextRun HandRun(RoboTextRange range, RoboTextScript script, RoboTextDirection direction,
                           RoboTextBidiLevel level, const TextFixture *fixture)
{
    RoboTextRun run;

    memset(&run, 0, sizeof(run));
    run.range = range;
    run.script = script;
    run.direction = direction;
    run.bidiLevel = level;
    run.baseDirection = RoboTextDirectionLtr;
    run.font = fixture->font;
    RoboTextLanguageInit(&run.language);
    RoboTextFeatureSetInit(&run.features);
    return run;
}

/* ------------------------------------------------------------------ */
/* The key (12.14.13)                                                  */
/* ------------------------------------------------------------------ */

static void TestGlyphRunKey(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, "AVATAR");
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    RoboTextGlyphRunKey key;
    RoboTextGlyphRunKey other;

    RoboCheckEqInt((int)RoboTextGlyphRunListCount(list), 1);

    /* The key of the run, and of the list's view of it: the same record through two doors. */
    RoboCheckEqInt((int)RoboTextGlyphRunKeyOf(RoboTextGlyphRunListRun(list, 0u), &key), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunListKey(list, 0u, &other), (int)RoboOk);
    RoboCheck(RoboTextGlyphRunKeyEqual(&key, &other));
    RoboCheckEqInt((int)RoboTextGlyphRunKeyDifference(&key, &other), (int)RoboTextGlyphRunKeyFieldNone);

    /* The identity a key carries: the face it was shaped with, by pointer, and the size it was set at. */
    RoboCheck(key.font.face == fixture.face);
    Near(key.font.style.sizePx, 16.0f, 0.001f, "the key's size");
    RoboCheckEqInt((int)key.script, (int)RoboUScriptLatin);

    /*
     * One field at a time, and each must name *itself*: this is the phase's \"deterministic splitting\". A
     * caller that saw two runs where it expected one asks the difference and gets the field, not just \"no\".
     */
    other = key;
    other.script = RoboUScriptGreek;
    RoboCheckEqInt((int)RoboTextGlyphRunKeyDifference(&key, &other), (int)RoboTextGlyphRunKeyFieldScript);
    RoboCheck(!RoboTextGlyphRunKeyEqual(&key, &other));

    other = key;
    other.direction = RoboTextDirectionRtl;
    RoboCheckEqInt((int)RoboTextGlyphRunKeyDifference(&key, &other),
                   (int)RoboTextGlyphRunKeyFieldDirection);

    other = key;
    other.bidiLevel = (RoboTextBidiLevel)(key.bidiLevel + 1u);
    RoboCheckEqInt((int)RoboTextGlyphRunKeyDifference(&key, &other),
                   (int)RoboTextGlyphRunKeyFieldBidiLevel);

    other = key;
    other.baseDirection = RoboTextDirectionRtl;
    RoboCheckEqInt((int)RoboTextGlyphRunKeyDifference(&key, &other),
                   (int)RoboTextGlyphRunKeyFieldBaseDirection);

    other = key;
    other.mode = RoboTextGlyphGenerationCmapPerScalar;
    if (key.mode != RoboTextGlyphGenerationCmapPerScalar) {
        RoboCheckEqInt((int)RoboTextGlyphRunKeyDifference(&key, &other),
                       (int)RoboTextGlyphRunKeyFieldMode);
    }

    other = key;
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&other.features, "kern", RoboTextFeatureOff, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunKeyDifference(&key, &other),
                   (int)RoboTextGlyphRunKeyFieldFeatures);

    other = key;
    other.font.style.sizePx = 32.0f;
    RoboCheckEqInt((int)RoboTextGlyphRunKeyDifference(&key, &other), (int)RoboTextGlyphRunKeyFieldFont);

    /* The vocabulary is complete and spelled. */
    for (int field = (int)RoboTextGlyphRunKeyFieldNone; field <= (int)RoboTextGlyphRunKeyFieldMode; field++) {
        RoboCheck(RoboTextGlyphRunKeyFieldIsKnown((RoboTextGlyphRunKeyField)field));
        RoboCheck(RoboTextGlyphRunKeyFieldName((RoboTextGlyphRunKeyField)field) != NULL);
    }
    RoboCheck(!RoboTextGlyphRunKeyFieldIsKnown((RoboTextGlyphRunKeyField)99));
    RoboCheckStrEq(RoboTextGlyphRunKeyFieldName(RoboTextGlyphRunKeyFieldNone), "none");
    RoboCheckStrEq(RoboTextGlyphRunKeyFieldName(RoboTextGlyphRunKeyFieldBidiLevel), "bidi-level");
    RoboCheck(RoboTextGlyphRunKeyEqual(NULL, NULL));
    RoboCheck(!RoboTextGlyphRunKeyEqual(&key, NULL));
    RoboCheckEqInt((int)RoboTextGlyphRunKeyOf(NULL, &other), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunKeyOf(RoboTextGlyphRunListRun(list, 0u), NULL),
                   (int)RoboInvalidArgument);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Clusters (12.14.6, 12.14.7)                                         */
/* ------------------------------------------------------------------ */

static void TestGlyphRunClusters(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, "abc");
    const RoboTextGlyphRun *run = OnlyRun(result);
    RoboTextClusterIndex cluster = 0u;

    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 3);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterCount(run), 3);
    RoboCheckEqInt((int)RoboTextGlyphRunMarks(run), 0);
    RoboCheckEqInt((int)RoboTextGlyphRunUnsafeToBreak(run), 0);

    for (size_t index = 0u; index < 3u; index++) {
        const RoboTextGlyphCluster *entry = RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)index);
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)index);

        RoboCheckMsg(entry != NULL, "a cluster is missing where the glyph count says there is one");
        RoboCheckEqInt((int)entry->source.start, (int)index);
        RoboCheckEqInt((int)entry->source.end, (int)index + 1);
        RoboCheckEqInt((int)entry->glyphStart, (int)index);
        RoboCheckEqInt((int)entry->glyphEnd, (int)index + 1);
        RoboCheckEqInt((int)glyph->cluster, (int)index);
        RoboCheck((glyph->flags & (RoboTextGlyphFlags)RoboTextGlyphFlagClusterStart) != 0u);
        RoboCheck((glyph->flags & (RoboTextGlyphFlags)RoboTextGlyphFlagClusterContinuation) == 0u);
    }
    RoboCheck(RoboTextGlyphRunCluster(run, 3u) == NULL);

    /* The inverse door: a click produces a scalar index and wants a cluster. */
    RoboCheckEqInt((int)RoboTextGlyphRunClusterForScalar(run, 0u, &cluster, NULL), (int)RoboOk);
    RoboCheckEqInt((int)cluster, 0);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterForScalar(run, 2u, &cluster, NULL), (int)RoboOk);
    RoboCheckEqInt((int)cluster, 2);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterForScalar(run, 3u, &cluster, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterForScalar(run, 0u, NULL, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterForScalar(NULL, 0u, &cluster, NULL), (int)RoboInvalidArgument);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/**
 * A base and its combining mark are **one** cluster, and the mark is the glyph a line may not begin at.
 *
 * `q` + U+0301, and the choice of base is the case's whole subtlety: **Inter composes `e` + U+0301** through a
 * contextual lookup, so the phase's own example arrives as *one* glyph covering two scalars — which is a real
 * and interesting record (it is asserted below, in `TestGlyphRunClustersComposed`) and is not the two-glyph
 * shape this case is about. `qacute` does not exist, so a `q` keeps its mark beside it and the pair is the
 * cluster-with-two-glyphs the flag word exists for.
 */
static void TestGlyphRunClustersMark(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, "q\xCC\x81");
    const RoboTextGlyphRun *run = OnlyRun(result);
    const RoboTextGlyphCluster *cluster;
    int markIndex = FirstGlyphWithFlag(run, RoboTextGlyphFlagMark);

    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 2);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterCount(run), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunMarks(run), 1);

    cluster = RoboTextGlyphRunCluster(run, 0u);
    RoboCheckMsg(cluster != NULL, "a base and its mark produced no cluster");
    RoboCheckEqInt((int)cluster->source.start, 0);
    RoboCheckEqInt((int)cluster->source.end, 2);
    RoboCheckEqInt((int)cluster->glyphStart, 0);
    RoboCheckEqInt((int)cluster->glyphEnd, 2);

    /* The base opens the cluster and the mark continues it — the two flags are exclusive by construction. */
    RoboCheck((RoboTextGlyphRunGlyph(run, 0u)->flags & (RoboTextGlyphFlags)RoboTextGlyphFlagClusterStart) != 0u);
    RoboCheck((RoboTextGlyphRunGlyph(run, 1u)->flags &
               (RoboTextGlyphFlags)RoboTextGlyphFlagClusterContinuation) != 0u);

    RoboCheckMsg(markIndex == 1, "the combining mark was not identified by its character");
    RoboCheck((RoboTextGlyphRunGlyph(run, 1u)->flags &
               (RoboTextGlyphFlags)RoboTextGlyphFlagUnsafeToBreak) != 0u);
    RoboCheckEqInt((int)RoboTextGlyphRunUnsafeToBreak(run), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, 1u)->codePoint, 0x0301);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, 0u)->codePoint, (int)'q');

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/**
 * A base and a mark the face **composed** into one glyph: one glyph, one cluster, two scalars, no character.
 *
 * The other half of the mark story, and the half a shape record has to get right for the same reason: the
 * substitution is not a ligature the reader asked for, it is the font's own composition, and the glyph that
 * results is neither the base nor the mark. `codePoint == 0` is "no single character" — a reader asking which
 * character this drew gets the honest answer rather than a plausible `e`.
 */
static void TestGlyphRunClustersComposed(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, "e\xCC\x81");
    const RoboTextGlyphRun *run = OnlyRun(result);
    const RoboTextGlyphCluster *cluster;

    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterCount(run), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, 0u)->codePoint, 0);
    /* The characters are untouched: the glyph still stands for both scalars. */
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, 0u)->source.start, 0);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, 0u)->source.end, 2);

    cluster = RoboTextGlyphRunCluster(run, 0u);
    RoboCheckMsg(cluster != NULL, "a composition produced no cluster");
    RoboCheckEqInt((int)cluster->source.end, 2);
    RoboCheckEqInt((int)cluster->glyphEnd, 1);

    /* Not a mark: the product of mark attachment is a character of its own, not a mark hanging off one. */
    RoboCheckEqInt((int)RoboTextGlyphRunMarks(run), 0);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/**
 * A ligature is one glyph standing for several scalars, in **one** cluster that covers all of them.
 *
 * The shipped Inter carries `dlig` and no `liga`, so `!?` under the discretionary policy is the real-font
 * evidence: two scalars, one glyph, one cluster, and `ligatureComponents` of two.
 */
static void TestGlyphRunClustersLigature(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions options;
    RoboTextShapeResult *plain;
    RoboTextShapeResult *ligated;
    const RoboTextGlyphRun *run;
    const RoboTextGlyphCluster *cluster;

    RoboTextShapeOptionsInit(&options);
    plain = ShapeWith(&fixture, &options, "!?");

    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantDiscretionary, &options.ligatures);
    ligated = ShapeWith(&fixture, &options, "!?");

    RoboCheckMsg(RoboTextShapeResultGlyphRunCount(plain) == 1 && RoboTextShapeResultGlyphRunCount(ligated) == 1,
                 "the interrobang case is a one-run text");

    /* Without the policy: two glyphs, two clusters, no ligature flag. */
    RoboCheckEqInt((int)RoboTextGlyphRunCount(RoboTextShapeResultGlyphRun(plain, 0u)), 2);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterCount(RoboTextShapeResultGlyphRun(plain, 0u)), 2);
    RoboCheckEqInt((int)RoboTextGlyphRunLigatures(RoboTextShapeResultGlyphRun(plain, 0u)), 0);

    run = RoboTextShapeResultGlyphRun(ligated, 0u);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunLigatures(run), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterCount(run), 1);
    RoboCheckEqInt((int)FirstGlyphWithFlag(run, RoboTextGlyphFlagLigature), 0);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, 0u)->ligatureComponents, 2);
    /* A glyph standing for two scalars has no single character — and says so. */
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, 0u)->codePoint, 0);
    /* The characters are never touched: the glyph's source still names both scalars. */
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(run).start, 0);
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(run).end, 2);

    cluster = RoboTextGlyphRunCluster(run, 0u);
    RoboCheckMsg(cluster != NULL, "a ligature produced no cluster");
    RoboCheckEqInt((int)cluster->source.start, 0);
    RoboCheckEqInt((int)cluster->source.end, 2);
    RoboCheckEqInt((int)cluster->glyphStart, 0);
    RoboCheckEqInt((int)cluster->glyphEnd, 1);

    /*
     * A ligature may not have a shape boundary drawn through it: splitting the run on either side asks the
     * font a different contextual question, which is what `UnsafeToConcat` means.
     */
    RoboCheck((RoboTextGlyphRunGlyph(run, 0u)->flags &
               (RoboTextGlyphFlags)RoboTextGlyphFlagUnsafeToConcat) != 0u);

    RoboTextShapeResultDestroy(plain);
    RoboTextShapeResultDestroy(ligated);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Flags (12.14.22, 12.14.23)                                          */
/* ------------------------------------------------------------------ */

static void TestGlyphRunFlags(void)
{
    char word[80];
    size_t length = 0u;

    RoboCheck(RoboTextGlyphFlagIsKnown(RoboTextGlyphFlagNone));
    RoboCheck(RoboTextGlyphFlagIsKnown(RoboTextGlyphFlagClusterStart));
    RoboCheck(RoboTextGlyphFlagIsKnown(RoboTextGlyphFlagUnsafeToConcat));
    RoboCheck(!RoboTextGlyphFlagIsKnown((RoboTextGlyphFlag)64));
    RoboCheckStrEq(RoboTextGlyphFlagName(RoboTextGlyphFlagNone), "none");
    RoboCheckStrEq(RoboTextGlyphFlagName(RoboTextGlyphFlagClusterStart), "cluster-start");
    RoboCheckStrEq(RoboTextGlyphFlagName(RoboTextGlyphFlagClusterContinuation), "cluster-continuation");
    RoboCheckStrEq(RoboTextGlyphFlagName(RoboTextGlyphFlagMark), "mark");
    RoboCheckStrEq(RoboTextGlyphFlagName(RoboTextGlyphFlagLigature), "ligature");
    RoboCheckStrEq(RoboTextGlyphFlagName(RoboTextGlyphFlagUnsafeToBreak), "unsafe-to-break");
    RoboCheckStrEq(RoboTextGlyphFlagName(RoboTextGlyphFlagUnsafeToConcat), "unsafe-to-concat");

    /* A word with no flags is `@`, which cannot be mistaken for a flag's name. */
    RoboCheckEqInt((int)RoboTextGlyphFlagsWrite(RoboTextGlyphFlagNone, word, sizeof(word), &length),
                   (int)RoboOk);
    RoboCheckStrEq(word, "@");
    RoboCheckEqInt((int)length, 1);

    /* Two flags, in the enum's order rather than the order they were set. */
    RoboCheckEqInt((int)RoboTextGlyphFlagsWrite((RoboTextGlyphFlags)RoboTextGlyphFlagMark |
                                                    (RoboTextGlyphFlags)RoboTextGlyphFlagClusterStart,
                                                word, sizeof(word), &length),
                   (int)RoboOk);
    RoboCheckStrEq(word, "cluster-start|mark");

    RoboCheckEqInt((int)RoboTextGlyphFlagsWrite((RoboTextGlyphFlags)RoboTextGlyphFlagUnsafeToBreak |
                                                    (RoboTextGlyphFlags)RoboTextGlyphFlagLigature |
                                                    (RoboTextGlyphFlags)RoboTextGlyphFlagClusterContinuation,
                                                word, sizeof(word), &length),
                   (int)RoboOk);
    RoboCheckStrEq(word, "cluster-continuation|ligature|unsafe-to-break");

    /* Complete or absent: a word that does not fit leaves the empty string, never a prefix. */
    word[0] = 'x';
    RoboCheckEqInt((int)RoboTextGlyphFlagsWrite((RoboTextGlyphFlags)RoboTextGlyphFlagUnsafeToConcat,
                                                word, 4u, &length),
                   (int)RoboNotSupported);
    RoboCheckStrEq(word, "");
    RoboCheckEqInt((int)length, 0);
    RoboCheckEqInt((int)RoboTextGlyphFlagsWrite(RoboTextGlyphFlagNone, NULL, 4u, &length),
                   (int)RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Metrics (12.14.18)                                                  */
/* ------------------------------------------------------------------ */

static void TestGlyphRunMetrics(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, "Typography");
    const RoboTextGlyphRun *run = OnlyRun(result);
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    RoboTextFontMetrics fromRun;
    RoboTextFontMetrics fromContext;
    RoboTextFontMetrics fromList;
    RoboTextShapeResult *empty;

    RoboCheckEqInt((int)RoboTextGlyphRunMetrics(run, &fromRun), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontContextMetrics(&fixture.font, &fromContext, NULL), (int)RoboOk);

    /* The run's metrics are the context's, because the run asked the same door at the same size. */
    Near(fromRun.ascentPx, fromContext.ascentPx, 0.001f, "the run's ascent");
    Near(fromRun.descentPx, fromContext.descentPx, 0.001f, "the run's descent");
    Near(fromRun.lineGapPx, fromContext.lineGapPx, 0.001f, "the run's line gap");
    Near(fromRun.lineHeightPx, fromContext.lineHeightPx, 0.001f, "the run's line height");
    Near(fromRun.capHeightPx, fromContext.capHeightPx, 0.001f, "the run's cap height");
    Near(fromRun.xHeightPx, fromContext.xHeightPx, 0.001f, "the run's x-height");
    RoboCheckEqInt((int)fromRun.unitsPerEm, (int)fromContext.unitsPerEm);

    Near(RoboTextGlyphRunAscent(run), fromContext.ascentPx, 0.001f, "the ascent accessor");
    Near(RoboTextGlyphRunDescent(run), fromContext.descentPx, 0.001f, "the descent accessor");
    Near(RoboTextGlyphRunLineGap(run), fromContext.lineGapPx, 0.001f, "the line gap accessor");
    Near(RoboTextGlyphRunLineHeight(run), fromContext.lineHeightPx, 0.001f, "the line height accessor");

    /* The identity the font layer documents, now a property of a run: ascent + descent + gap == line height. */
    Near(RoboTextGlyphRunAscent(run) + RoboTextGlyphRunDescent(run) + RoboTextGlyphRunLineGap(run),
         RoboTextGlyphRunLineHeight(run), 0.01f, "the line box identity");

    /* A paragraph's metrics come from its first run, and agree with that run's. */
    RoboCheckEqInt((int)RoboTextGlyphRunListMetrics(list, &fromList), (int)RoboOk);
    Near(fromList.ascentPx, fromRun.ascentPx, 0.001f, "the list's ascent");
    Near(fromList.lineHeightPx, fromRun.lineHeightPx, 0.001f, "the list's line height");

    RoboCheckEqInt((int)RoboTextGlyphRunMetrics(NULL, &fromRun), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunMetrics(run, NULL), (int)RoboInvalidArgument);

    /* Empty text: a list with no runs, which has no metrics and says so rather than answering zeroes. */
    {
        RoboTextShapeOptions options;

        RoboTextShapeOptionsInit(&options);
        empty = ShapeWith(&fixture, &options, "");
    }
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(empty), 0);
    RoboCheckEqInt((int)RoboTextGlyphRunListCount(RoboTextShapeResultGlyphRunList(empty)), 0);
    RoboCheckEqInt((int)RoboTextGlyphRunListMetrics(RoboTextShapeResultGlyphRunList(empty), &fromList),
                   (int)RoboNotFound);
    RoboCheck(RoboTextGlyphRunListVisualIsLogical(RoboTextShapeResultGlyphRunList(empty)));
    RoboCheckEqInt((int)RoboTextGlyphRunListVisualCount(RoboTextShapeResultGlyphRunList(empty)), 0);

    RoboTextShapeResultDestroy(empty);
    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Totals (12.14.19, 12.14.20)                                         */
/* ------------------------------------------------------------------ */

static void TestGlyphRunTotals(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, "AVATAR");
    const RoboTextGlyphRun *run = OnlyRun(result);
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    double sum = 0.0;
    float upto = 0.0f;

    for (size_t index = 0u; index < RoboTextGlyphRunCount(run); index++) {
        sum += (double)RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)index)->advanceX;
    }
    /* One rounding per run, of a sum accumulated in `double`: the phase's \"total advances are correct\". */
    Near(RoboTextGlyphRunAdvance(run), (float)sum, 0.0005f, "the run's advance");
    Near(RoboTextGlyphRunListAdvance(list), RoboTextGlyphRunAdvance(run), 0.0005f, "the list's advance");
    Near(RoboTextGlyphRunAdvanceY(run), 0.0f, 0.0001f, "the horizontal run's vertical advance");
    Near(RoboTextGlyphRunListAdvanceY(list), 0.0f, 0.0001f, "the list's vertical advance");

    /* The prefix sum a caret uses: the whole advance at the count, and a growing one before it. */
    RoboCheckEqInt((int)RoboTextGlyphRunAdvanceUpTo(run, (RoboTextGlyphIndex)RoboTextGlyphRunCount(run),
                                                    &upto, NULL),
                   (int)RoboOk);
    Near(upto, RoboTextGlyphRunAdvance(run), 0.0005f, "the whole advance");
    RoboCheckEqInt((int)RoboTextGlyphRunAdvanceUpTo(run, 0u, &upto, NULL), (int)RoboOk);
    Near(upto, 0.0f, 0.0001f, "the advance before the first glyph");
    RoboCheckEqInt((int)RoboTextGlyphRunAdvanceUpTo(run, (RoboTextGlyphIndex)RoboTextGlyphRunCount(run) + 1u,
                                                    &upto, NULL),
                   (int)RoboInvalidArgument);

    /* Every glyph's advance is the face's own plus the font's part: the identity that makes both readable. */
    for (size_t index = 0u; index < RoboTextGlyphRunCount(run); index++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)index);
        float base = 0.0f;

        RoboCheckEqInt((int)RoboTextFontUnitsToDevice(&fixture.font, (double)glyph->advanceUnits, &base, NULL),
                       (int)RoboOk);
        Near(glyph->advanceX, base + glyph->advanceAdjustX, 0.0005f, "a glyph's advance identity");
    }

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Order (12.14.9, 12.14.10, 12.14.28)                                 */
/* ------------------------------------------------------------------ */

static void TestGlyphRunOrderLogical(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, "Hello");
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    size_t visual = 0u;

    RoboCheckEqInt((int)RoboTextGlyphRunListCount(list), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunListVisualCount(list), 1);
    /* One left-to-right run needs no reordering, and the list says so rather than leaving it to be inferred. */
    RoboCheck(RoboTextGlyphRunListVisualIsLogical(list));
    RoboCheckEqInt((int)RoboTextGlyphRunListVisualRun(list, 0u, &visual), (int)RoboOk);
    RoboCheckEqInt((int)visual, 0);
    RoboCheckEqInt((int)RoboTextGlyphRunListVisualIndex(list, 0u, &visual), (int)RoboOk);
    RoboCheckEqInt((int)visual, 0);
    RoboCheckEqInt((int)RoboTextGlyphRunListVisualRun(list, 1u, &visual), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunListVisualRun(list, 0u, NULL), (int)RoboInvalidArgument);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/**
 * Right-to-left, and the one claim the visual order makes: a Latin word between two Hebrew ones is painted
 * **between** them.
 *
 * `עברית English עברית` with the base declared right-to-left. The levels are 1 for the Hebrew, 1 for the
 * spaces N1 resolves down to the base, and 2 for the Latin — so UAX #9 L2 reverses the whole run sequence and
 * the Latin run ends up in the middle of the line rather than at one end. The assertion is stated as that
 * linguistic fact rather than as a permutation of indices, because the run *count* is the run set's business
 * (three runs today, more if the shaper ever cuts finer) and the fact is not.
 */
static void TestGlyphRunOrderVisual(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;
    const RoboTextGlyphRunList *list;
    size_t count;
    size_t latin = (size_t)-1;
    size_t latinVisual = 0u;

    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionRtl;
    result = ShapeWith(&fixture, &options,
                       "\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA English \xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA");
    list = RoboTextShapeResultGlyphRunList(result);
    count = RoboTextGlyphRunListCount(list);

    RoboCheckMsg(count >= 3u, "the mixed-direction text produced too few runs to have an order");

    /* Not one run was reversed in place: the logical order is still the text's own. */
    for (size_t index = 0u; index < count; index++) {
        const RoboTextGlyphRun *run = RoboTextGlyphRunListRun(list, index);

        RoboCheckMsg(run != NULL, "a logical run is missing");
        if (index != 0u) {
            const RoboTextGlyphRun *previous = RoboTextGlyphRunListRun(list, index - 1u);

            RoboCheckMsg(RoboTextGlyphRunSourceRange(previous).end <= RoboTextGlyphRunSourceRange(run).start,
                         "the logical order is not ascending in source");
        }
        /*
         * The Latin word is the run the **embedding raised** — level 2, an even level inside a right-to-left
         * paragraph — and that is what identifies it. The spaces around it are scripted `Latn` too (the script
         * resolver carries a neutral to its left neighbour), so "the Latin run" is not a unique description;
         * "the run the embedding raised" is, and it is the one the case is about.
         */
        if (RoboTextGlyphRunScript(run) == RoboUScriptLatin &&
            !RoboTextBidiLevelIsRightToLeft(RoboTextGlyphRunBidiLevel(run))) {
            RoboCheckMsg(latin == (size_t)-1, "the embedding raised more than one run");
            latin = index;
        }
    }
    RoboCheckMsg(latin != (size_t)-1, "the Latin word produced no run");

    /* L2 reordered: the visual order is not the logical one, which is the whole point of the case. */
    RoboCheck(!RoboTextGlyphRunListVisualIsLogical(list));

    /*
     * A permutation, both ways. Injectivity is checked by *measuring* it rather than by counting bits: an
     * index may appear at most once in a visual order, and with a run count this small the pairwise check is
     * the clearer statement of that.
     */
    for (size_t visual = 0u; visual < count; visual++) {
        size_t logical = 0u;
        size_t back = 0u;

        RoboCheckEqInt((int)RoboTextGlyphRunListVisualRun(list, visual, &logical), (int)RoboOk);
        RoboCheck(logical < count);
        RoboCheckEqInt((int)RoboTextGlyphRunListVisualIndex(list, logical, &back), (int)RoboOk);
        RoboCheckEqInt((int)back, (int)visual);
        for (size_t earlier = 0u; earlier < visual; earlier++) {
            size_t other = 0u;

            RoboCheckEqInt((int)RoboTextGlyphRunListVisualRun(list, earlier, &other), (int)RoboOk);
            RoboCheckMsg(other != logical, "a logical run appears twice in the visual order");
        }
    }

    /* The Latin run sits between the two Hebrew ones — the linguistic fact, asserted as one. */
    RoboCheckEqInt((int)RoboTextGlyphRunListVisualIndex(list, latin, &latinVisual), (int)RoboOk);
    RoboCheckMsg(latinVisual != 0u, "the Latin word was painted leftmost");
    RoboCheckMsg(latinVisual + 1u != count, "the Latin word was painted rightmost");

    /*
     * And the extremes are Hebrew, which is the other half of the same fact: the first logical run is painted
     * **rightmost** — its visual index is the last — and the text ends on the left. A logical-order list with a
     * visual array beside it is what makes both statements readable at once.
     */
    {
        size_t firstVisual = 0u;

        RoboCheckEqInt((int)RoboTextGlyphRunListVisualIndex(list, 0u, &firstVisual), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextGlyphRunListVisualRun(list, count - 1u, &latin), (int)RoboOk);
        RoboCheckEqInt((int)firstVisual, (int)(count - 1u));
        RoboCheckEqInt((int)latin, 0);
    }

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Splitting (12.14.11) and merging (12.14.12)                         */
/* ------------------------------------------------------------------ */

/** The difference between two adjacent runs of one list, or `None` when the index is the last. */
static RoboTextGlyphRunKeyField DifferenceAt(const RoboTextGlyphRunList *list, size_t index)
{
    RoboTextGlyphRunKey first;
    RoboTextGlyphRunKey second;

    if (index + 1u >= RoboTextGlyphRunListCount(list)) {
        return RoboTextGlyphRunKeyFieldNone;
    }
    RoboCheckEqInt((int)RoboTextGlyphRunListKey(list, index, &first), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunListKey(list, index + 1u, &second), (int)RoboOk);
    return RoboTextGlyphRunKeyDifference(&first, &second);
}

static void TestGlyphRunSplit(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions options;
    RoboTextShapeResult *scripts;
    RoboTextShapeResult *ranged;
    const RoboTextGlyphRunList *list;
    int found = 0;

    /* A script change, from the text itself: Greek then Latin is two script runs by construction. */
    scripts = Shape(&fixture, "\xCE\xB1\xCE\xB2\xCE\xB3 ABC");
    list = RoboTextShapeResultGlyphRunList(scripts);
    RoboCheckMsg(RoboTextGlyphRunListCount(list) >= 2u, "a script change did not split the runs");
    for (size_t index = 0u; index + 1u < RoboTextGlyphRunListCount(list); index++) {
        RoboTextGlyphRunKeyField field = DifferenceAt(list, index);

        if (field == RoboTextGlyphRunKeyFieldScript) {
            found = 1;
            break;
        }
    }
    RoboCheckMsg(found, "no adjacent pair of runs differed by script");

    /* A feature change, from the caller: `kern` off over the first two scalars of `AVATAR`. */
    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeatureRange(&options, RoboTextRangeMake(0u, 2u), "kern",
                                                            RoboTextFeatureOff, 0u),
                   (int)RoboOk);
    ranged = ShapeWith(&fixture, &options, "AVATAR");
    list = RoboTextShapeResultGlyphRunList(ranged);
    RoboCheckMsg(RoboTextGlyphRunListCount(list) >= 2u, "a feature override did not split the runs");
    for (size_t index = 0u; index + 1u < RoboTextGlyphRunListCount(list); index++) {
        RoboTextGlyphRunKeyField field = DifferenceAt(list, index);

        if (field == RoboTextGlyphRunKeyFieldFeatures) {
            found = 1;
            break;
        }
    }
    RoboCheckMsg(found, "no adjacent pair of runs differed by feature settings");

    RoboTextShapeResultDestroy(scripts);
    RoboTextShapeResultDestroy(ranged);
    TextFixtureClose(&fixture);
}

/**
 * Two runs over one view, merged into one — and the three cases where the merge must **not** happen.
 *
 * The positive case is built by hand because shaping a text cannot produce two runs that *should* be one; the
 * negatives are the three boundaries the phase names. After the merge the record has to be a real run, which is
 * what the count, the total and the validation below check.
 */
static void TestGlyphRunCoalesce(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions options;
    RoboTextShapeResult *whole;
    RoboTextView view;
    RoboTextGlyphRun *first = NULL;
    RoboTextGlyphRun *second = NULL;
    RoboTextGlyphRun *third = NULL;
    RoboTextGlyphRun **pair;
    RoboTextGlyphRunList *list = NULL;
    RoboTextGlyphRunList *view2 = NULL;
    RoboTextGlyphRunKey keyA;
    RoboTextGlyphRunKey keyB;
    RoboTextGlyphRunProblem runProblem = RoboTextGlyphRunProblemNone;
    RoboTextGlyphRunListProblem problem = RoboTextGlyphRunListProblemNone;
    RoboTextRun runA;
    RoboTextRun runB;
    RoboTextRun runC;
    float wholeAdvance;
    float mergedAdvance;

    RoboTextShapeOptionsInit(&options);
    view = View("Typo");
    whole = ShapeWith(&fixture, &options, "Typo");
    wholeAdvance = RoboTextGlyphRunAdvance(OnlyRun(whole));

    runA = HandRun(RoboTextRangeMake(0u, 3u), RoboUScriptLatin, RoboTextDirectionLtr, 0u, &fixture);
    runB = HandRun(RoboTextRangeMake(3u, 4u), RoboUScriptLatin, RoboTextDirectionLtr, 0u, &fixture);
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &runA, &first, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &runB, &second, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(first), 3);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(second), 1);

    /* The precondition the merge rests on: the two halves are the *same* shape run. */
    RoboCheckEqInt((int)RoboTextGlyphRunKeyOf(first, &keyA), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunKeyOf(second, &keyB), (int)RoboOk);
    RoboCheckMsg(RoboTextGlyphRunKeyEqual(&keyA, &keyB), "two halves of one word are not the same shape run");

    pair = (RoboTextGlyphRun **)RoboCalloc(2u, sizeof(*pair));
    RoboCheckMsg(pair != NULL, "the run pair could not be allocated");
    pair[0] = first;
    pair[1] = second;
    RoboCheckEqInt((int)RoboTextGlyphRunListCreate(pair, 2u, &list, NULL), (int)RoboOk);
    RoboCheck(RoboTextGlyphRunListOwnsRuns(list));
    RoboCheckEqInt((int)RoboTextGlyphRunListValidate(list, &problem, NULL), (int)RoboOk);
    RoboCheckEqInt((int)problem, (int)RoboTextGlyphRunListProblemNone);
    RoboCheckEqInt((int)RoboTextGlyphRunListCount(list), 2);

    RoboCheckEqInt((int)RoboTextGlyphRunListCoalesce(list, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunListCount(list), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(RoboTextGlyphRunListRun(list, 0u)), 4);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterCount(RoboTextGlyphRunListRun(list, 0u)), 4);
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(RoboTextGlyphRunListRun(list, 0u)).start, 0);
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(RoboTextGlyphRunListRun(list, 0u)).end, 4);
    mergedAdvance = RoboTextGlyphRunAdvance(RoboTextGlyphRunListRun(list, 0u));
    Near(mergedAdvance, wholeAdvance, 0.0005f, "the merged run's advance");
    /* The merge is a real run, not a record with the two totals added and the first run's clusters. */
    RoboCheckEqInt((int)RoboTextGlyphRunValidate(RoboTextGlyphRunListRun(list, 0u), &runProblem, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)runProblem, (int)RoboTextGlyphRunProblemNone);
    RoboCheckEqInt((int)RoboTextGlyphRunListValidate(list, &problem, NULL), (int)RoboOk);
    RoboCheck(RoboTextGlyphRunListVisualIsLogical(list));
    RoboTextGlyphRunListDestroy(list);
    list = NULL;

    /* A different script is a different run, even over contiguous text. */
    runC = HandRun(RoboTextRangeMake(3u, 4u), RoboUScriptGreek, RoboTextDirectionLtr, 0u, &fixture);
    first = NULL;
    second = NULL;
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &runA, &first, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &runC, &second, NULL), (int)RoboOk);
    pair = (RoboTextGlyphRun **)RoboCalloc(2u, sizeof(*pair));
    RoboCheckMsg(pair != NULL, "the run pair could not be allocated");
    pair[0] = first;
    pair[1] = second;
    RoboCheckEqInt((int)RoboTextGlyphRunListCreate(pair, 2u, &list, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunListCoalesce(list, NULL), (int)RoboOk);
    RoboCheckMsg(RoboTextGlyphRunListCount(list) == 2u, "two runs of different scripts were merged");
    RoboTextGlyphRunListDestroy(list);
    list = NULL;

    /* A gap is not a merge: equal keys across missing text would swallow what the list does not own. */
    runC = HandRun(RoboTextRangeMake(3u, 4u), RoboUScriptLatin, RoboTextDirectionLtr, 0u, &fixture);
    runA = HandRun(RoboTextRangeMake(0u, 2u), RoboUScriptLatin, RoboTextDirectionLtr, 0u, &fixture);
    first = NULL;
    third = NULL;
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &runA, &first, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &runC, &third, NULL), (int)RoboOk);
    pair = (RoboTextGlyphRun **)RoboCalloc(2u, sizeof(*pair));
    RoboCheckMsg(pair != NULL, "the run pair could not be allocated");
    pair[0] = first;
    pair[1] = third;
    RoboCheckEqInt((int)RoboTextGlyphRunListCreate(pair, 2u, &list, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunListCoalesce(list, NULL), (int)RoboOk);
    RoboCheckMsg(RoboTextGlyphRunListCount(list) == 2u, "two runs with a gap between them were merged");
    /* A gap is legal for a list, and the total is still the sum of what the list holds. */
    RoboCheckEqInt((int)RoboTextGlyphRunListValidate(list, &problem, NULL), (int)RoboOk);
    RoboTextGlyphRunListDestroy(list);
    list = NULL;

    /* A view does not own its runs, so it may not merge them. */
    first = NULL;
    second = NULL;
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &runA, &first, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &runB, &second, NULL), (int)RoboOk);
    pair = (RoboTextGlyphRun **)RoboCalloc(2u, sizeof(*pair));
    RoboCheckMsg(pair != NULL, "the run pair could not be allocated");
    pair[0] = first;
    pair[1] = second;
    RoboCheckEqInt((int)RoboTextGlyphRunListCreateView(pair, 2u, &view2, NULL), (int)RoboOk);
    RoboCheck(!RoboTextGlyphRunListOwnsRuns(view2));
    RoboCheckEqInt((int)RoboTextGlyphRunListCoalesce(view2, NULL), (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextGlyphRunListCount(view2), 2);
    /* A view releases itself and not the runs, so the caller still owns them. */
    RoboTextGlyphRunListDestroy(view2);
    RoboFree(pair);
    RoboTextGlyphRunDestroy(first);
    RoboTextGlyphRunDestroy(second);

    RoboTextShapeResultDestroy(whole);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Construction refusal (12.14.37)                                     */
/* ------------------------------------------------------------------ */

static void TestGlyphRunListRefusal(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextGlyphRunProblem runProblem = RoboTextGlyphRunProblemNone;
    RoboTextView view = View("Typo");
    RoboTextGlyphRun *runs[2];
    RoboTextGlyphRun **pair;
    RoboTextGlyphRunList *list = NULL;
    RoboTextGlyphRunListProblem problem = RoboTextGlyphRunListProblemNone;
    RoboTextRun runA = HandRun(RoboTextRangeMake(0u, 2u), RoboUScriptLatin, RoboTextDirectionLtr, 0u, &fixture);
    RoboTextRun runB = HandRun(RoboTextRangeMake(1u, 3u), RoboUScriptLatin, RoboTextDirectionLtr, 0u, &fixture);
    RoboTextRun runC = HandRun(RoboTextRangeMake(3u, 4u), RoboUScriptLatin, RoboTextDirectionLtr, 0u, &fixture);

    /* Overlap: two runs claiming one scalar is not a set of runs, and is refused before it is indexed. */
    runs[0] = NULL;
    runs[1] = NULL;
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &runA, &runs[0], NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &runB, &runs[1], NULL), (int)RoboOk);
    pair = (RoboTextGlyphRun **)RoboCalloc(2u, sizeof(*pair));
    RoboCheckMsg(pair != NULL, "the run pair could not be allocated");
    pair[0] = runs[0];
    pair[1] = runs[1];
    RoboCheckEqInt((int)RoboTextGlyphRunListCreate(pair, 2u, &list, NULL), (int)RoboInvalidArgument);
    RoboCheckMsg(list == NULL, "an overlapping set became a list");
    /*
     * And a construction that failed did not consume what it was handed. Read both runs below, then release
     * them: the reads are what make the ownership rule a *checked* claim rather than a documented one — a run
     * the failed constructor had freed would be read here as whatever the allocator left behind, and under a
     * sanitizer this is the same place a double free would surface.
     */
    RoboCheckEqInt((int)RoboTextGlyphRunValidate(runs[0], &runProblem, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunValidate(runs[1], &runProblem, NULL), (int)RoboOk);
    RoboFree(pair);
    RoboTextGlyphRunDestroy(runs[0]);
    RoboTextGlyphRunDestroy(runs[1]);

    /* Descending sources: an order that is not the text's is not an order. */
    runs[0] = NULL;
    runs[1] = NULL;
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &runC, &runs[0], NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &runA, &runs[1], NULL), (int)RoboOk);
    pair = (RoboTextGlyphRun **)RoboCalloc(2u, sizeof(*pair));
    RoboCheckMsg(pair != NULL, "the run pair could not be allocated");
    pair[0] = runs[0];
    pair[1] = runs[1];
    RoboCheckEqInt((int)RoboTextGlyphRunListCreate(pair, 2u, &list, NULL), (int)RoboInvalidArgument);
    RoboFree(pair);
    RoboTextGlyphRunDestroy(runs[0]);
    RoboTextGlyphRunDestroy(runs[1]);

    /* The vocabulary is complete and spelled. */
    RoboCheck(RoboTextGlyphRunListProblemIsKnown(RoboTextGlyphRunListProblemNone));
    RoboCheck(RoboTextGlyphRunListProblemIsKnown(RoboTextGlyphRunListProblemTotal));
    RoboCheck(!RoboTextGlyphRunListProblemIsKnown((RoboTextGlyphRunListProblem)99));
    RoboCheckStrEq(RoboTextGlyphRunListProblemName(RoboTextGlyphRunListProblemNone), "none");
    RoboCheckStrEq(RoboTextGlyphRunListProblemName(RoboTextGlyphRunListProblemOverlap), "overlap");
    RoboCheckStrEq(RoboTextGlyphRunListProblemName(RoboTextGlyphRunListProblemVisualOrder), "visual-order");

    RoboCheckEqInt((int)RoboTextGlyphRunListValidate(NULL, &problem, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunListCreate(NULL, 2u, &list, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunListCreate(NULL, 0u, NULL, NULL), (int)RoboInvalidArgument);

    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Validation (12.14.36, 12.14.37)                                     */
/* ------------------------------------------------------------------ */

static void TestGlyphRunValidation(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, "office AVATAR");
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    RoboTextGlyphRunProblem problem = RoboTextGlyphRunProblemNone;
    RoboTextGlyphRunListProblem listProblem = RoboTextGlyphRunListProblemNone;

    /*
     * Every run a result produces is valid, and so is the set: this is the phase's \"invalid runs are rejected
     * by validation\" proved in the direction that matters — the module does not produce one.
     */
    for (size_t index = 0u; index < RoboTextShapeResultGlyphRunCount(result); index++) {
        const RoboTextGlyphRun *run = RoboTextShapeResultGlyphRun(result, index);

        RoboCheckEqInt((int)RoboTextGlyphRunValidate(run, &problem, NULL), (int)RoboOk);
        RoboCheckEqInt((int)problem, (int)RoboTextGlyphRunProblemNone);
    }
    RoboCheckEqInt((int)RoboTextGlyphRunListValidate(list, &listProblem, NULL), (int)RoboOk);
    RoboCheckEqInt((int)listProblem, (int)RoboTextGlyphRunListProblemNone);

    /* Every run knows its own counters, and the list totals are the runs'. */
    {
        RoboTextGlyphStats stats;
        double sum = 0.0;

        RoboCheckEqInt((int)RoboTextShapeResultGetGlyphStats(result, &stats), (int)RoboOk);
        RoboCheckEqInt((int)stats.glyphRuns, (int)RoboTextShapeResultGlyphRunCount(result));
        for (size_t index = 0u; index < stats.glyphRuns; index++) {
            sum += (double)RoboTextGlyphRunAdvance(RoboTextShapeResultGlyphRun(result, index));
        }
        Near(RoboTextGlyphRunListAdvance(list), (float)sum, 0.0005f, "the list's total against the runs'");
    }

    /*
     * And the aggregate is a sum of **every** counter, which is a claim about a door rather than about a run:
     * a text with a ligature in it must report one, because a caller reading `ligatureGlyphs` from the result
     * is asking exactly that question and could not otherwise tell a zero from a field nobody summed.
     */
    {
        RoboTextShapeOptions options;
        RoboTextShapeResult *ligated;
        RoboTextGlyphStats stats;

        RoboTextShapeOptionsInit(&options);
        RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantDiscretionary, &options.ligatures);
        ligated = ShapeWith(&fixture, &options, "!?");
        RoboCheckEqInt((int)RoboTextShapeResultGetGlyphStats(ligated, &stats), (int)RoboOk);
        RoboCheckEqInt((int)stats.ligatureGlyphs, 1);
        RoboCheckEqInt((int)stats.ligatureScalars, 2);
        RoboCheckEqInt((int)stats.clusters, 1);
        RoboCheckEqInt((int)stats.marks, 0);
        RoboTextShapeResultDestroy(ligated);
    }

    /* The vocabulary is complete and spelled, and a NULL run is refused rather than assumed valid. */
    RoboCheck(RoboTextGlyphRunProblemIsKnown(RoboTextGlyphRunProblemNone));
    RoboCheck(RoboTextGlyphRunProblemIsKnown(RoboTextGlyphRunProblemDirection));
    RoboCheck(!RoboTextGlyphRunProblemIsKnown((RoboTextGlyphRunProblem)99));
    RoboCheckStrEq(RoboTextGlyphRunProblemName(RoboTextGlyphRunProblemNone), "none");
    RoboCheckStrEq(RoboTextGlyphRunProblemName(RoboTextGlyphRunProblemDirection), "direction");
    RoboCheckStrEq(RoboTextGlyphRunProblemName((RoboTextGlyphRunProblem)99), "unknown");
    RoboCheckStrEq(RoboTextGlyphRunListProblemName((RoboTextGlyphRunListProblem)99), "unknown");
    RoboCheckEqInt((int)RoboTextGlyphRunValidate(NULL, &problem, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunValidate(RoboTextShapeResultGlyphRun(result, 0u), NULL, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunListValidate(NULL, NULL, NULL), (int)RoboInvalidArgument);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Fallback (12.14.17)                                                 */
/* ------------------------------------------------------------------ */

/**
 * A character the pack's one family does not cover produces a **run with a counted miss**, not a second run.
 *
 * `Hello 世界` is the phase's example, and the honest answer in this tree is that the Han scalars are script
 * runs of their own (so three runs, cut by script) but *the same face* — because a fallback boundary is a
 * **chain** the caller supplies and the shipped pack carries one family. Phase 12.5 records the same limitation.
 * What this case proves is the half that a text layer owns: the runs are cut where the *text* changes, the
 * missing count follows the characters, and no run claims a face the caller did not give it.
 */
static void TestGlyphRunFallback(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, "ab\xE4\xB8\x96\xE7\x95\x8C cd");
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    size_t han = (size_t)-1;
    size_t missing = 0u;
    RoboTextGlyphRunListProblem problem = RoboTextGlyphRunListProblemNone;

    RoboCheckMsg(RoboTextGlyphRunListCount(list) >= 2u, "a script change produced one run");

    for (size_t index = 0u; index < RoboTextGlyphRunListCount(list); index++) {
        const RoboTextGlyphRun *run = RoboTextGlyphRunListRun(list, index);
        const RoboTextFontContext *font = RoboTextGlyphRunFont(run);

        RoboCheckMsg(font != NULL, "a run carries no font context");
        RoboCheckMsg(font->face == fixture.face, "a run claims a face the caller did not give it");
        RoboCheck(font->face != NULL);
        missing += RoboTextGlyphRunMissingGlyphs(run);
        if (RoboTextGlyphRunScript(run) == RoboUScriptHan) {
            han = index;
        }
    }
    RoboCheckMsg(han != (size_t)-1, "the Han characters did not form a script run of their own");
    RoboCheckMsg(RoboTextGlyphRunMissingGlyphs(RoboTextGlyphRunListRun(list, han)) > 0u,
                 "the Han run claims glyphs the shipped face does not have");
    RoboCheckMsg(missing > 0u, "no glyph was counted missing for characters the face does not cover");
    RoboCheckEqInt((int)RoboTextGlyphRunListValidate(list, &problem, NULL), (int)RoboOk);
    RoboCheckEqInt((int)problem, (int)RoboTextGlyphRunListProblemNone);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The dump (12.14.35)                                                 */
/* ------------------------------------------------------------------ */

static void TestGlyphRunWrite(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = Shape(&fixture, "AV");
    const RoboTextGlyphRun *run = OnlyRun(result);
    const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(result);
    char buffer[8192];
    size_t length = 0u;

    RoboCheckEqInt((int)RoboTextGlyphRunWrite(run, buffer, sizeof(buffer), &length), (int)RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(buffer, "GlyphRun {") != NULL);
    RoboCheck(strstr(buffer, "clusters 2") != NULL);
    RoboCheck(strstr(buffer, "flags=") != NULL);
    RoboCheck(strstr(buffer, "INVALID") == NULL);
    RoboCheck(buffer[length - 1u] == '\n');

    RoboCheckEqInt((int)RoboTextGlyphRunListWrite(list, buffer, sizeof(buffer), &length), (int)RoboOk);
    RoboCheck(strstr(buffer, "GlyphRunList {") != NULL);
    RoboCheck(strstr(buffer, "runs 1") != NULL);
    RoboCheck(strstr(buffer, "visual 0") != NULL);

    /* Complete or absent: a buffer that cannot hold the block is left empty, never half-written. */
    buffer[0] = 'x';
    RoboCheckEqInt((int)RoboTextGlyphRunWrite(run, buffer, 8u, &length), (int)RoboNotSupported);
    RoboCheckStrEq(buffer, "");
    buffer[0] = 'x';
    RoboCheckEqInt((int)RoboTextGlyphRunListWrite(list, buffer, 8u, &length), (int)RoboNotSupported);
    RoboCheckStrEq(buffer, "");

    RoboCheckEqInt((int)RoboTextGlyphRunWrite(NULL, buffer, sizeof(buffer), &length), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunListWrite(NULL, buffer, sizeof(buffer), &length),
                   (int)RoboInvalidArgument);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Performance (12.14.48)                                              */
/* ------------------------------------------------------------------ */

/**
 * A measured baseline for `UTF-8 → GlyphRunList`, printed rather than asserted against a threshold.
 *
 * The phase asks for a **baseline**, not for an optimization: the numbers below are what a later phase compares
 * itself with, and a threshold in a unit test would be a machine-speed assertion that fails on a slower box
 * without anything being wrong. What *is* asserted is that the work is done — runs, glyphs and clusters are
 * counted and must be non-zero — so a regression that silently shaped nothing cannot pass as fast.
 */
static void TestGlyphRunPerformance(void)
{
    const char *text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. AVATAR Typography 2026.";
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions options;
    RoboTextShapeResult *result = NULL;
    RoboTextGlyphStats stats;
    clock_t started;
    clock_t finished;
    double millis;
    size_t runs;
    size_t clusters = 0u;

    RoboTextShapeOptionsInit(&options);
    started = clock();
    RoboCheckEqInt((int)RoboTextShape(View(text), &fixture.font, &options, &result, NULL), (int)RoboOk);
    finished = clock();
    millis = ((double)(finished - started) * 1000.0) / (double)CLOCKS_PER_SEC;

    RoboCheckEqInt((int)RoboTextShapeResultGetGlyphStats(result, &stats), (int)RoboOk);
    runs = RoboTextShapeResultGlyphRunCount(result);
    RoboCheck(runs > 0u);
    RoboCheck(stats.glyphs > 0u);
    for (size_t index = 0u; index < runs; index++) {
        clusters += RoboTextGlyphRunClusterCount(RoboTextShapeResultGlyphRun(result, index));
    }
    RoboCheck(clusters > 0u);
    RoboCheckEqInt((int)RoboTextGlyphRunListCount(RoboTextShapeResultGlyphRunList(result)), (int)runs);

    printf("           text.glyphrun.performance: bytes=%u runs=%u glyphs=%u clusters=%u ms=%.3f\n",
           (unsigned)strlen(text), (unsigned)runs, (unsigned)stats.glyphs, (unsigned)clusters, millis);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterTextGlyphRuns(void)
{
    TestRegister("text.glyphrun.key", TestGlyphRunKey);
    TestRegister("text.glyphrun.clusters", TestGlyphRunClusters);
    TestRegister("text.glyphrun.clusters_mark", TestGlyphRunClustersMark);
    TestRegister("text.glyphrun.clusters_composed", TestGlyphRunClustersComposed);
    TestRegister("text.glyphrun.clusters_ligature", TestGlyphRunClustersLigature);
    TestRegister("text.glyphrun.flags", TestGlyphRunFlags);
    TestRegister("text.glyphrun.metrics", TestGlyphRunMetrics);
    TestRegister("text.glyphrun.totals", TestGlyphRunTotals);
    TestRegister("text.glyphrun.order_logical", TestGlyphRunOrderLogical);
    TestRegister("text.glyphrun.order_visual", TestGlyphRunOrderVisual);
    TestRegister("text.glyphrun.split", TestGlyphRunSplit);
    TestRegister("text.glyphrun.coalesce", TestGlyphRunCoalesce);
    TestRegister("text.glyphrun.refusal", TestGlyphRunListRefusal);
    TestRegister("text.glyphrun.validation", TestGlyphRunValidation);
    TestRegister("text.glyphrun.fallback", TestGlyphRunFallback);
    TestRegister("text.glyphrun.write", TestGlyphRunWrite);
    TestRegister("text.glyphrun.performance", TestGlyphRunPerformance);
}
