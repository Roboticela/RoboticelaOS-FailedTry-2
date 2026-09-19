/*
 * RoboLayout borders (Phase 10.11).
 *
 * The third inset, and the phase that finishes the box model: a border sits between
 * a margin and a padding, it belongs to the element's **own** box exactly as a
 * padding does and never to the space beside it as a margin does, and it changes
 * both numbers 10.10 made distinct -- how big the box is, and where its contents
 * begin.
 *
 *   a margin     is OUTSIDE the box  -- it moves the box and grows the space beside it
 *   a border     is PART OF the box  -- it grows the box and moves its contents in
 *   a padding    is INSIDE the box   -- it grows the box and moves its contents in
 *
 * Which makes this phase the mirror of 10.10 rather than a new shape, and it is
 * worth saying so plainly: a border and a padding do the *same two things* to a
 * box. What differs is only where they sit, and that difference is one number -- the
 * edge a paint pass draws its line on -- so the whole of this phase is a second
 * layer of the same arithmetic, composed rather than duplicated.
 *
 * The prediction 10.10 left, and the correction it needed
 *
 * RoboLayoutPadding.h closed with:
 *
 *   "The doors below are named for what they add so that 10.11 can extend them
 *   rather than rename them."
 *
 * That was half right, and which half was wrong is worth recording. The **content
 * width** door did extend cleanly: it takes a width and subtracts what it owns,
 * and a caller that has already subtracted the border hands it a padding box. The
 * **`..._border_box`** door did not, and the reason is that it was named for the box
 * it *hoped* to complete rather than for the box it computed: it returns
 * `content + padding`, which is the **padding box**. That was an unobservable
 * misnomer while a padding was the only inset -- the two boxes were one number --
 * and it became an observable one the moment a border existed, because paint draws
 * the border box's edge and would have drawn it `border-width` too far in.
 *
 * So the door moved to `robo_layout_padding_edges_padding_box`, and this phase's
 * `robo_layout_border_edges_border_box` is the composition above it. One name was
 * corrected and no arithmetic was: the numbers the padding layer produced are
 * unchanged, and every case 10.10 wrote about them passes with the new spelling.
 * That is the "small compatibility change strictly required" the phase's brief
 * allows, and the honest reading of it is that the *name* was the defect.
 *
 * The composition, in both directions
 *
 * Downward, one subtraction per layer, each owned by the layer that knows the
 * property:
 *
 *   the containing block's content box, less the margins (10.9)
 *          |                        -> the space the box's own box may occupy
 *          |  less the border       -> the PADDING box width   (this file)
 *          |  less the padding      -> the CONTENT width       (10.10)
 *          v
 *   what an `auto` width fills, and what the children are laid out into
 *
 * Upward, one addition per layer, so the result a caller reads is the box a paint
 * pass can draw without re-deriving anything:
 *
 *   the content box the sizing layer resolved   (10.8)
 *          |  plus the padding                  -> the PADDING box (10.10)
 *          |  plus the border                   -> the BORDER box  (this file)
 *          v
 *   `robo_layout_result_t::size`
 *
 * CSS's own constraint equation is why the downward direction has a border term at
 * all, and it is the same sentence 10.10 wrote about padding one layer down: a box
 * with `border: 5px solid` and no declared width is exactly as wide as its
 * containing block, not 10px wider.
 *
 * Three kinds, and the same three a padding has -- because the grammar is the same
 *
 *   ABSENT       the side a style declared **nothing** for. CSS's initial border
 *                width is `medium`, and this build uses a zero for it in geometry
 *                and counts it nowhere -- which is the only place it is
 *                distinguishable from the next kind, and the reason 10.9 learned to
 *                keep them apart.
 *
 *   RESOLVED     a px length this build can use, **non-negative**, in
 *                @ref robo_layout_border_width::length.
 *
 *   UNRESOLVED   declared, and not usable here: a percentage, a relative unit, a
 *                negative length, or the `auto` the grammar does not allow.
 *
 * There is no `AUTO` kind for the reason RoboLayoutPadding.h gives at length: the
 * shared classification can answer it, the CSS grammar has no `border-width: auto`,
 * and folding it onto `UNRESOLVED` is the honest report of a style that broke its
 * own grammar rather than a fourth kind invented for it.
 *
 * A border width's *used* value is not its declared one, and that is this phase's
 * one genuinely new rule
 *
 * A padding's used value was its declared value; a margin's was a distribution. A
 * border's is a *decision*, and CSS states it: **if `border-style` is `none` or
 * `hidden`, the used border width is zero**, whatever `border-width` says. So a box
 * with `border: 5px none` occupies exactly as much room as one with no border
 * declaration at all, and `border: 5px solid` occupies five on that side.
 *
 * That is what `robo_css_border_style_generates_border` exists for -- 9.15's own
 * header calls it "the one fact about a style that a later phase must have" -- and
 * this phase is that later phase, for **geometry** as much as for paint. A build
 * that used the declared width would give a `border: 5px none` box ten extra pixels
 * of width and a painted nothing to show for them, which is the single most
 * plausible way to get this phase wrong.
 *
 * The width keywords, and the one place this build picks a number
 *
 * `thin`, `medium` and `thick` compute as **keywords** in RoboCSS -- 9.15 keeps the
 * vocabulary intact rather than resolving it, which is right, because the used
 * value is a rendering decision and not a parse -- so the number has to come from
 * somewhere to be geometry. CSS leaves the three "implementation-defined but
 * constant"; this build uses **1, 3 and 5** px, which is what the browsers do, and
 * states them as named constants rather than inline literals so that a later goal
 * changes one place.
 *
 * The alternative -- recording the keyword as `UNRESOLVED` and using zero -- was
 * rejected because it would make `border: thin solid` draw *nothing* in every
 * document, and a keyword the specification defines as a length growing a box by
 * nothing is a worse lie than a number this build chose and documented.
 *
 * The style and the colour are carried **with** the width, and that is paint's
 * requirement rather than geometry's
 *
 * @ref robo_layout_border_edges_t carries, per side, the used width *and* the style
 * and colour that produced it. Geometry needs only the first. A paint pass needs
 * all three at the same moment -- it decides whether to draw, which of the ten
 * styles to draw and in what colour -- and it must not have to decide them from a
 * style, because "is this declaration one this build could use" is a decision this
 * layer has already made once. Carrying them costs twelve bytes a side and removes
 * a second reader of the same declarations, which is the module's standing rule.
 *
 * What is deliberately absent
 *
 *   **`box-sizing: border-box`** -- this build is `content-box`, CSS's initial
 *   value, exactly as 10.10 left it. A declared `width: 200px` with `border: 5px
 *   solid` and `padding: 5px` is 200 wide *inside* and 220 outside.
 *
 *   **`border-radius`**, and any corner geometry -- a border here is four
 *   trapezoids' worth of arithmetic reduced to four numbers, and a corner is a later
 *   goal's problem altogether.
 *
 *   **`hidden`'s collapsing meaning.** `none` and `hidden` are distinct values that
 *   answer `generates_border` the same way, and CSS's difference between them is a
 *   border-collapsing rule for tables -- which this build has no tables for. They
 *   stay distinct so that the day tables arrive the fact is still recorded.
 *
 *   **percentage border widths**, which CSS does not define for a border width at
 *   all (the grammar has lengths and the three keywords) -- so an `UNRESOLVED`
 *   width here is a relative unit, a `calc()`, a negative length or the forbidden
 *   `auto`, and never a percentage this build declined to resolve. That is a
 *   difference from 10.10's padding vocabulary worth noting, where a percentage is
 *   legal CSS and is counted as a declaration the build cannot yet honour.
 *
 *   **border collapsing** (10.9's still-absent margin collapsing, and its table
 *   cousin), and **any border applies to a transparent inline box**: its rectangle
 *   is the union of its contents, so it takes no border for the reason it takes no
 *   margin and no padding.
 */

#ifndef ROBO_LAYOUT_BORDER_H
#define ROBO_LAYOUT_BORDER_H

#include <stddef.h>
#include <stdint.h>

#include "RoboCSS/RoboCSSBorder.h"
#include "RoboCSS/RoboCSSBox.h"
#include "RoboCSS/RoboCSSColor.h"
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
 * What one side's `border-*-width` declaration holds, before anybody decides what to
 * do with it.
 *
 * Deliberately the same three kinds as 10.10's padding and for the same two reasons:
 * CSS's initial value means an undeclared side and a declared zero are one used value
 * and two facts, and the grammar has no `auto`. `ABSENT` is **0** so that a cleared
 * record holds the side's initial state -- which is what
 * @ref robo_layout_borders_init writes and what a caller who only cleared its struct
 * has -- and a vocabulary whose zero meant `RESOLVED` would make a forgotten
 * initialisation a claim.
 */
typedef enum robo_layout_border_width_kind {
    /** Nobody declared this side. Used as a zero in geometry, and counted nowhere. */
    ROBO_LAYOUT_BORDER_WIDTH_ABSENT = 0,
    /** A usable, **non-negative** px length in @ref robo_layout_border_width::length. */
    ROBO_LAYOUT_BORDER_WIDTH_RESOLVED = 1,
    /** Declared, and not a value this build can use. */
    ROBO_LAYOUT_BORDER_WIDTH_UNRESOLVED = 2
} robo_layout_border_width_kind_t;

/** Number of defined border width kinds. */
#define ROBO_LAYOUT_BORDER_WIDTH_KIND_COUNT 3u

/** Non-zero when @p kind is one of the three above. */
int robo_layout_border_width_kind_is_known(robo_layout_border_width_kind_t kind);

/** Name of @p kind; never NULL; "unknown" when undefined. */
const char *robo_layout_border_width_kind_name(robo_layout_border_width_kind_t kind);

/**
 * The used px width CSS's three width keywords name, in this build.
 *
 * CSS defines `thin`, `medium` and `thick` as "implementation-defined but constant",
 * and these are this build's constants: **1, 3 and 5**, which is what the browsers
 * use and what a page designed against them expects. 9.15 keeps the keywords as
 * keywords -- the right call, because a used width is a rendering decision and not a
 * parse -- so the number is chosen *here*, once, where geometry first needs it.
 */
#define ROBO_LAYOUT_BORDER_THIN_PX 1
#define ROBO_LAYOUT_BORDER_MEDIUM_PX 3
#define ROBO_LAYOUT_BORDER_THICK_PX 5

/**
 * One side's declared border width.
 *
 * A record by value and owned by nobody, like every other model record here: no
 * allocation, safe to copy and to put on the stack. `length` is meaningful only when
 * `kind` is `RESOLVED` -- reading it in the other two states is reading a field that
 * means nothing, which is why @ref robo_layout_border_width_length reports a *miss*
 * rather than handing back the zero it happens to hold.
 *
 * Note what it is **not**: it is not the used width. `border-width: 5px` with
 * `border-style: none` is a declared five and a used zero, and the two are one field
 * apart on purpose -- the declaration is a fact about a style, the used value is a
 * conclusion about a box (@ref robo_layout_border_edges_t).
 */
typedef struct robo_layout_border_width {
    /** What the style declared for this side. */
    robo_layout_border_width_kind_t kind;
    /** The declared length in px, meaningful only for `RESOLVED`. Non-negative by
     *  construction; @ref robo_layout_border_width_is_valid is what says so. */
    robo_gfx_pixel_x_t length;
} robo_layout_border_width_t;

/** A `RESOLVED` width of zero: what `border-width: 0` is, and *a* default rather
 *  than the absence of one. */
robo_layout_border_width_t robo_layout_border_width_zero(void);

/** The side nobody declared: what a cleared record holds and what
 *  @ref robo_layout_borders_init writes. */
robo_layout_border_width_t robo_layout_border_width_absent(void);

/** A `RESOLVED` width of @p length px. A negative @p length is *stored* and reported
 *  invalid by @ref robo_layout_border_width_is_valid -- a checker's counterpart, not
 *  a clamp. */
robo_layout_border_width_t robo_layout_border_width_resolved(robo_gfx_pixel_x_t length);

/** A declaration this build cannot use: a relative unit, a `calc()`, a negative
 *  length, `auto`. */
robo_layout_border_width_t robo_layout_border_width_unresolved(void);

/** Non-zero when @p width is a resolvable length. */
int robo_layout_border_width_is_resolved(const robo_layout_border_width_t *width);

/** Non-zero when @p width is a side nobody declared. */
int robo_layout_border_width_is_absent(const robo_layout_border_width_t *width);

/** Non-zero when @p width is a declaration this build cannot use. */
int robo_layout_border_width_is_unresolved(const robo_layout_border_width_t *width);

/** Non-zero when @p width is a record this build may read: a known kind and, for
 *  `RESOLVED`, a length that is finite and **non-negative**. A border width is never
 *  negative -- CSS has no such border, and a negative one would shrink the box it
 *  belongs to. */
int robo_layout_border_width_is_valid(const robo_layout_border_width_t *width);

/**
 * The length of a `RESOLVED` @p width.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND when
 *         @p width is not `RESOLVED` (@p out is untouched) -- a different answer from
 *         a length of zero, which is a border an author asked not to have.
 */
robo_result_t robo_layout_border_width_length(const robo_layout_border_width_t *width,
                                              robo_gfx_pixel_x_t *out);

/** Non-zero when @p a and @p b are the same declaration. Every field, and for a kind
 *  that carries a length that includes the length. */
int robo_layout_border_width_equal(const robo_layout_border_width_t *a,
                                   const robo_layout_border_width_t *b);

/* ------------------------------------------------------------------ */
/* One side                                                            */
/* ------------------------------------------------------------------ */

/**
 * One side of a border as the **style** declared it: the width, the style and the
 * colour, which are the three properties `border-top` distributes to.
 *
 * The style is 9.15's own enumerator rather than the keyword's text, and the colour
 * is 9.9's own record rather than a channel set: this module reads two vocabularies
 * that already exist and owns neither. A **cleared** side is
 * @ref robo_layout_border_side_none -- an absent width, `STYLE_NONE` and a `NONE`
 * colour -- which is CSS's initial state for the three properties read separately
 * (`medium none currentColor`), arrived at from an undeclared style.
 *
 * Why the colour's kind is enough, and no extra "was it declared" flag is needed:
 * `ROBO_CSS_COLOR_NONE` means nobody declared a colour, `RGBA` means the style
 * resolved one, and `CURRENT` means the author wrote `currentColor` and the
 * computed layer kept it contextual. The record is self-describing, so a paint pass
 * reads it rather than a second flag beside it.
 */
typedef struct robo_layout_border_side {
    /** The declared width of this side. */
    robo_layout_border_width_t width;
    /** The declared style of this side (9.15). `NONE` when nobody declared one. */
    robo_css_border_style_t style;
    /** The declared colour of this side (9.9). `NONE` when nobody declared one. */
    robo_css_color_t color;
} robo_layout_border_side_t;

/** The side nobody declared: an absent width, `none`, and no colour. */
robo_layout_border_side_t robo_layout_border_side_none(void);

/** Non-zero when @p side is a record this build may read. */
int robo_layout_border_side_is_valid(const robo_layout_border_side_t *side);

/**
 * Non-zero when @p side draws anything: the style generates a border **and** the
 * declared width resolves to something above zero.
 *
 * The declared question, not the used one -- it says what the style asked for. The
 * used answer is on @ref robo_layout_border_edges_t, where a side that draws nothing
 * has a used width of zero, and the difference between the two is exactly the
 * `border: 5px none` case the phase's opening note argues.
 */
int robo_layout_border_side_generates(const robo_layout_border_side_t *side);

/** Non-zero when @p a and @p b are the same declaration: all three facts, and for the
 *  colour the whole record including its kind, so a `currentColor` and a resolved one
 *  that happened to share channels stay different values. */
int robo_layout_border_side_equal(const robo_layout_border_side_t *a,
                                  const robo_layout_border_side_t *b);

/* ------------------------------------------------------------------ */
/* The four sides                                                      */
/* ------------------------------------------------------------------ */

/**
 * The four sides of one box, in CSS's own order.
 *
 * The order is `robo_css_box_side_t`'s and the enum is **borrowed** rather than
 * restated, for the reason this module borrows `display`, `position` and the padding
 * vocabulary's order: top, right, bottom, left is the order an author's shorthand is
 * read in, and a second definition of it in one product is one place for a
 * transposed `border-width: 1px 2px` to hide.
 */
typedef struct robo_layout_borders {
    robo_layout_border_side_t top;
    robo_layout_border_side_t right;
    robo_layout_border_side_t bottom;
    robo_layout_border_side_t left;
} robo_layout_borders_t;

/** Clear @p borders to four **undeclared** sides -- the state in which CSS's initial
 *  values apply because nobody declared anything. */
void robo_layout_borders_init(robo_layout_borders_t *borders);

/**
 * Read @p style's twelve border longhands.
 *
 * The one place a computed style becomes a border vocabulary, and the reason
 * RoboLayout holds no CSS parser: 9.15 already expanded `border`, `border-top` and
 * `border-width: 1px 2px 3px 4px` into twelve longhand declarations, the cascade
 * already chose among them and inheritance already resolved what it applies to, so
 * `border: 1px solid black` arrives here as four widths, four styles and four
 * colours and this function reads a dozen entries.
 *
 * A NULL @p style is not a failure: a box with no style has made no declarations, so
 * the answer is four undeclared sides -- which is what a caller's initialised record
 * says too, and is the state the geometry reads as no border.
 *
 * The three readers are deliberately different, because the three properties have
 * three different computed shapes (RoboLayoutBorder.c argues each): a width is a
 * length *or* one of three keywords, a style is a typed enumerator, and a colour is
 * a typed record.
 */
robo_layout_borders_t robo_layout_borders_of(const robo_css_computed_style_t *style);

/**
 * The side @p borders holds for @p side.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p out or @p borders;
 *         ROBO_NOT_FOUND for a side that is not one of the four (@p out untouched).
 */
robo_result_t robo_layout_borders_side(const robo_layout_borders_t *borders,
                                       robo_css_box_side_t side,
                                       robo_layout_border_side_t *out);

/** Non-zero when both records hold the same four declarations, side by side. */
int robo_layout_borders_equal(const robo_layout_borders_t *a,
                              const robo_layout_borders_t *b);

/** Non-zero when all four sides are valid records. */
int robo_layout_borders_is_valid(const robo_layout_borders_t *borders);

/* ------------------------------------------------------------------ */
/* The used values                                                     */
/* ------------------------------------------------------------------ */

/**
 * One side's **used** border: the width it occupies, and the style and colour that
 * produced it.
 *
 * `width` is the used value and not the declared one, which is the rule this phase
 * adds: zero when the style draws nothing, so a `border: 5px none` side is
 * indistinguishable here from one nobody declared -- correctly, because that is what
 * it costs the layout.
 *
 * A plain `robo_gfx_pixel_x_t` rather than a @ref robo_layout_border_width_t, because
 * a used width is not a declaration: it has no kind, no `auto` and no "unresolved",
 * and giving it a vocabulary of three kinds to hold one number would invite a caller
 * to read a kind that means nothing.
 */
typedef struct robo_layout_border_used_side {
    /** The width this side occupies, in px: non-negative, zero when it draws nothing. */
    robo_gfx_pixel_x_t width;
    /** The style that decided it (9.15), carried for paint. */
    robo_css_border_style_t style;
    /** The colour of it (9.9), carried for paint. A `CURRENT` colour is still
     *  contextual here: only the layer with an element can resolve one. */
    robo_css_color_t color;
} robo_layout_border_used_side_t;

/**
 * What the four declarations came to: the used border, per side, and the counters.
 *
 * The record the two derived doors take, the record the object carries and the record
 * a paint pass reads. Geometry wants the four widths and nothing else; paint wants the
 * widths *and* the styles and colours, and gets all of it here rather than from a
 * style, because which declarations this build could use is a question this layer has
 * already answered once.
 *
 * Every `width` is **non-negative**. That is not a clamp applied late; it is a
 * property of the kinds the resolver accepts and of the `generates_border` rule, and
 * @ref robo_layout_border_edges_is_valid states it so that a hand-built record cannot
 * smuggle a negative side into a size.
 *
 * The three counters answer three different questions, and the third is the one only
 * this record can answer:
 *
 *   `widths_resolved`    a side the style declared a usable width for, `0` included;
 *   `widths_unresolved`  a declaration this build could not use. Its used value is 0;
 *   `borders_drawn`      a side whose **used** width exceeds zero -- a border that
 *                        occupies space and will paint. Not derivable from the four
 *                        widths by a caller that wants "how many borders does this
 *                        document have", because the zero it must compare against is
 *                        `widths_resolved`'s answer too.
 *
 * `widths_resolved + widths_unresolved <= 4`, and the remainder is the `ABSENT`
 * sides. `borders_drawn <= widths_resolved`.
 */
typedef struct robo_layout_border_edges {
    robo_layout_border_used_side_t top;
    robo_layout_border_used_side_t right;
    robo_layout_border_used_side_t bottom;
    robo_layout_border_used_side_t left;
    /** Sides whose declared width reached this layer, zero included. */
    size_t widths_resolved;
    /** Sides declared with a width this build cannot use. */
    size_t widths_unresolved;
    /** Sides whose used width is above zero: a border that takes room and draws. */
    size_t borders_drawn;
} robo_layout_border_edges_t;

/** Clear @p edges to no border at all on any side. */
void robo_layout_border_edges_init(robo_layout_border_edges_t *edges);

/** Non-zero when @p edges describe something a size can hold: four non-negative
 *  widths, counters that agree with four sides, and a drawn count no larger than the
 *  resolved one. */
int robo_layout_border_edges_is_valid(const robo_layout_border_edges_t *edges);

/**
 * Non-zero when @p a and @p b hold the same used values *and* the same counters.
 *
 * Every field, for the reason 10.9's and 10.10's comparisons give: the record is a
 * conclusion rather than a request, and two resolutions that agreed about the numbers
 * and disagreed about how many declarations they could not use would be two different
 * answers to one document. The styles and colours are compared too, because two
 * records that drew the same lines in different colours are not the same border.
 */
int robo_layout_border_edges_equal(const robo_layout_border_edges_t *a,
                                   const robo_layout_border_edges_t *b);

/**
 * The **used** width of @p side.
 *
 * The one-line reader the derived doors and the suite both use, so that "which field
 * of the used record is the width" is asked in one place rather than spelled out at
 * every arithmetic site -- four of those would be four chances to read a style
 * instead.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a side that is not one
 *         of the four.
 */
robo_result_t robo_layout_border_edges_width(const robo_layout_border_edges_t *edges,
                                             robo_css_box_side_t side,
                                             robo_gfx_pixel_x_t *out);

/**
 * The width of the **padding box** a box of @p containing_width may have, with these
 * borders.
 *
 * The downward half of the composition, and the number 10.10's own
 * `robo_layout_padding_edges_content_width` is then handed: CSS's constraint equation
 * has a border term on each side, so a box with no declared width fills what its
 * margins *and* its borders left rather than overflowing its containing block by
 * them. One axis rather than a size, because that is how a contained width arrives.
 *
 * Floored at **zero**, once, here -- a box whose borders already exceed its containing
 * block leaves no room for a padding box, and a negative width is not a width. The
 * floor is applied to the result rather than to each term, which is the shape both
 * earlier layers use and the reason the three floors in this module all agree.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_ERROR and a
 *         diagnostic for edges that are not valid (a negative side is a record this
 *         module's own resolver never produces, so one reaching here is a caller bug
 *         and the moment to see it is before a size is derived from it).
 */
robo_result_t robo_layout_border_edges_padding_box_width(const robo_layout_border_edges_t *edges,
                                                        int32_t containing_width,
                                                        int32_t *out,
                                                        robo_layout_diag_t *diag);

/**
 * The **border box** a padding box of @p padding_box asks for, with these borders.
 *
 * The upward half of the composition and the last number before a result: 10.10 turns
 * the content box into the padding box and this turns that into the border box, which
 * is what `robo_layout_result_t::size` carries. Both axes at once because a size is
 * what a result holds, and the two are computed identically.
 *
 * Done in 64 bits and clamped to the geometry's range, because padding and border are
 * each bounded by the seam's maximum and their sum is not: a `width: 2147483647px` box
 * with a `border: 5px solid` must not wrap into a negative size. Clamped rather than
 * refused, because there is no document an author can write that some number cannot
 * describe, and a size that saturates is still a drawable rectangle. 10.10 reached the
 * same decision for the same reason one layer down; the two clamps compose because
 * each saturates at the same bound.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a negative
 *         @p padding_box (a box's own extent is never negative); ROBO_ERROR and a
 *         diagnostic for invalid edges.
 */
robo_result_t robo_layout_border_edges_border_box(const robo_layout_border_edges_t *edges,
                                                  robo_layout_size_t padding_box,
                                                  robo_layout_size_t *out,
                                                  robo_layout_diag_t *diag);

/* ------------------------------------------------------------------ */
/* The one policy                                                      */
/* ------------------------------------------------------------------ */

/**
 * Resolve @p borders to the four used values.
 *
 * Four rules, and the fourth is the one this phase adds:
 *
 *   1. an `ABSENT` width is used as **zero** and counted nowhere;
 *   2. a `RESOLVED` width is used as written, and it is non-negative by construction;
 *   3. an `UNRESOLVED` width is used as zero and counted;
 *   4. either way, the used width is **zero unless the style generates a border** --
 *      `none` and `hidden` draw nothing, and a border that draws nothing takes no
 *      room (`robo_css_border_style_generates_border`).
 *
 * Rule 4 is why a `border: 5px none` box and a box with no border declaration at all
 * are the same box here, and why the counter for a border that is actually *drawn* is
 * separate from the one for a width that was resolvable.
 *
 * No distribution and no second door: a border has no `auto`, so the same twelve
 * declarations mean the same thing to a block box and to an atomic inline, exactly as
 * 10.10 argued for padding.
 *
 * @return ROBO_OK and @p out; ROBO_INVALID_ARGUMENT for NULL arguments. This function
 *         cannot fail on a document: every declaration has a stated answer.
 */
robo_result_t robo_layout_border_resolve(const robo_layout_borders_t *borders,
                                         robo_layout_border_edges_t *out,
                                         robo_layout_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_BORDER_H */
