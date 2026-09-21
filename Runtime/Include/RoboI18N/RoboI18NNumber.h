/*
 * RoboI18N number formatting foundation (Goal 4.3).
 *
 * Locale-aware decimal formatting using CLDR symbols, grouping patterns,
 * and numbering-system digits. Not currency / percent / compact / scientific.
 *
 * Numeric model:
 *   - Exact: int64 / uint64 / I18nDecimal (ASCII digit payload)
 *   - Approximate: IEEE-754 binary64 via I18nNumberFormatDouble
 *     (documented specials + conversion limits; not exact decimal).
 *
 * Thread safety: a formatter is not synchronized. Treat as owned by one
 * thread, or protect externally. Configuration is fixed after init.
 */

#ifndef ROBO_I18N_NUMBER_H
#define ROBO_I18N_NUMBER_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NLocale.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboI18nDecimalDigitsMax 40u

typedef enum I18nRounding {
    RoboI18nRoundHalfEven = 0, /* banker's rounding (default) */
    RoboI18nRoundHalfUp = 1,   /* away from zero on ties */
    RoboI18nRoundHalfDown = 2, /* toward zero on ties */
    RoboI18nRoundDown = 3,      /* toward zero */
    RoboI18nRoundUp = 4,        /* away from zero */
    RoboI18nRoundCeiling = 5,   /* toward +inf */
    RoboI18nRoundFloor = 6      /* toward -inf */
} I18nRounding;

typedef enum I18nDecimalKind {
    RoboI18nDecimalNumber = 0,
    RoboI18nDecimalNan = 1,
    RoboI18nDecimalPosInfinity = 2,
    RoboI18nDecimalNegInfinity = 3
} I18nDecimalKind;

/**
 * Exact decimal payload for formatting.
 * digits[] holds ASCII '0'..'9' only, most-significant first.
 * scale = number of fraction digits (trailing digits are fractional).
 * Example: value -12.34 → negative=1, digitCount=4, digits="1234", scale=2.
 * Zero: digitCount=1, digits="0", scale=0, negative=0 (or 1 for -0).
 */
typedef struct I18nDecimal {
    I18nDecimalKind kind;
    uint8_t negative; /* 1 if sign is negative (incl. -0) */
    uint8_t scale;
    uint8_t digitCount;
    char digits[RoboI18nDecimalDigitsMax];
} I18nDecimal;

typedef struct I18nNumberOptions {
    int32_t minIntegerDigits;   /* default 1; clamped 1..40 */
    int32_t minFractionDigits;  /* default 0 */
    int32_t maxFractionDigits;  /* default -1 = use CLDR pattern max (often 3) */
    int useGrouping;             /* default 1 */
    int showPlus;                /* default 0 */
    I18nRounding rounding;/* default HALF_EVEN */
    char numberingSystem[16];    /* empty = locale default; else explicit id */
} I18nNumberOptions;

/**
 * Reusable formatter. Stack-allocatable. No heap of its own.
 * Resolved symbol pointers refer to immortal generated CLDR string pool.
 */
typedef struct I18nNumberFormatter {
    I18nLocale locale;
    I18nNumberOptions options;
    char numberingSystem[16];
    const char *decimalSep;
    const char *groupSep;
    const char *plusSign;
    const char *minusSign;
    const char *nanSymbol;
    const char *infinitySymbol;
    const char *digitGlyphs; /* UTF-8 concat of 10 digits; empty if unavailable */
    uint8_t minGrouping;
    uint8_t primaryGroup;
    uint8_t secondaryGroup;
    uint8_t patternMaxFraction;
    uint8_t ready;
} I18nNumberFormatter;

void I18nNumberOptionsInit(I18nNumberOptions *opts);

Result I18nNumberFormatterInit(I18nNumberFormatter *fmt,
                                              const I18nLocale *locale,
                                              const I18nNumberOptions *opts);

/** Clears the formatter; no heap to free. Safe on NULL. */
void I18nNumberFormatterDestroy(I18nNumberFormatter *fmt);

Result I18nDecimalFromI64(int64_t value, I18nDecimal *out);
Result I18nDecimalFromU64(uint64_t value, I18nDecimal *out);

/**
 * Convert IEEE-754 binary64 into a decimal for formatting.
 * Specials → NAN / ±INFINITY kinds.
 * Finite conversion is best-effort for |value| < 1e18 with up to 15 fraction
 * digits considered before options rounding; larger magnitudes may return
 * RoboNotSupported. Negative zero is preserved.
 */
Result I18nDecimalFromDouble(double value, I18nDecimal *out);

/**
 * Format into caller buffer (UTF-8, always NUL-terminated on success when
 * bufCap > 0). If buf is NULL or bufCap == 0, only computes *outLen.
 * Returns RoboError when bufCap is too small (outLen still set).
 */
Result I18nNumberFormat(const I18nNumberFormatter *fmt,
                                      const I18nDecimal *value,
                                      char *buf,
                                      size_t bufCap,
                                      size_t *outLen);

Result I18nNumberFormatI64(const I18nNumberFormatter *fmt,
                                          int64_t value,
                                          char *buf,
                                          size_t bufCap,
                                          size_t *outLen);

Result I18nNumberFormatU64(const I18nNumberFormatter *fmt,
                                          uint64_t value,
                                          char *buf,
                                          size_t bufCap,
                                          size_t *outLen);

Result I18nNumberFormatDouble(const I18nNumberFormatter *fmt,
                                             double value,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen);

/** Append formatted number onto an existing RoboString. */
Result I18nNumberFormatToString(const I18nNumberFormatter *fmt,
                                                const I18nDecimal *value,
                                                String *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_NUMBER_H */
