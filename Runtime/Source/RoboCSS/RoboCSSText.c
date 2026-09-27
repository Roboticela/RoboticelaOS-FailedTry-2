/*
 * RoboCSS text vocabulary (Phase 18.15) — see RoboCSSText.h.
 *
 * Fourteen tables, one per closed set, and every accessor below reads one of them.
 * The rows carry the keyword, the enumerator and the classifications a consumer
 * dispatches on; nothing in this file wraps a line, distributes slack, finds a
 * hyphenation point, changes a case or measures a glyph.
 *
 * The keywords are lowercase because that is how CSS writes them and how the writers
 * below must print them; matching is ASCII case-insensitive against these spellings
 * and against nothing else, so `nowrap` matches and `no-wrap` does not.
 *
 * Two habits are worth naming because they are what the classifications are for.
 *
 * First, a classification is never the negation of another: `CssTextAlignIsLogical`
 * and `CssTextAlignIsPhysical` are both false for `center`, and
 * `CssTextDecorationStyleIsSegmented` is false for `double` as well as for `solid`.
 * A consumer that reads one as "not the other" is asking a question the table does
 * not answer, which is why each predicate is a column rather than a comparison.
 *
 * Second, an unknown enumerator answers the value that changes **least**: a
 * `white-space` this build cannot read collapses its spaces and wraps (the initial
 * value's behaviour, not "preserve everything"), and an `overflow-wrap` it cannot
 * read breaks nothing. A classification that defaulted the other way would reflow or
 * overflow a document nobody asked to change.
 */

#include "RoboCSS/RoboCSSText.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/** The one keyword-write body every vocabulary below shares. */
static Result TextWriteKeyword(const char *keyword,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainText, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainText, RoboInvalidArgument,
                           "not a text value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainText, RoboNotSupported,
                           "the text keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

/** Non-zero when @p keyword equals @p spelling, ASCII case-insensitively. */
static int TextKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

/** The one "not a defined value" refusal every writer below shares. */
static Result TextWriteUnknown(char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               CssDiag *diag,
                               const char *what)
{
    if (buffer && outLength && capacity > 0u) {
        buffer[0] = '\0';
    }
    return RoboCssDiag(diag, RoboCssDomainText, RoboInvalidArgument, what);
}

/* ------------------------------------------------------------------ */
/* text-align                                                          */
/* ------------------------------------------------------------------ */

typedef struct textAlignRow {
    CssTextAlign value;
    const char *keyword;
    int logical;
    int physical;
    int justified;
} TextAlignRow;

static const TextAlignRow textAlignVocabulary[] = {
    { RoboCssTextAlignStart,       "start",        1, 0, 0 },
    { RoboCssTextAlignEnd,         "end",          1, 0, 0 },
    { RoboCssTextAlignLeft,        "left",         0, 1, 0 },
    { RoboCssTextAlignRight,       "right",        0, 1, 0 },
    { RoboCssTextAlignCenter,      "center",       0, 0, 0 },
    { RoboCssTextAlignJustify,     "justify",      0, 0, 1 },
    { RoboCssTextAlignMatchParent, "match-parent", 1, 0, 0 }
};

#define TEXT_ALIGN_COUNT \
    (sizeof(textAlignVocabulary) / sizeof(textAlignVocabulary[0]))

static const TextAlignRow *TextAlignRowOf(CssTextAlign align)
{
    size_t index;

    for (index = 0u; index < TEXT_ALIGN_COUNT; index++) {
        if (textAlignVocabulary[index].value == align) {
            return &textAlignVocabulary[index];
        }
    }
    return NULL;
}

int CssTextAlignIsKnown(CssTextAlign align)
{
    return TextAlignRowOf(align) != NULL;
}

const char *CssTextAlignName(CssTextAlign align)
{
    const TextAlignRow *row = TextAlignRowOf(align);

    return row ? row->keyword : "";
}

size_t CssTextAlignCount(void)
{
    return TEXT_ALIGN_COUNT;
}

CssTextAlign CssTextAlignFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTextAlignStart;
    }
    for (index = 0u; index < TEXT_ALIGN_COUNT; index++) {
        if (TextKeywordEquals(keyword, textAlignVocabulary[index].keyword)) {
            return textAlignVocabulary[index].value;
        }
    }
    return RoboCssTextAlignStart;
}

CssTextAlign CssTextAlignFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTextAlignStart;
    }
    return CssTextAlignFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTextAlignSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TEXT_ALIGN_COUNT; index++) {
        if (TextKeywordEquals(keyword, textAlignVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTextAlignIsLogical(CssTextAlign align)
{
    const TextAlignRow *row = TextAlignRowOf(align);

    return row ? row->logical : 0;
}

int CssTextAlignIsPhysical(CssTextAlign align)
{
    const TextAlignRow *row = TextAlignRowOf(align);

    return row ? row->physical : 0;
}

int CssTextAlignIsJustified(CssTextAlign align)
{
    const TextAlignRow *row = TextAlignRowOf(align);

    return row ? row->justified : 0;
}

Result CssTextAlignWrite(CssTextAlign align,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag)
{
    const TextAlignRow *row = TextAlignRowOf(align);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a text-align value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* text-align-last                                                     */
/* ------------------------------------------------------------------ */

typedef struct textAlignLastRow {
    CssTextAlignLast value;
    const char *keyword;
    int automatic;
    int logical;
    int physical;
    int justified;
} TextAlignLastRow;

static const TextAlignLastRow textAlignLastVocabulary[] = {
    { RoboCssTextAlignLastAuto,    "auto",    1, 0, 0, 0 },
    { RoboCssTextAlignLastStart,   "start",   0, 1, 0, 0 },
    { RoboCssTextAlignLastEnd,     "end",     0, 1, 0, 0 },
    { RoboCssTextAlignLastLeft,    "left",    0, 0, 1, 0 },
    { RoboCssTextAlignLastRight,   "right",   0, 0, 1, 0 },
    { RoboCssTextAlignLastCenter,  "center",  0, 0, 0, 0 },
    { RoboCssTextAlignLastJustify, "justify", 0, 0, 0, 1 }
};

#define TEXT_ALIGN_LAST_COUNT \
    (sizeof(textAlignLastVocabulary) / sizeof(textAlignLastVocabulary[0]))

static const TextAlignLastRow *TextAlignLastRowOf(CssTextAlignLast align)
{
    size_t index;

    for (index = 0u; index < TEXT_ALIGN_LAST_COUNT; index++) {
        if (textAlignLastVocabulary[index].value == align) {
            return &textAlignLastVocabulary[index];
        }
    }
    return NULL;
}

int CssTextAlignLastIsKnown(CssTextAlignLast align)
{
    return TextAlignLastRowOf(align) != NULL;
}

const char *CssTextAlignLastName(CssTextAlignLast align)
{
    const TextAlignLastRow *row = TextAlignLastRowOf(align);

    return row ? row->keyword : "";
}

size_t CssTextAlignLastCount(void)
{
    return TEXT_ALIGN_LAST_COUNT;
}

CssTextAlignLast CssTextAlignLastFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTextAlignLastAuto;
    }
    for (index = 0u; index < TEXT_ALIGN_LAST_COUNT; index++) {
        if (TextKeywordEquals(keyword, textAlignLastVocabulary[index].keyword)) {
            return textAlignLastVocabulary[index].value;
        }
    }
    return RoboCssTextAlignLastAuto;
}

CssTextAlignLast CssTextAlignLastFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTextAlignLastAuto;
    }
    return CssTextAlignLastFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTextAlignLastSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TEXT_ALIGN_LAST_COUNT; index++) {
        if (TextKeywordEquals(keyword, textAlignLastVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTextAlignLastIsAutomatic(CssTextAlignLast align)
{
    const TextAlignLastRow *row = TextAlignLastRowOf(align);

    return row ? row->automatic : 0;
}

int CssTextAlignLastIsLogical(CssTextAlignLast align)
{
    const TextAlignLastRow *row = TextAlignLastRowOf(align);

    return row ? row->logical : 0;
}

int CssTextAlignLastIsPhysical(CssTextAlignLast align)
{
    const TextAlignLastRow *row = TextAlignLastRowOf(align);

    return row ? row->physical : 0;
}

int CssTextAlignLastIsJustified(CssTextAlignLast align)
{
    const TextAlignLastRow *row = TextAlignLastRowOf(align);

    return row ? row->justified : 0;
}

Result CssTextAlignLastWrite(CssTextAlignLast align,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag)
{
    const TextAlignLastRow *row = TextAlignLastRowOf(align);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a text-align-last value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* text-justify                                                        */
/* ------------------------------------------------------------------ */

typedef struct textJustifyRow {
    CssTextJustify value;
    const char *keyword;
    int justifies;
    int expandsCharacters;
} TextJustifyRow;

static const TextJustifyRow textJustifyVocabulary[] = {
    { RoboCssTextJustifyAuto,           "auto",            1, 0 },
    { RoboCssTextJustifyInterWord,      "inter-word",      1, 0 },
    { RoboCssTextJustifyInterCharacter, "inter-character", 1, 1 },
    { RoboCssTextJustifyNone,           "none",            0, 0 }
};

#define TEXT_JUSTIFY_COUNT \
    (sizeof(textJustifyVocabulary) / sizeof(textJustifyVocabulary[0]))

static const TextJustifyRow *TextJustifyRowOf(CssTextJustify justify)
{
    size_t index;

    for (index = 0u; index < TEXT_JUSTIFY_COUNT; index++) {
        if (textJustifyVocabulary[index].value == justify) {
            return &textJustifyVocabulary[index];
        }
    }
    return NULL;
}

int CssTextJustifyIsKnown(CssTextJustify justify)
{
    return TextJustifyRowOf(justify) != NULL;
}

const char *CssTextJustifyName(CssTextJustify justify)
{
    const TextJustifyRow *row = TextJustifyRowOf(justify);

    return row ? row->keyword : "";
}

size_t CssTextJustifyCount(void)
{
    return TEXT_JUSTIFY_COUNT;
}

CssTextJustify CssTextJustifyFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTextJustifyAuto;
    }
    for (index = 0u; index < TEXT_JUSTIFY_COUNT; index++) {
        if (TextKeywordEquals(keyword, textJustifyVocabulary[index].keyword)) {
            return textJustifyVocabulary[index].value;
        }
    }
    return RoboCssTextJustifyAuto;
}

CssTextJustify CssTextJustifyFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTextJustifyAuto;
    }
    return CssTextJustifyFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTextJustifySliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TEXT_JUSTIFY_COUNT; index++) {
        if (TextKeywordEquals(keyword, textJustifyVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTextJustifyJustifies(CssTextJustify justify)
{
    const TextJustifyRow *row = TextJustifyRowOf(justify);

    return row ? row->justifies : 0;
}

int CssTextJustifyExpandsCharacters(CssTextJustify justify)
{
    const TextJustifyRow *row = TextJustifyRowOf(justify);

    return row ? row->expandsCharacters : 0;
}

Result CssTextJustifyWrite(CssTextJustify justify,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag)
{
    const TextJustifyRow *row = TextJustifyRowOf(justify);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a text-justify value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* text-transform                                                      */
/* ------------------------------------------------------------------ */

typedef struct textTransformRow {
    CssTextTransform value;
    const char *keyword;
    int changesCase;
    int changesWidth;
    int changesSound;
} TextTransformRow;

static const TextTransformRow textTransformVocabulary[] = {
    { RoboCssTextTransformNone,         "none",           0, 0, 0 },
    { RoboCssTextTransformCapitalize,   "capitalize",     1, 0, 0 },
    { RoboCssTextTransformUppercase,    "uppercase",      1, 0, 0 },
    { RoboCssTextTransformLowercase,    "lowercase",      1, 0, 0 },
    { RoboCssTextTransformFullWidth,    "full-width",     0, 1, 0 },
    { RoboCssTextTransformFullSizeKana, "full-size-kana", 0, 0, 1 }
};

#define TEXT_TRANSFORM_COUNT \
    (sizeof(textTransformVocabulary) / sizeof(textTransformVocabulary[0]))

static const TextTransformRow *TextTransformRowOf(CssTextTransform transform)
{
    size_t index;

    for (index = 0u; index < TEXT_TRANSFORM_COUNT; index++) {
        if (textTransformVocabulary[index].value == transform) {
            return &textTransformVocabulary[index];
        }
    }
    return NULL;
}

int CssTextTransformIsKnown(CssTextTransform transform)
{
    return TextTransformRowOf(transform) != NULL;
}

const char *CssTextTransformName(CssTextTransform transform)
{
    const TextTransformRow *row = TextTransformRowOf(transform);

    return row ? row->keyword : "";
}

size_t CssTextTransformCount(void)
{
    return TEXT_TRANSFORM_COUNT;
}

CssTextTransform CssTextTransformFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTextTransformNone;
    }
    for (index = 0u; index < TEXT_TRANSFORM_COUNT; index++) {
        if (TextKeywordEquals(keyword, textTransformVocabulary[index].keyword)) {
            return textTransformVocabulary[index].value;
        }
    }
    return RoboCssTextTransformNone;
}

CssTextTransform CssTextTransformFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTextTransformNone;
    }
    return CssTextTransformFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTextTransformSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TEXT_TRANSFORM_COUNT; index++) {
        if (TextKeywordEquals(keyword, textTransformVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTextTransformChangesCase(CssTextTransform transform)
{
    const TextTransformRow *row = TextTransformRowOf(transform);

    return row ? row->changesCase : 0;
}

int CssTextTransformChangesWidth(CssTextTransform transform)
{
    const TextTransformRow *row = TextTransformRowOf(transform);

    return row ? row->changesWidth : 0;
}

int CssTextTransformChangesSound(CssTextTransform transform)
{
    const TextTransformRow *row = TextTransformRowOf(transform);

    return row ? row->changesSound : 0;
}

Result CssTextTransformWrite(CssTextTransform transform,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag)
{
    const TextTransformRow *row = TextTransformRowOf(transform);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a text-transform value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* white-space                                                         */
/* ------------------------------------------------------------------ */

typedef struct whiteSpaceRow {
    CssWhiteSpace value;
    const char *keyword;
    int collapsesSpaces;
    int preservesNewlines;
    int wraps;
    int breaksAfterPreservedSpace;
} WhiteSpaceRow;

static const WhiteSpaceRow whiteSpaceVocabulary[] = {
    { RoboCssWhiteSpaceNormal,      "normal",       1, 0, 1, 0 },
    { RoboCssWhiteSpacePre,         "pre",          0, 1, 0, 0 },
    { RoboCssWhiteSpaceNowrap,      "nowrap",       1, 0, 0, 0 },
    { RoboCssWhiteSpacePreWrap,     "pre-wrap",     0, 1, 1, 0 },
    { RoboCssWhiteSpacePreLine,     "pre-line",     1, 1, 1, 0 },
    { RoboCssWhiteSpaceBreakSpaces, "break-spaces", 0, 1, 1, 1 }
};

#define WHITE_SPACE_COUNT \
    (sizeof(whiteSpaceVocabulary) / sizeof(whiteSpaceVocabulary[0]))

static const WhiteSpaceRow *WhiteSpaceRowOf(CssWhiteSpace whiteSpace)
{
    size_t index;

    for (index = 0u; index < WHITE_SPACE_COUNT; index++) {
        if (whiteSpaceVocabulary[index].value == whiteSpace) {
            return &whiteSpaceVocabulary[index];
        }
    }
    return NULL;
}

int CssWhiteSpaceIsKnown(CssWhiteSpace whiteSpace)
{
    return WhiteSpaceRowOf(whiteSpace) != NULL;
}

const char *CssWhiteSpaceName(CssWhiteSpace whiteSpace)
{
    const WhiteSpaceRow *row = WhiteSpaceRowOf(whiteSpace);

    return row ? row->keyword : "";
}

size_t CssWhiteSpaceCount(void)
{
    return WHITE_SPACE_COUNT;
}

CssWhiteSpace CssWhiteSpaceFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssWhiteSpaceNormal;
    }
    for (index = 0u; index < WHITE_SPACE_COUNT; index++) {
        if (TextKeywordEquals(keyword, whiteSpaceVocabulary[index].keyword)) {
            return whiteSpaceVocabulary[index].value;
        }
    }
    return RoboCssWhiteSpaceNormal;
}

CssWhiteSpace CssWhiteSpaceFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssWhiteSpaceNormal;
    }
    return CssWhiteSpaceFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssWhiteSpaceSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < WHITE_SPACE_COUNT; index++) {
        if (TextKeywordEquals(keyword, whiteSpaceVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssWhiteSpaceCollapsesSpaces(CssWhiteSpace whiteSpace)
{
    const WhiteSpaceRow *row = WhiteSpaceRowOf(whiteSpace);

    /* An unknown value answers 1: the initial value collapses, and a value this
     * build cannot read must not be read as "preserve every space". */
    return row ? row->collapsesSpaces : 1;
}

int CssWhiteSpacePreservesNewlines(CssWhiteSpace whiteSpace)
{
    const WhiteSpaceRow *row = WhiteSpaceRowOf(whiteSpace);

    return row ? row->preservesNewlines : 0;
}

int CssWhiteSpaceWraps(CssWhiteSpace whiteSpace)
{
    const WhiteSpaceRow *row = WhiteSpaceRowOf(whiteSpace);

    /* An unknown value answers 1, for the reason above: wrapping is the initial
     * value's behaviour and the one that cannot overflow a box. */
    return row ? row->wraps : 1;
}

int CssWhiteSpaceBreaksAfterPreservedSpace(CssWhiteSpace whiteSpace)
{
    const WhiteSpaceRow *row = WhiteSpaceRowOf(whiteSpace);

    return row ? row->breaksAfterPreservedSpace : 0;
}

Result CssWhiteSpaceWrite(CssWhiteSpace whiteSpace,
                          char *buffer,
                          size_t capacity,
                          size_t *outLength,
                          CssDiag *diag)
{
    const WhiteSpaceRow *row = WhiteSpaceRowOf(whiteSpace);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a white-space value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* word-break                                                          */
/* ------------------------------------------------------------------ */

typedef struct wordBreakRow {
    CssWordBreak value;
    const char *keyword;
    int breaksWithinWords;
    int forbidsScriptBreaks;
} WordBreakRow;

static const WordBreakRow wordBreakVocabulary[] = {
    { RoboCssWordBreakNormal,    "normal",     0, 0 },
    { RoboCssWordBreakBreakAll,  "break-all",  1, 0 },
    { RoboCssWordBreakKeepAll,   "keep-all",   0, 1 },
    { RoboCssWordBreakBreakWord, "break-word", 1, 0 }
};

#define WORD_BREAK_COUNT \
    (sizeof(wordBreakVocabulary) / sizeof(wordBreakVocabulary[0]))

static const WordBreakRow *WordBreakRowOf(CssWordBreak wordBreak)
{
    size_t index;

    for (index = 0u; index < WORD_BREAK_COUNT; index++) {
        if (wordBreakVocabulary[index].value == wordBreak) {
            return &wordBreakVocabulary[index];
        }
    }
    return NULL;
}

int CssWordBreakIsKnown(CssWordBreak wordBreak)
{
    return WordBreakRowOf(wordBreak) != NULL;
}

const char *CssWordBreakName(CssWordBreak wordBreak)
{
    const WordBreakRow *row = WordBreakRowOf(wordBreak);

    return row ? row->keyword : "";
}

size_t CssWordBreakCount(void)
{
    return WORD_BREAK_COUNT;
}

CssWordBreak CssWordBreakFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssWordBreakNormal;
    }
    for (index = 0u; index < WORD_BREAK_COUNT; index++) {
        if (TextKeywordEquals(keyword, wordBreakVocabulary[index].keyword)) {
            return wordBreakVocabulary[index].value;
        }
    }
    return RoboCssWordBreakNormal;
}

CssWordBreak CssWordBreakFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssWordBreakNormal;
    }
    return CssWordBreakFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssWordBreakSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < WORD_BREAK_COUNT; index++) {
        if (TextKeywordEquals(keyword, wordBreakVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssWordBreakBreaksWithinWords(CssWordBreak wordBreak)
{
    const WordBreakRow *row = WordBreakRowOf(wordBreak);

    return row ? row->breaksWithinWords : 0;
}

int CssWordBreakForbidsScriptBreaks(CssWordBreak wordBreak)
{
    const WordBreakRow *row = WordBreakRowOf(wordBreak);

    return row ? row->forbidsScriptBreaks : 0;
}

Result CssWordBreakWrite(CssWordBreak wordBreak,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag)
{
    const WordBreakRow *row = WordBreakRowOf(wordBreak);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a word-break value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* overflow-wrap                                                       */
/* ------------------------------------------------------------------ */

typedef struct overflowWrapRow {
    CssOverflowWrap value;
    const char *keyword;
    int breaksWithinWords;
    int affectsIntrinsicSize;
} OverflowWrapRow;

static const OverflowWrapRow overflowWrapVocabulary[] = {
    { RoboCssOverflowWrapNormal,    "normal",     0, 0 },
    { RoboCssOverflowWrapBreakWord, "break-word", 1, 0 },
    { RoboCssOverflowWrapAnywhere,  "anywhere",   1, 1 }
};

#define OVERFLOW_WRAP_COUNT \
    (sizeof(overflowWrapVocabulary) / sizeof(overflowWrapVocabulary[0]))

static const OverflowWrapRow *OverflowWrapRowOf(CssOverflowWrap overflowWrap)
{
    size_t index;

    for (index = 0u; index < OVERFLOW_WRAP_COUNT; index++) {
        if (overflowWrapVocabulary[index].value == overflowWrap) {
            return &overflowWrapVocabulary[index];
        }
    }
    return NULL;
}

int CssOverflowWrapIsKnown(CssOverflowWrap overflowWrap)
{
    return OverflowWrapRowOf(overflowWrap) != NULL;
}

const char *CssOverflowWrapName(CssOverflowWrap overflowWrap)
{
    const OverflowWrapRow *row = OverflowWrapRowOf(overflowWrap);

    return row ? row->keyword : "";
}

size_t CssOverflowWrapCount(void)
{
    return OVERFLOW_WRAP_COUNT;
}

CssOverflowWrap CssOverflowWrapFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssOverflowWrapNormal;
    }
    for (index = 0u; index < OVERFLOW_WRAP_COUNT; index++) {
        if (TextKeywordEquals(keyword, overflowWrapVocabulary[index].keyword)) {
            return overflowWrapVocabulary[index].value;
        }
    }
    return RoboCssOverflowWrapNormal;
}

CssOverflowWrap CssOverflowWrapFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssOverflowWrapNormal;
    }
    return CssOverflowWrapFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssOverflowWrapSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < OVERFLOW_WRAP_COUNT; index++) {
        if (TextKeywordEquals(keyword, overflowWrapVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssOverflowWrapBreaksWithinWords(CssOverflowWrap overflowWrap)
{
    const OverflowWrapRow *row = OverflowWrapRowOf(overflowWrap);

    return row ? row->breaksWithinWords : 0;
}

int CssOverflowWrapAffectsIntrinsicSize(CssOverflowWrap overflowWrap)
{
    const OverflowWrapRow *row = OverflowWrapRowOf(overflowWrap);

    return row ? row->affectsIntrinsicSize : 0;
}

Result CssOverflowWrapWrite(CssOverflowWrap overflowWrap,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            CssDiag *diag)
{
    const OverflowWrapRow *row = OverflowWrapRowOf(overflowWrap);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not an overflow-wrap value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* line-break                                                          */
/* ------------------------------------------------------------------ */

typedef struct lineBreakRow {
    CssLineBreak value;
    const char *keyword;
    int automatic;
    int strictness;
    int breaksAnywhere;
} LineBreakRow;

static const LineBreakRow lineBreakVocabulary[] = {
    { RoboCssLineBreakAuto,     "auto",     1, 0, 0 },
    { RoboCssLineBreakLoose,    "loose",    0, 1, 0 },
    { RoboCssLineBreakNormal,   "normal",   0, 1, 0 },
    { RoboCssLineBreakStrict,   "strict",   0, 1, 0 },
    { RoboCssLineBreakAnywhere, "anywhere", 0, 0, 1 }
};

#define LINE_BREAK_COUNT \
    (sizeof(lineBreakVocabulary) / sizeof(lineBreakVocabulary[0]))

static const LineBreakRow *LineBreakRowOf(CssLineBreak lineBreak)
{
    size_t index;

    for (index = 0u; index < LINE_BREAK_COUNT; index++) {
        if (lineBreakVocabulary[index].value == lineBreak) {
            return &lineBreakVocabulary[index];
        }
    }
    return NULL;
}

int CssLineBreakIsKnown(CssLineBreak lineBreak)
{
    return LineBreakRowOf(lineBreak) != NULL;
}

const char *CssLineBreakName(CssLineBreak lineBreak)
{
    const LineBreakRow *row = LineBreakRowOf(lineBreak);

    return row ? row->keyword : "";
}

size_t CssLineBreakCount(void)
{
    return LINE_BREAK_COUNT;
}

CssLineBreak CssLineBreakFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssLineBreakAuto;
    }
    for (index = 0u; index < LINE_BREAK_COUNT; index++) {
        if (TextKeywordEquals(keyword, lineBreakVocabulary[index].keyword)) {
            return lineBreakVocabulary[index].value;
        }
    }
    return RoboCssLineBreakAuto;
}

CssLineBreak CssLineBreakFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssLineBreakAuto;
    }
    return CssLineBreakFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssLineBreakSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < LINE_BREAK_COUNT; index++) {
        if (TextKeywordEquals(keyword, lineBreakVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssLineBreakIsAutomatic(CssLineBreak lineBreak)
{
    const LineBreakRow *row = LineBreakRowOf(lineBreak);

    return row ? row->automatic : 0;
}

int CssLineBreakIsStrictness(CssLineBreak lineBreak)
{
    const LineBreakRow *row = LineBreakRowOf(lineBreak);

    return row ? row->strictness : 0;
}

int CssLineBreakBreaksAnywhere(CssLineBreak lineBreak)
{
    const LineBreakRow *row = LineBreakRowOf(lineBreak);

    return row ? row->breaksAnywhere : 0;
}

Result CssLineBreakWrite(CssLineBreak lineBreak,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag)
{
    const LineBreakRow *row = LineBreakRowOf(lineBreak);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a line-break value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* hyphens                                                             */
/* ------------------------------------------------------------------ */

typedef struct hyphensRow {
    CssHyphens value;
    const char *keyword;
    int enablesHyphenation;
    int insertsHyphens;
} HyphensRow;

static const HyphensRow hyphensVocabulary[] = {
    { RoboCssHyphensNone,   "none",   0, 0 },
    { RoboCssHyphensManual, "manual", 1, 0 },
    { RoboCssHyphensAuto,   "auto",   1, 1 }
};

#define HYPHENS_COUNT \
    (sizeof(hyphensVocabulary) / sizeof(hyphensVocabulary[0]))

static const HyphensRow *HyphensRowOf(CssHyphens hyphens)
{
    size_t index;

    for (index = 0u; index < HYPHENS_COUNT; index++) {
        if (hyphensVocabulary[index].value == hyphens) {
            return &hyphensVocabulary[index];
        }
    }
    return NULL;
}

int CssHyphensIsKnown(CssHyphens hyphens)
{
    return HyphensRowOf(hyphens) != NULL;
}

const char *CssHyphensName(CssHyphens hyphens)
{
    const HyphensRow *row = HyphensRowOf(hyphens);

    return row ? row->keyword : "";
}

size_t CssHyphensCount(void)
{
    return HYPHENS_COUNT;
}

CssHyphens CssHyphensFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssHyphensNone;
    }
    for (index = 0u; index < HYPHENS_COUNT; index++) {
        if (TextKeywordEquals(keyword, hyphensVocabulary[index].keyword)) {
            return hyphensVocabulary[index].value;
        }
    }
    return RoboCssHyphensNone;
}

CssHyphens CssHyphensFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssHyphensNone;
    }
    return CssHyphensFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssHyphensSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < HYPHENS_COUNT; index++) {
        if (TextKeywordEquals(keyword, hyphensVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssHyphensEnablesHyphenation(CssHyphens hyphens)
{
    const HyphensRow *row = HyphensRowOf(hyphens);

    return row ? row->enablesHyphenation : 0;
}

int CssHyphensInsertsHyphens(CssHyphens hyphens)
{
    const HyphensRow *row = HyphensRowOf(hyphens);

    return row ? row->insertsHyphens : 0;
}

Result CssHyphensWrite(CssHyphens hyphens,
                       char *buffer,
                       size_t capacity,
                       size_t *outLength,
                       CssDiag *diag)
{
    const HyphensRow *row = HyphensRowOf(hyphens);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a hyphens value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* text-decoration-style                                               */
/* ------------------------------------------------------------------ */

typedef struct textDecorationStyleRow {
    CssTextDecorationStyle value;
    const char *keyword;
    int solid;
    int wavy;
    int segmented;
} TextDecorationStyleRow;

static const TextDecorationStyleRow textDecorationStyleVocabulary[] = {
    { RoboCssTextDecorationStyleSolid,  "solid",  1, 0, 0 },
    { RoboCssTextDecorationStyleDouble, "double", 0, 0, 0 },
    { RoboCssTextDecorationStyleDotted, "dotted", 0, 0, 1 },
    { RoboCssTextDecorationStyleDashed, "dashed", 0, 0, 1 },
    { RoboCssTextDecorationStyleWavy,   "wavy",   0, 1, 0 }
};

#define TEXT_DECORATION_STYLE_COUNT \
    (sizeof(textDecorationStyleVocabulary) / \
     sizeof(textDecorationStyleVocabulary[0]))

static const TextDecorationStyleRow *TextDecorationStyleRowOf(
    CssTextDecorationStyle style)
{
    size_t index;

    for (index = 0u; index < TEXT_DECORATION_STYLE_COUNT; index++) {
        if (textDecorationStyleVocabulary[index].value == style) {
            return &textDecorationStyleVocabulary[index];
        }
    }
    return NULL;
}

int CssTextDecorationStyleIsKnown(CssTextDecorationStyle style)
{
    return TextDecorationStyleRowOf(style) != NULL;
}

const char *CssTextDecorationStyleName(CssTextDecorationStyle style)
{
    const TextDecorationStyleRow *row = TextDecorationStyleRowOf(style);

    return row ? row->keyword : "";
}

size_t CssTextDecorationStyleCount(void)
{
    return TEXT_DECORATION_STYLE_COUNT;
}

CssTextDecorationStyle CssTextDecorationStyleFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTextDecorationStyleSolid;
    }
    for (index = 0u; index < TEXT_DECORATION_STYLE_COUNT; index++) {
        if (TextKeywordEquals(keyword,
                              textDecorationStyleVocabulary[index].keyword)) {
            return textDecorationStyleVocabulary[index].value;
        }
    }
    return RoboCssTextDecorationStyleSolid;
}

CssTextDecorationStyle CssTextDecorationStyleFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTextDecorationStyleSolid;
    }
    return CssTextDecorationStyleFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTextDecorationStyleSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TEXT_DECORATION_STYLE_COUNT; index++) {
        if (TextKeywordEquals(keyword,
                              textDecorationStyleVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTextDecorationStyleIsSolid(CssTextDecorationStyle style)
{
    const TextDecorationStyleRow *row = TextDecorationStyleRowOf(style);

    return row ? row->solid : 0;
}

int CssTextDecorationStyleIsWavy(CssTextDecorationStyle style)
{
    const TextDecorationStyleRow *row = TextDecorationStyleRowOf(style);

    return row ? row->wavy : 0;
}

int CssTextDecorationStyleIsSegmented(CssTextDecorationStyle style)
{
    const TextDecorationStyleRow *row = TextDecorationStyleRowOf(style);

    return row ? row->segmented : 0;
}

Result CssTextDecorationStyleWrite(CssTextDecorationStyle style,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength,
                                   CssDiag *diag)
{
    const TextDecorationStyleRow *row = TextDecorationStyleRowOf(style);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a text-decoration-style value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* text-decoration-line                                                */
/* ------------------------------------------------------------------ */

typedef struct textDecorationLineRow {
    CssTextDecorationLine value;
    const char *keyword;
    int drawsInk;
    int blink;
} TextDecorationLineRow;

static const TextDecorationLineRow textDecorationLineVocabulary[] = {
    { RoboCssTextDecorationLineNone,        "none",         0, 0 },
    { RoboCssTextDecorationLineUnderline,   "underline",    1, 0 },
    { RoboCssTextDecorationLineOverline,    "overline",     1, 0 },
    { RoboCssTextDecorationLineLineThrough, "line-through", 1, 0 },
    { RoboCssTextDecorationLineBlink,       "blink",        0, 1 }
};

#define TEXT_DECORATION_LINE_COUNT \
    (sizeof(textDecorationLineVocabulary) / \
     sizeof(textDecorationLineVocabulary[0]))

static const TextDecorationLineRow *TextDecorationLineRowOf(
    CssTextDecorationLine line)
{
    size_t index;

    for (index = 0u; index < TEXT_DECORATION_LINE_COUNT; index++) {
        if (textDecorationLineVocabulary[index].value == line) {
            return &textDecorationLineVocabulary[index];
        }
    }
    return NULL;
}

int CssTextDecorationLineIsKnown(CssTextDecorationLine line)
{
    return TextDecorationLineRowOf(line) != NULL;
}

const char *CssTextDecorationLineName(CssTextDecorationLine line)
{
    const TextDecorationLineRow *row = TextDecorationLineRowOf(line);

    return row ? row->keyword : "";
}

size_t CssTextDecorationLineCount(void)
{
    return TEXT_DECORATION_LINE_COUNT;
}

CssTextDecorationLine CssTextDecorationLineFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTextDecorationLineNone;
    }
    for (index = 0u; index < TEXT_DECORATION_LINE_COUNT; index++) {
        if (TextKeywordEquals(keyword,
                              textDecorationLineVocabulary[index].keyword)) {
            return textDecorationLineVocabulary[index].value;
        }
    }
    return RoboCssTextDecorationLineNone;
}

CssTextDecorationLine CssTextDecorationLineFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTextDecorationLineNone;
    }
    return CssTextDecorationLineFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTextDecorationLineSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TEXT_DECORATION_LINE_COUNT; index++) {
        if (TextKeywordEquals(keyword,
                              textDecorationLineVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTextDecorationLineDrawsInk(CssTextDecorationLine line)
{
    const TextDecorationLineRow *row = TextDecorationLineRowOf(line);

    return row ? row->drawsInk : 0;
}

int CssTextDecorationLineIsBlink(CssTextDecorationLine line)
{
    const TextDecorationLineRow *row = TextDecorationLineRowOf(line);

    return row ? row->blink : 0;
}

Result CssTextDecorationLineWrite(CssTextDecorationLine line,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag)
{
    const TextDecorationLineRow *row = TextDecorationLineRowOf(line);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a text-decoration-line value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* text-overflow                                                       */
/* ------------------------------------------------------------------ */

typedef struct textOverflowRow {
    CssTextOverflow value;
    const char *keyword;
    int addsMarker;
} TextOverflowRow;

static const TextOverflowRow textOverflowVocabulary[] = {
    { RoboCssTextOverflowClip,     "clip",     0 },
    { RoboCssTextOverflowEllipsis, "ellipsis", 1 }
};

#define TEXT_OVERFLOW_COUNT \
    (sizeof(textOverflowVocabulary) / sizeof(textOverflowVocabulary[0]))

static const TextOverflowRow *TextOverflowRowOf(CssTextOverflow overflow)
{
    size_t index;

    for (index = 0u; index < TEXT_OVERFLOW_COUNT; index++) {
        if (textOverflowVocabulary[index].value == overflow) {
            return &textOverflowVocabulary[index];
        }
    }
    return NULL;
}

int CssTextOverflowIsKnown(CssTextOverflow overflow)
{
    return TextOverflowRowOf(overflow) != NULL;
}

const char *CssTextOverflowName(CssTextOverflow overflow)
{
    const TextOverflowRow *row = TextOverflowRowOf(overflow);

    return row ? row->keyword : "";
}

size_t CssTextOverflowCount(void)
{
    return TEXT_OVERFLOW_COUNT;
}

CssTextOverflow CssTextOverflowFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTextOverflowClip;
    }
    for (index = 0u; index < TEXT_OVERFLOW_COUNT; index++) {
        if (TextKeywordEquals(keyword, textOverflowVocabulary[index].keyword)) {
            return textOverflowVocabulary[index].value;
        }
    }
    return RoboCssTextOverflowClip;
}

CssTextOverflow CssTextOverflowFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTextOverflowClip;
    }
    return CssTextOverflowFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTextOverflowSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TEXT_OVERFLOW_COUNT; index++) {
        if (TextKeywordEquals(keyword, textOverflowVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTextOverflowAddsMarker(CssTextOverflow overflow)
{
    const TextOverflowRow *row = TextOverflowRowOf(overflow);

    return row ? row->addsMarker : 0;
}

Result CssTextOverflowWrite(CssTextOverflow overflow,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            CssDiag *diag)
{
    const TextOverflowRow *row = TextOverflowRowOf(overflow);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a text-overflow value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* text-decoration-thickness                                           */
/* ------------------------------------------------------------------ */

typedef struct textDecorationThicknessRow {
    CssTextDecorationThickness value;
    const char *keyword;
    int automatic;
    int usesFontMetric;
} TextDecorationThicknessRow;

static const TextDecorationThicknessRow textDecorationThicknessVocabulary[] = {
    { RoboCssTextDecorationThicknessAuto,     "auto",      1, 0 },
    { RoboCssTextDecorationThicknessFromFont, "from-font", 0, 1 }
};

#define TEXT_DECORATION_THICKNESS_COUNT \
    (sizeof(textDecorationThicknessVocabulary) / \
     sizeof(textDecorationThicknessVocabulary[0]))

static const TextDecorationThicknessRow *TextDecorationThicknessRowOf(
    CssTextDecorationThickness thickness)
{
    size_t index;

    for (index = 0u; index < TEXT_DECORATION_THICKNESS_COUNT; index++) {
        if (textDecorationThicknessVocabulary[index].value == thickness) {
            return &textDecorationThicknessVocabulary[index];
        }
    }
    return NULL;
}

int CssTextDecorationThicknessIsKnown(CssTextDecorationThickness thickness)
{
    return TextDecorationThicknessRowOf(thickness) != NULL;
}

const char *CssTextDecorationThicknessName(CssTextDecorationThickness thickness)
{
    const TextDecorationThicknessRow *row =
        TextDecorationThicknessRowOf(thickness);

    return row ? row->keyword : "";
}

size_t CssTextDecorationThicknessCount(void)
{
    return TEXT_DECORATION_THICKNESS_COUNT;
}

CssTextDecorationThickness CssTextDecorationThicknessFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTextDecorationThicknessAuto;
    }
    for (index = 0u; index < TEXT_DECORATION_THICKNESS_COUNT; index++) {
        if (TextKeywordEquals(
                keyword, textDecorationThicknessVocabulary[index].keyword)) {
            return textDecorationThicknessVocabulary[index].value;
        }
    }
    return RoboCssTextDecorationThicknessAuto;
}

CssTextDecorationThickness CssTextDecorationThicknessFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTextDecorationThicknessAuto;
    }
    return CssTextDecorationThicknessFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTextDecorationThicknessSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TEXT_DECORATION_THICKNESS_COUNT; index++) {
        if (TextKeywordEquals(
                keyword, textDecorationThicknessVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTextDecorationThicknessIsAutomatic(CssTextDecorationThickness thickness)
{
    const TextDecorationThicknessRow *row =
        TextDecorationThicknessRowOf(thickness);

    return row ? row->automatic : 0;
}

int CssTextDecorationThicknessUsesFontMetric(
    CssTextDecorationThickness thickness)
{
    const TextDecorationThicknessRow *row =
        TextDecorationThicknessRowOf(thickness);

    return row ? row->usesFontMetric : 0;
}

Result CssTextDecorationThicknessWrite(CssTextDecorationThickness thickness,
                                       char *buffer,
                                       size_t capacity,
                                       size_t *outLength,
                                       CssDiag *diag)
{
    const TextDecorationThicknessRow *row =
        TextDecorationThicknessRowOf(thickness);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a text-decoration-thickness value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* text-underline-position                                             */
/* ------------------------------------------------------------------ */

typedef struct textUnderlinePositionRow {
    CssTextUnderlinePosition value;
    const char *keyword;
    int automatic;
    int clearsDescenders;
    int physicalSide;
} TextUnderlinePositionRow;

static const TextUnderlinePositionRow textUnderlinePositionVocabulary[] = {
    { RoboCssTextUnderlinePositionAuto,  "auto",  1, 0, 0 },
    { RoboCssTextUnderlinePositionUnder, "under", 0, 1, 0 },
    { RoboCssTextUnderlinePositionLeft,  "left",  0, 0, 1 },
    { RoboCssTextUnderlinePositionRight, "right", 0, 0, 1 }
};

#define TEXT_UNDERLINE_POSITION_COUNT \
    (sizeof(textUnderlinePositionVocabulary) / \
     sizeof(textUnderlinePositionVocabulary[0]))

static const TextUnderlinePositionRow *TextUnderlinePositionRowOf(
    CssTextUnderlinePosition position)
{
    size_t index;

    for (index = 0u; index < TEXT_UNDERLINE_POSITION_COUNT; index++) {
        if (textUnderlinePositionVocabulary[index].value == position) {
            return &textUnderlinePositionVocabulary[index];
        }
    }
    return NULL;
}

int CssTextUnderlinePositionIsKnown(CssTextUnderlinePosition position)
{
    return TextUnderlinePositionRowOf(position) != NULL;
}

const char *CssTextUnderlinePositionName(CssTextUnderlinePosition position)
{
    const TextUnderlinePositionRow *row = TextUnderlinePositionRowOf(position);

    return row ? row->keyword : "";
}

size_t CssTextUnderlinePositionCount(void)
{
    return TEXT_UNDERLINE_POSITION_COUNT;
}

CssTextUnderlinePosition CssTextUnderlinePositionFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTextUnderlinePositionAuto;
    }
    for (index = 0u; index < TEXT_UNDERLINE_POSITION_COUNT; index++) {
        if (TextKeywordEquals(
                keyword, textUnderlinePositionVocabulary[index].keyword)) {
            return textUnderlinePositionVocabulary[index].value;
        }
    }
    return RoboCssTextUnderlinePositionAuto;
}

CssTextUnderlinePosition CssTextUnderlinePositionFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTextUnderlinePositionAuto;
    }
    return CssTextUnderlinePositionFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTextUnderlinePositionSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TEXT_UNDERLINE_POSITION_COUNT; index++) {
        if (TextKeywordEquals(
                keyword, textUnderlinePositionVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTextUnderlinePositionIsAutomatic(CssTextUnderlinePosition position)
{
    const TextUnderlinePositionRow *row = TextUnderlinePositionRowOf(position);

    return row ? row->automatic : 0;
}

int CssTextUnderlinePositionClearsDescenders(CssTextUnderlinePosition position)
{
    const TextUnderlinePositionRow *row = TextUnderlinePositionRowOf(position);

    return row ? row->clearsDescenders : 0;
}

int CssTextUnderlinePositionIsPhysicalSide(CssTextUnderlinePosition position)
{
    const TextUnderlinePositionRow *row = TextUnderlinePositionRowOf(position);

    return row ? row->physicalSide : 0;
}

Result CssTextUnderlinePositionWrite(CssTextUnderlinePosition position,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     CssDiag *diag)
{
    const TextUnderlinePositionRow *row = TextUnderlinePositionRowOf(position);

    if (!row) {
        return TextWriteUnknown(buffer, capacity, outLength, diag,
                                "not a text-underline-position value");
    }
    return TextWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}
