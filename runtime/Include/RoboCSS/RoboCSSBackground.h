/*
 * RoboCSS background vocabulary (Phase 9.14).
 *
 * A background is the first property *family* whose parts are not the same kind
 * of thing. The box model's four edges are four of one value; `background` is one
 * declaration that speaks for six properties of six different shapes — a colour,
 * an image reference, a repeat style, a pair of positions, a pair of sizes and an
 * attachment. That is what makes it a different module from RoboCSSBox.h rather
 * than another row in it: there is no positional mapping from one component to
 * one longhand, because a component's *kind* is what decides where it goes.
 *
 * Four things live here and nothing else:
 *
 *   the vocabularies   `background-repeat` (four values) and `background-attachment`
 *                      (two), each a closed set with one row per value
 *   the position pair  two components, each a keyword on its axis or a length or
 *                      a percentage — the thing `<length-percentage>` alone cannot
 *                      express, because `left top` and `10px 20px` are the same
 *                      *property* and different *values*
 *   the size pair      `auto`, `cover`, `contain`, or one or two components that
 *                      are each `auto` or a length or a percentage
 *   the shorthand      the six longhands `background` stands for, and how one
 *                      layer's components distribute over them
 *
 * The repeat vocabulary is a table with two columns, not four spellings
 *
 * `repeat-x` is not a fifth behaviour: it is "repeat horizontally, do not repeat
 * vertically", and `repeat-y` is the mirror of it. So the row carries the two
 * *axes* rather than a flag, exactly as the display vocabulary carries an outer
 * and an inner display and the positioning vocabulary carries in-flow and
 * positioned. `repeats_on_axis` is derived from the row, so a value added later
 * (`space`, `round` — CSS's other two) answers both questions for itself, and no
 * function below compares a spelling to decide anything.
 *
 * The position pair, and why a keyword's meaning depends on its axis
 *
 * `background-position: left top` names an edge on each axis; `10px 20px` names an
 * offset on each. Both are two components, and the *component* is what differs:
 *
 *   an edge        CENTER / START / END, where `left` is START horizontally and
 *                  `top` is START vertically — so the axis is what spells it, and
 *                  `left` in the vertical slot is a mistake rather than a synonym
 *   a length       a number with a unit, kept as one
 *   a percentage   `25%`, kept as one, because only layout knows 25% of what
 *
 * The pair therefore carries a *per-axis* component rather than one shape with two
 * optional halves, and the single-value form is folded during parsing rather than
 * carried: `center` computes to `center center` and `10px` to `10px center`,
 * because the computed value of `background-position` is a pair however it was
 * written. What is *not* folded is the edge-relative form (`left 10px`,
 * `right 2em`) and the three- and four-component spellings: those are a different
 * grammar whose component count decides the meaning, and this build refuses them
 * rather than guessing at a reading.
 *
 * What it deliberately is not
 *
 * Not a property registry: nothing here classifies a property or gives it an
 * initial value (RoboCSSPropertyRegistry.h). Not a value parser: what arrives is
 * a `robo_css_value_t` — a component-value tree the grammar already built — and
 * the tokens inside it are the tokenizer's. And not layout or paint: nothing
 * below resolves a percentage, loads an image, measures a box or draws anything.
 * `background-size: 50% 25%` stays two percentages because only the phase that
 * knows the box can say what they are 50% *of*.
 *
 * What it does not implement, and says so
 *
 * Multiple background *layers* (`background: url(a.png), url(b.png)`) are legal
 * CSS and are refused here, with a reason, because the shorthand grammar for them
 * is a comma-separated list of layers and each layer is a whole set of longhands —
 * which is a value model this build does not have. An image *reference* is carried
 * rather than resolved: `url("a.png")` is a reference this build can hold and
 * cannot fetch, which is a fact about the value rather than a reason to lose the
 * declaration (the same answer 9.9 gives `oklch()`).
 */

#ifndef ROBO_CSS_BACKGROUND_H
#define ROBO_CSS_BACKGROUND_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSUnit.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The repeat vocabulary                                               */
/* ------------------------------------------------------------------ */

/**
 * A computed `background-repeat`.
 *
 * A closed set, and the enumerator values are append-only because a stored
 * enumerator is a contract: CSS's other two (`space`, `round`) join as 4 and 5
 * rather than reshuffling what a previous phase wrote down. There is no "none"
 * enumerator, so unlike the display and positioning vocabularies a zero record
 * reads as `repeat` — the property's *initial* value, which is the safest thing
 * a cleared record could hold and still not something to read without checking
 * the containing record's kind.
 */
typedef enum robo_css_background_repeat {
    /** `repeat`: both axes tile. The initial value. */
    ROBO_CSS_BACKGROUND_REPEAT = 0,
    /** `repeat-x`: horizontally only. */
    ROBO_CSS_BACKGROUND_REPEAT_X = 1,
    /** `repeat-y`: vertically only. */
    ROBO_CSS_BACKGROUND_REPEAT_Y = 2,
    /** `no-repeat`: neither axis tiles. */
    ROBO_CSS_BACKGROUND_NO_REPEAT = 3
} robo_css_background_repeat_t;

/** Number of defined repeat values. */
#define ROBO_CSS_BACKGROUND_REPEAT_COUNT 4u

/** Non-zero when @p repeat is one of the four. */
int robo_css_background_repeat_is_known(robo_css_background_repeat_t repeat);

/** Human-readable name ("repeat-x"); never NULL, "" for unknown. */
const char *robo_css_background_repeat_name(robo_css_background_repeat_t repeat);

/** Number of rows in the vocabulary. */
size_t robo_css_background_repeat_count(void);

/**
 * The repeat value an identifier spells, or `repeat` when it spells none of them.
 *
 * ASCII case-insensitive, as CSS spells every keyword, and against the canonical
 * spellings only — so `REPEAT-X` matches and `repeatx` does not. `repeat` is both
 * a value and the answer for "not one of them", which is the ambiguity
 * @ref robo_css_background_repeat_slice_is_known exists to resolve: ask the
 * *identifier*, then read the value.
 */
robo_css_background_repeat_t robo_css_background_repeat_from_slice(robo_css_slice_t keyword);

/** NUL-terminated flavour of the above. */
robo_css_background_repeat_t robo_css_background_repeat_from_text(const char *keyword);

/** Non-zero when @p keyword spells one of the vocabulary, case-insensitively. */
int robo_css_background_repeat_slice_is_known(robo_css_slice_t keyword);

/**
 * Write @p repeat's canonical keyword into @p buffer, NUL-terminated.
 *
 * One stable spelling per value: lowercase, hyphenated, `repeat-x` never the
 * enumerator's name.
 *
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param out_length non-NULL; receives the length written, excluding the NUL.
 * @param diag       optional diagnostic sink.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument or an unknown
 *         enumerator; ROBO_NOT_SUPPORTED when the keyword does not fit, in which
 *         case @p out_length receives the length it would have needed.
 */
robo_result_t robo_css_background_repeat_write(robo_css_background_repeat_t repeat,
                                               char *buffer,
                                               size_t capacity,
                                               size_t *out_length,
                                               robo_css_diag_t *diag);

/** Capacity @ref robo_css_background_repeat_write needs: `no-repeat` (9) + NUL. */
#define ROBO_CSS_BACKGROUND_REPEAT_TEXT_MAX 10u

/**
 * A background's axis.
 *
 * Two, and they are named rather than `x`/`y` because a position keyword is
 * spelled by its axis: START is `left` horizontally and `top` vertically, and a
 * function that has to pick a spelling needs to be told which.
 */
typedef enum robo_css_background_axis {
    ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL = 0,
    ROBO_CSS_BACKGROUND_AXIS_VERTICAL = 1
} robo_css_background_axis_t;

/** Number of axes a background has. */
#define ROBO_CSS_BACKGROUND_AXIS_COUNT 2u

/** Non-zero when @p axis is one of the two. */
int robo_css_background_axis_is_known(robo_css_background_axis_t axis);

/** Human-readable axis name ("horizontal", "vertical"); never NULL. */
const char *robo_css_background_axis_name(robo_css_background_axis_t axis);

/**
 * Whether @p repeat tiles @p axis — asked of the row, so a value added later
 * answers for itself.
 *
 * `repeat-x` is 1 horizontally and 0 vertically; `repeat` is 1 on both, and
 * `no-repeat` 0 on both. An unknown enumerator answers 0 rather than the first
 * row's answer.
 */
int robo_css_background_repeat_repeats_on_axis(robo_css_background_repeat_t repeat,
                                               robo_css_background_axis_t axis);

/* ------------------------------------------------------------------ */
/* The attachment vocabulary                                           */
/* ------------------------------------------------------------------ */

/**
 * A computed `background-attachment`.
 *
 * Two values, and the set is closed: CSS's third (`local`, which scrolls with
 * the element's own content rather than with the page) is a value this build has
 * not added, which is a different answer from a word nothing knows.
 */
typedef enum robo_css_background_attachment {
    /** `scroll`: the background is fixed to the element's box. The initial value. */
    ROBO_CSS_BACKGROUND_ATTACHMENT_SCROLL = 0,
    /** `fixed`: the background is fixed to the viewport. */
    ROBO_CSS_BACKGROUND_ATTACHMENT_FIXED = 1
} robo_css_background_attachment_t;

/** Number of defined attachment values. */
#define ROBO_CSS_BACKGROUND_ATTACHMENT_COUNT 2u

/** Non-zero when @p attachment is one of the two. */
int robo_css_background_attachment_is_known(robo_css_background_attachment_t attachment);

/** Human-readable name ("scroll"); never NULL, "" for unknown. */
const char *robo_css_background_attachment_name(robo_css_background_attachment_t attachment);

/** Number of rows in the vocabulary. */
size_t robo_css_background_attachment_count(void);

/** The attachment an identifier spells, or `scroll` when it spells none of them. */
robo_css_background_attachment_t robo_css_background_attachment_from_slice(
    robo_css_slice_t keyword);

/** NUL-terminated flavour of the above. */
robo_css_background_attachment_t robo_css_background_attachment_from_text(
    const char *keyword);

/** Non-zero when @p keyword spells one of the vocabulary, case-insensitively. */
int robo_css_background_attachment_slice_is_known(robo_css_slice_t keyword);

/** Write @p attachment's canonical keyword; same contract as the repeat writer. */
robo_result_t robo_css_background_attachment_write(robo_css_background_attachment_t attachment,
                                                   char *buffer,
                                                   size_t capacity,
                                                   size_t *out_length,
                                                   robo_css_diag_t *diag);

/** Capacity @ref robo_css_background_attachment_write needs: `scroll` (6) + NUL. */
#define ROBO_CSS_BACKGROUND_ATTACHMENT_TEXT_MAX 7u

/* ------------------------------------------------------------------ */
/* The position pair                                                   */
/* ------------------------------------------------------------------ */

/**
 * Where along an axis a position component sits, when it is a keyword.
 *
 * Three, and they are START/END rather than LEFT/RIGHT because the axis spells
 * them: START is `left` horizontally and `top` vertically, END is `right` and
 * `bottom`. CENTER is the one that means the same on both.
 */
typedef enum robo_css_background_edge {
    /** `center`, on either axis. */
    ROBO_CSS_BACKGROUND_EDGE_CENTER = 0,
    /** `left` / `top`. */
    ROBO_CSS_BACKGROUND_EDGE_START = 1,
    /** `right` / `bottom`. */
    ROBO_CSS_BACKGROUND_EDGE_END = 2
} robo_css_background_edge_t;

/** Number of defined edge positions. */
#define ROBO_CSS_BACKGROUND_EDGE_COUNT 3u

/** Non-zero when @p edge is one of the three. */
int robo_css_background_edge_is_known(robo_css_background_edge_t edge);

/**
 * Human-readable edge name, spelled for @p axis: `left`/`top` for START,
 * `right`/`bottom` for END, and `center` for CENTER on either.
 *
 * Never NULL; "" for an unknown edge or axis.
 */
const char *robo_css_background_edge_name(robo_css_background_edge_t edge,
                                          robo_css_background_axis_t axis);

/**
 * The edge @p keyword names *on @p axis*, or CENTER when it names none of them.
 *
 * The axis is an argument rather than ignored because it is what decides whether
 * the keyword is a keyword at all: `top` names an edge on the vertical axis and
 * nothing on the horizontal one, which is why the question has a second door —
 * @ref robo_css_background_edge_slice_is_known — so that "`top` in the horizontal
 * slot" is a refusal rather than a silent CENTER.
 */
robo_css_background_edge_t robo_css_background_edge_from_slice(robo_css_slice_t keyword,
                                                               robo_css_background_axis_t axis);

/** Non-zero when @p keyword names an edge on @p axis, case-insensitively. */
int robo_css_background_edge_slice_is_known(robo_css_slice_t keyword,
                                            robo_css_background_axis_t axis);

/** What a position component is. */
typedef enum robo_css_background_component_kind {
    /** No component: a cleared record. */
    ROBO_CSS_BACKGROUND_COMPONENT_NONE = 0,
    /** An edge keyword; @ref robo_css_background_component_t.edge is live. */
    ROBO_CSS_BACKGROUND_COMPONENT_EDGE = 1,
    /** A length; @ref robo_css_background_component_t.numeric is live. */
    ROBO_CSS_BACKGROUND_COMPONENT_LENGTH = 2,
    /** A percentage; @ref robo_css_background_component_t.numeric is live. */
    ROBO_CSS_BACKGROUND_COMPONENT_PERCENTAGE = 3
} robo_css_background_component_kind_t;

/** Number of defined component kinds. */
#define ROBO_CSS_BACKGROUND_COMPONENT_KIND_COUNT 4u

/** Non-zero when @p kind is a defined enumerator. */
int robo_css_background_component_kind_is_known(robo_css_background_component_kind_t kind);

/** Human-readable kind name ("edge", "percentage"); never NULL. */
const char *robo_css_background_component_kind_name(robo_css_background_component_kind_t kind);

/**
 * One component of a `background-position`.
 *
 * A flat record of a tag, an enumerator and a numeric value, owning nothing —
 * the same shape `robo_css_numeric_t` and `robo_css_color_t` have, and for the
 * same reason: it can be copied, compared and stored without a lifetime rule.
 * `edge` is live for EDGE and `numeric` for LENGTH and PERCENTAGE, and nothing
 * outside this module constructs one, so a component cannot come to carry a
 * field its kind does not mean.
 */
typedef struct robo_css_background_component {
    robo_css_background_component_kind_t kind;
    /** EDGE: where along the axis. */
    robo_css_background_edge_t edge;
    /** LENGTH / PERCENTAGE: the magnitude and, for a length, the unit. */
    robo_css_numeric_t numeric;
} robo_css_background_component_t;

/** The absence of a component. */
robo_css_background_component_t robo_css_background_component_none(void);

/** Non-zero when @p component holds a value. */
int robo_css_background_component_is_present(robo_css_background_component_t component);

/**
 * Kind-aware equality: two components are equal when they are the same kind and
 * the same value, so `left` and `0px` are different however equal they look.
 *
 * Across kinds it is 0, including for two absent components being asked — which
 * is deliberate, since "no component" is not a value something can equal.
 */
int robo_css_background_component_equals(robo_css_background_component_t a,
                                         robo_css_background_component_t b);

/**
 * Write @p component as @p axis spells it into @p buffer, NUL-terminated.
 *
 * An edge writes its axis spelling; a length its number and unit; a percentage
 * its number and `%`. One stable spelling per value, so two equal components
 * cannot print differently.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument, an unknown axis,
 *         an unknown edge or an absent component; ROBO_NOT_SUPPORTED when it
 *         does not fit, with @p out_length set to the length needed.
 */
robo_result_t robo_css_background_component_write(robo_css_background_component_t component,
                                                  robo_css_background_axis_t axis,
                                                  char *buffer,
                                                  size_t capacity,
                                                  size_t *out_length,
                                                  robo_css_diag_t *diag);

/**
 * A computed `background-position`: one component per axis, both always present.
 *
 * Both halves being required is the representation's whole content. CSS's
 * single-value forms are *folded* here rather than carried — `center` is
 * `center center` and `10px` is `10px center` — because the computed value of
 * this property is the pair, and a consumer that had to remember which half was
 * defaulted would be re-deriving a rule this module already applied.
 */
typedef struct robo_css_background_position {
    robo_css_background_component_t horizontal;
    robo_css_background_component_t vertical;
} robo_css_background_position_t;

/** The absence of a position: both components absent. */
robo_css_background_position_t robo_css_background_position_none(void);

/** Non-zero when *both* components hold a value — the only complete state. */
int robo_css_background_position_is_present(robo_css_background_position_t position);

/** Component-wise equality; two incomplete positions are never equal. */
int robo_css_background_position_equals(robo_css_background_position_t a,
                                        robo_css_background_position_t b);

/**
 * Write @p position as `horizontal vertical` into @p buffer, NUL-terminated.
 *
 * Both components, always, separated by one space — the computed value's own
 * spelling, whatever the declaration looked like. `center` prints `center center`.
 */
robo_result_t robo_css_background_position_write(robo_css_background_position_t position,
                                                 char *buffer,
                                                 size_t capacity,
                                                 size_t *out_length,
                                                 robo_css_diag_t *diag);

/* ------------------------------------------------------------------ */
/* The size pair                                                       */
/* ------------------------------------------------------------------ */

/** What a computed `background-size` is. */
typedef enum robo_css_background_size_kind {
    /** `auto`: the image's intrinsic size. The initial value. */
    ROBO_CSS_BACKGROUND_SIZE_AUTO = 0,
    /** `cover`: scaled to cover the box, aspect preserved. */
    ROBO_CSS_BACKGROUND_SIZE_COVER = 1,
    /** `contain`: scaled to fit inside the box, aspect preserved. */
    ROBO_CSS_BACKGROUND_SIZE_CONTAIN = 2,
    /** Two components; @ref robo_css_background_size_t.width / .height are live. */
    ROBO_CSS_BACKGROUND_SIZE_PAIR = 3
} robo_css_background_size_kind_t;

/** Number of defined size kinds. */
#define ROBO_CSS_BACKGROUND_SIZE_KIND_COUNT 4u

/** Non-zero when @p kind is a defined enumerator. */
int robo_css_background_size_kind_is_known(robo_css_background_size_kind_t kind);

/** Human-readable kind name ("auto", "cover", "pair"); never NULL. */
const char *robo_css_background_size_kind_name(robo_css_background_size_kind_t kind);

/**
 * One component of a `background-size` pair: `auto`, a length, or a percentage.
 *
 * `auto` is a flag rather than a third `robo_css_numeric_t` kind because it is
 * not a length: it says "keep the image's own size", which is a different
 * statement from any number, and one the module that has an image will read.
 */
typedef struct robo_css_background_size_component {
    /** Non-zero when this component is `auto`; @ref numeric is then NONE. */
    int is_auto;
    /** LENGTH / PERCENTAGE, when it is not `auto`. */
    robo_css_numeric_t numeric;
} robo_css_background_size_component_t;

/**
 * A computed `background-size`.
 *
 * Either one of the three whole-value keywords or a pair, and never both: `cover`
 * is not a pair of anything, and a pair whose components were both `auto` is
 * written `auto` — which is what makes `background-size: auto` and
 * `background-size: auto auto` the *same* computed value rather than two that
 * print differently. The single-component form is folded for the same reason
 * `background-position`'s is: CSS says a single width leaves the height `auto`.
 */
typedef struct robo_css_background_size {
    robo_css_background_size_kind_t kind;
    /** PAIR only. */
    robo_css_background_size_component_t width;
    /** PAIR only. */
    robo_css_background_size_component_t height;
} robo_css_background_size_t;

/** The absence of a size: a cleared record. */
robo_css_background_size_t robo_css_background_size_none(void);

/** Non-zero when @p size holds a value — a keyword, or a pair with both halves. */
int robo_css_background_size_is_present(robo_css_background_size_t size);

/** Kind-aware equality; two absent sizes are never equal. */
int robo_css_background_size_equals(robo_css_background_size_t a,
                                    robo_css_background_size_t b);

/**
 * Write @p size as the keyword or `width height` into @p buffer, NUL-terminated.
 *
 * `auto` prints `auto`, not `auto auto`: the two are one computed value and have
 * one spelling.
 */
robo_result_t robo_css_background_size_write(robo_css_background_size_t size,
                                             char *buffer,
                                             size_t capacity,
                                             size_t *out_length,
                                             robo_css_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

/**
 * What happened when a background-shaped value was read.
 *
 * Four outcomes, and the third is the one that keeps this phase honest.
 * DEFERRED means "legal CSS this build has no representation for" — a `var()`
 * reference, or a colour function 9.9 already defers — and such a declaration is
 * *carried* with its tokens rather than refused. INVALID means the value means
 * nothing here at all. They are separate because the diagnostic differs and the
 * diagnostic is the only reason the outcome exists: an author who wrote
 * `background-position: var(--p)` needs a different sentence from one who wrote
 * `background-position: banana`.
 */
typedef enum robo_css_background_parse {
    /** Not a background-shaped value. */
    ROBO_CSS_BACKGROUND_PARSE_INVALID = 0,
    /** A position: @p out holds both components. */
    ROBO_CSS_BACKGROUND_PARSE_POSITION = 1,
    /** A size: @p out holds a keyword or a pair. */
    ROBO_CSS_BACKGROUND_PARSE_SIZE = 2,
    /** Legal CSS with no representation here; keep the tokens. */
    ROBO_CSS_BACKGROUND_PARSE_DEFERRED = 3
} robo_css_background_parse_t;

/** Number of defined outcomes. */
#define ROBO_CSS_BACKGROUND_PARSE_COUNT 4u

/** Non-zero when @p outcome is a defined enumerator. */
int robo_css_background_parse_is_known(robo_css_background_parse_t outcome);

/** Human-readable outcome name ("position", "deferred"); never NULL. */
const char *robo_css_background_parse_name(robo_css_background_parse_t outcome);

/**
 * Read @p specified as a `background-position`, writing the pair to @p out.
 *
 * Accepted: one or two *significant* components, where each is an edge keyword on
 * its own axis, a length, or a percentage; or two components that are both lengths
 * or percentages. A single component names one axis and leaves the other at
 * CENTER, as CSS says it does.
 *
 * Refused, each for its own reason: the edge-relative form (`left 10px`,
 * `right 2em`), which is a four-component grammar and not a two-component one;
 * three or more components; a bare number, which is not a position because it has
 * no unit; two keywords on the same axis (`left right`); an identifier that names
 * no edge; and anything that is not a component at all.
 *
 * @p out is cleared first and left absent on any outcome but POSITION.
 */
robo_css_background_parse_t robo_css_background_position_from_value(
    const robo_css_value_t *specified,
    robo_css_background_position_t *out);

/**
 * Read @p specified as a `background-size`, writing the value to @p out.
 *
 * Accepted: `auto`, `cover`, `contain`, or one or two components that are each
 * `auto`, a length or a percentage. A single component sets the width and leaves
 * the height `auto`.
 *
 * Refused: three or more components; a bare number; a keyword that is not one of
 * the three; and anything that is not a component at all.
 */
robo_css_background_parse_t robo_css_background_size_from_value(
    const robo_css_value_t *specified,
    robo_css_background_size_t *out);

/* ------------------------------------------------------------------ */
/* The shorthand                                                       */
/* ------------------------------------------------------------------ */

/**
 * Number of longhands `background` stands for.
 *
 * Six, and the order is the order a serialisation would write them in — image,
 * position, size, repeat, attachment, colour — because that is the order CSS
 * itself serialises the shorthand, and a table whose order is arbitrary is a
 * table two readers can disagree about.
 */
#define ROBO_CSS_BACKGROUND_LONGHAND_COUNT 6u

/** Number of longhands `background` stands for. */
size_t robo_css_background_longhand_count(void);

/**
 * The longhand at @p index, or NULL when out of range. Borrowed and static.
 *
 * The names are spelled here rather than derived, because the registry's rows and
 * this table are the two places that must agree about them and this is the one
 * place that can be walked.
 */
const char *robo_css_background_longhand_name(size_t index);

/** Non-zero when @p name is one of the six longhands, case-insensitively. */
int robo_css_background_is_longhand(robo_css_slice_t name);

/** The shorthand's own name: `background`. Never NULL. */
const char *robo_css_background_shorthand_name(void);

/**
 * Distribute one `background:` layer over the six longhands.
 *
 * @p out_parts is indexed by the order @ref robo_css_background_longhand_name
 * uses, and each element is either a *list* holding that longhand's component (or
 * its one or two components, for a pair) or KIND_NONE when the shorthand did not
 * mention it.
 *
 * KIND_NONE is not "leave it alone": CSS's `background` is a shorthand for every
 * one of the six, so a component the author did not write means the longhand's
 * *initial value*, and the caller applies that. This function deliberately does
 * not supply it — the initial values live in the registry, and a module that
 * invented them would be a second place they are written down.
 *
 * The classification, which is what makes this a different machine from the box
 * model's positional mapping:
 *
 *   a colour function, or a colour name      `background-color`
 *   any other function, or a URL token       `background-image` (a reference)
 *   `none`                                   `background-image`
 *   a repeat keyword                         `background-repeat`
 *   an attachment keyword                    `background-attachment`
 *   a position keyword, length or percentage `background-position`
 *   after a `/`                              `background-size`
 *
 * Two components for the same longhand are folded into one part where the grammar
 * allows it (two position components, two size components) and refused where it
 * does not (two colours, two images, two repeats, two attachments).
 *
 * Refused, with a diagnostic and every output left KIND_NONE: more than one layer
 * (a top-level comma); a `var()` or `env()` component, whose substitution can
 * change how many components there are; `cover`, `contain` or `auto` written
 * without a `/`, which is where CSS's grammar puts them; more than one `/`; a
 * component that belongs to no longhand; a `/` with nothing after it; and two
 * components the same longhand cannot take.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument; ROBO_NOT_SUPPORTED
 *         for a value that cannot be distributed (the author's declaration
 *         survives on the shorthand's own entry either way); ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_css_background_expand(
    const robo_css_value_t *value,
    robo_css_value_t out_parts[ROBO_CSS_BACKGROUND_LONGHAND_COUNT],
    robo_css_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_BACKGROUND_H */
