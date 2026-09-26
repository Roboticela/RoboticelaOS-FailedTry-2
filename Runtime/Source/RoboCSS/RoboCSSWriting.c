/*
 * RoboCSS writing-mode, direction and text-orientation vocabulary (Phase 18.13)
 * — see RoboCSSWriting.h.
 *
 * Three tables, one per vocabulary, and every accessor below reads one of them.
 * The writing-mode rows carry two classification columns rather than one, because
 * "vertical" and "sideways" are two questions: `vertical-rl` is vertical and not
 * sideways, `sideways-rl` is both. Nothing in this file shapes, reorders, measures
 * or places anything.
 *
 * The keywords are lowercase because that is how CSS writes them and how the
 * writers below must print them; matching is ASCII case-insensitive against these
 * spellings and against nothing else, so `rtl` matches and `rtll` does not.
 */

#include "RoboCSS/RoboCSSWriting.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/** The one keyword-write body every vocabulary below shares. */
static Result WritingWriteKeyword(const char *keyword,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainWriting, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainWriting, RoboInvalidArgument,
                           "not a writing value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainWriting, RoboNotSupported,
                           "the writing keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

/** Non-zero when @p keyword equals @p spelling, ASCII case-insensitively. */
static int WritingKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

/* ------------------------------------------------------------------ */
/* direction                                                           */
/* ------------------------------------------------------------------ */

typedef struct directionRow {
    CssDirection value;
    const char *keyword;
    int rightToLeft;
} DirectionRow;

static const DirectionRow directionVocabulary[] = {
    { RoboCssDirectionLtr, "ltr", 0 },
    { RoboCssDirectionRtl, "rtl", 1 }
};

#define DIRECTION_COUNT \
    (sizeof(directionVocabulary) / sizeof(directionVocabulary[0]))

static const DirectionRow *DirectionRowOf(CssDirection direction)
{
    size_t index;

    for (index = 0u; index < DIRECTION_COUNT; index++) {
        if (directionVocabulary[index].value == direction) {
            return &directionVocabulary[index];
        }
    }
    return NULL;
}

int CssDirectionIsKnown(CssDirection direction)
{
    return DirectionRowOf(direction) != NULL;
}

const char *CssDirectionName(CssDirection direction)
{
    const DirectionRow *row = DirectionRowOf(direction);

    return row ? row->keyword : "";
}

size_t CssDirectionCount(void)
{
    return DIRECTION_COUNT;
}

CssDirection CssDirectionFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssDirectionLtr;
    }
    for (index = 0u; index < DIRECTION_COUNT; index++) {
        if (WritingKeywordEquals(keyword, directionVocabulary[index].keyword)) {
            return directionVocabulary[index].value;
        }
    }
    return RoboCssDirectionLtr;
}

CssDirection CssDirectionFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssDirectionLtr;
    }
    return CssDirectionFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssDirectionSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < DIRECTION_COUNT; index++) {
        if (WritingKeywordEquals(keyword, directionVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssDirectionIsRightToLeft(CssDirection direction)
{
    const DirectionRow *row = DirectionRowOf(direction);

    return row ? row->rightToLeft : 0;
}

Result CssDirectionWrite(CssDirection direction,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag)
{
    const DirectionRow *row = DirectionRowOf(direction);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainWriting, RoboInvalidArgument,
                           "not a direction value");
    }
    return WritingWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* writing-mode                                                        */
/* ------------------------------------------------------------------ */

typedef struct writingModeRow {
    CssWritingMode value;
    const char *keyword;
    int horizontal;
    int vertical;
    int sideways;
} WritingModeRow;

static const WritingModeRow writingModeVocabulary[] = {
    { RoboCssWritingModeHorizontalTb, "horizontal-tb", 1, 0, 0 },
    { RoboCssWritingModeVerticalRl,   "vertical-rl",   0, 1, 0 },
    { RoboCssWritingModeVerticalLr,   "vertical-lr",   0, 1, 0 },
    { RoboCssWritingModeSidewaysRl,   "sideways-rl",   0, 1, 1 },
    { RoboCssWritingModeSidewaysLr,   "sideways-lr",   0, 1, 1 }
};

#define WRITING_MODE_COUNT \
    (sizeof(writingModeVocabulary) / sizeof(writingModeVocabulary[0]))

static const WritingModeRow *WritingModeRowOf(CssWritingMode mode)
{
    size_t index;

    for (index = 0u; index < WRITING_MODE_COUNT; index++) {
        if (writingModeVocabulary[index].value == mode) {
            return &writingModeVocabulary[index];
        }
    }
    return NULL;
}

int CssWritingModeIsKnown(CssWritingMode mode)
{
    return WritingModeRowOf(mode) != NULL;
}

const char *CssWritingModeName(CssWritingMode mode)
{
    const WritingModeRow *row = WritingModeRowOf(mode);

    return row ? row->keyword : "";
}

size_t CssWritingModeCount(void)
{
    return WRITING_MODE_COUNT;
}

CssWritingMode CssWritingModeFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssWritingModeHorizontalTb;
    }
    for (index = 0u; index < WRITING_MODE_COUNT; index++) {
        if (WritingKeywordEquals(keyword, writingModeVocabulary[index].keyword)) {
            return writingModeVocabulary[index].value;
        }
    }
    return RoboCssWritingModeHorizontalTb;
}

CssWritingMode CssWritingModeFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssWritingModeHorizontalTb;
    }
    return CssWritingModeFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssWritingModeSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < WRITING_MODE_COUNT; index++) {
        if (WritingKeywordEquals(keyword, writingModeVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssWritingModeIsHorizontal(CssWritingMode mode)
{
    const WritingModeRow *row = WritingModeRowOf(mode);

    return row ? row->horizontal : 0;
}

int CssWritingModeIsVertical(CssWritingMode mode)
{
    const WritingModeRow *row = WritingModeRowOf(mode);

    return row ? row->vertical : 0;
}

int CssWritingModeIsSideways(CssWritingMode mode)
{
    const WritingModeRow *row = WritingModeRowOf(mode);

    return row ? row->sideways : 0;
}

Result CssWritingModeWrite(CssWritingMode mode,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag)
{
    const WritingModeRow *row = WritingModeRowOf(mode);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainWriting, RoboInvalidArgument,
                           "not a writing-mode value");
    }
    return WritingWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* text-orientation                                                    */
/* ------------------------------------------------------------------ */

typedef struct textOrientationRow {
    CssTextOrientation value;
    const char *keyword;
    int sideways;
} TextOrientationRow;

static const TextOrientationRow textOrientationVocabulary[] = {
    { RoboCssTextOrientationMixed,    "mixed",    0 },
    { RoboCssTextOrientationUpright,  "upright",  0 },
    { RoboCssTextOrientationSideways, "sideways", 1 }
};

#define TEXT_ORIENTATION_COUNT \
    (sizeof(textOrientationVocabulary) / sizeof(textOrientationVocabulary[0]))

static const TextOrientationRow *TextOrientationRowOf(CssTextOrientation orientation)
{
    size_t index;

    for (index = 0u; index < TEXT_ORIENTATION_COUNT; index++) {
        if (textOrientationVocabulary[index].value == orientation) {
            return &textOrientationVocabulary[index];
        }
    }
    return NULL;
}

int CssTextOrientationIsKnown(CssTextOrientation orientation)
{
    return TextOrientationRowOf(orientation) != NULL;
}

const char *CssTextOrientationName(CssTextOrientation orientation)
{
    const TextOrientationRow *row = TextOrientationRowOf(orientation);

    return row ? row->keyword : "";
}

size_t CssTextOrientationCount(void)
{
    return TEXT_ORIENTATION_COUNT;
}

CssTextOrientation CssTextOrientationFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTextOrientationMixed;
    }
    for (index = 0u; index < TEXT_ORIENTATION_COUNT; index++) {
        if (WritingKeywordEquals(keyword,
                                 textOrientationVocabulary[index].keyword)) {
            return textOrientationVocabulary[index].value;
        }
    }
    return RoboCssTextOrientationMixed;
}

CssTextOrientation CssTextOrientationFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTextOrientationMixed;
    }
    return CssTextOrientationFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTextOrientationSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TEXT_ORIENTATION_COUNT; index++) {
        if (WritingKeywordEquals(keyword,
                                 textOrientationVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTextOrientationIsSideways(CssTextOrientation orientation)
{
    const TextOrientationRow *row = TextOrientationRowOf(orientation);

    return row ? row->sideways : 0;
}

Result CssTextOrientationWrite(CssTextOrientation orientation,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               CssDiag *diag)
{
    const TextOrientationRow *row = TextOrientationRowOf(orientation);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainWriting, RoboInvalidArgument,
                           "not a text-orientation value");
    }
    return WritingWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}
