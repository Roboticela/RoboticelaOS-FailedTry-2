/*
 * RoboUnicode case mapping / folding suite (Goal 3.7).
 */

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboTest.h"

#include <string.h>

#include "../Data/robo_case_conformance.inc"

static int Utf8Eq(const char *a, size_t alen, const char *b, size_t blen)
{
    return alen == blen && (alen == 0u || memcmp(a, b, alen) == 0);
}

static void ExpectUtf8(UnicodeCaseForm form,
                        const char *src,
                        size_t srcLen,
                        const char *want,
                        size_t wantLen)
{
    char *out = NULL;
    size_t outLen = 0;
    RoboCheckEqInt(UnicodeCaseMapUtf8(
                          src, srcLen, form, RoboUnicodeStrict, &out, &outLen),
                      RoboOk);
    RoboCheck(Utf8Eq(out, outLen, want, wantLen));
    RoboFree(out);
}

static void TestCaseVersion(void)
{
    RoboCheckStrEq(UnicodeCaseUcdVersionString(), "16.0.0");
}

static void TestCaseAscii(void)
{
    RoboCheckEqInt(UnicodeSimpleTolower('A'), 'a');
    RoboCheckEqInt(UnicodeSimpleToupper('z'), 'Z');
    ExpectUtf8(RoboUnicodeCaseUpper, "Hello", 5, "HELLO", 5);
    ExpectUtf8(RoboUnicodeCaseLower, "Hello", 5, "hello", 5);
    ExpectUtf8(RoboUnicodeCaseFold, "Hello", 5, "hello", 5);
}

static void TestCaseEmptyInvalid(void)
{
    char *out = NULL;
    size_t outLen = 0;
    const char bad[] = {(char)0x80, 'A'};
    int cmp = 99;

    RoboCheckEqInt(UnicodeToUpperUtf8("", 0, RoboUnicodeStrict, &out, &outLen),
                      RoboOk);
    RoboCheck(out != NULL);
    RoboCheckEqInt(outLen, 0);
    RoboFree(out);

    RoboCheckEqInt(UnicodeToLowerUtf8(
                          bad, 2, RoboUnicodeStrict, &out, &outLen),
                      RoboParseError);
    RoboCheckEqInt(UnicodeToLowerUtf8(
                          bad, 2, RoboUnicodeReplace, &out, &outLen),
                      RoboOk);
    RoboCheck(out != NULL);
    RoboFree(out);

    RoboCheckEqInt(UnicodeCasefoldCompareUtf8(
                          "AbC", 3, "aBc", 3, RoboUnicodeStrict, &cmp),
                      RoboOk);
    RoboCheckEqInt(cmp, 0);
}

static void TestCaseLatinSharpS(void)
{
    /* ß → SS upper, ss fold */
    ExpectUtf8(RoboUnicodeCaseUpper, "\xC3\x9F", 2, "SS", 2);
    ExpectUtf8(RoboUnicodeCaseFold, "\xC3\x9F", 2, "ss", 2);
    ExpectUtf8(RoboUnicodeCaseLower, "\xC3\x9F", 2, "\xC3\x9F", 2);
    /* simple upper of ß is itself (no simple mapping to SS) */
    RoboCheckEqInt(UnicodeSimpleToupper(0x00DF), 0x00DF);
}

static void TestCaseLigatures(void)
{
    /* ﬁ → FI upper */
    ExpectUtf8(RoboUnicodeCaseUpper, "\xEF\xAC\x81", 3, "FI", 2);
    ExpectUtf8(RoboUnicodeCaseTitle, "\xEF\xAC\x81", 3, "Fi", 2);
}

static void TestCaseGreekFinalSigma(void)
{
    /* Σ at end → ς ; Σ mid-word → σ */
    /* ΟΣ → ος */
    ExpectUtf8(RoboUnicodeCaseLower, "\xCE\x9F\xCE\xA3", 4, "\xCE\xBF\xCF\x82", 4);
    /* ΟΣΑ → οσα */
    ExpectUtf8(RoboUnicodeCaseLower,
                "\xCE\x9F\xCE\xA3\xCE\x91",
                6,
                "\xCE\xBF\xCF\x83\xCE\xB1",
                6);
}

static void TestCaseScripts(void)
{
    /* Cyrillic Ж → ж */
    ExpectUtf8(RoboUnicodeCaseLower, "\xD0\x96", 2, "\xD0\xB6", 2);
    /* Armenian Ա → ա */
    ExpectUtf8(RoboUnicodeCaseLower, "\xD4\xB1", 2, "\xD5\xA1", 2);
    /* Georgian has caseless Mkhedruli mostly; Asomtavruli Ⴀ → ⴀ if mapped */
    {
        Uchar32 out[4];
        size_t n = UnicodeToLower(0x10A0, out, 4); /* Ⴀ */
        RoboCheck(n >= 1u);
    }
    /* Turkish İ U+0130 full lower → i + combining dot (locale-independent SpecialCasing) */
    {
        Uchar32 out[4];
        size_t n = UnicodeToLower(0x0130, out, 4);
        RoboCheckEqInt(n, 2u);
        RoboCheckEqInt(out[0], 0x0069);
        RoboCheckEqInt(out[1], 0x0307);
    }
    /* Greek Α → α */
    ExpectUtf8(RoboUnicodeCaseLower, "\xCE\x91", 2, "\xCE\xB1", 2);
}

static void TestCaseMultiAndSupp(void)
{
    /* Combining: É NFC vs E+acute — upper of e+acute stays structure via simple per-cp */
    ExpectUtf8(RoboUnicodeCaseUpper, "e\xCC\x81", 3, "E\xCC\x81", 3);
    /* Deseret capital — supplementary plane if mapped */
    {
        Uchar32 out[4];
        size_t n = UnicodeToLower(0x10400, out, 4); /* 𐐀 */
        RoboCheckEqInt(n, 1u);
        RoboCheckEqInt(out[0], 0x10428); /* 𐐨 */
    }
}

static void TestCaseCompare(void)
{
    int cmp = 1;
    /* Maße vs MASSE after fold */
    RoboCheckEqInt(UnicodeCasefoldCompareUtf8("Ma\xC3\x9F" "e",
                                                         5,
                                                         "MASSE",
                                                         5,
                                                         RoboUnicodeStrict,
                                                         &cmp),
                      RoboOk);
    RoboCheckEqInt(cmp, 0);
    RoboCheckEqInt(UnicodeCasefoldCompareUtf8(
                          "abc", 3, "abd", 3, RoboUnicodeStrict, &cmp),
                      RoboOk);
    RoboCheck(cmp < 0);
}

static void TestCaseConformance(void)
{
    size_t i;
    size_t fail = 0;
    const size_t maxReport = 8u;

    RoboCheck(robo_case_conf_cases_count > 0u);
    for (i = 0; i < robo_case_conf_cases_count; i++) {
        const robo_case_conf_case_t *c = &robo_case_conf_cases[i];
        UnicodeCaseForm form;
        char *out = NULL;
        size_t outLen = 0;
        switch (c->kind) {
        case ROBO_CASE_CONF_UPPER:
            form = RoboUnicodeCaseUpper;
            break;
        case ROBO_CASE_CONF_LOWER:
            form = RoboUnicodeCaseLower;
            break;
        case ROBO_CASE_CONF_TITLE:
            form = RoboUnicodeCaseTitle;
            break;
        case ROBO_CASE_CONF_FOLD:
            form = RoboUnicodeCaseFold;
            break;
        case ROBO_CASE_CONF_SIMPLE_UPPER:
            form = RoboUnicodeCaseSimpleUpper;
            break;
        case ROBO_CASE_CONF_SIMPLE_LOWER:
            form = RoboUnicodeCaseSimpleLower;
            break;
        default:
            continue;
        }
        if (UnicodeCaseMapUtf8(c->src,
                                       c->src_len,
                                       form,
                                       RoboUnicodeStrict,
                                       &out,
                                       &outLen) != RoboOk ||
            !Utf8Eq(out, outLen, c->want, c->want_len)) {
            fail++;
            if (fail <= maxReport) {
                TestFail(__FILE__,
                               __LINE__,
                               "case conf %zu kind=%d",
                               i,
                               (int)c->kind);
            }
        }
        RoboFree(out);
    }
    RoboCheckEqInt(fail, 0);
}

void TestRegisterUnicodeCase(void)
{
    TestRegister("case.version", TestCaseVersion);
    TestRegister("case.ascii", TestCaseAscii);
    TestRegister("case.empty_invalid", TestCaseEmptyInvalid);
    TestRegister("case.latin_sharp_s", TestCaseLatinSharpS);
    TestRegister("case.ligatures", TestCaseLigatures);
    TestRegister("case.greek_final_sigma", TestCaseGreekFinalSigma);
    TestRegister("case.scripts", TestCaseScripts);
    TestRegister("case.multi_and_supp", TestCaseMultiAndSupp);
    TestRegister("case.compare", TestCaseCompare);
    TestRegister("case.conformance", TestCaseConformance);
}
