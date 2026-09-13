/*
 * RoboUnicode normalization suite (Goal 3.5).
 */

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeNorm.h"
#include "RoboTest.h"

#include <string.h>

#include "../Data/robo_norm_conformance.inc"

static int utf8_eq(const char *a, size_t alen, const char *b, size_t blen)
{
    return alen == blen && (alen == 0u || memcmp(a, b, alen) == 0);
}

static void expect_utf8_form(const char *src,
                             size_t src_len,
                             robo_unicode_norm_form_t form,
                             const char *want,
                             size_t want_len)
{
    char *out = NULL;
    size_t out_len = 0;
    robo_result_t r = robo_unicode_normalize_utf8(
        src, src_len, form, ROBO_UNICODE_STRICT, &out, &out_len);
    ROBO_CHECK_EQ_INT(r, ROBO_OK);
    ROBO_CHECK(utf8_eq(out, out_len, want, want_len));
    robo_free(out);
}

static void test_norm_version(void)
{
    ROBO_CHECK_STR_EQ(robo_unicode_norm_ucd_version_string(), "16.0.0");
}

static void test_norm_empty_ascii(void)
{
    char *out = NULL;
    size_t out_len = 0;
    robo_uchar32_t *cps = NULL;
    size_t cps_len = 0;

    ROBO_CHECK_EQ_INT(
        robo_unicode_normalize_nfc_utf8("", 0, ROBO_UNICODE_STRICT, &out, &out_len),
        ROBO_OK);
    ROBO_CHECK(out != NULL);
    ROBO_CHECK_EQ_INT(out_len, 0);
    ROBO_CHECK_EQ_INT(out[0], 0);
    robo_free(out);

    ROBO_CHECK_EQ_INT(
        robo_unicode_normalize_nfc(NULL, 0, &cps, &cps_len), ROBO_OK);
    ROBO_CHECK(cps == NULL);
    ROBO_CHECK_EQ_INT(cps_len, 0);

    out = NULL;
    ROBO_CHECK_EQ_INT(robo_unicode_normalize_nfc_utf8(
                          "Hello, Roboticela!",
                          strlen("Hello, Roboticela!"),
                          ROBO_UNICODE_STRICT,
                          &out,
                          &out_len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(out, "Hello, Roboticela!");
    robo_free(out);
}

static void test_norm_latin_combining(void)
{
    /* é = U+00E9 NFC; e + U+0301 NFD */
    const char *nfc = "\xC3\xA9";
    const char *nfd = "e\xCC\x81";
    expect_utf8_form(nfc, 2, ROBO_UNICODE_NORM_NFC, nfc, 2);
    expect_utf8_form(nfc, 2, ROBO_UNICODE_NORM_NFD, nfd, 3);
    expect_utf8_form(nfd, 3, ROBO_UNICODE_NORM_NFC, nfc, 2);
    expect_utf8_form(nfd, 3, ROBO_UNICODE_NORM_NFD, nfd, 3);

    /* Multiple marks: a + acute + dot below → order by CCC */
    /* U+0061 U+0301 U+0323 NFD order becomes a + 0323 + 0301 */
    {
        const char *src = "a\xCC\x81\xCC\xA3"; /* ạ́ wrong order */
        const char *ordered = "a\xCC\xA3\xCC\x81";
        expect_utf8_form(src, 5, ROBO_UNICODE_NORM_NFD, ordered, 5);
    }
}

static void test_norm_hangul(void)
{
    /* 가 U+AC00 → ᄀ U+1100 + ᅡ U+1161 */
    const char *syll = "\xEA\xB0\x80";
    const char *jamo = "\xE1\x84\x80\xE1\x85\xA1";
    expect_utf8_form(syll, 3, ROBO_UNICODE_NORM_NFD, jamo, 6);
    expect_utf8_form(jamo, 6, ROBO_UNICODE_NORM_NFC, syll, 3);
    expect_utf8_form(syll, 3, ROBO_UNICODE_NORM_NFC, syll, 3);
}

static void test_norm_compatibility(void)
{
    /* ﬁ U+FB01 → fi under NFKC/NFKD */
    const char *fi_lig = "\xEF\xAC\x81";
    const char *fi = "fi";
    expect_utf8_form(fi_lig, 3, ROBO_UNICODE_NORM_NFC, fi_lig, 3);
    expect_utf8_form(fi_lig, 3, ROBO_UNICODE_NORM_NFD, fi_lig, 3);
    expect_utf8_form(fi_lig, 3, ROBO_UNICODE_NORM_NFKC, fi, 2);
    expect_utf8_form(fi_lig, 3, ROBO_UNICODE_NORM_NFKD, fi, 2);

    /* Fullwidth A U+FF21 → A under NFKC */
    {
        const char *fw = "\xEF\xBC\xA1";
        expect_utf8_form(fw, 3, ROBO_UNICODE_NORM_NFKC, "A", 1);
        expect_utf8_form(fw, 3, ROBO_UNICODE_NORM_NFC, fw, 3);
    }
}

static void test_norm_scripts_samples(void)
{
    /* Arabic ا U+0627 (stable) */
    expect_utf8_form("\xD8\xA7", 2, ROBO_UNICODE_NORM_NFC, "\xD8\xA7", 2);
    /* Persian پ */
    expect_utf8_form("\xD9\xBE", 2, ROBO_UNICODE_NORM_NFC, "\xD9\xBE", 2);
    /* Urdu ھ */
    expect_utf8_form("\xDA\xBE", 2, ROBO_UNICODE_NORM_NFC, "\xDA\xBE", 2);
    /* Hebrew א */
    expect_utf8_form("\xD7\x90", 2, ROBO_UNICODE_NORM_NFC, "\xD7\x90", 2);
    /* Devanagari अ */
    expect_utf8_form("\xE0\xA4\x85", 3, ROBO_UNICODE_NORM_NFC, "\xE0\xA4\x85", 3);
    /* Bengali অ */
    expect_utf8_form("\xE0\xA6\x85", 3, ROBO_UNICODE_NORM_NFC, "\xE0\xA6\x85", 3);
    /* Emoji 😀 U+1F600 — no decomp */
    expect_utf8_form("\xF0\x9F\x98\x80",
                     4,
                     ROBO_UNICODE_NORM_NFC,
                     "\xF0\x9F\x98\x80",
                     4);
    /* Supplementary math 𝐀 U+1D400 → A under NFKC */
    expect_utf8_form("\xF0\x9D\x90\x80", 4, ROBO_UNICODE_NORM_NFKC, "A", 1);
}

static void test_norm_invalid_utf8(void)
{
    char *out = NULL;
    size_t out_len = 0;
    const char bad[] = {(char)0x80, (char)0x41};

    ROBO_CHECK_EQ_INT(robo_unicode_normalize_nfc_utf8(
                          bad, 2, ROBO_UNICODE_STRICT, &out, &out_len),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK(out == NULL);

    ROBO_CHECK_EQ_INT(robo_unicode_normalize_nfc_utf8(
                          bad, 2, ROBO_UNICODE_REPLACE, &out, &out_len),
                      ROBO_OK);
    ROBO_CHECK(out != NULL);
    /* U+FFFD + 'A' */
    ROBO_CHECK(out_len >= 4u);
    ROBO_CHECK_EQ_INT((unsigned char)out[0], 0xEFu);
    ROBO_CHECK_EQ_INT(out[out_len - 1u], 'A');
    robo_free(out);
}

static void test_norm_long_combining(void)
{
    /* a + 60 × combining acute — stays NFD-ordered, no compose to precomposed beyond first */
    char src[1 + 60 * 2];
    size_t i;
    char *out = NULL;
    size_t out_len = 0;
    src[0] = 'a';
    for (i = 0; i < 60u; i++) {
        src[1 + i * 2u] = (char)0xCC;
        src[2 + i * 2u] = (char)0x81;
    }
    ROBO_CHECK_EQ_INT(robo_unicode_normalize_nfd_utf8(
                          src, 1u + 60u * 2u, ROBO_UNICODE_STRICT, &out, &out_len),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(out_len, 1u + 60u * 2u);
    ROBO_CHECK_EQ_INT(out[0], 'a');
    robo_free(out);
}

static void test_norm_conformance(void)
{
    size_t i;
    size_t fail = 0;
    const size_t max_report = 8u;

    ROBO_CHECK(robo_norm_conf_cases_count > 0u);

    for (i = 0; i < robo_norm_conf_cases_count; i++) {
        const robo_norm_conf_case_t *c = &robo_norm_conf_cases[i];
        const struct {
            robo_unicode_norm_form_t form;
            const char *want;
            unsigned want_len;
            const char *label;
        } forms[4] = {
            {ROBO_UNICODE_NORM_NFC, c->nfc, c->nfc_len, "NFC"},
            {ROBO_UNICODE_NORM_NFD, c->nfd, c->nfd_len, "NFD"},
            {ROBO_UNICODE_NORM_NFKC, c->nfkc, c->nfkc_len, "NFKC"},
            {ROBO_UNICODE_NORM_NFKD, c->nfkd, c->nfkd_len, "NFKD"},
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
            if (robo_unicode_normalize_utf8(c->src,
                                            c->src_len,
                                            ROBO_UNICODE_NORM_NFC,
                                            ROBO_UNICODE_STRICT,
                                            &o,
                                            &ol) != ROBO_OK ||
                !utf8_eq(o, ol, c->nfc, c->nfc_len)) {
                fail++;
                if (fail <= max_report) {
                    robo_test_fail(__FILE__,
                                   __LINE__,
                                   "NFC part=%d case=%zu",
                                   c->part,
                                   i);
                }
            }
            robo_free(o);
            o = NULL;
            if (robo_unicode_normalize_utf8(c->src,
                                            c->src_len,
                                            ROBO_UNICODE_NORM_NFD,
                                            ROBO_UNICODE_STRICT,
                                            &o,
                                            &ol) != ROBO_OK ||
                !utf8_eq(o, ol, c->nfd, c->nfd_len)) {
                fail++;
                if (fail <= max_report) {
                    robo_test_fail(__FILE__,
                                   __LINE__,
                                   "NFD part=%d case=%zu",
                                   c->part,
                                   i);
                }
            }
            robo_free(o);
            o = NULL;
            if (robo_unicode_normalize_utf8(c->src,
                                            c->src_len,
                                            ROBO_UNICODE_NORM_NFKC,
                                            ROBO_UNICODE_STRICT,
                                            &o,
                                            &ol) != ROBO_OK ||
                !utf8_eq(o, ol, c->nfkc, c->nfkc_len)) {
                fail++;
                if (fail <= max_report) {
                    robo_test_fail(__FILE__,
                                   __LINE__,
                                   "NFKC part=%d case=%zu",
                                   c->part,
                                   i);
                }
            }
            robo_free(o);
            o = NULL;
            if (robo_unicode_normalize_utf8(c->src,
                                            c->src_len,
                                            ROBO_UNICODE_NORM_NFKD,
                                            ROBO_UNICODE_STRICT,
                                            &o,
                                            &ol) != ROBO_OK ||
                !utf8_eq(o, ol, c->nfkd, c->nfkd_len)) {
                fail++;
                if (fail <= max_report) {
                    robo_test_fail(__FILE__,
                                   __LINE__,
                                   "NFKD part=%d case=%zu",
                                   c->part,
                                   i);
                }
            }
            robo_free(o);
        }

        /* Also: toNFC(c2)==c2, toNFD(c3)==c3, round-trips on expected columns */
        for (f = 0; f < 4u; f++) {
            char *o = NULL;
            size_t ol = 0;
            const char *in = forms[f].want;
            unsigned in_len = forms[f].want_len;
            if (robo_unicode_normalize_utf8(in,
                                            in_len,
                                            forms[f].form,
                                            ROBO_UNICODE_STRICT,
                                            &o,
                                            &ol) != ROBO_OK ||
                !utf8_eq(o, ol, in, in_len)) {
                fail++;
                if (fail <= max_report) {
                    robo_test_fail(__FILE__,
                                   __LINE__,
                                   "idempotent %s part=%d case=%zu",
                                   forms[f].label,
                                   c->part,
                                   i);
                }
            }
            robo_free(o);
        }
    }

    ROBO_CHECK_EQ_INT(fail, 0);
}

void robo_test_register_unicode_norm(void)
{
    robo_test_register("norm.version", test_norm_version);
    robo_test_register("norm.empty_ascii", test_norm_empty_ascii);
    robo_test_register("norm.latin_combining", test_norm_latin_combining);
    robo_test_register("norm.hangul", test_norm_hangul);
    robo_test_register("norm.compatibility", test_norm_compatibility);
    robo_test_register("norm.scripts_samples", test_norm_scripts_samples);
    robo_test_register("norm.invalid_utf8", test_norm_invalid_utf8);
    robo_test_register("norm.long_combining", test_norm_long_combining);
    robo_test_register("norm.conformance", test_norm_conformance);
}
