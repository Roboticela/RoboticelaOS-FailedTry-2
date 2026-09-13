/*
 * RoboI18N locale negotiation & Accept-Language (Goal 4.17).
 *
 * Language: C.
 *
 * Distinguishes negotiation (pick best supported locale for preferences)
 * from resource fallback (Goal 4.16 data-parent chains). This is not
 * language detection.
 *
 * Matching uses BCP 47 + CLDR likely subtags for script compatibility.
 * Quality values use millipercent integers (0..1000), not float compares.
 */

#ifndef ROBO_I18N_LOCALE_NEGOTIATE_H
#define ROBO_I18N_LOCALE_NEGOTIATE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NLocale.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Resource / security limits (untrusted Accept-Language). */
#define ROBO_I18N_ACCEPT_LANG_MAX_BYTES 2048u
#define ROBO_I18N_PREF_MAX_RANGES 32u
#define ROBO_I18N_RANGE_MAX_BYTES ROBO_I18N_TAG_MAX
#define ROBO_I18N_SUPPORTED_MAX 256u

/** q in millipercent: 1000 = 1.0, 900 = 0.9, 0 = exclude. */
#define ROBO_I18N_Q_MILLI_MAX 1000u
#define ROBO_I18N_Q_MILLI_DEFAULT 1000u

typedef uint16_t robo_i18n_q_milli_t;

/**
 * Why a supported locale was selected (diagnostic; not part of locale identity).
 */
typedef enum robo_i18n_negotiate_kind {
    ROBO_I18N_NEG_NONE = 0,
    ROBO_I18N_NEG_EXACT = 1,
    ROBO_I18N_NEG_REGION = 2,
    ROBO_I18N_NEG_SCRIPT = 3,
    ROBO_I18N_NEG_LANGUAGE = 4,
    ROBO_I18N_NEG_WILDCARD = 5,
    ROBO_I18N_NEG_FALLBACK = 6 /* reserved; negotiation itself does not walk data parents */
} robo_i18n_negotiate_kind_t;

/**
 * One language range / preference entry.
 * Wildcard: is_wildcard != 0 (locale fields unused).
 * q_milli == 0 means this range is an exclusion.
 */
typedef struct robo_i18n_language_range {
    uint8_t is_wildcard;
    uint8_t q_explicit; /* 1 if ";q=" was present */
    robo_i18n_q_milli_t q_milli;
    robo_i18n_locale_t locale; /* canonical when !is_wildcard */
} robo_i18n_language_range_t;

typedef struct robo_i18n_preference_list robo_i18n_preference_list_t;

typedef struct robo_i18n_negotiate_result {
    robo_i18n_locale_t selected;     /* supported locale chosen */
    robo_i18n_locale_t requested;    /* preference locale that won (* → empty) */
    robo_i18n_negotiate_kind_t kind;
    size_t preference_index;         /* index in normalized preference list */
    robo_i18n_q_milli_t q_milli;
} robo_i18n_negotiate_result_t;

void robo_i18n_language_range_clear(robo_i18n_language_range_t *r);
void robo_i18n_negotiate_result_clear(robo_i18n_negotiate_result_t *r);

/**
 * Parse one language range: "*" or a BCP 47 tag (canonicalized).
 * Optional ";q=0.8" (0..1, ≤3 fractional digits). Default q = 1000.
 * Malformed → ROBO_PARSE_ERROR. Does not accept extended *-CH forms.
 */
robo_result_t robo_i18n_language_range_parse(const char *text,
                                             size_t len,
                                             robo_i18n_language_range_t *out);

/** Match quality of @p supported against @p range (lookup-style). */
robo_i18n_negotiate_kind_t robo_i18n_locale_match_quality(
    const robo_i18n_language_range_t *range,
    const robo_i18n_locale_t *supported);

/** Map negotiate kind ↔ legacy match enum where possible. */
robo_i18n_match_t robo_i18n_negotiate_kind_to_match(robo_i18n_negotiate_kind_t k);

/* --- Preference lists --- */

void robo_i18n_preference_list_destroy(robo_i18n_preference_list_t *list);
size_t robo_i18n_preference_list_size(const robo_i18n_preference_list_t *list);

robo_result_t robo_i18n_preference_list_get(const robo_i18n_preference_list_t *list,
                                            size_t index,
                                            robo_i18n_language_range_t *out);

/**
 * Parse HTTP Accept-Language (or identical preference syntax).
 * Skips empty entries. Skips entries that fail range parse.
 * Rejects: len > ROBO_I18N_ACCEPT_LANG_MAX_BYTES, > ROBO_I18N_PREF_MAX_RANGES
 * after normalization attempt, invalid UTF-8 (non-ASCII control except HTAB).
 * Does not sort; call normalize for deterministic priority order.
 */
robo_result_t robo_i18n_accept_language_parse(const char *text,
                                              size_t len,
                                              robo_i18n_preference_list_t **out);

/**
 * Serialize preference list to Accept-Language form.
 * q=1.0 omitted; other q values use up to 3 decimal digits.
 * Semantic round-trip (not byte-identical).
 */
robo_result_t robo_i18n_accept_language_serialize(const robo_i18n_preference_list_t *list,
                                                  char *buf,
                                                  size_t buf_cap,
                                                  size_t *out_len);

/**
 * Normalize in place:
 *   - sort by q descending (stable)
 *   - duplicate ranges (same wildcard/locale identity): highest-q wins;
 *     equal q → first-wins
 *   - q=0 exclusions retained (sorted with other zeros at end)
 *   - empty list remains empty
 */
robo_result_t robo_i18n_preference_list_normalize(robo_i18n_preference_list_t *list);

/**
 * Convert locale array to preference ranges (each q=1000, list order).
 */
robo_result_t robo_i18n_preference_list_from_locales(const robo_i18n_locale_t *locales,
                                                     size_t count,
                                                     robo_i18n_preference_list_t **out);

/**
 * Build preference list from Goal 4.1 locale_list, preserving q-weights
 * (converted to millipercent). Caller destroys with preference_list_destroy.
 */
robo_result_t robo_i18n_preference_list_from_locale_list(
    const robo_i18n_locale_list_t *list,
    robo_i18n_preference_list_t **out);

/**
 * Negotiate: pick best supported locale for normalized preferences.
 * Canonicalizes supported locales internally (does not mutate inputs).
 * @p supported_count must be ≤ ROBO_I18N_SUPPORTED_MAX.
 *
 * Algorithm (documented):
 *  1. Normalize preferences if not already (caller should normalize).
 *  2. Collect exclusions (q=0 ranges).
 *  3. Walk preferences with q>0 in order; for each, score all supported;
 *     pick highest kind; ties → lowest supported index (deterministic).
 *  4. Wildcard matches first non-excluded supported (index order).
 *  5. Script conflict after likely-subtag maximize → no match.
 *
 * No match → ROBO_NOT_FOUND.
 */
robo_result_t robo_i18n_locale_negotiate_ex(const robo_i18n_preference_list_t *prefs,
                                            const robo_i18n_locale_t *supported,
                                            size_t supported_count,
                                            robo_i18n_negotiate_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_LOCALE_NEGOTIATE_H */
