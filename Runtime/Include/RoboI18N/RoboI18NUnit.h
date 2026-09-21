/*
 * RoboI18N unit formatting + conversion foundation (Goal 4.19).
 *
 * Formats measurement values with CLDR unit patterns (long/short/narrow),
 * plural categories, and locale-aware number formatting. Compound "per"
 * units use perUnitPattern when available, else compoundUnitPattern.
 *
 * Conversion is separate from localization: convert quantities first, then
 * format. Exact rational SI conversions where practical; approximate
 * factors are documented on each conversion path.
 *
 * Does NOT parse unit strings (Goal 4.22 territory).
 *
 * Thread safety: a formatter is not synchronized — one owner thread, or lock.
 * Generated CLDR tables are immortal/read-only.
 */

#ifndef ROBO_I18N_UNIT_H
#define ROBO_I18N_UNIT_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NNumber.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Curated CLDR unit identifiers (type-subtype). Order matches RoboUnitData.
 * Unsupported ids return RoboNotSupported / RoboInvalidArgument.
 */
typedef enum I18nUnitId {
    RoboI18nUnitLengthKilometer = 0,
    RoboI18nUnitLengthMeter = 1,
    RoboI18nUnitLengthCentimeter = 2,
    RoboI18nUnitLengthMillimeter = 3,
    RoboI18nUnitLengthMile = 4,
    RoboI18nUnitLengthYard = 5,
    RoboI18nUnitLengthFoot = 6,
    RoboI18nUnitLengthInch = 7,
    RoboI18nUnitMassKilogram = 8,
    RoboI18nUnitMassGram = 9,
    RoboI18nUnitMassPound = 10,
    RoboI18nUnitMassOunce = 11,
    RoboI18nUnitAreaSquareKilometer = 12,
    RoboI18nUnitAreaSquareMeter = 13,
    RoboI18nUnitAreaHectare = 14,
    RoboI18nUnitAreaAcre = 15,
    RoboI18nUnitVolumeLiter = 16,
    RoboI18nUnitVolumeMilliliter = 17,
    RoboI18nUnitVolumeGallon = 18,
    RoboI18nUnitVolumeCubicMeter = 19,
    RoboI18nUnitDurationYear = 20,
    RoboI18nUnitDurationMonth = 21,
    RoboI18nUnitDurationWeek = 22,
    RoboI18nUnitDurationDay = 23,
    RoboI18nUnitDurationHour = 24,
    RoboI18nUnitDurationMinute = 25,
    RoboI18nUnitDurationSecond = 26,
    RoboI18nUnitDurationMillisecond = 27,
    RoboI18nUnitSpeedKilometerPerHour = 28,
    RoboI18nUnitSpeedMeterPerSecond = 29,
    RoboI18nUnitSpeedMilePerHour = 30,
    RoboI18nUnitTemperatureCelsius = 31,
    RoboI18nUnitTemperatureFahrenheit = 32,
    RoboI18nUnitTemperatureKelvin = 33,
    RoboI18nUnitDigitalBit = 34,
    RoboI18nUnitDigitalByte = 35,
    RoboI18nUnitDigitalKilobyte = 36,
    RoboI18nUnitDigitalMegabyte = 37,
    RoboI18nUnitDigitalGigabyte = 38,
    RoboI18nUnitDigitalTerabyte = 39,
    RoboI18nUnitEnergyJoule = 40,
    RoboI18nUnitEnergyKilowattHour = 41,
    RoboI18nUnitEnergyCalorie = 42,
    RoboI18nUnitEnergyFoodcalorie = 43,
    RoboI18nUnitPowerWatt = 44,
    RoboI18nUnitPowerKilowatt = 45,
    RoboI18nUnitPowerHorsepower = 46,
    RoboI18nUnitPressurePascal = 47,
    RoboI18nUnitPressureHectopascal = 48,
    RoboI18nUnitPressureMillibar = 49,
    RoboI18nUnitPressurePoundForcePerSquareInch = 50,
    RoboI18nUnitFrequencyHertz = 51,
    RoboI18nUnitFrequencyKilohertz = 52,
    RoboI18nUnitFrequencyMegahertz = 53,
    RoboI18nUnitFrequencyGigahertz = 54,
    RoboI18nUnitAngleDegree = 55,
    RoboI18nUnitAngleRadian = 56,
    RoboI18nUnitAngleArcMinute = 57,
    RoboI18nUnitAngleArcSecond = 58,
    RoboI18nUnitConsumptionLiterPer100Kilometer = 59,
    RoboI18nUnitCount = 60
} I18nUnitId;

typedef enum I18nUnitWidth {
    RoboI18nUnitWidthLong = 0,
    RoboI18nUnitWidthShort = 1,
    RoboI18nUnitWidthNarrow = 2
} I18nUnitWidth;

typedef enum I18nUnitQuantity {
    RoboI18nUnitQLength = 0,
    RoboI18nUnitQMass = 1,
    RoboI18nUnitQArea = 2,
    RoboI18nUnitQVolume = 3,
    RoboI18nUnitQDuration = 4,
    RoboI18nUnitQSpeed = 5,
    RoboI18nUnitQTemperature = 6,
    RoboI18nUnitQDigital = 7,
    RoboI18nUnitQEnergy = 8,
    RoboI18nUnitQPower = 9,
    RoboI18nUnitQPressure = 10,
    RoboI18nUnitQFrequency = 11,
    RoboI18nUnitQAngle = 12,
    RoboI18nUnitQConsumption = 13,
    RoboI18nUnitQUnknown = 255
} I18nUnitQuantity;

typedef struct I18nUnitOptions {
    I18nUnitWidth width;
    char numberingSystem[16];
} I18nUnitOptions;

typedef struct I18nUnitFormatter {
    I18nLocale locale;
    I18nUnitOptions options;
    I18nNumberFormatter number;
    uint8_t ready;
} I18nUnitFormatter;

void I18nUnitOptionsInit(I18nUnitOptions *opts);

Result I18nUnitFormatterInit(I18nUnitFormatter *fmt,
                                            const I18nLocale *locale,
                                            const I18nUnitOptions *opts);

void I18nUnitFormatterDestroy(I18nUnitFormatter *fmt);

/** CLDR key for a curated unit id, or NULL if out of range. */
const char *I18nUnitCldrKey(I18nUnitId unit);

/** Resolve CLDR type-subtype key to curated id (RoboNotFound if unknown). */
Result I18nUnitIdFromCldrKey(const char *key, I18nUnitId *out);

I18nUnitQuantity MakeI18nUnitQuantity(I18nUnitId unit);

/**
 * True when patterns exist for the resolved locale (after fallback) at the
 * formatter width. Unknown locales fall back toward English.
 */
int I18nUnitHasPatterns(const I18nLocale *locale,
                                I18nUnitId unit,
                                I18nUnitWidth width);

Result I18nUnitFormat(const I18nUnitFormatter *fmt,
                                    const I18nDecimal *value,
                                    I18nUnitId unit,
                                    char *buf,
                                    size_t bufCap,
                                    size_t *outLen);

Result I18nUnitFormatI64(const I18nUnitFormatter *fmt,
                                        int64_t value,
                                        I18nUnitId unit,
                                        char *buf,
                                        size_t bufCap,
                                        size_t *outLen);

Result I18nUnitFormatDouble(const I18nUnitFormatter *fmt,
                                           double value,
                                           I18nUnitId unit,
                                           char *buf,
                                           size_t bufCap,
                                           size_t *outLen);

/**
 * Compound “value unitNum per unitDen” (e.g. megabyte / second).
 * Prefers unitDen’s perUnitPattern; else compound “per” + displayName.
 * Precomposed units (km/h, L/100km) should use format_* with that unit id.
 */
Result I18nUnitFormatPer(const I18nUnitFormatter *fmt,
                                        const I18nDecimal *value,
                                        I18nUnitId unitNum,
                                        I18nUnitId unitDen,
                                        char *buf,
                                        size_t bufCap,
                                        size_t *outLen);

Result I18nUnitFormatPerI64(const I18nUnitFormatter *fmt,
                                            int64_t value,
                                            I18nUnitId unitNum,
                                            I18nUnitId unitDen,
                                            char *buf,
                                            size_t bufCap,
                                            size_t *outLen);

Result I18nUnitFormatToString(const I18nUnitFormatter *fmt,
                                              const I18nDecimal *value,
                                              I18nUnitId unit,
                                              String *out);

/**
 * Convert between compatible units into an exact decimal when possible.
 *
 * Exact: metric length/mass/area/volume/duration/digital/frequency factors
 * that are integer powers of 10 (or exact binary digital 1000-based SI
 * prefixes used by CLDR digital-* keys).
 *
 * Approximate (IEEE-754 double path, marked via *approximateOut=1):
 * customary length/mass/volume, horsepower, psi, food calorie, temperature
 * affine transforms, speed customary, acre/hectare bridges.
 *
 * Cross-quantity conversion → RoboNotSupported.
 * Temperature uses affine formulas (not a pure scale).
 */
Result I18nUnitConvert(I18nUnitId from,
                                     I18nUnitId to,
                                     const I18nDecimal *in,
                                     I18nDecimal *out,
                                     int *approximateOut);

Result I18nUnitConvertI64(I18nUnitId from,
                                         I18nUnitId to,
                                         int64_t value,
                                         int64_t *out,
                                         int *approximateOut);

const char *I18nUnitCldrVersionString(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_UNIT_H */
