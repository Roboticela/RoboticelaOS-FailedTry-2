/*
 * RoboCSS grid vocabulary (Phase 18.4).
 *
 * Grid is the fourth property *family* in this build (after the box model, the
 * background and the border families), and the reason it is a module rather than a
 * handful of rows is the reason every family here is one: a consumer reads its
 * properties together — a grid container places its items into *tracks*, and the two
 * facts that decide where they go before any track list is computed are the automatic
 * flow and the gutters. This module owns the one property of the family whose value is
 * a small closed set; the gutters are a length family and live in the shared length
 * grammar beside `flex-basis`, and the remaining grid properties are carried
 * declarations for the reasons stated below.
 *
 *   CssGridAutoFlow   row | column | row dense | column dense
 *
 * The one code point worth reading twice is that `grid-auto-flow` is a value of *two
 * words* in three of its four spellings. That is not a detail of this module's design:
 * CSS writes it as `[ row | column ] || dense`, so `row dense` and `dense row` are the
 * same value and `dense` alone is `row dense`. A single-identifier vocabulary — the
 * shape `display`, `position` and `flex-wrap` all use — therefore cannot hold it, and
 * a table of one-word rows would silently accept `row` for `row dense` and lose the
 * density. So the reader below takes a *component value*, not a slice
 * (@ref CssGridAutoFlowFromValue), the way `background-position` reads its pair, and
 * the canonical spelling it hands back is the one CSS prints (`row dense`, never
 * `dense row`).
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h). Not a value parser for the track lists: `grid-template-
 * columns`, `grid-template-rows` and their relatives take a *track list* — a size, a
 * `minmax()`, a repeat count, a line name — whose grammar is a phase of its own, and
 * this build carries those declarations PRESERVED rather than pretending to read them.
 * Not a layout: nothing here measures a track, places an item, or computes a line
 * number, and no function below takes or returns a rectangle. A value says *what rule*
 * a layout pass follows; where the items land belongs to the phase that has a box.
 */

#ifndef ROBO_CSS_GRID_H
#define ROBO_CSS_GRID_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* grid-auto-flow                                                      */
/* ------------------------------------------------------------------ */

/**
 * A computed `grid-auto-flow`.
 *
 * A closed set of four spellings, and the enumerator values are append-only because a
 * stored enumerator is a contract. ROW is **0** because it is the property's initial
 * value, so a cleared record holds the state in which an author who declared nothing
 * finds their container.
 *
 * The four are the cross product of an *axis* (`row` or `column`) and whether the
 * packing is *dense*; the two are separate columns of the rows below, because a
 * consumer asking "does this fill holes?" and one asking "which axis do the auto-items
 * follow?" want two different answers about one value.
 */
typedef enum CssGridAutoFlow {
    /** `row`: auto-placed items fill each row in turn, in source order. */
    RoboCssGridAutoFlowRow = 0,
    /** `column`: auto-placed items fill each column in turn, in source order. */
    RoboCssGridAutoFlowColumn = 1,
    /** `row dense`: as `row`, but backfilling holes with later items. */
    RoboCssGridAutoFlowRowDense = 2,
    /** `column dense`: as `column`, packed densely. */
    RoboCssGridAutoFlowColumnDense = 3
} CssGridAutoFlow;

/** Number of defined grid-auto-flow values. */
#define RoboCssGridAutoFlowCount 4u

/** Non-zero when @p flow is one of the four. ROW is defined. */
int CssGridAutoFlowIsKnown(CssGridAutoFlow flow);

/** Human-readable name ("row", "column dense"); never NULL, "" for unknown. */
const char *CssGridAutoFlowName(CssGridAutoFlow flow);

/** Number of rows in the vocabulary. */
size_t CssGridAutoFlowCount(void);

/**
 * The flow a single canonical spelling names, or ROW when it names none of them.
 *
 * ASCII case-insensitive. Accepts the four canonical spellings *and* the one-word
 * `dense`, which CSS defines as `row dense`; `denserow` and `dense column` are not
 * spellings and answer ROW. ROW is both a value and the answer for "not one", which is
 * the ambiguity every closed vocabulary in this build has, and the door that resolves
 * it is @ref CssGridAutoFlowSliceIsKnown rather than a predicate on the result.
 */
CssGridAutoFlow CssGridAutoFlowFromSlice(CssSlice keyword);

/** NUL-terminated flavour of the above. */
CssGridAutoFlow CssGridAutoFlowFromText(const char *keyword);

/**
 * Whether @p keyword is a spelling this vocabulary knows, case-insensitively.
 *
 * The single-word spellings only (`row`, `column`, `dense`) — the two-word forms are
 * not one identifier and are read by @ref CssGridAutoFlowFromValue. This is the door
 * that tells "row" apart from "not one of the four", which `FromSlice` alone cannot.
 */
int CssGridAutoFlowSliceIsKnown(CssSlice keyword);

/**
 * The flow an axis and a density name, with no text involved.
 *
 * `CssGridAutoFlowFromParts(1, 0)` is `column` and `…(0, 1)` is `row dense`. It exists
 * so the reader below reports what it read as two facts rather than assembling a
 * string, and so a caller that has already classified an axis asks the vocabulary
 * instead of re-spelling it.
 */
CssGridAutoFlow CssGridAutoFlowFromParts(int column, int dense);

/**
 * Write @p flow's canonical keyword into @p buffer, NUL-terminated.
 *
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param outLength non-NULL; receives the length written, excluding the NUL.
 * @param diag       optional diagnostic sink.
 * @return RoboOk; RoboInvalidArgument for a NULL argument or an unknown
 *         enumerator; RoboNotSupported when the keyword does not fit, in which
 *         case @p outLength receives the length it would have needed.
 */
Result CssGridAutoFlowWrite(CssGridAutoFlow flow,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            CssDiag *diag);

/** Capacity @ref CssGridAutoFlowWrite needs ("column dense" plus a NUL). */
#define RoboCssGridAutoFlowTextMax 16u

/**
 * Whether @p flow's auto-placement runs along the **column** axis — `column` or
 * `column dense`.
 *
 * The one classification a layout pass dispatches on before anything else, and it is a
 * column of the row rather than a comparison against two spellings, so a value added
 * later answers for itself. An unknown value answers 0, which is the safe direction: a
 * value this build cannot read must not claim auto-placement follows columns.
 */
int CssGridAutoFlowIsColumn(CssGridAutoFlow flow);

/**
 * Whether @p flow packs **densely** — backfilling holes earlier items left.
 *
 * Not a hint: a dense container may place a later item *before* an earlier one, which
 * is a different order and therefore a different picture. It is asked of the row for
 * the same reason @ref CssGridAutoFlowIsColumn is, and an unknown value answers 0.
 */
int CssGridAutoFlowIsDense(CssGridAutoFlow flow);

/* ------------------------------------------------------------------ */
/* Reading `grid-auto-flow` from a component value                     */
/* ------------------------------------------------------------------ */

/**
 * The outcome of reading a component value as a `grid-auto-flow`.
 *
 * Three answers, and they are three because they tell a developer three different
 * things, exactly as the background module's pair reader does: a value this build can
 * read, legal CSS whose value is decided elsewhere (a `var()` reference), and a value
 * that is not a `grid-auto-flow` at all.
 */
typedef enum CssGridParse {
    /** Not a grid-auto-flow value. */
    RoboCssGridParseInvalid = 0,
    /** A flow: @p out holds the classified value. */
    RoboCssGridParseFlow = 1,
    /** Legal CSS with no representation here; keep the tokens. */
    RoboCssGridParseDeferred = 2
} CssGridParse;

/** Number of defined outcomes. */
#define RoboCssGridParseCount 3u

/** Non-zero when @p outcome is a defined enumerator. */
int CssGridParseIsKnown(CssGridParse outcome);

/** Human-readable outcome name ("invalid", "flow", "deferred"); never NULL. */
const char *CssGridParseName(CssGridParse outcome);

/**
 * Read @p specified as a `grid-auto-flow`.
 *
 * Accepted: `row`, `column`, `dense` (which is `row dense`), `row dense`, `dense row`,
 * `column dense` and `dense column`, in one or two identifier components with only
 * whitespace between them.
 *
 * Deferred: a single `var()`/`calc()`-family function, which is legal CSS whose value is
 * decided where the reference is.
 *
 * Refused: any other component count, a non-identifier component, a repeated keyword
 * (`row column`), or a keyword this vocabulary does not know.
 *
 * @p out is written only on @ref RoboCssGridParseFlow.
 */
CssGridParse CssGridAutoFlowFromValue(const CssValue *specified,
                                      CssGridAutoFlow *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_GRID_H */
