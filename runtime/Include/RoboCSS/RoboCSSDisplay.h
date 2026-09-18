/*
 * RoboCSS display vocabulary (Phase 9.12).
 *
 * `display` is the one property every later phase reads on every element, and it
 * is not a word: `display: block` says three separate things at once — a box is
 * generated, it is block-level, and its contents are laid out in normal flow —
 * while `display: none` says the first of those is false, which is a different
 * *kind* of answer and not a smaller one. Writing that as one identifier in one
 * field is how `display: none` comes to be modelled as zero geometry, and zero
 * geometry is exactly what it is not: it is the absence of a box.
 *
 * So a display value here is three facts, and they are the three CSS Display 3
 * names for them:
 *
 *   the value         one of a closed set, spelled by exactly one keyword
 *   the outer display what kind of box-level it is: block, inline, or none at all
 *   the inner display what lays out its contents: normal flow, or a new
 *                     formatting context (`flow-root`)
 *
 * They live in a table with one row per value, and every question below is asked
 * of the table rather than answered by a comparison against a spelling. That is
 * the same decision the unit vocabulary made in 9.10 and the box vocabulary made
 * in 9.11, and it is what makes the *next* display value an added row: `flex` is
 * an outer `block` with an inner `flex`, `grid` likewise, `inline-flex` an outer
 * `inline` with an inner `flex`, `contents` a value that generates no box and is
 * therefore not `none`. None of those is claimed here — this build supports the
 * four values it implements and rejects every other identifier, including the
 * ones this table is deliberately shaped to accept later.
 *
 * The inner column is not decoration, and it is why `inline-block` is not a
 * synonym for `inline` with a flag: an atomic inline-level box *is* an inline
 * outer with a `flow-root` inner, which is the same statement CSS makes when it
 * defines `inline-block` as `inline flow-root`. A build that modelled it as
 * "inline, but also atomic" would have a fact of its own to keep in sync with the
 * spec; this one has a row.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides whether `display` inherits or what its
 * initial value is (RoboCSSPropertyRegistry.h). Not a value parser: the tokens are
 * the tokenizer's and the component values are the value layer's, and what arrives
 * here is one identifier's text. Not a layout: nothing here measures a box, and no
 * function in this header takes or returns a rectangle. And it is deliberately not
 * a statement about *elements*: HTML's own default for `div` is `block` and for
 * `span` is `inline`, and neither fact is spelled anywhere below, because that is a
 * user-agent *stylesheet* and this is a vocabulary. A `div` with no declaration
 * therefore computes to the property's initial value like anything else, and the
 * phase that ships user-agent styles is the phase where a `div` becomes block.
 *
 * What it does not implement, and says so
 *
 * The multi-keyword display syntax (`display: inline flow-root`, `display: block
 * flex`) is legal CSS 3 and is **not** parsed here: it needs the two-keyword
 * grammar, and the four rows above are single-keyword values. A declaration
 * written that way is refused with a diagnostic naming that reason rather than
 * being guessed at or carried as an unreadable token tree — `display` is what
 * layout consumes, and a value nothing can read is worse than one that was
 * dropped by CSS's own rule for a declaration the parser cannot use. The
 * outer/inner columns are the extension point for it, exactly as they are for
 * `flex` and `grid`.
 */

#ifndef ROBO_CSS_DISPLAY_H
#define ROBO_CSS_DISPLAY_H

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
 * A computed `display`.
 *
 * A closed set, and the enumerator values are append-only because a stored
 * enumerator is a contract: `flex` joins as 4 rather than reshuffling what a
 * previous phase wrote down. NONE is 0 and is a *value* — `display: none` — not
 * an absence, so a record carrying this field is only meaningful when the record
 * says it holds a display value; a cleared record reads as NONE for the same
 * reason a cleared colour record reads as the colour NONE, and it is guarded the
 * same way (see RoboCSSComputedValue.h).
 */
typedef enum robo_css_display {
    /** `none`: no box is generated at all. */
    ROBO_CSS_DISPLAY_NONE = 0,
    /** `block`: block-level, contents in normal flow. */
    ROBO_CSS_DISPLAY_BLOCK = 1,
    /** `inline`: inline-level, contents in normal flow. */
    ROBO_CSS_DISPLAY_INLINE = 2,
    /** `inline-block`: inline-level, contents in a new formatting context. */
    ROBO_CSS_DISPLAY_INLINE_BLOCK = 3
} robo_css_display_t;

/** Number of defined display values. */
#define ROBO_CSS_DISPLAY_COUNT 4u

/**
 * What kind of box a display value generates, or that it generates none.
 *
 * The *outer* display, in CSS Display 3's terms, and the fact that decides
 * whether an element participates in its parent's block or inline formatting
 * context. It is apart from the value itself because it is the half of a display
 * value that a future two-keyword syntax varies independently of the other half.
 */
typedef enum robo_css_display_outer {
    /** No box: `none`. */
    ROBO_CSS_DISPLAY_OUTER_NONE = 0,
    /** A block-level box. */
    ROBO_CSS_DISPLAY_OUTER_BLOCK = 1,
    /** An inline-level box. */
    ROBO_CSS_DISPLAY_OUTER_INLINE = 2
} robo_css_display_outer_t;

/** Number of defined outer displays. */
#define ROBO_CSS_DISPLAY_OUTER_COUNT 3u

/**
 * What lays out a display value's contents.
 *
 * Only ever asked about a value that generates a box, which is why NONE is the
 * answer for `display: none` and for an unknown enumerator rather than an error:
 * a value that generates no box has no contents to lay out.
 */
typedef enum robo_css_display_inner {
    /** No contents to lay out because there is no box. */
    ROBO_CSS_DISPLAY_INNER_NONE = 0,
    /** Normal flow, sharing its formatting context with its siblings. */
    ROBO_CSS_DISPLAY_INNER_FLOW = 1,
    /** A new block formatting context of its own — CSS's `flow-root`. */
    ROBO_CSS_DISPLAY_INNER_FLOW_ROOT = 2
} robo_css_display_inner_t;

/** Number of defined inner displays. */
#define ROBO_CSS_DISPLAY_INNER_COUNT 3u

/** Non-zero when @p display is one of the four. NONE is defined. */
int robo_css_display_is_known(robo_css_display_t display);

/** Human-readable name ("none", "inline-block"); never NULL, "" for unknown. */
const char *robo_css_display_name(robo_css_display_t display);

/** Number of rows in the vocabulary. */
size_t robo_css_display_count(void);

/* ------------------------------------------------------------------ */
/* Parsing and serialization                                           */
/* ------------------------------------------------------------------ */

/**
 * The display value an identifier spells, or NONE when it spells none of them.
 *
 * ASCII case-insensitive, as CSS spells every keyword: `BLOCK` and `block` are
 * the same display. The comparison is against the canonical spellings only, so
 * `inline-block` matches and `inlineblock` does not, and an unknown identifier
 * — `bogus`, `flex`, `table` — answers NONE rather than matching a prefix.
 *
 * NONE is both a value and the answer for "not one", which is the one ambiguity
 * this interface has — and unlike the unit vocabulary it cannot be resolved by a
 * predicate on the *result*, because `none` is a display value a caller may
 * legitimately be asking about. "Did this identifier name a display value" is
 * therefore a question about the *identifier*, and it has a door of its own:
 *
 *     if (!robo_css_display_slice_is_known(text)) { refuse(text); }
 *     else { use(robo_css_display_from_slice(text)); }
 *
 * A caller that has already established the identifier is one of the four — the
 * computed-value layer asks both, in that order — can use this function alone.
 */
robo_css_display_t robo_css_display_from_slice(robo_css_slice_t keyword);

/** NUL-terminated flavour of the above. */
robo_css_display_t robo_css_display_from_text(const char *keyword);

/**
 * Whether @p keyword spells one of the vocabulary, case-insensitively.
 *
 * The door that keeps `display: none` and `display: bogus` apart: both answer
 * NONE to @ref robo_css_display_from_slice, and only the first answers 1 here.
 */
int robo_css_display_slice_is_known(robo_css_slice_t keyword);

/**
 * Write @p display's canonical keyword into @p buffer, NUL-terminated.
 *
 * One stable spelling per value, and it is the keyword CSS writes: lowercase,
 * hyphen included, `inline-block` never `inline_block`. A display value that
 * cannot be written back as the keyword an author would recognise is a value a
 * developer cannot debug.
 *
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param out_length non-NULL; receives the length written, excluding the NUL.
 * @param diag       optional diagnostic sink.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument or an unknown
 *         enumerator; ROBO_NOT_SUPPORTED when the keyword does not fit, in which
 *         case @p out_length receives the length it would have needed.
 */
robo_result_t robo_css_display_write(robo_css_display_t display,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_css_diag_t *diag);

/** Capacity @ref robo_css_display_write needs for any value in the vocabulary. */
#define ROBO_CSS_DISPLAY_TEXT_MAX 16u

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

/**
 * Whether @p display generates a box at all.
 *
 * The question `display: none` answers no to, and the one a layout pass asks
 * before anything else. It is asked of the outer display rather than by
 * comparing against NONE, and the two agree today — they will not once
 * `contents` is a row, because `display: contents` generates no box while being
 * emphatically *not* `display: none`: its children are still generated and still
 * laid out, in the place their parent would have been.
 */
int robo_css_display_generates_box(robo_css_display_t display);

/**
 * Whether @p display is exactly `none`.
 *
 * The other half of the question above, and separate from it on purpose: what a
 * layout pass needs to know first is "is there a box", and `contents` will make
 * that a different answer from "was this written as none".
 */
int robo_css_display_is_none(robo_css_display_t display);

/** Whether @p display generates a block-level box. */
int robo_css_display_is_block_level(robo_css_display_t display);

/** Whether @p display generates an inline-level box. */
int robo_css_display_is_inline_level(robo_css_display_t display);

/**
 * Whether @p display is an atomic inline-level box — `inline-block`.
 *
 * Derived from the two columns rather than from the spelling: an atomic inline
 * is an inline outer with a formatting context of its own, which is the same
 * statement as CSS's own `inline-block` = `inline flow-root`. A future
 * `inline-flex` is inline-level and atomic in the same sense, and this predicate
 * will have to say *which* inner display it has rather than merely that it is not
 * flow — which is exactly the question @ref robo_css_display_inner answers.
 */
int robo_css_display_is_inline_block(robo_css_display_t display);

/** @p display's outer display, or OUTER_NONE for an unknown enumerator. */
robo_css_display_outer_t robo_css_display_outer(robo_css_display_t display);

/** @p display's inner display, or INNER_NONE when there is no box or no such row. */
robo_css_display_inner_t robo_css_display_inner(robo_css_display_t display);

/** Human-readable outer display name ("block", "inline"); never NULL. */
const char *robo_css_display_outer_name(robo_css_display_outer_t outer);

/** Human-readable inner display name ("flow", "flow-root"); never NULL. */
const char *robo_css_display_inner_name(robo_css_display_inner_t inner);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_DISPLAY_H */
