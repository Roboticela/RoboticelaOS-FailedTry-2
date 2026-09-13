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

static int add_i32_checked(int32_t a, int32_t b, int32_t *out)
{
    int64_t s = (int64_t)a + (int64_t)b;
    if (s < INT32_MIN || s > INT32_MAX) {
        return 0;
    }
    *out = (int32_t)s;
    return 1;
}

static int add_i64_checked(int64_t a, int64_t b, int64_t *out)
{
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int mul_i64_checked(int64_t a, int64_t b, int64_t *out)
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

void robo_i18n_period_init(robo_i18n_period_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

robo_result_t robo_i18n_period_of(int32_t years,
                                  int32_t months,
                                  int32_t weeks,
                                  int32_t days,
                                  robo_i18n_period_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->years = years;
    out->months = months;
    out->weeks = weeks;
    out->days = days;
    return ROBO_OK;
}

robo_result_t robo_i18n_period_add(const robo_i18n_period_t *a,
                                   const robo_i18n_period_t *b,
                                   robo_i18n_period_t *out)
{
    if (!a || !b || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!add_i32_checked(a->years, b->years, &out->years) ||
        !add_i32_checked(a->months, b->months, &out->months) ||
        !add_i32_checked(a->weeks, b->weeks, &out->weeks) ||
        !add_i32_checked(a->days, b->days, &out->days)) {
        return ROBO_ERROR;
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_period_negate(const robo_i18n_period_t *in, robo_i18n_period_t *out)
{
    if (!in || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (in->years == INT32_MIN || in->months == INT32_MIN || in->weeks == INT32_MIN ||
        in->days == INT32_MIN) {
        return ROBO_ERROR;
    }
    out->years = -in->years;
    out->months = -in->months;
    out->weeks = -in->weeks;
    out->days = -in->days;
    return ROBO_OK;
}

robo_result_t robo_i18n_duration_from_seconds(int64_t seconds, robo_i18n_duration_t *out)
{
    int64_t ms;
    if (!out || !mul_i64_checked(seconds, 1000LL, &ms)) {
        return out ? ROBO_ERROR : ROBO_INVALID_ARGUMENT;
    }
    out->millis = ms;
    return ROBO_OK;
}

robo_result_t robo_i18n_duration_from_minutes(int64_t minutes, robo_i18n_duration_t *out)
{
    int64_t ms;
    if (!out || !mul_i64_checked(minutes, 60000LL, &ms)) {
        return out ? ROBO_ERROR : ROBO_INVALID_ARGUMENT;
    }
    out->millis = ms;
    return ROBO_OK;
}

robo_result_t robo_i18n_duration_from_hours(int64_t hours, robo_i18n_duration_t *out)
{
    int64_t ms;
    if (!out || !mul_i64_checked(hours, 3600000LL, &ms)) {
        return out ? ROBO_ERROR : ROBO_INVALID_ARGUMENT;
    }
    out->millis = ms;
    return ROBO_OK;
}

robo_result_t robo_i18n_duration_from_days(int64_t days, robo_i18n_duration_t *out)
{
    int64_t ms;
    if (!out || !mul_i64_checked(days, ROBO_I18N_DURATION_MILLIS_PER_DAY, &ms)) {
        return out ? ROBO_ERROR : ROBO_INVALID_ARGUMENT;
    }
    out->millis = ms;
    return ROBO_OK;
}

robo_result_t robo_i18n_duration_negate(const robo_i18n_duration_t *in,
                                        robo_i18n_duration_t *out)
{
    if (!in || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (in->millis == INT64_MIN) {
        return ROBO_ERROR;
    }
    out->millis = -in->millis;
    return ROBO_OK;
}

robo_result_t robo_i18n_duration_add(const robo_i18n_duration_t *a,
                                     const robo_i18n_duration_t *b,
                                     robo_i18n_duration_t *out)
{
    int64_t s;
    if (!a || !b || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!add_i64_checked(a->millis, b->millis, &s)) {
        return ROBO_ERROR;
    }
    out->millis = s;
    return ROBO_OK;
}

int robo_i18n_duration_compare(const robo_i18n_duration_t *a, const robo_i18n_duration_t *b)
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

robo_result_t robo_i18n_date_add_weeks(const robo_i18n_date_t *in,
                                       int32_t weeks,
                                       robo_i18n_date_t *out)
{
    int64_t days;
    if (!mul_i64_checked((int64_t)weeks, 7LL, &days)) {
        return ROBO_ERROR;
    }
    if (days < INT32_MIN || days > INT32_MAX) {
        return ROBO_ERROR;
    }
    return robo_i18n_date_add_days(in, (int32_t)days, out);
}

robo_result_t robo_i18n_date_add_months(const robo_i18n_date_t *in,
                                        int32_t months,
                                        robo_i18n_date_t *out)
{
    int64_t total;
    int64_t y;
    int32_t m;
    uint8_t dim;

    if (!in || !out || !robo_i18n_date_is_valid(in)) {
        return ROBO_INVALID_ARGUMENT;
    }
    total = (int64_t)in->year * 12LL + ((int64_t)in->month - 1LL) + (int64_t)months;
    y = total / 12;
    m = (int32_t)(total % 12);
    if (m < 0) {
        m += 12;
        y -= 1;
    }
    if (y < ROBO_I18N_DATE_YEAR_MIN || y > ROBO_I18N_DATE_YEAR_MAX) {
        return ROBO_ERROR;
    }
    dim = robo_i18n_date_days_in_month((int32_t)y, (uint8_t)(m + 1));
    out->year = (int32_t)y;
    out->month = (uint8_t)(m + 1);
    out->day = in->day > dim ? dim : in->day; /* CLAMP */
    return ROBO_OK;
}

robo_result_t robo_i18n_date_add_years(const robo_i18n_date_t *in,
                                       int32_t years,
                                       robo_i18n_date_t *out)
{
    int64_t months = (int64_t)years * 12LL;
    if (months < INT32_MIN || months > INT32_MAX) {
        return ROBO_ERROR;
    }
    return robo_i18n_date_add_months(in, (int32_t)months, out);
}

robo_result_t robo_i18n_date_add_period(const robo_i18n_date_t *in,
                                        const robo_i18n_period_t *period,
                                        robo_i18n_date_t *out)
{
    robo_i18n_date_t cur;
    robo_result_t r;
    int64_t ym;
    int32_t month_delta;

    if (!in || !period || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    ym = (int64_t)period->years * 12LL + (int64_t)period->months;
    if (ym < INT32_MIN || ym > INT32_MAX) {
        return ROBO_ERROR;
    }
    month_delta = (int32_t)ym;
    r = robo_i18n_date_add_months(in, month_delta, &cur);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_i18n_date_add_weeks(&cur, period->weeks, &cur);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_date_add_days(&cur, period->days, out);
}

robo_result_t robo_i18n_date_difference_days(const robo_i18n_date_t *start,
                                             const robo_i18n_date_t *end,
                                             int32_t *out_days)
{
    int64_t a;
    int64_t b;
    int64_t d;
    if (!start || !end || !out_days) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_i18n_date_to_rata_die(start, &a) != ROBO_OK ||
        robo_i18n_date_to_rata_die(end, &b) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    d = b - a;
    if (d < INT32_MIN || d > INT32_MAX) {
        return ROBO_ERROR;
    }
    *out_days = (int32_t)d;
    return ROBO_OK;
}

robo_result_t robo_i18n_date_difference_period(const robo_i18n_date_t *start,
                                               const robo_i18n_date_t *end,
                                               robo_i18n_period_t *out)
{
    robo_i18n_date_t cur;
    int32_t years = 0;
    int32_t months = 0;
    int32_t days = 0;
    int cmp;
    int sign = 1;

    if (!start || !end || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_i18n_date_is_valid(start) || !robo_i18n_date_is_valid(end)) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_i18n_period_init(out);
    cmp = robo_i18n_date_compare(start, end);
    if (cmp == 0) {
        return ROBO_OK;
    }
    if (cmp > 0) {
        sign = -1;
        /* Compute end→start then negate. */
        {
            const robo_i18n_date_t *tmp = start;
            start = end;
            end = tmp;
        }
    }

    /* Years while start+years still ≤ end. */
    {
        robo_i18n_date_t probe;
        int32_t y = 0;
        for (;;) {
            if (robo_i18n_date_add_years(start, y + 1, &probe) != ROBO_OK) {
                break;
            }
            if (robo_i18n_date_compare(&probe, end) > 0) {
                break;
            }
            y++;
            if (y > 20000) {
                return ROBO_ERROR;
            }
        }
        years = y;
        if (robo_i18n_date_add_years(start, years, &cur) != ROBO_OK) {
            return ROBO_ERROR;
        }
    }
    {
        robo_i18n_date_t probe;
        int32_t m = 0;
        for (;;) {
            if (robo_i18n_date_add_months(&cur, m + 1, &probe) != ROBO_OK) {
                break;
            }
            if (robo_i18n_date_compare(&probe, end) > 0) {
                break;
            }
            m++;
            if (m > 24) {
                break;
            }
        }
        months = m;
        if (robo_i18n_date_add_months(&cur, months, &cur) != ROBO_OK) {
            return ROBO_ERROR;
        }
    }
    if (robo_i18n_date_difference_days(&cur, end, &days) != ROBO_OK) {
        return ROBO_ERROR;
    }

    out->years = years * sign;
    out->months = months * sign;
    out->weeks = 0;
    out->days = days * sign;
    return ROBO_OK;
}

int32_t robo_i18n_date_days_in_year(int32_t year)
{
    return robo_i18n_date_is_leap_year(year) ? 366 : 365;
}

robo_result_t robo_i18n_instant_subtract(const robo_i18n_instant_t *in,
                                         const robo_i18n_duration_t *dur,
                                         robo_i18n_instant_t *out)
{
    robo_i18n_duration_t neg;
    robo_result_t r = robo_i18n_duration_negate(dur, &neg);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_instant_add(in, &neg, out);
}

int robo_i18n_time_compare(const robo_i18n_time_t *a, const robo_i18n_time_t *b)
{
    int64_t na;
    int64_t nb;
    if (!a || !b) {
        return 0;
    }
    if (robo_i18n_time_to_nanos_of_day(a, &na) != ROBO_OK ||
        robo_i18n_time_to_nanos_of_day(b, &nb) != ROBO_OK) {
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

int robo_i18n_datetime_compare(const robo_i18n_datetime_t *a, const robo_i18n_datetime_t *b)
{
    int c;
    if (!a || !b) {
        return 0;
    }
    c = robo_i18n_date_compare(&a->date, &b->date);
    if (c != 0) {
        return c;
    }
    return robo_i18n_time_compare(&a->time, &b->time);
}

robo_result_t robo_i18n_iso_weeks_in_year(int32_t week_year, uint8_t *out_weeks)
{
    robo_i18n_date_t dec28;
    int32_t wy;
    uint8_t week;
    robo_i18n_weekday_t wd;

    if (!out_weeks) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Dec 28 is always in the last ISO week of week_year. */
    if (robo_i18n_date_init(&dec28, week_year, 12, 28) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_i18n_date_to_iso_week(&dec28, &wy, &week, &wd) != ROBO_OK) {
        return ROBO_ERROR;
    }
    if (wy != week_year) {
        return ROBO_ERROR;
    }
    *out_weeks = week;
    return ROBO_OK;
}

robo_result_t robo_i18n_date_iso_week_start(const robo_i18n_date_t *date,
                                            robo_i18n_date_t *out_monday)
{
    robo_i18n_weekday_t wd;
    if (!date || !out_monday) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_i18n_date_day_of_week(date, &wd) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_i18n_date_add_days(date, 1 - (int32_t)wd, out_monday);
}

robo_result_t robo_i18n_date_iso_week_end(const robo_i18n_date_t *date,
                                          robo_i18n_date_t *out_sunday)
{
    robo_i18n_weekday_t wd;
    if (!date || !out_sunday) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_i18n_date_day_of_week(date, &wd) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_i18n_date_add_days(date, 7 - (int32_t)wd, out_sunday);
}

robo_result_t robo_i18n_date_start_of_month(const robo_i18n_date_t *in, robo_i18n_date_t *out)
{
    if (!in || !out || !robo_i18n_date_is_valid(in)) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->year = in->year;
    out->month = in->month;
    out->day = 1;
    return ROBO_OK;
}

robo_result_t robo_i18n_date_end_of_month(const robo_i18n_date_t *in, robo_i18n_date_t *out)
{
    if (!in || !out || !robo_i18n_date_is_valid(in)) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->year = in->year;
    out->month = in->month;
    out->day = robo_i18n_date_days_in_month(in->year, in->month);
    return ROBO_OK;
}

robo_result_t robo_i18n_date_start_of_year(const robo_i18n_date_t *in, robo_i18n_date_t *out)
{
    if (!in || !out || !robo_i18n_date_is_valid(in)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_i18n_date_init(out, in->year, 1, 1);
}

robo_result_t robo_i18n_date_end_of_year(const robo_i18n_date_t *in, robo_i18n_date_t *out)
{
    if (!in || !out || !robo_i18n_date_is_valid(in)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_i18n_date_init(out, in->year, 12, 31);
}

robo_result_t robo_i18n_date_start_of_quarter(const robo_i18n_date_t *in, robo_i18n_date_t *out)
{
    uint8_t qstart;
    if (!in || !out || !robo_i18n_date_is_valid(in)) {
        return ROBO_INVALID_ARGUMENT;
    }
    qstart = (uint8_t)((((in->month - 1) / 3) * 3) + 1);
    return robo_i18n_date_init(out, in->year, qstart, 1);
}

robo_result_t robo_i18n_date_end_of_quarter(const robo_i18n_date_t *in, robo_i18n_date_t *out)
{
    uint8_t qend;
    if (!in || !out || !robo_i18n_date_is_valid(in)) {
        return ROBO_INVALID_ARGUMENT;
    }
    qend = (uint8_t)((((in->month - 1) / 3) * 3) + 3);
    out->year = in->year;
    out->month = qend;
    out->day = robo_i18n_date_days_in_month(in->year, qend);
    return ROBO_OK;
}

robo_result_t robo_i18n_datetime_start_of_day(const robo_i18n_date_t *date,
                                              robo_i18n_datetime_t *out)
{
    robo_i18n_time_t t;
    if (!date || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_i18n_time_init(&t, 0, 0, 0, 0) != ROBO_OK) {
        return ROBO_ERROR;
    }
    return robo_i18n_datetime_init(out, date, &t);
}

robo_result_t robo_i18n_datetime_end_of_day(const robo_i18n_date_t *date,
                                            robo_i18n_datetime_t *out)
{
    robo_i18n_time_t t;
    if (!date || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_i18n_time_init(&t, 23, 59, 59, 999999999u) != ROBO_OK) {
        return ROBO_ERROR;
    }
    return robo_i18n_datetime_init(out, date, &t);
}

robo_result_t robo_i18n_date_start_of_week(const robo_i18n_date_t *in,
                                           const robo_i18n_locale_t *locale,
                                           robo_i18n_date_t *out)
{
    robo_i18n_weekday_t first;
    robo_i18n_weekday_t wd;
    int32_t delta;

    if (!in || !locale || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_i18n_locale_first_day_of_week(locale, &first) != ROBO_OK ||
        robo_i18n_date_day_of_week(in, &wd) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    delta = ((int32_t)wd - (int32_t)first + 7) % 7;
    return robo_i18n_date_add_days(in, -delta, out);
}

robo_result_t robo_i18n_date_end_of_week(const robo_i18n_date_t *in,
                                         const robo_i18n_locale_t *locale,
                                         robo_i18n_date_t *out)
{
    robo_i18n_date_t start;
    robo_result_t r = robo_i18n_date_start_of_week(in, locale, &start);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_date_add_days(&start, 6, out);
}

robo_result_t robo_i18n_instant_start_of_day(const robo_i18n_instant_t *instant,
                                             const robo_i18n_timezone_t *tz,
                                             robo_i18n_instant_t *out)
{
    robo_i18n_datetime_t local;
    robo_i18n_datetime_t midnight;
    robo_i18n_tz_resolve_t resolve = ROBO_I18N_TZ_UNIQUE;
    robo_result_t r;

    if (!instant || !tz || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_timezone_to_local(tz, instant, &local, &resolve);
    if (r != ROBO_OK) {
        return r;
    }
    if (resolve != ROBO_I18N_TZ_UNIQUE) {
        return ROBO_NOT_SUPPORTED;
    }
    r = robo_i18n_datetime_start_of_day(&local.date, &midnight);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_i18n_timezone_to_utc(tz, &midnight, out, &resolve);
    if (r != ROBO_OK) {
        return r;
    }
    if (resolve != ROBO_I18N_TZ_UNIQUE) {
        return ROBO_NOT_SUPPORTED;
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_datetime_add_period(const robo_i18n_datetime_t *in,
                                            const robo_i18n_period_t *period,
                                            robo_i18n_datetime_t *out)
{
    robo_i18n_date_t d;
    robo_result_t r;
    if (!in || !period || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_date_add_period(&in->date, period, &d);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_datetime_init(out, &d, &in->time);
}

robo_result_t robo_i18n_instant_add_period(const robo_i18n_instant_t *in,
                                           const robo_i18n_period_t *period,
                                           const robo_i18n_timezone_t *tz,
                                           robo_i18n_instant_t *out)
{
    robo_i18n_datetime_t local;
    robo_i18n_datetime_t next;
    robo_i18n_tz_resolve_t resolve = ROBO_I18N_TZ_UNIQUE;
    robo_result_t r;

    if (!in || !period || !tz || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_timezone_to_local(tz, in, &local, &resolve);
    if (r != ROBO_OK) {
        return r;
    }
    if (resolve != ROBO_I18N_TZ_UNIQUE) {
        return ROBO_NOT_SUPPORTED;
    }
    r = robo_i18n_datetime_add_period(&local, period, &next);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_i18n_timezone_to_utc(tz, &next, out, &resolve);
    if (r != ROBO_OK) {
        return r;
    }
    if (resolve != ROBO_I18N_TZ_UNIQUE) {
        return ROBO_NOT_SUPPORTED;
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_calendar_add_period(const robo_i18n_calendar_t *cal,
                                            const robo_i18n_calendar_fields_t *in,
                                            const robo_i18n_period_t *period,
                                            robo_i18n_calendar_fields_t *out)
{
    robo_i18n_date_t g;
    robo_i18n_date_t g2;
    robo_result_t r;

    if (!cal || !in || !period || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_i18n_calendar_has_conversion(cal)) {
        return ROBO_NOT_SUPPORTED;
    }
    r = robo_i18n_calendar_to_gregorian(cal, in, &g);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_i18n_date_add_period(&g, period, &g2);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_calendar_from_gregorian(cal, &g2, out);
}

void robo_i18n_business_calendar_init(robo_i18n_business_calendar_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    /* Default Sat+Sun weekend: bits 5 and 6. */
    out->weekend_bits = (uint8_t)((1u << 5) | (1u << 6));
}

robo_result_t robo_i18n_business_calendar_from_locale(const robo_i18n_locale_t *locale,
                                                      robo_i18n_business_calendar_t *out)
{
    robo_i18n_weekday_t ws;
    robo_i18n_weekday_t we;
    robo_i18n_weekday_t cur;
    uint8_t bits = 0;

    if (!locale || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_i18n_business_calendar_init(out);
    if (robo_i18n_locale_weekend(locale, &ws, &we) != ROBO_OK) {
        return ROBO_NOT_FOUND;
    }
    cur = ws;
    for (;;) {
        bits = (uint8_t)(bits | (uint8_t)(1u << ((unsigned)cur - 1u)));
        if (cur == we) {
            break;
        }
        cur = (robo_i18n_weekday_t)((cur % 7) + 1);
    }
    out->weekend_bits = bits;
    return ROBO_OK;
}

int robo_i18n_date_is_weekend(const robo_i18n_date_t *date,
                              const robo_i18n_business_calendar_t *biz)
{
    robo_i18n_weekday_t wd;
    if (!date || !biz || robo_i18n_date_day_of_week(date, &wd) != ROBO_OK) {
        return 0;
    }
    return (biz->weekend_bits & (uint8_t)(1u << ((unsigned)wd - 1u))) != 0;
}

int robo_i18n_date_is_business_day(const robo_i18n_date_t *date,
                                   const robo_i18n_business_calendar_t *biz)
{
    if (!date || !biz) {
        return 0;
    }
    if (robo_i18n_date_is_weekend(date, biz)) {
        return 0;
    }
    if (biz->is_holiday && biz->is_holiday(date, biz->holiday_ctx)) {
        return 0;
    }
    return 1;
}

robo_result_t robo_i18n_date_add_business_days(const robo_i18n_date_t *in,
                                               int32_t n,
                                               const robo_i18n_business_calendar_t *biz,
                                               robo_i18n_date_t *out)
{
    robo_i18n_date_t cur;
    int32_t step;
    int32_t remaining;
    int guard = 0;

    if (!in || !biz || !out || !robo_i18n_date_is_valid(in)) {
        return ROBO_INVALID_ARGUMENT;
    }
    cur = *in;
    if (n == 0) {
        *out = cur;
        return ROBO_OK;
    }
    step = n > 0 ? 1 : -1;
    remaining = n > 0 ? n : -n;
    while (remaining > 0) {
        if (robo_i18n_date_add_days(&cur, step, &cur) != ROBO_OK) {
            return ROBO_ERROR;
        }
        if (robo_i18n_date_is_business_day(&cur, biz)) {
            remaining--;
        }
        if (++guard > 1000000) {
            return ROBO_ERROR;
        }
    }
    *out = cur;
    return ROBO_OK;
}
