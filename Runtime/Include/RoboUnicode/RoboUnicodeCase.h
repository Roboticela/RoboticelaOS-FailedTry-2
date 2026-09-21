/*
 * RoboUnicodeCase — Unicode case mapping and case folding (Goal 3.7).
 *
 * Distinguishes:
 *   simple mapping  — 1:1 from UnicodeData.txt
 *   full mapping    — SpecialCasing unconditional + simple fallback (may expand)
 *   case folding    — CaseFolding.txt status C+F (full) or C+S (simple)
 *
 * Locale-independent by default. Turkish/Lithuanian SpecialCasing and
 * CaseFolding status T are NOT applied. Final_Sigma (Greek) IS applied for
 * full lowercase (context-sensitive, not locale-sensitive).
 *
 * Does NOT combine normalization with case mapping unless the caller does so.
 */

#ifndef ROBO_UNICODE_CASE_H
#define ROBO_UNICODE_CASE_H

#include "RoboBase/RoboError.h"
#include "RoboUnicode/RoboCaseData.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum UnicodeCaseForm {
    RoboUnicodeCaseUpper = 0,       /* full uppercase */
    RoboUnicodeCaseLower = 1,       /* full lowercase (+ Final_Sigma in strings) */
    RoboUnicodeCaseTitle = 2,       /* full titlecase (per code point) */
    RoboUnicodeCaseFold = 3,        /* full case fold (C+F) */
    RoboUnicodeCaseSimpleUpper = 4,
    RoboUnicodeCaseSimpleLower = 5,
    RoboUnicodeCaseSimpleTitle = 6,
    RoboUnicodeCaseSimpleFold = 7
} UnicodeCaseForm;

const char *UnicodeCaseUcdVersionString(void);

/** Simple 1:1 mapping; returns mapped scalar or @p cp itself. */
Uchar32 UnicodeSimpleToupper(Uchar32 cp);
Uchar32 UnicodeSimpleTolower(Uchar32 cp);
Uchar32 UnicodeSimpleTotitle(Uchar32 cp);
Uchar32 UnicodeSimpleCasefold(Uchar32 cp);

/**
 * Full (or simple) case mapping of one scalar into @p out[0..RoboCaseMaxMap).
 * @return number of output scalars (always >= 1 on success), or 0 if outCap too small.
 *
 * Context-free: Final_Sigma is NOT applied here (no surrounding string).
 * Use UTF-8 / code-point sequence APIs for Final_Sigma-aware lowercase.
 */
size_t UnicodeCaseMap(Uchar32 cp,
                             UnicodeCaseForm form,
                             Uchar32 *out,
                             size_t outCap);

/** Convenience wrappers around full mappings. */
size_t UnicodeToUpper(Uchar32 cp, Uchar32 *out, size_t outCap);
size_t UnicodeToLower(Uchar32 cp, Uchar32 *out, size_t outCap);
size_t UnicodeToTitle(Uchar32 cp, Uchar32 *out, size_t outCap);
size_t UnicodeCasefold(Uchar32 cp, Uchar32 *out, size_t outCap);

/**
 * Map a code-point sequence. For LOWER, applies Final_Sigma when U+03A3 is at
 * a Final_Sigma context boundary (Unicode Default Case Algorithms).
 *
 * On success *out is RoboMem-allocated; empty → *out NULL, *outLen 0.
 * Caller RoboFree(*out).
 */
Result UnicodeCaseMapCps(const Uchar32 *src,
                                        size_t srcLen,
                                        UnicodeCaseForm form,
                                        Uchar32 **out,
                                        size_t *outLen);

/**
 * Map a UTF-8 string. Output is NUL-terminated UTF-8 (RoboMem); caller frees.
 * Empty → allocated "".
 *
 * Invalid UTF-8: STRICT → RoboParseError; REPLACE → U+FFFD then map.
 *
 * TITLE is character-level (each scalar's titlecase mapping), NOT word/sentence
 * titlecasing (requires word segmentation — future goal).
 *
 * Does NOT normalize. Case folding does not preserve NFC/NFD.
 */
Result UnicodeCaseMapUtf8(const char *src,
                                         size_t srcLen,
                                         UnicodeCaseForm form,
                                         UnicodeMode mode,
                                         char **out,
                                         size_t *outLen);

Result UnicodeToUpperUtf8(const char *src, size_t srcLen,
                                         UnicodeMode mode,
                                         char **out, size_t *outLen);
Result UnicodeToLowerUtf8(const char *src, size_t srcLen,
                                         UnicodeMode mode,
                                         char **out, size_t *outLen);
Result UnicodeToTitleUtf8(const char *src, size_t srcLen,
                                         UnicodeMode mode,
                                         char **out, size_t *outLen);
Result UnicodeCasefoldUtf8(const char *src, size_t srcLen,
                                         UnicodeMode mode,
                                         char **out, size_t *outLen);

/**
 * Compare two UTF-8 strings after full case folding (C+F).
 *
 * Equality is binary equality of the folded scalar sequences — NOT
 * normalization-aware and NOT locale-sensitive.
 *
 * @param cmp  set to <0, 0, or >0 like memcmp of folded UTF-8 bytes.
 */
Result UnicodeCasefoldCompareUtf8(const char *a, size_t aLen,
                                                 const char *b, size_t bLen,
                                                 UnicodeMode mode,
                                                 int *cmp);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_CASE_H */
