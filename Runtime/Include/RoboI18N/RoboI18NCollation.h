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
 * Invalid UTF-8: STRICT rejection → RoboParseError (no replacement, no UB).
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
const char *I18nCollationUcaVersionString(void);
const char *I18nCollationCldrVersionString(void);

typedef enum I18nCollationStrength {
    RoboI18nCollationPrimary = 0,     /* base letters */
    RoboI18nCollationSecondary = 1,   /* accents */
    RoboI18nCollationTertiary = 2,    /* case / variants (default) */
    RoboI18nCollationQuaternary = 3,  /* punctuation when shifted */
    RoboI18nCollationIdentical = 4    /* code-point tie-break after weights */
} I18nCollationStrength;

typedef enum I18nCollationAlternate {
    RoboI18nCollationNonIgnorable = 0, /* punctuation has primary weight */
    RoboI18nCollationShifted = 1        /* variables → quaternary (UCA) */
} I18nCollationAlternate;

/**
 * Collator options. Strength is independent of locale.
 * numeric: digit runs compared by integer value (ASCII 0-9 + Nd); not float.
 * caseFirst: 0 = default tertiary, 1 = upper first, 2 = lower first (tertiary tweak).
 */
typedef struct I18nCollatorOptions {
    I18nCollationStrength strength;
    I18nCollationAlternate alternate;
    uint8_t numeric;     /* 0/1 */
    uint8_t caseFirst;  /* 0 default, 1 upperFirst, 2 lowerFirst */
} I18nCollatorOptions;

typedef struct I18nCollator I18nCollator;

void I18nCollatorOptionsInit(I18nCollatorOptions *out);

/**
 * Create collator for @p locale (copied). @p collationType may be NULL/"standard".
 * Unknown type → standard/root. Locale without CLDR row → root UCA (still OK).
 * Caller destroys with I18nCollatorDestroy.
 */
Result I18nCollatorCreate(const I18nLocale *locale,
                                        const char *collationType,
                                        const I18nCollatorOptions *options,
                                        I18nCollator **out);

void I18nCollatorDestroy(I18nCollator *coll);

/** Replace options without rebuilding locale/tailoring binding. */
Result I18nCollatorSetOptions(I18nCollator *coll,
                                             const I18nCollatorOptions *options);

Result I18nCollatorGetOptions(const I18nCollator *coll,
                                             I18nCollatorOptions *out);

/**
 * Compare UTF-8 strings.
 * @return *outCmp <0 / 0 / >0. Malformed UTF-8 → RoboParseError.
 */
Result I18nCollatorCompare(const I18nCollator *coll,
                                         const char *a,
                                         size_t aLen,
                                         const char *b,
                                         size_t bLen,
                                         int *outCmp);

/** Convenience: compare == 0. */
Result I18nCollatorEqual(const I18nCollator *coll,
                                       const char *a,
                                       size_t aLen,
                                       const char *b,
                                       size_t bLen,
                                       int *outEqual);

/**
 * Sort key: opaque byte sequence; memcmp-compatible for same collator options
 * and same UCA/CLDR data version. Not portable across data-version bumps.
 *
 * If outKey is NULL, writes required size to *outLen (excluding NUL if any).
 * Keys are not NUL-terminated; *outLen is byte length.
 * If outCap < needed → RoboError and *outLen = needed.
 */
Result I18nCollatorGetSortKey(const I18nCollator *coll,
                                              const char *text,
                                              size_t textLen,
                                              uint8_t *outKey,
                                              size_t outCap,
                                              size_t *outLen);

/**
 * Binary / code-point helpers (not linguistic).
 * UTF-8 code-point order: STRICT decode, compare scalars; invalid → RoboParseError.
 */
Result I18nCompareUtf8Binary(const char *a,
                                            size_t aLen,
                                            const char *b,
                                            size_t bLen,
                                            int *outCmp);

Result I18nCompareUtf8Codepoints(const char *a,
                                                size_t aLen,
                                                const char *b,
                                                size_t bLen,
                                                int *outCmp);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_COLLATION_H */
