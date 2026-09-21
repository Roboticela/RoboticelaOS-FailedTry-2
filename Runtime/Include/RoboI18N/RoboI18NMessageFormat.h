/*
 * RoboI18N message formatting engine (Goal 4.12) — RoboMsgFmt.
 *
 * Parses UTF-8 templates into an immutable AST, then formats with named
 * arguments using RoboI18N number / plural / date-time formatters.
 *
 * Syntax is Roboticela-specific (ICU MessageFormat-inspired subset).
 * See runtime/i18n/RoboMsgFormat.md — not full ICU compatibility.
 *
 * Language: C (matches RoboI18N). Ownership: destroy parsed messages;
 * arg string/date pointers are borrowed for the duration of format().
 * Thread safety: parsed messages are immutable and shareable; format()
 * uses stack locals (re-entrant). No process-wide mutable formatter state.
 */

#ifndef ROBO_I18N_MESSAGE_FORMAT_H
#define ROBO_I18N_MESSAGE_FORMAT_H

#include "RoboBase/RoboError.h"
#include "RoboContainers/RoboString.h"
#include "RoboI18N/RoboI18NDateTime.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NMessage.h"
#include "RoboI18N/RoboI18NNumber.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboI18nMfTemplateMax 8192u
#define RoboI18nMfNestingMax 16u
#define RoboI18nMfBranchesMax 32u
#define RoboI18nMfArgsMax 32u
#define RoboI18nMfOutputMax 16384u
#define RoboI18nMfNameMax 64u

typedef enum I18nMfValueKind {
    RoboI18nMfValString = 0,
    RoboI18nMfValI64 = 1,
    RoboI18nMfValU64 = 2,
    RoboI18nMfValDecimal = 3,
    RoboI18nMfValDouble = 4,
    RoboI18nMfValBool = 5,
    RoboI18nMfValDate = 6,
    RoboI18nMfValTime = 7,
    RoboI18nMfValDatetime = 8,
    RoboI18nMfValInstant = 9
} I18nMfValueKind;

/** Named argument. String/date pointers are borrowed (not copied). */
typedef struct I18nMfArg {
    char name[RoboI18nMfNameMax];
    I18nMfValueKind kind;
    union {
        struct {
            const char *ptr;
            size_t len; /* 0 → strlen(ptr) when ptr non-NULL */
        } str;
        int64_t i64;
        uint64_t u64;
        I18nDecimal decimal;
        double dbl;
        int boolean; /* 0/1 */
        I18nDate date;
        I18nTime time;
        I18nDatetime datetime;
        I18nInstant instant;
    } u;
} I18nMfArg;

typedef struct I18nMfArgs {
    const I18nMfArg *items;
    size_t count;
} I18nMfArgs;

/** Opaque parsed message (heap). */
typedef struct I18nMfMessage I18nMfMessage;

/**
 * Parse UTF-8 template. Rejects invalid UTF-8, oversized input, bad braces,
 * unknown types, missing other in select/plural, excessive nesting.
 */
Result I18nMfParse(const char *utf8,
                                 size_t len,
                                 I18nMfMessage **out);

/** Parse then destroy — validates syntax only. */
Result I18nMfValidate(const char *utf8, size_t len);

void I18nMfDestroy(I18nMfMessage *msg);

/**
 * Format parsed message. Missing required args → RoboNotFound.
 * Select without matching key and without other → RoboNotFound.
 * Output truncated beyond RoboI18nMfOutputMax → RoboError.
 */
Result I18nMfFormat(const I18nMfMessage *msg,
                                  const I18nLocale *locale,
                                  const I18nMfArgs *args,
                                  char *out,
                                  size_t outCap,
                                  size_t *outLen);

Result I18nMfFormatToString(const I18nMfMessage *msg,
                                            const I18nLocale *locale,
                                            const I18nMfArgs *args,
                                            String *out);

/** One-shot: parse → format → destroy. */
Result I18nMfFormatTemplate(const char *utf8,
                                           size_t len,
                                           const I18nLocale *locale,
                                           const I18nMfArgs *args,
                                           char *out,
                                           size_t outCap,
                                           size_t *outLen);

/**
 * Catalog lookup (Goal 4.11) + RoboMsgFmt format.
 * Uses message_get for template, then mf_format_template.
 */
Result I18nMessageFormatMf(const I18nCatalog *catalog,
                                          const char *messageId,
                                          size_t idLen,
                                          const char *context,
                                          size_t contextLen,
                                          const I18nLocale *locale,
                                          const I18nMfArgs *args,
                                          const I18nMsgOptions *options,
                                          char *out,
                                          size_t outCap,
                                          size_t *outLen);

/* Helpers to fill args (zero the struct first or use these). */
void I18nMfArgSetString(I18nMfArg *a,
                                 const char *name,
                                 const char *utf8,
                                 size_t len);
void I18nMfArgSetI64(I18nMfArg *a, const char *name, int64_t v);
void I18nMfArgSetU64(I18nMfArg *a, const char *name, uint64_t v);
void I18nMfArgSetDecimal(I18nMfArg *a,
                                  const char *name,
                                  const I18nDecimal *d);
void I18nMfArgSetBool(I18nMfArg *a, const char *name, int v);
void I18nMfArgSetDate(I18nMfArg *a,
                               const char *name,
                               const I18nDate *d);
void I18nMfArgSetTime(I18nMfArg *a,
                               const char *name,
                               const I18nTime *t);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_MESSAGE_FORMAT_H */
