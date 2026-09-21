/*
 * RoboLayout invalidation suite (Phase 10.14) — layout.invalidation.*
 *
 * Six things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary**: four state bits that compose, a closed set of reasons, three
 *   style effects, and the name doors a failing assertion prints through. The empty
 *   state is `CLEAN` and not a bit, so a cleared record is a statement rather than a
 *   default;
 *
 *   the **classifier**: what changing a *property name* costs this build's layout.
 *   Driven over real spellings -- `width`, `padding-left`, `background-color`,
 *   `border-style`, `cursor`, `WIDTH` -- because the question is what a stylesheet
 *   author writes, and the two answers that matter are the ones a naive table gets
 *   wrong: `border-style: none` is a *geometry* change (10.11's used width is zero
 *   unless the style draws), and an unknown property is *nothing* rather than
 *   everything;
 *
 *   the **policy**: three marking doors and the shape each one makes. Every case
 *   asserts the dirty set box for box, because the phase's claim is not "something was
 *   invalidated" but "exactly these boxes were" -- a build that marked the document
 *   would pass every weaker assertion here;
 *
 *   the **pass contract**: a recomputation clears the layout bits and leaves STYLE, a
 *   pass with no viewport clears nothing, and no box survives a pass holding a stale
 *   layout bit;
 *
 *   the **decision**: `LayoutContextRunIncremental` skips when nothing needs
 *   layout and runs when something does, and the four terms of that decision are each
 *   exercised on their own;
 *
 *   and the **outcome on real geometry**: a stacked document whose middle box grows,
 *   the incremental pass that follows, and the three statements the brief asks for --
 *   the changed box moved, the following flow moved with it, and the box before it did
 *   not.
 *
 * The fixture is 10.9's through 10.13's: real computed styles built through 9.8's own
 * makers, borrowed by a tree built by the real builder, and boxes resolved **by tag**
 * so that no case depends on arena order. What is new here is `LayoutInvStyleSetPx`,
 * which rewrites one declaration **on the style a built tree already borrows** -- the
 * only way to change what a pass will read without rebuilding the tree, and therefore
 * the only way the incremental door can be tested at all.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fixture that can declare px lengths                               */
/* ------------------------------------------------------------------ */

#define LAYOUT_INV_MAX_DECLARATIONS 12u
#define LAYOUT_INV_MAX_RULES 8u

typedef struct layoutInvDecl {
    const char *name;
    double px;
} LayoutInvDecl;

typedef struct layoutInvRule {
    const char *tag;
    CssDisplay display;
    LayoutInvDecl declarations[LAYOUT_INV_MAX_DECLARATIONS];
    size_t declarationsCount;
} LayoutInvRule;

static void LayoutInvRuleInit(LayoutInvRule *rule,
                                 const char *tag,
                                 CssDisplay display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static void LayoutInvRulePx(LayoutInvRule *rule, const char *name, double px)
{
    RoboCheckEqInt(rule->declarationsCount < LAYOUT_INV_MAX_DECLARATIONS, 1);
    rule->declarations[rule->declarationsCount].name = name;
    rule->declarations[rule->declarationsCount].px = px;
    rule->declarationsCount++;
}

typedef struct layoutInvStyleSlot {
    char tag[16];
    CssComputedStyle *style; /* owned */
    size_t answers;
} LayoutInvStyleSlot;

typedef struct layoutInvSource {
    LayoutInvStyleSlot rules[LAYOUT_INV_MAX_RULES];
    size_t count;
} LayoutInvSource;

static CssComputedStyle *LayoutInvStyleBuild(const LayoutInvRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    size_t index;

    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeDisplay(&value, rule->display, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("display"), 0u,
                                                       &specified, &value, NULL),
                      RoboOk);
    for (index = 0u; index < rule->declarationsCount; index++) {
        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        RoboCheckEqInt(CssComputedValueMakeLength(&value,
                                                             rule->declarations[index].px,
                                                             RoboCssUnitPx, 0, NULL),
                          RoboOk);
        RoboCheckEqInt(CssComputedStyleSetEntry(
                              style, RoboCssPropertyIdNone,
                              CssSliceFromText(rule->declarations[index].name), 0u,
                              &specified, &value, NULL),
                          RoboOk);
    }
    return style;
}

static void LayoutInvSourceAdd(LayoutInvSource *source, const LayoutInvRule *rule)
{
    size_t slot;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_INV_MAX_RULES, 1);
    slot = source->count;
    strncpy(source->rules[slot].tag, rule->tag, sizeof(source->rules[slot].tag) - 1u);
    source->rules[slot].tag[sizeof(source->rules[slot].tag) - 1u] = '\0';
    source->rules[slot].style = LayoutInvStyleBuild(rule);
    source->rules[slot].answers = 0u;
    source->count++;
}

/** The slot @p tag is answered from, or NULL. */
static LayoutInvStyleSlot *LayoutInvSourceSlot(LayoutInvSource *source,
                                                       const char *tag)
{
    size_t index;

    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            return &source->rules[index];
        }
    }
    return NULL;
}

static const CssComputedStyle *LayoutInvStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutInvSource *source = (LayoutInvSource *)userdata;
    const char *name;
    LayoutInvStyleSlot *slot;

    if (!source || !element) {
        return NULL;
    }
    name = HtmlElementName(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    slot = LayoutInvSourceSlot(source, name);
    if (!slot) {
        return NULL;
    }
    slot->answers++;
    return slot->style;
}

typedef struct layoutInvFixture {
    LayoutDocument doc;
    LayoutInvSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutInvFixture;

static void LayoutInvBegin(LayoutInvFixture *fixture,
                             const char *html,
                             const LayoutInvRule *rules,
                             size_t ruleCount)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < ruleCount; index++) {
        LayoutInvSourceAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutInvStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutInvEnd(LayoutInvFixture *fixture)
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

/**
 * Rewrite one px declaration **on the style a built tree already borrows**.
 *
 * The whole reason this suite can test the incremental door: a caller that replaced the
 * style and rebuilt the tree would invalidate everything by construction, and the
 * decision under test would never be reached. Here the borrowed pointer stays valid and
 * only what the next pass *reads* changes.
 */
static void LayoutInvStyleSetPx(LayoutInvFixture *fixture,
                                    const char *tag,
                                    const char *name,
                                    double px)
{
    LayoutInvStyleSlot *slot = LayoutInvSourceSlot(&fixture->source, tag);
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;

    RoboCheck(slot != NULL);
    RoboCheckEqInt(CssComputedValueMakeLength(&value, px, RoboCssUnitPx, 0, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(
                          slot->style, RoboCssPropertyIdNone,
                          CssSliceFromText(name), 0u, &specified, &value, NULL),
                      RoboOk);
}

#define LAYOUT_INV_VIEWPORT_WIDTH 800
#define LAYOUT_INV_VIEWPORT_HEIGHT 600

static LayoutContext *LayoutInvContext(int32_t viewportWidth)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;

    viewport.width = viewportWidth;
    viewport.height = LAYOUT_INV_VIEWPORT_HEIGHT;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    return context;
}

/** One full pass, with the context thrown away -- what most cases want. */
static void LayoutInvPass(LayoutInvFixture *fixture, LayoutStats *outStats)
{
    LayoutContext *context = LayoutInvContext(LAYOUT_INV_VIEWPORT_WIDTH);
    LayoutStats stats;

    RoboCheckEqInt(LayoutContextRun(context, fixture->tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);
    if (outStats) {
        *outStats = stats;
    }
}

static LayoutNodeId LayoutInvBoxFor(LayoutInvFixture *fixture,
                                                const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);
    LayoutNodeId id;

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    id = LayoutObjectIdForDomNode(fixture->tree, &element);
    RoboCheck(id != RoboLayoutNodeIdNone);
    return id;
}

static uint32_t LayoutInvStateOf(LayoutInvFixture *fixture, const char *tag)
{
    return LayoutTreeInvalidationState(fixture->tree, LayoutInvBoxFor(fixture, tag));
}

static LayoutInvalidationReason LayoutInvReasonOf(LayoutInvFixture *fixture,
                                                              const char *tag)
{
    return LayoutTreeInvalidationReason(fixture->tree,
                                                LayoutInvBoxFor(fixture, tag));
}

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestInvVocabulary(void)
{
    /*
     * The bits are a partition and the partition is the phase: STYLE is not part of
     * either of the two wider masks, and LAYOUT_BITS is a superset of RELAYOUT_BITS with
     * OVERFLOW as the difference. A build that changed one of these would pass every
     * "is it dirty?" assertion in this file and be wrong about *what*.
     */
    RoboCheckEqInt((int)RoboLayoutInvalidationClean, 0);
    RoboCheckEqInt(RoboLayoutInvalidationAll,
                      RoboLayoutInvalidationStyle | RoboLayoutInvalidationLayout |
                          RoboLayoutInvalidationSubtree | RoboLayoutInvalidationOverflow);
    RoboCheckEqInt(RoboLayoutInvalidationLayoutBits & RoboLayoutInvalidationStyle, 0);
    RoboCheckEqInt(RoboLayoutInvalidationOwnBits &
                          (RoboLayoutInvalidationSubtree | RoboLayoutInvalidationOverflow),
                      0);
    RoboCheckEqInt(RoboLayoutInvalidationOwnBits & RoboLayoutInvalidationLayoutBits,
                      RoboLayoutInvalidationLayout);

    RoboCheckEqInt(LayoutInvalidationStateIsKnown(RoboLayoutInvalidationAll), 1);
    RoboCheckEqInt(LayoutInvalidationStateIsKnown(RoboLayoutInvalidationClean), 1);
    RoboCheckEqInt(LayoutInvalidationStateIsKnown(
                          RoboLayoutInvalidationLayout | RoboLayoutInvalidationSubtree),
                      1);
    RoboCheckEqInt(LayoutInvalidationStateIsKnown(1u << 7), 0);
    RoboCheckEqInt(LayoutInvalidationStateIsKnown(
                          RoboLayoutInvalidationAll | (1u << 7)),
                      0);

    RoboCheckEqInt(strcmp(LayoutInvalidationStateName(RoboLayoutInvalidationClean),
                             "clean"),
                      0);
    RoboCheckEqInt(strcmp(LayoutInvalidationStateName(
                                 RoboLayoutInvalidationLayout | RoboLayoutInvalidationSubtree),
                             "layout+subtree"),
                      0);
    RoboCheckEqInt(strcmp(LayoutInvalidationStateName(RoboLayoutInvalidationAll),
                             "style+layout+subtree+overflow"),
                      0);
    RoboCheckEqInt(strcmp(LayoutInvalidationStateName(1u << 7), "unknown"), 0);

    RoboCheckEqInt(LayoutInvalidationReasonIsKnown(RoboLayoutInvalidationReasonNone),
                      1);
    RoboCheckEqInt(LayoutInvalidationReasonIsKnown(
                          (LayoutInvalidationReason)RoboLayoutInvalidationReasonCount),
                      0);
    RoboCheckEqInt(strcmp(LayoutInvalidationReasonName(
                                 RoboLayoutInvalidationReasonPropagated),
                             "propagated"),
                      0);
    RoboCheckEqInt(strcmp(LayoutInvalidationReasonName(
                                 (LayoutInvalidationReason)99),
                             "unknown"),
                      0);

    RoboCheckEqInt(LayoutStyleEffectIsKnown(RoboLayoutStyleEffectNone), 1);
    RoboCheckEqInt(LayoutStyleEffectIsKnown(RoboLayoutStyleEffectLayout), 1);
    RoboCheckEqInt(LayoutStyleEffectIsKnown(
                          (LayoutStyleEffect)RoboLayoutStyleEffectCount),
                      0);
    RoboCheckEqInt(strcmp(LayoutStyleEffectName(RoboLayoutStyleEffectPaint),
                             "paint"),
                      0);
    RoboCheckEqInt(strcmp(LayoutStyleEffectName((LayoutStyleEffect)42),
                             "unknown"),
                      0);
}

/* ------------------------------------------------------------------ */
/* The classifier                                                      */
/* ------------------------------------------------------------------ */

static void TestInvClassifier(void)
{
    /*
     * Geometry, in each of the four families that feed it: the box's own size, the box
     * model, the border family, and what decides where contents go.
     */
    static const char *geometryNames[] = {
        "display", "position", "top", "right", "bottom", "left",
        "width", "height", "min-width", "max-width", "min-height", "max-height",
        "box-sizing", "margin", "margin-left", "padding", "padding-left",
        "border", "border-width", "border-left-width", "border-style", "border-left-style",
        "overflow", "overflow-x", "overflow-y",
        "font-family", "font-size", "font-weight", "line-height", "letter-spacing",
        "word-spacing", "direction", "writing-mode"
    };
    static const char *paintNames[] = {
        "color", "background", "background-color", "background-image",
        "border-color", "border-top-color", "visibility", "opacity", "text-decoration"
    };
    static const char *noneNames[] = {
        "cursor", "z-index", "transition", "content", "vertical-align", "text-transform"
    };
    size_t index;

    for (index = 0u; index < sizeof(geometryNames) / sizeof(geometryNames[0]); index++) {
        RoboCheckEqInt(LayoutStyleEffectForProperty(geometryNames[index]),
                          RoboLayoutStyleEffectLayout);
    }
    for (index = 0u; index < sizeof(paintNames) / sizeof(paintNames[0]); index++) {
        RoboCheckEqInt(LayoutStyleEffectForProperty(paintNames[index]),
                          RoboLayoutStyleEffectPaint);
    }
    for (index = 0u; index < sizeof(noneNames) / sizeof(noneNames[0]); index++) {
        RoboCheckEqInt(LayoutStyleEffectForProperty(noneNames[index]),
                          RoboLayoutStyleEffectNone);
    }

    /*
     * The border split, stated once more where a reader will look for it: the *width*
     * and the *style* are geometry (10.11 gives a width of zero to `none`, and to
     * `hidden`), and the *colour* is not. A table that grouped all twelve longhands
     * would be wrong in the direction this phase exists to prevent.
     */
    RoboCheckEqInt(LayoutStyleEffectForProperty("border-style"),
                      RoboLayoutStyleEffectLayout);
    RoboCheckEqInt(LayoutStyleEffectForProperty("border-color"),
                      RoboLayoutStyleEffectPaint);

    /* A spelling from a document, not from a table. */
    RoboCheckEqInt(LayoutStyleEffectForProperty("WIDTH"),
                      RoboLayoutStyleEffectLayout);
    RoboCheckEqInt(LayoutStyleEffectForProperty("Padding-Left"),
                      RoboLayoutStyleEffectLayout);
    RoboCheckEqInt(LayoutStyleEffectForProperty("Background-Color"),
                      RoboLayoutStyleEffectPaint);

    /* Nothing, and not everything: an unknown name cannot have changed a conclusion
     * this module made, so it costs nothing -- and NULL is the same answer. */
    RoboCheckEqInt(LayoutStyleEffectForProperty("will-change"),
                      RoboLayoutStyleEffectNone);
    RoboCheckEqInt(LayoutStyleEffectForProperty(""),
                      RoboLayoutStyleEffectNone);
    RoboCheckEqInt(LayoutStyleEffectForProperty(NULL),
                      RoboLayoutStyleEffectNone);
}

/* ------------------------------------------------------------------ */
/* Records                                                             */
/* ------------------------------------------------------------------ */

static void TestInvRecords(void)
{
    LayoutInvalidation a;
    LayoutInvalidation b;
    LayoutInvalidationStats statsA;
    LayoutInvalidationStats statsB;

    LayoutInvalidationInit(&a);
    RoboCheckEqInt((int)a.state, RoboLayoutInvalidationClean);
    RoboCheckEqInt((int)a.reason, RoboLayoutInvalidationReasonNone);
    RoboCheckEqInt(LayoutInvalidationIsClean(&a), 1);
    RoboCheckEqInt(GetLayoutInvalidationState(&a), RoboLayoutInvalidationClean);
    RoboCheckEqInt(MakeLayoutInvalidationReason(&a),
                      RoboLayoutInvalidationReasonNone);

    /* NULL reads as the cleared record in every door, because "nobody said" and
     * "nothing is stale" are the same statement here. */
    RoboCheckEqInt(LayoutInvalidationIsClean(NULL), 1);
    RoboCheckEqInt(GetLayoutInvalidationState(NULL), RoboLayoutInvalidationClean);
    RoboCheckEqInt(MakeLayoutInvalidationReason(NULL),
                      RoboLayoutInvalidationReasonNone);
    RoboCheckEqInt(LayoutInvalidationEqual(&a, NULL), 0);
    RoboCheckEqInt(LayoutInvalidationEqual(NULL, NULL), 1);

    b = a;
    RoboCheckEqInt(LayoutInvalidationEqual(&a, &b), 1);
    b.state = RoboLayoutInvalidationStyle;
    b.reason = RoboLayoutInvalidationReasonPaint;
    RoboCheckEqInt(LayoutInvalidationEqual(&a, &b), 0);
    RoboCheckEqInt(LayoutInvalidationIsClean(&b), 0);

    LayoutInvalidationStatsInit(&statsA);
    RoboCheckEqInt(statsA.invalidated, 0u);
    RoboCheckEqInt(statsA.styleOnly, 0u);
    RoboCheckEqInt((int)statsA.root, (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt((int)statsA.reason, RoboLayoutInvalidationReasonNone);
    RoboCheckEqInt(statsA.recomputed, 0);

    statsB = statsA;
    RoboCheckEqInt(LayoutInvalidationStatsEqual(&statsA, &statsB), 1);
    statsB.recomputed = 1;
    RoboCheckEqInt(LayoutInvalidationStatsEqual(&statsA, &statsB), 0);
    RoboCheckEqInt(LayoutInvalidationStatsEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutInvalidationStatsEqual(&statsA, NULL), 0);
}

/* ------------------------------------------------------------------ */
/* A freshly built tree                                                */
/* ------------------------------------------------------------------ */

static void TestInvFreshTree(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[4];
    size_t objects;

    LayoutFixture();

    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "main", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[3], "div", RoboCssDisplayBlock);

    LayoutInvBegin(&fixture,
                     "<html><body><main><div></div></main></body></html>",
                     rules, 4u);

    /*
     * Every box a build creates owes a recomputation, and it is the *tree* that says so
     * rather than a caller. The count is the whole object set, so a document that grew a
     * box cannot have grown a clean one.
     */
    objects = LayoutTreeObjectCount(fixture.tree);
    RoboCheck(objects > 0u);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 0);
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 1);
    RoboCheckEqInt(LayoutTreeInvalidationCount(fixture.tree), objects);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "div"),
                      RoboLayoutInvalidationReasonContent);

    /* The shallowest box carrying a relayout bit is the root box: a document nobody has
     * laid out is a document a relayout would have to start from the top. */
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(fixture.tree),
                      (int)LayoutTreeRoot(fixture.tree));

    LayoutInvEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pass contract                                                   */
/* ------------------------------------------------------------------ */

static void TestInvPassClearsLayoutBits(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[3];
    LayoutStats stats;
    size_t objects;
    size_t index;
    size_t dirty = 0u;

    LayoutFixture();

    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "div", RoboCssDisplayBlock);

    LayoutInvBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    objects = LayoutTreeObjectCount(fixture.tree);

    LayoutInvPass(&fixture, &stats);

    RoboCheckEqInt(stats.invalidation.recomputed, 1);
    RoboCheckEqInt(stats.invalidation.invalidated, objects);
    RoboCheckEqInt((int)stats.invalidation.root, (int)LayoutTreeRoot(fixture.tree));
    RoboCheckEqInt((int)stats.invalidation.reason,
                      RoboLayoutInvalidationReasonContent);

    /* Nothing survives the pass holding a layout bit, and the count agrees with a walk
     * rather than with itself. */
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 0);
    RoboCheckEqInt(LayoutTreeInvalidationCount(fixture.tree), 0u);
    RoboCheckEqInt(LayoutInvalidationStyleOnlyCount(fixture.tree), 0u);
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(fixture.tree),
                      (int)RoboLayoutNodeIdNone);

    for (index = 0u; index < objects; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;

        RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, index, &id), RoboOk);
        if (LayoutTreeInvalidationState(fixture.tree, id) != RoboLayoutInvalidationClean) {
            dirty++;
        }
    }
    RoboCheckEqInt(dirty, 0u);

    LayoutInvEnd(&fixture);
}

static void TestInvPassWithoutViewportClearsNothing(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[3];
    LayoutContext *context = NULL;
    LayoutStats stats;
    size_t objects;

    LayoutFixture();

    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "div", RoboCssDisplayBlock);

    LayoutInvBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    objects = LayoutTreeObjectCount(fixture.tree);

    /*
     * The pass that computed no coordinate may not claim any conclusion was recomputed.
     * Requirement 11's stale-clean state is exactly what a build that cleared the bits
     * here would produce -- and it would be invisible until a later pass skipped work
     * that had never been done.
     */
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);

    RoboCheckEqInt(stats.invalidation.recomputed, 0);
    RoboCheckEqInt(stats.invalidation.invalidated, objects);
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 1);
    RoboCheckEqInt(LayoutTreeInvalidationCount(fixture.tree), objects);

    LayoutInvEnd(&fixture);
}

static void TestInvMarkRecomputedLeavesStyle(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[3];
    LayoutNodeId div;

    LayoutFixture();

    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "div", RoboCssDisplayBlock);

    LayoutInvBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutInvPass(&fixture, NULL);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);

    div = LayoutInvBoxFor(&fixture, "div");
    RoboCheckEqInt(LayoutTreeMarkStyle(fixture.tree, div,
                                                 RoboLayoutStyleEffectPaint, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"), RoboLayoutInvalidationStyle);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 0);
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 0);
    RoboCheckEqInt(LayoutTreeInvalidationCount(fixture.tree), 0u);
    RoboCheckEqInt(LayoutInvalidationStyleOnlyCount(fixture.tree), 1u);

    /*
     * And the clearing a pass performs is the narrow one: the layout bits go, the
     * repaint request stays. That pair is the whole reason STYLE is a separate bit.
     */
    RoboCheckEqInt(LayoutTreeMarkRecomputed(fixture.tree), RoboOk);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"), RoboLayoutInvalidationStyle);
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 0);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 0);

    /* And the full reset takes it. */
    RoboCheckEqInt(LayoutTreeMarkClean(fixture.tree), RoboOk);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"), RoboLayoutInvalidationClean);
    RoboCheckEqInt((int)LayoutInvReasonOf(&fixture, "div"),
                      RoboLayoutInvalidationReasonNone);

    LayoutInvEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The three policies                                                  */
/* ------------------------------------------------------------------ */

/*
 * The fixture the propagation cases share:
 *
 *   html
 *     body
 *       main
 *         div        <- what most cases invalidate
 *         section
 *       footer       <- main's following sibling, so the ancestor walk is exercised
 *
 * `div`'s following siblings are `section`, and `main`'s are `footer`, which is what
 * makes the transitive rule observable in two steps rather than in one.
 */
static void LayoutInvTreeRules(LayoutInvRule *rules)
{
    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "main", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[3], "div", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[4], "section", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[5], "footer", RoboCssDisplayBlock);
}

#define LAYOUT_INV_TREE_HTML \
    "<html><body><main><div></div><section></section></main><footer></footer></body></html>"

static void TestInvLayoutDirtyPropagation(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[6];
    LayoutNodeId div;

    LayoutFixture();
    LayoutInvTreeRules(rules);
    LayoutInvBegin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    LayoutInvPass(&fixture, NULL);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);

    div = LayoutInvBoxFor(&fixture, "div");
    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(fixture.tree, div,
                                                        RoboLayoutInvalidationReasonGeometry,
                                                        NULL),
                      RoboOk);

    /* The box itself owes all three: its placement, its contents, and its measurement. */
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "div"),
                      RoboLayoutInvalidationReasonGeometry);

    /* Its following sibling is the flow after it: a full re-derivation, propagated. */
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "section"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "section"),
                      RoboLayoutInvalidationReasonPropagated);

    /* Its ancestors are told their subtree and their measurement changed -- and
     * *not* that they must be placed again, because an ancestor's origin is computed
     * from its own containing block. */
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "main"),
                      RoboLayoutInvalidationSubtree | RoboLayoutInvalidationOverflow);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "body"),
                      RoboLayoutInvalidationSubtree | RoboLayoutInvalidationOverflow);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "html"),
                      RoboLayoutInvalidationSubtree | RoboLayoutInvalidationOverflow);

    /* And the transitively following flow: `footer` is not a sibling of `div`, it is the
     * sibling of `div`'s ancestor -- which is the rule a one-deep version would miss. */
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "footer"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "footer"),
                      RoboLayoutInvalidationReasonPropagated);

    RoboCheckEqInt(LayoutTreeInvalidationCount(fixture.tree), 6u);
    /*
     * And the region, which is not `div` and not the root box either. Three boxes need
     * placing -- `div`, `section` and `footer` -- and no box below `body` contains all
     * three, because `footer` is `main`'s sibling rather than its descendant. So the
     * smallest subtree a relayout could start from is `body`'s, and that is the answer:
     * the phase reports the region rather than the leaf, because the region is what a pass
     * would actually have to redo.
     */
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(fixture.tree),
                      (int)LayoutInvBoxFor(&fixture, "body"));
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "body"),
                      RoboLayoutInvalidationReasonPropagated);

    LayoutInvEnd(&fixture);
}

static void TestInvPositionDirtyIsNarrow(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[6];
    LayoutNodeId div;

    LayoutFixture();
    LayoutInvTreeRules(rules);
    LayoutInvBegin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    LayoutInvPass(&fixture, NULL);

    div = LayoutInvBoxFor(&fixture, "div");
    RoboCheckEqInt(LayoutTreeMarkPositionDirty(fixture.tree, div, NULL), RoboOk);

    /* The box and everything it contains: a displacement moves the contents with it. */
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "div"),
                      RoboLayoutInvalidationReasonPosition);

    /* Ancestors: measurement only. They are not told their subtree changed, because the
     * box still occupies the space flow gave it -- 10.12's contract, in one assertion. */
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "main"), RoboLayoutInvalidationOverflow);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "body"), RoboLayoutInvalidationOverflow);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "html"), RoboLayoutInvalidationOverflow);

    /*
     * And the following flow is left **clean**: a sibling's placement is computed from
     * the flow position, which did not move. This is the assertion the brief's "must not
     * unnecessarily recompute unrelated normal-flow sizing" turns into.
     */
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "section"), RoboLayoutInvalidationClean);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "footer"), RoboLayoutInvalidationClean);

    RoboCheckEqInt(LayoutTreeInvalidationCount(fixture.tree), 4u);
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(fixture.tree), (int)div);

    LayoutInvEnd(&fixture);
}

static void TestInvOverflowDirtyIsNarrowest(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[6];
    LayoutNodeId div;
    size_t index;
    size_t objects;
    size_t relayout = 0u;

    LayoutFixture();
    LayoutInvTreeRules(rules);
    LayoutInvBegin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    LayoutInvPass(&fixture, NULL);

    div = LayoutInvBoxFor(&fixture, "div");
    RoboCheckEqInt(LayoutTreeMarkOverflowDirty(fixture.tree, div, NULL), RoboOk);

    /*
     * A mode is not a size: the box's geometry is correct, so nothing carries a relayout
     * bit -- no LAYOUT anywhere and no SUBTREE anywhere -- while the box and its
     * ancestors all owe a re-measurement. The pass still has to run (a boundary moved),
     * which is why `needs_layout` and `root` are two different questions.
     */
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"), RoboLayoutInvalidationOverflow);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "main"), RoboLayoutInvalidationOverflow);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "html"), RoboLayoutInvalidationOverflow);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "section"), RoboLayoutInvalidationClean);

    objects = LayoutTreeObjectCount(fixture.tree);
    for (index = 0u; index < objects; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;
        uint32_t state;

        RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, index, &id), RoboOk);
        state = LayoutTreeInvalidationState(fixture.tree, id);
        if ((state & RoboLayoutInvalidationOwnBits) != 0u) {
            relayout++;
        }
    }
    RoboCheckEqInt(relayout, 0u);
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(fixture.tree),
                      (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 1);

    LayoutInvEnd(&fixture);
}

static void TestInvSubtreeDirty(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[6];
    LayoutNodeId mainBox;

    LayoutFixture();
    LayoutInvTreeRules(rules);
    LayoutInvBegin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    LayoutInvPass(&fixture, NULL);

    mainBox = LayoutInvBoxFor(&fixture, "main");
    RoboCheckEqInt(LayoutTreeMarkSubtreeDirty(fixture.tree, mainBox,
                                                          RoboLayoutInvalidationReasonContent,
                                                          NULL),
                      RoboOk);

    /* The box and every descendant, with the *explicit* reason -- not the propagated
     * one, which is what makes "something was rebuilt here" readable. */
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "main"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "main"),
                      RoboLayoutInvalidationReasonContent);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "div"),
                      RoboLayoutInvalidationReasonContent);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "section"),
                      RoboLayoutInvalidationLayoutBits);

    /* Ancestors and the following flow, exactly as a box-model change reaches them. */
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "body"),
                      RoboLayoutInvalidationSubtree | RoboLayoutInvalidationOverflow);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "footer"),
                      RoboLayoutInvalidationLayoutBits);

    /* `footer` is stale too, and it is a sibling of `main`, so the region is one level
     * above the box the mark was made on. */
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(fixture.tree),
                      (int)LayoutInvBoxFor(&fixture, "body"));

    LayoutInvEnd(&fixture);
}

static void TestInvStyleEffects(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[6];
    LayoutNodeId div;

    LayoutFixture();
    LayoutInvTreeRules(rules);
    LayoutInvBegin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    LayoutInvPass(&fixture, NULL);
    div = LayoutInvBoxFor(&fixture, "div");

    /* NONE marks nothing at all -- the answer that keeps an unread property from costing
     * a document's geometry. */
    RoboCheckEqInt(LayoutTreeMarkStyle(fixture.tree, div,
                                                 RoboLayoutStyleEffectNone, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);

    /* PAINT marks the box and nobody else: a colour is not inherited by a *box's*
     * geometry and moves nothing. */
    RoboCheckEqInt(LayoutTreeMarkStyle(fixture.tree, div,
                                                 RoboLayoutStyleEffectPaint, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"), RoboLayoutInvalidationStyle);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "main"), RoboLayoutInvalidationClean);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "section"), RoboLayoutInvalidationClean);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "html"), RoboLayoutInvalidationClean);

    RoboCheckEqInt(LayoutTreeMarkClean(fixture.tree), RoboOk);

    /* LAYOUT is the full policy, reached through the name door. */
    RoboCheckEqInt(LayoutTreeMarkStyleProperty(fixture.tree, div, "padding-left",
                                                           NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "div"),
                      RoboLayoutInvalidationReasonGeometry);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "section"),
                      RoboLayoutInvalidationLayoutBits);

    RoboCheckEqInt(LayoutTreeMarkClean(fixture.tree), RoboOk);

    /* And the classifier's own spelling, mixed case, through the same door. */
    RoboCheckEqInt(LayoutTreeMarkStyleProperty(fixture.tree, div, "Background-Color",
                                                           NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"), RoboLayoutInvalidationStyle);

    RoboCheckEqInt(LayoutTreeMarkClean(fixture.tree), RoboOk);
    RoboCheckEqInt(LayoutTreeMarkStyleProperty(fixture.tree, div, "cursor", NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);

    LayoutInvEnd(&fixture);
}

static void TestInvMarkAll(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[6];
    size_t objects;

    LayoutFixture();
    LayoutInvTreeRules(rules);
    LayoutInvBegin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    LayoutInvPass(&fixture, NULL);
    objects = LayoutTreeObjectCount(fixture.tree);

    RoboCheckEqInt(LayoutTreeMarkAll(fixture.tree,
                                                RoboLayoutInvalidationReasonViewport, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeInvalidationCount(fixture.tree), objects);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "section"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "section"),
                      RoboLayoutInvalidationReasonViewport);
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(fixture.tree),
                      (int)LayoutTreeRoot(fixture.tree));

    /* The reason is validated, so a typo cannot travel through the pipeline looking like
     * a fact about what happened. */
    RoboCheckEqInt(LayoutTreeMarkAll(
                          fixture.tree, (LayoutInvalidationReason)99, NULL),
                      RoboInvalidArgument);

    LayoutInvEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Reason rule and idempotence                                         */
/* ------------------------------------------------------------------ */

static void TestInvReasonRule(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[3];
    LayoutNodeId div;

    LayoutFixture();
    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "div", RoboCssDisplayBlock);
    LayoutInvBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutInvPass(&fixture, NULL);
    div = LayoutInvBoxFor(&fixture, "div");

    /*
     * A propagated reason never replaces an explicit one, and an explicit one replaces
     * "something below you". Both directions are asserted here because the interesting
     * failure is the second: a subtree mark arriving after a geometry change would
     * otherwise erase the answer to "what happened?".
     */
    RoboCheckEqInt(LayoutTreeMarkStyle(fixture.tree, div,
                                                 RoboLayoutStyleEffectLayout, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "div"),
                      RoboLayoutInvalidationReasonGeometry);

    RoboCheckEqInt(LayoutTreeMarkPositionDirty(fixture.tree, div, NULL), RoboOk);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "div"),
                      RoboLayoutInvalidationReasonGeometry);

    RoboCheckEqInt(LayoutTreeMarkClean(fixture.tree), RoboOk);

    /* Propagated first, explicit second: the explicit cause is the one a reader wants. */
    RoboCheckEqInt(LayoutTreeMarkSubtreeDirty(fixture.tree, div,
                                                         RoboLayoutInvalidationReasonContent,
                                                         NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "html"),
                      RoboLayoutInvalidationReasonPropagated);
    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(fixture.tree,
                                                        LayoutInvBoxFor(&fixture, "html"),
                                                        RoboLayoutInvalidationReasonGeometry,
                                                        NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "html"),
                      RoboLayoutInvalidationReasonGeometry);

    LayoutInvEnd(&fixture);
}

static void TestInvIdempotence(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[3];
    LayoutNodeId div;

    LayoutFixture();
    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "div", RoboCssDisplayBlock);
    LayoutInvBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutInvPass(&fixture, NULL);
    div = LayoutInvBoxFor(&fixture, "div");

    /* Three marks, one box: the count is a count of dirty boxes and not of marks, which
     * is the property that makes it a read rather than a running total to keep in step. */
    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(fixture.tree, div,
                                                        RoboLayoutInvalidationReasonGeometry,
                                                        NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeInvalidationCount(fixture.tree), 3u); /* div, body, html */
    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(fixture.tree, div,
                                                        RoboLayoutInvalidationReasonGeometry,
                                                        NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(fixture.tree, div,
                                                        RoboLayoutInvalidationReasonContent,
                                                        NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeInvalidationCount(fixture.tree), 3u);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvReasonOf(&fixture, "div"),
                      RoboLayoutInvalidationReasonGeometry);

    /* And marking clean twice is the same as marking it once. */
    RoboCheckEqInt(LayoutTreeMarkClean(fixture.tree), RoboOk);
    RoboCheckEqInt(LayoutTreeMarkClean(fixture.tree), RoboOk);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);

    LayoutInvEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Refusals                                                            */
/* ------------------------------------------------------------------ */

static void TestInvRefusals(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[3];
    LayoutNodeId div;
    LayoutNodeId past;

    LayoutFixture();
    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "div", RoboCssDisplayBlock);
    LayoutInvBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutInvPass(&fixture, NULL);

    div = LayoutInvBoxFor(&fixture, "div");
    past = RoboLayoutNodeIdNone;
    {
        LayoutNodeId last = RoboLayoutNodeIdNone;

        RoboCheckEqInt(LayoutObjectIdAt(
                              fixture.tree,
                              LayoutTreeObjectCount(fixture.tree) - 1u, &last),
                          RoboOk);
        past = last + 100u;
    }

    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(
                          fixture.tree, past, RoboLayoutInvalidationReasonGeometry, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutTreeMarkSubtreeDirty(
                          fixture.tree, past, RoboLayoutInvalidationReasonContent, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutTreeMarkPositionDirty(fixture.tree, past, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutTreeMarkOverflowDirty(fixture.tree, past, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutTreeMarkStyle(fixture.tree, past,
                                                 RoboLayoutStyleEffectLayout, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutTreeMarkStyleProperty(fixture.tree, past, "width", NULL),
                      RoboNotFound);

    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(
                          fixture.tree, div, (LayoutInvalidationReason)99, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeMarkStyle(
                          fixture.tree, div, (LayoutStyleEffect)99, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(
                          NULL, div, RoboLayoutInvalidationReasonGeometry, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeMarkStyle(NULL, div, RoboLayoutStyleEffectPaint, NULL),
                      RoboInvalidArgument);

    /* A refused mark leaves the tree exactly as it was: nothing was invalidated by a call
     * that failed. */
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);

    /* And reading about an id that names nothing is `clean`, not an error -- the same
     * convention every flag read in this module uses. */
    RoboCheckEqInt(LayoutTreeInvalidationState(NULL, div), RoboLayoutInvalidationClean);
    RoboCheckEqInt((int)LayoutTreeInvalidationReason(fixture.tree, past),
                      RoboLayoutInvalidationReasonNone);
    RoboCheckEqInt(LayoutTreeIsClean(NULL), 1);
    RoboCheckEqInt(LayoutTreeNeedsLayout(NULL), 0);
    RoboCheckEqInt(LayoutTreeInvalidationCount(NULL), 0u);
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(NULL),
                      (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt(LayoutTreeMarkClean(NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeMarkRecomputed(NULL), RoboOk);

    LayoutInvEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The dirty root                                                      */
/* ------------------------------------------------------------------ */

static void TestInvRootIsTheCommonAncestor(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[6];
    LayoutNodeId div;
    LayoutNodeId body;

    LayoutFixture();
    LayoutInvTreeRules(rules);
    LayoutInvBegin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    LayoutInvPass(&fixture, NULL);

    div = LayoutInvBoxFor(&fixture, "div");
    body = LayoutInvBoxFor(&fixture, "body");

    /*
     * A mark with **nothing after it** answers the box itself: `footer` is the last box in
     * the document, so no sibling anywhere up the chain is dragged along and the only box
     * that needs placing is the one that was marked.
     *
     * (`section` would not do here, and the reason is worth writing down: it is the last
     * child of `main`, but `main` itself has a following sibling, so marking `section`
     * reaches `footer` -- the ancestor walk is what makes "the last child" insufficient.)
     */
    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(
                          fixture.tree, LayoutInvBoxFor(&fixture, "footer"),
                          RoboLayoutInvalidationReasonGeometry, NULL),
                      RoboOk);
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(fixture.tree),
                      (int)LayoutInvBoxFor(&fixture, "footer"));

    RoboCheckEqInt(LayoutTreeMarkClean(fixture.tree), RoboOk);

    /*
     * And one whose damage spans branches answers their common ancestor. Marking `div`
     * dirties `section` (its sibling) and `footer` (its ancestor's sibling), and no box
     * below `body` contains `footer` along with `div` -- so the region is `body`, not the
     * shallower `footer` and not the leaf that changed.
     */
    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(fixture.tree, div,
                                                        RoboLayoutInvalidationReasonGeometry,
                                                        NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "footer"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(fixture.tree), (int)body);

    /* And a mark on the root box answers the root box. */
    RoboCheckEqInt(LayoutTreeMarkClean(fixture.tree), RoboOk);
    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(
                          fixture.tree, LayoutTreeRoot(fixture.tree),
                          RoboLayoutInvalidationReasonGeometry, NULL),
                      RoboOk);
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(fixture.tree),
                      (int)LayoutTreeRoot(fixture.tree));

    LayoutInvEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The incremental decision                                            */
/* ------------------------------------------------------------------ */

static void TestInvIncrementalSkipsACleanTree(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[3];
    LayoutContext *context;
    LayoutStats first;
    LayoutStats second;
    LayoutResult before;
    LayoutResult after;
    LayoutNodeId div;

    LayoutFixture();
    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "div", RoboCssDisplayBlock);
    LayoutInvBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

    context = LayoutInvContext(LAYOUT_INV_VIEWPORT_WIDTH);

    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context), (int)RoboLayoutPassWalked);
    LayoutContextStats(context, &first);
    RoboCheckEqInt(first.invalidation.recomputed, 1);

    div = LayoutInvBoxFor(&fixture, "div");
    RoboCheckEqInt(LayoutObjectResult(fixture.tree, div, &before), RoboOk);

    /*
     * Nothing changed, nothing was asked for: the second call is the same door and it does
     * no work. The outcome says so, `visited` is 0 because nothing was walked, and
     * `passes` still advances -- a pass that concluded "nothing to do" is a pass.
     */
    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context), (int)RoboLayoutPassClean);
    LayoutContextStats(context, &second);
    RoboCheckEqInt(second.invalidation.recomputed, 0);
    RoboCheckEqInt(second.invalidation.invalidated, 0u);
    RoboCheckEqInt(second.visited, 0u);
    RoboCheckEqInt(second.passes, 2u);
    RoboCheck(second.objects > 0u);

    /* And the results a caller is holding are still current, which is the point. */
    RoboCheckEqInt(LayoutObjectResult(fixture.tree, div, &after), RoboOk);
    RoboCheckEqInt(LayoutResultEqual(&before, &after), 1);

    LayoutContextDestroy(context);
    LayoutInvEnd(&fixture);
}

static void TestInvIncrementalRunsAfterAMark(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[6];
    LayoutContext *context;
    LayoutStats stats;
    LayoutNodeId div;

    LayoutFixture();
    LayoutInvTreeRules(rules);
    LayoutInvBegin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);

    context = LayoutInvContext(LAYOUT_INV_VIEWPORT_WIDTH);
    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);

    div = LayoutInvBoxFor(&fixture, "div");
    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(fixture.tree, div,
                                                        RoboLayoutInvalidationReasonGeometry,
                                                        NULL),
                      RoboOk);

    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context), (int)RoboLayoutPassWalked);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.invalidation.recomputed, 1);
    RoboCheckEqInt(stats.invalidation.invalidated, 6u);
    /* The region's root, not the leaf: `div`'s damage reaches `footer`, which is
     * shallower, so the common ancestor is `body` -- and its reason is the propagated one,
     * which is the truth about it (the region was not marked by a cause, it was told). */
    RoboCheckEqInt((int)stats.invalidation.root,
                      (int)LayoutInvBoxFor(&fixture, "body"));
    RoboCheckEqInt((int)stats.invalidation.reason,
                      RoboLayoutInvalidationReasonPropagated);

    /* The pass consumed what it found. */
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);

    LayoutContextDestroy(context);
    LayoutInvEnd(&fixture);
}

static void TestInvIncrementalRunsWhenInputsChange(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[3];
    LayoutContext *context;
    LayoutStats stats;
    LayoutSize viewport;

    LayoutFixture();
    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "div", RoboCssDisplayBlock);
    LayoutInvBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

    context = LayoutInvContext(LAYOUT_INV_VIEWPORT_WIDTH);
    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context), (int)RoboLayoutPassClean);

    /*
     * The viewport is not part of the tree, so the model above cannot see it move -- the
     * context records that an *input* changed, and that is what makes the third call run
     * even though every box is clean. The conservative direction is deliberate: the same
     * size re-recorded costs one pass rather than a comparison of two floats.
     */
    viewport.width = LAYOUT_INV_VIEWPORT_WIDTH / 2;
    viewport.height = LAYOUT_INV_VIEWPORT_HEIGHT;
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context), (int)RoboLayoutPassWalked);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.invalidation.recomputed, 1);
    RoboCheckEqInt(stats.invalidation.invalidated, 0u);

    /* And a metrics source is an input for the same reason, in both directions. */
    RoboCheckEqInt(LayoutContextSetTextMetrics(context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context), (int)RoboLayoutPassWalked);

    /* A different tree is the fourth term: a context has no results for it. */
    {
        LayoutInvFixture other;
        LayoutInvRule otherRules[3];

        LayoutInvRuleInit(&otherRules[0], "html", RoboCssDisplayBlock);
        LayoutInvRuleInit(&otherRules[1], "body", RoboCssDisplayBlock);
        LayoutInvRuleInit(&otherRules[2], "p", RoboCssDisplayBlock);
        LayoutInvBegin(&other, "<html><body><p></p></body></html>", otherRules, 3u);

        RoboCheckEqInt(LayoutContextRunIncremental(context, other.tree, NULL), RoboOk);
        RoboCheckEqInt((int)LayoutContextOutcome(context), (int)RoboLayoutPassWalked);
        LayoutInvEnd(&other);
    }

    LayoutContextDestroy(context);
    LayoutInvEnd(&fixture);
}

static void TestInvIncrementalSkipsAPaintOnlyChange(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[3];
    LayoutContext *context;
    LayoutStats stats;
    LayoutResult before;
    LayoutResult after;
    LayoutNodeId div;

    LayoutFixture();
    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "div", RoboCssDisplayBlock);
    LayoutInvBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

    context = LayoutInvContext(LAYOUT_INV_VIEWPORT_WIDTH);
    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);

    div = LayoutInvBoxFor(&fixture, "div");
    RoboCheckEqInt(LayoutObjectResult(fixture.tree, div, &before), RoboOk);
    RoboCheckEqInt(LayoutTreeMarkStyleProperty(fixture.tree, div,
                                                          "background-color", NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 0);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 0);

    /*
     * Requirement 13, in three assertions: a colour change is *not clean* and *does not
     * need layout*, so the pass is skipped -- and the repaint request survives the call
     * rather than being silently consumed by a pass that never ran.
     */
    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context), (int)RoboLayoutPassClean);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.invalidation.recomputed, 0);
    RoboCheckEqInt(stats.invalidation.styleOnly, 1u);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"), RoboLayoutInvalidationStyle);
    RoboCheckEqInt(LayoutObjectResult(fixture.tree, div, &after), RoboOk);
    RoboCheckEqInt(LayoutResultEqual(&before, &after), 1);

    LayoutContextDestroy(context);
    LayoutInvEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Real geometry: the brief's stacked-block scenario                   */
/* ------------------------------------------------------------------ */

static void TestInvRelayoutMovesTheFollowingFlow(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[6];
    LayoutContext *context;
    LayoutStats stats;
    LayoutResult pBefore;
    LayoutResult pAfter;
    LayoutResult divBefore;
    LayoutResult divAfter;
    LayoutResult sectionBefore;
    LayoutResult sectionAfter;
    LayoutNodeId p;
    LayoutNodeId div;
    LayoutNodeId section;

    LayoutFixture();

    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "main", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[3], "p", RoboCssDisplayBlock);
    LayoutInvRulePx(&rules[3], "height", 20.0);
    LayoutInvRuleInit(&rules[4], "div", RoboCssDisplayBlock);
    LayoutInvRulePx(&rules[4], "height", 40.0);
    /*
     * `section` needs a rule of its own, and the reason is worth stating: an element no
     * rule answers for gets **no style**, so its display is CSS's initial `inline` and it
     * is inline-level in a block context -- placed by nobody, `UNRESOLVED`, and a fixture
     * bug rather than a layout one. The three boxes here are the brief's
     * normal/relative/after, so all three are declared.
     */
    LayoutInvRuleInit(&rules[5], "section", RoboCssDisplayBlock);

    LayoutInvBegin(&fixture,
                     "<html><body><main><p></p><div></div><section></section></main></body></html>",
                     rules, 6u);

    context = LayoutInvContext(LAYOUT_INV_VIEWPORT_WIDTH);
    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);

    p = LayoutInvBoxFor(&fixture, "p");
    div = LayoutInvBoxFor(&fixture, "div");
    section = LayoutInvBoxFor(&fixture, "section");

    RoboCheckEqInt(LayoutObjectResult(fixture.tree, p, &pBefore), RoboOk);
    RoboCheckEqInt(LayoutObjectResult(fixture.tree, div, &divBefore), RoboOk);
    RoboCheckEqInt(LayoutObjectResult(fixture.tree, section, &sectionBefore), RoboOk);

    /* The stack: 20, then 40, then whatever follows. */
    RoboCheckEqInt(divBefore.position.y, 20);
    RoboCheckEqInt(sectionBefore.position.y, 60);
    RoboCheckEqInt(sectionBefore.position.y - divBefore.position.y, 40);

    /*
     * The middle box grows by 20. Its declaration is rewritten **on the style the tree
     * already borrows**, so the same tree is laid out again -- which is what makes the
     * incremental decision, and the policy behind it, reachable at all.
     */
    LayoutInvStyleSetPx(&fixture, "div", "height", 60.0);
    RoboCheckEqInt(LayoutTreeMarkStyleProperty(fixture.tree, div, "height", NULL),
                      RoboOk);

    /* The dirty set, before the pass runs: the box, and the flow after it. `p` -- the box
     * *before* it -- is untouched, which is the assertion a document-wide mark fails. */
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "div"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "section"),
                      RoboLayoutInvalidationLayoutBits);
    RoboCheckEqInt(LayoutInvStateOf(&fixture, "p"), RoboLayoutInvalidationClean);

    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context), (int)RoboLayoutPassWalked);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.invalidation.recomputed, 1);
    /* The changed box is `div` and its following sibling `section` moved: the region that
     * contains both is `main`, `div`'s parent -- which is the smallest subtree a relayout
     * could have started from, and therefore the honest answer. */
    RoboCheckEqInt((int)stats.invalidation.root,
                      (int)LayoutInvBoxFor(&fixture, "main"));

    RoboCheckEqInt(LayoutObjectResult(fixture.tree, p, &pAfter), RoboOk);
    RoboCheckEqInt(LayoutObjectResult(fixture.tree, div, &divAfter), RoboOk);
    RoboCheckEqInt(LayoutObjectResult(fixture.tree, section, &sectionAfter), RoboOk);

    /* 1. the changed box grew and did not move; 2. the flow after it moved by the delta;
     * 3. the box before it did not change at all. */
    RoboCheckEqInt(divAfter.size.height, 60);
    RoboCheckEqInt(divAfter.position.y, 20);
    RoboCheckEqInt(sectionAfter.position.y, 80);
    RoboCheckEqInt(sectionAfter.position.y - divAfter.position.y, 60);
    RoboCheckEqInt(LayoutResultEqual(&pBefore, &pAfter), 1);

    /* And no stale bit survives the pass, on any box. */
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);
    RoboCheckEqInt(LayoutTreeInvalidationCount(fixture.tree), 0u);

    LayoutContextDestroy(context);
    LayoutInvEnd(&fixture);
}

static void TestInvRelayoutIsStable(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[4];
    LayoutContext *context;
    LayoutResult results[6];
    LayoutResult repeat[6];
    size_t count;
    size_t index;
    int round;

    LayoutFixture();

    LayoutInvRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[2], "main", RoboCssDisplayBlock);
    LayoutInvRuleInit(&rules[3], "div", RoboCssDisplayBlock);
    LayoutInvRulePx(&rules[3], "height", 30.0);

    LayoutInvBegin(&fixture, "<html><body><main><div></div></main></body></html>", rules, 4u);
    context = LayoutInvContext(LAYOUT_INV_VIEWPORT_WIDTH);
    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);

    count = LayoutTreeObjectCount(fixture.tree);
    RoboCheck(count <= 6u);
    for (index = 0u; index < count; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;

        RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, index, &id), RoboOk);
        RoboCheckEqInt(LayoutObjectResult(fixture.tree, id, &results[index]), RoboOk);
    }

    /*
     * Invalidate, layout, invalidate, layout -- four times -- with the same input each
     * round. The geometry must be equal to the first round's box for box and the tree must
     * be clean at the end of every round: a cumulative pass would drift here, and a
     * leaky clearing would leave a bit behind on the third.
     */
    for (round = 0; round < 4; round++) {
        LayoutNodeId div = LayoutInvBoxFor(&fixture, "div");
        LayoutStats stats;

        RoboCheckEqInt(LayoutTreeMarkStyleProperty(fixture.tree, div, "height", NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL), RoboOk);
        RoboCheckEqInt((int)LayoutContextOutcome(context), (int)RoboLayoutPassWalked);
        LayoutContextStats(context, &stats);
        RoboCheckEqInt(stats.invalidation.recomputed, 1);
        RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);
    }

    for (index = 0u; index < count; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;

        RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, index, &id), RoboOk);
        RoboCheckEqInt(LayoutObjectResult(fixture.tree, id, &repeat[index]), RoboOk);
        RoboCheckEqInt(LayoutResultEqual(&results[index], &repeat[index]), 1);
    }

    LayoutContextDestroy(context);
    LayoutInvEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Determinism and allocation                                          */
/* ------------------------------------------------------------------ */

static void TestInvDeterminism(void)
{
    LayoutInvFixture first;
    LayoutInvFixture second;
    LayoutInvRule rules[6];
    LayoutInvRule twinRules[6];
    size_t index;

    LayoutFixture();
    LayoutInvTreeRules(rules);
    LayoutInvTreeRules(twinRules);

    LayoutInvBegin(&first, LAYOUT_INV_TREE_HTML, rules, 6u);
    LayoutInvBegin(&second, LAYOUT_INV_TREE_HTML, twinRules, 6u);
    LayoutInvPass(&first, NULL);
    LayoutInvPass(&second, NULL);

    /*
     * One marking script, run over two trees built from one document, compared box for box
     * and bit for bit -- state, reason, counts and root. Anything read from an address, an
     * arena's history or an iteration order would disagree here.
     */
    {
        static const char *const tags[] = { "div", "section", "footer", "main", "html" };
        size_t t;

        for (t = 0u; t < sizeof(tags) / sizeof(tags[0]); t++) {
            LayoutNodeId a = LayoutInvBoxFor(&first, tags[t]);
            LayoutNodeId b = LayoutInvBoxFor(&second, tags[t]);

            if (t % 2u == 0u) {
                RoboCheckEqInt(LayoutTreeMarkLayoutDirty(
                                      first.tree, a, RoboLayoutInvalidationReasonGeometry,
                                      NULL),
                                  RoboOk);
                RoboCheckEqInt(LayoutTreeMarkLayoutDirty(
                                      second.tree, b, RoboLayoutInvalidationReasonGeometry,
                                      NULL),
                                  RoboOk);
            } else {
                RoboCheckEqInt(LayoutTreeMarkPositionDirty(second.tree, b, NULL),
                                  RoboOk);
                RoboCheckEqInt(LayoutTreeMarkPositionDirty(first.tree, a, NULL),
                                  RoboOk);
            }
        }
    }

    RoboCheckEqInt(LayoutTreeInvalidationCount(first.tree),
                      LayoutTreeInvalidationCount(second.tree));
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(first.tree),
                      (int)LayoutTreeInvalidationRoot(second.tree));
    RoboCheckEqInt(LayoutTreeNeedsLayout(first.tree),
                      LayoutTreeNeedsLayout(second.tree));

    for (index = 0u; index < LayoutTreeObjectCount(first.tree); index++) {
        LayoutNodeId a = RoboLayoutNodeIdNone;
        LayoutNodeId b = RoboLayoutNodeIdNone;

        RoboCheckEqInt(LayoutObjectIdAt(first.tree, index, &a), RoboOk);
        RoboCheckEqInt(LayoutObjectIdAt(second.tree, index, &b), RoboOk);
        RoboCheckEqInt(LayoutTreeInvalidationState(first.tree, a),
                          LayoutTreeInvalidationState(second.tree, b));
        RoboCheckEqInt((int)LayoutTreeInvalidationReason(first.tree, a),
                          (int)LayoutTreeInvalidationReason(second.tree, b));
    }

    LayoutInvEnd(&first);
    LayoutInvEnd(&second);
}

static void TestInvAllocatorBalance(void)
{
    LayoutInvFixture fixture;
    LayoutInvRule rules[6];
    size_t baseline;
    size_t index;

    LayoutFixture();
    baseline = LayoutLiveBlocks();

    LayoutInvTreeRules(rules);
    LayoutInvBegin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    for (index = 0u; index < 8u; index++) {
        LayoutStats stats;

        if (index % 2u == 0u) {
            LayoutNodeId div = LayoutInvBoxFor(&fixture, "div");

            RoboCheckEqInt(LayoutTreeMarkLayoutDirty(
                                  fixture.tree, div, RoboLayoutInvalidationReasonGeometry,
                                  NULL),
                              RoboOk);
        } else {
            LayoutNodeId section = LayoutInvBoxFor(&fixture, "section");

            RoboCheckEqInt(LayoutTreeMarkOverflowDirty(fixture.tree, section, NULL),
                              RoboOk);
        }
        LayoutInvPass(&fixture, &stats);
    }
    LayoutInvEnd(&fixture);

    /* Nothing in this phase allocates per mark or per pass, which is the whole reason the
     * record lives on the box and the count lives on the tree. */
    RoboCheckEqInt(LayoutLiveBlocks(), baseline);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutInvalidation(void)
{
    TestRegister("layout.invalidation.vocabulary", TestInvVocabulary);
    TestRegister("layout.invalidation.classifier", TestInvClassifier);
    TestRegister("layout.invalidation.records", TestInvRecords);
    TestRegister("layout.invalidation.fresh_tree", TestInvFreshTree);
    TestRegister("layout.invalidation.pass_clears_layout_bits",
                       TestInvPassClearsLayoutBits);
    TestRegister("layout.invalidation.pass_without_viewport_clears_nothing",
                       TestInvPassWithoutViewportClearsNothing);
    TestRegister("layout.invalidation.mark_recomputed_leaves_style",
                       TestInvMarkRecomputedLeavesStyle);
    TestRegister("layout.invalidation.layout_dirty_propagation",
                       TestInvLayoutDirtyPropagation);
    TestRegister("layout.invalidation.position_dirty_is_narrow",
                       TestInvPositionDirtyIsNarrow);
    TestRegister("layout.invalidation.overflow_dirty_is_narrowest",
                       TestInvOverflowDirtyIsNarrowest);
    TestRegister("layout.invalidation.subtree_dirty", TestInvSubtreeDirty);
    TestRegister("layout.invalidation.style_effects", TestInvStyleEffects);
    TestRegister("layout.invalidation.mark_all", TestInvMarkAll);
    TestRegister("layout.invalidation.reason_rule", TestInvReasonRule);
    TestRegister("layout.invalidation.idempotence", TestInvIdempotence);
    TestRegister("layout.invalidation.refusals", TestInvRefusals);
    TestRegister("layout.invalidation.root_is_the_common_ancestor",
                       TestInvRootIsTheCommonAncestor);
    TestRegister("layout.invalidation.incremental_skips_a_clean_tree",
                       TestInvIncrementalSkipsACleanTree);
    TestRegister("layout.invalidation.incremental_runs_after_a_mark",
                       TestInvIncrementalRunsAfterAMark);
    TestRegister("layout.invalidation.incremental_runs_when_inputs_change",
                       TestInvIncrementalRunsWhenInputsChange);
    TestRegister("layout.invalidation.incremental_skips_a_paint_only_change",
                       TestInvIncrementalSkipsAPaintOnlyChange);
    TestRegister("layout.invalidation.relayout_moves_the_following_flow",
                       TestInvRelayoutMovesTheFollowingFlow);
    TestRegister("layout.invalidation.relayout_is_stable", TestInvRelayoutIsStable);
    TestRegister("layout.invalidation.determinism", TestInvDeterminism);
    TestRegister("layout.invalidation.allocator_balance", TestInvAllocatorBalance);
}
