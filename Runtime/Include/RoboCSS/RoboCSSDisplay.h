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
typedef enum CssDisplay {
    /** `none`: no box is generated at all. */
    RoboCssDisplayNone = 0,
    /** `block`: block-level, contents in normal flow. */
    RoboCssDisplayBlock = 1,
    /** `inline`: inline-level, contents in normal flow. */
    RoboCssDisplayInline = 2,
    /** `inline-block`: inline-level, contents in a new formatting context. */
    RoboCssDisplayInlineBlock = 3
} CssDisplay;

/** Number of defined display values. */
#define RoboCssDisplayCount 4u

/**
 * What kind of box a display value generates, or that it generates none.
 *
 * The *outer* display, in CSS Display 3's terms, and the fact that decides
 * whether an element participates in its parent's block or inline formatting
 * context. It is apart from the value itself because it is the half of a display
 * value that a future two-keyword syntax varies independently of the other half.
 */
typedef enum CssDisplayOuter {
    /** No box: `none`. */
    RoboCssDisplayOuterNone = 0,
    /** A block-level box. */
    RoboCssDisplayOuterBlock = 1,
    /** An inline-level box. */
    RoboCssDisplayOuterInline = 2
} CssDisplayOuter;

/** Number of defined outer displays. */
#define RoboCssDisplayOuterCount 3u

/**
 * What lays out a display value's contents.
 *
 * Only ever asked about a value that generates a box, which is why NONE is the
 * answer for `display: none` and for an unknown enumerator rather than an error:
 * a value that generates no box has no contents to lay out.
 */
typedef enum CssDisplayInner {
    /** No contents to lay out because there is no box. */
    RoboCssDisplayInnerNone = 0,
    /** Normal flow, sharing its formatting context with its siblings. */
    RoboCssDisplayInnerFlow = 1,
    /** A new block formatting context of its own — CSS's `flow-root`. */
    RoboCssDisplayInnerFlowRoot = 2
} CssDisplayInner;

/** Number of defined inner displays. */
#define RoboCssDisplayInnerCount 3u

/** Non-zero when @p display is one of the four. NONE is defined. */
int CssDisplayIsKnown(CssDisplay display);

/** Human-readable name ("none", "inline-block"); never NULL, "" for unknown. */
const char *CssDisplayName(CssDisplay display);

/** Number of rows in the vocabulary. */
size_t CssDisplayCount(void);

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
 *     if (!CssDisplaySliceIsKnown(text)) { refuse(text); }
 *     else { use(CssDisplayFromSlice(text)); }
 *
 * A caller that has already established the identifier is one of the four — the
 * computed-value layer asks both, in that order — can use this function alone.
 */
CssDisplay CssDisplayFromSlice(CssSlice keyword);

/** NUL-terminated flavour of the above. */
CssDisplay CssDisplayFromText(const char *keyword);

/**
 * Whether @p keyword spells one of the vocabulary, case-insensitively.
 *
 * The door that keeps `display: none` and `display: bogus` apart: both answer
 * NONE to @ref CssDisplayFromSlice, and only the first answers 1 here.
 */
int CssDisplaySliceIsKnown(CssSlice keyword);

/**
 * Write @p display's canonical keyword into @p buffer, NUL-terminated.
 *
 * One stable spelling per value, and it is the keyword CSS writes: lowercase,
 * hyphen included, `inline-block` never `inlineBlock`. A display value that
 * cannot be written back as the keyword an author would recognise is a value a
 * developer cannot debug.
 *
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param outLength non-NULL; receives the length written, excluding the NUL.
 * @param diag       optional diagnostic sink.
 * @return RoboOk; RoboInvalidArgument for a NULL argument or an unknown
 *         enumerator; RoboNotSupported when the keyword does not fit, in which
 *         case @p outLength receives the length it would have needed.
 */
Result CssDisplayWrite(CssDisplay display,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     CssDiag *diag);

/** Capacity @ref CssDisplayWrite needs for any value in the vocabulary. */
#define RoboCssDisplayTextMax 16u

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
int CssDisplayGeneratesBox(CssDisplay display);

/**
 * Whether @p display is exactly `none`.
 *
 * The other half of the question above, and separate from it on purpose: what a
 * layout pass needs to know first is "is there a box", and `contents` will make
 * that a different answer from "was this written as none".
 */
int CssDisplayIsNone(CssDisplay display);

/** Whether @p display generates a block-level box. */
int CssDisplayIsBlockLevel(CssDisplay display);

/** Whether @p display generates an inline-level box. */
int CssDisplayIsInlineLevel(CssDisplay display);

/**
 * Whether @p display is an atomic inline-level box — `inline-block`.
 *
 * Derived from the two columns rather than from the spelling: an atomic inline
 * is an inline outer with a formatting context of its own, which is the same
 * statement as CSS's own `inline-block` = `inline flow-root`. A future
 * `inline-flex` is inline-level and atomic in the same sense, and this predicate
 * will have to say *which* inner display it has rather than merely that it is not
 * flow — which is exactly the question @ref CssDisplayInner answers.
 */
int CssDisplayIsInlineBlock(CssDisplay display);

/** @p display's outer display, or OUTER_NONE for an unknown enumerator. */
CssDisplayOuter MakeCssDisplayOuter(CssDisplay display);

/** @p display's inner display, or INNER_NONE when there is no box or no such row. */
CssDisplayInner MakeCssDisplayInner(CssDisplay display);

/** Human-readable outer display name ("block", "inline"); never NULL. */
const char *CssDisplayOuterName(CssDisplayOuter outer);

/** Human-readable inner display name ("flow", "flow-root"); never NULL. */
const char *CssDisplayInnerName(CssDisplayInner inner);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_DISPLAY_H */
