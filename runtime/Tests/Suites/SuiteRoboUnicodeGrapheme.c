/*
 * RoboUnicode Extended Grapheme Cluster suite (Goal 3.6).
 */

#include "RoboContainers/RoboString.h"
#include "RoboUnicode/RoboUnicodeGrapheme.h"
#include "RoboTest.h"

#include <string.h>

#include "../Data/robo_grapheme_conformance.inc"

static void test_grapheme_version(void)
{
    ROBO_CHECK_STR_EQ(robo_unicode_grapheme_ucd_version_string(), "16.0.0");
}

static void test_grapheme_ascii(void)
{
    const char *s = "Hello";
    size_t count = 0;
    size_t cs = 0;
    size_t ce = 0;
    ROBO_CHECK_EQ_INT(
        robo_unicode_grapheme_count_utf8(s, 5, ROBO_UNICODE_STRICT, &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 5u);
    ROBO_CHECK_EQ_INT(
        robo_unicode_grapheme_next(s, 5, 0, ROBO_UNICODE_STRICT, &cs, &ce), ROBO_OK);
    ROBO_CHECK_EQ_INT(cs, 0u);
    ROBO_CHECK_EQ_INT(ce, 1u);
}

static void test_grapheme_combining(void)
{
    /* e + combining acute = one grapheme */
    const char *nfd = "e\xCC\x81";
    size_t count = 0;
    size_t cs = 0;
    size_t ce = 0;
    ROBO_CHECK_EQ_INT(robo_unicode_grapheme_count_utf8(
                          nfd, 3, ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 1u);
    ROBO_CHECK_EQ_INT(robo_unicode_grapheme_next(
                          nfd, 3, 0, ROBO_UNICODE_STRICT, &cs, &ce),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(ce - cs, 3u);
}

static void test_grapheme_cr_lf(void)
{
    const char crlf[] = {'a', '\r', '\n', 'b'};
    robo_uchar32_t cps[] = {'a', '\r', '\n', 'b'};
    ROBO_CHECK(robo_unicode_grapheme_break(cps, 4, 1));
    ROBO_CHECK(!robo_unicode_grapheme_break(cps, 4, 2));
    ROBO_CHECK(robo_unicode_grapheme_break(cps, 4, 3));
    {
        size_t count = 0;
        ROBO_CHECK_EQ_INT(robo_unicode_grapheme_count_utf8(
                              crlf, 4, ROBO_UNICODE_STRICT, &count),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(count, 3u);
    }
}

static void test_grapheme_hangul(void)
{
    const char *ga = "\xEA\xB0\x80"; /* 가 LV */
    const char *jamo = "\xE1\x84\x80\xE1\x85\xA1";
    size_t c1 = 0;
    size_t c2 = 0;
    ROBO_CHECK_EQ_INT(
        robo_unicode_grapheme_count_utf8(ga, 3, ROBO_UNICODE_STRICT, &c1), ROBO_OK);
    ROBO_CHECK_EQ_INT(c1, 1u);
    ROBO_CHECK_EQ_INT(
        robo_unicode_grapheme_count_utf8(jamo, 6, ROBO_UNICODE_STRICT, &c2), ROBO_OK);
    ROBO_CHECK_EQ_INT(c2, 1u);
}

static void test_grapheme_emoji_zwj(void)
{
    /* 👩‍❤️‍👩 family ZWJ sequence — one grapheme cluster */
    const char *zwj =
        "\xF0\x9F\x91\xA9\xE2\x80\x8D\xE2\x9D\xA4\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\x91\xA9";
    size_t count = 0;
    ROBO_CHECK_EQ_INT(robo_unicode_grapheme_count_utf8(
                          zwj, strlen(zwj), ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 1u);
}

static void test_grapheme_flags(void)
{
    /* 🇺🇸 = two RIs, one cluster */
    const char *flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
    size_t one = 0;
    size_t two = 0;
    const char *both =
        "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
    ROBO_CHECK_EQ_INT(
        robo_unicode_grapheme_count_utf8(flag, 8, ROBO_UNICODE_STRICT, &one), ROBO_OK);
    ROBO_CHECK_EQ_INT(one, 1u);
    ROBO_CHECK_EQ_INT(
        robo_unicode_grapheme_count_utf8(both, 16, ROBO_UNICODE_STRICT, &two), ROBO_OK);
    ROBO_CHECK_EQ_INT(two, 2u);
}

static void test_grapheme_scripts(void)
{
    const char *samples[] = {
        "\xD8\xA7",                   /* Arabic */
        "\xD9\xBE",                   /* Persian */
        "\xDA\xBE",                   /* Urdu */
        "\xD7\x90",                   /* Hebrew */
        "\xE0\xA4\x85",               /* Devanagari */
        "\xE0\xA6\x85",               /* Bengali */
        "\xE0\xB8\x81",               /* Thai */
        "\xE4\xB8\xAD",               /* Chinese */
        "\xE3\x81\x82",               /* Japanese */
        "\xEA\xB0\x80",               /* Korean */
        "\xF0\x9F\x98\x80",           /* emoji */
        "\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD", /* emoji + skin tone */
    };
    size_t i;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        size_t count = 0;
        ROBO_CHECK_EQ_INT(robo_unicode_grapheme_count_utf8(samples[i],
                                                           strlen(samples[i]),
                                                           ROBO_UNICODE_STRICT,
                                                           &count),
                          ROBO_OK);
        ROBO_CHECK(count >= 1u);
    }
}

static void test_grapheme_invalid_utf8(void)
{
    const char bad[] = {(char)0x80, (char)0x41};
    size_t count = 0;
    ROBO_CHECK_EQ_INT(robo_unicode_grapheme_count_utf8(
                          bad, 2, ROBO_UNICODE_STRICT, &count),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(
        robo_unicode_grapheme_count_utf8(bad, 2, ROBO_UNICODE_REPLACE, &count),
        ROBO_OK);
    ROBO_CHECK(count >= 1u);
}

static void test_grapheme_iter_prev(void)
{
    const char *s = "ab";
    size_t cs = 0;
    size_t ce = 0;
    ROBO_CHECK_EQ_INT(
        robo_unicode_grapheme_prev(s, 2, 2, ROBO_UNICODE_STRICT, &cs, &ce), ROBO_OK);
    ROBO_CHECK_EQ_INT(cs, 1u);
    ROBO_CHECK_EQ_INT(ce, 2u);
}

static void test_grapheme_string_integration(void)
{
    robo_string_t str;
    size_t count = 0;
    ROBO_CHECK_EQ_INT(robo_string_init(&str), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_set_cstr(&str, "e\xCC\x81"), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_grapheme_count(&str, ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 1u);
    ROBO_CHECK_EQ_INT(robo_string_size(&str), 3u);
    robo_string_destroy(&str);
}

static void test_grapheme_conformance(void)
{
    size_t ci;
    size_t fail = 0;
    const size_t max_report = 8u;

    ROBO_CHECK(robo_grapheme_conf_cases_count > 0u);

    for (ci = 0; ci < robo_grapheme_conf_cases_count; ci++) {
        const robo_grapheme_conf_case_t *c = &robo_grapheme_conf_cases[ci];
        size_t i;
        for (i = 0; i < c->cp_count; i++) {
            int got = robo_unicode_grapheme_break(c->cps, c->cp_count, i);
            int want = c->breaks[i] ? 1 : 0;
            if (got != want) {
                fail++;
                if (fail <= max_report) {
                    robo_test_fail(__FILE__,
                                   __LINE__,
                                   "GraphemeBreakTest case %zu index %zu got=%d want=%d",
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

void robo_test_register_unicode_grapheme(void)
{
    robo_test_register("grapheme.version", test_grapheme_version);
    robo_test_register("grapheme.ascii", test_grapheme_ascii);
    robo_test_register("grapheme.combining", test_grapheme_combining);
    robo_test_register("grapheme.cr_lf", test_grapheme_cr_lf);
    robo_test_register("grapheme.hangul", test_grapheme_hangul);
    robo_test_register("grapheme.emoji_zwj", test_grapheme_emoji_zwj);
    robo_test_register("grapheme.flags", test_grapheme_flags);
    robo_test_register("grapheme.scripts", test_grapheme_scripts);
    robo_test_register("grapheme.invalid_utf8", test_grapheme_invalid_utf8);
    robo_test_register("grapheme.iter_prev", test_grapheme_iter_prev);
    robo_test_register("grapheme.string_integration", test_grapheme_string_integration);
    robo_test_register("grapheme.conformance", test_grapheme_conformance);
}
