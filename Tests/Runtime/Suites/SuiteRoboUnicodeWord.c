/*
 * RoboUnicode Word Segmentation suite (Goal 3.8).
 */

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeWord.h"
#include "RoboTest.h"

#include <string.h>

#include "../Data/robo_word_conformance.inc"

static void TestWordVersion(void)
{
    RoboCheckStrEq(UnicodeWordUcdVersionString(), "16.0.0");
}

static void TestWordAsciiLatin(void)
{
    size_t count = 0;
    size_t ws = 0;
    size_t we = 0;
    const char *s = "hello world";

    RoboCheckEqInt(
        UnicodeWordCountUtf8(s, strlen(s), RoboUnicodeStrict, &count),
        RoboOk);
    /* hello | space | world → 3 segments */
    RoboCheckEqInt(count, 3u);

    RoboCheckEqInt(
        UnicodeWordNext(s, strlen(s), 0, RoboUnicodeStrict, &ws, &we),
        RoboOk);
    RoboCheckEqInt(ws, 0u);
    RoboCheckEqInt(we, 5u); /* "hello" */

    /* don't — apostrophe MidNumLet keeps letters together */
    count = 0;
    RoboCheckEqInt(UnicodeWordCountUtf8(
                          "don't", 5, RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 1u);
}

static void TestWordNumbersPunct(void)
{
    size_t count = 0;
    RoboCheckEqInt(UnicodeWordCountUtf8(
                          "3.14", 4, RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 1u);

    RoboCheckEqInt(UnicodeWordCountUtf8(
                          "A3", 2, RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 1u);
}

static void TestWordCrLf(void)
{
    Uchar32 cps[] = {'a', '\r', '\n', 'b'};
    RoboCheck(IsUnicodeWordBreak(cps, 4, 1));  /* a ÷ CR */
    RoboCheck(!IsUnicodeWordBreak(cps, 4, 2)); /* CR × LF */
    RoboCheck(IsUnicodeWordBreak(cps, 4, 3));  /* LF ÷ b */
}

static void TestWordScripts(void)
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
        RoboCheckEqInt(UnicodeWordCountUtf8(samples[i],
                                                        strlen(samples[i]),
                                                        RoboUnicodeStrict,
                                                        &count),
                          RoboOk);
        RoboCheck(count >= 1u);
    }
}

static void TestWordCjkNoSpace(void)
{
    /* Chinese without spaces — Unicode WB typically breaks between ideographs */
    size_t count = 0;
    const char *zh = "\xE4\xBD\xA0\xE5\xA5\xBD"; /* 你好 */
    RoboCheckEqInt(
        UnicodeWordCountUtf8(zh, 6, RoboUnicodeStrict, &count), RoboOk);
    RoboCheckEqInt(count, 2u);
}

static void TestWordEmojiFlags(void)
{
    size_t count = 0;
    /* 😀 alone */
    RoboCheckEqInt(UnicodeWordCountUtf8(
                          "\xF0\x9F\x98\x80", 4, RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 1u);

    /* 🇺🇸 flag — two RI, one word segment for the pair? RI×RI then break after */
    {
        const char *flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
        RoboCheckEqInt(
            UnicodeWordCountUtf8(flag, 8, RoboUnicodeStrict, &count),
            RoboOk);
        RoboCheckEqInt(count, 1u);
    }

    /* hello😀world — letters / emoji / letters */
    {
        const char *s = "hi\xF0\x9F\x98\x80" "ok";
        RoboCheckEqInt(
            UnicodeWordCountUtf8(s, strlen(s), RoboUnicodeStrict, &count),
            RoboOk);
        RoboCheck(count >= 3u);
    }
}

static void TestWordKatakana(void)
{
    size_t count = 0;
    /* カタカナ stays together (WB13) */
    const char *kata = "\xE3\x82\xAB\xE3\x82\xBF\xE3\x82\xAB\xE3\x83\x8A";
    RoboCheckEqInt(UnicodeWordCountUtf8(
                          kata, strlen(kata), RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 1u);
}

static void TestWordInvalidEmpty(void)
{
    size_t count = 0;
    const char bad[] = {(char)0x80, 'A'};
    char *dummy;
    (void)dummy;

    RoboCheckEqInt(
        UnicodeWordCountUtf8("", 0, RoboUnicodeStrict, &count), RoboOk);
    RoboCheckEqInt(count, 0u);

    RoboCheckEqInt(UnicodeWordCountUtf8(
                          bad, 2, RoboUnicodeStrict, &count),
                      RoboParseError);
    RoboCheckEqInt(UnicodeWordCountUtf8(
                          bad, 2, RoboUnicodeReplace, &count),
                      RoboOk);
    RoboCheck(count >= 1u);
}

static void TestWordPrev(void)
{
    const char *s = "ab cd";
    size_t ws = 0;
    size_t we = 0;
    RoboCheckEqInt(
        UnicodeWordPrev(s, 5, 5, RoboUnicodeStrict, &ws, &we), RoboOk);
    RoboCheckEqInt(ws, 3u);
    RoboCheckEqInt(we, 5u);
}

static void TestWordConformance(void)
{
    size_t ci;
    size_t fail = 0;
    const size_t maxReport = 8u;

    RoboCheck(robo_word_conf_cases_count > 0u);
    for (ci = 0; ci < robo_word_conf_cases_count; ci++) {
        const robo_word_conf_case_t *c = &robo_word_conf_cases[ci];
        size_t i;
        for (i = 0; i < c->cp_count; i++) {
            int got = IsUnicodeWordBreak(c->cps, c->cp_count, i);
            int want = c->breaks[i] ? 1 : 0;
            if (got != want) {
                fail++;
                if (fail <= maxReport) {
                    TestFail(__FILE__,
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
    RoboCheckEqInt(fail, 0);
}

void TestRegisterUnicodeWord(void)
{
    TestRegister("word.version", TestWordVersion);
    TestRegister("word.ascii_latin", TestWordAsciiLatin);
    TestRegister("word.numbers_punct", TestWordNumbersPunct);
    TestRegister("word.cr_lf", TestWordCrLf);
    TestRegister("word.scripts", TestWordScripts);
    TestRegister("word.cjk_no_space", TestWordCjkNoSpace);
    TestRegister("word.emoji_flags", TestWordEmojiFlags);
    TestRegister("word.katakana", TestWordKatakana);
    TestRegister("word.invalid_empty", TestWordInvalidEmpty);
    TestRegister("word.prev", TestWordPrev);
    TestRegister("word.conformance", TestWordConformance);
}
