/*
 * RoboCSS internal helpers (Phase 9.1) — see RoboCSSInternal.h.
 */

#include "RoboCSSInternal.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* ASCII                                                               */
/* ------------------------------------------------------------------ */

int robo_css_ascii_is_space(uint8_t c)
{
    return c == (uint8_t)' ' || c == (uint8_t)'\t' || c == (uint8_t)'\n' ||
           c == (uint8_t)'\f' || c == (uint8_t)'\r';
}

int robo_css_ascii_is_digit(uint8_t c)
{
    return c >= (uint8_t)'0' && c <= (uint8_t)'9';
}

int robo_css_ascii_is_alpha(uint8_t c)
{
    return (c >= (uint8_t)'a' && c <= (uint8_t)'z') ||
           (c >= (uint8_t)'A' && c <= (uint8_t)'Z');
}

uint8_t robo_css_ascii_lower(uint8_t c)
{
    if (c >= (uint8_t)'A' && c <= (uint8_t)'Z') {
        return (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
    }
    return c;
}

int robo_css_bytes_equal_ascii_ci(const uint8_t *bytes, size_t length, const char *ascii)
{
    size_t i;

    if (!bytes || !ascii) {
        return 0;
    }
    for (i = 0u; i < length; i++) {
        uint8_t expected;

        if (ascii[i] == '\0') {
            return 0; /* ascii is shorter than the bytes */
        }
        expected = (uint8_t)ascii[i];
        if (robo_css_ascii_lower(bytes[i]) != robo_css_ascii_lower(expected)) {
            return 0;
        }
    }
    return ascii[length] == '\0';
}

int robo_css_bytes_equal_ascii(const uint8_t *bytes, size_t length, const char *ascii)
{
    if (!bytes || !ascii) {
        return 0;
    }
    if (strlen(ascii) != length) {
        return 0;
    }
    return memcmp(bytes, ascii, length) == 0;
}

void robo_css_copy_label(char *dst, size_t capacity, const char *src)
{
    size_t i;

    if (!dst || capacity == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (i = 0u; i + 1u < capacity && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* Newlines                                                            */
/* ------------------------------------------------------------------ */

int robo_css_is_newline_byte(uint8_t b)
{
    return b == (uint8_t)'\n' || b == (uint8_t)'\r' || b == (uint8_t)'\f';
}

size_t robo_css_newline_length(const uint8_t *bytes, size_t available)
{
    if (!bytes || available == 0u || !robo_css_is_newline_byte(bytes[0])) {
        return 0u;
    }
    /* A CRLF pair is one break; the tokenizer reads it as one even though both
     * bytes stay in the source and in the token's range. */
    if (bytes[0] == (uint8_t)'\r' && available >= 2u && bytes[1] == (uint8_t)'\n') {
        return 2u;
    }
    return 1u;
}

/* ------------------------------------------------------------------ */
/* Escapes                                                             */
/* ------------------------------------------------------------------ */

static int css_hex_digit_value(uint8_t c, uint32_t *out_value)
{
    if (c >= (uint8_t)'0' && c <= (uint8_t)'9') {
        *out_value = (uint32_t)(c - (uint8_t)'0');
        return 1;
    }
    if (c >= (uint8_t)'a' && c <= (uint8_t)'f') {
        *out_value = (uint32_t)(c - (uint8_t)'a') + 10u;
        return 1;
    }
    if (c >= (uint8_t)'A' && c <= (uint8_t)'F') {
        *out_value = (uint32_t)(c - (uint8_t)'A') + 10u;
        return 1;
    }
    return 0;
}

robo_css_escape_t robo_css_escape_scan(const uint8_t *bytes, size_t available)
{
    robo_css_escape_t escape;
    uint32_t value = 0u;
    size_t i = 0u;
    int digits = 0;

    escape.valid = 0;
    escape.is_hex = 0;
    escape.length = 0u;
    escape.code_point = 0u;

    if (!bytes || available < 2u || bytes[0] != (uint8_t)'\\') {
        return escape;
    }
    /* A backslash followed by a newline is not an escape: it is the one case
     * the specification makes a parse error and leaves as a bare delimiter. */
    if (robo_css_is_newline_byte(bytes[1])) {
        return escape;
    }

    escape.valid = 1;

    if (css_hex_digit_value(bytes[1], &value)) {
        escape.is_hex = 1;
        /* The test above answered through `value`; the loop below is what
         * accumulates, and it starts at that same first digit. Zeroing is what
         * stops the first digit from being counted twice — `\41` is `A`, not
         * `D`. */
        value = 0u;
        i = 1u;
        while (i < available && digits < 6) {
            uint32_t digit = 0u;

            if (!css_hex_digit_value(bytes[i], &digit)) {
                break;
            }
            value = (value * 16u) + digit;
            i++;
            digits++;
        }
        /* One whitespace after a hexadecimal escape belongs to the escape; a
         * CRLF pair is one whitespace, not two. */
        if (i < available && robo_css_ascii_is_space(bytes[i])) {
            size_t break_length = robo_css_newline_length(bytes + i, available - i);

            /* A CRLF pair counts as one whitespace; a space, tab or form feed is
             * one byte. robo_css_newline_length reports 0 for those, which is
             * not the same as "nothing to consume" — and it is `i`, already past
             * the first hex digit, that must grow, so the 0 has to be turned
             * into one here. */
            i += (break_length > 0u) ? break_length : 1u;
        }
        escape.length = i;
        /* Zero, a surrogate and anything above U+10FFFF are the specification's
         * three out-of-range cases, and all three become U+FFFD. */
        if (value == 0u || value > 0x10FFFFu ||
            (value >= 0xD800u && value <= 0xDFFFu)) {
            value = 0xFFFDu;
        }
        escape.code_point = value;
        return escape;
    }

    /* Any other escaped character stands for itself: the escape's length is the
     * backslash plus one whole scalar, so a multi-byte character is not cut in
     * half even when its bytes would otherwise read as a delimiter. */
    escape.length = 1u + robo_css_utf8_scalar_length(bytes + 1u, available - 1u);
    {
        uint32_t code_point = 0u;
        size_t next = 0u;

        if (robo_unicode_utf8_decode((const char *)(bytes + 1u), escape.length - 1u, 0u,
                                     ROBO_UNICODE_REPLACE, &code_point, &next) !=
            ROBO_UNICODE_OK) {
            code_point = 0xFFFDu;
        }
        escape.code_point = code_point;
    }
    return escape;
}

int robo_css_utf8_append(uint8_t *out, size_t capacity, size_t *length, uint32_t code_point)
{
    size_t written = 0u;

    if (!out || !length || *length > capacity) {
        return 0;
    }
    /* A code point that cannot be a scalar is written as U+FFFD, which is what
     * CSS does with it; robo_unicode_utf8_encode would refuse it instead. */
    if (code_point > 0x10FFFFu || (code_point >= 0xD800u && code_point <= 0xDFFFu)) {
        code_point = 0xFFFDu;
    }
    if (robo_unicode_utf8_encode((robo_uchar32_t)code_point, (char *)(out + *length),
                                 capacity - *length, &written) != ROBO_UNICODE_OK) {
        return 0;
    }
    *length += written;
    return 1;
}

/* ------------------------------------------------------------------ */
/* UTF-8                                                               */
/* ------------------------------------------------------------------ */

size_t robo_css_utf8_scalar_length(const uint8_t *bytes, size_t available)
{
    uint8_t b0;

    if (!bytes || available == 0u) {
        return 0u;
    }
    b0 = bytes[0];
    if (b0 < 0x80u) {
        return 1u;
    }
    if ((b0 & 0xE0u) == 0xC0u) {
        if (b0 < 0xC2u) {
            return 1u; /* overlong two-byte form */
        }
        if (available < 2u || (bytes[1] & 0xC0u) != 0x80u) {
            return 1u;
        }
        return 2u;
    }
    if ((b0 & 0xF0u) == 0xE0u) {
        if (available < 2u || (bytes[1] & 0xC0u) != 0x80u) {
            return 1u;
        }
        if (b0 == 0xE0u && bytes[1] < 0xA0u) {
            return 1u; /* overlong */
        }
        if (b0 == 0xEDu && bytes[1] >= 0xA0u) {
            return 1u; /* surrogate */
        }
        if (available < 3u || (bytes[2] & 0xC0u) != 0x80u) {
            return 1u;
        }
        return 3u;
    }
    if ((b0 & 0xF8u) == 0xF0u) {
        if (available < 2u || (bytes[1] & 0xC0u) != 0x80u) {
            return 1u;
        }
        if (b0 == 0xF0u && bytes[1] < 0x90u) {
            return 1u; /* overlong */
        }
        if (b0 > 0xF4u) {
            return 1u; /* beyond U+10FFFF */
        }
        if (b0 == 0xF4u && bytes[1] >= 0x90u) {
            return 1u; /* beyond U+10FFFF */
        }
        if (available < 3u || (bytes[2] & 0xC0u) != 0x80u) {
            return 1u;
        }
        if (available < 4u || (bytes[3] & 0xC0u) != 0x80u) {
            return 1u;
        }
        return 4u;
    }
    return 1u;
}

/* ------------------------------------------------------------------ */
/* Collections                                                         */
/* ------------------------------------------------------------------ */

int robo_css_grow(void **items, size_t *capacity, size_t count, size_t element_size)
{
    size_t new_capacity;
    size_t bytes;
    void *grown;

    if (!items || !capacity || element_size == 0u) {
        return 0;
    }
    if (count < *capacity) {
        return 1;
    }
    new_capacity = (*capacity == 0u) ? ROBO_CSS_COLLECTION_MIN_CAPACITY : (*capacity * 2u);
    if (new_capacity <= *capacity) {
        return 0; /* overflow: the collection cannot grow further */
    }
    if (new_capacity > (SIZE_MAX / element_size)) {
        return 0;
    }
    bytes = new_capacity * element_size;
    grown = robo_realloc(*items, bytes);
    if (!grown) {
        return 0;
    }
    *items = grown;
    *capacity = new_capacity;
    return 1;
}
