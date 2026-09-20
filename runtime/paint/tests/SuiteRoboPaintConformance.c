/*
 * RoboPaint conformance suite (Phase 11.1) — paint.conformance.*
 *
 * The cases that are about the *module* rather than about any one piece of it, and they
 * fall into four kinds:
 *
 *   capabilities   every field of `robo_paint_capabilities_t`, read **positionally**
 *                  against a list of names. That is the device RoboLayout's conformance
 *                  suite uses and it is kept for the same reason: `capabilities` is a
 *                  struct, a struct read in declaration order is the one way to assert
 *                  eleven `int` fields without writing eleven accessors, and a field added
 *                  without an assertion in that list is a visible omission rather than a
 *                  silently untested flag. Four of the fields are deliberately 0, and the
 *                  list says which four and why;
 *
 *   the absence    the three-place statement 11.1 makes about drawing -- the constant,
 *                  the door and the capability field -- asserted together, because a
 *                  caller that checked only a version number would conclude this build
 *                  paints;
 *
 *   a document     a document that uses every layer this build paints from, with the
 *                  invariants asked of the whole list: every operation is a record, every
 *                  kind is in the vocabulary, every box's three rectangles nest, and every
 *                  operation's two coordinate spaces agree with the tree. That last one is
 *                  the phase's central claim at the widest scope a suite can ask it;
 *
 *   hostile input  every public door called with NULLs, zero capacities, out-of-range
 *                  indices and undefined enumerators. The contract is uniform -- a
 *                  refusal, a documented default, or a no-op, and never a crash -- and the
 *                  sweep is what makes "uniform" a fact rather than a claim.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends
 * on no Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. Capabilities                                                     */
/* ------------------------------------------------------------------ */

/** One capability field: its name, and what this build must report for it. */
typedef struct paint_capability_row {
    const char *name;
    int value;
} paint_capability_row_t;

/*
 * Declaration order, and the order is the assertion: the walk below pairs each name with
 * the field at the same offset, so a field inserted anywhere but the end shows up as every
 * row after it disagreeing -- which is the loudest possible failure and the right one.
 */
static const paint_capability_row_t paint_capability_rows[] = {
    { "context", 1 },
    { "operation_model", 1 },
    { "paint_list", 1 },
    { "traversal", 1 },
    { "box_geometry", 1 },
    { "clip_state", 1 },
    { "decoration_ops", 1 },
    /* The four absences. `drawing` is read from its own constant so the field and the
     * two doors cannot drift; the other three name later phases outright. */
    { "drawing", ROBO_PAINT_DRAWING_IMPLEMENTED },
    { "display_lists", 0 },
    { "raster", 0 },
    { "paint_invalidation", 0 }
};

static void test_paint_conformance_capabilities(void)
{
    robo_paint_capabilities_t capabilities;
    int values[sizeof(robo_paint_capabilities_t) / sizeof(int)];
    size_t index;

    paint_fixture();
    /*
     * The walk below is positional, so the *counts* are what make it sound: the struct
     * has exactly one `int` per row and no padding, or a row would be asserting the
     * neighbouring field.
     */
    ROBO_CHECK_EQ_INT((int)(sizeof(robo_paint_capabilities_t) / sizeof(int)),
                      (int)(sizeof(paint_capability_rows) / sizeof(paint_capability_rows[0])));
    ROBO_CHECK_EQ_INT((int)sizeof(robo_paint_capabilities_t),
                      (int)(sizeof(paint_capability_rows) / sizeof(paint_capability_rows[0])) *
                          (int)sizeof(int));
    ROBO_CHECK_EQ_INT(robo_paint_get_capabilities(&capabilities), ROBO_OK);

    memset(values, 0, sizeof(values));
    memcpy(values, &capabilities, sizeof(capabilities));
    for (index = 0u; index < sizeof(paint_capability_rows) / sizeof(paint_capability_rows[0]);
         index++) {
        if (values[index] != paint_capability_rows[index].value) {
            robo_test_fail(__FILE__, __LINE__, "capability %s is %d, expected %d",
                           paint_capability_rows[index].name, values[index],
                           paint_capability_rows[index].value);
        }
        /* Every field is a flag, so a value that is neither 0 nor 1 is a capability that
         * cannot be read as one. */
        ROBO_CHECK(values[index] == 0 || values[index] == 1);
    }

    /* `capabilities_init` writes the "nothing" state rather than a build's list, so a
     * caller can tell "I did not ask" from "I asked and this build does nothing". */
    robo_paint_capabilities_init(&capabilities);
    {
        int cleared[sizeof(robo_paint_capabilities_t) / sizeof(int)];
        size_t cleared_index;

        memset(cleared, 0, sizeof(cleared));
        memcpy(cleared, &capabilities, sizeof(capabilities));
        for (cleared_index = 0u;
             cleared_index < sizeof(cleared) / sizeof(cleared[0]); cleared_index++) {
            ROBO_CHECK_EQ_INT(cleared[cleared_index], 0);
        }
    }
    robo_paint_capabilities_init(NULL); /* safe */
}

static void test_paint_conformance_drawing_absent(void)
{
    robo_paint_capabilities_t capabilities;
    robo_i32 major = 0;
    robo_i32 minor = 0;
    robo_i32 patch = 0;

    paint_fixture();

    /*
     * The statement 11.1 makes in three places, asserted in one: the constant is 0, the
     * door answers 0, and the capability field reports it. A caller deciding whether to
     * look for a surface reads the door; one deciding whether to *make* one reads the
     * field; and neither can be true while the other says otherwise.
     */
    ROBO_CHECK_EQ_INT(ROBO_PAINT_DRAWING_IMPLEMENTED, 0);
    ROBO_CHECK_EQ_INT(robo_paint_drawing_is_implemented(), 0);
    ROBO_CHECK_EQ_INT(robo_paint_get_capabilities(&capabilities), ROBO_OK);
    ROBO_CHECK_EQ_INT(capabilities.drawing, robo_paint_drawing_is_implemented());

    /* The version this build reports is 0.2.0 and its API is not frozen -- two facts a
     * consumer of a first release needs, and the second is the one that says a later goal
     * may still add to this surface. */
    ROBO_CHECK_EQ_INT(robo_paint_get_version(&major, &minor, &patch), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)major, 0);
    ROBO_CHECK_EQ_INT((int)minor, 2);
    ROBO_CHECK_EQ_INT((int)patch, 0);
    ROBO_CHECK_STR_EQ(robo_paint_version_string(), ROBO_PAINT_VERSION_STRING);
    ROBO_CHECK_EQ_INT(ROBO_PAINT_API_FROZEN, 0);

    /*
     * What the release *does* claim has to be true of the build rather than of a header:
     * the decoration layer really is present, so a list from a document with a background
     * and a border really does contain those operations. The traversal suite proves it in
     * detail; this is the one-line version that keeps the capability field honest.
     */
    {
        paint_fixture_doc_t fixture;
        paint_result_t result;

        ROBO_CHECK_EQ_INT(capabilities.decoration_ops, 1);
        paint_begin_laid_out(&fixture,
                             "<html><body><div class=\"card\"></div></body></html>",
                             "html, body, div, section, aside, figure, p { display: block }"
                             " .card { width: 10px; height: 10px;"
                             "   background-color: #ff0000; border: 1px solid #000000 }");
        paint_once(&fixture, &result);
        ROBO_CHECK(paint_count_of(&result, ROBO_PAINT_OP_BACKGROUND) > 0u);
        ROBO_CHECK(paint_count_of(&result, ROBO_PAINT_OP_BORDER_TOP) > 0u);
        /* And what the list *is* is a description: the pass ran, the list is not empty,
         * and there is still nothing that puts a colour on a surface. */
        ROBO_CHECK(robo_paint_list_count(result.list) > 0u);
        ROBO_CHECK_EQ_INT(robo_paint_drawing_is_implemented(), 0);
        paint_result_dispose(&result);
        paint_end(&fixture);
    }
}

static void test_paint_conformance_vocabulary_exhaustive(void)
{
    size_t index;

    paint_fixture();

    /*
     * The vocabulary's shape as one statement: as many names as kinds, every kind in the
     * range known and named, and the three predicates agreeing with the enumeration.
     * A phase that added an enumerator without a name row would fail here -- which is the
     * whole point of the table being a table.
     */
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_OP_KIND_COUNT, 9);
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_DOMAIN_COUNT, 9);
    for (index = 0u; index < (size_t)ROBO_PAINT_OP_KIND_COUNT; index++) {
        robo_paint_op_kind_t kind = (robo_paint_op_kind_t)index;
        int is_structural = robo_paint_op_kind_is_structural(kind);
        int is_border = robo_paint_op_kind_is_border(kind);
        int is_drawing = robo_paint_op_kind_draws(kind);

        ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_known(kind), 1);
        ROBO_CHECK(strcmp(robo_paint_op_kind_name(kind), "unknown") != 0);
        /* Exactly one of structural/drawing for every kind but `NONE`, which is neither.
         * `CONTENT` is the kind that makes this the right pair to ask: it is structural
         * but not a box marker, so `is_marker + is_drawing` would leave it at 0. */
        if (kind == ROBO_PAINT_OP_NONE) {
            ROBO_CHECK_EQ_INT(is_structural, 0);
            ROBO_CHECK_EQ_INT(is_drawing, 0);
        } else {
            ROBO_CHECK_EQ_INT(is_structural + is_drawing, 1);
        }
        /* A drawing kind is exactly the border-or-background pair the builder asks for,
         * and the marker predicate is the strict subset of structural that excludes the
         * content boundary. */
        ROBO_CHECK_EQ_INT(is_drawing,
                          is_border || kind == ROBO_PAINT_OP_BACKGROUND ? 1 : 0);
        if (robo_paint_op_kind_is_marker(kind)) {
            ROBO_CHECK_EQ_INT(is_structural, 1);
            ROBO_CHECK(kind != ROBO_PAINT_OP_CONTENT);
        }
        /* A border kind has a side and nothing else does: no kind can be both a border
         * and a marker, and no side is invented for a kind that has none. */
        {
            robo_css_box_side_t side = ROBO_CSS_BOX_SIDE_TOP;
            robo_result_t status = robo_paint_op_kind_side(kind, &side);

            ROBO_CHECK_EQ_INT(status, is_border ? ROBO_OK : ROBO_INVALID_ARGUMENT);
            if (is_border) {
                ROBO_CHECK(side >= ROBO_CSS_BOX_SIDE_TOP &&
                           side < (robo_css_box_side_t)ROBO_CSS_BOX_SIDE_COUNT);
            }
        }
    }
    for (index = 0u; index < (size_t)ROBO_PAINT_DOMAIN_COUNT; index++) {
        ROBO_CHECK_EQ_INT(
            robo_paint_domain_is_known((robo_paint_diagnostic_domain_t)index), 1);
        ROBO_CHECK(strcmp(robo_paint_domain_name((robo_paint_diagnostic_domain_t)index),
                          "unknown") != 0);
    }

    /* The three pass outcomes 11.1 defines, and the fourth is the refusal that a caller
     * acts on -- which is why it is an outcome rather than an error. */
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_PASS_OUTCOME_COUNT, 4);
    ROBO_CHECK_EQ_INT(robo_paint_pass_outcome_is_known(ROBO_PAINT_PASS_NONE), 1);
    ROBO_CHECK_EQ_INT(robo_paint_pass_outcome_is_known(ROBO_PAINT_PASS_PAINTED), 1);
    ROBO_CHECK_EQ_INT(robo_paint_pass_outcome_is_known(ROBO_PAINT_PASS_FAILED), 1);
    ROBO_CHECK_EQ_INT(robo_paint_pass_outcome_is_known(ROBO_PAINT_PASS_STALE), 1);
    ROBO_CHECK_EQ_INT(robo_paint_pass_outcome_is_known((robo_paint_pass_outcome_t)99), 0);
    ROBO_CHECK_STR_EQ(robo_paint_pass_outcome_name(ROBO_PAINT_PASS_PAINTED), "painted");
    ROBO_CHECK_STR_EQ(robo_paint_pass_outcome_name(ROBO_PAINT_PASS_STALE), "stale");
    ROBO_CHECK_STR_EQ(robo_paint_pass_outcome_name((robo_paint_pass_outcome_t)99),
                      "unknown");
}

/* ------------------------------------------------------------------ */
/* 2. A document, and the invariants over the whole list               */
/* ------------------------------------------------------------------ */

static void test_paint_conformance_document_list(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    size_t index;

    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"card\"><section class=\"badge\"></section></div>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "<p class=\"para\">Text</p>"
                         "</body></html>",
                         "html, body, div, section, aside, figure, p { display: block }"
                         " .card { width: 120px; height: 40px; padding: 4px;"
                         "   border: 1px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px;"
                         "   background-color: #123456 }"
                         " .clip { width: 60px; height: 30px; overflow: hidden }"
                         " .spill { width: 200px; height: 200px;"
                         "   border: 2px solid #ff0000 }");
    paint_once(&fixture, &result);

    ROBO_CHECK(robo_paint_list_count(result.list) > 0u);
    for (index = 0u; index < robo_paint_list_count(result.list); index++) {
        robo_paint_operation_t operation;
        int kind_structural;

        ROBO_CHECK_EQ_INT(robo_paint_list_at(result.list, index, &operation), ROBO_OK);

        /* Every operation is a record -- which `robo_paint_list_append` enforced, so this
         * is the container's promise asked of a whole document rather than of one case. */
        ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 1);
        /* Every kind is in the vocabulary: a list can never carry a value that prints
         * "unknown", because there is no door that would accept one. */
        ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_known(operation.kind), 1);
        ROBO_CHECK(operation.kind != ROBO_PAINT_OP_NONE);
        /* Every operation names the box a reader verifies it against. */
        ROBO_CHECK(operation.node != ROBO_LAYOUT_NODE_ID_NONE);
        ROBO_CHECK_EQ_INT(robo_layout_object_is_live(fixture.tree, operation.node), 1);

        /* Structural and drawing agree with the colour rule, in both directions: a
         * structural record carries RoboCSS's own "no colour" and a drawing carries one a
         * rasteriser can use. That is not a tautology -- it is the rule this module's
         * whole validity story rests on, asked once more over a document -- and it is the
         * *structural* predicate rather than the marker one, so a `CONTENT` record is held
         * to the no-colour side rather than asked for a colour it has no use for. */
        kind_structural = robo_paint_operation_is_structural(&operation);
        if (kind_structural) {
            ROBO_CHECK_EQ_INT((int)robo_paint_operation_color(&operation).kind,
                              (int)ROBO_CSS_COLOR_NONE);
        } else {
            ROBO_CHECK_EQ_INT(robo_css_color_is_valid(robo_paint_operation_color(&operation)),
                              1);
        }

        /* Nested rectangles, and the two coordinate spaces agreeing with the tree -- the
         * widest scope at which the phase's central claim can be asked. */
        ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&operation.boxes), 1);
        paint_local_boxes_match_layout(&fixture, operation);
    }

    /* The marker pairs balance, and the stats agree with the list: the two facts a
     * reader uses to tell a document that painted in full from one half of which was
     * never laid out. */
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BEGIN_BOX),
                      (int)paint_count_of(&result, ROBO_PAINT_OP_END_BOX));
    ROBO_CHECK_EQ_INT((int)result.stats.boxes,
                      (int)paint_count_of(&result, ROBO_PAINT_OP_BEGIN_BOX));
    /* The content boundary is unconditional, so it is exactly one per placed box. */
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_CONTENT),
                      (int)result.stats.boxes);
    ROBO_CHECK_EQ_INT((int)result.stats.ops, (int)robo_paint_list_count(result.list));
    ROBO_CHECK_EQ_INT((int)result.stats.visited, (int)result.stats.objects);
    ROBO_CHECK((int)result.stats.text_runs >= 1);
    ROBO_CHECK((int)result.stats.clipped >= 1);
    ROBO_CHECK((int)result.stats.backgrounds >= 2);
    ROBO_CHECK_EQ_INT((int)result.stats.passes, 1);

    /* And the dump of that list is a dump: one line per operation, every one of them
     * describing the operation the list holds. A dump that disagreed with the record
     * would be a second implementation of it, so this asks the two agree in count. */
    {
        char buffer[8192];
        size_t length = 0u;
        size_t lines = 0u;
        const char *scan;

        memset(buffer, 0, sizeof(buffer));
        ROBO_CHECK_EQ_INT(robo_paint_list_write(result.list, buffer, sizeof(buffer),
                                                &length, NULL),
                          ROBO_OK);
        scan = buffer;
        while (*scan != '\0') {
            if (*scan == '\n') {
                lines++;
            }
            scan++;
        }
        ROBO_CHECK_EQ_INT((int)lines, (int)robo_paint_list_count(result.list));
    }

    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Hostile input                                                    */
/* ------------------------------------------------------------------ */

static void test_paint_conformance_hostile_inputs(void)
{
    robo_paint_boxes_t boxes;
    robo_paint_clip_t clip;
    robo_layout_rect_t rect;
    robo_paint_operation_t operation;
    char buffer[16];
    size_t length = 0u;

    paint_fixture();

    /*
     * The sweep: every door reached with the argument that must not crash it. The
     * contract is uniform -- a refusal, a documented default, or a no-op -- and the point
     * of asking all of them in one case is that "uniform" is a fact rather than a claim a
     * reader checks door by door.
     */
    robo_paint_boxes_init(NULL);
    robo_paint_clip_init(NULL);
    robo_paint_operation_init(NULL);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_active(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_empty(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_marker(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_drawing(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_operation_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_paint_operation_side(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(NULL, &rect, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_operation_write(NULL, buffer, sizeof(buffer), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_of(NULL, ROBO_LAYOUT_NODE_ID_NONE, &boxes, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_translate(NULL, 0, 0, &boxes, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(NULL, NULL, ROBO_CSS_OVERFLOW_VISIBLE,
                                                ROBO_CSS_OVERFLOW_VISIBLE, &clip, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(NULL, &rect), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_list_append(NULL, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_list_at(NULL, 0u, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(NULL), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_capacity(NULL), 0);
    ROBO_CHECK(robo_paint_list_operations(NULL) == NULL);
    ROBO_CHECK_EQ_INT(robo_paint_list_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count_of_kind(NULL, ROBO_PAINT_OP_NONE), 0);
    robo_paint_list_clear(NULL);
    robo_paint_list_destroy(NULL);

    /* An id that names no box is a miss rather than a read of whatever occupies the slot
     * -- and a `NONE` id is an argument error rather than a miss, which is the one
     * distinction every read door in this tree draws the same way. */
    {
        paint_fixture_doc_t fixture;
        robo_layout_node_id_t missing;

        paint_begin_laid_out(&fixture, "<html><body></body></html>",
                             "html, body, div, section, aside, figure, p { display: block }");
        missing = (robo_layout_node_id_t)999999u;
        ROBO_CHECK_EQ_INT(robo_paint_boxes_of(fixture.tree, missing, &boxes, NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_paint_boxes_of(fixture.tree, ROBO_LAYOUT_NODE_ID_NONE, &boxes,
                                              NULL),
                          ROBO_INVALID_ARGUMENT);
        paint_end(&fixture);
    }

    /* An operation built the wrong way round is refused by the list rather than stored,
     * which is the boundary the whole "every list is a list of records" claim rests on. */
    {
        robo_paint_list_t *list = NULL;

        robo_paint_operation_init(&operation);
        ROBO_CHECK_EQ_INT(robo_paint_list_create(&list, NULL), ROBO_OK);
        operation.kind = ROBO_PAINT_OP_END_BOX;
        operation.node = (robo_layout_node_id_t)1u;
        operation.color = robo_css_color_make(1, 1, 1, 255); /* a marker with a colour */
        ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &operation, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT((int)robo_paint_list_count(list), 0);
        robo_paint_list_destroy(list);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_paint_conformance(void)
{
    robo_test_register("paint.conformance.capabilities",
                       test_paint_conformance_capabilities);
    robo_test_register("paint.conformance.drawing_absent",
                       test_paint_conformance_drawing_absent);
    robo_test_register("paint.conformance.vocabulary_exhaustive",
                       test_paint_conformance_vocabulary_exhaustive);
    robo_test_register("paint.conformance.document_list",
                       test_paint_conformance_document_list);
    robo_test_register("paint.conformance.hostile_inputs",
                       test_paint_conformance_hostile_inputs);
}
