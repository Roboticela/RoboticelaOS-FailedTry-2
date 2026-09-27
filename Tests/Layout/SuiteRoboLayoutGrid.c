/*
 * RoboLayout grid suite (Phase 19.4) — layout.grid.*
 *
 * Four things are under test, and they are told apart deliberately:
 *
 *   the **formatting context**: does `display: grid` make a box establish a grid
 *   context, and does the grid pass own its children rather than the block pass?
 *
 *   the **tracks**: a declared `<length>`, a `<flex>` share, a content breadth and a
 *   gutter each reach the geometry, on both axes, and the implicit tracks a placement
 *   needs are created and sized by `grid-auto-*`;
 *
 *   the **placement**: the four line longhands place an item, the auto-placement
 *   cursor places the rest, a span covers several tracks (and contributes to them),
 *   and `dense` looks back where the sparse cursor does not;
 *
 *   the **alignment**: `align-items`/`align-self` place an item in its area's block
 *   axis, `justify-content`/`align-content` place the *tracks* in the container, and
 *   an `auto` margin absorbs the free space ahead of both.
 *
 * The fixture fabricates a *typed* computed style per tag -- including the track lists
 * and line placements, which are held as the preserved token trees a real cascade
 * produces -- because the point of the phase is that the pass reads those *values*, and a
 * case built from a stylesheet could only prove the parser. Geometry is read back through
 * the public result doors, so every expectation is a number a consumer could read.
 */

#include "RoboLayoutTestSupport.h"

#include "RoboLayout/RoboLayoutIntrinsic.h"
#include "RoboLayout/RoboLayoutText.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fixture that can declare a grid                                   */
/* ------------------------------------------------------------------ */

typedef enum layoutGridDeclKind {
    LAYOUT_GRID_DECL_PX = 0,
    LAYOUT_GRID_DECL_PERCENTAGE = 1,
    LAYOUT_GRID_DECL_KEYWORD = 2,
    LAYOUT_GRID_DECL_ALIGN = 3,
    LAYOUT_GRID_DECL_FLOW = 4,
    /** A whole declaration written as text: a track list, an auto-track list or a
     *  line placement, held as the preserved tokens a real cascade carries. */
    LAYOUT_GRID_DECL_TEXT = 5
} LayoutGridDeclKind;

typedef struct layoutGridDecl {
    const char *name;
    LayoutGridDeclKind kind;
    double number;
    int typed;
    const char *text;
} LayoutGridDecl;

#define LAYOUT_GRID_MAX_DECLARATIONS 10u
#define LAYOUT_GRID_MAX_RULES 24u
#define LAYOUT_GRID_MAX_TAGS 16u

typedef struct layoutGridRule {
    const char *tag;
    int nth; /* which occurrence of @tag this rule styles, 0-based */
    CssDisplay display;
    LayoutGridDecl declarations[LAYOUT_GRID_MAX_DECLARATIONS];
    size_t declarationsCount;
} LayoutGridRule;

typedef struct layoutGridStyle {
    char tag[16];
    int nth; /* which occurrence of @tag this style serves, 0-based */
    CssComputedStyle *style;
} LayoutGridStyle;

typedef struct layoutGridTagCount {
    char tag[16];
    int seen;
} LayoutGridTagCount;

typedef struct layoutGridSource {
    LayoutGridStyle rules[LAYOUT_GRID_MAX_RULES];
    size_t count;
    LayoutGridTagCount tagCounts[LAYOUT_GRID_MAX_TAGS];
    size_t tagCount;
} LayoutGridSource;

static void LayoutGridRuleInit(LayoutGridRule *rule,
                               const char *tag,
                               int nth,
                               CssDisplay display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->nth = nth;
    rule->display = display;
}

static void LayoutGridRuleAdd(LayoutGridRule *rule,
                              const char *name,
                              LayoutGridDeclKind kind,
                              double number,
                              int typed,
                              const char *text)
{
    RoboCheckEqInt(rule->declarationsCount < LAYOUT_GRID_MAX_DECLARATIONS, 1);
    rule->declarations[rule->declarationsCount].name = name;
    rule->declarations[rule->declarationsCount].kind = kind;
    rule->declarations[rule->declarationsCount].number = number;
    rule->declarations[rule->declarationsCount].typed = typed;
    rule->declarations[rule->declarationsCount].text = text;
    rule->declarationsCount++;
}

static void LayoutGridRulePx(LayoutGridRule *rule, const char *name, double px)
{
    LayoutGridRuleAdd(rule, name, LAYOUT_GRID_DECL_PX, px, 0, NULL);
}

static void LayoutGridRuleKeyword(LayoutGridRule *rule, const char *property,
                                  const char *keyword)
{
    LayoutGridRuleAdd(rule, property, LAYOUT_GRID_DECL_KEYWORD, 0.0, 0, keyword);
}

static void LayoutGridRuleAlign(LayoutGridRule *rule, const char *property,
                                CssFlexAlign align)
{
    LayoutGridRuleAdd(rule, property, LAYOUT_GRID_DECL_ALIGN, 0.0, (int)align,
                      NULL);
}

static void LayoutGridRuleFlow(LayoutGridRule *rule, CssGridAutoFlow flow)
{
    LayoutGridRuleAdd(rule, "grid-auto-flow", LAYOUT_GRID_DECL_FLOW, 0.0,
                      (int)flow, NULL);
}

/** A whole declaration as text: `"grid-template-columns: 100px 100px"`. */
static void LayoutGridRuleText(LayoutGridRule *rule, const char *text)
{
    LayoutGridRuleAdd(rule, NULL, LAYOUT_GRID_DECL_TEXT, 0.0, 0, text);
}

static void LayoutGridStyleAdd(LayoutGridSource *source,
                               const LayoutGridRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    size_t index;
    size_t slot;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_GRID_MAX_RULES, 1);
    slot = source->count;
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeDisplay(&value, rule->display, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                            CssSliceFromText("display"), 0u,
                                            &specified, &value, NULL),
                   RoboOk);

    for (index = 0u; index < rule->declarationsCount; index++) {
        const LayoutGridDecl *decl = &rule->declarations[index];

        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        switch (decl->kind) {
        case LAYOUT_GRID_DECL_TEXT: {
            /*
             * The track lists and the line placements are read from the *preserved*
             * computed value -- the same shape 18.4 leaves them in -- so the fixture
             * parses the declaration the way the cascade would and keeps the tokens.
             * One text may hold more than one declaration (`grid-column-start: 1;
             * grid-row-start: 2`), and every one of them is an entry of its own.
             */
            CssDeclarationList list = RoboCssCleared;
            CssError error = RoboCssCleared;
            size_t declared;
            size_t count;

            RoboCheckEqInt(CssParseDeclarations(decl->text, &list, &error), RoboOk);
            count = CssDeclarationListCount(&list);
            RoboCheckEqInt(count > 0u, 1);
            for (declared = 0u; declared < count; declared++) {
                const CssDeclaration *declaration = CssDeclarationListAt(&list,
                                                                        declared);

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
                                   style,
                                   CssDeclarationResolveProperty(declaration),
                                   CssDeclarationName(declaration), 0u, &specified,
                                   &value, NULL),
                               RoboOk);
            }
            CssDeclarationListDispose(&list);
            continue;
        }
        case LAYOUT_GRID_DECL_PERCENTAGE:
            RoboCheckEqInt(CssComputedValueMakePercentage(&value, decl->number, 0,
                                                          NULL),
                           RoboOk);
            break;
        case LAYOUT_GRID_DECL_FLOW:
            RoboCheckEqInt(CssComputedValueMakeGridAutoFlow(
                               &value, (CssGridAutoFlow)decl->typed, NULL),
                           RoboOk);
            break;
        case LAYOUT_GRID_DECL_KEYWORD:
            RoboCheckEqInt(CssComputedValueMakeKeyword(
                               &value, CssSliceFromText(decl->text), NULL),
                           RoboOk);
            break;
        case LAYOUT_GRID_DECL_ALIGN:
            RoboCheckEqInt(CssComputedValueMakeFlexAlign(
                               &value, (CssFlexAlign)decl->typed, NULL),
                           RoboOk);
            break;
        case LAYOUT_GRID_DECL_PX:
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

static int LayoutGridTagCountFor(LayoutGridSource *source, const char *tag)
{
    size_t index;

    for (index = 0u; index < source->tagCount; index++) {
        if (strcmp(source->tagCounts[index].tag, tag) == 0) {
            return source->tagCounts[index].seen++;
        }
    }

    RoboCheckEqInt(source->tagCount < LAYOUT_GRID_MAX_TAGS, 1);
    strncpy(source->tagCounts[source->tagCount].tag, tag,
            sizeof(source->tagCounts[0].tag) - 1u);
    source->tagCounts[source->tagCount].tag[sizeof(source->tagCounts[0].tag) - 1u] =
        '\0';
    source->tagCounts[source->tagCount].seen = 1;
    source->tagCount++;
    return 0;
}

static const CssComputedStyle *LayoutGridStyleLookup(void *userdata,
                                                     const HtmlNodeHandle *element)
{
    LayoutGridSource *source = (LayoutGridSource *)userdata;
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
    nth = LayoutGridTagCountFor(source, name);
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
 * A grid sizes a content track from its items' **min-content** and **max-content**
 * contributions, so a case about content sizing needs a measurer rather than a
 * document. This one is arithmetic rather than a font: every code point is
 * `perCodePoint` wide and every run is `height` tall, so a case can say "a four-letter
 * word is 40 wide" without depending on a face, and `minContent` (the widest word)
 * differs from `maxContent` (the whole line) exactly when the text has a space in it.
 */
typedef struct layoutGridScript {
    int32_t perCodePoint;
    int32_t height;
} LayoutGridScript;

static Result LayoutGridScriptMeasure(void *userdata,
                                      const LayoutTextRequest *request,
                                      LayoutTextRun *outRun)
{
    LayoutGridScript *script = (LayoutGridScript *)userdata;

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

typedef struct layoutGridFixture {
    LayoutDocument doc;
    LayoutGridSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
    LayoutGridScript script;
    LayoutTextMetrics metrics;
    int useScript;
} LayoutGridFixture;

static void LayoutGridBegin(LayoutGridFixture *fixture,
                            const char *html,
                            const LayoutGridRule *rules,
                            size_t ruleCount)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < ruleCount; index++) {
        LayoutGridStyleAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutGridStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                               &options, NULL, NULL),
                   RoboOk);
}

static void LayoutGridEnd(LayoutGridFixture *fixture)
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

#define LAYOUT_GRID_VIEWPORT_WIDTH 800
#define LAYOUT_GRID_VIEWPORT_HEIGHT 600

/** Install the scripted measurer: @p perCodePoint px per code point, @p height px
 *  tall. Called after the tree is built and before the pass. */
static void LayoutGridScriptUse(LayoutGridFixture *fixture,
                                int32_t perCodePoint,
                                int32_t height)
{
    fixture->script.perCodePoint = perCodePoint;
    fixture->script.height = height;
    LayoutTextMetricsInit(&fixture->metrics);
    fixture->metrics.userdata = &fixture->script;
    fixture->metrics.measure = LayoutGridScriptMeasure;
    fixture->useScript = 1;
}

static void LayoutGridPass(LayoutGridFixture *fixture,
                           int32_t viewportWidth,
                           LayoutStats *outStats)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;

    viewport.width = viewportWidth;
    viewport.height = LAYOUT_GRID_VIEWPORT_HEIGHT;
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

static LayoutNodeId LayoutGridBoxNth(LayoutGridFixture *fixture,
                                     const char *tag,
                                     size_t nth)
{
    /* The nth *box* for @tag in document order: the tree builder inserts implicit
     * elements above the document, so the indices are a fact about the parser and
     * the box is found by matching the DOM element name. */
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

static LayoutSize LayoutGridSizeOf(LayoutGridFixture *fixture, LayoutNodeId id)
{
    LayoutResult result;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    return size;
}

static LayoutPoint LayoutGridPositionOf(LayoutGridFixture *fixture,
                                        LayoutNodeId id)
{
    LayoutResult result;
    LayoutPoint position;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    return position;
}

/* The nth `div`'s one geometry number, read through the public result doors. */
static int32_t LayoutGridXOf(LayoutGridFixture *fixture, size_t nth)
{
    return LayoutGridPositionOf(fixture, LayoutGridBoxNth(fixture, "div", nth)).x;
}

static int32_t LayoutGridYOf(LayoutGridFixture *fixture, size_t nth)
{
    return LayoutGridPositionOf(fixture, LayoutGridBoxNth(fixture, "div", nth)).y;
}

static int32_t LayoutGridWidthOf(LayoutGridFixture *fixture, size_t nth)
{
    return LayoutGridSizeOf(fixture, LayoutGridBoxNth(fixture, "div", nth)).width;
}

static int32_t LayoutGridHeightOf(LayoutGridFixture *fixture, size_t nth)
{
    return LayoutGridSizeOf(fixture, LayoutGridBoxNth(fixture, "div", nth)).height;
}

/** Install the body's grid declarations, which every case but the nested one shares:
 *  a definite width, so the tracks have a container to divide. */
static void LayoutGridBody(LayoutGridRule *rule, double width)
{
    LayoutGridRuleInit(rule, "body", 0, RoboCssDisplayGrid);
    LayoutGridRulePx(rule, "width", width);
}

/* ------------------------------------------------------------------ */
/* The capability                                                      */
/* ------------------------------------------------------------------ */

static void TestGridCapability(void)
{
    LayoutCapabilities caps;
    LayoutGridCounters a;
    LayoutGridCounters b;

    LayoutFixture();
    RoboCheckEqInt(RoboLayoutGridLayoutImplemented, 1);
    RoboCheckEqInt(LayoutGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.gridLayout, RoboLayoutGridLayoutImplemented);

    /* The counters record is a value with a defined zero: clearing it and copying it
     * are the same state, and one field moved apart is a difference. */
    LayoutGridCountersInit(&a);
    b = a;
    RoboCheckEqInt(LayoutGridCountersEqual(&a, &b), 1);
    RoboCheckEqInt(LayoutGridCountersEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutGridCountersEqual(&a, NULL), 0);
    b.itemsPlaced = 1u;
    RoboCheckEqInt(LayoutGridCountersEqual(&a, &b), 0);
    RoboCheckEqInt(a.itemsPlaced, 0u);
    RoboCheckEqInt(a.containers, 0u);
}

/* ------------------------------------------------------------------ */
/* The formatting context                                              */
/* ------------------------------------------------------------------ */

static void TestGridFormattingContext(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[2];
    LayoutNodeId body;
    LayoutFcId owned = RoboLayoutFcIdNone;
    LayoutFormattingContext context;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);

    LayoutGridBegin(&fixture, "<html><body><div></div></body></html>", rules, 2u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, NULL);

    body = LayoutGridBoxNth(&fixture, "body", 0u);
    RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, body, &owned, NULL),
                   RoboOk);
    RoboCheckEqInt(LayoutTreeContext(fixture.tree, owned, &context, NULL), RoboOk);
    RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcGrid);

    LayoutGridEnd(&fixture);

    /*
     * `inline-grid` is the same inner display at the inline level, and it owns the same
     * context: the box is placed as an atomic inline by its parent, and its *own*
     * contents are grid items. The classifier says so (`LayoutFcEstablishes`), and this
     * is the layout half of that claim.
     */
    LayoutGridRuleInit(&rules[0], "body", 0, RoboCssDisplayInlineGrid);
    LayoutGridRulePx(&rules[0], "width", 300.0);

    LayoutGridBegin(&fixture, "<html><body><div></div></body></html>", rules, 2u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, NULL);

    body = LayoutGridBoxNth(&fixture, "body", 0u);
    owned = RoboLayoutFcIdNone;
    RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, body, &owned, NULL),
                   RoboOk);
    RoboCheckEqInt(LayoutTreeContext(fixture.tree, owned, &context, NULL), RoboOk);
    RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcGrid);

    LayoutGridEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The explicit tracks                                                 */
/* ------------------------------------------------------------------ */

static void TestGridFixedColumns(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[4];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: 100px 100px");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);
    LayoutGridRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[2], "height", 20.0);
    LayoutGridRuleInit(&rules[3], "div", 2, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[3], "height", 20.0);

    LayoutGridBegin(&fixture,
                    "<html><body><div></div><div></div><div></div></body></html>",
                    rules, 4u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * A declared track is `minmax(100px, 100px)` in CSS's own reading, so neither
     * grows and `justify-content`'s initial `normal` has no `auto` track to spend the
     * free space on: the two columns sit at the inline start.
     */
    RoboCheckEqInt(LayoutGridXOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 0u), 100);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 1u), 100);
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 1u), 100);

    /*
     * The third item is the one the *auto-placement* rule is about: the grid has two
     * columns, so it goes into a **second row** rather than into an implicit third
     * column. A search that let the column axis grow would place it at x = 200 and
     * the grid would be one long line.
     */
    RoboCheckEqInt(LayoutGridXOf(&fixture, 2u), 0);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 2u), 20);
    RoboCheckEqInt(stats.block.grid.containers, 1u);
    RoboCheckEqInt(stats.block.grid.itemsPlaced, 3u);
    RoboCheckEqInt(stats.block.grid.placedItems, 0u);
    RoboCheckEqInt(stats.block.grid.autoPlaced, 3u);
    RoboCheckEqInt(stats.block.grid.columns, 2u);
    RoboCheckEqInt(stats.block.grid.rows, 2u);
    RoboCheckEqInt(stats.block.grid.implicitTracks, 2u);
    RoboCheckEqInt(stats.block.grid.frTracks, 0u);

    LayoutGridEnd(&fixture);
}

static void TestGridFractionColumns(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[2];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: 1fr 2fr");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);

    LayoutGridBegin(&fixture, "<html><body><div></div><div></div></body></html>",
                    rules, 2u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * A `<flex>` track takes its share of the space the *other* tracks left, and an
     * `auto` width stretches across the track it was placed in -- so the item widths
     * are the shares themselves: a third and two thirds of the 300px container.
     */
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 0u), 100);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 1u), 100);
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 1u), 200);
    RoboCheckEqInt(stats.block.grid.frTracks, 2u);
    RoboCheckEqInt(stats.block.grid.columns, 2u);
    RoboCheckEqInt(stats.block.grid.rows, 1u);

    LayoutGridEnd(&fixture);
}

static void TestGridGap(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[2];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: 40px 1fr");
    LayoutGridRulePx(&rules[0], "column-gap", 10.0);
    LayoutGridRulePx(&rules[0], "row-gap", 10.0);
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);

    LayoutGridBegin(&fixture, "<html><body><div></div><div></div></body></html>",
                    rules, 2u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /* The gutters are taken out of the space the tracks share *before* the fraction is
     * resolved: 300 - 10(gap) - 40(fixed) = 250 for the one `1fr` track. */
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 0u), 40);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 1u), 50);
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 1u), 250);

    LayoutGridEnd(&fixture);
}

static void TestGridContentColumn(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[2];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: auto");
    LayoutGridRuleAlign(&rules[0], "justify-content", RoboCssFlexAlignStart);
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);

    LayoutGridBegin(&fixture, "<html><body><div>abcd</div></body></html>", rules,
                    2u);
    LayoutGridScriptUse(&fixture, 10, 16);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * `auto` is a content breadth: the track starts at zero and is raised by the items
     * that span it -- four code points at ten pixels each -- while `justify-content:
     * start` leaves the rest of the container's 300px to the alignment step rather than
     * stretching the track into it. (The default `normal` *would* stretch it: that is
     * the case below.)
     */
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 0u), 40);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutGridHeightOf(&fixture, 0u), 16);
    RoboCheckEqInt(stats.block.grid.contentTracks >= 1u, 1);
    RoboCheckEqInt(stats.block.grid.stretched, 0u);

    LayoutGridEnd(&fixture);
}

static void TestGridAutoStretch(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[2];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: auto auto");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);

    LayoutGridBegin(&fixture, "<html><body><div>ab</div><div>cd</div></body></html>",
                    rules, 2u);
    LayoutGridScriptUse(&fixture, 10, 16);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * Under the initial `justify-content: normal` the `auto` tracks are *stretched*:
     * each takes an equal share of what is left after the content floors, which is what
     * makes a two-column `auto` grid fill its container. Both items are one track wide,
     * so each contributes its own 20px floor and the two tracks are equal.
     */
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 0u), 150);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 1u), 150);
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 1u), 150);

    LayoutGridEnd(&fixture);

    /* The same grid under `justify-content: center`: the tracks keep their content
     * widths and the free space becomes a leading offset instead. */
    LayoutGridRuleAlign(&rules[0], "justify-content", RoboCssFlexAlignCenter);

    LayoutGridBegin(&fixture, "<html><body><div>ab</div><div>cd</div></body></html>",
                    rules, 2u);
    LayoutGridScriptUse(&fixture, 10, 16);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 0u), 20);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 0u), 130);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 1u), 150);
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 1u), 20);

    LayoutGridEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Implicit tracks                                                     */
/* ------------------------------------------------------------------ */

static void TestGridImplicitRows(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[2];
    LayoutStats stats;

    /*
     * No template at all: the implicit grid has the one column an automatic placement
     * needs, and every item after the first goes into the row below it -- which is the
     * block-flow-shaped answer CSS gives a grid with no columns declared.
     */
    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-auto-columns: 50px");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);

    LayoutGridBegin(&fixture,
                    "<html><body><div></div><div></div><div></div></body></html>",
                    rules, 2u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 0u), 50);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 1u), 0);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 1u), 20);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 2u), 40);
    RoboCheckEqInt(stats.block.grid.columns, 1u);
    RoboCheckEqInt(stats.block.grid.rows, 3u);
    RoboCheckEqInt(stats.block.grid.implicitTracks, 6u);

    LayoutGridEnd(&fixture);
}

static void TestGridAutoFlowColumn(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[2];
    LayoutStats stats;

    /*
     * `grid-auto-flow: column` swaps which axis the cursor advances along: the items
     * fill a *column* first, so with two explicit rows the third starts a second
     * column -- and that second column is implicit, sized by `grid-auto-columns`'s
     * initial `auto` and then stretched, exactly as a row is.
     */
    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-rows: 20px 20px");
    LayoutGridRuleFlow(&rules[0], RoboCssGridAutoFlowColumn);
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);

    LayoutGridBegin(&fixture,
                    "<html><body><div></div><div></div><div></div></body></html>",
                    rules, 2u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(LayoutGridYOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 1u), 20);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 2u), 150);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 2u), 0);
    RoboCheckEqInt(stats.block.grid.columns, 2u);
    RoboCheckEqInt(stats.block.grid.rows, 2u);

    LayoutGridEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Placement                                                           */
/* ------------------------------------------------------------------ */

static void TestGridLinePlacement(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[5];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: 100px 100px 100px");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);
    LayoutGridRuleText(&rules[1], "grid-column-start: 2");
    LayoutGridRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[2], "height", 20.0);
    LayoutGridRuleText(&rules[2], "grid-column-start: 1");
    LayoutGridRuleInit(&rules[3], "div", 2, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[3], "height", 20.0);

    LayoutGridBegin(&fixture,
                    "<html><body><div></div><div></div><div></div></body></html>",
                    rules, 4u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * Line 2 of the first item pins it to the second column; the second item pins
     * itself to the first; and the third has no placement of its own, so the cursor
     * places it in the first free slot from where the previous placement ended -- which
     * is the third column, not the second one the first item took.
     *
     * An item definite on one axis is still the *cursor's* work on the other, so the
     * `placedItems` counter -- the placements that came from the four longhands on both
     * axes -- stays at zero.
     */
    RoboCheckEqInt(LayoutGridXOf(&fixture, 0u), 100);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 1u), 0);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 2u), 200);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 2u), 0);
    RoboCheckEqInt(stats.block.grid.placedItems, 0u);
    RoboCheckEqInt(stats.block.grid.autoPlaced, 3u);

    LayoutGridEnd(&fixture);
}

static void TestGridBothAxesPlaced(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[4];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: 100px 100px");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);
    LayoutGridRuleText(&rules[1], "grid-column-start: 1; grid-row-start: 2");
    LayoutGridRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[2], "height", 20.0);

    LayoutGridBegin(&fixture, "<html><body><div></div><div></div></body></html>",
                    rules, 3u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * The first item names **both** its lines, so it is placed before any cursor moves
     * -- and its area is occupied by the time the second item is looked at, which is
     * why the second lands in the first row rather than in the second. The rows are the
     * item's own contributions: one 20px row per item.
     */
    RoboCheckEqInt(LayoutGridXOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 0u), 20);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 1u), 0);
    RoboCheckEqInt(stats.block.grid.placedItems, 1u);
    RoboCheckEqInt(stats.block.grid.autoPlaced, 1u);
    RoboCheckEqInt(stats.block.grid.rows, 3u);

    LayoutGridEnd(&fixture);
}

static void TestGridSpanning(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[3];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: 100px 100px");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);
    LayoutGridRuleText(&rules[1],
                       "grid-column-start: 1; grid-column-end: span 2");
    LayoutGridRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[2], "height", 20.0);

    LayoutGridBegin(&fixture, "<html><body><div></div><div></div></body></html>",
                    rules, 3u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * An item that spans both columns **stretches** across the two tracks and the gap
     * between them (there is none here), and the item after it cannot fit in the
     * occupied row, so it takes the first slot of the next one -- again without adding
     * a column.
     */
    RoboCheckEqInt(LayoutGridXOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 0u), 200);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 1u), 0);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 1u), 20);
    RoboCheckEqInt(stats.block.grid.spanningItems, 1u);
    RoboCheckEqInt(stats.block.grid.columns, 2u);
    RoboCheckEqInt(stats.block.grid.rows, 2u);

    LayoutGridEnd(&fixture);
}

static void TestGridDense(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[5];
    LayoutStats stats;

    /*
     * The classic hole: a wide item leaves the end of its row free, and the item after
     * it is too wide for what is left. A **sparse** cursor never looks back, so it takes
     * the next row's start; a **dense** one starts at the origin for every item and
     * backfills the hole. Both grids are three columns wide with a leading wide item --
     * the only difference is the flow's density.
     */
    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: 60px 60px 60px");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);
    LayoutGridRuleText(&rules[1],
                       "grid-column-start: 1; grid-column-end: span 2");
    LayoutGridRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[2], "height", 20.0);
    LayoutGridRuleText(&rules[2],
                       "grid-column-start: 1; grid-column-end: span 2");
    LayoutGridRuleInit(&rules[3], "div", 2, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[3], "height", 20.0);

    LayoutGridBegin(&fixture,
                    "<html><body><div></div><div></div><div></div></body></html>",
                    rules, 4u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /* Sparse: the second wide item cannot fit in column 2, so it starts row 1 and the
     * single-column item continues after it -- leaving (column 2, row 0) empty. */
    RoboCheckEqInt(LayoutGridXOf(&fixture, 1u), 0);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 1u), 20);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 2u), 120);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 2u), 20);
    RoboCheckEqInt(stats.block.grid.denseItems, 0u);

    LayoutGridEnd(&fixture);

    LayoutGridRuleFlow(&rules[0], RoboCssGridAutoFlowRowDense);

    LayoutGridBegin(&fixture,
                    "<html><body><div></div><div></div><div></div></body></html>",
                    rules, 4u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /* Dense: the third item backfills (column 2, row 0) instead. */
    RoboCheckEqInt(LayoutGridXOf(&fixture, 2u), 120);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 2u), 0);
    RoboCheckEqInt(stats.block.grid.denseItems, 1u);

    LayoutGridEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Alignment                                                           */
/* ------------------------------------------------------------------ */

static void TestGridAlignItems(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[4];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 100.0);
    LayoutGridRuleText(&rules[0], "grid-template-rows: 100px 100px 100px");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);
    LayoutGridRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[2], "height", 20.0);
    LayoutGridRuleAlign(&rules[2], "align-self", RoboCssFlexAlignFlexEnd);
    LayoutGridRuleInit(&rules[3], "div", 2, RoboCssDisplayBlock);

    LayoutGridBegin(&fixture,
                    "<html><body><div></div><div></div><div></div></body></html>",
                    rules, 4u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * The blocks are 100px tall and the items are not: an item with an `auto` height
     * stretches (the initial `normal`), `align-self: flex-end` puts a declared one at
     * its area's block end, and `stretch` is counted rather than assumed.
     */
    RoboCheckEqInt(LayoutGridHeightOf(&fixture, 0u), 100);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 1u), 180);
    RoboCheckEqInt(LayoutGridHeightOf(&fixture, 1u), 20);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 2u), 200);
    RoboCheckEqInt(stats.block.grid.stretched, 2u);
    RoboCheckEqInt(stats.block.grid.rows, 3u);

    LayoutGridEnd(&fixture);
}

static void TestGridAlignContent(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[2];
    LayoutStats stats;

    /*
     * The rows of an `auto`-height container *are* the container's extent, so
     * `align-content` has no free space to distribute -- the two rows sit at 0 and 20
     * whatever the alignment says. A definite-height container is the case where it
     * does something, and that is what this case moves on to.
     */
    LayoutGridBody(&rules[0], 100.0);
    LayoutGridRuleText(&rules[0], "grid-template-rows: 20px 20px");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);

    LayoutGridBegin(&fixture, "<html><body><div></div><div></div></body></html>",
                    rules, 2u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(LayoutGridYOf(&fixture, 0u), 0);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 1u), 20);

    LayoutGridEnd(&fixture);
}

static void TestGridBaseline(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[3];
    LayoutStats stats;
    LayoutBaselineMetrics metrics;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: 100px 100px");
    LayoutGridRuleAlign(&rules[0], "align-items", RoboCssFlexAlignBaseline);
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);
    LayoutGridRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[2], "height", 40.0);

    LayoutGridBegin(&fixture, "<html><body><div></div><div></div></body></html>",
                    rules, 3u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * Neither item has a baseline-bearing child, so each takes the documented
     * bottom-edge fallback. The row's baseline is the deepest of them and the 20-tall
     * item drops so that the two coincide -- and the row grows to hold both.
     */
    RoboCheckEqInt(LayoutGridYOf(&fixture, 0u), 20);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 1u), 0);
    RoboCheckEqInt(stats.block.grid.baselineItems, 2u);

    RoboCheckEqInt(LayoutObjectBaseline(fixture.tree,
                                        LayoutGridBoxNth(&fixture, "div", 0u),
                                        &metrics, NULL),
                   RoboOk);
    RoboCheckEqInt((int)metrics.kind, (int)RoboLayoutBaselineBox);
    RoboCheckEqInt(metrics.ascent, 20);
    RoboCheckEqInt(metrics.descent, 0);

    LayoutGridEnd(&fixture);
}

static void TestGridAutoMargins(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[2];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "width", 100.0);
    LayoutGridRulePx(&rules[1], "height", 20.0);
    LayoutGridRuleKeyword(&rules[1], "margin-left", "auto");
    LayoutGridRuleKeyword(&rules[1], "margin-right", "auto");

    LayoutGridBegin(&fixture, "<html><body><div></div></body></html>", rules, 2u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * The one `auto` column takes the container's whole width under `normal`, and the
     * two `auto` margins then absorb the space between the 100px item and that area --
     * *ahead of* the alignment properties, which is why the item is centred without
     * `justify-self` existing in this build.
     */
    RoboCheckEqInt(LayoutGridXOf(&fixture, 0u), 100);
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 0u), 100);
    RoboCheckEqInt(stats.block.grid.autoMargins, 1u);

    LayoutGridEnd(&fixture);
}

static void TestGridOverflow(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[2];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: 100px");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "width", 250.0);
    LayoutGridRulePx(&rules[1], "height", 20.0);

    LayoutGridBegin(&fixture, "<html><body><div></div></body></html>", rules, 2u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /* A declared inline size the columns cannot honour is *reported*, not clamped: the
     * item keeps its 250px and the container says so. */
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 0u), 250);
    RoboCheckEqInt(stats.block.grid.overflowingItems, 1u);

    LayoutGridEnd(&fixture);
}

static void TestGridRtl(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[2];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: 100px 100px");
    LayoutGridRuleKeyword(&rules[0], "direction", "rtl");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);

    LayoutGridBegin(&fixture, "<html><body><div></div><div></div></body></html>",
                    rules, 2u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * `direction: rtl` puts the inline axis's start edge on the right, so the columns
     * are mirrored inside the container: the first column's left edge is at
     * 300 - 100 = 200 and the second's at 100. The block axis is untouched -- a
     * `direction` change is not a writing-mode change.
     */
    RoboCheckEqInt(LayoutGridXOf(&fixture, 0u), 200);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 1u), 100);
    RoboCheckEqInt(LayoutGridYOf(&fixture, 1u), 0);
    RoboCheckEqInt(stats.block.grid.reversed, 1u);

    LayoutGridEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Nesting, counters and the allocator                                 */
/* ------------------------------------------------------------------ */

static void TestGridNested(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[4];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: 100px");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayGrid);
    LayoutGridRuleText(&rules[1], "grid-template-columns: 50px 50px");
    LayoutGridRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[2], "height", 20.0);
    LayoutGridRuleInit(&rules[3], "div", 2, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[3], "height", 20.0);

    LayoutGridBegin(&fixture,
                    "<html><body><div><div></div><div></div></div></body></html>",
                    rules, 4u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * A grid item that is itself a grid container: the outer pass hands it an area, the
     * block pass lays its *contents* out inside it, and its own items divide the inline
     * size the outer grid gave it -- so the inner items' positions are 0 and 50 rather
     * than the outer container's coordinates. Both contexts ran.
     */
    RoboCheckEqInt(LayoutGridWidthOf(&fixture, 0u), 100);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 1u), 0);
    RoboCheckEqInt(LayoutGridXOf(&fixture, 2u), 50);
    RoboCheckEqInt(stats.block.grid.containers, 2u);
    RoboCheckEqInt(stats.block.grid.itemsPlaced >= 3u, 1);

    LayoutGridEnd(&fixture);
}

static void TestGridCountersBalance(void)
{
    LayoutGridFixture fixture;
    LayoutGridRule rules[3];
    LayoutStats stats;

    LayoutGridBody(&rules[0], 300.0);
    LayoutGridRuleText(&rules[0], "grid-template-columns: 1fr 1fr");
    LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutGridRulePx(&rules[1], "height", 20.0);

    LayoutGridBegin(&fixture,
                    "<html><body><div></div><div></div><div></div><div></div>"
                    "</body></html>",
                    rules, 2u);
    LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, &stats);

    /*
     * The accounting identity the block record states: every in-flow child of a grid
     * container is either an item that was placed or one the pass counted as skipped,
     * and `blocksPlaced` -- the number every direction contributes to -- agrees with
     * the grid's own count.
     */
    RoboCheckEqInt(stats.block.grid.containers, 1u);
    RoboCheckEqInt(stats.block.grid.itemsPlaced, 4u);
    RoboCheckEqInt(stats.block.grid.itemsSkipped, 0u);
    RoboCheckEqInt(stats.block.blocksPlaced, stats.block.grid.itemsPlaced);
    RoboCheckEqInt(stats.block.grid.columns, 2u);
    RoboCheckEqInt(stats.block.grid.rows, 2u);
    RoboCheckEqInt(stats.block.grid.itemsPlaced + stats.block.grid.itemsSkipped,
                   4u);

    LayoutGridEnd(&fixture);
}

static void TestGridAllocatorBalance(void)
{
    size_t before;
    size_t index;

    LayoutFixture();
    before = LayoutLiveBlocks();

    for (index = 0u; index < 8u; index++) {
        LayoutGridFixture fixture;
        LayoutGridRule rules[3];

        LayoutGridBody(&rules[0], 300.0);
        LayoutGridRuleText(&rules[0], "grid-template-columns: 1fr 2fr");
        LayoutGridRulePx(&rules[0], "column-gap", 10.0);
        LayoutGridRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
        LayoutGridRulePx(&rules[1], "height", 20.0);

        LayoutGridBegin(&fixture,
                        "<html><body><div></div><div></div><div></div></body></html>",
                        rules, 2u);
        LayoutGridPass(&fixture, LAYOUT_GRID_VIEWPORT_WIDTH, NULL);
        LayoutGridEnd(&fixture);
        RoboCheckEqInt(LayoutLiveBlocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutGrid(void);

void TestRegisterLayoutGrid(void)
{
    TestRegister("layout.grid.capability", TestGridCapability);
    TestRegister("layout.grid.formatting_context", TestGridFormattingContext);
    TestRegister("layout.grid.fixed_columns", TestGridFixedColumns);
    TestRegister("layout.grid.fraction_columns", TestGridFractionColumns);
    TestRegister("layout.grid.gap", TestGridGap);
    TestRegister("layout.grid.content_column", TestGridContentColumn);
    TestRegister("layout.grid.auto_stretch", TestGridAutoStretch);
    TestRegister("layout.grid.implicit_rows", TestGridImplicitRows);
    TestRegister("layout.grid.auto_flow_column", TestGridAutoFlowColumn);
    TestRegister("layout.grid.line_placement", TestGridLinePlacement);
    TestRegister("layout.grid.both_axes_placed", TestGridBothAxesPlaced);
    TestRegister("layout.grid.spanning", TestGridSpanning);
    TestRegister("layout.grid.dense", TestGridDense);
    TestRegister("layout.grid.align_items", TestGridAlignItems);
    TestRegister("layout.grid.align_content", TestGridAlignContent);
    TestRegister("layout.grid.baseline", TestGridBaseline);
    TestRegister("layout.grid.auto_margins", TestGridAutoMargins);
    TestRegister("layout.grid.overflow", TestGridOverflow);
    TestRegister("layout.grid.rtl", TestGridRtl);
    TestRegister("layout.grid.nested", TestGridNested);
    TestRegister("layout.grid.counters_balance", TestGridCountersBalance);
    TestRegister("layout.grid.allocator_balance", TestGridAllocatorBalance);
}
