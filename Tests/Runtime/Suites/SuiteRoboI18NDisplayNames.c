/*
 * RoboI18N locale display names (Goal 4.10).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboDispData.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void TestDispVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nDisplayCldrVersionString(), "46");
    RoboCheck(DispLanguageCount > 1000u);
    RoboCheck(DispScriptCount > 100u);
    RoboCheck(DispTerritoryCount > 100u);
    RoboCheck(DispCurrencyCount > 100u);
}

static void ExpectName(Result (*fn)(const char *, size_t, const I18nLocale *,
                                            const I18nDisplayOptions *, char *, size_t,
                                            size_t *),
                        const char *id,
                        const char *dispTag,
                        const char *expectSub)
{
    I18nLocale disp;
    char buf[256];
    size_t n = 0;
    ParseTag(dispTag, &disp);
    RoboCheckEqInt(fn(id, strlen(id), &disp, NULL, buf, sizeof(buf), &n), RoboOk);
    RoboCheck(strstr(buf, expectSub) != NULL);
}

static void TestDispLanguages(void)
{
    I18nLocale fr;
    char buf[128];
    size_t n = 0;
    ParseTag("fr", &fr);
    RoboCheckEqInt(
        I18nLanguageDisplayName("en", 2, &fr, NULL, buf, sizeof(buf), &n), RoboOk);
    RoboCheckStrEq(buf, "anglais");

    ExpectName(I18nLanguageDisplayName, "ur", "en", "Urdu");
    ExpectName(I18nLanguageDisplayName, "ar", "ur", "عرب");
    ExpectName(I18nLanguageDisplayName, "ja", "ja", "日本");
}

static void TestDispScriptsRegions(void)
{
    ExpectName(I18nScriptDisplayName, "Latn", "en", "Latin");
    ExpectName(I18nScriptDisplayName, "Arab", "en", "Arabic");
    ExpectName(I18nScriptDisplayName, "Deva", "hi", "देव");
    ExpectName(I18nScriptDisplayName, "Hebr", "he", "עבר");
    ExpectName(I18nScriptDisplayName, "Hans", "zh", "简");
    ExpectName(I18nRegionDisplayName, "US", "en", "United States");
    ExpectName(I18nRegionDisplayName, "PK", "ur", "پاکستان");
    ExpectName(I18nRegionDisplayName, "JP", "ja", "日本");
    ExpectName(I18nRegionDisplayName, "SA", "ar", "سعود");
}

static void TestDispCurrency(void)
{
    I18nLocale en;
    char buf[128];
    size_t n = 0;
    ParseTag("en", &en);
    RoboCheckEqInt(I18nCurrencyDisplayName("USD", 3, RoboI18nCurrencyName, &en,
                                                      NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "Dollar") != NULL);
    RoboCheckEqInt(I18nCurrencyDisplayName("USD", 3, RoboI18nCurrencySymbol, &en,
                                                      NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "$");
    RoboCheckEqInt(I18nCurrencyDisplayName("PKR", 3, RoboI18nCurrencyName, &en,
                                                      NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "Rupee") != NULL);
    RoboCheckEqInt(I18nCurrencyDisplayName("EUR", 3, RoboI18nCurrencyNarrowSymbol,
                                                      &en, NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheck(buf[0] != '\0');
}

static void TestDispCalendarNumbering(void)
{
    I18nLocale en;
    I18nCalendar cal;
    char buf[128];
    size_t n = 0;
    ParseTag("en", &en);

    RoboCheckEqInt(I18nCalendarFromId("gregorian", 9, &cal), RoboOk);
    RoboCheckEqInt(
        I18nCalendarDisplayName(&cal, &en, NULL, buf, sizeof(buf), &n), RoboOk);
    RoboCheck(strstr(buf, "Gregorian") != NULL);

    RoboCheckEqInt(I18nCalendarFromId("buddhist", 8, &cal), RoboOk);
    RoboCheckEqInt(
        I18nCalendarDisplayName(&cal, &en, NULL, buf, sizeof(buf), &n), RoboOk);
    RoboCheck(strstr(buf, "Buddhist") != NULL);

    RoboCheckEqInt(I18nCalendarFromId("islamic", 7, &cal), RoboOk);
    RoboCheckEqInt(
        I18nCalendarDisplayName(&cal, &en, NULL, buf, sizeof(buf), &n), RoboOk);
    RoboCheck(strstr(buf, "Islamic") != NULL || strstr(buf, "Hijri") != NULL);

    RoboCheckEqInt(I18nNumberingSystemDisplayName("latn", 4, &en, NULL, buf,
                                                              sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "Latin") != NULL || strstr(buf, "Western") != NULL);
    RoboCheckEqInt(I18nNumberingSystemDisplayName("arab", 4, &en, NULL, buf,
                                                              sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "Arabic") != NULL);
    RoboCheckEqInt(I18nNumberingSystemDisplayName("deva", 4, &en, NULL, buf,
                                                              sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "Devanagari") != NULL);
}

static void TestDispTimezone(void)
{
    I18nLocale en;
    I18nTimezone tz;
    char buf[128];
    size_t n = 0;
    ParseTag("en", &en);

    RoboCheckEqInt(I18nTimezoneFromId("Asia/Karachi", 12, &tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneDisplayName(
                          &tz, RoboI18nTzDispLongStandard, &en, NULL, buf, sizeof(buf), &n),
                      RoboOk);
    /* Metazone Pakistan → "Pakistan Standard Time" or similar */
    RoboCheck(strstr(buf, "Pakistan") != NULL || strstr(buf, "Karachi") != NULL);

    RoboCheckEqInt(I18nTimezoneUtc(&tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneDisplayName(&tz, RoboI18nTzDispGmt, &en, NULL, buf,
                                                      sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "GMT") != NULL);

    RoboCheckEqInt(I18nTimezoneFromId("+05:00", 6, &tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneDisplayName(&tz, RoboI18nTzDispGmt, &en, NULL, buf,
                                                      sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "05") != NULL);
}

static void TestDispLocaleCompose(void)
{
    I18nLocale described;
    I18nLocale en;
    char buf[256];
    size_t n = 0;
    ParseTag("en", &en);
    ParseTag("en-US", &described);
    RoboCheckEqInt(
        I18nLocaleDisplayName(&described, &en, NULL, buf, sizeof(buf), &n), RoboOk);
    RoboCheck(strstr(buf, "English") != NULL);
    RoboCheck(strstr(buf, "United States") != NULL);

    ParseTag("ur-PK", &described);
    RoboCheckEqInt(
        I18nLocaleDisplayName(&described, &en, NULL, buf, sizeof(buf), &n), RoboOk);
    RoboCheck(strstr(buf, "Urdu") != NULL);
    RoboCheck(strstr(buf, "Pakistan") != NULL);

    ParseTag("zh-Hans", &described);
    RoboCheckEqInt(
        I18nLocaleDisplayName(&described, &en, NULL, buf, sizeof(buf), &n), RoboOk);
    RoboCheck(strstr(buf, "Chinese") != NULL);
}

static void TestDispFallback(void)
{
    I18nLocale en;
    I18nDisplayOptions opt;
    char buf[64];
    size_t n = 0;
    ParseTag("en", &en);
    I18nDisplayOptionsInit(&opt);
    opt.fallback = RoboI18nDispFallbackCode;
    RoboCheckEqInt(I18nLanguageDisplayName("zzxnotalang", 11, &en, &opt, buf,
                                                      sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "zzxnotalang");

    opt.fallback = RoboI18nDispFallbackNotFound;
    RoboCheckEqInt(I18nLanguageDisplayName("zzxnotalang", 11, &en, &opt, buf,
                                                      sizeof(buf), &n),
                      RoboNotFound);
}

static void TestDispLocalesSmoke(void)
{
    static const char *tags[] = {
        "en", "ur", "ar", "fa", "he", "hi", "bn", "th", "zh", "ja", "ko", "de", "fr", "es", "tr"
    };
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        I18nLocale disp;
        char buf[128];
        size_t n = 0;
        ParseTag(tags[i], &disp);
        RoboCheckEqInt(
            I18nLanguageDisplayName("en", 2, &disp, NULL, buf, sizeof(buf), &n),
            RoboOk);
        RoboCheck(buf[0] != '\0');
        RoboCheckEqInt(
            I18nRegionDisplayName("US", 2, &disp, NULL, buf, sizeof(buf), &n),
            RoboOk);
        RoboCheck(buf[0] != '\0');
    }
}

static void TestDispFuzz(void)
{
    I18nLocale en;
    char buf[64];
    size_t n = 0;
    uint32_t seed = 0xD150u;
    int i;
    ParseTag("en", &en);
    for (i = 0; i < 40; i++) {
        char id[8];
        size_t len = (seed % 5u) + 1u;
        size_t j;
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            id[j] = (char)('a' + ((seed >> j) % 26u));
            seed = seed * 1664525u + 1013904223u;
        }
        (void)I18nLanguageDisplayName(id, len, &en, NULL, buf, sizeof(buf), &n);
        (void)I18nScriptDisplayName("Xyza", 4, &en, NULL, buf, sizeof(buf), &n);
        (void)I18nRegionDisplayName("ZZ", 2, &en, NULL, buf, sizeof(buf), &n);
        (void)I18nCurrencyDisplayName("XXX", 3, RoboI18nCurrencyName, &en, NULL, buf,
                                              sizeof(buf), &n);
    }
    RoboCheckEqInt(
        I18nLanguageDisplayName(NULL, 0, &en, NULL, buf, sizeof(buf), &n),
        RoboInvalidArgument);
}

void TestRegisterI18nDisplayNames(void)
{
    TestRegister("i18n.disp.versions", TestDispVersions);
    TestRegister("i18n.disp.languages", TestDispLanguages);
    TestRegister("i18n.disp.scripts_regions", TestDispScriptsRegions);
    TestRegister("i18n.disp.currency", TestDispCurrency);
    TestRegister("i18n.disp.calendar_numbering", TestDispCalendarNumbering);
    TestRegister("i18n.disp.timezone", TestDispTimezone);
    TestRegister("i18n.disp.locale_compose", TestDispLocaleCompose);
    TestRegister("i18n.disp.fallback", TestDispFallback);
    TestRegister("i18n.disp.locales_smoke", TestDispLocalesSmoke);
    TestRegister("i18n.disp.fuzz", TestDispFuzz);
}
