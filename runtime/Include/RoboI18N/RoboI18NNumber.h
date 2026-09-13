/*
 * RoboI18N number formatting foundation (Goal 4.3).
 *
 * Locale-aware decimal formatting using CLDR symbols, grouping patterns,
 * and numbering-system digits. Not currency / percent / compact / scientific.
 *
 * Numeric model:
 *   - Exact: int64 / uint64 / robo_i18n_decimal_t (ASCII digit payload)
 *   - Approximate: IEEE-754 binary64 via robo_i18n_number_format_double
 *     (documented specials + conversion limits; not exact decimal).
 *
 * Thread safety: a formatter is not synchronized. Treat as owned by one
 * thread, or protect externally. Configuration is fixed after init.
 */

#ifndef ROBO_I18N_NUMBER_H
#define ROBO_I18N_NUMBER_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NLocale.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_I18N_DECIMAL_DIGITS_MAX 40u

typedef enum robo_i18n_rounding {
    ROBO_I18N_ROUND_HALF_EVEN = 0, /* banker's rounding (default) */
    ROBO_I18N_ROUND_HALF_UP = 1,   /* away from zero on ties */
    ROBO_I18N_ROUND_HALF_DOWN = 2, /* toward zero on ties */
    ROBO_I18N_ROUND_DOWN = 3,      /* toward zero */
    ROBO_I18N_ROUND_UP = 4,        /* away from zero */
    ROBO_I18N_ROUND_CEILING = 5,   /* toward +inf */
    ROBO_I18N_ROUND_FLOOR = 6      /* toward -inf */
} robo_i18n_rounding_t;

typedef enum robo_i18n_decimal_kind {
    ROBO_I18N_DECIMAL_NUMBER = 0,
    ROBO_I18N_DECIMAL_NAN = 1,
    ROBO_I18N_DECIMAL_POS_INFINITY = 2,
    ROBO_I18N_DECIMAL_NEG_INFINITY = 3
} robo_i18n_decimal_kind_t;

/**
 * Exact decimal payload for formatting.
 * digits[] holds ASCII '0'..'9' only, most-significant first.
 * scale = number of fraction digits (trailing digits are fractional).
 * Example: value -12.34 → negative=1, digit_count=4, digits="1234", scale=2.
 * Zero: digit_count=1, digits="0", scale=0, negative=0 (or 1 for -0).
 */
typedef struct robo_i18n_decimal {
    robo_i18n_decimal_kind_t kind;
    uint8_t negative; /* 1 if sign is negative (incl. -0) */
    uint8_t scale;
    uint8_t digit_count;
    char digits[ROBO_I18N_DECIMAL_DIGITS_MAX];
} robo_i18n_decimal_t;

typedef struct robo_i18n_number_options {
    int32_t min_integer_digits;   /* default 1; clamped 1..40 */
    int32_t min_fraction_digits;  /* default 0 */
    int32_t max_fraction_digits;  /* default -1 = use CLDR pattern max (often 3) */
    int use_grouping;             /* default 1 */
    int show_plus;                /* default 0 */
    robo_i18n_rounding_t rounding;/* default HALF_EVEN */
    char numbering_system[16];    /* empty = locale default; else explicit id */
} robo_i18n_number_options_t;

/**
 * Reusable formatter. Stack-allocatable. No heap of its own.
 * Resolved symbol pointers refer to immortal generated CLDR string pool.
 */
typedef struct robo_i18n_number_formatter {
    robo_i18n_locale_t locale;
    robo_i18n_number_options_t options;
    char numbering_system[16];
    const char *decimal_sep;
    const char *group_sep;
    const char *plus_sign;
    const char *minus_sign;
    const char *nan_symbol;
    const char *infinity_symbol;
    const char *digit_glyphs; /* UTF-8 concat of 10 digits; empty if unavailable */
    uint8_t min_grouping;
    uint8_t primary_group;
    uint8_t secondary_group;
    uint8_t pattern_max_fraction;
    uint8_t ready;
} robo_i18n_number_formatter_t;

void robo_i18n_number_options_init(robo_i18n_number_options_t *opts);

robo_result_t robo_i18n_number_formatter_init(robo_i18n_number_formatter_t *fmt,
                                              const robo_i18n_locale_t *locale,
                                              const robo_i18n_number_options_t *opts);

/** Clears the formatter; no heap to free. Safe on NULL. */
void robo_i18n_number_formatter_destroy(robo_i18n_number_formatter_t *fmt);

robo_result_t robo_i18n_decimal_from_i64(int64_t value, robo_i18n_decimal_t *out);
robo_result_t robo_i18n_decimal_from_u64(uint64_t value, robo_i18n_decimal_t *out);

/**
 * Convert IEEE-754 binary64 into a decimal for formatting.
 * Specials → NAN / ±INFINITY kinds.
 * Finite conversion is best-effort for |value| < 1e18 with up to 15 fraction
 * digits considered before options rounding; larger magnitudes may return
 * ROBO_NOT_SUPPORTED. Negative zero is preserved.
 */
robo_result_t robo_i18n_decimal_from_double(double value, robo_i18n_decimal_t *out);

/**
 * Format into caller buffer (UTF-8, always NUL-terminated on success when
 * buf_cap > 0). If buf is NULL or buf_cap == 0, only computes *out_len.
 * Returns ROBO_ERROR when buf_cap is too small (out_len still set).
 */
robo_result_t robo_i18n_number_format(const robo_i18n_number_formatter_t *fmt,
                                      const robo_i18n_decimal_t *value,
                                      char *buf,
                                      size_t buf_cap,
                                      size_t *out_len);

robo_result_t robo_i18n_number_format_i64(const robo_i18n_number_formatter_t *fmt,
                                          int64_t value,
                                          char *buf,
                                          size_t buf_cap,
                                          size_t *out_len);

robo_result_t robo_i18n_number_format_u64(const robo_i18n_number_formatter_t *fmt,
                                          uint64_t value,
                                          char *buf,
                                          size_t buf_cap,
                                          size_t *out_len);

robo_result_t robo_i18n_number_format_double(const robo_i18n_number_formatter_t *fmt,
                                             double value,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len);

/** Append formatted number onto an existing RoboString. */
robo_result_t robo_i18n_number_format_to_string(const robo_i18n_number_formatter_t *fmt,
                                                const robo_i18n_decimal_t *value,
                                                robo_string_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_NUMBER_H */
