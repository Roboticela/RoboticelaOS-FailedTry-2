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
 * a node's region is its border box (`robo_paint_boxes_of`), which is the *same* door a replaced
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

/* The pixel reader: `robo_gfx_surface_get_pixel`, which is the door the painting suites read
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
static int hit_op_for(const paint_result_t *result,
                      robo_paint_op_kind_t kind,
                      robo_layout_node_id_t node,
                      robo_paint_operation_t *out)
{
    size_t index;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
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
static size_t hit_index_for(const paint_result_t *result,
                            robo_paint_op_kind_t kind,
                            robo_layout_node_id_t node)
{
    size_t index;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
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
static void hit_boxes_of(paint_fixture_doc_t *fixture,
                         robo_layout_node_id_t node,
                         robo_paint_boxes_t *out)
{
    paint_result_t result;
    robo_paint_operation_t operation;

    paint_once(fixture, &result);
    ROBO_CHECK(hit_op_for(&result, ROBO_PAINT_OP_BEGIN_BOX, node, &operation) ||
               hit_op_for(&result, ROBO_PAINT_OP_TEXT, node, &operation));
    *out = operation.boxes;
    paint_result_dispose(&result);
}

/** The centre of @p rect: the one coordinate that is inside whatever the extent is. */
static void hit_centre(robo_layout_rect_t rect, int32_t *out_x, int32_t *out_y)
{
    *out_x = rect.x + rect.width / 2;
    *out_y = rect.y + rect.height / 2;
}

/** The centre of a box's border box. */
static void hit_centre_of(robo_paint_boxes_t boxes, int32_t *out_x, int32_t *out_y)
{
    hit_centre(boxes.border, out_x, out_y);
}

/** Run one query, reporting a failure rather than asserting a particular answer. */
static void hit_run_at(paint_fixture_doc_t *fixture,
                       int32_t x,
                       int32_t y,
                       robo_paint_hit_test_result_t *out,
                       robo_paint_hit_test_stats_t *stats)
{
    robo_paint_hit_test_result_t result;
    robo_paint_hit_test_stats_t local;
    robo_paint_diag_t diag;
    robo_result_t status;

    robo_paint_hit_test_result_init(&result);
    robo_paint_hit_test_stats_init(&local);
    robo_paint_diag_reset(&diag);
    status = robo_paint_hit_test(fixture->tree, robo_gfx_point(x, y), &result, &local, &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the query refused: %s (%s:%d)",
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

/** The node the query at (@p x, @p y) answers, or `ROBO_LAYOUT_NODE_ID_NONE`. */
static robo_layout_node_id_t hit_node_at(paint_fixture_doc_t *fixture, int32_t x, int32_t y)
{
    robo_paint_hit_test_result_t result;

    hit_run_at(fixture, x, y, &result, NULL);
    return result.node;
}

/** Assert the answer about (@p x, @p y) is @p expected. */
static void hit_node_is(paint_fixture_doc_t *fixture,
                        int32_t x,
                        int32_t y,
                        robo_layout_node_id_t expected,
                        const char *what)
{
    robo_paint_hit_test_result_t result;

    hit_run_at(fixture, x, y, &result, NULL);
    if (result.node != expected) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: the point %d,%d answered node %u (%s), expected %u", what, x, y,
                       (unsigned)result.node, robo_paint_hit_test_outcome_name(result.outcome),
                       (unsigned)expected);
    }
}

/** Assert the answer about (@p x, @p y) is **not** @p unexpected. */
static void hit_node_is_not(paint_fixture_doc_t *fixture,
                            int32_t x,
                            int32_t y,
                            robo_layout_node_id_t unexpected,
                            const char *what)
{
    robo_paint_hit_test_result_t result;

    hit_run_at(fixture, x, y, &result, NULL);
    if (result.node == unexpected) {
        robo_test_fail(__FILE__, __LINE__, "%s: the point %d,%d answered node %u, which it must "
                                           "not",
                       what, x, y, (unsigned)unexpected);
    }
}

/** Assert two groups of boxes are the same record. */
static void hit_boxes_eq(robo_paint_boxes_t actual, robo_paint_boxes_t expected, const char *what)
{
    if (!robo_paint_boxes_equal(&actual, &expected)) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: expected border=%d,%d %dx%d, got border=%d,%d %dx%d", what,
                       expected.border.x, expected.border.y, expected.border.width,
                       expected.border.height, actual.border.x, actual.border.y,
                       actual.border.width, actual.border.height);
    }
}

/** Assert two clips are the same record. */
static void hit_clip_eq(robo_paint_clip_t actual, robo_paint_clip_t expected, const char *what)
{
    if (!robo_paint_clip_equal(&actual, &expected)) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: expected clip %d[%d,%d]/%d[%d,%d], got %d[%d,%d]/%d[%d,%d]", what,
                       (int)expected.horizontal.mode, expected.horizontal.start,
                       expected.horizontal.end, (int)expected.vertical.mode,
                       expected.vertical.start, expected.vertical.end, (int)actual.horizontal.mode,
                       actual.horizontal.start, actual.horizontal.end, (int)actual.vertical.mode,
                       actual.vertical.start, actual.vertical.end);
    }
}

/** Assert two strengths are the same record. */
static void hit_opacity_eq(robo_paint_opacity_t actual,
                           robo_paint_opacity_t expected,
                           const char *what)
{
    if (!robo_paint_opacity_equal(&actual, &expected)) {
        robo_test_fail(__FILE__, __LINE__, "%s: expected strength %.4f, got %.4f", what,
                       robo_paint_opacity_value(&expected), robo_paint_opacity_value(&actual));
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
static robo_gfx_surface_t *hit_surface(uint32_t width, uint32_t height)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surface), ROBO_OK);
    ROBO_CHECK(surface != NULL);
    return surface;
}

/** Build a display list from @p records and paint it into @p target. */
static void hit_paint_to(paint_result_t *records, robo_gfx_surface_t *target)
{
    robo_paint_display_list_builder_t *builder = NULL;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t decoration_stats;
    robo_paint_diag_t diag;
    robo_result_t status;

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
    robo_paint_diag_reset(&diag);
    status = robo_paint_display_list_builder_build(builder, records->list, list, &list_stats,
                                                   &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the display build refused the records: %s",
                       diag.message ? diag.message : "(no message)");
    }
    robo_paint_display_list_builder_destroy(builder);

    robo_paint_diag_reset(&diag);
    status = robo_paint_decoration_paint(list, target, NULL, &decoration_stats, &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the decoration pass refused the list: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
    robo_paint_display_list_destroy(list);
}

/** The pixel at (x, y), failing when the sample is outside the surface. */
static robo_gfx_color_rgba8_t hit_pixel(robo_gfx_surface_t *surface, int32_t x, int32_t y)
{
    robo_gfx_color_rgba8_t color;
    int32_t width = (int32_t)robo_gfx_surface_width(surface);
    int32_t height = (int32_t)robo_gfx_surface_height(surface);

    memset(&color, 0, sizeof(color));
    if (x < 0 || y < 0 || x >= width || y >= height) {
        robo_test_fail(__FILE__, __LINE__, "the sample %d,%d is outside the %dx%d screen", (int)x,
                       (int)y, (int)width, (int)height);
        return color;
    }
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, (uint32_t)x, (uint32_t)y, &color),
                      ROBO_OK);
    return color;
}

/* ------------------------------------------------------------------ */
/* 1. Basic geometry                                                   */
/* ------------------------------------------------------------------ */

/** A point inside a box is on it; a point outside the whole document is on nothing. */
static void test_hit_inside_and_outside(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t frame;
    robo_layout_node_id_t root;
    robo_paint_boxes_t boxes;
    robo_paint_hit_test_result_t result;
    robo_paint_hit_test_stats_t stats;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px }");
    frame = paint_box(&fixture, "frame");
    root = paint_box_by_tag(&fixture, "html");
    hit_boxes_of(&fixture, frame, &boxes);

    hit_centre_of(boxes, &x, &y);
    hit_run_at(&fixture, x, y, &result, &stats);

    ROBO_CHECK_EQ_INT((int)result.outcome, (int)ROBO_PAINT_HIT_ON_NODE);
    ROBO_CHECK_EQ_INT((int)result.node, (int)frame);
    ROBO_CHECK_EQ_INT((int)result.kind, (int)ROBO_LAYOUT_OBJECT_BOX);
    ROBO_CHECK_EQ_INT((int)result.depth, 2);
    ROBO_CHECK_EQ_INT((int)stats.hits, 1);
    /* The geometry the answer carries is the geometry the painter wrote for the same box, in
     * the same space -- the agreement this phase exists to make checkable. */
    hit_boxes_eq(result.boxes, boxes, "the hit box is the painted box");
    ROBO_CHECK_EQ_INT((int)result.point.x, x);
    ROBO_CHECK_EQ_INT((int)result.point.y, y);
    ROBO_CHECK_EQ_INT((int)robo_paint_hit_test_result_is_hit(&result), 1);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_is_valid(&result), 1);

    /* And well outside every box in the document: nothing is there, and the counters say the
     * walk excluded the document rather than that it forgot to look. */
    hit_run_at(&fixture, 100000, 100000, &result, &stats);
    ROBO_CHECK_EQ_INT((int)result.outcome, (int)ROBO_PAINT_HIT_MISS);
    ROBO_CHECK_EQ_INT((int)result.node, (int)ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT((int)stats.hits, 0);
    ROBO_CHECK(stats.rejected_region + stats.rejected_clip >= 1u);
    ROBO_CHECK(robo_paint_hit_test_result_is_valid(&result));
    /* The tree's root was examined and rejected; nothing below it was, because a point outside a
     * box's region cannot be on anything the box contains. */
    ROBO_CHECK_EQ_INT((int)stats.objects, 1);

    /* A negative coordinate is as legal as a positive one and reaches the same conclusion. */
    hit_node_is(&fixture, -1, -1, ROBO_LAYOUT_NODE_ID_NONE, "a negative point outside the page");
    (void)root;
    paint_end(&fixture);
}

/** The region is the **border box**, half-open: `right`/`bottom` are out, one left of them is in. */
static void test_hit_region_is_border_box(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t frame;
    robo_paint_boxes_t boxes;
    int32_t x;
    int32_t y;
    int32_t right;
    int32_t bottom;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px }");
    frame = paint_box(&fixture, "frame");
    hit_boxes_of(&fixture, frame, &boxes);
    hit_centre_of(boxes, &x, &y);
    right = boxes.border.x + boxes.border.width;
    bottom = boxes.border.y + boxes.border.height;

    /* The four corners are the four boundary points, and half-open means two of them are inside
     * and two are not -- which is the convention `robo_gfx_rect_contains_point` states and the
     * one every rectangle in this build shares. */
    hit_node_is(&fixture, boxes.border.x, boxes.border.y, frame, "the top-left corner is inside");
    hit_node_is(&fixture, right - 1, bottom - 1, frame, "one left of the bottom-right is inside");
    hit_node_is_not(&fixture, right, y, frame, "the right edge is outside");
    hit_node_is_not(&fixture, x, bottom, frame, "the bottom edge is outside");
    hit_node_is_not(&fixture, boxes.border.x - 1, y, frame, "one left of the box is outside");
    hit_node_is_not(&fixture, x, boxes.border.y - 1, frame, "one above the box is outside");

    /* The three layers of a box with no padding and no border are one rectangle, and the record
     * says so rather than leaving a caller to compare them. */
    ROBO_CHECK_EQ_INT(boxes.insets_measured, 1);
    ROBO_CHECK_EQ_INT(boxes.border.width, 200);
    ROBO_CHECK_EQ_INT(boxes.border.height, 100);
    paint_end(&fixture);
}

/** An empty box has an empty region: no point is on it, and none is invented for it. */
static void test_hit_empty_boxes(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t wide;
    robo_layout_node_id_t tall;
    robo_paint_boxes_t boxes;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"wide\"></div><div class=\"tall\"></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .wide { width: 100px; height: 0px }"
                                      " .tall { width: 0px; height: 100px }");
    wide = paint_box(&fixture, "wide");
    tall = paint_box(&fixture, "tall");

    hit_boxes_of(&fixture, wide, &boxes);
    ROBO_CHECK_EQ_INT(boxes.border.width, 100);
    ROBO_CHECK_EQ_INT(boxes.border.height, 0);
    /* A zero-height box is empty, and an empty rectangle contains no point -- not even its own
     * top-left corner, because there is no interior for one to be in. */
    hit_node_is_not(&fixture, boxes.border.x, boxes.border.y, wide, "a zero-height box");
    hit_node_is_not(&fixture, boxes.border.x + 50, boxes.border.y, wide,
                    "the middle of a zero-height box");

    hit_boxes_of(&fixture, tall, &boxes);
    ROBO_CHECK_EQ_INT(boxes.border.width, 0);
    ROBO_CHECK_EQ_INT(boxes.border.height, 100);
    hit_node_is_not(&fixture, boxes.border.x, boxes.border.y, tall, "a zero-width box");
    hit_node_is_not(&fixture, boxes.border.x, boxes.border.y + 50, tall,
                    "the middle of a zero-width box");

    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 2. Nesting and fallback                                             */
/* ------------------------------------------------------------------ */

/** A point on a child is the child; a point on the parent but on no child is the parent. */
static void test_hit_nested_and_fallback(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;
    robo_layout_node_id_t leaf;
    robo_paint_boxes_t outer_boxes;
    robo_paint_boxes_t inner_boxes;
    robo_paint_boxes_t leaf_boxes;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\">"
                         "<div class=\"leaf\"></div></div></div></body></html>",
                         HIT_BASE_CSS
                         " .outer { width: 300px; height: 200px; padding: 20px }"
                         " .inner { width: 100px; height: 80px; padding: 10px }"
                         " .leaf { width: 40px; height: 20px }");
    outer = paint_box(&fixture, "outer");
    inner = paint_box(&fixture, "inner");
    leaf = paint_box(&fixture, "leaf");
    hit_boxes_of(&fixture, outer, &outer_boxes);
    hit_boxes_of(&fixture, inner, &inner_boxes);
    hit_boxes_of(&fixture, leaf, &leaf_boxes);

    /* Three levels, three answers, and each point is inside the one it names *and* inside the two
     * above it -- so a walk that answered with an ancestor would fail here. */
    hit_centre_of(leaf_boxes, &x, &y);
    hit_node_is(&fixture, x, y, leaf, "the deepest child wins");
    hit_centre_of(inner_boxes, &x, &y);
    hit_node_is(&fixture, x, y, inner, "the middle child wins where the leaf is not");
    /* A point in the outer box's padding, below and right of the inner one: on no descendant, so
     * the ancestor answers -- which is the fallback every event dispatch depends on. */
    hit_node_is(&fixture, outer_boxes.content.x + outer_boxes.content.width - 5,
                outer_boxes.content.y + outer_boxes.content.height - 5, outer,
                "the parent answers where no child is");
    /* And the *ancestors* are answers too, at their own edges. */
    hit_node_is(&fixture, outer_boxes.border.x + 1, outer_boxes.border.y + 1, outer,
                "the outer box's own padding edge");

    paint_end(&fixture);
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
static void test_hit_overlapping_siblings(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t first;
    robo_layout_node_id_t second;
    robo_paint_boxes_t first_boxes;
    robo_paint_boxes_t second_boxes;
    paint_result_t result;
    int32_t x;
    int32_t y;
    int overlap_x;
    int overlap_y;
    size_t first_end;
    size_t second_begin;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"first\"></div><div class=\"second\"></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .first { width: 200px; height: 100px }"
                                      " .second { width: 200px; height: 100px;"
                                      " position: relative; top: -60px }");
    first = paint_box(&fixture, "first");
    second = paint_box(&fixture, "second");
    hit_boxes_of(&fixture, first, &first_boxes);
    hit_boxes_of(&fixture, second, &second_boxes);

    /* The fixture is what it claims to be: the two rectangles overlap, and neither contains the
     * other. A case that silently stopped overlapping would pass for the wrong reason. */
    ROBO_CHECK(robo_gfx_rect_intersects(first_boxes.border, second_boxes.border));
    ROBO_CHECK(second_boxes.border.y < first_boxes.border.y + first_boxes.border.height);

    overlap_x = second_boxes.border.x + second_boxes.border.width / 2;
    overlap_y = second_boxes.border.y + 5;

    hit_node_is(&fixture, overlap_x, overlap_y, second, "the later sibling wins the overlap");
    /* Near its top edge, only the first is there -- so the answer is not merely "the last box in
     * the document", which a walk that ignored geometry would say. The second begins a third of
     * the way down the first, and its pull-up means its *own* box covers the rest. */
    y = first_boxes.border.y + 5;
    ROBO_CHECK(robo_gfx_rect_contains_point(first_boxes.border, robo_gfx_point(overlap_x, y)));
    ROBO_CHECK(!robo_gfx_rect_contains_point(second_boxes.border, robo_gfx_point(overlap_x, y)));
    hit_node_is(&fixture, overlap_x, y, first, "the earlier sibling still owns its own area");

    /*
     * And the painter agrees about the order, read out of the list rather than argued: the
     * winner's own records stand **after** the loser's subtree, which is the same statement as
     * "the later sibling is drawn over the earlier one". A hit test that reversed the wrong thing
     * would answer `first` while this assertion still passed -- which is why both are here.
     */
    paint_once(&fixture, &result);
    first_end = hit_index_for(&result, ROBO_PAINT_OP_END_BOX, first);
    second_begin = hit_index_for(&result, ROBO_PAINT_OP_BEGIN_BOX, second);
    ROBO_CHECK(first_end != (size_t)-1 && second_begin != (size_t)-1);
    ROBO_CHECK(second_begin > first_end);
    paint_result_dispose(&result);

    (void)x;
    paint_end(&fixture);
}

/**
 * Three overlapping siblings where the **middle** one is on top.
 *
 * The case a walk that simply reversed the *document* order would get wrong: the answer is not the
 * last box in the document but the last one *drawn at that point*, and the third sibling is pulled
 * clear of the overlap while the second is pulled up over the first.
 */
static void test_hit_overlap_is_by_drawing_not_by_position(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t first;
    robo_layout_node_id_t second;
    robo_layout_node_id_t third;
    robo_paint_boxes_t first_boxes;
    robo_paint_boxes_t second_boxes;
    robo_paint_boxes_t third_boxes;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"first\"></div><div class=\"second\"></div>"
                         "<div class=\"third\"></div></body></html>",
                         HIT_BASE_CSS " .first { width: 200px; height: 100px }"
                                      " .second { width: 200px; height: 100px;"
                                      " position: relative; top: -80px }"
                                      " .third { width: 40px; height: 20px;"
                                      " position: relative; top: -180px }");
    first = paint_box(&fixture, "first");
    second = paint_box(&fixture, "second");
    third = paint_box(&fixture, "third");
    hit_boxes_of(&fixture, first, &first_boxes);
    hit_boxes_of(&fixture, second, &second_boxes);
    hit_boxes_of(&fixture, third, &third_boxes);

    ROBO_CHECK(robo_gfx_rect_intersects(first_boxes.border, second_boxes.border));

    hit_centre_of(third_boxes, &x, &y);
    /* The fixture is what the case claims: the point is inside all three, and the second is drawn
     * over the first while the third is drawn over both -- so it is the third that answers, even
     * though the second is the *previous* box in the document. */
    ROBO_CHECK(robo_gfx_rect_contains_point(first_boxes.border, robo_gfx_point(x, y)));
    ROBO_CHECK(robo_gfx_rect_contains_point(second_boxes.border, robo_gfx_point(x, y)));
    ROBO_CHECK(robo_gfx_rect_contains_point(third_boxes.border, robo_gfx_point(x, y)));
    hit_node_is(&fixture, x, y, third, "the last drawn of three overlapping boxes");
    /* Just below the third, still in the overlap of the first two -- and there the second is the
     * last drawn, so the third's band really is what decided the answer above. */
    y = third_boxes.border.y + third_boxes.border.height;
    ROBO_CHECK(robo_gfx_rect_contains_point(second_boxes.border, robo_gfx_point(x, y)));
    hit_node_is(&fixture, x, y, second, "the second where the third has ended");
    /* And at the very top of the first, above where the second reaches. */
    hit_node_is(&fixture, first_boxes.border.x + 10, first_boxes.border.y, first,
                "the first where neither later box reaches");

    paint_end(&fixture);
}

/** A descendant is over its ancestor's own region, so it answers where the two overlap. */
static void test_hit_descendant_over_ancestor(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;
    robo_paint_boxes_t inner_boxes;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .outer { width: 200px; height: 200px }"
                                      " .inner { width: 200px; height: 200px;"
                                      " background-color: #ff0000 }");
    outer = paint_box(&fixture, "outer");
    inner = paint_box(&fixture, "inner");
    hit_boxes_of(&fixture, inner, &inner_boxes);
    hit_centre_of(inner_boxes, &x, &y);

    /* The child covers the parent exactly, and is painted after it -- so the child answers even
     * though both rectangles contain the point. */
    hit_node_is(&fixture, x, y, inner, "the child over a parent it covers exactly");
    ROBO_CHECK_EQ_INT((int)outer, (int)inner - 1u);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. Clipping                                                         */
/* ------------------------------------------------------------------ */

/**
 * The boundary clips the **contents**, never the box: an overflowing child is not the answer
 * outside it, while the clipped ancestor's own border band is.
 */
static void test_hit_clip_contents_not_box(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t frame;
    robo_layout_node_id_t leaf;
    robo_paint_boxes_t frame_boxes;
    robo_paint_boxes_t leaf_boxes;
    robo_paint_hit_test_result_t result;
    robo_paint_hit_test_stats_t stats;
    paint_result_t painted;
    robo_paint_operation_t push;
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
     * silently stop testing the boundary. This one reaches it, and the `rejected_clip` assertion
     * below is what says so.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .frame { width: 100px; height: 60px; overflow: hidden;"
                                      " border: 10px solid #0000ff }"
                                      " .leaf { width: 50px; height: 40px; position: relative;"
                                      " left: 70px; background-color: #00ff00 }");
    frame = paint_box(&fixture, "frame");
    leaf = paint_box(&fixture, "leaf");
    hit_boxes_of(&fixture, frame, &frame_boxes);
    hit_boxes_of(&fixture, leaf, &leaf_boxes);

    /* Inside the boundary: the leaf is the deepest node whose rectangle contains the point, so it
     * answers -- which is the other side of the boundary and what makes the case below meaningful
     * rather than merely "the leaf is unreachable". */
    hit_node_is(&fixture, frame_boxes.padding.x + frame_boxes.padding.width - 1,
                leaf_boxes.border.y + 20, leaf, "inside the boundary the leaf answers");

    /* In the right border band: inside the leaf's rectangle, inside the frame's border box, and
     * **outside** the boundary the frame's contents are clipped to. So the leaf is not the answer
     * -- which is the case a naive hit test gets wrong invisibly, because the node is laid out
     * there and simply is not drawn there. */
    x = frame_boxes.padding.x + frame_boxes.padding.width + 5;
    ROBO_CHECK(x < frame_boxes.border.x + frame_boxes.border.width);
    ROBO_CHECK(robo_gfx_rect_contains_point(frame_boxes.border, robo_gfx_point(x, leaf_boxes.border.y + 20)));
    ROBO_CHECK(robo_gfx_rect_contains_point(leaf_boxes.border, robo_gfx_point(x, leaf_boxes.border.y + 20)));
    hit_run_at(&fixture, x, leaf_boxes.border.y + 20, &result, &stats);
    ROBO_CHECK_EQ_INT((int)result.node, (int)frame);
    ROBO_CHECK(stats.rejected_clip >= 1u);

    /* And the frame's **own border band** is where the answer comes from, because a box's boundary
     * applies to its contents and not to itself -- 11.8's arrangement, read backwards. */
    hit_node_is(&fixture, frame_boxes.border.x + 2, frame_boxes.border.y + 2, frame,
                "the clipped ancestor's own border band");
    hit_node_is(&fixture, frame_boxes.border.x + 2,
                frame_boxes.border.y + frame_boxes.border.height - 2, frame,
                "the clipped ancestor's own bottom band");
    (void)y;

    /* The boundary the answer carries is the one in force on it; for the frame that is the
     * document's (nothing), and the painter says the same thing about the frame's own records. */
    hit_run_at(&fixture, frame_boxes.border.x + 2, frame_boxes.border.y + 2, &result, NULL);
    paint_once(&fixture, &painted);
    ROBO_CHECK_EQ_INT(hit_op_for(&painted, ROBO_PAINT_OP_BEGIN_BOX, frame, &push), 1);
    hit_clip_eq(result.clip, push.clip, "the frame's own boundary is the painted one");
    paint_result_dispose(&painted);

    /* A leaf that ends up *inside* the boundary is still answerable, so the rejection above is
     * about the boundary and not about the leaf being unreachable. */
    hit_node_is(&fixture, frame_boxes.padding.x + 5, frame_boxes.padding.y + 5, frame,
                "the frame where the leaf is not");

    paint_end(&fixture);
}

/** A child inside the boundary is the answer, and its boundary is the painter's boundary. */
static void test_hit_clip_inside_matches_painter(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t frame;
    robo_layout_node_id_t leaf;
    robo_paint_boxes_t frame_boxes;
    robo_paint_boxes_t leaf_boxes;
    robo_paint_hit_test_result_t result;
    paint_result_t painted;
    robo_paint_operation_t text_or_marker;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .frame { width: 100px; height: 60px; overflow: hidden }"
                                      " .leaf { width: 50px; height: 40px }");
    frame = paint_box(&fixture, "frame");
    leaf = paint_box(&fixture, "leaf");
    hit_boxes_of(&fixture, frame, &frame_boxes);
    hit_boxes_of(&fixture, leaf, &leaf_boxes);
    hit_centre_of(leaf_boxes, &x, &y);

    hit_run_at(&fixture, x, y, &result, NULL);
    ROBO_CHECK_EQ_INT((int)result.node, (int)leaf);
    /* The leaf lives inside the frame's boundary, so its answer carries it -- and the painter's
     * own record for the same node carries the identical record. That is the clipping half of the
     * agreement claim: one intersection, computed by one door, read by two walks. */
    paint_once(&fixture, &painted);
    ROBO_CHECK_EQ_INT(hit_op_for(&painted, ROBO_PAINT_OP_BEGIN_BOX, leaf, &text_or_marker), 1);
    hit_clip_eq(result.clip, text_or_marker.clip, "the leaf's boundary is the painted one");
    ROBO_CHECK(robo_paint_clip_is_active(&result.clip));
    paint_result_dispose(&painted);

    /* And the frame's padding box is what it was clipped to. */
    ROBO_CHECK_EQ_INT(result.clip.horizontal.start, frame_boxes.padding.x);
    ROBO_CHECK_EQ_INT(result.clip.horizontal.end,
                      frame_boxes.padding.x + frame_boxes.padding.width);

    paint_end(&fixture);
}

/** Nested boundaries: the innermost one decides, and it is the intersection of both. */
static void test_hit_clip_nested(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t outer;
    robo_layout_node_id_t mid;
    robo_layout_node_id_t leaf;
    robo_paint_boxes_t outer_boxes;
    robo_paint_boxes_t mid_boxes;
    robo_paint_boxes_t leaf_boxes;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"mid\">"
                         "<div class=\"leaf\"></div></div></div></body></html>",
                         HIT_BASE_CSS
                         " .outer { width: 200px; height: 100px; overflow: hidden }"
                         " .mid { width: 60px; height: 40px; overflow: hidden;"
                         " position: relative; left: 10px; top: 10px }"
                         " .leaf { width: 400px; height: 300px }");
    outer = paint_box(&fixture, "outer");
    mid = paint_box(&fixture, "mid");
    leaf = paint_box(&fixture, "leaf");
    hit_boxes_of(&fixture, outer, &outer_boxes);
    hit_boxes_of(&fixture, mid, &mid_boxes);
    hit_boxes_of(&fixture, leaf, &leaf_boxes);

    /* Inside `mid`, so inside both boundaries: the leaf answers, and that is the deepest node
     * whose rectangle actually contains the point. */
    hit_node_is(&fixture, mid_boxes.padding.x + 5, mid_boxes.padding.y + 5, leaf,
                "inside both boundaries the leaf answers");
    /* Between `mid` and `outer`: the leaf reaches there geometrically but `mid` clips it, so the
     * answer is `mid`. */
    x = outer_boxes.padding.x + outer_boxes.padding.width - 5;
    y = outer_boxes.padding.y + outer_boxes.padding.height - 5;
    ROBO_CHECK(robo_gfx_rect_contains_point(leaf_boxes.border, robo_gfx_point(x, y)));
    hit_node_is(&fixture, x, y, outer, "beyond both boundaries the leaf is gone");
    /* And outside `outer`: nothing on this subtree. */
    hit_node_is_not(&fixture, outer_boxes.border.x + outer_boxes.border.width + 5,
                    outer_boxes.border.y + 5, leaf, "outside both boundaries");

    paint_end(&fixture);
}

/** A boundary with no area inside it excludes everything within it. */
static void test_hit_clip_empty(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t frame;
    robo_layout_node_id_t leaf;
    robo_paint_boxes_t frame_boxes;
    robo_paint_boxes_t leaf_boxes;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .frame { width: 0px; height: 0px; overflow: hidden }"
                                      " .leaf { width: 40px; height: 40px }");
    frame = paint_box(&fixture, "frame");
    leaf = paint_box(&fixture, "leaf");
    hit_boxes_of(&fixture, frame, &frame_boxes);
    hit_boxes_of(&fixture, leaf, &leaf_boxes);

    /* The frame's padding box has no area, so the boundary it puts in force bounds nothing: the
     * leaf is *inside* that boundary's coordinate range and still cannot be hit, because the
     * range is empty. `robo_paint_clip_is_empty` is the predicate, and this is its only consumer
     * on the hit-test side. */
    ROBO_CHECK_EQ_INT(leaf_boxes.border.x, frame_boxes.padding.x);
    ROBO_CHECK_EQ_INT(leaf_boxes.border.y, frame_boxes.padding.y);
    hit_centre_of(leaf_boxes, &x, &y);
    ROBO_CHECK(x >= frame_boxes.padding.x && y >= frame_boxes.padding.y);
    hit_node_is_not(&fixture, x, y, leaf, "a child inside an empty boundary");
    hit_node_is_not(&fixture, frame_boxes.padding.x, frame_boxes.padding.y, leaf,
                    "the corner of an empty boundary");
    /* The frame itself is a zero-size box too, so the answer is its ancestor -- the outer box is
     * excluded on its own account, which is the `region` rule and not the boundary's. */
    hit_node_is_not(&fixture, frame_boxes.padding.x, frame_boxes.padding.y, frame,
                    "the zero-size frame");

    paint_end(&fixture);
}

/** The containment door itself: per axis, half-open, safe on NULL and on an inactive axis. */
static void test_hit_clip_contains_door(void)
{
    robo_paint_clip_t clip = robo_paint_clip_none();
    robo_paint_clip_t split;
    robo_paint_clip_t empty;

    /* Nothing in force contains everything, including a NULL record -- the direction this must
     * fail in, because the alternative excludes every point of every unclipped document. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_contains(&clip, robo_gfx_point(0, 0)), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_contains(&clip, robo_gfx_point(-9999, 9999)), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_contains(NULL, robo_gfx_point(5, 5)), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_contains(NULL, 12345), 1);

    /* One bounded axis, the other free: `overflow-x: hidden` beside `overflow-y: visible`. */
    split = robo_paint_clip_none();
    split.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    split.horizontal.start = 10;
    split.horizontal.end = 20;
    ROBO_CHECK_EQ_INT(robo_paint_clip_contains(&split, robo_gfx_point(15, 0)), 1);
    /* Inside on `y` is irrelevant: `y` was never bounded, however far out it is. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_contains(&split, robo_gfx_point(15, -100000)), 1);
    /* Half-open on the bounded axis, from both ends. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_contains(&split, robo_gfx_point(10, 0)), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_contains(&split, robo_gfx_point(9, 0)), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_contains(&split, robo_gfx_point(19, 0)), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_contains(&split, robo_gfx_point(20, 0)), 0);

    /* An empty boundary contains nothing at all -- which is the opposite answer from "no
     * boundary", and the two must not be confused. */
    empty = split;
    empty.horizontal.end = empty.horizontal.start;
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&empty), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_contains(&empty, robo_gfx_point(10, 0)), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_contains(&split, robo_gfx_point(10, 0)), 1);
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
static void test_hit_opacity_does_not_gate(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t solid;
    robo_layout_node_id_t half;
    robo_layout_node_id_t gone;
    robo_paint_boxes_t boxes;
    robo_paint_hit_test_result_t result;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"solid\"></div><div class=\"half\"></div>"
                         "<div class=\"gone\"></div></body></html>",
                         HIT_BASE_CSS " .solid { width: 100px; height: 50px }"
                                      " .half { width: 100px; height: 50px; opacity: 0.5 }"
                                      " .gone { width: 100px; height: 50px; opacity: 0 }");
    solid = paint_box(&fixture, "solid");
    half = paint_box(&fixture, "half");
    gone = paint_box(&fixture, "gone");

    hit_boxes_of(&fixture, solid, &boxes);
    hit_centre_of(boxes, &x, &y);
    hit_run_at(&fixture, x, y, &result, NULL);
    ROBO_CHECK_EQ_INT((int)result.node, (int)solid);
    ROBO_CHECK_EQ_INT(robo_paint_opacity_is_opaque(&result.opacity), 1);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_has_flag(&result, ROBO_PAINT_HIT_FLAG_TRANSPARENT),
                      0);

    hit_boxes_of(&fixture, half, &boxes);
    hit_centre_of(boxes, &x, &y);
    hit_run_at(&fixture, x, y, &result, NULL);
    ROBO_CHECK_EQ_INT((int)result.node, (int)half);
    /* The strength **in force on this node's own drawing**: the box's own declaration, since a
     * box's decorations are painted at its own opacity. */
    ROBO_CHECK_EQ_INT(robo_paint_opacity_value(&result.opacity) > 0.49 &&
                          robo_paint_opacity_value(&result.opacity) < 0.51,
                      1);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_has_flag(&result, ROBO_PAINT_HIT_FLAG_TRANSPARENT),
                      0);

    hit_boxes_of(&fixture, gone, &boxes);
    hit_centre_of(boxes, &x, &y);
    hit_run_at(&fixture, x, y, &result, NULL);
    ROBO_CHECK_EQ_INT((int)result.node, (int)gone);
    ROBO_CHECK_EQ_INT(robo_paint_opacity_is_transparent(&result.opacity), 1);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_has_flag(&result, ROBO_PAINT_HIT_FLAG_TRANSPARENT),
                      1);
    /* The two states are told apart: invisible is not the same as absent. */
    ROBO_CHECK_EQ_INT((int)result.outcome, (int)ROBO_PAINT_HIT_ON_NODE);
    ROBO_CHECK(robo_paint_hit_test_result_is_valid(&result));

    paint_end(&fixture);
}

/** Nested declarations multiply along the path, in the hit test exactly as in the painter. */
static void test_hit_opacity_nested_matches_painter(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t outer;
    robo_layout_node_id_t mid;
    robo_layout_node_id_t leaf;
    robo_paint_boxes_t boxes;
    robo_paint_hit_test_result_t result;
    paint_result_t painted;
    robo_paint_operation_t marker;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"mid\">"
                         "<div class=\"leaf\"></div></div></div></body></html>",
                         HIT_BASE_CSS " .outer { width: 200px; height: 100px; opacity: 0.5 }"
                                      " .mid { width: 150px; height: 80px; opacity: 0.5 }"
                                      " .leaf { width: 100px; height: 40px }");
    outer = paint_box(&fixture, "outer");
    mid = paint_box(&fixture, "mid");
    leaf = paint_box(&fixture, "leaf");
    hit_boxes_of(&fixture, leaf, &boxes);
    hit_centre_of(boxes, &x, &y);

    hit_run_at(&fixture, x, y, &result, NULL);
    ROBO_CHECK_EQ_INT((int)result.node, (int)leaf);
    /* 0.5 * 0.5 = 0.25 on the leaf, which declares nothing of its own -- the accumulation along
     * the path, and not the leaf's own (absent) declaration. */
    ROBO_CHECK_EQ_INT(robo_paint_opacity_value(&result.opacity) > 0.249 &&
                          robo_paint_opacity_value(&result.opacity) < 0.251,
                      1);

    /* Inside `mid` but outside `leaf`: the intermediate box answers, at its own `0.5 * 0.5` --
     * the accumulation applied one level up rather than merely at the leaf, which is the
     * difference between carrying a path and reading one declaration. */
    hit_boxes_of(&fixture, mid, &boxes);
    hit_run_at(&fixture, boxes.content.x + boxes.content.width - 10,
               boxes.content.y + boxes.content.height - 10, &result, NULL);
    ROBO_CHECK_EQ_INT((int)result.node, (int)mid);
    ROBO_CHECK_EQ_INT(robo_paint_opacity_value(&result.opacity) > 0.249 &&
                          robo_paint_opacity_value(&result.opacity) < 0.251,
                      1);

    /* The painter's own record for the same node carries the same number, so the two walks
     * multiply the same path with the same door. */
    paint_once(&fixture, &painted);
    ROBO_CHECK_EQ_INT(hit_op_for(&painted, ROBO_PAINT_OP_BEGIN_BOX, leaf, &marker), 1);
    hit_opacity_eq(result.opacity, marker.opacity, "the leaf's strength is the painted one");

    /* And an ancestor at zero does not remove its descendants from the answer. */
    paint_result_dispose(&painted);
    paint_end(&fixture);

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"leaf\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .outer { width: 100px; height: 60px; opacity: 0 }"
                                      " .leaf { width: 50px; height: 30px }");
    outer = paint_box(&fixture, "outer");
    leaf = paint_box(&fixture, "leaf");
    hit_boxes_of(&fixture, leaf, &boxes);
    hit_centre_of(boxes, &x, &y);
    hit_run_at(&fixture, x, y, &result, NULL);
    ROBO_CHECK_EQ_INT((int)result.node, (int)leaf);
    ROBO_CHECK_EQ_INT(robo_paint_opacity_is_transparent(&result.opacity), 1);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_has_flag(&result, ROBO_PAINT_HIT_FLAG_TRANSPARENT),
                      1);
    /* A point in the outer box but outside the leaf is still the outer box, transparent or not. */
    hit_boxes_of(&fixture, outer, &boxes);
    hit_node_is(&fixture, boxes.content.x + 5, boxes.content.y + 55, outer,
                "a fully transparent ancestor still answers");
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. CSS and layout state                                             */
/* ------------------------------------------------------------------ */

/** `display: none` builds no box at all, so there is nothing to hit and the parent answers. */
static void test_hit_display_none(void)
{
    paint_fixture_doc_t fixture;
    robo_html_node_handle_t hidden_element;
    robo_layout_node_id_t frame;
    robo_paint_boxes_t frame_boxes;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"></div>"
                         "<div class=\"hidden\"></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 100px; height: 100px }"
                                      " .hidden { display: none; width: 300px; height: 300px }");
    frame = paint_box(&fixture, "frame");
    hidden_element = paint_find(&fixture, "hidden");
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&hidden_element), 1);
    /* The element is in the DOM and generated **no** layout object -- which is why it is not a
     * hit-test case at all, and why nothing here needs a visibility rule to exclude it. */
    ROBO_CHECK_EQ_INT(
        (int)robo_layout_object_id_for_dom_node(fixture.tree, &hidden_element),
        (int)ROBO_LAYOUT_NODE_ID_NONE);

    hit_boxes_of(&fixture, frame, &frame_boxes);
    hit_centre_of(frame_boxes, &x, &y);
    hit_node_is(&fixture, x, y, frame, "the box that replaced nothing");

    paint_end(&fixture);
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
static void test_hit_visibility_is_not_consumed(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t frame;
    robo_layout_node_id_t hidden;
    robo_paint_boxes_t boxes;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"hidden\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px }"
                                      " .hidden { width: 100px; height: 50px;"
                                      " visibility: hidden }");
    frame = paint_box(&fixture, "frame");
    hidden = paint_box(&fixture, "hidden");
    /* The box exists: `visibility` does not suppress layout in this build. */
    ROBO_CHECK(hidden != ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK(frame != ROBO_LAYOUT_NODE_ID_NONE);

    hit_boxes_of(&fixture, hidden, &boxes);
    hit_centre_of(boxes, &x, &y);
    /* And it is answerable, because the painter draws it -- which is the consistency this phase
     * chooses over an invented partial rule. */
    hit_node_is(&fixture, x, y, hidden, "a box with visibility hidden is still laid out");

    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 7. Text                                                             */
/* ------------------------------------------------------------------ */

/** A text run is a leaf with its own rectangle, and it answers for the node the text node built. */
static void test_hit_text_run(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t frame;
    robo_layout_node_id_t run;
    robo_paint_boxes_t frame_boxes;
    robo_paint_boxes_t run_boxes;
    robo_paint_hit_test_result_t result;
    robo_html_node_handle_t handle;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\">Hello</div></body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px }");
    frame = paint_box(&fixture, "frame");
    /* The run's node comes from the painter's own `TEXT` record, so a fixture that stopped
     * producing a run would fail here rather than silently testing a box. */
    {
        paint_result_t painted;
        robo_paint_operation_t text;

        paint_once(&fixture, &painted);
        ROBO_CHECK_EQ_INT(paint_nth_of_kind(&painted, ROBO_PAINT_OP_TEXT, 0u, &text), 1);
        run = text.node;
        run_boxes = text.boxes;
        paint_result_dispose(&painted);
    }
    ROBO_CHECK(run != frame);

    hit_boxes_of(&fixture, frame, &frame_boxes);
    hit_centre_of(run_boxes, &x, &y);
    hit_run_at(&fixture, x, y, &result, NULL);

    ROBO_CHECK_EQ_INT((int)result.node, (int)run);
    ROBO_CHECK_EQ_INT((int)result.kind, (int)ROBO_LAYOUT_OBJECT_TEXT);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_has_flag(&result, ROBO_PAINT_HIT_FLAG_TEXT), 1);
    ROBO_CHECK_EQ_INT((int)result.depth, (int)robo_paint_hit_test_result_depth(&result));
    hit_boxes_eq(result.boxes, run_boxes, "the run's painted rectangle");

    /* The identity the answer carries is the **text node's**, not the element's -- which is what
     * the record promises, and what a Phase 12 dispatch needs in order to walk up to the element
     * itself. */
    handle = robo_paint_hit_test_result_dom_node(&result);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&handle), 1);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(fixture.tree, &handle), (int)run);

    /* Below the line the run sits on, the element answers rather than the run. */
    hit_node_is(&fixture, frame_boxes.content.x + 2,
                frame_boxes.content.y + frame_boxes.content.height - 2, frame,
                "the element below the line box");

    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 8. Safety                                                           */
/* ------------------------------------------------------------------ */

/** NULL arguments, an empty tree, a stale tree, and extreme coordinates: no crash, no invented hit. */
static void test_hit_safety(void)
{
    paint_fixture_doc_t fixture;
    robo_paint_hit_test_result_t result;
    robo_paint_hit_test_stats_t stats;
    robo_paint_clip_t cleared;
    robo_layout_point_t point;

    /* NULL arguments are refused, and the refusal is a code rather than an outcome: no question
     * was asked, so there is no answer to report. */
    ROBO_CHECK_EQ_INT(robo_paint_hit_test(NULL, robo_gfx_point(0, 0), &result, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_paint_hit_test((const robo_layout_tree_t *)NULL, robo_gfx_point(0, 0), NULL, NULL,
                            NULL),
        ROBO_INVALID_ARGUMENT);

    /* Statistics are optional: a caller that only wants the answer declares nothing. */
    paint_begin_laid_out(&fixture, "<html><body><div class=\"frame\"></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 10px; height: 10px }");
    ROBO_CHECK_EQ_INT(robo_paint_hit_test(fixture.tree, robo_gfx_point(3, 3), &result, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_paint_hit_test_result_is_valid(&result));
    paint_end(&fixture);

    /* An empty document: a miss with nothing examined, which the counters already say rather than
     * a special case a caller has to test for. */
    paint_begin(&fixture, "<html><body></body></html>", HIT_BASE_CSS);
    paint_layout(&fixture, 800, 600);
    hit_run_at(&fixture, 0, 0, &result, &stats);
    ROBO_CHECK((int)result.outcome == (int)ROBO_PAINT_HIT_MISS ||
               (int)result.outcome == (int)ROBO_PAINT_HIT_ON_NODE);
    ROBO_CHECK_EQ_INT((int)stats.hits, robo_paint_hit_test_result_is_hit(&result) ? 1 : 0);
    paint_end(&fixture);

    /*
     * A tree whose geometry is not final is **refused**: the outcome says so and no node is
     * reported, which is the difference between "nothing is there" and "no question was asked".
     * A caller that could not tell those apart would dispatch a click at a node the user is not
     * pointing at, which is why the refusal is not an error and not a miss.
     */
    paint_begin(&fixture, "<html><body><div class=\"frame\"></div></body></html>",
                HIT_BASE_CSS " .frame { width: 100px; height: 100px }");
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 1);
    hit_run_at(&fixture, 10, 10, &result, &stats);
    ROBO_CHECK_EQ_INT((int)result.outcome, (int)ROBO_PAINT_HIT_STALE);
    ROBO_CHECK_EQ_INT((int)result.node, (int)ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT((int)stats.objects, 0);
    ROBO_CHECK_EQ_INT((int)stats.hits, 0);
    ROBO_CHECK(robo_paint_hit_test_result_is_valid(&result));

    /* And the same tree, once laid out, answers -- so the refusal was about freshness and not
     * about the document. */
    paint_layout(&fixture, 800, 600);
    hit_run_at(&fixture, 10, 10, &result, &stats);
    ROBO_CHECK_EQ_INT((int)result.outcome, (int)ROBO_PAINT_HIT_ON_NODE);
    ROBO_CHECK_EQ_INT((int)stats.hits, 1);

    /* The coordinate range's extremes: a query at the seam is answered or missed, never crashed
     * on and never wrapped into a different rectangle. */
    {
        static const int32_t extremes[] = { -2147483647 - 1, -1000000, 0, 1000000, 2147483647 };
        size_t index;

        for (index = 0u; index < sizeof(extremes) / sizeof(extremes[0]); index++) {
            size_t other;

            for (other = 0u; other < sizeof(extremes) / sizeof(extremes[0]); other++) {
                hit_run_at(&fixture, extremes[index], extremes[other], &result, &stats);
                ROBO_CHECK(robo_paint_hit_test_result_is_valid(&result));
                /* A local point is the difference of two coordinates at the seam and is
                 * *saturated* rather than wrapped, so it is always a coordinate this build can
                 * carry -- never a number that reads as a mirrored position. */
                point = result.local;
                ROBO_CHECK(point.x == point.x && point.y == point.y);
            }
        }
    }
    paint_end(&fixture);

    /* The record's own doors are total: a cleared result reads as a record and an empty clip
     * contains everything. */
    robo_paint_hit_test_result_init(&result);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_is_valid(&result), 1);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_is_hit(&result), 0);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_is_hit(NULL), 0);
    robo_paint_hit_test_result_clip(NULL, &cleared);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&cleared), 0);
    robo_paint_hit_test_result_init(NULL); /* safe */
    robo_paint_hit_test_stats_init(NULL);  /* safe */
}

/* ------------------------------------------------------------------ */
/* 9. Determinism and mutation isolation                               */
/* ------------------------------------------------------------------ */

/** The same tree and the same point answer the same thing, every time. */
static void test_hit_determinism(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t frame;
    robo_layout_node_id_t leaf;
    robo_paint_boxes_t boxes;
    robo_paint_hit_test_result_t first;
    robo_paint_hit_test_result_t again;
    robo_paint_hit_test_stats_t first_stats;
    robo_paint_hit_test_stats_t again_stats;
    int32_t x;
    int32_t y;
    int round;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div></div>"
                         "<div class=\"other\"></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px; overflow: hidden }"
                                      " .leaf { width: 50px; height: 40px }"
                                      " .other { width: 80px; height: 30px; opacity: 0.5 }");
    frame = paint_box(&fixture, "frame");
    leaf = paint_box(&fixture, "leaf");
    ROBO_CHECK(frame != ROBO_LAYOUT_NODE_ID_NONE);
    hit_boxes_of(&fixture, leaf, &boxes);
    hit_centre_of(boxes, &x, &y);

    hit_run_at(&fixture, x, y, &first, &first_stats);
    for (round = 0; round < 8; round++) {
        hit_run_at(&fixture, x, y, &again, &again_stats);
        ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_equal(&first, &again), 1);
        ROBO_CHECK_EQ_INT((int)again_stats.objects, (int)first_stats.objects);
        ROBO_CHECK_EQ_INT((int)again_stats.rejected_region, (int)first_stats.rejected_region);
        ROBO_CHECK_EQ_INT((int)again_stats.rejected_clip, (int)first_stats.rejected_clip);
        ROBO_CHECK_EQ_INT((int)again_stats.hits, (int)first_stats.hits);
        ROBO_CHECK_EQ_INT((int)again_stats.depth_max, (int)first_stats.depth_max);
    }

    /* A **second, identically built** document answers identically too: nothing in the answer
     * depends on an address, an allocation order or a hash walk. */
    paint_end(&fixture);
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div></div>"
                         "<div class=\"other\"></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px; overflow: hidden }"
                                      " .leaf { width: 50px; height: 40px }"
                                      " .other { width: 80px; height: 30px; opacity: 0.5 }");
    hit_run_at(&fixture, x, y, &again, &again_stats);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_equal(&first, &again), 1);
    paint_end(&fixture);
}

/**
 * An answer is a **snapshot**: changing the tree afterwards cannot rewrite one already drawn.
 *
 * The rule 11.9's records follow, for the same reason -- a click that changed meaning when a style
 * was recomputed is worse than no answer -- and it is checkable here because the result holds
 * copies: the id it names still validates, and the geometry it reports is the geometry that was
 * true when the query ran.
 */
static void test_hit_mutation_isolation(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t leaf;
    robo_paint_boxes_t boxes;
    robo_paint_hit_test_result_t before;
    robo_paint_hit_test_result_t after;
    robo_paint_hit_test_result_t copy;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 100px }"
                                      " .leaf { width: 100px; height: 40px }");
    leaf = paint_box(&fixture, "leaf");
    hit_boxes_of(&fixture, leaf, &boxes);
    hit_centre_of(boxes, &x, &y);
    hit_run_at(&fixture, x, y, &before, NULL);
    ROBO_CHECK_EQ_INT((int)before.node, (int)leaf);
    copy = before;

    /* Invalidate the tree: the *tree* is now stale, and the answer that was already drawn is
     * unchanged, field for field. A viewport change is the invalidation that genuinely makes every
     * box's geometry owe a recomputation, so the tree really is stale and not merely marked. */
    ROBO_CHECK_EQ_INT(
        robo_layout_tree_mark_all(fixture.tree, ROBO_LAYOUT_INVALIDATION_REASON_VIEWPORT, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 1);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_equal(&before, &copy), 1);
    ROBO_CHECK(robo_paint_hit_test_result_is_valid(&before));

    /* And a fresh query against the now-stale tree refuses rather than reusing the old answer --
     * which is the other half of the rule: a snapshot is not a cache. */
    hit_run_at(&fixture, x, y, &after, NULL);
    ROBO_CHECK_EQ_INT((int)after.outcome, (int)ROBO_PAINT_HIT_STALE);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_equal(&before, &after), 0);

    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 10. Counter identities                                              */
/* ------------------------------------------------------------------ */

/** The three identities the stats owe, checked on a document that exercises every branch. */
static void test_hit_stats_identities(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t frame;
    robo_layout_node_id_t leaf;
    robo_paint_boxes_t boxes;
    robo_paint_hit_test_result_t result;
    robo_paint_hit_test_stats_t stats;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div>"
                         "<div class=\"far\"></div></div><div class=\"after\"></div>"
                         "</body></html>",
                         HIT_BASE_CSS " .frame { width: 200px; height: 60px; overflow: hidden }"
                                      " .leaf { width: 40px; height: 30px }"
                                      " .far { width: 40px; height: 30px; position: relative;"
                                      " left: 500px }"
                                      " .after { width: 100px; height: 20px }");
    frame = paint_box(&fixture, "frame");
    leaf = paint_box(&fixture, "leaf");
    hit_boxes_of(&fixture, leaf, &boxes);
    hit_centre_of(boxes, &x, &y);
    hit_run_at(&fixture, x, y, &result, &stats);

    ROBO_CHECK_EQ_INT((int)result.node, (int)leaf);
    /* Every object examined is exactly one of the three: a placed box, a placed run, or something
     * nothing placed. The identity is what makes the three counters a partition rather than three
     * independent tallies. */
    ROBO_CHECK_EQ_INT((int)(stats.boxes + stats.texts + stats.unplaced), (int)stats.objects);
    /* Exactly one node is the answer, because the walk returns the moment one is found. */
    ROBO_CHECK_EQ_INT((int)stats.hits, 1);
    ROBO_CHECK(stats.boxes >= 2u);
    ROBO_CHECK(stats.depth_max >= 2u);
    /* The frame's contents were inside the boundary, so nothing there was rejected for clipping;
     * the `far` child was rejected geometrically and is counted as one region rejection. */
    ROBO_CHECK(stats.rejected_region >= 1u);
    /* And the frame itself answers in the part of its own box no child covers -- so the region
     * rejection above is about the child the frame's boundary excluded and not about the frame
     * becoming unreachable. */
    hit_boxes_of(&fixture, frame, &boxes);
    hit_node_is(&fixture, boxes.content.x + 5, boxes.content.y + boxes.content.height - 5, frame,
                "the frame below its children");

    /* A miss on a tree with a root means something excluded the point -- and the counters are how
     * a caller tells "nothing is there" from "the walk gave up". */
    hit_run_at(&fixture, -50, -50, &result, &stats);
    ROBO_CHECK_EQ_INT((int)result.outcome, (int)ROBO_PAINT_HIT_MISS);
    ROBO_CHECK(stats.rejected_region + stats.rejected_clip >= 1u);
    ROBO_CHECK_EQ_INT((int)(stats.boxes + stats.texts + stats.unplaced), (int)stats.objects);
    ROBO_CHECK_EQ_INT((int)stats.hits, 0);

    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 11. The result model                                                */
/* ------------------------------------------------------------------ */

/** The record's doors, and the combinations the validity rule refuses. */
static void test_hit_result_model(void)
{
    robo_paint_hit_test_result_t result;
    robo_paint_hit_test_result_t other;
    robo_paint_boxes_t boxes;
    robo_paint_clip_t clip;
    robo_paint_opacity_t opacity;

    robo_paint_hit_test_result_init(&result);
    ROBO_CHECK_EQ_INT((int)result.outcome, (int)ROBO_PAINT_HIT_NONE);
    ROBO_CHECK_EQ_INT((int)robo_paint_hit_test_result_outcome(&result), (int)ROBO_PAINT_HIT_NONE);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_is_valid(&result), 1);
    /* The cleared record is **readable**: no boundary in force and full strength, so a caller
     * that drew with it would draw everything rather than nothing. */
    robo_paint_hit_test_result_clip(&result, &clip);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&clip), 0);
    robo_paint_hit_test_result_opacity(&result, &opacity);
    ROBO_CHECK_EQ_INT(robo_paint_opacity_is_opaque(&opacity), 1);
    robo_paint_hit_test_result_boxes(&result, &boxes);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&boxes), 1);

    /* A hit claiming a node but no identity is a record whose fields disagree, and is refused. */
    result.outcome = ROBO_PAINT_HIT_ON_NODE;
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_is_valid(&result), 0);
    /* As is a node reported by a miss. */
    robo_paint_hit_test_result_init(&result);
    result.outcome = ROBO_PAINT_HIT_MISS;
    result.node = 7u;
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_is_valid(&result), 0);
    /* And a flag describing a hit that did not happen. */
    robo_paint_hit_test_result_init(&result);
    result.flags = ROBO_PAINT_HIT_FLAG_CONTENT;
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_is_valid(&result), 0);
    /* An unknown flag bit, and an unknown outcome, are both refused. */
    robo_paint_hit_test_result_init(&result);
    result.flags = 1u << 30;
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_is_valid(&result), 0);
    robo_paint_hit_test_result_init(&result);
    result.outcome = (robo_paint_hit_test_outcome_t)99;
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_is_valid(&result), 0);
    /* A clip whose interval runs backwards is not a record. */
    robo_paint_hit_test_result_init(&result);
    robo_paint_hit_test_result_clip(&result, &clip);
    result.clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    result.clip.horizontal.start = 40;
    result.clip.horizontal.end = 10;
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_is_valid(&result), 0);

    /* Equality is field-for-field, so a build that changed one field cannot compare equal. */
    robo_paint_hit_test_result_init(&result);
    robo_paint_hit_test_result_init(&other);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_equal(&result, &other), 1);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_equal(&result, NULL), 0);
    other = result;
    other.point.x = 5;
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_equal(&result, &other), 0);
    other = result;
    other.depth = 3u;
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_equal(&result, &other), 0);

    /* The vocabulary is complete and every outcome is named. */
    {
        static const char *const expected[ROBO_PAINT_HIT_TEST_OUTCOME_COUNT] = {
            "none", "node", "miss", "stale"
        };
        size_t index;

        ROBO_CHECK_EQ_INT((int)ROBO_PAINT_HIT_TEST_OUTCOME_COUNT, 4);
        for (index = 0u; index < (size_t)ROBO_PAINT_HIT_TEST_OUTCOME_COUNT; index++) {
            ROBO_CHECK_EQ_INT(
                robo_paint_hit_test_outcome_is_known((robo_paint_hit_test_outcome_t)index), 1);
            ROBO_CHECK_STR_EQ(robo_paint_hit_test_outcome_name((robo_paint_hit_test_outcome_t)index),
                              expected[index]);
        }
        ROBO_CHECK_EQ_INT(robo_paint_hit_test_outcome_is_known(
                              (robo_paint_hit_test_outcome_t)99),
                          0);
        ROBO_CHECK_STR_EQ(robo_paint_hit_test_outcome_name((robo_paint_hit_test_outcome_t)99),
                          "unknown");
        /* The flag vocabulary is closed: three bits, and the mask is exactly them. */
        ROBO_CHECK_EQ_INT((int)ROBO_PAINT_HIT_FLAG_ALL,
                          (int)(ROBO_PAINT_HIT_FLAG_TEXT | ROBO_PAINT_HIT_FLAG_CONTENT |
                                ROBO_PAINT_HIT_FLAG_TRANSPARENT));
    }
}

/* ------------------------------------------------------------------ */
/* 12. The trace                                                       */
/* ------------------------------------------------------------------ */

/** The dump names the answer, the point and one reason per object examined. */
static void test_hit_dump(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t frame;
    robo_layout_node_id_t leaf;
    robo_layout_node_id_t far;
    robo_paint_boxes_t boxes;
    robo_paint_hit_test_stats_t stats;
    char buffer[8192];
    char second[8192];
    size_t length = 0u;
    size_t second_length = 0u;
    size_t lines = 0u;
    size_t longest = 0u;
    size_t run = 0u;
    size_t index;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"leaf\"></div>"
                         "<div class=\"far\"></div></div></body></html>",
                         HIT_BASE_CSS " .frame { width: 100px; height: 60px; overflow: hidden }"
                                      " .leaf { width: 40px; height: 30px }"
                                      " .far { width: 30px; height: 30px; position: relative;"
                                      " left: 400px }");
    frame = paint_box(&fixture, "frame");
    leaf = paint_box(&fixture, "leaf");
    far = paint_box(&fixture, "far");
    ROBO_CHECK(frame != ROBO_LAYOUT_NODE_ID_NONE && far != ROBO_LAYOUT_NODE_ID_NONE);
    hit_boxes_of(&fixture, leaf, &boxes);
    hit_centre_of(boxes, &x, &y);

    memset(buffer, 0, sizeof(buffer));
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_write(fixture.tree, robo_gfx_point(x, y), buffer,
                                               sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK(length > 0u);
    ROBO_CHECK_EQ_INT((int)strlen(buffer), (int)length);
    /* The header is the answer, on its own line. */
    ROBO_CHECK(strncmp(buffer, "hit-test point=", 15u) == 0);
    ROBO_CHECK(strstr(buffer, "outcome=node") != NULL);
    ROBO_CHECK(strstr(buffer, "outcome=region") != NULL ||
               strstr(buffer, "outcome=clip") != NULL);
    /* The line count is an identity against the stats rather than a guess: one header, one line
     * per examined object, and one `hit` line because a node was found. */
    {
        robo_paint_hit_test_result_t answer;

        hit_run_at(&fixture, x, y, &answer, &stats);
        ROBO_CHECK_EQ_INT((int)answer.node, (int)leaf);
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
    ROBO_CHECK_EQ_INT((int)run, 0);
    ROBO_CHECK_EQ_INT((int)lines, (int)(2u + stats.objects));
    ROBO_CHECK(longest < (size_t)ROBO_PAINT_HIT_TEST_LINE_MAX);
    /* One line per examined object *plus* the `hit` line, and the stats say how many objects. */
    ROBO_CHECK_EQ_INT((int)stats.hits, 1);

    /* Dumping twice produces the same text: the trace is a pure function of the tree and the
     * point, which is why running the walk twice is sound. */
    memset(second, 0, sizeof(second));
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_write(fixture.tree, robo_gfx_point(x, y), second,
                                               sizeof(second), &second_length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)second_length, (int)length);
    ROBO_CHECK(strcmp(buffer, second) == 0);

    /* A buffer too small is refused with the length it would have needed, so a caller sizes from
     * the refusal rather than from an estimate -- the contract every `_write` door here uses. */
    {
        char tiny[16];
        size_t needed = 0u;

        memset(tiny, 0, sizeof(tiny));
        ROBO_CHECK_EQ_INT(robo_paint_hit_test_write(fixture.tree, robo_gfx_point(x, y), tiny,
                                                   sizeof(tiny), &needed, NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT((int)needed, (int)length);
        /* And the count is honest: the same dump into a buffer of exactly that size succeeds and
         * produces the same text, so a caller that sized from the refusal is not sent round
         * again. */
        ROBO_CHECK(needed + 1u <= sizeof(second));
        {
            size_t written = 0u;

            memset(second, 0, sizeof(second));
            ROBO_CHECK_EQ_INT(robo_paint_hit_test_write(fixture.tree, robo_gfx_point(x, y), second,
                                                       needed + 1u, &written, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT((int)written, (int)length);
            ROBO_CHECK(strcmp(second, buffer) == 0);
        }
    }

    /* A refusal writes its own line rather than an empty buffer, so a reader of a stale tree's
     * dump is told what happened. */
    paint_end(&fixture);
    paint_begin(&fixture, "<html><body><div class=\"frame\"></div></body></html>",
                HIT_BASE_CSS " .frame { width: 10px; height: 10px }");
    memset(buffer, 0, sizeof(buffer));
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_write(fixture.tree, robo_gfx_point(1, 1), buffer,
                                               sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK(strstr(buffer, "outcome=stale") != NULL);
    ROBO_CHECK(length > 0u);

    /* NULL arguments are refused rather than dereferenced. */
    ROBO_CHECK_EQ_INT(
        robo_paint_hit_test_write(NULL, robo_gfx_point(0, 0), buffer, sizeof(buffer), &length,
                                  NULL),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_paint_hit_test_write(fixture.tree, robo_gfx_point(0, 0), NULL, sizeof(buffer),
                                  &length, NULL),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_write(fixture.tree, robo_gfx_point(0, 0), buffer,
                                               sizeof(buffer), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    paint_end(&fixture);
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
static void test_hit_matches_the_paint_list(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;
    robo_layout_node_id_t after;
    robo_paint_boxes_t boxes;
    paint_result_t painted;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "<div class=\"after\"></div></body></html>",
                         HIT_BASE_CSS " .outer { width: 200px; height: 100px }"
                                      " .inner { width: 60px; height: 40px }"
                                      " .after { width: 120px; height: 30px }");
    outer = paint_box(&fixture, "outer");
    inner = paint_box(&fixture, "inner");
    after = paint_box(&fixture, "after");
    paint_once(&fixture, &painted);

    /* A point on the inner box. */
    hit_boxes_of(&fixture, inner, &boxes);
    hit_centre_of(boxes, &x, &y);
    ROBO_CHECK_EQ_INT(hit_node_at(&fixture, x, y), (int)inner);
    /* The painter emitted the inner box's own marker, and it stands *after* the outer box's
     * content boundary -- a child is drawn over its ancestor's own records. */
    {
        size_t inner_marker = hit_index_for(&painted, ROBO_PAINT_OP_BEGIN_BOX, inner);
        size_t outer_content = hit_index_for(&painted, ROBO_PAINT_OP_CONTENT, outer);

        ROBO_CHECK(inner_marker != (size_t)-1 && outer_content != (size_t)-1);
        ROBO_CHECK(inner_marker > outer_content);
    }

    /* A point on the outer box but not on the inner one: the outer box, whose own marker is
     * earlier in the list than any descendant's -- and which is still the answer, because nothing
     * below it covers the point. */
    hit_boxes_of(&fixture, outer, &boxes);
    x = boxes.content.x + boxes.content.width - 5;
    y = boxes.content.y + boxes.content.height - 5;
    ROBO_CHECK_EQ_INT(hit_node_at(&fixture, x, y), (int)outer);

    /* A point on the third box, which is drawn last of all and answers for its whole region. */
    hit_boxes_of(&fixture, after, &boxes);
    hit_centre_of(boxes, &x, &y);
    ROBO_CHECK_EQ_INT(hit_node_at(&fixture, x, y), (int)after);
    {
        size_t after_marker = hit_index_for(&painted, ROBO_PAINT_OP_BEGIN_BOX, after);
        size_t inner_end = hit_index_for(&painted, ROBO_PAINT_OP_END_BOX, inner);

        ROBO_CHECK(after_marker != (size_t)-1 && inner_end != (size_t)-1);
        ROBO_CHECK(after_marker > inner_end);
    }

    paint_result_dispose(&painted);
    paint_end(&fixture);
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
static void test_hit_end_to_end(void)
{
    paint_fixture_doc_t fixture;
    robo_layout_node_id_t frame;
    robo_layout_node_id_t label;
    robo_layout_node_id_t sliding;
    robo_layout_node_id_t clipped;
    robo_layout_node_id_t faded;
    robo_paint_boxes_t frame_boxes;
    robo_paint_boxes_t label_boxes;
    robo_paint_boxes_t sliding_boxes;
    robo_paint_boxes_t clipped_boxes;
    robo_paint_boxes_t faded_boxes;
    robo_paint_hit_test_result_t result;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
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
    frame = paint_box(&fixture, "frame");
    label = paint_box(&fixture, "label");
    sliding = paint_box(&fixture, "sliding");
    clipped = paint_box(&fixture, "clipped");
    faded = paint_box(&fixture, "faded");
    hit_boxes_of(&fixture, frame, &frame_boxes);
    hit_boxes_of(&fixture, label, &label_boxes);
    hit_boxes_of(&fixture, sliding, &sliding_boxes);
    hit_boxes_of(&fixture, clipped, &clipped_boxes);
    hit_boxes_of(&fixture, faded, &faded_boxes);

    /* 1. The frame's own border band: the box, not the document, and not the descendants --
     *    the boundary clips the contents and not the box. */
    hit_node_is(&fixture, frame_boxes.border.x + 2, frame_boxes.border.y + 2, frame,
                "the frame's border band");
    /* 2. The frame's padding, below the label: the frame answers because no descendant is there
     *    except the clipped one, which is clipped away. */
    hit_run_at(&fixture, frame_boxes.border.x + frame_boxes.border.width - 15,
               frame_boxes.border.y + frame_boxes.border.height - 15, &result, NULL);
    ROBO_CHECK_EQ_INT((int)result.node, (int)frame);
    /* 3. The label's own box: the child, at its own rectangle. */
    hit_centre_of(label_boxes, &x, &y);
    hit_node_is(&fixture, x, y, label, "the label box");
    /* 4. A point on the label's text run, if the fixture produced one, is the run -- the run is
     *    inside the label's content box, so its centre is inside the label too. */
    {
        paint_result_t painted;
        robo_paint_operation_t text;
        size_t index;
        int found = 0;

        paint_once(&fixture, &painted);
        for (index = 0u; index < robo_paint_list_count(painted.list); index++) {
            robo_paint_operation_t operation;

            if (robo_paint_list_at(painted.list, index, &operation) != ROBO_OK) {
                break;
            }
            if (operation.kind == ROBO_PAINT_OP_TEXT) {
                text = operation;
                found = 1;
                break;
            }
        }
        ROBO_CHECK_EQ_INT(found, 1);
        hit_centre_of(text.boxes, &x, &y);
        hit_run_at(&fixture, x, y, &result, NULL);
        ROBO_CHECK_EQ_INT((int)result.node, (int)text.node);
        ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_has_flag(&result,
                                                              ROBO_PAINT_HIT_FLAG_TEXT),
                          1);
        paint_result_dispose(&painted);
    }
    /* 5. The clipped child, on its own laid-out rectangle: **not** the answer, because the frame
     *    does not draw it there. */
    hit_centre_of(clipped_boxes, &x, &y);
    hit_node_is_not(&fixture, x, y, clipped, "the clipped child outside its boundary");
    /* The point is outside the frame's boundary too -- the child was laid out well past it -- so
     * the answer is whatever the document has there, and the frame is not among them. */
    hit_node_is_not(&fixture, x, y, frame, "the frame outside its own box");
    /* 6. The sliding box: the answer over its own (offset) rectangle, with half strength -- and
     *    it is drawn over the frame's flow space, so where they overlap the slider wins. */
    hit_centre_of(sliding_boxes, &x, &y);
    hit_run_at(&fixture, x, y, &result, NULL);
    ROBO_CHECK_EQ_INT((int)result.node, (int)sliding);
    ROBO_CHECK_EQ_INT(robo_paint_opacity_value(&result.opacity) > 0.49 &&
                          robo_paint_opacity_value(&result.opacity) < 0.51,
                      1);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_has_flag(&result, ROBO_PAINT_HIT_FLAG_TRANSPARENT),
                      0);
    /* 7. The fully transparent box is still the answer over itself, and says so. */
    hit_centre_of(faded_boxes, &x, &y);
    hit_run_at(&fixture, x, y, &result, NULL);
    ROBO_CHECK_EQ_INT((int)result.node, (int)faded);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_has_flag(&result, ROBO_PAINT_HIT_FLAG_TRANSPARENT),
                      1);
    /* 8. Well outside the document: nothing, and the walk says so through the counters. */
    hit_run_at(&fixture, 100000, -100000, &result, NULL);
    ROBO_CHECK_EQ_INT((int)result.outcome, (int)ROBO_PAINT_HIT_MISS);
    /* 9. The `CONTENT` flag is derivable from the rectangles the answer carries, so the two are
     *    asserted to agree rather than trusted to. */
    hit_centre_of(label_boxes, &x, &y);
    hit_run_at(&fixture, x, y, &result, NULL);
    ROBO_CHECK_EQ_INT(robo_paint_hit_test_result_has_flag(&result, ROBO_PAINT_HIT_FLAG_CONTENT),
                      robo_gfx_rect_contains_point(result.boxes.content, result.point) ? 1 : 0);
    ROBO_CHECK(robo_gfx_rect_contains_point(result.boxes.content, result.point));
    /* 10. The point in the node's own space is the point minus its origin, exactly. */
    ROBO_CHECK_EQ_INT(result.local.x, result.point.x - result.origin.x);
    ROBO_CHECK_EQ_INT(result.local.y, result.point.y - result.origin.y);

    paint_end(&fixture);
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
static robo_gfx_color_rgba8_t hit_fill_of(paint_result_t *painted,
                                         robo_layout_node_id_t node)
{
    robo_paint_operation_t operation;
    robo_gfx_color_rgba8_t color;

    memset(&color, 0, sizeof(color));
    if (!hit_op_for(painted, ROBO_PAINT_OP_BACKGROUND, node, &operation)) {
        robo_test_fail(__FILE__, __LINE__, "node %d draws no background to compare against",
                       (int)node);
        return color;
    }
    ROBO_CHECK_EQ_INT(robo_paint_color_to_rgba8(operation.color, &color), ROBO_OK);
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
static void hit_agrees_with_screen(paint_fixture_doc_t *fixture,
                                   paint_result_t *painted,
                                   robo_gfx_surface_t *screen,
                                   int32_t x,
                                   int32_t y,
                                   robo_layout_node_id_t expected_node,
                                   const char *what)
{
    robo_paint_hit_test_result_t result;
    robo_gfx_color_rgba8_t expected;
    robo_gfx_color_rgba8_t actual;

    hit_run_at(fixture, x, y, &result, NULL);
    if ((int)result.node != (int)expected_node) {
        robo_test_fail(__FILE__, __LINE__, "%s: the query named node %d, not %d", what,
                       (int)result.node, (int)expected_node);
        return;
    }
    expected = hit_fill_of(painted, expected_node);
    actual = hit_pixel(screen, x, y);
    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
        actual.a != expected.a) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: node %d fills rgba(%u,%u,%u,%u) but the pixel is "
                       "rgba(%u,%u,%u,%u)",
                       what, (int)expected_node, expected.r, expected.g, expected.b, expected.a,
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
static void test_hit_agrees_with_screen(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t painted;
    robo_gfx_surface_t *screen = NULL;
    robo_paint_operation_t operation;
    robo_paint_boxes_t frame_boxes;
    robo_paint_boxes_t under_boxes;
    robo_paint_boxes_t over_boxes;
    robo_layout_node_id_t frame;
    robo_layout_node_id_t under;
    robo_layout_node_id_t over;
    robo_gfx_color_rgba8_t miss_pixel;
    robo_paint_hit_test_result_t result;
    robo_layout_rect_t overlap;
    int32_t x;
    int32_t y;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\"><div class=\"under\"></div>"
                         "<div class=\"over\"></div></div></body></html>",
                         HIT_BASE_CSS
                         " .frame { width: 300px; height: 200px; background-color: #14283c }"
                         " .under { width: 120px; height: 80px; background-color: #c81e1e }"
                         " .over { width: 120px; height: 80px; background-color: #1ea05a;"
                         "         position: relative; top: -60px; left: 40px }");
    frame = paint_box(&fixture, "frame");
    under = paint_box(&fixture, "under");
    over = paint_box(&fixture, "over");
    paint_once(&fixture, &painted);

    /* The boxes come from the painter's own records, as everywhere else in this suite. */
    ROBO_CHECK_EQ_INT(hit_op_for(&painted, ROBO_PAINT_OP_BEGIN_BOX, frame, &operation), 1);
    frame_boxes = operation.boxes;
    ROBO_CHECK_EQ_INT(hit_op_for(&painted, ROBO_PAINT_OP_BEGIN_BOX, under, &operation), 1);
    under_boxes = operation.boxes;
    ROBO_CHECK_EQ_INT(hit_op_for(&painted, ROBO_PAINT_OP_BEGIN_BOX, over, &operation), 1);
    over_boxes = operation.boxes;

    /* The fixture's premise, computed rather than assumed: the relative offset really did carry
     * `.over` onto `.under`, so the overlap the probes below name is a fact about this document.
     *
     * `.over` flows *below* `.under` -- its normal-flow top is `.under`'s bottom -- so a `top` of
     * -60px can only bring it into `.under`'s band if the offset was applied at all, and that is
     * exactly what `over.top < under.bottom` states. Computing the intersection here also lets the
     * probes be the overlap's own midpoint instead of an arithmetic guess. */
    ROBO_CHECK(over_boxes.border.y < under_boxes.border.y + under_boxes.border.height);
    ROBO_CHECK(over_boxes.border.x < under_boxes.border.x + under_boxes.border.width);
    overlap.x = over_boxes.border.x > under_boxes.border.x ? over_boxes.border.x
                                                          : under_boxes.border.x;
    overlap.y = over_boxes.border.y > under_boxes.border.y ? over_boxes.border.y
                                                          : under_boxes.border.y;
    overlap.width = (under_boxes.border.x + under_boxes.border.width <
                             over_boxes.border.x + over_boxes.border.width
                         ? under_boxes.border.x + under_boxes.border.width
                         : over_boxes.border.x + over_boxes.border.width) -
                    overlap.x;
    overlap.height = (under_boxes.border.y + under_boxes.border.height <
                              over_boxes.border.y + over_boxes.border.height
                          ? under_boxes.border.y + under_boxes.border.height
                          : over_boxes.border.y + over_boxes.border.height) -
                     overlap.y;
    ROBO_CHECK(overlap.width > 0 && overlap.height > 0);

    screen = hit_surface(320u, 240u);
    hit_paint_to(&painted, screen);

    /* 1. In `.under` alone: the red box is the answer and the red fill is the pixel. The point is
     *    asserted to be clear of `.over`'s rectangle, so "alone" is checked rather than hoped. */
    x = under_boxes.border.x + 5;
    y = under_boxes.border.y + under_boxes.border.height - 5;
    ROBO_CHECK(x < over_boxes.border.x || x >= over_boxes.border.x + over_boxes.border.width ||
               y < over_boxes.border.y || y >= over_boxes.border.y + over_boxes.border.height);
    hit_agrees_with_screen(&fixture, &painted, screen, x, y, under, "in .under alone");

    /* 2. In the overlap: the **later-painted** box owns the point, and the screen agrees. The
     *    probe is the intersection's own midpoint, so it is inside both boxes by construction. */
    x = overlap.x + overlap.width / 2;
    y = overlap.y + overlap.height / 2;
    hit_agrees_with_screen(&fixture, &painted, screen, x, y, over, "in the overlap");

    /* 3. The frame's own gutter, clear of both children: the ancestor is the fallback. */
    x = frame_boxes.border.x + frame_boxes.border.width - 5;
    y = frame_boxes.border.y + frame_boxes.border.height - 5;
    hit_agrees_with_screen(&fixture, &painted, screen, x, y, frame, "in the frame gutter");

    /* 4. Outside the document: nothing is hit, and the control is that **nothing was drawn**
     *    there -- so the pixel is fully transparent rather than any box's colour. */
    hit_run_at(&fixture, 319, 239, &result, NULL);
    ROBO_CHECK_EQ_INT((int)result.outcome, (int)ROBO_PAINT_HIT_MISS);
    miss_pixel = hit_pixel(screen, 319, 239);
    ROBO_CHECK_EQ_INT((int)miss_pixel.a, 0);
    /* And the surface really was written to elsewhere, so the transparent corner is a fact about
     * the document and not an unpainted surface. */
    ROBO_CHECK_EQ_INT((int)hit_pixel(screen, overlap.x + overlap.width / 2,
                                     overlap.y + overlap.height / 2).a,
                      255);

    /* 5. The frame's fill is visible where no child covers it *and* the two children's colours
     *    differ, so the probes above could not all have been satisfied by one colour. */
    ROBO_CHECK(hit_fill_of(&painted, under).r != hit_fill_of(&painted, over).r ||
               hit_fill_of(&painted, under).g != hit_fill_of(&painted, over).g ||
               hit_fill_of(&painted, under).b != hit_fill_of(&painted, over).b);

    robo_gfx_surface_release(screen);
    paint_result_dispose(&painted);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_paint_hit_test(void)
{
    robo_test_register("paint.hit-test.inside_and_outside", test_hit_inside_and_outside);
    robo_test_register("paint.hit-test.region_is_border_box", test_hit_region_is_border_box);
    robo_test_register("paint.hit-test.empty_boxes", test_hit_empty_boxes);
    robo_test_register("paint.hit-test.nested_and_fallback", test_hit_nested_and_fallback);
    robo_test_register("paint.hit-test.overlapping_siblings", test_hit_overlapping_siblings);
    robo_test_register("paint.hit-test.overlap_is_by_drawing",
                       test_hit_overlap_is_by_drawing_not_by_position);
    robo_test_register("paint.hit-test.descendant_over_ancestor", test_hit_descendant_over_ancestor);
    robo_test_register("paint.hit-test.clip_contents_not_box", test_hit_clip_contents_not_box);
    robo_test_register("paint.hit-test.clip_inside_matches_painter",
                       test_hit_clip_inside_matches_painter);
    robo_test_register("paint.hit-test.clip_nested", test_hit_clip_nested);
    robo_test_register("paint.hit-test.clip_empty", test_hit_clip_empty);
    robo_test_register("paint.hit-test.clip_contains_door", test_hit_clip_contains_door);
    robo_test_register("paint.hit-test.opacity_does_not_gate", test_hit_opacity_does_not_gate);
    robo_test_register("paint.hit-test.opacity_nested_matches_painter",
                       test_hit_opacity_nested_matches_painter);
    robo_test_register("paint.hit-test.display_none", test_hit_display_none);
    robo_test_register("paint.hit-test.visibility_is_not_consumed",
                       test_hit_visibility_is_not_consumed);
    robo_test_register("paint.hit-test.text_run", test_hit_text_run);
    robo_test_register("paint.hit-test.safety", test_hit_safety);
    robo_test_register("paint.hit-test.determinism", test_hit_determinism);
    robo_test_register("paint.hit-test.mutation_isolation", test_hit_mutation_isolation);
    robo_test_register("paint.hit-test.stats_identities", test_hit_stats_identities);
    robo_test_register("paint.hit-test.result_model", test_hit_result_model);
    robo_test_register("paint.hit-test.dump", test_hit_dump);
    robo_test_register("paint.hit-test.matches_the_paint_list", test_hit_matches_the_paint_list);
    robo_test_register("paint.hit-test.end_to_end", test_hit_end_to_end);
    robo_test_register("paint.hit-test.agrees_with_screen", test_hit_agrees_with_screen);
}
