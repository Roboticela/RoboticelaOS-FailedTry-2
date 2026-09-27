/*
 * RoboLayout flex layout (Phase 19.3) — see RoboLayoutFlex.h and FlexLayout.md.
 *
 * One pass, and it is the third formatting context this build lays out. The block
 * pass reaches it exactly as it reaches the inline one: a box whose *owned* context
 * is `RoboLayoutFcFlex` hands its contents to `LayoutFlexWalkContents` instead of
 * stacking them (RoboLayoutBlock.c), and this file places every in-flow child as a
 * flex item -- writing each item's result, its four box-model records, its overflow
 * and its children.
 *
 * The algorithm is CSS Flexible Box Layout's, in the order it states it, and the
 * order is the whole of the implementation:
 *
 *   1. collect the items (in-flow boxes only) and order them by `order`;
 *   2. a flex base size per item (flex-basis, the main-size property, or the
 *      measured content size) with its min/max clamp;
 *   3. break the items into lines when wrapping applies and the container's main
 *      size is definite;
 *   4. resolve the flexible lengths per line (grow/shrink with freezing);
 *   5. lay the items' contents out at their resolved main size, which is what the
 *      cross sizes -- and the items' baselines -- are a function of;
 *   6. size the lines (including a baseline-aligned line's own baseline), stretch
 *      the items that stretch, absorb `auto` margins, and align everything.
 *
 * The two axes are a *flag* (`rowMain`) rather than two code paths, and that is a
 * decision worth keeping: every position below is computed once in main/cross terms
 * and only swapped at the two points it becomes a coordinate, so a `column` and a
 * `row` cannot disagree about `justify-content` or about `flex-grow`.
 *
 * What this file deliberately does not do is named in RoboLayoutFlex.h and in
 * `Documentation/Runtime/Layout/FlexLayout.md` §6: no flex root, no baseline for a
 * `column`'s inline axis, no `gap`, no shorthand expansion, no reversed inline axis
 * on a `column`, and no vertical writing mode.
 */

#include "RoboLayout/RoboLayoutFlex.h"

#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSDisplay.h"
#include "RoboCSS/RoboCSSFlex.h"
#include "RoboCSS/RoboCSSTypes.h"

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
 * Items one flex line's resolution handles without allocating.
 *
 * A fixed cap rather than a growing arena, because a flex container is a *line* of
 * boxes and a document with more than a few hundred children in one flex line is
 * an authoring mistake rather than a case to optimise. A child beyond the cap is
 * counted as skipped (`flex.itemsSkipped`) rather than dropped silently.
 */
#define LAYOUT_FLEX_MAX_ITEMS 128u

/** The largest content-box size a resolved flex length may reach. */
#define LAYOUT_FLEX_SIZE_MAX ((int32_t)LAYOUT_STYLE_LENGTH_MAX)

/* ------------------------------------------------------------------ */
/* The counters record                                                 */
/* ------------------------------------------------------------------ */

void LayoutFlexCountersInit(LayoutFlexCounters *counters)
{
    if (!counters) {
        return;
    }
    memset(counters, 0, sizeof(*counters));
}

int LayoutFlexCountersEqual(const LayoutFlexCounters *a,
                            const LayoutFlexCounters *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->containers == b->containers &&
           a->itemsPlaced == b->itemsPlaced &&
           a->itemsSkipped == b->itemsSkipped &&
           a->lines == b->lines &&
           a->growItems == b->growItems &&
           a->stretched == b->stretched &&
           a->reordered == b->reordered &&
           a->reversed == b->reversed &&
           a->contentMinItems == b->contentMinItems &&
           a->autoMargins == b->autoMargins &&
           a->baselineItems == b->baselineItems;
}

/* ------------------------------------------------------------------ */
/* Reading the container's declarations                                */
/* ------------------------------------------------------------------ */

static const CssComputedValue *LayoutFlexProperty(const CssComputedStyle *style,
                                                  const char *property)
{
    if (!style) {
        return NULL;
    }
    return CssComputedStyleComputedByName(style, CssSliceFromText(property));
}

static CssFlexDirection LayoutFlexDirectionOf(const CssComputedStyle *style)
{
    const CssComputedValue *value = LayoutFlexProperty(style, "flex-direction");

    if (!value || value->kind != RoboCssComputedValueFlexDirection) {
        return RoboCssFlexDirectionRow;
    }
    return CssComputedValueFlexDirection(value);
}

/**
 * Whether the container's inline axis runs right-to-left (`direction: rtl`).
 *
 * Read from the same computed keyword the text layer reads
 * (`LayoutTextStyleResolve`), so a container's flex direction and its text's
 * direction can never disagree: one property, one door. An absent or unknown value
 * answers left-to-right, which is the property's initial value.
 */
static int LayoutFlexDirectionIsRtl(const CssComputedStyle *style)
{
    const CssComputedValue *value = LayoutFlexProperty(style, "direction");

    if (!value || value->kind != RoboCssComputedValueKeyword) {
        return 0;
    }
    return CssSliceEqualsAsciiCi(CssComputedValueText(value), "rtl");
}

static CssFlexWrap LayoutFlexWrapOf(const CssComputedStyle *style)
{
    const CssComputedValue *value = LayoutFlexProperty(style, "flex-wrap");

    if (!value || value->kind != RoboCssComputedValueFlexWrap) {
        return RoboCssFlexWrapNoWrap;
    }
    return CssComputedValueFlexWrap(value);
}

static CssFlexAlign LayoutFlexAlignOf(const CssComputedStyle *style,
                                      const char *property,
                                      CssFlexAlign fallback)
{
    const CssComputedValue *value = LayoutFlexProperty(style, property);

    if (!value || value->kind != RoboCssComputedValueFlexAlign) {
        return fallback;
    }
    return CssComputedValueFlexAlign(value);
}

static double LayoutFlexNumber(const CssComputedStyle *style,
                               const char *property,
                               double fallback)
{
    const CssComputedValue *value = LayoutFlexProperty(style, property);

    if (!value || value->kind != RoboCssComputedValueNumber || value->unresolved) {
        return fallback;
    }
    return value->number;
}

static int32_t LayoutFlexOrder(const CssComputedStyle *style)
{
    double number = LayoutFlexNumber(style, "order", 0.0);

    if (number < -2147483647.0) {
        return -2147483647;
    }
    if (number > 2147483647.0) {
        return 2147483647;
    }
    return (int32_t)number;
}

/* ------------------------------------------------------------------ */
/* Box-model helpers for one item                                      */
/* ------------------------------------------------------------------ */

static int32_t LayoutFlexPercentagePx(double percentage, int32_t base)
{
    return (int32_t)LayoutPxRound(percentage / 100.0 * (double)base);
}

/**
 * Resolve the four margins of a flex item.
 *
 * The one policy this phase adds to 10.9's two: an `auto` is a used value of
 * **zero here**, and the side it was on is reported through @p outAuto so the
 * alignment step can hand it the free space instead (CSS Flexbox §8.1: "Prior to
 * alignment via justify-content and align-self, any positive free space is
 * distributed to auto margins in that axis"). A percentage resolves against the
 * containing block's **width** (the flex container's content width), which is CSS's
 * rule for every side and the same rule 19.2 gave a block box.
 *
 * @p outAuto is a bitmask over @ref CssBoxSide -- one bit per side whose declared
 * margin was `auto` -- so the caller can tell "no free space was claimed" from "a
 * margin was willing to claim it" without a second read of the style.
 */
static void LayoutFlexMarginsOf(const CssComputedStyle *style,
                                int32_t containingWidth,
                                LayoutMarginEdges *out,
                                uint32_t *outAuto)
{
    LayoutMargins declared;
    CssBoxSide sides[RoboCssBoxSideCount] = {
        RoboCssBoxSideTop, RoboCssBoxSideRight,
        RoboCssBoxSideBottom, RoboCssBoxSideLeft
    };
    GfxPixelX used[RoboCssBoxSideCount] = { 0, 0, 0, 0 };
    uint32_t autoMask = 0u;
    size_t index;

    memset(out, 0, sizeof(*out));
    out->availableContentWidth = containingWidth;
    LayoutMarginsInit(&declared);
    declared = LayoutMarginsOf(style);

    for (index = 0u; index < RoboCssBoxSideCount; index++) {
        LayoutMargin margin = LayoutMarginAbsent();

        if (LayoutMarginsSide(&declared, sides[index], &margin) != RoboOk) {
            continue;
        }
        if (margin.kind == RoboLayoutMarginResolved) {
            used[index] = margin.length;
            out->resolved++;
        } else if (margin.kind == RoboLayoutMarginPercentage) {
            used[index] = LayoutFlexPercentagePx(margin.percentage, containingWidth);
            out->resolved++;
        } else if (margin.kind == RoboLayoutMarginUnresolved) {
            out->unresolved++;
        } else if (margin.kind == RoboLayoutMarginAuto) {
            /* A used value of zero -- until the alignment step finds free space to
             * give it -- and recorded so that step can. */
            autoMask |= (uint32_t)1u << (uint32_t)sides[index];
        }
        /* ABSENT is a used value of zero and claims nothing. */
    }
    out->top = used[RoboCssBoxSideTop];
    out->right = used[RoboCssBoxSideRight];
    out->bottom = used[RoboCssBoxSideBottom];
    out->left = used[RoboCssBoxSideLeft];
    if (outAuto) {
        *outAuto = autoMask;
    }
}

/* ------------------------------------------------------------------ */
/* The item                                                            */
/* ------------------------------------------------------------------ */

typedef struct LayoutFlexItem {
    LayoutNodeId id;
    int32_t order;
    double grow;
    double shrink;

    /* The main axis, content-box units. */
    int32_t baseMain;
    int32_t minMain;
    int32_t maxMain;
    int32_t mainSize;
    int frozen;
    int32_t mainInsets;   /* margins + border + padding along the main axis */
    int32_t outerMain;

    /* The cross axis, content-box units. */
    int32_t crossHypothetical;  /* before stretch */
    int32_t crossSize;          /* resolved content-box cross size */
    int32_t crossInsets;
    int32_t outerCross;
    int stretch;
    size_t line;

    /*
     * The `auto` margins, mapped onto the two axes' **flow-relative** sides: the
     * `*Before` flag is the main-start (or cross-start) margin, whichever physical
     * side the reversal flags made that. The alignment step reads only these, so a
     * `row-reverse` or RTL container cannot give an auto margin the wrong side.
     */
    int autoMainBefore;
    int autoMainAfter;
    int autoCrossBefore;
    int autoCrossAfter;
    /* Set when an `auto` margin actually **absorbed** free space, which is what
     * the counter and the box-model bookkeeping want to know. */
    int autoAbsorbed;

    /*
     * The item's **baseline**, as a distance along the cross axis from the item's
     * cross-start margin edge. Only a `row` consults it (a column's cross axis is
     * the inline one). `baselineFromChild` says whether it came from a
     * baseline-bearing child rather than the bottom-edge fallback.
     */
    int32_t baseline;
    int baselineFromChild;

    /* The item's resolved cross alignment (`align-self` over `align-items`), read
     * once so the line sizing, the baseline pass and the placement cannot disagree. */
    CssFlexAlign align;
    int baselineAligned;

    /* The used minimum main size came from the automatic (content) floor. */
    int contentMin;

    /* The item's own content box and where its border box goes (container space). */
    int32_t contentW;
    int32_t contentH;
    int32_t positionX;
    int32_t positionY;

    /* The four box-model records, resolved once and written to the object. */
    LayoutMarginEdges margins;
    LayoutPaddingEdges padding;
    LayoutBorderEdges borders;
} LayoutFlexItem;

static int32_t LayoutFlexClamp(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

/**
 * The content-box length an item's declared dimension asks for, or -1 when it asks
 * for none this build can use (`auto`, a percentage with no base, an unusable unit).
 */
static int32_t LayoutFlexDeclared(const LayoutDimension *dimension,
                                  int32_t base,
                                  int hasBase)
{
    if (dimension->kind == RoboLayoutDimensionDefinite) {
        return dimension->length;
    }
    if (dimension->kind == RoboLayoutDimensionPercentage && hasBase) {
        int32_t value = LayoutFlexPercentagePx(LayoutDimensionPercentageOf(dimension),
                                               base);

        return value < 0 ? 0 : value;
    }
    return -1;
}

/** An item's measured content size; a failure degrades to zero, exactly as a pass
 *  with no metrics source measures every run as zero. */
static void LayoutFlexIntrinsic(const LayoutTree *tree,
                                LayoutNodeId id,
                                const LayoutTextMetrics *metrics,
                                LayoutIntrinsicSize *out)
{
    LayoutIntrinsicSizeInit(out);
    (void)LayoutIntrinsicMeasureObject(tree, id, metrics, out, NULL);
}

/**
 * The item's **baseline**, as a distance along the cross axis from its cross-start
 * margin edge.
 *
 * The item's baseline is its first baseline-bearing **direct child**'s, which is
 * the item's first baseline at the depth this build places text: an item that
 * establishes an inline formatting context has its measured runs as direct
 * children, and that child's baseline is exactly the run's font baseline
 * (`LayoutInlineFinalizeLine` writes each member's `y` so that this holds). An item
 * with no such child -- an empty box, or a box whose text sits deeper than one
 * level -- takes the documented **bottom-edge fallback** (`RoboLayoutBaseline.h`'s
 * BOX rule), which puts the item on its line by its feet.
 *
 * The offset is measured from the item's cross-start *margin* edge, because that
 * edge is what the cross-axis placement positions: `item->baseline` and a
 * candidate `crossPos` share an origin, so "the baseline sits at `crossPos +
 * baseline`" needs no third term.
 */
static int32_t LayoutFlexItemBaseline(const LayoutWalk *walk,
                                     const LayoutFlexItem *item,
                                     int rowMain,
                                     int *outFromChild)
{
    const LayoutObject *itemObject = LayoutObjectSlotConst(walk->tree, item->id);
    int32_t crossStart;
    LayoutNodeId child;

    if (outFromChild) {
        *outFromChild = 0;
    }
    if (!itemObject) {
        return item->outerCross;
    }
    crossStart = rowMain
                     ? item->margins.top + item->borders.top.width +
                           item->padding.top
                     : item->margins.left + item->borders.left.width +
                           item->padding.left;

    child = itemObject->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree, child);

        if (!childObject) {
            break;
        }
        if (LayoutResultIsComputed(&childObject->result) &&
            LayoutBaselineKindIsUsable(childObject->baseline.kind)) {
            int32_t along = rowMain ? childObject->result.position.y
                                    : childObject->result.position.x;

            if (outFromChild) {
                *outFromChild = 1;
            }
            return crossStart + along + childObject->baseline.ascent;
        }
        child = childObject->NextSibling;
    }
    return item->outerCross;
}

/**
 * Give an absorbed `auto` margin its free space, on the physical side the author
 * wrote it on.
 *
 * The inverse of the mapping the collection loop made from physical sides to
 * flow-relative flags, and it lives here rather than at each call site because
 * that is the whole point of the mapping: `margin-left: auto` keeps its physical
 * meaning whatever `row-reverse` or `direction: rtl` did to the axis, and only the
 * *question* "is this the before-margin?" is directional.
 */
static void LayoutFlexAbsorbMargin(LayoutFlexItem *item,
                                   int rowMain,
                                   int mainReversed,
                                   int before,
                                   int32_t amount)
{
    if (amount <= 0) {
        return;
    }
    /* The before-margin is on the axis's *start* side, which the reversal flag moves
     * from left/top to right/bottom. */
    if (before != mainReversed) {
        if (rowMain) {
            item->margins.left += amount;
        } else {
            item->margins.top += amount;
        }
    } else {
        if (rowMain) {
            item->margins.right += amount;
        } else {
            item->margins.bottom += amount;
        }
    }
}

/** The same for the cross axis, whose before side is the top of a `row` and the
 *  left of a `column` (a column's reversed inline axis is 19.10's --
 *  RoboLayoutFlex.h). */
static void LayoutFlexAbsorbCrossMargin(LayoutFlexItem *item,
                                        int rowMain,
                                        int before,
                                        int32_t amount)
{
    if (amount <= 0) {
        return;
    }
    if (rowMain) {
        if (before) {
            item->margins.top += amount;
        } else {
            item->margins.bottom += amount;
        }
    } else {
        if (before) {
            item->margins.left += amount;
        } else {
            item->margins.right += amount;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

Result LayoutFlexWalkContents(LayoutWalk *walk,
                              LayoutNodeId owner,
                              int32_t contentWidth,
                              int hasDefiniteHeight,
                              GfxPixelY definiteHeight,
                              GfxPixelY *outExtent)
{
    const LayoutObject *container;
    CssFlexDirection direction;
    CssFlexWrap wrap;
    CssFlexAlign justify;
    CssFlexAlign itemsAlign;
    CssFlexAlign contentAlign;
    int rowMain;
    int mainReversed;
    int crossReversed;
    int mainDefinite;
    int32_t containerMain;
    int32_t containerCross;
    int crossDefinite;
    LayoutFlexItem items[LAYOUT_FLEX_MAX_ITEMS];
    size_t itemCount = 0u;
    size_t lineStart[LAYOUT_FLEX_MAX_ITEMS + 1u];
    int32_t lineCross[LAYOUT_FLEX_MAX_ITEMS];
    int32_t lineOffset[LAYOUT_FLEX_MAX_ITEMS];
    int32_t lineBaseline[LAYOUT_FLEX_MAX_ITEMS];
    size_t lineCount = 0u;
    size_t index;
    LayoutNodeId child;

    if (!walk || !walk->tree || !walk->block || !outExtent) {
        return RoboLayoutDiag(walk ? walk->diag : NULL, RoboLayoutDomainFlex,
                              RoboInvalidArgument,
                              "the flex pass needs a walk, a tree and an out");
    }
    if (contentWidth < 0) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainFlex,
                              RoboInvalidArgument, "a negative container width");
    }

    container = LayoutObjectSlotConst(walk->tree, owner);
    if (!container) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainFlex, RoboError,
                              "the box that owns a flex context is not in the tree");
    }

    direction = LayoutFlexDirectionOf(container->style);
    wrap = LayoutFlexWrapOf(container->style);
    justify = LayoutFlexAlignOf(container->style, "justify-content",
                                RoboCssFlexAlignNormal);
    itemsAlign = LayoutFlexAlignOf(container->style, "align-items",
                                   RoboCssFlexAlignNormal);
    contentAlign = LayoutFlexAlignOf(container->style, "align-content",
                                     RoboCssFlexAlignNormal);

    rowMain = CssFlexDirectionIsRow(direction);

    /*
     * Which physical direction each axis runs in, and the two flags that follow
     * from it. The **main** axis's start is the inline-start edge, so a
     * `direction: rtl` container reverses a row's main axis exactly as
     * `row-reverse` does -- and the two compose by exclusive-or, which is what makes
     * `row-reverse` inside an RTL container read left-to-right. A column's main axis
     * is the block one, which `direction` does not touch. The **cross** axis is
     * swapped by `wrap-reverse`; a column's cross axis is the inline one, and
     * reversing that is 19.10's, so it is deliberately not folded in here
     * (RoboLayoutFlex.h states the non-claim).
     */
    mainReversed = CssFlexDirectionIsReversed(direction) ? 1 : 0;
    if (rowMain && LayoutFlexDirectionIsRtl(container->style)) {
        mainReversed = !mainReversed;
    }
    crossReversed = CssFlexWrapIsReversed(wrap) ? 1 : 0;

    /* The main size is the container's content width for a row and its definite
     * content height for a column; the cross is the other. A row's main size is
     * always definite (the block pass resolved its width), which is why a row wraps
     * and a column with an `auto` height does not. */
    mainDefinite = rowMain ? 1 : (hasDefiniteHeight ? 1 : 0);
    containerMain = rowMain ? contentWidth
                            : (hasDefiniteHeight ? (int32_t)definiteHeight : 0);
    crossDefinite = rowMain ? (hasDefiniteHeight ? 1 : 0) : 1;
    containerCross = rowMain
                         ? (hasDefiniteHeight ? (int32_t)definiteHeight : 0)
                         : contentWidth;

    walk->block->flex.containers++;
    if (mainReversed) {
        walk->block->flex.reversed++;
    }

    /* ---- 1. collect the items ------------------------------------- */

    child = container->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree, child);
        LayoutFlexItem *item;
        LayoutDimension declaredMain;
        LayoutDimension declaredCross;
        LayoutDimension declaredMin;
        LayoutDimension declaredMax;
        LayoutDimension basis;
        LayoutIntrinsicSize intrinsic;
        int32_t declared;
        int32_t mainBase;

        if (!childObject) {
            return RoboLayoutDiag(walk->diag, RoboLayoutDomainFlex, RoboError,
                                  "a flex container's child link names no box");
        }

        /* An out-of-flow child is not a flex item: it is placed by the containing
         * block's own pass (14.0), exactly as it is in a block container. */
        {
            LayoutPosition position = LayoutPositionOf(childObject->style);

            if (LayoutAbsoluteIsOutOfFlow(&position)) {
                child = childObject->NextSibling;
                continue;
            }
        }

        /* A text run, or a box this build cannot place, is counted rather than
         * measured: CSS wraps an inline run in an anonymous flex item and this
         * build makes none. It is counted in both records so that the block pass's
         * accounting identity still closes (`RoboLayoutBlock.h`). */
        if (childObject->kind != RoboLayoutObjectBox ||
            itemCount >= LAYOUT_FLEX_MAX_ITEMS) {
            walk->block->flex.itemsSkipped++;
            walk->block->inlineLevelSkipped++;
            child = childObject->NextSibling;
            continue;
        }

        item = &items[itemCount];
        memset(item, 0, sizeof(*item));
        item->id = child;
        item->order = LayoutFlexOrder(childObject->style);
        item->grow = LayoutFlexNumber(childObject->style, "flex-grow", 0.0);
        item->shrink = LayoutFlexNumber(childObject->style, "flex-shrink", 1.0);
        if (item->grow < 0.0) {
            item->grow = 0.0;
        }
        if (item->shrink < 0.0) {
            item->shrink = 0.0;
        }

        {
            uint32_t autoMask = 0u;
            int autoTop;
            int autoRight;
            int autoBottom;
            int autoLeft;

            /*
             * The `auto` margins, mapped onto the two axes once and for all. Which
             * physical side is "before" is a fact about the reversal flags, and every
             * later step reads only the flow-relative flags -- so a `row-reverse` or
             * RTL container cannot hand an `auto` margin the wrong side.
             */
            LayoutFlexMarginsOf(childObject->style, contentWidth, &item->margins,
                                &autoMask);
            autoTop = (autoMask & ((uint32_t)1u << RoboCssBoxSideTop)) != 0u;
            autoRight = (autoMask & ((uint32_t)1u << RoboCssBoxSideRight)) != 0u;
            autoBottom = (autoMask & ((uint32_t)1u << RoboCssBoxSideBottom)) != 0u;
            autoLeft = (autoMask & ((uint32_t)1u << RoboCssBoxSideLeft)) != 0u;
            if (rowMain) {
                item->autoMainBefore = mainReversed ? autoRight : autoLeft;
                item->autoMainAfter = mainReversed ? autoLeft : autoRight;
                item->autoCrossBefore = autoTop;
                item->autoCrossAfter = autoBottom;
            } else {
                item->autoMainBefore = mainReversed ? autoBottom : autoTop;
                item->autoMainAfter = mainReversed ? autoTop : autoBottom;
                item->autoCrossBefore = autoLeft;
                item->autoCrossAfter = autoRight;
            }
        }
        {
            LayoutPaddings declaredPadding = LayoutPaddingsOf(childObject->style);
            LayoutBorders declaredBorders = LayoutBordersOf(childObject->style);

            if (LayoutPaddingResolve(&declaredPadding, contentWidth, &item->padding,
                                     walk->diag) != RoboOk) {
                return walk->diag ? walk->diag->code : RoboError;
            }
            if (LayoutBorderResolve(&declaredBorders, &item->borders,
                                    walk->diag) != RoboOk) {
                return walk->diag ? walk->diag->code : RoboError;
            }
        }

        LayoutDimensionInit(&declaredMain);
        LayoutDimensionInit(&declaredCross);
        LayoutDimensionInit(&declaredMin);
        LayoutDimensionInit(&declaredMax);
        LayoutDimensionInit(&basis);
        (void)LayoutSizingReadDimension(childObject->style,
                                        rowMain ? "width" : "height",
                                        &declaredMain);
        (void)LayoutSizingReadDimension(childObject->style,
                                        rowMain ? "height" : "width",
                                        &declaredCross);
        (void)LayoutSizingReadDimension(childObject->style,
                                        rowMain ? "min-width" : "min-height",
                                        &declaredMin);
        (void)LayoutSizingReadDimension(childObject->style,
                                        rowMain ? "max-width" : "max-height",
                                        &declaredMax);
        (void)LayoutSizingReadDimension(childObject->style, "flex-basis", &basis);

        LayoutFlexIntrinsic(walk->tree, child, walk->textMetrics, &intrinsic);

        /* The flex base size, in three steps for the reason CSS states three: a
         * `flex-basis` that names a size wins, the main-size property is next when
         * it is definite, and the measured content size is the last word. */
        mainBase = LayoutFlexDeclared(&basis, containerMain, mainDefinite);
        if (mainBase < 0) {
            mainBase = LayoutFlexDeclared(&declaredMain, contentWidth, 1);
        }
        if (mainBase < 0) {
            mainBase = rowMain ? (int32_t)intrinsic.maxContent
                               : (int32_t)intrinsic.maxContentHeight;
        }
        if (mainBase < 0) {
            mainBase = 0;
        }
        item->baseMain = mainBase;

        declared = LayoutFlexDeclared(&declaredMax, containerMain, mainDefinite);
        item->maxMain = declared >= 0 ? declared : LAYOUT_FLEX_SIZE_MAX;

        /*
         * The used minimum along the main axis: the declared one, or -- when
         * `min-width`/`min-height` is `auto` -- **the automatic minimum size**,
         * which CSS takes from the item's own content (Flexbox §4.5: the
         * content-based minimum size, clamped up to at most the item's maximum).
         * The floor is the *min-content* size, so an item does not shrink past its
         * longest unbreakable piece by accident, which is the whole point of the
         * rule. A document with no metrics measures it as zero, the same degradation
         * every other measurement here has. The one part of §4.5 this build cannot
         * honour is the `overflow: visible` exception, and RoboLayoutFlex.h names it.
         */
        declared = LayoutFlexDeclared(&declaredMin, containerMain, mainDefinite);
        if (declared >= 0) {
            item->minMain = declared;
        } else {
            int32_t contentMin = rowMain ? (int32_t)intrinsic.minContent
                                         : (int32_t)intrinsic.minContentHeight;

            if (contentMin < 0) {
                contentMin = 0;
            }
            item->minMain = LayoutFlexClamp(contentMin, 0, item->maxMain);
            /* Counted only when the floor was above zero, because the interesting
             * fact is whether the automatic minimum *bit*, not how many items were
             * eligible for it. */
            item->contentMin = item->minMain > 0 ? 1 : 0;
        }

        item->mainSize = LayoutFlexClamp(item->baseMain, item->minMain,
                                         item->maxMain);

        /* The insets along each axis, so an outer size is one addition. */
        if (rowMain) {
            item->mainInsets = item->margins.left + item->margins.right +
                               item->padding.left + item->padding.right +
                               item->borders.left.width + item->borders.right.width;
            item->crossInsets = item->margins.top + item->margins.bottom +
                                item->padding.top + item->padding.bottom +
                                item->borders.top.width + item->borders.bottom.width;
        } else {
            item->mainInsets = item->margins.top + item->margins.bottom +
                               item->padding.top + item->padding.bottom +
                               item->borders.top.width + item->borders.bottom.width;
            item->crossInsets = item->margins.left + item->margins.right +
                                item->padding.left + item->padding.right +
                                item->borders.left.width + item->borders.right.width;
        }
        if (item->mainInsets < 0) {
            item->mainInsets = 0;
        }
        if (item->crossInsets < 0) {
            item->crossInsets = 0;
        }

        /* The cross hypothetical: the declared cross size when there is one, else
         * the measured content cross size. It is only a *hypothesis* -- a stretch
         * replaces it once the line size is known -- and it is what the line sizes
         * are computed from. */
        declared = LayoutFlexDeclared(&declaredCross, containerCross, crossDefinite);
        item->crossHypothetical = declared >= 0
                                      ? declared
                                      : (rowMain ? (int32_t)intrinsic.maxContentHeight
                                                 : (int32_t)intrinsic.maxContent);
        if (item->crossHypothetical < 0) {
            item->crossHypothetical = 0;
        }
        item->crossSize = item->crossHypothetical;
        item->outerCross = item->crossHypothetical + item->crossInsets;

        itemCount++;
        child = childObject->NextSibling;
    }

    if (itemCount == 0u) {
        walk->block->emptyContainers++;
        *outExtent = 0;
        return RoboOk;
    }

    /* ---- 2. order the items (stable insertion sort) ---------------- */

    for (index = 1u; index < itemCount; index++) {
        LayoutFlexItem moving = items[index];
        size_t scan = index;

        while (scan > 0u && items[scan - 1u].order > moving.order) {
            items[scan] = items[scan - 1u];
            scan--;
        }
        items[scan] = moving;
    }
    for (index = 0u; index < itemCount; index++) {
        if (items[index].order != 0) {
            walk->block->flex.reordered++;
        }
    }

    /* ---- 3. break the items into lines ----------------------------- */

    lineStart[0] = 0u;
    lineCount = 1u;
    if (CssFlexWrapIsWrapped(wrap) && mainDefinite) {
        int64_t used = 0;

        for (index = 0u; index < itemCount; index++) {
            int32_t outer = items[index].mainSize + items[index].mainInsets;

            if (index > lineStart[lineCount - 1u] &&
                used + (int64_t)outer > (int64_t)containerMain) {
                lineStart[lineCount] = index;
                lineCount++;
                used = 0;
            }
            used += (int64_t)outer;
        }
    }
    lineStart[lineCount] = itemCount;
    for (index = 0u; index < itemCount; index++) {
        size_t line;

        for (line = 0u; line + 1u < lineCount; line++) {
            if (index >= lineStart[line] && index < lineStart[line + 1u]) {
                break;
            }
        }
        items[index].line = line;
    }

    /* ---- 4. resolve the flexible lengths per line ------------------ */

    for (index = 0u; index < lineCount; index++) {
        size_t first = lineStart[index];
        size_t last = lineStart[index + 1u];
        size_t count = last - first;
        size_t iter;

        for (iter = 0u; iter <= count; iter++) {
            int32_t used = 0;
            double factorSum = 0.0;
            int anyUnfrozen = 0;
            int32_t free;
            size_t i;

            for (i = first; i < last; i++) {
                LayoutFlexItem *item = &items[i];

                if (item->frozen) {
                    used += item->mainSize + item->mainInsets;
                } else {
                    used += item->baseMain + item->mainInsets;
                    anyUnfrozen = 1;
                }
            }
            free = (mainDefinite ? containerMain : used) - used;

            if (!mainDefinite || !anyUnfrozen) {
                for (i = first; i < last; i++) {
                    if (!items[i].frozen) {
                        items[i].mainSize = LayoutFlexClamp(items[i].baseMain,
                                                            items[i].minMain,
                                                            items[i].maxMain);
                        items[i].frozen = 1;
                    }
                }
                break;
            }

            for (i = first; i < last; i++) {
                if (!items[i].frozen) {
                    factorSum += (free > 0)
                                     ? items[i].grow
                                     : (items[i].shrink * (double)items[i].baseMain);
                }
            }
            if (factorSum <= 0.0) {
                for (i = first; i < last; i++) {
                    if (!items[i].frozen) {
                        items[i].mainSize = LayoutFlexClamp(items[i].baseMain,
                                                            items[i].minMain,
                                                            items[i].maxMain);
                        items[i].frozen = 1;
                    }
                }
                break;
            }

            {
                int violations = 0;

                for (i = first; i < last; i++) {
                    LayoutFlexItem *item = &items[i];
                    double factor;
                    double share;
                    int32_t target;
                    int32_t clamped;

                    if (item->frozen) {
                        continue;
                    }
                    factor = (free > 0) ? item->grow
                                        : (item->shrink * (double)item->baseMain);
                    share = (double)free * factor / factorSum;
                    target = (int32_t)LayoutPxRound((double)item->baseMain + share);
                    clamped = LayoutFlexClamp(target, item->minMain, item->maxMain);
                    if (clamped != target) {
                        /*
                         * A clamped item is **frozen** at its bound, and the freeze is
                         * the whole of CSS's "fix min/max violations" step: it leaves
                         * the redistribution, and the next pass divvies the remaining
                         * free space among the items that did not reach a bound. A
                         * build that only *counted* the violation -- as this one did
                         * before the min/max case existed -- would recompute the same
                         * clamped target every pass and never redistribute at all,
                         * which is a wrong answer that looks like a right one.
                         */
                        item->mainSize = clamped;
                        item->frozen = 1;
                        violations++;
                    } else {
                        item->mainSize = target;
                    }
                }

                if (violations == 0) {
                    for (i = first; i < last; i++) {
                        items[i].frozen = 1;
                    }
                    break;
                }
            }
        }
    }
    for (index = 0u; index < itemCount; index++) {
        items[index].outerMain = items[index].mainSize + items[index].mainInsets;
        if (items[index].grow > 0.0) {
            walk->block->flex.growItems++;
        }
        if (items[index].contentMin) {
            walk->block->flex.contentMinItems++;
        }
    }

    /* ---- 5. lay the items' contents out ---------------------------- */

    /*
     * An item's content towards the cross axis is what its children turn out to
     * occupy, and that needs a width to lay them out at. For a row, the main size is
     * the width and is already resolved. For a column the cross is the width, and it
     * is the declared one, a stretch to the container (which is definite), or the
     * item's measured content width capped at the container.
     */
    for (index = 0u; index < itemCount; index++) {
        LayoutFlexItem *item = &items[index];
        const LayoutObject *itemObject = LayoutObjectSlotConst(walk->tree, item->id);
        GfxPixelY contentExtent = 0;

        if (!itemObject) {
            return RoboLayoutDiag(walk->diag, RoboLayoutDomainFlex, RoboError,
                                  "a flex item vanished under the pass");
        }

        if (rowMain) {
            item->contentW = item->mainSize;
        } else {
            LayoutDimension declaredWidth = LayoutDimensionAuto();
            int32_t width;

            (void)LayoutSizingReadDimension(itemObject->style, "width",
                                            &declaredWidth);
            width = LayoutFlexDeclared(&declaredWidth, containerCross, 1);
            if (width < 0) {
                LayoutIntrinsicSize intrinsic;

                LayoutFlexIntrinsic(walk->tree, item->id, walk->textMetrics,
                                    &intrinsic);
                width = (int32_t)intrinsic.maxContent;
                if (width > containerCross - item->crossInsets) {
                    width = containerCross - item->crossInsets;
                }
                if (width < 0) {
                    width = 0;
                }
            }
            item->contentW = width;
            item->crossHypothetical = width;
            item->outerCross = width + item->crossInsets;
        }

        if (LayoutBlockWalkChildren(walk, item->id, item->contentW,
                                    &contentExtent) != RoboOk) {
            return walk->diag ? walk->diag->code : RoboError;
        }

        if (rowMain) {
            /* The height is only this item's content when nothing declares it; a
             * declared height was already the hypothesis. */
            LayoutDimension declaredHeight = LayoutDimensionAuto();
            int32_t height;

            (void)LayoutSizingReadDimension(itemObject->style, "height",
                                            &declaredHeight);
            height = LayoutFlexDeclared(&declaredHeight, containerCross,
                                        crossDefinite);
            if (height < 0) {
                item->crossHypothetical = (int32_t)contentExtent;
                item->outerCross = item->crossHypothetical + item->crossInsets;
            }
        }
    }

    /* ---- 5b. each item's resolved alignment and baseline ----------- */

    /*
     * One read of `align-self` per item, here, so that the line sizing below, the
     * baseline pass and the placement all consult the same answer -- the item's
     * alignment is a fact about the item, and reading it three times in three
     * places is three chances to disagree.
     *
     * The baseline itself comes now, and now is forced: a baseline is a statement
     * about the item's contents, and the contents were laid out in step 5. A `row`
     * alone consults it -- its cross axis is the block one, which is where a
     * baseline lives.
     */
    for (index = 0u; index < itemCount; index++) {
        LayoutFlexItem *item = &items[index];
        const LayoutObject *itemObject =
            LayoutObjectSlotConst(walk->tree, item->id);
        CssFlexAlign self = RoboCssFlexAlignAuto;

        item->align = itemsAlign;
        if (itemObject) {
            self = LayoutFlexAlignOf(itemObject->style, "align-self",
                                     RoboCssFlexAlignAuto);
        }
        if (self != RoboCssFlexAlignAuto) {
            item->align = self;
        }
        if (rowMain && item->align == RoboCssFlexAlignBaseline) {
            item->baselineAligned = 1;
            walk->block->flex.baselineItems++;
            item->baseline = LayoutFlexItemBaseline(walk, item, rowMain,
                                                    &item->baselineFromChild);
        }
    }

    /* ---- 6. line cross sizes, align-content, stretch -------------- */

    for (index = 0u; index < lineCount; index++) {
        size_t first = lineStart[index];
        size_t last = lineStart[index + 1u];
        size_t i;
        int32_t cross = 0;
        int32_t baselineMax = 0;
        int32_t descentMax = 0;

        for (i = first; i < last; i++) {
            if (items[i].outerCross > cross) {
                cross = items[i].outerCross;
            }
            /*
             * A baseline-aligned line has to be tall enough for the line's own
             * baseline plus the deepest descent below it, and that can exceed every
             * item's cross size: two items whose baselines line up occupy more room
             * than either alone whenever their cross sizes differ.
             */
            if (items[i].baselineAligned) {
                int32_t descent = items[i].outerCross - items[i].baseline;

                if (items[i].baseline > baselineMax) {
                    baselineMax = items[i].baseline;
                }
                if (descent > descentMax) {
                    descentMax = descent;
                }
            }
        }
        if (baselineMax + descentMax > cross) {
            cross = baselineMax + descentMax;
        }
        lineCross[index] = cross;
        lineBaseline[index] = baselineMax;
    }

    {
        int32_t totalCross = 0;
        int32_t start = 0;
        int32_t gap = 0;
        int stretchLines = crossDefinite &&
                           (contentAlign == RoboCssFlexAlignNormal ||
                            contentAlign == RoboCssFlexAlignStretch);

        for (index = 0u; index < lineCount; index++) {
            totalCross += lineCross[index];
        }

        if (stretchLines && containerCross > totalCross) {
            int32_t extra = (containerCross - totalCross) / (int32_t)lineCount;

            for (index = 0u; index < lineCount; index++) {
                lineCross[index] += extra;
            }
            totalCross = containerCross;
        } else if (crossDefinite) {
            int32_t free = containerCross - totalCross;

            switch (contentAlign) {
            case RoboCssFlexAlignFlexEnd:
            case RoboCssFlexAlignEnd:
            case RoboCssFlexAlignRight:
                start = free;
                break;
            case RoboCssFlexAlignCenter:
                start = free / 2;
                break;
            case RoboCssFlexAlignSpaceBetween:
                if (lineCount > 1u) {
                    gap = free / (int32_t)(lineCount - 1u);
                }
                break;
            case RoboCssFlexAlignSpaceAround:
                gap = free / (int32_t)lineCount;
                start = gap / 2;
                break;
            case RoboCssFlexAlignSpaceEvenly:
                gap = free / (int32_t)(lineCount + 1u);
                start = gap;
                break;
            default:
                break;
            }
        }

        {
            int32_t cursor = start;

            for (index = 0u; index < lineCount; index++) {
                lineOffset[index] = cursor;
                cursor += lineCross[index] + gap;
            }
        }

        /* `wrap-reverse` swaps the cross-axis start and end: the line order is
         * mirrored within the cross extent, and the items are untouched. */
        if (crossReversed && lineCount > 0u) {
            int32_t extent = totalCross + gap * (int32_t)(lineCount - 1u);

            for (index = 0u; index < lineCount; index++) {
                lineOffset[index] = extent - lineOffset[index] - lineCross[index];
            }
        }
    }

    for (index = 0u; index < itemCount; index++) {
        LayoutFlexItem *item = &items[index];
        const LayoutObject *itemObject = LayoutObjectSlotConst(walk->tree, item->id);
        LayoutDimension declaredCross = LayoutDimensionAuto();
        int32_t declared;

        if (!itemObject) {
            return RoboLayoutDiag(walk->diag, RoboLayoutDomainFlex, RoboError,
                                  "a flex item vanished under the pass");
        }

        (void)LayoutSizingReadDimension(itemObject->style,
                                        rowMain ? "height" : "width",
                                        &declaredCross);
        declared = LayoutFlexDeclared(&declaredCross, containerCross, crossDefinite);

        /*
         * An `auto` cross margin claims the free space, so the item must not also
         * stretch into it: CSS Flexbox §8.1 puts auto margins *ahead* of the
         * alignment properties, and `stretch` is one of them. An item that asked
         * for the space gets its hypothetical size and the margin does the rest.
         */
        if ((item->align == RoboCssFlexAlignNormal ||
             item->align == RoboCssFlexAlignStretch) &&
            declared < 0 && !item->autoCrossBefore && !item->autoCrossAfter) {
            int32_t stretched = lineCross[item->line] - item->crossInsets;

            if (stretched < 0) {
                stretched = 0;
            }
            item->crossSize = stretched;
            item->stretch = 1;
            walk->block->flex.stretched++;
        } else {
            item->crossSize = item->crossHypothetical;
        }
        item->outerCross = item->crossSize + item->crossInsets;
    }

    /* ---- 7. main-axis and cross-axis positions --------------------- */

    for (index = 0u; index < lineCount; index++) {
        size_t first = lineStart[index];
        size_t last = lineStart[index + 1u];
        size_t count = last - first;
        size_t i;
        int32_t usedMain = 0;
        int32_t free;
        int32_t start = 0;
        int32_t gap = 0;
        int32_t autoCount = 0;
        int32_t autoExtra = 0;
        int32_t cursor;

        for (i = first; i < last; i++) {
            usedMain += items[i].outerMain;
        }
        free = (mainDefinite ? containerMain : usedMain) - usedMain;

        /*
         * An `auto` main-axis margin absorbs the free space **before**
         * `justify-content` is consulted -- that *is* the alignment step's first
         * sub-step (Flexbox §8.1), and the reason an `auto` margin on a flex item is
         * not the same thing as one on a block box. The space is split evenly
         * between the auto margins that asked for it, and `justify-content` is then
         * not consulted at all.
         */
        if (free > 0) {
            for (i = first; i < last; i++) {
                if (items[i].autoMainBefore) {
                    autoCount++;
                }
                if (items[i].autoMainAfter) {
                    autoCount++;
                }
            }
            if (autoCount > 0) {
                autoExtra = free / autoCount;
            }
        }

        if (free > 0 && autoCount == 0) {
            switch (justify) {
            case RoboCssFlexAlignFlexEnd:
            case RoboCssFlexAlignEnd:
            case RoboCssFlexAlignRight:
                start = free;
                break;
            case RoboCssFlexAlignCenter:
                start = free / 2;
                break;
            case RoboCssFlexAlignSpaceBetween:
                if (count > 1u) {
                    gap = free / (int32_t)(count - 1u);
                }
                break;
            case RoboCssFlexAlignSpaceAround:
                gap = free / (int32_t)count;
                start = gap / 2;
                break;
            case RoboCssFlexAlignSpaceEvenly:
                gap = free / (int32_t)(count + 1u);
                start = gap;
                break;
            default:
                break;
            }
        }

        cursor = start;
        for (i = first; i < last; i++) {
            LayoutFlexItem *item = &items[i];
            int32_t itemFree;
            int32_t crossPos;
            int32_t mainPos;

            /*
             * The absorbed space belongs **inside** the item's margin box -- that is
             * what an `auto` margin is -- so it grows the item's outer main size
             * rather than moving the cursor past any extra room, and the position the
             * cursor reports is the margin box's origin either way. The border box
             * lands `margins.start` further in, because step 8 adds the start margin.
             */
            if (autoExtra > 0) {
                if (item->autoMainBefore) {
                    LayoutFlexAbsorbMargin(item, rowMain, mainReversed, 1, autoExtra);
                    item->outerMain += autoExtra;
                    item->autoAbsorbed = 1;
                }
                if (item->autoMainAfter) {
                    LayoutFlexAbsorbMargin(item, rowMain, mainReversed, 0, autoExtra);
                    item->outerMain += autoExtra;
                    item->autoAbsorbed = 1;
                }
            }
            mainPos = cursor;
            cursor += item->outerMain + gap;

            if (mainReversed) {
                int32_t total = mainDefinite ? containerMain : usedMain;

                mainPos = total - mainPos - item->outerMain;
            }

            itemFree = lineCross[item->line] - item->outerCross;
            if (itemFree > 0 &&
                (item->autoCrossBefore || item->autoCrossAfter)) {
                /*
                 * The cross-axis half of the same rule: the free space along the
                 * item's line goes to its `auto` cross margins, so the item sits at
                 * the line's cross start and its margin box grows to fill the line.
                 * `align-self` is not consulted, which is what "auto margins take
                 * precedence over the alignment properties" means concretely.
                 */
                int32_t share = (item->autoCrossBefore && item->autoCrossAfter)
                                    ? itemFree / 2
                                    : itemFree;

                crossPos = lineOffset[item->line];
                if (item->autoCrossBefore) {
                    LayoutFlexAbsorbCrossMargin(item, rowMain, 1, share);
                }
                if (item->autoCrossAfter) {
                    LayoutFlexAbsorbCrossMargin(
                        item, rowMain, 0,
                        item->autoCrossBefore ? itemFree - share : share);
                }
                item->autoAbsorbed = 1;
            } else if (item->baselineAligned) {
                /* The line's baseline is the deepest one on it, and every
                 * baseline-aligned item sits so that its own baseline lands there. */
                crossPos = lineOffset[item->line] +
                           (lineBaseline[item->line] - item->baseline);
            } else {
                switch (item->align) {
                case RoboCssFlexAlignCenter:
                    crossPos = lineOffset[item->line] + itemFree / 2;
                    break;
                case RoboCssFlexAlignFlexEnd:
                case RoboCssFlexAlignEnd:
                case RoboCssFlexAlignRight:
                    crossPos = lineOffset[item->line] + itemFree;
                    break;
                default:
                    crossPos = lineOffset[item->line];
                    break;
                }
            }

            if (rowMain) {
                item->positionX = mainPos;
                item->positionY = crossPos;
            } else {
                item->positionX = crossPos;
                item->positionY = mainPos;
            }
        }
    }

    /*
     * The counter is per **item**, not per absorbed side, so it is totalled once
     * here rather than incremented at each of the three places an `auto` margin can
     * claim its space.
     */
    for (index = 0u; index < itemCount; index++) {
        if (items[index].autoAbsorbed) {
            walk->block->flex.autoMargins++;
        }
    }

    /* ---- 8. write each item --------------------------------------- */

    for (index = 0u; index < itemCount; index++) {
        LayoutFlexItem *item = &items[index];
        LayoutObject *itemObject = LayoutObjectSlot(walk->tree, item->id);
        LayoutSize contentSize;
        LayoutSize paddingBox;
        LayoutSize size;
        LayoutPoint origin;
        LayoutOverflowEdges overflow;
        uint32_t flags = 0u;

        if (!itemObject) {
            return RoboLayoutDiag(walk->diag, RoboLayoutDomainFlex, RoboError,
                                  "a flex item vanished under the pass");
        }

        contentSize.width = rowMain ? item->mainSize : item->crossSize;
        contentSize.height = rowMain ? item->crossSize : item->mainSize;

        if (LayoutPaddingEdgesPaddingBox(&item->padding, contentSize, &paddingBox,
                                         walk->diag) != RoboOk) {
            return walk->diag ? walk->diag->code : RoboError;
        }
        if (LayoutBorderEdgesBorderBox(&item->borders, paddingBox, &size,
                                       walk->diag) != RoboOk) {
            return walk->diag ? walk->diag->code : RoboError;
        }

        /* The border box's origin: the item's margin origin, which is where the
         * alignment put its margin box, plus its start margins. */
        origin.x = LayoutExtentClamp((int64_t)item->positionX +
                                     (int64_t)item->margins.left);
        origin.y = LayoutExtentClamp((int64_t)item->positionY +
                                     (int64_t)item->margins.top);

        itemObject->margins = item->margins;
        itemObject->padding = item->padding;
        itemObject->borders = item->borders;
        LayoutPositionOffsetInit(&itemObject->positionOffset);

        /*
         * The baseline the item was **placed** with, recorded here because this is
         * where the placement happens and because a baseline is by definition a
         * placement fact (RoboLayoutBaseline.h). It is a `row`'s vertical quantity,
         * so a column records none -- a horizontal "ascent" would be a second
         * convention for one word, and 19.9/19.10 own the inline axis.
         *
         * The extent is the item's outer cross size: TEXT when it came from a
         * measured child run's font baseline, BOX (the bottom-edge fallback) when it
         * did not, which are the two kinds this module's baseline model has.
         */
        if (rowMain) {
            int32_t ascent = item->baseline;
            int32_t descent = item->outerCross - item->baseline;

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
        walk->block->flex.itemsPlaced++;
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

    walk->block->flex.lines += lineCount;

    /* The container's content extent: the cross total for a row (vertical) and the
     * main total for a column (also vertical). */
    if (rowMain) {
        int32_t total = 0;

        for (index = 0u; index < lineCount; index++) {
            total += lineCross[index];
        }
        *outExtent = (GfxPixelY)LayoutExtentClamp(total);
    } else {
        int32_t total = 0;

        for (index = 0u; index < itemCount; index++) {
            total += items[index].outerMain;
        }
        if (mainDefinite && containerMain > total) {
            total = containerMain;
        }
        *outExtent = (GfxPixelY)LayoutExtentClamp(total);
    }
    return RoboOk;
}
