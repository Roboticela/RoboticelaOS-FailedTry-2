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
 * Metadata-only (conversion → ROBO_NOT_SUPPORTED):
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

#define ROBO_I18N_CALENDAR_ID_MAX 32u

typedef enum robo_i18n_calendar_kind {
    ROBO_I18N_CAL_GREGORIAN = 0,
    ROBO_I18N_CAL_ISO8601 = 1,
    ROBO_I18N_CAL_BUDDHIST = 2,
    ROBO_I18N_CAL_JAPANESE = 3,
    ROBO_I18N_CAL_ISLAMIC = 4,
    ROBO_I18N_CAL_HEBREW = 5,
    ROBO_I18N_CAL_PERSIAN = 6,
    ROBO_I18N_CAL_CHINESE = 7,
    ROBO_I18N_CAL_UNKNOWN = 255
} robo_i18n_calendar_kind_t;

typedef enum robo_i18n_cal_name_width {
    ROBO_I18N_CAL_NAME_WIDE = 0,
    ROBO_I18N_CAL_NAME_ABBREVIATED = 1,
    ROBO_I18N_CAL_NAME_NARROW = 2
} robo_i18n_cal_name_width_t;

typedef enum robo_i18n_cal_format_style {
    ROBO_I18N_CAL_STYLE_FULL = 0,
    ROBO_I18N_CAL_STYLE_LONG = 1,
    ROBO_I18N_CAL_STYLE_MEDIUM = 2,
    ROBO_I18N_CAL_STYLE_SHORT = 3
} robo_i18n_cal_format_style_t;

/**
 * Calendar fields. For Japanese, year is era year and era_index selects the era.
 * For Gregorian/Buddhist/ISO, era_index is typically 0 (AD) or unused.
 */
typedef struct robo_i18n_calendar_fields {
    int32_t year;
    uint8_t month;
    uint8_t day;
    int32_t era_index; /* Japanese era ordinal from CLDR; 0 if unused */
} robo_i18n_calendar_fields_t;

typedef struct robo_i18n_calendar {
    robo_i18n_calendar_kind_t kind;
    char id[ROBO_I18N_CALENDAR_ID_MAX + 1u];
} robo_i18n_calendar_t;

robo_result_t robo_i18n_calendar_from_id(const char *id,
                                         size_t len,
                                         robo_i18n_calendar_t *out);

robo_result_t robo_i18n_calendar_from_kind(robo_i18n_calendar_kind_t kind,
                                           robo_i18n_calendar_t *out);

const char *robo_i18n_calendar_id(const robo_i18n_calendar_t *cal);
robo_i18n_calendar_kind_t robo_i18n_calendar_kind(const robo_i18n_calendar_t *cal);

/** 1 if field↔Gregorian conversion is implemented for this calendar. */
int robo_i18n_calendar_has_conversion(const robo_i18n_calendar_t *cal);

robo_result_t robo_i18n_calendar_to_gregorian(const robo_i18n_calendar_t *cal,
                                              const robo_i18n_calendar_fields_t *fields,
                                              robo_i18n_date_t *out_gregorian);

robo_result_t robo_i18n_calendar_from_gregorian(const robo_i18n_calendar_t *cal,
                                                const robo_i18n_date_t *gregorian,
                                                robo_i18n_calendar_fields_t *out_fields);

/* ---- Locale week / weekend metadata (CLDR weekData) ---- */

robo_result_t robo_i18n_locale_first_day_of_week(const robo_i18n_locale_t *loc,
                                                 robo_i18n_weekday_t *out);

robo_result_t robo_i18n_locale_weekend(const robo_i18n_locale_t *loc,
                                       robo_i18n_weekday_t *out_start,
                                       robo_i18n_weekday_t *out_end);

/* ---- CLDR display metadata (foundation for later formatting) ---- */

/**
 * Month name (1..12). Returns pointer into immortal generated tables.
 * ROBO_NOT_FOUND if locale/calendar/width missing after inheritance.
 */
robo_result_t robo_i18n_calendar_month_name(const robo_i18n_locale_t *loc,
                                            const robo_i18n_calendar_t *cal,
                                            uint8_t month,
                                            robo_i18n_cal_name_width_t width,
                                            const char **out_utf8);

robo_result_t robo_i18n_calendar_weekday_name(const robo_i18n_locale_t *loc,
                                              const robo_i18n_calendar_t *cal,
                                              robo_i18n_weekday_t weekday,
                                              robo_i18n_cal_name_width_t width,
                                              const char **out_utf8);

robo_result_t robo_i18n_calendar_era_name(const robo_i18n_locale_t *loc,
                                          const robo_i18n_calendar_t *cal,
                                          int32_t era_index,
                                          robo_i18n_cal_name_width_t width,
                                          const char **out_utf8);

robo_result_t robo_i18n_calendar_day_period_name(const robo_i18n_locale_t *loc,
                                                 const robo_i18n_calendar_t *cal,
                                                 const char *period_id,
                                                 robo_i18n_cal_name_width_t width,
                                                 const char **out_utf8);

/** CLDR pattern strings (e.g. "MMMM d, y") — not formatted output. */
robo_result_t robo_i18n_calendar_date_pattern(const robo_i18n_locale_t *loc,
                                              const robo_i18n_calendar_t *cal,
                                              robo_i18n_cal_format_style_t style,
                                              const char **out_utf8);

robo_result_t robo_i18n_calendar_time_pattern(const robo_i18n_locale_t *loc,
                                              const robo_i18n_calendar_t *cal,
                                              robo_i18n_cal_format_style_t style,
                                              const char **out_utf8);

robo_result_t robo_i18n_calendar_datetime_pattern(const robo_i18n_locale_t *loc,
                                                  const robo_i18n_calendar_t *cal,
                                                  robo_i18n_cal_format_style_t style,
                                                  const char **out_utf8);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_CALENDAR_H */
