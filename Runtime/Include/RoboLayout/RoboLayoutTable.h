/*
 * RoboLayout table layout (Phase 19.6).
 *
 * A table container is a box whose inner display is `table` (`display: table` or
 * `display: inline-table`), and this phase is the algorithm that places its contents
 * into **rows and columns** rather than stacking them or placing them on a line. It is
 * the fifth formatting context this build lays out, beside the block, inline, flex and
 * grid directions, and it has the same shape they do: the box owns a context
 * (`RoboLayoutFcTable`), the block pass dispatches to it, and the boxes it places are
 * ordinary boxes to everything downstream -- they have results, margins, padding,
 * borders, overflow and content of their own.
 *
 * The axis model, and how it differs from grid
 *
 * A grid resolves its two axes from **declarations and items**, in that order, and an
 * item's placement is a fact about the item. A table resolves its two axes from its
 * **contents** in one pass -- a column's width is a function of the cells in it and a
 * row's height is a function of the cells in *that* -- and a cell's slot is a fact about
 * where it sits in the row, not about anything it declares. That is why this pass has no
 * placement step at all: the model *is* the placement, and the algorithm is
 *
 *   table-layout: auto   each column is between its widest cell's min-content and its
 *                        widest cell's max-content contribution, and the container's
 *                        width is distributed over the columns that can still grow;
 *   table-layout: fixed  each column is the width the first row (or a `table-column`)
 *                        declared, and the columns that declared none share what is
 *                        left equally.
 *
 * A detail worth stating because it removes a whole class of CSS's own cases: a table here
 * **always has a definite inline size.** The block pass resolves the container's content
 * width before it asks this pass, exactly as it does for a flex row or a grid, so the
 * `table-layout: fixed` declaration is always *usable* (CSS only honours it against a
 * definite width, and shrink-to-fit is not this build's) and the `auto` algorithm always
 * has a width to distribute rather than a content-derived one to solve for.
 *
 * This build lays a table out in the horizontal writing mode only, and
 *
 *   `border-collapse: separate`  cells are separated by the `border-spacing` gutters;
 *   `border-collapse: collapse`  the gutters are zero, because CSS says `border-spacing`
 *                                does not apply to a collapsing table. The *border*
 *                                arithmetic of a collapsing table -- one border where
 *                                two adjoin, and the rules that decide which -- is a
 *                                later row's and is named as absent below.
 *
 * What the phase claims
 *
 *   - `display: table` and `display: inline-table` are real display values
 *     (RoboCSSDisplay.h) and each establishes a table formatting context, and the
 *     eight **internal** values (`table-row-group`, `table-header-group`,
 *     `table-footer-group`, `table-row`, `table-cell`, `table-column`,
 *     `table-column-group`, `table-caption`) are boxes the table's own pass places
 *     rather than ones block or inline flow claims;
 *   - the container's table properties are read as the **computed values** 18.5 types and
 *     never re-parsed here: `table-layout` (`fixed`/`auto`), `border-collapse`
 *     (`separate`/`collapse`), `caption-side` (`top`/`bottom` and the block spellings),
 *     and `border-spacing` as the **carried** declaration 18.5 preserves -- one length for
 *     both gutters or two, which is CSS's own grammar;
 *   - the **fixup**: a cell that is a direct child of the table is placed as an
 *     anonymous row of its own, a row group contributes its rows in document order, a
 *     column or column group contributes a **declared column width** and generates no
 *     geometry, and a child that is neither a table-internal box nor a caption (a text
 *     run, or a box with an ordinary display) is **counted** rather than wrapped in the
 *     anonymous cell CSS would generate -- this build makes no boxes, which is the same
 *     answer the grid pass gives an anonymous grid item;
 *   - a **caption** is placed on the table's block-start or block-end edge per
 *     `caption-side`, laid out at the container's content width, and the rows follow it;
 *   - a **cell** and a **caption** establish a formatting context of their own (CSS 2.1
 *     §9.4.1's own list), so a cell's contents are laid out *inside* it at the width the
 *     columns gave it;
 *   - an **empty** cell is counted (`cellsEmpty`) -- a cell with no in-flow box child --
 *     and `empty-cells` itself is **not read**, because its only effect is on a cell's
 *     border and background and this module paints nothing: a layout pass that acted on
 *     it would be claiming a paint decision. `visibility` is not read either: this build
 *     places every cell it is given.
 *
 * What the phase deliberately does **not** claim
 *
 *   - **`colspan` and `rowspan`.** They are HTML *attributes* and not CSS properties, so
 *     no computed value carries them and this module reads no attribute: every cell
 *     occupies exactly one slot, and a document whose cells span must not expect the
 *     table to be right about it.
 *   - **the border arithmetic of a collapsing table** -- one border where two adjoin, and
 *     the rules that pick which. `border-collapse: collapse` is read, counted and made to
 *     zero the gutters; the borders themselves are each cell's own, exactly as in a
 *     separate table.
 *   - **`visibility: collapse`** on a row, `caption-side: inline-start`/`inline-end` (read
 *     and placed as the block-start edge, because this build's tables have no inline
 *     caption edges), and a table **root** box (the tree root establishes the initial
 *     block context whatever it computes to).
 *   - **An internal table box outside a table.** A `table-cell`, `table-row` or
 *     `table-column` whose ancestors contain no table container is **counted and left
 *     UNRESOLVED** (`LayoutBlockVerdictFor`'s `LAYOUT_BLOCK_TABLE_INTERNAL`), because
 *     CSS 2.1 §17.2.1's answer to it is an anonymous table, row group and row, and this
 *     build makes no boxes -- the fixup above is the one *inside* a table container.
 *   - **anonymous cell generation** (a table's child that is neither a table-internal box
 *     nor a caption is counted, not wrapped), an **HTML attribute** read,
 *     **vertical alignment** (`vertical-align` on a cell, so a cell's contents sit at its
 *     block-start edge), a **row's or a cell's own `padding`/`border`/`margin` in the row
 *     band** (a row and a row group are written with no insets of their own, which CSS's
 *     table model requires), **replaced elements / `aspect-ratio`**, **`overflow` on a
 *     row group** and **vertical writing modes**.
 *
 * This header is the counters record the pass reports through `LayoutBlockStats` and the
 * two doors over it; the algorithm is RoboLayoutTable.c and is reached only through the
 * block pass, exactly as the other four directions are.
 */

#ifndef ROBO_LAYOUT_TABLE_H
#define ROBO_LAYOUT_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* What one table container's layout came to                           */
/* ------------------------------------------------------------------ */

/**
 * Counters for the table work one block pass did. A record by value, owned by nobody,
 * and nested in `LayoutBlockStats` beside the flex and grid counters and the block,
 * inline, margin, padding, border, position and overflow ones.
 *
 * Counters rather than one total, for the reason every other record in this module
 * gives: the interesting fact is the *mix*. Two tables can place the same number of cells
 * while one sized every column from its contents and the other read every width from a
 * `table-layout: fixed` row, and a record that could not tell them apart would not be
 * testing the algorithm.
 */
typedef struct LayoutTableCounters {
    /** Table containers whose contents the table pass laid out. */
    size_t containers;
    /** **Row** boxes placed -- a row group's rows included, and an anonymous row the
     *  fixup generated. The table counterpart of `grid.rows`. */
    size_t rows;
    /** Row **groups** placed (either kind, and a group that holds no row is still
     *  placed: it is a box with a result either way). */
    size_t groups;
    /** Column slots the pass sized, declared or discovered. */
    size_t columns;
    /** `table-column` / `table-column-group` boxes read for a declared width. They
     *  generate **no geometry** -- CSS has no column box on the page -- so they are
     *  counted here and left UNRESOLVED, which is why this is apart from `columns`. */
    size_t columnBoxes;
    /** Cells that received geometry. */
    size_t cellsPlaced;
    /** Cells with no in-flow **box** child -- what `empty-cells` selects on. Counted, not
     *  acted on: this build places and paints every cell it is given, and the property's
     *  effect is a border and a background, neither of which a layout pass owns. */
    size_t cellsEmpty;
    /** Captions placed on the table's block-start or block-end edge. A table with more
     *  than one caption counts the first and reports the rest as skipped. */
    size_t captions;
    /** Anonymous rows the **fixup** generated, because a cell was a direct child of the
     *  table. The counter that says whether the fixup was real work or merely
     *  declared. */
    size_t anonymousRows;
    /** In-flow children the fixup could not place: a text run, a box whose display is
     *  not a table-internal one, or a child past the pass's own ceiling. Counted so
     *  `unreached` stays an honest difference (`RoboLayoutBlock.h` states the identity
     *  that closes over every direction). */
    size_t itemsSkipped;
    /** Columns whose width came from their **contents**: an `auto` pass where neither a
     *  `table-column` box nor the first row declared one, so the width is the widest
     *  cell's min-content floor plus whatever of the container's width its max-content
     *  ceiling claimed. Never counted in a `fixed` pass, because no column there is
     *  sized from its cells. */
    size_t contentColumns;
    /** Columns whose width came from a **declaration**: a `table-column` box's `width`,
     *  the first row's cell in a `table-layout: fixed` pass, or that algorithm's own
     *  equal share of what is left. Every column of a `fixed` pass is one of these, so
     *  `contentColumns + fixedColumns == columns` always holds and the pair says which
     *  arithmetic sized the table. */
    size_t fixedColumns;
    /** Tables whose cells are separated by a **gutter** (`border-collapse: separate`
     *  with a non-zero `border-spacing`). */
    size_t spacedTables;
    /** Tables whose borders **adjoin** (`border-collapse: collapse`), so their gutters
     *  are zero. The border arithmetic itself is not this phase's -- see the header. */
    size_t collapsedTables;
    /** Cells whose declared width the columns could not honour, so the cell is wider
     *  than the column it occupies. Reported rather than clamped: an overflowing cell
     *  is CSS's answer, and hiding it would be this build's. */
    size_t overflowingCells;
} LayoutTableCounters;

/** Clear @p counters to zero. Safe on NULL. */
void LayoutTableCountersInit(LayoutTableCounters *counters);

/** Field-by-field equality. Safe on NULL. */
int LayoutTableCountersEqual(const LayoutTableCounters *a,
                             const LayoutTableCounters *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_TABLE_H */
