/*
 * RoboCSS transition vocabulary (Phase 18.8).
 *
 * A transition is four longhands and a shorthand: which properties animate, how
 * long, with what easing, after what delay. Three of the five are *grammars* —
 * a property name list, a time list, a timing-function list — so they are
 * registered as carried declarations, exactly as a grid track list is. What this
 * module supplies is the two closed sets an author's declaration is checked
 * against:
 *
 *   transition-timing-function keyword subset
 *       ease | linear | ease-in | ease-out | ease-in-out | step-start | step-end
 *   transition-behavior   normal | allow-discrete
 *
 * The timing-function vocabulary is the *keyword* half of the property's grammar:
 * the property also admits `cubic-bezier()`, `steps()` and `linear()`, which are
 * functional notations this phase does not represent. So the set here is what a
 * consumer reads to know a transition's easing is discrete (`step-start`,
 * `step-end`) or continuous, not a claim that the whole grammar is parsed.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h). Not an animation engine and not a clock: nothing here
 * starts a transition, samples an easing curve, interpolates two values or advances
 * a timeline. A value says *what easing* and *what discrete rule* an author wrote;
 * the module that owns a clock reads it.
 */

#ifndef ROBO_CSS_TRANSITION_H
#define ROBO_CSS_TRANSITION_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* timing-function keywords                                            */
/* ------------------------------------------------------------------ */

/**
 * The keyword half of `transition-timing-function` / `animation-timing-function`.
 *
 * EASE is 0 — the initial value. The seven keywords are the whole of the set; the
 * functional forms (`cubic-bezier()`, `steps()`, `linear()`) belong to the
 * property's grammar and are carried rather than represented here.
 */
typedef enum CssTimingFunction {
    /** `ease`: the default cubic-bezier(0.25, 0.1, 0.25, 1). */
    RoboCssTimingFunctionEase = 0,
    /** `linear`: constant rate. */
    RoboCssTimingFunctionLinear,
    /** `ease-in`: slow start. */
    RoboCssTimingFunctionEaseIn,
    /** `ease-out`: slow end. */
    RoboCssTimingFunctionEaseOut,
    /** `ease-in-out`: slow start and end. */
    RoboCssTimingFunctionEaseInOut,
    /** `step-start`: the discrete jump at the start — `steps(1, jump-start)`. */
    RoboCssTimingFunctionStepStart,
    /** `step-end`: the discrete jump at the end — `steps(1, jump-end)`. */
    RoboCssTimingFunctionStepEnd
} CssTimingFunction;

/** Number of defined timing-function keywords. */
#define RoboCssTimingFunctionKeywordCount 7u

int CssTimingFunctionIsKnown(CssTimingFunction function);
const char *CssTimingFunctionName(CssTimingFunction function);
size_t CssTimingFunctionCount(void);
CssTimingFunction CssTimingFunctionFromSlice(CssSlice keyword);
CssTimingFunction CssTimingFunctionFromText(const char *keyword);
int CssTimingFunctionSliceIsKnown(CssSlice keyword);

/**
 * Whether @p function is a discrete step (`step-start`, `step-end`).
 *
 * A discrete easing jumps rather than interpolates, which is the one fact a
 * consumer of an easing keyword needs before it samples anything. Unknown answers 0.
 */
int CssTimingFunctionIsStep(CssTimingFunction function);

Result CssTimingFunctionWrite(CssTimingFunction function,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              CssDiag *diag);

/** Capacity @ref CssTimingFunctionWrite needs ("ease-in-out" plus a NUL). */
#define RoboCssTimingFunctionTextMax 16u

/* ------------------------------------------------------------------ */
/* transition-behavior                                                 */
/* ------------------------------------------------------------------ */

/** A computed `transition-behavior`. NORMAL is 0 — the initial value. */
typedef enum CssTransitionBehavior {
    /** `normal`: only animatable (continuous) properties transition. */
    RoboCssTransitionBehaviorNormal = 0,
    /** `allow-discrete`: discrete properties may transition too. */
    RoboCssTransitionBehaviorAllowDiscrete = 1
} CssTransitionBehavior;

/** Number of defined `transition-behavior` values. */
#define RoboCssTransitionBehaviorCount 2u

int CssTransitionBehaviorIsKnown(CssTransitionBehavior behavior);
const char *CssTransitionBehaviorName(CssTransitionBehavior behavior);
size_t CssTransitionBehaviorCount(void);
CssTransitionBehavior CssTransitionBehaviorFromSlice(CssSlice keyword);
CssTransitionBehavior CssTransitionBehaviorFromText(const char *keyword);
int CssTransitionBehaviorSliceIsKnown(CssSlice keyword);

/** Whether @p behavior allows discrete properties to transition. Unknown answers 0. */
int CssTransitionBehaviorAllowsDiscrete(CssTransitionBehavior behavior);

Result CssTransitionBehaviorWrite(CssTransitionBehavior behavior,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag);

/** Capacity @ref CssTransitionBehaviorWrite needs ("allow-discrete" plus a NUL). */
#define RoboCssTransitionBehaviorTextMax 20u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_TRANSITION_H */
