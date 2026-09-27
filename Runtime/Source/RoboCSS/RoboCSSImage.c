/*
 * RoboCSS image vocabulary (Phase 18.16) — see RoboCSSImage.h.
 *
 * Four tables, one per closed set, and every accessor below reads one of them. The
 * rows carry the keyword, the enumerator and the classifications a consumer
 * dispatches on; nothing in this file opens a file, decodes a header, samples a
 * colour stop, interpolates a gradient, scales or filters a pixel, or measures a box.
 *
 * The keywords are lowercase because that is how CSS writes them and how the writers
 * below must print them; matching is ASCII case-insensitive against these spellings
 * and against nothing else, so `scale-down` matches and `scale_down` does not.
 *
 * The habit worth naming, because it is what the classifications are for, is that a
 * classification is never the negation of another. `image-rendering`'s `auto` is in
 * neither the smoothing group nor the pixelated group, so a consumer that read
 * "not smoothing" as "pixelated" would pin a choice `auto` deliberately leaves open;
 * `object-fit`'s `none` preserves the aspect ratio without scaling, so
 * "preserves the ratio" is not "scales"; and a non-gradient image is on no axis at
 * all, so the three axis predicates are false together rather than complementing one
 * another.
 *
 * The second habit is what an unknown enumerator answers. For the two sets whose
 * initial value is the *safe* direction — `image-rendering`'s `auto`, whose choice
 * belongs to the user agent, and `image-orientation`'s `from-image`, which honours the
 * file's own orientation — an unknown value answers the initial's behaviour, because
 * discarding a rotated photo or pinning a rendering algorithm is exactly what a
 * build that cannot read a value must not do. For the rest an unknown value claims
 * nothing rather than guessing.
 */

#include "RoboCSS/RoboCSSImage.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/** The one keyword-write body every vocabulary below shares. */
static Result ImageWriteKeyword(const char *keyword,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainImage, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainImage, RoboInvalidArgument,
                           "not an image value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainImage, RoboNotSupported,
                           "the image keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

/** Non-zero when @p keyword equals @p spelling, ASCII case-insensitively. */
static int ImageKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

/** The one "not a defined value" refusal every writer below shares. */
static Result ImageWriteUnknown(char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                CssDiag *diag,
                                const char *what)
{
    if (buffer && outLength && capacity > 0u) {
        buffer[0] = '\0';
    }
    return RoboCssDiag(diag, RoboCssDomainImage, RoboInvalidArgument, what);
}

/* ------------------------------------------------------------------ */
/* image-rendering                                                     */
/* ------------------------------------------------------------------ */

typedef struct imageRenderingRow {
    CssImageRendering value;
    const char *keyword;
    int automatic;
    int smoothing;
    int pixelated;
    int showsPixelGrid;
} ImageRenderingRow;

static const ImageRenderingRow imageRenderingVocabulary[] = {
    { RoboCssImageRenderingAuto,        "auto",         1, 0, 0, 0 },
    { RoboCssImageRenderingSmooth,      "smooth",       0, 1, 0, 0 },
    { RoboCssImageRenderingHighQuality, "high-quality", 0, 1, 0, 0 },
    { RoboCssImageRenderingCrispEdges,  "crisp-edges",  0, 0, 1, 0 },
    { RoboCssImageRenderingPixelated,   "pixelated",    0, 0, 1, 1 }
};

#define IMAGE_RENDERING_COUNT \
    (sizeof(imageRenderingVocabulary) / sizeof(imageRenderingVocabulary[0]))

static const ImageRenderingRow *ImageRenderingRowOf(CssImageRendering rendering)
{
    size_t index;

    for (index = 0u; index < IMAGE_RENDERING_COUNT; index++) {
        if (imageRenderingVocabulary[index].value == rendering) {
            return &imageRenderingVocabulary[index];
        }
    }
    return NULL;
}

int CssImageRenderingIsKnown(CssImageRendering rendering)
{
    return ImageRenderingRowOf(rendering) != NULL;
}

const char *CssImageRenderingName(CssImageRendering rendering)
{
    const ImageRenderingRow *row = ImageRenderingRowOf(rendering);

    return row ? row->keyword : "";
}

size_t CssImageRenderingCount(void)
{
    return IMAGE_RENDERING_COUNT;
}

CssImageRendering CssImageRenderingFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssImageRenderingAuto;
    }
    for (index = 0u; index < IMAGE_RENDERING_COUNT; index++) {
        if (ImageKeywordEquals(keyword, imageRenderingVocabulary[index].keyword)) {
            return imageRenderingVocabulary[index].value;
        }
    }
    return RoboCssImageRenderingAuto;
}

CssImageRendering CssImageRenderingFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssImageRenderingAuto;
    }
    return CssImageRenderingFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssImageRenderingSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < IMAGE_RENDERING_COUNT; index++) {
        if (ImageKeywordEquals(keyword, imageRenderingVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssImageRenderingIsAutomatic(CssImageRendering rendering)
{
    const ImageRenderingRow *row = ImageRenderingRowOf(rendering);

    /* An unknown value answers the initial's behaviour: the user agent decides. */
    return row ? row->automatic : 1;
}

int CssImageRenderingIsSmoothing(CssImageRendering rendering)
{
    const ImageRenderingRow *row = ImageRenderingRowOf(rendering);

    return row ? row->smoothing : 0;
}

int CssImageRenderingIsPixelated(CssImageRendering rendering)
{
    const ImageRenderingRow *row = ImageRenderingRowOf(rendering);

    return row ? row->pixelated : 0;
}

int CssImageRenderingShowsPixelGrid(CssImageRendering rendering)
{
    const ImageRenderingRow *row = ImageRenderingRowOf(rendering);

    return row ? row->showsPixelGrid : 0;
}

Result CssImageRenderingWrite(CssImageRendering rendering,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              CssDiag *diag)
{
    const ImageRenderingRow *row = ImageRenderingRowOf(rendering);

    if (!row) {
        return ImageWriteUnknown(buffer, capacity, outLength, diag,
                                 "not an image-rendering value");
    }
    return ImageWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* object-fit                                                          */
/* ------------------------------------------------------------------ */

typedef struct objectFitRow {
    CssObjectFit value;
    const char *keyword;
    int preservesRatio;
    int scales;
    int clips;
    int scalesDownOnly;
} ObjectFitRow;

static const ObjectFitRow objectFitVocabulary[] = {
    { RoboCssObjectFitFill,      "fill",       0, 1, 0, 0 },
    { RoboCssObjectFitContain,   "contain",    1, 1, 0, 0 },
    { RoboCssObjectFitCover,     "cover",      1, 1, 1, 0 },
    { RoboCssObjectFitNone,      "none",       1, 0, 1, 0 },
    { RoboCssObjectFitScaleDown, "scale-down", 1, 1, 0, 1 }
};

#define OBJECT_FIT_COUNT \
    (sizeof(objectFitVocabulary) / sizeof(objectFitVocabulary[0]))

static const ObjectFitRow *ObjectFitRowOf(CssObjectFit fit)
{
    size_t index;

    for (index = 0u; index < OBJECT_FIT_COUNT; index++) {
        if (objectFitVocabulary[index].value == fit) {
            return &objectFitVocabulary[index];
        }
    }
    return NULL;
}

int CssObjectFitIsKnown(CssObjectFit fit)
{
    return ObjectFitRowOf(fit) != NULL;
}

const char *CssObjectFitName(CssObjectFit fit)
{
    const ObjectFitRow *row = ObjectFitRowOf(fit);

    return row ? row->keyword : "";
}

size_t CssObjectFitCount(void)
{
    return OBJECT_FIT_COUNT;
}

CssObjectFit CssObjectFitFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssObjectFitFill;
    }
    for (index = 0u; index < OBJECT_FIT_COUNT; index++) {
        if (ImageKeywordEquals(keyword, objectFitVocabulary[index].keyword)) {
            return objectFitVocabulary[index].value;
        }
    }
    return RoboCssObjectFitFill;
}

CssObjectFit CssObjectFitFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssObjectFitFill;
    }
    return CssObjectFitFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssObjectFitSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < OBJECT_FIT_COUNT; index++) {
        if (ImageKeywordEquals(keyword, objectFitVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssObjectFitPreservesAspectRatio(CssObjectFit fit)
{
    const ObjectFitRow *row = ObjectFitRowOf(fit);

    return row ? row->preservesRatio : 0;
}

int CssObjectFitScales(CssObjectFit fit)
{
    const ObjectFitRow *row = ObjectFitRowOf(fit);

    return row ? row->scales : 1;
}

int CssObjectFitClips(CssObjectFit fit)
{
    const ObjectFitRow *row = ObjectFitRowOf(fit);

    return row ? row->clips : 0;
}

int CssObjectFitScalesDownOnly(CssObjectFit fit)
{
    const ObjectFitRow *row = ObjectFitRowOf(fit);

    return row ? row->scalesDownOnly : 0;
}

Result CssObjectFitWrite(CssObjectFit fit,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag)
{
    const ObjectFitRow *row = ObjectFitRowOf(fit);

    if (!row) {
        return ImageWriteUnknown(buffer, capacity, outLength, diag,
                                 "not an object-fit value");
    }
    return ImageWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* image-orientation                                                   */
/* ------------------------------------------------------------------ */

typedef struct imageOrientationRow {
    CssImageOrientation value;
    const char *keyword;
    int fromImage;
} ImageOrientationRow;

static const ImageOrientationRow imageOrientationVocabulary[] = {
    { RoboCssImageOrientationFromImage, "from-image", 1 },
    { RoboCssImageOrientationNone,      "none",       0 }
};

#define IMAGE_ORIENTATION_COUNT \
    (sizeof(imageOrientationVocabulary) / sizeof(imageOrientationVocabulary[0]))

static const ImageOrientationRow *ImageOrientationRowOf(
    CssImageOrientation orientation)
{
    size_t index;

    for (index = 0u; index < IMAGE_ORIENTATION_COUNT; index++) {
        if (imageOrientationVocabulary[index].value == orientation) {
            return &imageOrientationVocabulary[index];
        }
    }
    return NULL;
}

int CssImageOrientationIsKnown(CssImageOrientation orientation)
{
    return ImageOrientationRowOf(orientation) != NULL;
}

const char *CssImageOrientationName(CssImageOrientation orientation)
{
    const ImageOrientationRow *row = ImageOrientationRowOf(orientation);

    return row ? row->keyword : "";
}

size_t CssImageOrientationCount(void)
{
    return IMAGE_ORIENTATION_COUNT;
}

CssImageOrientation CssImageOrientationFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssImageOrientationFromImage;
    }
    for (index = 0u; index < IMAGE_ORIENTATION_COUNT; index++) {
        if (ImageKeywordEquals(keyword, imageOrientationVocabulary[index].keyword)) {
            return imageOrientationVocabulary[index].value;
        }
    }
    return RoboCssImageOrientationFromImage;
}

CssImageOrientation CssImageOrientationFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssImageOrientationFromImage;
    }
    return CssImageOrientationFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssImageOrientationSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < IMAGE_ORIENTATION_COUNT; index++) {
        if (ImageKeywordEquals(keyword, imageOrientationVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssImageOrientationIsFromImage(CssImageOrientation orientation)
{
    const ImageOrientationRow *row = ImageOrientationRowOf(orientation);

    /* An unknown value answers the initial's behaviour: honour the file. */
    return row ? row->fromImage : 1;
}

int CssImageOrientationIsNone(CssImageOrientation orientation)
{
    const ImageOrientationRow *row = ImageOrientationRowOf(orientation);

    return row ? !row->fromImage : 0;
}

Result CssImageOrientationWrite(CssImageOrientation orientation,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                CssDiag *diag)
{
    const ImageOrientationRow *row = ImageOrientationRowOf(orientation);

    if (!row) {
        return ImageWriteUnknown(buffer, capacity, outLength, diag,
                                 "not an image-orientation keyword");
    }
    return ImageWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* The image function vocabulary                                       */
/* ------------------------------------------------------------------ */

typedef struct imageFunctionRow {
    CssImageFunction value;
    const char *keyword;
    int gradient;
    int repeating;
    int linear;
    int radial;
    int conic;
    int intrinsicSize;
    int resource;
} ImageFunctionRow;

static const ImageFunctionRow imageFunctionVocabulary[] = {
    { RoboCssImageFunctionUrl,          "url",          0, 0, 0, 0, 0, 1, 1 },
    { RoboCssImageFunctionSrc,          "src",          0, 0, 0, 0, 0, 1, 1 },
    { RoboCssImageFunctionImage,        "image",        0, 0, 0, 0, 0, 1, 0 },
    { RoboCssImageFunctionImageSet,     "image-set",    0, 0, 0, 0, 0, 0, 0 },
    { RoboCssImageFunctionCrossFade,    "cross-fade",   0, 0, 0, 0, 0, 0, 0 },
    { RoboCssImageFunctionElement,      "element",      0, 0, 0, 0, 0, 1, 0 },
    { RoboCssImageFunctionPaint,        "paint",        0, 0, 0, 0, 0, 0, 0 },
    { RoboCssImageFunctionLinearGradient,
      "linear-gradient",           1, 0, 1, 0, 0, 0, 0 },
    { RoboCssImageFunctionRadialGradient,
      "radial-gradient",           1, 0, 0, 1, 0, 0, 0 },
    { RoboCssImageFunctionConicGradient,
      "conic-gradient",            1, 0, 0, 0, 1, 0, 0 },
    { RoboCssImageFunctionRepeatingLinearGradient,
      "repeating-linear-gradient", 1, 1, 1, 0, 0, 0, 0 },
    { RoboCssImageFunctionRepeatingRadialGradient,
      "repeating-radial-gradient", 1, 1, 0, 1, 0, 0, 0 },
    { RoboCssImageFunctionRepeatingConicGradient,
      "repeating-conic-gradient",  1, 1, 0, 0, 1, 0, 0 }
};

#define IMAGE_FUNCTION_COUNT \
    (sizeof(imageFunctionVocabulary) / sizeof(imageFunctionVocabulary[0]))

static const ImageFunctionRow *ImageFunctionRowOf(CssImageFunction function)
{
    size_t index;

    for (index = 0u; index < IMAGE_FUNCTION_COUNT; index++) {
        if (imageFunctionVocabulary[index].value == function) {
            return &imageFunctionVocabulary[index];
        }
    }
    return NULL;
}

int CssImageFunctionIsKnown(CssImageFunction function)
{
    return ImageFunctionRowOf(function) != NULL;
}

const char *CssImageFunctionName(CssImageFunction function)
{
    const ImageFunctionRow *row = ImageFunctionRowOf(function);

    return row ? row->keyword : "";
}

size_t CssImageFunctionCount(void)
{
    return IMAGE_FUNCTION_COUNT;
}

CssImageFunction CssImageFunctionFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssImageFunctionUrl;
    }
    for (index = 0u; index < IMAGE_FUNCTION_COUNT; index++) {
        if (ImageKeywordEquals(keyword, imageFunctionVocabulary[index].keyword)) {
            return imageFunctionVocabulary[index].value;
        }
    }
    return RoboCssImageFunctionUrl;
}

CssImageFunction CssImageFunctionFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssImageFunctionUrl;
    }
    return CssImageFunctionFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssImageFunctionSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < IMAGE_FUNCTION_COUNT; index++) {
        if (ImageKeywordEquals(keyword, imageFunctionVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssImageFunctionIsGradient(CssImageFunction function)
{
    const ImageFunctionRow *row = ImageFunctionRowOf(function);

    return row ? row->gradient : 0;
}

int CssImageFunctionIsRepeating(CssImageFunction function)
{
    const ImageFunctionRow *row = ImageFunctionRowOf(function);

    return row ? row->repeating : 0;
}

int CssImageFunctionIsLinear(CssImageFunction function)
{
    const ImageFunctionRow *row = ImageFunctionRowOf(function);

    return row ? row->linear : 0;
}

int CssImageFunctionIsRadial(CssImageFunction function)
{
    const ImageFunctionRow *row = ImageFunctionRowOf(function);

    return row ? row->radial : 0;
}

int CssImageFunctionIsConic(CssImageFunction function)
{
    const ImageFunctionRow *row = ImageFunctionRowOf(function);

    return row ? row->conic : 0;
}

int CssImageFunctionHasIntrinsicSize(CssImageFunction function)
{
    const ImageFunctionRow *row = ImageFunctionRowOf(function);

    return row ? row->intrinsicSize : 0;
}

int CssImageFunctionIsResourceReference(CssImageFunction function)
{
    const ImageFunctionRow *row = ImageFunctionRowOf(function);

    return row ? row->resource : 0;
}

Result CssImageFunctionWrite(CssImageFunction function,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag)
{
    const ImageFunctionRow *row = ImageFunctionRowOf(function);

    if (!row) {
        return ImageWriteUnknown(buffer, capacity, outLength, diag,
                                 "not an image function");
    }
    return ImageWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}
