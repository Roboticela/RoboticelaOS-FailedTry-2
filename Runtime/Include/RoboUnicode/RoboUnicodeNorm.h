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

typedef enum UnicodeNormForm {
    RoboUnicodeNormNfc = 0,
    RoboUnicodeNormNfd = 1,
    RoboUnicodeNormNfkc = 2,
    RoboUnicodeNormNfkd = 3
} UnicodeNormForm;

/** UCD version string used for normalization tables (e.g. "16.0.0"). */
const char *UnicodeNormUcdVersionString(void);

/**
 * Normalize a buffer of Unicode scalar values.
 *
 * @param src     Input scalars (may be NULL if srcLen == 0).
 * @param srcLen Number of code points.
 * @param form    NFC / NFD / NFKC / NFKD.
 * @param out     On success, receives a RoboMem-allocated buffer of scalars.
 *                Empty input → *out == NULL, *outLen == 0.
 *                Caller must RoboFree(*out) when non-NULL.
 * @param outLen On success, number of output scalars.
 *
 * @return RoboOk, RoboInvalidArgument, RoboOutOfMemory.
 *
 * Surrogates in @p src are treated as ordinary code points for table lookup
 * (callers should pass scalars). Hangul uses the standard algorithmic mapping.
 */
Result UnicodeNormalize(const Uchar32 *src,
                                     size_t srcLen,
                                     UnicodeNormForm form,
                                     Uchar32 **out,
                                     size_t *outLen);

/** Convenience wrappers around UnicodeNormalize. */
Result UnicodeNormalizeNfc(const Uchar32 *src,
                                         size_t srcLen,
                                         Uchar32 **out,
                                         size_t *outLen);
Result UnicodeNormalizeNfd(const Uchar32 *src,
                                         size_t srcLen,
                                         Uchar32 **out,
                                         size_t *outLen);
Result UnicodeNormalizeNfkc(const Uchar32 *src,
                                          size_t srcLen,
                                          Uchar32 **out,
                                          size_t *outLen);
Result UnicodeNormalizeNfkd(const Uchar32 *src,
                                          size_t srcLen,
                                          Uchar32 **out,
                                          size_t *outLen);

/**
 * Normalize a UTF-8 byte string.
 *
 * Output is a NUL-terminated UTF-8 buffer allocated with RoboMem.
 * *outLen is the byte length excluding the trailing NUL.
 * Empty input → *out points to an allocated empty string (""), *outLen == 0.
 * Caller must RoboFree(*out) on success.
 *
 * Invalid UTF-8:
 *   STRICT  → RoboParseError, *out left NULL
 *   REPLACE → U+FFFD replacements, then normalize (same policy as encoding)
 *
 * ASCII / already-ASCII text uses a fast identity path for all four forms.
 */
Result UnicodeNormalizeUtf8(const char *src,
                                          size_t srcLen,
                                          UnicodeNormForm form,
                                          UnicodeMode mode,
                                          char **out,
                                          size_t *outLen);

Result UnicodeNormalizeNfcUtf8(const char *src,
                                              size_t srcLen,
                                              UnicodeMode mode,
                                              char **out,
                                              size_t *outLen);
Result UnicodeNormalizeNfdUtf8(const char *src,
                                              size_t srcLen,
                                              UnicodeMode mode,
                                              char **out,
                                              size_t *outLen);
Result UnicodeNormalizeNfkcUtf8(const char *src,
                                               size_t srcLen,
                                               UnicodeMode mode,
                                               char **out,
                                               size_t *outLen);
Result UnicodeNormalizeNfkdUtf8(const char *src,
                                               size_t srcLen,
                                               UnicodeMode mode,
                                               char **out,
                                               size_t *outLen);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_NORM_H */
