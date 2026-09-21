/*
 * RoboI18N number & currency parsing (Goal 4.22).
 *
 * Formatting (Goals 4.3 / 4.21) and parsing are separate APIs.
 * Validation is a by-product of parse modes; FX conversion is out of scope.
 *
 * Modes:
 *   STRICT     — reject malformed grouping, unexpected chars, trailing junk,
 *                and ambiguous currency symbols without an explicit constraint.
 *   PERMISSIVE — accept missing grouping and irregular group sizes; may resolve
 *                ambiguous symbols to the locale default currency when that
 *                default is among the matches. Still deterministic.
 *
 * Numeric payload is I18nDecimal (ASCII digits + scale) — not double —
 * so "1" / "1.0" / "1.00" remain distinguishable via scale.
 *
 * Error positions are UTF-8 byte offsets into the input span.
 *
 * Thread safety: parsers are not synchronized — one owner thread, or lock.
 * No global mutable parse state. CLDR/display tables are immortal/read-only.
 */

#ifndef ROBO_I18N_PARSE_H
#define ROBO_I18N_PARSE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NCurrency.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NNumber.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hard limit on input span length (bytes). Longer → RoboInvalidArgument. */
#define RoboI18nParseInputMax 4096u

/** Max ambiguous currency codes reported in a result. */
#define RoboI18nParseAmbigMax 8u

typedef enum I18nParseMode {
    RoboI18nParseStrict = 0,
    RoboI18nParsePermissive = 1
} I18nParseMode;

/**
 * Detailed parse failure category (orthogonal to Result).
 * On success, category is RoboI18nParseErrNone.
 */
typedef enum I18nParseErr {
    RoboI18nParseErrNone = 0,
    RoboI18nParseErrEmpty = 1,
    RoboI18nParseErrInvalidUtf8 = 2,
    RoboI18nParseErrInvalidDigit = 3,
    RoboI18nParseErrInvalidGrouping = 4,
    RoboI18nParseErrInvalidDecimal = 5,
    RoboI18nParseErrInvalidSign = 6,
    RoboI18nParseErrInvalidCurrency = 7,
    RoboI18nParseErrAmbiguousCurrency = 8,
    RoboI18nParseErrUnexpectedChar = 9,
    RoboI18nParseErrTrailing = 10,
    RoboI18nParseErrOverflow = 11,
    RoboI18nParseErrUnsupportedLocale = 12,
    RoboI18nParseErrInputTooLong = 13
} I18nParseErr;

typedef struct I18nParseOptions {
    I18nParseMode mode;
    int allowLeadingWs;  /* default 1 */
    int allowTrailingWs; /* default 1 */
    int requireFull;      /* default 1: reject non-ws trailing input */
    int integerOnly;      /* default 0: reject decimal separator */
    char numberingSystem[16]; /* empty = locale default */
} I18nParseOptions;

/**
 * Structured parse outcome. Always filled on API return (success or failure).
 * currency[0]==0 when no currency was present / resolved.
 * consumed = bytes successfully consumed (may be < len on partial permissive).
 * errorOffset = byte index of failure (or consumed on success).
 */
typedef struct I18nParseResult {
    I18nDecimal value;
    char currency[4];
    size_t consumed;
    size_t errorOffset;
    I18nParseErr error;
    uint8_t currencyAmbiguous;
    uint8_t ambiguousCount;
    char ambiguous[RoboI18nParseAmbigMax][4];
} I18nParseResult;

typedef struct I18nNumberParser {
    I18nLocale locale;
    I18nParseOptions options;
    I18nNumberFormatter symbols; /* resolved seps / digits / signs */
    const char *digitUtf8[10];
    uint8_t digitLen[10];
    uint8_t ready;
} I18nNumberParser;

typedef struct I18nCurrencyParseOptions {
    I18nParseOptions number;
    char requireCurrency[4]; /* empty = not required */
    /** Optional NUL-terminated list of accepted ISO codes (max 16). */
    char accepted[16][4];
    uint8_t acceptedCount; /* 0 = any known / resolved */
    int allowCurrencyNames; /* default 1 */
} I18nCurrencyParseOptions;

typedef struct I18nCurrencyParser {
    I18nNumberParser number;
    I18nCurrencyParseOptions options;
    char localeDefault[4];
    uint8_t ready;
} I18nCurrencyParser;

void I18nParseOptionsInit(I18nParseOptions *opts);
void I18nParseResultClear(I18nParseResult *out);
void I18nCurrencyParseOptionsInit(I18nCurrencyParseOptions *opts);

Result I18nNumberParserInit(I18nNumberParser *parser,
                                           const I18nLocale *locale,
                                           const I18nParseOptions *opts);

void I18nNumberParserDestroy(I18nNumberParser *parser);

/**
 * Parse a localized number from UTF-8 @p text[0..len).
 * Returns RoboOk on success; RoboParseError on grammar/content failure
 * (details in @p out); RoboInvalidArgument for NULL/oversized input.
 */
Result I18nParseNumber(const I18nNumberParser *parser,
                                     const char *text,
                                     size_t len,
                                     I18nParseResult *out);

Result I18nParseInteger(const I18nNumberParser *parser,
                                      const char *text,
                                      size_t len,
                                      I18nParseResult *out);

Result I18nCurrencyParserInit(
    I18nCurrencyParser *parser,
    const I18nLocale *locale,
    const I18nCurrencyParseOptions *opts);

void I18nCurrencyParserDestroy(I18nCurrencyParser *parser);

Result I18nParseCurrency(const I18nCurrencyParser *parser,
                                       const char *text,
                                       size_t len,
                                       I18nParseResult *out);

/** Human-readable name for a parse error category (static string). */
const char *I18nParseErrString(I18nParseErr err);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_PARSE_H */
