/*
 * RoboCSS media queries (Phase 18.10) — see RoboCSSMedia.h.
 *
 * Three tables — media types, media features and their kinds — plus one matcher
 * bounded to the features a `CssMediaContext` can answer. A feature the context
 * cannot answer is UNKNOWN, never NO: the two are different sentences, and a
 * `(prefers-color-scheme: dark)` sheet evaluated against a context with no colour
 * scheme must not silently not-match.
 */

#include "RoboCSS/RoboCSSMedia.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

static Result MediaWriteKeyword(const char *keyword,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainMedia, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainMedia, RoboInvalidArgument,
                           "not a media value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainMedia, RoboNotSupported,
                           "the media keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

static int MediaKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

static int MediaSliceEquals(const char *text, CssSlice keyword)
{
    if (!text || CssSliceIsAbsent(keyword)) {
        return 0;
    }
    return MediaKeywordEquals(keyword, text);
}

/* ------------------------------------------------------------------ */
/* media types                                                         */
/* ------------------------------------------------------------------ */

typedef struct mediaTypeRow {
    CssMediaType value;
    const char *keyword;
} MediaTypeRow;

static const MediaTypeRow mediaTypeVocabulary[] = {
    { RoboCssMediaTypeAll,    "all"    },
    { RoboCssMediaTypeScreen, "screen" },
    { RoboCssMediaTypePrint,  "print"  }
};

#define MEDIA_TYPE_COUNT \
    (sizeof(mediaTypeVocabulary) / sizeof(mediaTypeVocabulary[0]))

static const MediaTypeRow *MediaTypeRowOf(CssMediaType type)
{
    size_t index;

    for (index = 0u; index < MEDIA_TYPE_COUNT; index++) {
        if (mediaTypeVocabulary[index].value == type) {
            return &mediaTypeVocabulary[index];
        }
    }
    return NULL;
}

int CssMediaTypeIsKnown(CssMediaType type)
{
    return MediaTypeRowOf(type) != NULL;
}

const char *CssMediaTypeName(CssMediaType type)
{
    const MediaTypeRow *row = MediaTypeRowOf(type);

    return row ? row->keyword : "";
}

size_t CssMediaTypeCount(void)
{
    return MEDIA_TYPE_COUNT;
}

CssMediaType CssMediaTypeFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssMediaTypeAll;
    }
    for (index = 0u; index < MEDIA_TYPE_COUNT; index++) {
        if (MediaKeywordEquals(keyword, mediaTypeVocabulary[index].keyword)) {
            return mediaTypeVocabulary[index].value;
        }
    }
    return RoboCssMediaTypeAll;
}

CssMediaType CssMediaTypeFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssMediaTypeAll;
    }
    return CssMediaTypeFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssMediaTypeSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < MEDIA_TYPE_COUNT; index++) {
        if (MediaKeywordEquals(keyword, mediaTypeVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

Result CssMediaTypeWrite(CssMediaType type,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag)
{
    const MediaTypeRow *row = MediaTypeRowOf(type);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainMedia, RoboInvalidArgument,
                           "not a media type");
    }
    return MediaWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* media features                                                      */
/* ------------------------------------------------------------------ */

typedef struct mediaFeatureRow {
    CssMediaFeature value;
    const char *keyword;
    CssMediaFeatureKind kind;
    int isRange;
} MediaFeatureRow;

static const MediaFeatureRow mediaFeatureVocabulary[] = {
    { RoboCssMediaFeatureWidth,               "width",                 RoboCssMediaFeatureKindLength,     1 },
    { RoboCssMediaFeatureHeight,              "height",                RoboCssMediaFeatureKindLength,     1 },
    { RoboCssMediaFeatureAspectRatio,         "aspect-ratio",          RoboCssMediaFeatureKindRatio,      1 },
    { RoboCssMediaFeatureOrientation,         "orientation",           RoboCssMediaFeatureKindKeyword,    0 },
    { RoboCssMediaFeatureResolution,          "resolution",            RoboCssMediaFeatureKindResolution, 1 },
    { RoboCssMediaFeatureColor,               "color",                 RoboCssMediaFeatureKindInteger,    1 },
    { RoboCssMediaFeatureMonochrome,          "monochrome",            RoboCssMediaFeatureKindInteger,    1 },
    { RoboCssMediaFeaturePrefersColorScheme,  "prefers-color-scheme",  RoboCssMediaFeatureKindKeyword,    0 },
    { RoboCssMediaFeaturePrefersReducedMotion,"prefers-reduced-motion",RoboCssMediaFeatureKindKeyword,    0 },
    { RoboCssMediaFeaturePrefersContrast,     "prefers-contrast",      RoboCssMediaFeatureKindKeyword,    0 },
    { RoboCssMediaFeatureHover,               "hover",                 RoboCssMediaFeatureKindKeyword,    0 },
    { RoboCssMediaFeaturePointer,             "pointer",               RoboCssMediaFeatureKindKeyword,    0 },
    { RoboCssMediaFeatureAnyHover,            "any-hover",             RoboCssMediaFeatureKindKeyword,    0 },
    { RoboCssMediaFeatureAnyPointer,          "any-pointer",           RoboCssMediaFeatureKindKeyword,    0 },
    { RoboCssMediaFeatureDisplayMode,         "display-mode",          RoboCssMediaFeatureKindKeyword,    0 },
    { RoboCssMediaFeatureColorGamut,          "color-gamut",           RoboCssMediaFeatureKindKeyword,    0 },
    { RoboCssMediaFeatureScan,                "scan",                  RoboCssMediaFeatureKindKeyword,    0 },
    { RoboCssMediaFeatureGrid,                "grid",                  RoboCssMediaFeatureKindBoolean,    0 },
    { RoboCssMediaFeatureUpdate,              "update",                RoboCssMediaFeatureKindKeyword,    0 }
};

#define MEDIA_FEATURE_COUNT \
    (sizeof(mediaFeatureVocabulary) / sizeof(mediaFeatureVocabulary[0]))

static const MediaFeatureRow *MediaFeatureRowOf(CssMediaFeature feature)
{
    size_t index;

    for (index = 0u; index < MEDIA_FEATURE_COUNT; index++) {
        if (mediaFeatureVocabulary[index].value == feature) {
            return &mediaFeatureVocabulary[index];
        }
    }
    return NULL;
}

int CssMediaFeatureIsKnown(CssMediaFeature feature)
{
    return MediaFeatureRowOf(feature) != NULL;
}

const char *CssMediaFeatureName(CssMediaFeature feature)
{
    const MediaFeatureRow *row = MediaFeatureRowOf(feature);

    return row ? row->keyword : "";
}

size_t CssMediaFeatureCount(void)
{
    return MEDIA_FEATURE_COUNT;
}

CssMediaFeatureKind CssMediaFeatureKindOf(CssMediaFeature feature)
{
    const MediaFeatureRow *row = MediaFeatureRowOf(feature);

    return row ? row->kind : RoboCssMediaFeatureKindBoolean;
}

int CssMediaFeatureIsRange(CssMediaFeature feature)
{
    const MediaFeatureRow *row = MediaFeatureRowOf(feature);

    return row ? row->isRange : 0;
}

CssMediaFeature CssMediaFeatureFromSlice(CssSlice keyword)
{
    size_t index;
    CssSlice base = keyword;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssMediaFeatureWidth;
    }
    /* Strip a `min-`/`max-` prefix so `min-width` answers `width`. */
    if (keyword.length > 4u &&
        CssBytesEqualAsciiCi(keyword.bytes, 4u, "min-")) {
        base = CssSliceFromBytes(keyword.bytes + 4u, keyword.length - 4u);
    } else if (keyword.length > 4u &&
               CssBytesEqualAsciiCi(keyword.bytes, 4u, "max-")) {
        base = CssSliceFromBytes(keyword.bytes + 4u, keyword.length - 4u);
    }
    for (index = 0u; index < MEDIA_FEATURE_COUNT; index++) {
        if (MediaKeywordEquals(base, mediaFeatureVocabulary[index].keyword)) {
            return mediaFeatureVocabulary[index].value;
        }
    }
    return RoboCssMediaFeatureWidth;
}

CssMediaFeature CssMediaFeatureFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssMediaFeatureWidth;
    }
    return CssMediaFeatureFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

static const MediaFeatureRow *MediaFeatureRowForBase(CssSlice base)
{
    size_t index;

    for (index = 0u; index < MEDIA_FEATURE_COUNT; index++) {
        if (MediaKeywordEquals(base, mediaFeatureVocabulary[index].keyword)) {
            return &mediaFeatureVocabulary[index];
        }
    }
    return NULL;
}

int CssMediaFeatureSliceIsKnown(CssSlice keyword)
{
    CssSlice base = keyword;
    int prefixed = 0;
    const MediaFeatureRow *row;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    if (keyword.length > 4u &&
        CssBytesEqualAsciiCi(keyword.bytes, 4u, "min-")) {
        base = CssSliceFromBytes(keyword.bytes + 4u, keyword.length - 4u);
        prefixed = 1;
    } else if (keyword.length > 4u &&
               CssBytesEqualAsciiCi(keyword.bytes, 4u, "max-")) {
        base = CssSliceFromBytes(keyword.bytes + 4u, keyword.length - 4u);
        prefixed = 1;
    }
    row = MediaFeatureRowForBase(base);
    if (!row) {
        return 0;
    }
    /* A `min-`/`max-` prefix is legal only on a range feature. */
    return prefixed ? row->isRange : 1;
}

int CssMediaFeatureFromKeyword(CssSlice keyword,
                               CssMediaFeature *outFeature,
                               CssMediaOperator *outOperator)
{
    const MediaFeatureRow *row;
    CssSlice base = keyword;
    CssMediaOperator op = RoboCssMediaOperatorNone;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    if (keyword.length > 4u &&
        CssBytesEqualAsciiCi(keyword.bytes, 4u, "min-")) {
        base = CssSliceFromBytes(keyword.bytes + 4u, keyword.length - 4u);
        op = RoboCssMediaOperatorMin;
    } else if (keyword.length > 4u &&
               CssBytesEqualAsciiCi(keyword.bytes, 4u, "max-")) {
        base = CssSliceFromBytes(keyword.bytes + 4u, keyword.length - 4u);
        op = RoboCssMediaOperatorMax;
    }
    row = MediaFeatureRowForBase(base);
    if (!row) {
        return 0;
    }
    /* A prefix on a discrete feature is not a feature spelling. */
    if (op != RoboCssMediaOperatorNone && !row->isRange) {
        return 0;
    }
    if (outFeature) {
        *outFeature = row->value;
    }
    if (outOperator) {
        *outOperator = op;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* the context                                                         */
/* ------------------------------------------------------------------ */

void CssMediaContextInit(CssMediaContext *context)
{
    if (!context) {
        return;
    }
    memset(context, 0, sizeof(*context));
}

/* ------------------------------------------------------------------ */
/* matching                                                            */
/* ------------------------------------------------------------------ */

const char *CssMediaMatchName(CssMediaMatch match)
{
    switch (match) {
    case RoboCssMediaMatchNo:
        return "no";
    case RoboCssMediaMatchYes:
        return "yes";
    case RoboCssMediaMatchUnknown:
        return "unknown";
    default:
        return "unknown";
    }
}

/** Compare a numeric context value with a query bound. */
static CssMediaMatch MatchNumber(double contextValue,
                                 int known,
                                 int integer,
                                 double queryValue,
                                 CssMediaOperator op)
{
    if (!known) {
        return RoboCssMediaMatchUnknown;
    }
    if (integer) {
        /* Integer features (`color`, `monochrome`) are queried by a whole value. */
        if (op == RoboCssMediaOperatorMin) {
            return contextValue >= queryValue ? RoboCssMediaMatchYes
                                              : RoboCssMediaMatchNo;
        }
        if (op == RoboCssMediaOperatorMax) {
            return contextValue <= queryValue ? RoboCssMediaMatchYes
                                              : RoboCssMediaMatchNo;
        }
        return contextValue == queryValue ? RoboCssMediaMatchYes
                                          : RoboCssMediaMatchNo;
    }
    if (op == RoboCssMediaOperatorMin) {
        return contextValue >= queryValue ? RoboCssMediaMatchYes
                                          : RoboCssMediaMatchNo;
    }
    if (op == RoboCssMediaOperatorMax) {
        return contextValue <= queryValue ? RoboCssMediaMatchYes
                                          : RoboCssMediaMatchNo;
    }
    /* An exact comparison with a float is exact only where a caller rounded; a
     * query that names a pixel width expects an exact answer. */
    return contextValue == queryValue ? RoboCssMediaMatchYes
                                      : RoboCssMediaMatchNo;
}

static CssMediaMatch MatchKeyword(CssSlice query,
                                  const char *expected)
{
    return MediaSliceEquals(expected, query) ? RoboCssMediaMatchYes
                                             : RoboCssMediaMatchNo;
}

CssMediaMatch CssMediaConditionMatches(const CssMediaCondition *condition,
                                       const CssMediaContext *context)
{
    const MediaFeatureRow *row;

    if (!condition || !context) {
        return RoboCssMediaMatchUnknown;
    }
    row = MediaFeatureRowOf(condition->feature);
    if (!row) {
        return RoboCssMediaMatchUnknown;
    }

    switch (condition->feature) {
    case RoboCssMediaFeatureWidth:
        return MatchNumber(context->viewportWidthPx,
                           context->viewportWidthPx > 0.0, 0,
                           condition->number, condition->op);
    case RoboCssMediaFeatureHeight:
        return MatchNumber(context->viewportHeightPx,
                           context->viewportHeightPx > 0.0, 0,
                           condition->number, condition->op);
    case RoboCssMediaFeatureAspectRatio: {
        double ratio;

        if (context->viewportWidthPx <= 0.0 ||
            context->viewportHeightPx <= 0.0) {
            return RoboCssMediaMatchUnknown;
        }
        ratio = context->viewportWidthPx / context->viewportHeightPx;
        return MatchNumber(ratio, 1, 0, condition->number, condition->op);
    }
    case RoboCssMediaFeatureResolution:
        return MatchNumber(context->resolutionDpi,
                           context->resolutionDpi > 0.0, 0,
                           condition->number, condition->op);
    case RoboCssMediaFeatureColor:
        return MatchNumber((double)context->colorBits,
                           context->colorBits > 0, 1,
                           condition->number, condition->op);
    case RoboCssMediaFeatureMonochrome:
        return MatchNumber((double)context->monochromeBits,
                           context->monochromeBits >= 0 &&
                               context->viewportWidthPx > 0.0,
                           1, condition->number, condition->op);
    case RoboCssMediaFeatureOrientation:
        if (context->viewportWidthPx <= 0.0 ||
            context->viewportHeightPx <= 0.0) {
            return RoboCssMediaMatchUnknown;
        }
        if (context->viewportWidthPx >= context->viewportHeightPx) {
            return MatchKeyword(condition->keyword, "landscape");
        }
        return MatchKeyword(condition->keyword, "portrait");
    case RoboCssMediaFeaturePrefersColorScheme:
        return MatchKeyword(condition->keyword,
                            context->colorScheme == RoboCssMediaColorSchemeDark
                                ? "dark"
                                : "light");
    case RoboCssMediaFeaturePrefersReducedMotion:
        return MatchKeyword(condition->keyword,
                            context->reducedMotion ? "reduce"
                                                   : "no-preference");
    case RoboCssMediaFeatureHover:
        return MatchKeyword(condition->keyword,
                            context->hover == RoboCssMediaCapabilityNone
                                ? "none"
                                : "hover");
    case RoboCssMediaFeaturePointer:
        if (context->pointer == RoboCssMediaCapabilityCoarse) {
            return MatchKeyword(condition->keyword, "coarse");
        }
        if (context->pointer == RoboCssMediaCapabilityFine) {
            return MatchKeyword(condition->keyword, "fine");
        }
        return MatchKeyword(condition->keyword, "none");
    case RoboCssMediaFeatureGrid:
        /* A bitmapped context is not a grid device. */
        return RoboCssMediaMatchNo;
    default:
        /* Anything else the context cannot answer: unknown, never a guess. */
        return RoboCssMediaMatchUnknown;
    }
}

CssMediaMatch CssMediaQueryMatches(const CssMediaQuery *query,
                                   const CssMediaQueryContext *context)
{
    CssMediaMatch overall = RoboCssMediaMatchYes;
    size_t index;

    if (!query || !context) {
        return RoboCssMediaMatchUnknown;
    }

    /* The type is the first gate, and it is decidable. */
    if (query->type != RoboCssMediaTypeAll &&
        query->type != context->mediaType) {
        overall = RoboCssMediaMatchNo;
    }

    for (index = 0u;
         overall != RoboCssMediaMatchNo && index < query->conditionCount;
         index++) {
        CssMediaMatch match =
            CssMediaConditionMatches(&query->conditions[index],
                                     &context->metrics);

        if (match == RoboCssMediaMatchNo) {
            overall = RoboCssMediaMatchNo;
        } else if (match == RoboCssMediaMatchUnknown) {
            overall = RoboCssMediaMatchUnknown;
        }
    }

    if (query->negated) {
        if (overall == RoboCssMediaMatchYes) {
            return RoboCssMediaMatchNo;
        }
        if (overall == RoboCssMediaMatchNo) {
            return RoboCssMediaMatchYes;
        }
    }
    return overall;
}
