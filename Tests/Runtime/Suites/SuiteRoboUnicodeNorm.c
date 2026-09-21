/*
 * RoboUnicode normalization suite (Goal 3.5).
 */

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeNorm.h"
#include "RoboTest.h"

#include <string.h>

#include "../Data/robo_norm_conformance.inc"

static int Utf8Eq(const char *a, size_t alen, const char *b, size_t blen)
{
    return alen == blen && (alen == 0u || memcmp(a, b, alen) == 0);
}

static void ExpectUtf8Form(const char *src,
                             size_t srcLen,
                             UnicodeNormForm form,
                             const char *want,
                             size_t wantLen)
{
    char *out = NULL;
    size_t outLen = 0;
    Result r = UnicodeNormalizeUtf8(
        src, srcLen, form, RoboUnicodeStrict, &out, &outLen);
    RoboCheckEqInt(r, RoboOk);
    RoboCheck(Utf8Eq(out, outLen, want, wantLen));
    RoboFree(out);
}

static void TestNormVersion(void)
{
    RoboCheckStrEq(UnicodeNormUcdVersionString(), "16.0.0");
}

static void TestNormEmptyAscii(void)
{
    char *out = NULL;
    size_t outLen = 0;
    Uchar32 *cps = NULL;
    size_t cpsLen = 0;

    RoboCheckEqInt(
        UnicodeNormalizeNfcUtf8("", 0, RoboUnicodeStrict, &out, &outLen),
        RoboOk);
    RoboCheck(out != NULL);
    RoboCheckEqInt(outLen, 0);
    RoboCheckEqInt(out[0], 0);
    RoboFree(out);

    RoboCheckEqInt(
        UnicodeNormalizeNfc(NULL, 0, &cps, &cpsLen), RoboOk);
    RoboCheck(cps == NULL);
    RoboCheckEqInt(cpsLen, 0);

    out = NULL;
    RoboCheckEqInt(UnicodeNormalizeNfcUtf8(
                          "Hello, Roboticela!",
                          strlen("Hello, Roboticela!"),
                          RoboUnicodeStrict,
                          &out,
                          &outLen),
                      RoboOk);
    RoboCheckStrEq(out, "Hello, Roboticela!");
    RoboFree(out);
}

static void TestNormLatinCombining(void)
{
    /* é = U+00E9 NFC; e + U+0301 NFD */
    const char *nfc = "\xC3\xA9";
    const char *nfd = "e\xCC\x81";
    ExpectUtf8Form(nfc, 2, RoboUnicodeNormNfc, nfc, 2);
    ExpectUtf8Form(nfc, 2, RoboUnicodeNormNfd, nfd, 3);
    ExpectUtf8Form(nfd, 3, RoboUnicodeNormNfc, nfc, 2);
    ExpectUtf8Form(nfd, 3, RoboUnicodeNormNfd, nfd, 3);

    /* Multiple marks: a + acute + dot below → order by CCC */
    /* U+0061 U+0301 U+0323 NFD order becomes a + 0323 + 0301 */
    {
        const char *src = "a\xCC\x81\xCC\xA3"; /* ạ́ wrong order */
        const char *ordered = "a\xCC\xA3\xCC\x81";
        ExpectUtf8Form(src, 5, RoboUnicodeNormNfd, ordered, 5);
    }
}

static void TestNormHangul(void)
{
    /* 가 U+AC00 → ᄀ U+1100 + ᅡ U+1161 */
    const char *syll = "\xEA\xB0\x80";
    const char *jamo = "\xE1\x84\x80\xE1\x85\xA1";
    ExpectUtf8Form(syll, 3, RoboUnicodeNormNfd, jamo, 6);
    ExpectUtf8Form(jamo, 6, RoboUnicodeNormNfc, syll, 3);
    ExpectUtf8Form(syll, 3, RoboUnicodeNormNfc, syll, 3);
}

static void TestNormCompatibility(void)
{
    /* ﬁ U+FB01 → fi under NFKC/NFKD */
    const char *fiLig = "\xEF\xAC\x81";
    const char *fi = "fi";
    ExpectUtf8Form(fiLig, 3, RoboUnicodeNormNfc, fiLig, 3);
    ExpectUtf8Form(fiLig, 3, RoboUnicodeNormNfd, fiLig, 3);
    ExpectUtf8Form(fiLig, 3, RoboUnicodeNormNfkc, fi, 2);
    ExpectUtf8Form(fiLig, 3, RoboUnicodeNormNfkd, fi, 2);

    /* Fullwidth A U+FF21 → A under NFKC */
    {
        const char *fw = "\xEF\xBC\xA1";
        ExpectUtf8Form(fw, 3, RoboUnicodeNormNfkc, "A", 1);
        ExpectUtf8Form(fw, 3, RoboUnicodeNormNfc, fw, 3);
    }
}

static void TestNormScriptsSamples(void)
{
    /* Arabic ا U+0627 (stable) */
    ExpectUtf8Form("\xD8\xA7", 2, RoboUnicodeNormNfc, "\xD8\xA7", 2);
    /* Persian پ */
    ExpectUtf8Form("\xD9\xBE", 2, RoboUnicodeNormNfc, "\xD9\xBE", 2);
    /* Urdu ھ */
    ExpectUtf8Form("\xDA\xBE", 2, RoboUnicodeNormNfc, "\xDA\xBE", 2);
    /* Hebrew א */
    ExpectUtf8Form("\xD7\x90", 2, RoboUnicodeNormNfc, "\xD7\x90", 2);
    /* Devanagari अ */
    ExpectUtf8Form("\xE0\xA4\x85", 3, RoboUnicodeNormNfc, "\xE0\xA4\x85", 3);
    /* Bengali অ */
    ExpectUtf8Form("\xE0\xA6\x85", 3, RoboUnicodeNormNfc, "\xE0\xA6\x85", 3);
    /* Emoji 😀 U+1F600 — no decomp */
    ExpectUtf8Form("\xF0\x9F\x98\x80",
                     4,
                     RoboUnicodeNormNfc,
                     "\xF0\x9F\x98\x80",
                     4);
    /* Supplementary math 𝐀 U+1D400 → A under NFKC */
    ExpectUtf8Form("\xF0\x9D\x90\x80", 4, RoboUnicodeNormNfkc, "A", 1);
}

static void TestNormInvalidUtf8(void)
{
    char *out = NULL;
    size_t outLen = 0;
    const char bad[] = {(char)0x80, (char)0x41};

    RoboCheckEqInt(UnicodeNormalizeNfcUtf8(
                          bad, 2, RoboUnicodeStrict, &out, &outLen),
                      RoboParseError);
    RoboCheck(out == NULL);

    RoboCheckEqInt(UnicodeNormalizeNfcUtf8(
                          bad, 2, RoboUnicodeReplace, &out, &outLen),
                      RoboOk);
    RoboCheck(out != NULL);
    /* U+FFFD + 'A' */
    RoboCheck(outLen >= 4u);
    RoboCheckEqInt((unsigned char)out[0], 0xEFu);
    RoboCheckEqInt(out[outLen - 1u], 'A');
    RoboFree(out);
}

static void TestNormLongCombining(void)
{
    /* a + 60 × combining acute — stays NFD-ordered, no compose to precomposed beyond first */
    char src[1 + 60 * 2];
    size_t i;
    char *out = NULL;
    size_t outLen = 0;
    src[0] = 'a';
    for (i = 0; i < 60u; i++) {
        src[1 + i * 2u] = (char)0xCC;
        src[2 + i * 2u] = (char)0x81;
    }
    RoboCheckEqInt(UnicodeNormalizeNfdUtf8(
                          src, 1u + 60u * 2u, RoboUnicodeStrict, &out, &outLen),
                      RoboOk);
    RoboCheckEqInt(outLen, 1u + 60u * 2u);
    RoboCheckEqInt(out[0], 'a');
    RoboFree(out);
}

static void TestNormConformance(void)
{
    size_t i;
    size_t fail = 0;
    const size_t maxReport = 8u;

    RoboCheck(NormConfCasesCount > 0u);

    for (i = 0; i < NormConfCasesCount; i++) {
        const NormConfCase *c = &NormConfCases[i];
        const struct {
            UnicodeNormForm form;
            const char *want;
            unsigned wantLen;
            const char *label;
        } forms[4] = {
            {RoboUnicodeNormNfc, c->nfc, c->nfcLen, "NFC"},
            {RoboUnicodeNormNfd, c->nfd, c->nfdLen, "NFD"},
            {RoboUnicodeNormNfkc, c->nfkc, c->nfkcLen, "NFKC"},
            {RoboUnicodeNormNfkd, c->nfkd, c->nfkdLen, "NFKD"},
        };
        size_t f;
        /* UAX #15: for each form X, X == toX(source) == toX(toX(source))
         * and NFC(c1)==c2, NFD(c1)==c3, etc. Also:
         * c2 == toNFC(c1) == toNFC(c2) == toNFC(c3)
         * Official test asserts:
         *   NFC(c1)=c2 NFC(c2)=c2 NFC(c3)=c2 NFC(c4)=c4 NFC(c5)=c4
         *   NFD(c1)=c3 NFD(c2)=c3 NFD(c3)=c3 NFD(c4)=c5 NFD(c5)=c5
         *   NFKC(c1)=c4 ... NFKD(c1)=c5 ...
         */
        (void)forms;
        {
            char *o = NULL;
            size_t ol = 0;
            if (UnicodeNormalizeUtf8(c->src,
                                            c->srcLen,
                                            RoboUnicodeNormNfc,
                                            RoboUnicodeStrict,
                                            &o,
                                            &ol) != RoboOk ||
                !Utf8Eq(o, ol, c->nfc, c->nfcLen)) {
                fail++;
                if (fail <= maxReport) {
                    TestFail(__FILE__,
                                   __LINE__,
                                   "NFC part=%d case=%zu",
                                   c->part,
                                   i);
                }
            }
            RoboFree(o);
            o = NULL;
            if (UnicodeNormalizeUtf8(c->src,
                                            c->srcLen,
                                            RoboUnicodeNormNfd,
                                            RoboUnicodeStrict,
                                            &o,
                                            &ol) != RoboOk ||
                !Utf8Eq(o, ol, c->nfd, c->nfdLen)) {
                fail++;
                if (fail <= maxReport) {
                    TestFail(__FILE__,
                                   __LINE__,
                                   "NFD part=%d case=%zu",
                                   c->part,
                                   i);
                }
            }
            RoboFree(o);
            o = NULL;
            if (UnicodeNormalizeUtf8(c->src,
                                            c->srcLen,
                                            RoboUnicodeNormNfkc,
                                            RoboUnicodeStrict,
                                            &o,
                                            &ol) != RoboOk ||
                !Utf8Eq(o, ol, c->nfkc, c->nfkcLen)) {
                fail++;
                if (fail <= maxReport) {
                    TestFail(__FILE__,
                                   __LINE__,
                                   "NFKC part=%d case=%zu",
                                   c->part,
                                   i);
                }
            }
            RoboFree(o);
            o = NULL;
            if (UnicodeNormalizeUtf8(c->src,
                                            c->srcLen,
                                            RoboUnicodeNormNfkd,
                                            RoboUnicodeStrict,
                                            &o,
                                            &ol) != RoboOk ||
                !Utf8Eq(o, ol, c->nfkd, c->nfkdLen)) {
                fail++;
                if (fail <= maxReport) {
                    TestFail(__FILE__,
                                   __LINE__,
                                   "NFKD part=%d case=%zu",
                                   c->part,
                                   i);
                }
            }
            RoboFree(o);
        }

        /* Also: toNFC(c2)==c2, toNFD(c3)==c3, round-trips on expected columns */
        for (f = 0; f < 4u; f++) {
            char *o = NULL;
            size_t ol = 0;
            const char *in = forms[f].want;
            unsigned inLen = forms[f].wantLen;
            if (UnicodeNormalizeUtf8(in,
                                            inLen,
                                            forms[f].form,
                                            RoboUnicodeStrict,
                                            &o,
                                            &ol) != RoboOk ||
                !Utf8Eq(o, ol, in, inLen)) {
                fail++;
                if (fail <= maxReport) {
                    TestFail(__FILE__,
                                   __LINE__,
                                   "idempotent %s part=%d case=%zu",
                                   forms[f].label,
                                   c->part,
                                   i);
                }
            }
            RoboFree(o);
        }
    }

    RoboCheckEqInt(fail, 0);
}

void TestRegisterUnicodeNorm(void)
{
    TestRegister("norm.version", TestNormVersion);
    TestRegister("norm.empty_ascii", TestNormEmptyAscii);
    TestRegister("norm.latin_combining", TestNormLatinCombining);
    TestRegister("norm.hangul", TestNormHangul);
    TestRegister("norm.compatibility", TestNormCompatibility);
    TestRegister("norm.scripts_samples", TestNormScriptsSamples);
    TestRegister("norm.invalid_utf8", TestNormInvalidUtf8);
    TestRegister("norm.long_combining", TestNormLongCombining);
    TestRegister("norm.conformance", TestNormConformance);
}
