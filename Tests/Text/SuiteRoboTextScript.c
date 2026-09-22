/*
 * RoboText's script segmentation (Phase 12.6) — `text.script.*`.
 *
 * The phase's subject is the *boundary between scripts*, so the suite is written as tables of exact run
 * boundaries rather than as assertions that a script "looks right". Each table row names the text, the
 * scalars it holds and every run expected, and the helper checks the whole answer: the count, each range,
 * each script, and the invariants the phase requires of any run list (contiguous, half-open, non-empty, no
 * two adjacent runs carrying the same script — coalescing is a property of the output, not of a fixup pass).
 *
 * The rows are grouped by what they are a test *of*, because the rules interact and a single big table
 * would hide which one answered:
 *
 *   `pure_scripts`    one anchor script, and the short name RoboUnicode gives it.
 *   `mixed_scripts`   where the boundaries fall, including that a space takes the carry from its left.
 *   `common`          punctuation, digits and symbols between one script: **one** run, not three.
 *   `numbers`         Unicode metadata, not visual appearance — `123` is `Zyyy`, Devanagari's digits are
 *                     `Deva`, Myanmar's are `Mymr`.
 *   `combining_marks` a mark belongs to its base, left first and right when there is no left.
 *   `extensions`      `Script_Extensions` decides a neutral the two anchors disagree about, and a scalar
 *                     listed by **both** keeps its neutrality instead of being claimed by either.
 *   `symbols_emoji`   symbols and emoji are `Zyyy`/`Zinh`; no invented `Emoji` script, no invented run.
 *   `unknown`         an unassigned scalar is `Unknown` and stays its own run rather than becoming Latin.
 *   `hint`            a caller's script hint classifies everything and is nothing but a hint.
 *   `diagnostics`     the dump, exactly, in both index spaces.
 *   `performance`     one pass, one allocation, and the ASCII path that makes that true.
 *
 * The samples are the phase's own list, spelled as UTF-8 bytes rather than as wide literals so that the
 * *encoding* is tested beside the classification — the byte-range column of the table dump is the receipt.
 */

#include "RoboTextTestSupport.h"

#include "RoboMem/RoboMemDebug.h"

#include <time.h>

/* ------------------------------------------------------------------ */
/* The samples, as UTF-8                                                */
/* ------------------------------------------------------------------ */

#define ScriptLatin "Hello"                                     /* 5 */
#define ScriptCyrillic "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"     /* Привет, 6 */
#define ScriptGreek "\xCE\xB3\xCE\xB5\xCE\xB9\xCE\xAC"                         /* γειά, 4 */
#define ScriptArabic "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7"                /* مرحبا, 5 */
#define ScriptHebrew "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"                        /* שלום, 4 */
#define ScriptDevanagari "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87" /* नमस्ते, 6 */
#define ScriptBengali "\xE0\xA6\xAC\xE0\xA6\xBE\xE0\xA6\x82\xE0\xA6\xB2\xE0\xA6\xBE"                /* বাংলা, 5 */
#define ScriptThai "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2"                      /* ไทย, 3 */
#define ScriptHan "\xE4\xB8\xAD\xE6\x96\x87"                                   /* 中文, 2 */
#define ScriptJapanese "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"                  /* 日本語, 3 */
#define ScriptHangul "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4"                    /* 한국어, 3 */
#define ScriptHindi "\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D\xE0\xA4\xA6\xE0\xA5\x80"      /* हिन्दी, 6 */
#define ScriptTamil "\xE0\xAE\x85"                                             /* அ, 1 */

/** Single scalars the rules turn on, as UTF-8. */
#define ScalarAcute "\xCC\x81"            /* U+0301, Zinh, scx ⊇ {Latn Grek Cyrl} */
#define ScalarRing "\xCC\x8A"             /* U+030A, Zinh, scx {Dupl Latn Syrc} */
#define ScalarYpogegrammeni "\xCD\x85"    /* U+0345, Zinh, scx {Grek} only */
#define ScalarModifierApostrophe "\xCA\xBC" /* U+02BC, Zyyy, scx {Beng Cyrl Deva Latn Lisu Thai Toto} */
#define ScalarMiddleDot "\xC2\xB7"        /* U+00B7, Zyyy, scx 16 scripts, Latn among them */
#define ScalarTatweel "\xD9\x80"          /* U+0640, Zyyy, scx ⊇ {Arab Syrc} */
#define ScalarSyriacAlaph "\xDC\x90"      /* U+0710, Syrc */
#define ScalarHebrewAccent "\xD6\x91"     /* U+0591, Hebr — a mark that is *not* Inherited */
#define ScalarHebrewAlef "\xD7\x90"       /* U+05D0, Hebr */
#define ScalarHebrewBet "\xD7\x91"        /* U+05D1, Hebr */
#define ScalarArabicAlef "\xD8\xA7"       /* U+0627, Arab */
#define ScalarArabicBeh "\xD8\xA8"        /* U+0628, Arab */
#define ScalarArabicIndicDigits "\xD9\xA1\xD9\xA2\xD9\xA3"                    /* ١٢٣ */
#define ScalarThaiDigits "\xE0\xB9\x90\xE0\xB9\x91\xE0\xB9\x92"               /* ๐๑๒ */
#define ScalarMyanmarDigits "\xE1\x81\x80\xE1\x81\x81\xE1\x81\x82"            /* ၀၁၂ */
#define ScalarDanda "\xE0\xA5\xA4"        /* U+0964, Zyyy, scx 21 scripts, Deva and Taml among them */
#define ScalarIdeographicComma "\xE3\x80\x81" /* U+3001, Zyyy, scx ⊇ {Hani Hang Hira Kana} */
#define ScalarUnassigned "\xCD\xB8"       /* U+0378, Zzzz */
#define ScalarCopyright "\xC2\xA9"        /* U+00A9, Zyyy */
#define ScalarRegistered "\xC2\xAE"       /* U+00AE, Zyyy */
#define ScalarTradeMark "\xE2\x84\xA2"    /* U+2122, Zyyy */
#define ScalarArrowRight "\xE2\x86\x92"   /* U+2192, Zyyy */
#define ScalarArrowLeft "\xE2\x86\x90"    /* U+2190, Zyyy */
#define ScalarArrowUp "\xE2\x86\x91"      /* U+2191, Zyyy */
#define ScalarArrowDown "\xE2\x86\x93"    /* U+2193, Zyyy */
#define ScalarGrinning "\xF0\x9F\x98\x80" /* U+1F600, Zyyy */
#define ScalarRocket "\xF0\x9F\x9A\x80"   /* U+1F680, Zyyy */
#define ScalarHeart "\xE2\x9D\xA4\xEF\xB8\x8F" /* U+2764 U+FE0F */
#define ScalarZwjFamily "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"
#define ScalarMarkNoFace "\xF3\xA0\x84\x80" /* U+E0100, Zinh with no extension entry */

/* ------------------------------------------------------------------ */
/* The harness                                                         */
/* ------------------------------------------------------------------ */

/** One expected run: a half-open scalar range and the script it must carry. */
typedef struct ScriptRunExpectation {
    RoboTextScalarIndex start;
    RoboTextScalarIndex end;
    RoboTextScript script;
} ScriptRunExpectation;

#define ScriptRunMax 24u

static RoboTextView ScriptView(const char *text)
{
    return RoboTextViewMake(text, strlen(text));
}

/**
 * Resolve @p text through the **view** door and check every run against @p expected.
 *
 * Also checks the two derived properties the phase requires of any answer: the runs tile the text exactly
 * (first starts at 0, last ends at the scalar count, each run's end is the next run's start, none is
 * empty) and no two adjacent runs carry the same script, which is what makes "coalesced" a property of
 * the output rather than of a later pass.
 */
static size_t ExpectScriptRuns(const char *text,
                               const ScriptRunExpectation *expected,
                               size_t expectedCount)
{
    RoboTextScriptRun runs[ScriptRunMax];
    size_t count = 0u;
    RoboTextView view = ScriptView(text);
    RoboTextScalarIndex scalars = 0u;

    memset(runs, 0, sizeof(runs));
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(view, RoboTextScriptCommon, runs, ScriptRunMax,
                                                      &count, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, (int)expectedCount);
    RoboCheckEqInt((int)RoboTextViewScalarCount(view, &scalars, NULL), (int)RoboOk);

    for (size_t index = 0u; index < expectedCount && index < count; index++) {
        RoboCheckEqInt((int)runs[index].range.start, (int)expected[index].start);
        RoboCheckEqInt((int)runs[index].range.end, (int)expected[index].end);
        RoboCheckEqInt((int)runs[index].script, (int)expected[index].script);
        RoboCheckStrEq(RoboTextScriptName(runs[index].script),
                       RoboTextScriptName(expected[index].script));
    }
    if (count != 0u) {
        RoboCheckEqInt((int)runs[0].range.start, 0);
        RoboCheckEqInt((int)runs[count - 1u].range.end, (int)scalars);
        for (size_t index = 0u; index + 1u < count; index++) {
            RoboCheckEqInt((int)runs[index].range.end, (int)runs[index + 1u].range.start);
            RoboCheck(runs[index].script != runs[index + 1u].script);
        }
        for (size_t index = 0u; index < count; index++) {
            /* Every script is either an anchor or a neutral; there is no third kind, which is what lets the
             * resolver say "this run waits for an anchor". */
            RoboCheck(RoboTextScriptIsAnchor(runs[index].script) ||
                      RoboTextScriptIsNeutral(runs[index].script));
            RoboCheck(runs[index].range.start < runs[index].range.end);
        }
    } else {
        RoboCheckEqInt((int)scalars, 0);
    }

    /* The count-only door agrees with the filled one, and so does the array door over the same scalars. */
    {
        size_t counted = 0u;
        Uchar32 *codePoints = NULL;
        RoboTextScalarIndex decoded = 0u;
        RoboTextScriptRun again[ScriptRunMax];

        memset(again, 0, sizeof(again));
        RoboCheckEqInt((int)RoboTextScriptRunsResolveView(view, RoboTextScriptCommon, NULL, 0u, &counted,
                                                         NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)counted, (int)count);

        RoboCheckEqInt((int)RoboTextUnicodeDecodeAll(view, &codePoints, &decoded, NULL), (int)RoboOk);
        RoboCheckEqInt((int)decoded, (int)scalars);
        {
            RoboTextScript scripts[ScriptRunMax];
            size_t fromArray = 0u;

            RoboCheckEqInt((int)RoboTextScriptRunsResolve(codePoints, (size_t)decoded,
                                                          RoboTextScriptCommon, scripts, again,
                                                          ScriptRunMax, &fromArray, NULL),
                           (int)RoboOk);
            RoboCheckEqInt((int)fromArray, (int)count);
            for (size_t index = 0u; index < count; index++) {
                RoboCheck(RoboTextRangeEqual(&again[index].range, &runs[index].range));
                RoboCheckEqInt((int)again[index].script, (int)runs[index].script);
            }
            /* The per-scalar resolutions agree with the runs they were cut from: an index inside run `i`
             * resolves to run `i`'s script, for every scalar. */
            for (size_t index = 0u; index < (size_t)decoded; index++) {
                size_t run = 0u;

                while (run + 1u < count && index >= (size_t)runs[run].range.end) {
                    run++;
                }
                if (count != 0u) {
                    RoboCheckEqInt((int)scripts[index], (int)runs[run].script);
                }
            }
        }
        RoboTextUnicodeScalarsFree(codePoints);
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Pure scripts                                                        */
/* ------------------------------------------------------------------ */

static void TestScriptPureScripts(void)
{
    static const struct PureCase {
        const char *text;
        RoboTextScript script;
        const char *shortName;
        RoboTextScalarIndex scalars;
    } cases[] = {
        { ScriptLatin, RoboUScriptLatin, "Latn", 5 },
        { ScriptCyrillic, RoboUScriptCyrillic, "Cyrl", 6 },
        { ScriptGreek, RoboUScriptGreek, "Grek", 4 },
        { ScriptArabic, RoboUScriptArabic, "Arab", 5 },
        { ScriptHebrew, RoboUScriptHebrew, "Hebr", 4 },
        { ScriptDevanagari, RoboUScriptDevanagari, "Deva", 6 },
        { ScriptBengali, RoboUScriptBengali, "Beng", 5 },
        { ScriptThai, RoboUScriptThai, "Thai", 3 },
        { ScriptHan, RoboUScriptHan, "Hani", 2 },
        { ScriptJapanese, RoboUScriptHan, "Hani", 3 },
        { ScriptHangul, RoboUScriptHangul, "Hang", 3 },
    };
    size_t caseIndex;

    for (caseIndex = 0u; caseIndex < sizeof(cases) / sizeof(cases[0]); caseIndex++) {
        ScriptRunExpectation expected;
        RoboTextScriptRun runs[4];
        size_t count = 0u;
        RoboTextView view = ScriptView(cases[caseIndex].text);
        RoboTextScalarIndex scalars = 0u;

        memset(runs, 0, sizeof(runs));
        RoboCheckEqInt((int)RoboTextViewScalarCount(view, &scalars, NULL), (int)RoboOk);
        RoboCheckEqInt((int)scalars, (int)cases[caseIndex].scalars);

        expected.start = 0u;
        expected.end = cases[caseIndex].scalars;
        expected.script = cases[caseIndex].script;
        RoboCheckEqInt((int)ExpectScriptRuns(cases[caseIndex].text, &expected, 1u), 1);

        RoboCheckEqInt((int)RoboTextScriptRunsResolveView(view, RoboTextScriptCommon, runs, 4u, &count,
                                                          NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)count, 1);
        /* The short name pins the vocabulary to RoboUnicode's: a run that claimed the right enum by
         * accident would be caught here, and so would a second script table. */
        RoboCheckStrEq(RoboTextScriptName(runs[0].script), cases[caseIndex].shortName);
        RoboCheck(!RoboTextScriptIsNeutral(runs[0].script));
        RoboCheck(RoboTextScriptIsAnchor(runs[0].script));
    }
}

/* ------------------------------------------------------------------ */
/* Mixed scripts                                                       */
/* ------------------------------------------------------------------ */

static void TestScriptMixedScripts(void)
{
    /* `"Hello 世界"`: the space carries the Latin script left, so the boundary is 6, not 5. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 6, RoboUScriptLatin },
                                                         { 6, 8, RoboUScriptHan } };

        ExpectScriptRuns("Hello " ScriptHan, expected, 2u);
    }
    /* `"Hello مرحبا"`: the same carry, then five Arabic scalars. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 6, RoboUScriptLatin },
                                                         { 6, 11, RoboUScriptArabic } };

        ExpectScriptRuns("Hello " ScriptArabic, expected, 2u);
    }
    /* `"English مرحبا हिन्दी"`: three anchors, two carries — 8 + 5 + 1 + 6 scalars. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 8, RoboUScriptLatin },
                                                         { 8, 14, RoboUScriptArabic },
                                                         { 14, 20, RoboUScriptDevanagari } };

        ExpectScriptRuns("English " ScriptArabic " " ScriptHindi, expected, 3u);
    }
    /* `"Latin Привет γειά"`, the same shape with two more scripts — 6 + 6 + 1 + 4 scalars. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 6, RoboUScriptLatin },
                                                         { 6, 13, RoboUScriptCyrillic },
                                                         { 13, 17, RoboUScriptGreek } };

        ExpectScriptRuns("Latin " ScriptCyrillic " " ScriptGreek, expected, 3u);
    }
    /* A Greek word inside a Latin sentence is three runs — one boundary on each side — not one run per
     * word, and the spaces beside it belong to the scripts they sit between. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 4, RoboUScriptLatin },
                                                         { 4, 9, RoboUScriptGreek },
                                                         { 9, 16, RoboUScriptLatin } };

        ExpectScriptRuns("The " ScriptGreek " is here", expected, 3u);
    }
    /* Segments of one script separated by punctuation are one run, however many there are: the punctuation
     * and the space carry the script left, which is the whole point of the `prev == next` case. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 15, RoboUScriptCyrillic } };

        ExpectScriptRuns(ScriptCyrillic ", " ScriptCyrillic "!", expected, 1u);
    }
}

/* ------------------------------------------------------------------ */
/* Common characters                                                   */
/* ------------------------------------------------------------------ */

static void TestScriptCommon(void)
{
    static const struct CommonCase {
        const char *text;
        RoboTextScalarIndex scalars;
        RoboTextScript script;
    } cases[] = {
        { "Hello, world!", 13, RoboUScriptLatin },
        { "Version 2.0", 11, RoboUScriptLatin },
        { "e.g. a.b", 8, RoboUScriptLatin },
        { "2026/09/22", 10, RoboUScriptCommon },
        { "   ", 3, RoboUScriptCommon },
        { "123 456", 7, RoboUScriptCommon },
        { ScalarCopyright ScalarRegistered ScalarTradeMark, 3, RoboUScriptCommon },
        { " \t\n ", 4, RoboUScriptCommon },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        ScriptRunExpectation expected;

        expected.start = 0u;
        expected.end = cases[index].scalars;
        expected.script = cases[index].script;
        ExpectScriptRuns(cases[index].text, &expected, 1u);
    }

    /* A leading neutral resolves *right* — `" 123 abc"` is one Latin run, and the digits are not stranded
     * in front of it. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 8, RoboUScriptLatin } };

        ExpectScriptRuns(" 123 abc", expected, 1u);
    }
    /* And where the two anchors differ, a neutral with **no listed set** takes the left carry: an ASCII
     * space and an ASCII comma are `Zyyy` with nothing listed, so both stay with the script they follow.
     * (`U+00B7` is *not* that case — it lists Greek and Latin both, which the extensions case pins.) */
    {
        static const ScriptRunExpectation expected[] = { { 0, 5, RoboUScriptGreek },
                                                         { 5, 7, RoboUScriptLatin } };
        static const ScriptRunExpectation commaArabic[] = { { 0, 7, RoboUScriptArabic },
                                                            { 7, 9, RoboUScriptLatin } };

        RoboCheck(!RoboTextUnicodeScriptExtensionsInclude(0x0020u, RoboUScriptGreek));
        RoboCheck(!RoboTextUnicodeScriptExtensionsInclude(0x002Cu, RoboUScriptArabic));
        ExpectScriptRuns(ScriptGreek " ab", expected, 2u);
        ExpectScriptRuns(ScriptArabic ", ab", commaArabic, 2u);
    }
}

/* ------------------------------------------------------------------ */
/* Numbers carry Unicode metadata, not an assumption                   */
/* ------------------------------------------------------------------ */

static void TestScriptNumbers(void)
{
    /* ASCII digits are `Zyyy`, and a text of nothing else has nothing to be carried by. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 3, RoboUScriptCommon } };

        ExpectScriptRuns("123", expected, 1u);
        RoboCheckEqInt((int)RoboTextUnicodeScript('1'), (int)RoboUScriptCommon);
        RoboCheck(!RoboTextScriptIsAnchor(RoboTextUnicodeScript('7')));
    }
    /* Other scripts' digits are *theirs*: Arabic-Indic, Devanagari, Thai and Myanmar all print differently
     * and are classified differently — the classification is the property, never the appearance. */
    {
        static const ScriptRunExpectation arabic[] = { { 0, 3, RoboUScriptArabic } };
        static const ScriptRunExpectation thai[] = { { 0, 3, RoboUScriptThai } };
        static const ScriptRunExpectation myanmar[] = { { 0, 3, RoboUScriptMyanmar } };

        ExpectScriptRuns(ScalarArabicIndicDigits, arabic, 1u);
        ExpectScriptRuns(ScalarThaiDigits, thai, 1u);
        ExpectScriptRuns(ScalarMyanmarDigits, myanmar, 1u);
        RoboCheckEqInt((int)RoboTextUnicodeScript(0x0661u), (int)RoboUScriptArabic);
        RoboCheckEqInt((int)RoboTextUnicodeScript(0x0E51u), (int)RoboUScriptThai);
        RoboCheckEqInt((int)RoboTextUnicodeScript(0x1041u), (int)RoboUScriptMyanmar);
    }
    /* A script's digits among its own text join it rather than splitting it: 3 + 1 + 3 + 1 + 3 scalars,
     * one run. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 11, RoboUScriptThai } };

        ExpectScriptRuns(ScriptThai " " ScalarThaiDigits " " ScriptThai, expected, 1u);
    }
}

/* ------------------------------------------------------------------ */
/* Combining marks                                                     */
/* ------------------------------------------------------------------ */

static void TestScriptCombiningMarks(void)
{
    /* The phase's own example: `e` + combining acute stays one Latin run. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 2, RoboUScriptLatin } };

        ExpectScriptRuns("e" ScalarAcute, expected, 1u);
    }
    {
        static const ScriptRunExpectation expected[] = { { 0, 2, RoboUScriptLatin } };

        ExpectScriptRuns("a" ScalarRing, expected, 1u);
    }
    /* A mark in the middle of a run does not split it, and neither does one at the end. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 3, RoboUScriptLatin } };

        ExpectScriptRuns("e" ScalarAcute "x", expected, 1u);
    }
    {
        static const ScriptRunExpectation expected[] = { { 0, 2, RoboUScriptLatin } };

        ExpectScriptRuns("x" ScalarAcute, expected, 1u);
    }
    /* A leading mark has no base on its left, so it attaches *right* rather than becoming its own run: a
     * mark is never a shaping context of its own. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 2, RoboUScriptLatin } };
        static const ScriptRunExpectation surrounded[] = { { 0, 3, RoboUScriptLatin } };

        ExpectScriptRuns(ScalarAcute "e", expected, 1u);
        ExpectScriptRuns(ScalarAcute "e" ScalarAcute, surrounded, 1u);
    }
    /* A mark with neither base *is* neutral, and the honest answer for an unresolved neutral block is
     * Common — `Inherited` is not a script a run can claim any more than Common is. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 1, RoboUScriptCommon } };
        RoboTextScriptRun runs[2];
        size_t count = 0u;

        memset(runs, 0, sizeof(runs));
        ExpectScriptRuns(ScalarAcute, expected, 1u);
        RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ScriptView(ScalarAcute), RoboTextScriptCommon,
                                                          runs, 2u, &count, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)count, 1);
        RoboCheck(RoboTextScriptIsNeutral(runs[0].script));
    }
    /* The mark rule is checked **before** the extension table, and that is the point: U+0345 lists Grek
     * and nothing else, so an implementation that consulted extensions first would pull it out of a Latin
     * base. */
    {
        static const ScriptRunExpectation latin[] = { { 0, 2, RoboUScriptLatin } };
        static const ScriptRunExpectation greek[] = { { 0, 2, RoboUScriptGreek } };

        RoboCheck(RoboTextUnicodeScriptExtensionsInclude(0x0345u, RoboUScriptGreek));
        RoboCheck(!RoboTextUnicodeScriptExtensionsInclude(0x0345u, RoboUScriptLatin));
        ExpectScriptRuns("a" ScalarYpogegrammeni, latin, 1u);
        ExpectScriptRuns("\xCE\x9B" ScalarYpogegrammeni, greek, 1u);
    }
    /* A combining mark whose own script is a real one — not Inherited — anchors it, so Hebrew's accent is
     * Hebrew and joins the word instead of being resolved as a neutral. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 3, RoboUScriptHebrew } };

        RoboCheckEqInt((int)RoboTextUnicodeScript(0x0591u), (int)RoboUScriptHebrew);
        RoboCheck(RoboTextUnicodeIsCombiningMark(0x0591u));
        RoboCheck(!RoboTextScriptIsNeutral(RoboTextUnicodeScript(0x0591u)));
        ExpectScriptRuns(ScalarHebrewAlef ScalarHebrewAccent ScalarHebrewBet, expected, 1u);
    }
    /* A mark between two different scripts stays with the base on its left. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 2, RoboUScriptLatin },
                                                         { 2, 3, RoboUScriptGreek } };

        ExpectScriptRuns("a" ScalarAcute "\xCE\xA9", expected, 2u);
    }
    /* And a mark no installed face covers is still resolved by script: classification is text, not font. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 2, RoboUScriptLatin } };

        ExpectScriptRuns("e" ScalarMarkNoFace, expected, 1u);
    }
}

/* ------------------------------------------------------------------ */
/* Script extensions                                                   */
/* ------------------------------------------------------------------ */

static void TestScriptExtensionsFacts(void)
{
    RoboTextUnicodeScriptInfo info;
    RoboTextScript extensions[RoboTextScriptExtensionsMax];

    /* A code point with no listed set: the property defaults to the singleton `{Script}`, and the record
     * says so rather than pretending a one-entry set was listed. */
    RoboCheckEqInt((int)RoboTextUnicodeScriptInfoOf(0x0020u, &info, NULL), (int)RoboOk);
    RoboCheckEqInt((int)info.primaryScript, (int)RoboUScriptCommon);
    RoboCheckEqInt((int)info.extensionCount, 1);
    RoboCheckEqInt((int)info.extensions[0], (int)RoboUScriptCommon);
    RoboCheckEqInt(info.extensionsExplicit, 0);
    RoboCheckEqInt(info.extensionsTruncated, 0);

    /* A combining acute is Inherited and lists Latin, Greek and Cyrillic among others — the fact the
     * mark rule makes unnecessary, and that the neutral path would otherwise need. */
    RoboCheckEqInt((int)RoboTextUnicodeScriptInfoOf(0x0301u, &info, NULL), (int)RoboOk);
    RoboCheckEqInt((int)info.primaryScript, (int)RoboUScriptInherited);
    RoboCheckEqInt(info.extensionsExplicit, 1);
    RoboCheck(RoboTextUnicodeScriptInfoHas(&info, RoboUScriptLatin));
    RoboCheck(RoboTextUnicodeScriptInfoHas(&info, RoboUScriptGreek));
    RoboCheck(RoboTextUnicodeScriptInfoHas(&info, RoboUScriptCyrillic));
    RoboCheck(!RoboTextUnicodeScriptInfoHas(&info, RoboUScriptArabic));
    RoboCheck(!RoboTextUnicodeScriptInfoHas(&info, RoboUScriptHan));

    /* An explicit singleton is still explicit, and the pair `Script != Inherited` is exactly what tells it
     * apart from a defaulted set. */
    RoboCheckEqInt((int)RoboTextUnicodeScriptInfoOf(0x0345u, &info, NULL), (int)RoboOk);
    RoboCheckEqInt((int)info.primaryScript, (int)RoboUScriptInherited);
    RoboCheckEqInt((int)info.extensionCount, 1);
    RoboCheckEqInt((int)info.extensions[0], (int)RoboUScriptGreek);
    RoboCheckEqInt(info.extensionsExplicit, 1);

    /* A shared character: tatweel is Common and may be written in Arabic or Syriac. */
    RoboCheckEqInt((int)RoboTextUnicodeScriptInfoOf(0x0640u, &info, NULL), (int)RoboOk);
    RoboCheckEqInt((int)info.primaryScript, (int)RoboUScriptCommon);
    RoboCheckEqInt((int)info.extensionCount, 9);
    RoboCheck(RoboTextUnicodeScriptInfoHas(&info, RoboUScriptArabic));
    RoboCheck(RoboTextUnicodeScriptInfoHas(&info, RoboUScriptSyriac));
    RoboCheck(!RoboTextUnicodeScriptInfoHas(&info, RoboUScriptLatin));

    /* The two doors agree entry for entry, over a matrix of characters and scripts. */
    {
        static const RoboTextCodePoint codePoints[] = { 0x0020u, 0x0301u, 0x0345u, 0x02BCu, 0x0640u,
                                                        0x0964u, 0x3001u, 0x00B7u, 0x0041u, 0x0031u };
        static const RoboTextScript scripts[] = { RoboUScriptCommon, RoboUScriptInherited, RoboUScriptLatin,
                                                  RoboUScriptGreek,  RoboUScriptCyrillic, RoboUScriptArabic,
                                                  RoboUScriptSyriac, RoboUScriptDevanagari, RoboUScriptTamil,
                                                  RoboUScriptHan,    RoboUScriptHangul, RoboUScriptThai };
        size_t a;
        size_t b;

        for (a = 0u; a < sizeof(codePoints) / sizeof(codePoints[0]); a++) {
            size_t total = RoboTextUnicodeScriptExtensions(codePoints[a], extensions,
                                                           RoboTextScriptExtensionsMax);
            RoboTextUnicodeScriptInfo scalarInfo;

            RoboCheckEqInt((int)RoboTextUnicodeScriptInfoOf(codePoints[a], &scalarInfo, NULL), (int)RoboOk);
            RoboCheckEqInt((int)total, (int)scalarInfo.extensionCount);
            for (b = 0u; b < total && b < RoboTextScriptExtensionsMax; b++) {
                RoboCheckEqInt((int)extensions[b], (int)scalarInfo.extensions[b]);
            }
            for (b = 0u; b < sizeof(scripts) / sizeof(scripts[0]); b++) {
                int has = RoboTextUnicodeScriptInfoHas(&scalarInfo, scripts[b]);
                int includes = RoboTextUnicodeScriptExtensionsInclude(codePoints[a], scripts[b]);

                RoboCheckEqInt(has, includes);
                if (has) {
                    /* A member of the set is a member whichever door is asked, and the primary script of a
                     * scalar with an explicit set need not be in it — tatweel is `Common` and lists nine
                     * other scripts, which is exactly why the two properties must not be conflated. */
                    RoboCheck(RoboTextUnicodeScriptInfoHas(&scalarInfo, scripts[b]));
                }
            }
        }
    }

    /* The per-scalar door writes at most `capacity` entries and returns the total, so a caller with a
     * small buffer learns the set is longer rather than believing a prefix is the whole of it. */
    {
        size_t total = RoboTextUnicodeScriptExtensions(0x00B7u, extensions, 4u);

        RoboCheck(total > 4u);
        RoboCheckEqInt((int)RoboTextUnicodeScriptExtensions(0x00B7u, NULL, 0u), (int)total);
    }

    /* Refusals and the non-character case: a lone surrogate is not a character, so it has no set — and it
     * is answered rather than failed, like coverage. */
    RoboCheckEqInt((int)RoboTextUnicodeScriptInfoOf(0x0020u, NULL, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextUnicodeScriptInfoOf(0xD800u, &info, NULL), (int)RoboOk);
    RoboCheckEqInt((int)info.primaryScript, (int)RoboUScriptUnknown);
    RoboCheckEqInt((int)info.extensionCount, 1);
    RoboCheckEqInt((int)info.extensions[0], (int)RoboUScriptUnknown);
    RoboCheck(!RoboTextUnicodeScriptExtensionsInclude(0xD800u, RoboUScriptUnknown));
    RoboCheckEqInt((int)RoboTextUnicodeScriptExtensions(0xD800u, extensions,
                                                        RoboTextScriptExtensionsMax),
                   0);
}

static void TestScriptExtensionsResolution(void)
{
    /* The distinguishing case: a neutral whose set lists the anchor on the **right** but not the one on
     * the left resolves to the right — where a plain carry-left would say Greek. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 1, RoboUScriptGreek },
                                                         { 1, 3, RoboUScriptLatin } };
        static const ScriptRunExpectation mirrored[] = { { 0, 2, RoboUScriptLatin },
                                                         { 2, 3, RoboUScriptGreek } };

        RoboCheck(RoboTextUnicodeScriptExtensionsInclude(0x02BCu, RoboUScriptLatin));
        RoboCheck(!RoboTextUnicodeScriptExtensionsInclude(0x02BCu, RoboUScriptGreek));
        ExpectScriptRuns("\xCE\x9B" ScalarModifierApostrophe "a", expected, 2u);
        ExpectScriptRuns("a" ScalarModifierApostrophe "\xCE\xA9", mirrored, 2u);
    }
    /* A neutral listed by **both** differing neighbours keeps its neutrality: the context genuinely does
     * not decide, and inventing a script would be the thing the phase forbids. */
    {
        static const ScriptRunExpectation thai[] = { { 0, 1, RoboUScriptLatin },
                                                     { 1, 2, RoboUScriptCommon },
                                                     { 2, 3, RoboUScriptThai } };

        RoboCheck(RoboTextUnicodeScriptExtensionsInclude(0x02BCu, RoboUScriptThai));
        ExpectScriptRuns("a" ScalarModifierApostrophe "\xE0\xB8\x97", thai, 3u);
    }
    /* Tatweel between Arabic and Syriac: the UCD lists both, so it stays Common between them. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 1, RoboUScriptArabic },
                                                         { 1, 2, RoboUScriptCommon },
                                                         { 2, 3, RoboUScriptSyriac } };

        ExpectScriptRuns(ScalarArabicAlef ScalarTatweel ScalarSyriacAlaph, expected, 3u);
    }
    /* But the `prev == next` short-circuit is checked first, so the same character inside one script never
     * reaches the table and never splits the run. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 3, RoboUScriptArabic } };

        ExpectScriptRuns(ScalarArabicAlef ScalarTatweel ScalarArabicBeh, expected, 1u);
    }
    /* The danda lists 21 Indic scripts and not Latin, so after Latin it resolves right to the Devanagari
     * that follows. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 1, RoboUScriptLatin },
                                                         { 1, 8, RoboUScriptDevanagari } };

        RoboCheck(RoboTextUnicodeScriptExtensionsInclude(0x0964u, RoboUScriptDevanagari));
        RoboCheck(!RoboTextUnicodeScriptExtensionsInclude(0x0964u, RoboUScriptLatin));
        ExpectScriptRuns("a" ScalarDanda ScriptDevanagari, expected, 2u);
    }
    /* And between two *different* Indic scripts it is listed by both, so it stays neutral. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 1, RoboUScriptDevanagari },
                                                         { 1, 2, RoboUScriptCommon },
                                                         { 2, 3, RoboUScriptTamil } };

        ExpectScriptRuns("\xE0\xA4\xA8" ScalarDanda ScriptTamil, expected, 3u);
    }
    /* The ideographic comma lists Han, Hangul and the Japanese kana: neutral between Han and Hangul, and
     * absorbed when both sides are Han. */
    {
        static const ScriptRunExpectation between[] = { { 0, 1, RoboUScriptHan },
                                                        { 1, 2, RoboUScriptCommon },
                                                        { 2, 3, RoboUScriptHangul } };
        static const ScriptRunExpectation within[] = { { 0, 3, RoboUScriptHan } };

        RoboCheck(RoboTextUnicodeScriptExtensionsInclude(0x3001u, RoboUScriptHan));
        RoboCheck(RoboTextUnicodeScriptExtensionsInclude(0x3001u, RoboUScriptHangul));
        ExpectScriptRuns("\xE4\xB8\xAD" ScalarIdeographicComma "\xED\x95\x9C", between, 3u);
        ExpectScriptRuns("\xE4\xB8\xAD" ScalarIdeographicComma "\xE6\x96\x87", within, 1u);
    }
    /* The same both-listed shape with a Latin/Greek pair and no space: the middle dot lists both, so the
     * boundary falls on either side of it and the dot is its own neutral run. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 1, RoboUScriptGreek },
                                                         { 1, 2, RoboUScriptCommon },
                                                         { 2, 3, RoboUScriptLatin } };

        RoboCheck(RoboTextUnicodeScriptExtensionsInclude(0x00B7u, RoboUScriptGreek));
        RoboCheck(RoboTextUnicodeScriptExtensionsInclude(0x00B7u, RoboUScriptLatin));
        ExpectScriptRuns("\xCE\x9B" ScalarMiddleDot "a", expected, 3u);
    }
    /* A Japanese sentence is Han + kana, so it is *not* one run — kana are their own scripts, and the
     * phase's answer must say so rather than calling the whole thing Han by association. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 1, RoboUScriptHiragana },
                                                         { 1, 3, RoboUScriptHan },
                                                         { 3, 4, RoboUScriptKatakana } };

        ExpectScriptRuns("\xE3\x81\x82" "\xE6\x97\xA5\xE6\x9C\xAC" "\xE3\x82\xA2", expected, 3u);
    }
}

/* ------------------------------------------------------------------ */
/* Symbols and emoji                                                   */
/* ------------------------------------------------------------------ */

static void TestScriptSymbolsAndEmoji(void)
{
    /* Symbols are `Zyyy`: two of them plus a space is one Common run, not three. */
    {
        static const ScriptRunExpectation copyright[] = { { 0, 3, RoboUScriptCommon } };
        static const ScriptRunExpectation arrows[] = { { 0, 7, RoboUScriptCommon } };

        ExpectScriptRuns(ScalarCopyright ScalarRegistered ScalarTradeMark, copyright, 1u);
        ExpectScriptRuns(ScalarArrowRight " " ScalarArrowLeft " " ScalarArrowUp " " ScalarArrowDown,
                         arrows, 1u);
        RoboCheckEqInt((int)RoboTextUnicodeGeneralCategory(0x00A9u), (int)RoboUGcSo);
        RoboCheckEqInt((int)RoboTextUnicodeGeneralCategory(0x2192u), (int)RoboUGcSm);
    }
    /* Emoji are `Zyyy` too — there is no `Emoji` script to invent, and the phase forbids inventing one. */
    {
        static const ScriptRunExpectation one[] = { { 0, 1, RoboUScriptCommon } };
        static const ScriptRunExpectation heart[] = { { 0, 2, RoboUScriptCommon } };
        static const ScriptRunExpectation family[] = { { 0, 5, RoboUScriptCommon } };

        ExpectScriptRuns(ScalarGrinning, one, 1u);
        ExpectScriptRuns(ScalarRocket, one, 1u);
        /* The heart is followed by a variation selector, which is Inherited and attaches left. */
        ExpectScriptRuns(ScalarHeart, heart, 1u);
        /* A ZWJ sequence is five scalars — three pictographs and two joiners — and still one run. */
        ExpectScriptRuns(ScalarZwjFamily, family, 1u);
        RoboCheckEqInt((int)RoboTextUnicodeScript(0x1F600u), (int)RoboUScriptCommon);
        RoboCheckEqInt((int)RoboTextUnicodeScript(0xFE0Fu), (int)RoboUScriptInherited);
        RoboCheck(UnicodeIsExtendedPictographic(0x1F600u));
        RoboCheck(!RoboTextScriptIsAnchor(RoboUScriptCommon));
        /* No script short name in this build is "Emoji": the vocabulary is Unicode's, and a second table
         * invented for emoji would be exactly the parallel implementation the phase forbids. */
        {
            int index = 0;

            for (index = 0; index < (int)RoboUScriptCount; index++) {
                RoboCheck(strcmp(RoboTextScriptName((RoboTextScript)index), "Emoji") != 0);
            }
        }
    }
    /* An emoji between two scripts is a neutral between differing anchors: it takes the left carry, and it
     * does **not** grow a run of its own. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 2, RoboUScriptLatin },
                                                         { 2, 3, RoboUScriptHan } };

        ExpectScriptRuns("A" ScalarGrinning "\xE4\xB8\xAD", expected, 2u);
    }
    /* An emoji inside one script is that script's, and the run does not split. */
    {
        static const ScriptRunExpectation expected[] = { { 0, 3, RoboUScriptLatin } };

        ExpectScriptRuns("A" ScalarGrinning "B", expected, 1u);
    }
}

/* ------------------------------------------------------------------ */
/* Unknown / unassigned                                                */
/* ------------------------------------------------------------------ */

static void TestScriptUnknown(void)
{
    /* An unassigned scalar is `Zzzz`, and `Zzzz` anchors: the phase's rule is that it must not be
     * absorbed into a neighbouring script, least of all Latin. */
    RoboCheckEqInt((int)RoboTextUnicodeScript(0x0378u), (int)RoboUScriptUnknown);
    RoboCheckStrEq(RoboTextScriptName(RoboUScriptUnknown), "Zzzz");
    RoboCheck(!RoboTextScriptIsNeutral(RoboUScriptUnknown));
    RoboCheck(RoboTextScriptIsAnchor(RoboUScriptUnknown));

    {
        static const ScriptRunExpectation alone[] = { { 0, 1, RoboUScriptUnknown } };
        static const ScriptRunExpectation between[] = { { 0, 1, RoboUScriptLatin },
                                                        { 1, 2, RoboUScriptUnknown },
                                                        { 2, 3, RoboUScriptLatin } };

        ExpectScriptRuns(ScalarUnassigned, alone, 1u);
        ExpectScriptRuns("A" ScalarUnassigned "B", between, 3u);
    }
    /* The general category is `Cn` and the code point is not assigned — the classification is honest about
     * both, and neither is an error. */
    RoboCheckEqInt((int)RoboTextUnicodeGeneralCategory(0x0378u), (int)RoboUGcCn);
    RoboCheck(!RoboTextUnicodeIsAssigned(0x0378u));
    RoboCheckEqInt((int)RoboTextUnicodeCategory(0x0378u), (int)RoboTextCategoryOther);

    /* A noncharacter is unassigned too, and the private use area is *assigned* — two different facts that a
     * single "unknown" flag would conflate. */
    RoboCheckEqInt((int)RoboTextUnicodeScript(0xFDD0u), (int)RoboUScriptUnknown);
    RoboCheck(RoboTextUnicodeIsAssigned(0xE000u));
    RoboCheck(!RoboTextUnicodeIsAssigned(0xFDD0u));
}

/* ------------------------------------------------------------------ */
/* The script hint                                                     */
/* ------------------------------------------------------------------ */

static void TestScriptHint(void)
{
    /* A hint classifies every scalar as the hinted script: that is what a hint is for, and it is the
     * caller's claim rather than the data's. */
    RoboTextScriptRun runs[4];
    size_t count = 0u;

    memset(runs, 0, sizeof(runs));
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ScriptView(ScriptLatin), RoboUScriptCyrillic, runs,
                                                      4u, &count, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)runs[0].range.start, 0);
    RoboCheckEqInt((int)runs[0].range.end, 5);
    RoboCheckEqInt((int)runs[0].script, (int)RoboUScriptCyrillic);

    /* With no hint the same text is Latin, so the hint is the only difference. */
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ScriptView(ScriptLatin), RoboTextScriptCommon, runs,
                                                      4u, &count, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)runs[0].script, (int)RoboUScriptLatin);

    /* A hint over empty text still produces no runs. */
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(RoboTextViewMake(NULL, 0u), RoboUScriptCyrillic,
                                                      runs, 4u, &count, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 0);
}

/* ------------------------------------------------------------------ */
/* The ASCII fast path                                                 */
/* ------------------------------------------------------------------ */

static void TestScriptAsciiFastPath(void)
{
    RoboTextScript ascii;
    RoboTextCodePoint codePoint;

    /* The fast path answers exactly the ASCII block and declines everything else. */
    for (codePoint = 0u; codePoint <= 0x7Fu; codePoint++) {
        RoboCheck(RoboTextUnicodeAsciiScript(codePoint, &ascii));
    }
    RoboCheck(!RoboTextUnicodeAsciiScript(0x80u, &ascii));
    RoboCheck(!RoboTextUnicodeAsciiScript(0x0301u, &ascii));
    RoboCheck(!RoboTextUnicodeAsciiScript(0xD800u, &ascii));
    RoboCheck(!RoboTextUnicodeAsciiScript(0x10FFFFu, &ascii));
    RoboCheck(!RoboTextUnicodeAsciiScript('A', NULL));

    /*
     * And it is **equivalent** to the tables over the whole block: the property door, the extension door,
     * the extension set and the general category all agree with it. This is what makes the shortcut a
     * shortcut rather than a guess — if a future UCD moved an ASCII code point's script, this loop fails
     * rather than the resolver silently disagreeing with the data.
     */
    for (codePoint = 0u; codePoint <= 0x7Fu; codePoint++) {
        RoboTextUnicodeScriptInfo info;
        RoboTextScript fast = RoboUScriptUnknown;
        int isLetter = (codePoint >= (RoboTextCodePoint)'A' && codePoint <= (RoboTextCodePoint)'Z') ||
                       (codePoint >= (RoboTextCodePoint)'a' && codePoint <= (RoboTextCodePoint)'z');

        RoboCheck(RoboTextUnicodeAsciiScript(codePoint, &fast));
        RoboCheckEqInt((int)fast, (int)(isLetter ? RoboUScriptLatin : RoboUScriptCommon));

        /* The `Script` property agrees. */
        RoboCheckEqInt((int)RoboTextUnicodeScript(codePoint), (int)fast);

        /* No ASCII code point lists an explicit `Script_Extensions` set, so the door reduces to equality. */
        RoboCheckEqInt((int)RoboTextUnicodeScriptInfoOf(codePoint, &info, NULL), (int)RoboOk);
        RoboCheckEqInt((int)info.primaryScript, (int)fast);
        RoboCheckEqInt((int)info.extensionCount, 1);
        RoboCheckEqInt((int)info.extensions[0], (int)fast);
        RoboCheckEqInt(info.extensionsExplicit, 0);
        RoboCheck(RoboTextUnicodeScriptExtensionsInclude(codePoint, fast));
        RoboCheck(!RoboTextUnicodeScriptExtensionsInclude(codePoint,
                                                          (fast == RoboUScriptLatin) ? RoboUScriptCommon
                                                                                     : RoboUScriptLatin));

        /* And no ASCII code point is a mark, which is the other half of the path — proven below U+0300. */
        RoboCheck(!RoboTextUnicodeIsCombiningMark(codePoint));
    }

    /* The mark bound, checked against the tables over every code point it covers rather than asserted. */
    for (codePoint = 0u; codePoint < 0x0300u; codePoint++) {
        RoboCheck(!RoboTextUnicodeIsCombiningMark(codePoint));
        RoboCheck(RoboTextUnicodeCategory(codePoint) != RoboTextCategoryMark);
    }
    /* And U+0300 itself is the first mark, so the bound is tight rather than merely true. */
    RoboCheck(RoboTextUnicodeIsCombiningMark(0x0300u));
    RoboCheckEqInt((int)RoboTextUnicodeCategory(0x0300u), (int)RoboTextCategoryMark);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static void TestScriptDiagnostics(void)
{
    /* `"Hello مرحبا 世界"`: three script runs, and the table names both index spaces on every line, which
     * is the whole reason it takes the view. */
    static const char *text = "Hello " ScriptArabic " " ScriptHan;
    static const char *expectedTable =
        "Script Analysis\n"
        "[0..6) scalars  bytes [0..6)  Latn\n"
        "[6..12) scalars  bytes [6..17)  Arab\n"
        "[12..14) scalars  bytes [17..23)  Hani\n";
    RoboTextScriptRun runs[ScriptRunMax];
    size_t count = 0u;
    RoboTextView view = ScriptView(text);
    char table[512];
    size_t length = 0u;

    memset(runs, 0, sizeof(runs));
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(view, RoboTextScriptCommon, runs, ScriptRunMax,
                                                      &count, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 3);
    RoboCheckEqInt((int)RoboTextScriptRunsWrite(view, runs, count, table, sizeof(table), &length),
                   (int)RoboOk);
    RoboCheckEqInt((int)strlen(table), (int)length);
    RoboCheckStrEq(table, expectedTable);

    /* One run, as a line — the form a caller logs without building a table. */
    {
        char line[64];

        RoboCheckEqInt((int)RoboTextScriptRunWrite(&runs[0], line, sizeof(line), &length), (int)RoboOk);
        RoboCheckStrEq(line, "[0..6) Latn");
        RoboCheckEqInt((int)RoboTextScriptRunWrite(&runs[1], line, sizeof(line), &length), (int)RoboOk);
        RoboCheckStrEq(line, "[6..12) Arab");
        RoboCheckEqInt((int)RoboTextScriptRunWrite(&runs[2], line, sizeof(line), &length), (int)RoboOk);
        RoboCheckStrEq(line, "[12..14) Hani");

        /* A line that does not fit is refused with the requirement, and writes nothing. */
        {
            char small[8];

            memset(small, 'x', sizeof(small));
            RoboCheckEqInt((int)RoboTextScriptRunWrite(&runs[0], small, sizeof(small), &length),
                           (int)RoboNotSupported);
            RoboCheckEqInt((int)length, 11);
            /* Refused, and left empty: a half-written line reads as a different run. */
            RoboCheckEqInt((int)small[0], 0);
        }
        RoboCheckEqInt((int)RoboTextScriptRunWrite(NULL, line, sizeof(line), &length),
                       (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextScriptRunWrite(&runs[0], line, 0u, &length), (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextScriptRunWrite(&runs[0], NULL, 4u, &length), (int)RoboInvalidArgument);
    }

    /* A buffer that cannot hold the table is refused with the requirement and left empty: a half-written
     * table reads as a different analysis. */
    {
        char small[32];

        memset(small, 'x', sizeof(small));
        RoboCheckEqInt((int)RoboTextScriptRunsWrite(view, runs, count, small, sizeof(small), &length),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)length, (int)strlen(expectedTable));
        RoboCheckEqInt((int)small[0], 0);
    }

    /* Without a view the table carries scalar ranges only — there is nothing to name bytes with. */
    {
        static const char *scalarOnly = "Script Analysis\n[0..6) scalars  Latn\n[6..12) scalars  Arab\n";

        RoboCheckEqInt((int)RoboTextScriptRunsWrite(RoboTextViewMake(NULL, 0u), runs, 2u, table,
                                                    sizeof(table), &length),
                       (int)RoboOk);
        RoboCheckStrEq(table, scalarOnly);
    }

    /* Runs that do not lie inside the view are refused rather than printed against another text's bytes. */
    {
        RoboTextScriptRun outside[1];
        RoboTextScriptRun inverted[1];

        outside[0].range = RoboTextRangeMake(0u, 99u);
        outside[0].script = RoboUScriptLatin;
        inverted[0].range = RoboTextRangeMake(14u, 12u);
        inverted[0].script = RoboUScriptLatin;
        RoboCheckEqInt((int)RoboTextScriptRunsWrite(view, outside, 1u, table, sizeof(table), &length),
                       (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextScriptRunsWrite(view, inverted, 1u, table, sizeof(table), &length),
                       (int)RoboInvalidArgument);
    }

    /* Empty and NULL arguments. */
    RoboCheckEqInt((int)RoboTextScriptRunsWrite(view, runs, 0u, table, sizeof(table), &length),
                   (int)RoboOk);
    RoboCheckStrEq(table, "Script Analysis\n");
    RoboCheckEqInt((int)RoboTextScriptRunsWrite(view, NULL, 3u, table, sizeof(table), &length),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextScriptRunsWrite(view, runs, 3u, table, 0u, &length),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextScriptRunsWrite(view, runs, 3u, NULL, sizeof(table), &length),
                   (int)RoboInvalidArgument);

    /* And the dump is the same when the runs came from the array door rather than the view door: one
     * writer, one format, whichever way the caller got here. */
    {
        char viaArray[512];

        RoboCheckEqInt((int)RoboTextScriptRunsWrite(view, runs, count, viaArray, sizeof(viaArray), &length),
                       (int)RoboOk);
        RoboCheckStrEq(viaArray, expectedTable);
    }
}

/* ------------------------------------------------------------------ */
/* Performance: one pass, one allocation, and a bounded run count      */
/* ------------------------------------------------------------------ */

static void TestScriptPerformance(void)
{
    /* A representative UI paragraph of 10–20 KB, built from the phase's own pangram. */
    static char paragraph[20480];
    Uchar32 *codePoints = NULL;
    RoboTextScalarIndex scalars = 0u;
    RoboTextScriptRun runs[ScriptRunMax];
    size_t count = 0u;
    size_t written = 0u;
    size_t mixedScalars = 0u;
    size_t mixedRuns = 0u;
    MemStats before;
    MemStats after;
    clock_t started;
    clock_t finished;

    while (written + sizeof(TextSamplePangram) - 1u < sizeof(paragraph) - 2u) {
        memcpy(paragraph + written, TextSamplePangram, sizeof(TextSamplePangram) - 1u);
        written += sizeof(TextSamplePangram) - 1u;
        paragraph[written++] = ' ';
    }
    paragraph[written] = '\0';

    /* The ASCII paragraph is **one** run: letters are anchors and every inter-word space takes the carry,
     * so the run count is 1 however long the text is. */
    memset(runs, 0, sizeof(runs));
    started = clock();
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ScriptView(paragraph), RoboTextScriptCommon, runs,
                                                      ScriptRunMax, &count, NULL),
                   (int)RoboOk);
    finished = clock();
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)runs[0].range.end, (int)written);
    RoboCheck(RoboTextScriptRunsResolveView(ScriptView(paragraph), RoboTextScriptCommon, NULL, 0u, &count,
                                            NULL) == RoboOk);
    RoboCheckEqInt((int)count, 1);

    /*
     * And the array door over the same scalars allocates **nothing**: the resolver's only storage is the
     * caller's run array, so its cost is one pass rather than one allocation per scalar. These two
     * assertions are the phase's §30 "allocation count" — real in a build with the debug allocator (the
     * test build) and vacuous without it, which is why the run count and the run array above are asserted
     * either way.
     */
    RoboCheckEqInt((int)RoboTextUnicodeDecodeAll(ScriptView(paragraph), &codePoints, &scalars, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)scalars, (int)written);
    MemDebugGetStats(&before);
    RoboCheckEqInt((int)RoboTextScriptRunsResolve(codePoints, (size_t)scalars, RoboTextScriptCommon, NULL,
                                                  runs, ScriptRunMax, &count, NULL),
                   (int)RoboOk);
    MemDebugGetStats(&after);
    RoboCheckEqInt((int)after.totalAllocs - (int)before.totalAllocs, 0);
    RoboCheckEqInt((int)after.totalFrees - (int)before.totalFrees, 0);
    RoboCheckEqInt((int)count, 1);

    /* The whole view door — decode once, resolve once — is exactly one allocation, for the scalars. */
    MemDebugGetStats(&before);
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ScriptView(paragraph), RoboTextScriptCommon, runs,
                                                      ScriptRunMax, &count, NULL),
                   (int)RoboOk);
    MemDebugGetStats(&after);
    RoboCheckEqInt((int)after.totalAllocs - (int)before.totalAllocs, 1);
    RoboCheckEqInt((int)after.totalFrees - (int)before.totalFrees, 1);

    /* Mixed scripts scale with the number of *boundaries*, not with the number of scalars: a Latin
     * sentence with one Greek word repeated resolves to one Latin run and one Greek run per word boundary,
     * inside one shared run array. */
    {
        static char mixed[4096];
        size_t mixedWritten = 0u;
        size_t latinStep = sizeof(ScriptLatin " ") - 1u;
        size_t greekStep = sizeof(ScriptGreek " ") - 1u;

        while (mixedWritten + latinStep + greekStep < sizeof(mixed) - 2u) {
            memcpy(mixed + mixedWritten, ScriptLatin " ", latinStep);
            mixedWritten += latinStep;
            memcpy(mixed + mixedWritten, ScriptGreek " ", greekStep);
            mixedWritten += greekStep;
        }
        mixed[mixedWritten] = '\0';

        MemDebugGetStats(&before);
        RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ScriptView(mixed), RoboTextScriptCommon, runs,
                                                          ScriptRunMax, &count, NULL),
                       (int)RoboNotSupported);
        MemDebugGetStats(&after);
        /* A run array that cannot hold the answer is refused with the requirement rather than truncated,
         * and the refusal still allocates only the scalars it had to decode. */
        RoboCheck(count > ScriptRunMax);
        /* Exactly two runs per repetition — Latin then Greek — because the trailing space of each word carries
         * its own script and the next word needs no extra run. */
        RoboCheckEqInt((int)count, (int)(2u * (mixedWritten / (latinStep + greekStep))));
        RoboCheckEqInt((int)after.totalAllocs - (int)before.totalAllocs, 1);
        RoboCheckEqInt((int)after.totalFrees - (int)before.totalFrees, 1);
        mixedScalars = mixedWritten;
        mixedRuns = count;
    }

    /* The phase asks for this to be *recorded*, so it is printed: bytes in, runs out, allocations, time. */
    printf("text.script.performance: ascii %u bytes -> 1 run, mixed %u bytes -> %u runs, "
           "1 allocation each, %ld ms\n",
           (unsigned)written, (unsigned)mixedScalars, (unsigned)mixedRuns,
           (long)((finished - started) * 1000L / CLOCKS_PER_SEC));

    RoboTextUnicodeScalarsFree(codePoints);
}

/* ------------------------------------------------------------------ */
/* Empty text, refusals and the analysis integration                   */
/* ------------------------------------------------------------------ */

static void TestScriptEmptyAndRefusals(void)
{
    RoboTextScriptRun runs[4];
    /* One resolution per scalar, so this must hold the longest text in the case below — a short array here
     * is a buffer overflow, not a test failure. */
    RoboTextScript scripts[32];
    size_t count = 7u;
    RoboTextView empty = RoboTextViewMake(NULL, 0u);

    memset(runs, 0, sizeof(runs));

    /* Empty text produces no runs, through both doors, and does not allocate. */
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(empty, RoboTextScriptCommon, runs, 4u, &count,
                                                      NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 0);
    RoboCheckEqInt((int)RoboTextScriptRunsResolve(NULL, 0u, RoboTextScriptCommon, scripts, runs, 4u,
                                                  &count, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 0);
    RoboCheckEqInt((int)RoboTextScriptRunsResolve(NULL, 0u, RoboTextScriptCommon, NULL, NULL, 0u, &count,
                                                  NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 0);

    /* Refusals: no out count, scalars with a count but no array, and a capacity that cannot hold the
     * answer (which reports the requirement rather than truncating). */
    RoboCheckEqInt((int)RoboTextScriptRunsResolve(NULL, 0u, RoboTextScriptCommon, NULL, NULL, 0u, NULL,
                                                  NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextScriptRunsResolve(NULL, 4u, RoboTextScriptCommon, NULL, runs, 4u, &count,
                                                  NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ScriptView(ScriptLatin), RoboTextScriptCommon, NULL,
                                                      0u, NULL, NULL),
                   (int)RoboInvalidArgument);
    count = 0u;
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ScriptView(ScriptLatin), RoboTextScriptCommon, runs,
                                                      0u, &count, NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)count, 1);
    /* A view with a length and no bytes is not a text, and is refused by the decoder. */
    RoboCheck(RoboTextScriptRunsResolveView(RoboTextViewMake(NULL, 3u), RoboTextScriptCommon, runs, 4u,
                                            &count, NULL) != RoboOk);
    /* A hint cannot change the refusals. */
    RoboCheckEqInt((int)RoboTextScriptRunsResolveView(ScriptView(ScriptLatin), RoboUScriptCyrillic, runs,
                                                      0u, &count, NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)count, 1);

    /* The per-scalar array door fills the resolutions too, and every entry is an anchor or a neutral. */
    {
        Uchar32 *codePoints = NULL;
        RoboTextScalarIndex scalars = 0u;

        RoboCheckEqInt((int)RoboTextUnicodeDecodeAll(ScriptView("Hello, " ScriptGreek "!"), &codePoints,
                                                     &scalars, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextScriptRunsResolve(codePoints, (size_t)scalars, RoboTextScriptCommon,
                                                      scripts, runs, 4u, &count, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)count, 2);
        RoboCheckEqInt((int)runs[0].script, (int)RoboUScriptLatin);
        RoboCheckEqInt((int)runs[1].script, (int)RoboUScriptGreek);
        /* The trailing `!` resolves into the Greek run, and its resolution says so. */
        RoboCheckEqInt((int)scripts[scalars - 1u], (int)RoboUScriptGreek);
        RoboCheckEqInt((int)scripts[0], (int)RoboUScriptLatin);
        RoboTextUnicodeScalarsFree(codePoints);
    }

    /*
     * Fuzz-shaped input: arbitrary bytes never crash the decoder or the resolver. What is asserted is the
     * *contract* over whatever it decides the text is — never a particular classification, because the
     * point of the case is that there is no assumption to break. One run per scalar is the array's worst
     * case, so it is sized for it.
     */
    {
        static uint8_t junk[512];
        static RoboTextScriptRun junkRuns[512];
        RoboTextView junkView;
        size_t index;
        size_t junkCount = 0u;

        for (index = 0u; index < sizeof(junk); index++) {
            junk[index] = (uint8_t)(index * 97u + 11u);
        }
        junkView = RoboTextViewMake((const char *)junk, sizeof(junk));
        memset(junkRuns, 0, sizeof(junkRuns));
        RoboCheckEqInt((int)RoboTextScriptRunsResolveView(junkView, RoboTextScriptCommon, junkRuns,
                                                          512u, &junkCount, NULL),
                       (int)RoboOk);
        RoboCheck(junkCount >= 1u);
        RoboCheckEqInt((int)junkRuns[0].range.start, 0);
        for (index = 0u; index + 1u < junkCount; index++) {
            RoboCheckEqInt((int)junkRuns[index].range.end, (int)junkRuns[index + 1u].range.start);
            RoboCheck(junkRuns[index].range.start < junkRuns[index].range.end);
            RoboCheck(junkRuns[index].script != junkRuns[index + 1u].script);
        }
        RoboCheck(junkRuns[junkCount - 1u].range.end <= (RoboTextScalarIndex)sizeof(junk));
        /* A capacity that cannot hold the answer is refused, and only then: the same call with one slot
         * succeeds exactly when the answer is one run. */
        {
            size_t one = 0u;

            RoboCheckEqInt((int)RoboTextScriptRunsResolveView(junkView, RoboTextScriptCommon, junkRuns, 1u,
                                                              &one, NULL),
                           (int)((junkCount > 1u) ? RoboNotSupported : RoboOk));
            RoboCheckEqInt((int)one, (int)junkCount);
        }
    }
}

static void TestScriptAnalysisIntegration(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontSelector *selector = NULL;
    RoboTextAnalysis *analysis = NULL;
    RoboTextShapeRequest request;
    RoboTextAnalysisStats stats;
    static const char *text = "Hello " ScriptArabic;

    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&selector, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, &fixture.font, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextAnalysisCreate(ScriptView(text), selector, NULL, &analysis, NULL),
                   (int)RoboOk);

    /* The script runs the analysis holds are the resolver's, and the analysis runs cut them by direction
     * — so the two agree here, where the direction is constant inside each script run. */
    RoboCheckEqInt((int)RoboTextAnalysisScriptRunCount(analysis), 2);
    RoboCheckEqInt((int)RoboTextAnalysisRunCount(analysis), 2);
    RoboCheckEqInt((int)RoboTextAnalysisScriptRun(analysis, 0u)->script, (int)RoboUScriptLatin);
    RoboCheckEqInt((int)RoboTextAnalysisScriptRun(analysis, 1u)->script, (int)RoboUScriptArabic);
    RoboCheck(RoboTextAnalysisScriptRun(analysis, 2u) == NULL);
    RoboCheck(RoboTextRangeEqual(&RoboTextAnalysisScriptRun(analysis, 0u)->range,
                                 &RoboTextAnalysisRunAt(analysis, 0u)->range));
    RoboCheck(RoboTextRangeEqual(&RoboTextAnalysisScriptRun(analysis, 1u)->range,
                                 &RoboTextAnalysisRunAt(analysis, 1u)->range));

    RoboCheckEqInt((int)RoboTextAnalysisGetStats(analysis, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.scriptRuns, 2);
    RoboCheckEqInt((int)stats.rtlRuns, 1);

    /* The shape request the Arabic run produces carries the *resolved* script, which is the field 12.6
     * exists to fill. */
    RoboTextShapeRequestInit(&request);
    RoboCheckEqInt((int)RoboTextAnalysisShapeRequest(analysis, 1u, &request), (int)RoboOk);
    RoboCheckEqInt((int)request.script, (int)RoboUScriptArabic);
    RoboCheckEqInt((int)request.direction, (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)request.range.start, 6);
    RoboCheckEqInt((int)request.range.end, 11);
    /* No installed family covers Arabic, so the run's face is absent — the script is real and the face is
     * honestly missing, and the two are separate facts. */
    RoboCheckEqInt((int)RoboTextAnalysisRunAt(analysis, 1u)->font.source, (int)RoboTextFontSourceNone);
    RoboCheckEqInt((int)RoboTextAnalysisShapeRequest(analysis, 0u, &request), (int)RoboOk);
    RoboCheckEqInt((int)request.script, (int)RoboUScriptLatin);
    RoboCheck(request.font.face != NULL);

    /* 12.5's coverage answers still stand beside the new classification — the two are independent, which
     * is what lets a font change move a boundary without moving a script. */
    {
        int supported = -1;

        RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(fixture.face, 'A', &supported, NULL),
                       (int)RoboOk);
        RoboCheckEqInt(supported, 1);
        RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(fixture.face, 0x4E2Du, &supported, NULL),
                       (int)RoboOk);
        RoboCheckEqInt(supported, 0);
        RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(fixture.face, 0x0645u, &supported, NULL),
                       (int)RoboOk);
        RoboCheckEqInt(supported, 0);
    }

    RoboTextAnalysisDestroy(analysis);
    RoboTextFontSelectorDestroy(selector);
    TextFixtureClose(&fixture);
}

void TestRegisterTextScript(void)
{
    TestRegister("text.script.pure_scripts", TestScriptPureScripts);
    TestRegister("text.script.mixed_scripts", TestScriptMixedScripts);
    TestRegister("text.script.common", TestScriptCommon);
    TestRegister("text.script.numbers", TestScriptNumbers);
    TestRegister("text.script.combining_marks", TestScriptCombiningMarks);
    TestRegister("text.script.extensions_facts", TestScriptExtensionsFacts);
    TestRegister("text.script.extensions_resolution", TestScriptExtensionsResolution);
    TestRegister("text.script.symbols_emoji", TestScriptSymbolsAndEmoji);
    TestRegister("text.script.unknown", TestScriptUnknown);
    TestRegister("text.script.hint", TestScriptHint);
    TestRegister("text.script.ascii_fast_path", TestScriptAsciiFastPath);
    TestRegister("text.script.diagnostics", TestScriptDiagnostics);
    TestRegister("text.script.performance", TestScriptPerformance);
    TestRegister("text.script.empty_and_refusals", TestScriptEmptyAndRefusals);
    TestRegister("text.script.analysis_integration", TestScriptAnalysisIntegration);
}
