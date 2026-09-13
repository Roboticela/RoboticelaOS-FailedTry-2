/*
 * RoboI18N measurement systems & unit preferences (Goal 4.20).
 *
 * Selects which units a locale/preference context should use. Formatting and
 * conversion remain in RoboI18NUnit (Goal 4.19). Measurement system lookup
 * from Goal 4.2 is reused and extended with CLDR unitPreferenceData.
 *
 * Precedence for preferred unit resolution:
 *   1. Explicit unit argument (API call)
 *   2. Per-category override on the preference context
 *   3. System-level override on the context (affects category defaults)
 *   4. Unicode locale extension `ms` (metric / ussystem / uksystem)
 *   5. CLDR unitPreferenceData for region + usage (fallback region 001)
 *   6. Measurement-system heuristic defaults
 *
 * An explicit unit request is never replaced by locale defaults.
 *
 * Thread safety: contexts are value types (no heap). Not synchronized —
 * share only when immutable / read-only after init, or lock externally.
 */

#ifndef ROBO_I18N_MEASURE_H
#define ROBO_I18N_MEASURE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboI18NNumber.h"
#include "RoboI18N/RoboI18NUnit.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Semantic measurement categories (stable). New CLDR categories can be added
 * at the end without renumbering existing values.
 */
typedef enum robo_i18n_measure_category {
    ROBO_I18N_MEASURE_CAT_TEMPERATURE = 0,
    ROBO_I18N_MEASURE_CAT_LENGTH = 1,
    ROBO_I18N_MEASURE_CAT_AREA = 2,
    ROBO_I18N_MEASURE_CAT_MASS = 3,
    ROBO_I18N_MEASURE_CAT_VOLUME = 4,
    ROBO_I18N_MEASURE_CAT_SPEED = 5,
    ROBO_I18N_MEASURE_CAT_PRESSURE = 6,
    ROBO_I18N_MEASURE_CAT_ENERGY = 7,
    ROBO_I18N_MEASURE_CAT_POWER = 8,
    ROBO_I18N_MEASURE_CAT_COUNT = 9
} robo_i18n_measure_category_t;

/**
 * Preference usage within a category (CLDR usage names). DEFAULT is always
 * valid; other usages are category-specific (unsupported → fall back DEFAULT).
 */
typedef enum robo_i18n_measure_usage {
    ROBO_I18N_MEASURE_USAGE_DEFAULT = 0,
    ROBO_I18N_MEASURE_USAGE_WEATHER = 1,
    ROBO_I18N_MEASURE_USAGE_ROAD = 2,
    ROBO_I18N_MEASURE_USAGE_FLUID = 3,
    ROBO_I18N_MEASURE_USAGE_FOOD = 4,
    ROBO_I18N_MEASURE_USAGE_ENGINE = 5,
    ROBO_I18N_MEASURE_USAGE_PERSON = 6,
    ROBO_I18N_MEASURE_USAGE_LAND = 7,
    ROBO_I18N_MEASURE_USAGE_COUNT = 8
} robo_i18n_measure_usage_t;

typedef enum robo_i18n_measure_source {
    ROBO_I18N_MEASURE_SRC_EXPLICIT = 0,
    ROBO_I18N_MEASURE_SRC_CATEGORY_OVERRIDE = 1,
    ROBO_I18N_MEASURE_SRC_SYSTEM_OVERRIDE = 2,
    ROBO_I18N_MEASURE_SRC_UNICODE_MS = 3,
    ROBO_I18N_MEASURE_SRC_CLDR_PREFERENCE = 4,
    ROBO_I18N_MEASURE_SRC_SYSTEM_DEFAULT = 5
} robo_i18n_measure_source_t;

/**
 * Immutable-after-init preference context (stack-allocatable).
 * No global mutable preference state.
 */
typedef struct robo_i18n_measure_prefs {
    robo_i18n_locale_t locale;
    char region[8];
    robo_i18n_measurement_t system; /* effective measurement system */
    robo_i18n_measurement_t temp_system; /* temperature category system */
    uint8_t system_overridden; /* 1 if set_system was used */
    uint8_t has_cat_override[ROBO_I18N_MEASURE_CAT_COUNT];
    robo_i18n_unit_id_t cat_override[ROBO_I18N_MEASURE_CAT_COUNT];
    uint8_t ready;
} robo_i18n_measure_prefs_t;

void robo_i18n_measure_prefs_clear(robo_i18n_measure_prefs_t *prefs);

/**
 * Build locale-derived defaults (region maximize + CLDR systems + ms keyword).
 * Does not read OS settings. Safe to call repeatedly.
 */
robo_result_t robo_i18n_measure_prefs_init(robo_i18n_measure_prefs_t *prefs,
                                           const robo_i18n_locale_t *locale);

/** Override measurement system (metric/US/UK). Does not clear category overrides. */
robo_result_t robo_i18n_measure_prefs_set_system(robo_i18n_measure_prefs_t *prefs,
                                                 robo_i18n_measurement_t system);

/** Clear system override; re-resolve from locale / Unicode `ms`. */
robo_result_t robo_i18n_measure_prefs_clear_system(robo_i18n_measure_prefs_t *prefs);

/** Per-category preferred unit override (must match category quantity). */
robo_result_t robo_i18n_measure_prefs_set_unit(robo_i18n_measure_prefs_t *prefs,
                                               robo_i18n_measure_category_t category,
                                               robo_i18n_unit_id_t unit);

robo_result_t robo_i18n_measure_prefs_clear_unit(robo_i18n_measure_prefs_t *prefs,
                                                 robo_i18n_measure_category_t category);

robo_i18n_measurement_t robo_i18n_measure_prefs_system(
    const robo_i18n_measure_prefs_t *prefs);

robo_i18n_measurement_t robo_i18n_measure_prefs_temperature_system(
    const robo_i18n_measure_prefs_t *prefs);

/**
 * Resolve preferred unit. @p explicit_unit non-NULL → always wins (SRC_EXPLICIT).
 * Unknown category → ROBO_NOT_SUPPORTED.
 */
robo_result_t robo_i18n_measure_resolve_unit(const robo_i18n_measure_prefs_t *prefs,
                                             robo_i18n_measure_category_t category,
                                             robo_i18n_measure_usage_t usage,
                                             const robo_i18n_unit_id_t *explicit_unit,
                                             robo_i18n_unit_id_t *out_unit,
                                             robo_i18n_measure_source_t *out_src);

robo_result_t robo_i18n_measure_preferred_unit(const robo_i18n_measure_prefs_t *prefs,
                                               robo_i18n_measure_category_t category,
                                               robo_i18n_unit_id_t *out_unit);

/**
 * Convenience: preferred unit for a usage (e.g. VOLUME+FLUID → gallon in US).
 */
robo_result_t robo_i18n_measure_preferred_unit_usage(
    const robo_i18n_measure_prefs_t *prefs,
    robo_i18n_measure_category_t category,
    robo_i18n_measure_usage_t usage,
    robo_i18n_unit_id_t *out_unit);

/**
 * Convert @p value from @p from_unit into the resolved preferred unit, then
 * format with @p fmt. If @p explicit_unit is set, that unit is used (no
 * preference override). Uses RoboI18NUnit conversion + formatting.
 */
robo_result_t robo_i18n_measure_format_converted(
    const robo_i18n_measure_prefs_t *prefs,
    const robo_i18n_unit_formatter_t *fmt,
    robo_i18n_measure_category_t category,
    robo_i18n_measure_usage_t usage,
    const robo_i18n_decimal_t *value,
    robo_i18n_unit_id_t from_unit,
    const robo_i18n_unit_id_t *explicit_unit,
    char *buf,
    size_t buf_cap,
    size_t *out_len);

robo_result_t robo_i18n_measure_format_converted_i64(
    const robo_i18n_measure_prefs_t *prefs,
    const robo_i18n_unit_formatter_t *fmt,
    robo_i18n_measure_category_t category,
    robo_i18n_measure_usage_t usage,
    int64_t value,
    robo_i18n_unit_id_t from_unit,
    const robo_i18n_unit_id_t *explicit_unit,
    char *buf,
    size_t buf_cap,
    size_t *out_len);

/** Locale-only helpers (no preference context). */
robo_result_t robo_i18n_measure_system_for_locale(const robo_i18n_locale_t *locale,
                                                  robo_i18n_measurement_t *out);

robo_result_t robo_i18n_measure_temperature_system_for_locale(
    const robo_i18n_locale_t *locale,
    robo_i18n_measurement_t *out);

const char *robo_i18n_measure_category_name(robo_i18n_measure_category_t category);
const char *robo_i18n_measure_usage_name(robo_i18n_measure_usage_t usage);
const char *robo_i18n_measure_cldr_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_MEASURE_H */
