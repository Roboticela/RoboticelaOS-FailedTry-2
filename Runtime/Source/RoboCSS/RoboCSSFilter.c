/*
 * RoboCSS filter vocabulary (Phase 18.19) — see RoboCSSFilter.h.
 *
 * One table, one row per function, and every accessor below reads one of its columns.
 * The rows carry the name and the classifications a consumer dispatches on; nothing in
 * this file blurs a pixel, shifts a shadow, mixes a colour matrix, fetches an SVG
 * filter or measures anything.
 *
 * The names are lowercase because that is how CSS writes them and how the writer below
 * must print them; matching is ASCII case-insensitive against these spellings and
 * against nothing else, so `hue-rotate` matches and `huerotate` does not.
 *
 * The classifications are three independent questions rather than one tree, and that is
 * the point of the table. `url()` is in none of the spatial/colour groups because it is
 * a reference; the eight colour filters are all in one group but take four different
 * *kinds* of argument between them (an angle for `hue-rotate`, a colour for
 * `drop-shadow` through the length group, and a bare amount for the rest); and an
 * unknown enumerator answers 0 to every one of them rather than guessing.
 */

#include "RoboCSS/RoboCSSFilter.h"

#include "RoboCSSInternal.h"

#include <string.h>

typedef struct filterFunctionRow {
    CssFilterFunction value;
    const char *keyword;
    int reference;
    int spatial;
    int color;
    int takesLength;
    int takesAngle;
    int takesAmount;
    int takesColor;
} FilterFunctionRow;

static const FilterFunctionRow filterFunctionVocabulary[] = {
    /* url,      reference spatial colour length angle amount colorArg */
    { RoboCssFilterFunctionUrl,        "url",         1, 0, 0, 0, 0, 0, 0 },
    { RoboCssFilterFunctionBlur,       "blur",        0, 1, 0, 1, 0, 0, 0 },
    { RoboCssFilterFunctionBrightness, "brightness",  0, 0, 1, 0, 0, 1, 0 },
    { RoboCssFilterFunctionContrast,   "contrast",    0, 0, 1, 0, 0, 1, 0 },
    { RoboCssFilterFunctionDropShadow, "drop-shadow", 0, 1, 0, 1, 0, 0, 1 },
    { RoboCssFilterFunctionGrayscale,  "grayscale",   0, 0, 1, 0, 0, 1, 0 },
    { RoboCssFilterFunctionHueRotate,  "hue-rotate",  0, 0, 1, 0, 1, 0, 0 },
    { RoboCssFilterFunctionInvert,     "invert",      0, 0, 1, 0, 0, 1, 0 },
    { RoboCssFilterFunctionOpacity,    "opacity",     0, 0, 1, 0, 0, 1, 0 },
    { RoboCssFilterFunctionSaturate,   "saturate",    0, 0, 1, 0, 0, 1, 0 },
    { RoboCssFilterFunctionSepia,      "sepia",       0, 0, 1, 0, 0, 1, 0 }
};

#define FILTER_FUNCTION_COUNT \
    (sizeof(filterFunctionVocabulary) / sizeof(filterFunctionVocabulary[0]))

static const FilterFunctionRow *FilterFunctionRowOf(CssFilterFunction function)
{
    size_t index;

    for (index = 0u; index < FILTER_FUNCTION_COUNT; index++) {
        if (filterFunctionVocabulary[index].value == function) {
            return &filterFunctionVocabulary[index];
        }
    }
    return NULL;
}

int CssFilterFunctionIsKnown(CssFilterFunction function)
{
    return FilterFunctionRowOf(function) != NULL;
}

const char *CssFilterFunctionName(CssFilterFunction function)
{
    const FilterFunctionRow *row = FilterFunctionRowOf(function);

    return row ? row->keyword : "";
}

size_t CssFilterFunctionCount(void)
{
    return FILTER_FUNCTION_COUNT;
}

CssFilterFunction CssFilterFunctionFromSlice(CssSlice name)
{
    size_t index;

    if (CssSliceIsAbsent(name) || name.length == 0u) {
        return RoboCssFilterFunctionUrl;
    }
    for (index = 0u; index < FILTER_FUNCTION_COUNT; index++) {
        if (CssBytesEqualAsciiCi(name.bytes, name.length,
                                          filterFunctionVocabulary[index].keyword)) {
            return filterFunctionVocabulary[index].value;
        }
    }
    return RoboCssFilterFunctionUrl;
}

CssFilterFunction CssFilterFunctionFromText(const char *name)
{
    if (!name) {
        return RoboCssFilterFunctionUrl;
    }
    return CssFilterFunctionFromSlice(
        CssSliceFromBytes((const uint8_t *)name, strlen(name)));
}

int CssFilterFunctionSliceIsKnown(CssSlice name)
{
    size_t index;

    if (CssSliceIsAbsent(name) || name.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FILTER_FUNCTION_COUNT; index++) {
        if (CssBytesEqualAsciiCi(name.bytes, name.length,
                                          filterFunctionVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssFilterFunctionIsReference(CssFilterFunction function)
{
    const FilterFunctionRow *row = FilterFunctionRowOf(function);

    return row ? row->reference : 0;
}

int CssFilterFunctionIsSpatial(CssFilterFunction function)
{
    const FilterFunctionRow *row = FilterFunctionRowOf(function);

    return row ? row->spatial : 0;
}

int CssFilterFunctionIsColor(CssFilterFunction function)
{
    const FilterFunctionRow *row = FilterFunctionRowOf(function);

    return row ? row->color : 0;
}

int CssFilterFunctionTakesLength(CssFilterFunction function)
{
    const FilterFunctionRow *row = FilterFunctionRowOf(function);

    return row ? row->takesLength : 0;
}

int CssFilterFunctionTakesAngle(CssFilterFunction function)
{
    const FilterFunctionRow *row = FilterFunctionRowOf(function);

    return row ? row->takesAngle : 0;
}

int CssFilterFunctionTakesAmount(CssFilterFunction function)
{
    const FilterFunctionRow *row = FilterFunctionRowOf(function);

    return row ? row->takesAmount : 0;
}

int CssFilterFunctionTakesColor(CssFilterFunction function)
{
    const FilterFunctionRow *row = FilterFunctionRowOf(function);

    return row ? row->takesColor : 0;
}

Result CssFilterFunctionWrite(CssFilterFunction function,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              CssDiag *diag)
{
    const FilterFunctionRow *row;
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainFilter, RoboInvalidArgument,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    row = FilterFunctionRowOf(function);
    if (!row) {
        return RoboCssDiag(diag, RoboCssDomainFilter, RoboInvalidArgument,
                             "not a filter function");
    }
    length = strlen(row->keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainFilter, RoboNotSupported,
                             "the filter function name does not fit");
    }
    memcpy(buffer, row->keyword, length + 1u);
    *outLength = length;
    return RoboOk;
}
