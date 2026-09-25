/*
 * RoboUnicode Line Breaking suite (Goal 3.10).
 */

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeLine.h"
#include "RoboTest.h"

#include <string.h>

#include "../Data/robo_line_conformance.inc"

static void TestLineVersion(void)
{
    RoboCheckStrEq(UnicodeLineUcdVersionString(), "16.0.0");
}

static void TestLineAsciiSpaces(void)
{
    size_t count = 0;
    size_t ls = 0;
    size_t le = 0;
    UnicodeLineBreakOp op = RoboLineBreakProhibited;
    const char *s = "hello world";
    Uchar32 cps[] = {'h', 'e', 'l', 'l', 'o', ' ', 'w'};

    /* Space is an allowed break opportunity (LB18) — break before 'w' after SP */
    RoboCheckEqInt(UnicodeLineBreakOpportunity(cps, 7, 6),
                      RoboLineBreakAllowed);

    RoboCheckEqInt(UnicodeLineBreakCountUtf8(
                          s, strlen(s), RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheck(count >= 2u);

    RoboCheckEqInt(UnicodeLineBreakNext(
                          s, strlen(s), 0, RoboUnicodeStrict, &ls, &le, &op),
                      RoboOk);
    RoboCheck(ls == 0u);
    RoboCheck(le > ls);
}

static void TestLineCrLf(void)
{
    Uchar32 cps[] = {'a', '\r', '\n', 'b'};
    /* a × CR? actually after letter before CR — often allowed (LB31)
       CR × LF (LB5), LF ! b (mandatory) */
    RoboCheckEqInt(UnicodeLineBreakOpportunity(cps, 4, 2),
                      RoboLineBreakProhibited); /* CR × LF */
    RoboCheckEqInt(UnicodeLineBreakOpportunity(cps, 4, 3),
                      RoboLineBreakMandatory); /* after LF */

    {
        Uchar32 cr[] = {'a', '\r', 'b'};
        RoboCheckEqInt(UnicodeLineBreakOpportunity(cr, 3, 2),
                          RoboLineBreakMandatory);
    }
    {
        Uchar32 lf[] = {'a', '\n', 'b'};
        RoboCheckEqInt(UnicodeLineBreakOpportunity(lf, 3, 2),
                          RoboLineBreakMandatory);
    }
    {
        Uchar32 para[] = {'a', 0x2029u, 'b'};
        RoboCheckEqInt(UnicodeLineBreakOpportunity(para, 3, 2),
                          RoboLineBreakMandatory);
    }
}

static void TestLineHyphenNumbers(void)
{
    Uchar32 num[] = {'1', '2', '.', '3', '4'};
    size_t i;
    /* Digits / decimal stay together under LB25 */
    for (i = 1; i < 5; i++) {
        RoboCheck(!UnicodeLineBreak(num, 5, i) || i == 0);
    }
    RoboCheck(!UnicodeLineBreak(num, 5, 1));
    RoboCheck(!UnicodeLineBreak(num, 5, 2));
    RoboCheck(!UnicodeLineBreak(num, 5, 3));
    RoboCheck(!UnicodeLineBreak(num, 5, 4));
}

static void TestLineCjk(void)
{
    /* Ideographs generally allow breaks between (LB31 / ID) */
    Uchar32 zh[] = {0x4F60u, 0x597Du}; /* 你好 */
    RoboCheck(UnicodeLineBreak(zh, 2, 1));

    {
        size_t count = 0;
        const char *s = "\xE3\x81\x82\xE3\x81\x84"; /* あい */
        RoboCheckEqInt(UnicodeLineBreakCountUtf8(
                              s, strlen(s), RoboUnicodeStrict, &count),
                          RoboOk);
        RoboCheck(count >= 1u);
    }
}

static void TestLineScripts(void)
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
        RoboCheckEqInt(UnicodeLineBreakCountUtf8(samples[i],
                                                              strlen(samples[i]),
                                                              RoboUnicodeStrict,
                                                              &count),
                          RoboOk);
        RoboCheck(count >= 1u);
    }
}

static void TestLineEmojiRi(void)
{
    size_t count = 0;
    /* 😀 */
    RoboCheckEqInt(UnicodeLineBreakCountUtf8(
                          "\xF0\x9F\x98\x80", 4, RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 1u);

    /* ZWJ sequence family — should not break inside under LB8a/LB9 */
    {
        /* 👨‍👩‍👧 simplified check: count succeeds */
        const char *zwj =
            "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
        RoboCheckEqInt(UnicodeLineBreakCountUtf8(
                              zwj, strlen(zwj), RoboUnicodeStrict, &count),
                          RoboOk);
        RoboCheck(count >= 1u);
    }

    /* Flag 🇺🇸 — RI×RI (LB30a) */
    {
        Uchar32 flag[] = {0x1F1FAu, 0x1F1F8u};
        RoboCheck(!UnicodeLineBreak(flag, 2, 1));
    }
}

static void TestLineHangul(void)
{
    /* L V T jamo syllable — LB26 */
    Uchar32 jam[] = {0x1100u, 0x1161u, 0x11A8u}; /* 가 + jongseong */
    RoboCheck(!UnicodeLineBreak(jam, 3, 1));
    RoboCheck(!UnicodeLineBreak(jam, 3, 2));
}

static void TestLineCombiningQuotes(void)
{
    /* base + combining mark — no break (LB9) */
    Uchar32 cm[] = {'e', 0x0301u};
    RoboCheck(!UnicodeLineBreak(cm, 2, 1));

    {
        size_t count = 0;
        RoboCheckEqInt(UnicodeLineBreakCountUtf8(
                              "(\"hi\")", 6, RoboUnicodeStrict, &count),
                          RoboOk);
        RoboCheck(count >= 1u);
    }
}

static void TestLineInvalidEmpty(void)
{
    size_t count = 0;
    const char bad[] = {(char)0x80, 'A'};

    RoboCheckEqInt(UnicodeLineBreakCountUtf8(
                          "", 0, RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 0u);

    RoboCheckEqInt(UnicodeLineBreakCountUtf8(
                          "X", 1, RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 1u);

    RoboCheckEqInt(UnicodeLineBreakCountUtf8(
                          bad, 2, RoboUnicodeStrict, &count),
                      RoboParseError);
    RoboCheckEqInt(UnicodeLineBreakCountUtf8(
                          bad, 2, RoboUnicodeReplace, &count),
                      RoboOk);
}

static void TestLinePrevLong(void)
{
    const char *s = "one two three";
    size_t ls = 0;
    size_t le = 0;
    UnicodeLineBreakOp op = RoboLineBreakProhibited;
    char longbuf[400];
    size_t i;
    size_t count = 0;

    RoboCheckEqInt(UnicodeLineBreakPrev(
                          s, strlen(s), strlen(s), RoboUnicodeStrict, &ls, &le, &op),
                      RoboOk);
    RoboCheck(ls < le);

    for (i = 0; i < sizeof(longbuf) - 1u; i++) {
        longbuf[i] = (i % 40u == 39u) ? ' ' : (char)('a' + (i % 26u));
    }
    longbuf[sizeof(longbuf) - 1u] = '\0';
    RoboCheckEqInt(UnicodeLineBreakCountUtf8(
                          longbuf, strlen(longbuf), RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheck(count >= 2u);
}

static void TestLineSotEot(void)
{
    Uchar32 cps[] = {'A'};
    RoboCheckEqInt(UnicodeLineBreakOpportunity(cps, 1, 0),
                      RoboLineBreakProhibited);
    RoboCheckEqInt(UnicodeLineBreakOpportunity(cps, 1, 1),
                      RoboLineBreakMandatory);
}

static void TestLineConformance(void)
{
    size_t ci;
    size_t fail = 0;
    const size_t maxReport = 12u;

    RoboCheck(robo_line_conf_cases_count > 0u);
    for (ci = 0; ci < robo_line_conf_cases_count; ci++) {
        const robo_line_conf_case_t *c = &robo_line_conf_cases[ci];
        size_t i;
        for (i = 0; i < c->cp_count; i++) {
            int got = UnicodeLineBreak(c->cps, c->cp_count, i) ? 1 : 0;
            int want = c->breaks[i] ? 1 : 0;
            if (got != want) {
                fail++;
                if (fail <= maxReport) {
                    TestFail(__FILE__,
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
    RoboCheckEqInt(fail, 0);
}

void TestRegisterUnicodeLine(void)
{
    TestRegister("line.version", TestLineVersion);
    TestRegister("line.ascii_spaces", TestLineAsciiSpaces);
    TestRegister("line.cr_lf", TestLineCrLf);
    TestRegister("line.hyphen_numbers", TestLineHyphenNumbers);
    TestRegister("line.cjk", TestLineCjk);
    TestRegister("line.scripts", TestLineScripts);
    TestRegister("line.emoji_ri", TestLineEmojiRi);
    TestRegister("line.hangul", TestLineHangul);
    TestRegister("line.combining_quotes", TestLineCombiningQuotes);
    TestRegister("line.invalid_empty", TestLineInvalidEmpty);
    TestRegister("line.prev_long", TestLinePrevLong);
    TestRegister("line.sot_eot", TestLineSotEot);
    TestRegister("line.conformance", TestLineConformance);
}
