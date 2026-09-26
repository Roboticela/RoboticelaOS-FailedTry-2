/*
 * RoboCSS logical properties (Phase 18.12) — see RoboCSSLogical.h.
 *
 * Two tables for the side/axis vocabulary and one resolver. The resolver's four
 * answers come from two rows — the block axis is the mode's and the inline axis is
 * the mode's turned by `direction` — so the mapping is a table read, not a chain of
 * `if`s, and the two `sideways-*` modes share their vertical counterpart's axes.
 */

#include "RoboCSS/RoboCSSLogical.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

static Result LogicalWriteKeyword(const char *keyword,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainLogical, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainLogical, RoboInvalidArgument,
                           "not a logical value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainLogical, RoboNotSupported,
                           "the logical keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

static int LogicalKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

/* ------------------------------------------------------------------ */
/* logical sides                                                       */
/* ------------------------------------------------------------------ */

typedef struct logicalSideRow {
    CssLogicalSide value;
    const char *keyword;
    CssLogicalAxis axis;
    int isStart;
} LogicalSideRow;

static const LogicalSideRow logicalSideVocabulary[] = {
    { RoboCssLogicalSideBlockStart,  "block-start",  RoboCssLogicalAxisBlock,  1 },
    { RoboCssLogicalSideBlockEnd,    "block-end",    RoboCssLogicalAxisBlock,  0 },
    { RoboCssLogicalSideInlineStart, "inline-start", RoboCssLogicalAxisInline, 1 },
    { RoboCssLogicalSideInlineEnd,   "inline-end",   RoboCssLogicalAxisInline, 0 }
};

#define LOGICAL_SIDE_COUNT \
    (sizeof(logicalSideVocabulary) / sizeof(logicalSideVocabulary[0]))

static const LogicalSideRow *LogicalSideRowOf(CssLogicalSide side)
{
    size_t index;

    for (index = 0u; index < LOGICAL_SIDE_COUNT; index++) {
        if (logicalSideVocabulary[index].value == side) {
            return &logicalSideVocabulary[index];
        }
    }
    return NULL;
}

int CssLogicalSideIsKnown(CssLogicalSide side)
{
    return LogicalSideRowOf(side) != NULL;
}

const char *CssLogicalSideName(CssLogicalSide side)
{
    const LogicalSideRow *row = LogicalSideRowOf(side);

    return row ? row->keyword : "";
}

size_t CssLogicalSideCount(void)
{
    return LOGICAL_SIDE_COUNT;
}

CssLogicalSide CssLogicalSideFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssLogicalSideBlockStart;
    }
    for (index = 0u; index < LOGICAL_SIDE_COUNT; index++) {
        if (LogicalKeywordEquals(keyword, logicalSideVocabulary[index].keyword)) {
            return logicalSideVocabulary[index].value;
        }
    }
    return RoboCssLogicalSideBlockStart;
}

CssLogicalSide CssLogicalSideFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssLogicalSideBlockStart;
    }
    return CssLogicalSideFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssLogicalSideSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < LOGICAL_SIDE_COUNT; index++) {
        if (LogicalKeywordEquals(keyword, logicalSideVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

CssLogicalAxis CssLogicalSideAxis(CssLogicalSide side)
{
    const LogicalSideRow *row = LogicalSideRowOf(side);

    return row ? row->axis : RoboCssLogicalAxisBlock;
}

int CssLogicalSideIsStart(CssLogicalSide side)
{
    const LogicalSideRow *row = LogicalSideRowOf(side);

    return row ? row->isStart : 0;
}

Result CssLogicalSideWrite(CssLogicalSide side,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag)
{
    const LogicalSideRow *row = LogicalSideRowOf(side);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainLogical, RoboInvalidArgument,
                           "not a logical side");
    }
    return LogicalWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* logical axes                                                        */
/* ------------------------------------------------------------------ */

typedef struct logicalAxisRow {
    CssLogicalAxis value;
    const char *keyword;
} LogicalAxisRow;

static const LogicalAxisRow logicalAxisVocabulary[] = {
    { RoboCssLogicalAxisBlock,  "block"  },
    { RoboCssLogicalAxisInline, "inline" }
};

#define LOGICAL_AXIS_COUNT \
    (sizeof(logicalAxisVocabulary) / sizeof(logicalAxisVocabulary[0]))

static const LogicalAxisRow *LogicalAxisRowOf(CssLogicalAxis axis)
{
    size_t index;

    for (index = 0u; index < LOGICAL_AXIS_COUNT; index++) {
        if (logicalAxisVocabulary[index].value == axis) {
            return &logicalAxisVocabulary[index];
        }
    }
    return NULL;
}

int CssLogicalAxisIsKnown(CssLogicalAxis axis)
{
    return LogicalAxisRowOf(axis) != NULL;
}

const char *CssLogicalAxisName(CssLogicalAxis axis)
{
    const LogicalAxisRow *row = LogicalAxisRowOf(axis);

    return row ? row->keyword : "";
}

size_t CssLogicalAxisCount(void)
{
    return LOGICAL_AXIS_COUNT;
}

CssLogicalAxis CssLogicalAxisFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssLogicalAxisBlock;
    }
    for (index = 0u; index < LOGICAL_AXIS_COUNT; index++) {
        if (LogicalKeywordEquals(keyword, logicalAxisVocabulary[index].keyword)) {
            return logicalAxisVocabulary[index].value;
        }
    }
    return RoboCssLogicalAxisBlock;
}

CssLogicalAxis CssLogicalAxisFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssLogicalAxisBlock;
    }
    return CssLogicalAxisFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssLogicalAxisSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < LOGICAL_AXIS_COUNT; index++) {
        if (LogicalKeywordEquals(keyword, logicalAxisVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* resolution                                                          */
/* ------------------------------------------------------------------ */

/*
 * The block axis is the mode's, and the inline axis is the mode's turned by
 * `direction`. Four rows, one per physical orientation the mode pair can take:
 *
 *   blockDown   block axis runs top → bottom   (horizontal-tb)
 *   blockRight  block axis runs right → left   (vertical-rl, sideways-rl)
 *   blockLeft   block axis runs left → right   (vertical-lr, sideways-lr)
 *
 * A vertical mode's inline axis runs top → bottom for `ltr` and bottom → top for
 * `rtl`. `horizontal-tb`'s inline axis is horizontal, and `ltr`/`rtl` pick the
 * direction. `direction` is read on the inline axis in all three cases, which is why
 * the vertical `rtl` case is not the horizontal one mirrored.
 */
CssBoxSide CssLogicalResolveSide(CssLogicalSide side,
                                 CssWritingMode mode,
                                 CssDirection direction)
{
    int rtl = CssDirectionIsRightToLeft(direction);
    int blockIsVertical;

    /* An unknown writing mode is read as `horizontal-tb`, the safe answer. */
    blockIsVertical = CssWritingModeIsVertical(mode);

    if (!blockIsVertical) {
        /* horizontal-tb: block top→bottom, inline left/right by direction. */
        switch (side) {
        case RoboCssLogicalSideBlockStart:
            return RoboCssBoxSideTop;
        case RoboCssLogicalSideBlockEnd:
            return RoboCssBoxSideBottom;
        case RoboCssLogicalSideInlineStart:
            return rtl ? RoboCssBoxSideRight : RoboCssBoxSideLeft;
        case RoboCssLogicalSideInlineEnd:
            return rtl ? RoboCssBoxSideLeft : RoboCssBoxSideRight;
        default:
            return RoboCssBoxSideTop;
        }
    }

    /* A vertical mode: block is horizontal (rl → right, lr → left). */
    if (mode == RoboCssWritingModeVerticalRl ||
        mode == RoboCssWritingModeSidewaysRl) {
        switch (side) {
        case RoboCssLogicalSideBlockStart:
            return RoboCssBoxSideRight;
        case RoboCssLogicalSideBlockEnd:
            return RoboCssBoxSideLeft;
        case RoboCssLogicalSideInlineStart:
            return rtl ? RoboCssBoxSideBottom : RoboCssBoxSideTop;
        case RoboCssLogicalSideInlineEnd:
            return rtl ? RoboCssBoxSideTop : RoboCssBoxSideBottom;
        default:
            return RoboCssBoxSideRight;
        }
    }
    /* vertical-lr / sideways-lr: block left → right. */
    switch (side) {
    case RoboCssLogicalSideBlockStart:
        return RoboCssBoxSideLeft;
    case RoboCssLogicalSideBlockEnd:
        return RoboCssBoxSideRight;
    case RoboCssLogicalSideInlineStart:
        return rtl ? RoboCssBoxSideBottom : RoboCssBoxSideTop;
    case RoboCssLogicalSideInlineEnd:
        return rtl ? RoboCssBoxSideTop : RoboCssBoxSideBottom;
    default:
        return RoboCssBoxSideLeft;
    }
}
