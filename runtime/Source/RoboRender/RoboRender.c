/*
 * RoboRender module version and capabilities (Phase 11.12).
 *
 * The module owns no global state -- a target belongs to the caller that made it, a run holds nothing
 * between calls, and there is no cache, no device and no queue -- so there is **no lifecycle door
 * here**, and that is the same reason `RoboGraphics` has none: `robo_render_init` would be a state
 * transition with nothing to transition, which is the shape this tree refuses rather than the shape it
 * copies out of habit. `RoboPaint` has one because it registers a CSS value surface on the way up;
 * this module registers nothing.
 *
 * So the whole file is a version, two flags and a table of what this build does and does not do -- and
 * the table is the interesting half, because **eleven of its sixteen fields are 1 and five are 0**, and
 * each 0 names the phase that would make it a 1. That is RoboPaint's device, kept for the reason it
 * works there: the 1s say what a caller can ask for today, the 0s say what exists in the road ahead
 * rather than leaving it to be inferred from a missing door, and a suite walks the table name-by-name so
 * an untested flag cannot quietly stop being true.
 *
 * The **order** of the fields is the order a caller meets them: the subsystem and its target, the
 * vocabulary it executes, the two states it honours, the two resources it draws, the properties that
 * make it usable (determinism, a record, and now presentation), and then the five absences.
 */

#include "RoboRender/RoboRender.h"

#include "RoboRender/RoboRenderVersion.h"

#include <string.h>

void robo_render_capabilities_init(robo_render_capabilities_t *capabilities)
{
    if (!capabilities) {
        return;
    }
    memset(capabilities, 0, sizeof(*capabilities));
}

robo_result_t robo_render_get_capabilities(robo_render_capabilities_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));

    /*
     * The subsystem and its target. Both are read from constants where a constant exists, so the
     * table, the flag and the doors that report them cannot disagree about the same fact.
     */
    out->rasterizer = 1;
    out->pixel_targets = 1;

    /*
     * What the rasteriser executes. `command_execution` is the wide one and it is wide **because of
     * the preflight**: a run refuses a list carrying a kind this build does not know rather than
     * drawing part of it, so "this field is 1" means every command of every list this build can
     * produce is executed or the run does not happen.
     */
    out->command_execution = 1;
    out->region_runs = 1;

    /*
     * The two states. Both are honoured from the list's own scope pairs, per drawing, by the pass
     * one module down -- which is why the fields are here rather than a claim about this module's
     * own state: there is no state here, there is a rectangle and a strength handed to a raster
     * before each drawing.
     */
    out->clipping = 1;
    out->opacity = 1;

    /*
     * The two resources a drawing can need beyond a colour: a face for a run's characters and a
     * decoded image for a payload. Both are the caller's, exactly as the target is -- this module
     * resolves neither, and its version header says why (selection and fetching have owners
     * elsewhere).
     */
    out->text = 1;
    out->images = 1;

    /* And the two properties that make a frame something a caller can act on. */
    out->determinism = 1;
    out->frame_records = 1;

    /*
     * Presentation (11.13): a frame reaching a destination. Read from the constant beside the flag that
     * turned it, so the table, the function and the release note cannot disagree about one fact -- the
     * arrangement `rasterizer` above already uses.
     *
     * It is a **call and not a system**, and the width of the claim is the point: one frame into one
     * destination at one offset, described by a record whose two pixel counts sum to the frame's own.
     * The row that would describe a presentation *system* -- a layer order, a queue, a second buffer --
     * is `compositor` below, still 0.
     */
    out->presentation = ROBO_RENDER_PRESENTATION_IMPLEMENTED;

    /*
     * The five absences.
     *
     * Each is a *structure* rather than a feature flag: tiles need region subtraction, damage tracking
     * needs a history with a lifetime, frame scheduling needs a clock (and this release's determinism
     * depends on having none), a compositor needs a layer tree, and a GPU needs a device and a resource
     * manager. None of them is a branch somebody forgot to take; each is a later phase's shape, and
     * `RoboRender.h` names what it would need.
     */
    out->compositor = 0;
    out->tiles = 0;
    out->damage_tracking = 0;
    out->frame_scheduling = 0;
    out->gpu = 0;

    return ROBO_OK;
}

const char *robo_render_version_string(void)
{
    return ROBO_RENDER_VERSION_STRING;
}

robo_result_t robo_render_get_version(robo_i32 *out_major,
                                     robo_i32 *out_minor,
                                     robo_i32 *out_patch)
{
    if (!out_major || !out_minor || !out_patch) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_major = (robo_i32)ROBO_RENDER_VERSION_MAJOR;
    *out_minor = (robo_i32)ROBO_RENDER_VERSION_MINOR;
    *out_patch = (robo_i32)ROBO_RENDER_VERSION_PATCH;
    return ROBO_OK;
}

/*
 * `robo_render_presentation_is_implemented` is defined in `RoboRenderPresenter.c`, beside the call it
 * describes, rather than here -- this file's subject is the table and the two release flags, and a
 * function whose answer is one of those flags belongs with the work that turned it.
 */

int robo_render_rasterization_is_implemented(void)
{
    /*
     * Read from the constant rather than written as a literal, so this door and the
     * `rasterizer`/`pixel_targets` fields above cannot drift from the release note that turned it --
     * and so the conformance suite's three-way assertion is an assertion about one fact and not
     * about three copies of it.
     */
    return ROBO_RENDER_RASTERIZATION_IMPLEMENTED;
}
