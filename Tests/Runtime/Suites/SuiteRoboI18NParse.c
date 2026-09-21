/*
 * RoboI18N number & currency parsing suite (Goal 4.22).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void ExpectNumber(const char *tag,
                          I18nParseMode mode,
                          const char *text,
                          const char *wantDigits,
                          uint8_t wantScale,
                          uint8_t wantNeg)
{
    I18nLocale loc;
    I18nNumberParser p;
    I18nParseOptions opts;
    I18nParseResult res;

    ParseTag(tag, &loc);
    I18nParseOptionsInit(&opts);
    opts.mode = mode;
    RoboCheckEqInt(I18nNumberParserInit(&p, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nParseNumber(&p, text, strlen(text), &res), RoboOk);
    RoboCheckEqInt((int)res.error, (int)RoboI18nParseErrNone);
    RoboCheckEqInt((int)res.value.kind, (int)RoboI18nDecimalNumber);
    RoboCheckEqInt((int)res.value.scale, (int)wantScale);
    RoboCheckEqInt((int)res.value.negative, (int)wantNeg);
    RoboCheckEqInt((int)res.value.digitCount, (int)strlen(wantDigits));
    RoboCheck(memcmp(res.value.digits, wantDigits, res.value.digitCount) == 0);
    I18nNumberParserDestroy(&p);
}

static void ExpectNumberFail(const char *tag,
                               I18nParseMode mode,
                               const char *text,
                               I18nParseErr err)
{
    I18nLocale loc;
    I18nNumberParser p;
    I18nParseOptions opts;
    I18nParseResult res;

    ParseTag(tag, &loc);
    I18nParseOptionsInit(&opts);
    opts.mode = mode;
    RoboCheckEqInt(I18nNumberParserInit(&p, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nParseNumber(&p, text, strlen(text), &res),
                      RoboParseError);
    RoboCheckEqInt((int)res.error, (int)err);
    I18nNumberParserDestroy(&p);
}

static void TestParseVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nParseErrString(RoboI18nParseErrNone), "ok");
}

static void TestParseEnBasic(void)
{
    ExpectNumber("en-US", RoboI18nParseStrict, "1234", "1234", 0, 0);
    ExpectNumber("en-US", RoboI18nParseStrict, "1,234.56", "123456", 2, 0);
    ExpectNumber("en-US", RoboI18nParseStrict, "-1,234.50", "123450", 2, 1);
    ExpectNumber("en-US", RoboI18nParseStrict, "0", "0", 0, 0);
    ExpectNumber("en-US", RoboI18nParseStrict, "1.0", "10", 1, 0);
    ExpectNumber("en-US", RoboI18nParseStrict, "1.00", "100", 2, 0);
    ExpectNumberFail("en-US", RoboI18nParseStrict, "1,23.45",
                       RoboI18nParseErrInvalidGrouping);
    ExpectNumberFail("en-US", RoboI18nParseStrict, "12.34.56",
                       RoboI18nParseErrTrailing);
}

static void TestParseDeFr(void)
{
    ExpectNumber("de-DE", RoboI18nParseStrict, "1.234,56", "123456", 2, 0);
    ExpectNumber("fr-FR", RoboI18nParseStrict, "1\xE2\x80\xAF" "234,56", "123456", 2,
                  0); /* U+202F narrow NBSP */
    ExpectNumber("es-ES", RoboI18nParseStrict, "1.234,5", "12345", 1, 0);
}

static void TestParseHiGrouping(void)
{
    /* Indian secondary grouping of 2 */
    ExpectNumber("hi-IN", RoboI18nParseStrict, "1,23,456.78", "12345678", 2, 0);
}

static void TestParseLocalizedDigits(void)
{
    /* Arabic-Indic ١٢٣٤٫٥٦ with arab numbering — use ar with nu=arab if needed */
    I18nLocale loc;
    I18nNumberParser p;
    I18nParseOptions opts;
    I18nParseResult res;

    ParseTag("ar", &loc);
    I18nParseOptionsInit(&opts);
    memcpy(opts.numberingSystem, "arab", 5);
    RoboCheckEqInt(I18nNumberParserInit(&p, &loc, &opts), RoboOk);
    /* ١٢٣٤٫٥٦ = 1234.56 in arab digits + arab decimal */
    {
        const char *t = "\xD9\xA1\xD9\xA2\xD9\xA3\xD9\xA4\xD9\xAB\xD9\xA5\xD9\xA6";
        RoboCheckEqInt(I18nParseNumber(&p, t, strlen(t), &res), RoboOk);
        RoboCheckEqInt((int)res.value.scale, 2);
        RoboCheck(memcmp(res.value.digits, "123456", 6) == 0);
    }
    I18nNumberParserDestroy(&p);

    ParseTag("fa", &loc);
    I18nParseOptionsInit(&opts);
    memcpy(opts.numberingSystem, "arabext", 8);
    RoboCheckEqInt(I18nNumberParserInit(&p, &loc, &opts), RoboOk);
    {
        /* ۰۱۲ arabext */
        const char *t = "\xDB\xB1\xDB\xB2\xDB\xB3";
        RoboCheckEqInt(I18nParseNumber(&p, t, strlen(t), &res), RoboOk);
        RoboCheck(memcmp(res.value.digits, "123", 3) == 0);
    }
    I18nNumberParserDestroy(&p);

    ParseTag("hi", &loc);
    I18nParseOptionsInit(&opts);
    memcpy(opts.numberingSystem, "deva", 5);
    RoboCheckEqInt(I18nNumberParserInit(&p, &loc, &opts), RoboOk);
    {
        const char *t = "\xE0\xA5\xA7\xE0\xA5\xA8\xE0\xA5\xA9"; /* १२३ */
        RoboCheckEqInt(I18nParseNumber(&p, t, strlen(t), &res), RoboOk);
        RoboCheck(memcmp(res.value.digits, "123", 3) == 0);
    }
    I18nNumberParserDestroy(&p);

    ParseTag("bn", &loc);
    I18nParseOptionsInit(&opts);
    memcpy(opts.numberingSystem, "beng", 5);
    RoboCheckEqInt(I18nNumberParserInit(&p, &loc, &opts), RoboOk);
    {
        const char *t = "\xE0\xA7\xA7\xE0\xA7\xA8"; /* ১২ */
        RoboCheckEqInt(I18nParseNumber(&p, t, strlen(t), &res), RoboOk);
        RoboCheck(memcmp(res.value.digits, "12", 2) == 0);
    }
    I18nNumberParserDestroy(&p);

    ParseTag("th", &loc);
    I18nParseOptionsInit(&opts);
    memcpy(opts.numberingSystem, "thai", 5);
    RoboCheckEqInt(I18nNumberParserInit(&p, &loc, &opts), RoboOk);
    {
        const char *t = "\xE0\xB9\x91\xE0\xB9\x92\xE0\xB9\x93"; /* ๑๒๓ */
        RoboCheckEqInt(I18nParseNumber(&p, t, strlen(t), &res), RoboOk);
        RoboCheck(memcmp(res.value.digits, "123", 3) == 0);
    }
    I18nNumberParserDestroy(&p);
}

static void TestParsePermissive(void)
{
    ExpectNumber("en-US", RoboI18nParsePermissive, "1234.56", "123456", 2, 0);
    ExpectNumber("en-US", RoboI18nParsePermissive, "1,23,456", "123456", 0, 0);
}

static void TestParseRoundtrip(void)
{
    const char *tags[] = {"en-US", "de-DE", "fr-FR", "ja-JP", "zh-CN", "ko-KR", "pl-PL",
                          "ru-RU"};
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        I18nLocale loc;
        I18nNumberFormatter fmt;
        I18nNumberParser parser;
        I18nParseOptions popts;
        I18nNumberOptions fopts;
        I18nDecimal dec;
        I18nParseResult res;
        char buf[128];
        size_t len = 0;

        ParseTag(tags[i], &loc);
        I18nNumberOptionsInit(&fopts);
        fopts.minFractionDigits = 2;
        fopts.maxFractionDigits = 2;
        RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &fopts), RoboOk);
        RoboCheckEqInt(I18nDecimalFromI64(123456, &dec), RoboOk);
        /* 1234.56 */
        dec.digitCount = 6;
        memcpy(dec.digits, "123456", 6);
        dec.scale = 2;
        RoboCheckEqInt(I18nNumberFormat(&fmt, &dec, buf, sizeof(buf), &len),
                          RoboOk);

        I18nParseOptionsInit(&popts);
        popts.mode = RoboI18nParseStrict;
        RoboCheckEqInt(I18nNumberParserInit(&parser, &loc, &popts), RoboOk);
        RoboCheckEqInt(I18nParseNumber(&parser, buf, len, &res), RoboOk);
        RoboCheckEqInt((int)res.value.scale, 2);
        RoboCheck(memcmp(res.value.digits, "123456", 6) == 0);
        I18nNumberParserDestroy(&parser);
        I18nNumberFormatterDestroy(&fmt);
    }
}

static void TestParseCurrencyBasic(void)
{
    I18nLocale loc;
    I18nCurrencyParser p;
    I18nCurrencyParseOptions opts;
    I18nParseResult res;

    ParseTag("en-US", &loc);
    I18nCurrencyParseOptionsInit(&opts);
    opts.number.mode = RoboI18nParseStrict;
    memcpy(opts.requireCurrency, "USD", 4);
    RoboCheckEqInt(I18nCurrencyParserInit(&p, &loc, &opts), RoboOk);

    RoboCheckEqInt(I18nParseCurrency(&p, "$1,234.56", 9, &res), RoboOk);
    RoboCheckStrEq(res.currency, "USD");
    RoboCheck(memcmp(res.value.digits, "123456", 6) == 0);
    RoboCheckEqInt((int)res.value.scale, 2);

    RoboCheckEqInt(I18nParseCurrency(&p, "USD 10.00", 9, &res), RoboOk);
    RoboCheckStrEq(res.currency, "USD");

    RoboCheckEqInt(I18nParseCurrency(&p, "($12.00)", 8, &res), RoboOk);
    RoboCheck(res.value.negative);
    RoboCheck(memcmp(res.value.digits, "1200", 4) == 0);

    I18nCurrencyParserDestroy(&p);
}

static void TestParseCurrencyLocales(void)
{
    I18nLocale loc;
    I18nCurrencyParser p;
    I18nCurrencyParseOptions opts;
    I18nParseResult res;

    ParseTag("de-DE", &loc);
    I18nCurrencyParseOptionsInit(&opts);
    memcpy(opts.requireCurrency, "EUR", 4);
    RoboCheckEqInt(I18nCurrencyParserInit(&p, &loc, &opts), RoboOk);
    {
        const char *text = "1.234,56\xC2\xA0\xE2\x82\xAC";
        RoboCheckEqInt(I18nParseCurrency(&p, text, strlen(text), &res), RoboOk);
    }
    RoboCheckStrEq(res.currency, "EUR");
    I18nCurrencyParserDestroy(&p);

    ParseTag("en-GB", &loc);
    I18nCurrencyParseOptionsInit(&opts);
    memcpy(opts.requireCurrency, "GBP", 4);
    RoboCheckEqInt(I18nCurrencyParserInit(&p, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nParseCurrency(&p, "\xC2\xA3" "10.00", 7, &res), RoboOk);
    RoboCheckStrEq(res.currency, "GBP");
    I18nCurrencyParserDestroy(&p);

    ParseTag("ja-JP", &loc);
    I18nCurrencyParseOptionsInit(&opts);
    memcpy(opts.requireCurrency, "JPY", 4);
    RoboCheckEqInt(I18nCurrencyParserInit(&p, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nParseCurrency(&p, "JPY 1000", 8, &res), RoboOk);
    RoboCheckStrEq(res.currency, "JPY");
    I18nCurrencyParserDestroy(&p);
}

static void TestParseCurrencyAmbiguous(void)
{
    I18nLocale loc;
    I18nCurrencyParser p;
    I18nCurrencyParseOptions opts;
    I18nParseResult res;

    ParseTag("en-US", &loc);
    I18nCurrencyParseOptionsInit(&opts);
    opts.number.mode = RoboI18nParseStrict;
    /* No requireCurrency — bare $ may be ambiguous across USD/CAD/... */
    RoboCheckEqInt(I18nCurrencyParserInit(&p, &loc, &opts), RoboOk);
    {
        Result r = I18nParseCurrency(&p, "$5.00", 5, &res);
        /* Either unique USD in display data, or ambiguous — both OK if deterministic */
        if (r == RoboParseError) {
            RoboCheckEqInt((int)res.error, (int)RoboI18nParseErrAmbiguousCurrency);
            RoboCheck(res.ambiguousCount >= 1u);
        } else {
            RoboCheckEqInt(r, RoboOk);
            RoboCheck(res.currency[0] != '\0');
        }
    }
    I18nCurrencyParserDestroy(&p);

    /* Permissive resolves to locale default when among matches */
    I18nCurrencyParseOptionsInit(&opts);
    opts.number.mode = RoboI18nParsePermissive;
    RoboCheckEqInt(I18nCurrencyParserInit(&p, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nParseCurrency(&p, "$5.00", 5, &res), RoboOk);
    RoboCheckStrEq(res.currency, "USD");
    I18nCurrencyParserDestroy(&p);
}

static void TestParseCurrencyRoundtrip(void)
{
    I18nLocale loc;
    I18nCurrencyFormatter fmt;
    I18nCurrencyOptions fopts;
    I18nCurrencyParser parser;
    I18nCurrencyParseOptions popts;
    I18nParseResult res;
    char buf[128];
    size_t len = 0;

    ParseTag("en-US", &loc);
    I18nCurrencyOptionsInit(&fopts);
    memcpy(fopts.currency, "USD", 4);
    RoboCheckEqInt(I18nCurrencyFormatterInit(&fmt, &loc, &fopts), RoboOk);
    RoboCheckEqInt(I18nCurrencyFormatMinor(&fmt, 12345, buf, sizeof(buf), &len),
                      RoboOk);

    I18nCurrencyParseOptionsInit(&popts);
    memcpy(popts.requireCurrency, "USD", 4);
    RoboCheckEqInt(I18nCurrencyParserInit(&parser, &loc, &popts), RoboOk);
    RoboCheckEqInt(I18nParseCurrency(&parser, buf, len, &res), RoboOk);
    RoboCheckStrEq(res.currency, "USD");
    RoboCheck(memcmp(res.value.digits, "12345", 5) == 0);
    RoboCheckEqInt((int)res.value.scale, 2);
    I18nCurrencyParserDestroy(&parser);
    I18nCurrencyFormatterDestroy(&fmt);
}

static void TestParseRobust(void)
{
    I18nLocale loc;
    I18nNumberParser p;
    I18nParseOptions opts;
    I18nParseResult res;
    char longbuf[5000];
    size_t i;

    ParseTag("en", &loc);
    I18nParseOptionsInit(&opts);
    RoboCheckEqInt(I18nNumberParserInit(&p, &loc, &opts), RoboOk);

    RoboCheckEqInt(I18nParseNumber(&p, "", 0, &res), RoboParseError);
    RoboCheckEqInt((int)res.error, (int)RoboI18nParseErrEmpty);

    /* malformed UTF-8 */
    {
        char bad[2] = {(char)0xC3, (char)0x28};
        RoboCheckEqInt(I18nParseNumber(&p, bad, 2, &res), RoboParseError);
        RoboCheckEqInt((int)res.error, (int)RoboI18nParseErrInvalidUtf8);
    }

    memset(longbuf, '1', sizeof(longbuf));
    RoboCheckEqInt(I18nParseNumber(&p, longbuf, sizeof(longbuf), &res),
                      RoboInvalidArgument);

    for (i = 0; i < 64u; i++) {
        char junk[8];
        junk[0] = (char)(i + 1);
        junk[1] = ',';
        junk[2] = '.';
        junk[3] = '$';
        junk[4] = 0;
        (void)I18nParseNumber(&p, junk, 4, &res);
    }
    I18nNumberParserDestroy(&p);
}

void TestRegisterI18nParse(void)
{
    TestRegister("i18n.parse.versions", TestParseVersions);
    TestRegister("i18n.parse.en", TestParseEnBasic);
    TestRegister("i18n.parse.de_fr", TestParseDeFr);
    TestRegister("i18n.parse.hi", TestParseHiGrouping);
    TestRegister("i18n.parse.digits", TestParseLocalizedDigits);
    TestRegister("i18n.parse.permissive", TestParsePermissive);
    TestRegister("i18n.parse.roundtrip", TestParseRoundtrip);
    TestRegister("i18n.parse.currency", TestParseCurrencyBasic);
    TestRegister("i18n.parse.currency_locales", TestParseCurrencyLocales);
    TestRegister("i18n.parse.currency_ambig", TestParseCurrencyAmbiguous);
    TestRegister("i18n.parse.currency_roundtrip", TestParseCurrencyRoundtrip);
    TestRegister("i18n.parse.robust", TestParseRobust);
}
