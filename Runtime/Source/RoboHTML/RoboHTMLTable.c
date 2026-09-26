/*
 * RoboHTML table structure (Phase 17.6).
 *
 * Reads the contract in RoboHTMLTable.h. Everything here is a read over the
 * tree the parser (or the DOM) already built: no node is created, no attribute
 * is written, and no layout is computed. The one rule that is easy to get
 * wrong — and is therefore stated once — is that a *nested* table's rows are
 * not rows of the outer table.
 */

#include "RoboHTML/RoboHTMLTable.h"
#include "RoboHTML/RoboHTMLElement.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Name helpers                                                        */
/* ------------------------------------------------------------------ */

static int IsSection(const HtmlNodeHandle *element)
{
    return HtmlElementIsNamed(element, "thead") ||
           HtmlElementIsNamed(element, "tbody") ||
           HtmlElementIsNamed(element, "tfoot");
}

static int IsBody(const HtmlNodeHandle *element)
{
    return HtmlElementIsNamed(element, "tbody");
}

static int IsRow(const HtmlNodeHandle *element)
{
    return HtmlElementIsNamed(element, "tr");
}

static int IsCell(const HtmlNodeHandle *element)
{
    return HtmlElementIsNamed(element, "td") ||
           HtmlElementIsNamed(element, "th");
}

/* Non-zero when @p child's parent is exactly @p parent. */
static int IsDirectChildOf(const HtmlNodeHandle *parent,
                           const HtmlNodeHandle *child)
{
    HtmlNodeHandle actual;

    if (HtmlNodeParent(child, &actual, NULL) != RoboOk) {
        return 0;
    }
    return HtmlNodeHandleEqual(&actual, parent);
}

/* The table a row belongs to, or an invalid handle. */
static int TableOfRow(const HtmlNodeHandle *row, HtmlNodeHandle *outTable)
{
    HtmlNodeHandle parent;

    HtmlNodeHandleClear(outTable);
    if (HtmlNodeParent(row, &parent, NULL) != RoboOk) {
        return 0;
    }
    if (HtmlElementIsNamed(&parent, "table")) {
        *outTable = parent;
        return 1;
    }
    if (IsSection(&parent)) {
        HtmlNodeHandle grand;
        if (HtmlNodeParent(&parent, &grand, NULL) == RoboOk &&
            HtmlElementIsNamed(&grand, "table")) {
            *outTable = grand;
            return 1;
        }
    }
    return 0;
}

/* Non-zero when @p row is a row of @p table per the collection rule. */
static int RowBelongsToTable(const HtmlNodeHandle *table,
                             const HtmlNodeHandle *row)
{
    HtmlNodeHandle owner;

    if (!TableOfRow(row, &owner)) {
        return 0;
    }
    return HtmlNodeHandleEqual(&owner, table);
}

/* ------------------------------------------------------------------ */
/* Collection thunks                                                   */
/* ------------------------------------------------------------------ */

typedef struct TableContext {
    const HtmlNodeHandle *table; /* the table the walk is scoped to */
    HtmlNodeVisitFn visit;
    void *visitUser;
    size_t count;
} TableContext;

static Result RowVisit(const HtmlNodeView *view, size_t depth, void *user)
{
    TableContext *ctx = (TableContext *)user;

    if (IsRow(&view->handle) &&
        RowBelongsToTable(ctx->table, &view->handle)) {
        ctx->count++;
        if (ctx->visit) {
            return ctx->visit(view, depth, ctx->visitUser);
        }
    }
    return RoboOk;
}

/*
 * Rows of one *section*: a `<tr>` that is a direct child of the section. The
 * table-scoped rule ("the row's table is this table") would ask the wrong
 * question here — a section's rows report the table as their table, not the
 * section — so the section walk states the direct-child rule instead.
 */
static Result SectionRowVisit(const HtmlNodeView *view, size_t depth, void *user)
{
    TableContext *ctx = (TableContext *)user;

    if (IsRow(&view->handle) && IsDirectChildOf(ctx->table, &view->handle)) {
        ctx->count++;
        if (ctx->visit) {
            return ctx->visit(view, depth, ctx->visitUser);
        }
    }
    return RoboOk;
}

static Result SectionVisit(const HtmlNodeView *view, size_t depth, void *user)
{
    TableContext *ctx = (TableContext *)user;

    if (IsSection(&view->handle) &&
        IsDirectChildOf(ctx->table, &view->handle)) {
        ctx->count++;
        if (ctx->visit) {
            return ctx->visit(view, depth, ctx->visitUser);
        }
    }
    return RoboOk;
}

static Result BodyVisit(const HtmlNodeView *view, size_t depth, void *user)
{
    TableContext *ctx = (TableContext *)user;

    if (IsBody(&view->handle) && IsDirectChildOf(ctx->table, &view->handle)) {
        ctx->count++;
        if (ctx->visit) {
            return ctx->visit(view, depth, ctx->visitUser);
        }
    }
    return RoboOk;
}

static Result CellVisit(const HtmlNodeView *view, size_t depth, void *user)
{
    TableContext *ctx = (TableContext *)user;

    if (IsCell(&view->handle) && IsDirectChildOf(ctx->table, &view->handle)) {
        ctx->count++;
        if (ctx->visit) {
            return ctx->visit(view, depth, ctx->visitUser);
        }
    }
    return RoboOk;
}

/* Visit @p descendant (row/cell) through the same element walk. */
static Result CollectUnder(const HtmlNodeHandle *root,
                           HtmlNodeVisitFn thunk,
                           TableContext *ctx,
                           size_t *outCount,
                           HtmlDiag *diag)
{
    Result status;

    if (outCount) {
        *outCount = 0u;
    }
    status = HtmlNodeWalkDescendants(root, RoboHtmlNodeFilterElements, thunk, ctx,
                                     diag);
    if (status != RoboOk) {
        return status;
    }
    if (outCount) {
        *outCount = ctx->count;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Rows / sections / cells                                             */
/* ------------------------------------------------------------------ */

Result HtmlTableGetRows(
    const HtmlNodeHandle *table,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag)
{
    TableContext ctx;

    if (!table) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                              "a table is required");
    }
    if (!HtmlElementIsNamed(table, "table")) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "the node is not a table");
    }
    ctx.table = table;
    ctx.visit = visit;
    ctx.visitUser = user;
    ctx.count = 0u;
    return CollectUnder(table, RowVisit, &ctx, outCount, diag);
}

Result HtmlTableGetSections(
    const HtmlNodeHandle *table,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag)
{
    TableContext ctx;

    if (!table || !HtmlElementIsNamed(table, "table")) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              table ? RoboNotFound : RoboInvalidArgument,
                              "a table is required");
    }
    ctx.table = table;
    ctx.visit = visit;
    ctx.visitUser = user;
    ctx.count = 0u;
    return CollectUnder(table, SectionVisit, &ctx, outCount, diag);
}

Result HtmlTableGetBodies(
    const HtmlNodeHandle *table,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag)
{
    TableContext ctx;

    if (!table || !HtmlElementIsNamed(table, "table")) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              table ? RoboNotFound : RoboInvalidArgument,
                              "a table is required");
    }
    ctx.table = table;
    ctx.visit = visit;
    ctx.visitUser = user;
    ctx.count = 0u;
    return CollectUnder(table, BodyVisit, &ctx, outCount, diag);
}

Result HtmlTableSectionGetRows(
    const HtmlNodeHandle *section,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag)
{
    TableContext ctx;

    if (!section || !IsSection(section)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              section ? RoboNotFound : RoboInvalidArgument,
                              "a row group is required");
    }
    ctx.table = section;
    ctx.visit = visit;
    ctx.visitUser = user;
    ctx.count = 0u;
    return CollectUnder(section, SectionRowVisit, &ctx, outCount, diag);
}

Result HtmlTableRowGetCells(
    const HtmlNodeHandle *row,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag)
{
    TableContext ctx;

    if (!row || !IsRow(row)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              row ? RoboNotFound : RoboInvalidArgument,
                              "a row is required");
    }
    ctx.table = row;
    ctx.visit = visit;
    ctx.visitUser = user;
    ctx.count = 0u;
    return CollectUnder(row, CellVisit, &ctx, outCount, diag);
}

/* ------------------------------------------------------------------ */
/* Indexes                                                             */
/* ------------------------------------------------------------------ */

typedef struct IndexScan {
    const HtmlNodeHandle *target;
    size_t index;
    int found;
} IndexScan;

static Result RowIndexVisitor(const HtmlNodeView *view, size_t depth, void *user)
{
    IndexScan *scan = (IndexScan *)user;

    (void)depth;
    if (!scan->found) {
        if (HtmlNodeHandleEqual(&view->handle, scan->target)) {
            scan->found = 1;
        } else {
            scan->index++;
        }
    }
    return RoboOk;
}

static Result CellIndexVisitor(const HtmlNodeView *view, size_t depth,
                               void *user)
{
    IndexScan *scan = (IndexScan *)user;

    (void)depth;
    if (!scan->found) {
        if (HtmlNodeHandleEqual(&view->handle, scan->target)) {
            scan->found = 1;
        } else {
            scan->index++;
        }
    }
    return RoboOk;
}

Result HtmlTableRowIndex(
    const HtmlNodeHandle *row,
    size_t *outIndex,
    HtmlDiag *diag)
{
    HtmlNodeHandle table;
    IndexScan scan;
    size_t count = 0u;
    Result status;

    if (!row || !outIndex) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                              "a row and an output index are required");
    }
    if (!IsRow(row) || !TableOfRow(row, &table)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "the node is not a row of a table");
    }
    scan.target = row;
    scan.index = 0u;
    scan.found = 0;
    status = HtmlTableGetRows(&table, RowIndexVisitor, &scan, &count, diag);
    if (status != RoboOk) {
        return status;
    }
    if (!scan.found) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "the row was not found in its table");
    }
    *outIndex = scan.index;
    return RoboOk;
}

Result HtmlTableCellIndex(
    const HtmlNodeHandle *cell,
    size_t *outIndex,
    HtmlDiag *diag)
{
    HtmlNodeHandle row;
    IndexScan scan;
    size_t count = 0u;
    Result status;

    if (!cell || !outIndex) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                              "a cell and an output index are required");
    }
    if (!IsCell(cell) || HtmlNodeParent(cell, &row, NULL) != RoboOk ||
        !IsRow(&row)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "the node is not a cell of a row");
    }
    scan.target = cell;
    scan.index = 0u;
    scan.found = 0;
    status = HtmlTableRowGetCells(&row, CellIndexVisitor, &scan, &count, diag);
    if (status != RoboOk) {
        return status;
    }
    if (!scan.found) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "the cell was not found in its row");
    }
    *outIndex = scan.index;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Spans                                                               */
/* ------------------------------------------------------------------ */

static int ParseSpanAttribute(const HtmlNodeHandle *cell, const char *name)
{
    const char *value = HtmlElementAttributeValue(cell, name, NULL);
    int result = 0;
    size_t i = 0u;

    if (!value) {
        return 1;
    }
    while (value[i] >= '0' && value[i] <= '9') {
        result = result * 10 + (value[i] - '0');
        if (result > 65534) {
            result = 65534;
        }
        i++;
    }
    /* A leading digit is required, and zero/empty means the default. */
    if (i == 0u || result <= 0) {
        return 1;
    }
    return result;
}

Result HtmlTableCellColSpan(
    const HtmlNodeHandle *cell,
    int *outSpan,
    HtmlDiag *diag)
{
    if (!cell || !outSpan) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                              "a cell and an output span are required");
    }
    if (!IsCell(cell)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "the node is not a table cell");
    }
    *outSpan = ParseSpanAttribute(cell, "colspan");
    return RoboOk;
}

Result HtmlTableCellRowSpan(
    const HtmlNodeHandle *cell,
    int *outSpan,
    HtmlDiag *diag)
{
    if (!cell || !outSpan) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                              "a cell and an output span are required");
    }
    if (!IsCell(cell)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "the node is not a table cell");
    }
    *outSpan = ParseSpanAttribute(cell, "rowspan");
    return RoboOk;
}

Result HtmlTableGetCaption(
    const HtmlNodeHandle *table,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    size_t count = 0u;
    size_t index;

    if (!table || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                              "a table and an output handle are required");
    }
    HtmlNodeHandleClear(out);
    if (!HtmlElementIsNamed(table, "table")) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "the node is not a table");
    }
    if (HtmlNodeChildCount(table, &count, diag) != RoboOk) {
        return diag ? diag->code : RoboNotFound;
    }
    for (index = 0u; index < count; index++) {
        HtmlNodeHandle child;

        if (HtmlNodeChildAt(table, index, &child, diag) != RoboOk) {
            return diag ? diag->code : RoboNotFound;
        }
        if (HtmlElementIsNamed(&child, "caption")) {
            *out = child;
            return RoboOk;
        }
    }
    return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                          "the table has no caption");
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

Result HtmlTableGetCapabilities(HtmlTableCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->structure = 1;
    out->indexes = 1;
    out->spansRead = 1;
    out->spanLayout = 0;
    out->caption = 1;
    return RoboOk;
}
