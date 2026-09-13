/*
 * RoboI18N date/time model (Goal 4.5).
 *
 * Concepts (do not conflate):
 *   instant     — absolute point on the UTC timeline
 *   calendar date — civil year/month/day in a calendar system
 *   wall time   — hour/minute/second within a local day
 *   date-time   — calendar date + wall time (no zone)
 *   duration    — signed elapsed time (not calendar arithmetic)
 *   time zone   — rules mapping instant ↔ local date-time
 *   calendar    — numbering system for civil dates
 *   locale      — language/region preferences (patterns/names later)
 *
 * Leap seconds: NOT supported. Every UTC day is treated as 86400 seconds
 * (POSIX / Unix time). API shape allows future leap-second awareness.
 *
 * Thread safety: all types are value types or immutable table views.
 * No process-wide mutable caches.
 */

#ifndef ROBO_I18N_DATETIME_H
#define ROBO_I18N_DATETIME_H

#include "RoboBase/RoboError.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Documented Gregorian field year range for conversions (proleptic). */
#define ROBO_I18N_DATE_YEAR_MIN (-9999)
#define ROBO_I18N_DATE_YEAR_MAX 9999

/** Wall-clock fractional precision: nanoseconds in [0, 999999999]. */
#define ROBO_I18N_TIME_NANOS_PER_SECOND 1000000000

/**
 * Calendar date fields. Interpretation depends on calendar_id when used
 * with calendar APIs; Gregorian APIs treat fields as proleptic Gregorian.
 * month: 1..12 (Gregorian), day: 1..31.
 */
typedef struct robo_i18n_date {
    int32_t year;
    uint8_t month;
    uint8_t day;
} robo_i18n_date_t;

/** Wall-clock time (24-hour). hour 0..23, minute/second 0..59, nano 0..1e9-1. */
typedef struct robo_i18n_time {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint32_t nano;
} robo_i18n_time_t;

/** Civil date-time without time zone. */
typedef struct robo_i18n_datetime {
    robo_i18n_date_t date;
    robo_i18n_time_t time;
} robo_i18n_datetime_t;

/**
 * Absolute instant: milliseconds since Unix epoch
 * 1970-01-01T00:00:00.000Z (UTC), excluding leap seconds.
 * Range for field conversion: years ROBO_I18N_DATE_YEAR_MIN..MAX.
 */
typedef struct robo_i18n_instant {
    int64_t unix_ms;
} robo_i18n_instant_t;

/** Signed duration in milliseconds (not calendar months/years). */
typedef struct robo_i18n_duration {
    int64_t millis;
} robo_i18n_duration_t;

/** ISO-8601 weekday: Monday=1 .. Sunday=7. */
typedef enum robo_i18n_weekday {
    ROBO_I18N_WEEKDAY_MON = 1,
    ROBO_I18N_WEEKDAY_TUE = 2,
    ROBO_I18N_WEEKDAY_WED = 3,
    ROBO_I18N_WEEKDAY_THU = 4,
    ROBO_I18N_WEEKDAY_FRI = 5,
    ROBO_I18N_WEEKDAY_SAT = 6,
    ROBO_I18N_WEEKDAY_SUN = 7
} robo_i18n_weekday_t;

/* ---- Date (proleptic Gregorian) ---- */

robo_result_t robo_i18n_date_init(robo_i18n_date_t *out, int32_t year, uint8_t month, uint8_t day);

/** 1 if valid proleptic Gregorian date in supported year range. */
int robo_i18n_date_is_valid(const robo_i18n_date_t *date);

/**
 * Normalize invalid day overflows into following months when requested.
 * Without normalize, invalid → ROBO_INVALID_ARGUMENT and *out unchanged.
 */
robo_result_t robo_i18n_date_init_checked(robo_i18n_date_t *out,
                                          int32_t year,
                                          uint8_t month,
                                          uint8_t day,
                                          int normalize);

int robo_i18n_date_is_leap_year(int32_t year);
uint8_t robo_i18n_date_days_in_month(int32_t year, uint8_t month);

robo_result_t robo_i18n_date_day_of_week(const robo_i18n_date_t *date,
                                         robo_i18n_weekday_t *out);
robo_result_t robo_i18n_date_day_of_year(const robo_i18n_date_t *date, int32_t *out);

/**
 * ISO week date: week_year may differ from Gregorian year near year boundaries.
 * week: 1..53, weekday: Mon=1..Sun=7.
 */
robo_result_t robo_i18n_date_to_iso_week(const robo_i18n_date_t *date,
                                         int32_t *out_week_year,
                                         uint8_t *out_week,
                                         robo_i18n_weekday_t *out_weekday);

robo_result_t robo_i18n_date_from_iso_week(int32_t week_year,
                                           uint8_t week,
                                           robo_i18n_weekday_t weekday,
                                           robo_i18n_date_t *out);

/** Rata Die: day 1 = 0001-01-01 (proleptic Gregorian). */
robo_result_t robo_i18n_date_to_rata_die(const robo_i18n_date_t *date, int64_t *out_rd);
robo_result_t robo_i18n_date_from_rata_die(int64_t rd, robo_i18n_date_t *out);

robo_result_t robo_i18n_date_add_days(const robo_i18n_date_t *in,
                                      int32_t days,
                                      robo_i18n_date_t *out);

int robo_i18n_date_compare(const robo_i18n_date_t *a, const robo_i18n_date_t *b);

/* ---- Time ---- */

robo_result_t robo_i18n_time_init(robo_i18n_time_t *out,
                                  uint8_t hour,
                                  uint8_t minute,
                                  uint8_t second,
                                  uint32_t nano);

int robo_i18n_time_is_valid(const robo_i18n_time_t *time);

robo_result_t robo_i18n_time_from_nanos_of_day(int64_t nanos, robo_i18n_time_t *out);
robo_result_t robo_i18n_time_to_nanos_of_day(const robo_i18n_time_t *time, int64_t *out);

/* ---- Date-time ---- */

robo_result_t robo_i18n_datetime_init(robo_i18n_datetime_t *out,
                                      const robo_i18n_date_t *date,
                                      const robo_i18n_time_t *time);

int robo_i18n_datetime_is_valid(const robo_i18n_datetime_t *dt);

/* ---- Instant ---- */

robo_result_t robo_i18n_instant_from_unix_ms(int64_t unix_ms, robo_i18n_instant_t *out);
robo_result_t robo_i18n_instant_to_unix_ms(const robo_i18n_instant_t *in, int64_t *out);
robo_result_t robo_i18n_instant_from_unix_s(int64_t unix_s, robo_i18n_instant_t *out);

/** Convert UTC Gregorian date-time ↔ instant. */
robo_result_t robo_i18n_instant_from_utc(const robo_i18n_datetime_t *utc,
                                         robo_i18n_instant_t *out);
robo_result_t robo_i18n_instant_to_utc(const robo_i18n_instant_t *in,
                                       robo_i18n_datetime_t *out_utc);

robo_result_t robo_i18n_instant_add(const robo_i18n_instant_t *in,
                                    const robo_i18n_duration_t *dur,
                                    robo_i18n_instant_t *out);

int robo_i18n_instant_compare(const robo_i18n_instant_t *a, const robo_i18n_instant_t *b);

/* ---- Duration ---- */

robo_result_t robo_i18n_duration_from_millis(int64_t ms, robo_i18n_duration_t *out);
robo_result_t robo_i18n_duration_between(const robo_i18n_instant_t *start,
                                         const robo_i18n_instant_t *end,
                                         robo_i18n_duration_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_DATETIME_H */
