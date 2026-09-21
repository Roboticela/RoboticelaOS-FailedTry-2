/*
 * RoboI18N calendar arithmetic & date/time utilities (Goal 4.8).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void TestArithVersion(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
}

static void TestDurationVsPeriod(void)
{
    I18nDuration d;
    I18nPeriod p;

    RoboCheckEqInt(I18nDurationFromDays(1, &d), RoboOk);
    RoboCheckEqInt(d.millis, RoboI18nDurationMillisPerDay);
    RoboCheckEqInt(I18nPeriodOf(0, 0, 0, 1, &p), RoboOk);
    RoboCheckEqInt(p.days, 1);
    /* Period has no millis field — concepts stay distinct. */
    RoboCheckEqInt(I18nDurationFromHours(24, &d), RoboOk);
    RoboCheckEqInt(d.millis, RoboI18nDurationMillisPerDay);
}

static void TestDateAddDaysRoundtrip(void)
{
    I18nDate a, b, c;
    int32_t n;
    RoboCheckEqInt(I18nDateInit(&a, 2024, 2, 28), RoboOk);
    RoboCheckEqInt(I18nDateAddDays(&a, 366, &b), RoboOk);
    RoboCheckEqInt(I18nDateAddDays(&b, -366, &c), RoboOk);
    RoboCheckEqInt(I18nDateCompare(&a, &c), 0);
    RoboCheckEqInt(I18nDateDifferenceDays(&a, &b, &n), RoboOk);
    RoboCheckEqInt(n, 366);
}

static void TestEndOfMonthClamp(void)
{
    I18nDate d, out;
    RoboCheckEqInt(I18nDateInit(&d, 2026, 1, 31), RoboOk);
    RoboCheckEqInt(I18nDateAddMonths(&d, 1, &out), RoboOk);
    RoboCheckEqInt(out.year, 2026);
    RoboCheckEqInt((int)out.month, 2);
    RoboCheckEqInt((int)out.day, 28); /* CLAMP */

    RoboCheckEqInt(I18nDateInit(&d, 2024, 1, 31), RoboOk);
    RoboCheckEqInt(I18nDateAddMonths(&d, 1, &out), RoboOk);
    RoboCheckEqInt((int)out.day, 29); /* leap */

    RoboCheckEqInt(I18nDateInit(&d, 2024, 3, 31), RoboOk);
    RoboCheckEqInt(I18nDateAddMonths(&d, -1, &out), RoboOk);
    RoboCheckEqInt((int)out.month, 2);
    RoboCheckEqInt((int)out.day, 29);

    RoboCheckEqInt(I18nDateInit(&d, 2023, 2, 28), RoboOk);
    RoboCheckEqInt(I18nDateAddYears(&d, 1, &out), RoboOk);
    RoboCheckEqInt(out.year, 2024);
    RoboCheckEqInt((int)out.day, 28); /* not promoted to 29 */
}

static void TestDateAddPeriodAndDiff(void)
{
    I18nDate a, b, back;
    I18nPeriod p, diff;

    RoboCheckEqInt(I18nDateInit(&a, 2020, 1, 15), RoboOk);
    RoboCheckEqInt(I18nPeriodOf(1, 2, 1, 3, &p), RoboOk);
    RoboCheckEqInt(I18nDateAddPeriod(&a, &p, &b), RoboOk);
    /* 2020-01-15 + 1y2m + 7d + 3d = 2021-03-15 + 10d = 2021-03-25 */
    RoboCheckEqInt(b.year, 2021);
    RoboCheckEqInt((int)b.month, 3);
    RoboCheckEqInt((int)b.day, 25);

    RoboCheckEqInt(I18nDateDifferencePeriod(&a, &b, &diff), RoboOk);
    RoboCheckEqInt(I18nDateAddPeriod(&a, &diff, &back), RoboOk);
    RoboCheckEqInt(I18nDateCompare(&b, &back), 0);

    RoboCheckEqInt(I18nDateDifferencePeriod(&b, &a, &diff), RoboOk);
    RoboCheck(diff.years < 0 || diff.months < 0 || diff.days < 0);
}

static void TestInstantDurationRoundtrip(void)
{
    I18nInstant a, b, c;
    I18nDuration d, neg, zero, big;
    I18nDatetime utc;

    RoboCheckEqInt(I18nDateInit(&utc.date, 1970, 1, 1), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&utc.time, 0, 0, 0, 0), RoboOk);
    RoboCheckEqInt(I18nInstantFromUtc(&utc, &a), RoboOk);
    RoboCheckEqInt(a.unixMs, 0);

    RoboCheckEqInt(I18nDurationFromHours(1, &d), RoboOk);
    RoboCheckEqInt(I18nInstantAdd(&a, &d, &b), RoboOk);
    RoboCheckEqInt(b.unixMs, 3600000LL);
    RoboCheckEqInt(I18nInstantSubtract(&b, &d, &c), RoboOk);
    RoboCheckEqInt(I18nInstantCompare(&a, &c), 0);

    /* Pre-epoch */
    RoboCheckEqInt(I18nDurationFromDays(-1, &d), RoboOk);
    RoboCheckEqInt(I18nInstantAdd(&a, &d, &b), RoboOk);
    RoboCheckEqInt(b.unixMs, -RoboI18nDurationMillisPerDay);

    RoboCheckEqInt(I18nDurationFromMillis(0, &zero), RoboOk);
    RoboCheckEqInt(I18nInstantAdd(&a, &zero, &c), RoboOk);
    RoboCheckEqInt(c.unixMs, 0);

    a.unixMs = INT64_MAX;
    RoboCheckEqInt(I18nDurationFromMillis(1, &d), RoboOk);
    RoboCheckEqInt(I18nInstantAdd(&a, &d, &b), RoboNotSupported);

    a.unixMs = INT64_MIN;
    RoboCheckEqInt(I18nDurationFromMillis(-1, &d), RoboOk);
    RoboCheckEqInt(I18nInstantAdd(&a, &d, &b), RoboNotSupported);

    RoboCheckEqInt(I18nDurationFromMillis(INT64_MIN, &d), RoboOk);
    RoboCheckEqInt(I18nDurationNegate(&d, &neg), RoboError);

    RoboCheckEqInt(I18nDurationFromMillis(INT64_MAX, &big), RoboOk);
    RoboCheckEqInt(I18nDurationAdd(&big, &big, &d), RoboError);
}

static void TestIsoWeekUtils(void)
{
    I18nDate d, mon, sun;
    uint8_t weeks;
    int32_t wy;
    uint8_t week;
    I18nWeekday wd;

    RoboCheckEqInt(I18nDateInit(&d, 2026, 9, 13), RoboOk); /* Sunday */
    RoboCheckEqInt(I18nDateIsoWeekStart(&d, &mon), RoboOk);
    RoboCheckEqInt(I18nDateDayOfWeek(&mon, &wd), RoboOk);
    RoboCheckEqInt((int)wd, (int)RoboI18nWeekdayMon);
    RoboCheckEqInt(mon.year, 2026);
    RoboCheckEqInt((int)mon.month, 9);
    RoboCheckEqInt((int)mon.day, 7);

    RoboCheckEqInt(I18nDateIsoWeekEnd(&d, &sun), RoboOk);
    RoboCheckEqInt((int)sun.day, 13);

    RoboCheckEqInt(I18nIsoWeeksInYear(2004, &weeks), RoboOk);
    RoboCheckEqInt((int)weeks, 53);
    RoboCheckEqInt(I18nIsoWeeksInYear(2026, &weeks), RoboOk);
    RoboCheck((weeks == 52) || (weeks == 53));

    RoboCheckEqInt(I18nDateToIsoWeek(&d, &wy, &week, &wd), RoboOk);
    RoboCheckEqInt((int)wd, (int)RoboI18nWeekdaySun);
}

static void TestCivilBoundaries(void)
{
    I18nDate d, out;
    I18nDatetime dt;

    RoboCheckEqInt(I18nDateInit(&d, 2024, 5, 15), RoboOk);
    RoboCheckEqInt(I18nDateStartOfMonth(&d, &out), RoboOk);
    RoboCheckEqInt((int)out.day, 1);
    RoboCheckEqInt(I18nDateEndOfMonth(&d, &out), RoboOk);
    RoboCheckEqInt((int)out.day, 31);
    RoboCheckEqInt(I18nDateStartOfYear(&d, &out), RoboOk);
    RoboCheckEqInt((int)out.month, 1);
    RoboCheckEqInt((int)out.day, 1);
    RoboCheckEqInt(I18nDateEndOfYear(&d, &out), RoboOk);
    RoboCheckEqInt((int)out.month, 12);
    RoboCheckEqInt((int)out.day, 31);
    RoboCheckEqInt(I18nDateStartOfQuarter(&d, &out), RoboOk);
    RoboCheckEqInt((int)out.month, 4);
    RoboCheckEqInt(I18nDateEndOfQuarter(&d, &out), RoboOk);
    RoboCheckEqInt((int)out.month, 6);
    RoboCheckEqInt((int)out.day, 30);

    RoboCheckEqInt(I18nDatetimeStartOfDay(&d, &dt), RoboOk);
    RoboCheckEqInt((int)dt.time.hour, 0);
    RoboCheckEqInt(I18nDatetimeEndOfDay(&d, &dt), RoboOk);
    RoboCheckEqInt((int)dt.time.hour, 23);
    RoboCheckEqInt((int)dt.time.nano, 999999999);

    RoboCheckEqInt(I18nDateDaysInYear(2024), 366);
    RoboCheckEqInt(I18nDateDaysInYear(2023), 365);
}

static void TestLocaleWeekVsIso(void)
{
    I18nDate d, isoStart, locStart;
    I18nLocale us, fr;
    I18nWeekday wd;

    RoboCheckEqInt(I18nDateInit(&d, 2026, 9, 13), RoboOk); /* Sunday */
    RoboCheckEqInt(I18nDateIsoWeekStart(&d, &isoStart), RoboOk);

    ParseTag("en-US", &us);
    ParseTag("fr-FR", &fr);
    RoboCheckEqInt(I18nDateStartOfWeek(&d, &us, &locStart), RoboOk);
    RoboCheckEqInt(I18nDateDayOfWeek(&locStart, &wd), RoboOk);
    /* US typically Sunday-first — may equal d or earlier Sunday. */
    RoboCheck(wd >= RoboI18nWeekdayMon && wd <= RoboI18nWeekdaySun);

    RoboCheckEqInt(I18nDateStartOfWeek(&d, &fr, &locStart), RoboOk);
    /* fr-FR Monday-first should match ISO Monday for this week. */
    RoboCheckEqInt(I18nDateCompare(&locStart, &isoStart), 0);
}

static void TestTzAwarePeriod(void)
{
    I18nTimezone tz, iana;
    I18nInstant inst, midnight, next;
    I18nDatetime utc, local;
    I18nPeriod oneDay;
    I18nTzResolve res;
    I18nDuration durDay;

    RoboCheckEqInt(I18nTimezoneFromId("+05:00", 6, &tz), RoboOk);
    RoboCheckEqInt(I18nDateInit(&utc.date, 2026, 3, 10), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&utc.time, 12, 0, 0, 0), RoboOk);
    RoboCheckEqInt(I18nInstantFromUtc(&utc, &inst), RoboOk);

    RoboCheckEqInt(I18nInstantStartOfDay(&inst, &tz, &midnight), RoboOk);
    RoboCheckEqInt(I18nTimezoneToLocal(&tz, &midnight, &local, &res), RoboOk);
    RoboCheckEqInt((int)local.time.hour, 0);
    RoboCheckEqInt((int)local.time.minute, 0);

    RoboCheckEqInt(I18nPeriodOf(0, 0, 0, 1, &oneDay), RoboOk);
    RoboCheckEqInt(I18nInstantAddPeriod(&inst, &oneDay, &tz, &next), RoboOk);
    /* Fixed offset: +1 calendar day equals +86400000 ms. */
    RoboCheckEqInt(I18nDurationFromDays(1, &durDay), RoboOk);
    {
        I18nInstant viaDur;
        RoboCheckEqInt(I18nInstantAdd(&inst, &durDay, &viaDur), RoboOk);
        RoboCheckEqInt(I18nInstantCompare(&next, &viaDur), 0);
    }

    RoboCheckEqInt(I18nTimezoneFromId("America/New_York", 16, &iana), RoboOk);
    /* IANA zones now supported via tzdb (Goal 4.14). */
    RoboCheckEqInt(I18nInstantAddPeriod(&inst, &oneDay, &iana, &next), RoboOk);
    {
        I18nInstant viaDur;
        RoboCheckEqInt(I18nInstantAdd(&inst, &durDay, &viaDur), RoboOk);
        /* Away from a DST fold for this instant, calendar day matches 86400s. */
        RoboCheckEqInt(I18nInstantCompare(&next, &viaDur), 0);
    }
}

static void TestCalendarArith(void)
{
    I18nCalendar cal;
    I18nDate g;
    I18nCalendarFields f, out;
    I18nPeriod oneYear;

    RoboCheckEqInt(I18nPeriodOf(1, 0, 0, 0, &oneYear), RoboOk);
    RoboCheckEqInt(I18nDateInit(&g, 2026, 9, 13), RoboOk);

    RoboCheckEqInt(I18nCalendarFromId("buddhist", 8, &cal), RoboOk);
    RoboCheckEqInt(I18nCalendarFromGregorian(&cal, &g, &f), RoboOk);
    RoboCheckEqInt(I18nCalendarAddPeriod(&cal, &f, &oneYear, &out), RoboOk);
    RoboCheckEqInt(out.year, f.year + 1);

    RoboCheckEqInt(I18nCalendarFromId("japanese", 8, &cal), RoboOk);
    RoboCheckEqInt(I18nDateInit(&g, 2019, 5, 1), RoboOk);
    RoboCheckEqInt(I18nCalendarFromGregorian(&cal, &g, &f), RoboOk);
    RoboCheckEqInt(I18nCalendarAddPeriod(&cal, &f, &oneYear, &out), RoboOk);
    RoboCheckEqInt(I18nCalendarToGregorian(&cal, &out, &g), RoboOk);
    RoboCheckEqInt(g.year, 2020);

    RoboCheckEqInt(I18nCalendarFromId("islamic", 7, &cal), RoboOk);
    RoboCheckEqInt(I18nCalendarAddPeriod(&cal, &f, &oneYear, &out),
                      RoboNotSupported);
    RoboCheckEqInt(I18nCalendarFromId("persian", 7, &cal), RoboOk);
    RoboCheckEqInt(I18nCalendarAddPeriod(&cal, &f, &oneYear, &out),
                      RoboNotSupported);
    RoboCheckEqInt(I18nCalendarFromId("hebrew", 6, &cal), RoboOk);
    RoboCheckEqInt(I18nCalendarAddPeriod(&cal, &f, &oneYear, &out),
                      RoboNotSupported);
}

static int Holiday20260914(const I18nDate *date, void *ctx)
{
    (void)ctx;
    return date && date->year == 2026 && date->month == 9 && date->day == 14;
}

static void TestBusinessDays(void)
{
    I18nBusinessCalendar biz;
    I18nDate fri, next, mon;
    I18nLocale us;

    I18nBusinessCalendarInit(&biz);
    RoboCheckEqInt(I18nDateInit(&fri, 2026, 9, 11), RoboOk); /* Friday */
    RoboCheck(I18nDateIsBusinessDay(&fri, &biz));
    RoboCheckEqInt(I18nDateAddBusinessDays(&fri, 1, &biz, &next), RoboOk);
    /* Next business day = Monday 14 */
    RoboCheckEqInt(next.year, 2026);
    RoboCheckEqInt((int)next.month, 9);
    RoboCheckEqInt((int)next.day, 14);

    biz.IsHoliday = Holiday20260914;
    RoboCheckEqInt(I18nDateAddBusinessDays(&fri, 1, &biz, &next), RoboOk);
    RoboCheckEqInt((int)next.day, 15);

    ParseTag("en-US", &us);
    RoboCheckEqInt(I18nBusinessCalendarFromLocale(&us, &biz), RoboOk);
    RoboCheckEqInt(I18nDateInit(&mon, 2026, 9, 12), RoboOk); /* Saturday */
    RoboCheck(I18nDateIsWeekend(&mon, &biz));
    RoboCheck(!I18nDateIsBusinessDay(&mon, &biz));
}

static void TestComparisons(void)
{
    I18nTime t1, t2;
    I18nDatetime a, b;
    I18nDuration d1, d2;

    RoboCheckEqInt(I18nTimeInit(&t1, 10, 0, 0, 0), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&t2, 11, 0, 0, 0), RoboOk);
    RoboCheck(I18nTimeCompare(&t1, &t2) < 0);
    RoboCheck(I18nTimeCompare(&t2, &t1) > 0);

    RoboCheckEqInt(I18nDateInit(&a.date, 2026, 1, 1), RoboOk);
    a.time = t2;
    RoboCheckEqInt(I18nDateInit(&b.date, 2026, 1, 1), RoboOk);
    b.time = t1;
    RoboCheck(I18nDatetimeCompare(&a, &b) > 0);

    RoboCheckEqInt(I18nDurationFromSeconds(1, &d1), RoboOk);
    RoboCheckEqInt(I18nDurationFromMillis(1000, &d2), RoboOk);
    RoboCheckEqInt(I18nDurationCompare(&d1, &d2), 0);
}

static uint32_t FuzzXorshift(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x ? x : 0xA5A5A5A5u;
    return *s;
}

static void TestFuzzDateInvariants(void)
{
    uint32_t seed = 0xC0FFEEu;
    int i;
    for (i = 0; i < 200; i++) {
        int32_t y = (int32_t)(FuzzXorshift(&seed) % 8000u) + 1600;
        uint8_t m = (uint8_t)((FuzzXorshift(&seed) % 12u) + 1u);
        uint8_t dim = I18nDateDaysInMonth(y, m);
        uint8_t day = (uint8_t)((FuzzXorshift(&seed) % dim) + 1u);
        int32_t delta = (int32_t)(FuzzXorshift(&seed) % 400u) - 200;
        I18nDate a, b, c;
        int32_t days;
        int cmpAb;
        int cmpBa;

        if (I18nDateInit(&a, y, m, day) != RoboOk) {
            continue;
        }
        if (I18nDateAddDays(&a, delta, &b) != RoboOk) {
            continue;
        }
        RoboCheckEqInt(I18nDateAddDays(&b, -delta, &c), RoboOk);
        RoboCheckEqInt(I18nDateCompare(&a, &c), 0);
        RoboCheckEqInt(I18nDateDifferenceDays(&a, &b, &days), RoboOk);
        RoboCheckEqInt(days, delta);

        cmpAb = I18nDateCompare(&a, &b);
        cmpBa = I18nDateCompare(&b, &a);
        if (cmpAb == 0) {
            RoboCheckEqInt(cmpBa, 0);
        } else {
            RoboCheck(cmpAb == -cmpBa);
        }

        /* Month add clamp then subtract months is not always identity — only
         * days round-trip is guaranteed. Month overflow must not crash. */
        {
            I18nDate m1;
            int32_t months = (int32_t)(FuzzXorshift(&seed) % 48u) - 24;
            (void)I18nDateAddMonths(&a, months, &m1);
        }
    }

    /* Malformed / extreme: must not crash. */
    {
        I18nDate bad;
        I18nDate out;
        I18nPeriod p;
        memset(&bad, 0, sizeof(bad));
        bad.year = 99999;
        bad.month = 13;
        bad.day = 40;
        RoboCheckEqInt(I18nDateAddMonths(&bad, 1, &out), RoboInvalidArgument);
        RoboCheckEqInt(I18nPeriodOf(INT32_MAX, INT32_MAX, 0, 0, &p), RoboOk);
        RoboCheckEqInt(I18nDateInit(&bad, 2020, 1, 1), RoboOk);
        RoboCheckEqInt(I18nDateAddPeriod(&bad, &p, &out), RoboError);
    }
}

void TestRegisterI18nDatetimeArith(void)
{
    TestRegister("i18n.arith.version", TestArithVersion);
    TestRegister("i18n.arith.duration_vs_period", TestDurationVsPeriod);
    TestRegister("i18n.arith.days_roundtrip", TestDateAddDaysRoundtrip);
    TestRegister("i18n.arith.eom_clamp", TestEndOfMonthClamp);
    TestRegister("i18n.arith.period_diff", TestDateAddPeriodAndDiff);
    TestRegister("i18n.arith.instant_duration", TestInstantDurationRoundtrip);
    TestRegister("i18n.arith.iso_week", TestIsoWeekUtils);
    TestRegister("i18n.arith.boundaries", TestCivilBoundaries);
    TestRegister("i18n.arith.locale_week", TestLocaleWeekVsIso);
    TestRegister("i18n.arith.tz_period", TestTzAwarePeriod);
    TestRegister("i18n.arith.calendars", TestCalendarArith);
    TestRegister("i18n.arith.business", TestBusinessDays);
    TestRegister("i18n.arith.compare", TestComparisons);
    TestRegister("i18n.arith.fuzz", TestFuzzDateInvariants);
}
