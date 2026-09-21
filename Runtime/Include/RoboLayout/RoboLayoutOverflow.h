/*
 * RoboLayout overflow (Phase 10.13).
 *
 * The box model is complete -- a box has margins, a border, a padding and content --
 * and every phase up to 10.12 assumed the contents *fit*. This phase is the first
 * that admits they may not, and its whole shape follows from one decision about what
 * an overflow **is**:
 *
 *   an overflow is **not** a size. The box is the size its declarations and its
 *   contents asked for, and it stays that size; what this phase adds is a second
 *   rectangle -- the **region** the contents reached -- recorded beside the box
 *   rather than folded into it.
 *
 * That is CSS's own model (`overflow` does not make a box grow; it decides whether
 * what does not fit is drawn or clipped), and it is the reason this module's record
 * is an *extent per side* rather than a second size: a box that overflows on the left
 * and a box that overflows on the right have the same width and two different
 * overflow regions, and a size cannot tell them apart.
 *
 * Four numbers, and they are measured from the **padding box**
 *
 *   left / top / right / bottom   how far the contents reached *beyond* that edge,
 *                                 each one non-negative, each one zero when nothing
 *                                 reached past it
 *
 * The reference is the padding box because that is where CSS puts the clipping
 * boundary (`overflow: hidden` clips to the padding edge, not to the border edge and
 * not to the content edge), and because it is the one rectangle a box's own
 * declarations cannot move: a margin is outside it, a border is outside it, and a
 * padding is *inside* it. So a box with `padding: 20px` and a child that sticks 5px
 * out of the padding box has an overflow of 5, whatever its border does.
 *
 * Two rectangles, and the difference between them is the phase
 *
 *   **region**      the padding box grown by the extents -- what the contents occupy
 *                   in the end, and what a parent includes when it measures its own
 *                   overflow. On an axis whose mode **clips**, the region is the
 *                   padding box itself: a box that hides an axis keeps what it hides.
 *
 *   **clip rect**   the padding box, when at least one axis clips. It is the boundary
 *                   a paint pass will clip to, and this phase stops at *exposing* it:
 *                   nothing here rasterises anything (`RoboLayoutOverflow.h` has no
 *                   pixel in it).
 *
 * The distinction is what makes the recursion work: a child contributes its **region**
 * to its parent, so a child that clips contributes only what it kept, while its own
 * record still says how far its contents reached. Two consumers, two answers, one
 * record -- and the alternative (fold the child's contents into the parent and forget
 * the child clipped) is how a clipped subtree comes to leak into an ancestor's
 * scrollable area.
 *
 * `visible` and `hidden`, per axis, and where the boundary is
 *
 * 10.13 added the CSS vocabulary (RoboCSSOverflow.h): two values, `visible` and
 * `hidden`, with `scroll`, `auto` and `clip` named as rows this build has not added.
 * That is the whole of what this phase reads, and the modes are resolved **per axis**
 * -- `overflow-x` and `overflow-y` are separate declarations, and a box can clip
 * horizontally and not vertically (`overflow-x: hidden; overflow-y: visible`), so the
 * record carries two modes and never one.
 *
 * The used value per axis, in the order the resolve asks it:
 *
 *   1. `overflow-x`/`overflow-y` (the longhands) first, each read from its own
 *      declaration;
 *   2. then `overflow` -- CSS's **shorthand**, which this build reads rather than
 *      expands, for either axis the longhand said nothing about. `overflow: hidden`
 *      therefore means both axes, which is what it means, and the phase's one cost is
 *      stated where it is paid: the two-value form (`overflow: hidden visible`) is not
 *      read, and a declaration written that way is an invalid value
 *      (RoboCSSOverflow.h);
 *   3. and `visible` for anything neither of them determined -- CSS's initial value,
 *      including for a declaration this build could not use. An axis whose declaration
 *      was refused is **used as `visible` and counted**, which is 10.8's rule for a
 *      dimension it cannot resolve: a document whose geometry depends on a `var()`
 *      reference lays out unclipped here, and the counter is what says so.
 *
 * What a box does NOT do
 *
 *   It does not grow. `width: 100px; overflow: visible` with 200px of contents is
 *   100px wide with an overflow of 100 on one side; the sizing layer is untouched, and
 *   the suite asserts the *pair* because a build that grew the box would look right in
 *   every single-box test and be wrong in every layout that depends on the number.
 *
 *   It does not collapse an axis into the pair. `clips(axis)` is asked per axis, and
 *   the result's two flags are per axis, because a scrollable axis and a clipped one
 *   are different boxes.
 *
 *   It does not move anything. A box that hides an axis keeps the geometry it was
 *   given; what changes is which rectangle a paint pass would clip to, and this phase
 *   does not paint.
 *
 * What is deliberately absent
 *
 *   **Scrolling.** No scroll offset, no scrollport, no scrollbar, and `overflow: auto`
 *   is not a mode this build has. `hidden` is the only behaviour here, and it is
 *   *clipping* rather than scrolling -- which is why the clip rect is exposed and no
 *   scrollable area is.
 *
 *   **Axis promotion.** CSS promotes `overflow-x: visible` to `auto` when the other axis
 *   scrolls, because a box cannot scroll one axis and paint outside itself on the other.
 *   That rule exists for scrolling, and this build has none, so `visible` beside `hidden`
 *   stays `visible` -- stated here rather than left to be discovered, and asserted by the
 *   suite so the phase that adds scrolling has a case to change.
 *
 *   **`text-overflow`, ellipsis, `overflow-clip-margin`, `contain`.** Each is a later
 *   phase's or a later value's.
 *
 * Float arithmetic is deliberately absent too: every number here is an integer pixel
 * count, every subtraction is done in 64 bits and saturated, and no extent can come
 * out negative -- a box whose contents reached *inside* it has an overflow of zero,
 * which is a different statement from a negative one and the reason the extents are
 * clamped at the boundary rather than allowed to go through zero.
 */

#ifndef ROBO_LAYOUT_OVERFLOW_H
#define ROBO_LAYOUT_OVERFLOW_H

#include <stddef.h>
#include <stdint.h>

#include "RoboCSS/RoboCSSBox.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSOverflow.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutGeometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * What one axis's overflow declaration holds, before anybody decides what to do with
 * it.
 *
 * Three kinds, and the third is the one worth stating: a declaration this build could
 * not use is a *declaration*, not an absence -- `overflow-x: var(--o)` and
 * `overflow-x: scroll` both arrive as an unusable value (a carried reference and a
 * refused identifier are indistinguishable by the time a computed style holds them),
 * and both are counted rather than dropped, because a document whose clipping depends
 * on one should be visible as such.
 *
 * `ABSENT` is **0** so a cleared record holds the state in which CSS's initial value
 * applies because nobody declared anything -- what @ref LayoutOverflowInit
 * writes and what a caller who only zeroed its struct has.
 */
typedef enum LayoutOverflowKind {
    /** Nobody declared this axis. CSS's initial value applies because it applies. */
    RoboLayoutOverflowAbsent = 0,
    /** A mode this build has, in @ref LayoutOverflowAxis::mode. */
    RoboLayoutOverflowResolved = 1,
    /** Declared, and not a value this build can use; used as `visible`. */
    RoboLayoutOverflowUnresolved = 2
} LayoutOverflowKind;

/** Number of defined kinds. */
#define RoboLayoutOverflowKindCount 3u

/** Non-zero when @p kind is one of the three above. */
int LayoutOverflowKindIsKnown(LayoutOverflowKind kind);

/** Name of @p kind; never NULL; "unknown" when undefined. */
const char *LayoutOverflowKindName(LayoutOverflowKind kind);

/**
 * Which axis an overflow belongs to.
 *
 * Two, and named for the direction rather than for a letter, because every door below
 * takes one: a mode is *per axis* and a build that indexed the pair by `0` and `1`
 * would make `clips(1)` a question about the document's writing conventions. The
 * enumerator values are append-only -- nothing is planned after these two -- and
 * HORIZONTAL is 0 because that is the axis `overflow-x` names.
 */
typedef enum LayoutOverflowAxisId {
    /** `overflow-x`: the axis contents advance along. */
    RoboLayoutOverflowAxisHorizontal = 0,
    /** `overflow-y`: the axis block-level contents stack down. */
    RoboLayoutOverflowAxisVertical = 1
} LayoutOverflowAxisId;

/** Number of defined axes. */
#define RoboLayoutOverflowAxisCount 2u

/** Non-zero when @p axis is one of the two. */
int LayoutOverflowAxisIsKnown(LayoutOverflowAxisId axis);

/** Name of @p axis ("horizontal", "vertical"); never NULL; "unknown" when undefined. */
const char *LayoutOverflowAxisName(LayoutOverflowAxisId axis);

/**
 * One axis's overflow declaration.
 *
 * A record by value and owned by nobody, like every other model record here. `mode` is
 * meaningful only when `kind` is `RESOLVED` -- reading it in the other two states is
 * reading a field that means nothing, which is why @ref LayoutOverflowAxisMode
 * reports a *miss* rather than handing back the `visible` it happens to hold.
 */
typedef struct LayoutOverflowAxis {
    /** What the style declared for this axis. */
    LayoutOverflowKind kind;
    /** The declared mode, meaningful only for `RESOLVED`. */
    CssOverflow mode;
} LayoutOverflowAxis;

/** An axis nobody declared: what a cleared record holds, and `visible` in effect. */
LayoutOverflowAxis LayoutOverflowAxisAbsent(void);

/** A declared mode: `visible` or `hidden`. */
LayoutOverflowAxis LayoutOverflowAxisResolved(CssOverflow mode);

/** A declaration this build cannot use. Used as `visible`, and counted. */
LayoutOverflowAxis LayoutOverflowAxisUnresolved(void);

/** Non-zero when @p axis is a side nobody declared. */
int LayoutOverflowAxisIsAbsent(const LayoutOverflowAxis *axis);

/** Non-zero when @p axis is a mode this build has. */
int LayoutOverflowAxisIsResolved(const LayoutOverflowAxis *axis);

/** Non-zero when @p axis is a declaration this build cannot use. */
int LayoutOverflowAxisIsUnresolved(const LayoutOverflowAxis *axis);

/** Non-zero when @p axis is a record this build may read. */
int LayoutOverflowAxisIsValid(const LayoutOverflowAxis *axis);

/**
 * The mode of a `RESOLVED` @p axis.
 * @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound when
 *         @p axis is not `RESOLVED` (@p out is untouched) -- a different answer from
 *         `visible`, which is a mode the initial value *is*.
 */
Result LayoutOverflowAxisMode(const LayoutOverflowAxis *axis,
                                             CssOverflow *out);

/** Non-zero when @p a and @p b are the same declaration. Every field, kind-aware. */
int LayoutOverflowAxisEqual(const LayoutOverflowAxis *a,
                                    const LayoutOverflowAxis *b);

/* ------------------------------------------------------------------ */
/* The declared overflow                                               */
/* ------------------------------------------------------------------ */

/**
 * What an element declares about what happens to contents that do not fit: one record
 * per axis.
 *
 * The two axes are the two `overflow-*` longhands, and the *shorthand* is read as
 * speaking for both of them -- see the header's rule 2. Keeping that fallback here
 * rather than in a consumer is what makes "one declaration, two axes" a fact about the
 * read rather than a rule three call sites have to remember.
 */
typedef struct LayoutOverflow {
    /** `overflow-x`, or the shorthand, or nothing. */
    LayoutOverflowAxis horizontal;
    /** `overflow-y`, or the shorthand, or nothing. */
    LayoutOverflowAxis vertical;
} LayoutOverflow;

/** Clear @p overflow to two **undeclared** axes -- CSS's initial state. */
void LayoutOverflowInit(LayoutOverflow *overflow);

/**
 * Read @p style's two axes.
 *
 * The one place a computed style becomes an overflow vocabulary, and the reason
 * RoboLayout holds no CSS parser: 10.13 registered `overflow`, `overflow-x` and
 * `overflow-y` on RoboCSS's closed overflow vocabulary, so an author's declaration
 * arrives as a typed value and this function classifies it -- `RESOLVED` for a mode
 * this build has, `UNRESOLVED` for a computed value that is not one (a `var()`
 * reference this build carries, or a declaration CSS refused), and `ABSENT` for an
 * entry that is not there at all.
 *
 * A NULL @p style is not a failure: a box with no style has made no declarations, so
 * the answer is two undeclared axes -- which is what a caller's initialised record
 * says too, and is the state in which CSS's initial value applies without anybody
 * having said so.
 */
LayoutOverflow LayoutOverflowOf(const CssComputedStyle *style);

/**
 * The declaration @p overflow holds for @p axis.
 * @return RoboOk; RoboInvalidArgument for NULL @p out or @p overflow;
 *         RoboNotFound for an axis that is not one of the two (@p out untouched).
 */
Result LayoutOverflowAxisOf(const LayoutOverflow *overflow,
                                           LayoutOverflowAxisId axis,
                                           LayoutOverflowAxis *out);

/** Non-zero when both records hold the same two declarations, axis by axis. */
int LayoutOverflowEqual(const LayoutOverflow *a,
                               const LayoutOverflow *b);

/** Non-zero when both axes are valid records. */
int LayoutOverflowIsValid(const LayoutOverflow *overflow);

/* ------------------------------------------------------------------ */
/* The used overflow                                                   */
/* ------------------------------------------------------------------ */

/**
 * What a box's declarations came to: two modes, four extents, and the counters.
 *
 * The record a box carries after a pass and the record a consumer reads, and it holds
 * two kinds of fact because a consumer needs both together:
 *
 *   the **modes**          what the declarations resolved to, per axis, so a paint
 *                          pass knows *whether* there is a boundary without re-reading
 *                          a style and re-deciding which declarations this build could
 *                          use;
 *   the **extents**        how far the contents reached beyond the padding box, per
 *                          side, so a paint pass knows *what* it would be clipping and
 *                          a parent knows what its child contributes.
 *
 * All four extents are **non-negative** and are measured from the padding box: the
 * left extent is how far contents reached *past the left edge*, not the coordinate of
 * the leftmost thing. A box whose contents fit has four zeroes, which is the common
 * case and is not an error -- it is a box with no overflow, and the difference between
 * it and a box nothing measured is the object's result state, exactly as everywhere
 * else in this module.
 *
 * The three counters partition the two axes:
 *
 *   `axesResolved`    an axis whose declared mode reached geometry. Counted even
 *                      when the mode is `visible`, because `overflow-x: visible` is a
 *                      declaration an author wrote and is not the same statement as
 *                      nobody having written anything.
 *   `axesUnresolved`  an axis declared with a value this build could not use. Its
 *                      used mode is `visible`.
 *   and the remainder are the axes nobody declared, in which CSS's initial value
 *   applies.
 *
 * `axesResolved + axesUnresolved <= 2`, and the rest of the pair is undeclared.
 */
typedef struct LayoutOverflowEdges {
    /** The used mode of the horizontal axis (`overflow-x`). */
    CssOverflow horizontal;
    /** The used mode of the vertical axis (`overflow-y`). */
    CssOverflow vertical;
    /** How far contents reached past the padding box's left edge; `>= 0`. */
    GfxPixelX left;
    /** How far contents reached above the padding box's top edge; `>= 0`. */
    GfxPixelY top;
    /** How far contents reached past the padding box's right edge; `>= 0`. */
    GfxPixelX right;
    /** How far contents reached below the padding box's bottom edge; `>= 0`. */
    GfxPixelY bottom;
    /** Axes whose declared mode reached geometry. */
    size_t axesResolved;
    /** Axes declared with a value this build cannot use. */
    size_t axesUnresolved;
} LayoutOverflowEdges;

/** Clear @p edges to no overflow, two `visible` axes and no declarations counted. */
void LayoutOverflowEdgesInit(LayoutOverflowEdges *edges);

/** Non-zero when @p edges is a record this build may read: four non-negative extents,
 *  two known modes and counters that agree with two axes. */
int LayoutOverflowEdgesIsValid(const LayoutOverflowEdges *edges);

/** Non-zero when @p a and @p b hold the same modes, the same extents *and* the same
 *  counters -- the whole record, for the reason every comparison in this module does:
 *  two conclusions that agreed about the geometry and disagreed about how many
 *  declarations they used would be two different answers to one document. */
int LayoutOverflowEdgesEqual(const LayoutOverflowEdges *a,
                                     const LayoutOverflowEdges *b);

/** The used mode of @p axis; `visible` for an unknown axis. */
CssOverflow LayoutOverflowEdgesMode(
    const LayoutOverflowEdges *edges,
    LayoutOverflowAxisId axis);

/** Non-zero when @p axis has a boundary -- the used mode clips. */
int LayoutOverflowEdgesClips(const LayoutOverflowEdges *edges,
                                     LayoutOverflowAxisId axis);

/** Non-zero when either axis clips. */
int LayoutOverflowEdgesClipsAny(const LayoutOverflowEdges *edges);

/**
 * The extent @p side's edge was passed by, or `0` when it was not passed at all.
 *
 * `CssBoxSide` rather than a fourth enumeration of the same four edges, for
 * the reason every other borrowed vocabulary in this module is borrowed: top, right,
 * bottom, left is the order an author reads a shorthand in, and a second definition of
 * it is one place for a transposed pair to hide.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a side that is not one
 *         of the four (@p out is untouched).
 */
Result LayoutOverflowEdgesExtent(
    const LayoutOverflowEdges *edges,
    CssBoxSide side,
    int32_t *out);

/** Non-zero when any extent is above zero: the fact a result's flags carry. */
int LayoutOverflowEdgesHasOverflow(const LayoutOverflowEdges *edges);

/** Non-zero when @p axis's extents are above zero -- the per-axis half of the above. */
int LayoutOverflowEdgesHasOverflowAxis(
    const LayoutOverflowEdges *edges,
    LayoutOverflowAxisId axis);

/**
 * Grow @p edges so that @p region is inside the region @p paddingBox describes.
 *
 * The one accumulation rule, and it is a per-side **maximum** of how far `region`
 * reaches past `paddingBox` -- not a union, because the record is measured from the
 * padding box rather than being a rectangle of its own, and the two are equivalent
 * only when both operands share their reference. `paddingBox` is the *caller's* box
 * and does not change; `region` is the thing being folded in, and both are in the same
 * coordinate space (the padding box's, whose origin is the padding box's own corner).
 *
 * Called once per placed child -- with the child's **region**, which is its padding box
 * grown by its own extents and clipped back on an axis it hides -- and once per box
 * with its own contents in a model that has them; the identity that makes those two the
 * same operation is the phase's recursion.
 *
 * Saturating: an extent is computed in 64 bits and clamped, so a box at the seam's
 * maximum reached by a child at the seam's maximum gets a maximum rather than a wrapped
 * negative. A negative extent is never produced: a `region` wholly inside
 * `paddingBox` leaves every extent where it was.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a rectangle with a
 *         negative size (@p edges is left untouched); RoboError and a diagnostic for
 *         an @p edges that is not a record (a hand-built one with counters that
 *         disagree with two axes).
 */
Result LayoutOverflowEdgesInclude(LayoutOverflowEdges *edges,
                                                LayoutRect paddingBox,
                                                LayoutRect region,
                                                LayoutDiag *diag);

/**
 * The **region** @p edges describes, given the box it was measured against.
 *
 * The padding box grown by the four extents, and clipped back to the padding box on an
 * axis whose mode clips -- which is the whole of what `overflow: hidden` means
 * geometrically, and the reason this door rather than the record is what a parent folds
 * in: a box that hides an axis contributes only what it kept.
 *
 * The same arithmetic a paint pass needs to know what it *may* draw.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for NULL arguments or a padding box
 *         with a negative size; RoboError for an @p edges that is not a record.
 */
Result LayoutOverflowEdgesRegion(const LayoutOverflowEdges *edges,
                                               LayoutRect paddingBox,
                                               LayoutRect *out,
                                               LayoutDiag *diag);

/**
 * The rectangle a paint pass would **clip to**, given the box the overflow was measured
 * against: the padding box, CSS's clipping edge.
 *
 * @return RoboOk and @p out when at least one axis clips; RoboNotFound when neither
 *         does (@p out untouched) -- a box with no boundary has no clip rectangle, which
 *         is a different statement from an empty one; RoboInvalidArgument for NULL
 *         arguments or a negative size; RoboError for a record that is not one.
 */
Result LayoutOverflowEdgesClipRect(
    const LayoutOverflowEdges *edges,
    LayoutRect paddingBox,
    LayoutRect *out,
    LayoutDiag *diag);

/* ------------------------------------------------------------------ */
/* The one policy                                                      */
/* ------------------------------------------------------------------ */

/**
 * Resolve @p declared to the modes a pass acts on, with no overflow measured yet.
 *
 * One policy rather than two, and the contrast with 10.12 is the point: a *displacement*
 * is the same arithmetic for every kind of box, and so is an overflow -- an atomic inline
 * and a block box read their declarations and derive their extents by the same two rules
 * -- so there is nothing for a second door to say.
 *
 * The rules, in the order the function asks them:
 *
 *   1. an axis the style declared a usable mode for takes that mode, and is counted
 *      `axesResolved` -- `visible` included;
 *   2. an axis declared with a value this build could not use is used as `visible` and is
 *      counted `axesUnresolved`;
 *   3. an axis nobody declared is `visible`, CSS's initial value, and is counted nowhere,
 *      because "the author wrote nothing" and "the author wrote `visible`" are two
 *      declarations and the counters are where the difference lives;
 *   4. the extents start at zero: this function answers *what was declared*, and the pass
 *      that measures the contents is what fills them in.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for NULL arguments. This function
 *         cannot fail on a document: every axis has a stated answer.
 */
Result LayoutOverflowResolve(const LayoutOverflow *declared,
                                           LayoutOverflowEdges *out,
                                           LayoutDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_OVERFLOW_H */
