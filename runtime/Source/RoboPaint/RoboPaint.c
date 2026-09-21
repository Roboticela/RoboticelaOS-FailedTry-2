/*
 * RoboPaint module lifecycle, version and capabilities (Phase 11.1).
 *
 * The module owns *nothing*: every list and context belongs to the caller that created
 * it. That is why init cannot fail for an allocation reason, why shutdown releases no
 * state, and why the whole file is a flag, a version and a table of what this build does
 * and does not do.
 *
 * The table is the interesting half. **Every field is 1 as of 0.12.0**, and the last one to turn
 * was `raster` -- in 11.12, and the flip is a statement about the module *above* this one rather
 * than about this file: `RoboRender` executes a whole display list into an owned pixel target, so
 * "a caller with a list can obtain pixels, today" is now true of the pair. What this module gained
 * for it is one door, the **bounded** walk (`robo_paint_decoration_paint_bounded`), which is what
 * makes a region run equal to a full frame *inside* the region instead of merely close to it.
 * Before that, `paint_invalidation` became 1 when this module could answer which regions a change
 * made stale (`RoboPaintInvalidation.h`), and that is the shape the whole capability model is built
 * to keep readable: the fields say what a caller can ask for, and a field that turned early is a
 * claim with a fence beside it rather than a hope.
 * `display_lists` joined the 1s in 11.3 -- the artifact a rasteriser consumes now exists -- and
 * `drawing`, `background_painting`, `border_painting` (11.5) and `text_painting` (11.6) joined
 * them in 11.4: a **drawing** command's rectangle and colour -- and, for a run, its glyphs --
 * now reach a RoboGraphics surface. Keeping the narrow fields apart from `raster` is what stops
 * "a colour reached a surface" from being read as "the document rasterises": the painter
 * consumes the drawing kinds and skips the structural ones, and text beside them is the one
 * claim here that needs a resource other than the surface.
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
     * twelve `int` fields without writing twelve accessors -- and an untested capability
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
     * The drawing claim, read from its own constant so that the field and the two doors that
     * report it -- this one and `robo_paint_drawing_is_implemented()` -- cannot disagree.
     */
    out->drawing = ROBO_PAINT_DRAWING_IMPLEMENTED;
    /*
     * **1 since 11.3**: a display list exists as a consumable artifact -- the
     * `RoboPaintDisplayList` the builder produces from a paint list, with the box model
     * resolved and the traversal's structural convention enforced.
     */
    out->display_lists = 1;
    /*
     * **1 since 11.12**, and the flip is a statement about the module above rather than about
     * this one -- `RoboRender` is the subsystem that executes a display list into an owned pixel
     * target, with a frame record, byte-determinism and region execution
     * (`RoboRenderRasterizer.h`, `render/docs/Rasterization.md`). What this module owns is what
     * the list *means*: the walk, the two scope stacks, and the boundary and strength each
     * drawing is executed under -- which is why the bounded door beside the walk is what 11.12
     * actually added here, and why a caller reading this field should read `RoboRender.h`'s
     * capability table for the width of the claim (a rasteriser's harder senses -- tiles, damage
     * tracking, a frame scheduler, a compositor, a GPU -- are each a named 0 there).
     */
    out->raster = 1;
    /*
     * **1 since 11.11**, and the first of the 11.1 fields whose name the first release left with
     * nothing behind it to turn. What it reports is `RoboPaintInvalidation.h`: a set of regions,
     * each with its reason, its boundary and a conservative-whole-extent flag -- filled by
     * explicit marks, by comparing two paint artifacts, or by reading `style_only` above.
     *
     * It is *not* a claim about a display list being patched: the list is regenerated, and the
     * set says how little of it a caller *may* re-execute -- which is the distinction a caller
     * deciding between "repaint these rectangles" and "re-execute the whole list" has to read.
     * As of 11.12 both of those are things this build can do.
     */
    out->paint_invalidation = 1;

    /*
     * **1 since 11.4**, and the first of the narrow fields beside `drawing`: this build fills a
     * box's background. The claim is about the *drawing kind*, not about the list -- a background
     * command's rectangle and colour reach a surface, and the three structural kinds draw nothing
     * and are counted.
     */
    out->background_painting = 1;
    /*
     * **1 since 11.5**, the second drawing and the first with a shape: each border side whose
     * used width is above zero is filled across its band, in the side's colour. It claims the
     * band and not the pattern inside it -- seven of CSS's ten styles are counted
     * (`unshaped`) -- which is why this is a field beside `background_painting` rather than a
     * claim about `raster`.
     */
    out->border_painting = 1;
    /*
     * **1 since 11.6**, the third drawing kind and the first whose payload is not a number: a
     * text run's characters are resolved to glyphs through a **caller-supplied face** and drawn
     * through RoboGraphics' outline-stroke glyph path. One field of its own rather than folded
     * into `drawing`, because this is the only drawing here that needs a resource other than the
     * surface -- so a build can paint every background and band of a document and none of its
     * text, and only this field beside `drawing` tells a caller which it got.
     */
    out->text_painting = 1;
    /*
     * **1 since 11.8**, and the first field in this struct that bounds a drawing rather than
     * producing one: the boundary the document's `overflow` puts in force is now a **scope the
     * display list states** -- a `push-clip` before a box's contents and a `pop-clip` after them
     * -- the builder refuses any command inside a scope carrying a different boundary, and the
     * drawing pass bounds each drawing by the scope it is inside (falling back to the command's
     * own clip for a list that carries no scopes).
     *
     * `clip_state` above is the field that said the *record* existed (11.1); this one says the
     * scopes do, and the two are separate claims on purpose: a build can accumulate a clip
     * correctly, carry it on every command, bound each fill by it and still have no idea which
     * boundary a consumer should restore after a nested box. That gap is what this phase closed.
     *
     * It does **not** claim rounded corners, clip paths, masks, filters, scroll containers, tile
     * clipping or a compositor's clip, and it does not claim `SAVE_STATE`/`RESTORE_STATE` -- the
     * raster rebuilds its clip per drawing, so there is no graphics state for such a pair to
     * save (RoboPaintDisplayCommand.h argues it).
     */
    out->clipping = 1;
    /*
     * **1 since 11.9**, and the second field that bounds a drawing rather than producing one --
     * the first that bounds it in *strength* rather than in space. An element's `opacity` is
     * accumulated along the path that reaches it, the display list states the accumulation as a
     * **scope** (`push-opacity` after a box's marker, `pop-opacity` before its end marker), the
     * builder refuses any command inside a scope carrying a different strength and any scope
     * that would raise it, and the drawing pass sets the raster's own global opacity per drawing
     * from the scope it is inside.
     *
     * `clip_state` above is 11.1's record and `clipping` is 11.8's scopes; this field is the same
     * pair of claims for a second state, and they are again separate on purpose. What it does
     * **not** claim is the group compositing CSS specifies: this build multiplies the effective
     * opacity per drawing, which is exact for a subtree whose contents do not overlap and
     * differs from group opacity where they do (`RoboPaintOpacity.h` states the deviation and
     * what would replace it -- a flag and a layer id on the record). Nor does it claim
     * `mix-blend-mode`, filters, `backdrop-filter`, masks, transforms or a compositor layer.
     */
    out->opacity = 1;
    /*
     * **1 from 11.10**, and the first field here that is not about *drawing* at all. Every field
     * above answers a question about the sequence this module produces; this one says the module
     * can also be asked the **inverse** question -- given a point, which node is drawn there --
     * and that the answer respects the same four rules the sequence does: reverse paint order,
     * the boundary a box's `overflow` puts in force for its contents, the strength in force, and
     * the geometry RoboLayout concluded rather than recomputed.
     *
     * It is a field beside `raster` rather than a claim about it: a hit test is not a drawing and
     * does not need one to exist. What it does **not** claim is event dispatch (the result names
     * one node; walking up to the nearest element that could listen is Phase 12's), caret or
     * character mapping inside a run, rounded or clipped shapes, `visibility`, scrolling, zoom or
     * device-pixel-ratio handling, transforms, or any part of `pointer-events` -- this build has
     * none of those, so hit testing is over the rectangles the painter draws and nothing else.
     *
     * The one rule worth reading twice, because it is the opposite of what a reader expects: an
     * element at `opacity: 0` **is** hit-testable. CSS governs that with `pointer-events`, not
     * with `opacity`, and this build has no `pointer-events` -- so the property's initial value
     * applies and the strength travels in the result instead, where a caller that one day does
     * implement the property will find the number already rather than having to add it.
     */
    out->hit_testing = 1;
    /*
     * **1 since 11.7**, and the fourth drawing kind -- placed here rather than beside the other
     * three because the struct is read positionally by its conformance suite, which is a
     * constraint about a test and not about the claim (`RoboPaint.h` says so at the field).
     *
     * What it claims is an image **resource** reaching the surface: the record layer carries a
     * retained `robo_gfx_image_t`, the display command resolves the destination rectangle from
     * the box's content box, and the drawing pass hands both to
     * `robo_gfx_raster_draw_image`. It is the second field here whose payload is a resource
     * rather than a number -- `text_painting` is the first -- and the two are separate for the
     * same reason: a build can paint every glyph of a document and none of its pictures, and
     * only the pair tells a caller which it got.
     *
     * It does **not** claim fetching, decoding, intrinsic sizing, `object-fit`, cropping,
     * animation, `srcset` or a broken-image presentation. Those are absent by design: the
     * resource arrives already decoded (`RoboHTML`'s model owns the fetch seam), the destination
     * is what RoboLayout concluded, and an image that is missing draws nothing and is counted.
     */
    out->image_painting = 1;
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
     * **1** since 11.4, where the condition the version header wrote down for the flip was
     * met: a `draw-background` command's rectangle and colour reach a RoboGraphics surface.
     *
     * It does not mean a whole list is a frame -- `robo_render_rasterize` is the door for that --
     * so a caller must not read
     * it as "the document renders". What it renders is the backgrounds.
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
