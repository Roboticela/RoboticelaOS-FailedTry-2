/*
 * RoboHTML Phase 17.6 table-structure suite.
 *
 * Covers rows, row groups (sections/bodies), cells, rowIndex/cellIndex,
 * colspan/rowspan reads, caption lookup, the capability table, and the one
 * rule a descendant walk gets wrong: a nested table's rows are not rows of the
 * outer table.
 *
 * The main fixture is parsed markup, so the structure under test is the one
 * the real tree builder produced. The nested-table case is built through the
 * DOM creation doors instead, because foster-parenting rules would otherwise
 * decide where the inner table lands and the point of the case is the query,
 * not the parser's recovery.
 */

#include "RoboHTML/RoboHTMLTable.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboTest.h"

#include "RoboHTMLDomTestSupport.h"

#include <stddef.h>
#include <string.h>

static const char TableMarkup[] =
    "<html><body>"
    "<table id=\"t\">"
    "<caption>Cap</caption>"
    "<colgroup><col><col></colgroup>"
    "<thead><tr><th>H1</th><th>H2</th></tr></thead>"
    "<tbody>"
    "<tr><td>a</td><td colspan=\"2\">b</td></tr>"
    "<tr><td rowspan=\"2\">c</td><td>d</td></tr>"
    "</tbody>"
    "<tfoot><tr><td>f</td></tr></tfoot>"
    "</table>"
    "</body></html>";

static HtmlDocumentId TableDoc(void)
{
    return DomDocumentOf(TableMarkup);
}

/* First element named @p name anywhere in the document, asserted to exist. */
static HtmlNodeHandle TableFirstOf(HtmlDocumentId id, const char *name)
{
    HtmlNodeHandle out;
    HtmlNodeHandle root = DomRoot(id);

    RoboCheckEqInt(HtmlNodeFindElement(&root, name, &out, NULL), RoboOk);
    return out;
}

static void TestTableCapabilities(void)
{
    HtmlTableCapabilities caps;

    memset(&caps, 0, sizeof(caps));
    RoboCheckEqInt(HtmlTableGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.structure, 1);
    RoboCheckEqInt(caps.indexes, 1);
    RoboCheckEqInt(caps.spansRead, 1);
    RoboCheckEqInt(caps.spanLayout, 0);
    RoboCheckEqInt(caps.caption, 1);
    RoboCheckEqInt(HtmlTableGetCapabilities(NULL), RoboInvalidArgument);
}

static void TestTableRows(void)
{
    HtmlDocumentId id = TableDoc();
    HtmlNodeHandle table = TableFirstOf(id, "table");
    size_t count = 0u;
    size_t visited = 0u;

    RoboCheckEqInt(HtmlTableGetRows(&table, NULL, NULL, &count, NULL), RoboOk);
    /* thead 1 + tbody 2 + tfoot 1 */
    RoboCheckEqInt((int)count, 4);

    (void)visited;
    /* A non-table is refused. */
    {
        HtmlNodeHandle body = TableFirstOf(id, "body");

        RoboCheckEqInt(HtmlTableGetRows(&body, NULL, NULL, &count, NULL),
                       RoboNotFound);
    }
    DomExpectCleanTree(id);
    HtmlShutdown();
}

static void TestTableSectionsBodies(void)
{
    HtmlDocumentId id = TableDoc();
    HtmlNodeHandle table = TableFirstOf(id, "table");
    size_t sections = 0u;
    size_t bodies = 0u;

    RoboCheckEqInt(HtmlTableGetSections(&table, NULL, NULL, &sections, NULL),
                   RoboOk);
    RoboCheckEqInt((int)sections, 3);
    RoboCheckEqInt(HtmlTableGetBodies(&table, NULL, NULL, &bodies, NULL),
                   RoboOk);
    RoboCheckEqInt((int)bodies, 1);

    HtmlShutdown();
}

static void TestTableSectionRows(void)
{
    HtmlDocumentId id = TableDoc();
    HtmlNodeHandle tbody = TableFirstOf(id, "tbody");
    size_t count = 0u;

    RoboCheckEqInt(HtmlTableSectionGetRows(&tbody, NULL, NULL, &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 2);

    HtmlShutdown();
}

static void TestTableCells(void)
{
    HtmlDocumentId id = TableDoc();
    HtmlNodeHandle table = TableFirstOf(id, "table");
    HtmlNodeHandle row;
    size_t rows = 0u;
    size_t cells = 0u;

    /* The first row is the thead row: two <th> cells. */
    RoboCheckEqInt(HtmlTableGetRows(&table, NULL, NULL, &rows, NULL), RoboOk);
    RoboCheckEqInt((int)rows, 4);
    RoboCheckEqInt(HtmlNodeFindElement(&table, "tr", &row, NULL), RoboOk);
    RoboCheckEqInt(HtmlTableRowGetCells(&row, NULL, NULL, &cells, NULL),
                   RoboOk);
    RoboCheckEqInt((int)cells, 2);

    /* A non-row is refused. */
    RoboCheckEqInt(HtmlTableRowGetCells(&table, NULL, NULL, &cells, NULL),
                   RoboNotFound);

    HtmlShutdown();
}

static void TestTableIndexes(void)
{
    HtmlDocumentId id = TableDoc();
    HtmlNodeHandle tbody = TableFirstOf(id, "tbody");
    HtmlNodeHandle row0;
    HtmlNodeHandle row1;
    HtmlNodeHandle cell0;
    HtmlNodeHandle cell1;
    size_t index = 99u;

    /* First tbody row -> cell index 0; the row itself is index 1 (after thead). */
    RoboCheckEqInt(HtmlNodeChildAt(&tbody, 0u, &row0, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeChildAt(&tbody, 1u, &row1, NULL), RoboOk);
    RoboCheckEqInt(HtmlTableRowIndex(&row0, &index, NULL), RoboOk);
    RoboCheckEqInt((int)index, 1);
    RoboCheckEqInt(HtmlTableRowIndex(&row1, &index, NULL), RoboOk);
    RoboCheckEqInt((int)index, 2);

    RoboCheckEqInt(HtmlNodeChildAt(&row0, 0u, &cell0, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeChildAt(&row0, 1u, &cell1, NULL), RoboOk);
    RoboCheckEqInt(HtmlTableCellIndex(&cell0, &index, NULL), RoboOk);
    RoboCheckEqInt((int)index, 0);
    RoboCheckEqInt(HtmlTableCellIndex(&cell1, &index, NULL), RoboOk);
    RoboCheckEqInt((int)index, 1);

    /* A detached/foreign node is not a row of a table. */
    RoboCheckEqInt(HtmlTableRowIndex(&cell0, &index, NULL), RoboNotFound);

    HtmlShutdown();
}

static void TestTableSpans(void)
{
    HtmlDocumentId id = TableDoc();
    HtmlNodeHandle tbody = TableFirstOf(id, "tbody");
    HtmlNodeHandle row0;
    HtmlNodeHandle row1;
    HtmlNodeHandle cell0;
    HtmlNodeHandle cell1;
    int span = 0;

    RoboCheckEqInt(HtmlNodeChildAt(&tbody, 0u, &row0, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeChildAt(&tbody, 1u, &row1, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeChildAt(&row0, 0u, &cell0, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeChildAt(&row0, 1u, &cell1, NULL), RoboOk);

    /* Default spans are 1. */
    RoboCheckEqInt(HtmlTableCellColSpan(&cell0, &span, NULL), RoboOk);
    RoboCheckEqInt(span, 1);
    RoboCheckEqInt(HtmlTableCellRowSpan(&cell0, &span, NULL), RoboOk);
    RoboCheckEqInt(span, 1);

    /* Explicit colspan / rowspan are read. */
    RoboCheckEqInt(HtmlTableCellColSpan(&cell1, &span, NULL), RoboOk);
    RoboCheckEqInt(span, 2);
    {
        HtmlNodeHandle rowspanCell;

        RoboCheckEqInt(HtmlNodeChildAt(&row1, 0u, &rowspanCell, NULL), RoboOk);
        RoboCheckEqInt(HtmlTableCellRowSpan(&rowspanCell, &span, NULL), RoboOk);
        RoboCheckEqInt(span, 2);
        RoboCheckEqInt(HtmlTableCellColSpan(&rowspanCell, &span, NULL), RoboOk);
        RoboCheckEqInt(span, 1);
    }

    /* A non-cell is refused. */
    RoboCheckEqInt(HtmlTableCellColSpan(&row0, &span, NULL), RoboNotFound);

    HtmlShutdown();
}

static void TestTableCaption(void)
{
    HtmlDocumentId id = TableDoc();
    HtmlNodeHandle table = TableFirstOf(id, "table");
    HtmlNodeHandle caption;
    char text[32];
    size_t length = 0u;

    RoboCheckEqInt(HtmlTableGetCaption(&table, &caption, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeTextContentGet(&caption, text, sizeof(text),
                                          &length, NULL),
                   RoboOk);
    RoboCheck(strcmp(text, "Cap") == 0);

    /* A table without a caption reports it rather than a zero handle. */
    {
        HtmlNodeHandle body = TableFirstOf(id, "body");

        RoboCheckEqInt(HtmlTableGetCaption(&body, &caption, NULL),
                       RoboNotFound);
    }

    HtmlShutdown();
}

static void TestTableNestedExcluded(void)
{
    HtmlDocumentId id = DomDocumentOf("<html><body></body></html>");
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle body = DomFindTag(&root, RoboHtmlTagBody);
    HtmlNodeHandle outer;
    HtmlNodeHandle outerRow;
    HtmlNodeHandle outerCell;
    HtmlNodeHandle inner;
    HtmlNodeHandle innerRow;
    size_t count = 0u;

    RoboCheckEqInt(HtmlDocumentCreateElement(id, "table", &outer, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &outer, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "tr", &outerRow, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&outer, &outerRow, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "td", &outerCell, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&outerRow, &outerCell, NULL), RoboOk);

    /* A nested table lives inside the outer cell. */
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "table", &inner, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&outerCell, &inner, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "tr", &innerRow, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&inner, &innerRow, NULL), RoboOk);

    /* The outer table has exactly one row; the nested row is not one of them. */
    RoboCheckEqInt(HtmlTableGetRows(&outer, NULL, NULL, &count, NULL), RoboOk);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt(HtmlTableGetRows(&inner, NULL, NULL, &count, NULL), RoboOk);
    RoboCheckEqInt((int)count, 1);
    DomExpectCleanTree(id);

    HtmlShutdown();
}

void TestRegisterHtmlTable(void)
{
    TestRegister("html.table.capabilities", TestTableCapabilities);
    TestRegister("html.table.rows", TestTableRows);
    TestRegister("html.table.sections_bodies", TestTableSectionsBodies);
    TestRegister("html.table.section_rows", TestTableSectionRows);
    TestRegister("html.table.cells", TestTableCells);
    TestRegister("html.table.indexes", TestTableIndexes);
    TestRegister("html.table.spans", TestTableSpans);
    TestRegister("html.table.caption", TestTableCaption);
    TestRegister("html.table.nested_excluded", TestTableNestedExcluded);
}
