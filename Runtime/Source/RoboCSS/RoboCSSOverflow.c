/*
 * RoboCSS overflow vocabulary (Phase 10.13) — see RoboCSSOverflow.h.
 *
 * One table, and everything else in this file reads it:
 *
 *   row      one value, its keyword, and whether it clips
 *
 * Two rows. Adding `scroll` is a row whose `clips` is 1 and whose *behaviour* is a
 * later phase's; adding `clip` is a row whose clipping boundary differs from
 * `hidden`'s, which is why the boundary question is asked of the table's classification
 * rather than spelled out at the call site. Neither addition touches a function below,
 * which is the entire justification for the table: every question in this module used
 * to be a place a spelling could be compared wrongly, and there is now exactly one
 * place a spelling appears.
 *
 * The keywords are lowercase because that is how CSS writes them and how
 * `CssOverflowWrite` must print them; matching is ASCII case-insensitive
 * against these spellings and against nothing else, so `hidd` is not an overflow
 * value and neither is a prefix of one.
 */

#include "RoboCSS/RoboCSSOverflow.h"

#include "RoboCSSInternal.h"

#include <string.h>

/*
 * The vocabulary.
 *
 * `clips` is 1 for `hidden` and 0 for `visible`, and it is a field rather than a
 * comparison against the enumerator for the reason the header gives: a consumer asks
 * "does this axis have a boundary?" and the day a third row answers *differently*
 * from `hidden` -- `clip`, whose boundary is the same rectangle but whose content is
 * not scrollable -- the answer changes here rather than at every call site.
 */
typedef struct overflowRow {
    CssOverflow value;
    const char *keyword;
    int clips;
} OverflowRow;

static const OverflowRow overflowVocabulary[] = {
    { RoboCssOverflowVisible, "visible", 0 },
    { RoboCssOverflowHidden,  "hidden",  1 }
};

#define OVERFLOW_VOCABULARY_COUNT \
    (sizeof(overflowVocabulary) / sizeof(overflowVocabulary[0]))

/**
 * The row for @p overflow, or NULL when it is not one of them.
 *
 * The one lookup every accessor below goes through, so a table search exists in
 * exactly one place and a row added to the table is reachable by all of them.
 */
static const OverflowRow *OverflowRowOf(CssOverflow overflow)
{
    size_t index;

    for (index = 0u; index < OVERFLOW_VOCABULARY_COUNT; index++) {
        if (overflowVocabulary[index].value == overflow) {
            return &overflowVocabulary[index];
        }
    }
    return NULL;
}

int CssOverflowIsKnown(CssOverflow overflow)
{
    return OverflowRowOf(overflow) != NULL;
}

const char *CssOverflowName(CssOverflow overflow)
{
    const OverflowRow *row = OverflowRowOf(overflow);

    return row ? row->keyword : "";
}

size_t CssOverflowCount(void)
{
    return OVERFLOW_VOCABULARY_COUNT;
}

CssOverflow CssOverflowFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssOverflowVisible;
    }
    for (index = 0u; index < OVERFLOW_VOCABULARY_COUNT; index++) {
        if (CssBytesEqualAsciiCi(keyword.bytes, keyword.length,
                                          overflowVocabulary[index].keyword)) {
            return overflowVocabulary[index].value;
        }
    }
    /* Not one of them. VISIBLE is the answer a caller reads *after* asking
     * CssOverflowSliceIsKnown — see the header: `visible` is a value, so a
     * result alone cannot say whether the identifier was recognised. */
    return RoboCssOverflowVisible;
}

CssOverflow CssOverflowFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssOverflowVisible;
    }
    return CssOverflowFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssOverflowSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < OVERFLOW_VOCABULARY_COUNT; index++) {
        if (CssBytesEqualAsciiCi(keyword.bytes, keyword.length,
                                          overflowVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

Result CssOverflowWrite(CssOverflow overflow,
                                      char *buffer,
                                      size_t capacity,
                                      size_t *outLength,
                                      CssDiag *diag)
{
    const OverflowRow *row;
    const char *keyword;
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainOverflow, RoboInvalidArgument,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    row = OverflowRowOf(overflow);
    if (!row) {
        return RoboCssDiag(diag, RoboCssDomainOverflow, RoboInvalidArgument,
                             "not an overflow value");
    }
    keyword = row->keyword;
    length = strlen(keyword);
    if (length >= capacity) {
        /* The length it would have needed, so a caller can size a buffer from the
         * refusal rather than from a constant it has to keep in step. */
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainOverflow, RoboNotSupported,
                             "the overflow keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

int CssOverflowClips(CssOverflow overflow)
{
    const OverflowRow *row = OverflowRowOf(overflow);

    /* An unknown enumerator answers 0 rather than 1, and the direction matters: a
     * record nothing can read must not make a box clip its contents, because clipping
     * throws geometry away and *not* clipping only draws outside a box. */
    return row ? row->clips : 0;
}

int CssOverflowIsVisible(CssOverflow overflow)
{
    return overflow == RoboCssOverflowVisible;
}
