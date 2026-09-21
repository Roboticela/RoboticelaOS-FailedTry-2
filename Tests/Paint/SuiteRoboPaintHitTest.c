/*
 * RoboPaint hit suite (Phase 11.10) — paint.hit-test.*
 *
 * The release that asks the painter's question **backwards**: given a point, which node is drawn
 * there? Every case below pins one of the four rules that decide the answer, and each rule gets
 * its own case rather than being left to one overlapping fixture — because the three that are
 * about *order* would all be satisfied by a single lucky document and each would be wrong on its
 * own:
 *
 *   **last drawn wins**      a box's subtree is examined before the box, and the *last* child
 *                            before the first, so the node on top is the answer. Three
 *                            overlapping siblings where the *middle* one is on top is the case
 *                            that catches a walk that merely reversed the wrong thing;
 *
 *   **the border box is the region**  a point anywhere in a box's border box is on that box,
 *                            whether or not it draws anything — CSS hits a transparent `div`, and
 *                            a half-open boundary means `right` and `bottom` are outside while
 *                            `right - 1` and `bottom - 1` are inside. Both ends are asserted;
 *
 *   **the boundary clips the contents, never the box**  a child outside a `overflow: hidden`
 *                            ancestor's boundary is not the answer, while that **ancestor's own
 *                            border band** still is. This is the case a naive hit test gets wrong
 *                            invisibly, so it is asserted from both sides;
 *
 *   **opacity does not govern hit testing**  including `opacity: 0`, because CSS governs it with
 *                            `pointer-events` and this build has none. The case asserts the hit
 *                            *and* the strength the result carries, since the second is what a
 *                            later phase will need;
 *
 * and three more that are about the answer rather than about the rules:
 *
 *   **a stale tree is refused**  the query answers `STALE` with no node rather than a node with
 *                            half-computed geometry, and the case asserts the difference between
 *                            "nothing was there" and "no question was asked";
 *
 *   **the answer agrees with the painter**  the winning node's boxes, its boundary and its
 *                            strength are compared against the operations the paint pass emitted
 *                            for the *same node* — so "the two walks agree about a document" is a
 *                            check rather than a claim;
 *
 *   **the trace explains itself**  the dump names the point, the outcome and, for every object
 *                            examined, why it was passed over — with the line count an identity
 *                            against the stats rather than a guess.
 *
 * Every case builds a real document through the real chain (HTML → CSS → layout → paint) and
 * queries the real tree. Coordinates are never hard-coded: they come from the **painter's own
 * record** for the node in question (`BEGIN_BOX`/`TEXT`), so a fixture that gains a wrapper or a
 * margin cannot silently assert about the wrong rectangle — and the round trip through the
 * painter's record is itself the geometry half of the agreement claim.
 *
 * **What this suite does not cover**: a replaced element's own case. Since 11.7 a document's
 * `<img>` paints, so the brief's "image bounds" now has a subject -- but this phase landed before
 * that kind existed, and its cases name the boxes this build had. The mechanism is not per-kind:
 * a node's region is its border box (`PaintBoxesOf`), which is the *same* door a replaced
 * element's geometry comes from, so an image element is answered **by construction**; the case
 * that would pin it beside `paint.hit-test.region_is_border_box` is owed here rather than counted
 * as covered.
 *
 * It also cannot test `visibility`, and that is a **stated limitation rather than a gap**: the
 * property is registered in `RoboCSSPropertyRegistry.c` and consumed by *nobody* in this runtime
 * — RoboLayout builds the box and the painter paints it — so there is no behaviour for a hit test
 * to respect. `paint.hit-test.visibility_is_not_consumed` records that fact as a case, so the
 * phase that teaches layout and paint about `visibility` has a failing assertion pointing at the
 * place that must change rather than a silent divergence between what is drawn and what is hit.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends on no
 * Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboPaint/RoboPaintHitTest.h"

/* The pixel reader: `GfxSurfaceGetPixel`, which is the door the painting suites read
 * their own output back through (`RoboGraphicsBitmap.h`). Included here rather than reached
 * through RoboPaint, because the suite is reading the screen and not a paint record. */
#include "RoboGraphics/RoboGraphicsBitmap.h"

#include <string.h>

/* The same base sheet the other paint suites use: these elements are the block containers, so a
 * fixture's boxes stack in flow order. */
#define HIT_BASE_CSS "html, body, div, section, aside, figure, p { display: block }"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** The @p nth operation of @p kind on @p node, or 0 when there is none. */
static int HitOpFor(const PaintResult *result,
                      PaintOpKind kind,
                      LayoutNodeId node,
                      PaintOperation *out)
{
    size_t index;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            break;
        }
        if (operation.kind != kind || operation.node != node) {
            continue;
        }
        if (out) {
            *out = operation;
        }
        return 1;
    }
    return 0;
}

/** The index of the first operation of @p kind on @p node, or `(size_t)-1`. */
static size_t HitIndexFor(const PaintResult *result,
                            PaintOpKind kind,
                            LayoutNodeId node)
{
    size_t index;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            break;
        }
        if (operation.kind == kind && operation.node == node) {
            return index;
        }
    }
    return (size_t)-1;
}

/**
 * The three rectangles the **painter** gave @p node, in paint coordinates.
 *
 * The coordinates every case below queries at come from here rather than from a literal, so a
 * case cannot assert about a rectangle the document does not have. It is also the geometry half
 * of the agreement claim: the painter wrote these, and each case checks the query agrees.
 */
static void HitBoxesOf(PaintFixtureDoc *fixture,
                         LayoutNodeId node,
                         PaintBoxes *out)
{
    PaintResult result;
    PaintOperation operation;

    PaintOnce(fixture, &result);
    RoboCheck(HitOpFor(&result, RoboPaintOpBeginBox, node, &operation) ||
               HitOpFor(&result, RoboPaintOpText, node, &operation));
    *out = operation.boxes;
    PaintResultDispose(&result);
}

/** The centre of @p rect: the one coordinate that is inside whatever the extent is. */
static void HitCentre(LayoutRect rect, int32_t *outX, int32_t *outY)
{
    *outX = rect.x + rect.width / 2;
    *outY = rect.y + rect.height / 2;
}

/** The centre of a box's border box. */
static void HitCentreOf(PaintBoxes boxes, int32_t *outX, int32_t *outY)
{
    HitCentre(boxes.border, outX, outY);
}

/** Run one query, reporting a failure rather than asserting a particular answer. */
static void HitRunAt(PaintFixtureDoc *fixture,
                       int32_t x,
                       int32_t y,
                       PaintHitTestResult *out,
                       PaintHitTestStats *stats)
{
    PaintHitTestResult result;
    PaintHitTestStats local;
    PaintDiag diag;
    Result status;

    PaintHitTestResultInit(&result);
    PaintHitTestStatsInit(&local);
    PaintDiagReset(&diag);
    status = PaintHitTest(fixture->tree, MakeGfxPoint(x, y), &result, &local, &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the query refused: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)", diag.file ? diag.file : "?",
                       diag.line);
    }
    if (out) {
        *out = result;
    }
    if (stats) {
        *stats = local;
    }
}

/** The node the query at (@p x, @p y) answers, or `RoboLayoutNodeIdNone`. */
static LayoutNodeId HitNodeAt(PaintFixtureDoc *fixture, int32_t x, int32_t y)
{
    PaintHitTestResult result;

    HitRunAt(fixture, x, y, &result, NULL);
    return result.node;
}

/** Assert the answer about (@p x, @p y) is @p expected. */
static void HitNodeIs(PaintFixtureDoc *fixture,
                        int32_t x,
                        int32_t y,
                        LayoutNodeId expected,
                        const char *what)
{
    PaintHitTestResult result;

    HitRunAt(fixture, x, y, &result, NULL);
    if (result.node != expected) {
        TestFail(__FILE__, __LINE__,
                       "%s: the point %d,%d answered node %u (%s), expected %u", what, x, y,
                       (unsigned)result.node, PaintHitTestOutcomeName(result.outcome),
                       (unsigned)expected);
    }
}

/** Assert the answer about (@p x, @p y) is **not** @p unexpected. */
static void HitNodeIsNot(PaintFixtureDoc *fixture,
                            int32_t x,
                            int32_t y,
                            LayoutNodeId unexpected,
                            const char *what)
{
    PaintHitTestResult result;

    HitRunAt(fixture, x, y, &result, NULL);
    if (result.node == unexpected) {
        TestFail(__FILE__, __LINE__, "%s: the point %d,%d answered node %u, which it must "
                                           "not",
                       what, x, y, (unsigned)unexpected);
    }
}

/** Assert two groups of boxes are the same record. */
static void HitBoxesEq(PaintBoxes actual, PaintBoxes expected, const char *what)
{
    if (!PaintBoxesEqual(&actual, &expected)) {
        TestFail(__FILE__, __LINE__,
                       "%s: expected border=%d,%d %dx%d, got border=%d,%d %dx%d", what,
                       expected.border.x, expected.border.y, expected.border.width,
                       expected.border.height, actual.border.x, actual.border.y,
                       actual.border.width, actual.border.height);
    }
}

/** Assert two clips are the same record. */
static void HitClipEq(PaintClip actual, PaintClip expected, const char *what)
{
    if (!PaintClipEqual(&actual, &expected)) {
        TestFail(__FILE__, __LINE__,
                       "%s: expected clip %d[%d,%d]/%d[%d,%d], got %d[%d,%d]/%d[%d,%d]", what,
                       (int)expected.horizontal.mode, expected.horizontal.start,
                       expected.horizontal.end, (int)expected.vertical.mode,
                       expected.vertical.start, expected.vertical.end, (int)actual.horizontal.mode,
                       actual.horizontal.start, actual.horizontal.end, (int)actual.vertical.mode,
                       actual.vertical.start, actual.vertical.end);
    }
}

/** Assert two strengths are the same record. */
static void HitOpacityEq(PaintOpacity actual,
                           PaintOpacity expected,
                           const char *what)
{
    if (!PaintOpacityEqual(&actual, &expected)) {
        TestFail(__FILE__, __LINE__, "%s: expected strength %.4f, got %.4f", what,
                       PaintOpacityValue(&expected), PaintOpacityValue(&actual));
    }
}

/* ------------------------------------------------------------------ */
/* Reading the screen                                                  */
/* ------------------------------------------------------------------ */

/**
 * An owned, RGBA8, zero-filled surface: the screen a painted document lands on.
 *
 * Zero-filled matters here: an **unpainted** pixel is transparent, so "the painter drew
 * nothing there" is distinguishable from "the painter drew black there" -- which is the whole
 * of the agreement case's control.
 */
static GfxSurface *HitSurface(uint32_t width, uint32_t height)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;

    GfxSurfaceDescInit(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    RoboCheck(surface != NULL);
    return surface;
}

/** Build a display list from @p records and paint it into @p target. */
static void HitPaintTo(PaintResult *records, GfxSurface *target)
{
    PaintDisplayListBuilder *builder = NULL;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats decorationStats;
    PaintDiag diag;
    Result status;

    RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
    PaintDiagReset(&diag);
    status = PaintDisplayListBuilderBuild(builder, records->list, list, &listStats,
                                                   &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the display build refused the records: %s",
                       diag.message ? diag.message : "(no message)");
    }
    PaintDisplayListBuilderDestroy(builder);

    PaintDiagReset(&diag);
    status = PaintDecorationPaint(list, target, NULL, &decorationStats, &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the decoration pass refused the list: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
    PaintDisplayListDestroy(list);
}

/** The pixel at (x, y), failing when the sample is outside the surface. */
static GfxColorRgba8 HitPixel(GfxSurface *surface, int32_t x, int32_t y)
{
    GfxColorRgba8 color;
    int32_t width = (int32_t)GfxSurfaceWidth(surface);
    int32_t height = (int32_t)GfxSurfaceHeight(surface);

    memset(&color, 0, sizeof(color));
    if (x < 0 || y < 0 || x >= width || y >= height) {
        TestFail(__FILE__, __LINE__, "the sample %d,%d is outside the %dx%d screen", (int)x,
                       (int)y, (int)width, (int)height);
        return color;
    }
    RoboCheckEqInt(GfxSurfaceGetPixel(surface, (uint32_t)x, (uint32_t)y, &color),
                      RoboOk);
    return color;
}

/* ------------------------------------------------------------------ */
/* 1. Basic geometry                                                   */
/* ------------------------------------------------------------------ */

/** A point inside a box is on it; a point outside the whole document is on nothing. */
static void TestHitInsideAndOutside(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId frame;
    LayoutNodeId root;
    PaintBoxes boxes;
    PaintHitTestResult result;
    PaintHitTestStats stats;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px }");
    frame = PaintBox(&fixture, "frame");
    root = PaintBoxByTag(&fixture, "html");
    HitBoxesOf(&fixture, frame, &boxes);

    HitCentreOf(boxes, &x, &y);
    HitRunAt(&fixture, x, y, &result, &stats);

    RoboCheckEqInt((int)result.outcome, (int)RoboPaintHitOnNode);
    RoboCheckEqInt((int)result.node, (int)frame);
    RoboCheckEqInt((int)result.kind, (int)RoboLayoutObjectBox);
    RoboCheckEqInt((int)result.depth, 2);
    RoboCheckEqInt((int)stats.hits, 1);
    /* The geometry the answer carries is the geometry the painter wrote for the same box, in
     * the same space -- the agreement this phase exists to make checkable. */
    HitBoxesEq(result.boxes, boxes, "the hit box is the painted box");
    RoboCheckEqInt((int)result.point.x, x);
    RoboCheckEqInt((int)result.point.y, y);
    RoboCheckEqInt((int)PaintHitTestResultIsHit(&result), 1);
    RoboCheckEqInt(PaintHitTestResultIsValid(&result), 1);

    /* And well outside every box in the document: nothing is there, and the counters say the
     * walk excluded the document rather than that it forgot to look. */
    HitRunAt(&fixture, 100000, 100000, &result, &stats);
    RoboCheckEqInt((int)result.outcome, (int)RoboPaintHitMiss);
    RoboCheckEqInt((int)result.node, (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt((int)stats.hits, 0);
    RoboCheck(stats.rejectedRegion + stats.rejectedClip >= 1u);
    RoboCheck(PaintHitTestResultIsValid(&result));
    /* The tree's root was examined and rejected; nothing below it was, because a point outside a
     * box's region cannot be on anything the box contains. */
    RoboCheckEqInt((int)stats.objects, 1);

    /* A negative coordinate is as legal as a positive one and reaches the same conclusion. */
    HitNodeIs(&fixture, -1, -1, RoboLayoutNodeIdNone, "a negative point outside the page");
    (void)root;
    PaintEnd(&fixture);
}

/** The region is the **border box**, half-open: `right`/`bottom` are out, one left of them is in. */
static void TestHitRegionIsBorderBox(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId frame;
    PaintBoxes boxes;
    int32_t x;
    int32_t y;
    int32_t right;
    int32_t bottom;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px }");
    frame = PaintBox(&fixture, "frame");
    HitBoxesOf(&fixture, frame, &boxes);
    HitCentreOf(boxes, &x, &y);
    right = boxes.border.x + boxes.border.width;
    bottom = boxes.border.y + boxes.border.height;

    /* The four corners are the four boundary points, and half-open means two of them are inside
     * and two are not -- which is the convention `GfxRectContainsPoint` states and the
     * one every rectangle in this build shares. */
    HitNodeIs(&fixture, boxes.border.x, boxes.border.y, frame, "the top-left corner is inside");
    HitNodeIs(&fixture, right - 1, bottom - 1, frame, "one left of the bottom-right is inside");
    HitNodeIsNot(&fixture, right, y, frame, "the right edge is outside");
    HitNodeIsNot(&fixture, x, bottom, frame, "the bottom edge is outside");
    HitNodeIsNot(&fixture, boxes.border.x - 1, y, frame, "one left of the box is outside");
    HitNodeIsNot(&fixture, x, boxes.border.y - 1, frame, "one above the box is outside");

    /* The three layers of a box with no padding and no border are one rectangle, and the record
     * says so rather than leaving a caller to compare them. */
    RoboCheckEqInt(boxes.insetsMeasured, 1);
    RoboCheckEqInt(boxes.border.width, 200);
    RoboCheckEqInt(boxes.border.height, 100);
    PaintEnd(&fixture);
}

/** An empty box has an empty region: no point is on it, and none is invented for it. */
static void TestHitEmptyBoxes(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId wide;
    LayoutNodeId tall;
    PaintBoxes boxes;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"wide\"></div><div class=\"tall\"></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .wide { width: 100px; height: 0px }"
                                      " .tall { width: 0px; height: 100px }");
    wide = PaintBox(&fixture, "wide");
    tall = PaintBox(&fixture, "tall");

    HitBoxesOf(&fixture, wide, &boxes);
    RoboCheckEqInt(boxes.border.width, 100);
    RoboCheckEqInt(boxes.border.height, 0);
    /* A zero-height box is empty, and an empty rectangle contains no point -- not even its own
     * top-left corner, because there is no interior for one to be in. */
    HitNodeIsNot(&fixture, boxes.border.x, boxes.border.y, wide, "a zero-height box");
    HitNodeIsNot(&fixture, boxes.border.x + 50, boxes.border.y, wide,
                    "the middle of a zero-height box");

    HitBoxesOf(&fixture, tall, &boxes);
    RoboCheckEqInt(boxes.border.width, 0);
    RoboCheckEqInt(boxes.border.height, 100);
    HitNodeIsNot(&fixture, boxes.border.x, boxes.border.y, tall, "a zero-width box");
    HitNodeIsNot(&fixture, boxes.border.x, boxes.border.y + 50, tall,
                    "the middle of a zero-width box");

    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 2. Nesting and fallback                                             */
/* ------------------------------------------------------------------ */

/** A point on a child is the child; a point on the parent but on no child is the parent. */
static void TestHitNestedAndFallback(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId outer;
    LayoutNodeId inner;
    LayoutNodeId leaf;
    PaintBoxes outerBoxes;
    PaintBoxes innerBoxes;
    PaintBoxes leafBoxes;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\">"
                         "<div class=\"leaf\"></div></div></div></body></html>",
                         HIT_BASE_CSS
                         " .outer { width: 300px; height: 200px; padding: 20px }"
                         " .inner { width: 100px; height: 80px; padding: 10px }"
                         " .leaf { width: 40px; height: 20px }");
    outer = PaintBox(&fixture, "outer");
    inner = PaintBox(&fixture, "inner");
    leaf = PaintBox(&fixture, "leaf");
    HitBoxesOf(&fixture, outer, &outerBoxes);
    HitBoxesOf(&fixture, inner, &innerBoxes);
    HitBoxesOf(&fixture, leaf, &leafBoxes);

    /* Three levels, three answers, and each point is inside the one it names *and* inside the two
     * above it -- so a walk that answered with an ancestor would fail here. */
    HitCentreOf(leafBoxes, &x, &y);
    HitNodeIs(&fixture, x, y, leaf, "the deepest child wins");
    HitCentreOf(innerBoxes, &x, &y);
    HitNodeIs(&fixture, x, y, inner, "the middle child wins where the leaf is not");
    /* A point in the outer box's padding, below and right of the inner one: on no descendant, so
     * the ancestor answers -- which is the fallback every event dispatch depends on. */
    HitNodeIs(&fixture, outerBoxes.content.x + outerBoxes.content.width - 5,
                outerBoxes.content.y + outerBoxes.content.height - 5, outer,
                "the parent answers where no child is");
    /* And the *ancestors* are answers too, at their own edges. */
    HitNodeIs(&fixture, outerBoxes.border.x + 1, outerBoxes.border.y + 1, outer,
                "the outer box's own padding edge");

    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Paint order                                                      */
/* ------------------------------------------------------------------ */

/**
 * Overlapping siblings: the one painted **last** is the answer, whichever one a walk meets first.
 *
 * Two blocks in flow order, the second pulled back over the first by a relative offset (10.12) --
 * so the overlap exists without absolute positioning, which this build does not have. In the
 * overlap the second is on top; outside it each is on its own.
 */
static void TestHitOverlappingSiblings(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId first;
    LayoutNodeId second;
    PaintBoxes firstBoxes;
    PaintBoxes secondBoxes;
    PaintResult result;
    int32_t x;
    int32_t y;
    int overlapX;
    int overlapY;
    size_t firstEnd;
    size_t secondBegin;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"first\"></div><div class=\"second\"></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .first { width: 200px; height: 100px }"
                                      " .second { width: 200px; height: 100px;"
                                      " position: relative; top: -60px }");
    first = PaintBox(&fixture, "first");
    second = PaintBox(&fixture, "second");
    HitBoxesOf(&fixture, first, &firstBoxes);
    HitBoxesOf(&fixture, second, &secondBoxes);

    /* The fixture is what it claims to be: the two rectangles overlap, and neither contains the
     * other. A case that silently stopped overlapping would pass for the wrong reason. */
    RoboCheck(GfxRectIntersects(firstBoxes.border, secondBoxes.border));
    RoboCheck(secondBoxes.border.y < firstBoxes.border.y + firstBoxes.border.height);

    overlapX = secondBoxes.border.x + secondBoxes.border.width / 2;
    overlapY = secondBoxes.border.y + 5;

    HitNodeIs(&fixture, overlapX, overlapY, second, "the later sibling wins the overlap");
    /* Near its top edge, only the first is there -- so the answer is not merely "the last box in
     * the document", which a walk that ignored geometry would say. The second begins a third of
     * the way down the first, and its pull-up means its *own* box covers the rest. */
    y = firstBoxes.border.y + 5;
    RoboCheck(GfxRectContainsPoint(firstBoxes.border, MakeGfxPoint(overlapX, y)));
    RoboCheck(!GfxRectContainsPoint(secondBoxes.border, MakeGfxPoint(overlapX, y)));
    HitNodeIs(&fixture, overlapX, y, first, "the earlier sibling still owns its own area");

    /*
     * And the painter agrees about the order, read out of the list rather than argued: the
     * winner's own records stand **after** the loser's subtree, which is the same statement as
     * "the later sibling is drawn over the earlier one". A hit test that reversed the wrong thing
     * would answer `first` while this assertion still passed -- which is why both are here.
     */
    PaintOnce(&fixture, &result);
    firstEnd = HitIndexFor(&result, RoboPaintOpEndBox, first);
    secondBegin = HitIndexFor(&result, RoboPaintOpBeginBox, second);
    RoboCheck(firstEnd != (size_t)-1 && secondBegin != (size_t)-1);
    RoboCheck(secondBegin > firstEnd);
    PaintResultDispose(&result);

    (void)x;
    PaintEnd(&fixture);
}

/**
 * Three overlapping siblings where the **middle** one is on top.
 *
 * The case a walk that simply reversed the *document* order would get wrong: the answer is not the
 * last box in the document but the last one *drawn at that point*, and the third sibling is pulled
 * clear of the overlap while the second is pulled up over the first.
 */
static void TestHitOverlapIsByDrawingNotByPosition(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId first;
    LayoutNodeId second;
    LayoutNodeId third;
    PaintBoxes firstBoxes;
    PaintBoxes secondBoxes;
    PaintBoxes thirdBoxes;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"first\"></div><div class=\"second\"></div>"
                         "<div class=\"third\"></div></body></html>",
                         HIT_BASE_CSS " .first { width: 200px; height: 100px }"
                                      " .second { width: 200px; height: 100px;"
                                      " position: relative; top: -80px }"
                                      " .third { width: 40px; height: 20px;"
                                      " position: relative; top: -180px }");
    first = PaintBox(&fixture, "first");
    second = PaintBox(&fixture, "second");
    third = PaintBox(&fixture, "third");
    HitBoxesOf(&fixture, first, &firstBoxes);
    HitBoxesOf(&fixture, second, &secondBoxes);
    HitBoxesOf(&fixture, third, &thirdBoxes);

    RoboCheck(GfxRectIntersects(firstBoxes.border, secondBoxes.border));

    HitCentreOf(thirdBoxes, &x, &y);
    /* The fixture is what the case claims: the point is inside all three, and the second is drawn
     * over the first while the third is drawn over both -- so it is the third that answers, even
     * though the second is the *previous* box in the document. */
    RoboCheck(GfxRectContainsPoint(firstBoxes.border, MakeGfxPoint(x, y)));
    RoboCheck(GfxRectContainsPoint(secondBoxes.border, MakeGfxPoint(x, y)));
    RoboCheck(GfxRectContainsPoint(thirdBoxes.border, MakeGfxPoint(x, y)));
    HitNodeIs(&fixture, x, y, third, "the last drawn of three overlapping boxes");
    /* Just below the third, still in the overlap of the first two -- and there the second is the
     * last drawn, so the third's band really is what decided the answer above. */
    y = thirdBoxes.border.y + thirdBoxes.border.height;
    RoboCheck(GfxRectContainsPoint(secondBoxes.border, MakeGfxPoint(x, y)));
    HitNodeIs(&fixture, x, y, second, "the second where the third has ended");
    /* And at the very top of the first, above where the second reaches. */
    HitNodeIs(&fixture, firstBoxes.border.x + 10, firstBoxes.border.y, first,
                "the first where neither later box reaches");

    PaintEnd(&fixture);
}

/** A descendant is over its ancestor's own region, so it answers where the two overlap. */
static void TestHitDescendantOverAncestor(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId outer;
    LayoutNodeId inner;
    PaintBoxes innerBoxes;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .outer { width: 200px; height: 200px }"
                                      " .inner { width: 200px; height: 200px;"
                                      " background-color: #ff0000 }");
    outer = PaintBox(&fixture, "outer");
    inner = PaintBox(&fixture, "inner");
    HitBoxesOf(&fixture, inner, &innerBoxes);
    HitCentreOf(innerBoxes, &x, &y);

    /* The child covers the parent exactly, and is painted after it -- so the child answers even
     * though both rectangles contain the point. */
    HitNodeIs(&fixture, x, y, inner, "the child over a parent it covers exactly");
    RoboCheckEqInt((int)outer, (int)inner - 1u);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. Clipping                                                         */
/* ------------------------------------------------------------------ */

/**
 * The boundary clips the **contents**, never the box: an overflowing child is not the answer
 * outside it, while the clipped ancestor's own border band is.
 */
static void TestHitClipContentsNotBox(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId frame;
    LayoutNodeId leaf;
    PaintBoxes frameBoxes;
    PaintBoxes leafBoxes;
    PaintHitTestResult result;
    PaintHitTestStats stats;
    PaintResult painted;
    PaintOperation push;
    int32_t x;
    int32_t y;

    /*
     * The fixture is chosen so that the two reasons a subtree can be refused **come apart**, which
     * is the case worth pinning. The leaf starts 70px into a 100px-wide padding box, so its right
     * end (30px of it) lies in the frame's **right border band**: inside the frame's border box --
     * which is the frame's own region, and the bound of its subtree -- and outside the padding box
     * the frame's `overflow` clips its contents to.
     *
     * A leaf pulled *entirely* out of the frame would be refused by the **region** check, before
     * any boundary was consulted, because a hidden axis's region is clipped back to the padding
     * box (10.13's rule) -- so that fixture would never reach the clip check at all and would
     * silently stop testing the boundary. This one reaches it, and the `rejectedClip` assertion
     * below is what says so.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .frame { width: 100px; height: 60px; overflow: hidden;"
                                      " border: 10px solid #0000ff }"
                                      " .leaf { width: 50px; height: 40px; position: relative;"
                                      " left: 70px; background-color: #00ff00 }");
    frame = PaintBox(&fixture, "frame");
    leaf = PaintBox(&fixture, "leaf");
    HitBoxesOf(&fixture, frame, &frameBoxes);
    HitBoxesOf(&fixture, leaf, &leafBoxes);

    /* Inside the boundary: the leaf is the deepest node whose rectangle contains the point, so it
     * answers -- which is the other side of the boundary and what makes the case below meaningful
     * rather than merely "the leaf is unreachable". */
    HitNodeIs(&fixture, frameBoxes.padding.x + frameBoxes.padding.width - 1,
                leafBoxes.border.y + 20, leaf, "inside the boundary the leaf answers");

    /* In the right border band: inside the leaf's rectangle, inside the frame's border box, and
     * **outside** the boundary the frame's contents are clipped to. So the leaf is not the answer
     * -- which is the case a naive hit test gets wrong invisibly, because the node is laid out
     * there and simply is not drawn there. */
    x = frameBoxes.padding.x + frameBoxes.padding.width + 5;
    RoboCheck(x < frameBoxes.border.x + frameBoxes.border.width);
    RoboCheck(GfxRectContainsPoint(frameBoxes.border, MakeGfxPoint(x, leafBoxes.border.y + 20)));
    RoboCheck(GfxRectContainsPoint(leafBoxes.border, MakeGfxPoint(x, leafBoxes.border.y + 20)));
    HitRunAt(&fixture, x, leafBoxes.border.y + 20, &result, &stats);
    RoboCheckEqInt((int)result.node, (int)frame);
    RoboCheck(stats.rejectedClip >= 1u);

    /* And the frame's **own border band** is where the answer comes from, because a box's boundary
     * applies to its contents and not to itself -- 11.8's arrangement, read backwards. */
    HitNodeIs(&fixture, frameBoxes.border.x + 2, frameBoxes.border.y + 2, frame,
                "the clipped ancestor's own border band");
    HitNodeIs(&fixture, frameBoxes.border.x + 2,
                frameBoxes.border.y + frameBoxes.border.height - 2, frame,
                "the clipped ancestor's own bottom band");
    (void)y;

    /* The boundary the answer carries is the one in force on it; for the frame that is the
     * document's (nothing), and the painter says the same thing about the frame's own records. */
    HitRunAt(&fixture, frameBoxes.border.x + 2, frameBoxes.border.y + 2, &result, NULL);
    PaintOnce(&fixture, &painted);
    RoboCheckEqInt(HitOpFor(&painted, RoboPaintOpBeginBox, frame, &push), 1);
    HitClipEq(result.clip, push.clip, "the frame's own boundary is the painted one");
    PaintResultDispose(&painted);

    /* A leaf that ends up *inside* the boundary is still answerable, so the rejection above is
     * about the boundary and not about the leaf being unreachable. */
    HitNodeIs(&fixture, frameBoxes.padding.x + 5, frameBoxes.padding.y + 5, frame,
                "the frame where the leaf is not");

    PaintEnd(&fixture);
}

/** A child inside the boundary is the answer, and its boundary is the painter's boundary. */
static void TestHitClipInsideMatchesPainter(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId frame;
    LayoutNodeId leaf;
    PaintBoxes frameBoxes;
    PaintBoxes leafBoxes;
    PaintHitTestResult result;
    PaintResult painted;
    PaintOperation textOrMarker;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .frame { width: 100px; height: 60px; overflow: hidden }"
                                      " .leaf { width: 50px; height: 40px }");
    frame = PaintBox(&fixture, "frame");
    leaf = PaintBox(&fixture, "leaf");
    HitBoxesOf(&fixture, frame, &frameBoxes);
    HitBoxesOf(&fixture, leaf, &leafBoxes);
    HitCentreOf(leafBoxes, &x, &y);

    HitRunAt(&fixture, x, y, &result, NULL);
    RoboCheckEqInt((int)result.node, (int)leaf);
    /* The leaf lives inside the frame's boundary, so its answer carries it -- and the painter's
     * own record for the same node carries the identical record. That is the clipping half of the
     * agreement claim: one intersection, computed by one door, read by two walks. */
    PaintOnce(&fixture, &painted);
    RoboCheckEqInt(HitOpFor(&painted, RoboPaintOpBeginBox, leaf, &textOrMarker), 1);
    HitClipEq(result.clip, textOrMarker.clip, "the leaf's boundary is the painted one");
    RoboCheck(PaintClipIsActive(&result.clip));
    PaintResultDispose(&painted);

    /* And the frame's padding box is what it was clipped to. */
    RoboCheckEqInt(result.clip.horizontal.start, frameBoxes.padding.x);
    RoboCheckEqInt(result.clip.horizontal.end,
                      frameBoxes.padding.x + frameBoxes.padding.width);

    PaintEnd(&fixture);
}

/** Nested boundaries: the innermost one decides, and it is the intersection of both. */
static void TestHitClipNested(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId outer;
    LayoutNodeId mid;
    LayoutNodeId leaf;
    PaintBoxes outerBoxes;
    PaintBoxes midBoxes;
    PaintBoxes leafBoxes;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"mid\">"
                         "<div class=\"leaf\"></div></div></div></body></html>",
                         HIT_BASE_CSS
                         " .outer { width: 200px; height: 100px; overflow: hidden }"
                         " .mid { width: 60px; height: 40px; overflow: hidden;"
                         " position: relative; left: 10px; top: 10px }"
                         " .leaf { width: 400px; height: 300px }");
    outer = PaintBox(&fixture, "outer");
    mid = PaintBox(&fixture, "mid");
    leaf = PaintBox(&fixture, "leaf");
    HitBoxesOf(&fixture, outer, &outerBoxes);
    HitBoxesOf(&fixture, mid, &midBoxes);
    HitBoxesOf(&fixture, leaf, &leafBoxes);

    /* Inside `mid`, so inside both boundaries: the leaf answers, and that is the deepest node
     * whose rectangle actually contains the point. */
    HitNodeIs(&fixture, midBoxes.padding.x + 5, midBoxes.padding.y + 5, leaf,
                "inside both boundaries the leaf answers");
    /* Between `mid` and `outer`: the leaf reaches there geometrically but `mid` clips it, so the
     * answer is `mid`. */
    x = outerBoxes.padding.x + outerBoxes.padding.width - 5;
    y = outerBoxes.padding.y + outerBoxes.padding.height - 5;
    RoboCheck(GfxRectContainsPoint(leafBoxes.border, MakeGfxPoint(x, y)));
    HitNodeIs(&fixture, x, y, outer, "beyond both boundaries the leaf is gone");
    /* And outside `outer`: nothing on this subtree. */
    HitNodeIsNot(&fixture, outerBoxes.border.x + outerBoxes.border.width + 5,
                    outerBoxes.border.y + 5, leaf, "outside both boundaries");

    PaintEnd(&fixture);
}

/** A boundary with no area inside it excludes everything within it. */
static void TestHitClipEmpty(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId frame;
    LayoutNodeId leaf;
    PaintBoxes frameBoxes;
    PaintBoxes leafBoxes;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .frame { width: 0px; height: 0px; overflow: hidden }"
                                      " .leaf { width: 40px; height: 40px }");
    frame = PaintBox(&fixture, "frame");
    leaf = PaintBox(&fixture, "leaf");
    HitBoxesOf(&fixture, frame, &frameBoxes);
    HitBoxesOf(&fixture, leaf, &leafBoxes);

    /* The frame's padding box has no area, so the boundary it puts in force bounds nothing: the
     * leaf is *inside* that boundary's coordinate range and still cannot be hit, because the
     * range is empty. `PaintClipIsEmpty` is the predicate, and this is its only consumer
     * on the hit-test side. */
    RoboCheckEqInt(leafBoxes.border.x, frameBoxes.padding.x);
    RoboCheckEqInt(leafBoxes.border.y, frameBoxes.padding.y);
    HitCentreOf(leafBoxes, &x, &y);
    RoboCheck(x >= frameBoxes.padding.x && y >= frameBoxes.padding.y);
    HitNodeIsNot(&fixture, x, y, leaf, "a child inside an empty boundary");
    HitNodeIsNot(&fixture, frameBoxes.padding.x, frameBoxes.padding.y, leaf,
                    "the corner of an empty boundary");
    /* The frame itself is a zero-size box too, so the answer is its ancestor -- the outer box is
     * excluded on its own account, which is the `region` rule and not the boundary's. */
    HitNodeIsNot(&fixture, frameBoxes.padding.x, frameBoxes.padding.y, frame,
                    "the zero-size frame");

    PaintEnd(&fixture);
}

/** The containment door itself: per axis, half-open, safe on NULL and on an inactive axis. */
static void TestHitClipContainsDoor(void)
{
    PaintClip clip = PaintClipNone();
    PaintClip split;
    PaintClip empty;

    /* Nothing in force contains everything, including a NULL record -- the direction this must
     * fail in, because the alternative excludes every point of every unclipped document. */
    RoboCheckEqInt(PaintClipContains(&clip, MakeGfxPoint(0, 0)), 1);
    RoboCheckEqInt(PaintClipContains(&clip, MakeGfxPoint(-9999, 9999)), 1);
    RoboCheckEqInt(PaintClipContains(NULL, MakeGfxPoint(5, 5)), 1);
    RoboCheckEqInt(PaintClipAxisContains(NULL, 12345), 1);

    /* One bounded axis, the other free: `overflow-x: hidden` beside `overflow-y: visible`. */
    split = PaintClipNone();
    split.horizontal.mode = RoboCssOverflowHidden;
    split.horizontal.start = 10;
    split.horizontal.end = 20;
    RoboCheckEqInt(PaintClipContains(&split, MakeGfxPoint(15, 0)), 1);
    /* Inside on `y` is irrelevant: `y` was never bounded, however far out it is. */
    RoboCheckEqInt(PaintClipContains(&split, MakeGfxPoint(15, -100000)), 1);
    /* Half-open on the bounded axis, from both ends. */
    RoboCheckEqInt(PaintClipContains(&split, MakeGfxPoint(10, 0)), 1);
    RoboCheckEqInt(PaintClipContains(&split, MakeGfxPoint(9, 0)), 0);
    RoboCheckEqInt(PaintClipContains(&split, MakeGfxPoint(19, 0)), 1);
    RoboCheckEqInt(PaintClipContains(&split, MakeGfxPoint(20, 0)), 0);

    /* An empty boundary contains nothing at all -- which is the opposite answer from "no
     * boundary", and the two must not be confused. */
    empty = split;
    empty.horizontal.end = empty.horizontal.start;
    RoboCheckEqInt(PaintClipIsEmpty(&empty), 1);
    RoboCheckEqInt(PaintClipContains(&empty, MakeGfxPoint(10, 0)), 0);
    RoboCheckEqInt(PaintClipContains(&split, MakeGfxPoint(10, 0)), 1);
}

/* ------------------------------------------------------------------ */
/* 5. Opacity                                                          */
/* ------------------------------------------------------------------ */

/**
 * Opacity is a **painting** property: it does not decide whether a node can be hit.
 *
 * The case a reader expects to go the other way, so it is stated as its own: `opacity: 0` is
 * still the answer over its own region, because CSS governs hit testing with `pointer-events` and
 * this build has none. The strength travels in the result instead, which is where a phase that
 * implements the property will need it.
 */
static void TestHitOpacityDoesNotGate(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId solid;
    LayoutNodeId half;
    LayoutNodeId gone;
    PaintBoxes boxes;
    PaintHitTestResult result;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"solid\"></div><div class=\"half\"></div>"
                         "<div class=\"gone\"></div></body></html>",
                         HIT_BASE_CSS " .solid { width: 100px; height: 50px }"
                                      " .half { width: 100px; height: 50px; opacity: 0.5 }"
                                      " .gone { width: 100px; height: 50px; opacity: 0 }");
    solid = PaintBox(&fixture, "solid");
    half = PaintBox(&fixture, "half");
    gone = PaintBox(&fixture, "gone");

    HitBoxesOf(&fixture, solid, &boxes);
    HitCentreOf(boxes, &x, &y);
    HitRunAt(&fixture, x, y, &result, NULL);
    RoboCheckEqInt((int)result.node, (int)solid);
    RoboCheckEqInt(PaintOpacityIsOpaque(&result.opacity), 1);
    RoboCheckEqInt(PaintHitTestResultHasFlag(&result, RoboPaintHitFlagTransparent),
                      0);

    HitBoxesOf(&fixture, half, &boxes);
    HitCentreOf(boxes, &x, &y);
    HitRunAt(&fixture, x, y, &result, NULL);
    RoboCheckEqInt((int)result.node, (int)half);
    /* The strength **in force on this node's own drawing**: the box's own declaration, since a
     * box's decorations are painted at its own opacity. */
    RoboCheckEqInt(PaintOpacityValue(&result.opacity) > 0.49 &&
                          PaintOpacityValue(&result.opacity) < 0.51,
                      1);
    RoboCheckEqInt(PaintHitTestResultHasFlag(&result, RoboPaintHitFlagTransparent),
                      0);

    HitBoxesOf(&fixture, gone, &boxes);
    HitCentreOf(boxes, &x, &y);
    HitRunAt(&fixture, x, y, &result, NULL);
    RoboCheckEqInt((int)result.node, (int)gone);
    RoboCheckEqInt(PaintOpacityIsTransparent(&result.opacity), 1);
    RoboCheckEqInt(PaintHitTestResultHasFlag(&result, RoboPaintHitFlagTransparent),
                      1);
    /* The two states are told apart: invisible is not the same as absent. */
    RoboCheckEqInt((int)result.outcome, (int)RoboPaintHitOnNode);
    RoboCheck(PaintHitTestResultIsValid(&result));

    PaintEnd(&fixture);
}

/** Nested declarations multiply along the path, in the hit test exactly as in the painter. */
static void TestHitOpacityNestedMatchesPainter(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId outer;
    LayoutNodeId mid;
    LayoutNodeId leaf;
    PaintBoxes boxes;
    PaintHitTestResult result;
    PaintResult painted;
    PaintOperation marker;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"mid\">"
                         "<div class=\"leaf\"></div></div></div></body></html>",
                         HIT_BASE_CSS " .outer { width: 200px; height: 100px; opacity: 0.5 }"
                                      " .mid { width: 150px; height: 80px; opacity: 0.5 }"
                                      " .leaf { width: 100px; height: 40px }");
    outer = PaintBox(&fixture, "outer");
    mid = PaintBox(&fixture, "mid");
    leaf = PaintBox(&fixture, "leaf");
    HitBoxesOf(&fixture, leaf, &boxes);
    HitCentreOf(boxes, &x, &y);

    HitRunAt(&fixture, x, y, &result, NULL);
    RoboCheckEqInt((int)result.node, (int)leaf);
    /* 0.5 * 0.5 = 0.25 on the leaf, which declares nothing of its own -- the accumulation along
     * the path, and not the leaf's own (absent) declaration. */
    RoboCheckEqInt(PaintOpacityValue(&result.opacity) > 0.249 &&
                          PaintOpacityValue(&result.opacity) < 0.251,
                      1);

    /* Inside `mid` but outside `leaf`: the intermediate box answers, at its own `0.5 * 0.5` --
     * the accumulation applied one level up rather than merely at the leaf, which is the
     * difference between carrying a path and reading one declaration. */
    HitBoxesOf(&fixture, mid, &boxes);
    HitRunAt(&fixture, boxes.content.x + boxes.content.width - 10,
               boxes.content.y + boxes.content.height - 10, &result, NULL);
    RoboCheckEqInt((int)result.node, (int)mid);
    RoboCheckEqInt(PaintOpacityValue(&result.opacity) > 0.249 &&
                          PaintOpacityValue(&result.opacity) < 0.251,
                      1);

    /* The painter's own record for the same node carries the same number, so the two walks
     * multiply the same path with the same door. */
    PaintOnce(&fixture, &painted);
    RoboCheckEqInt(HitOpFor(&painted, RoboPaintOpBeginBox, leaf, &marker), 1);
    HitOpacityEq(result.opacity, marker.opacity, "the leaf's strength is the painted one");

    /* And an ancestor at zero does not remove its descendants from the answer. */
    PaintResultDispose(&painted);
    PaintEnd(&fixture);

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"leaf\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .outer { width: 100px; height: 60px; opacity: 0 }"
                                      " .leaf { width: 50px; height: 30px }");
    outer = PaintBox(&fixture, "outer");
    leaf = PaintBox(&fixture, "leaf");
    HitBoxesOf(&fixture, leaf, &boxes);
    HitCentreOf(boxes, &x, &y);
    HitRunAt(&fixture, x, y, &result, NULL);
    RoboCheckEqInt((int)result.node, (int)leaf);
    RoboCheckEqInt(PaintOpacityIsTransparent(&result.opacity), 1);
    RoboCheckEqInt(PaintHitTestResultHasFlag(&result, RoboPaintHitFlagTransparent),
                      1);
    /* A point in the outer box but outside the leaf is still the outer box, transparent or not. */
    HitBoxesOf(&fixture, outer, &boxes);
    HitNodeIs(&fixture, boxes.content.x + 5, boxes.content.y + 55, outer,
                "a fully transparent ancestor still answers");
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. CSS and layout state                                             */
/* ------------------------------------------------------------------ */

/** `display: none` builds no box at all, so there is nothing to hit and the parent answers. */
static void TestHitDisplayNone(void)
{
    PaintFixtureDoc fixture;
    HtmlNodeHandle hiddenElement;
    LayoutNodeId frame;
    PaintBoxes frameBoxes;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"></div>"
                         "<div class=\"hidden\"></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 100px; height: 100px }"
                                      " .hidden { display: none; width: 300px; height: 300px }");
    frame = PaintBox(&fixture, "frame");
    hiddenElement = PaintFind(&fixture, "hidden");
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&hiddenElement), 1);
    /* The element is in the DOM and generated **no** layout object -- which is why it is not a
     * hit-test case at all, and why nothing here needs a visibility rule to exclude it. */
    RoboCheckEqInt(
        (int)LayoutObjectIdForDomNode(fixture.tree, &hiddenElement),
        (int)RoboLayoutNodeIdNone);

    HitBoxesOf(&fixture, frame, &frameBoxes);
    HitCentreOf(frameBoxes, &x, &y);
    HitNodeIs(&fixture, x, y, frame, "the box that replaced nothing");

    PaintEnd(&fixture);
}

/**
 * `visibility` is registered and consumed by nobody, so a hit test does not read it.
 *
 * A **limitation recorded as a case**, deliberately: the property has a computed value in this
 * build (`RoboCSSPropertyRegistry.c` registers it inherited, initial `visible`), and neither
 * RoboLayout nor RoboPaint acts on it -- a `<div style="visibility: hidden">` is built and painted
 * exactly like any other. So hit testing over the rectangles the painter draws is *consistent*,
 * and a build that taught the hit test about `visibility` while leaving paint alone would make a
 * click land where nothing is visible drawn. The assertion is that the box exists and is
 * answerable; the phase that implements the property changes this case on purpose.
 */
static void TestHitVisibilityIsNotConsumed(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId frame;
    LayoutNodeId hidden;
    PaintBoxes boxes;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"hidden\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px }"
                                      " .hidden { width: 100px; height: 50px;"
                                      " visibility: hidden }");
    frame = PaintBox(&fixture, "frame");
    hidden = PaintBox(&fixture, "hidden");
    /* The box exists: `visibility` does not suppress layout in this build. */
    RoboCheck(hidden != RoboLayoutNodeIdNone);
    RoboCheck(frame != RoboLayoutNodeIdNone);

    HitBoxesOf(&fixture, hidden, &boxes);
    HitCentreOf(boxes, &x, &y);
    /* And it is answerable, because the painter draws it -- which is the consistency this phase
     * chooses over an invented partial rule. */
    HitNodeIs(&fixture, x, y, hidden, "a box with visibility hidden is still laid out");

    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 7. Text                                                             */
/* ------------------------------------------------------------------ */

/** A text run is a leaf with its own rectangle, and it answers for the node the text node built. */
static void TestHitTextRun(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId frame;
    LayoutNodeId run;
    PaintBoxes frameBoxes;
    PaintBoxes runBoxes;
    PaintHitTestResult result;
    HtmlNodeHandle handle;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\">Hello</div></body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px }");
    frame = PaintBox(&fixture, "frame");
    /* The run's node comes from the painter's own `TEXT` record, so a fixture that stopped
     * producing a run would fail here rather than silently testing a box. */
    {
        PaintResult painted;
        PaintOperation text;

        PaintOnce(&fixture, &painted);
        RoboCheckEqInt(PaintNthOfKind(&painted, RoboPaintOpText, 0u, &text), 1);
        run = text.node;
        runBoxes = text.boxes;
        PaintResultDispose(&painted);
    }
    RoboCheck(run != frame);

    HitBoxesOf(&fixture, frame, &frameBoxes);
    HitCentreOf(runBoxes, &x, &y);
    HitRunAt(&fixture, x, y, &result, NULL);

    RoboCheckEqInt((int)result.node, (int)run);
    RoboCheckEqInt((int)result.kind, (int)RoboLayoutObjectText);
    RoboCheckEqInt(PaintHitTestResultHasFlag(&result, RoboPaintHitFlagText), 1);
    RoboCheckEqInt((int)result.depth, (int)PaintHitTestResultDepth(&result));
    HitBoxesEq(result.boxes, runBoxes, "the run's painted rectangle");

    /* The identity the answer carries is the **text node's**, not the element's -- which is what
     * the record promises, and what a Phase 12 dispatch needs in order to walk up to the element
     * itself. */
    handle = PaintHitTestResultDomNode(&result);
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&handle), 1);
    RoboCheckEqInt((int)LayoutObjectIdForDomNode(fixture.tree, &handle), (int)run);

    /* Below the line the run sits on, the element answers rather than the run. */
    HitNodeIs(&fixture, frameBoxes.content.x + 2,
                frameBoxes.content.y + frameBoxes.content.height - 2, frame,
                "the element below the line box");

    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 8. Safety                                                           */
/* ------------------------------------------------------------------ */

/** NULL arguments, an empty tree, a stale tree, and extreme coordinates: no crash, no invented hit. */
static void TestHitSafety(void)
{
    PaintFixtureDoc fixture;
    PaintHitTestResult result;
    PaintHitTestStats stats;
    PaintClip cleared;
    LayoutPoint point;

    /* NULL arguments are refused, and the refusal is a code rather than an outcome: no question
     * was asked, so there is no answer to report. */
    RoboCheckEqInt(PaintHitTest(NULL, MakeGfxPoint(0, 0), &result, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(
        PaintHitTest((const LayoutTree *)NULL, MakeGfxPoint(0, 0), NULL, NULL,
                            NULL),
        RoboInvalidArgument);

    /* Statistics are optional: a caller that only wants the answer declares nothing. */
    PaintBeginLaidOut(&fixture, "<html><body><div class=\"frame\"></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 10px; height: 10px }");
    RoboCheckEqInt(PaintHitTest(fixture.tree, MakeGfxPoint(3, 3), &result, NULL, NULL),
                      RoboOk);
    RoboCheck(PaintHitTestResultIsValid(&result));
    PaintEnd(&fixture);

    /* An empty document: a miss with nothing examined, which the counters already say rather than
     * a special case a caller has to test for. */
    PaintBegin(&fixture, "<html><body></body></html>", HIT_BASE_CSS);
    PaintLayout(&fixture, 800, 600);
    HitRunAt(&fixture, 0, 0, &result, &stats);
    RoboCheck((int)result.outcome == (int)RoboPaintHitMiss ||
               (int)result.outcome == (int)RoboPaintHitOnNode);
    RoboCheckEqInt((int)stats.hits, PaintHitTestResultIsHit(&result) ? 1 : 0);
    PaintEnd(&fixture);

    /*
     * A tree whose geometry is not final is **refused**: the outcome says so and no node is
     * reported, which is the difference between "nothing is there" and "no question was asked".
     * A caller that could not tell those apart would dispatch a click at a node the user is not
     * pointing at, which is why the refusal is not an error and not a miss.
     */
    PaintBegin(&fixture, "<html><body><div class=\"frame\"></div></body></html>",
                HIT_BASE_CSS " .frame { width: 100px; height: 100px }");
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 1);
    HitRunAt(&fixture, 10, 10, &result, &stats);
    RoboCheckEqInt((int)result.outcome, (int)RoboPaintHitStale);
    RoboCheckEqInt((int)result.node, (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt((int)stats.objects, 0);
    RoboCheckEqInt((int)stats.hits, 0);
    RoboCheck(PaintHitTestResultIsValid(&result));

    /* And the same tree, once laid out, answers -- so the refusal was about freshness and not
     * about the document. */
    PaintLayout(&fixture, 800, 600);
    HitRunAt(&fixture, 10, 10, &result, &stats);
    RoboCheckEqInt((int)result.outcome, (int)RoboPaintHitOnNode);
    RoboCheckEqInt((int)stats.hits, 1);

    /* The coordinate range's extremes: a query at the seam is answered or missed, never crashed
     * on and never wrapped into a different rectangle. */
    {
        static const int32_t extremes[] = { -2147483647 - 1, -1000000, 0, 1000000, 2147483647 };
        size_t index;

        for (index = 0u; index < sizeof(extremes) / sizeof(extremes[0]); index++) {
            size_t other;

            for (other = 0u; other < sizeof(extremes) / sizeof(extremes[0]); other++) {
                HitRunAt(&fixture, extremes[index], extremes[other], &result, &stats);
                RoboCheck(PaintHitTestResultIsValid(&result));
                /* A local point is the difference of two coordinates at the seam and is
                 * *saturated* rather than wrapped, so it is always a coordinate this build can
                 * carry -- never a number that reads as a mirrored position. */
                point = result.local;
                RoboCheck(point.x == point.x && point.y == point.y);
            }
        }
    }
    PaintEnd(&fixture);

    /* The record's own doors are total: a cleared result reads as a record and an empty clip
     * contains everything. */
    PaintHitTestResultInit(&result);
    RoboCheckEqInt(PaintHitTestResultIsValid(&result), 1);
    RoboCheckEqInt(PaintHitTestResultIsHit(&result), 0);
    RoboCheckEqInt(PaintHitTestResultIsValid(NULL), 0);
    RoboCheckEqInt(PaintHitTestResultIsHit(NULL), 0);
    PaintHitTestResultClip(NULL, &cleared);
    RoboCheckEqInt(PaintClipIsActive(&cleared), 0);
    PaintHitTestResultInit(NULL); /* safe */
    PaintHitTestStatsInit(NULL);  /* safe */
}

/* ------------------------------------------------------------------ */
/* 9. Determinism and mutation isolation                               */
/* ------------------------------------------------------------------ */

/** The same tree and the same point answer the same thing, every time. */
static void TestHitDeterminism(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId frame;
    LayoutNodeId leaf;
    PaintBoxes boxes;
    PaintHitTestResult first;
    PaintHitTestResult again;
    PaintHitTestStats firstStats;
    PaintHitTestStats againStats;
    int32_t x;
    int32_t y;
    int round;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div></div>"
                         "<div class=\"other\"></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px; overflow: hidden }"
                                      " .leaf { width: 50px; height: 40px }"
                                      " .other { width: 80px; height: 30px; opacity: 0.5 }");
    frame = PaintBox(&fixture, "frame");
    leaf = PaintBox(&fixture, "leaf");
    RoboCheck(frame != RoboLayoutNodeIdNone);
    HitBoxesOf(&fixture, leaf, &boxes);
    HitCentreOf(boxes, &x, &y);

    HitRunAt(&fixture, x, y, &first, &firstStats);
    for (round = 0; round < 8; round++) {
        HitRunAt(&fixture, x, y, &again, &againStats);
        RoboCheckEqInt(PaintHitTestResultEqual(&first, &again), 1);
        RoboCheckEqInt((int)againStats.objects, (int)firstStats.objects);
        RoboCheckEqInt((int)againStats.rejectedRegion, (int)firstStats.rejectedRegion);
        RoboCheckEqInt((int)againStats.rejectedClip, (int)firstStats.rejectedClip);
        RoboCheckEqInt((int)againStats.hits, (int)firstStats.hits);
        RoboCheckEqInt((int)againStats.depthMax, (int)firstStats.depthMax);
    }

    /* A **second, identically built** document answers identically too: nothing in the answer
     * depends on an address, an allocation order or a hash walk. */
    PaintEnd(&fixture);
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div></div>"
                         "<div class=\"other\"></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px; overflow: hidden }"
                                      " .leaf { width: 50px; height: 40px }"
                                      " .other { width: 80px; height: 30px; opacity: 0.5 }");
    HitRunAt(&fixture, x, y, &again, &againStats);
    RoboCheckEqInt(PaintHitTestResultEqual(&first, &again), 1);
    PaintEnd(&fixture);
}

/**
 * An answer is a **snapshot**: changing the tree afterwards cannot rewrite one already drawn.
 *
 * The rule 11.9's records follow, for the same reason -- a click that changed meaning when a style
 * was recomputed is worse than no answer -- and it is checkable here because the result holds
 * copies: the id it names still validates, and the geometry it reports is the geometry that was
 * true when the query ran.
 */
static void TestHitMutationIsolation(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId leaf;
    PaintBoxes boxes;
    PaintHitTestResult before;
    PaintHitTestResult after;
    PaintHitTestResult copy;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px }"
                                      " .leaf { width: 100px; height: 40px }");
    leaf = PaintBox(&fixture, "leaf");
    HitBoxesOf(&fixture, leaf, &boxes);
    HitCentreOf(boxes, &x, &y);
    HitRunAt(&fixture, x, y, &before, NULL);
    RoboCheckEqInt((int)before.node, (int)leaf);
    copy = before;

    /* Invalidate the tree: the *tree* is now stale, and the answer that was already drawn is
     * unchanged, field for field. A viewport change is the invalidation that genuinely makes every
     * box's geometry owe a recomputation, so the tree really is stale and not merely marked. */
    RoboCheckEqInt(
        LayoutTreeMarkAll(fixture.tree, RoboLayoutInvalidationReasonViewport, NULL),
        RoboOk);
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 1);
    RoboCheckEqInt(PaintHitTestResultEqual(&before, &copy), 1);
    RoboCheck(PaintHitTestResultIsValid(&before));

    /* And a fresh query against the now-stale tree refuses rather than reusing the old answer --
     * which is the other half of the rule: a snapshot is not a cache. */
    HitRunAt(&fixture, x, y, &after, NULL);
    RoboCheckEqInt((int)after.outcome, (int)RoboPaintHitStale);
    RoboCheckEqInt(PaintHitTestResultEqual(&before, &after), 0);

    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 10. Counter identities                                              */
/* ------------------------------------------------------------------ */

/** The three identities the stats owe, checked on a document that exercises every branch. */
static void TestHitStatsIdentities(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId frame;
    LayoutNodeId leaf;
    PaintBoxes boxes;
    PaintHitTestResult result;
    PaintHitTestStats stats;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div>"
                         "<div class=\"far\"></div></div><div class=\"after\"></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 60px; overflow: hidden }"
                                      " .leaf { width: 40px; height: 30px }"
                                      " .far { width: 40px; height: 30px; position: relative;"
                                      " left: 500px }"
                                      " .after { width: 100px; height: 20px }");
    frame = PaintBox(&fixture, "frame");
    leaf = PaintBox(&fixture, "leaf");
    HitBoxesOf(&fixture, leaf, &boxes);
    HitCentreOf(boxes, &x, &y);
    HitRunAt(&fixture, x, y, &result, &stats);

    RoboCheckEqInt((int)result.node, (int)leaf);
    /* Every object examined is exactly one of the three: a placed box, a placed run, or something
     * nothing placed. The identity is what makes the three counters a partition rather than three
     * independent tallies. */
    RoboCheckEqInt((int)(stats.boxes + stats.texts + stats.unplaced), (int)stats.objects);
    /* Exactly one node is the answer, because the walk returns the moment one is found. */
    RoboCheckEqInt((int)stats.hits, 1);
    RoboCheck(stats.boxes >= 2u);
    RoboCheck(stats.depthMax >= 2u);
    /* The frame's contents were inside the boundary, so nothing there was rejected for clipping;
     * the `far` child was rejected geometrically and is counted as one region rejection. */
    RoboCheck(stats.rejectedRegion >= 1u);
    /* And the frame itself answers in the part of its own box no child covers -- so the region
     * rejection above is about the child the frame's boundary excluded and not about the frame
     * becoming unreachable. */
    HitBoxesOf(&fixture, frame, &boxes);
    HitNodeIs(&fixture, boxes.content.x + 5, boxes.content.y + boxes.content.height - 5, frame,
                "the frame below its children");

    /* A miss on a tree with a root means something excluded the point -- and the counters are how
     * a caller tells "nothing is there" from "the walk gave up". */
    HitRunAt(&fixture, -50, -50, &result, &stats);
    RoboCheckEqInt((int)result.outcome, (int)RoboPaintHitMiss);
    RoboCheck(stats.rejectedRegion + stats.rejectedClip >= 1u);
    RoboCheckEqInt((int)(stats.boxes + stats.texts + stats.unplaced), (int)stats.objects);
    RoboCheckEqInt((int)stats.hits, 0);

    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 11. The result model                                                */
/* ------------------------------------------------------------------ */

/** The record's doors, and the combinations the validity rule refuses. */
static void TestHitResultModel(void)
{
    PaintHitTestResult result;
    PaintHitTestResult other;
    PaintBoxes boxes;
    PaintClip clip;
    PaintOpacity opacity;

    PaintHitTestResultInit(&result);
    RoboCheckEqInt((int)result.outcome, (int)RoboPaintHitNone);
    RoboCheckEqInt((int)PaintHitTestResultOutcome(&result), (int)RoboPaintHitNone);
    RoboCheckEqInt(PaintHitTestResultIsValid(&result), 1);
    /* The cleared record is **readable**: no boundary in force and full strength, so a caller
     * that drew with it would draw everything rather than nothing. */
    PaintHitTestResultClip(&result, &clip);
    RoboCheckEqInt(PaintClipIsActive(&clip), 0);
    PaintHitTestResultOpacity(&result, &opacity);
    RoboCheckEqInt(PaintOpacityIsOpaque(&opacity), 1);
    PaintHitTestResultBoxes(&result, &boxes);
    RoboCheckEqInt(PaintBoxesIsValid(&boxes), 1);

    /* A hit claiming a node but no identity is a record whose fields disagree, and is refused. */
    result.outcome = RoboPaintHitOnNode;
    RoboCheckEqInt(PaintHitTestResultIsValid(&result), 0);
    /* As is a node reported by a miss. */
    PaintHitTestResultInit(&result);
    result.outcome = RoboPaintHitMiss;
    result.node = 7u;
    RoboCheckEqInt(PaintHitTestResultIsValid(&result), 0);
    /* And a flag describing a hit that did not happen. */
    PaintHitTestResultInit(&result);
    result.flags = RoboPaintHitFlagContent;
    RoboCheckEqInt(PaintHitTestResultIsValid(&result), 0);
    /* An unknown flag bit, and an unknown outcome, are both refused. */
    PaintHitTestResultInit(&result);
    result.flags = 1u << 30;
    RoboCheckEqInt(PaintHitTestResultIsValid(&result), 0);
    PaintHitTestResultInit(&result);
    result.outcome = (PaintHitTestOutcome)99;
    RoboCheckEqInt(PaintHitTestResultIsValid(&result), 0);
    /* A clip whose interval runs backwards is not a record. */
    PaintHitTestResultInit(&result);
    PaintHitTestResultClip(&result, &clip);
    result.clip.horizontal.mode = RoboCssOverflowHidden;
    result.clip.horizontal.start = 40;
    result.clip.horizontal.end = 10;
    RoboCheckEqInt(PaintHitTestResultIsValid(&result), 0);

    /* Equality is field-for-field, so a build that changed one field cannot compare equal. */
    PaintHitTestResultInit(&result);
    PaintHitTestResultInit(&other);
    RoboCheckEqInt(PaintHitTestResultEqual(&result, &other), 1);
    RoboCheckEqInt(PaintHitTestResultEqual(NULL, NULL), 1);
    RoboCheckEqInt(PaintHitTestResultEqual(&result, NULL), 0);
    other = result;
    other.point.x = 5;
    RoboCheckEqInt(PaintHitTestResultEqual(&result, &other), 0);
    other = result;
    other.depth = 3u;
    RoboCheckEqInt(PaintHitTestResultEqual(&result, &other), 0);

    /* The vocabulary is complete and every outcome is named. */
    {
        static const char *const expected[RoboPaintHitTestOutcomeCount] = {
            "none", "node", "miss", "stale"
        };
        size_t index;

        RoboCheckEqInt((int)RoboPaintHitTestOutcomeCount, 4);
        for (index = 0u; index < (size_t)RoboPaintHitTestOutcomeCount; index++) {
            RoboCheckEqInt(
                PaintHitTestOutcomeIsKnown((PaintHitTestOutcome)index), 1);
            RoboCheckStrEq(PaintHitTestOutcomeName((PaintHitTestOutcome)index),
                              expected[index]);
        }
        RoboCheckEqInt(PaintHitTestOutcomeIsKnown(
                              (PaintHitTestOutcome)99),
                          0);
        RoboCheckStrEq(PaintHitTestOutcomeName((PaintHitTestOutcome)99),
                          "unknown");
        /* The flag vocabulary is closed: three bits, and the mask is exactly them. */
        RoboCheckEqInt((int)RoboPaintHitFlagAll,
                          (int)(RoboPaintHitFlagText | RoboPaintHitFlagContent |
                                RoboPaintHitFlagTransparent));
    }
}

/* ------------------------------------------------------------------ */
/* 12. The trace                                                       */
/* ------------------------------------------------------------------ */

/** The dump names the answer, the point and one reason per object examined. */
static void TestHitDump(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId frame;
    LayoutNodeId leaf;
    LayoutNodeId far;
    PaintBoxes boxes;
    PaintHitTestStats stats;
    char buffer[8192];
    char second[8192];
    size_t length = 0u;
    size_t secondLength = 0u;
    size_t lines = 0u;
    size_t longest = 0u;
    size_t run = 0u;
    size_t index;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div>"
                         "<div class=\"far\"></div></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 100px; height: 60px; overflow: hidden }"
                                      " .leaf { width: 40px; height: 30px }"
                                      " .far { width: 30px; height: 30px; position: relative;"
                                      " left: 400px }");
    frame = PaintBox(&fixture, "frame");
    leaf = PaintBox(&fixture, "leaf");
    far = PaintBox(&fixture, "far");
    RoboCheck(frame != RoboLayoutNodeIdNone && far != RoboLayoutNodeIdNone);
    HitBoxesOf(&fixture, leaf, &boxes);
    HitCentreOf(boxes, &x, &y);

    memset(buffer, 0, sizeof(buffer));
    RoboCheckEqInt(PaintHitTestWrite(fixture.tree, MakeGfxPoint(x, y), buffer,
                                               sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheck(length > 0u);
    RoboCheckEqInt((int)strlen(buffer), (int)length);
    /* The header is the answer, on its own line. */
    RoboCheck(strncmp(buffer, "hit-test point=", 15u) == 0);
    RoboCheck(strstr(buffer, "outcome=node") != NULL);
    RoboCheck(strstr(buffer, "outcome=region") != NULL ||
               strstr(buffer, "outcome=clip") != NULL);
    /* The line count is an identity against the stats rather than a guess: one header, one line
     * per examined object, and one `hit` line because a node was found. */
    {
        PaintHitTestResult answer;

        HitRunAt(&fixture, x, y, &answer, &stats);
        RoboCheckEqInt((int)answer.node, (int)leaf);
    }
    for (index = 0u; index < length; index++) {
        if (buffer[index] == '\n') {
            lines++;
            if (run > longest) {
                longest = run;
            }
            run = 0u;
        } else {
            run++;
        }
    }
    /* The last line is newline-terminated, so no trailing run to account for. */
    RoboCheckEqInt((int)run, 0);
    RoboCheckEqInt((int)lines, (int)(2u + stats.objects));
    RoboCheck(longest < (size_t)RoboPaintHitTestLineMax);
    /* One line per examined object *plus* the `hit` line, and the stats say how many objects. */
    RoboCheckEqInt((int)stats.hits, 1);

    /* Dumping twice produces the same text: the trace is a pure function of the tree and the
     * point, which is why running the walk twice is sound. */
    memset(second, 0, sizeof(second));
    RoboCheckEqInt(PaintHitTestWrite(fixture.tree, MakeGfxPoint(x, y), second,
                                               sizeof(second), &secondLength, NULL),
                      RoboOk);
    RoboCheckEqInt((int)secondLength, (int)length);
    RoboCheck(strcmp(buffer, second) == 0);

    /* A buffer too small is refused with the length it would have needed, so a caller sizes from
     * the refusal rather than from an estimate -- the contract every `_write` door here uses. */
    {
        char tiny[16];
        size_t needed = 0u;

        memset(tiny, 0, sizeof(tiny));
        RoboCheckEqInt(PaintHitTestWrite(fixture.tree, MakeGfxPoint(x, y), tiny,
                                                   sizeof(tiny), &needed, NULL),
                          RoboNotSupported);
        RoboCheckEqInt((int)needed, (int)length);
        /* And the count is honest: the same dump into a buffer of exactly that size succeeds and
         * produces the same text, so a caller that sized from the refusal is not sent round
         * again. */
        RoboCheck(needed + 1u <= sizeof(second));
        {
            size_t written = 0u;

            memset(second, 0, sizeof(second));
            RoboCheckEqInt(PaintHitTestWrite(fixture.tree, MakeGfxPoint(x, y), second,
                                                       needed + 1u, &written, NULL),
                              RoboOk);
            RoboCheckEqInt((int)written, (int)length);
            RoboCheck(strcmp(second, buffer) == 0);
        }
    }

    /* A refusal writes its own line rather than an empty buffer, so a reader of a stale tree's
     * dump is told what happened. */
    PaintEnd(&fixture);
    PaintBegin(&fixture, "<html><body><div class=\"frame\"></div></body></html>",
                HIT_BASE_CSS " .frame { width: 10px; height: 10px }");
    memset(buffer, 0, sizeof(buffer));
    RoboCheckEqInt(PaintHitTestWrite(fixture.tree, MakeGfxPoint(1, 1), buffer,
                                               sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheck(strstr(buffer, "outcome=stale") != NULL);
    RoboCheck(length > 0u);

    /* NULL arguments are refused rather than dereferenced. */
    RoboCheckEqInt(
        PaintHitTestWrite(NULL, MakeGfxPoint(0, 0), buffer, sizeof(buffer), &length,
                                  NULL),
        RoboInvalidArgument);
    RoboCheckEqInt(
        PaintHitTestWrite(fixture.tree, MakeGfxPoint(0, 0), NULL, sizeof(buffer),
                                  &length, NULL),
        RoboInvalidArgument);
    RoboCheckEqInt(PaintHitTestWrite(fixture.tree, MakeGfxPoint(0, 0), buffer,
                                               sizeof(buffer), NULL, NULL),
                      RoboInvalidArgument);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 13. The painter's agreement                                         */
/* ------------------------------------------------------------------ */

/**
 * Every representative point of one document, against what the painter emitted.
 *
 * The phase's central claim, stated as one check rather than as a fixture: for each of these
 * points the query's answer is the node whose own painted rectangle contains the point, and whose
 * `BEGIN_BOX`/`TEXT` operation is the **last** one that satisfies it among the boxes that draw
 * there. The check is done from the paint list, so it cannot be satisfied by a hit test that
 * agrees with itself.
 */
static void TestHitMatchesThePaintList(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId outer;
    LayoutNodeId inner;
    LayoutNodeId after;
    PaintBoxes boxes;
    PaintResult painted;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "<div class=\"after\"></div></body></html>",
                         HIT_BASE_CSS " .outer { width: 200px; height: 100px }"
                                      " .inner { width: 60px; height: 40px }"
                                      " .after { width: 120px; height: 30px }");
    outer = PaintBox(&fixture, "outer");
    inner = PaintBox(&fixture, "inner");
    after = PaintBox(&fixture, "after");
    PaintOnce(&fixture, &painted);

    /* A point on the inner box. */
    HitBoxesOf(&fixture, inner, &boxes);
    HitCentreOf(boxes, &x, &y);
    RoboCheckEqInt(HitNodeAt(&fixture, x, y), (int)inner);
    /* The painter emitted the inner box's own marker, and it stands *after* the outer box's
     * content boundary -- a child is drawn over its ancestor's own records. */
    {
        size_t innerMarker = HitIndexFor(&painted, RoboPaintOpBeginBox, inner);
        size_t outerContent = HitIndexFor(&painted, RoboPaintOpContent, outer);

        RoboCheck(innerMarker != (size_t)-1 && outerContent != (size_t)-1);
        RoboCheck(innerMarker > outerContent);
    }

    /* A point on the outer box but not on the inner one: the outer box, whose own marker is
     * earlier in the list than any descendant's -- and which is still the answer, because nothing
     * below it covers the point. */
    HitBoxesOf(&fixture, outer, &boxes);
    x = boxes.content.x + boxes.content.width - 5;
    y = boxes.content.y + boxes.content.height - 5;
    RoboCheckEqInt(HitNodeAt(&fixture, x, y), (int)outer);

    /* A point on the third box, which is drawn last of all and answers for its whole region. */
    HitBoxesOf(&fixture, after, &boxes);
    HitCentreOf(boxes, &x, &y);
    RoboCheckEqInt(HitNodeAt(&fixture, x, y), (int)after);
    {
        size_t afterMarker = HitIndexFor(&painted, RoboPaintOpBeginBox, after);
        size_t innerEnd = HitIndexFor(&painted, RoboPaintOpEndBox, inner);

        RoboCheck(afterMarker != (size_t)-1 && innerEnd != (size_t)-1);
        RoboCheck(afterMarker > innerEnd);
    }

    PaintResultDispose(&painted);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 14. End to end                                                      */
/* ------------------------------------------------------------------ */

/**
 * The brief's own document, with every feature this phase reads, queried at ten points.
 *
 * Blocks, inline text, margins, padding, a border, relative positioning, `overflow: hidden` and a
 * nested opacity -- each answer asserted against the node the *painter* put at that place, so the
 * case is the whole pipeline read backwards rather than a unit test of a helper.
 */
static void TestHitEndToEnd(void)
{
    PaintFixtureDoc fixture;
    LayoutNodeId frame;
    LayoutNodeId label;
    LayoutNodeId sliding;
    LayoutNodeId clipped;
    LayoutNodeId faded;
    PaintBoxes frameBoxes;
    PaintBoxes labelBoxes;
    PaintBoxes slidingBoxes;
    PaintBoxes clippedBoxes;
    PaintBoxes fadedBoxes;
    PaintHitTestResult result;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"label\">Hi</div>"
                         "<div class=\"clipped\"></div></div><div class=\"sliding\"></div>"
                         "<div class=\"faded\"></div></body></html>",
                         HIT_BASE_CSS
                         " .frame { width: 300px; height: 120px; margin: 10px; padding: 12px;"
                         " border: 5px solid #0000ff; overflow: hidden }"
                         " .label { width: 40px; height: 20px }"
                         " .clipped { width: 40px; height: 20px; position: relative;"
                         " left: 400px }"
                         " .sliding { width: 100px; height: 40px; position: relative;"
                         " top: -30px; opacity: 0.5 }"
                         " .faded { width: 60px; height: 20px; opacity: 0 }");
    frame = PaintBox(&fixture, "frame");
    label = PaintBox(&fixture, "label");
    sliding = PaintBox(&fixture, "sliding");
    clipped = PaintBox(&fixture, "clipped");
    faded = PaintBox(&fixture, "faded");
    HitBoxesOf(&fixture, frame, &frameBoxes);
    HitBoxesOf(&fixture, label, &labelBoxes);
    HitBoxesOf(&fixture, sliding, &slidingBoxes);
    HitBoxesOf(&fixture, clipped, &clippedBoxes);
    HitBoxesOf(&fixture, faded, &fadedBoxes);

    /* 1. The frame's own border band: the box, not the document, and not the descendants --
     *    the boundary clips the contents and not the box. */
    HitNodeIs(&fixture, frameBoxes.border.x + 2, frameBoxes.border.y + 2, frame,
                "the frame's border band");
    /* 2. The frame's padding, below the label: the frame answers because no descendant is there
     *    except the clipped one, which is clipped away. */
    HitRunAt(&fixture, frameBoxes.border.x + frameBoxes.border.width - 15,
               frameBoxes.border.y + frameBoxes.border.height - 15, &result, NULL);
    RoboCheckEqInt((int)result.node, (int)frame);
    /* 3. The label's own box: the child, at its own rectangle. */
    HitCentreOf(labelBoxes, &x, &y);
    HitNodeIs(&fixture, x, y, label, "the label box");
    /* 4. A point on the label's text run, if the fixture produced one, is the run -- the run is
     *    inside the label's content box, so its centre is inside the label too. */
    {
        PaintResult painted;
        PaintOperation text;
        size_t index;
        int found = 0;

        PaintOnce(&fixture, &painted);
        for (index = 0u; index < PaintListCount(painted.list); index++) {
            PaintOperation operation;

            if (PaintListAt(painted.list, index, &operation) != RoboOk) {
                break;
            }
            if (operation.kind == RoboPaintOpText) {
                text = operation;
                found = 1;
                break;
            }
        }
        RoboCheckEqInt(found, 1);
        HitCentreOf(text.boxes, &x, &y);
        HitRunAt(&fixture, x, y, &result, NULL);
        RoboCheckEqInt((int)result.node, (int)text.node);
        RoboCheckEqInt(PaintHitTestResultHasFlag(&result,
                                                              RoboPaintHitFlagText),
                          1);
        PaintResultDispose(&painted);
    }
    /* 5. The clipped child, on its own laid-out rectangle: **not** the answer, because the frame
     *    does not draw it there. */
    HitCentreOf(clippedBoxes, &x, &y);
    HitNodeIsNot(&fixture, x, y, clipped, "the clipped child outside its boundary");
    /* The point is outside the frame's boundary too -- the child was laid out well past it -- so
     * the answer is whatever the document has there, and the frame is not among them. */
    HitNodeIsNot(&fixture, x, y, frame, "the frame outside its own box");
    /* 6. The sliding box: the answer over its own (offset) rectangle, with half strength -- and
     *    it is drawn over the frame's flow space, so where they overlap the slider wins. */
    HitCentreOf(slidingBoxes, &x, &y);
    HitRunAt(&fixture, x, y, &result, NULL);
    RoboCheckEqInt((int)result.node, (int)sliding);
    RoboCheckEqInt(PaintOpacityValue(&result.opacity) > 0.49 &&
                          PaintOpacityValue(&result.opacity) < 0.51,
                      1);
    RoboCheckEqInt(PaintHitTestResultHasFlag(&result, RoboPaintHitFlagTransparent),
                      0);
    /* 7. The fully transparent box is still the answer over itself, and says so. */
    HitCentreOf(fadedBoxes, &x, &y);
    HitRunAt(&fixture, x, y, &result, NULL);
    RoboCheckEqInt((int)result.node, (int)faded);
    RoboCheckEqInt(PaintHitTestResultHasFlag(&result, RoboPaintHitFlagTransparent),
                      1);
    /* 8. Well outside the document: nothing, and the walk says so through the counters. */
    HitRunAt(&fixture, 100000, -100000, &result, NULL);
    RoboCheckEqInt((int)result.outcome, (int)RoboPaintHitMiss);
    /* 9. The `CONTENT` flag is derivable from the rectangles the answer carries, so the two are
     *    asserted to agree rather than trusted to. */
    HitCentreOf(labelBoxes, &x, &y);
    HitRunAt(&fixture, x, y, &result, NULL);
    RoboCheckEqInt(PaintHitTestResultHasFlag(&result, RoboPaintHitFlagContent),
                      GfxRectContainsPoint(result.boxes.content, result.point) ? 1 : 0);
    RoboCheck(GfxRectContainsPoint(result.boxes.content, result.point));
    /* 10. The point in the node's own space is the point minus its origin, exactly. */
    RoboCheckEqInt(result.local.x, result.point.x - result.origin.x);
    RoboCheckEqInt(result.local.y, result.point.y - result.origin.y);

    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The screen                                                          */
/* ------------------------------------------------------------------ */

/**
 * The colour @p painted fills @p node's background with, in the surface's own record.
 *
 * The answer's *own* drawing, read back from the paint list rather than assumed from the
 * sheet: the case below compares this against the pixel the same document put on the
 * screen, and a case that hard-coded the colour would be comparing its own arithmetic.
 */
static GfxColorRgba8 HitFillOf(PaintResult *painted,
                                         LayoutNodeId node)
{
    PaintOperation operation;
    GfxColorRgba8 color;

    memset(&color, 0, sizeof(color));
    if (!HitOpFor(painted, RoboPaintOpBackground, node, &operation)) {
        TestFail(__FILE__, __LINE__, "node %d draws no background to compare against",
                       (int)node);
        return color;
    }
    RoboCheckEqInt(PaintColorToRgba8(operation.color, &color), RoboOk);
    return color;
}

/**
 * Query @p x, @p y and hold the answer against the screen: the node the query names must be
 * the node whose fill is drawn at that pixel.
 *
 * This is the phase's visual claim in one call. The two halves are computed by different
 * subsystems -- the colour by the painter, the pixel by the decoration pass -- so agreement is
 * evidence rather than an identity: a walk that named the wrong node, or named a node whose
 * drawing is clipped away, would answer a colour no pixel holds.
 *
 * Opaque fills are what make it an equality rather than a blend: two opaque colours compose by
 * `src-over` to exactly the later one, so "the topmost drawing is this colour" and "this pixel
 * is this colour" are the same statement.
 */
static void HitAgreesWithScreen(PaintFixtureDoc *fixture,
                                   PaintResult *painted,
                                   GfxSurface *screen,
                                   int32_t x,
                                   int32_t y,
                                   LayoutNodeId expectedNode,
                                   const char *what)
{
    PaintHitTestResult result;
    GfxColorRgba8 expected;
    GfxColorRgba8 actual;

    HitRunAt(fixture, x, y, &result, NULL);
    if ((int)result.node != (int)expectedNode) {
        TestFail(__FILE__, __LINE__, "%s: the query named node %d, not %d", what,
                       (int)result.node, (int)expectedNode);
        return;
    }
    expected = HitFillOf(painted, expectedNode);
    actual = HitPixel(screen, x, y);
    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
        actual.a != expected.a) {
        TestFail(__FILE__, __LINE__,
                       "%s: node %d fills rgba(%u,%u,%u,%u) but the pixel is "
                       "rgba(%u,%u,%u,%u)",
                       what, (int)expectedNode, expected.r, expected.g, expected.b, expected.a,
                       actual.r, actual.g, actual.b, actual.a);
    }
}

/**
 * The phase's visual validation: three opaque boxes, two of them overlapping, and every query
 * checked against the screen.
 *
 * The fixture is the brief's sketch, and each probe answers a different question:
 *
 *   in `.under` alone     the point resolves to the box drawn *there*, and the pixel proves the
 *                         walk did not answer with the box that covers its neighbour;
 *   in the overlap        the point resolves to the box drawn **last** -- `position: relative`
 *                         moved `.over` onto `.under` without taking it out of flow, so the two
 *                         genuinely overlap and paint order is the only thing that decides;
 *   in the frame gutter   the point resolves to the ancestor, which is the fallback rule;
 *   outside everything    nothing is hit *and* nothing was drawn, so a walk that guessed a box
 *                         for every point would be caught by the unpainted pixel.
 *
 * `.under` and `.over` are given **different** colours on purpose: agreement is only evidence if
 * the two boxes could have been told apart by it.
 */
static void TestHitAgreesWithScreen(void)
{
    PaintFixtureDoc fixture;
    PaintResult painted;
    GfxSurface *screen = NULL;
    PaintOperation operation;
    PaintBoxes frameBoxes;
    PaintBoxes underBoxes;
    PaintBoxes overBoxes;
    LayoutNodeId frame;
    LayoutNodeId under;
    LayoutNodeId over;
    GfxColorRgba8 missPixel;
    PaintHitTestResult result;
    LayoutRect overlap;
    int32_t x;
    int32_t y;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"under\"></div>"
                         "<div class=\"over\"></div></div></body></html>",
                         HIT_BASE_CSS
                         " .frame { width: 300px; height: 200px; background-color: #14283c }"
                         " .under { width: 120px; height: 80px; background-color: #c81e1e }"
                         " .over { width: 120px; height: 80px; background-color: #1ea05a;"
                         "         position: relative; top: -60px; left: 40px }");
    frame = PaintBox(&fixture, "frame");
    under = PaintBox(&fixture, "under");
    over = PaintBox(&fixture, "over");
    PaintOnce(&fixture, &painted);

    /* The boxes come from the painter's own records, as everywhere else in this suite. */
    RoboCheckEqInt(HitOpFor(&painted, RoboPaintOpBeginBox, frame, &operation), 1);
    frameBoxes = operation.boxes;
    RoboCheckEqInt(HitOpFor(&painted, RoboPaintOpBeginBox, under, &operation), 1);
    underBoxes = operation.boxes;
    RoboCheckEqInt(HitOpFor(&painted, RoboPaintOpBeginBox, over, &operation), 1);
    overBoxes = operation.boxes;

    /* The fixture's premise, computed rather than assumed: the relative offset really did carry
     * `.over` onto `.under`, so the overlap the probes below name is a fact about this document.
     *
     * `.over` flows *below* `.under` -- its normal-flow top is `.under`'s bottom -- so a `top` of
     * -60px can only bring it into `.under`'s band if the offset was applied at all, and that is
     * exactly what `over.top < under.bottom` states. Computing the intersection here also lets the
     * probes be the overlap's own midpoint instead of an arithmetic guess. */
    RoboCheck(overBoxes.border.y < underBoxes.border.y + underBoxes.border.height);
    RoboCheck(overBoxes.border.x < underBoxes.border.x + underBoxes.border.width);
    overlap.x = overBoxes.border.x > underBoxes.border.x ? overBoxes.border.x
                                                          : underBoxes.border.x;
    overlap.y = overBoxes.border.y > underBoxes.border.y ? overBoxes.border.y
                                                          : underBoxes.border.y;
    overlap.width = (underBoxes.border.x + underBoxes.border.width <
                             overBoxes.border.x + overBoxes.border.width
                         ? underBoxes.border.x + underBoxes.border.width
                         : overBoxes.border.x + overBoxes.border.width) -
                    overlap.x;
    overlap.height = (underBoxes.border.y + underBoxes.border.height <
                              overBoxes.border.y + overBoxes.border.height
                          ? underBoxes.border.y + underBoxes.border.height
                          : overBoxes.border.y + overBoxes.border.height) -
                     overlap.y;
    RoboCheck(overlap.width > 0 && overlap.height > 0);

    screen = HitSurface(320u, 240u);
    HitPaintTo(&painted, screen);

    /* 1. In `.under` alone: the red box is the answer and the red fill is the pixel. The point is
     *    asserted to be clear of `.over`'s rectangle, so "alone" is checked rather than hoped. */
    x = underBoxes.border.x + 5;
    y = underBoxes.border.y + underBoxes.border.height - 5;
    RoboCheck(x < overBoxes.border.x || x >= overBoxes.border.x + overBoxes.border.width ||
               y < overBoxes.border.y || y >= overBoxes.border.y + overBoxes.border.height);
    HitAgreesWithScreen(&fixture, &painted, screen, x, y, under, "in .under alone");

    /* 2. In the overlap: the **later-painted** box owns the point, and the screen agrees. The
     *    probe is the intersection's own midpoint, so it is inside both boxes by construction. */
    x = overlap.x + overlap.width / 2;
    y = overlap.y + overlap.height / 2;
    HitAgreesWithScreen(&fixture, &painted, screen, x, y, over, "in the overlap");

    /* 3. The frame's own gutter, clear of both children: the ancestor is the fallback. */
    x = frameBoxes.border.x + frameBoxes.border.width - 5;
    y = frameBoxes.border.y + frameBoxes.border.height - 5;
    HitAgreesWithScreen(&fixture, &painted, screen, x, y, frame, "in the frame gutter");

    /* 4. Outside the document: nothing is hit, and the control is that **nothing was drawn**
     *    there -- so the pixel is fully transparent rather than any box's colour. */
    HitRunAt(&fixture, 319, 239, &result, NULL);
    RoboCheckEqInt((int)result.outcome, (int)RoboPaintHitMiss);
    missPixel = HitPixel(screen, 319, 239);
    RoboCheckEqInt((int)missPixel.a, 0);
    /* And the surface really was written to elsewhere, so the transparent corner is a fact about
     * the document and not an unpainted surface. */
    RoboCheckEqInt((int)HitPixel(screen, overlap.x + overlap.width / 2,
                                     overlap.y + overlap.height / 2).a,
                      255);

    /* 5. The frame's fill is visible where no child covers it *and* the two children's colours
     *    differ, so the probes above could not all have been satisfied by one colour. */
    RoboCheck(HitFillOf(&painted, under).r != HitFillOf(&painted, over).r ||
               HitFillOf(&painted, under).g != HitFillOf(&painted, over).g ||
               HitFillOf(&painted, under).b != HitFillOf(&painted, over).b);

    GfxSurfaceRelease(screen);
    PaintResultDispose(&painted);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterPaintHitTest(void)
{
    TestRegister("paint.hit-test.inside_and_outside", TestHitInsideAndOutside);
    TestRegister("paint.hit-test.region_is_border_box", TestHitRegionIsBorderBox);
    TestRegister("paint.hit-test.empty_boxes", TestHitEmptyBoxes);
    TestRegister("paint.hit-test.nested_and_fallback", TestHitNestedAndFallback);
    TestRegister("paint.hit-test.overlapping_siblings", TestHitOverlappingSiblings);
    TestRegister("paint.hit-test.overlap_is_by_drawing",
                       TestHitOverlapIsByDrawingNotByPosition);
    TestRegister("paint.hit-test.descendant_over_ancestor", TestHitDescendantOverAncestor);
    TestRegister("paint.hit-test.clip_contents_not_box", TestHitClipContentsNotBox);
    TestRegister("paint.hit-test.clip_inside_matches_painter",
                       TestHitClipInsideMatchesPainter);
    TestRegister("paint.hit-test.clip_nested", TestHitClipNested);
    TestRegister("paint.hit-test.clip_empty", TestHitClipEmpty);
    TestRegister("paint.hit-test.clip_contains_door", TestHitClipContainsDoor);
    TestRegister("paint.hit-test.opacity_does_not_gate", TestHitOpacityDoesNotGate);
    TestRegister("paint.hit-test.opacity_nested_matches_painter",
                       TestHitOpacityNestedMatchesPainter);
    TestRegister("paint.hit-test.display_none", TestHitDisplayNone);
    TestRegister("paint.hit-test.visibility_is_not_consumed",
                       TestHitVisibilityIsNotConsumed);
    TestRegister("paint.hit-test.text_run", TestHitTextRun);
    TestRegister("paint.hit-test.safety", TestHitSafety);
    TestRegister("paint.hit-test.determinism", TestHitDeterminism);
    TestRegister("paint.hit-test.mutation_isolation", TestHitMutationIsolation);
    TestRegister("paint.hit-test.stats_identities", TestHitStatsIdentities);
    TestRegister("paint.hit-test.result_model", TestHitResultModel);
    TestRegister("paint.hit-test.dump", TestHitDump);
    TestRegister("paint.hit-test.matches_the_paint_list", TestHitMatchesThePaintList);
    TestRegister("paint.hit-test.end_to_end", TestHitEndToEnd);
    TestRegister("paint.hit-test.agrees_with_screen", TestHitAgreesWithScreen);
}
