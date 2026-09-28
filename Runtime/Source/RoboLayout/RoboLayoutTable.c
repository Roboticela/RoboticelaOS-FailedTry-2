/*
 * RoboLayout table layout (Phase 19.6) — see RoboLayoutTable.h and TableLayout.md.
 *
 * One pass, and it is the fifth formatting context this build lays out. The block pass
 * reaches it exactly as it reaches the other four: a box whose *owned* context is
 * `RoboLayoutFcTable` hands its contents to `LayoutTableWalkContents` instead of stacking
 * them (RoboLayoutBlock.c), and this file places every row, cell and caption -- writing
 * each one's result, its box-model records, its overflow and its contents.
 *
 * The algorithm, in the order CSS 2.1 §17 states it, which is the whole of the
 * implementation:
 *
 *   1. read the container's declarations -- `table-layout`, `border-collapse`,
 *      `border-spacing`, `caption-side` and `empty-cells`, as the computed values 18.5
 *      types;
 *   2. build the **row and cell model**, applying the fixup: a caption, the
 *      `table-column` boxes and the rows -- with a cell that is a direct child of the
 *      table wrapped in an **anonymous row**. Everything after this step reads the model
 *      rather than the tree, which is what makes a table a table;
 *   3. lay the **caption** out at the container's content width, because its extent is
 *      what the rows start below;
 *   4. size the **columns** as a function of the cells in them: `table-layout: fixed`
 *      takes the width the first row (or a column box) declared and shares what is left
 *      equally among the columns that declared none, and `table-layout: auto` takes a
 *      floor from the widest cell's min-content contribution and a ceiling from its
 *      max-content one, then distributes the container's width over the columns that can
 *      still grow;
 *   5. lay each **cell's** contents out at the width its column gave it, and take the
 *      row's height from the tallest cell in it -- the step that makes a table a table,
 *      and the reason the two axes are resolved in this order: a row's height is a
 *      function of contents that do not exist until the columns are sized;
 *   6. place every row, group, cell and caption, and report the extent the contents
 *      reached.
 *
 * The fixup is the part worth stating twice: CSS *generates* boxes and this build does
 * not, so what it does instead is *read* the shapes CSS would generate. A cell directly
 * inside a table is placed as a row of one (that one is real work and is counted); a row
 * group contributes its rows in document order; a column or column group contributes a
 * declared width and generates no geometry; and a child that is neither -- a text run, or
 * a box whose display is not a table-internal one -- is counted rather than wrapped in an
 * anonymous cell, exactly as the grid pass counts the anonymous grid item it does not
 * build.
 *
 * A row and a row group are written with **no** insets of their own: CSS's table model
 * ignores a row's padding, border and margin for the geometry, and a build that gave them
 * a box model of their own would place its cells relative to a number no other
 * implementation agrees about. The declarations are still read by nothing here, which is
 * stated as a non-goal rather than left to be discovered.
 *
 * What this file deliberately does not do is named in RoboLayoutTable.h and in
 * `Documentation/Runtime/Layout/TableLayout.md` §6: no `colspan`/`rowspan` (HTML
 * attributes, not CSS properties), no collapsing-border arithmetic, no `vertical-align`
 * on a cell, no `visibility: collapse`, no `caption-side: inline-*`, no anonymous cell
 * generation and no vertical writing mode.
 */

#include "RoboLayout/RoboLayoutTable.h"

#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSDisplay.h"
#include "RoboCSS/RoboCSSTable.h"
#include "RoboCSS/RoboCSSToken.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSUnit.h"
#include "RoboCSS/RoboCSSValue.h"

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
 * Cells one table's model holds without allocating.
 *
 * A fixed cap rather than a growing arena, for the reason the flex and grid passes give:
 * a table is a *page's* set of cells and a document with more than a hundred and a few of
 * them is an authoring mistake rather than a case to optimise. A cell beyond the cap is
 * counted as skipped (`table.itemsSkipped`) rather than dropped silently.
 */
#define LAYOUT_TABLE_MAX_CELLS 128u

/** Rows one table's model holds. A row beyond it is counted as skipped. */
#define LAYOUT_TABLE_MAX_ROWS 32u

/** Columns one table may have, declared or discovered. */
#define LAYOUT_TABLE_MAX_COLUMNS 64u

/** The largest column width the pass may resolve. */
#define LAYOUT_TABLE_SIZE_MAX ((int32_t)LAYOUT_STYLE_LENGTH_MAX)

/* ------------------------------------------------------------------ */
/* The counters record                                                 */
/* ------------------------------------------------------------------ */

void LayoutTableCountersInit(LayoutTableCounters *counters)
{
    if (!counters) {
        return;
    }
    memset(counters, 0, sizeof(*counters));
}

int LayoutTableCountersEqual(const LayoutTableCounters *a,
                             const LayoutTableCounters *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->containers == b->containers &&
           a->rows == b->rows &&
           a->groups == b->groups &&
           a->columns == b->columns &&
           a->columnBoxes == b->columnBoxes &&
           a->cellsPlaced == b->cellsPlaced &&
           a->cellsEmpty == b->cellsEmpty &&
           a->captions == b->captions &&
           a->anonymousRows == b->anonymousRows &&
           a->itemsSkipped == b->itemsSkipped &&
           a->contentColumns == b->contentColumns &&
           a->fixedColumns == b->fixedColumns &&
           a->spacedTables == b->spacedTables &&
           a->collapsedTables == b->collapsedTables &&
           a->overflowingCells == b->overflowingCells;
}

/* ------------------------------------------------------------------ */
/* The container's declarations                                        */
/* ------------------------------------------------------------------ */

static const CssComputedValue *LayoutTableProperty(const CssComputedStyle *style,
                                                   const char *property)
{
    if (!style) {
        return NULL;
    }
    return CssComputedStyleComputedByName(style, CssSliceFromText(property));
}

/** `table-layout`, or `auto` -- the property's own initial value. */
static CssTableLayout LayoutTableLayoutOf(const CssComputedStyle *style)
{
    const CssComputedValue *value = LayoutTableProperty(style, "table-layout");
    CssSlice text;

    if (!value || value->kind != RoboCssComputedValueKeyword) {
        return RoboCssTableLayoutAuto;
    }
    text = CssComputedValueText(value);
    if (!CssTableLayoutSliceIsKnown(text)) {
        return RoboCssTableLayoutAuto;
    }
    return CssTableLayoutFromSlice(text);
}

/**
 * `border-collapse`, or `separate`, and the two lengths of `border-spacing`.
 *
 * The three are read together because CSS states one rule over all of them: a collapsing
 * table's `border-spacing` does not apply, so the gutters are zero whatever the
 * declaration says. `border-spacing` itself is **carried** rather than typed -- 18.5
 * preserves it, because two lengths is not one of the closed vocabularies -- so this is a
 * read of the preserved components and not a re-parse: one length is both axes and two are
 * the inline and block ones in order, which is CSS's own grammar.
 */
static void LayoutTableSpacingOf(const CssComputedStyle *style,
                                 int32_t *outColumnGap,
                                 int32_t *outRowGap,
                                 int *outCollapsed)
{
    const CssComputedValue *value = LayoutTableProperty(style, "border-collapse");
    const CssComputedValue *spacing = LayoutTableProperty(style, "border-spacing");
    const CssValue *preserved;
    int32_t values[2] = { 0, 0 };
    size_t count = 0u;
    size_t index;
    size_t seen = 0u;

    *outColumnGap = 0;
    *outRowGap = 0;
    *outCollapsed = 0;
    if (value && value->kind == RoboCssComputedValueKeyword &&
        CssBorderCollapseSliceIsKnown(CssComputedValueText(value))) {
        *outCollapsed =
            CssBorderCollapseBordersAdjoin(
                CssBorderCollapseFromSlice(CssComputedValueText(value)));
    }
    if (*outCollapsed) {
        /* CSS's own rule, and the reason the two are read together. */
        return;
    }
    if (!spacing || spacing->kind != RoboCssComputedValuePreserved) {
        return;
    }
    preserved = CssComputedValuePreserved(spacing);
    if (!preserved) {
        return;
    }
    count = CssValueChildCount(preserved);
    for (index = 0u; index < count && seen < 2u; index++) {
        const CssValue *component = CssValueChild(preserved, index);
        CssNumeric numeric;
        double px = 0.0;

        if (!component) {
            continue;
        }
        if (component->kind == RoboCssValueKindToken &&
            (component->token.type == RoboCssTokenWhitespace ||
             component->token.type == RoboCssTokenComment)) {
            continue;
        }
        if (component->kind != RoboCssValueKindToken ||
            CssNumericFromValue(component, &numeric) !=
                RoboCssNumericParseResolved ||
            numeric.kind != RoboCssNumericLength) {
            /* Anything that is not a length -- including the bare `0` the grammar allows
             * as a number -- makes the pair unreadable, so both axes stay zero, which is
             * what a table with no spacing has and cannot be wrong about a length nobody
             * gave. */
            continue;
        }
        if (CssUnitResolvePx(numeric.number, numeric.unit, NULL, &px) !=
            RoboCssLengthResolved) {
            continue;
        }
        values[seen] = px < 0.0 ? 0 : LayoutPxRound(px);
        seen++;
    }
    if (seen == 0u) {
        return;
    }
    *outColumnGap = values[0];
    *outRowGap = seen > 1u ? values[1] : values[0];
}

/**
 * Whether `caption-side` puts the caption **after** the table's rows.
 *
 * A table in this build has one caption edge per side of its **block** axis, which is what
 * `caption-side` names; the two inline spellings are read and placed as the block-start
 * edge, because this build's tables have no inline caption edges and a caption nobody can
 * see is worse than one on the edge the property's own initial value names. That is a
 * named non-goal rather than a silent fallback.
 */
static int LayoutTableCaptionIsAfter(const CssComputedStyle *style)
{
    const CssComputedValue *value = LayoutTableProperty(style, "caption-side");
    CssSlice text;

    if (!value || value->kind != RoboCssComputedValueKeyword) {
        return 0;
    }
    text = CssComputedValueText(value);
    if (!CssCaptionSideSliceIsKnown(text)) {
        return 0;
    }
    switch (CssCaptionSideFromSlice(text)) {
    case RoboCssCaptionSideBottom:
    case RoboCssCaptionSideBlockEnd:
        return 1;
    default:
        return 0;
    }
}

/** A declared dimension in px against @p base, or -1 when there is none. */
static int32_t LayoutTableDeclaredPx(const LayoutDimension *dimension,
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

/* ------------------------------------------------------------------ */
/* A box's used box model                                              */
/* ------------------------------------------------------------------ */

/** The four records a table box is placed with, and the two insets they add up to. */
typedef struct LayoutTableBoxModel {
    LayoutMarginEdges margins;
    LayoutPaddingEdges padding;
    LayoutBorderEdges borders;
    int32_t inlineInsets;
    int32_t blockInsets;
    int32_t declaredW;
    int32_t declaredH;
} LayoutTableBoxModel;

/**
 * Resolve @p object's box model against @p contentWidth.
 *
 * The same resolution every other direction makes, through the same four layers: the
 * margins through 10.9, the padding through 10.10, the border through 10.11 and the two
 * declared dimensions through 10.8. A percentage resolves against the container's content
 * width, which is CSS's rule for every box's own declarations and the one place a table
 * cell's percentages are *stated* here: a cell's containing block is its column, and this
 * pass hands the cell the width the column resolved to before its contents are laid out,
 * so `width: 50%` on a cell means half its column.
 */
static Result LayoutTableBoxModelRead(LayoutWalk *walk,
                                      const LayoutObject *object,
                                      int32_t contentWidth,
                                      LayoutTableBoxModel *out)
{
    LayoutMargins declaredMargins;
    CssBoxSide sides[RoboCssBoxSideCount] = {
        RoboCssBoxSideTop, RoboCssBoxSideRight,
        RoboCssBoxSideBottom, RoboCssBoxSideLeft
    };
    GfxPixelX used[RoboCssBoxSideCount] = { 0, 0, 0, 0 };
    LayoutDimension declared = LayoutDimensionAuto();
    size_t side;

    memset(out, 0, sizeof(*out));
    out->declaredW = -1;
    out->declaredH = -1;
    if (!object) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainTable, RoboError,
                              "a table box is not in the tree");
    }

    LayoutMarginsInit(&declaredMargins);
    declaredMargins = LayoutMarginsOf(object->style);
    out->margins.availableContentWidth = contentWidth;
    for (side = 0u; side < RoboCssBoxSideCount; side++) {
        LayoutMargin margin = LayoutMarginAbsent();

        if (LayoutMarginsSide(&declaredMargins, sides[side], &margin) != RoboOk) {
            continue;
        }
        if (margin.kind == RoboLayoutMarginResolved) {
            used[side] = margin.length;
            out->margins.resolved++;
        } else if (margin.kind == RoboLayoutMarginPercentage) {
            used[side] = LayoutPxRound(margin.percentage / 100.0 *
                                       (double)contentWidth);
            out->margins.resolved++;
        } else if (margin.kind == RoboLayoutMarginUnresolved) {
            out->margins.unresolved++;
        }
        /* An `auto` margin on a table box is not CSS: read, and used as zero. */
    }
    out->margins.top = used[RoboCssBoxSideTop];
    out->margins.right = used[RoboCssBoxSideRight];
    out->margins.bottom = used[RoboCssBoxSideBottom];
    out->margins.left = used[RoboCssBoxSideLeft];

    {
        LayoutPaddings declaredPadding = LayoutPaddingsOf(object->style);
        LayoutBorders declaredBorders = LayoutBordersOf(object->style);

        if (LayoutPaddingResolve(&declaredPadding, contentWidth, &out->padding,
                                 walk->diag) != RoboOk) {
            return walk->diag ? walk->diag->code : RoboError;
        }
        if (LayoutBorderResolve(&declaredBorders, &out->borders, walk->diag) !=
            RoboOk) {
            return walk->diag ? walk->diag->code : RoboError;
        }
    }
    out->inlineInsets = out->margins.left + out->margins.right +
                        out->borders.left.width + out->borders.right.width +
                        out->padding.left + out->padding.right;
    out->blockInsets = out->margins.top + out->margins.bottom +
                       out->borders.top.width + out->borders.bottom.width +
                       out->padding.top + out->padding.bottom;

    (void)LayoutSizingReadDimension(object->style, "width", &declared);
    out->declaredW = LayoutTableDeclaredPx(&declared, contentWidth, 1);
    declared = LayoutDimensionAuto();
    (void)LayoutSizingReadDimension(object->style, "height", &declared);
    out->declaredH = LayoutTableDeclaredPx(&declared, 0, 0);
    return RoboOk;
}

/**
 * Write one placed box: its records, its geometry, its overflow and its result flags.
 *
 * The one door every box this pass places goes through -- a cell, a row, a row group and a
 * caption -- which is what keeps the block record's counters honest across all four: each
 * placement adds one `blocksPlaced`, its own margin/padding/border counts and its own
 * overflow verdict, whatever the box is.
 */
static Result LayoutTableWriteBox(LayoutWalk *walk,
                                  LayoutNodeId id,
                                  const LayoutTableBoxModel *model,
                                  int32_t x,
                                  int32_t y,
                                  int32_t contentW,
                                  int32_t contentH)
{
    LayoutObject *object = LayoutObjectSlot(walk->tree, id);
    LayoutSize contentSize;
    LayoutSize paddingBox;
    LayoutSize size;
    LayoutPoint origin;
    LayoutOverflowEdges overflow;
    uint32_t flags = 0u;

    if (!object) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainTable, RoboError,
                              "a placed table box is not in the tree");
    }
    contentSize.width = contentW;
    contentSize.height = contentH;
    if (LayoutPaddingEdgesPaddingBox(&model->padding, contentSize, &paddingBox,
                                     walk->diag) != RoboOk) {
        return walk->diag ? walk->diag->code : RoboError;
    }
    if (LayoutBorderEdgesBorderBox(&model->borders, paddingBox, &size,
                                   walk->diag) != RoboOk) {
        return walk->diag ? walk->diag->code : RoboError;
    }
    origin.x = LayoutExtentClamp((int64_t)x + (int64_t)model->margins.left);
    origin.y = LayoutExtentClamp((int64_t)y + (int64_t)model->margins.top);

    object->margins = model->margins;
    object->padding = model->padding;
    object->borders = model->borders;
    LayoutPositionOffsetInit(&object->positionOffset);
    LayoutBaselineMetricsInit(&object->baseline);

    if (LayoutResultSetGeometry(&object->result, origin, size, walk->diag) != RoboOk) {
        return walk->diag ? walk->diag->code : RoboError;
    }
    if (LayoutOverflowMeasure(walk->tree, object, &model->borders, &model->padding,
                              size, &overflow, walk->diag) != RoboOk) {
        return walk->diag ? walk->diag->code : RoboError;
    }
    object->overflow = overflow;
    if (LayoutOverflowEdgesHasOverflowAxis(&overflow,
                                           RoboLayoutOverflowAxisHorizontal)) {
        flags |= RoboLayoutResultFlagOverflowX;
    }
    if (LayoutOverflowEdgesHasOverflowAxis(&overflow,
                                           RoboLayoutOverflowAxisVertical)) {
        flags |= RoboLayoutResultFlagOverflowY;
    }
    (void)LayoutResultSetFlags(&object->result, flags, NULL);

    walk->block->blocksPlaced++;
    walk->block->marginsApplied += model->margins.resolved;
    walk->block->marginsUnresolved += model->margins.unresolved;
    walk->block->paddingsApplied += model->padding.resolved;
    walk->block->paddingsUnresolved += model->padding.unresolved;
    walk->block->bordersApplied += model->borders.widthsResolved;
    walk->block->bordersUnresolved += model->borders.widthsUnresolved;
    walk->block->bordersDrawn += model->borders.bordersDrawn;
    walk->block->overflowClipped +=
        LayoutOverflowEdgesClipsAny(&overflow) ? 1u : 0u;
    walk->block->overflowing +=
        LayoutOverflowEdgesHasOverflow(&overflow) ? 1u : 0u;
    walk->block->overflowUnresolved += overflow.axesUnresolved;
    if (object->depth > walk->block->depthMax) {
        walk->block->depthMax = object->depth;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The row and cell model                                             */
/* ------------------------------------------------------------------ */

/** One cell slot of a table's model. */
typedef struct LayoutTableCell {
    /** The cell box. A slot always has one: a hole CSS generates for a row's missing
     *  cell is a box this build does not make, so it is not in the model either. */
    LayoutNodeId id;
    /** The column the cell occupies -- its index in the row. */
    size_t column;
    LayoutTableBoxModel box;
    int32_t contentW;
    int32_t contentH;
    /** Non-zero when the cell has no in-flow contents (`empty-cells`'s subject). */
    int empty;
} LayoutTableCell;

/** One row of a table's model: a run of cell slots, and the box that owned it. */
typedef struct LayoutTableRow {
    /** The row box, or `RoboLayoutNodeIdNone` for an **anonymous** row -- one the fixup
     *  generated because a cell was a direct child of the table. */
    LayoutNodeId id;
    /** The row's **group** box, when the row came out of one. */
    LayoutNodeId group;
    size_t firstCell;
    size_t cellCount;
    int32_t height;
} LayoutTableRow;

/** Everything one table's model holds, on the pass's stack. */
typedef struct LayoutTableModel {
    LayoutTableCell cells[LAYOUT_TABLE_MAX_CELLS];
    LayoutTableRow rows[LAYOUT_TABLE_MAX_ROWS];
    size_t cellCount;
    size_t rowCount;
    LayoutNodeId caption;
    /** The caption's content extent, read in step 3 because the rows start below it
     *  (or end above it) and the offsets are step 6's. */
    int32_t captionExtent;
} LayoutTableModel;

/** Non-zero when @p object has no in-flow box child -- what `empty-cells` asks about. */
static int LayoutTableObjectIsEmpty(const LayoutWalk *walk,
                                    const LayoutObject *object)
{
    LayoutNodeId child;

    if (!object) {
        return 1;
    }
    child = object->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree, child);
        LayoutNodeId next;

        if (!childObject) {
            break;
        }
        next = childObject->NextSibling;
        if (childObject->kind == RoboLayoutObjectBox) {
            return 0;
        }
        child = next;
    }
    return 1;
}

/** Skip a child the model cannot use, counting it the way the block record expects. */
static void LayoutTableSkip(LayoutWalk *walk)
{
    walk->block->table.itemsSkipped++;
    walk->block->inlineLevelSkipped++;
}

/**
 * Open one row and read its cells.
 *
 * Every path into the model ends here, which is what keeps the two arrays consistent: a row
 * is appended and its cell run begins at the cell count *before* its cells, so
 * `firstCell`/`cellCount` are exact for every row however it was reached -- a row box, one
 * of a group's rows, or an anonymous row the fixup generated for a row-less cell.
 *
 * @p anonymousCell is the cell of an anonymous row, or `RoboLayoutNodeIdNone` for an
 * ordinary row whose cells come from @p rowId's children.
 */
static Result LayoutTableReadRow(LayoutWalk *walk,
                                 LayoutTableModel *model,
                                 LayoutNodeId rowId,
                                 LayoutNodeId group,
                                 LayoutNodeId anonymousCell,
                                 int32_t contentWidth,
                                 size_t *outColumns)
{
    LayoutTableRow *row;

    if (model->rowCount >= LAYOUT_TABLE_MAX_ROWS) {
        return RoboOk; /* the caller counts it; the model keeps what it can hold */
    }
    row = &model->rows[model->rowCount];
    row->id = rowId;
    row->group = group;
    row->firstCell = model->cellCount;
    row->cellCount = 0u;
    row->height = 0;
    model->rowCount++;

    if (anonymousCell != RoboLayoutNodeIdNone) {
        LayoutTableCell cell;

        if (model->cellCount >= LAYOUT_TABLE_MAX_CELLS) {
            return RoboOk;
        }
        if (LayoutTableBoxModelRead(walk, LayoutObjectSlotConst(walk->tree,
                                                               anonymousCell),
                                    contentWidth, &cell.box) != RoboOk) {
            return walk->diag ? walk->diag->code : RoboError;
        }
        cell.id = anonymousCell;
        cell.column = 0u;
        cell.contentW = 0;
        cell.contentH = 0;
        cell.empty = LayoutTableObjectIsEmpty(
            walk, LayoutObjectSlotConst(walk->tree, anonymousCell));
        model->cells[model->cellCount] = cell;
        model->cellCount++;
        row->cellCount = 1u;
        if (*outColumns < 1u) {
            *outColumns = 1u;
        }
        return RoboOk;
    }

    {
        const LayoutObject *rowObject = LayoutObjectSlotConst(walk->tree, rowId);
        LayoutNodeId child = rowObject ? rowObject->firstChild
                                       : RoboLayoutNodeIdNone;

        while (child != RoboLayoutNodeIdNone) {
            const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree, child);
            LayoutNodeId next;

            if (!childObject) {
                break;
            }
            next = childObject->NextSibling;
            if (childObject->kind != RoboLayoutObjectBox ||
                !CssDisplayIsTableCell(childObject->display)) {
                /* A text run in a row is what CSS wraps in an anonymous cell; a box that
                 * is not a cell is not a table box at all. Both are counted, because this
                 * build makes no boxes at move-2. */
                LayoutTableSkip(walk);
                child = next;
                continue;
            }
            {
                LayoutTableCell cell;

                if (model->cellCount >= LAYOUT_TABLE_MAX_CELLS) {
                    LayoutTableSkip(walk);
                    child = next;
                    continue;
                }
                if (LayoutTableBoxModelRead(walk, childObject, contentWidth,
                                            &cell.box) != RoboOk) {
                    return walk->diag ? walk->diag->code : RoboError;
                }
                cell.id = child;
                cell.column = row->cellCount;
                cell.contentW = 0;
                cell.contentH = 0;
                cell.empty = LayoutTableObjectIsEmpty(walk, childObject);
                model->cells[model->cellCount] = cell;
                model->cellCount++;
                row->cellCount++;
            }
            child = next;
        }
    }
    if (row->cellCount > *outColumns) {
        *outColumns = row->cellCount;
    }
    return RoboOk;
}

/** Read one `table-column` box's declared width, if it declares one. */
static void LayoutTableReadColumn(const LayoutWalk *walk,
                                  const LayoutObject *object,
                                  int32_t contentWidth,
                                  int32_t *declared,
                                  size_t *count,
                                  int *outDeclared)
{
    LayoutDimension dimension = LayoutDimensionAuto();
    int32_t px;

    (void)walk;
    *outDeclared = 0;
    if (!object) {
        return;
    }
    (void)LayoutSizingReadDimension(object->style, "width", &dimension);
    px = LayoutTableDeclaredPx(&dimension, contentWidth, 1);
    if (px >= 0 && *count < LAYOUT_TABLE_MAX_COLUMNS) {
        declared[*count] = px;
        *outDeclared = 1;
    }
    if (*count < LAYOUT_TABLE_MAX_COLUMNS) {
        (*count)++;
    }
}

Result LayoutTableWalkContents(LayoutWalk *walk,
                               LayoutNodeId owner,
                               int32_t contentWidth,
                               int hasDefiniteHeight,
                               GfxPixelY definiteHeight,
                               GfxPixelY *outExtent)
{
    const LayoutObject *container;
    LayoutTableModel model;
    CssTableLayout layoutMode;
    int32_t columnGap;
    int32_t rowGap;
    int collapsed = 0;
    int captionAfter;
    int32_t columnMin[LAYOUT_TABLE_MAX_COLUMNS];
    int32_t columnMax[LAYOUT_TABLE_MAX_COLUMNS];
    int32_t columnDeclared[LAYOUT_TABLE_MAX_COLUMNS];
    int32_t columnWidth[LAYOUT_TABLE_MAX_COLUMNS];
    size_t declaredColumns = 0u;
    size_t columns = 0u;
    size_t row;
    LayoutNodeId child;

    if (!walk || !walk->tree || !walk->block || !outExtent) {
        return RoboLayoutDiag(walk ? walk->diag : NULL, RoboLayoutDomainTable,
                              RoboInvalidArgument,
                              "the table pass needs a walk, a tree and an out");
    }
    if (contentWidth < 0) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainTable, RoboInvalidArgument,
                              "a negative container width");
    }

    memset(&model, 0, sizeof(model));
    memset(columnMin, 0, sizeof(columnMin));
    memset(columnMax, 0, sizeof(columnMax));
    memset(columnWidth, 0, sizeof(columnWidth));
    for (row = 0u; row < LAYOUT_TABLE_MAX_COLUMNS; row++) {
        columnDeclared[row] = -1;
    }

    container = LayoutObjectSlotConst(walk->tree, owner);
    if (!container) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainTable, RoboError,
                              "the box that owns a table context is not in the tree");
    }

    /* ---- 1. the container's declarations --------------------------- */

    layoutMode = LayoutTableLayoutOf(container->style);
    LayoutTableSpacingOf(container->style, &columnGap, &rowGap, &collapsed);
    captionAfter = LayoutTableCaptionIsAfter(container->style);
    if (collapsed) {
        walk->block->table.collapsedTables++;
    } else if (columnGap > 0 || rowGap > 0) {
        walk->block->table.spacedTables++;
    }
    (void)hasDefiniteHeight;
    (void)definiteHeight;
    walk->block->table.containers++;

    /* ---- 2. the row and cell model, with the fixup ------------------ */

    child = container->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree, child);
        LayoutNodeId next;

        if (!childObject) {
            return RoboLayoutDiag(walk->diag, RoboLayoutDomainTable, RoboError,
                                  "a table's child link names no box");
        }
        next = childObject->NextSibling;
        {
            LayoutPosition position = LayoutPositionOf(childObject->style);

            /* An out-of-flow child is not a table box: it is placed by its containing
             * block's own pass (14.0), exactly as it is in a block container. */
            if (LayoutAbsoluteIsOutOfFlow(&position)) {
                child = next;
                continue;
            }
        }
        if (childObject->kind != RoboLayoutObjectBox) {
            LayoutTableSkip(walk);
            child = next;
            continue;
        }
        if (CssDisplayIsTableCaption(childObject->display)) {
            if (model.caption == RoboLayoutNodeIdNone) {
                model.caption = child;
            } else {
                /* One caption is CSS: the rest are counted rather than placed. */
                LayoutTableSkip(walk);
            }
            child = next;
            continue;
        }
        if (CssDisplayIsTableColumnLike(childObject->display)) {
            int declared;

            walk->block->table.columnBoxes++;
            /* A column contributes a width and generates **no geometry**: CSS has no
             * column box on the page. It is counted in the block record's
             * `inlineLevelSkipped` -- the bucket the identity names for "an object with
             * no geometry, for a stated reason" -- which is what keeps the identity
             * closing over a direction that declines boxes on purpose. */
            walk->block->inlineLevelSkipped++;
            LayoutTableReadColumn(walk, childObject, contentWidth, columnDeclared,
                                  &declaredColumns, &declared);
            (void)declared;
            if (childObject->display == RoboCssDisplayTableColumnGroup) {
                LayoutNodeId columnChild = childObject->firstChild;

                while (columnChild != RoboLayoutNodeIdNone) {
                    const LayoutObject *columnObject =
                        LayoutObjectSlotConst(walk->tree, columnChild);
                    LayoutNodeId columnNext;

                    if (!columnObject) {
                        break;
                    }
                    columnNext = columnObject->NextSibling;
                    if (columnObject->kind == RoboLayoutObjectBox &&
                        columnObject->display == RoboCssDisplayTableColumn) {
                        walk->block->table.columnBoxes++;
                        walk->block->inlineLevelSkipped++;
                        LayoutTableReadColumn(walk, columnObject, contentWidth,
                                              columnDeclared, &declaredColumns,
                                              &declared);
                        (void)declared;
                    } else {
                        LayoutTableSkip(walk);
                    }
                    columnChild = columnNext;
                }
            }
            child = next;
            continue;
        }
        if (CssDisplayIsTableRowLike(childObject->display) &&
            !CssDisplayIsTableCell(childObject->display) &&
            childObject->display != RoboCssDisplayTableRow) {
            /* A **row group**: it is placed like any other box, and its rows are the
             * model's rows. A cell directly inside a group is a row of one, which is
             * CSS's fixup for a row the author did not write. */
            LayoutNodeId groupChild = childObject->firstChild;

            while (groupChild != RoboLayoutNodeIdNone) {
                const LayoutObject *groupObject =
                    LayoutObjectSlotConst(walk->tree, groupChild);
                LayoutNodeId groupNext;

                if (!groupObject) {
                    break;
                }
                groupNext = groupObject->NextSibling;
                if (groupObject->kind != RoboLayoutObjectBox) {
                    LayoutTableSkip(walk);
                } else if (groupObject->display == RoboCssDisplayTableRow) {
                    if (LayoutTableReadRow(walk, &model, groupChild, child,
                                           RoboLayoutNodeIdNone, contentWidth,
                                           &columns) != RoboOk) {
                        return walk->diag ? walk->diag->code : RoboError;
                    }
                } else if (CssDisplayIsTableCell(groupObject->display)) {
                    size_t before = model.rowCount;

                    if (LayoutTableReadRow(walk, &model, RoboLayoutNodeIdNone, child,
                                           groupChild, contentWidth,
                                           &columns) != RoboOk) {
                        return walk->diag ? walk->diag->code : RoboError;
                    }
                    if (model.rowCount > before) {
                        walk->block->table.anonymousRows++;
                    } else {
                        LayoutTableSkip(walk);
                    }
                } else {
                    LayoutTableSkip(walk);
                }
                groupChild = groupNext;
            }
            child = next;
            continue;
        }
        if (childObject->display == RoboCssDisplayTableRow) {
            size_t before = model.rowCount;

            if (LayoutTableReadRow(walk, &model, child, RoboLayoutNodeIdNone,
                                   RoboLayoutNodeIdNone, contentWidth,
                                   &columns) != RoboOk) {
                return walk->diag ? walk->diag->code : RoboError;
            }
            if (model.rowCount == before) {
                LayoutTableSkip(walk);
            }
            child = next;
            continue;
        }
        if (CssDisplayIsTableCell(childObject->display)) {
            /* A cell with no row: **this** is the fixup's real work, and it is counted so
             * that a document which omits its `<tr>` says so. */
            size_t before = model.rowCount;

            if (LayoutTableReadRow(walk, &model, RoboLayoutNodeIdNone,
                                   RoboLayoutNodeIdNone, child, contentWidth,
                                   &columns) != RoboOk) {
                return walk->diag ? walk->diag->code : RoboError;
            }
            if (model.rowCount > before) {
                walk->block->table.anonymousRows++;
            } else {
                LayoutTableSkip(walk);
            }
            child = next;
            continue;
        }
        /* A text run, or a box whose display is not a table-internal one: CSS wraps it in
         * an anonymous cell, and this build makes no boxes -- so it is counted, exactly as
         * the grid pass counts the anonymous grid item it does not build. */
        LayoutTableSkip(walk);
        child = next;
    }

    if (declaredColumns > columns) {
        columns = declaredColumns;
    }
    if (columns > LAYOUT_TABLE_MAX_COLUMNS) {
        columns = LAYOUT_TABLE_MAX_COLUMNS;
    }
    walk->block->table.columns += columns;

    /* ---- 3. the caption, which is what the rows start below --------- */

    {
        int32_t captionExtent = 0;

        if (model.caption != RoboLayoutNodeIdNone) {
            GfxPixelY captionContent = 0;
            LayoutTableBoxModel captionBox;

            if (LayoutTableBoxModelRead(walk,
                                        LayoutObjectSlotConst(walk->tree,
                                                              model.caption),
                                        contentWidth, &captionBox) != RoboOk) {
                return walk->diag ? walk->diag->code : RoboError;
            }
            if (LayoutBlockWalkChildren(walk, model.caption, contentWidth,
                                        &captionContent) != RoboOk) {
                return walk->diag ? walk->diag->code : RoboError;
            }
            captionExtent = (int32_t)captionContent;
            model.captionExtent = captionExtent;
            walk->block->table.captions++;
        }

        /* ---- 4. size the columns ----------------------------------- */

        {
            int32_t declaredSum = 0;
            int32_t spreadSum = 0;
            int32_t fixedSum = 0;
            int32_t leftover;
            size_t index;
            int fixed = layoutMode == RoboCssTableLayoutFixed;
            int undeclaredCount = 0;

            /*
             * `table-layout: fixed` reads the **first row's** cells as declarations for
             * the columns they sit in (CSS 2.1 §17.5.2.1), after the `table-column`
             * boxes have had their say -- a column box's width wins, because it comes
             * first in the algorithm and a later declaration does not unmake an earlier
             * one. This is the one place a *cell* decides a column rather than the other
             * way round, and it is what makes `fixed` a real algorithm here rather than
             * an equal-share split: without it, a table whose first row states its
             * widths would have every column equal.
             */
            if (fixed && model.rowCount > 0u && model.rows[0].cellCount > 0u) {
                size_t firstCell = model.rows[0].firstCell;
                size_t lastCell = firstCell + model.rows[0].cellCount;
                size_t cell;

                for (cell = firstCell; cell < lastCell; cell++) {
                    const LayoutTableCell *modelCell = &model.cells[cell];

                    if (modelCell->column < columns &&
                        columnDeclared[modelCell->column] < 0 &&
                        modelCell->box.declaredW >= 0) {
                        columnDeclared[modelCell->column] = modelCell->box.declaredW;
                    }
                }
            }

            for (index = 0u; index < columns; index++) {
                if (columnDeclared[index] >= 0) {
                    declaredSum += columnDeclared[index];
                } else {
                    undeclaredCount++;
                }
            }
            /* The cells' contributions: a floor from the widest **min-content** of the
             * cells in a column and a ceiling from their widest **max-content**, which is
             * 19.1's measurement read per column -- the two numbers the flex and grid
             * passes consume as well. */
            for (row = 0u; row < model.cellCount; row++) {
                const LayoutTableCell *cell = &model.cells[row];
                LayoutIntrinsicSize intrinsic;
                int32_t contribution;

                if (cell->column >= columns || cell->id == RoboLayoutNodeIdNone) {
                    continue;
                }
                LayoutIntrinsicSizeInit(&intrinsic);
                (void)LayoutIntrinsicMeasureObject(walk->tree, cell->id,
                                                   walk->textMetrics, &intrinsic,
                                                   NULL);
                contribution = (int32_t)intrinsic.minContent + cell->box.inlineInsets;
                if (contribution > columnMin[cell->column]) {
                    columnMin[cell->column] = contribution;
                }
                contribution = (int32_t)intrinsic.maxContent + cell->box.inlineInsets;
                if (contribution > columnMax[cell->column]) {
                    columnMax[cell->column] = contribution;
                }
            }

            leftover = contentWidth - declaredSum -
                       columnGap * (columns > 0u ? (int32_t)(columns - 1u) : 0);
            if (leftover < 0) {
                leftover = 0;
            }
            if (fixed) {
                /*
                 * The equal share is the fixed algorithm's own answer for a column that
                 * declared nothing, and it is a **declaration** for the counter's
                 * purposes rather than a content measurement -- the two numbers below
                 * are incremented once, at the end of this block, so that a column is
                 * counted in exactly one bucket whatever branch sized it.
                 */
                for (index = 0u; index < columns; index++) {
                    if (columnDeclared[index] >= 0) {
                        columnWidth[index] = columnDeclared[index];
                    } else {
                        columnWidth[index] = undeclaredCount > 0
                                                 ? leftover / (int32_t)undeclaredCount
                                                 : 0;
                    }
                }
            } else {
                for (index = 0u; index < columns; index++) {
                    if (columnDeclared[index] >= 0) {
                        columnWidth[index] = columnDeclared[index];
                        /* A declared width is a *floor* as well as a hint: a column whose
                         * cell needs more than it declared is as wide as the cell, which
                         * is what makes a `width: 10px` cell holding a long word lay out
                         * rather than overflow for no stated reason. */
                        if (columnMin[index] > columnWidth[index]) {
                            columnWidth[index] = columnMin[index];
                        }
                    } else {
                        columnWidth[index] = columnMin[index];
                    }
                    if (columnMax[index] > columnWidth[index]) {
                        spreadSum += columnMax[index] - columnWidth[index];
                    }
                    fixedSum += columnWidth[index];
                }
                leftover = contentWidth - fixedSum -
                           columnGap * (columns > 0u ? (int32_t)(columns - 1u) : 0);
                if (leftover > 0) {
                    int32_t remaining = leftover;

                    for (index = 0u; index < columns; index++) {
                        int32_t spread = columnMax[index] - columnWidth[index];
                        int32_t share = 0;

                        if (spread <= 0) {
                            continue;
                        }
                        share = spreadSum > 0
                                    ? (int32_t)((int64_t)leftover * (int64_t)spread /
                                                (int64_t)spreadSum)
                                    : 0;
                        if (share > remaining) {
                            share = remaining;
                        }
                        columnWidth[index] += share;
                        remaining -= share;
                    }
                    /* Free space no column's ceiling claimed goes to the last column,
                     * which is CSS's own fallback for a width nobody asked for. */
                    if (remaining > 0 && columns > 0u) {
                        columnWidth[columns - 1u] += remaining;
                    }
                }
            }
            for (index = 0u; index < columns; index++) {
                if (columnWidth[index] < 0) {
                    columnWidth[index] = 0;
                }
                if (columnWidth[index] > LAYOUT_TABLE_SIZE_MAX) {
                    columnWidth[index] = LAYOUT_TABLE_SIZE_MAX;
                }
                /*
                 * One increment per column, in exactly one bucket, and the bucket is the
                 * *question* rather than the branch: in a `table-layout: fixed` pass every
                 * column's width is a declaration -- its own, the first row's, or the
                 * algorithm's equal share -- and in an `auto` pass a column with no
                 * declaration is the one whose width came from its cells' contents.
                 */
                if (fixed || columnDeclared[index] >= 0) {
                    walk->block->table.fixedColumns++;
                } else {
                    walk->block->table.contentColumns++;
                }
            }
        }

        /* ---- 5. each cell's contents, and each row's height -------- */

        for (row = 0u; row < model.rowCount; row++) {
            LayoutTableRow *modelRow = &model.rows[row];
            size_t cell;

            for (cell = modelRow->firstCell;
                 cell < modelRow->firstCell + modelRow->cellCount; cell++) {
                LayoutTableCell *modelCell = &model.cells[cell];
                const LayoutObject *cellObject;
                int32_t columnSpace;
                int32_t contentW;
                GfxPixelY contentExtent = 0;

                cellObject = LayoutObjectSlotConst(walk->tree, modelCell->id);
                if (!cellObject) {
                    return RoboLayoutDiag(walk->diag, RoboLayoutDomainTable,
                                          RoboError, "a cell vanished under the pass");
                }
                columnSpace = modelCell->column < columns
                                  ? columnWidth[modelCell->column]
                                  : contentWidth;
                if (modelCell->box.declaredW >= 0) {
                    contentW = modelCell->box.declaredW;
                    if (contentW + modelCell->box.inlineInsets > columnSpace) {
                        walk->block->table.overflowingCells++;
                    }
                } else {
                    contentW = columnSpace - modelCell->box.inlineInsets;
                    if (contentW < 0) {
                        contentW = 0;
                    }
                }
                modelCell->contentW = contentW;
                if (LayoutBlockWalkChildren(walk, modelCell->id, contentW,
                                            &contentExtent) != RoboOk) {
                    return walk->diag ? walk->diag->code : RoboError;
                }
                modelCell->contentH = modelCell->box.declaredH >= 0
                                          ? modelCell->box.declaredH
                                          : (int32_t)contentExtent;
                if (modelCell->contentH + modelCell->box.blockInsets >
                    modelRow->height) {
                    modelRow->height = modelCell->contentH + modelCell->box.blockInsets;
                }
            }
        }

        /* A row's own declared height raises it where the author asked for more room than
         * its cells needed, which is the precedence every other declared height has. */
        for (row = 0u; row < model.rowCount; row++) {
            LayoutTableRow *modelRow = &model.rows[row];
            LayoutDimension declaredHeight = LayoutDimensionAuto();
            const LayoutObject *rowObject;
            int32_t height;

            if (modelRow->id == RoboLayoutNodeIdNone) {
                continue;
            }
            rowObject = LayoutObjectSlotConst(walk->tree, modelRow->id);
            if (!rowObject) {
                continue;
            }
            (void)LayoutSizingReadDimension(rowObject->style, "height",
                                            &declaredHeight);
            height = LayoutTableDeclaredPx(&declaredHeight, 0, 0);
            if (height > modelRow->height) {
                modelRow->height = height;
            }
        }

        /* ---- 6. place every row, group, cell and caption ------------ */

        {
            int32_t columnOffset[LAYOUT_TABLE_MAX_COLUMNS];
            int32_t rowOffset[LAYOUT_TABLE_MAX_ROWS];
            int32_t rowsExtent = 0;
            int32_t total;
            int32_t cursor = 0;
            size_t index;
            int32_t captionExtent = model.captionExtent;

            for (index = 0u; index < columns; index++) {
                columnOffset[index] = cursor;
                cursor += columnWidth[index] + columnGap;
            }
            for (row = 0u; row < model.rowCount; row++) {
                rowsExtent += model.rows[row].height;
                if (row + 1u < model.rowCount) {
                    rowsExtent += rowGap;
                }
            }
            total = rowsExtent + (captionExtent > 0 ? captionExtent + rowGap : 0);

            cursor = captionAfter ? 0 : (captionExtent > 0 ? captionExtent + rowGap : 0);
            for (row = 0u; row < model.rowCount; row++) {
                rowOffset[row] = cursor;
                cursor += model.rows[row].height + rowGap;
            }

            if (model.caption != RoboLayoutNodeIdNone) {
                LayoutTableBoxModel captionBox;

                if (LayoutTableBoxModelRead(walk,
                                            LayoutObjectSlotConst(walk->tree,
                                                                  model.caption),
                                            contentWidth, &captionBox) != RoboOk) {
                    return walk->diag ? walk->diag->code : RoboError;
                }
                if (LayoutTableWriteBox(walk, model.caption, &captionBox, 0,
                                        captionAfter ? total - captionExtent : 0,
                                        contentWidth, captionExtent) != RoboOk) {
                    return walk->diag ? walk->diag->code : RoboError;
                }
            }

            for (row = 0u; row < model.rowCount; row++) {
                LayoutTableRow *modelRow = &model.rows[row];
                size_t cell;

                for (cell = modelRow->firstCell;
                     cell < modelRow->firstCell + modelRow->cellCount; cell++) {
                    LayoutTableCell *modelCell = &model.cells[cell];
                    int32_t x = modelCell->column < columns
                                    ? columnOffset[modelCell->column]
                                    : 0;

                    if (LayoutTableWriteBox(walk, modelCell->id, &modelCell->box, x,
                                            rowOffset[row], modelCell->contentW,
                                            modelCell->contentH) != RoboOk) {
                        return walk->diag ? walk->diag->code : RoboError;
                    }
                    if (modelCell->empty) {
                        walk->block->table.cellsEmpty++;
                    }
                    walk->block->table.cellsPlaced++;
                }

                /* The **row**: a box with one rectangle, at the block extent its cells
                 * came to. A row's own padding, border and margin are ignored for the
                 * geometry -- CSS's model has no place for them -- so it is written with
                 * no insets at all. */
                if (modelRow->id != RoboLayoutNodeIdNone) {
                    LayoutTableBoxModel rowBox;

                    memset(&rowBox, 0, sizeof(rowBox));
                    rowBox.declaredW = -1;
                    rowBox.declaredH = -1;
                    if (LayoutTableWriteBox(walk, modelRow->id, &rowBox, 0,
                                            rowOffset[row], contentWidth,
                                            modelRow->height) != RoboOk) {
                        return walk->diag ? walk->diag->code : RoboError;
                    }
                }
                walk->block->table.rows++;
            }

            /* Each **group** once, over all of its rows: a group is a box whose
             * rectangle is the union of the rows it holds, which is what makes a
             * `table-row-group` a box a paint pass can draw a background on. */
            for (row = 0u; row < model.rowCount; row++) {
                LayoutNodeId group = model.rows[row].group;
                int32_t top;
                int32_t bottom;
                LayoutTableBoxModel groupBox;

                if (group == RoboLayoutNodeIdNone) {
                    continue;
                }
                if (row > 0u && model.rows[row - 1u].group == group) {
                    continue;
                }
                top = rowOffset[row];
                bottom = rowOffset[row] + model.rows[row].height;
                for (index = row + 1u; index < model.rowCount; index++) {
                    if (model.rows[index].group != group) {
                        break;
                    }
                    bottom = rowOffset[index] + model.rows[index].height;
                }
                memset(&groupBox, 0, sizeof(groupBox));
                groupBox.declaredW = -1;
                groupBox.declaredH = -1;
                if (LayoutTableWriteBox(walk, group, &groupBox, 0, top, contentWidth,
                                        bottom - top) != RoboOk) {
                    return walk->diag ? walk->diag->code : RoboError;
                }
                walk->block->table.groups++;
            }

            *outExtent = (GfxPixelY)LayoutExtentClamp((int64_t)total);
        }
    }
    return RoboOk;
}
