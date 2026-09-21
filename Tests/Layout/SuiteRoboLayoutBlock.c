/*
 * RoboLayout block-layout suite (Phase 10.3) — layout.block.*
 *
 * The first suite in this directory that asserts a **number**, and the shape of it
 * is determined by that: while the module computed nothing, a test could only ask
 * whether a box had geometry at all, and asking *which* geometry was impossible.
 * Now the interesting assertions are arithmetic, and they are asserted exactly
 * rather than as "greater than zero", because a block algorithm's whole output is
 * arithmetic over a document.
 *
 * Five groups, in the order the phase is built:
 *
 *   the vocabulary   the predicate this module deliberately does *not* own, and the
 *                    stats record
 *
 *   placement        one box, three siblings, nesting -- the containing-block
 *                    convention, which is the one thing a wrong build gets wrong in
 *                    a way that still produces plausible numbers
 *
 *   heights          declared, `auto`, percentage, and a declared height that is
 *                    smaller than the content it holds
 *
 *   the gate         what block layout declines to place, and why -- inline-level
 *                    boxes in a *block-direction* context (from 10.4 they are
 *                    placed when they are in an inline one), `display: none`, and
 *                    the invariant that a block-level box is never in a non-block
 *                    context
 *
 *   the pass         determinism, invalidation, the real CSS pipeline, and the
 *                    allocator
 *
 * The fixtures are *scripted* styles rather than a stylesheet, except in
 * `layout.block.pipeline`: a case whose subject is arithmetic should fail because
 * the arithmetic is wrong and not because a selector did not match, and a case
 * whose subject is "the numbers came from CSS" should go through CSS.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A scripted style source with heights                                */
/* ------------------------------------------------------------------ */

/** One scripted answer: this tag computes to this display, and this height. */
typedef struct layoutBlockRule {
    const char *tag;
    CssDisplay display;
    /** Non-zero when this element declares `height: <heightPx>px`. */
    int hasHeight;
    double heightPx;
} LayoutBlockRule;

typedef struct layoutBlockStyle {
    char tag[16];
    CssComputedStyle *style; /* owned */
} LayoutBlockStyle;

#define LAYOUT_BLOCK_MAX_RULES 12u

typedef struct layoutBlockSource {
    LayoutBlockStyle rules[LAYOUT_BLOCK_MAX_RULES];
    size_t count;
} LayoutBlockSource;

/**
 * A real computed style carrying a real DISPLAY and, when asked for, a real
 * LENGTH.
 *
 * Built through 9.8's own maker and container rather than a hand-written struct,
 * for the reason the 10.1 fixture gives for the same thing: a fake that fabricated
 * the value would prove the pass reads a struct, not that it reads a computed CSS
 * value. The height goes in as a LENGTH in `px` because that is what 9.8's computed
 * rule produces for an absolute unit — which is exactly the shape the pass has to
 * recognise, and therefore the shape worth constructing.
 */
static void LayoutBlockStyleAdd(LayoutBlockSource *source,
                                   const LayoutBlockRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_BLOCK_MAX_RULES, 1);
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeDisplay(&value, rule->display, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("display"),
                                                       0u, &specified, &value, NULL),
                      RoboOk);
    if (rule->hasHeight) {
        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        RoboCheckEqInt(CssComputedValueMakeLength(&value, rule->heightPx,
                                                             RoboCssUnitPx, 0, NULL),
                          RoboOk);
        RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                           CssSliceFromText("height"),
                                                           0u, &specified, &value, NULL),
                          RoboOk);
    }
    strncpy(source->rules[source->count].tag, rule->tag,
            sizeof(source->rules[source->count].tag) - 1u);
    source->rules[source->count].tag[sizeof(source->rules[source->count].tag) - 1u] = '\0';
    source->rules[source->count].style = style;
    source->count++;
}

static const CssComputedStyle *LayoutBlockStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutBlockSource *source = (LayoutBlockSource *)userdata;
    const char *name;
    size_t i;

    if (!source || !element) {
        return NULL;
    }
    name = HtmlElementName(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (i = 0u; i < source->count; i++) {
        if (strcmp(source->rules[i].tag, name) == 0) {
            return source->rules[i].style;
        }
    }
    /* An element nothing declared for: the property's *initial* value, which is
     * `inline` and therefore not in the block flow. That is what makes a document's
     * geometry depend on the declarations the test wrote rather than on the
     * elements the parser synthesized. */
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

typedef struct layoutBlockFixture {
    LayoutDocument doc;
    LayoutBlockSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutBlockFixture;

/**
 * Set @p fixture up on a document the caller already built.
 *
 * The seam `LayoutBlockBegin` is written on top of, and it exists because one
 * case needs to *keep owning* its document: a parsed document is finalized by the
 * build, so an element can no longer be created in it, and a hand-bound one has no
 * such edge (RoboLayoutTestSupport.h). Everything else is shared, which is the
 * point -- a test that assembled its own tree and style source would be testing a
 * different builder from the one every other case uses.
 */
static void LayoutBlockBeginFrom(LayoutBlockFixture *fixture,
                                    const LayoutDocument *doc,
                                    const LayoutBlockRule *rules,
                                    size_t count)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    fixture->doc = *doc;
    for (index = 0u; index < count; index++) {
        LayoutBlockStyleAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutBlockStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutBlockBegin(LayoutBlockFixture *fixture,
                               const char *html,
                               const LayoutBlockRule *rules,
                               size_t count)
{
    LayoutDocument doc;

    LayoutDocumentBuild(&doc, html);
    LayoutBlockBeginFrom(fixture, &doc, rules, count);
}

static void LayoutBlockEnd(LayoutBlockFixture *fixture)
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
 * One pass with a viewport, which is what classifies *and* lays the tree out.
 *
 * A viewport of 800x600 throughout, so that a width in an assertion is a width the
 * test chose rather than one the display layer happened to have.
 */
#define LAYOUT_BLOCK_VIEWPORT_WIDTH 800
#define LAYOUT_BLOCK_VIEWPORT_HEIGHT 600

static void LayoutBlockPass(LayoutBlockFixture *fixture,
                              LayoutStats *outStats,
                              Result *outResult)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;
    Result status;

    viewport.width = LAYOUT_BLOCK_VIEWPORT_WIDTH;
    viewport.height = LAYOUT_BLOCK_VIEWPORT_HEIGHT;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    status = LayoutContextRun(context, fixture->tree, NULL);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);
    if (outStats) {
        *outStats = stats;
    }
    if (outResult) {
        *outResult = status;
    }
}

/** The box @p index of the tree, in creation order (which is document order). */
static LayoutNodeId LayoutBlockBoxAt(LayoutBlockFixture *fixture, size_t index)
{
    LayoutNodeId id = RoboLayoutNodeIdNone;

    RoboCheckEqInt(LayoutObjectIdAt(fixture->tree, index, &id), RoboOk);
    return id;
}

/** The box built for the element @p tag. */
static LayoutNodeId LayoutBlockBoxOf(LayoutBlockFixture *fixture,
                                                 const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    return LayoutObjectIdForDomNode(fixture->tree, &element);
}

/**
 * Assert one box's geometry, and that the geometry is *known*.
 *
 * The four numbers in one expression, because a case about stacking that asserted a
 * y and forgot a height would pass on a build that stacked correctly and sized
 * everything to zero.
 */
static void LayoutBlockExpect(LayoutBlockFixture *fixture,
                                LayoutNodeId id,
                                int32_t x,
                                int32_t y,
                                int32_t width,
                                int32_t height)
{
    LayoutResult result;
    LayoutPoint position;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    RoboCheckEqInt(position.x, x);
    RoboCheckEqInt(position.y, y);
    RoboCheckEqInt(size.width, width);
    RoboCheckEqInt(size.height, height);
    /*
     * The two overflow flags, and since 10.13 they are set -- by 10.13. That a box's
     * contents do not fit is a fact about contents, so the block pass does not decide it:
     * it hands the box to the overflow layer's measurement and copies that record's two
     * halves onto the result. What is asserted here is therefore the **agreement** rather
     * than the silence it used to be -- the flags and the record are one answer, and a
     * pass that set a bit the record did not justify would fail here.
     */
    {
        LayoutOverflowEdges overflow;
        Result status = LayoutObjectOverflow(fixture->tree, id, &overflow, NULL);
        int expected = 0;

        /* The door declines the two kinds of box that are not measured -- a text run and a
         * transparent inline box -- and *clears* the record when it does, so the fold below
         * yields no flags for them, which is the honest answer: a box with no measurement
         * cannot have an overflow flag. Nothing else may be refused. */
        RoboCheck(status == RoboOk || status == RoboNotFound);
        if (status != RoboOk) {
            LayoutOverflowEdgesInit(&overflow);
        }
        if (LayoutOverflowEdgesHasOverflowAxis(
                &overflow, RoboLayoutOverflowAxisHorizontal)) {
            expected |= RoboLayoutResultFlagOverflowX;
        }
        if (LayoutOverflowEdgesHasOverflowAxis(
                &overflow, RoboLayoutOverflowAxisVertical)) {
            expected |= RoboLayoutResultFlagOverflowY;
        }
        RoboCheckEqInt((int)(result.flags & RoboLayoutResultFlagAll), expected);
    }
}

static void LayoutBlockExpectUnresolved(LayoutBlockFixture *fixture,
                                           LayoutNodeId id)
{
    LayoutResult result;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultUnresolved);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboNotFound);
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * The phase adds no display predicate, and its absence is the assertion.
 *
 * 9.12 answers "is this block-level?" off its own vocabulary table. A second
 * predicate in RoboLayout would be a second place for one answer to live — and the
 * worse copy, because the day `flex` adds a row with an outer of `block`, 9.12's
 * table is where that fact lands. So what is asserted here is the *table the pass
 * reads*, row by row, on the module's own terms: which of the four display values
 * generate a box that participates in a block flow.
 */
static void TestBlockDisplayTable(void)
{
    LayoutFixture();

    RoboCheckEqInt(CssDisplayIsBlockLevel(RoboCssDisplayBlock), 1);
    RoboCheckEqInt(CssDisplayIsBlockLevel(RoboCssDisplayInline), 0);
    RoboCheckEqInt(CssDisplayIsBlockLevel(RoboCssDisplayInlineBlock), 0);
    RoboCheckEqInt(CssDisplayIsBlockLevel(RoboCssDisplayNone), 0);

    /* And the *other* half of the gate agrees with it, which is the invariant
     * `layout.block.context_gate` asserts over a real tree: an outer of `block` is
     * a block container, and the two displays that are not block-level are not. */
    RoboCheckEqInt(CssDisplayGeneratesBox(RoboCssDisplayBlock), 1);
    RoboCheckEqInt(CssDisplayGeneratesBox(RoboCssDisplayNone), 0);
    RoboCheckEqInt(MakeCssDisplayInner(RoboCssDisplayBlock),
                      RoboCssDisplayInnerFlow);
    RoboCheckEqInt(MakeCssDisplayInner(RoboCssDisplayInlineBlock),
                      RoboCssDisplayInnerFlowRoot);

    /*
     * `inline-block` is the value that separates the two questions, so it is worth
     * stating in the suite that reads them: it is *not* block-level (the box is laid
     * out along a line) while its inner box *is* a block container (its contents
     * stack). A pass that asked only one of the two questions would place it wrongly
     * in one direction or the other.
     */
    RoboCheckEqInt(LayoutFcDisplayIsBlockContainer(RoboCssDisplayInlineBlock),
                      1);
    RoboCheckEqInt(CssDisplayIsBlockLevel(RoboCssDisplayInlineBlock), 0);
}

static void TestBlockStats(void)
{
    LayoutBlockStats a;
    LayoutBlockStats b;

    LayoutFixture();

    LayoutBlockStatsInit(&a);
    RoboCheckEqInt(a.containers, 0u);
    RoboCheckEqInt(a.blocksPlaced, 0u);
    RoboCheckEqInt(a.inlineLevelSkipped, 0u);
    RoboCheckEqInt(a.unreached, 0u);
    RoboCheckEqInt(a.heightsExplicit, 0u);
    RoboCheckEqInt(a.heightsFromContent, 0u);
    RoboCheckEqInt(a.emptyContainers, 0u);
    RoboCheckEqInt(a.depthMax, 0u);

    b = a;
    RoboCheckEqInt(LayoutBlockStatsEqual(&a, &b), 1);
    b.blocksPlaced = 1u;
    RoboCheckEqInt(LayoutBlockStatsEqual(&a, &b), 0);
    /* Every field is part of the comparison, and the one below is the field a
     * hand-written equality is most likely to forget -- it was added last. */
    b = a;
    b.emptyContainers = 1u;
    RoboCheckEqInt(LayoutBlockStatsEqual(&a, &b), 0);
    b = a;
    b.depthMax = 1u;
    RoboCheckEqInt(LayoutBlockStatsEqual(&a, &b), 0);

    /* Safe on NULL, both ways round, and NULL is only equal to NULL. */
    RoboCheckEqInt(LayoutBlockStatsEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutBlockStatsEqual(NULL, &a), 0);
    LayoutBlockStatsInit(NULL);
}

/* ------------------------------------------------------------------ */
/* The pass's preconditions                                            */
/* ------------------------------------------------------------------ */

static void TestBlockPreconditions(void)
{
    LayoutBlockFixture fixture;
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 }
    };
    LayoutSize viewport;
    LayoutBlockStats stats;
    LayoutBlockStats zeros;
    LayoutInlineStats inlineStats;
    LayoutInlineStats inlineZeros;
    LayoutTextStats textStats;
    LayoutTextStats textZeros;
    LayoutDiag diag;

    LayoutFixture();
    LayoutBlockBegin(&fixture, "<html><body></body></html>", rules, 2u);
    viewport.width = LAYOUT_BLOCK_VIEWPORT_WIDTH;
    viewport.height = LAYOUT_BLOCK_VIEWPORT_HEIGHT;
    LayoutBlockStatsInit(&zeros);
    LayoutInlineStatsInit(&inlineZeros);
    LayoutTextStatsInit(&textZeros);

    /* An *argument* refusal happens before the output records are touched, so a
     * caller's stats are left exactly as they were rather than half-cleared -- all
     * three of them, which is why the inline and text records are asserted alongside
     * the block one. Since 10.5 the pass fills three, and "a refusal must not clear
     * what it did not answer" is one rule about three records.
     */
    stats = zeros;
    stats.blocksPlaced = 99u;
    inlineStats.participantsPlaced = 99u;
    textStats.runsPlaced = 99u;
    RoboCheckEqInt(LayoutBlockLayout(NULL, viewport, NULL, &stats, &inlineStats,
                                              &textStats, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(stats.blocksPlaced, 99u);
    RoboCheckEqInt(inlineStats.participantsPlaced, 99u);
    RoboCheckEqInt(textStats.runsPlaced, 99u);
    viewport.width = -1;
    RoboCheckEqInt(LayoutBlockLayout(fixture.tree, viewport, NULL, &stats,
                                               &inlineStats, &textStats, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(stats.blocksPlaced, 99u);
    RoboCheckEqInt(inlineStats.participantsPlaced, 99u);
    RoboCheckEqInt(textStats.runsPlaced, 99u);
    viewport.width = LAYOUT_BLOCK_VIEWPORT_WIDTH;

    /*
     * Unclassified: refused rather than repaired, and the refusal happens *after* the
     * output is cleared, because it is a statement about the tree rather than about the
     * arguments. The placement gate *is* the formatting context, so a pass that invented
     * "every block-level box" would be inventing the second classifier this module
     * refuses to have.
     */
    RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 0);
    RoboCheckEqInt(LayoutBlockLayout(fixture.tree, viewport, NULL, &stats,
                                               &inlineStats, &textStats, &diag),
                      RoboError);
    RoboCheckEqInt(LayoutBlockStatsEqual(&stats, &zeros), 1);
    RoboCheckEqInt(LayoutInlineStatsEqual(&inlineStats, &inlineZeros), 1);
    RoboCheckEqInt(LayoutTextStatsEqual(&textStats, &textZeros), 1);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainBlockLayout);
    RoboCheckStrEq(LayoutDiagnosticDomainName(diag.domain), "block-layout");

    LayoutBlockEnd(&fixture);

    /*
     * A document whose root element generates no box: there is nothing to place, and
     * that is an answer about the document rather than a failure. The pass over it
     * *is* legitimate and reports zero; a direct block-layout call answers
     * RoboNotFound, which is how the two doors differ.
     */
    {
        static const LayoutBlockRule hidden[] = {
            { "html", RoboCssDisplayNone, 0, 0.0 }
        };
        LayoutStats passStats;

        LayoutBlockBegin(&fixture, "<html><body></body></html>", hidden, 1u);
        RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 0u);
        RoboCheckEqInt(LayoutTreeRoot(fixture.tree), RoboLayoutNodeIdNone);

        LayoutBlockPass(&fixture, &passStats, NULL);
        RoboCheckEqInt(passStats.objects, 0u);
        RoboCheckEqInt(passStats.boxesComputed, 0u);
        RoboCheckEqInt(passStats.block.blocksPlaced, 0u);
        RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 1);
        RoboCheckEqInt(LayoutBlockLayout(fixture.tree, viewport, NULL, &stats, NULL,
                                                  NULL, NULL),
                          RoboNotFound);
        LayoutBlockEnd(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* Placement: the containing-block convention                          */
/* ------------------------------------------------------------------ */

static void TestBlockSingle(void)
{
    LayoutBlockFixture fixture;
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutBlockBegin(&fixture, "<html><body></body></html>", rules, 2u);

    /* html, head, body. `<head>` is the parser's and nothing declared for it, so it
     * computes to `inline` and is not in the block flow. */
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 3u);

    LayoutBlockPass(&fixture, &stats, NULL);

    /* The root fills the viewport and is placed at its origin; `<body>` is the
     * only block-level child, so it fills what html filled. Every height is zero
     * because nothing declares one and neither box has block-level content. */
    LayoutBlockExpect(&fixture, LayoutBlockBoxAt(&fixture, 0u), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 0);
    LayoutBlockExpect(&fixture, LayoutBlockBoxAt(&fixture, 2u), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 0);
    LayoutBlockExpectUnresolved(&fixture, LayoutBlockBoxAt(&fixture, 1u));

    RoboCheckEqInt(stats.objects, 3u);
    RoboCheckEqInt(stats.visited, 3u);
    RoboCheckEqInt(stats.boxesComputed, 2u);
    RoboCheckEqInt(stats.boxesUnresolved, 1u);
    RoboCheckEqInt(stats.block.blocksPlaced, 2u);
    RoboCheckEqInt(stats.block.inlineLevelSkipped, 1u);
    RoboCheckEqInt(stats.block.containers, 1u);      /* html placed body */
    RoboCheckEqInt(stats.block.emptyContainers, 1u); /* body has no children */
    RoboCheckEqInt(stats.block.unreached, 0u);
    RoboCheckEqInt(stats.block.heightsFromContent, 2u);
    RoboCheckEqInt(stats.block.heightsExplicit, 0u);
    /* The deepest *placed* box, which is `<body>` at depth 1: `<head>` is deeper
     * than nothing and is not placed, so it does not count. */
    RoboCheckEqInt(stats.block.depthMax, 1u);

    LayoutBlockEnd(&fixture);
}

static void TestBlockSiblings(void)
{
    LayoutBlockFixture fixture;
    /*
     * Three siblings with distinct declared heights, and the tags are distinct
     * because the scripted source answers per *tag*: a fixture that keyed off a class
     * would be exercising the selector matcher, and this case is about arithmetic.
     */
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 },
        { "p", RoboCssDisplayBlock, 1, 100.0 },
        { "h1", RoboCssDisplayBlock, 1, 50.0 },
        { "ul", RoboCssDisplayBlock, 1, 75.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutBlockBegin(&fixture, "<html><body><p></p><h1></h1><ul></ul></body></html>",
                       rules, 5u);
    /* html, head, body, p, h1, ul */
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 6u);

    LayoutBlockPass(&fixture, &stats, NULL);

    /* Each sibling below the last, each filling the containing block's width. The
     * cursor is the *only* thing that carries between them, which is why the three
     * y values are asserted rather than just the last. */
    LayoutBlockExpect(&fixture, LayoutBlockBoxAt(&fixture, 3u), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 100);
    LayoutBlockExpect(&fixture, LayoutBlockBoxAt(&fixture, 4u), 0, 100,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 50);
    LayoutBlockExpect(&fixture, LayoutBlockBoxAt(&fixture, 5u), 0, 150,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 75);
    /* `<body>`'s height is the extent its children reached, and html's is body's. */
    LayoutBlockExpect(&fixture, LayoutBlockBoxAt(&fixture, 2u), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 225);
    LayoutBlockExpect(&fixture, LayoutBlockBoxAt(&fixture, 0u), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 225);

    RoboCheckEqInt(stats.block.blocksPlaced, 5u);
    RoboCheckEqInt(stats.block.inlineLevelSkipped, 1u);
    RoboCheckEqInt(stats.block.heightsExplicit, 3u);
    RoboCheckEqInt(stats.block.heightsFromContent, 2u);
    RoboCheckEqInt(stats.block.containers, 2u);       /* html, body */
    RoboCheckEqInt(stats.block.emptyContainers, 3u); /* p, h1, ul */
    RoboCheckEqInt(stats.block.depthMax, 2u);
    RoboCheckEqInt(stats.block.unreached, 0u);

    LayoutBlockEnd(&fixture);
}

static void TestBlockNested(void)
{
    LayoutBlockFixture fixture;
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 },
        { "p", RoboCssDisplayBlock, 1, 30.0 },
        { "div", RoboCssDisplayBlock, 0, 0.0 },
        { "h1", RoboCssDisplayBlock, 1, 20.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutBlockBegin(&fixture,
                       "<html><body><p></p><div><h1></h1></div></body></html>",
                       rules, 5u);
    /* html, head, body, p, div, h1 */
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 6u);

    LayoutBlockPass(&fixture, &stats, NULL);

    /*
     * The assertion this case exists for. `<h1>` is at y = 0 — in the space of the
     * `<div>` that contains it — while the `<div>` is at y = 30 in the space of
     * `<body>`. A pass that wrote absolute coordinates would put `<h1>` at 30, and a
     * pass that ignored nesting would put it at 0 with the wrong parent.
     *
     * The document-space position is deliberately *not* asserted, because computing
     * it means walking the chain of containing blocks and that is 10.12's job: what
     * this phase fixes is that a result is stated relative to a box's containing
     * block, and a test that summed the chain here would be asserting a convention
     * the module has not promised.
     */
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "h1"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 20);
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "div"), 0, 30,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 20);
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "p"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 30);
    /* body's extent is 30 + the div's 20, and html's is body's. */
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 50);
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "html"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 50);

    RoboCheckEqInt(stats.block.blocksPlaced, 5u);
    RoboCheckEqInt(stats.block.containers, 3u);      /* html, body, div */
    RoboCheckEqInt(stats.block.emptyContainers, 2u); /* p, h1 */
    RoboCheckEqInt(stats.block.depthMax, 3u);
    /* The div's height came from its content and it has one child, so it is a
     * container; the div itself is not an empty container. */
    RoboCheckEqInt(stats.block.heightsFromContent, 3u);
    RoboCheckEqInt(stats.block.heightsExplicit, 2u);

    LayoutBlockEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Heights                                                             */
/* ------------------------------------------------------------------ */

/**
 * A declared height that is smaller than the content does not clip it, and does not
 * raise a flag.
 *
 * Both halves matter. The children are still placed — they are boxes and they have
 * geometry — and their extents run past their parent's, which is a *fact* rather
 * than an error: CSS says the content overflows, and which axis overflows is what
 * the result's two flags are for. They stay 0 because deciding it is 10.13's, and a
 * block pass that decided it here would be answering a question with a containing
 * block it does not have.
 */
static void TestBlockExplicitHeight(void)
{
    LayoutBlockFixture fixture;
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 },
        { "div", RoboCssDisplayBlock, 1, 10.0 },
        { "p", RoboCssDisplayBlock, 1, 30.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutBlockBegin(&fixture,
                       "<html><body><div><p></p><p></p></div></body></html>",
                       rules, 4u);

    LayoutBlockPass(&fixture, &stats, NULL);

    /* Two `<p>`s at 0 and 30 inside a `<div>` that declares 10. */
    LayoutBlockExpect(&fixture, LayoutBlockBoxAt(&fixture, 4u), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 30);
    LayoutBlockExpect(&fixture, LayoutBlockBoxAt(&fixture, 5u), 0, 30,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 30);
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "div"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 10);
    /* The parent's declared height is what propagates, not the content's 60. */
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 10);
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "html"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 10);

    RoboCheckEqInt(stats.block.heightsExplicit, 3u);
    RoboCheckEqInt(stats.block.heightsFromContent, 2u);

    /* And a fractional declared height is rounded rather than truncated, which is
     * the decision the implementation states: 10.5 has to become one of two
     * integers, and truncating would make every fractional height come out a pixel
     * short. */
    LayoutBlockEnd(&fixture);
    {
        static const LayoutBlockRule fractional[] = {
            { "html", RoboCssDisplayBlock, 0, 0.0 },
            { "body", RoboCssDisplayBlock, 0, 0.0 },
            { "p", RoboCssDisplayBlock, 1, 10.5 }
        };

        LayoutBlockBegin(&fixture, "<html><body><p></p></body></html>", fractional, 3u);
        LayoutBlockPass(&fixture, &stats, NULL);
        LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "p"), 0, 0,
                            LAYOUT_BLOCK_VIEWPORT_WIDTH, 11);
    }

    LayoutBlockEnd(&fixture);
}

/**
 * A height this build cannot resolve takes the `auto` path.
 *
 * `50%` needs a containing block height and `2em` needs a font; 9.10 keeps both in
 * their own kinds and units precisely so that a consumer can tell "I cannot resolve
 * this" from "this is ten pixels". The honest answer for both is the content's
 * extent — not a guess, and not a refusal of the whole document.
 */
/**
 * Replace @p tag's declared height with @p value, which is *moved* in.
 *
 * `set_entry` replaces an entry in place, so a case can declare a height once and
 * then vary what it *is* — and a second rule for the same tag would be invisible
 * anyway, because the lookup answers with the first match. That is the kind of
 * fixture bug that makes a test pass for a reason nobody wrote down, so this helper
 * exists rather than four near-copies of it.
 */
static void LayoutBlockSetHeight(LayoutBlockSource *source,
                                   const char *tag,
                                   CssComputedValue *value)
{
    CssValue specified = RoboCssCleared;
    size_t index;

    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            RoboCheckEqInt(CssComputedStyleSetEntry(
                                  source->rules[index].style, RoboCssPropertyIdNone,
                                  CssSliceFromText("height"), 0u, &specified, value,
                                  NULL),
                              RoboOk);
            return;
        }
    }
    RoboCheckMsg(0, "no rule for that tag");
}

/** Rebuild @p fixture's tree from the same document and the same style source. */
static void LayoutBlockRebuild(LayoutBlockFixture *fixture)
{
    LayoutBuildOptions options;

    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void TestBlockUnresolvableHeight(void)
{
    LayoutBlockFixture fixture;
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 },
        { "p", RoboCssDisplayBlock, 1, 40.0 },
        /* The `<div>` starts with no height entry at all: the presence check's case. */
        { "div", RoboCssDisplayBlock, 0, 0.0 }
    };
    LayoutBlockSource *source;
    LayoutStats stats;
    size_t index;

    LayoutFixture();
    LayoutBlockBegin(&fixture, "<html><body><div><p></p></div></body></html>", rules, 4u);
    source = &fixture.source;

    /* No entry at all: `auto`, which is the property's initial value. */
    LayoutBlockPass(&fixture, &stats, NULL);
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "div"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 40);
    RoboCheckEqInt(stats.block.heightsExplicit, 1u); /* only the `<p>` */
    RoboCheckEqInt(stats.block.heightsFromContent, 3u);

    /*
     * Then three declared heights this build cannot use, each failing a *different*
     * check: a relative unit fails the unit test, a percentage fails the kind test,
     * and a negative length passes both and is refused by the range test. All three
     * must produce the same 40 — the child's extent — so a build that guessed at any
     * of them shows up as a wrong number rather than as nothing.
     */
    for (index = 0u; index < 3u; index++) {
        CssComputedValue value = RoboCssCleared;

        if (index == 0u) {
            RoboCheckEqInt(CssComputedValueMakeLength(&value, 5.0,
                                                                 RoboCssUnitEm, 0, NULL),
                              RoboOk);
        } else if (index == 1u) {
            RoboCheckEqInt(CssComputedValueMakePercentage(&value, 50.0, 1, NULL),
                              RoboOk);
        } else {
            RoboCheckEqInt(CssComputedValueMakeLength(&value, -4.0,
                                                                 RoboCssUnitPx, 0, NULL),
                              RoboOk);
        }
        LayoutBlockSetHeight(source, "div", &value);
        LayoutBlockRebuild(&fixture);
        LayoutBlockPass(&fixture, &stats, NULL);

        LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "div"), 0, 0,
                            LAYOUT_BLOCK_VIEWPORT_WIDTH, 40);
        RoboCheckEqInt(stats.block.heightsExplicit, 1u); /* only the `<p>` */
        RoboCheckEqInt(stats.block.heightsFromContent, 3u);
    }

    /*
     * And a resolvable height still wins over the content, so the three refusals
     * above are about the *value* and not about the code path having been taken: 7
     * is neither 40 nor 0.
     */
    {
        CssComputedValue value = RoboCssCleared;

        RoboCheckEqInt(CssComputedValueMakeLength(&value, 7.0, RoboCssUnitPx,
                                                             0, NULL),
                          RoboOk);
        LayoutBlockSetHeight(source, "div", &value);
        LayoutBlockRebuild(&fixture);
        LayoutBlockPass(&fixture, &stats, NULL);
        LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "div"), 0, 0,
                            LAYOUT_BLOCK_VIEWPORT_WIDTH, 7);
        RoboCheckEqInt(stats.block.heightsExplicit, 2u);     /* `<div>` and `<p>` */
        RoboCheckEqInt(stats.block.heightsFromContent, 2u); /* html, body */
    }

    LayoutBlockEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The gate: what block layout declines to place                       */
/* ------------------------------------------------------------------ */

static void TestBlockInlineSkipped(void)
{
    LayoutBlockFixture fixture;
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 },
        { "p", RoboCssDisplayBlock, 1, 20.0 },
        /* An atomic inline: a box that belongs on a line, and whose own contents
         * would stack. It is *not* placed here -- and from 10.4 the reason is not
         * merely "it is inline-level" but "the context it participates in is the
         * root's, which is a block-direction one": 10.2 made `<body>` transparent
         * because it has a block-level child, so there is no line for the span to be
         * on and the inline direction never sees it. */
        { "span", RoboCssDisplayInlineBlock, 1, 30.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutBlockBegin(&fixture, "<html><body><p></p><span></span></body></html>",
                       rules, 4u);

    LayoutBlockPass(&fixture, &stats, NULL);

    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "p"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 20);
    LayoutBlockExpectUnresolved(&fixture, LayoutBlockBoxOf(&fixture, "span"));
    /* The inline-block contributes *nothing* to the cursor: `<p>` is at 0 and the
     * document's extent is 20, not 50. A build that measured an unplaced sibling
     * would stack it and report a height nobody computed. */
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 20);

    /* Three placed (html, body, p) and two declined: `<head>`, which nothing declared
     * for, and the atomic inline. */
    RoboCheckEqInt(stats.block.blocksPlaced, 3u);
    RoboCheckEqInt(stats.block.inlineLevelSkipped, 2u);
    RoboCheckEqInt(stats.boxesComputed, 3u);
    RoboCheckEqInt(stats.boxesUnresolved, 2u);
    RoboCheckEqInt(stats.block.unreached, 0u);

    LayoutBlockEnd(&fixture);
}

/**
 * A `display: none` element is not in the tree at all, and its *children* are.
 *
 * This is 10.1's rule, restated where a number now depends on it: the element
 * generates no box, so it cannot be placed, skipped or counted by the block pass —
 * it simply is not there. The child inside it *does* generate a box, whose parent
 * is the hidden element's parent, which is exactly the position 10.1's builder
 * gives it.
 */
static void TestBlockDisplayNone(void)
{
    LayoutBlockFixture fixture;
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 },
        { "div", RoboCssDisplayNone, 0, 0.0 },
        { "p", RoboCssDisplayBlock, 1, 25.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutBlockBegin(&fixture, "<html><body><p></p><div></div></body></html>",
                       rules, 4u);
    /* html, head, body, p — the div generates nothing. */
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 4u);
    {
        HtmlNodeHandle div = LayoutDocumentFind(&fixture.doc, "div");

        RoboCheckEqInt(HtmlNodeHandleIsWellformed(&div), 1);
        RoboCheckEqInt(LayoutObjectIdForDomNode(fixture.tree, &div),
                          RoboLayoutNodeIdNone);
    }

    LayoutBlockPass(&fixture, &stats, NULL);

    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "p"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 25);
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 25);
    RoboCheckEqInt(stats.objects, 4u);
    RoboCheckEqInt(stats.block.blocksPlaced, 3u);
    RoboCheckEqInt(stats.block.inlineLevelSkipped, 1u);
    RoboCheckEqInt(stats.block.unreached, 0u);

    LayoutBlockEnd(&fixture);
}

/**
 * The invariant the two phases have to agree on.
 *
 * Every box this pass declines for a *stated* reason is inline-level, and every box
 * it places is block-level in a block-direction context. Those two rules are stated
 * in different modules — 9.12's display vocabulary and 10.2's classifier — and the
 * interesting case is where they could disagree: a block-level box inside a box
 * that establishes an **inline** context. 10.2's rules make that impossible (a box
 * establishes an inline context only when it has *no* block-level child, and such a
 * box would be one), so the pass checks it rather than assuming it and this case
 * proves the check is never reached.
 *
 * The `p` with text in it is the case worth having: a block container whose only
 * content is a text node establishes an inline context, so the *text* is inline —
 * and the box itself is still a block in the flow, placed at its parent's origin.
 */
static void TestBlockContextGate(void)
{
    LayoutBlockFixture fixture;
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 },
        { "p", RoboCssDisplayBlock, 1, 18.0 },
        { "span", RoboCssDisplayInline, 0, 0.0 },
        { "em", RoboCssDisplayBlock, 1, 12.0 }
    };
    LayoutStats stats;
    size_t index;

    LayoutFixture();
    LayoutBlockBegin(&fixture,
                       "<html><body><p>text</p><span></span></body></html>",
                       rules, 5u);

    LayoutBlockPass(&fixture, &stats, NULL);

    /* `<p>` holds a text node, which is not a box: so it has no block-level child,
     * establishes an inline context, and is still placed *as a block*. */
    {
        LayoutNodeId pBox = LayoutBlockBoxOf(&fixture, "p");
        LayoutFcId owned = RoboLayoutFcIdNone;
        LayoutFormattingContext context;

        RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, pBox, &owned, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutTreeContext(fixture.tree, owned, &context, NULL),
                          RoboOk);
        RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcInline);
        LayoutBlockExpect(&fixture, pBox, 0, 0, LAYOUT_BLOCK_VIEWPORT_WIDTH, 18);
    }

    /*
     * And the invariant, over every box in the tree: a placed box is block-level in
     * a block-direction context, and a block-level box is never in any other kind.
     * Walked rather than asserted per case, because the claim is about *all* boxes
     * and a case that checked one would not be checking it.
     */
    for (index = 0u; index < LayoutTreeObjectCount(fixture.tree); index++) {
        LayoutNodeId id = LayoutBlockBoxAt(&fixture, index);
        LayoutResult result;
        LayoutFcId participating = RoboLayoutFcIdNone;
        LayoutFormattingContext context;
        CssDisplay display = LayoutObjectDisplay(fixture.tree, id);

        RoboCheckEqInt(LayoutObjectContext(fixture.tree, id, &participating, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutTreeContext(fixture.tree, participating, &context, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutObjectResult(fixture.tree, id, &result), RoboOk);

        if (CssDisplayIsBlockLevel(display)) {
            /* A block-level box is always in a block-direction context -- the claim. */
            RoboCheckEqInt(LayoutFcKindIsBlockContainer(context.kind), 1);
        } else if (LayoutFcKindIsBlockContainer(context.kind)) {
            /* And an inline-level box in a *block-direction* context is never placed
             * by this pass -- it is the anonymous-box case, and it is counted rather
             * than dropped. The mirror case, an inline-level box in an inline context,
             * is *not* asserted here: 10.4 places it, and that claim belongs to
             * layout.inline. */
            RoboCheckEqInt(result.state, RoboLayoutResultUnresolved);
        }
    }

    RoboCheckEqInt(stats.block.blocksPlaced, 3u);      /* html, body, p */
    RoboCheckEqInt(stats.block.inlineLevelSkipped, 2u); /* head, span */
    RoboCheckEqInt(stats.block.unreached, 0u);

    LayoutBlockEnd(&fixture);
    /*
     * And the *mirror* case, which 10.4 moved: a block-level box inside an inline
     * box. `<body>`'s only child is a `<span>`, so body has no block-level child and
     * 10.2 makes it establish an inline formatting context -- which means the `<em>`
     * inside the span is a block-level box participating in an *inline* context, and
     * 10.4 declines it for the same reason 10.3 declines the inline-level boxes
     * above: the anonymous block box CSS would give it is a box with no element and
     * no style, and this build makes none.
     *
     * The span itself moved the other way, and that is the half worth asserting: it
     * used to be `inlineLevelSkipped` and is now *placed* by the inline direction
     * with the zero extent its declined child left it. So the two records each
     * decline exactly one box and the identity below closes over both directions.
     */
    {
        static const LayoutBlockRule nested[] = {
            { "html", RoboCssDisplayBlock, 0, 0.0 },
            { "body", RoboCssDisplayBlock, 0, 0.0 },
            { "span", RoboCssDisplayInline, 0, 0.0 },
            { "em", RoboCssDisplayBlock, 1, 12.0 }
        };

        LayoutBlockBegin(&fixture, "<html><body><span><em></em></span></body></html>",
                           nested, 4u);
        LayoutBlockPass(&fixture, &stats, NULL);

        LayoutBlockExpectUnresolved(&fixture, LayoutBlockBoxOf(&fixture, "em"));
        LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "span"), 0, 0, 0, 0);
        RoboCheckEqInt(stats.block.blocksPlaced, 2u);      /* html, body */
        RoboCheckEqInt(stats.block.inlineLevelSkipped, 1u); /* head */
        RoboCheckEqInt(stats.block.unreached, 1u);          /* em */
        RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 1u); /* span */
        RoboCheckEqInt(stats.inlineFlow.blockLevelDeclined, 1u); /* em */
        RoboCheckEqInt(stats.boxesComputed, 3u);
        RoboCheckEqInt(stats.boxesUnresolved, 2u);
        /*
         * The accounting closes over both directions: every box is placed, declined by
         * one of the two, or unreached, and nothing is in two of those at once.
         * Asserted as an identity rather than as four numbers, because the claim is
         * that the pass *accounts for* the tree -- a build that dropped a box would
         * have right-looking counts and a document that did not add up.
         */
        RoboCheckEqInt(stats.block.blocksPlaced + stats.block.inlineLevelSkipped +
                              stats.inlineFlow.participantsPlaced + stats.block.unreached,
                          stats.objects);
    }

    LayoutBlockEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pass: determinism, invalidation, the real pipeline              */
/* ------------------------------------------------------------------ */

static void TestBlockNoViewport(void)
{
    LayoutBlockFixture fixture;
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 },
        { "p", RoboCssDisplayBlock, 1, 40.0 }
    };
    LayoutContext *context = NULL;
    LayoutStats stats;
    size_t index;

    LayoutFixture();
    LayoutBlockBegin(&fixture, "<html><body><p></p></body></html>", rules, 3u);

    /*
     * The precondition, stated rather than silently satisfied: the root box's
     * containing block is the initial containing block, which is the viewport, and
     * RoboLayout owns no window to ask. So a pass with no viewport classifies and
     * places nothing -- and says so in two independent places, which is what this
     * case checks.
     */
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context),
                      (int)RoboLayoutPassWalked);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.objects, 4u);
    RoboCheckEqInt(stats.boxesComputed, 0u);
    RoboCheckEqInt(stats.boxesUnresolved, 4u);
    RoboCheckEqInt(stats.block.blocksPlaced, 0u);
    RoboCheckEqInt(LayoutBlockStatsEqual(&stats.block, &(LayoutBlockStats){0}),
                      1);
    /* The classification still happened: the pass is not "half a pass", it is the
     * part of the work the input allowed. */
    RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 1);
    RoboCheckEqInt(stats.contexts > 0u, 1);
    for (index = 0u; index < 4u; index++) {
        LayoutBlockExpectUnresolved(&fixture, LayoutBlockBoxAt(&fixture, index));
    }
    LayoutContextDestroy(context);

    /* And the same tree, given a viewport, is laid out -- which is what makes the
     * zero above a statement about the *input* rather than about the build. */
    LayoutBlockPass(&fixture, &stats, NULL);
    RoboCheckEqInt(stats.boxesComputed, 3u);
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "p"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 40);

    LayoutBlockEnd(&fixture);
}

static void TestBlockDeterminism(void)
{
    LayoutBlockFixture fixture;
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 },
        { "p", RoboCssDisplayBlock, 1, 30.0 },
        { "div", RoboCssDisplayBlock, 1, 12.0 },
        { "h1", RoboCssDisplayBlock, 1, 7.0 }
    };
    LayoutStats first;
    LayoutStats second;
    LayoutResult before[8];
    size_t count;
    size_t index;

    LayoutFixture();
    LayoutBlockBegin(&fixture,
                       "<html><body><p></p><div><h1></h1></div></body></html>",
                       rules, 5u);
    count = LayoutTreeObjectCount(fixture.tree);
    RoboCheck(count <= 8u);

    /*
     * Two passes on **one** context, which is the point: a helper that made a fresh
     * context per call would prove the geometry is reproducible and say nothing about
     * whether a pass stacks on the one before it. The second run must not turn each
     * box's height into its own height plus its content's -- the failure a cumulative
     * pass has -- and the context's own `passes` count is what proves both runs were
     * the same context.
     */
    {
        LayoutContext *context = NULL;
        LayoutSize viewport;

        viewport.width = LAYOUT_BLOCK_VIEWPORT_WIDTH;
        viewport.height = LAYOUT_BLOCK_VIEWPORT_HEIGHT;
        RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
        RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
        RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
        LayoutContextStats(context, &first);
        for (index = 0u; index < count; index++) {
            LayoutNodeId id = LayoutBlockBoxAt(&fixture, index);

            RoboCheckEqInt(LayoutObjectResult(fixture.tree, id, &before[index]),
                              RoboOk);
        }

        RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
        LayoutContextStats(context, &second);
        RoboCheckEqInt((int)LayoutContextOutcome(context),
                          (int)RoboLayoutPassWalked);
        LayoutContextDestroy(context);
    }
    RoboCheckEqInt(first.passes, 1u);
    RoboCheckEqInt(second.passes, 2u);
    RoboCheckEqInt(LayoutBlockStatsEqual(&first.block, &second.block), 1);
    for (index = 0u; index < count; index++) {
        LayoutNodeId id = LayoutBlockBoxAt(&fixture, index);
        LayoutResult now;

        RoboCheckEqInt(LayoutObjectResult(fixture.tree, id, &now), RoboOk);
        RoboCheckEqInt(LayoutResultEqual(&before[index], &now), 1);
    }

    /*
     * And a second *tree* from the same document. Both borrow the same styles, so
     * this is determinism of the algorithm rather than of the build: the geometry
     * must be equal box for box, not merely equal in count.
     */
    {
        LayoutBlockFixture twin;
        LayoutStats twinStats;

        memset(&twin, 0, sizeof(twin));
        LayoutDocumentBuild(&twin.doc, "<html><body><p></p><div><h1></h1></div></body></html>");
        twin.styles = fixture.styles;
        RoboCheckEqInt(LayoutTreeCreate(&twin.tree, NULL), RoboOk);
        {
            LayoutBuildOptions options;

            LayoutBuildOptionsInit(&options);
            options.styles = &twin.styles;
            RoboCheckEqInt(LayoutTreeBuildFromDocument(
                                  twin.tree, twin.doc.id, &options, NULL, NULL),
                              RoboOk);
        }
        LayoutBlockPass(&twin, &twinStats, NULL);
        RoboCheckEqInt(LayoutBlockStatsEqual(&first.block, &twinStats.block), 1);
        RoboCheckEqInt(LayoutTreeObjectCount(twin.tree), count);
        for (index = 0u; index < count; index++) {
            LayoutNodeId id = LayoutBlockBoxAt(&twin, index);
            LayoutResult now;

            RoboCheckEqInt(LayoutObjectResult(twin.tree, id, &now), RoboOk);
            RoboCheckEqInt(LayoutResultEqual(&before[index], &now), 1);
        }
        LayoutTreeDestroy(twin.tree);
        LayoutDocumentRelease(&twin.doc);
    }

    LayoutBlockEnd(&fixture);
}

/**
 * Geometry is derived from the classification, so releasing the classification
 * releases it.
 *
 * The finding this case was written for: before it, a result stayed `COMPUTED`
 * across an invalidation, holding coordinates computed from a classification that
 * no longer existed -- and `COMPUTED` is precisely the state that is not allowed to
 * mean "true a moment ago". Both doors that invalidate are exercised, because they
 * are the same door and a fix in one of them would leave the other lying.
 */
static void TestBlockInvalidation(void)
{
    LayoutBlockFixture fixture;
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 },
        { "p", RoboCssDisplayBlock, 1, 30.0 },
        /* The tag the second door adds later: a style waiting for an element that
         * does not exist yet, which is the shape the scripted source is built for. */
        { "section", RoboCssDisplayBlock, 1, 30.0 }
    };
    LayoutStats stats;
    LayoutNodeId added = RoboLayoutNodeIdNone;
    LayoutObjectInfo info;
    LayoutResult freshResult;
    LayoutSize freshSize;
    HtmlNodeHandle body;
    HtmlNodeHandle section;

    /*
     * The document is assembled through the DOM seam rather than parsed, and that is
     * load-bearing rather than taste: this case's second door needs an element that
     * appears *after* the tree was built, and every parsed document is finalized by
     * the build -- which closes the construction gate `HtmlElementCreate`
     * writes through. A seam-built document is never finalized, so it can keep
     * growing while the tree built from it is being tested.
     *
     * `<head>` is bound by hand because the parser synthesizes it, and leaving it
     * out would quietly change what this case covers: it is the box the block pass
     * *declines* (nothing styles it, so it computes to `inline`), and the first pass
     * below asserts three placed out of four for that reason.
     */
    LayoutFixture();
    {
        LayoutDocument doc;
        HtmlNodeHandle documentRoot;
        HtmlNodeHandle html;

        LayoutDocumentCreateEmpty(&doc);
        documentRoot = HtmlDocumentRoot(doc.id);
        html = LayoutDocumentAddElement(&doc, &documentRoot, "html");
        (void)LayoutDocumentAddElement(&doc, &html, "head");
        body = LayoutDocumentAddElement(&doc, &html, "body");
        (void)LayoutDocumentAddElement(&doc, &body, "p");
        LayoutBlockBeginFrom(&fixture, &doc, rules, 4u);
    }

    LayoutBlockPass(&fixture, &stats, NULL);
    RoboCheckEqInt(stats.block.blocksPlaced, 3u);
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 30);

    /* Door one: an explicit invalidation. */
    LayoutTreeInvalidateContexts(fixture.tree);
    RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 0);
    {
        size_t index;

        for (index = 0u; index < 4u; index++) {
            LayoutBlockExpectUnresolved(&fixture, LayoutBlockBoxAt(&fixture, index));
        }
    }

    /* Re-running recalculates, so the release is not destructive. */
    LayoutBlockPass(&fixture, &stats, NULL);
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 30);

    /*
     * Door two: adding a box. It invalidates the classification implicitly, because
     * a new child changes the composition one of 10.2's three inputs is made of --
     * and therefore changes every height above it, which is why the *geometry* has to
     * go with it.
     *
     * The element the box models is created here, after the tree exists, because that
     * is the situation: a document that grew a node and a tree that has to be told
     * about it. Its style comes from the same scripted source the builder reads, so
     * the box is the box the next build would have made -- one declaration, thirty
     * pixels, nothing this door invents.
     */
    section = LayoutDocumentAddElement(&fixture.doc, &body, "section");

    LayoutObjectInfoInit(&info);
    info.parent = LayoutBlockBoxOf(&fixture, "body");
    info.domNode = section;
    info.display = RoboCssDisplayBlock;
    info.style = LayoutBlockStyleLookup(&fixture.source, &section);
    RoboCheck(info.style != NULL);
    RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &added, NULL),
                      RoboOk);
    RoboCheck(LayoutObjectIsLive(fixture.tree, added));
    RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 0);

    /*
     * And its state is NONE, where the four boxes above read UNRESOLVED -- the
     * distinction the two states exist to make and the one a blunt release would
     * erase.
     *
     * The four were COMPUTED a moment ago: they *had* geometry and it has been
     * withdrawn, so the honest answer is "a box exists, nobody has computed it". A
     * box that has just been added never had geometry to withdraw, and if the
     * release manufactured UNRESOLVED for it then NONE would be a state no path in
     * this module produces -- a header documenting a value nothing can return. Both
     * answers say "I cannot tell you where this box is"; only one of them says
     * whether anybody ever could. */
    RoboCheckEqInt(LayoutObjectResult(fixture.tree, added, &freshResult), RoboOk);
    RoboCheckEqInt((int)freshResult.state, (int)RoboLayoutResultNone);
    RoboCheckEqInt(LayoutResultIsComputed(&freshResult), 0);
    RoboCheckEqInt(LayoutResultSize(&freshResult, &freshSize), RoboNotFound);
    /* The four it did not touch are still UNRESOLVED, read back one apiece -- so the
     * conditional is exercised on both of its branches in one case rather than in
     * two suites that could drift apart. */
    {
        size_t index;

        for (index = 0u; index < 4u; index++) {
            LayoutBlockExpectUnresolved(&fixture, LayoutBlockBoxAt(&fixture, index));
        }
    }

    /* And a fresh pass sees the new box: body's extent is 30 + 30. */
    LayoutBlockPass(&fixture, &stats, NULL);
    RoboCheckEqInt(stats.objects, 5u);
    RoboCheckEqInt(stats.block.blocksPlaced, 4u);
    LayoutBlockExpect(&fixture, added, 0, 30, LAYOUT_BLOCK_VIEWPORT_WIDTH, 30);
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 60);
    LayoutBlockExpect(&fixture, LayoutBlockBoxOf(&fixture, "html"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 60);

    LayoutBlockEnd(&fixture);
}

/**
 * The numbers came from CSS.
 *
 * A real stylesheet through the real cascade, and the assertions are chosen to say
 * what each phase reads and what it deliberately does not: `height` is honoured,
 * `width` was *not* until 10.8 and `margin` was *not* until 10.9, while
 * `padding`/`border` still change nothing. Each of the ignored declarations is the
 * reason a capability field is 0, and putting them in the same document as the
 * honoured ones is what makes that visible in one place.
 */
static void TestBlockPipeline(void)
{
    LayoutDocument doc;
    LayoutCssFixture css;
    LayoutDom *dom = NULL;
    LayoutTree *tree = NULL;
    LayoutStyleSource source;
    LayoutBuildOptions options;
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;
    HtmlNodeHandle card;
    LayoutNodeId cardBox;

    LayoutFixture();
    LayoutDocumentBuild(&doc,
                          "<html>\n"
                          "  <body>\n"
                          "    <div class=\"card\"><p>Hello</p></div>\n"
                          "  </body>\n"
                          "</html>\n");
    LayoutCssBegin(&css);
    LayoutCssSheet(&css,
                     "html, body, p { display: block }\n"
                     ".card {\n"
                     "  display: block;\n"
                     "  height: 100px;\n"
                     "  width: 200px;\n"
                     "  margin: 10px 20px;\n"
                     "  padding: 5px;\n"
                     "  border: 4px solid black;\n"
                     "  position: relative;\n"
                     "  top: 12px;\n"
                     "}\n");
    RoboCheckEqInt(LayoutDomCreate(css.context, doc.id, &dom, NULL), RoboOk);
    RoboCheckEqInt(LayoutDomRefresh(dom, NULL), RoboOk);
    RoboCheckEqInt(LayoutDomStyleSource(dom, &source, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &source;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, &options, NULL, NULL),
                      RoboOk);

    /* html, head, body, div.card, p -- five boxes, plus the text object `<p>`'s text
     * node generates (10.5). That object is inline-level and the paragraph owns an
     * inline context, so the inline pass places it; with no metrics source in this
     * case it measures zero, which is why the extents below are unchanged. */
    RoboCheckEqInt(LayoutTreeObjectCount(tree), 6u);

    viewport.width = LAYOUT_BLOCK_VIEWPORT_WIDTH;
    viewport.height = LAYOUT_BLOCK_VIEWPORT_HEIGHT;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt((int)LayoutContextOutcome(context),
                      (int)RoboLayoutPassWalked);

    card = LayoutDocumentFind(&doc, "div");
    cardBox = LayoutObjectIdForDomNode(tree, &card);
    {
        LayoutResult result;
        LayoutPoint position;
        LayoutSize size;

        RoboCheckEqInt(LayoutObjectResult(tree, cardBox, &result), RoboOk);
        RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
        RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
        RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);

        /*
         * `height: 100px` and `width: 200px` reach the result -- and since 10.11 the
         * numbers here are the card's **border** box, because `padding: 5px` puts five on
         * each of its four sides *and* `border: 4px solid black` puts four more outside
         * those. This case is where 10.3 recorded that a declared width was **ignored**
         * ("a block box fills its containing block, and resolving a declared width is
         * 10.8"), where 10.8 recorded it being honoured, and where 10.10 moved the number
         * by the padding. 10.11 is the fourth phase to move it and the **last**: the box
         * model is complete, so `width: 200px` is the *content* box under this build's
         * `content-box` model and the box a reader sees is 218 by 118 -- 200 + 10 of
         * padding + 8 of border, and 100 + 10 + 8. A build that had read the declaration
         * as a border box would still give 200 and 100 here, which is why the pair is worth
         * writing down.
         */
        RoboCheckEqInt(size.height, 118);
        RoboCheckEqInt(size.width, 218);
        /*
         * `margin: 10px 20px` reaches geometry since 10.9, and neither `border` nor
         * `padding` ever touches this number: the box is at its parent's content origin
         * **plus its own margins**. A margin is a declaration about the gap beside a box, so
         * it moves the box and leaves the size alone; a padding and a border are declarations
         * about the space inside one, so they change the size and leave the flow position
         * alone. And since 10.12 the card also declares `position: relative; top: 12px`, so
         * the **final** position is that flow position displaced -- y is 10 + 12 = 22 while
         * x keeps the margins' 20 (`left`/`right` are absent, and an absent side displaces
         * nothing). The displacement adds outside the four assertions above it: the size is
         * the border box still, and the following sibling still starts where the *flow*
         * position says.
         */
        RoboCheckEqInt(position.x, 20);
        RoboCheckEqInt(position.y, 22);
    }
    /* `<p>` is a block inside the card and declares no height, so the card's extent
     * would be 0 -- and the declared 100 is what propagates up the chain. */
    RoboCheckEqInt(stats.block.blocksPlaced, 4u);
    RoboCheckEqInt(stats.block.heightsExplicit, 1u);
    RoboCheckEqInt(stats.block.heightsFromContent, 3u);

    /*
     * The accounting closes here too, and it is worth asserting on a document whose
     * box count the *parser* decided (it synthesizes `<head>`) rather than one the
     * fixture wrote.
     *
     * The four terms are the four answers to "why has this object no geometry?", and
     * the fourth is 10.4's: a participant the *inline* pass placed is explained by that
     * pass rather than by this one. `<p>`'s text run is exactly such an object, which
     * is why the sum omits it and still has to add up -- a build that dropped it from
     * the identity would have six objects and five explanations.
     */
    RoboCheckEqInt(stats.block.blocksPlaced + stats.block.inlineLevelSkipped +
                          stats.inlineFlow.participantsPlaced + stats.block.unreached,
                      stats.objects);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 1u);
    RoboCheckEqInt(stats.objects, 6u);

    LayoutContextDestroy(context);
    LayoutTreeDestroy(tree);
    LayoutDomDestroy(dom);
    LayoutCssEnd(&css);
    LayoutDocumentRelease(&doc);
}

static void TestBlockAllocatorBalance(void)
{
    LayoutBlockFixture fixture;
    static const LayoutBlockRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0 },
        { "p", RoboCssDisplayBlock, 1, 30.0 },
        { "div", RoboCssDisplayBlock, 1, 12.0 }
    };
    size_t before;
    int pass;

    LayoutFixture();
    before = LayoutLiveBlocks();

    for (pass = 0; pass < 8; pass++) {
        LayoutStats stats;

        LayoutBlockBegin(&fixture,
                           "<html><body><p></p><div></div></body></html>", rules, 4u);
        LayoutBlockPass(&fixture, &stats, NULL);
        LayoutBlockPass(&fixture, &stats, NULL);
        LayoutBlockEnd(&fixture);
    }

    RoboCheckEqInt(LayoutLiveBlocks(), before);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutBlock(void);

void TestRegisterLayoutBlock(void)
{
    TestRegister("layout.block.display_table", TestBlockDisplayTable);
    TestRegister("layout.block.stats", TestBlockStats);
    TestRegister("layout.block.preconditions", TestBlockPreconditions);
    TestRegister("layout.block.single", TestBlockSingle);
    TestRegister("layout.block.siblings", TestBlockSiblings);
    TestRegister("layout.block.nested", TestBlockNested);
    TestRegister("layout.block.explicit_height", TestBlockExplicitHeight);
    TestRegister("layout.block.unresolvable_height", TestBlockUnresolvableHeight);
    TestRegister("layout.block.inline_skipped", TestBlockInlineSkipped);
    TestRegister("layout.block.display_none", TestBlockDisplayNone);
    TestRegister("layout.block.context_gate", TestBlockContextGate);
    TestRegister("layout.block.no_viewport", TestBlockNoViewport);
    TestRegister("layout.block.determinism", TestBlockDeterminism);
    TestRegister("layout.block.invalidation", TestBlockInvalidation);
    TestRegister("layout.block.pipeline", TestBlockPipeline);
    TestRegister("layout.block.allocator_balance", TestBlockAllocatorBalance);
}
