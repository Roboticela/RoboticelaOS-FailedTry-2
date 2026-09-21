/*
 * RoboI18N date/time/calendar/timezone suite (Goal 4.5).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboCalData.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void TestDtVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nCldrVersionString(), "46");
    RoboCheckStrEq(RoboCalCldrVersion, "46");
    RoboCheck(CalWeekCount > 100u);
    RoboCheck(CalJapaneseEraStartCount > 200u);
    RoboCheck(CalSymbolsCount >= 20u);
}

static void TestGregorianLeap(void)
{
    RoboCheck(I18nDateIsLeapYear(2000));
    RoboCheck(!I18nDateIsLeapYear(1900));
    RoboCheck(I18nDateIsLeapYear(2004));
    RoboCheck(!I18nDateIsLeapYear(2100));
    RoboCheckEqInt((int)I18nDateDaysInMonth(2024, 2), 29);
    RoboCheckEqInt((int)I18nDateDaysInMonth(2023, 2), 28);
    RoboCheckEqInt((int)I18nDateDaysInMonth(2023, 4), 30);
    RoboCheckEqInt((int)I18nDateDaysInMonth(2023, 1), 31);
}

static void TestGregorianValid(void)
{
    I18nDate d;
    RoboCheckEqInt(I18nDateInit(&d, 2026, 9, 13), RoboOk);
    RoboCheck(I18nDateIsValid(&d));
    RoboCheckEqInt(I18nDateInit(&d, 2023, 2, 29), RoboInvalidArgument);
    RoboCheckEqInt(I18nDateInitChecked(&d, 2023, 2, 29, 0), RoboInvalidArgument);
    RoboCheckEqInt(I18nDateInitChecked(&d, 2023, 2, 29, 1), RoboOk);
    RoboCheckEqInt(d.year, 2023);
    RoboCheckEqInt((int)d.month, 3);
    RoboCheckEqInt((int)d.day, 1);
}

static void TestGregorianBoundaries(void)
{
    I18nDate d, n;
    I18nWeekday wd;
    int32_t doy;
    RoboCheckEqInt(I18nDateInit(&d, 2023, 12, 31), RoboOk);
    RoboCheckEqInt(I18nDateAddDays(&d, 1, &n), RoboOk);
    RoboCheckEqInt(n.year, 2024);
    RoboCheckEqInt((int)n.month, 1);
    RoboCheckEqInt((int)n.day, 1);
    RoboCheckEqInt(I18nDateInit(&d, 2024, 1, 1), RoboOk);
    RoboCheckEqInt(I18nDateDayOfYear(&d, &doy), RoboOk);
    RoboCheckEqInt(doy, 1);
    RoboCheckEqInt(I18nDateInit(&d, 2024, 12, 31), RoboOk);
    RoboCheckEqInt(I18nDateDayOfYear(&d, &doy), RoboOk);
    RoboCheckEqInt(doy, 366);
    /* 2026-09-13 is Sunday */
    RoboCheckEqInt(I18nDateInit(&d, 2026, 9, 13), RoboOk);
    RoboCheckEqInt(I18nDateDayOfWeek(&d, &wd), RoboOk);
    RoboCheckEqInt((int)wd, (int)RoboI18nWeekdaySun);
}

static void TestIsoWeek(void)
{
    I18nDate d, back;
    int32_t wy;
    uint8_t week;
    I18nWeekday wd;
    RoboCheckEqInt(I18nDateInit(&d, 2026, 1, 1), RoboOk);
    RoboCheckEqInt(I18nDateToIsoWeek(&d, &wy, &week, &wd), RoboOk);
    RoboCheckEqInt(I18nDateFromIsoWeek(wy, week, wd, &back), RoboOk);
    RoboCheckEqInt(I18nDateCompare(&d, &back), 0);
    /* Known: 2005-01-01 is ISO week 2004-W53 */
    RoboCheckEqInt(I18nDateInit(&d, 2005, 1, 1), RoboOk);
    RoboCheckEqInt(I18nDateToIsoWeek(&d, &wy, &week, &wd), RoboOk);
    RoboCheckEqInt(wy, 2004);
    RoboCheckEqInt((int)week, 53);
}

static void TestTimePrimitives(void)
{
    I18nTime t;
    int64_t nanos;
    RoboCheckEqInt(I18nTimeInit(&t, 0, 0, 0, 0), RoboOk);
    RoboCheck(I18nTimeIsValid(&t));
    RoboCheckEqInt(I18nTimeInit(&t, 23, 59, 59, 999999999u), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&t, 24, 0, 0, 0), RoboInvalidArgument);
    RoboCheckEqInt(I18nTimeInit(&t, 0, 0, 0, 1000000000u), RoboInvalidArgument);
    RoboCheckEqInt(I18nTimeInit(&t, 12, 30, 45, 123456789u), RoboOk);
    RoboCheckEqInt(I18nTimeToNanosOfDay(&t, &nanos), RoboOk);
    RoboCheckEqInt(I18nTimeFromNanosOfDay(nanos, &t), RoboOk);
    RoboCheckEqInt((int)t.hour, 12);
    RoboCheckEqInt((int)t.minute, 30);
    RoboCheckEqInt((int)t.second, 45);
    RoboCheckEqInt((int)t.nano, 123456789);
}

static void TestInstantRoundtrip(void)
{
    I18nDatetime utc, back;
    I18nInstant inst, epoch, before, after;
    int64_t ms;
    RoboCheckEqInt(I18nDateInit(&utc.date, 1970, 1, 1), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&utc.time, 0, 0, 0, 0), RoboOk);
    RoboCheckEqInt(I18nInstantFromUtc(&utc, &epoch), RoboOk);
    RoboCheckEqInt(epoch.unixMs, 0);
    RoboCheckEqInt(I18nInstantToUtc(&epoch, &back), RoboOk);
    RoboCheckEqInt(back.date.year, 1970);

    RoboCheckEqInt(I18nDateInit(&utc.date, 1969, 12, 31), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&utc.time, 23, 59, 59, 0), RoboOk);
    RoboCheckEqInt(I18nInstantFromUtc(&utc, &before), RoboOk);
    RoboCheck(before.unixMs < 0);
    RoboCheckEqInt(I18nInstantToUtc(&before, &back), RoboOk);
    RoboCheckEqInt(back.date.year, 1969);
    RoboCheckEqInt((int)back.date.month, 12);
    RoboCheckEqInt((int)back.date.day, 31);

    RoboCheckEqInt(I18nDateInit(&utc.date, 2026, 9, 13), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&utc.time, 6, 19, 0, 500000000u), RoboOk);
    RoboCheckEqInt(I18nInstantFromUtc(&utc, &inst), RoboOk);
    RoboCheckEqInt(I18nInstantToUtc(&inst, &back), RoboOk);
    RoboCheckEqInt(back.date.year, 2026);
    RoboCheckEqInt((int)back.date.month, 9);
    RoboCheckEqInt((int)back.date.day, 13);
    RoboCheckEqInt((int)back.time.hour, 6);
    RoboCheckEqInt((int)back.time.minute, 19);
    /* nano truncated to ms in instant representation */
    RoboCheckEqInt((int)back.time.nano, 500000000);

    RoboCheckEqInt(I18nInstantFromUnixS(1, &after), RoboOk);
    RoboCheckEqInt(after.unixMs, 1000);
    RoboCheckEqInt(I18nInstantToUnixMs(&after, &ms), RoboOk);
    RoboCheckEqInt((int)ms, 1000);
}

static void TestTimezoneFixed(void)
{
    I18nTimezone utc, karachi, neg, iana, sys;
    I18nInstant inst;
    I18nDatetime local, utcDt;
    I18nTzResolve res;
    int32_t off;

    RoboCheckEqInt(I18nTimezoneUtc(&utc), RoboOk);
    RoboCheckEqInt((int)MakeI18nTimezoneKind(&utc), (int)RoboI18nTzUtc);

    RoboCheckEqInt(I18nTimezoneFromId("+05:00", 6, &karachi), RoboOk);
    RoboCheckEqInt((int)MakeI18nTimezoneKind(&karachi), (int)RoboI18nTzFixed);
    RoboCheckEqInt(karachi.offsetSeconds, 5 * 3600);

    RoboCheckEqInt(I18nTimezoneFromId("-08:00", 6, &neg), RoboOk);
    RoboCheckEqInt(neg.offsetSeconds, -8 * 3600);

    RoboCheckEqInt(I18nDateInit(&utcDt.date, 2026, 9, 13), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&utcDt.time, 6, 19, 0, 0), RoboOk);
    RoboCheckEqInt(I18nInstantFromUtc(&utcDt, &inst), RoboOk);

    RoboCheckEqInt(I18nTimezoneOffsetAt(&karachi, &inst, &off), RoboOk);
    RoboCheckEqInt(off, 5 * 3600);
    RoboCheckEqInt(I18nTimezoneToLocal(&karachi, &inst, &local, &res), RoboOk);
    RoboCheckEqInt((int)res, (int)RoboI18nTzUnique);
    RoboCheckEqInt((int)local.time.hour, 11);
    RoboCheckEqInt((int)local.time.minute, 19);

    {
        I18nInstant back;
        RoboCheckEqInt(I18nTimezoneToUtc(&karachi, &local, &back, &res), RoboOk);
        RoboCheckEqInt(I18nInstantCompare(&inst, &back), 0);
    }

    RoboCheckEqInt(I18nTimezoneFromId("Asia/Karachi", 12, &iana), RoboOk);
    RoboCheckEqInt((int)MakeI18nTimezoneKind(&iana), (int)RoboI18nTzIana);
    RoboCheckEqInt(I18nTimezoneOffsetAt(&iana, &inst, &off), RoboOk);
    RoboCheckEqInt(off, 5 * 3600);

    RoboCheckEqInt(I18nTimezoneFromId("UTC+05:30", 9, &karachi), RoboOk);
    RoboCheckEqInt(karachi.offsetSeconds, 5 * 3600 + 30 * 60);

    RoboCheckEqInt(I18nTimezoneFromId("local", 5, &sys), RoboOk);
    RoboCheckEqInt((int)MakeI18nTimezoneKind(&sys), (int)RoboI18nTzSystem);
}

static void TestCalendarConversions(void)
{
    I18nCalendar cal;
    I18nDate g;
    I18nCalendarFields f, back;

    RoboCheckEqInt(I18nCalendarFromId("gregorian", 9, &cal), RoboOk);
    RoboCheck(I18nCalendarHasConversion(&cal));
    RoboCheckEqInt(I18nDateInit(&g, 2026, 9, 13), RoboOk);
    RoboCheckEqInt(I18nCalendarFromGregorian(&cal, &g, &f), RoboOk);
    RoboCheckEqInt(f.year, 2026);

    RoboCheckEqInt(I18nCalendarFromId("buddhist", 8, &cal), RoboOk);
    RoboCheckEqInt(I18nCalendarFromGregorian(&cal, &g, &f), RoboOk);
    RoboCheckEqInt(f.year, 2026 + 543);
    RoboCheckEqInt(I18nCalendarToGregorian(&cal, &f, &g), RoboOk);
    RoboCheckEqInt(g.year, 2026);

    RoboCheckEqInt(I18nCalendarFromId("japanese", 8, &cal), RoboOk);
    RoboCheckEqInt(I18nDateInit(&g, 2019, 5, 1), RoboOk); /* Reiwa starts */
    RoboCheckEqInt(I18nCalendarFromGregorian(&cal, &g, &f), RoboOk);
    RoboCheckEqInt(f.year, 1);
    RoboCheckEqInt(I18nCalendarToGregorian(&cal, &f, &g), RoboOk);
    RoboCheckEqInt(g.year, 2019);
    RoboCheckEqInt((int)g.month, 5);
    RoboCheckEqInt((int)g.day, 1);

    RoboCheckEqInt(I18nDateInit(&g, 2019, 4, 30), RoboOk); /* Heisei last day */
    RoboCheckEqInt(I18nCalendarFromGregorian(&cal, &g, &f), RoboOk);
    RoboCheckEqInt(I18nCalendarToGregorian(&cal, &f, &g), RoboOk);
    RoboCheckEqInt(g.year, 2019);
    RoboCheckEqInt((int)g.month, 4);
    RoboCheckEqInt((int)g.day, 30);

    RoboCheckEqInt(I18nCalendarFromId("islamic", 7, &cal), RoboOk);
    RoboCheck(!I18nCalendarHasConversion(&cal));
    RoboCheckEqInt(I18nCalendarFromGregorian(&cal, &g, &back), RoboNotSupported);

    RoboCheckEqInt(I18nCalendarFromId("persian", 7, &cal), RoboOk);
    RoboCheckEqInt(I18nCalendarFromGregorian(&cal, &g, &back), RoboNotSupported);
    RoboCheckEqInt(I18nCalendarFromId("hebrew", 6, &cal), RoboOk);
    RoboCheckEqInt(I18nCalendarFromGregorian(&cal, &g, &back), RoboNotSupported);
    RoboCheckEqInt(I18nCalendarFromId("chinese", 7, &cal), RoboOk);
    RoboCheckEqInt(I18nCalendarFromGregorian(&cal, &g, &back), RoboNotSupported);
}

static void TestCalendarMetadataLocales(void)
{
    static const char *tags[] = {
        "en", "ur", "ar", "fa", "he", "hi", "bn", "th", "zh", "ja", "ko"
    };
    size_t i;
    I18nCalendar greg;
    const char *name;
    const char *pat;

    RoboCheckEqInt(I18nCalendarFromKind(RoboI18nCalGregorian, &greg), RoboOk);
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        I18nLocale loc;
        I18nWeekday first, ws, we;
        ParseTag(tags[i], &loc);
        RoboCheckEqInt(I18nLocaleFirstDayOfWeek(&loc, &first), RoboOk);
        RoboCheck(first >= 1 && first <= 7);
        RoboCheckEqInt(I18nLocaleWeekend(&loc, &ws, &we), RoboOk);
        RoboCheckEqInt(
            I18nCalendarMonthName(&loc, &greg, 1, RoboI18nCalNameWide, &name),
            RoboOk);
        RoboCheck(name && name[0]);
        RoboCheckEqInt(
            I18nCalendarWeekdayName(
                &loc, &greg, RoboI18nWeekdayMon, RoboI18nCalNameAbbreviated, &name),
            RoboOk);
        RoboCheck(name && name[0]);
        RoboCheckEqInt(
            I18nCalendarDatePattern(&loc, &greg, RoboI18nCalStyleMedium, &pat),
            RoboOk);
        RoboCheck(pat && pat[0]);
        RoboCheckEqInt(
            I18nCalendarTimePattern(&loc, &greg, RoboI18nCalStyleShort, &pat),
            RoboOk);
        RoboCheckEqInt(
            I18nCalendarDayPeriodName(
                &loc, &greg, "am", RoboI18nCalNameAbbreviated, &name),
            RoboOk);
    }

    {
        I18nLocale ja, th, enUs, sa;
        I18nCalendar jap, bud;
        I18nWeekday first;
        const char *era;
        ParseTag("ja", &ja);
        ParseTag("th", &th);
        ParseTag("en-US", &enUs);
        ParseTag("sa", &sa); /* Saudi — weekend Fri-Sat typically */
        RoboCheckEqInt(I18nCalendarFromKind(RoboI18nCalJapanese, &jap), RoboOk);
        RoboCheckEqInt(I18nCalendarFromKind(RoboI18nCalBuddhist, &bud), RoboOk);
        RoboCheckEqInt(
            I18nCalendarEraName(&ja, &jap, 236, RoboI18nCalNameAbbreviated, &era),
            RoboOk); /* Reiwa */
        RoboCheck(era && era[0]);
        RoboCheckEqInt(
            I18nCalendarMonthName(&th, &bud, 1, RoboI18nCalNameWide, &era), RoboOk);
        RoboCheckEqInt(I18nLocaleFirstDayOfWeek(&enUs, &first), RoboOk);
        RoboCheckEqInt((int)first, (int)RoboI18nWeekdaySun);
    }
}

static void TestDatetimeFuzzSmoke(void)
{
    int32_t y;
    uint8_t m, d;
    I18nDate date, round;
    int64_t rd;
    for (y = 1900; y <= 2100; y += 17) {
        for (m = 1; m <= 12; m++) {
            d = I18nDateDaysInMonth(y, m);
            if (I18nDateInit(&date, y, m, d) != RoboOk) {
                continue;
            }
            RoboCheckEqInt(I18nDateToRataDie(&date, &rd), RoboOk);
            RoboCheckEqInt(I18nDateFromRataDie(rd, &round), RoboOk);
            RoboCheckEqInt(I18nDateCompare(&date, &round), 0);
        }
    }
    /* Invalid timezone ids must not crash */
    {
        I18nTimezone tz;
        RoboCheck(I18nTimezoneFromId("", 0, &tz) != RoboOk);
        RoboCheck(I18nTimezoneFromId("@@@", 3, &tz) != RoboOk);
        RoboCheck(I18nTimezoneFromId("+99:99", 6, &tz) != RoboOk);
    }
}

void TestRegisterI18nDatetime(void)
{
    TestRegister("i18n.datetime.versions", TestDtVersions);
    TestRegister("i18n.datetime.gregorian_leap", TestGregorianLeap);
    TestRegister("i18n.datetime.gregorian_valid", TestGregorianValid);
    TestRegister("i18n.datetime.gregorian_boundaries", TestGregorianBoundaries);
    TestRegister("i18n.datetime.iso_week", TestIsoWeek);
    TestRegister("i18n.datetime.time", TestTimePrimitives);
    TestRegister("i18n.datetime.instant", TestInstantRoundtrip);
    TestRegister("i18n.datetime.timezone", TestTimezoneFixed);
    TestRegister("i18n.datetime.calendars", TestCalendarConversions);
    TestRegister("i18n.datetime.metadata_locales", TestCalendarMetadataLocales);
    TestRegister("i18n.datetime.fuzz_smoke", TestDatetimeFuzzSmoke);
}
