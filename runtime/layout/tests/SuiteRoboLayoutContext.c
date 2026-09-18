/*
 * RoboLayout context suite (Phase 10.1) — layout.context.*
 *
 * One pass: the tree it ran on, the constraints it ran with, what it saw, and
 * where a later goal hangs its scratch state.
 *
 * The pass is the awkward part of this phase to test *well*, because its whole
 * output is a statement of what has *not* been computed. So every counter is
 * asserted against a tree whose shape the suite built itself, and the central
 * assertion is one no other suite can make: after a pass, every box's result is
 * UNRESOLVED and reading its geometry is a *miss*. A build that returned a zeroed
 * rectangle instead would pass a "did the pass run" test and fail this one -- and
 * the difference is a box painted at the origin versus a box skipped.
 *
 * `boxes_computed` is asserted to be 0 and `geometry_is_implemented()` to be 0 in
 * the same test, because those are one fact stated twice and a build that flipped
 * one would have two answers to one question.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A tree to run on                                                    */
/* ------------------------------------------------------------------ */

typedef struct layout_context_fixture {
    layout_document_t doc;
    robo_layout_tree_t *tree;
    robo_layout_node_id_t root;
} layout_context_fixture_t;

#define LAYOUT_CONTEXT_FIXTURE_HTML \
    "<div>\n"                       \
    "  <section>\n"                 \
    "    <p>one</p>\n"              \
    "    <p>two</p>\n"              \
    "  </section>\n"                \
    "  <aside></aside>\n"           \
    "</div>\n"

static void layout_context_tree_begin(layout_context_fixture_t *fixture,
                                      const char *html,
                                      const robo_layout_style_source_t *styles,
                                      robo_layout_build_report_t *out_report)
{
    robo_layout_build_options_t options;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                           &options, out_report, NULL),
                      ROBO_OK);
    fixture->root = robo_layout_tree_root(fixture->tree);
}

static void layout_context_tree_end(layout_context_fixture_t *fixture)
{
    robo_layout_tree_destroy(fixture->tree);
    fixture->tree = NULL;
    layout_document_release(&fixture->doc);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void test_context_lifecycle(void)
{
    robo_layout_context_t *context = NULL;
    robo_layout_stats_t stats;
    robo_layout_constraints_t constraints;
    robo_layout_size_t size;

    layout_fixture();

    ROBO_CHECK_EQ_INT(robo_layout_context_create(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK(context != NULL);

    /* Nothing has run, so nothing is claimed. */
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context), (int)ROBO_LAYOUT_PASS_NONE);
    ROBO_CHECK(robo_layout_context_tree(context) == NULL);
    ROBO_CHECK(robo_layout_context_user_data(context) == NULL);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.objects, 0u);
    ROBO_CHECK_EQ_INT(stats.visited, 0u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 0u);
    ROBO_CHECK_EQ_INT(stats.passes, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_context_constraints(context, &constraints), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_available(&constraints), 0);

    /* Every read door answers a sane thing for a NULL context rather than
     * crashing: a caller that lost its handle gets "nothing", not a fault. */
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(NULL), (int)ROBO_LAYOUT_PASS_NONE);
    ROBO_CHECK(robo_layout_context_tree(NULL) == NULL);
    ROBO_CHECK(robo_layout_context_user_data(NULL) == NULL);
    robo_layout_context_stats(NULL, &stats);
    ROBO_CHECK_EQ_INT(stats.visited, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_context_viewport(NULL, &size), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_context_constraints(NULL, &constraints),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_context_viewport(context, NULL), ROBO_INVALID_ARGUMENT);

    robo_layout_context_set_user_data(NULL, context);
    robo_layout_context_reset(NULL);
    robo_layout_context_destroy(NULL);
    robo_layout_context_destroy(context);
}

static void test_context_reset(void)
{
    robo_layout_context_t *context = NULL;
    layout_context_fixture_t fixture;
    robo_layout_size_t viewport;
    robo_layout_constraints_t after_reset;
    robo_layout_stats_t stats;
    int *marker;
    int value = 7;

    layout_fixture();
    marker = &value;

    layout_context_tree_begin(&fixture, LAYOUT_CONTEXT_FIXTURE_HTML, NULL, NULL);
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);

    viewport.width = 1024;
    viewport.height = 768;
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    robo_layout_context_set_user_data(context, marker);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                      (int)ROBO_LAYOUT_PASS_WALKED);
    ROBO_CHECK(robo_layout_context_tree(context) == fixture.tree);

    robo_layout_context_reset(context);

    /* "This context has run nothing" is the state reset restores, not "this
     * context has run something else": the pass count goes to zero with the
     * rest. */
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context), (int)ROBO_LAYOUT_PASS_NONE);
    ROBO_CHECK(robo_layout_context_tree(context) == NULL);
    ROBO_CHECK_EQ_INT(robo_layout_context_viewport(context, &viewport), ROBO_NOT_FOUND);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.passes, 0u);
    ROBO_CHECK_EQ_INT(stats.visited, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_context_constraints(context, &after_reset), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_available(&after_reset), 0);

    /* User data belongs to the caller and survives a reset: a caller that
     * installs its scratch once and resets per pass is the normal way to use
     * this. */
    ROBO_CHECK(robo_layout_context_user_data(context) == marker);

    robo_layout_context_destroy(context);
    layout_context_tree_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Viewport and constraints                                            */
/* ------------------------------------------------------------------ */

static void test_context_viewport(void)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_size_t out;
    robo_layout_constraints_t constraints;
    robo_layout_size_t negative;

    layout_fixture();
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);

    /* Nothing recorded means NOT_FOUND, not an invented screen size. */
    out.width = -5;
    out.height = -5;
    ROBO_CHECK_EQ_INT(robo_layout_context_viewport(context, &out), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(out.width, -5);

    viewport.width = 1280;
    viewport.height = 800;
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_viewport(context, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 1280);
    ROBO_CHECK_EQ_INT(out.height, 800);

    /* The viewport is recorded as the *available* space and as nothing else --
     * whether a box fills it is a formatting-context decision (10.2), so a
     * context that also treated it as a maximum would be answering for a later
     * goal. */
    ROBO_CHECK_EQ_INT(robo_layout_context_constraints(context, &constraints), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_available(&constraints), 1);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_maximum(&constraints), 0);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_minimum(&constraints), 0);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_available(&constraints, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 1280);
    ROBO_CHECK_EQ_INT(out.height, 800);

    /* A negative viewport is a caller bug, and it is refused rather than clamped
     * into something that looks like a screen. */
    negative.width = -1280;
    negative.height = 800;
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, negative, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_context_viewport(context, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 1280);

    robo_layout_context_destroy(context);
}

static void test_context_constraints(void)
{
    robo_layout_context_t *context = NULL;
    robo_layout_constraints_t constraints;
    robo_layout_constraints_t out;
    robo_layout_size_t size;

    layout_fixture();
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);

    /* No constraints installed reads back as the cleared record rather than as a
     * failure: "nothing is known" is not an error. */
    ROBO_CHECK_EQ_INT(robo_layout_context_constraints(context, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_available(&out), 0);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_minimum(&out), 0);

    /* Install a pair of bounds and check the record travels by value. */
    robo_layout_constraints_init(&constraints);
    size.width = 640;
    size.height = 480;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_minimum(&constraints, size, NULL), ROBO_OK);
    size.width = 640;
    size.height = 480;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_maximum(&constraints, size, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_constraints(context, &constraints, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_constraints(context, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_minimum(&out), 1);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_maximum(&out), 1);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_equal(&constraints, &out), 1);

    /* Setting the viewport and setting the constraints are independent: a pass
     * may be run with bounds that come from somewhere other than a viewport. */
    size.width = 320;
    size.height = 240;
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, size, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_constraints(context, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_maximum(&out), 1);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_maximum(&out, &size), ROBO_OK);
    ROBO_CHECK_EQ_INT(size.width, 640);

    ROBO_CHECK_EQ_INT(robo_layout_context_set_constraints(context, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_constraints(NULL, &constraints, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_layout_context_destroy(context);
}

static void test_context_user_data(void)
{
    robo_layout_context_t *context = NULL;
    int marker = 3;
    int other = 4;

    layout_fixture();
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);

    /* The extension point a later goal uses instead of growing the struct:
     * RoboLayout never dereferences it, so what it points at is entirely the
     * caller's business. */
    ROBO_CHECK(robo_layout_context_user_data(context) == NULL);
    robo_layout_context_set_user_data(context, &marker);
    ROBO_CHECK(robo_layout_context_user_data(context) == &marker);
    robo_layout_context_set_user_data(context, &other);
    ROBO_CHECK(robo_layout_context_user_data(context) == &other);
    /* NULL is a legal value and means "no scratch". */
    robo_layout_context_set_user_data(context, NULL);
    ROBO_CHECK(robo_layout_context_user_data(context) == NULL);

    robo_layout_context_destroy(context);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

static void test_context_run(void)
{
    robo_layout_context_t *context = NULL;
    layout_context_fixture_t fixture;
    robo_layout_build_report_t report;
    robo_layout_stats_t stats;
    size_t index;
    uint32_t deepest = 0u;
    robo_layout_result_t result;
    robo_layout_point_t position;
    robo_layout_size_t size;
    robo_layout_rect_t rect;

    layout_fixture();
    layout_context_tree_begin(&fixture, LAYOUT_CONTEXT_FIXTURE_HTML, NULL, &report);
    ROBO_CHECK(report.boxes_created > 0u);
    ROBO_CHECK_EQ_INT((int)fixture.root, 1);

    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                      (int)ROBO_LAYOUT_PASS_WALKED);
    ROBO_CHECK(robo_layout_context_tree(context) == fixture.tree);

    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.objects, report.boxes_created);
    ROBO_CHECK_EQ_INT(stats.visited, report.boxes_created);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, report.boxes_created);
    ROBO_CHECK_EQ_INT(stats.passes, 1u);
    /* No style source was supplied, so every box is marked and *counted*: it is a
     * fact about the build, not a failure. */
    ROBO_CHECK_EQ_INT(stats.without_style, report.boxes_created);

    /* The pass is the thing that moves a box from "built" to "unresolved", so the
     * counters and the results agree. */
    for (index = 0u; index < report.boxes_created; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
        uint32_t depth = 0u;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, index, &id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_UNRESOLVED);
        ROBO_CHECK_EQ_INT(robo_layout_result_is_computed(&result), 0);
        ROBO_CHECK_EQ_INT(result.flags, 0u);
        ROBO_CHECK_EQ_INT(robo_layout_object_depth(fixture.tree, id, &depth), ROBO_OK);
        if (depth > deepest) {
            deepest = depth;
        }
    }
    ROBO_CHECK_EQ_INT(stats.depth_max, deepest);

    /* And this is the phase in one assertion: reading a result tells the truth
     * instead of handing back (0, 0). */
    ROBO_CHECK_EQ_INT(result.state, ROBO_LAYOUT_RESULT_UNRESOLVED);
    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_result_rect(&result, &rect, NULL), ROBO_NOT_FOUND);

    /* The two statements of "no coordinate exists" cannot disagree. */
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_geometry_is_implemented(), 0);

    robo_layout_context_destroy(context);
    layout_context_tree_end(&fixture);
}

static void test_context_run_repeated(void)
{
    robo_layout_context_t *context = NULL;
    layout_context_fixture_t fixture;
    robo_layout_build_report_t report;
    robo_layout_stats_t first;
    robo_layout_stats_t second;
    robo_layout_result_t result;
    int run;

    layout_fixture();
    layout_context_tree_begin(&fixture, LAYOUT_CONTEXT_FIXTURE_HTML, NULL, &report);
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);

    /* A caller reading stats after a second pass wants *that* pass's numbers, and
     * a caller asking how many passes have run wants the total. Both are true at
     * once, which is why `passes` is cumulative and everything else is not. */
    for (run = 0; run < 4; run++) {
        ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
        robo_layout_context_stats(context, &first);
        ROBO_CHECK_EQ_INT(first.objects, report.boxes_created);
        ROBO_CHECK_EQ_INT(first.visited, report.boxes_created);
        ROBO_CHECK_EQ_INT(first.boxes_unresolved, report.boxes_created);
        ROBO_CHECK_EQ_INT(first.passes, (size_t)(run + 1));
        ROBO_CHECK_EQ_INT(first.boxes_computed, 0u);
        (void)second;
    }

    /* A repeated pass does not accumulate: every box is UNRESOLVED, not
     * "unresolved twice". */
    {
        robo_layout_node_id_t id = fixture.root;

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_UNRESOLVED);
        ROBO_CHECK_EQ_INT(result.size.width, 0);
        ROBO_CHECK_EQ_INT(result.size.height, 0);
    }

    robo_layout_context_destroy(context);
    layout_context_tree_end(&fixture);
}

static void test_context_run_empty_tree(void)
{
    robo_layout_context_t *context = NULL;
    robo_layout_tree_t *tree = NULL;
    robo_layout_stats_t stats;

    layout_fixture();
    /* A document whose root element is `display: none` builds no boxes, and an
     * empty tree is a legitimate pass rather than an error. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                      (int)ROBO_LAYOUT_PASS_WALKED);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.objects, 0u);
    ROBO_CHECK_EQ_INT(stats.visited, 0u);
    ROBO_CHECK_EQ_INT(stats.depth_max, 0u);
    ROBO_CHECK_EQ_INT(stats.passes, 1u);

    robo_layout_context_destroy(context);
    robo_layout_tree_destroy(tree);
}

static void test_context_run_failures(void)
{
    robo_layout_context_t *context = NULL;
    layout_context_fixture_t fixture;
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;

    layout_fixture();
    layout_context_tree_begin(&fixture, LAYOUT_CONTEXT_FIXTURE_HTML, NULL, NULL);
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_layout_context_run(NULL, fixture.tree, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_CONTEXT);

    robo_layout_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, NULL, &diag), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INVALID_ARGUMENT);

    /* A failed pass says so rather than leaving the previous pass's outcome
     * standing, because a caller reads the outcome to decide whether the
     * counters mean anything. */
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                      (int)ROBO_LAYOUT_PASS_FAILED);

    robo_layout_context_destroy(context);
    layout_context_tree_end(&fixture);
}

static void test_context_run_with_styles(void)
{
    robo_layout_context_t *context = NULL;
    layout_context_fixture_t fixture;
    robo_layout_build_report_t report;
    layout_fake_source_t fake;
    robo_layout_style_source_t styles;
    robo_layout_stats_t stats;

    layout_fixture();
    layout_fake_init(&fake);
    layout_fake_add(&fake, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "p", ROBO_CSS_DISPLAY_BLOCK);
    /* `<aside>` is deliberately left out: an element the source answers nothing
     * about is a box with no style, not a missing box. */
    styles = layout_fake_source_of(&fake);

    layout_context_tree_begin(&fixture, LAYOUT_CONTEXT_FIXTURE_HTML, &styles, &report);
    ROBO_CHECK(report.boxes_created > 0u);
    ROBO_CHECK(report.boxes_without_style > 0u);
    ROBO_CHECK(report.boxes_without_style < report.boxes_created);

    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
    robo_layout_context_stats(context, &stats);
    /* The pass counts exactly what the build marked, so the two halves of the
     * module agree about which boxes have no style. */
    ROBO_CHECK_EQ_INT(stats.without_style, report.boxes_without_style);
    ROBO_CHECK_EQ_INT(stats.objects, report.boxes_created);
    /* The fake answered some elements, so it was actually consulted. */
    ROBO_CHECK(fake.answers > 0u);

    robo_layout_context_destroy(context);
    layout_fake_dispose(&fake);
    layout_context_tree_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The context owns nothing borrowed                                   */
/* ------------------------------------------------------------------ */

static void test_context_allocator_balance(void)
{
    size_t before;
    layout_context_fixture_t fixture;
    robo_layout_context_t *context = NULL;
    int pass;

    layout_fixture();
    /* The baseline is taken *before* the tree, so the comparison at the end covers
     * the tree's arena as well as the contexts: a context that leaked one of the
     * tree's boxes would show here and nowhere else. */
    before = layout_live_blocks();
    layout_context_tree_begin(&fixture, LAYOUT_CONTEXT_FIXTURE_HTML, NULL, NULL);

    /* Destroying a context frees the context and nothing else -- not the tree it
     * ran on, not the user data, not the styles. One context, many passes, one
     * allocation. */
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    for (pass = 0; pass < 32; pass++) {
        robo_layout_context_set_viewport(context, (robo_layout_size_t){ 800, 600 }, NULL);
        ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
    }
    robo_layout_context_destroy(context);

    /* And the tree it ran on is untouched by the pass having existed: still live,
     * still the same shape. */
    ROBO_CHECK_EQ_INT(robo_layout_object_is_live(fixture.tree, fixture.root), 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree) > 0u, 1);

    layout_context_tree_end(&fixture);
    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout_context(void);

void robo_test_register_layout_context(void)
{
    robo_test_register("layout.context.lifecycle", test_context_lifecycle);
    robo_test_register("layout.context.reset", test_context_reset);
    robo_test_register("layout.context.viewport", test_context_viewport);
    robo_test_register("layout.context.constraints", test_context_constraints);
    robo_test_register("layout.context.user_data", test_context_user_data);
    robo_test_register("layout.context.run", test_context_run);
    robo_test_register("layout.context.run.repeated", test_context_run_repeated);
    robo_test_register("layout.context.run.empty", test_context_run_empty_tree);
    robo_test_register("layout.context.run.failures", test_context_run_failures);
    robo_test_register("layout.context.run.styles", test_context_run_with_styles);
    robo_test_register("layout.context.allocator_balance", test_context_allocator_balance);
}
