/*
 * RoboCSS grid track templates and line placements (Phase 19.4).
 *
 * Phase 18.4 typed the two grid properties whose values are small closed sets --
 * `grid-auto-flow` and the gutters -- and *carried* everything else in the family,
 * because the rest takes a grammar this build had no reader for. This is that
 * reader: the half of the grid family whose value is a **track list**, and the line
 * placements that name a track boundary.
 *
 *   grid-template-columns ┐  a track list: sizes, minmax(), repeat(), fr
 *   grid-template-rows    ┘
 *   grid-auto-columns     ┐  the same grammar, with `auto` as the initial value
 *   grid-auto-rows        ┘
 *   grid-row-start/end    ┐  auto | <integer> | span <integer>
 *   grid-column-start/end ┘
 *
 * Vocabulary here, geometry there
 *
 * A track list is *style*: `grid-template-columns: 1fr 2fr` says what rule the
 * container follows, and this module turns the declaration into a bounded **value
 * record** -- a track's minimum and maximum breadth, and a line's kind. Where the
 * tracks land, how wide a `1fr` becomes and where an item is placed belongs to
 * RoboLayout (19.4's `RoboLayoutGrid.c`), which is why nothing below takes or
 * returns a rectangle and nothing below knows what a box is. The same boundary
 * `flex-basis` and `flex-wrap` are drawn at.
 *
 * The records are **by value and pointer-free**
 *
 * A `CssGridTrackList` is a count and a fixed array of track records. That is the
 * same discipline 12.17's `LayoutFontRequest` follows and for the same reason: a
 * consumer copies the record out of a computed style and uses it after the
 * declaration that produced it is irrelevant, so a pointer into a `CssValue`'s
 * child array would be a lifetime rule every call site had to keep. A value this
 * module cannot represent in the bound is *refused* rather than truncated, so a
 * record is never a prefix of what the author wrote.
 *
 * `fr` is a unit CSS has and this build's unit table does not
 *
 * `1fr` is a `<flex>`, and the unit vocabulary (RoboCSSUnit.h) is a closed set
 * covering lengths; `fr` is not one of them and is not a length. A reader that
 * pushed `1fr` through `CssNumericFromValue` would get UNKNOWN_UNIT and throw away
 * the one number a grid needs. So a dimension whose unit spells `fr` is classified
 * here, before the numeric door is asked, and the fraction is kept as a `double`.
 *
 * What this module is not
 *
 * Not a layout (above). Not a track *resolver*: a percentage is carried as the
 * percentage it is and a `fit-content()` limit as the length it is, because
 * resolving either needs the container's size and this module has no container.
 * Not `auto-fill`/`auto-fit`: those expand to a *count* that depends on the
 * container's used size, so they are `DEFERRED` here and named as absent where a
 * placement would need them. Not line **names**: `[full-start]` is carried in the
 * declaration and is not read -- a placement is a number, a `span`, or `auto`, and
 * a named line is a row of its own rather than a guess.
 */

#ifndef ROBO_CSS_GRID_TEMPLATE_H
#define ROBO_CSS_GRID_TEMPLATE_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSUnit.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* A track's breadth                                                   */
/* ------------------------------------------------------------------ */

/**
 * What a track's breadth is.
 *
 * Seven answers, and the first is the initial value because a cleared record has
 * to be the state an author who declared nothing finds their container in:
 *
 *   AUTO          the track takes what its contents ask for and, under a
 *                 `stretch` content distribution, may grow into free space;
 *   LENGTH        a `<length>` -- `40px`, `2em` -- with its unit kept;
 *   PERCENTAGE    a `<percentage>` of the container's content box on that axis;
 *   MIN_CONTENT   the width of the widest unbreakable piece (19.1);
 *   MAX_CONTENT   the whole content unwrapped (19.1);
 *   FRACTION      `<flex>`, a share of the free space;
 *   FIT_CONTENT   `fit-content(<length-percentage>)`, a clamp between
 *                 min-content and the limit.
 */
typedef enum CssGridTrackBreadthKind {
    /** `auto`: the initial value, and what an unreadable declaration falls back to. */
    RoboCssGridBreadthAuto = 0,
    /** A `<length>`, in @ref CssGridTrackBreadth::numeric. */
    RoboCssGridBreadthLength = 1,
    /** A `<percentage>`, in @ref CssGridTrackBreadth::numeric. */
    RoboCssGridBreadthPercentage = 2,
    /** `min-content` (19.1's measurement). */
    RoboCssGridBreadthMinContent = 3,
    /** `max-content`. */
    RoboCssGridBreadthMaxContent = 4,
    /** A `<flex>` -- `1fr` -- in @ref CssGridTrackBreadth::fraction. */
    RoboCssGridBreadthFraction = 5,
    /** `fit-content(<length-percentage>)`; the limit is in @ref
     *  CssGridTrackBreadth::numeric. */
    RoboCssGridBreadthFitContent = 6
} CssGridTrackBreadthKind;

/** Number of defined breadth kinds. AUTO is one of them. */
#define RoboCssGridTrackBreadthKindCount 7u

/** Non-zero when @p kind is a defined breadth kind (AUTO is defined). */
int CssGridTrackBreadthKindIsKnown(CssGridTrackBreadthKind kind);

/** Human-readable breadth name ("auto", "1fr"); never NULL, "" for unknown. */
const char *CssGridTrackBreadthKindName(CssGridTrackBreadthKind kind);

/**
 * One side of a track's size.
 *
 * `numeric` is live for LENGTH, PERCENTAGE and FIT_CONTENT and holds the parsed
 * value with its unit; `fraction` is live for FRACTION. Both are meaningless for
 * the four keyword kinds, and a cleared breadth is AUTO with neither -- which is
 * the property's initial value and therefore the safest thing a cleared record
 * could hold, exactly as `display`'s cleared record is `none`.
 */
typedef struct CssGridTrackBreadth {
    CssGridTrackBreadthKind kind;
    /** LENGTH/PERCENTAGE/FIT_CONTENT: the parsed value (number + unit). */
    CssNumeric numeric;
    /** FRACTION: the `<flex>` factor. Always finite and above zero. */
    double fraction;
} CssGridTrackBreadth;

/** Clear @p breadth to `auto`. Safe on NULL. */
void CssGridTrackBreadthInit(CssGridTrackBreadth *breadth);

/** Field-by-field equality, including the kind. Safe on NULL. */
int CssGridTrackBreadthEqual(const CssGridTrackBreadth *a,
                             const CssGridTrackBreadth *b);

/**
 * Non-zero when @p breadth determines a size without measuring anything.
 *
 * True for LENGTH and PERCENTAGE -- a percentage needs the container's size and
 * needs no *content* -- and false for `auto`, the two content keywords and
 * `fit-content()`, which is a clamp *around* a content measurement. FRACTION is
 * false: a share of free space is not a size until there is free space, which is
 * the whole reason the track-sizing step has a step for it.
 */
int CssGridTrackBreadthIsDefinite(const CssGridTrackBreadth *breadth);

/** Non-zero when @p breadth asks for the track to grow into free space. */
int CssGridTrackBreadthIsFlexible(const CssGridTrackBreadth *breadth);

/* ------------------------------------------------------------------ */
/* A track, and a track list                                           */
/* ------------------------------------------------------------------ */

/**
 * One track as a minimum and a maximum, which is what `minmax()` means.
 *
 * A plain `<track-breadth>` writes both halves with the same kind -- `40px` is
 * `minmax(40px, 40px)` in CSS's own reading of it -- so the two-field record is
 * not an over-generalisation of `minmax()`: it is the shape CSS's grammar has, and
 * `minmax(40px, 1fr)` is the one spelling that makes the two differ.
 */
typedef struct CssGridTrackSize {
    /** The floor: what the track is at least. */
    CssGridTrackBreadth minimum;
    /** The ceiling: how far the track may grow. */
    CssGridTrackBreadth maximum;
} CssGridTrackSize;

/** Clear @p size to `auto`/`auto`. Safe on NULL. */
void CssGridTrackSizeInit(CssGridTrackSize *size);

/** Field-by-field equality of both halves. Safe on NULL. */
int CssGridTrackSizeEqual(const CssGridTrackSize *a,
                          const CssGridTrackSize *b);

/**
 * Tracks one list may hold.
 *
 * A fixed cap rather than a growing arena, and it is a *refusal* rather than a
 * truncation: a declaration that expands past it answers INVALID, so a caller is
 * never handed a prefix of the tracks the author wrote and left to lay the rest
 * out as though nothing were missing. Sized for the widest grid an authored page
 * is likely to state (a `repeat(12, 1fr)` plus its neighbours, with room to
 * spare); an implicit track created by placement is a *different* budget and
 * RoboLayout states its own.
 */
#define RoboCssGridMaxTracks 32u

/** One track list: `none`, or a bounded sequence of tracks. */
typedef struct CssGridTrackList {
    /** Non-zero when the declaration was `none` (the templates' initial value). */
    int none;
    /** Tracks in @ref tracks; 0 when @ref none, or when nothing was declared. */
    size_t count;
    CssGridTrackSize tracks[RoboCssGridMaxTracks];
} CssGridTrackList;

/** Clear @p list to `none` with no tracks. Safe on NULL. */
void CssGridTrackListInit(CssGridTrackList *list);

/** Field-by-field equality: the flag, the count and every track. Safe on NULL. */
int CssGridTrackListEqual(const CssGridTrackList *a,
                          const CssGridTrackList *b);

/** Non-zero when @p list names no track at all (`none`, or nothing declared). */
int CssGridTrackListIsNone(const CssGridTrackList *list);

/* ------------------------------------------------------------------ */
/* Reading a track list from a component value                         */
/* ------------------------------------------------------------------ */

/**
 * The outcome of reading a component value as a track list.
 *
 * Four answers, and the split is the information a developer needs. Tracks is a
 * list this build can lay out; None is the keyword `none` where the property
 * allows it; Deferred is legal CSS whose value is decided elsewhere -- a `var()`
 * reference, or an `auto-fill`/`auto-fit` repeat whose *count* is a function of
 * the container's used size and therefore of a stage this reader is not; Invalid
 * is everything else, including a grammar this build does not read at all
 * (`subgrid`, brackets around a line name).
 */
typedef enum CssGridTrackListParse {
    /** Not a track list. @p out is untouched. */
    RoboCssGridTrackListParseInvalid = 0,
    /** A list; @p out holds it. */
    RoboCssGridTrackListParseTracks = 1,
    /** `none`; @p out holds a cleared list with NONE set. */
    RoboCssGridTrackListParseNone = 2,
    /** Legal CSS with no representation here; keep the tokens. */
    RoboCssGridTrackListParseDeferred = 3
} CssGridTrackListParse;

/** Number of defined outcomes. */
#define RoboCssGridTrackListParseCount 4u

/** Non-zero when @p outcome is a defined enumerator. */
int CssGridTrackListParseIsKnown(CssGridTrackListParse outcome);

/** Human-readable outcome name ("invalid", "tracks", "none", "deferred"). */
const char *CssGridTrackListParseName(CssGridTrackListParse outcome);

/**
 * Read @p specified as a track list.
 *
 * Accepted: `<track-size>+`, where a track size is a `<track-breadth>`, a
 * `<fixed-size>` (`minmax()`) or a `<track-repeat>` (`repeat(<integer>,
 * <track-size>+)`); and the keyword `none` when @p noneAllowed.
 *
 * `repeat()`'s count is an `<integer>` of at least one and is expanded **here**,
 * so a consumer reads a flat list and never re-derives the repeat; the expansion
 * is bounded by @ref RoboCssGridMaxTracks and a list that would exceed the bound
 * is INVALID rather than truncated. A nested `repeat()` is INVALID, which is
 * CSS's own rule rather than a limitation.
 *
 * Deferred: a lone `var()`-family reference anywhere in the list, and
 * `repeat(auto-fill | auto-fit, ...)`, whose count is the container's size
 * divided by the track size -- a layout question this module cannot answer.
 *
 * Refused: an empty value, a `<track-breadth>` that is not one (a bare number, a
 * string, a colour), a three-argument `minmax()`, a bracket (a line name), the
 * `subgrid` keyword, and a track count past the bound.
 *
 * @p out is disposed-first and is left `none`/empty on every outcome but TRACKS
 * and NONE.
 *
 * @param noneAllowed whether the `none` keyword is legal for this property --
 *                    true for the two templates, false for the two auto-track
 *                    lists, whose grammar has no `none`.
 */
CssGridTrackListParse CssGridTrackListFromValue(const CssValue *specified,
                                                int noneAllowed,
                                                CssGridTrackList *out);

/**
 * Write @p list's canonical text into @p buffer, NUL-terminated.
 *
 * `none` for the keyword, otherwise one space-separated entry per track: a
 * breadth's canonical spelling, `minmax(<min>, <max>)` when the halves differ,
 * `fit-content(<limit>)`, and `1fr` for a fraction. Deterministic, so a suite can
 * assert the reader's answer as text, and complete-or-absent: a list whose text
 * does not fit is refused with the length it needed rather than truncated.
 */
Result CssGridTrackListWrite(const CssGridTrackList *list,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag);

/** Capacity @ref CssGridTrackListWrite needs for a full list. */
#define RoboCssGridTrackListTextMax 512u

/* ------------------------------------------------------------------ */
/* A line placement                                                    */
/* ------------------------------------------------------------------ */

/**
 * What a `grid-row-start`, `grid-row-end`, `grid-column-start` or
 * `grid-column-end` declaration says.
 *
 * Three answers, and AUTO is 0 because it is the initial value: a placement an
 * author did not write is an auto-placement, and a cleared record has to read as
 * the state they find their container in.
 */
typedef enum CssGridLinePlacementKind {
    /** `auto`: the line is decided by the placement algorithm. */
    RoboCssGridLineAuto = 0,
    /** An `<integer>`: the line's number, positive or negative. */
    RoboCssGridLineNumber = 1,
    /** `span <integer>`: this many tracks from the other edge. */
    RoboCssGridLineSpan = 2
} CssGridLinePlacementKind;

/** Number of defined line-placement kinds. AUTO is one of them. */
#define RoboCssGridLinePlacementKindCount 3u

/** Non-zero when @p kind is a defined line-placement kind. */
int CssGridLinePlacementKindIsKnown(CssGridLinePlacementKind kind);

/** Human-readable kind name ("auto", "line", "span"); never NULL. */
const char *CssGridLinePlacementKindName(CssGridLinePlacementKind kind);

/**
 * The largest magnitude a line number or a span may have.
 *
 * A grid is a bounded thing -- RoboLayout states its own track ceiling -- and a
 * declaration of `grid-column-start: 4000000` is not a placement any track set
 * can honour. Bounded here rather than there so that a value nothing can place is
 * refused by the reader that read it, with a diagnostic, instead of silently
 * clamped three layers down.
 */
#define RoboCssGridLineLimit 10000

/** One line placement, by value. */
typedef struct CssGridLinePlacement {
    CssGridLinePlacementKind kind;
    /**
     * NUMBER: the line, 1-based from the start and negative from the end
     * (`-1` is the last explicit line). SPAN: the count, always at least 1.
     */
    int32_t line;
} CssGridLinePlacement;

/** Clear @p placement to `auto`. Safe on NULL. */
void CssGridLinePlacementInit(CssGridLinePlacement *placement);

/** Field-by-field equality. Safe on NULL. */
int CssGridLinePlacementEqual(const CssGridLinePlacement *a,
                              const CssGridLinePlacement *b);

/** Non-zero when @p placement is `auto` -- or NULL, which is the absence of one. */
int CssGridLinePlacementIsAuto(const CssGridLinePlacement *placement);

/** The outcome of reading a component value as a line placement. */
typedef enum CssGridLineParse {
    /** Not a line placement. @p out is untouched. */
    RoboCssGridLineParseInvalid = 0,
    /** A placement; @p out holds it. */
    RoboCssGridLineParsePlacement = 1,
    /** A `var()` reference or a named line: legal CSS, not read here. */
    RoboCssGridLineParseDeferred = 2
} CssGridLineParse;

/** Number of defined outcomes. */
#define RoboCssGridLineParseCount 3u

/** Non-zero when @p outcome is a defined enumerator. */
int CssGridLineParseIsKnown(CssGridLineParse outcome);

/** Human-readable outcome name ("invalid", "placement", "deferred"). */
const char *CssGridLineParseName(CssGridLineParse outcome);

/**
 * Read @p specified as a line placement.
 *
 * Accepted: `auto`; an `<integer>` whose magnitude is within
 * @ref RoboCssGridLineLimit; and `span <integer>` with the same bound and a count
 * of at least one (`span 0` is invalid CSS).
 *
 * Deferred: a lone `var()` reference, and a `<custom-ident>` other than `span` --
 * a **named line** is legal CSS whose semantics need the line-name table this
 * build does not keep (the header's own non-goals), so it is carried rather than
 * guessed at.
 *
 * Refused: an empty value, two components, a number that is not written as an
 * integer (`span 1.5`), `span` with no count or with a non-integer one, and a
 * magnitude past the limit.
 */
CssGridLineParse CssGridLinePlacementFromValue(const CssValue *specified,
                                               CssGridLinePlacement *out);

/**
 * Write @p placement's canonical text into @p buffer, NUL-terminated.
 *
 * `auto`, `<n>`, `-<n>` or `span <n>`, with the same complete-or-absent
 * discipline as the track-list writer.
 */
Result CssGridLinePlacementWrite(const CssGridLinePlacement *placement,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength,
                                 CssDiag *diag);

/** Capacity @ref CssGridLinePlacementWrite needs ("span 10000" plus a NUL). */
#define RoboCssGridLinePlacementTextMax 16u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_GRID_TEMPLATE_H */
