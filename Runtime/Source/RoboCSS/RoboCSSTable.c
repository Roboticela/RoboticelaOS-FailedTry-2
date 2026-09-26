/*
 * RoboCSS table vocabulary (Phase 18.5) — see RoboCSSTable.h.
 *
 * Five tables, one per vocabulary, and every accessor below reads one of them.
 * The tables carry the keyword, the enumerator, and the one or two classifications
 * a consumer dispatches on; nothing in this file measures, paints or lays anything
 * out.
 *
 * The keywords are lowercase because that is how CSS writes them and how the
 * writers below must print them; matching is ASCII case-insensitive against these
 * spellings and against nothing else, so `show` matches and `showw` does not.
 */

#include "RoboCSS/RoboCSSTable.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/** The one keyword-write body every vocabulary below shares. */
static Result TableWriteKeyword(const char *keyword,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainTable, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainTable, RoboInvalidArgument,
                           "not a table value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainTable, RoboNotSupported,
                           "the table keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

/** Non-zero when @p keyword equals @p spelling, ASCII case-insensitively. */
static int TableKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

/* ------------------------------------------------------------------ */
/* table-layout                                                        */
/* ------------------------------------------------------------------ */

typedef struct tableLayoutRow {
    CssTableLayout value;
    const char *keyword;
    int contentDriven;
} TableLayoutRow;

static const TableLayoutRow tableLayoutVocabulary[] = {
    { RoboCssTableLayoutAuto,  "auto",  1 },
    { RoboCssTableLayoutFixed, "fixed", 0 }
};

#define TABLE_LAYOUT_COUNT \
    (sizeof(tableLayoutVocabulary) / sizeof(tableLayoutVocabulary[0]))

static const TableLayoutRow *TableLayoutRowOf(CssTableLayout layout)
{
    size_t index;

    for (index = 0u; index < TABLE_LAYOUT_COUNT; index++) {
        if (tableLayoutVocabulary[index].value == layout) {
            return &tableLayoutVocabulary[index];
        }
    }
    return NULL;
}

int CssTableLayoutIsKnown(CssTableLayout layout)
{
    return TableLayoutRowOf(layout) != NULL;
}

const char *CssTableLayoutName(CssTableLayout layout)
{
    const TableLayoutRow *row = TableLayoutRowOf(layout);

    return row ? row->keyword : "";
}

size_t CssTableLayoutCount(void)
{
    return TABLE_LAYOUT_COUNT;
}

CssTableLayout CssTableLayoutFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTableLayoutAuto;
    }
    for (index = 0u; index < TABLE_LAYOUT_COUNT; index++) {
        if (TableKeywordEquals(keyword, tableLayoutVocabulary[index].keyword)) {
            return tableLayoutVocabulary[index].value;
        }
    }
    return RoboCssTableLayoutAuto;
}

CssTableLayout CssTableLayoutFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTableLayoutAuto;
    }
    return CssTableLayoutFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTableLayoutSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TABLE_LAYOUT_COUNT; index++) {
        if (TableKeywordEquals(keyword, tableLayoutVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTableLayoutIsContentDriven(CssTableLayout layout)
{
    const TableLayoutRow *row = TableLayoutRowOf(layout);

    return row ? row->contentDriven : 0;
}

Result CssTableLayoutWrite(CssTableLayout layout,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag)
{
    const TableLayoutRow *row = TableLayoutRowOf(layout);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainTable, RoboInvalidArgument,
                           "not a table-layout value");
    }
    return TableWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* border-collapse                                                     */
/* ------------------------------------------------------------------ */

typedef struct borderCollapseRow {
    CssBorderCollapse value;
    const char *keyword;
    int adjoin;
} BorderCollapseRow;

static const BorderCollapseRow borderCollapseVocabulary[] = {
    { RoboCssBorderCollapseSeparate, "separate", 0 },
    { RoboCssBorderCollapseCollapse, "collapse", 1 }
};

#define BORDER_COLLAPSE_COUNT \
    (sizeof(borderCollapseVocabulary) / sizeof(borderCollapseVocabulary[0]))

static const BorderCollapseRow *BorderCollapseRowOf(CssBorderCollapse collapse)
{
    size_t index;

    for (index = 0u; index < BORDER_COLLAPSE_COUNT; index++) {
        if (borderCollapseVocabulary[index].value == collapse) {
            return &borderCollapseVocabulary[index];
        }
    }
    return NULL;
}

int CssBorderCollapseIsKnown(CssBorderCollapse collapse)
{
    return BorderCollapseRowOf(collapse) != NULL;
}

const char *CssBorderCollapseName(CssBorderCollapse collapse)
{
    const BorderCollapseRow *row = BorderCollapseRowOf(collapse);

    return row ? row->keyword : "";
}

size_t CssBorderCollapseCount(void)
{
    return BORDER_COLLAPSE_COUNT;
}

CssBorderCollapse CssBorderCollapseFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssBorderCollapseSeparate;
    }
    for (index = 0u; index < BORDER_COLLAPSE_COUNT; index++) {
        if (TableKeywordEquals(keyword,
                               borderCollapseVocabulary[index].keyword)) {
            return borderCollapseVocabulary[index].value;
        }
    }
    return RoboCssBorderCollapseSeparate;
}

CssBorderCollapse CssBorderCollapseFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssBorderCollapseSeparate;
    }
    return CssBorderCollapseFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssBorderCollapseSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < BORDER_COLLAPSE_COUNT; index++) {
        if (TableKeywordEquals(keyword,
                               borderCollapseVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssBorderCollapseBordersAdjoin(CssBorderCollapse collapse)
{
    const BorderCollapseRow *row = BorderCollapseRowOf(collapse);

    return row ? row->adjoin : 0;
}

Result CssBorderCollapseWrite(CssBorderCollapse collapse,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              CssDiag *diag)
{
    const BorderCollapseRow *row = BorderCollapseRowOf(collapse);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainTable, RoboInvalidArgument,
                           "not a border-collapse value");
    }
    return TableWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* caption-side                                                        */
/* ------------------------------------------------------------------ */

typedef struct captionSideRow {
    CssCaptionSide value;
    const char *keyword;
    int blockAxis;
    int inlineAxis;
} CaptionSideRow;

static const CaptionSideRow captionSideVocabulary[] = {
    { RoboCssCaptionSideTop,         "top",          0, 0 },
    { RoboCssCaptionSideBottom,      "bottom",       0, 0 },
    { RoboCssCaptionSideBlockStart,  "block-start",  1, 0 },
    { RoboCssCaptionSideBlockEnd,    "block-end",    1, 0 },
    { RoboCssCaptionSideInlineStart, "inline-start", 0, 1 },
    { RoboCssCaptionSideInlineEnd,   "inline-end",   0, 1 }
};

#define CAPTION_SIDE_COUNT \
    (sizeof(captionSideVocabulary) / sizeof(captionSideVocabulary[0]))

static const CaptionSideRow *CaptionSideRowOf(CssCaptionSide side)
{
    size_t index;

    for (index = 0u; index < CAPTION_SIDE_COUNT; index++) {
        if (captionSideVocabulary[index].value == side) {
            return &captionSideVocabulary[index];
        }
    }
    return NULL;
}

int CssCaptionSideIsKnown(CssCaptionSide side)
{
    return CaptionSideRowOf(side) != NULL;
}

const char *CssCaptionSideName(CssCaptionSide side)
{
    const CaptionSideRow *row = CaptionSideRowOf(side);

    return row ? row->keyword : "";
}

size_t CssCaptionSideCount(void)
{
    return CAPTION_SIDE_COUNT;
}

CssCaptionSide CssCaptionSideFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssCaptionSideTop;
    }
    for (index = 0u; index < CAPTION_SIDE_COUNT; index++) {
        if (TableKeywordEquals(keyword, captionSideVocabulary[index].keyword)) {
            return captionSideVocabulary[index].value;
        }
    }
    return RoboCssCaptionSideTop;
}

CssCaptionSide CssCaptionSideFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssCaptionSideTop;
    }
    return CssCaptionSideFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssCaptionSideSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < CAPTION_SIDE_COUNT; index++) {
        if (TableKeywordEquals(keyword, captionSideVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssCaptionSideIsBlockAxis(CssCaptionSide side)
{
    const CaptionSideRow *row = CaptionSideRowOf(side);

    return row ? row->blockAxis : 0;
}

int CssCaptionSideIsInlineAxis(CssCaptionSide side)
{
    const CaptionSideRow *row = CaptionSideRowOf(side);

    return row ? row->inlineAxis : 0;
}

Result CssCaptionSideWrite(CssCaptionSide side,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag)
{
    const CaptionSideRow *row = CaptionSideRowOf(side);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainTable, RoboInvalidArgument,
                           "not a caption-side value");
    }
    return TableWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* empty-cells                                                         */
/* ------------------------------------------------------------------ */

typedef struct emptyCellsRow {
    CssEmptyCells value;
    const char *keyword;
    int drawsEmpty;
} EmptyCellsRow;

static const EmptyCellsRow emptyCellsVocabulary[] = {
    { RoboCssEmptyCellsShow, "show", 1 },
    { RoboCssEmptyCellsHide, "hide", 0 }
};

#define EMPTY_CELLS_COUNT \
    (sizeof(emptyCellsVocabulary) / sizeof(emptyCellsVocabulary[0]))

static const EmptyCellsRow *EmptyCellsRowOf(CssEmptyCells cells)
{
    size_t index;

    for (index = 0u; index < EMPTY_CELLS_COUNT; index++) {
        if (emptyCellsVocabulary[index].value == cells) {
            return &emptyCellsVocabulary[index];
        }
    }
    return NULL;
}

int CssEmptyCellsIsKnown(CssEmptyCells cells)
{
    return EmptyCellsRowOf(cells) != NULL;
}

const char *CssEmptyCellsName(CssEmptyCells cells)
{
    const EmptyCellsRow *row = EmptyCellsRowOf(cells);

    return row ? row->keyword : "";
}

size_t CssEmptyCellsCount(void)
{
    return EMPTY_CELLS_COUNT;
}

CssEmptyCells CssEmptyCellsFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssEmptyCellsShow;
    }
    for (index = 0u; index < EMPTY_CELLS_COUNT; index++) {
        if (TableKeywordEquals(keyword, emptyCellsVocabulary[index].keyword)) {
            return emptyCellsVocabulary[index].value;
        }
    }
    return RoboCssEmptyCellsShow;
}

CssEmptyCells CssEmptyCellsFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssEmptyCellsShow;
    }
    return CssEmptyCellsFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssEmptyCellsSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < EMPTY_CELLS_COUNT; index++) {
        if (TableKeywordEquals(keyword, emptyCellsVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssEmptyCellsDrawsEmpty(CssEmptyCells cells)
{
    const EmptyCellsRow *row = EmptyCellsRowOf(cells);

    return row ? row->drawsEmpty : 0;
}

Result CssEmptyCellsWrite(CssEmptyCells cells,
                          char *buffer,
                          size_t capacity,
                          size_t *outLength,
                          CssDiag *diag)
{
    const EmptyCellsRow *row = EmptyCellsRowOf(cells);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainTable, RoboInvalidArgument,
                           "not an empty-cells value");
    }
    return TableWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* visibility                                                          */
/* ------------------------------------------------------------------ */

typedef struct visibilityRow {
    CssVisibility value;
    const char *keyword;
    int takesSpace;
    int paints;
} VisibilityRow;

static const VisibilityRow visibilityVocabulary[] = {
    { RoboCssVisibilityVisible,  "visible",  1, 1 },
    { RoboCssVisibilityHidden,   "hidden",   1, 0 },
    { RoboCssVisibilityCollapse, "collapse", 0, 0 }
};

#define VISIBILITY_COUNT \
    (sizeof(visibilityVocabulary) / sizeof(visibilityVocabulary[0]))

static const VisibilityRow *VisibilityRowOf(CssVisibility visibility)
{
    size_t index;

    for (index = 0u; index < VISIBILITY_COUNT; index++) {
        if (visibilityVocabulary[index].value == visibility) {
            return &visibilityVocabulary[index];
        }
    }
    return NULL;
}

int CssVisibilityIsKnown(CssVisibility visibility)
{
    return VisibilityRowOf(visibility) != NULL;
}

const char *CssVisibilityName(CssVisibility visibility)
{
    const VisibilityRow *row = VisibilityRowOf(visibility);

    return row ? row->keyword : "";
}

size_t CssVisibilityCount(void)
{
    return VISIBILITY_COUNT;
}

CssVisibility CssVisibilityFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssVisibilityVisible;
    }
    for (index = 0u; index < VISIBILITY_COUNT; index++) {
        if (TableKeywordEquals(keyword, visibilityVocabulary[index].keyword)) {
            return visibilityVocabulary[index].value;
        }
    }
    return RoboCssVisibilityVisible;
}

CssVisibility CssVisibilityFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssVisibilityVisible;
    }
    return CssVisibilityFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssVisibilitySliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < VISIBILITY_COUNT; index++) {
        if (TableKeywordEquals(keyword, visibilityVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssVisibilityTakesSpace(CssVisibility visibility)
{
    const VisibilityRow *row = VisibilityRowOf(visibility);

    /* An unknown value answers 1: the initial value takes space, and a value this
     * build cannot read must not silently shrink a layout. */
    return row ? row->takesSpace : 1;
}

int CssVisibilityPaints(CssVisibility visibility)
{
    const VisibilityRow *row = VisibilityRowOf(visibility);

    return row ? row->paints : 0;
}

Result CssVisibilityWrite(CssVisibility visibility,
                          char *buffer,
                          size_t capacity,
                          size_t *outLength,
                          CssDiag *diag)
{
    const VisibilityRow *row = VisibilityRowOf(visibility);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainTable, RoboInvalidArgument,
                           "not a visibility value");
    }
    return TableWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}
