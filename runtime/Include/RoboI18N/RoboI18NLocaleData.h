/*
 * RoboI18N locale data — CLDR 46 derived metadata (Goal 4.2).
 *
 * Not number/date formatting. Not language detection.
 */

#ifndef ROBO_I18N_LOCALE_DATA_H
#define ROBO_I18N_LOCALE_DATA_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NLocale.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_i18n_measurement {
    ROBO_I18N_MEASURE_METRIC = 0,
    ROBO_I18N_MEASURE_US = 1,
    ROBO_I18N_MEASURE_UK = 2
} robo_i18n_measurement_t;

typedef struct robo_i18n_numbering_system {
    char id[16];
    char type[16];   /* "numeric" or "algorithmic" */
    char digits[80]; /* UTF-8 concatenation of 10 digits; empty if algorithmic */
} robo_i18n_numbering_system_t;

/**
 * CLDR likely-subtags maximize. Writes a new locale into @p out.
 * Does not modify @p in. Inferred script/region may be filled.
 */
robo_result_t robo_i18n_locale_maximize(const robo_i18n_locale_t *in,
                                        robo_i18n_locale_t *out);

/**
 * CLDR likely-subtags minimize. Maximize first, then drop redundant script/region.
 */
robo_result_t robo_i18n_locale_minimize(const robo_i18n_locale_t *in,
                                        robo_i18n_locale_t *out);

/**
 * CLDR data inheritance parent (parentLocales map, else truncation, else root "root").
 * When @p in is already root → ROBO_NOT_FOUND.
 */
robo_result_t robo_i18n_locale_data_parent(const robo_i18n_locale_t *in,
                                           robo_i18n_locale_t *out);

/** Default numbering system id for locale (inheritance → latn). */
robo_result_t robo_i18n_locale_default_numbering_system(const robo_i18n_locale_t *loc,
                                                        char *out_id,
                                                        size_t out_cap);

/** Native numbering system when published; else same as default. */
robo_result_t robo_i18n_locale_native_numbering_system(const robo_i18n_locale_t *loc,
                                                       char *out_id,
                                                       size_t out_cap);

robo_result_t robo_i18n_numbering_system_get(const char *id,
                                             robo_i18n_numbering_system_t *out);

/**
 * Preferred calendars for locale's region (CLDR calendarPreferenceData).
 * Writes up to @p cap ids; @p out_count set. Ids are CLDR names (e.g. "gregorian").
 */
robo_result_t robo_i18n_locale_calendars(const robo_i18n_locale_t *loc,
                                         char out_ids[][32],
                                         size_t cap,
                                         size_t *out_count);

/** First preferred calendar (default). */
robo_result_t robo_i18n_locale_default_calendar(const robo_i18n_locale_t *loc,
                                                char *out_id,
                                                size_t out_cap);

robo_result_t robo_i18n_locale_measurement_system(const robo_i18n_locale_t *loc,
                                                  robo_i18n_measurement_t *out);

const char *robo_i18n_cldr_unicode_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_LOCALE_DATA_H */
