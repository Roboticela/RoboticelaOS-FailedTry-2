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
 * in 9.11, and it is what makes the *next* display value an added row: 19.3 added
 * `flex` (an outer `block` with an inner `flex`) and `inline-flex` (an outer
 * `inline` with the same inner) as two rows and no function changed; 19.4 added
 * `grid` and `inline-grid` as the same pair once more; 19.6 added the **table
 * family**, which is the first group of rows that needed a new **outer** kind as
 * well (`internal`, for a box the table's own pass places rather than one that
 * participates in block or inline flow); and `contents` will be a value that
 * generates no box and is therefore not `none`. This build supports the eighteen
 * values it implements and rejects every other identifier, including the ones this
 * table is deliberately shaped to accept later.
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
 * grammar, and the rows above are single-keyword values. A declaration
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
    RoboCssDisplayInlineBlock = 3,
    /** `flex`: block-level, contents laid out as flex items (Phase 19.3). */
    RoboCssDisplayFlex = 4,
    /** `inline-flex`: inline-level atomic, contents laid out as flex items (19.3). */
    RoboCssDisplayInlineFlex = 5,
    /** `grid`: block-level, contents placed into tracks (Phase 19.4). */
    RoboCssDisplayGrid = 6,
    /** `inline-grid`: inline-level atomic, contents placed into tracks (19.4). */
    RoboCssDisplayInlineGrid = 7,
    /** `table`: block-level, contents placed into rows and columns (Phase 19.6). */
    RoboCssDisplayTable = 8,
    /** `inline-table`: inline-level atomic, contents placed into rows and columns
     *  (19.6). The box participates in its parent's inline context as one atomic
     *  unit; its own contents are a table. */
    RoboCssDisplayInlineTable = 9,
    /** `table-row-group`: an **internal** table box holding rows (19.6). */
    RoboCssDisplayTableRowGroup = 10,
    /** `table-header-group`: an internal table box holding a table's header rows
     *  (19.6). It is a row group that a later phase may repeat across pages; this
     *  phase places its rows in document order, like any other group. */
    RoboCssDisplayTableHeaderGroup = 11,
    /** `table-footer-group`: an internal table box holding a table's footer rows
     *  (19.6). */
    RoboCssDisplayTableFooterGroup = 12,
    /** `table-row`: an internal table box whose children are cells (19.6). */
    RoboCssDisplayTableRow = 13,
    /** `table-cell`: an internal table box that holds one grid slot's contents and
     *  establishes a formatting context of its own (19.6). */
    RoboCssDisplayTableCell = 14,
    /** `table-column`: an internal table box that contributes a column's declared
     *  width and generates no geometry of its own (19.6). */
    RoboCssDisplayTableColumn = 15,
    /** `table-column-group`: an internal table box holding columns (19.6). */
    RoboCssDisplayTableColumnGroup = 16,
    /** `table-caption`: an internal table box placed on the table's block-start or
     *  block-end edge, and a formatting context of its own (19.6). */
    RoboCssDisplayTableCaption = 17
} CssDisplay;

/** Number of defined display values. */
#define RoboCssDisplayCount 18u

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
    RoboCssDisplayOuterInline = 2,
    /**
     * An **internal** table box (19.6): a box that generates a box and holds
     * contents, but that neither block nor inline flow places -- the **table**'s own
     * context does, and the box is a row, a row group, a cell, a column, a column
     * group or a caption.
     *
     * It is a third kind rather than a modifier on the other two because the
     * question a pass asks first is "which flow places this box", and an internal
     * table box's answer is neither of them. A build that called a cell
     * block-level would place a table's cells down the page as a column of blocks,
     * which is the whole of what a table is not.
     */
    RoboCssDisplayOuterInternal = 3
} CssDisplayOuter;

/** Number of defined outer displays. */
#define RoboCssDisplayOuterCount 4u

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
    RoboCssDisplayInnerFlowRoot = 2,
    /** A new **flex** formatting context of its own (Phase 19.3): the box is a flex
     *  container and its in-flow children are flex items. */
    RoboCssDisplayInnerFlex = 3,
    /** A new **grid** formatting context of its own (Phase 19.4): the box is a grid
     *  container and its in-flow children are grid items. */
    RoboCssDisplayInnerGrid = 4,
    /** A new **table** formatting context of its own (Phase 19.6): the box is a
     *  table container and it places its contents into rows and columns. */
    RoboCssDisplayInnerTable = 5
} CssDisplayInner;

/** Number of defined inner displays. */
#define RoboCssDisplayInnerCount 6u

/** Non-zero when @p display is one of the eighteen. NONE is defined. */
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

/** Capacity @ref CssDisplayWrite needs for any value in the vocabulary.
 *
 * Nineteen + the NUL, which is `table-column-group` -- the longest keyword in the set as
 * of 19.6 and the reason the bound is not 16 any more. A value that cannot be written
 * into this buffer is a value a caller cannot print, so the constant is checked against
 * every row by the suite rather than left to a comment. */
#define RoboCssDisplayTextMax 24u

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

/**
 * Whether @p display is an **atomic inline**: inline-level, and with an inner that is
 * not plain flow (19.6).
 *
 * The generalisation `CssDisplayIsInlineBlock` always pointed at: `inline-block`,
 * `inline-flex`, `inline-grid` and `inline-table` are the same *shape* of box — an
 * inline-level box that is one indivisible unit on its line because it lays its own
 * contents out — and the five pairwise predicates the module used to answer that
 * question with were five places for one answer to drift.
 *
 * It is deliberately **not** folded into `CssDisplayIsInlineBlock`: that name means
 * `inline-block` (CSS's own historical spelling for the plain-flow case), and a caller
 * that wants *that* value should be able to ask for it.
 */
int CssDisplayIsAtomicInline(CssDisplay display);

/**
 * Whether @p display establishes a **table** formatting context (19.6): `table` and
 * `inline-table`.
 *
 * The four display values before this one each established a context of their own, and
 * this is the pair that does it for a table: the box places its in-flow children into
 * rows and columns rather than stacking them or placing them on a line, and its
 * *internal* descendants are the rows, cells, columns and caption that make that up.
 */
int CssDisplayIsTableRoot(CssDisplay display);

/**
 * Whether @p display is one of the eight **internal** table boxes (19.6): a row group of
 * either kind, a row, a cell, a column, a column group or a caption.
 *
 * The question a pass asks before anything else about such a box, and the reason the
 * outer column needed a third kind: an internal table box is placed by the *table* it
 * belongs to, so neither block nor inline flow may claim it.
 */
int CssDisplayIsTableInternal(CssDisplay display);

/**
 * Whether @p display is a row or a row group (19.6).
 *
 * The two values that make up a table's block axis, and the pair a caption is *not*: a
 * row group is a box that holds rows and a row is a box that holds cells, so a pass that
 * walks a table's block axis walks exactly these.
 */
int CssDisplayIsTableRowLike(CssDisplay display);

/**
 * Whether @p display is a column or a column group (19.6).
 *
 * A column contributes a **declared width** and generates no geometry: CSS has no
 * column box on the page, which is why this predicate exists apart from
 * `CssDisplayIsTableInternal` — a pass reads a column's declarations and places
 * nothing.
 */
int CssDisplayIsTableColumnLike(CssDisplay display);

/** Whether @p display is exactly `table-cell` (19.6). */
int CssDisplayIsTableCell(CssDisplay display);

/** Whether @p display is exactly `table-caption` (19.6). */
int CssDisplayIsTableCaption(CssDisplay display);

/**
 * Whether @p display is an internal table box that establishes a formatting context of
 * its own (19.6): a **cell** or a **caption**.
 *
 * CSS 2.1 §9.4.1 lists table cells and captions among the boxes that establish a new
 * block formatting context, and the distinction is load-bearing twice over: a cell's
 * children are laid out *inside* the cell at the width the columns gave it, and a
 * caption's are laid out inside the caption. Every other internal table box — a row, a
 * row group, a column — establishes nothing, because the table's own pass places what
 * is inside it.
 */
int CssDisplayIsTableBoundary(CssDisplay display);

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
