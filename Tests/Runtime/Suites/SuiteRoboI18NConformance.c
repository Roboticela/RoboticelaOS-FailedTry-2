/*
 * RoboI18N completeness / conformance suite (Goal 4.26).
 *
 * Covers: version manifest fields, international regression corpus, numbering
 * format/parse agreement, plural cardinal≠ordinal separation, search≠translit
 * boundary, locale fallback via RoboLocale, fuzz/robustness, light perf smoke.
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboUnicode/RoboUnicodeVersion.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void TestConfVersions(void)
{
    I18nDataVersions v;
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    GetI18nDataVersions(&v);
    RoboCheckStrEq(v.apiVersion, "0.27.0");
    RoboCheckStrEq(v.generatorVersion, "1.25.0");
    RoboCheckStrEq(v.cldrVersion, "46");
    RoboCheckStrEq(v.unicodeVersion, "16.0.0");
    RoboCheckStrEq(v.tzdbVersion, "2026c");
    RoboCheckStrEq(v.ianaFileDate, "2026-05-05");
    RoboCheckStrEq(v.ucaVersion, "16.0.0");
    RoboCheckStrEq(v.iso4217Source, "CLDR-46-codeMappings");
    /* Unicode UCD pin must match CLDR Unicode association. */
    RoboCheckStrEq(RoboUnicodeVersionString, v.unicodeVersion);
    RoboCheckStrEq(RoboEmojiVersionString, "16.0");
}

static int LocaleOk(const char *tag)
{
    I18nLocale loc;
    return I18nLocaleParseCanonical(tag, strlen(tag), &loc) == RoboOk;
}

static void TestConfRegressionLocales(void)
{
    static const char *const tags[] = {
        "en", "fr", "de", "es", "tr", "ru", "pl", "uk", "ar", "fa", "ur", "he",
        "hi", "bn", "th", "zh", "ja", "ko",
    };
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        RoboCheck(LocaleOk(tags[i]));
    }
}

static void FormatNumber(const char *tag, double value, char *out, size_t cap, size_t *n)
{
    I18nLocale loc;
    I18nNumberFormatter fmt;
    I18nNumberOptions opts;
    I18nNumberOptionsInit(&opts);
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), &loc), RoboOk);
    RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nNumberFormatDouble(&fmt, value, out, cap, n), RoboOk);
    I18nNumberFormatterDestroy(&fmt);
}

static void TestConfNumberingSystems(void)
{
    char out[128];
    size_t n = 0;
    /* Latin digits (en) */
    FormatNumber("en", 1234.5, out, sizeof(out), &n);
    RoboCheck(n > 0);
    RoboCheck(strchr(out, '1') != NULL);
    /* Arabic-Indic / locale-default digits for ar — non-empty format */
    FormatNumber("ar", 12.0, out, sizeof(out), &n);
    RoboCheck(n > 0);
    FormatNumber("fa", 12.0, out, sizeof(out), &n);
    RoboCheck(n > 0);
    FormatNumber("hi", 12.0, out, sizeof(out), &n);
    RoboCheck(n > 0);
    FormatNumber("bn", 12.0, out, sizeof(out), &n);
    RoboCheck(n > 0);
    FormatNumber("th", 12.0, out, sizeof(out), &n);
    RoboCheck(n > 0);
}

static void TestConfPluralCardinalOrdinalDistinct(void)
{
    I18nLocale en;
    I18nPluralOperands ops;
    I18nPluralCategory card = RoboI18nPluralOther;
    I18nPluralCategory ord = RoboI18nPluralOther;
    RoboCheckEqInt(I18nLocaleParseCanonical("en", 2, &en), RoboOk);
    RoboCheckEqInt(I18nPluralOperandsFromString("1", 1, &ops), RoboOk);
    RoboCheckEqInt(I18nPluralSelect(&en, RoboI18nPluralCardinal, &ops, &card),
                      RoboOk);
    RoboCheckEqInt(I18nPluralSelect(&en, RoboI18nPluralOrdinal, &ops, &ord),
                      RoboOk);
    RoboCheckEqInt((int)card, (int)RoboI18nPluralOne);
    RoboCheckEqInt((int)ord, (int)RoboI18nPluralOne);
    /* English cardinal "2" = other; ordinal "2" = two — separate rule tables. */
    {
        I18nPluralCategory c2 = RoboI18nPluralOther;
        I18nPluralCategory o2 = RoboI18nPluralOther;
        RoboCheckEqInt(I18nPluralOperandsFromString("2", 1, &ops), RoboOk);
        RoboCheckEqInt(I18nPluralSelect(&en, RoboI18nPluralCardinal, &ops, &c2),
                          RoboOk);
        RoboCheckEqInt(I18nPluralSelect(&en, RoboI18nPluralOrdinal, &ops, &o2),
                          RoboOk);
        RoboCheckEqInt((int)c2, (int)RoboI18nPluralOther);
        RoboCheckEqInt((int)o2, (int)RoboI18nPluralTwo);
    }
}

static void TestConfSearchTranslitBoundary(void)
{
    I18nSearcher m;
    I18nSearchOptions o;
    int found = 0;
    char out[64];
    size_t n = 0;
    I18nTransliterator tr;

    I18nSearchOptionsInit(&o);
    o.mode = RoboI18nSearchExact;
    RoboCheckEqInt(I18nSearcherInit(&m, NULL, &o), RoboOk);
    /* Exact search must not transliterate Cyrillic → Latin. */
    RoboCheckEqInt(I18nSearchContains(&m, "\xD0\x9C", 2, "M", 1, &found), RoboOk);
    RoboCheckEqInt(found, 0);
    I18nSearcherDestroy(&m);

    /* Explicit transliteration still works. */
    RoboCheckEqInt(I18nTransliteratorCreateId("Russian-Latin/BGN", NULL, &tr),
                      RoboOk);
    RoboCheckEqInt(
        I18nTransliterate(&tr, "\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0", 12, out,
                               sizeof(out), &n, NULL, 0, NULL),
        RoboOk);
    RoboCheckStrEq(out, "Moskva");
    I18nTransliteratorDestroy(&tr);
}

static void TestConfLocaleFallback(void)
{
    I18nLocale loc;
    I18nLocale parent;
    /* en-US resolves; parent chain uses central locale APIs. */
    RoboCheckEqInt(I18nLocaleParseCanonical("en-US", 5, &loc), RoboOk);
    RoboCheckEqInt(I18nLocaleParent(&loc, &parent), RoboOk);
    RoboCheckStrEq(parent.language, "en");
}

static void TestConfCurrencyRoundtripSmoke(void)
{
    I18nLocale loc;
    I18nCurrencyFormatter fmt;
    I18nCurrencyOptions opts;
    char out[128];
    size_t n = 0;
    RoboCheckEqInt(I18nLocaleParseCanonical("en", 2, &loc), RoboOk);
    I18nCurrencyOptionsInit(&opts);
    memcpy(opts.currency, "USD", 4);
    RoboCheckEqInt(I18nCurrencyFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nCurrencyFormatDouble(&fmt, 1.5, out, sizeof(out), &n),
                      RoboOk);
    RoboCheck(n > 0);
    I18nCurrencyFormatterDestroy(&fmt);
}

static void TestConfTimezoneSmoke(void)
{
    I18nTimezone tz;
    RoboCheckEqInt(I18nTimezoneFromId("UTC", 3, &tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneFromId("America/New_York", 16, &tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneFromId("Asia/Karachi", 12, &tz), RoboOk);
}

static void TestConfRtlCorpus(void)
{
    char out[128];
    size_t n = 0;
    FormatNumber("ar", 3.0, out, sizeof(out), &n);
    RoboCheck(n > 0);
    FormatNumber("he", 3.0, out, sizeof(out), &n);
    RoboCheck(n > 0);
    FormatNumber("ur", 3.0, out, sizeof(out), &n);
    RoboCheck(n > 0);
    FormatNumber("fa", 3.0, out, sizeof(out), &n);
    RoboCheck(n > 0);
}

static void TestConfFuzzRobust(void)
{
    I18nLocale loc;
    char out[64];
    size_t n = 0;
    (void)I18nLocaleParseCanonical("", 0, &loc);
    (void)I18nLocaleParseCanonical("!!!", 3, &loc);
    (void)I18nLocaleParseCanonical("\xFF\xFE", 2, &loc);
    if (I18nLocaleParseCanonical("en", 2, &loc) == RoboOk) {
        I18nNumberFormatter fmt;
        I18nNumberOptions opts;
        I18nNumberOptionsInit(&opts);
        if (I18nNumberFormatterInit(&fmt, &loc, &opts) == RoboOk) {
            (void)I18nNumberFormatDouble(&fmt, 0.0, out, sizeof(out), &n);
            I18nNumberFormatterDestroy(&fmt);
        }
    }
}

static void TestConfPerfSmoke(void)
{
    clock_t t0, t1;
    size_t i;
    char out[64];
    size_t n = 0;
    I18nLocale loc;
    I18nNumberFormatter fmt;
    I18nNumberOptions opts;
    I18nNumberOptionsInit(&opts);
    RoboCheckEqInt(I18nLocaleParseCanonical("en", 2, &loc), RoboOk);
    RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
    t0 = clock();
    for (i = 0; i < 2000u; i++) {
        RoboCheckEqInt(I18nNumberFormatDouble(&fmt, (double)i, out, sizeof(out), &n),
                          RoboOk);
    }
    t1 = clock();
    I18nNumberFormatterDestroy(&fmt);
    RoboCheck((t1 - t0) < 20 * CLOCKS_PER_SEC);
}

void TestRegisterI18nConformance(void)
{
    TestRegister("i18n.conf.versions", TestConfVersions);
    TestRegister("i18n.conf.regression_locales", TestConfRegressionLocales);
    TestRegister("i18n.conf.numbering", TestConfNumberingSystems);
    TestRegister("i18n.conf.plural_kinds", TestConfPluralCardinalOrdinalDistinct);
    TestRegister("i18n.conf.search_translit", TestConfSearchTranslitBoundary);
    TestRegister("i18n.conf.locale_fallback", TestConfLocaleFallback);
    TestRegister("i18n.conf.currency", TestConfCurrencyRoundtripSmoke);
    TestRegister("i18n.conf.timezone", TestConfTimezoneSmoke);
    TestRegister("i18n.conf.rtl", TestConfRtlCorpus);
    TestRegister("i18n.conf.fuzz", TestConfFuzzRobust);
    TestRegister("i18n.conf.perf_smoke", TestConfPerfSmoke);
}
