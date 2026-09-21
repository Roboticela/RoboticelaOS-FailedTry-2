/*
 * RoboI18N application message catalogs (Goal 4.11).
 *
 * See runtime/i18n/RoboMsg.md for format, placeholders, fallback, and security.
 *
 * Ownership: catalogs are immutable after open/load; caller owns output buffers.
 * Thread safety: concurrent reads of one catalog are safe; no global locale.
 */

#ifndef ROBO_I18N_MESSAGE_H
#define ROBO_I18N_MESSAGE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NPlural.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboI18nMsgIdMax 128u
#define RoboI18nMsgContextMax 128u
#define RoboI18nMsgVarNameMax 64u
#define RoboI18nMsgVarsMax 16u

typedef enum I18nMsgMissing {
    /** No translation → RoboNotFound. */
    RoboI18nMsgMissNotFound = 0,
    /** No translation → copy message id; RoboOk. */
    RoboI18nMsgMissReturnId = 1
} I18nMsgMissing;

typedef enum I18nMsgVarMissing {
    /** Missing placeholder value → RoboNotFound. */
    RoboI18nMsgVarMissError = 0,
    /** Leave `{name}` literally in the output. */
    RoboI18nMsgVarMissLiteral = 1
} I18nMsgVarMissing;

typedef struct I18nMsgOptions {
    /** Configured fallback locale tag (empty = none). Not assumed English. */
    char fallbackLocale[RoboI18nTagMax];
    I18nMsgMissing missing;
    I18nMsgVarMissing varMissing;
} I18nMsgOptions;

typedef struct I18nMsgVar {
    char name[RoboI18nMsgVarNameMax];
    const char *value; /* UTF-8; not owned */
    size_t valueLen;  /* if 0 and value non-NULL, strlen(value) */
} I18nMsgVar;

/**
 * Opaque catalog. Either references generated demo tables or an owned blob copy.
 * Do not copy by value after load; use pointers.
 */
typedef struct I18nCatalog I18nCatalog;

void I18nMsgOptionsInit(I18nMsgOptions *out);

/** Open the built-in demo catalog (robo.demo). No heap. */
Result I18nCatalogOpenDemo(I18nCatalog **out);

/**
 * Validate and load a RoboMsg binary blob (copies into heap).
 * Rejects bad magic/version/bounds/UTF-8. Caller must catalog_close.
 */
Result I18nCatalogLoad(const uint8_t *blob,
                                     size_t blobLen,
                                     I18nCatalog **out);

/** Release a loaded catalog; no-op for demo (still safe to call). */
void I18nCatalogClose(I18nCatalog *catalog);

const char *I18nCatalogId(const I18nCatalog *catalog);
const char *I18nCatalogVersion(const I18nCatalog *catalog);
uint32_t I18nCatalogFormatVersion(const I18nCatalog *catalog);

int I18nMessageExists(const I18nCatalog *catalog,
                             const char *messageId,
                             size_t idLen,
                             const char *context,
                             size_t contextLen,
                             const I18nLocale *locale,
                             const I18nMsgOptions *options);

/**
 * Copy localized template (or selected plural form) into @p out.
 * For plural messages without @p pluralValue, uses "other".
 * When @p pluralValue is non-NULL, selects via I18nPluralCardinal.
 */
Result I18nMessageGet(const I18nCatalog *catalog,
                                    const char *messageId,
                                    size_t idLen,
                                    const char *context,
                                    size_t contextLen,
                                    const I18nLocale *locale,
                                    const I18nDecimal *pluralValue,
                                    const I18nMsgOptions *options,
                                    char *out,
                                    size_t outCap,
                                    size_t *outLen);

/**
 * Format template: substitute named `{var}` from @p vars.
 * Escapes: `{{` → `{`, `}}` → `}`. No expression evaluation.
 */
Result I18nMessageFormat(const char *templateUtf8,
                                       size_t templateLen,
                                       const I18nMsgVar *vars,
                                       size_t varCount,
                                       const I18nMsgOptions *options,
                                       char *out,
                                       size_t outCap,
                                       size_t *outLen);

/**
 * Lookup + format in one step (get template then substitute).
 */
Result I18nMessageFormatId(const I18nCatalog *catalog,
                                          const char *messageId,
                                          size_t idLen,
                                          const char *context,
                                          size_t contextLen,
                                          const I18nLocale *locale,
                                          const I18nDecimal *pluralValue,
                                          const I18nMsgVar *vars,
                                          size_t varCount,
                                          const I18nMsgOptions *options,
                                          char *out,
                                          size_t outCap,
                                          size_t *outLen);

/** Number of raw catalog entries (all locales). */
uint32_t I18nCatalogEntryCount(const I18nCatalog *catalog);

/**
 * Read metadata for entry @p index (0 .. count-1).
 * Strings are catalog-owned (stable until catalog_close); may be empty.
 * @p isPlural: 1 if message uses plural forms (text may be empty).
 */
Result I18nCatalogEntryAt(const I18nCatalog *catalog,
                                         uint32_t index,
                                         const char **outLocale,
                                         const char **outId,
                                         const char **outContext,
                                         int *outIsPlural);

/**
 * Copy text for entry @p index.
 * @p pluralSlot: -1 = plain text; 0..5 = zero..other plural form.
 * Missing form → RoboNotFound.
 */
Result I18nCatalogEntryText(const I18nCatalog *catalog,
                                           uint32_t index,
                                           int pluralSlot,
                                           char *out,
                                           size_t outCap,
                                           size_t *outLen);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_MESSAGE_H */
