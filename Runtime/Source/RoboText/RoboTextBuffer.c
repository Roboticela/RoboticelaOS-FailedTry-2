/*
 * RoboText's buffer (Phase 12.4) — see RoboTextBuffer.h.
 *
 * Two things live here and they share one decoder: the owned buffer with its scalar index, and the
 * borrowed view. The view has no index and therefore no cache — it decodes to answer — and that is a
 * deliberate asymmetry rather than an omission: a view exists so that measuring a DOM node does not copy
 * its bytes, and a view that built an index would allocate on every question, which is the cost the view
 * was introduced to avoid. A caller that asks a view for many scalars in order walks it, which is what
 * the run segmenter does.
 *
 * The scalar index is `n + 1` byte offsets for `n` scalars: the start of each scalar, plus the end of
 * the text. The last entry is what makes `ScalarToByte(n)` — the caret position one past the last
 * scalar — total, which is the property the header argues for and the reason an empty range at the end
 * of a text is expressible.
 */

#include "RoboText/RoboTextBuffer.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"

#include <string.h>

/** Bytes a buffer holds before its first growth. */
#define TextBufferInitialCapacity 64u

struct RoboTextBuffer {
    /** UTF-8, NUL-terminated for the benefit of callers that want a C string out of it. */
    char *bytes;
    /** Bytes held, excluding the terminator. */
    size_t length;
    /** Bytes allocated, including the terminator's slot. */
    size_t capacity;
    /**
     * Scalar index: `scalarCount + 1` byte offsets, built on first use and invalidated by any edit.
     *
     * NULL with `indexBuilt == 0` is "not built yet"; NULL with `indexBuilt == 1` is the empty text,
     * whose index is the single entry 0.
     */
    uint32_t *scalarStarts;
    uint32_t scalarCount;
    int indexBuilt;
};

/* ------------------------------------------------------------------ */
/* The decoder                                                         */
/* ------------------------------------------------------------------ */

/**
 * Decode one scalar at byte @p offset.
 *
 * REPLACE mode, and the `next <= offset` guard is not paranoia: a decoder that answered without
 * advancing the cursor would make every "walk the text" loop in this module spin, and a spin is worse
 * than a wrong character because it never returns.
 */
static Result TextDecodeAt(const char *bytes,
                           size_t length,
                           size_t offset,
                           Uchar32 *outScalar,
                           size_t *outNext)
{
    Uchar32 scalar = 0;
    size_t next = offset;

    if (offset >= length) {
        return RoboInvalidArgument;
    }
    if (UnicodeUtf8Decode(bytes, length, offset, RoboUnicodeReplace, &scalar, &next) !=
            RoboUnicodeOk ||
        next <= offset) {
        return RoboParseError;
    }
    if (outScalar) {
        *outScalar = scalar;
    }
    if (outNext) {
        *outNext = next;
    }
    return RoboOk;
}

/** Scalars in @p bytes; RoboParseError only for the decoder's own non-advance. */
static Result TextCountScalars(const char *bytes, size_t length, uint32_t *outCount)
{
    size_t offset = 0u;
    uint32_t count = 0u;

    while (offset < length) {
        size_t next = offset;

        if (TextDecodeAt(bytes, length, offset, NULL, &next) != RoboOk) {
            return RoboParseError;
        }
        offset = next;
        count++;
    }
    if (outCount) {
        *outCount = count;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Growth                                                              */
/* ------------------------------------------------------------------ */

/** Make room for @p extra more bytes beyond the current length, terminator included. */
static Result TextBufferReserve(RoboTextBuffer *buffer, size_t extra, RoboTextDiag *diag)
{
    size_t needed;
    size_t capacity;
    char *grown;

    if (extra > (size_t)RoboTextIndexMax - buffer->length) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "the text is longer than this build can index");
    }
    needed = buffer->length + extra + 1u; /* + 1 for the terminator. */
    if (needed <= buffer->capacity) {
        return RoboOk;
    }
    capacity = (buffer->capacity < TextBufferInitialCapacity) ? TextBufferInitialCapacity
                                                             : buffer->capacity;
    while (capacity < needed) {
        capacity *= 2u;
    }
    grown = (char *)Realloc(buffer->bytes, capacity);
    if (!grown) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboOutOfMemory,
                               "the text buffer could not grow");
    }
    buffer->bytes = grown;
    buffer->capacity = capacity;
    return RoboOk;
}

/** Drop the scalar index. Every mutation goes through here, which is what keeps it from going stale. */
static void TextBufferInvalidateIndex(RoboTextBuffer *buffer)
{
    if (buffer->scalarStarts) {
        RoboFree(buffer->scalarStarts);
        buffer->scalarStarts = NULL;
    }
    buffer->scalarCount = 0u;
    buffer->indexBuilt = 0;
}

/**
 * Build the scalar index if it is not built.
 *
 * O(bytes) once per mutation, and every scalar-indexed door pays it at most once: `ScalarToByte` is then
 * one array read and `ScalarAt` is one decode at a known offset.
 */
static Result TextBufferBuildIndex(RoboTextBuffer *buffer, RoboTextDiag *diag)
{
    uint32_t count = 0u;
    uint32_t *starts;
    size_t offset = 0u;
    uint32_t index = 0u;

    if (buffer->indexBuilt) {
        return RoboOk;
    }
    if (TextCountScalars(buffer->bytes, buffer->length, &count) != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboParseError,
                               "the buffer's bytes could not be decoded");
    }
    if (count > 0u) {
        starts = (uint32_t *)RoboCalloc((size_t)count + 1u, sizeof(*starts));
        if (!starts) {
            return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboOutOfMemory,
                                   "the text buffer's scalar index could not be allocated");
        }
    } else {
        starts = NULL;
    }

    while (offset < buffer->length) {
        size_t next = offset;

        if (TextDecodeAt(buffer->bytes, buffer->length, offset, NULL, &next) != RoboOk) {
            RoboFree(starts);
            return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboParseError,
                                   "the buffer's bytes could not be decoded");
        }
        starts[index++] = (uint32_t)offset;
        offset = next;
    }
    if (starts) {
        starts[count] = (uint32_t)buffer->length; /* The caret one past the last scalar. */
    }
    buffer->scalarStarts = starts;
    buffer->scalarCount = count;
    buffer->indexBuilt = 1;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

Result RoboTextBufferCreate(RoboTextBuffer **out, RoboTextDiag *diag)
{
    RoboTextBuffer *buffer;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "an out pointer is required");
    }
    *out = NULL;
    buffer = (RoboTextBuffer *)RoboCalloc(1u, sizeof(*buffer));
    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboOutOfMemory,
                               "the text buffer could not be allocated");
    }
    buffer->bytes = (char *)RoboCalloc(TextBufferInitialCapacity, 1u);
    if (!buffer->bytes) {
        RoboFree(buffer);
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboOutOfMemory,
                               "the text buffer's first block could not be allocated");
    }
    buffer->capacity = TextBufferInitialCapacity;
    *out = buffer;
    return RoboOk;
}

Result RoboTextBufferCreateFrom(RoboTextBuffer **out,
                                const char *utf8,
                                size_t length,
                                RoboTextDiag *diag)
{
    RoboTextBuffer *buffer = NULL;
    Result status;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "an out pointer is required");
    }
    *out = NULL;
    if (!utf8 && length != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "bytes are required for a non-empty text");
    }
    status = RoboTextBufferCreate(&buffer, diag);
    if (status != RoboOk) {
        return status;
    }
    status = RoboTextBufferAppend(buffer, utf8, length, diag);
    if (status != RoboOk) {
        RoboTextBufferDestroy(buffer);
        return status;
    }
    *out = buffer;
    return RoboOk;
}

Result RoboTextBufferCreateFromScalars(RoboTextBuffer **out,
                                       const Uchar32 *scalars,
                                       size_t count,
                                       RoboTextDiag *diag)
{
    RoboTextBuffer *buffer = NULL;
    Result status;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "an out pointer is required");
    }
    *out = NULL;
    if (!scalars && count != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "scalars are required for a non-empty text");
    }
    status = RoboTextBufferCreate(&buffer, diag);
    if (status != RoboOk) {
        return status;
    }
    for (size_t index = 0u; index < count; index++) {
        /*
         * A lone surrogate is refused rather than replaced. A caller holding `Uchar32` values has already
         * decided what they are, and the DOM above will never hand one across: silently turning it into
         * U+FFFD would lose a decision somebody made and could not see.
         */
        if (!UnicodeIsScalar(scalars[index])) {
            RoboTextBufferDestroy(buffer);
            return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                                   "a value that is not a Unicode scalar cannot be text");
        }
        status = RoboTextBufferAppendScalar(buffer, scalars[index], diag);
        if (status != RoboOk) {
            RoboTextBufferDestroy(buffer);
            return status;
        }
    }
    *out = buffer;
    return RoboOk;
}

void RoboTextBufferDestroy(RoboTextBuffer *buffer)
{
    if (!buffer) {
        return;
    }
    RoboFree(buffer->scalarStarts);
    RoboFree(buffer->bytes);
    RoboFree(buffer);
}

Result RoboTextBufferClone(const RoboTextBuffer *buffer, RoboTextBuffer **out, RoboTextDiag *diag)
{
    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer is required");
    }
    return RoboTextBufferCreateFrom(out, buffer->bytes, buffer->length, diag);
}

/* ------------------------------------------------------------------ */
/* Reading                                                             */
/* ------------------------------------------------------------------ */

RoboTextByteIndex RoboTextBufferByteLength(const RoboTextBuffer *buffer)
{
    return buffer ? (RoboTextByteIndex)buffer->length : 0u;
}

RoboTextScalarIndex RoboTextBufferScalarCount(const RoboTextBuffer *buffer)
{
    RoboTextBuffer *mutable = (RoboTextBuffer *)buffer;

    if (!buffer) {
        return 0u;
    }
    if (TextBufferBuildIndex(mutable, NULL) != RoboOk) {
        return 0u;
    }
    return buffer->scalarCount;
}

int RoboTextBufferIsEmpty(const RoboTextBuffer *buffer)
{
    return !buffer || buffer->length == 0u;
}

const char *RoboTextBufferBytes(const RoboTextBuffer *buffer)
{
    if (!buffer || buffer->length == 0u) {
        return NULL;
    }
    return buffer->bytes;
}

Result RoboTextBufferToCString(const RoboTextBuffer *buffer, char **out, RoboTextDiag *diag)
{
    char *copy;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "an out pointer is required");
    }
    *out = NULL;
    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer is required");
    }
    copy = (char *)Malloc(buffer->length + 1u);
    if (!copy) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboOutOfMemory,
                               "the string copy could not be allocated");
    }
    if (buffer->length != 0u) {
        memcpy(copy, buffer->bytes, buffer->length);
    }
    copy[buffer->length] = '\0';
    *out = copy;
    return RoboOk;
}

RoboTextRange RoboTextBufferRange(const RoboTextBuffer *buffer)
{
    return RoboTextRangeFull(RoboTextBufferScalarCount(buffer));
}

int RoboTextBufferContainsRange(const RoboTextBuffer *buffer, RoboTextRange range)
{
    if (!buffer) {
        return 0;
    }
    return RoboTextRangeIsWithin(range, RoboTextBufferScalarCount(buffer));
}

Result RoboTextBufferScalarAt(const RoboTextBuffer *buffer,
                              RoboTextScalarIndex index,
                              Uchar32 *outScalar,
                              RoboTextDiag *diag)
{
    size_t offset;
    RoboTextByteIndex byte = 0u;
    Result status;

    if (!buffer || !outScalar) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer and an out scalar are required");
    }
    status = RoboTextBufferScalarToByte(buffer, index, &byte, diag);
    if (status != RoboOk) {
        return status;
    }
    if ((size_t)byte >= buffer->length) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "no scalar starts at the end of the text");
    }
    offset = (size_t)byte;
    if (TextDecodeAt(buffer->bytes, buffer->length, offset, outScalar, NULL) != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboParseError,
                               "the scalar at that index did not decode");
    }
    return RoboOk;
}

Result RoboTextBufferScalarToByte(const RoboTextBuffer *buffer,
                                  RoboTextScalarIndex index,
                                  RoboTextByteIndex *outByte,
                                  RoboTextDiag *diag)
{
    RoboTextBuffer *mutable = (RoboTextBuffer *)buffer;
    Result status;

    if (!buffer || !outByte) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer and an out offset are required");
    }
    *outByte = 0u;
    status = TextBufferBuildIndex(mutable, diag);
    if (status != RoboOk) {
        return status;
    }
    if (index > buffer->scalarCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a scalar index past the end of the text has no byte offset");
    }
    if (buffer->scalarCount == 0u) {
        /* The empty text: index 0 is both the first scalar and the end, and its offset is 0. */
        *outByte = 0u;
        return RoboOk;
    }
    *outByte = (RoboTextByteIndex)buffer->scalarStarts[index];
    return RoboOk;
}

Result RoboTextBufferByteToScalar(const RoboTextBuffer *buffer,
                                  RoboTextByteIndex byte,
                                  RoboTextScalarIndex *outScalar,
                                  RoboTextDiag *diag)
{
    RoboTextBuffer *mutable = (RoboTextBuffer *)buffer;
    uint32_t low;
    uint32_t high;
    Result status;

    if (!buffer || !outScalar) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer and an out index are required");
    }
    *outScalar = 0u;
    status = TextBufferBuildIndex(mutable, diag);
    if (status != RoboOk) {
        return status;
    }
    if ((size_t)byte > buffer->length) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a byte offset past the end of the text is not a scalar position");
    }
    if (buffer->scalarCount == 0u) {
        if (byte != 0u) {
            return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                                   "an empty text has only one position");
        }
        *outScalar = 0u;
        return RoboOk;
    }

    /*
     * A binary search over the index, and the *refusal* is the point: a byte offset that falls between
     * two scalar starts is not a scalar position at all, and a caller that received the nearest one would
     * have silently been given a slightly different string. The search therefore looks for an exact
     * match and fails otherwise.
     */
    low = 0u;
    high = buffer->scalarCount;
    while (low <= high) {
        uint32_t mid = low + (high - low) / 2u;

        if (buffer->scalarStarts[mid] == byte) {
            *outScalar = mid;
            return RoboOk;
        }
        if (buffer->scalarStarts[mid] < byte) {
            if (mid == buffer->scalarCount) {
                break;
            }
            low = mid + 1u;
        } else {
            if (mid == 0u) {
                break;
            }
            high = mid - 1u;
        }
    }
    return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                           "a byte offset inside a multi-byte scalar is not a character position");
}

Result RoboTextBufferRangeToBytes(const RoboTextBuffer *buffer,
                                  RoboTextRange range,
                                  RoboTextByteRange *outBytes,
                                  RoboTextDiag *diag)
{
    RoboTextByteIndex start = 0u;
    RoboTextByteIndex end = 0u;
    Result status;

    if (!buffer || !outBytes) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer and an out range are required");
    }
    outBytes->start = 0u;
    outBytes->end = 0u;
    if (RoboTextRangeIsInverted(range)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a text range cannot end before it starts");
    }
    if (!RoboTextBufferContainsRange(buffer, range)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a text range ends past the end of the text");
    }
    /*
     * Both endpoints through the same door, so a range whose *end* falls mid-sequence is refused by the
     * second conversion rather than rounded down to the boundary it happens to be near. `ScalarToByte`
     * is total for in-range indices, so the only failure left is the build itself.
     */
    status = RoboTextBufferScalarToByte(buffer, range.start, &start, diag);
    if (status != RoboOk) {
        return status;
    }
    status = RoboTextBufferScalarToByte(buffer, range.end, &end, diag);
    if (status != RoboOk) {
        return status;
    }
    outBytes->start = start;
    outBytes->end = end;
    return RoboOk;
}

Result RoboTextBufferDecodeRange(const RoboTextBuffer *buffer,
                                 RoboTextRange range,
                                 Uchar32 *out,
                                 size_t outCapacity,
                                 size_t *outCount,
                                 RoboTextDiag *diag)
{
    uint32_t length;

    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer is required");
    }
    if (outCount) {
        *outCount = 0u;
    }
    if (RoboTextRangeIsInverted(range)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a text range cannot end before it starts");
    }
    if (!RoboTextBufferContainsRange(buffer, range)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a text range ends past the end of the text");
    }
    length = RoboTextRangeLength(range);
    if (outCount) {
        *outCount = (size_t)length;
    }
    if (!out) {
        return RoboOk; /* A count-only call. */
    }
    if (outCapacity < (size_t)length) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboNotSupported,
                               "the destination cannot hold the range");
    }
    for (uint32_t index = 0u; index < length; index++) {
        RoboTextByteIndex byte = 0u;

        if (RoboTextBufferScalarToByte(buffer, range.start + index, &byte, diag) != RoboOk) {
            return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                                   "a scalar in the range has no byte offset");
        }
        if (TextDecodeAt(buffer->bytes, buffer->length, (size_t)byte, &out[index], NULL) !=
            RoboOk) {
            return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboParseError,
                                   "a scalar in the range did not decode");
        }
    }
    return RoboOk;
}

Result RoboTextBufferSlice(const RoboTextBuffer *buffer,
                           RoboTextRange range,
                           RoboTextBuffer **out,
                           RoboTextDiag *diag)
{
    RoboTextByteRange bytes;
    RoboTextBuffer *slice = NULL;
    Result status;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "an out pointer is required");
    }
    *out = NULL;
    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer is required");
    }
    status = RoboTextBufferRangeToBytes(buffer, range, &bytes, diag);
    if (status != RoboOk) {
        return status;
    }
    status = RoboTextBufferCreateFrom(&slice, buffer->bytes + (size_t)bytes.start,
                                      (size_t)bytes.end - (size_t)bytes.start, diag);
    if (status != RoboOk) {
        return status;
    }
    *out = slice;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Editing                                                             */
/* ------------------------------------------------------------------ */

Result RoboTextBufferAppend(RoboTextBuffer *buffer,
                            const char *utf8,
                            size_t length,
                            RoboTextDiag *diag)
{
    Result status;

    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer is required");
    }
    if (length == 0u) {
        return RoboOk;
    }
    if (!utf8) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "bytes are required");
    }
    status = TextBufferReserve(buffer, length, diag);
    if (status != RoboOk) {
        return status;
    }
    memcpy(buffer->bytes + buffer->length, utf8, length);
    buffer->length += length;
    buffer->bytes[buffer->length] = '\0';
    TextBufferInvalidateIndex(buffer);
    return RoboOk;
}

Result RoboTextBufferAppendScalar(RoboTextBuffer *buffer,
                                  Uchar32 scalar,
                                  RoboTextDiag *diag)
{
    char encoded[4];
    size_t length;

    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer is required");
    }
    length = UnicodeUtf8EncodeLen(scalar);
    if (length == 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a value that is not a Unicode scalar cannot be text");
    }
    if (UnicodeUtf8Encode(scalar, encoded, sizeof(encoded), &length) != RoboUnicodeOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboError,
                               "a Unicode scalar could not be encoded");
    }
    return RoboTextBufferAppend(buffer, encoded, length, diag);
}

Result RoboTextBufferInsert(RoboTextBuffer *buffer,
                            RoboTextScalarIndex at,
                            const char *utf8,
                            size_t length,
                            RoboTextDiag *diag)
{
    RoboTextByteIndex byte = 0u;
    Result status;

    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer is required");
    }
    if (length == 0u) {
        return RoboOk;
    }
    if (!utf8) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "bytes are required");
    }
    status = RoboTextBufferScalarToByte(buffer, at, &byte, diag);
    if (status != RoboOk) {
        return status;
    }
    status = TextBufferReserve(buffer, length, diag);
    if (status != RoboOk) {
        return status;
    }
    /* `byte <= length` holds, because `ScalarToByte` is total only for in-range indices and the last
     * in-range index maps to the end of the text. The move is `memmove` because the source may overlap
     * the destination. */
    memmove(buffer->bytes + (size_t)byte + length, buffer->bytes + (size_t)byte,
            buffer->length - (size_t)byte);
    memcpy(buffer->bytes + (size_t)byte, utf8, length);
    buffer->length += length;
    buffer->bytes[buffer->length] = '\0';
    TextBufferInvalidateIndex(buffer);
    return RoboOk;
}

Result RoboTextBufferRemove(RoboTextBuffer *buffer, RoboTextRange range, RoboTextDiag *diag)
{
    RoboTextByteRange bytes;
    Result status;

    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer is required");
    }
    status = RoboTextBufferRangeToBytes(buffer, range, &bytes, diag);
    if (status != RoboOk) {
        return status;
    }
    if (bytes.end == bytes.start) {
        return RoboOk;
    }
    memmove(buffer->bytes + (size_t)bytes.start, buffer->bytes + (size_t)bytes.end,
            buffer->length - (size_t)bytes.end);
    buffer->length -= (size_t)bytes.end - (size_t)bytes.start;
    buffer->bytes[buffer->length] = '\0';
    TextBufferInvalidateIndex(buffer);
    return RoboOk;
}

void RoboTextBufferClear(RoboTextBuffer *buffer)
{
    if (!buffer) {
        return;
    }
    /* The storage is kept, exactly as the paint list's `clear` keeps its own: a per-frame consumer reuses
     * the block, and a `memset` would be work whose only effect is invisible. */
    buffer->length = 0u;
    buffer->bytes[0] = '\0';
    TextBufferInvalidateIndex(buffer);
}

/* ------------------------------------------------------------------ */
/* The view                                                            */
/* ------------------------------------------------------------------ */

RoboTextView RoboTextViewMake(const char *bytes, size_t length)
{
    RoboTextView view;

    /*
     * A raw value, and the length is stored **as given**: a NULL pointer with a non-zero length is an
     * unsound view rather than an empty one, and repairing it here would hide the caller's mistake at the
     * exact place it was made. Every door below refuses it (`TextViewIsSound`), which is the same
     * convention as the mid-sequence byte offset: a mistake is reported, never rounded to something
     * plausible.
     */
    view.bytes = bytes;
    view.length = length;
    return view;
}

/** Non-zero when @p view can be read at all: bytes, or a length of zero. */
static int TextViewIsSound(RoboTextView view)
{
    return view.bytes != NULL || view.length == 0u;
}

Result RoboTextViewScalarCount(RoboTextView view, RoboTextScalarIndex *out, RoboTextDiag *diag)
{
    uint32_t count = 0u;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "an out count is required");
    }
    *out = 0u;
    if (!TextViewIsSound(view)) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a view with a length needs bytes");
    }
    if (TextCountScalars(view.bytes, view.length, &count) != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboParseError,
                               "the view's bytes could not be decoded");
    }
    *out = count;
    return RoboOk;
}

Result RoboTextViewScalarAt(RoboTextView view,
                            RoboTextScalarIndex index,
                            Uchar32 *out,
                            RoboTextDiag *diag)
{
    size_t offset = 0u;
    uint32_t seen = 0u;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "an out scalar is required");
    }
    if (!TextViewIsSound(view)) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a view with a length needs bytes");
    }
    while (offset < view.length && seen < index) {
        size_t next = offset;

        if (TextDecodeAt(view.bytes, view.length, offset, NULL, &next) != RoboOk) {
            return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboParseError,
                                   "the view's bytes could not be decoded");
        }
        offset = next;
        seen++;
    }
    if (seen != index || offset >= view.length) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a scalar index past the end of the text");
    }
    if (TextDecodeAt(view.bytes, view.length, offset, out, NULL) != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboParseError,
                               "the scalar at that index did not decode");
    }
    return RoboOk;
}

Result RoboTextViewScalarToByte(RoboTextView view,
                                RoboTextScalarIndex index,
                                RoboTextByteIndex *out,
                                RoboTextDiag *diag)
{
    size_t offset = 0u;
    uint32_t seen = 0u;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "an out offset is required");
    }
    *out = 0u;
    if (!TextViewIsSound(view)) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a view with a length needs bytes");
    }
    while (offset < view.length && seen < index) {
        size_t next = offset;

        if (TextDecodeAt(view.bytes, view.length, offset, NULL, &next) != RoboOk) {
            return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboParseError,
                                   "the view's bytes could not be decoded");
        }
        offset = next;
        seen++;
    }
    if (seen != index) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a scalar index past the end of the text has no byte offset");
    }
    if (offset > view.length) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a scalar index past the end of the text has no byte offset");
    }
    *out = (RoboTextByteIndex)offset;
    return RoboOk;
}

Result RoboTextViewDecodeRange(RoboTextView view,
                               RoboTextRange range,
                               Uchar32 *out,
                               size_t outCapacity,
                               size_t *outCount,
                               RoboTextDiag *diag)
{
    RoboTextScalarIndex scalars = 0u;
    uint32_t length;
    Result status;

    if (outCount) {
        *outCount = 0u;
    }
    status = RoboTextViewScalarCount(view, &scalars, diag);
    if (status != RoboOk) {
        return status;
    }
    if (RoboTextRangeIsInverted(range) || !RoboTextRangeIsWithin(range, scalars)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a text range does not lie inside the text");
    }
    length = RoboTextRangeLength(range);
    if (outCount) {
        *outCount = (size_t)length;
    }
    if (!out) {
        return RoboOk;
    }
    if (outCapacity < (size_t)length) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboNotSupported,
                               "the destination cannot hold the range");
    }
    for (uint32_t index = 0u; index < length; index++) {
        /*
         * Each scalar through `ScalarAt` rather than one walk with a cursor, and the reason is the caller
         * rather than the cost: a view has no index, so walking would be the only way to decode in
         * order — and `DecodeRange` is called on a *range*, which is not necessarily asked about once in
         * increasing order. `ScalarAt` is O(index) and correct for every range; a caller decoding a whole
         * document scalar by scalar should walk the bytes, which is what the segmenter does.
         */
        status = RoboTextViewScalarAt(view, range.start + index, &out[index], diag);
        if (status != RoboOk) {
            return status;
        }
    }
    return RoboOk;
}
