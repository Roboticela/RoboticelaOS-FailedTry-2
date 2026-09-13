/*
 * RoboI18N relative-time formatting (Goals 4.7 / 4.19).
 *
 * Formats a relative quantity (value + unit + direction) using CLDR
 * dateFields relative / relativeTime patterns and RoboI18N plural rules.
 *
 * AUTO mode uses CLDR relative-type keywords for −2…+1…+2 when present
 * (e.g. yesterday / today / tomorrow / day-before-yesterday).
 *
 * Does NOT compute “how far away” a calendar date is — callers supply the
 * relative value. Does NOT invent English words.
 *
 * Thread safety: formatter is not synchronized — one owner thread, or lock.
 */

#ifndef ROBO_I18N_RELATIVE_TIME_H
#define ROBO_I18N_RELATIVE_TIME_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NNumber.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_i18n_relative_unit {
    ROBO_I18N_REL_YEAR = 0,
    ROBO_I18N_REL_QUARTER = 1,
    ROBO_I18N_REL_MONTH = 2,
    ROBO_I18N_REL_WEEK = 3,
    ROBO_I18N_REL_DAY = 4,
    ROBO_I18N_REL_HOUR = 5,
    ROBO_I18N_REL_MINUTE = 6,
    ROBO_I18N_REL_SECOND = 7
} robo_i18n_relative_unit_t;

typedef enum robo_i18n_relative_width {
    ROBO_I18N_REL_WIDTH_LONG = 0,
    ROBO_I18N_REL_WIDTH_SHORT = 1,
    ROBO_I18N_REL_WIDTH_NARROW = 2
} robo_i18n_relative_width_t;

/**
 * numeric  — always use relativeTime patterns ("in 1 day")
 * auto     — use relative-type keywords for −2…+2 when CLDR provides them
 */
typedef enum robo_i18n_relative_numeric {
    ROBO_I18N_REL_NUMERIC_AUTO = 0,
    ROBO_I18N_REL_NUMERIC_ALWAYS = 1
} robo_i18n_relative_numeric_t;

typedef struct robo_i18n_relative_time {
    int64_t value; /* signed quantity; negative = past in auto sign convention */
    robo_i18n_relative_unit_t unit;
} robo_i18n_relative_time_t;

typedef struct robo_i18n_relative_options {
    robo_i18n_relative_width_t width;
    robo_i18n_relative_numeric_t numeric;
    char numbering_system[16];
} robo_i18n_relative_options_t;

typedef struct robo_i18n_relative_formatter {
    robo_i18n_locale_t locale;
    robo_i18n_relative_options_t options;
    robo_i18n_number_formatter_t number;
    uint8_t ready;
} robo_i18n_relative_formatter_t;

void robo_i18n_relative_options_init(robo_i18n_relative_options_t *opts);

robo_result_t robo_i18n_relative_time_init(robo_i18n_relative_time_t *out,
                                           int64_t value,
                                           robo_i18n_relative_unit_t unit);

robo_result_t robo_i18n_relative_formatter_init(robo_i18n_relative_formatter_t *fmt,
                                                const robo_i18n_locale_t *locale,
                                                const robo_i18n_relative_options_t *opts);

void robo_i18n_relative_formatter_destroy(robo_i18n_relative_formatter_t *fmt);

robo_result_t robo_i18n_relative_time_format(const robo_i18n_relative_formatter_t *fmt,
                                             const robo_i18n_relative_time_t *rel,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len);

robo_result_t robo_i18n_relative_time_format_unit(const robo_i18n_relative_formatter_t *fmt,
                                                  int64_t value,
                                                  robo_i18n_relative_unit_t unit,
                                                  char *buf,
                                                  size_t buf_cap,
                                                  size_t *out_len);

robo_result_t robo_i18n_relative_time_format_to_string(
    const robo_i18n_relative_formatter_t *fmt,
    const robo_i18n_relative_time_t *rel,
    robo_string_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_RELATIVE_TIME_H */
