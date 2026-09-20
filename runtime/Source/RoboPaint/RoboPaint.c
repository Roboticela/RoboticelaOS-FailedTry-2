/*
 * RoboPaint module lifecycle, version and capabilities (Phase 11.1).
 *
 * The module owns *nothing*: every list and context belongs to the caller that created
 * it. That is why init cannot fail for an allocation reason, why shutdown releases no
 * state, and why the whole file is a flag, a version and a table of what this build does
 * and does not do.
 *
 * The table is the interesting half. Four of its eleven fields are deliberately 0, and
 * they are the four a reader would otherwise have to discover by experiment:
 * `drawing`, `display_lists`, `raster` and `paint_invalidation`. `decoration_ops` beside
 * them is 1 -- this build *does* decide that a background will be filled and which
 * colour -- and keeping the two apart is what stops "a background operation exists" from
 * being read as "a background was painted".
 */

#include "RoboPaint/RoboPaint.h"

#include "RoboPaint/RoboPaintVersion.h"

#include <string.h>

static int g_paint_initialized;

void robo_paint_capabilities_init(robo_paint_capabilities_t *capabilities)
{
    if (!capabilities) {
        return;
    }
    memset(capabilities, 0, sizeof(*capabilities));
}

robo_result_t robo_paint_get_capabilities(robo_paint_capabilities_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));

    /*
     * The three 11.1 layers, each a finished feature of this build rather than a promise:
     * the context and its pass record, the operation vocabulary with its validity rule,
     * the list with its ownership, the traversal that produces operations from a tree,
     * the geometry boundary that reads a box's three rectangles out of layout's result,
     * and the clip state.
     *
     * Every one of them is asserted by `paint.conformance.capabilities`, by name and in
     * declaration order, because a struct read positionally is the only way to assert
     * eleven `int` fields without writing eleven accessors -- and an untested capability
     * flag is one that stops being true without anybody noticing.
     */
    out->context = 1;
    out->operation_model = 1;
    out->paint_list = 1;
    out->traversal = 1;
    out->box_geometry = 1;
    out->clip_state = 1;

    /*
     * What the traversal *does* produce: backgrounds and border sides, with their colours
     * resolved from the computed style. Named for what it is rather than for what a reader
     * might hope -- see the header comment.
     */
    out->decoration_ops = 1;

    /*
     * And the four that are 0, each naming a later goal rather than an omission.
     *
     * `drawing` is read from its own constant so that the field and the two doors that
     * report it -- this one and `robo_paint_drawing_is_implemented()` -- cannot disagree.
     * `display_lists` and `raster` are the two halves of "the list reaches a surface",
     * split because they are separate pieces of work: a display list is a consumable
     * artifact and rasterising is what consumes it.
     *
     * `paint_invalidation` is 0 and what exists instead is the *input* to one:
     * `robo_paint_stats_t.style_only`. A paint list is regenerated rather than patched, so
     * there is no dirty-rect system to report here -- and a caller that read only a version
     * number would conclude otherwise.
     */
    out->drawing = ROBO_PAINT_DRAWING_IMPLEMENTED;
    out->display_lists = 0;
    out->raster = 0;
    out->paint_invalidation = 0;
    return ROBO_OK;
}

const char *robo_paint_version_string(void)
{
    return ROBO_PAINT_VERSION_STRING;
}

robo_result_t robo_paint_get_version(robo_i32 *out_major,
                                     robo_i32 *out_minor,
                                     robo_i32 *out_patch)
{
    if (!out_major || !out_minor || !out_patch) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_major = ROBO_PAINT_VERSION_MAJOR;
    *out_minor = ROBO_PAINT_VERSION_MINOR;
    *out_patch = ROBO_PAINT_VERSION_PATCH;
    return ROBO_OK;
}

int robo_paint_drawing_is_implemented(void)
{
    /*
     * One macro, read by both this function and the capability field, so the two cannot
     * disagree -- which is exactly the drift a caller would have no way to see. It answers
     * 0 and will keep answering 0 for at least one more goal: what this release produces
     * is a *description* of what would be drawn.
     */
    return ROBO_PAINT_DRAWING_IMPLEMENTED;
}

robo_result_t robo_paint_init(void)
{
    if (g_paint_initialized) {
        return ROBO_ALREADY_EXISTS;
    }
    g_paint_initialized = 1;
    return ROBO_OK;
}

void robo_paint_shutdown(void)
{
    /* Nothing to release: the module owns no state. Stated rather than left as an empty
     * function body, because "there is nothing here" is the fact a reader needs. */
    g_paint_initialized = 0;
}

int robo_paint_is_initialized(void)
{
    return g_paint_initialized;
}
