/*
 * RoboCSS fragmentation break vocabulary (Phase 19.7) — see RoboCSSFragment.h.
 *
 * Two tables, one per vocabulary, and every accessor below reads one of them. The
 * rows carry the keyword, the enumerator and the classifications a fragmentation
 * pass dispatches on; nothing in this file splits a box, sizes a fragmentainer or
 * lays anything out.
 *
 * The two tables are deliberately **not** one table with a property attached. The
 * `break-between` and `break-inside` sets look almost the same and are not: a
 * boundary may be forced, and an interior may only be forbidden, so
 * `break-inside: column` is not a value at all rather than a value this build
 * ignores. One table would have had to carry "which property may hold me", which is
 * the second definition of a vocabulary that `RoboCSSColumn.h` already refused for
 * `column-span` and `column-fill`.
 */

#include "RoboCSS/RoboCSSFragment.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

static Result FragmentWriteKeyword(const char *keyword,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength,
                                   CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainFragment, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainFragment, RoboInvalidArgument,
                           "not a fragmentation value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainFragment, RoboNotSupported,
                           "the fragmentation keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

static int FragmentKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

static Result FragmentUnknownValue(const char *what,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength,
                                   CssDiag *diag)
{
    if (buffer && outLength && capacity > 0u) {
        buffer[0] = '\0';
    }
    return RoboCssDiag(diag, RoboCssDomainFragment, RoboInvalidArgument, what);
}

/* ------------------------------------------------------------------ */
/* break-before / break-after                                          */
/* ------------------------------------------------------------------ */

/*
 * The four classifications are columns of this one table rather than four
 * functions over a switch, because the questions are asked *about a row* and a
 * switch would be a second place the vocabulary is written down. `forced` and
 * `avoided` are independent on purpose: no row is both, and the two columns that
 * remain (`columnBreak`, `pageBreak`) are only meaningful for a forced row, which
 * is why they are 0 on every `avoid`.
 */
typedef struct breakBetweenRow {
    CssBreakBetween value;
    const char *keyword;
    int forced;
    int avoided;
    int columnBreak;
    int pageBreak;
} BreakBetweenRow;

static const BreakBetweenRow breakBetweenVocabulary[] = {
    { RoboCssBreakBetweenAuto,         "auto",         0, 0, 0, 0 },
    { RoboCssBreakBetweenAvoid,        "avoid",        0, 1, 0, 0 },
    { RoboCssBreakBetweenAvoidPage,    "avoid-page",   0, 1, 0, 0 },
    { RoboCssBreakBetweenAvoidColumn,  "avoid-column", 0, 1, 0, 0 },
    { RoboCssBreakBetweenAvoidRegion,  "avoid-region", 0, 1, 0, 0 },
    /* `page` is forced and a column container does not honour it; `column` is the
     * mirror. `always` is the generic forced break and both containers honour it,
     * which is the row that makes `columnBreak` and `pageBreak` two columns rather
     * than one. */
    { RoboCssBreakBetweenPage,         "page",         1, 0, 0, 1 },
    { RoboCssBreakBetweenColumn,       "column",       1, 0, 1, 0 },
    /* `region` is forced and this build lays out no regions, so it is forced for
     * neither container: the row states the absence instead of borrowing `always`. */
    { RoboCssBreakBetweenRegion,       "region",       1, 0, 0, 0 },
    { RoboCssBreakBetweenAlways,       "always",       1, 0, 1, 1 },
    /* The four page-side values are forced breaks *and* a request about which side
     * the next page begins on. This build has no pages, so they force a page break
     * and carry nothing else; the side is a stated gap, not an approximation. */
    { RoboCssBreakBetweenLeft,         "left",         1, 0, 0, 1 },
    { RoboCssBreakBetweenRight,        "right",        1, 0, 0, 1 },
    { RoboCssBreakBetweenRecto,        "recto",        1, 0, 0, 1 },
    { RoboCssBreakBetweenVerso,        "verso",        1, 0, 0, 1 }
};

#define BREAK_BETWEEN_COUNT \
    (sizeof(breakBetweenVocabulary) / sizeof(breakBetweenVocabulary[0]))

static const BreakBetweenRow *BreakBetweenRowOf(CssBreakBetween value)
{
    size_t index;

    for (index = 0u; index < BREAK_BETWEEN_COUNT; index++) {
        if (breakBetweenVocabulary[index].value == value) {
            return &breakBetweenVocabulary[index];
        }
    }
    return NULL;
}

int CssBreakBetweenIsKnown(CssBreakBetween value)
{
    return BreakBetweenRowOf(value) != NULL;
}

const char *CssBreakBetweenName(CssBreakBetween value)
{
    const BreakBetweenRow *row = BreakBetweenRowOf(value);

    return row ? row->keyword : "";
}

size_t CssBreakBetweenCount(void)
{
    return BREAK_BETWEEN_COUNT;
}

CssBreakBetween CssBreakBetweenFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssBreakBetweenAuto;
    }
    for (index = 0u; index < BREAK_BETWEEN_COUNT; index++) {
        if (FragmentKeywordEquals(keyword, breakBetweenVocabulary[index].keyword)) {
            return breakBetweenVocabulary[index].value;
        }
    }
    return RoboCssBreakBetweenAuto;
}

CssBreakBetween CssBreakBetweenFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssBreakBetweenAuto;
    }
    return CssBreakBetweenFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssBreakBetweenSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < BREAK_BETWEEN_COUNT; index++) {
        if (FragmentKeywordEquals(keyword, breakBetweenVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssBreakBetweenIsForced(CssBreakBetween value)
{
    const BreakBetweenRow *row = BreakBetweenRowOf(value);

    return row ? row->forced : 0;
}

int CssBreakBetweenIsAvoided(CssBreakBetween value)
{
    const BreakBetweenRow *row = BreakBetweenRowOf(value);

    return row ? row->avoided : 0;
}

int CssBreakBetweenForcesColumn(CssBreakBetween value)
{
    const BreakBetweenRow *row = BreakBetweenRowOf(value);

    return row ? row->columnBreak : 0;
}

int CssBreakBetweenForcesPage(CssBreakBetween value)
{
    const BreakBetweenRow *row = BreakBetweenRowOf(value);

    return row ? row->pageBreak : 0;
}

Result CssBreakBetweenWrite(CssBreakBetween value,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            CssDiag *diag)
{
    const BreakBetweenRow *row = BreakBetweenRowOf(value);

    if (!row) {
        return FragmentUnknownValue("not a break-before or break-after value",
                                    buffer, capacity, outLength, diag);
    }
    return FragmentWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* break-inside                                                        */
/* ------------------------------------------------------------------ */

typedef struct breakInsideRow {
    CssBreakInside value;
    const char *keyword;
    int avoided;
} BreakInsideRow;

static const BreakInsideRow breakInsideVocabulary[] = {
    { RoboCssBreakInsideAuto,         "auto",         0 },
    { RoboCssBreakInsideAvoid,        "avoid",        1 },
    { RoboCssBreakInsideAvoidPage,    "avoid-page",   1 },
    { RoboCssBreakInsideAvoidColumn,  "avoid-column", 1 },
    { RoboCssBreakInsideAvoidRegion,  "avoid-region", 1 }
};

#define BREAK_INSIDE_COUNT \
    (sizeof(breakInsideVocabulary) / sizeof(breakInsideVocabulary[0]))

static const BreakInsideRow *BreakInsideRowOf(CssBreakInside value)
{
    size_t index;

    for (index = 0u; index < BREAK_INSIDE_COUNT; index++) {
        if (breakInsideVocabulary[index].value == value) {
            return &breakInsideVocabulary[index];
        }
    }
    return NULL;
}

int CssBreakInsideIsKnown(CssBreakInside value)
{
    return BreakInsideRowOf(value) != NULL;
}

const char *CssBreakInsideName(CssBreakInside value)
{
    const BreakInsideRow *row = BreakInsideRowOf(value);

    return row ? row->keyword : "";
}

size_t CssBreakInsideCount(void)
{
    return BREAK_INSIDE_COUNT;
}

CssBreakInside CssBreakInsideFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssBreakInsideAuto;
    }
    for (index = 0u; index < BREAK_INSIDE_COUNT; index++) {
        if (FragmentKeywordEquals(keyword, breakInsideVocabulary[index].keyword)) {
            return breakInsideVocabulary[index].value;
        }
    }
    return RoboCssBreakInsideAuto;
}

CssBreakInside CssBreakInsideFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssBreakInsideAuto;
    }
    return CssBreakInsideFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssBreakInsideSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < BREAK_INSIDE_COUNT; index++) {
        if (FragmentKeywordEquals(keyword, breakInsideVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssBreakInsideIsAvoided(CssBreakInside value)
{
    const BreakInsideRow *row = BreakInsideRowOf(value);

    return row ? row->avoided : 0;
}

Result CssBreakInsideWrite(CssBreakInside value,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag)
{
    const BreakInsideRow *row = BreakInsideRowOf(value);

    if (!row) {
        return FragmentUnknownValue("not a break-inside value",
                                    buffer, capacity, outLength, diag);
    }
    return FragmentWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}
