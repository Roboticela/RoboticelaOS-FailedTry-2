/*
 * RoboLayout overflow suite (Phase 10.13) — layout.overflow.*
 *
 * Five things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary** -- an axis record with three kinds (`absent`, `resolved`,
 *   `unresolved`) over two axes, and an *extent* record that is four non-negative
 *   numbers measured from the padding box. The zero of the axis vocabulary is
 *   `ABSENT`, so a cleared record holds CSS's initial state rather than a claim that
 *   somebody wrote `visible`;
 *
 *   the **seam**: what a computed style declared. The longhand first, the `overflow`
 *   shorthand only for an axis the longhand said nothing about -- which is why
 *   `overflow-x: visible; overflow: hidden` clips vertically and *not* horizontally,
 *   and why a declaration this build cannot use arrives as `UNRESOLVED` rather than as
 *   an absence;
 *
 *   the **policy**: the three rules driven over fabricated records, so a wrong
 *   counter fails at the record rather than three boxes later in an extent;
 *
 *   the **arithmetic**: four per-side *maxima* (not a union), the region that is the
 *   padding box grown by them, and the clip rectangle that is the padding box. The
 *   distinction that makes the recursion work -- a child contributes its **region**,
 *   so a child that clips contributes only what it kept -- is asserted at both ends:
 *   on the fabricated records here, and on a three-level document below;
 *
 *   the **pass**: a box that keeps the size its declarations asked for while the
 *   region its contents reached is recorded *beside* it. The witness is the pair of
 *   assertions every single-box test could pass without honouring -- the box's
 *   `size` is unchanged and its **ancestor** did not grow -- because the most
 *   plausible way to get this phase wrong is to fold an overflow into a size.
 *
 * The scripts here are *computed styles*, built through 9.8's makers exactly as the
 * sizing, margin, padding, border and position suites build them, because a fabricated
 * struct would prove the pass reads a struct rather than a computed CSS value. The
 * value is 10.13's own OVERFLOW kind, and `scroll` -- a real CSS overflow value this
 * build has not added -- arrives as a *keyword*, which is exactly how the seam meets
 * an unusable declaration.
 *
 * Elements are resolved **by tag** (`LayoutOverflowBoxFor`) rather than by arena
 * index, which is the lesson 10.8's suite wrote down, so the tags here are distinct
 * and one scripted style per element is enough.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fabricated style that can declare anything                        */
/* ------------------------------------------------------------------ */

/** One declaration to put on a fabricated style. */
typedef enum layoutOverflowDeclKind {
    LAYOUT_OVERFLOW_DECL_PX = 0,
    /** A px length **above** the style seam's maximum, for the saturation cases. */
    LAYOUT_OVERFLOW_DECL_HUGE_PX = 1,
    /** A px length that is negative: legal for an offset, refused for an extent. */
    LAYOUT_OVERFLOW_DECL_NEGATIVE_PX = 2,
    /** A px length flagged unresolved, which is what a `var()` arrives as. */
    LAYOUT_OVERFLOW_DECL_UNRESOLVED_PX = 3,
    /**
     * An identifier. `scroll` and `auto` are real CSS overflow values this build has
     * not added, and they arrive at the seam as this -- a keyword the overflow reader
     * cannot classify -- which is the phase's `UNRESOLVED` case.
     */
    LAYOUT_OVERFLOW_DECL_KEYWORD = 4,
    /** A `CssComputedValue` of kind OVERFLOW: 10.13's typed vocabulary. */
    LAYOUT_OVERFLOW_DECL_OVERFLOW = 5,
    /** A `CssComputedValue` of kind POSITION: 9.13's, for the relative cases. */
    LAYOUT_OVERFLOW_DECL_POSITION = 6,
    /** A typed `border-*-style`, which is what makes a declared width a *used* one. */
    LAYOUT_OVERFLOW_DECL_BORDER_STYLE = 7
} LayoutOverflowDeclKind;

typedef struct layoutOverflowDecl {
    const char *name;
    LayoutOverflowDeclKind kind;
    double number;
    const char *keyword;
    CssOverflow overflow;
    CssPositioning position;
    CssBorderStyle borderStyle;
} LayoutOverflowDecl;

#define LAYOUT_OVERFLOW_MAX_DECLARATIONS 24u
#define LAYOUT_OVERFLOW_MAX_RULES 8u

typedef struct layoutOverflowRule {
    const char *tag;
    CssDisplay display;
    LayoutOverflowDecl declarations[LAYOUT_OVERFLOW_MAX_DECLARATIONS];
    size_t declarationsCount;
} LayoutOverflowRule;

static void LayoutOverflowRuleInit(LayoutOverflowRule *rule,
                                      const char *tag,
                                      CssDisplay display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static LayoutOverflowDecl *LayoutOverflowRuleAdd(LayoutOverflowRule *rule,
                                                        const char *name,
                                                        LayoutOverflowDeclKind kind)
{
    LayoutOverflowDecl *declaration;

    RoboCheckEqInt(rule->declarationsCount < LAYOUT_OVERFLOW_MAX_DECLARATIONS, 1);
    declaration = &rule->declarations[rule->declarationsCount];
    memset(declaration, 0, sizeof(*declaration));
    declaration->name = name;
    declaration->kind = kind;
    declaration->overflow = RoboCssOverflowVisible;
    declaration->position = RoboCssPositioningStatic;
    rule->declarationsCount++;
    return declaration;
}

static void LayoutOverflowRulePx(LayoutOverflowRule *rule, const char *name, double px)
{
    LayoutOverflowRuleAdd(rule, name, LAYOUT_OVERFLOW_DECL_PX)->number = px;
}

static void LayoutOverflowRuleKeyword(LayoutOverflowRule *rule,
                                         const char *name,
                                         const char *keyword)
{
    LayoutOverflowRuleAdd(rule, name, LAYOUT_OVERFLOW_DECL_KEYWORD)->keyword = keyword;
}

static void LayoutOverflowRuleOverflow(LayoutOverflowRule *rule,
                                          const char *name,
                                          CssOverflow overflow)
{
    LayoutOverflowRuleAdd(rule, name, LAYOUT_OVERFLOW_DECL_OVERFLOW)->overflow = overflow;
}

static void LayoutOverflowRulePosition(LayoutOverflowRule *rule,
                                          CssPositioning mode)
{
    LayoutOverflowRuleAdd(rule, "position", LAYOUT_OVERFLOW_DECL_POSITION)->position = mode;
}

static void LayoutOverflowRuleBorderStyle(LayoutOverflowRule *rule,
                                              const char *name,
                                              CssBorderStyle style)
{
    LayoutOverflowRuleAdd(rule, name, LAYOUT_OVERFLOW_DECL_BORDER_STYLE)->borderStyle =
        style;
}

/** `border-width: Npx solid`, all four sides -- the shorthand this build has no expansion for. */
static void LayoutOverflowRuleBorderAll(LayoutOverflowRule *rule, double px)
{
    LayoutOverflowRulePx(rule, "border-top-width", px);
    LayoutOverflowRulePx(rule, "border-right-width", px);
    LayoutOverflowRulePx(rule, "border-bottom-width", px);
    LayoutOverflowRulePx(rule, "border-left-width", px);
    LayoutOverflowRuleBorderStyle(rule, "border-top-style", RoboCssBorderStyleSolid);
    LayoutOverflowRuleBorderStyle(rule, "border-right-style", RoboCssBorderStyleSolid);
    LayoutOverflowRuleBorderStyle(rule, "border-bottom-style", RoboCssBorderStyleSolid);
    LayoutOverflowRuleBorderStyle(rule, "border-left-style", RoboCssBorderStyleSolid);
}

/** `padding: Npx`, four longhands. */
static void LayoutOverflowRulePaddingAll(LayoutOverflowRule *rule, double px)
{
    LayoutOverflowRulePx(rule, "padding-top", px);
    LayoutOverflowRulePx(rule, "padding-right", px);
    LayoutOverflowRulePx(rule, "padding-bottom", px);
    LayoutOverflowRulePx(rule, "padding-left", px);
}

typedef struct layoutOverflowStyleSlot {
    char tag[16];
    CssComputedStyle *style; /* owned */
    size_t answers;
} LayoutOverflowStyleSlot;

typedef struct layoutOverflowSource {
    LayoutOverflowStyleSlot rules[LAYOUT_OVERFLOW_MAX_RULES];
    size_t count;
} LayoutOverflowSource;

/**
 * Build the style @p rule describes: a real DISPLAY value plus whatever the rule
 * declares, all through 9.8's own makers and container.
 *
 * `display` is set even when the rule declares nothing else, because 9.12's initial
 * value is `inline` and a fixture that wanted a block box would otherwise be asserting
 * a document made of inline boxes.
 */
static CssComputedStyle *LayoutOverflowStyleBuild(const LayoutOverflowRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    size_t index;

    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeDisplay(&value, rule->display, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("display"), 0u,
                                                       &specified, &value, NULL),
                      RoboOk);

    for (index = 0u; index < rule->declarationsCount; index++) {
        const LayoutOverflowDecl *declaration = &rule->declarations[index];

        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        switch (declaration->kind) {
        case LAYOUT_OVERFLOW_DECL_OVERFLOW:
            RoboCheckEqInt(CssComputedValueMakeOverflow(&value, declaration->overflow,
                                                                    NULL),
                              RoboOk);
            break;
        case LAYOUT_OVERFLOW_DECL_POSITION:
            RoboCheckEqInt(CssComputedValueMakePosition(&value, declaration->position,
                                                                    NULL),
                              RoboOk);
            break;
        case LAYOUT_OVERFLOW_DECL_BORDER_STYLE:
            RoboCheckEqInt(CssComputedValueMakeBorderStyle(
                                  &value, declaration->borderStyle, NULL),
                              RoboOk);
            break;
        case LAYOUT_OVERFLOW_DECL_KEYWORD:
            RoboCheckEqInt(CssComputedValueMakeKeyword(
                                  &value, CssSliceFromText(declaration->keyword), NULL),
                              RoboOk);
            break;
        case LAYOUT_OVERFLOW_DECL_UNRESOLVED_PX:
            RoboCheckEqInt(CssComputedValueMakeLength(&value, declaration->number,
                                                                 RoboCssUnitPx, 0, NULL),
                              RoboOk);
            /* The flag the computed layer sets for a `var()` or a `calc()`: carried
             * intact and not a value *yet*, whatever its kind says. */
            value.unresolved = 1;
            break;
        case LAYOUT_OVERFLOW_DECL_HUGE_PX:
        case LAYOUT_OVERFLOW_DECL_NEGATIVE_PX:
        case LAYOUT_OVERFLOW_DECL_PX:
        default:
            RoboCheckEqInt(CssComputedValueMakeLength(&value, declaration->number,
                                                                 RoboCssUnitPx, 0, NULL),
                              RoboOk);
            break;
        }
        RoboCheckEqInt(CssComputedStyleSetEntry(
                              style, RoboCssPropertyIdNone,
                              CssSliceFromText(declaration->name), 0u, &specified, &value,
                              NULL),
                          RoboOk);
    }
    return style;
}

/** Add @p rule to @p source, and answer the style it built. */
static CssComputedStyle *LayoutOverflowStyleAdd(LayoutOverflowSource *source,
                                                            const LayoutOverflowRule *rule)
{
    size_t slot;
    CssComputedStyle *style;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_OVERFLOW_MAX_RULES, 1);
    slot = source->count;
    style = LayoutOverflowStyleBuild(rule);
    strncpy(source->rules[slot].tag, rule->tag, sizeof(source->rules[slot].tag) - 1u);
    source->rules[slot].tag[sizeof(source->rules[slot].tag) - 1u] = '\0';
    source->rules[slot].style = style;
    source->rules[slot].answers = 0u;
    source->count++;
    return style;
}

/**
 * Rebuild the style @p source answers @p tag with, **in the same slot**.
 *
 * Replacing rather than appending, and that is not tidiness: the source answers the
 * *first* row whose tag matches, so an appended second row would be shadowed by the one
 * it was meant to replace and the case would assert the old declaration while appearing
 * to have changed it. The rule is 10.9's, 10.10's, 10.11's and 10.12's.
 */
static void LayoutOverflowStyleReplace(LayoutOverflowSource *source,
                                          const char *tag,
                                          const LayoutOverflowRule *rule)
{
    size_t index;

    LayoutFixture();
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            CssComputedStyleDestroy(source->rules[index].style);
            source->rules[index].style = LayoutOverflowStyleBuild(rule);
            return;
        }
    }
    RoboCheckMsg(0, "the tag to replace is not in this source");
}

static const CssComputedStyle *LayoutOverflowStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutOverflowSource *source = (LayoutOverflowSource *)userdata;
    const char *name;
    size_t index;

    if (!source || !element) {
        return NULL;
    }
    name = HtmlElementName(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, name) == 0) {
            source->rules[index].answers++;
            return source->rules[index].style;
        }
    }
    /* An element nothing was declared for: `display: inline` and two undeclared
     * overflow axes, which are CSS's initial values rather than a fixture default. */
    return NULL;
}

typedef struct layoutOverflowFixture {
    LayoutDocument doc;
    LayoutOverflowSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutOverflowFixture;

static void LayoutOverflowBegin(LayoutOverflowFixture *fixture,
                                  const char *html,
                                  const LayoutOverflowRule *rules,
                                  size_t ruleCount)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < ruleCount; index++) {
        (void)LayoutOverflowStyleAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutOverflowStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutOverflowEnd(LayoutOverflowFixture *fixture)
{
    size_t index;

    LayoutTreeDestroy(fixture->tree);
    fixture->tree = NULL;
    for (index = 0u; index < fixture->source.count; index++) {
        CssComputedStyleDestroy(fixture->source.rules[index].style);
        fixture->source.rules[index].style = NULL;
    }
    fixture->source.count = 0u;
    LayoutDocumentRelease(&fixture->doc);
}

#define LAYOUT_OVERFLOW_VIEWPORT_WIDTH 800
#define LAYOUT_OVERFLOW_VIEWPORT_HEIGHT 600

/** One pass at @p viewportWidth. */
static void LayoutOverflowPass(LayoutOverflowFixture *fixture,
                                 int32_t viewportWidth,
                                 LayoutStats *outStats)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;

    viewport.width = viewportWidth;
    viewport.height = LAYOUT_OVERFLOW_VIEWPORT_HEIGHT;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, fixture->tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);
    if (outStats) {
        *outStats = stats;
    }
}

/** The layout box of the first @p tag in the fixture's document. */
static LayoutNodeId LayoutOverflowBoxFor(LayoutOverflowFixture *fixture,
                                                     const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);
    LayoutNodeId id;

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    id = LayoutObjectIdForDomNode(fixture->tree, &element);
    RoboCheck(id != RoboLayoutNodeIdNone);
    return id;
}

/** The first object in @p fixture's tree of @p kind -- how a text run is found. */
static LayoutNodeId LayoutOverflowFirstOfKind(LayoutOverflowFixture *fixture,
                                                           LayoutObjectKind kind)
{
    size_t total = LayoutTreeObjectCount(fixture->tree);
    size_t index;

    for (index = 0u; index < total; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;

        if (LayoutObjectIdAt(fixture->tree, index, &id) != RoboOk) {
            break;
        }
        if (MakeLayoutObjectKind(fixture->tree, id) == kind) {
            return id;
        }
    }
    return RoboLayoutNodeIdNone;
}

static LayoutResult LayoutOverflowResultOf(LayoutOverflowFixture *fixture,
                                                     LayoutNodeId id)
{
    LayoutResult result;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)MakeLayoutResultState(&result),
                      (int)RoboLayoutResultComputed);
    return result;
}

static LayoutPoint LayoutOverflowOriginOf(LayoutOverflowFixture *fixture,
                                                     LayoutNodeId id)
{
    LayoutResult result = LayoutOverflowResultOf(fixture, id);
    LayoutPoint position;

    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    return position;
}

static LayoutSize LayoutOverflowSizeOf(LayoutOverflowFixture *fixture,
                                                  LayoutNodeId id)
{
    LayoutResult result = LayoutOverflowResultOf(fixture, id);
    LayoutSize size;

    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    return size;
}

/** The record @p id was measured with, through the public door. */
static LayoutOverflowEdges LayoutOverflowEdgesOf(
    LayoutOverflowFixture *fixture,
    LayoutNodeId id)
{
    LayoutOverflowEdges edges;

    RoboCheckEqInt(LayoutObjectOverflow(fixture->tree, id, &edges, NULL), RoboOk);
    return edges;
}

/** The box's **padding box**, in its own space: the border box less the borders. */
static LayoutRect LayoutOverflowPaddingBoxOf(LayoutOverflowFixture *fixture,
                                                        LayoutNodeId id)
{
    LayoutSize size = LayoutOverflowSizeOf(fixture, id);
    LayoutBorderEdges borders;
    LayoutRect rect;

    RoboCheckEqInt(LayoutObjectBorders(fixture->tree, id, &borders, NULL), RoboOk);
    rect.x = 0;
    rect.y = 0;
    rect.width = size.width - borders.left.width - borders.right.width;
    rect.height = size.height - borders.top.width - borders.bottom.width;
    return rect;
}

/**
 * Rebuild the layout tree against the styles @p fixture was built with.
 *
 * The step a changed declaration needs and the reason it is not optional: the tree
 * **borrows** one computed style per element for as long as the bridge lives, so a
 * replacement that left the old tree standing would leave it holding a destroyed style.
 */
static void LayoutOverflowRebuild(LayoutOverflowFixture *fixture)
{
    LayoutBuildOptions options;

    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static int LayoutOverflowAxisCountOf(const LayoutOverflowEdges *edges)
{
    return (int)(edges->axesResolved + edges->axesUnresolved);
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestOverflowKindVocabulary(void)
{
    size_t index;

    LayoutFixture();

    RoboCheckEqInt((int)RoboLayoutOverflowKindCount, 3);
    /*
     * The zero of the vocabulary is `ABSENT`, and that is a decision rather than a
     * numbering accident: a cleared record then holds the axis's *initial state*, so a
     * forgotten initialisation is not a claim that somebody declared `visible`. The
     * same decision the margin, padding, border and position vocabularies made.
     */
    RoboCheckEqInt((int)RoboLayoutOverflowAbsent, 0);
    RoboCheckEqInt((int)RoboLayoutOverflowResolved, 1);
    RoboCheckEqInt((int)RoboLayoutOverflowUnresolved, 2);

    for (index = 0u; index < RoboLayoutOverflowKindCount; index++) {
        LayoutOverflowKind kind = (LayoutOverflowKind)index;

        RoboCheckEqInt(LayoutOverflowKindIsKnown(kind), 1);
        RoboCheck(LayoutOverflowKindName(kind) != NULL);
    }
    RoboCheckEqInt(LayoutOverflowKindIsKnown((LayoutOverflowKind)3), 0);
    RoboCheckEqInt(LayoutOverflowKindIsKnown((LayoutOverflowKind)77), 0);
    RoboCheckStrEq(LayoutOverflowKindName(RoboLayoutOverflowAbsent), "absent");
    RoboCheckStrEq(LayoutOverflowKindName(RoboLayoutOverflowResolved), "resolved");
    RoboCheckStrEq(LayoutOverflowKindName(RoboLayoutOverflowUnresolved),
                      "unresolved");
    RoboCheckStrEq(LayoutOverflowKindName((LayoutOverflowKind)3), "unknown");
    RoboCheck(LayoutOverflowKindName((LayoutOverflowKind)77) != NULL);

    /* And the domain this phase added, spelled and counted: 24 since 19.1 appended its
     * own, with `overflow` the twentieth -- the first domain in this module about what
     * happens when the box was the wrong size. */
    RoboCheckEqInt((int)RoboLayoutDomainCount, 24);
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainOverflow),
                      "overflow");
    RoboCheckEqInt((int)RoboLayoutDomainOverflow,
                      (int)RoboLayoutDomainInvalidation - 1);
    RoboCheckEqInt((int)RoboLayoutDomainPosition,
                      (int)RoboLayoutDomainOverflow - 1);
}

static void TestOverflowAxisVocabulary(void)
{
    size_t index;

    LayoutFixture();

    /* Two, and named for the direction rather than for a letter, because every door
     * takes one and a build that indexed the pair by 0 and 1 would make `clips(1)` a
     * question about the document's writing conventions. */
    RoboCheckEqInt((int)RoboLayoutOverflowAxisCount, 2);
    RoboCheckEqInt((int)RoboLayoutOverflowAxisHorizontal, 0);
    RoboCheckEqInt((int)RoboLayoutOverflowAxisVertical, 1);

    for (index = 0u; index < RoboLayoutOverflowAxisCount; index++) {
        LayoutOverflowAxisId axis = (LayoutOverflowAxisId)index;

        RoboCheckEqInt(LayoutOverflowAxisIsKnown(axis), 1);
        RoboCheck(LayoutOverflowAxisName(axis) != NULL);
    }
    RoboCheckEqInt(LayoutOverflowAxisIsKnown((LayoutOverflowAxisId)2), 0);
    RoboCheckEqInt(LayoutOverflowAxisIsKnown((LayoutOverflowAxisId)91), 0);
    RoboCheckStrEq(LayoutOverflowAxisName(RoboLayoutOverflowAxisHorizontal),
                      "horizontal");
    RoboCheckStrEq(LayoutOverflowAxisName(RoboLayoutOverflowAxisVertical),
                      "vertical");
    RoboCheckStrEq(LayoutOverflowAxisName((LayoutOverflowAxisId)2),
                      "unknown");
    RoboCheck(LayoutOverflowAxisName((LayoutOverflowAxisId)91) != NULL);
}

static void TestOverflowAxisRecord(void)
{
    LayoutOverflowAxis absent = LayoutOverflowAxisAbsent();
    LayoutOverflowAxis visible =
        LayoutOverflowAxisResolved(RoboCssOverflowVisible);
    LayoutOverflowAxis hidden =
        LayoutOverflowAxisResolved(RoboCssOverflowHidden);
    LayoutOverflowAxis unresolved = LayoutOverflowAxisUnresolved();
    LayoutOverflowAxis sameAbsent = LayoutOverflowAxisAbsent();
    LayoutOverflowAxis nonsensical;
    CssOverflow mode = RoboCssOverflowHidden;

    LayoutFixture();

    RoboCheckEqInt(LayoutOverflowAxisIsAbsent(&absent), 1);
    RoboCheckEqInt(LayoutOverflowAxisIsResolved(&absent), 0);
    RoboCheckEqInt(LayoutOverflowAxisIsUnresolved(&absent), 0);
    RoboCheckEqInt(LayoutOverflowAxisIsValid(&absent), 1);

    RoboCheckEqInt(LayoutOverflowAxisIsAbsent(&visible), 0);
    RoboCheckEqInt(LayoutOverflowAxisIsResolved(&visible), 1);
    RoboCheckEqInt(LayoutOverflowAxisIsValid(&visible), 1);

    RoboCheckEqInt(LayoutOverflowAxisIsResolved(&unresolved), 0);
    RoboCheckEqInt(LayoutOverflowAxisIsUnresolved(&unresolved), 1);
    RoboCheckEqInt(LayoutOverflowAxisIsValid(&unresolved), 1);

    /* The mode door refuses the two kinds whose `mode` field means nothing, and it
     * refuses them with NOT_FOUND rather than by handing back the `visible` they happen
     * to hold -- which is a different statement from a mode that *is* `visible`. */
    RoboCheckEqInt(LayoutOverflowAxisMode(&hidden, &mode), RoboOk);
    RoboCheckEqInt((int)mode, (int)RoboCssOverflowHidden);
    mode = RoboCssOverflowHidden;
    RoboCheckEqInt(LayoutOverflowAxisMode(&absent, &mode), RoboNotFound);
    RoboCheckEqInt((int)mode, (int)RoboCssOverflowHidden); /* untouched */
    RoboCheckEqInt(LayoutOverflowAxisMode(&unresolved, &mode), RoboNotFound);
    RoboCheckEqInt((int)mode, (int)RoboCssOverflowHidden);
    RoboCheckEqInt(LayoutOverflowAxisMode(NULL, &mode), RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowAxisMode(&hidden, NULL), RoboInvalidArgument);

    /* A *kind* is what makes two records equal in the two states where the mode does
     * not mean anything: an absent axis equals another absent axis whatever stale
     * `mode` either holds. */
    RoboCheckEqInt(LayoutOverflowAxisEqual(&absent, &sameAbsent), 1);
    RoboCheckEqInt(LayoutOverflowAxisEqual(&absent, &visible), 0);
    RoboCheckEqInt(LayoutOverflowAxisEqual(&visible, &hidden), 0);
    RoboCheckEqInt(LayoutOverflowAxisEqual(&visible, &visible), 1);
    RoboCheckEqInt(LayoutOverflowAxisEqual(&unresolved, &unresolved), 1);
    RoboCheckEqInt(LayoutOverflowAxisEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutOverflowAxisEqual(&absent, NULL), 0);

    /* A mode nothing defined is stored as `visible` rather than refused, and the
     * record is still a record: the door that catches a hand-built one is *_is_valid. */
    nonsensical = LayoutOverflowAxisResolved((CssOverflow)7);
    RoboCheckEqInt(LayoutOverflowAxisIsValid(&nonsensical), 1);
    RoboCheckEqInt(LayoutOverflowAxisMode(&nonsensical, &mode), RoboOk);
    RoboCheckEqInt((int)mode, (int)RoboCssOverflowVisible);

    /* And the record rules: a kind nothing defines, or a mode nothing defines, is a
     * record this build may not read. */
    nonsensical = visible;
    nonsensical.kind = (LayoutOverflowKind)9;
    RoboCheckEqInt(LayoutOverflowAxisIsValid(&nonsensical), 0);
    nonsensical = visible;
    nonsensical.mode = (CssOverflow)9;
    RoboCheckEqInt(LayoutOverflowAxisIsValid(&nonsensical), 0);
    RoboCheckEqInt(LayoutOverflowAxisIsValid(NULL), 0);
    RoboCheckEqInt(LayoutOverflowAxisIsAbsent(NULL), 0);
    RoboCheckEqInt(LayoutOverflowAxisIsResolved(NULL), 0);
    RoboCheckEqInt(LayoutOverflowAxisIsUnresolved(NULL), 0);
}

static void TestOverflowDeclaredRecord(void)
{
    LayoutOverflow declared;
    LayoutOverflow other;
    LayoutOverflowAxis axis;
    LayoutOverflowAxis fallback;

    LayoutFixture();

    LayoutOverflowInit(&declared);
    RoboCheckEqInt(LayoutOverflowIsValid(&declared), 1);
    RoboCheckEqInt(LayoutOverflowAxisIsAbsent(&declared.horizontal), 1);
    RoboCheckEqInt(LayoutOverflowAxisIsAbsent(&declared.vertical), 1);

    LayoutOverflowInit(&other);
    RoboCheckEqInt(LayoutOverflowEqual(&declared, &other), 1);
    other.horizontal = LayoutOverflowAxisResolved(RoboCssOverflowHidden);
    RoboCheckEqInt(LayoutOverflowEqual(&declared, &other), 0);
    RoboCheckEqInt(LayoutOverflowEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutOverflowEqual(&declared, NULL), 0);

    RoboCheckEqInt(LayoutOverflowAxisOf(&other,
                                                   RoboLayoutOverflowAxisHorizontal, &axis),
                      RoboOk);
    RoboCheckEqInt(LayoutOverflowAxisIsResolved(&axis), 1);
    RoboCheckEqInt(LayoutOverflowAxisOf(&other, RoboLayoutOverflowAxisVertical,
                                                   &axis),
                      RoboOk);
    RoboCheckEqInt(LayoutOverflowAxisIsAbsent(&axis), 1);
    fallback = axis;
    RoboCheckEqInt(LayoutOverflowAxisOf(&other, (LayoutOverflowAxisId)5,
                                                   &axis),
                      RoboNotFound);
    RoboCheckEqInt(LayoutOverflowAxisEqual(&axis, &fallback), 1); /* untouched */
    RoboCheckEqInt(LayoutOverflowAxisOf(NULL, RoboLayoutOverflowAxisHorizontal,
                                                   &axis),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowAxisOf(&other,
                                                   RoboLayoutOverflowAxisHorizontal, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowIsValid(NULL), 0);

    /* A NULL style declares nothing: "no style" and "no entry" are one case, exactly
     * as they are for `display`. */
    declared = LayoutOverflowOf(NULL);
    RoboCheckEqInt(LayoutOverflowAxisIsAbsent(&declared.horizontal), 1);
    RoboCheckEqInt(LayoutOverflowAxisIsAbsent(&declared.vertical), 1);
}

static void TestOverflowEdgesRecord(void)
{
    LayoutOverflowEdges edges;
    LayoutOverflowEdges other;
    LayoutOverflowEdges bad;
    int32_t extent = 0;

    LayoutFixture();

    LayoutOverflowEdgesInit(&edges);
    /*
     * The cleared record is a *real* box -- `overflow: visible` with nothing sticking
     * out is the most common box in every document -- which is why the zero of the
     * mode vocabulary is a mode rather than a "no answer" enumerator.
     */
    RoboCheckEqInt((int)edges.horizontal, (int)RoboCssOverflowVisible);
    RoboCheckEqInt((int)edges.vertical, (int)RoboCssOverflowVisible);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.top, 0);
    RoboCheckEqInt(edges.right, 0);
    RoboCheckEqInt(edges.bottom, 0);
    RoboCheckEqInt(LayoutOverflowAxisCountOf(&edges), 0);
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(&edges), 1);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflow(&edges), 0);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflowAxis(
                          &edges, RoboLayoutOverflowAxisHorizontal),
                      0);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflowAxis(
                          &edges, RoboLayoutOverflowAxisVertical),
                      0);
    RoboCheckEqInt(LayoutOverflowEdgesClipsAny(&edges), 0);
    RoboCheckEqInt(LayoutOverflowEdgesClips(
                          &edges, RoboLayoutOverflowAxisHorizontal),
                      0);
    RoboCheckEqInt((int)LayoutOverflowEdgesMode(
                          &edges, RoboLayoutOverflowAxisHorizontal),
                      (int)RoboCssOverflowVisible);
    /* An unknown axis answers `visible`, for the reason an unknown overflow value
     * answers "does not clip": a record nothing can read must not throw contents away. */
    RoboCheckEqInt((int)LayoutOverflowEdgesMode(
                          &edges, (LayoutOverflowAxisId)3),
                      (int)RoboCssOverflowVisible);
    RoboCheckEqInt(LayoutOverflowEdgesClips(&edges,
                                                       (LayoutOverflowAxisId)3),
                      0);
    RoboCheckEqInt((int)LayoutOverflowEdgesMode(
                          NULL, RoboLayoutOverflowAxisVertical),
                      (int)RoboCssOverflowVisible);

    RoboCheckEqInt(LayoutOverflowEdgesExtent(&edges, RoboCssBoxSideTop, &extent),
                      RoboOk);
    RoboCheckEqInt(extent, 0);
    RoboCheckEqInt(LayoutOverflowEdgesExtent(&edges, RoboCssBoxSideRight,
                                                        &extent),
                      RoboOk);
    RoboCheckEqInt(LayoutOverflowEdgesExtent(&edges, RoboCssBoxSideBottom,
                                                        &extent),
                      RoboOk);
    RoboCheckEqInt(LayoutOverflowEdgesExtent(&edges, RoboCssBoxSideLeft, &extent),
                      RoboOk);
    RoboCheckEqInt(LayoutOverflowEdgesExtent(&edges, (CssBoxSide)9, &extent),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowEdgesExtent(NULL, RoboCssBoxSideTop, &extent),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowEdgesExtent(&edges, RoboCssBoxSideTop, NULL),
                      RoboInvalidArgument);

    other = edges;
    RoboCheckEqInt(LayoutOverflowEdgesEqual(&edges, &other), 1);
    RoboCheckEqInt(LayoutOverflowEdgesEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutOverflowEdgesEqual(&edges, NULL), 0);
    other.right = 1;
    RoboCheckEqInt(LayoutOverflowEdgesEqual(&edges, &other), 0);
    other = edges;
    other.horizontal = RoboCssOverflowHidden;
    RoboCheckEqInt(LayoutOverflowEdgesEqual(&edges, &other), 0);
    other = edges;
    other.axesResolved = 1u;
    /* The *counters* are part of the record: two conclusions that agreed about the
     * geometry and disagreed about how many declarations they used would be two
     * different answers to one document. */
    RoboCheckEqInt(LayoutOverflowEdgesEqual(&edges, &other), 0);

    other = edges;
    other.right = 12;
    other.bottom = 7;
    other.horizontal = RoboCssOverflowHidden;
    other.axesResolved = 1u;
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(&other), 1);
    RoboCheckEqInt(LayoutOverflowEdgesClipsAny(&other), 1);
    RoboCheckEqInt(LayoutOverflowEdgesClips(&other,
                                                       RoboLayoutOverflowAxisVertical),
                      0);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflow(&other), 1);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflowAxis(
                          &other, RoboLayoutOverflowAxisHorizontal),
                      1);
    /* Both extents are above zero, so both verbs are verbs about this record. */
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflowAxis(
                          &other, RoboLayoutOverflowAxisVertical),
                      1);

    /* Then the three records this build may not read, each for its own reason. */
    bad = other;
    bad.left = -1;
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(&bad), 0);
    bad = other;
    bad.vertical = (CssOverflow)9;
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(&bad), 0);
    bad = other;
    bad.axesResolved = 5u;
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(&bad), 0);
    bad = other;
    bad.axesResolved = 1u;
    bad.axesUnresolved = 2u;
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(&bad), 0);
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(NULL), 0);
}

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

static void TestOverflowSeamDeclarations(void)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    LayoutOverflow declared;
    LayoutOverflowAxis axis;
    CssOverflow mode = RoboCssOverflowVisible;

    LayoutFixture();

    /* Nothing declared: two undeclared axes, which is CSS's initial state and the
     * state in which `visible` applies because it applies rather than because
     * somebody wrote it. */
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    declared = LayoutOverflowOf(style);
    RoboCheckEqInt(LayoutOverflowAxisIsAbsent(&declared.horizontal), 1);
    RoboCheckEqInt(LayoutOverflowAxisIsAbsent(&declared.vertical), 1);

    /* `overflow: hidden` -- the **shorthand**, which this build reads rather than
     * expands. One declaration, two axes; that is the whole of its shorthand handling
     * and the reason the fallback lives in the reader rather than at three call
     * sites. */
    RoboCheckEqInt(CssComputedValueMakeOverflow(&value, RoboCssOverflowHidden,
                                                            NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("overflow"), 0u,
                                                       &specified, &value, NULL),
                      RoboOk);
    declared = LayoutOverflowOf(style);
    RoboCheckEqInt(LayoutOverflowAxisIsResolved(&declared.horizontal), 1);
    RoboCheckEqInt(LayoutOverflowAxisIsResolved(&declared.vertical), 1);
    RoboCheckEqInt(LayoutOverflowAxisMode(&declared.horizontal, &mode), RoboOk);
    RoboCheckEqInt((int)mode, (int)RoboCssOverflowHidden);
    RoboCheckEqInt(LayoutOverflowAxisMode(&declared.vertical, &mode), RoboOk);
    RoboCheckEqInt((int)mode, (int)RoboCssOverflowHidden);
    CssComputedStyleDestroy(style);

    /* `overflow-x: visible` beside the same shorthand. The longhand speaks for its own
     * axis and the shorthand is consulted only for the other one -- which is the case
     * a build that fell back on a *resolved* `visible` would get wrong by clipping
     * both axes. */
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeOverflow(&value, RoboCssOverflowVisible,
                                                            NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("overflow-x"), 0u,
                                                       &specified, &value, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueMakeOverflow(&value, RoboCssOverflowHidden,
                                                            NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("overflow"), 0u,
                                                       &specified, &value, NULL),
                      RoboOk);
    declared = LayoutOverflowOf(style);
    RoboCheckEqInt(LayoutOverflowAxisMode(&declared.horizontal, &mode), RoboOk);
    RoboCheckEqInt((int)mode, (int)RoboCssOverflowVisible);
    RoboCheckEqInt(LayoutOverflowAxisMode(&declared.vertical, &mode), RoboOk);
    RoboCheckEqInt((int)mode, (int)RoboCssOverflowHidden);
    CssComputedStyleDestroy(style);

    /* `overflow-y` alone: the other axis stays undeclared rather than becoming
     * `visible`-as-a-declaration. */
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeOverflow(&value, RoboCssOverflowHidden,
                                                            NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("overflow-y"), 0u,
                                                       &specified, &value, NULL),
                      RoboOk);
    declared = LayoutOverflowOf(style);
    RoboCheckEqInt(LayoutOverflowAxisIsAbsent(&declared.horizontal), 1);
    RoboCheckEqInt(LayoutOverflowAxisMode(&declared.vertical, &mode), RoboOk);
    RoboCheckEqInt((int)mode, (int)RoboCssOverflowHidden);
    CssComputedStyleDestroy(style);

    /*
     * And `overflow-x: scroll` -- a real CSS overflow value this build has not added.
     * It arrives as a *keyword*, which the overflow reader cannot classify, so the axis
     * is `UNRESOLVED` and its used mode is `visible`. Counted rather than dropped: a
     * document whose clipping depends on one should be visible as such.
     */
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeKeyword(
                          &value, CssSliceFromText("scroll"), NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("overflow-x"), 0u,
                                                       &specified, &value, NULL),
                      RoboOk);
    declared = LayoutOverflowOf(style);
    RoboCheckEqInt(LayoutOverflowAxisIsUnresolved(&declared.horizontal), 1);
    RoboCheckEqInt(LayoutOverflowAxisIsAbsent(&declared.vertical), 1);
    RoboCheckEqInt(LayoutOverflowAxisMode(&declared.horizontal, &mode),
                      RoboNotFound);
    RoboCheckEqInt(LayoutOverflowAxisOf(&declared,
                                                   RoboLayoutOverflowAxisHorizontal, &axis),
                      RoboOk);
    RoboCheckEqInt(LayoutOverflowAxisIsUnresolved(&axis), 1);
    CssComputedStyleDestroy(style);
}

/* ------------------------------------------------------------------ */
/* The one policy                                                      */
/* ------------------------------------------------------------------ */

/** A declared record: an axis kind per side, built from the vocabulary's own makers. */
static LayoutOverflow LayoutOverflowDeclaredOf(LayoutOverflowAxis horizontal,
                                                          LayoutOverflowAxis vertical)
{
    LayoutOverflow declared;

    declared.horizontal = horizontal;
    declared.vertical = vertical;
    return declared;
}

static void TestOverflowResolveModes(void)
{
    LayoutOverflow declared;
    LayoutOverflowEdges edges;
    LayoutDiag diag = RoboLayoutCleared;

    LayoutFixture();

    /* Nobody declared anything: two `visible` axes, no counter moved, no extents. This
     * is the most common box in every document and it is a *stated* answer. */
    declared = LayoutOverflowDeclaredOf(LayoutOverflowAxisAbsent(),
                                          LayoutOverflowAxisAbsent());
    RoboCheckEqInt(LayoutOverflowResolve(&declared, &edges, &diag), RoboOk);
    RoboCheckEqInt((int)edges.horizontal, (int)RoboCssOverflowVisible);
    RoboCheckEqInt((int)edges.vertical, (int)RoboCssOverflowVisible);
    RoboCheckEqInt(edges.axesResolved, 0u);
    RoboCheckEqInt(edges.axesUnresolved, 0u);
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(&edges), 1);

    /* An axis declared `visible` is a declaration an author wrote, and it is counted:
     * "the author wrote nothing" and "the author wrote `visible`" are two declarations
     * and the counters are where the difference lives. */
    declared = LayoutOverflowDeclaredOf(
        LayoutOverflowAxisResolved(RoboCssOverflowVisible),
        LayoutOverflowAxisAbsent());
    RoboCheckEqInt(LayoutOverflowResolve(&declared, &edges, &diag), RoboOk);
    RoboCheckEqInt(edges.axesResolved, 1u);
    RoboCheckEqInt(edges.axesUnresolved, 0u);
    RoboCheckEqInt((int)edges.horizontal, (int)RoboCssOverflowVisible);

    declared = LayoutOverflowDeclaredOf(
        LayoutOverflowAxisResolved(RoboCssOverflowHidden),
        LayoutOverflowAxisResolved(RoboCssOverflowHidden));
    RoboCheckEqInt(LayoutOverflowResolve(&declared, &edges, &diag), RoboOk);
    RoboCheckEqInt(edges.axesResolved, 2u);
    RoboCheckEqInt(edges.axesUnresolved, 0u);
    RoboCheckEqInt(edges.axesResolved + edges.axesUnresolved,
                      (size_t)RoboLayoutOverflowAxisCount);
    RoboCheckEqInt(LayoutOverflowEdgesClipsAny(&edges), 1);
    RoboCheckEqInt(LayoutOverflowEdgesClips(&edges,
                                                       RoboLayoutOverflowAxisHorizontal),
                      1);
    RoboCheckEqInt(LayoutOverflowEdgesClips(&edges,
                                                       RoboLayoutOverflowAxisVertical),
                      1);

    /* The mixture: one axis resolved, the other a declaration this build cannot use.
     * Its used mode is `visible` -- 10.8's rule for a dimension nothing can resolve --
     * and it is counted so that it is not invisible in the record. */
    declared = LayoutOverflowDeclaredOf(
        LayoutOverflowAxisResolved(RoboCssOverflowHidden),
        LayoutOverflowAxisUnresolved());
    RoboCheckEqInt(LayoutOverflowResolve(&declared, &edges, &diag), RoboOk);
    RoboCheckEqInt(edges.axesResolved, 1u);
    RoboCheckEqInt(edges.axesUnresolved, 1u);
    RoboCheckEqInt((int)edges.horizontal, (int)RoboCssOverflowHidden);
    RoboCheckEqInt((int)edges.vertical, (int)RoboCssOverflowVisible);
    RoboCheckEqInt(LayoutOverflowEdgesClipsAny(&edges), 1);
    RoboCheckEqInt(LayoutOverflowEdgesClips(&edges,
                                                       RoboLayoutOverflowAxisVertical),
                      0);

    /* And a mode nothing defines cannot leak through: the resolve reads it as
     * `visible` because it is not a mode this build has. */
    declared = LayoutOverflowDeclaredOf(
        LayoutOverflowAxisResolved((CssOverflow)6),
        LayoutOverflowAxisResolved((CssOverflow)6));
    RoboCheckEqInt(LayoutOverflowResolve(&declared, &edges, &diag), RoboOk);
    RoboCheckEqInt(edges.axesResolved, 2u);
    RoboCheckEqInt((int)edges.horizontal, (int)RoboCssOverflowVisible);
    RoboCheckEqInt((int)edges.vertical, (int)RoboCssOverflowVisible);
    RoboCheckEqInt(LayoutOverflowEdgesClipsAny(&edges), 0);

    RoboCheckEqInt(LayoutOverflowResolve(NULL, &edges, &diag), RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowResolve(&declared, NULL, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowResolve(&declared, &edges, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* The accumulation rule                                               */
/* ------------------------------------------------------------------ */

static LayoutRect LayoutOverflowRect(int32_t x, int32_t y, int32_t width,
                                               int32_t height)
{
    LayoutRect rect;

    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    return rect;
}

static void TestOverflowIncludeExtents(void)
{
    LayoutOverflowEdges edges;
    LayoutOverflowEdges before;
    LayoutRect paddingBox = LayoutOverflowRect(0, 0, 100, 50);

    LayoutFixture();
    LayoutOverflowEdgesInit(&edges);

    /* A child that reaches right and below: each side is its own number, and each one
     * is how far *past* the edge the region got. */
    RoboCheckEqInt(LayoutOverflowEdgesInclude(
                          &edges, paddingBox, LayoutOverflowRect(0, 0, 300, 80), NULL),
                      RoboOk);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.top, 0);
    RoboCheckEqInt(edges.right, 200);
    RoboCheckEqInt(edges.bottom, 30);

    /* And a child that reaches the other way, from the far edges rather than from a
     * width: the region's own coordinates place it. This one reaches left and no other
     * way, and that is the per-axis half of the same statement. */
    LayoutOverflowEdgesInit(&edges);
    RoboCheckEqInt(LayoutOverflowEdgesInclude(
                          &edges, paddingBox, LayoutOverflowRect(-20, 10, 50, 20), NULL),
                      RoboOk);
    RoboCheckEqInt(edges.left, 20);
    RoboCheckEqInt(edges.top, 0);
    RoboCheckEqInt(edges.right, 0);
    RoboCheckEqInt(edges.bottom, 0);
    /* The extents are a measurement, and a measurement of a region that stayed inside
     * is zero rather than negative: contents that reached *inside* the box are a
     * different statement from contents that reached past it. */
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflow(&edges), 1);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflowAxis(
                          &edges, RoboLayoutOverflowAxisHorizontal),
                      1);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflowAxis(
                          &edges, RoboLayoutOverflowAxisVertical),
                      0);

    /* A **maximum**, not a union, and the difference is the second fold: the outermost
     * thing any of a box's contents reached, not the last one measured. */
    LayoutOverflowEdgesInit(&edges);
    RoboCheckEqInt(LayoutOverflowEdgesInclude(
                          &edges, paddingBox, LayoutOverflowRect(0, 0, 150, 50), NULL),
                      RoboOk);
    RoboCheckEqInt(edges.right, 50);
    before = edges;
    RoboCheckEqInt(LayoutOverflowEdgesInclude(
                          &edges, paddingBox, LayoutOverflowRect(0, 0, 120, 50), NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutOverflowEdgesEqual(&edges, &before), 1);

    /* Idempotent for a region that was already inside, which is the same rule from the
     * other side. */
    RoboCheckEqInt(LayoutOverflowEdgesInclude(
                          &edges, paddingBox, LayoutOverflowRect(10, 10, 20, 20), NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutOverflowEdgesEqual(&edges, &before), 1);

    /* The refusals: no record, a negative size, and a record that is not one. */
    RoboCheckEqInt(LayoutOverflowEdgesInclude(
                          NULL, paddingBox, LayoutOverflowRect(0, 0, 10, 10), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowEdgesInclude(
                          &edges, paddingBox, LayoutOverflowRect(0, 0, -1, 10), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowEdgesInclude(
                          &edges, LayoutOverflowRect(0, 0, 10, -1),
                          LayoutOverflowRect(0, 0, 10, 10), NULL),
                      RoboInvalidArgument);
    before = edges;
    RoboCheckEqInt(LayoutOverflowEdgesInclude(
                          &edges, paddingBox, LayoutOverflowRect(0, 0, 900, 900), NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutOverflowEdgesEqual(&edges, &before), 0);
    edges.axesResolved = 9u;
    RoboCheckEqInt(LayoutOverflowEdgesInclude(
                          &edges, paddingBox, LayoutOverflowRect(0, 0, 10, 10), NULL),
                      RoboError);
}

static void TestOverflowIncludeSaturates(void)
{
    LayoutOverflowEdges edges;
    LayoutRect paddingBox = LayoutOverflowRect(0, 0, 1, 1);

    LayoutFixture();
    LayoutOverflowEdgesInit(&edges);

    /*
     * The terms are each bounded by the seam's maximum and a sum of two of them is not,
     * so the four maxima are performed in 64 bits and clamped. A box at the seam's
     * maximum reached by a child at the seam's maximum gets a maximum -- never a
     * wrapped negative, which is the failure this door exists to make impossible.
     */
    RoboCheckEqInt(LayoutOverflowEdgesInclude(
                          &edges, paddingBox,
                          LayoutOverflowRect(-2147483647, -2147483647, 1, 1), NULL),
                      RoboOk);
    RoboCheckEqInt(edges.left, 2147483647);
    RoboCheckEqInt(edges.top, 2147483647);
    RoboCheckEqInt(edges.left > 0, 1);
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(&edges), 1);

    LayoutOverflowEdgesInit(&edges);
    RoboCheckEqInt(LayoutOverflowEdgesInclude(
                          &edges, paddingBox,
                          LayoutOverflowRect(2147483646, 2147483646, 2147483647,
                                               2147483647),
                          NULL),
                      RoboOk);
    RoboCheckEqInt(edges.right, 2147483647);
    RoboCheckEqInt(edges.bottom, 2147483647);
    RoboCheckEqInt(edges.right >= 0, 1);
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(&edges), 1);
}

/* ------------------------------------------------------------------ */
/* The region and the clip rectangle                                   */
/* ------------------------------------------------------------------ */

static void TestOverflowRegionPerAxis(void)
{
    LayoutOverflowEdges edges;
    LayoutRect paddingBox = LayoutOverflowRect(0, 0, 100, 50);
    LayoutRect region;

    LayoutFixture();
    LayoutOverflowEdgesInit(&edges);
    edges.right = 200;
    edges.bottom = 30;

    /* Both axes `visible`: the padding box grown by the extents -- what the contents
     * occupy in the end, and what a parent folds in. */
    RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, paddingBox, &region, NULL),
                      RoboOk);
    RoboCheckEqInt(region.x, 0);
    RoboCheckEqInt(region.y, 0);
    RoboCheckEqInt(region.width, 300);
    RoboCheckEqInt(region.height, 80);

    /*
     * Then one axis at a time, and this is the phase's recursion in three lines: on an
     * axis whose mode **clips**, the region is the padding box itself -- a box that
     * hides an axis keeps what it hides -- while the *record* still says how far the
     * contents reached. Two consumers, two answers, one record.
     */
    edges.horizontal = RoboCssOverflowHidden;
    RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, paddingBox, &region, NULL),
                      RoboOk);
    RoboCheckEqInt(region.width, 100);
    RoboCheckEqInt(region.height, 80);
    RoboCheckEqInt(edges.right, 200); /* untouched: the record is the measurement */

    edges.horizontal = RoboCssOverflowVisible;
    edges.vertical = RoboCssOverflowHidden;
    RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, paddingBox, &region, NULL),
                      RoboOk);
    RoboCheckEqInt(region.width, 300);
    RoboCheckEqInt(region.height, 50);

    edges.horizontal = RoboCssOverflowHidden;
    RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, paddingBox, &region, NULL),
                      RoboOk);
    RoboCheckEqInt(region.width, 100);
    RoboCheckEqInt(region.height, 50);

    /* A record with no overflow and two `visible` axes answers the padding box, which
     * is a legitimate region and not an error: the commonest box in a document. */
    LayoutOverflowEdgesInit(&edges);
    RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, paddingBox, &region, NULL),
                      RoboOk);
    RoboCheckEqInt(region.width, 100);
    RoboCheckEqInt(region.height, 50);

    RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, paddingBox, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, LayoutOverflowRect(0, 0, -1, 1),
                                                        &region, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowEdgesRegion(NULL, paddingBox, &region, NULL),
                      RoboInvalidArgument);
    edges.axesUnresolved = 4u;
    RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, paddingBox, &region, NULL),
                      RoboError);
}

static void TestOverflowClipRectRefusals(void)
{
    LayoutOverflowEdges edges;
    LayoutRect paddingBox = LayoutOverflowRect(3, 7, 100, 50);
    LayoutRect clip = LayoutOverflowRect(-1, -1, -1, -1);
    LayoutRect untouched = clip;

    LayoutFixture();
    LayoutOverflowEdgesInit(&edges);

    /*
     * A box with no boundary has **no clip rectangle**, and the door refuses rather
     * than answering the padding box anyway -- because answering it would tell a paint
     * pass to clip a box that paints its contents outside itself, which is the failure
     * this phase exists to prevent arriving from the other side.
     */
    RoboCheckEqInt(LayoutOverflowEdgesClipRect(&edges, paddingBox, &clip, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutOverflowEdgesEqual(&edges, &edges), 1);
    RoboCheckEqInt(clip.width, untouched.width);
    RoboCheckEqInt(clip.x, untouched.x);

    edges.horizontal = RoboCssOverflowHidden;
    RoboCheckEqInt(LayoutOverflowEdgesClipRect(&edges, paddingBox, &clip, NULL),
                      RoboOk);
    /* The padding box and nothing else: CSS's own boundary for `overflow: hidden`, so
     * the door neither shrinks nor grows it. */
    RoboCheckEqInt(clip.x, 3);
    RoboCheckEqInt(clip.y, 7);
    RoboCheckEqInt(clip.width, 100);
    RoboCheckEqInt(clip.height, 50);

    edges.vertical = RoboCssOverflowHidden;
    RoboCheckEqInt(LayoutOverflowEdgesClipRect(&edges, paddingBox, &clip, NULL),
                      RoboOk);
    RoboCheckEqInt(clip.width, 100);

    RoboCheckEqInt(LayoutOverflowEdgesClipRect(NULL, paddingBox, &clip, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowEdgesClipRect(&edges, paddingBox, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutOverflowEdgesClipRect(&edges,
                                                           LayoutOverflowRect(0, 0, 1, -1),
                                                           &clip, NULL),
                      RoboInvalidArgument);
    edges.axesResolved = 3u;
    RoboCheckEqInt(LayoutOverflowEdgesClipRect(&edges, paddingBox, &clip, NULL),
                      RoboError);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

/**
 * `html` and `body` as blocks, then @p rules -- the fixture shape every pass case here
 * shares.
 *
 * Both are written out rather than left to the builder, because a box with no style
 * computes to `display: inline` (9.12's initial value) and a document made of inline
 * boxes is not the document any of these cases is about.
 */
static void LayoutOverflowBlocks(LayoutOverflowRule *htmlRule,
                                  LayoutOverflowRule *bodyRule)
{
    LayoutOverflowRuleInit(htmlRule, "html", RoboCssDisplayBlock);
    LayoutOverflowRuleInit(bodyRule, "body", RoboCssDisplayBlock);
}

static void TestOverflowPassFitting(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule divRule;
    LayoutOverflowRule rules[3];
    LayoutStats stats;
    LayoutNodeId id;
    LayoutOverflowEdges edges;
    LayoutResult result;

    LayoutFixture();

    /* A box whose contents fit: four zeroes, two `visible` axes, no declaration
     * counted, and two flags that are not set. The commonest case, and a *stated*
     * answer rather than an absent one. */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&divRule, "width", 100.0);
    LayoutOverflowRulePx(&divRule, "height", 50.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutOverflowBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, &stats);

    id = LayoutOverflowBoxFor(&fixture, "div");
    edges = LayoutOverflowEdgesOf(&fixture, id);
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(&edges), 1);
    RoboCheckEqInt((int)edges.horizontal, (int)RoboCssOverflowVisible);
    RoboCheckEqInt((int)edges.vertical, (int)RoboCssOverflowVisible);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.top, 0);
    RoboCheckEqInt(edges.right, 0);
    RoboCheckEqInt(edges.bottom, 0);
    RoboCheckEqInt(edges.axesResolved, 0u);
    RoboCheckEqInt(edges.axesUnresolved, 0u);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflow(&edges), 0);
    RoboCheckEqInt(LayoutOverflowEdgesClipsAny(&edges), 0);

    result = LayoutOverflowResultOf(&fixture, id);
    RoboCheckEqInt(LayoutResultFlags(&result), 0u);
    RoboCheckEqInt(LayoutResultHasFlag(
                          &result, RoboLayoutResultFlagOverflowX),
                      0);
    RoboCheckEqInt(LayoutResultHasFlag(
                          &result, RoboLayoutResultFlagOverflowY),
                      0);
    /* And the clip door agrees with the flags: there is nothing to clip to. */
    {
        LayoutRect clip;

        RoboCheckEqInt(LayoutOverflowEdgesClipRect(
                              &edges, LayoutOverflowPaddingBoxOf(&fixture, id), &clip, NULL),
                          RoboNotFound);
    }

    RoboCheckEqInt(stats.block.overflowClipped, 0u);
    RoboCheckEqInt(stats.block.overflowing, 0u);
    RoboCheckEqInt(stats.block.overflowUnresolved, 0u);

    LayoutOverflowEnd(&fixture);
}

static void TestOverflowPassChildRight(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule outerRule;
    LayoutOverflowRule innerRule;
    LayoutOverflowRule rules[4];
    LayoutNodeId outer;
    LayoutNodeId inner;
    LayoutOverflowEdges edges;
    LayoutSize size;
    LayoutResult result;

    LayoutFixture();

    /* A child wider than the box that holds it, and the two numbers every case here
     * turns on: the child's region reaches 200 past the parent's padding box on the
     * right, and the *parent* is still 100 wide. */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&outerRule, "width", 100.0);
    LayoutOverflowRulePx(&outerRule, "height", 50.0);
    LayoutOverflowRuleInit(&innerRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&innerRule, "width", 300.0);
    LayoutOverflowRulePx(&innerRule, "height", 20.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = outerRule;
    rules[3] = innerRule;

    LayoutOverflowBegin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    inner = LayoutOverflowBoxFor(&fixture, "section");

    edges = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.top, 0);
    RoboCheckEqInt(edges.right, 200);
    RoboCheckEqInt(edges.bottom, 0);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflowAxis(
                          &edges, RoboLayoutOverflowAxisHorizontal),
                      1);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflowAxis(
                          &edges, RoboLayoutOverflowAxisVertical),
                      0);

    size = LayoutOverflowSizeOf(&fixture, outer);
    RoboCheckEqInt(size.width, 100);
    RoboCheckEqInt(size.height, 50);

    /* The child is a leaf: it kept its own geometry and measured nothing of its own. */
    size = LayoutOverflowSizeOf(&fixture, inner);
    RoboCheckEqInt(size.width, 300);
    RoboCheckEqInt(size.height, 20);
    edges = LayoutOverflowEdgesOf(&fixture, inner);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflow(&edges), 0);

    result = LayoutOverflowResultOf(&fixture, outer);
    RoboCheckEqInt(LayoutResultHasFlag(
                          &result, RoboLayoutResultFlagOverflowX),
                      1);
    RoboCheckEqInt(LayoutResultHasFlag(
                          &result, RoboLayoutResultFlagOverflowY),
                      0);

    /* And the region a parent would fold in: the padding box grown by the extent. The
     * `outer`'s record is re-read here rather than reused, because `edges` was
     * overwritten with the *child*'s above -- and a region computed from a leaf's
     * cleared record would be the padding box itself. */
    {
        LayoutOverflowEdges outerEdges = LayoutOverflowEdgesOf(&fixture, outer);
        LayoutRect region;

        RoboCheckEqInt(LayoutOverflowEdgesRegion(
                              &outerEdges, LayoutOverflowPaddingBoxOf(&fixture, outer),
                              &region, NULL),
                          RoboOk);
        RoboCheckEqInt(region.width, 300);
        RoboCheckEqInt(region.height, 50);
        RoboCheckEqInt(region.x, 0);
    }

    LayoutOverflowEnd(&fixture);
}

static void TestOverflowPassChildBelow(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule outerRule;
    LayoutOverflowRule innerRule;
    LayoutOverflowRule rules[4];
    LayoutNodeId outer;
    LayoutNodeId inner;
    LayoutOverflowEdges edges;
    LayoutResult result;

    LayoutFixture();

    /* The same fact the other way up: an explicit height smaller than the contents, and
     * a child that reaches 50 past the bottom edge. Nothing collapses and nothing
     * grows -- the box is 30 tall and says so. */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&outerRule, "width", 100.0);
    LayoutOverflowRulePx(&outerRule, "height", 30.0);
    LayoutOverflowRuleInit(&innerRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&innerRule, "height", 80.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = outerRule;
    rules[3] = innerRule;

    LayoutOverflowBegin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    inner = LayoutOverflowBoxFor(&fixture, "section");

    edges = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt(edges.right, 0);
    RoboCheckEqInt(edges.bottom, 50);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.top, 0);

    /* The child's `width: auto` fills the containing block, so it reaches neither side;
     * its height is the explicit one. */
    RoboCheckEqInt(LayoutOverflowSizeOf(&fixture, inner).width, 100);
    RoboCheckEqInt(LayoutOverflowSizeOf(&fixture, inner).height, 80);

    result = LayoutOverflowResultOf(&fixture, outer);
    RoboCheckEqInt(LayoutResultHasFlag(
                          &result, RoboLayoutResultFlagOverflowY),
                      1);
    RoboCheckEqInt(LayoutResultHasFlag(
                          &result, RoboLayoutResultFlagOverflowX),
                      0);

    LayoutOverflowEnd(&fixture);
}

static void TestOverflowPassSizeUnchanged(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule outerRule;
    LayoutOverflowRule innerRule;
    LayoutOverflowRule rules[4];
    LayoutNodeId outer;
    LayoutNodeId html;
    LayoutNodeId body;
    LayoutOverflowEdges edges;

    LayoutFixture();

    /*
     * The phase in one case, and the assertion a build that folded an overflow into a
     * size would fail: the box is 100 by 30 with 200 of contents sticking out to the
     * right and 50 below, **and its two ancestors are exactly as tall as the box's
     * margin box**. An overflow that reached flow would show up here as a taller `body`
     * and a taller `html`, and would look perfectly correct in every single-box test.
     */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&outerRule, "width", 100.0);
    LayoutOverflowRulePx(&outerRule, "height", 30.0);
    LayoutOverflowRuleInit(&innerRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&innerRule, "width", 300.0);
    LayoutOverflowRulePx(&innerRule, "height", 80.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = outerRule;
    rules[3] = innerRule;

    LayoutOverflowBegin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    body = LayoutOverflowBoxFor(&fixture, "body");
    html = LayoutOverflowBoxFor(&fixture, "html");

    RoboCheckEqInt(LayoutOverflowSizeOf(&fixture, outer).width, 100);
    RoboCheckEqInt(LayoutOverflowSizeOf(&fixture, outer).height, 30);
    edges = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt(edges.right, 200);
    RoboCheckEqInt(edges.bottom, 50);

    RoboCheckEqInt(LayoutOverflowSizeOf(&fixture, body).height, 30);
    RoboCheckEqInt(LayoutOverflowSizeOf(&fixture, html).height, 30);
    /*
     * And the *pair* of facts that separates an overflow from a size: the two ancestors
     * are 30 tall and their own records say 50 of contents reached past them, because the
     * `div`'s **region** -- its padding box grown by its extents, which is what a parent
     * folds in -- is 80 tall while the `div`'s *margin box* is 30. Flow stopped at the
     * margin box; the record did not.
     *
     * A build that had instead grown `body` to 80 would satisfy every assertion above it
     * here and be wrong, which is why both halves are asserted.
     */
    {
        LayoutOverflowEdges bodyEdges = LayoutOverflowEdgesOf(&fixture, body);
        LayoutOverflowEdges htmlEdges = LayoutOverflowEdgesOf(&fixture, html);

        RoboCheckEqInt(LayoutOverflowEdgesHasOverflow(&bodyEdges), 1);
        RoboCheckEqInt(bodyEdges.bottom, 50);
        RoboCheckEqInt(LayoutOverflowEdgesHasOverflow(&htmlEdges), 1);
        RoboCheckEqInt(htmlEdges.bottom, 50);
    }

    LayoutOverflowEnd(&fixture);
}

static void TestOverflowPassHiddenClips(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule outerRule;
    LayoutOverflowRule innerRule;
    LayoutOverflowRule rules[4];
    LayoutStats stats;
    LayoutNodeId outer;
    LayoutOverflowEdges edges;
    LayoutRect paddingBox;
    LayoutRect region;
    LayoutRect clip;

    LayoutFixture();

    /*
     * `overflow: hidden` records the same extent a visible box records -- the record is
     * the *measurement* -- and adds a boundary: the region becomes the padding box and
     * the clip rectangle exists. The flags are set from the extents, so a box that hides
     * what does not fit still says that something did not fit, which is the difference
     * between "the contents are too big" and "the contents are drawn outside".
     */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&outerRule, "width", 100.0);
    LayoutOverflowRulePx(&outerRule, "height", 30.0);
    LayoutOverflowRuleOverflow(&outerRule, "overflow", RoboCssOverflowHidden);
    LayoutOverflowRuleInit(&innerRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&innerRule, "height", 80.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = outerRule;
    rules[3] = innerRule;

    LayoutOverflowBegin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, &stats);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    edges = LayoutOverflowEdgesOf(&fixture, outer);

    RoboCheckEqInt((int)edges.horizontal, (int)RoboCssOverflowHidden);
    RoboCheckEqInt((int)edges.vertical, (int)RoboCssOverflowHidden);
    RoboCheckEqInt(edges.axesResolved, 2u);
    RoboCheckEqInt(edges.axesUnresolved, 0u);
    RoboCheckEqInt(edges.bottom, 50);
    RoboCheckEqInt(LayoutOverflowEdgesClipsAny(&edges), 1);
    {
        LayoutResult result = LayoutOverflowResultOf(&fixture, outer);

        RoboCheckEqInt(
            LayoutResultHasFlag(&result, RoboLayoutResultFlagOverflowY), 1);
    }

    paddingBox = LayoutOverflowPaddingBoxOf(&fixture, outer);
    RoboCheckEqInt(paddingBox.width, 100);
    RoboCheckEqInt(paddingBox.height, 30);

    RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, paddingBox, &region, NULL),
                      RoboOk);
    RoboCheckEqInt(region.width, 100);
    RoboCheckEqInt(region.height, 30);
    RoboCheckEqInt(LayoutOverflowEdgesClipRect(&edges, paddingBox, &clip, NULL),
                      RoboOk);
    RoboCheckEqInt(clip.width, 100);
    RoboCheckEqInt(clip.height, 30);

    /* A box that clips and a box that overflows are two different facts, and neither
     * implies the other: one box here clips, one box here overflows. */
    RoboCheckEqInt(stats.block.overflowClipped, 1u);
    RoboCheckEqInt(stats.block.overflowing, 1u);
    RoboCheckEqInt(stats.block.overflowUnresolved, 0u);

    LayoutOverflowEnd(&fixture);
}

static void TestOverflowPassAxisSplit(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule outerRule;
    LayoutOverflowRule innerRule;
    LayoutOverflowRule rules[4];
    LayoutNodeId outer;
    LayoutOverflowEdges edges;
    LayoutRect paddingBox;
    LayoutRect region;

    LayoutFixture();

    /*
     * `overflow-x: hidden; overflow-y: visible`, and the axis is never collapsed into
     * the pair: the region is 100 wide (clipped) and 80 tall (grown), which is a
     * rectangle neither mode could have produced alone.
     *
     * And no **axis promotion**: CSS promotes `visible` to `auto` when the other axis
     * scrolls, because a box cannot scroll one axis and paint outside itself on the
     * other -- a rule that exists for scrolling, which this build has none of. So
     * `visible` beside `hidden` stays `visible` here, and this case is what the phase
     * that adds scrolling changes.
     */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&outerRule, "width", 100.0);
    LayoutOverflowRulePx(&outerRule, "height", 50.0);
    LayoutOverflowRuleOverflow(&outerRule, "overflow-x", RoboCssOverflowHidden);
    LayoutOverflowRuleOverflow(&outerRule, "overflow-y", RoboCssOverflowVisible);
    LayoutOverflowRuleInit(&innerRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&innerRule, "width", 300.0);
    LayoutOverflowRulePx(&innerRule, "height", 80.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = outerRule;
    rules[3] = innerRule;

    LayoutOverflowBegin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    edges = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt((int)edges.horizontal, (int)RoboCssOverflowHidden);
    RoboCheckEqInt((int)edges.vertical, (int)RoboCssOverflowVisible);
    RoboCheckEqInt(edges.axesResolved, 2u);
    RoboCheckEqInt(edges.right, 200);
    RoboCheckEqInt(edges.bottom, 30);
    RoboCheckEqInt(LayoutOverflowEdgesClips(&edges,
                                                       RoboLayoutOverflowAxisHorizontal),
                      1);
    RoboCheckEqInt(LayoutOverflowEdgesClips(&edges,
                                                       RoboLayoutOverflowAxisVertical),
                      0);

    paddingBox = LayoutOverflowPaddingBoxOf(&fixture, outer);
    RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, paddingBox, &region, NULL),
                      RoboOk);
    RoboCheckEqInt(region.width, 100);
    RoboCheckEqInt(region.height, 80);

    LayoutOverflowEnd(&fixture);
}

static void TestOverflowPassRecursionClipped(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule outerRule;
    LayoutOverflowRule midRule;
    LayoutOverflowRule deepRule;
    LayoutOverflowRule rules[5];
    LayoutNodeId outer;
    LayoutNodeId mid;
    LayoutOverflowEdges edges;

    LayoutFixture();

    /*
     * Three levels and one declared mode, and this is the recursion the phase is built
     * around: a child contributes its **region**, not its extents. `mid` reaches 50 past
     * its own box in both runs -- its record never changes -- and what reaches `outer`
     * is 40 in the first run and nothing at all in the second, because a box that hides
     * an axis keeps what it hides.
     *
     * The alternative -- fold the child's contents into the parent and forget the child
     * clipped -- is how a clipped subtree comes to leak into an ancestor's scrollable
     * area.
     */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&outerRule, "height", 40.0);
    LayoutOverflowRuleInit(&midRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&midRule, "height", 30.0);
    LayoutOverflowRuleInit(&deepRule, "article", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&deepRule, "height", 80.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = outerRule;
    rules[3] = midRule;
    rules[4] = deepRule;

    LayoutOverflowBegin(&fixture,
                          "<html><body><div><section><article></article></section></div>"
                          "</body></html>",
                          rules, 5u);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    mid = LayoutOverflowBoxFor(&fixture, "section");

    edges = LayoutOverflowEdgesOf(&fixture, mid);
    RoboCheckEqInt(edges.bottom, 50);
    RoboCheckEqInt(LayoutOverflowEdgesClipsAny(&edges), 0);

    edges = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt(edges.bottom, 40);

    /* Then the one declaration that changes, and the ancestor follows it down. */
    LayoutOverflowRuleInit(&midRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&midRule, "height", 30.0);
    LayoutOverflowRuleOverflow(&midRule, "overflow", RoboCssOverflowHidden);
    LayoutOverflowStyleReplace(&fixture.source, "section", &midRule);
    LayoutOverflowRebuild(&fixture);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    mid = LayoutOverflowBoxFor(&fixture, "section");

    /* The measurement is unchanged: `mid` still reaches 50 past its own box. */
    edges = LayoutOverflowEdgesOf(&fixture, mid);
    RoboCheckEqInt(edges.bottom, 50);
    RoboCheckEqInt(LayoutOverflowEdgesClipsAny(&edges), 1);
    /* And what it *contributes* is now only what it kept: 30 against `outer`'s 40, so
     * nothing reaches past `outer` at all. */
    edges = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt(edges.bottom, 0);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflow(&edges), 0);

    LayoutOverflowEnd(&fixture);
}

static void TestOverflowPassRelativeChild(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule outerRule;
    LayoutOverflowRule innerRule;
    LayoutOverflowRule rules[4];
    LayoutNodeId outer;
    LayoutNodeId inner;
    LayoutOverflowEdges edges;
    LayoutPoint origin;
    LayoutPositionOffset offset;

    LayoutFixture();

    /*
     * A `relative` child, and the one thing 10.13 had to settle about 10.12: overflow is
     * measured against the **final** position, because that is where the contents are.
     * The child is 80 wide and displaced 40 to the right, so it reaches 20 past a
     * 100-wide box -- not zero, which is what measuring the flow position would give,
     * and the box it *would* have fitted inside is the one it left.
     */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&outerRule, "width", 100.0);
    LayoutOverflowRulePx(&outerRule, "height", 50.0);
    LayoutOverflowRuleInit(&innerRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&innerRule, "width", 80.0);
    LayoutOverflowRulePx(&innerRule, "height", 20.0);
    LayoutOverflowRulePosition(&innerRule, RoboCssPositioningRelative);
    LayoutOverflowRulePx(&innerRule, "left", 40.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = outerRule;
    rules[3] = innerRule;

    LayoutOverflowBegin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    inner = LayoutOverflowBoxFor(&fixture, "section");

    origin = LayoutOverflowOriginOf(&fixture, inner);
    RoboCheckEqInt(origin.x, 40);
    RoboCheckEqInt(origin.y, 0);
    RoboCheckEqInt(LayoutObjectPosition(fixture.tree, inner, &offset, NULL), RoboOk);
    RoboCheckEqInt(offset.dx, 40);

    edges = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt(edges.right, 20);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.top, 0);
    RoboCheckEqInt(edges.bottom, 0);
    /* The displacement did not change the box either: 10.12's claim, re-asserted here
     * because it is the fact the extent above is derived from. */
    RoboCheckEqInt(LayoutOverflowSizeOf(&fixture, inner).width, 80);

    LayoutOverflowEnd(&fixture);
}

static void TestOverflowPassStatsAndUnresolved(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule clipperRule;
    LayoutOverflowRule overflowerRule;
    LayoutOverflowRule spreaderRule;
    LayoutOverflowRule unusableRule;
    LayoutOverflowRule rules[6];
    LayoutStats stats;
    LayoutNodeId spreader;
    LayoutOverflowEdges edges;

    LayoutFixture();

    /*
     * Three boxes, three different facts, and the counters are where they are told
     * apart: one clips and does not overflow, one overflows and does not clip, and one
     * declared an overflow this build cannot use. The third is the one a build would
     * most easily lose -- its used mode is `visible`, so nothing about its *geometry*
     * says an author asked for something.
     */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&clipperRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&clipperRule, "height", 40.0);
    LayoutOverflowRuleOverflow(&clipperRule, "overflow-x", RoboCssOverflowHidden);
    LayoutOverflowRuleInit(&overflowerRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&overflowerRule, "height", 30.0);
    LayoutOverflowRuleInit(&spreaderRule, "article", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&spreaderRule, "width", 300.0);
    LayoutOverflowRulePx(&spreaderRule, "height", 20.0);
    LayoutOverflowRuleInit(&unusableRule, "aside", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&unusableRule, "height", 10.0);
    LayoutOverflowRuleKeyword(&unusableRule, "overflow-y", "scroll");
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = clipperRule;
    rules[3] = overflowerRule;
    rules[4] = spreaderRule;
    rules[5] = unusableRule;

    LayoutOverflowBegin(&fixture,
                          "<html><body><div><section></section></div><article></article>"
                          "<aside></aside></body></html>",
                          rules, 6u);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, &stats);

    /* The `div` clips on one axis and has nothing sticking out of it. */
    RoboCheckEqInt(stats.block.overflowClipped, 1u);
    /* The `article` declares 300 where its containing block is 800, so it reaches past
     * *nothing*: the counter below is about its own contents, and it has none. */
    spreader = LayoutOverflowBoxFor(&fixture, "article");
    edges = LayoutOverflowEdgesOf(&fixture, spreader);
    RoboCheckEqInt(LayoutOverflowEdgesHasOverflow(&edges), 0);
    /* `section` is 30 tall with no contents of its own, and `div` clips rather than
     * overflowing -- so exactly nothing overflows here. */
    RoboCheckEqInt(stats.block.overflowing, 0u);
    /* And the declared-but-unusable axis is counted, which is the only place it is
     * visible at all. */
    RoboCheckEqInt(stats.block.overflowUnresolved, 1u);
    edges = LayoutOverflowEdgesOf(&fixture, LayoutOverflowBoxFor(&fixture, "aside"));
    RoboCheckEqInt(edges.axesResolved, 0u);
    RoboCheckEqInt(edges.axesUnresolved, 1u);
    RoboCheckEqInt((int)edges.vertical, (int)RoboCssOverflowVisible);
    RoboCheckEqInt(LayoutOverflowEdgesClipsAny(&edges), 0);

    LayoutOverflowEnd(&fixture);
}

static void TestOverflowPassBoxModel(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule outerRule;
    LayoutOverflowRule innerRule;
    LayoutOverflowRule rules[4];
    LayoutNodeId outer;
    LayoutNodeId inner;
    LayoutOverflowEdges edges;
    LayoutSize size;
    LayoutPoint origin;
    LayoutRect paddingBox;

    LayoutFixture();

    /*
     * The reference is the **padding box**, and this case is the one that pins it: a
     * border of 2 and a padding of 10 on every side, and a 150-wide child. Measured
     * against the padding box the extent is 40; measured against the *border* box it
     * would be 36, and a build that used the border box -- or the content box, which
     * would give 50 -- would be wrong by exactly one box layer.
     *
     * And the margin is *outside* all of it: the box's own position moves by 10 and its
     * extents do not, which is the difference between external spacing and internal
     * overflow stated in two assertions.
     */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&outerRule, "width", 100.0);
    LayoutOverflowRulePx(&outerRule, "height", 50.0);
    LayoutOverflowRulePx(&outerRule, "margin-left", 10.0);
    LayoutOverflowRulePx(&outerRule, "margin-top", 10.0);
    LayoutOverflowRulePaddingAll(&outerRule, 10.0);
    LayoutOverflowRuleBorderAll(&outerRule, 2.0);
    LayoutOverflowRuleInit(&innerRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&innerRule, "width", 150.0);
    LayoutOverflowRulePx(&innerRule, "height", 90.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = outerRule;
    rules[3] = innerRule;

    LayoutOverflowBegin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    inner = LayoutOverflowBoxFor(&fixture, "section");

    /* The border box is the content box plus the padding plus the border -- 10.10's and
     * 10.11's arithmetic, re-asserted because the extent below is measured against the
     * box those two phases built. */
    size = LayoutOverflowSizeOf(&fixture, outer);
    RoboCheckEqInt(size.width, 124);
    RoboCheckEqInt(size.height, 74);
    RoboCheckEqInt(LayoutOverflowSizeOf(&fixture, inner).width, 150);

    paddingBox = LayoutOverflowPaddingBoxOf(&fixture, outer);
    RoboCheckEqInt(paddingBox.width, 120);
    RoboCheckEqInt(paddingBox.height, 70);

    /* The reference is the 120-wide, 70-tall *padding* box, and the child begins 10
     * inside it on both axes because that is where the content origin is: 10 + 150 - 120
     * on the right and 10 + 90 - 70 at the bottom. Against the border box the two would
     * be 36 and 26, and against the content box 50 and 40. */
    edges = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.top, 0);
    RoboCheckEqInt(edges.right, 40);
    RoboCheckEqInt(edges.bottom, 30);

    /* The margin put the box at 10, 10 in its containing block and changed neither
     * extent: a margin is spacing outside the box, an overflow is contents inside it. */
    origin = LayoutOverflowOriginOf(&fixture, outer);
    RoboCheckEqInt(origin.x, 10);
    RoboCheckEqInt(origin.y, 10);

    LayoutOverflowEnd(&fixture);
}

static void TestOverflowPassExtremeWidth(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule outerRule;
    LayoutOverflowRule innerRule;
    LayoutOverflowRule rules[4];
    LayoutNodeId outer;
    LayoutOverflowEdges edges;
    LayoutSize size;

    LayoutFixture();

    /*
     * Extreme geometry, and no invalid geometry may escape: a one-pixel box holding a
     * two-billion-pixel child produces one large, finite, positive extent and a box that
     * is still one pixel wide. Nothing wraps, nothing goes negative, and the record is
     * still a record.
     */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&outerRule, "width", 1.0);
    LayoutOverflowRulePx(&outerRule, "height", 1.0);
    LayoutOverflowRuleInit(&innerRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&innerRule, "width", 2000000000.0);
    LayoutOverflowRulePx(&innerRule, "height", 1.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = outerRule;
    rules[3] = innerRule;

    LayoutOverflowBegin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    edges = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(&edges), 1);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.top, 0);
    RoboCheckEqInt(edges.bottom, 0);
    RoboCheckEqInt(edges.right > 0, 1);
    RoboCheckEqInt(edges.right > 1000000000, 1);

    size = LayoutOverflowSizeOf(&fixture, outer);
    RoboCheckEqInt(size.width, 1);
    RoboCheckEqInt(size.height, 1);

    LayoutOverflowEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The object door                                                     */
/* ------------------------------------------------------------------ */

static void TestOverflowObjectDoor(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule divRule;
    LayoutOverflowRule rules[3];
    LayoutOverflowEdges edges;
    LayoutOverflowEdges zeroed;
    LayoutNodeId div;
    LayoutNodeId text;

    LayoutFixture();

    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&divRule, "height", 40.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutOverflowBegin(&fixture, "<html><body><div>text</div></body></html>", rules, 3u);
    LayoutOverflowEdgesInit(&zeroed);
    div = LayoutOverflowBoxFor(&fixture, "div");

    /* Before any pass: this box has no overflow *yet*, and the door says so rather than
     * answering the cleared record -- a cleared record is a legal value (no overflow is
     * the common case), so the door asks the result rather than reading the record's
     * silence. */
    LayoutOverflowEdgesInit(&edges);
    edges.right = 999;
    RoboCheckEqInt(LayoutObjectOverflow(fixture.tree, div, &edges, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutOverflowEdgesEqual(&edges, &zeroed), 1);

    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    div = LayoutOverflowBoxFor(&fixture, "div");
    RoboCheckEqInt(LayoutObjectOverflow(fixture.tree, div, &edges, NULL), RoboOk);
    RoboCheckEqInt(LayoutOverflowEdgesIsValid(&edges), 1);

    /* A text run is not a box at all, and it is declined: its contents contribute to its
     * parent through the region the parent's measurement reads, which is where the
     * honest answer lives. */
    text = LayoutOverflowFirstOfKind(&fixture, RoboLayoutObjectText);
    RoboCheck(text != RoboLayoutNodeIdNone);
    RoboCheckEqInt(LayoutObjectOverflow(fixture.tree, text, &edges, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutOverflowEdgesEqual(&edges, &zeroed), 1);

    /* And the four refusals every door in this module makes. */
    RoboCheckEqInt(LayoutObjectOverflow(NULL, div, &edges, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectOverflow(fixture.tree, RoboLayoutNodeIdNone,
                                                  &edges, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectOverflow(fixture.tree, (LayoutNodeId)9999,
                                                  &edges, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutObjectOverflow(fixture.tree, div, NULL, NULL),
                      RoboInvalidArgument);

    LayoutOverflowEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Re-layout and determinism                                           */
/* ------------------------------------------------------------------ */

static void TestOverflowRelayout(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule outerRule;
    LayoutOverflowRule innerRule;
    LayoutOverflowRule rules[4];
    LayoutNodeId outer;
    LayoutOverflowEdges edges;
    LayoutRect clip;

    LayoutFixture();

    /*
     * Changing a *content* size and changing an *overflow declaration*, both through the
     * module's one invalidation route -- a rebuilt tree and another pass -- and neither
     * of which is a second architecture. The extent follows the width, and the clip
     * rectangle appears when the mode does.
     */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&outerRule, "width", 100.0);
    LayoutOverflowRulePx(&outerRule, "height", 50.0);
    LayoutOverflowRuleInit(&innerRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&innerRule, "width", 300.0);
    LayoutOverflowRulePx(&innerRule, "height", 20.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = outerRule;
    rules[3] = innerRule;

    LayoutOverflowBegin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    edges = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt(edges.right, 200);
    RoboCheckEqInt(LayoutOverflowEdgesClipRect(
                          &edges, LayoutOverflowPaddingBoxOf(&fixture, outer), &clip, NULL),
                      RoboNotFound);

    LayoutOverflowRuleInit(&innerRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&innerRule, "width", 500.0);
    LayoutOverflowRulePx(&innerRule, "height", 20.0);
    LayoutOverflowStyleReplace(&fixture.source, "section", &innerRule);
    LayoutOverflowRebuild(&fixture);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    edges = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt(edges.right, 400);

    LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&outerRule, "width", 100.0);
    LayoutOverflowRulePx(&outerRule, "height", 50.0);
    LayoutOverflowRuleOverflow(&outerRule, "overflow", RoboCssOverflowHidden);
    LayoutOverflowStyleReplace(&fixture.source, "div", &outerRule);
    LayoutOverflowRebuild(&fixture);
    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = LayoutOverflowBoxFor(&fixture, "div");
    edges = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt(edges.right, 400); /* the measurement is unchanged */
    RoboCheckEqInt(LayoutOverflowEdgesClipsAny(&edges), 1);
    RoboCheckEqInt(LayoutOverflowEdgesClipRect(
                          &edges, LayoutOverflowPaddingBoxOf(&fixture, outer), &clip, NULL),
                      RoboOk);
    RoboCheckEqInt(clip.width, 100);

    LayoutOverflowEnd(&fixture);
}

static void TestOverflowDeterminism(void)
{
    LayoutOverflowFixture fixture;
    LayoutOverflowRule htmlRule;
    LayoutOverflowRule bodyRule;
    LayoutOverflowRule outerRule;
    LayoutOverflowRule innerRule;
    LayoutOverflowRule rules[4];
    LayoutNodeId outer;
    LayoutOverflowEdges first;
    LayoutOverflowEdges second;
    LayoutStats firstStats;
    LayoutStats secondStats;
    size_t index;

    LayoutFixture();

    /*
     * Identical input, identical geometry: the same tree laid out twice produces the
     * same record and the same counters, and changing only the *viewport* -- which
     * cannot move a box with every dimension declared -- produces them again. Repeated
     * more than twice because a pass that cached something across runs would show up
     * only on the third.
     */
    LayoutOverflowBlocks(&htmlRule, &bodyRule);
    LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&outerRule, "width", 100.0);
    LayoutOverflowRulePx(&outerRule, "height", 50.0);
    LayoutOverflowRuleOverflow(&outerRule, "overflow-x", RoboCssOverflowHidden);
    LayoutOverflowRuleInit(&innerRule, "section", RoboCssDisplayBlock);
    LayoutOverflowRulePx(&innerRule, "width", 300.0);
    LayoutOverflowRulePx(&innerRule, "height", 80.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = outerRule;
    rules[3] = innerRule;

    LayoutOverflowBegin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);

    LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, &firstStats);
    outer = LayoutOverflowBoxFor(&fixture, "div");
    first = LayoutOverflowEdgesOf(&fixture, outer);
    RoboCheckEqInt(first.right, 200);
    RoboCheckEqInt(first.bottom, 30);

    for (index = 0u; index < 3u; index++) {
        LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, &secondStats);
        outer = LayoutOverflowBoxFor(&fixture, "div");
        second = LayoutOverflowEdgesOf(&fixture, outer);
        RoboCheckEqInt(LayoutOverflowEdgesEqual(&first, &second), 1);
        RoboCheckEqInt(LayoutBlockStatsEqual(&firstStats.block, &secondStats.block),
                          1);
        RoboCheckEqInt(
            LayoutInlineStatsEqual(&firstStats.inlineFlow, &secondStats.inlineFlow),
            1);
    }

    LayoutOverflowEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The module owns nothing                                             */
/* ------------------------------------------------------------------ */

static void TestOverflowAllocatorBalance(void)
{
    size_t before;
    size_t settled;
    size_t index;

    LayoutFixture();
    before = LayoutLiveBlocks();

    /* One full cycle, several times: build, pass, pass again, destroy -- and the live
     * block count comes back to where it started every time. A record that owned an
     * allocation would show up here and nowhere else. */
    for (index = 0u; index < 3u; index++) {
        LayoutOverflowFixture fixture;
        LayoutOverflowRule htmlRule;
        LayoutOverflowRule bodyRule;
        LayoutOverflowRule outerRule;
        LayoutOverflowRule innerRule;
        LayoutOverflowRule rules[4];

        LayoutOverflowBlocks(&htmlRule, &bodyRule);
        LayoutOverflowRuleInit(&outerRule, "div", RoboCssDisplayBlock);
        LayoutOverflowRulePx(&outerRule, "width", 100.0);
        LayoutOverflowRuleOverflow(&outerRule, "overflow", RoboCssOverflowHidden);
        LayoutOverflowRuleInit(&innerRule, "section", RoboCssDisplayBlock);
        LayoutOverflowRulePx(&innerRule, "width", 300.0);
        rules[0] = htmlRule;
        rules[1] = bodyRule;
        rules[2] = outerRule;
        rules[3] = innerRule;

        LayoutOverflowBegin(&fixture,
                              "<html><body><div><section></section></div></body></html>",
                              rules, 4u);
        LayoutOverflowPass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);
        settled = LayoutLiveBlocks();
        /* The overflow layer allocates nothing of its own across a pass: the record is a
         * value on the object, the region and the clip rectangle are locals, and the
         * context is destroyed inside the helper. A second pass at another viewport
         * changes no count either. */
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
        LayoutOverflowPass(&fixture, 400, NULL);
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
        LayoutOverflowEnd(&fixture);
        RoboCheckEqInt(LayoutLiveBlocks(), before);
    }
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutOverflow(void);

void TestRegisterLayoutOverflow(void)
{
    TestRegister("layout.overflow.kind_vocabulary", TestOverflowKindVocabulary);
    TestRegister("layout.overflow.axis_vocabulary", TestOverflowAxisVocabulary);
    TestRegister("layout.overflow.axis_record", TestOverflowAxisRecord);
    TestRegister("layout.overflow.declared_record", TestOverflowDeclaredRecord);
    TestRegister("layout.overflow.edges_record", TestOverflowEdgesRecord);
    TestRegister("layout.overflow.seam_declarations", TestOverflowSeamDeclarations);
    TestRegister("layout.overflow.resolve_modes", TestOverflowResolveModes);
    TestRegister("layout.overflow.include_extents", TestOverflowIncludeExtents);
    TestRegister("layout.overflow.include_saturates", TestOverflowIncludeSaturates);
    TestRegister("layout.overflow.region_per_axis", TestOverflowRegionPerAxis);
    TestRegister("layout.overflow.clip_rect_refusals", TestOverflowClipRectRefusals);
    TestRegister("layout.overflow.pass_fitting", TestOverflowPassFitting);
    TestRegister("layout.overflow.pass_child_right", TestOverflowPassChildRight);
    TestRegister("layout.overflow.pass_child_below", TestOverflowPassChildBelow);
    TestRegister("layout.overflow.pass_size_unchanged", TestOverflowPassSizeUnchanged);
    TestRegister("layout.overflow.pass_hidden_clips", TestOverflowPassHiddenClips);
    TestRegister("layout.overflow.pass_axis_split", TestOverflowPassAxisSplit);
    TestRegister("layout.overflow.pass_recursion_clipped",
                       TestOverflowPassRecursionClipped);
    TestRegister("layout.overflow.pass_relative_child",
                       TestOverflowPassRelativeChild);
    TestRegister("layout.overflow.pass_stats_and_unresolved",
                       TestOverflowPassStatsAndUnresolved);
    TestRegister("layout.overflow.pass_box_model", TestOverflowPassBoxModel);
    TestRegister("layout.overflow.pass_extreme_width", TestOverflowPassExtremeWidth);
    TestRegister("layout.overflow.object_door", TestOverflowObjectDoor);
    TestRegister("layout.overflow.relayout", TestOverflowRelayout);
    TestRegister("layout.overflow.determinism", TestOverflowDeterminism);
    TestRegister("layout.overflow.allocator_balance", TestOverflowAllocatorBalance);
}
