/*
 * RoboLayout padding (Phase 10.10).
 *
 * What an author asks for *inside* a box, and the one arithmetic that turns those
 * declarations into the box a reader sees.
 *
 * 10.9 answered *how much room a box takes* and put four numbers **outside** it; this
 * phase is the mirror image, four numbers **inside** it, and the difference between
 * the two is the whole of both phases:
 *
 *   a margin     changes where the box is, what is left for it, and how far the next
 *                box down starts -- and never changes the box;
 *
 *   a padding    changes how big the box is and where its contents begin -- and never
 *                moves where the box is.
 *
 * The identity that changes
 *
 * Until this phase a box's content box *was* its border box and every document said
 * so. RoboLayoutMargin.h closed with the prediction -- "its border box *is* its
 * content box, which is the identity that changes when padding arrives" -- and this
 * is where it arrives:
 *
 *   robo_layout_result_t::size        the BORDER box: what the box's edges enclose
 *   the box's own declarations        the CONTENT box: what `width` and `height` mean
 *
 * `RoboLayoutResult.h` has said `size` is the border-box size since 10.1, "because
 * that is the only size paint can draw without re-deriving padding and border", and
 * that statement is now *doing something* rather than agreeing with a coincidence.
 * The two numbers are equal in every document that declares no padding, which is why
 * nothing before this phase had to distinguish them.
 *
 * This build is `box-sizing: content-box`, which is the CSS **initial value**
 *
 * Stated loudly because it is the decision every number below follows from, and
 * because `box-sizing` is a later goal's property that this build does not have.
 * The model is CSS's own default: a declared `width` or `height` is the **content**
 * box, and the border box is `content + padding`. Three consequences, each stated
 * where it is implemented:
 *
 *   a declared `width: 200px` with `padding: 5px` **is 200 wide inside and 210
 *   outside**, and the box's children are laid out into the 200. A build that read
 *   the declaration as a border box would give the children 190;
 *
 *   `width: auto` is not "the containing block's width plus my padding" -- CSS's
 *   constraint equation has a padding term on each side, so an `auto` width fills
 *   what the margins **and the padding** left. A box with `padding: 10px` and no
 *   declared width is exactly as wide as its containing block, not 20px wider;
 *
 *   a `min-width`/`max-width` bound applies to the content box, which is where
 *   content-box sizing puts it, and the bounds path is 10.8's unchanged.
 *
 * A later `box-sizing` phase adds one branch -- "subtract the padding before the
 * declaration is used" -- at the one place the reader below hands the sizing layer a
 * number, and changes nothing else in this module.
 *
 * Three kinds, and `auto` is deliberately not one of them
 *
 *   ABSENT       the side a style declared **nothing** for. CSS's initial value is
 *                `0`, so the geometry uses a zero for it -- and it is counted
 *                nowhere, which is the only place it is distinguishable from the
 *                next kind. 10.9 learned this the expensive way: a vocabulary that
 *                folded "nobody declared this" into "declared `0`" cannot answer how
 *                many paddings a document had, and `stats.paddings_applied` is
 *                exactly that question.
 *
 *   RESOLVED     a px length this build can use, in @ref robo_layout_padding::length.
 *                **Non-negative**, and that is the one respect in which this
 *                vocabulary differs from the margin one: `padding: -10px` is invalid
 *                CSS and the classification refuses it, where `margin: -10px` is a
 *                technique. The two readers share one classification and differ in
 *                one parameter.
 *
 *   UNRESOLVED   declared, and not usable here: a percentage, a relative unit, a
 *                `calc()`, a negative length, or `auto` -- which CSS does not allow
 *                for this property at all. It contributes **zero** to the geometry and
 *                is *counted* instead, which is this module's rule for every
 *                declaration it cannot honour: a document must not fail because of
 *                one, and an author's `5%` must not be silently a confident zero
 *                nobody can see.
 *
 * There is no `AUTO` kind, and the absence is a decision rather than an oversight.
 * The classification both this reader and 10.9's are shaped from *can* answer `AUTO`
 * -- it is one function -- and this reader maps that answer onto `UNRESOLVED`, for the
 * reason 9.11 gives for rejecting `padding: auto` outright: the CSS grammar has no
 * such value, so a style carrying one is a style that violated its own grammar, and
 * inventing a fourth kind for a value the grammar forbids would give that violation a
 * vocabulary of its own. It is counted as a declaration this build cannot use, which
 * is what it is.
 *
 * One resolver, not two, and why that is different from margins
 *
 * 10.9 kept two doors -- `robo_layout_margins_resolve_block` and `_resolve_inline` --
 * because `auto` means two different things to a block box and to an inline-level
 * participant: it distributes for the first and is zero for the second, and a flag
 * would let a caller pass the wrong rule and get a plausible answer.
 *
 * Padding has no `auto`, so there is nothing to distribute and nothing for two
 * policies to disagree about: the same four declarations mean the same thing to a
 * block box, an atomic inline and a transparent one. One door follows from the
 * grammar, and a second would be a second answer to a question with one.
 *
 * What a padding does to a box, in the two directions it has
 *
 *   the CONTENT box       starts `padding-left`/`padding-top` in from the border
 *                         edge, so a box's children begin inside it;
 *   the BORDER box        is the content box plus the padding on each axis, which is
 *                         what @ref robo_layout_padding_edges_border_box computes and
 *                         what a result's `size` carries;
 *   what is left for an   is the containing block less the margins (10.9) **and** the
 *   `auto` width          padding, because CSS's equation has both terms;
 *   how far the next box  is unchanged, and that is the point: a padding is inside
 *   down starts           the box, so a parent still stacks by the child's margin
 *                         box -- which since this phase is its margin plus a border
 *                         box that already contains the padding.
 *
 * A padding never moves a box. `result.position` stays the box's **border-box** origin
 * -- 10.9's margin origin with the margins added, and the same number it was -- and the
 * **content origin** is that plus the padding, which a caller derives from
 * `robo_layout_object_padding` rather than reading out of the result. That is the same
 * bargain 10.9 struck for the margin box: the result holds the box, and the insets that
 * are outside or inside it are kept on the object where re-deriving them is impossible.
 *
 * What is deliberately absent
 *
 *   **Borders** -- 10.11. So the border box computed here is a content box plus
 *   padding and nothing else, and a `border: 4px solid` declaration is still read from
 *   the style and applied to no number. The doors below are named for what they add so
 *   that 10.11 can extend them rather than rename them.
 *
 *   **Padding on a transparent inline box** -- a `<span>` with contents, whose
 *   rectangle is the *union* of what it contains. CSS does grow such a box's inline
 *   advance by its horizontal padding and its line box by its vertical padding; this
 *   build applies neither, and the reason is not economy: an inline box's rectangle is
 *   derived from its contents rather than placed, so padding on it is a rule about
 *   *fragmenting* an inline box across lines, which is the anonymous-box and
 *   line-breaking work this module has not done. It is stated in the header, asserted
 *   as a value by the suite (`layout.paddings.inline_transparent_not_applied`), and
 *   named in `layout/docs/Padding.md` as the thing a later goal changes -- the same
 *   treatment 10.9 gave to margin collapsing.
 *
 *   **Percentages** -- `padding: 5%` resolves against the containing block's *width* in
 *   CSS, for every side including the vertical ones. This build has a containing width,
 *   so it arguably could; it does not, because 10.8 left every percentage unresolved and
 *   resolving one here would make `padding: 5%` the only percentage in the module that
 *   means something. It is `UNRESOLVED` and counted, and the phase that resolves
 *   percentages owns all of them at once.
 *
 *   **`box-sizing`** -- see above. The model is `content-box`, the CSS initial value,
 *   and it is the default so that a later `box-sizing: border-box` is a branch rather
 *   than a change of meaning.
 */

#ifndef ROBO_LAYOUT_PADDING_H
#define ROBO_LAYOUT_PADDING_H

#include <stddef.h>
#include <stdint.h>

#include "RoboCSS/RoboCSSBox.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutGeometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * What one side's declaration holds, before anybody decides what to do with it.
 *
 * `ABSENT` is **0** deliberately, and for the reason RoboCSS gives for making
 * `position: static` the zero of its own vocabulary: a cleared or zeroed record then
 * holds the side's **initial state**, which is what `robo_layout_paddings_init` writes
 * and what a caller who only cleared its struct has. A vocabulary whose zero meant
 * `RESOLVED` would make a forgotten initialisation a claim -- a box that had a
 * padding nobody wrote.
 */
typedef enum robo_layout_padding_kind {
    /** Nobody declared this side. CSS's initial value is `0`, so this is a zero in
     *  geometry and an *absence* in the counters. */
    ROBO_LAYOUT_PADDING_ABSENT = 0,
    /** A usable, **non-negative** px length in @ref robo_layout_padding::length. */
    ROBO_LAYOUT_PADDING_RESOLVED = 1,
    /** Declared, and not a value this build can use: a percentage, a relative unit, a
     *  negative length, or the `auto` the grammar does not allow. */
    ROBO_LAYOUT_PADDING_UNRESOLVED = 2
} robo_layout_padding_kind_t;

/** Number of defined padding kinds. */
#define ROBO_LAYOUT_PADDING_KIND_COUNT 3u

/** Non-zero when @p kind is one of the three above. */
int robo_layout_padding_kind_is_known(robo_layout_padding_kind_t kind);

/** Name of @p kind; never NULL; "unknown" when undefined. */
const char *robo_layout_padding_kind_name(robo_layout_padding_kind_t kind);

/**
 * One side's padding, as the style declared it.
 *
 * A record by value and owned by nobody, like every other model record here: no
 * allocation, safe to copy and to put on the stack. `length` is meaningful only when
 * `kind` is `RESOLVED` -- reading it in the other two states is reading a field that
 * means nothing, which is why `robo_layout_padding_length` reports a *miss* rather
 * than handing back the zero it happens to hold.
 */
typedef struct robo_layout_padding {
    /** What the style declared for this side. */
    robo_layout_padding_kind_t kind;
    /**
     * The declared length in px, meaningful only for `RESOLVED`.
     *
     * **Unsigned in meaning**, and the field is the geometry's own pixel type because
     * there is nothing else it could be: a negative length is not a padding this build
     * can use, it is a declaration the classification refuses, so a `RESOLVED` record
     * always holds a value at or above zero -- and
     * @ref robo_layout_padding_is_valid is what says so out loud rather than leaving a
     * reader to trust it.
     */
    robo_gfx_pixel_x_t length;
} robo_layout_padding_t;

/**
 * The four sides of one box, in CSS's own order.
 *
 * The order is `robo_css_box_side_t`'s and the enum is **borrowed** from 9.11 rather
 * than restated, for the reason this module borrows `display` from 9.12 and the margin
 * record borrows the same order: top, right, bottom, left is the order an author's
 * shorthand is read in, and a second definition of it in one product is one place for
 * a transposed `padding: 10px 20px` to hide.
 */
typedef struct robo_layout_paddings {
    robo_layout_padding_t top;
    robo_layout_padding_t right;
    robo_layout_padding_t bottom;
    robo_layout_padding_t left;
} robo_layout_paddings_t;

/* ------------------------------------------------------------------ */
/* One side                                                            */
/* ------------------------------------------------------------------ */

/** A `RESOLVED` padding of zero: what `padding: 0` is, and *a* default rather than
 *  the absence of one. */
robo_layout_padding_t robo_layout_padding_zero(void);

/**
 * The side nobody declared.
 *
 * What a cleared record holds and what `robo_layout_paddings_init` writes, because
 * CSS's initial value for every padding is `0` and this is the state in which that
 * value is used without anybody having said so.
 */
robo_layout_padding_t robo_layout_padding_absent(void);

/** A `RESOLVED` padding of @p length px. A negative @p length is *stored* and is
 *  reported invalid by @ref robo_layout_padding_is_valid -- this is the checker's
 *  counterpart, not a clamp, because a caller that built one from a computed style has
 *  already been through the classification. */
robo_layout_padding_t robo_layout_padding_resolved(robo_gfx_pixel_x_t length);

/** A declaration this build cannot use: a percentage, a relative unit, a negative
 *  length, `auto`. */
robo_layout_padding_t robo_layout_padding_unresolved(void);

/** Non-zero when @p padding is a resolvable length. */
int robo_layout_padding_is_resolved(const robo_layout_padding_t *padding);

/** Non-zero when @p padding is a side nobody declared. */
int robo_layout_padding_is_absent(const robo_layout_padding_t *padding);

/** Non-zero when @p padding is a declaration this build cannot use. */
int robo_layout_padding_is_unresolved(const robo_layout_padding_t *padding);

/**
 * Non-zero when @p padding is a record this build may read.
 *
 * A known kind, and -- for `RESOLVED` -- a length that is finite, **non-negative** and
 * inside the geometry's range. The non-negativity is the check this vocabulary has and
 * the margin one does not, and it is worth having in a predicate rather than only in
 * the reader: a caller may hand this module a record it built itself, and "is this
 * something I may put into a size?" should have an answer that does not depend on how
 * the record was built. It is also what makes a negative length *visible* where it
 * entered rather than as a box that shrank.
 */
int robo_layout_padding_is_valid(const robo_layout_padding_t *padding);

/**
 * The length of a `RESOLVED` @p padding.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND when
 *         @p padding is not `RESOLVED` (@p out is untouched) -- a different answer from
 *         a length of zero, which is a box an author asked to have no inset.
 */
robo_result_t robo_layout_padding_length(const robo_layout_padding_t *padding,
                                        robo_gfx_pixel_x_t *out);

/**
 * Non-zero when @p a and @p b are the same declaration.
 *
 * Every field, and for a kind that carries a length that includes the length: two
 * paddings are equal when a caller could not tell them apart, which is the same
 * definition 10.8 and 10.9 use.
 */
int robo_layout_padding_equal(const robo_layout_padding_t *a,
                              const robo_layout_padding_t *b);

/* ------------------------------------------------------------------ */
/* The four sides                                                      */
/* ------------------------------------------------------------------ */

/**
 * Clear @p paddings to four **`ABSENT`** sides -- the state in which CSS's initial
 * value `0` applies to every side because nobody declared one.
 */
void robo_layout_paddings_init(robo_layout_paddings_t *paddings);

/**
 * Read @p style's four padding longhands.
 *
 * The one place a computed style becomes a padding vocabulary, and the reason
 * RoboLayout holds no CSS parser: 9.11 already resolved the shorthand, the cascade and
 * the inheritance, so `padding: 10px 20px` arrives here as four computed longhands and
 * this function reads four entries and classifies each. `padding: 10px 20px 30px 40px`
 * is four declarations one layer down and four entries here, and neither reader nor
 * resolver knows the shorthand existed.
 *
 * A NULL @p style is not a failure: a box with no style has made no declarations, so the
 * answer is four `ABSENT` sides -- which is what a caller's initialised record says too,
 * and is the state the geometry reads as the initial value `0`.
 *
 * A longhand **absent** from the computed style is `ABSENT` for the same reason.
 */
robo_layout_paddings_t robo_layout_paddings_of(const robo_css_computed_style_t *style);

/**
 * The padding @p paddings holds for @p side.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p out or @p paddings;
 *         ROBO_NOT_FOUND for a side that is not one of the four (@p out untouched).
 */
robo_result_t robo_layout_paddings_side(const robo_layout_paddings_t *paddings,
                                       robo_css_box_side_t side,
                                       robo_layout_padding_t *out);

/** Non-zero when both records hold the same four declarations, side by side. */
int robo_layout_paddings_equal(const robo_layout_paddings_t *a,
                               const robo_layout_paddings_t *b);

/** Non-zero when all four sides are valid records. */
int robo_layout_paddings_is_valid(const robo_layout_paddings_t *paddings);

/* ------------------------------------------------------------------ */
/* The used values                                                     */
/* ------------------------------------------------------------------ */

/**
 * What the four declarations came to, in the coordinate space a size is written in.
 *
 * The *used* values rather than the declared ones -- though for this property the two
 * are the same number, which is worth stating because it is not true of a margin: a
 * padding has no keyword to distribute and no containing block to resolve against, so
 * "the used padding" is "the declared padding, or zero for a side nobody wrote and for
 * a declaration this build cannot use". The record exists all the same, and for the
 * reason every other record does: it is the shape the passes read, it is what the
 * comparison in a determinism case compares, and it is where the two counters live.
 *
 * Every field is **non-negative**. That is not a clamp applied late; it is a property of
 * the kinds the resolver accepts, and @ref robo_layout_padding_edges_is_valid states it
 * so that a hand-built record cannot smuggle a negative side into a size.
 *
 * The two counters partition the four sides by what the *style* said, and that is the
 * honest part -- a padding is not "used" by a containing block, so there is nothing
 * derived to count:
 *
 *   `resolved`    a side the style declared a usable length for, `0` included;
 *   `unresolved`  a declaration this build could not use. Its used value is 0.
 *
 * `resolved + unresolved <= 4`, and the remainder is the `ABSENT` sides. Neither an
 * `ABSENT` side nor an `UNRESOLVED` one is a declaration that reached geometry, and the
 * first is invisible in the numbers while the second is visible in the counters -- which
 * is the whole reason they are counted separately rather than as "not resolved".
 */
typedef struct robo_layout_padding_edges {
    robo_gfx_pixel_x_t top;
    robo_gfx_pixel_x_t right;
    robo_gfx_pixel_x_t bottom;
    robo_gfx_pixel_x_t left;
    /** Sides whose declared length reached geometry, zero included. */
    size_t resolved;
    /** Sides declared with a value this build cannot use. */
    size_t unresolved;
} robo_layout_padding_edges_t;

/** Clear @p edges to no padding at all. */
void robo_layout_padding_edges_init(robo_layout_padding_edges_t *edges);

/** Non-zero when @p edges describe something a size can hold: four non-negative
 *  numbers and counters that agree with four sides. */
int robo_layout_padding_edges_is_valid(const robo_layout_padding_edges_t *edges);

/**
 * Non-zero when @p a and @p b hold the same used values *and* the same counters.
 *
 * Every field, for the reason 10.9's edges comparison gives: the record is a conclusion
 * rather than a request, and two resolutions that agreed about the numbers and disagreed
 * about how many declarations they could not use would be two different answers to one
 * document.
 */
int robo_layout_padding_edges_equal(const robo_layout_padding_edges_t *a,
                                    const robo_layout_padding_edges_t *b);

/**
 * The width left for the **contents** of a box whose border box is @p border_width.
 *
 * The first of the two derived numbers, and the one an `auto` width goes through: CSS's
 * constraint equation has a padding term on each side, so a box with no declared width
 * fills what its margins *and* its padding left rather than overflowing its containing
 * block by the padding. It is a single axis rather than a size because that is how a
 * contained width arrives -- one number, and a block box's height is its contents' and
 * never its containing block's (10.8).
 *
 * Floored at **zero**, once, here: a border box narrower than its own padding leaves no
 * room for contents and a negative available width is not a width. The alternative --
 * refusing -- would fail a document for a stylesheet an author is allowed to write.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_ERROR and a
 *         diagnostic for edges that are not valid (a negative side is a record this
 *         module's own resolver never produces, so one reaching here is a caller bug
 *         and the moment to see it is before a size is derived from it).
 */
robo_result_t robo_layout_padding_edges_content_width(const robo_layout_padding_edges_t *edges,
                                                      int32_t border_width,
                                                      int32_t *out,
                                                      robo_layout_diag_t *diag);

/**
 * The **border box** a content box of @p content asks for, with these padded sides.
 *
 * The second derived number and the phase in one call: the content box plus the padding
 * on each axis, which is what `robo_layout_result_t::size` carries. Both axes at once
 * because a size is what a result holds, and the two are computed identically.
 *
 * Done in 64 bits and clamped to the geometry's range, because content and padding are
 * each bounded by the seam's maximum and their sum is not: a `width: 2147483647px` box
 * with a `padding: 10px` must not wrap into a negative size. Clamped rather than
 * refused, because there is no document an author can write that some number cannot
 * describe, and a size that saturates is still a drawable rectangle.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a negative @p content
 *         (a box's own extent is never negative); ROBO_ERROR and a diagnostic for
 *         invalid edges.
 */
robo_result_t robo_layout_padding_edges_border_box(const robo_layout_padding_edges_t *edges,
                                                   robo_layout_size_t content,
                                                   robo_layout_size_t *out,
                                                   robo_layout_diag_t *diag);

/* ------------------------------------------------------------------ */
/* The one policy                                                      */
/* ------------------------------------------------------------------ */

/**
 * Resolve @p paddings to the four used values.
 *
 * The rules, and there are three of them rather than six:
 *
 *   1. an `ABSENT` side is used as **zero** and counted nowhere: CSS's initial value for
 *      a padding is `0`, and a side nobody declared is not a declaration that reached
 *      geometry;
 *   2. a `RESOLVED` side is used as written, and it is non-negative by construction;
 *   3. an `UNRESOLVED` side is used as zero and counted -- a percentage, a relative unit,
 *      a negative length, or the `auto` the grammar forbids.
 *
 * No distribution, no containing width, no second door: this is the whole policy, and
 * RoboLayoutPadding.h's opening note argues why margins needed two of these and padding
 * needs one.
 *
 * @return ROBO_OK and @p out; ROBO_INVALID_ARGUMENT for NULL arguments. This function
 *         cannot fail on a document: every declaration has a stated answer.
 */
robo_result_t robo_layout_padding_resolve(const robo_layout_paddings_t *paddings,
                                          robo_layout_padding_edges_t *out,
                                          robo_layout_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_PADDING_H */
