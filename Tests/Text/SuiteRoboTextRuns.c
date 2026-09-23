/*
 * RoboText's segmentation (Phase 12.4) — `text.runs.*` and `text.direction.*`.
 *
 * No font is needed for any case here, and that is a property the module claims rather than a convenience:
 * a run's script and its classified direction are properties of the **text**, so the fixture passes a
 * context with no face and the segmentation still has to answer. The two cases that would catch a design
 * mistake are the neutrality ones — `"abc 123 def"` must be **one** script run and **one** direction run,
 * not three and not five — and the declared-base one, where declaring RTL *splits* the Latin run at the
 * space. Both are the header's argument, made executable.
 *
 * The direction cases deliberately do **not** test the Unicode bidirectional algorithm, because there is
 * none: what is asserted is classification (these scalars are strong RTL) and the paragraph default, which
 * is the input 12.8's algorithm consumes.
 */

#include "RoboTextTestSupport.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static RoboTextView View(const char *text)
{
    return RoboTextViewMake(text, strlen(text));
}

/** Segment @p text with no face and the given options, asserting the call succeeded. */
static RoboTextRunSet *Segment(const char *text, const RoboTextShapeOptions *options)
{
    RoboTextRunSet *set = NULL;

    RoboCheckEqInt((int)RoboTextRunSetCreate(View(text), NULL, options, &set, NULL), (int)RoboOk);
    RoboCheckMsg(set != NULL, "segmentation produced no run set");
    return set;
}

/* ------------------------------------------------------------------ */
/* One script, and the neutrality rule                                 */
/* ------------------------------------------------------------------ */

static void TestRunsSingleScript(void)
{
    RoboTextRunSet *set = Segment(TextSampleHello, NULL);
    const RoboTextRun *run;
    RoboTextRunStats stats;

    RoboCheckEqInt((int)RoboTextRunSetScalarCount(set), 18);
    RoboCheckEqInt((int)RoboTextRunSetScriptRunCount(set), 1);
    RoboCheckEqInt((int)RoboTextRunSetDirectionRunCount(set), 1);
    RoboCheckEqInt((int)RoboTextRunSetTextRunCount(set), 1);
    RoboCheckEqInt((int)RoboTextRunSetBaseDirection(set), (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextRunSetScriptHint(set), (int)RoboTextScriptCommon);

    run = RoboTextRunSetTextRun(set, 0u);
    RoboCheck(run != NULL);
    RoboCheckEqInt((int)run->range.start, 0);
    RoboCheckEqInt((int)run->range.end, 18);
    RoboCheckEqInt((int)run->script, (int)RoboUScriptLatin);
    RoboCheckEqInt((int)run->direction, (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)run->baseDirection, (int)RoboTextDirectionLtr);
    /* The font context is carried by value and here it is the "no font" one: a run is still a run. */
    RoboCheck(run->font.face == NULL);
    RoboCheck(RoboTextFontStyleIsValid(&run->font.style));
    RoboCheck(!RoboTextLanguageIsSet(&run->language));
    RoboCheckStrEq(RoboTextLanguageTag(&run->language), "");

    RoboCheckEqInt((int)RoboTextRunSetGetStats(set, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.scriptRuns, 1);
    RoboCheckEqInt((int)stats.directionRuns, 1);
    RoboCheckEqInt((int)stats.textRuns, 1);
    RoboCheckEqInt((int)stats.rtlRuns, 0);
    RoboCheckEqInt((int)stats.unknownDirectionRuns, 0);
    RoboCheckEqInt((int)stats.distinctScripts, 1);
    RoboCheckEqInt((int)stats.scalars, 18);

    /* Every scalar in the run resolves to it, and the end-of-text position resolves to none. */
    for (RoboTextScalarIndex index = 0u; index < 18u; index++) {
        RoboCheck(RoboTextRunSetRunAtScalar(set, index) == run);
    }
    RoboCheck(RoboTextRunSetRunAtScalar(set, 18u) == NULL);
    RoboCheck(RoboTextRunSetTextRun(set, 1u) == NULL);
    RoboCheck(RoboTextRunSetScriptRun(set, 1u) == NULL);
    RoboCheck(RoboTextRunSetDirectionRun(set, 1u) == NULL);

    RoboTextRunSetDestroy(set);
}

static void TestRunsNeutrality(void)
{
    RoboTextRunSet *set = Segment("abc 123 def", NULL);
    const RoboTextRun *run = RoboTextRunSetTextRun(set, 0u);
    RoboTextRunStats stats;

    /*
     * The case the header argues: digits and spaces are `Common`, and Common is *not* a script of its own,
     * so the whole string is one Latin run rather than `[Latn][Zyyy][Latn]`. A consumer that wanted three
     * runs would have to ask for them; the default is the answer every consumer wants.
     */
    RoboCheckEqInt((int)RoboTextRunSetScriptRunCount(set), 1);
    RoboCheckEqInt((int)RoboTextRunSetDirectionRunCount(set), 1);
    RoboCheckEqInt((int)RoboTextRunSetTextRunCount(set), 1);
    RoboCheck(run != NULL);
    RoboCheckEqInt((int)run->script, (int)RoboUScriptLatin);
    RoboCheckEqInt((int)run->direction, (int)RoboTextDirectionLtr);

    RoboCheckEqInt((int)RoboTextRunSetGetStats(set, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.distinctScripts, 1);
    RoboTextRunSetDestroy(set);

    /*
     * A *leading* neutral takes the script that follows it, which is the same rule read forwards. The
     * digits are the only script-bearing run, so the space and the digits are Latin — and the run still
     * starts at scalar 0.
     */
    set = Segment(" 123 abc", NULL);
    RoboCheckEqInt((int)RoboTextRunSetTextRunCount(set), 1);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->range.start, 0);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->script, (int)RoboUScriptLatin);
    RoboTextRunSetDestroy(set);

    /*
     * And a text with **no** script-bearing scalar at all is `Common` rather than a guess: nothing resolved,
     * so nothing is claimed.
     *
     * Its *direction*, though, is left-to-right since Phase 12.8 — and the difference between this and the
     * `Unknown` it was in 12.7 is the whole point of the bidi stage. 12.7 classifies: the digits are not
     * strong, the text has no strong scalar and nothing was declared, so it refuses to claim a direction.
     * 12.8 resolves: UAX #9's P3 answers level 0 for exactly that paragraph, so the text run carries the
     * direction that applies and `RoboTextRunSetBaseSource` says the text did not ask for it. The refused
     * answer is not lost — it is the **direction runs'**, asserted below and in the following case.
     */
    set = Segment("123 456", NULL);
    RoboCheckEqInt((int)RoboTextRunSetTextRunCount(set), 1);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->script, (int)RoboTextScriptCommon);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->direction, (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->bidiLevel, 0);
    RoboCheckEqInt((int)RoboTextRunSetBaseDirection(set), (int)RoboTextDirectionUnknown);
    RoboCheckEqInt((int)RoboTextRunSetBaseLevel(set), 0);
    RoboCheckEqInt((int)RoboTextRunSetBaseSource(set), (int)RoboTextBidiBaseDefault);
    RoboTextRunSetDestroy(set);

    /*
     * The same text, read through the two stages at once: the direction runs keep 12.7's `Unknown` — the
     * classification refuses to claim what UAX #9's P2/P3 has not assigned — while the text run beside
     * them carries the level the algorithm resolved. One fact, two readings, and this is the case that
     * pins them apart.
     */
    set = Segment("123 456", NULL);
    {
        RoboTextRunStats stats2;

        RoboCheckEqInt((int)RoboTextRunSetGetStats(set, &stats2), (int)RoboOk);
        /* The counter counts the direction runs' classification, so it still sees the state 12.8 resolved. */
        RoboCheckEqInt((int)stats2.unknownDirectionRuns, 1);
        RoboCheckEqInt((int)stats2.rtlRuns, 0);
        RoboCheckEqInt((int)stats2.bidiRuns, 1);
        RoboCheckEqInt((int)stats2.baseLevel, 0);
        /*
         * And the algorithm's own segmentation, which here is **one** run at level 0 — the case that
         * shows why "a digit is not left-to-right" is a statement about classes rather than about levels.
         * The digits are `EN`, but W7 searches back from each European number for the first strong type
         * and finds L — `sor` is L, because the paragraph level is even — so it *changes them to L*, and
         * L at an even level stays at 0. The same digits in a right-to-left paragraph keep their `EN` and
         * land at level 2 (I2), which the declared-base case below asserts.
         */
        RoboCheckEqInt((int)RoboTextRunSetBidiRunCount(set), 1);
        RoboCheckEqInt((int)RoboTextRunSetBidiRun(set, 0u)->level, 0);
        RoboCheckEqInt((int)RoboTextRunSetBidiRun(set, 0u)->direction, (int)RoboTextDirectionLtr);
        /* The level runs cover the text with no gaps, which is the invariant the phase names. */
        RoboCheckEqInt((int)RoboTextRunSetBidiRun(set, 0u)->range.start, 0);
        RoboCheckEqInt((int)RoboTextRunSetBidiRun(set, 0u)->range.end, 7);
        RoboCheckEqInt((int)RoboTextRunSetBidiRun(set, 0u)->logicalIndex, 0);
    }
    RoboTextRunSetDestroy(set);
}

/* ------------------------------------------------------------------ */
/* Mixed scripts and directions                                        */
/* ------------------------------------------------------------------ */

static void TestRunsMixedScripts(void)
{
    /* `"Hello שלום"` — the header's own example, in Latin then Hebrew. */
    static const char *text = "Hello \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";
    RoboTextRunSet *set = Segment(text, NULL);
    RoboTextRunStats stats;

    RoboCheckEqInt((int)RoboTextRunSetScalarCount(set), 10);
    /* Latin `Hello` plus the space (which takes the Latin carry) is one run; four Hebrew scalars another. */
    RoboCheckEqInt((int)RoboTextRunSetScriptRunCount(set), 2);
    RoboCheckEqInt((int)RoboTextRunSetTextRunCount(set), 2);
    RoboCheckEqInt((int)RoboTextRunSetScriptRun(set, 0u)->script, (int)RoboUScriptLatin);
    RoboCheckEqInt((int)RoboTextRunSetScriptRun(set, 0u)->range.start, 0);
    RoboCheckEqInt((int)RoboTextRunSetScriptRun(set, 0u)->range.end, 6);
    RoboCheckEqInt((int)RoboTextRunSetScriptRun(set, 1u)->script, (int)RoboUScriptHebrew);
    RoboCheckEqInt((int)RoboTextRunSetScriptRun(set, 1u)->range.start, 6);
    RoboCheckEqInt((int)RoboTextRunSetScriptRun(set, 1u)->range.end, 10);

    /* The base is left-to-right (the first strong scalar is `H`), so the space before the Hebrew is LTR
     * and the Hebrew is RTL: two direction runs, and the text runs match the script runs because the
     * boundaries agree. */
    RoboCheckEqInt((int)RoboTextRunSetBaseDirection(set), (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextRunSetDirectionRunCount(set), 2);
    RoboCheckEqInt((int)RoboTextRunSetDirectionRun(set, 0u)->direction, (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextRunSetDirectionRun(set, 0u)->range.end, 6);
    RoboCheckEqInt((int)RoboTextRunSetDirectionRun(set, 1u)->direction, (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 1u)->script, (int)RoboUScriptHebrew);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 1u)->direction, (int)RoboTextDirectionRtl);

    RoboCheckEqInt((int)RoboTextRunSetGetStats(set, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.rtlRuns, 1);
    RoboCheckEqInt((int)stats.unknownDirectionRuns, 0);
    RoboCheckEqInt((int)stats.distinctScripts, 2);

    /* Index 6 is Hebrew, so it is in the second run; index 5 is the space, still in the first. */
    RoboCheck(RoboTextRunSetRunAtScalar(set, 5u) == RoboTextRunSetTextRun(set, 0u));
    RoboCheck(RoboTextRunSetRunAtScalar(set, 6u) == RoboTextRunSetTextRun(set, 1u));
    RoboCheck(RoboTextRunSetRunAtScalar(set, 9u) == RoboTextRunSetTextRun(set, 1u));
    RoboCheck(RoboTextRunSetRunAtScalar(set, 10u) == NULL);
    RoboTextRunSetDestroy(set);
}

static void TestRunsDeclaredBase(void)
{
    static const char *text = "Hello \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";
    RoboTextShapeOptions options;
    RoboTextRunSet *set;

    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionRtl;
    set = Segment(text, &options);

    /*
     * Declaring RTL is not decoration: it changes the **neutral** classification, so the space between the
     * Latin and the Hebrew no longer belongs to the Latin run. Three text runs where there were two, which
     * is the rule stated in the header made visible — a declared base is the document's direction, and the
     * bidirectional algorithm's job in 12.8 is to refine that, not to guess it here.
     */
    RoboCheckEqInt((int)RoboTextRunSetBaseDirection(set), (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextRunSetTextRunCount(set), 3);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->range.start, 0);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->range.end, 5);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->script, (int)RoboUScriptLatin);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->direction, (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 1u)->range.start, 5);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 1u)->range.end, 6);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 1u)->direction, (int)RoboTextDirectionRtl);
    /* Every run echoes the declared base beside its own classified direction. */
    for (size_t index = 0u; index < 3u; index++) {
        RoboCheckEqInt((int)RoboTextRunSetTextRun(set, index)->baseDirection,
                       (int)RoboTextDirectionRtl);
    }
    RoboTextRunSetDestroy(set);

    /*
     * Digits with a declared RTL base are resolved rather than `Unknown` — no strong scalar is needed,
     * because the paragraph said so — and what they resolve to is **left-to-right**, which is 12.7's rule
     * and not a leftover of the "neutrals take the base" simplification 12.5 shipped: a number's level is
     * even in either base (UAX #9 I1/I2), and even is left-to-right. So a declared RTL paragraph does *not*
     * make every scalar in it RTL, which is the property the direction stage exists to establish.
     */
    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionRtl;
    set = Segment("123 456", &options);
    RoboCheckEqInt((int)RoboTextRunSetBaseDirection(set), (int)RoboTextDirectionRtl);
    /* Two numbers and the space between them: the numbers are left-to-right, and the space is right-to-left
     * because that is what a number lends a neutral beside it (UAX #9 N1). */
    RoboCheckEqInt((int)RoboTextRunSetTextRunCount(set), 3);
    RoboCheckEqInt((int)RoboTextRunSetDirectionRunCount(set), 3);
    /*
     * The **level** runs for the same text, and the case where a digit really is at level 2: with an odd
     * paragraph level W7 finds no L to change the `EN` into (it finds `sor`, which is R), so the digits
     * stay `EN` and I2 lifts them one level — to 2, which is even and therefore left-to-right. The space
     * between them takes the `R` they lend it (N1) and stays at the paragraph's level 1.
     */
    RoboCheckEqInt((int)RoboTextRunSetBidiRunCount(set), 3);
    RoboCheckEqInt((int)RoboTextRunSetBidiRun(set, 0u)->level, 2);
    RoboCheckEqInt((int)RoboTextRunSetBidiRun(set, 0u)->direction, (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextRunSetBidiRun(set, 0u)->range.end, 3);
    RoboCheckEqInt((int)RoboTextRunSetBidiRun(set, 1u)->level, 1);
    RoboCheckEqInt((int)RoboTextRunSetBidiRun(set, 1u)->direction, (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextRunSetBidiRun(set, 2u)->level, 2);
    RoboCheckEqInt((int)RoboTextRunSetBidiRun(set, 2u)->range.end, 7);
    RoboCheckEqInt((int)RoboTextRunSetBaseLevel(set), 1);
    RoboCheckEqInt((int)RoboTextRunSetBaseSource(set), (int)RoboTextBidiBaseDeclared);
    RoboCheckEqInt((int)RoboTextRunSetDirectionRun(set, 0u)->direction, (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextRunSetDirectionRun(set, 0u)->range.end, 3);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->direction, (int)RoboTextDirectionLtr);
    RoboTextRunSetDestroy(set);

    /*
     * The contrast the case above is worth: the *same* text with nothing declared has no paragraph
     * direction — no strong scalar to derive one from and no declaration to apply — so the digits are
     * `Unknown`, which is the honest answer and the one `text.runs.neutrality` pins.
     */
    RoboTextShapeOptionsInit(&options);
    set = Segment("123 456", &options);
    RoboCheckEqInt((int)RoboTextRunSetBaseDirection(set), (int)RoboTextDirectionUnknown);
    RoboCheckEqInt((int)RoboTextRunSetDirectionRun(set, 0u)->direction, (int)RoboTextDirectionUnknown);
    RoboTextRunSetDestroy(set);
}

/* ------------------------------------------------------------------ */
/* Hints, language and the options record                              */
/* ------------------------------------------------------------------ */

static void TestRunsHintAndLanguage(void)
{
    RoboTextShapeOptions options;
    RoboTextRunSet *set;
    static const char *text = "abc \xD7\xA9\xD7\x9C";

    /* A script hint classifies every scalar as the hinted script, digits and spaces included. */
    RoboTextShapeOptionsInit(&options);
    options.scriptHint = RoboUScriptGreek;
    set = Segment(text, &options);
    RoboCheckEqInt((int)RoboTextRunSetScriptHint(set), (int)RoboUScriptGreek);
    RoboCheckEqInt((int)RoboTextRunSetScriptRunCount(set), 1);
    RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->script, (int)RoboUScriptGreek);
    /* But the *direction* is still classified from the scalars: a hint is a script hint, not a bidi one. */
    RoboCheckEqInt((int)RoboTextRunSetDirectionRunCount(set), 2);
    RoboTextRunSetDestroy(set);

    /* A language is carried into every run, and an unset one reads as the empty tag. */
    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextLanguageSet(&options.language, "en-US"), (int)RoboOk);
    RoboCheck(RoboTextLanguageIsSet(&options.language));
    RoboCheckStrEq(RoboTextLanguageTag(&options.language), "en-US");
    set = Segment(text, &options);
    for (size_t index = 0u; index < RoboTextRunSetTextRunCount(set); index++) {
        RoboCheckStrEq(RoboTextLanguageTag(&RoboTextRunSetTextRun(set, index)->language), "en-US");
    }
    RoboCheckStrEq(RoboTextLanguageTag(RoboTextRunSetLanguage(set)), "en-US");
    RoboTextRunSetDestroy(set);

    /* A tag that is not BCP 47's alphabet is refused; a too-long one is refused; empty clears. */
    {
        RoboTextLanguage language;

        RoboTextLanguageInit(&language);
        RoboCheckEqInt((int)RoboTextLanguageSet(&language, "en US"), (int)RoboParseError);
        RoboCheckEqInt((int)RoboTextLanguageSet(&language, "en_US"), (int)RoboParseError);
        RoboCheckEqInt((int)RoboTextLanguageSet(&language, "aaaaaaaaaaaaaaaaaaaaaaaaaaa"), (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextLanguageSet(&language, "zh-Hant-TW"), (int)RoboOk);
        RoboCheckStrEq(RoboTextLanguageTag(&language), "zh-Hant-TW");
        RoboCheckEqInt((int)RoboTextLanguageSet(&language, ""), (int)RoboOk);
        RoboCheck(!RoboTextLanguageIsSet(&language));
        RoboCheckStrEq(RoboTextLanguageTag(NULL), "");
    }

    /* Features are validated and stored, and nothing reads them: the field exists so 12.10's shaper does
     * not have to add one. */
    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextShapeOptionsAddFeature(&options, "liga"), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextShapeOptionsAddFeature(&options, "kern"), (int)RoboOk);
    RoboCheckEqInt((int)options.featureCount, 2);
    RoboCheckStrEq(options.features[1], "kern");
    RoboCheckEqInt((int)RoboTextShapeOptionsAddFeature(&options, "li"), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextShapeOptionsAddFeature(&options, "ligas"), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextShapeOptionsAddFeature(NULL, "liga"), (int)RoboInvalidArgument);
    RoboCheck(RoboTextShapeOptionsIsValid(&options));
    RoboCheck(!RoboTextShapeOptionsIsValid(NULL));
    {
        /* A record with more features than the array can hold is not usable, and the segmenter refuses it
         * rather than reading past the array. */
        RoboTextShapeOptions broken;

        RoboTextShapeOptionsInit(&broken);
        broken.featureCount = RoboTextMaxFeatures + 1u;
        RoboCheck(!RoboTextShapeOptionsIsValid(&broken));
        {
            RoboTextRunSet *none = NULL;

            RoboCheckEqInt((int)RoboTextRunSetCreate(View("abc"), NULL, &broken, &none, NULL),
                           (int)RoboInvalidArgument);
            RoboCheck(none == NULL);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Empty text, refusals and fuzz                                       */
/* ------------------------------------------------------------------ */

static void TestRunsEmptyAndRefusals(void)
{
    RoboTextRunSet *set = NULL;
    RoboTextRunStats stats;

    /*
     * No text is a fact about a document, not an error in it: an empty set with no runs, and every door
     * above it answering without a special case.
     */
    RoboCheckEqInt((int)RoboTextRunSetCreate(RoboTextViewMake(NULL, 0u), NULL, NULL, &set, NULL),
                   (int)RoboOk);
    RoboCheck(set != NULL);
    RoboCheckEqInt((int)RoboTextRunSetScalarCount(set), 0);
    RoboCheckEqInt((int)RoboTextRunSetScriptRunCount(set), 0);
    RoboCheckEqInt((int)RoboTextRunSetDirectionRunCount(set), 0);
    RoboCheckEqInt((int)RoboTextRunSetTextRunCount(set), 0);
    RoboCheck(RoboTextRunSetRunAtScalar(set, 0u) == NULL);
    RoboCheck(RoboTextRunSetTextRun(set, 0u) == NULL);
    RoboCheckEqInt((int)RoboTextRunSetGetStats(set, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.scalars, 0);
    RoboCheckEqInt((int)stats.textRuns, 0);
    RoboTextRunSetDestroy(set);

    /* A view with NULL bytes and a length is not text, and neither is a NULL out pointer. */
    RoboCheckEqInt((int)RoboTextRunSetCreate(RoboTextViewMake(NULL, 4u), NULL, NULL, &set, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextRunSetCreate(View("abc"), NULL, NULL, NULL, NULL),
                   (int)RoboInvalidArgument);

    /* Every accessor answers for a destroyed/absent set rather than crashing. */
    RoboCheckEqInt((int)RoboTextRunSetScalarCount(NULL), 0);
    RoboCheckEqInt((int)RoboTextRunSetScriptRunCount(NULL), 0);
    RoboCheckEqInt((int)RoboTextRunSetTextRunCount(NULL), 0);
    RoboCheckEqInt((int)RoboTextRunSetDirectionRunCount(NULL), 0);
    RoboCheck(RoboTextRunSetTextRun(NULL, 0u) == NULL);
    RoboCheck(RoboTextRunSetRunAtScalar(NULL, 0u) == NULL);
    RoboCheckEqInt((int)RoboTextRunSetBaseDirection(NULL), (int)RoboTextDirectionUnknown);
    RoboCheckEqInt((int)RoboTextRunSetScriptHint(NULL), (int)RoboTextScriptCommon);
    RoboCheckStrEq(RoboTextLanguageTag(RoboTextRunSetLanguage(NULL)), "");
    RoboCheckEqInt((int)RoboTextRunSetGetStats(NULL, &stats), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextRunSetGetStats(NULL, NULL), (int)RoboInvalidArgument);
    RoboTextRunSetDestroy(NULL);

    /* Determinism: the same bytes segment to the same counters, in the same order, every time. */
    {
        RoboTextRunSet *first = Segment(TextSampleUnicode, NULL);
        RoboTextRunSet *second = Segment(TextSampleUnicode, NULL);
        RoboTextRunStats a;
        RoboTextRunStats b;

        RoboCheckEqInt((int)RoboTextRunSetGetStats(first, &a), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextRunSetGetStats(second, &b), (int)RoboOk);
        /* Counters equal is not enough: the two sets must agree run for run, boundary for boundary, or
         * "deterministic" would only mean "the same number of runs". */
        RoboCheckEqInt((int)a.textRuns, (int)b.textRuns);
        RoboCheckEqInt((int)a.scriptRuns, (int)b.scriptRuns);
        RoboCheckEqInt((int)a.distinctScripts, (int)b.distinctScripts);
        RoboCheckEqInt((int)RoboTextRunSetBaseDirection(first), (int)RoboTextRunSetBaseDirection(second));
        for (size_t index = 0u; index < RoboTextRunSetTextRunCount(first); index++) {
            const RoboTextRun *left = RoboTextRunSetTextRun(first, index);
            const RoboTextRun *right = RoboTextRunSetTextRun(second, index);

            RoboCheck(right != NULL);
            RoboCheck(RoboTextRangeEqual(&left->range, &right->range));
            RoboCheckEqInt((int)left->script, (int)right->script);
            RoboCheckEqInt((int)left->direction, (int)right->direction);
        }
        RoboTextRunSetDestroy(first);
        RoboTextRunSetDestroy(second);
    }
}

static void TestRunsFuzz(void)
{
    uint8_t junk[256];

    for (size_t index = 0u; index < sizeof(junk); index++) {
        junk[index] = (uint8_t)(index * 37u + 11u);
    }
    RoboCheckEqInt((int)RoboTextFuzzSegment(junk, sizeof(junk)), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFuzzSegment(NULL, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFuzzSegment(NULL, 4u), (int)RoboInvalidArgument);
    /* A string made only of continuation bytes is the case a hand-written decoder usually spins on. */
    {
        static const uint8_t continuations[] = {0x80, 0x80, 0x80, 0x80, 0xBF, 0xBF};

        RoboCheckEqInt((int)RoboTextFuzzSegment(continuations, sizeof(continuations)), (int)RoboOk);
    }
    /* And an unterminated multi-byte sequence at the very end of the window. */
    {
        static const uint8_t truncated[] = {'a', 'b', 0xF0, 0x9F, 0x99};

        RoboCheckEqInt((int)RoboTextFuzzSegment(truncated, sizeof(truncated)), (int)RoboOk);
    }
}

void TestRegisterTextRuns(void)
{
    TestRegister("text.runs.single_script", TestRunsSingleScript);
    TestRegister("text.runs.neutrality", TestRunsNeutrality);
    TestRegister("text.runs.mixed_scripts", TestRunsMixedScripts);
    TestRegister("text.runs.declared_base", TestRunsDeclaredBase);
    TestRegister("text.runs.hint_language", TestRunsHintAndLanguage);
    TestRegister("text.runs.empty_refusals", TestRunsEmptyAndRefusals);
    TestRegister("text.runs.fuzz", TestRunsFuzz);
}
