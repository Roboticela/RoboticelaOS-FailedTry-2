/*
 * RoboLayout — RoboticelaOS Runtime's layout architecture (Phase 10.1).
 *
 * Dependency:
 *   RoboBase / RoboMem → RoboGraphics (geometry only) → RoboLayout
 *   RoboLayout may also depend on RoboHTML and RoboCSS, and is the first module
 *   that may: the two are peers and must not know each other, so whoever needs
 *   both sits above both (DEPENDENCIES.md). RoboLayout does not depend on
 *   RoboWindow, RoboDisplay, RoboPaint or anything above it.
 *
 * Phase 10.1 is architecture: the layout tree, the object, the context, the
 * constraints, the result, the DOM-to-tree builder, and the DOM+computed-style
 * bridge. It computes **no geometry** -- `robo_layout_geometry_is_implemented()`
 * answers 0 and a pass leaves every result UNRESOLVED, which is a statement about
 * this build rather than a placeholder for one.
 *
 * What that buys the phases that follow: every model they extend already exists,
 * so 10.3 fills a result that already has a state and 10.11 paints an edge whose
 * width already came from 9.15. What it does *not* buy is a single coordinate, and
 * this header says so in three places rather than leaving a caller to find out.
 *
 * Phase 10.2 (RoboLayoutFormattingContext.h) adds the second thing a layout pass
 * needs before it can lay anything out: **which boxes it is coordinating, and in
 * which direction**. A box now carries two context ids -- the one it participates
 * in and the one it establishes for its contents -- and a pass classifies every
 * box into both.
 *
 * Phase 10.3 (RoboLayoutBlock.h) is the first phase that computes a coordinate.
 * A block box fills its containing block's content width, its block-level
 * children are stacked below one another from its content origin, and its height is
 * the declared one when this build can resolve it and the stacked extent of its
 * children otherwise. The containing block is the *parent box*; the formatting
 * context decides the *direction*. Margins, padding, borders, declared widths,
 * inline layout, positioning and overflow are all still absent and all still
 * belong to named later goals -- so `geometry` and `block_layout` are 1 while
 * `inline_layout` and `block_and_inline_layout` are 0.
 *
 * Chromium's LayoutObject / LayoutBox / constraint models are temporary technical
 * reference only; nothing here is a translation of them. See layout/README.md and
 * layout/docs/.
 */

#ifndef ROBO_LAYOUT_H
#define ROBO_LAYOUT_H

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutBlock.h"
#include "RoboLayout/RoboLayoutBuilder.h"
#include "RoboLayout/RoboLayoutConstraints.h"
#include "RoboLayout/RoboLayoutContext.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutDom.h"
#include "RoboLayout/RoboLayoutFormattingContext.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutObject.h"
#include "RoboLayout/RoboLayoutResult.h"
#include "RoboLayout/RoboLayoutStyle.h"
#include "RoboLayout/RoboLayoutTree.h"
#include "RoboLayout/RoboLayoutTypes.h"
#include "RoboLayout/RoboLayoutVersion.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

/**
 * What this build of RoboLayout actually does.
 *
 * Capabilities, not marketing: the module owns no state to initialise, so its
 * capabilities are a statement about which *features* exist, and two of them are
 * deliberately 0. A caller that checked only for a version number would conclude
 * that 0.1.0 lays boxes out; `geometry` is the field that says it does not.
 *
 * The fields are `int` in declaration order with no padding, which
 * `layout.conformance.capabilities` pins by walking the struct positionally
 * against a list of names -- so a field added here without an assertion is a
 * visible omission rather than a silently untested flag.
 */
typedef struct robo_layout_capabilities {
    /** The layout tree: objects, relationships, lookup (10.1). */
    int object_model;
    /** The constraints record: availability and bounds (10.1). */
    int constraints;
    /** The result record and its explicit state (10.1). */
    int result_model;
    /** The pass context: constraints, counters, user data (10.1). */
    int context;
    /** Building a layout tree from a RoboHTML document (10.1). */
    int dom_build;
    /** Reading computed styles through the DOM bridge (10.1). */
    int computed_style_link;
    /** Computing a box's geometry. **1** since 10.3: the block pass writes real
     *  positions and sizes. A statement about the *build*, not about a box -- a
     *  box the block pass did not place is still UNRESOLVED, and the result state
     *  is the per-box authority. */
    int geometry;
    /** Classifying boxes into formatting contexts (10.2). */
    int formatting_contexts;
    /** Laying the **block** direction out: containing blocks, stacking, extents
     *  (10.3). */
    int block_layout;
    /** Laying the **inline** direction out: line boxes, inline placement, the
     *  anonymous block boxes a mixed container needs. **0**: Phase 10.4. */
    int inline_layout;
    /** Computing a *line box* or a block extent. **0** until both of the two
     *  above are 1 -- and it is kept as a derived, wider question rather than
     *  retired, because a caller that wants "is layout done?" should have one field
     *  to read that cannot be mistaken for "is half of it done?". A phase that
     *  implemented one direction and flipped this one instead would have named a
     *  feature it does not have. */
    int block_and_inline_layout;
    /** Painting a box. **0**: Phase 11. */
    int painting;
} robo_layout_capabilities_t;

/** Zero @p capabilities (every feature reported absent). Safe on NULL. */
void robo_layout_capabilities_init(robo_layout_capabilities_t *capabilities);

/** Fill @p out with this build's capabilities.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p out. */
robo_result_t robo_layout_get_capabilities(robo_layout_capabilities_t *out);

/** The version this build was compiled as ("0.1.0"); never NULL. */
const char *robo_layout_version_string(void);

/**
 * The version, by number.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for any NULL output.
 */
robo_result_t robo_layout_get_version(robo_i32 *out_major,
                                      robo_i32 *out_minor,
                                      robo_i32 *out_patch);

/**
 * Non-zero when geometry computation exists in this build.
 *
 * A function rather than only a capability field, because it is the question a
 * caller actually asks before deciding whether to look at a result -- and because
 * a *function* cannot be read past.
 *
 * It answers 1 since Phase 10.3: the block direction is laid out, so a box *can*
 * have geometry. What it does **not** mean is that any particular box has one.
 * Two inputs decide that, and both are per-box rather than per-build:
 *
 *   a box the block pass did not place -- an inline-level box, or anything inside
 *   an unplaced box -- is UNRESOLVED, and reading its position is a
 *   `ROBO_NOT_FOUND` rather than a zero;
 *
 *   a pass with **no viewport** records no geometry at all, because the root box's
 *   containing block is the viewport and RoboLayout owns no window to ask. That is
 *   a documented precondition (`robo_layout_context_run`), not a silent zero.
 *
 * So this function answers "does this build compute coordinates?" and
 * `robo_layout_result_is_computed` answers "does this box have one?". A caller
 * that needs only the first has it here; a caller about to read a result must ask
 * the second, whichever way this answers.
 */
int robo_layout_geometry_is_implemented(void);

/**
 * Bring the module up.
 *
 * RoboLayout owns no global state -- no style table, no sheet slots, no window
 * list -- so this is a state transition with nothing that can fail beyond being
 * called twice. It is here because every module in the runtime has the same
 * lifecycle, and a caller should not have to know which ones are special.
 *
 * @return ROBO_OK; ROBO_ALREADY_EXISTS when already initialised.
 */
robo_result_t robo_layout_init(void);

/** Bring the module down. Owned models are the callers' and are not touched.
 *  Safe to call when not initialised. */
void robo_layout_shutdown(void);

/** Non-zero between init and shutdown. */
int robo_layout_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_H */
