/*
 * Arabic shaping — `text.arabic.*` (Phase 12.19 §8–§15).
 *
 * ## What this suite is judging
 *
 * Not "does Arabic look right" — a screenshot cannot say that — but the four layers the phase names, each
 * with its own assertion, over a face whose bytes this repository states (`RoboTextComplexFont.h`):
 *
 *   **joining**         one contextual form per scalar, four questions in one order, with a mark invisible to
 *                      the walk and a joining control that breaks the connection (`text.arabic.joining_*`);
 *   **the forms reach the font**  the four features applied *at the positions the masks name*
 *                      (`text.arabic.forms`), which is the whole reason 12.19 gave the substitution engine a
 *                      positional test;
 *   **provenance**      a `lam` + `alef` ligature is one glyph covering two scalars, and the two scalars are
 *                      still the text (`text.arabic.lam_alef_ligature`);
 *   **the attachments** a mark placed where its base's anchor is (`text.arabic.mark`), two diacritics stacking
 *                      (`text.arabic.mkmk`), and a cursive chain moving the pen by the face's own distance
 *                      (`text.arabic.cursive`).
 *
 * ## Why the inputs are written in logical order and never reversed
 *
 * The text is the *characters* in the order their author wrote them, and every expectation is in that order.
 * Arabic is right-to-left, and the direction those characters are *drawn* in is the bidi stage's answer —
 * `text.arabic.bidi_mixed` reads the level and the classification rather than asserting a reversal here,
 * because a suite that reversed its own input would be testing its own loop.
 *
 * ## The numbers come from the fixture, not from a session
 *
 * Every expected value is derived from a `Cplx…` constant that `RoboTextComplexFont.h` states — an advance, an
 * anchor — so a case reads as arithmetic over the font rather than as a number someone once observed. That is
 * the phase's §31 (a controlled fixture, not a production face) applied to the assertions themselves.
 */

#include "RoboTextComplexFont.h"
#include "RoboTextConformanceSupport.h"

#include <string.h>

#define ArabicSizePx 16.0f

/** The fixture's 1000-unit em at @ref ArabicSizePx. */
static float ArabicUnitsToPx(float units)
{
    return units * ArabicSizePx / 1000.0f;
}

/* `ببب`: three dual-joining letters — the shortest text with an initial, a medial and a final. */
#define ArabicBehBehBeh "\xD8\xA8\xD8\xA8\xD8\xA8"
/* `بب`: two — initial and final. */
#define ArabicBehBeh "\xD8\xA8\xD8\xA8"
/* `ب` alone: no neighbour, so the isolated form. */
#define ArabicBeh "\xD8\xA8"
/* `ب` + fatha + `ب`: the mark is transparent, so the letters still join through it. */
#define ArabicBehFathaBeh "\xD8\xA8\xD9\x8E\xD8\xA8"
/* `ب` + ZWNJ + `ب`: a joining control, which ends a connection and starts nothing. */
#define ArabicBehZwnjBeh "\xD8\xA8\xE2\x80\x8C\xD8\xA8"
/* `ل` + `ا`: the ligature. */
#define ArabicLamAlef "\xD9\x84\xD8\xA7"
/* `ب` + fatha: one mark that must attach. */
#define ArabicBehFatha "\xD8\xA8\xD9\x8E"
/* A fatha on its own: a mark with no base, which must fail safely rather than attach to nothing. */
#define ArabicFatha "\xD9\x8E"
/* `ب` + fatha + shadda: two marks, the second on the first. */
#define ArabicBehFathaShadda "\xD8\xA8\xD9\x8E\xD9\x91"

/** Open the complex fixture over @p face, so no case restates the spec. */
static void ArabicFaceOpen(ConfFace *face)
{
    ConfFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    CplxSpecComplex(&spec);
    ConfFaceOpen(face, &spec, ArabicSizePx);
}

/* ------------------------------------------------------------------ */
/* ±8 — joining: the four forms, and the two scalars invisible to them  */
/* ------------------------------------------------------------------ */

/** Assert one form per scalar of @p text, printing which scalar disagreed. */
static void ArabicExpectForms(const char *text, size_t count, const RoboTextArabicForm *expected)
{
    RoboTextArabicForm forms[8];
    size_t produced = 0u;

    memset(forms, 0, sizeof(forms));
    RoboCheckEqInt((int)RoboTextArabicJoiningResolve(ConfView(text),
                                                     RoboTextRangeFull((RoboTextScalarIndex)count), forms,
                                                     8u, &produced, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)produced, (int)count);
    for (size_t index = 0u; index < count; index++) {
        if (forms[index] != expected[index]) {
            fprintf(stdout, "    Arabic form at scalar %u: expected %s, got %s\n", (unsigned)index,
                    RoboTextArabicFormName(expected[index]), RoboTextArabicFormName(forms[index]));
        }
        RoboCheckMsg(forms[index] == expected[index], "a scalar's contextual form is not the expected one");
    }
}

static void TestArabicJoiningForms(void)
{
    static const RoboTextArabicForm run[3] = {RoboTextArabicFormInitial, RoboTextArabicFormMedial,
                                              RoboTextArabicFormFinal};
    static const RoboTextArabicForm pair[2] = {RoboTextArabicFormInitial, RoboTextArabicFormFinal};
    static const RoboTextArabicForm alone[1] = {RoboTextArabicFormIsolated};

    ArabicExpectForms(ArabicBehBehBeh, 3u, run);
    ArabicExpectForms(ArabicBehBeh, 2u, pair);
    ArabicExpectForms(ArabicBeh, 1u, alone);

    /* The form vocabulary and its feature tags are one fact each, and the tag is what the engine is given. */
    RoboCheckStrEq(RoboTextArabicFormName(RoboTextArabicFormMedial), "medial");
    RoboCheckStrEq(RoboTextArabicFormName(RoboTextArabicFormNone), "none");
    RoboCheckStrEq(RoboTextArabicFormFeature(RoboTextArabicFormIsolated), "isol");
    RoboCheckStrEq(RoboTextArabicFormFeature(RoboTextArabicFormInitial), "init");
    RoboCheckStrEq(RoboTextArabicFormFeature(RoboTextArabicFormMedial), "medi");
    RoboCheckStrEq(RoboTextArabicFormFeature(RoboTextArabicFormFinal), "fina");
    RoboCheck(RoboTextArabicFormIsShaped(RoboTextArabicFormFinal));
    RoboCheck(!RoboTextArabicFormIsShaped(RoboTextArabicFormNone));
    RoboCheckEqInt((int)RoboTextArabicFormMask(RoboTextArabicFormMedial), (int)RoboTextArabicMaskMedial);
    RoboCheckEqInt((int)RoboTextArabicFormMask(RoboTextArabicFormNone), 0);
}

static void TestArabicJoiningTransparency(void)
{
    /* The mark takes no form of its own and does not break the connection between the two letters. */
    static const RoboTextArabicForm throughMark[3] = {RoboTextArabicFormInitial, RoboTextArabicFormNone,
                                                      RoboTextArabicFormFinal};
    /* A joining control is non-joining: it ends one connection and gives none to the other side. */
    static const RoboTextArabicForm throughControl[3] = {RoboTextArabicFormIsolated, RoboTextArabicFormNone,
                                                         RoboTextArabicFormIsolated};

    ArabicExpectForms(ArabicBehFathaBeh, 3u, throughMark);
    ArabicExpectForms(ArabicBehZwnjBeh, 3u, throughControl);
}

static void TestArabicMasks(void)
{
    uint32_t masks[4];
    size_t count = 0u;

    memset(masks, 0, sizeof(masks));
    RoboCheckEqInt((int)RoboTextArabicMasksBuild(ConfView(ArabicBehBehBeh), RoboTextRangeFull(3u), masks, 4u,
                                                 &count, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 3);
    /* One bit per scalar, and the three bits are the three forms: what the engine's positional test reads. */
    RoboCheckEqInt((int)masks[0], (int)RoboTextArabicMaskInitial);
    RoboCheckEqInt((int)masks[1], (int)RoboTextArabicMaskMedial);
    RoboCheckEqInt((int)masks[2], (int)RoboTextArabicMaskFinal);
}

/* ------------------------------------------------------------------ */
/* §11 — the forms reach the font, and only where the masks say        */
/* ------------------------------------------------------------------ */

static void TestArabicForms(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    static const uint16_t run[3] = {CplxInitBeh, CplxMediBeh, CplxFinaBeh};
    static const uint16_t isolated[1] = {CplxIsolBeh};

    ArabicFaceOpen(&face);
    result = ConfShapeDefault(&face, ArabicBehBehBeh);
    /*
     * Three scalars, three glyphs, and the **ids** are the three forms. The ids are what makes this decisive:
     * the fixture gives each (letter, form) its own glyph, so a mask that let `medi` apply everywhere would
     * show as three medial ids rather than as a plausible-looking word.
     */
    ConfExpectGlyphs(result, run, 3u, "text.arabic.forms", ArabicBehBehBeh);
    /* One run, and it is Arabic: script segmentation and shaping agree about what this text is. */
    ConfExpectRunCount(result, 1u, "text.arabic.forms", ArabicBehBehBeh);
    RoboCheckEqInt((int)RoboTextGlyphRunScript(RoboTextShapeResultGlyphRun(result, 0)),
                   (int)RoboUScriptArabic);
    ConfExpectValid(result, "text.arabic.forms");
    RoboTextShapeResultDestroy(result);

    /* A single letter is `isol`, not "no substitution". */
    result = ConfShapeDefault(&face, ArabicBeh);
    ConfExpectGlyphs(result, isolated, 1u, "text.arabic.isolated", ArabicBeh);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

static void TestArabicFormDisabled(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    RoboTextShapeOptions options;
    /*
     * `init` off and nothing else: the first letter keeps its base id and the other two still shape. The
     * case exists because "a caller's setting wins over the strategy's default" is the property that makes
     * `font-feature-settings: "init" 0` mean something, and a plan that ignored it would look identical in
     * every other case here.
     */
    static const uint16_t expected[3] = {CplxBeh, CplxMediBeh, CplxFinaBeh};

    ArabicFaceOpen(&face);
    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "init", RoboTextFeatureOff, 0),
                   (int)RoboOk);
    result = ConfShape(&face, ArabicBehBehBeh, &options);
    ConfExpectGlyphs(result, expected, 3u, "text.arabic.feature_overridden", ArabicBehBehBeh);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §12 — the ligature, and the provenance it must keep                 */
/* ------------------------------------------------------------------ */

static void TestArabicLamAlefLigature(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    static const uint16_t expected[1] = {CplxLamAlef};
    /* The cluster the ligature covers, and its one glyph. */
    static const uint32_t clusters[3] = {0u, 2u, 1u};

    ArabicFaceOpen(&face);
    result = ConfShapeDefault(&face, ArabicLamAlef);
    ConfExpectGlyphs(result, expected, 1u, "text.arabic.lam_alef", ArabicLamAlef);
    /* One glyph, two scalars, and the two scalars are still the text. */
    {
        const RoboTextGlyph *glyph = ConfGlyphAt(result, 0u);

        RoboCheckMsg(glyph && glyph->ligatureComponents >= 2u, "the ligature kept no provenance");
        RoboCheckMsg(glyph && glyph->source.start == 0u && glyph->source.end == 2u,
                     "the ligature's source is not both scalars");
    }
    ConfExpectClusters(result, 0u, clusters, 1u, "text.arabic.lam_alef", ArabicLamAlef);
    /* The advance is the ligature's own, not the sum of the two letters' — which is what §12 asks. */
    ConfExpectAdvance(result, ArabicUnitsToPx((float)CplxLamAlefAdvance), "text.arabic.lam_alef",
                      ArabicLamAlef);
    ConfExpectValid(result, "text.arabic.lam_alef");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §13/§14 — the attachments, read off the anchors the fixture states   */
/* ------------------------------------------------------------------ */

static void TestArabicMark(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    const RoboTextGlyph *mark;

    ArabicFaceOpen(&face);
    result = ConfShapeDefault(&face, ArabicBehFatha);
    /* Two glyphs: the letter and its mark. */
    ConfExpectGlyphCount(result, 2u, "text.arabic.mark", ArabicBehFatha);
    /* A lone letter takes no connection, so the base is the isolated form — the form the mark anchors on. */
    RoboCheckEqInt((int)ConfGlyphAt(result, 0u)->glyphId, (int)CplxIsolBeh);
    mark = ConfGlyphAt(result, 1u);
    RoboCheckEqInt((int)mark->glyphId, (int)CplxFatha);
    /* The mark advances nothing: an Arabic width is the letters'. */
    ConfExpectFloatClose(mark->advanceX, 0.0f, "text.arabic.mark", "the mark's advance");
    ConfExpectAdvance(result, ArabicUnitsToPx((float)CplxFormAdvance), "text.arabic.mark", ArabicBehFatha);
    /*
     * And it is where the two anchors put it, which is the whole of mark attachment: the engine's answer is
     * `baseAnchor - (pen distance + markAnchor)`, and every number in it is one the fixture stated.
     */
    ConfExpectFloatClose(mark->offsetX,
                         ArabicUnitsToPx((float)CplxFathaBaseXUnits - (float)CplxFormAdvance -
                                         (float)CplxFathaMarkXUnits),
                         "text.arabic.mark", "the mark's x placement");
    ConfExpectFloatClose(mark->offsetY,
                         ArabicUnitsToPx((float)CplxFathaBaseYUnits - (float)CplxFathaMarkYUnits),
                         "text.arabic.mark", "the mark's y placement");
    ConfExpectValid(result, "text.arabic.mark");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

static void TestArabicMarkMark(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    const RoboTextGlyph *fatha;
    const RoboTextGlyph *shadda;

    ArabicFaceOpen(&face);
    result = ConfShapeDefault(&face, ArabicBehFathaShadda);
    ConfExpectGlyphCount(result, 3u, "text.arabic.mkmk", ArabicBehFathaShadda);
    fatha = ConfGlyphAt(result, 1u);
    shadda = ConfGlyphAt(result, 2u);
    RoboCheckEqInt((int)fatha->glyphId, (int)CplxFatha);
    RoboCheckEqInt((int)shadda->glyphId, (int)CplxShadda);
    /* The second mark stacks on the first: its anchor is the first mark's, which is what `mkmk` is for. */
    ConfExpectFloatClose(shadda->offsetX,
                         fatha->offsetX +
                             ArabicUnitsToPx((float)CplxShaddaOnFathaXUnits - (float)CplxShaddaMarkXUnits),
                         "text.arabic.mkmk", "the shadda's x placement");
    ConfExpectFloatClose(shadda->offsetY,
                         fatha->offsetY +
                             ArabicUnitsToPx((float)CplxShaddaOnFathaYUnits - (float)CplxShaddaMarkYUnits),
                         "text.arabic.mkmk", "the shadda's y placement");
    RoboCheckMsg(shadda->offsetY > fatha->offsetY, "the second mark did not stack above the first");
    ConfExpectValid(result, "text.arabic.mkmk");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

static void TestArabicCursive(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    const RoboTextGlyph *first;
    const RoboTextGlyph *second;
    static const uint16_t expected[2] = {CplxInitBeh, CplxFinaBeh};

    ArabicFaceOpen(&face);
    result = ConfShapeDefault(&face, ArabicBehBeh);
    ConfExpectGlyphs(result, expected, 2u, "text.arabic.cursive", ArabicBehBeh);
    first = ConfGlyphAt(result, 0u);
    second = ConfGlyphAt(result, 1u);
    /*
     * The chain, in the format's own two statements: the pen after the first letter lands on the anchor it
     * leaves by, and the second letter's receiving anchor is placed where that pen is. The first statement is
     * an advance and the second a placement, which is why the engine needs the face's own advance at all — a
     * distance GPOS does not carry.
     *
     * Which anchor is the outgoing one is the format's direction convention, and this text is **Arabic**: the
     * run's resolved direction is right-to-left, so the first letter leaves by its *entry* anchor (the join on
     * its right) and the second receives by its *exit* one. The expectation names those two, and the two
     * numbers are the fixture's own anchors — never a value observed from a run.
     */
    ConfExpectFloatClose(first->advanceAdjustX,
                         ArabicUnitsToPx((float)CplxBehEntryXUnits - (float)CplxFormAdvance),
                         "text.arabic.cursive", "the first letter's cursive advance correction");
    ConfExpectFloatClose(second->offsetX, ArabicUnitsToPx(-(float)CplxBehExitXUnits),
                         "text.arabic.cursive", "the second letter's receiving placement");
    /*
     * And the run's width is the two letters' corrected advances, which is what the pen will walk: the first
     * letter now advances exactly as far as the join it hands over at (`CplxBehEntryXUnits`), and the second
     * keeps its own width.
     */
    ConfExpectAdvance(result, ArabicUnitsToPx((float)CplxBehEntryXUnits + (float)CplxFormAdvance),
                      "text.arabic.cursive", ArabicBehBeh);
    ConfExpectValid(result, "text.arabic.cursive");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §6/§28 — the clusters, and where a split is unsafe                  */
/* ------------------------------------------------------------------ */

/** Resolve @p text's complex clusters, asserting the call and the scalar count they cover. */
static size_t ArabicClusters(const char *text, RoboTextComplexCluster *out, size_t capacity)
{
    RoboTextView view = ConfView(text);
    RoboTextScalarIndex scalars = 0u;
    size_t count = 0u;

    RoboCheckEqInt((int)RoboTextViewScalarCount(view, &scalars, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextComplexClustersResolve(view, RoboTextRangeFull(scalars), out, capacity, &count,
                                                       NULL),
                   (int)RoboOk);
    return count;
}

static void TestArabicClusters(void)
{
    RoboTextComplexCluster clusters[8];
    size_t count;

    memset(clusters, 0, sizeof(clusters));
    /* A joining sequence is one cluster: the three letters connect, so nothing may cut them apart. */
    count = ArabicClusters(ArabicBehBehBeh, clusters, 8u);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)clusters[0].source.start, 0);
    RoboCheckEqInt((int)clusters[0].source.end, 3);
    RoboCheckEqInt((int)clusters[0].kind, (int)RoboTextComplexClusterJoining);
    RoboCheckEqInt((int)clusters[0].strategy, (int)RoboTextComplexStrategyArabic);
    RoboCheckMsg(clusters[0].unsafeToBreak != 0, "a joining sequence did not claim its own break");

    /* A mark belongs to the cluster it follows, and the letters still join through it. */
    count = ArabicClusters(ArabicBehFathaBeh, clusters, 8u);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)clusters[0].source.end, 3);

    /*
     * `ZWNJ` is the boundary itself: it is non-joining, so it ends one sequence and the next letter begins
     * another — which is exactly what the character is for.
     */
    count = ArabicClusters(ArabicBehZwnjBeh, clusters, 8u);
    RoboCheckEqInt((int)count, 3);
    RoboCheckEqInt((int)clusters[1].kind, (int)RoboTextComplexClusterControl);

    /* And a numeric answer is what a line breaker reads: inside a cluster never, at its start yes. */
    count = ArabicClusters(ArabicBehBehBeh, clusters, 8u);
    RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, 0u), 1);
    RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, 1u), 0);
    RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, 3u), 0);
}

/* ------------------------------------------------------------------ */
/* §11/§30 — the plan and its diagnostic                               */
/* ------------------------------------------------------------------ */

static void TestArabicPlan(void)
{
    RoboTextComplexPlan plan;
    char text[1024];
    size_t length = 0u;

    RoboTextComplexPlanInit(&plan);
    RoboCheckEqInt((int)RoboTextComplexPlanResolve(RoboUScriptArabic, NULL, &plan, NULL), (int)RoboOk);
    RoboCheckEqInt((int)plan.strategy, (int)RoboTextComplexStrategyArabic);
    RoboCheckEqInt((int)plan.stageCount, 3);
    RoboCheckMsg(RoboTextComplexPlanHasStages(&plan) != 0, "the Arabic plan has no stages");
    RoboCheckStrEq(RoboTextComplexPlanStage(&plan, 0u)->name, "arabic.common");
    RoboCheckStrEq(RoboTextComplexPlanStage(&plan, 1u)->name, "arabic.forms");
    RoboCheckStrEq(RoboTextComplexPlanStage(&plan, 2u)->name, "arabic.ligatures");
    /* The script tag is the font's own `arab`, which is what the plan resolver asks the ScriptList for. */
    RoboCheckEqInt((int)RoboTextComplexPlanScriptTag(&plan), (int)RoboTextOtTagOf("arab"));
    /* The forms stage is the masked one: one featured bit per contextual form. */
    RoboCheckEqInt((int)plan.stages[1].features[0].mask, (int)RoboTextArabicMaskIsolated);
    RoboCheckEqInt((int)plan.stages[1].features[3].mask, (int)RoboTextArabicMaskFinal);
    /* A common script has no plan at all: the strategy answers, and the stages are the Latin path. */
    RoboCheckEqInt((int)RoboTextComplexPlanResolve(RoboUScriptLatin, NULL, &plan, NULL), (int)RoboOk);
    RoboCheckEqInt((int)plan.strategy, (int)RoboTextComplexStrategyNone);
    RoboCheckEqInt((int)RoboTextComplexPlanHasStages(&plan), 0);

    memset(text, 0, sizeof(text));
    RoboCheckEqInt((int)RoboTextComplexPlanWrite(&plan, text, sizeof(text), &length), (int)RoboOk);
    RoboCheckMsg(length > 0u, "the plan dump wrote nothing");
    RoboCheckStrEq(RoboTextComplexStrategyName(RoboTextComplexStrategyArabic), "arabic");
    RoboCheckStrEq(RoboTextComplexStrategyName(RoboTextComplexStrategyIndic), "indic");
    RoboCheckStrEq(RoboTextComplexStrategyName(RoboTextComplexStrategyNone), "none");
}

static void TestArabicDiagnostics(void)
{
    RoboTextArabicForm forms[4];
    char text[1024];
    size_t count = 0u;
    size_t length = 0u;

    memset(forms, 0, sizeof(forms));
    /* Zero forms is a complete record of zero forms, not nothing: the header and the count are written. */
    RoboCheckEqInt((int)RoboTextArabicJoiningWrite(ConfView(ArabicBehBehBeh), RoboTextRangeFull(3u), forms, 0u,
                                                   text, sizeof(text), &length),
                   (int)RoboOk);
    RoboCheckMsg(length > 0u && strstr(text, "forms 0") != NULL, "an empty joining dump lost its header");
    RoboCheckEqInt((int)RoboTextArabicJoiningResolve(ConfView(ArabicBehBehBeh), RoboTextRangeFull(3u), forms, 4u,
                                                     &count, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 3);
    memset(text, 0, sizeof(text));
    RoboCheckEqInt((int)RoboTextArabicJoiningWrite(ConfView(ArabicBehBehBeh), RoboTextRangeFull(3u), forms, count,
                                                   text, sizeof(text), &length),
                   (int)RoboOk);
    RoboCheckMsg(strstr(text, "form=initial") != NULL, "the joining dump lost the initial form");
    RoboCheckMsg(strstr(text, "form=medial") != NULL, "the joining dump lost the medial form");
    RoboCheckMsg(strstr(text, "form=final") != NULL, "the joining dump lost the final form");

    /* A dump that cannot fit is refused rather than truncated: complete-or-absent, as §30 asks. */
    RoboCheckEqInt((int)RoboTextArabicJoiningWrite(ConfView(ArabicBehBehBeh), RoboTextRangeFull(3u), forms, count,
                                                   text, 4u, &length),
                   (int)RoboNotSupported);
}

/* ------------------------------------------------------------------ */
/* §15 — direction is bidi's answer, read and never recomputed         */
/* ------------------------------------------------------------------ */

static void TestArabicBidiMixed(void)
{
    ConfFace face;
    RoboTextShapeResult *result;

    ArabicFaceOpen(&face);
    result = ConfShapeDefault(&face, "A" ArabicBehBehBeh);
    /* Two scripts, two runs. */
    ConfExpectRunCount(result, 2u, "text.arabic.bidi_mixed", "A" ArabicBehBehBeh);
    ConfExpectRunClassification(result, 0u, RoboUScriptLatin, RoboTextDirectionLtr,
                                "text.arabic.bidi_mixed", "A" ArabicBehBehBeh);
    ConfExpectRunClassification(result, 1u, RoboUScriptArabic, RoboTextDirectionRtl,
                                "text.arabic.bidi_mixed", "A" ArabicBehBehBeh);
    /*
     * The Arabic run is still in **logical** order — the order the author wrote it — because the visual order
     * is 12.8's array beside the list and not a reversal here. That is §15's rule with a number on it.
     */
    {
        const RoboTextGlyphRun *arabic = RoboTextShapeResultGlyphRun(result, 1u);
        static const uint16_t expected[3] = {CplxInitBeh, CplxMediBeh, CplxFinaBeh};

        RoboCheckEqInt((int)RoboTextGlyphRunCount(arabic), 3);
        for (size_t index = 0u; index < 3u; index++) {
            const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(arabic, (RoboTextGlyphIndex)index);

            RoboCheckMsg(glyph && glyph->glyphId == expected[index],
                         "the Arabic run is not in the order the author wrote it");
        }
    }
    ConfExpectValid(result, "text.arabic.bidi_mixed");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §25 — font fallback does not split a joining sequence                */
/* ------------------------------------------------------------------ */

/**
 * A joining sequence is one cluster and one script run, and the selector's unit is the script run, so the face
 * that draws `ببب` draws all three letters or none of them.
 *
 * As in the Indic suite the assertion is a count first — `runs == scriptRuns` is precisely "the font added no
 * boundary of its own", and a per-scalar walk would raise it — and then the cluster resolved over the run,
 * which must be the **whole** joining cluster rather than a letter, a letter and a letter.
 *
 * The chain holds the Arabic face and the primary does not cover Arabic, so the run that matters is answered
 * by the fallback: this is the fallback path, not the primary path wearing its name.
 */
static void TestArabicFallbackNoSplit(void)
{
    ConfFontSpec primarySpec;
    ConfFace primaryFace;
    ConfFace face;
    RoboTextFontCandidate candidate;
    RoboTextFontFallback chain;
    RoboTextFontSelector *selector = NULL;
    RoboTextAnalysis *analysis = NULL;
    RoboTextAnalysisStats stats;
    RoboTextShapeOptions options;
    const char *text = "A" ArabicBehBehBeh;

    /* The primary is the European face: it covers the Latin letter and no Arabic scalar. */
    memset(&primarySpec, 0, sizeof(primarySpec));
    ConfSpecEuropean(&primarySpec);
    ConfFaceOpen(&primaryFace, &primarySpec, ArabicSizePx);
    ArabicFaceOpen(&face);
    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    memset(&candidate, 0, sizeof(candidate));
    memset(&chain, 0, sizeof(chain));
    candidate.family = "Complex Fixture";
    candidate.face = face.face;
    chain.entries = &candidate;
    chain.count = 1u;
    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&selector, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, &primaryFace.font, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetFallback(selector, &chain, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextAnalysisCreate(ConfView(text), selector, &options, &analysis, NULL),
                   (int)RoboOk);
    memset(&stats, 0, sizeof(stats));
    RoboCheckEqInt((int)RoboTextAnalysisGetStats(analysis, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.runs, (int)stats.scriptRuns);
    RoboCheckEqInt((int)stats.runs, 2);
    /* The Latin run is the primary's, the joining sequence the chain's. */
    RoboCheckEqInt((int)stats.primaryRuns, 1);
    RoboCheckEqInt((int)stats.fallbackRuns, 1);
    RoboCheckEqInt((int)stats.unresolvedRuns, 0);

    /* The Arabic run is the second, and its one cluster is all three letters. */
    {
        const RoboTextAnalysisRun *run = RoboTextAnalysisRunAt(analysis, 1u);
        RoboTextComplexCluster clusters[8];
        size_t count = 0u;

        RoboCheck(run != NULL);
        RoboCheckMsg(run->font.face != NULL, "a served Arabic run named no face");
        memset(clusters, 0, sizeof(clusters));
        RoboCheckEqInt((int)RoboTextComplexClustersResolve(ConfView(text), run->range, clusters, 8u, &count,
                                                           NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)count, 1);
        RoboCheckEqInt((int)clusters[0].source.start, (int)run->range.start);
        RoboCheckEqInt((int)clusters[0].source.end, (int)run->range.end);
        RoboCheckEqInt((int)clusters[0].kind, (int)RoboTextComplexClusterJoining);
    }
    RoboTextAnalysisDestroy(analysis);
    RoboTextFontSelectorDestroy(selector);
    ConfFaceClose(&face);
    ConfFaceClose(&primaryFace);
}

static void TestArabicDeterminism(void)
{
    ConfFace face;
    RoboTextShapeResult *first;
    RoboTextShapeResult *second;

    ArabicFaceOpen(&face);
    first = ConfShapeDefault(&face, ArabicBehFathaShadda);
    second = ConfShapeDefault(&face, ArabicBehFathaShadda);
    RoboCheckMsg(ConfResultsEqual(first, second), "two runs of one input disagreed");
    RoboTextShapeResultDestroy(first);
    RoboTextShapeResultDestroy(second);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §13 — malformed and unsupported sequences fail safely               */
/* ------------------------------------------------------------------ */

static void TestArabicStrayMark(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    RoboTextComplexCluster clusters[4];
    size_t count;

    ArabicFaceOpen(&face);
    /*
     * A mark with no base. Nothing may attach to nothing, so the mark is left where it is, still advances
     * nothing, and the run stays a valid record — which is acceptance §13: an unsupported sequence is answered
     * rather than crashed on, and the buffer is not corrupted by a lookup that found no base.
     */
    result = ConfShapeDefault(&face, ArabicFatha);
    ConfExpectGlyphCount(result, 1u, "text.arabic.stray_mark", ArabicFatha);
    RoboCheckEqInt((int)ConfGlyphAt(result, 0u)->glyphId, (int)CplxFatha);
    ConfExpectFloatClose(ConfGlyphAt(result, 0u)->advanceX, 0.0f, "text.arabic.stray_mark",
                         "a base-less mark's advance");
    ConfExpectValid(result, "text.arabic.stray_mark");
    RoboTextShapeResultDestroy(result);

    /* And it is a cluster of its own, so a line breaker may cut on either side of it. */
    count = ArabicClusters(ArabicFatha, clusters, 4u);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)clusters[0].source.end, 1);
    ConfFaceClose(&face);
}

static void TestArabicNoLayoutTables(void)
{
    ConfFace face;
    ConfFontSpec spec;
    RoboTextShapeResult *result;
    /* The `cmap`'s own answer, one glyph per scalar: a plan with no table to run against changes nothing. */
    static const uint16_t expected[3] = {CplxBeh, CplxBeh, CplxBeh};

    memset(&spec, 0, sizeof(spec));
    CplxSpecComplex(&spec);
    spec.omitGsub = 1;
    spec.omitGpos = 1;
    ConfFaceOpen(&face, &spec, ArabicSizePx);

    result = ConfShapeDefault(&face, ArabicBehBehBeh);
    ConfExpectGlyphs(result, expected, 3u, "text.arabic.no_layout_tables", ArabicBehBehBeh);
    ConfExpectRunCount(result, 1u, "text.arabic.no_layout_tables", ArabicBehBehBeh);
    /* A face with no shaping is not a face with no text: the letters are still Arabic and still measure. */
    ConfExpectRunClassification(result, 0u, RoboUScriptArabic, RoboTextDirectionRtl,
                                "text.arabic.no_layout_tables", ArabicBehBehBeh);
    ConfExpectAdvance(result, ArabicUnitsToPx(3.0f * (float)CplxBaseLetterAdvance),
                      "text.arabic.no_layout_tables", ArabicBehBehBeh);
    ConfExpectValid(result, "text.arabic.no_layout_tables");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

void TestRegisterTextArabic(void)
{
    TestRegister("text.arabic.joining_forms", TestArabicJoiningForms);
    TestRegister("text.arabic.joining_transparency", TestArabicJoiningTransparency);
    TestRegister("text.arabic.masks", TestArabicMasks);
    TestRegister("text.arabic.forms", TestArabicForms);
    TestRegister("text.arabic.feature_overridden", TestArabicFormDisabled);
    TestRegister("text.arabic.lam_alef_ligature", TestArabicLamAlefLigature);
    TestRegister("text.arabic.mark", TestArabicMark);
    TestRegister("text.arabic.mkmk", TestArabicMarkMark);
    TestRegister("text.arabic.cursive", TestArabicCursive);
    TestRegister("text.arabic.clusters", TestArabicClusters);
    TestRegister("text.arabic.plan", TestArabicPlan);
    TestRegister("text.arabic.diagnostics", TestArabicDiagnostics);
    TestRegister("text.arabic.bidi_mixed", TestArabicBidiMixed);
    TestRegister("text.arabic.stray_mark", TestArabicStrayMark);
    TestRegister("text.arabic.no_layout_tables", TestArabicNoLayoutTables);
    TestRegister("text.arabic.fallback_no_split", TestArabicFallbackNoSplit);
    TestRegister("text.arabic.determinism", TestArabicDeterminism);
}
