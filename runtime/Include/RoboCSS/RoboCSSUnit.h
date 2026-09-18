/*
 * RoboCSS units and numbers (Phase 9.10).
 *
 * A CSS *value* is mostly a number with something attached to it, and this module
 * is the something: the unit, the number's shape, and the arithmetic that turns a
 * written length into pixels once — and only once — enough is known to do it
 * honestly.
 *
 * Three things live here, and they are three because they have three different
 * lifetimes:
 *
 *   the unit set    a closed vocabulary: which units exist, what family each
 *                   belongs to, and which of them have a fixed factor to `px`
 *   the context     what a *relative* unit needs in order to mean anything —
 *                   the font size, the root font size, the font's metrics, the
 *                   viewport — supplied by the caller, never read from a global
 *   the value       `robo_css_numeric_t`: a number, an integer, a percentage or a
 *                   dimension, with deterministic equality and one canonical
 *                   spelling
 *
 * Units are a closed set, not strings
 *
 * `10px` is not `10` plus the text "px". The unit is an enumerator, and the
 * module's job is to be the single place that says what each one *is* — so a
 * reader asks "is `em` absolute?" rather than comparing a spelling, and an
 * unknown unit is a value that did not parse rather than a string that was
 * carried hopefully.
 *
 * 9.8 put this vocabulary in RoboCSSComputedValue.h, because the computed value
 * was its only reader. 9.10 gives it a module, for the same reason 9.9 gave
 * colours one: a second reader arrived. The computed-value layer consumes it, and
 * the thing that will resolve a length against a font or a viewport is not the
 * computed-value layer — it is whoever has those facts, which is why the context
 * is an argument rather than a member.
 *
 * Absolute conversion is exact, and relative conversion is a *question*
 *
 * CSS's absolute units are defined in terms of the reference pixel: `1in` is
 * 96px, `1cm` is 96/2.54px, `1pt` is 4/3px, `1Q` is 1/40 of a centimetre. Those are exact by
 * definition rather than by measurement, so `robo_css_unit_to_px` converts them
 * and never consults a display. Nothing here knows anything about DPI, and that
 * is deliberate: an absolute length means the same thing on every screen, and
 * only a *used* value — a later phase's — may care about the device.
 *
 * A relative unit is a different kind of thing. `2em` is not a length until
 * something says what the font size is, and a module that guessed would produce a
 * number that looked computed and was wrong. So `robo_css_unit_resolve_px` takes
 * a context and answers one of four things:
 *
 *   RESOLVED          the context had what the unit needed, and *out_px is set
 *   NEEDS_CONTEXT     the unit is fine and the context is not; nothing is invented
 *   NOT_A_LENGTH      the unit is NONE or not a unit at all
 *   INVALID           a magnitude or a context input that is not a finite number
 *
 * NEEDS_CONTEXT is the answer that matters. `50vh` with no viewport is not an
 * error — it is a value that cannot be resolved *yet*, and saying so is the whole
 * reason this is a function with four outcomes rather than a converter that
 * multiplies by something.
 *
 * The context, and why the small/large/dynamic viewport is a fallback chain
 *
 * `em`, `ex`, `ch` and `lh` need the element's font metrics; `rem` and `rlh` need
 * the root's. `vw`, `vh`, `vmin` and `vmax` need the viewport, and CSS Values 4
 * defines them as the *large* viewport. `sv*`, `lv*` and `dv*` are the small,
 * large and dynamic viewport respectively, which differ only when something (a
 * toolbar, a keyboard) is hiding or showing.
 *
 * That shape is why the context carries one **default** viewport plus three
 * optional overrides. A caller that has a window and no dynamic chrome — which is
 * every caller today — sets the default and gets `vw` and `svw` and `dvw` all
 * answering the same size, which is exactly right for a window whose chrome never
 * moves. A caller that does have the three sizes sets them and gets three
 * different answers. Neither case needs a flag on the *unit*.
 *
 * `vi` and `vb` are the inline and block axis viewport units, so they are `vw`
 * and `vh` in a horizontal writing mode and swapped in a vertical one. The
 * default writing mode is horizontal, so the context's `vertical_writing` flag
 * defaults to 0 and `vi`/`vb` answer correctly for the initial value — which is
 * the honest amount of support to give a unit whose real input is a property this
 * build does not compute yet.
 *
 * Numbers, and the shape a property's grammar cares about
 *
 * `robo_css_numeric_t` has four kinds, and the four are the four things CSS's
 * numeric grammar can ask for:
 *
 *   NUMBER       a `<number>`: `1.5`, `-0.25`, `1e2`
 *   INTEGER      a `<number>` written as an integer: `10`, `-3`. Separate from
 *                NUMBER because CSS's grammar distinguishes them — a property
 *                requiring an `<integer>` must refuse `1.5` and must refuse
 *                `1.0`, which is a number token — and because "convert a decimal
 *                to an integer when someone wanted one" is exactly the silent
 *                coercion that produces a plausible wrong value
 *   PERCENTAGE   a `<percentage>`: `50%`. Not a unit, and not a number: 50% of a
 *                width and 50% of a line height are different computations
 *   LENGTH       a `<dimension>`: `10px`, `2em`, `-20vh`, and the unit is live
 *
 * There is no INVALID kind. A value that is not one of the four is not a
 * *numeric* value, which is a fact about the parse and not about the value — so
 * it is `ROBO_CSS_NUMERIC_PARSE_*`, and `robo_css_numeric_t` stays a type whose
 * every instance is a number of a known shape. A "cleared" record is NONE, which
 * is the absence of one.
 *
 * Five parse outcomes, and "did it work" is not enough of an answer when "legal
 * CSS this build cannot evaluate yet" has to stay distinguishable from "a
 * mistake":
 *
 *   RESOLVED      *out holds one of the four kinds
 *   DEFERRED      `calc()`, `var()`, `min()`, `max()`, `clamp()` — a *reference*,
 *                 carried by the caller, never evaluated here
 *   UNKNOWN_UNIT  `10foo`. A dimension whose unit this build does not know is a
 *                 different diagnostic from a value that is not numeric at all,
 *                 and a caller can tell a developer which one they wrote
 *   NON_FINITE    the tokenizer produced an infinity from a huge literal. CSS has
 *                 no spelling for one, so it is refused rather than clamped
 *   INVALID       not a numeric value at all: a string, a colour, `red blue`,
 *                 `1.2.3`, `10 px`, nothing
 *
 * There is no MALFORMED, and that is a finding rather than an omission. The
 * colour module has one because `rgb(1)` is *one* function component that is
 * syntactically fine and semantically wrong. Writing a number wrong does not have
 * that shape: the tokenizer is lossless and the grammar is total, so `1.2.3`
 * arrives as three components and `1.` as a number beside a delimiter — neither of
 * which is one numeric value, which is INVALID — and the only remaining way to
 * be malformed is to build a token by hand that the tokenizer cannot produce. A
 * member nothing can reach would be documentation of a state that does not exist.
 *
 * `calc()` is not implemented here and is not claimed to be. A `calc()` expression
 * is DEFERRED and carried with its tokens entire; the extension point is the four
 * kinds above plus a future expression kind, not a half-evaluated number that
 * silently dropped its `%`.
 *
 * Numeric safety
 *
 * Every magnitude is a `double` and every one that this module *keeps* has been
 * checked finite, because a NaN or an infinity reaching a style is how one bad
 * declaration poisons a whole layout. Integer conversion refuses rather than
 * rounds: `robo_css_numeric_integer_value` answers only for a value that is
 * exactly representable as an integer in the documented range, so an overflow
 * cannot turn into a plausible small number.
 *
 * Ownership
 *
 * Nothing here owns anything. A unit is an enumerator, a context is a plain struct
 * of numbers, and a `robo_css_numeric_t` is a tag, a double and an enumerator by
 * value — which is what lets one be copied, compared and stored without a lifetime
 * rule. The one borrowed input, a component-value tree, is read and released
 * nowhere: `robo_css_numeric_from_value` looks at it and copies nothing out of it.
 */

#ifndef ROBO_CSS_UNIT_H
#define ROBO_CSS_UNIT_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The unit set                                                        */
/* ------------------------------------------------------------------ */

/**
 * A CSS length unit.
 *
 * The enumerators are grouped by family and the values are **append-only**: 9.8's
 * seven absolute and four font-relative units and four viewport units keep the
 * numbers they had, and 9.10 appends the rest. A reader that switches on a family
 * is asking a question about today's set, so the order is documentation rather
 * than a contract — but a *stored* enumerator is a contract, and appending is what
 * keeps one written yesterday readable today.
 *
 * PERCENTAGE is not here, and that is the same decision 9.8 made: `%` is a kind of
 * its own (@ref robo_css_numeric_kind_t), because it is a proportion of something
 * rather than a length whose unit is a sign.
 */
typedef enum robo_css_unit {
    /** No unit: the value is not a length. */
    ROBO_CSS_UNIT_NONE = 0,
    /* absolute, with a fixed factor to px */
    ROBO_CSS_UNIT_PX = 1,
    ROBO_CSS_UNIT_CM = 2,
    ROBO_CSS_UNIT_MM = 3,
    ROBO_CSS_UNIT_Q = 4,
    ROBO_CSS_UNIT_IN = 5,
    ROBO_CSS_UNIT_PT = 6,
    ROBO_CSS_UNIT_PC = 7,
    /* font-relative, resolved against the element's own metrics */
    ROBO_CSS_UNIT_EM = 8,
    ROBO_CSS_UNIT_REM = 9,
    ROBO_CSS_UNIT_EX = 10,
    ROBO_CSS_UNIT_CH = 11,
    /* viewport-relative, the default (large) viewport */
    ROBO_CSS_UNIT_VW = 12,
    ROBO_CSS_UNIT_VH = 13,
    ROBO_CSS_UNIT_VMIN = 14,
    ROBO_CSS_UNIT_VMAX = 15,
    /* 9.10 appends below here */
    ROBO_CSS_UNIT_LH = 16,
    ROBO_CSS_UNIT_RLH = 17,
    ROBO_CSS_UNIT_VI = 18,
    ROBO_CSS_UNIT_VB = 19,
    ROBO_CSS_UNIT_SVW = 20,
    ROBO_CSS_UNIT_SVH = 21,
    ROBO_CSS_UNIT_SVMIN = 22,
    ROBO_CSS_UNIT_SVMAX = 23,
    ROBO_CSS_UNIT_LVW = 24,
    ROBO_CSS_UNIT_LVH = 25,
    ROBO_CSS_UNIT_LVMIN = 26,
    ROBO_CSS_UNIT_LVMAX = 27,
    ROBO_CSS_UNIT_DVW = 28,
    ROBO_CSS_UNIT_DVH = 29,
    ROBO_CSS_UNIT_DVMIN = 30,
    ROBO_CSS_UNIT_DVMAX = 31
} robo_css_unit_t;

/** Number of defined units, NONE included. */
#define ROBO_CSS_UNIT_COUNT 32u

/**
 * What family a unit belongs to, which is the question resolution asks.
 *
 * The family decides *what the unit needs*: nothing (absolute), the element's
 * font metrics (font-relative), or the viewport. `robo_css_unit_resolve_px` is a
 * switch over these three, and a unit whose family is NONE is not a length.
 */
typedef enum robo_css_unit_class {
    /** Not a unit. */
    ROBO_CSS_UNIT_CLASS_NONE = 0,
    /** A fixed factor to px; needs no context. */
    ROBO_CSS_UNIT_CLASS_ABSOLUTE = 1,
    /** Relative to the font: needs the element's or the root's metrics. */
    ROBO_CSS_UNIT_CLASS_FONT_RELATIVE = 2,
    /** Relative to the viewport: needs a viewport size. */
    ROBO_CSS_UNIT_CLASS_VIEWPORT = 3
} robo_css_unit_class_t;

/** Number of defined unit classes. */
#define ROBO_CSS_UNIT_CLASS_COUNT 4u

/**
 * Which viewport a viewport unit measures.
 *
 * CSS Values 4 defines four viewports, and they differ only when something is
 * hiding: the *small* one is what is left when the browser's chrome is showing,
 * the *large* one when it is not, and the *dynamic* one is whichever applies right
 * now. `vw`, `vh`, `vmin` and `vmax` are the large viewport, which is also the
 * family this build calls DEFAULT — a caller that has one size has the large one,
 * because a window that never changes has no smaller state.
 */
typedef enum robo_css_viewport_family {
    /** `vw`, `vh`, `vmin`, `vmax`, `vi`, `vb`: the large viewport. */
    ROBO_CSS_VIEWPORT_DEFAULT = 0,
    /** `svw`, `svh`, `svmin`, `svmax`. */
    ROBO_CSS_VIEWPORT_SMALL = 1,
    /** `lvw`, `lvh`, `lvmin`, `lvmax`. */
    ROBO_CSS_VIEWPORT_LARGE = 2,
    /** `dvw`, `dvh`, `dvmin`, `dvmax`. */
    ROBO_CSS_VIEWPORT_DYNAMIC = 3
} robo_css_viewport_family_t;

/** Number of defined viewport families. */
#define ROBO_CSS_VIEWPORT_FAMILY_COUNT 4u

/** Non-zero when @p unit is a defined enumerator. NONE is defined. */
int robo_css_unit_is_known(robo_css_unit_t unit);

/** @p unit's family, or CLASS_NONE for NONE and for an unknown enumerator. */
robo_css_unit_class_t robo_css_unit_class(robo_css_unit_t unit);

/** Human-readable family name ("absolute", "font-relative"); never NULL. */
const char *robo_css_unit_class_name(robo_css_unit_class_t unit_class);

/** Non-zero when @p unit converts to px with a fixed factor. */
int robo_css_unit_is_absolute(robo_css_unit_t unit);

/** Non-zero when @p unit is a viewport unit (any of the four families). */
int robo_css_unit_is_viewport(robo_css_unit_t unit);

/**
 * Which viewport @p unit measures, or DEFAULT for a non-viewport unit.
 *
 * DEFAULT for a unit that is not a viewport one is not a lie: the question is only
 * ever asked of a unit already known to be one (@ref robo_css_unit_is_viewport),
 * and answering with the family every such unit would use by default beats an
 * out-parameter a caller has to check.
 */
robo_css_viewport_family_t robo_css_unit_viewport_family(robo_css_unit_t unit);

/** The unit's canonical spelling ("px", "rem"); "" for NONE and unknown. */
const char *robo_css_unit_name(robo_css_unit_t unit);

/**
 * The unit an identifier spells, or NONE.
 *
 * ASCII case-insensitive, as CSS spells units: `10PX` and `10px` are the same
 * length. The comparison is against the canonical spellings only, so an unknown
 * identifier — `10foo`, `10pixels` — answers NONE rather than matching a prefix.
 */
robo_css_unit_t robo_css_unit_from_slice(robo_css_slice_t name);

/** NUL-terminated flavour of the above. */
robo_css_unit_t robo_css_unit_from_text(const char *name);

/**
 * @p value in px for an absolute @p unit.
 *
 * Exact and context-free, because CSS's absolute factors are definitions rather
 * than measurements. For a unit with **no** fixed factor the value is returned
 * unchanged: this function answers "what is this in px" only when the answer does
 * not depend on anything, and `robo_css_unit_resolve_px` is where a relative unit
 * is answered. The two are deliberately different doors, because one is a
 * multiplication and the other is a question.
 */
double robo_css_unit_to_px(double value, robo_css_unit_t unit);

/* ------------------------------------------------------------------ */
/* The resolution context                                              */
/* ------------------------------------------------------------------ */

/**
 * What a relative unit needs in order to mean anything.
 *
 * Every field is paired with a `has_` flag, and the flags are the design: a
 * context that says `font_size = 16` without saying it *has* one would make `2em`
 * resolve against a number nobody supplied, and "I do not know the font size" and
 * "the font size is zero" are different answers. A caller zeroes the struct and
 * sets the flags for the facts it actually has.
 *
 * The three viewport overrides are optional and fall back to the default, which
 * is what makes a window with no dynamic chrome expressible without repeating
 * itself three times. See the header comment.
 */
typedef struct robo_css_unit_context {
    /* --- font-relative: the element's own metrics --- */
    /** Non-zero when the four fields below are set. */
    int has_font_metrics;
    /** The element's computed font size in px, for `em`. */
    double font_size;
    /** The font's x-height in px, for `ex`. */
    double x_height;
    /** The advance of the font's zero glyph in px, for `ch`. */
    double zero_width;
    /** The element's computed line height in px, for `lh`. */
    double line_height;
    /* --- font-relative: the root element's metrics --- */
    /** Non-zero when the two fields below are set. */
    int has_root_font_metrics;
    /** The root element's computed font size in px, for `rem`. */
    double root_font_size;
    /** The root element's computed line height in px, for `rlh`. */
    double root_line_height;
    /* --- viewport --- */
    /** Non-zero when the default (large) viewport size is set, for `vw`/`vh`. */
    int has_viewport;
    double viewport_width;
    double viewport_height;
    /** Non-zero when the small viewport differs from the default. */
    int has_small_viewport;
    double small_viewport_width;
    double small_viewport_height;
    /** Non-zero when the large viewport differs from the default. */
    int has_large_viewport;
    double large_viewport_width;
    double large_viewport_height;
    /** Non-zero when the dynamic viewport differs from the default. */
    int has_dynamic_viewport;
    double dynamic_viewport_width;
    double dynamic_viewport_height;
    /**
     * Non-zero when the writing mode is vertical.
     *
     * `vi` and `vb` are the inline and block axes, so they are `vw` and `vh` in a
     * horizontal writing mode and swapped in a vertical one. The initial value of
     * `writing-mode` is `horizontal-tb`, so 0 is the correct default rather than a
     * placeholder — and the flag is the extension point for when that property is
     * computed.
     */
    int vertical_writing;
} robo_css_unit_context_t;

/** Set @p context to "I know nothing", which is four bytes of flags. */
void robo_css_unit_context_init(robo_css_unit_context_t *context);

/**
 * Set the element's font metrics, for `em`/`ex`/`ch`/`lh`.
 *
 * All four are recorded together because they come from one source — the font the
 * element resolves to — and a caller that had the font size but not the x-height
 * would be a caller that had not measured the font at all.
 */
void robo_css_unit_context_set_font(robo_css_unit_context_t *context,
                                    double font_size,
                                    double x_height,
                                    double zero_width,
                                    double line_height);

/** Set the root element's metrics, for `rem`/`rlh`. */
void robo_css_unit_context_set_root_font(robo_css_unit_context_t *context,
                                         double root_font_size,
                                         double root_line_height);

/** Set the default viewport, which is also the fallback for the three families. */
void robo_css_unit_context_set_viewport(robo_css_unit_context_t *context,
                                        double width,
                                        double height);

/** Set the small viewport, for the `sv*` family. */
void robo_css_unit_context_set_small_viewport(robo_css_unit_context_t *context,
                                              double width,
                                              double height);

/** Set the large viewport, for the `lv*` family (and the default family). */
void robo_css_unit_context_set_large_viewport(robo_css_unit_context_t *context,
                                              double width,
                                              double height);

/** Set the dynamic viewport, for the `dv*` family. */
void robo_css_unit_context_set_dynamic_viewport(robo_css_unit_context_t *context,
                                                double width,
                                                double height);

/* ------------------------------------------------------------------ */
/* Contextual resolution                                               */
/* ------------------------------------------------------------------ */

/**
 * What resolving a length against a context produced.
 *
 * Four answers, and each is a different thing for a caller to do. Collapsing
 * NEEDS_CONTEXT into "failed" is how a caller ends up substituting a default it
 * invented, which is the mistake this module exists to prevent.
 */
typedef enum robo_css_length_resolution {
    /** The context had what the unit needed; @p out_px holds the length. */
    ROBO_CSS_LENGTH_RESOLVED = 0,
    /** The unit is fine and the context does not say enough. Nothing written. */
    ROBO_CSS_LENGTH_NEEDS_CONTEXT = 1,
    /** The unit is NONE, unknown, or not a length at all. */
    ROBO_CSS_LENGTH_NOT_A_LENGTH = 2,
    /** A magnitude or a context input that is not a finite number. */
    ROBO_CSS_LENGTH_INVALID = 3
} robo_css_length_resolution_t;

/** Number of defined resolutions. */
#define ROBO_CSS_LENGTH_RESOLUTION_COUNT 4u

/** Non-zero when @p resolution is a defined enumerator. */
int robo_css_length_resolution_is_known(robo_css_length_resolution_t resolution);

/** Human-readable resolution name ("resolved", "needs-context"); never NULL. */
const char *robo_css_length_resolution_name(robo_css_length_resolution_t resolution);

/**
 * Resolve @p value in @p unit to px against @p context.
 *
 * The one place a relative or viewport length becomes a number of pixels, and it
 * is a *question* rather than a conversion: an absolute unit is multiplied by its
 * fixed factor, and every other unit is answered only when the context supplies
 * what it needs. `context` may be NULL, which is a caller that knows nothing — and
 * that is NEEDS_CONTEXT for a relative unit and RESOLVED for an absolute one,
 * because an absolute length does not depend on anything.
 *
 * A non-finite @p value or a non-finite context input is INVALID rather than
 * propagated: an infinity that reaches a style is how one bad declaration poisons
 * everything downstream, and `50vh` with an infinite viewport is not a length.
 *
 * @param out_px receives the length in px, and is written only for RESOLVED.
 * @return what happened; see @ref robo_css_length_resolution_t.
 */
robo_css_length_resolution_t robo_css_unit_resolve_px(double value,
                                                      robo_css_unit_t unit,
                                                      const robo_css_unit_context_t *context,
                                                      double *out_px);

/* ------------------------------------------------------------------ */
/* The typed numeric value                                             */
/* ------------------------------------------------------------------ */

/**
 * What shape a numeric value has.
 *
 * The four shapes CSS's numeric grammar distinguishes, and no fifth: a value that
 * is none of them is not a numeric value, which is a parse outcome rather than a
 * kind (@ref robo_css_numeric_parse_t).
 */
typedef enum robo_css_numeric_kind {
    /** Nothing: a cleared record. */
    ROBO_CSS_NUMERIC_NONE = 0,
    /** A `<number>`: `1.5`, `-0.25`, `1e2`. */
    ROBO_CSS_NUMERIC_NUMBER = 1,
    /** A `<number>` written as an integer: `10`, `-3`, `+7`. */
    ROBO_CSS_NUMERIC_INTEGER = 2,
    /** A `<percentage>`: `50%`, `-10%`, `12.5%`. */
    ROBO_CSS_NUMERIC_PERCENTAGE = 3,
    /** A `<dimension>`: `10px`, `2em`, `-20vh`. @ref unit is live. */
    ROBO_CSS_NUMERIC_LENGTH = 4
} robo_css_numeric_kind_t;

/** Number of defined kinds. */
#define ROBO_CSS_NUMERIC_KIND_COUNT 5u

/** Non-zero when @p kind is a defined enumerator. */
int robo_css_numeric_kind_is_known(robo_css_numeric_kind_t kind);

/** Human-readable kind name ("number", "length"); never NULL. */
const char *robo_css_numeric_kind_name(robo_css_numeric_kind_t kind);

/**
 * One typed numeric value.
 *
 * A flat record of a tag, a double and an enumerator, owning nothing — so it can
 * be copied, compared and stored without a lifetime rule, exactly like a colour.
 *
 * `unit` is live for LENGTH and meaningless otherwise, and `robo_css_numeric_make`
 * is the only way to construct one, so a PERCENTAGE cannot come to carry a unit it
 * did not mean.
 */
typedef struct robo_css_numeric {
    /** What shape this value has. */
    robo_css_numeric_kind_t kind;
    /** The magnitude. */
    double number;
    /** LENGTH: the unit; NONE otherwise. */
    robo_css_unit_t unit;
} robo_css_numeric_t;

/** The absence of a numeric value. */
robo_css_numeric_t robo_css_numeric_none(void);

/**
 * A unitless number.
 *
 * @param is_integer    non-zero when the value was *written* as an integer, which
 *                      is what decides NUMBER from INTEGER. It is the grammar's
 *                      question and not the value's: `1.0` is a number token, and
 *                      a property requiring an `<integer>` must refuse it.
 * @return ROBO_OK, or ROBO_INVALID_ARGUMENT for a NULL @p out or a non-finite
 *         @p number. Both are programmer errors rather than parse outcomes, which
 *         is why this is a result and not an outcome: the tokenizer cannot
 *         produce a non-finite number without the parser having rejected it, and
 *         a caller that hands one over has a bug.
 */
robo_result_t robo_css_numeric_make_number(robo_css_numeric_t *out,
                                           double number,
                                           int is_integer,
                                           robo_css_diag_t *diag);

/** A percentage. @p percentage is the number before the `%`. */
robo_result_t robo_css_numeric_make_percentage(robo_css_numeric_t *out,
                                               double percentage,
                                               robo_css_diag_t *diag);

/**
 * A dimension.
 *
 * @p unit must be a known unit other than NONE: a length with no unit is a
 * number, and a caller that meant that should say so.
 */
robo_result_t robo_css_numeric_make_length(robo_css_numeric_t *out,
                                           double number,
                                           robo_css_unit_t unit,
                                           robo_css_diag_t *diag);

/** Non-zero when @p numeric is one of the four real kinds. */
int robo_css_numeric_is_present(robo_css_numeric_t numeric);

/**
 * Non-zero when @p numeric's value happens to be a whole number.
 *
 * A question about the *value*, and asked separately from the kind because they
 * are different: `1.0` is integral and is not an INTEGER, and `10` is both. A
 * property that needs a whole number asks this; a property whose grammar says
 * `<integer>` asks @ref robo_css_numeric_integer_value, which is stricter.
 */
int robo_css_numeric_is_integral(robo_css_numeric_t numeric);

/**
 * The largest magnitude @ref robo_css_numeric_integer_value will convert.
 *
 * 2^53, which is where a `double` stops representing every integer: above it, two
 * different integers can have the same `double`, so converting one would be
 * guessing. A bound that is the *representable* limit rather than an arbitrary
 * one is a bound a caller can reason about.
 */
#define ROBO_CSS_NUMERIC_INTEGER_LIMIT 9007199254740992.0

/**
 * @p numeric as an integer, exactly.
 *
 * Refuses rather than rounds or truncates, and refuses an over-large magnitude
 * rather than casting a value the cast cannot hold — which is the whole of
 * "avoid overflow and undefined behaviour" for this module.
 *
 * Requires kind INTEGER: a NUMBER that happens to be whole is a value a caller
 * asked a *different* question about, and `1.0` being accepted where `<integer>`
 * is required would be the silent coercion CSS's grammar exists to catch.
 *
 * @return 1 and writes @p out_integer on success; 0 otherwise, leaving it alone.
 */
int robo_css_numeric_integer_value(robo_css_numeric_t numeric, int64_t *out_integer);

/**
 * Non-zero when @p a and @p b are the same numeric value.
 *
 * Kind-aware and unit-aware: `0` is not `0%`, `0px` is not unitless zero, and
 * `10px` is not `10em`. That is deliberate rather than pedantic — CSS distinguishes
 * all three, and a comparison that ignored the difference is how a value gets used
 * as a length when it was a proportion.
 */
int robo_css_numeric_equals(robo_css_numeric_t a, robo_css_numeric_t b);

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

/**
 * What a numeric parse produced.
 *
 * Five outcomes, and the split between them is the information a developer needs:
 * UNKNOWN_UNIT says "you wrote a unit this build does not know", NON_FINITE says
 * "your number is not one a runtime can hold", DEFERRED says "this is legal and
 * needs a later stage", and INVALID says "this is not a numeric value at all".
 * See the header comment for why there is no MALFORMED.
 */
typedef enum robo_css_numeric_parse {
    /** Not a numeric value: @p out is untouched. */
    ROBO_CSS_NUMERIC_PARSE_INVALID = 0,
    /** One of the four kinds; @p out holds it. */
    ROBO_CSS_NUMERIC_PARSE_RESOLVED = 1,
    /** `calc()`, `var()`, `min()`, `max()`, `clamp()`: carried, not evaluated. */
    ROBO_CSS_NUMERIC_PARSE_DEFERRED = 2,
    /** A dimension whose unit this build does not know. */
    ROBO_CSS_NUMERIC_PARSE_UNKNOWN_UNIT = 3,
    /** A magnitude that is not finite, which CSS has no spelling for. */
    ROBO_CSS_NUMERIC_PARSE_NON_FINITE = 4
} robo_css_numeric_parse_t;

/** Number of defined outcomes. */
#define ROBO_CSS_NUMERIC_PARSE_COUNT 5u

/** Non-zero when @p outcome is a defined enumerator. */
int robo_css_numeric_parse_is_known(robo_css_numeric_parse_t outcome);

/** Human-readable outcome name ("resolved", "unknown-unit"); never NULL. */
const char *robo_css_numeric_parse_name(robo_css_numeric_parse_t outcome);

/**
 * The numeric value one specified value spells, if it spells one.
 *
 * The door a property's length or number computation uses, and the *only* reader
 * of a numeric token in this subsystem. It requires exactly one significant
 * component — a declaration's value arrives as a list, so `10 20` is two numbers
 * and therefore not one — and it answers DEFERRED for a reference function rather
 * than calling it a mistake, because `calc(100% - 20px)` is legal CSS that this
 * build does not evaluate.
 *
 * @param specified the component-value tree as declared.
 * @param out       receives the value for RESOLVED; untouched otherwise.
 */
robo_css_numeric_parse_t robo_css_numeric_from_value(const robo_css_value_t *specified,
                                                     robo_css_numeric_t *out);

/** The same, from a single token. NONE for a token that is not numeric. */
robo_css_numeric_parse_t robo_css_numeric_from_token(const robo_css_token_t *token,
                                                     robo_css_numeric_t *out);

/* ------------------------------------------------------------------ */
/* Serialization                                                       */
/* ------------------------------------------------------------------ */

/**
 * Capacity @ref robo_css_numeric_write needs.
 *
 * Sized for the longest thing this build produces: a fixed-point magnitude with
 * six decimals, a unit, and a NUL. A value whose text does not fit is refused
 * rather than truncated, so a caller learns instead of silently receiving half a
 * value.
 */
#define ROBO_CSS_NUMERIC_TEXT_MAX 64u

/**
 * Write @p numeric's canonical text into @p buffer, NUL-terminated.
 *
 * Deterministic, and the rules are the ones a reader can check by eye:
 *
 *   * an integral magnitude has no decimal point: `10`, not `10.000000`;
 *   * a fractional one has no trailing zeroes: `1.5`, not `1.500000`;
 *   * a negative zero is `0`, because it is zero;
 *   * a magnitude **smaller than the fixed-point precision** is written in a
 *     compact exponent form — `1e-07`, not `0`. Rendering a value that is not
 *     zero as `0` would be a lie about it, and a lie that a layout would then
 *     act on;
 *   * a percentage is written with its sign: `50%`;
 *   * a length is written with the unit's *canonical* spelling, so `10PX` writes
 *     as `10px`.
 *
 * @param out_length receives the length written, always; required.
 * @return ROBO_OK, or ROBO_NOT_SUPPORTED when the text does not fit (with
 *         @p out_length telling the caller how much room it needed),
 *         or ROBO_INVALID_ARGUMENT for a NULL argument.
 */
robo_result_t robo_css_numeric_write(robo_css_numeric_t numeric,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_css_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_UNIT_H */
