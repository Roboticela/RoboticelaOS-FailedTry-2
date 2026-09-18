/*
 * RoboCSS colours (Phase 9.9).
 *
 * A colour is the one CSS value whose *meaning* is arithmetic — three channels
 * and an alpha, in a space, at a precision — and everything else in CSS is a
 * number with a unit, an identifier, or a token sequence. That is why it gets a
 * module of its own rather than a by-product of the computed-value layer, and
 * why the module's whole job is the four things a colour can be:
 *
 *   parse       CSS's spellings in, a colour out: the standard named set,
 *               `transparent`, all four hex lengths, `rgb()`/`rgba()` and
 *               `hsl()`/`hsla()` in both the legacy comma and the modern space
 *               syntax
 *   represent   one record, one canonical space, deterministic equality
 *   resolve     `currentColor`, which is not a colour until something has a
 *               `color` to read
 *   serialize   one stable spelling, because a value nobody can print is a
 *               value nobody can debug
 *
 * Four kinds, not one
 *
 *   NONE       nothing: a cleared record, and the state a failed parse leaves
 *   RGBA       four 8-bit channels in sRGB — the canonical resolved colour
 *   CURRENT    `currentColor`: a *contextual* colour, and deliberately not an
 *              RGB value
 *   INVALID    a colour *record* that is not a colour, so a caller can carry
 *              "there was supposed to be one here" without inventing black
 *
 * CURRENT is the kind a reader will question, and it is load-bearing.
 * `currentColor` is not a colour: it is a reference to whatever `color` turns
 * out to be for the element it lands on, and CSS keeps it unresolved through
 * the computed-value stage for exactly that reason. Resolving it early — to
 * black, or to anything else — would make `color: currentColor` mean a fixed
 * colour, which is the one thing it must never mean. So it is a kind here, it
 * survives the value layer intact, and RoboCSSComputed.c resolves it at the only
 * point that has the answer: the element whose style is being computed, with the
 * parent's computed `color` in hand.
 *
 * 8 bits per channel is a decision, not a rounding
 *
 * CSS's ordinary colour syntax cannot express more (`rgb()` takes 0–255 or a
 * percentage, a hex form is 3/4/6/8 nibbles), the runtime's framebuffer is 8-bit
 * per channel, and a float triple would invite equality comparisons that fail on
 * values which are the same colour. `hsl()` lands here by converting once, with
 * rounding at the end rather than at every step, so `hsl(120 100% 25%)` is
 * exactly the same colour as `#008000` — which is a property a test can hold.
 *
 * What is not here, and why
 *
 * `hwb()`, `lab()`, `lch()`, `oklab()`, `oklch()`, `color()`, `color-mix()` and
 * `device-cmyk()` are **not** implemented. They are not silently accepted
 * either: a colour *property* given one of them is carried with its tokens and
 * flagged unresolved (the caller keeps the value; this module reports
 * ROBO_CSS_COLOR_PARSE_DEFERRED), because a colour that is wrong is worse than a
 * colour that is deferred, and calling the author's declaration invalid would be
 * a lie about CSS. The extension point is `kind`: a new space is a new kind plus
 * a payload member, and nothing in the value system, the cascade or the computed
 * style has to change, because all three pass a `robo_css_color_t` by value and
 * ask it what it is.
 *
 * Ownership
 *
 * Nothing here owns anything. Every input is a borrowed slice or a borrowed
 * component-value tree, and every output is a record of plain integers — which
 * is what lets a colour be copied, compared and stored without a lifetime rule.
 */

#ifndef ROBO_CSS_COLOR_H
#define ROBO_CSS_COLOR_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

/**
 * What a `robo_css_color_t` is.
 *
 * A tag rather than a convention: "no colour", "a resolved colour", "the
 * contextual keyword" and "not a colour" are four different answers, and a
 * record that could only say three of them would make a caller guess which one
 * an all-zero channel set meant.
 */
typedef enum robo_css_color_kind {
    /** Nothing: a cleared record, and what a failed parse leaves. */
    ROBO_CSS_COLOR_NONE = 0,
    /** A resolved colour: four channels, sRGB, 8 bits each. */
    ROBO_CSS_COLOR_RGBA = 1,
    /** `currentColor`: contextual, resolved against an element's `color`. */
    ROBO_CSS_COLOR_CURRENT = 2,
    /** Not a colour, carried so a caller can see that it was meant to be. */
    ROBO_CSS_COLOR_INVALID = 3
} robo_css_color_kind_t;

/** Number of defined kinds. */
#define ROBO_CSS_COLOR_KIND_COUNT 4u

/** Non-zero when @p kind is a defined enumerator. */
int robo_css_color_kind_is_known(robo_css_color_kind_t kind);

/** Human-readable kind name ("rgba", "current", "invalid"); never NULL. */
const char *robo_css_color_kind_name(robo_css_color_kind_t kind);

/**
 * One colour.
 *
 * The channels are live exactly when the kind is RGBA (and, for the record's
 * purposes, meaningless otherwise — `robo_css_color_make` is the only way to set
 * them, so a CURRENT record cannot come to carry channels it did not mean). A
 * flat record rather than a union, matching `robo_css_computed_value_t`: the
 * channels are four bytes, and a union would buy those bytes in exchange for an
 * ownership rule per member that a reader has to reconstruct.
 */
typedef struct robo_css_color {
    /** What this record is. */
    robo_css_color_kind_t kind;
    /** RGBA: red channel. */
    uint8_t red;
    /** RGBA: green channel. */
    uint8_t green;
    /** RGBA: blue channel. */
    uint8_t blue;
    /** RGBA: alpha; 255 is opaque. */
    uint8_t alpha;
} robo_css_color_t;

/** The NONE colour: no colour at all. */
robo_css_color_t robo_css_color_none(void);

/** The INVALID colour: the absence of one, recorded rather than assumed. */
robo_css_color_t robo_css_color_invalid(void);

/** The CURRENT colour: `currentColor`, unresolved by construction. */
robo_css_color_t robo_css_color_current(void);

/** A resolved colour with the four channels given. */
robo_css_color_t robo_css_color_make(uint8_t red, uint8_t green, uint8_t blue,
                                     uint8_t alpha);

/** Non-zero when @p color is a resolved colour. */
int robo_css_color_is_resolved(robo_css_color_t color);

/** Non-zero when @p color is `currentColor`. */
int robo_css_color_is_current(robo_css_color_t color);

/**
 * Non-zero when @p color is something a style can hold: a resolved colour or
 * `currentColor`. NONE and INVALID are not.
 */
int robo_css_color_is_valid(robo_css_color_t color);

/**
 * Non-zero when @p a and @p b are the same colour *as records*.
 *
 * Kind-aware: a resolved red is never equal to `currentColor`, because they are
 * different answers and a comparison that ignored the kind is how a contextual
 * keyword gets mistaken for a fixed channel set. The channels are compared only
 * when both kinds are RGBA, so a CURRENT never compares equal to anything but a
 * CURRENT.
 */
int robo_css_color_equals(robo_css_color_t a, robo_css_color_t b);

/* ------------------------------------------------------------------ */
/* The standard named colours                                          */
/* ------------------------------------------------------------------ */

/*
 * The set is CSS's own: the 16 CSS2.1 keywords, the 130 colour names SVG shares
 * with X11 (duplicates and all — `gray` and `grey` are two names for one
 * colour), and `rebeccapurple`, which CSS added to the standard set and which is
 * therefore not an exotic spelling. 148 entries, in one table, reachable two
 * ways: by name, and by index so a suite can walk the whole set rather than the
 * handful of names someone thought to type.
 *
 * `transparent` is deliberately *not* a named colour. It is a colour — the
 * transparent black — but it is a *keyword*, and it is handled where the
 * identifier is, so that a reader looking for it does not have to know it is
 * spelled as a table entry.
 */

/** Number of standard named colours. */
size_t robo_css_color_named_count(void);

/**
 * The @p index'th named colour, in the table's own order.
 *
 * @param out_name  receives the name, borrowed with static storage duration.
 * @param out_color receives the colour (always RGBA, always opaque).
 * @return 1 when @p index is in range; 0 otherwise, leaving both outputs alone.
 */
int robo_css_color_named_at(size_t index, robo_css_slice_t *out_name,
                            robo_css_color_t *out_color);

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

/**
 * The colour a `#…` hash token's *value* spells, or nothing.
 *
 * The token's value bytes carry no `#` (the tokenizer strips the delimiter), so
 * this takes the digits alone. A length of exactly 3, 4, 6 or 8 is required —
 * CSS's four hex forms and nothing else — and every byte must be a hex digit.
 * Any other length or character is refused rather than truncated, because a
 * three-digit colour read as six would be a colour nobody wrote.
 *
 * The nibble forms are *expanded*, not zero-padded: `#f00` is (0xff, 0, 0), and
 * reading it as (0x0f, 0, 0) is the classic three-digit mistake. `#rgba` and
 * `#rrggbbaa` carry alpha; the other two are opaque.
 *
 * @return 1 and writes @p out on success; 0 otherwise, leaving @p out alone.
 */
int robo_css_color_from_hex(robo_css_slice_t digits, robo_css_color_t *out);

/**
 * The colour a named keyword spells (`red`, `rebeccapurple`).
 *
 * The comparison is ASCII case-insensitive — CSS keywords are — and covers the
 * full standard set, so `ReBeCcApUrPlE` is a colour and `reddish` is not.
 *
 * `transparent` answers 0 here, deliberately: it is a colour (the transparent
 * black) but not a *named* one, and it is handled where the keyword is
 * (@ref robo_css_color_from_value), so a reader looking for it does not have to
 * know it is spelled as a table entry.
 *
 * @return 1 and writes @p out on success; 0 otherwise. An unknown identifier is
 *         **not** silently a colour, which is the whole point of returning 0
 *         rather than a default.
 */
int robo_css_color_from_name(robo_css_slice_t name, robo_css_color_t *out);

/**
 * The colour an `rgb()`/`rgba()` call spells.
 *
 * Both argument-list syntaxes are accepted — the legacy comma form
 * (`rgb(255, 0, 0)`, `rgba(255, 0, 0, .5)`) and the modern space form
 * (`rgb(255 0 0)`, `rgb(255 0 0 / 50%)`) — with numeric or percentage channels
 * and an optional alpha, because both are CSS and a build that understood one of
 * them would reject valid authoring. Mixing them (`rgb(255, 0 0)`) is refused,
 * which is CSS's own rule.
 *
 * Channels are clamped into range rather than wrapped: `rgb(300, 0, 0)` is red
 * and not a colour with a wrapped green channel. Every form of `rgb`/`rgba`
 * accepts every shape, as CSS Color 4 says, so nothing here distinguishes the
 * two names.
 *
 * @param function a FUNCTION component value named `rgb` or `rgba`.
 * @param out      receives the colour; untouched on failure.
 * @return 1 when the call parsed to a colour; 0 otherwise — including for a
 *         function of the wrong name, a missing or extra argument, a bad
 *         separator, or a non-numeric argument.
 */
int robo_css_color_from_rgb_function(const robo_css_value_t *function,
                                     robo_css_color_t *out);

/**
 * The colour an `hsl()`/`hsla()` call spells.
 *
 * The hue is an angle: a bare number is degrees, and `deg`, `grad`, `rad` and
 * `turn` are the four angle units, because `hsl(0.25turn …)` is ordinary
 * authoring and refusing it would be a limitation rather than a decision. Hue is
 * normalised rather than clamped — `-120` and `240` are the same hue, and
 * `720` is `0` — which is CSS's rule and the reason a wheel can be written
 * either way round.
 *
 * Saturation and lightness are clamped to 0–100% (CSS permits the overshoot and
 * clamps it), and they must be percentages in the legacy comma syntax; the modern
 * space syntax also accepts bare numbers, which CSS Color 4 reads as percentages.
 * Alpha follows the same rules as `rgb()`: a number 0–1 or a percentage.
 *
 * The conversion is CSS Color 4's own, done in doubles once and rounded once, so
 * the result agrees with the hex spelling of the same colour.
 *
 * @return 1 when the call parsed to a colour; 0 otherwise.
 */
int robo_css_color_from_hsl_function(const robo_css_value_t *function,
                                     robo_css_color_t *out);

/**
 * What a parse produced, when "did it work" is not enough of an answer.
 *
 * A colour-valued property has to distinguish four outcomes, and collapsing them
 * loses exactly the information an author needs:
 *
 *   RESOLVED    the value is a colour, and @p out holds it
 *   CURRENT     the value is `currentColor`, and @p out says so
 *   DEFERRED    the value is a colour *function this build cannot represent*
 *               (`oklch()`, `color()`, …). The declaration is legal CSS and
 *               calling it invalid would be a lie; the caller keeps the tokens.
 *   MALFORMED   the value is a colour function, written wrong (`rgb(1)`,
 *               `hsl(0, 100%)`). It is a mistake, and it is reported as one —
 *               with the function named, so the message says *which* call.
 *   UNKNOWN_NAME  an identifier that is not a colour keyword: `nonsense`.
 *   INVALID     the value is not a colour at all, and not an identifier either
 *               (`10px`, `red blue`, nothing).
 *
 * MALFORMED, UNKNOWN_NAME and INVALID all end as an invalid computed value for a
 * colour property. They are separate because the *diagnostic* differs, and a
 * developer reading "rgb() arguments are out of range or malformed",
 * "unknown colour name" or "not a colour" learns three different things about
 * the declaration they wrote. Collapsing them into one would be less code and
 * less information, and the information is the only reason a diagnostic exists.
 */
typedef enum robo_css_color_parse {
    /** Not a colour: the value means nothing here. */
    ROBO_CSS_COLOR_PARSE_INVALID = 0,
    /** A colour: @p out holds the channels. */
    ROBO_CSS_COLOR_PARSE_RESOLVED = 1,
    /** `currentColor`: @p out holds the CURRENT kind. */
    ROBO_CSS_COLOR_PARSE_CURRENT = 2,
    /** A colour function this build does not represent; keep the tokens. */
    ROBO_CSS_COLOR_PARSE_DEFERRED = 3,
    /** A colour function written wrong. */
    ROBO_CSS_COLOR_PARSE_MALFORMED = 4,
    /** An identifier that is not a colour keyword. */
    ROBO_CSS_COLOR_PARSE_UNKNOWN_NAME = 5
} robo_css_color_parse_t;

/** Number of defined outcomes. */
#define ROBO_CSS_COLOR_PARSE_COUNT 6u

/** Non-zero when @p outcome is a defined enumerator. */
int robo_css_color_parse_is_known(robo_css_color_parse_t outcome);

/** Human-readable outcome name ("resolved", "deferred"); never NULL. */
const char *robo_css_color_parse_name(robo_css_color_parse_t outcome);

/**
 * The colour a function call spells, whatever function it is.
 *
 * The dispatch, and the only place a function name is compared against the
 * colour syntax: `rgb`/`rgba` and `hsl`/`hsla` come back RESOLVED, a colour
 * function this build cannot represent comes back DEFERRED, a malformed call
 * comes back MALFORMED, and any other function — including `currentColor(...)`,
 * which is not a thing, since the keyword is an ident — is INVALID.
 *
 * @param out receives the colour for RESOLVED and CURRENT; left NONE otherwise.
 */
robo_css_color_parse_t robo_css_color_from_function(const robo_css_value_t *function,
                                                    robo_css_color_t *out);

/**
 * The colour one specified value spells, whatever shape it is.
 *
 * The door the value layer uses, and the one that knows every spelling: an
 * identifier (a named colour, `transparent`, or `currentColor`), a `#…` hash, or
 * a function. It requires exactly one significant component — a declaration's
 * value arrives as a list, so `red blue` is not a colour — and it decides the
 * outcome without ever resolving `currentColor` against a default, because a
 * default is exactly what `currentColor` does not have.
 *
 * The outcome distinguishes an identifier that is not a colour keyword
 * (UNKNOWN_NAME) from a value that is not an identifier at all (INVALID), which
 * is the difference between "you misspelled a colour" and "this is not a colour
 * property's value".
 *
 * @param specified the component-value tree as declared.
 * @param out       receives the colour for RESOLVED and CURRENT; NONE otherwise.
 */
robo_css_color_parse_t robo_css_color_from_value(const robo_css_value_t *specified,
                                                 robo_css_color_t *out);

/**
 * Non-zero when @p name is a colour *function* this build recognises and cannot
 * represent — `hwb`, `lab`, `lch`, `oklab`, `oklch`, `color`, `color-mix`,
 * `device-cmyk`.
 *
 * A predicate rather than a list at the call site, because this is a fact about
 * the colour syntax and belongs with the rest of it. It exists so that "legal CSS
 * this build defers" and "a function nobody has heard of" stay distinguishable:
 * the first is carried, the second is refused.
 */
int robo_css_color_function_is_unrepresentable(robo_css_slice_t name);

/* ------------------------------------------------------------------ */
/* Contextual resolution                                               */
/* ------------------------------------------------------------------ */

/**
 * Resolve @p color against the `color` that is in scope, in place of
 * `currentColor`.
 *
 * @p current_color is the colour `currentColor` stands for *here*, and which
 * colour that is depends on the property being computed — a fact the computed
 * style owns, because it is the only layer that knows the property and the
 * parent (see RoboCSSComputed.h). This function does the one substitution and
 * nothing else:
 *
 *   RGBA     unchanged, with @p current_color never read
 *   CURRENT  becomes @p current_color, when that is itself resolved
 *   NONE     unchanged; there was no colour to resolve
 *   INVALID  unchanged; it was never a colour to begin with
 *
 * A CURRENT with a `currentColor` (or absent) @p current_color stays CURRENT:
 * there is nothing to substitute, and inventing black would be inventing an
 * answer. That is also what makes the resolution honest for an element whose
 * parent's computed style does not exist.
 *
 * @return 1 when @p out holds a resolved colour; 0 when the colour remains
 *         contextual or is not a colour at all.
 */
int robo_css_color_resolve_current(robo_css_color_t color, robo_css_color_t current_color,
                                   robo_css_color_t *out);

/* ------------------------------------------------------------------ */
/* Serialization                                                       */
/* ------------------------------------------------------------------ */

/**
 * Capacity @ref robo_css_color_write needs: `#rrggbbaa` and a NUL.
 *
 * The longest spelling this build writes is nine bytes. Named colours are
 * deliberately *not* written back as names: a colour that arrived as
 * `rebeccapurple` is the same colour as one that arrived as `#663399`, and
 * serializing the one you happened to write would make two equal values print
 * differently. Hex it is, always.
 */
#define ROBO_CSS_COLOR_TEXT_MAX 16u

/**
 * Write @p color's canonical text into @p buffer, NUL-terminated.
 *
 * One stable spelling per kind, and stable in the sense the CSSOM cares about:
 * lowercase hex, no shorthand, and alpha present only when it is not opaque —
 * `#ff0000` for an opaque red and `#ff000080` for a half-transparent one.
 * `currentColor` keeps its camel-case spelling because that is how CSS writes
 * the keyword, and it is one spelling and not two.
 *
 * NONE and INVALID have no CSS serialization, so they are written as the
 * *diagnostics* they are: `none` and `invalid`. This is a diagnostic rendering
 * and not a CSSOM serialization — see RoboCSSComputedValue.h for why the text
 * boundary is deliberately not the CSSOM's.
 *
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param out_length non-NULL; receives the length written, excluding the NUL.
 * @param diag       optional diagnostic sink.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument or an unknown kind;
 *         ROBO_NOT_SUPPORTED when the text does not fit, in which case
 *         @p out_length receives the length it would have needed.
 */
robo_result_t robo_css_color_write(robo_css_color_t color, char *buffer, size_t capacity,
                                   size_t *out_length, robo_css_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_COLOR_H */
