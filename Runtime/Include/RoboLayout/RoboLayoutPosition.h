/*
 * RoboLayout positioning (Phase 10.12).
 *
 * The first phase in this module whose number is not a size, an inset or a gap, but
 * a **displacement**: where a box ends up relative to where flow put it. Everything
 * before it changed the box; this changes nothing about the box and moves all of it.
 *
 *   a margin      moves the box and never the size          (10.9)
 *   a border      grows the box and moves its contents in    (10.11)
 *   a padding     grows the box and moves its contents in    (10.10)
 *   a position    moves the box and never the size OR the flow
 *
 * That last line is the whole phase, and it is why the numbers here are a *pair*
 * (`dx`, `dy`) rather than four edges: a margin's four sides are four independent
 * gaps around a box, while an inset's four sides are four *claims about where* the
 * box should be, and only one of each opposing pair can win. Collapsing the four
 * into the two that survive is what `LayoutPositionResolve` does, and the
 * record it produces is the smallest thing a caller needs to place a box.
 *
 * Two coordinate spaces, and the phase is the reason there are two
 *
 * A layout pass has always produced one position per box -- the box's **flow**
 * position, where its containing block and its margins put it. A relatively
 * positioned box needs a *second*: where it is actually drawn, which is the flow
 * position plus the offset. This module keeps both, and the split is deliberate:
 *
 *   `LayoutResult::position`  the **final** position -- the border-box
 *                                     origin a paint pass draws at and a hit test
 *                                     tests against, offset included;
 *   @ref LayoutPositionOffset on the object  the displacement itself, so
 *                                     the **flow** position is `final - offset` and
 *                                     neither number has to be guessed.
 *
 * The alternative -- writing the flow position into the result and the offset
 * nowhere -- would make every consumer above add the offset back, which is the
 * arithmetic this module refuses to make a caller re-derive (10.9's rule, applied to
 * the one number that is a *displacement* rather than an extent).
 *
 * Why the result is the final position and not the flow one
 *
 * Because the two published consumers both want the final one. Paint draws where a
 * box is, not where flow would have put it; a hit test is answered by the rectangle a
 * user can see. And a **descendant's** position is stored relative to its containing
 * block's *content origin*, so when a caller accumulates positions down the tree it
 * accumulates the parent's final position -- which means a relative shift on a
 * parent moves its whole subtree without any child's stored number changing. That is
 * the correct CSS behaviour and it falls out of the coordinate convention rather than
 * being implemented a second time.
 *
 * What a relative box does NOT do: it does not take up less room, and it does not
 * give its room away. The parent stacks its next child below the box's **flow**
 * extent, so a `position: relative` box that has been moved 100px down still leaves
 * the hole flow gave it, and the box after it starts where it always would have.
 * That is stated as a value by the suite, because a build that moved the box *and*
 * its flow contribution would look identical in every single-box test.
 *
 * The four kinds, and why `auto` is a kind rather than a zero
 *
 *   ABSENT       the side a style declared **nothing** for. CSS's initial value, and
 *                this is the state in which that value applies without anybody having
 *                said so. It contributes nothing and is counted nowhere.
 *
 *   RESOLVED     a px length this build can use, in @ref LayoutInset::length.
 *                **Signed**: `left: -10px` is legal CSS, and a negative offset is how
 *                an author moves a box up or to the left.
 *
 *   AUTO         the keyword. An inset's `auto` is not a distribution like a margin's
 *                -- nothing is split -- it means "this side says nothing", and the
 *                *other* side of the pair is the one that decides. It is a kind rather
 *                than folded onto `ABSENT` for the reason 10.9 gives for its own: "the
 *                author wrote `auto`" and "the author wrote nothing" are two
 *                declarations, and the counters are where the difference is visible.
 *
 *   UNRESOLVED   declared, and not usable here: a percentage, a relative unit, a
 *                `calc()` this build has no basis for. It contributes **zero** and is
 *                *counted*, which is 10.8's rule for a dimension it cannot resolve.
 *
 * The precedence rule, and why it is one sentence rather than a specification
 *
 * CSS's full offset resolution -- which side wins depends on the box's writing mode,
 * its direction, and whether the containing block is left- or right-anchored -- is a
 * later phase's, and this build states one rule instead:
 *
 *   **left wins over right, top wins over bottom, and an `auto` side says nothing.**
 *
 * So `left: 10px` and `right: 5px` together give `dx = +10` (left wins), `right: 5px`
 * alone gives `dx = -5` (there is no left, so right is the one that means something),
 * and `top: 4px; bottom: 9px` gives `dy = +4`. It is deterministic, it is enough for
 * every document that writes one side of a pair, and it is *documented at the rule*
 * so that the phase that replaces it changes one function.
 *
 * What is deliberately absent
 *
 *   **`absolute` and `fixed`.** Both take a box out of flow and place it against an
 *   ancestor or the viewport, and neither is this phase: `CssPositioning`
 *   already carries the values and their two facts (9.13), so the vocabulary is
 *   complete and the *behaviour* is 18.5's. This phase moves a box that is in flow.
 *
 *   **`sticky`.** In flow until a scroll constraint moves it, and this build has no
 *   scrollport to constrain against -- so the value is read, classified as in flow and
 *   positioned (which is what it is before anything scrolls), and it takes no offset
 *   here. Named for the reason `RoboCSSPosition.h` names it: dropping the declaration
 *   would lose the values beside it.
 *
 *   **`z-index`, stacking contexts and paint order.** A displaced box in this build is
 *   displaced and nothing more; nothing is reordered, and no flag says otherwise.
 *
 *   **Percentages**, for every side: read, recorded `UNRESOLVED` and used as zero,
 *   which is the rule 10.8 set and 10.9, 10.10 and 10.11 each inherited.
 */

#ifndef ROBO_LAYOUT_POSITION_H
#define ROBO_LAYOUT_POSITION_H

#include <stddef.h>
#include <stdint.h>

#include "RoboCSS/RoboCSSBox.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSPosition.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutGeometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * What one side's inset declaration holds, before anybody decides what to do with it.
 *
 * Deliberately the same four kinds 10.9's margin vocabulary has, for the same reason:
 * the grammar is the same (`<length-percentage> | auto` -- 9.13 points at the box
 * model's `EDGE_OFFSET` shape and this phase reads it back), an undeclared side and a
 * declared `auto` are one used value and two declarations, and the counters are where
 * the difference lives.
 *
 * `ABSENT` is **0** so a cleared record holds the side's initial state -- which is what
 * @ref LayoutPositionInit writes and what a caller who only zeroed its struct
 * has. A vocabulary whose zero meant `RESOLVED` would make a forgotten initialisation
 * a claim that the box declares an inset of `0px`.
 */
typedef enum LayoutInsetKind {
    /** Nobody declared this side. Contributes nothing and is counted nowhere. */
    RoboLayoutInsetAbsent = 0,
    /** A usable px length in @ref LayoutInset::length; may be negative. */
    RoboLayoutInsetResolved = 1,
    /** The keyword `auto`: this side says nothing, and its partner decides. */
    RoboLayoutInsetAuto = 2,
    /** Declared, and not a value this build can use: a percentage, a relative unit. */
    RoboLayoutInsetUnresolved = 3
} LayoutInsetKind;

/** Number of defined inset kinds. */
#define RoboLayoutInsetKindCount 4u

/** Non-zero when @p kind is one of the four above. */
int LayoutInsetKindIsKnown(LayoutInsetKind kind);

/** Name of @p kind; never NULL; "unknown" when undefined. */
const char *LayoutInsetKindName(LayoutInsetKind kind);

/**
 * One side's inset, as the style declared it.
 *
 * A record by value and owned by nobody, like every other model record here: no
 * allocation, safe to copy and to put on the stack. `length` is meaningful only when
 * `kind` is `RESOLVED` -- reading it in the other three states is reading a field that
 * means nothing, which is why @ref LayoutInsetLength reports a *miss* rather
 * than handing back the zero it happens to hold.
 *
 * **Signed**, and it is the same signedness a margin has rather than the
 * non-negativity a padding or a border has: `left: -10px` is a legal declaration and a
 * real technique, and the offset arithmetic below is written so that a negative value
 * needs no clamp to keep a position valid.
 */
typedef struct LayoutInset {
    /** What the style declared for this side. */
    LayoutInsetKind kind;
    /** The declared length in px, meaningful only for `RESOLVED`. */
    GfxPixelX length;
} LayoutInset;

/** A `RESOLVED` inset of zero: what `top: 0` is, and *a* default rather than the
 *  absence of one. */
LayoutInset LayoutInsetZero(void);

/** The side nobody declared: what a cleared record holds and what
 *  @ref LayoutPositionInit writes. */
LayoutInset LayoutInsetAbsent(void);

/** A `RESOLVED` inset of @p length px. Negative is legal and is why this is signed. */
LayoutInset LayoutInsetResolved(GfxPixelX length);

/** The keyword `auto`: this side says nothing. */
LayoutInset LayoutInsetAuto(void);

/** A declaration this build cannot use: a percentage, a relative unit. */
LayoutInset LayoutInsetUnresolved(void);

/** Non-zero when @p inset is the `auto` kind. */
int LayoutInsetIsAuto(const LayoutInset *inset);

/** Non-zero when @p inset is a resolvable length. */
int LayoutInsetIsResolved(const LayoutInset *inset);

/** Non-zero when @p inset is a side nobody declared. */
int LayoutInsetIsAbsent(const LayoutInset *inset);

/** Non-zero when @p inset is a declaration this build cannot use. */
int LayoutInsetIsUnresolved(const LayoutInset *inset);

/** Non-zero when @p inset is a record this build may read: a known kind and, for
 *  `RESOLVED`, a length inside the geometry's range. */
int LayoutInsetIsValid(const LayoutInset *inset);

/**
 * The length of a `RESOLVED` @p inset.
 * @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound when
 *         @p inset is not `RESOLVED` (@p out is untouched) -- a different answer from
 *         a length of zero, which is a box an author asked to shift by nothing.
 */
Result LayoutInsetLength(const LayoutInset *inset,
                                       GfxPixelX *out);

/** Non-zero when @p a and @p b are the same declaration. Every field, and for a kind
 *  that carries a length that includes the length. */
int LayoutInsetEqual(const LayoutInset *a,
                            const LayoutInset *b);

/* ------------------------------------------------------------------ */
/* The four sides                                                      */
/* ------------------------------------------------------------------ */

/**
 * The four sides of one box, in CSS's own order.
 *
 * The order is `CssBoxSide`'s and the enum is **borrowed** from 9.11 rather
 * than restated, for the reason this module borrows `display` and `position`: top,
 * right, bottom, left is the order an author reads a shorthand in, and a second
 * definition of it in one product is one place for a transposed pair to hide. The
 * insets are the four `EDGE_OFFSET` properties, so the side vocabulary spells them.
 */
typedef struct LayoutInsets {
    LayoutInset top;
    LayoutInset right;
    LayoutInset bottom;
    LayoutInset left;
} LayoutInsets;

/** Clear @p insets to four **undeclared** sides -- the state in which CSS's initial
 *  value applies because nobody declared anything. */
void LayoutInsetsInit(LayoutInsets *insets);

/**
 * Read @p style's four inset longhands.
 *
 * The one place a computed style becomes an inset vocabulary, and the reason
 * RoboLayout holds no CSS parser: 9.11/9.13 already gave `top`, `right`, `bottom` and
 * `left` registry rows on the box model's `EDGE_OFFSET` shape, so an author's
 * `top: 10px` arrives here as one computed `top` and this function reads four entries
 * and classifies each.
 *
 * A NULL @p style is not a failure: a box with no style has made no declarations, so
 * the answer is four undeclared sides -- which is what a caller's initialised record
 * says too, and is the state the geometry reads as no offset.
 */
LayoutInsets LayoutInsetsOf(const CssComputedStyle *style);

/**
 * The inset @p insets holds for @p side.
 * @return RoboOk; RoboInvalidArgument for a NULL @p out or @p insets;
 *         RoboNotFound for a side that is not one of the four (@p out untouched).
 */
Result LayoutInsetsSide(const LayoutInsets *insets,
                                      CssBoxSide side,
                                      LayoutInset *out);

/** Non-zero when both records hold the same four declarations, side by side. */
int LayoutInsetsEqual(const LayoutInsets *a,
                             const LayoutInsets *b);

/** Non-zero when all four sides are valid records. */
int LayoutInsetsIsValid(const LayoutInsets *insets);

/* ------------------------------------------------------------------ */
/* The declared position                                               */
/* ------------------------------------------------------------------ */

/**
 * What an element declares about where its box is: the mode, and the four insets.
 *
 * The mode is 9.13's own enumerator rather than a copy of it, and it is carried here
 * because the insets are **meaningless apart from it**: CSS does not apply an offset
 * to a `static` box, so "`left: 10px`" is a declaration whose effect depends entirely
 * on the value beside it. Keeping the two in one record is what makes that
 * dependence structural rather than a rule a caller has to remember.
 *
 * The order of the fields is the order the questions are asked in: the mode first,
 * because whether the insets apply at all is a fact about it.
 */
typedef struct LayoutPosition {
    /** The declared `position`, or `STATIC` when the style declared none. */
    CssPositioning mode;
    /** The four declared insets. */
    LayoutInsets insets;
} LayoutPosition;

/** Clear @p position to CSS's initial state: `static` and four undeclared sides. */
void LayoutPositionInit(LayoutPosition *position);

/**
 * Read @p style's `position` and its four insets.
 *
 * Two reads and not one, because the two properties compute differently -- a mode is a
 * typed enumerator (9.13) and an inset is the box model's offset shape (9.11) -- and
 * the record keeps them together because neither means anything alone. An absent
 * `position` answers `STATIC`, which is CSS's initial value and not a guess: 9.13
 * makes `static` the zero of its own vocabulary for exactly this reason.
 */
LayoutPosition LayoutPositionOf(const CssComputedStyle *style);

/** Non-zero when @p position's mode is one this phase moves -- `relative`. */
int LayoutPositionIsRelative(const LayoutPosition *position);

/** Non-zero when @p position is a record this build may read. */
int LayoutPositionIsValid(const LayoutPosition *position);

/** Non-zero when @p a and @p b are the same declaration: the mode and the four insets. */
int LayoutPositionEqual(const LayoutPosition *a,
                               const LayoutPosition *b);

/* ------------------------------------------------------------------ */
/* The used offset                                                     */
/* ------------------------------------------------------------------ */

/**
 * What the declarations came to: a displacement, and the counters.
 *
 * The record a pass applies to a box and a caller reads back to recover the flow
 * position. `dx` is **rightward-positive** and `dy` is **downward-positive**, which is
 * the geometry's own sense (`GfxPixelX`/`_y_t`), so `top: 5px` is `dy = +5`
 * and `bottom: 5px` is `dy = -5` -- the sign flip is the rule that makes `bottom` a
 * distance *from* the bottom edge and is the one place a reader can get this wrong.
 *
 * A zero offset is a legal value and the common one: every `static` box in every
 * document resolves to it, which is why @ref LayoutPositionResolve cannot fail
 * on a document and why the object's record being cleared is distinguishable from a
 * pass never having run only by the result's state.
 *
 * The four counters partition the four sides, and the first two are the ones worth
 * stating:
 *
 *   `insetsResolved`   a side the style declared a usable length for, `0` and a
 *                       negative one included. Counted even when precedence ignored
 *                       it, because it did reach this layer -- "the author wrote two
 *                       opposing insets" is a fact a document has.
 *   `insetsAuto`       a side written `auto`: a declaration that says nothing, and
 *                       is not the same statement as nobody having written one.
 *   `insetsUnresolved` a declaration this build could not use. Its value is zero.
 *   `insetsDeclined`   a declaration **this phase did not act on**, for either of the
 *                       two reasons it has: the box is `static`, and CSS does not apply
 *                       an offset to one; or its mode is one this build does not move
 *                       (`absolute`, `fixed`, `sticky`). The mode is on the position
 *                       record and tells the two apart; this counter says only that the
 *                       declarations were there and were not used.
 *
 * `insetsResolved + insetsAuto + insetsUnresolved + insetsDeclined <= 4`, and the
 * remainder is the `ABSENT` sides. On a relative box `insetsDeclined` is zero and the
 * other three partition the declared sides; on any other box the first three are zero
 * and `insetsDeclined` counts everything declared.
 */
typedef struct LayoutPositionOffset {
    /** Horizontal displacement in px, rightward-positive. */
    GfxPixelX dx;
    /** Vertical displacement in px, downward-positive. */
    GfxPixelY dy;
    /** Sides whose declared length reached this layer, zero and negative included. */
    size_t insetsResolved;
    /** Sides written `auto`: a declaration that says nothing. */
    size_t insetsAuto;
    /** Sides declared with a value this build cannot use. */
    size_t insetsUnresolved;
    /** Sides this phase did not act on, for either of the two reasons above. */
    size_t insetsDeclined;
} LayoutPositionOffset;

/** Clear @p offset to no displacement and no declarations. */
void LayoutPositionOffsetInit(LayoutPositionOffset *offset);

/** Non-zero when @p offset is a record this build may read: two numbers inside the
 *  geometry's range and four counters that agree with four sides. */
int LayoutPositionOffsetIsValid(const LayoutPositionOffset *offset);

/**
 * Non-zero when @p a and @p b hold the same displacement *and* the same counters.
 *
 * Every field, for the reason 10.9's and 10.11's comparisons give: the record is a
 * conclusion rather than a request, and two resolutions that agreed about where a box
 * went and disagreed about how many declarations they declined would be two different
 * answers to one document.
 */
int LayoutPositionOffsetEqual(const LayoutPositionOffset *a,
                                      const LayoutPositionOffset *b);

/* ------------------------------------------------------------------ */
/* The one policy                                                      */
/* ------------------------------------------------------------------ */

/**
 * Resolve @p position to the displacement a pass applies, and the counters.
 *
 * One policy rather than two, and the contrast with 10.9 is the point: a margin's four
 * declarations mean different things to a block box and to an atomic inline, so that
 * phase needed two doors. A *position* moves a box by the same rule whatever
 * participates in what -- `relative; left: 10px` shifts a block box and an
 * inline-block identically -- so there is nothing for a second door to say.
 *
 * The rules, in the order the function asks them:
 *
 *   1. a mode that is not `relative` produces a **zero offset** and counts every
 *      declared side as `insetsDeclined`. Two reasons reach this branch and the mode
 *      tells them apart: `static` is CSS's initial value and CSS does not apply an
 *      offset to one, while `absolute`, `fixed` and `sticky` are in-flow or not but
 *      are placed against something this build does not compute yet;
 *   2. a `relative` box takes `dx` from `left` when `left` is `RESOLVED`, else from
 *      `-right` when `right` is `RESOLVED`, else `0`;
 *   3. and `dy` from `top`, else `-bottom`, else `0`, by the same shape;
 *   4. an `auto` side and an `ABSENT` side both say nothing, and are counted
 *      separately because they are separate declarations;
 *   5. an `UNRESOLVED` side is used as zero and counted.
 *
 * So **left wins over right and top wins over bottom** (`RoboLayoutPosition.h` states
 * the rule at length), an `auto` never decides, and a document that writes one side of
 * a pair is unambiguous.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for NULL arguments. This function
 *         cannot fail on a document: every declaration has a stated answer.
 */
Result LayoutPositionResolve(const LayoutPosition *position,
                                           LayoutPositionOffset *out,
                                           LayoutDiag *diag);

/**
 * The **final** position of a box whose flow position is @p flow, displaced by
 * @p offset.
 *
 * The one place the two coordinate spaces meet, and it exists so that the addition is
 * stated once rather than at every pass that places a box. In 64 bits and clamped into
 * the geometry's range, because both terms are bounded by the seam's maximum and their
 * sum is not -- a box whose margin origin is already at the extreme displaced by an
 * extreme inset must saturate rather than wrap into a negative coordinate, which is
 * the same arithmetic 10.10 and 10.11 do for a size one axis over.
 *
 * Clamped rather than refused, for the reason the two inset layers give: there is no
 * document an author can write that some number cannot describe, and a position that
 * saturates is still a drawable coordinate.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for NULL arguments; RoboError and
 *         a diagnostic for an offset record no position can be derived from (a
 *         hand-built record with counters that disagree with four sides).
 */
Result LayoutPositionApply(LayoutPoint flow,
                                         const LayoutPositionOffset *offset,
                                         LayoutPoint *out,
                                         LayoutDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_POSITION_H */
