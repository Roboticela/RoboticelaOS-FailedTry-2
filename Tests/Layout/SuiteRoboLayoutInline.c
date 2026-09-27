/*
 * RoboLayout inline-layout suite (Phase 10.4) — layout.inline.*
 *
 * The second suite in this directory that asserts a number, and the numbers it
 * asserts are *line* numbers: a cursor that advances, a line that moves down when a
 * participant does not fit, and a union that grows as a transparent inline box's
 * contents are placed.
 *
 * Five groups, in the order the phase is built:
 *
 *   the vocabulary   the stats record, and the one display predicate this module
 *                    deliberately does not own
 *
 *   placement        atomics on a line, the cursor arithmetic, and the union that a
 *                    non-atomic inline box *is*
 *
 *   wrapping         the cursor returning to the line start, and the two cases that
 *                    must not wrap -- a box wider than its line, and a transparent
 *                    box, which is not a unit and wraps inside itself instead
 *
 *   nesting          a transparent box inside a transparent box, which is where the
 *                    coordinate convention (a result is in its *containing block's*
 *                    space) has to survive a rebase
 *
 *   the pass         determinism, invalidation, a viewport that changes the answer,
 *                    the real CSS pipeline, and the allocator
 *
 * The fixtures are *scripted* styles rather than a stylesheet, except in
 * `layout.inline.pipeline`: a case whose subject is cursor arithmetic should fail
 * because the arithmetic is wrong and not because a selector did not match, and a
 * case whose subject is "these numbers came from CSS" should go through CSS.
 *
 * One fact about every fixture below, because it is what makes the assertions
 * readable: a block box fills its containing block (10.3), so the *line width* an
 * inline formatting context gets is the viewport width. A case that wants a wrap
 * asks for a narrow viewport rather than for a declared width, because a declared
 * width is not read in the block direction until 10.8.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A scripted style source with sizes                                  */
/* ------------------------------------------------------------------ */

/** One scripted answer: this tag computes to this display, and these declarations.
 *
 *  `hasWidth`/`hasHeight` are separate from the numbers because the difference
 *  between "declares 0px" and "declares nothing" is exactly the difference between
 *  a box that is zero-sized on purpose and a box whose size comes from somewhere
 *  else. A fixture that conflated them could not test either. */
typedef struct layoutInlineRule {
    const char *tag;
    CssDisplay display;
    int hasWidth;
    double widthPx;
    int hasHeight;
    double heightPx;
} LayoutInlineRule;

typedef struct layoutInlineStyle {
    char tag[16];
    CssComputedStyle *style; /* owned */
} LayoutInlineStyle;

#define LAYOUT_INLINE_MAX_RULES 16u

typedef struct layoutInlineSource {
    LayoutInlineStyle rules[LAYOUT_INLINE_MAX_RULES];
    size_t count;
} LayoutInlineSource;

/**
 * A real computed style carrying a real DISPLAY and, when asked for, real
 * LENGTHs.
 *
 * Built through 9.8's own maker and container rather than a hand-written struct,
 * for the reason the 10.1 fixture gives for the same thing: a fake that fabricated
 * the value would prove the pass reads a struct, not that it reads a computed CSS
 * value. Both lengths go in as LENGTHs in `px` because that is what 9.8's computed
 * rule produces for an absolute unit -- which is exactly the shape the pass has to
 * recognise, and therefore the shape worth constructing.
 */
static void LayoutInlineStyleAdd(LayoutInlineSource *source,
                                    const LayoutInlineRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    size_t existing;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_INLINE_MAX_RULES, 1);

    /*
     * Two rules for one tag is refused here rather than left to the lookup, which
     * answers with the first match -- so a case that declared a tag twice would
     * quietly test the *earlier* rule and pass or fail for a reason its own text
     * does not state. A fixture is a script, and a script with two lines for one
     * element is a mistake in the test, not a document.
     */
    for (existing = 0u; existing < source->count; existing++) {
        RoboCheck(strcmp(source->rules[existing].tag, rule->tag) != 0);
    }
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeDisplay(&value, rule->display, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("display"),
                                                       0u, &specified, &value, NULL),
                      RoboOk);

    if (rule->hasWidth) {
        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        RoboCheckEqInt(CssComputedValueMakeLength(&value, rule->widthPx,
                                                             RoboCssUnitPx, 0, NULL),
                          RoboOk);
        RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                           CssSliceFromText("width"),
                                                           0u, &specified, &value, NULL),
                          RoboOk);
    }
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

static const CssComputedStyle *LayoutInlineStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutInlineSource *source = (LayoutInlineSource *)userdata;
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
     * elements the parser synthesized -- and it is why the parser's `<head>` is an
     * inline box this build declines. */
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

typedef struct layoutInlineFixture {
    LayoutDocument doc;
    LayoutInlineSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutInlineFixture;

static void LayoutInlineBegin(LayoutInlineFixture *fixture,
                                const char *html,
                                const LayoutInlineRule *rules,
                                size_t count)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < count; index++) {
        LayoutInlineStyleAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutInlineStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutInlineEnd(LayoutInlineFixture *fixture)
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

/** One pass with a viewport, and both records reported. */
static void LayoutInlinePassWith(LayoutInlineFixture *fixture,
                                    int32_t viewportWidth,
                                    LayoutStats *outStats,
                                    Result *outResult)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;
    Result status;

    viewport.width = viewportWidth;
    viewport.height = 600;
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

/** The default: the viewport every case gets unless it is testing a wrap. */
#define LAYOUT_INLINE_WIDE_VIEWPORT 800

static void LayoutInlinePass(LayoutInlineFixture *fixture, LayoutStats *outStats)
{
    LayoutInlinePassWith(fixture, LAYOUT_INLINE_WIDE_VIEWPORT, outStats, NULL);
}

/** The box @p index of the tree, in creation order (which is document order). */
static LayoutNodeId LayoutInlineBoxAt(LayoutInlineFixture *fixture, size_t index)
{
    LayoutNodeId id = RoboLayoutNodeIdNone;

    RoboCheckEqInt(LayoutObjectIdAt(fixture->tree, index, &id), RoboOk);
    return id;
}

/** The box built for the element @p tag. */
static LayoutNodeId LayoutInlineBoxOf(LayoutInlineFixture *fixture,
                                                  const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    return LayoutObjectIdForDomNode(fixture->tree, &element);
}

/** Assert one box's geometry, and that the geometry is *known*. */
static void LayoutInlineExpectIn(const LayoutTree *tree,
                                    LayoutNodeId id,
                                    int32_t x,
                                    int32_t y,
                                    int32_t width,
                                    int32_t height)
{
    LayoutResult result;
    LayoutPoint position;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    RoboCheckEqInt(position.x, x);
    RoboCheckEqInt(position.y, y);
    RoboCheckEqInt(size.width, width);
    RoboCheckEqInt(size.height, height);
    RoboCheckEqInt(result.flags & RoboLayoutResultFlagAll, 0);
}

/** The same, for a case that holds a raw tree rather than a fixture -- the real
 *  CSS pipeline, whose boxes are the bridge's and whose styles are the cascade's. */
static void LayoutInlineExpect(LayoutInlineFixture *fixture,
                                 LayoutNodeId id,
                                 int32_t x,
                                 int32_t y,
                                 int32_t width,
                                 int32_t height)
{
    LayoutInlineExpectIn(fixture->tree, id, x, y, width, height);
}

static void LayoutInlineExpectUnresolved(LayoutInlineFixture *fixture,
                                            LayoutNodeId id)
{
    LayoutResult result;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultUnresolved);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboNotFound);
}

/*
 * The shape every case below shares: `<html><body><p>` where the container that
 * owns the inline formatting context is the `<p>`, and everything inside it is the
 * test's subject. Naming it once keeps each case's markup about its own subject.
 */
#define LAYOUT_INLINE_BLOCK_RULES                          \
    { "html", RoboCssDisplayBlock, 0, 0.0, 0, 0.0 },    \
    { "body", RoboCssDisplayBlock, 0, 0.0, 0, 0.0 },    \
    { "p", RoboCssDisplayBlock, 0, 0.0, 0, 0.0 }

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestInlineStats(void)
{
    LayoutInlineStats a;
    LayoutInlineStats b;

    LayoutFixture();

    LayoutInlineStatsInit(&a);
    RoboCheckEqInt(a.contexts, 0u);
    RoboCheckEqInt(a.emptyContexts, 0u);
    RoboCheckEqInt(a.participantsPlaced, 0u);
    RoboCheckEqInt(a.atomicPlaced, 0u);
    RoboCheckEqInt(a.nonAtomicPlaced, 0u);
    RoboCheckEqInt(a.blockLevelDeclined, 0u);
    RoboCheckEqInt(a.lines, 0u);
    RoboCheckEqInt(a.wraps, 0u);
    RoboCheckEqInt(a.widthsDeclared, 0u);
    RoboCheckEqInt(a.widthsFilled, 0u);
    RoboCheckEqInt(a.heightsDeclared, 0u);
    RoboCheckEqInt(a.heightsFromContent, 0u);
    RoboCheckEqInt(a.depthMax, 0u);

    b = a;
    RoboCheckEqInt(LayoutInlineStatsEqual(&a, &b), 1);

    /* Every field is part of the comparison, and the ones below are the fields a
     * hand-written equality is most likely to forget -- the last one added, and the
     * one whose name sits closest to another's. */
    b = a;
    b.wraps = 1u;
    RoboCheckEqInt(LayoutInlineStatsEqual(&a, &b), 0);
    b = a;
    b.widthsFilled = 1u;
    RoboCheckEqInt(LayoutInlineStatsEqual(&a, &b), 0);
    b = a;
    b.blockLevelDeclined = 1u;
    RoboCheckEqInt(LayoutInlineStatsEqual(&a, &b), 0);
    b = a;
    b.depthMax = 3u;
    RoboCheckEqInt(LayoutInlineStatsEqual(&a, &b), 0);
    b = a;
    b.emptyContexts = 1u;
    RoboCheckEqInt(LayoutInlineStatsEqual(&a, &b), 0);

    /* Safe on NULL, both ways round, and NULL is only equal to NULL. */
    RoboCheckEqInt(LayoutInlineStatsEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutInlineStatsEqual(NULL, &a), 0);
    LayoutInlineStatsInit(NULL);
}

/**
 * The display predicate this module deliberately does *not* own.
 *
 * The pass reads "is this inline-level?" from 9.12's vocabulary table, exactly as
 * 10.3 reads "is this block-level?" from the same table, and the *second* half of
 * the atomic question -- did this box establish a context of its own? -- comes from
 * 10.2's classifier. So the table asserted here is the one the phase reads, and the
 * rows that matter are the two inline-level ones, because they are the two that
 * end up on a line and they are not the same kind of thing:
 *
 *   `inline`        inline-level, establishes nothing  -> transparent to the line
 *   `inline-block`  inline-level, inner `flow-root`    -> atomic on the line
 *
 * A build that read only the outer display would treat both as one kind and get one
 * of them wrong in a way that still produces plausible numbers.
 */
static void TestInlineDisplayTable(void)
{
    LayoutFixture();

    RoboCheckEqInt(CssDisplayIsInlineLevel(RoboCssDisplayInline), 1);
    RoboCheckEqInt(CssDisplayIsInlineLevel(RoboCssDisplayInlineBlock), 1);
    RoboCheckEqInt(CssDisplayIsInlineLevel(RoboCssDisplayBlock), 0);
    RoboCheckEqInt(CssDisplayIsInlineLevel(RoboCssDisplayNone), 0);

    RoboCheckEqInt(MakeCssDisplayInner(RoboCssDisplayInline),
                      RoboCssDisplayInnerFlow);
    RoboCheckEqInt(MakeCssDisplayInner(RoboCssDisplayInlineBlock),
                      RoboCssDisplayInnerFlowRoot);

    /*
     * And 10.2's classification says the same thing about the same two values, from
     * the other side: a plain block container with nothing block-level in it
     * establishes an INLINE context, and an `inline-block` establishes one of its
     * own whatever it holds. Those are the two facts the verdict in the pass is
     * built from.
     */
    RoboCheckEqInt(LayoutFcEstablishes(RoboCssDisplayBlock, 0, 0, 0, 0, 0),
                      RoboLayoutFcInline);
    RoboCheckEqInt(LayoutFcEstablishes(RoboCssDisplayInlineBlock, 0, 0, 0, 0, 0),
                      RoboLayoutFcBlock);
    RoboCheckEqInt(LayoutFcEstablishes(RoboCssDisplayInline, 0, 0, 0, 0, 0),
                      RoboLayoutFcNone);
    RoboCheckEqInt(LayoutFcDisplayIsBlockContainer(RoboCssDisplayInline), 0);
    RoboCheckEqInt(LayoutFcDisplayIsBlockContainer(RoboCssDisplayInlineBlock),
                      1);
}

/**
 * A document with no inline participant at all still visits its contexts.
 *
 * `<p></p>` is a block container with no block-level child, so 10.2 makes it
 * establish an inline formatting context and 10.4 lays that context out -- and the
 * context is *empty*. Both halves are asserted because both are conclusions: the
 * context exists (so the phase really is driven by boxes), and it placed nothing
 * (so an empty context is a counted answer rather than a silent one).
 */
static void TestInlineEmptyContext(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture, "<html><body><p></p></body></html>", rules, 3u);
    LayoutInlinePass(&fixture, &stats);

    RoboCheckEqInt(stats.inlineFlow.contexts, 1u);
    RoboCheckEqInt(stats.inlineFlow.emptyContexts, 1u);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 0u);
    RoboCheckEqInt(stats.inlineFlow.lines, 0u);
    RoboCheckEqInt(stats.inlineFlow.depthMax, 0u);

    /* `<p>` still has a geometry -- it is a block, and 10.3 places it. Its extent is
     * the lines' extent, which is zero: a block container with no measurable content
     * is a zero-height block, which is 10.3's answer restated where an inline context
     * now has a say in it. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 0);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced + stats.block.blocksPlaced,
                      stats.boxesComputed);
    RoboCheckEqInt(stats.boxesComputed, 3u); /* html, body, p */

    LayoutInlineEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Placement                                                           */
/* ------------------------------------------------------------------ */

static void TestInlineSingleAtomic(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        /* An atomic inline: laid out along a line, sized by its declarations. */
        { "span", RoboCssDisplayInlineBlock, 1, 120.0, 1, 40.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture, "<html><body><p><span></span></p></body></html>", rules, 4u);
    LayoutInlinePass(&fixture, &stats);

    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "span"), 0, 0, 120, 40);
    /* The container's own extent is the line's: one line, forty pixels tall. That
     * number is the whole reason the inline direction runs *inside* the block
     * recursion (RoboLayoutBlock.h) -- nothing else could hand it back. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 40);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "body"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 40);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "html"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 40);

    RoboCheckEqInt(stats.inlineFlow.contexts, 1u);
    RoboCheckEqInt(stats.inlineFlow.emptyContexts, 0u);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 1u);
    RoboCheckEqInt(stats.inlineFlow.atomicPlaced, 1u);
    RoboCheckEqInt(stats.inlineFlow.nonAtomicPlaced, 0u);
    RoboCheckEqInt(stats.inlineFlow.widthsDeclared, 1u);
    RoboCheckEqInt(stats.inlineFlow.widthsFilled, 0u);
    RoboCheckEqInt(stats.inlineFlow.heightsDeclared, 1u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    RoboCheckEqInt(stats.inlineFlow.wraps, 0u);
    RoboCheckEqInt(stats.inlineFlow.depthMax, 3u); /* the span's own depth */

    LayoutInlineEnd(&fixture);
}

/**
 * Sequential placement: the cursor is the only thing that carries between siblings.
 *
 * The assertion the header calls the cursor's whole job — `B.x == A.x + A.width` —
 * checked on the third box as well, so a build that reset the cursor after the
 * second one would be caught rather than passing on a two-box document.
 */
static void TestInlineSiblings(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", RoboCssDisplayInlineBlock, 1, 100.0, 1, 20.0 },
        { "i", RoboCssDisplayInlineBlock, 1, 150.0, 1, 20.0 },
        { "em", RoboCssDisplayInlineBlock, 1, 50.0, 1, 20.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture,
                        "<html><body><p><b></b><i></i><em></em></p></body></html>", rules, 6u);
    LayoutInlinePass(&fixture, &stats);

    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 0, 100, 20);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "i"), 100, 0, 150, 20);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "em"), 250, 0, 50, 20);
    /* And the container's extent is one line tall, not the sum of the three boxes'
     * heights: a line's height is the tallest thing on it. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 20);

    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 3u);
    RoboCheckEqInt(stats.inlineFlow.atomicPlaced, 3u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    RoboCheckEqInt(stats.inlineFlow.wraps, 0u);
    RoboCheckEqInt(stats.inlineFlow.widthsDeclared, 3u);

    LayoutInlineEnd(&fixture);
}

/**
 * A non-atomic inline box is the **union** of what its contents reached.
 *
 * This is the other half of the atomic/non-atomic distinction, and the case is
 * written so that the union cannot be confused with any other number: the box holds
 * two atomics of different widths and heights, so its width is their sum, its height
 * is the taller of the two, and its position is where the first of them landed.
 * A build that treated the box as a unit would have to invent a width for it (it
 * declares none, and CSS does not let it), and a build that treated it as
 * transparent but never wrote its geometry would leave it UNRESOLVED.
 */
static void TestInlineNonAtomicUnion(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0, 0.0, 0, 0.0 },
        { "b", RoboCssDisplayInlineBlock, 1, 70.0, 1, 30.0 },
        { "i", RoboCssDisplayInlineBlock, 1, 40.0, 1, 55.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture, "<html><body><p><span><b></b><i></i></span></p></body></html>",
                        rules, 6u);
    LayoutInlinePass(&fixture, &stats);

    /* The transparent box's two children are participants of the *same* context --
     * `<p>`'s -- so they carry the cursor forward just as if they were `<p>`'s own
     * children. That is what "transparent to the line" means, and the two positions
     * are what it looks like.
     *
     * Their *verticals* differ, and 10.7 is why: both boxes take the bottom-edge
     * fallback, so both rest their feet on the line's floor -- 55 below the line's top,
     * because the taller of the two set the ascent -- and the 30-tall box therefore sits
     * at y = 25 rather than at the line's top. The x values are still the cursor's.
     */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 25, 70, 30);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "i"), 70, 0, 40, 55);

    /* The box itself: from the first child's left edge to the second's right edge,
     * and from the line top to the taller child's bottom. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "span"), 0, 0, 110, 55);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 55);

    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 3u);
    RoboCheckEqInt(stats.inlineFlow.atomicPlaced, 2u);
    RoboCheckEqInt(stats.inlineFlow.nonAtomicPlaced, 1u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    /* The transparent box declares nothing and is sized by nothing: no width, no
     * height, and therefore neither counter moved for it. */
    RoboCheckEqInt(stats.inlineFlow.widthsDeclared, 2u);
    RoboCheckEqInt(stats.inlineFlow.heightsDeclared, 2u);

    LayoutInlineEnd(&fixture);
}

/**
 * A run nobody measured has no extent, and that is the honest answer.
 *
 * A paragraph holding text has a height in a browser, and this pass cannot know it
 * *here*: 10.5 made a text node measurable, but measurement is a **seam** and this
 * fixture installs no metrics source -- so the run is placed, it is a participant of
 * the line, and it measures zero. The case asserts the zero *and* the counter that
 * says why (`unmeasured`), rather than leaving a reader to guess whether the zero is
 * a placeholder: a pass given no font and a pass given a font that measures nothing
 * would otherwise look identical. The alternative — inventing a plausible width for
 * text nobody measured — is the one failure a later phase could not detect.
 *
 * Two empty spans are placed at the same cursor as the run, which is the second half
 * of the claim: a zero-width participant advances the cursor by nothing.
 */
static void TestInlineTextless(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0, 0.0, 0, 0.0 },
        { "b", RoboCssDisplayInline, 0, 0.0, 0, 0.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture,
                        "<html><body><p><span></span><b></b>text</p></body></html>", rules, 5u);
    LayoutInlinePass(&fixture, &stats);

    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "span"), 0, 0, 0, 0);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 0, 0, 0);
    /* Not one pixel of extent, from either box or from the run after them: the line's
     * height is zero and the container is a zero-height block -- which is exactly what
     * 10.3 concluded about `<p>Hello</p>` before 10.5 existed, and is now a statement
     * about an *unmeasured* run rather than about a node that generated nothing. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 0);

    /* Three participants: the two empty spans and the text run, which is a
     * participant and *not* a box -- so `nonAtomicPlaced`, the box counter, stays
     * at two while `participantsPlaced` moves. */
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 3u);
    RoboCheckEqInt(stats.inlineFlow.nonAtomicPlaced, 2u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    RoboCheckEqInt(stats.inlineFlow.wraps, 0u);
    RoboCheckEqInt(stats.inlineFlow.emptyContexts, 0u);
    /* And the reason the run is zero wide is recorded rather than implied. */
    RoboCheckEqInt(stats.text.runsPlaced, 1u);
    RoboCheckEqInt(stats.text.measured, 0u);
    RoboCheckEqInt(stats.text.unmeasured, 1u);

    LayoutInlineEnd(&fixture);
}

/**
 * An atomic inline with no declared width fills its containing block.
 *
 * The rule is CSS's `width: auto`, read the same way 10.3 reads it for a block box,
 * and the honest reason it is a *fill* rather than a shrink-to-fit: shrinking to fit
 * is intrinsic sizing, which this build cannot do, and the header says so where the
 * counter is declared. It has a visible consequence worth asserting — the box takes
 * the whole line, so the next participant starts a new one.
 */
static void TestInlineAtomicFill(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "span", RoboCssDisplayInlineBlock, 0, 0.0, 1, 12.0 },
        { "b", RoboCssDisplayInlineBlock, 1, 30.0, 1, 12.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture, "<html><body><p><span></span><b></b></p></body></html>",
                        rules, 5u);
    LayoutInlinePass(&fixture, &stats);

    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "span"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 12);
    /* The fill took the line, so `<b>` wraps to the next one — the fill rule's own
     * consequence, and the reason the counter is worth having: a build that guessed
     * a width here would have put `<b>` beside it. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 12, 30, 12);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 24);

    RoboCheckEqInt(stats.inlineFlow.widthsFilled, 1u);
    RoboCheckEqInt(stats.inlineFlow.widthsDeclared, 1u);
    RoboCheckEqInt(stats.inlineFlow.wraps, 1u);
    RoboCheckEqInt(stats.inlineFlow.lines, 2u);

    LayoutInlineEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Wrapping                                                            */
/* ------------------------------------------------------------------ */

static void TestInlineWrapping(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", RoboCssDisplayInlineBlock, 1, 100.0, 1, 30.0 },
        { "i", RoboCssDisplayInlineBlock, 1, 100.0, 1, 50.0 },
        { "em", RoboCssDisplayInlineBlock, 1, 100.0, 1, 20.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture,
                        "<html><body><p><b></b><i></i><em></em></p></body></html>", rules, 6u);
    /* 250 wide: two hundred-pixel boxes fit, the third does not. The line width is
     * the viewport's because a block box fills its containing block (10.3), which is
     * what makes a viewport the way to ask for a wrap. */
    LayoutInlinePassWith(&fixture, 250, &stats, NULL);

    /* Both rest on the first line's floor, which the 50-tall box sets: so the 30-tall
     * one sits 20 down from the top and the 50-tall one starts there. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 20, 100, 30);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "i"), 100, 0, 100, 50);
    /* The third box wrapped, and the line it left was 50 tall -- the tallest thing
     * on it -- so it sits at y = 50 rather than at 30 or 40. The *line height* is
     * the number a build that advanced by the last participant's height would get
     * wrong, which is why the middle box is the tall one. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "em"), 0, 50, 100, 20);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0, 250, 70);

    RoboCheckEqInt(stats.inlineFlow.wraps, 1u);
    RoboCheckEqInt(stats.inlineFlow.lines, 2u);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 3u);

    LayoutInlineEnd(&fixture);
}

/**
 * A participant wider than its line is placed anyway, and on its *own* line.
 *
 * CSS's rule, and the one a build implementing "wrap when it does not fit" without
 * the `cursor > 0` guard gets wrong in the worst possible way: a box wider than the
 * containing block would start a new line forever, or be dropped. Here it overflows,
 * which is a fact 10.13 owns and this pass reports in no flag at all.
 */
static void TestInlineOverflowDoesNotWrap(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", RoboCssDisplayInlineBlock, 1, 400.0, 1, 10.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture, "<html><body><p><b></b></p></body></html>", rules, 4u);
    LayoutInlinePassWith(&fixture, 100, &stats, NULL);

    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 0, 400, 10);
    /* Placed once, on the first line, with no wrap at all -- and the flags stay 0,
     * because "this box overflows" is 10.13's fact. */
    RoboCheckEqInt(stats.inlineFlow.wraps, 0u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 1u);

    LayoutInlineEnd(&fixture);
}

/**
 * A transparent box is not a unit, so it does not wrap as one.
 *
 * The second half of the wrapping rule, and the place the two kinds of inline box
 * part company most visibly: a `span`'s width is not known before it is placed, so
 * the line cannot decide whether it fits. Its *contents* wrap instead, one
 * participant at a time, and the span's own rectangle becomes the union of the
 * fragments it produced — which is a wider and taller box than any single line.
 */
static void TestInlineTransparentWrapsInside(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0, 0.0, 0, 0.0 },
        { "b", RoboCssDisplayInlineBlock, 1, 100.0, 1, 30.0 },
        { "i", RoboCssDisplayInlineBlock, 1, 100.0, 1, 30.0 },
        { "em", RoboCssDisplayInlineBlock, 1, 100.0, 1, 20.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture,
                        "<html><body><p><span><b></b><i></i><em></em></span></p></body></html>",
                        rules, 7u);
    LayoutInlinePassWith(&fixture, 250, &stats, NULL);

    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 0, 100, 30);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "i"), 100, 0, 100, 30);
    /* The third wrapped *inside the span*, at x = 0 of the second line -- and the
     * span's own left edge is the first line's zero, so the fragment at x = 0 is
     * inside the union rather than beside it. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "em"), 0, 30, 100, 20);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "span"), 0, 0, 200, 50);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0, 250, 50);

    RoboCheckEqInt(stats.inlineFlow.wraps, 1u);
    RoboCheckEqInt(stats.inlineFlow.lines, 2u);
    RoboCheckEqInt(stats.inlineFlow.nonAtomicPlaced, 1u);
    RoboCheckEqInt(stats.inlineFlow.atomicPlaced, 3u);

    LayoutInlineEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Nesting                                                             */
/* ------------------------------------------------------------------ */

/**
 * A participant inside a transparent box is positioned in **that box's** space.
 *
 * The coordinate convention every result in this module is stated in
 * (RoboLayoutResult.h) -- a position is in the box's containing block's space -- and
 * for an inline participant the containing block is its *parent*, which for a box
 * inside a `span` is the span and not the context's owner. So the pass rebases:
 * participants are placed in the line's space while the context runs, and moved into
 * their parent's space once the parent's origin is known.
 *
 * The case is built so that the rebase is visible rather than vacuous: the atomic
 * inside the span sits at x = 60 in the line, so its position relative to the span
 * is zero and its position relative to `<p>` is 60. A build with no rebase would
 * answer 60 for both, and a build that never placed the span would leave it
 * UNRESOLVED.
 */
static void TestInlineNestedRebase(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", RoboCssDisplayInlineBlock, 1, 60.0, 1, 25.0 },
        { "span", RoboCssDisplayInline, 0, 0.0, 0, 0.0 },
        { "em", RoboCssDisplayInlineBlock, 1, 45.0, 1, 25.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture, "<html><body><p><b></b><span><em></em></span></p></body></html>",
                        rules, 6u);
    LayoutInlinePass(&fixture, &stats);

    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 0, 60, 25);
    /* In the line, `<em>` is at x = 60. Relative to its parent, the span, it is at
     * zero -- and the span itself is where the cursor was when it began. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "span"), 60, 0, 45, 25);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "em"), 0, 0, 45, 25);

    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 3u);
    RoboCheckEqInt(stats.inlineFlow.nonAtomicPlaced, 1u);
    RoboCheckEqInt(stats.inlineFlow.atomicPlaced, 2u);
    RoboCheckEqInt(stats.inlineFlow.depthMax, 4u); /* html, body, p, span/em */

    LayoutInlineEnd(&fixture);
}

/**
 * Transparency reaches through more than one box.
 *
 * `span` inside `span` inside the container: neither establishes anything, so both
 * are participants of `<p>`'s context and both are written as unions — and the
 * innermost box's position is relative to the box that directly contains it, which
 * is the inner span, not the outer one. A build that flattened the chain would still
 * get the innermost *line* position right and the two parents wrong.
 */
static void TestInlineNestedTwoLevels(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", RoboCssDisplayInlineBlock, 1, 80.0, 1, 20.0 },
        { "span", RoboCssDisplayInline, 0, 0.0, 0, 0.0 },
        { "i", RoboCssDisplayInline, 0, 0.0, 0, 0.0 },
        { "em", RoboCssDisplayInlineBlock, 1, 35.0, 1, 20.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture,
                        "<html><body><p><b></b><span><i><em></em></i></span></p></body></html>",
                        rules, 7u);
    LayoutInlinePass(&fixture, &stats);

    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 0, 80, 20);
    /* All three transparent-or-inside boxes start where `<em>` starts, so each is at
     * zero relative to the one above it -- which is the assertion: the rebase is
     * applied once per level, not once per context. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "em"), 0, 0, 35, 20);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "i"), 0, 0, 35, 20);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "span"), 80, 0, 35, 20);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 20);

    RoboCheckEqInt(stats.inlineFlow.nonAtomicPlaced, 2u);
    RoboCheckEqInt(stats.inlineFlow.atomicPlaced, 2u);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 4u);
    RoboCheckEqInt(stats.inlineFlow.depthMax, 5u);

    LayoutInlineEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The gate: what the inline direction declines                        */
/* ------------------------------------------------------------------ */

/**
 * An inline-level box in a **block-direction** context is declined, not misplaced.
 *
 * `<div>` has a block-level child, so 10.2 makes it transparent and its contents
 * belong to the root's block-direction context. The `<span>` between its two
 * children is therefore inline-level with no line anywhere to put it on — the mixed
 * content case CSS solves with an anonymous block box, which this build makes none
 * of. It is counted by 10.3 and left UNRESOLVED, and the inline pass never sees it
 * at all, which is why `contexts` counts only the contexts this phase laid out.
 */
static void TestInlineMixedDeclined(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "div", RoboCssDisplayBlock, 0, 0.0, 0, 0.0 },
        { "span", RoboCssDisplayInline, 0, 0.0, 0, 0.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture, "<html><body><p></p><div><p></p><span></span></div></body></html>",
                        rules, 5u);
    LayoutInlinePass(&fixture, &stats);

    LayoutInlineExpectUnresolved(&fixture, LayoutInlineBoxOf(&fixture, "span"));
    /* Two contexts were laid out -- the two `<p>`s, each of which establishes an
     * inline one -- and both are empty, because a `<p>` with no element child has no
     * participant. The span is in neither of them. */
    RoboCheckEqInt(stats.inlineFlow.contexts, 2u);
    RoboCheckEqInt(stats.inlineFlow.emptyContexts, 2u);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 0u);

    LayoutInlineEnd(&fixture);
}

/**
 * A **block-level** box inside an inline box is the mirror case, and the anonymous
 * box exists for it too.
 *
 * The container establishes an inline context (nothing block-level is its *direct*
 * child), the `<span>` inside it is transparent so the `<section>` participates in
 * that inline context — and a block-level box cannot be a participant of a line.
 * Declining it is the same answer as the case above, and it is counted in the inline
 * record where the first case is counted by 10.3: the counter that moves says which
 * direction found it.
 *
 * The structure is *this* one rather than a `<div>` inside a `<p>`, and that is a
 * fact about RoboHTML rather than about layout: the tree builder implements HTML's
 * rule that a block-level start tag closes an open paragraph, so
 * `<p><span><div></div></span></p>` parses as a `<p>` holding the span followed by a
 * *sibling* `<div>` — the nesting this case needs would never exist. A container
 * element with no paragraph on the stack gives it, and the `<head>` the parser
 * synthesizes is still the one box the block direction declines.
 *
 * The accounting identity closes over both directions here — three blocks, one
 * declined inline participant, one declined block — and it is asserted rather than
 * eyeballed, because a build that dropped a box would have four right-looking
 * numbers and a document that did not add up.
 */
static void TestInlineBlockLevelDeclined(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0, 0, 0.0 },
        { "div", RoboCssDisplayBlock, 0, 0.0, 0, 0.0 },
        { "span", RoboCssDisplayInline, 0, 0.0, 0, 0.0 },
        { "section", RoboCssDisplayBlock, 0, 0.0, 0, 0.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture, "<html><body><div><span><section></section></span></div></body></html>",
                        rules, 5u);
    LayoutInlinePass(&fixture, &stats);

    LayoutInlineExpectUnresolved(&fixture, LayoutInlineBoxOf(&fixture, "section"));
    /* The span *is* placed: it is a participant of the line, it just has nothing in
     * it that could be measured. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "span"), 0, 0, 0, 0);

    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 1u);
    RoboCheckEqInt(stats.inlineFlow.blockLevelDeclined, 1u);
    RoboCheckEqInt(stats.block.inlineLevelSkipped, 1u); /* the parser's `<head>` */
    RoboCheckEqInt(stats.block.unreached, 1u);            /* the `<section>` */
    RoboCheckEqInt(stats.block.blocksPlaced, 3u);        /* html, body, div */

    /* The identity, asserted as an identity: every box is placed, declined for a
     * stated reason by one of the two directions, or unreached. */
    RoboCheckEqInt(stats.block.blocksPlaced + stats.block.inlineLevelSkipped +
                          stats.inlineFlow.participantsPlaced + stats.block.unreached,
                      stats.objects);

    LayoutInlineEnd(&fixture);
}

/**
 * An atomic inline's contents are a **block** formatting context, in the box's own
 * space.
 *
 * 10.2's rule, and the reason an `inline-block` is atomic at all: its inner display
 * is `flow-root`, so what is inside it stacks. The pass hands those contents to block
 * layout with the box's own width as their containing block, which is why the `<p>`
 * inside comes out exactly as wide as the span and positioned relative to it.
 *
 * The span's *height* is then the extent its contents reached — the same rule 10.3
 * applies to a block box, applied by the inline pass because the span is a
 * participant of a line rather than of a block flow.
 */
static void TestInlineAtomicContentsAreBlocks(void)
{
    LayoutInlineFixture fixture;
    /*
     * Spelled out rather than composed from LAYOUT_INLINE_BLOCK_RULES, and the
     * reason is that the `<p>` here is the *subject*: the container macro already
     * declares one, with no height. Two rules for one tag is a fixture bug the
     * lookup would swallow (it answers with the first match), so the macro is not
     * used where it would mean declaring `p` twice.
     */
    static const LayoutInlineRule rules[] = {
        { "html", RoboCssDisplayBlock, 0, 0.0, 0, 0.0 },
        { "body", RoboCssDisplayBlock, 0, 0.0, 0, 0.0 },
        { "span", RoboCssDisplayInlineBlock, 1, 300.0, 0, 0.0 },
        { "p", RoboCssDisplayBlock, 0, 0.0, 1, 25.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture, "<html><body><span><p></p></span></body></html>", rules, 4u);
    LayoutInlinePass(&fixture, &stats);

    /* `<body>`'s only child is inline-level, so body establishes an inline context
     * and the span is placed on its line. */
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "span"), 0, 0, 300, 25);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0, 300, 25);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "body"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 25);

    RoboCheckEqInt(stats.inlineFlow.atomicPlaced, 1u);
    RoboCheckEqInt(stats.inlineFlow.heightsFromContent, 1u);
    /* html, body, span, p: the span by the inline pass and the p by the block pass,
     * each counted once, which is the claim `boxesComputed` rests on. */
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 1u);
    RoboCheckEqInt(stats.block.blocksPlaced, 3u);
    RoboCheckEqInt(stats.boxesComputed, 4u);

    LayoutInlineEnd(&fixture);
}

/**
 * A `display: none` element is not in the tree at all, so it cannot be on a line.
 *
 * 10.1's rule, restated where a cursor now depends on it: the element generates no
 * box, so it neither advances the cursor nor contributes an extent — and the box
 * *after* it lands where it would have landed had the hidden element never been
 * written.
 */
static void TestInlineDisplayNone(void)
{
    LayoutInlineFixture fixture;
    /* `<u>` rather than a `<div>`, and the reason is RoboHTML's: a block-level start
     * tag closes an open paragraph, so a hidden `<div>` here would take the `<p>` down
     * with it and the case would be testing the parser rather than the cursor. */
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", RoboCssDisplayInlineBlock, 1, 100.0, 1, 20.0 },
        { "u", RoboCssDisplayNone, 0, 0.0, 0, 0.0 },
        { "i", RoboCssDisplayInlineBlock, 1, 100.0, 1, 20.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture, "<html><body><p><b></b><u></u><i></i></p></body></html>",
                        rules, 6u);
    LayoutInlinePass(&fixture, &stats);

    {
        HtmlNodeHandle hidden = LayoutDocumentFind(&fixture.doc, "u");

        RoboCheckEqInt(HtmlNodeHandleIsWellformed(&hidden), 1);
        RoboCheckEqInt(LayoutObjectIdForDomNode(fixture.tree, &hidden),
                          RoboLayoutNodeIdNone);
    }

    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 0, 100, 20);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "i"), 100, 0, 100, 20);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 2u);

    LayoutInlineEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

static void TestInlineNoViewport(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", RoboCssDisplayInlineBlock, 1, 100.0, 1, 20.0 }
    };
    LayoutContext *context = NULL;
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture, "<html><body><p><b></b></p></body></html>", rules, 4u);

    /*
     * The precondition, stated rather than silently satisfied: an inline formatting
     * context has no width to lay a line into without a containing block, and the
     * root's comes from the viewport. So a pass with no viewport classifies and
     * places nothing -- in *both* directions, which is what the two zero records say.
     */
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context),
                      (int)RoboLayoutPassWalked);
    LayoutContextStats(context, &stats);
    /* Five boxes for four written tags: the tree builder inserts the `<head>` a
     * well-formed document requires, and nothing styles it (RoboLayoutTestSupport.h).
     * The number is the parser's, and it is asserted rather than derived so that the
     * two agree by being compared. */
    RoboCheckEqInt(stats.objects, 5u);
    RoboCheckEqInt(stats.boxesComputed, 0u);
    RoboCheckEqInt(stats.boxesUnresolved, 5u);
    RoboCheckEqInt(LayoutBlockStatsEqual(&stats.block,
                                                    &(LayoutBlockStats){0}),
                      1);
    RoboCheckEqInt(LayoutInlineStatsEqual(&stats.inlineFlow,
                                                     &(LayoutInlineStats){0}),
                      1);
    /* The classification still happened: the pass is not half a pass, it is the part
     * of the work the input allowed. */
    RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 1);
    LayoutInlineExpectUnresolved(&fixture, LayoutInlineBoxOf(&fixture, "b"));
    LayoutContextDestroy(context);

    /* And the same tree, given a viewport, is laid out -- which is what makes the
     * zeros above a statement about the *input* rather than about the build. */
    LayoutInlinePass(&fixture, &stats);
    RoboCheckEqInt(stats.boxesComputed, 4u);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 0, 100, 20);

    LayoutInlineEnd(&fixture);
}

/**
 * The same input produces the same lines, twice running and in two trees.
 *
 * Two passes on **one** context, because a helper that made a fresh context per call
 * would prove the geometry is reproducible and say nothing about whether a second
 * pass stacks on the first: the failure a cumulative pass has is a cursor that never
 * resets, which would put the second pass's participants beside the first's. Then a
 * second *tree* from the same document, which is determinism of the algorithm rather
 * than of the build.
 */
static void TestInlineDeterminism(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", RoboCssDisplayInlineBlock, 1, 100.0, 1, 30.0 },
        { "i", RoboCssDisplayInlineBlock, 1, 100.0, 1, 50.0 },
        { "em", RoboCssDisplayInlineBlock, 1, 100.0, 1, 20.0 },
        { "span", RoboCssDisplayInline, 0, 0.0, 0, 0.0 }
    };
    static const char *const html =
        "<html><body><p><b></b><span><i></i></span><em></em></p></body></html>";
    LayoutStats first;
    LayoutStats second;
    LayoutResult before[8];
    size_t count;
    size_t index;

    LayoutFixture();
    LayoutInlineBegin(&fixture, html, rules, 7u);
    count = LayoutTreeObjectCount(fixture.tree);
    RoboCheck(count <= 8u);

    {
        LayoutContext *context = NULL;
        LayoutSize viewport;

        viewport.width = 250;
        viewport.height = 600;
        RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
        RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
        RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
        LayoutContextStats(context, &first);
        for (index = 0u; index < count; index++) {
            LayoutNodeId id = LayoutInlineBoxAt(&fixture, index);

            RoboCheckEqInt(LayoutObjectResult(fixture.tree, id, &before[index]),
                              RoboOk);
        }

        RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
        LayoutContextStats(context, &second);
        LayoutContextDestroy(context);
    }
    RoboCheckEqInt(first.passes, 1u);
    RoboCheckEqInt(second.passes, 2u);
    RoboCheckEqInt(LayoutInlineStatsEqual(&first.inlineFlow, &second.inlineFlow), 1);
    RoboCheckEqInt(LayoutBlockStatsEqual(&first.block, &second.block), 1);
    for (index = 0u; index < count; index++) {
        LayoutNodeId id = LayoutInlineBoxAt(&fixture, index);
        LayoutResult now;

        RoboCheckEqInt(LayoutObjectResult(fixture.tree, id, &now), RoboOk);
        RoboCheckEqInt(LayoutResultEqual(&before[index], &now), 1);
    }

    {
        LayoutInlineFixture twin;
        LayoutStats twinStats;

        memset(&twin, 0, sizeof(twin));
        LayoutDocumentBuild(&twin.doc, html);
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
        LayoutInlinePassWith(&twin, 250, &twinStats, NULL);
        RoboCheckEqInt(LayoutInlineStatsEqual(&first.inlineFlow,
                                                         &twinStats.inlineFlow),
                          1);
        RoboCheckEqInt(LayoutTreeObjectCount(twin.tree), count);
        for (index = 0u; index < count; index++) {
            LayoutNodeId id = LayoutInlineBoxAt(&twin, index);
            LayoutResult now;

            RoboCheckEqInt(LayoutObjectResult(twin.tree, id, &now), RoboOk);
            RoboCheckEqInt(LayoutResultEqual(&before[index], &now), 1);
        }
        LayoutTreeDestroy(twin.tree);
        LayoutDocumentRelease(&twin.doc);
    }

    LayoutInlineEnd(&fixture);
}

/** Releasing the classification releases the lines derived from it. */
static void TestInlineInvalidation(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", RoboCssDisplayInlineBlock, 1, 100.0, 1, 30.0 }
    };
    LayoutStats stats;
    size_t index;

    LayoutFixture();
    LayoutInlineBegin(&fixture, "<html><body><p><b></b></p></body></html>", rules, 4u);
    LayoutInlinePass(&fixture, &stats);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 0, 100, 30);

    LayoutTreeInvalidateContexts(fixture.tree);
    RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 0);
    for (index = 0u; index < 4u; index++) {
        LayoutInlineExpectUnresolved(&fixture, LayoutInlineBoxAt(&fixture, index));
    }

    /* And re-running recalculates, so the release is not destructive. */
    LayoutInlinePass(&fixture, &stats);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "b"), 0, 0, 100, 30);

    LayoutInlineEnd(&fixture);
}

/**
 * The available width is an input, and changing it changes the lines.
 *
 * The same tree, the same styles, one different number: 800 wide puts three boxes on
 * one line, 250 wide wraps the third. This is the case that proves the line width is
 * *read* from the containing block rather than fixed at build time -- and it is the
 * reason the inline direction cannot be its own pass over a finished tree, since the
 * wrap changes every ancestor's height.
 */
static void TestInlineViewportChangesLayout(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", RoboCssDisplayInlineBlock, 1, 100.0, 1, 30.0 },
        { "i", RoboCssDisplayInlineBlock, 1, 100.0, 1, 30.0 },
        { "em", RoboCssDisplayInlineBlock, 1, 100.0, 1, 30.0 }
    };
    LayoutStats stats;

    LayoutFixture();
    LayoutInlineBegin(&fixture,
                        "<html><body><p><b></b><i></i><em></em></p></body></html>", rules, 6u);

    LayoutInlinePassWith(&fixture, 800, &stats, NULL);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "em"), 200, 0, 100, 30);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0, 800, 30);
    RoboCheckEqInt(stats.inlineFlow.wraps, 0u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);

    LayoutInlinePassWith(&fixture, 250, &stats, NULL);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "em"), 0, 30, 100, 30);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0, 250, 60);
    RoboCheckEqInt(stats.inlineFlow.wraps, 1u);
    RoboCheckEqInt(stats.inlineFlow.lines, 2u);

    /* And back again: the pass replaces geometry rather than refining it, so the
     * wide answer comes back whole rather than leaving the wrapped one behind. */
    LayoutInlinePassWith(&fixture, 800, &stats, NULL);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "em"), 200, 0, 100, 30);
    LayoutInlineExpect(&fixture, LayoutInlineBoxOf(&fixture, "p"), 0, 0, 800, 30);

    LayoutInlineEnd(&fixture);
}

/**
 * The numbers came from CSS.
 *
 * A real stylesheet through the real cascade: three `inline-block` boxes with
 * declared widths inside a `<div>`, which establishes an inline formatting context
 * because 10.2's rule found nothing block-level in it. The declarations that produce
 * a line are `display` and `width` -- both of which the *cascade* resolved, neither
 * of which this module parses -- and the assertion that the three boxes land at
 * 0/100/250 is the whole pipeline, from a selector to a cursor.
 */
static void TestInlinePipeline(void)
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
    LayoutResult result;
    LayoutPoint position;
    LayoutSize size;
    LayoutNodeId box;
    HtmlNodeHandle element;

    LayoutFixture();
    LayoutDocumentBuild(&doc,
                          "<html>\n"
                          "  <body>\n"
                          "    <div><span class=\"a\"></span><span class=\"b\"></span>"
                          "<span class=\"c\"></span></div>\n"
                          "  </body>\n"
                          "</html>\n");
    LayoutCssBegin(&css);
    LayoutCssSheet(&css,
                     "html, body, div { display: block }\n"
                     "span { display: inline-block }\n"
                     ".a { width: 100px; height: 24px }\n"
                     ".b { width: 150px; height: 24px }\n"
                     ".c { width: 60px; height: 24px }\n");
    RoboCheckEqInt(LayoutDomCreate(css.context, doc.id, &dom, NULL), RoboOk);
    RoboCheckEqInt(LayoutDomRefresh(dom, NULL), RoboOk);
    RoboCheckEqInt(LayoutDomStyleSource(dom, &source, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &source;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, &options, NULL, NULL),
                      RoboOk);

    /* html, head, body, div, three spans. `<head>` is the parser's and nothing
     * styles it, so it is an inline box the block direction declines. */
    RoboCheckEqInt(LayoutTreeObjectCount(tree), 7u);

    viewport.width = LAYOUT_INLINE_WIDE_VIEWPORT;
    viewport.height = 600;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context),
                      (int)RoboLayoutPassWalked);
    LayoutContextStats(context, &stats);

    element = LayoutDocumentFind(&doc, "span");
    box = LayoutObjectIdForDomNode(tree, &element);
    RoboCheck(box != RoboLayoutNodeIdNone);
    RoboCheckEqInt(LayoutObjectResult(tree, box, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    RoboCheckEqInt(position.x, 0);
    RoboCheckEqInt(position.y, 0);
    RoboCheckEqInt(size.width, 100);
    RoboCheckEqInt(size.height, 24);

    /* The three spans, in document order, by their three declared widths -- which is
     * the cursor arithmetic done on values the cascade produced. The boxes are the
     * tree's 5th, 6th and 7th, and they are taken by *index* because
     * `LayoutDocumentFind` deliberately answers with the first element of a tag and
     * the three spans share one. */
    {
        static const int32_t expectedX[] = { 0, 100, 250 };
        static const int32_t expectedWidth[] = { 100, 150, 60 };
        size_t i;

        for (i = 0u; i < 3u; i++) {
            LayoutNodeId spanBox = RoboLayoutNodeIdNone;

            RoboCheckEqInt(LayoutObjectIdAt(tree, 4u + i, &spanBox), RoboOk);
            LayoutInlineExpectIn(tree, spanBox, expectedX[i], 0, expectedWidth[i], 24);
        }
    }
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 3u);
    RoboCheckEqInt(stats.inlineFlow.atomicPlaced, 3u);
    RoboCheckEqInt(stats.inlineFlow.widthsDeclared, 3u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    RoboCheckEqInt(stats.inlineFlow.wraps, 0u);

    /* `boxesComputed` is the two placement counts added together, asserted on a
     * document whose box count the *parser* decided. */
    RoboCheckEqInt(stats.boxesComputed,
                      stats.block.blocksPlaced + stats.inlineFlow.participantsPlaced);
    RoboCheckEqInt(stats.boxesComputed, 6u);
    RoboCheckEqInt(stats.boxesUnresolved, 1u);

    LayoutContextDestroy(context);
    LayoutTreeDestroy(tree);
    LayoutDomDestroy(dom);
    LayoutCssEnd(&css);
    LayoutDocumentRelease(&doc);
}

static void TestInlineAllocatorBalance(void)
{
    LayoutInlineFixture fixture;
    static const LayoutInlineRule rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", RoboCssDisplayInlineBlock, 1, 100.0, 1, 30.0 },
        { "span", RoboCssDisplayInline, 0, 0.0, 0, 0.0 }
    };
    size_t before;
    int pass;

    LayoutFixture();
    before = LayoutLiveBlocks();

    for (pass = 0; pass < 8; pass++) {
        LayoutStats stats;

        LayoutInlineBegin(&fixture, "<html><body><p><span><b></b></span><b></b></p></body></html>",
                            rules, 5u);
        LayoutInlinePass(&fixture, &stats);
        LayoutInlinePass(&fixture, &stats);
        LayoutInlineEnd(&fixture);
    }

    RoboCheckEqInt(LayoutLiveBlocks(), before);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutInline(void);

void TestRegisterLayoutInline(void)
{
    TestRegister("layout.inline.stats", TestInlineStats);
    TestRegister("layout.inline.display_table", TestInlineDisplayTable);
    TestRegister("layout.inline.empty_context", TestInlineEmptyContext);
    TestRegister("layout.inline.single_atomic", TestInlineSingleAtomic);
    TestRegister("layout.inline.siblings", TestInlineSiblings);
    TestRegister("layout.inline.non_atomic_union", TestInlineNonAtomicUnion);
    TestRegister("layout.inline.textless", TestInlineTextless);
    TestRegister("layout.inline.atomic_fill", TestInlineAtomicFill);
    TestRegister("layout.inline.wrapping", TestInlineWrapping);
    TestRegister("layout.inline.overflow_does_not_wrap",
                       TestInlineOverflowDoesNotWrap);
    TestRegister("layout.inline.transparent_wraps_inside",
                       TestInlineTransparentWrapsInside);
    TestRegister("layout.inline.nested_rebase", TestInlineNestedRebase);
    TestRegister("layout.inline.nested_two_levels", TestInlineNestedTwoLevels);
    TestRegister("layout.inline.mixed_declined", TestInlineMixedDeclined);
    TestRegister("layout.inline.block_level_declined", TestInlineBlockLevelDeclined);
    TestRegister("layout.inline.atomic_contents_are_blocks",
                       TestInlineAtomicContentsAreBlocks);
    TestRegister("layout.inline.display_none", TestInlineDisplayNone);
    TestRegister("layout.inline.no_viewport", TestInlineNoViewport);
    TestRegister("layout.inline.determinism", TestInlineDeterminism);
    TestRegister("layout.inline.invalidation", TestInlineInvalidation);
    TestRegister("layout.inline.viewport_changes_layout",
                       TestInlineViewportChangesLayout);
    TestRegister("layout.inline.pipeline", TestInlinePipeline);
    TestRegister("layout.inline.allocator_balance", TestInlineAllocatorBalance);
}
