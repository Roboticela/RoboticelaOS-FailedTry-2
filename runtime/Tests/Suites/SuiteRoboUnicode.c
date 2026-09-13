/*
 * RoboUnicode suite (Goal 3.3) — encoding / validation / conversion.
 */

#include "RoboContainers/RoboString.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboTest.h"

#include <string.h>

static void expect_utf8_roundtrip(robo_uchar32_t cp)
{
    char buf[4];
    size_t n = 0;
    size_t next = 0;
    robo_uchar32_t out = 0;

    ROBO_CHECK_EQ_INT(robo_unicode_utf8_encode(cp, buf, sizeof(buf), &n),
                      ROBO_UNICODE_OK);
    ROBO_CHECK(n >= 1 && n <= 4);
    ROBO_CHECK_EQ_INT(
        robo_unicode_utf8_decode(buf, n, 0, ROBO_UNICODE_STRICT, &out, &next),
        ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(out, (int)cp);
    ROBO_CHECK_EQ_INT(next, (int)n);
}

static void test_unicode_version_and_scalars(void)
{
    ROBO_CHECK_STR_EQ(robo_unicode_version_string(), "16.0.0");
    ROBO_CHECK(robo_unicode_is_scalar(0));
    ROBO_CHECK(robo_unicode_is_scalar(0x10FFFF));
    ROBO_CHECK(!robo_unicode_is_scalar(0x110000));
    ROBO_CHECK(robo_unicode_is_surrogate(0xD800));
    ROBO_CHECK(robo_unicode_is_surrogate(0xDFFF));
    ROBO_CHECK(!robo_unicode_is_scalar(0xD800));
    ROBO_CHECK(!robo_unicode_is_scalar(0xDFFF));
}

static void test_unicode_ascii_latin1(void)
{
    expect_utf8_roundtrip('A');
    expect_utf8_roundtrip(0x00); /* U+0000 */
    expect_utf8_roundtrip(0x7F);
    expect_utf8_roundtrip(0xA0); /* NBSP */
    expect_utf8_roundtrip(0xFF); /* ÿ */
}

static void test_unicode_scripts_and_emoji(void)
{
    /* Arabic: سلام U+0633 U+0644 U+0627 U+0645 */
    expect_utf8_roundtrip(0x0633);
    expect_utf8_roundtrip(0x0644);
    /* Urdu / Persian letters */
    expect_utf8_roundtrip(0x06BE); /* ھ */
    expect_utf8_roundtrip(0x067E); /* پ */
    expect_utf8_roundtrip(0x06AF); /* گ */
    /* Hebrew */
    expect_utf8_roundtrip(0x05D0); /* א */
    /* Devanagari */
    expect_utf8_roundtrip(0x0905); /* अ */
    expect_utf8_roundtrip(0x0939); /* ह */
    /* Bengali */
    expect_utf8_roundtrip(0x0985); /* অ */
    /* Thai */
    expect_utf8_roundtrip(0x0E01); /* ก */
    /* CJK */
    expect_utf8_roundtrip(0x4E2D); /* 中 */
    expect_utf8_roundtrip(0x65E5); /* 日 */
    expect_utf8_roundtrip(0xD55C); /* 한 */
    /* Emoji (supplementary plane) */
    expect_utf8_roundtrip(0x1F600); /* 😀 */
    expect_utf8_roundtrip(0x1F4A9);
    /* Combining mark */
    expect_utf8_roundtrip(0x0301); /* combining acute */
    /* Boundary */
    expect_utf8_roundtrip(0x10000); /* first supplementary */
    expect_utf8_roundtrip(0x10FFFF);
}

static void test_unicode_utf8_invalid(void)
{
    size_t next = 0;
    robo_uchar32_t cp = 0;
    /* Overlong encoding of U+002F as C0 AF */
    const char overlong[] = {(char)0xC0, (char)0xAF};
    /* Surrogate U+D800 as ED A0 80 */
    const char sur[] = {(char)0xED, (char)0xA0, (char)0x80};
    /* Truncated */
    const char trunc[] = {(char)0xE2, (char)0x82};
    /* Lone continuation */
    const char cont[] = {(char)0x80};
    /* Above U+10FFFF: F4 90 80 80 */
    const char too_big[] = {(char)0xF4, (char)0x90, (char)0x80, (char)0x80};

    ROBO_CHECK_EQ_INT(robo_unicode_utf8_decode(overlong, 2, 0, ROBO_UNICODE_STRICT,
                                               &cp, &next),
                      ROBO_UNICODE_INVALID);
    ROBO_CHECK_EQ_INT(
        robo_unicode_utf8_decode(sur, 3, 0, ROBO_UNICODE_STRICT, &cp, &next),
        ROBO_UNICODE_INVALID);
    ROBO_CHECK_EQ_INT(
        robo_unicode_utf8_decode(trunc, 2, 0, ROBO_UNICODE_STRICT, &cp, &next),
        ROBO_UNICODE_TRUNCATED);
    ROBO_CHECK_EQ_INT(
        robo_unicode_utf8_decode(cont, 1, 0, ROBO_UNICODE_STRICT, &cp, &next),
        ROBO_UNICODE_INVALID);
    ROBO_CHECK_EQ_INT(
        robo_unicode_utf8_decode(too_big, 4, 0, ROBO_UNICODE_STRICT, &cp, &next),
        ROBO_UNICODE_INVALID);

    ROBO_CHECK(!robo_unicode_utf8_validate(overlong, 2));
    ROBO_CHECK(!robo_unicode_utf8_validate(sur, 3));

    /* REPLACE consumes one byte */
    ROBO_CHECK_EQ_INT(robo_unicode_utf8_decode(cont, 1, 0, ROBO_UNICODE_REPLACE,
                                               &cp, &next),
                      ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(cp, (int)ROBO_UCHAR32_REPLACEMENT);
    ROBO_CHECK_EQ_INT(next, 1);
}

static void test_unicode_utf8_next_prev(void)
{
    /* "A中" = 41 E4 B8 AD */
    const char *s = "A\xE4\xB8\xAD";
    size_t len = 4;
    size_t next = 0;
    size_t start = 0;
    robo_uchar32_t cp = 0;

    ROBO_CHECK_EQ_INT(
        robo_unicode_utf8_next(s, len, 0, ROBO_UNICODE_STRICT, &cp, &next),
        ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(cp, 'A');
    ROBO_CHECK_EQ_INT(next, 1);

    ROBO_CHECK_EQ_INT(
        robo_unicode_utf8_next(s, len, 1, ROBO_UNICODE_STRICT, &cp, &next),
        ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(cp, 0x4E2D);
    ROBO_CHECK_EQ_INT(next, 4);

    ROBO_CHECK_EQ_INT(
        robo_unicode_utf8_prev(s, len, 4, ROBO_UNICODE_STRICT, &cp, &start),
        ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(cp, 0x4E2D);
    ROBO_CHECK_EQ_INT(start, 1);

    ROBO_CHECK_EQ_INT(
        robo_unicode_utf8_prev(s, len, 1, ROBO_UNICODE_STRICT, &cp, &start),
        ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(cp, 'A');
    ROBO_CHECK_EQ_INT(start, 0);
}

static void test_unicode_utf16_surrogates(void)
{
    robo_uchar16_t pair[2];
    robo_uchar16_t lone_low[] = {0xDC00};
    robo_uchar16_t lone_hi[] = {0xD800};
    size_t n = 0;
    size_t next = 0;
    robo_uchar32_t cp = 0;

    ROBO_CHECK_EQ_INT(robo_unicode_utf16_encode(0x1F600, pair, 2, &n),
                      ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(n, 2);
    ROBO_CHECK_EQ_INT(
        robo_unicode_utf16_decode(pair, 2, 0, ROBO_UNICODE_STRICT, &cp, &next),
        ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(cp, 0x1F600);
    ROBO_CHECK_EQ_INT(next, 2);

    ROBO_CHECK_EQ_INT(
        robo_unicode_utf16_decode(lone_low, 1, 0, ROBO_UNICODE_STRICT, &cp, &next),
        ROBO_UNICODE_INVALID);
    ROBO_CHECK_EQ_INT(
        robo_unicode_utf16_decode(lone_hi, 1, 0, ROBO_UNICODE_STRICT, &cp, &next),
        ROBO_UNICODE_TRUNCATED);
    ROBO_CHECK(!robo_unicode_utf16_validate(lone_low, 1));
}

static void test_unicode_conversions(void)
{
    /* سلام */
    const char *ar = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    size_t ar_len = 8;
    robo_uchar16_t u16[16];
    robo_uchar32_t u32[16];
    char back[32];
    size_t n16 = 0;
    size_t n32 = 0;
    size_t nb = 0;
    int ok = 0;

    ROBO_CHECK_EQ_INT(robo_unicode_utf8_count(ar, ar_len, ROBO_UNICODE_STRICT, &ok),
                      4);
    ROBO_CHECK(ok);

    ROBO_CHECK_EQ_INT(
        robo_unicode_utf8_to_utf16(ar, ar_len, u16, 16, &n16, ROBO_UNICODE_STRICT),
        ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(n16, 4);

    ROBO_CHECK_EQ_INT(
        robo_unicode_utf16_to_utf8(u16, n16, back, sizeof(back), &nb,
                                   ROBO_UNICODE_STRICT),
        ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(nb, (int)ar_len);
    ROBO_CHECK(memcmp(ar, back, ar_len) == 0);

    ROBO_CHECK_EQ_INT(
        robo_unicode_utf8_to_utf32(ar, ar_len, u32, 16, &n32, ROBO_UNICODE_STRICT),
        ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(n32, 4);
    ROBO_CHECK_EQ_INT(u32[0], 0x0633);

    ROBO_CHECK_EQ_INT(
        robo_unicode_utf32_to_utf8(u32, n32, back, sizeof(back), &nb,
                                   ROBO_UNICODE_STRICT),
        ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(nb, (int)ar_len);

    /* Measure-only */
    ROBO_CHECK_EQ_INT(
        robo_unicode_utf8_to_utf16(ar, ar_len, NULL, 0, &n16, ROBO_UNICODE_STRICT),
        ROBO_UNICODE_OK);
    ROBO_CHECK_EQ_INT(n16, 4);
}

static void test_unicode_multilingual_phrases(void)
{
    struct {
        const char *utf8;
        size_t bytes;
        size_t cps;
    } cases[] = {
        {"Hello", 5, 5},
        {"caf\xC3\xA9", 5, 4},
        {"\xD8\xB9\xD8\xB1\xD8\xA8\xDB\x8C", 8, 4},           /* عربی */
        {"\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88", 8, 4},           /* اردو */
        {"\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C", 10, 5}, /* فارسی */
        {"\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D", 8, 4},           /* שלום */
        {"\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D"
         "\xE0\xA4\xA4\xE0\xA5\x87",
         18, 6}, /* नमस्ते — न म स ् त े */
        {"\xE0\xA6\xAC\xE0\xA6\xBE\xE0\xA6\x82\xE0\xA6\xB2\xE0\xA6\xBE", 15,
         5}, /* বাংলা */
        {"\xE0\xB8\xAA\xE0\xB8\xA7\xE0\xB8\xB1\xE0\xB8\xAA\xE0\xB8\x94\xE0\xB8"
         "\xB5",
         18, 6}, /* สวัสดี */
        {"\xE4\xBD\xA0\xE5\xA5\xBD", 6, 2},                   /* 你好 */
        {"\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF", 15,
         5}, /* こんにちは */
        {"\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94", 15,
         5}, /* 안녕하세요 */
        {"\xF0\x9F\x98\x80", 4, 1}, /* 😀 */
        {"a\xCC\x81", 3, 2},        /* a + combining acute */
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int ok = 0;
        size_t n;
        ROBO_CHECK(robo_unicode_utf8_validate(cases[i].utf8, cases[i].bytes));
        n = robo_unicode_utf8_count(cases[i].utf8, cases[i].bytes,
                                    ROBO_UNICODE_STRICT, &ok);
        ROBO_CHECK(ok);
        ROBO_CHECK_EQ_INT(n, (int)cases[i].cps);
        ROBO_CHECK_EQ_INT(cases[i].bytes, (int)strlen(cases[i].utf8));
    }
}

static void test_unicode_string_integration(void)
{
    robo_string_t s;
    size_t off = 0;
    size_t next = 0;
    uint32_t cp = 0;
    int ok = 0;

    ROBO_CHECK_EQ_INT(robo_string_init(&s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_append_codepoint(&s, 0x1F600), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_append_codepoint(&s, 0x4E2D), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_size(&s), 7); /* 4 + 3 bytes */
    ROBO_CHECK_EQ_INT(robo_string_codepoint_count(&s, ROBO_UNICODE_STRICT, &ok),
                      2);
    ROBO_CHECK(ok);
    ROBO_CHECK(robo_string_utf8_valid(&s));

    ROBO_CHECK_EQ_INT(
        robo_string_next_codepoint(&s, 0, ROBO_UNICODE_STRICT, &cp, &next), 0);
    ROBO_CHECK_EQ_INT(cp, 0x1F600);
    off = next;
    ROBO_CHECK_EQ_INT(
        robo_string_next_codepoint(&s, off, ROBO_UNICODE_STRICT, &cp, &next), 0);
    ROBO_CHECK_EQ_INT(cp, 0x4E2D);

    ROBO_CHECK_EQ_INT(robo_string_append_codepoint(&s, 0xD800), ROBO_PARSE_ERROR);
    robo_string_destroy(&s);
}

static void test_unicode_utf32_encode_reject(void)
{
    robo_uchar32_t out = 0;
    size_t w = 0;
    ROBO_CHECK_EQ_INT(robo_unicode_utf32_encode(0xD800, &out, 1, &w),
                      ROBO_UNICODE_OUT_OF_RANGE);
    ROBO_CHECK_EQ_INT(robo_unicode_utf8_encode(0x110000, NULL, 0, &w),
                      ROBO_UNICODE_OUT_OF_RANGE);
}

void robo_test_register_unicode(void)
{
    robo_test_register("unicode.version_scalars", test_unicode_version_and_scalars);
    robo_test_register("unicode.ascii_latin1", test_unicode_ascii_latin1);
    robo_test_register("unicode.scripts_emoji", test_unicode_scripts_and_emoji);
    robo_test_register("unicode.utf8_invalid", test_unicode_utf8_invalid);
    robo_test_register("unicode.utf8_next_prev", test_unicode_utf8_next_prev);
    robo_test_register("unicode.utf16_surrogates", test_unicode_utf16_surrogates);
    robo_test_register("unicode.conversions", test_unicode_conversions);
    robo_test_register("unicode.multilingual", test_unicode_multilingual_phrases);
    robo_test_register("unicode.string_integration",
                       test_unicode_string_integration);
    robo_test_register("unicode.utf32_reject", test_unicode_utf32_encode_reject);
}
