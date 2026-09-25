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
 * bridge. It computes **no geometry** -- `LayoutGeometryIsImplemented()`
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
 * Line boxes are 10.6, baselines 10.7, sizing 10.8, margins 10.9, padding 10.10,
 * borders 10.11 -- the last three being the box model complete, inset by inset -- and
 * 10.12 (RoboLayoutPosition.h) is the first phase whose number is a **displacement**
 * rather than an extent: a `relative` box is moved by an offset while its size and its
 * flow contribution are untouched, so a result's `position` is where the box is *drawn*
 * and the object keeps the offset that turns it back into where flow put it. `absolute`,
 * `fixed` and `sticky` are values 9.13 already carries and behaviour this build does not
 * have.
 *
 * Phase 10.13 (RoboLayoutOverflow.h) is the first phase that admits the contents may not
 * **fit**. An overflow is not a size -- the box keeps the size its declarations and its
 * contents asked for -- so what the phase adds is a second rectangle *beside* the box: four
 * extents saying how far the contents reached beyond the padding box, two modes saying which
 * axis has a boundary, and a region and a clip rectangle a paint pass can consume. Every
 * placed child contributes its own region to its parent's measurement, which is the phase's
 * whole recursion, and a child that hides an axis contributes only what it kept. Scrolling
 * does not exist and is not claimed: the phase exposes the boundary and nothing rasterises
 * anything -- so `geometry`, `blockLayout`, `inlineLayout`, `blockAndInlineLayout`,
 * `textLayout`, `lineBoxes`, `baselines`, `sizing`, `margins`, `padding`, `border`,
 * `positioning`, `overflow` and -- since 10.14 (RoboLayoutInvalidation.h) -- `invalidation`
 * are all 1, while `painting` is 0.
 *
 * Phase 10.14 is the first whose subject is **not a box's geometry but its truth**: every
 * phase before it made an answer better, and this one makes an answer *expire*. A box now
 * carries a state saying what about it is stale, a property name is classified into what
 * changing it costs, and a pass can be skipped when nothing has. Nothing about any
 * coordinate changes -- the pass still recomputes from the root, because the brief that
 * asks for invalidation also forbids the incremental recursion that would make it
 * faster -- so what the phase adds is a model that can *say* what is stale, the one
 * decision built on it, and the accounting that proves the two agree.
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
#include "RoboLayout/RoboLayoutBorder.h"
#include "RoboLayout/RoboLayoutBuilder.h"
#include "RoboLayout/RoboLayoutConstraints.h"
#include "RoboLayout/RoboLayoutContext.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutDom.h"
#include "RoboLayout/RoboLayoutFormattingContext.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutInline.h"
#include "RoboLayout/RoboLayoutInvalidation.h"
#include "RoboLayout/RoboLayoutLineBox.h"
#include "RoboLayout/RoboLayoutMargin.h"
#include "RoboLayout/RoboLayoutOverflow.h"
#include "RoboLayout/RoboLayoutPadding.h"
#include "RoboLayout/RoboLayoutPosition.h"
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
typedef struct LayoutCapabilities {
    /** The layout tree: objects, relationships, lookup (10.1). */
    int objectModel;
    /** The constraints record: availability and bounds (10.1). */
    int constraints;
    /** The result record and its explicit state (10.1). */
    int resultModel;
    /** The pass context: constraints, counters, user data (10.1). */
    int context;
    /** Building a layout tree from a RoboHTML document (10.1). */
    int domBuild;
    /** Reading computed styles through the DOM bridge (10.1). */
    int computedStyleLink;
    /** Computing a box's geometry. **1** since 10.3: the block pass writes real
     *  positions and sizes. A statement about the *build*, not about a box -- a
     *  box the block pass did not place is still UNRESOLVED, and the result state
     *  is the per-box authority. */
    int geometry;
    /** Classifying boxes into formatting contexts (10.2). */
    int formattingContexts;
    /** Laying the **block** direction out: containing blocks, stacking, extents
     *  (10.3). */
    int blockLayout;
    /** Laying the **inline** direction out: the participants of an inline
     *  formatting context placed along a line, wrapping, unions for transparent
     *  inline boxes and declared sizes for atomic ones. **1** since 10.4 -- and it
     *  is a claim about *positions*, not about line **boxes**: a line box has
     *  identity, alignment, a baseline and a font-derived height, and it is 10.6's.
     *  The anonymous block box a mixed container needs is still absent and is a
     *  later goal's. */
    int inlineLayout;
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
    int blockAndInlineLayout;
    /** Measuring **text**: a text node generating a layout object, and a run's
     *  advance width and line extent coming from a font through a callback seam
     *  (10.5). **1** since 10.5.
     *
     *  A claim about the *build* and not about a pass, exactly as `geometry` is: a
     *  pass that was given no metrics source measures every run as zero and counts it
     *  (`LayoutTextStats.unmeasured`), which is a legitimate way to run this
     *  module — and the stats record is where that shows. What this field rules out is
     *  a build in which nothing *could* measure, which is what this module was until
     *  this phase. */
    int textLayout;
    /**
     * Producing **line boxes** a caller can read (10.6). **1** since 10.6.
     *
     * A claim about the *build* and not about a pass, exactly as `geometry` and
     * `textLayout` are: a document with no inline content produces no lines, and a
     * pass with no viewport places nothing at all -- so "are there lines?" is answered
     * by `LayoutTreeLineCount` and this field only says that the module can
     * *produce* them. Kept narrow on purpose: a line box here has identity, geometry,
     * membership and an ordinal, and does **not** yet have a baseline, a `line-height`
     * or a font-derived extent -- `lineBoxes` would be claiming those if it were
     * named for "text layout is finished".
     */
    int lineBoxes;
    /**
     * Placing participants **on a baseline** (10.7). **1** since 10.7.
     *
     * A claim about the *build*, exactly as `geometry` and `textLayout` are: a
     * document with no inline content produces no lines and therefore no baselines,
     * so "where is the baseline of this line?" is `LayoutLineBoxBaselineY`'s
     * and this field only says the module can *produce* one. Kept narrow for the same
     * reason `lineBoxes` is: a baseline here is a line's floor and the placement rule
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
     * A claim about the *build*, exactly as `geometry` and `textLayout` are: the
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
    /**
     * Turning a box's **padding** into the size it occupies and the room its contents
     * get (10.10). **1** since 10.10.
     *
     * A claim about the *build*, and one sentence short enough to be checked: four
     * non-negative sides are read from a computed style, given used values by one stated
     * policy, and the results are a box whose border box -- what a result's `size`
     * carries -- is its content box plus the padding, whose content begins
     * `padding-left`/`padding-top` inside its border edges, and whose `auto` width fills
     * what the margins and the padding left. Kept narrow for the same reason the other
     * flags are: **`box-sizing: border-box` does not exist** (this build is
     * `content-box`, CSS's initial value), percentage padding is read and used as zero,
     * and no padding applies to a transparent inline box. `padding` would be claiming
     * those if it were named for "padding is finished".
     */
    int padding;
    /**
     * Turning a box's **border** into the size it occupies, the room its contents get, and
     * the record a paint pass draws an edge from (10.11). **1** since 10.11.
     *
     * A claim about the *build*, and one sentence short enough to be checked: twelve
     * longhands are read from a computed style, each side's used width is zero unless its
     * `border-style` generates a border (CSS's own rule), and the results are a box whose
     * border box -- what a result's `size` carries -- is its padding box plus the border,
     * whose padding box is resolved by subtracting the borders from the containing block,
     * and whose whole record -- used width, style and colour per side -- a paint pass reads
     * without re-reading a declaration. Kept narrow for the same reason the other flags are:
     * **`border-radius` does not exist**, `hidden`'s border-collapsing meaning does not, and
     * no border applies to a transparent inline box. `border` would be claiming those if it
     * were named for "borders are finished".
     */
    int border;
    /**
     * Turning a box's **`position`** into a displacement (10.12). **1** since 10.12.
     *
     * A claim about the *build*, and one sentence short enough to be checked: the mode and
     * the four insets are read from a computed style, resolved to one displacement by a
     * stated precedence rule (`left` over `right`, `top` over `bottom`, an `auto` saying
     * nothing), and applied so that a `relative` box is drawn where its offset puts it
     * while its size and its flow contribution are untouched -- so the following sibling
     * starts exactly where it always would have. Kept narrow for the same reason the other
     * flags are: **`sticky` takes no offset here** (in flow, unconstrained, because there is no
     * scrollport), `absolute` and `fixed` belong to `outOfFlow` below rather than to this flag,
     * no stacking context or paint order moves, and no percentage inset resolves. `positioning`
     * would be claiming those if it were named for "positioning is finished".
     */
    int positioning;
    /**
     * Placing a box **out of flow** against a containing block (14.0). **1** since 14.0.
     *
     * A claim about the *build*, and one sentence short enough to be checked: an `absolute` or
     * `fixed` box is taken out of flow, its containing block is the nearest positioned
     * ancestor's padding box (or the initial containing block, which every `fixed` box gets),
     * and both axes are resolved by one policy -- the near side wins, the far side positions the
     * axis where the near side says nothing, and two opposing insets size the axis between them
     * when the declared dimension is `auto`.
     *
     * Kept narrow for the same reason the other flags are: **nothing is reordered**. `z-index` is
     * not read, no stacking context exists, paint order is still document order, `sticky` is
     * still in flow and takes no offset, there is no scrollport, no percentage inset resolves, no
     * `box-sizing: border-box`, and an out-of-flow *inline-level* box is still placed on its line
     * by the inline pass. `outOfFlow` would be claiming those if it were named for "positioning is
     * finished" -- this flag says where a box goes, not what is painted over what.
     */
    int outOfFlow;
    /**
     * Measuring what happens when contents do **not** fit: the two overflow axes, the four
     * extents a box's contents reached beyond its padding box, the region they occupy and
     * the rectangle a paint pass would clip to (10.13). **1** since 10.13.
     *
     * A claim about the *build*, and one sentence short enough to be checked: the two
     * `overflow-*` longhands and the `overflow` shorthand are read from a computed style
     * into a mode per axis, every placed child contributes its **region** to its parent's
     * measurement (clipped on an axis the child hides), and the results are a record on
     * every measured box and two flags on its result. Kept narrow for the same reason the
     * other flags are: **nothing scrolls** -- there is no scroll offset, no scrollport and
     * no scrollbar, `scroll`/`auto`/`clip` are values this build has not added, and CSS's
     * `visible`/`auto` axis promotion does not apply because it exists for scrolling. The
     * phase *exposes* the clipping boundary; it does not clip. `overflow` would be claiming
     * those if it were named for "overflow is finished".
     */
    int overflow;
    /**
     * Tracking which conclusions are still true, and skipping the recomputation of
     * the ones that are (10.14). **1** since 10.14.
     *
     * A claim about the *build*, and one sentence short enough to be checked: a box
     * carries an invalidation record, a style change is classified into "layout does
     * not read it", "paint-visible only" or "feeds geometry", the three marking
     * policies propagate to ancestors and to the following flow, and
     * `LayoutContextRunIncremental` skips a pass when nothing needs one. Kept
     * narrow for the same reason the other flags are: **no partial layout exists** --
     * the dirty root is computed and reported (`LayoutTreeInvalidationRoot`),
     * and the pass still recomputes from the root, because a dirty region a build
     * guesses is a wrong answer that happens to be fast. `invalidation` would be
     * claiming that if it were named for "invalidation is finished".
     */
    int invalidation;
    /**
     * Resolving a CSS `font-family` to a face (12.17). **1** since 12.17.
     *
     * A claim about the *build* and not about a pass, exactly as `textLayout` is: a caller with one
     * system face and no provider installs nothing and every run is measured with that face, which is
     * the pre-12.17 behaviour and still correct -- `LayoutTextFontSetProvider` is what changes it. Kept
     * narrow for the same reason the other flags are: **no font database**. RoboLayout owns the
     * *request* and the *seam*; the face a family resolves to is the provider's answer, and there is no
     * coverage walk, no fallback chain, no `@font-face` and no per-script run splitting in this module.
     * `font-request` would be claiming those if it were named for "fonts are finished".
     */
    int fontRequest;
    /** Painting a box. **0**: Phase 11. */
    int painting;
} LayoutCapabilities;

/** Zero @p capabilities (every feature reported absent). Safe on NULL. */
void LayoutCapabilitiesInit(LayoutCapabilities *capabilities);

/** Fill @p out with this build's capabilities.
 *  @return RoboOk; RoboInvalidArgument for NULL @p out. */
Result LayoutGetCapabilities(LayoutCapabilities *out);

/** The version this build was compiled as ("0.1.0"); never NULL. */
const char *LayoutVersionString(void);

/**
 * The version, by number.
 * @return RoboOk; RoboInvalidArgument for any NULL output.
 */
Result LayoutGetVersion(I32 *outMajor,
                                      I32 *outMinor,
                                      I32 *outPatch);

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
 *   is UNRESOLVED, and reading its position is a `RoboNotFound` rather than a
 *   zero;
 *
 *   a pass with **no viewport** records no geometry at all, because the root box's
 *   containing block is the viewport and RoboLayout owns no window to ask. That is
 *   a documented precondition (`LayoutContextRun`), not a silent zero.
 *
 * So this function answers "does this build compute coordinates?" and
 * `LayoutResultIsComputed` answers "does this box have one?". A caller
 * that needs only the first has it here; a caller about to read a result must ask
 * the second, whichever way this answers.
 */
int LayoutGeometryIsImplemented(void);

/**
 * Bring the module up.
 *
 * RoboLayout owns no global state -- no style table, no sheet slots, no window
 * list -- so this is a state transition with nothing that can fail beyond being
 * called twice. It is here because every module in the runtime has the same
 * lifecycle, and a caller should not have to know which ones are special.
 *
 * @return RoboOk; RoboAlreadyExists when already initialised.
 */
Result LayoutInit(void);

/** Bring the module down. Owned models are the callers' and are not touched.
 *  Safe to call when not initialised. */
void LayoutShutdown(void);

/** Non-zero between init and shutdown. */
int LayoutIsInitialized(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_H */
