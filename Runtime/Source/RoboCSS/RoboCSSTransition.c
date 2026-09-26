/*
 * RoboCSS transition vocabulary (Phase 18.8) — see RoboCSSTransition.h.
 *
 * Two tables, one per vocabulary, and every accessor below reads one of them. The
 * timing-function table is the *keyword* half of the property's grammar: the seven
 * words an author may write, with the one classification a consumer reads (discrete
 * or continuous). Nothing in this file starts a transition, samples a curve or
 * interpolates anything.
 */

#include "RoboCSS/RoboCSSTransition.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

static Result TransitionWriteKeyword(const char *keyword,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainTransition, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainTransition, RoboInvalidArgument,
                           "not a transition value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainTransition, RoboNotSupported,
                           "the transition keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

static int TransitionKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

/* ------------------------------------------------------------------ */
/* timing-function keywords                                            */
/* ------------------------------------------------------------------ */

typedef struct timingFunctionRow {
    CssTimingFunction value;
    const char *keyword;
    int isStep;
} TimingFunctionRow;

static const TimingFunctionRow timingFunctionVocabulary[] = {
    { RoboCssTimingFunctionEase,      "ease",        0 },
    { RoboCssTimingFunctionLinear,    "linear",      0 },
    { RoboCssTimingFunctionEaseIn,    "ease-in",     0 },
    { RoboCssTimingFunctionEaseOut,   "ease-out",    0 },
    { RoboCssTimingFunctionEaseInOut, "ease-in-out", 0 },
    { RoboCssTimingFunctionStepStart, "step-start",  1 },
    { RoboCssTimingFunctionStepEnd,   "step-end",    1 }
};

#define TIMING_FUNCTION_COUNT \
    (sizeof(timingFunctionVocabulary) / sizeof(timingFunctionVocabulary[0]))

static const TimingFunctionRow *TimingFunctionRowOf(CssTimingFunction function)
{
    size_t index;

    for (index = 0u; index < TIMING_FUNCTION_COUNT; index++) {
        if (timingFunctionVocabulary[index].value == function) {
            return &timingFunctionVocabulary[index];
        }
    }
    return NULL;
}

int CssTimingFunctionIsKnown(CssTimingFunction function)
{
    return TimingFunctionRowOf(function) != NULL;
}

const char *CssTimingFunctionName(CssTimingFunction function)
{
    const TimingFunctionRow *row = TimingFunctionRowOf(function);

    return row ? row->keyword : "";
}

size_t CssTimingFunctionCount(void)
{
    return TIMING_FUNCTION_COUNT;
}

CssTimingFunction CssTimingFunctionFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTimingFunctionEase;
    }
    for (index = 0u; index < TIMING_FUNCTION_COUNT; index++) {
        if (TransitionKeywordEquals(keyword,
                                    timingFunctionVocabulary[index].keyword)) {
            return timingFunctionVocabulary[index].value;
        }
    }
    return RoboCssTimingFunctionEase;
}

CssTimingFunction CssTimingFunctionFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTimingFunctionEase;
    }
    return CssTimingFunctionFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTimingFunctionSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TIMING_FUNCTION_COUNT; index++) {
        if (TransitionKeywordEquals(keyword,
                                    timingFunctionVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTimingFunctionIsStep(CssTimingFunction function)
{
    const TimingFunctionRow *row = TimingFunctionRowOf(function);

    return row ? row->isStep : 0;
}

Result CssTimingFunctionWrite(CssTimingFunction function,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              CssDiag *diag)
{
    const TimingFunctionRow *row = TimingFunctionRowOf(function);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainTransition, RoboInvalidArgument,
                           "not a timing-function keyword");
    }
    return TransitionWriteKeyword(row->keyword, buffer, capacity, outLength,
                                  diag);
}

/* ------------------------------------------------------------------ */
/* transition-behavior                                                 */
/* ------------------------------------------------------------------ */

typedef struct transitionBehaviorRow {
    CssTransitionBehavior value;
    const char *keyword;
    int allowsDiscrete;
} TransitionBehaviorRow;

static const TransitionBehaviorRow transitionBehaviorVocabulary[] = {
    { RoboCssTransitionBehaviorNormal,        "normal",         0 },
    { RoboCssTransitionBehaviorAllowDiscrete, "allow-discrete", 1 }
};

#define TRANSITION_BEHAVIOR_COUNT \
    (sizeof(transitionBehaviorVocabulary) / \
     sizeof(transitionBehaviorVocabulary[0]))

static const TransitionBehaviorRow *TransitionBehaviorRowOf(
    CssTransitionBehavior behavior)
{
    size_t index;

    for (index = 0u; index < TRANSITION_BEHAVIOR_COUNT; index++) {
        if (transitionBehaviorVocabulary[index].value == behavior) {
            return &transitionBehaviorVocabulary[index];
        }
    }
    return NULL;
}

int CssTransitionBehaviorIsKnown(CssTransitionBehavior behavior)
{
    return TransitionBehaviorRowOf(behavior) != NULL;
}

const char *CssTransitionBehaviorName(CssTransitionBehavior behavior)
{
    const TransitionBehaviorRow *row = TransitionBehaviorRowOf(behavior);

    return row ? row->keyword : "";
}

size_t CssTransitionBehaviorCount(void)
{
    return TRANSITION_BEHAVIOR_COUNT;
}

CssTransitionBehavior CssTransitionBehaviorFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssTransitionBehaviorNormal;
    }
    for (index = 0u; index < TRANSITION_BEHAVIOR_COUNT; index++) {
        if (TransitionKeywordEquals(keyword,
                                    transitionBehaviorVocabulary[index].keyword)) {
            return transitionBehaviorVocabulary[index].value;
        }
    }
    return RoboCssTransitionBehaviorNormal;
}

CssTransitionBehavior CssTransitionBehaviorFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssTransitionBehaviorNormal;
    }
    return CssTransitionBehaviorFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssTransitionBehaviorSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < TRANSITION_BEHAVIOR_COUNT; index++) {
        if (TransitionKeywordEquals(keyword,
                                    transitionBehaviorVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssTransitionBehaviorAllowsDiscrete(CssTransitionBehavior behavior)
{
    const TransitionBehaviorRow *row = TransitionBehaviorRowOf(behavior);

    return row ? row->allowsDiscrete : 0;
}

Result CssTransitionBehaviorWrite(CssTransitionBehavior behavior,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag)
{
    const TransitionBehaviorRow *row = TransitionBehaviorRowOf(behavior);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainTransition, RoboInvalidArgument,
                           "not a transition-behavior value");
    }
    return TransitionWriteKeyword(row->keyword, buffer, capacity, outLength,
                                  diag);
}
