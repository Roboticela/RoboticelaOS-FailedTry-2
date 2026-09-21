/*
 * RoboI18N relative-time suite (Goal 4.7).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboRelData.h"
#include "RoboTest.h"

#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void ExpectRel(const char *tag,
                       int64_t value,
                       I18nRelativeUnit unit,
                       I18nRelativeNumeric numeric,
                       const char *want)
{
    I18nLocale loc;
    I18nRelativeFormatter fmt;
    I18nRelativeOptions opts;
    char buf[128];
    size_t len = 0;

    ParseTag(tag, &loc);
    I18nRelativeOptionsInit(&opts);
    opts.numeric = numeric;
    RoboCheckEqInt(I18nRelativeFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nRelativeTimeFormatUnit(&fmt, value, unit, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, want);
    I18nRelativeFormatterDestroy(&fmt);
}

static void TestRelVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(RoboRelCldrVersion, "46");
    RoboCheck(RelUnitsCount > 100u);
}

static void TestRelEnAuto(void)
{
    ExpectRel("en", -1, RoboI18nRelDay, RoboI18nRelNumericAuto, "yesterday");
    ExpectRel("en", 0, RoboI18nRelDay, RoboI18nRelNumericAuto, "today");
    ExpectRel("en", 1, RoboI18nRelDay, RoboI18nRelNumericAuto, "tomorrow");
    ExpectRel("en", -1, RoboI18nRelYear, RoboI18nRelNumericAuto, "last year");
    ExpectRel("en", 1, RoboI18nRelYear, RoboI18nRelNumericAuto, "next year");
}

static void TestRelEnNumeric(void)
{
    ExpectRel("en", -1, RoboI18nRelDay, RoboI18nRelNumericAlways, "1 day ago");
    ExpectRel("en", 1, RoboI18nRelDay, RoboI18nRelNumericAlways, "in 1 day");
    ExpectRel("en", -3, RoboI18nRelDay, RoboI18nRelNumericAlways, "3 days ago");
    ExpectRel("en", 3, RoboI18nRelDay, RoboI18nRelNumericAlways, "in 3 days");
    ExpectRel("en", 0, RoboI18nRelDay, RoboI18nRelNumericAlways, "in 0 days");
    ExpectRel("en-GB", -2, RoboI18nRelWeek, RoboI18nRelNumericAlways, "2 weeks ago");
}

static void TestRelPluralsSlavic(void)
{
    /* Russian: 1 день, 2 дня, 5 дней */
    ExpectRel("ru", -1, RoboI18nRelDay, RoboI18nRelNumericAlways,
               "1 \xD0\xB4\xD0\xB5\xD0\xBD\xD1\x8C \xD0\xBD\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xB4");
    ExpectRel("ru", -2, RoboI18nRelDay, RoboI18nRelNumericAlways,
               "2 \xD0\xB4\xD0\xBD\xD1\x8F \xD0\xBD\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xB4");
    ExpectRel("ru", -5, RoboI18nRelDay, RoboI18nRelNumericAlways,
               "5 \xD0\xB4\xD0\xBD\xD0\xB5\xD0\xB9 \xD0\xBD\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xB4");

    /* Polish: 1 dzień temu, 2 dni temu */
    ExpectRel("pl", -1, RoboI18nRelDay, RoboI18nRelNumericAlways,
               "1 dzie\xC5\x84 temu");
    ExpectRel("pl", -2, RoboI18nRelDay, RoboI18nRelNumericAlways, "2 dni temu");
}

static void TestRelRtlAndAsian(void)
{
    I18nLocale loc;
    I18nRelativeFormatter fmt;
    char buf[128];
    size_t len = 0;
    static const char *tags[] = {"ar", "fa", "he", "ur", "hi", "bn", "th", "zh", "ja", "ko"};
    size_t i;

    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        ParseTag(tags[i], &loc);
        RoboCheckEqInt(I18nRelativeFormatterInit(&fmt, &loc, NULL), RoboOk);
        RoboCheckEqInt(
            I18nRelativeTimeFormatUnit(&fmt, -3, RoboI18nRelDay, buf, sizeof(buf),
                                                &len),
            RoboOk);
        RoboCheck(len > 0);
        RoboCheckEqInt(
            I18nRelativeTimeFormatUnit(&fmt, 1, RoboI18nRelDay, buf, sizeof(buf),
                                                &len),
            RoboOk);
        RoboCheck(len > 0);
        I18nRelativeFormatterDestroy(&fmt);
    }
}

static void TestRelUnitsAndWidths(void)
{
    ExpectRel("en", -2, RoboI18nRelHour, RoboI18nRelNumericAlways, "2 hours ago");
    ExpectRel("en", 4, RoboI18nRelMinute, RoboI18nRelNumericAlways, "in 4 minutes");
    ExpectRel("en", -1, RoboI18nRelSecond, RoboI18nRelNumericAuto, "1 second ago");

    {
        I18nLocale loc;
        I18nRelativeFormatter fmt;
        I18nRelativeOptions opts;
        char buf[64];
        size_t len = 0;
        ParseTag("en", &loc);
        I18nRelativeOptionsInit(&opts);
        opts.width = RoboI18nRelWidthShort;
        opts.numeric = RoboI18nRelNumericAlways;
        RoboCheckEqInt(I18nRelativeFormatterInit(&fmt, &loc, &opts), RoboOk);
        RoboCheckEqInt(
            I18nRelativeTimeFormatUnit(&fmt, -3, RoboI18nRelDay, buf, sizeof(buf),
                                                &len),
            RoboOk);
        RoboCheckStrEq(buf, "3 days ago");
        I18nRelativeFormatterDestroy(&fmt);
    }
}

static void TestRelFuzz(void)
{
    I18nLocale loc;
    I18nRelativeFormatter fmt;
    char buf[96];
    size_t len = 0;
    int64_t v;
    int u;

    ParseTag("en", &loc);
    RoboCheckEqInt(I18nRelativeFormatterInit(&fmt, &loc, NULL), RoboOk);
    for (v = -20; v <= 20; v++) {
        for (u = 0; u <= (int)RoboI18nRelSecond; u++) {
            RoboCheckEqInt(
                I18nRelativeTimeFormatUnit(&fmt, v, (I18nRelativeUnit)u, buf,
                                                    sizeof(buf), &len),
                RoboOk);
        }
    }
    RoboCheck(I18nRelativeTimeFormatUnit(&fmt, 0, (I18nRelativeUnit)99, buf,
                                                   sizeof(buf), &len) != RoboOk);
    I18nRelativeFormatterDestroy(&fmt);
}

void TestRegisterI18nRelative(void)
{
    TestRegister("i18n.relative.versions", TestRelVersions);
    TestRegister("i18n.relative.en_auto", TestRelEnAuto);
    TestRegister("i18n.relative.en_numeric", TestRelEnNumeric);
    TestRegister("i18n.relative.slavic", TestRelPluralsSlavic);
    TestRegister("i18n.relative.rtl_asian", TestRelRtlAndAsian);
    TestRegister("i18n.relative.units_widths", TestRelUnitsAndWidths);
    TestRegister("i18n.relative.fuzz", TestRelFuzz);
}
