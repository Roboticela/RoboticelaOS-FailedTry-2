/*
 * RoboUnicode suite (Goal 3.3) — encoding / validation / conversion.
 */

#include "RoboContainers/RoboString.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboTest.h"

#include <string.h>

static void ExpectUtf8Roundtrip(Uchar32 cp)
{
    char buf[4];
    size_t n = 0;
    size_t next = 0;
    Uchar32 out = 0;

    RoboCheckEqInt(UnicodeUtf8Encode(cp, buf, sizeof(buf), &n),
                      RoboUnicodeOk);
    RoboCheck(n >= 1 && n <= 4);
    RoboCheckEqInt(
        UnicodeUtf8Decode(buf, n, 0, RoboUnicodeStrict, &out, &next),
        RoboUnicodeOk);
    RoboCheckEqInt(out, (int)cp);
    RoboCheckEqInt(next, (int)n);
}

static void TestUnicodeVersionAndScalars(void)
{
    RoboCheckStrEq(UnicodeVersionString(), "16.0.0");
    RoboCheck(UnicodeIsScalar(0));
    RoboCheck(UnicodeIsScalar(0x10FFFF));
    RoboCheck(!UnicodeIsScalar(0x110000));
    RoboCheck(UnicodeIsSurrogate(0xD800));
    RoboCheck(UnicodeIsSurrogate(0xDFFF));
    RoboCheck(!UnicodeIsScalar(0xD800));
    RoboCheck(!UnicodeIsScalar(0xDFFF));
}

static void TestUnicodeAsciiLatin1(void)
{
    ExpectUtf8Roundtrip('A');
    ExpectUtf8Roundtrip(0x00); /* U+0000 */
    ExpectUtf8Roundtrip(0x7F);
    ExpectUtf8Roundtrip(0xA0); /* NBSP */
    ExpectUtf8Roundtrip(0xFF); /* ÿ */
}

static void TestUnicodeScriptsAndEmoji(void)
{
    /* Arabic: سلام U+0633 U+0644 U+0627 U+0645 */
    ExpectUtf8Roundtrip(0x0633);
    ExpectUtf8Roundtrip(0x0644);
    /* Urdu / Persian letters */
    ExpectUtf8Roundtrip(0x06BE); /* ھ */
    ExpectUtf8Roundtrip(0x067E); /* پ */
    ExpectUtf8Roundtrip(0x06AF); /* گ */
    /* Hebrew */
    ExpectUtf8Roundtrip(0x05D0); /* א */
    /* Devanagari */
    ExpectUtf8Roundtrip(0x0905); /* अ */
    ExpectUtf8Roundtrip(0x0939); /* ह */
    /* Bengali */
    ExpectUtf8Roundtrip(0x0985); /* অ */
    /* Thai */
    ExpectUtf8Roundtrip(0x0E01); /* ก */
    /* CJK */
    ExpectUtf8Roundtrip(0x4E2D); /* 中 */
    ExpectUtf8Roundtrip(0x65E5); /* 日 */
    ExpectUtf8Roundtrip(0xD55C); /* 한 */
    /* Emoji (supplementary plane) */
    ExpectUtf8Roundtrip(0x1F600); /* 😀 */
    ExpectUtf8Roundtrip(0x1F4A9);
    /* Combining mark */
    ExpectUtf8Roundtrip(0x0301); /* combining acute */
    /* Boundary */
    ExpectUtf8Roundtrip(0x10000); /* first supplementary */
    ExpectUtf8Roundtrip(0x10FFFF);
}

static void TestUnicodeUtf8Invalid(void)
{
    size_t next = 0;
    Uchar32 cp = 0;
    /* Overlong encoding of U+002F as C0 AF */
    const char overlong[] = {(char)0xC0, (char)0xAF};
    /* Surrogate U+D800 as ED A0 80 */
    const char sur[] = {(char)0xED, (char)0xA0, (char)0x80};
    /* Truncated */
    const char trunc[] = {(char)0xE2, (char)0x82};
    /* Lone continuation */
    const char cont[] = {(char)0x80};
    /* Above U+10FFFF: F4 90 80 80 */
    const char tooBig[] = {(char)0xF4, (char)0x90, (char)0x80, (char)0x80};

    RoboCheckEqInt(UnicodeUtf8Decode(overlong, 2, 0, RoboUnicodeStrict,
                                               &cp, &next),
                      RoboUnicodeInvalid);
    RoboCheckEqInt(
        UnicodeUtf8Decode(sur, 3, 0, RoboUnicodeStrict, &cp, &next),
        RoboUnicodeInvalid);
    RoboCheckEqInt(
        UnicodeUtf8Decode(trunc, 2, 0, RoboUnicodeStrict, &cp, &next),
        RoboUnicodeTruncated);
    RoboCheckEqInt(
        UnicodeUtf8Decode(cont, 1, 0, RoboUnicodeStrict, &cp, &next),
        RoboUnicodeInvalid);
    RoboCheckEqInt(
        UnicodeUtf8Decode(tooBig, 4, 0, RoboUnicodeStrict, &cp, &next),
        RoboUnicodeInvalid);

    RoboCheck(!UnicodeUtf8Validate(overlong, 2));
    RoboCheck(!UnicodeUtf8Validate(sur, 3));

    /* REPLACE consumes one byte */
    RoboCheckEqInt(UnicodeUtf8Decode(cont, 1, 0, RoboUnicodeReplace,
                                               &cp, &next),
                      RoboUnicodeOk);
    RoboCheckEqInt(cp, (int)RoboUchar32Replacement);
    RoboCheckEqInt(next, 1);
}

static void TestUnicodeUtf8NextPrev(void)
{
    /* "A中" = 41 E4 B8 AD */
    const char *s = "A\xE4\xB8\xAD";
    size_t len = 4;
    size_t next = 0;
    size_t start = 0;
    Uchar32 cp = 0;

    RoboCheckEqInt(
        UnicodeUtf8Next(s, len, 0, RoboUnicodeStrict, &cp, &next),
        RoboUnicodeOk);
    RoboCheckEqInt(cp, 'A');
    RoboCheckEqInt(next, 1);

    RoboCheckEqInt(
        UnicodeUtf8Next(s, len, 1, RoboUnicodeStrict, &cp, &next),
        RoboUnicodeOk);
    RoboCheckEqInt(cp, 0x4E2D);
    RoboCheckEqInt(next, 4);

    RoboCheckEqInt(
        UnicodeUtf8Prev(s, len, 4, RoboUnicodeStrict, &cp, &start),
        RoboUnicodeOk);
    RoboCheckEqInt(cp, 0x4E2D);
    RoboCheckEqInt(start, 1);

    RoboCheckEqInt(
        UnicodeUtf8Prev(s, len, 1, RoboUnicodeStrict, &cp, &start),
        RoboUnicodeOk);
    RoboCheckEqInt(cp, 'A');
    RoboCheckEqInt(start, 0);
}

static void TestUnicodeUtf16Surrogates(void)
{
    Uchar16 pair[2];
    Uchar16 loneLow[] = {0xDC00};
    Uchar16 loneHi[] = {0xD800};
    size_t n = 0;
    size_t next = 0;
    Uchar32 cp = 0;

    RoboCheckEqInt(UnicodeUtf16Encode(0x1F600, pair, 2, &n),
                      RoboUnicodeOk);
    RoboCheckEqInt(n, 2);
    RoboCheckEqInt(
        UnicodeUtf16Decode(pair, 2, 0, RoboUnicodeStrict, &cp, &next),
        RoboUnicodeOk);
    RoboCheckEqInt(cp, 0x1F600);
    RoboCheckEqInt(next, 2);

    RoboCheckEqInt(
        UnicodeUtf16Decode(loneLow, 1, 0, RoboUnicodeStrict, &cp, &next),
        RoboUnicodeInvalid);
    RoboCheckEqInt(
        UnicodeUtf16Decode(loneHi, 1, 0, RoboUnicodeStrict, &cp, &next),
        RoboUnicodeTruncated);
    RoboCheck(!UnicodeUtf16Validate(loneLow, 1));
}

static void TestUnicodeConversions(void)
{
    /* سلام */
    const char *ar = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    size_t arLen = 8;
    Uchar16 u16[16];
    Uchar32 u32[16];
    char back[32];
    size_t n16 = 0;
    size_t n32 = 0;
    size_t nb = 0;
    int ok = 0;

    RoboCheckEqInt(UnicodeUtf8Count(ar, arLen, RoboUnicodeStrict, &ok),
                      4);
    RoboCheck(ok);

    RoboCheckEqInt(
        UnicodeUtf8ToUtf16(ar, arLen, u16, 16, &n16, RoboUnicodeStrict),
        RoboUnicodeOk);
    RoboCheckEqInt(n16, 4);

    RoboCheckEqInt(
        UnicodeUtf16ToUtf8(u16, n16, back, sizeof(back), &nb,
                                   RoboUnicodeStrict),
        RoboUnicodeOk);
    RoboCheckEqInt(nb, (int)arLen);
    RoboCheck(memcmp(ar, back, arLen) == 0);

    RoboCheckEqInt(
        UnicodeUtf8ToUtf32(ar, arLen, u32, 16, &n32, RoboUnicodeStrict),
        RoboUnicodeOk);
    RoboCheckEqInt(n32, 4);
    RoboCheckEqInt(u32[0], 0x0633);

    RoboCheckEqInt(
        UnicodeUtf32ToUtf8(u32, n32, back, sizeof(back), &nb,
                                   RoboUnicodeStrict),
        RoboUnicodeOk);
    RoboCheckEqInt(nb, (int)arLen);

    /* Measure-only */
    RoboCheckEqInt(
        UnicodeUtf8ToUtf16(ar, arLen, NULL, 0, &n16, RoboUnicodeStrict),
        RoboUnicodeOk);
    RoboCheckEqInt(n16, 4);
}

static void TestUnicodeMultilingualPhrases(void)
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
        RoboCheck(UnicodeUtf8Validate(cases[i].utf8, cases[i].bytes));
        n = UnicodeUtf8Count(cases[i].utf8, cases[i].bytes,
                                    RoboUnicodeStrict, &ok);
        RoboCheck(ok);
        RoboCheckEqInt(n, (int)cases[i].cps);
        RoboCheckEqInt(cases[i].bytes, (int)strlen(cases[i].utf8));
    }
}

static void TestUnicodeStringIntegration(void)
{
    String s;
    size_t off = 0;
    size_t next = 0;
    uint32_t cp = 0;
    int ok = 0;

    RoboCheckEqInt(StringInit(&s), RoboOk);
    RoboCheckEqInt(StringAppendCodepoint(&s, 0x1F600), RoboOk);
    RoboCheckEqInt(StringAppendCodepoint(&s, 0x4E2D), RoboOk);
    RoboCheckEqInt(RoboStringSize(&s), 7); /* 4 + 3 bytes */
    RoboCheckEqInt(StringCodepointCount(&s, RoboUnicodeStrict, &ok),
                      2);
    RoboCheck(ok);
    RoboCheck(StringUtf8Valid(&s));

    RoboCheckEqInt(
        StringNextCodepoint(&s, 0, RoboUnicodeStrict, &cp, &next), 0);
    RoboCheckEqInt(cp, 0x1F600);
    off = next;
    RoboCheckEqInt(
        StringNextCodepoint(&s, off, RoboUnicodeStrict, &cp, &next), 0);
    RoboCheckEqInt(cp, 0x4E2D);

    RoboCheckEqInt(StringAppendCodepoint(&s, 0xD800), RoboParseError);
    StringDestroy(&s);
}

static void TestUnicodeUtf32EncodeReject(void)
{
    Uchar32 out = 0;
    size_t w = 0;
    RoboCheckEqInt(UnicodeUtf32Encode(0xD800, &out, 1, &w),
                      RoboUnicodeOutOfRange);
    RoboCheckEqInt(UnicodeUtf8Encode(0x110000, NULL, 0, &w),
                      RoboUnicodeOutOfRange);
}

void TestRegisterUnicode(void)
{
    TestRegister("unicode.version_scalars", TestUnicodeVersionAndScalars);
    TestRegister("unicode.ascii_latin1", TestUnicodeAsciiLatin1);
    TestRegister("unicode.scripts_emoji", TestUnicodeScriptsAndEmoji);
    TestRegister("unicode.utf8_invalid", TestUnicodeUtf8Invalid);
    TestRegister("unicode.utf8_next_prev", TestUnicodeUtf8NextPrev);
    TestRegister("unicode.utf16_surrogates", TestUnicodeUtf16Surrogates);
    TestRegister("unicode.conversions", TestUnicodeConversions);
    TestRegister("unicode.multilingual", TestUnicodeMultilingualPhrases);
    TestRegister("unicode.string_integration",
                       TestUnicodeStringIntegration);
    TestRegister("unicode.utf32_reject", TestUnicodeUtf32EncodeReject);
}
