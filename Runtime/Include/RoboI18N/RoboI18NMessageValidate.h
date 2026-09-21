/*
 * RoboI18N message-format validation & compatibility (Goal 4.13).
 *
 * Builds on RoboMsgFmt (4.12) and RoboMsg catalogs (4.11).
 * Language: C. See runtime/i18n/RoboMsgFormat.md (Validation section).
 *
 * Does not execute/format messages. Safe for untrusted templates.
 * Offsets are UTF-8 byte offsets unless noted.
 */

#ifndef ROBO_I18N_MESSAGE_VALIDATE_H
#define ROBO_I18N_MESSAGE_VALIDATE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NMessage.h"
#include "RoboI18N/RoboI18NMessageFormat.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Current RoboMsgFmt syntax version implemented by this runtime. */
#define RoboI18nMfSyntaxVersion 1u

/** Maximum diagnostics retained by one catalog validation pass. */
#define RoboI18nMfDiagnosticsMax 256u

typedef enum I18nMfDiagCode {
    RoboI18nMfDiagOk = 0,
    RoboI18nMfDiagMalformed = 1,
    RoboI18nMfDiagInvalidUtf8 = 2,
    RoboI18nMfDiagUnknownType = 3,
    RoboI18nMfDiagMissingOther = 4,
    RoboI18nMfDiagDuplicateBranch = 5,
    RoboI18nMfDiagTypeConflict = 6,
    RoboI18nMfDiagNesting = 7,
    RoboI18nMfDiagTooLarge = 8,
    RoboI18nMfDiagUnsupportedVersion = 9,
    RoboI18nMfDiagUnsupportedOption = 10,
    RoboI18nMfDiagMissingMessage = 11,
    RoboI18nMfDiagIncompatibleArg = 12,
    RoboI18nMfDiagInvalidSelector = 13,
    RoboI18nMfDiagPoundOutsidePlural = 14
} I18nMfDiagCode;

typedef enum I18nMfSev {
    RoboI18nMfSevError = 0,
    RoboI18nMfSevWarning = 1
} I18nMfSev;

/**
 * Structured diagnostic (caller-owned buffer).
 * byteOffset: UTF-8 byte index into the template (SIZE_MAX if N/A).
 * line/column: 1-based; column counts bytes on the line; 0 if N/A.
 */
typedef struct I18nMfDiagnostic {
    Result result;
    I18nMfDiagCode code;
    I18nMfSev severity;
    size_t byteOffset;
    uint32_t line;
    uint32_t column;
    char argumentName[RoboI18nMfNameMax];
    char construct[32];
    char messageId[RoboI18nMsgIdMax];
    char localeTag[RoboI18nTagMax];
    char explanation[160];
} I18nMfDiagnostic;

/** How an argument is used inside a template (bitmask). */
typedef enum I18nMfArgUsage {
    RoboI18nMfUsageNone = 0,
    RoboI18nMfUsageString = 1u << 0,
    RoboI18nMfUsageNumber = 1u << 1,
    RoboI18nMfUsageDate = 1u << 2,
    RoboI18nMfUsageTime = 1u << 3,
    RoboI18nMfUsageSelect = 1u << 4,
    RoboI18nMfUsagePlural = 1u << 5,
    RoboI18nMfUsageOrdinal = 1u << 6
} I18nMfArgUsage;

typedef struct I18nMfArgReq {
    char name[RoboI18nMfNameMax];
    uint32_t usage; /* I18nMfArgUsage bits */
} I18nMfArgReq;

/** Static argument contract for one template. */
typedef struct I18nMfContract {
    uint32_t syntaxVersion;
    uint32_t count;
    I18nMfArgReq args[RoboI18nMfArgsMax];
} I18nMfContract;

/**
 * Resource limits. Zero field → use built-in default for that field.
 * Safe defaults match ROBO_I18N_MF_* macros.
 */
typedef struct I18nMfLimits {
    uint32_t templateMax;
    uint32_t nestingMax;
    uint32_t branchesMax;
    uint32_t argsMax;
    uint32_t outputMax;
    uint32_t diagnosticsMax;
} I18nMfLimits;

void I18nMfLimitsInit(I18nMfLimits *out);
void I18nMfDiagnosticReset(I18nMfDiagnostic *d);
void I18nMfContractReset(I18nMfContract *c);

uint32_t I18nMfSyntaxVersion(void);
const char *I18nMfDiagCodeString(I18nMfDiagCode code);

/**
 * Validate template without formatting.
 * Optional @p diag receives the first error (or OK).
 * Optional @p limits (NULL = defaults). Rejects unsupported #!robo-mf/N.
 * Alias conceptually: I18nMessageValidate.
 */
Result I18nMfValidateEx(const char *utf8,
                                       size_t len,
                                       const I18nMfLimits *limits,
                                       I18nMfDiagnostic *diag);

/** Same as mf_validate_ex — catalog/tooling-facing name. */
Result I18nMessageValidate(const char *utf8,
                                         size_t len,
                                         const I18nMfLimits *limits,
                                         I18nMfDiagnostic *diag);

/**
 * Parse with optional diagnostics + limits.
 * On failure *out is NULL; diag filled when non-NULL.
 */
Result I18nMfParseEx(const char *utf8,
                                    size_t len,
                                    const I18nMfLimits *limits,
                                    I18nMfDiagnostic *diag,
                                    I18nMfMessage **out);

/**
 * Static argument analysis on a successfully parsed message.
 * Does not allocate; fills @p out (sorted by argument name).
 */
Result I18nMfAnalyze(const I18nMfMessage *msg,
                                   I18nMfContract *out);

/** Analyze template text (parse → analyze → destroy). */
Result I18nMfAnalyzeTemplate(const char *utf8,
                                            size_t len,
                                            const I18nMfLimits *limits,
                                            I18nMfDiagnostic *diag,
                                            I18nMfContract *out);

/**
 * Interface compatibility of two contracts.
 * Shared names must have compatible usage masks; subset/superset of names OK.
 * On incompatibility fills @p diag when non-NULL.
 */
Result I18nMfContractsCompatible(const I18nMfContract *a,
                                                const I18nMfContract *b,
                                                I18nMfDiagnostic *diag);

/** 1 if usage bitmasks are compatible (no type conflict). */
int I18nMfUsageCompatible(uint32_t a, uint32_t b);

typedef struct I18nMfCatalogReport {
    uint32_t checked;
    uint32_t errors;
    uint32_t warnings;
    uint32_t diagCount;
    I18nMfDiagnostic diags[RoboI18nMfDiagnosticsMax];
} I18nMfCatalogReport;

void I18nMfCatalogReportReset(I18nMfCatalogReport *r);

/**
 * Validate every entry in @p catalog as RoboMsgFmt (each text / plural form).
 * Collects diagnostics in deterministic entry order (index ascending).
 * Does not stop at first error. Returns RoboOk if no errors (warnings OK);
 * RoboParseError if any error-severity diagnostic was recorded.
 */
Result I18nMfCatalogValidate(const I18nCatalog *catalog,
                                            const I18nMfLimits *limits,
                                            I18nMfCatalogReport *report);

/**
 * Compare locale @p locale messages against @p referenceLocale interface.
 * Detects missing IDs (warning) and incompatible argument contracts (error).
 * Uses catalog fallback only for presence checks via exact locale tags.
 */
Result I18nMfCatalogValidateFallback(
    const I18nCatalog *catalog,
    const char *referenceLocale,
    size_t referenceLocaleLen,
    const char *locale,
    size_t localeLen,
    const I18nMfLimits *limits,
    I18nMfCatalogReport *report);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_MESSAGE_VALIDATE_H */
