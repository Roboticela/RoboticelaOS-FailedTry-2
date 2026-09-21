/*
 * RoboI18N locale-aware search & string matching foundation (Goal 4.24).
 *
 * Distinct from (do not conflate):
 *   exact byte / code-unit compare  — memcmp
 *   code-point order                — scalar lexicographic order
 *   canonical equivalence           — NFD/NFC equality
 *   Unicode case folding            — CaseFolding C+F (not locale lowercasing)
 *   collation equality / sorting    — UCA + CLDR (Goal 4.9)
 *   locale tag matching             — I18nMatch in Locale.h
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
#define RoboI18nSearchInputMax 65536u

/** Max matches returned by find_all. */
#define RoboI18nSearchFindAllMax 256u

/**
 * Explicit matching mode — not a single "fuzzy" switch.
 * (Named SEARCH_* to avoid clashing with locale I18nMatch.)
 */
typedef enum I18nSearchMode {
    /** Valid UTF-8 byte-for-byte equality. */
    RoboI18nSearchExact = 0,
    /** Unicode scalar sequence equality (STRICT decode). */
    RoboI18nSearchCodepoint = 1,
    /** Canonical equivalence via NFD (or NFKD if compatibility). */
    RoboI18nSearchCanonical = 2,
    /** Full Unicode case folding (C+F); not locale lowercasing. */
    RoboI18nSearchCasefold = 3,
    /** NFD (or NFKD) then full case fold. */
    RoboI18nSearchCanonicalCasefold = 4,
    /** Locale-aware via RoboCollator at configured strength. */
    RoboI18nSearchCollation = 5
} I18nSearchMode;

typedef struct I18nSearchOptions {
    I18nSearchMode mode;
    /**
     * Collation strength when mode == COLLATION.
     * PRIMARY ≈ base-letter equality (accent- and case-insensitive linguistically).
     * Default TERTIARY.
     */
    I18nCollationStrength collationStrength;
    /**
     * Diacritic-insensitive for non-COLLATION modes:
     * NFD/NFKD then drop General_Category=Mn (nonspacing marks).
     * Does NOT imply NFKC unless compatibility is set.
     * For COLLATION, prefer PRIMARY strength instead (this flag is ignored).
     */
    uint8_t ignoreDiacritics;
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
    uint8_t graphemeSafe;
} I18nSearchOptions;

/**
 * One hit in the original haystack.
 * [start, end) — semantic match byte range (before grapheme expansion).
 * [graphemeStart, graphemeEnd) — after optional grapheme expansion.
 */
typedef struct I18nSearchHit {
    size_t start;
    size_t end;
    size_t graphemeStart;
    size_t graphemeEnd;
} I18nSearchHit;

typedef struct I18nSearcher {
    I18nSearchOptions options;
    I18nLocale locale;
    uint8_t hasLocale;
    I18nCollator *collator; /* owned; NULL unless COLLATION */
    uint8_t ready;
} I18nSearcher;

/**
 * Find iterator: resumes after the previous match end (byte offset).
 * Does not own haystack/needle — pointers must remain valid.
 */
typedef struct I18nSearchIter {
    const I18nSearcher *searcher;
    const char *haystack;
    size_t hayLen;
    const char *needle;
    size_t needleLen;
    size_t nextStart; /* byte offset to resume */
    uint8_t active;
} I18nSearchIter;

void I18nSearchOptionsInit(I18nSearchOptions *opts);

/**
 * Create searcher. @p locale may be NULL; COLLATION without locale uses und/root.
 * Ownership: destroy with I18nSearcherDestroy.
 */
Result I18nSearcherInit(I18nSearcher *searcher,
                                      const I18nLocale *locale,
                                      const I18nSearchOptions *opts);

void I18nSearcherDestroy(I18nSearcher *searcher);

Result I18nSearcherSetOptions(I18nSearcher *searcher,
                                             const I18nSearchOptions *opts);

/** Compare under searcher mode. *outCmp like memcmp (<0/0/>0). */
Result I18nSearchCompare(const I18nSearcher *searcher,
                                       const char *a,
                                       size_t aLen,
                                       const char *b,
                                       size_t bLen,
                                       int *outCmp);

/** Equality under searcher mode. *outEqual is 0/1. */
Result I18nSearchEqual(const I18nSearcher *searcher,
                                     const char *a,
                                     size_t aLen,
                                     const char *b,
                                     size_t bLen,
                                     int *outEqual);

Result I18nSearchContains(const I18nSearcher *searcher,
                                        const char *haystack,
                                        size_t hayLen,
                                        const char *needle,
                                        size_t needleLen,
                                        int *outFound);

Result I18nSearchStartsWith(const I18nSearcher *searcher,
                                           const char *haystack,
                                           size_t hayLen,
                                           const char *needle,
                                           size_t needleLen,
                                           int *outFound);

Result I18nSearchEndsWith(const I18nSearcher *searcher,
                                         const char *haystack,
                                         size_t hayLen,
                                         const char *needle,
                                         size_t needleLen,
                                         int *outFound);

/**
 * Find first match. *outFound 0/1; when found, fills @p outHit.
 * Empty needle matches at offset 0 (zero-width).
 */
Result I18nSearchFind(const I18nSearcher *searcher,
                                    const char *haystack,
                                    size_t hayLen,
                                    const char *needle,
                                    size_t needleLen,
                                    int *outFound,
                                    I18nSearchHit *outHit);

Result I18nSearchIterInit(I18nSearchIter *it,
                                         const I18nSearcher *searcher,
                                         const char *haystack,
                                         size_t hayLen,
                                         const char *needle,
                                         size_t needleLen);

/**
 * Next non-overlapping match (resumes at previous end; empty needle advances
 * one grapheme or one code point to avoid infinite loops).
 * RoboOk + *outFound=0 at end.
 */
Result I18nSearchIterNext(I18nSearchIter *it,
                                         int *outFound,
                                         I18nSearchHit *outHit);

/**
 * Collect up to @p outCap matches (non-overlapping). *outCount set.
 * Caps at RoboI18nSearchFindAllMax even if outCap is larger.
 */
Result I18nSearchFindAll(const I18nSearcher *searcher,
                                        const char *haystack,
                                        size_t hayLen,
                                        const char *needle,
                                        size_t needleLen,
                                        I18nSearchHit *outHits,
                                        size_t outCap,
                                        size_t *outCount);

const char *I18nSearchModeString(I18nSearchMode mode);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_SEARCH_H */
