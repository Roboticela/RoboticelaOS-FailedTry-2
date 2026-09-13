/*
 * RoboI18N calendar arithmetic & date/time utilities (Goal 4.8).
 *
 * Concepts (do not collapse):
 *   duration  — fixed elapsed time in milliseconds (instant arithmetic)
 *   period    — calendar-relative years/months/weeks/days (not seconds)
 *   date diff — civil field or day-count difference
 *
 * End-of-month policy (months/years): CLAMP to last valid day of the target
 * month. Example: 2026-01-31 + 1 month → 2026-02-28. Documented; not overflow.
 *
 * Instant + period in a zone: convert to local civil fields, apply period,
 * convert back. IANA without tzdb → ROBO_NOT_SUPPORTED. Ambiguous/nonexistent
 * local results → ROBO_NOT_SUPPORTED (no silent pick).
 *
 * Overflow → ROBO_ERROR (no silent wrap). Unsupported calendar ops →
 * ROBO_NOT_SUPPORTED.
 *
 * Thread safety: value types / stack formatters; no global mutable caches.
 */

#ifndef ROBO_I18N_DATETIME_ARITH_H
#define ROBO_I18N_DATETIME_ARITH_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NCalendar.h"
#include "RoboI18N/RoboI18NDateTime.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NTimeZone.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Milliseconds in a POSIX day (no leap seconds). Not a calendar day. */
#define ROBO_I18N_DURATION_MILLIS_PER_DAY 86400000LL

/**
 * Calendar-relative period. Never auto-converted to a fixed duration.
 * weeks are independent of days (ISO-style: 1 week = 7 calendar days when
 * applied, but stored separately until applied).
 */
typedef struct robo_i18n_period {
    int32_t years;
    int32_t months;
    int32_t weeks;
    int32_t days;
} robo_i18n_period_t;

/**
 * Business-day calendar: weekend mask + optional holiday hook.
 * weekend_bits: bit (iso_weekday - 1) set ⇒ weekend (Mon=bit0 … Sun=bit6).
 * Holidays: is_holiday returns non-zero if @p date is a holiday; NULL = none.
 */
typedef struct robo_i18n_business_calendar {
    uint8_t weekend_bits;
    int (*is_holiday)(const robo_i18n_date_t *date, void *ctx);
    void *holiday_ctx;
} robo_i18n_business_calendar_t;

/* ---- Period ---- */

void robo_i18n_period_init(robo_i18n_period_t *out);
robo_result_t robo_i18n_period_of(int32_t years,
                                  int32_t months,
                                  int32_t weeks,
                                  int32_t days,
                                  robo_i18n_period_t *out);

/** Add periods component-wise (no month→day conversion). Overflow → ROBO_ERROR. */
robo_result_t robo_i18n_period_add(const robo_i18n_period_t *a,
                                   const robo_i18n_period_t *b,
                                   robo_i18n_period_t *out);

robo_result_t robo_i18n_period_negate(const robo_i18n_period_t *in, robo_i18n_period_t *out);

/* ---- Duration helpers (fixed ms; not calendar) ---- */

robo_result_t robo_i18n_duration_from_seconds(int64_t seconds, robo_i18n_duration_t *out);
robo_result_t robo_i18n_duration_from_minutes(int64_t minutes, robo_i18n_duration_t *out);
robo_result_t robo_i18n_duration_from_hours(int64_t hours, robo_i18n_duration_t *out);
/** Fixed 86400000 ms units — NOT calendar days across DST. */
robo_result_t robo_i18n_duration_from_days(int64_t days, robo_i18n_duration_t *out);

robo_result_t robo_i18n_duration_negate(const robo_i18n_duration_t *in,
                                        robo_i18n_duration_t *out);
robo_result_t robo_i18n_duration_add(const robo_i18n_duration_t *a,
                                     const robo_i18n_duration_t *b,
                                     robo_i18n_duration_t *out);
int robo_i18n_duration_compare(const robo_i18n_duration_t *a, const robo_i18n_duration_t *b);

/* ---- Gregorian date arithmetic (CLAMP end-of-month) ---- */

robo_result_t robo_i18n_date_add_weeks(const robo_i18n_date_t *in,
                                       int32_t weeks,
                                       robo_i18n_date_t *out);
robo_result_t robo_i18n_date_add_months(const robo_i18n_date_t *in,
                                        int32_t months,
                                        robo_i18n_date_t *out);
robo_result_t robo_i18n_date_add_years(const robo_i18n_date_t *in,
                                       int32_t years,
                                       robo_i18n_date_t *out);
robo_result_t robo_i18n_date_add_period(const robo_i18n_date_t *in,
                                        const robo_i18n_period_t *period,
                                        robo_i18n_date_t *out);

/** Signed day count: end − start (via Rata Die). */
robo_result_t robo_i18n_date_difference_days(const robo_i18n_date_t *start,
                                             const robo_i18n_date_t *end,
                                             int32_t *out_days);

/**
 * Civil period between dates (years, then months, then days), CLAMP-compatible
 * with add_period round-trips when start ≤ end.
 */
robo_result_t robo_i18n_date_difference_period(const robo_i18n_date_t *start,
                                               const robo_i18n_date_t *end,
                                               robo_i18n_period_t *out);

int32_t robo_i18n_date_days_in_year(int32_t year);

/* ---- Instant / duration ---- */

robo_result_t robo_i18n_instant_subtract(const robo_i18n_instant_t *in,
                                         const robo_i18n_duration_t *dur,
                                         robo_i18n_instant_t *out);

/* ---- Comparisons (civil / value; not zone-absolute for datetime) ---- */

int robo_i18n_time_compare(const robo_i18n_time_t *a, const robo_i18n_time_t *b);
int robo_i18n_datetime_compare(const robo_i18n_datetime_t *a, const robo_i18n_datetime_t *b);

/* ---- ISO week utilities ---- */

robo_result_t robo_i18n_iso_weeks_in_year(int32_t week_year, uint8_t *out_weeks);
robo_result_t robo_i18n_date_iso_week_start(const robo_i18n_date_t *date,
                                            robo_i18n_date_t *out_monday);
robo_result_t robo_i18n_date_iso_week_end(const robo_i18n_date_t *date,
                                          robo_i18n_date_t *out_sunday);

/* ---- Calendar boundaries (civil date / datetime; no zone) ---- */

robo_result_t robo_i18n_date_start_of_month(const robo_i18n_date_t *in, robo_i18n_date_t *out);
robo_result_t robo_i18n_date_end_of_month(const robo_i18n_date_t *in, robo_i18n_date_t *out);
robo_result_t robo_i18n_date_start_of_year(const robo_i18n_date_t *in, robo_i18n_date_t *out);
robo_result_t robo_i18n_date_end_of_year(const robo_i18n_date_t *in, robo_i18n_date_t *out);
robo_result_t robo_i18n_date_start_of_quarter(const robo_i18n_date_t *in, robo_i18n_date_t *out);
robo_result_t robo_i18n_date_end_of_quarter(const robo_i18n_date_t *in, robo_i18n_date_t *out);

robo_result_t robo_i18n_datetime_start_of_day(const robo_i18n_date_t *date,
                                              robo_i18n_datetime_t *out);
/** End of civil day: 23:59:59.999999999 (not next midnight). */
robo_result_t robo_i18n_datetime_end_of_day(const robo_i18n_date_t *date,
                                            robo_i18n_datetime_t *out);

/**
 * Locale week boundaries: first day from CLDR weekData; span 7 days.
 * Distinct from ISO week (always Monday–Sunday).
 */
robo_result_t robo_i18n_date_start_of_week(const robo_i18n_date_t *in,
                                           const robo_i18n_locale_t *locale,
                                           robo_i18n_date_t *out);
robo_result_t robo_i18n_date_end_of_week(const robo_i18n_date_t *in,
                                         const robo_i18n_locale_t *locale,
                                         robo_i18n_date_t *out);

/* ---- Time-zone-aware boundaries / period ---- */

/**
 * Instant of local midnight for @p instant's civil date in @p tz.
 * FIXED/UTC only until tzdb; uniqueness required.
 */
robo_result_t robo_i18n_instant_start_of_day(const robo_i18n_instant_t *instant,
                                             const robo_i18n_timezone_t *tz,
                                             robo_i18n_instant_t *out);

/**
 * Add calendar period in @p tz: to_local → add_period on date → keep wall time
 * → to_utc. Duration-like wall time is preserved (not stretched).
 */
robo_result_t robo_i18n_instant_add_period(const robo_i18n_instant_t *in,
                                           const robo_i18n_period_t *period,
                                           const robo_i18n_timezone_t *tz,
                                           robo_i18n_instant_t *out);

robo_result_t robo_i18n_datetime_add_period(const robo_i18n_datetime_t *in,
                                            const robo_i18n_period_t *period,
                                            robo_i18n_datetime_t *out);

/* ---- Calendar-aware field arithmetic ---- */

/**
 * Add period to calendar fields. Supported calendars with conversion:
 * gregorian, iso8601, buddhist, japanese. Others → ROBO_NOT_SUPPORTED.
 * Implementation: fields→Gregorian, Gregorian add_period, →fields.
 */
robo_result_t robo_i18n_calendar_add_period(const robo_i18n_calendar_t *cal,
                                            const robo_i18n_calendar_fields_t *in,
                                            const robo_i18n_period_t *period,
                                            robo_i18n_calendar_fields_t *out);

/* ---- Business days ---- */

void robo_i18n_business_calendar_init(robo_i18n_business_calendar_t *out);
/** Weekend from CLDR weekData for locale region; no holidays. */
robo_result_t robo_i18n_business_calendar_from_locale(const robo_i18n_locale_t *locale,
                                                      robo_i18n_business_calendar_t *out);

int robo_i18n_date_is_weekend(const robo_i18n_date_t *date,
                              const robo_i18n_business_calendar_t *biz);
int robo_i18n_date_is_business_day(const robo_i18n_date_t *date,
                                   const robo_i18n_business_calendar_t *biz);

/**
 * Add @p n business days (skipping weekends/holidays). n may be negative.
 * Zero returns a copy of @p in (even if that day is not a business day).
 */
robo_result_t robo_i18n_date_add_business_days(const robo_i18n_date_t *in,
                                               int32_t n,
                                               const robo_i18n_business_calendar_t *biz,
                                               robo_i18n_date_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_DATETIME_ARITH_H */
