/*
 * RoboI18N measurement preferences (Goal 4.20).
 */

#include "RoboI18N/RoboI18NMeasure.h"

#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboI18N/RoboMeasurePrefData.h"

#include <string.h>

static void CopyId(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= cap) {
        n = cap - 1u;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int ParseMsKeyword(const char *type, I18nMeasurement *out)
{
    if (!type || !out) {
        return 0;
    }
    if (strcmp(type, "metric") == 0) {
        *out = RoboI18nMeasureMetric;
        return 1;
    }
    if (strcmp(type, "ussystem") == 0 || strcmp(type, "US") == 0) {
        *out = RoboI18nMeasureUs;
        return 1;
    }
    if (strcmp(type, "uksystem") == 0 || strcmp(type, "UK") == 0) {
        *out = RoboI18nMeasureUk;
        return 1;
    }
    return 0;
}

static void ResolveRegion(const I18nLocale *loc, char *region, size_t cap)
{
    if (loc->region[0]) {
        CopyId(region, cap, loc->region);
        return;
    }
    {
        I18nLocale maxed;
        if (I18nLocaleMaximize(loc, &maxed) == RoboOk && maxed.region[0]) {
            CopyId(region, cap, maxed.region);
            return;
        }
    }
    CopyId(region, cap, "001");
}

static I18nMeasurement LookupSystemTable(const char *region, int temperature)
{
    const MprefSystem *row;
    if (temperature) {
        row = MprefTempSystemFind(region);
        if (row) {
            return (I18nMeasurement)row->system;
        }
        return RoboI18nMeasureMetric; /* no entry → use world default metric */
    }
    row = MprefSystemFind(region);
    if (!row) {
        row = MprefSystemFind("001");
    }
    if (!row) {
        return RoboI18nMeasureMetric;
    }
    return (I18nMeasurement)row->system;
}

static int ReadMsFromLocale(const I18nLocale *loc, I18nMeasurement *out)
{
    I18nUnicodeKeywords kw;
    const char *p;
    char ms[32];

    if (!loc || !out) {
        return 0;
    }
    ms[0] = '\0';
    if (I18nLocaleGetKeyword(loc, "ms", ms, sizeof(ms)) == RoboOk &&
        ParseMsKeyword(ms, out)) {
        return 1;
    }
    if (I18nLocaleUnicodeKeywords(loc, &kw) != RoboOk) {
        return 0;
    }
    /* Unknown keys (incl. ms) live in kw.other as key-type-key-type… */
    p = kw.other;
    while (p && *p) {
        const char *hit = strstr(p, "ms-");
        if (!hit) {
            break;
        }
        if (hit == kw.other || hit[-1] == '-') {
            char type[16];
            size_t i = 0;
            hit += 3;
            while (hit[i] && hit[i] != '-' && i + 1u < sizeof(type)) {
                type[i] = hit[i];
                i++;
            }
            type[i] = '\0';
            if (ParseMsKeyword(type, out)) {
                return 1;
            }
        }
        p = hit + 1;
    }
    return 0;
}

static void RefreshSystems(I18nMeasurePrefs *prefs)
{
    I18nMeasurement fromMs;

    if (prefs->systemOverridden) {
        prefs->tempSystem = prefs->system;
        return;
    }

    if (ReadMsFromLocale(&prefs->locale, &fromMs)) {
        prefs->system = fromMs;
        prefs->tempSystem = fromMs;
        return;
    }

    prefs->system = LookupSystemTable(prefs->region, 0);
    if (MprefTempSystemFind(prefs->region)) {
        prefs->tempSystem = LookupSystemTable(prefs->region, 1);
    } else {
        prefs->tempSystem =
            (prefs->system == RoboI18nMeasureUs) ? RoboI18nMeasureUs : RoboI18nMeasureMetric;
    }
}

void I18nMeasurePrefsClear(I18nMeasurePrefs *prefs)
{
    if (!prefs) {
        return;
    }
    memset(prefs, 0, sizeof(*prefs));
}

Result I18nMeasurePrefsInit(I18nMeasurePrefs *prefs,
                                           const I18nLocale *locale)
{
    if (!prefs || !locale) {
        return RoboInvalidArgument;
    }
    memset(prefs, 0, sizeof(*prefs));
    prefs->locale = *locale;
    ResolveRegion(locale, prefs->region, sizeof(prefs->region));
    RefreshSystems(prefs);
    prefs->ready = 1;
    return RoboOk;
}

Result I18nMeasurePrefsSetSystem(I18nMeasurePrefs *prefs,
                                                 I18nMeasurement system)
{
    if (!prefs || !prefs->ready ||
        (unsigned)system > (unsigned)RoboI18nMeasureUk) {
        return RoboInvalidArgument;
    }
    prefs->system = system;
    prefs->systemOverridden = 1;
    prefs->tempSystem = system;
    if (system != RoboI18nMeasureUs && MprefTempSystemFind(prefs->region)) {
        /* Keep temperature category exception only when not forcing US/metric/UK wholesale —
         * forced system applies to temperature too for predictability. */
    }
    return RoboOk;
}

Result I18nMeasurePrefsClearSystem(I18nMeasurePrefs *prefs)
{
    if (!prefs || !prefs->ready) {
        return RoboInvalidArgument;
    }
    prefs->systemOverridden = 0;
    RefreshSystems(prefs);
    return RoboOk;
}

static int UnitMatchesCategory(I18nUnitId unit, I18nMeasureCategory cat)
{
    I18nUnitQuantity q = MakeI18nUnitQuantity(unit);
    switch (cat) {
    case RoboI18nMeasureCatTemperature:
        return q == RoboI18nUnitQTemperature;
    case RoboI18nMeasureCatLength:
        return q == RoboI18nUnitQLength;
    case RoboI18nMeasureCatArea:
        return q == RoboI18nUnitQArea;
    case RoboI18nMeasureCatMass:
        return q == RoboI18nUnitQMass;
    case RoboI18nMeasureCatVolume:
        return q == RoboI18nUnitQVolume;
    case RoboI18nMeasureCatSpeed:
        return q == RoboI18nUnitQSpeed;
    case RoboI18nMeasureCatPressure:
        return q == RoboI18nUnitQPressure;
    case RoboI18nMeasureCatEnergy:
        return q == RoboI18nUnitQEnergy;
    case RoboI18nMeasureCatPower:
        return q == RoboI18nUnitQPower;
    default:
        return 0;
    }
}

Result I18nMeasurePrefsSetUnit(I18nMeasurePrefs *prefs,
                                               I18nMeasureCategory category,
                                               I18nUnitId unit)
{
    if (!prefs || !prefs->ready ||
        (unsigned)category >= (unsigned)RoboI18nMeasureCatCount) {
        return RoboInvalidArgument;
    }
    if ((unsigned)unit >= (unsigned)RoboI18nUnitCount ||
        !UnitMatchesCategory(unit, category)) {
        return RoboInvalidArgument;
    }
    prefs->hasCatOverride[(unsigned)category] = 1;
    prefs->catOverride[(unsigned)category] = unit;
    return RoboOk;
}

Result I18nMeasurePrefsClearUnit(I18nMeasurePrefs *prefs,
                                                 I18nMeasureCategory category)
{
    if (!prefs || !prefs->ready ||
        (unsigned)category >= (unsigned)RoboI18nMeasureCatCount) {
        return RoboInvalidArgument;
    }
    prefs->hasCatOverride[(unsigned)category] = 0;
    prefs->catOverride[(unsigned)category] = (I18nUnitId)0;
    return RoboOk;
}

I18nMeasurement I18nMeasurePrefsSystem(const I18nMeasurePrefs *prefs)
{
    if (!prefs || !prefs->ready) {
        return RoboI18nMeasureMetric;
    }
    return prefs->system;
}

I18nMeasurement I18nMeasurePrefsTemperatureSystem(
    const I18nMeasurePrefs *prefs)
{
    if (!prefs || !prefs->ready) {
        return RoboI18nMeasureMetric;
    }
    return prefs->tempSystem;
}

static I18nUnitId SystemDefaultUnit(I18nMeasureCategory cat,
                                               I18nMeasurement system,
                                               int forTemperature)
{
    I18nMeasurement s = forTemperature ? system : system;
    (void)forTemperature;
    switch (cat) {
    case RoboI18nMeasureCatTemperature:
        return (s == RoboI18nMeasureUs) ? RoboI18nUnitTemperatureFahrenheit
                                           : RoboI18nUnitTemperatureCelsius;
    case RoboI18nMeasureCatLength:
        return (s == RoboI18nMeasureMetric) ? RoboI18nUnitLengthKilometer
                                               : RoboI18nUnitLengthMile;
    case RoboI18nMeasureCatArea:
        return (s == RoboI18nMeasureMetric) ? RoboI18nUnitAreaSquareKilometer
                                               : RoboI18nUnitAreaAcre;
    case RoboI18nMeasureCatMass:
        return (s == RoboI18nMeasureMetric) ? RoboI18nUnitMassKilogram
                                               : RoboI18nUnitMassPound;
    case RoboI18nMeasureCatVolume:
        return (s == RoboI18nMeasureMetric) ? RoboI18nUnitVolumeLiter
                                               : RoboI18nUnitVolumeGallon;
    case RoboI18nMeasureCatSpeed:
        return (s == RoboI18nMeasureMetric) ? RoboI18nUnitSpeedKilometerPerHour
                                               : RoboI18nUnitSpeedMilePerHour;
    case RoboI18nMeasureCatPressure:
        return (s == RoboI18nMeasureMetric) ? RoboI18nUnitPressureHectopascal
                                               : RoboI18nUnitPressurePoundForcePerSquareInch;
    case RoboI18nMeasureCatEnergy:
        return RoboI18nUnitEnergyKilowattHour;
    case RoboI18nMeasureCatPower:
        return (s == RoboI18nMeasureMetric) ? RoboI18nUnitPowerKilowatt
                                               : RoboI18nUnitPowerHorsepower;
    default:
        return RoboI18nUnitLengthMeter;
    }
}

static uint8_t EffectiveUsage(I18nMeasureCategory cat,
                               I18nMeasureUsage usage)
{
    if ((unsigned)usage >= (unsigned)RoboI18nMeasureUsageCount) {
        return (uint8_t)RoboI18nMeasureUsageDefault;
    }
    /* Volume: prefer fluid list when DEFAULT (CLDR default volume is cubic-*). */
    if (cat == RoboI18nMeasureCatVolume && usage == RoboI18nMeasureUsageDefault) {
        return (uint8_t)RoboI18nMeasureUsageFluid;
    }
    return (uint8_t)usage;
}

static const MprefUnits *FindPrefUnits(uint8_t cat,
                                                 uint8_t usage,
                                                 const char *region)
{
    const MprefUnits *row = MprefUnitsFind(cat, usage, region);
    if (row) {
        return row;
    }
    return MprefUnitsFind(cat, usage, "001");
}

Result I18nMeasureResolveUnit(const I18nMeasurePrefs *prefs,
                                             I18nMeasureCategory category,
                                             I18nMeasureUsage usage,
                                             const I18nUnitId *explicitUnit,
                                             I18nUnitId *outUnit,
                                             I18nMeasureSource *outSrc)
{
    uint8_t u;
    const MprefUnits *row;
    I18nMeasurement sys;

    if (!prefs || !prefs->ready || !outUnit ||
        (unsigned)category >= (unsigned)RoboI18nMeasureCatCount) {
        return RoboInvalidArgument;
    }

    if (explicitUnit) {
        if ((unsigned)*explicitUnit >= (unsigned)RoboI18nUnitCount ||
            !UnitMatchesCategory(*explicitUnit, category)) {
            return RoboInvalidArgument;
        }
        *outUnit = *explicitUnit;
        if (outSrc) {
            *outSrc = RoboI18nMeasureSrcExplicit;
        }
        return RoboOk;
    }

    if (prefs->hasCatOverride[(unsigned)category]) {
        *outUnit = prefs->catOverride[(unsigned)category];
        if (outSrc) {
            *outSrc = RoboI18nMeasureSrcCategoryOverride;
        }
        return RoboOk;
    }

    u = EffectiveUsage(category, usage);
    row = FindPrefUnits((uint8_t)category, u, prefs->region);
    if (!row && u != (uint8_t)RoboI18nMeasureUsageDefault) {
        row = FindPrefUnits((uint8_t)category, (uint8_t)RoboI18nMeasureUsageDefault,
                              prefs->region);
    }
    if (row && row->count > 0) {
        *outUnit = (I18nUnitId)row->units[0];
        if (outSrc) {
            *outSrc = RoboI18nMeasureSrcCldrPreference;
        }
        return RoboOk;
    }

    sys = (category == RoboI18nMeasureCatTemperature) ? prefs->tempSystem : prefs->system;
    if (prefs->systemOverridden && outSrc) {
        *outSrc = RoboI18nMeasureSrcSystemOverride;
    } else if (outSrc) {
        *outSrc = RoboI18nMeasureSrcSystemDefault;
    }
    *outUnit = SystemDefaultUnit(category, sys, category == RoboI18nMeasureCatTemperature);
    return RoboOk;
}

Result I18nMeasurePreferredUnit(const I18nMeasurePrefs *prefs,
                                               I18nMeasureCategory category,
                                               I18nUnitId *outUnit)
{
    return I18nMeasureResolveUnit(prefs, category, RoboI18nMeasureUsageDefault, NULL,
                                          outUnit, NULL);
}

Result I18nMeasurePreferredUnitUsage(
    const I18nMeasurePrefs *prefs,
    I18nMeasureCategory category,
    I18nMeasureUsage usage,
    I18nUnitId *outUnit)
{
    return I18nMeasureResolveUnit(prefs, category, usage, NULL, outUnit, NULL);
}

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
    size_t *outLen)
{
    I18nUnitId toUnit;
    I18nDecimal converted;
    int approx = 0;
    Result r;

    if (!prefs || !fmt || !value) {
        return RoboInvalidArgument;
    }
    r = I18nMeasureResolveUnit(prefs, category, usage, explicitUnit, &toUnit, NULL);
    if (r != RoboOk) {
        return r;
    }
    if (fromUnit == toUnit) {
        return I18nUnitFormat(fmt, value, toUnit, buf, bufCap, outLen);
    }
    r = I18nUnitConvert(fromUnit, toUnit, value, &converted, &approx);
    if (r != RoboOk) {
        return r;
    }
    (void)approx;
    /* Prefer exact integer decimals when conversion lands on a whole number. */
    if (converted.kind == RoboI18nDecimalNumber && converted.scale > 0) {
        int fracNonzero = 0;
        size_t i;
        size_t intDigits;
        if (converted.digitCount > converted.scale) {
            intDigits = (size_t)(converted.digitCount - converted.scale);
        } else {
            intDigits = 0;
        }
        for (i = intDigits; i < converted.digitCount; i++) {
            if (converted.digits[i] != '0') {
                fracNonzero = 1;
                break;
            }
        }
        if (!fracNonzero) {
            int64_t iv = 0;
            int neg = converted.negative;
            if (intDigits == 0) {
                iv = 0;
            } else {
                for (i = 0; i < intDigits; i++) {
                    int digit = converted.digits[i] - '0';
                    if (iv > (9223372036854775807LL - digit) / 10) {
                        iv = 0;
                        fracNonzero = 1;
                        break;
                    }
                    iv = iv * 10 + digit;
                }
            }
            if (!fracNonzero) {
                if (neg) {
                    iv = -iv;
                }
                r = I18nDecimalFromI64(iv, &converted);
                if (r != RoboOk) {
                    return r;
                }
            }
        }
    }
    return I18nUnitFormat(fmt, &converted, toUnit, buf, bufCap, outLen);
}

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
    size_t *outLen)
{
    I18nDecimal dec;
    Result r = I18nDecimalFromI64(value, &dec);
    if (r != RoboOk) {
        return r;
    }
    return I18nMeasureFormatConverted(prefs, fmt, category, usage, &dec, fromUnit,
                                              explicitUnit, buf, bufCap, outLen);
}

Result I18nMeasureSystemForLocale(const I18nLocale *locale,
                                                  I18nMeasurement *out)
{
    I18nMeasurePrefs prefs;
    Result r;
    if (!out) {
        return RoboInvalidArgument;
    }
    r = I18nMeasurePrefsInit(&prefs, locale);
    if (r != RoboOk) {
        return r;
    }
    *out = prefs.system;
    return RoboOk;
}

Result I18nMeasureTemperatureSystemForLocale(
    const I18nLocale *locale,
    I18nMeasurement *out)
{
    I18nMeasurePrefs prefs;
    Result r;
    if (!out) {
        return RoboInvalidArgument;
    }
    r = I18nMeasurePrefsInit(&prefs, locale);
    if (r != RoboOk) {
        return r;
    }
    *out = prefs.tempSystem;
    return RoboOk;
}

const char *I18nMeasureCategoryName(I18nMeasureCategory category)
{
    if ((unsigned)category >= RoboMprefCategoryCount) {
        return "";
    }
    return MprefCategoryNames[(unsigned)category];
}

const char *I18nMeasureUsageName(I18nMeasureUsage usage)
{
    if ((unsigned)usage >= RoboMprefUsageCount) {
        return "";
    }
    return MprefUsageNames[(unsigned)usage];
}

const char *I18nMeasureCldrVersionString(void)
{
    return MprefVersionString();
}
