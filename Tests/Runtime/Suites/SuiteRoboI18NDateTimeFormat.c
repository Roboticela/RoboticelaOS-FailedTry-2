/*
 * RoboI18N localized date/time formatting suite (Goal 4.6).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NDateTimeFormat.h"
#include "RoboI18N/RoboCalData.h"
#include "RoboTest.h"

#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void ExpectDate(const char *tag,
                        I18nCalFormatStyle style,
                        int y,
                        int m,
                        int d,
                        const char *want)
{
    I18nLocale loc;
    I18nDatetimeFormatter fmt;
    I18nDtfmtOptions opts;
    I18nDate date;
    char buf[192];
    size_t len = 0;

    ParseTag(tag, &loc);
    I18nDtfmtOptionsInit(&opts);
    opts.dateStyle = style;
    RoboCheckEqInt(I18nDatetimeFormatterInit(&fmt, &loc, NULL, NULL, RoboI18nDtfmtDate,
                                                        &opts),
                      RoboOk);
    RoboCheckEqInt(I18nDateInit(&date, y, (uint8_t)m, (uint8_t)d), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatDate(&fmt, &date, buf, sizeof(buf), &len), RoboOk);
    RoboCheckStrEq(buf, want);
    I18nDatetimeFormatterDestroy(&fmt);
}

static void ExpectTime(const char *tag,
                        I18nCalFormatStyle style,
                        int h,
                        int mi,
                        int s,
                        const char *want)
{
    I18nLocale loc;
    I18nDatetimeFormatter fmt;
    I18nDtfmtOptions opts;
    I18nTime time;
    char buf[192];
    size_t len = 0;

    ParseTag(tag, &loc);
    I18nDtfmtOptionsInit(&opts);
    opts.timeStyle = style;
    RoboCheckEqInt(I18nDatetimeFormatterInit(&fmt, &loc, NULL, NULL, RoboI18nDtfmtTime,
                                                        &opts),
                      RoboOk);
    RoboCheckEqInt(I18nTimeInit(&time, (uint8_t)h, (uint8_t)mi, (uint8_t)s, 0), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatTime(&fmt, &time, buf, sizeof(buf), &len), RoboOk);
    RoboCheckStrEq(buf, want);
    I18nDatetimeFormatterDestroy(&fmt);
}

static void TestDtfmtVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nCldrVersionString(), "46");
    RoboCheckStrEq(RoboCalCldrVersion, "46");
    RoboCheck(CalAvailableFormatsCount > 1000u);
}

static void TestEnStyles(void)
{
    /* 2026-09-13 is Sunday */
    ExpectDate("en", RoboI18nCalStyleFull, 2026, 9, 13, "Sunday, September 13, 2026");
    ExpectDate("en", RoboI18nCalStyleLong, 2026, 9, 13, "September 13, 2026");
    ExpectDate("en", RoboI18nCalStyleMedium, 2026, 9, 13, "Sep 13, 2026");
    ExpectDate("en", RoboI18nCalStyleShort, 2026, 9, 13, "9/13/26");
    ExpectDate("en-US", RoboI18nCalStyleShort, 2026, 9, 13, "9/13/26");
    ExpectDate("en-GB", RoboI18nCalStyleShort, 2026, 9, 13, "13/09/2026");
    ExpectDate("en-GB", RoboI18nCalStyleMedium, 2026, 9, 13, "13 Sept 2026");

    /* NNBSP (U+202F) before day period in en */
    ExpectTime("en", RoboI18nCalStyleShort, 15, 45, 0, "3:45\xE2\x80\xAF""PM");
    ExpectTime("en", RoboI18nCalStyleMedium, 15, 45, 30, "3:45:30\xE2\x80\xAF""PM");
    ExpectTime("en-GB", RoboI18nCalStyleShort, 15, 45, 0, "15:45");
    ExpectTime("en-GB", RoboI18nCalStyleMedium, 15, 45, 30, "15:45:30");
}

static void TestEnDatetimeCombo(void)
{
    I18nLocale loc;
    I18nDatetimeFormatter fmt;
    I18nDtfmtOptions opts;
    I18nDatetime dt;
    char buf[256];
    size_t len = 0;

    ParseTag("en", &loc);
    I18nDtfmtOptionsInit(&opts);
    opts.dateStyle = RoboI18nCalStyleMedium;
    opts.timeStyle = RoboI18nCalStyleMedium;
    RoboCheckEqInt(
        I18nDatetimeFormatterInit(&fmt, &loc, NULL, NULL, RoboI18nDtfmtDatetime, &opts),
        RoboOk);
    RoboCheckEqInt(I18nDateInit(&dt.date, 2026, 9, 13), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&dt.time, 15, 45, 30, 0), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatDatetime(&fmt, &dt, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, "Sep 13, 2026, 3:45:30\xE2\x80\xAF""PM");
    I18nDatetimeFormatterDestroy(&fmt);
}

static void TestNonLatinDates(void)
{
    /* Arabic/Persian digits require region locales (CLDR default digits). */
    ExpectDate("ar-SA", RoboI18nCalStyleShort, 2026, 9, 13,
                "\xD9\xA1\xD9\xA3\xE2\x80\x8F/\xD9\xA9\xE2\x80\x8F/\xD9\xA2\xD9\xA0\xD9\xA2\xD9\xA6");
    ExpectDate("ar-SA", RoboI18nCalStyleLong, 2026, 9, 13,
                "\xD9\xA1\xD9\xA3 \xD8\xB3\xD8\xA8\xD8\xAA\xD9\x85\xD8\xA8\xD8\xB1 "
                "\xD9\xA2\xD9\xA0\xD9\xA2\xD9\xA6");

    /* Persian digits */
    ExpectDate("fa-IR", RoboI18nCalStyleShort, 2026, 9, 13,
                "\xDB\xB2\xDB\xB0\xDB\xB2\xDB\xB6/\xDB\xB9/\xDB\xB1\xDB\xB3");

    ExpectDate("ja", RoboI18nCalStyleLong, 2026, 9, 13,
                "2026\xE5\xB9\xB4"
                "9"
                "\xE6\x9C\x88"
                "13"
                "\xE6\x97\xA5");
    ExpectDate("zh", RoboI18nCalStyleShort, 2026, 9, 13, "2026/9/13");
    ExpectDate("ko", RoboI18nCalStyleMedium, 2026, 9, 13, "2026. 9. 13.");
    ExpectDate("hi", RoboI18nCalStyleMedium, 2026, 9, 13,
                "13 \xE0\xA4\xB8\xE0\xA4\xBF\xE0\xA4\xA4\xE0\xA5\xB0 2026");
}

static void TestUrBnHeTh(void)
{
    ExpectDate("ur", RoboI18nCalStyleMedium, 2026, 9, 13,
                "13 \xD8\xB3\xD8\xAA\xD9\x85\xD8\xA8\xD8\xB1\xD8\x8C 2026");
    ExpectDate("bn", RoboI18nCalStyleShort, 2026, 9, 13,
                "\xE0\xA7\xA7\xE0\xA7\xA9/\xE0\xA7\xAF/\xE0\xA7\xA8\xE0\xA7\xAC");
    ExpectDate("he", RoboI18nCalStyleShort, 2026, 9, 13, "13.9.2026");
    ExpectDate("th", RoboI18nCalStyleShort, 2026, 9, 13, "13/9/26");
}

static void TestDayPeriodsAndNames(void)
{
    I18nLocale loc;
    I18nDatetimeFormatter fmt;
    I18nTime time;
    char buf[64];
    size_t len = 0;
    const char *name;

    ParseTag("ja", &loc);
    RoboCheckEqInt(
        I18nDatetimeFormatterInitPattern(&fmt, &loc, NULL, NULL, "a", NULL), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&time, 9, 0, 0, 0), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatTime(&fmt, &time, buf, sizeof(buf), &len), RoboOk);
    RoboCheckStrEq(buf, "\xE5\x8D\x88\xE5\x89\x8D"); /* 午前 */
    RoboCheckEqInt(I18nTimeInit(&time, 15, 0, 0, 0), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatTime(&fmt, &time, buf, sizeof(buf), &len), RoboOk);
    RoboCheckStrEq(buf, "\xE5\x8D\x88\xE5\xBE\x8C"); /* 午後 */
    I18nDatetimeFormatterDestroy(&fmt);

    ParseTag("ar", &loc);
    {
        I18nCalendar greg;
        RoboCheckEqInt(I18nCalendarFromKind(RoboI18nCalGregorian, &greg), RoboOk);
        RoboCheckEqInt(
            I18nCalendarMonthName(&loc, &greg, 9, RoboI18nCalNameNarrow, &name),
            RoboOk);
        RoboCheck(name && name[0]);
        RoboCheckEqInt(
            I18nCalendarWeekdayName(&loc, &greg, RoboI18nWeekdaySun,
                                            RoboI18nCalNameNarrow, &name),
            RoboOk);
        RoboCheck(name && name[0]);
    }
}

static void TestSkeletons(void)
{
    I18nLocale loc;
    const char *pat = NULL;
    I18nDatetimeFormatter fmt;
    I18nDate date;
    char buf[96];
    size_t len = 0;

    ParseTag("en", &loc);
    RoboCheckEqInt(I18nDatetimeResolveSkeleton(&loc, NULL, "yMMMd", &pat), RoboOk);
    RoboCheckStrEq(pat, "MMM d, y");
    RoboCheckEqInt(I18nDatetimeResolveSkeleton(&loc, NULL, "Hm", &pat), RoboOk);
    RoboCheckStrEq(pat, "HH:mm");
    RoboCheckEqInt(I18nDatetimeResolveSkeleton(&loc, NULL, "yMd", &pat), RoboOk);
    RoboCheckStrEq(pat, "M/d/y");

    RoboCheckEqInt(
        I18nDatetimeFormatterInitSkeleton(&fmt, &loc, NULL, NULL, "yMMMd", NULL), RoboOk);
    RoboCheckEqInt(I18nDateInit(&date, 2026, 9, 13), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatDate(&fmt, &date, buf, sizeof(buf), &len), RoboOk);
    RoboCheckStrEq(buf, "Sep 13, 2026");
    I18nDatetimeFormatterDestroy(&fmt);

    /* Best-match foundation: unknown exact skeleton still resolves something usable. */
    RoboCheckEqInt(I18nDatetimeResolveSkeleton(&loc, NULL, "yMMMMd", &pat), RoboOk);
    RoboCheck(pat && pat[0]);
}

static void TestCalendarsFormat(void)
{
    I18nLocale loc;
    I18nCalendar cal;
    I18nDatetimeFormatter fmt;
    I18nDate date;
    char buf[96];
    size_t len = 0;

    ParseTag("en", &loc);
    RoboCheckEqInt(I18nDateInit(&date, 2026, 9, 13), RoboOk);

    RoboCheckEqInt(I18nCalendarFromId("buddhist", 8, &cal), RoboOk);
    RoboCheckEqInt(
        I18nDatetimeFormatterInitPattern(&fmt, &loc, &cal, NULL, "y", NULL), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatDate(&fmt, &date, buf, sizeof(buf), &len), RoboOk);
    RoboCheckStrEq(buf, "2569");
    I18nDatetimeFormatterDestroy(&fmt);

    RoboCheckEqInt(I18nCalendarFromId("japanese", 8, &cal), RoboOk);
    RoboCheckEqInt(I18nDateInit(&date, 2019, 5, 1), RoboOk);
    RoboCheckEqInt(
        I18nDatetimeFormatterInitPattern(&fmt, &loc, &cal, NULL, "Gy", NULL), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatDate(&fmt, &date, buf, sizeof(buf), &len), RoboOk);
    /* Reiwa + era year 1 */
    RoboCheck(strstr(buf, "1") != NULL);
    RoboCheck(strstr(buf, "Reiwa") != NULL || strstr(buf, "R") != NULL);
    I18nDatetimeFormatterDestroy(&fmt);

    RoboCheckEqInt(I18nCalendarFromId("islamic", 7, &cal), RoboOk);
    RoboCheckEqInt(
        I18nDatetimeFormatterInitPattern(&fmt, &loc, &cal, NULL, "y", NULL),
        RoboNotSupported);

    RoboCheckEqInt(I18nCalendarFromId("hebrew", 6, &cal), RoboOk);
    RoboCheckEqInt(
        I18nDatetimeFormatterInit(&fmt, &loc, &cal, NULL, RoboI18nDtfmtDate, NULL),
        RoboNotSupported);
    RoboCheckEqInt(I18nCalendarFromId("persian", 7, &cal), RoboOk);
    RoboCheckEqInt(
        I18nDatetimeFormatterInit(&fmt, &loc, &cal, NULL, RoboI18nDtfmtDate, NULL),
        RoboNotSupported);
}

static void TestTimezoneDisplay(void)
{
    I18nLocale loc;
    I18nTimezone tz;
    I18nDatetimeFormatter fmt;
    I18nInstant inst;
    char buf[96];
    size_t len = 0;

    ParseTag("en", &loc);
    RoboCheckEqInt(I18nTimezoneFixedSeconds(5 * 3600, &tz), RoboOk);
    RoboCheckEqInt(
        I18nDatetimeFormatterInitPattern(&fmt, &loc, NULL, &tz, "XXX", NULL), RoboOk);
    RoboCheckEqInt(I18nInstantFromUnixMs(0, &inst), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatInstant(&fmt, &inst, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, "+05:00");
    I18nDatetimeFormatterDestroy(&fmt);

    RoboCheckEqInt(
        I18nDatetimeFormatterInitPattern(&fmt, &loc, NULL, &tz, "O", NULL), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatInstant(&fmt, &inst, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheck(strstr(buf, "GMT") != NULL);
    RoboCheck(strstr(buf, "05") != NULL);
    I18nDatetimeFormatterDestroy(&fmt);

    RoboCheckEqInt(I18nTimezoneFromId("Asia/Karachi", 12, &tz), RoboOk);
    RoboCheckEqInt(
        I18nDatetimeFormatterInitPattern(&fmt, &loc, NULL, &tz, "y-MM-dd HH:mm", NULL),
        RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatInstant(&fmt, &inst, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, "1970-01-01 05:00");
    I18nDatetimeFormatterDestroy(&fmt);
}

static void TestFaDatetimeOrder(void)
{
    /* Persian combo uses "ساعت" between date and time — not a plain space concat. */
    I18nLocale loc;
    I18nDatetimeFormatter fmt;
    I18nDtfmtOptions opts;
    I18nDatetime dt;
    char buf[256];
    size_t len = 0;

    ParseTag("fa", &loc);
    I18nDtfmtOptionsInit(&opts);
    opts.dateStyle = RoboI18nCalStyleFull;
    opts.timeStyle = RoboI18nCalStyleShort;
    RoboCheckEqInt(
        I18nDatetimeFormatterInit(&fmt, &loc, NULL, NULL, RoboI18nDtfmtDatetime, &opts),
        RoboOk);
    RoboCheckEqInt(I18nDateInit(&dt.date, 2026, 9, 13), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&dt.time, 15, 45, 0, 0), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatDatetime(&fmt, &dt, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheck(strstr(buf, "\xD8\xB3\xD8\xA7\xD8\xB9\xD8\xAA") != NULL); /* ساعت */
    I18nDatetimeFormatterDestroy(&fmt);
}

static void TestPatternFuzz(void)
{
    I18nLocale loc;
    I18nDatetimeFormatter fmt;
    I18nDate date;
    char buf[128];
    size_t len = 0;
    const char *pats[] = {
        "",
        "'",
        "''''",
        "yyyy'MMMM'dd",
        "QQQQ",
        "zzzzzzzz",
        "y"
    };
    size_t i;

    ParseTag("en", &loc);
    RoboCheckEqInt(I18nDateInit(&date, 2026, 1, 1), RoboOk);

    RoboCheck(I18nDatetimeFormatterInitPattern(&fmt, &loc, NULL, NULL, "", NULL) !=
               RoboOk);
    RoboCheck(I18nDatetimeFormatterInitSkeleton(&fmt, &loc, NULL, NULL, "", NULL) !=
               RoboOk);
    RoboCheck(I18nDatetimeFormatterInitSkeleton(&fmt, &loc, NULL, NULL, "@@@", NULL) !=
               RoboOk);

    for (i = 2; i < 6; i++) {
        Result r =
            I18nDatetimeFormatterInitPattern(&fmt, &loc, NULL, NULL, pats[i], NULL);
        if (r == RoboOk) {
            (void)I18nDatetimeFormatDate(&fmt, &date, buf, sizeof(buf), &len);
            I18nDatetimeFormatterDestroy(&fmt);
        }
    }

    /* Extreme dates */
    RoboCheckEqInt(I18nDateInit(&date, RoboI18nDateYearMin, 1, 1), RoboOk);
    RoboCheckEqInt(
        I18nDatetimeFormatterInitPattern(&fmt, &loc, NULL, NULL, "y-MM-dd", NULL),
        RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatDate(&fmt, &date, buf, sizeof(buf), &len), RoboOk);
    I18nDatetimeFormatterDestroy(&fmt);
}

void TestRegisterI18nDatetimeFormat(void)
{
    TestRegister("i18n.dtfmt.versions", TestDtfmtVersions);
    TestRegister("i18n.dtfmt.en_styles", TestEnStyles);
    TestRegister("i18n.dtfmt.en_datetime", TestEnDatetimeCombo);
    TestRegister("i18n.dtfmt.non_latin", TestNonLatinDates);
    TestRegister("i18n.dtfmt.ur_bn_he_th", TestUrBnHeTh);
    TestRegister("i18n.dtfmt.day_periods", TestDayPeriodsAndNames);
    TestRegister("i18n.dtfmt.skeletons", TestSkeletons);
    TestRegister("i18n.dtfmt.calendars", TestCalendarsFormat);
    TestRegister("i18n.dtfmt.timezone", TestTimezoneDisplay);
    TestRegister("i18n.dtfmt.fa_combo", TestFaDatetimeOrder);
    TestRegister("i18n.dtfmt.fuzz", TestPatternFuzz);
}
