/*
 * RoboText's dump writer (Phase 12.4) — see RoboTextInternal.h.
 *
 * Every function here is the same three lines: compute what the append needs, refuse if it does not fit,
 * then write. The refusals are the interesting part — each one returns `RoboTextWriterOverflow` *before*
 * touching the buffer, so a caller that ignores the return value gets a line that is either complete or
 * empty, never one that is half of a record.
 */

#include "RoboTextInternal.h"

#include "RoboMem/RoboMem.h"
#include "RoboText/RoboTextUnicode.h"

#include <string.h>

size_t RoboTextWriterRunRow(char *line,
                          size_t capacity,
                          RoboTextRange range,
                          const char *label,
                          int hasBytes,
                          RoboTextByteIndex byteStart,                            RoboTextByteIndex byteEnd)
{
    /* One row, with or without the byte span. Returns the row's length or RoboTextWriterOverflow. */
    size_t length = 0u;

    length = RoboTextWriterText(line, capacity, length, "[");
    length = RoboTextWriterUnsigned(line, capacity, length, range.start);
    length = RoboTextWriterText(line, capacity, length, "..");
    length = RoboTextWriterUnsigned(line, capacity, length, range.end);
    length = RoboTextWriterText(line, capacity, length, ") scalars");
    if (hasBytes) {
        length = RoboTextWriterText(line, capacity, length, "  bytes [");
        length = RoboTextWriterUnsigned(line, capacity, length, byteStart);
        length = RoboTextWriterText(line, capacity, length, "..");
        length = RoboTextWriterUnsigned(line, capacity, length, byteEnd);
        length = RoboTextWriterText(line, capacity, length, ")");
    }
    length = RoboTextWriterText(line, capacity, length, "  ");
    return RoboTextWriterText(line, capacity, length, label);
}

Result RoboTextWriterRunTable(RoboTextView view,
                              const void *runs,
                              size_t count,
                              RoboTextRunTableRow row,
                              const char *header,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength)
{
    RoboTextByteIndex *offsets = NULL;
    RoboTextScalarIndex scalarCount = 0u;
    size_t required = 0u;
    int hasBytes = (view.bytes != NULL);

    if (outLength) {
        *outLength = 0u;
    }
    if (!runs || !row || !header || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';
    if (hasBytes) {
        Result status = RoboTextViewScalarCount(view, &scalarCount, NULL);

        if (status != RoboOk) {
            return status;
        }
        for (size_t index = 0u; index < count; index++) {
            RoboTextRange range;
            const char *label = NULL;

            row(runs, index, &range, &label);
            if (RoboTextRangeIsInverted(range) || !RoboTextRangeIsWithin(range, scalarCount)) {
                return RoboInvalidArgument;
            }
        }
        status = RoboTextWriterByteOffsets(view, (size_t)scalarCount, &offsets, NULL);
        if (status != RoboOk) {
            return status;
        }
    }

    /* Measuring pass: every row is built into one reusable line and its length summed, so a buffer that
     * cannot hold the table is left untouched rather than half written. */
    {
        char line[160];

        required = RoboTextWriterText(line, sizeof(line), 0u, header);
        if (required == RoboTextWriterOverflow) {
            RoboFree(offsets);
            return RoboNotSupported;
        }
        for (size_t index = 0u; index < count; index++) {
            RoboTextRange range;
            const char *label = NULL;
            size_t rowLength;

            row(runs, index, &range, &label);
            rowLength = RoboTextWriterRunRow(line, sizeof(line), range, label, hasBytes,
                                             hasBytes ? offsets[range.start] : 0u,
                                             hasBytes ? offsets[range.end] : 0u);
            if (rowLength == RoboTextWriterOverflow) {
                RoboFree(offsets);
                return RoboNotSupported;
            }
            required += rowLength + 1u; /* + newline */
        }
    }

    if (required + 1u > capacity) {
        if (outLength) {
            *outLength = required;
        }
        RoboFree(offsets);
        return RoboNotSupported;
    }

    {
        char line[160];
        size_t length = 0u;

        length = RoboTextWriterText(buffer, capacity, length, header);
        for (size_t index = 0u; index < count; index++) {
            RoboTextRange range;
            const char *label = NULL;
            size_t rowLength;

            row(runs, index, &range, &label);
            rowLength = RoboTextWriterRunRow(line, sizeof(line), range, label, hasBytes,
                                             hasBytes ? offsets[range.start] : 0u,
                                             hasBytes ? offsets[range.end] : 0u);
            memcpy(buffer + length, line, rowLength);
            length += rowLength;
            buffer[length++] = '\n';
        }
        buffer[length] = '\0';
        if (outLength) {
            *outLength = length;
        }
    }
    RoboFree(offsets);
    return RoboOk;
}

Result RoboTextWriterByteOffsets(RoboTextView view,
                                 size_t scalarCount,
                                 RoboTextByteIndex **outOffsets,
                                 RoboTextDiag *diag)
{
    RoboTextUnicodeDecoder decoder;
    RoboTextByteIndex *offsets;
    Result status;

    if (!outOffsets) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "an out offset array is required");
    }
    *outOffsets = NULL;
    if (scalarCount == 0u) {
        return RoboOk;
    }
    offsets = (RoboTextByteIndex *)RoboCalloc(scalarCount + 1u, sizeof(*offsets));
    if (!offsets) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboOutOfMemory,
                               "the byte offsets could not be allocated");
    }
    status = RoboTextUnicodeDecoderInit(&decoder, view, diag);
    if (status != RoboOk) {
        RoboFree(offsets);
        return status;
    }
    offsets[0] = 0u;
    for (size_t index = 0u; index < scalarCount; index++) {
        if (!RoboTextUnicodeDecoderHasNext(&decoder)) {
            /* The runs describe more scalars than the view holds; the table is refused rather than filled
             * with offsets that belong to a different text. */
            RoboFree(offsets);
            return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                                   "the runs cover more scalars than the text holds");
        }
        (void)RoboTextUnicodeDecoderNext(&decoder);
        offsets[index + 1u] = (RoboTextByteIndex)decoder.lastEnd;
    }
    *outOffsets = offsets;
    return RoboOk;
}

size_t RoboTextWriterText(char *buffer, size_t capacity, size_t length, const char *text)
{
    size_t need;

    if (!buffer || !text || length == RoboTextWriterOverflow) {
        return RoboTextWriterOverflow;
    }
    need = strlen(text);
    /* `+ 1` for the terminator, which stays inside `capacity` at every point a caller can observe the
     * buffer: a dump is read as a C string, and one without a terminator would be read past its end. */
    if (length + need + 1u > capacity) {
        return RoboTextWriterOverflow;
    }
    memcpy(buffer + length, text, need);
    buffer[length + need] = '\0';
    return length + need;
}

size_t RoboTextWriterUnsigned(char *buffer, size_t capacity, size_t length, uint32_t value)
{
    char digits[10];
    size_t count = 0u;

    if (!buffer || length == RoboTextWriterOverflow) {
        return RoboTextWriterOverflow;
    }
    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);
    if (length + count + 1u > capacity) {
        return RoboTextWriterOverflow;
    }
    for (size_t index = 0u; index < count; index++) {
        buffer[length + index] = digits[count - 1u - index];
    }
    length += count;
    buffer[length] = '\0';
    return length;
}

size_t RoboTextWriterSigned(char *buffer, size_t capacity, size_t length, int32_t value)
{
    if (value < 0) {
        /*
         * Negated in `uint32_t` rather than as an `int32_t`, because `-INT32_MIN` is not representable and
         * the usual `-value` would therefore be undefined for exactly one input. The width is unsigned for
         * the same reason: `(uint32_t)value` of a negative value is its two's-complement pattern.
         */
        uint32_t magnitude = (uint32_t)(-(int64_t)value);

        length = RoboTextWriterText(buffer, capacity, length, "-");
        return RoboTextWriterUnsigned(buffer, capacity, length, magnitude);
    }
    return RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)value);
}

size_t RoboTextWriterFloat(char *buffer, size_t capacity, size_t length, float value)
{
    char scratch[40];
    size_t at = 0u;
    int negative = 0;
    double magnitude;
    uint64_t whole;
    uint32_t fraction;

    if (!buffer || length == RoboTextWriterOverflow) {
        return RoboTextWriterOverflow;
    }
    if (value != value) {
        return RoboTextWriterText(buffer, capacity, length, "nan");
    }
    if (value > 3.0e9f || value < -3.0e9f) {
        return RoboTextWriterText(buffer, capacity, length, value < 0.0f ? "-inf" : "inf");
    }
    magnitude = (double)value;
    if (magnitude < 0.0) {
        negative = 1;
        magnitude = -magnitude;
    }
    whole = (uint64_t)magnitude;
    /* Half-up on the hundredths, then a carry that can move the whole part: `0.999` must print `1.00`, and
     * printing `1.100` would be a spelling no reader decodes. */
    fraction = (uint32_t)((magnitude - (double)whole) * 100.0 + 0.5);
    if (fraction >= 100u) {
        fraction = 0u;
        whole += 1u;
    }
    if (negative && (whole != 0u || fraction != 0u)) {
        scratch[at++] = '-';
    }
    {
        char digits[20];
        size_t count = 0u;

        do {
            digits[count++] = (char)('0' + (int)(whole % 10u));
            whole /= 10u;
        } while (whole != 0u);
        while (count > 0u) {
            scratch[at++] = digits[--count];
        }
    }
    scratch[at++] = '.';
    scratch[at++] = (char)('0' + (int)((fraction / 10u) % 10u));
    scratch[at++] = (char)('0' + (int)(fraction % 10u));
    scratch[at] = '\0';
    return RoboTextWriterText(buffer, capacity, length, scratch);
}
