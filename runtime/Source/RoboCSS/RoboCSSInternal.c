/*
 * RoboCSS internal helpers (Phase 9.1) — see RoboCSSInternal.h.
 */

#include "RoboCSSInternal.h"

#include "RoboMem/RoboMem.h"

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
