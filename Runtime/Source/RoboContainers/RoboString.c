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

#define RoboStringMinCap 16u

static size_t StringCstrLen(const char *cstr)
{
    if (!cstr) {
        return 0;
    }
    return strlen(cstr);
}

static int StringAllocOverflow(size_t contentCap)
{
    /* Need contentCap + 1 for NUL. */
    return contentCap > SIZE_MAX - 1u;
}

static Result StringGrowTo(String *str, size_t newCap)
{
    size_t bytes;
    char *neu;

    if (newCap <= str->capacity && str->data) {
        return RoboOk;
    }
    if (StringAllocOverflow(newCap)) {
        return RoboOutOfMemory;
    }
    bytes = newCap + 1u;
    neu = (char *)Realloc(str->data, bytes);
    if (!neu) {
        return RoboOutOfMemory;
    }
    str->data = neu;
    str->capacity = newCap;
    str->data[str->size] = '\0';
    return RoboOk;
}

static size_t StringNextCapacity(size_t current, size_t needed)
{
    size_t cap = needed < RoboStringMinCap ? RoboStringMinCap : needed;

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

Result StringInit(String *str)
{
    char *buf;

    if (!str) {
        return RoboInvalidArgument;
    }
    buf = (char *)Malloc(1);
    if (!buf) {
        return RoboOutOfMemory;
    }
    buf[0] = '\0';
    str->data = buf;
    str->size = 0;
    str->capacity = 0;
    return RoboOk;
}

void StringDestroy(String *str)
{
    if (!str) {
        return;
    }
    RoboFree(str->data);
    str->data = NULL;
    str->size = 0;
    str->capacity = 0;
}

size_t RoboStringSize(const String *str)
{
    return str ? str->size : 0;
}

size_t StringCapacity(const String *str)
{
    return str ? str->capacity : 0;
}

int StringEmpty(const String *str)
{
    return !str || str->size == 0;
}

const char *StringData(const String *str)
{
    return str ? str->data : NULL;
}

void StringClear(String *str)
{
    if (!str || !str->data) {
        return;
    }
    str->size = 0;
    str->data[0] = '\0';
}

Result StringReserve(String *str, size_t capacity)
{
    if (!str || !str->data) {
        return RoboInvalidArgument;
    }
    if (capacity <= str->capacity) {
        return RoboOk;
    }
    return StringGrowTo(str, capacity);
}

Result StringSet(String *str, const void *bytes, size_t len)
{
    Result r;

    if (!str || !str->data) {
        return RoboInvalidArgument;
    }
    if (len > 0 && !bytes) {
        return RoboInvalidArgument;
    }
    if (len > str->capacity) {
        size_t newCap = StringNextCapacity(str->capacity, len);
        if (StringAllocOverflow(newCap)) {
            if (StringAllocOverflow(len)) {
                return RoboOutOfMemory;
            }
            newCap = len;
        }
        r = StringGrowTo(str, newCap);
        if (r != RoboOk) {
            return r;
        }
    }
    if (len > 0) {
        memcpy(str->data, bytes, len);
    }
    str->size = len;
    str->data[len] = '\0';
    return RoboOk;
}

Result StringAppend(String *str, const void *bytes, size_t len)
{
    Result r;
    size_t needed;
    size_t newCap;

    if (!str || !str->data) {
        return RoboInvalidArgument;
    }
    if (len == 0) {
        return RoboOk;
    }
    if (!bytes) {
        return RoboInvalidArgument;
    }
    if (str->size > SIZE_MAX - len) {
        return RoboOutOfMemory;
    }
    needed = str->size + len;
    if (needed > str->capacity) {
        newCap = StringNextCapacity(str->capacity, needed);
        if (StringAllocOverflow(newCap)) {
            if (StringAllocOverflow(needed)) {
                return RoboOutOfMemory;
            }
            newCap = needed;
        }
        r = StringGrowTo(str, newCap);
        if (r != RoboOk) {
            return r;
        }
    }
    memcpy(str->data + str->size, bytes, len);
    str->size = needed;
    str->data[needed] = '\0';
    return RoboOk;
}

Result StringAppendChar(String *str, unsigned char ch)
{
    return StringAppend(str, &ch, 1);
}

int StringCompare(const String *a, const String *b)
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

Result StringFromCstr(String *str, const char *cstr)
{
    Result r;

    r = StringInit(str);
    if (r != RoboOk) {
        return r;
    }
    r = StringSetCstr(str, cstr);
    if (r != RoboOk) {
        StringDestroy(str);
    }
    return r;
}

Result StringSetCstr(String *str, const char *cstr)
{
    return StringSet(str, cstr, StringCstrLen(cstr));
}

Result StringAppendCstr(String *str, const char *cstr)
{
    size_t len = StringCstrLen(cstr);
    if (len == 0) {
        if (!str || !str->data) {
            return RoboInvalidArgument;
        }
        return RoboOk;
    }
    return StringAppend(str, cstr, len);
}

Result StringAppendCodepoint(String *str, uint32_t cp)
{
    char buf[4];
    size_t n = 0;
    UnicodeStatus st;

    if (!str || !str->data) {
        return RoboInvalidArgument;
    }
    st = UnicodeUtf8Encode((Uchar32)cp, buf, sizeof(buf), &n);
    if (st == RoboUnicodeOutOfRange) {
        return RoboParseError;
    }
    if (st != RoboUnicodeOk) {
        return RoboError;
    }
    return StringAppend(str, buf, n);
}

int StringNextCodepoint(const String *str,
                               size_t offset,
                               int mode,
                               uint32_t *outCp,
                               size_t *outNext)
{
    Uchar32 cp = 0;
    size_t next = 0;
    UnicodeStatus st;

    if (!str || !str->data) {
        return (int)RoboUnicodeInvalid;
    }
    st = UnicodeUtf8Next(str->data, str->size, offset,
                                (UnicodeMode)mode, &cp, &next);
    if (st == RoboUnicodeOk) {
        if (outCp) {
            *outCp = cp;
        }
        if (outNext) {
            *outNext = next;
        }
    }
    return (int)st;
}

size_t StringCodepointCount(const String *str, int mode, int *ok)
{
    if (!str || !str->data) {
        if (ok) {
            *ok = 0;
        }
        return 0;
    }
    return UnicodeUtf8Count(str->data, str->size,
                                   (UnicodeMode)mode, ok);
}

int StringUtf8Valid(const String *str)
{
    if (!str || !str->data) {
        return 0;
    }
    return UnicodeUtf8Validate(str->data, str->size);
}

Result StringGraphemeCount(const String *str,
                                         UnicodeMode mode,
                                         size_t *count)
{
    if (!count) {
        return RoboInvalidArgument;
    }
    if (!str || !str->data) {
        *count = 0;
        return RoboInvalidArgument;
    }
    return UnicodeGraphemeCountUtf8(str->data, str->size, mode, count);
}
