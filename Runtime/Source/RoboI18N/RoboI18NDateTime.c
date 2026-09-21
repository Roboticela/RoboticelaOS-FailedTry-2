/*
 * Proleptic Gregorian date/time/instant primitives (Goal 4.5).
 * Civil algorithms follow publicly documented rata-die methods
 * (e.g. Howard Hinnant); not copied from Chromium/ICU.
 */

#include "RoboI18N/RoboI18NDateTime.h"

#include <limits.h>

static int YearInRange(int32_t y)
{
    return y >= RoboI18nDateYearMin && y <= RoboI18nDateYearMax;
}

int I18nDateIsLeapYear(int32_t year)
{
    if (year % 4 != 0) {
        return 0;
    }
    if (year % 100 != 0) {
        return 1;
    }
    return (year % 400 == 0) ? 1 : 0;
}

uint8_t I18nDateDaysInMonth(int32_t year, uint8_t month)
{
    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && I18nDateIsLeapYear(year)) {
        return 29;
    }
    return mdays[month - 1u];
}

int I18nDateIsValid(const I18nDate *date)
{
    uint8_t dim;
    if (!date || !YearInRange(date->year)) {
        return 0;
    }
    if (date->month < 1 || date->month > 12) {
        return 0;
    }
    dim = I18nDateDaysInMonth(date->year, date->month);
    return date->day >= 1 && date->day <= dim;
}

Result I18nDateInit(I18nDate *out, int32_t year, uint8_t month, uint8_t day)
{
    return I18nDateInitChecked(out, year, month, day, 0);
}

/*
 * Days since 1970-01-01 (Hinnant DaysFromCivil).
 */
static int64_t DaysFromCivil(int32_t y, unsigned m, unsigned d)
{
    int64_t era;
    unsigned yoe;
    unsigned doy;
    unsigned doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void CivilFromDays(int64_t z, int32_t *y, unsigned *m, unsigned *d)
{
    int64_t era;
    unsigned doe;
    unsigned yoe;
    unsigned doy;
    unsigned mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *y = (int32_t)((int64_t)yoe + era * 400);
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y += (*m <= 2);
}

/* 0001-01-01 rata die = 1 */
static int64_t RdFromCivil(int32_t y, unsigned m, unsigned d)
{
    return DaysFromCivil(y, m, d) - DaysFromCivil(1, 1, 1) + 1;
}

static void CivilFromRd(int64_t rd, int32_t *y, unsigned *m, unsigned *d)
{
    CivilFromDays(rd - 1 + DaysFromCivil(1, 1, 1), y, m, d);
}

Result I18nDateInitChecked(I18nDate *out,
                                          int32_t year,
                                          uint8_t month,
                                          uint8_t day,
                                          int normalize)
{
    I18nDate tmp;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!YearInRange(year) || month < 1 || month > 12 || day < 1) {
        return RoboInvalidArgument;
    }
    tmp.year = year;
    tmp.month = month;
    tmp.day = day;
    if (I18nDateIsValid(&tmp)) {
        *out = tmp;
        return RoboOk;
    }
    if (!normalize) {
        return RoboInvalidArgument;
    }
    {
        int64_t rd;
        I18nDate base;
        base.year = year;
        base.month = month;
        base.day = 1;
        if (!I18nDateIsValid(&base)) {
            return RoboInvalidArgument;
        }
        rd = RdFromCivil(base.year, base.month, base.day);
        return I18nDateFromRataDie(rd + (int64_t)day - 1, out);
    }
}

Result I18nDateToRataDie(const I18nDate *date, int64_t *outRd)
{
    if (!date || !outRd || !I18nDateIsValid(date)) {
        return RoboInvalidArgument;
    }
    *outRd = RdFromCivil(date->year, date->month, date->day);
    return RoboOk;
}

Result I18nDateFromRataDie(int64_t rd, I18nDate *out)
{
    int32_t y;
    unsigned m, d;
    if (!out) {
        return RoboInvalidArgument;
    }
    CivilFromRd(rd, &y, &m, &d);
    if (!YearInRange(y)) {
        return RoboNotSupported;
    }
    out->year = y;
    out->month = (uint8_t)m;
    out->day = (uint8_t)d;
    return RoboOk;
}

Result I18nDateDayOfWeek(const I18nDate *date,
                                         I18nWeekday *out)
{
    int64_t rd;
    int64_t z;
    if (!out || I18nDateToRataDie(date, &rd) != RoboOk) {
        return RoboInvalidArgument;
    }
    /* 0001-01-01 was Monday. */
    z = (rd - 1) % 7;
    if (z < 0) {
        z += 7;
    }
    *out = (I18nWeekday)(z + 1);
    return RoboOk;
}

Result I18nDateDayOfYear(const I18nDate *date, int32_t *out)
{
    I18nDate jan1;
    int64_t a, b;
    if (!out || !I18nDateIsValid(date)) {
        return RoboInvalidArgument;
    }
    jan1.year = date->year;
    jan1.month = 1;
    jan1.day = 1;
    if (I18nDateToRataDie(&jan1, &a) != RoboOk ||
        I18nDateToRataDie(date, &b) != RoboOk) {
        return RoboInvalidArgument;
    }
    *out = (int32_t)(b - a + 1);
    return RoboOk;
}

Result I18nDateAddDays(const I18nDate *in,
                                      int32_t days,
                                      I18nDate *out)
{
    int64_t rd;
    if (!out || I18nDateToRataDie(in, &rd) != RoboOk) {
        return RoboInvalidArgument;
    }
    if ((days > 0 && rd > INT64_MAX - days) || (days < 0 && rd < INT64_MIN - days)) {
        return RoboNotSupported;
    }
    return I18nDateFromRataDie(rd + days, out);
}

int I18nDateCompare(const I18nDate *a, const I18nDate *b)
{
    if (!a || !b) {
        return 0;
    }
    if (a->year != b->year) {
        return a->year < b->year ? -1 : 1;
    }
    if (a->month != b->month) {
        return a->month < b->month ? -1 : 1;
    }
    if (a->day != b->day) {
        return a->day < b->day ? -1 : 1;
    }
    return 0;
}

Result I18nDateToIsoWeek(const I18nDate *date,
                                         int32_t *outWeekYear,
                                         uint8_t *outWeek,
                                         I18nWeekday *outWeekday)
{
    I18nWeekday wd;
    int64_t rd;
    int64_t thursdayRd;
    I18nDate thursday;
    I18nDate week1Thu;
    int64_t week1Rd;
    int64_t week;
    I18nWeekday w4;
    int64_t rd4;

    if (!outWeekYear || !outWeek || !outWeekday) {
        return RoboInvalidArgument;
    }
    if (I18nDateDayOfWeek(date, &wd) != RoboOk ||
        I18nDateToRataDie(date, &rd) != RoboOk) {
        return RoboInvalidArgument;
    }
    *outWeekday = wd;
    thursdayRd = rd + (4 - (int64_t)wd);
    if (I18nDateFromRataDie(thursdayRd, &thursday) != RoboOk) {
        return RoboNotSupported;
    }
    *outWeekYear = thursday.year;
    week1Thu.year = thursday.year;
    week1Thu.month = 1;
    week1Thu.day = 4;
    if (I18nDateDayOfWeek(&week1Thu, &w4) != RoboOk ||
        I18nDateToRataDie(&week1Thu, &rd4) != RoboOk) {
        return RoboInvalidArgument;
    }
    week1Rd = rd4 + (4 - (int64_t)w4);
    week = (thursdayRd - week1Rd) / 7 + 1;
    if (week < 1 || week > 53) {
        return RoboError;
    }
    *outWeek = (uint8_t)week;
    return RoboOk;
}

Result I18nDateFromIsoWeek(int32_t weekYear,
                                           uint8_t week,
                                           I18nWeekday weekday,
                                           I18nDate *out)
{
    I18nDate jan4;
    I18nWeekday w4;
    int64_t rd4;
    int64_t week1Monday;
    int64_t target;

    if (!out || week < 1 || week > 53 || weekday < 1 || weekday > 7) {
        return RoboInvalidArgument;
    }
    jan4.year = weekYear;
    jan4.month = 1;
    jan4.day = 4;
    if (I18nDateDayOfWeek(&jan4, &w4) != RoboOk ||
        I18nDateToRataDie(&jan4, &rd4) != RoboOk) {
        return RoboInvalidArgument;
    }
    week1Monday = rd4 - ((int64_t)w4 - 1);
    target = week1Monday + (int64_t)(week - 1) * 7 + ((int64_t)weekday - 1);
    return I18nDateFromRataDie(target, out);
}

int I18nTimeIsValid(const I18nTime *time)
{
    if (!time) {
        return 0;
    }
    if (time->hour > 23 || time->minute > 59 || time->second > 59) {
        return 0;
    }
    if (time->nano >= (uint32_t)RoboI18nTimeNanosPerSecond) {
        return 0;
    }
    return 1;
}

Result I18nTimeInit(I18nTime *out,
                                  uint8_t hour,
                                  uint8_t minute,
                                  uint8_t second,
                                  uint32_t nano)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->hour = hour;
    out->minute = minute;
    out->second = second;
    out->nano = nano;
    if (!I18nTimeIsValid(out)) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

Result I18nTimeToNanosOfDay(const I18nTime *time, int64_t *out)
{
    if (!time || !out || !I18nTimeIsValid(time)) {
        return RoboInvalidArgument;
    }
    *out = ((int64_t)time->hour * 3600 + (int64_t)time->minute * 60 + (int64_t)time->second) *
               (int64_t)RoboI18nTimeNanosPerSecond +
           (int64_t)time->nano;
    return RoboOk;
}

Result I18nTimeFromNanosOfDay(int64_t nanos, I18nTime *out)
{
    int64_t sec;
    if (!out || nanos < 0 ||
        nanos >= (int64_t)86400 * (int64_t)RoboI18nTimeNanosPerSecond) {
        return RoboInvalidArgument;
    }
    out->nano = (uint32_t)(nanos % RoboI18nTimeNanosPerSecond);
    sec = nanos / RoboI18nTimeNanosPerSecond;
    out->hour = (uint8_t)(sec / 3600);
    out->minute = (uint8_t)((sec % 3600) / 60);
    out->second = (uint8_t)(sec % 60);
    return RoboOk;
}

int I18nDatetimeIsValid(const I18nDatetime *dt)
{
    return dt && I18nDateIsValid(&dt->date) && I18nTimeIsValid(&dt->time);
}

Result I18nDatetimeInit(I18nDatetime *out,
                                      const I18nDate *date,
                                      const I18nTime *time)
{
    if (!out || !date || !time) {
        return RoboInvalidArgument;
    }
    if (!I18nDateIsValid(date) || !I18nTimeIsValid(time)) {
        return RoboInvalidArgument;
    }
    out->date = *date;
    out->time = *time;
    return RoboOk;
}

Result I18nInstantFromUnixMs(int64_t unixMs, I18nInstant *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->unixMs = unixMs;
    return RoboOk;
}

Result I18nInstantToUnixMs(const I18nInstant *in, int64_t *out)
{
    if (!in || !out) {
        return RoboInvalidArgument;
    }
    *out = in->unixMs;
    return RoboOk;
}

Result I18nInstantFromUnixS(int64_t unixS, I18nInstant *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (unixS > INT64_MAX / 1000 || unixS < INT64_MIN / 1000) {
        return RoboNotSupported;
    }
    out->unixMs = unixS * 1000;
    return RoboOk;
}

Result I18nInstantFromUtc(const I18nDatetime *utc,
                                         I18nInstant *out)
{
    int64_t days;
    int64_t nod;
    int64_t dayMs;
    if (!utc || !out || !I18nDatetimeIsValid(utc)) {
        return RoboInvalidArgument;
    }
    days = DaysFromCivil(utc->date.year, utc->date.month, utc->date.day);
    nod = ((int64_t)utc->time.hour * 3600 + (int64_t)utc->time.minute * 60 +
           (int64_t)utc->time.second) *
              1000 +
          (int64_t)(utc->time.nano / 1000000u);
    /* Safe multiply days * 86400000 without false negatives near epoch. */
    if (days > 0) {
        if (days > INT64_MAX / 86400000) {
            return RoboNotSupported;
        }
    } else if (days < 0) {
        if (days < INT64_MIN / 86400000) {
            return RoboNotSupported;
        }
    }
    dayMs = days * 86400000;
    if ((nod > 0 && dayMs > INT64_MAX - nod) || (nod < 0 && dayMs < INT64_MIN - nod)) {
        return RoboNotSupported;
    }
    out->unixMs = dayMs + nod;
    return RoboOk;
}

Result I18nInstantToUtc(const I18nInstant *in,
                                       I18nDatetime *outUtc)
{
    int64_t days;
    int64_t rem;
    int32_t y;
    unsigned m, d;
    int64_t sec;
    if (!in || !outUtc) {
        return RoboInvalidArgument;
    }
    days = in->unixMs / 86400000;
    rem = in->unixMs % 86400000;
    if (rem < 0) {
        rem += 86400000;
        days -= 1;
    }
    CivilFromDays(days, &y, &m, &d);
    if (!YearInRange(y)) {
        return RoboNotSupported;
    }
    outUtc->date.year = y;
    outUtc->date.month = (uint8_t)m;
    outUtc->date.day = (uint8_t)d;
    sec = rem / 1000;
    outUtc->time.hour = (uint8_t)(sec / 3600);
    outUtc->time.minute = (uint8_t)((sec % 3600) / 60);
    outUtc->time.second = (uint8_t)(sec % 60);
    outUtc->time.nano = (uint32_t)((rem % 1000) * 1000000u);
    return RoboOk;
}

Result I18nInstantAdd(const I18nInstant *in,
                                    const I18nDuration *dur,
                                    I18nInstant *out)
{
    if (!in || !dur || !out) {
        return RoboInvalidArgument;
    }
    if ((dur->millis > 0 && in->unixMs > INT64_MAX - dur->millis) ||
        (dur->millis < 0 && in->unixMs < INT64_MIN - dur->millis)) {
        return RoboNotSupported;
    }
    out->unixMs = in->unixMs + dur->millis;
    return RoboOk;
}

int I18nInstantCompare(const I18nInstant *a, const I18nInstant *b)
{
    if (!a || !b) {
        return 0;
    }
    if (a->unixMs < b->unixMs) {
        return -1;
    }
    if (a->unixMs > b->unixMs) {
        return 1;
    }
    return 0;
}

Result I18nDurationFromMillis(int64_t ms, I18nDuration *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->millis = ms;
    return RoboOk;
}

Result I18nDurationBetween(const I18nInstant *start,
                                         const I18nInstant *end,
                                         I18nDuration *out)
{
    if (!start || !end || !out) {
        return RoboInvalidArgument;
    }
    out->millis = end->unixMs - start->unixMs;
    return RoboOk;
}
