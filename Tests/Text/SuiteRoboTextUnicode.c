/*
 * RoboText's Unicode integration (Phase 12.5) — `text.unicode.*`.
 *
 * The suite is deliberately written against the **shipped** Unicode tables rather than against literals
 * this file invents: a script classification case asserts `RoboUScriptCyrillic`, which is RoboUnicode's own
 * enumerator, so the day the UCD data is regenerated the test fails if the answer changed rather than if a
 * number did. The same is true of the decoder: every expectation is a *policy* (an ill-formed byte is one
 * replacement and one byte of progress) rather than a byte count copied from one run.
 *
 * Two properties are worth pointing at because they are the phase's requirements rather than this build's:
 *
 *   **The decoder is the existing one.** `UnicodeUtf8Decode` in `RoboUnicodeReplace` mode, wrapped. The
 *   cases below check that this module's iterator agrees with it byte for byte, which is what makes "no
 *   second decoder" a fact a test can see rather than a claim in a document.
 *
 *   **Script resolution is one implementation.** `RoboTextScriptRunsResolve` is called by the run set and
 *   by the analysis layer; the neutrality cases here are the same ones `text.runs.neutrality` asserts, and
 *   they must keep passing through both consumers.
 */

#include "RoboTextTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static RoboTextView ViewOf(const char *text)
{
    return RoboTextViewMake(text, strlen(text));
}

/* The script samples the phase names, as UTF-8 (see the fixture's `TextSampleUnicode` note). */
#define SampleLatin "A"
#define SampleCyrillic "\xD0\x9F"                     /* П U+041F */
#define SampleGreek "\xCE\xA9"                        /* Ω U+03A9 */
#define SampleArabic "\xD9\x85"                       /* م U+0645 */
#define SampleHebrew "\xD7\xA9"                       /* ש U+05E9 */
#define SampleDevanagari "\xE0\xA4\xB9"               /* ह U+0939 */
#define SampleHan "\xE4\xB8\xAD"                      /* 中 U+4E2D */
#define SampleHiragana "\xE3\x81\x82"                 /* あ U+3042 */
#define SampleKatakana "\xE3\x82\xA2"                 /* ア U+30A2 */
#define SampleHangul "\xED\x95\x9C"                   /* 한 U+D55C */
#define SampleCombiningAcute "\xCC\x81"               /* ́ U+0301 */
#define SampleVariationSelector "\xF3\xA0\x84\x80"    /* U+E0100, a combining mark Inter does not cover */

static void TestCodePoints(void)
{
    /* The code-point test and the scalar test differ by exactly the surrogate range. */
    RoboCheck(RoboTextCodePointIsValid(0u));
    RoboCheck(RoboTextCodePointIsValid(RoboTextCodePointMax));
    RoboCheck(!RoboTextCodePointIsValid(RoboTextCodePointMax + 1u));
    RoboCheck(!RoboTextCodePointIsValid(0xFFFFFFFFu));

    RoboCheck(RoboTextCodePointIsScalar(0u));
    RoboCheck(RoboTextCodePointIsScalar(0xD7FFu));
    RoboCheck(!RoboTextCodePointIsScalar(0xD800u));
    RoboCheck(!RoboTextCodePointIsScalar(0xDFFFu));
    RoboCheck(RoboTextCodePointIsScalar(0xE000u));
    RoboCheck(RoboTextCodePointIsScalar(RoboTextCodePointMax));

    RoboCheck(RoboTextCodePointIsHighSurrogate(0xD800u));
    RoboCheck(RoboTextCodePointIsHighSurrogate(0xDBFFu));
    RoboCheck(!RoboTextCodePointIsHighSurrogate(0xDC00u));
    RoboCheck(RoboTextCodePointIsLowSurrogate(0xDC00u));
    RoboCheck(RoboTextCodePointIsLowSurrogate(0xDFFFu));
    RoboCheck(!RoboTextCodePointIsLowSurrogate(0xD800u));
    RoboCheck(RoboTextCodePointIsSurrogate(0xD800u));
    RoboCheck(RoboTextCodePointIsSurrogate(0xDFFFu));
    RoboCheck(!RoboTextCodePointIsSurrogate(0x0041u));

    /* `ToScalar` never invents a character: a surrogate or an out-of-range value becomes U+FFFD. */
    RoboCheckEqInt((int)RoboTextCodePointToScalar(0x0041u), 0x41);
    RoboCheckEqInt((int)RoboTextCodePointToScalar(0xD800u), (int)RoboTextCodePointReplacement);
    RoboCheckEqInt((int)RoboTextCodePointToScalar(RoboTextCodePointMax + 1u),
                   (int)RoboTextCodePointReplacement);

    /* Noncharacters are valid code points that no conforming font ships a glyph for. */
    RoboCheck(RoboTextCodePointIsNoncharacter(0xFDD0u));
    RoboCheck(RoboTextCodePointIsValid(0xFDD0u));
    RoboCheck(RoboTextCodePointIsNoncharacter(0xFFFFu));
    RoboCheck(RoboTextCodePointIsNoncharacter(0x10FFFFu));
    RoboCheck(!RoboTextCodePointIsNoncharacter(0x0041u));
    /* U+FDD0..U+FDEF is the *whole* permanent noncharacter block, so the next value after it is not one. */
    RoboCheck(RoboTextCodePointIsNoncharacter(0xFDEFu));
    RoboCheck(!RoboTextCodePointIsNoncharacter(0xFDF0u));
}

static void TestProperties(void)
{
    int index;

    /* Category groups: one representative per group, and the grouping never loses the detail. */
    RoboCheckEqInt((int)RoboTextUnicodeCategory('A'), (int)RoboTextCategoryLetter);
    RoboCheckEqInt((int)RoboTextUnicodeGeneralCategory('A'), (int)RoboUGcLu);
    RoboCheckEqInt((int)RoboTextUnicodeCategory('a'), (int)RoboTextCategoryLetter);
    RoboCheckEqInt((int)RoboTextUnicodeGeneralCategory('a'), (int)RoboUGcLl);
    RoboCheckEqInt((int)RoboTextUnicodeCategory('7'), (int)RoboTextCategoryNumber);
    RoboCheckEqInt((int)RoboTextUnicodeCategory('!'), (int)RoboTextCategoryPunctuation);
    RoboCheckEqInt((int)RoboTextUnicodeCategory('+'), (int)RoboTextCategorySymbol);
    RoboCheckEqInt((int)RoboTextUnicodeCategory(' '), (int)RoboTextCategorySeparator);
    RoboCheckEqInt((int)RoboTextUnicodeCategory('\n'), (int)RoboTextCategoryControl);
    RoboCheckEqInt((int)RoboTextUnicodeCategory(0x00ADu), (int)RoboTextCategoryFormat);
    RoboCheckEqInt((int)RoboTextUnicodeCategory(0x0301u), (int)RoboTextCategoryMark);
    RoboCheckEqInt((int)RoboTextUnicodeCategory(0xD800u), (int)RoboTextCategoryOther);

    /* Groups are a vocabulary with names, and every defined one has a distinct name. */
    RoboCheckEqInt((int)RoboTextCategoryCount, 9);
    for (index = 0; index < (int)RoboTextCategoryCount; index++) {
        RoboCheck(RoboTextCategoryIsKnown((RoboTextCategory)index));
        RoboCheck(RoboTextCategoryName((RoboTextCategory)index) != NULL);
    }
    RoboCheck(!RoboTextCategoryIsKnown((RoboTextCategory)9));
    RoboCheckStrEq(RoboTextCategoryName(RoboTextCategoryMark), "mark");
    RoboCheckStrEq(RoboTextCategoryName((RoboTextCategory)99), "other");

    /* Combining marks: the classification the fallback pass keys on. */
    RoboCheck(RoboTextUnicodeIsCombiningMark(0x0301u));
    RoboCheck(RoboTextUnicodeIsCombiningMark(0x20D0u));
    RoboCheck(RoboTextUnicodeIsCombiningMark(0xE0100u));
    RoboCheck(!RoboTextUnicodeIsCombiningMark('A'));
    RoboCheck(!RoboTextUnicodeIsCombiningMark(' '));
    RoboCheck(!RoboTextUnicodeIsCombiningMark(0xFFFDu));

    /* Whitespace, control, format and assignment, each through RoboUnicode. */
    RoboCheck(RoboTextUnicodeIsWhitespace(' '));
    RoboCheck(RoboTextUnicodeIsWhitespace('\t'));
    RoboCheck(RoboTextUnicodeIsWhitespace('\n'));
    RoboCheck(!RoboTextUnicodeIsWhitespace('A'));
    RoboCheck(RoboTextUnicodeIsControl('\n'));
    RoboCheck(!RoboTextUnicodeIsControl(' '));
    RoboCheck(RoboTextUnicodeIsFormat(0x00ADu));
    RoboCheck(!RoboTextUnicodeIsFormat('A'));
    RoboCheck(RoboTextUnicodeIsAssigned('A'));
    RoboCheck(!RoboTextUnicodeIsAssigned(0xFDD0u)); /* A noncharacter is never assigned. */

    /* A non-scalar is not a character: every property door answers without reading a table. */
    RoboCheck(!RoboTextUnicodeIsCombiningMark(0xD800u));
    RoboCheck(!RoboTextUnicodeIsWhitespace(0xD800u));
    RoboCheckEqInt((int)RoboTextUnicodeCategory(0xD800u), (int)RoboTextCategoryOther);
    RoboCheckEqInt((int)RoboTextUnicodeScript(0xD800u), (int)RoboUScriptUnknown);
    RoboCheckEqInt((int)RoboTextUnicodeCombiningClass(0x0301u), 230);
    RoboCheckEqInt((int)RoboTextUnicodeCombiningClass('A'), 0);
}

static void TestDataVersion(void)
{
    const char *version = RoboTextUnicodeDataVersion();

    RoboCheck(version != NULL);
    RoboCheck(version[0] != '\0');
    /* The module's version *is* RoboUnicode's: one pin, reported twice rather than two that can drift. */
    RoboCheckStrEq(version, UnicodeUcdVersionString());
    RoboCheckStrEq(version, RoboUnicodeUcdVersionString);
}

static void TestDecoder(void)
{
    RoboTextUnicodeDecoder decoder;
    RoboTextByteRange range;
    RoboTextView view = ViewOf("A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x99\x82"); /* A é 中 🙂 */
    RoboTextCodePoint expected[4];
    size_t expectedBytes[5];

    expected[0] = 0x0041u;
    expected[1] = 0x00E9u;
    expected[2] = 0x4E2Du;
    expected[3] = 0x1F642u;
    expectedBytes[0] = 0u;
    expectedBytes[1] = 1u;
    expectedBytes[2] = 3u;
    expectedBytes[3] = 6u;
    expectedBytes[4] = 10u;

    RoboCheckEqInt((int)RoboTextUnicodeDecoderInit(&decoder, view, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextUnicodeDecoderIndex(&decoder), 0);
    RoboCheck(RoboTextUnicodeDecoderHasNext(&decoder));

    for (int index = 0; index < 4; index++) {
        RoboCheckEqInt((int)RoboTextUnicodeDecoderIndex(&decoder), index);
        /* Peek does not consume: twice in a row and then Next must agree. */
        RoboCheckEqInt((int)RoboTextUnicodeDecoderPeek(&decoder), (int)expected[index]);
        RoboCheckEqInt((int)RoboTextUnicodeDecoderPeek(&decoder), (int)expected[index]);
        RoboCheckEqInt((int)RoboTextUnicodeDecoderNext(&decoder), (int)expected[index]);
        RoboCheckEqInt((int)RoboTextUnicodeDecoderIndex(&decoder), index + 1);
        RoboCheck(!RoboTextUnicodeDecoderLastWasReplaced(&decoder));
        RoboCheckEqInt((int)RoboTextUnicodeDecoderLastRange(&decoder, &range, NULL), (int)RoboOk);
        RoboCheckEqInt((int)range.start, (int)expectedBytes[index]);
        RoboCheckEqInt((int)range.end, (int)expectedBytes[index + 1]);
    }
    RoboCheck(!RoboTextUnicodeDecoderHasNext(&decoder));
    /* Exhaustion is not a character: the door answers, does not advance and does not claim a replacement. */
    RoboCheckEqInt((int)RoboTextUnicodeDecoderIndex(&decoder), 4);
    RoboCheckEqInt((int)RoboTextUnicodeDecoderNext(&decoder), (int)RoboTextCodePointReplacement);
    RoboCheckEqInt((int)RoboTextUnicodeDecoderIndex(&decoder), 4);
    RoboCheck(!RoboTextUnicodeDecoderLastWasReplaced(&decoder));
    /* Safe on NULL: every door answers rather than crashing, because a caller that lost its decoder is a
     * bug to report and not a crash to have. */
    RoboCheck(!RoboTextUnicodeDecoderHasNext(NULL));
    RoboCheckEqInt((int)RoboTextUnicodeDecoderPeek(NULL), (int)RoboTextCodePointReplacement);
    RoboCheckEqInt((int)RoboTextUnicodeDecoderNext(NULL), (int)RoboTextCodePointReplacement);
    RoboCheckEqInt((int)RoboTextUnicodeDecoderIndex(NULL), 0);
    RoboCheckEqInt((int)RoboTextUnicodeDecoderLastRange(NULL, &range, NULL), (int)RoboInvalidArgument);

    /* An unsound view is refused at the door, and an empty one is a legal (empty) text. */
    RoboCheckEqInt((int)RoboTextUnicodeDecoderInit(&decoder, RoboTextViewMake(NULL, 4u), NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextUnicodeDecoderInit(NULL, view, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextUnicodeDecoderInit(&decoder, RoboTextViewMake(NULL, 0u), NULL),
                   (int)RoboOk);
    RoboCheck(!RoboTextUnicodeDecoderHasNext(&decoder));
}

static void TestDecoderInvalidUtf8(void)
{
    RoboTextUnicodeDecoder decoder;
    RoboTextByteRange range;
    /* A stray continuation byte, a bad lead byte, then a valid letter: three scalars, five bytes. */
    static const char *bytes = "\x80\xFF" "A";
    /* An unterminated three-byte lead at the end of a buffer: one replacement per ill-formed byte. */
    static const char *truncated = "A\xE4\xB8";

    RoboCheckEqInt((int)RoboTextUnicodeDecoderInit(&decoder, ViewOf(bytes), NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextUnicodeDecoderNext(&decoder), (int)RoboTextCodePointReplacement);
    RoboCheck(RoboTextUnicodeDecoderLastWasReplaced(&decoder));
    RoboCheckEqInt((int)RoboTextUnicodeDecoderLastRange(&decoder, &range, NULL), (int)RoboOk);
    RoboCheckEqInt((int)range.start, 0);
    RoboCheckEqInt((int)range.end, 1);
    RoboCheckEqInt((int)RoboTextUnicodeDecoderNext(&decoder), (int)RoboTextCodePointReplacement);
    RoboCheck(RoboTextUnicodeDecoderLastWasReplaced(&decoder));
    RoboCheckEqInt((int)RoboTextUnicodeDecoderNext(&decoder), 'A');
    RoboCheck(!RoboTextUnicodeDecoderLastWasReplaced(&decoder));
    RoboCheckEqInt((int)RoboTextUnicodeDecoderIndex(&decoder), 3);
    RoboCheck(!RoboTextUnicodeDecoderHasNext(&decoder));

    /* A genuine U+FFFD is *not* reported as a replacement, which is the distinction the flag exists for. */
    RoboCheckEqInt((int)RoboTextUnicodeDecoderInit(&decoder, ViewOf("\xEF\xBF\xBD"), NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextUnicodeDecoderNext(&decoder), (int)RoboTextCodePointReplacement);
    RoboCheck(!RoboTextUnicodeDecoderLastWasReplaced(&decoder));

    RoboCheckEqInt((int)RoboTextUnicodeDecoderInit(&decoder, ViewOf(truncated), NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextUnicodeDecoderNext(&decoder), 'A');
    RoboCheckEqInt((int)RoboTextUnicodeDecoderNext(&decoder), (int)RoboTextCodePointReplacement);
    RoboCheck(RoboTextUnicodeDecoderLastWasReplaced(&decoder));
    RoboCheckEqInt((int)RoboTextUnicodeDecoderNext(&decoder), (int)RoboTextCodePointReplacement);
    RoboCheck(RoboTextUnicodeDecoderLastWasReplaced(&decoder));
    RoboCheck(!RoboTextUnicodeDecoderHasNext(&decoder));
    RoboCheckEqInt((int)RoboTextUnicodeDecoderIndex(&decoder), 3);
}

static void TestDecodeAll(void)
{
    Uchar32 *scalars = NULL;
    RoboTextScalarIndex count = 0u;

    RoboCheckEqInt((int)RoboTextUnicodeDecodeAll(ViewOf(SampleHan SampleLatin), &scalars, &count, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 2);
    RoboCheckEqInt((int)scalars[0], 0x4E2Du);
    RoboCheckEqInt((int)scalars[1], 0x41u);
    RoboTextUnicodeScalarsFree(scalars);

    /* The empty view allocates nothing and answers zero, and every door is safe on NULL. */
    scalars = (Uchar32 *)0x1;
    count = 9u;
    RoboCheckEqInt((int)RoboTextUnicodeDecodeAll(RoboTextViewMake(NULL, 0u), &scalars, &count, NULL),
                   (int)RoboOk);
    RoboCheck(scalars == NULL);
    RoboCheckEqInt((int)count, 0);
    RoboCheckEqInt((int)RoboTextUnicodeDecodeAll(ViewOf("A"), NULL, &count, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextUnicodeDecodeAll(ViewOf("A"), &scalars, NULL, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextUnicodeDecodeAll(RoboTextViewMake(NULL, 2u), &scalars, &count, NULL),
                   (int)RoboInvalidArgument);
    RoboTextUnicodeScalarsFree(NULL);
}

static void TestScripts(void)
{
    RoboCheckEqInt((int)RoboTextUnicodeScript('A'), (int)RoboUScriptLatin);
    RoboCheckEqInt((int)RoboTextUnicodeScript('7'), (int)RoboUScriptCommon);
    RoboCheckEqInt((int)RoboTextUnicodeScript(' '), (int)RoboUScriptCommon);
    RoboCheckEqInt((int)RoboTextUnicodeScript(0x041Fu), (int)RoboUScriptCyrillic);
    RoboCheckEqInt((int)RoboTextUnicodeScript(0x03A9u), (int)RoboUScriptGreek);
    RoboCheckEqInt((int)RoboTextUnicodeScript(0x0645u), (int)RoboUScriptArabic);
    RoboCheckEqInt((int)RoboTextUnicodeScript(0x05E9u), (int)RoboUScriptHebrew);
    RoboCheckEqInt((int)RoboTextUnicodeScript(0x0939u), (int)RoboUScriptDevanagari);
    RoboCheckEqInt((int)RoboTextUnicodeScript(0x4E2Du), (int)RoboUScriptHan);
    RoboCheckEqInt((int)RoboTextUnicodeScript(0x3042u), (int)RoboUScriptHiragana);
    RoboCheckEqInt((int)RoboTextUnicodeScript(0x30A2u), (int)RoboUScriptKatakana);
    RoboCheckEqInt((int)RoboTextUnicodeScript(0xD55Cu), (int)RoboUScriptHangul);
    /* A combining acute is Inherited: it takes its base's script rather than declaring one. */
    RoboCheckEqInt((int)RoboTextUnicodeScript(0x0301u), (int)RoboUScriptInherited);
}

static void TestScriptRuns(void)
{
    RoboTextScriptRun runs[16];
    size_t runCount = 0u;
    RoboTextScript scripts[16];

    /* One script, with the digits and the space folded in: the neutrality rule, stated as a count. */
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ViewOf("abc 123 def"), RoboTextScriptCommon, runs, 8u,
                                                      &runCount, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)runCount, 1);
    RoboCheckEqInt((int)runs[0].range.start, 0);
    RoboCheckEqInt((int)runs[0].range.end, 11);
    RoboCheckEqInt((int)runs[0].script, (int)RoboUScriptLatin);

    /* A leading neutral takes the script that *follows* it, which is the same rule read forwards. */
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ViewOf("  123 abc"), RoboTextScriptCommon, runs, 8u,
                                                      &runCount, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)runCount, 1);
    RoboCheckEqInt((int)runs[0].range.start, 0);
    RoboCheckEqInt((int)runs[0].script, (int)RoboUScriptLatin);

    /* Nothing script-bearing at all is Common, not a guess. */
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ViewOf("123 456"), RoboTextScriptCommon, runs, 8u,
                                                      &runCount, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)runCount, 1);
    RoboCheckEqInt((int)runs[0].script, (int)RoboTextScriptCommon);

    /* Latin then Han: two runs, and the boundary is the first Han scalar. */
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ViewOf("Hi " SampleHan SampleHan), RoboTextScriptCommon,
                                                      runs, 8u, &runCount, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)runCount, 2);
    RoboCheckEqInt((int)runs[0].script, (int)RoboUScriptLatin);
    RoboCheckEqInt((int)runs[0].range.end, 3);
    RoboCheckEqInt((int)runs[1].script, (int)RoboUScriptHan);
    RoboCheckEqInt((int)runs[1].range.start, 3);
    RoboCheckEqInt((int)runs[1].range.end, 5);

    /* Every script the phase names classifies to its own run, one after another. */
    {
        static const char *mixed = SampleLatin SampleCyrillic SampleGreek SampleArabic SampleHebrew
            SampleDevanagari SampleHan SampleHiragana SampleKatakana SampleHangul;
        RoboTextScript expected[10];

        expected[0] = RoboUScriptLatin;
        expected[1] = RoboUScriptCyrillic;
        expected[2] = RoboUScriptGreek;
        expected[3] = RoboUScriptArabic;
        expected[4] = RoboUScriptHebrew;
        expected[5] = RoboUScriptDevanagari;
        expected[6] = RoboUScriptHan;
        expected[7] = RoboUScriptHiragana;
        expected[8] = RoboUScriptKatakana;
        expected[9] = RoboUScriptHangul;
        RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ViewOf(mixed), RoboTextScriptCommon, runs, 16u,
                                                          &runCount, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)runCount, 10);
        for (size_t index = 0u; index < runCount; index++) {
            RoboCheckEqInt((int)runs[index].script, (int)expected[index]);
            RoboCheckEqInt((int)runs[index].range.start, (int)index);
            RoboCheckEqInt((int)runs[index].range.end, (int)index + 1);
        }
    }

    /* The array door also fills the per-scalar array, and the two views of one answer agree. */
    {
        Uchar32 *scalars = NULL;
        RoboTextScalarIndex count = 0u;

        RoboCheckEqInt((int)RoboTextUnicodeDecodeAll(ViewOf("a" SampleHan), &scalars, &count, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextScriptRunsResolve(scalars, (size_t)count, RoboTextScriptCommon, scripts,
                                                      runs, 8u, &runCount, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)runCount, 2);
        RoboCheckEqInt((int)scripts[0], (int)RoboUScriptLatin);
        RoboCheckEqInt((int)scripts[1], (int)RoboUScriptHan);
        RoboTextUnicodeScalarsFree(scalars);
    }

    /* A script hint classifies every scalar as the hinted script, digits and spaces included. */
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ViewOf("abc " SampleHebrew), RoboUScriptGreek, runs, 8u,
                                                      &runCount, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)runCount, 1);
    RoboCheckEqInt((int)runs[0].script, (int)RoboUScriptGreek);

    /* A small array is reported rather than overrun. */
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ViewOf(SampleLatin SampleHan), RoboTextScriptCommon,
                                                      runs, 1u, &runCount, NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)runCount, 2);
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ViewOf("abc"), RoboTextScriptCommon, NULL, 0u, NULL,
                                                      NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextScriptRunsResolve(NULL, 4u, RoboTextScriptCommon, NULL, runs, 8u, &runCount,
                                                  NULL),
                   (int)RoboInvalidArgument);
    /* Empty text: no runs, and the door still answers. */
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(RoboTextViewMake(NULL, 0u), RoboTextScriptCommon, runs,
                                                      8u, &runCount, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)runCount, 0);
}

static void TestDirection(void)
{
    Uchar32 scalars[4];

    RoboCheckEqInt((int)RoboTextUnicodeStrongDirection('A'), (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextUnicodeStrongDirection(0x05E9u), (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextUnicodeStrongDirection(0x0645u), (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextUnicodeStrongDirection('7'), (int)RoboTextDirectionUnknown);
    RoboCheckEqInt((int)RoboTextUnicodeStrongDirection(' '), (int)RoboTextDirectionUnknown);

    /* No strong scalar and nothing declared is the one real Unknown. */
    scalars[0] = '1';
    scalars[1] = '2';
    scalars[2] = ' ';
    scalars[3] = '3';
    RoboCheckEqInt((int)RoboTextUnicodeBaseDirection(scalars, 4u, RoboTextDirectionUnknown),
                   (int)RoboTextDirectionUnknown);
    /* The same text with a declared base takes the declaration. */
    RoboCheckEqInt((int)RoboTextUnicodeBaseDirection(scalars, 4u, RoboTextDirectionRtl),
                   (int)RoboTextDirectionRtl);

    scalars[0] = 0x05E9u;
    scalars[1] = 'A';
    RoboCheckEqInt((int)RoboTextUnicodeBaseDirection(scalars, 2u, RoboTextDirectionUnknown),
                   (int)RoboTextDirectionRtl);
    scalars[0] = 'A';
    RoboCheckEqInt((int)RoboTextUnicodeBaseDirection(scalars, 2u, RoboTextDirectionUnknown),
                   (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextUnicodeBaseDirection(NULL, 0u, RoboTextDirectionUnknown),
                   (int)RoboTextDirectionUnknown);

    /* Classification: strong scalars keep their own direction, everything else takes the base. */
    RoboCheckEqInt((int)RoboTextUnicodeClassifyDirection('A', RoboTextDirectionRtl),
                   (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextUnicodeClassifyDirection(0x05E9u, RoboTextDirectionLtr),
                   (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextUnicodeClassifyDirection('7', RoboTextDirectionRtl),
                   (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextUnicodeClassifyDirection(' ', RoboTextDirectionUnknown),
                   (int)RoboTextDirectionUnknown);
}

static void TestFuzz(void)
{
    uint8_t junk[128];

    for (size_t index = 0u; index < sizeof(junk); index++) {
        junk[index] = (uint8_t)(index * 53u + 7u);
    }
    /* The decoder is exercised over arbitrary bytes through the same doors a caller uses. */
    RoboTextUnicodeDecoder decoder;

    RoboCheckEqInt((int)RoboTextUnicodeDecoderInit(&decoder, RoboTextViewMake((const char *)junk,
                                                                             sizeof(junk)), NULL),
                   (int)RoboOk);
    while (RoboTextUnicodeDecoderHasNext(&decoder)) {
        (void)RoboTextUnicodeDecoderNext(&decoder);
    }
    RoboCheckEqInt((int)RoboTextUnicodeDecoderIndex(&decoder), (int)sizeof(junk));
}

void TestRegisterTextUnicode(void)
{
    TestRegister("text.unicode.code_points", TestCodePoints);
    TestRegister("text.unicode.properties", TestProperties);
    TestRegister("text.unicode.data_version", TestDataVersion);
    TestRegister("text.unicode.decoder", TestDecoder);
    TestRegister("text.unicode.decoder_invalid", TestDecoderInvalidUtf8);
    TestRegister("text.unicode.decode_all", TestDecodeAll);
    TestRegister("text.unicode.scripts", TestScripts);
    TestRegister("text.unicode.script_runs", TestScriptRuns);
    TestRegister("text.unicode.direction", TestDirection);
    TestRegister("text.unicode.fuzz", TestFuzz);
}
