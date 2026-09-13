/*
 * RoboUnicode Line Breaking suite (Goal 3.10).
 */

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeLine.h"
#include "RoboTest.h"

#include <string.h>

#include "../Data/robo_line_conformance.inc"

static void test_line_version(void)
{
    ROBO_CHECK_STR_EQ(robo_unicode_line_ucd_version_string(), "16.0.0");
}

static void test_line_ascii_spaces(void)
{
    size_t count = 0;
    size_t ls = 0;
    size_t le = 0;
    robo_unicode_line_break_op_t op = ROBO_LINE_BREAK_PROHIBITED;
    const char *s = "hello world";
    robo_uchar32_t cps[] = {'h', 'e', 'l', 'l', 'o', ' ', 'w'};

    /* Space is an allowed break opportunity (LB18) — break before 'w' after SP */
    ROBO_CHECK_EQ_INT(robo_unicode_line_break_opportunity(cps, 7, 6),
                      ROBO_LINE_BREAK_ALLOWED);

    ROBO_CHECK_EQ_INT(robo_unicode_line_break_count_utf8(
                          s, strlen(s), ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK(count >= 2u);

    ROBO_CHECK_EQ_INT(robo_unicode_line_break_next(
                          s, strlen(s), 0, ROBO_UNICODE_STRICT, &ls, &le, &op),
                      ROBO_OK);
    ROBO_CHECK(ls == 0u);
    ROBO_CHECK(le > ls);
}

static void test_line_cr_lf(void)
{
    robo_uchar32_t cps[] = {'a', '\r', '\n', 'b'};
    /* a × CR? actually after letter before CR — often allowed (LB31)
       CR × LF (LB5), LF ! b (mandatory) */
    ROBO_CHECK_EQ_INT(robo_unicode_line_break_opportunity(cps, 4, 2),
                      ROBO_LINE_BREAK_PROHIBITED); /* CR × LF */
    ROBO_CHECK_EQ_INT(robo_unicode_line_break_opportunity(cps, 4, 3),
                      ROBO_LINE_BREAK_MANDATORY); /* after LF */

    {
        robo_uchar32_t cr[] = {'a', '\r', 'b'};
        ROBO_CHECK_EQ_INT(robo_unicode_line_break_opportunity(cr, 3, 2),
                          ROBO_LINE_BREAK_MANDATORY);
    }
    {
        robo_uchar32_t lf[] = {'a', '\n', 'b'};
        ROBO_CHECK_EQ_INT(robo_unicode_line_break_opportunity(lf, 3, 2),
                          ROBO_LINE_BREAK_MANDATORY);
    }
    {
        robo_uchar32_t para[] = {'a', 0x2029u, 'b'};
        ROBO_CHECK_EQ_INT(robo_unicode_line_break_opportunity(para, 3, 2),
                          ROBO_LINE_BREAK_MANDATORY);
    }
}

static void test_line_hyphen_numbers(void)
{
    robo_uchar32_t num[] = {'1', '2', '.', '3', '4'};
    size_t i;
    /* Digits / decimal stay together under LB25 */
    for (i = 1; i < 5; i++) {
        ROBO_CHECK(!robo_unicode_line_break(num, 5, i) || i == 0);
    }
    ROBO_CHECK(!robo_unicode_line_break(num, 5, 1));
    ROBO_CHECK(!robo_unicode_line_break(num, 5, 2));
    ROBO_CHECK(!robo_unicode_line_break(num, 5, 3));
    ROBO_CHECK(!robo_unicode_line_break(num, 5, 4));
}

static void test_line_cjk(void)
{
    /* Ideographs generally allow breaks between (LB31 / ID) */
    robo_uchar32_t zh[] = {0x4F60u, 0x597Du}; /* 你好 */
    ROBO_CHECK(robo_unicode_line_break(zh, 2, 1));

    {
        size_t count = 0;
        const char *s = "\xE3\x81\x82\xE3\x81\x84"; /* あい */
        ROBO_CHECK_EQ_INT(robo_unicode_line_break_count_utf8(
                              s, strlen(s), ROBO_UNICODE_STRICT, &count),
                          ROBO_OK);
        ROBO_CHECK(count >= 1u);
    }
}

static void test_line_scripts(void)
{
    const char *samples[] = {
        "hello\tworld",
        "caf\xC3\xA9-au-lait",
        "\xCE\xB1\xCE\xB2 \xCE\xB3",
        "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82",
        "\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9",
        "\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C",
        "\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88",
        "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D",
        "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87",
        "\xE0\xA6\xAC\xE0\xA6\xBE\xE0\xA6\x82\xE0\xA6\xB2\xE0\xA6\xBE",
        "\xE0\xB8\xAA\xE0\xB8\xA7\xE0\xB8\xB1\xE0\xB8\xAA\xE0\xB8\x94\xE0\xB8\xB5",
        "\xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95",
        "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF",
        "\xED\x95\x9C\xEA\xB8\x80",
    };
    size_t i;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        size_t count = 0;
        ROBO_CHECK_EQ_INT(robo_unicode_line_break_count_utf8(samples[i],
                                                              strlen(samples[i]),
                                                              ROBO_UNICODE_STRICT,
                                                              &count),
                          ROBO_OK);
        ROBO_CHECK(count >= 1u);
    }
}

static void test_line_emoji_ri(void)
{
    size_t count = 0;
    /* 😀 */
    ROBO_CHECK_EQ_INT(robo_unicode_line_break_count_utf8(
                          "\xF0\x9F\x98\x80", 4, ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 1u);

    /* ZWJ sequence family — should not break inside under LB8a/LB9 */
    {
        /* 👨‍👩‍👧 simplified check: count succeeds */
        const char *zwj =
            "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
        ROBO_CHECK_EQ_INT(robo_unicode_line_break_count_utf8(
                              zwj, strlen(zwj), ROBO_UNICODE_STRICT, &count),
                          ROBO_OK);
        ROBO_CHECK(count >= 1u);
    }

    /* Flag 🇺🇸 — RI×RI (LB30a) */
    {
        robo_uchar32_t flag[] = {0x1F1FAu, 0x1F1F8u};
        ROBO_CHECK(!robo_unicode_line_break(flag, 2, 1));
    }
}

static void test_line_hangul(void)
{
    /* L V T jamo syllable — LB26 */
    robo_uchar32_t jam[] = {0x1100u, 0x1161u, 0x11A8u}; /* 가 + jongseong */
    ROBO_CHECK(!robo_unicode_line_break(jam, 3, 1));
    ROBO_CHECK(!robo_unicode_line_break(jam, 3, 2));
}

static void test_line_combining_quotes(void)
{
    /* base + combining mark — no break (LB9) */
    robo_uchar32_t cm[] = {'e', 0x0301u};
    ROBO_CHECK(!robo_unicode_line_break(cm, 2, 1));

    {
        size_t count = 0;
        ROBO_CHECK_EQ_INT(robo_unicode_line_break_count_utf8(
                              "(\"hi\")", 6, ROBO_UNICODE_STRICT, &count),
                          ROBO_OK);
        ROBO_CHECK(count >= 1u);
    }
}

static void test_line_invalid_empty(void)
{
    size_t count = 0;
    const char bad[] = {(char)0x80, 'A'};

    ROBO_CHECK_EQ_INT(robo_unicode_line_break_count_utf8(
                          "", 0, ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 0u);

    ROBO_CHECK_EQ_INT(robo_unicode_line_break_count_utf8(
                          "X", 1, ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 1u);

    ROBO_CHECK_EQ_INT(robo_unicode_line_break_count_utf8(
                          bad, 2, ROBO_UNICODE_STRICT, &count),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_unicode_line_break_count_utf8(
                          bad, 2, ROBO_UNICODE_REPLACE, &count),
                      ROBO_OK);
}

static void test_line_prev_long(void)
{
    const char *s = "one two three";
    size_t ls = 0;
    size_t le = 0;
    robo_unicode_line_break_op_t op = ROBO_LINE_BREAK_PROHIBITED;
    char longbuf[400];
    size_t i;
    size_t count = 0;

    ROBO_CHECK_EQ_INT(robo_unicode_line_break_prev(
                          s, strlen(s), strlen(s), ROBO_UNICODE_STRICT, &ls, &le, &op),
                      ROBO_OK);
    ROBO_CHECK(ls < le);

    for (i = 0; i < sizeof(longbuf) - 1u; i++) {
        longbuf[i] = (i % 40u == 39u) ? ' ' : (char)('a' + (i % 26u));
    }
    longbuf[sizeof(longbuf) - 1u] = '\0';
    ROBO_CHECK_EQ_INT(robo_unicode_line_break_count_utf8(
                          longbuf, strlen(longbuf), ROBO_UNICODE_STRICT, &count),
                      ROBO_OK);
    ROBO_CHECK(count >= 2u);
}

static void test_line_sot_eot(void)
{
    robo_uchar32_t cps[] = {'A'};
    ROBO_CHECK_EQ_INT(robo_unicode_line_break_opportunity(cps, 1, 0),
                      ROBO_LINE_BREAK_PROHIBITED);
    ROBO_CHECK_EQ_INT(robo_unicode_line_break_opportunity(cps, 1, 1),
                      ROBO_LINE_BREAK_MANDATORY);
}

static void test_line_conformance(void)
{
    size_t ci;
    size_t fail = 0;
    const size_t max_report = 12u;

    ROBO_CHECK(robo_line_conf_cases_count > 0u);
    for (ci = 0; ci < robo_line_conf_cases_count; ci++) {
        const robo_line_conf_case_t *c = &robo_line_conf_cases[ci];
        size_t i;
        for (i = 0; i < c->cp_count; i++) {
            int got = robo_unicode_line_break(c->cps, c->cp_count, i) ? 1 : 0;
            int want = c->breaks[i] ? 1 : 0;
            if (got != want) {
                fail++;
                if (fail <= max_report) {
                    robo_test_fail(__FILE__,
                                   __LINE__,
                                   "LineBreakTest case %zu index %zu got=%d want=%d",
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

void robo_test_register_unicode_line(void)
{
    robo_test_register("line.version", test_line_version);
    robo_test_register("line.ascii_spaces", test_line_ascii_spaces);
    robo_test_register("line.cr_lf", test_line_cr_lf);
    robo_test_register("line.hyphen_numbers", test_line_hyphen_numbers);
    robo_test_register("line.cjk", test_line_cjk);
    robo_test_register("line.scripts", test_line_scripts);
    robo_test_register("line.emoji_ri", test_line_emoji_ri);
    robo_test_register("line.hangul", test_line_hangul);
    robo_test_register("line.combining_quotes", test_line_combining_quotes);
    robo_test_register("line.invalid_empty", test_line_invalid_empty);
    robo_test_register("line.prev_long", test_line_prev_long);
    robo_test_register("line.sot_eot", test_line_sot_eot);
    robo_test_register("line.conformance", test_line_conformance);
}
