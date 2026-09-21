/*
 * RoboI18N relative-time formatting (Goals 4.7 / 4.19).
 *
 * Formats a relative quantity (value + unit + direction) using CLDR
 * dateFields relative / relativeTime patterns and RoboI18N plural rules.
 *
 * AUTO mode uses CLDR relative-type keywords for −2…+1…+2 when present
 * (e.g. yesterday / today / tomorrow / day-before-yesterday).
 *
 * Does NOT compute “how far away” a calendar date is — callers supply the
 * relative value. Does NOT invent English words.
 *
 * Thread safety: formatter is not synchronized — one owner thread, or lock.
 */

#ifndef ROBO_I18N_RELATIVE_TIME_H
#define ROBO_I18N_RELATIVE_TIME_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NNumber.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum I18nRelativeUnit {
    RoboI18nRelYear = 0,
    RoboI18nRelQuarter = 1,
    RoboI18nRelMonth = 2,
    RoboI18nRelWeek = 3,
    RoboI18nRelDay = 4,
    RoboI18nRelHour = 5,
    RoboI18nRelMinute = 6,
    RoboI18nRelSecond = 7
} I18nRelativeUnit;

typedef enum I18nRelativeWidth {
    RoboI18nRelWidthLong = 0,
    RoboI18nRelWidthShort = 1,
    RoboI18nRelWidthNarrow = 2
} I18nRelativeWidth;

/**
 * numeric  — always use relativeTime patterns ("in 1 day")
 * auto     — use relative-type keywords for −2…+2 when CLDR provides them
 */
typedef enum I18nRelativeNumeric {
    RoboI18nRelNumericAuto = 0,
    RoboI18nRelNumericAlways = 1
} I18nRelativeNumeric;

typedef struct I18nRelativeTime {
    int64_t value; /* signed quantity; negative = past in auto sign convention */
    I18nRelativeUnit unit;
} I18nRelativeTime;

typedef struct I18nRelativeOptions {
    I18nRelativeWidth width;
    I18nRelativeNumeric numeric;
    char numberingSystem[16];
} I18nRelativeOptions;

typedef struct I18nRelativeFormatter {
    I18nLocale locale;
    I18nRelativeOptions options;
    I18nNumberFormatter number;
    uint8_t ready;
} I18nRelativeFormatter;

void I18nRelativeOptionsInit(I18nRelativeOptions *opts);

Result I18nRelativeTimeInit(I18nRelativeTime *out,
                                           int64_t value,
                                           I18nRelativeUnit unit);

Result I18nRelativeFormatterInit(I18nRelativeFormatter *fmt,
                                                const I18nLocale *locale,
                                                const I18nRelativeOptions *opts);

void I18nRelativeFormatterDestroy(I18nRelativeFormatter *fmt);

Result I18nRelativeTimeFormat(const I18nRelativeFormatter *fmt,
                                             const I18nRelativeTime *rel,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen);

Result I18nRelativeTimeFormatUnit(const I18nRelativeFormatter *fmt,
                                                  int64_t value,
                                                  I18nRelativeUnit unit,
                                                  char *buf,
                                                  size_t bufCap,
                                                  size_t *outLen);

Result I18nRelativeTimeFormatToString(
    const I18nRelativeFormatter *fmt,
    const I18nRelativeTime *rel,
    String *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_RELATIVE_TIME_H */
