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

#define RoboI18nUkeyMax 8u
#define RoboI18nUtypeMax 32u
#define RoboI18nUkeyCount 16u
#define RoboI18nFallbackMax 16u

/** Where a resolved setting came from. */
typedef enum I18nSettingSource {
    RoboI18nSrcUnset = 0,
    RoboI18nSrcOverride = 1,
    RoboI18nSrcExtension = 2,
    RoboI18nSrcLocaleDefault = 3,
    RoboI18nSrcRuntimeFallback = 4
} I18nSettingSource;

typedef enum I18nHourCycle {
    RoboI18nHcUnknown = 0,
    RoboI18nHcH11 = 1,
    RoboI18nHcH12 = 2,
    RoboI18nHcH23 = 3,
    RoboI18nHcH24 = 4
} I18nHourCycle;

/**
 * Parsed Unicode locale keywords (known keys + unknown preserved as opaque
 * remainder when re-serializing). Empty strings mean absent.
 */
typedef struct I18nUnicodeKeywords {
    char calendar[RoboI18nUtypeMax + 1u];       /* ca */
    char numberingSystem[RoboI18nUtypeMax + 1u]; /* nu */
    char hourCycle[RoboI18nUtypeMax + 1u];     /* hc: h11/h12/h23/h24 */
    char collation[RoboI18nUtypeMax + 1u];      /* co */
    char timezone[RoboI18nTzIdMax + 1u];       /* tz (BCP47 / IANA short form) */
    /** Other u- keys as "key-type-key-type…" (no leading u-); may be empty. */
    char other[RoboI18nExtMax + 1u];
} I18nUnicodeKeywords;

/**
 * Fully resolved runtime locale settings (RoboLocale context).
 * Identity remains in @p locale; settings never conflate locale with timezone.
 */
typedef struct I18nLocaleSettings {
    I18nLocale locale;
    char calendar[32];
    char numberingSystem[16];
    char hourCycle[8];
    char collationType[16];
    char timezoneId[RoboI18nTzIdMax + 1u]; /* empty = unset */
    I18nSettingSource calendarSrc;
    I18nSettingSource numberingSrc;
    I18nSettingSource hourCycleSrc;
    I18nSettingSource collationSrc;
    I18nSettingSource timezoneSrc;
} I18nLocaleSettings;

/**
 * Optional overrides when resolving settings.
 * Empty / NULL fields mean "no override".
 */
typedef struct I18nLocaleOverrides {
    const char *calendar;          /* CLDR calendar id */
    const char *numberingSystem;  /* e.g. arabext */
    const char *hourCycle;        /* h11/h12/h23/h24 */
    const char *collationType;    /* standard / phonebook / … */
    const char *timezoneId;       /* IANA id or fixed/UTC label */
} I18nLocaleOverrides;

void I18nLocaleOverridesInit(I18nLocaleOverrides *out);
void I18nUnicodeKeywordsClear(I18nUnicodeKeywords *out);
void I18nLocaleSettingsClear(I18nLocaleSettings *out);

/** Parse Unicode keywords from @p loc->extensions (u-… blob). */
Result I18nLocaleUnicodeKeywords(const I18nLocale *loc,
                                                I18nUnicodeKeywords *out);

/**
 * Apply keywords back onto @p loc->extensions (rebuilds u- extension).
 * Preserves non-u extensions already in loc if present after first singleton
 * is not 'u' — when loc has only u-, replaces it.
 */
Result I18nLocaleSetUnicodeKeywords(I18nLocale *loc,
                                                    const I18nUnicodeKeywords *kw);

/** Get a single known keyword value (empty string if absent). */
Result I18nLocaleGetKeyword(const I18nLocale *loc,
                                           const char *key,
                                           char *out,
                                           size_t outCap);

Result I18nLocaleSetKeyword(I18nLocale *loc,
                                           const char *key,
                                           const char *type);

/**
 * Resolve effective settings for @p locale with optional overrides.
 * Does not mutate @p locale.
 */
Result I18nLocaleResolve(const I18nLocale *locale,
                                       const I18nLocaleOverrides *overrides,
                                       I18nLocaleSettings *out);

/** Convenience resolvers (same precedence as resolve). */
Result I18nLocaleResolveNumbering(const I18nLocale *locale,
                                                 const char *overrideId,
                                                 char *outId,
                                                 size_t outCap);

Result I18nLocaleResolveCalendar(const I18nLocale *locale,
                                                const char *overrideId,
                                                char *outId,
                                                size_t outCap);

Result I18nLocaleResolveHourCycle(const I18nLocale *locale,
                                                  const char *overrideId,
                                                  char *outId,
                                                  size_t outCap);

Result I18nLocaleResolveCollation(const I18nLocale *locale,
                                                 const char *overrideId,
                                                 char *outId,
                                                 size_t outCap);

Result I18nLocaleResolveTimezone(const I18nLocale *locale,
                                                const char *overrideId,
                                                char *outId,
                                                size_t outCap);

I18nHourCycle I18nHourCycleFromId(const char *id);
const char *I18nHourCycleId(I18nHourCycle hc);

/**
 * CLDR data-inheritance fallback chain into @p out[0..*outCount).
 * Starts with @p loc; ends before inventing beyond root. Caps at @p cap.
 * Never infinite: max RoboI18nFallbackMax steps.
 */
Result I18nLocaleFallbackChain(const I18nLocale *loc,
                                              I18nLocale *out,
                                              size_t cap,
                                              size_t *outCount);

/**
 * Negotiate best match: walk requested preferences; for each, find best
 * available locale by I18nLocaleMatches ranking (EXACT > REGION > …).
 * Empty available → RoboInvalidArgument. No match → RoboNotFound.
 */
Result I18nLocaleNegotiate(const I18nLocaleList *requested,
                                         const I18nLocale *available,
                                         size_t availableCount,
                                         I18nLocale *outBest,
                                         I18nMatch *outMatch);

/* --- Application locale (process-wide; distinct from default / platform) --- */

Result I18nSetApplicationLocale(const I18nLocale *loc);
Result I18nGetApplicationLocale(I18nLocale *out);
/** Clear application locale so get falls through to default locale. */
void I18nClearApplicationLocale(void);

/**
 * Effective locale for the runtime: application if set, else default locale.
 */
Result I18nGetEffectiveLocale(I18nLocale *out);

/* --- Capability queries (stable; no raw table exposure) --- */

int I18nLocaleSupportsCalendar(const I18nLocale *loc, const char *calendarId);
int I18nLocaleSupportsNumbering(const I18nLocale *loc, const char *nsId);
int I18nLocaleSupportsCollationType(const I18nLocale *loc, const char *type);
int I18nLocaleHasDisplayNames(const I18nLocale *loc);
int I18nLocaleHasTimezoneDisplay(const I18nLocale *loc);
int I18nTimezoneIdIsKnown(const char *id, size_t len);

/* --- Aggregated data versions --- */

typedef struct I18nDataVersions {
    const char *apiVersion;
    const char *generatorVersion;
    const char *cldrVersion;
    const char *unicodeVersion;
    const char *tzdbVersion;
    const char *ianaFileDate;
    const char *ucaVersion;
    const char *iso4217Source;
} I18nDataVersions;

void GetI18nDataVersions(I18nDataVersions *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_LOCALE_SERVICE_H */
