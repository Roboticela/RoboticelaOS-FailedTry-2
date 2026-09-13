/*
 * RoboI18N locale-aware search & string matching foundation (Goal 4.24).
 *
 * Distinct from (do not conflate):
 *   exact byte / code-unit compare  — memcmp
 *   code-point order                — scalar lexicographic order
 *   canonical equivalence           — NFD/NFC equality
 *   Unicode case folding            — CaseFolding C+F (not locale lowercasing)
 *   collation equality / sorting    — UCA + CLDR (Goal 4.9)
 *   locale tag matching             — robo_i18n_match_t in Locale.h
 *   search matching                 — this module (substring + ranges)
 *   transliteration                 — Goal 4.25 (NOT used here)
 *   regex / full-text indexing      — out of scope
 *
 * Indexing model: all offsets are UTF-8 byte offsets into the caller-provided
 * haystack (half-open [start, end)). Optional grapheme expansion adjusts the
 * reported range to extended grapheme cluster boundaries without changing
 * which text matched semantically.
 *
 * Thread safety: searcher is not synchronized — one owner thread, or lock.
 * Collator (if any) is owned by the searcher.
 */

#ifndef ROBO_I18N_SEARCH_H
#define ROBO_I18N_SEARCH_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NCollation.h"
#include "RoboI18N/RoboI18NLocale.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hard limit on haystack/needle UTF-8 byte length. */
#define ROBO_I18N_SEARCH_INPUT_MAX 65536u

/** Max matches returned by find_all. */
#define ROBO_I18N_SEARCH_FIND_ALL_MAX 256u

/**
 * Explicit matching mode — not a single "fuzzy" switch.
 * (Named SEARCH_* to avoid clashing with locale robo_i18n_match_t.)
 */
typedef enum robo_i18n_search_mode {
    /** Valid UTF-8 byte-for-byte equality. */
    ROBO_I18N_SEARCH_EXACT = 0,
    /** Unicode scalar sequence equality (STRICT decode). */
    ROBO_I18N_SEARCH_CODEPOINT = 1,
    /** Canonical equivalence via NFD (or NFKD if compatibility). */
    ROBO_I18N_SEARCH_CANONICAL = 2,
    /** Full Unicode case folding (C+F); not locale lowercasing. */
    ROBO_I18N_SEARCH_CASEFOLD = 3,
    /** NFD (or NFKD) then full case fold. */
    ROBO_I18N_SEARCH_CANONICAL_CASEFOLD = 4,
    /** Locale-aware via RoboCollator at configured strength. */
    ROBO_I18N_SEARCH_COLLATION = 5
} robo_i18n_search_mode_t;

typedef struct robo_i18n_search_options {
    robo_i18n_search_mode_t mode;
    /**
     * Collation strength when mode == COLLATION.
     * PRIMARY ≈ base-letter equality (accent- and case-insensitive linguistically).
     * Default TERTIARY.
     */
    robo_i18n_collation_strength_t collation_strength;
    /**
     * Diacritic-insensitive for non-COLLATION modes:
     * NFD/NFKD then drop General_Category=Mn (nonspacing marks).
     * Does NOT imply NFKC unless compatibility is set.
     * For COLLATION, prefer PRIMARY strength instead (this flag is ignored).
     */
    uint8_t ignore_diacritics;
    /**
     * Use NFKD/NFKC path instead of NFD/NFC where normalization applies.
     * Default 0 (canonical only).
     */
    uint8_t compatibility;
    /**
     * When 1 (default), expand reported match byte range to extended grapheme
     * cluster boundaries containing the semantic match.
     * When 0, report the minimal matching UTF-8 span (may split clusters).
     */
    uint8_t grapheme_safe;
} robo_i18n_search_options_t;

/**
 * One hit in the original haystack.
 * [start, end) — semantic match byte range (before grapheme expansion).
 * [grapheme_start, grapheme_end) — after optional grapheme expansion.
 */
typedef struct robo_i18n_search_hit {
    size_t start;
    size_t end;
    size_t grapheme_start;
    size_t grapheme_end;
} robo_i18n_search_hit_t;

typedef struct robo_i18n_searcher {
    robo_i18n_search_options_t options;
    robo_i18n_locale_t locale;
    uint8_t has_locale;
    robo_i18n_collator_t *collator; /* owned; NULL unless COLLATION */
    uint8_t ready;
} robo_i18n_searcher_t;

/**
 * Find iterator: resumes after the previous match end (byte offset).
 * Does not own haystack/needle — pointers must remain valid.
 */
typedef struct robo_i18n_search_iter {
    const robo_i18n_searcher_t *searcher;
    const char *haystack;
    size_t hay_len;
    const char *needle;
    size_t needle_len;
    size_t next_start; /* byte offset to resume */
    uint8_t active;
} robo_i18n_search_iter_t;

void robo_i18n_search_options_init(robo_i18n_search_options_t *opts);

/**
 * Create searcher. @p locale may be NULL; COLLATION without locale uses und/root.
 * Ownership: destroy with robo_i18n_searcher_destroy.
 */
robo_result_t robo_i18n_searcher_init(robo_i18n_searcher_t *searcher,
                                      const robo_i18n_locale_t *locale,
                                      const robo_i18n_search_options_t *opts);

void robo_i18n_searcher_destroy(robo_i18n_searcher_t *searcher);

robo_result_t robo_i18n_searcher_set_options(robo_i18n_searcher_t *searcher,
                                             const robo_i18n_search_options_t *opts);

/** Compare under searcher mode. *out_cmp like memcmp (<0/0/>0). */
robo_result_t robo_i18n_search_compare(const robo_i18n_searcher_t *searcher,
                                       const char *a,
                                       size_t a_len,
                                       const char *b,
                                       size_t b_len,
                                       int *out_cmp);

/** Equality under searcher mode. *out_equal is 0/1. */
robo_result_t robo_i18n_search_equal(const robo_i18n_searcher_t *searcher,
                                     const char *a,
                                     size_t a_len,
                                     const char *b,
                                     size_t b_len,
                                     int *out_equal);

robo_result_t robo_i18n_search_contains(const robo_i18n_searcher_t *searcher,
                                        const char *haystack,
                                        size_t hay_len,
                                        const char *needle,
                                        size_t needle_len,
                                        int *out_found);

robo_result_t robo_i18n_search_starts_with(const robo_i18n_searcher_t *searcher,
                                           const char *haystack,
                                           size_t hay_len,
                                           const char *needle,
                                           size_t needle_len,
                                           int *out_found);

robo_result_t robo_i18n_search_ends_with(const robo_i18n_searcher_t *searcher,
                                         const char *haystack,
                                         size_t hay_len,
                                         const char *needle,
                                         size_t needle_len,
                                         int *out_found);

/**
 * Find first match. *out_found 0/1; when found, fills @p out_hit.
 * Empty needle matches at offset 0 (zero-width).
 */
robo_result_t robo_i18n_search_find(const robo_i18n_searcher_t *searcher,
                                    const char *haystack,
                                    size_t hay_len,
                                    const char *needle,
                                    size_t needle_len,
                                    int *out_found,
                                    robo_i18n_search_hit_t *out_hit);

robo_result_t robo_i18n_search_iter_init(robo_i18n_search_iter_t *it,
                                         const robo_i18n_searcher_t *searcher,
                                         const char *haystack,
                                         size_t hay_len,
                                         const char *needle,
                                         size_t needle_len);

/**
 * Next non-overlapping match (resumes at previous end; empty needle advances
 * one grapheme or one code point to avoid infinite loops).
 * ROBO_OK + *out_found=0 at end.
 */
robo_result_t robo_i18n_search_iter_next(robo_i18n_search_iter_t *it,
                                         int *out_found,
                                         robo_i18n_search_hit_t *out_hit);

/**
 * Collect up to @p out_cap matches (non-overlapping). *out_count set.
 * Caps at ROBO_I18N_SEARCH_FIND_ALL_MAX even if out_cap is larger.
 */
robo_result_t robo_i18n_search_find_all(const robo_i18n_searcher_t *searcher,
                                        const char *haystack,
                                        size_t hay_len,
                                        const char *needle,
                                        size_t needle_len,
                                        robo_i18n_search_hit_t *out_hits,
                                        size_t out_cap,
                                        size_t *out_count);

const char *robo_i18n_search_mode_string(robo_i18n_search_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_SEARCH_H */
