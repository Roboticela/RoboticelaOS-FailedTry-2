/*
 * RoboCSS fundamental types (Phase 9.1) — see RoboCSSTypes.h.
 */

#include "RoboCSS/RoboCSSTypes.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Slices                                                              */
/* ------------------------------------------------------------------ */

CssSlice CssSliceEmpty(void)
{
    CssSlice slice;

    slice.bytes = NULL;
    slice.length = 0u;
    return slice;
}

CssSlice CssSliceFromBytes(const uint8_t *bytes, size_t length)
{
    CssSlice slice;

    slice.bytes = bytes;
    slice.length = length;
    return slice;
}

CssSlice CssSliceFromText(const char *utf8)
{
    CssSlice slice;

    if (!utf8) {
        return CssSliceEmpty();
    }
    slice.bytes = (const uint8_t *)utf8;
    slice.length = strlen(utf8);
    return slice;
}

int CssSliceIsEmpty(CssSlice slice)
{
    return !slice.bytes || slice.length == 0u;
}

int CssSliceIsAbsent(CssSlice slice)
{
    return slice.bytes == NULL;
}

int CssSliceEquals(CssSlice a, CssSlice b)
{
    if (a.length != b.length) {
        return 0;
    }
    if (a.length == 0u) {
        return 1;
    }
    if (!a.bytes || !b.bytes) {
        return 0;
    }
    return memcmp(a.bytes, b.bytes, a.length) == 0;
}

int CssSliceEqualsCi(CssSlice a, CssSlice b)
{
    size_t i;

    if (a.length != b.length) {
        return 0;
    }
    if (a.length == 0u) {
        return 1;
    }
    if (!a.bytes || !b.bytes) {
        return 0;
    }
    for (i = 0u; i < a.length; i++) {
        if (CssAsciiLower(a.bytes[i]) != CssAsciiLower(b.bytes[i])) {
            return 0;
        }
    }
    return 1;
}

int CssSliceEqualsAscii(CssSlice slice, const char *ascii)
{
    return CssBytesEqualAscii(slice.bytes, slice.length, ascii);
}

int CssSliceEqualsAsciiCi(CssSlice slice, const char *ascii)
{
    return CssBytesEqualAsciiCi(slice.bytes, slice.length, ascii);
}

int CssSliceStartsWithAscii(CssSlice slice, const char *prefix)
{
    size_t prefixLength;

    if (!prefix) {
        return 0;
    }
    prefixLength = strlen(prefix);
    if (prefixLength > slice.length) {
        return 0;
    }
    if (prefixLength == 0u) {
        return 1;
    }
    if (!slice.bytes) {
        return 0;
    }
    return memcmp(slice.bytes, prefix, prefixLength) == 0;
}

Result CssSliceCopyBytes(CssSlice slice,
                                        uint8_t *out,
                                        size_t outCapacity)
{
    if (!out || outCapacity < slice.length) {
        return RoboInvalidArgument;
    }
    if (slice.length == 0u) {
        return RoboOk;
    }
    if (!slice.bytes) {
        return RoboInvalidArgument;
    }
    memcpy(out, slice.bytes, slice.length);
    return RoboOk;
}

Result CssSliceCopyString(CssSlice slice,
                                         char *out,
                                         size_t outCapacity)
{
    if (!out || outCapacity < slice.length + 1u) {
        return RoboInvalidArgument;
    }
    if (slice.length > 0u) {
        if (!slice.bytes) {
            return RoboInvalidArgument;
        }
        memcpy(out, slice.bytes, slice.length);
    }
    out[slice.length] = '\0';
    return RoboOk;
}

size_t CssSplitAsciiWhitespace(CssSlice text,
                                       CssSlice *outFields,
                                       size_t capacity,
                                       int *outTruncated)
{
    size_t written = 0u;
    size_t i = 0u;
    int truncated = 0;

    if (outTruncated) {
        *outTruncated = 0;
    }
    if (!text.bytes || text.length == 0u) {
        return 0u;
    }
    while (i < text.length) {
        size_t start;

        while (i < text.length && CssAsciiIsSpace(text.bytes[i])) {
            i++;
        }
        if (i >= text.length) {
            break;
        }
        start = i;
        while (i < text.length && !CssAsciiIsSpace(text.bytes[i])) {
            i++;
        }
        if (written < capacity && outFields) {
            outFields[written].bytes = text.bytes + start;
            outFields[written].length = i - start;
            written++;
        } else {
            truncated = 1;
        }
    }
    if (outTruncated) {
        *outTruncated = truncated;
    }
    return written;
}

/* ------------------------------------------------------------------ */
/* Positions and ranges                                                */
/* ------------------------------------------------------------------ */

CssPosition CssPositionNone(void)
{
    CssPosition position;

    position.offset = 0u;
    position.line = 0u;
    position.column = 0u;
    return position;
}

CssPosition CssPositionAt(size_t offset)
{
    CssPosition position;

    position.offset = offset;
    position.line = 0u;
    position.column = 0u;
    return position;
}

int CssPositionHasLine(const CssPosition *position)
{
    return position && position->line > 0u;
}

CssRange CssRangeAt(size_t offset)
{
    CssRange range;

    range.begin = offset;
    range.end = offset;
    return range;
}

CssRange CssRangeSpan(size_t begin, size_t length)
{
    CssRange range;

    range.begin = begin;
    if (length > (SIZE_MAX - begin)) {
        range.end = SIZE_MAX;
    } else {
        range.end = begin + length;
    }
    return range;
}

int CssRangeIsEmpty(CssRange range)
{
    return range.begin >= range.end;
}

size_t CssRangeLength(CssRange range)
{
    if (range.end <= range.begin) {
        return 0u;
    }
    return range.end - range.begin;
}

int CssRangeContains(CssRange range, size_t offset)
{
    return offset >= range.begin && offset < range.end;
}

/* ------------------------------------------------------------------ */
/* Origin                                                              */
/* ------------------------------------------------------------------ */

int CssOriginIsKnown(CssOrigin origin)
{
    switch (origin) {
    case RoboCssOriginNone:
    case RoboCssOriginRuntime:
    case RoboCssOriginUser:
    case RoboCssOriginAuthor:
    case RoboCssOriginInline:
        return 1;
    default:
        return 0;
    }
}

const char *CssOriginName(CssOrigin origin)
{
    switch (origin) {
    case RoboCssOriginNone:
        return "none";
    case RoboCssOriginRuntime:
        return "runtime";
    case RoboCssOriginUser:
        return "user";
    case RoboCssOriginAuthor:
        return "author";
    case RoboCssOriginInline:
        return "inline";
    default:
        return "unknown";
    }
}

int CssOriginIsStylesheetOrigin(CssOrigin origin)
{
    return origin == RoboCssOriginRuntime || origin == RoboCssOriginUser ||
           origin == RoboCssOriginAuthor;
}

int CssOriginIsInline(CssOrigin origin)
{
    return origin == RoboCssOriginInline;
}

/* ------------------------------------------------------------------ */
/* Stylesheet state                                                    */
/* ------------------------------------------------------------------ */

int CssStylesheetStateIsKnown(CssStylesheetState state)
{
    switch (state) {
    case RoboCssStylesheetStateNone:
    case RoboCssStylesheetStateCreated:
    case RoboCssStylesheetStateParsed:
    case RoboCssStylesheetStateFailed:
        return 1;
    default:
        return 0;
    }
}

const char *CssStylesheetStateName(CssStylesheetState state)
{
    switch (state) {
    case RoboCssStylesheetStateNone:
        return "none";
    case RoboCssStylesheetStateCreated:
        return "created";
    case RoboCssStylesheetStateParsed:
        return "parsed";
    case RoboCssStylesheetStateFailed:
        return "failed";
    default:
        return "unknown";
    }
}

int CssStylesheetStateIsSettled(CssStylesheetState state)
{
    return state == RoboCssStylesheetStateParsed ||
           state == RoboCssStylesheetStateFailed;
}

/* ------------------------------------------------------------------ */
/* Diagnostic domains                                                  */
/* ------------------------------------------------------------------ */

int CssDiagnosticDomainIsKnown(CssDiagnosticDomain domain)
{
    return domain >= RoboCssDomainNone && domain < (CssDiagnosticDomain)RoboCssDomainCount;
}

const char *CssDiagnosticDomainName(CssDiagnosticDomain domain)
{
    switch (domain) {
    case RoboCssDomainNone:
        return "none";
    case RoboCssDomainModule:
        return "module";
    case RoboCssDomainSource:
        return "source";
    case RoboCssDomainToken:
        return "token";
    case RoboCssDomainValue:
        return "value";
    case RoboCssDomainProperty:
        return "property";
    case RoboCssDomainDeclaration:
        return "declaration";
    case RoboCssDomainSelector:
        return "selector";
    case RoboCssDomainRule:
        return "rule";
    case RoboCssDomainStylesheet:
        return "stylesheet";
    case RoboCssDomainParser:
        return "parser";
    case RoboCssDomainStyle:
        return "style";
    case RoboCssDomainComputed:
        return "computed";
    case RoboCssDomainPropertyRegistry:
        return "property registry";
    case RoboCssDomainInherit:
        return "inherit";
    case RoboCssDomainBox:
        return "box";
    case RoboCssDomainDisplay:
        return "display";
    case RoboCssDomainPosition:
        return "position";
    case RoboCssDomainBackground:
        return "background";
    case RoboCssDomainBorder:
        return "border";
    case RoboCssDomainOverflow:
        return "overflow";
    case RoboCssDomainCssom:
        return "cssom";
    case RoboCssDomainFlex:
        return "flex";
    case RoboCssDomainGrid:
        return "grid";
    case RoboCssDomainTable:
        return "table";
    case RoboCssDomainColumn:
        return "column";
    case RoboCssDomainWriting:
        return "writing";
    case RoboCssDomainTransform:
        return "transform";
    case RoboCssDomainTransition:
        return "transition";
    case RoboCssDomainAnimation:
        return "animation";
    case RoboCssDomainMedia:
        return "media";
    case RoboCssDomainContainer:
        return "container";
    case RoboCssDomainLogical:
        return "logical";
    case RoboCssDomainFont:
        return "font";
    case RoboCssDomainText:
        return "text";
    case RoboCssDomainImage:
        return "image";
    default:
        return "unknown";
    }
}
