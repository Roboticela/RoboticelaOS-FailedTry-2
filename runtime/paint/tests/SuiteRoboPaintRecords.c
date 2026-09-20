/*
 * RoboPaint records suite (Phase 11.2) — paint.records.*
 *
 * The phase that makes the record model explicit. 11.1 established the architecture --
 * an operation is a value snapshot, the list owns it, the traversal orders it
 * deterministically and the dump reads it back -- and this suite asks the guarantees
 * that architecture *claims* rather than the ones a single document happens to satisfy:
 *
 *   the **sequence** has a regular skeleton, because structural records (`BEGIN_BOX`,
 *   `CONTENT`, `END_BOX`) are unconditional while drawing records are emitted only when
 *   they draw;
 *
 *   the `CONTENT` boundary is the explicit end of a box's own records and the start of
 *   its subtree's, so nesting is recoverable without consulting the tree;
 *
 *   a record **snapshots** what it needs and references nothing mutable -- proved by
 *   mutating the layout state after a list exists and showing the list does not move;
 *
 *   ownership is the list's and copying is by value, so a stored record cannot be
 *   changed through the argument it was built from.
 *
 * Every case builds a real document through the real chain and paints it. The hand-built
 * records appear only where the question is about a *record* rather than about a
 * document -- the append-copies-by-value case -- and never in place of a layout result.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite
 * depends on no Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboPaint/RoboPaintDump.h"

#include <string.h>

/* The same base sheet the traversal suite uses: these elements are the block containers,
 * and `<head>` stays the one box nothing styles. */
#define RECORDS_BASE_CSS \
    "html, body, div, section, aside, figure, p { display: block }"

/* ------------------------------------------------------------------ */
/* Local helpers                                                       */
/* ------------------------------------------------------------------ */

/** The @p nth operation of @p kind on @p node, or 0 when there is none. */
static int records_op_of(const paint_result_t *result,
                         robo_paint_op_kind_t kind,
                         robo_layout_node_id_t node,
                         robo_paint_operation_t *out)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
            return 0;
        }
        if (operation.kind != kind || operation.node != node) {
            continue;
        }
        if (seen == 0u) {
            if (out) {
                *out = operation;
            }
            return 1;
        }
        seen++;
    }
    return 0;
}

/** The index of the first operation of @p kind on @p node, or `(size_t)-1`. */
static size_t records_index_of(const paint_result_t *result,
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

/** Write the whole list into @p buffer (asserted to fit) and return its length. */
static size_t records_dump(const paint_result_t *result, char *buffer, size_t capacity)
{
    size_t length = 0u;

    memset(buffer, 0, capacity);
    ROBO_CHECK_EQ_INT(robo_paint_list_write(result->list, buffer, capacity, &length, NULL),
                      ROBO_OK);
    return length;
}

/* ------------------------------------------------------------------ */
/* 1. The sequence                                                     */
/* ------------------------------------------------------------------ */

static void test_records_content_sequence(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t body_box;
    robo_layout_node_id_t card;
    robo_paint_operation_t begin;
    robo_paint_operation_t content;
    robo_paint_operation_t end;
    size_t index;
    int open = 0;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         RECORDS_BASE_CSS " .card { width: 100px; height: 40px }");
    body_box = paint_box_by_tag(&fixture, "body");
    card = paint_box(&fixture, "card");

    paint_once(&fixture, &result);

    /* The skeleton: one structural triple per placed box, and nothing that draws. */
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BEGIN_BOX), 3);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_CONTENT), 3);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_END_BOX), 3);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BACKGROUND), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(result.list), 9);

    /* Each box's own records are contiguous: `[BEGIN_BOX .. CONTENT]` then its subtree
     * then `END_BOX`. Walking and checking the nesting as an open/close count is how the
     * claim is asked without naming every index. */
    for (index = 0u; index < robo_paint_list_count(result.list); index++) {
        robo_paint_operation_t operation;

        ROBO_CHECK_EQ_INT(robo_paint_list_at(result.list, index, &operation), ROBO_OK);
        if (operation.kind == ROBO_PAINT_OP_BEGIN_BOX) {
            open++;
        } else if (operation.kind == ROBO_PAINT_OP_END_BOX) {
            open--;
        }
        ROBO_CHECK(open >= 0);
    }
    ROBO_CHECK_EQ_INT(open, 0);

    /* And for the two boxes it is worth naming: the content boundary sits between the
     * opening marker and the closing one, and a parent's comes before its child opens. */
    ROBO_CHECK_EQ_INT(records_op_of(&result, ROBO_PAINT_OP_BEGIN_BOX, body_box, &begin), 1);
    ROBO_CHECK_EQ_INT(records_op_of(&result, ROBO_PAINT_OP_CONTENT, body_box, &content), 1);
    ROBO_CHECK_EQ_INT(records_op_of(&result, ROBO_PAINT_OP_END_BOX, body_box, &end), 1);
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, body_box) <
               records_index_of(&result, ROBO_PAINT_OP_CONTENT, body_box));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_CONTENT, body_box) <
               records_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, card));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_CONTENT, body_box) <
               records_index_of(&result, ROBO_PAINT_OP_END_BOX, body_box));

    /* The triple carries one depth, so a consumer can group a box's records by it. */
    ROBO_CHECK_EQ_INT((int)begin.depth, (int)content.depth);
    ROBO_CHECK_EQ_INT((int)content.depth, (int)end.depth);
    ROBO_CHECK_EQ_INT((int)body_box, (int)content.node);

    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_records_structural_unconditional(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t card;
    robo_paint_operation_t content;

    /* A box that declares no background, draws no border and has no children: it still
     * gets its whole skeleton, because the records state a fact about the sequence
     * rather than about the document. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"bare\"></div></body></html>",
                         RECORDS_BASE_CSS " .bare { width: 30px; height: 30px }");
    card = paint_box(&fixture, "bare");

    paint_once(&fixture, &result);

    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BACKGROUND), 0);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BORDER_TOP), 0);
    ROBO_CHECK_EQ_INT((int)records_op_of(&result, ROBO_PAINT_OP_CONTENT, card, &content), 1);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_structural(&content), 1);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_drawing(&content), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_operation_color(&content).kind,
                      (int)ROBO_CSS_COLOR_NONE);

    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 2. The content boundary and its geometry                            */
/* ------------------------------------------------------------------ */

static void test_records_content_geometry(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t card;
    robo_paint_operation_t content;
    robo_paint_boxes_t boxes;
    robo_paint_boxes_t from_layout;

    /* A box whose padding and border differ on each axis, so that the content box is
     * neither the border box nor the padding box: the three cannot be confused. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         RECORDS_BASE_CSS
                         " .card { width: 100px; height: 40px;"
                         "   margin: 7px; padding: 3px 5px 7px 11px;"
                         "   border: 2px solid #000000 }");
    card = paint_box(&fixture, "card");

    paint_once(&fixture, &result);

    ROBO_CHECK_EQ_INT(records_op_of(&result, ROBO_PAINT_OP_CONTENT, card, &content), 1);
    memset(&boxes, 0, sizeof(boxes));
    memset(&from_layout, 0, sizeof(from_layout));
    robo_paint_operation_boxes(&content, &boxes);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_of(fixture.tree, card, &from_layout, NULL), ROBO_OK);

    /* The record carries the box's three rectangles, and the one it *acts on* is the
     * content box -- the region a later clip, scroll or content origin will read. */
    ROBO_CHECK_EQ_INT(robo_paint_boxes_equal(&boxes, &from_layout), 1);
    ROBO_CHECK_EQ_INT(boxes.insets_measured, 1);
    ROBO_CHECK(boxes.content.x > boxes.padding.x);
    ROBO_CHECK(boxes.content.y > boxes.padding.y);
    ROBO_CHECK(boxes.content.width < boxes.padding.width);
    ROBO_CHECK(boxes.content.height < boxes.padding.height);
    ROBO_CHECK(boxes.padding.x > boxes.border.x);
    ROBO_CHECK(boxes.padding.width < boxes.border.width);

    /* The content boundary and the box's own markers agree about the box, and disagree
     * about which rectangle they are about -- which is what makes the boundary worth
     * emitting rather than inferring. */
    {
        robo_paint_operation_t begin;

        ROBO_CHECK_EQ_INT(records_op_of(&result, ROBO_PAINT_OP_BEGIN_BOX, card, &begin), 1);
        ROBO_CHECK_EQ_INT(robo_paint_boxes_equal(&begin.boxes, &content.boxes), 1);
        ROBO_CHECK_EQ_INT(robo_gfx_point_equal(begin.origin, content.origin), 1);
    }

    paint_local_boxes_match_layout(&fixture, content);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Record fields                                                    */
/* ------------------------------------------------------------------ */

static void test_records_fields(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t card;
    size_t index;
    size_t structural = 0u;
    size_t drawing = 0u;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         RECORDS_BASE_CSS
                         " .card { width: 50px; height: 50px;"
                         "   background-color: #3366cc; border: 1px solid #000000 }");
    card = paint_box(&fixture, "card");

    paint_once(&fixture, &result);

    for (index = 0u; index < robo_paint_list_count(result.list); index++) {
        robo_paint_operation_t operation;

        ROBO_CHECK_EQ_INT(robo_paint_list_at(result.list, index, &operation), ROBO_OK);

        /* Kind, identity, depth and geometry: the fields every record carries, each
         * asserted to be a value the module's own rules accept. */
        ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_known(operation.kind), 1);
        ROBO_CHECK(operation.kind != ROBO_PAINT_OP_NONE);
        ROBO_CHECK(operation.node != ROBO_LAYOUT_NODE_ID_NONE);
        ROBO_CHECK_EQ_INT(robo_layout_object_is_live(fixture.tree, operation.node), 1);
        ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&operation.boxes), 1);
        ROBO_CHECK_EQ_INT(robo_paint_clip_is_valid(&operation.clip), 1);
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 1);

        /* Structural and drawing are exact complements over a live record, and the
         * colour rule follows the split. */
        if (robo_paint_operation_is_structural(&operation)) {
            structural++;
            ROBO_CHECK_EQ_INT(robo_paint_operation_is_drawing(&operation), 0);
            ROBO_CHECK_EQ_INT((int)robo_paint_operation_color(&operation).kind,
                              (int)ROBO_CSS_COLOR_NONE);
        } else {
            drawing++;
            ROBO_CHECK_EQ_INT(robo_paint_operation_is_structural(&operation), 0);
            ROBO_CHECK_EQ_INT(robo_paint_operation_is_drawing(&operation), 1);
            ROBO_CHECK_EQ_INT(robo_css_color_is_valid(
                                  robo_paint_operation_color(&operation)),
                              1);
        }

        paint_local_boxes_match_layout(&fixture, operation);
    }

    /* The card draws a background and four borders; the three boxes contribute three
     * structural triples. So structural = 9 and drawing = 5. */
    ROBO_CHECK_EQ_INT((int)structural, 9);
    ROBO_CHECK_EQ_INT((int)drawing, 5);

    /* A `CONTENT` record is structural and not a box marker -- the distinction the
     * predicates draw, asked once against a real record. */
    {
        robo_paint_operation_t content;

        ROBO_CHECK_EQ_INT(records_op_of(&result, ROBO_PAINT_OP_CONTENT, card, &content), 1);
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_structural(&content), 1);
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_marker(&content), 0);
    }

    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. Ordering                                                         */
/* ------------------------------------------------------------------ */

static void test_records_nested_order(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t html_box;
    robo_layout_node_id_t body_box;
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;

    /* A four-deep chain: html > body > outer > inner. Each level's content boundary must
     * precede the next level's opening marker, so the nesting is recoverable from the
     * record sequence alone -- which is the property a flat list with depths exists to
     * have. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\">"
                         "<div class=\"inner\"></div></div></body></html>",
                         RECORDS_BASE_CSS
                         " .outer { width: 200px; height: 100px;"
                         "   border: 1px solid #000000 }"
                         " .inner { width: 40px; height: 20px;"
                         "   background-color: #ff0000 }");
    html_box = paint_box_by_tag(&fixture, "html");
    body_box = paint_box_by_tag(&fixture, "body");
    outer = paint_box(&fixture, "outer");
    inner = paint_box(&fixture, "inner");

    paint_once(&fixture, &result);

    /* Four boxes' structural triples (12) plus `outer`'s four border sides and `inner`'s
     * background (5): the records that draw, in their declared order. */
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_CONTENT), 4);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(result.list), 17);

    /* The chain, in order: every parent's `CONTENT` before the next child's `BEGIN_BOX`. */
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, html_box) <
               records_index_of(&result, ROBO_PAINT_OP_CONTENT, html_box));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_CONTENT, html_box) <
               records_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, body_box));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_CONTENT, body_box) <
               records_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, outer));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_CONTENT, outer) <
               records_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, inner));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_CONTENT, inner) <
               records_index_of(&result, ROBO_PAINT_OP_END_BOX, inner));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_END_BOX, inner) <
               records_index_of(&result, ROBO_PAINT_OP_END_BOX, outer));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_END_BOX, outer) <
               records_index_of(&result, ROBO_PAINT_OP_END_BOX, body_box));

    /* And the depth of each level is its position in the chain, which is what a
     * rasteriser pushes and pops a clip stack by. */
    {
        robo_paint_operation_t operation;

        ROBO_CHECK_EQ_INT(records_op_of(&result, ROBO_PAINT_OP_BEGIN_BOX, html_box,
                                        &operation),
                          1);
        ROBO_CHECK_EQ_INT((int)operation.depth, 0);
        ROBO_CHECK_EQ_INT(records_op_of(&result, ROBO_PAINT_OP_BEGIN_BOX, body_box,
                                        &operation),
                          1);
        ROBO_CHECK_EQ_INT((int)operation.depth, 1);
        ROBO_CHECK_EQ_INT(records_op_of(&result, ROBO_PAINT_OP_BEGIN_BOX, outer,
                                        &operation),
                          1);
        ROBO_CHECK_EQ_INT((int)operation.depth, 2);
        ROBO_CHECK_EQ_INT(records_op_of(&result, ROBO_PAINT_OP_BEGIN_BOX, inner,
                                        &operation),
                          1);
        ROBO_CHECK_EQ_INT((int)operation.depth, 3);
    }

    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_records_siblings_order(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t first;
    robo_layout_node_id_t second;
    robo_layout_node_id_t third;

    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"a\"></div><div class=\"b\"></div><div class=\"c\"></div>"
                         "</body></html>",
                         RECORDS_BASE_CSS
                         " .a, .b, .c { width: 20px; height: 10px }");
    first = paint_box(&fixture, "a");
    second = paint_box(&fixture, "b");
    third = paint_box(&fixture, "c");

    paint_once(&fixture, &result);

    /* Siblings in DOM order, each with its own structural triple, none interleaved. */
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, first) <
               records_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, second));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, second) <
               records_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, third));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_END_BOX, first) <
               records_index_of(&result, ROBO_PAINT_OP_BEGIN_BOX, second));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_CONTENT, first) <
               records_index_of(&result, ROBO_PAINT_OP_END_BOX, first));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_CONTENT, second) <
               records_index_of(&result, ROBO_PAINT_OP_END_BOX, second));
    ROBO_CHECK(records_index_of(&result, ROBO_PAINT_OP_CONTENT, third) <
               records_index_of(&result, ROBO_PAINT_OP_END_BOX, third));
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_CONTENT), 5);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(result.list), 15);

    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 5. Mutation isolation                                               */
/* ------------------------------------------------------------------ */

static void test_records_snapshot_isolation(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    paint_result_t after;
    robo_paint_operation_t saved[32];
    size_t saved_count;
    size_t index;
    const robo_paint_operation_t *ops;

    /* A block with no declared width fills its containing block, so its geometry follows
     * the viewport -- which makes a second layout pass at a different width an observable
     * mutation rather than a no-op. */
    paint_begin(&fixture,
                "<html><body><div class=\"fill\"></div></body></html>",
                RECORDS_BASE_CSS " .fill { height: 25px; background-color: #00ff00 }");
    paint_layout(&fixture, 800, 600);
    paint_once(&fixture, &result);

    ops = robo_paint_list_operations(result.list);
    saved_count = robo_paint_list_count(result.list);
    ROBO_CHECK(saved_count > 0u);
    ROBO_CHECK(saved_count <= sizeof(saved) / sizeof(saved[0]));
    for (index = 0u; index < saved_count; index++) {
        saved[index] = ops[index];
    }

    /* Mutate the layout state the records were built from: lay the *same* tree out at a
     * different viewport and paint again into a second list. */
    paint_layout(&fixture, 400, 600);
    paint_once(&fixture, &after);

    /* The first list's records did not move: a record is a snapshot, not a view onto
     * mutable layout state. */
    for (index = 0u; index < saved_count; index++) {
        robo_paint_operation_t reread;

        ROBO_CHECK_EQ_INT(robo_paint_list_at(result.list, index, &reread), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_operation_equal(&saved[index], &reread), 1);
    }

    /* And the mutation was real: the second pass produced different geometry, which is
     * what makes the isolation above a statement about records rather than about the
     * mutation never having happened. */
    ROBO_CHECK_EQ_INT(robo_paint_list_equal(result.list, after.list), 0);

    paint_result_dispose(&after);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. Ownership                                                        */
/* ------------------------------------------------------------------ */

static void test_records_ownership(void)
{
    robo_paint_operation_t operation;
    robo_paint_operation_t read;
    robo_paint_list_t *list = NULL;
    robo_paint_list_t *copy = NULL;
    size_t capacity;

    paint_fixture();

    /* A hand-built record is the one place a suite fabricates rather than paints, and
     * here it is exactly the point: the question is about the *container's* ownership. */
    robo_paint_operation_init(&operation);
    operation.kind = ROBO_PAINT_OP_CONTENT;
    operation.node = (robo_layout_node_id_t)1u;
    operation.depth = 2u;
    operation.boxes.border.x = 5;
    operation.boxes.padding.x = 5;
    operation.boxes.content.x = 5;

    ROBO_CHECK_EQ_INT(robo_paint_list_create(&list, NULL), ROBO_OK);
    capacity = robo_paint_list_capacity(list);
    ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &operation, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(list), 1);

    /* Append copies by value: changing the argument afterwards leaves the stored record
     * exactly as it was. */
    operation.node = (robo_layout_node_id_t)999u;
    operation.boxes.border.x = -1234;
    ROBO_CHECK_EQ_INT(robo_paint_list_at(list, 0u, &read), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)read.node, 1);
    ROBO_CHECK_EQ_INT(read.boxes.border.x, 5);

    /* A second list is independent: destroying it does not touch the first. */
    ROBO_CHECK_EQ_INT(robo_paint_list_create(&copy, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_append(copy, &read, NULL), ROBO_OK);
    robo_paint_list_destroy(copy);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(list), 1);

    /* `clear` keeps the storage and drops every record. */
    robo_paint_list_clear(list);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(list), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_capacity(list), (int)capacity);
    ROBO_CHECK(robo_paint_list_operations(list) == NULL);

    /* And the container refuses a record that is not one, rather than storing it. */
    robo_paint_operation_init(&operation);
    operation.kind = ROBO_PAINT_OP_END_BOX;
    operation.node = (robo_layout_node_id_t)1u;
    operation.color = robo_css_color_make(1, 2, 3, 255); /* a structural record with a colour */
    ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &operation, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(list), 0);

    /* Empty cleanup and a NULL list are both safe. */
    robo_paint_list_destroy(list);
    robo_paint_list_destroy(NULL);
    robo_paint_list_clear(NULL);
}

/* ------------------------------------------------------------------ */
/* 7. Empty and degenerate cases                                       */
/* ------------------------------------------------------------------ */

static void test_records_empty_cases(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_list_t *empty = NULL;
    char buffer[256];
    size_t length = 0u;

    /* A document whose body is empty still lays out `html` and `body`, so its records
     * are two structural triples and nothing that draws. */
    paint_begin_laid_out(&fixture, "<html><body></body></html>", RECORDS_BASE_CSS);
    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_CONTENT), 2);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BACKGROUND), 0);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BORDER_TOP), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(result.list), 6);
    paint_result_dispose(&result);
    paint_end(&fixture);

    /* A zero-sized box: its records exist and every geometry is a valid empty rectangle. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"zero\"></div></body></html>",
                         RECORDS_BASE_CSS " .zero { width: 0; height: 0 }");
    paint_once(&fixture, &result);
    {
        size_t index;

        for (index = 0u; index < robo_paint_list_count(result.list); index++) {
            robo_paint_operation_t operation;

            ROBO_CHECK_EQ_INT(robo_paint_list_at(result.list, index, &operation), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 1);
        }
    }
    paint_result_dispose(&result);
    paint_end(&fixture);

    /* An empty list dumps as an empty buffer of length 0 -- a legitimate answer, not a
     * placeholder line. */
    robo_paint_list_create(&empty, NULL);
    memset(buffer, 0x7f, sizeof(buffer));
    ROBO_CHECK_EQ_INT(robo_paint_list_write(empty, buffer, sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)length, 0);
    ROBO_CHECK_EQ_INT((int)buffer[0], 0);
    robo_paint_list_destroy(empty);
}

/* ------------------------------------------------------------------ */
/* 8. Geometry safety                                                  */
/* ------------------------------------------------------------------ */

static void test_records_geometry_safety(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    size_t index;

    /* A box at the geometry type's maximum with a border that must not wrap it, a box
     * with a zero extent, and a nested pair whose offsets accumulate. None may produce a
     * record with a negative extent, an un-nested set of boxes, or an unrepresentable
     * coordinate. */
    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"wide\"></div>"
                         "<div class=\"zero\"></div>"
                         "<div class=\"outer\"><div class=\"inner\"></div></div>"
                         "</body></html>",
                         RECORDS_BASE_CSS
                         " .wide { width: 2147483647px; height: 10px;"
                         "   border: 5px solid #000000; background-color: #ffffff }"
                         " .zero { width: 0; height: 0;"
                         "   padding: 4px; border: 3px solid #000000 }"
                         " .outer { width: 100px; height: 60px; margin: 9px;"
                         "   padding: 6px; border: 2px solid #000000 }"
                         " .inner { width: 20px; height: 20px; margin: 3px }");

    paint_once(&fixture, &result);

    ROBO_CHECK(robo_paint_list_count(result.list) > 0u);
    for (index = 0u; index < robo_paint_list_count(result.list); index++) {
        robo_paint_operation_t operation;

        ROBO_CHECK_EQ_INT(robo_paint_list_at(result.list, index, &operation), ROBO_OK);
        /* The container's rule, asked over an extreme document: every stored record is
         * one, which is three valid nested boxes with non-negative extents. */
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 1);
        ROBO_CHECK(operation.boxes.border.width >= 0);
        ROBO_CHECK(operation.boxes.border.height >= 0);
        ROBO_CHECK(operation.boxes.padding.width >= 0);
        ROBO_CHECK(operation.boxes.content.width >= 0);
        ROBO_CHECK(operation.boxes.content.height >= 0);
        /* A border record's band is finite and non-negative on both axes. */
        if (robo_paint_op_kind_is_border(operation.kind)) {
            robo_layout_rect_t band;

            ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(&operation, &band, NULL),
                              ROBO_OK);
            ROBO_CHECK(band.width >= 0);
            ROBO_CHECK(band.height >= 0);
        }
        paint_local_boxes_match_layout(&fixture, operation);
    }

    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 9. Determinism                                                      */
/* ------------------------------------------------------------------ */

static void test_records_determinism(void)
{
    paint_fixture_doc_t fixture;
    paint_fixture_doc_t rebuilt;
    paint_result_t first;
    paint_result_t second;
    char dump_first[4096];
    char dump_second[4096];
    size_t length_first;
    size_t length_second;

    const char *html =
        "<html><body>"
        "<div class=\"card\"><section class=\"badge\">New</section></div>"
        "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
        "</body></html>";
    const char *css =
        RECORDS_BASE_CSS
        " .card, .clip { width: 120px; height: 40px;"
        "   border: 1px solid #000000; background-color: #ffffff }"
        " .badge { width: 20px; height: 10px; background-color: #123456 }"
        " .clip { overflow: hidden }"
        " .spill { width: 400px; height: 400px; border: 2px solid #ff0000 }";

    /* The same document, painted twice from one tree... */
    paint_begin_laid_out(&fixture, html, css);
    paint_once(&fixture, &first);
    paint_once(&fixture, &second);
    ROBO_CHECK_EQ_INT(robo_paint_list_equal(first.list, second.list), 1);
    length_first = records_dump(&first, dump_first, sizeof(dump_first));
    length_second = records_dump(&second, dump_second, sizeof(dump_second));
    ROBO_CHECK_EQ_INT((int)length_first, (int)length_second);
    ROBO_CHECK_EQ_INT(strcmp(dump_first, dump_second), 0);
    paint_result_dispose(&second);
    paint_result_dispose(&first);
    paint_end(&fixture);

    /* ...and the same document built into a *second* tree, which is the stronger claim:
     * a conclusion that depended on a pointer, an allocation or an iteration order would
     * differ between two independently built trees. */
    paint_begin_laid_out(&rebuilt, html, css);
    paint_once(&rebuilt, &second);
    length_second = records_dump(&second, dump_second, sizeof(dump_second));

    paint_begin_laid_out(&fixture, html, css);
    paint_once(&fixture, &first);
    length_first = records_dump(&first, dump_first, sizeof(dump_first));

    ROBO_CHECK_EQ_INT(robo_paint_list_equal(first.list, second.list), 1);
    ROBO_CHECK_EQ_INT(strcmp(dump_first, dump_second), 0);

    paint_result_dispose(&second);
    paint_result_dispose(&first);
    paint_end(&fixture);
    paint_end(&rebuilt);
}

/* ------------------------------------------------------------------ */
/* 10. The dump                                                        */
/* ------------------------------------------------------------------ */

static void test_records_dump(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    char buffer[8192];
    size_t length;
    size_t lines = 0u;
    const char *scan;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         RECORDS_BASE_CSS
                         " .card { width: 100px; height: 40px;"
                         "   background-color: #ff0000; border: 1px solid #000000 }");
    paint_once(&fixture, &result);

    length = records_dump(&result, buffer, sizeof(buffer));
    ROBO_CHECK(length > 0u);

    /* One line per record, and the content boundary is one of them -- naming the
     * rectangle it acts on so a reader can tell it from a marker. */
    scan = buffer;
    while (*scan != '\0') {
        if (*scan == '\n') {
            lines++;
        }
        scan++;
    }
    ROBO_CHECK_EQ_INT((int)lines, (int)robo_paint_list_count(result.list));
    ROBO_CHECK(strstr(buffer, "content") != NULL);
    ROBO_CHECK(strstr(buffer, " content=") != NULL);
    ROBO_CHECK(strstr(buffer, "begin-box") != NULL);
    ROBO_CHECK(strstr(buffer, "background") != NULL);
    ROBO_CHECK(strstr(buffer, "border-top") != NULL);
    ROBO_CHECK(strstr(buffer, "end-box") != NULL);

    /* The dump is deterministic and a refused buffer reports the length it needed. */
    {
        char again[8192];
        size_t again_length;

        again_length = records_dump(&result, again, sizeof(again));
        ROBO_CHECK_EQ_INT((int)length, (int)again_length);
        ROBO_CHECK_EQ_INT(strcmp(buffer, again), 0);

        {
            char tiny[8];
            size_t needed = 0u;

            ROBO_CHECK_EQ_INT(robo_paint_list_write(result.list, tiny, sizeof(tiny),
                                                    &needed, NULL),
                              ROBO_NOT_SUPPORTED);
            ROBO_CHECK_EQ_INT((int)needed, (int)length);
        }
    }

    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 11. Allocation balance                                              */
/* ------------------------------------------------------------------ */

static void test_records_allocator_balance(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    size_t before;
    size_t after;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         RECORDS_BASE_CSS
                         " .card { width: 10px; height: 10px;"
                         "   background-color: #fff; border: 1px solid #000 }");

    before = paint_live_blocks();
    paint_once(&fixture, &result);
    /* The list is disposed before the second measurement, because its storage is the
     * module's *only* allocation and the balance is the question: a pass that leaked a
     * per-record block would show here rather than only under a sanitizer. */
    paint_result_dispose(&result);
    after = paint_live_blocks();
    ROBO_CHECK_EQ_INT((int)after, (int)before);

    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_paint_records(void)
{
    robo_test_register("paint.records.content_sequence", test_records_content_sequence);
    robo_test_register("paint.records.structural_unconditional",
                       test_records_structural_unconditional);
    robo_test_register("paint.records.content_geometry", test_records_content_geometry);
    robo_test_register("paint.records.fields", test_records_fields);
    robo_test_register("paint.records.nested_order", test_records_nested_order);
    robo_test_register("paint.records.siblings_order", test_records_siblings_order);
    robo_test_register("paint.records.snapshot_isolation", test_records_snapshot_isolation);
    robo_test_register("paint.records.ownership", test_records_ownership);
    robo_test_register("paint.records.empty_cases", test_records_empty_cases);
    robo_test_register("paint.records.geometry_safety", test_records_geometry_safety);
    robo_test_register("paint.records.determinism", test_records_determinism);
    robo_test_register("paint.records.dump", test_records_dump);
    robo_test_register("paint.records.allocator_balance", test_records_allocator_balance);
}
