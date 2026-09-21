/*
 * RoboI18N localized date/time formatting (Goal 4.6).
 *
 * Converts date / time / datetime / instant values into locale-aware UTF-8
 * strings using CLDR patterns, field names, and numbering-system digits.
 *
 * Does NOT:
 *   - invent localized data
 *   - perform bidi visual reordering (logical order only)
 *   - implement relative time or interval formatting
 *   - invent localized zone names (uses CLDR display / GMT; never fabricates)
 *
 * Ambiguous/nonexistent local times: prefer formatting from an instant.
 * Formatting from a local datetime + zone that requires conversion returns
 * RoboNotSupported for IANA zones without tzdb; FIXED/UTC are UNIQUE.
 *
 * Thread safety: formatter is not synchronized — one owner thread, or lock.
 * Pattern compilation is cached inside the formatter (no global mutable cache).
 */

#ifndef ROBO_I18N_DATETIME_FORMAT_H
#define ROBO_I18N_DATETIME_FORMAT_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NCalendar.h"
#include "RoboI18N/RoboI18NDateTime.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NNumber.h"
#include "RoboI18N/RoboI18NTimeZone.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboI18nDtfmtPatternMax 192u
#define RoboI18nDtfmtTokenMax 96u
#define RoboI18nDtfmtSkeletonMax 64u

typedef enum I18nDtfmtKind {
    RoboI18nDtfmtDate = 0,
    RoboI18nDtfmtTime = 1,
    RoboI18nDtfmtDatetime = 2
} I18nDtfmtKind;

typedef struct I18nDtfmtOptions {
    I18nCalFormatStyle dateStyle; /* DATE / DATETIME */
    I18nCalFormatStyle timeStyle; /* TIME / DATETIME */
    char numberingSystem[16];               /* empty = locale service resolve */
    char hourCycle[8];                      /* empty = locale service; h11/h12/h23/h24 */
} I18nDtfmtOptions;

/**
 * Compiled pattern token. Literals reference bytes inside formatter->pattern.
 * field is the CLDR pattern letter (e.g. 'y','M','d','H','a','z').
 */
typedef struct I18nDtfmtToken {
    uint8_t isLiteral;
    uint8_t field;
    uint8_t count;
    uint16_t litOff;
    uint16_t litLen;
} I18nDtfmtToken;

/**
 * Stack formatter. Compiles the active pattern into tokens on init.
 * Resolved names/patterns come from immortal CLDR tables.
 */
typedef struct I18nDatetimeFormatter {
    I18nLocale locale;
    I18nCalendar calendar;
    I18nTimezone timezone;
    I18nDtfmtOptions options;
    I18nNumberFormatter number;
    char pattern[RoboI18nDtfmtPatternMax];
    I18nDtfmtToken tokens[RoboI18nDtfmtTokenMax];
    uint16_t tokenCount;
    uint8_t ready;
} I18nDatetimeFormatter;

void I18nDtfmtOptionsInit(I18nDtfmtOptions *opts);

/**
 * Initialize for style-based formatting (date/time/datetime).
 * @p calendar NULL → gregorian. @p tz NULL → UTC.
 * Calendars without conversion support → RoboNotSupported.
 */
Result I18nDatetimeFormatterInit(I18nDatetimeFormatter *fmt,
                                                const I18nLocale *locale,
                                                const I18nCalendar *calendar,
                                                const I18nTimezone *tz,
                                                I18nDtfmtKind kind,
                                                const I18nDtfmtOptions *opts);

/** Initialize with an explicit CLDR pattern string. */
Result I18nDatetimeFormatterInitPattern(
    I18nDatetimeFormatter *fmt,
    const I18nLocale *locale,
    const I18nCalendar *calendar,
    const I18nTimezone *tz,
    const char *pattern,
    const I18nDtfmtOptions *opts);

/**
 * Initialize from a CLDR skeleton (e.g. "yMMMd", "Hm").
 * Matching: exact availableFormats hit, else best field-coverage match
 * (see i18n/README.md).
 */
Result I18nDatetimeFormatterInitSkeleton(
    I18nDatetimeFormatter *fmt,
    const I18nLocale *locale,
    const I18nCalendar *calendar,
    const I18nTimezone *tz,
    const char *skeleton,
    const I18nDtfmtOptions *opts);

void I18nDatetimeFormatterDestroy(I18nDatetimeFormatter *fmt);

/** Resolve skeleton → immortal pattern string (CLDR availableFormats). */
Result I18nDatetimeResolveSkeleton(const I18nLocale *locale,
                                                  const I18nCalendar *calendar,
                                                  const char *skeleton,
                                                  const char **outPattern);

Result I18nDatetimeFormatDate(const I18nDatetimeFormatter *fmt,
                                             const I18nDate *date,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen);

Result I18nDatetimeFormatTime(const I18nDatetimeFormatter *fmt,
                                             const I18nTime *time,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen);

Result I18nDatetimeFormatDatetime(const I18nDatetimeFormatter *fmt,
                                                 const I18nDatetime *dt,
                                                 char *buf,
                                                 size_t bufCap,
                                                 size_t *outLen);

/**
 * Format an absolute instant in the formatter's time zone (to local fields).
 * IANA zones without tzdb → RoboNotSupported.
 */
Result I18nDatetimeFormatInstant(const I18nDatetimeFormatter *fmt,
                                                const I18nInstant *instant,
                                                char *buf,
                                                size_t bufCap,
                                                size_t *outLen);

Result I18nDatetimeFormatToString(const I18nDatetimeFormatter *fmt,
                                                  const I18nDatetime *dt,
                                                  String *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_DATETIME_FORMAT_H */
