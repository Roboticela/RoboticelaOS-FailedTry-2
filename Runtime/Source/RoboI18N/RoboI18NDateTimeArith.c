/*
 * RoboI18N date/time arithmetic utilities (Goal 4.8).
 *
 * Algorithms:
 *   - Proleptic Gregorian month/year add with end-of-month CLAMP
 *     (same policy as Java.time ChronoUnit.MONTHS / ISO-8601 civil calendars)
 *   - ISO week: already via RoboI18NDateTime Rata Die helpers
 *   - Instant overflow: checked int64 add
 *   - Duration: fixed milliseconds; never equated to calendar days
 */

#include "RoboI18N/RoboI18NDateTimeArith.h"

#include <limits.h>
#include <string.h>

static int AddI32Checked(int32_t a, int32_t b, int32_t *out)
{
    int64_t s = (int64_t)a + (int64_t)b;
    if (s < INT32_MIN || s > INT32_MAX) {
        return 0;
    }
    *out = (int32_t)s;
    return 1;
}

static int AddI64Checked(int64_t a, int64_t b, int64_t *out)
{
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int MulI64Checked(int64_t a, int64_t b, int64_t *out)
{
    if (a == 0 || b == 0) {
        *out = 0;
        return 1;
    }
    if (a == INT64_MIN && b == -1) {
        return 0;
    }
    if (b == INT64_MIN && a == -1) {
        return 0;
    }
    if (a > 0) {
        if (b > 0) {
            if (a > INT64_MAX / b) {
                return 0;
            }
        } else {
            if (b < INT64_MIN / a) {
                return 0;
            }
        }
    } else {
        if (b > 0) {
            if (a < INT64_MIN / b) {
                return 0;
            }
        } else {
            if (a != 0 && b < INT64_MAX / a) {
                return 0;
            }
        }
    }
    *out = a * b;
    return 1;
}

void I18nPeriodInit(I18nPeriod *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

Result I18nPeriodOf(int32_t years,
                                  int32_t months,
                                  int32_t weeks,
                                  int32_t days,
                                  I18nPeriod *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->years = years;
    out->months = months;
    out->weeks = weeks;
    out->days = days;
    return RoboOk;
}

Result I18nPeriodAdd(const I18nPeriod *a,
                                   const I18nPeriod *b,
                                   I18nPeriod *out)
{
    if (!a || !b || !out) {
        return RoboInvalidArgument;
    }
    if (!AddI32Checked(a->years, b->years, &out->years) ||
        !AddI32Checked(a->months, b->months, &out->months) ||
        !AddI32Checked(a->weeks, b->weeks, &out->weeks) ||
        !AddI32Checked(a->days, b->days, &out->days)) {
        return RoboError;
    }
    return RoboOk;
}

Result I18nPeriodNegate(const I18nPeriod *in, I18nPeriod *out)
{
    if (!in || !out) {
        return RoboInvalidArgument;
    }
    if (in->years == INT32_MIN || in->months == INT32_MIN || in->weeks == INT32_MIN ||
        in->days == INT32_MIN) {
        return RoboError;
    }
    out->years = -in->years;
    out->months = -in->months;
    out->weeks = -in->weeks;
    out->days = -in->days;
    return RoboOk;
}

Result I18nDurationFromSeconds(int64_t seconds, I18nDuration *out)
{
    int64_t ms;
    if (!out || !MulI64Checked(seconds, 1000LL, &ms)) {
        return out ? RoboError : RoboInvalidArgument;
    }
    out->millis = ms;
    return RoboOk;
}

Result I18nDurationFromMinutes(int64_t minutes, I18nDuration *out)
{
    int64_t ms;
    if (!out || !MulI64Checked(minutes, 60000LL, &ms)) {
        return out ? RoboError : RoboInvalidArgument;
    }
    out->millis = ms;
    return RoboOk;
}

Result I18nDurationFromHours(int64_t hours, I18nDuration *out)
{
    int64_t ms;
    if (!out || !MulI64Checked(hours, 3600000LL, &ms)) {
        return out ? RoboError : RoboInvalidArgument;
    }
    out->millis = ms;
    return RoboOk;
}

Result I18nDurationFromDays(int64_t days, I18nDuration *out)
{
    int64_t ms;
    if (!out || !MulI64Checked(days, RoboI18nDurationMillisPerDay, &ms)) {
        return out ? RoboError : RoboInvalidArgument;
    }
    out->millis = ms;
    return RoboOk;
}

Result I18nDurationNegate(const I18nDuration *in,
                                        I18nDuration *out)
{
    if (!in || !out) {
        return RoboInvalidArgument;
    }
    if (in->millis == INT64_MIN) {
        return RoboError;
    }
    out->millis = -in->millis;
    return RoboOk;
}

Result I18nDurationAdd(const I18nDuration *a,
                                     const I18nDuration *b,
                                     I18nDuration *out)
{
    int64_t s;
    if (!a || !b || !out) {
        return RoboInvalidArgument;
    }
    if (!AddI64Checked(a->millis, b->millis, &s)) {
        return RoboError;
    }
    out->millis = s;
    return RoboOk;
}

int I18nDurationCompare(const I18nDuration *a, const I18nDuration *b)
{
    if (!a || !b) {
        return 0;
    }
    if (a->millis < b->millis) {
        return -1;
    }
    if (a->millis > b->millis) {
        return 1;
    }
    return 0;
}

Result I18nDateAddWeeks(const I18nDate *in,
                                       int32_t weeks,
                                       I18nDate *out)
{
    int64_t days;
    if (!MulI64Checked((int64_t)weeks, 7LL, &days)) {
        return RoboError;
    }
    if (days < INT32_MIN || days > INT32_MAX) {
        return RoboError;
    }
    return I18nDateAddDays(in, (int32_t)days, out);
}

Result I18nDateAddMonths(const I18nDate *in,
                                        int32_t months,
                                        I18nDate *out)
{
    int64_t total;
    int64_t y;
    int32_t m;
    uint8_t dim;

    if (!in || !out || !I18nDateIsValid(in)) {
        return RoboInvalidArgument;
    }
    total = (int64_t)in->year * 12LL + ((int64_t)in->month - 1LL) + (int64_t)months;
    y = total / 12;
    m = (int32_t)(total % 12);
    if (m < 0) {
        m += 12;
        y -= 1;
    }
    if (y < RoboI18nDateYearMin || y > RoboI18nDateYearMax) {
        return RoboError;
    }
    dim = I18nDateDaysInMonth((int32_t)y, (uint8_t)(m + 1));
    out->year = (int32_t)y;
    out->month = (uint8_t)(m + 1);
    out->day = in->day > dim ? dim : in->day; /* CLAMP */
    return RoboOk;
}

Result I18nDateAddYears(const I18nDate *in,
                                       int32_t years,
                                       I18nDate *out)
{
    int64_t months = (int64_t)years * 12LL;
    if (months < INT32_MIN || months > INT32_MAX) {
        return RoboError;
    }
    return I18nDateAddMonths(in, (int32_t)months, out);
}

Result I18nDateAddPeriod(const I18nDate *in,
                                        const I18nPeriod *period,
                                        I18nDate *out)
{
    I18nDate cur;
    Result r;
    int64_t ym;
    int32_t monthDelta;

    if (!in || !period || !out) {
        return RoboInvalidArgument;
    }
    ym = (int64_t)period->years * 12LL + (int64_t)period->months;
    if (ym < INT32_MIN || ym > INT32_MAX) {
        return RoboError;
    }
    monthDelta = (int32_t)ym;
    r = I18nDateAddMonths(in, monthDelta, &cur);
    if (r != RoboOk) {
        return r;
    }
    r = I18nDateAddWeeks(&cur, period->weeks, &cur);
    if (r != RoboOk) {
        return r;
    }
    return I18nDateAddDays(&cur, period->days, out);
}

Result I18nDateDifferenceDays(const I18nDate *start,
                                             const I18nDate *end,
                                             int32_t *outDays)
{
    int64_t a;
    int64_t b;
    int64_t d;
    if (!start || !end || !outDays) {
        return RoboInvalidArgument;
    }
    if (I18nDateToRataDie(start, &a) != RoboOk ||
        I18nDateToRataDie(end, &b) != RoboOk) {
        return RoboInvalidArgument;
    }
    d = b - a;
    if (d < INT32_MIN || d > INT32_MAX) {
        return RoboError;
    }
    *outDays = (int32_t)d;
    return RoboOk;
}

Result I18nDateDifferencePeriod(const I18nDate *start,
                                               const I18nDate *end,
                                               I18nPeriod *out)
{
    I18nDate cur;
    int32_t years = 0;
    int32_t months = 0;
    int32_t days = 0;
    int cmp;
    int sign = 1;

    if (!start || !end || !out) {
        return RoboInvalidArgument;
    }
    if (!I18nDateIsValid(start) || !I18nDateIsValid(end)) {
        return RoboInvalidArgument;
    }
    I18nPeriodInit(out);
    cmp = I18nDateCompare(start, end);
    if (cmp == 0) {
        return RoboOk;
    }
    if (cmp > 0) {
        sign = -1;
        /* Compute end→start then negate. */
        {
            const I18nDate *tmp = start;
            start = end;
            end = tmp;
        }
    }

    /* Years while start+years still ≤ end. */
    {
        I18nDate probe;
        int32_t y = 0;
        for (;;) {
            if (I18nDateAddYears(start, y + 1, &probe) != RoboOk) {
                break;
            }
            if (I18nDateCompare(&probe, end) > 0) {
                break;
            }
            y++;
            if (y > 20000) {
                return RoboError;
            }
        }
        years = y;
        if (I18nDateAddYears(start, years, &cur) != RoboOk) {
            return RoboError;
        }
    }
    {
        I18nDate probe;
        int32_t m = 0;
        for (;;) {
            if (I18nDateAddMonths(&cur, m + 1, &probe) != RoboOk) {
                break;
            }
            if (I18nDateCompare(&probe, end) > 0) {
                break;
            }
            m++;
            if (m > 24) {
                break;
            }
        }
        months = m;
        if (I18nDateAddMonths(&cur, months, &cur) != RoboOk) {
            return RoboError;
        }
    }
    if (I18nDateDifferenceDays(&cur, end, &days) != RoboOk) {
        return RoboError;
    }

    out->years = years * sign;
    out->months = months * sign;
    out->weeks = 0;
    out->days = days * sign;
    return RoboOk;
}

int32_t I18nDateDaysInYear(int32_t year)
{
    return I18nDateIsLeapYear(year) ? 366 : 365;
}

Result I18nInstantSubtract(const I18nInstant *in,
                                         const I18nDuration *dur,
                                         I18nInstant *out)
{
    I18nDuration neg;
    Result r = I18nDurationNegate(dur, &neg);
    if (r != RoboOk) {
        return r;
    }
    return I18nInstantAdd(in, &neg, out);
}

int I18nTimeCompare(const I18nTime *a, const I18nTime *b)
{
    int64_t na;
    int64_t nb;
    if (!a || !b) {
        return 0;
    }
    if (I18nTimeToNanosOfDay(a, &na) != RoboOk ||
        I18nTimeToNanosOfDay(b, &nb) != RoboOk) {
        return 0;
    }
    if (na < nb) {
        return -1;
    }
    if (na > nb) {
        return 1;
    }
    return 0;
}

int I18nDatetimeCompare(const I18nDatetime *a, const I18nDatetime *b)
{
    int c;
    if (!a || !b) {
        return 0;
    }
    c = I18nDateCompare(&a->date, &b->date);
    if (c != 0) {
        return c;
    }
    return I18nTimeCompare(&a->time, &b->time);
}

Result I18nIsoWeeksInYear(int32_t weekYear, uint8_t *outWeeks)
{
    I18nDate dec28;
    int32_t wy;
    uint8_t week;
    I18nWeekday wd;

    if (!outWeeks) {
        return RoboInvalidArgument;
    }
    /* Dec 28 is always in the last ISO week of weekYear. */
    if (I18nDateInit(&dec28, weekYear, 12, 28) != RoboOk) {
        return RoboInvalidArgument;
    }
    if (I18nDateToIsoWeek(&dec28, &wy, &week, &wd) != RoboOk) {
        return RoboError;
    }
    if (wy != weekYear) {
        return RoboError;
    }
    *outWeeks = week;
    return RoboOk;
}

Result I18nDateIsoWeekStart(const I18nDate *date,
                                            I18nDate *outMonday)
{
    I18nWeekday wd;
    if (!date || !outMonday) {
        return RoboInvalidArgument;
    }
    if (I18nDateDayOfWeek(date, &wd) != RoboOk) {
        return RoboInvalidArgument;
    }
    return I18nDateAddDays(date, 1 - (int32_t)wd, outMonday);
}

Result I18nDateIsoWeekEnd(const I18nDate *date,
                                          I18nDate *outSunday)
{
    I18nWeekday wd;
    if (!date || !outSunday) {
        return RoboInvalidArgument;
    }
    if (I18nDateDayOfWeek(date, &wd) != RoboOk) {
        return RoboInvalidArgument;
    }
    return I18nDateAddDays(date, 7 - (int32_t)wd, outSunday);
}

Result I18nDateStartOfMonth(const I18nDate *in, I18nDate *out)
{
    if (!in || !out || !I18nDateIsValid(in)) {
        return RoboInvalidArgument;
    }
    out->year = in->year;
    out->month = in->month;
    out->day = 1;
    return RoboOk;
}

Result I18nDateEndOfMonth(const I18nDate *in, I18nDate *out)
{
    if (!in || !out || !I18nDateIsValid(in)) {
        return RoboInvalidArgument;
    }
    out->year = in->year;
    out->month = in->month;
    out->day = I18nDateDaysInMonth(in->year, in->month);
    return RoboOk;
}

Result I18nDateStartOfYear(const I18nDate *in, I18nDate *out)
{
    if (!in || !out || !I18nDateIsValid(in)) {
        return RoboInvalidArgument;
    }
    return I18nDateInit(out, in->year, 1, 1);
}

Result I18nDateEndOfYear(const I18nDate *in, I18nDate *out)
{
    if (!in || !out || !I18nDateIsValid(in)) {
        return RoboInvalidArgument;
    }
    return I18nDateInit(out, in->year, 12, 31);
}

Result I18nDateStartOfQuarter(const I18nDate *in, I18nDate *out)
{
    uint8_t qstart;
    if (!in || !out || !I18nDateIsValid(in)) {
        return RoboInvalidArgument;
    }
    qstart = (uint8_t)((((in->month - 1) / 3) * 3) + 1);
    return I18nDateInit(out, in->year, qstart, 1);
}

Result I18nDateEndOfQuarter(const I18nDate *in, I18nDate *out)
{
    uint8_t qend;
    if (!in || !out || !I18nDateIsValid(in)) {
        return RoboInvalidArgument;
    }
    qend = (uint8_t)((((in->month - 1) / 3) * 3) + 3);
    out->year = in->year;
    out->month = qend;
    out->day = I18nDateDaysInMonth(in->year, qend);
    return RoboOk;
}

Result I18nDatetimeStartOfDay(const I18nDate *date,
                                              I18nDatetime *out)
{
    I18nTime t;
    if (!date || !out) {
        return RoboInvalidArgument;
    }
    if (I18nTimeInit(&t, 0, 0, 0, 0) != RoboOk) {
        return RoboError;
    }
    return I18nDatetimeInit(out, date, &t);
}

Result I18nDatetimeEndOfDay(const I18nDate *date,
                                            I18nDatetime *out)
{
    I18nTime t;
    if (!date || !out) {
        return RoboInvalidArgument;
    }
    if (I18nTimeInit(&t, 23, 59, 59, 999999999u) != RoboOk) {
        return RoboError;
    }
    return I18nDatetimeInit(out, date, &t);
}

Result I18nDateStartOfWeek(const I18nDate *in,
                                           const I18nLocale *locale,
                                           I18nDate *out)
{
    I18nWeekday first;
    I18nWeekday wd;
    int32_t delta;

    if (!in || !locale || !out) {
        return RoboInvalidArgument;
    }
    if (I18nLocaleFirstDayOfWeek(locale, &first) != RoboOk ||
        I18nDateDayOfWeek(in, &wd) != RoboOk) {
        return RoboInvalidArgument;
    }
    delta = ((int32_t)wd - (int32_t)first + 7) % 7;
    return I18nDateAddDays(in, -delta, out);
}

Result I18nDateEndOfWeek(const I18nDate *in,
                                         const I18nLocale *locale,
                                         I18nDate *out)
{
    I18nDate start;
    Result r = I18nDateStartOfWeek(in, locale, &start);
    if (r != RoboOk) {
        return r;
    }
    return I18nDateAddDays(&start, 6, out);
}

Result I18nInstantStartOfDay(const I18nInstant *instant,
                                             const I18nTimezone *tz,
                                             I18nInstant *out)
{
    I18nDatetime local;
    I18nDatetime midnight;
    I18nTzResolve resolve = RoboI18nTzUnique;
    Result r;

    if (!instant || !tz || !out) {
        return RoboInvalidArgument;
    }
    r = I18nTimezoneToLocal(tz, instant, &local, &resolve);
    if (r != RoboOk) {
        return r;
    }
    if (resolve != RoboI18nTzUnique) {
        return RoboNotSupported;
    }
    r = I18nDatetimeStartOfDay(&local.date, &midnight);
    if (r != RoboOk) {
        return r;
    }
    r = I18nTimezoneToUtc(tz, &midnight, out, &resolve);
    if (r != RoboOk) {
        return r;
    }
    if (resolve != RoboI18nTzUnique) {
        return RoboNotSupported;
    }
    return RoboOk;
}

Result I18nDatetimeAddPeriod(const I18nDatetime *in,
                                            const I18nPeriod *period,
                                            I18nDatetime *out)
{
    I18nDate d;
    Result r;
    if (!in || !period || !out) {
        return RoboInvalidArgument;
    }
    r = I18nDateAddPeriod(&in->date, period, &d);
    if (r != RoboOk) {
        return r;
    }
    return I18nDatetimeInit(out, &d, &in->time);
}

Result I18nInstantAddPeriod(const I18nInstant *in,
                                           const I18nPeriod *period,
                                           const I18nTimezone *tz,
                                           I18nInstant *out)
{
    I18nDatetime local;
    I18nDatetime next;
    I18nTzResolve resolve = RoboI18nTzUnique;
    Result r;

    if (!in || !period || !tz || !out) {
        return RoboInvalidArgument;
    }
    r = I18nTimezoneToLocal(tz, in, &local, &resolve);
    if (r != RoboOk) {
        return r;
    }
    if (resolve != RoboI18nTzUnique) {
        return RoboNotSupported;
    }
    r = I18nDatetimeAddPeriod(&local, period, &next);
    if (r != RoboOk) {
        return r;
    }
    r = I18nTimezoneToUtc(tz, &next, out, &resolve);
    if (r != RoboOk) {
        return r;
    }
    if (resolve != RoboI18nTzUnique) {
        return RoboNotSupported;
    }
    return RoboOk;
}

Result I18nCalendarAddPeriod(const I18nCalendar *cal,
                                            const I18nCalendarFields *in,
                                            const I18nPeriod *period,
                                            I18nCalendarFields *out)
{
    I18nDate g;
    I18nDate g2;
    Result r;

    if (!cal || !in || !period || !out) {
        return RoboInvalidArgument;
    }
    if (!I18nCalendarHasConversion(cal)) {
        return RoboNotSupported;
    }
    r = I18nCalendarToGregorian(cal, in, &g);
    if (r != RoboOk) {
        return r;
    }
    r = I18nDateAddPeriod(&g, period, &g2);
    if (r != RoboOk) {
        return r;
    }
    return I18nCalendarFromGregorian(cal, &g2, out);
}

void I18nBusinessCalendarInit(I18nBusinessCalendar *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    /* Default Sat+Sun weekend: bits 5 and 6. */
    out->weekendBits = (uint8_t)((1u << 5) | (1u << 6));
}

Result I18nBusinessCalendarFromLocale(const I18nLocale *locale,
                                                      I18nBusinessCalendar *out)
{
    I18nWeekday ws;
    I18nWeekday we;
    I18nWeekday cur;
    uint8_t bits = 0;

    if (!locale || !out) {
        return RoboInvalidArgument;
    }
    I18nBusinessCalendarInit(out);
    if (I18nLocaleWeekend(locale, &ws, &we) != RoboOk) {
        return RoboNotFound;
    }
    cur = ws;
    for (;;) {
        bits = (uint8_t)(bits | (uint8_t)(1u << ((unsigned)cur - 1u)));
        if (cur == we) {
            break;
        }
        cur = (I18nWeekday)((cur % 7) + 1);
    }
    out->weekendBits = bits;
    return RoboOk;
}

int I18nDateIsWeekend(const I18nDate *date,
                              const I18nBusinessCalendar *biz)
{
    I18nWeekday wd;
    if (!date || !biz || I18nDateDayOfWeek(date, &wd) != RoboOk) {
        return 0;
    }
    return (biz->weekendBits & (uint8_t)(1u << ((unsigned)wd - 1u))) != 0;
}

int I18nDateIsBusinessDay(const I18nDate *date,
                                   const I18nBusinessCalendar *biz)
{
    if (!date || !biz) {
        return 0;
    }
    if (I18nDateIsWeekend(date, biz)) {
        return 0;
    }
    if (biz->IsHoliday && biz->IsHoliday(date, biz->holidayCtx)) {
        return 0;
    }
    return 1;
}

Result I18nDateAddBusinessDays(const I18nDate *in,
                                               int32_t n,
                                               const I18nBusinessCalendar *biz,
                                               I18nDate *out)
{
    I18nDate cur;
    int32_t step;
    int32_t remaining;
    int guard = 0;

    if (!in || !biz || !out || !I18nDateIsValid(in)) {
        return RoboInvalidArgument;
    }
    cur = *in;
    if (n == 0) {
        *out = cur;
        return RoboOk;
    }
    step = n > 0 ? 1 : -1;
    remaining = n > 0 ? n : -n;
    while (remaining > 0) {
        if (I18nDateAddDays(&cur, step, &cur) != RoboOk) {
            return RoboError;
        }
        if (I18nDateIsBusinessDay(&cur, biz)) {
            remaining--;
        }
        if (++guard > 1000000) {
            return RoboError;
        }
    }
    *out = cur;
    return RoboOk;
}
