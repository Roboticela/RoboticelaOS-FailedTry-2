/*
 * RoboCSS multi-column vocabulary (Phase 18.6) — see RoboCSSColumn.h.
 *
 * Two tables, one per vocabulary, and every accessor below reads one of them.
 * The tables carry the keyword, the enumerator and the one classification a
 * consumer dispatches on; nothing in this file fragments, balances or lays
 * anything out.
 */

#include "RoboCSS/RoboCSSColumn.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

static Result ColumnWriteKeyword(const char *keyword,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength,
                                 CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainColumn, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainColumn, RoboInvalidArgument,
                           "not a column value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainColumn, RoboNotSupported,
                           "the column keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

static int ColumnKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

/* ------------------------------------------------------------------ */
/* column-span                                                         */
/* ------------------------------------------------------------------ */

typedef struct columnSpanRow {
    CssColumnSpan value;
    const char *keyword;
    int spansAll;
} ColumnSpanRow;

static const ColumnSpanRow columnSpanVocabulary[] = {
    { RoboCssColumnSpanNone, "none", 0 },
    { RoboCssColumnSpanAll,  "all",  1 }
};

#define COLUMN_SPAN_COUNT \
    (sizeof(columnSpanVocabulary) / sizeof(columnSpanVocabulary[0]))

static const ColumnSpanRow *ColumnSpanRowOf(CssColumnSpan span)
{
    size_t index;

    for (index = 0u; index < COLUMN_SPAN_COUNT; index++) {
        if (columnSpanVocabulary[index].value == span) {
            return &columnSpanVocabulary[index];
        }
    }
    return NULL;
}

int CssColumnSpanIsKnown(CssColumnSpan span)
{
    return ColumnSpanRowOf(span) != NULL;
}

const char *CssColumnSpanName(CssColumnSpan span)
{
    const ColumnSpanRow *row = ColumnSpanRowOf(span);

    return row ? row->keyword : "";
}

size_t CssColumnSpanCount(void)
{
    return COLUMN_SPAN_COUNT;
}

CssColumnSpan CssColumnSpanFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssColumnSpanNone;
    }
    for (index = 0u; index < COLUMN_SPAN_COUNT; index++) {
        if (ColumnKeywordEquals(keyword, columnSpanVocabulary[index].keyword)) {
            return columnSpanVocabulary[index].value;
        }
    }
    return RoboCssColumnSpanNone;
}

CssColumnSpan CssColumnSpanFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssColumnSpanNone;
    }
    return CssColumnSpanFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssColumnSpanSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < COLUMN_SPAN_COUNT; index++) {
        if (ColumnKeywordEquals(keyword, columnSpanVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssColumnSpanSpansAll(CssColumnSpan span)
{
    const ColumnSpanRow *row = ColumnSpanRowOf(span);

    return row ? row->spansAll : 0;
}

Result CssColumnSpanWrite(CssColumnSpan span,
                          char *buffer,
                          size_t capacity,
                          size_t *outLength,
                          CssDiag *diag)
{
    const ColumnSpanRow *row = ColumnSpanRowOf(span);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainColumn, RoboInvalidArgument,
                           "not a column-span value");
    }
    return ColumnWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* column-fill                                                         */
/* ------------------------------------------------------------------ */

typedef struct columnFillRow {
    CssColumnFill value;
    const char *keyword;
    int balances;
} ColumnFillRow;

static const ColumnFillRow columnFillVocabulary[] = {
    { RoboCssColumnFillAuto,    "auto",    0 },
    { RoboCssColumnFillBalance, "balance", 1 }
};

#define COLUMN_FILL_COUNT \
    (sizeof(columnFillVocabulary) / sizeof(columnFillVocabulary[0]))

static const ColumnFillRow *ColumnFillRowOf(CssColumnFill fill)
{
    size_t index;

    for (index = 0u; index < COLUMN_FILL_COUNT; index++) {
        if (columnFillVocabulary[index].value == fill) {
            return &columnFillVocabulary[index];
        }
    }
    return NULL;
}

int CssColumnFillIsKnown(CssColumnFill fill)
{
    return ColumnFillRowOf(fill) != NULL;
}

const char *CssColumnFillName(CssColumnFill fill)
{
    const ColumnFillRow *row = ColumnFillRowOf(fill);

    return row ? row->keyword : "";
}

size_t CssColumnFillCount(void)
{
    return COLUMN_FILL_COUNT;
}

CssColumnFill CssColumnFillFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssColumnFillAuto;
    }
    for (index = 0u; index < COLUMN_FILL_COUNT; index++) {
        if (ColumnKeywordEquals(keyword, columnFillVocabulary[index].keyword)) {
            return columnFillVocabulary[index].value;
        }
    }
    return RoboCssColumnFillAuto;
}

CssColumnFill CssColumnFillFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssColumnFillAuto;
    }
    return CssColumnFillFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssColumnFillSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < COLUMN_FILL_COUNT; index++) {
        if (ColumnKeywordEquals(keyword, columnFillVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssColumnFillBalances(CssColumnFill fill)
{
    const ColumnFillRow *row = ColumnFillRowOf(fill);

    return row ? row->balances : 0;
}

Result CssColumnFillWrite(CssColumnFill fill,
                          char *buffer,
                          size_t capacity,
                          size_t *outLength,
                          CssDiag *diag)
{
    const ColumnFillRow *row = ColumnFillRowOf(fill);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainColumn, RoboInvalidArgument,
                           "not a column-fill value");
    }
    return ColumnWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}
