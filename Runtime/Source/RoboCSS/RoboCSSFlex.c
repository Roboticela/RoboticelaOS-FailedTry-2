/*
 * RoboCSS flexbox vocabulary (Phase 18.3) — see RoboCSSFlex.h.
 *
 * Three tables, and everything else in this file reads one of them:
 *
 *   direction  one value, its keyword, whether the main axis is the inline axis,
 *              and whether the edges are reversed
 *   wrap       one value, its keyword, whether it wraps, and whether it reverses
 *   align      one value, its keyword, and which of the four properties may hold it
 *
 * Two of the three carry a *physical* fact as a column rather than deriving it from
 * the enumerator's name, and the reason is the same in both cases: `row-reverse` is
 * `row` on the same axis with the edges swapped, so a consumer asking "is the main
 * axis the inline axis" and a consumer asking "are the items reversed" want two
 * different answers about one value, and both are columns of its row.
 *
 * The alignment table's four property columns are what makes one enumerator set
 * honest for four properties. A single list would make `align-items: space-between`
 * computable, which is the silent wrong answer every closed vocabulary in this module
 * exists to prevent; four columns put the difference in the row, so the *next*
 * alignment value answers for itself.
 *
 * The keywords are lowercase because that is how CSS writes them and how the writers
 * below must print them; matching is ASCII case-insensitive against these spellings
 * and against nothing else, so `row` matches and `roww` does not.
 */

#include "RoboCSS/RoboCSSFlex.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/** Non-zero when @p keyword equals @p spelling, ASCII case-insensitively. */
static int FlexKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

/**
 * Write @p keyword into @p buffer with the buffer discipline every writer in this
 * module shares: a NUL on entry, a refusal that reports the length it needed, and a
 * literal empty string rather than a half-copied keyword on the way out.
 */
static Result FlexWriteKeyword(const char *keyword,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               CssDiagnosticDomain domain,
                               CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, domain, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, domain, RoboInvalidArgument,
                           "not a flex value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, domain, RoboNotSupported,
                           "the flex keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* flex-direction                                                      */
/* ------------------------------------------------------------------ */

typedef struct flexDirectionRow {
    CssFlexDirection value;
    const char *keyword;
    int row;      /* main axis is the inline axis */
    int reversed; /* the start and end edges are swapped */
} FlexDirectionRow;

static const FlexDirectionRow flexDirectionVocabulary[] = {
    { RoboCssFlexDirectionRow,           "row",            1, 0 },
    { RoboCssFlexDirectionRowReverse,    "row-reverse",    1, 1 },
    { RoboCssFlexDirectionColumn,        "column",         0, 0 },
    { RoboCssFlexDirectionColumnReverse, "column-reverse", 0, 1 }
};

#define FLEX_DIRECTION_COUNT \
    (sizeof(flexDirectionVocabulary) / sizeof(flexDirectionVocabulary[0]))

static const FlexDirectionRow *FlexDirectionRowOf(CssFlexDirection direction)
{
    size_t index;

    for (index = 0u; index < FLEX_DIRECTION_COUNT; index++) {
        if (flexDirectionVocabulary[index].value == direction) {
            return &flexDirectionVocabulary[index];
        }
    }
    return NULL;
}

int CssFlexDirectionIsKnown(CssFlexDirection direction)
{
    return FlexDirectionRowOf(direction) != NULL;
}

const char *CssFlexDirectionName(CssFlexDirection direction)
{
    const FlexDirectionRow *row = FlexDirectionRowOf(direction);

    return row ? row->keyword : "";
}

size_t CssFlexDirectionCount(void)
{
    return FLEX_DIRECTION_COUNT;
}

CssFlexDirection CssFlexDirectionFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFlexDirectionRow;
    }
    for (index = 0u; index < FLEX_DIRECTION_COUNT; index++) {
        if (FlexKeywordEquals(keyword, flexDirectionVocabulary[index].keyword)) {
            return flexDirectionVocabulary[index].value;
        }
    }
    return RoboCssFlexDirectionRow;
}

CssFlexDirection CssFlexDirectionFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFlexDirectionRow;
    }
    return CssFlexDirectionFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFlexDirectionSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FLEX_DIRECTION_COUNT; index++) {
        if (FlexKeywordEquals(keyword, flexDirectionVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

Result CssFlexDirectionWrite(CssFlexDirection direction,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag)
{
    const FlexDirectionRow *row = FlexDirectionRowOf(direction);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainFlex, RoboInvalidArgument,
                           "not a flex-direction value");
    }
    return FlexWriteKeyword(row->keyword, buffer, capacity, outLength,
                            RoboCssDomainFlex, diag);
}

int CssFlexDirectionIsRow(CssFlexDirection direction)
{
    const FlexDirectionRow *row = FlexDirectionRowOf(direction);

    /* An unknown value answers 0 rather than 1: "the main axis is the inline axis" is
     * a claim a value nothing defined cannot support, and a row is the wrong guess to
     * make about a value that could not be read. */
    return row ? row->row : 0;
}

int CssFlexDirectionIsColumn(CssFlexDirection direction)
{
    const FlexDirectionRow *row = FlexDirectionRowOf(direction);

    return row ? !row->row : 0;
}

int CssFlexDirectionIsReversed(CssFlexDirection direction)
{
    const FlexDirectionRow *row = FlexDirectionRowOf(direction);

    return row ? row->reversed : 0;
}

/* ------------------------------------------------------------------ */
/* flex-wrap                                                           */
/* ------------------------------------------------------------------ */

typedef struct flexWrapRow {
    CssFlexWrap value;
    const char *keyword;
    int wrapped;  /* breaks into several lines */
    int reversed; /* the cross-axis line order is swapped */
} FlexWrapRow;

static const FlexWrapRow flexWrapVocabulary[] = {
    { RoboCssFlexWrapNoWrap,      "nowrap",       0, 0 },
    { RoboCssFlexWrapWrap,        "wrap",         1, 0 },
    { RoboCssFlexWrapWrapReverse, "wrap-reverse", 1, 1 }
};

#define FLEX_WRAP_COUNT (sizeof(flexWrapVocabulary) / sizeof(flexWrapVocabulary[0]))

static const FlexWrapRow *FlexWrapRowOf(CssFlexWrap wrap)
{
    size_t index;

    for (index = 0u; index < FLEX_WRAP_COUNT; index++) {
        if (flexWrapVocabulary[index].value == wrap) {
            return &flexWrapVocabulary[index];
        }
    }
    return NULL;
}

int CssFlexWrapIsKnown(CssFlexWrap wrap)
{
    return FlexWrapRowOf(wrap) != NULL;
}

const char *CssFlexWrapName(CssFlexWrap wrap)
{
    const FlexWrapRow *row = FlexWrapRowOf(wrap);

    return row ? row->keyword : "";
}

size_t CssFlexWrapCount(void)
{
    return FLEX_WRAP_COUNT;
}

CssFlexWrap CssFlexWrapFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFlexWrapNoWrap;
    }
    for (index = 0u; index < FLEX_WRAP_COUNT; index++) {
        if (FlexKeywordEquals(keyword, flexWrapVocabulary[index].keyword)) {
            return flexWrapVocabulary[index].value;
        }
    }
    return RoboCssFlexWrapNoWrap;
}

CssFlexWrap CssFlexWrapFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFlexWrapNoWrap;
    }
    return CssFlexWrapFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFlexWrapSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FLEX_WRAP_COUNT; index++) {
        if (FlexKeywordEquals(keyword, flexWrapVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

Result CssFlexWrapWrite(CssFlexWrap wrap,
                        char *buffer,
                        size_t capacity,
                        size_t *outLength,
                        CssDiag *diag)
{
    const FlexWrapRow *row = FlexWrapRowOf(wrap);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainFlex, RoboInvalidArgument,
                           "not a flex-wrap value");
    }
    return FlexWriteKeyword(row->keyword, buffer, capacity, outLength,
                            RoboCssDomainFlex, diag);
}

int CssFlexWrapIsWrapped(CssFlexWrap wrap)
{
    const FlexWrapRow *row = FlexWrapRowOf(wrap);

    return row ? row->wrapped : 0;
}

int CssFlexWrapIsReversed(CssFlexWrap wrap)
{
    const FlexWrapRow *row = FlexWrapRowOf(wrap);

    return row ? row->reversed : 0;
}

/* ------------------------------------------------------------------ */
/* The alignment vocabulary                                            */
/* ------------------------------------------------------------------ */

/*
 * One row per value, and four columns saying which property may hold it.
 *
 * The columns are the whole reason one enumerator set serves four properties. In CSS
 * the four are subsets of one vocabulary — `justify-content` and `align-content` take
 * the content-distribution and content-position values, `align-items` and `align-self`
 * the self-position and baseline ones, `align-self` also `auto` — and the subsets are
 * not nested, which is exactly what a single list could not express. `space-between`
 * is legal for `justify-content` and `align-content` and not for the two item
 * properties; `baseline` is legal for the three alignment properties and not for
 * `justify-content`; `left` and `right` are `justify-content`'s alone.
 *
 * `normal` is 1 for all four, which is what makes it the safe value for a cleared
 * record — every property that could hold an alignment can hold it.
 */
typedef struct flexAlignRow {
    CssFlexAlign value;
    const char *keyword;
    int justify;
    int items;
    int self;
    int content;
} FlexAlignRow;

static const FlexAlignRow flexAlignVocabulary[] = {
    { RoboCssFlexAlignNormal,       "normal",       1, 1, 1, 1 },
    { RoboCssFlexAlignStretch,      "stretch",      1, 1, 1, 1 },
    { RoboCssFlexAlignFlexStart,    "flex-start",   1, 1, 1, 1 },
    { RoboCssFlexAlignFlexEnd,      "flex-end",     1, 1, 1, 1 },
    { RoboCssFlexAlignCenter,       "center",       1, 1, 1, 1 },
    { RoboCssFlexAlignSpaceBetween, "space-between",1, 0, 0, 1 },
    { RoboCssFlexAlignSpaceAround,  "space-around", 1, 0, 0, 1 },
    { RoboCssFlexAlignSpaceEvenly,  "space-evenly", 1, 0, 0, 1 },
    { RoboCssFlexAlignBaseline,     "baseline",     0, 1, 1, 1 },
    { RoboCssFlexAlignAuto,         "auto",         0, 0, 1, 0 },
    { RoboCssFlexAlignStart,        "start",        1, 1, 1, 1 },
    { RoboCssFlexAlignEnd,          "end",          1, 1, 1, 1 },
    { RoboCssFlexAlignSelfStart,    "self-start",   0, 1, 1, 0 },
    { RoboCssFlexAlignSelfEnd,      "self-end",     0, 1, 1, 0 },
    { RoboCssFlexAlignLeft,         "left",         1, 0, 0, 0 },
    { RoboCssFlexAlignRight,        "right",        1, 0, 0, 0 }
};

#define FLEX_ALIGN_COUNT \
    (sizeof(flexAlignVocabulary) / sizeof(flexAlignVocabulary[0]))

static const FlexAlignRow *FlexAlignRowOf(CssFlexAlign align)
{
    size_t index;

    for (index = 0u; index < FLEX_ALIGN_COUNT; index++) {
        if (flexAlignVocabulary[index].value == align) {
            return &flexAlignVocabulary[index];
        }
    }
    return NULL;
}

int CssFlexAlignIsKnown(CssFlexAlign align)
{
    return FlexAlignRowOf(align) != NULL;
}

const char *CssFlexAlignName(CssFlexAlign align)
{
    const FlexAlignRow *row = FlexAlignRowOf(align);

    return row ? row->keyword : "";
}

size_t CssFlexAlignCount(void)
{
    return FLEX_ALIGN_COUNT;
}

CssFlexAlign CssFlexAlignFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFlexAlignNormal;
    }
    for (index = 0u; index < FLEX_ALIGN_COUNT; index++) {
        if (FlexKeywordEquals(keyword, flexAlignVocabulary[index].keyword)) {
            return flexAlignVocabulary[index].value;
        }
    }
    return RoboCssFlexAlignNormal;
}

CssFlexAlign CssFlexAlignFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFlexAlignNormal;
    }
    return CssFlexAlignFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFlexAlignSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FLEX_ALIGN_COUNT; index++) {
        if (FlexKeywordEquals(keyword, flexAlignVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

Result CssFlexAlignWrite(CssFlexAlign align,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag)
{
    const FlexAlignRow *row = FlexAlignRowOf(align);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainFlex, RoboInvalidArgument,
                           "not an alignment value");
    }
    return FlexWriteKeyword(row->keyword, buffer, capacity, outLength,
                            RoboCssDomainFlex, diag);
}

int CssFlexAlignRoleIsKnown(CssFlexAlignRole role)
{
    return role >= RoboCssFlexAlignRoleJustify &&
           role < (CssFlexAlignRole)RoboCssFlexAlignRoleCount;
}

const char *CssFlexAlignRoleName(CssFlexAlignRole role)
{
    switch (role) {
    case RoboCssFlexAlignRoleJustify:
        return "justify";
    case RoboCssFlexAlignRoleItems:
        return "items";
    case RoboCssFlexAlignRoleSelf:
        return "self";
    case RoboCssFlexAlignRoleContent:
        return "content";
    default:
        return "unknown";
    }
}

int CssFlexAlignAllowedFor(CssFlexAlign align, CssFlexAlignRole role)
{
    const FlexAlignRow *row = FlexAlignRowOf(align);

    if (!row || !CssFlexAlignRoleIsKnown(role)) {
        return 0;
    }
    switch (role) {
    case RoboCssFlexAlignRoleJustify:
        return row->justify;
    case RoboCssFlexAlignRoleItems:
        return row->items;
    case RoboCssFlexAlignRoleSelf:
        return row->self;
    case RoboCssFlexAlignRoleContent:
        return row->content;
    default:
        return 0;
    }
}

int CssFlexAlignIsDistributed(CssFlexAlign align)
{
    return align == RoboCssFlexAlignSpaceBetween ||
           align == RoboCssFlexAlignSpaceAround ||
           align == RoboCssFlexAlignSpaceEvenly;
}

int CssFlexAlignIsBaselinePosition(CssFlexAlign align)
{
    return align == RoboCssFlexAlignBaseline;
}

int CssFlexAlignIsAuto(CssFlexAlign align)
{
    return align == RoboCssFlexAlignAuto;
}

int CssFlexAlignIsNormal(CssFlexAlign align)
{
    return align == RoboCssFlexAlignNormal;
}

/* ------------------------------------------------------------------ */
/* flex-grow / flex-shrink                                             */
/* ------------------------------------------------------------------ */

int CssFlexFactorIsValid(double factor)
{
    /* `factor != factor` catches NaN; the comparison catches ±infinity. Both are
     * refusals rather than clamps, unlike `opacity`: a factor has no upper bound, so a
     * magnitude that overflowed is arithmetic that went wrong rather than a value an
     * author wrote. */
    return factor == factor && factor >= 0.0 && factor <= 1.7976931348623157e308;
}
