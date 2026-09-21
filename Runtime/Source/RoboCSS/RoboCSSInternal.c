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

int CssAsciiIsSpace(uint8_t c)
{
    return c == (uint8_t)' ' || c == (uint8_t)'\t' || c == (uint8_t)'\n' ||
           c == (uint8_t)'\f' || c == (uint8_t)'\r';
}

int CssAsciiIsDigit(uint8_t c)
{
    return c >= (uint8_t)'0' && c <= (uint8_t)'9';
}

int CssAsciiIsAlpha(uint8_t c)
{
    return (c >= (uint8_t)'a' && c <= (uint8_t)'z') ||
           (c >= (uint8_t)'A' && c <= (uint8_t)'Z');
}

uint8_t CssAsciiLower(uint8_t c)
{
    if (c >= (uint8_t)'A' && c <= (uint8_t)'Z') {
        return (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
    }
    return c;
}

int CssBytesEqualAsciiCi(const uint8_t *bytes, size_t length, const char *ascii)
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
        if (CssAsciiLower(bytes[i]) != CssAsciiLower(expected)) {
            return 0;
        }
    }
    return ascii[length] == '\0';
}

int CssBytesEqualAscii(const uint8_t *bytes, size_t length, const char *ascii)
{
    if (!bytes || !ascii) {
        return 0;
    }
    if (strlen(ascii) != length) {
        return 0;
    }
    return memcmp(bytes, ascii, length) == 0;
}

void CssCopyLabel(char *dst, size_t capacity, const char *src)
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

int CssIsNewlineByte(uint8_t b)
{
    return b == (uint8_t)'\n' || b == (uint8_t)'\r' || b == (uint8_t)'\f';
}

size_t CssNewlineLength(const uint8_t *bytes, size_t available)
{
    if (!bytes || available == 0u || !CssIsNewlineByte(bytes[0])) {
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

static int CssHexDigitValue(uint8_t c, uint32_t *outValue)
{
    if (c >= (uint8_t)'0' && c <= (uint8_t)'9') {
        *outValue = (uint32_t)(c - (uint8_t)'0');
        return 1;
    }
    if (c >= (uint8_t)'a' && c <= (uint8_t)'f') {
        *outValue = (uint32_t)(c - (uint8_t)'a') + 10u;
        return 1;
    }
    if (c >= (uint8_t)'A' && c <= (uint8_t)'F') {
        *outValue = (uint32_t)(c - (uint8_t)'A') + 10u;
        return 1;
    }
    return 0;
}

CssEscape CssEscapeScan(const uint8_t *bytes, size_t available)
{
    CssEscape escape;
    uint32_t value = 0u;
    size_t i = 0u;
    int digits = 0;

    escape.valid = 0;
    escape.isHex = 0;
    escape.length = 0u;
    escape.codePoint = 0u;

    if (!bytes || available < 2u || bytes[0] != (uint8_t)'\\') {
        return escape;
    }
    /* A backslash followed by a newline is not an escape: it is the one case
     * the specification makes a parse error and leaves as a bare delimiter. */
    if (CssIsNewlineByte(bytes[1])) {
        return escape;
    }

    escape.valid = 1;

    if (CssHexDigitValue(bytes[1], &value)) {
        escape.isHex = 1;
        /* The test above answered through `value`; the loop below is what
         * accumulates, and it starts at that same first digit. Zeroing is what
         * stops the first digit from being counted twice — `\41` is `A`, not
         * `D`. */
        value = 0u;
        i = 1u;
        while (i < available && digits < 6) {
            uint32_t digit = 0u;

            if (!CssHexDigitValue(bytes[i], &digit)) {
                break;
            }
            value = (value * 16u) + digit;
            i++;
            digits++;
        }
        /* One whitespace after a hexadecimal escape belongs to the escape; a
         * CRLF pair is one whitespace, not two. */
        if (i < available && CssAsciiIsSpace(bytes[i])) {
            size_t breakLength = CssNewlineLength(bytes + i, available - i);

            /* A CRLF pair counts as one whitespace; a space, tab or form feed is
             * one byte. CssNewlineLength reports 0 for those, which is
             * not the same as "nothing to consume" — and it is `i`, already past
             * the first hex digit, that must grow, so the 0 has to be turned
             * into one here. */
            i += (breakLength > 0u) ? breakLength : 1u;
        }
        escape.length = i;
        /* Zero, a surrogate and anything above U+10FFFF are the specification's
         * three out-of-range cases, and all three become U+FFFD. */
        if (value == 0u || value > 0x10FFFFu ||
            (value >= 0xD800u && value <= 0xDFFFu)) {
            value = 0xFFFDu;
        }
        escape.codePoint = value;
        return escape;
    }

    /* Any other escaped character stands for itself: the escape's length is the
     * backslash plus one whole scalar, so a multi-byte character is not cut in
     * half even when its bytes would otherwise read as a delimiter. */
    escape.length = 1u + CssUtf8ScalarLength(bytes + 1u, available - 1u);
    {
        uint32_t codePoint = 0u;
        size_t next = 0u;

        if (UnicodeUtf8Decode((const char *)(bytes + 1u), escape.length - 1u, 0u,
                                     RoboUnicodeReplace, &codePoint, &next) !=
            RoboUnicodeOk) {
            codePoint = 0xFFFDu;
        }
        escape.codePoint = codePoint;
    }
    return escape;
}

int CssUtf8Append(uint8_t *out, size_t capacity, size_t *length, uint32_t codePoint)
{
    size_t written = 0u;

    if (!out || !length || *length > capacity) {
        return 0;
    }
    /* A code point that cannot be a scalar is written as U+FFFD, which is what
     * CSS does with it; UnicodeUtf8Encode would refuse it instead. */
    if (codePoint > 0x10FFFFu || (codePoint >= 0xD800u && codePoint <= 0xDFFFu)) {
        codePoint = 0xFFFDu;
    }
    if (UnicodeUtf8Encode((Uchar32)codePoint, (char *)(out + *length),
                                 capacity - *length, &written) != RoboUnicodeOk) {
        return 0;
    }
    *length += written;
    return 1;
}

/* ------------------------------------------------------------------ */
/* UTF-8                                                               */
/* ------------------------------------------------------------------ */

size_t CssUtf8ScalarLength(const uint8_t *bytes, size_t available)
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

int CssGrow(void **items, size_t *capacity, size_t count, size_t elementSize)
{
    size_t newCapacity;
    size_t bytes;
    void *grown;

    if (!items || !capacity || elementSize == 0u) {
        return 0;
    }
    if (count < *capacity) {
        return 1;
    }
    newCapacity = (*capacity == 0u) ? RoboCssCollectionMinCapacity : (*capacity * 2u);
    if (newCapacity <= *capacity) {
        return 0; /* overflow: the collection cannot grow further */
    }
    if (newCapacity > (SIZE_MAX / elementSize)) {
        return 0;
    }
    bytes = newCapacity * elementSize;
    grown = Realloc(*items, bytes);
    if (!grown) {
        return 0;
    }
    *items = grown;
    *capacity = newCapacity;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Reading a component-value tree                                      */
/* ------------------------------------------------------------------ */

int CssValueIsTrivia(const CssValue *value)
{
    if (!value) {
        return 1;
    }
    return value->kind == RoboCssValueKindToken &&
           (value->token.type == RoboCssTokenWhitespace ||
            value->token.type == RoboCssTokenComment);
}

const CssValue *CssValueSingle(const CssValue *value)
{
    const CssValue *found = NULL;
    size_t index;

    if (!value || value->kind == RoboCssValueKindNone) {
        return NULL;
    }
    if (value->kind != RoboCssValueKindList) {
        return value;
    }
    for (index = 0u; index < value->children.count; index++) {
        const CssValue *child = &value->children.items[index];

        if (CssValueIsTrivia(child)) {
            continue;
        }
        if (found) {
            return NULL;
        }
        found = child;
    }
    return found;
}

int CssValueFunctionNamed(const CssValue *value, const char *name)
{
    if (!value || !name || value->kind != RoboCssValueKindFunction) {
        return 0;
    }
    return CssBytesEqualAsciiCi(value->name.bytes, value->name.length, name);
}

int CssTokenKeywordText(const CssToken *token,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength)
{
    size_t length;
    size_t index;

    if (!token || !buffer || capacity == 0u) {
        return 0;
    }
    length = CssTokenValueLength(token);
    if (length == 0u || length >= capacity) {
        return 0;
    }
    if (CssTokenValueCopy(token, (uint8_t *)buffer, length, outLength,
                                  NULL) != RoboOk) {
        return 0;
    }
    for (index = 0u; index < length; index++) {
        buffer[index] = (char)CssAsciiLower((uint8_t)buffer[index]);
    }
    buffer[length] = '\0';
    if (outLength) {
        *outLength = length;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Numbers                                                             */
/* ------------------------------------------------------------------ */

int CssNumberIsFinite(double value)
{
    return value == value && (value - value) == 0.0;
}

double CssNumberClamp(double value, double low, double high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

int CssNumberRound(double value)
{
    return (int)(value < 0.0 ? value - 0.5 : value + 0.5);
}
