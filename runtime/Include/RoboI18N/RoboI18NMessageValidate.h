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
#define ROBO_I18N_MF_SYNTAX_VERSION 1u

/** Maximum diagnostics retained by one catalog validation pass. */
#define ROBO_I18N_MF_DIAGNOSTICS_MAX 256u

typedef enum robo_i18n_mf_diag_code {
    ROBO_I18N_MF_DIAG_OK = 0,
    ROBO_I18N_MF_DIAG_MALFORMED = 1,
    ROBO_I18N_MF_DIAG_INVALID_UTF8 = 2,
    ROBO_I18N_MF_DIAG_UNKNOWN_TYPE = 3,
    ROBO_I18N_MF_DIAG_MISSING_OTHER = 4,
    ROBO_I18N_MF_DIAG_DUPLICATE_BRANCH = 5,
    ROBO_I18N_MF_DIAG_TYPE_CONFLICT = 6,
    ROBO_I18N_MF_DIAG_NESTING = 7,
    ROBO_I18N_MF_DIAG_TOO_LARGE = 8,
    ROBO_I18N_MF_DIAG_UNSUPPORTED_VERSION = 9,
    ROBO_I18N_MF_DIAG_UNSUPPORTED_OPTION = 10,
    ROBO_I18N_MF_DIAG_MISSING_MESSAGE = 11,
    ROBO_I18N_MF_DIAG_INCOMPATIBLE_ARG = 12,
    ROBO_I18N_MF_DIAG_INVALID_SELECTOR = 13,
    ROBO_I18N_MF_DIAG_POUND_OUTSIDE_PLURAL = 14
} robo_i18n_mf_diag_code_t;

typedef enum robo_i18n_mf_sev {
    ROBO_I18N_MF_SEV_ERROR = 0,
    ROBO_I18N_MF_SEV_WARNING = 1
} robo_i18n_mf_sev_t;

/**
 * Structured diagnostic (caller-owned buffer).
 * byte_offset: UTF-8 byte index into the template (SIZE_MAX if N/A).
 * line/column: 1-based; column counts bytes on the line; 0 if N/A.
 */
typedef struct robo_i18n_mf_diagnostic {
    robo_result_t result;
    robo_i18n_mf_diag_code_t code;
    robo_i18n_mf_sev_t severity;
    size_t byte_offset;
    uint32_t line;
    uint32_t column;
    char argument_name[ROBO_I18N_MF_NAME_MAX];
    char construct[32];
    char message_id[ROBO_I18N_MSG_ID_MAX];
    char locale_tag[ROBO_I18N_TAG_MAX];
    char explanation[160];
} robo_i18n_mf_diagnostic_t;

/** How an argument is used inside a template (bitmask). */
typedef enum robo_i18n_mf_arg_usage {
    ROBO_I18N_MF_USAGE_NONE = 0,
    ROBO_I18N_MF_USAGE_STRING = 1u << 0,
    ROBO_I18N_MF_USAGE_NUMBER = 1u << 1,
    ROBO_I18N_MF_USAGE_DATE = 1u << 2,
    ROBO_I18N_MF_USAGE_TIME = 1u << 3,
    ROBO_I18N_MF_USAGE_SELECT = 1u << 4,
    ROBO_I18N_MF_USAGE_PLURAL = 1u << 5,
    ROBO_I18N_MF_USAGE_ORDINAL = 1u << 6
} robo_i18n_mf_arg_usage_t;

typedef struct robo_i18n_mf_arg_req {
    char name[ROBO_I18N_MF_NAME_MAX];
    uint32_t usage; /* robo_i18n_mf_arg_usage bits */
} robo_i18n_mf_arg_req_t;

/** Static argument contract for one template. */
typedef struct robo_i18n_mf_contract {
    uint32_t syntax_version;
    uint32_t count;
    robo_i18n_mf_arg_req_t args[ROBO_I18N_MF_ARGS_MAX];
} robo_i18n_mf_contract_t;

/**
 * Resource limits. Zero field → use built-in default for that field.
 * Safe defaults match ROBO_I18N_MF_* macros.
 */
typedef struct robo_i18n_mf_limits {
    uint32_t template_max;
    uint32_t nesting_max;
    uint32_t branches_max;
    uint32_t args_max;
    uint32_t output_max;
    uint32_t diagnostics_max;
} robo_i18n_mf_limits_t;

void robo_i18n_mf_limits_init(robo_i18n_mf_limits_t *out);
void robo_i18n_mf_diagnostic_reset(robo_i18n_mf_diagnostic_t *d);
void robo_i18n_mf_contract_reset(robo_i18n_mf_contract_t *c);

uint32_t robo_i18n_mf_syntax_version(void);
const char *robo_i18n_mf_diag_code_string(robo_i18n_mf_diag_code_t code);

/**
 * Validate template without formatting.
 * Optional @p diag receives the first error (or OK).
 * Optional @p limits (NULL = defaults). Rejects unsupported #!robo-mf/N.
 * Alias conceptually: robo_i18n_message_validate.
 */
robo_result_t robo_i18n_mf_validate_ex(const char *utf8,
                                       size_t len,
                                       const robo_i18n_mf_limits_t *limits,
                                       robo_i18n_mf_diagnostic_t *diag);

/** Same as mf_validate_ex — catalog/tooling-facing name. */
robo_result_t robo_i18n_message_validate(const char *utf8,
                                         size_t len,
                                         const robo_i18n_mf_limits_t *limits,
                                         robo_i18n_mf_diagnostic_t *diag);

/**
 * Parse with optional diagnostics + limits.
 * On failure *out is NULL; diag filled when non-NULL.
 */
robo_result_t robo_i18n_mf_parse_ex(const char *utf8,
                                    size_t len,
                                    const robo_i18n_mf_limits_t *limits,
                                    robo_i18n_mf_diagnostic_t *diag,
                                    robo_i18n_mf_message_t **out);

/**
 * Static argument analysis on a successfully parsed message.
 * Does not allocate; fills @p out (sorted by argument name).
 */
robo_result_t robo_i18n_mf_analyze(const robo_i18n_mf_message_t *msg,
                                   robo_i18n_mf_contract_t *out);

/** Analyze template text (parse → analyze → destroy). */
robo_result_t robo_i18n_mf_analyze_template(const char *utf8,
                                            size_t len,
                                            const robo_i18n_mf_limits_t *limits,
                                            robo_i18n_mf_diagnostic_t *diag,
                                            robo_i18n_mf_contract_t *out);

/**
 * Interface compatibility of two contracts.
 * Shared names must have compatible usage masks; subset/superset of names OK.
 * On incompatibility fills @p diag when non-NULL.
 */
robo_result_t robo_i18n_mf_contracts_compatible(const robo_i18n_mf_contract_t *a,
                                                const robo_i18n_mf_contract_t *b,
                                                robo_i18n_mf_diagnostic_t *diag);

/** 1 if usage bitmasks are compatible (no type conflict). */
int robo_i18n_mf_usage_compatible(uint32_t a, uint32_t b);

typedef struct robo_i18n_mf_catalog_report {
    uint32_t checked;
    uint32_t errors;
    uint32_t warnings;
    uint32_t diag_count;
    robo_i18n_mf_diagnostic_t diags[ROBO_I18N_MF_DIAGNOSTICS_MAX];
} robo_i18n_mf_catalog_report_t;

void robo_i18n_mf_catalog_report_reset(robo_i18n_mf_catalog_report_t *r);

/**
 * Validate every entry in @p catalog as RoboMsgFmt (each text / plural form).
 * Collects diagnostics in deterministic entry order (index ascending).
 * Does not stop at first error. Returns ROBO_OK if no errors (warnings OK);
 * ROBO_PARSE_ERROR if any error-severity diagnostic was recorded.
 */
robo_result_t robo_i18n_mf_catalog_validate(const robo_i18n_catalog_t *catalog,
                                            const robo_i18n_mf_limits_t *limits,
                                            robo_i18n_mf_catalog_report_t *report);

/**
 * Compare locale @p locale messages against @p reference_locale interface.
 * Detects missing IDs (warning) and incompatible argument contracts (error).
 * Uses catalog fallback only for presence checks via exact locale tags.
 */
robo_result_t robo_i18n_mf_catalog_validate_fallback(
    const robo_i18n_catalog_t *catalog,
    const char *reference_locale,
    size_t reference_locale_len,
    const char *locale,
    size_t locale_len,
    const robo_i18n_mf_limits_t *limits,
    robo_i18n_mf_catalog_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_MESSAGE_VALIDATE_H */
