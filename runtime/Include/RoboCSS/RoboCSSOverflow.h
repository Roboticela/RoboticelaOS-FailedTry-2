/*
 * RoboCSS overflow vocabulary (Phase 10.13).
 *
 * `overflow` is the first property in this build whose value is not about the box
 * at all but about the **room the box has**: what happens to contents that do not
 * fit inside the box an author gave them. That makes it a different kind of value
 * from every other one in this layer, and two facts about it decide this module's
 * whole shape.
 *
 * The value is per **axis**, and the axes are separate properties
 *
 * CSS writes `overflow` as a shorthand over `overflow-x` and `overflow-y`, and the
 * two longhands are what everything downstream reads: a box can clip horizontally
 * and not vertically, and a build that collapsed the pair into one value could not
 * express the document that does. So the vocabulary is one *value* -- `visible` or
 * `hidden` -- and the axis belongs to the **property**, not to the value:
 * `overflow-x: hidden` and `overflow-y: hidden` are two typed values of this
 * vocabulary, and a consumer reads them one axis at a time.
 *
 * The shorthand is read rather than expanded, and saying so is the honest boundary
 *
 * No phase in this runtime expands a shorthand yet -- `margin`, `padding`, `border`
 * and `background` are registered as shorthands and *carry their tokens* while their
 * longhands are what anything reads. `overflow` could have taken the same road, and
 * it deliberately does not: `overflow: hidden` is a declaration an author writes on
 * nearly every scrolling-looking box, and dropping it because the longhand expansion
 * machinery does not exist would make a common document lay out as if nobody had
 * written anything. So the *shorthand's own name* is registered on this vocabulary
 * too, and a consumer that finds no longhand reads it as speaking for both axes --
 * which is exactly what the shorthand means, one step earlier than CSS computes it.
 * What that costs is stated where it matters: `overflow: hidden visible` is a legal
 * shorthand this build does not read, because reading it needs the expansion that
 * this vocabulary's registration deliberately works around.
 *
 * Two values, and the closed-vocabulary precedent
 *
 * The set is closed, like `display`'s, `position`'s and `border-style`'s, and for the
 * same reason: `overflow` is what a layout pass *dispatches* on -- "does this box
 * clip?" is a question whose answer changes geometry -- so `overflow: banana` has to
 * be a mistake rather than a keyword carrying the text `banana` that the consuming
 * phase silently fails to interpret. `scroll`, `auto` and `clip` are rows this build
 * has not added rather than words it does not know: `scroll` and `auto` are
 * *scrolling*, which needs a scrollport, scrollbars and a scroll offset that no phase
 * in this build has, and `clip` is a clipping rule this build's single boundary does
 * not distinguish from `hidden`. Each is named here so the phase that adds it knows
 * where it goes.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides whether `overflow` inherits or what its
 * initial value is (RoboCSSPropertyRegistry.h). Not a value parser: what arrives is
 * one identifier's text. Not a layout: nothing here measures a box, and no function
 * below takes or returns a rectangle -- a value says *whether* an axis clips, and
 * where the boundary is belongs to the phase that has the box.
 */

#ifndef ROBO_CSS_OVERFLOW_H
#define ROBO_CSS_OVERFLOW_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The value                                                           */
/* ------------------------------------------------------------------ */

/**
 * A computed `overflow` value.
 *
 * A closed set, and the enumerator values are append-only because a stored
 * enumerator is a contract: `scroll` joins as 2 rather than reshuffling what a
 * previous phase wrote down. VISIBLE is **0** because it is the property's initial
 * value, so a cleared record holds the state in which an author who declared
 * nothing finds their box -- which is what @ref robo_css_computed_value_init writes
 * and what a caller who only zeroed its struct has. A vocabulary whose zero meant
 * `hidden` would make a forgotten initialisation a promise to clip.
 */
typedef enum robo_css_overflow {
    /** `visible`: contents that do not fit are drawn outside the box. */
    ROBO_CSS_OVERFLOW_VISIBLE = 0,
    /** `hidden`: contents that do not fit are clipped to the box's padding edge. */
    ROBO_CSS_OVERFLOW_HIDDEN = 1
} robo_css_overflow_t;

/** Number of defined overflow values. */
#define ROBO_CSS_OVERFLOW_COUNT 2u

/** Non-zero when @p overflow is one of the two. VISIBLE is defined. */
int robo_css_overflow_is_known(robo_css_overflow_t overflow);

/** Human-readable name ("visible", "hidden"); never NULL, "" for unknown. */
const char *robo_css_overflow_name(robo_css_overflow_t overflow);

/** Number of rows in the vocabulary. */
size_t robo_css_overflow_count(void);

/* ------------------------------------------------------------------ */
/* Parsing and serialization                                           */
/* ------------------------------------------------------------------ */

/**
 * The overflow value an identifier spells, or VISIBLE when it spells none of them.
 *
 * ASCII case-insensitive, as CSS spells every keyword: `HIDDEN` and `hidden` are the
 * same overflow. The comparison is against the canonical spellings only, so `hidden`
 * matches and `hiddenly` does not, and an unknown identifier -- `bogus`, `scroll`,
 * `auto` -- answers VISIBLE rather than matching a prefix.
 *
 * VISIBLE is both a value and the answer for "not one", which is the one ambiguity
 * this interface has -- and unlike the unit vocabulary it cannot be resolved by a
 * predicate on the *result*, because `visible` is a value a caller may legitimately
 * be asking about. "Did this identifier name an overflow value" is therefore a
 * question about the *identifier*, and it has a door of its own:
 *
 *     if (!robo_css_overflow_slice_is_known(text)) { refuse(text); }
 *     else { use(robo_css_overflow_from_slice(text)); }
 *
 * A caller that has already established the identifier is one of the two -- the
 * computed-value layer asks both, in that order -- can use this function alone.
 */
robo_css_overflow_t robo_css_overflow_from_slice(robo_css_slice_t keyword);

/** NUL-terminated flavour of the above. */
robo_css_overflow_t robo_css_overflow_from_text(const char *keyword);

/**
 * Whether @p keyword spells one of the vocabulary, case-insensitively.
 *
 * The door that keeps `overflow: visible` and `overflow: bogus` apart: both answer
 * VISIBLE to @ref robo_css_overflow_from_slice, and only the first answers 1 here.
 */
int robo_css_overflow_slice_is_known(robo_css_slice_t keyword);

/**
 * Write @p overflow's canonical keyword into @p buffer, NUL-terminated.
 *
 * One stable spelling per value, and it is the keyword CSS writes: lowercase, and
 * `visible` never `VISIBLE`. A value that cannot be written back as the keyword an
 * author would recognise is a value a developer cannot debug.
 *
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param out_length non-NULL; receives the length written, excluding the NUL.
 * @param diag       optional diagnostic sink.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument or an unknown
 *         enumerator; ROBO_NOT_SUPPORTED when the keyword does not fit, in which
 *         case @p out_length receives the length it would have needed.
 */
robo_result_t robo_css_overflow_write(robo_css_overflow_t overflow,
                                      char *buffer,
                                      size_t capacity,
                                      size_t *out_length,
                                      robo_css_diag_t *diag);

/** Capacity @ref robo_css_overflow_write needs for any value in the vocabulary. */
#define ROBO_CSS_OVERFLOW_TEXT_MAX 8u

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

/**
 * Whether @p overflow **clips** its axis.
 *
 * The one fact a consumer of this property dispatches on, and the reason it is a
 * door on the vocabulary rather than a comparison against `HIDDEN` at each call
 * site: it is the question a layout pass asks (`does this axis have a boundary?`)
 * and the question a paint pass asks (`is there a rectangle to clip to?`), and the
 * phase that adds `clip` will have to answer it *differently* from `hidden` -- which
 * is a change in one table row here rather than in every caller.
 */
int robo_css_overflow_clips(robo_css_overflow_t overflow);

/** Whether @p overflow is the initial value `visible`. */
int robo_css_overflow_is_visible(robo_css_overflow_t overflow);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_OVERFLOW_H */
