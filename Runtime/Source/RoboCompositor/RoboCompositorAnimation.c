/*
 * Animation tracks and smooth scrolling — Phase 15.13 and 15.12.
 *
 * ## A track is a function of one number
 *
 * Every track is `property(layer) = interpolate(from, to, easing(t))`, where `t` is
 * `(now - start) / duration` on the caller's clock. There is no scheduler, no callback, no
 * per-frame bookkeeping beyond `active`: a track is a value, and stepping is arithmetic. That is
 * what makes an animation in this module reproducible — a test advances the clock and reads a number
 * rather than waiting and hoping.
 *
 * ## The three properties that need care
 *
 *   **Opacity** is a byte, so a fractional value is rounded and clamped rather than cast: a track
 *   that ends at 255 must land on 255, and a cast would give 254 on some curves.
 *
 *   **Scale must stay positive**, because a zero or negative scale is refused by the layer door and
 *   would make the animation's own *destination* illegal. A curve that dips to zero (it should not,
 *   but a caller's `from` may) is clamped to the smallest usable scale instead.
 *
 *   **Scroll** is not a fifth property: it is `OffsetX`/`OffsetY` with the flag that says why, which
 *   is what `CompositorScrollAnimate` sets so a dump can tell a smooth scroll from a slide.
 *
 * ## Reduced motion snaps rather than drops
 *
 * With reduced motion on, `CompositorAnimate` applies the **end** value in the same call and counts
 * it (`snapped`). Dropping the track instead would be the bug this exists to avoid: a scroll that is
 * never applied is a state the caller asked for that never arrives, and the desktop would simply be
 * somewhere else with no explanation.
 */

#include "RoboCompositorInternal.h"

/* ------------------------------------------------------------------ */
/* Easings                                                             */
/* ------------------------------------------------------------------ */

static float ClampUnit(float t)
{
    if (!(t > 0.0f)) {
        return 0.0f;
    }
    if (t > 1.0f) {
        return 1.0f;
    }
    return t;
}

float CompositorEasingApply(CompositorEasing easing, float t)
{
    float x = ClampUnit(t);
    float s;

    switch (easing) {
    case RoboCompositorEasingEaseOut:
        s = 1.0f - x;
        return ClampUnit(1.0f - s * s);
    case RoboCompositorEasingEaseInOut:
        if (x < 0.5f) {
            return ClampUnit(2.0f * x * x);
        }
        s = 1.0f - x;
        return ClampUnit(1.0f - 2.0f * s * s);
    case RoboCompositorEasingLinear:
    default:
        return x;
    }
}

/* ------------------------------------------------------------------ */
/* Applying a value                                                    */
/* ------------------------------------------------------------------ */

static uint8_t OpacityByte(float value)
{
    int32_t rounded;

    if (!(value > 0.0f)) {
        return 0u;
    }
    if (value >= 255.0f) {
        return 255u;
    }
    rounded = CompositorRound(value);
    if (rounded < 0) {
        return 0u;
    }
    return (uint8_t)rounded;
}

/** Apply @p value to @p property of @p layer, damaging the layer. 0 when the layer went away. */
static int ApplyValue(Compositor *compositor, CompositorLayerId layer, CompositorProperty property,
                      float value)
{
    CompositorLayerSlot *slot = CompositorFindLayer(compositor, layer);

    if (!slot) {
        return 0;
    }
    CompositorInvalidateLayer(compositor, layer, NULL);
    switch (property) {
    case RoboCompositorPropertyOpacity:
        slot->opacity = OpacityByte(value);
        break;
    case RoboCompositorPropertyOffsetX:
        slot->offsetX = value;
        break;
    case RoboCompositorPropertyOffsetY:
        slot->offsetY = value;
        break;
    case RoboCompositorPropertyScale:
        slot->scale = (value > 0.0001f) ? value : 0.0001f;
        break;
    case RoboCompositorPropertyScrollX:
        slot->offsetX = value;
        break;
    case RoboCompositorPropertyScrollY:
        slot->offsetY = value;
        break;
    default:
        return 0;
    }
    CompositorInvalidateLayer(compositor, layer, NULL);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Starting and stopping                                               */
/* ------------------------------------------------------------------ */

static CompositorAnimationSlot *FreeAnimationSlot(Compositor *compositor)
{
    unsigned i;

    for (i = 0u; i < compositor->animationSlots; i++) {
        if (compositor->animations[i].active) {
            continue;
        }
        if (compositor->animations[i].id == RoboCompositorAnimationNone) {
            return &compositor->animations[i];
        }
    }
    /* Reuse a finished track's slot rather than refusing: a completed track is a record, and a
     * compositor that ran out of slots because it never reused one would be a leak. */
    for (i = 0u; i < compositor->animationSlots; i++) {
        if (!compositor->animations[i].active) {
            return &compositor->animations[i];
        }
    }
    return NULL;
}

static Result StartTrack(Compositor *compositor,
                         CompositorLayerId layer,
                         CompositorProperty property,
                         float from,
                         float to,
                         CompositorTime startMs,
                         uint32_t durationMs,
                         CompositorEasing easing,
                         int scroll,
                         CompositorAnimationId *outId,
                         CompositorDiag *diag)
{
    CompositorAnimationSlot *slot;
    CompositorAnimationId id;
    unsigned i;

    if (!CompositorFindLayer(compositor, layer)) {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainAnimation,
                       "no such layer to animate");
        return RoboInvalidArgument;
    }
    if ((unsigned)property >= (unsigned)RoboCompositorPropertyCount || !CompositorPropertyName(property)
        || CompositorPropertyName(property)[0] == '?') {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainAnimation,
                       "that is not a property a track can drive");
        return RoboInvalidArgument;
    }
    if (!CompositorValueIsFinite(from) || !CompositorValueIsFinite(to)) {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainAnimation,
                       "an animation's values must be finite");
        return RoboInvalidArgument;
    }
    /* A property with an active track is replaced: the caller's `from` is the current value, so the
     * new track continues where the old one was rather than fighting it. */
    for (i = 0u; i < compositor->animationSlots; i++) {
        CompositorAnimationSlot *existing = &compositor->animations[i];
        if (existing->active && existing->layer == layer && existing->property == property) {
            existing->active = 0;
            compositor->statAnimationsReplaced++;
        }
    }
    slot = FreeAnimationSlot(compositor);
    if (!slot) {
        CompositorFail(compositor, diag, RoboOutOfMemory, RoboCompositorDomainAnimation,
                       "every animation slot is active: raise maxAnimations");
        return RoboOutOfMemory;
    }
    compositor->nextAnimationId++;
    if (compositor->nextAnimationId == RoboCompositorAnimationNone) {
        compositor->nextAnimationId++;
    }
    id = compositor->nextAnimationId;
    *slot = (CompositorAnimationSlot){
        .id = id,
        .layer = layer,
        .property = property,
        .from = from,
        .to = to,
        .startMs = startMs,
        .durationMs = durationMs,
        .easing = easing,
        .scroll = scroll,
        .active = 1
    };
    compositor->statAnimationsStarted++;
    if (outId) {
        *outId = id;
    }
    if (compositor->reducedMotion) {
        /* Snap: the value arrives now, and the track is finished before it is ever stepped. */
        ApplyValue(compositor, layer, property, to);
        slot->active = 0;
        compositor->statAnimationsSnapped++;
        compositor->statAnimationsCompleted++;
    } else {
        /* The first step is on the caller's clock, so a track started but never stepped is not
         * applied at all — the property is already wherever the caller put it. */
        CompositorPostFrame(compositor, RoboCompositorFrameReasonAnimation, NULL);
    }
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorAnimate(Compositor *compositor,
                         CompositorLayerId layer,
                         CompositorProperty property,
                         float from,
                         float to,
                         CompositorTime startMs,
                         uint32_t durationMs,
                         CompositorEasing easing,
                         CompositorAnimationId *outId,
                         CompositorDiag *diag)
{
    if (outId) {
        *outId = RoboCompositorAnimationNone;
    }
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    return StartTrack(compositor, layer, property, from, to, startMs, durationMs, easing, 0, outId,
                      diag);
}

Result CompositorAnimationCancel(Compositor *compositor, CompositorAnimationId id,
                                 CompositorDiag *diag)
{
    unsigned i;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (id == RoboCompositorAnimationNone) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < compositor->animationSlots; i++) {
        CompositorAnimationSlot *slot = &compositor->animations[i];
        if (slot->id != id || !slot->active) {
            continue;
        }
        /* Cancelled, not applied: the property stays where the last step left it, which is the
         * honest answer for a drag the user let go of mid-flight. */
        slot->active = 0;
        compositor->statAnimationsCancelled++;
        if (diag) {
            CompositorDiagReset(diag);
        }
        return RoboOk;
    }
    CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainAnimation,
                   "no such active animation to cancel");
    return RoboNotFound;
}

Result CompositorAnimationStep(Compositor *compositor,
                               CompositorTime now,
                               unsigned *outApplied,
                               CompositorDiag *diag)
{
    unsigned applied = 0u;
    unsigned i;
    Result result;

    if (outApplied) {
        *outApplied = 0u;
    }
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    /* The clock is the compositor's, so a caller that already advanced it gets the same answer as
     * one that did not. */
    result = CompositorAdvanceTime(compositor, now, NULL, diag);
    if (result != RoboOk) {
        return result;
    }
    for (i = 0u; i < compositor->animationSlots; i++) {
        CompositorAnimationSlot *slot = &compositor->animations[i];
        float value;
        CompositorTime elapsed;
        int done;
        if (!slot->active) {
            continue;
        }
        if (!CompositorFindLayer(compositor, slot->layer)) {
            /* The layer went away under the track: end it rather than animate a ghost. */
            slot->active = 0;
            compositor->statAnimationsCancelled++;
            continue;
        }
        elapsed = now - slot->startMs;
        done = (slot->durationMs == 0u) || (elapsed >= (CompositorTime)slot->durationMs);
        if (done) {
            value = slot->to;
        } else {
            float t = (elapsed <= 0) ? 0.0f
                                     : (float)elapsed / (float)slot->durationMs;
            float eased = CompositorEasingApply(slot->easing, t);
            value = slot->from + (slot->to - slot->from) * eased;
        }
        if (ApplyValue(compositor, slot->layer, slot->property, value)) {
            applied++;
            compositor->statAnimationSteps++;
        }
        if (done) {
            slot->active = 0;
            compositor->statAnimationsCompleted++;
        }
    }
    if (outApplied) {
        *outApplied = applied;
    }
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

unsigned CompositorActiveAnimationCount(const Compositor *compositor)
{
    unsigned i;
    unsigned active = 0u;

    if (!compositor) {
        return 0u;
    }
    for (i = 0u; i < compositor->animationSlots; i++) {
        if (compositor->animations[i].active) {
            active++;
        }
    }
    return active;
}

Result CompositorGetAnimationInfo(const Compositor *compositor,
                                  CompositorAnimationId id,
                                  CompositorAnimationInfo *out)
{
    unsigned i;

    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < compositor->animationSlots; i++) {
        const CompositorAnimationSlot *slot = &compositor->animations[i];
        if (slot->id != id || !slot->active) {
            continue;
        }
        MemSet(out, 0, sizeof(*out));
        out->id = slot->id;
        out->layer = slot->layer;
        out->property = slot->property;
        out->from = slot->from;
        out->to = slot->to;
        out->startMs = slot->startMs;
        out->durationMs = slot->durationMs;
        out->easing = slot->easing;
        out->scroll = slot->scroll;
        out->active = slot->active;
        return RoboOk;
    }
    return RoboNotFound;
}

unsigned CompositorAnimationSlots(const Compositor *compositor)
{
    unsigned i;
    unsigned used = 0u;

    if (!compositor) {
        return 0u;
    }
    for (i = 0u; i < compositor->animationSlots; i++) {
        if (compositor->animations[i].id != RoboCompositorAnimationNone) {
            used++;
        }
    }
    return used;
}

/* ------------------------------------------------------------------ */
/* Smooth scrolling (15.12)                                            */
/* ------------------------------------------------------------------ */

Result CompositorScrollAnimate(Compositor *compositor,
                               CompositorLayerId layer,
                               float deltaX,
                               float deltaY,
                               uint32_t durationMs,
                               CompositorEasing easing,
                               CompositorAnimationId *outScrollX,
                               CompositorAnimationId *outScrollY,
                               CompositorDiag *diag)
{
    CompositorLayerSlot *slot;
    float fromX;
    float fromY;
    Result result;

    if (outScrollX) {
        *outScrollX = RoboCompositorAnimationNone;
    }
    if (outScrollY) {
        *outScrollY = RoboCompositorAnimationNone;
    }
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayer(compositor, layer);
    if (!slot) {
        CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainScroll,
                       "no such layer to scroll smoothly");
        return RoboNotFound;
    }
    if (!CompositorValueIsFinite(deltaX) || !CompositorValueIsFinite(deltaY)) {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainScroll,
                       "a scroll delta must be finite");
        return RoboInvalidArgument;
    }
    fromX = slot->offsetX;
    fromY = slot->offsetY;
    if (deltaX != 0.0f) {
        result = StartTrack(compositor, layer, RoboCompositorPropertyScrollX, fromX, fromX + deltaX,
                            compositor->now, durationMs, easing, 1, outScrollX, diag);
        if (result != RoboOk) {
            return result;
        }
    }
    if (deltaY != 0.0f) {
        result = StartTrack(compositor, layer, RoboCompositorPropertyScrollY, fromY, fromY + deltaY,
                            compositor->now, durationMs, easing, 1, outScrollY, diag);
        if (result != RoboOk) {
            return result;
        }
    }
    {
        GfxRect bounds = CompositorLayerLocalBounds(compositor, layer);
        if (!GfxRectIsEmpty(bounds)) {
            CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, bounds, NULL);
        }
    }
    compositor->statSmoothScrolls++;
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Stats and dump                                                      */
/* ------------------------------------------------------------------ */

void CompositorAnimationStatsInit(CompositorAnimationStats *stats)
{
    if (stats) {
        MemSet(stats, 0, sizeof(*stats));
    }
}

Result CompositorGetAnimationStats(const Compositor *compositor, CompositorAnimationStats *out)
{
    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    CompositorAnimationStatsInit(out);
    out->started = compositor->statAnimationsStarted;
    out->completed = compositor->statAnimationsCompleted;
    out->cancelled = compositor->statAnimationsCancelled;
    out->replaced = compositor->statAnimationsReplaced;
    out->snapped = compositor->statAnimationsSnapped;
    out->steps = compositor->statAnimationSteps;
    out->smoothScrolls = compositor->statSmoothScrolls;
    out->active = CompositorActiveAnimationCount(compositor);
    out->reducedMotion = compositor->reducedMotion;
    return RoboOk;
}

void CompositorResetAnimationStats(Compositor *compositor)
{
    if (!compositor) {
        return;
    }
    compositor->statAnimationsStarted = 0u;
    compositor->statAnimationsCompleted = 0u;
    compositor->statAnimationsCancelled = 0u;
    compositor->statAnimationsReplaced = 0u;
    compositor->statAnimationsSnapped = 0u;
    compositor->statAnimationSteps = 0u;
    compositor->statSmoothScrolls = 0u;
}

Result CompositorWriteAnimations(const Compositor *compositor,
                                 char *buffer,
                                 size_t bufferSize,
                                 size_t *outNeeded)
{
    CompositorDump dump;
    unsigned i;

    if (!compositor) {
        return RoboInvalidArgument;
    }
    CompositorDumpInit(&dump, buffer, bufferSize);
    CompositorDumpPrintf(&dump, "animations active=%u reducedMotion=%d\n",
                         CompositorActiveAnimationCount(compositor), compositor->reducedMotion);
    for (i = 0u; i < compositor->animationSlots; i++) {
        const CompositorAnimationSlot *slot = &compositor->animations[i];
        if (slot->id == RoboCompositorAnimationNone) {
            continue;
        }
        CompositorDumpPrintf(&dump,
                             "animation %u %s layer %llu %g -> %g start %lld duration %u %s %s "
                             "%s\n",
                             slot->id, CompositorPropertyName(slot->property),
                             (unsigned long long)slot->layer, (double)slot->from, (double)slot->to,
                             (long long)slot->startMs, slot->durationMs,
                             CompositorEasingName(slot->easing), slot->scroll ? "scroll" : "plain",
                             slot->active ? "active" : "done");
    }
    return CompositorDumpFinish(&dump, outNeeded);
}
