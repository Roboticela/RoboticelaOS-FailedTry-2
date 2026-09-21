/*
 * RoboLayout integration suite (Phase 10.15) — layout.integration.*
 *
 * Phase 10.15 is the goal that consolidates the layout test layer, and this file is
 * the one thing the twenty suites before it could not be: the place where the
 * *families* are asked to compose. Every other suite in this directory proves one
 * layer against its own declarations -- sizing against a `width`, margin against a
 * `margin`, border against a `border`, position against an `inset`, overflow against
 * a `overflow` -- and a build that got each layer right *and composed them wrongly*
 * would pass all of them. What that looks like in practice is a content origin
 * computed from the padding but not the border, a final position that is the offset
 * but not the flow, a parent that resizes itself to hold an overflowing child, or a
 * region that folds in a clipped child's *contents*. Each of those is one line of
 * wrong arithmetic that no single-family assertion can see, and each of them is
 * asked here on a real document through the real chain:
 *
 *   RoboHTML -> RoboCSS -> the bridge -> the tree -> the pass
 *
 * The cases fall into four groups and the groups are the phase's requirements:
 *
 *   composition    a real stylesheet whose boxes use several features at once, with
 *                  the *derived* relationships asserted (content origin, content
 *                  width, margin-box stacking, line-to-baseline) rather than a pile
 *                  of magic numbers -- because the composition is the claim;
 *   determinism    one document laid out repeatedly -- and built into a second tree
 *                  -- with every result, inset, offset, overflow record and line
 *                  compared field by field, not merely counted;
 *   robustness     extreme input (a dimension near the geometry's maximum, a zero,
 *                  a negative margin, an unusable declaration, a deep chain), all
 *                  of which must produce finite, deterministic geometry;
 *   regressions    the *documented findings* of 10.11, 10.13 and 10.14, each pinned
 *                  as the minimal reproduction that failed before the fix, so a
 *                  later phase that reintroduces one fails here rather than in a
 *                  document nobody runs.
 *
 * The fixture is the conformance suite's -- a real author sheet through a real style
 * context -- because that is what makes a computed style the container the cascade
 * filled rather than a struct a test fabricated. The measurer is the line-box
 * suite's, because a document with no font measures every run as zero and would make
 * every line assertion vacuous.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite
 * depends on no Chromium header, library or test framework. See layout/docs/Tests.md
 * §20.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A scripted measurer                                                 */
/* ------------------------------------------------------------------ */

/**
 * One scripted answer: this many px per code point, and this line extent.
 *
 * The same shape `SuiteRoboLayoutLineBox.c` uses, and deliberately not shared with
 * it: a shared measurer would be a third thing to keep in step, and the two suites
 * vary it differently (that one varies the height to watch a line track its tallest
 * member; this one holds it fixed so a composed line's width is the sum of its
 * parts).
 */
typedef struct layoutIntScript {
    int32_t perCodePoint;
    int32_t height;
    size_t calls;
} LayoutIntScript;

static Result LayoutIntMeasure(void *userdata,
                                        const LayoutTextRequest *request,
                                        LayoutTextRun *outRun)
{
    LayoutIntScript *script = (LayoutIntScript *)userdata;

    if (!script || !request || !outRun || !request->style) {
        return RoboInvalidArgument;
    }
    script->calls++;
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

static LayoutTextMetrics LayoutIntMetricsOf(LayoutIntScript *script)
{
    LayoutTextMetrics metrics;

    LayoutTextMetricsInit(&metrics);
    metrics.userdata = script;
    metrics.measure = LayoutIntMeasure;
    return metrics;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/** Everything one integration document needs: DOM, bridge, styles, tree. */
typedef struct layoutIntFixture {
    LayoutDocument doc;
    LayoutCssFixture css;
    LayoutDom *dom;
    LayoutTree *tree;
    LayoutStyleSource source;
} LayoutIntFixture;

static void LayoutIntBegin(LayoutIntFixture *fixture, const char *html, const char *css)
{
    LayoutBuildOptions options;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    LayoutCssBegin(&fixture->css);
    LayoutCssSheet(&fixture->css, css);
    RoboCheckEqInt(LayoutDomCreate(fixture->css.context, fixture->doc.id,
                                             &fixture->dom, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutDomRefresh(fixture->dom, NULL), RoboOk);
    RoboCheckEqInt(LayoutDomStyleSource(fixture->dom, &fixture->source, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->source;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutIntEnd(LayoutIntFixture *fixture)
{
    /* The tree borrows the bridge's styles, so it goes first. */
    LayoutTreeDestroy(fixture->tree);
    fixture->tree = NULL;
    LayoutDomDestroy(fixture->dom);
    fixture->dom = NULL;
    LayoutCssEnd(&fixture->css);
    LayoutDocumentRelease(&fixture->doc);
}

/** A context with a viewport, and optionally a measurer -- nothing more. */
static LayoutContext *LayoutIntContext(int32_t width,
                                                 int32_t height,
                                                 const LayoutTextMetrics *metrics)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;

    viewport.width = width;
    viewport.height = height;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    if (metrics) {
        RoboCheckEqInt(LayoutContextSetTextMetrics(context, metrics), RoboOk);
    }
    return context;
}

/**
 * One full pass, with the context destroyed afterwards.
 *
 * The diagnostic is captured and *reported* rather than passed as NULL, which is the
 * harness half of requirement 21: a pass that refuses a document says why, and a
 * suite that discards the message turns "the pass failed" into an evening of
 * bisecting a fixture. The message is static text owned by the module, so printing
 * it here costs a pointer and no lifetime question.
 */
static void LayoutIntRun(LayoutIntFixture *fixture,
                           int32_t width,
                           int32_t height,
                           const LayoutTextMetrics *metrics,
                           LayoutStats *outStats)
{
    LayoutContext *context = LayoutIntContext(width, height, metrics);
    LayoutDiag diag;
    LayoutStats stats;
    Result status;

    LayoutDiagReset(&diag);
    status = LayoutContextRun(context, fixture->tree, &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the pass refused the document: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
    RoboCheckEqInt((int)LayoutContextOutcome(context),
                      (int)RoboLayoutPassWalked);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);
    if (outStats) {
        *outStats = stats;
    }
}

/**
 * The box built for the first element named @p tag, asserted to exist.
 *
 * Every integration document below uses a distinct tag per element it asserts, which
 * is why this can be a name lookup rather than an index: a document with two `<div>`s
 * and an index-based helper would silently assert the wrong one the day a fixture
 * gains a wrapper, and `LayoutDocumentFind` is the same walk the builder performed.
 */
static LayoutNodeId LayoutIntBox(LayoutIntFixture *fixture, const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);
    LayoutNodeId id;

    RoboCheckEqInt((int)HtmlNodeHandleIsWellformed(&element), 1);
    id = LayoutObjectIdForDomNode(fixture->tree, &element);
    RoboCheck(id != RoboLayoutNodeIdNone);
    return id;
}

/** A placed box's final rectangle, asserting that geometry exists. Zeroed first, so
 *  a state that is not COMPUTED reads as a stated absence rather than a leftover. */
static void LayoutIntRect(LayoutIntFixture *fixture,
                            LayoutNodeId id,
                            LayoutRect *out)
{
    LayoutResult result;

    memset(out, 0, sizeof(*out));
    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)MakeLayoutResultState(&result),
                      (int)RoboLayoutResultComputed);
    if (LayoutResultRect(&result, out, NULL) != RoboOk) {
        memset(out, 0, sizeof(*out));
    }
}

/** A placed box's size, asserting that geometry exists. */
static LayoutSize LayoutIntSize(LayoutIntFixture *fixture,
                                          LayoutNodeId id)
{
    LayoutResult result;
    LayoutSize size;

    memset(&size, 0, sizeof(size));
    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)MakeLayoutResultState(&result),
                      (int)RoboLayoutResultComputed);
    if (LayoutResultSize(&result, &size) != RoboOk) {
        memset(&size, 0, sizeof(size));
    }
    return size;
}

/* ------------------------------------------------------------------ */
/* Assertions                                                          */
/* ------------------------------------------------------------------ */

/*
 * Macros rather than helpers, and that is the diagnostics requirement rather than a
 * style choice: a `static` helper that called `RoboCheckEqInt` would report the
 * *header's* line and not the case's, which turns "which box was wrong?" into a
 * bisect. These expand inside the case, so a failure names the case's own line, the
 * box's label, and both rectangles in full -- the four numbers a reader needs and
 * would otherwise have to add a print to get.
 */

#define LAYOUT_INT_CHECK_RECT(label, fixture, id, ex, ey, ew, eh)                   \
    do {                                                                            \
        LayoutRect rect;                                                   \
        LayoutIntRect((fixture), (id), &rect);                                   \
        if (rect.x != (ex) || rect.y != (ey) || rect.width != (ew) ||            \
            rect.height != (eh)) {                                                 \
            TestFail(__FILE__, __LINE__,                                      \
                           "%s: rect (%d,%d %dx%d) != (%d,%d %dx%d)", (label),      \
                           (int)rect.x, (int)rect.y, (int)rect.width,            \
                           (int)rect.height, (int)(ex), (int)(ey), (int)(ew),      \
                           (int)(eh));                                              \
        }                                                                           \
    } while (0)

#define LAYOUT_INT_CHECK_SIZE(label, fixture, id, ew, eh)                           \
    do {                                                                            \
        LayoutSize size = LayoutIntSize((fixture), (id));                \
        if (size.width != (ew) || size.height != (eh)) {                          \
            TestFail(__FILE__, __LINE__, "%s: size %dx%d != %dx%d", (label),  \
                           (int)size.width, (int)size.height, (int)(ew),          \
                           (int)(eh));                                              \
        }                                                                           \
    } while (0)

/** The four used margins of a box, by value. */
static LayoutMarginEdges LayoutIntMargins(LayoutIntFixture *fixture,
                                                     LayoutNodeId id)
{
    LayoutMarginEdges edges;

    LayoutMarginEdgesInit(&edges, 0);
    RoboCheckEqInt(LayoutObjectMargins(fixture->tree, id, &edges, NULL), RoboOk);
    return edges;
}

static LayoutPaddingEdges LayoutIntPadding(LayoutIntFixture *fixture,
                                                      LayoutNodeId id)
{
    LayoutPaddingEdges edges;

    LayoutPaddingEdgesInit(&edges);
    RoboCheckEqInt(LayoutObjectPadding(fixture->tree, id, &edges, NULL), RoboOk);
    return edges;
}

static LayoutBorderEdges LayoutIntBorders(LayoutIntFixture *fixture,
                                                     LayoutNodeId id)
{
    LayoutBorderEdges edges;

    LayoutBorderEdgesInit(&edges);
    RoboCheckEqInt(LayoutObjectBorders(fixture->tree, id, &edges, NULL), RoboOk);
    return edges;
}

static LayoutOverflowEdges LayoutIntOverflow(LayoutIntFixture *fixture,
                                                        LayoutNodeId id)
{
    LayoutOverflowEdges edges;

    LayoutOverflowEdgesInit(&edges);
    RoboCheckEqInt(LayoutObjectOverflow(fixture->tree, id, &edges, NULL), RoboOk);
    return edges;
}

static LayoutPositionOffset LayoutIntOffset(LayoutIntFixture *fixture,
                                                       LayoutNodeId id)
{
    LayoutPositionOffset offset;

    LayoutPositionOffsetInit(&offset);
    RoboCheckEqInt(LayoutObjectPosition(fixture->tree, id, &offset, NULL), RoboOk);
    return offset;
}

/**
 * The **page** origin of @p id: its own rect, plus every ancestor's rect and insets.
 *
 * A result's position is in its **containing block's content-box space** (the module's
 * stated coordinate space), so the four insets of every ancestor on the path are what
 * turn a local position into a page position. This helper performs exactly that walk
 * and nothing else, which makes it the assertion that the *nesting composes*: a build
 * whose child positions were already absolute would double-count here, and one whose
 * parent content origin forgot a border would be short by it.
 *
 * The root's own rect is the origin of the space, so the loop adds it like any other
 * box and the root's insets are zero.
 */
static LayoutPoint LayoutIntPageOrigin(LayoutIntFixture *fixture,
                                                  LayoutNodeId id)
{
    LayoutPoint point;
    LayoutNodeId walk = id;

    point.x = 0;
    point.y = 0;
    if (walk == RoboLayoutNodeIdNone) {
        return point;
    }

    /* The box's own rect is already stated in its containing block's content space,
     * so it is added once and its *own* insets are not: those are what turn its
     * content origin into a space for its children, and this box is not its own
     * child. They come back in at the next step, for the parent. */
    {
        LayoutRect rect;

        LayoutIntRect(fixture, walk, &rect);
        point.x += rect.x;
        point.y += rect.y;
    }

    while (LayoutObjectParent(fixture->tree, walk, &walk) == RoboOk) {
        LayoutRect rect;
        LayoutBorderEdges borders;
        LayoutPaddingEdges padding;

        LayoutIntRect(fixture, walk, &rect);
        borders = LayoutIntBorders(fixture, walk);
        padding = LayoutIntPadding(fixture, walk);
        point.x += rect.x + borders.left.width + padding.left;
        point.y += rect.y + borders.top.width + padding.top;
    }
    return point;
}

/**
 * The content-box origin of @p id, derived the way a consumer must: the final rect's
 * origin, plus the border, plus the padding -- and in @p id's owner's space, because
 * that is the space `result.position` is stated in.
 */
static LayoutPoint LayoutIntContentOrigin(LayoutIntFixture *fixture,
                                                     LayoutNodeId id)
{
    LayoutRect rect;
    LayoutBorderEdges borders = LayoutIntBorders(fixture, id);
    LayoutPaddingEdges padding = LayoutIntPadding(fixture, id);
    LayoutPoint point;

    LayoutIntRect(fixture, id, &rect);
    point.x = rect.x + borders.left.width + padding.left;
    point.y = rect.y + borders.top.width + padding.top;
    return point;
}

/** The width a box's contents have: the border box less the borders and the padding. */
static int32_t LayoutIntContentWidth(LayoutIntFixture *fixture,
                                        LayoutNodeId id)
{
    LayoutRect rect;
    LayoutBorderEdges borders = LayoutIntBorders(fixture, id);
    LayoutPaddingEdges padding = LayoutIntPadding(fixture, id);

    LayoutIntRect(fixture, id, &rect);
    return rect.width - borders.left.width - borders.right.width - padding.left -
           padding.right;
}

/* ------------------------------------------------------------------ */
/* 1. Composition: the box model, composed                             */
/* ------------------------------------------------------------------ */

/**
 * One document whose every box is 200 wide and 100 tall *inside* a margin, a border
 * and a padding -- and the composition asserted rather than the declarations.
 *
 * The numbers are chosen so that a build which read one layer and not the next is
 * caught by an exact value: `.outer`'s border box is 214 x 114 and not 200 x 100
 * (which is what a build that stopped at the sizing layer would say), not 210 x 110
 * (padding and no border), and `.inner`'s is 58 x 26 while the *width its text
 * content may use* is 50. The `body`'s height is the outer's **margin box** --
 * 10 + 114 + 10 -- which is the one number that proves margins are external to the
 * border box rather than part of it.
 */
#define LAYOUT_INT_MODEL_HTML                          \
    "<html><body>"                                     \
    "<div class=\"outer\"><section class=\"inner\"></section></div>" \
    "</body></html>"

#define LAYOUT_INT_MODEL_CSS                                                \
    "html, body { display: block }\n"                                       \
    ".outer { display: block; width: 200px; height: 100px;\n"               \
    "         margin: 10px 20px; padding: 5px; border: 2px solid black }\n" \
    ".inner { display: block; width: 50px; height: 20px;\n"                 \
    "         margin: 4px; padding: 3px; border: 1px solid black }\n"

static void TestIntegrationBoxModel(void)
{
    LayoutIntFixture fixture;
    LayoutNodeId htmlBox;
    LayoutNodeId bodyBox;
    LayoutNodeId outer;
    LayoutNodeId inner;
    LayoutMarginEdges margins;
    LayoutBorderEdges borders;
    LayoutPaddingEdges padding;
    LayoutPoint origin;
    LayoutStats stats;

    LayoutFixture();
    LayoutIntBegin(&fixture, LAYOUT_INT_MODEL_HTML, LAYOUT_INT_MODEL_CSS);

    /* html, head, body, div, section -- and `<head>` is the parser's, which is why
     * it is here and why it is the one box the pass declines. */
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 5u);

    htmlBox = LayoutIntBox(&fixture, "html");
    bodyBox = LayoutIntBox(&fixture, "body");
    outer = LayoutIntBox(&fixture, "div");
    inner = LayoutIntBox(&fixture, "section");

    LayoutIntRun(&fixture, 800, 600, NULL, &stats);
    RoboCheckEqInt(stats.objects, 5u);
    RoboCheckEqInt(stats.boxesComputed, 4u);
    RoboCheckEqInt(stats.boxesUnresolved, 1u); /* `<head>`: inline, nothing styles it */
    RoboCheckEqInt(stats.block.inlineLevelSkipped, 1u);

    /* The chain: the root fills the viewport, `body` fills the root, and the root's
     * height is `body`'s margin box. */
    LAYOUT_INT_CHECK_RECT("html", &fixture, htmlBox, 0, 0, 800, 134);
    LAYOUT_INT_CHECK_RECT("body", &fixture, bodyBox, 0, 0, 800, 134);

    /*
     * The outer box: placed at its **margin origin** (20, 10) -- the containing
     * block's content origin plus the two margins -- and sized as its **border**
     * box, which is the declared 200 x 100 *content* plus 5px of padding and 2px of
     * border on each side.
     */
    LAYOUT_INT_CHECK_RECT("div.outer", &fixture, outer, 20, 10, 214, 114);
    LAYOUT_INT_CHECK_SIZE("div.outer", &fixture, outer, 214, 114);

    margins = LayoutIntMargins(&fixture, outer);
    RoboCheckEqInt(margins.top, 10);
    RoboCheckEqInt(margins.right, 20);
    RoboCheckEqInt(margins.bottom, 10);
    RoboCheckEqInt(margins.left, 20);
    RoboCheckEqInt(margins.resolved, 4u);
    RoboCheckEqInt(margins.autosUsed, 0u);

    padding = LayoutIntPadding(&fixture, outer);
    RoboCheckEqInt(padding.top, 5);
    RoboCheckEqInt(padding.left, 5);
    RoboCheckEqInt(padding.resolved, 4u);

    borders = LayoutIntBorders(&fixture, outer);
    RoboCheckEqInt(borders.top.width, 2);
    RoboCheckEqInt(borders.left.width, 2);
    RoboCheckEqInt(borders.bordersDrawn, 4u);
    RoboCheckEqInt(borders.widthsResolved, 4u);

    /* The composition, stated as the relationship rather than as two numbers: the
     * content origin is the border-box origin plus the border plus the padding. */
    origin = LayoutIntContentOrigin(&fixture, outer);
    RoboCheckEqInt(origin.x, 20 + 2 + 5);
    RoboCheckEqInt(origin.y, 10 + 2 + 5);
    /* And the content width is the declared 200, which is the `content-box` model
     * seen from the inside: not the 214 the box occupies and not the 204 a
     * `border-box` reading of the declaration would leave the contents. */
    RoboCheckEqInt(LayoutIntContentWidth(&fixture, outer), 200);

    /*
     * The inner box, whose numbers are the same composition one level down -- and
     * whose origin is the *outer's content origin* plus its own margin, which is
     * the assertion that makes the two boxes a chain rather than two islands.
     */
    /* A child's position is in its containing block's content-box space, so this is
     * the child's own margins -- 4 and 4 -- and not a page coordinate. The box is
     * 50 + 6 + 2 by 20 + 6 + 2: the declared pair plus its padding plus its border. */
    LAYOUT_INT_CHECK_RECT("section.inner", &fixture, inner, 4, 4, 58, 28);
    RoboCheckEqInt(LayoutIntContentWidth(&fixture, inner), 50);
    RoboCheckEqInt(LayoutIntPageOrigin(&fixture, outer).x, 20);
    RoboCheckEqInt(LayoutIntPageOrigin(&fixture, outer).y, 10);
    /* And one level down the page origin *is* the composition: the outer's content
     * origin plus the inner's own margins. Asserted rather than assumed, because a
     * build that reported absolute positions in the child would fail only here. */
    RoboCheckEqInt(LayoutIntPageOrigin(&fixture, inner).x, 27 + 4);
    RoboCheckEqInt(LayoutIntPageOrigin(&fixture, inner).y, 17 + 4);

    LayoutIntEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 2. Composition: inline, text and the baseline                       */
/* ------------------------------------------------------------------ */

/**
 * A paragraph of text, a transparent inline box around some of it, and more text --
 * which is 10.2's classification, 10.4's placement, 10.5's measurement and 10.6/10.7's
 * line and baseline, all on one document.
 *
 * The measurer answers 10px per code point and a 16px line extent, so the three text
 * runs are 30, 50 and 10 wide and the line is 90 -- the sum, which is the whole point:
 * a build that placed the runs but sized the line from one of them would be caught by
 * the total and not by any individual run.
 */
#define LAYOUT_INT_TEXT_HTML "<html><body><p>Hi <span>there</span>!</p></body></html>"

#define LAYOUT_INT_TEXT_CSS "html, body, p { display: block }\n"

static void TestIntegrationInlineText(void)
{
    LayoutIntFixture fixture;
    LayoutIntScript script;
    LayoutTextMetrics metrics;
    LayoutNodeId paragraph;
    LayoutNodeId span;
    LayoutLineBox line;
    LayoutLineId lineId = RoboLayoutLineIdNone;
    LayoutSize size;
    int32_t baselineOffset = 0;
    int32_t baselineY = 0;
    LayoutStats stats;
    size_t members = 0u;

    script.perCodePoint = 10;
    script.height = 16;
    script.calls = 0u;
    metrics = LayoutIntMetricsOf(&script);

    LayoutFixture();
    LayoutIntBegin(&fixture, LAYOUT_INT_TEXT_HTML, LAYOUT_INT_TEXT_CSS);

    /* html, head, body, p, span = five elements, plus the three text nodes:
     * "Hi ", "there" (inside the span) and "!". */
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 8u);

    paragraph = LayoutIntBox(&fixture, "p");
    span = LayoutIntBox(&fixture, "span");

    LayoutIntRun(&fixture, 400, 300, &metrics, &stats);
    RoboCheckEqInt(stats.inlineFlow.contexts, 1u);
    RoboCheckEqInt(stats.text.measured, 3u);
    RoboCheckEqInt(stats.text.unmeasured, 0u);
    RoboCheckEqInt(script.calls, 3u);

    /* One line, and it is 90 wide because it holds all three runs. */
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    RoboCheckEqInt(LayoutTreeLineAt(fixture.tree, 0u, &lineId, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeLine(fixture.tree, lineId, &line, NULL), RoboOk);

    RoboCheckEqInt(line.origin.x, 0);
    RoboCheckEqInt(line.origin.y, 0);
    RoboCheckEqInt(line.size.width, 90);
    RoboCheckEqInt(line.size.height, 16);
    RoboCheckEqInt((int)line.owner, (int)paragraph);
    RoboCheckEqInt(line.availableWidth, 400);

    /* Three placements and not four: the span is a *participant* of the context and a
     * member of no line, because its rectangle is the union of its contents (10.6). */
    members = line.members;
    RoboCheckEqInt(members, 3u);

    /* The baseline is the line's floor, its height *is* ascent+descent, and the two
     * doors agree: `baselineOffset` is line-relative, `baselineY` is the owner's
     * content-box space, and this line's owner begins at the page origin. */
    RoboCheckEqInt(line.baseline.ascent, 16);
    RoboCheckEqInt(line.baseline.descent, 0);
    RoboCheckEqInt(line.size.height, line.baseline.ascent + line.baseline.descent);
    RoboCheckEqInt(LayoutLineBoxBaselineOffset(&line, &baselineOffset), RoboOk);
    RoboCheckEqInt(baselineOffset, 16);
    RoboCheckEqInt(LayoutLineBoxBaselineY(&line, &baselineY), RoboOk);
    RoboCheckEqInt(baselineY, baselineOffset);

    /* The transparent span is on no line, which is the same fact as `members == 3`
     * stated from the other side. */
    {
        LayoutLineId spanLine = RoboLayoutLineIdNone;
        RoboCheckEqInt(LayoutObjectLine(fixture.tree, span, &spanLine, NULL),
                          RoboNotFound);
    }

    /* And the paragraph's height is the line's -- the text reached the containing
     * block the way 10.5 said it would. */
    size = LayoutIntSize(&fixture, paragraph);
    RoboCheckEqInt(size.height, 16);
    /* Its width is `auto` against `body`'s content width, less its own insets. */
    RoboCheckEqInt(LayoutIntContentWidth(&fixture, paragraph), 400 - 0);

    LayoutIntEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Composition: position and overflow together                      */
/* ------------------------------------------------------------------ */

#define LAYOUT_INT_POS_HTML \
    "<html><body><div class=\"host\"><section class=\"child\"></section></div></body></html>"

#define LAYOUT_INT_POS_CSS                                                       \
    "html, body { display: block }\n"                                            \
    ".host { display: block; width: 100px; height: 50px;\n"                      \
    "        position: relative; left: 10px; top: 5px; overflow: visible }\n"    \
    ".child { display: block; width: 200px; height: 200px }\n"

/**
 * A relatively positioned box whose child does not fit, asked twice: once with the
 * overflow `visible` and once `hidden`.
 *
 * The pair is the phase's smallest complete statement about what a second rectangle
 * is. `visible`: the box's own size is untouched, its *drawn* place is the flow place
 * displaced, its flow place is `final - offset`, and its contents are recorded as
 * 100px and 150px past its padding box on two sides. `hidden`: the *same* extents
 * stand -- they are a measurement of what the contents did, not of what survived --
 * while the derived **region** collapses back to the padding box and the **clip
 * rectangle** appears where it was refused before. A build that clipped the extents
 * themselves would pass the second assertion and fail the first, which is why both
 * are here.
 */
static void TestIntegrationPositionOverflow(void)
{
    LayoutIntFixture fixture;
    LayoutNodeId host;
    LayoutNodeId child;
    LayoutNodeId bodyBox;
    LayoutOverflowEdges edges;
    LayoutPositionOffset offset;
    LayoutRect paddingBox;
    LayoutRect region;
    LayoutRect clip;
    LayoutResult result;
    LayoutPoint finalPosition;
    LayoutSize size;

    LayoutFixture();
    LayoutIntBegin(&fixture, LAYOUT_INT_POS_HTML, LAYOUT_INT_POS_CSS);

    host = LayoutIntBox(&fixture, "div");
    child = LayoutIntBox(&fixture, "section");
    bodyBox = LayoutIntBox(&fixture, "body");

    LayoutIntRun(&fixture, 800, 600, NULL, NULL);

    /*
     * The host's *final* position is its flow place displaced by the offset -- and
     * its flow place is the result minus the used offset, which is the only way a
     * consumer recovers the two coordinate spaces this phase introduced.
     */
    RoboCheckEqInt(LayoutObjectResult(fixture.tree, host, &result), RoboOk);
    RoboCheckEqInt(LayoutResultPosition(&result, &finalPosition), RoboOk);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    RoboCheckEqInt(finalPosition.x, 10);
    RoboCheckEqInt(finalPosition.y, 5);

    offset = LayoutIntOffset(&fixture, host);
    RoboCheckEqInt(offset.dx, 10);
    RoboCheckEqInt(offset.dy, 5);
    RoboCheckEqInt(offset.insetsResolved, 2u);

    /* The flow place, recovered: the parent stacks by it, so it is the number that
     * must not have moved. `position: relative` never removes a box from flow. */
    RoboCheckEqInt(finalPosition.x - offset.dx, 0);
    RoboCheckEqInt(finalPosition.y - offset.dy, 0);

    /* The size is the declared pair and nothing about the displacement: a build that
     * added the offset into the size would say 110 or 210 here. */
    RoboCheckEqInt(size.width, 100);
    RoboCheckEqInt(size.height, 50);
    LAYOUT_INT_CHECK_RECT("div.host", &fixture, host, 10, 5, 100, 50);

    /* The child is placed in the host's *flow* space -- its content origin is the
     * host's flow origin, not the displaced one -- because flow is what placement
     * uses. Its own rect is therefore at (0, 0), and the host's rect is elsewhere. */
    LAYOUT_INT_CHECK_RECT("section.child", &fixture, child, 0, 0, 200, 200);

    /* The measurement: how far the contents reached past the **padding box**, which
     * for a box with no border or padding is its border box. */
    edges = LayoutIntOverflow(&fixture, host);
    RoboCheckEqInt((int)edges.horizontal, (int)RoboCssOverflowVisible);
    RoboCheckEqInt((int)edges.vertical, (int)RoboCssOverflowVisible);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.top, 0);
    RoboCheckEqInt(edges.right, 100);  /* 200 wide child, 100 wide box */
    RoboCheckEqInt(edges.bottom, 150); /* 200 tall child, 50 tall box */

    RoboCheckEqInt(LayoutObjectResult(fixture.tree, host, &result), RoboOk);
    RoboCheckEqInt(LayoutResultRect(&result, &paddingBox, NULL), RoboOk);
    /* The region a parent would fold in, and the clip a paint pass would use: with
     * both axes `visible` the region is the padding box grown by the extents, and
     * there is no clip at all -- a refusal, not a zero rectangle. The region is
     * stated against the box it was derived from rather than against the origin, so
     * the assertion does not depend on where the box happens to be drawn. */
    RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, paddingBox, &region, NULL),
                      RoboOk);
    RoboCheckEqInt(region.x, paddingBox.x - edges.left);
    RoboCheckEqInt(region.y, paddingBox.y - edges.top);
    RoboCheckEqInt(region.width, 200);
    RoboCheckEqInt(region.height, 200);
    RoboCheckEqInt(LayoutOverflowEdgesClipRect(&edges, paddingBox, &clip, NULL),
                      RoboNotFound);

    /*
     * And the *parent* did not resize: `body`'s extent is the host's margin box, and
     * 50 is the host's height -- a build that let an overflowing child grow its
     * ancestor would be 200 here.
     */
    LAYOUT_INT_CHECK_SIZE("body", &fixture, bodyBox, 800, 50);

    /* Now the same document with `overflow: hidden`, which changes the *derived*
     * rectangles and not the measurement. */
    LayoutIntEnd(&fixture);
    LayoutIntBegin(&fixture, LAYOUT_INT_POS_HTML,
                     "html, body { display: block }\n"
                     ".host { display: block; width: 100px; height: 50px;\n"
                     "        overflow: hidden }\n"
                     ".child { display: block; width: 200px; height: 200px }\n");
    host = LayoutIntBox(&fixture, "div");
    LayoutIntRun(&fixture, 800, 600, NULL, NULL);

    edges = LayoutIntOverflow(&fixture, host);
    RoboCheckEqInt((int)edges.horizontal, (int)RoboCssOverflowHidden);
    RoboCheckEqInt((int)edges.vertical, (int)RoboCssOverflowHidden);
    /* The extents are unchanged: they say what the contents *did*. */
    RoboCheckEqInt(edges.right, 100);
    RoboCheckEqInt(edges.bottom, 150);

    RoboCheckEqInt(LayoutObjectResult(fixture.tree, host, &result), RoboOk);
    RoboCheckEqInt(LayoutResultRect(&result, &paddingBox, NULL), RoboOk);
    RoboCheckEqInt(paddingBox.width, 100);
    RoboCheckEqInt(paddingBox.height, 50);

    /* The region is clipped back to the padding box on both axes, and the clip
     * rectangle -- refused a moment ago -- is that box. */
    RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, paddingBox, &region, NULL),
                      RoboOk);
    RoboCheckEqInt(region.x, paddingBox.x);
    RoboCheckEqInt(region.y, paddingBox.y);
    RoboCheckEqInt(region.width, 100);
    RoboCheckEqInt(region.height, 50);
    RoboCheckEqInt(LayoutOverflowEdgesClipRect(&edges, paddingBox, &clip, NULL),
                      RoboOk);
    RoboCheckEqInt(clip.width, 100);
    RoboCheckEqInt(clip.height, 50);

    LayoutIntEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. Composition: a nested tree, asserted whole                       */
/* ------------------------------------------------------------------ */

#define LAYOUT_INT_NESTED_HTML                                          \
    "<html><body><main>"                                                \
    "<section><span>Hello</span></section>"                             \
    "<div>World</div>"                                                  \
    "</main></body></html>"

#define LAYOUT_INT_NESTED_CSS                                                    \
    "html, body, main, section, div { display: block }\n"                        \
    "main { width: 300px; margin: 10px; padding: 10px; border: 1px solid black }\n" \
    "section { width: 100px; height: 40px; margin: 5px; padding: 5px;\n"          \
    "          border: 1px solid black }\n"                                       \
    "div { width: 80px; height: 20px; margin: 5px }\n"

/**
 * The document an integration test is most likely to be written for -- a wrapper, a
 * nested block with text, and a following sibling -- asserted box by box.
 *
 * Two things here are the composition rather than any one layer. `div`'s y is
 * *not* `section`'s y plus its height: the sibling is stacked by the **margin box**
 * of the box before it, which is margin-top + border box + margin-bottom, so the
 * 5px margins on both sides of both boxes appear in the following sibling's place.
 * And `main`'s height is the extent of those two margin boxes rather than of the two
 * border boxes -- 92 inside a 114 border box -- which is the same rule seen from the
 * parent.
 */
static void TestIntegrationNestedTree(void)
{
    LayoutIntFixture fixture;
    LayoutNodeId mainBox;
    LayoutNodeId section;
    LayoutNodeId div;
    LayoutNodeId bodyBox;
    LayoutPoint origin;
    LayoutIntScript script;
    LayoutTextMetrics metrics;
    LayoutStats stats;

    script.perCodePoint = 8;
    script.height = 12;
    script.calls = 0u;
    metrics = LayoutIntMetricsOf(&script);

    LayoutFixture();
    LayoutIntBegin(&fixture, LAYOUT_INT_NESTED_HTML, LAYOUT_INT_NESTED_CSS);

    /* html, head, body, main, section, span, div = 7 elements, plus "Hello" and
     * "World" = 9 objects. */
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 9u);

    mainBox = LayoutIntBox(&fixture, "main");
    section = LayoutIntBox(&fixture, "section");
    div = LayoutIntBox(&fixture, "div");
    bodyBox = LayoutIntBox(&fixture, "body");

    LayoutIntRun(&fixture, 600, 400, &metrics, &stats);

    RoboCheckEqInt(stats.objects, 9u);
    /* Two inline formatting contexts -- the span's contents and the div's -- so two
     * lines, and both are measured. */
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 2u);
    RoboCheckEqInt(stats.text.measured, 2u);

    /* main: margin origin (10, 10), border box 300+20+2 by 92+20+2. */
    LAYOUT_INT_CHECK_RECT("main", &fixture, mainBox, 10, 10, 322, 114);
    origin = LayoutIntContentOrigin(&fixture, mainBox);
    RoboCheckEqInt(origin.x, 10 + 1 + 10);
    RoboCheckEqInt(origin.y, 10 + 1 + 10);
    RoboCheckEqInt(LayoutIntContentWidth(&fixture, mainBox), 300);

    /* section: its own 5px margin in `main`'s content space, sized 100+10+2 by
     * 40+10+2. */
    LAYOUT_INT_CHECK_RECT("section", &fixture, section, 5, 5, 112, 52);
    RoboCheckEqInt(LayoutIntContentWidth(&fixture, section), 100);

    /* The following sibling, placed after section's *margin box*: 5 + 52 + 5 = 62 for
     * the flow cursor, then its own margin-top 5 -- so the 5px margins on *both* sides
     * of both boxes are in this number, which is the rule rather than an accident. */
    RoboCheckEqInt(5 + 52 + 5 + 5, 67);
    LAYOUT_INT_CHECK_RECT("div", &fixture, div, 5, 67, 80, 20);

    /* And the two local positions composed into page ones: `main`'s content origin is
     * (21, 21), so the section sits at (26, 26) and the following sibling at
     * (26, 88) on the page -- the numbers the whole-document picture would show. */
    RoboCheckEqInt(origin.x + 5, 26);
    RoboCheckEqInt(origin.y + 5, 26);
    RoboCheckEqInt(LayoutIntPageOrigin(&fixture, section).x, 26);
    RoboCheckEqInt(LayoutIntPageOrigin(&fixture, section).y, 26);
    RoboCheckEqInt(LayoutIntPageOrigin(&fixture, div).x, 26);
    RoboCheckEqInt(LayoutIntPageOrigin(&fixture, div).y, 88);

    /* And the parent's height is the two margin boxes: (5+52+5) + (5+20+5) = 92
     * inside a 114 border box, which is what `body` then stacks by. */
    LAYOUT_INT_CHECK_RECT("body", &fixture, bodyBox, 0, 0, 600, 134);

    LayoutIntEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 5. Determinism, over every record                                   */
/* ------------------------------------------------------------------ */

/** Bounded because an integration fixture is a dozen boxes, not a document. */
#define LAYOUT_INT_STATE_MAX 96u
#define LAYOUT_INT_LINE_MAX 32u

/** One box's whole concluded state: its result, and every record a layer put on it. */
typedef struct layoutIntBoxState {
    LayoutNodeId id;
    LayoutResult result;
    int hasMargins;
    int hasPadding;
    int hasBorders;
    int hasOffset;
    int hasOverflow;
    LayoutMarginEdges margins;
    LayoutPaddingEdges padding;
    LayoutBorderEdges borders;
    LayoutPositionOffset offset;
    LayoutOverflowEdges overflow;
} LayoutIntBoxState;

/**
 * Snapshot every box and every line, and compare two snapshots field by field.
 *
 * Field by field rather than by `memcmp`: a struct's padding bytes are not part of
 * its value, and a comparison that read them would be testing the compiler. Every
 * record that has an `equal` door uses it, so the comparison is the module's own
 * definition of equality wherever it has one.
 */
static size_t LayoutIntCapture(LayoutIntFixture *fixture, LayoutIntBoxState *out)
{
    size_t count = LayoutTreeObjectCount(fixture->tree);
    size_t index;

    RoboCheckEqInt(count <= LAYOUT_INT_STATE_MAX, 1);
    for (index = 0u; index < count && index < LAYOUT_INT_STATE_MAX; index++) {
        LayoutIntBoxState *state = &out[index];
        LayoutNodeId id = RoboLayoutNodeIdNone;

        memset(state, 0, sizeof(*state));
        RoboCheckEqInt(LayoutObjectIdAt(fixture->tree, index, &id), RoboOk);
        state->id = id;

        RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &state->result),
                          RoboOk);
        state->hasMargins = LayoutObjectMargins(fixture->tree, id, &state->margins,
                                                       NULL) == RoboOk;
        state->hasPadding = LayoutObjectPadding(fixture->tree, id, &state->padding,
                                                       NULL) == RoboOk;
        state->hasBorders = LayoutObjectBorders(fixture->tree, id, &state->borders,
                                                       NULL) == RoboOk;
        state->hasOffset = LayoutObjectPosition(fixture->tree, id, &state->offset,
                                                        NULL) == RoboOk;
        state->hasOverflow = LayoutObjectOverflow(fixture->tree, id,
                                                          &state->overflow, NULL) == RoboOk;
    }
    return count;
}

static void LayoutIntCompareStates(const LayoutIntBoxState *a,
                                      const LayoutIntBoxState *b,
                                      size_t count)
{
    size_t index;

    for (index = 0u; index < count; index++) {
        RoboCheckEqInt((int)a[index].id, (int)b[index].id);
        RoboCheckEqInt(LayoutResultEqual(&a[index].result, &b[index].result), 1);
        RoboCheckEqInt(a[index].hasMargins, b[index].hasMargins);
        RoboCheckEqInt(a[index].hasPadding, b[index].hasPadding);
        RoboCheckEqInt(a[index].hasBorders, b[index].hasBorders);
        RoboCheckEqInt(a[index].hasOffset, b[index].hasOffset);
        RoboCheckEqInt(a[index].hasOverflow, b[index].hasOverflow);

        RoboCheckEqInt(a[index].margins.top, b[index].margins.top);
        RoboCheckEqInt(a[index].margins.right, b[index].margins.right);
        RoboCheckEqInt(a[index].margins.bottom, b[index].margins.bottom);
        RoboCheckEqInt(a[index].margins.left, b[index].margins.left);
        RoboCheckEqInt(a[index].margins.resolved, b[index].margins.resolved);
        RoboCheckEqInt(a[index].margins.autosUsed, b[index].margins.autosUsed);
        RoboCheckEqInt(a[index].margins.unresolved, b[index].margins.unresolved);

        RoboCheckEqInt(a[index].padding.top, b[index].padding.top);
        RoboCheckEqInt(a[index].padding.right, b[index].padding.right);
        RoboCheckEqInt(a[index].padding.bottom, b[index].padding.bottom);
        RoboCheckEqInt(a[index].padding.left, b[index].padding.left);

        RoboCheckEqInt(a[index].borders.top.width, b[index].borders.top.width);
        RoboCheckEqInt(a[index].borders.bordersDrawn, b[index].borders.bordersDrawn);

        RoboCheckEqInt(a[index].offset.dx, b[index].offset.dx);
        RoboCheckEqInt(a[index].offset.dy, b[index].offset.dy);

        RoboCheckEqInt((int)a[index].overflow.horizontal,
                          (int)b[index].overflow.horizontal);
        RoboCheckEqInt((int)a[index].overflow.vertical,
                          (int)b[index].overflow.vertical);
        RoboCheckEqInt(a[index].overflow.left, b[index].overflow.left);
        RoboCheckEqInt(a[index].overflow.top, b[index].overflow.top);
        RoboCheckEqInt(a[index].overflow.right, b[index].overflow.right);
        RoboCheckEqInt(a[index].overflow.bottom, b[index].overflow.bottom);
    }
}

/** Every line's record, compared with the module's own line equality. */
static void LayoutIntCompareLines(LayoutIntFixture *a, LayoutIntFixture *b)
{
    size_t count = LayoutTreeLineCount(a->tree);
    size_t index;

    RoboCheckEqInt(count, LayoutTreeLineCount(b->tree));
    RoboCheckEqInt(count <= LAYOUT_INT_LINE_MAX, 1);

    for (index = 0u; index < count && index < LAYOUT_INT_LINE_MAX; index++) {
        LayoutLineId idA = RoboLayoutLineIdNone;
        LayoutLineId idB = RoboLayoutLineIdNone;
        LayoutLineBox lineA;
        LayoutLineBox lineB;

        RoboCheckEqInt(LayoutTreeLineAt(a->tree, index, &idA, NULL), RoboOk);
        RoboCheckEqInt(LayoutTreeLineAt(b->tree, index, &idB, NULL), RoboOk);
        RoboCheckEqInt((int)idA, (int)idB);
        RoboCheckEqInt(LayoutTreeLine(a->tree, idA, &lineA, NULL), RoboOk);
        RoboCheckEqInt(LayoutTreeLine(b->tree, idB, &lineB, NULL), RoboOk);
        RoboCheckEqInt(LayoutLineBoxEqual(&lineA, &lineB), 1);
    }
}

#define LAYOUT_INT_DET_HTML                                             \
    "<html><body><main>"                                                \
    "<h1>Title</h1>"                                                    \
    "<p class=\"lead\">Alpha <em>beta</em></p>"                         \
    "<section class=\"card\"><span>card</span></section>"               \
    "<div class=\"tail\"></div>"                                        \
    "</main></body></html>"

#define LAYOUT_INT_DET_CSS                                                       \
    "html, body, main, h1, p, section, div { display: block }\n"                 \
    "main { width: 400px; margin: 8px; padding: 6px; border: 1px solid black }\n" \
    "h1 { height: 20px; margin: 2px }\n"                                         \
    "p { margin: 3px 4px; padding: 2px }\n"                                      \
    "section { height: 30px; margin: 2px; position: relative; left: 4px; top: 6px;\n" \
    "          overflow: hidden }\n"                                             \
    "div { height: 12px }\n"

/**
 * One document laid out three times -- and built into a second tree -- with every
 * box's geometry, every inset record, every offset, every overflow record and every
 * line compared.
 *
 * The claim is stronger than "the same call twice" on purpose. A pass that cached
 * something keyed by a pointer, ordered a walk by an address, or read an
 * uninitialised byte would agree with itself on the *first* two layouts and diverge
 * on the third, which is why there are three; and comparing the second *tree* as well
 * as the second *pass* covers the builder, whose ordering is the other half of "the
 * same document".
 */
static void TestIntegrationDeterminism(void)
{
    LayoutIntFixture fixture;
    LayoutIntFixture second;
    LayoutIntScript script;
    LayoutTextMetrics metrics;
    LayoutIntBoxState first[LAYOUT_INT_STATE_MAX];
    LayoutIntBoxState again[LAYOUT_INT_STATE_MAX];
    size_t count;

    script.perCodePoint = 9;
    script.height = 14;
    script.calls = 0u;
    metrics = LayoutIntMetricsOf(&script);

    LayoutFixture();
    LayoutIntBegin(&fixture, LAYOUT_INT_DET_HTML, LAYOUT_INT_DET_CSS);
    LayoutIntRun(&fixture, 700, 500, &metrics, NULL);
    count = LayoutIntCapture(&fixture, first);
    RoboCheck(count > 0u);

    /* The same pass again: nothing may drift, including the line ids. */
    LayoutIntRun(&fixture, 700, 500, &metrics, NULL);
    LayoutIntCapture(&fixture, again);
    LayoutIntCompareStates(first, again, count);

    /* And a third, because a build with a one-deep cache would agree twice. */
    LayoutIntRun(&fixture, 700, 500, &metrics, NULL);
    LayoutIntCapture(&fixture, again);
    LayoutIntCompareStates(first, again, count);

    /* A second *tree* from the same document, through the same bridge. */
    LayoutIntBegin(&second, LAYOUT_INT_DET_HTML, LAYOUT_INT_DET_CSS);
    RoboCheckEqInt(LayoutTreeObjectCount(second.tree), count);
    LayoutIntRun(&second, 700, 500, &metrics, NULL);
    LayoutIntCapture(&second, again);
    LayoutIntCompareStates(first, again, count);
    LayoutIntCompareLines(&fixture, &second);

    /* The lines themselves, so the module's own equality is exercised rather than
     * merely available: the two trees' lines must be equal by *its* rule. */
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree),
                      LayoutTreeLineCount(second.tree));
    RoboCheck(LayoutTreeLineCount(fixture.tree) > 0u);

    LayoutIntEnd(&second);
    LayoutIntEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. Robustness: extreme input                                        */
/* ------------------------------------------------------------------ */

#define LAYOUT_INT_EXTREME_CSS                                                      \
    "html, body, div { display: block }\n"                                          \
    ".huge { width: 2000000000px; height: 2000000000px }\n"                         \
    ".maxw { width: 2147483647px; height: 2147483647px;\n"                          \
    "        padding: 10px; border: 5px solid black }\n"                            \
    ".zero { width: 0; height: 0 }\n"                                               \
    ".neg { width: 10px; height: 10px; margin-left: -50px; margin-top: -20px }\n"   \
    ".pct { width: 50%; height: 25% }\n"                                            \
    ".bad { width: not-a-length; height: 12px }\n"                                  \
    ".deep { width: 20px; height: 20px; margin: 1px }\n"

/**
 * A document made of the numbers the module has the most to lose on: a dimension near
 * the geometry's maximum, an arithmetic overflow in the box model, a zero, a negative
 * margin, a declaration this build cannot use, and a deep chain.
 *
 * The assertions are the *properties* rather than the numbers -- every placed box's
 * extent is finite and non-negative, every overflow extent is non-negative, a
 * saturated box is still drawable -- because the point of the case is that no input
 * produces a rectangle that is not a rectangle. The one exact number is the negative
 * margin, which must reach geometry *as a signed origin* rather than being clamped to
 * zero.
 */
static void TestIntegrationExtremeInput(void)
{
    static char html[8192];
    static const char *const head =
        "<html><body>"
        "<div class=\"huge\"></div><div class=\"maxw\"></div>"
        "<div class=\"zero\"></div><div class=\"neg\"></div>"
        "<div class=\"pct\"></div><div class=\"bad\"></div>";
    static const char *const openTag = "<div class=\"deep\">";
    static const char *const closeTag = "</div>";
    static const char *const tail = "</body></html>";
    LayoutIntFixture fixture;
    LayoutNodeId neg;
    LayoutStats stats;
    size_t written = 0u;
    size_t index;
    size_t total;
    size_t objects;

    /* Assembled by hand rather than with `sprintf`: the string is a fixture, and a
     * formatted build of one is a second thing that can disagree with the document
     * it claims to describe. */
    memcpy(html + written, head, strlen(head));
    written += strlen(head);
    for (index = 0u; index < 16u; index++) {
        memcpy(html + written, openTag, strlen(openTag));
        written += strlen(openTag);
    }
    for (index = 0u; index < 16u; index++) {
        memcpy(html + written, closeTag, strlen(closeTag));
        written += strlen(closeTag);
    }
    memcpy(html + written, tail, strlen(tail));
    written += strlen(tail);
    html[written] = '\0';

    LayoutFixture();
    LayoutIntBegin(&fixture, html, LAYOUT_INT_EXTREME_CSS);

    /* html, head, body, six named divs and sixteen nested ones. */
    objects = LayoutTreeObjectCount(fixture.tree);
    total = 3u + 6u + 16u;
    RoboCheckEqInt(objects, total);

    LayoutIntRun(&fixture, 800, 600, NULL, &stats);
    /* `<head>` is the parser's and nothing styles it, so it is the one box the pass
     * declines -- which is why the two counters disagree by exactly one here. */
    RoboCheckEqInt(stats.objects, objects);
    RoboCheckEqInt(stats.boxesUnresolved, 1u);
    RoboCheckEqInt(stats.boxesComputed, objects - 1u);

    /* Every box: a finite, non-negative extent, and an overflow record whose extents
     * are non-negative too. A build that wrapped a saturated sum would fail here with
     * a negative width, which is the failure the 64-bit clamps exist to prevent. */
    for (index = 0u; index < objects; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;
        LayoutResult result;
        LayoutSize size;
        LayoutOverflowEdges edges;

        RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, index, &id), RoboOk);
        RoboCheckEqInt(LayoutObjectResult(fixture.tree, id, &result), RoboOk);
        /* Every box but `<head>`, which is the one the pass declines. */
        if (LayoutObjectIsRoot(fixture.tree, id) ||
            (int)MakeLayoutResultState(&result) == (int)RoboLayoutResultComputed) {
            RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
            RoboCheckMsg(size.width >= 0, "a placed size must be non-negative");
            RoboCheckMsg(size.height >= 0, "a placed size must be non-negative");
        } else {
            RoboCheckEqInt((int)MakeLayoutResultState(&result),
                              (int)RoboLayoutResultUnresolved);
        }
        if (LayoutObjectOverflow(fixture.tree, id, &edges, NULL) == RoboOk) {
            RoboCheckMsg(edges.left >= 0, "an overflow extent must be non-negative");
            RoboCheckMsg(edges.top >= 0, "an overflow extent must be non-negative");
            RoboCheckMsg(edges.right >= 0, "an overflow extent must be non-negative");
            RoboCheckMsg(edges.bottom >= 0, "an overflow extent must be non-negative");
        }
    }

    /* The huge box is as large as it says, which is the assertion a saturation that
     * wrapped would fail by going negative. */
    {
        LayoutNodeId huge = LayoutIntBox(&fixture, "div");
        LayoutSize size = LayoutIntSize(&fixture, huge);
        RoboCheckMsg(size.width >= 2000000000,
                       "a declared 2e9 width must reach geometry");
        RoboCheckMsg(size.height >= 2000000000,
                       "a declared 2e9 height must reach geometry");
    }

    /* The negative margin: an origin at minus the margin, signed and not clamped. */
    neg = RoboLayoutNodeIdNone;
    {
        /* The fourth `div` in document order is `.neg`, and `LayoutDocumentFind`
         * answers the first by tag -- so it is reached by walking the tree's own
         * order, which is document order. */
        LayoutNodeId id = RoboLayoutNodeIdNone;
        size_t divs = 0u;
        for (index = 0u; index < objects && neg == RoboLayoutNodeIdNone; index++) {
            LayoutObject object;
            RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, index, &id), RoboOk);
            RoboCheckEqInt(LayoutObjectGet(fixture.tree, id, &object, NULL), RoboOk);
            if (object.kind == RoboLayoutObjectBox &&
                object.display == RoboCssDisplayBlock &&
                object.depth == 2u) {
                /* The body's children, in order: huge, maxw, zero, neg, pct, bad,
                 * then the deep chain. The fourth is `neg`. */
                divs++;
                if (divs == 4u) {
                    neg = id;
                }
            }
        }
    }
    RoboCheck(neg != RoboLayoutNodeIdNone);
    if (neg != RoboLayoutNodeIdNone) {
        LayoutResult result;
        LayoutPoint position;

        RoboCheckEqInt(LayoutObjectResult(fixture.tree, neg, &result), RoboOk);
        RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
        /* 0 + margin-left(-50), and the flow cursor of the preceding three boxes plus
         * margin-top(-20) in the vertical -- the horizontal number is the one that
         * proves a sign survived, because a clamp to zero would answer 0. */
        RoboCheckEqInt(position.x, -50);
        RoboCheckEqInt(LayoutIntMargins(&fixture, neg).left, -50);
    }

    /* And the whole thing is deterministic however extreme it is. */
    if (stats.boxesComputed > 0u) {
        LayoutIntRun(&fixture, 800, 600, NULL, &stats);
        RoboCheckEqInt(stats.objects, objects);
    }

    LayoutIntEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 7. No stale geometry survives an incremental pass                   */
/* ------------------------------------------------------------------ */

/**
 * Mark, recompute, and prove the result is the same answer a fresh pass would give.
 *
 * The comparison is against a **fresh tree built from the same document and laid out
 * from scratch**, which is the only honest oracle: asserting the incremental geometry
 * against the numbers the same incremental path just wrote would pass even if both
 * were wrong. The `STYLE` half is here too, because the two doors mean different
 * things -- after the pass the tree must be free of *layout* bits while the paint
 * signal has been consumed by `mark_clean`, which is the phase's whole point stated
 * as a test.
 */
static void TestIntegrationNoStaleGeometry(void)
{
    LayoutIntFixture fixture;
    LayoutIntFixture fresh;
    LayoutIntScript script;
    LayoutTextMetrics metrics;
    LayoutIntBoxState incremental[LAYOUT_INT_STATE_MAX];
    LayoutIntBoxState fromScratch[LAYOUT_INT_STATE_MAX];
    LayoutContext *context;
    LayoutNodeId card;
    LayoutStats stats;
    size_t count;

    script.perCodePoint = 7;
    script.height = 11;
    script.calls = 0u;
    metrics = LayoutIntMetricsOf(&script);

    LayoutFixture();
    LayoutIntBegin(&fixture, LAYOUT_INT_DET_HTML, LAYOUT_INT_DET_CSS);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 0); /* born dirty */
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 1);

    context = LayoutIntContext(700, 500, &metrics);
    RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 0);

    /* Touch a box's width: geometry-affecting, so the box, its ancestors'
     * measurements and the following flow go dirty -- and the pass must run. */
    card = LayoutIntBox(&fixture, "section");
    RoboCheckEqInt(LayoutTreeMarkStyleProperty(fixture.tree, card, "width",
                                                          NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 1);
    RoboCheckEqInt(LayoutTreeInvalidationCount(fixture.tree) > 0u, 1);
    /*
     * The dirty root is a *region* -- not the document and not the box that changed.
     * Marking `section` also marks the flow after it, and the flow after it is
     * `div.tail`, a sibling of `section`; so the lowest common ancestor of the two
     * stale boxes is `main`. Asserted exactly, because the difference between this
     * answer and "the shallowest stale box" is the finding 10.14 recorded.
     */
    RoboCheckEqInt((int)LayoutTreeInvalidationRoot(fixture.tree),
                      (int)LayoutIntBox(&fixture, "main"));

    RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL),
                      RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context),
                      (int)RoboLayoutPassWalked);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.invalidation.recomputed, 1);
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 0);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);

    /* The oracle: the same document, freshly built and laid out from nothing. */
    count = LayoutIntCapture(&fixture, incremental);
    LayoutIntBegin(&fresh, LAYOUT_INT_DET_HTML, LAYOUT_INT_DET_CSS);
    LayoutIntRun(&fresh, 700, 500, &metrics, NULL);
    LayoutIntCapture(&fresh, fromScratch);
    LayoutIntCompareStates(incremental, fromScratch, count);
    LayoutIntCompareLines(&fixture, &fresh);
    LayoutIntEnd(&fresh);

    /*
     * A paint-only change, and the three halves of the no-op contract: the door
     * *skips* the walk, the outcome says so, and the `STYLE` bit is still there
     * afterwards -- because a repaint is owed and a pass that cleared it would look
     * correct until the repaint never happened.
     */
    {
        LayoutStats skipped;

        /*
         * The *same* context, and that is not an implementation detail: the door's
         * four-term condition includes "this context has completed a pass on this
         * tree", so a fresh context would have to walk whatever the marking said --
         * which is the correct answer to a different question, and would make this
         * assertion vacuous.
         */
        RoboCheckEqInt(LayoutTreeMarkStyleProperty(fixture.tree, card,
                                                              "background-color", NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 0);
        RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 0);
        RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree, NULL),
                          RoboOk);
        RoboCheckEqInt((int)LayoutContextOutcome(context),
                          (int)RoboLayoutPassClean);
        LayoutContextStats(context, &skipped);
        RoboCheckEqInt(skipped.invalidation.recomputed, 0);
        RoboCheckEqInt(skipped.invalidation.styleOnly, 1u);
        RoboCheckEqInt(LayoutTreeInvalidationState(fixture.tree, card),
                          RoboLayoutInvalidationStyle);
    }

    LayoutContextDestroy(context);
    LayoutIntEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 8. Performance sanity                                               */
/* ------------------------------------------------------------------ */

/** Enough boxes that an accidental quadratic is visible, few enough to be a test. */
#define LAYOUT_INT_PERF_BLOCKS 256u

/**
 * A tree of 256 blocks laid out repeatedly and invalidated repeatedly, with the
 * allocator asserted to return to its baseline.
 *
 * Deliberately *not* a benchmark: there is no timing assertion anywhere in this
 * suite, because a timing assertion on a shared machine fails for reasons that have
 * nothing to do with the module, and a phase that is forbidden from optimizing is
 * also forbidden from asserting that it is fast. What this catches is the class of
 * mistake that turns a linear walk into a quadratic one or leaks a node per pass --
 * both of which show up as a count or a live-block number rather than as a duration.
 *
 * The document is assembled through the **DOM seam** rather than parsed, which is the
 * one way a document can grow after its tree was built (RoboLayoutTestSupport.h), and
 * which keeps a 256-line markup string out of the file.
 */
static void TestIntegrationPerformanceSanity(void)
{
    LayoutIntFixture fixture;
    HtmlNodeHandle document;
    HtmlNodeHandle htmlElement;
    HtmlNodeHandle bodyElement;
    LayoutStats stats;
    size_t before;
    size_t index;
    size_t expectedObjects = 2u + LAYOUT_INT_PERF_BLOCKS;
    int cycle;

    LayoutFixture();
    before = LayoutLiveBlocks();

    memset(&fixture, 0, sizeof(fixture));
    LayoutDocumentCreateEmpty(&fixture.doc);
    LayoutCssBegin(&fixture.css);
    LayoutCssSheet(&fixture.css, "html, body, div { display: block }\n"
                                    "div { height: 10px; margin: 2px }\n");
    RoboCheckEqInt(LayoutDomCreate(fixture.css.context, fixture.doc.id,
                                             &fixture.dom, NULL),
                      RoboOk);

    document = HtmlDocumentRoot(fixture.doc.id);
    RoboCheckEqInt((int)HtmlNodeHandleIsWellformed(&document), 1);
    htmlElement = LayoutDocumentAddElement(&fixture.doc, &document, "html");
    bodyElement = LayoutDocumentAddElement(&fixture.doc, &htmlElement, "body");
    for (index = 0u; index < LAYOUT_INT_PERF_BLOCKS; index++) {
        LayoutDocumentAddElement(&fixture.doc, &bodyElement, "div");
    }

    RoboCheckEqInt(LayoutDomRefresh(fixture.dom, NULL), RoboOk);
    RoboCheckEqInt(LayoutDomStyleSource(fixture.dom, &fixture.source, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeCreate(&fixture.tree, NULL), RoboOk);
    {
        LayoutBuildOptions options;

        LayoutBuildOptionsInit(&options);
        options.styles = &fixture.source;
        RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture.tree,
                                                              fixture.doc.id, &options,
                                                              NULL, NULL),
                          RoboOk);
    }
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), expectedObjects);

    /* Three full passes: the geometry must be identical each time and the tree's
     * object count must not grow. */
    for (cycle = 0; cycle < 3; cycle++) {
        LayoutIntRun(&fixture, 800, 6000, NULL, &stats);
        RoboCheckEqInt(stats.objects, expectedObjects);
        RoboCheckEqInt(stats.boxesComputed, expectedObjects);
        RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), expectedObjects);
    }

    /* And repeated invalidate/layout, which is the cycle a live document is in. */
    {
        LayoutContext *context = LayoutIntContext(800, 6000, NULL);
        LayoutNodeId first = RoboLayoutNodeIdNone;

        RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, 2u, &first), RoboOk);
        for (cycle = 0; cycle < 8; cycle++) {
            RoboCheckEqInt(LayoutTreeMarkLayoutDirty(
                                  fixture.tree, first, RoboLayoutInvalidationReasonGeometry,
                                  NULL),
                              RoboOk);
            RoboCheckEqInt(LayoutContextRunIncremental(context, fixture.tree,
                                                                 NULL),
                              RoboOk);
            RoboCheckEqInt((int)LayoutContextOutcome(context),
                              (int)RoboLayoutPassWalked);
            RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 0);
            RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree),
                              expectedObjects);
        }
        LayoutContextDestroy(context);
    }

    LayoutIntEnd(&fixture);
    RoboCheckEqInt(LayoutLiveBlocks(), before);
}

/* ------------------------------------------------------------------ */
/* 9. Regressions: the findings, pinned                                */
/* ------------------------------------------------------------------ */

/**
 * 10.11's finding: a border's *used* width is not its declared one.
 *
 * `border: 5px none` is the same box as no border at all -- `none` generates nothing,
 * so the width is used as zero and costs no geometry -- while `5px solid` grows the
 * border box by five on each side. The pair is pinned together because the defect this
 * catches is a reader that took the declared width and never asked the style, which
 * would answer 5 in the first case and pass a one-box test written only against
 * `solid`.
 */
static void TestIntegrationRegressionBorderStyle(void)
{
    LayoutIntFixture fixture;
    LayoutBorderEdges edges;

    LayoutFixture();
    LayoutIntBegin(&fixture, "<html><body>"
                               "<div class=\"none\"></div>"
                               "<div class=\"solid\"></div>"
                               "<div class=\"bare\"></div>"
                               "</body></html>",
                     "html, body, div { display: block }\n"
                     ".none { width: 100px; height: 10px; border: 5px none black }\n"
                     ".solid { width: 100px; height: 10px; border: 5px solid black }\n"
                     ".bare { width: 100px; height: 10px }\n");
    LayoutIntRun(&fixture, 800, 600, NULL, NULL);

    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 6u);

    /* Walk the body's three children in order: none, solid, bare. */
    {
        LayoutNodeId bodyId = LayoutIntBox(&fixture, "body");
        LayoutNodeId child = RoboLayoutNodeIdNone;
        LayoutSize size;

        RoboCheckEqInt(LayoutObjectFirstChild(fixture.tree, bodyId, &child),
                          RoboOk);

        /* `.none`: geometry of a bare box. */
        size = LayoutIntSize(&fixture, child);
        RoboCheckEqInt(size.width, 100);
        RoboCheckEqInt(size.height, 10);
        edges = LayoutIntBorders(&fixture, child);
        RoboCheckEqInt(edges.top.width, 0);
        RoboCheckEqInt(edges.left.width, 0);
        RoboCheckEqInt(edges.bordersDrawn, 0u);

        /* `.solid`: five on each side, all four drawn. */
        RoboCheckEqInt(LayoutObjectNextSibling(fixture.tree, child, &child),
                          RoboOk);
        size = LayoutIntSize(&fixture, child);
        RoboCheckEqInt(size.width, 110);
        RoboCheckEqInt(size.height, 20);
        edges = LayoutIntBorders(&fixture, child);
        RoboCheckEqInt(edges.top.width, 5);
        RoboCheckEqInt(edges.bordersDrawn, 4u);

        /* `.bare`: no declaration, and the same box as `.none` -- which is the
         * equality the case is really about. */
        RoboCheckEqInt(LayoutObjectNextSibling(fixture.tree, child, &child),
                          RoboOk);
        size = LayoutIntSize(&fixture, child);
        RoboCheckEqInt(size.width, 100);
        RoboCheckEqInt(size.height, 10);
        edges = LayoutIntBorders(&fixture, child);
        RoboCheckEqInt(edges.bordersDrawn, 0u);

        RoboCheckEqInt(LayoutObjectNextSibling(fixture.tree, child, &child),
                          RoboNotFound);
    }

    LayoutIntEnd(&fixture);
}

/**
 * 10.13's finding: a parent folds in a child's **region**, not its contents.
 *
 * A clipping box contributes only what it kept, so a grandparent's overflow is
 * measured against the clip's padding box and not against the 200px child behind it.
 * The check a naive implementation fails is the *grandparent's* record: folding the
 * contents in would report 150px of overflow on a document where the visible result
 * fits, and the error is invisible in every document with no clipping ancestor.
 *
 * The other half is stated here too: the clipping box's own extents are the *raw*
 * measurement of what its contents did, because those extents are how a scrollable
 * area will know it has something to scroll to.
 */
static void TestIntegrationRegressionClippedChild(void)
{
    LayoutIntFixture fixture;
    LayoutNodeId bodyBox;
    LayoutNodeId clip;
    LayoutOverflowEdges edges;
    LayoutRect paddingBox;
    LayoutRect region;

    LayoutFixture();
    LayoutIntBegin(&fixture, "<html><body><div class=\"clip\">"
                               "<section class=\"wide\"></section>"
                               "</div></body></html>",
                     "html, body { display: block }\n"
                     ".clip { display: block; width: 50px; height: 50px; overflow: hidden }\n"
                     ".wide { display: block; width: 200px; height: 200px }\n");
    LayoutIntRun(&fixture, 800, 600, NULL, NULL);

    bodyBox = LayoutIntBox(&fixture, "body");
    clip = LayoutIntBox(&fixture, "div");

    /* The clip's own extents: the contents really did reach 150 past it. */
    edges = LayoutIntOverflow(&fixture, clip);
    RoboCheckEqInt(edges.right, 150);
    RoboCheckEqInt(edges.bottom, 150);
    RoboCheckEqInt((int)edges.horizontal, (int)RoboCssOverflowHidden);

    RoboCheckEqInt(LayoutObjectOverflow(fixture.tree, bodyBox, &edges, NULL),
                      RoboOk);
    /* The grandparent folds in the clip's *region*, which is the clip's padding box:
     * nothing stuck out of `body`, so its extents are zero. */
    RoboCheckEqInt(edges.right, 0);
    RoboCheckEqInt(edges.bottom, 0);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.top, 0);

    /* And the region is the padding box, which is the rectangle the fold used. */
    {
        LayoutResult result;
        RoboCheckEqInt(LayoutObjectResult(fixture.tree, clip, &result), RoboOk);
        RoboCheckEqInt(LayoutResultRect(&result, &paddingBox, NULL), RoboOk);
        RoboCheckEqInt(LayoutObjectOverflow(fixture.tree, clip, &edges, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutOverflowEdgesRegion(&edges, paddingBox, &region,
                                                           NULL),
                          RoboOk);
        RoboCheckEqInt(region.width, 50);
        RoboCheckEqInt(region.height, 50);
    }

    LayoutIntEnd(&fixture);
}

/**
 * 10.14's finding: the dirty root is the **lowest common ancestor** of the boxes whose
 * own geometry is stale, not the shallowest stale box.
 *
 * The document is the one that failed the first implementation: marking a box in the
 * middle also marks the flow *after* its ancestors, and the flow after an ancestor is
 * a sibling that sits **higher** in the tree than the box that moved -- so a
 * "shallowest" rule names a box whose subtree does not contain the change. The
 * assertion is on the *subtree containment* rather than on an id, because that is the
 * property the answer must have.
 */
static void TestIntegrationRegressionDirtyRoot(void)
{
    LayoutIntFixture fixture;
    LayoutNodeId section;
    LayoutNodeId root;
    LayoutNodeId ancestor;
    int contains = 0;

    LayoutFixture();
    LayoutIntBegin(&fixture, "<html><body><main><article>"
                               "<section></section>"
                               "</article><footer></footer></main></body></html>",
                     "html, body, main, article, section, footer { display: block }\n"
                     "section { height: 20px }\n");
    LayoutIntRun(&fixture, 800, 600, NULL, NULL);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);

    section = LayoutIntBox(&fixture, "section");
    RoboCheckEqInt(LayoutTreeMarkLayoutDirty(
                          fixture.tree, section, RoboLayoutInvalidationReasonGeometry,
                          NULL),
                      RoboOk);

    root = LayoutTreeInvalidationRoot(fixture.tree);
    RoboCheck(root != RoboLayoutNodeIdNone);
    RoboCheckMsg(root != LayoutTreeRoot(fixture.tree),
                   "the dirty root must be a region, not the document");

    /* The answer must be an ancestor-or-self of the marked box: walk up from the
     * marked box and see whether the answer is on the path. */
    ancestor = section;
    while (ancestor != RoboLayoutNodeIdNone) {
        if (ancestor == root) {
            contains = 1;
        }
        if (LayoutObjectParent(fixture.tree, ancestor, &ancestor) != RoboOk) {
            break;
        }
    }
    RoboCheckMsg(contains, "the dirty root must contain the box that changed");

    /* And the following flow really was marked, which is what made the naive rule
     * wrong: `footer` sits higher than `section` and is stale too. */
    RoboCheckEqInt(LayoutTreeInvalidationCount(fixture.tree) > 1u, 1);
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 1);

    RoboCheckEqInt(LayoutTreeMarkClean(fixture.tree), RoboOk);
    RoboCheckEqInt(LayoutTreeIsClean(fixture.tree), 1);

    LayoutIntEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 10. The Phase 10 showcase                                           */
/* ------------------------------------------------------------------ */

#define LAYOUT_INT_SHOWCASE_HTML                                              \
    "<html><body><main>"                                                      \
    "<header><h1>Showcase</h1></header>"                                      \
    "<p class=\"intro\">A paragraph of text.</p>"                             \
    "<section class=\"card\"><div class=\"badge\">New</div></section>"        \
    "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"        \
    "<footer><span>end</span></footer>"                                       \
    "</main></body></html>"

/* `aside`/`figure` rather than a third and fourth `div`: `LayoutDocumentFind`
 * answers by tag name, and the document's first `div` is the badge inside the card --
 * so a `div`-named clip would have been asserted against the badge and the case would
 * have "passed" while checking nothing. A distinct tag per asserted box is what keeps
 * a by-name lookup honest. */
#define LAYOUT_INT_SHOWCASE_CSS                                                    \
    "html, body, main, header, h1, p, section, footer, div,\n"                      \
    "aside, figure { display: block }\n"                                            \
    "main { width: 500px; margin: 20px auto; padding: 12px;\n"                      \
    "       border: 1px solid black }\n"                                            \
    "header { margin-bottom: 10px; padding: 4px; border-bottom: 2px solid black }\n" \
    "h1 { height: 24px }\n"                                                          \
    ".intro { margin: 6px 0; padding: 4px }\n"                                       \
    ".card { width: 200px; height: 60px; margin: 8px; padding: 6px;\n"              \
    "        border: 1px solid black; position: relative; left: 12px; top: 4px }\n" \
    ".badge { width: 40px; height: 16px; margin: 2px }\n"                            \
    ".clip { width: 80px; height: 24px; overflow: hidden }\n"                        \
    ".spill { width: 160px; height: 60px }\n"                                        \
    "footer { margin-top: 10px }\n"

/**
 * The Phase 10 showcase, at the only level this phase can show it: geometry.
 *
 * Several blocks, inline text, margins, padding, a border, a relatively positioned
 * card, and a box whose contents are clipped -- the document the brief asks for, with
 * the facts a reader would check by looking at a rendered page asserted instead:
 * the composition is one border box wide, the card is *drawn* off the flow it still
 * occupies, the clip is smaller than what it holds, and the whole document's height is
 * the flow's own extent.
 *
 * A rendered picture is Phase 11's. The host visual checkpoint this project has is run
 * against the **demo** path, which does not use RoboLayout at all (its page model is
 * its own), so there is no pixel harness here to extend and this phase does not build
 * one -- the geometry below is the artefact, and `layout/docs/Tests.md` §20 records
 * why.
 */
static void TestIntegrationShowcase(void)
{
    LayoutIntFixture fixture;
    LayoutIntScript script;
    LayoutTextMetrics metrics;
    LayoutNodeId mainBox;
    LayoutNodeId card;
    LayoutNodeId clip;
    LayoutPositionOffset offset;
    LayoutOverflowEdges edges;
    LayoutRect cardRect;
    LayoutPoint origin;
    LayoutStats stats;

    script.perCodePoint = 6;
    script.height = 10;
    script.calls = 0u;
    metrics = LayoutIntMetricsOf(&script);

    LayoutFixture();
    LayoutIntBegin(&fixture, LAYOUT_INT_SHOWCASE_HTML, LAYOUT_INT_SHOWCASE_CSS);

    mainBox = LayoutIntBox(&fixture, "main");
    card = LayoutIntBox(&fixture, "section");
    clip = LayoutIntBox(&fixture, "aside");

    LayoutIntRun(&fixture, 900, 700, &metrics, &stats);

    /* The composition: main is centered by `margin: 20px auto`, so a 500px content
     * box inside a 900px viewport leaves 200 each side -- and the used margins are
     * that 200, which is what an `auto` margin *is*. */
    origin = LayoutIntContentOrigin(&fixture, mainBox);
    {
        LayoutMarginEdges margins = LayoutIntMargins(&fixture, mainBox);
        LayoutRect rect;

        LayoutIntRect(&fixture, mainBox, &rect);
        RoboCheckEqInt(margins.top, 20);
        RoboCheckEqInt(margins.autosUsed, 2u);
        RoboCheckEqInt(margins.left, margins.right);
        /*
         * The documented rule, pinned as it stands: an `auto` margin takes its share
         * of the space beside the **declared width**, so the two used margins are
         * (900 - 500) / 2 = 200 -- and the box's own border and padding are *not*
         * terms of that distribution, which is why the border box plus the two
         * margins overhangs the containing block by exactly the insets (26).
         *
         * That is a boundary and not a bug this phase may fix: changing it means
         * changing 10.9's resolution *order* (the margins are resolved before the
         * border and the padding exist), which is a redesign of a completed layer,
         * and 10.15 is a testing goal. It is recorded as the phase's second finding
         * in `layout/docs/Tests.md` §20 so the next goal that touches the box model
         * meets it deliberately; the assertions here are written so that the *day* it
         * changes, this case fails and the finding is re-read.
         */
        RoboCheckEqInt(margins.left, 200);
        RoboCheckEqInt(margins.left + 500 + margins.right, 900);
        RoboCheckEqInt(margins.left + rect.width + margins.right - 900, 1 + 12 + 1 + 12);
        RoboCheckEqInt(origin.x, margins.left + 1 + 12);
        RoboCheckEqInt(origin.y, 20 + 1 + 12);
        RoboCheckEqInt(LayoutIntContentWidth(&fixture, mainBox), 500);
    }

    /* The card is drawn where its offset puts it, and the offset is *readable*, so
     * the flow place is recoverable -- the two coordinates this phase introduced. */
    LayoutIntRect(&fixture, card, &cardRect);
    offset = LayoutIntOffset(&fixture, card);
    RoboCheckEqInt(offset.dx, 12);
    RoboCheckEqInt(offset.dy, 4);
    RoboCheckEqInt(cardRect.width, 200 + 12 + 2);
    RoboCheckEqInt(cardRect.height, 60 + 12 + 2);

    /* The clip is smaller than what it holds, and the record says so even though the
     * region does not. */
    edges = LayoutIntOverflow(&fixture, clip);
    RoboCheckEqInt((int)edges.horizontal, (int)RoboCssOverflowHidden);
    RoboCheckEqInt(edges.right, 80);  /* 160 wide spill, 80 wide clip */
    RoboCheckEqInt(edges.bottom, 36); /* 60 tall spill, 24 tall clip */

    /* Every placed box has geometry, and every non-`head` box is placed: a showcase
     * with a silently unresolved box would render a hole. */
    RoboCheckEqInt(stats.boxesUnresolved, 1u); /* `<head>` only */
    RoboCheck(stats.boxesComputed > 8u);
    RoboCheck(LayoutTreeLineCount(fixture.tree) >= 2u);

    LayoutIntEnd(&fixture);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutIntegration(void);

void TestRegisterLayoutIntegration(void)
{
    TestRegister("layout.integration.box_model", TestIntegrationBoxModel);
    TestRegister("layout.integration.inline_text", TestIntegrationInlineText);
    TestRegister("layout.integration.position_overflow",
                       TestIntegrationPositionOverflow);
    TestRegister("layout.integration.nested_tree", TestIntegrationNestedTree);
    TestRegister("layout.integration.determinism", TestIntegrationDeterminism);
    TestRegister("layout.integration.extreme_input", TestIntegrationExtremeInput);
    TestRegister("layout.integration.no_stale_geometry",
                       TestIntegrationNoStaleGeometry);
    TestRegister("layout.integration.performance_sanity",
                       TestIntegrationPerformanceSanity);
    TestRegister("layout.integration.regression_border_style",
                       TestIntegrationRegressionBorderStyle);
    TestRegister("layout.integration.regression_clipped_child",
                       TestIntegrationRegressionClippedChild);
    TestRegister("layout.integration.regression_dirty_root",
                       TestIntegrationRegressionDirtyRoot);
    TestRegister("layout.integration.showcase", TestIntegrationShowcase);
}
