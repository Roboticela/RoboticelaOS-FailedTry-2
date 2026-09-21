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
 * convert back. IANA without tzdb → RoboNotSupported. Ambiguous/nonexistent
 * local results → RoboNotSupported (no silent pick).
 *
 * Overflow → RoboError (no silent wrap). Unsupported calendar ops →
 * RoboNotSupported.
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
#define RoboI18nDurationMillisPerDay 86400000LL

/**
 * Calendar-relative period. Never auto-converted to a fixed duration.
 * weeks are independent of days (ISO-style: 1 week = 7 calendar days when
 * applied, but stored separately until applied).
 */
typedef struct I18nPeriod {
    int32_t years;
    int32_t months;
    int32_t weeks;
    int32_t days;
} I18nPeriod;

/**
 * Business-day calendar: weekend mask + optional holiday hook.
 * weekendBits: bit (iso_weekday - 1) set ⇒ weekend (Mon=bit0 … Sun=bit6).
 * Holidays: IsHoliday returns non-zero if @p date is a holiday; NULL = none.
 */
typedef struct I18nBusinessCalendar {
    uint8_t weekendBits;
    int (*IsHoliday)(const I18nDate *date, void *ctx);
    void *holidayCtx;
} I18nBusinessCalendar;

/* ---- Period ---- */

void I18nPeriodInit(I18nPeriod *out);
Result I18nPeriodOf(int32_t years,
                                  int32_t months,
                                  int32_t weeks,
                                  int32_t days,
                                  I18nPeriod *out);

/** Add periods component-wise (no month→day conversion). Overflow → RoboError. */
Result I18nPeriodAdd(const I18nPeriod *a,
                                   const I18nPeriod *b,
                                   I18nPeriod *out);

Result I18nPeriodNegate(const I18nPeriod *in, I18nPeriod *out);

/* ---- Duration helpers (fixed ms; not calendar) ---- */

Result I18nDurationFromSeconds(int64_t seconds, I18nDuration *out);
Result I18nDurationFromMinutes(int64_t minutes, I18nDuration *out);
Result I18nDurationFromHours(int64_t hours, I18nDuration *out);
/** Fixed 86400000 ms units — NOT calendar days across DST. */
Result I18nDurationFromDays(int64_t days, I18nDuration *out);

Result I18nDurationNegate(const I18nDuration *in,
                                        I18nDuration *out);
Result I18nDurationAdd(const I18nDuration *a,
                                     const I18nDuration *b,
                                     I18nDuration *out);
int I18nDurationCompare(const I18nDuration *a, const I18nDuration *b);

/* ---- Gregorian date arithmetic (CLAMP end-of-month) ---- */

Result I18nDateAddWeeks(const I18nDate *in,
                                       int32_t weeks,
                                       I18nDate *out);
Result I18nDateAddMonths(const I18nDate *in,
                                        int32_t months,
                                        I18nDate *out);
Result I18nDateAddYears(const I18nDate *in,
                                       int32_t years,
                                       I18nDate *out);
Result I18nDateAddPeriod(const I18nDate *in,
                                        const I18nPeriod *period,
                                        I18nDate *out);

/** Signed day count: end − start (via Rata Die). */
Result I18nDateDifferenceDays(const I18nDate *start,
                                             const I18nDate *end,
                                             int32_t *outDays);

/**
 * Civil period between dates (years, then months, then days), CLAMP-compatible
 * with add_period round-trips when start ≤ end.
 */
Result I18nDateDifferencePeriod(const I18nDate *start,
                                               const I18nDate *end,
                                               I18nPeriod *out);

int32_t I18nDateDaysInYear(int32_t year);

/* ---- Instant / duration ---- */

Result I18nInstantSubtract(const I18nInstant *in,
                                         const I18nDuration *dur,
                                         I18nInstant *out);

/* ---- Comparisons (civil / value; not zone-absolute for datetime) ---- */

int I18nTimeCompare(const I18nTime *a, const I18nTime *b);
int I18nDatetimeCompare(const I18nDatetime *a, const I18nDatetime *b);

/* ---- ISO week utilities ---- */

Result I18nIsoWeeksInYear(int32_t weekYear, uint8_t *outWeeks);
Result I18nDateIsoWeekStart(const I18nDate *date,
                                            I18nDate *outMonday);
Result I18nDateIsoWeekEnd(const I18nDate *date,
                                          I18nDate *outSunday);

/* ---- Calendar boundaries (civil date / datetime; no zone) ---- */

Result I18nDateStartOfMonth(const I18nDate *in, I18nDate *out);
Result I18nDateEndOfMonth(const I18nDate *in, I18nDate *out);
Result I18nDateStartOfYear(const I18nDate *in, I18nDate *out);
Result I18nDateEndOfYear(const I18nDate *in, I18nDate *out);
Result I18nDateStartOfQuarter(const I18nDate *in, I18nDate *out);
Result I18nDateEndOfQuarter(const I18nDate *in, I18nDate *out);

Result I18nDatetimeStartOfDay(const I18nDate *date,
                                              I18nDatetime *out);
/** End of civil day: 23:59:59.999999999 (not next midnight). */
Result I18nDatetimeEndOfDay(const I18nDate *date,
                                            I18nDatetime *out);

/**
 * Locale week boundaries: first day from CLDR weekData; span 7 days.
 * Distinct from ISO week (always Monday–Sunday).
 */
Result I18nDateStartOfWeek(const I18nDate *in,
                                           const I18nLocale *locale,
                                           I18nDate *out);
Result I18nDateEndOfWeek(const I18nDate *in,
                                         const I18nLocale *locale,
                                         I18nDate *out);

/* ---- Time-zone-aware boundaries / period ---- */

/**
 * Instant of local midnight for @p instant's civil date in @p tz.
 * FIXED/UTC only until tzdb; uniqueness required.
 */
Result I18nInstantStartOfDay(const I18nInstant *instant,
                                             const I18nTimezone *tz,
                                             I18nInstant *out);

/**
 * Add calendar period in @p tz: to_local → add_period on date → keep wall time
 * → to_utc. Duration-like wall time is preserved (not stretched).
 */
Result I18nInstantAddPeriod(const I18nInstant *in,
                                           const I18nPeriod *period,
                                           const I18nTimezone *tz,
                                           I18nInstant *out);

Result I18nDatetimeAddPeriod(const I18nDatetime *in,
                                            const I18nPeriod *period,
                                            I18nDatetime *out);

/* ---- Calendar-aware field arithmetic ---- */

/**
 * Add period to calendar fields. Supported calendars with conversion:
 * gregorian, iso8601, buddhist, japanese. Others → RoboNotSupported.
 * Implementation: fields→Gregorian, Gregorian add_period, →fields.
 */
Result I18nCalendarAddPeriod(const I18nCalendar *cal,
                                            const I18nCalendarFields *in,
                                            const I18nPeriod *period,
                                            I18nCalendarFields *out);

/* ---- Business days ---- */

void I18nBusinessCalendarInit(I18nBusinessCalendar *out);
/** Weekend from CLDR weekData for locale region; no holidays. */
Result I18nBusinessCalendarFromLocale(const I18nLocale *locale,
                                                      I18nBusinessCalendar *out);

int I18nDateIsWeekend(const I18nDate *date,
                              const I18nBusinessCalendar *biz);
int I18nDateIsBusinessDay(const I18nDate *date,
                                   const I18nBusinessCalendar *biz);

/**
 * Add @p n business days (skipping weekends/holidays). n may be negative.
 * Zero returns a copy of @p in (even if that day is not a business day).
 */
Result I18nDateAddBusinessDays(const I18nDate *in,
                                               int32_t n,
                                               const I18nBusinessCalendar *biz,
                                               I18nDate *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_DATETIME_ARITH_H */
