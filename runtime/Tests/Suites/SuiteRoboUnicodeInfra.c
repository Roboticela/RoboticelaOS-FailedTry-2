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

static void expect_version(const char *label, const char *got)
{
    if (!got || strcmp(got, ROBO_UNICODE_VERSION_STRING) != 0) {
        robo_test_fail(__FILE__,
                       __LINE__,
                       "Unicode test failure\n"
                       "Category: VersionCoherence\n"
                       "Unicode version: %s\n"
                       "Module: %s\n"
                       "Expected: %s\n"
                       "Actual: %s",
                       ROBO_UNICODE_VERSION_STRING,
                       label,
                       ROBO_UNICODE_VERSION_STRING,
                       got ? got : "(null)");
    }
}

static void test_infra_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_unicode_version_string(), ROBO_UNICODE_VERSION_STRING);
    ROBO_CHECK_STR_EQ(ROBO_EMOJI_VERSION_STRING, "16.0");
    ROBO_CHECK_STR_EQ(ROBO_UNICODE_GENERATOR_VERSION, "1.0.0");

    expect_version("ucd", robo_unicode_ucd_version_string());
    expect_version("norm", robo_unicode_norm_ucd_version_string());
    expect_version("grapheme", robo_unicode_grapheme_ucd_version_string());
    expect_version("case", robo_unicode_case_ucd_version_string());
    expect_version("word", robo_unicode_word_ucd_version_string());
    expect_version("sentence", robo_unicode_sentence_ucd_version_string());
    expect_version("line", robo_unicode_line_ucd_version_string());
    expect_version("bidi", robo_unicode_bidi_ucd_version_string());
    expect_version("emoji_ucd", robo_unicode_emoji_ucd_version_string());
    ROBO_CHECK_STR_EQ(robo_unicode_emoji_version_string(), ROBO_EMOJI_VERSION_STRING);
}

static void test_infra_broad_scripts(void)
{
    struct {
        robo_uchar32_t cp;
        robo_unicode_script_t want;
        const char *label;
    } samples[] = {
        {0x0041u, ROBO_U_SCRIPT_LATIN, "Latin"},
        {0x0391u, ROBO_U_SCRIPT_GREEK, "Greek"},
        {0x0410u, ROBO_U_SCRIPT_CYRILLIC, "Cyrillic"},
        {0x0531u, ROBO_U_SCRIPT_ARMENIAN, "Armenian"},
        {0x10D0u, ROBO_U_SCRIPT_GEORGIAN, "Georgian"},
        {0x0627u, ROBO_U_SCRIPT_ARABIC, "Arabic"},
        {0x05D0u, ROBO_U_SCRIPT_HEBREW, "Hebrew"},
        {0x0905u, ROBO_U_SCRIPT_DEVANAGARI, "Devanagari"},
        {0x0985u, ROBO_U_SCRIPT_BENGALI, "Bengali"},
        {0x0A85u, ROBO_U_SCRIPT_GUJARATI, "Gujarati"},
        {0x0A05u, ROBO_U_SCRIPT_GURMUKHI, "Gurmukhi"},
        {0x0B85u, ROBO_U_SCRIPT_TAMIL, "Tamil"},
        {0x0C05u, ROBO_U_SCRIPT_TELUGU, "Telugu"},
        {0x0C85u, ROBO_U_SCRIPT_KANNADA, "Kannada"},
        {0x0D05u, ROBO_U_SCRIPT_MALAYALAM, "Malayalam"},
        {0x0D85u, ROBO_U_SCRIPT_SINHALA, "Sinhala"},
        {0x0E01u, ROBO_U_SCRIPT_THAI, "Thai"},
        {0x0E81u, ROBO_U_SCRIPT_LAO, "Lao"},
        {0x1780u, ROBO_U_SCRIPT_KHMER, "Khmer"},
        {0x1000u, ROBO_U_SCRIPT_MYANMAR, "Myanmar"},
        {0x1200u, ROBO_U_SCRIPT_ETHIOPIC, "Ethiopic"},
        {0xAC00u, ROBO_U_SCRIPT_HANGUL, "Hangul"},
        {0x4E00u, ROBO_U_SCRIPT_HAN, "Han"},
        {0x3042u, ROBO_U_SCRIPT_HIRAGANA, "Hiragana"},
        {0x30A2u, ROBO_U_SCRIPT_KATAKANA, "Katakana"},
        {0x103A0u, ROBO_U_SCRIPT_OLD_PERSIAN, "Old_Persian"},
        {0x1F600u, ROBO_U_SCRIPT_COMMON, "emoji_grin"},
    };
    size_t i;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        robo_unicode_script_t got = robo_unicode_script(samples[i].cp);
        if (got != samples[i].want) {
            robo_test_fail(__FILE__,
                           __LINE__,
                           "Unicode test failure\n"
                           "Category: ScriptSample\n"
                           "Unicode version: %s\n"
                           "Label: %s\n"
                           "Code point: U+%04X\n"
                           "Expected script: %u\n"
                           "Actual script: %u",
                           ROBO_UNICODE_VERSION_STRING,
                           samples[i].label,
                           (unsigned)samples[i].cp,
                           (unsigned)samples[i].want,
                           (unsigned)got);
        }
    }

    ROBO_CHECK_EQ_INT(robo_unicode_script(0x067Eu), ROBO_U_SCRIPT_ARABIC); /* پ */
    ROBO_CHECK(robo_unicode_has_script(0x060Cu, ROBO_U_SCRIPT_ARABIC));
}

static void test_infra_microbench(void)
{
    volatile int sink = 0;
    clock_t t0, t1;
    size_t i;
    const size_t N = 200000u;
    double ms_prop, ms_emoji;
    robo_uchar32_t cps[] = {0x0041u, 0x0627u, 0x0905u, 0x4E00u, 0x1F600u, 0xAC00u};
    robo_uchar32_t grin = 0x1F600u;

    t0 = clock();
    for (i = 0; i < N; i++) {
        sink += robo_unicode_is_emoji(cps[i % 6]);
        sink += (int)robo_unicode_category(cps[i % 6]);
        sink += (int)robo_unicode_script(cps[i % 6]);
    }
    t1 = clock();
    ms_prop = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;

    t0 = clock();
    for (i = 0; i < N; i++) {
        sink += (int)robo_unicode_emoji_sequence_classify(&grin, 1);
    }
    t1 = clock();
    ms_emoji = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;

    printf("infra.microbench property_lookups=%zu time_ms=%.3f sink=%d\n",
           N,
           ms_prop,
           sink);
    printf("infra.microbench emoji_classify=%zu time_ms=%.3f\n", N, ms_emoji);
    ROBO_CHECK(sink != 0);
}

void robo_test_register_unicode_infra(void)
{
    robo_test_register("infra.versions", test_infra_versions);
    robo_test_register("infra.broad_scripts", test_infra_broad_scripts);
    robo_test_register("infra.microbench", test_infra_microbench);
}
