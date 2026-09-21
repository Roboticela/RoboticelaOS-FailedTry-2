/*
 * RoboI18N calendar abstraction (Goal 4.5).
 *
 * Calendar systems are not interchangeable. Field meanings, leap rules,
 * and eras differ. This module:
 *   - identifies calendars (CLDR ids)
 *   - exposes CLDR display metadata (names/patterns) for later formatting
 *   - converts fields ↔ Gregorian/instant where algorithms are implemented
 *
 * Implemented conversions:
 *   gregorian (proleptic), iso8601 week, buddhist (year offset), japanese (era)
 *
 * Metadata-only (conversion → RoboNotSupported):
 *   islamic, hebrew, persian, chinese (architecture reserved)
 *
 * Formatting strings are NOT produced here — Goal 4.6+.
 */

#ifndef ROBO_I18N_CALENDAR_H
#define ROBO_I18N_CALENDAR_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NDateTime.h"
#include "RoboI18N/RoboI18NLocale.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboI18nCalendarIdMax 32u

typedef enum I18nCalendarKind {
    RoboI18nCalGregorian = 0,
    RoboI18nCalIso8601 = 1,
    RoboI18nCalBuddhist = 2,
    RoboI18nCalJapanese = 3,
    RoboI18nCalIslamic = 4,
    RoboI18nCalHebrew = 5,
    RoboI18nCalPersian = 6,
    RoboI18nCalChinese = 7,
    RoboI18nCalUnknown = 255
} I18nCalendarKind;

typedef enum I18nCalNameWidth {
    RoboI18nCalNameWide = 0,
    RoboI18nCalNameAbbreviated = 1,
    RoboI18nCalNameNarrow = 2
} I18nCalNameWidth;

typedef enum I18nCalFormatStyle {
    RoboI18nCalStyleFull = 0,
    RoboI18nCalStyleLong = 1,
    RoboI18nCalStyleMedium = 2,
    RoboI18nCalStyleShort = 3
} I18nCalFormatStyle;

/**
 * Calendar fields. For Japanese, year is era year and eraIndex selects the era.
 * For Gregorian/Buddhist/ISO, eraIndex is typically 0 (AD) or unused.
 */
typedef struct I18nCalendarFields {
    int32_t year;
    uint8_t month;
    uint8_t day;
    int32_t eraIndex; /* Japanese era ordinal from CLDR; 0 if unused */
} I18nCalendarFields;

typedef struct I18nCalendar {
    I18nCalendarKind kind;
    char id[RoboI18nCalendarIdMax + 1u];
} I18nCalendar;

Result I18nCalendarFromId(const char *id,
                                         size_t len,
                                         I18nCalendar *out);

Result I18nCalendarFromKind(I18nCalendarKind kind,
                                           I18nCalendar *out);

const char *I18nCalendarId(const I18nCalendar *cal);
I18nCalendarKind MakeI18nCalendarKind(const I18nCalendar *cal);

/** 1 if field↔Gregorian conversion is implemented for this calendar. */
int I18nCalendarHasConversion(const I18nCalendar *cal);

Result I18nCalendarToGregorian(const I18nCalendar *cal,
                                              const I18nCalendarFields *fields,
                                              I18nDate *outGregorian);

Result I18nCalendarFromGregorian(const I18nCalendar *cal,
                                                const I18nDate *gregorian,
                                                I18nCalendarFields *outFields);

/* ---- Locale week / weekend metadata (CLDR weekData) ---- */

Result I18nLocaleFirstDayOfWeek(const I18nLocale *loc,
                                                 I18nWeekday *out);

Result I18nLocaleWeekend(const I18nLocale *loc,
                                       I18nWeekday *outStart,
                                       I18nWeekday *outEnd);

/* ---- CLDR display metadata (foundation for later formatting) ---- */

/**
 * Month name (1..12). Returns pointer into immortal generated tables.
 * RoboNotFound if locale/calendar/width missing after inheritance.
 */
Result I18nCalendarMonthName(const I18nLocale *loc,
                                            const I18nCalendar *cal,
                                            uint8_t month,
                                            I18nCalNameWidth width,
                                            const char **outUtf8);

Result I18nCalendarWeekdayName(const I18nLocale *loc,
                                              const I18nCalendar *cal,
                                              I18nWeekday weekday,
                                              I18nCalNameWidth width,
                                              const char **outUtf8);

Result I18nCalendarEraName(const I18nLocale *loc,
                                          const I18nCalendar *cal,
                                          int32_t eraIndex,
                                          I18nCalNameWidth width,
                                          const char **outUtf8);

Result I18nCalendarDayPeriodName(const I18nLocale *loc,
                                                 const I18nCalendar *cal,
                                                 const char *periodId,
                                                 I18nCalNameWidth width,
                                                 const char **outUtf8);

/** CLDR pattern strings (e.g. "MMMM d, y") — not formatted output. */
Result I18nCalendarDatePattern(const I18nLocale *loc,
                                              const I18nCalendar *cal,
                                              I18nCalFormatStyle style,
                                              const char **outUtf8);

Result I18nCalendarTimePattern(const I18nLocale *loc,
                                              const I18nCalendar *cal,
                                              I18nCalFormatStyle style,
                                              const char **outUtf8);

Result I18nCalendarDatetimePattern(const I18nLocale *loc,
                                                  const I18nCalendar *cal,
                                                  I18nCalFormatStyle style,
                                                  const char **outUtf8);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_CALENDAR_H */
