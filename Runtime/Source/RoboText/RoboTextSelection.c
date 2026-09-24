/*
 * Selection mapping (Phase 12.21) — see `RoboTextSelection.h` for the contract.
 *
 * The file is five pieces, in the order a query meets them:
 *
 *   **the vocabulary**    positions, affinities, selections, the caret and the hit record. Value types with an
 *                         equality and a name each, and nothing that allocates.
 *   **the placement**     the one thing a caller must supply — where a run was put — plus the projection that
 *                         turns a point into (inline, block) through 12.20's axes.
 *   **the map**           the borrowed list and the copied placements, with the invariants the constructor
 *                         refuses and the *reasons* it refuses them.
 *   **the queries**       hit test, caret, geometry. Every one of them walks clusters and asks the face for
 *                         nothing: the numbers are already on the run.
 *   **the dumps**         complete-or-absent, through the shared writer, like every other dump in this module.
 *
 * Two facts about a run drive everything below, and both are read rather than assumed:
 *
 *   - **which axis is inline.** A run's glyphs advance along its writing mode's inline axis, so
 *     `RoboTextWritingModeAxes` turns "the pen moved 4px" into a physical vector and nothing here writes
 *     `point.x - origin.x`.
 *   - **which way its clusters progress in the caller's space.** `RoboTextSelectionPlacementReversed` says the
 *     caller draws the run backwards; the default is forwards, because that is what this build's painter does
 *     for every run it is handed. The flag exists so that the question is *asked*: the day 12.8's visual order
 *     reaches the painter, an RTL run's mapping is a bit on the placement rather than a rewrite of this file.
 *
 * The one rule that is easy to get wrong and is therefore written down once, in `LogicalEdges`: for a
 * *forwards* run a cluster's visual start is its source start, and for a *backwards* run it is its source
 * **end**. Everything that turns an offset into a position or a position into an offset goes through it.
 */
#include "RoboText/RoboTextSelection.h"

#include "RoboMem/RoboMem.h"
#include "RoboText/RoboTextShape.h"
#include "RoboText/RoboTextUnicode.h"
#include "RoboText/RoboTextVertical.h"
#include "RoboTextInternal.h"

#include <float.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Small constants                                                     */
/* ------------------------------------------------------------------ */

/** The distance below which two inline coordinates are one place. A thousandth of a pixel. */
#define SelectionEpsilon 0.001f

/** The scratch a dump builds into before it is known to fit. */
#define SelectionDumpChars 8192u

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

RoboTextPosition RoboTextPositionMake(RoboTextScalarIndex offset)
{
    RoboTextPosition position;

    position.offset = offset;
    return position;
}

void RoboTextPositionInit(RoboTextPosition *position)
{
    if (position) {
        position->offset = 0u;
    }
}

int RoboTextPositionEqual(RoboTextPosition a, RoboTextPosition b)
{
    return a.offset == b.offset;
}

int RoboTextPositionIsWithin(RoboTextPosition position, RoboTextScalarIndex scalars)
{
    return position.offset <= scalars;
}

int RoboTextAffinityIsKnown(RoboTextAffinity affinity)
{
    return affinity == RoboTextAffinityUpstream || affinity == RoboTextAffinityDownstream;
}

const char *RoboTextAffinityName(RoboTextAffinity affinity)
{
    switch (affinity) {
    case RoboTextAffinityUpstream:
        return "upstream";
    case RoboTextAffinityDownstream:
        return "downstream";
    default:
        return "unknown";
    }
}

RoboTextAffinity RoboTextAffinityOther(RoboTextAffinity affinity)
{
    if (affinity == RoboTextAffinityUpstream) {
        return RoboTextAffinityDownstream;
    }
    if (affinity == RoboTextAffinityDownstream) {
        return RoboTextAffinityUpstream;
    }
    return affinity;
}

RoboTextSelection RoboTextSelectionMake(RoboTextPosition anchor, RoboTextPosition focus)
{
    RoboTextSelection selection;

    selection.anchor = anchor;
    selection.focus = focus;
    return selection;
}

RoboTextSelection RoboTextSelectionAt(RoboTextPosition position)
{
    return RoboTextSelectionMake(position, position);
}

RoboTextSelection RoboTextSelectionCollapse(RoboTextSelection selection)
{
    RoboTextSelection collapsed;

    collapsed.anchor = selection.anchor;
    collapsed.focus = selection.anchor;
    return collapsed;
}

RoboTextRange RoboTextSelectionRange(RoboTextSelection selection)
{
    RoboTextRange range;

    if (selection.anchor.offset <= selection.focus.offset) {
        range.start = selection.anchor.offset;
        range.end = selection.focus.offset;
    } else {
        range.start = selection.focus.offset;
        range.end = selection.anchor.offset;
    }
    return range;
}

RoboTextSelectionDirection RoboTextSelectionDirectionOf(RoboTextSelection selection)
{
    if (selection.anchor.offset == selection.focus.offset) {
        return RoboTextSelectionDirectionNone;
    }
    return selection.anchor.offset < selection.focus.offset ? RoboTextSelectionDirectionForward
                                                            : RoboTextSelectionDirectionBackward;
}

int RoboTextSelectionIsEmpty(RoboTextSelection selection)
{
    return selection.anchor.offset == selection.focus.offset;
}

RoboTextSelection RoboTextSelectionClamp(RoboTextSelection selection, RoboTextScalarIndex scalars)
{
    RoboTextSelection clamped = selection;

    if (clamped.anchor.offset > scalars) {
        clamped.anchor.offset = scalars;
    }
    if (clamped.focus.offset > scalars) {
        clamped.focus.offset = scalars;
    }
    return clamped;
}

int RoboTextSelectionDirectionIsKnown(RoboTextSelectionDirection direction)
{
    return direction == RoboTextSelectionDirectionNone || direction == RoboTextSelectionDirectionForward ||
           direction == RoboTextSelectionDirectionBackward;
}

const char *RoboTextSelectionDirectionName(RoboTextSelectionDirection direction)
{
    switch (direction) {
    case RoboTextSelectionDirectionNone:
        return "none";
    case RoboTextSelectionDirectionForward:
        return "forward";
    case RoboTextSelectionDirectionBackward:
        return "backward";
    default:
        return "unknown";
    }
}

RoboTextSelectionPoint RoboTextSelectionPointMake(float x, float y)
{
    RoboTextSelectionPoint point;

    point.x = x;
    point.y = y;
    return point;
}

int RoboTextSelectionRectIsEmpty(const RoboTextSelectionRect *rect)
{
    return !rect || rect->width <= 0.0f || rect->height <= 0.0f;
}

float RoboTextSelectionRectWidth(const RoboTextSelectionRect *rect)
{
    return rect && rect->width > 0.0f ? rect->width : 0.0f;
}

float RoboTextSelectionRectHeight(const RoboTextSelectionRect *rect)
{
    return rect && rect->height > 0.0f ? rect->height : 0.0f;
}

int RoboTextSelectionRectEqual(const RoboTextSelectionRect *a, const RoboTextSelectionRect *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

void RoboTextCaretGeometryInit(RoboTextCaretGeometry *caret)
{
    if (!caret) {
        return;
    }
    memset(caret, 0, sizeof(*caret));
    caret->writingMode = RoboTextWritingHorizontal;
}

int RoboTextCaretGeometryIsValid(const RoboTextCaretGeometry *caret)
{
    return caret && caret->valid;
}

float RoboTextCaretGeometrySpanX(const RoboTextCaretGeometry *caret)
{
    float span;

    if (!caret) {
        return 0.0f;
    }
    span = caret->end.x - caret->start.x;
    return span < 0.0f ? -span : span;
}

float RoboTextCaretGeometrySpanY(const RoboTextCaretGeometry *caret)
{
    float span;

    if (!caret) {
        return 0.0f;
    }
    span = caret->end.y - caret->start.y;
    return span < 0.0f ? -span : span;
}

int RoboTextCaretGeometryIsHorizontal(const RoboTextCaretGeometry *caret)
{
    /*
     * A caret's *segment* lies across the block axis, so a horizontal line's caret is vertical and asks for
     * `SpanY`. The reading is therefore the opposite of the writing mode's own name, and saying so here is the
     * point of the door: a caller that assumed "horizontal mode, horizontal caret" would draw a one-pixel line
     * across the text instead of through it.
     */
    return caret ? RoboTextCaretGeometrySpanX(caret) > RoboTextCaretGeometrySpanY(caret) : 0;
}

void RoboTextSelectionGeometryInit(RoboTextSelectionGeometry *geometry)
{
    if (!geometry) {
        return;
    }
    memset(geometry, 0, sizeof(*geometry));
    geometry->isEmpty = 1;
}

int RoboTextSelectionGeometryIsEmpty(const RoboTextSelectionGeometry *geometry)
{
    return !geometry || geometry->count == 0u;
}

const RoboTextSelectionRect *RoboTextSelectionGeometryAt(const RoboTextSelectionGeometry *geometry,
                                                         size_t index)
{
    if (!geometry || index >= geometry->count) {
        return NULL;
    }
    return &geometry->rects[index];
}

void RoboTextSelectionHitInit(RoboTextSelectionHit *hit)
{
    if (!hit) {
        return;
    }
    memset(hit, 0, sizeof(*hit));
    hit->affinity = RoboTextAffinityDownstream;
}

void RoboTextSelectionOptionsInit(RoboTextSelectionOptions *options)
{
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->caretThickness = 1.0f;
    options->includeTrailingWhitespace = 1;
    options->preferNearestLine = 1;
}

/* ------------------------------------------------------------------ */
/* Placement                                                           */
/* ------------------------------------------------------------------ */

void RoboTextSelectionPlacementInit(RoboTextSelectionPlacement *placement)
{
    if (!placement) {
        return;
    }
    memset(placement, 0, sizeof(*placement));
    placement->flags = RoboTextSelectionPlacementDefault;
}

Result RoboTextSelectionPlacementForRun(const RoboTextGlyphRun *run,
                                        size_t runIndex,
                                        size_t lineIndex,
                                        float originX,
                                        float originY,
                                        RoboTextSelectionPlacement *out,
                                        RoboTextDiag *diag)
{
    RoboTextWritingAxes axes;
    RoboTextGlyphRunProblem problem = RoboTextGlyphRunProblemNone;
    float ascent;
    float descent;

    if (!run || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a run and an out placement are required");
    }
    if (RoboTextGlyphRunValidate(run, &problem, NULL) != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "the run is not a valid run to place");
    }

    RoboTextSelectionPlacementInit(out);
    out->runIndex = runIndex;
    out->lineIndex = lineIndex;
    out->origin = RoboTextSelectionPointMake(originX, originY);
    out->writingMode = RoboTextGlyphRunWritingMode(run);

    /*
     * The band is the line's cross extent expressed **along the block axis**, which is why it is computed with
     * the axes rather than as a pair of y coordinates: for a horizontal line the block axis is `+y` and the band
     * is the familiar `-ascent .. +descent`, while a vertical line's block axis is `±x` and the same two numbers
     * become its cross extent. A run whose metrics will not read gets a zero-height band rather than a refusal —
     * see the header: a caret that is visibly wrong beats a selection that cannot be computed.
     */
    axes = RoboTextWritingModeAxes(out->writingMode);
    ascent = RoboTextGlyphRunAscent(run);
    descent = RoboTextGlyphRunDescent(run);
    if (!(ascent >= 0.0f)) {
        ascent = 0.0f;
    }
    if (!(descent >= 0.0f)) {
        descent = 0.0f;
    }
    /*
     * The near end of the band is *before* the baseline along the block axis, so a mode whose block vector
     * points negative (a right-to-left column) gets the two ends the other way round — which is exactly the
     * ordering a caret drawn from `start` to `end` needs, and why the ordering is decided by the axes rather
     * than by a comparison of two y values.
     */
    out->blockStart = -ascent * (axes.blockX + axes.blockY);
    out->blockEnd = descent * (axes.blockX + axes.blockY);
    return RoboOk;
}

Result RoboTextSelectionPlacementProject(const RoboTextSelectionPlacement *placement,
                                        RoboTextSelectionPoint point,
                                        RoboTextSelectionPoint *out,
                                        RoboTextDiag *diag)
{
    RoboTextWritingAxes axes;
    float dx;
    float dy;

    if (!placement || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a placement and an out point are required");
    }
    if (!RoboTextWritingModeIsKnown(placement->writingMode)) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "the placement's writing mode is not a defined one");
    }
    axes = RoboTextWritingModeAxes(placement->writingMode);
    dx = point.x - placement->origin.x;
    dy = point.y - placement->origin.y;
    /*
     * Both axes are unit vectors on one of the two coordinates, so the projection is a sum of two products
     * rather than a matrix multiply — and it is written here, once, so that no query in this file ever writes
     * `point.x - origin.x` and no caller has to know which axis a writing mode put the pen on.
     */
    out->x = dx * axes.inlineX + dy * axes.inlineY;
    out->y = dx * axes.blockX + dy * axes.blockY;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The map                                                             */
/* ------------------------------------------------------------------ */

struct RoboTextSelectionMap {
    /** Borrowed: the runs, clusters and source every query reads. */
    const RoboTextGlyphRunList *list;
    /** Owned copy of the caller's placements, in the caller's order (which is visual order). */
    RoboTextSelectionPlacement *placements;
    size_t placementCount;
    size_t lineCount;
    RoboTextSelectionOptions options;
    RoboTextRange source;
    RoboTextScalarIndex scalars;
};

/** The run a placement names, or NULL when the placement is not one of this map's. */
static const RoboTextGlyphRun *MapRun(const RoboTextSelectionMap *map,
                                      const RoboTextSelectionPlacement *placement)
{
    return RoboTextGlyphRunListRun(map->list, placement->runIndex);
}

/** The distance the pen moves within the run, along its own inline axis. */
static float RunInlineTotal(const RoboTextGlyphRun *run)
{
    float total;

    if (!run) {
        return 0.0f;
    }
    total = RoboTextGlyphRunIsVertical(run) ? RoboTextGlyphRunAdvanceY(run) : RoboTextGlyphRunAdvance(run);
    return total >= 0.0f ? total : 0.0f;
}

/** One glyph's advance along the inline axis. */
static float GlyphInlineAdvance(const RoboTextGlyphRun *run, const RoboTextGlyph *glyph)
{
    return RoboTextGlyphRunIsVertical(run) ? glyph->advanceY : glyph->advanceX;
}

/** Non-zero when the caller draws this placement's run backwards along its inline axis. */
static int PlacementIsReversed(const RoboTextSelectionPlacement *placement)
{
    return (placement->flags & RoboTextSelectionPlacementReversed) != 0u;
}

/**
 * A cluster's span in **caller** inline coordinates: distance from the pen origin along the inline axis.
 *
 * In the run's own frame a cluster occupies `[before, before + own)`, which is the pen's own arithmetic. In the
 * caller's frame it occupies that span when the placement draws forwards, and its mirror inside the run's box
 * when it draws backwards — which is the whole of what `reversed` means, and the only place in this file where
 * the glyph array's order stops being the visual order.
 */
static void ClusterInlineSpan(const RoboTextGlyphRun *run,
                              const RoboTextGlyphCluster *cluster,
                              float inlineTotal,
                              int reversed,
                              float *outStart,
                              float *outEnd)
{
    float before = 0.0f;
    float own = 0.0f;
    RoboTextGlyphIndex index;

    for (index = 0u; index < cluster->glyphStart; index++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, index);

        if (glyph) {
            before += GlyphInlineAdvance(run, glyph);
        }
    }
    for (index = cluster->glyphStart; index < cluster->glyphEnd; index++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, index);

        if (glyph) {
            own += GlyphInlineAdvance(run, glyph);
        }
    }
    if (reversed) {
        *outStart = inlineTotal - (before + own);
        *outEnd = inlineTotal - before;
    } else {
        *outStart = before;
        *outEnd = before + own;
    }
}

/** Open a placement's block band in its own axis order: `*outLow <= *outHigh`. */
static void PlacementBand(const RoboTextSelectionPlacement *placement, float *outLow, float *outHigh)
{
    if (placement->blockStart <= placement->blockEnd) {
        *outLow = placement->blockStart;
        *outHigh = placement->blockEnd;
    } else {
        *outLow = placement->blockEnd;
        *outHigh = placement->blockStart;
    }
}

/**
 * The two **logical** edges of a cluster as positions, and their inline offsets.
 *
 * Forwards, a cluster's visual start is its source start. Backwards, it is its source **end**, because the
 * glyph at the caller's smaller offset came from the last scalar the cluster covers. Getting this pair the
 * wrong way round is the single mistake that makes an RTL caret jump to the wrong side of a word, so it is one
 * function used by every query rather than four expressions that have to agree.
 */
static void LogicalEdges(const RoboTextGlyphRun *run,
                         const RoboTextGlyphCluster *cluster,
                         float inlineTotal,
                         int reversed,
                         RoboTextPosition *outStartPosition,
                         float *outStartOffset,
                         RoboTextPosition *outEndPosition,
                         float *outEndOffset)
{
    float low = 0.0f;
    float high = 0.0f;

    ClusterInlineSpan(run, cluster, inlineTotal, reversed, &low, &high);
    if (reversed) {
        *outStartPosition = RoboTextPositionMake(cluster->source.end);
        *outStartOffset = low;
        *outEndPosition = RoboTextPositionMake(cluster->source.start);
        *outEndOffset = high;
    } else {
        *outStartPosition = RoboTextPositionMake(cluster->source.start);
        *outStartOffset = low;
        *outEndPosition = RoboTextPositionMake(cluster->source.end);
        *outEndOffset = high;
    }
}

/** The cluster covering @p scalar of @p run, or NULL when none does. @p scalar is absolute. */
static const RoboTextGlyphCluster *ClusterOfScalar(const RoboTextGlyphRun *run, RoboTextScalarIndex scalar)
{
    RoboTextClusterIndex index = 0u;

    if (!run || RoboTextGlyphRunClusterForScalar(run, scalar, &index, NULL) != RoboOk) {
        return NULL;
    }
    return RoboTextGlyphRunCluster(run, index);
}

/** Non-zero when every scalar of @p cluster is whitespace, by the Unicode tables rather than by width. */
static int ClusterIsWhitespace(const RoboTextGlyphRun *run, const RoboTextGlyphCluster *cluster)
{
    RoboTextScalarIndex scalar;

    for (scalar = cluster->source.start; scalar < cluster->source.end; scalar++) {
        RoboTextGlyphIndex index = 0u;
        const RoboTextGlyph *glyph;

        if (RoboTextGlyphRunGlyphForScalarIndex(run, scalar, &index, NULL) != RoboOk) {
            /* No glyph to speak for the scalar: it cannot be shown to be whitespace, so it is not. */
            return 0;
        }
        glyph = RoboTextGlyphRunGlyph(run, index);
        if (glyph && !RoboTextUnicodeIsWhitespace((RoboTextCodePoint)glyph->codePoint)) {
            return 0;
        }
    }
    return 1;
}

Result RoboTextSelectionMapCreate(const RoboTextGlyphRunList *list,
                                  const RoboTextSelectionPlacement *placements,
                                  size_t placementCount,
                                  const RoboTextSelectionOptions *options,
                                  RoboTextSelectionMap **out,
                                  RoboTextDiag *diag)
{
    RoboTextSelectionMap *map = NULL;
    size_t runCount;
    size_t index;

    if (!list || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a run list and an out map are required");
    }
    *out = NULL;
    if (!placements && placementCount > 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "placements were named but not given");
    }
    runCount = RoboTextGlyphRunListCount(list);
    if (placementCount != runCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "every run of the list must be placed exactly once");
    }

    map = (RoboTextSelectionMap *)RoboCalloc(1u, sizeof(*map));
    if (!map) {
        return RoboOutOfMemory;
    }
    map->list = list;
    map->placementCount = placementCount;
    map->source = RoboTextGlyphRunListSourceRange(list);
    map->scalars = map->source.end;
    RoboTextSelectionOptionsInit(&map->options);
    if (options) {
        map->options = *options;
    }
    if (placementCount > 0u) {
        map->placements = (RoboTextSelectionPlacement *)RoboCalloc(placementCount, sizeof(*map->placements));
        if (!map->placements) {
            RoboFree(map);
            return RoboOutOfMemory;
        }
    }

    for (index = 0u; index < placementCount; index++) {
        const RoboTextGlyphRun *run;
        size_t earlier;

        if (placements[index].runIndex >= runCount) {
            RoboTextSelectionMapDestroy(map);
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                                   "a placement names no run of the list");
        }
        for (earlier = 0u; earlier < index; earlier++) {
            if (placements[earlier].runIndex == placements[index].runIndex) {
                RoboTextSelectionMapDestroy(map);
                return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                                       "a run was placed twice");
            }
        }
        if (index > 0u && placements[index - 1u].lineIndex > placements[index].lineIndex) {
            RoboTextSelectionMapDestroy(map);
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                                   "the placements' lines are not in visual order");
        }
        run = RoboTextGlyphRunListRun(list, placements[index].runIndex);
        if (!run) {
            RoboTextSelectionMapDestroy(map);
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                                   "a placement names no run of the list");
        }
        map->placements[index] = placements[index];
        /*
         * The run's own writing mode wins over the placement's, and it is not a preference: the run was
         * **shaped** in its mode, so a placement that disagreed would be projecting a point onto an axis the
         * glyphs never moved along. `RoboTextSelectionPlacementProject` is the door a caller uses when it has
         * a placement and no map, and the field is what it reads.
         */
        map->placements[index].writingMode = RoboTextGlyphRunWritingMode(run);
    }

    /* Distinct line indices, which is the answer to "how many lines is this paragraph". */
    for (index = 0u; index < placementCount; index++) {
        size_t earlier;
        int seen = 0;

        for (earlier = 0u; earlier < index; earlier++) {
            if (map->placements[earlier].lineIndex == map->placements[index].lineIndex) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            map->lineCount++;
        }
    }

    *out = map;
    return RoboOk;
}

void RoboTextSelectionMapDestroy(RoboTextSelectionMap *map)
{
    if (!map) {
        return;
    }
    RoboFree(map->placements);
    RoboFree(map);
}

size_t RoboTextSelectionMapPlacementCount(const RoboTextSelectionMap *map)
{
    return map ? map->placementCount : 0u;
}

size_t RoboTextSelectionMapLineCount(const RoboTextSelectionMap *map)
{
    return map ? map->lineCount : 0u;
}

RoboTextRange RoboTextSelectionMapSourceRange(const RoboTextSelectionMap *map)
{
    return map ? map->source : RoboTextRangeAt(0u);
}

RoboTextSelectionOptions RoboTextSelectionMapOptions(const RoboTextSelectionMap *map)
{
    RoboTextSelectionOptions options;

    if (map) {
        return map->options;
    }
    RoboTextSelectionOptionsInit(&options);
    return options;
}

const RoboTextSelectionPlacement *RoboTextSelectionMapPlacementAt(const RoboTextSelectionMap *map,
                                                                  size_t index)
{
    if (!map || index >= map->placementCount) {
        return NULL;
    }
    return &map->placements[index];
}

const RoboTextSelectionPlacement *RoboTextSelectionMapPlacementForRun(const RoboTextSelectionMap *map,
                                                                     size_t runIndex)
{
    size_t index;

    if (!map) {
        return NULL;
    }
    for (index = 0u; index < map->placementCount; index++) {
        if (map->placements[index].runIndex == runIndex) {
            return &map->placements[index];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Hit testing                                                         */
/* ------------------------------------------------------------------ */

/**
 * The placement a point lands in, with the projections the caller's answer is computed from.
 *
 * The line is chosen on the **block** axis and the run within it on the inline axis, which is the order a
 * reader's eye takes across a page: you pick the line, then the place in it. A point inside a line but past
 * every run of it clamps to that line's nearest run — a click to the right of a line's last word belongs to
 * that line — while a point off every line's band needs `preferNearestLine` to resolve at all.
 */
static const RoboTextSelectionPlacement *HitPlacement(const RoboTextSelectionMap *map,
                                                      RoboTextSelectionPoint point,
                                                      float *outInline,
                                                      size_t *outLineIndex)
{
    float lineDist = FLT_MAX;
    size_t chosenLine = (size_t)-1;
    size_t index;

    for (index = 0u; index < map->placementCount; index++) {
        const RoboTextSelectionPlacement *candidate = &map->placements[index];
        RoboTextSelectionPoint projected;
        float lineLow = FLT_MAX;
        float lineHigh = -FLT_MAX;
        size_t member;
        size_t earlier;
        int seen = 0;

        /* One pass per line: the first placement of each line answers for the whole band. */
        for (earlier = 0u; earlier < index; earlier++) {
            if (map->placements[earlier].lineIndex == candidate->lineIndex) {
                seen = 1;
                break;
            }
        }
        if (seen) {
            continue;
        }
        if (RoboTextSelectionPlacementProject(candidate, point, &projected, NULL) != RoboOk) {
            continue;
        }
        for (member = 0u; member < map->placementCount; member++) {
            const RoboTextSelectionPlacement *other = &map->placements[member];
            float otherLow;
            float otherHigh;

            if (other->lineIndex != candidate->lineIndex) {
                continue;
            }
            PlacementBand(other, &otherLow, &otherHigh);
            if (otherLow < lineLow) {
                lineLow = otherLow;
            }
            if (otherHigh > lineHigh) {
                lineHigh = otherHigh;
            }
        }
        if (projected.y >= lineLow - SelectionEpsilon && projected.y <= lineHigh + SelectionEpsilon) {
            chosenLine = candidate->lineIndex;
            break;
        }
        if (map->options.preferNearestLine) {
            float distance = projected.y < lineLow ? lineLow - projected.y : projected.y - lineHigh;

            if (distance < lineDist) {
                lineDist = distance;
                chosenLine = candidate->lineIndex;
            }
        }
    }

    if (chosenLine == (size_t)-1) {
        return NULL;
    }

    {
        const RoboTextSelectionPlacement *best = NULL;
        float bestDistance = FLT_MAX;
        float bestInline = 0.0f;

        for (index = 0u; index < map->placementCount; index++) {
            const RoboTextSelectionPlacement *candidate = &map->placements[index];
            const RoboTextGlyphRun *run;
            RoboTextSelectionPoint projected;
            float total;
            float distance;

            if (candidate->lineIndex != chosenLine) {
                continue;
            }
            run = MapRun(map, candidate);
            if (!run) {
                continue;
            }
            if (RoboTextSelectionPlacementProject(candidate, point, &projected, NULL) != RoboOk) {
                continue;
            }
            total = RunInlineTotal(run);
            if (projected.x >= -SelectionEpsilon && projected.x <= total + SelectionEpsilon) {
                *outInline = projected.x < 0.0f ? 0.0f : projected.x;
                *outLineIndex = chosenLine;
                return candidate;
            }
            distance = projected.x < 0.0f ? -projected.x : projected.x - total;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = candidate;
                bestInline = projected.x;
            }
        }
        if (best) {
            *outInline = bestInline;
            *outLineIndex = chosenLine;
            return best;
        }
    }
    return NULL;
}

/**
 * Resolve an inline distance inside @p run to a cluster boundary (Phases 12.21.24/38).
 *
 * The rule, and it is the one every editor that has had to explain itself uses: a point inside a cluster names
 * one of the cluster's two edges, never a place inside it, because the text has no position there. Which edge
 * is the half that was clicked, and the affinity that comes back is the side of that edge the caret hugs.
 *
 * The **visual** order is what is walked, and it is the order of the offsets rather than of the array: for a
 * backwards run the smallest offset belongs to the last logical cluster, which is why the edges come from
 * `LogicalEdges` and never from `cluster->source` directly.
 */
static RoboTextPosition ResolveInlinePosition(const RoboTextGlyphRun *run,
                                             float inlineTotal,
                                             int reversed,
                                             float inlineDistance,
                                             float *outBoundaryInline,
                                             RoboTextScalarIndex *outClusterStart,
                                             int *outSnapped,
                                             RoboTextAffinity *outAffinity)
{
    size_t count = RoboTextGlyphRunClusterCount(run);
    const RoboTextGlyphCluster *hit = NULL;
    float hitStart = 0.0f;
    float hitEnd = 0.0f;
    size_t index;

    *outClusterStart = RoboTextGlyphRunSourceRange(run).start;
    *outSnapped = 0;
    *outAffinity = RoboTextAffinityDownstream;
    *outBoundaryInline = 0.0f;

    for (index = 0u; index < count; index++) {
        const RoboTextGlyphCluster *cluster = RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)index);
        float start;
        float end;

        if (!cluster) {
            continue;
        }
        ClusterInlineSpan(run, cluster, inlineTotal, reversed, &start, &end);
        if (inlineDistance >= start - SelectionEpsilon && inlineDistance <= end + SelectionEpsilon) {
            hit = cluster;
            hitStart = start;
            hitEnd = end;
            break;
        }
    }

    if (hit) {
        RoboTextPosition startPosition;
        RoboTextPosition endPosition;
        float startOffset;
        float endOffset;
        int nearerStart;

        LogicalEdges(run, hit, inlineTotal, reversed, &startPosition, &startOffset, &endPosition, &endOffset);
        *outClusterStart = hit->source.start;
        *outSnapped = RoboTextRangeLength(hit->source) > 1u ? 1 : 0;
        /*
         * The near half names the edge the pen met first and the far half the edge it left last, and the
         * affinity says which side of that position the caret hugs: the visual start of a forwards cluster is
         * the caret *before* it (hugging the text that follows, `Downstream`), while the visual start of a
         * backwards cluster is the caret after the text it came from (`Upstream`).
         */
        nearerStart = (inlineDistance - hitStart) <= (hitEnd - inlineDistance);
        if (nearerStart) {
            *outBoundaryInline = startOffset;
            *outAffinity = reversed ? RoboTextAffinityUpstream : RoboTextAffinityDownstream;
            return startPosition;
        }
        *outBoundaryInline = endOffset;
        *outAffinity = reversed ? RoboTextAffinityDownstream : RoboTextAffinityUpstream;
        return endPosition;
    }

    /* Before every cluster, or past all of them: the two edges of the run in visual order. */
    if (count > 0u) {
        const RoboTextGlyphCluster *first = NULL;
        const RoboTextGlyphCluster *last = NULL;
        float firstOffset = 0.0f;
        float lastOffset = 0.0f;

        for (index = 0u; index < count; index++) {
            const RoboTextGlyphCluster *cluster = RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)index);
            float start;
            float end;

            if (!cluster) {
                continue;
            }
            ClusterInlineSpan(run, cluster, inlineTotal, reversed, &start, &end);
            if (!first || start < firstOffset) {
                first = cluster;
                firstOffset = start;
            }
            if (!last || end > lastOffset) {
                last = cluster;
                lastOffset = end;
            }
        }
        if (first && inlineDistance <= firstOffset) {
            RoboTextPosition startPosition;
            RoboTextPosition endPosition;
            float startOffset;
            float endOffset;

            LogicalEdges(run, first, inlineTotal, reversed, &startPosition, &startOffset, &endPosition,
                         &endOffset);
            *outBoundaryInline = startOffset;
            *outClusterStart = first->source.start;
            *outAffinity = reversed ? RoboTextAffinityUpstream : RoboTextAffinityDownstream;
            return startPosition;
        }
        if (last) {
            RoboTextPosition startPosition;
            RoboTextPosition endPosition;
            float startOffset;
            float endOffset;

            LogicalEdges(run, last, inlineTotal, reversed, &startPosition, &startOffset, &endPosition,
                         &endOffset);
            *outBoundaryInline = endOffset;
            *outClusterStart = last->source.start;
            *outAffinity = reversed ? RoboTextAffinityDownstream : RoboTextAffinityUpstream;
            return endPosition;
        }
    }

    return RoboTextPositionMake(RoboTextGlyphRunSourceRange(run).start);
}

Result RoboTextSelectionMapHitTest(const RoboTextSelectionMap *map,
                                   RoboTextSelectionPoint point,
                                   RoboTextSelectionHit *out,
                                   RoboTextDiag *diag)
{
    const RoboTextSelectionPlacement *placement;
    const RoboTextGlyphRun *run;
    RoboTextSelectionHit hit;
    float inlineDistance = 0.0f;
    size_t lineIndex = 0u;
    float boundaryInline = 0.0f;
    RoboTextScalarIndex clusterStart = 0u;
    int snapped = 0;
    RoboTextAffinity affinity = RoboTextAffinityDownstream;

    if (!map || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a map and an out hit are required");
    }
    RoboTextSelectionHitInit(out);
    if (map->placementCount == 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboNotFound,
                               "the map places no run, so no point is on it");
    }

    placement = HitPlacement(map, point, &inlineDistance, &lineIndex);
    if (!placement) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboNotFound,
                               "no line of the map is near that point");
    }
    run = MapRun(map, placement);
    if (!run) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboNotFound, "the placement names no run");
    }

    {
        float inlineTotal = RunInlineTotal(run);
        int reversed = PlacementIsReversed(placement);
        RoboTextRange runSource = RoboTextGlyphRunSourceRange(run);
        RoboTextPosition position;

        position = ResolveInlinePosition(run, inlineTotal, reversed, inlineDistance, &boundaryInline,
                                        &clusterStart, &snapped, &affinity);

        /*
         * Trailing whitespace, applied to the answer rather than to the walk: with the option off, a click past
         * a line's text stops at the boundary **before** the line's trailing spaces rather than after them. The
         * rule is read off the scalars (`RoboTextUnicodeIsWhitespace`) and never off the advances — a space's
         * width is the font's business, and a zero-width space is still a space.
         */
        if (!map->options.includeTrailingWhitespace && position.offset >= runSource.end) {
            size_t count = RoboTextGlyphRunClusterCount(run);
            size_t firstTrailing = count;

            while (firstTrailing > 0u) {
                const RoboTextGlyphCluster *cluster =
                    RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)(firstTrailing - 1u));

                if (!cluster || !ClusterIsWhitespace(run, cluster)) {
                    break;
                }
                firstTrailing--;
            }
            if (firstTrailing < count) {
                const RoboTextGlyphCluster *cluster =
                    RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)firstTrailing);
                RoboTextPosition startPosition;
                RoboTextPosition endPosition;
                float startOffset;
                float endOffset;

                LogicalEdges(run, cluster, inlineTotal, reversed, &startPosition, &startOffset, &endPosition,
                             &endOffset);
                position = startPosition;
                boundaryInline = startOffset;
                clusterStart = cluster->source.start;
                affinity = RoboTextAffinityDownstream;
                snapped = 0;
            }
        }

        hit.position = position;
        hit.affinity = affinity;
        hit.runIndex = placement->runIndex;
        hit.lineIndex = lineIndex;
        hit.clusterStart = clusterStart;
        hit.snapped = snapped;
        hit.distance = inlineDistance - boundaryInline;
    }

    *out = hit;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Carets                                                             */
/* ------------------------------------------------------------------ */

/**
 * The placement that answers @p position for @p affinity, and whether it answers it from its own end.
 *
 * A position inside a run has one visual location and either affinity names it. A position that is one run's
 * end *and* the next run's start — a bidi boundary, a fallback font change, a line break — has two, and the
 * affinity is the whole of which one was meant. The search order is therefore: a run that strictly contains the
 * position, then the run that wanted side, then the other one.
 */
static const RoboTextSelectionPlacement *CaretPlacement(const RoboTextSelectionMap *map,
                                                        RoboTextPosition position,
                                                        RoboTextAffinity affinity,
                                                        int *outFromEnd)
{
    const RoboTextSelectionPlacement *inside = NULL;
    const RoboTextSelectionPlacement *starts = NULL;
    const RoboTextSelectionPlacement *ends = NULL;
    size_t index;

    *outFromEnd = 0;
    for (index = 0u; index < map->placementCount; index++) {
        const RoboTextSelectionPlacement *placement = &map->placements[index];
        const RoboTextGlyphRun *run = MapRun(map, placement);
        RoboTextRange source;

        if (!run) {
            continue;
        }
        source = RoboTextGlyphRunSourceRange(run);
        if (position.offset > source.start && position.offset < source.end) {
            if (!inside) {
                inside = placement;
            }
            continue;
        }
        if (position.offset == source.start && !starts) {
            starts = placement;
        }
        if (position.offset == source.end && !ends) {
            ends = placement;
        }
    }

    if (inside) {
        *outFromEnd = 0;
        return inside;
    }
    if (affinity == RoboTextAffinityUpstream && ends) {
        *outFromEnd = 1;
        return ends;
    }
    if (starts) {
        *outFromEnd = 0;
        return starts;
    }
    if (ends) {
        *outFromEnd = 1;
        return ends;
    }
    return NULL;
}

/**
 * The inline offset of @p position inside @p run, snapping a position inside a cluster to the edge the
 * affinity names (the same rule the hit test applies, over the same clusters).
 */
static int CaretInlineOffset(const RoboTextGlyphRun *run,
                            RoboTextPosition position,
                            RoboTextAffinity affinity,
                            int reversed,
                            int fromEnd,
                            float *outOffset)
{
    RoboTextRange source = RoboTextGlyphRunSourceRange(run);
    float inlineTotal = RunInlineTotal(run);
    const RoboTextGlyphCluster *cluster;
    RoboTextPosition startPosition;
    RoboTextPosition endPosition;
    float startOffset = 0.0f;
    float endOffset = 0.0f;

    if (fromEnd || position.offset >= source.end) {
        cluster = RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)(RoboTextGlyphRunClusterCount(run) - 1u));
        if (!cluster) {
            *outOffset = reversed ? 0.0f : inlineTotal;
            return 1;
        }
        LogicalEdges(run, cluster, inlineTotal, reversed, &startPosition, &startOffset, &endPosition,
                     &endOffset);
        *outOffset = endOffset;
        return 1;
    }
    if (position.offset <= source.start) {
        cluster = RoboTextGlyphRunCluster(run, 0u);
        if (!cluster) {
            *outOffset = reversed ? inlineTotal : 0.0f;
            return 1;
        }
        LogicalEdges(run, cluster, inlineTotal, reversed, &startPosition, &startOffset, &endPosition,
                     &endOffset);
        *outOffset = startOffset;
        return 1;
    }
    cluster = ClusterOfScalar(run, position.offset);
    if (!cluster) {
        return 0;
    }
    LogicalEdges(run, cluster, inlineTotal, reversed, &startPosition, &startOffset, &endPosition, &endOffset);
    /*
     * A position inside a multi-scalar cluster is snapped to the edge the affinity names, so a caret can never
     * be placed inside a ligature or a conjunct; a position that already *is* an edge answers with that edge's
     * own offset, which is what makes the round trip (position → caret → hit → position) stable.
     */
    if (position.offset == cluster->source.start) {
        *outOffset = startOffset;
        return 1;
    }
    if (position.offset == cluster->source.end) {
        *outOffset = endOffset;
        return 1;
    }
    *outOffset = affinity == RoboTextAffinityDownstream ? startOffset : endOffset;
    return 1;
}

Result RoboTextSelectionMapCaret(const RoboTextSelectionMap *map,
                                 RoboTextPosition position,
                                 RoboTextAffinity affinity,
                                 RoboTextCaretGeometry *out,
                                 RoboTextDiag *diag)
{
    const RoboTextSelectionPlacement *placement;
    const RoboTextGlyphRun *run;
    RoboTextWritingAxes axes;
    float offset = 0.0f;
    float inlineX = 0.0f;
    float inlineY = 0.0f;
    float startX = 0.0f;
    float startY = 0.0f;
    float endX = 0.0f;
    float endY = 0.0f;
    int fromEnd = 0;

    if (!map || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a map and an out caret are required");
    }
    RoboTextCaretGeometryInit(out);
    out->thickness = map->options.caretThickness;
    if (map->placementCount == 0u) {
        return RoboOk;
    }

    placement = CaretPlacement(map, position, affinity, &fromEnd);
    if (!placement) {
        return RoboOk;
    }
    run = MapRun(map, placement);
    if (!run) {
        return RoboOk;
    }
    if (!CaretInlineOffset(run, position, affinity, PlacementIsReversed(placement), fromEnd, &offset)) {
        return RoboOk;
    }

    axes = RoboTextWritingModeAxes(RoboTextGlyphRunWritingMode(run));
    (void)RoboTextWritingAxesInline(&axes, offset, &inlineX, &inlineY);
    (void)RoboTextWritingAxesBlock(&axes, placement->blockStart, &startX, &startY);
    (void)RoboTextWritingAxesBlock(&axes, placement->blockEnd, &endX, &endY);
    out->start = RoboTextSelectionPointMake(placement->origin.x + inlineX + startX,
                                            placement->origin.y + inlineY + startY);
    out->end = RoboTextSelectionPointMake(placement->origin.x + inlineX + endX,
                                          placement->origin.y + inlineY + endY);
    out->thickness = map->options.caretThickness;
    out->writingMode = RoboTextGlyphRunWritingMode(run);
    out->valid = 1;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Selection geometry                                                  */
/* ------------------------------------------------------------------ */

static RoboTextSelectionRect RectFromCorners(RoboTextSelectionPoint a, RoboTextSelectionPoint b)
{
    RoboTextSelectionRect rect;

    rect.x = a.x < b.x ? a.x : b.x;
    rect.y = a.y < b.y ? a.y : b.y;
    rect.width = a.x < b.x ? b.x - a.x : a.x - b.x;
    rect.height = a.y < b.y ? b.y - a.y : a.y - b.y;
    return rect;
}

/** Add @p rect to @p geometry, or report the cap. */
static void GeometryAdd(RoboTextSelectionGeometry *geometry, RoboTextSelectionRect rect)
{
    if (geometry->count >= RoboTextMaxSelectionRects) {
        geometry->truncated = 1;
        return;
    }
    if (geometry->count == 0u) {
        geometry->bounds = rect;
    } else {
        float minX = geometry->bounds.x < rect.x ? geometry->bounds.x : rect.x;
        float minY = geometry->bounds.y < rect.y ? geometry->bounds.y : rect.y;
        float maxX = geometry->bounds.x + geometry->bounds.width;
        float maxY = geometry->bounds.y + geometry->bounds.height;
        float rectMaxX = rect.x + rect.width;
        float rectMaxY = rect.y + rect.height;

        if (rectMaxX > maxX) {
            maxX = rectMaxX;
        }
        if (rectMaxY > maxY) {
            maxY = rectMaxY;
        }
        geometry->bounds.x = minX;
        geometry->bounds.y = minY;
        geometry->bounds.width = maxX - minX;
        geometry->bounds.height = maxY - minY;
    }
    geometry->rects[geometry->count] = rect;
    geometry->count++;
}

Result RoboTextSelectionMapGeometry(const RoboTextSelectionMap *map,
                                    RoboTextSelection selection,
                                    RoboTextSelectionGeometry *out,
                                    RoboTextDiag *diag)
{
    RoboTextRange range;
    size_t index;

    if (!map || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a map and an out geometry are required");
    }
    RoboTextSelectionGeometryInit(out);
    range = RoboTextSelectionRange(RoboTextSelectionClamp(selection, map->scalars));
    if (RoboTextRangeIsEmpty(range)) {
        return RoboOk;
    }

    /*
     * Placements are walked in the order they were given, which is the order they are laid out in — the
     * constructor does not sort them, because sorting by run index would put a bidi line's runs back into
     * logical order and lose the one fact the rectangle order is supposed to carry.
     */
    for (index = 0u; index < map->placementCount; index++) {
        const RoboTextSelectionPlacement *placement = &map->placements[index];
        const RoboTextGlyphRun *run = MapRun(map, placement);
        RoboTextRange source;
        RoboTextRange covered;
        const RoboTextGlyphCluster *firstCluster = NULL;
        const RoboTextGlyphCluster *lastCluster = NULL;
        size_t first;
        size_t last;
        size_t probe;
        float inlineTotal;
        int reversed;
        float from = 0.0f;
        float to = 0.0f;
        RoboTextWritingAxes axes;
        float inlineX;
        float inlineY;
        float blockX;
        float blockY;
        RoboTextSelectionPoint cornerA;
        RoboTextSelectionPoint cornerB;
        RoboTextSelectionRect rect;

        if (!run) {
            continue;
        }
        source = RoboTextGlyphRunSourceRange(run);
        covered = RoboTextRangeMake(source.start, source.end);
        if (!RoboTextRangeIntersects(covered, range)) {
            continue;
        }

        first = (size_t)-1;
        last = (size_t)-1;
        for (probe = 0u; probe < RoboTextGlyphRunClusterCount(run); probe++) {
            const RoboTextGlyphCluster *cluster =
                RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)probe);

            if (!cluster || !RoboTextRangeIntersects(cluster->source, range)) {
                continue;
            }
            if (first == (size_t)-1) {
                first = probe;
            }
            last = probe;
        }
        if (first == (size_t)-1) {
            continue;
        }
        firstCluster = RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)first);
        lastCluster = RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)last);
        if (!firstCluster || !lastCluster) {
            continue;
        }

        inlineTotal = RunInlineTotal(run);
        reversed = PlacementIsReversed(placement);
        /*
         * A cluster is never split geometrically (Phase 12.21.9): a range that begins or ends inside one covers
         * the whole of it, because the text has no boundary there to draw a rectangle against. So the two
         * offsets are the **logical edges** of the first and last covered clusters, whichever scalars the range
         * named inside them — which is also what keeps a base and its zero-advance mark to one rectangle.
         */
        {
            RoboTextPosition startPosition;
            RoboTextPosition endPosition;
            float startOffset = 0.0f;
            float endOffset = 0.0f;

            LogicalEdges(run, firstCluster, inlineTotal, reversed, &startPosition, &startOffset, &endPosition,
                         &endOffset);
            from = startOffset;
            LogicalEdges(run, lastCluster, inlineTotal, reversed, &startPosition, &startOffset, &endPosition,
                         &endOffset);
            to = endOffset;
        }

        /*
         * A zero inline extent — a selection of nothing but zero-advance clusters, or a mark whose cluster has
         * no advance of its own — is widened to the caret's thickness rather than emitted as an invisible
         * rectangle. A selection a reader cannot see is the one failure this layer must not produce.
         */
        if (to - from < SelectionEpsilon) {
            float centre = (from + to) * 0.5f;
            float half = map->options.caretThickness * 0.5f;

            if (!(half > 0.0f)) {
                half = 0.5f;
            }
            from = centre - half;
            to = centre + half;
        }

        axes = RoboTextWritingModeAxes(RoboTextGlyphRunWritingMode(run));
        (void)RoboTextWritingAxesInline(&axes, from, &inlineX, &inlineY);
        (void)RoboTextWritingAxesBlock(&axes, placement->blockStart, &blockX, &blockY);
        cornerA = RoboTextSelectionPointMake(placement->origin.x + inlineX + blockX,
                                             placement->origin.y + inlineY + blockY);
        (void)RoboTextWritingAxesInline(&axes, to, &inlineX, &inlineY);
        (void)RoboTextWritingAxesBlock(&axes, placement->blockEnd, &blockX, &blockY);
        cornerB = RoboTextSelectionPointMake(placement->origin.x + inlineX + blockX,
                                             placement->origin.y + inlineY + blockY);
        rect = RectFromCorners(cornerA, cornerB);
        if (RoboTextSelectionRectIsEmpty(&rect)) {
            continue;
        }
        GeometryAdd(out, rect);
    }

    out->isEmpty = out->count == 0u ? 1 : 0;
    if (out->truncated) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboNotSupported,
                               "the selection needs more rectangles than the cap allows");
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

int RoboTextSelectionProblemIsKnown(RoboTextSelectionProblem problem)
{
    return problem == RoboTextSelectionProblemNone || problem == RoboTextSelectionProblemRun ||
           problem == RoboTextSelectionProblemPlacement || problem == RoboTextSelectionProblemDuplicate ||
           problem == RoboTextSelectionProblemOrder || problem == RoboTextSelectionProblemUnplaced ||
           problem == RoboTextSelectionProblemBand;
}

const char *RoboTextSelectionProblemName(RoboTextSelectionProblem problem)
{
    switch (problem) {
    case RoboTextSelectionProblemNone:
        return "none";
    case RoboTextSelectionProblemRun:
        return "run";
    case RoboTextSelectionProblemPlacement:
        return "placement";
    case RoboTextSelectionProblemDuplicate:
        return "duplicate";
    case RoboTextSelectionProblemOrder:
        return "order";
    case RoboTextSelectionProblemUnplaced:
        return "unplaced";
    case RoboTextSelectionProblemBand:
        return "band";
    default:
        return "unknown";
    }
}

Result RoboTextSelectionMapValidate(const RoboTextSelectionMap *map,
                                    RoboTextSelectionProblem *outProblem,
                                    RoboTextDiag *diag)
{
    size_t runCount;
    size_t index;

    if (outProblem) {
        *outProblem = RoboTextSelectionProblemNone;
    }
    if (!map) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument, "a map is required");
    }
    runCount = RoboTextGlyphRunListCount(map->list);
    if (map->placementCount != runCount) {
        if (outProblem) {
            *outProblem = RoboTextSelectionProblemUnplaced;
        }
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a run of the list is not placed");
    }
    for (index = 0u; index < map->placementCount; index++) {
        const RoboTextSelectionPlacement *placement = &map->placements[index];
        size_t earlier;
        float low;
        float high;

        if (placement->runIndex >= runCount) {
            if (outProblem) {
                *outProblem = RoboTextSelectionProblemRun;
            }
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                                   "a placement names no run of the list");
        }
        for (earlier = 0u; earlier < index; earlier++) {
            if (map->placements[earlier].runIndex == placement->runIndex) {
                if (outProblem) {
                    *outProblem = RoboTextSelectionProblemDuplicate;
                }
                return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument, "a run was placed twice");
            }
        }
        if (!RoboTextWritingModeIsKnown(placement->writingMode)) {
            if (outProblem) {
                *outProblem = RoboTextSelectionProblemPlacement;
            }
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                                   "a placement has no defined writing mode");
        }
        PlacementBand(placement, &low, &high);
        if (!(high >= low)) {
            if (outProblem) {
                *outProblem = RoboTextSelectionProblemBand;
            }
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                                   "a placement's block band is not a band");
        }
        if (index > 0u && map->placements[index - 1u].lineIndex > placement->lineIndex) {
            if (outProblem) {
                *outProblem = RoboTextSelectionProblemOrder;
            }
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                                   "the placements' lines are not in visual order");
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                        */
/* ------------------------------------------------------------------ */

/** Copy a scratch line out, unless it does not fit — complete or absent, as everywhere in this module. */
static Result SelectionPublish(const char *scratch,
                               size_t length,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength)
{
    if (!buffer || !outLength) {
        return RoboInvalidArgument;
    }
    *outLength = length;
    buffer[0] = '\0';
    if (length + 1u > capacity) {
        return RoboNotSupported;
    }
    memcpy(buffer, scratch, length);
    buffer[length] = '\0';
    return RoboOk;
}

Result RoboTextPositionWrite(RoboTextPosition position,
                             RoboTextScalarIndex scalars,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength)
{
    char scratch[64];
    size_t length = 0u;

    if (!buffer || !outLength) {
        return RoboInvalidArgument;
    }
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, position.offset);
    if (scalars > 0u && position.offset == 0u) {
        length = RoboTextWriterText(scratch, sizeof(scratch), length, " (start)");
    } else if (scalars > 0u && position.offset >= scalars) {
        length = RoboTextWriterText(scratch, sizeof(scratch), length, " (end)");
    }
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    return SelectionPublish(scratch, length, buffer, capacity, outLength);
}

Result RoboTextSelectionWrite(RoboTextSelection selection,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength)
{
    char scratch[96];
    size_t length = 0u;
    RoboTextRange range = RoboTextSelectionRange(selection);

    if (!buffer || !outLength) {
        return RoboInvalidArgument;
    }
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, range.start);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, "..");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, range.end);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " ");
    length = RoboTextWriterText(scratch, sizeof(scratch), length,
                                RoboTextSelectionDirectionName(RoboTextSelectionDirectionOf(selection)));
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    return SelectionPublish(scratch, length, buffer, capacity, outLength);
}

Result RoboTextCaretGeometryWrite(const RoboTextCaretGeometry *caret,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength)
{
    char scratch[192];
    size_t length = 0u;

    if (!caret || !buffer || !outLength) {
        return RoboInvalidArgument;
    }
    length = RoboTextWriterText(scratch, sizeof(scratch), length,
                                RoboTextCaretGeometryIsHorizontal(caret) ? "horizontal" : "vertical");
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " (");
    length = RoboTextWriterFloat(scratch, sizeof(scratch), length, caret->start.x);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, ", ");
    length = RoboTextWriterFloat(scratch, sizeof(scratch), length, caret->start.y);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, ")-(");
    length = RoboTextWriterFloat(scratch, sizeof(scratch), length, caret->end.x);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, ", ");
    length = RoboTextWriterFloat(scratch, sizeof(scratch), length, caret->end.y);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, ") w=");
    length = RoboTextWriterFloat(scratch, sizeof(scratch), length, caret->thickness);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " ");
    length = RoboTextWriterText(scratch, sizeof(scratch), length, RoboTextWritingModeName(caret->writingMode));
    length = RoboTextWriterText(scratch, sizeof(scratch), length, caret->valid ? " valid" : " invalid");
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    return SelectionPublish(scratch, length, buffer, capacity, outLength);
}

Result RoboTextSelectionHitWrite(const RoboTextSelectionHit *hit,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength)
{
    char scratch[256];
    size_t length = 0u;

    if (!hit || !buffer || !outLength) {
        return RoboInvalidArgument;
    }
    length = RoboTextWriterText(scratch, sizeof(scratch), length, "position ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, hit->position.offset);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " ");
    length = RoboTextWriterText(scratch, sizeof(scratch), length, RoboTextAffinityName(hit->affinity));
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " (run ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, (uint32_t)hit->runIndex);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, ", line ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, (uint32_t)hit->lineIndex);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, ", cluster ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, hit->clusterStart);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, hit->snapped ? ", snapped" : ", exact");
    length = RoboTextWriterText(scratch, sizeof(scratch), length, ", ");
    length = RoboTextWriterFloat(scratch, sizeof(scratch), length, hit->distance);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, ")");
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    return SelectionPublish(scratch, length, buffer, capacity, outLength);
}

Result RoboTextSelectionMapWrite(const RoboTextSelectionMap *map,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength)
{
    char scratch[SelectionDumpChars];
    size_t length = 0u;
    size_t index;

    if (!map || !buffer || !outLength) {
        return RoboInvalidArgument;
    }
    length = RoboTextWriterText(scratch, sizeof(scratch), length, "selection map: ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, (uint32_t)map->placementCount);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " runs, ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, (uint32_t)map->lineCount);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " lines, ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, map->scalars);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " scalars");

    for (index = 0u; index < map->placementCount; index++) {
        const RoboTextSelectionPlacement *placement = &map->placements[index];
        const RoboTextGlyphRun *run = MapRun(map, placement);
        RoboTextRange source = run ? RoboTextGlyphRunSourceRange(run) : RoboTextRangeAt(0u);
        float low = 0.0f;
        float high = 0.0f;

        PlacementBand(placement, &low, &high);
        length = RoboTextWriterText(scratch, sizeof(scratch), length, "\n  run ");
        length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, (uint32_t)placement->runIndex);
        length = RoboTextWriterText(scratch, sizeof(scratch), length, " line ");
        length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, (uint32_t)placement->lineIndex);
        length = RoboTextWriterText(scratch, sizeof(scratch), length, " src ");
        length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, source.start);
        length = RoboTextWriterText(scratch, sizeof(scratch), length, "..");
        length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, source.end);
        length = RoboTextWriterText(scratch, sizeof(scratch), length, " ");
        length = RoboTextWriterText(scratch, sizeof(scratch), length,
                                    RoboTextWritingModeName(placement->writingMode));
        length = RoboTextWriterText(scratch, sizeof(scratch), length,
                                    PlacementIsReversed(placement) ? " reversed" : " forward");
        length = RoboTextWriterText(scratch, sizeof(scratch), length, " origin (");
        length = RoboTextWriterFloat(scratch, sizeof(scratch), length, placement->origin.x);
        length = RoboTextWriterText(scratch, sizeof(scratch), length, ", ");
        length = RoboTextWriterFloat(scratch, sizeof(scratch), length, placement->origin.y);
        length = RoboTextWriterText(scratch, sizeof(scratch), length, ") band ");
        length = RoboTextWriterFloat(scratch, sizeof(scratch), length, low);
        length = RoboTextWriterText(scratch, sizeof(scratch), length, "..");
        length = RoboTextWriterFloat(scratch, sizeof(scratch), length, high);
        length = RoboTextWriterText(scratch, sizeof(scratch), length, " inline ");
        length = RoboTextWriterFloat(scratch, sizeof(scratch), length, RunInlineTotal(run));
        if (length == RoboTextWriterOverflow) {
            return RoboNotSupported;
        }

        if (run) {
            size_t cluster;

            for (cluster = 0u; cluster < RoboTextGlyphRunClusterCount(run); cluster++) {
                const RoboTextGlyphCluster *entry =
                    RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)cluster);

                if (!entry) {
                    continue;
                }
                length = RoboTextWriterText(scratch, sizeof(scratch), length, "\n    cluster ");
                length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, entry->source.start);
                length = RoboTextWriterText(scratch, sizeof(scratch), length, "..");
                length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, entry->source.end);
                length = RoboTextWriterText(scratch, sizeof(scratch), length, " glyphs ");
                length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, entry->glyphStart);
                length = RoboTextWriterText(scratch, sizeof(scratch), length, "..");
                length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, entry->glyphEnd);
                if (length == RoboTextWriterOverflow) {
                    return RoboNotSupported;
                }
            }
        }
    }

    return SelectionPublish(scratch, length, buffer, capacity, outLength);
}

/* ------------------------------------------------------------------ */
/* Fuzzing                                                            */
/* ------------------------------------------------------------------ */

/**
 * The invariant every fuzz iteration checks, so that "no crash" is not the only thing asserted.
 *
 * A position the map answered with must lie inside the text it maps, and a distance must be a real number: a
 * hit test that named offset 4000 of a three-scalar text would not crash and would still be wrong, which is
 * exactly the kind of defect a fuzz entry that only watched for signals would miss.
 */
static int FuzzPositionIsSane(RoboTextPosition position, RoboTextScalarIndex scalars)
{
    return position.offset <= scalars;
}

Result RoboTextSelectionFuzz(const uint8_t *data, size_t size)
{
    RoboTextSelectionOptions options;
    RoboTextSelection selection;
    RoboTextCaretGeometry caret;
    RoboTextSelectionGeometry geometry;
    RoboTextSelectionHit hit;
    RoboTextSelectionMap *map = NULL;
    RoboTextSelectionPlacement *placements = NULL;
    RoboTextFontContext font;
    RoboTextShapeOptions shapeOptions;
    RoboTextFontStyle style;
    RoboTextView view;
    RoboTextShapeResult *result = NULL;
    const RoboTextGlyphRunList *list;
    RoboTextDiag diag;
    RoboTextScalarIndex scalars = 0u;
    size_t runCount;
    size_t index;
    int status = 1;

    if (!data && size > 0u) {
        return RoboInvalidArgument;
    }
    if (size == 0u) {
        return RoboOk;
    }

    /*
     * The bytes become text; the text is shaped with **no face**, which is a supported context rather than a
     * degenerate one (the run set still segments, the glyph run still generates and the clusters still tile),
     * and then every door in this header is asked with coordinates the bytes chose. Nothing here may crash,
     * hang, or answer with a position outside the text.
     */
    view.bytes = (const char *)data;
    view.length = size;
    RoboTextFontContextInit(&font);
    RoboTextFontStyleInit(&style);
    RoboTextShapeOptionsInit(&shapeOptions);
    if (size > 1u && (data[1] & 0x1u) != 0u) {
        shapeOptions.writingMode = RoboTextWritingVerticalRightToLeft;
    }
    RoboTextSelectionOptionsInit(&options);
    options.caretThickness = 1.0f + (float)(data[0] & 0x7u);
    options.includeTrailingWhitespace = (data[0] & 0x8u) != 0u ? 1 : 0;
    options.preferNearestLine = (data[0] & 0x10u) != 0u ? 1 : 0;

    if (size > 2u && (data[2] & 0x1u) != 0u) {
        /* The map is exercised over the shaped list only when a configuration says so: another iteration
         * covers the vocabulary doors alone, which is the half that needs no shaping at all. */
        status = 0;
    }
    if (status) {
        memset(&diag, 0, sizeof(diag));
        (void)RoboTextShape(view, &font, &shapeOptions, &result, &diag);
        list = result ? RoboTextShapeResultGlyphRunList(result) : NULL;
        runCount = RoboTextGlyphRunListCount(list);
        if (runCount > 0u) {
            placements = (RoboTextSelectionPlacement *)RoboCalloc(runCount, sizeof(*placements));
        }
        if (placements) {
            for (index = 0u; index < runCount; index++) {
                RoboTextSelectionPlacementInit(&placements[index]);
                placements[index].runIndex = index;
                placements[index].lineIndex = index;
                placements[index].origin = RoboTextSelectionPointMake((float)(index * 32u),
                                                                     (float)(data[0] & 0x1Fu));
                if ((data[1] & 0x2u) != 0u) {
                    placements[index].flags |= RoboTextSelectionPlacementReversed;
                }
            }
        }
        if (RoboTextSelectionMapCreate(list, placements, runCount, &options, &map, &diag) == RoboOk && map) {
            scalars = RoboTextSelectionMapSourceRange(map).end;
            for (index = 0u; index + 3u <= size; index += 3u) {
                RoboTextSelectionPoint point;
                RoboTextPosition position;

                point = RoboTextSelectionPointMake((float)(uint8_t)data[index],
                                                  (float)(uint8_t)data[index + 1u]);
                position = RoboTextPositionMake(((RoboTextScalarIndex)(uint8_t)data[index + 2u] * 7u) %
                                                (scalars + 1u));
                memset(&hit, 0, sizeof(hit));
                if (RoboTextSelectionMapHitTest(map, point, &hit, &diag) == RoboOk) {
                    if (!FuzzPositionIsSane(hit.position, scalars) || hit.distance != hit.distance) {
                        RoboTextSelectionMapDestroy(map);
                        RoboFree(placements);
                        RoboTextShapeResultDestroy(result);
                        return RoboError;
                    }
                }
                RoboTextCaretGeometryInit(&caret);
                (void)RoboTextSelectionMapCaret(map, position,
                                                (data[index] & 0x1u) ? RoboTextAffinityUpstream
                                                                     : RoboTextAffinityDownstream,
                                                &caret, &diag);
                RoboTextSelectionGeometryInit(&geometry);
                selection = RoboTextSelectionMake(position,
                                                  RoboTextPositionMake(position.offset + (data[index] & 0x3u)));
                (void)RoboTextSelectionMapGeometry(map, selection, &geometry, &diag);
            }
            (void)RoboTextSelectionMapValidate(map, NULL, NULL);
        }
    }

    /* The vocabulary doors, which are the half that needs no text at all. */
    (void)RoboTextSelectionWrite(RoboTextSelectionMake(RoboTextPositionMake(0u),
                                                      RoboTextPositionMake((RoboTextScalarIndex)size)),
                                 NULL, 0u, NULL);
    (void)RoboTextPositionWrite(RoboTextPositionMake(0u), (RoboTextScalarIndex)size, NULL, 0u, NULL);
    /*
     * NULL with a non-zero size is the one combination this entry refuses, and it refuses it **before** it
     * touches the bytes — which is also why checking it here cannot recurse: the call returns on its second
     * statement.
     */
    if (RoboTextSelectionFuzz(NULL, 1u) != RoboInvalidArgument) {
        RoboTextSelectionMapDestroy(map);
        RoboFree(placements);
        RoboTextShapeResultDestroy(result);
        return RoboError;
    }

    RoboTextSelectionMapDestroy(map);
    RoboFree(placements);
    RoboTextShapeResultDestroy(result);
    return RoboOk;
}
