/*
 * RoboCSS computed values (Phase 9.8).
 *
 * A *specified* value is what a declaration says: the component-value tree the
 * grammar built, tokens intact. A *computed* value is what the property's own
 * semantics make of it — `50%` is a percentage and not a length, `#f00` is the
 * colour red and not a hash token, `auto` is the keyword it is and not the five
 * letters. This header is that second representation, and it is deliberately a
 * small closed set of shapes rather than "a string, but typed":
 *
 *   NONE         nothing computed (a cleared record)
 *   KEYWORD      one identifier, decoded: `auto`, `normal`, `inline`, `static`
 *   NUMBER       a bare number: `line-height: 1.5`
 *   PERCENTAGE   a percentage, which stays one because only layout can resolve it
 *   LENGTH       a number and a unit: `10px`, `2rem`, `1em`
 *   COLOR        four 8-bit channels, including alpha
 *   FAMILY_LIST  an ordered font family list
 *   PRESERVED    the specified tokens, carried intact
 *   INVALID      the value did not mean anything for this property
 *
 * The last two are the ones a reader will question, and both are load-bearing.
 *
 * PRESERVED is not "unimplemented". `width: calc(100% - 20px)` is a real
 * computed value in this build: it is the expression, structurally intact, and
 * reducing it here would be a lie because `%` needs a containing block. The
 * same is true of `var(--x)` (a reference, not a value), of a shorthand whose
 * longhands are not expanded yet, and of a custom property, which is *defined*
 * as an opaque token sequence. A representation that kept the tokens is exactly
 * as computed as one that produced a pixel, and more honest.
 *
 * INVALID is not an error return. "This value means nothing for this property"
 * is an *answer* about the value, and it is reported as one: the operation
 * succeeded, the computed value says INVALID, a diagnostic carries a short
 * reason, and the computed style counts it as unsupported. A function that
 * returned a failure code instead would force a caller to choose between
 * dropping the property and inventing a value for it.
 *
 * What is deliberately not here: used values. `50%` stays `50%`, `1em` stays
 * `1em`, `normal` stays `normal`, and nothing asks how wide anything is. The
 * conversion of a length percentage to a number is the layout phases' business,
 * and `currentColor` and the full colour syntax are 9.9's (see below).
 *
 * Colours, and the 9.9 seam
 *
 * 9.8 normalises the colour forms the parser and this build can already
 * express — the standard named keywords, `transparent`, all four hex lengths,
 * and `rgb()`/`rgba()` in both the comma and the space syntax — into the
 * channel record here. It does **not** implement `hsl()`, `hwb()`, `lab()`,
 * `lch()`, `oklab()`, `oklch()`, `color()` or `color-mix()`: those are the
 * subject of the dedicated colour subsystem, and rejecting them cleanly is
 * better than producing a colour that is wrong. `currentColor` is recognised
 * and carried PRESERVED with the "unresolved" flag, because resolving it needs
 * the element's own computed `color` and that dance belongs to the colour
 * subsystem too.
 *
 * Ownership
 *
 * A computed value owns every byte it reports except the slices inside a
 * PRESERVED tree, which borrow the source that declared them exactly as every
 * component value does (see RoboCSSValue.h). A computed style therefore stays
 * valid as long as the sheets it was computed from.
 */

#ifndef ROBO_CSS_COMPUTED_VALUE_H
#define ROBO_CSS_COMPUTED_VALUE_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSPropertyRegistry.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Length units                                                        */
/* ------------------------------------------------------------------ */

/**
 * A CSS length unit, as a computed value carries it.
 *
 * Absolute units are normalised to `px` when the value is computed — the
 * conversion factors are fixed by CSS (`1in = 96px`, `1pt = 4/3 px`) and do not
 * depend on the display, so doing it here is exact rather than a guess. Relative
 * units are **not** converted: `2em` needs a parent font size and `50vh` needs a
 * viewport, and neither exists at this stage. They keep their unit and their
 * number, which is what makes the value usable by whoever has the context.
 *
 * PERCENTAGE is not a unit here because it is a separate *kind*: `50%` of a
 * width and `50%` of a line height are different computations, so a single
 * kind that only means "a proportion" is the right amount of information.
 */
typedef enum robo_css_unit {
    /** No unit: the value is not a length. */
    ROBO_CSS_UNIT_NONE = 0,
    /* absolute, resolved to px when computed */
    ROBO_CSS_UNIT_PX = 1,
    ROBO_CSS_UNIT_CM = 2,
    ROBO_CSS_UNIT_MM = 3,
    ROBO_CSS_UNIT_Q = 4,
    ROBO_CSS_UNIT_IN = 5,
    ROBO_CSS_UNIT_PT = 6,
    ROBO_CSS_UNIT_PC = 7,
    /* relative, carried unconverted */
    ROBO_CSS_UNIT_EM = 8,
    ROBO_CSS_UNIT_REM = 9,
    ROBO_CSS_UNIT_EX = 10,
    ROBO_CSS_UNIT_CH = 11,
    ROBO_CSS_UNIT_VW = 12,
    ROBO_CSS_UNIT_VH = 13,
    ROBO_CSS_UNIT_VMIN = 14,
    ROBO_CSS_UNIT_VMAX = 15
} robo_css_unit_t;

/** Number of defined units. */
#define ROBO_CSS_UNIT_COUNT 16u

/** Non-zero when @p unit is a defined enumerator. */
int robo_css_unit_is_known(robo_css_unit_t unit);

/** Non-zero when @p unit converts to px with a fixed factor. */
int robo_css_unit_is_absolute(robo_css_unit_t unit);

/** The unit's canonical spelling (\"px\", \"rem\"); never NULL. */
const char *robo_css_unit_name(robo_css_unit_t unit);

/**
 * The unit an identifier spells, or NONE.
 * ASCII case-insensitive, as CSS spells units.
 */
robo_css_unit_t robo_css_unit_from_slice(robo_css_slice_t name);

/** NUL-terminated flavour of the above. */
robo_css_unit_t robo_css_unit_from_text(const char *name);

/** @p value in px for an absolute @p unit; @p value unchanged otherwise. */
double robo_css_unit_to_px(double value, robo_css_unit_t unit);

/* ------------------------------------------------------------------ */
/* Colours                                                             */
/* ------------------------------------------------------------------ */

/**
 * One resolved colour: four channels, 8 bits each.
 *
 * 8 bits per channel is the representation, not a rounding of one: CSS's
 * ordinary colour syntax cannot express more (`rgb()` takes 0–255 or a
 * percentage), the framebuffer the runtime targets is 8-bit per channel, and a
 * float triple would invite equality comparisons that fail on values that are
 * the same colour. Alpha is included rather than kept apart so that
 * `rgb(255 0 0 / 0.5)` is one value and not a pair.
 */
typedef struct robo_css_color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
} robo_css_color_t;

/** An opaque colour: (0, 0, 0, 255) is black, and so is this with alpha 255. */
robo_css_color_t robo_css_color_make(uint8_t red, uint8_t green, uint8_t blue,
                                     uint8_t alpha);

/** Non-zero when two colours have the same four channels. */
int robo_css_color_equals(robo_css_color_t a, robo_css_color_t b);

/**
 * The colour a `#…` hash token's *value* spells, or a sentinel.
 *
 * The token's value bytes carry no `#` (the tokenizer strips the delimiter), so
 * this takes the digits alone. A length of 3, 4, 6 or 8 is required — CSS's
 * four hex forms and nothing else — and every byte must be a hex digit. Any
 * other length or character is refused rather than truncated, because a
 * three-digit colour read as six would be a colour nobody wrote.
 *
 * @return 1 and writes @p out on success; 0 otherwise, leaving @p out alone.
 */
int robo_css_color_from_hex(robo_css_slice_t digits, robo_css_color_t *out);

/**
 * The colour a named keyword spells (`red`, `rebeccapurple`), or `transparent`.
 *
 * The comparison is ASCII case-insensitive and covers the full standard set:
 * the 16 CSS2.1 names, the 130 `X11`/SVG names and `rebeccapurple`, which is
 * what "the standard named colours" means and what keeps `rebeccapurple` from
 * being an unsupported spelling. `transparent` is the transparent black.
 *
 * @return 1 and writes @p out when @p name is one; 0 otherwise.
 */
int robo_css_color_from_name(robo_css_slice_t name, robo_css_color_t *out);

/**
 * The colour an `rgb()`/`rgba()` call spells.
 *
 * Both the legacy comma syntax (`rgb(255, 0, 0)`, `rgba(255, 0, 0, .5)`) and the
 * modern one (`rgb(255 0 0)`, `rgb(255 0 0 / 50%)`) are accepted, with numeric or
 * percentage channels and an optional alpha, because both are CSS and a build
 * that understood only one of them would reject valid authoring. Mixing the two
 * (`rgb(255, 0 0)`) is refused, which is CSS's own rule.
 *
 * Channels are clamped into range rather than wrapped: `rgb(300, 0, 0)` is red,
 * not a colour with a wrapped green channel, and clamping is what CSS says.
 *
 * @param function  a FUNCTION component value whose name is `rgb` or `rgba`.
 * @param out       receives the colour; untouched on failure.
 * @return 1 when the call parsed to a colour; 0 otherwise — including for a
 *         function of the wrong name, a missing or extra argument, a bad
 *         separator, or a non-numeric argument.
 */
int robo_css_color_from_rgb_function(const robo_css_value_t *function,
                                     robo_css_color_t *out);

/* ------------------------------------------------------------------ */
/* The computed value                                                  */
/* ------------------------------------------------------------------ */

/** What a computed value is. */
typedef enum robo_css_computed_value_kind {
    /** Nothing computed: a cleared record. */
    ROBO_CSS_COMPUTED_VALUE_NONE = 0,
    /** One decoded identifier. */
    ROBO_CSS_COMPUTED_VALUE_KEYWORD = 1,
    /** A bare number. */
    ROBO_CSS_COMPUTED_VALUE_NUMBER = 2,
    /** A percentage, unresolved by design. */
    ROBO_CSS_COMPUTED_VALUE_PERCENTAGE = 3,
    /** A number with a unit. */
    ROBO_CSS_COMPUTED_VALUE_LENGTH = 4,
    /** Four channels. */
    ROBO_CSS_COMPUTED_VALUE_COLOR = 5,
    /** An ordered font family list. */
    ROBO_CSS_COMPUTED_VALUE_FAMILY_LIST = 6,
    /** The specified tokens, intact. */
    ROBO_CSS_COMPUTED_VALUE_PRESERVED = 7,
    /** The value means nothing for this property. */
    ROBO_CSS_COMPUTED_VALUE_INVALID = 8
} robo_css_computed_value_kind_t;

/** Number of defined kinds. */
#define ROBO_CSS_COMPUTED_VALUE_KIND_COUNT 9u

/** Non-zero when @p kind is a defined enumerator. */
int robo_css_computed_value_kind_is_known(robo_css_computed_value_kind_t kind);

/** Human-readable kind name (\"length\", \"colour\"); never NULL. */
const char *robo_css_computed_value_kind_name(robo_css_computed_value_kind_t kind);

/**
 * Capacity of the buffer robo_css_computed_value_write needs.
 *
 * Sized for the longest thing this build produces: a serialised font family
 * list, or a number with six decimals, a unit and a percentage sign. A value
 * whose text does not fit is not truncated — @ref
 * robo_css_computed_value_write refuses, so a caller learns rather than
 * silently receiving half a value.
 */
#define ROBO_CSS_COMPUTED_VALUE_TEXT_MAX 256u

/**
 * One computed value.
 *
 * A flat record rather than a tagged union: each kind uses one or two of the
 * fields, and a union would save a few bytes in exchange for an ownership rule
 * per member that a reader has to reconstruct. `text` and `preserved` are the
 * only owned members, and `robo_css_computed_value_dispose` frees exactly the
 * ones the kind says are live.
 */
typedef struct robo_css_computed_value {
    robo_css_computed_value_kind_t kind;
    /** NUMBER / PERCENTAGE / LENGTH: the magnitude. */
    double number;
    /** LENGTH: the unit; NONE otherwise. */
    robo_css_unit_t unit;
    /** NUMBER: non-zero when the value was written as an integer. */
    int is_integer;
    /** COLOR: the channels; zeroed otherwise. */
    robo_css_color_t color;
    /**
     * KEYWORD: the decoded identifier.
     * FAMILY_LIST: the family list as written, whitespace normalised.
     * INVALID: a short reason.
     * Owned, NUL-terminated; NULL when the kind does not use it.
     */
    char *text;
    /** Length of @p text in bytes, excluding its NUL. */
    size_t text_length;
    /**
     * PRESERVED: the specified tokens, owned (a deep copy).
     * KIND_NONE exactly when the kind does not use it.
     */
    robo_css_value_t preserved;
    /**
     * Non-zero when this value names something only a later stage can resolve.
     *
     * `var(--x)` is a reference, `calc(100% - 20px)` needs a containing block,
     * and `currentColor` needs the element's own computed `color`. All three are
     * legal, all three are carried intact, and none of them is a value *yet* —
     * which is a fact about the value that has to be recorded, because a
     * consumer that treated `var(--x)` as an ordinary preserved token list would
     * be reading a reference as if it were a colour.
     */
    int unresolved;
} robo_css_computed_value_t;

/** Clear @p value to NONE. Safe on NULL. */
void robo_css_computed_value_init(robo_css_computed_value_t *value);

/** Free @p value's owned members and reset it. Idempotent; safe on NULL. */
void robo_css_computed_value_dispose(robo_css_computed_value_t *value);

/**
 * Move @p value into @p out_value, leaving @p value NONE.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument or for
 *         `value == out_value`.
 */
robo_result_t robo_css_computed_value_move(robo_css_computed_value_t *value,
                                          robo_css_computed_value_t *out_value,
                                          robo_css_diag_t *diag);

/**
 * Deep-copy @p value into @p out_value, which is disposed first.
 * Transactional: on failure @p out_value is left NONE rather than half-copied.
 */
robo_result_t robo_css_computed_value_copy(robo_css_computed_value_t *out_value,
                                          const robo_css_computed_value_t *value,
                                          robo_css_diag_t *diag);

/** Non-zero when @p value is NONE. NULL → 1. */
int robo_css_computed_value_is_empty(const robo_css_computed_value_t *value);

/* --- construction --- */

/** Make @p value a KEYWORD (the text is copied). */
robo_result_t robo_css_computed_value_make_keyword(robo_css_computed_value_t *value,
                                                  robo_css_slice_t keyword,
                                                  robo_css_diag_t *diag);

/** Make @p value a NUMBER. */
robo_result_t robo_css_computed_value_make_number(robo_css_computed_value_t *value,
                                                 double number,
                                                 int is_integer,
                                                 robo_css_diag_t *diag);

/** Make @p value a PERCENTAGE. */
robo_result_t robo_css_computed_value_make_percentage(robo_css_computed_value_t *value,
                                                     double percentage,
                                                     int is_integer,
                                                     robo_css_diag_t *diag);

/** Make @p value a LENGTH. @p unit must not be NONE. */
robo_result_t robo_css_computed_value_make_length(robo_css_computed_value_t *value,
                                                 double number,
                                                 robo_css_unit_t unit,
                                                 int is_integer,
                                                 robo_css_diag_t *diag);

/** Make @p value a COLOR. */
robo_result_t robo_css_computed_value_make_color(robo_css_computed_value_t *value,
                                                robo_css_color_t color,
                                                robo_css_diag_t *diag);

/** Make @p value a FAMILY_LIST (the list text is copied). */
robo_result_t robo_css_computed_value_make_family_list(robo_css_computed_value_t *value,
                                                       robo_css_slice_t families,
                                                       robo_css_diag_t *diag);

/** Make @p value a PRESERVED copy of @p specified. */
robo_result_t robo_css_computed_value_make_preserved(robo_css_computed_value_t *value,
                                                     const robo_css_value_t *specified,
                                                     robo_css_diag_t *diag);

/** Make @p value INVALID with @p reason as its explanation (copied). */
robo_result_t robo_css_computed_value_make_invalid(robo_css_computed_value_t *value,
                                                   const char *reason,
                                                   robo_css_diag_t *diag);

/* --- reading --- */

/** Borrowed keyword/family/reason text, or an absent slice. */
robo_css_slice_t robo_css_computed_value_text(const robo_css_computed_value_t *value);

/** Borrowed preserved tokens, or NULL when the value is not PRESERVED. */
const robo_css_value_t *robo_css_computed_value_preserved(
    const robo_css_computed_value_t *value);

/** The colour of a COLOR value; black-transparent for any other kind. */
robo_css_color_t robo_css_computed_value_color(const robo_css_computed_value_t *value);

/**
 * Non-zero when @p a and @p b are the same computed value.
 *
 * Structural, and total: two PRESERVED values are equal when their trees are
 * equal (kind, token text, children, recursively), two LENGTHs when the unit and
 * the number agree, and values of different kinds are never equal. This is what
 * makes a computed style testable without serialising it, and it is why the
 * representation uses integers for channels rather than floats.
 */
int robo_css_computed_value_equals(const robo_css_computed_value_t *a,
                                   const robo_css_computed_value_t *b);

/**
 * Write @p value's canonical text into @p buffer, NUL-terminated.
 *
 * The text is a *diagnostic* rendering, not CSS serialisation: it is stable,
 * locale-independent and exactly reversible for numbers, but a PRESERVED value
 * renders as its token text and an INVALID one as its reason. Rationale: this
 * exists so that a test can say what it expected and a developer tool can print
 * what it got, and neither needs the CSSOM's serialisation rules (which are a
 * later phase's subject).
 *
 * @param buffer    destination, at least @p capacity bytes.
 * @param capacity  bytes available.
 * @param out_length non-NULL; receives the length written, excluding the NUL.
 * @param diag      optional diagnostic sink.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument;
 *         ROBO_NOT_SUPPORTED when the text does not fit, in which case
 *         @p out_length receives the length it would have needed.
 */
robo_result_t robo_css_computed_value_write(const robo_css_computed_value_t *value,
                                            char *buffer,
                                            size_t capacity,
                                            size_t *out_length,
                                            robo_css_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Computing a specified value                                         */
/* ------------------------------------------------------------------ */

/**
 * Turn a specified value into a computed one for a property of @p computation.
 *
 * The one normalising function, and the only place a token sequence becomes
 * semantic. It is a function of (the value's shape, the specified tree) and
 * nothing else: no context, no parent, no DOM — which is what makes it
 * testable and what keeps the inheritance story (\"a child inherits its parent's
 * *computed* value\") a property of the caller rather than of this function.
 *
 * @p computation comes from the property's definition
 * (RoboCSSPropertyRegistry.h) or, for a property with no definition — a custom
 * property, or a name nothing classifies — is
 * ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE.
 *
 * What each shape accepts, and what it does with anything else:
 *
 *   COLOUR       one ident (a named colour), `transparent`, one hash of 3/4/6/8
 *                digits, `rgb()`/`rgba()` in the comma or space syntax with
 *                numbers or percentages and an optional alpha. `currentcolor`
 *                is carried PRESERVED and flagged unresolved; anything else is
 *                INVALID.
 *   LENGTH       one dimension with a known unit (absolute units converted to
 *                px), one bare `0`, one percentage, or one ident (`auto`,
 *                `normal`, `medium`). A negative value is accepted and kept:
 *                whether `-10px` is legal is the property's rule, and this
 *                function has the *shape*, not the rule.
 *   KEYWORD      exactly one ident, decoded. Anything else is INVALID.
 *   FAMILY_LIST  a comma-separated list of strings and idents. The items are
 *                kept in order and joined with `, ` so the value is a list and
 *                not a coincidence of token order.
 *   FONT_WEIGHT  `normal`, `bold`, `lighter`, `bolder`, or a number.
 *   LINE_HEIGHT  `normal`, a number, a dimension or a percentage.
 *   PRESERVE     the tree, copied. Never invalid: a property whose shape is not
 *                claimed cannot have a value that is wrong for it.
 *
 * @p out is disposed first and is left NONE whenever the call fails to produce
 * anything at all.
 *
 * @return ROBO_OK whenever a computed value was produced — **including** an
 *         INVALID one, which is an answer about the value and not a failure of
 *         this call; ROBO_INVALID_ARGUMENT for a NULL argument or an unknown
 *         @p computation; ROBO_OUT_OF_MEMORY when nothing could be built.
 *         A diagnostic is recorded for an INVALID result, naming the property
 *         shape and the reason, so a caller can report *why*.
 */
robo_result_t robo_css_computed_value_compute(
    robo_css_property_computation_t computation,
    const robo_css_value_t *specified,
    robo_css_computed_value_t *out,
    robo_css_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_COMPUTED_VALUE_H */
