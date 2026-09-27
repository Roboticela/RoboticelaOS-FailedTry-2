/*
 * RoboLayout grid layout (Phase 19.4) — see RoboLayoutGrid.h and GridLayout.md.
 *
 * One pass, and it is the fourth formatting context this build lays out. The block
 * pass reaches it exactly as it reaches the other three: a box whose *owned* context
 * is `RoboLayoutFcGrid` hands its contents to `LayoutGridWalkContents` instead of
 * stacking them (RoboLayoutBlock.c), and this file places every in-flow child as a
 * grid item -- writing each item's result, its four box-model records, its overflow
 * and its children.
 *
 * The algorithm, in the order CSS Grid Layout states it, which is the whole of the
 * implementation:
 *
 *   1. read the container's declarations -- the two track lists, the two auto-track
 *      lists, the flow, the two gutters and the four alignment properties;
 *   2. collect the items (in-flow boxes only) and resolve each one's placement from
 *      its four line longhands;
 *   3. **auto-place** the rest, in the order CSS states it: items definite on both
 *      axes, then items definite on the axis the flow advances *last*, then the rest
 *      in document order, against a **dense** or **sparse** cursor, creating implicit
 *      tracks as needed;
 *   4. size the **columns**: a declared breadth is its size, a content breadth is a
 *      floor from the items' min-content contributions and a ceiling from their
 *      max-content ones, a `<flex>` takes a share of what is left and the `auto`
 *      tracks then grow into what still is;
 *   5. lay each item's contents out at the inline size its area gave it -- this is
 *      the step that makes a grid a grid, because a row's size is a function of the
 *      *block* contributions of contents that do not exist until the columns are
 *      sized, and it is why the two axes are resolved in this order rather than
 *      together;
 *   6. size the **rows** from those contributions, the same way, and raise a
 *      baseline-aligned row to the deepest baseline it holds plus the deepest
 *      descent below it;
 *   7. place the tracks (`justify-content`/`align-content` distributing what is free
 *      in a definite container), then each item inside its area;
 *   8. resolve each item's own box -- stretch, baseline, `auto` margins -- and write
 *      it.
 *
 * The two axes are *not* a flag, unlike the flex pass's main/cross pair, and the
 * reason is that they are not symmetric: the columns are sized before any item has
 * contents and the rows after, so the two are two loops over the same shape rather
 * than one loop parameterised. What *is* shared is the track model (`LayoutGridTrack`
 * and the two functions that size a track set), which is what keeps a row and a column
 * from disagreeing about what `fr` or `auto` mean.
 *
 * The **auto-placement cursor** is the one place the two flow axes have to be named
 * explicitly, and it is named once: a placement advances along the axis the flow
 * *does not* name (its "inner" axis, which is the column under `row` and the row under
 * `column`), wrapping to the next step of the other when it runs out of room. That is
 * one parameterisation of both spellings rather than two copies of the algorithm.
 *
 * What this file deliberately does not do is named in RoboLayoutGrid.h and in
 * `Documentation/Runtime/Layout/GridLayout.md` §7: no `justify-items`/`justify-self`,
 * no named lines, no `grid-template-areas`, no `subgrid`, no `auto-fill` repeat, no
 * shorthand expansion, no masonry, no `order`-modified placement order and no vertical
 * writing mode.
 */

#include "RoboLayout/RoboLayoutGrid.h"

#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSDisplay.h"
#include "RoboCSS/RoboCSSFlex.h"
#include "RoboCSS/RoboCSSGrid.h"
#include "RoboCSS/RoboCSSGridTemplate.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSUnit.h"

#include "RoboLayout/RoboLayoutBaseline.h"
#include "RoboLayout/RoboLayoutBorder.h"
#include "RoboLayout/RoboLayoutFormattingContext.h"
#include "RoboLayout/RoboLayoutIntrinsic.h"
#include "RoboLayout/RoboLayoutMargin.h"
#include "RoboLayout/RoboLayoutObject.h"
#include "RoboLayout/RoboLayoutOverflow.h"
#include "RoboLayout/RoboLayoutPadding.h"
#include "RoboLayout/RoboLayoutPosition.h"
#include "RoboLayout/RoboLayoutResult.h"
#include "RoboLayout/RoboLayoutSizing.h"

#include "RoboLayoutInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Bounds                                                              */
/* ------------------------------------------------------------------ */

/**
 * Items one grid's placement handles without allocating.
 *
 * A fixed cap rather than a growing arena, for the reason the flex pass gives: a grid
 * container is a *page's* set of children, and a document with more than a few hundred
 * of them is an authoring mistake rather than a case to optimise. A child beyond the
 * cap is counted as skipped (`grid.itemsSkipped`) rather than dropped silently.
 */
#define LAYOUT_GRID_MAX_ITEMS 128u

/**
 * Tracks one axis may have, explicit and implicit together.
 *
 * It bounds two things at once: the track arrays this pass holds on its stack, and the
 * **occupancy** bitset, which is one 64-bit word per column with one bit per row -- so
 * 64 tracks an axis is 512 bytes and one word of bits. A placement past the bound is
 * **counted and skipped** rather than clamped into a track that is not the one the
 * author named, and the bound is stated here, in `GridLayout.md` and in the capability
 * claim rather than discovered.
 */
#define LAYOUT_GRID_MAX_TRACKS 64u

/** The largest content-box size a resolved track may reach. */
#define LAYOUT_GRID_SIZE_MAX ((int32_t)LAYOUT_STYLE_LENGTH_MAX)

/* ------------------------------------------------------------------ */
/* The counters record                                                 */
/* ------------------------------------------------------------------ */

void LayoutGridCountersInit(LayoutGridCounters *counters)
{
    if (!counters) {
        return;
    }
    memset(counters, 0, sizeof(*counters));
}

int LayoutGridCountersEqual(const LayoutGridCounters *a,
                            const LayoutGridCounters *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->containers == b->containers &&
           a->itemsPlaced == b->itemsPlaced &&
           a->itemsSkipped == b->itemsSkipped &&
           a->columns == b->columns &&
           a->rows == b->rows &&
           a->implicitTracks == b->implicitTracks &&
           a->placedItems == b->placedItems &&
           a->autoPlaced == b->autoPlaced &&
           a->denseItems == b->denseItems &&
           a->spanningItems == b->spanningItems &&
           a->frTracks == b->frTracks &&
           a->contentTracks == b->contentTracks &&
           a->stretched == b->stretched &&
           a->autoMargins == b->autoMargins &&
           a->baselineItems == b->baselineItems &&
           a->reversed == b->reversed &&
           a->overflowingItems == b->overflowingItems;
}

/* ------------------------------------------------------------------ */
/* Reading the container's declarations                                */
/* ------------------------------------------------------------------ */

static const CssComputedValue *LayoutGridProperty(const CssComputedStyle *style,
                                                 const char *property)
{
    if (!style) {
        return NULL;
    }
    return CssComputedStyleComputedByName(style, CssSliceFromText(property));
}

static CssFlexAlign LayoutGridAlignOf(const CssComputedStyle *style,
                                      const char *property,
                                      CssFlexAlign fallback)
{
    const CssComputedValue *value = LayoutGridProperty(style, property);

    if (!value || value->kind != RoboCssComputedValueFlexAlign) {
        return fallback;
    }
    return CssComputedValueFlexAlign(value);
}

/**
 * Whether the container's inline axis runs right-to-left (`direction: rtl`).
 *
 * Read from the same computed keyword the text layer and the flex pass read, so a
 * container's column order and its text's direction can never disagree: one property,
 * one door. An absent or unknown value answers left-to-right, which is the property's
 * initial value.
 */
static int LayoutGridDirectionIsRtl(const CssComputedStyle *style)
{
    const CssComputedValue *value = LayoutGridProperty(style, "direction");

    if (!value || value->kind != RoboCssComputedValueKeyword) {
        return 0;
    }
    return CssSliceEqualsAsciiCi(CssComputedValueText(value), "rtl");
}

/**
 * One gutter, in px.
 *
 * The gutters are a length family (18.4 reads them through the shared length grammar),
 * so this is a *resolution* and not a parse: a px length is the length, a percentage is
 * a share of the container's content box on that axis, and `normal` -- and anything
 * this build cannot resolve -- is **zero**, which is CSS's own rule for a grid
 * container rather than a shrug. (For a multi-column container `normal` is `1em`; a
 * grid has no ordinary gutter, which is a statement about a grid and not about a
 * gutter, which is why it is answered here rather than in the length grammar.)
 */
static int32_t LayoutGridGapPx(const CssComputedStyle *style,
                               const char *property,
                               int32_t percentageBase,
                               int hasPercentageBase)
{
    const CssComputedValue *value = LayoutGridProperty(style, property);

    if (!value) {
        return 0;
    }
    if (value->kind == RoboCssComputedValueLength) {
        double px = 0.0;

        if (CssUnitResolvePx(value->number, value->unit, NULL, &px) ==
            RoboCssLengthResolved) {
            return px < 0.0 ? 0 : LayoutPxRound(px);
        }
        return 0;
    }
    if (value->kind == RoboCssComputedValuePercentage) {
        int32_t px;

        if (!hasPercentageBase) {
            return 0;
        }
        px = LayoutPxRound(value->number / 100.0 * (double)percentageBase);
        return px < 0 ? 0 : px;
    }
    /* `normal`, or a declaration this build cannot resolve. */
    return 0;
}

/** A track size of `auto`, which is both auto-lists' initial value. */
static CssGridTrackSize LayoutGridAutoTrackSize(void)
{
    CssGridTrackSize size;

    CssGridTrackSizeInit(&size);
    return size;
}

/**
 * Read one track list out of a computed style.
 *
 * `grid-template-columns` and its three siblings are **carried** declarations (18.4),
 * so the computed value holds the specified component tree verbatim and the reading is
 * RoboCSS's own (`CssGridTrackListFromValue`) -- this layer parses nothing.
 *
 * A value this build cannot read degrades to the property's **initial value** rather
 * than failing: an unreadable template means no explicit tracks, so every item is
 * auto-placed into implicit tracks sized by `grid-auto-*`. That is a layout an author
 * would recognise as wrong in the way their declaration is unsupported, which is what
 * an unsupported declaration should look like.
 */
static void LayoutGridReadTrackList(const CssComputedStyle *style,
                                    const char *property,
                                    int noneAllowed,
                                    CssGridTrackList *out)
{
    const CssComputedValue *value;

    CssGridTrackListInit(out);
    value = LayoutGridProperty(style, property);
    if (!value) {
        return;
    }
    if (value->kind == RoboCssComputedValueKeyword) {
        /* The initial value, materialised as a keyword by the registry: `none` for a
         * template (no explicit tracks) and `auto` for an auto-list (one auto track). */
        if (CssSliceEqualsAsciiCi(CssComputedValueText(value), "auto")) {
            out->count = 1u;
            out->tracks[0] = LayoutGridAutoTrackSize();
            return;
        }
        return;
    }
    if (value->kind != RoboCssComputedValuePreserved) {
        return;
    }
    (void)CssGridTrackListFromValue(CssComputedValuePreserved(value),
                                   noneAllowed, out);
}

/** Read one line placement out of a computed style; `auto` when nothing is read. */
static CssGridLinePlacement LayoutGridReadLine(const CssComputedStyle *style,
                                               const char *property)
{
    CssGridLinePlacement placement;
    const CssComputedValue *value;

    CssGridLinePlacementInit(&placement);
    value = LayoutGridProperty(style, property);
    if (!value) {
        return placement;
    }
    /* `auto` is the initial value and the only keyword either property takes, so a
     * materialised keyword is the placement this record already holds. */
    if (value->kind != RoboCssComputedValuePreserved) {
        return placement;
    }
    (void)CssGridLinePlacementFromValue(CssComputedValuePreserved(value),
                                        &placement);
    return placement;
}

/** Everything the container's own style says, read once. */
typedef struct LayoutGridDeclarations {
    CssGridTrackList columns;
    CssGridTrackList rows;
    CssGridTrackList autoColumns;
    CssGridTrackList autoRows;
    CssFlexAlign justify;
    CssFlexAlign alignContent;
    CssFlexAlign alignItems;
    int flowColumn;
    int dense;
    int rtl;
    int32_t columnGap;
    int32_t rowGap;
} LayoutGridDeclarations;

static void LayoutGridReadDeclarations(const CssComputedStyle *style,
                                       int32_t contentWidth,
                                       int hasDefiniteHeight,
                                       int32_t definiteHeight,
                                       LayoutGridDeclarations *out)
{
    const CssComputedValue *flowValue;
    CssGridAutoFlow flow = RoboCssGridAutoFlowRow;

    memset(out, 0, sizeof(*out));
    flowValue = LayoutGridProperty(style, "grid-auto-flow");
    if (flowValue && flowValue->kind == RoboCssComputedValueGridAutoFlow) {
        flow = CssComputedValueGridAutoFlow(flowValue);
    }
    out->flowColumn = CssGridAutoFlowIsColumn(flow);
    out->dense = CssGridAutoFlowIsDense(flow);
    out->rtl = LayoutGridDirectionIsRtl(style);
    out->justify = LayoutGridAlignOf(style, "justify-content",
                                     RoboCssFlexAlignNormal);
    out->alignContent = LayoutGridAlignOf(style, "align-content",
                                          RoboCssFlexAlignNormal);
    out->alignItems = LayoutGridAlignOf(style, "align-items",
                                        RoboCssFlexAlignNormal);
    out->columnGap = LayoutGridGapPx(style, "column-gap", contentWidth, 1);
    out->rowGap = LayoutGridGapPx(style, "row-gap",
                                  hasDefiniteHeight ? definiteHeight : 0,
                                  hasDefiniteHeight ? 1 : 0);
    LayoutGridReadTrackList(style, "grid-template-columns", 1, &out->columns);
    LayoutGridReadTrackList(style, "grid-template-rows", 1, &out->rows);
    LayoutGridReadTrackList(style, "grid-auto-columns", 0, &out->autoColumns);
    LayoutGridReadTrackList(style, "grid-auto-rows", 0, &out->autoRows);
}

/* ------------------------------------------------------------------ */
/* Tracks and items                                                    */
/* ------------------------------------------------------------------ */

/**
 * One track, as the sizing algorithm works in it.
 *
 * `base` is the floor the track may not go below, `limit` is the ceiling it may grow
 * to, and `size` is what it was used at -- which differs from both once the maximizing
 * step or a `<flex>` share has run. `fraction` is a field rather than a kind because
 * `minmax(40px, 1fr)` has both a declared floor and a share of the free space.
 *
 * The two `*FromContent` flags are what makes the two halves of the sizing algorithm
 * fall in the right places, and they are two rather than one because a track can have
 * either without the other: `minmax(40px, max-content)` has a **definite base** and a
 * content-derived limit, and `minmax(auto, 200px)` the other way round. A track whose
 * base is definite must not be raised by an item's min-content contribution, and one
 * whose limit is a declared length must not be raised by its max-content contribution
 * -- so "which half is a content measurement" is asked per half.
 */
typedef struct LayoutGridTrack {
    int32_t base;
    int32_t limit;
    int32_t size;
    double fraction;
    int isFraction;
    /** The base is a floor from the items' min-content contributions. */
    int baseFromContent;
    /** The limit is a ceiling from the items' max-content contributions. */
    int limitFromContent;
    /** Either half is content-sized; what the counter and the split ask. */
    int isContentSized;
    int isAuto;
} LayoutGridTrack;

/** One grid item, from its placement to the geometry written for it. */
typedef struct LayoutGridItem {
    LayoutNodeId id;

    /* Placement, as track index ranges [start, end). */
    size_t columnStart;
    size_t columnEnd;
    size_t rowStart;
    size_t rowEnd;
    int columnDefinite;
    int rowDefinite;

    /* The item's own box. */
    int32_t contentW;
    int32_t contentH;
    int32_t blockHypothetical;
    int autoHeight;
    int32_t inlineInsets;
    int32_t blockInsets;
    int32_t positionX;
    int32_t positionY;

    /* Alignment, resolved once so placement and writing cannot disagree. */
    CssFlexAlign align;
    int baselineAligned;
    int32_t baseline;
    int baselineFromChild;

    /* `auto` margins, mapped onto the two axes' flow-relative sides. */
    int autoInlineBefore;
    int autoInlineAfter;
    int autoBlockBefore;
    int autoBlockAfter;
    int autoAbsorbed;
    int stretched;
    int overflowed;

    LayoutMarginEdges margins;
    LayoutPaddingEdges padding;
    LayoutBorderEdges borders;
} LayoutGridItem;

/**
 * The content-box length a declared dimension asks for, or -1 when it asks for none
 * this build can use (`auto`, a percentage with no base, an unusable unit).
 */
static int32_t LayoutGridDeclared(const LayoutDimension *dimension,
                                  int32_t base,
                                  int hasBase)
{
    if (dimension->kind == RoboLayoutDimensionDefinite) {
        return dimension->length;
    }
    if (dimension->kind == RoboLayoutDimensionPercentage && hasBase) {
        int32_t value = LayoutPxRound(LayoutDimensionPercentageOf(dimension) /
                                      100.0 * (double)base);

        return value < 0 ? 0 : value;
    }
    return -1;
}

/** A track breadth's definite size, or 0 with `*outDefinite` clear. */
static int32_t LayoutGridBreadthPx(const CssGridTrackBreadth *breadth,
                                   int32_t percentageBase,
                                   int hasPercentageBase,
                                   int *outDefinite)
{
    if (outDefinite) {
        *outDefinite = 0;
    }
    if (!breadth) {
        return 0;
    }
    if (breadth->kind == RoboCssGridBreadthLength) {
        double px = 0.0;

        if (CssUnitResolvePx(breadth->numeric.number, breadth->numeric.unit,
                             NULL, &px) == RoboCssLengthResolved) {
            if (outDefinite) {
                *outDefinite = 1;
            }
            return px < 0.0 ? 0 : LayoutPxRound(px);
        }
        return 0;
    }
    if (breadth->kind == RoboCssGridBreadthPercentage) {
        int32_t px;

        if (!hasPercentageBase) {
            return 0;
        }
        px = LayoutPxRound(breadth->numeric.number / 100.0 *
                           (double)percentageBase);
        if (outDefinite) {
            *outDefinite = 1;
        }
        return px < 0 ? 0 : px;
    }
    return 0;
}

/** The `fit-content()` limit in px, or -1 when the breadth is not one. */
static int32_t LayoutGridBreadthLimitPx(const CssGridTrackBreadth *breadth,
                                        int32_t percentageBase,
                                        int hasPercentageBase)
{
    CssGridTrackBreadth limit;
    int definite = 0;
    int32_t px;

    if (!breadth || breadth->kind != RoboCssGridBreadthFitContent) {
        return -1;
    }
    /*
     * The limit is a `<length-percentage>`, so it is resolved through the same door a
     * breadth is -- with the breadth's kind swapped for the kind its parsed numeric
     * actually has. Reusing the door is the point: a `fit-content(30%)` and a `30%`
     * track must not be normalised by two different pieces of arithmetic.
     */
    limit = *breadth;
    limit.kind = breadth->numeric.kind == RoboCssNumericPercentage
                     ? RoboCssGridBreadthPercentage
                     : RoboCssGridBreadthLength;
    px = LayoutGridBreadthPx(&limit, percentageBase, hasPercentageBase,
                             &definite);
    return definite ? px : -1;
}

/**
 * The track size the container's declaration asks for at @p index.
 *
 * An **explicit** track takes its template's entry; an **implicit** one cycles the
 * auto-track list, which is how CSS defines the two -- and the cycle is what makes
 * `grid-auto-columns: 40px 80px` alternate rather than repeat its first entry. An empty
 * auto-track list is `auto`, the property's initial value.
 */
static CssGridTrackSize LayoutGridTrackSizeAt(const CssGridTrackList *explicit,
                                              const CssGridTrackList *autoTracks,
                                              size_t index)
{
    if (!explicit->none && index < explicit->count) {
        return explicit->tracks[index];
    }
    if (autoTracks && autoTracks->count > 0u) {
        return autoTracks->tracks[index % autoTracks->count];
    }
    return LayoutGridAutoTrackSize();
}

/**
 * Initialise one track from the declaration its container gives for @p index.
 *
 * The two halves of `minmax()` are read as two facts and not folded, because they are
 * two: a **declared** minimum is the base, a **content** minimum starts at zero and is
 * raised by the items that span the track, and the maximum is either a declared ceiling,
 * a content one, or a **share of the free space** -- which is the one breadth that makes
 * the track get its size from the container rather than from its contents.
 *
 * A content **ceiling** starts at *zero* rather than at the geometry's maximum, and that
 * is the difference between a limit and a shrug: the ceiling is the items' max-content
 * contribution, and a limit nothing can reach would let the maximizing step grow an
 * `auto` track into every pixel of free space -- which is the `stretch` behaviour, one
 * step later, and not what `auto` means beside a `justify-content: start`.
 */
static void LayoutGridTrackInit(LayoutGridTrack *track,
                                const CssGridTrackSize *declared,
                                int32_t percentageBase,
                                int hasPercentageBase,
                                int *outContentTrack)
{
    int definite = 0;
    int32_t px;

    memset(track, 0, sizeof(*track));
    if (outContentTrack) {
        *outContentTrack = 0;
    }

    px = LayoutGridBreadthPx(&declared->minimum, percentageBase,
                             hasPercentageBase, &definite);
    if (definite) {
        track->base = px;
    } else {
        track->base = 0;
        track->baseFromContent = 1;
    }

    if (declared->maximum.kind == RoboCssGridBreadthFraction) {
        track->isFraction = 1;
        track->fraction = declared->maximum.fraction;
        track->limit = track->base;
    } else if (declared->maximum.kind == RoboCssGridBreadthMinContent) {
        /* `min-content` as a *maximum* is a ceiling at the content's own floor, which is
         * the base -- so the track can be raised by items' min-content contributions and
         * never by their max-content ones. */
        track->limit = track->base;
    } else if (declared->maximum.kind == RoboCssGridBreadthAuto ||
               declared->maximum.kind == RoboCssGridBreadthMaxContent) {
        track->limit = 0;
        track->limitFromContent = 1;
    } else if (declared->maximum.kind == RoboCssGridBreadthFitContent) {
        int32_t fitLimit = LayoutGridBreadthLimitPx(&declared->maximum,
                                                    percentageBase,
                                                    hasPercentageBase);

        if (fitLimit >= 0) {
            /* `fit-content(<length>)`: the declared limit *is* the ceiling. */
            track->limit = fitLimit < track->base ? track->base : fitLimit;
        } else {
            /* An indefinite limit is `max-content`: CSS's own answer. */
            track->limit = 0;
            track->limitFromContent = 1;
        }
    } else {
        int maxDefinite = 0;

        px = LayoutGridBreadthPx(&declared->maximum, percentageBase,
                                 hasPercentageBase, &maxDefinite);
        track->limit = maxDefinite ? (px < track->base ? track->base : px)
                                   : track->base;
    }
    /*
     * A **content** ceiling is raised by the items rather than floored here, and the
     * raised value cannot be below the base -- a max-content contribution is never
     * smaller than the min-content one it was measured over the same content -- so the
     * two agree by construction rather than by a clamp that would have to be repeated.
     */
    if (!track->limitFromContent && track->limit < track->base) {
        track->limit = track->base;
    }
    track->size = track->base;
    track->isAuto = declared->maximum.kind == RoboCssGridBreadthAuto;
    track->isContentSized = track->baseFromContent || track->limitFromContent;
    if (outContentTrack) {
        *outContentTrack = track->isContentSized;
    }
}

/**
 * Raise the intrinsic tracks @p item spans by the contribution it makes.
 *
 * An item spanning **one** intrinsic track contributes its whole content size to that
 * track, which is CSS's "span 1" contribution and the case an authored grid is nearly
 * always in. An item spanning several is the case CSS's planned-increase algorithm
 * exists for, and this build states its simplification rather than pretending to the
 * full arithmetic: the part of the contribution the definite tracks cannot hold is
 * divided **equally** among the intrinsic tracks the item spans (`GridLayout.md` §5).
 * That is exact for the common shapes -- an item spanning only intrinsic tracks, or
 * spanning a definite track beside an intrinsic one -- and it never *understates* the
 * room the item needs, which is the direction a content floor has to err in.
 */
static void LayoutGridDistributeContribution(LayoutGridTrack *tracks,
                                             const LayoutGridItem *item,
                                             int columns,
                                             int32_t contribution,
                                             int toLimit)
{
    size_t first = columns ? item->columnStart : item->rowStart;
    size_t last = columns ? item->columnEnd : item->rowEnd;
    size_t index;
    size_t participants = 0u;
    int32_t held = 0;
    int32_t extra;
    int32_t share;

    for (index = first; index < last; index++) {
        /*
         * Per half, and that is the whole reason the two flags exist: a definite minimum
         * is not raised by a min-content contribution, and a declared maximum is not
         * raised by a max-content one. What a track *already holds* on the half being
         * distributed is its base for the floor and its ceiling for the limit, so a
         * `minmax(40px, max-content)` beside a spanning item contributes its 40px to the
         * space the item's own width has to cover.
         */
        if (toLimit ? tracks[index].limitFromContent
                    : tracks[index].baseFromContent) {
            participants++;
        } else {
            held += toLimit ? (tracks[index].limit > tracks[index].base
                                   ? tracks[index].limit
                                   : tracks[index].base)
                            : tracks[index].base;
        }
    }
    if (participants == 0u) {
        /* Every track the item covers is definite on this half: the item overflows its
         * area, which is CSS's answer and is counted rather than clamped (step 4). */
        return;
    }
    extra = contribution - held;
    if (extra <= 0) {
        return;
    }
    share = (int32_t)((extra + (int32_t)participants - 1) /
                      (int32_t)participants);
    for (index = first; index < last; index++) {
        if (toLimit) {
            if (tracks[index].limitFromContent && share > tracks[index].limit) {
                tracks[index].limit = share;
            }
        } else if (tracks[index].baseFromContent && share > tracks[index].base) {
            tracks[index].base = share;
        }
    }
}

/** A track set's resolved sizes, so the two axes share one resolution. */
typedef struct LayoutGridTrackSet {
    LayoutGridTrack tracks[LAYOUT_GRID_MAX_TRACKS];
    size_t count;
    /** Free space left after sizing: what the content distribution spends. */
    int32_t free;
    /** Where the tracks start, and the extra gap `space-*` inserted. */
    int32_t start;
    int32_t gapExtra;
} LayoutGridTrackSet;

/**
 * Resolve a track set's used sizes: content floors and ceilings, `<flex>` shares, then
 * the `auto` tracks a `stretch` distribution grows into.
 *
 * The order is the algorithm's. A `<flex>` track takes its share of the space the other
 * tracks left; an `auto` track takes an equal share of what is *still* left, but only
 * under `normal`/`stretch`, because `justify-content: center` must leave that space for
 * the alignment step to distribute; and whatever remains unmatched is the alignment's,
 * which the caller reads out of `free`.
 */
static void LayoutGridTrackResolve(LayoutGridTrackSet *set,
                                   int32_t available,
                                   int32_t gapTotal,
                                   CssFlexAlign contentAlign)
{
    int32_t used = 0;
    double fractionTotal = 0.0;
    int32_t flexSpace;
    size_t index;
    int pass;

    for (index = 0u; index < set->count; index++) {
        if (set->tracks[index].limit < set->tracks[index].base) {
            set->tracks[index].limit = set->tracks[index].base;
        }
    }
    for (index = 0u; index < set->count; index++) {
        set->tracks[index].size = set->tracks[index].base;
        used += set->tracks[index].base;
        if (set->tracks[index].isFraction) {
            fractionTotal += set->tracks[index].fraction;
        }
    }

    flexSpace = available - gapTotal - used;
    /*
     * Two passes, and the second is not a refinement: a share below a track's own floor
     * leaves the floor in place, and the space that share did not use has to go to the
     * tracks that are still flexible -- otherwise a `minmax(400px, 1fr)` beside a `1fr`
     * in an 800px container would leave 200px unclaimed. One extra pass is enough
     * because the floors are fixed, so a second pass cannot newly clamp anything.
     */
    for (pass = 0; pass < 2 && fractionTotal > 0.0 && flexSpace > 0; pass++) {
        int32_t leftover = 0;
        double liveFraction = 0.0;

        for (index = 0u; index < set->count; index++) {
            if (set->tracks[index].isFraction &&
                set->tracks[index].size <= set->tracks[index].base) {
                liveFraction += set->tracks[index].fraction;
            }
        }
        if (liveFraction <= 0.0) {
            break;
        }
        for (index = 0u; index < set->count; index++) {
            LayoutGridTrack *track = &set->tracks[index];
            int32_t target;

            if (!track->isFraction) {
                continue;
            }
            if (track->size > track->base) {
                leftover += track->size - track->base;
                continue;
            }
            target = track->base +
                     LayoutPxRound((double)flexSpace * track->fraction /
                                   liveFraction);
            if (target > LAYOUT_GRID_SIZE_MAX) {
                target = LAYOUT_GRID_SIZE_MAX;
            }
            track->size = target < track->base ? track->base : target;
            leftover += track->size - track->base;
        }
        flexSpace -= leftover;
    }

    /*
     * **Maximize**: with free space beside them and content-sized tracks below their
     * ceilings, the tracks grow equally until the space is used or every ceiling is
     * reached. This is the step between the intrinsic sizing and the alignment, and it
     * is why an `auto` track with room beside it is as wide as its content *asks for*
     * rather than as narrow as its floor -- `justify-content: start` leaves the space
     * for the alignment, it does not leave the track at min-content.
     *
     * A `<flex>` track's ceiling is its base, so it never participates: its size came
     * from the previous step and is not a content measurement. A track with a definite
     * ceiling does participate, and stops there -- which is what a `minmax(40px, 200px)`
     * or a `fit-content(120px)` means.
     */
    for (pass = 0; pass <= (int)set->count; pass++) {
        int32_t free = available - gapTotal;
        size_t growable = 0u;
        int32_t share;

        for (index = 0u; index < set->count; index++) {
            free -= set->tracks[index].size;
            if (set->tracks[index].size < set->tracks[index].limit) {
                growable++;
            }
        }
        if (free <= 0 || growable == 0u) {
            break;
        }
        share = free / (int32_t)growable;
        if (share <= 0) {
            break;
        }
        for (index = 0u; index < set->count; index++) {
            LayoutGridTrack *track = &set->tracks[index];

            if (track->size >= track->limit) {
                continue;
            }
            track->size += share;
            if (track->size > track->limit) {
                track->size = track->limit;
            }
        }
    }

    /*
     * `normal` and `stretch`: the `auto` tracks grow into what is left, equally -- and
     * **past their ceilings**, which is what `stretch` means and the one way an `auto`
     * track differs from a `max-content` one. `justify-content: center` must instead
     * leave the space alone for the alignment step, which is why this is asked of the
     * content distribution rather than applied unconditionally.
     */
    if (contentAlign == RoboCssFlexAlignNormal ||
        contentAlign == RoboCssFlexAlignStretch) {
        int32_t free = available - gapTotal;
        size_t autoCount = 0u;

        for (index = 0u; index < set->count; index++) {
            free -= set->tracks[index].size;
            if (set->tracks[index].isAuto && !set->tracks[index].isFraction) {
                autoCount++;
            }
        }
        if (free > 0 && autoCount > 0u) {
            int32_t share = free / (int32_t)autoCount;

            for (index = 0u; index < set->count; index++) {
                if (set->tracks[index].isAuto &&
                    !set->tracks[index].isFraction) {
                    set->tracks[index].size += share;
                }
            }
        }
    }

    {
        int32_t total = gapTotal;

        for (index = 0u; index < set->count; index++) {
            total += set->tracks[index].size;
        }
        set->free = available - total;
    }
}

/**
 * Turn a resolved track set's free space into a starting offset and an extra gap, by
 * `justify-content` / `align-content`.
 *
 * `stretch`/`normal` have already spent the free space on the `auto` tracks, so
 * reaching them here means there were none -- and the honest answer then is the
 * content-distribution's own default, `start`.
 */
static void LayoutGridContentDistribute(LayoutGridTrackSet *set,
                                        CssFlexAlign align,
                                        int32_t count)
{
    int32_t free = set->free;

    set->start = 0;
    set->gapExtra = 0;
    if (free <= 0 || count <= 0) {
        return;
    }
    switch (align) {
    case RoboCssFlexAlignFlexEnd:
    case RoboCssFlexAlignEnd:
    case RoboCssFlexAlignRight:
        set->start = free;
        break;
    case RoboCssFlexAlignCenter:
        set->start = free / 2;
        break;
    case RoboCssFlexAlignSpaceBetween:
        if (count > 1) {
            set->gapExtra = free / (count - 1);
        }
        break;
    case RoboCssFlexAlignSpaceAround:
        set->gapExtra = free / count;
        set->start = set->gapExtra / 2;
        break;
    case RoboCssFlexAlignSpaceEvenly:
        set->gapExtra = free / (count + 1);
        set->start = set->gapExtra;
        break;
    default:
        /* `start`, `flex-start`, `normal` and `stretch` all leave it at the start. */
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Placement                                                           */
/* ------------------------------------------------------------------ */

/**
 * The track index a 1-based line number names.
 *
 * Positive numbers count from the start line (1 is index 0); negative ones count
 * backwards from the **end of the explicit grid**, so `-1` is the line *after* the last
 * explicit track -- CSS's own rule, and the one an author writing
 * `grid-column: 1 / -1` depends on. A line before the start line and one past the track
 * bound are both refused, which sends the axis to auto-placement rather than to a track
 * it did not name.
 */
static int LayoutGridLineToIndex(int32_t line, size_t explicitCount,
                                 size_t *out)
{
    int64_t index;

    if (line > 0) {
        index = (int64_t)line - 1;
    } else {
        index = (int64_t)explicitCount + 1 + (int64_t)line;
    }
    if (index < 0 || index >= (int64_t)LAYOUT_GRID_MAX_TRACKS) {
        return 0;
    }
    *out = (size_t)index;
    return 1;
}

/**
 * Resolve one axis of one item's placement from its two longhands.
 *
 * The nine combinations CSS defines, and the two that matter are a definite line and a
 * `span`: a definite line on either side pins the axis (a `span` on the other side gives
 * the extent), and two `auto`s leave it to the auto-placement cursor. A placement this
 * build cannot honour -- a line past the bound, an end at or before its start, a named
 * line, which the reader defers -- degrades to `auto` rather than failing, which is the
 * same degradation the track lists get and for the same reason.
 */
static void LayoutGridResolveAxis(const CssGridLinePlacement *start,
                                  const CssGridLinePlacement *end,
                                  size_t explicitCount,
                                  size_t *outStart,
                                  size_t *outEnd,
                                  int *outDefinite)
{
    size_t startIndex = 0u;
    size_t endIndex = 0u;
    size_t span = 1u;
    int haveStart = 0;
    int haveEnd = 0;

    *outDefinite = 0;
    *outStart = 0u;
    *outEnd = 1u;

    if (end->kind == RoboCssGridLineSpan && end->line > 1) {
        span = (size_t)end->line;
    }
    if (start->kind == RoboCssGridLineNumber) {
        haveStart = LayoutGridLineToIndex(start->line, explicitCount,
                                          &startIndex);
    } else if (start->kind == RoboCssGridLineSpan && start->line > 1) {
        span = (size_t)start->line;
    }
    if (end->kind == RoboCssGridLineNumber) {
        haveEnd = LayoutGridLineToIndex(end->line, explicitCount, &endIndex);
    }

    if (haveStart && haveEnd) {
        if (endIndex <= startIndex) {
            return; /* over-constrained: auto-placement is the honest answer */
        }
        *outStart = startIndex;
        *outEnd = endIndex;
        *outDefinite = 1;
        return;
    }
    if (haveStart) {
        if (startIndex + span > LAYOUT_GRID_MAX_TRACKS) {
            return;
        }
        *outStart = startIndex;
        *outEnd = startIndex + span;
        *outDefinite = 1;
        return;
    }
    if (haveEnd) {
        if (endIndex < span) {
            return;
        }
        *outStart = endIndex - span;
        *outEnd = endIndex;
        *outDefinite = 1;
    }
}

/** Whether the area [columnStart, columnEnd) x [rowStart, rowEnd) is free. */
static int LayoutGridAreaFits(const uint64_t *occupancy,
                              size_t columnStart,
                              size_t columnEnd,
                              size_t rowStart,
                              size_t rowEnd)
{
    size_t column;
    size_t row;

    for (column = columnStart; column < columnEnd; column++) {
        for (row = rowStart; row < rowEnd; row++) {
            if ((occupancy[column] & ((uint64_t)1u << row)) != 0u) {
                return 0;
            }
        }
    }
    return 1;
}

static void LayoutGridAreaMark(uint64_t *occupancy,
                               size_t columnStart,
                               size_t columnEnd,
                               size_t rowStart,
                               size_t rowEnd)
{
    size_t column;
    size_t row;

    for (column = columnStart; column < columnEnd; column++) {
        for (row = rowStart; row < rowEnd; row++) {
            occupancy[column] |= ((uint64_t)1u << row);
        }
    }
}

/**
 * Find the first free area of the given shape.
 *
 * Two axes, and the *outer* one is the axis the flow does **not** advance along: it is
 * the row under `grid-auto-flow: row` and the column under `column`, which is what makes
 * one search serve both spellings. The **inner** axis is the one a placement advances
 * along, so the cursor is an inner position and the outer one only moves when the inner
 * axis runs out of room.
 *
 * The inner axis is **bounded** by the tracks the grid already has on it
 * (@p innerTrackLimit: the explicit tracks of the flow's cross axis, extended by every
 * placement that named a line past them) and the outer axis grows instead. That bound is
 * the rule, not a search limit: CSS says an automatic placement never adds a track to the
 * axis it advances *across*, so a third item in a two-column `grid-auto-flow: row` grid
 * goes into a second **row** -- a search that let the inner axis grow would put it in an
 * implicit third column and lay the document out as one long line. An item whose span is
 * wider than that bound takes the span as the bound, which is CSS's own "the implicit grid
 * grows to hold a span" and the reason a `span 3` in a two-column grid is placed at all.
 *
 * The cursor's semantics are CSS's: a **dense** one starts at the origin for every item
 * and therefore backfills a hole an earlier item left, while a **sparse** one starts
 * where the previous placement ended and never looks back. That is the whole difference
 * between the two spellings, and it is a difference in *order* rather than in packing --
 * which is why a suite tests it by looking at which item landed where.
 *
 * A **fixed** axis (an item with a definite line on one axis) constrains the search to
 * that band rather than moving the cursor, which is CSS's rule step for a partially
 * placed item.
 */
static int LayoutGridFindArea(const uint64_t *occupancy,
                              int outerIsColumn,
                              size_t spanColumns,
                              size_t spanRows,
                              size_t outerCursor,
                              size_t innerCursor,
                              size_t innerTrackLimit,
                              int outerFixed,
                              size_t fixedOuter,
                              int innerFixed,
                              size_t fixedInner,
                              size_t *outColumn,
                              size_t *outRow)
{
    size_t outerSpan = outerIsColumn ? spanColumns : spanRows;
    size_t innerSpan = outerIsColumn ? spanRows : spanColumns;
    size_t innerSpanLimit = innerTrackLimit < innerSpan ? innerSpan
                                                        : innerTrackLimit;
    size_t outerFrom = outerFixed ? fixedOuter : outerCursor;
    size_t outerLimit = outerFixed ? outerFrom + 1u : LAYOUT_GRID_MAX_TRACKS;
    size_t outer;

    for (outer = outerFrom;
         outer < outerLimit && outer + outerSpan <= LAYOUT_GRID_MAX_TRACKS;
         outer++) {
        size_t innerFrom;
        size_t innerLimit;
        size_t inner;

        if (innerFixed) {
            innerFrom = fixedInner;
            innerLimit = innerFrom + 1u;
        } else {
            innerFrom = (outer == outerCursor) ? innerCursor : 0u;
            innerLimit = innerSpanLimit;
        }
        for (inner = innerFrom;
             inner < innerLimit && inner + innerSpan <= innerSpanLimit;
             inner++) {
            size_t column = outerIsColumn ? outer : inner;
            size_t row = outerIsColumn ? inner : outer;

            if (LayoutGridAreaFits(occupancy, column, column + spanColumns,
                                   row, row + spanRows)) {
                *outColumn = column;
                *outRow = row;
                return 1;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

/**
 * The item's **baseline**, as a distance along the block axis from its block-start
 * margin edge.
 *
 * The same rule the flex pass states and for the same reason: the item's baseline is
 * its first baseline-bearing **direct child**'s -- a run of text, whose font baseline
 * the inline pass placed -- and an item with no such child takes the documented
 * bottom-edge fallback. Grid adds nothing to the rule; what it adds is that *several*
 * items in a row share one baseline, which the row step computes.
 */
static int32_t LayoutGridItemBaseline(const LayoutWalk *walk,
                                      const LayoutGridItem *item,
                                      int *outFromChild)
{
    const LayoutObject *itemObject = LayoutObjectSlotConst(walk->tree, item->id);
    int32_t blockStart;
    LayoutNodeId child;

    if (outFromChild) {
        *outFromChild = 0;
    }
    if (!itemObject) {
        return item->blockInsets;
    }
    blockStart = item->margins.top + item->borders.top.width +
                 item->padding.top;

    child = itemObject->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree, child);

        if (!childObject) {
            break;
        }
        if (LayoutResultIsComputed(&childObject->result) &&
            LayoutBaselineKindIsUsable(childObject->baseline.kind)) {
            if (outFromChild) {
                *outFromChild = 1;
            }
            return blockStart + childObject->result.position.y +
                   childObject->baseline.ascent;
        }
        child = childObject->NextSibling;
    }
    return item->blockInsets;
}

Result LayoutGridWalkContents(LayoutWalk *walk,
                              LayoutNodeId owner,
                              int32_t contentWidth,
                              int hasDefiniteHeight,
                              GfxPixelY definiteHeight,
                              GfxPixelY *outExtent)
{
    const LayoutObject *container;
    LayoutGridDeclarations declarations;
    LayoutGridTrackSet columnSet;
    LayoutGridTrackSet rowSet;
    LayoutGridItem items[LAYOUT_GRID_MAX_ITEMS];
    uint64_t occupancy[LAYOUT_GRID_MAX_TRACKS];
    int32_t rowBaseline[LAYOUT_GRID_MAX_TRACKS];
    int32_t rowDescent[LAYOUT_GRID_MAX_TRACKS];
    size_t itemCount = 0u;
    size_t explicitColumns;
    size_t explicitRows;
    size_t gridColumns;
    size_t gridRows;
    size_t columns;
    size_t rows;
    size_t index;
    LayoutNodeId child;
    int32_t containerHeight = hasDefiniteHeight ? (int32_t)definiteHeight : 0;

    if (!walk || !walk->tree || !walk->block || !outExtent) {
        return RoboLayoutDiag(walk ? walk->diag : NULL, RoboLayoutDomainGrid,
                              RoboInvalidArgument,
                              "the grid pass needs a walk, a tree and an out");
    }
    if (contentWidth < 0) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainGrid,
                              RoboInvalidArgument, "a negative container width");
    }

    memset(&columnSet, 0, sizeof(columnSet));
    memset(&rowSet, 0, sizeof(rowSet));
    memset(occupancy, 0, sizeof(occupancy));
    memset(rowBaseline, 0, sizeof(rowBaseline));
    memset(rowDescent, 0, sizeof(rowDescent));

    container = LayoutObjectSlotConst(walk->tree, owner);
    if (!container) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainGrid, RoboError,
                              "the box that owns a grid context is not in the tree");
    }

    /* ---- 1. the container's declarations --------------------------- */

    LayoutGridReadDeclarations(container->style, contentWidth, hasDefiniteHeight,
                               containerHeight, &declarations);
    explicitColumns = declarations.columns.none ? 0u : declarations.columns.count;
    explicitRows = declarations.rows.none ? 0u : declarations.rows.count;

    walk->block->grid.containers++;
    if (declarations.rtl) {
        walk->block->grid.reversed++;
    }

    /* ---- 2. collect the items and read their placements ------------- */

    child = container->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree, child);

        if (!childObject) {
            return RoboLayoutDiag(walk->diag, RoboLayoutDomainGrid, RoboError,
                                  "a grid container's child link names no box");
        }
        {
            LayoutPosition position = LayoutPositionOf(childObject->style);

            /* An out-of-flow child is not a grid item: it is placed by the containing
             * block's own pass (14.0), exactly as it is in a block container. */
            if (LayoutAbsoluteIsOutOfFlow(&position)) {
                child = childObject->NextSibling;
                continue;
            }
        }
        /* A text run, or a box past the pass's ceiling, is counted rather than placed:
         * CSS wraps an inline run in an anonymous grid item and this build makes none.
         * It is counted in both records so the block pass's accounting identity still
         * closes (`RoboLayoutBlock.h`). */
        if (childObject->kind != RoboLayoutObjectBox ||
            itemCount >= LAYOUT_GRID_MAX_ITEMS) {
            walk->block->grid.itemsSkipped++;
            walk->block->inlineLevelSkipped++;
            child = childObject->NextSibling;
            continue;
        }
        {
            LayoutGridItem *item = &items[itemCount];
            CssGridLinePlacement columnStart;
            CssGridLinePlacement columnEnd;
            CssGridLinePlacement rowStart;
            CssGridLinePlacement rowEnd;
            LayoutMargins declaredMargins;
            CssBoxSide sides[RoboCssBoxSideCount] = {
                RoboCssBoxSideTop, RoboCssBoxSideRight,
                RoboCssBoxSideBottom, RoboCssBoxSideLeft
            };
            GfxPixelX used[RoboCssBoxSideCount] = { 0, 0, 0, 0 };
            uint32_t autoMask = 0u;
            size_t side;

            memset(item, 0, sizeof(*item));
            item->id = child;
            item->align = declarations.alignItems;
            {
                const CssComputedValue *self =
                    LayoutGridProperty(childObject->style, "align-self");

                if (self && self->kind == RoboCssComputedValueFlexAlign) {
                    CssFlexAlign align = CssComputedValueFlexAlign(self);

                    if (align != RoboCssFlexAlignAuto) {
                        item->align = align;
                    }
                }
            }

            columnStart = LayoutGridReadLine(childObject->style,
                                             "grid-column-start");
            columnEnd = LayoutGridReadLine(childObject->style, "grid-column-end");
            rowStart = LayoutGridReadLine(childObject->style, "grid-row-start");
            rowEnd = LayoutGridReadLine(childObject->style, "grid-row-end");
            LayoutGridResolveAxis(&columnStart, &columnEnd, explicitColumns,
                                  &item->columnStart, &item->columnEnd,
                                  &item->columnDefinite);
            LayoutGridResolveAxis(&rowStart, &rowEnd, explicitRows,
                                  &item->rowStart, &item->rowEnd,
                                  &item->rowDefinite);

            /*
             * The margins, with the `auto` ones mapped onto the two axes'
             * flow-relative sides once and for all -- the decision the flex pass makes
             * and for the same reason: every later step reads only the flow-relative
             * flags, so an RTL container cannot give an `auto` margin the wrong side. A
             * percentage resolves against the container's content **width**, which is
             * CSS's rule for every side of every box and the rule 19.2 gave a block box.
             */
            LayoutMarginsInit(&declaredMargins);
            declaredMargins = LayoutMarginsOf(childObject->style);
            memset(&item->margins, 0, sizeof(item->margins));
            item->margins.availableContentWidth = contentWidth;
            for (side = 0u; side < RoboCssBoxSideCount; side++) {
                LayoutMargin margin = LayoutMarginAbsent();

                if (LayoutMarginsSide(&declaredMargins, sides[side], &margin) !=
                    RoboOk) {
                    continue;
                }
                if (margin.kind == RoboLayoutMarginResolved) {
                    used[side] = margin.length;
                    item->margins.resolved++;
                } else if (margin.kind == RoboLayoutMarginPercentage) {
                    used[side] = LayoutPxRound(margin.percentage / 100.0 *
                                               (double)contentWidth);
                    item->margins.resolved++;
                } else if (margin.kind == RoboLayoutMarginUnresolved) {
                    item->margins.unresolved++;
                } else if (margin.kind == RoboLayoutMarginAuto) {
                    autoMask |= (uint32_t)1u << (uint32_t)sides[side];
                }
            }
            item->margins.top = used[RoboCssBoxSideTop];
            item->margins.right = used[RoboCssBoxSideRight];
            item->margins.bottom = used[RoboCssBoxSideBottom];
            item->margins.left = used[RoboCssBoxSideLeft];

            /* The inline axis's before side is the left edge, or the right one when
             * `direction: rtl` moved it -- the same rule the flex pass states for a
             * `row`, and the *only* directional fact in the mapping. */
            if (declarations.rtl) {
                item->autoInlineBefore =
                    (autoMask & ((uint32_t)1u << RoboCssBoxSideRight)) != 0u;
                item->autoInlineAfter =
                    (autoMask & ((uint32_t)1u << RoboCssBoxSideLeft)) != 0u;
            } else {
                item->autoInlineBefore =
                    (autoMask & ((uint32_t)1u << RoboCssBoxSideLeft)) != 0u;
                item->autoInlineAfter =
                    (autoMask & ((uint32_t)1u << RoboCssBoxSideRight)) != 0u;
            }
            item->autoBlockBefore =
                (autoMask & ((uint32_t)1u << RoboCssBoxSideTop)) != 0u;
            item->autoBlockAfter =
                (autoMask & ((uint32_t)1u << RoboCssBoxSideBottom)) != 0u;

            {
                LayoutPaddings declaredPadding =
                    LayoutPaddingsOf(childObject->style);
                LayoutBorders declaredBorders =
                    LayoutBordersOf(childObject->style);

                if (LayoutPaddingResolve(&declaredPadding, contentWidth,
                                         &item->padding, walk->diag) != RoboOk) {
                    return walk->diag ? walk->diag->code : RoboError;
                }
                if (LayoutBorderResolve(&declaredBorders, &item->borders,
                                        walk->diag) != RoboOk) {
                    return walk->diag ? walk->diag->code : RoboError;
                }
            }
            item->inlineInsets = item->margins.left + item->margins.right +
                                 item->borders.left.width +
                                 item->borders.right.width +
                                 item->padding.left + item->padding.right;
            item->blockInsets = item->margins.top + item->margins.bottom +
                                item->borders.top.width +
                                item->borders.bottom.width +
                                item->padding.top + item->padding.bottom;
            itemCount++;
        }
        child = childObject->NextSibling;
    }

    /* ---- 3. auto-placement ----------------------------------------- */

    /*
     * CSS's ordering, in three sweeps: an item definite on **both** axes is placed where
     * it says; then an item definite on the axis the flow advances *last* is placed in
     * that band; and then everything else follows the cursor in document order. The
     * ordering matters because a later item may occupy a slot an earlier auto item would
     * otherwise have taken, so a build that placed everything in source order would
     * produce a different grid for a document that states one axis and leaves the rest
     * to the flow.
     */
    {
        int sweep;
        size_t columnCursor = 0u;
        size_t rowCursor = 0u;

        for (index = 0u; index < itemCount; index++) {
            LayoutGridItem *item = &items[index];

            if (item->columnDefinite && item->rowDefinite) {
                LayoutGridAreaMark(occupancy, item->columnStart, item->columnEnd,
                                   item->rowStart, item->rowEnd);
                walk->block->grid.placedItems++;
            }
        }

        /*
         * The implicit grid's **count on each axis** before auto-placement: the explicit
         * tracks, extended by any placement that named a line past them. The axis the
         * flow advances along is bounded by this count and the other one grows, which is
         * what makes a two-column `grid-auto-flow: row` grid stack its third item into a
         * *second row* rather than into an implicit third column.
         */
        gridColumns = explicitColumns;
        gridRows = explicitRows;
        for (index = 0u; index < itemCount; index++) {
            LayoutGridItem *item = &items[index];

            if (item->id == RoboLayoutNodeIdNone) {
                continue;
            }
            if (item->columnDefinite && item->columnEnd > gridColumns) {
                gridColumns = item->columnEnd;
            }
            if (item->rowDefinite && item->rowEnd > gridRows) {
                gridRows = item->rowEnd;
            }
        }
        if (gridColumns > LAYOUT_GRID_MAX_TRACKS) {
            gridColumns = LAYOUT_GRID_MAX_TRACKS;
        }
        if (gridRows > LAYOUT_GRID_MAX_TRACKS) {
            gridRows = LAYOUT_GRID_MAX_TRACKS;
        }

        for (sweep = 0; sweep < 2; sweep++) {
            columnCursor = 0u;
            rowCursor = 0u;
            for (index = 0u; index < itemCount; index++) {
                LayoutGridItem *item = &items[index];
                size_t spanColumns = item->columnEnd - item->columnStart;
                size_t spanRows = item->rowEnd - item->rowStart;
                /* The outer axis is the one the flow does not advance along. */
                int outerIsColumn = declarations.flowColumn;
                int outerFixed = outerIsColumn ? item->columnDefinite
                                               : item->rowDefinite;
                int innerFixed = outerIsColumn ? item->rowDefinite
                                               : item->columnDefinite;
                size_t fixedOuter = outerIsColumn ? item->columnStart
                                                  : item->rowStart;
                size_t fixedInner = outerIsColumn ? item->rowStart
                                                  : item->columnStart;
                size_t outerCursor = outerIsColumn ? columnCursor : rowCursor;
                size_t innerCursor = outerIsColumn ? rowCursor : columnCursor;
                size_t column = 0u;
                size_t row = 0u;

                if (item->id == RoboLayoutNodeIdNone ||
                    (item->columnDefinite && item->rowDefinite)) {
                    continue;
                }
                /* Sweep 0 is the items with a definite position on the outer axis;
                 * sweep 1 everything left. An item definite only on the inner axis is
                 * placed in sweep 1, with that axis pinned. */
                if (sweep == 0 && !outerFixed) {
                    continue;
                }
                if (sweep == 1 && outerFixed) {
                    continue;
                }
                if (declarations.dense) {
                    outerCursor = 0u;
                    innerCursor = 0u;
                }
                if (!LayoutGridFindArea(occupancy, outerIsColumn, spanColumns,
                                        spanRows, outerCursor, innerCursor,
                                        outerIsColumn ? gridRows : gridColumns,
                                        sweep == 0 ? 1 : 0, fixedOuter,
                                        innerFixed, fixedInner,
                                        &column, &row)) {
                    walk->block->grid.itemsSkipped++;
                    item->id = RoboLayoutNodeIdNone;
                    continue;
                }
                item->columnStart = column;
                item->columnEnd = column + spanColumns;
                item->rowStart = row;
                item->rowEnd = row + spanRows;
                LayoutGridAreaMark(occupancy, item->columnStart, item->columnEnd,
                                   item->rowStart, item->rowEnd);
                walk->block->grid.autoPlaced++;
                if (declarations.dense) {
                    walk->block->grid.denseItems++;
                }
                /* The sparse cursor advances along the inner axis and keeps the outer
                 * one, which is what makes a sparse placement never look back. */
                if (outerIsColumn) {
                    columnCursor = item->columnStart;
                    rowCursor = item->rowEnd;
                } else {
                    columnCursor = item->columnEnd;
                    rowCursor = item->rowStart;
                }
            }
        }
    }

    /* The tracks the placements reached, inside the pass's bound. */
    columns = explicitColumns;
    rows = explicitRows;
    for (index = 0u; index < itemCount; index++) {
        if (items[index].id == RoboLayoutNodeIdNone) {
            continue;
        }
        if (items[index].columnEnd > columns) {
            columns = items[index].columnEnd;
        }
        if (items[index].rowEnd > rows) {
            rows = items[index].rowEnd;
        }
    }
    if (columns > LAYOUT_GRID_MAX_TRACKS) {
        columns = LAYOUT_GRID_MAX_TRACKS;
    }
    if (rows > LAYOUT_GRID_MAX_TRACKS) {
        rows = LAYOUT_GRID_MAX_TRACKS;
    }
    for (index = 0u; index < itemCount; index++) {
        LayoutGridItem *item = &items[index];

        if (item->id == RoboLayoutNodeIdNone) {
            continue;
        }
        if (item->columnEnd > columns || item->rowEnd > rows) {
            /* A placement past the bound, counted and left unplaced rather than clamped
             * into a track it did not name. */
            walk->block->grid.itemsSkipped++;
            item->id = RoboLayoutNodeIdNone;
            continue;
        }
        if (item->columnEnd > explicitColumns) {
            walk->block->grid.implicitTracks +=
                item->columnEnd -
                (item->columnStart > explicitColumns ? item->columnStart
                                                     : explicitColumns);
        }
        if (item->rowEnd > explicitRows) {
            walk->block->grid.implicitTracks +=
                item->rowEnd -
                (item->rowStart > explicitRows ? item->rowStart : explicitRows);
        }
        if (item->columnEnd - item->columnStart > 1u ||
            item->rowEnd - item->rowStart > 1u) {
            walk->block->grid.spanningItems++;
        }
    }

    columnSet.count = columns;
    rowSet.count = rows;
    walk->block->grid.columns += columns;
    walk->block->grid.rows += rows;

    /* ---- 4. size the columns --------------------------------------- */

    for (index = 0u; index < columns; index++) {
        CssGridTrackSize declared = LayoutGridTrackSizeAt(
            &declarations.columns, &declarations.autoColumns, index);
        int contentSized = 0;

        LayoutGridTrackInit(&columnSet.tracks[index], &declared, contentWidth, 1,
                            &contentSized);
        walk->block->grid.contentTracks += (size_t)contentSized;
        if (columnSet.tracks[index].isFraction) {
            walk->block->grid.frTracks++;
        }
    }
    for (index = 0u; index < itemCount; index++) {
        LayoutGridItem *item = &items[index];
        LayoutIntrinsicSize intrinsic;
        int32_t contribution;

        if (item->id == RoboLayoutNodeIdNone) {
            continue;
        }
        LayoutIntrinsicSizeInit(&intrinsic);
        (void)LayoutIntrinsicMeasureObject(walk->tree, item->id, walk->textMetrics,
                                          &intrinsic, NULL);
        contribution = (int32_t)intrinsic.minContent + item->inlineInsets;
        LayoutGridDistributeContribution(columnSet.tracks, item, 1, contribution, 0);
        contribution = (int32_t)intrinsic.maxContent + item->inlineInsets;
        LayoutGridDistributeContribution(columnSet.tracks, item, 1, contribution, 1);
    }
    LayoutGridTrackResolve(&columnSet, contentWidth,
                           declarations.columnGap *
                               (int32_t)(columns > 0u ? columns - 1u : 0u),
                           declarations.justify);
    LayoutGridContentDistribute(&columnSet, declarations.justify,
                                (int32_t)columns);

    /* ---- 5. each item's inline size, and its contents -------------- */

    for (index = 0u; index < itemCount; index++) {
        LayoutGridItem *item = &items[index];
        const LayoutObject *itemObject;
        int32_t areaInline = 0;
        int32_t contentW;
        GfxPixelY contentExtent = 0;
        size_t track;

        if (item->id == RoboLayoutNodeIdNone) {
            continue;
        }
        itemObject = LayoutObjectSlotConst(walk->tree, item->id);
        if (!itemObject) {
            return RoboLayoutDiag(walk->diag, RoboLayoutDomainGrid, RoboError,
                                  "a grid item vanished under the pass");
        }
        for (track = item->columnStart; track < item->columnEnd; track++) {
            areaInline += columnSet.tracks[track].size;
        }
        if (item->columnEnd > item->columnStart + 1u) {
            areaInline += declarations.columnGap *
                          (int32_t)(item->columnEnd - item->columnStart - 1u);
        }

        {
            LayoutDimension declared = LayoutDimensionAuto();
            int32_t declaredWidth;

            (void)LayoutSizingReadDimension(itemObject->style, "width", &declared);
            /*
             * A grid item's containing block *is its area*, so a percentage width
             * resolves against the inline size the columns gave it -- which is CSS's
             * rule, and the one an author writing `width: 50%` in a two-column grid
             * means. `auto` is the **stretch** case: the item fills its area less its
             * own insets, which is the initial `justify-items: normal`.
             */
            declaredWidth = LayoutGridDeclared(&declared, areaInline, 1);
            if (declaredWidth >= 0) {
                contentW = declaredWidth;
                if (contentW + item->inlineInsets > areaInline) {
                    item->overflowed = 1;
                    walk->block->grid.overflowingItems++;
                }
            } else {
                contentW = areaInline - item->inlineInsets;
                if (contentW < 0) {
                    contentW = 0;
                }
            }
        }
        item->contentW = contentW;

        if (LayoutBlockWalkChildren(walk, item->id, contentW, &contentExtent) !=
            RoboOk) {
            return walk->diag ? walk->diag->code : RoboError;
        }

        {
            LayoutDimension declaredHeight = LayoutDimensionAuto();
            int32_t height;

            (void)LayoutSizingReadDimension(itemObject->style, "height",
                                            &declaredHeight);
            height = LayoutGridDeclared(&declaredHeight, containerHeight,
                                        hasDefiniteHeight ? 1 : 0);
            if (height >= 0) {
                item->blockHypothetical = height;
            } else {
                item->blockHypothetical = (int32_t)contentExtent;
                item->autoHeight = 1;
            }
        }
        item->contentH = item->blockHypothetical;
    }

    /* ---- 6. size the rows ------------------------------------------ */

    for (index = 0u; index < rows; index++) {
        CssGridTrackSize declared = LayoutGridTrackSizeAt(
            &declarations.rows, &declarations.autoRows, index);
        int contentSized = 0;

        /*
         * An **indefinite** container has no free space for a share of, so a `fr` row is
         * treated as `auto`: CSS sizes such a row to its content, and a build that kept
         * the fraction would give every `1fr` row zero height in an `auto`-height grid.
         */
        if (!hasDefiniteHeight &&
            declared.maximum.kind == RoboCssGridBreadthFraction) {
            declared.maximum.kind = RoboCssGridBreadthAuto;
        }
        LayoutGridTrackInit(&rowSet.tracks[index], &declared, containerHeight,
                            hasDefiniteHeight ? 1 : 0, &contentSized);
        walk->block->grid.contentTracks += (size_t)contentSized;
        if (rowSet.tracks[index].isFraction) {
            walk->block->grid.frTracks++;
        }
    }
    for (index = 0u; index < itemCount; index++) {
        LayoutGridItem *item = &items[index];
        int32_t contribution;

        if (item->id == RoboLayoutNodeIdNone) {
            continue;
        }
        contribution = item->contentH + item->blockInsets;
        LayoutGridDistributeContribution(rowSet.tracks, item, 0, contribution, 0);
        LayoutGridDistributeContribution(rowSet.tracks, item, 0, contribution, 1);
    }
    LayoutGridTrackResolve(&rowSet,
                           hasDefiniteHeight ? containerHeight : 0,
                           declarations.rowGap *
                               (int32_t)(rows > 0u ? rows - 1u : 0u),
                           declarations.alignContent);
    if (!hasDefiniteHeight) {
        /* An `auto`-height container has no free space to distribute: the rows *are*
         * the extent, which is why `align-content` does nothing here. */
        rowSet.free = 0;
        rowSet.start = 0;
        rowSet.gapExtra = 0;
    } else {
        LayoutGridContentDistribute(&rowSet, declarations.alignContent,
                                    (int32_t)rows);
    }

    /* ---- 6b. the items' baselines, and the rows they raise ---------- */

    /*
     * A **baseline-aligned row** is as tall as its deepest baseline plus the deepest
     * descent below it, exactly as a baseline-aligned flex line is: two items whose
     * baselines line up occupy more room than either alone whenever their block sizes
     * differ. The row's own baseline is then the deepest one on it, and every
     * baseline-aligned item in the row is placed so that its own baseline lands there.
     */
    for (index = 0u; index < itemCount; index++) {
        LayoutGridItem *item = &items[index];
        size_t row;

        if (item->id == RoboLayoutNodeIdNone ||
            item->align != RoboCssFlexAlignBaseline) {
            continue;
        }
        item->baselineAligned = 1;
        item->baseline = LayoutGridItemBaseline(walk, item,
                                               &item->baselineFromChild);
        walk->block->grid.baselineItems++;
        for (row = item->rowStart; row < item->rowEnd; row++) {
            int32_t descent = (item->contentH + item->blockInsets) -
                              item->baseline;

            if (item->baseline > rowBaseline[row]) {
                rowBaseline[row] = item->baseline;
            }
            if (descent > rowDescent[row]) {
                rowDescent[row] = descent;
            }
        }
    }
    for (index = 0u; index < rows; index++) {
        int32_t need = rowBaseline[index] + rowDescent[index];

        if (need > rowSet.tracks[index].size) {
            rowSet.tracks[index].size = need;
        }
    }

    /* ---- 7. track offsets, and each item's area --------------------- */

    {
        int32_t columnOffset[LAYOUT_GRID_MAX_TRACKS];
        int32_t rowOffset[LAYOUT_GRID_MAX_TRACKS];
        int32_t cursor = columnSet.start;
        int32_t blockCursor = rowSet.start;

        for (index = 0u; index < columns; index++) {
            columnOffset[index] = cursor;
            cursor += columnSet.tracks[index].size + declarations.columnGap +
                      columnSet.gapExtra;
        }
        for (index = 0u; index < rows; index++) {
            rowOffset[index] = blockCursor;
            blockCursor += rowSet.tracks[index].size + declarations.rowGap +
                           rowSet.gapExtra;
        }

        /*
         * `direction: rtl` reverses the **inline** axis: the columns' start edge is the
         * right one, so each column is mirrored within the container's content box. Only
         * the columns move -- the block axis is untouched, because this build lays a grid
         * out in the horizontal writing mode and nothing about `direction` changes which
         * axis is which.
         */
        if (declarations.rtl) {
            for (index = 0u; index < columns; index++) {
                columnOffset[index] = contentWidth - columnOffset[index] -
                                      columnSet.tracks[index].size;
            }
        }

        /* ---- 8. each item's alignment and position ------------------ */

        for (index = 0u; index < itemCount; index++) {
            LayoutGridItem *item = &items[index];
            LayoutObject *itemObject;
            int32_t areaInline = 0;
            int32_t areaBlock = 0;
            int32_t outerInline;
            int32_t outerBlock;
            int32_t inlinePos;
            int32_t blockPos;
            size_t track;

            if (item->id == RoboLayoutNodeIdNone) {
                continue;
            }
            itemObject = LayoutObjectSlot(walk->tree, item->id);
            if (!itemObject) {
                return RoboLayoutDiag(walk->diag, RoboLayoutDomainGrid, RoboError,
                                      "a grid item vanished under the pass");
            }
            for (track = item->columnStart; track < item->columnEnd; track++) {
                areaInline += columnSet.tracks[track].size;
            }
            if (item->columnEnd > item->columnStart + 1u) {
                areaInline += declarations.columnGap *
                              (int32_t)(item->columnEnd - item->columnStart - 1u);
            }
            for (track = item->rowStart; track < item->rowEnd; track++) {
                areaBlock += rowSet.tracks[track].size;
            }
            if (item->rowEnd > item->rowStart + 1u) {
                areaBlock += declarations.rowGap *
                             (int32_t)(item->rowEnd - item->rowStart - 1u);
            }

            /*
             * The block-axis size, then the two alignments. `stretch` (and `normal`)
             * grows an item with an `auto` height to its area -- the same rule the flex
             * pass applies to an item's cross size, asked here of the block axis. An item
             * with a *declared* height keeps it and is positioned inside the area instead.
             */
            item->contentH = item->blockHypothetical;
            outerBlock = item->contentH + item->blockInsets;
            if (item->autoHeight &&
                (item->align == RoboCssFlexAlignNormal ||
                 item->align == RoboCssFlexAlignStretch) &&
                !item->autoBlockBefore && !item->autoBlockAfter) {
                int32_t stretched = areaBlock - item->blockInsets;

                if (stretched > item->contentH) {
                    item->contentH = stretched;
                    outerBlock = item->contentH + item->blockInsets;
                    item->stretched = 1;
                    walk->block->grid.stretched++;
                }
            }
            outerInline = item->contentW + item->inlineInsets;

            /* `auto` margins absorb the free space in their axis, **ahead of** the
             * alignment properties -- the alignment step's own first sub-step. */
            blockPos = rowOffset[item->rowStart];
            if (areaBlock > outerBlock &&
                (item->autoBlockBefore || item->autoBlockAfter)) {
                int32_t free = areaBlock - outerBlock;

                if (item->autoBlockBefore) {
                    item->margins.top += item->autoBlockAfter ? free / 2 : free;
                }
                if (item->autoBlockAfter) {
                    item->margins.bottom +=
                        item->autoBlockBefore ? free - free / 2 : free;
                }
                outerBlock = areaBlock;
                item->autoAbsorbed = 1;
            } else if (item->baselineAligned) {
                /* The row's baseline is the deepest one on it, computed in step 6b. */
                blockPos = rowOffset[item->rowStart] +
                           (rowBaseline[item->rowStart] - item->baseline);
            } else {
                switch (item->align) {
                case RoboCssFlexAlignCenter:
                    blockPos = rowOffset[item->rowStart] +
                               (areaBlock - outerBlock) / 2;
                    break;
                case RoboCssFlexAlignFlexEnd:
                case RoboCssFlexAlignEnd:
                case RoboCssFlexAlignRight:
                    blockPos = rowOffset[item->rowStart] +
                               (areaBlock - outerBlock);
                    break;
                default:
                    blockPos = rowOffset[item->rowStart];
                    break;
                }
            }
            if (blockPos < 0) {
                blockPos = 0;
            }

            /*
             * The inline axis. An `auto` width already stretched across the area; a
             * declared one sits at the area's **inline start**, which is the left edge
             * unless `direction: rtl` moved it -- and the two `auto` inline margins take
             * the space between them exactly as the block ones did. The border box's
             * physical left edge is `inlineStart + margin-left` in both directions,
             * because the mapping above already made `margins.left` the side the inline
             * axis reaches second under RTL.
             */
            inlinePos = columnOffset[item->columnStart];
            if (declarations.rtl) {
                inlinePos = columnOffset[item->columnEnd - 1u] +
                            columnSet.tracks[item->columnEnd - 1u].size -
                            outerInline;
            }
            if (areaInline > outerInline &&
                (item->autoInlineBefore || item->autoInlineAfter)) {
                int32_t free = areaInline - outerInline;

                if (item->autoInlineBefore) {
                    if (declarations.rtl) {
                        item->margins.right +=
                            item->autoInlineAfter ? free / 2 : free;
                    } else {
                        item->margins.left +=
                            item->autoInlineAfter ? free / 2 : free;
                    }
                }
                if (item->autoInlineAfter) {
                    if (declarations.rtl) {
                        item->margins.left +=
                            item->autoInlineBefore ? free - free / 2 : free;
                    } else {
                        item->margins.right +=
                            item->autoInlineBefore ? free - free / 2 : free;
                    }
                }
                item->autoAbsorbed = 1;
            }
            if (inlinePos < 0) {
                inlinePos = 0;
            }
            item->positionX = inlinePos;
            item->positionY = blockPos;
            if (item->autoAbsorbed) {
                walk->block->grid.autoMargins++;
            }
        }

        /* ---- 9. write each item ------------------------------------ */

        for (index = 0u; index < itemCount; index++) {
            LayoutGridItem *item = &items[index];
            LayoutObject *itemObject;
            LayoutSize contentSize;
            LayoutSize paddingBox;
            LayoutSize size;
            LayoutPoint origin;
            LayoutOverflowEdges overflow;
            uint32_t flags = 0u;

            if (item->id == RoboLayoutNodeIdNone) {
                continue;
            }
            itemObject = LayoutObjectSlot(walk->tree, item->id);
            if (!itemObject) {
                return RoboLayoutDiag(walk->diag, RoboLayoutDomainGrid, RoboError,
                                      "a grid item vanished under the pass");
            }

            contentSize.width = item->contentW;
            contentSize.height = item->contentH;
            if (LayoutPaddingEdgesPaddingBox(&item->padding, contentSize,
                                             &paddingBox, walk->diag) != RoboOk) {
                return walk->diag ? walk->diag->code : RoboError;
            }
            if (LayoutBorderEdgesBorderBox(&item->borders, paddingBox, &size,
                                           walk->diag) != RoboOk) {
                return walk->diag ? walk->diag->code : RoboError;
            }
            origin.x = LayoutExtentClamp((int64_t)item->positionX +
                                         (int64_t)item->margins.left);
            origin.y = LayoutExtentClamp((int64_t)item->positionY +
                                         (int64_t)item->margins.top);

            itemObject->margins = item->margins;
            itemObject->padding = item->padding;
            itemObject->borders = item->borders;
            LayoutPositionOffsetInit(&itemObject->positionOffset);

            /* The baseline the item was **placed** with, recorded here for the reason the
             * flex pass records it: a baseline is by definition a placement fact. */
            if (item->baselineAligned) {
                int32_t ascent = item->baseline;
                int32_t descent = (item->contentH + item->blockInsets) -
                                  item->baseline;

                if (ascent < 0) {
                    ascent = 0;
                }
                if (descent < 0) {
                    descent = 0;
                }
                itemObject->baseline.kind = item->baselineFromChild
                                                ? RoboLayoutBaselineText
                                                : RoboLayoutBaselineBox;
                itemObject->baseline.ascent = ascent;
                itemObject->baseline.descent = descent;
            } else {
                LayoutBaselineMetricsInit(&itemObject->baseline);
            }

            if (LayoutResultSetGeometry(&itemObject->result, origin, size,
                                        walk->diag) != RoboOk) {
                return walk->diag ? walk->diag->code : RoboError;
            }
            if (LayoutOverflowMeasure(walk->tree, itemObject, &item->borders,
                                      &item->padding, size, &overflow,
                                      walk->diag) != RoboOk) {
                return walk->diag ? walk->diag->code : RoboError;
            }
            itemObject->overflow = overflow;
            if (LayoutOverflowEdgesHasOverflowAxis(
                    &overflow, RoboLayoutOverflowAxisHorizontal)) {
                flags |= RoboLayoutResultFlagOverflowX;
            }
            if (LayoutOverflowEdgesHasOverflowAxis(
                    &overflow, RoboLayoutOverflowAxisVertical)) {
                flags |= RoboLayoutResultFlagOverflowY;
            }
            (void)LayoutResultSetFlags(&itemObject->result, flags, NULL);

            walk->block->blocksPlaced++;
            walk->block->grid.itemsPlaced++;
            walk->block->marginsApplied += item->margins.resolved;
            walk->block->marginsUnresolved += item->margins.unresolved;
            walk->block->paddingsApplied += item->padding.resolved;
            walk->block->paddingsUnresolved += item->padding.unresolved;
            walk->block->bordersApplied += item->borders.widthsResolved;
            walk->block->bordersUnresolved += item->borders.widthsUnresolved;
            walk->block->bordersDrawn += item->borders.bordersDrawn;
            walk->block->overflowClipped +=
                LayoutOverflowEdgesClipsAny(&overflow) ? 1u : 0u;
            walk->block->overflowing +=
                LayoutOverflowEdgesHasOverflow(&overflow) ? 1u : 0u;
            walk->block->overflowUnresolved += overflow.axesUnresolved;

            if (itemObject->depth > walk->block->depthMax) {
                walk->block->depthMax = itemObject->depth;
            }
        }
    }

    /* The container's content extent: the rows and the block-axis gutters between them,
     * which is what a block pass stacks by. */
    {
        int64_t total = 0;

        for (index = 0u; index < rows; index++) {
            total += rowSet.tracks[index].size;
        }
        if (rows > 1u) {
            total += (int64_t)declarations.rowGap * (int64_t)(rows - 1u);
            total += (int64_t)rowSet.gapExtra * (int64_t)(rows - 1u);
        }
        total += rowSet.start;
        *outExtent = (GfxPixelY)LayoutExtentClamp(total);
    }
    return RoboOk;
}
