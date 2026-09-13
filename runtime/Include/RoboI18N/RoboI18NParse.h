/*
 * RoboI18N number & currency parsing (Goal 4.22).
 *
 * Formatting (Goals 4.3 / 4.21) and parsing are separate APIs.
 * Validation is a by-product of parse modes; FX conversion is out of scope.
 *
 * Modes:
 *   STRICT     — reject malformed grouping, unexpected chars, trailing junk,
 *                and ambiguous currency symbols without an explicit constraint.
 *   PERMISSIVE — accept missing grouping and irregular group sizes; may resolve
 *                ambiguous symbols to the locale default currency when that
 *                default is among the matches. Still deterministic.
 *
 * Numeric payload is robo_i18n_decimal_t (ASCII digits + scale) — not double —
 * so "1" / "1.0" / "1.00" remain distinguishable via scale.
 *
 * Error positions are UTF-8 byte offsets into the input span.
 *
 * Thread safety: parsers are not synchronized — one owner thread, or lock.
 * No global mutable parse state. CLDR/display tables are immortal/read-only.
 */

#ifndef ROBO_I18N_PARSE_H
#define ROBO_I18N_PARSE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NCurrency.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NNumber.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hard limit on input span length (bytes). Longer → ROBO_INVALID_ARGUMENT. */
#define ROBO_I18N_PARSE_INPUT_MAX 4096u

/** Max ambiguous currency codes reported in a result. */
#define ROBO_I18N_PARSE_AMBIG_MAX 8u

typedef enum robo_i18n_parse_mode {
    ROBO_I18N_PARSE_STRICT = 0,
    ROBO_I18N_PARSE_PERMISSIVE = 1
} robo_i18n_parse_mode_t;

/**
 * Detailed parse failure category (orthogonal to robo_result_t).
 * On success, category is ROBO_I18N_PARSE_ERR_NONE.
 */
typedef enum robo_i18n_parse_err {
    ROBO_I18N_PARSE_ERR_NONE = 0,
    ROBO_I18N_PARSE_ERR_EMPTY = 1,
    ROBO_I18N_PARSE_ERR_INVALID_UTF8 = 2,
    ROBO_I18N_PARSE_ERR_INVALID_DIGIT = 3,
    ROBO_I18N_PARSE_ERR_INVALID_GROUPING = 4,
    ROBO_I18N_PARSE_ERR_INVALID_DECIMAL = 5,
    ROBO_I18N_PARSE_ERR_INVALID_SIGN = 6,
    ROBO_I18N_PARSE_ERR_INVALID_CURRENCY = 7,
    ROBO_I18N_PARSE_ERR_AMBIGUOUS_CURRENCY = 8,
    ROBO_I18N_PARSE_ERR_UNEXPECTED_CHAR = 9,
    ROBO_I18N_PARSE_ERR_TRAILING = 10,
    ROBO_I18N_PARSE_ERR_OVERFLOW = 11,
    ROBO_I18N_PARSE_ERR_UNSUPPORTED_LOCALE = 12,
    ROBO_I18N_PARSE_ERR_INPUT_TOO_LONG = 13
} robo_i18n_parse_err_t;

typedef struct robo_i18n_parse_options {
    robo_i18n_parse_mode_t mode;
    int allow_leading_ws;  /* default 1 */
    int allow_trailing_ws; /* default 1 */
    int require_full;      /* default 1: reject non-ws trailing input */
    int integer_only;      /* default 0: reject decimal separator */
    char numbering_system[16]; /* empty = locale default */
} robo_i18n_parse_options_t;

/**
 * Structured parse outcome. Always filled on API return (success or failure).
 * currency[0]==0 when no currency was present / resolved.
 * consumed = bytes successfully consumed (may be < len on partial permissive).
 * error_offset = byte index of failure (or consumed on success).
 */
typedef struct robo_i18n_parse_result {
    robo_i18n_decimal_t value;
    char currency[4];
    size_t consumed;
    size_t error_offset;
    robo_i18n_parse_err_t error;
    uint8_t currency_ambiguous;
    uint8_t ambiguous_count;
    char ambiguous[ROBO_I18N_PARSE_AMBIG_MAX][4];
} robo_i18n_parse_result_t;

typedef struct robo_i18n_number_parser {
    robo_i18n_locale_t locale;
    robo_i18n_parse_options_t options;
    robo_i18n_number_formatter_t symbols; /* resolved seps / digits / signs */
    const char *digit_utf8[10];
    uint8_t digit_len[10];
    uint8_t ready;
} robo_i18n_number_parser_t;

typedef struct robo_i18n_currency_parse_options {
    robo_i18n_parse_options_t number;
    char require_currency[4]; /* empty = not required */
    /** Optional NUL-terminated list of accepted ISO codes (max 16). */
    char accepted[16][4];
    uint8_t accepted_count; /* 0 = any known / resolved */
    int allow_currency_names; /* default 1 */
} robo_i18n_currency_parse_options_t;

typedef struct robo_i18n_currency_parser {
    robo_i18n_number_parser_t number;
    robo_i18n_currency_parse_options_t options;
    char locale_default[4];
    uint8_t ready;
} robo_i18n_currency_parser_t;

void robo_i18n_parse_options_init(robo_i18n_parse_options_t *opts);
void robo_i18n_parse_result_clear(robo_i18n_parse_result_t *out);
void robo_i18n_currency_parse_options_init(robo_i18n_currency_parse_options_t *opts);

robo_result_t robo_i18n_number_parser_init(robo_i18n_number_parser_t *parser,
                                           const robo_i18n_locale_t *locale,
                                           const robo_i18n_parse_options_t *opts);

void robo_i18n_number_parser_destroy(robo_i18n_number_parser_t *parser);

/**
 * Parse a localized number from UTF-8 @p text[0..len).
 * Returns ROBO_OK on success; ROBO_PARSE_ERROR on grammar/content failure
 * (details in @p out); ROBO_INVALID_ARGUMENT for NULL/oversized input.
 */
robo_result_t robo_i18n_parse_number(const robo_i18n_number_parser_t *parser,
                                     const char *text,
                                     size_t len,
                                     robo_i18n_parse_result_t *out);

robo_result_t robo_i18n_parse_integer(const robo_i18n_number_parser_t *parser,
                                      const char *text,
                                      size_t len,
                                      robo_i18n_parse_result_t *out);

robo_result_t robo_i18n_currency_parser_init(
    robo_i18n_currency_parser_t *parser,
    const robo_i18n_locale_t *locale,
    const robo_i18n_currency_parse_options_t *opts);

void robo_i18n_currency_parser_destroy(robo_i18n_currency_parser_t *parser);

robo_result_t robo_i18n_parse_currency(const robo_i18n_currency_parser_t *parser,
                                       const char *text,
                                       size_t len,
                                       robo_i18n_parse_result_t *out);

/** Human-readable name for a parse error category (static string). */
const char *robo_i18n_parse_err_string(robo_i18n_parse_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_PARSE_H */
