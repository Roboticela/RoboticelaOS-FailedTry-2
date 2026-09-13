/*
 * RoboI18N locale display names (Goal 4.10).
 *
 * Distinguishes:
 *   identifier being described  (e.g. "ur", "Latn", "PK", "Asia/Karachi")
 *   vs display locale           (locale that provides the human-readable name)
 *
 * Data: CLDR 46 (RoboDispData). Fallback chain:
 *   display locale → CLDR parentLocales / truncation parents → "en" →
 *   stable identifier (if FALLBACK_CODE) or ROBO_NOT_FOUND.
 *
 * Never invents translations. Output is logical UTF-8 (no bidi reordering).
 *
 * Ownership: write into caller buffers. Tables are immutable process data.
 * Thread safety: pure lookups; no global mutable state.
 */

#ifndef ROBO_I18N_DISPLAY_NAMES_H
#define ROBO_I18N_DISPLAY_NAMES_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NCalendar.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NTimeZone.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *robo_i18n_display_cldr_version_string(void);

typedef enum robo_i18n_display_fallback {
    /** Missing name → ROBO_NOT_FOUND; out buffer untouched / empty. */
    ROBO_I18N_DISP_FALLBACK_NOT_FOUND = 0,
    /** Missing name → copy canonical identifier; return ROBO_OK. */
    ROBO_I18N_DISP_FALLBACK_CODE = 1
} robo_i18n_display_fallback_t;

typedef enum robo_i18n_currency_name_style {
    ROBO_I18N_CURRENCY_NAME = 0,
    ROBO_I18N_CURRENCY_SYMBOL = 1,
    ROBO_I18N_CURRENCY_NARROW_SYMBOL = 2
} robo_i18n_currency_name_style_t;

typedef enum robo_i18n_timezone_name_style {
    ROBO_I18N_TZ_DISP_EXEMPLAR_CITY = 0,
    ROBO_I18N_TZ_DISP_LONG_GENERIC = 1,
    ROBO_I18N_TZ_DISP_LONG_STANDARD = 2,
    ROBO_I18N_TZ_DISP_LONG_DAYLIGHT = 3,
    ROBO_I18N_TZ_DISP_GMT = 4 /**< Formatted via CLDR gmtFormat + offset; needs FIXED/UTC. */
} robo_i18n_timezone_name_style_t;

/**
 * Options for display-name lookups.
 * fallback defaults to FALLBACK_CODE.
 * dialect: reserved (CLDR -alt-menu / dialect); ignored in Goal 4.10 → standard names.
 */
typedef struct robo_i18n_display_options {
    robo_i18n_display_fallback_t fallback;
    uint8_t dialect; /* 0 = standard (only implemented) */
} robo_i18n_display_options_t;

void robo_i18n_display_options_init(robo_i18n_display_options_t *out);

/**
 * All APIs:
 *   @p display_locale — locale used to look up the name (may be NULL → "en")
 *   @p options        — NULL → defaults
 *   @p out / @p out_cap / @p out_len — UTF-8 buffer; *out_len excludes NUL;
 *     always NUL-terminates when out_cap > 0 and ROBO_OK.
 *   ROBO_ERROR if buffer too small (sets *out_len to needed size including NUL).
 */

robo_result_t robo_i18n_language_display_name(const char *language_id,
                                              size_t language_len,
                                              const robo_i18n_locale_t *display_locale,
                                              const robo_i18n_display_options_t *options,
                                              char *out,
                                              size_t out_cap,
                                              size_t *out_len);

robo_result_t robo_i18n_script_display_name(const char *script_id,
                                            size_t script_len,
                                            const robo_i18n_locale_t *display_locale,
                                            const robo_i18n_display_options_t *options,
                                            char *out,
                                            size_t out_cap,
                                            size_t *out_len);

robo_result_t robo_i18n_region_display_name(const char *region_id,
                                            size_t region_len,
                                            const robo_i18n_locale_t *display_locale,
                                            const robo_i18n_display_options_t *options,
                                            char *out,
                                            size_t out_cap,
                                            size_t *out_len);

robo_result_t robo_i18n_currency_display_name(const char *currency_code,
                                              size_t currency_len,
                                              robo_i18n_currency_name_style_t style,
                                              const robo_i18n_locale_t *display_locale,
                                              const robo_i18n_display_options_t *options,
                                              char *out,
                                              size_t out_cap,
                                              size_t *out_len);

robo_result_t robo_i18n_calendar_display_name(const robo_i18n_calendar_t *calendar,
                                              const robo_i18n_locale_t *display_locale,
                                              const robo_i18n_display_options_t *options,
                                              char *out,
                                              size_t out_cap,
                                              size_t *out_len);

robo_result_t robo_i18n_numbering_system_display_name(
    const char *numbering_id,
    size_t numbering_len,
    const robo_i18n_locale_t *display_locale,
    const robo_i18n_display_options_t *options,
    char *out,
    size_t out_cap,
    size_t *out_len);

/**
 * Time-zone display names from CLDR. Exemplar city / metazone long names when
 * present. GMT style formats offset for FIXED/UTC zones; IANA without tzdb
 * → ROBO_NOT_SUPPORTED for GMT style.
 */
robo_result_t robo_i18n_timezone_display_name(const robo_i18n_timezone_t *tz,
                                              robo_i18n_timezone_name_style_t style,
                                              const robo_i18n_locale_t *display_locale,
                                              const robo_i18n_display_options_t *options,
                                              char *out,
                                              size_t out_cap,
                                              size_t *out_len);

/**
 * Compose a locale display name using CLDR localePattern / localeSeparator.
 * Uses language + optional script + region from @p described.
 */
robo_result_t robo_i18n_locale_display_name(const robo_i18n_locale_t *described,
                                            const robo_i18n_locale_t *display_locale,
                                            const robo_i18n_display_options_t *options,
                                            char *out,
                                            size_t out_cap,
                                            size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_DISPLAY_NAMES_H */
