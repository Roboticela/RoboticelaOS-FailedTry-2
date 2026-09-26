/*
 * RoboCSS animation vocabulary (Phase 18.9).
 *
 * An animation is nine longhands and a shorthand, and four of them are closed sets:
 *
 *   animation-direction     normal | reverse | alternate | alternate-reverse
 *   animation-fill-mode     none | forwards | backwards | both
 *   animation-play-state    running | paused
 *   animation-composition   replace | add | accumulate
 *
 * The rest are grammars — a keyframes name, a time list, a timing-function list and
 * the iteration count — so they are carried, except the count, which is the one
 * numeric shape of its own (`infinite`, or a non-negative number). A `@keyframes`
 * at-rule is the *source* an animation name refers to and is not a property; this
 * module supplies the vocabulary a name is checked against and does not parse the
 * at-rule.
 *
 * `animation-timing-function` reuses the transition family's timing-function
 * keywords (`ease`, `linear`, `step-start`, …) rather than growing a second copy,
 * exactly as `column-rule-*` reuse the border shapes.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h). Not an animation engine and not a clock: nothing here
 * starts an animation, resolves a keyframe, samples an easing curve, interpolates a
 * value or advances a timeline. A value says *what rule* an author wrote; the module
 * that owns a clock reads it.
 */

#ifndef ROBO_CSS_ANIMATION_H
#define ROBO_CSS_ANIMATION_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* animation-direction                                                 */
/* ------------------------------------------------------------------ */

/** A computed `animation-direction`. NORMAL is 0 — the initial value. */
typedef enum CssAnimationDirection {
    /** `normal`: every iteration plays forward. */
    RoboCssAnimationDirectionNormal = 0,
    /** `reverse`: every iteration plays backward. */
    RoboCssAnimationDirectionReverse,
    /** `alternate`: forward, then backward, and so on. */
    RoboCssAnimationDirectionAlternate,
    /** `alternate-reverse`: backward, then forward, and so on. */
    RoboCssAnimationDirectionAlternateReverse
} CssAnimationDirection;

/** Number of defined `animation-direction` values. */
#define RoboCssAnimationDirectionCount 4u

int CssAnimationDirectionIsKnown(CssAnimationDirection direction);
const char *CssAnimationDirectionName(CssAnimationDirection direction);
size_t CssAnimationDirectionCount(void);
CssAnimationDirection CssAnimationDirectionFromSlice(CssSlice keyword);
CssAnimationDirection CssAnimationDirectionFromText(const char *keyword);
int CssAnimationDirectionSliceIsKnown(CssSlice keyword);

/**
 * Whether @p direction plays every iteration backward (`reverse`,
 * `alternate-reverse`). Unknown answers 0.
 */
int CssAnimationDirectionReverses(CssAnimationDirection direction);

/**
 * Whether @p direction flips between iterations (`alternate`, `alternate-reverse`).
 *
 * A separate answer from @ref CssAnimationDirectionReverses: an alternating
 * animation reverses on the *odd* iterations only, so a consumer must know both. An
 * unknown value answers 0.
 */
int CssAnimationDirectionAlternates(CssAnimationDirection direction);

Result CssAnimationDirectionWrite(CssAnimationDirection direction,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag);

/** Capacity @ref CssAnimationDirectionWrite needs ("alternate-reverse" plus a NUL). */
#define RoboCssAnimationDirectionTextMax 24u

/* ------------------------------------------------------------------ */
/* animation-fill-mode                                                 */
/* ------------------------------------------------------------------ */

/** A computed `animation-fill-mode`. NONE is 0 — the initial value. */
typedef enum CssAnimationFillMode {
    /** `none`: the animation applies no style outside its active period. */
    RoboCssAnimationFillModeNone = 0,
    /** `forwards`: the last keyframe's style persists. */
    RoboCssAnimationFillModeForwards,
    /** `backwards`: the first keyframe's style is applied before the start. */
    RoboCssAnimationFillModeBackwards,
    /** `both`: forwards and backwards. */
    RoboCssAnimationFillModeBoth
} CssAnimationFillMode;

/** Number of defined `animation-fill-mode` values. */
#define RoboCssAnimationFillModeCount 4u

int CssAnimationFillModeIsKnown(CssAnimationFillMode mode);
const char *CssAnimationFillModeName(CssAnimationFillMode mode);
size_t CssAnimationFillModeCount(void);
CssAnimationFillMode CssAnimationFillModeFromSlice(CssSlice keyword);
CssAnimationFillMode CssAnimationFillModeFromText(const char *keyword);
int CssAnimationFillModeSliceIsKnown(CssSlice keyword);

/** Whether @p mode fills after the animation (`forwards`, `both`). Unknown answers 0. */
int CssAnimationFillModeFillsForwards(CssAnimationFillMode mode);

/** Whether @p mode fills before the animation (`backwards`, `both`). Unknown answers 0. */
int CssAnimationFillModeFillsBackwards(CssAnimationFillMode mode);

Result CssAnimationFillModeWrite(CssAnimationFillMode mode,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength,
                                 CssDiag *diag);

/** Capacity @ref CssAnimationFillModeWrite needs ("backwards" plus a NUL). */
#define RoboCssAnimationFillModeTextMax 12u

/* ------------------------------------------------------------------ */
/* animation-play-state                                                */
/* ------------------------------------------------------------------ */

/** A computed `animation-play-state`. RUNNING is 0 — the initial value. */
typedef enum CssAnimationPlayState {
    /** `running`: the animation is playing. */
    RoboCssAnimationPlayStateRunning = 0,
    /** `paused`: it is held. */
    RoboCssAnimationPlayStatePaused
} CssAnimationPlayState;

/** Number of defined `animation-play-state` values. */
#define RoboCssAnimationPlayStateCount 2u

int CssAnimationPlayStateIsKnown(CssAnimationPlayState state);
const char *CssAnimationPlayStateName(CssAnimationPlayState state);
size_t CssAnimationPlayStateCount(void);
CssAnimationPlayState CssAnimationPlayStateFromSlice(CssSlice keyword);
CssAnimationPlayState CssAnimationPlayStateFromText(const char *keyword);
int CssAnimationPlayStateSliceIsKnown(CssSlice keyword);

/** Whether @p state is playing (`running`). Unknown answers 0. */
int CssAnimationPlayStateIsRunning(CssAnimationPlayState state);

Result CssAnimationPlayStateWrite(CssAnimationPlayState state,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag);

/** Capacity @ref CssAnimationPlayStateWrite needs ("running" plus a NUL). */
#define RoboCssAnimationPlayStateTextMax 12u

/* ------------------------------------------------------------------ */
/* animation-composition                                               */
/* ------------------------------------------------------------------ */

/** A computed `animation-composition`. REPLACE is 0 — the initial value. */
typedef enum CssAnimationComposition {
    /** `replace`: the keyframe's value replaces the underlying one. */
    RoboCssAnimationCompositionReplace = 0,
    /** `add`: it is added to the underlying one. */
    RoboCssAnimationCompositionAdd,
    /** `accumulate`: it combines with the underlying one. */
    RoboCssAnimationCompositionAccumulate
} CssAnimationComposition;

/** Number of defined `animation-composition` values. */
#define RoboCssAnimationCompositionCount 3u

int CssAnimationCompositionIsKnown(CssAnimationComposition composition);
const char *CssAnimationCompositionName(CssAnimationComposition composition);
size_t CssAnimationCompositionCount(void);
CssAnimationComposition CssAnimationCompositionFromSlice(CssSlice keyword);
CssAnimationComposition CssAnimationCompositionFromText(const char *keyword);
int CssAnimationCompositionSliceIsKnown(CssSlice keyword);

/** Whether @p composition combines with the underlying value (`add`, `accumulate`). */
int CssAnimationCompositionCombines(CssAnimationComposition composition);

Result CssAnimationCompositionWrite(CssAnimationComposition composition,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength,
                                    CssDiag *diag);

/** Capacity @ref CssAnimationCompositionWrite needs ("accumulate" plus a NUL). */
#define RoboCssAnimationCompositionTextMax 16u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_ANIMATION_H */
