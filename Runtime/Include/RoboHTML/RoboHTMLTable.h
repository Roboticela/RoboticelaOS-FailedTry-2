/*
 * RoboHTML table structure (Phase 17.6).
 *
 * A table is not a node kind — it is a *shape* the tree already has:
 * `<table>` containing `<caption>`, `<colgroup>` and the three row groups
 * (`<thead>`, `<tbody>`, `<tfoot>`), each holding `<tr>`, each holding `<td>`
 * and `<th>`. The parser builds that shape; this header is the read side an
 * application (or RoboJS on its behalf) needs to walk it the way the DOM's
 * `table.rows` / `row.cells` / `cell.cellIndex` do.
 *
 * It changes nothing: no node is created, no attribute is written, and no
 * *layout* is computed. Table layout — column measurement, colspans spanning
 * columns, row-height distribution — belongs to RoboCSS/layout phases, and
 * keeping it out of here is the point. What is here is the structural query a
 * layout pass (or a script) consumes.
 *
 * ## The collection rule, stated once
 *
 * `table.rows` is every row of *this* table in document order: a `<tr>` whose
 * parent is the table itself, or whose parent is a section that is a direct
 * child of the table. A row of a nested table therefore never appears in the
 * outer table's rows, which is the DOM's own rule and the one a naive
 * descendant walk gets wrong.
 *
 * ## Rowspan/colspan are read, not applied
 *
 * `colspan` and `rowspan` are parsed as positive integers with the specified
 * default of 1; a missing, zero, negative or non-numeric value reads as 1,
 * because the DOM's `colSpan` getter is a number and never a parse error.
 * Nothing here re-slices a grid — that is the layout pass's job.
 */

#ifndef ROBO_HTML_TABLE_H
#define ROBO_HTML_TABLE_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Visit every row of @p table in document order (see the collection rule).
 *
 * @return RoboOk; the visitor's own code when it stops the walk;
 *         RoboInvalidArgument (NULL @p table); RoboNotFound (@p table does
 *         not resolve or is not an element).
 */
Result HtmlTableGetRows(
    const HtmlNodeHandle *table,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag);

/**
 * Visit @p table's row groups — `<thead>`, `<tbody>`, `<tfoot>` — that are its
 * direct children, in document order. `<tbody>` is included; the DOM's
 * `tBodies` is `HtmlTableGetBodies`.
 */
Result HtmlTableGetSections(
    const HtmlNodeHandle *table,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag);

/** Visit only the `<tbody>` groups of @p table (the DOM's `tBodies`). */
Result HtmlTableGetBodies(
    const HtmlNodeHandle *table,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag);

/** Visit the rows of one section (`<thead>`/`<tbody>`/`<tfoot>`) in order. */
Result HtmlTableSectionGetRows(
    const HtmlNodeHandle *section,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag);

/** Visit the cells (`<td>`/`<th>`) of @p row, in document order. */
Result HtmlTableRowGetCells(
    const HtmlNodeHandle *row,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag);

/**
 * Index of @p row within its table's rows, counted in document order
 * (`row.rowIndex`).
 *
 * @return RoboOk; RoboInvalidArgument; RoboNotFound when @p row is not a row
 *         of a table (a detached `<tr>`, or one whose parent is not a table or
 *         a section).
 */
Result HtmlTableRowIndex(
    const HtmlNodeHandle *row,
    size_t *outIndex,
    HtmlDiag *diag);

/**
 * Index of @p cell within its row's cells, counted in document order
 * (`cell.cellIndex`).
 *
 * @return RoboOk; RoboInvalidArgument; RoboNotFound when @p cell is not a cell
 *         of a row.
 */
Result HtmlTableCellIndex(
    const HtmlNodeHandle *cell,
    size_t *outIndex,
    HtmlDiag *diag);

/**
 * `colspan` of @p cell as a positive integer (default 1).
 * @return RoboOk; RoboInvalidArgument; RoboNotFound (not a live cell).
 */
Result HtmlTableCellColSpan(
    const HtmlNodeHandle *cell,
    int *outSpan,
    HtmlDiag *diag);

/** `rowspan` of @p cell as a positive integer (default 1). */
Result HtmlTableCellRowSpan(
    const HtmlNodeHandle *cell,
    int *outSpan,
    HtmlDiag *diag);

/**
 * First `<caption>` that is a direct child of @p table, or RoboNotFound.
 * `caption` is a single element in HTML; the *first* is the DOM's answer.
 */
Result HtmlTableGetCaption(
    const HtmlNodeHandle *table,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/**
 * Capability table for the table-structure surface. Every field is what this
 * build does: structure is present, layout and span application are not (they
 * belong to the document/paint layers).
 */
typedef struct HtmlTableCapabilities {
    int structure;      /* rows / sections / cells */
    int indexes;        /* rowIndex / cellIndex */
    int spansRead;      /* colspan / rowspan read */
    int spanLayout;     /* grid re-slicing: 0 by design */
    int caption;        /* caption lookup */
} HtmlTableCapabilities;

/** Fill @p out with the capabilities above. RoboInvalidArgument on NULL. */
Result HtmlTableGetCapabilities(HtmlTableCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TABLE_H */
