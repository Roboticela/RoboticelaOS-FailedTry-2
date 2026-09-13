/*
 * RoboI18N locale — BCP 47 language tags (RFC 5646).
 *
 * Distinctions:
 *   syntactic validity  — grammar of subtags
 *   canonical form      — casing + IANA Preferred-Value replacements
 *   preferred locale    — CLDR likely subtags (see RoboI18NLocaleData.h)
 *
 * Locale objects are value types (no heap). Locale lists allocate via RoboMem.
 */

#ifndef ROBO_I18N_LOCALE_H
#define ROBO_I18N_LOCALE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NVersion.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_I18N_LANG_MAX 8u
#define ROBO_I18N_SCRIPT_MAX 4u
#define ROBO_I18N_REGION_MAX 3u
#define ROBO_I18N_VARIANT_MAX 8u
#define ROBO_I18N_VARIANT_COUNT 8u
#define ROBO_I18N_EXT_MAX 96u
#define ROBO_I18N_TAG_MAX 128u

typedef struct robo_i18n_locale {
    char language[ROBO_I18N_LANG_MAX + 1u];
    char script[ROBO_I18N_SCRIPT_MAX + 1u];   /* empty if absent */
    char region[ROBO_I18N_REGION_MAX + 1u];   /* empty if absent */
    char variants[ROBO_I18N_VARIANT_COUNT][ROBO_I18N_VARIANT_MAX + 1u];
    uint8_t variant_count;
    char extensions[ROBO_I18N_EXT_MAX + 1u];  /* e.g. "u-ca-gregory" (no leading '-') */
    char privateuse[ROBO_I18N_EXT_MAX + 1u];  /* e.g. "x-private" without requiring 'x-' prefix stored as "x-..." */
} robo_i18n_locale_t;

typedef enum robo_i18n_match {
    ROBO_I18N_MATCH_NONE = 0,
    ROBO_I18N_MATCH_LANGUAGE = 1,
    ROBO_I18N_MATCH_SCRIPT = 2,
    ROBO_I18N_MATCH_REGION = 3,
    ROBO_I18N_MATCH_EXACT = 4
} robo_i18n_match_t;

/** Clear to empty / invalid language. */
void robo_i18n_locale_clear(robo_i18n_locale_t *loc);

/**
 * Parse a BCP 47 language tag (not null-terminated required).
 * On success, @p out holds syntactically validated + cased fields.
 * Does not apply IANA Preferred-Value (use canonicalize).
 * Grandfathered/redundant full-tag Preferred-Value is applied during parse
 * when the entire input matches a registry Tag (authoritative replacement).
 *
 * Unknown-but-structurally-valid subtags are accepted.
 * Malformed syntax → ROBO_PARSE_ERROR.
 */
robo_result_t robo_i18n_locale_parse(const char *tag,
                                     size_t len,
                                     robo_i18n_locale_t *out);

/** Parse then apply IANA Preferred-Value replacements on language/script/region/variant. */
robo_result_t robo_i18n_locale_parse_canonical(const char *tag,
                                               size_t len,
                                               robo_i18n_locale_t *out);

/** Apply Preferred-Value maps in place (casing already expected). */
robo_result_t robo_i18n_locale_canonicalize(robo_i18n_locale_t *loc);

/** Format canonical tag into @p buf (NUL-terminated). */
robo_result_t robo_i18n_locale_format(const robo_i18n_locale_t *loc,
                                      char *buf,
                                      size_t buf_cap,
                                      size_t *out_len);

int robo_i18n_locale_equal(const robo_i18n_locale_t *a, const robo_i18n_locale_t *b);

/**
 * How specifically @p candidate matches @p range (range may omit script/region).
 * Matching is case-insensitive on subtags after normalize via equal fields.
 */
robo_i18n_match_t robo_i18n_locale_matches(const robo_i18n_locale_t *range,
                                           const robo_i18n_locale_t *candidate);

/**
 * Truncation parent (resource-fallback style), not CLDR likely-subtags.
 * Order: privateuse → extensions → last variant → region → script → (none).
 * Returns ROBO_NOT_FOUND when only primary language remains (no parent).
 */
robo_result_t robo_i18n_locale_parent(const robo_i18n_locale_t *loc,
                                      robo_i18n_locale_t *out);

int robo_i18n_locale_has_script(const robo_i18n_locale_t *loc);
int robo_i18n_locale_has_region(const robo_i18n_locale_t *loc);

const char *robo_i18n_locale_language(const robo_i18n_locale_t *loc);
const char *robo_i18n_locale_script(const robo_i18n_locale_t *loc);
const char *robo_i18n_locale_region(const robo_i18n_locale_t *loc);

/* --- Default locale (process-wide; caller synchronizes until RoboThread) --- */

robo_result_t robo_i18n_set_default_locale(const robo_i18n_locale_t *loc);
robo_result_t robo_i18n_get_default_locale(robo_i18n_locale_t *out);

/* --- Locale preference lists (heap) --- */

typedef struct robo_i18n_locale_list robo_i18n_locale_list_t;

/**
 * Parse comma-separated tags: "en-US, en, ur-PK".
 * Optional ";q=0.8" weights are accepted and used for stable sort (desc).
 * Empty / whitespace-only entries skipped.
 */
robo_result_t robo_i18n_locale_list_parse(const char *text,
                                          size_t len,
                                          robo_i18n_locale_list_t **out);

void robo_i18n_locale_list_destroy(robo_i18n_locale_list_t *list);

size_t robo_i18n_locale_list_size(const robo_i18n_locale_list_t *list);

robo_result_t robo_i18n_locale_list_get(const robo_i18n_locale_list_t *list,
                                        size_t index,
                                        robo_i18n_locale_t *out);

/**
 * Optional weight from list parse (millipercent, 0..1000). Default 1000 if
 * the list was not created with q-values. Goal 4.17.
 */
robo_result_t robo_i18n_locale_list_get_q_milli(const robo_i18n_locale_list_t *list,
                                                size_t index,
                                                uint16_t *out_q_milli);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_LOCALE_H */
