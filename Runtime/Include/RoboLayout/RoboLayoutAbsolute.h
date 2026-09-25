/*
 * RoboLayout out-of-flow placement — the `position: absolute` / `position: fixed`
 * behaviour (Phase 14.0).
 *
 * Phase 10.12 taught this module to move a box that is **in flow**: a `relative` box is
 * displaced from where flow put it, and its size, its flow contribution and every
 * following sibling are untouched. That phase named the other two values and said, in
 * one sentence at the flag, that placing a box *against* something is a later phase's:
 *
 *   `absolute` and `fixed`. Both take a box out of flow and place it against an
 *   ancestor or the viewport, and neither is this phase.
 *
 * This is that phase, pulled forward from the CSS-completion goal because Phase 14's
 * desktop shell cannot be composed without it: a wallpaper that fills the viewport, a
 * dock that sits on the bottom edge, a menu that floats over everything and a window
 * that is a rectangle at a place are four out-of-flow boxes, and a build that stacks
 * them in normal flow can express none of the four.
 *
 * The three questions, and why they are one module
 *
 * An out-of-flow box needs three answers, and no two of them can be given apart from
 * the third:
 *
 *   1. **what it is placed against** — its *containing block*. Not its parent: the
 *      nearest ancestor that is itself positioned, or the viewport when there is none.
 *      This is the question 9.13's two facts exist for (`CssPositioningIsPositioned`,
 *      `LayoutPositionIsRelative`'s neighbours), and getting it wrong is how an
 *      absolutely positioned box ends up inside a wrapper that never contained it.
 *
 *   2. **how big it is** — CSS's constraint equation for an out-of-flow box has an
 *      inset term on *each* side, so `left: 0; right: 0` on a box with `width: auto`
 *      means "fill the containing block", and `top: 0; bottom: 0` does the same
 *      vertically. That is not a special case of `auto`; it is `auto` *with* two
 *      insets, and it is the whole reason the desktop shell's wallpaper is one line of
 *      CSS here.
 *
 *   3. **where it goes** — and this is the answer 10.12 could not give at all: the
 *      **far** side anchors too. `right: 0` with no `left` is a box against the
 *      containing block's right edge, and it cannot be resolved until the box's own
 *      width is known, which is why this module's origin door takes the box's size as
 *      an argument rather than returning an offset.
 *
 * The four kinds of declaration, and what is deliberately *not* one
 *
 *   ABSENT (`declared == 0`)          nobody wrote this side. It says nothing.
 *   RESOLVED (`present == 1`)         a px length this build can use, signed, because
 *                                     `left: -10px` is legal CSS.
 *   AUTO (`autoSide == 1`)            the keyword. It says nothing either -- but it is a
 *                                     *different* declaration from nobody having written
 *                                     one, and the counters keep the two apart for the
 *                                     reason 10.9's, 10.10's and 10.12's do.
 *   UNRESOLVED (`declared == 1` and neither of the two above)  a percentage, a relative
 *                                     unit, a `calc()`. Used as zero and *counted*.
 *
 * The precedence rule, stated here rather than inferred, and it is 10.12's own rule
 * extended by one sentence:
 *
 *   **the near side wins over the far side, and an `auto` or absent near side lets the
 *   far side decide.**
 *
 * So `left: 10px` with `right: 4px` places the box 10px from the left edge; `right: 4px`
 * alone places its right *edge* 4px from the containing block's right edge; and with
 * neither side declared the box sits at its containing block's origin, which is the
 * *static position* approximation this build states instead of computing (`shrink-to-fit`
 * is below). CSS's own rule -- which side wins depends on the writing mode and the
 * direction, and the box's static position when neither side is declared -- is a later
 * phase's, and this build states one rule so that the phase which replaces it changes one
 * function and one suite.
 *
 * What this module is not
 *
 * Not a stack: nothing here reorders anything, `z-index` is not read, and paint order
 * remains document order. Not a scrollport: `sticky` is in flow and takes no offset here
 * exactly as it did in 10.12, and there is nothing to constrain against until a scroll
 * offset exists. Not the shaper of a box: the *pass* lays the box out; this module answers
 * the three questions above and hands back a record the pass applies, which is the same
 * split 10.9, 10.10, 10.11 and 10.12 each made and the reason a document can be asserted
 * against a policy rather than against a whole pass.
 *
 *   **A declared dimension beside two insets is over-constrained**, and this build's
 *   answer is the one CSS gives for a *left-to-right* document: the near inset and the
 *   declared size win, the far inset is ignored, and the axis is counted
 *   (`declaredOverConstrained`) so that a document that wrote it is visible in the record
 *   rather than silently half-honoured.
 *
 *   **`shrink-to-fit` is not implemented.** A box with `width: auto` and only one inset
 *   declared (or none) fills what the insets left rather than shrinking to its contents,
 *   because this build has no intrinsic width to shrink to (`RoboLayoutSizing.h` names
 *   `min-content`, `max-content` and `fit-content` as later work). Every desktop-shell
 *   shape this phase needed writes the side that makes the answer unambiguous; a document
 *   that relies on shrink-to-fit gets a filled box and a stated gap rather than a wrong
 *   one that looks right.
 *
 *   **Percentages do not resolve**, for either the insets or the containing block, which is
 *   10.8's rule unchanged: read, recorded `UNRESOLVED` and used as zero.
 *
 *   **`box-sizing: border-box` does not exist** anywhere in this build, so the used size
 *   this module reports is the **margin box** -- the basis CSS's constraint equation is
 *   written on -- and the pass subtracts the margins, the border and the padding to reach
 *   the content box, exactly as it does for an in-flow box.
 */

#ifndef ROBO_LAYOUT_ABSOLUTE_H
#define ROBO_LAYOUT_ABSOLUTE_H

#include <stddef.h>
#include <stdint.h>

#include "RoboCSS/RoboCSSBox.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSPosition.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutPosition.h"
#include "RoboLayout/RoboLayoutSizing.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * One of the two axes an out-of-flow box is placed on.
 *
 * Two axes and not one pair of four sides, because the two questions are asked
 * separately and their answers differ: a box can be stretched horizontally by
 * `left: 0; right: 0` and placed vertically by `top: 20px` alone, which is the shape
 * every menu bar in the desktop shell has.
 */
typedef enum LayoutAbsoluteAxis {
    /** The inline-across axis: `left` and `right`. */
    RoboLayoutAbsoluteAxisHorizontal = 0,
    /** The block-down axis: `top` and `bottom`. */
    RoboLayoutAbsoluteAxisVertical = 1
} LayoutAbsoluteAxis;

/** Number of defined axes. */
#define RoboLayoutAbsoluteAxisCount 2u

/** Non-zero when @p axis is one of the two above. */
int LayoutAbsoluteAxisIsKnown(LayoutAbsoluteAxis axis);

/** Name of @p axis; never NULL; "unknown" when undefined. */
const char *LayoutAbsoluteAxisName(LayoutAbsoluteAxis axis);

/**
 * One side's inset, as a style declared it: the four kinds above in three fields.
 *
 * `present` and `autoSide` are two flags rather than one three-valued kind because two
 * of the four states are "says nothing" and only the second of them is a declaration.
 * @ref LayoutInset already models exactly this (10.12, with `LayoutInsetKind`); this
 * record is that vocabulary narrowed to what an *out-of-flow* box asks about it -- the
 * near/far distinction is the module's, and a caller that wants the kinds can read
 * @ref LayoutPositionOf directly.
 */
typedef struct LayoutAbsoluteSide {
    /** 1 when the style declared a px length this build can use. */
    int present;
    /** 1 when the style declared the keyword `auto`. */
    int autoSide;
    /**
     * 1 when the style declared this side **at all**: a usable length, the keyword
     * `auto`, or a value this build cannot use.
     *
     * The third flag is what keeps the four states above apart in one record, and it is
     * what makes `declared && !present && !autoSide` the *counted* case rather than an
     * invisible one -- the same distinction 10.9's, 10.10's, 10.11's and 10.12's
     * vocabularies each needed a kind for.
     */
    int declared;
    /** The declared length in px; meaningful only when @p present. Signed. */
    GfxPixelX length;
} LayoutAbsoluteSide;

/** A side nobody declared: what a cleared record holds. */
LayoutAbsoluteSide LayoutAbsoluteSideAbsent(void);

/**
 * The two sides of one axis, in the order the rule asks them.
 *
 * `near` is `left` / `top` -- the side that wins -- and `far` is `right` / `bottom`.
 * The names are the ones CSS's own prose uses for this exact rule, and they are stored
 * that way rather than as `start`/`end` because the writing-mode-aware vocabulary is a
 * later phase's and this build's rule is stated on the physical sides.
 */
typedef struct LayoutAbsoluteEdges {
    LayoutAbsoluteSide near;
    LayoutAbsoluteSide far;
} LayoutAbsoluteEdges;

/** Clear @p edges to two undeclared sides. */
void LayoutAbsoluteEdgesInit(LayoutAbsoluteEdges *edges);

/** Non-zero when @p edges is a record this build may read. */
int LayoutAbsoluteEdgesIsValid(const LayoutAbsoluteEdges *edges);

/** Non-zero when both records hold the same two declarations. */
int LayoutAbsoluteEdgesEqual(const LayoutAbsoluteEdges *a,
                                      const LayoutAbsoluteEdges *b);

/** Non-zero when @p a and @p b are the same declaration. */
int LayoutAbsoluteSideEqual(const LayoutAbsoluteSide *a,
                                     const LayoutAbsoluteSide *b);

/** The declarations of one axis, read from a computed style. */
LayoutAbsoluteEdges LayoutAbsoluteEdgesOf(const CssComputedStyle *style,
                                                   LayoutAbsoluteAxis axis);

/** The declarations of a whole position record, per axis. A NULL @p position answers
 *  two undeclared axes rather than failing, for @ref LayoutInsetsOf's reason. */
LayoutAbsoluteEdges LayoutAbsoluteEdgesForAxis(const LayoutPosition *position,
                                                        LayoutAbsoluteAxis axis);

/**
 * The rectangle a box is placed **against**: the containing block's padding box.
 *
 * A rectangle rather than a size, because the origin is half the answer: an absolutely
 * positioned box inside a positioned wrapper is placed against that wrapper's padding
 * edge, and a build that carried only a size would place it against the viewport and look
 * right in every single-level test.
 *
 * `initial` says whether this is the **initial containing block** -- the viewport, whose
 * size the pass was handed -- and it is a field rather than an inference because the two
 * are reached by different questions: a `fixed` box is placed against it always, and an
 * `absolute` box reaches it only when no ancestor is positioned.
 */
typedef struct LayoutAbsoluteContainingBlock {
    /** The padding-box origin, in the pass's own coordinates. */
    LayoutPoint origin;
    /** The padding-box size. */
    LayoutSize size;
    /** 1 when this is the initial containing block (the viewport). */
    int initial;
} LayoutAbsoluteContainingBlock;

/** Clear @p block to an empty rectangle at the origin, not the initial one. */
void LayoutAbsoluteContainingBlockInit(LayoutAbsoluteContainingBlock *block);

/** Non-zero when @p block is a record this build may read. */
int LayoutAbsoluteContainingBlockIsValid(
    const LayoutAbsoluteContainingBlock *block);

/* ------------------------------------------------------------------ */
/* The one axis policy                                                 */
/* ------------------------------------------------------------------ */

/**
 * What one axis's two declarations came to.
 *
 * The record a pass applies, and the four facts in it are the four things that can be
 * *different* about an axis:
 *
 *   `sizedByInsets`            both sides present and the declared dimension not definite:
 *                              the used margin-box extent is the containing block less the
 *                              two insets. This is `left: 0; right: 0` and it is the flag a
 *                              suite asserts, because a build that read it as "fill the
 *                              containing block" agrees with it only when both insets are 0.
 *   `marginBoxExtent`          the used margin-box extent when `sizedByInsets`; `-1`
 *                              otherwise, which is the value that means "the box's own size
 *                              decides" and is not a size. Clamped into the geometry's range
 *                              and floored at zero.
 *   `anchorFar`                no *near* declaration (absent or `auto`) and a far side
 *                              present: the far side positions this axis. It cannot be
 *                              resolved to a coordinate here -- the box's own extent is part
 *                              of the answer -- which is why @ref LayoutAbsolutePlacementOrigin
 *                              is a second call.
 *   `declaredOverConstrained`  a definite declared dimension beside two present insets: the
 *                              near inset and the declared size win, the far one is ignored,
 *                              and this counter says the document wrote one.
 */
typedef struct LayoutAbsoluteAxisUse {
    /** The two declarations, carried so a caller can report them. */
    LayoutAbsoluteEdges edges;
    /** 1 when the two insets sized this axis. */
    int sizedByInsets;
    /** The used margin-box extent when `sizedByInsets`; `-1` otherwise. */
    int32_t marginBoxExtent;
    /** 1 when the far side positions this axis. */
    int anchorFar;
    /** 1 when a definite declared dimension met two present insets. */
    int declaredOverConstrained;
    /** Sides whose declared length reached this layer, zero and negative included. */
    size_t sidesUsed;
    /** Sides written `auto`: a declaration that says nothing. */
    size_t sidesAuto;
    /** Sides declared with a value this build cannot use; their value is zero. */
    size_t sidesUnresolved;
} LayoutAbsoluteAxisUse;

/** Clear @p use to an axis nothing declared: no size, no anchor, no counters. */
void LayoutAbsoluteAxisUseInit(LayoutAbsoluteAxisUse *use);

/** Non-zero when @p use is a record this build may read. */
int LayoutAbsoluteAxisUseIsValid(const LayoutAbsoluteAxisUse *use);

/** Non-zero when @p a and @p b hold the same conclusion, counters included. */
int LayoutAbsoluteAxisUseEqual(const LayoutAbsoluteAxisUse *a,
                                        const LayoutAbsoluteAxisUse *b);

/**
 * Resolve one @p axis of @p position against a containing block @p containingBlockExtent px
 * along it, with @p declared as the dimension the box declared on it.
 *
 * The whole of this module's policy, and it is one function rather than two because the
 * width and the height of an out-of-flow box are answered by the same three rules:
 *
 *   1. **the near side first.** A present `left`/`top` is used, whether or not the far
 *      side was written, and the far one is then only counted (it is `declaredOverConstrained`
 *      when the declared dimension is definite, and simply ignored when it is not);
 *   2. **then the far side**, when the near side says nothing (absent or `auto`) and the
 *      far side is present: this axis is anchored by its far edge, and the *used extent* is
 *      the box's own -- which is why `marginBoxExtent` stays `-1` and `anchorFar` is set;
 *   3. **then the size**, and only when the far side also says something: both insets
 *      present with no definite declared dimension size the axis between them.
 *
 * A NULL @p declared is CSS's initial value rather than an omission: a box with no style
 * declared nothing, and nothing is `auto`. The same rule @ref LayoutSizingReadDimension
 * follows for a missing entry.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for NULL arguments or an unknown axis;
 *         RoboError and a diagnostic for a record no policy can read (a negative
 *         containing extent, or a declared dimension that is not a valid record).
 */
Result LayoutAbsoluteResolveAxis(const LayoutPosition *position,
                                            LayoutAbsoluteAxis axis,
                                            const LayoutDimension *declared,
                                            int32_t containingBlockExtent,
                                            LayoutAbsoluteAxisUse *out,
                                            LayoutDiag *diag);

/* ------------------------------------------------------------------ */
/* What a pass counts, and the record it applies                       */
/* ------------------------------------------------------------------ */

/**
 * The counters of one pass's out-of-flow work -- **defined below**, beside the doors that
 * report them, and forward-declared here because @ref LayoutAbsolutePlacementBuild takes a
 * pointer to one. A forward declaration rather than a second copy of the record: one
 * definition, one place to change it.
 */
struct LayoutAbsoluteCounters;

/**
 * Everything a pass needs to place one out-of-flow box, resolved from its style.
 *
 * One record rather than the seven arguments a pass would otherwise thread, and it is
 * deliberately **complete**: the pass it is handed to has already read the style, decided
 * the containing block and resolved both axes, so placing the box is applying this record
 * and nothing else. It is a *request* rather than a conclusion -- the box's own size is not
 * in it, because the pass is what computes that -- which is why the two origin- and
 * height-dependent facts are kept as the flags and numbers they are.
 */
typedef struct LayoutAbsolutePlacement {
    /** What the box is placed against. */
    LayoutAbsoluteContainingBlock containingBlock;
    /** The horizontal axis's two declarations. */
    LayoutAbsoluteEdges horizontal;
    /** The vertical axis's two declarations. */
    LayoutAbsoluteEdges vertical;
    /** 1 when the far horizontal side positions this box. */
    int anchorFarX;
    /** 1 when the far vertical side positions this box. */
    int anchorFarY;
    /** 1 when two insets sized the width. */
    int sizedByInsetsX;
    /** 1 when two insets sized the height. */
    int sizedByInsetsY;
    /** The used **margin-box** height when `sizedByInsetsY`; `-1` otherwise. */
    int32_t marginBoxHeight;
    /** 1 when a definite declared width met two insets. */
    int overConstrainedX;
    /** 1 when a definite declared height met two insets. */
    int overConstrainedY;
    /** The declared `position` this came from, carried for a reader. */
    CssPositioning mode;
    /**
     * The width the box's contents are laid out into: the containing block less whatever
     * the insets claim, floored at zero.
     *
     * The *same* door an in-flow box's available width is, deliberately: with `width: auto`
     * the sizing layer then fills it, and the resulting margin box is exactly the containing
     * block less the two insets -- CSS's stretch -- because the pass subtracts the margins,
     * the border and the padding from this number exactly as it does in flow. That is why
     * the width needs no `marginBoxWidth` field beside the height's: one is an input to the
     * sizing layer and the other is not.
     */
    int32_t availableWidth;
} LayoutAbsolutePlacement;

/** Non-zero when @p placement is a record this build may read. */
int LayoutAbsolutePlacementIsValid(const LayoutAbsolutePlacement *placement);

/** Non-zero when @p a and @p b hold the same request, field for field. */
int LayoutAbsolutePlacementEqual(const LayoutAbsolutePlacement *a,
                                         const LayoutAbsolutePlacement *b);

/**
 * Resolve @p position against @p containingBlock into the request a pass applies, with
 * @p declaredWidth and @p declaredHeight the dimensions the box declared.
 *
 * Two axis resolutions and the assembly, and it exists so that a pass reads a style **once**
 * and gets one record: the alternative -- a pass calling @ref LayoutAbsoluteResolveAxis twice
 * and putting the answer together itself -- is how the two axes' `availableWidth` arithmetic
 * and their `anchorFar` handling drift apart.
 *
 * @param counters optional; both axes' counters are summed into it. NULL is "not asked for"
 *        and not a failure, for the reason every other record here treats it so.
 * @return RoboOk and @p out; RoboInvalidArgument for NULL @p position, @p containingBlock or
 *         @p out, or an invalid containing block; RoboError and a diagnostic for a declared
 *         dimension this build cannot read.
 */
Result LayoutAbsolutePlacementBuild(const LayoutPosition *position,
                                              const LayoutAbsoluteContainingBlock *containingBlock,
                                              const LayoutDimension *declaredWidth,
                                              const LayoutDimension *declaredHeight,
                                              LayoutAbsolutePlacement *out,
                                              struct LayoutAbsoluteCounters *counters,
                                              LayoutDiag *diag);

/**
 * The **margin edge** origin of a box of margin-box size @p marginBox placed by @p placement.
 *
 * The second call, and it is second for a reason that is the phase: `right: 0` means "my right
 * edge is at the containing block's right edge", so the coordinate depends on the box's own
 * extent -- which does not exist until the box has been sized. A build that resolved the
 * origin *before* sizing could only implement the near side, which is exactly what 10.12 said
 * and why `absolute` was declared out of its scope.
 *
 * The arithmetic, per axis, is one expression with a branch in it:
 *
 *   near axis   `containingBlock.origin + near`
 *   far axis    `containingBlock.origin + containingBlock.size - far - marginBox`
 *
 * Both terms are bounded and the sum is not, so each is computed in 64 bits and clamped into
 * the geometry's range rather than wrapped -- the rule 10.15 established for the pass and
 * every layer since has followed.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for NULL arguments or an invalid placement;
 *         RoboError and a diagnostic for a margin box this build cannot hold.
 */
Result LayoutAbsolutePlacementOrigin(const LayoutAbsolutePlacement *placement,
                                                  LayoutSize marginBox,
                                                  LayoutPoint *out,
                                                  LayoutDiag *diag);

/**
 * The **content box height** a stretched box's `top` and `bottom` ask for.
 *
 * `top: 0; bottom: 0` on a box with `height: auto` sizes its **margin box** to the containing
 * block's height, and the content box is that less the four insets the pass has already
 * resolved: the two margins, the two borders and the two paddings. The subtraction is here
 * rather than in the pass because it is part of the same equation the insets are, and floored
 * at zero because a padding box taller than its own insets leaves no room rather than a
 * negative height.
 *
 * @param placement the request, whose `marginBoxHeight` is the number this starts from.
 * @param marginsAndInsets `margin-top + margin-bottom + border-top + border-bottom +
 *        padding-top + padding-bottom`, which is what the pass has in hand and what this
 *        module has no business recomputing.
 * @return RoboOk and @p out; RoboInvalidArgument for NULL arguments; RoboError and a
 *         diagnostic for a placement that is not sized by its insets (there is no height to
 *         convert, and a caller asking is a caller bug worth seeing).
 */
Result LayoutAbsoluteContentHeight(const LayoutAbsolutePlacement *placement,
                                              int32_t marginsAndInsets,
                                              int32_t *out,
                                              LayoutDiag *diag);

/**
 * The counters of one pass's out-of-flow work.
 *
 * Six counters, and each answers a question a document can ask that no other counter can:
 *
 *   `boxesOutOfFlow`      boxes this phase took **out of flow** and placed. A counter about a
 *                         box rather than a declaration, like 10.12's `boxesShifted`, and the
 *                         one that says the phase did anything at all.
 *   `sidesUsed`           side declarations this phase acted on, summed over boxes, zero and
 *                         negative lengths included. Deliberately the *opposite* of 10.12's
 *                         `offsetsDeclined` for the same declarations: they reached this layer
 *                         and were used, where before this phase they were counted as declined.
 *   `sidesAuto`           sides written `auto`: a declaration that says nothing, and not the
 *                         same statement as nobody having written one.
 *   `sidesUnresolved`     sides declared with a value this build cannot use -- a percentage, a
 *                         relative unit. Their value is zero, which is why they are counted: a
 *                         box positioned with `top: 10%` sits at its containing block's origin
 *                         here, and the count is what says so.
 *   `axesFromInsets`      axes whose used extent came from **two** opposing insets. The counter
 *                         that distinguishes "this box is 400px wide" from "this box's two insets
 *                         made it 400px wide", which is the whole of the stretch rule.
 *   `axesAnchoredFar`     axes positioned by the **far** side. The counter that says a document
 *                         used the capability this phase added; a build that read `right: 0` as
 *                         "says nothing" would have four right-looking counts and this one at
 *                         zero.
 *   `viewportAnchored`    boxes placed against the **initial containing block**: every `fixed`
 *                         box, and an `absolute` box whose ancestors are all static. Counted
 *                         apart from `boxesOutOfFlow` for the reason every other pair here is:
 *                         a document whose only out-of-flow box is `fixed` and one whose only
 *                         box is `absolute` inside a positioned wrapper are different documents,
 *                         and both would show one box otherwise.
 */
typedef struct LayoutAbsoluteCounters {
    /** Boxes placed out of flow (`absolute` or `fixed`). */
    size_t boxesOutOfFlow;
    /** Sides whose declared length reached this layer, zero and negative included. */
    size_t sidesUsed;
    /** Sides written `auto`. */
    size_t sidesAuto;
    /** Sides declared with a value this build cannot use. */
    size_t sidesUnresolved;
    /** Axes whose used extent came from two opposing insets. */
    size_t axesFromInsets;
    /** Axes positioned by the far side. */
    size_t axesAnchoredFar;
    /** Boxes placed against the initial containing block. */
    size_t viewportAnchored;
} LayoutAbsoluteCounters;

/** Clear @p counters to zero. */
void LayoutAbsoluteCountersInit(LayoutAbsoluteCounters *counters);

/** Non-zero when @p a and @p b hold the same seven numbers. */
int LayoutAbsoluteCountersEqual(const LayoutAbsoluteCounters *a,
                                         const LayoutAbsoluteCounters *b);

/* ------------------------------------------------------------------ */
/* The two classification questions                                    */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p position takes its box **out of flow**: `absolute` or `fixed`.
 *
 * One door rather than a comparison at every call site, and it is 9.13's own answer rather
 * than a second rule: `CssPositioningIsOutOfFlow` is the vocabulary's fact, and this function
 * exists to add the two things the pass needs around it -- a NULL record is `static` (CSS's
 * initial value, and @ref LayoutPositionOf's own answer for a style that declared nothing),
 * and a record whose mode is not one of the five is refused rather than guessed at.
 */
int LayoutAbsoluteIsOutOfFlow(const LayoutPosition *position);

/**
 * Non-zero when @p position is placed against the **initial containing block**: `fixed`.
 *
 * A `fixed` box is not placed against its nearest positioned ancestor -- that is the one thing
 * that separates it from `absolute` -- and the pass therefore needs the question asked
 * separately from "is this out of flow".
 */
int LayoutAbsoluteIsViewportAnchored(const LayoutPosition *position);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_ABSOLUTE_H */
