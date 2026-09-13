/*
 * RoboI18N runtime locale service (Goal 4.16).
 *
 * Centralizes Unicode extension keywords, resolved settings (calendar,
 * numbering, hour cycle, collation type, timezone), fallback chains,
 * negotiation, and capability queries so formatters share one model.
 *
 * Language: C. Locale settings are value types (no heap cache).
 * Process application locale is process-wide (caller synchronizes).
 * Generated CLDR/tzdb tables remain immutable.
 *
 * Override precedence (documented):
 *   formatter/application explicit override
 *     > Unicode u-extension on the locale
 *     > CLDR locale/region default
 *     > runtime root fallback (gregorian / latn / h23 / UTC)
 */

#ifndef ROBO_I18N_LOCALE_SERVICE_H
#define ROBO_I18N_LOCALE_SERVICE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboI18NTimeZone.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_I18N_UKEY_MAX 8u
#define ROBO_I18N_UTYPE_MAX 32u
#define ROBO_I18N_UKEY_COUNT 16u
#define ROBO_I18N_FALLBACK_MAX 16u

/** Where a resolved setting came from. */
typedef enum robo_i18n_setting_source {
    ROBO_I18N_SRC_UNSET = 0,
    ROBO_I18N_SRC_OVERRIDE = 1,
    ROBO_I18N_SRC_EXTENSION = 2,
    ROBO_I18N_SRC_LOCALE_DEFAULT = 3,
    ROBO_I18N_SRC_RUNTIME_FALLBACK = 4
} robo_i18n_setting_source_t;

typedef enum robo_i18n_hour_cycle {
    ROBO_I18N_HC_UNKNOWN = 0,
    ROBO_I18N_HC_H11 = 1,
    ROBO_I18N_HC_H12 = 2,
    ROBO_I18N_HC_H23 = 3,
    ROBO_I18N_HC_H24 = 4
} robo_i18n_hour_cycle_t;

/**
 * Parsed Unicode locale keywords (known keys + unknown preserved as opaque
 * remainder when re-serializing). Empty strings mean absent.
 */
typedef struct robo_i18n_unicode_keywords {
    char calendar[ROBO_I18N_UTYPE_MAX + 1u];       /* ca */
    char numbering_system[ROBO_I18N_UTYPE_MAX + 1u]; /* nu */
    char hour_cycle[ROBO_I18N_UTYPE_MAX + 1u];     /* hc: h11/h12/h23/h24 */
    char collation[ROBO_I18N_UTYPE_MAX + 1u];      /* co */
    char timezone[ROBO_I18N_TZ_ID_MAX + 1u];       /* tz (BCP47 / IANA short form) */
    /** Other u- keys as "key-type-key-type…" (no leading u-); may be empty. */
    char other[ROBO_I18N_EXT_MAX + 1u];
} robo_i18n_unicode_keywords_t;

/**
 * Fully resolved runtime locale settings (RoboLocale context).
 * Identity remains in @p locale; settings never conflate locale with timezone.
 */
typedef struct robo_i18n_locale_settings {
    robo_i18n_locale_t locale;
    char calendar[32];
    char numbering_system[16];
    char hour_cycle[8];
    char collation_type[16];
    char timezone_id[ROBO_I18N_TZ_ID_MAX + 1u]; /* empty = unset */
    robo_i18n_setting_source_t calendar_src;
    robo_i18n_setting_source_t numbering_src;
    robo_i18n_setting_source_t hour_cycle_src;
    robo_i18n_setting_source_t collation_src;
    robo_i18n_setting_source_t timezone_src;
} robo_i18n_locale_settings_t;

/**
 * Optional overrides when resolving settings.
 * Empty / NULL fields mean "no override".
 */
typedef struct robo_i18n_locale_overrides {
    const char *calendar;          /* CLDR calendar id */
    const char *numbering_system;  /* e.g. arabext */
    const char *hour_cycle;        /* h11/h12/h23/h24 */
    const char *collation_type;    /* standard / phonebook / … */
    const char *timezone_id;       /* IANA id or fixed/UTC label */
} robo_i18n_locale_overrides_t;

void robo_i18n_locale_overrides_init(robo_i18n_locale_overrides_t *out);
void robo_i18n_unicode_keywords_clear(robo_i18n_unicode_keywords_t *out);
void robo_i18n_locale_settings_clear(robo_i18n_locale_settings_t *out);

/** Parse Unicode keywords from @p loc->extensions (u-… blob). */
robo_result_t robo_i18n_locale_unicode_keywords(const robo_i18n_locale_t *loc,
                                                robo_i18n_unicode_keywords_t *out);

/**
 * Apply keywords back onto @p loc->extensions (rebuilds u- extension).
 * Preserves non-u extensions already in loc if present after first singleton
 * is not 'u' — when loc has only u-, replaces it.
 */
robo_result_t robo_i18n_locale_set_unicode_keywords(robo_i18n_locale_t *loc,
                                                    const robo_i18n_unicode_keywords_t *kw);

/** Get a single known keyword value (empty string if absent). */
robo_result_t robo_i18n_locale_get_keyword(const robo_i18n_locale_t *loc,
                                           const char *key,
                                           char *out,
                                           size_t out_cap);

robo_result_t robo_i18n_locale_set_keyword(robo_i18n_locale_t *loc,
                                           const char *key,
                                           const char *type);

/**
 * Resolve effective settings for @p locale with optional overrides.
 * Does not mutate @p locale.
 */
robo_result_t robo_i18n_locale_resolve(const robo_i18n_locale_t *locale,
                                       const robo_i18n_locale_overrides_t *overrides,
                                       robo_i18n_locale_settings_t *out);

/** Convenience resolvers (same precedence as resolve). */
robo_result_t robo_i18n_locale_resolve_numbering(const robo_i18n_locale_t *locale,
                                                 const char *override_id,
                                                 char *out_id,
                                                 size_t out_cap);

robo_result_t robo_i18n_locale_resolve_calendar(const robo_i18n_locale_t *locale,
                                                const char *override_id,
                                                char *out_id,
                                                size_t out_cap);

robo_result_t robo_i18n_locale_resolve_hour_cycle(const robo_i18n_locale_t *locale,
                                                  const char *override_id,
                                                  char *out_id,
                                                  size_t out_cap);

robo_result_t robo_i18n_locale_resolve_collation(const robo_i18n_locale_t *locale,
                                                 const char *override_id,
                                                 char *out_id,
                                                 size_t out_cap);

robo_result_t robo_i18n_locale_resolve_timezone(const robo_i18n_locale_t *locale,
                                                const char *override_id,
                                                char *out_id,
                                                size_t out_cap);

robo_i18n_hour_cycle_t robo_i18n_hour_cycle_from_id(const char *id);
const char *robo_i18n_hour_cycle_id(robo_i18n_hour_cycle_t hc);

/**
 * CLDR data-inheritance fallback chain into @p out[0..*out_count).
 * Starts with @p loc; ends before inventing beyond root. Caps at @p cap.
 * Never infinite: max ROBO_I18N_FALLBACK_MAX steps.
 */
robo_result_t robo_i18n_locale_fallback_chain(const robo_i18n_locale_t *loc,
                                              robo_i18n_locale_t *out,
                                              size_t cap,
                                              size_t *out_count);

/**
 * Negotiate best match: walk requested preferences; for each, find best
 * available locale by robo_i18n_locale_matches ranking (EXACT > REGION > …).
 * Empty available → ROBO_INVALID_ARGUMENT. No match → ROBO_NOT_FOUND.
 */
robo_result_t robo_i18n_locale_negotiate(const robo_i18n_locale_list_t *requested,
                                         const robo_i18n_locale_t *available,
                                         size_t available_count,
                                         robo_i18n_locale_t *out_best,
                                         robo_i18n_match_t *out_match);

/* --- Application locale (process-wide; distinct from default / platform) --- */

robo_result_t robo_i18n_set_application_locale(const robo_i18n_locale_t *loc);
robo_result_t robo_i18n_get_application_locale(robo_i18n_locale_t *out);
/** Clear application locale so get falls through to default locale. */
void robo_i18n_clear_application_locale(void);

/**
 * Effective locale for the runtime: application if set, else default locale.
 */
robo_result_t robo_i18n_get_effective_locale(robo_i18n_locale_t *out);

/* --- Capability queries (stable; no raw table exposure) --- */

int robo_i18n_locale_supports_calendar(const robo_i18n_locale_t *loc, const char *calendar_id);
int robo_i18n_locale_supports_numbering(const robo_i18n_locale_t *loc, const char *ns_id);
int robo_i18n_locale_supports_collation_type(const robo_i18n_locale_t *loc, const char *type);
int robo_i18n_locale_has_display_names(const robo_i18n_locale_t *loc);
int robo_i18n_locale_has_timezone_display(const robo_i18n_locale_t *loc);
int robo_i18n_timezone_id_is_known(const char *id, size_t len);

/* --- Aggregated data versions --- */

typedef struct robo_i18n_data_versions {
    const char *api_version;
    const char *generator_version;
    const char *cldr_version;
    const char *unicode_version;
    const char *tzdb_version;
    const char *iana_file_date;
} robo_i18n_data_versions_t;

void robo_i18n_data_versions(robo_i18n_data_versions_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_LOCALE_SERVICE_H */
