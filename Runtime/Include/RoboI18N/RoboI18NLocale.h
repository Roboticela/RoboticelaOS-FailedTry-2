/*
 * RoboI18N locale — BCP 47 language tags (RFC 5646).
 *
 * Distinctions:
 *   syntactic validity  — grammar of subtags
 *   canonical form      — casing + IANA Preferred-Value replacements
 *   preferred locale    — CLDR likely subtags (see RoboI18NLocaleData.h)
 *
 * Locale objects are value types (no heap). Locale lists allocate via RoboMem.
 */

#ifndef ROBO_I18N_LOCALE_H
#define ROBO_I18N_LOCALE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NVersion.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboI18nLangMax 8u
#define RoboI18nScriptMax 4u
#define RoboI18nRegionMax 3u
#define RoboI18nVariantMax 8u
#define RoboI18nVariantCount 8u
#define RoboI18nExtMax 96u
#define RoboI18nTagMax 128u

typedef struct I18nLocale {
    char language[RoboI18nLangMax + 1u];
    char script[RoboI18nScriptMax + 1u];   /* empty if absent */
    char region[RoboI18nRegionMax + 1u];   /* empty if absent */
    char variants[RoboI18nVariantCount][RoboI18nVariantMax + 1u];
    uint8_t variantCount;
    char extensions[RoboI18nExtMax + 1u];  /* e.g. "u-ca-gregory" (no leading '-') */
    char privateuse[RoboI18nExtMax + 1u];  /* e.g. "x-private" without requiring 'x-' prefix stored as "x-..." */
} I18nLocale;

typedef enum I18nMatch {
    RoboI18nMatchNone = 0,
    RoboI18nMatchLanguage = 1,
    RoboI18nMatchScript = 2,
    RoboI18nMatchRegion = 3,
    RoboI18nMatchExact = 4
} I18nMatch;

/** Clear to empty / invalid language. */
void I18nLocaleClear(I18nLocale *loc);

/**
 * Parse a BCP 47 language tag (not null-terminated required).
 * On success, @p out holds syntactically validated + cased fields.
 * Does not apply IANA Preferred-Value (use canonicalize).
 * Grandfathered/redundant full-tag Preferred-Value is applied during parse
 * when the entire input matches a registry Tag (authoritative replacement).
 *
 * Unknown-but-structurally-valid subtags are accepted.
 * Malformed syntax → RoboParseError.
 */
Result I18nLocaleParse(const char *tag,
                                     size_t len,
                                     I18nLocale *out);

/** Parse then apply IANA Preferred-Value replacements on language/script/region/variant. */
Result I18nLocaleParseCanonical(const char *tag,
                                               size_t len,
                                               I18nLocale *out);

/** Apply Preferred-Value maps in place (casing already expected). */
Result I18nLocaleCanonicalize(I18nLocale *loc);

/** Format canonical tag into @p buf (NUL-terminated). */
Result I18nLocaleFormat(const I18nLocale *loc,
                                      char *buf,
                                      size_t bufCap,
                                      size_t *outLen);

int I18nLocaleEqual(const I18nLocale *a, const I18nLocale *b);

/**
 * How specifically @p candidate matches @p range (range may omit script/region).
 * Matching is case-insensitive on subtags after normalize via equal fields.
 */
I18nMatch I18nLocaleMatches(const I18nLocale *range,
                                           const I18nLocale *candidate);

/**
 * Truncation parent (resource-fallback style), not CLDR likely-subtags.
 * Order: privateuse → extensions → last variant → region → script → (none).
 * Returns RoboNotFound when only primary language remains (no parent).
 */
Result I18nLocaleParent(const I18nLocale *loc,
                                      I18nLocale *out);

int I18nLocaleHasScript(const I18nLocale *loc);
int I18nLocaleHasRegion(const I18nLocale *loc);

const char *I18nLocaleLanguage(const I18nLocale *loc);
const char *I18nLocaleScript(const I18nLocale *loc);
const char *I18nLocaleRegion(const I18nLocale *loc);

/* --- Default locale (process-wide; caller synchronizes until RoboThread) --- */

Result I18nSetDefaultLocale(const I18nLocale *loc);
Result I18nGetDefaultLocale(I18nLocale *out);

/* --- Locale preference lists (heap) --- */

typedef struct I18nLocaleList I18nLocaleList;

/**
 * Parse comma-separated tags: "en-US, en, ur-PK".
 * Optional ";q=0.8" weights are accepted and used for stable sort (desc).
 * Empty / whitespace-only entries skipped.
 */
Result I18nLocaleListParse(const char *text,
                                          size_t len,
                                          I18nLocaleList **out);

void I18nLocaleListDestroy(I18nLocaleList *list);

size_t I18nLocaleListSize(const I18nLocaleList *list);

Result I18nLocaleListGet(const I18nLocaleList *list,
                                        size_t index,
                                        I18nLocale *out);

/**
 * Optional weight from list parse (millipercent, 0..1000). Default 1000 if
 * the list was not created with q-values. Goal 4.17.
 */
Result I18nLocaleListGetQMilli(const I18nLocaleList *list,
                                                size_t index,
                                                uint16_t *outQMilli);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_LOCALE_H */
