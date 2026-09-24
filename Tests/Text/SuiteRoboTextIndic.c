/*
 * Indic shaping — `text.indic.*` (Phase 12.19 §16–§22).
 *
 * ## The four answers this suite insists on separately
 *
 * "Devanagari looks right" is not a testable claim, and it is not one claim either: a syllable is a **cluster**
 * of scalars, its **base** is one of them, its **shapes** come from the font's features applied at the right
 * positions, and some of its elements are **reordered**. Each of those is its own door here:
 *
 *   **the clusters**   what groups, where the base is, which of the flags are set, and which boundary a line
 *                      breaker may cut at (`text.indic.clusters`, `text.indic.base`, `text.indic.safe_boundary`);
 *   **the masks**      one word per scalar, which is how "`half` here and `cjct` there" is stated
 *                      (`text.indic.masks`);
 *   **the shapes**     the glyph ids the fixture states, read off the bytes: the half form, the conjunct, the
 *                      reph, the pre-base and post-base vowel signs (`text.indic.{half_form,conjunct,reph,
 *                      pre_base_matra,post_base_matra}`);
 *   **the plan**       the strategy, the script tag and the three stages
 *                      (`text.indic.script_plan`, `text.indic.stages`).
 *
 * ## The order things come out in
 *
 * Source order is *not* shaping order and shaping order is *not* visual order, and the suite keeps all three
 * distinguishable rather than asserting one and hoping. The clearest case is the reph: `र` `्` `म` is written
 * reph-first and is *drawn* reph-last, so the glyph sequence is `[म, र्]` while the text is still `र्म` —
 * `text.indic.reph` reads the ids, and every case reads the **source** of each glyph rather than a position,
 * which is §7's rule with a number on it.
 *
 * ## What is not here, and why
 *
 * No below-base or post-base *form*: the fixture states no `blwf`/`pstf` rule, so those masks are asserted as
 * masks (`text.indic.masks` names them) rather than passed off as shaped output. No nukta, no `akhn`: the
 * engine runs those lookups and this face declares none, and `Documentation/Runtime/Text/ComplexScripts.md`
 * §"what the fixture does not state" repeats the list beside the engine's own limits.
 */

#include "RoboTextComplexFont.h"
#include "RoboTextConformanceSupport.h"

#include <string.h>

#define IndicSizePx 16.0f

/** The fixture's 1000-unit em at @ref IndicSizePx. */
static float IndicUnitsToPx(float units)
{
    return units * IndicSizePx / 1000.0f;
}

/* The scalars, spelled in UTF-8 so a reader can see which letter each case is about. */
#define IndicKa "\xE0\xA4\x95"      /* U+0915 DEVANAGARI LETTER KA */
#define IndicSsa "\xE0\xA4\xB7"     /* U+0937 DEVANAGARI LETTER SSA */
#define IndicRa "\xE0\xA4\xB0"      /* U+0930 DEVANAGARI LETTER RA */
#define IndicMa "\xE0\xA4\xAE"      /* U+092E DEVANAGARI LETTER MA */
#define IndicVirama "\xE0\xA5\x8D"  /* U+094D DEVANAGARI SIGN VIRAMA */
#define IndicIMatra "\xE0\xA4\xBF"  /* U+093F DEVANAGARI VOWEL SIGN I (pre-base) */
#define IndicAaMatra "\xE0\xA4\xBE" /* U+093E DEVANAGARI VOWEL SIGN AA (post-base) */

/* The texts. One per orthographic shape the phase's §21 asks for. */
#define IndicKaAlone IndicKa
#define IndicKaVirama IndicKa IndicVirama
#define IndicKaViramaSsa IndicKa IndicVirama IndicSsa
#define IndicRaViramaMa IndicRa IndicVirama IndicMa
#define IndicKaViramaSpaceKa IndicKa IndicVirama " " IndicKa
#define IndicKaIMatra IndicKa IndicIMatra
#define IndicKaAaMatra IndicKa IndicAaMatra
#define IndicKaRaViramaMa IndicKa IndicRa IndicVirama IndicMa

/* `ببب` — three Arabic letters, for the case that needs a third script in one string. */
#define IndicArabicBehBehBeh "\xD8\xA8\xD8\xA8\xD8\xA8"

/** Open the complex fixture, so no case restates the spec. */
static void IndicFaceOpen(ConfFace *face)
{
    ConfFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    CplxSpecComplex(&spec);
    ConfFaceOpen(face, &spec, IndicSizePx);
}

/* ------------------------------------------------------------------ */
/* §18 — the syllables, the base and the flags                          */
/* ------------------------------------------------------------------ */

/** Resolve @p text's clusters through the module's own door, asserting the call. */
static size_t IndicClusters(const char *text, RoboTextComplexCluster *out, size_t capacity)
{
    RoboTextView view = ConfView(text);
    RoboTextScalarIndex scalars = 0u;
    size_t count = 0u;

    RoboCheckEqInt((int)RoboTextViewScalarCount(view, &scalars, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextIndicClusters(view, RoboTextRangeFull(scalars), out, capacity, &count, NULL),
                   (int)RoboOk);
    return count;
}

static void TestIndicClusters(void)
{
    RoboTextComplexCluster clusters[8];
    size_t count;

    memset(clusters, 0, sizeof(clusters));

    /* A lone consonant is a syllable of one, based on itself. */
    count = IndicClusters(IndicKaAlone, clusters, 8u);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)clusters[0].kind, (int)RoboTextComplexClusterSyllable);
    RoboCheckEqInt((int)clusters[0].baseIndex, 0);
    RoboCheckEqInt((int)clusters[0].hasVirama, 0);
    RoboCheckEqInt((int)clusters[0].source.end, 1);

    /*
     * The conjunct: `क` `्` `ष` is **one** syllable whose base is the *second* consonant — what the conjunct
     * is built on — with the virama joining them.
     */
    count = IndicClusters(IndicKaViramaSsa, clusters, 8u);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)clusters[0].kind, (int)RoboTextComplexClusterSyllable);
    RoboCheckEqInt((int)clusters[0].source.end, 3);
    RoboCheckEqInt((int)clusters[0].baseIndex, 2);
    RoboCheckMsg(clusters[0].hasVirama != 0, "the conjunct lost its virama");
    RoboCheckEqInt((int)clusters[0].hasReph, 0);
    RoboCheckEqInt((int)clusters[0].hasPreBase, 0);

    /* A trailing virama attaches **forward**: the half form belongs to whatever comes next. */
    count = IndicClusters(IndicKaVirama, clusters, 8u);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)clusters[0].source.end, 2);
    RoboCheckMsg(clusters[0].unsafeToBreak != 0, "a half form did not claim its own break");

    /* A Latin letter and a space are their own simple clusters, so a mixed range needs no branch here. */
    count = IndicClusters(IndicKaViramaSpaceKa, clusters, 8u);
    RoboCheckEqInt((int)count, 3);
    RoboCheckEqInt((int)clusters[1].kind, (int)RoboTextComplexClusterSimple);
    RoboCheckEqInt((int)clusters[1].source.start, 2);
    RoboCheckEqInt((int)clusters[1].source.end, 3);
}

static void TestIndicBase(void)
{
    RoboTextComplexCluster clusters[8];
    size_t count;

    memset(clusters, 0, sizeof(clusters));
    /*
     * `क` + `र्म`: an inherent vowel ends the consonant sequence, so `क` is its own syllable and the `ra` +
     * virama + `ma` is the next one — based on the `ma`, with the reph detected at the syllable's start.
     */
    count = IndicClusters(IndicKaRaViramaMa, clusters, 8u);
    RoboCheckEqInt((int)count, 2);
    RoboCheckEqInt((int)clusters[0].source.start, 0);
    RoboCheckEqInt((int)clusters[0].source.end, 1);
    RoboCheckEqInt((int)clusters[0].baseIndex, 0);
    RoboCheckEqInt((int)clusters[1].source.start, 1);
    RoboCheckEqInt((int)clusters[1].source.end, 4);
    RoboCheckEqInt((int)clusters[1].baseIndex, 2);
    RoboCheckMsg(clusters[1].hasReph != 0, "the mid-word reph was not detected");
}

static void TestIndicMasks(void)
{
    uint32_t masks[8];
    size_t count = 0u;

    memset(masks, 0, sizeof(masks));
    RoboCheckEqInt((int)RoboTextIndicMasksBuild(ConfView(IndicKaViramaSsa), RoboTextRangeFull(3u), masks, 8u,
                                                &count, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 3);
    /*
     * The first consonant halves (`half`), begins a conjunct (`cjct`) and — because it is the syllable's
     * first of several — is where `pref` matches. The virama is not a consonant and carries nothing. The base
     * is below the conjunct and part of it.
     */
    RoboCheckEqInt((int)masks[0], (int)(RoboTextIndicMaskConsonant | RoboTextIndicMaskHalf |
                                        RoboTextIndicMaskPre | RoboTextIndicMaskConjunct));
    RoboCheckEqInt((int)masks[1], 0);
    RoboCheckEqInt((int)masks[2], (int)(RoboTextIndicMaskConsonant | RoboTextIndicMaskBelow |
                                        RoboTextIndicMaskConjunct));

    /* The reph carries its own bit **on the `ra`**, because that is the scalar the lookup is tried at. */
    memset(masks, 0, sizeof(masks));
    RoboCheckEqInt((int)RoboTextIndicMasksBuild(ConfView(IndicRaViramaMa), RoboTextRangeFull(3u), masks, 8u,
                                                &count, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)masks[0], (int)(RoboTextIndicMaskConsonant | RoboTextIndicMaskReph |
                                        RoboTextIndicMaskHalf | RoboTextIndicMaskPre |
                                        RoboTextIndicMaskConjunct));
}

static void TestIndicSafeBoundary(void)
{
    RoboTextComplexCluster clusters[8];
    size_t count = IndicClusters(IndicKaViramaSpaceKa, clusters, 8u);
    RoboTextScalarIndex scalars = 0u;

    RoboCheckEqInt((int)RoboTextViewScalarCount(ConfView(IndicKaViramaSpaceKa), &scalars, NULL), (int)RoboOk);
    RoboCheckEqInt((int)count, 3);
    /* The start of the text is a boundary, and inside the half-formed syllable is not. */
    RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, 0u), 1);
    RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, 1u), 0);
    /*
     * And the space's start is **not** one, even though it begins a cluster: the virama before it attaches
     * forward, which is the half of the rule a cluster cannot state about itself.
     */
    RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, 2u), 0);
    RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, scalars), 0);
}

/* ------------------------------------------------------------------ */
/* §20/§21 — the shapes, read off the bytes the fixture states          */
/* ------------------------------------------------------------------ */

static void TestIndicHalfForm(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    static const uint16_t expected[1] = {CplxHalfKa};

    IndicFaceOpen(&face);
    /* A consonant the virama follows halves, and the half form's width is its own. */
    result = ConfShapeDefault(&face, IndicKaVirama);
    ConfExpectGlyphs(result, expected, 1u, "text.indic.half_form", IndicKaVirama);
    ConfExpectAdvance(result, IndicUnitsToPx((float)CplxHalfKaAdvance), "text.indic.half_form",
                      IndicKaVirama);
    ConfExpectRunCount(result, 1u, "text.indic.half_form", IndicKaVirama);
    RoboCheckEqInt((int)RoboTextGlyphRunScript(RoboTextShapeResultGlyphRun(result, 0u)),
                   (int)RoboUScriptDevanagari);
    ConfExpectValid(result, "text.indic.half_form");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

static void TestIndicConjunct(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    static const uint16_t expected[1] = {CplxKsa};
    static const uint32_t clusters[3] = {0u, 3u, 1u};

    IndicFaceOpen(&face);
    /*
     * `क` `्` `ष` is **one glyph** covering three scalars: the half form was made by `half` and the conjunct
     * by `cjct`, and the proof that both ran is the id — a face that halves but never joins would leave the
     * `ssa` beside the half form, and a face that joins without halving would leave the virama's glyph in the
     * run.
     */
    result = ConfShapeDefault(&face, IndicKaViramaSsa);
    ConfExpectGlyphs(result, expected, 1u, "text.indic.conjunct", IndicKaViramaSsa);
    ConfExpectClusters(result, 0u, clusters, 1u, "text.indic.conjunct", IndicKaViramaSsa);
    {
        const RoboTextGlyph *glyph = ConfGlyphAt(result, 0u);

        RoboCheckMsg(glyph && glyph->ligatureComponents >= 2u,
                     "the conjunct kept no provenance for its components");
        RoboCheckMsg(glyph && glyph->source.start == 0u && glyph->source.end == 3u,
                     "the conjunct's source is not all three scalars");
    }
    /* The width is the conjunct's own, which is neither component's — §37's rule in one number. */
    ConfExpectAdvance(result, IndicUnitsToPx((float)CplxKsaAdvance), "text.indic.conjunct", IndicKaViramaSsa);
    ConfExpectValid(result, "text.indic.conjunct");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

static void TestIndicReph(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    /* Written reph-first, drawn reph-last: the `rphf` product follows the base it belongs to. */
    static const uint16_t expected[2] = {CplxMa, CplxReph};

    IndicFaceOpen(&face);
    result = ConfShapeDefault(&face, IndicRaViramaMa);
    ConfExpectGlyphs(result, expected, 2u, "text.indic.reph", IndicRaViramaMa);
    /*
     * The two glyphs carry the sources they were made from — the base's own scalar and the reph's pair — so a
     * caret still reads the text in the order the author wrote it (§7), even though the glyph order is the
     * drawn one.
     */
    RoboCheckMsg(ConfGlyphAt(result, 0u)->source.start == 2u, "the base glyph came from the wrong scalar");
    RoboCheckMsg(ConfGlyphAt(result, 1u)->source.start == 0u && ConfGlyphAt(result, 1u)->source.end == 2u,
                 "the reph's source is not the `ra` and its virama");
    /* The reph advances nothing: it is drawn above the base, not beside it. */
    ConfExpectFloatClose(ConfGlyphAt(result, 1u)->advanceX, 0.0f, "text.indic.reph", "the reph's advance");
    ConfExpectAdvance(result, IndicUnitsToPx((float)CplxBaseLetterAdvance), "text.indic.reph", IndicRaViramaMa);
    ConfExpectValid(result, "text.indic.reph");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

static void TestIndicPreBaseMatra(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    /* The vowel sign is written after its consonant and shaped before it. */
    static const uint16_t expected[2] = {CplxIMatra, CplxKa};
    static const uint32_t clusters[3] = {0u, 2u, 2u};

    IndicFaceOpen(&face);
    result = ConfShapeDefault(&face, IndicKaIMatra);
    ConfExpectGlyphs(result, expected, 2u, "text.indic.pre_base_matra", IndicKaIMatra);
    /* One syllable, two glyphs, and the vowel sign advances nothing. */
    ConfExpectClusters(result, 0u, clusters, 1u, "text.indic.pre_base_matra", IndicKaIMatra);
    ConfExpectFloatClose(ConfGlyphAt(result, 0u)->advanceX, 0.0f, "text.indic.pre_base_matra",
                         "the vowel sign's advance");
    ConfExpectAdvance(result, IndicUnitsToPx((float)CplxBaseLetterAdvance), "text.indic.pre_base_matra",
                      IndicKaIMatra);
    ConfExpectValid(result, "text.indic.pre_base_matra");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

static void TestIndicPostBaseMatra(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    /* A post-base vowel sign is written *and* shaped after its consonant, so nothing moves. */
    static const uint16_t expected[2] = {CplxKa, CplxAaMatra};

    IndicFaceOpen(&face);
    result = ConfShapeDefault(&face, IndicKaAaMatra);
    ConfExpectGlyphs(result, expected, 2u, "text.indic.post_base_matra", IndicKaAaMatra);
    ConfExpectFloatClose(ConfGlyphAt(result, 1u)->advanceX, 0.0f, "text.indic.post_base_matra",
                         "the vowel sign's advance");
    ConfExpectAdvance(result, IndicUnitsToPx((float)CplxBaseLetterAdvance), "text.indic.post_base_matra",
                      IndicKaAaMatra);
    ConfExpectValid(result, "text.indic.post_base_matra");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

static void TestIndicMixedLatin(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    static const uint16_t latin[1] = {CplxUpperA};
    static const uint16_t deva[1] = {CplxKsa};

    IndicFaceOpen(&face);
    result = ConfShapeDefault(&face, "A" IndicKaViramaSsa);
    /* Two scripts, two runs, and the Devanagari half is still shaped as a conjunct. */
    ConfExpectRunCount(result, 2u, "text.indic.mixed_latin", "A" IndicKaViramaSsa);
    ConfExpectRunClassification(result, 0u, RoboUScriptLatin, RoboTextDirectionLtr, "text.indic.mixed_latin",
                                "A" IndicKaViramaSsa);
    ConfExpectRunClassification(result, 1u, RoboUScriptDevanagari, RoboTextDirectionLtr,
                                "text.indic.mixed_latin", "A" IndicKaViramaSsa);
    {
        const RoboTextGlyphRun *first = RoboTextShapeResultGlyphRun(result, 0u);
        const RoboTextGlyphRun *second = RoboTextShapeResultGlyphRun(result, 1u);

        RoboCheckMsg(RoboTextGlyphRunCount(first) == 1u &&
                         RoboTextGlyphRunGlyph(first, 0u)->glyphId == latin[0],
                     "the Latin half did not shape as itself");
        RoboCheckMsg(RoboTextGlyphRunCount(second) == 1u &&
                         RoboTextGlyphRunGlyph(second, 0u)->glyphId == deva[0],
                     "the Devanagari half did not shape as a conjunct");
    }
    ConfExpectValid(result, "text.indic.mixed_latin");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §17/§20 — the plan, the stages and the script's own rules            */
/* ------------------------------------------------------------------ */

static void TestIndicScriptPlan(void)
{
    const RoboTextComplexScriptPlan *plan = RoboTextComplexPlanForScript(RoboUScriptDevanagari);
    const RoboTextIndicScriptRules *rules = RoboTextIndicRulesForScript(RoboUScriptDevanagari);

    RoboCheckMsg(plan != NULL, "the module has no row for Devanagari");
    RoboCheckEqInt((int)plan->strategy, (int)RoboTextComplexStrategyIndic);
    RoboCheckEqInt((int)plan->scriptTag, (int)RoboTextOtTagOf("deva"));
    RoboCheckEqInt((int)plan->requiresClusterAnalysis, 1);
    RoboCheckEqInt((int)plan->requiresJoining, 0);
    RoboCheckEqInt((int)plan->requiresReordering, 1);
    RoboCheckEqInt((int)plan->hasReph, 1);
    RoboCheckEqInt((int)plan->hasHalfForms, 1);
    RoboCheckEqInt((int)plan->hasVirama, 1);
    /* A script this build has no rows for answers the empty plan rather than another script's. */
    {
        const RoboTextComplexScriptPlan *none = RoboTextComplexPlanForScript(RoboUScriptThaana);

        RoboCheckEqInt((int)none->strategy, (int)RoboTextComplexStrategyNone);
        RoboCheckEqInt((int)none->scriptTag, 0);
    }

    /* The script's own rules: its virama, its nukta, and what counts as a consonant. */
    RoboCheckMsg(rules != NULL, "the module has no rules for Devanagari");
    RoboCheckEqInt(RoboTextIndicIsVirama(RoboUScriptDevanagari, 0x094Du), 1);
    RoboCheckEqInt(RoboTextIndicIsVirama(RoboUScriptDevanagari, 0x0915u), 0);
    /* A vowel sign is not a consonant: it can never be a syllable's base. */
    RoboCheckEqInt(RoboTextIndicIsConsonant(RoboUScriptDevanagari, 0x093Fu), 0);
    RoboCheckEqInt(RoboTextIndicIsConsonant(RoboUScriptDevanagari, 0x0915u), 1);
    RoboCheckEqInt(RoboTextIndicIsAttachment(0x093Fu), 1);
    RoboCheckEqInt(RoboTextIndicIsAttachment(0x0915u), 0);
}

static void TestIndicStages(void)
{
    RoboTextComplexPlan plan;
    char text[1024];
    size_t length = 0u;

    RoboTextComplexPlanInit(&plan);
    RoboCheckEqInt((int)RoboTextComplexPlanResolve(RoboUScriptDevanagari, NULL, &plan, NULL), (int)RoboOk);
    RoboCheckEqInt((int)plan.strategy, (int)RoboTextComplexStrategyIndic);
    RoboCheckEqInt((int)plan.stageCount, 3);
    RoboCheckStrEq(RoboTextComplexPlanStage(&plan, 0u)->name, "indic.common");
    RoboCheckStrEq(RoboTextComplexPlanStage(&plan, 1u)->name, "indic.substitute");
    RoboCheckStrEq(RoboTextComplexPlanStage(&plan, 2u)->name, "indic.presentation");
    /*
     * The two reorders are on the stages the phase's §19 requires: the initial move **before** the first
     * stage (while the buffer is still one glyph per scalar, so a move can be stated in scalars) and the final
     * move **after** the substitution stage, because until `rphf` has run there is no reph glyph to place.
     */
    RoboCheckEqInt((int)plan.stages[0].reorderBefore, (int)RoboTextComplexReorderInitial);
    RoboCheckEqInt((int)plan.stages[1].reorderAfter, (int)RoboTextComplexReorderFinal);
    /* The masked features are the ones whose position decides what a conjunct becomes. */
    RoboCheckEqInt((int)plan.stages[1].features[2].mask, (int)RoboTextIndicMaskReph);
    RoboCheckEqInt((int)plan.stages[1].features[8].mask, (int)RoboTextIndicMaskHalf);
    RoboCheckEqInt((int)plan.stages[1].features[10].mask, (int)RoboTextIndicMaskConjunct);

    memset(text, 0, sizeof(text));
    RoboCheckEqInt((int)RoboTextComplexPlanWrite(&plan, text, sizeof(text), &length), (int)RoboOk);
    RoboCheckMsg(length > 0u, "the Indic plan dump wrote nothing");
    RoboCheckMsg(strstr(text, "indic.substitute") != NULL, "the plan dump lost a stage name");
}

static void TestIndicDiagnostics(void)
{
    RoboTextComplexCluster clusters[8];
    char text[1024];
    size_t length = 0u;

    memset(clusters, 0, sizeof(clusters));
    memset(text, 0, sizeof(text));
    RoboCheckEqInt((int)IndicClusters(IndicKaViramaSsa, clusters, 8u), 1);
    RoboCheckEqInt((int)RoboTextIndicClustersWrite(clusters, 1u, text, sizeof(text), &length), (int)RoboOk);
    RoboCheckMsg(length > 0u, "the cluster dump wrote nothing");
    RoboCheckMsg(strstr(text, "syllable") != NULL, "the cluster dump lost the kind");
    RoboCheckMsg(strstr(text, "virama") != NULL, "the cluster dump lost the virama flag");
    /* Complete-or-absent: a buffer that cannot hold the record is refused rather than truncated. */
    RoboCheckEqInt((int)RoboTextIndicClustersWrite(clusters, 1u, text, 4u, &length), (int)RoboNotSupported);

    /* Reordering needs a buffer: asking one to move nothing is an invalid argument, not silence. */
    RoboCheckEqInt((int)RoboTextComplexReorderApply(NULL, RoboTextComplexReorderInitial, NULL,
                                                    ConfView(IndicKaViramaSsa), RoboTextRangeFull(3u), NULL),
                   (int)RoboInvalidArgument);
}

static void TestIndicOrphanSigns(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    RoboTextComplexCluster clusters[8];
    size_t count;

    IndicFaceOpen(&face);
    /*
     * A virama with no consonant before it. There is no syllable to complete, so nothing substitutes, the run
     * stays valid, and the virama keeps its zero advance — acceptance §13, and the reason the cluster
     * classifier checks `RoboTextIndicIsConsonant` before it treats a scalar as a syllable's start.
     */
    result = ConfShapeDefault(&face, IndicVirama);
    ConfExpectGlyphCount(result, 1u, "text.indic.orphan_sign", IndicVirama);
    ConfExpectValid(result, "text.indic.orphan_sign");
    RoboTextShapeResultDestroy(result);

    /* A vowel sign before any consonant is the same shape of input and gets the same answer. */
    result = ConfShapeDefault(&face, IndicIMatra IndicKa);
    ConfExpectGlyphCount(result, 2u, "text.indic.orphan_sign", IndicIMatra IndicKa);
    ConfExpectValid(result, "text.indic.orphan_sign");
    RoboTextShapeResultDestroy(result);

    /* Two clusters, not one: the sign is its own, and the consonant after it is its own syllable. */
    count = IndicClusters(IndicVirama IndicKa, clusters, 8u);
    RoboCheckEqInt((int)count, 2);
    RoboCheckEqInt((int)clusters[0].source.end, 1);
    RoboCheckEqInt((int)clusters[1].baseIndex, 0);
    ConfFaceClose(&face);
}

static void TestIndicThreeScripts(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    /* Latin, then Arabic, then Devanagari: three strategies in one logical string (§10). */
    const char *text = "A" IndicArabicBehBehBeh IndicKa IndicVirama IndicSsa;
    static const uint16_t latin[1] = {CplxUpperA};
    static const uint16_t arabic[3] = {CplxInitBeh, CplxMediBeh, CplxFinaBeh};
    static const uint16_t deva[1] = {CplxKsa};

    IndicFaceOpen(&face);
    result = ConfShapeDefault(&face, text);
    ConfExpectRunCount(result, 3u, "text.indic.three_scripts", text);
    ConfExpectRunClassification(result, 0u, RoboUScriptLatin, RoboTextDirectionLtr,
                                "text.indic.three_scripts", text);
    ConfExpectRunClassification(result, 1u, RoboUScriptArabic, RoboTextDirectionRtl,
                                "text.indic.three_scripts", text);
    ConfExpectRunClassification(result, 2u, RoboUScriptDevanagari, RoboTextDirectionLtr,
                                "text.indic.three_scripts", text);
    /*
     * Each half shaped by its own strategy and by nothing else: the Arabic run took its contextual forms, the
     * Devanagari run took its conjunct, and neither ran the other's stage. The list is in **logical** order,
     * so the Devanagari run is the third even though two of the three scripts disagree about direction.
     */
    {
        static const uint16_t *expected[3] = {latin, arabic, deva};
        static const size_t counts[3] = {1u, 3u, 1u};

        for (size_t run = 0u; run < 3u; run++) {
            const RoboTextGlyphRun *glyphs = RoboTextShapeResultGlyphRun(result, run);

            RoboCheckEqInt((int)RoboTextGlyphRunCount(glyphs), (int)counts[run]);
            for (size_t index = 0u; index < counts[run]; index++) {
                RoboCheckMsg(RoboTextGlyphRunGlyph(glyphs, (RoboTextGlyphIndex)index)->glyphId ==
                                 expected[run][index],
                             "a run of the three-script line shaped with the wrong script's stage");
            }
        }
    }
    ConfExpectValid(result, "text.indic.three_scripts");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

static void TestIndicDeterminism(void)
{
    ConfFace face;
    RoboTextShapeResult *first;
    RoboTextShapeResult *second;

    IndicFaceOpen(&face);
    first = ConfShapeDefault(&face, IndicKaViramaSsa);
    second = ConfShapeDefault(&face, IndicKaViramaSsa);
    /* Byte-for-byte the same run two times over: ids, sources, clusters, advances and offsets. */
    RoboCheckMsg(ConfResultsEqual(first, second), "two runs of one conjunct disagreed");
    RoboTextShapeResultDestroy(first);
    RoboTextShapeResultDestroy(second);
    ConfFaceClose(&face);
}

void TestRegisterTextIndic(void)
{
    TestRegister("text.indic.clusters", TestIndicClusters);
    TestRegister("text.indic.base", TestIndicBase);
    TestRegister("text.indic.masks", TestIndicMasks);
    TestRegister("text.indic.safe_boundary", TestIndicSafeBoundary);
    TestRegister("text.indic.half_form", TestIndicHalfForm);
    TestRegister("text.indic.conjunct", TestIndicConjunct);
    TestRegister("text.indic.reph", TestIndicReph);
    TestRegister("text.indic.pre_base_matra", TestIndicPreBaseMatra);
    TestRegister("text.indic.post_base_matra", TestIndicPostBaseMatra);
    TestRegister("text.indic.mixed_latin", TestIndicMixedLatin);
    TestRegister("text.indic.script_plan", TestIndicScriptPlan);
    TestRegister("text.indic.stages", TestIndicStages);
    TestRegister("text.indic.diagnostics", TestIndicDiagnostics);
    TestRegister("text.indic.orphan_sign", TestIndicOrphanSigns);
    TestRegister("text.indic.three_scripts", TestIndicThreeScripts);
    TestRegister("text.indic.determinism", TestIndicDeterminism);
}
