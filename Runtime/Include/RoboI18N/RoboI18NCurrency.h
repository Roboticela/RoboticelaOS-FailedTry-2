/*
 * RoboI18N currency metadata & formatting (Goal 4.21).
 *
 * Currency identity / metadata / precision / cash rounding / formatting are
 * separate from FX conversion (out of scope). Parsing is Goal 4.22.
 *
 * Numeric model: prefer I18nDecimal / integer minor units — not double —
 * for money values. Double helpers exist only as approximate convenience.
 *
 * Precedence for currency code:
 *   1. Explicit code argument
 *   2. Unicode locale extension `cu`
 *   3. Region default from CLDR currencyData
 *
 * Style `cf=account` selects accounting patterns when present.
 *
 * Thread safety: formatters are not synchronized — one owner thread, or lock.
 * Generated tables are immortal/read-only. No global mutable currency state.
 */

#ifndef ROBO_I18N_CURRENCY_H
#define ROBO_I18N_CURRENCY_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NNumber.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Currency display / format style (extensible without renumbering). */
typedef enum I18nCurrencyStyle {
    RoboI18nCurrencyStyleSymbol = 0,
    RoboI18nCurrencyStyleNarrowSymbol = 1,
    RoboI18nCurrencyStyleCode = 2,
    RoboI18nCurrencyStyleName = 3,
    RoboI18nCurrencyStyleAccounting = 4
} I18nCurrencyStyle;

/**
 * Fraction / rounding usage.
 * STANDARD = electronic/accounting digits from CLDR fractions.
 * CASH = cashDigits/cashRounding when present; never applied unless requested.
 */
typedef enum I18nCurrencyUsage {
    RoboI18nCurrencyUsageStandard = 0,
    RoboI18nCurrencyUsageCash = 1
} I18nCurrencyUsage;

typedef struct I18nCurrencyInfo {
    char code[4]; /* NUL-terminated ISO 4217 alpha */
    int16_t numeric; /* -1 if unknown */
    uint8_t digits;
    uint16_t rounding; /* 0 = unit at last fraction digit */
    int8_t cashDigits; /* -1 = same as digits */
    int16_t cashRounding; /* -1 = same as rounding */
    uint8_t flags; /* ROBO_CURR_FLAG_* from RoboCurrencyData.h */
    uint8_t known; /* 1 if present in generated meta tables */
} I18nCurrencyInfo;

typedef struct I18nCurrencyOptions {
    I18nCurrencyStyle style;
    I18nCurrencyUsage usage;
    I18nRounding rounding;
    char numberingSystem[16]; /* empty = locale default */
    char currency[4]; /* empty = resolve from locale cu / region */
} I18nCurrencyOptions;

typedef struct I18nCurrencyFormatter {
    I18nLocale locale;
    I18nCurrencyOptions options;
    I18nNumberFormatter number;
    char resolvedCurrency[4];
    I18nCurrencyInfo info;
    uint8_t accounting; /* 1 if accounting patterns active */
    uint8_t ready;
} I18nCurrencyFormatter;

void I18nCurrencyOptionsInit(I18nCurrencyOptions *opts);

/**
 * Normalize a currency identifier to uppercase ISO 4217 alpha in out[4].
 * Malformed → RoboInvalidArgument. Does not require the code to be known.
 */
Result I18nCurrencyNormalize(const char *code,
                                           size_t codeLen,
                                           char out[4]);

/** 1 if code is well-formed ISO 4217 alphabetic (after normalize rules). */
int I18nCurrencyIsWellFormed(const char *code, size_t codeLen);

/**
 * Lookup metadata. Well-formed unknown codes → RoboNotFound with DEFAULT
 * digits filled and known=0. Malformed → RoboInvalidArgument.
 */
Result I18nCurrencyLookup(const char *code,
                                        size_t codeLen,
                                        I18nCurrencyInfo *out);

/** Region → current tender currency (CLDR). Unknown region → RoboNotFound. */
Result I18nCurrencyForRegion(const char *region,
                                            size_t regionLen,
                                            char out[4]);

/**
 * Locale default currency: explicit `cu` keyword, else maximized region map.
 */
Result I18nCurrencyForLocale(const I18nLocale *locale,
                                            char out[4]);

Result I18nCurrencyFormatterInit(
    I18nCurrencyFormatter *fmt,
    const I18nLocale *locale,
    const I18nCurrencyOptions *opts);

void I18nCurrencyFormatterDestroy(I18nCurrencyFormatter *fmt);

/** Effective ISO code for this formatter. */
const char *I18nCurrencyFormatterCode(
    const I18nCurrencyFormatter *fmt);

const I18nCurrencyInfo *I18nCurrencyFormatterInfo(
    const I18nCurrencyFormatter *fmt);

/**
 * Format money value. Prefer decimal / i64 minor-unit helpers for exactness.
 * Buffer contract matches RoboI18NNumber.
 */
Result I18nCurrencyFormat(const I18nCurrencyFormatter *fmt,
                                        const I18nDecimal *value,
                                        char *buf,
                                        size_t bufCap,
                                        size_t *outLen);

Result I18nCurrencyFormatI64(const I18nCurrencyFormatter *fmt,
                                            int64_t majorUnits,
                                            char *buf,
                                            size_t bufCap,
                                            size_t *outLen);

/**
 * Format an integer amount in minor units (e.g. cents for USD digits=2).
 * major = minor / 10^digits (exact).
 */
Result I18nCurrencyFormatMinor(const I18nCurrencyFormatter *fmt,
                                              int64_t minorUnits,
                                              char *buf,
                                              size_t bufCap,
                                              size_t *outLen);

Result I18nCurrencyFormatDouble(const I18nCurrencyFormatter *fmt,
                                               double value,
                                               char *buf,
                                               size_t bufCap,
                                               size_t *outLen);

Result I18nCurrencyFormatToString(
    const I18nCurrencyFormatter *fmt,
    const I18nDecimal *value,
    String *out);

const char *I18nCurrencyCldrVersionString(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_CURRENCY_H */
