/*
 * RoboPaint unit suite (Phase 11.1) — paint.*
 *
 * The half of the phase's tests that asks each piece **on its own**: the two
 * vocabularies and their tables, the diagnostic record, the three-box derivation and the
 * clip record, the operation's validity rule and its two coordinate spaces, the list and
 * its ownership, and the dump's format. The other half — `SuiteRoboPaintTraversal.c` —
 * asks the same pieces through a real document, and the two are deliberately not the
 * same suite because they catch different things: a hand-built record catches an
 * arithmetic or a rule that a document would produce by accident, and a document catches
 * a rule that is right in isolation and wrong when a box is nested four deep.
 *
 * Nothing here fabricates a *layout result*: the hand-built records are the module's own
 * value types (a border box, an inset record, a clip), which are inputs a caller may
 * legitimately construct -- and the layout suite that produces them for real is
 * `layout.tests`' business, not this one's.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite
 * depends on no Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Hand-built records, for the arithmetic                              */
/* ------------------------------------------------------------------ */

/** A used border record: the four widths, every side generating a border. */
static robo_layout_border_edges_t paint_border_edges(int32_t top,
                                                     int32_t right,
                                                     int32_t bottom,
                                                     int32_t left)
{
    robo_layout_borders_t declared;
    robo_layout_border_edges_t edges;

    robo_layout_borders_init(&declared);
    declared.top.width = robo_layout_border_width_resolved(top);
    declared.top.style = ROBO_CSS_BORDER_STYLE_SOLID;
    declared.right.width = robo_layout_border_width_resolved(right);
    declared.right.style = ROBO_CSS_BORDER_STYLE_SOLID;
    declared.bottom.width = robo_layout_border_width_resolved(bottom);
    declared.bottom.style = ROBO_CSS_BORDER_STYLE_SOLID;
    declared.left.width = robo_layout_border_width_resolved(left);
    declared.left.style = ROBO_CSS_BORDER_STYLE_SOLID;
    memset(&edges, 0, sizeof(edges));
    ROBO_CHECK_EQ_INT(robo_layout_border_resolve(&declared, &edges, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_is_valid(&edges), 1);
    return edges;
}

/** A padding record: four used lengths. */
static robo_layout_padding_edges_t paint_padding_edges(int32_t top,
                                                       int32_t right,
                                                       int32_t bottom,
                                                       int32_t left)
{
    robo_layout_padding_edges_t edges;

    memset(&edges, 0, sizeof(edges));
    edges.top = top;
    edges.right = right;
    edges.bottom = bottom;
    edges.left = left;
    edges.resolved = 4u;
    edges.unresolved = 0u;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_is_valid(&edges), 1);
    return edges;
}

/** A valid three-box record for a border box with the given insets. */
static robo_paint_boxes_t paint_boxes_make(int32_t x, int32_t y, int32_t w, int32_t h,
                                           int32_t border, int32_t padding)
{
    robo_layout_border_edges_t borders = paint_border_edges(border, border, border, border);
    robo_layout_padding_edges_t paddings = paint_padding_edges(padding, padding, padding,
                                                               padding);
    robo_layout_rect_t box;
    robo_paint_boxes_t boxes;

    box.x = x;
    box.y = y;
    box.width = w;
    box.height = h;
    memset(&boxes, 0, sizeof(boxes));
    ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(&box, &borders, &paddings, &boxes, NULL),
                      ROBO_OK);
    return boxes;
}

/** A `BEGIN_BOX` operation with the given node and boxes: the simplest valid marker. */
static robo_paint_operation_t paint_marker_make(robo_layout_node_id_t node,
                                               size_t depth,
                                               robo_paint_boxes_t boxes)
{
    robo_paint_operation_t operation;

    robo_paint_operation_init(&operation);
    operation.kind = ROBO_PAINT_OP_BEGIN_BOX;
    operation.node = node;
    operation.depth = depth;
    operation.boxes = boxes;
    return operation;
}

/** A drawing operation with the given kind and colour. */
static robo_paint_operation_t paint_drawing_make(robo_paint_op_kind_t kind,
                                                robo_layout_node_id_t node,
                                                robo_paint_boxes_t boxes,
                                                robo_css_color_t color)
{
    robo_paint_operation_t operation;

    robo_paint_operation_init(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = 0u;
    operation.boxes = boxes;
    operation.color = color;
    /* The style rule's own shape (11.5): a hand-built drawing carries the style its kind
     * requires -- `solid` for a border side, because a side's band is its shape, and `none`
     * for a background, which has no side to style. A record is refused otherwise, which is
     * the assertion `test_paint_operation_record` makes on both halves. */
    operation.style = robo_paint_op_kind_is_border(kind) ? ROBO_CSS_BORDER_STYLE_SOLID
                                                         : ROBO_CSS_BORDER_STYLE_NONE;
    return operation;
}

/* ------------------------------------------------------------------ */
/* 1. The module                                                       */
/* ------------------------------------------------------------------ */

static void test_paint_module_lifecycle(void)
{
    robo_i32 major = -1;
    robo_i32 minor = -1;
    robo_i32 patch = -1;
    robo_paint_capabilities_t capabilities;

    paint_fixture();

    ROBO_CHECK_STR_EQ(robo_paint_version_string(), "0.6.0");
    ROBO_CHECK_EQ_INT(robo_paint_get_version(&major, &minor, &patch), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)major, 0);
    ROBO_CHECK_EQ_INT((int)minor, 6);
    ROBO_CHECK_EQ_INT((int)patch, 0);
    /* The three-way agreement the version header promises: the constant, the door and
     * the capability field are one statement -- and since 11.4 (backgrounds), 11.5
     * (border bands) and 11.6 (text runs) all three say a drawing reaches a surface. */
    ROBO_CHECK_EQ_INT(robo_paint_drawing_is_implemented(), 1);
    ROBO_CHECK_EQ_INT(ROBO_PAINT_DRAWING_IMPLEMENTED, 1);
    ROBO_CHECK_EQ_INT(ROBO_PAINT_API_FROZEN, 0);
    ROBO_CHECK_EQ_INT(robo_paint_get_capabilities(&capabilities), ROBO_OK);
    ROBO_CHECK_EQ_INT(capabilities.drawing, ROBO_PAINT_DRAWING_IMPLEMENTED);

    ROBO_CHECK_EQ_INT(robo_paint_get_version(NULL, &minor, &patch), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_get_version(&major, NULL, &patch), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_get_version(&major, &minor, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_get_capabilities(NULL), ROBO_INVALID_ARGUMENT);

    /* The lifecycle: init is idempotent-except-that-it-says-so, and shutdown is safe
     * twice. The module owns no state, which is why neither can fail for an allocation
     * reason -- and why a caller should not have to know that. */
    ROBO_CHECK_EQ_INT(robo_paint_is_initialized(), 1);
    ROBO_CHECK_EQ_INT(robo_paint_init(), ROBO_ALREADY_EXISTS);
    robo_paint_shutdown();
    ROBO_CHECK_EQ_INT(robo_paint_is_initialized(), 0);
    robo_paint_shutdown();
    ROBO_CHECK_EQ_INT(robo_paint_is_initialized(), 0);
    ROBO_CHECK_EQ_INT(robo_paint_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_is_initialized(), 1);
}

static void test_paint_kind_vocabulary(void)
{
    static const char *const expected[ROBO_PAINT_OP_KIND_COUNT] = {
        "none", "begin-box", "background", "border-top", "border-right", "border-bottom",
        "border-left", "content", "text", "end-box"
    };
    size_t index;

    paint_fixture();

    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_OP_KIND_COUNT, 10);
    /* The zero of the vocabulary is `NONE`, so a cleared record is not an operation --
     * the same decision RoboLayout's border width kinds make one module down. */
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_OP_NONE, 0);
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_OP_BEGIN_BOX, 1);
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_OP_BACKGROUND, 2);
    /* `CONTENT` sits between the last decoration and the closing marker, because the
     * enumerators are in the order a linear consumer meets them (RoboPaintTypes.h). */
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_OP_CONTENT, 7);
    /* 11.6's `TEXT` is a drawing and not a marker, so it sits with the decorations
     * and before the closing marker. */
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_OP_TEXT, 8);
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_OP_END_BOX, 9);

    for (index = 0u; index < (size_t)ROBO_PAINT_OP_KIND_COUNT; index++) {
        robo_paint_op_kind_t kind = (robo_paint_op_kind_t)index;

        ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_known(kind), 1);
        ROBO_CHECK_STR_EQ(robo_paint_op_kind_name(kind), expected[index]);
        ROBO_CHECK(robo_paint_op_kind_name(kind) != NULL);
        /* The structural/drawing split, asserted where the vocabulary is: every kind
         * above `NONE` either puts a pixel down or is structural, and never both. */
        if (kind == ROBO_PAINT_OP_NONE) {
            ROBO_CHECK_EQ_INT(robo_paint_op_kind_draws(kind), 0);
            ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_structural(kind), 0);
        } else {
            ROBO_CHECK_EQ_INT(robo_paint_op_kind_draws(kind) +
                                  robo_paint_op_kind_is_structural(kind),
                              1);
        }
    }

    /* Every kind prints its own name: a table with a duplicated row would pass the walk
     * above and be wrong, and this is the cheap check that catches it. */
    for (index = 0u; index < (size_t)ROBO_PAINT_OP_KIND_COUNT; index++) {
        size_t other;

        for (other = index + 1u; other < (size_t)ROBO_PAINT_OP_KIND_COUNT; other++) {
            ROBO_CHECK(strcmp(robo_paint_op_kind_name((robo_paint_op_kind_t)index),
                              robo_paint_op_kind_name((robo_paint_op_kind_t)other)) != 0);
        }
    }

    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_marker(ROBO_PAINT_OP_BEGIN_BOX), 1);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_marker(ROBO_PAINT_OP_END_BOX), 1);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_marker(ROBO_PAINT_OP_BACKGROUND), 0);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_marker(ROBO_PAINT_OP_BORDER_TOP), 0);

    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_border(ROBO_PAINT_OP_BORDER_TOP), 1);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_border(ROBO_PAINT_OP_BORDER_RIGHT), 1);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_border(ROBO_PAINT_OP_BORDER_BOTTOM), 1);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_border(ROBO_PAINT_OP_BORDER_LEFT), 1);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_border(ROBO_PAINT_OP_BACKGROUND), 0);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_border(ROBO_PAINT_OP_BEGIN_BOX), 0);

    /* The side is *derived* from the kind, so no operation can carry a side that
     * disagrees with what it draws -- and every other kind is a refusal rather than a
     * sentinel, which is what removing the `side` field bought. */
    {
        robo_css_box_side_t side = ROBO_CSS_BOX_SIDE_LEFT;
        robo_paint_op_kind_t border_kinds[ROBO_CSS_BOX_SIDE_COUNT] = {
            ROBO_PAINT_OP_BORDER_TOP, ROBO_PAINT_OP_BORDER_RIGHT,
            ROBO_PAINT_OP_BORDER_BOTTOM, ROBO_PAINT_OP_BORDER_LEFT
        };

        for (index = 0u; index < (size_t)ROBO_CSS_BOX_SIDE_COUNT; index++) {
            ROBO_CHECK_EQ_INT(robo_paint_op_kind_side(border_kinds[index], &side), ROBO_OK);
            ROBO_CHECK_EQ_INT((int)side, (int)index);
        }
        ROBO_CHECK_EQ_INT(robo_paint_op_kind_side(ROBO_PAINT_OP_BACKGROUND, &side),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_paint_op_kind_side(ROBO_PAINT_OP_BEGIN_BOX, &side),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_paint_op_kind_side(ROBO_PAINT_OP_BORDER_TOP, NULL),
                          ROBO_INVALID_ARGUMENT);
    }

    /* An undefined kind is not known, has no name, and is neither a marker nor a border
     * -- the three answers a caller gets instead of a crash. */
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_known((robo_paint_op_kind_t)99), 0);
    ROBO_CHECK_STR_EQ(robo_paint_op_kind_name((robo_paint_op_kind_t)99), "unknown");
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_marker((robo_paint_op_kind_t)99), 0);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_border((robo_paint_op_kind_t)99), 0);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_known((robo_paint_op_kind_t)-1), 0);
}

static void test_paint_domain_vocabulary(void)
{
    static const char *const expected[ROBO_PAINT_DOMAIN_COUNT] = {
        "none", "module", "context", "operation", "list", "geometry", "traversal", "clip",
        "dump", "display-list"
    };
    size_t index;

    paint_fixture();

    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_DOMAIN_COUNT, 10);
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_DOMAIN_NONE, 0);
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_DOMAIN_GEOMETRY, 5);
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_DOMAIN_DUMP, 8);
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_DOMAIN_DISPLAY_LIST, 9);
    for (index = 0u; index < (size_t)ROBO_PAINT_DOMAIN_COUNT; index++) {
        ROBO_CHECK_EQ_INT(robo_paint_domain_is_known((robo_paint_diagnostic_domain_t)index), 1);
        ROBO_CHECK_STR_EQ(robo_paint_domain_name((robo_paint_diagnostic_domain_t)index),
                          expected[index]);
    }
    ROBO_CHECK_EQ_INT(robo_paint_domain_is_known((robo_paint_diagnostic_domain_t)99), 0);
    ROBO_CHECK_STR_EQ(robo_paint_domain_name((robo_paint_diagnostic_domain_t)99), "unknown");
}

static void test_paint_diagnostics_record(void)
{
    robo_paint_diag_t diag;

    paint_fixture();

    robo_paint_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_OK);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_PAINT_DOMAIN_NONE);
    ROBO_CHECK(diag.message == NULL);
    ROBO_CHECK(diag.file == NULL);
    ROBO_CHECK_EQ_INT(diag.line, 0);

    /* A success clears rather than stores, so a caller may reuse one record across a
     * sequence of calls and read only the last failure. */
    ROBO_CHECK_EQ_INT(robo_paint_diag_set(&diag, ROBO_OK, ROBO_PAINT_DOMAIN_LIST, "ignored",
                                          "f.c", 1),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_OK);
    ROBO_CHECK(diag.message == NULL);

    ROBO_CHECK_EQ_INT(robo_paint_diag_set(&diag, ROBO_OUT_OF_MEMORY, ROBO_PAINT_DOMAIN_LIST,
                                          "could not grow", "f.c", 42),
                      ROBO_OUT_OF_MEMORY);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_OUT_OF_MEMORY);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_PAINT_DOMAIN_LIST);
    ROBO_CHECK_STR_EQ(diag.message, "could not grow");
    ROBO_CHECK_STR_EQ(diag.file, "f.c");
    ROBO_CHECK_EQ_INT(diag.line, 42);

    /* An undefined domain is normalised to NONE rather than stored, so a reader never
     * sees a domain that has no name. */
    ROBO_CHECK_EQ_INT(robo_paint_diag_set(&diag, ROBO_ERROR,
                                          (robo_paint_diagnostic_domain_t)77, "bad", "f.c", 3),
                      ROBO_ERROR);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_PAINT_DOMAIN_NONE);

    /* The macro form is what the module's own call sites use: it always yields the code
     * and tolerates a NULL sink, because diagnostics are optional and the result is
     * authoritative. */
    ROBO_CHECK_EQ_INT(ROBO_PAINT_DIAG(NULL, ROBO_PAINT_DOMAIN_CONTEXT, ROBO_NOT_FOUND, "x"),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(ROBO_PAINT_DIAG(&diag, ROBO_PAINT_DOMAIN_CONTEXT, ROBO_NOT_FOUND, "x"),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_PAINT_DOMAIN_CONTEXT);

    ROBO_CHECK_EQ_INT(robo_paint_diag_set(NULL, ROBO_ERROR, ROBO_PAINT_DOMAIN_NONE, "x",
                                          "f.c", 1),
                      ROBO_INVALID_ARGUMENT);
    robo_paint_diag_reset(NULL); /* safe */
}

/* ------------------------------------------------------------------ */
/* 2. The three boxes                                                  */
/* ------------------------------------------------------------------ */

static void test_paint_boxes_record(void)
{
    robo_paint_boxes_t boxes;
    robo_paint_boxes_t other;

    paint_fixture();

    robo_paint_boxes_init(&boxes);
    ROBO_CHECK_EQ_INT(boxes.insets_measured, 0);
    ROBO_CHECK_EQ_INT(boxes.border.width, 0);
    ROBO_CHECK_EQ_INT(boxes.padding.width, 0);
    ROBO_CHECK_EQ_INT(boxes.content.width, 0);
    /* A cleared record is a *record*: three equal empty rectangles with no insets. */
    ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&boxes), 1);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_equal(&boxes, &boxes), 1);

    other = paint_boxes_make(10, 20, 100, 50, 2, 5);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_equal(&boxes, &other), 0);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&other), 1);

    /* A record whose layers cross is not a record: the three are nested by
     * construction, so a padding box outside its border box is arithmetic that went
     * wrong one level down. */
    {
        robo_paint_boxes_t crossed = other;

        crossed.padding.x = 0;
        crossed.padding.width = 1000;
        ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&crossed), 0);
    }
    /* And a record whose flag disagrees with its geometry is not one either: a box whose
     * insets were not measured has one rectangle wearing three names. */
    {
        robo_paint_boxes_t unmeasured = other;

        unmeasured.insets_measured = 0;
        ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&unmeasured), 0);
    }
    ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_equal(&other, NULL), 0);
    robo_paint_boxes_init(NULL); /* safe */
}

static void test_paint_boxes_from_layers(void)
{
    robo_layout_border_edges_t borders = paint_border_edges(2, 2, 2, 2);
    robo_layout_padding_edges_t paddings = paint_padding_edges(5, 5, 5, 5);
    robo_layout_padding_edges_t none_padding = paint_padding_edges(0, 0, 0, 0);
    robo_layout_rect_t box;
    robo_paint_boxes_t boxes;

    paint_fixture();
    box.x = 10;
    box.y = 20;
    box.width = 100;
    box.height = 50;
    memset(&boxes, 0, sizeof(boxes));

    ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(&box, &borders, &paddings, &boxes, NULL),
                      ROBO_OK);
    /* border -> padding -> content, geometrically distinct and each inset from the one
     * outside it. These are the numbers a paint pass draws bands between, so an error
     * here is a border in the wrong place rather than a missing one. */
    paint_rect_is(10, 20, 100, 50, boxes.border, "the border box");
    paint_rect_is(12, 22, 96, 46, boxes.padding, "the padding box");
    paint_rect_is(17, 27, 86, 36, boxes.content, "the content box");
    ROBO_CHECK_EQ_INT(boxes.insets_measured, 1);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&boxes), 1);

    /* No insets: three rectangles that agree, and the flag *still* set -- the author's
     * declarations were read and came to nothing, which is a different statement from
     * "this box has no layers to measure". */
    {
        robo_layout_border_edges_t none_borders = paint_border_edges(0, 0, 0, 0);

        ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(&box, &none_borders, &none_padding,
                                                       &boxes, NULL),
                          ROBO_OK);
        paint_rect_is(10, 20, 100, 50, boxes.padding, "an unchanged padding box");
        paint_rect_is(10, 20, 100, 50, boxes.content, "an unchanged content box");
        ROBO_CHECK_EQ_INT(boxes.insets_measured, 1);
    }

    /* A box at the edge of the plane: the origins saturate and no extent goes negative,
     * which is the rule Phase 10.15 found the cost of getting wrong. */
    {
        robo_layout_rect_t saturated;
        robo_layout_border_edges_t wide = paint_border_edges(5, 5, 5, 5);

        saturated.x = INT32_MAX;
        saturated.y = INT32_MAX;
        saturated.width = INT32_MAX;
        saturated.height = INT32_MAX;
        ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(&saturated, &wide, &none_padding,
                                                       &boxes, NULL),
                          ROBO_OK);
        ROBO_CHECK(boxes.padding.x >= 0);
        ROBO_CHECK(boxes.padding.width >= 0);
        ROBO_CHECK(boxes.content.width >= 0);
        ROBO_CHECK(boxes.content.height >= 0);
        ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&boxes), 1);
    }

    /* A border wider than the box leaves no room for the padding box, and a negative
     * width is not a width: the floor is applied to the result. */
    {
        robo_layout_rect_t thin;
        robo_layout_border_edges_t thick = paint_border_edges(60, 60, 60, 60);

        thin.x = 0;
        thin.y = 0;
        thin.width = 100;
        thin.height = 50;
        ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(&thin, &thick, &none_padding, &boxes,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(boxes.padding.width, 0);
        ROBO_CHECK_EQ_INT(boxes.padding.height, 0);
        ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&boxes), 1);
    }

    /* The refusals: NULL arguments, a negative-size border box, and an inset record that
     * is not one (a hand-built record with counters that disagree with four sides). */
    ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(NULL, &borders, &paddings, &boxes, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(&box, NULL, &paddings, &boxes, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(&box, &borders, NULL, &boxes, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(&box, &borders, &paddings, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        robo_layout_rect_t negative = box;

        negative.width = -1;
        ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(&negative, &borders, &paddings,
                                                       &boxes, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
    {
        robo_layout_padding_edges_t broken = paddings;

        broken.resolved = 9u; /* counters that disagree with two axes */
        ROBO_CHECK(robo_paint_boxes_from_layers(&box, &borders, &broken, &boxes, NULL) !=
                   ROBO_OK);
    }
}

static void test_paint_boxes_translate(void)
{
    robo_paint_boxes_t boxes = paint_boxes_make(10, 20, 100, 50, 2, 5);
    robo_paint_boxes_t moved;

    paint_fixture();
    memset(&moved, 0, sizeof(moved));

    ROBO_CHECK_EQ_INT(robo_paint_boxes_translate(&boxes, 5, 7, &moved, NULL), ROBO_OK);
    paint_rect_is(15, 27, 100, 50, moved.border, "the moved border box");
    paint_rect_is(17, 29, 96, 46, moved.padding, "the moved padding box");
    paint_rect_is(22, 34, 86, 36, moved.content, "the moved content box");
    ROBO_CHECK_EQ_INT(moved.insets_measured, 1);

    /* A displacement that would wrap saturates instead: a coordinate at the edge of the
     * plane is a wrong answer that is still drawable, where a wrapped one is a box at
     * the wrong end of the page. */
    ROBO_CHECK_EQ_INT(robo_paint_boxes_translate(&boxes, INT32_MAX, INT32_MIN, &moved,
                                                 NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(moved.border.width, boxes.border.width);
    ROBO_CHECK_EQ_INT(moved.border.height, boxes.border.height);
    ROBO_CHECK(moved.border.x >= boxes.border.x);

    ROBO_CHECK_EQ_INT(robo_paint_boxes_translate(NULL, 1, 1, &moved, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_translate(&boxes, 1, 1, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* 3. The clip in force                                                */
/* ------------------------------------------------------------------ */

static void test_paint_clip_record(void)
{
    robo_paint_clip_t clip;
    robo_paint_clip_t none = robo_paint_clip_none();

    paint_fixture();

    robo_paint_clip_init(&clip);
    ROBO_CHECK_EQ_INT((int)clip.horizontal.mode, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT((int)clip.vertical.mode, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    /* A cleared clip is CSS's initial value on both axes, so a document that never
     * declared an overflow carries it unchanged from the root down. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_active(&clip.horizontal), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_active(&clip.vertical), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&clip), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_valid(&clip), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_equal(&clip, &none), 1);

    /* "Has a boundary" and "this mode clips" are one question, borrowed from RoboCSS
     * rather than re-decided here. */
    clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_active(&clip.horizontal), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&clip), 1);

    /* An interval that runs backwards on an axis that hides is not a record. */
    {
        robo_paint_clip_t backwards = clip;

        backwards.horizontal.start = 100;
        backwards.horizontal.end = 0;
        ROBO_CHECK_EQ_INT(robo_paint_clip_is_valid(&backwards), 0);
        /* The same numbers on a *visible* axis mean nothing and are therefore fine. */
        backwards.horizontal.mode = ROBO_CSS_OVERFLOW_VISIBLE;
        ROBO_CHECK_EQ_INT(robo_paint_clip_is_valid(&backwards), 1);
    }
    {
        robo_paint_clip_t unknown;

        robo_paint_clip_init(&unknown);
        unknown.vertical.mode = (robo_css_overflow_t)99;
        ROBO_CHECK_EQ_INT(robo_paint_clip_is_valid(&unknown), 0);
    }

    /* An active axis with no extent is empty, and it is *success* rather than a
     * failure: a box a clipping ancestor has already excluded draws nothing. */
    clip.horizontal.start = 10;
    clip.horizontal.end = 10;
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_empty(&clip.horizontal), 1);
    clip.horizontal.end = 20;
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_empty(&clip.horizontal), 0);

    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_active(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_equal(&clip, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_empty(NULL), 0);
    robo_paint_clip_init(NULL); /* safe */
}

static void test_paint_clip_rect_door(void)
{
    robo_paint_clip_t clip = robo_paint_clip_none();
    robo_layout_rect_t rect;

    paint_fixture();
    memset(&rect, 0, sizeof(rect));

    /* One rectangle says everything only when *both* axes are bounded, and the door
     * refuses the rest rather than answering a `visible` axis with a number that means
     * "free" as if it meant "bounded". */
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(&clip, &rect), ROBO_NOT_FOUND);
    clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    clip.horizontal.start = 1;
    clip.horizontal.end = 11;
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(&clip, &rect), ROBO_NOT_FOUND);
    clip.vertical.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    clip.vertical.start = 2;
    clip.vertical.end = 12;
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(&clip, &rect), ROBO_OK);
    paint_rect_is(1, 2, 10, 10, rect, "the clip rectangle");
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(NULL, &rect), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(&clip, NULL), ROBO_INVALID_ARGUMENT);
}

static void test_paint_clip_intersect(void)
{
    robo_paint_clip_t clip = robo_paint_clip_none();
    robo_paint_clip_t out;
    robo_layout_rect_t boundary;

    paint_fixture();
    boundary.x = 10;
    boundary.y = 20;
    boundary.width = 100;
    boundary.height = 50;

    /* A boundary that hides neither axis leaves the clip in force untouched -- the case
     * every box in an ordinary document produces on every descent, and the reason the
     * traversal can call this unconditionally. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&clip, &boundary, ROBO_CSS_OVERFLOW_VISIBLE,
                                                ROBO_CSS_OVERFLOW_VISIBLE, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_clip_equal(&out, &clip), 1);

    /* Hidden on both axes: the interval is the boundary's, as a half-open range. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&clip, &boundary, ROBO_CSS_OVERFLOW_HIDDEN,
                                                ROBO_CSS_OVERFLOW_HIDDEN, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&out), 1);
    ROBO_CHECK_EQ_INT(out.horizontal.start, 10);
    ROBO_CHECK_EQ_INT(out.horizontal.end, 110);
    ROBO_CHECK_EQ_INT(out.vertical.start, 20);
    ROBO_CHECK_EQ_INT(out.vertical.end, 70);

    /* The axis split, which is the whole reason the record is two intervals: a boundary
     * hidden horizontally and visible vertically bounds *only* x, and nothing about
     * `y` moves. */
    {
        robo_paint_clip_t split = robo_paint_clip_none();

        ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&split, &boundary,
                                                    ROBO_CSS_OVERFLOW_HIDDEN,
                                                    ROBO_CSS_OVERFLOW_VISIBLE, &out, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_active(&out.horizontal), 1);
        ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_active(&out.vertical), 0);
        ROBO_CHECK_EQ_INT(out.horizontal.end, 110);
    }

    /* A nested boundary is an intersection on the axes that hide: the larger start and
     * the smaller end, per axis. */
    {
        robo_paint_clip_t outer;
        robo_layout_rect_t inner;

        robo_paint_clip_init(&outer);
        outer.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
        outer.horizontal.start = 0;
        outer.horizontal.end = 60;
        inner.x = 30;
        inner.y = 0;
        inner.width = 100;
        inner.height = 10;
        ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&outer, &inner, ROBO_CSS_OVERFLOW_HIDDEN,
                                                    ROBO_CSS_OVERFLOW_VISIBLE, &out, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(out.horizontal.start, 30);
        ROBO_CHECK_EQ_INT(out.horizontal.end, 60);
        /* An interval that comes out empty is success with an empty interval. */
        {
            robo_layout_rect_t far;

            far.x = 200;
            far.y = 0;
            far.width = 10;
            far.height = 10;
            ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&outer, &far,
                                                        ROBO_CSS_OVERFLOW_HIDDEN,
                                                        ROBO_CSS_OVERFLOW_VISIBLE, &out, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_empty(&out.horizontal), 1);
        }
    }

    /* The refusals. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(NULL, &boundary, ROBO_CSS_OVERFLOW_VISIBLE,
                                                ROBO_CSS_OVERFLOW_VISIBLE, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&clip, NULL, ROBO_CSS_OVERFLOW_VISIBLE,
                                                ROBO_CSS_OVERFLOW_VISIBLE, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&clip, &boundary, ROBO_CSS_OVERFLOW_VISIBLE,
                                                ROBO_CSS_OVERFLOW_VISIBLE, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&clip, &boundary,
                                                (robo_css_overflow_t)99,
                                                ROBO_CSS_OVERFLOW_VISIBLE, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        robo_layout_rect_t negative = boundary;

        negative.height = -1;
        ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&clip, &negative,
                                                    ROBO_CSS_OVERFLOW_HIDDEN,
                                                    ROBO_CSS_OVERFLOW_HIDDEN, &out, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
    {
        robo_paint_clip_t broken = robo_paint_clip_none();

        broken.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
        broken.horizontal.start = 10;
        broken.horizontal.end = 5;
        ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&broken, &boundary,
                                                    ROBO_CSS_OVERFLOW_HIDDEN,
                                                    ROBO_CSS_OVERFLOW_HIDDEN, &out, NULL),
                          ROBO_ERROR);
    }
}

/* ------------------------------------------------------------------ */
/* 4. The operation                                                    */
/* ------------------------------------------------------------------ */

static void test_paint_operation_record(void)
{
    robo_paint_boxes_t boxes = paint_boxes_make(0, 0, 100, 40, 2, 5);
    robo_paint_operation_t operation;
    robo_css_color_t red = robo_css_color_make(255, 0, 0, 255);

    paint_fixture();

    robo_paint_operation_init(&operation);
    ROBO_CHECK_EQ_INT((int)operation.kind, (int)ROBO_PAINT_OP_NONE);
    ROBO_CHECK_EQ_INT((int)operation.node, (int)ROBO_LAYOUT_NODE_ID_NONE);
    /* A colour a rasteriser *cannot* use, which is what makes a cleared record a stated
     * absence rather than a black that might be drawn. */
    ROBO_CHECK_EQ_INT((int)operation.color.kind, (int)ROBO_CSS_COLOR_NONE);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&operation.clip), 0);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 0);

    /* A marker: no colour, a live node, valid boxes. */
    operation = paint_marker_make(3, 1u, boxes);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 1);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_marker(&operation), 1);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_drawing(&operation), 0);

    /* Both halves of the colour rule, in both directions. */
    {
        robo_paint_operation_t wrong = operation;

        wrong.color = red;
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&wrong), 0);
    }
    operation = paint_drawing_make(ROBO_PAINT_OP_BACKGROUND, 3, boxes, red);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 1);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_marker(&operation), 0);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_drawing(&operation), 1);
    {
        robo_paint_operation_t wrong = operation;

        wrong.color = robo_css_color_none();
        /* A fill with no colour draws nothing, and an operation that draws nothing is
         * not a statement about the document. */
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&wrong), 0);
    }
    {
        robo_paint_operation_t contextual = operation;

        contextual.color = robo_css_color_current();
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&contextual), 1);
    }
    {
        robo_paint_operation_t invalid = operation;

        invalid.color = robo_css_color_invalid();
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&invalid), 0);
    }

    /*
     * And the style rule, one field along from the colour's (11.5). It is asserted from both
     * sides because it has two halves that fail differently: a border record must say **which**
     * of the ten styles shapes its band, since that fact is not derivable from the band's
     * rectangle, and a background record must not carry a style, because it has no side to style.
     */
    {
        robo_paint_operation_t border = paint_drawing_make(ROBO_PAINT_OP_BORDER_TOP, 3, boxes,
                                                          red);

        ROBO_CHECK_EQ_INT((int)robo_paint_operation_style(&border),
                          (int)ROBO_CSS_BORDER_STYLE_SOLID);
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&border), 1);
        border.style = ROBO_CSS_BORDER_STYLE_NONE;
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&border), 0);
        border.style = ROBO_CSS_BORDER_STYLE_HIDDEN;
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&border), 0);
        border.style = ROBO_CSS_BORDER_STYLE_DASHED;
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&border), 1);
    }
    {
        robo_paint_operation_t wrong = operation;

        wrong.style = ROBO_CSS_BORDER_STYLE_SOLID;
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&wrong), 0);
    }

    /* Every other refusal: a cleared kind, an unknown kind, no node, boxes that are not
     * a record, a clip that is not one. */
    {
        robo_paint_operation_t wrong = operation;

        wrong.kind = ROBO_PAINT_OP_NONE;
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&wrong), 0);
        wrong.kind = (robo_paint_op_kind_t)99;
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&wrong), 0);
        wrong = operation;
        wrong.node = ROBO_LAYOUT_NODE_ID_NONE;
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&wrong), 0);
        wrong = operation;
        wrong.boxes.padding.width = 0;
        wrong.boxes.content.x = 500;
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&wrong), 0);
        wrong = operation;
        wrong.clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
        wrong.clip.horizontal.start = 10;
        wrong.clip.horizontal.end = 1;
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&wrong), 0);
    }
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(NULL), 0);
    robo_paint_operation_init(NULL); /* safe */
}

static void test_paint_operation_accessors(void)
{
    robo_paint_boxes_t boxes = paint_boxes_make(10, 20, 100, 40, 2, 5);
    robo_paint_operation_t a = paint_drawing_make(ROBO_PAINT_OP_BORDER_TOP, 7, boxes,
                                                 robo_css_color_make(0, 0, 255, 255));
    robo_paint_operation_t b;
    robo_paint_boxes_t read;
    robo_layout_point_t origin;
    robo_paint_clip_t clip;
    robo_css_box_side_t side;

    paint_fixture();
    b = a;
    a.origin.x = 10;
    a.origin.y = 20;
    b.origin.x = 10;
    b.origin.y = 20;

    ROBO_CHECK_EQ_INT(robo_paint_operation_equal(&a, &b), 1);
    ROBO_CHECK_EQ_INT(robo_paint_operation_equal(&a, &a), 1);
    ROBO_CHECK_EQ_INT(robo_paint_operation_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_paint_operation_equal(&a, NULL), 0);

    /* Field by field: each one on its own, because a comparison that ignored a field
     * would make two different conclusions look equal. */
    {
        robo_paint_operation_t c = a;

        c.kind = ROBO_PAINT_OP_BORDER_RIGHT;
        ROBO_CHECK_EQ_INT(robo_paint_operation_equal(&a, &c), 0);
        c = a;
        c.node = 8;
        ROBO_CHECK_EQ_INT(robo_paint_operation_equal(&a, &c), 0);
        c = a;
        c.depth = 2u;
        ROBO_CHECK_EQ_INT(robo_paint_operation_equal(&a, &c), 0);
        c = a;
        c.origin.x = 11;
        ROBO_CHECK_EQ_INT(robo_paint_operation_equal(&a, &c), 0);
        c = a;
        c.color = robo_css_color_make(0, 0, 255, 254);
        ROBO_CHECK_EQ_INT(robo_paint_operation_equal(&a, &c), 0);
        c = a;
        c.clip.vertical.mode = ROBO_CSS_OVERFLOW_HIDDEN;
        c.clip.vertical.start = 0;
        c.clip.vertical.end = 10;
        ROBO_CHECK_EQ_INT(robo_paint_operation_equal(&a, &c), 0);
    }

    ROBO_CHECK_EQ_INT((int)robo_paint_operation_kind(&a), (int)ROBO_PAINT_OP_BORDER_TOP);
    ROBO_CHECK_EQ_INT((int)robo_paint_operation_node(&a), 7);
    ROBO_CHECK_EQ_INT((int)robo_paint_operation_depth(&a), 0);
    ROBO_CHECK_EQ_INT(robo_paint_operation_side(&a, &side), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)side, (int)ROBO_CSS_BOX_SIDE_TOP);
    ROBO_CHECK_EQ_INT(robo_paint_operation_side(&a, NULL), ROBO_INVALID_ARGUMENT);
    {
        robo_paint_operation_t background = paint_drawing_make(
            ROBO_PAINT_OP_BACKGROUND, 7, boxes, robo_css_color_make(0, 0, 0, 255));

        ROBO_CHECK_EQ_INT(robo_paint_operation_side(&background, &side),
                          ROBO_INVALID_ARGUMENT);
    }

    robo_paint_operation_boxes(&a, &read);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_equal(&read, &boxes), 1);
    robo_paint_operation_origin(&a, &origin);
    ROBO_CHECK_EQ_INT(origin.x, 10);
    ROBO_CHECK_EQ_INT(origin.y, 20);
    robo_paint_operation_clip(&a, &clip);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&clip), 0);
    paint_color_eq(robo_paint_operation_color(&a), robo_css_color_make(0, 0, 255, 255),
                   "the operation's colour");

    /* The two coordinate spaces: `local = boxes - origin`, and it is exactly what
     * layout wrote -- which is the assertion that makes carrying both halves safe rather
     * than a second opinion. */
    {
        robo_paint_boxes_t local;

        memset(&local, 0, sizeof(local));
        ROBO_CHECK_EQ_INT(robo_paint_operation_local_boxes(&a, &local, NULL), ROBO_OK);
        paint_rect_is(0, 0, 100, 40, local.border, "the local border box");
        paint_rect_is(2, 2, 96, 36, local.padding, "the local padding box");
        paint_rect_is(7, 7, 86, 26, local.content, "the local content box");
        ROBO_CHECK_EQ_INT(local.insets_measured, 1);
    }
    ROBO_CHECK_EQ_INT(robo_paint_operation_local_boxes(NULL, &read, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_operation_local_boxes(&a, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        robo_paint_operation_t broken = a;

        broken.boxes.padding.x = 900; /* the layers now cross */
        ROBO_CHECK_EQ_INT(robo_paint_operation_local_boxes(&broken, &read, NULL), ROBO_ERROR);
    }

    /* Every accessor on NULL answers the documented value rather than crashing. */
    ROBO_CHECK_EQ_INT((int)robo_paint_operation_kind(NULL), (int)ROBO_PAINT_OP_NONE);
    ROBO_CHECK_EQ_INT((int)robo_paint_operation_node(NULL), (int)ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT((int)robo_paint_operation_depth(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_marker(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_drawing(NULL), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_operation_color(NULL).kind, (int)ROBO_CSS_COLOR_NONE);
    robo_paint_operation_boxes(NULL, &read);
    ROBO_CHECK_EQ_INT(read.insets_measured, 0);
    robo_paint_operation_origin(NULL, &origin);
    ROBO_CHECK_EQ_INT(origin.x, 0);
    robo_paint_operation_clip(NULL, &clip);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&clip), 0);
    robo_paint_operation_boxes(&a, NULL); /* safe */
    robo_paint_operation_origin(&a, NULL);
    robo_paint_operation_clip(&a, NULL);
}

static void test_paint_operation_side_band(void)
{
    /* A box 100 x 40 with four different borders and 5px of padding all round: the four
     * bands are what a rasteriser will fill, and each one is derived from the two
     * rectangles the record already carries rather than stored a third time. */
    robo_layout_border_edges_t borders = paint_border_edges(5, 3, 7, 2);
    robo_layout_padding_edges_t paddings = paint_padding_edges(5, 5, 5, 5);
    robo_layout_rect_t box;
    robo_paint_boxes_t boxes;
    robo_layout_rect_t band;

    paint_fixture();
    box.x = 0;
    box.y = 0;
    box.width = 100;
    box.height = 40;
    memset(&boxes, 0, sizeof(boxes));
    ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(&box, &borders, &paddings, &boxes, NULL),
                      ROBO_OK);
    /* padding box: x 2, y 5, w 95, h 28. */
    paint_rect_is(2, 5, 95, 28, boxes.padding, "the padded box of the band fixture");

    {
        robo_paint_operation_t top = paint_drawing_make(
            ROBO_PAINT_OP_BORDER_TOP, 1, boxes, robo_css_color_make(0, 0, 0, 255));

        memset(&band, 0, sizeof(band));
        ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(&top, &band, NULL), ROBO_OK);
        /* The top band spans the box's full width and stops where the padding begins. */
        paint_rect_is(0, 0, 100, 5, band, "the top band");
    }
    {
        robo_paint_operation_t right = paint_drawing_make(
            ROBO_PAINT_OP_BORDER_RIGHT, 1, boxes, robo_css_color_make(0, 0, 0, 255));

        ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(&right, &band, NULL), ROBO_OK);
        /* 2 + 95 = 97, and the band is 3 wide (100 - 97). */
        paint_rect_is(97, 0, 3, 40, band, "the right band");
    }
    {
        robo_paint_operation_t bottom = paint_drawing_make(
            ROBO_PAINT_OP_BORDER_BOTTOM, 1, boxes, robo_css_color_make(0, 0, 0, 255));

        ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(&bottom, &band, NULL), ROBO_OK);
        /* 5 + 28 = 33, and the band is 7 tall (40 - 33). */
        paint_rect_is(0, 33, 100, 7, band, "the bottom band");
    }
    {
        robo_paint_operation_t left = paint_drawing_make(
            ROBO_PAINT_OP_BORDER_LEFT, 1, boxes, robo_css_color_make(0, 0, 0, 255));

        ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(&left, &band, NULL), ROBO_OK);
        paint_rect_is(0, 0, 2, 40, band, "the left band");
    }

    /* A non-border operation has no band, and the refusal is the answer rather than a
     * rectangle that happens to be zero. */
    {
        robo_paint_operation_t background = paint_drawing_make(
            ROBO_PAINT_OP_BACKGROUND, 1, boxes, robo_css_color_make(0, 0, 0, 255));

        ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(&background, &band, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(NULL, &band, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(&background, NULL, NULL),
                          ROBO_INVALID_ARGUMENT);
    }

    /*
     * A box whose border already exceeds it: the padding box is squeezed to nothing **at
     * the box's own edge** rather than pushed outside it, so the three layers still nest
     * -- which is what keeps every operation derived from such a box a record instead of
     * failing a whole pass on a document that is merely extreme.
     */
    {
        robo_layout_border_edges_t thick = paint_border_edges(60, 60, 60, 60);
        robo_layout_padding_edges_t none = paint_padding_edges(0, 0, 0, 0);
        robo_paint_boxes_t squeezed;
        robo_paint_operation_t top;

        memset(&squeezed, 0, sizeof(squeezed));
        ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(&box, &thick, &none, &squeezed, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(squeezed.padding.width, 0);
        ROBO_CHECK_EQ_INT(squeezed.padding.height, 0);
        ROBO_CHECK_EQ_INT(squeezed.content.width, 0);
        ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&squeezed), 1);

        top = paint_drawing_make(ROBO_PAINT_OP_BORDER_TOP, 1, squeezed,
                                 robo_css_color_make(0, 0, 0, 255));
        ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(&top, &band, NULL), ROBO_OK);
        /* The band spans the box's whole width, and its height is however much of the box
         * the padding box's origin leaves -- which for an emptied layer is all of it. */
        ROBO_CHECK_EQ_INT(band.width, squeezed.border.width);
        ROBO_CHECK_EQ_INT(band.y, squeezed.border.y);
        ROBO_CHECK(band.height <= squeezed.border.height);
        ROBO_CHECK_EQ_INT(band.height, squeezed.padding.y - squeezed.border.y);
    }
}

static void test_paint_operation_write(void)
{
    robo_paint_boxes_t boxes = paint_boxes_make(0, 0, 100, 40, 2, 5);
    robo_paint_operation_t background = paint_drawing_make(
        ROBO_PAINT_OP_BACKGROUND, 3, boxes, robo_css_color_make(255, 0, 0, 255));
    robo_paint_operation_t marker = paint_marker_make(3, 1u, boxes);
    char buffer[256];
    size_t length = 0u;

    paint_fixture();
    memset(buffer, 0, sizeof(buffer));

    ROBO_CHECK_EQ_INT(robo_paint_operation_write(&background, buffer, sizeof(buffer),
                                                 &length, NULL),
                      ROBO_OK);
    ROBO_CHECK(length > 0u);
    ROBO_CHECK(strstr(buffer, "background") != NULL);
    ROBO_CHECK(strstr(buffer, "node=3") != NULL);
    ROBO_CHECK(strstr(buffer, "depth=0") != NULL);
    ROBO_CHECK(strstr(buffer, "clip=visible/visible") != NULL);
    /* A background acts on the border box, and the line says which rectangle it is
     * rather than leaving a reader to remember. */
    ROBO_CHECK(strstr(buffer, "rect=0,0,100,40") != NULL);
    ROBO_CHECK(strstr(buffer, "color=") != NULL);
    ROBO_CHECK_EQ_INT((int)length, (int)strlen(buffer));

    ROBO_CHECK_EQ_INT(robo_paint_operation_write(&marker, buffer, sizeof(buffer), &length,
                                                 NULL),
                      ROBO_OK);
    ROBO_CHECK(strstr(buffer, "begin-box") != NULL);
    ROBO_CHECK(strstr(buffer, "border=0,0,100,40") != NULL);
    /* A marker has no colour to print, because it has none to draw with. */
    ROBO_CHECK(strstr(buffer, "color=") == NULL);

    {
        robo_paint_operation_t border = paint_drawing_make(
            ROBO_PAINT_OP_BORDER_TOP, 3, boxes, robo_css_color_make(0, 0, 0, 255));

        ROBO_CHECK_EQ_INT(robo_paint_operation_write(&border, buffer, sizeof(buffer),
                                                     &length, NULL),
                          ROBO_OK);
        ROBO_CHECK(strstr(buffer, "border-top") != NULL);
        /* A border prints its band, which is the rectangle it fills. */
        ROBO_CHECK(strstr(buffer, "band=0,0,100,2") != NULL);
    }

    /* A buffer too small is refused **with the length it would have needed**, so a
     * caller can size one from the refusal rather than from an estimate. */
    {
        char tiny[8];
        size_t needed = 0u;

        ROBO_CHECK_EQ_INT(robo_paint_operation_write(&background, tiny, sizeof(tiny),
                                                     &needed, NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK(needed > sizeof(tiny));
        {
            char exact[512];

            ROBO_CHECK(needed < sizeof(exact));
            ROBO_CHECK_EQ_INT(robo_paint_operation_write(&background, exact, needed + 1u,
                                                         &length, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT((int)length, (int)needed);
        }
    }

    ROBO_CHECK_EQ_INT(robo_paint_operation_write(NULL, buffer, sizeof(buffer), &length,
                                                 NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_operation_write(&background, NULL, sizeof(buffer), &length,
                                                 NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_operation_write(&background, buffer, sizeof(buffer), NULL,
                                                 NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_operation_write(&background, buffer, 0u, &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        robo_paint_operation_t cleared;

        robo_paint_operation_init(&cleared);
        ROBO_CHECK_EQ_INT(robo_paint_operation_write(&cleared, buffer, sizeof(buffer),
                                                     &length, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
}

/* ------------------------------------------------------------------ */
/* 5. The list                                                         */
/* ------------------------------------------------------------------ */

static void test_paint_list_lifecycle(void)
{
    robo_paint_list_t *list = NULL;
    robo_paint_boxes_t boxes = paint_boxes_make(0, 0, 100, 40, 2, 5);
    robo_paint_operation_t marker = paint_marker_make(1, 0u, boxes);
    robo_paint_operation_t background = paint_drawing_make(
        ROBO_PAINT_OP_BACKGROUND, 1, boxes, robo_css_color_make(1, 2, 3, 255));
    robo_paint_operation_t read;

    paint_fixture();

    ROBO_CHECK_EQ_INT(robo_paint_list_create(&list, NULL), ROBO_OK);
    /* The first block is taken at creation, so a caller that sizes a list before a pass
     * does not have to branch. */
    ROBO_CHECK_EQ_INT((int)robo_paint_list_capacity(list), (int)ROBO_PAINT_INITIAL_CAPACITY);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(list), 0);
    ROBO_CHECK(robo_paint_list_operations(list) == NULL);
    ROBO_CHECK_EQ_INT(robo_paint_list_create(NULL, NULL), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &marker, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &background, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(list), 2);
    ROBO_CHECK(robo_paint_list_operations(list) != NULL);

    ROBO_CHECK_EQ_INT(robo_paint_list_at(list, 0u, &read), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_operation_equal(&read, &marker), 1);
    ROBO_CHECK_EQ_INT(robo_paint_list_at(list, 1u, &read), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_operation_equal(&read, &background), 1);
    ROBO_CHECK_EQ_INT(robo_paint_list_at(list, 2u, &read), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_paint_list_at(list, 0u, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_list_at(NULL, 0u, &read), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT((int)robo_paint_list_count_of_kind(list, ROBO_PAINT_OP_BACKGROUND), 1);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count_of_kind(list, ROBO_PAINT_OP_BEGIN_BOX), 1);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count_of_kind(list, ROBO_PAINT_OP_END_BOX), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count_of_kind(NULL, ROBO_PAINT_OP_END_BOX), 0);

    /* The append's validation is the boundary that keeps an invalid operation out of a
     * list, so a refused append leaves the list untouched. */
    {
        robo_paint_operation_t cleared;
        size_t before = robo_paint_list_count(list);

        robo_paint_operation_init(&cleared);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &cleared, NULL), ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT((int)robo_paint_list_count(list), (int)before);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(list, NULL, NULL), ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(NULL, &marker, NULL), ROBO_INVALID_ARGUMENT);
    }

    /* `clear` drops the operations and keeps the storage -- the concession this phase
     * makes to a per-frame renderer, and not an incremental mechanism. */
    {
        size_t capacity_before = robo_paint_list_capacity(list);

        robo_paint_list_clear(list);
        ROBO_CHECK_EQ_INT((int)robo_paint_list_count(list), 0);
        ROBO_CHECK_EQ_INT((int)robo_paint_list_capacity(list), (int)capacity_before);
        ROBO_CHECK_EQ_INT(robo_paint_list_at(list, 0u, &read), ROBO_NOT_FOUND);
    }

    /* `reserve` below the capacity is a no-op rather than a shrink, so a caller may size
     * a list before every pass without asking what it already holds. */
    ROBO_CHECK_EQ_INT(robo_paint_list_reserve(list, 4u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_capacity(list), (int)ROBO_PAINT_INITIAL_CAPACITY);
    ROBO_CHECK_EQ_INT(robo_paint_list_reserve(list, 100u, NULL), ROBO_OK);
    ROBO_CHECK(robo_paint_list_capacity(list) >= 100u);
    ROBO_CHECK_EQ_INT(robo_paint_list_reserve(NULL, 4u, NULL), ROBO_INVALID_ARGUMENT);

    /* Growth: a hundred operations appended one at a time, every one readable and a
     * record, which is what makes the doubling path worth testing at all. */
    {
        size_t index;

        for (index = 0u; index < 100u; index++) {
            robo_paint_operation_t operation = marker;

            operation.depth = index;
            ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &operation, NULL), ROBO_OK);
        }
        ROBO_CHECK_EQ_INT((int)robo_paint_list_count(list), 100);
        for (index = 0u; index < 100u; index++) {
            ROBO_CHECK_EQ_INT(robo_paint_list_at(list, index, &read), ROBO_OK);
            ROBO_CHECK_EQ_INT((int)read.depth, (int)index);
            ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&read), 1);
        }
    }

    robo_paint_list_destroy(list);
    robo_paint_list_destroy(NULL); /* safe */
    robo_paint_list_clear(NULL);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(NULL), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_capacity(NULL), 0);
    ROBO_CHECK(robo_paint_list_operations(NULL) == NULL);
}

static void test_paint_list_equality(void)
{
    robo_paint_boxes_t boxes = paint_boxes_make(0, 0, 100, 40, 2, 5);
    robo_paint_operation_t a_op = paint_marker_make(1, 0u, boxes);
    robo_paint_operation_t b_op = paint_drawing_make(
        ROBO_PAINT_OP_BACKGROUND, 1, boxes, robo_css_color_make(9, 9, 9, 255));
    robo_paint_list_t *a = NULL;
    robo_paint_list_t *b = NULL;
    robo_paint_list_t *c = NULL;

    paint_fixture();
    ROBO_CHECK_EQ_INT(robo_paint_list_create(&a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_create(&b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_create(&c, NULL), ROBO_OK);

    /* Two empty lists are equal, and so are two NULLs -- which is the same statement as
     * "a list holds its own operations". */
    ROBO_CHECK_EQ_INT(robo_paint_list_equal(a, b), 1);
    ROBO_CHECK_EQ_INT(robo_paint_list_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_paint_list_equal(a, NULL), 0);

    ROBO_CHECK_EQ_INT(robo_paint_list_append(a, &a_op, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_append(a, &b_op, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_equal(a, b), 0);
    ROBO_CHECK_EQ_INT(robo_paint_list_append(b, &a_op, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_append(b, &b_op, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_equal(a, b), 1);

    /* A difference in the *middle*: a comparison that stopped at the counts would say
     * these were equal, and this is the cheap check that catches it. */
    ROBO_CHECK_EQ_INT(robo_paint_list_append(c, &a_op, NULL), ROBO_OK);
    {
        robo_paint_operation_t different = b_op;

        different.depth = 3u;
        ROBO_CHECK_EQ_INT(robo_paint_list_append(c, &different, NULL), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_paint_list_append(c, &b_op, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_equal(a, c), 0);
    /* And swapping the two operations of one list makes it unequal to itself's order. */
    {
        robo_paint_list_t *d = NULL;

        ROBO_CHECK_EQ_INT(robo_paint_list_create(&d, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(d, &b_op, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(d, &a_op, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_equal(a, d), 0);
        robo_paint_list_destroy(d);
    }

    robo_paint_list_destroy(a);
    robo_paint_list_destroy(b);
    robo_paint_list_destroy(c);
}

/* ------------------------------------------------------------------ */
/* 6. The dump                                                         */
/* ------------------------------------------------------------------ */

/** Count the newlines in @p text, which is one per line. */
static size_t paint_count_lines(const char *text)
{
    size_t count = 0u;

    while (*text != '\0') {
        if (*text == '\n') {
            count++;
        }
        text++;
    }
    return count;
}

static void test_paint_dump_format(void)
{
    robo_paint_boxes_t boxes = paint_boxes_make(0, 0, 100, 40, 2, 5);
    robo_paint_operation_t begin = paint_marker_make(1, 0u, boxes);
    robo_paint_operation_t background = paint_drawing_make(
        ROBO_PAINT_OP_BACKGROUND, 1, boxes, robo_css_color_make(255, 0, 0, 255));
    robo_paint_operation_t border = paint_drawing_make(
        ROBO_PAINT_OP_BORDER_TOP, 1, boxes, robo_css_color_make(0, 0, 0, 255));
    robo_paint_operation_t end = paint_marker_make(1, 0u, boxes);
    robo_paint_list_t *list = NULL;
    char buffer[2048];
    size_t length = 0u;

    paint_fixture();
    memset(buffer, 0, sizeof(buffer));
    begin.kind = ROBO_PAINT_OP_BEGIN_BOX;
    end.kind = ROBO_PAINT_OP_END_BOX;

    ROBO_CHECK_EQ_INT(robo_paint_list_create(&list, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &begin, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &background, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &border, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &end, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_paint_list_write(list, buffer, sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)length, (int)strlen(buffer));
    ROBO_CHECK_EQ_INT((int)paint_count_lines(buffer), 4);
    /* The ordinal is zero-padded so that the lines sort the way the list does, and every
     * line is newline-terminated including the last. */
    ROBO_CHECK(strncmp(buffer, "#0000 begin-box", 8) == 0);
    ROBO_CHECK(strstr(buffer, "#0001 ") != NULL);
    ROBO_CHECK(strstr(buffer, "#0002 border-top") != NULL);
    ROBO_CHECK(strstr(buffer, "#0003 end-box") != NULL);
    ROBO_CHECK(buffer[length - 1u] == '\n');

    /* Indentation is the operation's own depth, not a nested print of the tree: the
     * flat sequence is what a rasteriser would see, and a dump that re-derived the
     * structure from the tree would hide exactly the misplacement worth looking for. */
    robo_paint_list_destroy(list);
    list = NULL;

    {
        robo_paint_operation_t deep = paint_marker_make(1, 2u, boxes);

        ROBO_CHECK_EQ_INT(robo_paint_list_create(&list, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &deep, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_write(list, buffer, sizeof(buffer), &length, NULL),
                          ROBO_OK);
        /* One space after the ordinal, then two per level of depth: `#0000` + 5 spaces. */
        ROBO_CHECK(strncmp(buffer, "#0000     begin-box", 19) == 0);
        robo_paint_list_destroy(list);
        list = NULL;
    }

    /* An empty list writes an empty dump rather than a placeholder line: a document that
     * draws nothing has no operations to describe. */
    ROBO_CHECK_EQ_INT(robo_paint_list_create(&list, NULL), ROBO_OK);
    buffer[0] = 'x';
    ROBO_CHECK_EQ_INT(robo_paint_list_write(list, buffer, sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)length, 0);
    ROBO_CHECK(buffer[0] == '\0');
    ROBO_CHECK_EQ_INT(robo_paint_list_write(NULL, buffer, sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)length, 0);
    robo_paint_list_destroy(list);
    list = NULL;

    /* The refusal carries the length the dump would have needed, which is what lets a
     * caller size a buffer from the refusal instead of from an estimate. */
    {
        robo_paint_list_t *big = NULL;
        char tiny[16];
        size_t needed = 0u;
        size_t index;

        ROBO_CHECK_EQ_INT(robo_paint_list_create(&big, NULL), ROBO_OK);
        for (index = 0u; index < 12u; index++) {
            ROBO_CHECK_EQ_INT(robo_paint_list_append(big, &background, NULL), ROBO_OK);
        }
        ROBO_CHECK_EQ_INT(robo_paint_list_write(big, tiny, sizeof(tiny), &needed, NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK(needed > sizeof(tiny));
        {
            char exact[4096];

            ROBO_CHECK(needed < sizeof(exact));
            ROBO_CHECK_EQ_INT(robo_paint_list_write(big, exact, needed + 1u, &length, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT((int)length, (int)needed);
            ROBO_CHECK_EQ_INT((int)paint_count_lines(exact), 12);
        }
        robo_paint_list_destroy(big);
    }

    ROBO_CHECK_EQ_INT(robo_paint_list_write(NULL, NULL, 1u, &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_list_write(NULL, buffer, sizeof(buffer), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_list_write(NULL, buffer, 0u, &length, NULL),
                      ROBO_INVALID_ARGUMENT);
}

static void test_paint_dump_line_bound(void)
{
    /*
     * `ROBO_PAINT_DUMP_LINE_MAX` is a *bound*, not a guess: the widest line a dumped
     * operation can need is a border side with every field at its widest printed form,
     * and a suite that asserted the bound would be asserting nothing useful. So this
     * measures the widest one this build can produce and states the bound is wide
     * enough -- which is the check that catches a future field being added to the line
     * without the bound growing with it.
     */
    robo_layout_border_edges_t borders = paint_border_edges(5, 5, 5, 5);
    robo_layout_padding_edges_t paddings = paint_padding_edges(5, 5, 5, 5);
    robo_layout_rect_t box;
    robo_paint_boxes_t boxes;
    robo_paint_operation_t border;
    char line[ROBO_PAINT_DUMP_LINE_MAX + 1u];
    size_t length = 0u;

    paint_fixture();
    box.x = INT32_MIN;
    box.y = INT32_MIN;
    box.width = INT32_MAX;
    box.height = INT32_MAX;
    memset(&boxes, 0, sizeof(boxes));
    ROBO_CHECK_EQ_INT(robo_paint_boxes_from_layers(&box, &borders, &paddings, &boxes, NULL),
                      ROBO_OK);
    border = paint_drawing_make(ROBO_PAINT_OP_BORDER_BOTTOM, (robo_layout_node_id_t)999999u,
                                boxes, robo_css_color_make(255, 255, 255, 255));
    border.depth = 99u;
    border.clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    border.clip.horizontal.start = INT32_MIN;
    border.clip.horizontal.end = INT32_MAX;
    border.clip.vertical.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    border.clip.vertical.start = INT32_MIN;
    border.clip.vertical.end = INT32_MAX;

    memset(line, 0, sizeof(line));
    ROBO_CHECK_EQ_INT(robo_paint_operation_write(&border, line, sizeof(line), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK(length < (size_t)ROBO_PAINT_DUMP_LINE_MAX);
    ROBO_CHECK(length > 0u);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_paint(void)
{
    robo_test_register("paint.module.lifecycle", test_paint_module_lifecycle);
    robo_test_register("paint.types.kind_vocabulary", test_paint_kind_vocabulary);
    robo_test_register("paint.types.domain_vocabulary", test_paint_domain_vocabulary);
    robo_test_register("paint.diagnostics.record", test_paint_diagnostics_record);
    robo_test_register("paint.geometry.boxes_record", test_paint_boxes_record);
    robo_test_register("paint.geometry.boxes_from_layers", test_paint_boxes_from_layers);
    robo_test_register("paint.geometry.boxes_translate", test_paint_boxes_translate);
    robo_test_register("paint.geometry.clip_record", test_paint_clip_record);
    robo_test_register("paint.geometry.clip_rect", test_paint_clip_rect_door);
    robo_test_register("paint.geometry.clip_intersect", test_paint_clip_intersect);
    robo_test_register("paint.operation.record", test_paint_operation_record);
    robo_test_register("paint.operation.accessors", test_paint_operation_accessors);
    robo_test_register("paint.operation.side_band", test_paint_operation_side_band);
    robo_test_register("paint.operation.write", test_paint_operation_write);
    robo_test_register("paint.list.lifecycle", test_paint_list_lifecycle);
    robo_test_register("paint.list.equality", test_paint_list_equality);
    robo_test_register("paint.dump.format", test_paint_dump_format);
    robo_test_register("paint.dump.line_bound", test_paint_dump_line_bound);
}
