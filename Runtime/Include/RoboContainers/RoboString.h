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

typedef struct String {
    char *data;
    size_t size;     /* byte length of content (excludes trailing NUL) */
    size_t capacity; /* bytes available for content (excludes trailing NUL) */
} String;

/**
 * Initialize to empty string (""). Always NUL-terminated after success.
 * @return RoboOk, RoboInvalidArgument, or RoboOutOfMemory.
 */
Result StringInit(String *str);

/**
 * Free buffer and reset. Safe on NULL. Call init again before reuse.
 */
void StringDestroy(String *str);

size_t RoboStringSize(const String *str);
size_t StringCapacity(const String *str);

/** @return 1 if size == 0 (or str NULL), else 0. */
int StringEmpty(const String *str);

/**
 * Pointer to NUL-terminated bytes, or NULL if str is NULL / not initialized.
 * Invalidated by set/append/reserve/destroy that reallocate.
 */
const char *StringData(const String *str);

/** Set size to 0; keep capacity; write trailing NUL. Safe on NULL. */
void StringClear(String *str);

/**
 * Ensure content capacity >= @p capacity (bytes, excluding NUL). Never shrinks.
 */
Result StringReserve(String *str, size_t capacity);

/**
 * Replace contents with @p bytes of length @p len.
 * Allows embedded NULs; still NUL-terminates after the @p len bytes.
 * @p bytes may be NULL only when @p len == 0.
 */
Result StringSet(String *str, const void *bytes, size_t len);

/**
 * Append @p bytes of length @p len. Embedded NULs allowed.
 * @p bytes may be NULL only when @p len == 0.
 */
Result StringAppend(String *str, const void *bytes, size_t len);

/** Append a single byte (not necessarily a Unicode code point). */
Result StringAppendChar(String *str, unsigned char ch);

/**
 * Lexicographic compare of byte sequences (by size, not strlen).
 * NULL treated as empty. @return <0, 0, or >0 like memcmp.
 */
int StringCompare(const String *a, const String *b);

/**
 * Initialize @p str and copy a C string.
 * @p cstr NULL → empty string (same as init).
 */
Result StringFromCstr(String *str, const char *cstr);

/**
 * Replace contents from a C string (strlen).
 * @p cstr NULL → clear to empty.
 */
Result StringSetCstr(String *str, const char *cstr);

/**
 * Append a C string (strlen).
 * @p cstr NULL → no-op success.
 */
Result StringAppendCstr(String *str, const char *cstr);

/**
 * Append one Unicode scalar value as UTF-8.
 * @return RoboOk, RoboInvalidArgument, RoboOutOfMemory,
 *         or RoboParseError if @p cp is not a scalar.
 */
Result StringAppendCodepoint(String *str, uint32_t cp);

/**
 * Decode next UTF-8 code point at byte @p offset.
 * @p mode is RoboUnicodeStrict or RoboUnicodeReplace (see RoboUnicode.h).
 * @return 0 on success; non-zero maps to UnicodeStatus values.
 */
int StringNextCodepoint(const String *str,
                               size_t offset,
                               int mode,
                               uint32_t *outCp,
                               size_t *outNext);

/**
 * Count Unicode scalar values in the string (byte size remains RoboStringSize).
 * On STRICT failure, returns 0 and *ok=0.
 */
size_t StringCodepointCount(const String *str, int mode, int *ok);

/** @return 1 if contents are well-formed UTF-8. */
int StringUtf8Valid(const String *str);

/**
 * Count extended grapheme clusters (UAX #29). Not the same as RoboStringSize()
 * or StringCodepointCount().
 */
Result StringGraphemeCount(const String *str,
                                         UnicodeMode mode,
                                         size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_STRING_H */
