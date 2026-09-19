/*
 * RoboLayout margins (Phase 10.9).
 *
 * What an author asks for around a box, and the one arithmetic that turns that
 * declaration into the space the box actually occupies.
 *
 * 10.8 answered *how big* a box is. This phase answers *how much room it takes*,
 * which is a different number in exactly one place: a margin is outside the box, so
 * it never changes a box's own `size` -- it changes where the box sits, how much of
 * its containing block is left for its contents, and how far the next box down
 * starts. That separation is the phase, and it is why nothing in this header is a
 * dimension: a `robo_layout_dimension_t` describes a box's content box, and a
 * margin is the gap beside it.
 *
 * The four kinds, and why `auto` is not zero
 *
 *   AUTO         the keyword. It *distributes* in the one place CSS lets it -- the
 *                horizontal margins of a block-level box in normal flow, where it
 *                takes the space the declared width left over. That is how
 *                `margin: 0 auto` centres a box, and it is the only reason this
 *                kind exists.
 *
 *   RESOLVED     a px length this build can use, in @ref robo_layout_margin::length.
 *                **Signed**, and that is the one respect in which this vocabulary
 *                differs from 10.8's: `margin: -10px` is legal CSS and a real layout
 *                technique, while a negative `width` is neither.
 *
 *   UNRESOLVED   declared, and not usable here: a percentage, a relative unit, a
 *                `calc()` this build has no basis for. It contributes **zero** to
 *                the geometry and is *counted* instead, which is 10.8's rule for a
 *                dimension it cannot resolve -- an unresolved declaration must not
 *                be able to fail a document, and it must not be silently a
 *                confident zero either.
 *
 *   ABSENT       the side a style declared **nothing** for. This is CSS's initial
 *                value, and the geometry uses a zero for it -- which is why it looks
 *                like the first kind and is not it. The two are distinguishable in
 *                exactly one place: the *counters* below, and the statistics built on
 *                them. "How many of this document's margins did an author write?" is
 *                a question a debug surface has to be able to answer, and a vocabulary
 *                that folded ABSENT into RESOLVED would answer "four" for every box in
 *                every document -- including one that declared no margin at all. 10.8
 *                reached the same split from the other side, where "no declaration" is
 *                `ROBO_LAYOUT_DIMENSION_SOURCE_NONE` and is likewise not a length.
 *
 * So a cleared record and `margin: 0` are **one used value and two facts**, and the
 * record keeps the fact that a reader of the numbers cannot see. That is the same
 * bargain 9.11's `margin` shorthand already struck one layer down, where a declared `0`
 * and an undeclared side are one computed value and two declarations.
 *
 * Two policies, and why this is not one function with a flag
 *
 * The same four declarations mean different things to a block box and to an
 * inline-level one, and CSS is explicit about both:
 *
 *   `robo_layout_margins_resolve_block`  a block-level box in normal flow. `auto`
 *                                        on a horizontal side takes a share of the
 *                                        leftover space; `auto` on a vertical side
 *                                        is **zero**, because the keyword only
 *                                        means something for an absolutely
 *                                        positioned box (10.12).
 *
 *   `robo_layout_margins_resolve_inline` an atomic inline's margins. Every `auto`
 *                                        is **zero**, because distribution is a
 *                                        block-level normal-flow rule and an
 *                                        inline-level box has no containing block to
 *                                        be over- or under-constrained against.
 *
 * Two doors rather than one with a flag for the reason 10.8 kept two readers: the
 * difference *is* the two CSS rules, and a flag would let a caller pass the wrong
 * one and get a plausible answer.
 *
 * What is deliberately absent
 *
 *   **Margin collapsing** — this build adds margins up and nothing else. Adjacent
 *   vertical margins, a parent's top margin with its first child's, and an empty
 *   box's own pair all keep their full sum. That is stated here, asserted by the
 *   suite, and named in `layout/docs/Margins.md` §6 as the next thing a later goal
 *   will change; the *shape* this header gives it is that collapsing is a rule over
 *   the four used values and not a rule over the declarations, which is why
 *   `robo_layout_margin_edges_t` is a record of numbers rather than another
 *   vocabulary.
 *
 *   **Percentage resolution** — `margin: 5%` resolves against the containing
 *   block's *width* in CSS, for every side. This build has a containing width, so it
 *   arguably could; it does not, because 10.8 left every percentage unresolved and
 *   resolving one here would make `margin: 5%` the only percentage in the module that
 *   means something. It is `UNRESOLVED` and counted, and the phase that resolves
 *   percentages owns all of them at once.
 *
 *   **Padding and borders** — 10.10 and 10.11. So a box's margin box is its border
 *   box plus these four numbers, and its border box *is* its content box, which is
 *   the identity that changes when padding arrives.
 */

#ifndef ROBO_LAYOUT_MARGIN_H
#define ROBO_LAYOUT_MARGIN_H

#include <stddef.h>
#include <stdint.h>

#include "RoboCSS/RoboCSSBox.h"
#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutSizing.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * What one side's declaration holds, before anybody decides what to do with it.
 *
 * `ABSENT` is **0** deliberately, and the reason is the one RoboCSS gives for making
 * `position: static` the zero of its own vocabulary: a cleared or zeroed record then
 * holds the side's **initial state**, which is what `robo_layout_margins_init` writes
 * and what a caller who only cleared its struct has. A vocabulary whose zero meant
 * `auto` would make a forgotten initialisation a *distribution* -- a box that centred
 * itself for no stated reason.
 */
typedef enum robo_layout_margin_kind {
    /** Nobody declared this side. CSS's initial value is `0`, so this is a zero in
     *  geometry and an *absence* in the counters. */
    ROBO_LAYOUT_MARGIN_ABSENT = 0,
    /** A usable px length in @ref robo_layout_margin::length; may be negative. */
    ROBO_LAYOUT_MARGIN_RESOLVED = 1,
    /** The keyword `auto`. Distributes on a block-level box's horizontal sides. */
    ROBO_LAYOUT_MARGIN_AUTO = 2,
    /** Declared, and not a value this build can use: a percentage, a relative unit. */
    ROBO_LAYOUT_MARGIN_UNRESOLVED = 3
} robo_layout_margin_kind_t;

/** Number of defined margin kinds. */
#define ROBO_LAYOUT_MARGIN_KIND_COUNT 4u

/** Non-zero when @p kind is one of the three above. */
int robo_layout_margin_kind_is_known(robo_layout_margin_kind_t kind);

/** Name of @p kind; never NULL; "unknown" when undefined. */
const char *robo_layout_margin_kind_name(robo_layout_margin_kind_t kind);

/**
 * One side's margin, as the style declared it.
 *
 * A record by value and owned by nobody, like every other model record here: no
 * allocation, safe to copy and to put on the stack. `length` is meaningful only
 * when `kind` is `RESOLVED` -- reading it in the other two states is reading a field
 * that means nothing, which is why `robo_layout_margin_length` reports a *miss*
 * rather than handing back the zero it happens to hold.
 */
typedef struct robo_layout_margin {
    /** What the style declared for this side. */
    robo_layout_margin_kind_t kind;
    /**
     * The declared length in px, **signed**, meaningful only for `RESOLVED`.
     *
     * Signed in both directions: a negative margin pulls, and the arithmetic in
     * `robo_layout_margins_resolve_block` is written so that a negative value never
     * has to be clamped to keep a size non-negative. It is the *available content
     * width* that is floored, once, at the end.
     */
    robo_gfx_pixel_x_t length;
} robo_layout_margin_t;

/**
 * The four sides of one box, in CSS's own order.
 *
 * The order is `robo_css_box_side_t`'s and the enum is **borrowed** from 9.11 rather
 * than restated, for the reason this module borrows `display` from 9.12: top, right,
 * bottom, left is the order an author's shorthand is read in, and two definitions of
 * it in one product is one place for a transposed `margin: 10px 20px` to hide.
 */
typedef struct robo_layout_margins {
    robo_layout_margin_t top;
    robo_layout_margin_t right;
    robo_layout_margin_t bottom;
    robo_layout_margin_t left;
} robo_layout_margins_t;

/* ------------------------------------------------------------------ */
/* One side                                                            */
/* ------------------------------------------------------------------ */

/** A `RESOLVED` margin of zero: what `margin: 0` is, and *a* default rather than the
 *  absence of one. */
robo_layout_margin_t robo_layout_margin_zero(void);

/**
 * The side nobody declared.
 *
 * What a cleared record holds and what `robo_layout_margins_init` writes, because CSS's
 * initial value for every margin is `0` and this is the state in which that value is
 * used without anybody having said so.
 */
robo_layout_margin_t robo_layout_margin_absent(void);

/** A `RESOLVED` margin of @p length px. Negative is legal and is why this is signed. */
robo_layout_margin_t robo_layout_margin_resolved(robo_gfx_pixel_x_t length);

/** The keyword `auto`. */
robo_layout_margin_t robo_layout_margin_auto(void);

/** A declaration this build cannot use: a percentage, a relative unit. */
robo_layout_margin_t robo_layout_margin_unresolved(void);

/** Non-zero when @p margin is the `auto` kind. */
int robo_layout_margin_is_auto(const robo_layout_margin_t *margin);

/** Non-zero when @p margin is a resolvable length. */
int robo_layout_margin_is_resolved(const robo_layout_margin_t *margin);

/** Non-zero when @p margin is a side nobody declared. */
int robo_layout_margin_is_absent(const robo_layout_margin_t *margin);

/** Non-zero when @p margin is a declaration this build cannot use. */
int robo_layout_margin_is_unresolved(const robo_layout_margin_t *margin);

/**
 * Non-zero when @p margin is one of the three kinds with a length this build can put
 * into geometry: every kind is, which is why this is about the *record* rather than
 * about the declaration.
 *
 * The three checks are the three fields, and the reason a validity predicate exists
 * at all is the one 10.8 gives for its dimension: a caller may hand this module a
 * record it built itself, and "is this something I may read?" should have an answer
 * that does not depend on how it was built.
 */
int robo_layout_margin_is_valid(const robo_layout_margin_t *margin);

/**
 * The length of a `RESOLVED` @p margin.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND when
 *         @p margin is not `RESOLVED` (@p out is untouched) -- a different answer from
 *         a length of zero, which is a box an author asked to have no gap around.
 */
robo_result_t robo_layout_margin_length(const robo_layout_margin_t *margin,
                                        robo_gfx_pixel_x_t *out);

/**
 * Non-zero when @p a and @p b are the same declaration.
 *
 * Every field, and for a kind that carries a length that includes the length: two
 * margins are equal when a caller could not tell them apart, which is the same
 * definition 10.8 uses for a dimension and is what makes this usable in a test that
 * wants to assert a declaration survived a round trip rather than a geometry.
 */
int robo_layout_margin_equal(const robo_layout_margin_t *a, const robo_layout_margin_t *b);

/* ------------------------------------------------------------------ */
/* The four sides                                                      */
/* ------------------------------------------------------------------ */

/**
 * Clear @p margins to four **`ABSENT`** sides -- the state in which CSS's initial value
 * `0` applies to every side because nobody declared one.
 */
void robo_layout_margins_init(robo_layout_margins_t *margins);

/**
 * Read @p style's four margin longhands.
 *
 * The one place a computed style becomes a margin vocabulary, and the reason
 * RoboLayout holds no CSS parser: 9.11 already resolved the shorthand, the cascade
 * and the inheritance, so `margin: 10px 20px` arrives here as four computed
 * longhands and this function reads four entries and classifies each.
 *
 * A NULL @p style is not a failure: a box with no style has made no declarations, so
 * the answer is four `ABSENT` sides -- which is what a caller's initialised record says
 * too, and is the state the geometry reads as the initial value `0`.
 *
 * A longhand **absent** from the computed style is `ABSENT` for the same reason: a side
 * nobody wrote a declaration for is the side the initial value applies to, and it is
 * neither a confident zero nor a declaration this build could not use.
 */
robo_layout_margins_t robo_layout_margins_of(const robo_css_computed_style_t *style);

/**
 * The margin @p margins holds for @p side.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p out or @p margins;
 *         ROBO_NOT_FOUND for a side that is not one of the four (@p out untouched).
 */
robo_result_t robo_layout_margins_side(const robo_layout_margins_t *margins,
                                       robo_css_box_side_t side,
                                       robo_layout_margin_t *out);

/** Non-zero when both records hold the same four declarations, side by side. */
int robo_layout_margins_equal(const robo_layout_margins_t *a,
                              const robo_layout_margins_t *b);

/** Non-zero when all four sides are valid records. */
int robo_layout_margins_is_valid(const robo_layout_margins_t *margins);

/* ------------------------------------------------------------------ */
/* The used values                                                     */
/* ------------------------------------------------------------------ */

/**
 * What the four declarations came to, in the coordinate space a result is written in.
 *
 * The *used* values rather than the declared ones, which is the distinction the whole
 * phase turns on: `margin: auto` is not a number until somebody decides what the
 * containing block left over, and a percentage is never a number in this build.
 *
 * `top`, `right`, `bottom` and `left` are signed px. `available_content_width` is the
 * one derived number: the containing block's content width with the two horizontal
 * margins taken out, **floored at zero**, which is what a box's own `auto` width and
 * its children are resolved against. It is floored rather than refused because the
 * alternative is a negative available width, and a size arithmetic that can go
 * negative is a size arithmetic that can produce a rectangle nobody can draw. A
 * *negative margin* is still honoured in the edges themselves -- `left` may be
 * negative, and the box then overflows its containing block to the left, which is
 * what CSS does.
 *
 * The three counters partition the four sides, and the partition is the honest part:
 *
 *   `resolved`    a side the style declared a usable length for, `margin: 0` and a
 *                 negative one included.
 *   `autos_used`  a side that was `auto` and was **given a distributed value** here.
 *                 Zero on the inline policy by rule, and zero for a vertical `auto`
 *                 even on the block policy.
 *   `unresolved`  a declaration this build could not use. Its used value is 0.
 *
 * `resolved + autos_used + unresolved <= 4`, and the three do not have to add up to the
 * four sides: the remainder is the `ABSENT` ones, plus the `auto`s that took no
 * distributed value. Neither is a declaration that reached geometry, and both are
 * visible in the numbers above -- the first as the initial value, the second as a zero
 * beside an `auto` whose sibling was distributed to.
 *
 * The counters are the **one place** the `ABSENT`/`RESOLVED` distinction survives, and
 * that is why they are worth a kind: `stats.block.margins_applied` is this field added
 * over every placed box, and a record that counted an undeclared side as a used length
 * would report four margins for every box in every document.
 */
typedef struct robo_layout_margin_edges {
    robo_gfx_pixel_x_t top;
    robo_gfx_pixel_x_t right;
    robo_gfx_pixel_x_t bottom;
    robo_gfx_pixel_x_t left;
    /** The containing block's content width less @ref left and @ref right; never negative. */
    int32_t available_content_width;
    /** Sides whose declared length reached geometry, zero included. */
    size_t resolved;
    /** Sides that were `auto` and were given a value by distribution. */
    size_t autos_used;
    /** Sides declared with a value this build cannot use. */
    size_t unresolved;
} robo_layout_margin_edges_t;

/** Clear @p edges to no margins at all, with @p containing_width left available. */
void robo_layout_margin_edges_init(robo_layout_margin_edges_t *edges,
                                   int32_t containing_width);

/**
 * Non-zero when @p edges describe something a result can hold: four valid numbers,
 * an available width that is not negative, and counters that agree with four sides.
 */
int robo_layout_margin_edges_is_valid(const robo_layout_margin_edges_t *edges);

/**
 * Non-zero when @p a and @p b hold the same used values *and* the same counters.
 *
 * Every field, because the record is a conclusion rather than a request: two
 * resolutions that agreed about the numbers and disagreed about how many declarations
 * they could not use would be two different answers to one document.
 */
int robo_layout_margin_edges_equal(const robo_layout_margin_edges_t *a,
                                  const robo_layout_margin_edges_t *b);

/**
 * The horizontal extent a box occupies with these edges: `left + content_width +
 * right`.
 *
 * A signed sum, and deliberately not clamped: it is the *advance* an inline
 * participant moves the cursor by, and a negative pair of margins that makes a box
 * narrower than its content is a layout an author can ask for. Not named
 * "margin box width" because it is only that while padding and borders are absent
 * (10.10, 10.11) -- a name this function could not keep.
 *
 * @return ROBO_INVALID_ARGUMENT for a NULL @p edges.
 */
robo_result_t robo_layout_margin_edges_outer_width(const robo_layout_margin_edges_t *edges,
                                                  int32_t content_width,
                                                  int32_t *out);

/* ------------------------------------------------------------------ */
/* The two policies                                                    */
/* ------------------------------------------------------------------ */

/**
 * Resolve @p margins for a **block-level box in normal flow**.
 *
 * @p containing_width is the containing block's content width -- the parent's, or the
 * viewport for the root -- and @p width is the box's **declared** width as 10.8
 * classified it, which is what decides whether `auto` has anything to distribute.
 * Passing the dimension rather than a number is deliberate: "this box's width is
 * `auto`" and "this box's width is 0" are different documents, and the caller has the
 * dimension rather than the resolution precisely because the margins have to be
 * settled *before* the width is.
 *
 * The rules, in the order CSS states them:
 *
 *   1. an `ABSENT` side is used as **zero** and counted nowhere: CSS's initial value
 *      for a margin is `0`, and a side nobody declared is not a declaration that
 *      reached geometry;
 *   2. a `RESOLVED` side is used as written, a negative one included;
 *   3. an `UNRESOLVED` side is used as zero and counted;
 *   4. a vertical `auto` is **zero**: `margin-top: auto` distributes nothing in normal
 *      flow, and it is the positioning goal (10.12) where the keyword means something
 *      vertically;
 *   5. a horizontal `auto` distributes the **leftover**: with a definite declared
 *      width, `containing - width - (the margins that are not auto)` is split over the
 *      auto sides. Two autos split it, and an odd pixel goes to `right`, so
 *      `margin: 0 auto` in an odd leftover is one pixel left of centre -- stated
 *      because it is invisible in every test that does not look for it;
 *   6. when there is no leftover to distribute -- a non-definite width, or an
 *      over-constrained box -- a horizontal `auto` is **zero**, which is CSS's rule
 *      for the first case and the "ignore the autos and let the box overflow" reading
 *      of the second.
 *
 * @return ROBO_OK and @p out; ROBO_INVALID_ARGUMENT for NULL arguments. This function
 *         cannot fail on a document: every declaration has a stated answer.
 */
robo_result_t robo_layout_margins_resolve_block(const robo_layout_margins_t *margins,
                                               int32_t containing_width,
                                               const robo_layout_dimension_t *width,
                                               robo_layout_margin_edges_t *out,
                                               robo_layout_diag_t *diag);

/**
 * Resolve @p margins for an **inline-level participant**.
 *
 * Every `auto` is zero here, and that is a rule rather than a simplification: `auto`
 * margins distribute against a containing block in normal flow, and an inline-level
 * box has no width constraint to be under- or over-. CSS says the used value is `0`,
 * and this is where it says so.
 *
 * `available_content_width` is still computed, because an atomic inline's `auto` width
 * fills what its horizontal margins leave -- 10.4's rule, kept, with the margins now
 * subtracted from it.
 *
 * @return ROBO_OK and @p out; ROBO_INVALID_ARGUMENT for NULL arguments.
 */
robo_result_t robo_layout_margins_resolve_inline(const robo_layout_margins_t *margins,
                                                int32_t containing_width,
                                                robo_layout_margin_edges_t *out,
                                                robo_layout_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_MARGIN_H */
