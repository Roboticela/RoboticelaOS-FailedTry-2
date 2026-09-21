/*
 * RoboUnicode infrastructure suite (Goal 3.14).
 *
 * Version coherence across modules, broad script sampling, and a tiny
 * host-side microbench for property / emoji lookups.
 */

#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeBidi.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboUnicode/RoboUnicodeEmoji.h"
#include "RoboUnicode/RoboUnicodeGrapheme.h"
#include "RoboUnicode/RoboUnicodeLine.h"
#include "RoboUnicode/RoboUnicodeNorm.h"
#include "RoboUnicode/RoboUnicodeProps.h"
#include "RoboUnicode/RoboUnicodeSentence.h"
#include "RoboUnicode/RoboUnicodeVersion.h"
#include "RoboUnicode/RoboUnicodeWord.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void ExpectVersion(const char *label, const char *got)
{
    if (!got || strcmp(got, RoboUnicodeVersionString) != 0) {
        TestFail(__FILE__,
                       __LINE__,
                       "Unicode test failure\n"
                       "Category: VersionCoherence\n"
                       "Unicode version: %s\n"
                       "Module: %s\n"
                       "Expected: %s\n"
                       "Actual: %s",
                       RoboUnicodeVersionString,
                       label,
                       RoboUnicodeVersionString,
                       got ? got : "(null)");
    }
}

static void TestInfraVersions(void)
{
    RoboCheckStrEq(UnicodeVersionString(), RoboUnicodeVersionString);
    RoboCheckStrEq(RoboEmojiVersionString, "16.0");
    RoboCheckStrEq(RoboUnicodeGeneratorVersion, "1.0.0");

    ExpectVersion("ucd", UnicodeUcdVersionString());
    ExpectVersion("norm", UnicodeNormUcdVersionString());
    ExpectVersion("grapheme", UnicodeGraphemeUcdVersionString());
    ExpectVersion("case", UnicodeCaseUcdVersionString());
    ExpectVersion("word", UnicodeWordUcdVersionString());
    ExpectVersion("sentence", UnicodeSentenceUcdVersionString());
    ExpectVersion("line", UnicodeLineUcdVersionString());
    ExpectVersion("bidi", UnicodeBidiUcdVersionString());
    ExpectVersion("emoji_ucd", UnicodeEmojiUcdVersionString());
    RoboCheckStrEq(UnicodeEmojiVersionString(), RoboEmojiVersionString);
}

static void TestInfraBroadScripts(void)
{
    struct {
        Uchar32 cp;
        UnicodeScript want;
        const char *label;
    } samples[] = {
        {0x0041u, RoboUScriptLatin, "Latin"},
        {0x0391u, RoboUScriptGreek, "Greek"},
        {0x0410u, RoboUScriptCyrillic, "Cyrillic"},
        {0x0531u, RoboUScriptArmenian, "Armenian"},
        {0x10D0u, RoboUScriptGeorgian, "Georgian"},
        {0x0627u, RoboUScriptArabic, "Arabic"},
        {0x05D0u, RoboUScriptHebrew, "Hebrew"},
        {0x0905u, RoboUScriptDevanagari, "Devanagari"},
        {0x0985u, RoboUScriptBengali, "Bengali"},
        {0x0A85u, RoboUScriptGujarati, "Gujarati"},
        {0x0A05u, RoboUScriptGurmukhi, "Gurmukhi"},
        {0x0B85u, RoboUScriptTamil, "Tamil"},
        {0x0C05u, RoboUScriptTelugu, "Telugu"},
        {0x0C85u, RoboUScriptKannada, "Kannada"},
        {0x0D05u, RoboUScriptMalayalam, "Malayalam"},
        {0x0D85u, RoboUScriptSinhala, "Sinhala"},
        {0x0E01u, RoboUScriptThai, "Thai"},
        {0x0E81u, RoboUScriptLao, "Lao"},
        {0x1780u, RoboUScriptKhmer, "Khmer"},
        {0x1000u, RoboUScriptMyanmar, "Myanmar"},
        {0x1200u, RoboUScriptEthiopic, "Ethiopic"},
        {0xAC00u, RoboUScriptHangul, "Hangul"},
        {0x4E00u, RoboUScriptHan, "Han"},
        {0x3042u, RoboUScriptHiragana, "Hiragana"},
        {0x30A2u, RoboUScriptKatakana, "Katakana"},
        {0x103A0u, RoboUScriptOldPersian, "Old_Persian"},
        {0x1F600u, RoboUScriptCommon, "emoji_grin"},
    };
    size_t i;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        UnicodeScript got = MakeUnicodeScript(samples[i].cp);
        if (got != samples[i].want) {
            TestFail(__FILE__,
                           __LINE__,
                           "Unicode test failure\n"
                           "Category: ScriptSample\n"
                           "Unicode version: %s\n"
                           "Label: %s\n"
                           "Code point: U+%04X\n"
                           "Expected script: %u\n"
                           "Actual script: %u",
                           RoboUnicodeVersionString,
                           samples[i].label,
                           (unsigned)samples[i].cp,
                           (unsigned)samples[i].want,
                           (unsigned)got);
        }
    }

    RoboCheckEqInt(MakeUnicodeScript(0x067Eu), RoboUScriptArabic); /* پ */
    RoboCheck(UnicodeHasScript(0x060Cu, RoboUScriptArabic));
}

static void TestInfraMicrobench(void)
{
    volatile int sink = 0;
    clock_t t0, t1;
    size_t i;
    const size_t N = 200000u;
    double msProp, msEmoji;
    Uchar32 cps[] = {0x0041u, 0x0627u, 0x0905u, 0x4E00u, 0x1F600u, 0xAC00u};
    Uchar32 grin = 0x1F600u;

    t0 = clock();
    for (i = 0; i < N; i++) {
        sink += UnicodeIsEmoji(cps[i % 6]);
        sink += (int)UnicodeCategory(cps[i % 6]);
        sink += (int)MakeUnicodeScript(cps[i % 6]);
    }
    t1 = clock();
    msProp = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;

    t0 = clock();
    for (i = 0; i < N; i++) {
        sink += (int)UnicodeEmojiSequenceClassify(&grin, 1);
    }
    t1 = clock();
    msEmoji = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;

    printf("infra.microbench property_lookups=%zu time_ms=%.3f sink=%d\n",
           N,
           msProp,
           sink);
    printf("infra.microbench emoji_classify=%zu time_ms=%.3f\n", N, msEmoji);
    RoboCheck(sink != 0);
}

void TestRegisterUnicodeInfra(void)
{
    TestRegister("infra.versions", TestInfraVersions);
    TestRegister("infra.broad_scripts", TestInfraBroadScripts);
    TestRegister("infra.microbench", TestInfraMicrobench);
}
