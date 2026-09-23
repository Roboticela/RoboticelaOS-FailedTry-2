/*
 * Greek, Cyrillic and mixed-script shaping conformance (Phase 12.18) — `text.shaping.scripts.*`.
 *
 * The phase's §23, §24 and §27. The three claims this file makes about European scripts other than Latin are
 * the three a Latin-only implementation gets wrong:
 *
 *   1. **a Greek or Cyrillic letter is not a Latin letter with a different code point.** It shapes through
 *      the same `cmap`, but the *run* it lands in carries its own script, and §27 asks that the suite prove
 *      the script was classified rather than assumed. `Greek` and `Cyrillic` are checked per run.
 *   2. **a mixed line is more than one run.** `"Latin Ελλαδα Кириллица"` is three script runs because the
 *      resolver says so, and the case asserts the boundaries and the order rather than a glyph count that
 *      would be satisfied by one run of thirty glyphs.
 *   3. **a diacritic belongs to its base's script.** A combining mark is `Inherited`, so a naive
 *      implementation puts `α` + U+0301 in two runs — or in a face that covers neither. The mark case asserts
 *      one run, one cluster, and the mark's own zero advance.
 */

#include "RoboTextConformanceSupport.h"

#include <string.h>

#define ScriptsSizePx 16.0f

/** A font unit at @ref ScriptsSizePx over the fixture's 1000-unit em. */
static float ScriptsUnitsToPx(float units)
{
    return units * ScriptsSizePx / 1000.0f;
}

/* ------------------------------------------------------------------ */
/* The blocks, one character at a time (§23, §24)                      */
/* ------------------------------------------------------------------ */

/** Walk @p first..@p last (skipping @p skip) and assert one shaped character per code point. */
static void ScriptsWalkBlock(ConfFace *face,
                             const char *label,
                             uint32_t first,
                             uint32_t last,
                             uint32_t skip,
                             RoboTextScript script)
{
    for (uint32_t cp = first; cp <= last; cp++) {
        char text[8];
        uint16_t want;
        RoboTextShapeResult *result;

        if (cp == skip) {
            continue;
        }
        want = ConfGlyphOf(cp);
        if (want == 0u) {
            continue;
        }
        (void)ConfUtf8(cp, text);
        result = ConfShapeDefault(face, text);
        ConfExpectGlyphs(result, &want, 1u, label, text);
        ConfExpectAdvance(result, ScriptsUnitsToPx(ConfAdvanceOf(want)), label, text);
        ConfExpectRunClassification(result, 0u, script, RoboTextDirectionLtr, label, text);
        ConfExpectValid(result, label);
        RoboCheckEqInt((int)ConfTotalMissing(result), 0);
        RoboTextShapeResultDestroy(result);
    }
}

/** Every Greek capital and small letter the fixture's `cmap` carries. */
static void TestShapingGreekAlphabet(void)
{
    ConfFontSpec spec;
    ConfFace face;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, ScriptsSizePx);
    ScriptsWalkBlock(&face, "text.shaping.scripts.greek_upper", 0x0391u, 0x03A9u, 0x03A2u,
                     RoboUScriptGreek);
    ScriptsWalkBlock(&face, "text.shaping.scripts.greek_lower", 0x03B1u, 0x03C9u, 0u, RoboUScriptGreek);
    ConfFaceClose(&face);
}

/** Every Cyrillic capital and small letter. */
static void TestShapingCyrillicAlphabet(void)
{
    ConfFontSpec spec;
    ConfFace face;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, ScriptsSizePx);
    ScriptsWalkBlock(&face, "text.shaping.scripts.cyrillic_upper", 0x0410u, 0x042Fu, 0u,
                     RoboUScriptCyrillic);
    ScriptsWalkBlock(&face, "text.shaping.scripts.cyrillic_lower", 0x0430u, 0x044Fu, 0u,
                     RoboUScriptCyrillic);
    ConfFaceClose(&face);
}

/**
 * Representative words (§23, §24), every scalar of which the fixture maps.
 *
 * The words are chosen so that the case cannot be satisfied by a `cmap` that happens to map a code point
 * range: each letter is asserted to be exactly the id the canonical list gives it, in order.
 */
static void TestShapingScriptWords(void)
{
    static const struct {
        const char *label;
        const char *text;   /* Αλφα, Ελλαδα, Привет, Кириллица */
        RoboTextScript script;
    } kWords[] = {{"text.shaping.scripts.greek_word", "\xCE\x91\xCE\xBB\xCF\x86\xCE\xB1", RoboUScriptGreek},
                  {"text.shaping.scripts.greek_word2", "\xCE\x95\xCE\xBB\xCE\xBB\xCE\xB1\xCE\xB4\xCE\xB1",
                   RoboUScriptGreek},
                  {"text.shaping.scripts.cyrillic_word", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82",
                   RoboUScriptCyrillic},
                  {"text.shaping.scripts.cyrillic_word2",
                   "\xD0\x9A\xD0\xB8\xD1\x80\xD0\xB8\xD0\xBB\xD0\xBB\xD0\xB8\xD1\x86\xD0\xB0",
                   RoboUScriptCyrillic}};
    ConfFontSpec spec;
    ConfFace face;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, ScriptsSizePx);
    for (size_t w = 0u; w < sizeof(kWords) / sizeof(kWords[0]); w++) {
        RoboTextShapeResult *result = ConfShapeDefault(&face, kWords[w].text);
        RoboTextView view = ConfView(kWords[w].text);
        RoboTextScalarIndex scalars = 0u;

        RoboCheckEqInt((int)RoboTextViewScalarCount(view, &scalars, NULL), (int)RoboOk);
        ConfExpectRunCount(result, 1u, kWords[w].label, kWords[w].text);
        ConfExpectRunClassification(result, 0u, kWords[w].script, RoboTextDirectionLtr, kWords[w].label,
                                    kWords[w].text);
        ConfExpectGlyphCount(result, (size_t)scalars, kWords[w].label, kWords[w].text);
        ConfExpectValid(result, kWords[w].label);
        RoboCheckEqInt((int)ConfTotalMissing(result), 0);
        for (size_t i = 0u; i < (size_t)scalars; i++) {
            uint32_t cp = 0u;
            const RoboTextGlyph *glyph = ConfGlyphAt(result, i);

            RoboCheckEqInt((int)RoboTextViewScalarAt(view, (RoboTextScalarIndex)i, &cp, NULL), (int)RoboOk);
            RoboCheck(glyph != NULL);
            RoboCheckEqInt((int)glyph->glyphId, (int)ConfGlyphOf(cp));
        }
        RoboTextShapeResultDestroy(result);
    }
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §23 — Greek diacritics                                              */
/* ------------------------------------------------------------------ */

/** `α` + U+0301: the mark keeps the base's script, its cluster, and contributes no advance. */
static void TestShapingGreekMark(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;
    static const char kMarked[] = "\xCE\xB1\xCC\x81"; /* α + combining acute */
    static const uint32_t kClusters[] = {0u, 2u, 2u};
    const RoboTextGlyph *base;
    const RoboTextGlyph *mark;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, ScriptsSizePx);
    result = ConfShapeDefault(&face, kMarked);
    ConfExpectRunCount(result, 1u, "text.shaping.scripts.greek_mark", kMarked);
    ConfExpectRunClassification(result, 0u, RoboUScriptGreek, RoboTextDirectionLtr,
                                "text.shaping.scripts.greek_mark", kMarked);
    ConfExpectGlyphCount(result, 2u, "text.shaping.scripts.greek_mark", kMarked);
    ConfExpectClusters(result, 0u, kClusters, 1u, "text.shaping.scripts.greek_mark", kMarked);
    base = ConfGlyphAt(result, 0u);
    mark = ConfGlyphAt(result, 1u);
    RoboCheck(base != NULL && mark != NULL);
    RoboCheckEqInt((int)base->glyphId, (int)ConfGlyphOf(0x03B1u));
    RoboCheckEqInt((int)mark->glyphId, (int)ConfGlyphOf(0x0301u));
    /* The mark carries no advance because `hmtx` says so — not because a heuristic zeroed it. */
    RoboCheckEqInt((int)mark->advanceUnits, 0);
    ConfExpectAdvance(result, ScriptsUnitsToPx(ConfAdvanceOf(ConfGlyphOf(0x03B1u))),
                      "text.shaping.scripts.greek_mark", kMarked);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §27 — script runs in a mixed line                                   */
/* ------------------------------------------------------------------ */

/** Latin, Greek and Cyrillic in one line are three runs, in logical order, with their own scripts. */
static void TestShapingMixedScriptRuns(void)
{
    /* "Latin Ελλαδα Кириллица" */
    static const char kText[] = "Latin \xCE\x95\xCE\xBB\xCE\xBB\xCE\xB1\xCE\xB4\xCE\xB1 "
                                "\xD0\x9A\xD0\xB8\xD1\x80\xD0\xB8\xD0\xBB\xD0\xBB\xD0\xB8\xD1\x86\xD0\xB0";
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, ScriptsSizePx);
    result = ConfShapeDefault(&face, kText);
    ConfExpectRunCount(result, 3u, "text.shaping.scripts.mixed_runs", kText);
    ConfExpectRunClassification(result, 0u, RoboUScriptLatin, RoboTextDirectionLtr,
                                "text.shaping.scripts.mixed_runs", kText);
    ConfExpectRunClassification(result, 1u, RoboUScriptGreek, RoboTextDirectionLtr,
                                "text.shaping.scripts.mixed_runs", kText);
    ConfExpectRunClassification(result, 2u, RoboUScriptCyrillic, RoboTextDirectionLtr,
                                "text.shaping.scripts.mixed_runs", kText);
    /* The runs tile the text in logical order and none is empty: the invariant the phase's §27 asks for. */
    {
        RoboTextScalarIndex at = 0u;

        for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
            RoboTextRange range = RoboTextGlyphRunSourceRange(RoboTextShapeResultGlyphRun(result, run));

            RoboCheckEqInt((int)range.start, (int)at);
            RoboCheck(range.end > range.start);
            at = range.end;
        }
        {
            RoboTextView view = ConfView(kText);
            RoboTextScalarIndex scalars = 0u;

            RoboCheckEqInt((int)RoboTextViewScalarCount(view, &scalars, NULL), (int)RoboOk);
            RoboCheckEqInt((int)at, (int)scalars);
        }
    }
    ConfExpectValid(result, "text.shaping.scripts.mixed_runs");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/** A script hint decides a line that is all punctuation, which is what §27's hint rule is for. */
static void TestShapingScriptHint(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, ScriptsSizePx);
    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    options.scriptHint = RoboUScriptGreek;
    result = ConfShape(&face, "...", &options);
    ConfExpectRunCount(result, 1u, "text.shaping.scripts.hint", "...");
    ConfExpectRunClassification(result, 0u, RoboUScriptGreek, RoboTextDirectionLtr,
                                "text.shaping.scripts.hint", "...");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

void TestRegisterTextShapingScripts(void)
{
    TestRegister("text.shaping.scripts.greek_upper", TestShapingGreekAlphabet);
    TestRegister("text.shaping.scripts.cyrillic_upper", TestShapingCyrillicAlphabet);
    TestRegister("text.shaping.scripts.words", TestShapingScriptWords);
    TestRegister("text.shaping.scripts.greek_mark", TestShapingGreekMark);
    TestRegister("text.shaping.scripts.mixed_runs", TestShapingMixedScriptRuns);
    TestRegister("text.shaping.scripts.hint", TestShapingScriptHint);
}
