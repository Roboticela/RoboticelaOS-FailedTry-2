/*
 * Proleptic Gregorian date/time/instant primitives (Goal 4.5).
 * Civil algorithms follow publicly documented rata-die methods
 * (e.g. Howard Hinnant); not copied from Chromium/ICU.
 */

#include "RoboI18N/RoboI18NDateTime.h"

#include <limits.h>

static int year_in_range(int32_t y)
{
    return y >= ROBO_I18N_DATE_YEAR_MIN && y <= ROBO_I18N_DATE_YEAR_MAX;
}

int robo_i18n_date_is_leap_year(int32_t year)
{
    if (year % 4 != 0) {
        return 0;
    }
    if (year % 100 != 0) {
        return 1;
    }
    return (year % 400 == 0) ? 1 : 0;
}

uint8_t robo_i18n_date_days_in_month(int32_t year, uint8_t month)
{
    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && robo_i18n_date_is_leap_year(year)) {
        return 29;
    }
    return mdays[month - 1u];
}

int robo_i18n_date_is_valid(const robo_i18n_date_t *date)
{
    uint8_t dim;
    if (!date || !year_in_range(date->year)) {
        return 0;
    }
    if (date->month < 1 || date->month > 12) {
        return 0;
    }
    dim = robo_i18n_date_days_in_month(date->year, date->month);
    return date->day >= 1 && date->day <= dim;
}

robo_result_t robo_i18n_date_init(robo_i18n_date_t *out, int32_t year, uint8_t month, uint8_t day)
{
    return robo_i18n_date_init_checked(out, year, month, day, 0);
}

/*
 * Days since 1970-01-01 (Hinnant days_from_civil).
 */
static int64_t days_from_civil(int32_t y, unsigned m, unsigned d)
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

static void civil_from_days(int64_t z, int32_t *y, unsigned *m, unsigned *d)
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
static int64_t rd_from_civil(int32_t y, unsigned m, unsigned d)
{
    return days_from_civil(y, m, d) - days_from_civil(1, 1, 1) + 1;
}

static void civil_from_rd(int64_t rd, int32_t *y, unsigned *m, unsigned *d)
{
    civil_from_days(rd - 1 + days_from_civil(1, 1, 1), y, m, d);
}

robo_result_t robo_i18n_date_init_checked(robo_i18n_date_t *out,
                                          int32_t year,
                                          uint8_t month,
                                          uint8_t day,
                                          int normalize)
{
    robo_i18n_date_t tmp;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!year_in_range(year) || month < 1 || month > 12 || day < 1) {
        return ROBO_INVALID_ARGUMENT;
    }
    tmp.year = year;
    tmp.month = month;
    tmp.day = day;
    if (robo_i18n_date_is_valid(&tmp)) {
        *out = tmp;
        return ROBO_OK;
    }
    if (!normalize) {
        return ROBO_INVALID_ARGUMENT;
    }
    {
        int64_t rd;
        robo_i18n_date_t base;
        base.year = year;
        base.month = month;
        base.day = 1;
        if (!robo_i18n_date_is_valid(&base)) {
            return ROBO_INVALID_ARGUMENT;
        }
        rd = rd_from_civil(base.year, base.month, base.day);
        return robo_i18n_date_from_rata_die(rd + (int64_t)day - 1, out);
    }
}

robo_result_t robo_i18n_date_to_rata_die(const robo_i18n_date_t *date, int64_t *out_rd)
{
    if (!date || !out_rd || !robo_i18n_date_is_valid(date)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_rd = rd_from_civil(date->year, date->month, date->day);
    return ROBO_OK;
}

robo_result_t robo_i18n_date_from_rata_die(int64_t rd, robo_i18n_date_t *out)
{
    int32_t y;
    unsigned m, d;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    civil_from_rd(rd, &y, &m, &d);
    if (!year_in_range(y)) {
        return ROBO_NOT_SUPPORTED;
    }
    out->year = y;
    out->month = (uint8_t)m;
    out->day = (uint8_t)d;
    return ROBO_OK;
}

robo_result_t robo_i18n_date_day_of_week(const robo_i18n_date_t *date,
                                         robo_i18n_weekday_t *out)
{
    int64_t rd;
    int64_t z;
    if (!out || robo_i18n_date_to_rata_die(date, &rd) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* 0001-01-01 was Monday. */
    z = (rd - 1) % 7;
    if (z < 0) {
        z += 7;
    }
    *out = (robo_i18n_weekday_t)(z + 1);
    return ROBO_OK;
}

robo_result_t robo_i18n_date_day_of_year(const robo_i18n_date_t *date, int32_t *out)
{
    robo_i18n_date_t jan1;
    int64_t a, b;
    if (!out || !robo_i18n_date_is_valid(date)) {
        return ROBO_INVALID_ARGUMENT;
    }
    jan1.year = date->year;
    jan1.month = 1;
    jan1.day = 1;
    if (robo_i18n_date_to_rata_die(&jan1, &a) != ROBO_OK ||
        robo_i18n_date_to_rata_die(date, &b) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = (int32_t)(b - a + 1);
    return ROBO_OK;
}

robo_result_t robo_i18n_date_add_days(const robo_i18n_date_t *in,
                                      int32_t days,
                                      robo_i18n_date_t *out)
{
    int64_t rd;
    if (!out || robo_i18n_date_to_rata_die(in, &rd) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    if ((days > 0 && rd > INT64_MAX - days) || (days < 0 && rd < INT64_MIN - days)) {
        return ROBO_NOT_SUPPORTED;
    }
    return robo_i18n_date_from_rata_die(rd + days, out);
}

int robo_i18n_date_compare(const robo_i18n_date_t *a, const robo_i18n_date_t *b)
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

robo_result_t robo_i18n_date_to_iso_week(const robo_i18n_date_t *date,
                                         int32_t *out_week_year,
                                         uint8_t *out_week,
                                         robo_i18n_weekday_t *out_weekday)
{
    robo_i18n_weekday_t wd;
    int64_t rd;
    int64_t thursday_rd;
    robo_i18n_date_t thursday;
    robo_i18n_date_t week1_thu;
    int64_t week1_rd;
    int64_t week;
    robo_i18n_weekday_t w4;
    int64_t rd4;

    if (!out_week_year || !out_week || !out_weekday) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_i18n_date_day_of_week(date, &wd) != ROBO_OK ||
        robo_i18n_date_to_rata_die(date, &rd) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_weekday = wd;
    thursday_rd = rd + (4 - (int64_t)wd);
    if (robo_i18n_date_from_rata_die(thursday_rd, &thursday) != ROBO_OK) {
        return ROBO_NOT_SUPPORTED;
    }
    *out_week_year = thursday.year;
    week1_thu.year = thursday.year;
    week1_thu.month = 1;
    week1_thu.day = 4;
    if (robo_i18n_date_day_of_week(&week1_thu, &w4) != ROBO_OK ||
        robo_i18n_date_to_rata_die(&week1_thu, &rd4) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    week1_rd = rd4 + (4 - (int64_t)w4);
    week = (thursday_rd - week1_rd) / 7 + 1;
    if (week < 1 || week > 53) {
        return ROBO_ERROR;
    }
    *out_week = (uint8_t)week;
    return ROBO_OK;
}

robo_result_t robo_i18n_date_from_iso_week(int32_t week_year,
                                           uint8_t week,
                                           robo_i18n_weekday_t weekday,
                                           robo_i18n_date_t *out)
{
    robo_i18n_date_t jan4;
    robo_i18n_weekday_t w4;
    int64_t rd4;
    int64_t week1_monday;
    int64_t target;

    if (!out || week < 1 || week > 53 || weekday < 1 || weekday > 7) {
        return ROBO_INVALID_ARGUMENT;
    }
    jan4.year = week_year;
    jan4.month = 1;
    jan4.day = 4;
    if (robo_i18n_date_day_of_week(&jan4, &w4) != ROBO_OK ||
        robo_i18n_date_to_rata_die(&jan4, &rd4) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    week1_monday = rd4 - ((int64_t)w4 - 1);
    target = week1_monday + (int64_t)(week - 1) * 7 + ((int64_t)weekday - 1);
    return robo_i18n_date_from_rata_die(target, out);
}

int robo_i18n_time_is_valid(const robo_i18n_time_t *time)
{
    if (!time) {
        return 0;
    }
    if (time->hour > 23 || time->minute > 59 || time->second > 59) {
        return 0;
    }
    if (time->nano >= (uint32_t)ROBO_I18N_TIME_NANOS_PER_SECOND) {
        return 0;
    }
    return 1;
}

robo_result_t robo_i18n_time_init(robo_i18n_time_t *out,
                                  uint8_t hour,
                                  uint8_t minute,
                                  uint8_t second,
                                  uint32_t nano)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->hour = hour;
    out->minute = minute;
    out->second = second;
    out->nano = nano;
    if (!robo_i18n_time_is_valid(out)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_time_to_nanos_of_day(const robo_i18n_time_t *time, int64_t *out)
{
    if (!time || !out || !robo_i18n_time_is_valid(time)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = ((int64_t)time->hour * 3600 + (int64_t)time->minute * 60 + (int64_t)time->second) *
               (int64_t)ROBO_I18N_TIME_NANOS_PER_SECOND +
           (int64_t)time->nano;
    return ROBO_OK;
}

robo_result_t robo_i18n_time_from_nanos_of_day(int64_t nanos, robo_i18n_time_t *out)
{
    int64_t sec;
    if (!out || nanos < 0 ||
        nanos >= (int64_t)86400 * (int64_t)ROBO_I18N_TIME_NANOS_PER_SECOND) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->nano = (uint32_t)(nanos % ROBO_I18N_TIME_NANOS_PER_SECOND);
    sec = nanos / ROBO_I18N_TIME_NANOS_PER_SECOND;
    out->hour = (uint8_t)(sec / 3600);
    out->minute = (uint8_t)((sec % 3600) / 60);
    out->second = (uint8_t)(sec % 60);
    return ROBO_OK;
}

int robo_i18n_datetime_is_valid(const robo_i18n_datetime_t *dt)
{
    return dt && robo_i18n_date_is_valid(&dt->date) && robo_i18n_time_is_valid(&dt->time);
}

robo_result_t robo_i18n_datetime_init(robo_i18n_datetime_t *out,
                                      const robo_i18n_date_t *date,
                                      const robo_i18n_time_t *time)
{
    if (!out || !date || !time) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_i18n_date_is_valid(date) || !robo_i18n_time_is_valid(time)) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->date = *date;
    out->time = *time;
    return ROBO_OK;
}

robo_result_t robo_i18n_instant_from_unix_ms(int64_t unix_ms, robo_i18n_instant_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->unix_ms = unix_ms;
    return ROBO_OK;
}

robo_result_t robo_i18n_instant_to_unix_ms(const robo_i18n_instant_t *in, int64_t *out)
{
    if (!in || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = in->unix_ms;
    return ROBO_OK;
}

robo_result_t robo_i18n_instant_from_unix_s(int64_t unix_s, robo_i18n_instant_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (unix_s > INT64_MAX / 1000 || unix_s < INT64_MIN / 1000) {
        return ROBO_NOT_SUPPORTED;
    }
    out->unix_ms = unix_s * 1000;
    return ROBO_OK;
}

robo_result_t robo_i18n_instant_from_utc(const robo_i18n_datetime_t *utc,
                                         robo_i18n_instant_t *out)
{
    int64_t days;
    int64_t nod;
    int64_t day_ms;
    if (!utc || !out || !robo_i18n_datetime_is_valid(utc)) {
        return ROBO_INVALID_ARGUMENT;
    }
    days = days_from_civil(utc->date.year, utc->date.month, utc->date.day);
    nod = ((int64_t)utc->time.hour * 3600 + (int64_t)utc->time.minute * 60 +
           (int64_t)utc->time.second) *
              1000 +
          (int64_t)(utc->time.nano / 1000000u);
    /* Safe multiply days * 86400000 without false negatives near epoch. */
    if (days > 0) {
        if (days > INT64_MAX / 86400000) {
            return ROBO_NOT_SUPPORTED;
        }
    } else if (days < 0) {
        if (days < INT64_MIN / 86400000) {
            return ROBO_NOT_SUPPORTED;
        }
    }
    day_ms = days * 86400000;
    if ((nod > 0 && day_ms > INT64_MAX - nod) || (nod < 0 && day_ms < INT64_MIN - nod)) {
        return ROBO_NOT_SUPPORTED;
    }
    out->unix_ms = day_ms + nod;
    return ROBO_OK;
}

robo_result_t robo_i18n_instant_to_utc(const robo_i18n_instant_t *in,
                                       robo_i18n_datetime_t *out_utc)
{
    int64_t days;
    int64_t rem;
    int32_t y;
    unsigned m, d;
    int64_t sec;
    if (!in || !out_utc) {
        return ROBO_INVALID_ARGUMENT;
    }
    days = in->unix_ms / 86400000;
    rem = in->unix_ms % 86400000;
    if (rem < 0) {
        rem += 86400000;
        days -= 1;
    }
    civil_from_days(days, &y, &m, &d);
    if (!year_in_range(y)) {
        return ROBO_NOT_SUPPORTED;
    }
    out_utc->date.year = y;
    out_utc->date.month = (uint8_t)m;
    out_utc->date.day = (uint8_t)d;
    sec = rem / 1000;
    out_utc->time.hour = (uint8_t)(sec / 3600);
    out_utc->time.minute = (uint8_t)((sec % 3600) / 60);
    out_utc->time.second = (uint8_t)(sec % 60);
    out_utc->time.nano = (uint32_t)((rem % 1000) * 1000000u);
    return ROBO_OK;
}

robo_result_t robo_i18n_instant_add(const robo_i18n_instant_t *in,
                                    const robo_i18n_duration_t *dur,
                                    robo_i18n_instant_t *out)
{
    if (!in || !dur || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if ((dur->millis > 0 && in->unix_ms > INT64_MAX - dur->millis) ||
        (dur->millis < 0 && in->unix_ms < INT64_MIN - dur->millis)) {
        return ROBO_NOT_SUPPORTED;
    }
    out->unix_ms = in->unix_ms + dur->millis;
    return ROBO_OK;
}

int robo_i18n_instant_compare(const robo_i18n_instant_t *a, const robo_i18n_instant_t *b)
{
    if (!a || !b) {
        return 0;
    }
    if (a->unix_ms < b->unix_ms) {
        return -1;
    }
    if (a->unix_ms > b->unix_ms) {
        return 1;
    }
    return 0;
}

robo_result_t robo_i18n_duration_from_millis(int64_t ms, robo_i18n_duration_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->millis = ms;
    return ROBO_OK;
}

robo_result_t robo_i18n_duration_between(const robo_i18n_instant_t *start,
                                         const robo_i18n_instant_t *end,
                                         robo_i18n_duration_t *out)
{
    if (!start || !end || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->millis = end->unix_ms - start->unix_ms;
    return ROBO_OK;
}
