/*
 * RoboString — dynamic UTF-8 byte string (Goal 3.2).
 *
 * Stores UTF-8 as opaque bytes with an explicit byte length. Always
 * NUL-terminated after successful init. Byte length != Unicode character count.
 * Treat struct fields as private; use the accessors below.
 *
 * See containers/README.md.
 */

#ifndef ROBO_STRING_H
#define ROBO_STRING_H

#include "RoboBase/RoboError.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_string {
    char *data;
    size_t size;     /* byte length of content (excludes trailing NUL) */
    size_t capacity; /* bytes available for content (excludes trailing NUL) */
} robo_string_t;

/**
 * Initialize to empty string (""). Always NUL-terminated after success.
 * @return ROBO_OK, ROBO_INVALID_ARGUMENT, or ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_string_init(robo_string_t *str);

/**
 * Free buffer and reset. Safe on NULL. Call init again before reuse.
 */
void robo_string_destroy(robo_string_t *str);

size_t robo_string_size(const robo_string_t *str);
size_t robo_string_capacity(const robo_string_t *str);

/** @return 1 if size == 0 (or str NULL), else 0. */
int robo_string_empty(const robo_string_t *str);

/**
 * Pointer to NUL-terminated bytes, or NULL if str is NULL / not initialized.
 * Invalidated by set/append/reserve/destroy that reallocate.
 */
const char *robo_string_data(const robo_string_t *str);

/** Set size to 0; keep capacity; write trailing NUL. Safe on NULL. */
void robo_string_clear(robo_string_t *str);

/**
 * Ensure content capacity >= @p capacity (bytes, excluding NUL). Never shrinks.
 */
robo_result_t robo_string_reserve(robo_string_t *str, size_t capacity);

/**
 * Replace contents with @p bytes of length @p len.
 * Allows embedded NULs; still NUL-terminates after the @p len bytes.
 * @p bytes may be NULL only when @p len == 0.
 */
robo_result_t robo_string_set(robo_string_t *str, const void *bytes, size_t len);

/**
 * Append @p bytes of length @p len. Embedded NULs allowed.
 * @p bytes may be NULL only when @p len == 0.
 */
robo_result_t robo_string_append(robo_string_t *str, const void *bytes, size_t len);

/** Append a single byte (not necessarily a Unicode code point). */
robo_result_t robo_string_append_char(robo_string_t *str, unsigned char ch);

/**
 * Lexicographic compare of byte sequences (by size, not strlen).
 * NULL treated as empty. @return <0, 0, or >0 like memcmp.
 */
int robo_string_compare(const robo_string_t *a, const robo_string_t *b);

/**
 * Initialize @p str and copy a C string.
 * @p cstr NULL → empty string (same as init).
 */
robo_result_t robo_string_from_cstr(robo_string_t *str, const char *cstr);

/**
 * Replace contents from a C string (strlen).
 * @p cstr NULL → clear to empty.
 */
robo_result_t robo_string_set_cstr(robo_string_t *str, const char *cstr);

/**
 * Append a C string (strlen).
 * @p cstr NULL → no-op success.
 */
robo_result_t robo_string_append_cstr(robo_string_t *str, const char *cstr);

/**
 * Append one Unicode scalar value as UTF-8.
 * @return ROBO_OK, ROBO_INVALID_ARGUMENT, ROBO_OUT_OF_MEMORY,
 *         or ROBO_PARSE_ERROR if @p cp is not a scalar.
 */
robo_result_t robo_string_append_codepoint(robo_string_t *str, uint32_t cp);

/**
 * Decode next UTF-8 code point at byte @p offset.
 * @p mode is ROBO_UNICODE_STRICT or ROBO_UNICODE_REPLACE (see RoboUnicode.h).
 * @return 0 on success; non-zero maps to robo_unicode_status_t values.
 */
int robo_string_next_codepoint(const robo_string_t *str,
                               size_t offset,
                               int mode,
                               uint32_t *out_cp,
                               size_t *out_next);

/**
 * Count Unicode scalar values in the string (byte size remains robo_string_size).
 * On STRICT failure, returns 0 and *ok=0.
 */
size_t robo_string_codepoint_count(const robo_string_t *str, int mode, int *ok);

/** @return 1 if contents are well-formed UTF-8. */
int robo_string_utf8_valid(const robo_string_t *str);

/**
 * Count extended grapheme clusters (UAX #29). Not the same as robo_string_size()
 * or robo_string_codepoint_count().
 */
robo_result_t robo_string_grapheme_count(const robo_string_t *str,
                                         robo_unicode_mode_t mode,
                                         size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_STRING_H */
