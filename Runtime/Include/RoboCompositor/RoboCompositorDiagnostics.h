/*
 * Diagnostics — every view of the compositor, as text a person reads and a test diffs.
 *
 * ## One rule, applied to every door here
 *
 * **Complete or absent.** Each writer reports the length a complete line (or block) needs in
 * @p outNeeded and leaves @p buffer **empty** when it does not fit — never half a line, never a
 * truncated number. That is the rule the text, input and layout dumps follow, and it exists because
 * the alternative is a log that looks complete: a truncated `opacity 2` reads as a value rather than
 * as a failure, and the reader's next hour goes into the wrong question. A caller that wants the
 * whole thing asks once with a NULL buffer for the length.
 *
 * ## What is worth dumping
 *
 * The views are the phase's own list of things that can be wrong, one door each: the capability
 * table (what this build claims), the layer tree (where things are and what is on top), the damage
 * list (why a frame is happening), the tiles (what a frame must produce), the jobs (how it is
 * budgeted), the surfaces (what it costs), the animations (what is moving) and the frames (what it
 * did). `CompositorWriteSummary` is all of them in one buffer, which is what a shell's debug overlay
 * and a bug report both want.
 *
 * ## The last refusal, kept
 *
 * Every door that can fail may be called with a NULL `CompositorDiag` — a caller that does not care
 * should not have to pass one — and the compositor keeps the **most recent** refusal
 * (`CompositorGetLastDiag`) so a failure is still readable afterwards. One record, not a log: this
 * build has one `RoboLog` and a second ring buffer of diagnostics would be a second place for a
 * failure to be.
 *
 * ## Fuzzing
 *
 * `CompositorFuzz` drives the public doors from arbitrary bytes with one invariant: whatever the
 * bytes say, the compositor stays *valid* — every id it holds names a layer or a surface, every
 * rectangle is non-negative, the tree is acyclic, and no call reads out of range. It is the only
 * door here that is not a dump, and it lives beside them because the thing it produces is a claim
 * about the module rather than a feature of it.
 */

#ifndef ROBO_COMPOSITOR_DIAGNOSTICS_H
#define ROBO_COMPOSITOR_DIAGNOSTICS_H

#include "RoboCompositor/RoboCompositorFrame.h"
#include "RoboCompositor/RoboCompositorVersion.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The capability table, one field per line. Complete or absent. */
Result CompositorWriteCapabilities(const Compositor *compositor,
                                   char *buffer,
                                   size_t bufferSize,
                                   size_t *outNeeded);

/** One layer: `layer id parent p surface s rect [x,y,w,h] offset … opacity … scale …`. */
Result CompositorWriteLayer(const Compositor *compositor,
                            CompositorLayerId id,
                            char *buffer,
                            size_t bufferSize,
                            size_t *outNeeded);

/** Every layer in **paint order**, one per line — the tree as a frame sees it. */
Result CompositorWriteLayers(const Compositor *compositor,
                             char *buffer,
                             size_t bufferSize,
                             size_t *outNeeded);

/** The layers under @p point, topmost first, one per line (what a pointer would traverse). */
Result CompositorWriteHitTest(const Compositor *compositor,
                              GfxPoint point,
                              char *buffer,
                              size_t bufferSize,
                              size_t *outNeeded);

/** The scheduled jobs, in queue order: `job i kind tile|region surface s [x,y,w,h]`. */
Result CompositorWriteJobs(const Compositor *compositor,
                           char *buffer,
                           size_t bufferSize,
                           size_t *outNeeded);

/**
 * Everything, in sections: capabilities, layers, damage, tiles, jobs, surfaces, animations, stats.
 *
 * Each section is written by its own door, so a summary and a single-section dump can never disagree
 * about a number. Complete or absent applies to the whole block: a buffer that cannot hold every
 * section gets nothing and the length it needs.
 */
Result CompositorWriteSummary(const Compositor *compositor,
                              char *buffer,
                              size_t bufferSize,
                              size_t *outNeeded);

/** The most recent refusal, or `RoboOk` when nothing has failed. */
Result CompositorGetLastDiag(const Compositor *compositor, CompositorDiag *out);

/** Refusals counted, per domain — the table a "why is nothing drawing" question starts from. */
Result CompositorGetRefusalCounts(const Compositor *compositor,
                                  uint64_t *outCounts,
                                  size_t count);

/**
 * Check every invariant the module states, and say which one broke.
 *
 * A layer's parent exists, the roots are a list and not a cycle, a child agrees about its parent,
 * every scale is positive and finite, every layer's surface exists, no reference count is
 * impossible, and no table holds more than it has slots. `RoboOk` means the compositor is
 * consistent; anything else fills @p diag with the invariant that failed — which is what makes a
 * fuzz failure a sentence rather than an idea.
 */
Result CompositorValidate(const Compositor *compositor, CompositorDiag *diag);

/**
 * Drive the public doors from @p bytes.
 *
 * Returns the number of doors called in @p outCalls when non-NULL. The invariant is validity, not
 * meaning: the bytes choose arguments, and nothing they choose may make the compositor inconsistent
 * or make a call read out of range. A NULL @p bytes or @p compositor is `RoboInvalidArgument`.
 */
Result CompositorFuzz(Compositor *compositor,
                     const uint8_t *bytes,
                     size_t length,
                     size_t *outCalls,
                     CompositorDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_COMPOSITOR_DIAGNOSTICS_H */
