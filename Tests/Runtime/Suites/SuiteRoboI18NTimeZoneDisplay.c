/*
 * RoboI18N timezone display names / GMT / formatter integration (Goal 4.15).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NDateTimeFormat.h"
#include "RoboI18N/RoboI18NDisplayNames.h"
#include "RoboI18N/RoboDispData.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void MakeUtcInstant(int y, int mo, int d, int h, int mi, I18nInstant *out)
{
    I18nDatetime utc;
    RoboCheckEqInt(I18nDateInit(&utc.date, y, mo, d), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&utc.time, h, mi, 0, 0), RoboOk);
    RoboCheckEqInt(I18nInstantFromUtc(&utc, out), RoboOk);
}

static void TestTzdispVersion(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nDisplayCldrVersionString(), "46");
    RoboCheck(DispTzMetazoneCount > 100u);
    RoboCheck(DispIanaMzRangeCount > 100u);
    RoboCheck(DispTzExemplarCount > 100u);
}

static void TestTzdispLongStdDst(void)
{
    I18nLocale en;
    I18nTimezone tz;
    I18nInstant winter, summer;
    char buf[128];
    size_t n = 0;

    ParseTag("en", &en);
    RoboCheckEqInt(I18nTimezoneFromId("America/New_York", 16, &tz), RoboOk);
    MakeUtcInstant(2024, 1, 15, 17, 0, &winter);
    MakeUtcInstant(2024, 7, 15, 16, 0, &summer);

    RoboCheckEqInt(I18nTimezoneDisplayNameAt(
                          &tz, &winter, RoboI18nTzDispSpecificLong, &en, NULL, buf,
                          sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "Eastern Standard Time") != NULL);

    RoboCheckEqInt(I18nTimezoneDisplayNameAt(
                          &tz, &summer, RoboI18nTzDispSpecificLong, &en, NULL, buf,
                          sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "Eastern Daylight Time") != NULL);

    RoboCheckEqInt(I18nTimezoneDisplayNameAt(
                          &tz, &summer, RoboI18nTzDispLongGeneric, &en, NULL, buf,
                          sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "Eastern Time") != NULL);

    RoboCheckEqInt(I18nTimezoneDisplayNameAt(
                          &tz, &winter, RoboI18nTzDispSpecificShort, &en, NULL, buf,
                          sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "EST") != NULL);
}

static void TestTzdispExemplarAlias(void)
{
    I18nLocale en;
    I18nTimezone tz;
    char buf[128];
    size_t n = 0;
    ParseTag("en", &en);

    RoboCheckEqInt(I18nTimezoneFromId("Asia/Karachi", 12, &tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneDisplayName(
                          &tz, RoboI18nTzDispExemplarCity, &en, NULL, buf, sizeof(buf), &n),
                      RoboOk);
    /* en CLDR often omits common English exemplars; structural fallback → Karachi. */
    RoboCheck(strcmp(buf, "Karachi") == 0 || strstr(buf, "Karachi") != NULL);

    RoboCheckEqInt(I18nTimezoneFromId("US/Eastern", 10, &tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneDisplayName(
                          &tz, RoboI18nTzDispExemplarCity, &en, NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "New York");
    /* Localized exemplar where CLDR provides one (ja). */
    {
        I18nLocale ja;
        ParseTag("ja", &ja);
        RoboCheckEqInt(I18nTimezoneFromId("America/New_York", 16, &tz), RoboOk);
        RoboCheckEqInt(I18nTimezoneDisplayName(
                              &tz, RoboI18nTzDispExemplarCity, &ja, NULL, buf, sizeof(buf),
                              &n),
                          RoboOk);
        RoboCheck(n > 0);
        RoboCheck(strcmp(buf, "New_York") != 0);
    }
    RoboCheckEqInt(I18nTimezoneFromId("US/Eastern", 10, &tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneDisplayName(
                          &tz, RoboI18nTzDispIdentifier, &en, NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "US/Eastern");
}

static void TestTzdispGmtIana(void)
{
    I18nLocale en, ar;
    I18nTimezone tz;
    I18nInstant inst;
    char buf[128];
    size_t n = 0;

    ParseTag("en", &en);
    ParseTag("ar", &ar);
    RoboCheckEqInt(I18nTimezoneFromId("Asia/Karachi", 12, &tz), RoboOk);
    MakeUtcInstant(2024, 6, 1, 0, 0, &inst);

    RoboCheckEqInt(I18nTimezoneDisplayNameAt(
                          &tz, &inst, RoboI18nTzDispGmt, &en, NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "GMT") != NULL);
    RoboCheck(strstr(buf, "05") != NULL);

    RoboCheckEqInt(I18nTimezoneDisplayName(&tz, RoboI18nTzDispGmt, &en, NULL, buf,
                                                      sizeof(buf), &n),
                      RoboInvalidArgument);

    RoboCheckEqInt(I18nTimezoneUtc(&tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneDisplayName(&tz, RoboI18nTzDispGmt, &en, NULL, buf,
                                                      sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "GMT");

    RoboCheckEqInt(I18nTimezoneDisplayNameAt(
                          &tz, &inst, RoboI18nTzDispGmt, &ar, NULL, buf, sizeof(buf), &n),
                      RoboOk);
    /* Arabic gmtZeroFormat — contains Arabic letters, not necessarily ASCII GMT. */
    RoboCheck(n > 0);
}

static void TestTzdispIntlLocales(void)
{
    static const char *tags[] = {
        "en", "ar", "ur", "fa", "he", "hi", "bn", "th", "zh", "ja", "ko", "ru", "de", "fr"
    };
    static const char *zones[] = {
        "UTC", "Asia/Karachi", "Asia/Kolkata", "Asia/Tokyo", "Asia/Shanghai", "Asia/Seoul",
        "Europe/London", "Europe/Paris", "America/New_York", "America/Los_Angeles",
        "Australia/Sydney", "Africa/Cairo"
    };
    size_t ti, zi;
    I18nInstant inst;
    MakeUtcInstant(2024, 6, 15, 12, 0, &inst);

    for (ti = 0; ti < sizeof(tags) / sizeof(tags[0]); ti++) {
        I18nLocale loc;
        ParseTag(tags[ti], &loc);
        for (zi = 0; zi < sizeof(zones) / sizeof(zones[0]); zi++) {
            I18nTimezone tz;
            char buf[192];
            size_t n = 0;
            RoboCheckEqInt(
                I18nTimezoneFromId(zones[zi], strlen(zones[zi]), &tz), RoboOk);
            RoboCheckEqInt(I18nTimezoneDisplayNameAt(
                                  &tz, &inst, RoboI18nTzDispLongGeneric, &loc, NULL, buf,
                                  sizeof(buf), &n),
                              RoboOk);
            RoboCheck(n > 0);
            RoboCheckEqInt(I18nTimezoneDisplayNameAt(
                                  &tz, &inst, RoboI18nTzDispGmt, &loc, NULL, buf, sizeof(buf),
                                  &n),
                              RoboOk);
            RoboCheck(n > 0);
        }
    }
}

static void TestTzdispFormatter(void)
{
    I18nLocale en;
    I18nTimezone tz;
    I18nDatetimeFormatter fmt;
    I18nInstant winter;
    char buf[192];
    size_t n = 0;

    ParseTag("en", &en);
    RoboCheckEqInt(I18nTimezoneFromId("America/New_York", 16, &tz), RoboOk);
    MakeUtcInstant(2024, 1, 15, 17, 0, &winter);
    RoboCheckEqInt(
        I18nDatetimeFormatterInitPattern(&fmt, &en, NULL, &tz, "zzzz", NULL), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatInstant(&fmt, &winter, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "Eastern Standard Time") != NULL);
    I18nDatetimeFormatterDestroy(&fmt);

    RoboCheckEqInt(
        I18nDatetimeFormatterInitPattern(&fmt, &en, NULL, &tz, "VVVV", NULL), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatInstant(&fmt, &winter, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "New York") != NULL);
    I18nDatetimeFormatterDestroy(&fmt);
}

static void TestTzdispMetazoneHistory(void)
{
    /* Europe/London used British metazone before 1971-10-31; then GMT. */
    I18nLocale en;
    I18nTimezone tz;
    I18nInstant early, late;
    char bufA[128], bufB[128];
    size_t na = 0, nb = 0;

    ParseTag("en", &en);
    RoboCheckEqInt(I18nTimezoneFromId("Europe/London", 13, &tz), RoboOk);
    MakeUtcInstant(1960, 6, 1, 12, 0, &early);
    MakeUtcInstant(2020, 6, 1, 12, 0, &late);
    RoboCheckEqInt(I18nTimezoneDisplayNameAt(
                          &tz, &early, RoboI18nTzDispLongGeneric, &en, NULL, bufA,
                          sizeof(bufA), &na),
                      RoboOk);
    RoboCheckEqInt(I18nTimezoneDisplayNameAt(
                          &tz, &late, RoboI18nTzDispLongGeneric, &en, NULL, bufB,
                          sizeof(bufB), &nb),
                      RoboOk);
    RoboCheck(na > 0 && nb > 0);
    /* Names may differ across metazone eras; both must be non-empty UTF-8. */
}

static void TestTzdispFuzz(void)
{
    uint32_t seed = 0x415u;
    int i;
    for (i = 0; i < 64; i++) {
        char id[40];
        size_t len = (seed % 24u) + 1u;
        size_t j;
        I18nTimezone tz;
        I18nLocale en;
        I18nInstant inst;
        char buf[128];
        size_t n = 0;
        ParseTag("en", &en);
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz/_-";
            id[j] = alphabet[seed % (sizeof(alphabet) - 1u)];
            seed = seed * 1664525u + 1013904223u;
        }
        inst.unixMs = (int64_t)(int32_t)seed * 1000;
        if (I18nTimezoneFromId(id, len, &tz) == RoboOk) {
            (void)I18nTimezoneDisplayNameAt(&tz, &inst, RoboI18nTzDispLongGeneric,
                                                     &en, NULL, buf, sizeof(buf), &n);
            (void)I18nTimezoneDisplayNameAt(&tz, &inst, RoboI18nTzDispGmt, &en, NULL,
                                                     buf, sizeof(buf), &n);
        }
    }
    RoboCheck(1);
}

void TestRegisterI18nTimezoneDisplay(void)
{
    TestRegister("i18n.tzdisp.version", TestTzdispVersion);
    TestRegister("i18n.tzdisp.std_dst", TestTzdispLongStdDst);
    TestRegister("i18n.tzdisp.exemplar_alias", TestTzdispExemplarAlias);
    TestRegister("i18n.tzdisp.gmt", TestTzdispGmtIana);
    TestRegister("i18n.tzdisp.intl", TestTzdispIntlLocales);
    TestRegister("i18n.tzdisp.formatter", TestTzdispFormatter);
    TestRegister("i18n.tzdisp.metazone_hist", TestTzdispMetazoneHistory);
    TestRegister("i18n.tzdisp.fuzz", TestTzdispFuzz);
}
