/*
 * RoboCSS transform vocabulary (Phase 18.7) — see RoboCSSTransform.h.
 *
 * Three tables, one per vocabulary, and every accessor below reads one of them.
 * The transform-function table carries two classification columns — 3D and matrix
 * — because a consumer asks them for different reasons: 3D says whether the list
 * needs the 3D machinery at all, matrix says whether one member already fixes the
 * whole transform. Nothing in this file composes, multiplies or draws anything.
 */

#include "RoboCSS/RoboCSSTransform.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/** The one keyword-write body every vocabulary below shares. */
static Result TransformWriteKeyword(const char *keyword,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength,
                                    CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainTransform, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainTransform, RoboInvalidArgument,
                           "not a transform value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainTransform, RoboNotSupported,
                           "the transform keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

/** Non-zero when @p keyword equals @p spelling, ASCII case-insensitively. */
static int TransformKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

/* ------------------------------------------------------------------ */
/* transform-style                                                     */
/* ------------------------------------------------------------------ */

typedef struct transformStyleRow {
    CssTransformStyle value;
    const char *keyword;
    int creates3DContext;
} TransformStyleRow;

static const TransformStyleRow transformStyleVocabulary[] = {
    { RoboCssTransformStyleFlat,       "flat",        0 },
    { RoboCssTransformStylePreserve3d, "preserve-3d", 1 }
};

#define TRANSFORM_STYLE_COUNT \
    (sizeof(transformStyleVocabulary) / sizeof(transformStyleVocabulary[0]))

static const TransformStyleRow *TransformStyleRowOf(CssTransformStyle style)
{
    size_t index;

    for (index = 0u; index < TRANSFORM_STYLE_COUNT; index++) {
        if (transformStyleVocabulary[index].value == style) {
            return &transformStyleVocabulary[index];
        }
    }
    return NULL;
}

int CssTransformStyleIsKnown(CssTransformStyle style)
{
    return TransformStyleRowOf(style) != NULL;
}

const char *CssTransformStyleName(CssTransformStyle style)
{
    const TransformStyleRow *row = TransformStyleRowOf(style);

    return row ? row->keyword : "";
}

size_t CssTransformStyleCount(void)
{
    return TRANSFORM_STYLE_COUNT;
}

CssTransformStyle CssTransformStyleFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTransformStyleFlat;
    }
    for (index = 0u; index < TRANSFORM_STYLE_COUNT; index++) {
        if (TransformKeywordEquals(keyword,
                                   transformStyleVocabulary[index].keyword)) {
            return transformStyleVocabulary[index].value;
        }
    }
    return RoboCssTransformStyleFlat;
}

CssTransformStyle CssTransformStyleFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTransformStyleFlat;
    }
    return CssTransformStyleFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTransformStyleSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TRANSFORM_STYLE_COUNT; index++) {
        if (TransformKeywordEquals(keyword,
                                   transformStyleVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTransformStyleCreates3DContext(CssTransformStyle style)
{
    const TransformStyleRow *row = TransformStyleRowOf(style);

    return row ? row->creates3DContext : 0;
}

Result CssTransformStyleWrite(CssTransformStyle style,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              CssDiag *diag)
{
    const TransformStyleRow *row = TransformStyleRowOf(style);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainTransform, RoboInvalidArgument,
                           "not a transform-style value");
    }
    return TransformWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* backface-visibility                                                 */
/* ------------------------------------------------------------------ */

typedef struct backfaceVisibilityRow {
    CssBackfaceVisibility value;
    const char *keyword;
    int showsBackface;
} BackfaceVisibilityRow;

static const BackfaceVisibilityRow backfaceVisibilityVocabulary[] = {
    { RoboCssBackfaceVisibilityVisible, "visible", 1 },
    { RoboCssBackfaceVisibilityHidden,  "hidden",  0 }
};

#define BACKFACE_VISIBILITY_COUNT \
    (sizeof(backfaceVisibilityVocabulary) / \
     sizeof(backfaceVisibilityVocabulary[0]))

static const BackfaceVisibilityRow *BackfaceVisibilityRowOf(
    CssBackfaceVisibility visibility)
{
    size_t index;

    for (index = 0u; index < BACKFACE_VISIBILITY_COUNT; index++) {
        if (backfaceVisibilityVocabulary[index].value == visibility) {
            return &backfaceVisibilityVocabulary[index];
        }
    }
    return NULL;
}

int CssBackfaceVisibilityIsKnown(CssBackfaceVisibility visibility)
{
    return BackfaceVisibilityRowOf(visibility) != NULL;
}

const char *CssBackfaceVisibilityName(CssBackfaceVisibility visibility)
{
    const BackfaceVisibilityRow *row = BackfaceVisibilityRowOf(visibility);

    return row ? row->keyword : "";
}

size_t CssBackfaceVisibilityCount(void)
{
    return BACKFACE_VISIBILITY_COUNT;
}

CssBackfaceVisibility CssBackfaceVisibilityFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssBackfaceVisibilityVisible;
    }
    for (index = 0u; index < BACKFACE_VISIBILITY_COUNT; index++) {
        if (TransformKeywordEquals(keyword,
                                   backfaceVisibilityVocabulary[index].keyword)) {
            return backfaceVisibilityVocabulary[index].value;
        }
    }
    return RoboCssBackfaceVisibilityVisible;
}

CssBackfaceVisibility CssBackfaceVisibilityFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssBackfaceVisibilityVisible;
    }
    return CssBackfaceVisibilityFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssBackfaceVisibilitySliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < BACKFACE_VISIBILITY_COUNT; index++) {
        if (TransformKeywordEquals(keyword,
                                   backfaceVisibilityVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssBackfaceVisibilityShowsBackface(CssBackfaceVisibility visibility)
{
    const BackfaceVisibilityRow *row = BackfaceVisibilityRowOf(visibility);

    return row ? row->showsBackface : 0;
}

Result CssBackfaceVisibilityWrite(CssBackfaceVisibility visibility,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag)
{
    const BackfaceVisibilityRow *row = BackfaceVisibilityRowOf(visibility);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainTransform, RoboInvalidArgument,
                           "not a backface-visibility value");
    }
    return TransformWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* transform functions                                                 */
/* ------------------------------------------------------------------ */

typedef struct transformFunctionRow {
    CssTransformFunction value;
    const char *keyword;
    int is3D;
    int isMatrix;
} TransformFunctionRow;

static const TransformFunctionRow transformFunctionVocabulary[] = {
    { RoboCssTransformFunctionMatrix,      "matrix",      0, 1 },
    { RoboCssTransformFunctionMatrix3d,    "matrix3d",    1, 1 },
    { RoboCssTransformFunctionTranslate,   "translate",   0, 0 },
    { RoboCssTransformFunctionTranslate3d, "translate3d", 1, 0 },
    { RoboCssTransformFunctionTranslateX,  "translatex",  0, 0 },
    { RoboCssTransformFunctionTranslateY,  "translatey",  0, 0 },
    { RoboCssTransformFunctionTranslateZ,  "translatez",  1, 0 },
    { RoboCssTransformFunctionScale,       "scale",       0, 0 },
    { RoboCssTransformFunctionScale3d,     "scale3d",     1, 0 },
    { RoboCssTransformFunctionScaleX,      "scalex",      0, 0 },
    { RoboCssTransformFunctionScaleY,      "scaley",      0, 0 },
    { RoboCssTransformFunctionScaleZ,      "scalez",      1, 0 },
    { RoboCssTransformFunctionRotate,      "rotate",      0, 0 },
    { RoboCssTransformFunctionRotate3d,    "rotate3d",    1, 0 },
    { RoboCssTransformFunctionRotateX,     "rotatex",     1, 0 },
    { RoboCssTransformFunctionRotateY,     "rotatey",     1, 0 },
    { RoboCssTransformFunctionRotateZ,     "rotatez",     1, 0 },
    { RoboCssTransformFunctionSkew,        "skew",        0, 0 },
    { RoboCssTransformFunctionSkewX,       "skewx",       0, 0 },
    { RoboCssTransformFunctionSkewY,       "skewy",       0, 0 },
    { RoboCssTransformFunctionPerspective, "perspective", 1, 0 }
};

#define TRANSFORM_FUNCTION_COUNT \
    (sizeof(transformFunctionVocabulary) / \
     sizeof(transformFunctionVocabulary[0]))

static const TransformFunctionRow *TransformFunctionRowOf(
    CssTransformFunction function)
{
    size_t index;

    for (index = 0u; index < TRANSFORM_FUNCTION_COUNT; index++) {
        if (transformFunctionVocabulary[index].value == function) {
            return &transformFunctionVocabulary[index];
        }
    }
    return NULL;
}

int CssTransformFunctionIsKnown(CssTransformFunction function)
{
    return TransformFunctionRowOf(function) != NULL;
}

const char *CssTransformFunctionName(CssTransformFunction function)
{
    const TransformFunctionRow *row = TransformFunctionRowOf(function);

    return row ? row->keyword : "";
}

size_t CssTransformFunctionCount(void)
{
    return TRANSFORM_FUNCTION_COUNT;
}

CssTransformFunction CssTransformFunctionFromSlice(CssSlice name)
{
    size_t index;

    if (CssSliceIsAbsent(name) || name.length == 0u) {
        return RoboCssTransformFunctionMatrix;
    }
    for (index = 0u; index < TRANSFORM_FUNCTION_COUNT; index++) {
        if (TransformKeywordEquals(name,
                                   transformFunctionVocabulary[index].keyword)) {
            return transformFunctionVocabulary[index].value;
        }
    }
    return RoboCssTransformFunctionMatrix;
}

CssTransformFunction CssTransformFunctionFromText(const char *name)
{
    if (!name) {
        return RoboCssTransformFunctionMatrix;
    }
    return CssTransformFunctionFromSlice(
        CssSliceFromBytes((const uint8_t *)name, strlen(name)));
}

int CssTransformFunctionSliceIsKnown(CssSlice name)
{
    size_t index;

    if (CssSliceIsAbsent(name) || name.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TRANSFORM_FUNCTION_COUNT; index++) {
        if (TransformKeywordEquals(name,
                                   transformFunctionVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTransformFunctionIs3D(CssTransformFunction function)
{
    const TransformFunctionRow *row = TransformFunctionRowOf(function);

    return row ? row->is3D : 0;
}

int CssTransformFunctionIsMatrix(CssTransformFunction function)
{
    const TransformFunctionRow *row = TransformFunctionRowOf(function);

    return row ? row->isMatrix : 0;
}

Result CssTransformFunctionWrite(CssTransformFunction function,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength,
                                 CssDiag *diag)
{
    const TransformFunctionRow *row = TransformFunctionRowOf(function);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainTransform, RoboInvalidArgument,
                           "not a transform function");
    }
    return TransformWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}
