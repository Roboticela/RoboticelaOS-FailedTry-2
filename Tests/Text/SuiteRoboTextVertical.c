/*
 * Vertical writing — `text.vertical.*` (Phase 12.20).
 *
 * ## The claim this suite is arranged around
 *
 * > RoboText owns writing-mode-aware shaping and glyph positioning; the renderer only paints the resulting
 * > `GlyphRun`.
 *
 * So the cases below check exactly the facts a painter would otherwise have to decide for itself:
 *
 *   **the axes**        which vector the pen accumulates (`text.vertical.writing_axes`);
 *   **the orientation** upright or sideways, per character and in context, and the rotation that follows
 *                       (`text.vertical.orientation`, `text.vertical.orientation_context`);
 *   **the cut**         a change of orientation is a run boundary, so a painter reads one orientation per run
 *                       and never a code point (`text.vertical.run_split`);
 *   **the features**    `vert`/`vrt2` reaching 12.10's GSUB like any other tag (`text.vertical.vert_feature`);
 *   **the metrics**     `advanceY` from `vmtx`, the origin as a placement offset, and the synthesised count
 *                       when the face carries no vertical metrics (`text.vertical.{advance,origin,synthesised}`);
 *   **the consumers**   12.15's measurement and 12.8's bidi, both read rather than re-derived
 *                       (`text.vertical.measurement`, `text.vertical.bidi`).
 *
 * ## Why the fixture states both numbers
 *
 * The vertical fixture gives every glyph a horizontal advance of `400 + id` and a vertical one of `1000 + id`,
 * and that difference is the whole reason the advance cases mean anything: a build that quietly reused `hmtx`
 * for `advanceY` would agree with a fixture that derived one from the other, and disagree with this one.
 *
 * ## What is deliberately absent
 *
 * No `vrt2` separate from `vert`: the fixture carries one tag and both are requested by the hook, which is
 * what `text.vertical.vert_feature` asserts by reading the *result* — a face that has only `vert` applies
 * only `vert`, and enabling the tag it lacks changes nothing. No glyph *outline* is involved: the fixture has
 * no `glyf`, so the vertical origin is the `vmtx` bearing (the case the fixture is built for), and a
 * `glyf`-boxed origin is `RoboTextFontGlyphVerticalMetrics`' own case, stated in its comment and in
 * `Documentation/Runtime/Text/VerticalText.md` §"the origin".
 */

#include "RoboTextConformanceFont.h"
#include "RoboTextConformanceSupport.h"

#include <string.h>

#define VerticalSizePx 16.0f

/* The scalars, spelled in UTF-8 so a reader can see which character each case is about. */
#define VerticalHan0 "\xE4\xB8\x80"     /* U+4E00 */
#define VerticalHan1 "\xE4\xB8\x81"     /* U+4E01 */
#define VerticalKana0 "\xE3\x81\x82"    /* U+3042 */
#define VerticalComma "\xE3\x80\x81"    /* U+3001 */
#define VerticalHalfKana "\xEF\xBD\xB6" /* U+FF76 */
#define VerticalArabicBeh "\xD8\xA8"    /* U+0628 */
#define VerticalVoicing "\xE3\x82\x99"   /* U+3099, the combining voiced sound mark */
#define VerticalKanaVoicing VerticalKana0 VerticalVoicing
#define VerticalLatinA "A"
#define VerticalLatinB "B"
#define VerticalDigit "1"
#define VerticalSpace " "
#define VerticalLatinAB VerticalLatinA VerticalLatinB
#define VerticalHanPair VerticalHan0 VerticalHan1
#define VerticalCjkLatinCjk VerticalHan0 VerticalLatinAB VerticalHan1
#define VerticalDecimal VerticalDigit VerticalDigit

/** The fixture's 1000-unit em at @ref VerticalSizePx, through the module's own one scaling door. */
static float VerticalPx(const ConfFace *face, double units)
{
    float out = 0.0f;

    RoboCheckEqInt((int)RoboTextFontUnitsToDevice(&face->font, units, &out, NULL), (int)RoboOk);
    return out;
}

/** Open the vertical fixture and hand back its glyph ids. */
static void VerticalFaceOpen(ConfFace *face, ConfVerticalGlyphIds *ids)
{
    ConfFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    ConfSpecVertical(&spec, ids);
    ConfFaceOpen(face, &spec, VerticalSizePx);
}

/** Shape @p text at @p mode with @p orientation, everything else defaulted. */
static RoboTextShapeResult *VerticalShape(const ConfFace *face,
                                          const char *text,
                                          RoboTextWritingMode mode,
                                          RoboTextTextOrientation orientation)
{
    RoboTextShapeOptions options;

    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    options.writingMode = mode;
    options.textOrientation = orientation;
    return ConfShape(face, text, &options);
}

/** The first glyph of @p result's @p run. */
static const RoboTextGlyph *VerticalFirstGlyph(const RoboTextShapeResult *result)
{
    const RoboTextGlyphRun *run = RoboTextShapeResultGlyphRun(result, 0u);

    RoboCheckMsg(run != NULL && RoboTextGlyphRunCount(run) > 0u, "the sample produced no glyph");
    return RoboTextGlyphRunGlyph(run, 0u);
}

/* ------------------------------------------------------------------ */
/* §2 — the writing mode and its axes                                  */
/* ------------------------------------------------------------------ */

static void TestVerticalWritingAxes(void)
{
    RoboTextWritingAxes horizontal = RoboTextWritingModeAxes(RoboTextWritingHorizontal);
    RoboTextWritingAxes rl = RoboTextWritingModeAxes(RoboTextWritingVerticalRightToLeft);
    RoboTextWritingAxes lr = RoboTextWritingModeAxes(RoboTextWritingVerticalLeftToRight);
    float x = 0.0f;
    float y = 0.0f;

    /* The table the header draws, one row each: the *inline* axis is +y in both vertical modes and the
     * *block* axis is what tells them apart. A boolean could not have said this. */
    RoboCheckEqInt((int)horizontal.inlineX, 1);
    RoboCheckEqInt((int)horizontal.inlineY, 0);
    RoboCheckEqInt((int)horizontal.blockX, 0);
    RoboCheckEqInt((int)horizontal.blockY, 1);
    RoboCheck(RoboTextWritingAxesIsHorizontal(&horizontal));

    RoboCheckEqInt((int)rl.inlineX, 0);
    RoboCheckEqInt((int)rl.inlineY, 1);
    RoboCheckEqInt((int)rl.blockX, -1);
    RoboCheckEqInt((int)rl.blockY, 0);
    RoboCheck(!RoboTextWritingAxesIsHorizontal(&rl));

    RoboCheckEqInt((int)lr.inlineX, 0);
    RoboCheckEqInt((int)lr.inlineY, 1);
    RoboCheckEqInt((int)lr.blockX, 1);
    RoboCheckEqInt((int)lr.blockY, 0);

    /* The two doors that turn "the pen moved 12px" into a vector, so no call site writes the swap. */
    RoboCheckEqInt((int)RoboTextWritingAxesInline(&rl, 12.0f, &x, &y), (int)RoboOk);
    RoboCheckEqInt((int)x, 0);
    RoboCheckEqInt((int)y, 12);
    RoboCheckEqInt((int)RoboTextWritingAxesBlock(&rl, 12.0f, &x, &y), (int)RoboOk);
    RoboCheckEqInt((int)x, -12);
    RoboCheckEqInt((int)y, 0);
    /* The block axis is the only difference between the two vertical modes. */
    RoboCheckEqInt((int)RoboTextWritingAxesBlock(&lr, 12.0f, &x, &y), (int)RoboOk);
    RoboCheckEqInt((int)x, 12);
    RoboCheckEqInt((int)y, 0);

    /* The predicates, and the one convenience that says why the block vector matters. */
    RoboCheck(RoboTextWritingModeIsKnown(RoboTextWritingHorizontal));
    RoboCheck(RoboTextWritingModeIsKnown(RoboTextWritingVerticalRightToLeft));
    RoboCheck(RoboTextWritingModeIsKnown(RoboTextWritingVerticalLeftToRight));
    RoboCheck(!RoboTextWritingModeIsKnown((RoboTextWritingMode)99));
    RoboCheck(RoboTextWritingModeIsVertical(RoboTextWritingVerticalRightToLeft));
    RoboCheck(RoboTextWritingModeIsVertical(RoboTextWritingVerticalLeftToRight));
    RoboCheck(!RoboTextWritingModeIsVertical(RoboTextWritingHorizontal));
    RoboCheck(RoboTextWritingModeBlockIsLeftward(RoboTextWritingVerticalRightToLeft));
    RoboCheck(!RoboTextWritingModeBlockIsLeftward(RoboTextWritingVerticalLeftToRight));
    RoboCheck(!RoboTextWritingModeBlockIsLeftward(RoboTextWritingHorizontal));

    RoboCheck(RoboTextWritingModeIsValid(RoboTextWritingVerticalRightToLeft, RoboTextTextOrientationMixed));
    RoboCheck(RoboTextWritingModeIsValid(RoboTextWritingHorizontal, RoboTextTextOrientationUpright));
    RoboCheck(!RoboTextWritingModeIsValid((RoboTextWritingMode)99, RoboTextTextOrientationMixed));

    /* An undefined mode answers the horizontal pair rather than a zero vector — a zero axis would collapse
     * every coordinate a layout produced, which reads as a layout bug rather than as a bad mode. */
    {
        RoboTextWritingAxes unknown = RoboTextWritingModeAxes((RoboTextWritingMode)99);

        RoboCheckEqInt((int)unknown.inlineX, 1);
        RoboCheckEqInt((int)unknown.blockY, 1);
    }
}

/* ------------------------------------------------------------------ */
/* §4 — upright or sideways, per character                             */
/* ------------------------------------------------------------------ */

static void TestVerticalOrientation(void)
{
    /* §13: the ideographic scripts stand up; every other script lies on its side. `Common` and `Inherited`
     * name no script of their own, so they are false and the code-point table decides. */
    RoboCheck(RoboTextVerticalScriptIsUpright(RoboUScriptHan));
    RoboCheck(RoboTextVerticalScriptIsUpright(RoboUScriptHiragana));
    RoboCheck(RoboTextVerticalScriptIsUpright(RoboUScriptKatakana));
    RoboCheck(RoboTextVerticalScriptIsUpright(RoboUScriptBopomofo));
    RoboCheck(RoboTextVerticalScriptIsUpright(RoboUScriptHangul));
    RoboCheck(!RoboTextVerticalScriptIsUpright(RoboUScriptLatin));
    RoboCheck(!RoboTextVerticalScriptIsUpright(RoboUScriptGreek));
    RoboCheck(!RoboTextVerticalScriptIsUpright(RoboUScriptCyrillic));
    RoboCheck(!RoboTextVerticalScriptIsUpright(RoboUScriptArabic));
    RoboCheck(!RoboTextVerticalScriptIsUpright(RoboUScriptCommon));

    /* The code-point half, for the characters whose script is `Common`. */
    RoboCheck(RoboTextVerticalCodePointIsUpright(0x3001u));  /* a CJK comma */
    RoboCheck(RoboTextVerticalCodePointIsUpright(0x4E00u));  /* a Han ideograph by its block */
    RoboCheck(RoboTextVerticalCodePointIsUpright(0xFE10u));  /* the vertical forms block */
    RoboCheck(RoboTextVerticalCodePointIsUpright(0xFF01u));  /* fullwidth */
    RoboCheck(!RoboTextVerticalCodePointIsUpright(0x0041u));
    RoboCheck(!RoboTextVerticalCodePointIsUpright(0x0031u));

    /* And the table that has to beat the script: halfwidth Katakana's script stands up, and UAX #50 rotates
     * the character anyway. Reading this *before* the script is the ordering the case below pins. */
    RoboCheck(RoboTextVerticalCodePointIsRotated(0xFF76u));
    RoboCheck(RoboTextVerticalCodePointIsRotated(0xFF9Eu));
    RoboCheck(!RoboTextVerticalCodePointIsRotated(0x4E00u));
    RoboCheck(!RoboTextVerticalCodePointIsRotated(0x3042u));

    /* The per-character door, `Mixed` asking the three tables in the header's order. */
    RoboCheckEqInt((int)RoboTextVerticalOrientationOf(0x4E00u, RoboUScriptHan, RoboTextTextOrientationMixed),
                   (int)RoboTextGlyphOrientationUpright);
    RoboCheckEqInt((int)RoboTextVerticalOrientationOf(0x3042u, RoboUScriptHiragana,
                                                     RoboTextTextOrientationMixed),
                   (int)RoboTextGlyphOrientationUpright);
    RoboCheckEqInt((int)RoboTextVerticalOrientationOf(0x3001u, RoboUScriptCommon,
                                                     RoboTextTextOrientationMixed),
                   (int)RoboTextGlyphOrientationUpright);
    RoboCheckEqInt((int)RoboTextVerticalOrientationOf(0x0041u, RoboUScriptLatin, RoboTextTextOrientationMixed),
                   (int)RoboTextGlyphOrientationSideways);
    RoboCheckEqInt((int)RoboTextVerticalOrientationOf(0x0031u, RoboUScriptCommon,
                                                     RoboTextTextOrientationMixed),
                   (int)RoboTextGlyphOrientationSideways);
    /* The ordering: upright script, rotated character — the rotated table wins. */
    RoboCheckEqInt((int)RoboTextVerticalOrientationOf(0xFF76u, RoboUScriptKatakana,
                                                     RoboTextTextOrientationMixed),
                   (int)RoboTextGlyphOrientationSideways);
    /* A forced orientation overrides both tables, in both directions. */
    RoboCheckEqInt((int)RoboTextVerticalOrientationOf(0x0041u, RoboUScriptLatin,
                                                     RoboTextTextOrientationUpright),
                   (int)RoboTextGlyphOrientationUpright);
    RoboCheckEqInt((int)RoboTextVerticalOrientationOf(0x4E00u, RoboUScriptHan,
                                                     RoboTextTextOrientationSideways),
                   (int)RoboTextGlyphOrientationSideways);

    /* The rotation a painter composes: zero for upright, a quarter turn for sideways, and never a choice the
     * painter makes. */
    RoboCheck(RoboTextGlyphOrientationRotation(RoboTextGlyphOrientationUpright) == 0.0f);
    RoboCheck(RoboTextGlyphOrientationRotation(RoboTextGlyphOrientationSideways) > 1.57f);
    RoboCheck(RoboTextGlyphOrientationRotation(RoboTextGlyphOrientationSideways) < 1.58f);
    RoboCheck(RoboTextGlyphOrientationIsUpright(RoboTextGlyphOrientationUpright));
    RoboCheck(!RoboTextGlyphOrientationIsUpright(RoboTextGlyphOrientationSideways));
}

/* ------------------------------------------------------------------ */
/* §4 — and in context, where whitespace needs a neighbour              */
/* ------------------------------------------------------------------ */

static void TestVerticalOrientationContext(void)
{
    /* The one contextual rule, and the reason the door exists beside the per-character one. */
    RoboCheckEqInt((int)RoboTextVerticalOrientationInContext(0x0020u, RoboUScriptCommon,
                                                            RoboTextGlyphOrientationUpright,
                                                            RoboTextTextOrientationMixed),
                   (int)RoboTextGlyphOrientationUpright);
    RoboCheckEqInt((int)RoboTextVerticalOrientationInContext(0x0020u, RoboUScriptCommon,
                                                            RoboTextGlyphOrientationSideways,
                                                            RoboTextTextOrientationMixed),
                   (int)RoboTextGlyphOrientationSideways);
    /* An ideographic space is whitespace *and* code-point-upright; whitespace wins, and it follows the
     * neighbour — which is what keeps a space between two ideographs from opening a sideways run. */
    RoboCheck(RoboTextVerticalCodePointIsWhitespace(0x3000u));
    RoboCheckEqInt((int)RoboTextVerticalOrientationInContext(0x3000u, RoboUScriptCommon,
                                                            RoboTextGlyphOrientationUpright,
                                                            RoboTextTextOrientationMixed),
                   (int)RoboTextGlyphOrientationUpright);
    RoboCheckEqInt((int)RoboTextVerticalOrientationInContext(0x0020u, RoboUScriptCommon,
                                                            RoboTextGlyphOrientationSideways,
                                                            RoboTextTextOrientationUpright),
                   (int)RoboTextGlyphOrientationUpright);
    /* A non-whitespace character ignores the neighbour entirely. */
    RoboCheckEqInt((int)RoboTextVerticalOrientationInContext(0x0041u, RoboUScriptLatin,
                                                            RoboTextGlyphOrientationUpright,
                                                            RoboTextTextOrientationMixed),
                   (int)RoboTextGlyphOrientationSideways);

    RoboCheck(RoboTextVerticalCodePointIsWhitespace(0x0020u));
    RoboCheck(RoboTextVerticalCodePointIsWhitespace(0x000Au));
    RoboCheck(RoboTextVerticalCodePointIsWhitespace(0x3000u));
    RoboCheck(!RoboTextVerticalCodePointIsWhitespace(0x0041u));
}

/* ------------------------------------------------------------------ */
/* §15/§16 — orientation is a run boundary                             */
/* ------------------------------------------------------------------ */

static void TestVerticalRunSplit(void)
{
    ConfFace face;
    ConfVerticalGlyphIds ids;
    RoboTextShapeResult *result;
    RoboTextShapeResult *latin;

    VerticalFaceOpen(&face, &ids);

    /* A pure ideographic run is one run and upright. */
    result = VerticalShape(&face, VerticalHanPair, RoboTextWritingVerticalRightToLeft,
                           RoboTextTextOrientationMixed);
    ConfExpectRunCount(result, 1u, "vertical.run_split", VerticalHanPair);
    RoboCheckEqInt((int)RoboTextGlyphRunOrientation(RoboTextShapeResultGlyphRun(result, 0u)), 
                   (int)RoboTextGlyphOrientationUpright);
    RoboCheck(RoboTextGlyphRunIsVertical(RoboTextShapeResultGlyphRun(result, 0u)));
    RoboCheckEqInt((int)RoboTextGlyphRunWritingMode(RoboTextShapeResultGlyphRun(result, 0u)),
                   (int)RoboTextWritingVerticalRightToLeft);
    RoboTextShapeResultDestroy(result);

    /* A pure Latin run inside a vertical column is one run and sideways — the whole of §14, and it is the
     * *same* `GlyphRun` type rather than a rotated-text API. */
    latin = VerticalShape(&face, VerticalLatinAB, RoboTextWritingVerticalRightToLeft,
                          RoboTextTextOrientationMixed);
    ConfExpectRunCount(latin, 1u, "vertical.run_split", VerticalLatinAB);
    RoboCheckEqInt((int)RoboTextGlyphRunOrientation(RoboTextShapeResultGlyphRun(latin, 0u)),
                   (int)RoboTextGlyphOrientationSideways);
    {
        const uint16_t expected[2] = {ids.latinA, ids.latinB};

        ConfExpectGlyphs(latin, expected, 2u, "vertical.run_split", VerticalLatinAB);
    }
    RoboTextShapeResultDestroy(latin);

    /* Mixed: three runs, in logical order, each with its own orientation — Han upright, Latin sideways, Han
     * upright. The orientation is what cuts the middle one out; the script would have cut there anyway, which
     * is why the assertion that matters is the **orientation per run** rather than the count alone. */
    result = VerticalShape(&face, VerticalCjkLatinCjk, RoboTextWritingVerticalRightToLeft,
                           RoboTextTextOrientationMixed);
    ConfExpectRunCount(result, 3u, "vertical.run_split", VerticalCjkLatinCjk);
    RoboCheckEqInt((int)RoboTextGlyphRunOrientation(RoboTextShapeResultGlyphRun(result, 0u)),
                   (int)RoboTextGlyphOrientationUpright);
    RoboCheckEqInt((int)RoboTextGlyphRunOrientation(RoboTextShapeResultGlyphRun(result, 1u)),
                   (int)RoboTextGlyphOrientationSideways);
    RoboCheckEqInt((int)RoboTextGlyphRunOrientation(RoboTextShapeResultGlyphRun(result, 2u)),
                   (int)RoboTextGlyphOrientationUpright);
    /* Every run carries the same writing mode and the same requested orientation: the setting is per call and
     * the *resolved* orientation is what varies. */
    for (size_t run = 0u; run < 3u; run++) {
        const RoboTextGlyphRun *glyphRun = RoboTextShapeResultGlyphRun(result, run);

        RoboCheckEqInt((int)RoboTextGlyphRunWritingMode(glyphRun),
                       (int)RoboTextWritingVerticalRightToLeft);
        RoboCheckEqInt((int)RoboTextGlyphRunTextOrientation(glyphRun), (int)RoboTextTextOrientationMixed);
    }
    RoboTextShapeResultDestroy(result);

    /*
     * Forcing `sideways` removes the orientation cut while the *script* cut stays: three script runs remain,
     * and every one of them is sideways. That is the assertion that keeps the two cuts separable — a run set
     * that had merged the Han runs with the Latin one would have lost the script boundary, not gained an
     * orientation merge.
     */
    result = VerticalShape(&face, VerticalCjkLatinCjk, RoboTextWritingVerticalRightToLeft,
                           RoboTextTextOrientationSideways);
    ConfExpectRunCount(result, 3u, "vertical.run_split", VerticalCjkLatinCjk);
    for (size_t run = 0u; run < 3u; run++) {
        RoboCheckEqInt((int)RoboTextGlyphRunOrientation(RoboTextShapeResultGlyphRun(result, run)),
                       (int)RoboTextGlyphOrientationSideways);
    }
    RoboTextShapeResultDestroy(result);

    /* The halfwidth Katakana case end to end: its script stands up and the run is sideways. */
    result = VerticalShape(&face, VerticalHalfKana, RoboTextWritingVerticalRightToLeft,
                           RoboTextTextOrientationMixed);
    ConfExpectRunCount(result, 1u, "vertical.run_split", VerticalHalfKana);
    RoboCheckEqInt((int)RoboTextGlyphRunOrientation(RoboTextShapeResultGlyphRun(result, 0u)),
                   (int)RoboTextGlyphOrientationSideways);
    RoboTextShapeResultDestroy(result);

    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §9 — vert / vrt2 through the ordinary GSUB engine                    */
/* ------------------------------------------------------------------ */

static void TestVerticalVertFeature(void)
{
    ConfFace face;
    ConfVerticalGlyphIds ids;
    RoboTextShapeResult *result;

    VerticalFaceOpen(&face, &ids);

    /* Vertically, the ideograph reaches the upright form the fixture's `vert` lookup states. The id is read
     * off the fixture, never written as a literal. */
    result = VerticalShape(&face, VerticalHan0, RoboTextWritingVerticalRightToLeft,
                           RoboTextTextOrientationMixed);
    ConfExpectGlyphs(result, &ids.vertHan0, 1u, "vertical.vert_feature", VerticalHan0);
    RoboCheck(RoboTextGlyphRunSubstituted(RoboTextShapeResultGlyphRun(result, 0u)) >= 1u);
    /*
     * The generation mode names the **furthest** stage that ran, and the fixture now carries one GPOS rule (the
     * mark attachment `text.vertical.mark` reads), so the answer is positioning — which is also the statement
     * that the two stages ran in their own order: substitution first, positioning over its product. The id above
     * is what proves the substitution happened; this reads the order it happened in.
     */
    RoboCheckEqInt((int)RoboTextGlyphRunGenerationMode(RoboTextShapeResultGlyphRun(result, 0u)),
                   (int)RoboTextGlyphGenerationGpos);
    RoboTextShapeResultDestroy(result);

    /* The comma too: it is `Common`, so this is the case that says the feature is asked for by the *writing
     * mode* rather than by a script. */
    result = VerticalShape(&face, VerticalComma, RoboTextWritingVerticalRightToLeft,
                           RoboTextTextOrientationMixed);
    ConfExpectGlyphs(result, &ids.vertComma, 1u, "vertical.vert_feature", VerticalComma);
    RoboTextShapeResultDestroy(result);

    /* Horizontally, nothing is asked for and nothing is substituted: the same text keeps the `cmap`'s id. */
    result = VerticalShape(&face, VerticalHan0, RoboTextWritingHorizontal, RoboTextTextOrientationMixed);
    ConfExpectGlyphs(result, &ids.han0, 1u, "vertical.vert_feature", VerticalHan0);
    RoboCheckEqInt((int)RoboTextGlyphRunSubstituted(RoboTextShapeResultGlyphRun(result, 0u)), 0);
    RoboTextShapeResultDestroy(result);

    /* A character the face states no rule for is drawn as itself, vertically or not — the feature is a
     * request, not a rewrite. */
    result = VerticalShape(&face, VerticalKana0, RoboTextWritingVerticalRightToLeft,
                           RoboTextTextOrientationMixed);
    ConfExpectGlyphs(result, &ids.kana0, 1u, "vertical.vert_feature", VerticalKana0);
    RoboTextShapeResultDestroy(result);

    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §7 — the vertical advance comes from the face                       */
/* ------------------------------------------------------------------ */

static void TestVerticalAdvance(void)
{
    ConfFace face;
    ConfVerticalGlyphIds ids;
    RoboTextShapeResult *vertical;
    RoboTextShapeResult *horizontal;
    const RoboTextGlyphRun *run;
    float expected;

    VerticalFaceOpen(&face, &ids);

    vertical = VerticalShape(&face, VerticalHan0, RoboTextWritingVerticalRightToLeft,
                             RoboTextTextOrientationMixed);
    run = RoboTextShapeResultGlyphRun(vertical, 0u);
    expected = VerticalPx(&face, (double)ConfVerticalAdvance(ids.vertHan0));

    /* The pen moved along y and not along x, and the number is the **substitute's** `vmtx` advance — the
     * advance belongs to the glyph the face produced, which is the rule 12.10 established for ligatures and
     * 12.20 reuses for vertical forms. */
    RoboCheck(VerticalFirstGlyph(vertical)->advanceX == 0.0f);
    ConfExpectFloatClose(VerticalFirstGlyph(vertical)->advanceY, expected, "vertical.advance", "advanceY");
    ConfExpectFloatClose(RoboTextGlyphRunAdvanceY(run), expected, "vertical.advance", "run total");
    RoboCheck(RoboTextGlyphRunAdvance(run) == 0.0f);
    RoboCheckEqInt((int)RoboTextGlyphRunVerticalSynthesised(run), 0);
    RoboCheck(RoboTextGlyphRunIsVertical(run));
    RoboTextShapeResultDestroy(vertical);

    /* Two ideographs accumulate: the run's total is the sum of the glyphs' own advances, and each glyph's
     * advance is read from its own substituted id. */
    vertical = VerticalShape(&face, VerticalHanPair, RoboTextWritingVerticalRightToLeft,
                             RoboTextTextOrientationMixed);
    run = RoboTextShapeResultGlyphRun(vertical, 0u);
    /* Both are han0 -> vertHan0 or han1 (no rule), so the sum is over whichever ids the face produced. */
    {
        double sum = 0.0;

        for (size_t g = 0u; g < RoboTextGlyphRunCount(run); g++) {
            sum += (double)RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)g)->advanceY;
        }
        ConfExpectFloatClose(RoboTextGlyphRunAdvanceY(run), (float)sum, "vertical.advance", "sum");
    }
    RoboCheck(RoboTextGlyphRunAdvanceY(run) > expected);
    RoboTextShapeResultDestroy(vertical);

    /* Horizontally the same text advances on x. The two axes are one record with one of them zero, never two
     * records — which is why this compares fields of the same type. */
    horizontal = VerticalShape(&face, VerticalHan0, RoboTextWritingHorizontal,
                               RoboTextTextOrientationMixed);
    run = RoboTextShapeResultGlyphRun(horizontal, 0u);
    ConfExpectFloatClose(VerticalFirstGlyph(horizontal)->advanceX,
                         VerticalPx(&face, (double)ConfVerticalHorizontalAdvance(ids.han0)),
                         "vertical.advance", "advanceX");
    RoboCheck(VerticalFirstGlyph(horizontal)->advanceY == 0.0f);
    RoboCheck(RoboTextGlyphRunAdvanceY(run) == 0.0f);
    RoboCheck(!RoboTextGlyphRunIsVertical(run));
    RoboTextShapeResultDestroy(horizontal);

    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §11 — the origin is a placement offset, not a renderer concern      */
/* ------------------------------------------------------------------ */

static void TestVerticalOrigin(void)
{
    ConfFace face;
    ConfVerticalGlyphIds ids;
    RoboTextShapeResult *result;
    const RoboTextGlyph *glyph;
    float halfWidth;

    VerticalFaceOpen(&face, &ids);

    result = VerticalShape(&face, VerticalHan0, RoboTextWritingVerticalRightToLeft,
                           RoboTextTextOrientationMixed);
    glyph = VerticalFirstGlyph(result);

    /* `offsetY = -vertOriginY`, where the fixture's face has no outline box and therefore answers with the
     * `vmtx` top side bearing — the second branch of the origin rule, and the one a synthetic face exercises. */
    ConfExpectFloatClose(glyph->offsetY, -VerticalPx(&face, (double)ConfVerticalBearing),
                         "vertical.origin", "offsetY");
    /* `offsetX = -vertOriginX`, and the origin centres the glyph on the column: half the advance, with the
     * left side bearing (zero in this fixture) folded in. */
    halfWidth = VerticalPx(&face, (double)ConfVerticalHorizontalAdvance(ids.vertHan0) * 0.5);
    ConfExpectFloatClose(glyph->offsetX, -halfWidth, "vertical.origin", "offsetX");

    /* A horizontal run's placement is untouched: the origin belongs to the vertical path and a horizontal
     * glyph carries no vertical adjustment at all. */
    RoboTextShapeResultDestroy(result);
    result = VerticalShape(&face, VerticalHan0, RoboTextWritingHorizontal, RoboTextTextOrientationMixed);
    glyph = VerticalFirstGlyph(result);
    RoboCheck(glyph->offsetX == 0.0f);
    RoboCheck(glyph->offsetY == 0.0f);
    RoboTextShapeResultDestroy(result);

    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §8 — a face with no vertical metrics is shaped, and counted         */
/* ------------------------------------------------------------------ */

static void TestVerticalSynthesised(void)
{
    ConfFontSpec spec;
    ConfVerticalGlyphIds ids;
    ConfFace face;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;

    memset(&spec, 0, sizeof(spec));
    ConfSpecVertical(&spec, &ids);
    /* The face has vertical *text* but no `vmtx`: the advance is the specification's own fallback, and the
     * phase's rule is that this is a supported configuration rather than a degenerate one. */
    spec.hasVertical = 0;
    ConfFaceOpen(&face, &spec, VerticalSizePx);

    result = VerticalShape(&face, VerticalHanPair, RoboTextWritingVerticalRightToLeft,
                           RoboTextTextOrientationMixed);
    run = RoboTextShapeResultGlyphRun(result, 0u);

    /* Every glyph's advance was synthesised, from the em box (1000 units), and both glyphs report the same
     * number because the fallback is the em's height and not the glyph's. */
    RoboCheckEqInt((int)RoboTextGlyphRunVerticalSynthesised(run), 2);
    ConfExpectFloatClose(VerticalFirstGlyph(result)->advanceY,
                         VerticalPx(&face, (double)1000u), "vertical.synthesised", "advanceY");
    ConfExpectFloatClose(RoboTextGlyphRunAdvanceY(run), 2.0f * VerticalPx(&face, (double)1000u),
                         "vertical.synthesised", "run total");
    /* The substitutions still ran: the vertical path does not depend on the metrics being there. `vert`
     * rewrites the first ideograph and leaves its neighbour — the fixture states one rule, and a substitution
     * is a font's answer about a specific glyph rather than a script-wide rewrite. */
    {
        const uint16_t expected[2] = {ids.vertHan0, ids.han1};

        ConfExpectGlyphs(result, expected, 2u, "vertical.synthesised", VerticalHanPair);
    }

    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §10/§21 — the attachment: a mark is placed against the vertical pen */
/* ------------------------------------------------------------------ */

static void TestVerticalMark(void)
{
    ConfFace face;
    ConfVerticalGlyphIds ids;
    RoboTextShapeResult *vertical;
    RoboTextShapeResult *horizontal;
    const RoboTextGlyphRun *run;
    const RoboTextGlyph *base;
    const RoboTextGlyph *mark;
    uint16_t expected[2] = {0u, 0u};
    float step;

    VerticalFaceOpen(&face, &ids);
    expected[0] = ids.kana0;
    expected[1] = ids.voicing;

    /* A voiced sound mark on the letter it belongs to: the pair a vertical Japanese column really holds. */
    vertical = VerticalShape(&face, VerticalKanaVoicing, RoboTextWritingVerticalRightToLeft,
                             RoboTextTextOrientationMixed);
    run = RoboTextShapeResultGlyphRun(vertical, 0u);
    ConfExpectGlyphCount(vertical, 2u, "vertical.mark", VerticalKanaVoicing);
    ConfExpectGlyphs(vertical, expected, 2u, "vertical.mark", VerticalKanaVoicing);
    base = ConfGlyphAt(vertical, 0u);
    mark = ConfGlyphAt(vertical, 1u);

    /* Both are **upright**: the mark's script is `Inherited`, so it rides the letter's script run and stands
     * up with it. One run, not two — an orientation cut here would split a base from its own mark. */
    ConfExpectRunCount(vertical, 1u, "vertical.mark", VerticalKanaVoicing);

    /*
     * The mark advances nothing on **either** axis — the fixture states zero in `hmtx` and in `vmtx` — so a
     * column with a mark in it is exactly as long as the letter alone. That is the vertical form of the rule
     * 12.18 states for a width, and it is why this case reads `advanceY`, the axis this run moves on.
     */
    RoboCheck(mark->advanceY == 0.0f);
    RoboCheck(mark->advanceX == 0.0f);
    ConfExpectFloatClose(RoboTextGlyphRunAdvanceY(run),
                         VerticalPx(&face, (double)ConfVerticalAdvance(ids.kana0)),
                         "vertical.mark", "the run's advanceY");
    /*
     * And it sits where the font's two anchors put it, minus the pen's step **along the inline axis** — which
     * in a vertical run is `vmtx`'s advance height, the number the pen really moved by. The mark's own vertical
     * origin is folded in exactly as it is for any glyph (`offsetY = -originY + …`), and its origin x is zero
     * here because its horizontal advance is, which is why the x placement is the anchors' difference alone.
     */
    step = VerticalPx(&face, (double)ConfVerticalAdvance(ids.kana0));
    ConfExpectFloatClose(mark->offsetX,
                         VerticalPx(&face,
                                    (double)ConfVerticalBaseAnchorX - (double)ConfVerticalMarkAnchorX),
                         "vertical.mark", "the x placement");
    ConfExpectFloatClose(mark->offsetY,
                         -VerticalPx(&face, (double)ConfVerticalBearing) +
                             VerticalPx(&face,
                                        (double)ConfVerticalBaseAnchorY - (double)ConfVerticalMarkAnchorY) -
                             step,
                         "vertical.mark", "the y placement");
    RoboCheckMsg(step > 0.0f, "the inline step was not a vertical advance");
    /*
     * And the **base** is where its own origin put it: an attachment moves the mark, never the glyph it
     * attaches to. A reader that wrote the placement onto the wrong slot of the pair would still land the mark
     * correctly and fail here.
     */
    ConfExpectFloatClose(base->offsetX,
                         -VerticalPx(&face, (double)ConfVerticalHorizontalAdvance(ids.kana0) * 0.5),
                         "vertical.mark", "the base's x placement");
    ConfExpectFloatClose(base->offsetY, -VerticalPx(&face, (double)ConfVerticalBearing),
                         "vertical.mark", "the base's y placement");
    ConfExpectValid(vertical, "vertical.mark");
    RoboTextShapeResultDestroy(vertical);

    /*
     * The same attachment horizontally, where the inline step is `hmtx`'s advance. One face, one rule, two
     * **different** distances: a reader that asked `hmtx` in both modes would place the vertical mark one cell
     * away from its base, and this pair of readings is what tells the two apart. The cross placement is the
     * anchors' difference in both, because a placement lives in the glyph's own space (x right, y up).
     */
    horizontal = VerticalShape(&face, VerticalKanaVoicing, RoboTextWritingHorizontal,
                               RoboTextTextOrientationMixed);
    ConfExpectRunCount(horizontal, 1u, "vertical.mark", VerticalKanaVoicing);
    mark = ConfGlyphAt(horizontal, 1u);
    ConfExpectFloatClose(mark->offsetX,
                         VerticalPx(&face,
                                    (double)ConfVerticalBaseAnchorX - (double)ConfVerticalMarkAnchorX) -
                             VerticalPx(&face, (double)ConfVerticalHorizontalAdvance(ids.kana0)),
                         "vertical.mark", "the horizontal x placement");
    ConfExpectFloatClose(mark->offsetY,
                         VerticalPx(&face,
                                    (double)ConfVerticalBaseAnchorY - (double)ConfVerticalMarkAnchorY),
                         "vertical.mark", "the horizontal y placement");
    RoboTextShapeResultDestroy(horizontal);

    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §9/§21 — measurement reads the final vertical advances              */
/* ------------------------------------------------------------------ */

static void TestVerticalMeasurement(void)
{
    ConfFace face;
    ConfVerticalGlyphIds ids;
    RoboTextShapeResult *result;
    RoboTextMeasurement measurement;
    double sumX = 0.0;
    double sumY = 0.0;

    VerticalFaceOpen(&face, &ids);

    result = VerticalShape(&face, VerticalCjkLatinCjk, RoboTextWritingVerticalRightToLeft,
                           RoboTextTextOrientationMixed);

    for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
        sumX += (double)RoboTextGlyphRunAdvance(RoboTextShapeResultGlyphRun(result, run));
        sumY += (double)RoboTextGlyphRunAdvanceY(RoboTextShapeResultGlyphRun(result, run));
    }

    /* The authoritative measurement is the engine's, over the finalized run list — not a second opinion. */
    memset(&measurement, 0, sizeof(measurement));
    RoboCheckEqInt((int)RoboTextMeasureGlyphRunList(RoboTextShapeResultGlyphRunList(result), &face.font,
                                                    NULL, &measurement, NULL),
                   (int)RoboOk);
    /* The result's own measurement is that same door's answer. */
    RoboCheckEqInt((int)RoboTextShapeResultMeasure(result, NULL, &measurement, NULL), (int)RoboOk);

    /* A pure vertical run has no horizontal extent and a full vertical one: the two facts live in two fields
     * of one record, which is what makes §22's axes a read rather than a special case. */
    ConfExpectFloatClose(measurement.width, (float)sumX, "vertical.measurement", "width");
    ConfExpectFloatClose(measurement.advanceY, (float)sumY, "vertical.measurement", "advanceY");
    RoboCheck(measurement.width == 0.0f);
    RoboCheck(measurement.advanceY > 0.0f);
    /* No fake: the vertical advance is not the horizontal rectangle rotated, and the record says so. */
    RoboCheck(measurement.width != measurement.advanceY);

    {
        RoboTextMeasureProblem problem = RoboTextMeasureProblemNone;

        RoboCheckEqInt((int)RoboTextMeasureValidate(&measurement, RoboTextShapeResultGlyphRunList(result),
                                                    &problem, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)problem, (int)RoboTextMeasureProblemNone);
    }
    RoboTextShapeResultDestroy(result);

    /* Horizontally the same text has a width and no vertical advance, so the two are one code path. */
    result = VerticalShape(&face, VerticalCjkLatinCjk, RoboTextWritingHorizontal,
                           RoboTextTextOrientationMixed);
    RoboCheckEqInt((int)RoboTextShapeResultMeasure(result, NULL, &measurement, NULL), (int)RoboOk);
    RoboCheck(measurement.width > 0.0f);
    RoboCheck(measurement.advanceY == 0.0f);
    RoboTextShapeResultDestroy(result);

    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §17 — vertical does not bypass bidi                                 */
/* ------------------------------------------------------------------ */

static void TestVerticalBidi(void)
{
    ConfFace face;
    ConfVerticalGlyphIds ids;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;

    VerticalFaceOpen(&face, &ids);

    /* An Arabic letter in a vertical column is still classified from the **Unicode** data: the run is
     * right-to-left, it carries a level, and it is vertical — three facts the phase keeps separable. */
    result = VerticalShape(&face, VerticalArabicBeh, RoboTextWritingVerticalRightToLeft,
                           RoboTextTextOrientationMixed);
    ConfExpectRunCount(result, 1u, "vertical.bidi", VerticalArabicBeh);
    run = RoboTextShapeResultGlyphRun(result, 0u);
    ConfExpectRunClassification(result, 0u, RoboUScriptArabic, RoboTextDirectionRtl, "vertical.bidi",
                                VerticalArabicBeh);
    RoboCheck(RoboTextGlyphRunIsVertical(run));
    RoboCheck(RoboTextGlyphRunBidiLevel(run) >= 1u);
    /* Sideways: Arabic is not an ideographic script, and vertical writing does not change that. */
    RoboCheckEqInt((int)RoboTextGlyphRunOrientation(run), (int)RoboTextGlyphOrientationSideways);
    /* The source is untouched: one glyph, mapped to scalar 0. */
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(run).start, 0);
    RoboTextShapeResultDestroy(result);

    /* A Latin paragraph with an Arabic word inside stays one paragraph with two levels — vertical writing
     * changed the axis, not the ordering. */
    result = VerticalShape(&face, VerticalLatinA VerticalArabicBeh VerticalLatinB,
                           RoboTextWritingVerticalRightToLeft, RoboTextTextOrientationMixed);
    ConfExpectRunCount(result, 3u, "vertical.bidi", VerticalLatinA VerticalArabicBeh VerticalLatinB);
    ConfExpectRunClassification(result, 0u, RoboUScriptLatin, RoboTextDirectionLtr, "vertical.bidi", "A");
    ConfExpectRunClassification(result, 1u, RoboUScriptArabic, RoboTextDirectionRtl, "vertical.bidi", "beh");
    ConfExpectRunClassification(result, 2u, RoboUScriptLatin, RoboTextDirectionLtr, "vertical.bidi", "B");
    /* One paragraph in logical order: the middle run is the one at an odd level, and the two Latin runs around
     * it keep their places. A vertical column reorders nothing, which is what `VisualIsLogical` reports. */
    RoboCheck(RoboTextGlyphRunListVisualIsLogical(RoboTextShapeResultGlyphRunList(result)));
    RoboTextShapeResultDestroy(result);

    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §36 — a writing setting as a dump                                   */
/* ------------------------------------------------------------------ */

static void TestVerticalDiagnostics(void)
{
    char buffer[64];
    size_t length = 0u;
    size_t needed = 0u;

    RoboCheckEqInt((int)RoboTextWritingModeWrite(RoboTextWritingVerticalRightToLeft,
                                                 RoboTextTextOrientationMixed, buffer, sizeof(buffer),
                                                 &length),
                   (int)RoboOk);
    RoboCheckStrEq(buffer, "vertical-rl mixed");
    RoboCheckEqInt((int)length, (int)strlen("vertical-rl mixed"));

    RoboCheckEqInt((int)RoboTextWritingModeWrite(RoboTextWritingVerticalLeftToRight,
                                                 RoboTextTextOrientationUpright, buffer, sizeof(buffer),
                                                 &length),
                   (int)RoboOk);
    RoboCheckStrEq(buffer, "vertical-lr upright");

    /* Complete or absent: a buffer that cannot hold the line is refused and the needed length is still
     * reported, which is the module's rule for every dump. */
    buffer[0] = 'x';
    RoboCheckEqInt((int)RoboTextWritingModeWrite(RoboTextWritingHorizontal, RoboTextTextOrientationMixed,
                                                 buffer, 3u, &needed),
                   (int)RoboNotSupported);
    RoboCheck(buffer[0] == '\0');
    RoboCheck(needed > 3u);

    /* The names, which every other dump in the module shares. */
    RoboCheckStrEq(RoboTextWritingModeName(RoboTextWritingHorizontal), "horizontal-tb");
    RoboCheckStrEq(RoboTextWritingModeName(RoboTextWritingVerticalRightToLeft), "vertical-rl");
    RoboCheckStrEq(RoboTextWritingModeName(RoboTextWritingVerticalLeftToRight), "vertical-lr");
    RoboCheckStrEq(RoboTextTextOrientationName(RoboTextTextOrientationMixed), "mixed");
    RoboCheckStrEq(RoboTextTextOrientationName(RoboTextTextOrientationUpright), "upright");
    RoboCheckStrEq(RoboTextTextOrientationName(RoboTextTextOrientationSideways), "sideways");
    RoboCheckStrEq(RoboTextGlyphOrientationName(RoboTextGlyphOrientationUpright), "upright");
    RoboCheckStrEq(RoboTextGlyphOrientationName(RoboTextGlyphOrientationSideways), "sideways");
}

/* ------------------------------------------------------------------ */
/* §15 — the same run twice                                            */
/* ------------------------------------------------------------------ */

static void TestVerticalDeterminism(void)
{
    ConfFace face;
    ConfVerticalGlyphIds ids;
    RoboTextShapeResult *first;
    RoboTextShapeResult *second;

    VerticalFaceOpen(&face, &ids);

    first = VerticalShape(&face, VerticalCjkLatinCjk, RoboTextWritingVerticalRightToLeft,
                          RoboTextTextOrientationMixed);
    second = VerticalShape(&face, VerticalCjkLatinCjk, RoboTextWritingVerticalRightToLeft,
                           RoboTextTextOrientationMixed);
    /* Ids, sources, clusters, advances — both axes — offsets and orientations, byte for byte. */
    RoboCheckMsg(ConfResultsEqual(first, second), "two vertical shapings of one text disagreed");
    ConfExpectValid(first, "vertical.determinism");
    ConfExpectValid(second, "vertical.determinism");

    RoboTextShapeResultDestroy(first);
    RoboTextShapeResultDestroy(second);
    ConfFaceClose(&face);
}

void TestRegisterTextVertical(void)
{
    TestRegister("text.vertical.writing_axes", TestVerticalWritingAxes);
    TestRegister("text.vertical.orientation", TestVerticalOrientation);
    TestRegister("text.vertical.orientation_context", TestVerticalOrientationContext);
    TestRegister("text.vertical.run_split", TestVerticalRunSplit);
    TestRegister("text.vertical.vert_feature", TestVerticalVertFeature);
    TestRegister("text.vertical.advance", TestVerticalAdvance);
    TestRegister("text.vertical.origin", TestVerticalOrigin);
    TestRegister("text.vertical.synthesised", TestVerticalSynthesised);
    TestRegister("text.vertical.mark", TestVerticalMark);
    TestRegister("text.vertical.measurement", TestVerticalMeasurement);
    TestRegister("text.vertical.bidi", TestVerticalBidi);
    TestRegister("text.vertical.diagnostics", TestVerticalDiagnostics);
    TestRegister("text.vertical.determinism", TestVerticalDeterminism);
}
