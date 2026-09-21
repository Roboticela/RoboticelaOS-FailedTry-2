/*
 * RoboCSS positioning vocabulary (Phase 9.13) — see RoboCSSPosition.h.
 *
 * One table, and everything else in this file reads it:
 *
 *   row        one value, its keyword, whether it stays in flow, and whether it
 *              is a positioned box
 *
 * Five rows today. Adding `position: running()` is adding a row; adding the
 * sticky variants is adding rows. Neither requires touching a function below,
 * which is the entire justification for the table: every question in this module
 * used to be a place a spelling could be compared wrongly, and there is now
 * exactly one place a spelling appears.
 *
 * The keywords are lowercase and unhyphenated because that is how CSS writes them
 * and how `CssPositioningWrite` must print them; matching is ASCII
 * case-insensitive against these spellings and against nothing else, so
 * `posabsolute` is not a position and neither is a prefix of one.
 */

#include "RoboCSS/RoboCSSPosition.h"

#include "RoboCSSInternal.h"

#include <string.h>

/*
 * The vocabulary.
 *
 * `positioned` is false for `static` alone, and the two flags are not each
 * other's opposite: `relative` and `sticky` are both in flow *and* positioned,
 * which is the pair that reads as a contradiction until you notice they are two
 * questions. A relative box takes up room where flow put it and is still a
 * containing block for an absolute descendant; a sticky box is the same, with a
 * scroll constraint on top.
 */
typedef struct positionRow {
    CssPositioning value;
    const char *keyword;
    int inFlow;
    int positioned;
} PositionRow;

static const PositionRow positionVocabulary[] = {
    { RoboCssPositioningStatic,   "static",   1, 0 },
    { RoboCssPositioningRelative, "relative", 1, 1 },
    { RoboCssPositioningAbsolute, "absolute", 0, 1 },
    { RoboCssPositioningFixed,    "fixed",    0, 1 },
    { RoboCssPositioningSticky,   "sticky",   1, 1 }
};

#define POSITION_VOCABULARY_COUNT \
    (sizeof(positionVocabulary) / sizeof(positionVocabulary[0]))

/**
 * The row for @p position, or NULL when it is not one of them.
 *
 * The one lookup every accessor below goes through, so a table search exists in
 * exactly one place and a row added to the table is reachable by all of them.
 */
static const PositionRow *PositionRowOf(CssPositioning position)
{
    size_t index;

    for (index = 0u; index < POSITION_VOCABULARY_COUNT; index++) {
        if (positionVocabulary[index].value == position) {
            return &positionVocabulary[index];
        }
    }
    return NULL;
}

int CssPositioningIsKnown(CssPositioning position)
{
    return PositionRowOf(position) != NULL;
}

const char *CssPositioningName(CssPositioning position)
{
    const PositionRow *row = PositionRowOf(position);

    return row ? row->keyword : "";
}

size_t CssPositioningCount(void)
{
    return POSITION_VOCABULARY_COUNT;
}

CssPositioning CssPositioningFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssPositioningStatic;
    }
    for (index = 0u; index < POSITION_VOCABULARY_COUNT; index++) {
        if (CssBytesEqualAsciiCi(keyword.bytes, keyword.length,
                                         positionVocabulary[index].keyword)) {
            return positionVocabulary[index].value;
        }
    }
    /* Not one of them. STATIC is the answer a caller reads *after* asking
     * CssPositioningSliceIsKnown — see the header: `static` is a value, so
     * a result alone cannot say whether the identifier was recognised. */
    return RoboCssPositioningStatic;
}

CssPositioning CssPositioningFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssPositioningStatic;
    }
    return CssPositioningFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssPositioningSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < POSITION_VOCABULARY_COUNT; index++) {
        if (CssBytesEqualAsciiCi(keyword.bytes, keyword.length,
                                         positionVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

Result CssPositioningWrite(CssPositioning position,
                                      char *buffer,
                                      size_t capacity,
                                      size_t *outLength,
                                      CssDiag *diag)
{
    const PositionRow *row;
    const char *keyword;
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainPosition, RoboInvalidArgument,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    row = PositionRowOf(position);
    if (!row) {
        return RoboCssDiag(diag, RoboCssDomainPosition, RoboInvalidArgument,
                             "not a position value");
    }
    keyword = row->keyword;
    length = strlen(keyword);
    if (length >= capacity) {
        /* The length it would have needed, so a caller can size a buffer from the
         * refusal rather than from a constant it has to keep in step. */
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainPosition, RoboNotSupported,
                             "the position keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

int CssPositioningIsPositioned(CssPositioning position)
{
    const PositionRow *row = PositionRowOf(position);

    /* Not `!= STATIC`, although the two agree today: `position: running()` is not
     * positioned in this sense, and the comparison would have to be revisited on
     * the day it is a row. */
    return row ? row->positioned : 0;
}

int CssPositioningIsInFlow(CssPositioning position)
{
    const PositionRow *row = PositionRowOf(position);

    return row ? row->inFlow : 0;
}

int CssPositioningIsOutOfFlow(CssPositioning position)
{
    const PositionRow *row = PositionRowOf(position);

    /* Asked of the row rather than written as `!inFlow`, so that an unknown
     * enumerator answers 0 to both questions instead of claiming the box is
     * somewhere else. */
    return row && !row->inFlow;
}
