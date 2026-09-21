/*
 * RoboCSS source model (Phase 9.1) — see RoboCSSSource.h.
 *
 * Two invariants make the rest of this file simple:
 *
 *   1. Exactly one source kind owns bytes, and it is COPY. Every other kind
 *      borrows or has none, so `dispose` is "free if COPY, then forget".
 *   2. Every configuration path disposes whatever the source held first. A
 *      source is therefore never half-configured — either the new
 *      configuration succeeded, or the source is back to KIND_NONE.
 */

#include "RoboCSS/RoboCSSSource.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

int CssSourceKindIsKnown(CssSourceKind kind)
{
    return kind >= RoboCssSourceKindNone &&
           kind < (CssSourceKind)RoboCssSourceKindCount;
}

const char *CssSourceKindName(CssSourceKind kind)
{
    switch (kind) {
    case RoboCssSourceKindNone:
        return "none";
    case RoboCssSourceKindEmpty:
        return "empty";
    case RoboCssSourceKindMemory:
        return "memory";
    case RoboCssSourceKindString:
        return "string";
    case RoboCssSourceKindCopy:
        return "copy";
    default:
        return "unknown";
    }
}

int CssSourceKindIsOwning(CssSourceKind kind)
{
    return kind == RoboCssSourceKindCopy;
}

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

void CssSourceInit(CssSource *source)
{
    if (!source) {
        return;
    }
    source->kind = RoboCssSourceKindNone;
    source->bytes = NULL;
    source->length = 0u;
    source->name[0] = '\0';
}

void CssSourceDispose(CssSource *source)
{
    if (!source) {
        return;
    }
    if (source->kind == RoboCssSourceKindCopy && source->bytes) {
        RoboFree((void *)source->bytes);
    }
    CssSourceInit(source);
}

/** Free whatever the source holds but keep its label. */
static void SourceReleaseContent(CssSource *source)
{
    if (source->kind == RoboCssSourceKindCopy && source->bytes) {
        RoboFree((void *)source->bytes);
    }
    source->kind = RoboCssSourceKindNone;
    source->bytes = NULL;
    source->length = 0u;
}

Result CssSourceSetEmpty(CssSource *source,
                                       CssDiag *diag)
{
    if (!source) {
        return RoboCssDiag(diag, RoboCssDomainSource, RoboInvalidArgument,
                             "source is NULL");
    }
    SourceReleaseContent(source);
    source->kind = RoboCssSourceKindEmpty;
    return RoboOk;
}

Result CssSourceSetMemory(CssSource *source,
                                        const uint8_t *bytes,
                                        size_t length,
                                        CssDiag *diag)
{
    if (!source) {
        return RoboCssDiag(diag, RoboCssDomainSource, RoboInvalidArgument,
                             "source is NULL");
    }
    if (!bytes && length > 0u) {
        return RoboCssDiag(diag, RoboCssDomainSource, RoboInvalidArgument,
                             "NULL bytes with a non-zero length");
    }
    SourceReleaseContent(source);
    if (length == 0u) {
        /* A zero-length memory source is an empty source, not a borrow of
         * nothing: the caller's pointer would be meaningless to keep. */
        source->kind = RoboCssSourceKindEmpty;
        return RoboOk;
    }
    source->kind = RoboCssSourceKindMemory;
    source->bytes = bytes;
    source->length = length;
    return RoboOk;
}

Result CssSourceSetString(CssSource *source,
                                        const char *utf8,
                                        CssDiag *diag)
{
    if (!source) {
        return RoboCssDiag(diag, RoboCssDomainSource, RoboInvalidArgument,
                             "source is NULL");
    }
    if (!utf8) {
        return CssSourceSetEmpty(source, diag);
    }
    /* Borrow as KIND_STRING rather than delegating to set_memory. The two kinds
     * describe the same borrowed bytes, but they are not interchangeable: the
     * caller asked for the NUL-terminated one, and the kind is what the model
     * reports back, what the source's own description says, and what a later
     * phase reads to know whether it may assume a terminator. A kind no setter
     * can produce is a kind that does not exist. */
    SourceReleaseContent(source);
    if (utf8[0] == '\0') {
        /* Same rule as set_memory: nothing to borrow is an empty source, even
         * though a valid pointer was passed. */
        source->kind = RoboCssSourceKindEmpty;
        return RoboOk;
    }
    source->kind = RoboCssSourceKindString;
    source->bytes = (const uint8_t *)utf8;
    source->length = strlen(utf8);
    return RoboOk;
}

Result CssSourceCopy(CssSource *source,
                                  const uint8_t *bytes,
                                  size_t length,
                                  CssDiag *diag)
{
    uint8_t *copy;

    if (!source) {
        return RoboCssDiag(diag, RoboCssDomainSource, RoboInvalidArgument,
                             "source is NULL");
    }
    if (!bytes && length > 0u) {
        return RoboCssDiag(diag, RoboCssDomainSource, RoboInvalidArgument,
                             "NULL bytes with a non-zero length");
    }
    SourceReleaseContent(source);
    if (length == 0u) {
        /* A copy of nothing allocates nothing: an empty source is the honest
         * representation of an empty copy, and it has no ownership to manage. */
        source->kind = RoboCssSourceKindEmpty;
        return RoboOk;
    }
    copy = (uint8_t *)Malloc(length);
    if (!copy) {
        /* Nothing was configured: the source is back to KIND_NONE, never
         * half-configured with a length it cannot back with bytes. */
        return RoboCssDiag(diag, RoboCssDomainSource, RoboOutOfMemory,
                             "could not allocate the source copy");
    }
    memcpy(copy, bytes, length);
    source->kind = RoboCssSourceKindCopy;
    source->bytes = copy;
    source->length = length;
    return RoboOk;
}

Result CssSourceSetName(CssSource *source,
                                      const char *name,
                                      CssDiag *diag)
{
    if (!source) {
        return RoboCssDiag(diag, RoboCssDomainSource, RoboInvalidArgument,
                             "source is NULL");
    }
    CssCopyLabel(source->name, sizeof(source->name), name);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

const char *CssSourceName(const CssSource *source)
{
    if (!source) {
        return NULL;
    }
    return source->name;
}

const uint8_t *CssSourceBytes(const CssSource *source)
{
    return source ? source->bytes : NULL;
}

size_t CssSourceLength(const CssSource *source)
{
    return source ? source->length : 0u;
}

int CssSourceIsEmpty(const CssSource *source)
{
    return !source || source->length == 0u;
}

int CssSourceIsOwned(const CssSource *source)
{
    return source && source->kind == RoboCssSourceKindCopy;
}

int CssSourceIsConfigured(const CssSource *source)
{
    return source && source->kind != RoboCssSourceKindNone;
}

CssSlice CssSourceSlice(const CssSource *source)
{
    if (!source || !source->bytes || source->length == 0u) {
        return CssSliceEmpty();
    }
    return CssSliceFromBytes(source->bytes, source->length);
}

/* ------------------------------------------------------------------ */
/* Bounds, positions, ranges                                           */
/* ------------------------------------------------------------------ */

int CssSourceOffsetIsValid(const CssSource *source, size_t offset)
{
    if (!source) {
        return 0;
    }
    return offset <= source->length;
}

Result CssSourcePosition(const CssSource *source,
                                      size_t offset,
                                      CssPosition *outPosition)
{
    size_t i = 0u;
    uint32_t line = 1u;
    uint32_t column = 1u;

    if (!source || !outPosition) {
        return RoboInvalidArgument;
    }
    if (offset > source->length) {
        return RoboInvalidArgument;
    }
    /* Nothing valid can be counted in a source with no bytes; offset 0 there
     * is line 1, column 1 rather than "no position", because the caller did
     * name a real place: the start. */
    while (i < offset) {
        uint8_t byte = source->bytes[i];

        if (byte == (uint8_t)'\r') {
            if (i + 1u < offset && source->bytes[i + 1u] == (uint8_t)'\n') {
                i += 2u;
            } else {
                i += 1u;
            }
            line++;
            column = 1u;
            continue;
        }
        /* A form feed ends a line exactly as LF and CR do: CSS defines its
         * newline as the three of them, and CssIsNewlineByte says the
         * same thing — counting only two of the three would make a position
         * disagree with the tokenizer that produced it. */
        if (byte == (uint8_t)'\n' || byte == (uint8_t)'\f') {
            i += 1u;
            line++;
            column = 1u;
            continue;
        }
        i += CssUtf8ScalarLength(source->bytes + i, source->length - i);
        column++;
    }
    outPosition->offset = offset;
    outPosition->line = line;
    outPosition->column = column;
    return RoboOk;
}

size_t CssSourceLineCount(const CssSource *source)
{
    size_t i = 0u;
    size_t breaks = 0u;

    if (!source || source->length == 0u) {
        return 0u;
    }
    while (i < source->length) {
        uint8_t byte = source->bytes[i];

        if (byte == (uint8_t)'\r') {
            breaks++;
            i++;
            if (i < source->length && source->bytes[i] == (uint8_t)'\n') {
                i++;
            }
            continue;
        }
        if (byte == (uint8_t)'\n') {
            breaks++;
            i++;
            continue;
        }
        i++;
    }
    /* One line per break, plus the line the last break opened — which is why a
     * source ending in a break counts the empty line after it, and why the
     * highest position CssSourcePosition can report agrees with this. */
    return breaks + 1u;
}

int CssSourceRangeIsValid(const CssSource *source,
                                   CssRange range)
{
    if (!source) {
        return 0;
    }
    if (range.begin > range.end) {
        return 0;
    }
    return range.end <= source->length;
}

Result CssSourceRangeSlice(const CssSource *source,
                                         CssRange range,
                                         CssSlice *outSlice)
{
    if (!outSlice) {
        return RoboInvalidArgument;
    }
    *outSlice = CssSliceEmpty();
    if (!source) {
        return RoboInvalidArgument;
    }
    if (!CssSourceRangeIsValid(source, range)) {
        return RoboInvalidArgument;
    }
    if (range.begin == range.end) {
        return RoboOk; /* a valid empty range yields an empty slice */
    }
    *outSlice = CssSliceFromBytes(source->bytes + range.begin,
                                           range.end - range.begin);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Describing the bytes (never rewriting)                              */
/* ------------------------------------------------------------------ */

int CssSourceIsValidUtf8(const CssSource *source)
{
    size_t i = 0u;

    if (!source || source->length == 0u) {
        return 1;
    }
    while (i < source->length) {
        size_t length = CssUtf8ScalarLength(source->bytes + i,
                                                   source->length - i);

        if (length == 1u && source->bytes[i] >= 0x80u) {
            return 0; /* an ill-formed scalar: we never repair, only report */
        }
        i += length;
    }
    return 1;
}

size_t CssSourceScalarCount(const CssSource *source)
{
    size_t i = 0u;
    size_t count = 0u;

    if (!source || source->length == 0u) {
        return 0u;
    }
    while (i < source->length) {
        size_t length = CssUtf8ScalarLength(source->bytes + i,
                                                   source->length - i);

        /* An ill-formed leading byte is one position, not zero: a column count
         * that skipped it would report a column that does not exist. */
        i += (length == 0u) ? 1u : length;
        count++;
    }
    return count;
}
