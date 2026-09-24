/*
 * Selection mapping: the logical ↔ visual bridge (Phase 12.21).
 *
 * ## What this is, and what it deliberately is not
 *
 * A glyph run knows where every glyph came from. A painter knows where every glyph went. **Nothing knew
 * both at once**, which is why a click could not become a caret and a caret could not become a rectangle.
 * This header is that bridge and only that bridge:
 *
 *     logical text ──▶ RoboTextPosition ──▶ cluster ──▶ glyph boundary ──▶ point
 *     point ──▶ visual run ──▶ inline distance ──▶ cluster ──▶ RoboTextPosition
 *
 * It contains **no shaping**. Nothing here decodes UTF-8, selects a font, resolves a script or a level,
 * runs GSUB or GPOS, or measures: every number it answers with is read off a `RoboTextGlyphRun` that was
 * shaped before it was asked, plus the one thing a run cannot know — **where the caller put it**.
 *
 * ## The four index spaces, and the one this header adds
 *
 * `RoboTextScalarIndex` is the canonical text position, and a `RoboTextPosition` is one of those and nothing
 * else. A selection is therefore *never* a glyph index, a byte offset or a screen coordinate, and that is the
 * phase's central rule rather than a stylistic preference: a caller holding glyph indices has already lost the
 * ligature, the mark and the reordered Indic cluster, because those are exactly the places where glyph index
 * and source offset stopped agreeing.
 *
 * ## Where the geometry comes from
 *
 * A run's glyphs are laid out along its **inline** axis, and which physical axis that is depends on the run's
 * writing mode (12.20). So this module never writes `point.x - origin.x`: it projects through
 * `RoboTextWritingAxesInline`/`…Block` — the phase 12.20 door that exists precisely so that no call site
 * performs the x/y swap — and everything below (the caret's cross-axis extent, the hit test's inline distance,
 * a selection rectangle's two corners) is expressed in that (inline, block) pair and converted back at the end.
 * A vertical column and a horizontal line therefore take the same path, and `vertical-rl` differs from
 * `vertical-lr` in the block vector and in nothing else.
 *
 * ## Affinity, and why a source offset does not have one screen position
 *
 * A position at the boundary between two visual runs — a bidi boundary, a fallback font change, the end of a
 * wrapped line — has **two** legitimate caret geometries, and a source offset inside a multi-scalar cluster
 * (a ligature, an Indic conjunct, a base with its marks) has **none** that a caller may ask for. So:
 *
 *   - every position is reported as a *cluster* boundary, never as an offset inside one. A hit inside a
 *     ligature names the ligature's start or its end, chosen by which half was clicked — the snapping rule the
 *     two `RoboTextAffinity` values carry, and the reason `RoboTextSelectionHit::snapped` is reported rather
 *     than the snap being silent;
 *   - `RoboTextAffinityUpstream` hugs the text *before* the position and `Downstream` the text after it, which
 *     is what makes a caret at a bidi boundary land on the side the reader's click meant.
 *
 * ## What this build does not claim
 *
 * Editing, insertion and deletion, caret navigation policy (word, grapheme, line movement), IME, clipboard,
 * mouse and gesture handling, DOM `Range`, `user-select`, and selection *painting* are later rows of this
 * phase list. The geometry this header produces is what all of them consume; none of them is implemented here.
 * A `SelectionMap()` accessor on RoboLayout's line boxes and a selection overlay in the desktop's display
 * list are named in `Documentation/Runtime/Text/TextSelection.md` §7 rather than implied.
 *
 * Contract and limitations: `Documentation/Runtime/Text/TextSelection.md`.
 */
#ifndef ROBO_TEXT_SELECTION_H
#define ROBO_TEXT_SELECTION_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextGlyphs.h"
#include "RoboText/RoboTextRunList.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Position                                                            */
/* ------------------------------------------------------------------ */

/**
 * A position in the source text: one scalar offset, and nothing else.
 *
 * A struct rather than a bare `RoboTextScalarIndex` even though it holds exactly one, for the reason the
 * index types are types: a caret position and a length are the same integer and different ideas, and a
 * signature that takes a `RoboTextPosition` cannot be handed a glyph index by accident. It compares by value,
 * so nothing here allocates or owns.
 *
 * The **end of the text is a legitimate position** (`offset == scalarCount`), because `RoboTextRange` is
 * half-open and `RoboTextRangeAt(n)` is where a caret at the end of a line lives. `Start` and `End` are the
 * two positions every text has; `…IsValid` is the door a caller uses before trusting one it built itself.
 */
typedef struct RoboTextPosition {
    /** The scalar offset. Half-open: it may equal the text's scalar count. */
    RoboTextScalarIndex offset;
} RoboTextPosition;

/** The position at @p offset. */
RoboTextPosition RoboTextPositionMake(RoboTextScalarIndex offset);

/** Clear @p position to offset 0. Safe on NULL. */
void RoboTextPositionInit(RoboTextPosition *position);

/** Field equality. Safe on NULL (NULL == NULL). */
int RoboTextPositionEqual(RoboTextPosition a, RoboTextPosition b);

/** Non-zero when @p position lies inside a text @p scalars long, end included. */
int RoboTextPositionIsWithin(RoboTextPosition position, RoboTextScalarIndex scalars);

/* ------------------------------------------------------------------ */
/* Affinity                                                            */
/* ------------------------------------------------------------------ */

/**
 * Which side of a position the caller means, when the position has two visual locations.
 *
 * `Upstream` is the side the text **before** the position occupies — the end of the previous run, the earlier
 * line — and `Downstream` the side after it. A position in the middle of a line has one location and both
 * affinities answer it; a position at a run or line boundary answers two, and *that* is what this value is for.
 *
 * The two names are the ones the Unicode bidirectional algorithm and every editor call them, so a reader
 * arriving from either side finds the word they expect.
 */
typedef enum RoboTextAffinity {
    /** The visual location at the end of the text before the position. */
    RoboTextAffinityUpstream = 0,
    /** The visual location at the start of the text after the position. */
    RoboTextAffinityDownstream = 1,
    RoboTextAffinityCount = 2
} RoboTextAffinity;

/** Non-zero when @p affinity is a defined enumerator. */
int RoboTextAffinityIsKnown(RoboTextAffinity affinity);

/** Lower-case name (`"upstream"`, `"downstream"`); never NULL. */
const char *RoboTextAffinityName(RoboTextAffinity affinity);

/** The other affinity — `Upstream` for `Downstream` and the reverse. An unknown value is unchanged. */
RoboTextAffinity RoboTextAffinityOther(RoboTextAffinity affinity);

/* ------------------------------------------------------------------ */
/* Selection                                                           */
/* ------------------------------------------------------------------ */

/**
 * A selection: where it was anchored and where it currently ends.
 *
 * Two positions rather than one range, because they are two facts. `anchor` is where the selection *started*
 * — the caret before the first drag, the last character a shift-click began at — and `focus` is where it is
 * *now*; the visual direction a caller draws is the sign of their difference, and discarding it would make
 * shift-extension impossible to express. `RoboTextSelectionRange` is the normalized span, computed rather
 * than stored, exactly as `RoboTextSelectionDirectionOf` is: one stored form and one derived form cannot
 * disagree.
 *
 * Collapsing a selection — moving the caret with no shift — is `anchor == focus`, which is the empty range.
 */
typedef struct RoboTextSelection {
    /** Where the selection began. */
    RoboTextPosition anchor;
    /** Where it ends now, which may be before the anchor. */
    RoboTextPosition focus;
} RoboTextSelection;

/** Which way a selection was made, in **logical** order. */
typedef enum RoboTextSelectionDirection {
    /** Anchor and focus are the same position: a caret, not a span. */
    RoboTextSelectionDirectionNone = 0,
    /** Focus is after the anchor in the source. */
    RoboTextSelectionDirectionForward = 1,
    /** Focus is before the anchor in the source. */
    RoboTextSelectionDirectionBackward = 2,
    RoboTextSelectionDirectionCount = 3
} RoboTextSelectionDirection;

/** Non-zero when @p direction is a defined enumerator. */
int RoboTextSelectionDirectionIsKnown(RoboTextSelectionDirection direction);

/** Lower-case name (`"none"`, `"forward"`, `"backward"`); never NULL. */
const char *RoboTextSelectionDirectionName(RoboTextSelectionDirection direction);

/** A selection from @p anchor to @p focus, direction and all. */
RoboTextSelection RoboTextSelectionMake(RoboTextPosition anchor, RoboTextPosition focus);

/** The caret at @p position: a selection with both ends equal. */
RoboTextSelection RoboTextSelectionAt(RoboTextPosition position);

/** @p selection with the focus pulled back onto the anchor. */
RoboTextSelection RoboTextSelectionCollapse(RoboTextSelection selection);

/**
 * The normalized span: `start <= end`, whichever way the selection was made.
 *
 * The half-open scalar range every geometry door takes, so no caller has to remember to order two positions —
 * and the reason a backwards drag produces the same rectangles as a forwards one.
 */
RoboTextRange RoboTextSelectionRange(RoboTextSelection selection);

/** Which way @p selection was made. See @ref RoboTextSelectionDirection. */
RoboTextSelectionDirection RoboTextSelectionDirectionOf(RoboTextSelection selection);

/** Non-zero when the selection covers nothing (`anchor == focus`). */
int RoboTextSelectionIsEmpty(RoboTextSelection selection);

/**
 * The selection clamped to a text @p scalars long.
 *
 * A drag that began in one document and ended in another, a selection held across an edit, a caller that built
 * a position from arithmetic — all of them hand over a position the text no longer has, and refusing would be
 * less useful than clamping. An inverted pair stays inverted: clamping is not reordering, and
 * `RoboTextSelectionRange` is the door that orders.
 */
RoboTextSelection RoboTextSelectionClamp(RoboTextSelection selection, RoboTextScalarIndex scalars);

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

/** A point in the **caller's own space**: the space the placements' origins are given in. */
typedef struct RoboTextSelectionPoint {
    float x;
    float y;
} RoboTextSelectionPoint;

/** @p x, @p y as a point. */
RoboTextSelectionPoint RoboTextSelectionPointMake(float x, float y);

/**
 * A rectangle in the caller's space, with `y` increasing **downward** — the convention a painter's device
 * space and `GfxRectF` use.
 *
 * Deliberately not `RoboTextMeasureRect`, which is the same four numbers with `y` increasing *up* from a
 * baseline. Two spaces, two types: a selection rectangle is a place on a page and an ink box is a place
 * relative to a pen, and one struct serving both would be converted wrongly exactly once, in the one place
 * nobody looked.
 *
 * A rect with a non-positive extent covers nothing, and a selection whose whole span has no inline extent (a
 * caret-width selection, a line of nothing but zero-advance clusters) is given the option's caret thickness
 * rather than a zero-width rectangle: a selection a reader cannot see is the one failure this layer must not
 * produce.
 */
typedef struct RoboTextSelectionRect {
    /** Left edge. */
    float x;
    /** **Top** edge. */
    float y;
    /** Extent on x; never negative in a rect this module produced. */
    float width;
    /** Extent on y; never negative in a rect this module produced. */
    float height;
} RoboTextSelectionRect;

/** Non-zero when @p rect covers no area. Safe on NULL. */
int RoboTextSelectionRectIsEmpty(const RoboTextSelectionRect *rect);

/** Width of @p rect, or 0 for a NULL or empty one. Never negative. */
float RoboTextSelectionRectWidth(const RoboTextSelectionRect *rect);

/** See @ref RoboTextSelectionRectWidth: the height. */
float RoboTextSelectionRectHeight(const RoboTextSelectionRect *rect);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextSelectionRectEqual(const RoboTextSelectionRect *a, const RoboTextSelectionRect *b);

/**
 * A caret: a segment across the line, plus the writing mode that says which way across.
 *
 * `start` is the end of the band the placement calls its `blockStart` and `end` the other one, so a caller
 * that draws from `start` to `end` needs no axis knowledge at all — and one that *does* care asks
 * `writingMode`, which is the run's own answer (never a script's: a Latin letter inside a Japanese column is
 * sideways and the caret beside it is horizontal, while the ideograph's is vertical, because the mode is data
 * — Phase 12.20).
 *
 * `valid` is 0 for a position this map cannot place — an empty map, a position outside the text — with every
 * number left at zero, so a caller has one thing to check rather than five.
 */
typedef struct RoboTextCaretGeometry {
    /** The end of the caret nearer the inline origin. */
    RoboTextSelectionPoint start;
    /** The far end. */
    RoboTextSelectionPoint end;
    /** The caret's own thickness, from the options. */
    float thickness;
    /** The line's writing mode, which is what makes the caret vertical or horizontal. */
    RoboTextWritingMode writingMode;
    /** Non-zero when the geometry is a place this map could compute. */
    int valid;
} RoboTextCaretGeometry;

/** Clear @p caret: the origin, zero thickness, horizontal, not valid. Safe on NULL. */
void RoboTextCaretGeometryInit(RoboTextCaretGeometry *caret);

/** Non-zero when @p caret is a place this module computed. Safe on NULL. */
int RoboTextCaretGeometryIsValid(const RoboTextCaretGeometry *caret);

/** Width of @p caret's segment along x. Never negative. */
float RoboTextCaretGeometrySpanX(const RoboTextCaretGeometry *caret);

/** See @ref RoboTextCaretGeometrySpanX: the span along y. */
float RoboTextCaretGeometrySpanY(const RoboTextCaretGeometry *caret);

/** Non-zero when @p caret is horizontal (its span is along x), as a horizontal line's caret is. */
int RoboTextCaretGeometryIsHorizontal(const RoboTextCaretGeometry *caret);

/**
 * How many rectangles one selection may produce.
 *
 * A paragraph's selection produces one rectangle per (line, run) segment it covers, and a mixed bidi line
 * needs one per level run inside it. Four lines of `Hello שלום world مرحبا` is six or seven; a screenful of a
 * bidirectional document is more. The cap is **stated and reported** rather than silently applied:
 * @ref RoboTextSelectionGeometry::truncated says when a caller is looking at a partial answer, which is the
 * same contract the module's dumps follow.
 */
#define RoboTextMaxSelectionRects 64u

/**
 * A selection as the rectangles a painter draws.
 *
 * Rectangles rather than one box, and that is the whole point of the type: a logical range that crosses a
 * bidi boundary is **several** places on the page, and a single bounding box around them would cover text the
 * selection does not contain. `bounds` is offered as well because a caller that wants a rough box (to scroll
 * something into view) can have one, but it is explicitly a convenience and never a substitute.
 *
 * `isEmpty` is 1 for an empty selection *and* for one whose range covers no placement at all; `count` is what
 * a painter walks.
 */
typedef struct RoboTextSelectionGeometry {
    /** The rectangles, in visual order: by line, then by the order the line's runs are laid out in. */
    RoboTextSelectionRect rects[RoboTextMaxSelectionRects];
    /** Rectangles held. Never greater than @ref RoboTextMaxSelectionRects. */
    size_t count;
    /** Non-zero when the selection needed more rectangles than the cap allows. */
    int truncated;
    /** The union of @ref rects: a convenience box, never the answer to "what is selected". */
    RoboTextSelectionRect bounds;
    /** Non-zero when nothing is selected, or nothing the map covers is. */
    int isEmpty;
} RoboTextSelectionGeometry;

/** Clear @p geometry to "nothing selected". Safe on NULL. */
void RoboTextSelectionGeometryInit(RoboTextSelectionGeometry *geometry);

/** Non-zero when @p geometry holds no rectangle. Safe on NULL. */
int RoboTextSelectionGeometryIsEmpty(const RoboTextSelectionGeometry *geometry);

/** Rectangle @p index, or NULL when out of range. */
const RoboTextSelectionRect *RoboTextSelectionGeometryAt(const RoboTextSelectionGeometry *geometry,
                                                         size_t index);

/**
 * A hit test's answer: a **logical** position and the visual facts that produced it.
 *
 * The position is the phase's rule made concrete — a click returns a place in the text, never a glyph index —
 * and `clusterStart` is the cluster it was resolved against, which is what a caller debugging a ligature
 * wants to see. `distance` is signed along the inline axis: negative before the boundary the position names,
 * positive after it, so a caller can tell "the point was past the end of this line" from "before its start".
 */
typedef struct RoboTextSelectionHit {
    /** The logical position the point names. */
    RoboTextPosition position;
    /** Which side of a two-location boundary the caller meant. */
    RoboTextAffinity affinity;
    /** The run the point landed in, as a **logical** index into the list. */
    size_t runIndex;
    /** The placement's line index, echoed. */
    size_t lineIndex;
    /** The cluster's source start, which is not always @ref position: a snapped hit names the cluster. */
    RoboTextScalarIndex clusterStart;
    /** Non-zero when the point landed inside a multi-scalar cluster and was snapped to a boundary. */
    int snapped;
    /** Signed inline distance from the returned boundary, in device pixels. */
    float distance;
} RoboTextSelectionHit;

/** Clear @p hit: position 0, downstream, no run, no distance. Safe on NULL. */
void RoboTextSelectionHitInit(RoboTextSelectionHit *hit);

/* ------------------------------------------------------------------ */
/* Placement: where the caller put a run                               */
/* ------------------------------------------------------------------ */

/**
 * One shaped run, placed on the page.
 *
 * The one thing this module cannot read off a glyph run, and therefore the one thing a caller must supply.
 * `origin` is the run's **pen origin** — the baseline point its first glyph advances from, in the caller's own
 * space, `+x` right and `+y` down, which is the space `GfxGlyphDraw::origin` is in and the reason a paint pass
 * can fill these in as it walks its own list.
 *
 * `blockStart`/`blockEnd` are the line's cross-axis band **as signed distances along the block axis from the
 * origin**: for a horizontal line, the negative ascent above the baseline and the positive descent below it;
 * for a vertical line, whatever the block axis makes them. `RoboTextSelectionPlacementForRun` computes both
 * from the run's own metrics, so the ordinary case is one call and a caller that wants a different band (a
 * line box's leading, a padded row) writes its own two numbers.
 *
 * `lineIndex` is the caller's: this module never groups placements into lines by itself, because what a line
 * *is* belongs to the layer that broke it. Two placements with one `lineIndex` are one line, and a line's
 * placements are ordered the way the caller laid them out — which for a bidi line is not their logical order,
 * and is exactly why the two are separate questions here.
 */
typedef struct RoboTextSelectionPlacement {
    /** The run's logical index in the list this map was built over. */
    size_t runIndex;
    /** The caller's line index. Equal indices are one line. */
    size_t lineIndex;
    /** The run's pen origin: the baseline point its first glyph advances from. */
    RoboTextSelectionPoint origin;
    /** The line band's near end, as a signed distance along the block axis from @ref origin. */
    float blockStart;
    /** The band's far end. May be less than @ref blockStart for a mode whose block axis points backwards. */
    float blockEnd;
    /**
     * The writing mode the run is **drawn** in.
     *
     * Read by `RoboTextSelectionPlacementProject`, which needs it to know which axis is inline, and overwritten
     * from the run by `RoboTextSelectionMapCreate`: a map projects onto the axis the glyphs were *shaped* along,
     * and a caller that set this to something else would be projecting onto an axis the run never moved on.
     */
    RoboTextWritingMode writingMode;
    /** How the caller draws the run. See @ref RoboTextSelectionPlacementReversed. */
    uint32_t flags;
} RoboTextSelectionPlacement;

/** The default: the run's clusters run along its inline axis in glyph-array order. */
#define RoboTextSelectionPlacementForward 0u
/**
 * The caller draws the run **backwards** along its inline axis: the glyph at the smallest offset is the last
 * one in the array.
 *
 * Default **off**, and that default is this build's own painter (`RoboPaintDecoration.c` walks every run's
 * span in array order, RTL included, and counts what it draws that way in `PaintDecorationStats::rtl`). The
 * flag is what keeps the mapping honest either way: an RTL run's caret geometry is a bit on the placement
 * rather than an assumption inside this module, so the day the painter reverses that order — 12.8's visual
 * order reaching the glyphs — one line of a caller changes and every query here is already correct.
 */
#define RoboTextSelectionPlacementReversed (1u << 0)
/** A placement as a caller first writes one: forwards, no flags. */
#define RoboTextSelectionPlacementDefault RoboTextSelectionPlacementForward

/** Clear @p placement: no run, line 0, origin 0, an empty band. Safe on NULL. */
void RoboTextSelectionPlacementInit(RoboTextSelectionPlacement *placement);

/**
 * Fill @p out from @p run's own metrics: `runIndex`, `lineIndex`, the origin, and the band from the run's
 * ascent and descent.
 *
 * The band is `-ascent` to `+descent` along the **block** axis, taken through
 * `RoboTextWritingAxesBlock`, so a vertical run's band is its line's cross extent and a horizontal run's is
 * the familiar above/below-baseline pair. A run whose metrics cannot be read (a face that will not answer)
 * gets a zero-height band rather than a refusal: a caret with no length is visibly wrong, which is better
 * than a selection that cannot be computed at all.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument or a @p run that is not a valid run.
 */
Result RoboTextSelectionPlacementForRun(const RoboTextGlyphRun *run,
                                        size_t runIndex,
                                        size_t lineIndex,
                                        float originX,
                                        float originY,
                                        RoboTextSelectionPlacement *out,
                                        RoboTextDiag *diag);

/**
 * Project a point onto the placement's own axes.
 *
 * `out.x` is the distance along the run's **inline** axis from the pen origin — the number every hit test and
 * caret in this module is expressed in — and `out.y` the distance along its **block** axis, which selects the
 * line. Both come from `RoboTextWritingAxesInline`/`…Block`, so this is the one place an x/y pair becomes an
 * (inline, block) pair in this header and no caller performs the swap.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument.
 */
Result RoboTextSelectionPlacementProject(const RoboTextSelectionPlacement *placement,
                                        RoboTextSelectionPoint point,
                                        RoboTextSelectionPoint *out,
                                        RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* Options                                                            */
/* ------------------------------------------------------------------ */

/** How a map resolves the questions its inputs leave open. */
typedef struct RoboTextSelectionOptions {
    /** The caret's thickness in the caller's units. The width a zero-extent selection is given. */
    float caretThickness;
    /**
     * Whether a point past the end of a line may name the line's trailing whitespace.
     *
     * 1 (the default) puts the caret after the trailing spaces, which is what a click to the right of a line's
     * text on a wrapped paragraph means; 0 stops it before them, which is what a single-line input with a
     * visible end marker wants. The rule is implemented on the **scalars**, through
     * `RoboTextUnicodeIsWhitespace`, and never on the advances: a space's width is the font's business.
     */
    int includeTrailingWhitespace;
    /**
     * Whether a point off every line's band resolves to the nearest line.
     *
     * 1 (the default) is what clicking in a paragraph's leading does. 0 makes such a point `RoboNotFound`,
     * which a caller wanting a strict hit region asks for.
     */
    int preferNearestLine;
    /** Reserved; pass 0. */
    uint32_t flags;
} RoboTextSelectionOptions;

/** Fill @p options with the defaults: a 1px caret, trailing whitespace included, nearest line preferred. */
void RoboTextSelectionOptionsInit(RoboTextSelectionOptions *options);

/* ------------------------------------------------------------------ */
/* The map                                                            */
/* ------------------------------------------------------------------ */

typedef struct RoboTextSelectionMap RoboTextSelectionMap;

/**
 * Build the map over @p list with the caller's @p placements.
 *
 * The list is **borrowed and must outlive the map**: its runs are the glyphs, the clusters and the source the
 * whole thing maps between, and a map over a destroyed list would be a map over freed memory. The placements
 * **are** copied, because they are the caller's own coordinates and a caller that moves a line should not have
 * to tell the map.
 *
 * The placements must cover the list exactly once each, **in the order they are laid out** — which for a bidi
 * line is not its logical order, and is the reason the array's order is preserved rather than sorted: the
 * rectangle order is visual order, and sorting by run index would throw it away. A run placed twice would give
 * one source position two visual locations with no way to choose, and a run not placed at all would be a run
 * the map silently pretends is not on the page. **A subset is refused**, and a caller with runs it does not
 * want to make selectable builds a list without them — which is the same rule the list's own constructor
 * follows for overlapping runs.
 *
 * @param placements may be NULL when @p placementCount is 0, which builds an empty map: every query answers
 *                   "nothing here" rather than failing.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument, a placement naming no run of the list,
 *         a run placed twice, placements out of order, or invalid runs; RoboOutOfMemory. On failure @p out is
 *         NULL and nothing is allocated.
 */
Result RoboTextSelectionMapCreate(const RoboTextGlyphRunList *list,
                                  const RoboTextSelectionPlacement *placements,
                                  size_t placementCount,
                                  const RoboTextSelectionOptions *options,
                                  RoboTextSelectionMap **out,
                                  RoboTextDiag *diag);

/** Release @p map. The list and the runs stay the caller's. Safe on NULL. */
void RoboTextSelectionMapDestroy(RoboTextSelectionMap *map);

/** Placements the map was built over. */
size_t RoboTextSelectionMapPlacementCount(const RoboTextSelectionMap *map);

/** Distinct line indices the map's placements name. */
size_t RoboTextSelectionMapLineCount(const RoboTextSelectionMap *map);

/** The scalars the map covers: the list's own source range. Zero for an empty map. */
RoboTextRange RoboTextSelectionMapSourceRange(const RoboTextSelectionMap *map);

/** The options the map was built with, by value. An initialised record for a NULL map. */
RoboTextSelectionOptions RoboTextSelectionMapOptions(const RoboTextSelectionMap *map);

/** Placement @p index in the order it was given, or NULL when out of range. */
const RoboTextSelectionPlacement *RoboTextSelectionMapPlacementAt(const RoboTextSelectionMap *map,
                                                                  size_t index);

/** The placement that carries logical run @p runIndex, or NULL when the map has none. */
const RoboTextSelectionPlacement *RoboTextSelectionMapPlacementForRun(const RoboTextSelectionMap *map,
                                                                     size_t runIndex);

/* ------------------------------------------------------------------ */
/* Queries                                                            */
/* ------------------------------------------------------------------ */

/**
 * Visual position → **logical** position (Phase 12.21.26).
 *
 * The algorithm, in the order it runs: the point is projected onto each placement's block axis to choose a
 * line, onto its inline axis to choose a run within it, and then to a cluster boundary within that run. The
 * answer is a `RoboTextPosition` — never a glyph index — with the cluster it resolved against and the affinity
 * a click in the middle of a ligature produced.
 *
 * The run's **direction** decides which way its inline axis is read: an `Rtl` run's glyph array is in logical
 * order and its pen starts at its right edge, so its first cluster is at the run's *largest* inline offset.
 * The test is the run's own classified direction and not the paragraph's, which is why a run of digits inside
 * an Arabic paragraph is not mirrored.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument; RoboNotFound when the map is empty or
 *         the point is off every line and the options forbid the nearest one.
 */
Result RoboTextSelectionMapHitTest(const RoboTextSelectionMap *map,
                                   RoboTextSelectionPoint point,
                                   RoboTextSelectionHit *out,
                                   RoboTextDiag *diag);

/**
 * **Logical** position → visual caret geometry (Phase 12.21.25).
 *
 * The inverse of @ref RoboTextSelectionMapHitTest, with the same cluster rule: a position inside a
 * multi-scalar cluster is snapped to its start when @p affinity is `Downstream` and to its end when it is
 * `Upstream`, so a caret can never be placed inside a ligature or a conjunct. At a boundary two runs share,
 * the affinity chooses which of them answers — `Upstream` the run that ends there, `Downstream` the run that
 * begins — and the two answers are legitimately different places.
 *
 * The caret is drawn across the **block** axis, so a horizontal line's is vertical and a vertical column's is
 * horizontal; `writingMode` is the answering run's own.
 *
 * @return RoboOk and @p out, with `valid` 0 when the map has no placement covering the position;
 *         RoboInvalidArgument for a NULL argument.
 */
Result RoboTextSelectionMapCaret(const RoboTextSelectionMap *map,
                                 RoboTextPosition position,
                                 RoboTextAffinity affinity,
                                 RoboTextCaretGeometry *out,
                                 RoboTextDiag *diag);

/**
 * A selection → the rectangles a painter draws (Phase 12.21.27/28).
 *
 * One rectangle per (line, run) segment the range covers, in visual order: by line, then by the order the
 * caller laid that line's placements out. A selection across a bidi boundary therefore produces one rectangle
 * per visual segment rather than one per logical run, and a selection across three lines produces at least
 * three — never one box around the lot.
 *
 * Within one run the covered clusters coalesce into **one** segment, which is what keeps zero-advance marks
 * from producing a rectangle each: a base and its mark are one place on the page, and a selection of only the
 * mark is a caret-width rectangle beside it rather than an invisible one.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument; RoboNotSupported when the answer needed
 *         more rectangles than @ref RoboTextMaxSelectionRects (in which case @p out holds the ones that fit and
 *         reports `truncated`).
 */
Result RoboTextSelectionMapGeometry(const RoboTextSelectionMap *map,
                                    RoboTextSelection selection,
                                    RoboTextSelectionGeometry *out,
                                    RoboTextDiag *diag);

/**
 * Why a map is not a valid one.
 *
 * A type of its own for the reason the run list has one: "is this run placed" and "is this map consistent" are
 * different questions asked of different things. The constructor refuses most of these, and they are checked
 * again here because what a constructor accepts and what a record *is* must not be two rules — the same
 * argument `RoboTextGlyphRunListProblem` makes.
 */
typedef enum RoboTextSelectionProblem {
    /** Every invariant holds. */
    RoboTextSelectionProblemNone = 0,
    /** A placement names no run of the list. */
    RoboTextSelectionProblemRun,
    /** A run of the list is not valid. */
    RoboTextSelectionProblemPlacement,
    /** Two placements name one run. */
    RoboTextSelectionProblemDuplicate,
    /** The placements are not in ascending run order. */
    RoboTextSelectionProblemOrder,
    /** A run is not placed at all. */
    RoboTextSelectionProblemUnplaced,
    /** A placement's band is empty and not a caret's width. */
    RoboTextSelectionProblemBand
} RoboTextSelectionProblem;

/** Non-zero when @p problem is a defined enumerator. */
int RoboTextSelectionProblemIsKnown(RoboTextSelectionProblem problem);

/** Name of @p problem (`"duplicate"`); `"none"` for the valid answer. Never NULL. */
const char *RoboTextSelectionProblemName(RoboTextSelectionProblem problem);

/** Check every map invariant. O(placements + glyphs). */
Result RoboTextSelectionMapValidate(const RoboTextSelectionMap *map,
                                    RoboTextSelectionProblem *outProblem,
                                    RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                        */
/* ------------------------------------------------------------------ */

/**
 * A position as `"12"`, or `"12 (start)"` / `"12 (end)"` at a text's own ends, into @p buffer.
 *
 * Complete or absent, like every dump in this module: a line that does not fit leaves the buffer empty and
 * reports the length a complete one needs either way.
 */
Result RoboTextPositionWrite(RoboTextPosition position,
                             RoboTextScalarIndex scalars,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength);

/** A selection as `"3..9 forward"`. See @ref RoboTextPositionWrite for the completeness rule. */
Result RoboTextSelectionWrite(RoboTextSelection selection,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength);

/** A caret as `"vertical (12, 4)-(12, 20) w=1 valid"`. See @ref RoboTextPositionWrite. */
Result RoboTextCaretGeometryWrite(const RoboTextCaretGeometry *caret,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength);

/** A hit as `"position 7 downstream (run 1, line 0, cluster 6, snapped, +2.50)"`. */
Result RoboTextSelectionHitWrite(const RoboTextSelectionHit *hit,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength);

/**
 * A map as a multi-line block: a header, then a line per placement with its run, its source range, its line,
 * its origin and its band, then each run's clusters as `source → glyphs`.
 *
 * The door Phase 12.21.34 asks for, and the reason the split between a placement and a run is visible in a
 * dump: a reader can see one line's three runs and which of them is the right-to-left one.
 */
Result RoboTextSelectionMapWrite(const RoboTextSelectionMap *map,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength);

/* ------------------------------------------------------------------ */
/* Fuzzing                                                            */
/* ------------------------------------------------------------------ */

/**
 * Throw @p data at the map's queries (Phase 12.21.42).
 *
 * Runs, invariant: no crash, no out-of-range access, no invalid position and no infinite loop, whatever the
 * bytes are. A **zero-length** call is valid and returns @ref RoboOk; a NULL pointer with a non-zero size is
 * @ref RoboInvalidArgument, exactly as `RoboTextFuzzGlyphs` reports it.
 *
 * @return RoboOk when every query was answered; RoboInvalidArgument for NULL with a non-zero @p size.
 */
Result RoboTextSelectionFuzz(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_SELECTION_H */
