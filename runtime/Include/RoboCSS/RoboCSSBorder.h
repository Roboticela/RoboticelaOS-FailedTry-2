/*
 * RoboCSS border family (Phase 9.15).
 *
 * A border is written three ways and all three have to agree:
 *
 *   border-top-width: 1px;  border-top-style: solid;  border-top-color: red;
 *   border-top: 1px solid red;
 *   border: 1px solid red;
 *
 * The second is three longhands spoken once, the third is those three on all
 * four sides. So there are *twelve* longhands — three kinds times four sides —
 * and no fewer than five declarations can set any one of them:
 *
 *   the longhand itself        `border-top-width`
 *   its positional row         `border-width`       (four sides, one kind)
 *   the side shorthand         `border-top`         (one side, three kinds)
 *   the whole-border shorthand `border`             (four sides, three kinds)
 *   (and, for nothing else)    the box model's own rows touch none of these
 *
 * Two facts follow, and they are the whole design
 *
 * *One owner per longhand.* A longhand two declarations can speak for cannot be
 * materialised by two independent passes, so this module owns all twelve and the
 * box model's distribution pass hands the three border rows over to it
 * (`robo_css_border_distributes_shorthand`). That is why `border-width` — which
 * 9.11 put on the box table provisionally, with the note that a
 * `border-width: 1px 2px` no longhand ever saw would be a declaration no later
 * phase could consume — is now distributed here, beside the two siblings it
 * shares a grammar and a competition with.
 *
 * *The cascade decides, not this module.* Two shorthands can cover one longhand
 * at different specificities, and which of them the longhand *is* comes down to
 * the same three keys as anything else. So the pass asks
 * `robo_css_cascade_candidate_compare` rather than weighing a level or comparing a
 * specificity here, exactly as the box and background passes do.
 *
 * Three things live here and nothing else
 *
 *   the style vocabulary      ten values as an ordered, closed set, each a row
 *   the kinds and the names   width / style / colour, and the twelve longhand
 *                             names read from the box table's positional rows
 *   the classifier            what one component of a composite shorthand is:
 *                             a width, a style, a colour, or a reference
 *
 * What it deliberately is not
 *
 * Not a value parser: a width is the 9.11 `LINE_WIDTH` shape's job, a colour the
 * 9.9 family's, and this module only says *which* of them a component belongs to.
 * Not a property registry: that the four styles start at `none` and that a border
 * starts at `medium none currentcolor` is a registry fact. And not geometry:
 * nothing here measures a border, paints one, or rounds a corner — `border-radius`
 * is not a value this build has, and a `none` border's used width being zero
 * regardless of `border-width` is a rule for the phase with a box.
 *
 * The `border-width` percentage, recorded rather than enforced
 *
 * `border-width: 50%` is not legal CSS, and this build computes it to a
 * percentage — 9.11 asserted that on purpose, because 9.8's contract is that a
 * value layer knows a value's *shape* and not a property's range, and the same
 * boundary is what lets `-1px` through. The classifier follows the registry
 * rather than overruling it: a percentage is length-shaped, so it classifies as a
 * width. A phase that enforces the range will enforce it in one place, and it is
 * not this one.
 */

#ifndef ROBO_CSS_BORDER_H
#define ROBO_CSS_BORDER_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSBox.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The style vocabulary                                                */
/* ------------------------------------------------------------------ */

/**
 * A border style.
 *
 * The order is CSS's own — `none`, `hidden`, then the eight drawn styles from
 * `dotted` to `outset` — and, as with the display and position vocabularies, it
 * is load-bearing rather than conventional: an enumerator is a contract a stored
 * value carries, so these are append-only and a new style arrives at the end.
 */
typedef enum robo_css_border_style {
    ROBO_CSS_BORDER_STYLE_NONE = 0,
    ROBO_CSS_BORDER_STYLE_HIDDEN = 1,
    ROBO_CSS_BORDER_STYLE_DOTTED = 2,
    ROBO_CSS_BORDER_STYLE_DASHED = 3,
    ROBO_CSS_BORDER_STYLE_SOLID = 4,
    ROBO_CSS_BORDER_STYLE_DOUBLE = 5,
    ROBO_CSS_BORDER_STYLE_GROOVE = 6,
    ROBO_CSS_BORDER_STYLE_RIDGE = 7,
    ROBO_CSS_BORDER_STYLE_INSET = 8,
    ROBO_CSS_BORDER_STYLE_OUTSET = 9
} robo_css_border_style_t;

/** Number of defined border styles. */
#define ROBO_CSS_BORDER_STYLE_COUNT 10u

/** Non-zero when @p style is one of the ten. */
int robo_css_border_style_is_known(robo_css_border_style_t style);

/** The keyword that spells @p style (`solid`); never NULL, even out of range. */
const char *robo_css_border_style_name(robo_css_border_style_t style);

/** Number of styles in the vocabulary. */
size_t robo_css_border_style_count(void);

/**
 * The style @p keyword names (ASCII case-insensitive), or
 * @ref ROBO_CSS_BORDER_STYLE_NONE when it names none of them.
 *
 * `none` is both a value and this function's answer for "not one of them", which
 * is why every *check* must go through @ref robo_css_border_style_slice_is_known:
 * asking the result would let `border-style: banana` compute to `none`, which is
 * the most damaging confusion this property has.
 */
robo_css_border_style_t robo_css_border_style_from_slice(robo_css_slice_t keyword);

/** @ref robo_css_border_style_from_slice for a NUL-terminated keyword. */
robo_css_border_style_t robo_css_border_style_from_text(const char *keyword);

/** Non-zero when @p keyword spells one of the ten, ASCII case-insensitively. */
int robo_css_border_style_slice_is_known(robo_css_slice_t keyword);

/**
 * Write @p style's keyword into @p out.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p out, a NULL @p out_length,
 *         or an unknown @p style; ROBO_NOT_SUPPORTED when @p capacity cannot hold
 *         the keyword and its NUL, with @p out_length set to what it needs — the
 *         same refusal the display and position writers give, so a caller sizes a
 *         buffer from the answer rather than from a constant.
 */
robo_result_t robo_css_border_style_write(robo_css_border_style_t style,
                                          char *out,
                                          size_t capacity,
                                          size_t *out_length,
                                          robo_css_diag_t *diag);

/**
 * Non-zero when @p style draws anything.
 *
 * The one fact about a style that a later phase must have, and the reason it is
 * asked of the row rather than derived from the enumerator: `none` and `hidden`
 * are the two that draw nothing, and a border that draws nothing has a *used*
 * width of zero whatever `border-width` says. Every other style draws.
 *
 * `hidden` and `none` differ only in border collapsing, where `hidden` outranks
 * every other style — a table-layout rule this build has no tables for, so the
 * two are kept as distinct values that answer this one question the same way.
 */
int robo_css_border_style_generates_border(robo_css_border_style_t style);

/* ------------------------------------------------------------------ */
/* The kinds                                                           */
/* ------------------------------------------------------------------ */

/**
 * One of the three things a border is made of.
 *
 * The order is the order a serialisation writes a composite shorthand in — `1px
 * solid red` — because the classifier's folding and a caller's reading of
 * `out_parts` both follow it, and a set that listed them differently would
 * silently transpose the two.
 */
typedef enum robo_css_border_kind {
    ROBO_CSS_BORDER_KIND_WIDTH = 0,
    ROBO_CSS_BORDER_KIND_STYLE = 1,
    ROBO_CSS_BORDER_KIND_COLOR = 2
} robo_css_border_kind_t;

/** Number of kinds a border is made of. */
#define ROBO_CSS_BORDER_KIND_COUNT 3u

/** Number of border longhands: four sides times three kinds. */
#define ROBO_CSS_BORDER_LONGHAND_COUNT (ROBO_CSS_BOX_SIDE_COUNT * ROBO_CSS_BORDER_KIND_COUNT)

/** Non-zero when @p kind is one of the three. */
int robo_css_border_kind_is_known(robo_css_border_kind_t kind);

/** Human-readable kind name ("width", "style", "color"); never NULL. */
const char *robo_css_border_kind_name(robo_css_border_kind_t kind);

/** Number of kinds. */
size_t robo_css_border_kind_count(void);

/** Number of border longhands this build owns. */
size_t robo_css_border_longhand_count(void);

/**
 * The longhand name for @p side and @p kind (`border-top-width`). Borrowed and
 * static, or NULL for an unknown side or kind.
 *
 * Read from the box table's positional rows rather than spelled here, so the
 * twelve names have one spelling in the build. The rows are what a walk of the
 * family is *for*: `for side, for kind` reaches every longhand, which is how a
 * test proves the registry has all twelve rather than trusting a count.
 */
const char *robo_css_border_longhand_name(robo_css_box_side_t side,
                                          robo_css_border_kind_t kind);

/** Non-zero when @p name is one of the twelve longhands, ASCII case-insensitively. */
int robo_css_border_is_longhand(robo_css_slice_t name);

/* ------------------------------------------------------------------ */
/* The positional shorthands                                           */
/* ------------------------------------------------------------------ */

/**
 * Number of *positional* border shorthands: `border-width`, `border-style` and
 * `border-color`.
 *
 * Positional because each is one kind over four sides, which is exactly the shape
 * the box model's row table already describes and exactly what its 1/2/3/4 mapping
 * distributes. They are rows of that table (`RoboCSSBox.h`) and are *distributed*
 * here, because a longhand they cover can also be set by `border` and by a side
 * shorthand and only one pass may own it.
 */
#define ROBO_CSS_BORDER_POSITIONAL_COUNT ROBO_CSS_BORDER_KIND_COUNT

/** Number of positional border shorthands. */
size_t robo_css_border_positional_count(void);

/** The positional shorthand's own name at @p index, or NULL when out of range. */
const char *robo_css_border_positional_name(size_t index);

/**
 * The kind the positional shorthand at @p index carries, or
 * @ref ROBO_CSS_BORDER_KIND_WIDTH when @p index is out of range.
 *
 * Index and kind are the same enumerator on purpose: `border-width` *is* the
 * width row, and a second table relating the two would be a place for them to
 * disagree.
 */
robo_css_border_kind_t robo_css_border_positional_kind(size_t index);

/**
 * The box-table row the positional shorthand at @p index names, or NULL when
 * @p index is out of range or the box table no longer holds it.
 *
 * Borrowed and static. The lookup is what keeps the twelve longhand names in one
 * place: the border module spells the three *shorthand* names and the box table
 * spells their longhands, and a walk of this table is what makes a drift between
 * the two visible.
 */
const robo_css_box_shorthand_t *robo_css_border_positional_row(size_t index);

/** The box-table row carrying @p kind, or NULL for an unknown kind. */
const robo_css_box_shorthand_t *robo_css_border_positional_row_of_kind(
    robo_css_border_kind_t kind);

/**
 * Non-zero when @p name is one of the three positional border shorthands.
 *
 * The predicate the box model's distribution pass asks before it distributes a
 * row: the question is not "is this a positional shorthand" but "is this row
 * yours or mine", and only this module can answer it.
 */
int robo_css_border_distributes_shorthand(robo_css_slice_t name);

/* ------------------------------------------------------------------ */
/* The composite shorthands                                            */
/* ------------------------------------------------------------------ */

/**
 * Number of composite border shorthands: `border` and the four side shorthands.
 *
 * Composite because each spans more than one *kind*: `border: 1px solid red`
 * writes three different grammars in one declaration, and `border-top: 1px solid`
 * writes two of them for one side.
 */
#define ROBO_CSS_BORDER_COMPOSITE_COUNT 5u

/** Number of composite border shorthands. */
size_t robo_css_border_composite_count(void);

/**
 * The composite shorthand's own name at @p index, or NULL when out of range.
 *
 * Index 0 is `border` and 1..4 are the sides in @ref robo_css_box_side_t order,
 * so the side a row covers is `index - 1` — stated here rather than as a second
 * parallel array a walk could fall out of step with.
 */
const char *robo_css_border_composite_name(size_t index);

/**
 * The side the composite shorthand at @p index covers, or
 * @ref ROBO_CSS_BOX_SIDE_TOP when it covers all four (index 0) or is out of range.
 *
 * The "all four" answer is the side that is not special, and it is only ever read
 * after @ref robo_css_border_composite_is_whole said no.
 */
robo_css_box_side_t robo_css_border_composite_side(size_t index);

/** Non-zero when the composite shorthand at @p index covers all four sides. */
int robo_css_border_composite_is_whole(size_t index);

/** Non-zero when @p name is one of the five, ASCII case-insensitively. */
int robo_css_border_is_composite(robo_css_slice_t name);

/* ------------------------------------------------------------------ */
/* The classifier                                                      */
/* ------------------------------------------------------------------ */

/**
 * What one component of a composite shorthand turned out to be.
 *
 * Four answers for three kinds, because a `var()` is a fourth thing: substitution
 * has not happened yet, and a component whose kind depends on a custom property
 * is *carried*, not guessed at.
 */
typedef enum robo_css_border_class {
    /** Belongs to no kind: `10%`, `#fff` on its own, a string, a nested block. */
    ROBO_CSS_BORDER_CLASS_INVALID = 0,
    /** A width: a length, a percentage, `0`, or thin/medium/thick. */
    ROBO_CSS_BORDER_CLASS_WIDTH = 1,
    /** A style keyword. */
    ROBO_CSS_BORDER_CLASS_STYLE = 2,
    /** A colour: a named colour, a hash, or a colour function. */
    ROBO_CSS_BORDER_CLASS_COLOR = 3,
    /** Legal CSS with no answer yet: a `var()` or `env()` reference. */
    ROBO_CSS_BORDER_CLASS_DEFERRED = 4
} robo_css_border_class_t;

/** Non-zero when @p outcome is one of the five. */
int robo_css_border_class_is_known(robo_css_border_class_t outcome);

/** Human-readable outcome name ("invalid", "width", ...); never NULL. */
const char *robo_css_border_class_name(robo_css_border_class_t outcome);

/** Number of classification outcomes. */
size_t robo_css_border_class_count(void);

/**
 * Classify one *component value* of a composite border shorthand.
 *
 * The order the questions are asked in is the whole of the logic, and it is the
 * order the vocabulary forces: a *style keyword* first, because `none` and
 * `hidden` are styles and are also colour-adjacent words a reader might place
 * wrong; then a width keyword (`thin`, `medium`, `thick`); then a colour *name* —
 * because after those two, an identifier that names a colour can only be one, and
 * one that names neither is a component this grammar has no place for. A
 * dimension or a bare number is a width, a hash or a colour function is a colour,
 * and `var()`/`env()` is deferred.
 *
 * A percentage is a *width*: see the header comment — the registry admits it, and
 * this classifier follows the registry rather than overruling it.
 *
 * @p component may be a single component or a list holding exactly one; the
 * declaration's own component values are what a caller iterates.
 */
robo_css_border_class_t robo_css_border_classify(const robo_css_value_t *component);

/* ------------------------------------------------------------------ */
/* Expansion                                                           */
/* ------------------------------------------------------------------ */

/**
 * Distribute one composite shorthand over the three kinds for one side.
 *
 * @p out_parts is indexed by @ref robo_css_border_kind_t and each element is
 * either a single-component list holding that kind's component or KIND_NONE when
 * the declaration did not name that kind.
 *
 * KIND_NONE is not "leave it alone", and this is where the composite shorthands
 * differ from the positional ones. `border-width: 1px` says nothing about a
 * border's style, so a longhand it does not cover is untouched. `border: 1px
 * solid` *does* say something about `border-top-color`: it stands for all three
 * kinds on all four sides, so a kind it did not mention takes that longhand's
 * *initial value*. The caller applies it; supplying the initial value here would
 * be a second place the registry's defaults are written down.
 *
 * Refused, every output left KIND_NONE: a `var()`/`env()` component, whose
 * substitution can change how many components there are and which kind each is;
 * more than one component for a single kind (`1px 2px solid` — the width is
 * ambiguous, not a pair, since there is no second width to give); and a component
 * that belongs to no kind.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument; ROBO_NOT_SUPPORTED
 *         for a value that cannot be distributed, with @p diag saying which of the
 *         three refusals it was (the author's declaration survives on the
 *         shorthand's own entry either way); ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_css_border_expand(const robo_css_value_t *value,
                                     robo_css_value_t out_parts[ROBO_CSS_BORDER_KIND_COUNT],
                                     robo_css_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_BORDER_H */
