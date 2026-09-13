/*
 * RoboI18N date/time interval formatting (Goal 4.7).
 *
 * Formats [start, end] using CLDR intervalFormats. Repeated fields are
 * collapsed only when the selected CLDR pattern encodes that collapse.
 *
 * Does NOT silently reorder reversed intervals (returns ROBO_INVALID_ARGUMENT
 * unless normalize is requested). Prefer instant-based intervals when zones
 * matter. IANA zones without tzdb → ROBO_NOT_SUPPORTED for instant conversion.
 *
 * Thread safety: formatter is not synchronized — one owner thread, or lock.
 */

#ifndef ROBO_I18N_DATETIME_INTERVAL_H
#define ROBO_I18N_DATETIME_INTERVAL_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NCalendar.h"
#include "RoboI18N/RoboI18NDateTime.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NTimeZone.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_I18N_INTERVAL_SKELETON_MAX 64u

typedef struct robo_i18n_datetime_interval {
    robo_i18n_datetime_t start;
    robo_i18n_datetime_t end;
} robo_i18n_datetime_interval_t;

typedef struct robo_i18n_instant_interval {
    robo_i18n_instant_t start;
    robo_i18n_instant_t end;
} robo_i18n_instant_interval_t;

typedef struct robo_i18n_interval_options {
    char skeleton[ROBO_I18N_INTERVAL_SKELETON_MAX]; /* empty = yMMMd / Hm heuristic */
    int normalize_reversed;                        /* 1 = swap if start > end */
    char numbering_system[16];
} robo_i18n_interval_options_t;

typedef struct robo_i18n_interval_formatter {
    robo_i18n_locale_t locale;
    robo_i18n_calendar_t calendar;
    robo_i18n_timezone_t timezone;
    robo_i18n_interval_options_t options;
    uint8_t ready;
} robo_i18n_interval_formatter_t;

void robo_i18n_interval_options_init(robo_i18n_interval_options_t *opts);

/**
 * Validate interval. Equal endpoints are valid.
 * start > end → ROBO_INVALID_ARGUMENT (unless checking only).
 */
robo_result_t robo_i18n_datetime_interval_init(robo_i18n_datetime_interval_t *out,
                                               const robo_i18n_datetime_t *start,
                                               const robo_i18n_datetime_t *end,
                                               int normalize_reversed);

robo_result_t robo_i18n_instant_interval_init(robo_i18n_instant_interval_t *out,
                                              const robo_i18n_instant_t *start,
                                              const robo_i18n_instant_t *end,
                                              int normalize_reversed);

robo_result_t robo_i18n_interval_formatter_init(robo_i18n_interval_formatter_t *fmt,
                                                const robo_i18n_locale_t *locale,
                                                const robo_i18n_calendar_t *calendar,
                                                const robo_i18n_timezone_t *tz,
                                                const robo_i18n_interval_options_t *opts);

void robo_i18n_interval_formatter_destroy(robo_i18n_interval_formatter_t *fmt);

/**
 * Greatest differing CLDR field letter among y M d H h m s (and a when 12h).
 * Writes a single ASCII letter into @p out_letter[0]; empty if identical.
 */
robo_result_t robo_i18n_interval_greatest_difference(const robo_i18n_datetime_t *start,
                                                     const robo_i18n_datetime_t *end,
                                                     char *out_letter,
                                                     size_t letter_cap);

robo_result_t robo_i18n_datetime_interval_format(const robo_i18n_interval_formatter_t *fmt,
                                                 const robo_i18n_datetime_interval_t *interval,
                                                 char *buf,
                                                 size_t buf_cap,
                                                 size_t *out_len);

robo_result_t robo_i18n_date_interval_format(const robo_i18n_interval_formatter_t *fmt,
                                             const robo_i18n_date_t *start,
                                             const robo_i18n_date_t *end,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len);

robo_result_t robo_i18n_time_interval_format(const robo_i18n_interval_formatter_t *fmt,
                                             const robo_i18n_time_t *start,
                                             const robo_i18n_time_t *end,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len);

robo_result_t robo_i18n_instant_interval_format(const robo_i18n_interval_formatter_t *fmt,
                                                const robo_i18n_instant_interval_t *interval,
                                                char *buf,
                                                size_t buf_cap,
                                                size_t *out_len);

robo_result_t robo_i18n_datetime_interval_format_to_string(
    const robo_i18n_interval_formatter_t *fmt,
    const robo_i18n_datetime_interval_t *interval,
    robo_string_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_DATETIME_INTERVAL_H */
