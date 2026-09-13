/*
 * RoboI18N date/time parsing (Goal 4.23).
 *
 * Formatting (Goal 4.6) and parsing are separate APIs.
 * Calendar conversion and timezone resolution are distinct steps after parse.
 *
 * Concepts (do not conflate):
 *   calendar date / wall time / local date-time / instant /
 *   UTC offset / named timezone / calendar system
 *
 * Missing timezone: returns a local date-time. Never invents UTC or the
 * host machine zone. Instant conversion requires an explicit offset/zone in
 * the input, or a resolve timezone supplied on the parser plus
 * options.resolve_instant.
 *
 * Modes:
 *   STRICT     — exact pattern literals, field ranges, weekday consistency,
 *                reject ambiguous/nonexistent local times when resolve policy
 *                is REJECT (default).
 *   PERMISSIVE — flexible whitespace around literals; numeric width may vary;
 *                weekday mismatch ignored; still deterministic.
 *
 * Disambiguation (local → instant): REJECT / EARLIER / LATER / COMPATIBLE.
 * COMPATIBLE maps to EARLIER for ambiguous folds; nonexistent gaps still fail
 * (same as the timezone engine — no silent gap-fill).
 *
 * Error positions are UTF-8 byte offsets into the input span.
 *
 * Thread safety: parsers are not synchronized — one owner thread, or lock.
 * No global mutable parse state. Pattern tokens are cached inside the parser.
 */

#ifndef ROBO_I18N_DATETIME_PARSE_H
#define ROBO_I18N_DATETIME_PARSE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NCalendar.h"
#include "RoboI18N/RoboI18NDateTime.h"
#include "RoboI18N/RoboI18NDateTimeFormat.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NParse.h"
#include "RoboI18N/RoboI18NTimeZone.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hard limit on input span length (bytes). */
#define ROBO_I18N_DTPARSE_INPUT_MAX 4096u

typedef enum robo_i18n_dtparse_err {
    ROBO_I18N_DTPARSE_ERR_NONE = 0,
    ROBO_I18N_DTPARSE_ERR_EMPTY = 1,
    ROBO_I18N_DTPARSE_ERR_INVALID_UTF8 = 2,
    ROBO_I18N_DTPARSE_ERR_UNEXPECTED_CHAR = 3,
    ROBO_I18N_DTPARSE_ERR_TRAILING = 4,
    ROBO_I18N_DTPARSE_ERR_INVALID_DATE = 5,
    ROBO_I18N_DTPARSE_ERR_INVALID_TIME = 6,
    ROBO_I18N_DTPARSE_ERR_INVALID_FIELD = 7,
    ROBO_I18N_DTPARSE_ERR_INVALID_MONTH_NAME = 8,
    ROBO_I18N_DTPARSE_ERR_INVALID_WEEKDAY_NAME = 9,
    ROBO_I18N_DTPARSE_ERR_INVALID_ERA = 10,
    ROBO_I18N_DTPARSE_ERR_INVALID_DAY_PERIOD = 11,
    ROBO_I18N_DTPARSE_ERR_INVALID_TIMEZONE = 12,
    ROBO_I18N_DTPARSE_ERR_AMBIGUOUS_TIMEZONE = 13,
    ROBO_I18N_DTPARSE_ERR_AMBIGUOUS_LOCAL = 14,
    ROBO_I18N_DTPARSE_ERR_NONEXISTENT_LOCAL = 15,
    ROBO_I18N_DTPARSE_ERR_MISSING_TIMEZONE = 16,
    ROBO_I18N_DTPARSE_ERR_OVERFLOW = 17,
    ROBO_I18N_DTPARSE_ERR_UNSUPPORTED_CALENDAR = 18,
    ROBO_I18N_DTPARSE_ERR_UNSUPPORTED_LOCALE = 19,
    ROBO_I18N_DTPARSE_ERR_INPUT_TOO_LONG = 20,
    ROBO_I18N_DTPARSE_ERR_PATTERN = 21
} robo_i18n_dtparse_err_t;

/**
 * Local→instant policy for the parser (extends timezone disambiguation).
 * COMPATIBLE → EARLIER on ambiguous; nonexistent still fails.
 */
typedef enum robo_i18n_dtparse_disambig {
    ROBO_I18N_DTPARSE_DISAMBIG_REJECT = 0,
    ROBO_I18N_DTPARSE_DISAMBIG_EARLIER = 1,
    ROBO_I18N_DTPARSE_DISAMBIG_LATER = 2,
    ROBO_I18N_DTPARSE_DISAMBIG_COMPATIBLE = 3
} robo_i18n_dtparse_disambig_t;

typedef struct robo_i18n_dtparse_options {
    robo_i18n_parse_mode_t mode;
    robo_i18n_cal_format_style_t date_style; /* style init: DATE / DATETIME */
    robo_i18n_cal_format_style_t time_style; /* style init: TIME / DATETIME */
    int allow_leading_ws;  /* default 1 */
    int allow_trailing_ws; /* default 1 */
    int require_full;      /* default 1 */
    int resolve_instant;   /* default 0: do not invent instants */
    robo_i18n_dtparse_disambig_t disambiguation; /* default REJECT */
    char numbering_system[16];
    char hour_cycle[8]; /* empty = locale service */
} robo_i18n_dtparse_options_t;

/**
 * Structured parse outcome. Fields are only meaningful when the corresponding
 * have_* flag is set. gregorian_date is filled when calendar conversion succeeds.
 * Instant is filled only when resolve succeeds (have_instant).
 */
typedef struct robo_i18n_dtparse_result {
    uint8_t have_date;
    uint8_t have_time;
    uint8_t have_offset;
    uint8_t have_timezone;
    uint8_t zone_explicit; /* 1 if offset/zone came from the input */
    uint8_t have_instant;
    uint8_t have_era;
    uint8_t have_day_period;
    uint8_t day_period_is_pm; /* meaningful if have_day_period */

    robo_i18n_calendar_fields_t fields;
    robo_i18n_date_t gregorian_date;
    robo_i18n_time_t time;
    robo_i18n_datetime_t local;
    int32_t offset_seconds;
    char timezone_id[ROBO_I18N_TZ_ID_MAX + 1u];
    robo_i18n_instant_t instant;
    robo_i18n_tz_resolve_t tz_resolve;

    size_t consumed;
    size_t error_offset;
    robo_i18n_dtparse_err_t error;
} robo_i18n_dtparse_result_t;

/**
 * Stack parser. Embeds a style/pattern formatter for compiled tokens and
 * numbering-system digits. Ownership: value type; destroy clears ready.
 */
typedef struct robo_i18n_datetime_parser {
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_dtparse_options_t options;
    robo_i18n_dtfmt_kind_t kind;
    const char *digit_utf8[10];
    uint8_t digit_len[10];
    uint8_t has_resolve_tz;
    uint8_t ready;
} robo_i18n_datetime_parser_t;

void robo_i18n_dtparse_options_init(robo_i18n_dtparse_options_t *opts);
void robo_i18n_dtparse_result_clear(robo_i18n_dtparse_result_t *out);

/**
 * Style-based parser. @p calendar NULL → gregorian. @p resolve_tz NULL →
 * no default zone for instant resolution (input zone/offset still honored).
 */
robo_result_t robo_i18n_datetime_parser_init(robo_i18n_datetime_parser_t *parser,
                                             const robo_i18n_locale_t *locale,
                                             const robo_i18n_calendar_t *calendar,
                                             const robo_i18n_timezone_t *resolve_tz,
                                             robo_i18n_dtfmt_kind_t kind,
                                             const robo_i18n_dtparse_options_t *opts);

robo_result_t robo_i18n_datetime_parser_init_pattern(
    robo_i18n_datetime_parser_t *parser,
    const robo_i18n_locale_t *locale,
    const robo_i18n_calendar_t *calendar,
    const robo_i18n_timezone_t *resolve_tz,
    const char *pattern,
    const robo_i18n_dtparse_options_t *opts);

robo_result_t robo_i18n_datetime_parser_init_skeleton(
    robo_i18n_datetime_parser_t *parser,
    const robo_i18n_locale_t *locale,
    const robo_i18n_calendar_t *calendar,
    const robo_i18n_timezone_t *resolve_tz,
    const char *skeleton,
    const robo_i18n_dtparse_options_t *opts);

void robo_i18n_datetime_parser_destroy(robo_i18n_datetime_parser_t *parser);

/**
 * Parse UTF-8 @p text[0..len) against the parser's pattern.
 * Returns ROBO_OK on success; ROBO_PARSE_ERROR on grammar/content failure
 * (details in @p out); ROBO_INVALID_ARGUMENT for NULL/oversized input;
 * ROBO_NOT_SUPPORTED for calendars without conversion when date fields present.
 */
robo_result_t robo_i18n_parse_date(const robo_i18n_datetime_parser_t *parser,
                                   const char *text,
                                   size_t len,
                                   robo_i18n_dtparse_result_t *out);

robo_result_t robo_i18n_parse_time(const robo_i18n_datetime_parser_t *parser,
                                   const char *text,
                                   size_t len,
                                   robo_i18n_dtparse_result_t *out);

robo_result_t robo_i18n_parse_local_datetime(const robo_i18n_datetime_parser_t *parser,
                                             const char *text,
                                             size_t len,
                                             robo_i18n_dtparse_result_t *out);

/**
 * Same as local datetime parse, but requires a timezone or offset in the
 * input (or resolve_instant + resolve_tz) so an instant can be produced when
 * options.resolve_instant is set. Without resolve_instant, still requires
 * zone/offset markers in the matched pattern payload.
 */
robo_result_t robo_i18n_parse_zoned_datetime(const robo_i18n_datetime_parser_t *parser,
                                             const char *text,
                                             size_t len,
                                             robo_i18n_dtparse_result_t *out);

const char *robo_i18n_dtparse_err_string(robo_i18n_dtparse_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_DATETIME_PARSE_H */
