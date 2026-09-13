/*
 * RoboI18N locale-aware collation (Goal 4.9).
 *
 * Comparison modes (do not conflate):
 *   binary / code-unit     — memcmp / raw bytes (not this API)
 *   Unicode code-point     — scalar order without linguistic weights
 *   canonical equivalence  — NFD/NFC equality (RoboUnicodeNorm)
 *   locale-aware collation — Unicode Collation Algorithm + CLDR tailoring
 *
 * Pipeline:
 *   UTF-8 → STRICT decode → NFD → collation elements → level compare / sort key
 *
 * Data: UCA DUCET 16.0.0 (RoboUcaData) + CLDR 46 tailorings (RoboCollData).
 * Invalid UTF-8: STRICT rejection → ROBO_PARSE_ERROR (no replacement, no UB).
 *
 * Ownership: collator owns configuration; sort keys are caller-owned buffers.
 * Thread safety: collator is not synchronized; immutable table data is shareable.
 * Options may be changed only by the owning thread (or with external locking).
 */

#ifndef ROBO_I18N_COLLATION_H
#define ROBO_I18N_COLLATION_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NLocale.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** UCA / DUCET version string (e.g. "16.0.0"). */
const char *robo_i18n_collation_uca_version_string(void);
const char *robo_i18n_collation_cldr_version_string(void);

typedef enum robo_i18n_collation_strength {
    ROBO_I18N_COLLATION_PRIMARY = 0,     /* base letters */
    ROBO_I18N_COLLATION_SECONDARY = 1,   /* accents */
    ROBO_I18N_COLLATION_TERTIARY = 2,    /* case / variants (default) */
    ROBO_I18N_COLLATION_QUATERNARY = 3,  /* punctuation when shifted */
    ROBO_I18N_COLLATION_IDENTICAL = 4    /* code-point tie-break after weights */
} robo_i18n_collation_strength_t;

typedef enum robo_i18n_collation_alternate {
    ROBO_I18N_COLLATION_NON_IGNORABLE = 0, /* punctuation has primary weight */
    ROBO_I18N_COLLATION_SHIFTED = 1        /* variables → quaternary (UCA) */
} robo_i18n_collation_alternate_t;

/**
 * Collator options. Strength is independent of locale.
 * numeric: digit runs compared by integer value (ASCII 0-9 + Nd); not float.
 * case_first: 0 = default tertiary, 1 = upper first, 2 = lower first (tertiary tweak).
 */
typedef struct robo_i18n_collator_options {
    robo_i18n_collation_strength_t strength;
    robo_i18n_collation_alternate_t alternate;
    uint8_t numeric;     /* 0/1 */
    uint8_t case_first;  /* 0 default, 1 upperFirst, 2 lowerFirst */
} robo_i18n_collator_options_t;

typedef struct robo_i18n_collator robo_i18n_collator_t;

void robo_i18n_collator_options_init(robo_i18n_collator_options_t *out);

/**
 * Create collator for @p locale (copied). @p collation_type may be NULL/"standard".
 * Unknown type → standard/root. Locale without CLDR row → root UCA (still OK).
 * Caller destroys with robo_i18n_collator_destroy.
 */
robo_result_t robo_i18n_collator_create(const robo_i18n_locale_t *locale,
                                        const char *collation_type,
                                        const robo_i18n_collator_options_t *options,
                                        robo_i18n_collator_t **out);

void robo_i18n_collator_destroy(robo_i18n_collator_t *coll);

/** Replace options without rebuilding locale/tailoring binding. */
robo_result_t robo_i18n_collator_set_options(robo_i18n_collator_t *coll,
                                             const robo_i18n_collator_options_t *options);

robo_result_t robo_i18n_collator_get_options(const robo_i18n_collator_t *coll,
                                             robo_i18n_collator_options_t *out);

/**
 * Compare UTF-8 strings.
 * @return *out_cmp <0 / 0 / >0. Malformed UTF-8 → ROBO_PARSE_ERROR.
 */
robo_result_t robo_i18n_collator_compare(const robo_i18n_collator_t *coll,
                                         const char *a,
                                         size_t a_len,
                                         const char *b,
                                         size_t b_len,
                                         int *out_cmp);

/** Convenience: compare == 0. */
robo_result_t robo_i18n_collator_equal(const robo_i18n_collator_t *coll,
                                       const char *a,
                                       size_t a_len,
                                       const char *b,
                                       size_t b_len,
                                       int *out_equal);

/**
 * Sort key: opaque byte sequence; memcmp-compatible for same collator options
 * and same UCA/CLDR data version. Not portable across data-version bumps.
 *
 * If out_key is NULL, writes required size to *out_len (excluding NUL if any).
 * Keys are not NUL-terminated; *out_len is byte length.
 * If out_cap < needed → ROBO_ERROR and *out_len = needed.
 */
robo_result_t robo_i18n_collator_get_sort_key(const robo_i18n_collator_t *coll,
                                              const char *text,
                                              size_t text_len,
                                              uint8_t *out_key,
                                              size_t out_cap,
                                              size_t *out_len);

/**
 * Binary / code-point helpers (not linguistic).
 * UTF-8 code-point order: STRICT decode, compare scalars; invalid → ROBO_PARSE_ERROR.
 */
robo_result_t robo_i18n_compare_utf8_binary(const char *a,
                                            size_t a_len,
                                            const char *b,
                                            size_t b_len,
                                            int *out_cmp);

robo_result_t robo_i18n_compare_utf8_codepoints(const char *a,
                                                size_t a_len,
                                                const char *b,
                                                size_t b_len,
                                                int *out_cmp);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_COLLATION_H */
