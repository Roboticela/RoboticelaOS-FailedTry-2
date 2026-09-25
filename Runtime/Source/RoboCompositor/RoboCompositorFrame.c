/*
 * Frames, the clock, the post queue and present fences — Phase 15.9, 15.10, 15.14, 15.15, 15.17.
 *
 * ## The schedule, in one place
 *
 *     posts drained  →  animations stepped  →  damage → tiles → jobs  →  composite  →  record →
 *     fence
 *
 * `CompositorPumpFrame` is that order exactly once, and `CompositorPresent` is its last three steps.
 * A caller that wants to drive the parts itself (a shell with its own loop) calls the doors; a
 * caller that wants one call gets the pump. Neither path has its own copy of the order, because
 * there is only one.
 *
 * ## Vsync is a deadline, and missing it is recorded rather than hidden
 *
 * With an interval set, a frame that arrives **before** its deadline coalesces: the damage stays,
 * the animation has not moved, and the present is deferred, which is what "don't paint faster than
 * the panel" means. A frame that arrives after the deadline draws and is marked **late** — not
 * snapped back to the boundary, because a frame that pretends it was on time is how a latency
 * problem becomes invisible.
 *
 * ## A present that fails keeps its damage
 *
 * The damage list is the only record of what still needs drawing, so it is cleared **after** the
 * composite succeeds and not before. A present that refused — a destination too small, a surface
 * that cannot be written — leaves the list exactly as it was, so the next frame still knows what to
 * do. That one ordering is the difference between a transient failure and a permanently stale
 * screen.
 *
 * ## Fences are the destination's pixels
 *
 * There is no GPU and no swap chain here, so a present *is* the destination's pixels and the fence
 * is a number saying which present a caller has seen. `CompositorWaitFence` therefore answers
 * `RoboTimeout` for a fence the compositor has not reached rather than blocking, and the conformance
 * suite proves the claim by reading the destination after a present rather than trusting the code.
 */

#include "RoboCompositorInternal.h"
#include "RoboCompositor/RoboCompositorFrame.h"

/* ------------------------------------------------------------------ */
/* The frame record                                                    */
/* ------------------------------------------------------------------ */

void CompositorFrameInit(CompositorFrame *frame)
{
    if (!frame) {
        return;
    }
    MemSet(frame, 0, sizeof(*frame));
    frame->index = RoboCompositorFrameNone;
    frame->reason = RoboCompositorFrameReasonDamage;
    frame->kind = RoboCompositorPresentFull;
    frame->fence = RoboCompositorFenceNone;
}

int CompositorFrameIsValid(const CompositorFrame *frame)
{
    if (!frame) {
        return 0;
    }
    /* A frame that was never presented has index 0 and is the *initial* record rather than a valid
     * one: a caller must be able to tell "no frame yet" from "a frame that cost nothing". */
    if (frame->index == RoboCompositorFrameNone || !frame->presented) {
        return 0;
    }
    if ((unsigned)frame->reason >= (unsigned)RoboCompositorFrameReasonCount) {
        return 0;
    }
    if ((unsigned)frame->kind > (unsigned)RoboCompositorPresentDamage) {
        return 0;
    }
    if (frame->damageRects > 0u && frame->kind == RoboCompositorPresentFull && frame->damagePixels == 0u) {
        return 1; /* a full present with no recorded damage is legal: it writes everything */
    }
    if (frame->compositedPixels > (uint64_t)0x7fffffffu) {
        return 0;
    }
    return 1;
}

int CompositorFrameEqual(const CompositorFrame *a, const CompositorFrame *b)
{
    if (!a || !b) {
        return 0;
    }
    return a->index == b->index && a->timeMs == b->timeMs && a->reason == b->reason &&
           a->kind == b->kind && a->damageRects == b->damageRects &&
           a->damagePixels == b->damagePixels && a->compositedPixels == b->compositedPixels &&
           a->presentedBytes == b->presentedBytes && a->layersPainted == b->layersPainted &&
           a->jobsRun == b->jobsRun && a->tilesRasterized == b->tilesRasterized &&
           a->presented == b->presented && a->vsyncAligned == b->vsyncAligned &&
           a->late == b->late && a->fence == b->fence;
}

/* ------------------------------------------------------------------ */
/* The post queue (15.14)                                              */
/* ------------------------------------------------------------------ */

Result CompositorPostFrame(Compositor *compositor, CompositorFrameReason reason, CompositorDiag *diag)
{
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if ((unsigned)reason >= (unsigned)RoboCompositorFrameReasonCount) {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainFrame,
                       "that is not a frame reason");
        return RoboInvalidArgument;
    }
    compositor->statPosts++;
    if (compositor->pending) {
        /* Coalesced: the first reason is kept, because it is the one that made the frame necessary
         * and a later post adds nothing a frame does not already do. */
        compositor->statPostsCoalesced++;
    } else {
        compositor->pending = 1;
        compositor->pendingReason = reason;
    }
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

int CompositorFramePending(const Compositor *compositor)
{
    return compositor ? compositor->pending : 0;
}

CompositorFrameReason CompositorPendingReason(const Compositor *compositor)
{
    if (!compositor || !compositor->pending) {
        return RoboCompositorFrameReasonVsync;
    }
    return compositor->pendingReason;
}

unsigned CompositorDrainPosts(Compositor *compositor)
{
    unsigned drained;

    if (!compositor) {
        return 0u;
    }
    drained = compositor->pending ? 1u : 0u;
    compositor->pending = 0;
    compositor->statPostsDrained += drained;
    return drained;
}

/* ------------------------------------------------------------------ */
/* Reduced motion                                                      */
/* ------------------------------------------------------------------ */

Result CompositorSetReducedMotion(Compositor *compositor, int on, CompositorDiag *diag)
{
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    compositor->reducedMotion = on ? 1 : 0;
    compositor->config.reducedMotion = compositor->reducedMotion;
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

int CompositorReducedMotion(const Compositor *compositor)
{
    return compositor ? compositor->reducedMotion : 0;
}

/* ------------------------------------------------------------------ */
/* Presenting                                                          */
/* ------------------------------------------------------------------ */

Result CompositorPresent(Compositor *compositor,
                         GfxSurface *target,
                         CompositorPresentKind kind,
                         CompositorFrame *out,
                         CompositorDiag *diag)
{
    GfxSize composition;
    CompositorComposeResult result;
    CompositorFrame frame;
    uint64_t damagePixels;
    unsigned i;
    GfxSize targetSize;

    if (!CompositorIsLive(compositor) || !target) {
        return RoboInvalidArgument;
    }
    composition = compositor->config.size;
    targetSize = GfxSurfaceSize(target);
    if (targetSize.width < composition.width || targetSize.height < composition.height) {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainFrame,
                       "the destination is smaller than the composition");
        return RoboInvalidArgument;
    }
    if (!GfxSurfacePixels(target)) {
        CompositorFail(compositor, diag, RoboNotSupported, RoboCompositorDomainFrame,
                       "the destination surface is not writable");
        return RoboNotSupported;
    }
    MemSet(&result, 0, sizeof(result));
    damagePixels = CompositorDamagePixelCount(compositor);
    CompositorFrameInit(&frame);
    if (kind == RoboCompositorPresentFull || compositor->damageFull) {
        GfxRect whole = CompositorCompositionBounds(compositor);
        CompositorComposeTree(compositor, target, &whole, &result, NULL);
    } else {
        /* Region presents walk the composition-space entries only: a surface region means pixels
         * were produced, and the raster run has already invalidated the layers that use it, so the
         * matching composition region is in this same list. */
        for (i = 0u; i < compositor->damageCount; i++) {
            CompositorComposeResult one;
            if (compositor->damage[i].surface != RoboCompositorSurfaceNone) {
                continue;
            }
            MemSet(&one, 0, sizeof(one));
            CompositorComposeTree(compositor, target, &compositor->damage[i].rect, &one, NULL);
            result.pixelsWritten += one.pixelsWritten;
            result.layersPainted += one.layersPainted;
            result.layersCulled += one.layersCulled;
            result.layersClipped += one.layersClipped;
        }
    }
    /* The frame's record. `pixelsWritten` is the frame's cost, and it is what a two-pixel change
     * proves: a present that wrote two pixels says two. */
    frame.index = compositor->frameCount + 1u;
    frame.timeMs = compositor->now;
    frame.reason = compositor->pending ? compositor->pendingReason : RoboCompositorFrameReasonDamage;
    frame.kind = (kind == RoboCompositorPresentFull || compositor->damageFull)
                     ? RoboCompositorPresentFull
                     : RoboCompositorPresentDamage;
    frame.damageRects = compositor->damageCount + (compositor->damageFull ? 1u : 0u);
    frame.damagePixels = damagePixels;
    frame.compositedPixels = result.pixelsWritten;
    frame.presentedBytes = (uint64_t)result.pixelsWritten * 4u;
    frame.layersPainted = result.layersPainted;
    frame.jobsRun = (unsigned)(compositor->statJobsRun - compositor->presentedJobsBaseline);
    frame.tilesRasterized =
        (unsigned)(compositor->statRasters - compositor->presentedTilesBaseline);
    frame.presented = 1;
    frame.vsyncAligned = (compositor->vsyncInterval && compositor->vsyncArmed) ? 1 : 0;
    frame.late = 0;
    compositor->fence++;
    frame.fence = compositor->fence;
    compositor->frameCount = frame.index;
    compositor->lastFrame = frame;
    compositor->hasLastFrame = 1;
    compositor->presentedJobsBaseline = compositor->statJobsRun;
    compositor->presentedTilesBaseline = compositor->statRasters;
    compositor->statFrames++;
    compositor->statPresents++;
    if (frame.kind == RoboCompositorPresentFull) {
        compositor->statPresentsFull++;
    } else {
        compositor->statPresentsDamage++;
    }
    compositor->statPixelsComposited += result.pixelsWritten;
    compositor->statBytesPresented += frame.presentedBytes;
    if (result.pixelsWritten > compositor->statCompositedPeak) {
        compositor->statCompositedPeak = result.pixelsWritten;
    }
    /* Only now is the damage consumed: a present that refused left it where it was. */
    CompositorDamageClear(compositor);
    compositor->pending = 0;
    if (compositor->vsyncInterval && !compositor->vsyncArmed) {
        /* Armed by the first present, from the time it happened: everything after that keeps the
         * boundary the clock's arithmetic put it on, so the interval does not drift with the jitter
         * of when a caller happened to pump. */
        compositor->vsyncArmed = 1;
        compositor->nextVsync = compositor->now + (CompositorTime)compositor->vsyncInterval;
    }
    compositor->vsyncDue = 0;
    if (out) {
        *out = frame;
    }
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorPumpFrame(Compositor *compositor,
                           GfxSurface *target,
                           CompositorTime now,
                           unsigned budget,
                           CompositorFrame *out,
                           CompositorDiag *diag)
{
    CompositorScheduleStats schedule;
    CompositorFrame frame;
    unsigned applied = 0u;
    int late = 0;
    int aligned = 0;
    CompositorTime deadline = 0;
    int armed;
    Result result;

    if (out) {
        CompositorFrameInit(out);
    }
    if (!CompositorIsLive(compositor) || !target) {
        return RoboInvalidArgument;
    }
    deadline = compositor->nextVsync;
    armed = compositor->vsyncArmed;
    if (now >= 0) {
        unsigned ticks = 0u;
        result = CompositorAdvanceTime(compositor, now, &ticks, diag);
        if (result != RoboOk) {
            return result;
        }
    } else {
        now = compositor->now;
    }
    if (armed && compositor->vsyncInterval) {
        /* Measured against the deadline that was in force when the pump started, which is the one the
         * frame was supposed to meet. */
        late = (now > deadline) ? 1 : 0;
        aligned = late ? 0 : 1;
    }
    if (!CompositorFrameIsDue(compositor)) {
        compositor->statFramesCoalesced++;
        return RoboBusy;
    }
    result = CompositorAnimationStep(compositor, now, &applied, diag);
    if (result != RoboOk) {
        return result;
    }
    CompositorScheduleStatsInit(&schedule);
    result = CompositorScheduleFrame(compositor, budget, &schedule, diag);
    if (result != RoboOk) {
        return result;
    }
    result = CompositorRunScheduled(compositor, &schedule, diag);
    if (result != RoboOk) {
        return result;
    }
    if (!compositor->pending && applied == 0u && CompositorDamageRectCount(compositor) == 0u &&
        !compositor->damageFull) {
        /* Nothing asked, nothing moved, nothing stale: the ordinary idle case, and not an error. */
        compositor->statFramesCoalesced++;
        return RoboBusy;
    }
    result = CompositorPresent(compositor,
                               target,
                               (compositor->config.damagePresent && !compositor->damageFull)
                                   ? RoboCompositorPresentDamage
                                   : RoboCompositorPresentFull,
                               &frame, diag);
    if (result != RoboOk) {
        return result;
    }
    frame.jobsRun = schedule.executed;
    frame.tilesRasterized = schedule.executed;
    frame.vsyncAligned = aligned;
    frame.late = late;
    compositor->lastFrame = frame;
    if (late) {
        compositor->statFramesLate++;
    }
    if (out) {
        *out = frame;
    }
    return RoboOk;
}

CompositorFrameIndex CompositorFrameCount(const Compositor *compositor)
{
    return compositor ? compositor->frameCount : 0u;
}

Result CompositorLastFrame(const Compositor *compositor, CompositorFrame *out)
{
    if (out) {
        CompositorFrameInit(out);
    }
    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    if (!compositor->hasLastFrame) {
        return RoboNotFound;
    }
    *out = compositor->lastFrame;
    return RoboOk;
}

CompositorFenceId CompositorPresentFence(const Compositor *compositor)
{
    return compositor ? compositor->fence : RoboCompositorFenceNone;
}

Result CompositorWaitFence(Compositor *compositor, CompositorFenceId fence, CompositorDiag *diag)
{
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (fence == RoboCompositorFenceNone || fence > compositor->fence) {
        /* A fence the compositor has not reached is a timeout, and a fence that never existed is not
         * found; neither blocks, because a CPU present is already done when it returns. */
        Result code = (fence == RoboCompositorFenceNone) ? RoboNotFound : RoboTimeout;
        CompositorFail(compositor, diag, code, RoboCompositorDomainFence,
                       fence == RoboCompositorFenceNone
                           ? "there is no fence zero"
                           : "that fence belongs to a present that has not happened");
        return code;
    }
    compositor->statFencesWaited++;
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Writers                                                             */
/* ------------------------------------------------------------------ */

Result CompositorWriteStats(const Compositor *compositor, char *buffer, size_t bufferSize,
                            size_t *outNeeded)
{
    CompositorDump dump;
    CompositorStats stats;

    if (!compositor) {
        return RoboInvalidArgument;
    }
    CompositorGetStats(compositor, &stats);
    CompositorDumpInit(&dump, buffer, bufferSize);
    CompositorDumpPrintf(&dump,
                         "frames=%llu coalesced=%llu late=%llu presents=%llu/%llu damage=%llu/%llu "
                         "pixels=%llu peak=%llu jobs=%llu/%llu deferred=%llu layers=%llu "
                         "posts=%llu/%llu/%llu vsync=%llu fences=%llu refusals=%llu\n",
                         (unsigned long long)stats.frames,
                         (unsigned long long)stats.framesCoalesced,
                         (unsigned long long)stats.framesLate,
                         (unsigned long long)stats.presentsDamage,
                         (unsigned long long)stats.presetsFull,
                         (unsigned long long)stats.damageAdded,
                         (unsigned long long)stats.damagePixels,
                         (unsigned long long)stats.pixelsComposited,
                         (unsigned long long)stats.compositedPixelsPeak,
                         (unsigned long long)stats.jobsRun,
                         (unsigned long long)stats.jobsScheduled,
                         (unsigned long long)stats.jobsDeferred,
                         (unsigned long long)stats.layersPainted,
                         (unsigned long long)stats.posts,
                         (unsigned long long)stats.postsDrained,
                         (unsigned long long)stats.postsCoalesced,
                         (unsigned long long)stats.vsyncTicks,
                         (unsigned long long)stats.fencesWaited,
                         (unsigned long long)stats.refusals);
    return CompositorDumpFinish(&dump, outNeeded);
}

Result CompositorWriteFrame(const Compositor *compositor,
                            CompositorFrameIndex index,
                            char *buffer,
                            size_t bufferSize,
                            size_t *outNeeded)
{
    CompositorDump dump;
    CompositorFrame frame;

    if (!compositor) {
        return RoboInvalidArgument;
    }
    if (compositor->hasLastFrame && index == compositor->lastFrame.index) {
        frame = compositor->lastFrame;
    } else {
        /* Only the last frame is retained: an index that names another one is reported as absent
         * rather than answered with a frame that is not it. */
        return RoboNotFound;
    }
    CompositorDumpInit(&dump, buffer, bufferSize);
    CompositorDumpPrintf(&dump,
                         "frame %u at %lld %s %s regions=%u damage=%llu pixels=%llu bytes=%llu "
                         "layers=%u jobs=%u tiles=%u presented=%d vsync=%d late=%d fence=%llu\n",
                         frame.index, (long long)frame.timeMs,
                         CompositorFrameReasonName(frame.reason),
                         CompositorPresentKindName(frame.kind), frame.damageRects,
                         (unsigned long long)frame.damagePixels,
                         (unsigned long long)frame.compositedPixels,
                         (unsigned long long)frame.presentedBytes, frame.layersPainted,
                         frame.jobsRun, frame.tilesRasterized, frame.presented, frame.vsyncAligned,
                         frame.late, (unsigned long long)frame.fence);
    return CompositorDumpFinish(&dump, outNeeded);
}
