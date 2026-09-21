/*
 * RoboPaint geometry (Phase 11.1).
 *
 * Two records and no new coordinate types, and both halves of that are decisions.
 *
 * **No new coordinate types**, because RoboLayout already made the point: it has no
 * geometry of its own and neither does this module. `RoboGraphicsGeometry.h` defines
 * the canonical pixel geometry, RoboLayout re-exports it under names that read as
 * layout, and RoboPaint reads it under layout's names -- so the numbers a rasteriser
 * eventually receives are the numbers RoboLayout wrote, with no conversion at any
 * boundary for a sign or an origin to flip in.
 *
 * **Two records**, because a painted box has two things a consumer needs and they have
 * different shapes:
 *
 *   `PaintBoxes` is the box model's three rectangles -- the border box, the
 *   padding box and the content box -- derived in one place from layout's result and
 *   layout's two inset records. Three rectangles rather than one because the
 *   difference between them *is* the thing painter and hit-tester both want: the band
 *   between the border box and the padding box is a border, the region inside the
 *   padding box is a background, and a consumer that re-derived either from a computed
 *   style would be re-deciding which declarations the layout pass could use.
 *
 *   `PaintClip` is the boundary in force, and it is **two intervals rather
 *   than one rectangle** -- which is the phase's one piece of geometry that is *not*
 *   the obvious shape, so it is argued at the record.
 *
 * Everything in this header is a value record owned by nobody: safe to copy, to embed
 * and to put on the stack. The one door that reads a tree borrows it and allocates
 * nothing.
 */
#ifndef ROBO_PAINT_GEOMETRY_H
#define ROBO_PAINT_GEOMETRY_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSOverflow.h"

#include "RoboLayout/RoboLayoutBorder.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutPadding.h"
#include "RoboLayout/RoboLayoutTree.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The three boxes                                                     */
/* ------------------------------------------------------------------ */

/**
 * One box, as the three rectangles that make it up.
 *
 * In **one coordinate space**, whichever the door that produced it used: the space is
 * the caller's question, not the record's, and `RoboPaintOperation.h` is where the two
 * spaces a paint list carries are named. A record that carried both would be a record
 * whose two halves could disagree.
 *
 * `insetsMeasured` is the flag that keeps two different boxes apart, and it is the
 * same "a flag, not a sentinel" rule `RoboLayoutConstraints.h` states for unset
 * bounds. A **text run** and a **transparent inline box with contents** are both
 * placements that RoboLayout's inset doors decline -- a run is not a box at all, and a
 * transparent inline box's rectangle is the union of what it enclosed rather than a
 * size this build can inflate by a declared padding and border
 * (`RoboLayoutPadding.h` argues it). For those, all three rectangles are the one
 * placement geometry and `insetsMeasured` is clear. A caller that needs "does this
 * box have box layers?" asks the flag; a caller that just wants the area may not care,
 * and the record says which one it is holding rather than making both look alike.
 */
typedef struct PaintBoxes {
    /** The outermost rectangle: what `LayoutResult::size` carries. */
    LayoutRect border;
    /** The border box less the four used border widths. */
    LayoutRect padding;
    /** The padding box less the four used paddings: where the contents begin. */
    LayoutRect content;
    /** Non-zero when this box's two inset layers answered for it. */
    int insetsMeasured;
} PaintBoxes;

/** Clear @p boxes to three empty rectangles with no insets measured. Safe on NULL. */
void PaintBoxesInit(PaintBoxes *boxes);

/**
 * Non-zero when @p boxes is a record this build may read.
 *
 * Three conditions, and the third is the one that makes the record worth having:
 *
 *   each rectangle has a non-negative size (an empty one is fine -- a box with no
 *   content has zero height, and emptiness is a predicate here, not a failure);
 *   the padding box is **inside** the border box and the content box inside the
 *   padding box, because the three are nested by construction and a record whose
 *   layers crossed would be arithmetic that went wrong one level down;
 *   and when @p insetsMeasured is clear, the three are **equal** -- a box with no
 *   layers has one rectangle, not three that happen to agree.
 */
int PaintBoxesIsValid(const PaintBoxes *boxes);

/** Field-by-field equality of all three rectangles *and* the measured flag. */
int PaintBoxesEqual(const PaintBoxes *a, const PaintBoxes *b);

/**
 * The three boxes a border box of @p borderBox has, with these two insets.
 *
 * The one derivation in this module, and it is deliberately the only place the two
 * inset layers are applied: a caller hands in what layout *concluded* -- the border
 * box, the four used border widths and the four used paddings -- and gets the layers
 * back. Nothing here reads a computed style, so there is no second opinion about
 * whether a declaration was one this build could use.
 *
 * Two things are worth stating because they are where the arithmetic can go wrong:
 *
 *   **the padding box is floored at zero on each axis.** A box whose borders are
 *   wider than the box itself leaves no room for the padding box, and a negative width
 *   is not a width. The floor is applied to the result rather than to each term, which
 *   is the shape RoboLayout's own three derived numbers use, and it is what makes a
 *   saturated border box -- `width: 2147483647px` with a `border: 5px solid` -- produce
 *   a readable record rather than a refusal;
 *
 *   **the origin is offset in 64 bits and saturated.** `border.x + width` is the sum of
 *   two bounded numbers and their sum is not bounded; phase 10.15 found that the hard
 *   way, in the pass itself, and this module restates the rule where its own
 *   accumulation is rather than reaching for layout's private helper.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or an @p insets record
 *         that is not one; RoboError with a diagnostic for a border box with a
 *         negative size.
 */
Result PaintBoxesFromLayers(const LayoutRect *borderBox,
                                          const LayoutBorderEdges *borders,
                                          const LayoutPaddingEdges *padding,
                                          PaintBoxes *out,
                                          PaintDiag *diag);

/**
 * The three boxes of layout object @p id, in **layout coordinates**.
 *
 * The read door, and the whole of the layout-to-paint geometry boundary: the result's
 * position and size, then the two inset records, then the derivation above. It
 * answers in the coordinates layout used -- relative to the containing block's content
 * origin -- because that is what layout concluded; turning them into one page space is
 * the traversal's job and is the one thing this door does not do, because a door that
 * did both would have to be handed an origin and a reader would have no way to ask for
 * the rectangle layout actually wrote.
 *
 * The insets are read through `LayoutObjectBorders` and
 * `LayoutObjectPadding`, and both **declining is not a failure**: a text run and
 * a transparent inline box with contents are placements this build does not inflate, so
 * their three rectangles are the placement's own and `insetsMeasured` is clear. A
 * miss (an id that names no box, or an object nothing placed) *is* a failure, because
 * there is no geometry to paint at all.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a NONE id; RoboNotFound
 *         when @p id names no box or nothing placed it; RoboError with a diagnostic
 *         when the derived layers are not a record.
 */
Result PaintBoxesOf(const LayoutTree *tree,
                                 LayoutNodeId id,
                                 PaintBoxes *out,
                                 PaintDiag *diag);

/**
 * @p boxes moved by @p dx/@p dy -- the accumulation step, in one place.
 *
 * The offset is applied in 64 bits and saturated, for the reason
 * `PaintBoxesFromLayers` gives: a page origin is an accumulation of bounded
 * numbers, and a wrapped one is a rectangle somewhere else on the page rather than an
 * obvious failure. A saturated offset puts the box at the edge of the drawable plane,
 * which is a wrong answer that is still *drawable* -- the choice RoboLayout's inset
 * layers made, restated here.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments.
 */
Result PaintBoxesTranslate(const PaintBoxes *boxes,
                                        int32_t dx,
                                        int32_t dy,
                                        PaintBoxes *out,
                                        PaintDiag *diag);

/* ------------------------------------------------------------------ */
/* The clip in force                                                   */
/* ------------------------------------------------------------------ */

/**
 * One axis's boundary: whether there is one, and where it is.
 *
 * `mode` is CSS's own record rather than a boolean, because "this axis has a boundary"
 * **is** "this axis resolved to a mode that hides" -- the same identity
 * `RoboLayoutOverflow.h` uses for its two axes -- and a second flag beside the mode
 * would be a second name for one fact.
 *
 * `start`/`end` are a **half-open interval in paint coordinates**: an x interval on the
 * horizontal axis and a y interval on the vertical one. They are meaningful only when
 * `mode` hides, and `PaintClipAxisIsActive` is the predicate that says so --
 * which is why there is no sentinel for "no boundary" and no need to read a number that
 * means nothing.
 */
typedef struct PaintClipAxis {
    /** The effective mode: `hidden` means this axis is bounded. */
    CssOverflow mode;
    /** The coordinate the interval starts at, half-open. */
    int32_t start;
    /** The coordinate it ends at, half-open; never below @ref start. */
    int32_t end;
} PaintClipAxis;

/**
 * The boundary in force at a point in the traversal.
 *
 * **Two axes rather than one rectangle**, and this is the record worth reading twice,
 * because one rectangle is the obvious shape and it is wrong. `overflow-x: hidden`
 * beside `overflow-y: visible` is a legal declaration an author writes and this build
 * resolves (`RoboLayoutOverflow.h`), and it means exactly what it says: bounded
 * horizontally, free vertically. A single rectangle cannot express that -- a consumer
 * handing its four numbers to a rasteriser would clip the axis the author left open,
 * and the document would look subtly wrong in a way no assertion about a rectangle
 * would catch.
 *
 * So the record is two intervals, and the two derived questions a consumer has are
 * answered by two doors rather than inferred: `PaintClipAxisIsActive` for "is
 * this axis bounded?", and `PaintClipRect` for the one case where a rectangle is
 * the right answer -- both axes bounded -- which it **refuses** rather than answering
 * otherwise, exactly as `LayoutOverflowEdgesClipRect` does.
 *
 * A cleared record is `visible` on both axes, which is CSS's initial value and the
 * state of a document that never declared an overflow.
 */
typedef struct PaintClip {
    /** The horizontal boundary (`overflow-x`), in paint coordinates. */
    PaintClipAxis horizontal;
    /** The vertical boundary (`overflow-y`), in paint coordinates. */
    PaintClipAxis vertical;
} PaintClip;

/** Clear @p clip to two unbounded axes. Safe on NULL. */
void PaintClipInit(PaintClip *clip);

/** The clip with no boundary on either axis: what a cleared record is, by value. */
PaintClip PaintClipNone(void);

/** Non-zero when @p axis has a boundary, i.e. its mode hides. Safe on NULL (0). */
int PaintClipAxisIsActive(const PaintClipAxis *axis);

/** Non-zero when either axis of @p clip has a boundary. Safe on NULL (0). */
int PaintClipIsActive(const PaintClip *clip);

/**
 * Non-zero when @p clip bounds **nothing at all**: an axis is bounded and its interval is
 * empty, so no point whatsoever can be drawn inside it.
 *
 * The predicate a consumer asks before spending work on a scope, and the reason it is here
 * rather than left to each of them: "a scope that draws nothing" is a fact about a document
 * -- a box whose ancestor's boundary already excluded it -- and a consumer that answered it
 * itself by comparing two intervals would be re-deriving `PaintClipAxisIsEmpty`.
 *
 * A record with no boundary on either axis is **not** empty; it is unbounded, which is the
 * opposite answer, and the two are exactly the cases a caller must keep apart. Safe on NULL
 * (0).
 */
int PaintClipIsEmpty(const PaintClip *clip);

/** Non-zero when @p clip is a record this build may read: a known mode per axis and an
 *  interval that does not run backwards on an axis that hides. */
int PaintClipIsValid(const PaintClip *clip);

/**
 * Non-zero when @p point is inside the region @p clip bounds.
 *
 * The record's **one containment question**, and it is asked per axis rather than of a
 * rectangle: an axis whose mode leaves the drawing free contains **every** coordinate on it,
 * and an axis that hides contains the half-open interval `[start, end)` -- so a point outside a
 * `hidden` axis is outside the boundary however the other axis answers, and a point beside a
 * `hidden` axis but within a `visible` one is *inside*, because that axis was never bounded.
 * That asymmetry is the whole reason the record is two intervals rather than one rectangle (see
 * the record's own comment), and a door that answered it by projecting to a rectangle would
 * have to invent a bound for the free axis and would then exclude points the document draws.
 *
 * It was added in **11.10**, by the first consumer that asks where an input point may land
 * rather than where a drawing must stop: a hit test refuses to descend into a box's contents
 * when the point is outside the boundary those contents are drawn under, and it decides that
 * with this call rather than by re-deriving the per-axis rule at the call site. The two
 * questions -- "is this drawing inside?" and "is this point inside?" -- are the same question
 * about the same record, and this is the second spelling of it.
 *
 * A **NULL** clip contains everything, which is the same reading an empty stack answers: no
 * boundary is in force, so nothing is excluded. An **empty** boundary contains nothing (see
 * `PaintClipIsEmpty`), so the two states are opposite answers and neither is an error.
 */
int PaintClipContains(const PaintClip *clip, LayoutPoint point);

/** Non-zero when @p point is inside one axis's half-open interval. False for an active
 *  axis the point is outside; true for an axis that bounds nothing. Safe on NULL (1). */
int PaintClipAxisContains(const PaintClipAxis *axis, int32_t coordinate);

/** Field-by-field equality of both axes and both intervals. Safe on NULL. */
int PaintClipEqual(const PaintClip *a, const PaintClip *b);

/**
 * The clip @p clip becomes inside @p boundary.
 *
 * The accumulation, and its whole rule is per axis rather than per rectangle: an axis
 * the boundary hides is **intersected** with the boundary's interval, and an axis it
 * leaves open is carried through untouched. A boundary that hides neither axis leaves
 * @p clip **unchanged**, which is the case a document with `overflow: visible`
 * everywhere produces on every box -- and the reason the traversal can call this for
 * every box rather than asking whether it needs to.
 *
 * The intersection is a maximum of two starts and a minimum of two ends, computed in 64
 * bits and saturated like every other accumulation in this module. An intersection that
 * comes out empty is **success with an empty interval**, not a failure: a box whose
 * clipping ancestor has already excluded it draws nothing, which is a statement about
 * the document rather than an error, and
 * `PaintClipAxisIsEmpty` is how a consumer asks.
 *
 * An intersection whose two intervals came out **disjoint** -- which is what an excluded
 * box produces, an ancestor's upper bound below the box's lower one -- is canonicalized to
 * the zero-length interval `[max(start), max(start)]` rather than left running backwards.
 * That is the record model's own rule stated in one place: an interval is *never* wider
 * than it is positioned, so "nothing is drawable here" is an empty interval and not an
 * illegal one, and every door downstream -- `PaintClipIsValid`,
 * `PaintClipAxisIsEmpty`, a clip stack push, a display command, a projection to a
 * rectangle -- can keep trusting the type. The interval lands on the **upper start**, the
 * near edge of the boundary that did the excluding, so equal inputs give an equal answer.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments, a mode that is not one of
 *         the two, or a @p boundary with a negative size; RoboError with a diagnostic
 *         for an @p clip that is not a record.
 */
Result PaintClipIntersect(const PaintClip *clip,
                                       const LayoutRect *boundary,
                                       CssOverflow horizontal,
                                       CssOverflow vertical,
                                       PaintClip *out,
                                       PaintDiag *diag);

/**
 * The clip @p outer becomes inside the scope @p inner: their **per-axis intersection**.
 *
 * The door beside `PaintClipIntersect` above, and the difference between them is what
 * each one is told. That one is handed a *boundary rectangle* and the two modes an element
 * declared, and answers "what does this element put in force?" -- the traversal's question,
 * asked where layout's padding box is at hand. This one is handed **two records** and
 * answers "what is in force inside a scope that is already bounded?" -- the question a
 * consumer replaying a flat sequence has, and the one a stack of scopes makes answerable
 * without reaching for geometry the sequence no longer carries.
 *
 * The rule is the smaller of two bounded intervals on each axis, and an axis @p inner leaves
 * **open is carried from @p outer rather than cleared**: a scope that bounds only `x` does
 * not free `y`, which is the same rule `PaintClipIntersect` states one layer down and
 * the reason both live in this module instead of one of them living in each caller.
 *
 * The resulting axis's `mode` is @p inner's on an axis it bounds, because the mode is the
 * *name* of the boundary and the narrower scope is the one whose declaration is being
 * honoured; the two modes agree about clipping by construction, since an axis that does not
 * clip never reaches this branch.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a mode neither record may
 *         carry; RoboError with a diagnostic when either record is not one this build
 *         produced. An intersection that comes out empty is success with an empty interval,
 *         exactly as it is one door up -- `PaintClipIsEmpty` is how a caller asks,
 *         and a *disjoint* pair of intervals is canonicalized to an empty one by the same
 *         helper, so the result is a record either way.
 */
Result PaintClipIntersectClip(const PaintClip *outer,
                                            const PaintClip *inner,
                                            PaintClip *out,
                                            PaintDiag *diag);

/** Non-zero when @p axis is bounded **and** its interval is empty: nothing inside it
 *  can be drawn. Safe on NULL (0). */
int PaintClipAxisIsEmpty(const PaintClipAxis *axis);

/**
 * The rectangle @p clip bounds, when it bounds both axes.
 *
 * The one case where a rectangle says everything the record says, and the door exists
 * for the consumer that can only clip rectangles. It refuses the rest, and the refusal
 * is the point: a `visible` axis has no bound to report, and answering one would hand a
 * rasteriser a number that means "free" as if it meant "bounded".
 *
 * @return RoboOk and @p out when both axes are bounded; RoboInvalidArgument for NULL
 *         arguments; RoboNotFound when either axis is unbounded (@p out untouched).
 */
Result PaintClipRect(const PaintClip *clip, LayoutRect *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_GEOMETRY_H */
