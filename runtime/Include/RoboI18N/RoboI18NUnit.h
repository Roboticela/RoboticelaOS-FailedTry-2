/*
 * RoboI18N unit formatting + conversion foundation (Goal 4.19).
 *
 * Formats measurement values with CLDR unit patterns (long/short/narrow),
 * plural categories, and locale-aware number formatting. Compound "per"
 * units use perUnitPattern when available, else compoundUnitPattern.
 *
 * Conversion is separate from localization: convert quantities first, then
 * format. Exact rational SI conversions where practical; approximate
 * factors are documented on each conversion path.
 *
 * Does NOT parse unit strings (Goal 4.22 territory).
 *
 * Thread safety: a formatter is not synchronized — one owner thread, or lock.
 * Generated CLDR tables are immortal/read-only.
 */

#ifndef ROBO_I18N_UNIT_H
#define ROBO_I18N_UNIT_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NNumber.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Curated CLDR unit identifiers (type-subtype). Order matches RoboUnitData.
 * Unsupported ids return ROBO_NOT_SUPPORTED / ROBO_INVALID_ARGUMENT.
 */
typedef enum robo_i18n_unit_id {
    ROBO_I18N_UNIT_LENGTH_KILOMETER = 0,
    ROBO_I18N_UNIT_LENGTH_METER = 1,
    ROBO_I18N_UNIT_LENGTH_CENTIMETER = 2,
    ROBO_I18N_UNIT_LENGTH_MILLIMETER = 3,
    ROBO_I18N_UNIT_LENGTH_MILE = 4,
    ROBO_I18N_UNIT_LENGTH_YARD = 5,
    ROBO_I18N_UNIT_LENGTH_FOOT = 6,
    ROBO_I18N_UNIT_LENGTH_INCH = 7,
    ROBO_I18N_UNIT_MASS_KILOGRAM = 8,
    ROBO_I18N_UNIT_MASS_GRAM = 9,
    ROBO_I18N_UNIT_MASS_POUND = 10,
    ROBO_I18N_UNIT_MASS_OUNCE = 11,
    ROBO_I18N_UNIT_AREA_SQUARE_KILOMETER = 12,
    ROBO_I18N_UNIT_AREA_SQUARE_METER = 13,
    ROBO_I18N_UNIT_AREA_HECTARE = 14,
    ROBO_I18N_UNIT_AREA_ACRE = 15,
    ROBO_I18N_UNIT_VOLUME_LITER = 16,
    ROBO_I18N_UNIT_VOLUME_MILLILITER = 17,
    ROBO_I18N_UNIT_VOLUME_GALLON = 18,
    ROBO_I18N_UNIT_VOLUME_CUBIC_METER = 19,
    ROBO_I18N_UNIT_DURATION_YEAR = 20,
    ROBO_I18N_UNIT_DURATION_MONTH = 21,
    ROBO_I18N_UNIT_DURATION_WEEK = 22,
    ROBO_I18N_UNIT_DURATION_DAY = 23,
    ROBO_I18N_UNIT_DURATION_HOUR = 24,
    ROBO_I18N_UNIT_DURATION_MINUTE = 25,
    ROBO_I18N_UNIT_DURATION_SECOND = 26,
    ROBO_I18N_UNIT_DURATION_MILLISECOND = 27,
    ROBO_I18N_UNIT_SPEED_KILOMETER_PER_HOUR = 28,
    ROBO_I18N_UNIT_SPEED_METER_PER_SECOND = 29,
    ROBO_I18N_UNIT_SPEED_MILE_PER_HOUR = 30,
    ROBO_I18N_UNIT_TEMPERATURE_CELSIUS = 31,
    ROBO_I18N_UNIT_TEMPERATURE_FAHRENHEIT = 32,
    ROBO_I18N_UNIT_TEMPERATURE_KELVIN = 33,
    ROBO_I18N_UNIT_DIGITAL_BIT = 34,
    ROBO_I18N_UNIT_DIGITAL_BYTE = 35,
    ROBO_I18N_UNIT_DIGITAL_KILOBYTE = 36,
    ROBO_I18N_UNIT_DIGITAL_MEGABYTE = 37,
    ROBO_I18N_UNIT_DIGITAL_GIGABYTE = 38,
    ROBO_I18N_UNIT_DIGITAL_TERABYTE = 39,
    ROBO_I18N_UNIT_ENERGY_JOULE = 40,
    ROBO_I18N_UNIT_ENERGY_KILOWATT_HOUR = 41,
    ROBO_I18N_UNIT_ENERGY_CALORIE = 42,
    ROBO_I18N_UNIT_ENERGY_FOODCALORIE = 43,
    ROBO_I18N_UNIT_POWER_WATT = 44,
    ROBO_I18N_UNIT_POWER_KILOWATT = 45,
    ROBO_I18N_UNIT_POWER_HORSEPOWER = 46,
    ROBO_I18N_UNIT_PRESSURE_PASCAL = 47,
    ROBO_I18N_UNIT_PRESSURE_HECTOPASCAL = 48,
    ROBO_I18N_UNIT_PRESSURE_MILLIBAR = 49,
    ROBO_I18N_UNIT_PRESSURE_POUND_FORCE_PER_SQUARE_INCH = 50,
    ROBO_I18N_UNIT_FREQUENCY_HERTZ = 51,
    ROBO_I18N_UNIT_FREQUENCY_KILOHERTZ = 52,
    ROBO_I18N_UNIT_FREQUENCY_MEGAHERTZ = 53,
    ROBO_I18N_UNIT_FREQUENCY_GIGAHERTZ = 54,
    ROBO_I18N_UNIT_ANGLE_DEGREE = 55,
    ROBO_I18N_UNIT_ANGLE_RADIAN = 56,
    ROBO_I18N_UNIT_ANGLE_ARC_MINUTE = 57,
    ROBO_I18N_UNIT_ANGLE_ARC_SECOND = 58,
    ROBO_I18N_UNIT_CONSUMPTION_LITER_PER_100_KILOMETER = 59,
    ROBO_I18N_UNIT_COUNT = 60
} robo_i18n_unit_id_t;

typedef enum robo_i18n_unit_width {
    ROBO_I18N_UNIT_WIDTH_LONG = 0,
    ROBO_I18N_UNIT_WIDTH_SHORT = 1,
    ROBO_I18N_UNIT_WIDTH_NARROW = 2
} robo_i18n_unit_width_t;

typedef enum robo_i18n_unit_quantity {
    ROBO_I18N_UNIT_Q_LENGTH = 0,
    ROBO_I18N_UNIT_Q_MASS = 1,
    ROBO_I18N_UNIT_Q_AREA = 2,
    ROBO_I18N_UNIT_Q_VOLUME = 3,
    ROBO_I18N_UNIT_Q_DURATION = 4,
    ROBO_I18N_UNIT_Q_SPEED = 5,
    ROBO_I18N_UNIT_Q_TEMPERATURE = 6,
    ROBO_I18N_UNIT_Q_DIGITAL = 7,
    ROBO_I18N_UNIT_Q_ENERGY = 8,
    ROBO_I18N_UNIT_Q_POWER = 9,
    ROBO_I18N_UNIT_Q_PRESSURE = 10,
    ROBO_I18N_UNIT_Q_FREQUENCY = 11,
    ROBO_I18N_UNIT_Q_ANGLE = 12,
    ROBO_I18N_UNIT_Q_CONSUMPTION = 13,
    ROBO_I18N_UNIT_Q_UNKNOWN = 255
} robo_i18n_unit_quantity_t;

typedef struct robo_i18n_unit_options {
    robo_i18n_unit_width_t width;
    char numbering_system[16];
} robo_i18n_unit_options_t;

typedef struct robo_i18n_unit_formatter {
    robo_i18n_locale_t locale;
    robo_i18n_unit_options_t options;
    robo_i18n_number_formatter_t number;
    uint8_t ready;
} robo_i18n_unit_formatter_t;

void robo_i18n_unit_options_init(robo_i18n_unit_options_t *opts);

robo_result_t robo_i18n_unit_formatter_init(robo_i18n_unit_formatter_t *fmt,
                                            const robo_i18n_locale_t *locale,
                                            const robo_i18n_unit_options_t *opts);

void robo_i18n_unit_formatter_destroy(robo_i18n_unit_formatter_t *fmt);

/** CLDR key for a curated unit id, or NULL if out of range. */
const char *robo_i18n_unit_cldr_key(robo_i18n_unit_id_t unit);

/** Resolve CLDR type-subtype key to curated id (ROBO_NOT_FOUND if unknown). */
robo_result_t robo_i18n_unit_id_from_cldr_key(const char *key, robo_i18n_unit_id_t *out);

robo_i18n_unit_quantity_t robo_i18n_unit_quantity(robo_i18n_unit_id_t unit);

/**
 * True when patterns exist for the resolved locale (after fallback) at the
 * formatter width. Unknown locales fall back toward English.
 */
int robo_i18n_unit_has_patterns(const robo_i18n_locale_t *locale,
                                robo_i18n_unit_id_t unit,
                                robo_i18n_unit_width_t width);

robo_result_t robo_i18n_unit_format(const robo_i18n_unit_formatter_t *fmt,
                                    const robo_i18n_decimal_t *value,
                                    robo_i18n_unit_id_t unit,
                                    char *buf,
                                    size_t buf_cap,
                                    size_t *out_len);

robo_result_t robo_i18n_unit_format_i64(const robo_i18n_unit_formatter_t *fmt,
                                        int64_t value,
                                        robo_i18n_unit_id_t unit,
                                        char *buf,
                                        size_t buf_cap,
                                        size_t *out_len);

robo_result_t robo_i18n_unit_format_double(const robo_i18n_unit_formatter_t *fmt,
                                           double value,
                                           robo_i18n_unit_id_t unit,
                                           char *buf,
                                           size_t buf_cap,
                                           size_t *out_len);

/**
 * Compound “value unit_num per unit_den” (e.g. megabyte / second).
 * Prefers unit_den’s perUnitPattern; else compound “per” + displayName.
 * Precomposed units (km/h, L/100km) should use format_* with that unit id.
 */
robo_result_t robo_i18n_unit_format_per(const robo_i18n_unit_formatter_t *fmt,
                                        const robo_i18n_decimal_t *value,
                                        robo_i18n_unit_id_t unit_num,
                                        robo_i18n_unit_id_t unit_den,
                                        char *buf,
                                        size_t buf_cap,
                                        size_t *out_len);

robo_result_t robo_i18n_unit_format_per_i64(const robo_i18n_unit_formatter_t *fmt,
                                            int64_t value,
                                            robo_i18n_unit_id_t unit_num,
                                            robo_i18n_unit_id_t unit_den,
                                            char *buf,
                                            size_t buf_cap,
                                            size_t *out_len);

robo_result_t robo_i18n_unit_format_to_string(const robo_i18n_unit_formatter_t *fmt,
                                              const robo_i18n_decimal_t *value,
                                              robo_i18n_unit_id_t unit,
                                              robo_string_t *out);

/**
 * Convert between compatible units into an exact decimal when possible.
 *
 * Exact: metric length/mass/area/volume/duration/digital/frequency factors
 * that are integer powers of 10 (or exact binary digital 1000-based SI
 * prefixes used by CLDR digital-* keys).
 *
 * Approximate (IEEE-754 double path, marked via *approximate_out=1):
 * customary length/mass/volume, horsepower, psi, food calorie, temperature
 * affine transforms, speed customary, acre/hectare bridges.
 *
 * Cross-quantity conversion → ROBO_NOT_SUPPORTED.
 * Temperature uses affine formulas (not a pure scale).
 */
robo_result_t robo_i18n_unit_convert(robo_i18n_unit_id_t from,
                                     robo_i18n_unit_id_t to,
                                     const robo_i18n_decimal_t *in,
                                     robo_i18n_decimal_t *out,
                                     int *approximate_out);

robo_result_t robo_i18n_unit_convert_i64(robo_i18n_unit_id_t from,
                                         robo_i18n_unit_id_t to,
                                         int64_t value,
                                         int64_t *out,
                                         int *approximate_out);

const char *robo_i18n_unit_cldr_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_UNIT_H */
