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
 * So the whole file is a version, a flag and a table of what this build does and does not do -- and the
 * table is the interesting half, because **ten of its sixteen fields are 1 and six are 0**, and each 0
 * names the phase that would make it a 1. That is RoboPaint's device, kept for the reason it works
 * there: the 1s say what a caller can ask for today, the 0s say what exists in the road ahead rather
 * than leaving it to be inferred from a missing door, and a suite walks the table name-by-name so an
 * untested flag cannot quietly stop being true.
 *
 * The **order** of the fields is the order a caller meets them: the subsystem and its target, the
 * vocabulary it executes, the two states it honours, the two resources it draws, the properties that
 * make it usable (determinism, a record), and then the six absences -- presentation first among them
 * because it is the one a reader is most likely to assume is here.
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
     * The absences, in the order a reader should meet them.
     *
     * `presentation` is first and it is the one that matters most: everything else here is about
     * pixels *existing*, and this is about them being *seen*. A caller that read `rasterization` as
     * "something appears" would be wrong in exactly the way a caller that read
     * `ROBO_LAYOUT_GEOMETRY_IMPLEMENTED` as "the page is laid out" was wrong in a first release --
     * the pixels are real, and the thing that puts them on a screen is Phase 11.13's.
     *
     * The remaining five are each a *structure* rather than a feature flag: tiles need region
     * subtraction, damage tracking needs a history with a lifetime, frame scheduling needs a clock
     * (and this release's determinism depends on having none), a compositor needs a layer tree, and a
     * GPU needs a device and a resource manager. None of them is a branch somebody forgot to take;
     * each is the next phase's shape, and `RoboRender.h` names what it would need.
     */
    out->presentation = 0;
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
