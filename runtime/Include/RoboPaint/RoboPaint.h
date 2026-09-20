/*
 * RoboPaint — RoboticelaOS Runtime's paint architecture (Phase 11.1).
 *
 * Dependency:
 *   RoboBase / RoboMem → RoboGraphics → ... → RoboCSS / RoboHTML → RoboLayout
 *       → RoboPaint → RoboRender → RoboCompositor
 *
 * RoboPaint sits directly above RoboLayout and directly below RoboRender
 * (DEPENDENCIES.md): it may read a layout tree, a computed style's paint-visible
 * properties and RoboGraphics' geometry, and it must not depend on render, compositing,
 * the shell, the API layer, JS, a window or a display. The boundary scan
 * (`Tools/Build.ps1 -Verify`) enforces it, and the direction that would be invisible to
 * a layering argument -- paint reaching *down* for a window to clip to -- is the one the
 * scan is really for.
 *
 * Phase 11.1 is architecture: the paint context, the operation vocabulary, the list and
 * its ownership, the geometry boundary, the clip state, the deterministic traversal and
 * the dump. It draws **no pixel** -- `robo_paint_drawing_is_implemented()` answers 0 and
 * the `drawing` capability field with it -- and that is a statement about this build
 * rather than a placeholder for one.
 *
 * The pipeline this module is the fourth stage of, and where this release stops:
 *
 *   HTML / DOM → RoboCSS → RoboLayout → **RoboPaint** → display list → raster →
 *   RoboGraphics → RoboWindow / display
 *
 * What this release buys the phases that follow: a tree that has been laid out can be
 * turned into an ordered list of operations, every operation carries the box it draws
 * and the two coordinate spaces it lives in, and the list is the *only* thing that has
 * to grow when a later goal adds text, images, opacity, transforms or clipping -- the
 * record already has a node, a depth, three rectangles, a clip and a colour, and a new
 * kind is one enumerator rather than a redesign.
 *
 * What it does *not* buy is a rasteriser, and this header says so in three places rather
 * than leaving a caller to find out: the constant is 0, the function answers 0, and the
 * capability field says `drawing` is absent. A caller that checked only a version number
 * would conclude that 0.3.0 paints; the field is the thing that says it does not.
 *
 * Chromium's paint and display-list architecture -- `PaintLayer`, `DisplayItem`,
 * `PaintController`, `cc::DisplayItemList` -- is temporary technical reference only, and
 * nothing here is a translation of any of it. The design decisions that matter are made
 * the other way round from Chromium's, and the two worth naming are that this list
 * carries **no transform stack and no property tree** (there is nothing to inherit,
 * because 11.1 paints nothing) and that it carries **both coordinate spaces per
 * operation** rather than one plus a walk. See paint/README.md and paint/docs/.
 */
#ifndef ROBO_PAINT_H
#define ROBO_PAINT_H

#include "RoboBase/RoboError.h"

#include "RoboPaint/RoboPaintDisplayCommand.h"
#include "RoboPaint/RoboPaintDisplayDump.h"
#include "RoboPaint/RoboPaintDisplayList.h"
#include "RoboPaint/RoboPaintDisplayListBuilder.h"
#include "RoboPaint/RoboPaintContext.h"
#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintDump.h"
#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboPaint/RoboPaintList.h"
#include "RoboPaint/RoboPaintOperation.h"
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
    /** The clip in force, accumulated per axis from `overflow` (11.1). A claim about
     *  the *build* and not about clipping a pixel: the record says which axis is
     *  bounded and where, and nothing rasterises anything. */
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
     * Putting a colour on a surface. **0**: a later Phase 11 goal, and the field a
     * caller who is about to look for a framebuffer should read.
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
    /** Rasterising a paint list into a RoboGraphics surface. **0**: a later goal. */
    int raster;
    /** Knowing which operations a change made stale. **0**: Phase 11.11. What exists
     *  instead is the *input* -- `robo_paint_stats_t.style_only` -- because a paint list
     *  is regenerated rather than patched (RoboPaintContext.h). */
    int paint_invalidation;
} robo_paint_capabilities_t;

/** Zero @p capabilities (every feature reported absent). Safe on NULL. */
void robo_paint_capabilities_init(robo_paint_capabilities_t *capabilities);

/** Fill @p out with this build's capabilities.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p out. */
robo_result_t robo_paint_get_capabilities(robo_paint_capabilities_t *out);

/** The version this build was compiled as ("0.3.0"); never NULL. */
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
 * It answers **0**, and unlike RoboLayout's geometry door this one will keep answering 0
 * for at least one more goal: what this release produces is a *description* of what
 * would be drawn. So a caller that has a list and no rasteriser is holding exactly what
 * 11.1 promises, and the two things it must not conclude are that the list is empty
 * (it is not -- `robo_paint_list_count` answers that) or that the list is a picture (it
 * is not -- `robo_paint_list_write` prints it, and
 * `robo_paint_operation_local_boxes` is how a consumer checks it against layout).
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
