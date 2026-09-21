/*
 * RoboCSS opacity vocabulary (Phase 11.9).
 *
 * `opacity` is the first property in this build whose value is **not a colour and not a
 * box**, and the first whose value is a *range* rather than a closed set of keywords. That
 * makes it a different kind of value from every other one in this layer, and three facts
 * about it decide this module's whole shape.
 *
 * The value is a real in [0, 1], and the initial value is 1
 *
 * CSS writes the value as `<alpha-value>`: a number, or a percentage that means the same
 * number divided by a hundred. `1` is fully opaque, `0` is fully transparent, and an
 * element that declares nothing is **fully opaque** -- which is the fact this module exists
 * to keep from being lost, because the dangerous default is the other one. A computed-value
 * record that answered `0` for "nothing was declared" would make every element in a
 * document without an `opacity` declaration invisible, and the mistake would be a *silent*
 * one: the geometry would be right, the layout would be right, and the page would be blank.
 * So the initial value is a door here (`robo_css_opacity_initial`) rather than a literal at
 * a call site, and it is what every accessor in this layer answers for a record that is not
 * an opacity.
 *
 * Out of range clamps, and a value that is not a number is not an opacity
 *
 * CSS does not reject `opacity: 2` or `opacity: -1` -- it **clamps** them at computed-value
 * time, and the clamp is the specification rather than a kindness: an author who wrote
 * `opacity: 150%` meant "as opaque as it goes", and an INVALID value there would make a
 * document that renders correctly in every browser render as if the declaration were
 * absent here. So `robo_css_opacity_clamp` is the range rule and the only place `[0, 1]`
 * appears; the *parse* refusals are a different question and they belong to the value:
 * `opacity: banana` is a mistake, and `opacity: calc(1 / 3)` is legal CSS this build does
 * not evaluate, which is why the two are separated by
 * `robo_css_numeric_from_value`'s answer one layer up rather than by this module guessing.
 *
 * A value that is not a number *at all* -- a NaN from arithmetic that went wrong -- is
 * answered with the initial value rather than with zero, and the direction is the same
 * choice the paragraph above makes: a record nothing can read must render the way an
 * element nothing declared does, which is *visible*. A NaN silently becoming transparent
 * would make a bug in a caller's arithmetic look like a document that draws nothing.
 *
 * Opacity is not a colour's alpha, and the two are deliberately separate
 *
 * `rgba(0, 0, 0, 0.5)` and `opacity: 0.5` are different declarations: the first makes the
 * *colour* half transparent, the second makes the *element* half transparent -- its
 * background, its border, its text and its descendants all composited at that strength.
 * Both end up as a factor on the alpha a surface receives, and they are still two facts:
 * one belongs to a colour and travels with it, the other belongs to a box and is inherited
 * by nothing. Keeping the two apart here is what lets the paint layer apply one without
 * touching the other, and the combination is exactly the product --
 * `robo_paint_opacity_multiply` owns the accumulation, because "what is in force" is a
 * paint question and not a value question.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides whether `opacity` inherits or what its initial value
 * is (RoboCSSPropertyRegistry.h), and nothing here knows the property's name. Not a value
 * parser: what arrives from the compute layer is a `robo_css_numeric_t` that already
 * resolved, and this module is handed its magnitude. Not a paint layer: it composes no two
 * opacities and knows nothing about a tree, a box or a stack.
 */

#ifndef ROBO_CSS_OPACITY_H
#define ROBO_CSS_OPACITY_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The property's initial value: fully opaque.
 *
 * A door rather than a `1.0` at each call site, and the reason is the one the header
 * argues: the difference between "fully opaque" and "fully transparent" is the difference
 * between a document and a blank page, and a literal spelled in five places is five places
 * for one of them to be zero.
 */
double robo_css_opacity_initial(void);

/** @p opacity fully transparent (exactly zero): an element that draws nothing. */
int robo_css_opacity_is_transparent(double opacity);

/** @p opacity fully opaque (exactly one): the initial value, and the state in which an
 *  element's own opacity changes nothing about it. */
int robo_css_opacity_is_opaque(double opacity);

/**
 * Non-zero when @p opacity is a value this build may hand to a consumer: **finite** and
 * within `[0, 1]`.
 *
 * A predicate on the *value* rather than a status, so a caller asking "is this a record I
 * can draw with?" gets an answer rather than an error to handle. It is strict about both
 * ends -- `0` and `1` are valid, `-0.0` is `0`, and a NaN is not valid -- and it is the
 * rule `robo_css_opacity_clamp` establishes rather than a second, looser one.
 */
int robo_css_opacity_is_valid(double opacity);

/**
 * @p opacity clamped into `[0, 1]`.
 *
 * CSS's own rule for the property, and the only place the range appears: a value below zero
 * is fully transparent, one above one is fully opaque, and a value that is **not a number**
 * (a NaN, an infinity) is answered with @ref robo_css_opacity_initial -- see the header
 * comment for why the direction is "visible" rather than "invisible". An input already in
 * range is returned unchanged, so this is safe to apply twice and safe to apply to a value
 * a caller has not questioned.
 */
double robo_css_opacity_clamp(double opacity);

/**
 * The opacity a **percentage** means: @p percentage divided by a hundred.
 *
 * CSS's `<alpha-value>` accepts both spellings and they are the same value, so the division
 * happens here -- the one place that knows a percentage of *this* property is a proportion
 * of one rather than of a box. The result is **not** clamped: a caller applies
 * @ref robo_css_opacity_clamp to it exactly as it does to a number, so the two spellings
 * meet the same rule at the same point rather than each having one.
 */
double robo_css_opacity_from_percentage(double percentage);

/**
 * Write @p opacity as CSS spells it, NUL-terminated.
 *
 * The value is printed the way an author would recognise it -- `1` and `0` as integers
 * rather than `1.000000`, and anything else to as many decimals as it takes to distinguish
 * it from its neighbours (`0.5`, `0.25`, `0.333333`) -- because a value that cannot be
 * written back as something a developer reads is a value a developer cannot debug. A value
 * that is not one (`robo_css_opacity_is_valid` clear) is **refused** rather than printed, so
 * a caller never receives text for a number this build would not draw with.
 *
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param out_length non-NULL; receives the length written, excluding the NUL.
 * @param diag       optional diagnostic sink.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument or an @p opacity that is not a
 *         value; ROBO_NOT_SUPPORTED when the text does not fit, in which case @p out_length
 *         receives the length it would have needed.
 */
robo_result_t robo_css_opacity_write(double opacity,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_css_diag_t *diag);

/** Capacity @ref robo_css_opacity_write needs for any value in the range. */
#define ROBO_CSS_OPACITY_TEXT_MAX 24u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_OPACITY_H */
