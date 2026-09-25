/*
 * Frames, the clock, the post queue and present fences — Phase 15.9, 15.10, 15.14, 15.15, 15.17.
 *
 * ## One frame, in one order
 *
 *     posts drained  →  animations stepped  →  damage turned into tiles  →  jobs run  →
 *     tree composited (whole, or only the damage)  →  the frame is recorded  →  the fence moves
 *
 * `CompositorPumpFrame` is that order, once, and it is the whole of 15.14's "compositor thread" in a
 * build with no second thread. What a real thread would buy is *parallelism*; what a frame pump
 * needs is *ownership*, and ownership is a rule this module can state and enforce without a second
 * stack:
 *
 *   a **device** thread may only call `CompositorPostFrame` (it enqueues a reason and a damage
 *   region and touches nothing else);
 *
 *   the **frame** runs on one thread — whichever calls `CompositorPumpFrame` or `CompositorPresent`
 *   — and is the only thread that mutates layers, surfaces, tiles or the tree.
 *
 * A compositor that is pumped from two threads is a caller bug this module cannot detect, which is
 * why the rule is written here rather than guarded: the post queue exists to make the *supported*
 * multi-threaded shape — post from anywhere, pump from one place — expressible.
 *
 * ## Vsync is a deadline on the caller's clock (15.10)
 *
 * `CompositorSetVsync(intervalMs)` makes the compositor expect frames at multiples of an interval.
 * A pump before the deadline **coalesces** rather than drawing (`framesCoalesced`), which is what
 * "don't paint faster than the panel" means when the clock is somebody else's; a pump after the
 * deadline draws and reports the frame as late (`framesLate`) rather than pretending the deadline
 * moved. Interval 0 — the default — is no vsync, and every pump presents.
 *
 * ## Present fences are the destination's pixels (15.15)
 *
 * This build has no GPU and no swap chain, so there is nothing to wait *for*: a CPU present returns
 * with the destination written, and the fence is a **number** that says which present a caller has
 * seen. That is not a fake: it is the contract the GPU build must keep (a fence is complete when the
 * pixels are somewhere the next reader can find them), stated in the one form this build can
 * honour — and the conformance suite proves the claim by reading the destination surface after a
 * present rather than trusting the return code.
 *
 * ## What a frame records
 *
 * The record is the phase's evidence: which kind of present it was, how many regions and pixels it
 * wrote, how many layers and jobs it ran, whether it was on a vsync boundary, and how much it
 * presented. A shell's frame budget, a regression and this suite's own assertions all read the same
 * fields.
 */

#ifndef ROBO_COMPOSITOR_FRAME_H
#define ROBO_COMPOSITOR_FRAME_H

#include "RoboCompositor/RoboCompositorAnimation.h"
#include "RoboCompositor/RoboCompositorDamage.h"
#include "RoboCompositor/RoboCompositorSurface.h"
#include "RoboCompositor/RoboCompositorTiles.h"
#include "RoboCompositor/RoboCompositorTypes.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One presented (or coalesced) frame. */
typedef struct CompositorFrame {
    CompositorFrameIndex index;      /* counted from 1; 0 means "no frame yet" */
    CompositorTime timeMs;
    CompositorFrameReason reason;    /* what asked for it */
    CompositorPresentKind kind;
    unsigned damageRects;
    uint64_t damagePixels;           /* the stale pixels this frame was asked about */
    uint64_t compositedPixels;       /* pixels actually written — the frame's cost */
    uint64_t presentedBytes;
    unsigned layersPainted;
    unsigned jobsRun;
    unsigned tilesRasterized;
    int presented;                   /* 0 when the frame coalesced or was deferred */
    int vsyncAligned;                /* 1 when it landed on the interval */
    int late;                        /* 1 when it landed after the deadline */
    CompositorFenceId fence;
} CompositorFrame;

void CompositorFrameInit(CompositorFrame *frame);
int CompositorFrameIsValid(const CompositorFrame *frame);
int CompositorFrameEqual(const CompositorFrame *a, const CompositorFrame *b);

/** Everything the compositor has counted, in one record. */
typedef struct CompositorStats {
    uint64_t frames;              /* presented */
    uint64_t framesCoalesced;     /* asked for, not drawn: too early, or nothing to draw */
    uint64_t framesLate;
    uint64_t presents;            /* a present call that wrote the destination */
    uint64_t presetsFull;         /* full presents */
    uint64_t presentsDamage;      /* region presents */
    uint64_t damageAdded;
    uint64_t damagePixels;
    uint64_t pixelsComposited;
    uint64_t bytesPresented;
    uint64_t compositedPixelsPeak;/* the largest single frame */
    uint64_t jobsScheduled;
    uint64_t jobsRun;
    uint64_t jobsDeferred;
    uint64_t layersPainted;
    uint64_t posts;               /* frame requests posted */
    uint64_t postsDrained;
    uint64_t postsCoalesced;
    uint64_t vsyncTicks;
    uint64_t fencesWaited;
    uint64_t refusals;            /* any door that said no, by domain */
    uint64_t refusalsByDomain[RoboCompositorDomainCount];
    unsigned layers;              /* now */
    unsigned surfaces;            /* now */
    unsigned damageRects;         /* now */
    unsigned pendingJobs;         /* now */
    unsigned activeAnimations;    /* now */
} CompositorStats;

void CompositorStatsInit(CompositorStats *stats);
Result CompositorGetStats(const Compositor *compositor, CompositorStats *out);
void CompositorResetStats(Compositor *compositor);

/* ---- The clock and vsync (15.10) ------------------------------------------------------------ */

/** The compositor's current time, in the caller's milliseconds. */
CompositorTime CompositorTimeNow(const Compositor *compositor);

/**
 * Set the compositor's clock to @p now.
 *
 * Time only ever moves forward: @p now before the current time is refused (`RoboInvalidArgument`),
 * because a clock that can go backwards makes every animation and deadline meaningless. @p outTicks,
 * when non-NULL, receives how many vsync deadlines passed.
 */
Result CompositorAdvanceTime(Compositor *compositor,
                            CompositorTime now,
                            unsigned *outTicks,
                            CompositorDiag *diag);

/** Set the vsync interval. 0 disables it (the default). Returns the interval in force. */
uint32_t CompositorSetVsync(Compositor *compositor, uint32_t intervalMs);

uint32_t CompositorVsyncInterval(const Compositor *compositor);

/** 1 when a frame may be drawn now: no vsync, or the current time reached the next deadline. */
int CompositorFrameIsDue(const Compositor *compositor);

/* ---- The post queue (15.14) ----------------------------------------------------------------- */

/**
 * Post a frame request from anywhere.
 *
 * The reason and the current damage are captured; the request is **coalesced** with an already
 * pending one (a second post before a pump adds nothing a frame does not already do), and the count
 * of coalesced posts is reported so "why did my two requests draw one frame" has an answer.
 */
Result CompositorPostFrame(Compositor *compositor,
                           CompositorFrameReason reason,
                           CompositorDiag *diag);

/** 1 when a frame has been asked for and not yet drawn. */
int CompositorFramePending(const Compositor *compositor);

/** Why the pending frame was asked for, or `RoboCompositorFrameReasonVsync` when none is pending. */
CompositorFrameReason CompositorPendingReason(const Compositor *compositor);

/**
 * Drop the pending request and report how many posts it collapses (0 or 1).
 *
 * The door a caller uses when it is tearing a scene down, and the one that makes "nothing is
 * pending" checkable rather than merely likely.
 */
unsigned CompositorDrainPosts(Compositor *compositor);

/* ---- Presenting (15.3, 15.15) --------------------------------------------------------------- */

/**
 * Composite the layer tree into @p target and record the frame.
 *
 * @p kind `Full` writes every pixel of the composition; `Damage` writes only the damaged regions
 * (and is what makes a two-pixel change cost two pixels). The composition's own size comes from the
 * compositor's configuration; @p target may be larger, and is clipped rather than resized — a
 * destination smaller than the composition refuses `RoboInvalidArgument` rather than writing part of
 * a frame and calling it a success.
 *
 * The damage list is cleared on success, the fence moves, and @p out (when non-NULL) receives the
 * record. On failure the damage is **kept**: a frame that did not happen must not consume the
 * regions that still need one.
 */
Result CompositorPresent(Compositor *compositor,
                         GfxSurface *target,
                         CompositorPresentKind kind,
                         CompositorFrame *out,
                         CompositorDiag *diag);

/**
 * One frame of the pump: posts, animations, scheduling, jobs, present.
 *
 * Returns `RoboOk` with a presented frame, or `RoboBusy` when the frame coalesced (nothing was
 * pending, no animation moved, and the damage list is empty) — a code rather than an error, because
 * "there was nothing to draw" is the ordinary case of an idle desktop. @p now advances the clock
 * first; pass -1 to leave it alone and let the caller's `CompositorAdvanceTime` be the only clock.
 *
 * @p budget caps the raster jobs this frame (0 = uncapped), which is 15.6's deferral in the place it
 * matters: a frame that runs out of budget still composites and presents, and the deferred tiles are
 * dirty for the next one.
 */
Result CompositorPumpFrame(Compositor *compositor,
                           GfxSurface *target,
                           CompositorTime now,
                           unsigned budget,
                           CompositorFrame *out,
                           CompositorDiag *diag);

/** Frames presented so far. */
CompositorFrameIndex CompositorFrameCount(const Compositor *compositor);

/** The last frame's record, or `RoboNotFound` when nothing has been presented. */
Result CompositorLastFrame(const Compositor *compositor, CompositorFrame *out);

/**
 * The fence of the last completed present.
 *
 * `RoboCompositorFenceNone` before the first present: a caller waiting on "the first frame" should
 * find that the fence does not exist rather than that it is complete.
 */
CompositorFenceId CompositorPresentFence(const Compositor *compositor);

/**
 * Wait for @p fence.
 *
 * On this build a present is synchronous, so a fence at or before the last completed present is
 * complete (`RoboOk`), a fence **ahead** of it is `RoboTimeout`, and 0 or an id past the present
 * counter is `RoboNotFound`. A wait that returns counts (`fencesWaited`), which is what a dump reads
 * to say whether anything is ever waiting.
 */
Result CompositorWaitFence(Compositor *compositor,
                          CompositorFenceId fence,
                          CompositorDiag *diag);

/* ---- Reduced motion (15.13) ----------------------------------------------------------------- */

/** Turn reduced motion on or off. On, a new animation snaps to its end value in one step. */
Result CompositorSetReducedMotion(Compositor *compositor, int on, CompositorDiag *diag);

int CompositorReducedMotion(const Compositor *compositor);

/** One line: the frame's own numbers as `frames=… pixels=… jobs=… …`. Complete or absent. */
Result CompositorWriteStats(const Compositor *compositor,
                            char *buffer,
                            size_t bufferSize,
                            size_t *outNeeded);

/** One line per presented frame: the record's fields, in the order the struct declares them. */
Result CompositorWriteFrame(const Compositor *compositor,
                            CompositorFrameIndex index,
                            char *buffer,
                            size_t bufferSize,
                            size_t *outNeeded);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_COMPOSITOR_FRAME_H */
