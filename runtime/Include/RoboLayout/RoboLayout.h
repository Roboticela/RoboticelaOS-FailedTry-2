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
 * context decides the *direction*.
 *
 * Phase 10.4 (RoboLayoutInline.h) adds the other direction to the same pass. The
 * contents of a box that establishes an **inline** formatting context are placed one
 * after another along a line, wrapping when the next participant does not fit, and a
 * box's rectangle is a *union* of what its contents reached when the box is
 * non-atomic and a declared size when it is atomic. The two directions are one pass
 * and one door, because an inline context's height *is* the block extent of the box
 * that owns it.
 *
 * Phase 10.5 (RoboLayoutText.h, RoboLayoutTextFont.h) adds the participant that is
 * neither box nor line: a **text run**. A text node generates a layout object of its
 * own -- not a box; it has no element and no `display` declaration -- it borrows its
 * parent element's computed style because every property a run needs is inherited,
 * and it is *measured* through a callback seam whose real supplier is a RoboFont face
 * asked through RoboFont's own API. A pass given no metrics source measures every run
 * as zero and counts it, which is the documented degradation rather than a failure.
 *
 * Line boxes are 10.6; margins, padding, borders, positioning and overflow are still
 * absent and still belong to named later goals -- so `geometry`, `block_layout`,
 * `inline_layout`, `block_and_inline_layout` and `text_layout` are all 1, while
 * `painting` is 0.
 *
 * Chromium's LayoutObject / LayoutBox / constraint models are temporary technical
 * reference only; nothing here is a translation of them. See layout/README.md and
 * layout/docs/.
 */

#ifndef ROBO_LAYOUT_H
#define ROBO_LAYOUT_H

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutBaseline.h"
#include "RoboLayout/RoboLayoutBlock.h"
#include "RoboLayout/RoboLayoutBuilder.h"
#include "RoboLayout/RoboLayoutConstraints.h"
#include "RoboLayout/RoboLayoutContext.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutDom.h"
#include "RoboLayout/RoboLayoutFormattingContext.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutInline.h"
#include "RoboLayout/RoboLayoutLineBox.h"
#include "RoboLayout/RoboLayoutMargin.h"
#include "RoboLayout/RoboLayoutObject.h"
#include "RoboLayout/RoboLayoutResult.h"
#include "RoboLayout/RoboLayoutSizing.h"
#include "RoboLayout/RoboLayoutStyle.h"
#include "RoboLayout/RoboLayoutText.h"
#include "RoboLayout/RoboLayoutTextFont.h"
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
    /** Laying the **inline** direction out: the participants of an inline
     *  formatting context placed along a line, wrapping, unions for transparent
     *  inline boxes and declared sizes for atomic ones. **1** since 10.4 -- and it
     *  is a claim about *positions*, not about line **boxes**: a line box has
     *  identity, alignment, a baseline and a font-derived height, and it is 10.6's.
     *  The anonymous block box a mixed container needs is still absent and is a
     *  later goal's. */
    int inline_layout;
    /** Computing a *line box* or a block extent.
     *
     * **1** since 10.4, as the conjunction of the two directions above rather than
     * as a separate literal, and kept as a derived, wider question rather than
     * retired because a caller that wants "is layout done?" should have one field to
     * read that cannot be mistaken for "is half of it done?". 10.3 wrote it as 0
     * while only one direction existed, and the derivation is what makes that phase's
     * rule -- a phase that implemented one direction and flipped *this* one instead
     * would have named a feature it does not have -- survive the next direction
     * rather than ending with this one. */
    int block_and_inline_layout;
    /** Measuring **text**: a text node generating a layout object, and a run's
     *  advance width and line extent coming from a font through a callback seam
     *  (10.5). **1** since 10.5.
     *
     *  A claim about the *build* and not about a pass, exactly as `geometry` is: a
     *  pass that was given no metrics source measures every run as zero and counts it
     *  (`robo_layout_text_stats_t.unmeasured`), which is a legitimate way to run this
     *  module — and the stats record is where that shows. What this field rules out is
     *  a build in which nothing *could* measure, which is what this module was until
     *  this phase. */
    int text_layout;
    /**
     * Producing **line boxes** a caller can read (10.6). **1** since 10.6.
     *
     * A claim about the *build* and not about a pass, exactly as `geometry` and
     * `text_layout` are: a document with no inline content produces no lines, and a
     * pass with no viewport places nothing at all -- so "are there lines?" is answered
     * by `robo_layout_tree_line_count` and this field only says that the module can
     * *produce* them. Kept narrow on purpose: a line box here has identity, geometry,
     * membership and an ordinal, and does **not** yet have a baseline, a `line-height`
     * or a font-derived extent -- `line_boxes` would be claiming those if it were
     * named for "text layout is finished".
     */
    int line_boxes;
    /**
     * Placing participants **on a baseline** (10.7). **1** since 10.7.
     *
     * A claim about the *build*, exactly as `geometry` and `text_layout` are: a
     * document with no inline content produces no lines and therefore no baselines,
     * so "where is the baseline of this line?" is `robo_layout_line_box_baseline_y`'s
     * and this field only says the module can *produce* one. Kept narrow for the same
     * reason `line_boxes` is: a baseline here is a line's floor and the placement rule
     * that follows from it, and it is **not** CSS `vertical-align`, a resolved
     * `line-height`, an inline-block baseline taken from its last in-flow line box, or
     * anything glyph-level -- `baselines` would be claiming those if it were named for
     * "text layout is finished".
     */
    int baselines;
    /**
     * Resolving a box's **width and height** from its declarations (10.8). **1**
     * since 10.8.
     *
     * A claim about the *build*, exactly as `geometry` and `text_layout` are: the
     * sizing layer exists, a declared px `width` and `height` reach the geometry a
     * block box and an atomic inline are given, and an `auto` dimension has a stated
     * answer (the containing block for a width, the contents for a height). Kept
     * narrow for the same reason the other flags are: **percentages** are read and
     * recorded as `UNRESOLVED` but not resolved against a containing dimension, and
     * intrinsic sizing -- `min-content`, `max-content`, `fit-content`,
     * shrink-to-fit -- does not exist at all. `sizing` would be claiming those if it
     * were named for "sizing is finished".
     *
     * One consequence worth stating where a caller will read it: the clamping path is
     * *live and tested* through the sizing record, and no property registered in
     * RoboCSS feeds it yet -- `min-width` and its three siblings are read through the
     * same seam as `width`, so the day the property table knows those names boxes
     * obey them with no change to either pass.
     */
    int sizing;
    /**
     * Turning a box's **margins** into the space it occupies (10.9). **1** since
     * 10.9.
     *
     * A claim about the *build*, and one sentence short enough to be checked: four
     * sides are read from a computed style, given **used** values by one of two stated
     * policies, and the results are a box placed at its margin origin, a width resolved
     * against what the margins left, and a parent stacking by a margin box. Kept narrow
     * for the same reason the other flags are: **collapsing** does not exist -- adjacent
     * vertical margins add -- percentage margins are read and used as zero, and no
     * margin applies to a non-replaced inline box. `margins` would be claiming those if
     * it were named for "margins are finished".
     */
    int margins;
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
 * It answers 1 since Phase 10.3: geometry is computed, so a box *can* have one.
 * What it does **not** mean is that any particular box has one. Two inputs decide
 * that, and both are per-box rather than per-build:
 *
 *   a box neither direction placed -- a block-level box in an inline context, an
 *   inline-level box in a block-direction one, or anything inside an unplaced box --
 *   is UNRESOLVED, and reading its position is a `ROBO_NOT_FOUND` rather than a
 *   zero;
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
