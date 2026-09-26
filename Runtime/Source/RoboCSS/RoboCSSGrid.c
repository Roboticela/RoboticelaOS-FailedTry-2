/*
 * RoboCSS grid vocabulary (Phase 18.4) — see RoboCSSGrid.h.
 *
 * One table, and everything else in this file reads it:
 *
 *   flow   one value, its canonical keyword, whether auto-placement follows columns,
 *          and whether it packs densely
 *
 * The interesting part of the file is not the table but the reader below it. `row`,
 * `column` and `dense` are each one identifier, but `row dense` is two, so the
 * single-identifier door the other closed vocabularies use cannot see the density — and
 * a build that read `grid-auto-flow: row dense` as `row` would lose the one thing
 * density changes: which *order* the items appear in. So the reader works on the
 * component value, collects the significant components, classifies each as an axis or a
 * density, and refuses a combination that repeats a half.
 *
 * The keywords are lowercase because that is how CSS writes them and how the writer
 * below must print them; matching is ASCII case-insensitive against these spellings and
 * against nothing else, so `row` matches and `roww` does not.
 */

#include "RoboCSS/RoboCSSGrid.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/** Non-zero when @p keyword equals @p spelling, ASCII case-insensitively. */
static int GridKeywordEquals(CssSlice keyword, const char *spelling)
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
static Result GridWriteKeyword(const char *keyword,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainGrid, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainGrid, RoboInvalidArgument,
                           "not a grid value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainGrid, RoboNotSupported,
                           "the grid keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* grid-auto-flow                                                      */
/* ------------------------------------------------------------------ */

typedef struct gridAutoFlowRow {
    CssGridAutoFlow value;
    const char *keyword;
    int column; /* auto-placement follows columns */
    int dense;  /* holes are backfilled */
} GridAutoFlowRow;

static const GridAutoFlowRow gridAutoFlowVocabulary[] = {
    { RoboCssGridAutoFlowRow,         "row",          0, 0 },
    { RoboCssGridAutoFlowColumn,      "column",       1, 0 },
    { RoboCssGridAutoFlowRowDense,    "row dense",    0, 1 },
    { RoboCssGridAutoFlowColumnDense, "column dense", 1, 1 }
};

#define GRID_AUTO_FLOW_COUNT \
    (sizeof(gridAutoFlowVocabulary) / sizeof(gridAutoFlowVocabulary[0]))

static const GridAutoFlowRow *GridAutoFlowRowOf(CssGridAutoFlow flow)
{
    size_t index;

    for (index = 0u; index < GRID_AUTO_FLOW_COUNT; index++) {
        if (gridAutoFlowVocabulary[index].value == flow) {
            return &gridAutoFlowVocabulary[index];
        }
    }
    return NULL;
}

int CssGridAutoFlowIsKnown(CssGridAutoFlow flow)
{
    return GridAutoFlowRowOf(flow) != NULL;
}

const char *CssGridAutoFlowName(CssGridAutoFlow flow)
{
    const GridAutoFlowRow *row = GridAutoFlowRowOf(flow);

    return row ? row->keyword : "";
}

size_t CssGridAutoFlowCount(void)
{
    return GRID_AUTO_FLOW_COUNT;
}

CssGridAutoFlow CssGridAutoFlowFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssGridAutoFlowRow;
    }
    /* The one-word spelling `dense` names `row dense`, and it is the only word that is
     * not the name of a row: it is a *column*, not a row, of `row dense`. Asking it
     * here rather than assembling the two-word spelling keeps the special case in one
     * place, next to the table it is a fact about. */
    if (GridKeywordEquals(keyword, "dense")) {
        return RoboCssGridAutoFlowRowDense;
    }
    for (index = 0u; index < GRID_AUTO_FLOW_COUNT; index++) {
        if (GridKeywordEquals(keyword, gridAutoFlowVocabulary[index].keyword)) {
            return gridAutoFlowVocabulary[index].value;
        }
    }
    return RoboCssGridAutoFlowRow;
}

CssGridAutoFlow CssGridAutoFlowFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssGridAutoFlowRow;
    }
    return CssGridAutoFlowFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssGridAutoFlowSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    if (GridKeywordEquals(keyword, "dense")) {
        return 1;
    }
    for (index = 0u; index < GRID_AUTO_FLOW_COUNT; index++) {
        if (GridKeywordEquals(keyword, gridAutoFlowVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

CssGridAutoFlow CssGridAutoFlowFromParts(int column, int dense)
{
    size_t index;

    for (index = 0u; index < GRID_AUTO_FLOW_COUNT; index++) {
        if (gridAutoFlowVocabulary[index].column == (column != 0) &&
            gridAutoFlowVocabulary[index].dense == (dense != 0)) {
            return gridAutoFlowVocabulary[index].value;
        }
    }
    return RoboCssGridAutoFlowRow;
}

Result CssGridAutoFlowWrite(CssGridAutoFlow flow,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            CssDiag *diag)
{
    const GridAutoFlowRow *row = GridAutoFlowRowOf(flow);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainGrid, RoboInvalidArgument,
                           "not a grid-auto-flow value");
    }
    return GridWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

int CssGridAutoFlowIsColumn(CssGridAutoFlow flow)
{
    const GridAutoFlowRow *row = GridAutoFlowRowOf(flow);

    /* An unknown value answers 0 rather than 1: "auto-placement follows columns" is a
     * claim a value nothing defined cannot support, and a column is the wrong guess to
     * make about a value that could not be read. */
    return row ? row->column : 0;
}

int CssGridAutoFlowIsDense(CssGridAutoFlow flow)
{
    const GridAutoFlowRow *row = GridAutoFlowRowOf(flow);

    return row ? row->dense : 0;
}

/* ------------------------------------------------------------------ */
/* Reading `grid-auto-flow` from a component value                     */
/* ------------------------------------------------------------------ */

int CssGridParseIsKnown(CssGridParse outcome)
{
    return outcome >= RoboCssGridParseInvalid &&
           outcome < (CssGridParse)RoboCssGridParseCount;
}

const char *CssGridParseName(CssGridParse outcome)
{
    switch (outcome) {
    case RoboCssGridParseInvalid:
        return "invalid";
    case RoboCssGridParseFlow:
        return "flow";
    case RoboCssGridParseDeferred:
        return "deferred";
    default:
        return "unknown";
    }
}

/** Whether @p value is a lone `var()` function — the one reference form deferred here. */
static int GridValueIsVarFunction(const CssValue *value)
{
    if (!value || value->kind != RoboCssValueKindFunction) {
        return 0;
    }
    return CssBytesEqualAsciiCi(value->name.bytes, value->name.length, "var");
}

/** Classify one significant component: -1 unknown, 0 axis-word, 1 density-word. */
static int GridAutoFlowComponentKind(const CssValue *component,
                                     int *outColumn)
{
    char keyword[64];
    size_t length = 0u;

    if (!component || component->kind != RoboCssValueKindToken) {
        return -1;
    }
    if (component->token.type != RoboCssTokenIdent) {
        return -1;
    }
    if (!CssTokenKeywordText(&component->token, keyword, sizeof(keyword),
                             &length)) {
        return -1;
    }
    if (CssBytesEqualAsciiCi((const uint8_t *)keyword, length, "dense")) {
        return 1;
    }
    if (CssBytesEqualAsciiCi((const uint8_t *)keyword, length, "row")) {
        *outColumn = 0;
        return 0;
    }
    if (CssBytesEqualAsciiCi((const uint8_t *)keyword, length, "column")) {
        *outColumn = 1;
        return 0;
    }
    return -1;
}

CssGridParse CssGridAutoFlowFromValue(const CssValue *specified,
                                      CssGridAutoFlow *out)
{
    const CssValue *components[2];
    size_t count = 0u;
    size_t index;
    int haveAxis = 0;
    int column = 0;
    int dense = 0;

    if (!out) {
        return RoboCssGridParseInvalid;
    }
    if (!specified || specified->kind == RoboCssValueKindNone) {
        return RoboCssGridParseInvalid;
    }
    /*
     * A lone reference is deferred, and only `var()` is: `calc()` and the maths family
     * are numbers, and a `grid-auto-flow` is two keywords, so a function that is not a
     * reference is simply not this value. Deferring every function would carry
     * `grid-auto-flow: rotate(3deg)` as legal CSS, which it is not.
     *
     * The declaration's value may itself be the function or a list wrapping it —
     * `CssValueSingle` is the door that answers both, and answers NULL when there is
     * more than one significant component, which is exactly when this is not a lone
     * reference.
     */
    if (GridValueIsVarFunction(CssValueSingle(specified))) {
        return RoboCssGridParseDeferred;
    }
    /*
     * Collect the significant children. The value arrives either as a single component
     * (one identifier, or one function) or as a list of components separated by
     * whitespace, and only the whitespace between them is trivia. Two is the most a
     * `grid-auto-flow` can hold, so a third component is a refusal rather than a value
     * this build reads halfway.
     */
    if (CssValueChildCount(specified) == 0u) {
        /* Not a list: the value *is* one component. */
        if (!CssValueIsTrivia(specified)) {
            components[count++] = specified;
        }
    } else {
        for (index = 0u; index < CssValueChildCount(specified); index++) {
            const CssValue *child = CssValueChild(specified, index);

            if (CssValueIsTrivia(child)) {
                continue;
            }
            if (count == 2u) {
                return RoboCssGridParseInvalid;
            }
            components[count++] = child;
        }
    }
    if (count == 0u) {
        return RoboCssGridParseInvalid;
    }
    for (index = 0u; index < count; index++) {
        int kind;
        int componentColumn = 0;

        kind = GridAutoFlowComponentKind(components[index], &componentColumn);
        if (kind < 0) {
            return RoboCssGridParseInvalid;
        }
        if (kind == 0) {
            if (haveAxis) {
                /* `row column` repeats the half that there is one of. */
                return RoboCssGridParseInvalid;
            }
            haveAxis = 1;
            column = componentColumn;
        } else {
            if (dense) {
                return RoboCssGridParseInvalid;
            }
            dense = 1;
        }
    }
    *out = CssGridAutoFlowFromParts(column, dense);
    return RoboCssGridParseFlow;
}
