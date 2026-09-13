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

typedef enum robo_unicode_case_form {
    ROBO_UNICODE_CASE_UPPER = 0,       /* full uppercase */
    ROBO_UNICODE_CASE_LOWER = 1,       /* full lowercase (+ Final_Sigma in strings) */
    ROBO_UNICODE_CASE_TITLE = 2,       /* full titlecase (per code point) */
    ROBO_UNICODE_CASE_FOLD = 3,        /* full case fold (C+F) */
    ROBO_UNICODE_CASE_SIMPLE_UPPER = 4,
    ROBO_UNICODE_CASE_SIMPLE_LOWER = 5,
    ROBO_UNICODE_CASE_SIMPLE_TITLE = 6,
    ROBO_UNICODE_CASE_SIMPLE_FOLD = 7
} robo_unicode_case_form_t;

const char *robo_unicode_case_ucd_version_string(void);

/** Simple 1:1 mapping; returns mapped scalar or @p cp itself. */
robo_uchar32_t robo_unicode_simple_toupper(robo_uchar32_t cp);
robo_uchar32_t robo_unicode_simple_tolower(robo_uchar32_t cp);
robo_uchar32_t robo_unicode_simple_totitle(robo_uchar32_t cp);
robo_uchar32_t robo_unicode_simple_casefold(robo_uchar32_t cp);

/**
 * Full (or simple) case mapping of one scalar into @p out[0..ROBO_CASE_MAX_MAP).
 * @return number of output scalars (always >= 1 on success), or 0 if out_cap too small.
 *
 * Context-free: Final_Sigma is NOT applied here (no surrounding string).
 * Use UTF-8 / code-point sequence APIs for Final_Sigma-aware lowercase.
 */
size_t robo_unicode_case_map(robo_uchar32_t cp,
                             robo_unicode_case_form_t form,
                             robo_uchar32_t *out,
                             size_t out_cap);

/** Convenience wrappers around full mappings. */
size_t robo_unicode_to_upper(robo_uchar32_t cp, robo_uchar32_t *out, size_t out_cap);
size_t robo_unicode_to_lower(robo_uchar32_t cp, robo_uchar32_t *out, size_t out_cap);
size_t robo_unicode_to_title(robo_uchar32_t cp, robo_uchar32_t *out, size_t out_cap);
size_t robo_unicode_casefold(robo_uchar32_t cp, robo_uchar32_t *out, size_t out_cap);

/**
 * Map a code-point sequence. For LOWER, applies Final_Sigma when U+03A3 is at
 * a Final_Sigma context boundary (Unicode Default Case Algorithms).
 *
 * On success *out is RoboMem-allocated; empty → *out NULL, *out_len 0.
 * Caller robo_free(*out).
 */
robo_result_t robo_unicode_case_map_cps(const robo_uchar32_t *src,
                                        size_t src_len,
                                        robo_unicode_case_form_t form,
                                        robo_uchar32_t **out,
                                        size_t *out_len);

/**
 * Map a UTF-8 string. Output is NUL-terminated UTF-8 (RoboMem); caller frees.
 * Empty → allocated "".
 *
 * Invalid UTF-8: STRICT → ROBO_PARSE_ERROR; REPLACE → U+FFFD then map.
 *
 * TITLE is character-level (each scalar's titlecase mapping), NOT word/sentence
 * titlecasing (requires word segmentation — future goal).
 *
 * Does NOT normalize. Case folding does not preserve NFC/NFD.
 */
robo_result_t robo_unicode_case_map_utf8(const char *src,
                                         size_t src_len,
                                         robo_unicode_case_form_t form,
                                         robo_unicode_mode_t mode,
                                         char **out,
                                         size_t *out_len);

robo_result_t robo_unicode_to_upper_utf8(const char *src, size_t src_len,
                                         robo_unicode_mode_t mode,
                                         char **out, size_t *out_len);
robo_result_t robo_unicode_to_lower_utf8(const char *src, size_t src_len,
                                         robo_unicode_mode_t mode,
                                         char **out, size_t *out_len);
robo_result_t robo_unicode_to_title_utf8(const char *src, size_t src_len,
                                         robo_unicode_mode_t mode,
                                         char **out, size_t *out_len);
robo_result_t robo_unicode_casefold_utf8(const char *src, size_t src_len,
                                         robo_unicode_mode_t mode,
                                         char **out, size_t *out_len);

/**
 * Compare two UTF-8 strings after full case folding (C+F).
 *
 * Equality is binary equality of the folded scalar sequences — NOT
 * normalization-aware and NOT locale-sensitive.
 *
 * @param cmp  set to <0, 0, or >0 like memcmp of folded UTF-8 bytes.
 */
robo_result_t robo_unicode_casefold_compare_utf8(const char *a, size_t a_len,
                                                 const char *b, size_t b_len,
                                                 robo_unicode_mode_t mode,
                                                 int *cmp);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_CASE_H */
