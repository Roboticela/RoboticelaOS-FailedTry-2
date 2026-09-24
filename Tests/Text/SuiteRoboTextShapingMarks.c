/*
 * Combining-mark conformance (Phase 12.18) — `text.shaping.marks.*`.
 *
 * The phase's §19 and §20, over a fixture whose five combining marks carry a **zero advance** in `hmtx` and a
 * stated placement under the `mark` feature (`xPlacement` 40, `yPlacement` 120 font units — 0.64 and 1.92
 * device pixels at 16px). Those two numbers are what make the cases decisive:
 *
 *   - "a combining mark does not add width" is checked against a mark whose `hmtx` entry really is zero, so a
 *     run that measures wider than its base is the *engine's* defect and not the font's;
 *   - "a mark is placed by the font" is checked against a stated offset, so a mark drawn at the pen origin
 *     (the classic symptom) fails rather than looking plausible.
 *
 * ## The one honest limitation, named rather than implied
 *
 * The placement is a GPOS **single adjustment** (lookup type 1) under `mark`, not a mark-to-base lookup
 * (type 4). Phase 12.11 executes types 1, 2 and 9 and deliberately skips 3-8, so a type-4 table here would be
 * counted unsupported and would place nothing. What these cases therefore prove is "the mark draws at the
 * offset the font states and advances nothing"; and `text.shaping.marks.attachment_claimed` is the flag 12.18
 * asserted as **0** and 12.19 flipped — a mark may now be placed against a named base glyph's anchor, which
 * `text.gpos.mark_to_base` and `text.gpos.mark_to_mark` prove over a fixture that states those lookups. The
 * mark's own placement here stays the fixture's **type-1** adjustment: this suite's font states no mark
 * attachment, so it is one value record and nothing else, which is exactly the distinction the two cases are
 * for.
 */

#include "RoboTextConformanceSupport.h"

#include <string.h>

#define MarksSizePx 16.0f

/** A font unit at @ref MarksSizePx over the fixture's 1000-unit em. */
static float MarksUnitsToPx(float units)
{
    return units * MarksSizePx / 1000.0f;
}

/** The fixture's stated mark placement, in font units. Kept beside the assertions that use them. */
#define MarkPlacementXUnits 40
#define MarkPlacementYUnits 120

/** `e` + U+0301, `a` + U+0300 and `o` + U+0308, as UTF-8 literals. */
#define MarksEAcute "e\xCC\x81"
#define MarksAGrave "a\xCC\x80"
#define MarksODiaeresis "o\xCC\x88"
#define MarksNTilde "n\xCC\x83"

/* ------------------------------------------------------------------ */
/* §19 — the mark is retained, and only the base wears the width         */
/* ------------------------------------------------------------------ */

/** A base and its mark are one cluster and two glyphs, and the mark adds no width. */
static void TestShapingMarkCluster(void)
{
    static const struct {
        const char *label;
        const char *text;
        char base;
        uint32_t mark;
    } kCases[] = {{"text.shaping.marks.e_acute", MarksEAcute, 'e', 0x0301u},
                  {"text.shaping.marks.a_grave", MarksAGrave, 'a', 0x0300u},
                  {"text.shaping.marks.o_diaeresis", MarksODiaeresis, 'o', 0x0308u},
                  {"text.shaping.marks.n_tilde", MarksNTilde, 'n', 0x0303u}};
    ConfFontSpec spec;
    ConfFace face;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, MarksSizePx);
    for (size_t c = 0u; c < sizeof(kCases) / sizeof(kCases[0]); c++) {
        RoboTextShapeResult *result = ConfShapeDefault(&face, kCases[c].text);
        char baseText[2];
        RoboTextShapeResult *baseResult;
        static const uint32_t kClusters[] = {0u, 2u, 2u};

        baseText[0] = kCases[c].base;
        baseText[1] = '\0';
        ConfExpectRunCount(result, 1u, kCases[c].label, kCases[c].text);
        ConfExpectGlyphCount(result, 2u, kCases[c].label, kCases[c].text);
        /* One cluster covering both scalars and holding both glyphs: the mark may not be separated from its
         * base, which is what selection, hit testing and a line breaker all depend on. */
        ConfExpectClusters(result, 0u, kClusters, 1u, kCases[c].label, kCases[c].text);
        {
            const RoboTextGlyph *base = ConfGlyphAt(result, 0u);
            const RoboTextGlyph *mark = ConfGlyphAt(result, 1u);

            RoboCheck(base != NULL && mark != NULL);
            RoboCheckEqInt((int)base->glyphId, (int)ConfGlyphOf((uint32_t)kCases[c].base));
            RoboCheckEqInt((int)mark->glyphId, (int)ConfGlyphOf(kCases[c].mark));
            RoboCheckEqInt((int)mark->advanceUnits, 0);
            RoboCheckEqInt((int)mark->source.start, 1);
            RoboCheckEqInt((int)mark->source.end, 2);
            /* The mark flag comes from the *character's* general category, which is why a glyph carries its
             * code point: after substitution the text is no longer at hand to ask. */
            RoboCheck((mark->flags & RoboTextGlyphFlagMark) != 0u);
            RoboCheck((mark->flags & RoboTextGlyphFlagClusterContinuation) != 0u);
        }
        /* The width is the base's alone — the case §15 exists for. */
        baseResult = ConfShapeDefault(&face, baseText);
        ConfExpectFloatClose(ConfTotalAdvance(result), ConfTotalAdvance(baseResult), kCases[c].label,
                             "a mark added width");
        RoboTextShapeResultDestroy(baseResult);
        ConfExpectValid(result, kCases[c].label);
        RoboTextShapeResultDestroy(result);
    }
    ConfFaceClose(&face);
}

/**
 * §19's refusal, stated: a precomposed letter and its decomposition are **not** required to be the same
 * glyphs.
 *
 * A font may compose, decompose, or do neither, and which one it does is the font's business. What the
 * phase asks for is that the build does not *claim* canonical equivalence it has not established, so the
 * case asserts the difference it can see — one glyph versus two — and that both shapes are valid, measure
 * positive, and map back to the same two scalars.
 */
static void TestShapingMarkPrecomposed(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *precomposed;
    RoboTextShapeResult *decomposed;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, MarksSizePx);
    precomposed = ConfShapeDefault(&face, "\xC3\xA9"); /* é U+00E9 */
    decomposed = ConfShapeDefault(&face, MarksEAcute);
    ConfExpectGlyphCount(precomposed, 1u, "text.shaping.marks.precomposed", "\\xC3\\xA9");
    ConfExpectGlyphCount(decomposed, 2u, "text.shaping.marks.precomposed", MarksEAcute);
    ConfExpectValid(precomposed, "text.shaping.marks.precomposed");
    ConfExpectValid(decomposed, "text.shaping.marks.precomposed");
    RoboCheck(ConfTotalAdvance(precomposed) > 0.0f);
    RoboCheck(ConfTotalAdvance(decomposed) > 0.0f);
    /* Both cover two scalars; the precomposed one with one glyph, the decomposed one with two. */
    RoboCheckEqInt((int)RoboTextGlyphRunScalarCount(RoboTextShapeResultGlyphRun(precomposed, 0u)), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunScalarCount(RoboTextShapeResultGlyphRun(decomposed, 0u)), 2);
    RoboTextShapeResultDestroy(precomposed);
    RoboTextShapeResultDestroy(decomposed);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §20 — placement and multiple marks                                   */
/* ------------------------------------------------------------------ */

/** The mark draws at the offset the font states, and the offset is a *placement*, not an advance. */
static void TestShapingMarkPlacement(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, MarksSizePx);
    result = ConfShapeDefault(&face, MarksEAcute);
    {
        const RoboTextGlyph *mark = ConfGlyphAt(result, 1u);

        RoboCheck(mark != NULL);
        ConfExpectFloatClose(mark->offsetX, MarksUnitsToPx((float)MarkPlacementXUnits),
                             "text.shaping.marks.placement", "the mark's x placement");
        ConfExpectFloatClose(mark->offsetY, MarksUnitsToPx((float)MarkPlacementYUnits),
                             "text.shaping.marks.placement", "the mark's y placement");
        ConfExpectFloatClose(mark->offsetAdjustX, mark->offsetX, "text.shaping.marks.placement",
                             "the font's own part of the x placement");
        ConfExpectFloatClose(mark->advanceAdjustX, 0.0f, "text.shaping.marks.placement",
                             "the mark's advance adjustment");
        /* Positioned even though the advance adjustment is zero: the font wrote a value record for it. */
        RoboCheck(mark->positioned != 0);
    }
    /* The base is untouched: a mark placement is not a line-wide adjustment. */
    {
        const RoboTextGlyph *base = ConfGlyphAt(result, 0u);

        RoboCheck(base != NULL);
        RoboCheckEqInt(base->positioned, 0);
        ConfExpectFloatClose(base->offsetX, 0.0f, "text.shaping.marks.placement", "the base's x offset");
        ConfExpectFloatClose(base->offsetY, 0.0f, "text.shaping.marks.placement", "the base's y offset");
    }
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/** Two marks on one base: one cluster, three glyphs, two placements, and still only the base's width. */
static void TestShapingMultipleMarks(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;
    RoboTextShapeResult *base;
    static const uint32_t kClusters[] = {0u, 3u, 3u};

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, MarksSizePx);
    result = ConfShapeDefault(&face, "e\xCC\x81\xCC\x88"); /* e + acute + diaeresis */
    ConfExpectGlyphCount(result, 3u, "text.shaping.marks.multiple", "e + 2 marks");
    ConfExpectClusters(result, 0u, kClusters, 1u, "text.shaping.marks.multiple", "e + 2 marks");
    {
        size_t placed = 0u;

        for (size_t i = 1u; i < 3u; i++) {
            const RoboTextGlyph *mark = ConfGlyphAt(result, i);

            RoboCheck(mark != NULL);
            RoboCheckEqInt((int)mark->advanceUnits, 0);
            RoboCheck(mark->positioned != 0);
            if (mark->offsetY != 0.0f) {
                placed++;
            }
        }
        RoboCheckEqInt((int)placed, 2);
        RoboCheckEqInt((int)ConfTotalPositioned(result), 2);
    }
    base = ConfShapeDefault(&face, "e");
    ConfExpectFloatClose(ConfTotalAdvance(result), ConfTotalAdvance(base), "text.shaping.marks.multiple",
                         "two marks added width");
    RoboTextShapeResultDestroy(base);
    ConfExpectValid(result, "text.shaping.marks.multiple");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §20 — marks do not break the script rule                             */
/* ------------------------------------------------------------------ */

/** A Latin mark on a Greek base — `Inherited` takes the base's script, not its own. */
static void TestShapingMarkScript(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, MarksSizePx);
    result = ConfShapeDefault(&face, "e\xCC\x81 a\xCC\x80");
    ConfExpectRunCount(result, 1u, "text.shaping.marks.script", "e+acute a+grave");
    ConfExpectRunClassification(result, 0u, RoboUScriptLatin, RoboTextDirectionLtr,
                                "text.shaping.marks.script", "e+acute a+grave");
    /* Three clusters — a base and its mark, the space, and the second base and its mark. */
    RoboCheckEqInt((int)RoboTextGlyphRunClusterCount(RoboTextShapeResultGlyphRun(result, 0u)), 3);
    {
        const RoboTextGlyphCluster *second =
            RoboTextGlyphRunCluster(RoboTextShapeResultGlyphRun(result, 0u), 2u);

        RoboCheck(second != NULL);
        RoboCheckEqInt((int)second->source.start, 3);
        RoboCheckEqInt((int)second->source.end, 5);
        RoboCheckEqInt((int)(second->glyphEnd - second->glyphStart), 2);
    }
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/**
 * §20's absence, in the module's own vocabulary: mark **attachment** is not implemented.
 *
 * `RoboTextCapabilities.complexScripts` is the field, and it is `0` because a mark placed relative to a
 * named base glyph's anchor — GPOS lookup type 4, and with it every script whose marks attach to a base's
 * anchors — is Phase 12.19's work. Asserting the flag keeps the limitation a *stated* one, so a later goal
 * that adds attachment has to flip this case in the same change rather than quietly making it false.
 */
static void TestShapingMarkAttachmentClaimed(void)
{
    RoboTextCapabilities capabilities;

    RoboCheckEqInt((int)RoboTextGetCapabilities(&capabilities), (int)RoboOk);
    /*
     * The flag 12.18 asserted as **0** and 12.19 flips, in the same change as the engine: the attachments
     * (GPOS types 3–6) now run, so a mark can be placed against a named base glyph's anchor and not only by a
     * value record. The proof is `text.gpos.mark_to_base` / `text.gpos.mark_to_mark` / `text.gpos.cursive`
     * over a fixture that states those lookups, not this case — this one exists so the flip is *pinned* rather
     * than left to a reader to notice.
     */
    RoboCheckEqInt(capabilities.complexScripts, 1);
    RoboCheckEqInt(capabilities.gposAttachments, 1);
    /* What was claimed before and still is: the mark itself, its cluster, its flags, and the value records. */
    RoboCheckEqInt(capabilities.combiningMarks, 1);
    RoboCheckEqInt(capabilities.glyphClusters, 1);
    RoboCheckEqInt(capabilities.glyphFlags, 1);
    RoboCheckEqInt(capabilities.positioning, 1);
    /* And the two things a mark needs that are absent for a different reason: vertical metrics, and a cache
     * that would make the "no second positioning" rule untestable. */
    RoboCheckEqInt(capabilities.shapingCache, 0);
    RoboCheckEqInt(capabilities.verticalText, 0);
}

void TestRegisterTextShapingMarks(void)
{
    TestRegister("text.shaping.marks.cluster", TestShapingMarkCluster);
    TestRegister("text.shaping.marks.precomposed", TestShapingMarkPrecomposed);
    TestRegister("text.shaping.marks.placement", TestShapingMarkPlacement);
    TestRegister("text.shaping.marks.multiple", TestShapingMultipleMarks);
    TestRegister("text.shaping.marks.script", TestShapingMarkScript);
    TestRegister("text.shaping.marks.attachment_claimed", TestShapingMarkAttachmentClaimed);
}
