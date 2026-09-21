/*
 * RoboI18N measurement systems & unit preferences (Goal 4.20).
 *
 * Selects which units a locale/preference context should use. Formatting and
 * conversion remain in RoboI18NUnit (Goal 4.19). Measurement system lookup
 * from Goal 4.2 is reused and extended with CLDR unitPreferenceData.
 *
 * Precedence for preferred unit resolution:
 *   1. Explicit unit argument (API call)
 *   2. Per-category override on the preference context
 *   3. System-level override on the context (affects category defaults)
 *   4. Unicode locale extension `ms` (metric / ussystem / uksystem)
 *   5. CLDR unitPreferenceData for region + usage (fallback region 001)
 *   6. Measurement-system heuristic defaults
 *
 * An explicit unit request is never replaced by locale defaults.
 *
 * Thread safety: contexts are value types (no heap). Not synchronized —
 * share only when immutable / read-only after init, or lock externally.
 */

#ifndef ROBO_I18N_MEASURE_H
#define ROBO_I18N_MEASURE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboI18NNumber.h"
#include "RoboI18N/RoboI18NUnit.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Semantic measurement categories (stable). New CLDR categories can be added
 * at the end without renumbering existing values.
 */
typedef enum I18nMeasureCategory {
    RoboI18nMeasureCatTemperature = 0,
    RoboI18nMeasureCatLength = 1,
    RoboI18nMeasureCatArea = 2,
    RoboI18nMeasureCatMass = 3,
    RoboI18nMeasureCatVolume = 4,
    RoboI18nMeasureCatSpeed = 5,
    RoboI18nMeasureCatPressure = 6,
    RoboI18nMeasureCatEnergy = 7,
    RoboI18nMeasureCatPower = 8,
    RoboI18nMeasureCatCount = 9
} I18nMeasureCategory;

/**
 * Preference usage within a category (CLDR usage names). DEFAULT is always
 * valid; other usages are category-specific (unsupported → fall back DEFAULT).
 */
typedef enum I18nMeasureUsage {
    RoboI18nMeasureUsageDefault = 0,
    RoboI18nMeasureUsageWeather = 1,
    RoboI18nMeasureUsageRoad = 2,
    RoboI18nMeasureUsageFluid = 3,
    RoboI18nMeasureUsageFood = 4,
    RoboI18nMeasureUsageEngine = 5,
    RoboI18nMeasureUsagePerson = 6,
    RoboI18nMeasureUsageLand = 7,
    RoboI18nMeasureUsageCount = 8
} I18nMeasureUsage;

typedef enum I18nMeasureSource {
    RoboI18nMeasureSrcExplicit = 0,
    RoboI18nMeasureSrcCategoryOverride = 1,
    RoboI18nMeasureSrcSystemOverride = 2,
    RoboI18nMeasureSrcUnicodeMs = 3,
    RoboI18nMeasureSrcCldrPreference = 4,
    RoboI18nMeasureSrcSystemDefault = 5
} I18nMeasureSource;

/**
 * Immutable-after-init preference context (stack-allocatable).
 * No global mutable preference state.
 */
typedef struct I18nMeasurePrefs {
    I18nLocale locale;
    char region[8];
    I18nMeasurement system; /* effective measurement system */
    I18nMeasurement tempSystem; /* temperature category system */
    uint8_t systemOverridden; /* 1 if set_system was used */
    uint8_t hasCatOverride[RoboI18nMeasureCatCount];
    I18nUnitId catOverride[RoboI18nMeasureCatCount];
    uint8_t ready;
} I18nMeasurePrefs;

void I18nMeasurePrefsClear(I18nMeasurePrefs *prefs);

/**
 * Build locale-derived defaults (region maximize + CLDR systems + ms keyword).
 * Does not read OS settings. Safe to call repeatedly.
 */
Result I18nMeasurePrefsInit(I18nMeasurePrefs *prefs,
                                           const I18nLocale *locale);

/** Override measurement system (metric/US/UK). Does not clear category overrides. */
Result I18nMeasurePrefsSetSystem(I18nMeasurePrefs *prefs,
                                                 I18nMeasurement system);

/** Clear system override; re-resolve from locale / Unicode `ms`. */
Result I18nMeasurePrefsClearSystem(I18nMeasurePrefs *prefs);

/** Per-category preferred unit override (must match category quantity). */
Result I18nMeasurePrefsSetUnit(I18nMeasurePrefs *prefs,
                                               I18nMeasureCategory category,
                                               I18nUnitId unit);

Result I18nMeasurePrefsClearUnit(I18nMeasurePrefs *prefs,
                                                 I18nMeasureCategory category);

I18nMeasurement I18nMeasurePrefsSystem(
    const I18nMeasurePrefs *prefs);

I18nMeasurement I18nMeasurePrefsTemperatureSystem(
    const I18nMeasurePrefs *prefs);

/**
 * Resolve preferred unit. @p explicitUnit non-NULL → always wins (SRC_EXPLICIT).
 * Unknown category → RoboNotSupported.
 */
Result I18nMeasureResolveUnit(const I18nMeasurePrefs *prefs,
                                             I18nMeasureCategory category,
                                             I18nMeasureUsage usage,
                                             const I18nUnitId *explicitUnit,
                                             I18nUnitId *outUnit,
                                             I18nMeasureSource *outSrc);

Result I18nMeasurePreferredUnit(const I18nMeasurePrefs *prefs,
                                               I18nMeasureCategory category,
                                               I18nUnitId *outUnit);

/**
 * Convenience: preferred unit for a usage (e.g. VOLUME+FLUID → gallon in US).
 */
Result I18nMeasurePreferredUnitUsage(
    const I18nMeasurePrefs *prefs,
    I18nMeasureCategory category,
    I18nMeasureUsage usage,
    I18nUnitId *outUnit);

/**
 * Convert @p value from @p fromUnit into the resolved preferred unit, then
 * format with @p fmt. If @p explicitUnit is set, that unit is used (no
 * preference override). Uses RoboI18NUnit conversion + formatting.
 */
Result I18nMeasureFormatConverted(
    const I18nMeasurePrefs *prefs,
    const I18nUnitFormatter *fmt,
    I18nMeasureCategory category,
    I18nMeasureUsage usage,
    const I18nDecimal *value,
    I18nUnitId fromUnit,
    const I18nUnitId *explicitUnit,
    char *buf,
    size_t bufCap,
    size_t *outLen);

Result I18nMeasureFormatConvertedI64(
    const I18nMeasurePrefs *prefs,
    const I18nUnitFormatter *fmt,
    I18nMeasureCategory category,
    I18nMeasureUsage usage,
    int64_t value,
    I18nUnitId fromUnit,
    const I18nUnitId *explicitUnit,
    char *buf,
    size_t bufCap,
    size_t *outLen);

/** Locale-only helpers (no preference context). */
Result I18nMeasureSystemForLocale(const I18nLocale *locale,
                                                  I18nMeasurement *out);

Result I18nMeasureTemperatureSystemForLocale(
    const I18nLocale *locale,
    I18nMeasurement *out);

const char *I18nMeasureCategoryName(I18nMeasureCategory category);
const char *I18nMeasureUsageName(I18nMeasureUsage usage);
const char *I18nMeasureCldrVersionString(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_MEASURE_H */
