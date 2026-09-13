/*
 * RoboUnicodeNorm — Unicode Normalization Forms (Goal 3.5).
 *
 * Forms: NFC, NFD, NFKC, NFKD (Unicode Standard / UAX #15).
 * UCD version: same pin as RoboUnicode / RoboUcd (16.0.0).
 *
 * This module does NOT implement case folding, grapheme segmentation,
 * bidi, shaping, or rendering.
 */

#ifndef ROBO_UNICODE_NORM_H
#define ROBO_UNICODE_NORM_H

#include "RoboBase/RoboError.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_unicode_norm_form {
    ROBO_UNICODE_NORM_NFC = 0,
    ROBO_UNICODE_NORM_NFD = 1,
    ROBO_UNICODE_NORM_NFKC = 2,
    ROBO_UNICODE_NORM_NFKD = 3
} robo_unicode_norm_form_t;

/** UCD version string used for normalization tables (e.g. "16.0.0"). */
const char *robo_unicode_norm_ucd_version_string(void);

/**
 * Normalize a buffer of Unicode scalar values.
 *
 * @param src     Input scalars (may be NULL if src_len == 0).
 * @param src_len Number of code points.
 * @param form    NFC / NFD / NFKC / NFKD.
 * @param out     On success, receives a RoboMem-allocated buffer of scalars.
 *                Empty input → *out == NULL, *out_len == 0.
 *                Caller must robo_free(*out) when non-NULL.
 * @param out_len On success, number of output scalars.
 *
 * @return ROBO_OK, ROBO_INVALID_ARGUMENT, ROBO_OUT_OF_MEMORY.
 *
 * Surrogates in @p src are treated as ordinary code points for table lookup
 * (callers should pass scalars). Hangul uses the standard algorithmic mapping.
 */
robo_result_t robo_unicode_normalize(const robo_uchar32_t *src,
                                     size_t src_len,
                                     robo_unicode_norm_form_t form,
                                     robo_uchar32_t **out,
                                     size_t *out_len);

/** Convenience wrappers around robo_unicode_normalize. */
robo_result_t robo_unicode_normalize_nfc(const robo_uchar32_t *src,
                                         size_t src_len,
                                         robo_uchar32_t **out,
                                         size_t *out_len);
robo_result_t robo_unicode_normalize_nfd(const robo_uchar32_t *src,
                                         size_t src_len,
                                         robo_uchar32_t **out,
                                         size_t *out_len);
robo_result_t robo_unicode_normalize_nfkc(const robo_uchar32_t *src,
                                          size_t src_len,
                                          robo_uchar32_t **out,
                                          size_t *out_len);
robo_result_t robo_unicode_normalize_nfkd(const robo_uchar32_t *src,
                                          size_t src_len,
                                          robo_uchar32_t **out,
                                          size_t *out_len);

/**
 * Normalize a UTF-8 byte string.
 *
 * Output is a NUL-terminated UTF-8 buffer allocated with RoboMem.
 * *out_len is the byte length excluding the trailing NUL.
 * Empty input → *out points to an allocated empty string (""), *out_len == 0.
 * Caller must robo_free(*out) on success.
 *
 * Invalid UTF-8:
 *   STRICT  → ROBO_PARSE_ERROR, *out left NULL
 *   REPLACE → U+FFFD replacements, then normalize (same policy as encoding)
 *
 * ASCII / already-ASCII text uses a fast identity path for all four forms.
 */
robo_result_t robo_unicode_normalize_utf8(const char *src,
                                          size_t src_len,
                                          robo_unicode_norm_form_t form,
                                          robo_unicode_mode_t mode,
                                          char **out,
                                          size_t *out_len);

robo_result_t robo_unicode_normalize_nfc_utf8(const char *src,
                                              size_t src_len,
                                              robo_unicode_mode_t mode,
                                              char **out,
                                              size_t *out_len);
robo_result_t robo_unicode_normalize_nfd_utf8(const char *src,
                                              size_t src_len,
                                              robo_unicode_mode_t mode,
                                              char **out,
                                              size_t *out_len);
robo_result_t robo_unicode_normalize_nfkc_utf8(const char *src,
                                               size_t src_len,
                                               robo_unicode_mode_t mode,
                                               char **out,
                                               size_t *out_len);
robo_result_t robo_unicode_normalize_nfkd_utf8(const char *src,
                                               size_t src_len,
                                               robo_unicode_mode_t mode,
                                               char **out,
                                               size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_NORM_H */
