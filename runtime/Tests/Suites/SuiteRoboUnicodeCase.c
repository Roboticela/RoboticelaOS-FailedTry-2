/*
 * RoboUnicode case mapping / folding suite (Goal 3.7).
 */

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboTest.h"

#include <string.h>

#include "../Data/robo_case_conformance.inc"

static int utf8_eq(const char *a, size_t alen, const char *b, size_t blen)
{
    return alen == blen && (alen == 0u || memcmp(a, b, alen) == 0);
}

static void expect_utf8(robo_unicode_case_form_t form,
                        const char *src,
                        size_t src_len,
                        const char *want,
                        size_t want_len)
{
    char *out = NULL;
    size_t out_len = 0;
    ROBO_CHECK_EQ_INT(robo_unicode_case_map_utf8(
                          src, src_len, form, ROBO_UNICODE_STRICT, &out, &out_len),
                      ROBO_OK);
    ROBO_CHECK(utf8_eq(out, out_len, want, want_len));
    robo_free(out);
}

static void test_case_version(void)
{
    ROBO_CHECK_STR_EQ(robo_unicode_case_ucd_version_string(), "16.0.0");
}

static void test_case_ascii(void)
{
    ROBO_CHECK_EQ_INT(robo_unicode_simple_tolower('A'), 'a');
    ROBO_CHECK_EQ_INT(robo_unicode_simple_toupper('z'), 'Z');
    expect_utf8(ROBO_UNICODE_CASE_UPPER, "Hello", 5, "HELLO", 5);
    expect_utf8(ROBO_UNICODE_CASE_LOWER, "Hello", 5, "hello", 5);
    expect_utf8(ROBO_UNICODE_CASE_FOLD, "Hello", 5, "hello", 5);
}

static void test_case_empty_invalid(void)
{
    char *out = NULL;
    size_t out_len = 0;
    const char bad[] = {(char)0x80, 'A'};
    int cmp = 99;

    ROBO_CHECK_EQ_INT(robo_unicode_to_upper_utf8("", 0, ROBO_UNICODE_STRICT, &out, &out_len),
                      ROBO_OK);
    ROBO_CHECK(out != NULL);
    ROBO_CHECK_EQ_INT(out_len, 0);
    robo_free(out);

    ROBO_CHECK_EQ_INT(robo_unicode_to_lower_utf8(
                          bad, 2, ROBO_UNICODE_STRICT, &out, &out_len),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_unicode_to_lower_utf8(
                          bad, 2, ROBO_UNICODE_REPLACE, &out, &out_len),
                      ROBO_OK);
    ROBO_CHECK(out != NULL);
    robo_free(out);

    ROBO_CHECK_EQ_INT(robo_unicode_casefold_compare_utf8(
                          "AbC", 3, "aBc", 3, ROBO_UNICODE_STRICT, &cmp),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(cmp, 0);
}

static void test_case_latin_sharp_s(void)
{
    /* ß → SS upper, ss fold */
    expect_utf8(ROBO_UNICODE_CASE_UPPER, "\xC3\x9F", 2, "SS", 2);
    expect_utf8(ROBO_UNICODE_CASE_FOLD, "\xC3\x9F", 2, "ss", 2);
    expect_utf8(ROBO_UNICODE_CASE_LOWER, "\xC3\x9F", 2, "\xC3\x9F", 2);
    /* simple upper of ß is itself (no simple mapping to SS) */
    ROBO_CHECK_EQ_INT(robo_unicode_simple_toupper(0x00DF), 0x00DF);
}

static void test_case_ligatures(void)
{
    /* ﬁ → FI upper */
    expect_utf8(ROBO_UNICODE_CASE_UPPER, "\xEF\xAC\x81", 3, "FI", 2);
    expect_utf8(ROBO_UNICODE_CASE_TITLE, "\xEF\xAC\x81", 3, "Fi", 2);
}

static void test_case_greek_final_sigma(void)
{
    /* Σ at end → ς ; Σ mid-word → σ */
    /* ΟΣ → ος */
    expect_utf8(ROBO_UNICODE_CASE_LOWER, "\xCE\x9F\xCE\xA3", 4, "\xCE\xBF\xCF\x82", 4);
    /* ΟΣΑ → οσα */
    expect_utf8(ROBO_UNICODE_CASE_LOWER,
                "\xCE\x9F\xCE\xA3\xCE\x91",
                6,
                "\xCE\xBF\xCF\x83\xCE\xB1",
                6);
}

static void test_case_scripts(void)
{
    /* Cyrillic Ж → ж */
    expect_utf8(ROBO_UNICODE_CASE_LOWER, "\xD0\x96", 2, "\xD0\xB6", 2);
    /* Armenian Ա → ա */
    expect_utf8(ROBO_UNICODE_CASE_LOWER, "\xD4\xB1", 2, "\xD5\xA1", 2);
    /* Georgian has caseless Mkhedruli mostly; Asomtavruli Ⴀ → ⴀ if mapped */
    {
        robo_uchar32_t out[4];
        size_t n = robo_unicode_to_lower(0x10A0, out, 4); /* Ⴀ */
        ROBO_CHECK(n >= 1u);
    }
    /* Turkish İ U+0130 full lower → i + combining dot (locale-independent SpecialCasing) */
    {
        robo_uchar32_t out[4];
        size_t n = robo_unicode_to_lower(0x0130, out, 4);
        ROBO_CHECK_EQ_INT(n, 2u);
        ROBO_CHECK_EQ_INT(out[0], 0x0069);
        ROBO_CHECK_EQ_INT(out[1], 0x0307);
    }
    /* Greek Α → α */
    expect_utf8(ROBO_UNICODE_CASE_LOWER, "\xCE\x91", 2, "\xCE\xB1", 2);
}

static void test_case_multi_and_supp(void)
{
    /* Combining: É NFC vs E+acute — upper of e+acute stays structure via simple per-cp */
    expect_utf8(ROBO_UNICODE_CASE_UPPER, "e\xCC\x81", 3, "E\xCC\x81", 3);
    /* Deseret capital — supplementary plane if mapped */
    {
        robo_uchar32_t out[4];
        size_t n = robo_unicode_to_lower(0x10400, out, 4); /* 𐐀 */
        ROBO_CHECK_EQ_INT(n, 1u);
        ROBO_CHECK_EQ_INT(out[0], 0x10428); /* 𐐨 */
    }
}

static void test_case_compare(void)
{
    int cmp = 1;
    /* Maße vs MASSE after fold */
    ROBO_CHECK_EQ_INT(robo_unicode_casefold_compare_utf8("Ma\xC3\x9F" "e",
                                                         5,
                                                         "MASSE",
                                                         5,
                                                         ROBO_UNICODE_STRICT,
                                                         &cmp),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(cmp, 0);
    ROBO_CHECK_EQ_INT(robo_unicode_casefold_compare_utf8(
                          "abc", 3, "abd", 3, ROBO_UNICODE_STRICT, &cmp),
                      ROBO_OK);
    ROBO_CHECK(cmp < 0);
}

static void test_case_conformance(void)
{
    size_t i;
    size_t fail = 0;
    const size_t max_report = 8u;

    ROBO_CHECK(robo_case_conf_cases_count > 0u);
    for (i = 0; i < robo_case_conf_cases_count; i++) {
        const robo_case_conf_case_t *c = &robo_case_conf_cases[i];
        robo_unicode_case_form_t form;
        char *out = NULL;
        size_t out_len = 0;
        switch (c->kind) {
        case ROBO_CASE_CONF_UPPER:
            form = ROBO_UNICODE_CASE_UPPER;
            break;
        case ROBO_CASE_CONF_LOWER:
            form = ROBO_UNICODE_CASE_LOWER;
            break;
        case ROBO_CASE_CONF_TITLE:
            form = ROBO_UNICODE_CASE_TITLE;
            break;
        case ROBO_CASE_CONF_FOLD:
            form = ROBO_UNICODE_CASE_FOLD;
            break;
        case ROBO_CASE_CONF_SIMPLE_UPPER:
            form = ROBO_UNICODE_CASE_SIMPLE_UPPER;
            break;
        case ROBO_CASE_CONF_SIMPLE_LOWER:
            form = ROBO_UNICODE_CASE_SIMPLE_LOWER;
            break;
        default:
            continue;
        }
        if (robo_unicode_case_map_utf8(c->src,
                                       c->src_len,
                                       form,
                                       ROBO_UNICODE_STRICT,
                                       &out,
                                       &out_len) != ROBO_OK ||
            !utf8_eq(out, out_len, c->want, c->want_len)) {
            fail++;
            if (fail <= max_report) {
                robo_test_fail(__FILE__,
                               __LINE__,
                               "case conf %zu kind=%d",
                               i,
                               (int)c->kind);
            }
        }
        robo_free(out);
    }
    ROBO_CHECK_EQ_INT(fail, 0);
}

void robo_test_register_unicode_case(void)
{
    robo_test_register("case.version", test_case_version);
    robo_test_register("case.ascii", test_case_ascii);
    robo_test_register("case.empty_invalid", test_case_empty_invalid);
    robo_test_register("case.latin_sharp_s", test_case_latin_sharp_s);
    robo_test_register("case.ligatures", test_case_ligatures);
    robo_test_register("case.greek_final_sigma", test_case_greek_final_sigma);
    robo_test_register("case.scripts", test_case_scripts);
    robo_test_register("case.multi_and_supp", test_case_multi_and_supp);
    robo_test_register("case.compare", test_case_compare);
    robo_test_register("case.conformance", test_case_conformance);
}
