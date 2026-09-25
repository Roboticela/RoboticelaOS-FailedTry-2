/*
 * RoboUnicode Extended Grapheme Cluster suite (Goal 3.6).
 */

#include "RoboContainers/RoboString.h"
#include "RoboUnicode/RoboUnicodeGrapheme.h"
#include "RoboTest.h"

#include <string.h>

#include "../Data/robo_grapheme_conformance.inc"

static void TestGraphemeVersion(void)
{
    RoboCheckStrEq(UnicodeGraphemeUcdVersionString(), "16.0.0");
}

static void TestGraphemeAscii(void)
{
    const char *s = "Hello";
    size_t count = 0;
    size_t cs = 0;
    size_t ce = 0;
    RoboCheckEqInt(
        UnicodeGraphemeCountUtf8(s, 5, RoboUnicodeStrict, &count), RoboOk);
    RoboCheckEqInt(count, 5u);
    RoboCheckEqInt(
        UnicodeGraphemeNext(s, 5, 0, RoboUnicodeStrict, &cs, &ce), RoboOk);
    RoboCheckEqInt(cs, 0u);
    RoboCheckEqInt(ce, 1u);
}

static void TestGraphemeCombining(void)
{
    /* e + combining acute = one grapheme */
    const char *nfd = "e\xCC\x81";
    size_t count = 0;
    size_t cs = 0;
    size_t ce = 0;
    RoboCheckEqInt(UnicodeGraphemeCountUtf8(
                          nfd, 3, RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 1u);
    RoboCheckEqInt(UnicodeGraphemeNext(
                          nfd, 3, 0, RoboUnicodeStrict, &cs, &ce),
                      RoboOk);
    RoboCheckEqInt(ce - cs, 3u);
}

static void TestGraphemeCrLf(void)
{
    const char crlf[] = {'a', '\r', '\n', 'b'};
    Uchar32 cps[] = {'a', '\r', '\n', 'b'};
    RoboCheck(IsUnicodeGraphemeBreak(cps, 4, 1));
    RoboCheck(!IsUnicodeGraphemeBreak(cps, 4, 2));
    RoboCheck(IsUnicodeGraphemeBreak(cps, 4, 3));
    {
        size_t count = 0;
        RoboCheckEqInt(UnicodeGraphemeCountUtf8(
                              crlf, 4, RoboUnicodeStrict, &count),
                          RoboOk);
        RoboCheckEqInt(count, 3u);
    }
}

static void TestGraphemeHangul(void)
{
    const char *ga = "\xEA\xB0\x80"; /* 가 LV */
    const char *jamo = "\xE1\x84\x80\xE1\x85\xA1";
    size_t c1 = 0;
    size_t c2 = 0;
    RoboCheckEqInt(
        UnicodeGraphemeCountUtf8(ga, 3, RoboUnicodeStrict, &c1), RoboOk);
    RoboCheckEqInt(c1, 1u);
    RoboCheckEqInt(
        UnicodeGraphemeCountUtf8(jamo, 6, RoboUnicodeStrict, &c2), RoboOk);
    RoboCheckEqInt(c2, 1u);
}

static void TestGraphemeEmojiZwj(void)
{
    /* 👩‍❤️‍👩 family ZWJ sequence — one grapheme cluster */
    const char *zwj =
        "\xF0\x9F\x91\xA9\xE2\x80\x8D\xE2\x9D\xA4\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\x91\xA9";
    size_t count = 0;
    RoboCheckEqInt(UnicodeGraphemeCountUtf8(
                          zwj, strlen(zwj), RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 1u);
}

static void TestGraphemeFlags(void)
{
    /* 🇺🇸 = two RIs, one cluster */
    const char *flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
    size_t one = 0;
    size_t two = 0;
    const char *both =
        "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
    RoboCheckEqInt(
        UnicodeGraphemeCountUtf8(flag, 8, RoboUnicodeStrict, &one), RoboOk);
    RoboCheckEqInt(one, 1u);
    RoboCheckEqInt(
        UnicodeGraphemeCountUtf8(both, 16, RoboUnicodeStrict, &two), RoboOk);
    RoboCheckEqInt(two, 2u);
}

static void TestGraphemeScripts(void)
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
        RoboCheckEqInt(UnicodeGraphemeCountUtf8(samples[i],
                                                           strlen(samples[i]),
                                                           RoboUnicodeStrict,
                                                           &count),
                          RoboOk);
        RoboCheck(count >= 1u);
    }
}

static void TestGraphemeInvalidUtf8(void)
{
    const char bad[] = {(char)0x80, (char)0x41};
    size_t count = 0;
    RoboCheckEqInt(UnicodeGraphemeCountUtf8(
                          bad, 2, RoboUnicodeStrict, &count),
                      RoboParseError);
    RoboCheckEqInt(
        UnicodeGraphemeCountUtf8(bad, 2, RoboUnicodeReplace, &count),
        RoboOk);
    RoboCheck(count >= 1u);
}

static void TestGraphemeIterPrev(void)
{
    const char *s = "ab";
    size_t cs = 0;
    size_t ce = 0;
    RoboCheckEqInt(
        UnicodeGraphemePrev(s, 2, 2, RoboUnicodeStrict, &cs, &ce), RoboOk);
    RoboCheckEqInt(cs, 1u);
    RoboCheckEqInt(ce, 2u);
}

static void TestGraphemeStringIntegration(void)
{
    String str;
    size_t count = 0;
    RoboCheckEqInt(StringInit(&str), RoboOk);
    RoboCheckEqInt(StringSetCstr(&str, "e\xCC\x81"), RoboOk);
    RoboCheckEqInt(StringGraphemeCount(&str, RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 1u);
    RoboCheckEqInt(RoboStringSize(&str), 3u);
    StringDestroy(&str);
}

static void TestGraphemeConformance(void)
{
    size_t ci;
    size_t fail = 0;
    const size_t maxReport = 8u;

    RoboCheck(robo_grapheme_conf_cases_count > 0u);

    for (ci = 0; ci < robo_grapheme_conf_cases_count; ci++) {
        const robo_grapheme_conf_case_t *c = &robo_grapheme_conf_cases[ci];
        size_t i;
        for (i = 0; i < c->cp_count; i++) {
            int got = IsUnicodeGraphemeBreak(c->cps, c->cp_count, i);
            int want = c->breaks[i] ? 1 : 0;
            if (got != want) {
                fail++;
                if (fail <= maxReport) {
                    TestFail(__FILE__,
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
    RoboCheckEqInt(fail, 0);
}

void TestRegisterUnicodeGrapheme(void)
{
    TestRegister("grapheme.version", TestGraphemeVersion);
    TestRegister("grapheme.ascii", TestGraphemeAscii);
    TestRegister("grapheme.combining", TestGraphemeCombining);
    TestRegister("grapheme.cr_lf", TestGraphemeCrLf);
    TestRegister("grapheme.hangul", TestGraphemeHangul);
    TestRegister("grapheme.emoji_zwj", TestGraphemeEmojiZwj);
    TestRegister("grapheme.flags", TestGraphemeFlags);
    TestRegister("grapheme.scripts", TestGraphemeScripts);
    TestRegister("grapheme.invalid_utf8", TestGraphemeInvalidUtf8);
    TestRegister("grapheme.iter_prev", TestGraphemeIterPrev);
    TestRegister("grapheme.string_integration", TestGraphemeStringIntegration);
    TestRegister("grapheme.conformance", TestGraphemeConformance);
}
