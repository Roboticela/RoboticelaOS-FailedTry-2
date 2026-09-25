/*
 * Animations and smooth scrolling — Phase 15.13 and 15.12.
 *
 * ## A track, not a callback
 *
 * An animation here is a **value over time**: a property, a start, an end, a duration and an easing.
 * That is all, and it is deliberate — a compositor's animation is the thing a compositor can run
 * without asking anybody: an opacity, an offset, a scale, a scroll. Anything richer (a layout
 * change, a font, a colour decided by a document) is not an animation, it is a *new frame's worth of
 * content*, and pretending otherwise is how a compositor ends up with a script engine inside it.
 *
 * ## Time is the caller's
 *
 * Nothing here sleeps and nothing reads a clock: `CompositorAnimationStep(compositor, now)` applies
 * every active track at @p now and reports how many it moved. A test therefore advances a frame by
 * calling it with the next number, which is the same shape RoboInput's auto-repeat takes and the
 * reason a timing failure in this module is reproducible rather than flaky.
 *
 * ## Reduced motion is a property of the compositor, not of a call site
 *
 * `CompositorSetReducedMotion` exists because the alternative — every animation site remembering to
 * check a setting — is a setting that is honoured in the places somebody remembered. With it on, a
 * new track is applied at its **end value in the same step** (`animationsSnapped`), so a scroll
 * still lands where it was asked to land and simply does not travel there. Snapping to the end
 * rather than dropping the animation is the important half: a dropped animation is a state that
 * never arrives.
 *
 * ## Scroll is the same mechanism
 *
 * `CompositorScrollAnimate` creates one or two `ScrollX`/`ScrollY` tracks over a layer, and its
 * damage is the layer's own box (15.11). There is no second scrolling engine: a smooth scroll is an
 * animation of the property a plain scroll sets, which is why `CompositorLayerSetScroll` remains the
 * door that *is* the state and these two are the door that moves toward it.
 */

#ifndef ROBO_COMPOSITOR_ANIMATION_H
#define ROBO_COMPOSITOR_ANIMATION_H

#include "RoboCompositor/RoboCompositorLayer.h"
#include "RoboCompositor/RoboCompositorTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The easing curve's answer at @p t.
 *
 * @p t is clamped to [0,1] and the result to the same range, so a caller stepping past the end gets
 * the end value rather than an extrapolation. The three curves are the ones the shell's motion
 * tokens name (14.8): linear, ease-out (decelerating, what a scroll is) and ease-in-out.
 */
float CompositorEasingApply(CompositorEasing easing, float t);

/** One animation's state, by value. */
typedef struct CompositorAnimationInfo {
    CompositorAnimationId id;
    CompositorLayerId layer;
    CompositorProperty property;
    float from;
    float to;
    CompositorTime startMs;
    uint32_t durationMs;
    CompositorEasing easing;
    /** 1 for a smooth scroll's track, which is what tells the two apart in a dump. */
    int scroll;
    int active;
} CompositorAnimationInfo;

/**
 * Start a track animating @p property of @p layer from @p from to @p to.
 *
 * @p durationMs 0 is legal and means "arrive at @p to on the next step" — a one-frame transition,
 * which is what reduced motion does and what a caller that wants an immediate change can ask for
 * without a second door. An unknown layer, an unknown property or a non-finite value is
 * `RoboInvalidArgument`; no free slot is `RoboOutOfMemory`.
 *
 * A property that already has an active track on that layer is **replaced** (the old track ends
 * where the new one starts, because the new `from` is the caller's current value), which is what
 * makes a drag that becomes an inertia scroll a continuation rather than a fight.
 */
Result CompositorAnimate(Compositor *compositor,
                         CompositorLayerId layer,
                         CompositorProperty property,
                         float from,
                         float to,
                         CompositorTime startMs,
                         uint32_t durationMs,
                         CompositorEasing easing,
                         CompositorAnimationId *outId,
                         CompositorDiag *diag);

/**
 * Stop a track **without** applying its end value: the property stays where the last step left it.
 * That is the honest answer for a cancelled drag, and the opposite of reduced motion's snap.
 */
Result CompositorAnimationCancel(Compositor *compositor,
                                 CompositorAnimationId id,
                                 CompositorDiag *diag);

/**
 * Apply every active track at @p now.
 *
 * A track that reaches its end applies @p to exactly and is completed. @p outApplied, when non-NULL,
 * receives the number of tracks that changed a property (a completed track counts once, on the step
 * it completed). The compositor's own clock is advanced to @p now by this door, so a caller need not
 * call `CompositorAdvanceTime` first — and a caller that *does* gets the same answer.
 */
Result CompositorAnimationStep(Compositor *compositor,
                              CompositorTime now,
                              unsigned *outApplied,
                              CompositorDiag *diag);

/** Active tracks. */
unsigned CompositorActiveAnimationCount(const Compositor *compositor);

/**
 * The @p id'th active track's state, or `RoboNotFound`.
 *
 * `CompositorGetAnimationInfo` rather than `CompositorAnimationInfo` because the record's own name is
 * the typedef, and a C typedef and a function share one identifier namespace.
 */
Result CompositorGetAnimationInfo(const Compositor *compositor,
                                  CompositorAnimationId id,
                                  CompositorAnimationInfo *out);

/** How many animations the compositor holds, active or completed, and how many slots are free. */
unsigned CompositorAnimationSlots(const Compositor *compositor);

/* ---- Smooth scrolling (15.12) -------------------------------------------------------------- */

/**
 * Scroll @p layer by (@p deltaX, @p deltaY) over @p durationMs.
 *
 * Creates one track per non-zero delta (`ScrollX`/`ScrollY`) from the layer's current offset to the
 * offset plus the delta, and damages the layer's box. @p outScrollX/@p outScrollY receive the ids
 * created (either may be `RoboCompositorAnimationNone`, and both are when the deltas are zero).
 * With reduced motion on, the scroll lands in one step — see this file's comment.
 */
Result CompositorScrollAnimate(Compositor *compositor,
                               CompositorLayerId layer,
                               float deltaX,
                               float deltaY,
                               uint32_t durationMs,
                               CompositorEasing easing,
                               CompositorAnimationId *outScrollX,
                               CompositorAnimationId *outScrollY,
                               CompositorDiag *diag);

/** What the animation machinery has done. */
typedef struct CompositorAnimationStats {
    uint64_t started;
    uint64_t completed;
    uint64_t cancelled;
    uint64_t replaced;
    uint64_t snapped;       /* applied at the end value immediately, by reduced motion */
    uint64_t steps;
    uint64_t smoothScrolls;
    unsigned active;
    int reducedMotion;
} CompositorAnimationStats;

void CompositorAnimationStatsInit(CompositorAnimationStats *stats);
Result CompositorGetAnimationStats(const Compositor *compositor, CompositorAnimationStats *out);
void CompositorResetAnimationStats(Compositor *compositor);

/** One line per track: `animation id property layer from -> to at t active`. Complete or absent. */
Result CompositorWriteAnimations(const Compositor *compositor,
                                 char *buffer,
                                 size_t bufferSize,
                                 size_t *outNeeded);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_COMPOSITOR_ANIMATION_H */
