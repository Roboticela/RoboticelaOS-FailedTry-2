/*
 * RoboI18N currency metadata & formatting (Goal 4.21).
 *
 * Currency identity / metadata / precision / cash rounding / formatting are
 * separate from FX conversion (out of scope). Parsing is Goal 4.22.
 *
 * Numeric model: prefer robo_i18n_decimal_t / integer minor units — not double —
 * for money values. Double helpers exist only as approximate convenience.
 *
 * Precedence for currency code:
 *   1. Explicit code argument
 *   2. Unicode locale extension `cu`
 *   3. Region default from CLDR currencyData
 *
 * Style `cf=account` selects accounting patterns when present.
 *
 * Thread safety: formatters are not synchronized — one owner thread, or lock.
 * Generated tables are immortal/read-only. No global mutable currency state.
 */

#ifndef ROBO_I18N_CURRENCY_H
#define ROBO_I18N_CURRENCY_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NNumber.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Currency display / format style (extensible without renumbering). */
typedef enum robo_i18n_currency_style {
    ROBO_I18N_CURRENCY_STYLE_SYMBOL = 0,
    ROBO_I18N_CURRENCY_STYLE_NARROW_SYMBOL = 1,
    ROBO_I18N_CURRENCY_STYLE_CODE = 2,
    ROBO_I18N_CURRENCY_STYLE_NAME = 3,
    ROBO_I18N_CURRENCY_STYLE_ACCOUNTING = 4
} robo_i18n_currency_style_t;

/**
 * Fraction / rounding usage.
 * STANDARD = electronic/accounting digits from CLDR fractions.
 * CASH = cashDigits/cashRounding when present; never applied unless requested.
 */
typedef enum robo_i18n_currency_usage {
    ROBO_I18N_CURRENCY_USAGE_STANDARD = 0,
    ROBO_I18N_CURRENCY_USAGE_CASH = 1
} robo_i18n_currency_usage_t;

typedef struct robo_i18n_currency_info {
    char code[4]; /* NUL-terminated ISO 4217 alpha */
    int16_t numeric; /* -1 if unknown */
    uint8_t digits;
    uint16_t rounding; /* 0 = unit at last fraction digit */
    int8_t cash_digits; /* -1 = same as digits */
    int16_t cash_rounding; /* -1 = same as rounding */
    uint8_t flags; /* ROBO_CURR_FLAG_* from RoboCurrencyData.h */
    uint8_t known; /* 1 if present in generated meta tables */
} robo_i18n_currency_info_t;

typedef struct robo_i18n_currency_options {
    robo_i18n_currency_style_t style;
    robo_i18n_currency_usage_t usage;
    robo_i18n_rounding_t rounding;
    char numbering_system[16]; /* empty = locale default */
    char currency[4]; /* empty = resolve from locale cu / region */
} robo_i18n_currency_options_t;

typedef struct robo_i18n_currency_formatter {
    robo_i18n_locale_t locale;
    robo_i18n_currency_options_t options;
    robo_i18n_number_formatter_t number;
    char resolved_currency[4];
    robo_i18n_currency_info_t info;
    uint8_t accounting; /* 1 if accounting patterns active */
    uint8_t ready;
} robo_i18n_currency_formatter_t;

void robo_i18n_currency_options_init(robo_i18n_currency_options_t *opts);

/**
 * Normalize a currency identifier to uppercase ISO 4217 alpha in out[4].
 * Malformed → ROBO_INVALID_ARGUMENT. Does not require the code to be known.
 */
robo_result_t robo_i18n_currency_normalize(const char *code,
                                           size_t code_len,
                                           char out[4]);

/** 1 if code is well-formed ISO 4217 alphabetic (after normalize rules). */
int robo_i18n_currency_is_well_formed(const char *code, size_t code_len);

/**
 * Lookup metadata. Well-formed unknown codes → ROBO_NOT_FOUND with DEFAULT
 * digits filled and known=0. Malformed → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_i18n_currency_lookup(const char *code,
                                        size_t code_len,
                                        robo_i18n_currency_info_t *out);

/** Region → current tender currency (CLDR). Unknown region → ROBO_NOT_FOUND. */
robo_result_t robo_i18n_currency_for_region(const char *region,
                                            size_t region_len,
                                            char out[4]);

/**
 * Locale default currency: explicit `cu` keyword, else maximized region map.
 */
robo_result_t robo_i18n_currency_for_locale(const robo_i18n_locale_t *locale,
                                            char out[4]);

robo_result_t robo_i18n_currency_formatter_init(
    robo_i18n_currency_formatter_t *fmt,
    const robo_i18n_locale_t *locale,
    const robo_i18n_currency_options_t *opts);

void robo_i18n_currency_formatter_destroy(robo_i18n_currency_formatter_t *fmt);

/** Effective ISO code for this formatter. */
const char *robo_i18n_currency_formatter_code(
    const robo_i18n_currency_formatter_t *fmt);

const robo_i18n_currency_info_t *robo_i18n_currency_formatter_info(
    const robo_i18n_currency_formatter_t *fmt);

/**
 * Format money value. Prefer decimal / i64 minor-unit helpers for exactness.
 * Buffer contract matches RoboI18NNumber.
 */
robo_result_t robo_i18n_currency_format(const robo_i18n_currency_formatter_t *fmt,
                                        const robo_i18n_decimal_t *value,
                                        char *buf,
                                        size_t buf_cap,
                                        size_t *out_len);

robo_result_t robo_i18n_currency_format_i64(const robo_i18n_currency_formatter_t *fmt,
                                            int64_t major_units,
                                            char *buf,
                                            size_t buf_cap,
                                            size_t *out_len);

/**
 * Format an integer amount in minor units (e.g. cents for USD digits=2).
 * major = minor / 10^digits (exact).
 */
robo_result_t robo_i18n_currency_format_minor(const robo_i18n_currency_formatter_t *fmt,
                                              int64_t minor_units,
                                              char *buf,
                                              size_t buf_cap,
                                              size_t *out_len);

robo_result_t robo_i18n_currency_format_double(const robo_i18n_currency_formatter_t *fmt,
                                               double value,
                                               char *buf,
                                               size_t buf_cap,
                                               size_t *out_len);

robo_result_t robo_i18n_currency_format_to_string(
    const robo_i18n_currency_formatter_t *fmt,
    const robo_i18n_decimal_t *value,
    robo_string_t *out);

const char *robo_i18n_currency_cldr_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_CURRENCY_H */
