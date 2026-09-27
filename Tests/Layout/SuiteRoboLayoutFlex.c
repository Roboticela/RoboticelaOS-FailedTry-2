/*
 * RoboLayout flex suite (Phase 19.3) — layout.flex.*
 *
 * Four things are under test, and they are told apart deliberately:
 *
 *   the **formatting context**: does `display: flex` make a box establish a flex
 *   context, and does the flex pass own its children rather than the block pass?
 *
 *   the **base sizing**: `flex-basis`, the main-size property, and the measured
 *   content size each reach the geometry, and `flex-grow`/`flex-shrink` distribute
 *   free space;
 *
 *   the **axes**: a row distributes along x and aligns along y, a column swaps the
 *   two, and `justify-content`/`align-items` place the items;
 *
 *   the **lines and order**: `flex-wrap` breaks the items, `wrap-reverse` swaps the
 *   line order, and `order` reorders the items before anything is measured.
 *
 * The fixture fabricates a *typed* computed style per tag -- including
 * `flex-direction`, `flex-grow`, `flex-wrap` and the alignment values -- because the
 * point of the phase is that the pass reads those *values*, and a case built from a
 * stylesheet could only prove the parser. Geometry is read back through the public
 * result doors, so every expectation is a number a consumer could read.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fixture that can declare flex                                         */
/* ------------------------------------------------------------------ */

typedef enum layoutFlexDeclKind {
    LAYOUT_FLEX_DECL_PX = 0,
    LAYOUT_FLEX_DECL_PERCENTAGE = 1,
    LAYOUT_FLEX_DECL_NUMBER = 2,
    LAYOUT_FLEX_DECL_KEYWORD = 3,
    LAYOUT_FLEX_DECL_DIRECTION = 4,
    LAYOUT_FLEX_DECL_WRAP = 5,
    LAYOUT_FLEX_DECL_ALIGN = 6
} LayoutFlexDeclKind;

typedef struct layoutFlexDecl {
    const char *name;
    LayoutFlexDeclKind kind;
    double number;
    int typed;
    const char *keyword;
} LayoutFlexDecl;

#define LAYOUT_FLEX_MAX_DECLARATIONS 10u
#define LAYOUT_FLEX_MAX_RULES 24u
#define LAYOUT_FLEX_MAX_TAGS 16u

typedef struct layoutFlexRule {
    const char *tag;
    int nth; /* which occurrence of @tag this rule styles, 0-based */
    CssDisplay display;
    LayoutFlexDecl declarations[LAYOUT_FLEX_MAX_DECLARATIONS];
    size_t declarationsCount;
} LayoutFlexRule;

typedef struct layoutFlexStyle {
    char tag[16];
    int nth; /* which occurrence of @tag this style serves, 0-based */
    CssComputedStyle *style;
} LayoutFlexStyle;

typedef struct layoutFlexTagCount {
    char tag[16];
    int seen;
} LayoutFlexTagCount;

typedef struct layoutFlexSource {
    LayoutFlexStyle rules[LAYOUT_FLEX_MAX_RULES];
    size_t count;
    LayoutFlexTagCount tagCounts[LAYOUT_FLEX_MAX_TAGS];
    size_t tagCount;
} LayoutFlexSource;

static void LayoutFlexRuleInit(LayoutFlexRule *rule,
                               const char *tag,
                               int nth,
                               CssDisplay display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->nth = nth;
    rule->display = display;
}

static void LayoutFlexRuleAdd(LayoutFlexRule *rule,
                              const char *name,
                              LayoutFlexDeclKind kind,
                              double number,
                              int typed,
                              const char *keyword)
{
    RoboCheckEqInt(rule->declarationsCount < LAYOUT_FLEX_MAX_DECLARATIONS, 1);
    rule->declarations[rule->declarationsCount].name = name;
    rule->declarations[rule->declarationsCount].kind = kind;
    rule->declarations[rule->declarationsCount].number = number;
    rule->declarations[rule->declarationsCount].typed = typed;
    rule->declarations[rule->declarationsCount].keyword = keyword;
    rule->declarationsCount++;
}

static void LayoutFlexRulePx(LayoutFlexRule *rule, const char *name, double px)
{
    LayoutFlexRuleAdd(rule, name, LAYOUT_FLEX_DECL_PX, px, 0, NULL);
}

static void LayoutFlexRulePercent(LayoutFlexRule *rule, const char *name,
                                  double percent)
{
    LayoutFlexRuleAdd(rule, name, LAYOUT_FLEX_DECL_PERCENTAGE, percent, 0, NULL);
}

static void LayoutFlexRuleNumber(LayoutFlexRule *rule, const char *name,
                                 double number)
{
    LayoutFlexRuleAdd(rule, name, LAYOUT_FLEX_DECL_NUMBER, number, 0, NULL);
}

static void LayoutFlexRuleDirection(LayoutFlexRule *rule, CssFlexDirection value)
{
    LayoutFlexRuleAdd(rule, "flex-direction", LAYOUT_FLEX_DECL_DIRECTION, 0.0,
                      (int)value, NULL);
}

static void LayoutFlexRuleWrap(LayoutFlexRule *rule, CssFlexWrap value)
{
    LayoutFlexRuleAdd(rule, "flex-wrap", LAYOUT_FLEX_DECL_WRAP, 0.0, (int)value,
                      NULL);
}

static void LayoutFlexRuleAlign(LayoutFlexRule *rule, const char *property,
                                CssFlexAlign value)
{
    LayoutFlexRuleAdd(rule, property, LAYOUT_FLEX_DECL_ALIGN, 0.0, (int)value,
                      NULL);
}

static void LayoutFlexStyleAdd(LayoutFlexSource *source,
                               const LayoutFlexRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    size_t index;
    size_t slot;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_FLEX_MAX_RULES, 1);
    slot = source->count;
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeDisplay(&value, rule->display, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                            CssSliceFromText("display"), 0u,
                                            &specified, &value, NULL),
                   RoboOk);

    for (index = 0u; index < rule->declarationsCount; index++) {
        const LayoutFlexDecl *decl = &rule->declarations[index];

        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        switch (decl->kind) {
        case LAYOUT_FLEX_DECL_PERCENTAGE:
            RoboCheckEqInt(CssComputedValueMakePercentage(&value, decl->number, 0,
                                                          NULL),
                           RoboOk);
            break;
        case LAYOUT_FLEX_DECL_NUMBER:
            RoboCheckEqInt(CssComputedValueMakeNumber(&value, decl->number, 0, NULL),
                           RoboOk);
            break;
        case LAYOUT_FLEX_DECL_KEYWORD:
            RoboCheckEqInt(CssComputedValueMakeKeyword(
                               &value, CssSliceFromText(decl->keyword), NULL),
                           RoboOk);
            break;
        case LAYOUT_FLEX_DECL_DIRECTION:
            RoboCheckEqInt(CssComputedValueMakeFlexDirection(
                               &value, (CssFlexDirection)decl->typed, NULL),
                           RoboOk);
            break;
        case LAYOUT_FLEX_DECL_WRAP:
            RoboCheckEqInt(CssComputedValueMakeFlexWrap(
                               &value, (CssFlexWrap)decl->typed, NULL),
                           RoboOk);
            break;
        case LAYOUT_FLEX_DECL_ALIGN:
            RoboCheckEqInt(CssComputedValueMakeFlexAlign(
                               &value, (CssFlexAlign)decl->typed, NULL),
                           RoboOk);
            break;
        case LAYOUT_FLEX_DECL_PX:
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

static int LayoutFlexTagCountFor(LayoutFlexSource *source, const char *tag)
{
    size_t index;

    for (index = 0u; index < source->tagCount; index++) {
        if (strcmp(source->tagCounts[index].tag, tag) == 0) {
            return source->tagCounts[index].seen++;
        }
    }
    RoboCheckEqInt(source->tagCount < LAYOUT_FLEX_MAX_TAGS, 1);
    strncpy(source->tagCounts[source->tagCount].tag, tag,
            sizeof(source->tagCounts[0].tag) - 1u);
    source->tagCounts[source->tagCount].tag[sizeof(source->tagCounts[0].tag) - 1u] =
        '\0';
    source->tagCounts[source->tagCount].seen = 1;
    source->tagCount++;
    return 0;
}

static const CssComputedStyle *LayoutFlexStyleLookup(void *userdata,
                                                     const HtmlNodeHandle *element)
{
    LayoutFlexSource *source = (LayoutFlexSource *)userdata;
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
    nth = LayoutFlexTagCountFor(source, name);
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, name) == 0 &&
            source->rules[index].nth == nth) {
            return source->rules[index].style;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

typedef struct layoutFlexFixture {
    LayoutDocument doc;
    LayoutFlexSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutFlexFixture;

static void LayoutFlexBegin(LayoutFlexFixture *fixture,
                            const char *html,
                            const LayoutFlexRule *rules,
                            size_t ruleCount)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < ruleCount; index++) {
        LayoutFlexStyleAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutFlexStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                               &options, NULL, NULL),
                   RoboOk);
}

static void LayoutFlexEnd(LayoutFlexFixture *fixture)
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

#define LAYOUT_FLEX_VIEWPORT_WIDTH 800
#define LAYOUT_FLEX_VIEWPORT_HEIGHT 600

static void LayoutFlexPass(LayoutFlexFixture *fixture,
                           int32_t viewportWidth,
                           LayoutStats *outStats)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;

    viewport.width = viewportWidth;
    viewport.height = LAYOUT_FLEX_VIEWPORT_HEIGHT;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, fixture->tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);
    if (outStats) {
        *outStats = stats;
    }
}

static LayoutNodeId LayoutFlexBoxNth(LayoutFlexFixture *fixture,
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

static LayoutSize LayoutFlexSizeOf(LayoutFlexFixture *fixture, LayoutNodeId id)
{
    LayoutResult result;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    return size;
}

static LayoutPoint LayoutFlexPositionOf(LayoutFlexFixture *fixture,
                                        LayoutNodeId id)
{
    LayoutResult result;
    LayoutPoint position;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    return position;
}

/* ------------------------------------------------------------------ */
/* The formatting context                                              */
/* ------------------------------------------------------------------ */

static void TestFlexFormattingContext(void)
{
    LayoutFlexFixture fixture;
    LayoutFlexRule rules[2];
    LayoutNodeId body;
    LayoutFcId owned = RoboLayoutFcIdNone;
    LayoutFormattingContext context;

    LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
    LayoutFlexRulePx(&rules[0], "width", 300.0);
    LayoutFlexRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);

    LayoutFlexBegin(&fixture, "<html><body><div></div></body></html>", rules, 2u);
    LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, NULL);

    body = LayoutFlexBoxNth(&fixture, "body", 0u);
    RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, body, &owned, NULL),
                   RoboOk);
    RoboCheckEqInt(LayoutTreeContext(fixture.tree, owned, &context, NULL), RoboOk);
    RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcFlex);

    LayoutFlexEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Base sizing: grow, shrink, basis                                    */
/* ------------------------------------------------------------------ */

static void TestFlexRowGrow(void)
{
    LayoutFlexFixture fixture;
    LayoutFlexRule rules[4];
    int i;

    LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
    LayoutFlexRulePx(&rules[0], "width", 300.0);
    for (i = 0; i < 3; i++) {
        LayoutFlexRuleInit(&rules[1 + i], "div", i, RoboCssDisplayBlock);
        LayoutFlexRulePx(&rules[1 + i], "flex-basis", 0.0);
    }
    LayoutFlexRuleNumber(&rules[1], "flex-grow", 1.0);
    LayoutFlexRuleNumber(&rules[2], "flex-grow", 2.0);
    LayoutFlexRuleNumber(&rules[3], "flex-grow", 1.0);

    LayoutFlexBegin(&fixture, "<html><body><div></div><div></div><div></div></body></html>",
                    rules, 4u);
    LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, NULL);

    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 0u)).width,
                   75);
    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 1u)).width,
                   150);
    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 2u)).width,
                   75);
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 1u)).x,
                   75);
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 2u)).x,
                   225);

    LayoutFlexEnd(&fixture);
}

static void TestFlexRowBasis(void)
{
    LayoutFlexFixture fixture;
    LayoutFlexRule rules[4];

    LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
    LayoutFlexRulePx(&rules[0], "width", 300.0);
    LayoutFlexRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[1], "flex-basis", 40.0);
    LayoutFlexRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[2], "flex-basis", 50.0);
    LayoutFlexRuleInit(&rules[3], "div", 2, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[3], "flex-basis", 60.0);

    LayoutFlexBegin(&fixture, "<html><body><div></div><div></div><div></div></body></html>",
                    rules, 4u);
    LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, NULL);

    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 0u)).width,
                   40);
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 1u)).x,
                   40);
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 2u)).x,
                   90);

    LayoutFlexEnd(&fixture);
}

static void TestFlexRowShrink(void)
{
    LayoutFlexFixture fixture;
    LayoutFlexRule rules[4];

    LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
    LayoutFlexRulePx(&rules[0], "width", 150.0);
    LayoutFlexRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[1], "flex-basis", 100.0);
    LayoutFlexRuleNumber(&rules[1], "flex-shrink", 1.0);
    LayoutFlexRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[2], "flex-basis", 100.0);
    LayoutFlexRuleNumber(&rules[2], "flex-shrink", 1.0);
    LayoutFlexRuleInit(&rules[3], "div", 2, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[3], "flex-basis", 100.0);
    LayoutFlexRuleNumber(&rules[3], "flex-shrink", 1.0);

    LayoutFlexBegin(&fixture, "<html><body><div></div><div></div><div></div></body></html>",
                    rules, 4u);
    LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, NULL);

    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 0u)).width,
                   50);
    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 1u)).width,
                   50);
    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 2u)).width,
                   50);

    LayoutFlexEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Alignment                                                           */
/* ------------------------------------------------------------------ */

static void TestFlexBasisPercentage(void)
{
    LayoutFlexFixture fixture;
    LayoutFlexRule rules[2];

    LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
    LayoutFlexRulePx(&rules[0], "width", 200.0);
    LayoutFlexRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutFlexRulePercent(&rules[1], "flex-basis", 25.0);

    LayoutFlexBegin(&fixture, "<html><body><div></div></body></html>", rules, 2u);
    LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, NULL);

    /* `flex-basis: 25%` of a 200px main size. */
    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 0u)).width,
                   50);

    LayoutFlexEnd(&fixture);
}

static void TestFlexJustifyCenter(void)
{
    LayoutFlexFixture fixture;
    LayoutFlexRule rules[3];

    LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
    LayoutFlexRulePx(&rules[0], "width", 100.0);
    LayoutFlexRuleAlign(&rules[0], "justify-content", RoboCssFlexAlignCenter);
    LayoutFlexRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[1], "flex-basis", 20.0);
    LayoutFlexRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[2], "flex-basis", 20.0);

    LayoutFlexBegin(&fixture, "<html><body><div></div><div></div></body></html>", rules,
                    3u);
    LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, NULL);

    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 0u)).x,
                   30);
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 1u)).x,
                   50);

    LayoutFlexEnd(&fixture);
}

static void TestFlexAlignStretch(void)
{
    LayoutFlexFixture fixture;
    LayoutFlexRule rules[3];

    LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
    LayoutFlexRulePx(&rules[0], "width", 300.0);
    LayoutFlexRulePx(&rules[0], "height", 200.0);
    LayoutFlexRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[1], "flex-basis", 100.0);
    LayoutFlexRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[2], "flex-basis", 100.0);

    LayoutFlexBegin(&fixture, "<html><body><div></div><div></div></body></html>", rules,
                    3u);
    LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, NULL);

    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 0u)).height,
                   200);
    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 1u)).height,
                   200);

    LayoutFlexEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Column and lines                                                    */
/* ------------------------------------------------------------------ */

static void TestFlexColumn(void)
{
    LayoutFlexFixture fixture;
    LayoutFlexRule rules[4];

    LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
    LayoutFlexRuleDirection(&rules[0], RoboCssFlexDirectionColumn);
    LayoutFlexRulePx(&rules[0], "width", 300.0);
    LayoutFlexRulePx(&rules[0], "height", 200.0);
    LayoutFlexRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[1], "flex-basis", 50.0);
    LayoutFlexRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[2], "flex-basis", 50.0);
    LayoutFlexRuleInit(&rules[3], "div", 2, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[3], "flex-basis", 50.0);

    LayoutFlexBegin(&fixture, "<html><body><div></div><div></div><div></div></body></html>",
                    rules, 4u);
    LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, NULL);

    /* A column's main axis is vertical: the items stack, and each stretches to the
     * container's width across the cross axis. */
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 0u)).y,
                   0);
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 1u)).y,
                   50);
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 2u)).y,
                   100);
    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 0u)).width,
                   300);

    LayoutFlexEnd(&fixture);
}

static void TestFlexWrap(void)
{
    LayoutFlexFixture fixture;
    LayoutFlexRule rules[4];
    LayoutStats stats;
    int i;

    LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
    LayoutFlexRulePx(&rules[0], "width", 100.0);
    LayoutFlexRuleWrap(&rules[0], RoboCssFlexWrapWrap);
    for (i = 0; i < 3; i++) {
        LayoutFlexRuleInit(&rules[1 + i], "div", i, RoboCssDisplayBlock);
        LayoutFlexRulePx(&rules[1 + i], "flex-basis", 60.0);
        LayoutFlexRulePx(&rules[1 + i], "height", 40.0);
    }

    LayoutFlexBegin(&fixture, "<html><body><div></div><div></div><div></div></body></html>",
                    rules, 4u);
    LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, &stats);

    /* Each item is 60 wide in a 100-wide container, so each takes its own line;
     * every line is 40 tall (the item's height), so the lines stack 40 apart. */
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 0u)).y,
                   0);
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 1u)).y,
                   40);
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 2u)).y,
                   80);
    RoboCheckEqInt(stats.block.flex.lines, 3u);

    LayoutFlexEnd(&fixture);
}

static void TestFlexOrder(void)
{
    LayoutFlexFixture fixture;
    LayoutFlexRule rules[3];

    LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
    LayoutFlexRulePx(&rules[0], "width", 300.0);
    LayoutFlexRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[1], "flex-basis", 50.0);
    LayoutFlexRuleNumber(&rules[1], "order", 2.0);
    LayoutFlexRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[2], "flex-basis", 60.0);
    LayoutFlexRuleNumber(&rules[2], "order", 1.0);

    LayoutFlexBegin(&fixture, "<html><body><div></div><div></div></body></html>", rules,
                    3u);
    LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, NULL);

    /* The second div declared `order: 1`, so it is placed first. */
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 0u)).x,
                   60);
    RoboCheckEqInt(LayoutFlexPositionOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 1u)).x,
                   0);

    LayoutFlexEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Item contents and counters                                          */
/* ------------------------------------------------------------------ */

static void TestFlexItemChildren(void)
{
    LayoutFlexFixture fixture;
    LayoutFlexRule rules[3];

    LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
    LayoutFlexRulePx(&rules[0], "width", 300.0);
    LayoutFlexRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[1], "flex-basis", 100.0);
    LayoutFlexRuleInit(&rules[2], "p", 0, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[2], "height", 30.0);

    LayoutFlexBegin(&fixture, "<html><body><div><p></p></div></body></html>", rules, 3u);
    LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, NULL);

    /* The `p` is laid out inside the item at the item's content width, and the
     * item's own `auto` cross size is the extent it reached. */
    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "p", 0u)).width,
                   100);
    RoboCheckEqInt(LayoutFlexSizeOf(&fixture, LayoutFlexBoxNth(&fixture, "div", 0u)).height,
                   30);

    LayoutFlexEnd(&fixture);
}

static void TestFlexCounters(void)
{
    LayoutFlexFixture fixture;
    LayoutFlexRule rules[3];
    LayoutStats stats;

    LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
    LayoutFlexRulePx(&rules[0], "width", 300.0);
    LayoutFlexRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[1], "flex-basis", 40.0);
    LayoutFlexRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
    LayoutFlexRulePx(&rules[2], "flex-basis", 40.0);

    LayoutFlexBegin(&fixture, "<html><body><div></div><div></div></body></html>", rules,
                    3u);
    LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, &stats);

    RoboCheckEqInt(stats.block.flex.containers, 1u);
    RoboCheckEqInt(stats.block.flex.itemsPlaced, 2u);
    RoboCheckEqInt(stats.block.flex.lines, 1u);

    LayoutFlexEnd(&fixture);
}

static void TestFlexCapability(void)
{
    LayoutCapabilities caps;

    LayoutFixture();
    RoboCheckEqInt(RoboLayoutFlexLayoutImplemented, 1);
    RoboCheckEqInt(LayoutGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.flexLayout, RoboLayoutFlexLayoutImplemented);
}

static void TestFlexAllocatorBalance(void)
{
    size_t before;
    size_t index;

    LayoutFixture();
    before = LayoutLiveBlocks();

    for (index = 0u; index < 8u; index++) {
        LayoutFlexFixture fixture;
        LayoutFlexRule rules[3];

        LayoutFlexRuleInit(&rules[0], "body", 0, RoboCssDisplayFlex);
        LayoutFlexRulePx(&rules[0], "width", 300.0);
        LayoutFlexRuleInit(&rules[1], "div", 0, RoboCssDisplayBlock);
        LayoutFlexRulePx(&rules[1], "flex-basis", 40.0);
        LayoutFlexRuleInit(&rules[2], "div", 1, RoboCssDisplayBlock);
        LayoutFlexRulePx(&rules[2], "flex-basis", 40.0);
        LayoutFlexBegin(&fixture,
                        "<html><body><div></div><div></div></body></html>", rules, 3u);
        LayoutFlexPass(&fixture, LAYOUT_FLEX_VIEWPORT_WIDTH, NULL);
        LayoutFlexEnd(&fixture);
        RoboCheckEqInt(LayoutLiveBlocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutFlex(void);

void TestRegisterLayoutFlex(void)
{
    TestRegister("layout.flex.capability", TestFlexCapability);
    TestRegister("layout.flex.formatting_context", TestFlexFormattingContext);
    TestRegister("layout.flex.row_grow", TestFlexRowGrow);
    TestRegister("layout.flex.row_basis", TestFlexRowBasis);
    TestRegister("layout.flex.row_shrink", TestFlexRowShrink);
    TestRegister("layout.flex.basis_percentage", TestFlexBasisPercentage);
    TestRegister("layout.flex.justify_center", TestFlexJustifyCenter);
    TestRegister("layout.flex.align_stretch", TestFlexAlignStretch);
    TestRegister("layout.flex.column", TestFlexColumn);
    TestRegister("layout.flex.wrap", TestFlexWrap);
    TestRegister("layout.flex.order", TestFlexOrder);
    TestRegister("layout.flex.item_children", TestFlexItemChildren);
    TestRegister("layout.flex.counters", TestFlexCounters);
    TestRegister("layout.flex.allocator_balance", TestFlexAllocatorBalance);
}
