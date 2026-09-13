/*
 * RoboUnicode — encoding foundation (Goal 3.3).
 *
 * Unicode version pin: Include/RoboUnicode/RoboUnicodeVersion.h
 * (synced from Tools/ucd/robo_unicode_version.py — Goal 3.14).
 *
 * Terminology (do not conflate):
 *   byte            — 8-bit unit in a UTF-8 sequence
 *   code unit       — UTF-16 16-bit unit or UTF-32 32-bit unit
 *   code point      — integer 0..0x10FFFF (may be a surrogate)
 *   scalar value    — code point excluding surrogates U+D800..U+DFFF
 *   grapheme cluster— user-perceived character (see RoboUnicodeGrapheme.h)
 *   glyph           — font rendering unit (not implemented here)
 */

#ifndef ROBO_UNICODE_H
#define ROBO_UNICODE_H

#include "RoboBase/RoboError.h"
#include "RoboUnicode/RoboUnicodeVersion.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Unicode scalar / code point container (0 .. 0x10FFFF). */
typedef uint32_t robo_uchar32_t;

/** UTF-16 code unit. */
typedef uint16_t robo_uchar16_t;

#define ROBO_UCHAR32_REPLACEMENT ((robo_uchar32_t)0xFFFDu)
#define ROBO_UCHAR32_MAX ((robo_uchar32_t)0x10FFFFu)

typedef enum robo_unicode_status {
    ROBO_UNICODE_OK = 0,
    ROBO_UNICODE_TRUNCATED = 1,   /* incomplete sequence at end of input */
    ROBO_UNICODE_INVALID = 2,     /* ill-formed / overlong / bad surrogate */
    ROBO_UNICODE_NO_SPACE = 3,    /* output buffer too small */
    ROBO_UNICODE_OUT_OF_RANGE = 4 /* not a Unicode scalar value */
} robo_unicode_status_t;

/**
 * STRICT — malformed input yields error status; no replacement emitted.
 * REPLACE — emit U+FFFD and advance by a defined amount (see README).
 */
typedef enum robo_unicode_mode {
    ROBO_UNICODE_STRICT = 0,
    ROBO_UNICODE_REPLACE = 1
} robo_unicode_mode_t;

const char *robo_unicode_version_string(void);

int robo_unicode_is_surrogate(robo_uchar32_t cp);
/** @return 1 if cp is a Unicode scalar value (assignable to UTF-8/16/32). */
int robo_unicode_is_scalar(robo_uchar32_t cp);

/* --- UTF-8 ---------------------------------------------------------------- */

/** Bytes needed to encode @p cp (1..4), or 0 if not a scalar. */
size_t robo_unicode_utf8_encode_len(robo_uchar32_t cp);

/**
 * Encode scalar @p cp into @p out.
 * @param written optional; set to bytes written on success.
 */
robo_unicode_status_t robo_unicode_utf8_encode(robo_uchar32_t cp,
                                               char *out,
                                               size_t out_cap,
                                               size_t *written);

/**
 * Decode one UTF-8 sequence starting at @p offset within @p src[0..len).
 * On success / REPLACE, sets *out_cp and *out_next (byte index after sequence).
 */
robo_unicode_status_t robo_unicode_utf8_decode(const char *src,
                                               size_t len,
                                               size_t offset,
                                               robo_unicode_mode_t mode,
                                               robo_uchar32_t *out_cp,
                                               size_t *out_next);

/** Advance one code point forward (same as decode). */
robo_unicode_status_t robo_unicode_utf8_next(const char *src,
                                             size_t len,
                                             size_t offset,
                                             robo_unicode_mode_t mode,
                                             robo_uchar32_t *out_cp,
                                             size_t *out_next);

/**
 * Move to the start of the code point that ends at @p offset (exclusive end),
 * or that contains the last byte before @p offset.
 * @p offset is a byte index 0..len; typically pass current position.
 */
robo_unicode_status_t robo_unicode_utf8_prev(const char *src,
                                             size_t len,
                                             size_t offset,
                                             robo_unicode_mode_t mode,
                                             robo_uchar32_t *out_cp,
                                             size_t *out_start);

/** @return 1 if entire buffer is well-formed UTF-8, else 0. */
int robo_unicode_utf8_validate(const char *src, size_t len);

/**
 * Count Unicode scalar values (STRICT stops and returns 0 with *ok=0 on error;
 * REPLACE counts each replacement as one).
 */
size_t robo_unicode_utf8_count(const char *src,
                               size_t len,
                               robo_unicode_mode_t mode,
                               int *ok);

/* --- UTF-16 --------------------------------------------------------------- */

size_t robo_unicode_utf16_encode_len(robo_uchar32_t cp); /* 1 or 2, or 0 */

robo_unicode_status_t robo_unicode_utf16_encode(robo_uchar32_t cp,
                                                robo_uchar16_t *out,
                                                size_t out_cap,
                                                size_t *written);

robo_unicode_status_t robo_unicode_utf16_decode(const robo_uchar16_t *src,
                                                size_t len,
                                                size_t offset,
                                                robo_unicode_mode_t mode,
                                                robo_uchar32_t *out_cp,
                                                size_t *out_next);

robo_unicode_status_t robo_unicode_utf16_next(const robo_uchar16_t *src,
                                              size_t len,
                                              size_t offset,
                                              robo_unicode_mode_t mode,
                                              robo_uchar32_t *out_cp,
                                              size_t *out_next);

int robo_unicode_utf16_validate(const robo_uchar16_t *src, size_t len);

/* --- UTF-32 --------------------------------------------------------------- */

robo_unicode_status_t robo_unicode_utf32_encode(robo_uchar32_t cp,
                                                robo_uchar32_t *out,
                                                size_t out_cap,
                                                size_t *written);

robo_unicode_status_t robo_unicode_utf32_decode(const robo_uchar32_t *src,
                                                size_t len,
                                                size_t offset,
                                                robo_unicode_mode_t mode,
                                                robo_uchar32_t *out_cp,
                                                size_t *out_next);

int robo_unicode_utf32_validate(const robo_uchar32_t *src, size_t len);

/* --- Conversion (caller-owned buffers) ------------------------------------ */

/**
 * Convert UTF-8 → UTF-16. @p dst_len receives units written (or needed if
 * ROBO_UNICODE_NO_SPACE and dst is NULL / too small — then *dst_len is required).
 * Pass dst=NULL to measure only (returns OK and sets *dst_len).
 */
robo_unicode_status_t robo_unicode_utf8_to_utf16(const char *src,
                                                 size_t src_len,
                                                 robo_uchar16_t *dst,
                                                 size_t dst_cap,
                                                 size_t *dst_len,
                                                 robo_unicode_mode_t mode);

robo_unicode_status_t robo_unicode_utf16_to_utf8(const robo_uchar16_t *src,
                                                 size_t src_len,
                                                 char *dst,
                                                 size_t dst_cap,
                                                 size_t *dst_len,
                                                 robo_unicode_mode_t mode);

robo_unicode_status_t robo_unicode_utf8_to_utf32(const char *src,
                                                 size_t src_len,
                                                 robo_uchar32_t *dst,
                                                 size_t dst_cap,
                                                 size_t *dst_len,
                                                 robo_unicode_mode_t mode);

robo_unicode_status_t robo_unicode_utf32_to_utf8(const robo_uchar32_t *src,
                                                 size_t src_len,
                                                 char *dst,
                                                 size_t dst_cap,
                                                 size_t *dst_len,
                                                 robo_unicode_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_H */
