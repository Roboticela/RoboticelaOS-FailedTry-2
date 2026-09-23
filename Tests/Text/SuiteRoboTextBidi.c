/*
 * The RoboText bidi stage (Phase 12.8) — `text.bidi.*`.
 *
 * 12.7's suite (`SuiteRoboTextDirection.c`) pins the **classification**; this one pins the **resolution**,
 * and the two are written to be read together because the phase's central claim is that they are different
 * things. Every case below is one of five:
 *
 *   the paragraph base      declared / first strong / fallback / P3's default, and the fact that the
 *                           fourth is *reported* rather than silently applied (`BaseSource`);
 *   the levels              one resolved level per scalar, with X9's removals marked as "no level"
 *                           rather than level 0;
 *   the level runs          the algorithm's own segmentation, contiguous and complete, plus L2's visual
 *                           order over them and over the visible scalars;
 *   the isolated cases      explicit embeddings, overrides, isolates and their malformed forms;
 *   the integration         the direction a shape request carries, the level a glyph run carries, and
 *                           L4's mirrored glyph selection — which must not touch the text.
 *
 * Three expectations are written as **whole level strings** and whole boundary lists rather than as
 * per-run assertions, because the defect this stage can have is an off-by-one level or a run that moved
 * by one scalar, and a run-by-run assertion hides both inside a passing count.
 *
 * Contract: `Documentation/Runtime/Text/BidirectionalText.md`.
 */

#include "RoboTextTestSupport.h"

#include "RoboMem/RoboMemDebug.h"
#include "RoboUnicode/RoboBidiMirror.h"
#include "RoboUnicode/RoboUnicodeBidi.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */

/** Room for the widest run set any case below asks for. */
#define BidiRunMax 64u

/** Room for the longest scalar-level string a case below prints. */
#define BidiLevelBuffer 512u

static RoboTextView BidiView(const char *text)
{
    return RoboTextViewMake(text, strlen(text));
}

/** The context a layout declares: first strong character, and left to right when there is none. */
static RoboTextDirectionContext BidiLayoutContext(void)
{
    RoboTextDirectionContext context;

    RoboTextDirectionContextInit(&context);
    context.mode = RoboTextDirectionModeAuto;
    context.fallbackDirection = RoboTextDirectionLtr;
    return context;
}

/** The context for a caller that declared @p direction. */
static RoboTextDirectionContext BidiDeclared(RoboTextDirection direction)
{
    return RoboTextDirectionContextForBase(direction);
}

/** A selector over @p primary, so the integration case drives the real font policy. */
static RoboTextFontSelector *BidiSelectorOver(const RoboTextFontContext *primary)
{
    RoboTextFontSelector *selector = NULL;

    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&selector, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, primary, NULL), (int)RoboOk);
    return selector;
}

/** A paragraph over @p text, or NULL. The caller destroys it. */
static RoboTextBidiParagraph *BidiResolve(const char *text, const RoboTextDirectionContext *context)
{
    RoboTextBidiParagraph *paragraph = NULL;

    if (RoboTextBidiParagraphResolveView(BidiView(text), context, &paragraph, NULL) != RoboOk) {
        return NULL;
    }
    return paragraph;
}

/** The resolved level of every scalar, as `"0 0 1 1"`. */
static const char *LevelsText(const RoboTextBidiParagraph *paragraph)
{
    static char buffer[BidiLevelBuffer];
    size_t length = 0u;

    buffer[0] = '\0';
    for (RoboTextScalarIndex index = 0u; index < RoboTextBidiParagraphScalarCount(paragraph); index++) {
        int written = snprintf(buffer + length, sizeof(buffer) - length, "%s%u",
                               (index == 0u) ? "" : " ",
                               (unsigned)RoboTextBidiParagraphResolvedLevel(paragraph, index));

        if (written < 0 || (size_t)written >= sizeof(buffer) - length) {
            return "<overflow>";
        }
        length += (size_t)written;
    }
    return buffer;
}

/** The **primitive** level of every scalar, as `"0 0 x 1"` — `x` is X9's removal. */
static const char *RawLevelsText(const RoboTextBidiParagraph *paragraph)
{
    static char buffer[BidiLevelBuffer];
    size_t length = 0u;

    buffer[0] = '\0';
    for (RoboTextScalarIndex index = 0u; index < RoboTextBidiParagraphScalarCount(paragraph); index++) {
        RoboTextBidiLevel level = RoboTextBidiParagraphLevel(paragraph, index);
        int written;
        char spelled[8];

        if (RoboTextBidiLevelIsResolved(level)) {
            snprintf(spelled, sizeof(spelled), "%u", (unsigned)level);
        } else {
            memcpy(spelled, "x", 2u);
        }
        written = snprintf(buffer + length, sizeof(buffer) - length, "%s%s",
                           (index == 0u) ? "" : " ", spelled);
        if (written < 0 || (size_t)written >= sizeof(buffer) - length) {
            return "<overflow>";
        }
        length += (size_t)written;
    }
    return buffer;
}

/** The logical runs, as `"ltr@0 0..6 | rtl@1 6..10"`. */
static const char *RunsText(const RoboTextBidiParagraph *paragraph)
{
    static char buffer[BidiLevelBuffer];
    size_t length = 0u;

    buffer[0] = '\0';
    for (size_t index = 0u; index < RoboTextBidiParagraphRunCount(paragraph); index++) {
        const RoboTextBidiRun *run = RoboTextBidiParagraphRun(paragraph, index);
        int written = snprintf(buffer + length, sizeof(buffer) - length, "%s%s@%u %u..%u",
                               (index == 0u) ? "" : " | ",
                               RoboTextDirectionName(run->direction), (unsigned)run->level,
                               (unsigned)run->range.start, (unsigned)run->range.end);

        if (written < 0 || (size_t)written >= sizeof(buffer) - length) {
            return "<overflow>";
        }
        length += (size_t)written;
    }
    return buffer;
}

/** L2's visual order of the runs, as `"0 1 2"` of logical indices. */
static const char *VisualRunsText(const RoboTextBidiParagraph *paragraph)
{
    static char buffer[BidiLevelBuffer];
    size_t length = 0u;

    buffer[0] = '\0';
    for (size_t visual = 0u; visual < RoboTextBidiParagraphRunCount(paragraph); visual++) {
        size_t logical = 0u;
        int written;

        if (RoboTextBidiParagraphVisualRun(paragraph, visual, &logical, NULL) != RoboOk) {
            return "<refused>";
        }
        written = snprintf(buffer + length, sizeof(buffer) - length, "%s%u", (visual == 0u) ? "" : " ",
                           (unsigned)logical);
        if (written < 0 || (size_t)written >= sizeof(buffer) - length) {
            return "<overflow>";
        }
        length += (size_t)written;
    }
    return buffer;
}

/** L2's visual order of the visible scalars, as logical indices. */
static const char *VisualScalarsText(const RoboTextBidiParagraph *paragraph)
{
    static char buffer[BidiLevelBuffer];
    size_t length = 0u;

    buffer[0] = '\0';
    for (size_t visual = 0u; visual < RoboTextBidiParagraphVisualScalarCount(paragraph); visual++) {
        RoboTextScalarIndex logical = 0u;
        int written;

        if (RoboTextBidiParagraphVisualScalar(paragraph, visual, &logical, NULL) != RoboOk) {
            return "<refused>";
        }
        written = snprintf(buffer + length, sizeof(buffer) - length, "%s%u", (visual == 0u) ? "" : " ",
                           (unsigned)logical);
        if (written < 0 || (size_t)written >= sizeof(buffer) - length) {
            return "<overflow>";
        }
        length += (size_t)written;
    }
    return buffer;
}

/** @p text in @p context resolves to exactly @p expected levels, and every invariant holds. */
static void ExpectLevels(const char *text, const RoboTextDirectionContext *context, const char *expected)
{
    RoboTextBidiParagraph *paragraph = BidiResolve(text, context);
    char message[768];

    RoboCheckMsg(paragraph != NULL, "the bidi stage refused a paragraph it should have resolved");
    if (!paragraph) {
        return;
    }
    snprintf(message, sizeof(message), "\"%s\" resolved to levels \"%s\", expected \"%s\"", text,
             LevelsText(paragraph), expected);
    RoboCheckMsg(strcmp(LevelsText(paragraph), expected) == 0, message);

    /*
     * And the same pass, checked against the algorithm itself: the shortcut this stage takes for ordinary
     * left-to-right text must produce what UAX #9 produces, level for level. Comparing against
     * `UnicodeBidiResolveLevels` — Phase 4's own door — is what makes that a proof rather than a claim.
     */
    {
        Uchar32 scalars[64];
        RoboTextScalarIndex count = 0u;
        uint8_t full[64];
        uint8_t paragraphLevel = 0u;

        RoboCheck(RoboTextBidiParagraphScalarCount(paragraph) <= 64u);
        for (RoboTextScalarIndex index = 0u; index < RoboTextBidiParagraphScalarCount(paragraph);
             index++) {
            scalars[index] = (Uchar32)RoboTextBidiParagraphScalar(paragraph, index);
            count++;
        }
        if (RoboTextBidiParagraphScalarCount(paragraph) <= 64u) {
            RoboCheckEqInt((int)UnicodeBidiResolveLevels(
                               scalars, (size_t)count,
                               (RoboTextBidiParagraphBaseLevel(paragraph) == 1u) ? RoboBidiRtl : RoboBidiLtr,
                               full, &paragraphLevel),
                           (int)RoboOk);
            RoboCheckEqInt((int)paragraphLevel, (int)RoboTextBidiParagraphBaseLevel(paragraph));
            for (RoboTextScalarIndex index = 0u; index < count; index++) {
                char detail[256];

                snprintf(detail, sizeof(detail), "scalar %u of \"%s\": bidi=%u algorithm=%u",
                         (unsigned)index, text, (unsigned)RoboTextBidiParagraphLevel(paragraph, index),
                         (unsigned)full[index]);
                RoboCheckMsg(RoboTextBidiParagraphLevel(paragraph, index) == full[index], detail);
            }
        }
    }
    RoboTextBidiParagraphDestroy(paragraph);
}

/** @p text in @p context produces exactly @p expected runs. */
static void ExpectBidiRuns(const char *text, const RoboTextDirectionContext *context, const char *expected)
{
    RoboTextBidiParagraph *paragraph = BidiResolve(text, context);
    char message[768];

    RoboCheckMsg(paragraph != NULL, "the bidi stage refused a paragraph it should have resolved");
    if (!paragraph) {
        return;
    }
    snprintf(message, sizeof(message), "\"%s\" resolved to \"%s\", expected \"%s\"", text,
             RunsText(paragraph), expected);
    RoboCheckMsg(strcmp(RunsText(paragraph), expected) == 0, message);
    RoboTextBidiParagraphDestroy(paragraph);
}

/** The samples every invariant case walks. */
static const char *const kBidiSamples[] = {
    "Hello world",
    "Hello שלום world",
    "مرحبا Hello",
    "123 456",
    "שלום 123",
    "(123) שלום",
    TextSampleDirectionPunctuation,
    TextSampleScriptMixed,
    TextSampleDirectionLevels,
    TextSampleScriptNumbers,
    "a\xE2\x81\xA7" "b" "\xE2\x81\xA9" "c", /* a RLI b PDI c */
    "\xE2\x80\xAA" "abc" "\xE2\x80\xAC",      /* LRE abc PDF */
    "e\xCC\x81 \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D",
    "\xF0\x9F\x98\x80 \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D",
    ""
};
#define BidiSampleCount (sizeof(kBidiSamples) / sizeof(kBidiSamples[0]))

/* ------------------------------------------------------------------ */
/* The level vocabulary                                                */
/* ------------------------------------------------------------------ */

static void TestBidiLevelVocabulary(void)
{
    /* Even is left-to-right, odd is right-to-left, and the two "not a level" values are neither. */
    RoboCheck(RoboTextBidiLevelIsResolved(0u));
    RoboCheck(RoboTextBidiLevelIsResolved(1u));
    RoboCheck(RoboTextBidiLevelIsResolved(RoboTextBidiLevelMax));
    RoboCheck(!RoboTextBidiLevelIsResolved(RoboTextBidiLevelRemoved));
    RoboCheck(!RoboTextBidiLevelIsResolved(RoboTextBidiLevelNone));

    RoboCheck(RoboTextBidiLevelIsLeftToRight(0u));
    RoboCheck(RoboTextBidiLevelIsLeftToRight(2u));
    RoboCheck(!RoboTextBidiLevelIsLeftToRight(1u));
    RoboCheck(!RoboTextBidiLevelIsLeftToRight(RoboTextBidiLevelRemoved));
    RoboCheck(RoboTextBidiLevelIsRightToLeft(1u));
    RoboCheck(RoboTextBidiLevelIsRightToLeft(3u));
    RoboCheck(!RoboTextBidiLevelIsRightToLeft(2u));
    RoboCheck(!RoboTextBidiLevelIsRightToLeft(RoboTextBidiLevelNone));

    RoboCheckEqInt((int)RoboTextBidiLevelDirection(0u), (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextBidiLevelDirection(1u), (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextBidiLevelDirection(2u), (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextBidiLevelDirection(RoboTextBidiLevelRemoved),
                   (int)RoboTextDirectionUnknown);
    RoboCheckEqInt((int)RoboTextBidiLevelDirection(RoboTextBidiLevelNone), (int)RoboTextDirectionUnknown);

    /* A declared direction fixes a level, and P3's default is level 0. */
    RoboCheckEqInt((int)RoboTextBidiLevelForDirection(RoboTextDirectionLtr), 0);
    RoboCheckEqInt((int)RoboTextBidiLevelForDirection(RoboTextDirectionRtl), 1);
    RoboCheckEqInt((int)RoboTextBidiLevelForDirection(RoboTextDirectionUnknown), 0);

    /*
     * `None` is not `Removed`, and that is the point of having two: one says "this scalar was removed from
     * reordering", the other "this record was never resolved". Both are outside the range the algorithm can
     * assign, which is what makes them distinguishable from a real level.
     */
    RoboCheck(RoboTextBidiLevelNone != RoboTextBidiLevelRemoved);
    RoboCheck(RoboTextBidiLevelMax < RoboTextBidiLevelNone);
    RoboCheck(RoboTextBidiLevelNone < RoboTextBidiLevelRemoved);

    /* The base-source vocabulary, which is what tells a caller *why* a paragraph is right-to-left. */
    RoboCheck(RoboTextBidiBaseSourceIsKnown(RoboTextBidiBaseDeclared));
    RoboCheck(RoboTextBidiBaseSourceIsKnown(RoboTextBidiBaseStrong));
    RoboCheck(RoboTextBidiBaseSourceIsKnown(RoboTextBidiBaseFallback));
    RoboCheck(RoboTextBidiBaseSourceIsKnown(RoboTextBidiBaseDefault));
    RoboCheck(!RoboTextBidiBaseSourceIsKnown((RoboTextBidiBaseSource)7));
    RoboCheckStrEq(RoboTextBidiBaseSourceName(RoboTextBidiBaseDeclared), "declared");
    RoboCheckStrEq(RoboTextBidiBaseSourceName(RoboTextBidiBaseStrong), "strong");
    RoboCheckStrEq(RoboTextBidiBaseSourceName(RoboTextBidiBaseFallback), "fallback");
    RoboCheckStrEq(RoboTextBidiBaseSourceName(RoboTextBidiBaseDefault), "default");
    RoboCheckStrEq(RoboTextBidiBaseSourceName((RoboTextBidiBaseSource)7), "unknown");
}

/* ------------------------------------------------------------------ */
/* The paragraph base                                                  */
/* ------------------------------------------------------------------ */

static void TestBidiParagraphBase(void)
{
    RoboTextBidiParagraph *paragraph;
    RoboTextDirectionContext context;
    RoboTextBidiStats stats;

    /* A declaration wins, and nothing in the text may overrule it. */
    {
        RoboTextDirectionContext declaredLtr = BidiDeclared(RoboTextDirectionLtr);

        paragraph = BidiResolve(TextSampleDirectionMixedRtl, &declaredLtr);
        RoboCheck(paragraph != NULL);
        RoboCheckEqInt((int)RoboTextBidiParagraphBaseDirection(paragraph), (int)RoboTextDirectionLtr);
        RoboCheckEqInt((int)RoboTextBidiParagraphBaseLevel(paragraph), 0);
        RoboCheckEqInt((int)RoboTextBidiParagraphBaseSource(paragraph), (int)RoboTextBidiBaseDeclared);
        RoboCheckEqInt((int)RoboTextBidiParagraphBaseWasDefaulted(paragraph), 0);
        /*
         * The Arabic is then the *embedded* direction: an even paragraph whose **first** run is odd,
         * because the declaration fixes the paragraph and not the text. A declared base that made the
         * Arabic left-to-right would be the defect this asserts against.
         */
        RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, 0u)->level, 1);
        RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, 0u)->range.start, 0);
        RoboTextBidiParagraphDestroy(paragraph);

        paragraph = BidiResolve("Hello", &declaredLtr);
        RoboCheck(paragraph != NULL);
        RoboCheckEqInt((int)RoboTextBidiParagraphBaseSource(paragraph), (int)RoboTextBidiBaseDeclared);
        RoboTextBidiParagraphDestroy(paragraph);
    }

    /* First strong: Hebrew and Arabic give RTL, Latin gives LTR, and P2 ignores digits and punctuation. */
    paragraph = BidiResolve(TextSampleScriptHebrew, NULL);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseDirection(paragraph), (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseLevel(paragraph), 1);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseSource(paragraph), (int)RoboTextBidiBaseStrong);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseWasDefaulted(paragraph), 0);
    RoboTextBidiParagraphDestroy(paragraph);

    paragraph = BidiResolve(TextSampleScriptArabic, NULL);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseLevel(paragraph), 1);
    RoboTextBidiParagraphDestroy(paragraph);

    paragraph = BidiResolve("Hello", NULL);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseLevel(paragraph), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseSource(paragraph), (int)RoboTextBidiBaseStrong);
    RoboTextBidiParagraphDestroy(paragraph);

    /*
     * `123 456` is digits, a space and a separator: P2 finds no strong character, so P3's level-0 default
     * is applied and **reported** — the paragraph is not called left-to-right on the strength of a digit.
     */
    paragraph = BidiResolve("123 456", NULL);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseSource(paragraph), (int)RoboTextBidiBaseDefault);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseWasDefaulted(paragraph), 1);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseLevel(paragraph), 0);
    RoboTextBidiParagraphDestroy(paragraph);

    /*
     * `TextSampleScriptNumbers` is **strong**, and the reason is worth keeping in the suite: the sample
     * carries Thai digits (U+0E50…), whose `Bidi_Class` is `L` — not `EN`. So "numbers are neutral" is a
     * claim about *which* numbers, and the phase's §"do not infer script from appearance" has a bidi twin.
     */
    paragraph = BidiResolve(TextSampleScriptNumbers, NULL);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseSource(paragraph), (int)RoboTextBidiBaseStrong);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseWasDefaulted(paragraph), 0);
    RoboTextBidiParagraphDestroy(paragraph);

    paragraph = BidiResolve(TextSampleScriptPunctuation, NULL);
    RoboCheck(paragraph != NULL);
    /* It has strong Latin letters, so this one is *strong* rather than defaulted — the contrast case. */
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseSource(paragraph), (int)RoboTextBidiBaseStrong);
    RoboTextBidiParagraphDestroy(paragraph);

    /* The context's fallback answers an `Auto` paragraph the text says nothing about. */
    RoboTextDirectionContextInit(&context);
    context.mode = RoboTextDirectionModeAuto;
    context.fallbackDirection = RoboTextDirectionRtl;
    paragraph = BidiResolve("123", &context);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseDirection(paragraph), (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseSource(paragraph), (int)RoboTextBidiBaseFallback);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseWasDefaulted(paragraph), 0);
    RoboTextBidiParagraphDestroy(paragraph);

    /*
     * And with neither a declaration nor a fallback, **P3's level-0 default** is applied and reported.
     * That is the one place 12.8 adds to 12.7's answer, which refuses to claim a direction it has not
     * resolved; a resolution has to have a level, so the answer is 0 and `BaseSource` says why.
     */
    paragraph = BidiResolve("123 456", NULL);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseLevel(paragraph), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseSource(paragraph), (int)RoboTextBidiBaseDefault);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseWasDefaulted(paragraph), 1);
    RoboCheckEqInt((int)RoboTextBidiParagraphGetStats(paragraph, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.baseSource, (int)RoboTextBidiBaseDefault);
    RoboCheckEqInt((int)stats.baseDefaulted, 1);
    RoboTextBidiParagraphDestroy(paragraph);

    /* Empty text: a paragraph with a base and no scalars, and every reading door answers. */
    paragraph = BidiResolve("", NULL);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphScalarCount(paragraph), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphRunCount(paragraph), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphVisualScalarCount(paragraph), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseLevel(paragraph), 0);
    RoboCheck(RoboTextBidiParagraphRun(paragraph, 0u) == NULL);
    RoboCheck(RoboTextBidiParagraphRunAtScalar(paragraph, 0u) == NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphResolvedLevel(paragraph, 0u), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphScalar(paragraph, 0u), (int)RoboTextCodePointReplacement);
    RoboTextBidiParagraphDestroy(paragraph);

    /* The algorithm's own P2/P3 agrees with the base this stage computed, for both bases. */
    {
        static const Uchar32 rtlScalars[] = {0x05E9u, 0x05DCu, 0x05D5u, 0x05DDu};
        static const Uchar32 ltrScalars[] = {'H', 'i'};

        RoboCheckEqInt((int)UnicodeBidiParagraphDirection(rtlScalars, 4u), 1);
        RoboCheckEqInt((int)UnicodeBidiParagraphDirection(ltrScalars, 2u), 0);
    }
}

/* ------------------------------------------------------------------ */
/* Pure paragraphs                                                     */
/* ------------------------------------------------------------------ */

static void TestBidiPureLeftToRight(void)
{
    RoboTextBidiParagraph *paragraph;
    RoboTextBidiStats stats;
    RoboTextDirectionContext layout = BidiLayoutContext();

    ExpectLevels("Hello world", &layout, "0 0 0 0 0 0 0 0 0 0 0");
    ExpectBidiRuns("Hello world", &layout, "ltr@0 0..11");

    paragraph = BidiResolve("Hello world", &layout);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphGetStats(paragraph, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.runs, 1);
    RoboCheckEqInt((int)stats.scalars, 11);
    RoboCheckEqInt((int)stats.visibleScalars, 11);
    RoboCheckEqInt((int)stats.removedScalars, 0);
    RoboCheckEqInt((int)stats.rtlRuns, 0);
    RoboCheckEqInt((int)stats.rtlScalars, 0);
    RoboCheckEqInt((int)stats.maxLevel, 0);
    RoboCheckEqInt((int)stats.mirroredScalars, 0);

    /* One level-0 run is the identity in both orders, so a Latin line cannot be reordered by accident. */
    RoboCheckStrEq(VisualRunsText(paragraph), "0");
    RoboCheckStrEq(VisualScalarsText(paragraph), "0 1 2 3 4 5 6 7 8 9 10");
    RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, 0u)->visualIndex, 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, 0u)->logicalIndex, 0);

    /* A mark is not a boundary and not a level: W1 gives it the type of the character before it. */
    RoboCheckStrEq(LevelsText(paragraph), "0 0 0 0 0 0 0 0 0 0 0");
    RoboTextBidiParagraphDestroy(paragraph);

    /* `"é å"` is five scalars — `e`, U+0301, space, `a`, U+030A — and one level-0 run. */
    ExpectLevels(TextSampleScriptAccents, &layout, "0 0 0 0 0");
    ExpectBidiRuns(TextSampleScriptAccents, &layout, "ltr@0 0..5");
}

static void TestBidiPureRightToLeft(void)
{
    RoboTextBidiParagraph *paragraph;
    RoboTextBidiStats stats;

    /* Four Hebrew scalars: base RTL from the first strong scalar, every level 1, one run. */
    ExpectLevels(TextSampleScriptHebrew, NULL, "1 1 1 1");
    ExpectBidiRuns(TextSampleScriptHebrew, NULL, "rtl@1 0..4");

    paragraph = BidiResolve(TextSampleScriptHebrew, NULL);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphGetStats(paragraph, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.runs, 1);
    RoboCheckEqInt((int)stats.rtlRuns, 1);
    RoboCheckEqInt((int)stats.rtlScalars, 4);
    RoboCheckEqInt((int)stats.maxLevel, 1);
    RoboCheckEqInt((int)stats.visibleScalars, 4);
    RoboCheckEqInt((int)stats.baseLevel, 1);

    /* One run in the visual order, and the scalars inside it read backwards — which is the renderer's
     * business, and the *source* is untouched either way (asserted by the text still being the text). */
    RoboCheckStrEq(VisualRunsText(paragraph), "0");
    RoboCheckStrEq(VisualScalarsText(paragraph), "3 2 1 0");
    RoboCheckStrEq(RoboTextLanguageTag(RoboTextAnalysisLanguage(NULL)), "");
    RoboTextBidiParagraphDestroy(paragraph);

    /* Two Hebrew words: the space is a neutral between two R's, so it is RTL too — one run, not three. */
    ExpectLevels(TextSampleDirectionHebrewSentence, NULL, "1 1 1 1 1 1 1 1 1");
    ExpectBidiRuns(TextSampleDirectionHebrewSentence, NULL, "rtl@1 0..9");

    /* Arabic with a Latin word inside it: the Latin is the embedded run, at level 2. */
    ExpectLevels(TextSampleDirectionMixedRtl, NULL, "1 1 1 1 1 1 2 2 2 2 2");
    ExpectBidiRuns(TextSampleDirectionMixedRtl, NULL, "rtl@1 0..6 | ltr@2 6..11");

    {
        RoboTextBidiParagraph *mixed = BidiResolve(TextSampleDirectionMixedRtl, NULL);

        RoboCheck(mixed != NULL);
        /*
         * The visual order, which is the whole point of the paragraph being RTL: the Latin run paints
         * *first* (leftmost) and the Arabic train takes the rest, and the Latin's own scalars keep their
         * order while the Arabic's are reversed.
         */
        RoboCheckStrEq(VisualRunsText(mixed), "1 0");
        RoboCheckStrEq(VisualScalarsText(mixed), "6 7 8 9 10 5 4 3 2 1 0");
        RoboTextBidiParagraphDestroy(mixed);
    }
}

/* ------------------------------------------------------------------ */
/* Mixed, neutrals and numbers                                         */
/* ------------------------------------------------------------------ */

static void TestBidiMixed(void)
{
    RoboTextBidiParagraph *paragraph;
    RoboTextBidiRun *run;

    /* `"Hello שלום world"`: the Hebrew is a level-1 island inside a level-0 paragraph. */
    ExpectLevels(TextSampleDirectionHebrew, NULL, "0 0 0 0 0 0 1 1 1 1 0 0 0 0 0 0");
    ExpectBidiRuns(TextSampleDirectionHebrew, NULL, "ltr@0 0..6 | rtl@1 6..10 | ltr@0 10..16");

    paragraph = BidiResolve(TextSampleDirectionHebrew, NULL);
    RoboCheck(paragraph != NULL);
    RoboCheckStrEq(VisualRunsText(paragraph), "0 1 2");
    /* Only the Hebrew four are reversed; the spaces stay where they are, on their own sides. */
    RoboCheckStrEq(VisualScalarsText(paragraph), "0 1 2 3 4 5 9 8 7 6 10 11 12 13 14 15");

    /* The lookup a caret uses: the run containing a scalar, and the run at the boundaries. */
    run = (RoboTextBidiRun *)RoboTextBidiParagraphRunAtScalar(paragraph, 0u);
    RoboCheck(run != NULL);
    RoboCheckEqInt((int)run->range.end, 6);
    RoboCheckEqInt((int)RoboTextBidiParagraphRunAtScalar(paragraph, 5u)->level, 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphRunAtScalar(paragraph, 6u)->level, 1);
    RoboCheckEqInt((int)RoboTextBidiParagraphRunAtScalar(paragraph, 9u)->level, 1);
    RoboCheckEqInt((int)RoboTextBidiParagraphRunAtScalar(paragraph, 10u)->level, 0);
    /* The end-of-text position has no run *containing* it — the caret convention every run type uses. */
    RoboCheck(RoboTextBidiParagraphRunAtScalar(paragraph, 16u) == NULL);
    RoboTextBidiParagraphDestroy(paragraph);

    /* The same text left-to-right by declaration: the paragraph is even, the Hebrew still odd. */
    {
        RoboTextDirectionContext declaredLtr = BidiDeclared(RoboTextDirectionLtr);

        ExpectLevels(TextSampleDirectionHebrew, &declaredLtr, "0 0 0 0 0 0 1 1 1 1 0 0 0 0 0 0");
    }

    /*
     * An LTR paragraph holding an RTL island and a number after it. The digits are at **level 2**, not 0
     * and not 1: W7 finds the Hebrew `R` behind them rather than the paragraph's `sor`, so they stay `EN`,
     * and I1 lifts an `EN` at an even level by two. The space before them joins the Hebrew run (level 1)
     * and the space after them does not (it is between `EN`-as-`R` and `L`, so N1 falls back to the
     * paragraph and lands on 0).
     */
    ExpectLevels(TextSampleDirectionLevels, NULL, "0 0 0 0 1 1 1 1 2 2 2 0 0 0 0");
    ExpectBidiRuns(TextSampleDirectionLevels, NULL,
                   "ltr@0 0..4 | rtl@1 4..8 | ltr@2 8..11 | ltr@0 11..15");
}

static void TestBidiNumbers(void)
{
    /* European digits in an RTL paragraph: the digits keep their own order, the space is RTL. */
    ExpectLevels(TextSampleDirectionNumbers, NULL, "1 1 1 1 1 1 2 2 2");
    ExpectBidiRuns(TextSampleDirectionNumbers, NULL, "rtl@1 0..6 | ltr@2 6..9");

    {
        RoboTextBidiParagraph *paragraph = BidiResolve(TextSampleDirectionNumbers, NULL);

        RoboCheck(paragraph != NULL);
        /* The run's own direction is even — left-to-right — so `123` is never reversed (§"numbers"). */
        RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, 1u)->direction, (int)RoboTextDirectionLtr);
        RoboCheckStrEq(VisualScalarsText(paragraph), "6 7 8 5 4 3 2 1 0");
        RoboTextBidiParagraphDestroy(paragraph);
    }

    /* Digits *first*, in an RTL paragraph: still left-to-right, and still to the right of the Arabic. */
    ExpectLevels(TextSampleDirectionNumbersFirst, NULL, "2 2 2 1 1 1 1 1 1");
    ExpectBidiRuns(TextSampleDirectionNumbersFirst, NULL, "ltr@2 0..3 | rtl@1 3..9");
    {
        RoboTextBidiParagraph *paragraph = BidiResolve(TextSampleDirectionNumbersFirst, NULL);

        RoboCheck(paragraph != NULL);
        RoboCheckStrEq(VisualRunsText(paragraph), "1 0");
        RoboTextBidiParagraphDestroy(paragraph);
    }

    /*
     * `AN` is not `EN`: Arabic-Indic digits have their own class, and neither is left-to-right. With no
     * strong scalar anywhere the paragraph takes P3's level-0 default, and I1 lifts an `AN` at an even
     * level by two — so `"١٢٣"` alone is level **2**, which is not a claim that it is Latin.
     */
    ExpectLevels(TextSampleDirectionArabicDigits, NULL, "2 2 2");

    /* The phase's §"numbers and punctuation" list, all in an RTL paragraph. */
    {
        RoboTextDirectionContext rtl = BidiDeclared(RoboTextDirectionRtl);

        ExpectLevels("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7" " 12345", &rtl,
                     "1 1 1 1 1 1 2 2 2 2 2");
        /*
         * `Invoice: 123-456` declared RTL is *all* level 2: the letters are `L` at an odd paragraph level,
         * and W7 makes the number's `EN` left-to-right too (the `L` of `Invoice` is the nearest strong
         * scalar behind the digits), so the hyphen between the two numbers stays `EN` and the whole run is
         * one embedding. An implementation that let the digits take the paragraph's direction would put
         * them at 1, which is the failure this pins.
         */
        ExpectLevels("Invoice: 123-456", &rtl, "2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2");
        /*
         * `(שלום 123) world` with nothing declared: base RTL from the Hebrew. The brackets and the space
         * are level 1 (N1: `EN` counts as `R` and the paragraph answers the disagreement with `world`),
         * the digits are level 2, and `world` is the embedded left-to-right run at level 2 as well.
         */
        ExpectLevels("(\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D" " 123) world", NULL,
                     "1 1 1 1 1 1 2 2 2 1 1 2 2 2 2 2");
    }

    /* `"123 456"` in a left-to-right paragraph is one run at level 0: W7 rewrites the `EN` as `L`. */
    ExpectLevels("123 456", NULL, "0 0 0 0 0 0 0");
    ExpectBidiRuns("123 456", NULL, "ltr@0 0..7");
}

static void TestBidiNeutrals(void)
{
    /* Punctuation between two equal directions takes them; between two differing ones it takes the
     * paragraph — the rule that keeps a trailing comma from splitting a line. */
    ExpectLevels("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D" " 123", NULL, "1 1 1 1 1 2 2 2");
    ExpectLevels(TextSampleDirectionPunctuation, NULL, "0 0 0 0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0");
    ExpectBidiRuns(TextSampleDirectionPunctuation, NULL, "ltr@0 0..7 | rtl@1 7..11 | ltr@0 11..19");

    /* `(123) שלום` — the parentheses resolve RTL with the word they sit beside. */
    ExpectLevels("(123) \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D", NULL, "1 2 2 2 1 1 1 1 1 1");
    /* `שלום (123) world` — mixed on both sides of the bracketed number. */
    ExpectLevels("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D" " (123) world", NULL,
                 "1 1 1 1 1 1 2 2 2 1 1 2 2 2 2 2");
    /*
     * A dash between an Arabic word and an English one: the two sides disagree, so the paragraph answers
     * with `R` — the dash (and the spaces around it) join the Arabic at level 1 — leaving the English at
     * level 2. The dash is the case the phase names: punctuation must not force a run's direction.
     */
    ExpectLevels("\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9" " \xE2\x80\x94 English",
                 NULL, "1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2");
    /* A URL with a Hebrew path: the Latin scheme and the Hebrew path are two directions. */
    ExpectLevels("http://example.com/\xD7\x90\xD7\x91\xD7\x92", NULL,
                 "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1");
}

/* ------------------------------------------------------------------ */
/* Explicit formatting and isolates                                    */
/* ------------------------------------------------------------------ */

static void TestBidiExplicitControls(void)
{
    RoboTextBidiParagraph *paragraph;
    RoboTextBidiStats stats;
    RoboTextDirectionContext layout = BidiLayoutContext();

    /*
     * `LRE abc PDF`: X4 pushes the *least even level greater than the current one*, which in a level-0
     * paragraph is **2** — not 0, and not "no change". (12.7 read this as X2's "least even greater than
     * the embedding level", which is 2 as well; an embedding whose level equals the one it is opened in
     * would not be an embedding.) The controls themselves are **removed** from reordering, which is a
     * state and not level 0, and the two `PDF`s take §5.2's preceding level.
     */
    ExpectLevels("\xE2\x80\xAA" "abc" "\xE2\x80\xAC", &layout, "0 2 2 2 2");
    /*
     * `RLE abc PDF`: level 1 holds the contents, and the `L` scalars inside it go **up** one more by I2 —
     * so the visible levels are 2. That is the whole point of the embedding: left-to-right text laid out
     * in an odd context.
     */
    ExpectLevels("\xE2\x80\xAB" "abc" "\xE2\x80\xAC", &layout, "0 2 2 2 2");

    paragraph = BidiResolve("\xE2\x80\xAA" "abc" "\xE2\x80\xAC", &layout);
    RoboCheck(paragraph != NULL);
    /* `x` is X9's removal, and the raw array is where it shows: the primitives answer "x", not 0. */
    RoboCheckStrEq(RawLevelsText(paragraph), "x 2 2 2 x");
    RoboCheckEqInt((int)RoboTextBidiParagraphScalarIsRemoved(paragraph, 0u), 1);
    RoboCheckEqInt((int)RoboTextBidiParagraphScalarIsRemoved(paragraph, 4u), 1);
    RoboCheckEqInt((int)RoboTextBidiParagraphScalarIsRemoved(paragraph, 1u), 0);
    /* §5.2: a removed scalar takes the level of the scalar before it, so the run list has no gaps. */
    RoboCheckEqInt((int)RoboTextBidiParagraphResolvedLevel(paragraph, 0u), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphResolvedLevel(paragraph, 4u), 2);
    RoboCheckEqInt((int)RoboTextBidiParagraphLevel(paragraph, 0u), (int)RoboTextBidiLevelRemoved);
    RoboCheckEqInt((int)RoboTextBidiParagraphGetStats(paragraph, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.removedScalars, 2);
    RoboCheckEqInt((int)stats.visibleScalars, 3);
    RoboCheckEqInt((int)stats.scalars, 5);
    /* …and the removal is why the visible count is what a painter walks, not the scalar count. */
    RoboCheckEqInt((int)RoboTextBidiParagraphVisualScalarCount(paragraph), 3);
    RoboCheckStrEq(VisualScalarsText(paragraph), "1 2 3");
    RoboTextBidiParagraphDestroy(paragraph);

    /*
     * A right-to-left override really does override: X6 retypes the `L` scalars as `R`, so they stay at
     * the override's own level 1 (I2 leaves an `R` where it is at an odd level) and the run is right-to-
     * left — an override is the one construct that makes Latin text at level 1 without a Latin island.
     */
    ExpectLevels("\xE2\x80\xAE" "abc" "\xE2\x80\xAC", &layout, "0 1 1 1 1");
    ExpectBidiRuns("\xE2\x80\xAE" "abc" "\xE2\x80\xAC", &layout, "ltr@0 0..1 | rtl@1 1..5");
    /*
     * `LRO שלום PDF` is the contrast that shows P2 and the override are *not* the same decision: the
     * Hebrew is a strong `R` when P2 scans the paragraph (the override retypes characters during level
     * resolution, which has not happened yet), so the base is **RTL** — level 1 — and the override then
     * pushes the least even level above it, which is 2. The Hebrew inside is retyped `L`, so it stays at
     * 2 rather than going to 3. A caller that assumed "an LRO makes the paragraph left-to-right" gets the
     * base level assertion below wrong, which is the point of pinning it.
     */
    ExpectLevels("\xE2\x80\xAD" "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D" "\xE2\x80\xAC", &layout,
                 "1 2 2 2 2 2");

    /*
     * Malformed sequences, which must resolve deterministically and never crash: an unmatched `PDF`, an
     * unclosed `RLE`, and a `PDF` with nothing to pop.
     */
    ExpectLevels("abc" "\xE2\x80\xAC", &layout, "0 0 0 0");
    ExpectLevels("\xE2\x80\xAB" "abc", &layout, "0 2 2 2");
    ExpectLevels("\xE2\x80\xAC\xE2\x80\xAC\xE2\x80\xAC", &layout, "0 0 0");
    /*
     * Four embeddings: each pushes the least odd level above the one it opens in — 1, 3, 5, 7 — so the
     * `a` inside the innermost is `L` at level 7 and I2 lifts it to **8**. The four controls take §5.2's
     * preceding level, which is the paragraph's, not the level they opened.
     */
    ExpectLevels("\xE2\x80\xAB\xE2\x80\xAB\xE2\x80\xAB\xE2\x80\xAB" "a", &layout, "0 0 0 0 8");
    /* An unclosed isolate is equally well defined: it runs to the end of the paragraph. */
    ExpectLevels("\xE2\x81\xA7" "abc", &layout, "0 2 2 2");

    /* A paragraph of nothing but controls is all-removed and still has a run to point at. */
    paragraph = BidiResolve("\xE2\x80\xAC\xE2\x80\xAC", &layout);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphRunCount(paragraph), 1);
    RoboCheckEqInt((int)RoboTextBidiParagraphVisualScalarCount(paragraph), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, 0u)->range.start, 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, 0u)->range.end, 2);
    RoboTextBidiParagraphDestroy(paragraph);
}

static void TestBidiIsolates(void)
{
    RoboTextBidiParagraph *paragraph;
    RoboTextDirectionContext layout = BidiLayoutContext();
    RoboTextDirectionContext rtl = BidiDeclared(RoboTextDirectionRtl);

    /*
     * `English [Arabic] English`: the isolate is a directional unit, so the Arabic inside it does not
     * leak its direction into the neutrals beside it — and the isolate's own controls do not paint.
     *
     * The isolations are written as raw escapes because they are invisible: RLI is U+2067, PDI is U+2069.
     */
    {
        RoboTextBidiParagraph *isolated =
            BidiResolve("English " "\xE2\x81\xA7" "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7"
                        "\xE2\x81\xA9" " English",
                        &layout);

        RoboCheck(isolated != NULL);
        /*
         * The two control characters are **not** X9-removed — UAX #9 §5.2 keeps the isolate formatting
         * characters, because they are what determines an isolate's paragraph level — and they have no
         * visible glyph all the same. The two predicates are the two answers, and a painter wants the
         * second.
         */
        RoboCheckEqInt((int)RoboTextBidiParagraphScalarIsRemoved(isolated, 8u), 0);
        RoboCheckEqInt((int)RoboTextBidiParagraphScalarIsRemoved(isolated, 14u), 0);
        RoboCheckEqInt((int)RoboTextBidiParagraphScalarIsDrawable(isolated, 8u), 0);
        RoboCheckEqInt((int)RoboTextBidiParagraphScalarIsDrawable(isolated, 14u), 0);
        RoboCheckEqInt((int)RoboTextBidiScalarIsFormattingControl(RoboTextBidiParagraphScalar(isolated, 8u)),
                       1);
        RoboCheckEqInt((int)RoboTextBidiParagraphScalarIsDrawable(isolated, 9u), 1);
        /* …the surrounding Latin stays at the paragraph's level on both sides… */
        RoboCheckEqInt((int)RoboTextBidiParagraphResolvedLevel(isolated, 0u), 0);
        RoboCheckEqInt((int)RoboTextBidiParagraphResolvedLevel(isolated, 15u), 0);
        /* …and the Arabic inside is right-to-left at an odd level of its own. */
        RoboCheckEqInt((int)RoboTextBidiParagraphResolvedLevel(isolated, 9u), 1);
        RoboCheck(RoboTextBidiLevelIsRightToLeft(RoboTextBidiParagraphResolvedLevel(isolated, 12u)));
        RoboCheckEqInt((int)RoboTextBidiParagraphVisualScalarCount(isolated), 21);
        RoboTextBidiParagraphDestroy(isolated);
    }

    /* An `LRI` inside an RTL paragraph: the URL is left-to-right while the sentence around it is not. */
    {
        RoboTextBidiParagraph *url =
            BidiResolve("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7" " " "\xE2\x81\xA6"
                        "http://example.com" "\xE2\x81\xA9" " world",
                        &rtl);

        RoboCheck(url != NULL);
        RoboCheckEqInt((int)RoboTextBidiParagraphBaseLevel(url), 1);
        RoboCheckEqInt((int)RoboTextBidiParagraphScalarIsDrawable(url, 6u), 0); /* The LRI itself. */
        /* Inside it: the least even level above the paragraph's 1, which is where a URL belongs. */
        RoboCheckEqInt((int)RoboTextBidiParagraphResolvedLevel(url, 7u), 2);
        RoboCheckEqInt((int)RoboTextBidiParagraphResolvedLevel(url, 24u), 2);
        RoboCheckEqInt((int)RoboTextBidiParagraphResolvedLevel(url, 0u), 1);
        RoboTextBidiParagraphDestroy(url);
    }

    /* Nested isolates: a first-strong isolate inside a right-to-left isolate. */
    {
        RoboTextBidiParagraph *nested =
            BidiResolve("a" "\xE2\x81\xA7" "b" "\xE2\x81\xA6" "c" "\xE2\x81\xA9" "d" "\xE2\x81\xA9" "e",
                        &layout);

        RoboCheck(nested != NULL);
        /*
         * Every scalar of the content is Latin, so every resolved level is **even** — 0 outside and 2
         * inside — and the nesting shows up as the levels' *values* rather than their parity: the RLI's
         * content is at 2 (the `L` at the RLI's odd 1 lifted by I2), the LRI inside it pushes to 2 (the
         * least even level above 1), and the last `PDI` returns to the paragraph's 0. This is why the
         * assertion is on the whole level string: a run-based check would pass for a nesting that did
         * nothing at all.
         */
        RoboCheckStrEq(LevelsText(nested), "0 0 2 2 2 2 2 0 0");
        /* The nesting is visible in the runs: three of them, with the level-2 region in the middle. */
        RoboCheckStrEq(RunsText(nested), "ltr@0 0..2 | ltr@2 2..7 | ltr@0 7..9");
        /* None of the four isolate controls draws, and none of them is X9-removed. */
        for (RoboTextScalarIndex index = 1u; index < 8u; index += 2u) {
            RoboCheckEqInt((int)RoboTextBidiParagraphScalarIsDrawable(nested, index), 0);
            RoboCheckEqInt((int)RoboTextBidiParagraphScalarIsRemoved(nested, index), 0);
        }
        RoboTextBidiParagraphDestroy(nested);
    }

    /*
     * `FSI` is first-strong — and the refinement the shipped algorithm makes to §12.8.12 is that "first
     * strong" is settled by applying **P2/P3 to the isolate's own content as if it were a paragraph**, so
     * a content with no strong scalar at all takes P3's level-0 default rather than the enclosing
     * paragraph's direction. All three answers appear in a right-to-left paragraph, which is what makes
     * them tell each other apart:
     *
     *   `FSI abc PDI`   → LRI, content at level 2 (even)
     *   `FSI 123 PDI`   → LRI by P3's default, content at level 2 as well — *not* the paragraph's 1
     *   `FSI مرحبا PDI` → RLI, content at level 3: a real right-to-left run inside a level-2 paragraph
     *
     * The Arabic case is the phase's §"Arabic font test" at the analysis layer: it is the only one of the
     * three that is right-to-left, and the reason is a strong scalar inside the isolate rather than the
     * script or the paragraph.
     */
    {
        RoboTextBidiParagraph *fsiLatin =
            BidiResolve("x" "\xE2\x81\xA8" "abc" "\xE2\x81\xA9" "y", &rtl);
        RoboTextBidiParagraph *fsiDigits =
            BidiResolve("x" "\xE2\x81\xA8" "123" "\xE2\x81\xA9" "y", &rtl);
        RoboTextBidiParagraph *fsiArabic =
            BidiResolve("x" "\xE2\x81\xA8" "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7" "\xE2\x81\xA9"
                        "y",
                        &rtl);

        RoboCheck(fsiLatin != NULL);
        RoboCheck(fsiDigits != NULL);
        RoboCheck(fsiArabic != NULL);
        RoboCheckStrEq(LevelsText(fsiLatin), "2 2 2 2 2 2 2");
        RoboCheckStrEq(LevelsText(fsiDigits), "2 2 2 2 2 2 2");
        RoboCheckStrEq(LevelsText(fsiArabic), "2 2 3 3 3 3 3 2 2");
        RoboCheck(RoboTextBidiLevelIsRightToLeft(RoboTextBidiParagraphResolvedLevel(fsiArabic, 4u)));
        RoboCheckStrEq(RunsText(fsiArabic), "ltr@2 0..2 | rtl@3 2..7 | ltr@2 7..9");
        RoboTextBidiParagraphDestroy(fsiLatin);
        RoboTextBidiParagraphDestroy(fsiDigits);
        RoboTextBidiParagraphDestroy(fsiArabic);
    }

    /*
     * Where an isolate control lands in the run list: the RLI takes §5.2's preceding level, so it sits at
     * the end of the run before it; the `PDI` that closes it is followed by content at a different level
     * and so opens a run of its own. Neither is drawn — the run a control sits in covers it, and the
     * painter skips it — which is why run boundaries and visual slots are two different lists.
     */
    paragraph = BidiResolve("a" "\xE2\x81\xA7" "b" "\xE2\x81\xA9", &layout);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphRunCount(paragraph), 3);
    RoboCheckStrEq(RunsText(paragraph), "ltr@0 0..2 | ltr@2 2..3 | ltr@0 3..4");
    RoboCheckStrEq(VisualScalarsText(paragraph), "0 2");
    RoboTextBidiParagraphDestroy(paragraph);
}

/* ------------------------------------------------------------------ */
/* Mirroring                                                           */
/* ------------------------------------------------------------------ */

static void TestBidiMirroring(void)
{
    RoboTextBidiParagraph *paragraph;

    /*
     * The table itself: sorted by code point (the lookup is a binary search), involutive (a mirror's
     * mirror is the original), and never its own mirror.
     */
    {
        size_t count = BidiMirrorPairCountValue();

        RoboCheck(count > 300u);
        RoboCheck(count == 428u);
        for (size_t index = 0u; index < count; index++) {
            Uchar32 codePoint = 0u;
            Uchar32 mirror = 0u;
            Uchar32 back = 0u;
            char message[128];

            RoboCheck(BidiMirrorPairAt(index, &codePoint, &mirror) == 1);
            snprintf(message, sizeof(message), "U+%04X is its own mirror", (unsigned)codePoint);
            RoboCheckMsg(codePoint != mirror, message);
            snprintf(message, sizeof(message), "U+%04X does not list U+%04X", (unsigned)mirror,
                     (unsigned)codePoint);
            RoboCheckMsg(BidiMirrorLookup(mirror, &back) == 1, message);
            RoboCheckEqInt((int)back, (int)codePoint);
            if (index != 0u) {
                Uchar32 previous = 0u;

                (void)BidiMirrorPairAt(index - 1u, &previous, NULL);
                RoboCheck(previous < codePoint);
            }
        }
        RoboCheck(BidiMirrorPairAt(count, NULL, NULL) == 0);
        /* A code point with no pairing answers itself, and says so. */
        RoboCheckEqInt(BidiMirrorLookup('a', NULL), 0);
        RoboCheckEqInt(BidiMirrorLookup(0x20u, NULL), 0);
    }

    /* The door RoboText adds: the UCD's answer, and the scalar itself when there is none. */
    {
        RoboTextCodePoint mirror = 0u;

        RoboCheckEqInt((int)RoboTextBidiMirrorOf('(', &mirror), 1);
        RoboCheckEqInt((int)mirror, ')');
        RoboCheckEqInt((int)RoboTextBidiMirrorOf(')', &mirror), 1);
        RoboCheckEqInt((int)mirror, '(');
        RoboCheckEqInt((int)RoboTextBidiMirrorOf('<', &mirror), 1);
        RoboCheckEqInt((int)mirror, '>');
        RoboCheckEqInt((int)RoboTextBidiMirrorOf('[', &mirror), 1);
        RoboCheckEqInt((int)mirror, ']');
        RoboCheckEqInt((int)RoboTextBidiMirrorOf('{', &mirror), 1);
        RoboCheckEqInt((int)mirror, '}');
        RoboCheckEqInt((int)RoboTextBidiMirrorOf('a', &mirror), 0);
        RoboCheckEqInt((int)mirror, 'a');
        RoboCheckEqInt((int)RoboTextBidiMirrorOf(0x00ABu, &mirror), 1); /* « */
        RoboCheckEqInt((int)mirror, 0x00BB);                            /* » */
        /* A code point outside Unicode has no row rather than reading off the end of the table. */
        RoboCheckEqInt((int)RoboTextBidiMirrorOf(RoboTextCodePointMax + 1u, &mirror), 0);
        RoboCheckEqInt((int)mirror, (int)(RoboTextCodePointMax + 1u));
        /* NULL is legal: the answer is the return value. */
        RoboCheckEqInt((int)RoboTextBidiMirrorOf('(', NULL), 1);
    }

    /*
     * L4 turns on the *resolved* level, not on the character: `(` in left-to-right text is drawn as
     * itself and in right-to-left text as `)`. The source is never touched.
     */
    paragraph = BidiResolve("(", NULL); /* no strong scalar: P3's level 0 */
    RoboCheck(paragraph != NULL);
    {
        RoboTextCodePoint draw = 0u;

        RoboCheckEqInt((int)RoboTextBidiParagraphMirrors(paragraph, 0u, &draw), 0);
        RoboCheckEqInt((int)draw, '(');
    }
    RoboTextBidiParagraphDestroy(paragraph);

    paragraph = BidiResolve("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D" "(", NULL);
    RoboCheck(paragraph != NULL);
    {
        RoboTextCodePoint draw = 0u;
        RoboTextBidiStats stats;

        /* Scalar 4 is the `(`; the paragraph is RTL because of the Hebrew, so it mirrors. */
        RoboCheckEqInt((int)RoboTextBidiParagraphScalar(paragraph, 4u), '(');
        RoboCheckEqInt((int)RoboTextBidiParagraphMirrors(paragraph, 4u, &draw), 1);
        RoboCheckEqInt((int)draw, ')');
        /* The Hebrew scalars are not mirrored — they are not in the pairing table. */
        RoboCheckEqInt((int)RoboTextBidiParagraphMirrors(paragraph, 0u, &draw), 0);
        RoboCheckEqInt((int)draw, 0x05E9);
        RoboCheckEqInt((int)RoboTextBidiParagraphGetStats(paragraph, &stats), (int)RoboOk);
        RoboCheckEqInt((int)stats.mirroredScalars, 1);
        /* An out-of-range scalar is refused rather than answered. */
        RoboCheckEqInt((int)RoboTextBidiParagraphMirrors(paragraph, 5u, &draw), 0);
    }
    RoboTextBidiParagraphDestroy(paragraph);
}

/* ------------------------------------------------------------------ */
/* Source mapping                                                      */
/* ------------------------------------------------------------------ */

static void TestBidiSourceMapping(void)
{
    RoboTextBidiParagraph *paragraph = BidiResolve("Hello \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D", NULL);
    RoboTextBidiStats stats;

    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphGetStats(paragraph, &stats), (int)RoboOk);

    /*
     * Both directions of the mapping, over every scalar: visual → logical is L2's own output, and
     * logical → visual is the inverse a hit test needs. They must invert each other exactly, which is the
     * property a caret depends on and which no second search can be trusted to give.
     */
    for (RoboTextScalarIndex logical = 0u; logical < RoboTextBidiParagraphScalarCount(paragraph);
         logical++) {
        size_t visual = 0u;
        RoboTextScalarIndex back = 0u;

        RoboCheckEqInt((int)RoboTextBidiParagraphVisualOfScalar(paragraph, logical, &visual, NULL),
                       (int)RoboOk);
        RoboCheck(visual != RoboTextBidiVisualNone);
        RoboCheckEqInt((int)RoboTextBidiParagraphVisualScalar(paragraph, visual, &back, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)back, (int)logical);
    }

    /* A removed scalar has no visual position — it is not painted — and that is not an error. */
    RoboTextBidiParagraphDestroy(paragraph);
    paragraph = BidiResolve("\xE2\x80\xAC" "ab", NULL);
    RoboCheck(paragraph != NULL);
    {
        size_t visual = 123u;
        RoboTextScalarIndex outOfRange = 0u;

        RoboCheckEqInt((int)RoboTextBidiParagraphVisualOfScalar(paragraph, 0u, &visual, NULL), (int)RoboOk);
        RoboCheck(visual == RoboTextBidiVisualNone);
        RoboCheckEqInt((int)RoboTextBidiParagraphVisualOfScalar(paragraph, 1u, &visual, NULL), (int)RoboOk);
        RoboCheckEqInt((int)visual, 0);
        /* Out-of-range indices are refused, in both directions. */
        RoboCheck(RoboTextBidiParagraphVisualOfScalar(paragraph, 3u, &visual, NULL) != RoboOk);
        RoboCheck(RoboTextBidiParagraphVisualScalar(paragraph, 2u, &outOfRange, NULL) != RoboOk);
        RoboCheck(RoboTextBidiParagraphVisualRun(paragraph, 9u, &visual, NULL) != RoboOk);
        RoboCheck(RoboTextBidiParagraphVisualOfScalar(paragraph, 0u, NULL, NULL) != RoboOk);
        RoboCheck(RoboTextBidiParagraphVisualScalar(NULL, 0u, &outOfRange, NULL) != RoboOk);
    }
    RoboTextBidiParagraphDestroy(paragraph);

    /* Every run records both readings: its logical index and where it paints. */
    paragraph = BidiResolve(TextSampleDirectionMixedRtl, NULL);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, 0u)->logicalIndex, 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, 1u)->logicalIndex, 1);
    RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, 0u)->visualIndex, 1);
    RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, 1u)->visualIndex, 0);
    RoboTextBidiParagraphDestroy(paragraph);
}

/* ------------------------------------------------------------------ */
/* The fast path, proved rather than assumed                           */
/* ------------------------------------------------------------------ */

static void TestBidiAsciiFastPath(void)
{
    /*
     * The shortcut `RoboTextBidiScalarNeedsAlgorithm` takes for the ASCII block is *exact*: no ASCII scalar
     * is `R`, `AL`, `AN` or a formatting control, so the only ASCII scalars that need the algorithm are the
     * ten digits. The claim is checked against the table for all 128 rather than trusted, which is the same
     * bargain the script and direction stages make.
     */
    for (RoboTextCodePoint codePoint = 0u; codePoint < 0x80u; codePoint++) {
        UnicodeBidiClass bidiClass = RoboTextUnicodeBidiClass(codePoint);
        int fromTable = (bidiClass == RoboUBcR || bidiClass == RoboUBcAl || bidiClass == RoboUBcEn ||
                         bidiClass == RoboUBcAn || bidiClass == RoboUBcLre || bidiClass == RoboUBcLro ||
                         bidiClass == RoboUBcRle || bidiClass == RoboUBcRlo || bidiClass == RoboUBcPdf ||
                         bidiClass == RoboUBcLri || bidiClass == RoboUBcRli || bidiClass == RoboUBcFsi ||
                         bidiClass == RoboUBcPdi)
                            ? 1
                            : 0;
        char message[96];

        snprintf(message, sizeof(message), "U+%04X: shortcut %d, table %d", (unsigned)codePoint,
                 RoboTextBidiScalarNeedsAlgorithm(codePoint), fromTable);
        RoboCheckMsg(RoboTextBidiScalarNeedsAlgorithm(codePoint) == fromTable, message);
    }
    /* Above the block the table answers, and each answer is the one the algorithm cares about. */
    RoboCheckEqInt((int)RoboTextBidiScalarNeedsAlgorithm(0x05E9u), 1); /* Hebrew R */
    /*
     * U+0640 ARABIC TATWEEL is `Common` by *script* and `AL` by *bidi class* — the two properties are not
     * each other, and this scalar is the cheapest proof of it. So it needs the algorithm, and 12.6's script
     * suite's "tatweel is Common" note is not in tension with this line.
     */
    RoboCheckEqInt((int)RoboTextBidiScalarNeedsAlgorithm(0x0640u), 1);
    RoboCheckEqInt((int)RoboTextBidiScalarNeedsAlgorithm(0x2014u), 0); /* Em dash is ON — genuinely inert */
    RoboCheckEqInt((int)RoboTextBidiScalarNeedsAlgorithm(0x0645u), 1); /* Arabic AL */
    RoboCheckEqInt((int)RoboTextBidiScalarNeedsAlgorithm(0x0661u), 1); /* Arabic-Indic AN */
    RoboCheckEqInt((int)RoboTextBidiScalarNeedsAlgorithm(0x4E2Du), 0); /* Han L */
    RoboCheckEqInt((int)RoboTextBidiScalarNeedsAlgorithm(0x202Au), 1); /* LRE */
    RoboCheckEqInt((int)RoboTextBidiScalarNeedsAlgorithm(0x2066u), 1); /* LRI */
    RoboCheckEqInt((int)RoboTextBidiScalarNeedsAlgorithm(0x2069u), 1); /* PDI */

    /*
     * And the levels the shortcut produces are the algorithm's, over every ASCII scalar **and** over every
     * pair of one ASCII scalar with a representative second one — which is where a wrong shortcut would
     * show up as a level that depends on the character before it.
     */
    {
        static const Uchar32 seconds[] = {0x00u, '0', '5', 'A', 'a', ' ', ')', '#', 0x7Fu};
        Uchar32 scalars[2];
        RoboTextBidiParagraph *paragraph = NULL;
        uint8_t full[2];
        uint8_t paragraphLevel = 0u;

        for (size_t index = 0u; index < 0x80u; index++) {
            scalars[0] = (Uchar32)index;
            scalars[1] = 0u;
            RoboCheckEqInt((int)RoboTextBidiParagraphResolve(scalars, 1u, NULL, &paragraph, NULL),
                           (int)RoboOk);
            RoboCheckEqInt((int)UnicodeBidiResolveLevels(scalars, 1u, RoboBidiLtr, full, &paragraphLevel),
                           (int)RoboOk);
            {
                char message[96];

                snprintf(message, sizeof(message), "U+%04X alone: bidi %u, algorithm %u", (unsigned)index,
                         (unsigned)RoboTextBidiParagraphLevel(paragraph, 0u), (unsigned)full[0]);
                RoboCheckMsg(RoboTextBidiParagraphLevel(paragraph, 0u) == full[0], message);
            }
            RoboTextBidiParagraphDestroy(paragraph);
        }
        for (size_t first = 0u; first < 0x80u; first++) {
            for (size_t second = 0u; second < sizeof(seconds) / sizeof(seconds[0]); second++) {
                scalars[0] = (Uchar32)first;
                scalars[1] = seconds[second];
                RoboCheckEqInt((int)RoboTextBidiParagraphResolve(scalars, 2u, NULL, &paragraph, NULL),
                               (int)RoboOk);
                RoboCheckEqInt(
                    (int)UnicodeBidiResolveLevels(scalars, 2u, RoboBidiLtr, full, &paragraphLevel),
                    (int)RoboOk);
                if (RoboTextBidiParagraphLevel(paragraph, 0u) != full[0] ||
                    RoboTextBidiParagraphLevel(paragraph, 1u) != full[1]) {
                    char message[128];

                    snprintf(message, sizeof(message),
                             "U+%04X U+%04X: bidi %u %u, algorithm %u %u", (unsigned)first,
                             (unsigned)seconds[second],
                             (unsigned)RoboTextBidiParagraphLevel(paragraph, 0u),
                             (unsigned)RoboTextBidiParagraphLevel(paragraph, 1u), (unsigned)full[0],
                             (unsigned)full[1]);
                    RoboCheckMsg(0, message);
                }
                RoboTextBidiParagraphDestroy(paragraph);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Invariants                                                          */
/* ------------------------------------------------------------------ */

/** Every invariant the phase names, over one paragraph. */
static void CheckBidiInvariants(const RoboTextBidiParagraph *paragraph, const char *text)
{
    size_t runs = RoboTextBidiParagraphRunCount(paragraph);
    RoboTextScalarIndex scalars = RoboTextBidiParagraphScalarCount(paragraph);

    if (scalars == 0u) {
        RoboCheckEqInt((int)runs, 0);
        RoboCheckEqInt((int)RoboTextBidiParagraphVisualScalarCount(paragraph), 0);
        return;
    }
    RoboCheck(runs > 0u);
    RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, 0u)->range.start, 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, runs - 1u)->range.end, (int)scalars);
    for (size_t index = 0u; index < runs; index++) {
        const RoboTextBidiRun *run = RoboTextBidiParagraphRun(paragraph, index);
        char message[192];

        snprintf(message, sizeof(message), "\"%s\": run %u is empty", text, (unsigned)index);
        RoboCheckMsg(!RoboTextRangeIsEmpty(run->range), message);
        snprintf(message, sizeof(message), "\"%s\": run %u is inverted", text, (unsigned)index);
        RoboCheckMsg(!RoboTextRangeIsInverted(run->range), message);
        RoboCheck(RoboTextRangeIsWithin(run->range, scalars));
        snprintf(message, sizeof(message), "\"%s\": run %u is at an unresolved level", text,
                 (unsigned)index);
        RoboCheckMsg(RoboTextBidiLevelIsResolved(run->level), message);
        snprintf(message, sizeof(message), "\"%s\": run %u disagrees with its own level", text,
                 (unsigned)index);
        RoboCheckMsg(run->direction == RoboTextBidiLevelDirection(run->level), message);
        RoboCheckEqInt((int)run->logicalIndex, (int)index);
        if (index != 0u) {
            snprintf(message, sizeof(message), "\"%s\": run %u does not follow the one before it", text,
                     (unsigned)index);
            RoboCheckMsg(RoboTextBidiParagraphRun(paragraph, index - 1u)->range.end == run->range.start,
                         message);
            snprintf(message, sizeof(message), "\"%s\": runs %u and %u share a level", text,
                     (unsigned)index - 1u, (unsigned)index);
            RoboCheckMsg(RoboTextBidiParagraphRun(paragraph, index - 1u)->level != run->level, message);
        }
    }

    /* The visual order is a permutation of the runs, and each run's visual index agrees with it. */
    {
        int seen[BidiRunMax];

        RoboCheck(runs <= BidiRunMax);
        memset(seen, 0, sizeof(seen));
        for (size_t visual = 0u; visual < runs; visual++) {
            size_t logical = 0u;

            RoboCheckEqInt((int)RoboTextBidiParagraphVisualRun(paragraph, visual, &logical, NULL),
                           (int)RoboOk);
            RoboCheckEqInt((int)seen[logical], 0);
            seen[logical] = 1;
            RoboCheckEqInt((int)RoboTextBidiParagraphRun(paragraph, logical)->visualIndex, (int)visual);
        }
    }

    /* The scalar-level visual order is a permutation of the visible scalars, and it inverts. */
    {
        size_t visible = RoboTextBidiParagraphVisualScalarCount(paragraph);
        size_t removed = 0u;
        size_t drawable = 0u;
        int seen[BidiLevelBuffer];

        RoboCheck(scalars <= BidiLevelBuffer);
        memset(seen, 0, sizeof(seen));
        for (RoboTextScalarIndex index = 0u; index < scalars; index++) {
            size_t visual = 0u;

            if (RoboTextBidiParagraphScalarIsDrawable(paragraph, index)) {
                drawable++;
            } else {
                /*
                 * Not drawable means not visible, whether X9 removed the scalar (a `BN`, an explicit
                 * embedding, a `PDF`) or §5.2 kept it (an isolate control). Both answers leave it out of
                 * the visual order rather than giving it a slot a painter would have to skip twice.
                 */
                RoboCheckEqInt((int)RoboTextBidiParagraphVisualOfScalar(paragraph, index, &visual, NULL),
                               (int)RoboOk);
                RoboCheck(visual == RoboTextBidiVisualNone);
            }
            if (RoboTextBidiParagraphScalarIsRemoved(paragraph, index)) {
                removed++;
            }
        }
        /* The visible count is the *drawable* count, which is the smaller of the two facts. */
        RoboCheckEqInt((int)visible, (int)drawable);
        RoboCheck(removed <= scalars - drawable);
        for (size_t visual = 0u; visual < visible; visual++) {
            RoboTextScalarIndex logical = 0u;
            size_t back = 0u;

            RoboCheckEqInt((int)RoboTextBidiParagraphVisualScalar(paragraph, visual, &logical, NULL),
                           (int)RoboOk);
            RoboCheck(logical < scalars);
            RoboCheckEqInt((int)seen[logical], 0);
            seen[logical] = 1;
            RoboCheckEqInt((int)RoboTextBidiParagraphVisualOfScalar(paragraph, logical, &back, NULL),
                           (int)RoboOk);
            RoboCheckEqInt((int)back, (int)visual);
        }
    }
}

static void TestBidiInvariants(void)
{
    RoboTextDirectionContext contexts[3];
    size_t contextCount = 0u;

    contexts[contextCount++] = BidiLayoutContext();
    contexts[contextCount++] = BidiDeclared(RoboTextDirectionLtr);
    contexts[contextCount++] = BidiDeclared(RoboTextDirectionRtl);

    /*
     * Every sample, in every context, **and every prefix of it** — a paragraph is re-sliced by a layout pass
     * and by a line breaker, so an invariant that only holds for the whole string is not an invariant.
     */
    for (size_t sample = 0u; sample < BidiSampleCount; sample++) {
        for (size_t contextIndex = 0u; contextIndex < contextCount; contextIndex++) {
            size_t bytes = strlen(kBidiSamples[sample]);

            for (size_t length = 0u; length <= bytes; length++) {
                RoboTextView view = RoboTextViewMake(kBidiSamples[sample], length);
                RoboTextBidiParagraph *paragraph = NULL;

                if (RoboTextBidiParagraphResolveView(view, &contexts[contextIndex], &paragraph, NULL) !=
                    RoboOk) {
                    RoboCheckMsg(0, "the bidi stage refused a prefix it should have resolved");
                    continue;
                }
                CheckBidiInvariants(paragraph, kBidiSamples[sample]);
                RoboTextBidiParagraphDestroy(paragraph);
            }
        }
    }

    /* Determinism: the same text and the same context give the same levels, runs and order, every time. */
    for (size_t sample = 0u; sample < BidiSampleCount; sample++) {
        RoboTextBidiParagraph *first = BidiResolve(kBidiSamples[sample], NULL);
        RoboTextBidiParagraph *second = BidiResolve(kBidiSamples[sample], NULL);
        char message[192];

        RoboCheck(first != NULL);
        RoboCheck(second != NULL);
        if (!first || !second) {
            continue;
        }
        snprintf(message, sizeof(message), "\"%s\": levels differ between runs", kBidiSamples[sample]);
        RoboCheckMsg(strcmp(LevelsText(first), LevelsText(second)) == 0, message);
        snprintf(message, sizeof(message), "\"%s\": runs differ between runs", kBidiSamples[sample]);
        RoboCheckMsg(strcmp(RunsText(first), RunsText(second)) == 0, message);
        snprintf(message, sizeof(message), "\"%s\": visual order differs between runs", kBidiSamples[sample]);
        RoboCheckMsg(strcmp(VisualRunsText(first), VisualRunsText(second)) == 0, message);
        RoboTextBidiParagraphDestroy(first);
        RoboTextBidiParagraphDestroy(second);
    }

    /* Bytes that are not UTF-8 still resolve: the one invalid-UTF-8 policy is the decoder's. */
    {
        static const char broken[] = "a\xFF\xFE" "\xD7\xA9" "\xC3";
        RoboTextBidiParagraph *paragraph = BidiResolve(broken, NULL);

        RoboCheck(paragraph != NULL);
        CheckBidiInvariants(paragraph, "broken bytes");
        RoboTextBidiParagraphDestroy(paragraph);
    }
}

/* ------------------------------------------------------------------ */
/* Integration: the analysis, the shape request and the glyph run      */
/* ------------------------------------------------------------------ */

static void TestBidiShapingIntegration(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontSelector *selector = BidiSelectorOver(&fixture.font);
    RoboTextAnalysis *analysis = NULL;
    RoboTextAnalysisStats stats;
    RoboTextView view = BidiView(TextSampleDirectionHebrew);
    RoboTextShapeOptions options;
    size_t bidiRuns = 0u;

    /*
     * The analysis resolves the paragraph the same way, and its runs are cut on the **resolved** direction:
     * `"Hello שלום world"` is three runs, and the middle one carries level 1 — the direction a shaper is
     * entitled to, rather than a classification.
     */
    RoboCheckEqInt((int)RoboTextAnalysisCreate(view, selector, NULL, &analysis, NULL), (int)RoboOk);
    RoboCheck(analysis != NULL);

    /*
     * Three **level** runs and four **analysis** runs over the same text, which is the phase's §"script +
     * bidi run intersection" in one comparison: `"Hello שלום World"` is cut by the algorithm into
     * `[0,6) lvl 0 | [6,10) lvl 1 | [10,16) lvl 0`, and the space after the Hebrew happens to resolve
     * left-to-right while still belonging to the **Hebrew script run** (12.6's carry rule). So the
     * intersection splits the trailing stretch in two where the algorithm does not, and *that* is the
     * boundary a shaper needs: the space's face is the one the Hebrew run selected.
     */
    bidiRuns = RoboTextAnalysisBidiRunCount(analysis);
    RoboCheckEqInt((int)bidiRuns, 3);
    RoboCheckEqInt((int)RoboTextAnalysisRunCount(analysis), 4);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 1u)->script, (int)RoboUScriptHebrew);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 2u)->script, (int)RoboUScriptHebrew);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 2u)->direction, (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextAnalysisBidiRun(analysis, 1u)->level, 1);
    RoboCheckEqInt((int)RoboTextAnalysisBidiLevelAt(analysis, 6u), 1);
    RoboCheckEqInt((int)RoboTextAnalysisBidiLevelAt(analysis, 0u), 0);
    RoboCheckEqInt((int)RoboTextAnalysisBaseDirection(analysis), (int)RoboTextDirectionLtr);
    RoboCheck(RoboTextAnalysisBidiParagraph(analysis) != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseLevel(RoboTextAnalysisBidiParagraph(analysis)), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseSource(RoboTextAnalysisBidiParagraph(analysis)),
                   (int)RoboTextBidiBaseStrong);

    RoboCheckEqInt((int)RoboTextAnalysisGetStats(analysis, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.bidiRuns, (int)bidiRuns);
    RoboCheckEqInt((int)stats.rtlRuns, 1);
    RoboCheckEqInt((int)stats.rtlScalars, 4);
    RoboCheckEqInt((int)stats.removedScalars, 0);
    RoboCheckEqInt((int)stats.baseLevel, 0);

    /* The visual order the analysis publishes is L2's, over the same level runs. */
    {
        size_t logical = 0u;

        RoboCheckEqInt((int)RoboTextAnalysisVisualBidiRun(analysis, 1u, &logical, NULL), (int)RoboOk);
        RoboCheckEqInt((int)logical, 1);
        RoboCheck(RoboTextAnalysisVisualBidiRun(analysis, 3u, &logical, NULL) != RoboOk);
        RoboCheck(RoboTextAnalysisVisualBidiRun(NULL, 0u, &logical, NULL) != RoboOk);
    }
    RoboCheck(RoboTextAnalysisBidiRun(analysis, bidiRuns) == NULL);
    RoboCheckEqInt((int)RoboTextAnalysisBidiRunCount(NULL), 0);
    RoboCheck(RoboTextAnalysisBidiParagraph(NULL) == NULL);

    /*
     * Every shape request now carries the resolved direction **and** the level, and the level reaches the
     * glyph run — which is the phase's §"shaping integration" read end to end.
     */
    for (size_t index = 0u; index < RoboTextAnalysisRunCount(analysis); index++) {
        const RoboTextAnalysisRun *run = RoboTextAnalysisRunAt(analysis, index);
        RoboTextShapeRequest request;
        RoboTextRun textRun;
        RoboTextGlyphRun *glyphs = NULL;
        int isLatin = run->script == RoboUScriptLatin;

        RoboCheckEqInt((int)RoboTextAnalysisShapeRequest(analysis, index, &request), (int)RoboOk);
        RoboCheckEqInt((int)request.direction, (int)run->direction);
        RoboCheckEqInt((int)request.bidiLevel, (int)run->bidiLevel);
        RoboCheckEqInt((int)run->direction, (int)RoboTextBidiLevelDirection(run->bidiLevel));

        RoboCheckEqInt((int)RoboTextShapeRequestToRun(&request, &textRun), (int)RoboOk);
        RoboCheckEqInt((int)textRun.direction, (int)run->direction);
        RoboCheckEqInt((int)textRun.bidiLevel, (int)run->bidiLevel);

        /*
         * The shipped pack is Inter alone, so only the Latin runs have a face — and the Hebrew ones reach
         * the shaping layer as a right-to-left run with **no face** rather than as a run that pretends.
         * `RoboTextGlyphRunGenerate` refuses a faceless run, which is what makes "no glyph" honest: a
         * `.notdef` box per Hebrew scalar would measure and paint something nobody chose. Adding a Hebrew
         * family is a *pack* change (assets + manifest + licence), not a text-layer one; the phase's
         * §"Hebrew font test" is therefore pinned here at the analysis layer, and the glyph layer is
         * pinned through the mirroring case below and through the Latin runs.
         */
        RoboCheckEqInt((int)(run->font.face != NULL), isLatin);
        if (!isLatin) {
            RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &textRun, &glyphs, NULL),
                           (int)RoboNotSupported);
            RoboCheck(glyphs == NULL);
            continue;
        }
        RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &textRun, &glyphs, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextGlyphRunBidiLevel(glyphs), (int)run->bidiLevel);
        RoboCheckEqInt((int)RoboTextGlyphRunDirection(glyphs), (int)run->direction);
        RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(glyphs).start, (int)run->range.start);
        RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(glyphs).end, (int)run->range.end);
        RoboCheckEqInt((int)RoboTextGlyphRunCount(glyphs), (int)RoboTextRangeLength(run->range));
        RoboCheck(RoboTextGlyphRunAdvance(glyphs) > 0.0f);
        /* One glyph per scalar, each one naming the scalar it came from — the source mapping the phase
         * requires to survive the visual reordering. */
        for (RoboTextScalarIndex scalar = 0u; scalar < RoboTextRangeLength(run->range); scalar++) {
            RoboCheckEqInt((int)RoboTextGlyphRunGlyph(glyphs, (RoboTextGlyphIndex)scalar)->source.start,
                           (int)(run->range.start + scalar));
        }
        RoboTextGlyphRunDestroy(glyphs);
    }

    /*
     * And the formatting-control case, which is §"bidi controls do not paint" reached from the glyph side:
     * `"a LRE b"` generates **three** glyphs — the source mapping stays one-per-scalar — of which the middle
     * one is empty and flagged. It is not a `.notdef`: the face was never asked about the control, so it is
     * not counted as a miss, and the run's advance is the two letters' alone.
     */
    {
        static const char withControl[] = "a" "\xE2\x80\xAA" "b";
        RoboTextRun controlRun;
        RoboTextGlyphRun *controlGlyphs = NULL;
        RoboTextGlyphStats glyphStats;

        memset(&controlRun, 0, sizeof(controlRun));
        controlRun.range = RoboTextRangeMake(0u, 3u);
        controlRun.script = RoboUScriptLatin;
        controlRun.direction = RoboTextDirectionLtr;
        controlRun.bidiLevel = 0u;
        controlRun.font = fixture.font;
        RoboCheckEqInt((int)RoboTextGlyphRunGenerate(BidiView(withControl), &controlRun, &controlGlyphs, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextGlyphRunCount(controlGlyphs), 3);
        RoboCheckEqInt((int)RoboTextGlyphRunControls(controlGlyphs), 1);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyph(controlGlyphs, 1u)->control, 1);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyph(controlGlyphs, 1u)->mapped, 0);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyph(controlGlyphs, 1u)->glyphId, (int)RoboGlyphIdNone);
        RoboCheck(RoboTextGlyphRunGlyph(controlGlyphs, 1u)->advanceX == 0.0f);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyph(controlGlyphs, 1u)->source.start, 1);
        RoboCheckEqInt((int)RoboTextGlyphRunMissingGlyphs(controlGlyphs), 0);
        RoboCheck(RoboTextGlyphRunAdvance(controlGlyphs) > 0.0f);
        RoboCheckEqInt((int)RoboTextGlyphRunGetStats(controlGlyphs, &glyphStats), (int)RoboOk);
        RoboCheckEqInt((int)glyphStats.controls, 1);
        RoboCheckEqInt((int)glyphStats.missing, 0);
        /* The two letters are drawn; only the control is skipped, which is the count a painter reports. */
        RoboCheckEqInt((int)glyphStats.glyphs - (int)glyphStats.controls, 2);
        RoboTextGlyphRunDestroy(controlGlyphs);
    }
    RoboTextAnalysisDestroy(analysis);
    analysis = NULL;

    /*
     * The run set publishes the same resolution beside its classification, and its text runs carry the
     * level. `"123 456"` with nothing declared is the contrast the phase is about: the direction runs say
     * `Unknown` (12.7's refusal) and the text run says level 0 (12.8's answer).
     */
    {
        RoboTextRunSet *set = NULL;
        RoboTextRunStats runStats;

        RoboCheckEqInt((int)RoboTextRunSetCreate(BidiView("123 456"), &fixture.font, NULL, &set, NULL),
                       (int)RoboOk);
        RoboCheck(set != NULL);
        RoboCheckEqInt((int)RoboTextRunSetBaseDirection(set), (int)RoboTextDirectionUnknown);
        RoboCheckEqInt((int)RoboTextRunSetBaseLevel(set), 0);
        RoboCheckEqInt((int)RoboTextRunSetBaseSource(set), (int)RoboTextBidiBaseDefault);
        RoboCheck(RoboTextRunSetBidiParagraph(set) != NULL);
        RoboCheckEqInt((int)RoboTextRunSetBidiRunCount(set), 1);
        RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->bidiLevel, 0);
        RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->direction, (int)RoboTextDirectionLtr);
        RoboCheckEqInt((int)RoboTextRunSetDirectionRun(set, 0u)->direction, (int)RoboTextDirectionUnknown);
        RoboCheckEqInt((int)RoboTextRunSetGetStats(set, &runStats), (int)RoboOk);
        RoboCheckEqInt((int)runStats.bidiRuns, 1);
        RoboCheckEqInt((int)runStats.unknownDirectionRuns, 1);
        {
            size_t logical = 0u;

            RoboCheckEqInt((int)RoboTextRunSetVisualBidiRun(set, 0u, &logical, NULL), (int)RoboOk);
            RoboCheckEqInt((int)logical, 0);
            RoboCheck(RoboTextRunSetVisualBidiRun(set, 1u, &logical, NULL) != RoboOk);
        }
        /* The run set's declarations carry into the *text* run, and the source is never reordered. */
        RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->range.start, 0);
        RoboCheckEqInt((int)RoboTextRunSetTextRun(set, 0u)->range.end, 7);
        RoboTextRunSetDestroy(set);

        /* An empty run set still has a paragraph, so its base level answers without a special case. */
        RoboCheckEqInt((int)RoboTextRunSetCreate(RoboTextViewMake(NULL, 0u), &fixture.font, NULL, &set,
                                                 NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextRunSetBaseLevel(set), 0);
        RoboCheckEqInt((int)RoboTextRunSetBaseSource(set), (int)RoboTextBidiBaseDefault);
        RoboCheckEqInt((int)RoboTextRunSetBidiRunCount(set), 0);
        RoboCheckEqInt((int)RoboTextRunSetVisualBidiRun(set, 0u, NULL, NULL), (int)RoboInvalidArgument);
        RoboTextRunSetDestroy(set);
    }

    /*
     * L4 at the glyph level, which is where the phase asks for it: a right-to-left run draws `(` with the
     * glyph of `)`, and the glyph's `source` still names the scalar the author wrote.
     */
    {
        RoboTextView paren = BidiView("(");
        RoboTextView close = BidiView(")");
        RoboTextRun leftToRight;
        RoboTextRun rightToLeft;
        RoboTextGlyphRun *mirrored = NULL;
        RoboTextGlyphRun *plain = NULL;
        RoboTextGlyphStats glyphStats;

        memset(&leftToRight, 0, sizeof(leftToRight));
        leftToRight.range = RoboTextRangeMake(0u, 1u);
        leftToRight.script = RoboUScriptCommon;
        leftToRight.direction = RoboTextDirectionLtr;
        leftToRight.bidiLevel = 0;
        leftToRight.baseDirection = RoboTextDirectionLtr;
        leftToRight.font = fixture.font;

        rightToLeft = leftToRight;
        rightToLeft.direction = RoboTextDirectionRtl;
        rightToLeft.bidiLevel = 1;

        RoboCheckEqInt((int)RoboTextGlyphRunGenerate(paren, &rightToLeft, &mirrored, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextGlyphRunGenerate(close, &leftToRight, &plain, NULL), (int)RoboOk);

        /* The mirrored run's glyph is the glyph of `)`, and its source is still `(`. */
        RoboCheckEqInt((int)RoboTextGlyphRunGlyph(mirrored, 0u)->glyphId,
                       (int)RoboTextGlyphRunGlyph(plain, 0u)->glyphId);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyph(mirrored, 0u)->mirrored, 1);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyph(mirrored, 0u)->source.start, 0);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyph(mirrored, 0u)->source.end, 1);
        RoboCheckEqInt((int)RoboTextGlyphRunMirrored(mirrored), 1);
        RoboCheckEqInt((int)RoboTextGlyphRunGetStats(mirrored, &glyphStats), (int)RoboOk);
        RoboCheckEqInt((int)glyphStats.mirrored, 1);
        /* The source *text* is unchanged: the glyph run says the range is the one we gave it. */
        RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(mirrored).end, 1);

        /* Left-to-right text is not mirrored, and neither is a mirrored run with no pairing to apply. */
        RoboCheckEqInt((int)RoboTextGlyphRunMirrored(plain), 0);
        RoboCheckEqInt((int)RoboTextGlyphRunGlyph(plain, 0u)->mirrored, 0);
        RoboTextGlyphRunDestroy(mirrored);
        RoboTextGlyphRunDestroy(plain);

        {
            RoboTextView word = BidiView("ab");
            RoboTextRun rtlWord;
            RoboTextGlyphRun *letters = NULL;

            memset(&rtlWord, 0, sizeof(rtlWord));
            rtlWord.range = RoboTextRangeMake(0u, 2u);
            rtlWord.script = RoboUScriptLatin;
            rtlWord.direction = RoboTextDirectionRtl;
            rtlWord.bidiLevel = 1;
            rtlWord.font = fixture.font;
            RoboCheckEqInt((int)RoboTextGlyphRunGenerate(word, &rtlWord, &letters, NULL), (int)RoboOk);
            RoboCheckEqInt((int)RoboTextGlyphRunMirrored(letters), 0);
            /* The glyph order is logical: nothing reorders the glyph vector, direction included. */
            RoboCheckEqInt((int)RoboTextGlyphRunGlyph(letters, 0u)->source.start, 0);
            RoboCheckEqInt((int)RoboTextGlyphRunGlyph(letters, 1u)->source.start, 1);
            RoboTextGlyphRunDestroy(letters);
        }
    }

    /*
     * The declaration a shell makes, which §28 asks to be preserved: an explicit left-to-right paragraph
     * holds a right-to-left run without becoming one.
     */
    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    RoboCheckEqInt((int)RoboTextAnalysisCreate(view, selector, &options, &analysis, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextAnalysisBaseDirection(analysis), (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 1u)->direction, (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 1u)->bidiLevel, 1);
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 0u)->bidiLevel, 0);
    RoboTextAnalysisDestroy(analysis);

    RoboTextFontSelectorDestroy(selector);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static void TestBidiDiagnostics(void)
{
    static const char text[] = "Hello \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"; /* Hello שלום */
    RoboTextView view = BidiView(text);
    RoboTextBidiParagraph *paragraph = BidiResolve(text, NULL);
    RoboTextBidiRun run;
    char line[64];
    /* Sized for the dump's own cap (`RoboTextBidiDumpScalarLimit` scalars) plus the runs and the header,
     * so the long-paragraph case below exercises the cap rather than the buffer. */
    char table[8192];
    size_t length = 0u;

    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphRunCount(paragraph), 2);

    /* One run as a line. */
    run.range = RoboTextRangeMake(0u, 6u);
    run.level = 0;
    run.direction = RoboTextDirectionLtr;
    run.logicalIndex = 0u;
    run.visualIndex = 0u;
    RoboCheckEqInt((int)RoboTextBidiRunWrite(&run, line, sizeof(line), &length), (int)RoboOk);
    RoboCheckStrEq(line, "[0..6) lvl 0 ltr");
    RoboCheckEqInt((int)length, (int)strlen("[0..6) lvl 0 ltr"));
    RoboCheckEqInt((int)RoboTextBidiRunWrite(&run, line, 4u, &length), (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextBidiRunWrite(NULL, line, sizeof(line), NULL), (int)RoboInvalidArgument);

    /*
     * The whole dump, byte for byte. The per-scalar section is what makes a mixed-direction bug findable:
     * every scalar's index, code point, `Bidi_Class` and resolved level, so a reader can see *where* the
     * algorithm changed its mind rather than only that the runs came out wrong.
     */
    RoboCheckEqInt((int)RoboTextBidiParagraphWrite(paragraph, view, table, sizeof(table), &length),
                   (int)RoboOk);
    RoboCheckStrEq(table,
                   "Bidi Analysis\n"
                   "base: ltr level 0 (strong)  scalars 10  runs 2  visible 10\n"
                   "[0..6) scalars  bytes [0..6)  lvl 0  ltr\n"
                   "[6..10) scalars  bytes [6..14)  lvl 1  rtl\n"
                   "visual: 0 1\n"
                   "scalars:\n"
                   "  #0  U+0048  L    lvl 0\n"
                   "  #1  U+0065  L    lvl 0\n"
                   "  #2  U+006C  L    lvl 0\n"
                   "  #3  U+006C  L    lvl 0\n"
                   "  #4  U+006F  L    lvl 0\n"
                   "  #5  U+0020  WS    lvl 0\n"
                   "  #6  U+05E9  R    lvl 1\n"
                   "  #7  U+05DC  R    lvl 1\n"
                   "  #8  U+05D5  R    lvl 1\n"
                   "  #9  U+05DD  R    lvl 1\n");
    RoboCheckEqInt((int)length, (int)strlen(table));

    /* A buffer that cannot hold the whole table is left empty and told what it needed. */
    {
        char small[24];
        size_t needed = 0u;

        memset(small, 'x', sizeof(small));
        RoboCheckEqInt((int)RoboTextBidiParagraphWrite(paragraph, view, small, sizeof(small), &needed),
                       (int)RoboNotSupported);
        /* Complete-or-absent: a refused dump leaves the buffer empty rather than holding a prefix. */
        RoboCheckEqInt((int)small[0], 0);
        RoboCheck(needed > sizeof(small));
        /* And the reported need is the real length of the table it would have written. */
        RoboCheck(needed < sizeof(table));
        RoboCheckEqInt((int)RoboTextBidiParagraphWrite(paragraph, view, small, sizeof(small), NULL),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)small[0], 0);
        RoboCheckEqInt((int)RoboTextBidiParagraphWrite(NULL, view, small, sizeof(small), NULL),
                       (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextBidiParagraphWrite(paragraph, view, NULL, 0u, NULL),
                       (int)RoboInvalidArgument);
    }

    /* Without a view the byte column is absent rather than wrong. */
    RoboCheckEqInt((int)RoboTextBidiParagraphWrite(paragraph, RoboTextViewMake(NULL, 0u), table,
                                                   sizeof(table), &length),
                   (int)RoboOk);
    RoboCheck(strstr(table, "bytes [") == NULL);
    RoboCheck(strstr(table, "Bidi Analysis\nbase: ltr level 0 (strong)  scalars 10  runs 2") == table);

    /* `x` in the level column is a scalar X9 removed, which is the one thing a level dump must not lie
     * about. */
    RoboTextBidiParagraphDestroy(paragraph);
    paragraph = BidiResolve("a" "\xE2\x80\xAA" "b", NULL);
    RoboCheck(paragraph != NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphWrite(paragraph, BidiView("a" "\xE2\x80\xAA" "b"), table,
                                                   sizeof(table), &length),
                   (int)RoboOk);
    RoboCheck(strstr(table, "  #1  U+202A  LRE    lvl x\n") != NULL);
    RoboCheck(strstr(table, "(strong)") != NULL);
    RoboTextBidiParagraphDestroy(paragraph);

    /* A paragraph longer than the scalar cap prints its runs and says how many lines it left out. */
    {
        static char longText[512];
        size_t at = 0u;

        while (at + 2u < sizeof(longText)) {
            longText[at++] = 'a';
        }
        longText[at] = '\0';
        paragraph = BidiResolve(longText, NULL);
        RoboCheck(paragraph != NULL);
        /*
         * The table is capped at `RoboTextBidiDumpScalarLimit` scalars and says so, which is what keeps a
         * dump of a 512-scalar paragraph inside a caller's stack buffer. The buffer below is sized for the
         * cap rather than for the text; a buffer that is *short* is the case the assertion above covers,
         * where the door answers complete-or-absent.
         */
        RoboCheckEqInt((int)RoboTextBidiParagraphWrite(paragraph, BidiView(longText), table, sizeof(table),
                                                       &length),
                       (int)RoboOk);
        RoboCheck(strstr(table, "more scalars not printed") != NULL);
        RoboCheckEqInt((int)RoboTextBidiParagraphScalarCount(paragraph), (int)at);
        RoboTextBidiParagraphDestroy(paragraph);
    }
}

/* ------------------------------------------------------------------ */
/* Cost                                                                */
/* ------------------------------------------------------------------ */

static void TestBidiPerformance(void)
{
    static char paragraph[20480];
    static char mixed[20480];
    size_t asciiWritten = 0u;
    size_t mixedWritten = 0u;
    RoboTextBidiParagraph *asciiSmall = NULL;
    RoboTextBidiParagraph *asciiLarge = NULL;
    RoboTextBidiParagraph *mixedParagraph = NULL;
    MemStats before;
    MemStats after;
    MemStats smallStats;
    MemStats mid;
    MemStats largeStats;
    size_t smallAllocs = 0u;
    size_t largeAllocs = 0u;
    clock_t started;
    clock_t finished;
    long asciiMs = 0;
    long mixedMs = 0;
    size_t mixedRuns = 0u;
    RoboTextDirectionContext layout = BidiLayoutContext();

    while (asciiWritten + sizeof(TextSamplePangram) - 1u < sizeof(paragraph) - 2u) {
        memcpy(paragraph + asciiWritten, TextSamplePangram, sizeof(TextSamplePangram) - 1u);
        asciiWritten += sizeof(TextSamplePangram) - 1u;
        paragraph[asciiWritten++] = ' ';
    }
    paragraph[asciiWritten] = '\0';

    /*
     * Cost is recorded rather than asserted, and the one assertion is the phase's §"performance" rule in a
     * form that cannot flake: **the allocation count does not grow with the text**. One paragraph call is a
     * fixed number of allocations — the paragraph, the scalars, the level array, the §5.2 array, the run
     * array, the visual orders and the inverse map — however many scalars it covers, and never one per
     * code point.
     *
     * The two counts are *deltas*, each measured around one call with nothing else alive, because a
     * cumulative reading would count the paragraph resolved earlier as well and "9 then 18" is exactly
     * what that mistake looks like.
     */
    MemDebugGetStats(&before);
    RoboCheckEqInt((int)RoboTextBidiParagraphResolveView(BidiView("Hello world"), &layout, &asciiSmall, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextBidiParagraphScalarCount(asciiSmall), 11);
    MemDebugGetStats(&smallStats);
    smallAllocs = smallStats.totalAllocs - before.totalAllocs;
    RoboTextBidiParagraphDestroy(asciiSmall);
    asciiSmall = NULL;
    MemDebugGetStats(&mid); /* The baseline for the second delta: nothing of ours is alive here. */
    RoboCheckEqInt((int)RoboTextBidiParagraphResolveView(BidiView(paragraph), &layout, &asciiLarge, NULL),
                   (int)RoboOk);
    MemDebugGetStats(&largeStats);
    largeAllocs = largeStats.totalAllocs - mid.totalAllocs;
    RoboCheckEqInt((int)RoboTextBidiParagraphScalarCount(asciiLarge), (int)asciiWritten);
    RoboCheckEqInt((int)RoboTextBidiParagraphRunCount(asciiLarge), 1);
    RoboCheckEqInt((int)RoboTextBidiParagraphRun(asciiLarge, 0u)->level, 0);

    started = clock();
    for (int pass = 0; pass < 8; pass++) {
        RoboTextBidiParagraphDestroy(asciiLarge);
        asciiLarge = NULL;
        RoboCheckEqInt((int)RoboTextBidiParagraphResolveView(BidiView(paragraph), &layout, &asciiLarge, NULL),
                       (int)RoboOk);
    }
    finished = clock();
    asciiMs = (long)((finished - started) * 1000L / CLOCKS_PER_SEC);
    RoboTextBidiParagraphDestroy(asciiLarge);
    asciiLarge = NULL;

    /* A mixed 10–20 KB paragraph: the same allocation count, and a run count that grows with the
     * *boundaries* rather than with the bytes. */
    {
        size_t latinStep = sizeof("RoboticelaOS text ") - 1u;
        size_t hebrewStep = sizeof(TextSampleScriptHebrew " ") - 1u;

        while (mixedWritten + latinStep + hebrewStep < sizeof(mixed) - 2u) {
            memcpy(mixed + mixedWritten, "RoboticelaOS text ", latinStep);
            mixedWritten += latinStep;
            memcpy(mixed + mixedWritten, TextSampleScriptHebrew " ", hebrewStep);
            mixedWritten += hebrewStep;
        }
        mixed[mixedWritten] = '\0';

        started = clock();
        RoboCheckEqInt((int)RoboTextBidiParagraphResolveView(BidiView(mixed), &layout, &mixedParagraph, NULL),
                       (int)RoboOk);
        finished = clock();
        mixedMs = (long)((finished - started) * 1000L / CLOCKS_PER_SEC);
        RoboCheck(mixedParagraph != NULL);
        mixedRuns = RoboTextBidiParagraphRunCount(mixedParagraph);
        RoboCheck(mixedRuns > 1000u);
        /*
         * The paragraph's scalar count is the **decoder's** count, not the byte count: this text is Latin
         * and Hebrew, so it has fewer scalars than bytes, and asserting the two were equal would be
         * asserting that the Hebrew was not there.
         */
        {
            Uchar32 *scalars = NULL;
            RoboTextScalarIndex decoded = 0u;

            RoboCheckEqInt((int)RoboTextUnicodeDecodeAll(BidiView(mixed), &scalars, &decoded, NULL),
                           (int)RoboOk);
            RoboCheckEqInt((int)RoboTextBidiParagraphScalarCount(mixedParagraph), (int)decoded);
            RoboCheck(decoded < mixedWritten);
            RoboTextUnicodeScalarsFree(scalars);
        }
        RoboTextBidiParagraphDestroy(mixedParagraph);
        mixedParagraph = NULL;
    }
    MemDebugGetStats(&after);

    /* Both sizes cost the same number of allocations: the property, stated as an equality. */
    RoboCheck(smallAllocs != 0u);
    RoboCheckEqInt((int)smallAllocs, (int)largeAllocs);
    /* And every allocation was returned, which is the other half of "a paragraph owns its own storage". */
    RoboCheckEqInt((int)(after.totalFrees - before.totalFrees), (int)(after.totalAllocs - before.totalAllocs));
    RoboCheckEqInt((int)after.totalAllocs, (int)after.totalFrees);

    printf("text.bidi.performance: ascii %u bytes -> 1 run in %ld ms (8 passes); mixed %u bytes -> %u runs "
           "in %ld ms; %u allocations per paragraph\n",
           (unsigned)asciiWritten, asciiMs, (unsigned)mixedWritten, (unsigned)mixedRuns, mixedMs,
           (unsigned)largeAllocs);
    RoboCheck(asciiWritten >= 10240u);
    RoboCheck(mixedWritten >= 10240u);
}

/* ------------------------------------------------------------------ */
/* Refusals                                                            */
/* ------------------------------------------------------------------ */

static void TestBidiRefusals(void)
{
    RoboTextBidiParagraph *paragraph = NULL;
    RoboTextDirectionContext context;
    RoboTextBidiStats stats;
    size_t visual = 0u;

    /* A NULL out pointer, and scalars that are absent while a count says otherwise. */
    RoboCheckEqInt((int)RoboTextBidiParagraphResolveView(BidiView("a"), NULL, NULL, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextBidiParagraphResolve(NULL, 4u, NULL, &paragraph, NULL),
                   (int)RoboInvalidArgument);
    RoboCheck(RoboTextBidiParagraphResolve(NULL, 0u, NULL, NULL, NULL) != RoboOk);
    /* A view with a length but no bytes is refused rather than read. */
    RoboCheck(RoboTextBidiParagraphResolveView(RoboTextViewMake(NULL, 5u), NULL, &paragraph, NULL) !=
              RoboOk);
    /* An unusable context is refused. */
    RoboTextDirectionContextInit(&context);
    context.mode = (RoboTextDirectionMode)9;
    RoboCheck(RoboTextBidiParagraphResolveView(BidiView("a"), &context, &paragraph, NULL) != RoboOk);

    /* Reading doors on a NULL paragraph answer rather than crash — the vocabulary's own defaults. */
    RoboCheckEqInt((int)RoboTextBidiParagraphScalarCount(NULL), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseDirection(NULL), (int)RoboTextDirectionUnknown);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseLevel(NULL), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphBaseWasDefaulted(NULL), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphRunCount(NULL), 0);
    RoboCheck(RoboTextBidiParagraphRun(NULL, 0u) == NULL);
    RoboCheck(RoboTextBidiParagraphRunAtScalar(NULL, 0u) == NULL);
    RoboCheckEqInt((int)RoboTextBidiParagraphLevel(NULL, 0u), (int)RoboTextBidiLevelRemoved);
    RoboCheckEqInt((int)RoboTextBidiParagraphResolvedLevel(NULL, 0u), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphScalarIsRemoved(NULL, 0u), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphScalar(NULL, 0u), (int)RoboTextCodePointReplacement);
    RoboCheckEqInt((int)RoboTextBidiParagraphVisualScalarCount(NULL), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphMirrors(NULL, 0u, NULL), 0);
    RoboCheckEqInt((int)RoboTextBidiParagraphGetStats(NULL, &stats), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextBidiParagraphVisualRun(NULL, 0u, &visual, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextBidiParagraphVisualRun(NULL, 0u, NULL, NULL), (int)RoboInvalidArgument);
    RoboTextBidiParagraphDestroy(NULL); /* Safe. */
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterTextBidi(void)
{
    TestRegister("text.bidi.levels_vocabulary", TestBidiLevelVocabulary);
    TestRegister("text.bidi.paragraph", TestBidiParagraphBase);
    TestRegister("text.bidi.pure_ltr", TestBidiPureLeftToRight);
    TestRegister("text.bidi.pure_rtl", TestBidiPureRightToLeft);
    TestRegister("text.bidi.mixed", TestBidiMixed);
    TestRegister("text.bidi.numbers", TestBidiNumbers);
    TestRegister("text.bidi.neutrals", TestBidiNeutrals);
    TestRegister("text.bidi.explicit", TestBidiExplicitControls);
    TestRegister("text.bidi.isolates", TestBidiIsolates);
    TestRegister("text.bidi.mirroring", TestBidiMirroring);
    TestRegister("text.bidi.source_mapping", TestBidiSourceMapping);
    TestRegister("text.bidi.ascii_fast_path", TestBidiAsciiFastPath);
    TestRegister("text.bidi.invariants", TestBidiInvariants);
    TestRegister("text.bidi.shaping_integration", TestBidiShapingIntegration);
    TestRegister("text.bidi.diagnostics", TestBidiDiagnostics);
    TestRegister("text.bidi.performance", TestBidiPerformance);
    TestRegister("text.bidi.refusals", TestBidiRefusals);
}
