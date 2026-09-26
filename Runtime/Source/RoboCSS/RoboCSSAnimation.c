/*
 * RoboCSS animation vocabulary (Phase 18.9) — see RoboCSSAnimation.h.
 *
 * Four tables, one per vocabulary, and every accessor below reads one of them.
 * `animation-direction` carries two classification columns (reverses, alternates)
 * because an alternating animation reverses on the odd iterations only, so they are
 * two questions. Nothing in this file starts an animation, resolves a keyframe or
 * samples a curve.
 */

#include "RoboCSS/RoboCSSAnimation.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

static Result AnimationWriteKeyword(const char *keyword,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength,
                                    CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainAnimation, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainAnimation, RoboInvalidArgument,
                           "not an animation value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainAnimation, RoboNotSupported,
                           "the animation keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

static int AnimationKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

/* ------------------------------------------------------------------ */
/* animation-direction                                                 */
/* ------------------------------------------------------------------ */

typedef struct animationDirectionRow {
    CssAnimationDirection value;
    const char *keyword;
    int reverses;
    int alternates;
} AnimationDirectionRow;

static const AnimationDirectionRow animationDirectionVocabulary[] = {
    { RoboCssAnimationDirectionNormal,           "normal",            0, 0 },
    { RoboCssAnimationDirectionReverse,          "reverse",           1, 0 },
    { RoboCssAnimationDirectionAlternate,        "alternate",         0, 1 },
    { RoboCssAnimationDirectionAlternateReverse, "alternate-reverse", 1, 1 }
};

#define ANIMATION_DIRECTION_COUNT \
    (sizeof(animationDirectionVocabulary) / \
     sizeof(animationDirectionVocabulary[0]))

static const AnimationDirectionRow *AnimationDirectionRowOf(
    CssAnimationDirection direction)
{
    size_t index;

    for (index = 0u; index < ANIMATION_DIRECTION_COUNT; index++) {
        if (animationDirectionVocabulary[index].value == direction) {
            return &animationDirectionVocabulary[index];
        }
    }
    return NULL;
}

int CssAnimationDirectionIsKnown(CssAnimationDirection direction)
{
    return AnimationDirectionRowOf(direction) != NULL;
}

const char *CssAnimationDirectionName(CssAnimationDirection direction)
{
    const AnimationDirectionRow *row = AnimationDirectionRowOf(direction);

    return row ? row->keyword : "";
}

size_t CssAnimationDirectionCount(void)
{
    return ANIMATION_DIRECTION_COUNT;
}

CssAnimationDirection CssAnimationDirectionFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssAnimationDirectionNormal;
    }
    for (index = 0u; index < ANIMATION_DIRECTION_COUNT; index++) {
        if (AnimationKeywordEquals(keyword,
                                   animationDirectionVocabulary[index].keyword)) {
            return animationDirectionVocabulary[index].value;
        }
    }
    return RoboCssAnimationDirectionNormal;
}

CssAnimationDirection CssAnimationDirectionFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssAnimationDirectionNormal;
    }
    return CssAnimationDirectionFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssAnimationDirectionSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < ANIMATION_DIRECTION_COUNT; index++) {
        if (AnimationKeywordEquals(keyword,
                                   animationDirectionVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssAnimationDirectionReverses(CssAnimationDirection direction)
{
    const AnimationDirectionRow *row = AnimationDirectionRowOf(direction);

    return row ? row->reverses : 0;
}

int CssAnimationDirectionAlternates(CssAnimationDirection direction)
{
    const AnimationDirectionRow *row = AnimationDirectionRowOf(direction);

    return row ? row->alternates : 0;
}

Result CssAnimationDirectionWrite(CssAnimationDirection direction,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag)
{
    const AnimationDirectionRow *row = AnimationDirectionRowOf(direction);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainAnimation, RoboInvalidArgument,
                           "not an animation-direction value");
    }
    return AnimationWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* animation-fill-mode                                                 */
/* ------------------------------------------------------------------ */

typedef struct animationFillModeRow {
    CssAnimationFillMode value;
    const char *keyword;
    int forwards;
    int backwards;
} AnimationFillModeRow;

static const AnimationFillModeRow animationFillModeVocabulary[] = {
    { RoboCssAnimationFillModeNone,      "none",      0, 0 },
    { RoboCssAnimationFillModeForwards,  "forwards",  1, 0 },
    { RoboCssAnimationFillModeBackwards, "backwards", 0, 1 },
    { RoboCssAnimationFillModeBoth,      "both",      1, 1 }
};

#define ANIMATION_FILL_MODE_COUNT \
    (sizeof(animationFillModeVocabulary) / \
     sizeof(animationFillModeVocabulary[0]))

static const AnimationFillModeRow *AnimationFillModeRowOf(
    CssAnimationFillMode mode)
{
    size_t index;

    for (index = 0u; index < ANIMATION_FILL_MODE_COUNT; index++) {
        if (animationFillModeVocabulary[index].value == mode) {
            return &animationFillModeVocabulary[index];
        }
    }
    return NULL;
}

int CssAnimationFillModeIsKnown(CssAnimationFillMode mode)
{
    return AnimationFillModeRowOf(mode) != NULL;
}

const char *CssAnimationFillModeName(CssAnimationFillMode mode)
{
    const AnimationFillModeRow *row = AnimationFillModeRowOf(mode);

    return row ? row->keyword : "";
}

size_t CssAnimationFillModeCount(void)
{
    return ANIMATION_FILL_MODE_COUNT;
}

CssAnimationFillMode CssAnimationFillModeFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssAnimationFillModeNone;
    }
    for (index = 0u; index < ANIMATION_FILL_MODE_COUNT; index++) {
        if (AnimationKeywordEquals(keyword,
                                   animationFillModeVocabulary[index].keyword)) {
            return animationFillModeVocabulary[index].value;
        }
    }
    return RoboCssAnimationFillModeNone;
}

CssAnimationFillMode CssAnimationFillModeFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssAnimationFillModeNone;
    }
    return CssAnimationFillModeFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssAnimationFillModeSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < ANIMATION_FILL_MODE_COUNT; index++) {
        if (AnimationKeywordEquals(keyword,
                                   animationFillModeVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssAnimationFillModeFillsForwards(CssAnimationFillMode mode)
{
    const AnimationFillModeRow *row = AnimationFillModeRowOf(mode);

    return row ? row->forwards : 0;
}

int CssAnimationFillModeFillsBackwards(CssAnimationFillMode mode)
{
    const AnimationFillModeRow *row = AnimationFillModeRowOf(mode);

    return row ? row->backwards : 0;
}

Result CssAnimationFillModeWrite(CssAnimationFillMode mode,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength,
                                 CssDiag *diag)
{
    const AnimationFillModeRow *row = AnimationFillModeRowOf(mode);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainAnimation, RoboInvalidArgument,
                           "not an animation-fill-mode value");
    }
    return AnimationWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* animation-play-state                                                */
/* ------------------------------------------------------------------ */

typedef struct animationPlayStateRow {
    CssAnimationPlayState value;
    const char *keyword;
    int running;
} AnimationPlayStateRow;

static const AnimationPlayStateRow animationPlayStateVocabulary[] = {
    { RoboCssAnimationPlayStateRunning, "running", 1 },
    { RoboCssAnimationPlayStatePaused,  "paused",  0 }
};

#define ANIMATION_PLAY_STATE_COUNT \
    (sizeof(animationPlayStateVocabulary) / \
     sizeof(animationPlayStateVocabulary[0]))

static const AnimationPlayStateRow *AnimationPlayStateRowOf(
    CssAnimationPlayState state)
{
    size_t index;

    for (index = 0u; index < ANIMATION_PLAY_STATE_COUNT; index++) {
        if (animationPlayStateVocabulary[index].value == state) {
            return &animationPlayStateVocabulary[index];
        }
    }
    return NULL;
}

int CssAnimationPlayStateIsKnown(CssAnimationPlayState state)
{
    return AnimationPlayStateRowOf(state) != NULL;
}

const char *CssAnimationPlayStateName(CssAnimationPlayState state)
{
    const AnimationPlayStateRow *row = AnimationPlayStateRowOf(state);

    return row ? row->keyword : "";
}

size_t CssAnimationPlayStateCount(void)
{
    return ANIMATION_PLAY_STATE_COUNT;
}

CssAnimationPlayState CssAnimationPlayStateFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssAnimationPlayStateRunning;
    }
    for (index = 0u; index < ANIMATION_PLAY_STATE_COUNT; index++) {
        if (AnimationKeywordEquals(keyword,
                                   animationPlayStateVocabulary[index].keyword)) {
            return animationPlayStateVocabulary[index].value;
        }
    }
    return RoboCssAnimationPlayStateRunning;
}

CssAnimationPlayState CssAnimationPlayStateFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssAnimationPlayStateRunning;
    }
    return CssAnimationPlayStateFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssAnimationPlayStateSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < ANIMATION_PLAY_STATE_COUNT; index++) {
        if (AnimationKeywordEquals(keyword,
                                   animationPlayStateVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssAnimationPlayStateIsRunning(CssAnimationPlayState state)
{
    const AnimationPlayStateRow *row = AnimationPlayStateRowOf(state);

    return row ? row->running : 0;
}

Result CssAnimationPlayStateWrite(CssAnimationPlayState state,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag)
{
    const AnimationPlayStateRow *row = AnimationPlayStateRowOf(state);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainAnimation, RoboInvalidArgument,
                           "not an animation-play-state value");
    }
    return AnimationWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* animation-composition                                               */
/* ------------------------------------------------------------------ */

typedef struct animationCompositionRow {
    CssAnimationComposition value;
    const char *keyword;
    int combines;
} AnimationCompositionRow;

static const AnimationCompositionRow animationCompositionVocabulary[] = {
    { RoboCssAnimationCompositionReplace,    "replace",    0 },
    { RoboCssAnimationCompositionAdd,        "add",        1 },
    { RoboCssAnimationCompositionAccumulate, "accumulate", 1 }
};

#define ANIMATION_COMPOSITION_COUNT \
    (sizeof(animationCompositionVocabulary) / \
     sizeof(animationCompositionVocabulary[0]))

static const AnimationCompositionRow *AnimationCompositionRowOf(
    CssAnimationComposition composition)
{
    size_t index;

    for (index = 0u; index < ANIMATION_COMPOSITION_COUNT; index++) {
        if (animationCompositionVocabulary[index].value == composition) {
            return &animationCompositionVocabulary[index];
        }
    }
    return NULL;
}

int CssAnimationCompositionIsKnown(CssAnimationComposition composition)
{
    return AnimationCompositionRowOf(composition) != NULL;
}

const char *CssAnimationCompositionName(CssAnimationComposition composition)
{
    const AnimationCompositionRow *row = AnimationCompositionRowOf(composition);

    return row ? row->keyword : "";
}

size_t CssAnimationCompositionCount(void)
{
    return ANIMATION_COMPOSITION_COUNT;
}

CssAnimationComposition CssAnimationCompositionFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssAnimationCompositionReplace;
    }
    for (index = 0u; index < ANIMATION_COMPOSITION_COUNT; index++) {
        if (AnimationKeywordEquals(keyword,
                                   animationCompositionVocabulary[index].keyword)) {
            return animationCompositionVocabulary[index].value;
        }
    }
    return RoboCssAnimationCompositionReplace;
}

CssAnimationComposition CssAnimationCompositionFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssAnimationCompositionReplace;
    }
    return CssAnimationCompositionFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssAnimationCompositionSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < ANIMATION_COMPOSITION_COUNT; index++) {
        if (AnimationKeywordEquals(keyword,
                                   animationCompositionVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssAnimationCompositionCombines(CssAnimationComposition composition)
{
    const AnimationCompositionRow *row = AnimationCompositionRowOf(composition);

    return row ? row->combines : 0;
}

Result CssAnimationCompositionWrite(CssAnimationComposition composition,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength,
                                    CssDiag *diag)
{
    const AnimationCompositionRow *row = AnimationCompositionRowOf(composition);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainAnimation, RoboInvalidArgument,
                           "not an animation-composition value");
    }
    return AnimationWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}
