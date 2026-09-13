/*
 * RoboUnicode Word Segmentation suite (Goal 3.8).
 */

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeWord.h"
#include "RoboTest.h"

#include <string.h>

#include "../Data/robo_word_conformance.inc"

static void test_word_version(void)
{
    ROBO_CHECK_STR_EQ(robo_unicode_word_ucd_version_string(), "16.0.0");
}

static void test_word_ascii_latin(void)
{
    size_t count = 0;
    size_t ws = 0;
    size_t we = 0;
    const char *s = "hello world";

    ROBO_CHECK_EQ_INT(
        robo_unicode_word_count_utf8(s, strlen(s), ROBO_UNICODE_STRICT, &count),
        ROBO_OK);
    /* hello | space | world → 3 segments */
    ROBO_CHECK_EQ_INT(count, 3u);

    ROBO_CHECK_EQ_INT(
        robo_unicode_word_next(s, strlen(s), 0, ROBO_UNICODE_STRICT, &ws, &we),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(ws, 0u);
    ROBO_CHECK_EQ_INT(we, 5u); /* "hello" */

    /* don't — apostrophe MidNumLet keeps letters together */
    count = 0;
    ROBO_CHECK_EQ_INT(robo_unicode_word_count_utf8(
                          "don't", 5, ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 1u);
}

static void test_word_numbers_punct(void)
{
    size_t count = 0;
    ROBO_CHECK_EQ_INT(robo_unicode_word_count_utf8(
                          "3.14", 4, ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 1u);

    ROBO_CHECK_EQ_INT(robo_unicode_word_count_utf8(
                          "A3", 2, ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 1u);
}

static void test_word_cr_lf(void)
{
    robo_uchar32_t cps[] = {'a', '\r', '\n', 'b'};
    ROBO_CHECK(robo_unicode_word_break(cps, 4, 1));  /* a ÷ CR */
    ROBO_CHECK(!robo_unicode_word_break(cps, 4, 2)); /* CR × LF */
    ROBO_CHECK(robo_unicode_word_break(cps, 4, 3));  /* LF ÷ b */
}

static void test_word_scripts(void)
{
    const char *samples[] = {
        "\xCE\xB1\xCE\xB2",                   /* Greek αβ */
        "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2",   /* Cyrillic прив */
        "\xD8\xA7\xD9\x84",                   /* Arabic ال */
        "\xD9\xBE\xD8\xA7",                   /* Persian پا */
        "\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88",   /* Urdu اردو */
        "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D",   /* Hebrew שלום */
        "\xE0\xA4\xA8\xE0\xA4\xAE",           /* Devanagari नम */
        "\xE0\xA6\xA8\xE0\xA6\xAE",           /* Bengali নম */
        "\xE0\xB8\xAA\xE0\xB8\xA7",           /* Thai สว */
        "\xE4\xB8\xAD\xE6\x96\x87",           /* Chinese 中文 — each ideograph often separate */
        "\xE3\x81\x82\xE3\x81\x84",           /* Hiragana あい */
        "\xE3\x82\xAB\xE3\x82\xBF",           /* Katakana カタ */
        "\xED\x95\x9C\xEA\xB8\x80",           /* Hangul 한글 */
    };
    size_t i;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        size_t count = 0;
        ROBO_CHECK_EQ_INT(robo_unicode_word_count_utf8(samples[i],
                                                        strlen(samples[i]),
                                                        ROBO_UNICODE_STRICT,
                                                        &count),
                          ROBO_OK);
        ROBO_CHECK(count >= 1u);
    }
}

static void test_word_cjk_no_space(void)
{
    /* Chinese without spaces — Unicode WB typically breaks between ideographs */
    size_t count = 0;
    const char *zh = "\xE4\xBD\xA0\xE5\xA5\xBD"; /* 你好 */
    ROBO_CHECK_EQ_INT(
        robo_unicode_word_count_utf8(zh, 6, ROBO_UNICODE_STRICT, &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 2u);
}

static void test_word_emoji_flags(void)
{
    size_t count = 0;
    /* 😀 alone */
    ROBO_CHECK_EQ_INT(robo_unicode_word_count_utf8(
                          "\xF0\x9F\x98\x80", 4, ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 1u);

    /* 🇺🇸 flag — two RI, one word segment for the pair? RI×RI then break after */
    {
        const char *flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
        ROBO_CHECK_EQ_INT(
            robo_unicode_word_count_utf8(flag, 8, ROBO_UNICODE_STRICT, &count),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(count, 1u);
    }

    /* hello😀world — letters / emoji / letters */
    {
        const char *s = "hi\xF0\x9F\x98\x80" "ok";
        ROBO_CHECK_EQ_INT(
            robo_unicode_word_count_utf8(s, strlen(s), ROBO_UNICODE_STRICT, &count),
            ROBO_OK);
        ROBO_CHECK(count >= 3u);
    }
}

static void test_word_katakana(void)
{
    size_t count = 0;
    /* カタカナ stays together (WB13) */
    const char *kata = "\xE3\x82\xAB\xE3\x82\xBF\xE3\x82\xAB\xE3\x83\x8A";
    ROBO_CHECK_EQ_INT(robo_unicode_word_count_utf8(
                          kata, strlen(kata), ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 1u);
}

static void test_word_invalid_empty(void)
{
    size_t count = 0;
    const char bad[] = {(char)0x80, 'A'};
    char *dummy;
    (void)dummy;

    ROBO_CHECK_EQ_INT(
        robo_unicode_word_count_utf8("", 0, ROBO_UNICODE_STRICT, &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 0u);

    ROBO_CHECK_EQ_INT(robo_unicode_word_count_utf8(
                          bad, 2, ROBO_UNICODE_STRICT, &count),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_unicode_word_count_utf8(
                          bad, 2, ROBO_UNICODE_REPLACE, &count),
                      ROBO_OK);
    ROBO_CHECK(count >= 1u);
}

static void test_word_prev(void)
{
    const char *s = "ab cd";
    size_t ws = 0;
    size_t we = 0;
    ROBO_CHECK_EQ_INT(
        robo_unicode_word_prev(s, 5, 5, ROBO_UNICODE_STRICT, &ws, &we), ROBO_OK);
    ROBO_CHECK_EQ_INT(ws, 3u);
    ROBO_CHECK_EQ_INT(we, 5u);
}

static void test_word_conformance(void)
{
    size_t ci;
    size_t fail = 0;
    const size_t max_report = 8u;

    ROBO_CHECK(robo_word_conf_cases_count > 0u);
    for (ci = 0; ci < robo_word_conf_cases_count; ci++) {
        const robo_word_conf_case_t *c = &robo_word_conf_cases[ci];
        size_t i;
        for (i = 0; i < c->cp_count; i++) {
            int got = robo_unicode_word_break(c->cps, c->cp_count, i);
            int want = c->breaks[i] ? 1 : 0;
            if (got != want) {
                fail++;
                if (fail <= max_report) {
                    robo_test_fail(__FILE__,
                                   __LINE__,
                                   "WordBreakTest case %zu index %zu got=%d want=%d",
                                   ci,
                                   i,
                                   got,
                                   want);
                }
            }
        }
    }
    ROBO_CHECK_EQ_INT(fail, 0);
}

void robo_test_register_unicode_word(void)
{
    robo_test_register("word.version", test_word_version);
    robo_test_register("word.ascii_latin", test_word_ascii_latin);
    robo_test_register("word.numbers_punct", test_word_numbers_punct);
    robo_test_register("word.cr_lf", test_word_cr_lf);
    robo_test_register("word.scripts", test_word_scripts);
    robo_test_register("word.cjk_no_space", test_word_cjk_no_space);
    robo_test_register("word.emoji_flags", test_word_emoji_flags);
    robo_test_register("word.katakana", test_word_katakana);
    robo_test_register("word.invalid_empty", test_word_invalid_empty);
    robo_test_register("word.prev", test_word_prev);
    robo_test_register("word.conformance", test_word_conformance);
}
