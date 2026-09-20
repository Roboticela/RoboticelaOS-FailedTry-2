/*
 * RoboPaint traversal suite (Phase 11.1) — paint.traversal.*
 *
 * The half of the phase's tests that asks the traversal through a **real document**: the
 * real tree builder, the real cascade, the real DOM bridge, a real layout pass, and then a
 * real paint pass. `SuiteRoboPaint.c` asks each piece on its own; this one asks whether
 * the pieces compose, and the difference is not academic -- a validity rule that is right
 * in isolation is still wrong if the traversal feeds it a box four levels deep, and an
 * accumulation that is right for one box is still wrong if the origin it adds is the
 * sibling's.
 *
 * The claims each group of cases pins, in the order they appear:
 *
 *   order and shape   document order, parents before children, an explicit marker pair
 *                     per placed box, the depth that makes the tree recoverable from a
 *                     flat list, and the two cases where a pass produces *nothing* (a
 *                     document whose root generates no box, and a tree whose geometry is
 *                     not final -- which is a refusal rather than a silent empty list);
 *
 *   decorations       emit only when it draws: a declared background, one operation per
 *                     border side whose *used* width is above zero, `currentColor`
 *                     resolved from the element's own `color` and counted when it cannot
 *                     be;
 *
 *   geometry          the two coordinate spaces, held together by the only assertion that
 *                     can hold them: for every operation, the local boxes must be
 *                     field-for-field what `robo_paint_boxes_of` reads out of the tree,
 *                     and the page-space boxes must be the local ones plus the
 *                     operation's own origin;
 *
 *   clip              the boundary in force, accumulated per axis, with the padding box as
 *                     the edge -- and the axis split, which is the case a single
 *                     rectangle cannot express;
 *
 *   counts            `visited` against `objects`, `boxes` against the marker pairs,
 *                     `ops` against the list, and the pass count across two passes;
 *
 *   determinism      the same tree twice, byte-identical, and the same document built into
 *                     a second tree, which is what rules out a conclusion that depended on
 *                     a pointer or on an allocation.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends
 * on no Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A document that gives every layer something to do                   */
/* ------------------------------------------------------------------ */

/*
 * The fixture stylesheet's own base, and the second line is the one a reader is owed an
 * explanation for: an element nothing styles computes to `display: inline`, and an
 * inline-level box in a block formatting context is a box this build gives **no** border
 * and **no** padding (`RoboLayoutPadding.h` argues why a transparent inline box's
 * rectangle is the union of its contents rather than a size to inflate). Every case below
 * is about what a *painted block box* does with its insets -- which side draws, where the
 * band is, what the clip edge is -- so the fixtures say once that these elements are the
 * block containers, rather than leaving each case to re-declare it. The initial value is
 * therefore still exercised, deliberately: `<head>` is a box nothing styles, and it is the
 * unplaced box every count in this file is calibrated against.
 *
 * `* { display: block }` would be shorter and would hide exactly that fact.
 */
#define PAINT_BASE_CSS                                 \
    "html, body, div, section, aside, figure, p { display: block }"

/** The `BEGIN_BOX` operation of @p node, or 0 when it has none. */
static int paint_begin_of(const paint_result_t *result,
                          robo_layout_node_id_t node,
                          robo_paint_operation_t *out)
{
    size_t index;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
            return 0;
        }
        if (operation.kind == ROBO_PAINT_OP_BEGIN_BOX && operation.node == node) {
            if (out) {
                *out = operation;
            }
            return 1;
        }
    }
    return 0;
}

/** The `END_BOX` operation of @p node, or 0 when it has none. */
static int paint_end_of(const paint_result_t *result,
                        robo_layout_node_id_t node,
                        robo_paint_operation_t *out)
{
    size_t index;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
            return 0;
        }
        if (operation.kind == ROBO_PAINT_OP_END_BOX && operation.node == node) {
            if (out) {
                *out = operation;
            }
            return 1;
        }
    }
    return 0;
}

/** The first operation of @p kind on @p node, or 0 when it has none. */
static int paint_op_of(const paint_result_t *result,
                       robo_paint_op_kind_t kind,
                       robo_layout_node_id_t node,
                       robo_paint_operation_t *out)
{
    size_t index;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
            return 0;
        }
        if (operation.kind == kind && operation.node == node) {
            if (out) {
                *out = operation;
            }
            return 1;
        }
    }
    return 0;
}

/** The index of the first operation of @p kind on @p node, or `(size_t)-1`. */
static size_t paint_index_of(const paint_result_t *result,
                             robo_paint_op_kind_t kind,
                             robo_layout_node_id_t node)
{
    size_t index;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
            return (size_t)-1;
        }
        if (operation.kind == kind && operation.node == node) {
            return index;
        }
    }
    return (size_t)-1;
}

/**
 * Assert the two coordinate spaces agree with the tree, for **every** operation.
 *
 * The phase's central claim, asked of a whole document rather than one box: every
 * operation's boxes minus its origin must be exactly what RoboLayout wrote, and the
 * page-space boxes must be the local ones plus that origin. A traversal that mixed up a
 * sibling's origin, or carried the wrong containing block down one level, passes every
 * count in this suite and fails here.
 */
static void paint_all_boxes_match_layout(paint_fixture_doc_t *fixture,
                                         const paint_result_t *result)
{
    size_t index;

    ROBO_CHECK(robo_paint_list_count(result->list) > 0u);
    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;
        robo_paint_boxes_t local;
        robo_paint_boxes_t shifted;

        ROBO_CHECK_EQ_INT(robo_paint_list_at(result->list, index, &operation), ROBO_OK);
        /* Every operation in a list is a record, because the list refuses anything else
         * -- so this is a check of the container's promise as much as of the walk's. */
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 1);
        paint_local_boxes_match_layout(fixture, operation);

        memset(&local, 0, sizeof(local));
        memset(&shifted, 0, sizeof(shifted));
        ROBO_CHECK_EQ_INT(robo_paint_operation_local_boxes(&operation, &local, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_boxes_translate(&local, operation.origin.x,
                                                     operation.origin.y, &shifted, NULL),
                          ROBO_OK);
        if (!robo_paint_boxes_equal(&shifted, &operation.boxes)) {
            robo_test_fail(__FILE__, __LINE__,
                           "operation %d on node %d is not its local boxes plus its origin",
                           (int)operation.kind, (int)operation.node);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 1. Order and shape                                                  */
/* ------------------------------------------------------------------ */

static void test_traversal_refusals_and_empty(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_context_t *context = NULL;

    /*
     * A **document that builds no boxes at all**: the root element generates none, and
     * nothing inside a `display: none` subtree generates one either -- so there is not
     * even a root for the traversal to start from. A legitimate pass that paints nothing
     * rather than an error: no root box is an answer, not an absence.
     */
    paint_begin(&fixture, "<html><body><div class=\"card\"></div></body></html>",
                "html { display: none }");
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_object_count(fixture.tree), 0u);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_root(fixture.tree),
                      (int)ROBO_LAYOUT_NODE_ID_NONE);
    paint_layout(&fixture, 800, 600);

    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(result.list), 0);
    ROBO_CHECK_EQ_INT((int)result.stats.objects, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.visited, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.ops, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.passes, 1);
    paint_result_dispose(&result);
    paint_end(&fixture);

    /*
     * A tree whose geometry is not final is *refused*, not painted: a list built from
     * half-computed geometry is a wrong list, and a caller who wanted a preview can run a
     * layout pass first. The refusal is an **outcome** and not a failure -- the pass
     * reached a conclusion, so the code is OK and the diagnosis is the fourth outcome.
     */
    paint_begin(&fixture, "<html><body><div class=\"card\"></div></body></html>",
                PAINT_BASE_CSS " .card { width: 20px; height: 10px }");
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 1);
    paint_result_init(&result);
    ROBO_CHECK_EQ_INT(robo_paint_context_create(&context, NULL), ROBO_OK);
    paint_run(&fixture, context, result.list);
    ROBO_CHECK_EQ_INT((int)robo_paint_context_outcome(context),
                      (int)ROBO_PAINT_PASS_STALE);
    /* A refused pass hands back an **empty** list rather than the previous pass's, which
     * is the only answer a caller can act on. */
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(result.list), 0);
    robo_paint_context_stats(context, &result.stats);
    ROBO_CHECK_EQ_INT((int)result.stats.ops, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.visited, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.passes, 1);

    /* And the same list, once the geometry *is* final, paints: the refusal was about the
     * tree's state and not about the document. */
    paint_layout(&fixture, 800, 600);
    paint_run(&fixture, context, result.list);
    ROBO_CHECK_EQ_INT((int)robo_paint_context_outcome(context),
                      (int)ROBO_PAINT_PASS_PAINTED);
    ROBO_CHECK(robo_paint_list_count(result.list) > 0u);
    robo_paint_context_destroy(context);
    paint_result_dispose(&result);
    paint_end(&fixture);

    /* The argument refusals, and the defaults a context that has run nothing answers. */
    paint_begin(&fixture, "<html><body></body></html>", PAINT_BASE_CSS);
    paint_layout(&fixture, 800, 600);
    paint_result_init(&result);
    ROBO_CHECK_EQ_INT(robo_paint_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_context_run(NULL, fixture.tree, result.list, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_context_run(context, NULL, result.list, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_context_run(context, fixture.tree, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)robo_paint_context_outcome(context), (int)ROBO_PAINT_PASS_FAILED);
    ROBO_CHECK_EQ_INT(robo_paint_context_create(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)robo_paint_context_outcome(NULL), (int)ROBO_PAINT_PASS_NONE);
    ROBO_CHECK(robo_paint_context_tree(NULL) == NULL);
    robo_paint_context_stats(NULL, &result.stats);
    ROBO_CHECK_EQ_INT((int)result.stats.objects, 0);
    robo_paint_context_stats(NULL, NULL); /* safe */
    robo_paint_context_destroy(context);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_traversal_single_box(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t html_box;
    robo_layout_node_id_t body_box;
    robo_layout_node_id_t card;
    robo_paint_operation_t begin;
    robo_paint_operation_t end;
    robo_paint_operation_t body_begin;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         PAINT_BASE_CSS " .card { width: 100px; height: 40px }");
    html_box = paint_box_by_tag(&fixture, "html");
    body_box = paint_box_by_tag(&fixture, "body");
    card = paint_box(&fixture, "card");
    /* The root box is the root *element*'s, which is what makes the tree's root and the
     * document's document element the same thing. */
    ROBO_CHECK_EQ_INT((int)html_box, (int)robo_layout_tree_root(fixture.tree));

    paint_once(&fixture, &result);

    /* Three placed boxes -- html, body and the card -- and one unplaced `<head>`, which
     * is the parser's own element and the box the layout pass declines. */
    ROBO_CHECK_EQ_INT((int)result.stats.objects, 4);
    ROBO_CHECK_EQ_INT((int)result.stats.visited, 4);
    ROBO_CHECK_EQ_INT((int)result.stats.boxes, 3);
    ROBO_CHECK_EQ_INT((int)result.stats.unplaced, 1);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BEGIN_BOX), 3);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_END_BOX), 3);
    /* The content boundary is emitted for every placed box, so it is as many records as
     * there are boxes -- and the sequence is three structural triples and nothing that
     * draws, because this document declares no background and no border. */
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_CONTENT), 3);
    ROBO_CHECK_EQ_INT((int)result.stats.ops, (int)robo_paint_list_count(result.list));
    ROBO_CHECK_EQ_INT((int)result.stats.ops, 9);

    /* The root's containing block is the initial containing block, so its origin is the
     * page origin -- and it is the one place an origin is not another box's content. */
    ROBO_CHECK_EQ_INT(paint_begin_of(&result, html_box, &begin), 1);
    ROBO_CHECK_EQ_INT((int)begin.depth, 0);
    ROBO_CHECK_EQ_INT(begin.origin.x, 0);
    ROBO_CHECK_EQ_INT(begin.origin.y, 0);
    ROBO_CHECK_EQ_INT(paint_begin_of(&result, body_box, &body_begin), 1);
    ROBO_CHECK_EQ_INT((int)body_begin.depth, 1);
    ROBO_CHECK_EQ_INT(paint_begin_of(&result, card, &begin), 1);
    ROBO_CHECK_EQ_INT((int)begin.depth, 2);

    /* Parents before children and the closing marker after them: the property a
     * rasteriser walks once and keeps a clip stack for. */
    ROBO_CHECK(paint_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, html_box) <
               paint_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, body_box));
    ROBO_CHECK(paint_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, body_box) <
               paint_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, card));
    ROBO_CHECK(paint_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, card) <
               paint_index_of(&result, ROBO_PAINT_OP_END_BOX, card));
    ROBO_CHECK(paint_index_of(&result, ROBO_PAINT_OP_END_BOX, card) <
               paint_index_of(&result, ROBO_PAINT_OP_END_BOX, body_box));
    /* The content boundary of a parent precedes its children's opening markers, and a
     * box's own boundary comes after its own opening marker: the shape that makes each
     * box's own records a contiguous group. */
    ROBO_CHECK(paint_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, body_box) <
               paint_index_of(&result, ROBO_PAINT_OP_CONTENT, body_box));
    ROBO_CHECK(paint_index_of(&result, ROBO_PAINT_OP_CONTENT, body_box) <
               paint_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, card));
    ROBO_CHECK(paint_index_of(&result, ROBO_PAINT_OP_CONTENT, card) <
               paint_index_of(&result, ROBO_PAINT_OP_END_BOX, card));
    ROBO_CHECK(paint_begin_of(&result, card, &begin) == 1);
    ROBO_CHECK(paint_end_of(&result, card, &end) == 1);
    /* The pair closes over the same rectangle: an `END_BOX` that reported different
     * geometry from its `BEGIN_BOX` would be two answers to one question. */
    ROBO_CHECK_EQ_INT(robo_paint_boxes_equal(&begin.boxes, &end.boxes), 1);

    paint_all_boxes_match_layout(&fixture, &result);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_traversal_siblings_in_order(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t boxes[3];
    size_t index;

    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"one\"></div>"
                         "<div class=\"two\"></div>"
                         "<div class=\"three\"></div>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .one, .two, .three { width: 50px; height: 10px }");

    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT((int)result.stats.boxes, 5); /* html, body, and three divs */
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BEGIN_BOX), 5);

    /* Document order, and nothing else: the three siblings' markers appear in the order
     * the DOM gave them, which is what makes a paint list reproducible from a tree. */
    boxes[0] = paint_box(&fixture, "one");
    boxes[1] = paint_box(&fixture, "two");
    boxes[2] = paint_box(&fixture, "three");
    for (index = 0u; index + 1u < 3u; index++) {
        size_t a = paint_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, boxes[index]);
        size_t b = paint_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, boxes[index + 1u]);

        ROBO_CHECK(a != (size_t)-1);
        ROBO_CHECK(b != (size_t)-1);
        ROBO_CHECK(a < b);
    }

    /* Their origins are the body's content origin -- one containing block, so one origin,
     * and each box's own rectangle is where layout put it relative to that. */
    {
        robo_paint_operation_t first;
        robo_paint_operation_t second;

        ROBO_CHECK_EQ_INT(paint_begin_of(&result, boxes[0], &first), 1);
        ROBO_CHECK_EQ_INT(paint_begin_of(&result, boxes[1], &second), 1);
        ROBO_CHECK_EQ_INT(first.origin.x, second.origin.x);
        ROBO_CHECK_EQ_INT(first.origin.y, second.origin.y);
        ROBO_CHECK(second.boxes.border.y >= first.boxes.border.y + first.boxes.border.height);
    }

    paint_all_boxes_match_layout(&fixture, &result);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_traversal_empty_element(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t empty;
    robo_paint_operation_t begin;
    robo_paint_operation_t end;

    /* A box with no children and no size is still a placement, so it still gets a marker
     * pair -- and an empty rectangle is a *predicate* rather than a failure. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"empty\"></div></body></html>",
                         PAINT_BASE_CSS " .empty { width: 0; height: 0 }");
    empty = paint_box(&fixture, "empty");

    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT(paint_begin_of(&result, empty, &begin), 1);
    ROBO_CHECK_EQ_INT(paint_end_of(&result, empty, &end), 1);
    ROBO_CHECK_EQ_INT(begin.boxes.border.width, 0);
    ROBO_CHECK_EQ_INT(begin.boxes.border.height, 0);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&begin.boxes), 1);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 2. Decorations: emitted only when they draw                         */
/* ------------------------------------------------------------------ */

static void test_traversal_background(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t painted;
    robo_layout_node_id_t bare;
    robo_layout_node_id_t clear;
    robo_paint_operation_t begin;
    robo_paint_operation_t background;

    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"painted\"></div>"
                         "<div class=\"bare\"></div>"
                         "<div class=\"clear\"></div>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .painted, .bare, .clear { width: 40px; height: 10px }"
                         " .painted { background-color: #ff0000 }"
                         " .clear { background-color: transparent }");
    painted = paint_box(&fixture, "painted");
    bare = paint_box(&fixture, "bare");
    clear = paint_box(&fixture, "clear");

    paint_once(&fixture, &result);

    /* One background operation, for the one box that declared one: an undeclared
     * background is `transparent` and a declared transparent one is the same answer, so
     * neither emits an operation that draws nothing. */
    ROBO_CHECK_EQ_INT((int)result.stats.backgrounds, 1);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BACKGROUND), 1);
    ROBO_CHECK_EQ_INT(paint_op_of(&result, ROBO_PAINT_OP_BACKGROUND, painted, &background),
                      1);
    ROBO_CHECK_EQ_INT(paint_op_of(&result, ROBO_PAINT_OP_BACKGROUND, bare, NULL), 0);
    ROBO_CHECK_EQ_INT(paint_op_of(&result, ROBO_PAINT_OP_BACKGROUND, clear, NULL), 0);

    /* The colour is the declaration's, and the painting area is the **border box** --
     * CSS's initial `background-clip`. */
    paint_color_eq(background.color, robo_css_color_make(255, 0, 0, 255),
                   "the background colour");
    ROBO_CHECK_EQ_INT(paint_begin_of(&result, painted, &begin), 1);
    paint_rect_eq(background.boxes.border, begin.boxes.border, "the background's area");
    ROBO_CHECK_EQ_INT((int)background.color.kind, (int)ROBO_CSS_COLOR_RGBA);

    paint_all_boxes_match_layout(&fixture, &result);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_traversal_border_sides(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t solid;
    robo_layout_node_id_t none_style;
    robo_layout_node_id_t zero_width;
    robo_paint_operation_t begin;
    robo_paint_operation_t side;
    robo_css_box_side_t box_side;

    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"solid\"></div>"
                         "<div class=\"nostyle\"></div>"
                         "<div class=\"zero\"></div>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .solid, .nostyle, .zero { width: 100px; height: 20px }"
                         " .solid { border: 2px solid #0000ff }"
                         " .nostyle { border: 5px none #000000 }"
                         " .zero { border: 0 solid #000000 }");
    solid = paint_box(&fixture, "solid");
    none_style = paint_box(&fixture, "nostyle");
    zero_width = paint_box(&fixture, "zero");

    paint_once(&fixture, &result);

    /* One operation per side whose **used** width is above zero, which is why a
     * `border: 5px none` side -- a declared five that costs no room -- emits nothing and
     * needs no second decision here. */
    ROBO_CHECK_EQ_INT((int)result.stats.border_sides, 4);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BORDER_TOP), 1);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BORDER_RIGHT), 1);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BORDER_BOTTOM), 1);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BORDER_LEFT), 1);
    ROBO_CHECK_EQ_INT(paint_op_of(&result, ROBO_PAINT_OP_BORDER_TOP, none_style, NULL), 0);
    ROBO_CHECK_EQ_INT(paint_op_of(&result, ROBO_PAINT_OP_BORDER_TOP, zero_width, NULL), 0);

    ROBO_CHECK_EQ_INT(paint_begin_of(&result, solid, &begin), 1);
    /* The top band spans the border box's full width and stops where the padding box
     * begins, which for a 2px border is the border box's own top edge plus 2. */
    ROBO_CHECK_EQ_INT(paint_op_of(&result, ROBO_PAINT_OP_BORDER_TOP, solid, &side), 1);
    ROBO_CHECK_EQ_INT(robo_paint_operation_side(&side, &box_side), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)box_side, (int)ROBO_CSS_BOX_SIDE_TOP);
    {
        robo_layout_rect_t band;

        memset(&band, 0, sizeof(band));
        ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(&side, &band, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(band.x, begin.boxes.border.x);
        ROBO_CHECK_EQ_INT(band.y, begin.boxes.border.y);
        ROBO_CHECK_EQ_INT(band.width, begin.boxes.border.width);
        ROBO_CHECK_EQ_INT(band.height, 2);
    }
    ROBO_CHECK_EQ_INT(paint_op_of(&result, ROBO_PAINT_OP_BORDER_LEFT, solid, &side), 1);
    {
        robo_layout_rect_t band;

        memset(&band, 0, sizeof(band));
        ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(&side, &band, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(band.width, 2);
        ROBO_CHECK_EQ_INT(band.height, begin.boxes.border.height);
    }
    /* Every side carries the declared colour. */
    {
        const robo_paint_op_kind_t kinds[ROBO_CSS_BOX_SIDE_COUNT] = {
            ROBO_PAINT_OP_BORDER_TOP, ROBO_PAINT_OP_BORDER_RIGHT,
            ROBO_PAINT_OP_BORDER_BOTTOM, ROBO_PAINT_OP_BORDER_LEFT
        };
        size_t index;

        for (index = 0u; index < (size_t)ROBO_CSS_BOX_SIDE_COUNT; index++) {
            ROBO_CHECK_EQ_INT(paint_op_of(&result, kinds[index], solid, &side), 1);
            paint_color_eq(side.color, robo_css_color_make(0, 0, 255, 255),
                           "a border side's colour");
        }
    }

    paint_all_boxes_match_layout(&fixture, &result);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_traversal_border_color_initial(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t initial;
    robo_layout_node_id_t explicit_color;
    robo_paint_operation_t side;

    /*
     * An undeclared border colour is CSS's initial `currentColor`, and resolving it is
     * exactly the job of the layer that has an element -- `RoboLayoutBorder.h` says the
     * used record carries a `CURRENT` colour because "only the layer with an element can
     * resolve one", and that layer is this one.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"initial\"></div>"
                         "<div class=\"explicit\"></div>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .initial, .explicit { width: 60px; height: 10px;"
                         "   border: 1px solid }"
                         " .initial { color: #00ff00 }"
                         " .explicit { color: #00ff00; border-color: #123456 }");
    initial = paint_box(&fixture, "initial");
    explicit_color = paint_box(&fixture, "explicit");

    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT(paint_op_of(&result, ROBO_PAINT_OP_BORDER_TOP, initial, &side), 1);
    paint_color_eq(side.color, robo_css_color_make(0, 255, 0, 255),
                   "an undeclared border colour resolved from `color`");
    ROBO_CHECK_EQ_INT((int)side.color.kind, (int)ROBO_CSS_COLOR_RGBA);

    /* And a declared colour wins over the element's `color`, which is what makes the
     * resolution above a fallback rather than an override. */
    ROBO_CHECK_EQ_INT(paint_op_of(&result, ROBO_PAINT_OP_BORDER_TOP, explicit_color, &side),
                      1);
    paint_color_eq(side.color, robo_css_color_make(0x12, 0x34, 0x56, 255),
                   "a declared border colour");

    ROBO_CHECK_EQ_INT((int)result.stats.contextual_colors, 0);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Geometry and the clip in force                                   */
/* ------------------------------------------------------------------ */

static void test_traversal_box_model_geometry(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;
    robo_paint_operation_t outer_begin;
    robo_paint_operation_t inner_begin;
    robo_paint_boxes_t from_layout;

    /* margin -> border -> padding -> content, and the *derived* relationships asserted
     * rather than a pile of magic numbers: the content origin of the outer box is where
     * the inner box's containing block starts, and the inner box's paint coordinates are
     * its layout coordinates plus that origin. */
    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"outer\"><section class=\"inner\"></section></div>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .outer { width: 200px; height: 100px; margin: 10px 20px;"
                         "   padding: 5px; border: 2px solid #000000 }"
                         " .inner { width: 50px; height: 20px; padding: 3px;"
                         "   border: 1px solid #000000 }");
    outer = paint_box(&fixture, "outer");
    inner = paint_box(&fixture, "inner");

    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT(paint_begin_of(&result, outer, &outer_begin), 1);
    ROBO_CHECK_EQ_INT(paint_begin_of(&result, inner, &inner_begin), 1);

    /* The inner box's containing block is the outer's content box, in paint coordinates
     * -- so its origin is that content box's origin, and nothing else. */
    ROBO_CHECK_EQ_INT(inner_begin.origin.x, outer_begin.boxes.content.x);
    ROBO_CHECK_EQ_INT(inner_begin.origin.y, outer_begin.boxes.content.y);

    /* The outer's three rectangles are each inset from the one outside it by exactly the
     * declared insets: 2 of border, then 5 of padding. */
    ROBO_CHECK_EQ_INT(outer_begin.boxes.padding.x - outer_begin.boxes.border.x, 2);
    ROBO_CHECK_EQ_INT(outer_begin.boxes.padding.y - outer_begin.boxes.border.y, 2);
    ROBO_CHECK_EQ_INT(outer_begin.boxes.content.x - outer_begin.boxes.padding.x, 5);
    ROBO_CHECK_EQ_INT(outer_begin.boxes.content.y - outer_begin.boxes.padding.y, 5);
    ROBO_CHECK_EQ_INT(outer_begin.boxes.border.width -
                          outer_begin.boxes.content.width, 14);
    ROBO_CHECK_EQ_INT(outer_begin.boxes.border.height -
                          outer_begin.boxes.content.height, 14);
    ROBO_CHECK_EQ_INT(outer_begin.boxes.insets_measured, 1);

    /* The levels are geometrically distinct, which is what makes a border band and a
     * background different regions rather than one region drawn twice. */
    ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&outer_begin.boxes), 1);

    /*
     * And the accumulation really happened: the inner box's *paint* rectangle is its
     * layout rectangle moved by its containing block's content origin, which is a
     * non-zero point here, so the two spaces are distinguishable rather than coincidentally
     * equal. A build that skipped the accumulation would report the inner box at the page
     * origin -- inside its parent's border, which is what makes this a check and not a
     * restatement.
     */
    {
        robo_paint_operation_t inner_begin;

        ROBO_CHECK_EQ_INT(paint_begin_of(&result, inner, &inner_begin), 1);
        memset(&from_layout, 0, sizeof(from_layout));
        ROBO_CHECK_EQ_INT(robo_paint_boxes_of(fixture.tree, inner, &from_layout, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_boxes_equal(&from_layout, &inner_begin.boxes), 0);
        ROBO_CHECK_EQ_INT(inner_begin.origin.x, outer_begin.boxes.content.x);
        ROBO_CHECK_EQ_INT(inner_begin.boxes.border.x,
                          from_layout.border.x + inner_begin.origin.x);
        ROBO_CHECK_EQ_INT(inner_begin.boxes.border.y,
                          from_layout.border.y + inner_begin.origin.y);
        ROBO_CHECK((int)inner_begin.origin.x > 0);
        ROBO_CHECK((int)inner_begin.origin.y > 0);
    }

    paint_all_boxes_match_layout(&fixture, &result);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_traversal_clip_visible(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t host;

    /* Nothing declares an overflow, so no boundary is ever in force and the clip
     * carried by every operation is the cleared one. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"host\"><section class=\"child\">"
                         "</section></div></body></html>",
                         PAINT_BASE_CSS
                         " .host { width: 100px; height: 50px }"
                         " .child { width: 300px; height: 300px }");
    host = paint_box(&fixture, "host");
    (void)host;

    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT((int)result.stats.clipped, 0);
    {
        size_t index;

        for (index = 0u; index < robo_paint_list_count(result.list); index++) {
            robo_paint_operation_t operation;

            ROBO_CHECK_EQ_INT(robo_paint_list_at(result.list, index, &operation), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&operation.clip), 0);
        }
    }
    paint_all_boxes_match_layout(&fixture, &result);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_traversal_clip_hidden(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t host;
    robo_layout_node_id_t child;
    robo_paint_operation_t host_begin;
    robo_paint_operation_t child_begin;
    robo_paint_operation_t child_background;
    robo_paint_clip_t clip;
    robo_layout_rect_t rect;

    /*
     * A box that hides both axes puts a boundary in force for its *contents*, and the
     * edge is the **padding box** -- CSS's clipping edge and the same box
     * `robo_layout_overflow_edges_clip_rect` reads one layer down.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"host\"><section class=\"child\">"
                         "</section></div></body></html>",
                         PAINT_BASE_CSS
                         " .host { width: 100px; height: 50px; padding: 4px;"
                         "   border: 2px solid #000000; overflow: hidden;"
                         "   background-color: #eeeeee }"
                         " .child { width: 300px; height: 300px;"
                         "   background-color: #ff0000 }");
    host = paint_box(&fixture, "host");
    child = paint_box(&fixture, "child");

    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT((int)result.stats.clipped, 1);
    ROBO_CHECK_EQ_INT(paint_begin_of(&result, host, &host_begin), 1);
    ROBO_CHECK_EQ_INT(paint_begin_of(&result, child, &child_begin), 1);

    /* The host's own operations are *outside* its boundary -- a box clips its contents,
     * not itself -- so its background carries no clip. */
    ROBO_CHECK_EQ_INT(paint_op_of(&result, ROBO_PAINT_OP_BACKGROUND, host, &child_background),
                      1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&child_background.clip), 0);

    /* The child's do, and the interval is exactly the host's padding box. */
    ROBO_CHECK_EQ_INT(paint_op_of(&result, ROBO_PAINT_OP_BACKGROUND, child, &child_background),
                      1);
    clip = child_background.clip;
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&clip), 1);
    ROBO_CHECK_EQ_INT(clip.horizontal.start, host_begin.boxes.padding.x);
    ROBO_CHECK_EQ_INT(clip.horizontal.end,
                      host_begin.boxes.padding.x + host_begin.boxes.padding.width);
    ROBO_CHECK_EQ_INT(clip.vertical.start, host_begin.boxes.padding.y);
    ROBO_CHECK_EQ_INT(clip.vertical.end,
                      host_begin.boxes.padding.y + host_begin.boxes.padding.height);

    /* Both axes bounded is the one case where a rectangle says everything, and the door
     * that answers a rectangle says so here. */
    memset(&rect, 0, sizeof(rect));
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(&clip, &rect), ROBO_OK);
    paint_rect_eq(rect, host_begin.boxes.padding, "the clip rectangle");

    /* The child's own operations carry the boundary too, and its begin marker keeps its
     * own geometry -- clipping is what a consumer does with the record, not a change to
     * what layout concluded. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&child_begin.clip), 1);
    paint_all_boxes_match_layout(&fixture, &result);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_traversal_clip_axis_split(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t child;
    robo_paint_operation_t operation;

    /*
     * `overflow-x: hidden` beside `overflow-y: visible` is a legal declaration an author
     * writes and this build resolves, and it means exactly what it says: bounded
     * horizontally, free vertically. A single rectangle cannot express it -- a consumer
     * handing four numbers to a rasteriser would clip the axis the author left open --
     * which is why the record is two intervals.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"host\"><section class=\"child\">"
                         "</section></div></body></html>",
                         PAINT_BASE_CSS
                         " .host { width: 100px; height: 50px;"
                         "   overflow-x: hidden; overflow-y: visible }"
                         " .child { width: 300px; height: 300px;"
                         "   background-color: #00ff00 }");
    child = paint_box(&fixture, "child");

    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT(paint_op_of(&result, ROBO_PAINT_OP_BACKGROUND, child, &operation), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_active(&operation.clip.horizontal), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_active(&operation.clip.vertical), 0);
    /* And a rectangle is exactly the wrong answer here, so the door refuses rather than
     * clipping the free axis. */
    {
        robo_layout_rect_t rect;

        memset(&rect, 0, sizeof(rect));
        ROBO_CHECK_EQ_INT(robo_paint_clip_rect(&operation.clip, &rect), ROBO_NOT_FOUND);
    }
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. Text, counts and the pass record                                 */
/* ------------------------------------------------------------------ */

static void test_traversal_text_painted(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t para;
    robo_paint_operation_t text;
    size_t index;
    size_t text_count = 0u;
    size_t box_count = 0u;

    /*
     * A text run is *visited, counted and painted* -- the 11.6 change stated where it can
     * be seen. The run is a leaf and a **drawing**, so it contributes one operation and no
     * `BEGIN_BOX`/`CONTENT`/`END_BOX` triple: the skeleton is a statement about boxes, and
     * a text node is not one. `text_runs = texts + text_skipped` stays exact, which is
     * what keeps "this document has text" and "this document's text drew nothing" two
     * different numbers rather than one silence.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><p class=\"para\">Alpha beta</p></body></html>",
                         PAINT_BASE_CSS " p { display: block }");
    para = paint_box(&fixture, "para");

    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT((int)result.stats.text_runs, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.texts, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.text_skipped, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.text_runs,
                      (int)(result.stats.texts + result.stats.text_skipped));
    ROBO_CHECK_EQ_INT(paint_begin_of(&result, para, NULL), 1);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_TEXT), 1);

    /* Every operation names either a box or the one text object, and the run's node is
     * reached through the tree as a text object rather than a box. */
    for (index = 0u; index < robo_paint_list_count(result.list); index++) {
        robo_paint_operation_t operation;
        robo_layout_object_kind_t kind;

        ROBO_CHECK_EQ_INT(robo_paint_list_at(result.list, index, &operation), ROBO_OK);
        kind = robo_layout_object_kind(fixture.tree, operation.node);
        if (kind == ROBO_LAYOUT_OBJECT_TEXT) {
            text_count++;
            ROBO_CHECK_EQ_INT((int)operation.kind, (int)ROBO_PAINT_OP_TEXT);
        } else {
            box_count++;
            ROBO_CHECK_EQ_INT((int)kind, (int)ROBO_LAYOUT_OBJECT_BOX);
            ROBO_CHECK(operation.kind != ROBO_PAINT_OP_TEXT);
        }
    }
    ROBO_CHECK_EQ_INT((int)text_count, 1);
    ROBO_CHECK_EQ_INT((int)box_count,
                      (int)paint_count_of(&result, ROBO_PAINT_OP_BEGIN_BOX) +
                          (int)paint_count_of(&result, ROBO_PAINT_OP_CONTENT) +
                          (int)paint_count_of(&result, ROBO_PAINT_OP_END_BOX));

    /* The run is painted, so the record carries characters and a baseline, and its
     * placement is layout's -- read back through the pool the list owns. */
    text = paint_require(&result, ROBO_PAINT_OP_TEXT);
    {
        robo_paint_text_pool_t *pool = robo_paint_list_text_pool(result.list);
        robo_paint_text_run_t run = robo_paint_operation_text(&text);
        uint32_t characters[16];
        size_t got = 0u;

        ROBO_CHECK_EQ_INT(robo_paint_text_run_is_valid(&run), 1);
        ROBO_CHECK_EQ_INT(
            (int)robo_layout_object_kind(fixture.tree, robo_paint_operation_node(&text)),
            (int)ROBO_LAYOUT_OBJECT_TEXT);
        ROBO_CHECK(pool != NULL);
        ROBO_CHECK_EQ_INT(robo_paint_text_pool_read(pool, run.span, characters, 16u, &got,
                                                    NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)got, (int)run.span.count);
        ROBO_CHECK(got == 10u);
        ROBO_CHECK(characters[0] == (uint32_t)'A');
    }

    paint_all_boxes_match_layout(&fixture, &result);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_traversal_counts_and_determinism(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    paint_result_t second;
    robo_paint_context_t *context = NULL;

    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"card\"><section class=\"badge\">New</section></div>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .card, .clip { width: 120px; height: 40px;"
                         "   border: 1px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px;"
                         "   background-color: #123456 }"
                         " .clip { overflow: hidden }"
                         " .spill { width: 400px; height: 400px;"
                         "   border: 2px solid #ff0000 }");

    paint_once(&fixture, &result);

    /*
     * The counts have to agree with each other and with the list, which is the pair of
     * facts a reader uses to tell a document that painted in full from one half of which
     * was never laid out.
     */
    ROBO_CHECK_EQ_INT((int)result.stats.visited, (int)result.stats.objects);
    ROBO_CHECK_EQ_INT((int)result.stats.ops, (int)robo_paint_list_count(result.list));
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BEGIN_BOX),
                      (int)result.stats.boxes);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_END_BOX),
                      (int)result.stats.boxes);
    /* Structural records are unconditional: one content boundary per placed box. */
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_CONTENT),
                      (int)result.stats.boxes);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BACKGROUND),
                      (int)result.stats.backgrounds);
    {
        size_t borders = paint_count_of(&result, ROBO_PAINT_OP_BORDER_TOP) +
                         paint_count_of(&result, ROBO_PAINT_OP_BORDER_RIGHT) +
                         paint_count_of(&result, ROBO_PAINT_OP_BORDER_BOTTOM) +
                         paint_count_of(&result, ROBO_PAINT_OP_BORDER_LEFT);

        ROBO_CHECK_EQ_INT((int)borders, (int)result.stats.border_sides);
    }
    ROBO_CHECK_EQ_INT((int)result.stats.depth_max,
                      (int)layout_tree_max_depth(fixture.tree));
    ROBO_CHECK_EQ_INT((int)result.stats.passes, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.clipped, 1);
    /* Two boxes at the top level plus the two nested ones plus html and body. */
    ROBO_CHECK_EQ_INT((int)result.stats.boxes, 6);
    paint_all_boxes_match_layout(&fixture, &result);

    /*
     * Determinism, three ways: the same tree painted again into a *second* list, the same
     * tree painted again after a `clear`, and the same document built into a second tree
     * and painted. A conclusion that depended on a pointer, an allocation or an
     * iteration order would fail at least one of them.
     */
    paint_result_init(&second);
    ROBO_CHECK_EQ_INT(robo_paint_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_context_run(context, fixture.tree, second.list, NULL),
                      ROBO_OK);
    robo_paint_context_stats(context, &second.stats);
    ROBO_CHECK_EQ_INT(robo_paint_list_equal(result.list, second.list), 1);
    ROBO_CHECK_EQ_INT((int)second.stats.ops, (int)result.stats.ops);
    ROBO_CHECK_EQ_INT((int)second.stats.boxes, (int)result.stats.boxes);
    ROBO_CHECK_EQ_INT((int)second.stats.backgrounds, (int)result.stats.backgrounds);
    ROBO_CHECK_EQ_INT((int)second.stats.border_sides, (int)result.stats.border_sides);
    ROBO_CHECK_EQ_INT((int)second.stats.clipped, (int)result.stats.clipped);
    ROBO_CHECK_EQ_INT((int)second.stats.depth_max, (int)result.stats.depth_max);

    /* One list, reused: `clear` keeps the storage and the next pass recomputes every
     * operation, so nothing from the previous pass survives. */
    ROBO_CHECK_EQ_INT(robo_paint_context_run(context, fixture.tree, result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_equal(result.list, second.list), 1);
    {
        robo_paint_stats_t stats;

        robo_paint_context_stats(context, &stats);
        ROBO_CHECK_EQ_INT((int)stats.passes, 2);
    }
    robo_paint_context_destroy(context);
    paint_result_dispose(&second);
    paint_result_dispose(&result);

    /* The same document, a second tree: identical, because a paint list is a conclusion
     * about the document and not about an allocation. */
    {
        paint_fixture_doc_t other;
        paint_result_t rebuilt;

        paint_begin_laid_out(&other,
                             "<html><body>"
                             "<div class=\"card\"><section class=\"badge\">New</section>"
                             "</div>"
                             "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                             "</body></html>",
                             PAINT_BASE_CSS
                             " .card, .clip { width: 120px; height: 40px;"
                             "   border: 1px solid #000000; background-color: #ffffff }"
                             " .badge { width: 20px; height: 10px;"
                             "   background-color: #123456 }"
                             " .clip { overflow: hidden }"
                             " .spill { width: 400px; height: 400px;"
                             "   border: 2px solid #ff0000 }");
        paint_once(&other, &rebuilt);
        paint_once(&fixture, &result);
        ROBO_CHECK_EQ_INT(robo_paint_list_equal(result.list, rebuilt.list), 1);
        paint_result_dispose(&rebuilt);
        paint_result_dispose(&result);
        paint_end(&other);
    }
    paint_end(&fixture);
}

static void test_traversal_context_lifecycle(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_context_t *context = NULL;
    int marker = 0;

    paint_begin_laid_out(&fixture, "<html><body></body></html>", PAINT_BASE_CSS);
    paint_result_init(&result);

    ROBO_CHECK_EQ_INT(robo_paint_context_create(&context, NULL), ROBO_OK);
    /* A fresh context has run nothing: no tree, no outcome, and a zeroed record. */
    ROBO_CHECK(robo_paint_context_tree(context) == NULL);
    ROBO_CHECK_EQ_INT((int)robo_paint_context_outcome(context), (int)ROBO_PAINT_PASS_NONE);
    ROBO_CHECK(robo_paint_context_user_data(context) == NULL);

    robo_paint_context_set_user_data(context, &marker);
    ROBO_CHECK(robo_paint_context_user_data(context) == &marker);

    paint_run(&fixture, context, result.list);
    ROBO_CHECK_EQ_INT((int)robo_paint_context_outcome(context),
                      (int)ROBO_PAINT_PASS_PAINTED);
    ROBO_CHECK(robo_paint_context_tree(context) == fixture.tree);

    /* A reset restores the just-created state, pass count included -- "this context has
     * run nothing" rather than "this context has run something else". The user data is
     * the caller's and survives, which is what makes install-once/reset-per-pass work. */
    robo_paint_context_reset(context);
    ROBO_CHECK_EQ_INT((int)robo_paint_context_outcome(context), (int)ROBO_PAINT_PASS_NONE);
    ROBO_CHECK(robo_paint_context_tree(context) == NULL);
    ROBO_CHECK(robo_paint_context_user_data(context) == &marker);
    robo_paint_context_stats(context, &result.stats);
    ROBO_CHECK_EQ_INT((int)result.stats.passes, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.objects, 0);

    robo_paint_context_reset(NULL);      /* safe */
    robo_paint_context_set_user_data(NULL, &marker);
    robo_paint_context_destroy(NULL);    /* safe */

    robo_paint_context_destroy(context);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 5. Safety                                                           */
/* ------------------------------------------------------------------ */

static void test_traversal_extreme_geometry(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t huge;

    /*
     * A dimension at the geometry's own maximum with a border on it: the accumulation
     * saturates rather than wrapping, and no negative extent escapes into a list. This is
     * the case Phase 10.15 found the cost of getting wrong in the layout pass's own
     * cursor, restated where *this* module does its own arithmetic.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"huge\"></div>"
                         "<div class=\"zero\"></div>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .huge { width: 2147483647px; height: 2147483647px;"
                         "   border: 5px solid #000000; background-color: #00ff00 }"
                         " .zero { width: 0; height: 0; margin: -10px;"
                         "   background-color: #123456 }");
    huge = paint_box(&fixture, "huge");

    paint_once(&fixture, &result);
    {
        size_t index;

        for (index = 0u; index < robo_paint_list_count(result.list); index++) {
            robo_paint_operation_t operation;

            ROBO_CHECK_EQ_INT(robo_paint_list_at(result.list, index, &operation), ROBO_OK);
            /* The container's promise, which is where "no invalid operation may escape"
             * is actually satisfied: every record in the list is one. */
            ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 1);
            ROBO_CHECK(operation.boxes.border.width >= 0);
            ROBO_CHECK(operation.boxes.border.height >= 0);
            ROBO_CHECK(operation.boxes.padding.width >= 0);
            ROBO_CHECK(operation.boxes.content.width >= 0);
            ROBO_CHECK(operation.boxes.content.height >= 0);
        }
    }
    ROBO_CHECK_EQ_INT(paint_begin_of(&result, huge, NULL), 1);
    ROBO_CHECK_EQ_INT((int)result.stats.contextual_colors, 0);
    paint_all_boxes_match_layout(&fixture, &result);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_traversal_allocator_balance(void)
{
    size_t baseline;
    int round;

    paint_fixture();
    /*
     * A settled baseline first: the bring-up helpers themselves allocate, so the number
     * to return to is the one after the fixture has been built once. The claim is that a
     * *pass* leaks nothing -- the list and the context included.
     */
    {
        paint_fixture_doc_t warmup;

        paint_begin_laid_out(&warmup,
                             "<html><body><div class=\"card\"><section class=\"badge\">"
                             "</section></div></body></html>",
                             PAINT_BASE_CSS
                             " .card { width: 40px; height: 20px;"
                             "   border: 1px solid #000000; background-color: #ffffff }"
                             " .badge { width: 10px; height: 5px; overflow: hidden }");
        paint_end(&warmup);
    }
    baseline = paint_live_blocks();

    for (round = 0; round < 3; round++) {
        paint_fixture_doc_t fixture;
        paint_result_t result;

        paint_begin_laid_out(&fixture,
                             "<html><body><div class=\"card\"><section class=\"badge\">"
                             "</section></div></body></html>",
                             PAINT_BASE_CSS
                             " .card { width: 40px; height: 20px;"
                             "   border: 1px solid #000000; background-color: #ffffff }"
                             " .badge { width: 10px; height: 5px; overflow: hidden }");
        paint_once(&fixture, &result);
        ROBO_CHECK(robo_paint_list_count(result.list) > 0u);
        paint_result_dispose(&result);
        paint_end(&fixture);
    }

    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_paint_traversal(void)
{
    robo_test_register("paint.traversal.refusals_and_empty",
                       test_traversal_refusals_and_empty);
    robo_test_register("paint.traversal.single_box", test_traversal_single_box);
    robo_test_register("paint.traversal.siblings_in_order", test_traversal_siblings_in_order);
    robo_test_register("paint.traversal.empty_element", test_traversal_empty_element);
    robo_test_register("paint.traversal.background", test_traversal_background);
    robo_test_register("paint.traversal.border_sides", test_traversal_border_sides);
    robo_test_register("paint.traversal.border_color_initial",
                       test_traversal_border_color_initial);
    robo_test_register("paint.traversal.box_model_geometry",
                       test_traversal_box_model_geometry);
    robo_test_register("paint.traversal.clip_visible", test_traversal_clip_visible);
    robo_test_register("paint.traversal.clip_hidden", test_traversal_clip_hidden);
    robo_test_register("paint.traversal.clip_axis_split", test_traversal_clip_axis_split);
    robo_test_register("paint.traversal.text_painted", test_traversal_text_painted);
    robo_test_register("paint.traversal.counts_and_determinism",
                       test_traversal_counts_and_determinism);
    robo_test_register("paint.traversal.context_lifecycle",
                       test_traversal_context_lifecycle);
    robo_test_register("paint.traversal.extreme_geometry",
                       test_traversal_extreme_geometry);
    robo_test_register("paint.traversal.allocator_balance",
                       test_traversal_allocator_balance);
}
