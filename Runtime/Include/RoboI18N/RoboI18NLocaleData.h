/*
 * RoboI18N locale data — CLDR 46 derived metadata (Goal 4.2).
 *
 * Not number/date formatting. Not language detection.
 */

#ifndef ROBO_I18N_LOCALE_DATA_H
#define ROBO_I18N_LOCALE_DATA_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NLocale.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum I18nMeasurement {
    RoboI18nMeasureMetric = 0,
    RoboI18nMeasureUs = 1,
    RoboI18nMeasureUk = 2
} I18nMeasurement;

typedef struct I18nNumberingSystem {
    char id[16];
    char type[16];   /* "numeric" or "algorithmic" */
    char digits[80]; /* UTF-8 concatenation of 10 digits; empty if algorithmic */
} I18nNumberingSystem;

/**
 * CLDR likely-subtags maximize. Writes a new locale into @p out.
 * Does not modify @p in. Inferred script/region may be filled.
 */
Result I18nLocaleMaximize(const I18nLocale *in,
                                        I18nLocale *out);

/**
 * CLDR likely-subtags minimize. Maximize first, then drop redundant script/region.
 */
Result I18nLocaleMinimize(const I18nLocale *in,
                                        I18nLocale *out);

/**
 * CLDR data inheritance parent (parentLocales map, else truncation, else root "root").
 * When @p in is already root → RoboNotFound.
 */
Result I18nLocaleDataParent(const I18nLocale *in,
                                           I18nLocale *out);

/** Default numbering system id for locale (inheritance → latn). */
Result I18nLocaleDefaultNumberingSystem(const I18nLocale *loc,
                                                        char *outId,
                                                        size_t outCap);

/** Native numbering system when published; else same as default. */
Result I18nLocaleNativeNumberingSystem(const I18nLocale *loc,
                                                       char *outId,
                                                       size_t outCap);

Result I18nNumberingSystemGet(const char *id,
                                             I18nNumberingSystem *out);

/**
 * Preferred calendars for locale's region (CLDR calendarPreferenceData).
 * Writes up to @p cap ids; @p outCount set. Ids are CLDR names (e.g. "gregorian").
 */
Result I18nLocaleCalendars(const I18nLocale *loc,
                                         char outIds[][32],
                                         size_t cap,
                                         size_t *outCount);

/** First preferred calendar (default). */
Result I18nLocaleDefaultCalendar(const I18nLocale *loc,
                                                char *outId,
                                                size_t outCap);

Result I18nLocaleMeasurementSystem(const I18nLocale *loc,
                                                  I18nMeasurement *out);

const char *I18nCldrUnicodeVersionString(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_LOCALE_DATA_H */
