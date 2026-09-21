/*
 * RoboI18N number formatting suite (Goal 4.3).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NNumber.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void FmtI64(const char *tag,
                    const I18nNumberOptions *opts,
                    int64_t value,
                    const char *want)
{
    I18nLocale loc;
    I18nNumberFormatter fmt;
    char buf[128];
    size_t n = 0;
    ParseTag(tag, &loc);
    RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, opts), RoboOk);
    RoboCheckEqInt(I18nNumberFormatI64(&fmt, value, buf, sizeof(buf), &n), RoboOk);
    RoboCheckStrEq(buf, want);
    RoboCheckEqInt((int)n, (int)strlen(want));
    I18nNumberFormatterDestroy(&fmt);
}

static void FmtDec(const char *tag,
                    const I18nNumberOptions *opts,
                    const I18nDecimal *dec,
                    const char *want)
{
    I18nLocale loc;
    I18nNumberFormatter fmt;
    char buf[192];
    ParseTag(tag, &loc);
    RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, opts), RoboOk);
    RoboCheckEqInt(I18nNumberFormat(&fmt, dec, buf, sizeof(buf), NULL), RoboOk);
    RoboCheckStrEq(buf, want);
    I18nNumberFormatterDestroy(&fmt);
}

static void MakeDec(const char *ascii, uint8_t scale, uint8_t neg, I18nDecimal *out)
{
    size_t n = strlen(ascii);
    memset(out, 0, sizeof(*out));
    out->kind = RoboI18nDecimalNumber;
    out->negative = neg;
    out->scale = scale;
    out->digitCount = (uint8_t)n;
    memcpy(out->digits, ascii, n);
}

static void TestNumberVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nCldrVersionString(), "46");
}

static void TestNumberEnUsIntegers(void)
{
    I18nNumberOptions opts;
    I18nNumberOptionsInit(&opts);
    opts.maxFractionDigits = 0;
    FmtI64("en-US", &opts, 0, "0");
    FmtI64("en-US", &opts, 1, "1");
    FmtI64("en-US", &opts, 10, "10");
    FmtI64("en-US", &opts, 999, "999");
    FmtI64("en-US", &opts, 1000, "1,000");
    FmtI64("en-US", &opts, 1234567, "1,234,567");
    FmtI64("en-US", &opts, -1234567, "-1,234,567");
    opts.useGrouping = 0;
    FmtI64("en-US", &opts, 1234567, "1234567");
}

static void TestNumberLocaleSymbols(void)
{
    I18nNumberOptions opts;
    I18nDecimal dec;
    I18nNumberOptionsInit(&opts);
    opts.maxFractionDigits = 2;
    opts.minFractionDigits = 2;

    MakeDec("123456", 2, 0, &dec); /* 1234.56 */
    FmtDec("en-US", &opts, &dec, "1,234.56");
    FmtDec("de-DE", &opts, &dec, "1.234,56");
    FmtDec("fr-FR", &opts, &dec, "1\xE2\x80\xAF" "234,56"); /* U+202F */

    MakeDec("1234567", 0, 0, &dec);
    opts.maxFractionDigits = 0;
    opts.minFractionDigits = 0;
    /* Force Latin digits so the assertion checks grouping, not digit glyphs. */
    memcpy(opts.numberingSystem, "latn", 5);
    FmtDec("hi-IN", &opts, &dec, "12,34,567");
    FmtDec("bn-BD", &opts, &dec, "12,34,567");
    opts.numberingSystem[0] = '\0';
}

static void TestNumberDigitsLocalized(void)
{
    I18nNumberOptions opts;
    I18nNumberOptionsInit(&opts);
    opts.maxFractionDigits = 0;

    /* ar-SA default numbering arab; 1234 → 1٬234 */
    {
        I18nLocale loc;
        I18nNumberFormatter fmt;
        char buf[64];
        ParseTag("ar-SA", &loc);
        RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
        RoboCheckEqInt(I18nNumberFormatI64(&fmt, 1234, buf, sizeof(buf), NULL),
                          RoboOk);
        RoboCheckStrEq(buf, "\xD9\xA1\xD9\xAC\xD9\xA2\xD9\xA3\xD9\xA4");
        I18nNumberFormatterDestroy(&fmt);
    }

    /* fa-IR arabext */
    {
        I18nLocale loc;
        I18nNumberFormatter fmt;
        char buf[64];
        ParseTag("fa-IR", &loc);
        RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
        RoboCheckEqInt(I18nNumberFormatI64(&fmt, 12, buf, sizeof(buf), NULL), RoboOk);
        RoboCheckStrEq(buf, "\xDB\xB1\xDB\xB2");
        I18nNumberFormatterDestroy(&fmt);
    }

    /* Explicit thai digits on th-TH */
    {
        I18nLocale loc;
        I18nNumberFormatter fmt;
        char buf[64];
        ParseTag("th-TH", &loc);
        opts.numberingSystem[0] = 't';
        opts.numberingSystem[1] = 'h';
        opts.numberingSystem[2] = 'a';
        opts.numberingSystem[3] = 'i';
        opts.numberingSystem[4] = '\0';
        RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
        RoboCheckEqInt(I18nNumberFormatI64(&fmt, 5, buf, sizeof(buf), NULL), RoboOk);
        RoboCheckStrEq(buf, "\xE0\xB9\x95");
        I18nNumberFormatterDestroy(&fmt);
        opts.numberingSystem[0] = '\0';
    }

    /* hi-IN with native override would still use latn by default; force deva */
    {
        I18nLocale loc;
        I18nNumberFormatter fmt;
        char buf[64];
        ParseTag("hi-IN", &loc);
        memcpy(opts.numberingSystem, "deva", 5);
        RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
        RoboCheckEqInt(I18nNumberFormatI64(&fmt, 7, buf, sizeof(buf), NULL), RoboOk);
        RoboCheckStrEq(buf, "\xE0\xA5\xAD");
        I18nNumberFormatterDestroy(&fmt);
        opts.numberingSystem[0] = '\0';
    }

    /* bn native default is beng */
    {
        I18nLocale loc;
        I18nNumberFormatter fmt;
        char buf[64];
        ParseTag("bn-BD", &loc);
        RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
        RoboCheckEqInt(I18nNumberFormatI64(&fmt, 9, buf, sizeof(buf), NULL), RoboOk);
        RoboCheckStrEq(buf, "\xE0\xA7\xAF");
        I18nNumberFormatterDestroy(&fmt);
    }
}

static void TestNumberRounding(void)
{
    I18nNumberOptions opts;
    I18nDecimal dec;
    I18nNumberOptionsInit(&opts);
    opts.maxFractionDigits = 0;
    opts.minFractionDigits = 0;
    opts.useGrouping = 0;
    opts.rounding = RoboI18nRoundHalfEven;

    MakeDec("15", 1, 0, &dec); /* 1.5 → 2 (even) */
    FmtDec("en-US", &opts, &dec, "2");
    MakeDec("25", 1, 0, &dec); /* 2.5 → 2 */
    FmtDec("en-US", &opts, &dec, "2");
    MakeDec("14", 1, 0, &dec); /* 1.4 → 1 */
    FmtDec("en-US", &opts, &dec, "1");
    MakeDec("16", 1, 0, &dec); /* 1.6 → 2 */
    FmtDec("en-US", &opts, &dec, "2");

    opts.rounding = RoboI18nRoundHalfUp;
    MakeDec("25", 1, 0, &dec);
    FmtDec("en-US", &opts, &dec, "3");

    MakeDec("15", 1, 1, &dec); /* -1.5 half-up → -2 */
    FmtDec("en-US", &opts, &dec, "-2");
}

static void TestNumberFractionOptions(void)
{
    I18nNumberOptions opts;
    I18nDecimal dec;
    I18nNumberOptionsInit(&opts);
    opts.useGrouping = 0;

    MakeDec("1234", 0, 0, &dec);
    opts.minFractionDigits = 2;
    opts.maxFractionDigits = 2;
    FmtDec("en-US", &opts, &dec, "1234.00");

    MakeDec("12345678", 4, 0, &dec); /* 1234.5678 */
    opts.minFractionDigits = 0;
    opts.maxFractionDigits = 2;
    opts.rounding = RoboI18nRoundHalfUp;
    FmtDec("en-US", &opts, &dec, "1234.57");

    opts.minIntegerDigits = 5;
    opts.maxFractionDigits = 0;
    MakeDec("12", 0, 0, &dec);
    FmtDec("en-US", &opts, &dec, "00012");
}

static double BitsToDouble(uint64_t bits)
{
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

static void TestNumberDoubleSpecials(void)
{
    I18nLocale loc;
    I18nNumberFormatter fmt;
    I18nNumberOptions opts;
    char buf[64];

    I18nNumberOptionsInit(&opts);
    ParseTag("en-US", &loc);
    RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);

    RoboCheckEqInt(
        I18nNumberFormatDouble(&fmt, BitsToDouble(0x7ff8000000000001ULL), buf,
                                       sizeof(buf), NULL),
        RoboOk);
    RoboCheckStrEq(buf, "NaN");

    RoboCheckEqInt(
        I18nNumberFormatDouble(&fmt, BitsToDouble(0x7ff0000000000000ULL), buf,
                                       sizeof(buf), NULL),
        RoboOk);
    RoboCheckStrEq(buf, "\xE2\x88\x9E");

    RoboCheckEqInt(
        I18nNumberFormatDouble(&fmt, BitsToDouble(0xfff0000000000000ULL), buf,
                                       sizeof(buf), NULL),
        RoboOk);
    RoboCheckStrEq(buf, "-\xE2\x88\x9E");

    opts.maxFractionDigits = 0;
    I18nNumberFormatterDestroy(&fmt);
    RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(
        I18nNumberFormatDouble(&fmt, BitsToDouble(0x8000000000000000ULL), buf,
                                       sizeof(buf), NULL),
        RoboOk);
    RoboCheckStrEq(buf, "-0");

    I18nNumberFormatterDestroy(&fmt);
}

static void TestNumberU64Exact(void)
{
    I18nNumberOptions opts;
    I18nLocale loc;
    I18nNumberFormatter fmt;
    char buf[64];
    I18nNumberOptionsInit(&opts);
    opts.useGrouping = 0;
    opts.maxFractionDigits = 0;
    ParseTag("en-US", &loc);
    RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(
        I18nNumberFormatU64(&fmt, 18446744073709551615ULL, buf, sizeof(buf), NULL),
        RoboOk);
    RoboCheckStrEq(buf, "18446744073709551615");
    I18nNumberFormatterDestroy(&fmt);
}

static void TestNumberIntlSmoke(void)
{
    static const char *tags[] = {
        "en-US", "en-GB", "de-DE", "fr-FR", "es-ES", "ur-PK", "ar-SA", "ar-EG",
        "fa-IR", "fa-AF", "hi-IN", "bn-BD", "th-TH", "zh-CN", "zh-TW", "ja-JP", "ko-KR",
    };
    size_t i;
    I18nNumberOptions opts;
    I18nNumberOptionsInit(&opts);
    opts.maxFractionDigits = 2;
    opts.minFractionDigits = 2;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        I18nLocale loc;
        I18nNumberFormatter fmt;
        char buf[128];
        size_t n = 0;
        ParseTag(tags[i], &loc);
        RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
        RoboCheckEqInt(I18nNumberFormatI64(&fmt, -1234, buf, sizeof(buf), &n),
                          RoboOk);
        RoboCheck(n > 0);
        RoboCheck(buf[0] != '\0');
        I18nNumberFormatterDestroy(&fmt);
    }
}

static void TestNumberBufferAndString(void)
{
    I18nLocale loc;
    I18nNumberFormatter fmt;
    I18nNumberOptions opts;
    String str;
    size_t need = 0;
    char tiny[2];

    I18nNumberOptionsInit(&opts);
    ParseTag("en-US", &loc);
    RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nNumberFormatI64(&fmt, 1234567, NULL, 0, &need), RoboOk);
    RoboCheck(need == strlen("1,234,567"));
    RoboCheckEqInt(I18nNumberFormatI64(&fmt, 1234567, tiny, sizeof(tiny), &need),
                      RoboError);
    RoboCheck(need == strlen("1,234,567"));

    RoboCheckEqInt(StringInit(&str), RoboOk);
    {
        I18nDecimal dec;
        RoboCheckEqInt(I18nDecimalFromI64(42, &dec), RoboOk);
        RoboCheckEqInt(I18nNumberFormatToString(&fmt, &dec, &str), RoboOk);
        RoboCheckStrEq(StringData(&str), "42");
    }
    StringDestroy(&str);
    I18nNumberFormatterDestroy(&fmt);
}

static void TestNumberInvalid(void)
{
    I18nLocale loc;
    I18nNumberFormatter fmt;
    I18nNumberOptions opts;
    char buf[16];

    I18nNumberOptionsInit(&opts);
    memcpy(opts.numberingSystem, "nope", 5);
    ParseTag("en-US", &loc);
    RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboNotFound);
    RoboCheckEqInt(I18nNumberFormatI64(NULL, 1, buf, sizeof(buf), NULL),
                      RoboInvalidArgument);
}

static void TestNumberMicrobench(void)
{
    volatile int sink = 0;
    clock_t t0, t1;
    size_t i;
    const size_t N = 50000u;
    double ms;
    I18nLocale loc;
    I18nNumberFormatter fmt;
    I18nNumberOptions opts;
    char buf[64];

    I18nNumberOptionsInit(&opts);
    ParseTag("de-DE", &loc);
    RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
    t0 = clock();
    for (i = 0; i < N; i++) {
        sink += (I18nNumberFormatI64(&fmt, (int64_t)i, buf, sizeof(buf), NULL) ==
                 RoboOk);
    }
    t1 = clock();
    ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    printf("i18n.number.microbench N=%zu time_ms=%.3f sink=%d\n", N, ms, sink);
    RoboCheck(sink != 0);
    I18nNumberFormatterDestroy(&fmt);
}

void TestRegisterI18nNumber(void)
{
    TestRegister("i18n.number.versions", TestNumberVersions);
    TestRegister("i18n.number.en_us_integers", TestNumberEnUsIntegers);
    TestRegister("i18n.number.locale_symbols", TestNumberLocaleSymbols);
    TestRegister("i18n.number.digits_localized", TestNumberDigitsLocalized);
    TestRegister("i18n.number.rounding", TestNumberRounding);
    TestRegister("i18n.number.fraction_options", TestNumberFractionOptions);
    TestRegister("i18n.number.double_specials", TestNumberDoubleSpecials);
    TestRegister("i18n.number.u64_exact", TestNumberU64Exact);
    TestRegister("i18n.number.intl_smoke", TestNumberIntlSmoke);
    TestRegister("i18n.number.buffer_string", TestNumberBufferAndString);
    TestRegister("i18n.number.invalid", TestNumberInvalid);
    TestRegister("i18n.number.microbench", TestNumberMicrobench);
}
