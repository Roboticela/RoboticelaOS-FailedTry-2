/*
 * RoboLayout table suite (Phase 19.6) — layout.table.*
 *
 * Five things are under test, and they are told apart deliberately:
 *
 *   the **formatting context**: does `display: table` make a box establish a table
 *   context, does `inline-table` establish the same one, and does an **internal**
 *   table box (`table-row`, `table-cell`, ...) establish nothing because the table
 *   that owns it places it?
 *
 *   the **fixup**: a cell that is a direct child of the table is placed as an
 *   anonymous row, a row group contributes its rows in document order, a
 *   `table-column` box contributes a declared width and generates no geometry, and a
 *   child that is neither is *counted* rather than wrapped in the anonymous cell CSS
 *   would generate;
 *
 *   the **columns**: `table-layout: fixed` reads the first row's and the column
 *   boxes' declared widths and shares what is left equally, `table-layout: auto`
 *   takes a floor from each column's widest cell's min-content contribution and a
 *   ceiling from its max-content one and then distributes the container's width over
 *   the columns that can still grow, and a cell wider than its column is *counted*
 *   rather than clamped;
 *
 *   the **two axes**: a row's height is its tallest cell's (raised by the row's own
 *   declared height), a caption is placed on the block-start or block-end edge, and
 *   `border-spacing` supplies the gutters `border-collapse: separate` leaves
 *   (`collapse` zeroes them);
 *
 *   the **accounting**: the counters, and the identity the block record states over
 *   every object in the tree.
 *
 * The fixture fabricates a *typed* computed style per tag occurrence -- including
 * `table-layout`, `border-collapse`, `caption-side` and the carried
 * `border-spacing` -- because the point of the phase is that the pass reads those
 * *values* through 18.5's computed-value doors, and a case built from a stylesheet
 * could only prove the parser. The container is the `body`, exactly as the flex and
 * grid suites make theirs, so a fixture never has to ask the HTML parser what a
 * `<tr>` is: every box is a `div` whose display the suite states, and the tree the
 * table pass reads is the tree this suite wrote.
 *
 * Geometry is read back through the public result doors, so every expectation is a
 * number a consumer could read.
 */

#include "RoboLayoutTestSupport.h"

#include "RoboLayout/RoboLayoutIntrinsic.h"
#include "RoboLayout/RoboLayoutTable.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fixture that can declare a table                                  */
/* ------------------------------------------------------------------ */

typedef enum layoutTableDeclKind {
    LAYOUT_TABLE_DECL_PX = 0,
    /** A keyword-valued property: `table-layout: fixed`, `border-collapse:
     *  collapse`, `caption-side: bottom`. */
    LAYOUT_TABLE_DECL_KEYWORD = 1,
    /** A whole declaration written as text, held as the preserved tokens a real
     *  cascade carries: `border-spacing: 5px 10px`, whose two lengths are not one
     *  of 18.5's closed vocabularies. */
    LAYOUT_TABLE_DECL_TEXT = 2
} LayoutTableDeclKind;

typedef struct layoutTableDecl {
    const char *name;
    LayoutTableDeclKind kind;
    double number;
    const char *text;
} LayoutTableDecl;

#define LAYOUT_TABLE_MAX_DECLARATIONS 8u
#define LAYOUT_TABLE_MAX_RULES 24u
#define LAYOUT_TABLE_MAX_TAGS 16u

typedef struct layoutTableRule {
    const char *tag;
    int nth; /* which occurrence of @tag this rule styles, 0-based */
    CssDisplay display;
    LayoutTableDecl declarations[LAYOUT_TABLE_MAX_DECLARATIONS];
    size_t declarationsCount;
} LayoutTableRule;

typedef struct layoutTableStyle {
    char tag[16];
    int nth;
    CssComputedStyle *style;
} LayoutTableStyle;

typedef struct layoutTableTagCount {
    char tag[16];
    int seen;
} LayoutTableTagCount;

typedef struct layoutTableSource {
    LayoutTableStyle rules[LAYOUT_TABLE_MAX_RULES];
    size_t count;
    LayoutTableTagCount tagCounts[LAYOUT_TABLE_MAX_TAGS];
    size_t tagCount;
} LayoutTableSource;

static void LayoutTableRuleInit(LayoutTableRule *rule,
                                const char *tag,
                                int nth,
                                CssDisplay display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->nth = nth;
    rule->display = display;
}

static void LayoutTableRuleAdd(LayoutTableRule *rule,
                               const char *name,
                               LayoutTableDeclKind kind,
                               double number,
                               const char *text)
{
    RoboCheckEqInt(rule->declarationsCount < LAYOUT_TABLE_MAX_DECLARATIONS, 1);
    rule->declarations[rule->declarationsCount].name = name;
    rule->declarations[rule->declarationsCount].kind = kind;
    rule->declarations[rule->declarationsCount].number = number;
    rule->declarations[rule->declarationsCount].text = text;
    rule->declarationsCount++;
}

static void LayoutTableRulePx(LayoutTableRule *rule, const char *name, double px)
{
    LayoutTableRuleAdd(rule, name, LAYOUT_TABLE_DECL_PX, px, NULL);
}

static void LayoutTableRuleKeyword(LayoutTableRule *rule,
                                   const char *property,
                                   const char *keyword)
{
    LayoutTableRuleAdd(rule, property, LAYOUT_TABLE_DECL_KEYWORD, 0.0, keyword);
}

/** A whole declaration as text: `"border-spacing: 5px 10px"`. */
static void LayoutTableRuleText(LayoutTableRule *rule, const char *text)
{
    LayoutTableRuleAdd(rule, NULL, LAYOUT_TABLE_DECL_TEXT, 0.0, text);
}

static void LayoutTableStyleAdd(LayoutTableSource *source,
                                const LayoutTableRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    size_t index;
    size_t slot;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_TABLE_MAX_RULES, 1);
    slot = source->count;
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeDisplay(&value, rule->display, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                            CssSliceFromText("display"), 0u,
                                            &specified, &value, NULL),
                   RoboOk);

    for (index = 0u; index < rule->declarationsCount; index++) {
        const LayoutTableDecl *decl = &rule->declarations[index];

        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        switch (decl->kind) {
        case LAYOUT_TABLE_DECL_TEXT: {
            /*
             * A carried declaration is read from the *preserved* computed value --
             * the same shape 18.5 leaves `border-spacing` in -- so the fixture parses
             * the declaration the way the cascade would and keeps the tokens.
             */
            CssDeclarationList list = RoboCssCleared;
            CssError error = RoboCssCleared;
            size_t declared;
            size_t count;

            RoboCheckEqInt(CssParseDeclarations(decl->text, &list, &error), RoboOk);
            count = CssDeclarationListCount(&list);
            RoboCheckEqInt(count > 0u, 1);
            for (declared = 0u; declared < count; declared++) {
                const CssDeclaration *declaration =
                    CssDeclarationListAt(&list, declared);

                RoboCheck(declaration != NULL);
                if (!declaration) {
                    continue;
                }
                value = (CssComputedValue)RoboCssCleared;
                specified = (CssValue)RoboCssCleared;
                RoboCheckEqInt(CssComputedValueMakePreserved(
                                   &value, CssDeclarationValue(declaration), NULL),
                               RoboOk);
                RoboCheckEqInt(CssComputedStyleSetEntry(
                                   style, RoboCssPropertyIdNone,
                                   CssDeclarationName(declaration), 0u, &specified,
                                   &value, NULL),
                               RoboOk);
            }
            CssDeclarationListDispose(&list);
            continue;
        }
        case LAYOUT_TABLE_DECL_KEYWORD:
            RoboCheckEqInt(CssComputedValueMakeKeyword(
                               &value, CssSliceFromText(decl->text), NULL),
                           RoboOk);
            break;
        case LAYOUT_TABLE_DECL_PX:
        default:
            RoboCheckEqInt(CssComputedValueMakeLength(&value, decl->number,
                                                      RoboCssUnitPx, 0, NULL),
                           RoboOk);
            break;
        }
        RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                CssSliceFromText(decl->name), 0u,
                                                &specified, &value, NULL),
                       RoboOk);
    }

    strncpy(source->rules[slot].tag, rule->tag,
            sizeof(source->rules[slot].tag) - 1u);
    source->rules[slot].tag[sizeof(source->rules[slot].tag) - 1u] = '\0';
    source->rules[slot].nth = rule->nth;
    source->rules[slot].style = style;
    source->count++;
}

static int LayoutTableTagCountFor(LayoutTableSource *source, const char *tag)
{
    size_t index;

    for (index = 0u; index < source->tagCount; index++) {
        if (strcmp(source->tagCounts[index].tag, tag) == 0) {
            return source->tagCounts[index].seen++;
        }
    }

    RoboCheckEqInt(source->tagCount < LAYOUT_TABLE_MAX_TAGS, 1);
    strncpy(source->tagCounts[source->tagCount].tag, tag,
            sizeof(source->tagCounts[0].tag) - 1u);
    source->tagCounts[source->tagCount].tag[sizeof(source->tagCounts[0].tag) - 1u] =
        '\0';
    source->tagCounts[source->tagCount].seen = 1;
    source->tagCount++;
    return 0;
}

static const CssComputedStyle *LayoutTableStyleLookup(void *userdata,
                                                      const HtmlNodeHandle *element)
{
    LayoutTableSource *source = (LayoutTableSource *)userdata;
    const char *name;
    int nth;
    size_t index;

    if (!source || !element) {
        return NULL;
    }
    name = HtmlElementName(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    nth = LayoutTableTagCountFor(source, name);
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, name) == 0 &&
            source->rules[index].nth == nth) {
            return source->rules[index].style;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* A scripted text measurer                                            */
/* ------------------------------------------------------------------ */

/*
 * A table sizes a column from its cells' **min-content** and **max-content**
 * contributions, so a case about content sizing needs a measurer rather than a
 * document. This one is arithmetic rather than a font: every code point is
 * `perCodePoint` wide and every run is `height` tall, so a case can say "a
 * two-letter word is 20 wide" without depending on a face, and `min-content` (the
 * widest word) differs from `max-content` (the whole run) exactly when the text has
 * a space in it.
 */
typedef struct layoutTableScript {
    int32_t perCodePoint;
    int32_t height;
} LayoutTableScript;

static Result LayoutTableScriptMeasure(void *userdata,
                                       const LayoutTextRequest *request,
                                       LayoutTextRun *outRun)
{
    LayoutTableScript *script = (LayoutTableScript *)userdata;

    if (!script || !request || !outRun || !request->style) {
        return RoboInvalidArgument;
    }
    outRun->source = request->textNode;
    outRun->range = request->range;
    outRun->direction = request->style->direction;
    outRun->codePoints = LayoutTextRangeLength(request->range);
    outRun->width = (int32_t)outRun->codePoints * script->perCodePoint;
    outRun->height = script->height;
    outRun->ascent = script->height;
    outRun->descent = 0;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

typedef struct layoutTableFixture {
    LayoutDocument doc;
    LayoutTableSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
    LayoutTableScript script;
    LayoutTextMetrics metrics;
    int useScript;
} LayoutTableFixture;

static void LayoutTableBegin(LayoutTableFixture *fixture,
                             const char *html,
                             const LayoutTableRule *rules,
                             size_t ruleCount)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < ruleCount; index++) {
        LayoutTableStyleAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutTableStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                               &options, NULL, NULL),
                   RoboOk);
}

static void LayoutTableEnd(LayoutTableFixture *fixture)
{
    size_t index;

    LayoutTreeDestroy(fixture->tree);
    fixture->tree = NULL;
    for (index = 0u; index < fixture->source.count; index++) {
        CssComputedStyleDestroy(fixture->source.rules[index].style);
        fixture->source.rules[index].style = NULL;
    }
    fixture->source.count = 0u;
    fixture->source.tagCount = 0u;
    LayoutDocumentRelease(&fixture->doc);
}

#define LAYOUT_TABLE_VIEWPORT_WIDTH 800
#define LAYOUT_TABLE_VIEWPORT_HEIGHT 600

/** Install the scripted measurer: @p perCodePoint px per code point, @p height px
 *  tall. Called after the tree is built and before the pass. */
static void LayoutTableScriptUse(LayoutTableFixture *fixture,
                                 int32_t perCodePoint,
                                 int32_t height)
{
    fixture->script.perCodePoint = perCodePoint;
    fixture->script.height = height;
    LayoutTextMetricsInit(&fixture->metrics);
    fixture->metrics.userdata = &fixture->script;
    fixture->metrics.measure = LayoutTableScriptMeasure;
    fixture->useScript = 1;
}

static void LayoutTablePass(LayoutTableFixture *fixture,
                            int32_t viewportWidth,
                            LayoutStats *outStats)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;

    viewport.width = viewportWidth;
    viewport.height = LAYOUT_TABLE_VIEWPORT_HEIGHT;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    if (fixture->useScript) {
        RoboCheckEqInt(LayoutContextSetTextMetrics(context, &fixture->metrics),
                       RoboOk);
    }
    RoboCheckEqInt(LayoutContextRun(context, fixture->tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);
    if (outStats) {
        *outStats = stats;
    }
}

static LayoutNodeId LayoutTableBoxNth(LayoutTableFixture *fixture,
                                      const char *tag,
                                      size_t nth)
{
    /* The nth *box* for @tag in document order: creation order is document order,
     * and the box is found by matching the DOM element name rather than by index, so
     * a fixture that gained an element above this one would still resolve. */
    size_t seen = 0u;
    size_t count = LayoutTreeObjectCount(fixture->tree);
    size_t index;

    for (index = 0u; index < count; index++) {
        LayoutNodeId id = (LayoutNodeId)(index + 1u);
        LayoutObject copy;
        const HtmlNodeHandle *node;
        const char *name;

        if (LayoutObjectGet(fixture->tree, id, &copy, NULL) != RoboOk) {
            continue;
        }
        if (copy.kind != RoboLayoutObjectBox) {
            continue;
        }
        node = &copy.domNode;
        if (!HtmlNodeHandleIsWellformed(node)) {
            continue;
        }
        name = HtmlElementName(node, NULL);
        if (!name || strcmp(name, tag) != 0) {
            continue;
        }
        if (seen == nth) {
            return id;
        }
        seen++;
    }
    RoboCheck(0);
    return RoboLayoutNodeIdNone;
}

static LayoutResult LayoutTableResultOf(LayoutTableFixture *fixture, LayoutNodeId id)
{
    LayoutResult result;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    return result;
}

static LayoutSize LayoutTableSizeOf(LayoutTableFixture *fixture, LayoutNodeId id)
{
    LayoutResult result = LayoutTableResultOf(fixture, id);
    LayoutSize size;

    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    return size;
}

static LayoutPoint LayoutTablePositionOf(LayoutTableFixture *fixture,
                                         LayoutNodeId id)
{
    LayoutResult result = LayoutTableResultOf(fixture, id);
    LayoutPoint position;

    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    return position;
}

/** Non-zero when the box was given geometry at all. A box the pass *declined* --
 *  a skipped child, a `table-column` -- answers 0, which is the fact that tells
 *  "placed with a zero size" from "not placed". */
static int LayoutTableIsPlaced(LayoutTableFixture *fixture, LayoutNodeId id)
{
    return LayoutTableResultOf(fixture, id).state == RoboLayoutResultComputed;
}

static int32_t LayoutTableXOf(LayoutTableFixture *fixture, size_t nth)
{
    return LayoutTablePositionOf(fixture, LayoutTableBoxNth(fixture, "div", nth)).x;
}

static int32_t LayoutTableYOf(LayoutTableFixture *fixture, size_t nth)
{
    return LayoutTablePositionOf(fixture, LayoutTableBoxNth(fixture, "div", nth)).y;
}

static int32_t LayoutTableWidthOf(LayoutTableFixture *fixture, size_t nth)
{
    return LayoutTableSizeOf(fixture, LayoutTableBoxNth(fixture, "div", nth)).width;
}

static int32_t LayoutTableHeightOf(LayoutTableFixture *fixture, size_t nth)
{
    return LayoutTableSizeOf(fixture, LayoutTableBoxNth(fixture, "div", nth)).height;
}

/** The body's own result size: the extent the table's contents reached. */
static LayoutSize LayoutTableBodySize(LayoutTableFixture *fixture)
{
    return LayoutTableSizeOf(fixture, LayoutTableBoxNth(fixture, "body", 0u));
}

/** Install the container's shared declarations: a table with a definite width, so
 *  the columns have something to divide. */
static void LayoutTableBody(LayoutTableRule *rule, double width)
{
    LayoutTableRuleInit(rule, "body", 0, RoboCssDisplayTable);
    LayoutTableRulePx(rule, "width", width);
}

/* `table-layout: fixed` and a two-cell row whose first row declares the columns:
 * the fixture every exact-geometry case shares. */
#define LAYOUT_TABLE_TWO_CELLS_HTML                                  \
    "<html><body>"                                                   \
    "<div><div></div><div></div></div>"                              \
    "<div><div></div><div></div></div>"                              \
    "</body></html>"

/* ------------------------------------------------------------------ */
/* The capability                                                      */
/* ------------------------------------------------------------------ */

static void TestTableCapability(void)
{
    LayoutCapabilities caps;
    LayoutTableCounters a;
    LayoutTableCounters b;

    LayoutFixture();
    RoboCheckEqInt(RoboLayoutTableLayoutImplemented, 1);
    RoboCheckEqInt(LayoutGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.tableLayout, RoboLayoutTableLayoutImplemented);

    /* The counters record is a value with a defined zero: clearing it and copying it
     * are the same state, and one field moved apart is a difference. */
    LayoutTableCountersInit(&a);
    b = a;
    RoboCheckEqInt(LayoutTableCountersEqual(&a, &b), 1);
    RoboCheckEqInt(LayoutTableCountersEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutTableCountersEqual(&a, NULL), 0);
    b.cellsPlaced = 1u;
    RoboCheckEqInt(LayoutTableCountersEqual(&a, &b), 0);
    RoboCheckEqInt(a.cellsPlaced, 0u);
    RoboCheckEqInt(a.containers, 0u);

    /* And the record is part of the block record's own identity: two block records
     * that placed different tables are not equal, which is the assertion that would
     * have caught the nested record being left out of the comparison. */
    {
        LayoutBlockStats left;
        LayoutBlockStats right;

        LayoutBlockStatsInit(&left);
        right = left;
        RoboCheckEqInt(LayoutBlockStatsEqual(&left, &right), 1);
        right.table.containers = 1u;
        RoboCheckEqInt(LayoutBlockStatsEqual(&left, &right), 0);
    }
}

/* ------------------------------------------------------------------ */
/* The formatting context                                              */
/* ------------------------------------------------------------------ */

static void TestTableFormattingContext(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[2];
    LayoutNodeId body;
    LayoutFcId owned = RoboLayoutFcIdNone;
    LayoutFormattingContext context;

    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRow);

    LayoutTableBegin(&fixture, "<html><body><div></div></body></html>", rules, 2u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, NULL);

    body = LayoutTableBoxNth(&fixture, "body", 0u);
    RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, body, &owned, NULL),
                   RoboOk);
    RoboCheckEqInt(LayoutTreeContext(fixture.tree, owned, &context, NULL), RoboOk);
    RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcTable);

    LayoutTableEnd(&fixture);

    /*
     * `inline-table` is the same inner display at the inline level, and it owns the
     * same context: the box participates in its parent's inline context as one atomic
     * unit, and its *own* contents are a table. The classifier says so
     * (`LayoutFcEstablishes`) and the classification is what this half asserts; the box
     * itself has no line to be on in this build's root context, which is the same fact
     * `layout.grid.formatting_context` records for `inline-grid`.
     */
    LayoutTableRuleInit(&rules[0], "body", 0, RoboCssDisplayInlineTable);
    LayoutTableRulePx(&rules[0], "width", 300.0);

    LayoutTableBegin(&fixture, "<html><body><div></div></body></html>", rules, 2u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, NULL);

    body = LayoutTableBoxNth(&fixture, "body", 0u);
    owned = RoboLayoutFcIdNone;
    RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, body, &owned, NULL),
                   RoboOk);
    RoboCheckEqInt(LayoutTreeContext(fixture.tree, owned, &context, NULL), RoboOk);
    RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcTable);

    LayoutTableEnd(&fixture);

    /*
     * And the contrast the third outer kind exists for: a `table-row` is **not**
     * block-level, so a row whose table is missing from the tree is neither placed nor
     * handed to the table pass -- it is counted, and the case below is the one that
     * asserts the count.
     */
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRow);
    RoboCheckEqInt(CssDisplayIsBlockLevel(RoboCssDisplayTableRow), 0);
    RoboCheckEqInt(CssDisplayIsInlineLevel(RoboCssDisplayTableRow), 0);
    RoboCheckEqInt(LayoutFcDisplayCanEstablish(RoboCssDisplayTable), 1);
    RoboCheckEqInt(LayoutFcDisplayCanEstablish(RoboCssDisplayTableRow), 0);
    RoboCheckEqInt(LayoutFcDisplayIsBlockContainer(RoboCssDisplayTableCell), 1);
    RoboCheckEqInt(LayoutFcDisplayIsBlockContainer(RoboCssDisplayTableRow), 0);
}

/* ------------------------------------------------------------------ */
/* Rows and cells                                                      */
/* ------------------------------------------------------------------ */

static void TestTableRowsAndCells(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[7];
    LayoutStats stats;
    size_t index;

    /*
     * One rule per `div`, in the fixture's document order: `0` and `3` are the two
     * rows and the four between them the cells. The **first** row's cells declare
     * 100px and the second row's 200px, which is the fact `table-layout: fixed`
     * turns on: the columns come from the first row, and a later row's declaration
     * neither moves them nor is clamped by them -- the wide cell is *counted*.
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleKeyword(&rules[0], "table-layout", "fixed");
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRow);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[2], "width", 100.0);
    LayoutTableRulePx(&rules[2], "height", 20.0);
    LayoutTableRuleInit(&rules[3], "div", 2, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[3], "width", 100.0);
    LayoutTableRulePx(&rules[3], "height", 20.0);
    LayoutTableRuleInit(&rules[4], "div", 3, RoboCssDisplayTableRow);
    for (index = 5u; index < 7u; index++) {
        LayoutTableRuleInit(&rules[index], "div", (int)index - 1,
                            RoboCssDisplayTableCell);
        LayoutTableRulePx(&rules[index], "width", 200.0);
        LayoutTableRulePx(&rules[index], "height", 32.0);
    }

    LayoutTableBegin(&fixture, LAYOUT_TABLE_TWO_CELLS_HTML, rules, 7u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    /*
     * `table-layout: fixed` takes each column's width from the first row's cells: two
     * columns of 100, and the 100px left over is not shared out because every column
     * declared a width. The cells therefore sit at x = 0 and x = 100 in both rows --
     * while the second row's own cells are 200 wide, which the column does not and
     * cannot honour.
     */
    RoboCheckEqInt(LayoutTableXOf(&fixture, 1u), 0);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 1u), 100);
    RoboCheckEqInt(LayoutTableXOf(&fixture, 2u), 100);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 2u), 100);
    RoboCheckEqInt(LayoutTableXOf(&fixture, 4u), 0);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 4u), 200);
    RoboCheckEqInt(LayoutTableXOf(&fixture, 5u), 100);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 5u), 200);

    /* The rows stack: the first row's box is the strip its cells are in, and the
     * second begins below it. A row is as tall as its tallest cell (20, then 32). */
    RoboCheckEqInt(LayoutTableYOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutTableHeightOf(&fixture, 0u), 20);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 3u), 20);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 4u), 20);
    RoboCheckEqInt(LayoutTableHeightOf(&fixture, 3u), 32);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 1u), 0);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 2u), 0);

    /* A row is written at the container's content width, and the container's own
     * height is the rows' band. */
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 0u), 300);
    RoboCheckEqInt(LayoutTableBodySize(&fixture).height, 52);

    RoboCheckEqInt(stats.block.table.containers, 1u);
    RoboCheckEqInt(stats.block.table.rows, 2u);
    RoboCheckEqInt(stats.block.table.columns, 2u);
    RoboCheckEqInt(stats.block.table.cellsPlaced, 4u);
    RoboCheckEqInt(stats.block.table.fixedColumns, 2u);
    RoboCheckEqInt(stats.block.table.contentColumns, 0u);
    RoboCheckEqInt(stats.block.table.groups, 0u);
    RoboCheckEqInt(stats.block.table.captions, 0u);
    RoboCheckEqInt(stats.block.table.anonymousRows, 0u);
    RoboCheckEqInt(stats.block.table.itemsSkipped, 0u);
    RoboCheckEqInt(stats.block.table.cellsEmpty, 4u);
    /* Both of the second row's cells are wider than the column they sit in. */
    RoboCheckEqInt(stats.block.table.overflowingCells, 2u);

    LayoutTableEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The columns                                                         */
/* ------------------------------------------------------------------ */

static void TestTableAutoColumns(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[4];
    LayoutStats stats;

    /*
     * No `table-layout` at all: the initial `auto` sizes each column from the cells
     * in it. Each cell holds `"ab cd"`, which is a 20px widest word and a 50px whole
     * run at ten pixels a code point -- so each column's floor is 20 and its ceiling
     * 50, and the container's 300px is shared out in proportion to the room each
     * column can still take: 130 each, for two columns of 150.
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRow);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[2], "height", 20.0);
    LayoutTableRuleInit(&rules[3], "div", 2, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[3], "height", 20.0);

    LayoutTableBegin(&fixture,
                     "<html><body>"
                     "<div><div>ab cd</div><div>ab cd</div></div>"
                     "</body></html>",
                     rules, 4u);
    LayoutTableScriptUse(&fixture, 10, 16);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(LayoutTableXOf(&fixture, 1u), 0);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 1u), 150);
    RoboCheckEqInt(LayoutTableXOf(&fixture, 2u), 150);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 2u), 150);
    RoboCheckEqInt(stats.block.table.contentColumns, 2u);
    RoboCheckEqInt(stats.block.table.fixedColumns, 0u);
    RoboCheckEqInt(stats.block.table.columns, 2u);

    LayoutTableEnd(&fixture);
}

static void TestTableColumnBox(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[5];
    LayoutStats stats;

    /*
     * A `table-column` box contributes a **declared width** and generates no
     * geometry: CSS has no column box on the page. The width it states is column
     * 0's, and because column 0 cannot grow past it the free space goes to the
     * column that can -- column 1, whose cells ask for 50 and are given 200.
     *
     * The HTML is one column box, one row and the row's two cells; the cell contents
     * are the same `"ab cd"` as the case above, so the two cases differ in exactly one
     * declaration.
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableColumn);
    LayoutTableRulePx(&rules[1], "width", 100.0);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayTableRow);
    LayoutTableRuleInit(&rules[3], "div", 2, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[3], "height", 20.0);
    LayoutTableRuleInit(&rules[4], "div", 3, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[4], "height", 20.0);

    LayoutTableBegin(&fixture,
                     "<html><body>"
                     "<div></div>"
                     "<div><div>ab cd</div><div>ab cd</div></div>"
                     "</body></html>",
                     rules, 5u);
    LayoutTableScriptUse(&fixture, 10, 16);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(stats.block.table.columnBoxes, 1u);
    RoboCheckEqInt(stats.block.table.columns, 2u);
    /*
     * The column box is counted as an object this pass declined rather than as one it
     * placed -- it has no geometry, and saying so is what keeps the block record's
     * `unreached` difference closing over this direction.
     */
    RoboCheckEqInt(LayoutTableIsPlaced(&fixture, LayoutTableBoxNth(&fixture, "div", 0u)),
                   0);
    RoboCheckEqInt(stats.block.inlineLevelSkipped >= 1u, 1);

    RoboCheckEqInt(LayoutTableXOf(&fixture, 2u), 0);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 2u), 100);
    RoboCheckEqInt(LayoutTableXOf(&fixture, 3u), 100);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 3u), 200);
    RoboCheckEqInt(stats.block.table.fixedColumns, 1u);
    RoboCheckEqInt(stats.block.table.contentColumns, 1u);

    LayoutTableEnd(&fixture);
}

static void TestTableOverflowingCell(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[4];
    LayoutStats stats;

    /*
     * A cell whose own declared width is wider than the column it sits in is
     * **counted** rather than clamped: an overflowing cell is CSS's answer, and
     * hiding it would be this build's. Column 0 is 40 wide -- the widest thing in it
     * is a four-letter word -- and its cell declares 200.
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRow);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[2], "width", 200.0);
    LayoutTableRulePx(&rules[2], "height", 20.0);
    LayoutTableRuleInit(&rules[3], "div", 2, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[3], "height", 20.0);

    LayoutTableBegin(&fixture,
                     "<html><body>"
                     "<div><div>abcd</div><div>ab cd</div></div>"
                     "</body></html>",
                     rules, 4u);
    LayoutTableScriptUse(&fixture, 10, 16);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    /* 40 (the first column's content floor) and 260 (the second column's floor plus
     * every pixel the first column could not claim). */
    RoboCheckEqInt(LayoutTableXOf(&fixture, 1u), 0);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 1u), 200);
    RoboCheckEqInt(LayoutTableXOf(&fixture, 2u), 40);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 2u), 260);
    RoboCheckEqInt(stats.block.table.overflowingCells, 1u);
    RoboCheckEqInt(stats.block.table.cellsPlaced, 2u);

    LayoutTableEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The block axis: rows, groups, captions                              */
/* ------------------------------------------------------------------ */

static void TestTableRowHeight(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[4];

    /*
     * A row is as tall as its **tallest** cell: the first cell's 20px and the second's
     * 32px make a 32px row, and each cell keeps its own height inside it.
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleKeyword(&rules[0], "table-layout", "fixed");
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRow);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[2], "width", 100.0);
    LayoutTableRulePx(&rules[2], "height", 20.0);
    LayoutTableRuleInit(&rules[3], "div", 2, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[3], "width", 100.0);
    LayoutTableRulePx(&rules[3], "height", 32.0);

    LayoutTableBegin(&fixture,
                     "<html><body>"
                     "<div><div></div><div></div></div>"
                     "</body></html>",
                     rules, 4u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, NULL);

    RoboCheckEqInt(LayoutTableHeightOf(&fixture, 0u), 32);
    RoboCheckEqInt(LayoutTableHeightOf(&fixture, 1u), 20);
    RoboCheckEqInt(LayoutTableHeightOf(&fixture, 2u), 32);
    RoboCheckEqInt(LayoutTableBodySize(&fixture).height, 32);

    LayoutTableEnd(&fixture);

    /*
     * And the row's **own** declared height raises it where the author asked for more
     * room than the cells needed -- the same precedence every other declared height
     * has. The cells do not grow with it: they are what they were.
     */
    LayoutTableRulePx(&rules[1], "height", 40.0);

    LayoutTableBegin(&fixture,
                     "<html><body>"
                     "<div><div></div><div></div></div>"
                     "</body></html>",
                     rules, 4u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, NULL);

    RoboCheckEqInt(LayoutTableHeightOf(&fixture, 0u), 40);
    RoboCheckEqInt(LayoutTableHeightOf(&fixture, 1u), 20);
    RoboCheckEqInt(LayoutTableHeightOf(&fixture, 2u), 32);
    RoboCheckEqInt(LayoutTableBodySize(&fixture).height, 40);

    LayoutTableEnd(&fixture);
}

static void TestTableRowGroup(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[8];
    LayoutStats stats;

    /*
     * A row group is a box of its own -- one rectangle over the rows it holds, which
     * is what makes a `table-row-group` something a paint pass can draw a background
     * on -- and its rows are the table's rows in document order.
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleKeyword(&rules[0], "table-layout", "fixed");
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRowGroup);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayTableRow);
    LayoutTableRuleInit(&rules[3], "div", 2, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[3], "width", 100.0);
    LayoutTableRulePx(&rules[3], "height", 20.0);
    LayoutTableRuleInit(&rules[4], "div", 3, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[4], "width", 100.0);
    LayoutTableRulePx(&rules[4], "height", 20.0);
    LayoutTableRuleInit(&rules[5], "div", 4, RoboCssDisplayTableRow);
    LayoutTableRuleInit(&rules[6], "div", 5, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[6], "width", 100.0);
    LayoutTableRulePx(&rules[6], "height", 20.0);
    LayoutTableRuleInit(&rules[7], "div", 6, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[7], "width", 100.0);
    LayoutTableRulePx(&rules[7], "height", 20.0);

    LayoutTableBegin(&fixture,
                     "<html><body>"
                     "<div>"
                     "<div><div></div><div></div></div>"
                     "<div><div></div><div></div></div>"
                     "</div>"
                     "</body></html>",
                     rules, 8u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    /* The group spans both rows: from the first row's top edge to the last row's
     * bottom edge, at the container's content width. */
    RoboCheckEqInt(LayoutTableYOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutTableHeightOf(&fixture, 0u), 40);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 0u), 300);
    /* And the rows are inside it: the second begins at the first row's height. */
    RoboCheckEqInt(LayoutTableYOf(&fixture, 1u), 0);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 4u), 20);
    RoboCheckEqInt(stats.block.table.groups, 1u);
    RoboCheckEqInt(stats.block.table.rows, 2u);
    RoboCheckEqInt(stats.block.table.cellsPlaced, 4u);

    LayoutTableEnd(&fixture);
}

/*
 * A caption with a block child 8px tall, so the caption has an extent the rows can
 * start below (or end above): a caption's height is its contents', exactly as every
 * other block container's is.
 */
#define LAYOUT_TABLE_CAPTION_HTML                                    \
    "<html><body>"                                                   \
    "<div><div></div></div>"                                         \
    "<div><div></div><div></div></div>"                              \
    "</body></html>"

static void LayoutTableCaptionRules(LayoutTableRule *rules,
                                    const char *captionSide)
{
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleKeyword(&rules[0], "table-layout", "fixed");
    if (captionSide) {
        LayoutTableRuleKeyword(&rules[0], "caption-side", captionSide);
    }
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableCaption);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutTableRulePx(&rules[2], "height", 8.0);
    LayoutTableRuleInit(&rules[3], "div", 2, RoboCssDisplayTableRow);
    LayoutTableRuleInit(&rules[4], "div", 3, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[4], "width", 100.0);
    LayoutTableRulePx(&rules[4], "height", 20.0);
    LayoutTableRuleInit(&rules[5], "div", 4, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[5], "width", 100.0);
    LayoutTableRulePx(&rules[5], "height", 20.0);
}

static void TestTableCaptionTop(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[6];
    LayoutStats stats;

    LayoutTableCaptionRules(rules, NULL);

    LayoutTableBegin(&fixture, LAYOUT_TABLE_CAPTION_HTML, rules, 6u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    /*
     * `caption-side`'s initial value is `top`, so the caption is placed on the table's
     * block-start edge and the rows follow it: the caption is the first thing in the
     * table's block flow, and the table is as tall as the two together.
     */
    RoboCheckEqInt(LayoutTableYOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutTableHeightOf(&fixture, 0u), 8);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 2u), 8);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 3u), 8);
    RoboCheckEqInt(LayoutTableBodySize(&fixture).height, 28);
    RoboCheckEqInt(stats.block.table.captions, 1u);
    RoboCheckEqInt(stats.block.table.rows, 1u);

    LayoutTableEnd(&fixture);
}

static void TestTableCaptionBottom(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[6];
    LayoutStats stats;

    LayoutTableCaptionRules(rules, "bottom");

    LayoutTableBegin(&fixture, LAYOUT_TABLE_CAPTION_HTML, rules, 6u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    /* `caption-side: bottom` moves the caption to the block-end edge: the rows take
     * the top of the table and the caption sits under them. */
    RoboCheckEqInt(LayoutTableYOf(&fixture, 2u), 0);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 3u), 0);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 0u), 20);
    RoboCheckEqInt(LayoutTableHeightOf(&fixture, 0u), 8);
    RoboCheckEqInt(LayoutTableBodySize(&fixture).height, 28);
    RoboCheckEqInt(stats.block.table.captions, 1u);

    LayoutTableEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The fixup                                                           */
/* ------------------------------------------------------------------ */

static void TestTableAnonymousRow(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[3];
    LayoutStats stats;

    /*
     * Two cells directly inside the table and no rows at all: CSS 2.1 §17.2.1 wraps
     * each in the anonymous row it needs, and this build *reads* that fixup rather
     * than generating boxes -- so each cell becomes a row of one, stacked down the
     * block axis, and the counter says the fixup was real work.
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleKeyword(&rules[0], "table-layout", "fixed");
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[1], "width", 100.0);
    LayoutTableRulePx(&rules[1], "height", 20.0);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[2], "width", 100.0);
    LayoutTableRulePx(&rules[2], "height", 20.0);

    LayoutTableBegin(&fixture, "<html><body><div></div><div></div></body></html>",
                     rules, 3u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(stats.block.table.anonymousRows, 2u);
    RoboCheckEqInt(stats.block.table.rows, 2u);
    RoboCheckEqInt(stats.block.table.columns, 1u);
    RoboCheckEqInt(stats.block.table.cellsPlaced, 2u);
    RoboCheckEqInt(LayoutTableXOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 1u), 20);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 0u), 100);
    RoboCheckEqInt(LayoutTableBodySize(&fixture).height, 40);

    LayoutTableEnd(&fixture);
}

static void TestTableInternalOutsideTable(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[4];
    LayoutStats stats;

    /*
     * A `table-cell` with no table above it, beside a block-level box so that their
     * container is a block formatting context (a container whose *only* child is a
     * table-internal box has no **block-level** child at all, so it would establish an
     * inline context and the cell would be declined by 10.4's pass instead -- a true
     * fact, and a different one from the one this case is about). CSS 2.1 §17.2.1's
     * answer to a cell with no table is the anonymous table, row group and row it
     * would need; this build makes no boxes, so the honest answer is the one the grid
     * pass gives an anonymous grid item -- count it, leave it UNRESOLVED, and say so.
     * It is counted in **both** buckets on purpose: the table record's own
     * `itemsSkipped` and the block record's `inlineLevelSkipped`, which is the field
     * the `unreached` difference closes over.
     */
    LayoutTableRuleInit(&rules[0], "body", 0, RoboCssDisplayBlock);
    LayoutTableRulePx(&rules[0], "width", 300.0);
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[2], "height", 20.0);
    LayoutTableRuleInit(&rules[3], "div", 2, RoboCssDisplayBlock);
    LayoutTableRulePx(&rules[3], "height", 20.0);

    LayoutTableBegin(&fixture,
                     "<html><body><div></div><div><div></div></div></body></html>",
                     rules, 4u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(stats.block.table.containers, 0u);
    RoboCheckEqInt(stats.block.table.rows, 0u);
    RoboCheckEqInt(stats.block.table.cellsPlaced, 0u);
    RoboCheckEqInt(stats.block.table.itemsSkipped, 1u);
    RoboCheckEqInt(LayoutTableIsPlaced(&fixture, LayoutTableBoxNth(&fixture, "div", 0u)),
                   1);
    RoboCheckEqInt(LayoutTableIsPlaced(&fixture, LayoutTableBoxNth(&fixture, "div", 1u)),
                   0);
    /*
     * Two objects were declined for a stated reason -- the parser's implicit `<head>`,
     * which is an inline-level box in the root's block context, and the cell -- and the
     * block-level box inside the *declined* cell is the one object nobody could reach,
     * which is exactly what `unreached` counts.
     */
    RoboCheckEqInt(stats.block.inlineLevelSkipped, 2u);
    RoboCheckEqInt(stats.block.unreached, 1u);
    RoboCheckEqInt(LayoutTableIsPlaced(&fixture, LayoutTableBoxNth(&fixture, "div", 2u)),
                   0);

    LayoutTableEnd(&fixture);

    /*
     * And the same answer *inside* a table, which is the other half of the fixup: a
     * child that is neither a table-internal box nor a caption is counted rather than
     * wrapped in the anonymous cell CSS would generate, so a table whose content is a
     * stray block places no cells at all and says how many children it could not use.
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutTableRulePx(&rules[1], "height", 20.0);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutTableRulePx(&rules[2], "height", 20.0);

    LayoutTableBegin(&fixture, "<html><body><div></div><div></div></body></html>",
                     rules, 3u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(stats.block.table.containers, 1u);
    RoboCheckEqInt(stats.block.table.rows, 0u);
    RoboCheckEqInt(stats.block.table.columns, 0u);
    RoboCheckEqInt(stats.block.table.itemsSkipped, 2u);
    RoboCheckEqInt(LayoutTableBodySize(&fixture).height, 0);

    LayoutTableEnd(&fixture);
}

static void TestTableEmptyCells(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[5];
    LayoutStats stats;

    /*
     * `empty-cells`'s subject, counted rather than acted on: a cell with no in-flow
     * **box** child. The first cell is empty and the second holds only a text run --
     * which is not a box, and which a table cell lays out through the block pass as
     * inline content with no line to be on -- so the two are the same state to this
     * count. The third holds a box, so it is not empty, and all three are placed:
     * this build paints no cell, so an empty one is not a cell to skip.
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleKeyword(&rules[0], "table-layout", "fixed");
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRow);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[2], "width", 100.0);
    LayoutTableRulePx(&rules[2], "height", 20.0);
    LayoutTableRuleInit(&rules[3], "div", 2, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[3], "width", 100.0);
    LayoutTableRulePx(&rules[3], "height", 20.0);
    LayoutTableRuleInit(&rules[4], "div", 3, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[4], "width", 100.0);
    LayoutTableRulePx(&rules[4], "height", 20.0);

    LayoutTableBegin(&fixture,
                     "<html><body>"
                     "<div><div></div><div>text</div><div><div></div></div></div>"
                     "</body></html>",
                     rules, 5u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(stats.block.table.cellsPlaced, 3u);
    RoboCheckEqInt(stats.block.table.cellsEmpty, 2u);
    RoboCheckEqInt(LayoutTableIsPlaced(&fixture, LayoutTableBoxNth(&fixture, "div", 3u)),
                   1);

    LayoutTableEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Nesting, spacing and the counters                                   */
/* ------------------------------------------------------------------ */

static void TestTableBorderSpacing(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[7];
    LayoutStats stats;
    size_t index;

    /*
     * `border-spacing: 5px 10px` is CSS's two-length grammar: the first length is the
     * **column** gutter and the second the row one. A separate table's gutters are
     * real geometry -- the second column begins 5px past the first column's end and
     * the second row 10px below the first row's bottom edge -- and they are taken out
     * of no column: the spacing sits *between* the columns.
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleKeyword(&rules[0], "table-layout", "fixed");
    LayoutTableRuleText(&rules[0], "border-spacing: 5px 10px");
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRow);
    for (index = 2u; index < 4u; index++) {
        LayoutTableRuleInit(&rules[index], "div", (int)(index - 1u),
                            RoboCssDisplayTableCell);
        LayoutTableRulePx(&rules[index], "width", 100.0);
        LayoutTableRulePx(&rules[index], "height", 20.0);
    }
    LayoutTableRuleInit(&rules[4], "div", 3, RoboCssDisplayTableRow);
    for (index = 5u; index < 7u; index++) {
        LayoutTableRuleInit(&rules[index], "div", (int)(index - 2u),
                            RoboCssDisplayTableCell);
        LayoutTableRulePx(&rules[index], "width", 100.0);
        LayoutTableRulePx(&rules[index], "height", 20.0);
    }

    LayoutTableBegin(&fixture, LAYOUT_TABLE_TWO_CELLS_HTML, rules, 7u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(LayoutTableXOf(&fixture, 1u), 0);
    RoboCheckEqInt(LayoutTableXOf(&fixture, 2u), 105);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 3u), 30);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 4u), 30);
    RoboCheckEqInt(LayoutTableBodySize(&fixture).height, 50);
    RoboCheckEqInt(stats.block.table.spacedTables, 1u);
    RoboCheckEqInt(stats.block.table.collapsedTables, 0u);

    LayoutTableEnd(&fixture);
}

static void TestTableBorderCollapse(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[7];
    LayoutStats stats;
    size_t index;

    /*
     * `border-collapse: collapse` is read, counted and made to **zero the gutters**:
     * CSS says `border-spacing` does not apply to a collapsing table, so the same
     * declaration that produced 5px/10px of space above produces none here. The
     * border arithmetic of a collapsing table -- one border where two adjoin, and the
     * rules that pick which -- is a later row's and is named as absent rather than
     * half-done: each cell's border stays its own.
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleKeyword(&rules[0], "table-layout", "fixed");
    LayoutTableRuleKeyword(&rules[0], "border-collapse", "collapse");
    LayoutTableRuleText(&rules[0], "border-spacing: 5px 10px");
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRow);
    for (index = 2u; index < 4u; index++) {
        LayoutTableRuleInit(&rules[index], "div", (int)(index - 1u),
                            RoboCssDisplayTableCell);
        LayoutTableRulePx(&rules[index], "width", 100.0);
        LayoutTableRulePx(&rules[index], "height", 20.0);
    }
    LayoutTableRuleInit(&rules[4], "div", 3, RoboCssDisplayTableRow);
    for (index = 5u; index < 7u; index++) {
        LayoutTableRuleInit(&rules[index], "div", (int)(index - 2u),
                            RoboCssDisplayTableCell);
        LayoutTableRulePx(&rules[index], "width", 100.0);
        LayoutTableRulePx(&rules[index], "height", 20.0);
    }

    LayoutTableBegin(&fixture, LAYOUT_TABLE_TWO_CELLS_HTML, rules, 7u);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(LayoutTableXOf(&fixture, 2u), 100);
    RoboCheckEqInt(LayoutTableYOf(&fixture, 3u), 20);
    RoboCheckEqInt(LayoutTableBodySize(&fixture).height, 40);
    RoboCheckEqInt(stats.block.table.collapsedTables, 1u);
    RoboCheckEqInt(stats.block.table.spacedTables, 0u);

    LayoutTableEnd(&fixture);
}

static void TestTableNested(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[6];
    LayoutStats stats;

    /*
     * A table inside a cell: the outer pass hands the cell an area, the cell's own
     * contents are laid out in the block context it establishes, and the box that is
     * itself a table establishes a second table context inside it -- so the inner
     * cells' coordinates are the inner table's, not the outer one's (the container's
     * content origin is what a child's coordinate is relative to, for every
     * direction).
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleKeyword(&rules[0], "table-layout", "fixed");
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRow);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[2], "width", 100.0);
    LayoutTableRuleInit(&rules[3], "div", 2, RoboCssDisplayTable);
    LayoutTableRuleInit(&rules[4], "div", 3, RoboCssDisplayTableRow);
    LayoutTableRuleInit(&rules[5], "div", 4, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[5], "height", 20.0);

    LayoutTableBegin(&fixture,
                     "<html><body>"
                     "<div><div><div><div><div>ab</div></div></div></div></div>"
                     "</body></html>",
                     rules, 6u);
    LayoutTableScriptUse(&fixture, 10, 16);
    LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(stats.block.table.containers, 2u);
    RoboCheckEqInt(stats.block.table.rows, 2u);
    RoboCheckEqInt(stats.block.table.cellsPlaced, 2u);
    /* The outer cell is 100 wide, and the inner table fills it: its single column
     * takes the cell's content floor (a 20px widest word) and then every pixel the
     * inner table's width has left -- 100. */
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 2u), 100);
    RoboCheckEqInt(LayoutTableXOf(&fixture, 4u), 0);
    RoboCheckEqInt(LayoutTableWidthOf(&fixture, 4u), 100);
    RoboCheckEqInt(LayoutTableHeightOf(&fixture, 4u), 20);

    LayoutTableEnd(&fixture);
}

static void TestTableCountersBalance(void)
{
    LayoutTableFixture fixture;
    LayoutTableRule rules[4];

    /*
     * The accounting, stated as identities rather than as numbers where the identity is
     * the fact: every column was sized by exactly one of the two rules, every box the
     * pass placed is one of its four kinds, and every object in the tree either has
     * geometry or is one of the directions' stated refusals.
     */
    LayoutTableBody(&rules[0], 300.0);
    LayoutTableRuleKeyword(&rules[0], "table-layout", "fixed");
    LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRow);
    LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[2], "width", 100.0);
    LayoutTableRulePx(&rules[2], "height", 20.0);
    LayoutTableRuleInit(&rules[3], "div", 2, RoboCssDisplayTableCell);
    LayoutTableRulePx(&rules[3], "width", 100.0);
    LayoutTableRulePx(&rules[3], "height", 20.0);

    LayoutTableBegin(&fixture,
                     "<html><body>"
                     "<div><div></div><div></div></div>"
                     "</body></html>",
                     rules, 4u);

    {
        LayoutStats stats;
        size_t objects = LayoutTreeObjectCount(fixture.tree);

        LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, &stats);

        RoboCheckEqInt(stats.block.table.containers, 1u);
        RoboCheckEqInt(stats.block.table.rows, 1u);
        RoboCheckEqInt(stats.block.table.columns, 2u);
        RoboCheckEqInt(stats.block.table.cellsPlaced, 2u);
        RoboCheckEqInt(stats.block.table.contentColumns +
                           stats.block.table.fixedColumns,
                       stats.block.table.columns);
        RoboCheckEqInt(stats.block.table.cellsPlaced,
                       stats.block.table.rows * 2u);
        /* The two boxes above the container (`html` and `body`), then one box per
         * placed table box: the rows, the groups, the cells and the captions. */
        RoboCheckEqInt(stats.block.blocksPlaced,
                       2u + stats.block.table.rows + stats.block.table.groups +
                           stats.block.table.cellsPlaced +
                           stats.block.table.captions);
        /* `html`, the parser's implicit `<head>`, `body` and the table's three boxes:
         * a row and two cells. */
        RoboCheckEqInt(objects, 6u);
        RoboCheckEqInt(stats.block.inlineLevelSkipped, 1u);
        RoboCheckEqInt(stats.block.blocksPlaced + stats.block.inlineLevelSkipped +
                           stats.inlineFlow.participantsPlaced +
                           stats.block.unreached,
                       objects);
        RoboCheckEqInt(stats.block.unreached, 0u);
    }

    LayoutTableEnd(&fixture);
}

static void TestTableAllocatorBalance(void)
{
    size_t before;
    size_t index;

    LayoutFixture();
    before = LayoutLiveBlocks();

    for (index = 0u; index < 8u; index++) {
        LayoutTableFixture fixture;
        LayoutTableRule rules[4];

        LayoutTableBody(&rules[0], 300.0);
        LayoutTableRuleKeyword(&rules[0], "table-layout", "fixed");
        LayoutTableRuleInit(&rules[1], "div", 0, RoboCssDisplayTableRow);
        LayoutTableRuleInit(&rules[2], "div", 1, RoboCssDisplayTableCell);
        LayoutTableRulePx(&rules[2], "width", 100.0);
        LayoutTableRuleInit(&rules[3], "div", 2, RoboCssDisplayTableCell);
        LayoutTableRulePx(&rules[3], "width", 100.0);

        LayoutTableBegin(&fixture,
                         "<html><body>"
                         "<div><div></div><div></div></div>"
                         "</body></html>",
                         rules, 4u);
        LayoutTablePass(&fixture, LAYOUT_TABLE_VIEWPORT_WIDTH, NULL);
        LayoutTableEnd(&fixture);
        RoboCheckEqInt(LayoutLiveBlocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutTable(void);

void TestRegisterLayoutTable(void)
{
    TestRegister("layout.table.capability", TestTableCapability);
    TestRegister("layout.table.formatting_context", TestTableFormattingContext);
    TestRegister("layout.table.rows_and_cells", TestTableRowsAndCells);
    TestRegister("layout.table.auto_columns", TestTableAutoColumns);
    TestRegister("layout.table.column_box", TestTableColumnBox);
    TestRegister("layout.table.overflowing_cell", TestTableOverflowingCell);
    TestRegister("layout.table.row_height", TestTableRowHeight);
    TestRegister("layout.table.row_group", TestTableRowGroup);
    TestRegister("layout.table.caption_top", TestTableCaptionTop);
    TestRegister("layout.table.caption_bottom", TestTableCaptionBottom);
    TestRegister("layout.table.anonymous_row", TestTableAnonymousRow);
    TestRegister("layout.table.internal_outside_table", TestTableInternalOutsideTable);
    TestRegister("layout.table.empty_cells", TestTableEmptyCells);
    TestRegister("layout.table.border_spacing", TestTableBorderSpacing);
    TestRegister("layout.table.border_collapse", TestTableBorderCollapse);
    TestRegister("layout.table.nested", TestTableNested);
    TestRegister("layout.table.counters_balance", TestTableCountersBalance);
    TestRegister("layout.table.allocator_balance", TestTableAllocatorBalance);
}
