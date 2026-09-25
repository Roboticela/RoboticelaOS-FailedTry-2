/*
 * RoboUnicode Sentence Segmentation suite (Goal 3.9).
 */

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeSentence.h"
#include "RoboTest.h"

#include <string.h>

#include "../Data/robo_sentence_conformance.inc"

static void TestSentenceVersion(void)
{
    RoboCheckStrEq(UnicodeSentenceUcdVersionString(), "16.0.0");
}

static void TestSentenceAsciiLatin(void)
{
    size_t count = 0;
    size_t ss = 0;
    size_t se = 0;
    const char *s = "Hello world. How are you? Fine!";

    RoboCheckEqInt(UnicodeSentenceCountUtf8(
                          s, strlen(s), RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 3u);

    RoboCheckEqInt(UnicodeSentenceNext(
                          s, strlen(s), 0, RoboUnicodeStrict, &ss, &se),
                      RoboOk);
    RoboCheckEqInt(ss, 0u);
    RoboCheckEqInt(se, 13u); /* "Hello world. " includes trailing space */
}

static void TestSentenceNumbersAbbrev(void)
{
    size_t count = 0;
    /* 3.14 stays one sentence (SB6) */
    RoboCheckEqInt(UnicodeSentenceCountUtf8(
                          "Value is 3.14 today.",
                          strlen("Value is 3.14 today."),
                          RoboUnicodeStrict,
                          &count),
                      RoboOk);
    RoboCheckEqInt(count, 1u);

    /* U.S.A. style — SB7 suppresses some breaks */
    {
        Uchar32 cps[] = {'U', '.', 'S', '.', 'A'};
        RoboCheck(!IsUnicodeSentenceBreak(cps, 5, 2)); /* . × S */
        RoboCheck(!IsUnicodeSentenceBreak(cps, 5, 4)); /* . × A */
    }
}

static void TestSentenceQuotesParens(void)
{
    size_t count = 0;
    const char *s = "She said \"Hi.\" Then left.";
    RoboCheckEqInt(UnicodeSentenceCountUtf8(
                          s, strlen(s), RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 2u);

    RoboCheckEqInt(UnicodeSentenceCountUtf8(
                          "(Hello.) Next.", strlen("(Hello.) Next."),
                          RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 2u);
}

static void TestSentenceCrLfPara(void)
{
    Uchar32 cps[] = {'a', '\r', '\n', 'b'};
    /* a × CR (SB998); CR × LF (SB3); LF ÷ b (SB4) */
    RoboCheck(!IsUnicodeSentenceBreak(cps, 4, 1));
    RoboCheck(!IsUnicodeSentenceBreak(cps, 4, 2));
    RoboCheck(IsUnicodeSentenceBreak(cps, 4, 3));

    {
        Uchar32 para[] = {'a', 0x2029u, 'b'}; /* PARAGRAPH SEPARATOR */
        RoboCheck(!IsUnicodeSentenceBreak(para, 3, 1)); /* a × Sep */
        RoboCheck(IsUnicodeSentenceBreak(para, 3, 2));  /* Sep ÷ b */
    }
}

static void TestSentenceScripts(void)
{
    const char *samples[] = {
        "Hola. \xC2\xBFQu\xC3\xA9 tal?",             /* Spanish */
        "Bonjour. Comment allez-vous?",               /* French */
        "Hallo. Wie geht's?",                         /* German */
        "\xCE\x93\xCE\xB5\xCE\xB9\xCE\xAC. "
        "\xCE\xA4\xCE\xB9;",                          /* Greek */
        "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82. "
        "\xD0\x9A\xD0\xB0\xD0\xBA?",                  /* Cyrillic */
        "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7. "
        "\xD9\x83\xD9\x8A\xD9\x81\xD8\x9F",           /* Arabic */
        "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85. "
        "\xDA\x86\xD8\xB7\xD9\x88\xD8\xB1\xD8\x9F",   /* Persian */
        "\xD8\xA7\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85. "
        "\xDA\xA9\xDB\x8C\xD8\xB3\xDB\x92?",          /* Urdu */
        "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D. "
        "\xD7\x9E\xD7\x94?",                          /* Hebrew */
        "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87. "
        "\xE0\xA4\x95\xE0\xA5\x88\xE0\xA4\xB8\xE0\xA5\x87?", /* Devanagari */
        "\xE0\xA6\xA8\xE0\xA6\xAE\xE0\xA6\xB8\xE0\xA7\x8D\xE0\xA6\x95\xE0\xA6\xBE\xE0\xA6\xB0. "
        "\xE0\xA6\x95\xE0\xA7\x87\xE0\xA6\xAE\xE0\xA6\xA8?", /* Bengali */
        "\xE0\xB8\xAA\xE0\xB8\xA7\xE0\xB8\xB1\xE0\xB8\xAA\xE0\xB8\x94\xE0\xB8\xB5. "
        "\xE0\xB8\xAA\xE0\xB8\x9A\xE0\xB8\xB2\xE0\xB8\xA2?", /* Thai */
        "\xE4\xBD\xA0\xE5\xA5\xBD\xE3\x80\x82\xE5\x90\x8D\xE5\xAD\x97\xEF\xBC\x9F", /* CJK */
        "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF\xE3\x80\x82"
        "\xE4\xBD\x95\xE3\x81\xA7\xE3\x81\x99\xE3\x81\x8B\xEF\xBC\x9F", /* Japanese */
        "\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94. "
        "\xEC\x96\xB4\xEB\x96\xBB\xEA\xB2\x8C\xEC\x9A\x94?", /* Korean */
    };
    size_t i;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        size_t count = 0;
        RoboCheckEqInt(UnicodeSentenceCountUtf8(samples[i],
                                                            strlen(samples[i]),
                                                            RoboUnicodeStrict,
                                                            &count),
                          RoboOk);
        RoboCheck(count >= 1u);
    }
}

static void TestSentenceEmojiCombining(void)
{
    size_t count = 0;
    const char *s = "Hi\xF0\x9F\x98\x80! Next.";
    RoboCheckEqInt(UnicodeSentenceCountUtf8(
                          s, strlen(s), RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 2u);

    /* combining acute on e: caf\u0301e. */
    {
        const char *c = "cafe\xCC\x81. Next.";
        RoboCheckEqInt(UnicodeSentenceCountUtf8(
                              c, strlen(c), RoboUnicodeStrict, &count),
                          RoboOk);
        RoboCheckEqInt(count, 2u);
    }
}

static void TestSentenceRepeatedPunct(void)
{
    size_t count = 0;
    RoboCheckEqInt(UnicodeSentenceCountUtf8(
                          "Wow!!! Really?? Yes...",
                          strlen("Wow!!! Really?? Yes..."),
                          RoboUnicodeStrict,
                          &count),
                      RoboOk);
    RoboCheck(count >= 2u);
}

static void TestSentenceInvalidEmpty(void)
{
    size_t count = 0;
    const char bad[] = {(char)0x80, 'A'};

    RoboCheckEqInt(UnicodeSentenceCountUtf8(
                          "", 0, RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 0u);

    RoboCheckEqInt(UnicodeSentenceCountUtf8(
                          "X", 1, RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 1u);

    RoboCheckEqInt(UnicodeSentenceCountUtf8(
                          bad, 2, RoboUnicodeStrict, &count),
                      RoboParseError);
    RoboCheckEqInt(UnicodeSentenceCountUtf8(
                          bad, 2, RoboUnicodeReplace, &count),
                      RoboOk);
    RoboCheck(count >= 1u);
}

static void TestSentencePrevLong(void)
{
    const char *s = "One. Two. Three.";
    size_t ss = 0;
    size_t se = 0;
    size_t count = 0;
    char longbuf[512];
    size_t i;

    RoboCheckEqInt(UnicodeSentencePrev(
                          s, strlen(s), strlen(s), RoboUnicodeStrict, &ss, &se),
                      RoboOk);
    RoboCheck(ss < se);
    RoboCheck(se == strlen(s));

    for (i = 0; i < sizeof(longbuf) - 1u; i++) {
        longbuf[i] = (char)('a' + (i % 26u));
    }
    longbuf[200] = '.';
    longbuf[201] = ' ';
    longbuf[202] = 'B'; /* Upper after Sp — SB11 break (not SB8 Lower suppress) */
    longbuf[sizeof(longbuf) - 1u] = '\0';
    RoboCheckEqInt(UnicodeSentenceCountUtf8(
                          longbuf, strlen(longbuf), RoboUnicodeStrict, &count),
                      RoboOk);
    RoboCheckEqInt(count, 2u);
}

static void TestSentenceConformance(void)
{
    size_t ci;
    size_t fail = 0;
    const size_t maxReport = 12u;

    RoboCheck(robo_sentence_conf_cases_count > 0u);
    for (ci = 0; ci < robo_sentence_conf_cases_count; ci++) {
        const robo_sentence_conf_case_t *c = &robo_sentence_conf_cases[ci];
        size_t i;
        for (i = 0; i < c->cp_count; i++) {
            int got = IsUnicodeSentenceBreak(c->cps, c->cp_count, i);
            int want = c->breaks[i] ? 1 : 0;
            if (got != want) {
                fail++;
                if (fail <= maxReport) {
                    TestFail(__FILE__,
                                   __LINE__,
                                   "SentenceBreakTest case %zu index %zu got=%d want=%d",
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

void TestRegisterUnicodeSentence(void)
{
    TestRegister("sentence.version", TestSentenceVersion);
    TestRegister("sentence.ascii_latin", TestSentenceAsciiLatin);
    TestRegister("sentence.numbers_abbrev", TestSentenceNumbersAbbrev);
    TestRegister("sentence.quotes_parens", TestSentenceQuotesParens);
    TestRegister("sentence.cr_lf_para", TestSentenceCrLfPara);
    TestRegister("sentence.scripts", TestSentenceScripts);
    TestRegister("sentence.emoji_combining", TestSentenceEmojiCombining);
    TestRegister("sentence.repeated_punct", TestSentenceRepeatedPunct);
    TestRegister("sentence.invalid_empty", TestSentenceInvalidEmpty);
    TestRegister("sentence.prev_long", TestSentencePrevLong);
    TestRegister("sentence.conformance", TestSentenceConformance);
}
