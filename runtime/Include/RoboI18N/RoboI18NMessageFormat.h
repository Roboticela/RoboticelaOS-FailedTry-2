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

#define ROBO_I18N_MF_TEMPLATE_MAX 8192u
#define ROBO_I18N_MF_NESTING_MAX 16u
#define ROBO_I18N_MF_BRANCHES_MAX 32u
#define ROBO_I18N_MF_ARGS_MAX 32u
#define ROBO_I18N_MF_OUTPUT_MAX 16384u
#define ROBO_I18N_MF_NAME_MAX 64u

typedef enum robo_i18n_mf_value_kind {
    ROBO_I18N_MF_VAL_STRING = 0,
    ROBO_I18N_MF_VAL_I64 = 1,
    ROBO_I18N_MF_VAL_U64 = 2,
    ROBO_I18N_MF_VAL_DECIMAL = 3,
    ROBO_I18N_MF_VAL_DOUBLE = 4,
    ROBO_I18N_MF_VAL_BOOL = 5,
    ROBO_I18N_MF_VAL_DATE = 6,
    ROBO_I18N_MF_VAL_TIME = 7,
    ROBO_I18N_MF_VAL_DATETIME = 8,
    ROBO_I18N_MF_VAL_INSTANT = 9
} robo_i18n_mf_value_kind_t;

/** Named argument. String/date pointers are borrowed (not copied). */
typedef struct robo_i18n_mf_arg {
    char name[ROBO_I18N_MF_NAME_MAX];
    robo_i18n_mf_value_kind_t kind;
    union {
        struct {
            const char *ptr;
            size_t len; /* 0 → strlen(ptr) when ptr non-NULL */
        } str;
        int64_t i64;
        uint64_t u64;
        robo_i18n_decimal_t decimal;
        double dbl;
        int boolean; /* 0/1 */
        robo_i18n_date_t date;
        robo_i18n_time_t time;
        robo_i18n_datetime_t datetime;
        robo_i18n_instant_t instant;
    } u;
} robo_i18n_mf_arg_t;

typedef struct robo_i18n_mf_args {
    const robo_i18n_mf_arg_t *items;
    size_t count;
} robo_i18n_mf_args_t;

/** Opaque parsed message (heap). */
typedef struct robo_i18n_mf_message robo_i18n_mf_message_t;

/**
 * Parse UTF-8 template. Rejects invalid UTF-8, oversized input, bad braces,
 * unknown types, missing other in select/plural, excessive nesting.
 */
robo_result_t robo_i18n_mf_parse(const char *utf8,
                                 size_t len,
                                 robo_i18n_mf_message_t **out);

/** Parse then destroy — validates syntax only. */
robo_result_t robo_i18n_mf_validate(const char *utf8, size_t len);

void robo_i18n_mf_destroy(robo_i18n_mf_message_t *msg);

/**
 * Format parsed message. Missing required args → ROBO_NOT_FOUND.
 * Select without matching key and without other → ROBO_NOT_FOUND.
 * Output truncated beyond ROBO_I18N_MF_OUTPUT_MAX → ROBO_ERROR.
 */
robo_result_t robo_i18n_mf_format(const robo_i18n_mf_message_t *msg,
                                  const robo_i18n_locale_t *locale,
                                  const robo_i18n_mf_args_t *args,
                                  char *out,
                                  size_t out_cap,
                                  size_t *out_len);

robo_result_t robo_i18n_mf_format_to_string(const robo_i18n_mf_message_t *msg,
                                            const robo_i18n_locale_t *locale,
                                            const robo_i18n_mf_args_t *args,
                                            robo_string_t *out);

/** One-shot: parse → format → destroy. */
robo_result_t robo_i18n_mf_format_template(const char *utf8,
                                           size_t len,
                                           const robo_i18n_locale_t *locale,
                                           const robo_i18n_mf_args_t *args,
                                           char *out,
                                           size_t out_cap,
                                           size_t *out_len);

/**
 * Catalog lookup (Goal 4.11) + RoboMsgFmt format.
 * Uses message_get for template, then mf_format_template.
 */
robo_result_t robo_i18n_message_format_mf(const robo_i18n_catalog_t *catalog,
                                          const char *message_id,
                                          size_t id_len,
                                          const char *context,
                                          size_t context_len,
                                          const robo_i18n_locale_t *locale,
                                          const robo_i18n_mf_args_t *args,
                                          const robo_i18n_msg_options_t *options,
                                          char *out,
                                          size_t out_cap,
                                          size_t *out_len);

/* Helpers to fill args (zero the struct first or use these). */
void robo_i18n_mf_arg_set_string(robo_i18n_mf_arg_t *a,
                                 const char *name,
                                 const char *utf8,
                                 size_t len);
void robo_i18n_mf_arg_set_i64(robo_i18n_mf_arg_t *a, const char *name, int64_t v);
void robo_i18n_mf_arg_set_u64(robo_i18n_mf_arg_t *a, const char *name, uint64_t v);
void robo_i18n_mf_arg_set_decimal(robo_i18n_mf_arg_t *a,
                                  const char *name,
                                  const robo_i18n_decimal_t *d);
void robo_i18n_mf_arg_set_bool(robo_i18n_mf_arg_t *a, const char *name, int v);
void robo_i18n_mf_arg_set_date(robo_i18n_mf_arg_t *a,
                               const char *name,
                               const robo_i18n_date_t *d);
void robo_i18n_mf_arg_set_time(robo_i18n_mf_arg_t *a,
                               const char *name,
                               const robo_i18n_time_t *t);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_MESSAGE_FORMAT_H */
