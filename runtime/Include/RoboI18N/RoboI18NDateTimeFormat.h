/*
 * RoboI18N localized date/time formatting (Goal 4.6).
 *
 * Converts date / time / datetime / instant values into locale-aware UTF-8
 * strings using CLDR patterns, field names, and numbering-system digits.
 *
 * Does NOT:
 *   - invent localized data
 *   - perform bidi visual reordering (logical order only)
 *   - implement relative time or interval formatting
 *   - invent localized zone names (offset / id only until tz names exist)
 *
 * Ambiguous/nonexistent local times: prefer formatting from an instant.
 * Formatting from a local datetime + zone that requires conversion returns
 * ROBO_NOT_SUPPORTED for IANA zones without tzdb; FIXED/UTC are UNIQUE.
 *
 * Thread safety: formatter is not synchronized — one owner thread, or lock.
 * Pattern compilation is cached inside the formatter (no global mutable cache).
 */

#ifndef ROBO_I18N_DATETIME_FORMAT_H
#define ROBO_I18N_DATETIME_FORMAT_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NCalendar.h"
#include "RoboI18N/RoboI18NDateTime.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NNumber.h"
#include "RoboI18N/RoboI18NTimeZone.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_I18N_DTFMT_PATTERN_MAX 192u
#define ROBO_I18N_DTFMT_TOKEN_MAX 96u
#define ROBO_I18N_DTFMT_SKELETON_MAX 64u

typedef enum robo_i18n_dtfmt_kind {
    ROBO_I18N_DTFMT_DATE = 0,
    ROBO_I18N_DTFMT_TIME = 1,
    ROBO_I18N_DTFMT_DATETIME = 2
} robo_i18n_dtfmt_kind_t;

typedef struct robo_i18n_dtfmt_options {
    robo_i18n_cal_format_style_t date_style; /* DATE / DATETIME */
    robo_i18n_cal_format_style_t time_style; /* TIME / DATETIME */
    char numbering_system[16];               /* empty = locale default */
} robo_i18n_dtfmt_options_t;

/**
 * Compiled pattern token. Literals reference bytes inside formatter->pattern.
 * field is the CLDR pattern letter (e.g. 'y','M','d','H','a','z').
 */
typedef struct robo_i18n_dtfmt_token {
    uint8_t is_literal;
    uint8_t field;
    uint8_t count;
    uint16_t lit_off;
    uint16_t lit_len;
} robo_i18n_dtfmt_token_t;

/**
 * Stack formatter. Compiles the active pattern into tokens on init.
 * Resolved names/patterns come from immortal CLDR tables.
 */
typedef struct robo_i18n_datetime_formatter {
    robo_i18n_locale_t locale;
    robo_i18n_calendar_t calendar;
    robo_i18n_timezone_t timezone;
    robo_i18n_dtfmt_options_t options;
    robo_i18n_number_formatter_t number;
    char pattern[ROBO_I18N_DTFMT_PATTERN_MAX];
    robo_i18n_dtfmt_token_t tokens[ROBO_I18N_DTFMT_TOKEN_MAX];
    uint16_t token_count;
    uint8_t ready;
} robo_i18n_datetime_formatter_t;

void robo_i18n_dtfmt_options_init(robo_i18n_dtfmt_options_t *opts);

/**
 * Initialize for style-based formatting (date/time/datetime).
 * @p calendar NULL → gregorian. @p tz NULL → UTC.
 * Calendars without conversion support → ROBO_NOT_SUPPORTED.
 */
robo_result_t robo_i18n_datetime_formatter_init(robo_i18n_datetime_formatter_t *fmt,
                                                const robo_i18n_locale_t *locale,
                                                const robo_i18n_calendar_t *calendar,
                                                const robo_i18n_timezone_t *tz,
                                                robo_i18n_dtfmt_kind_t kind,
                                                const robo_i18n_dtfmt_options_t *opts);

/** Initialize with an explicit CLDR pattern string. */
robo_result_t robo_i18n_datetime_formatter_init_pattern(
    robo_i18n_datetime_formatter_t *fmt,
    const robo_i18n_locale_t *locale,
    const robo_i18n_calendar_t *calendar,
    const robo_i18n_timezone_t *tz,
    const char *pattern,
    const robo_i18n_dtfmt_options_t *opts);

/**
 * Initialize from a CLDR skeleton (e.g. "yMMMd", "Hm").
 * Matching: exact availableFormats hit, else best field-coverage match
 * (see i18n/README.md).
 */
robo_result_t robo_i18n_datetime_formatter_init_skeleton(
    robo_i18n_datetime_formatter_t *fmt,
    const robo_i18n_locale_t *locale,
    const robo_i18n_calendar_t *calendar,
    const robo_i18n_timezone_t *tz,
    const char *skeleton,
    const robo_i18n_dtfmt_options_t *opts);

void robo_i18n_datetime_formatter_destroy(robo_i18n_datetime_formatter_t *fmt);

/** Resolve skeleton → immortal pattern string (CLDR availableFormats). */
robo_result_t robo_i18n_datetime_resolve_skeleton(const robo_i18n_locale_t *locale,
                                                  const robo_i18n_calendar_t *calendar,
                                                  const char *skeleton,
                                                  const char **out_pattern);

robo_result_t robo_i18n_datetime_format_date(const robo_i18n_datetime_formatter_t *fmt,
                                             const robo_i18n_date_t *date,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len);

robo_result_t robo_i18n_datetime_format_time(const robo_i18n_datetime_formatter_t *fmt,
                                             const robo_i18n_time_t *time,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len);

robo_result_t robo_i18n_datetime_format_datetime(const robo_i18n_datetime_formatter_t *fmt,
                                                 const robo_i18n_datetime_t *dt,
                                                 char *buf,
                                                 size_t buf_cap,
                                                 size_t *out_len);

/**
 * Format an absolute instant in the formatter's time zone (to local fields).
 * IANA zones without tzdb → ROBO_NOT_SUPPORTED.
 */
robo_result_t robo_i18n_datetime_format_instant(const robo_i18n_datetime_formatter_t *fmt,
                                                const robo_i18n_instant_t *instant,
                                                char *buf,
                                                size_t buf_cap,
                                                size_t *out_len);

robo_result_t robo_i18n_datetime_format_to_string(const robo_i18n_datetime_formatter_t *fmt,
                                                  const robo_i18n_datetime_t *dt,
                                                  robo_string_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_DATETIME_FORMAT_H */
