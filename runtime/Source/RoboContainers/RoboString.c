/*
 * RoboString — Goal 3.2 dynamic UTF-8 byte string.
 *
 * UTF-8 policy: contents are opaque bytes (no validation). Invalid UTF-8 is
 * stored and compared as bytes. size is always byte length, not character count.
 *
 * Buffer layout: capacity content bytes + 1 trailing NUL (always present after init).
 * Growth: geometric ×2 from a minimum of 16 content bytes (overflow-safe).
 */

#include "RoboContainers/RoboString.h"
#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeGrapheme.h"

#include <stdint.h>
#include <string.h>

#define ROBO_STRING_MIN_CAP 16u

static size_t robo_string_cstr_len(const char *cstr)
{
    if (!cstr) {
        return 0;
    }
    return strlen(cstr);
}

static int robo_string_alloc_overflow(size_t content_cap)
{
    /* Need content_cap + 1 for NUL. */
    return content_cap > SIZE_MAX - 1u;
}

static robo_result_t robo_string_grow_to(robo_string_t *str, size_t new_cap)
{
    size_t bytes;
    char *neu;

    if (new_cap <= str->capacity && str->data) {
        return ROBO_OK;
    }
    if (robo_string_alloc_overflow(new_cap)) {
        return ROBO_OUT_OF_MEMORY;
    }
    bytes = new_cap + 1u;
    neu = (char *)robo_realloc(str->data, bytes);
    if (!neu) {
        return ROBO_OUT_OF_MEMORY;
    }
    str->data = neu;
    str->capacity = new_cap;
    str->data[str->size] = '\0';
    return ROBO_OK;
}

static size_t robo_string_next_capacity(size_t current, size_t needed)
{
    size_t cap = needed < ROBO_STRING_MIN_CAP ? ROBO_STRING_MIN_CAP : needed;

    if (current == 0) {
        return cap;
    }
    while (current < needed) {
        if (current > SIZE_MAX / 2u) {
            return needed;
        }
        current *= 2u;
    }
    if (current < cap) {
        current = cap;
    }
    return current;
}

robo_result_t robo_string_init(robo_string_t *str)
{
    char *buf;

    if (!str) {
        return ROBO_INVALID_ARGUMENT;
    }
    buf = (char *)robo_malloc(1);
    if (!buf) {
        return ROBO_OUT_OF_MEMORY;
    }
    buf[0] = '\0';
    str->data = buf;
    str->size = 0;
    str->capacity = 0;
    return ROBO_OK;
}

void robo_string_destroy(robo_string_t *str)
{
    if (!str) {
        return;
    }
    robo_free(str->data);
    str->data = NULL;
    str->size = 0;
    str->capacity = 0;
}

size_t robo_string_size(const robo_string_t *str)
{
    return str ? str->size : 0;
}

size_t robo_string_capacity(const robo_string_t *str)
{
    return str ? str->capacity : 0;
}

int robo_string_empty(const robo_string_t *str)
{
    return !str || str->size == 0;
}

const char *robo_string_data(const robo_string_t *str)
{
    return str ? str->data : NULL;
}

void robo_string_clear(robo_string_t *str)
{
    if (!str || !str->data) {
        return;
    }
    str->size = 0;
    str->data[0] = '\0';
}

robo_result_t robo_string_reserve(robo_string_t *str, size_t capacity)
{
    if (!str || !str->data) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (capacity <= str->capacity) {
        return ROBO_OK;
    }
    return robo_string_grow_to(str, capacity);
}

robo_result_t robo_string_set(robo_string_t *str, const void *bytes, size_t len)
{
    robo_result_t r;

    if (!str || !str->data) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (len > 0 && !bytes) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (len > str->capacity) {
        size_t new_cap = robo_string_next_capacity(str->capacity, len);
        if (robo_string_alloc_overflow(new_cap)) {
            if (robo_string_alloc_overflow(len)) {
                return ROBO_OUT_OF_MEMORY;
            }
            new_cap = len;
        }
        r = robo_string_grow_to(str, new_cap);
        if (r != ROBO_OK) {
            return r;
        }
    }
    if (len > 0) {
        memcpy(str->data, bytes, len);
    }
    str->size = len;
    str->data[len] = '\0';
    return ROBO_OK;
}

robo_result_t robo_string_append(robo_string_t *str, const void *bytes, size_t len)
{
    robo_result_t r;
    size_t needed;
    size_t new_cap;

    if (!str || !str->data) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (len == 0) {
        return ROBO_OK;
    }
    if (!bytes) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (str->size > SIZE_MAX - len) {
        return ROBO_OUT_OF_MEMORY;
    }
    needed = str->size + len;
    if (needed > str->capacity) {
        new_cap = robo_string_next_capacity(str->capacity, needed);
        if (robo_string_alloc_overflow(new_cap)) {
            if (robo_string_alloc_overflow(needed)) {
                return ROBO_OUT_OF_MEMORY;
            }
            new_cap = needed;
        }
        r = robo_string_grow_to(str, new_cap);
        if (r != ROBO_OK) {
            return r;
        }
    }
    memcpy(str->data + str->size, bytes, len);
    str->size = needed;
    str->data[needed] = '\0';
    return ROBO_OK;
}

robo_result_t robo_string_append_char(robo_string_t *str, unsigned char ch)
{
    return robo_string_append(str, &ch, 1);
}

int robo_string_compare(const robo_string_t *a, const robo_string_t *b)
{
    const char *ad = (a && a->data) ? a->data : "";
    const char *bd = (b && b->data) ? b->data : "";
    size_t asz = a ? a->size : 0;
    size_t bsz = b ? b->size : 0;
    size_t n = asz < bsz ? asz : bsz;
    int cmp;

    if (n > 0) {
        cmp = memcmp(ad, bd, n);
        if (cmp != 0) {
            return cmp;
        }
    }
    if (asz < bsz) {
        return -1;
    }
    if (asz > bsz) {
        return 1;
    }
    return 0;
}

robo_result_t robo_string_from_cstr(robo_string_t *str, const char *cstr)
{
    robo_result_t r;

    r = robo_string_init(str);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_string_set_cstr(str, cstr);
    if (r != ROBO_OK) {
        robo_string_destroy(str);
    }
    return r;
}

robo_result_t robo_string_set_cstr(robo_string_t *str, const char *cstr)
{
    return robo_string_set(str, cstr, robo_string_cstr_len(cstr));
}

robo_result_t robo_string_append_cstr(robo_string_t *str, const char *cstr)
{
    size_t len = robo_string_cstr_len(cstr);
    if (len == 0) {
        if (!str || !str->data) {
            return ROBO_INVALID_ARGUMENT;
        }
        return ROBO_OK;
    }
    return robo_string_append(str, cstr, len);
}

robo_result_t robo_string_append_codepoint(robo_string_t *str, uint32_t cp)
{
    char buf[4];
    size_t n = 0;
    robo_unicode_status_t st;

    if (!str || !str->data) {
        return ROBO_INVALID_ARGUMENT;
    }
    st = robo_unicode_utf8_encode((robo_uchar32_t)cp, buf, sizeof(buf), &n);
    if (st == ROBO_UNICODE_OUT_OF_RANGE) {
        return ROBO_PARSE_ERROR;
    }
    if (st != ROBO_UNICODE_OK) {
        return ROBO_ERROR;
    }
    return robo_string_append(str, buf, n);
}

int robo_string_next_codepoint(const robo_string_t *str,
                               size_t offset,
                               int mode,
                               uint32_t *out_cp,
                               size_t *out_next)
{
    robo_uchar32_t cp = 0;
    size_t next = 0;
    robo_unicode_status_t st;

    if (!str || !str->data) {
        return (int)ROBO_UNICODE_INVALID;
    }
    st = robo_unicode_utf8_next(str->data, str->size, offset,
                                (robo_unicode_mode_t)mode, &cp, &next);
    if (st == ROBO_UNICODE_OK) {
        if (out_cp) {
            *out_cp = cp;
        }
        if (out_next) {
            *out_next = next;
        }
    }
    return (int)st;
}

size_t robo_string_codepoint_count(const robo_string_t *str, int mode, int *ok)
{
    if (!str || !str->data) {
        if (ok) {
            *ok = 0;
        }
        return 0;
    }
    return robo_unicode_utf8_count(str->data, str->size,
                                   (robo_unicode_mode_t)mode, ok);
}

int robo_string_utf8_valid(const robo_string_t *str)
{
    if (!str || !str->data) {
        return 0;
    }
    return robo_unicode_utf8_validate(str->data, str->size);
}

robo_result_t robo_string_grapheme_count(const robo_string_t *str,
                                         robo_unicode_mode_t mode,
                                         size_t *count)
{
    if (!count) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!str || !str->data) {
        *count = 0;
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_unicode_grapheme_count_utf8(str->data, str->size, mode, count);
}
