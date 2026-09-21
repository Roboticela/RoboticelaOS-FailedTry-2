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
#define RoboI18nAcceptLangMaxBytes 2048u
#define RoboI18nPrefMaxRanges 32u
#define RoboI18nRangeMaxBytes RoboI18nTagMax
#define RoboI18nSupportedMax 256u

/** q in millipercent: 1000 = 1.0, 900 = 0.9, 0 = exclude. */
#define RoboI18nQMilliMax 1000u
#define RoboI18nQMilliDefault 1000u

typedef uint16_t I18nQMilli;

/**
 * Why a supported locale was selected (diagnostic; not part of locale identity).
 */
typedef enum I18nNegotiateKind {
    RoboI18nNegNone = 0,
    RoboI18nNegExact = 1,
    RoboI18nNegRegion = 2,
    RoboI18nNegScript = 3,
    RoboI18nNegLanguage = 4,
    RoboI18nNegWildcard = 5,
    RoboI18nNegFallback = 6 /* reserved; negotiation itself does not walk data parents */
} I18nNegotiateKind;

/**
 * One language range / preference entry.
 * Wildcard: isWildcard != 0 (locale fields unused).
 * qMilli == 0 means this range is an exclusion.
 */
typedef struct I18nLanguageRange {
    uint8_t isWildcard;
    uint8_t qExplicit; /* 1 if ";q=" was present */
    I18nQMilli qMilli;
    I18nLocale locale; /* canonical when !isWildcard */
} I18nLanguageRange;

typedef struct I18nPreferenceList I18nPreferenceList;

typedef struct I18nNegotiateResult {
    I18nLocale selected;     /* supported locale chosen */
    I18nLocale requested;    /* preference locale that won (* → empty) */
    I18nNegotiateKind kind;
    size_t preferenceIndex;         /* index in normalized preference list */
    I18nQMilli qMilli;
} I18nNegotiateResult;

void I18nLanguageRangeClear(I18nLanguageRange *r);
void I18nNegotiateResultClear(I18nNegotiateResult *r);

/**
 * Parse one language range: "*" or a BCP 47 tag (canonicalized).
 * Optional ";q=0.8" (0..1, ≤3 fractional digits). Default q = 1000.
 * Malformed → RoboParseError. Does not accept extended *-CH forms.
 */
Result I18nLanguageRangeParse(const char *text,
                                             size_t len,
                                             I18nLanguageRange *out);

/** Match quality of @p supported against @p range (lookup-style). */
I18nNegotiateKind I18nLocaleMatchQuality(
    const I18nLanguageRange *range,
    const I18nLocale *supported);

/** Map negotiate kind ↔ legacy match enum where possible. */
I18nMatch I18nNegotiateKindToMatch(I18nNegotiateKind k);

/* --- Preference lists --- */

void I18nPreferenceListDestroy(I18nPreferenceList *list);
size_t I18nPreferenceListSize(const I18nPreferenceList *list);

Result I18nPreferenceListGet(const I18nPreferenceList *list,
                                            size_t index,
                                            I18nLanguageRange *out);

/**
 * Parse HTTP Accept-Language (or identical preference syntax).
 * Skips empty entries. Skips entries that fail range parse.
 * Rejects: len > RoboI18nAcceptLangMaxBytes, > RoboI18nPrefMaxRanges
 * after normalization attempt, invalid UTF-8 (non-ASCII control except HTAB).
 * Does not sort; call normalize for deterministic priority order.
 */
Result I18nAcceptLanguageParse(const char *text,
                                              size_t len,
                                              I18nPreferenceList **out);

/**
 * Serialize preference list to Accept-Language form.
 * q=1.0 omitted; other q values use up to 3 decimal digits.
 * Semantic round-trip (not byte-identical).
 */
Result I18nAcceptLanguageSerialize(const I18nPreferenceList *list,
                                                  char *buf,
                                                  size_t bufCap,
                                                  size_t *outLen);

/**
 * Normalize in place:
 *   - sort by q descending (stable)
 *   - duplicate ranges (same wildcard/locale identity): highest-q wins;
 *     equal q → first-wins
 *   - q=0 exclusions retained (sorted with other zeros at end)
 *   - empty list remains empty
 */
Result I18nPreferenceListNormalize(I18nPreferenceList *list);

/**
 * Convert locale array to preference ranges (each q=1000, list order).
 */
Result I18nPreferenceListFromLocales(const I18nLocale *locales,
                                                     size_t count,
                                                     I18nPreferenceList **out);

/**
 * Build preference list from Goal 4.1 locale_list, preserving q-weights
 * (converted to millipercent). Caller destroys with preference_list_destroy.
 */
Result I18nPreferenceListFromLocaleList(
    const I18nLocaleList *list,
    I18nPreferenceList **out);

/**
 * Negotiate: pick best supported locale for normalized preferences.
 * Canonicalizes supported locales internally (does not mutate inputs).
 * @p supportedCount must be ≤ RoboI18nSupportedMax.
 *
 * Algorithm (documented):
 *  1. Normalize preferences if not already (caller should normalize).
 *  2. Collect exclusions (q=0 ranges).
 *  3. Walk preferences with q>0 in order; for each, score all supported;
 *     pick highest kind; ties → lowest supported index (deterministic).
 *  4. Wildcard matches first non-excluded supported (index order).
 *  5. Script conflict after likely-subtag maximize → no match.
 *
 * No match → RoboNotFound.
 */
Result I18nLocaleNegotiateEx(const I18nPreferenceList *prefs,
                                            const I18nLocale *supported,
                                            size_t supportedCount,
                                            I18nNegotiateResult *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_LOCALE_NEGOTIATE_H */
