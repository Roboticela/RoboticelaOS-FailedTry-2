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
#define RoboI18nDateYearMin (-9999)
#define RoboI18nDateYearMax 9999

/** Wall-clock fractional precision: nanoseconds in [0, 999999999]. */
#define RoboI18nTimeNanosPerSecond 1000000000

/**
 * Calendar date fields. Interpretation depends on calendarId when used
 * with calendar APIs; Gregorian APIs treat fields as proleptic Gregorian.
 * month: 1..12 (Gregorian), day: 1..31.
 */
typedef struct I18nDate {
    int32_t year;
    uint8_t month;
    uint8_t day;
} I18nDate;

/** Wall-clock time (24-hour). hour 0..23, minute/second 0..59, nano 0..1e9-1. */
typedef struct I18nTime {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint32_t nano;
} I18nTime;

/** Civil date-time without time zone. */
typedef struct I18nDatetime {
    I18nDate date;
    I18nTime time;
} I18nDatetime;

/**
 * Absolute instant: milliseconds since Unix epoch
 * 1970-01-01T00:00:00.000Z (UTC), excluding leap seconds.
 * Range for field conversion: years RoboI18nDateYearMin..MAX.
 */
typedef struct I18nInstant {
    int64_t unixMs;
} I18nInstant;

/** Signed duration in milliseconds (not calendar months/years). */
typedef struct I18nDuration {
    int64_t millis;
} I18nDuration;

/** ISO-8601 weekday: Monday=1 .. Sunday=7. */
typedef enum I18nWeekday {
    RoboI18nWeekdayMon = 1,
    RoboI18nWeekdayTue = 2,
    RoboI18nWeekdayWed = 3,
    RoboI18nWeekdayThu = 4,
    RoboI18nWeekdayFri = 5,
    RoboI18nWeekdaySat = 6,
    RoboI18nWeekdaySun = 7
} I18nWeekday;

/* ---- Date (proleptic Gregorian) ---- */

Result I18nDateInit(I18nDate *out, int32_t year, uint8_t month, uint8_t day);

/** 1 if valid proleptic Gregorian date in supported year range. */
int I18nDateIsValid(const I18nDate *date);

/**
 * Normalize invalid day overflows into following months when requested.
 * Without normalize, invalid → RoboInvalidArgument and *out unchanged.
 */
Result I18nDateInitChecked(I18nDate *out,
                                          int32_t year,
                                          uint8_t month,
                                          uint8_t day,
                                          int normalize);

int I18nDateIsLeapYear(int32_t year);
uint8_t I18nDateDaysInMonth(int32_t year, uint8_t month);

Result I18nDateDayOfWeek(const I18nDate *date,
                                         I18nWeekday *out);
Result I18nDateDayOfYear(const I18nDate *date, int32_t *out);

/**
 * ISO week date: weekYear may differ from Gregorian year near year boundaries.
 * week: 1..53, weekday: Mon=1..Sun=7.
 */
Result I18nDateToIsoWeek(const I18nDate *date,
                                         int32_t *outWeekYear,
                                         uint8_t *outWeek,
                                         I18nWeekday *outWeekday);

Result I18nDateFromIsoWeek(int32_t weekYear,
                                           uint8_t week,
                                           I18nWeekday weekday,
                                           I18nDate *out);

/** Rata Die: day 1 = 0001-01-01 (proleptic Gregorian). */
Result I18nDateToRataDie(const I18nDate *date, int64_t *outRd);
Result I18nDateFromRataDie(int64_t rd, I18nDate *out);

Result I18nDateAddDays(const I18nDate *in,
                                      int32_t days,
                                      I18nDate *out);

int I18nDateCompare(const I18nDate *a, const I18nDate *b);

/* ---- Time ---- */

Result I18nTimeInit(I18nTime *out,
                                  uint8_t hour,
                                  uint8_t minute,
                                  uint8_t second,
                                  uint32_t nano);

int I18nTimeIsValid(const I18nTime *time);

Result I18nTimeFromNanosOfDay(int64_t nanos, I18nTime *out);
Result I18nTimeToNanosOfDay(const I18nTime *time, int64_t *out);

/* ---- Date-time ---- */

Result I18nDatetimeInit(I18nDatetime *out,
                                      const I18nDate *date,
                                      const I18nTime *time);

int I18nDatetimeIsValid(const I18nDatetime *dt);

/* ---- Instant ---- */

Result I18nInstantFromUnixMs(int64_t unixMs, I18nInstant *out);
Result I18nInstantToUnixMs(const I18nInstant *in, int64_t *out);
Result I18nInstantFromUnixS(int64_t unixS, I18nInstant *out);

/** Convert UTC Gregorian date-time ↔ instant. */
Result I18nInstantFromUtc(const I18nDatetime *utc,
                                         I18nInstant *out);
Result I18nInstantToUtc(const I18nInstant *in,
                                       I18nDatetime *outUtc);

Result I18nInstantAdd(const I18nInstant *in,
                                    const I18nDuration *dur,
                                    I18nInstant *out);

int I18nInstantCompare(const I18nInstant *a, const I18nInstant *b);

/* ---- Duration ---- */

Result I18nDurationFromMillis(int64_t ms, I18nDuration *out);
Result I18nDurationBetween(const I18nInstant *start,
                                         const I18nInstant *end,
                                         I18nDuration *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_DATETIME_H */
