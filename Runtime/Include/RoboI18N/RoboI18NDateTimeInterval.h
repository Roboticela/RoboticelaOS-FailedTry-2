/*
 * RoboI18N date/time interval formatting (Goal 4.7).
 *
 * Formats [start, end] using CLDR intervalFormats. Repeated fields are
 * collapsed only when the selected CLDR pattern encodes that collapse.
 *
 * Does NOT silently reorder reversed intervals (returns RoboInvalidArgument
 * unless normalize is requested). Prefer instant-based intervals when zones
 * matter. IANA zones without tzdb → RoboNotSupported for instant conversion.
 *
 * Thread safety: formatter is not synchronized — one owner thread, or lock.
 */

#ifndef ROBO_I18N_DATETIME_INTERVAL_H
#define ROBO_I18N_DATETIME_INTERVAL_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NCalendar.h"
#include "RoboI18N/RoboI18NDateTime.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NTimeZone.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboI18nIntervalSkeletonMax 64u

typedef struct I18nDatetimeInterval {
    I18nDatetime start;
    I18nDatetime end;
} I18nDatetimeInterval;

typedef struct I18nInstantInterval {
    I18nInstant start;
    I18nInstant end;
} I18nInstantInterval;

typedef struct I18nIntervalOptions {
    char skeleton[RoboI18nIntervalSkeletonMax]; /* empty = yMMMd / Hm heuristic */
    int normalizeReversed;                        /* 1 = swap if start > end */
    char numberingSystem[16];
} I18nIntervalOptions;

typedef struct I18nIntervalFormatter {
    I18nLocale locale;
    I18nCalendar calendar;
    I18nTimezone timezone;
    I18nIntervalOptions options;
    uint8_t ready;
} I18nIntervalFormatter;

void I18nIntervalOptionsInit(I18nIntervalOptions *opts);

/**
 * Validate interval. Equal endpoints are valid.
 * start > end → RoboInvalidArgument (unless checking only).
 */
Result I18nDatetimeIntervalInit(I18nDatetimeInterval *out,
                                               const I18nDatetime *start,
                                               const I18nDatetime *end,
                                               int normalizeReversed);

Result I18nInstantIntervalInit(I18nInstantInterval *out,
                                              const I18nInstant *start,
                                              const I18nInstant *end,
                                              int normalizeReversed);

Result I18nIntervalFormatterInit(I18nIntervalFormatter *fmt,
                                                const I18nLocale *locale,
                                                const I18nCalendar *calendar,
                                                const I18nTimezone *tz,
                                                const I18nIntervalOptions *opts);

void I18nIntervalFormatterDestroy(I18nIntervalFormatter *fmt);

/**
 * Greatest differing CLDR field letter among y M d H h m s (and a when 12h).
 * Writes a single ASCII letter into @p outLetter[0]; empty if identical.
 */
Result I18nIntervalGreatestDifference(const I18nDatetime *start,
                                                     const I18nDatetime *end,
                                                     char *outLetter,
                                                     size_t letterCap);

Result I18nDatetimeIntervalFormat(const I18nIntervalFormatter *fmt,
                                                 const I18nDatetimeInterval *interval,
                                                 char *buf,
                                                 size_t bufCap,
                                                 size_t *outLen);

Result I18nDateIntervalFormat(const I18nIntervalFormatter *fmt,
                                             const I18nDate *start,
                                             const I18nDate *end,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen);

Result I18nTimeIntervalFormat(const I18nIntervalFormatter *fmt,
                                             const I18nTime *start,
                                             const I18nTime *end,
                                             char *buf,
                                             size_t bufCap,
                                             size_t *outLen);

Result I18nInstantIntervalFormat(const I18nIntervalFormatter *fmt,
                                                const I18nInstantInterval *interval,
                                                char *buf,
                                                size_t bufCap,
                                                size_t *outLen);

Result I18nDatetimeIntervalFormatToString(
    const I18nIntervalFormatter *fmt,
    const I18nDatetimeInterval *interval,
    String *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_DATETIME_INTERVAL_H */
