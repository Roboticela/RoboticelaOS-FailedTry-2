/*
 * RoboI18N currency metadata + formatting suite (Goal 4.21).
 */

#include "RoboI18N/RoboCurrencyData.h"
#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void ExpectCurrency(const char *tag,
                            const char *code,
                            I18nCurrencyStyle style,
                            int64_t major,
                            const char *want)
{
    I18nLocale loc;
    I18nCurrencyFormatter fmt;
    I18nCurrencyOptions opts;
    char buf[256];
    size_t len = 0;

    ParseTag(tag, &loc);
    I18nCurrencyOptionsInit(&opts);
    opts.style = style;
    if (code && code[0]) {
        memcpy(opts.currency, code, 4u);
    }
    RoboCheckEqInt(I18nCurrencyFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nCurrencyFormatI64(&fmt, major, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, want);
    I18nCurrencyFormatterDestroy(&fmt);
}

static void ExpectCurrencyDec(const char *tag,
                                const char *code,
                                I18nCurrencyStyle style,
                                const char *digits,
                                uint8_t scale,
                                uint8_t negative,
                                const char *want)
{
    I18nLocale loc;
    I18nCurrencyFormatter fmt;
    I18nCurrencyOptions opts;
    I18nDecimal dec;
    char buf[256];
    size_t len = 0;
    size_t n;

    ParseTag(tag, &loc);
    I18nCurrencyOptionsInit(&opts);
    opts.style = style;
    if (code && code[0]) {
        memcpy(opts.currency, code, 4u);
    }
    memset(&dec, 0, sizeof(dec));
    dec.kind = RoboI18nDecimalNumber;
    dec.negative = negative;
    dec.scale = scale;
    n = strlen(digits);
    RoboCheck(n > 0u && n <= RoboI18nDecimalDigitsMax);
    dec.digitCount = (uint8_t)n;
    memcpy(dec.digits, digits, n);

    RoboCheckEqInt(I18nCurrencyFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nCurrencyFormat(&fmt, &dec, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, want);
    I18nCurrencyFormatterDestroy(&fmt);
}

static void TestCurrencyVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nCurrencyCldrVersionString(), "46");
    RoboCheckStrEq(RoboCurrencyCldrVersion, "46");
    RoboCheck(CurrMetaCount > 100u);
    RoboCheck(CurrRegionCount > 100u);
    RoboCheck(CurrFmtCount > 10u);
}

static void TestCurrencyMeta(void)
{
    I18nCurrencyInfo info;
    char code[4];

    RoboCheckEqInt(I18nCurrencyNormalize("usd", 3, code), RoboOk);
    RoboCheckStrEq(code, "USD");
    RoboCheckEqInt(I18nCurrencyNormalize("US", 2, code), RoboInvalidArgument);
    RoboCheckEqInt(I18nCurrencyNormalize("12A", 3, code), RoboInvalidArgument);

    RoboCheckEqInt(I18nCurrencyLookup("USD", 3, &info), RoboOk);
    RoboCheckEqInt((int)info.digits, 2);
    RoboCheckEqInt((int)info.numeric, 840);
    RoboCheck(info.known);

    RoboCheckEqInt(I18nCurrencyLookup("JPY", 3, &info), RoboOk);
    RoboCheckEqInt((int)info.digits, 0);

    RoboCheckEqInt(I18nCurrencyLookup("KWD", 3, &info), RoboOk);
    RoboCheckEqInt((int)info.digits, 3);

    RoboCheckEqInt(I18nCurrencyLookup("CHF", 3, &info), RoboOk);
    RoboCheckEqInt((int)info.digits, 2);
    RoboCheckEqInt((int)info.cashRounding, 5);

    RoboCheckEqInt(I18nCurrencyLookup("PKR", 3, &info), RoboOk);
    RoboCheckEqInt((int)info.cashDigits, 0);

    /* Well-formed unknown → defaults, NOT_FOUND */
    RoboCheckEqInt(I18nCurrencyLookup("ZZZ", 3, &info), RoboNotFound);
    RoboCheckStrEq(info.code, "ZZZ");
    RoboCheckEqInt((int)info.digits, 2);
    RoboCheck(!info.known);
}

static void TestCurrencyRegionLocale(void)
{
    char code[4];
    I18nLocale loc;

    RoboCheckEqInt(I18nCurrencyForRegion("US", 2, code), RoboOk);
    RoboCheckStrEq(code, "USD");
    RoboCheckEqInt(I18nCurrencyForRegion("GB", 2, code), RoboOk);
    RoboCheckStrEq(code, "GBP");
    RoboCheckEqInt(I18nCurrencyForRegion("DE", 2, code), RoboOk);
    RoboCheckStrEq(code, "EUR");
    RoboCheckEqInt(I18nCurrencyForRegion("JP", 2, code), RoboOk);
    RoboCheckStrEq(code, "JPY");
    RoboCheckEqInt(I18nCurrencyForRegion("PK", 2, code), RoboOk);
    RoboCheckStrEq(code, "PKR");
    RoboCheckEqInt(I18nCurrencyForRegion("BD", 2, code), RoboOk);
    RoboCheckStrEq(code, "BDT");

    ParseTag("en-US", &loc);
    RoboCheckEqInt(I18nCurrencyForLocale(&loc, code), RoboOk);
    RoboCheckStrEq(code, "USD");

    ParseTag("en-US-u-cu-eur", &loc);
    RoboCheckEqInt(I18nCurrencyForLocale(&loc, code), RoboOk);
    RoboCheckStrEq(code, "EUR");
}

static void TestCurrencyEnUs(void)
{
    ExpectCurrency("en-US", "USD", RoboI18nCurrencyStyleSymbol, 1234, "$1,234.00");
    ExpectCurrency("en-US", "USD", RoboI18nCurrencyStyleSymbol, 0, "$0.00");
    ExpectCurrencyDec("en-US", "USD", RoboI18nCurrencyStyleSymbol, "12345", 2, 1,
                        "-$123.45");
    ExpectCurrency("en-US", "USD", RoboI18nCurrencyStyleCode, 12, "USD\xC2\xA0" "12.00");
    ExpectCurrency("en-US", "USD", RoboI18nCurrencyStyleName, 1, "1.00 US dollar");
    ExpectCurrency("en-US", "USD", RoboI18nCurrencyStyleName, 2, "2.00 US dollars");
}

static void TestCurrencyAccounting(void)
{
    ExpectCurrencyDec("en-US", "USD", RoboI18nCurrencyStyleAccounting, "12345", 2, 1,
                        "($123.45)");
    ExpectCurrency("en-US", "USD", RoboI18nCurrencyStyleAccounting, 10, "$10.00");
    /* fr accounting uses parentheses without hard-coded US rules */
    ExpectCurrencyDec("fr-FR", "EUR", RoboI18nCurrencyStyleAccounting, "12345", 2, 1,
                        "(123,45\xC2\xA0\xE2\x82\xAC)");
}

static void TestCurrencyIntlMatrix(void)
{
    ExpectCurrency("en-GB", "GBP", RoboI18nCurrencyStyleSymbol, 10, "\xC2\xA3" "10.00");
    ExpectCurrency("de-DE", "EUR", RoboI18nCurrencyStyleSymbol, 10,
                    "10,00\xC2\xA0\xE2\x82\xAC");
    ExpectCurrency("fr-FR", "EUR", RoboI18nCurrencyStyleSymbol, 10,
                    "10,00\xC2\xA0\xE2\x82\xAC");
    ExpectCurrency("ja-JP", "JPY", RoboI18nCurrencyStyleSymbol, 1234,
                    "\xEF\xBF\xA5" "1,234"); /* U+FFE5 fullwidth yen */
    ExpectCurrency("zh-CN", "CNY", RoboI18nCurrencyStyleSymbol, 10, "\xC2\xA5" "10.00");
    ExpectCurrency("hi-IN", "INR", RoboI18nCurrencyStyleSymbol, 123456,
                    "\xE2\x82\xB9" "1,23,456.00");
    ExpectCurrency("ko-KR", "KRW", RoboI18nCurrencyStyleSymbol, 1000, "\xE2\x82\xA9" "1,000");

    /* ur-PK / ar-SA / fa-IR / bn-BD — symbol or code path must succeed */
    {
        I18nLocale loc;
        I18nCurrencyFormatter fmt;
        I18nCurrencyOptions opts;
        char buf[256];
        size_t len = 0;
        const char *cases[][2] = {
            {"ur-PK", "PKR"}, {"ar-SA", "SAR"}, {"fa-IR", "IRR"}, {"bn-BD", "BDT"}};
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            ParseTag(cases[i][0], &loc);
            I18nCurrencyOptionsInit(&opts);
            memcpy(opts.currency, cases[i][1], 4u);
            RoboCheckEqInt(I18nCurrencyFormatterInit(&fmt, &loc, &opts),
                              RoboOk);
            RoboCheckEqInt(
                I18nCurrencyFormatI64(&fmt, 100, buf, sizeof(buf), &len), RoboOk);
            RoboCheck(len > 0u);
            I18nCurrencyFormatterDestroy(&fmt);
        }
    }
}

static void TestCurrencyZeroAndThreeDecimals(void)
{
    ExpectCurrency("en-US", "JPY", RoboI18nCurrencyStyleSymbol, 1000, "\xC2\xA5" "1,000");
    ExpectCurrencyDec("en-US", "KWD", RoboI18nCurrencyStyleSymbol, "1234567", 3, 0,
                        "KWD\xC2\xA0" "1,234.567");
}

static void TestCurrencyCashRounding(void)
{
    I18nLocale loc;
    I18nCurrencyFormatter fmt;
    I18nCurrencyOptions opts;
    I18nDecimal dec;
    char buf[128];
    size_t len = 0;

    ParseTag("de-CH", &loc);
    I18nCurrencyOptionsInit(&opts);
    memcpy(opts.currency, "CHF", 4u);
    opts.usage = RoboI18nCurrencyUsageCash;
    opts.style = RoboI18nCurrencyStyleCode;

    memset(&dec, 0, sizeof(dec));
    dec.kind = RoboI18nDecimalNumber;
    dec.digitCount = 4;
    dec.scale = 2;
    memcpy(dec.digits, "1203", 4); /* 12.03 → cash 12.05 */

    RoboCheckEqInt(I18nCurrencyFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nCurrencyFormat(&fmt, &dec, buf, sizeof(buf), &len),
                      RoboOk);
    /* de pattern places currency after number with NBSP when alpha */
    RoboCheck(strstr(buf, "12.05") != NULL || strstr(buf, "12,05") != NULL);
    I18nCurrencyFormatterDestroy(&fmt);

    /* STANDARD must NOT apply cash rounding */
    opts.usage = RoboI18nCurrencyUsageStandard;
    RoboCheckEqInt(I18nCurrencyFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nCurrencyFormat(&fmt, &dec, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheck(strstr(buf, "12.03") != NULL || strstr(buf, "12,03") != NULL);
    I18nCurrencyFormatterDestroy(&fmt);
}

static void TestCurrencyMinorUnits(void)
{
    I18nLocale loc;
    I18nCurrencyFormatter fmt;
    I18nCurrencyOptions opts;
    char buf[128];
    size_t len = 0;

    ParseTag("en-US", &loc);
    I18nCurrencyOptionsInit(&opts);
    memcpy(opts.currency, "USD", 4u);
    RoboCheckEqInt(I18nCurrencyFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nCurrencyFormatMinor(&fmt, 12345, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, "$123.45");
    RoboCheckEqInt(I18nCurrencyFormatMinor(&fmt, -50, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, "-$0.50");
    I18nCurrencyFormatterDestroy(&fmt);
}

static void TestCurrencyCfAccount(void)
{
    I18nLocale loc;
    I18nCurrencyFormatter fmt;
    I18nCurrencyOptions opts;
    I18nDecimal dec;
    char buf[128];
    size_t len = 0;

    ParseTag("en-US-u-cf-account", &loc);
    I18nCurrencyOptionsInit(&opts);
    memcpy(opts.currency, "USD", 4u);
    memset(&dec, 0, sizeof(dec));
    dec.kind = RoboI18nDecimalNumber;
    dec.negative = 1;
    dec.digitCount = 3;
    dec.scale = 2;
    memcpy(dec.digits, "100", 3);
    RoboCheckEqInt(I18nCurrencyFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheck(fmt.accounting);
    RoboCheckEqInt(I18nCurrencyFormat(&fmt, &dec, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, "($1.00)");
    I18nCurrencyFormatterDestroy(&fmt);
}

static void TestCurrencyRobustness(void)
{
    I18nLocale loc;
    I18nCurrencyFormatter fmt;
    I18nCurrencyOptions opts;
    I18nCurrencyInfo info;
    char buf[64];
    size_t len = 0;
    size_t i;

    RoboCheckEqInt(I18nCurrencyLookup(NULL, 3, &info), RoboInvalidArgument);
    RoboCheckEqInt(I18nCurrencyLookup("", 0, &info), RoboInvalidArgument);
    RoboCheckEqInt(I18nCurrencyForRegion("", 0, buf), RoboInvalidArgument);

    ParseTag("en", &loc);
    I18nCurrencyOptionsInit(&opts);
    memcpy(opts.currency, "!!!", 3);
    opts.currency[3] = '\0';
    RoboCheckEqInt(I18nCurrencyFormatterInit(&fmt, &loc, &opts),
                      RoboInvalidArgument);

    memcpy(opts.currency, "USD", 4u);
    RoboCheckEqInt(I18nCurrencyFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nCurrencyFormat(&fmt, NULL, buf, sizeof(buf), &len),
                      RoboInvalidArgument);

    /* Fuzz well-formed codes */
    for (i = 0; i < 26u; i++) {
        char c[4] = {'A', 'A', (char)('A' + (i % 26)), '\0'};
        (void)I18nCurrencyLookup(c, 3, &info);
        memcpy(opts.currency, c, 4u);
        if (I18nCurrencyFormatterInit(&fmt, &loc, &opts) == RoboOk) {
            (void)I18nCurrencyFormatI64(&fmt, (int64_t)i * 1000, buf, sizeof(buf),
                                                &len);
            I18nCurrencyFormatterDestroy(&fmt);
        }
    }

    /* Malformed locale must not crash */
    if (I18nLocaleParseCanonical("@@@", 3, &loc) != RoboOk) {
        RoboCheck(1);
    }
}

void TestRegisterI18nCurrency(void)
{
    TestRegister("i18n.currency.versions", TestCurrencyVersions);
    TestRegister("i18n.currency.meta", TestCurrencyMeta);
    TestRegister("i18n.currency.region", TestCurrencyRegionLocale);
    TestRegister("i18n.currency.en_us", TestCurrencyEnUs);
    TestRegister("i18n.currency.accounting", TestCurrencyAccounting);
    TestRegister("i18n.currency.intl", TestCurrencyIntlMatrix);
    TestRegister("i18n.currency.precision", TestCurrencyZeroAndThreeDecimals);
    TestRegister("i18n.currency.cash", TestCurrencyCashRounding);
    TestRegister("i18n.currency.minor", TestCurrencyMinorUnits);
    TestRegister("i18n.currency.cf", TestCurrencyCfAccount);
    TestRegister("i18n.currency.robust", TestCurrencyRobustness);
}
