/*
 * RoboInput private declarations (Phase 12.1).
 *
 * Not installed: nothing public includes this, and only this module's own sources do. Two things live here,
 * and nothing else:
 *
 *   **the text writer.** Every dump in this module needs the same three behaviours -- never overflow, never
 *   truncate into something that reads as complete, and always report the length the text *would* have
 *   needed -- and the guest build has no `stdio`, so no `snprintf` is available to lean on. The writer is
 *   therefore a handful of appending operations plus one `finish` that decides whether what was stored is a
 *   whole answer or a refusal, which is the same builder `RoboDesktopDump.c` uses, made shared here because
 *   this module has five writers rather than one;
 *
 *   **the bounded string helpers.** A payload's `name` and `text` are fixed capacities inside structures the
 *   caller may have filled by hand, so "is this NUL-terminated within its capacity?" is a real question and
 *   has to be answered without reading past the end of the array.
 *
 * The module's *state* is deliberately not here, because there is none: no globals, no singleton, no lock.
 * The private header of a module that owns nothing is small on purpose.
 */
#ifndef ROBO_INPUT_INTERNAL_H
#define ROBO_INPUT_INTERNAL_H

#include "RoboInput/RoboInput.h"

#include "RoboBase/RoboLog.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A bounded text builder.
 *
 * `length` always counts what the text *needs*, whether or not it was stored, so a caller learns the full
 * length from a refusal and can size a buffer and try again. At most `capacity - 1` bytes are stored, and
 * the stored bytes are always NUL-terminated while a write is in progress.
 */
typedef struct robo_input_writer {
    char *buffer;
    /** Bytes available, including the NUL that is always kept at @ref length. */
    size_t capacity;
    /** Bytes the text needs, whether or not they fit. */
    size_t length;
} robo_input_writer_t;

static inline void ri_writer_init(robo_input_writer_t *text, char *buffer, size_t capacity)
{
    text->buffer = buffer;
    text->capacity = capacity;
    text->length = 0u;
    if (buffer && capacity > 0u) {
        buffer[0] = '\0';
    }
}

static inline void ri_writer_char(robo_input_writer_t *text, char c)
{
    if (text->buffer && text->length + 1u < text->capacity) {
        text->buffer[text->length] = c;
        text->buffer[text->length + 1u] = '\0';
    }
    text->length++;
}

static inline void ri_writer_text(robo_input_writer_t *text, const char *string)
{
    if (!string) {
        return;
    }
    while (*string != '\0') {
        ri_writer_char(text, *string);
        string++;
    }
}

static inline void ri_writer_u64(robo_input_writer_t *text, uint64_t value)
{
    char digits[24];
    size_t count = 0u;

    if (value == 0u) {
        ri_writer_char(text, '0');
        return;
    }
    while (value > 0u && count < sizeof(digits)) {
        digits[count] = (char)('0' + (int)(value % 10u));
        value /= 10u;
        count++;
    }
    while (count > 0u) {
        count--;
        ri_writer_char(text, digits[count]);
    }
}

static inline void ri_writer_i32(robo_input_writer_t *text, int32_t value)
{
    if (value < 0) {
        ri_writer_char(text, '-');
        /* Negated through the unsigned range so the most negative value prints rather than overflowing on
         * the way. */
        ri_writer_u64(text, (uint64_t)0u - (uint64_t)(int64_t)value);
        return;
    }
    ri_writer_u64(text, (uint64_t)value);
}

/** `key=value` in one call: the shape every field of every dump in this module uses. */
static inline void ri_writer_field_i32(robo_input_writer_t *text, const char *key, int32_t value)
{
    ri_writer_text(text, key);
    ri_writer_char(text, '=');
    ri_writer_i32(text, value);
}

static inline void ri_writer_field_u64(robo_input_writer_t *text, const char *key, uint64_t value)
{
    ri_writer_text(text, key);
    ri_writer_char(text, '=');
    ri_writer_u64(text, value);
}

static inline void ri_writer_field_text(robo_input_writer_t *text, const char *key, const char *value)
{
    ri_writer_text(text, key);
    ri_writer_char(text, '=');
    ri_writer_text(text, value);
}

/** `key=x,y`, the form every point in this tree is written in. */
static inline void ri_writer_field_point(robo_input_writer_t *text,
                                         const char *key,
                                         int32_t x,
                                         int32_t y)
{
    ri_writer_text(text, key);
    ri_writer_char(text, '=');
    ri_writer_i32(text, x);
    ri_writer_char(text, ',');
    ri_writer_i32(text, y);
}

/**
 * Close the text: store nothing new, decide whether it fits, and report the length.
 *
 * A text that does not fit leaves `?` in the buffer rather than the part that fit, so a caller that ignored
 * the refusal cannot read a truncated record as a whole one, and @p out_length receives the length the text
 * needed. Every dump door in this module returns through here.
 */
static inline robo_result_t ri_writer_finish(robo_input_writer_t *text,
                                             size_t *out_length,
                                             robo_input_diag_t *diag)
{
    if (out_length) {
        *out_length = text->length;
    }
    if (text->length >= text->capacity) {
        if (text->buffer && text->capacity > 0u) {
            text->buffer[0] = '?';
            if (text->capacity > 1u) {
                text->buffer[1] = '\0';
            }
        }
        return ROBO_INPUT_DIAG(diag,
                               ROBO_INPUT_DOMAIN_DUMP,
                               ROBO_NOT_SUPPORTED,
                               "duplicate of a dump needing more room than the buffer has");
    }
    return ROBO_OK;
}

/** The usual guard for a dump door's arguments. Non-zero when they are usable. */
static inline int ri_writer_arguments_are_valid(const char *buffer, size_t capacity, size_t *out_length)
{
    return buffer != NULL && capacity > 0u && out_length != NULL;
}

/** `strnlen`: the length of @p string, never reading past @p maximum bytes. */
static inline size_t ri_bounded_strlen(const char *string, size_t maximum)
{
    size_t length = 0u;

    if (!string) {
        return 0u;
    }
    while (length < maximum && string[length] != '\0') {
        length++;
    }
    return length;
}

/** Non-zero when @p string fits in @p capacity bytes *including* its NUL. */
static inline int ri_text_fits(const char *string, size_t capacity)
{
    size_t length = ri_bounded_strlen(string, capacity);

    return string == NULL || length < capacity;
}

/** Copy @p source into @p destination, which must have room for @p capacity bytes including the NUL. */
static inline void ri_text_copy(char *destination, size_t capacity, const char *source)
{
    size_t index = 0u;

    if (!destination || capacity == 0u) {
        return;
    }
    if (source) {
        while (index + 1u < capacity && source[index] != '\0') {
            destination[index] = source[index];
            index++;
        }
    }
    destination[index] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_INTERNAL_H */
