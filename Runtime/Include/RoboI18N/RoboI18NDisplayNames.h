/*
 * RoboI18N locale display names (Goals 4.10 + 4.15).
 *
 * Distinguishes:
 *   identifier being described  (e.g. "ur", "Latn", "PK", "Asia/Karachi")
 *   vs display locale           (locale that provides the human-readable name)
 *
 * Time-zone display (Goal 4.15) is localized *presentation* data from CLDR —
 * not tzdb transition rules (Goal 4.14). Instant selects metazone windows and
 * standard vs daylight forms via RoboI18NTimeZone.
 *
 * Data: CLDR 46 (RoboDispData). Fallback chain:
 *   display locale → CLDR parentLocales / truncation parents → "en" →
 *   documented style fallbacks → stable identifier (if FALLBACK_CODE) or
 *   RoboNotFound.
 *
 * Never invents translations. Output is logical UTF-8 (no bidi reordering).
 *
 * Ownership: write into caller buffers. Tables are immutable process data.
 * Thread safety: pure lookups; no global mutable state.
 * Language: C.
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

const char *I18nDisplayCldrVersionString(void);

typedef enum I18nDisplayFallback {
    /** Missing name → RoboNotFound; out buffer untouched / empty. */
    RoboI18nDispFallbackNotFound = 0,
    /** Missing name → copy canonical identifier; return RoboOk. */
    RoboI18nDispFallbackCode = 1
} I18nDisplayFallback;

typedef enum I18nCurrencyNameStyle {
    RoboI18nCurrencyName = 0,
    RoboI18nCurrencySymbol = 1,
    RoboI18nCurrencyNarrowSymbol = 2
} I18nCurrencyNameStyle;

/**
 * Time-zone display styles (Goal 4.15).
 *
 * SPECIFIC_* pick standard vs daylight from @p instant (via tzdb DST).
 * GENERIC_* never commit to DST; they use CLDR generic forms.
 * GMT formats the offset with CLDR gmtFormat/hourFormat (and numbering digits).
 * IDENTIFIER returns the requested zone id (aliases keep the requested spelling).
 */
typedef enum I18nTimezoneNameStyle {
    RoboI18nTzDispExemplarCity = 0,
    RoboI18nTzDispLongGeneric = 1,
    RoboI18nTzDispLongStandard = 2,
    RoboI18nTzDispLongDaylight = 3,
    RoboI18nTzDispGmt = 4,
    RoboI18nTzDispShortGeneric = 5,
    RoboI18nTzDispShortStandard = 6,
    RoboI18nTzDispShortDaylight = 7,
    RoboI18nTzDispSpecificLong = 8,
    RoboI18nTzDispSpecificShort = 9,
    RoboI18nTzDispIdentifier = 10
} I18nTimezoneNameStyle;

/**
 * Options for display-name lookups.
 * fallback defaults to FALLBACK_CODE.
 * dialect: reserved (CLDR -alt-menu / dialect); ignored → standard names.
 */
typedef struct I18nDisplayOptions {
    I18nDisplayFallback fallback;
    uint8_t dialect; /* 0 = standard (only implemented) */
} I18nDisplayOptions;

void I18nDisplayOptionsInit(I18nDisplayOptions *out);

/**
 * All APIs:
 *   @p displayLocale — locale used to look up the name (may be NULL → "en")
 *   @p options        — NULL → defaults
 *   @p out / @p outCap / @p outLen — UTF-8 buffer; *outLen excludes NUL;
 *     always NUL-terminates when outCap > 0 and RoboOk.
 *   RoboError if buffer too small (sets *outLen to needed size including NUL).
 */

Result I18nLanguageDisplayName(const char *languageId,
                                              size_t languageLen,
                                              const I18nLocale *displayLocale,
                                              const I18nDisplayOptions *options,
                                              char *out,
                                              size_t outCap,
                                              size_t *outLen);

Result I18nScriptDisplayName(const char *scriptId,
                                            size_t scriptLen,
                                            const I18nLocale *displayLocale,
                                            const I18nDisplayOptions *options,
                                            char *out,
                                            size_t outCap,
                                            size_t *outLen);

Result I18nRegionDisplayName(const char *regionId,
                                            size_t regionLen,
                                            const I18nLocale *displayLocale,
                                            const I18nDisplayOptions *options,
                                            char *out,
                                            size_t outCap,
                                            size_t *outLen);

Result I18nCurrencyDisplayName(const char *currencyCode,
                                              size_t currencyLen,
                                              I18nCurrencyNameStyle style,
                                              const I18nLocale *displayLocale,
                                              const I18nDisplayOptions *options,
                                              char *out,
                                              size_t outCap,
                                              size_t *outLen);

Result I18nCalendarDisplayName(const I18nCalendar *calendar,
                                              const I18nLocale *displayLocale,
                                              const I18nDisplayOptions *options,
                                              char *out,
                                              size_t outCap,
                                              size_t *outLen);

Result I18nNumberingSystemDisplayName(
    const char *numberingId,
    size_t numberingLen,
    const I18nLocale *displayLocale,
    const I18nDisplayOptions *options,
    char *out,
    size_t outCap,
    size_t *outLen);

/**
 * Time-zone display name at a specific instant (preferred API).
 *
 * @p instant may be NULL for styles that do not need DST/metazone windows
 * (EXEMPLAR, IDENTIFIER, LONG/SHORT GENERIC/STANDARD/DAYLIGHT explicit forms).
 * GMT and SPECIFIC_* require an instant for IANA zones (FIXED/UTC OK without).
 * Unknown IANA id → RoboNotFound from timezone layer before display lookup.
 * Aliases: data lookup uses canonical id; IDENTIFIER returns requested id.
 */
Result I18nTimezoneDisplayNameAt(const I18nTimezone *tz,
                                                 const I18nInstant *instant,
                                                 I18nTimezoneNameStyle style,
                                                 const I18nLocale *displayLocale,
                                                 const I18nDisplayOptions *options,
                                                 char *out,
                                                 size_t outCap,
                                                 size_t *outLen);

/**
 * Convenience wrapper: calls display_name_at with @p instant = NULL.
 * For GMT on IANA without instant → RoboInvalidArgument.
 */
Result I18nTimezoneDisplayName(const I18nTimezone *tz,
                                              I18nTimezoneNameStyle style,
                                              const I18nLocale *displayLocale,
                                              const I18nDisplayOptions *options,
                                              char *out,
                                              size_t outCap,
                                              size_t *outLen);

/**
 * Compose a locale display name using CLDR localePattern / localeSeparator.
 * Uses language + optional script + region from @p described.
 */
Result I18nLocaleDisplayName(const I18nLocale *described,
                                            const I18nLocale *displayLocale,
                                            const I18nDisplayOptions *options,
                                            char *out,
                                            size_t outCap,
                                            size_t *outLen);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_DISPLAY_NAMES_H */
