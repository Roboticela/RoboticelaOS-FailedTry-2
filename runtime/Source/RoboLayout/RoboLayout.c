/*
 * RoboLayout module lifecycle, version and capabilities (Phase 10.1-10.2).
 *
 * The module owns *nothing*: every tree, context, bridge and record belongs to
 * the caller that created it. That is why init cannot fail for an allocation
 * reason, why shutdown releases no state, and why the whole file is a flag, a
 * version and a table of things this build does not do yet.
 */

#include "RoboLayout/RoboLayout.h"

#include "RoboLayout/RoboLayoutVersion.h"

#include <string.h>

static int g_layout_initialized;

void robo_layout_capabilities_init(robo_layout_capabilities_t *capabilities)
{
    if (!capabilities) {
        return;
    }
    memset(capabilities, 0, sizeof(*capabilities));
}

robo_result_t robo_layout_get_capabilities(robo_layout_capabilities_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));

    /* 10.1, and every one of these is asserted by
     * `layout.conformance.capabilities` -- by name, not by trusting this
     * file. A flag is a promise about the build, and an untested promise is one
     * that stops being true without anybody noticing. */
    out->object_model = 1;
    out->constraints = 1;
    out->result_model = 1;
    out->context = 1;
    out->dom_build = 1;
    out->computed_style_link = 1;

    /* 10.2: the pass classifies every box into a formatting context, and that
     * classification is a finished feature rather than a flag waiting to be
     * flipped -- which is why it is 1 here while `geometry` was still 0. */
    out->formatting_contexts = 1;

    /*
     * 10.3 and 10.4, and the four flags that have to be read together.
     *
     * `geometry` is 1 because this build really does compute coordinates: the pass
     * writes positions and sizes, and a caller that ran one with a viewport has boxes
     * whose results are COMPUTED. It is a statement about the *build*, not about any
     * particular box -- a box neither direction placed is still UNRESOLVED, and its
     * result state is the per-box authority that says so.
     *
     * `block_layout` and `inline_layout` are the two directions, each read from its
     * own constant so that the field and the macro cannot disagree; 10.3 shipped the
     * first at 1 and the second at 0, and 10.4 is the phase where the second's code
     * arrived and the constant with it (`robo_layout_inline_walk_contents`).
     *
     * `block_and_inline_layout` is their conjunction rather than a third constant,
     * and it is deliberately still an expression instead of a literal 1: it names the
     * wider question -- "is layout done?" -- and 10.3's version of this file argued
     * that a phase which implemented one direction and flipped *this* one would have
     * advertised a feature it does not have. Writing it as the conjunction keeps that
     * property for the next direction that is added, because the day one of the two is
     * switched off this field goes back to 0 by itself.
     *
     * `painting` stays 0: nothing here draws.
     */
    out->geometry = ROBO_LAYOUT_GEOMETRY_IMPLEMENTED;
    out->block_layout = ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED;
    out->inline_layout = ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED;
    out->block_and_inline_layout = (ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED &&
                                    ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED);

    /*
     * 10.5, and read from its own constant for the same reason the two directions are:
     * the field and the macro cannot disagree. What it reports is that this build can
     * *measure* -- a text node becomes a layout object and a run's width can come from
     * a font -- and not that any particular pass measured anything: a pass with no
     * source installed is a legitimate configuration, counts every run as *unmeasured*,
     * and lays the document out exactly as 10.4 did. That distinction is why the
     * per-pass answer lives in `robo_layout_text_stats_t` and this flag is about the
     * build.
     */
    out->text_layout = ROBO_LAYOUT_TEXT_LAYOUT_IMPLEMENTED;

    /*
     * 10.6, read from its own constant: the pass finalizes line boxes into the tree,
     * so a caller can ask what is on a line, how wide it is and where it sits. Not a
     * promise about any document -- an empty inline context has no lines by CSS's own
     * rule -- and not a promise about what a line *knows*: a baseline and a
     * font-derived extent are 10.7's and the field's own comment says so.
     */
    out->line_boxes = ROBO_LAYOUT_LINE_BOXES_IMPLEMENTED;
    out->baselines = ROBO_LAYOUT_BASELINES_IMPLEMENTED;

    /*
     * 10.8, read from its own constant like the six above it. What it reports is that
     * the sizing layer exists and that a declared px `width`/`height` reach the
     * geometry -- **not** that sizing is finished: a percentage is read, recorded as
     * `UNRESOLVED`, and filled from the containing block, and intrinsic sizing does
     * not exist. The field's own comment in RoboLayout.h states that boundary where a
     * caller reads it rather than only here.
     */
    out->sizing = ROBO_LAYOUT_SIZING_IMPLEMENTED;

    /*
     * And 10.9's, same shape: the margin layer exists, a declaration reaches a used
     * value, and a box is placed at its margin origin with its parent stacking by its
     * margin box. **Not** "margins are finished": there is no collapsing, a percentage
     * is used as zero rather than resolved, and a non-replaced inline box has no
     * margins at all, which is why `robo_layout_object_margins` refuses one.
     */
    out->margins = ROBO_LAYOUT_MARGINS_IMPLEMENTED;
    out->padding = ROBO_LAYOUT_PADDING_IMPLEMENTED;

    /*
     * And 10.11's, the last of the box model's four layers and the only one whose *used*
     * value is not its declared one: a `border-style: none` gives every side a used width of
     * zero however `border-width` was written, which is why the record carries a counter for
     * a width that resolved and a second for a border that actually draws. **Not** "borders
     * are finished": `border-radius` does not exist, `hidden`'s collapsing meaning belongs
     * to tables this build has none of, and a transparent inline box takes no border.
     */
    out->border = ROBO_LAYOUT_BORDER_IMPLEMENTED;
    /*
     * And 10.12's, the first capability in this struct that is about *where* a box is rather
     * than how big it is or what is around it. A `relative` box is displaced and nothing else
     * changes: its size, its baseline and the room flow reserved for it are all untouched.
     * **Not** "positioning is finished": `absolute`, `fixed` and `sticky` are values 9.13
     * already carries and behaviour this build does not have, no stacking context or paint
     * order moves, and a transparent inline box and a text run carry no offset.
     */
    out->positioning = ROBO_LAYOUT_POSITIONING_IMPLEMENTED;
    /*
     * And 10.13's, the first capability in this struct that is about what happens when a
     * box's contents do *not* fit. A record per measured box -- two used modes and four
     * extents beyond the padding box -- plus a region and a clip rectangle a paint pass can
     * consume, with every placed child's region folded into its parent's measurement. **Not**
     * "overflow is finished": nothing scrolls, no scrollport or scrollbar exists,
     * `scroll`/`auto`/`clip` are values this build has not added, CSS's `visible`/`auto` axis
     * promotion does not apply (it exists for scrolling), and no pixel is touched here.
     */
    out->overflow = ROBO_LAYOUT_OVERFLOW_IMPLEMENTED;
    out->painting = 0;
    return ROBO_OK;
}

const char *robo_layout_version_string(void)
{
    return ROBO_LAYOUT_VERSION_STRING;
}

robo_result_t robo_layout_get_version(robo_i32 *out_major,
                                      robo_i32 *out_minor,
                                      robo_i32 *out_patch)
{
    if (!out_major || !out_minor || !out_patch) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_major = ROBO_LAYOUT_VERSION_MAJOR;
    *out_minor = ROBO_LAYOUT_VERSION_MINOR;
    *out_patch = ROBO_LAYOUT_VERSION_PATCH;
    return ROBO_OK;
}

int robo_layout_geometry_is_implemented(void)
{
    /*
     * One macro, used by both doors, so the capability field and the function
     * cannot disagree -- which is exactly the drift a caller would have no way to
     * see.
     */
    return ROBO_LAYOUT_GEOMETRY_IMPLEMENTED;
}

robo_result_t robo_layout_init(void)
{
    if (g_layout_initialized) {
        return ROBO_ALREADY_EXISTS;
    }
    g_layout_initialized = 1;
    return ROBO_OK;
}

void robo_layout_shutdown(void)
{
    /* Nothing to release: the module owns no state. Stated rather than left as an
     * empty function body, because "there is nothing here" is the fact a reader
     * needs. */
    g_layout_initialized = 0;
}

int robo_layout_is_initialized(void)
{
    return g_layout_initialized;
}
