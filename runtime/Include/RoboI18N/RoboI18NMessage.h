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

#define ROBO_I18N_MSG_ID_MAX 128u
#define ROBO_I18N_MSG_CONTEXT_MAX 128u
#define ROBO_I18N_MSG_VAR_NAME_MAX 64u
#define ROBO_I18N_MSG_VARS_MAX 16u

typedef enum robo_i18n_msg_missing {
    /** No translation → ROBO_NOT_FOUND. */
    ROBO_I18N_MSG_MISS_NOT_FOUND = 0,
    /** No translation → copy message id; ROBO_OK. */
    ROBO_I18N_MSG_MISS_RETURN_ID = 1
} robo_i18n_msg_missing_t;

typedef enum robo_i18n_msg_var_missing {
    /** Missing placeholder value → ROBO_NOT_FOUND. */
    ROBO_I18N_MSG_VAR_MISS_ERROR = 0,
    /** Leave `{name}` literally in the output. */
    ROBO_I18N_MSG_VAR_MISS_LITERAL = 1
} robo_i18n_msg_var_missing_t;

typedef struct robo_i18n_msg_options {
    /** Configured fallback locale tag (empty = none). Not assumed English. */
    char fallback_locale[ROBO_I18N_TAG_MAX];
    robo_i18n_msg_missing_t missing;
    robo_i18n_msg_var_missing_t var_missing;
} robo_i18n_msg_options_t;

typedef struct robo_i18n_msg_var {
    char name[ROBO_I18N_MSG_VAR_NAME_MAX];
    const char *value; /* UTF-8; not owned */
    size_t value_len;  /* if 0 and value non-NULL, strlen(value) */
} robo_i18n_msg_var_t;

/**
 * Opaque catalog. Either references generated demo tables or an owned blob copy.
 * Do not copy by value after load; use pointers.
 */
typedef struct robo_i18n_catalog robo_i18n_catalog_t;

void robo_i18n_msg_options_init(robo_i18n_msg_options_t *out);

/** Open the built-in demo catalog (robo.demo). No heap. */
robo_result_t robo_i18n_catalog_open_demo(robo_i18n_catalog_t **out);

/**
 * Validate and load a RoboMsg binary blob (copies into heap).
 * Rejects bad magic/version/bounds/UTF-8. Caller must catalog_close.
 */
robo_result_t robo_i18n_catalog_load(const uint8_t *blob,
                                     size_t blob_len,
                                     robo_i18n_catalog_t **out);

/** Release a loaded catalog; no-op for demo (still safe to call). */
void robo_i18n_catalog_close(robo_i18n_catalog_t *catalog);

const char *robo_i18n_catalog_id(const robo_i18n_catalog_t *catalog);
const char *robo_i18n_catalog_version(const robo_i18n_catalog_t *catalog);
uint32_t robo_i18n_catalog_format_version(const robo_i18n_catalog_t *catalog);

int robo_i18n_message_exists(const robo_i18n_catalog_t *catalog,
                             const char *message_id,
                             size_t id_len,
                             const char *context,
                             size_t context_len,
                             const robo_i18n_locale_t *locale,
                             const robo_i18n_msg_options_t *options);

/**
 * Copy localized template (or selected plural form) into @p out.
 * For plural messages without @p plural_value, uses "other".
 * When @p plural_value is non-NULL, selects via robo_i18n_plural_cardinal.
 */
robo_result_t robo_i18n_message_get(const robo_i18n_catalog_t *catalog,
                                    const char *message_id,
                                    size_t id_len,
                                    const char *context,
                                    size_t context_len,
                                    const robo_i18n_locale_t *locale,
                                    const robo_i18n_decimal_t *plural_value,
                                    const robo_i18n_msg_options_t *options,
                                    char *out,
                                    size_t out_cap,
                                    size_t *out_len);

/**
 * Format template: substitute named `{var}` from @p vars.
 * Escapes: `{{` → `{`, `}}` → `}`. No expression evaluation.
 */
robo_result_t robo_i18n_message_format(const char *template_utf8,
                                       size_t template_len,
                                       const robo_i18n_msg_var_t *vars,
                                       size_t var_count,
                                       const robo_i18n_msg_options_t *options,
                                       char *out,
                                       size_t out_cap,
                                       size_t *out_len);

/**
 * Lookup + format in one step (get template then substitute).
 */
robo_result_t robo_i18n_message_format_id(const robo_i18n_catalog_t *catalog,
                                          const char *message_id,
                                          size_t id_len,
                                          const char *context,
                                          size_t context_len,
                                          const robo_i18n_locale_t *locale,
                                          const robo_i18n_decimal_t *plural_value,
                                          const robo_i18n_msg_var_t *vars,
                                          size_t var_count,
                                          const robo_i18n_msg_options_t *options,
                                          char *out,
                                          size_t out_cap,
                                          size_t *out_len);

/** Number of raw catalog entries (all locales). */
uint32_t robo_i18n_catalog_entry_count(const robo_i18n_catalog_t *catalog);

/**
 * Read metadata for entry @p index (0 .. count-1).
 * Strings are catalog-owned (stable until catalog_close); may be empty.
 * @p is_plural: 1 if message uses plural forms (text may be empty).
 */
robo_result_t robo_i18n_catalog_entry_at(const robo_i18n_catalog_t *catalog,
                                         uint32_t index,
                                         const char **out_locale,
                                         const char **out_id,
                                         const char **out_context,
                                         int *out_is_plural);

/**
 * Copy text for entry @p index.
 * @p plural_slot: -1 = plain text; 0..5 = zero..other plural form.
 * Missing form → ROBO_NOT_FOUND.
 */
robo_result_t robo_i18n_catalog_entry_text(const robo_i18n_catalog_t *catalog,
                                           uint32_t index,
                                           int plural_slot,
                                           char *out,
                                           size_t out_cap,
                                           size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_MESSAGE_H */
