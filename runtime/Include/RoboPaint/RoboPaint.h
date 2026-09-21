/*
 * RoboPaint — RoboticelaOS Runtime's paint architecture (Phase 11.1–11.8).
 *
 * Dependency:
 *   RoboBase / RoboMem → RoboGraphics → ... → RoboCSS / RoboHTML → RoboLayout
 *       → RoboPaint → RoboRender → RoboCompositor
 *
 * RoboPaint sits directly above RoboLayout and directly below RoboRender
 * (DEPENDENCIES.md): it may read a layout tree, a computed style's paint-visible
 * properties, RoboGraphics' geometry and -- since 11.4, where a background reaches a
 * surface -- RoboGraphics' CPU raster primitive, and it must not depend on render,
 * compositing, the shell, the API layer, JS, a window or a display. The boundary scan
 * (`Tools/Build.ps1 -Verify`) enforces it, and the direction that would be invisible to
 * a layering argument -- paint reaching *down* for a window to clip to -- is the one the
 * scan is really for.
 *
 * Phase 11.1 is architecture: the paint context, the operation vocabulary, the list and
 * its ownership, the geometry boundary, the clip state, the deterministic traversal and
 * the dump. And since 11.4 it paints: `robo_paint_decoration_paint` walks a display list and
 * fills every **drawing** command into a caller's RoboGraphics surface, so
 * `robo_paint_drawing_is_implemented()` answers 1 -- a box's **background** (11.4) and, since
 * 11.5, each of its **border sides** -- while `raster` stays 0, because the structural
 * commands are not drawings and a general rasteriser that consumed them is a later goal's.
 *
 * The pipeline this module is the fourth stage of, and where this release stops:
 *
 *   HTML / DOM → RoboCSS → RoboLayout → **RoboPaint** → display list →
 *   **background and border fill** → RoboGraphics → RoboWindow / display
 *
 * What this release buys the phases that follow: a tree that has been laid out can be
 * turned into an ordered list of operations, every operation carries the box it draws
 * and the two coordinate spaces it lives in, and the list is the *only* thing that has
 * to grow when a later goal adds text, images, opacity, transforms or clipping -- the
 * record already has a node, a depth, three rectangles, a clip and a colour, and a new
 * kind is one enumerator rather than a redesign.
 *
 * What it does *not* buy is a **rasteriser**: `raster` is 0 and this header says so rather
 * than leaving a caller to find out from a blank surface. What 11.4–11.5 buy is narrower and
 * precise -- the two *drawing* command kinds reach a surface -- and a caller that checked only
 * a version number would conclude that 0.5.0 renders a document, when what it renders is a
 * box's background and its four border bands (as bands: seven of CSS's ten border styles are
 * pattern-shaped inside the band and are counted rather than shaped, see
 * `RoboPaintDecoration.h`).
 *
 * Chromium's paint and display-list architecture -- `PaintLayer`, `DisplayItem`,
 * `PaintController`, `cc::DisplayItemList` -- is temporary technical reference only, and
 * nothing here is a translation of any of it. The design decisions that matter are made
 * the other way round from Chromium's, and the two worth naming are that this list
 * carries **no transform stack and no property tree** (there is nothing to inherit yet) and
 * that it carries **both coordinate spaces per operation** rather than one plus a walk. See paint/README.md and paint/docs/.
 */
#ifndef ROBO_PAINT_H
#define ROBO_PAINT_H

#include "RoboBase/RoboError.h"

#include "RoboPaint/RoboPaintClip.h"
#include "RoboPaint/RoboPaintDisplayCommand.h"
#include "RoboPaint/RoboPaintDisplayDump.h"
#include "RoboPaint/RoboPaintDisplayList.h"
#include "RoboPaint/RoboPaintDisplayListBuilder.h"
#include "RoboPaint/RoboPaintColor.h"
#include "RoboPaint/RoboPaintContext.h"
#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintDump.h"
#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboPaint/RoboPaintHitTest.h"
#include "RoboPaint/RoboPaintList.h"
#include "RoboPaint/RoboPaintOpacity.h"
#include "RoboPaint/RoboPaintOperation.h"
#include "RoboPaint/RoboPaintText.h"
#include "RoboPaint/RoboPaintTypes.h"
#include "RoboPaint/RoboPaintVersion.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

/**
 * What this build of RoboPaint actually does.
 *
 * Capabilities, not marketing: the module owns no global state to initialise, so its
 * capabilities are a statement about which *features* exist -- and four of them are
 * deliberately 0, because the point of an architecture release is that the road ahead is
 * named rather than implied.
 *
 * The fields are `int` in declaration order with no padding, which
 * `paint.conformance.capabilities` pins by walking the struct positionally against a
 * list of names -- so a field added here without an assertion is a visible omission
 * rather than a silently untested flag. That is RoboLayout's device, and it is worth
 * keeping for the same reason: `capabilities` is a struct, and a struct read
 * positionally is the one way to assert it without writing an accessor per field.
 */
typedef struct robo_paint_capabilities {
    /** The paint context and its pass record (11.1). */
    int context;
    /** The operation vocabulary and its validity rules (11.1). */
    int operation_model;
    /** The operation list, its growth, its read doors and its ownership (11.1). */
    int paint_list;
    /** The deterministic traversal that produces operations from a layout tree
     *  (11.1). */
    int traversal;
    /** Reading a box's border, padding and content boxes out of layout's result and
     *  its two inset records (11.1). A claim about the *build*: a box nothing placed
     *  has no boxes to read, and the door says so rather than answering three zeroed
     *  rectangles. */
    int box_geometry;
    /**
     * The clip **record**, accumulated per axis from `overflow` (11.1): the boundary in force
     * at a point in the traversal, carried on every operation and every command.
     *
     * Superseded as a *claim* by `clipping` below, which is about the scopes rather than about
     * the record: a build could accumulate this correctly and still have no way to say where one
     * boundary ends. It stays because the record is still what carries the boundary, and a caller
     * asking "can this build represent a clip at all?" is asking a different question from "does
     * it know which scope a drawing is inside?".
     */
    int clip_state;
    /**
     * The **decoration** operations: a background and a border side, emitted as records
     * with a colour resolved from the computed style (11.1). **1** since 11.1.
     *
     * Named for what it is rather than for what a reader might hope: this build decides
     * *that* a background will be filled and *which* colour, and records it. It does not
     * fill it. `drawing` below is the field that answers the other question, and keeping
     * the two apart is what stops "a background operation exists" from being read as "a
     * background was painted".
     */
    int decoration_ops;
    /**
     * Putting a colour on a surface. **1** since 11.4: a drawing command's rectangle and
     * colour reach a RoboGraphics surface (`robo_paint_decoration_paint`).
     *
     * The field a caller about to look for a framebuffer should read -- and the one that says
     * this build paints a box's **background**, its **border bands** and, since 11.6, a **text
     * run's glyphs**. It is `background_painting`/`border_painting`/`text_painting` beside it
     * that say *which* drawings exist, because "a colour reaches a surface" and "every command
     * rasterises" are different claims and only the first is true.
     */
    int drawing;
    /**
     * A display list as a consumable artifact (**11.3**, and **1** since then): the
     * `RoboPaintDisplayList` that resolves the box model into one rectangle per command and
     * enforces the traversal's structural convention, built from a paint list.
     *
     * **1 while `drawing` is 0**, and keeping the two apart is the point: this build
     * produces a *render-ready* list and still puts no colour on a surface. A caller
     * that read only this field would conclude the pixels are there; a caller that read
     * only `drawing` would miss that the artifact a rasteriser consumes now exists.
     */
    int display_lists;
    /** Rasterising a paint list into a RoboGraphics surface. **0**: a later goal. The three
     *  *drawing* command kinds reach a surface today -- see `background_painting`,
     *  `border_painting` and `text_painting` -- but the three structural kinds are not drawings,
     *  so a pass that consumed a whole list is still absent. */
    int raster;
    /** Knowing which operations a change made stale. **0**: Phase 11.11. What exists
     *  instead is the *input* -- `robo_paint_stats_t.style_only` -- because a paint list
     *  is regenerated rather than patched (RoboPaintContext.h). */
    int paint_invalidation;
    /**
     * **Background painting** (11.4), and **1**: a box's `background-color` reaching its
     * RoboGraphics **border box**.
     *
     * A drawing kind, and one of the two the decoration pass fills. See
     * `RoboPaintDecoration.h` for what a background command carries and why the border box is
     * the rectangle CSS means by it.
     */
    int background_painting;
    /**
     * **Border painting** (11.5), and **1**: each border side whose used width is above zero
     * filled across its **band**. One field beside `background_painting` rather than folded
     * into it, because the two arrived a phase apart and a caller is entitled to know which
     * of a box's decorations a build draws.
     *
     * It claims the **band**, in the side's own colour -- which is CSS's `solid` shape -- and
     * not the seven pattern styles: `dotted`, `dashed`, `double`, `groove`, `ridge`, `inset`
     * and `outset` are counted rather than shaped (`robo_paint_decoration_stats_t.unshaped`),
     * so "borders are painted" is true of a border's place and colour and not of every
     * border's look. `raster` above is still 0: the structural kinds are not drawings.
     */
    int border_painting;
    /**
     * **Text painting** (11.6), and **1**: a text run's glyphs reaching a RoboGraphics surface
     * through a caller-supplied face.
     *
     * The third drawing kind, and the one a caller most needs a separate field for: it is the
     * only claim here that needs a **resource** other than the surface, so a build can paint
     * every background and band of a document and no text at all -- with the opposite being
     * impossible -- and this field beside `drawing` is how a caller tells which it got.
     *
     * What it claims is a run's **glyphs on a surface**, not their look: the glyph mode
     * `RoboGraphicsText.h` implements is an outline **stroke**, so this build draws text as
     * 1-pixel outlines rather than as filled or antialiased shapes, and shaping, bidi, fallback
     * and line breaking are all still absent (`robo_paint_decoration_stats_t` counts the RTL
     * runs it drew in logical order and the code points it drew as `.notdef`).
     */
    int text_painting;
    /**
     * **Clipping** (11.8), and **1**: the boundary a document's `overflow` puts in force is
     * carried by explicit **scopes** -- a `push-clip` command standing after a box's content
     * boundary and a `pop-clip` after its last child -- the builder refuses a command inside a
     * scope that carries a different boundary, and the drawing pass bounds each drawing by the
     * scope it stands inside.
     *
     * The fourth field beside `background_painting`, `border_painting` and `text_painting`, and
     * the first of the four that is not about a *drawing*: it is about where one stops. It does
     * **not** claim rounded corners, clip paths, masks, filters, scroll containers' clipping or a
     * compositor's clip, and it does not claim `SAVE_STATE`/`RESTORE_STATE` -- the raster rebuilds
     * its clip from the command it is about to draw, so there is no graphics state for such a pair
     * to save. `clip_state` above is 11.1's record; this is what 11.8 added above it.
     */
    int clipping;
    /**
     * **Opacity** (11.9), and **1**: an element's `opacity` is accumulated along the path that
     * reaches it, carried by explicit **scopes** -- a `push-opacity` command standing after a
     * box's `begin-node` and a `pop-opacity` before its `end-node` -- and applied to every
     * drawing inside them, on top of the colour's own alpha.
     *
     * The fifth field, the second that is not about a *drawing*, and the first that is about
     * something a drawing is made *of* rather than where it is put. It does **not** claim group
     * opacity: CSS composites an element's whole subtree and applies the opacity to the flattened
     * result, and this build multiplies per drawing (`RoboPaintOpacity.h` argues the deviation in
     * full), which is exact for a subtree whose contents do not overlap. It does not claim
     * `mix-blend-mode`, filters, `backdrop-filter`, masks, transforms or a compositor layer --
     * every one of which needs the offscreen buffer the group form would need as well.
     */
    int opacity;
    /**
     * **Hit testing** (11.10), and **1**: a point in paint coordinates resolves to the layout
     * node that would be drawn there, respecting the painter's order, the boundary its `overflow`
     * puts in force and the geometry RoboLayout concluded.
     *
     * The sixth field, and the first that is not about *drawing* at all: it is the **inverse**
     * question, asked of the same tree and answered by the same records. What it claims is a
     * **node** -- a box or a text run, its geometry, and the two states in force on it -- and the
     * two things a caller must not read into it are that it delivers events (it does not; walking
     * up to the nearest element that could listen is Phase 12's) and that opacity suppresses it
     * (it does not: a node at `opacity: 0` is still the answer, because CSS governs hit testing
     * with `pointer-events` and this build has none).
     */
    int hit_testing;
    /**
     * **Image painting** (11.7), and **1**: a document's `<img>` resource reaching a
     * RoboGraphics surface at the rectangle layout gave its element.
     *
     * The seventh field, and the fourth that is about a *drawing* -- placed at the end rather
     * than beside `background_painting`, `border_painting` and `text_painting`, where a reader
     * would look for it, because the struct is asserted **positionally** by
     * `paint.conformance.capabilities` and a field inserted in the middle would renumber every
     * field after it. That is a constraint about a test rather than about the claim, and the
     * claim itself is the one a caller about to look for a picture should read.
     *
     * What it claims is deliberately narrow, and the narrowness is the phase's own limit stated
     * where a caller will find it: the resource is one the document **already resolved** (there
     * is no fetch, no URL, no network), the destination is the box's **content box** from
     * RoboLayout (so an image is as large as its CSS says, because intrinsic sizing for replaced
     * elements is layout's work and is not implemented), and what is drawn is the **full source**
     * scaled into that rectangle by the graphics module's nearest-neighbour sampler. It does not
     * claim `object-fit`, cropping, filters, animation or a broken-image presentation -- a
     * document whose image is missing, failed or unsupported simply paints nothing for it, and
     * `robo_paint_stats_t.image_skipped` says how many times that happened.
     */
    int image_painting;
} robo_paint_capabilities_t;

/** Zero @p capabilities (every feature reported absent). Safe on NULL. */
void robo_paint_capabilities_init(robo_paint_capabilities_t *capabilities);

/** Fill @p out with this build's capabilities.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p out. */
robo_result_t robo_paint_get_capabilities(robo_paint_capabilities_t *out);

/** The version this build was compiled as ("0.4.0"); never NULL. */
const char *robo_paint_version_string(void);

/**
 * The version, by number.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for any NULL output.
 */
robo_result_t robo_paint_get_version(robo_i32 *out_major,
                                     robo_i32 *out_minor,
                                     robo_i32 *out_patch);

/**
 * Non-zero when drawing exists in this build.
 *
 * A function rather than only a capability field, because it is the question a caller
 * actually asks before deciding whether to make a surface -- and because a *function*
 * cannot be read past.
 *
 * It answers **1** since 11.4, and the condition the version header wrote down for the
 * flip was exactly this: "a build in which an operation's rectangle and colour reach a
 * RoboGraphics surface". A `draw-background` command's rectangle and colour did, and since
 * 11.5 a `draw-border` command's band and colour do too, through the same pass
 * (`robo_paint_decoration_paint`). What it still does **not** mean is that every command
 * rasterises: `raster` is 0, and the pass skips the three **structural** kinds -- which draw
 * nothing by definition -- and counts them. So the two things a caller must not conclude are
 * that a structural command reaches a surface (it does not --
 * `robo_paint_decoration_stats_t.skipped` counts it) or that a whole document is a picture
 * (the list still prints with `robo_paint_list_write`).
 */
int robo_paint_drawing_is_implemented(void);

/**
 * Bring the module up.
 *
 * RoboPaint owns no global state -- no surface list, no display list cache, no
 * per-pass scratch -- so this is a state transition with nothing that can fail beyond
 * being called twice. It is here because every module in the runtime has the same
 * lifecycle, and a caller should not have to know which ones are special.
 *
 * @return ROBO_OK; ROBO_ALREADY_EXISTS when already initialised.
 */
robo_result_t robo_paint_init(void);

/** Bring the module down. Lists and contexts are the callers' and are not touched.
 *  Safe to call when not initialised. */
void robo_paint_shutdown(void);

/** Non-zero between init and shutdown. */
int robo_paint_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_H */
