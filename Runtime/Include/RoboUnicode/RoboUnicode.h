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
typedef uint32_t Uchar32;

/** UTF-16 code unit. */
typedef uint16_t Uchar16;

#define RoboUchar32Replacement ((Uchar32)0xFFFDu)
#define RoboUchar32Max ((Uchar32)0x10FFFFu)

typedef enum UnicodeStatus {
    RoboUnicodeOk = 0,
    RoboUnicodeTruncated = 1,   /* incomplete sequence at end of input */
    RoboUnicodeInvalid = 2,     /* ill-formed / overlong / bad surrogate */
    RoboUnicodeNoSpace = 3,    /* output buffer too small */
    RoboUnicodeOutOfRange = 4 /* not a Unicode scalar value */
} UnicodeStatus;

/**
 * STRICT — malformed input yields error status; no replacement emitted.
 * REPLACE — emit U+FFFD and advance by a defined amount (see README).
 */
typedef enum UnicodeMode {
    RoboUnicodeStrict = 0,
    RoboUnicodeReplace = 1
} UnicodeMode;

const char *UnicodeVersionString(void);

int UnicodeIsSurrogate(Uchar32 cp);
/** @return 1 if cp is a Unicode scalar value (assignable to UTF-8/16/32). */
int UnicodeIsScalar(Uchar32 cp);

/* --- UTF-8 ---------------------------------------------------------------- */

/** Bytes needed to encode @p cp (1..4), or 0 if not a scalar. */
size_t UnicodeUtf8EncodeLen(Uchar32 cp);

/**
 * Encode scalar @p cp into @p out.
 * @param written optional; set to bytes written on success.
 */
UnicodeStatus UnicodeUtf8Encode(Uchar32 cp,
                                               char *out,
                                               size_t outCap,
                                               size_t *written);

/**
 * Decode one UTF-8 sequence starting at @p offset within @p src[0..len).
 * On success / REPLACE, sets *outCp and *outNext (byte index after sequence).
 */
UnicodeStatus UnicodeUtf8Decode(const char *src,
                                               size_t len,
                                               size_t offset,
                                               UnicodeMode mode,
                                               Uchar32 *outCp,
                                               size_t *outNext);

/** Advance one code point forward (same as decode). */
UnicodeStatus UnicodeUtf8Next(const char *src,
                                             size_t len,
                                             size_t offset,
                                             UnicodeMode mode,
                                             Uchar32 *outCp,
                                             size_t *outNext);

/**
 * Move to the start of the code point that ends at @p offset (exclusive end),
 * or that contains the last byte before @p offset.
 * @p offset is a byte index 0..len; typically pass current position.
 */
UnicodeStatus UnicodeUtf8Prev(const char *src,
                                             size_t len,
                                             size_t offset,
                                             UnicodeMode mode,
                                             Uchar32 *outCp,
                                             size_t *outStart);

/** @return 1 if entire buffer is well-formed UTF-8, else 0. */
int UnicodeUtf8Validate(const char *src, size_t len);

/**
 * Count Unicode scalar values (STRICT stops and returns 0 with *ok=0 on error;
 * REPLACE counts each replacement as one).
 */
size_t UnicodeUtf8Count(const char *src,
                               size_t len,
                               UnicodeMode mode,
                               int *ok);

/* --- UTF-16 --------------------------------------------------------------- */

size_t UnicodeUtf16EncodeLen(Uchar32 cp); /* 1 or 2, or 0 */

UnicodeStatus UnicodeUtf16Encode(Uchar32 cp,
                                                Uchar16 *out,
                                                size_t outCap,
                                                size_t *written);

UnicodeStatus UnicodeUtf16Decode(const Uchar16 *src,
                                                size_t len,
                                                size_t offset,
                                                UnicodeMode mode,
                                                Uchar32 *outCp,
                                                size_t *outNext);

UnicodeStatus UnicodeUtf16Next(const Uchar16 *src,
                                              size_t len,
                                              size_t offset,
                                              UnicodeMode mode,
                                              Uchar32 *outCp,
                                              size_t *outNext);

int UnicodeUtf16Validate(const Uchar16 *src, size_t len);

/* --- UTF-32 --------------------------------------------------------------- */

UnicodeStatus UnicodeUtf32Encode(Uchar32 cp,
                                                Uchar32 *out,
                                                size_t outCap,
                                                size_t *written);

UnicodeStatus UnicodeUtf32Decode(const Uchar32 *src,
                                                size_t len,
                                                size_t offset,
                                                UnicodeMode mode,
                                                Uchar32 *outCp,
                                                size_t *outNext);

int UnicodeUtf32Validate(const Uchar32 *src, size_t len);

/* --- Conversion (caller-owned buffers) ------------------------------------ */

/**
 * Convert UTF-8 → UTF-16. @p dstLen receives units written (or needed if
 * RoboUnicodeNoSpace and dst is NULL / too small — then *dstLen is required).
 * Pass dst=NULL to measure only (returns OK and sets *dstLen).
 */
UnicodeStatus UnicodeUtf8ToUtf16(const char *src,
                                                 size_t srcLen,
                                                 Uchar16 *dst,
                                                 size_t dstCap,
                                                 size_t *dstLen,
                                                 UnicodeMode mode);

UnicodeStatus UnicodeUtf16ToUtf8(const Uchar16 *src,
                                                 size_t srcLen,
                                                 char *dst,
                                                 size_t dstCap,
                                                 size_t *dstLen,
                                                 UnicodeMode mode);

UnicodeStatus UnicodeUtf8ToUtf32(const char *src,
                                                 size_t srcLen,
                                                 Uchar32 *dst,
                                                 size_t dstCap,
                                                 size_t *dstLen,
                                                 UnicodeMode mode);

UnicodeStatus UnicodeUtf32ToUtf8(const Uchar32 *src,
                                                 size_t srcLen,
                                                 char *dst,
                                                 size_t dstCap,
                                                 size_t *dstLen,
                                                 UnicodeMode mode);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_H */
