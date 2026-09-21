/*
 * RoboI18N unit formatter + conversion (Goal 4.19).
 */

#include "RoboI18N/RoboI18NUnit.h"

#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboI18N/RoboUnitData.h"

#include <string.h>

void I18nUnitOptionsInit(I18nUnitOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->width = RoboI18nUnitWidthLong;
}

Result I18nUnitFormatterInit(I18nUnitFormatter *fmt,
                                            const I18nLocale *locale,
                                            const I18nUnitOptions *opts)
{
    I18nNumberOptions nopts;
    Result r;

    if (!fmt || !locale) {
        return RoboInvalidArgument;
    }
    memset(fmt, 0, sizeof(*fmt));
    fmt->locale = *locale;
    if (opts) {
        fmt->options = *opts;
    } else {
        I18nUnitOptionsInit(&fmt->options);
    }
    if ((unsigned)fmt->options.width > (unsigned)RoboI18nUnitWidthNarrow) {
        return RoboInvalidArgument;
    }

    I18nNumberOptionsInit(&nopts);
    nopts.useGrouping = 1;
    nopts.minIntegerDigits = 1;
    nopts.maxFractionDigits = -1;
    if (fmt->options.numberingSystem[0]) {
        size_t n = strlen(fmt->options.numberingSystem);
        if (n >= sizeof(nopts.numberingSystem)) {
            return RoboInvalidArgument;
        }
        memcpy(nopts.numberingSystem, fmt->options.numberingSystem, n + 1u);
    }
    r = I18nNumberFormatterInit(&fmt->number, &fmt->locale, &nopts);
    if (r != RoboOk) {
        return r;
    }
    fmt->ready = 1;
    return RoboOk;
}

void I18nUnitFormatterDestroy(I18nUnitFormatter *fmt)
{
    if (!fmt) {
        return;
    }
    I18nNumberFormatterDestroy(&fmt->number);
    memset(fmt, 0, sizeof(*fmt));
}

const char *I18nUnitCldrKey(I18nUnitId unit)
{
    return UnitCldrKey((uint16_t)unit);
}

Result I18nUnitIdFromCldrKey(const char *key, I18nUnitId *out)
{
    size_t i;
    if (!key || !out) {
        return RoboInvalidArgument;
    }
    for (i = 0; i < (size_t)RoboI18nUnitCount; i++) {
        const char *k = UnitCldrKeys[i];
        if (k && strcmp(k, key) == 0) {
            *out = (I18nUnitId)i;
            return RoboOk;
        }
    }
    return RoboNotFound;
}

I18nUnitQuantity MakeI18nUnitQuantity(I18nUnitId unit)
{
    if ((unsigned)unit >= (unsigned)RoboI18nUnitCount) {
        return RoboI18nUnitQUnknown;
    }
    if (unit <= RoboI18nUnitLengthInch) {
        return RoboI18nUnitQLength;
    }
    if (unit <= RoboI18nUnitMassOunce) {
        return RoboI18nUnitQMass;
    }
    if (unit <= RoboI18nUnitAreaAcre) {
        return RoboI18nUnitQArea;
    }
    if (unit <= RoboI18nUnitVolumeCubicMeter) {
        return RoboI18nUnitQVolume;
    }
    if (unit <= RoboI18nUnitDurationMillisecond) {
        return RoboI18nUnitQDuration;
    }
    if (unit <= RoboI18nUnitSpeedMilePerHour) {
        return RoboI18nUnitQSpeed;
    }
    if (unit <= RoboI18nUnitTemperatureKelvin) {
        return RoboI18nUnitQTemperature;
    }
    if (unit <= RoboI18nUnitDigitalTerabyte) {
        return RoboI18nUnitQDigital;
    }
    if (unit <= RoboI18nUnitEnergyFoodcalorie) {
        return RoboI18nUnitQEnergy;
    }
    if (unit <= RoboI18nUnitPowerHorsepower) {
        return RoboI18nUnitQPower;
    }
    if (unit <= RoboI18nUnitPressurePoundForcePerSquareInch) {
        return RoboI18nUnitQPressure;
    }
    if (unit <= RoboI18nUnitFrequencyGigahertz) {
        return RoboI18nUnitQFrequency;
    }
    if (unit <= RoboI18nUnitAngleArcSecond) {
        return RoboI18nUnitQAngle;
    }
    if (unit == RoboI18nUnitConsumptionLiterPer100Kilometer) {
        return RoboI18nUnitQConsumption;
    }
    return RoboI18nUnitQUnknown;
}

static void FormatLocaleTag(const I18nLocale *loc, char *buf, size_t cap)
{
    size_t n = 0;
    size_t ln;
    if (!loc || !buf || cap == 0) {
        return;
    }
    ln = strlen(loc->language);
    if (ln >= cap) {
        ln = cap - 1u;
    }
    memcpy(buf, loc->language, ln);
    n = ln;
    buf[n] = '\0';
    if (loc->script[0] && n + 5u < cap) {
        buf[n++] = '-';
        memcpy(buf + n, loc->script, 4);
        n += 4;
        buf[n] = '\0';
    }
    if (loc->region[0] && n + 1u + strlen(loc->region) < cap) {
        buf[n++] = '-';
        memcpy(buf + n, loc->region, strlen(loc->region));
        n += strlen(loc->region);
        buf[n] = '\0';
    }
}

static const UnitRow *UnitResolve(const I18nLocale *loc,
                                           uint16_t unit,
                                           uint8_t width)
{
    I18nLocale cur;
    int guard = 0;
    char tag[64];
    const UnitRow *row;

    if (!loc || (unsigned)unit >= (unsigned)RoboI18nUnitCount) {
        return NULL;
    }
    cur = *loc;
    while (guard++ < 16) {
        FormatLocaleTag(&cur, tag, sizeof(tag));
        row = UnitRowFind(tag, unit, width);
        if (row) {
            return row;
        }
        row = UnitRowFind(cur.language, unit, width);
        if (row) {
            return row;
        }
        {
            I18nLocale parent;
            if (I18nLocaleDataParent(&cur, &parent) != RoboOk) {
                break;
            }
            cur = parent;
        }
    }
    return UnitRowFind("en", unit, width);
}

static const UnitCompound *CompoundResolve(const I18nLocale *loc, uint8_t width)
{
    I18nLocale cur;
    int guard = 0;
    char tag[64];
    const UnitCompound *row;

    if (!loc) {
        return NULL;
    }
    cur = *loc;
    while (guard++ < 16) {
        FormatLocaleTag(&cur, tag, sizeof(tag));
        row = UnitCompoundFind(tag, width);
        if (row) {
            return row;
        }
        row = UnitCompoundFind(cur.language, width);
        if (row) {
            return row;
        }
        {
            I18nLocale parent;
            if (I18nLocaleDataParent(&cur, &parent) != RoboOk) {
                break;
            }
            cur = parent;
        }
    }
    return UnitCompoundFind("en", width);
}

int I18nUnitHasPatterns(const I18nLocale *locale,
                                I18nUnitId unit,
                                I18nUnitWidth width)
{
    if (!locale || (unsigned)unit >= (unsigned)RoboI18nUnitCount ||
        (unsigned)width > (unsigned)RoboI18nUnitWidthNarrow) {
        return 0;
    }
    return UnitResolve(locale, (uint16_t)unit, (uint8_t)width) != NULL;
}

static Result AppendBytes(char *buf,
                                  size_t bufCap,
                                  size_t *pos,
                                  const char *bytes,
                                  size_t len,
                                  int writing)
{
    if (!pos) {
        return RoboInvalidArgument;
    }
    if (writing) {
        if (!buf || bufCap == 0) {
            return RoboInvalidArgument;
        }
        if (*pos + len + 1u > bufCap) {
            return RoboError;
        }
        memcpy(buf + *pos, bytes, len);
    }
    *pos += len;
    return RoboOk;
}

static const char *PatternForCategory(const int32_t pats[6],
                                        I18nPluralCategory cat)
{
    int32_t idx;
    if ((unsigned)cat > 5u) {
        cat = RoboI18nPluralOther;
    }
    idx = pats[(unsigned)cat];
    if (idx < 0) {
        idx = pats[RoboI18nPluralOther];
    }
    if (idx < 0) {
        return NULL;
    }
    return UnitStr(idx);
}

static Result ExpandUnitPattern(const I18nUnitFormatter *fmt,
                                         const char *pattern,
                                         const I18nDecimal *value,
                                         char *buf,
                                         size_t bufCap,
                                         size_t *outLen)
{
    size_t pos = 0;
    size_t i = 0;
    size_t plen;
    int writing = (buf != NULL && bufCap > 0);
    Result r;

    if (!pattern || !value || !outLen) {
        return RoboInvalidArgument;
    }
    plen = strlen(pattern);
    while (i < plen) {
        if (pattern[i] == '{' && i + 2u < plen && pattern[i + 1u] == '0' &&
            pattern[i + 2u] == '}') {
            char num[96];
            size_t nlen = 0;
            r = I18nNumberFormat(&fmt->number, value, num, sizeof(num), &nlen);
            if (r != RoboOk) {
                return r;
            }
            r = AppendBytes(buf, bufCap, &pos, num, nlen, writing);
            if (r != RoboOk) {
                *outLen = pos;
                return r;
            }
            i += 3u;
            continue;
        }
        r = AppendBytes(buf, bufCap, &pos, pattern + i, 1u, writing);
        if (r != RoboOk) {
            *outLen = pos;
            return r;
        }
        i++;
    }
    *outLen = pos;
    if (writing) {
        if (pos + 1u > bufCap) {
            return RoboError;
        }
        buf[pos] = '\0';
    }
    return RoboOk;
}

static Result ExpandSlot0(const char *pattern,
                                  const char *payload,
                                  size_t payloadLen,
                                  char *buf,
                                  size_t bufCap,
                                  size_t *outLen)
{
    size_t pos = 0;
    size_t i = 0;
    size_t plen;
    int writing = (buf != NULL && bufCap > 0);
    Result r;

    if (!pattern || !payload || !outLen) {
        return RoboInvalidArgument;
    }
    plen = strlen(pattern);
    while (i < plen) {
        if (pattern[i] == '{' && i + 2u < plen && pattern[i + 1u] == '0' &&
            pattern[i + 2u] == '}') {
            r = AppendBytes(buf, bufCap, &pos, payload, payloadLen, writing);
            if (r != RoboOk) {
                *outLen = pos;
                return r;
            }
            i += 3u;
            continue;
        }
        r = AppendBytes(buf, bufCap, &pos, pattern + i, 1u, writing);
        if (r != RoboOk) {
            *outLen = pos;
            return r;
        }
        i++;
    }
    *outLen = pos;
    if (writing) {
        if (pos + 1u > bufCap) {
            return RoboError;
        }
        buf[pos] = '\0';
    }
    return RoboOk;
}

static Result ExpandCompoundPer(const char *pattern,
                                         const char *a,
                                         size_t aLen,
                                         const char *b,
                                         size_t bLen,
                                         char *buf,
                                         size_t bufCap,
                                         size_t *outLen)
{
    size_t pos = 0;
    size_t i = 0;
    size_t plen;
    int writing = (buf != NULL && bufCap > 0);
    Result r;

    if (!pattern || !a || !b || !outLen) {
        return RoboInvalidArgument;
    }
    plen = strlen(pattern);
    while (i < plen) {
        if (pattern[i] == '{' && i + 2u < plen && pattern[i + 2u] == '}') {
            if (pattern[i + 1u] == '0') {
                r = AppendBytes(buf, bufCap, &pos, a, aLen, writing);
                if (r != RoboOk) {
                    *outLen = pos;
                    return r;
                }
                i += 3u;
                continue;
            }
            if (pattern[i + 1u] == '1') {
                r = AppendBytes(buf, bufCap, &pos, b, bLen, writing);
                if (r != RoboOk) {
                    *outLen = pos;
                    return r;
                }
                i += 3u;
                continue;
            }
        }
        r = AppendBytes(buf, bufCap, &pos, pattern + i, 1u, writing);
        if (r != RoboOk) {
            *outLen = pos;
            return r;
        }
        i++;
    }
    *outLen = pos;
    if (writing) {
        if (pos + 1u > bufCap) {
            return RoboError;
        }
        buf[pos] = '\0';
    }
    return RoboOk;
}

Result I18nUnitFormat(const I18nUnitFormatter *fmt,
                                    const I18nDecimal *value,
                                    I18nUnitId unit,
                                    char *buf,
                                    size_t bufCap,
                                    size_t *outLen)
{
    const UnitRow *row;
    I18nPluralCategory cat = RoboI18nPluralOther;
    const char *pattern;
    Result r;

    if (!fmt || !fmt->ready || !value || !outLen) {
        return RoboInvalidArgument;
    }
    if ((unsigned)unit >= (unsigned)RoboI18nUnitCount) {
        return RoboNotSupported;
    }

    row = UnitResolve(&fmt->locale, (uint16_t)unit, (uint8_t)fmt->options.width);
    if (!row) {
        return RoboNotFound;
    }

    /* Plural category follows the locale that owns the patterns (after fallback). */
    {
        const char *patLoc = UnitStr(row->locale);
        I18nLocale ploc;
        if (patLoc && patLoc[0] &&
            I18nLocaleParseCanonical(patLoc, strlen(patLoc), &ploc) == RoboOk) {
            r = I18nPluralCardinal(&ploc, value, &cat);
        } else {
            r = I18nPluralCardinal(&fmt->locale, value, &cat);
        }
    }
    if (r != RoboOk) {
        return r;
    }
    pattern = PatternForCategory(row->patterns, cat);
    if (!pattern) {
        return RoboNotFound;
    }
    return ExpandUnitPattern(fmt, pattern, value, buf, bufCap, outLen);
}

Result I18nUnitFormatI64(const I18nUnitFormatter *fmt,
                                        int64_t value,
                                        I18nUnitId unit,
                                        char *buf,
                                        size_t bufCap,
                                        size_t *outLen)
{
    I18nDecimal dec;
    Result r = I18nDecimalFromI64(value, &dec);
    if (r != RoboOk) {
        return r;
    }
    return I18nUnitFormat(fmt, &dec, unit, buf, bufCap, outLen);
}

Result I18nUnitFormatDouble(const I18nUnitFormatter *fmt,
                                           double value,
                                           I18nUnitId unit,
                                           char *buf,
                                           size_t bufCap,
                                           size_t *outLen)
{
    I18nDecimal dec;
    Result r = I18nDecimalFromDouble(value, &dec);
    if (r != RoboOk) {
        return r;
    }
    return I18nUnitFormat(fmt, &dec, unit, buf, bufCap, outLen);
}

Result I18nUnitFormatPer(const I18nUnitFormatter *fmt,
                                        const I18nDecimal *value,
                                        I18nUnitId unitNum,
                                        I18nUnitId unitDen,
                                        char *buf,
                                        size_t bufCap,
                                        size_t *outLen)
{
    char primary[192];
    size_t plen = 0;
    const UnitRow *denRow;
    const UnitCompound *comp;
    Result r;

    if (!fmt || !fmt->ready || !value || !outLen) {
        return RoboInvalidArgument;
    }
    if ((unsigned)unitNum >= (unsigned)RoboI18nUnitCount ||
        (unsigned)unitDen >= (unsigned)RoboI18nUnitCount) {
        return RoboNotSupported;
    }

    r = I18nUnitFormat(fmt, value, unitNum, primary, sizeof(primary), &plen);
    if (r != RoboOk) {
        return r;
    }

    denRow = UnitResolve(&fmt->locale, (uint16_t)unitDen, (uint8_t)fmt->options.width);
    if (!denRow) {
        return RoboNotFound;
    }

    if (denRow->perUnit >= 0) {
        const char *perPat = UnitStr(denRow->perUnit);
        return ExpandSlot0(perPat, primary, plen, buf, bufCap, outLen);
    }

    comp = CompoundResolve(&fmt->locale, (uint8_t)fmt->options.width);
    if (!comp || comp->perPattern < 0 || denRow->displayName < 0) {
        return RoboNotFound;
    }
    {
        const char *per = UnitStr(comp->perPattern);
        const char *dn = UnitStr(denRow->displayName);
        return ExpandCompoundPer(per, primary, plen, dn, strlen(dn), buf, bufCap, outLen);
    }
}

Result I18nUnitFormatPerI64(const I18nUnitFormatter *fmt,
                                            int64_t value,
                                            I18nUnitId unitNum,
                                            I18nUnitId unitDen,
                                            char *buf,
                                            size_t bufCap,
                                            size_t *outLen)
{
    I18nDecimal dec;
    Result r = I18nDecimalFromI64(value, &dec);
    if (r != RoboOk) {
        return r;
    }
    return I18nUnitFormatPer(fmt, &dec, unitNum, unitDen, buf, bufCap, outLen);
}

Result I18nUnitFormatToString(const I18nUnitFormatter *fmt,
                                              const I18nDecimal *value,
                                              I18nUnitId unit,
                                              String *out)
{
    char stack[256];
    size_t len = 0;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = I18nUnitFormat(fmt, value, unit, stack, sizeof(stack), &len);
    if (r == RoboOk) {
        return StringAppend(out, stack, len);
    }
    return r;
}

/* ---- conversion (SI factors; separate from localization) ---- */

typedef struct unitFactor {
    I18nUnitId unit;
    /* value_si = value * num / den  (SI base for quantity) */
    int64_t num;
    int64_t den;
    uint8_t exact; /* 1 = exact rational; 0 = approximate customary */
} UnitFactor;

/*
 * Length SI = meter. Exact metric; mile/yard/foot/inch approximate (intl yard).
 * Mass SI = kilogram. Exact metric; pound/ounce approximate (avoirdupois).
 * Area SI = square meter. hectare exact; acre approximate.
 * Volume SI = cubic meter. liter/mL exact; US liquid gallon approximate.
 * Duration SI = second. year/month approximate (Gregorian mean); week/day exact.
 * Speed SI = m/s. km/h exact; mph approximate.
 * Digital SI = bit. CLDR digital-* uses decimal 1000 prefixes (exact).
 * Energy SI = joule. kWh exact; calorie/foodcalorie approximate.
 * Power SI = watt. kW exact; horsepower approximate (mechanical).
 * Pressure SI = pascal. hPa/mbar exact; psi approximate.
 * Frequency SI = hertz. metric prefixes exact.
 * Angle SI = radian. degree/arcmin/arcsec approximate (pi).
 */

static const UnitFactor kFactors[] = {
    {RoboI18nUnitLengthMillimeter, 1, 1000, 1},
    {RoboI18nUnitLengthCentimeter, 1, 100, 1},
    {RoboI18nUnitLengthMeter, 1, 1, 1},
    {RoboI18nUnitLengthKilometer, 1000, 1, 1},
    {RoboI18nUnitLengthInch, 254, 10000, 0}, /* 0.0254 m */
    {RoboI18nUnitLengthFoot, 3048, 10000, 0},
    {RoboI18nUnitLengthYard, 9144, 10000, 0},
    {RoboI18nUnitLengthMile, 1609344, 1000, 0}, /* 1609.344 m */

    {RoboI18nUnitMassGram, 1, 1000, 1},
    {RoboI18nUnitMassKilogram, 1, 1, 1},
    {RoboI18nUnitMassOunce, 28349523125LL, 1000000000000LL, 0}, /* ~0.0283495 */
    {RoboI18nUnitMassPound, 45359237, 100000000, 0}, /* 0.45359237 */

    {RoboI18nUnitAreaSquareMeter, 1, 1, 1},
    {RoboI18nUnitAreaSquareKilometer, 1000000, 1, 1},
    {RoboI18nUnitAreaHectare, 10000, 1, 1},
    {RoboI18nUnitAreaAcre, 40468564224LL, 10000000LL, 0}, /* 4046.8564224 */

    {RoboI18nUnitVolumeMilliliter, 1, 1000000, 1},
    {RoboI18nUnitVolumeLiter, 1, 1000, 1},
    {RoboI18nUnitVolumeCubicMeter, 1, 1, 1},
    {RoboI18nUnitVolumeGallon, 3785411784LL, 1000000000000LL, 0}, /* US liq */

    {RoboI18nUnitDurationMillisecond, 1, 1000, 1},
    {RoboI18nUnitDurationSecond, 1, 1, 1},
    {RoboI18nUnitDurationMinute, 60, 1, 1},
    {RoboI18nUnitDurationHour, 3600, 1, 1},
    {RoboI18nUnitDurationDay, 86400, 1, 1},
    {RoboI18nUnitDurationWeek, 604800, 1, 1},
    {RoboI18nUnitDurationMonth, 2629746, 1, 0}, /* mean Gregorian month */
    {RoboI18nUnitDurationYear, 31556952, 1, 0}, /* mean Gregorian year */

    {RoboI18nUnitSpeedMeterPerSecond, 1, 1, 1},
    {RoboI18nUnitSpeedKilometerPerHour, 5, 18, 1}, /* /3.6 */
    {RoboI18nUnitSpeedMilePerHour, 1609344, 3600000, 0},

    {RoboI18nUnitDigitalBit, 1, 1, 1},
    {RoboI18nUnitDigitalByte, 8, 1, 1},
    {RoboI18nUnitDigitalKilobyte, 8000, 1, 1},
    {RoboI18nUnitDigitalMegabyte, 8000000, 1, 1},
    {RoboI18nUnitDigitalGigabyte, 8000000000LL, 1, 1},
    {RoboI18nUnitDigitalTerabyte, 8000000000000LL, 1, 1},

    {RoboI18nUnitEnergyJoule, 1, 1, 1},
    {RoboI18nUnitEnergyKilowattHour, 3600000, 1, 1},
    {RoboI18nUnitEnergyCalorie, 4184, 1000, 0}, /* thermochemical */
    {RoboI18nUnitEnergyFoodcalorie, 4184, 1, 0},

    {RoboI18nUnitPowerWatt, 1, 1, 1},
    {RoboI18nUnitPowerKilowatt, 1000, 1, 1},
    {RoboI18nUnitPowerHorsepower, 7457, 10, 0}, /* 745.7 W */

    {RoboI18nUnitPressurePascal, 1, 1, 1},
    {RoboI18nUnitPressureHectopascal, 100, 1, 1},
    {RoboI18nUnitPressureMillibar, 100, 1, 1},
    {RoboI18nUnitPressurePoundForcePerSquareInch, 6894757, 1000, 0},

    {RoboI18nUnitFrequencyHertz, 1, 1, 1},
    {RoboI18nUnitFrequencyKilohertz, 1000, 1, 1},
    {RoboI18nUnitFrequencyMegahertz, 1000000, 1, 1},
    {RoboI18nUnitFrequencyGigahertz, 1000000000, 1, 1},

    {RoboI18nUnitAngleRadian, 1, 1, 1},
    {RoboI18nUnitAngleDegree, 174532925, 10000000000LL, 0}, /* pi/180 */
    {RoboI18nUnitAngleArcMinute, 290888208, 1000000000000LL, 0},
    {RoboI18nUnitAngleArcSecond, 48481368, 10000000000000LL, 0},
};

static const UnitFactor *FindFactor(I18nUnitId unit)
{
    size_t i;
    for (i = 0; i < sizeof(kFactors) / sizeof(kFactors[0]); i++) {
        if (kFactors[i].unit == unit) {
            return &kFactors[i];
        }
    }
    return NULL;
}

static double DecimalToDouble(const I18nDecimal *d)
{
    double v = 0.0;
    size_t i;
    if (!d || d->kind != RoboI18nDecimalNumber || d->digitCount == 0) {
        return 0.0;
    }
    for (i = 0; i < d->digitCount; i++) {
        v = v * 10.0 + (double)(d->digits[i] - '0');
    }
    for (i = 0; i < d->scale; i++) {
        v /= 10.0;
    }
    if (d->negative) {
        v = -v;
    }
    return v;
}

static Result ConvertTemperature(I18nUnitId from,
                                         I18nUnitId to,
                                         const I18nDecimal *in,
                                         I18nDecimal *out,
                                         int *approximateOut)
{
    double v = DecimalToDouble(in);
    double k;

    if (approximateOut) {
        *approximateOut = 1;
    }
    /* Normalize to Kelvin. */
    if (from == RoboI18nUnitTemperatureKelvin) {
        k = v;
    } else if (from == RoboI18nUnitTemperatureCelsius) {
        k = v + 273.15;
    } else if (from == RoboI18nUnitTemperatureFahrenheit) {
        k = (v - 32.0) * 5.0 / 9.0 + 273.15;
    } else {
        return RoboNotSupported;
    }
    if (to == RoboI18nUnitTemperatureKelvin) {
        return I18nDecimalFromDouble(k, out);
    }
    if (to == RoboI18nUnitTemperatureCelsius) {
        return I18nDecimalFromDouble(k - 273.15, out);
    }
    if (to == RoboI18nUnitTemperatureFahrenheit) {
        return I18nDecimalFromDouble((k - 273.15) * 9.0 / 5.0 + 32.0, out);
    }
    return RoboNotSupported;
}

Result I18nUnitConvert(I18nUnitId from,
                                     I18nUnitId to,
                                     const I18nDecimal *in,
                                     I18nDecimal *out,
                                     int *approximateOut)
{
    I18nUnitQuantity qf;
    I18nUnitQuantity qt;
    const UnitFactor *ff;
    const UnitFactor *ft;
    double v;
    double si;
    double result;

    if (!in || !out) {
        return RoboInvalidArgument;
    }
    if (approximateOut) {
        *approximateOut = 0;
    }
    if (from == to) {
        *out = *in;
        return RoboOk;
    }

    qf = MakeI18nUnitQuantity(from);
    qt = MakeI18nUnitQuantity(to);
    if (qf == RoboI18nUnitQUnknown || qf != qt) {
        return RoboNotSupported;
    }
    if (qf == RoboI18nUnitQTemperature) {
        return ConvertTemperature(from, to, in, out, approximateOut);
    }
    if (qf == RoboI18nUnitQConsumption) {
        return RoboNotSupported;
    }

    ff = FindFactor(from);
    ft = FindFactor(to);
    if (!ff || !ft) {
        return RoboNotSupported;
    }
    if (!ff->exact || !ft->exact) {
        if (approximateOut) {
            *approximateOut = 1;
        }
    }

    v = DecimalToDouble(in);
    si = v * (double)ff->num / (double)ff->den;
    result = si * (double)ft->den / (double)ft->num;
    return I18nDecimalFromDouble(result, out);
}

Result I18nUnitConvertI64(I18nUnitId from,
                                         I18nUnitId to,
                                         int64_t value,
                                         int64_t *out,
                                         int *approximateOut)
{
    I18nDecimal in;
    I18nDecimal dec;
    Result r;
    double d;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = I18nDecimalFromI64(value, &in);
    if (r != RoboOk) {
        return r;
    }
    r = I18nUnitConvert(from, to, &in, &dec, approximateOut);
    if (r != RoboOk) {
        return r;
    }
    d = DecimalToDouble(&dec);
    if (d > 9223372036854775807.0 || d < -9223372036854775807.0) {
        return RoboError;
    }
    *out = (int64_t)(d >= 0.0 ? d + 0.5 : d - 0.5);
    return RoboOk;
}

const char *I18nUnitCldrVersionString(void)
{
    return UnitVersionString();
}
