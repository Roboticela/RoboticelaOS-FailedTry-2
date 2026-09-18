/*
 * RoboCSS positioning vocabulary (Phase 9.13).
 *
 * `position` decides what an element's box is positioned *relative to*, and the
 * five answers are not five sizes of one behaviour: `static` is not positioned at
 * all (its `top`/`left` are ignored), `relative` is in flow but shifted from where
 * flow put it, `absolute` is lifted out of flow and placed against an ancestor,
 * `fixed` is lifted out and placed against the viewport, and `sticky` is in flow
 * until a scroll constraint moves it. Modelling that as one identifier in one
 * field is how `top: 10px` comes to move a `static` element — the mistake CSS's own
 * rule (`top` does not apply to `position: static`) exists to prevent.
 *
 * So the vocabulary is a table with one row per value, and each row carries the
 * three facts the value *is*:
 *
 *   the value        one of a closed set, spelled by exactly one keyword
 *   flow             whether the box stays in normal flow — the fact that decides
 *                    whether the box's space is reserved for it
 *   positioned       whether the box is a *positioned* box — the fact that decides
 *                    whether its offsets apply at all, and whether it is a
 *                    containing block for an absolutely positioned descendant
 *
 * The two flags are columns rather than one three-valued "kind" because they are
 * asked separately and by different phases: flow is the layout question (does this
 * box take up room in its parent), and positioned is the containment question (what
 * does an offset inside this box resolve against). Every question below is asked of
 * the table rather than answered by a comparison against a spelling — the decision
 * the unit vocabulary made in 9.10, the box vocabulary in 9.11 and the display
 * vocabulary in 9.12, and it is what makes the *next* value an added row. CSS
 * Position 4 has `position: running()` and the `sticky` variants; neither is
 * claimed here, and this build rejects every identifier it has no row for.
 *
 * The inset properties, and why they are not in this header
 *
 * `top`, `right`, `bottom` and `left` are the four values `position` moves a box
 * *by*, and their grammar is `<length-percentage> | auto` — which is the box
 * model's `EDGE_OFFSET` shape exactly, the one `margin-top` and its three
 * relatives already use (RoboCSSPropertyRegistry.h). This build therefore gives
 * them registry rows and no vocabulary of their own, and the reason is the same
 * one that made 9.11 write the box edges as a table: a second shape with the same
 * grammar is how `top: 20%` and `margin-top: 20%` come to be normalised
 * differently. Their *names* are the side vocabulary's — `robo_css_box_side_name`
 * spells `top`, `right`, `bottom` and `left` in CSS's own order
 * (RoboCSSBox.h) — so the four inset properties are the four sides, and `inset`
 * as a shorthand would be one more row in that module's table rather than
 * anything new here.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides that `position` does not inherit or that
 * its initial value is `static` (RoboCSSPropertyRegistry.h). Not a value parser:
 * the tokens are the tokenizer's and the component values are the value layer's,
 * and what arrives here is one identifier's text. And not layout: nothing below
 * computes a containing block, an offset, a stacking order or a scroll position,
 * no function here takes or returns a rectangle, and `z-index` is deliberately
 * not mentioned — it is a separate property with its own grammar (`auto` or an
 * integer) and its own shape, and the stacking context that reads it belongs to a
 * later phase.
 *
 * What it does not implement, and says so
 *
 * Sticky behaviour is *not* implemented and cannot be: `sticky` is a value here,
 * with the same two facts as any other, and it classifies as in flow and
 * positioned, which is exactly what it is before anything scrolls. What a sticky
 * box does relative to a scrollport is a scrolling question and belongs to the
 * phase that has one. Refusing the value outright would be the other wrong
 * answer: `position: sticky` is legal CSS an author has written, and dropping
 * the declaration would lose the offset values beside it.
 */

#ifndef ROBO_CSS_POSITION_H
#define ROBO_CSS_POSITION_H

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
 * A computed `position`.
 *
 * A closed set, and the enumerator values are append-only because a stored
 * enumerator is a contract: a later phase's value joins as 5 rather than
 * reshuffling what a previous phase wrote down. STATIC is 0 and is a *value* —
 * the initial one — so a record carrying this field is only meaningful when the
 * record says it holds a position value; a cleared record reads as STATIC for the
 * same reason a cleared display record reads as NONE, and it is guarded the same
 * way (see RoboCSSComputedValue.h).
 */
typedef enum robo_css_positioning {
    /** `static`: not positioned; an offset does not apply to it. */
    ROBO_CSS_POSITIONING_STATIC = 0,
    /** `relative`: in flow, then shifted from where flow put it. */
    ROBO_CSS_POSITIONING_RELATIVE = 1,
    /** `absolute`: out of flow, placed against a positioned ancestor's box. */
    ROBO_CSS_POSITIONING_ABSOLUTE = 2,
    /** `fixed`: out of flow, placed against the viewport. */
    ROBO_CSS_POSITIONING_FIXED = 3,
    /** `sticky`: in flow until a scroll constraint moves it. */
    ROBO_CSS_POSITIONING_STICKY = 4
} robo_css_positioning_t;

/** Number of defined position values. */
#define ROBO_CSS_POSITIONING_COUNT 5u

/** Non-zero when @p position is one of the five. STATIC is defined. */
int robo_css_positioning_is_known(robo_css_positioning_t position);

/** Human-readable name ("static", "absolute"); never NULL, "" for unknown. */
const char *robo_css_positioning_name(robo_css_positioning_t position);

/** Number of rows in the vocabulary. */
size_t robo_css_positioning_count(void);

/* ------------------------------------------------------------------ */
/* Parsing and serialization                                           */
/* ------------------------------------------------------------------ */

/**
 * The position value an identifier spells, or STATIC when it spells none of them.
 *
 * ASCII case-insensitive, as CSS spells every keyword: `ABSOLUTE` and `absolute`
 * are the same position. The comparison is against the canonical spellings only,
 * so `sticky` matches and `stickyy` does not, and an unknown identifier —
 * `banana`, `running()`, `centre` — answers STATIC rather than matching a prefix.
 *
 * STATIC is both a value and the answer for "not one", which is the one ambiguity
 * this interface has — and unlike the display vocabulary it cannot be resolved by
 * a predicate on the *result*, because `static` is the initial value and therefore
 * the most likely thing a caller is asking about. "Did this identifier name a
 * position value" is a question about the *identifier*, and it has a door of its
 * own:
 *
 *     if (!robo_css_positioning_slice_is_known(text)) { refuse(text); }
 *     else { use(robo_css_positioning_from_slice(text)); }
 *
 * A caller that has already established the identifier is one of the five — the
 * computed-value layer asks both, in that order — can use this function alone.
 */
robo_css_positioning_t robo_css_positioning_from_slice(robo_css_slice_t keyword);

/** NUL-terminated flavour of the above. */
robo_css_positioning_t robo_css_positioning_from_text(const char *keyword);

/**
 * Whether @p keyword spells one of the vocabulary, case-insensitively.
 *
 * The door that keeps `position: static` and `position: banana` apart: both
 * answer STATIC to @ref robo_css_positioning_from_slice, and only the first answers
 * 1 here.
 */
int robo_css_positioning_slice_is_known(robo_css_slice_t keyword);

/**
 * Write @p position's canonical keyword into @p buffer, NUL-terminated.
 *
 * One stable spelling per value, and it is the keyword CSS writes: lowercase, no
 * hyphen, `static` never the enumerator's name. A position value that cannot be
 * written back as the keyword an author would recognise is a value a developer
 * cannot debug.
 *
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param out_length non-NULL; receives the length written, excluding the NUL.
 * @param diag       optional diagnostic sink.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument or an unknown
 *         enumerator; ROBO_NOT_SUPPORTED when the keyword does not fit, in which
 *         case @p out_length receives the length it would have needed.
 */
robo_result_t robo_css_positioning_write(robo_css_positioning_t position,
                                      char *buffer,
                                      size_t capacity,
                                      size_t *out_length,
                                      robo_css_diag_t *diag);

/**
 * Capacity @ref robo_css_positioning_write needs for any value in the vocabulary.
 *
 * The longest keyword is `absolute` (8 bytes) and a writer needs room for its NUL
 * beside it, so this is a capacity that *works* rather than the length of the
 * longest word — which is the difference the suite caught here, since a buffer of
 * exactly 8 refuses the two keywords that fill it.
 */
#define ROBO_CSS_POSITIONING_TEXT_MAX 9u

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

/**
 * Whether @p position is a *positioned* box — CSS's own term, and any value other
 * than `static`.
 *
 * The question two later stages each ask first. Offsets: `top`, `right`, `bottom`
 * and `left` apply to a positioned box and are ignored on a static one, which is
 * why the property and its four offsets are meaningless apart. Containment: a
 * positioned box is what an absolutely positioned descendant resolves its offsets
 * against, so a walk up the tree stops here.
 *
 * It is asked of the table rather than written as `!= STATIC` so that a value
 * added later can answer for itself — `position: running()` is not positioned in
 * this sense, and a comparison against STATIC would have to be revisited then.
 * An unknown enumerator answers 0: a value nothing defined is not positioned.
 */
int robo_css_positioning_is_positioned(robo_css_positioning_t position);

/**
 * Whether @p position leaves the box in normal flow.
 *
 * True for `static`, `relative` and `sticky`: the box takes up room where flow
 * put it, and an offset shifts what is painted without moving what comes after
 * (which is the whole of what "relative" means). False for `absolute` and
 * `fixed`, whose position is taken by whoever comes next.
 */
int robo_css_positioning_is_in_flow(robo_css_positioning_t position);

/**
 * Whether @p position takes the box out of normal flow.
 *
 * The other half of the question above, and separate from it on purpose: an
 * unknown enumerator answers 0 to *both*, because "this box is somewhere else" is
 * a claim a value nothing defined cannot support. `absolute` and `fixed` answer 1
 * — they differ in what their offsets resolve against, which is a containing-block
 * question and not a flow one.
 */
int robo_css_positioning_is_out_of_flow(robo_css_positioning_t position);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_POSITION_H */
