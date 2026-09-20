/*
 * Shared RoboPaint test fixtures (Phase 11.1).
 *
 * Every suite in paint/tests includes this. It is not a framework -- the runtime has
 * exactly one of those (`Tests/Framework/RoboTest.h`) and this phase adds none, on
 * purpose. It holds the four things a paint suite would otherwise copy:
 *
 *   bring-up    RoboBase, RoboHTML, RoboCSS, RoboLayout and RoboPaint, in that order,
 *               because that is the order DEPENDENCIES.md puts them in;
 *
 *   a pipeline  a real document built by the real tree builder, styled by a real
 *               cascade, bridged to a real layout tree and run through a real layout
 *               pass -- and then *painted*. Nothing in a paint suite fabricates a
 *               layout result, and that is the point: a hand-built record would prove
 *               the painter reads a struct, where these cases prove it reads what
 *               RoboLayout concluded;
 *
 *   a measurer  the scripted text measurer the layout suites use, so that a document
 *               with text produces a *measured* run and the traversal's text branch is
 *               exercised rather than skipped for want of a font;
 *
 *   lookups     one expression for "the box of the element with this class", plus the
 *               operation walks a paint assertion needs. The class lookup is a real
 *               DOM search (`robo_html_node_find_first`) rather than an index, so a
 *               fixture that gains a wrapper does not silently assert the wrong box.
 *
 * The layout fixture is shared with layout/tests on purpose: `RoboLayoutTestSupport.h`
 * already owns document building, the CSS-side style context and the fake style source,
 * and a second copy here would be a second thing to keep in step with the subsystem this
 * module merely consumes.
 *
 * Every helper asserts rather than returning a status, which is the convention the
 * layout suites set: a suite that had to check each step of its own scaffolding would
 * drown its subject in harness.
 */

#ifndef ROBO_PAINT_TEST_SUPPORT_H
#define ROBO_PAINT_TEST_SUPPORT_H

#include "RoboPaint/RoboPaint.h"

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Bring-up                                                           */
/* ------------------------------------------------------------------ */

/** Bring every layer a paint test needs up (once) and leave them up. */
static inline void paint_fixture(void)
{
    layout_fixture();
    if (!robo_paint_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_paint_init(), ROBO_OK);
    }
}

/** Live allocator blocks: the number a suite asserts returns to its baseline. */
static inline size_t paint_live_blocks(void)
{
    return robo_mem_debug_live_count();
}

/* ------------------------------------------------------------------ */
/* A scripted measurer                                                 */
/* ------------------------------------------------------------------ */

/**
 * One scripted answer: this many px per code point, and this line extent.
 *
 * The shape `SuiteRoboLayoutLineBox.c` and the layout integration suite both use, and
 * deliberately not shared with either: a shared measurer would be a third thing to keep
 * in step, and a paint suite varies it not at all -- it exists here so that a text run is
 * *measured* and the traversal's "counted and not painted" branch is exercised.
 */
typedef struct paint_script {
    int32_t per_code_point;
    int32_t height;
    size_t calls;
} paint_script_t;

static inline robo_result_t paint_measure(void *userdata,
                                   const robo_layout_text_request_t *request,
                                   robo_layout_text_run_t *out_run)
{
    paint_script_t *script = (paint_script_t *)userdata;

    if (!script || !request || !out_run || !request->style) {
        return ROBO_INVALID_ARGUMENT;
    }
    script->calls++;
    out_run->source = request->text_node;
    out_run->range = request->range;
    out_run->direction = request->style->direction;
    out_run->code_points = robo_layout_text_range_length(request->range);
    out_run->width = (int32_t)out_run->code_points * script->per_code_point;
    out_run->height = script->height;
    out_run->ascent = script->height;
    out_run->descent = 0;
    return ROBO_OK;
}

static inline robo_layout_text_metrics_t paint_metrics_of(paint_script_t *script)
{
    robo_layout_text_metrics_t metrics;

    robo_layout_text_metrics_init(&metrics);
    metrics.userdata = script;
    metrics.measure = paint_measure;
    return metrics;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/** Everything one paint document needs: DOM, bridge, styles, tree, measurer. */
typedef struct paint_fixture_doc {
    layout_document_t doc;
    layout_css_fixture_t css;
    robo_layout_dom_t *dom;
    robo_layout_tree_t *tree;
    robo_layout_style_source_t source;
    paint_script_t script;
} paint_fixture_doc_t;

static inline void paint_begin(paint_fixture_doc_t *fixture, const char *html, const char *css)
{
    robo_layout_build_options_t options;

    memset(fixture, 0, sizeof(*fixture));
    paint_fixture();
    layout_document_build(&fixture->doc, html);
    layout_css_begin(&fixture->css);
    layout_css_sheet(&fixture->css, css);
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(fixture->css.context, fixture->doc.id,
                                             &fixture->dom, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(fixture->dom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(fixture->dom, &fixture->source, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->source;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
    fixture->script.per_code_point = 8;
    fixture->script.height = 12;
}

static inline void paint_end(paint_fixture_doc_t *fixture)
{
    /* The tree borrows the bridge's styles, so it goes first. */
    robo_layout_tree_destroy(fixture->tree);
    fixture->tree = NULL;
    robo_layout_dom_destroy(fixture->dom);
    fixture->dom = NULL;
    layout_css_end(&fixture->css);
    layout_document_release(&fixture->doc);
}

/**
 * One full layout pass, with the context destroyed afterwards.
 *
 * The diagnostic is captured and reported rather than passed as NULL: a pass that
 * refuses a document says why, and a suite that discarded the message would turn "the
 * pass failed" into an evening of bisecting a fixture.
 */
static inline void paint_layout(paint_fixture_doc_t *fixture, int32_t width, int32_t height)
{
    robo_layout_context_t *context = NULL;
    robo_layout_text_metrics_t metrics;
    robo_layout_size_t viewport;
    robo_layout_diag_t diag;
    robo_result_t status;

    viewport.width = width;
    viewport.height = height;
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    metrics = paint_metrics_of(&fixture->script);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_text_metrics(context, &metrics), ROBO_OK);
    robo_layout_diag_reset(&diag);
    status = robo_layout_context_run(context, fixture->tree, &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the layout pass refused the document: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
    robo_layout_context_destroy(context);
}

/* ------------------------------------------------------------------ */
/* The rule the fixture is here to prove                               */
/* ------------------------------------------------------------------ */

/**
 * A document that has been **laid out**: the tree every paint case paints.
 *
 * `paint_begin` + `paint_layout` is the two-line preamble of every case that is about
 * painting rather than about the pipeline, and the pair exists so that a case can say
 * "given a laid-out document" in one line. The viewport is 800x600 -- wide enough that
 * nothing in these fixtures wraps unless it is meant to.
 */
static inline void paint_begin_laid_out(paint_fixture_doc_t *fixture,
                                        const char *html,
                                        const char *css)
{
    paint_begin(fixture, html, css);
    paint_layout(fixture, 800, 600);
}

/* ------------------------------------------------------------------ */
/* Lookups                                                             */
/* ------------------------------------------------------------------ */

/** Does @p view carry class @p user (an exact single-class match)? */
static inline int paint_match_class(const robo_html_node_view_t *view, void *user)
{
    const char *wanted = (const char *)user;
    robo_html_attribute_t attribute;

    if (!view || !wanted || view->kind != ROBO_HTML_NODE_KIND_ELEMENT) {
        return 0;
    }
    if (robo_html_element_find_attribute(&view->handle, "class", &attribute, NULL) != ROBO_OK) {
        return 0;
    }
    return attribute.value && strcmp(attribute.value, wanted) == 0;
}

/** The element with class @p name in @p fixture, or an unformed handle. */
static inline robo_html_node_handle_t paint_find(paint_fixture_doc_t *fixture,
                                                 const char *name)
{
    robo_html_node_handle_t root = robo_html_document_root(fixture->doc.id);
    robo_html_node_handle_t found;

    robo_html_node_handle_clear(&found);
    if (!robo_html_node_handle_is_wellformed(&root)) {
        return found;
    }
    if (robo_html_node_find_first(&root, ROBO_HTML_NODE_FILTER_ELEMENTS, paint_match_class,
                                  (void *)name, &found, NULL) != ROBO_OK) {
        robo_html_node_handle_clear(&found);
    }
    return found;
}

/** The box built for the element with class @p name, asserted to exist. */
static inline robo_layout_node_id_t paint_box(paint_fixture_doc_t *fixture, const char *name)
{
    robo_html_node_handle_t element = paint_find(fixture, name);
    robo_layout_node_id_t id;

    ROBO_CHECK_EQ_INT((int)robo_html_node_handle_is_wellformed(&element), 1);
    id = robo_layout_object_id_for_dom_node(fixture->tree, &element);
    ROBO_CHECK(id != ROBO_LAYOUT_NODE_ID_NONE);
    return id;
}

/**
 * The box built for the first element named @p tag, asserted to exist.
 *
 * The fixtures that have exactly one element of a kind use this -- `html`, `body` -- and
 * the ones with several use `paint_box`: a tag lookup with two `<div>`s would silently
 * assert whichever the parser reached first, which is the kind of harness bug that makes
 * a suite pass for the wrong reason.
 */
static inline robo_layout_node_id_t paint_box_by_tag(paint_fixture_doc_t *fixture, const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);
    robo_layout_node_id_t id;

    ROBO_CHECK_EQ_INT((int)robo_html_node_handle_is_wellformed(&element), 1);
    id = robo_layout_object_id_for_dom_node(fixture->tree, &element);
    ROBO_CHECK(id != ROBO_LAYOUT_NODE_ID_NONE);
    return id;
}

/* ------------------------------------------------------------------ */
/* Running a paint pass                                                */
/* ------------------------------------------------------------------ */

/** What one paint pass left behind: the list, and what the pass said it saw. */
typedef struct paint_result {
    robo_paint_list_t *list;
    robo_paint_stats_t stats;
    robo_paint_pass_outcome_t outcome;
} paint_result_t;

/** Create an empty list and a pass record. */
static inline void paint_result_init(paint_result_t *result)
{
    result->list = NULL;
    memset(&result->stats, 0, sizeof(result->stats));
    result->outcome = ROBO_PAINT_PASS_NONE;
    ROBO_CHECK_EQ_INT(robo_paint_list_create(&result->list, NULL), ROBO_OK);
}

static inline void paint_result_dispose(paint_result_t *result)
{
    robo_paint_list_destroy(result->list);
    result->list = NULL;
}

/**
 * Paint @p fixture's laid-out tree, reporting the outcome rather than asserting it.
 *
 * The outcome is deliberately *not* checked here: three of the cases below are about a
 * pass that refuses (a stale tree, an empty tree, a document that draws nothing), and a
 * helper that asserted PAINTED would make those unexpressible. The diagnostic is
 * reported when the pass *fails*, because a failure is always a bug in the case.
 */
static inline void paint_run(paint_fixture_doc_t *fixture,
                      robo_paint_context_t *context,
                      robo_paint_list_t *list)
{
    robo_paint_diag_t diag;
    robo_result_t status;

    robo_paint_diag_reset(&diag);
    status = robo_paint_context_run(context, fixture->tree, list, &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the paint pass refused the document: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
}

/** The whole pipeline in one call: a fresh context, a fresh list, one pass. */
static inline void paint_once(paint_fixture_doc_t *fixture, paint_result_t *out)
{
    robo_paint_context_t *context = NULL;

    paint_result_init(out);
    ROBO_CHECK_EQ_INT(robo_paint_context_create(&context, NULL), ROBO_OK);
    paint_run(fixture, context, out->list);
    ROBO_CHECK_EQ_INT((int)robo_paint_context_outcome(context),
                      (int)ROBO_PAINT_PASS_PAINTED);
    robo_paint_context_stats(context, &out->stats);
    out->outcome = robo_paint_context_outcome(context);
    robo_paint_context_destroy(context);
}

/* ------------------------------------------------------------------ */
/* Operations                                                          */
/* ------------------------------------------------------------------ */

/**
 * The @p nth operation of @p list whose kind is @p kind.
 *
 * The walk a suite would otherwise write five times, and the reason it is `nth` rather
 * than `first`: "the second border side draws" is a real question and "the first" is a
 * special case of it.
 *
 * @return 1 when the operation was found and @p out filled; 0 otherwise.
 */
static inline int paint_nth_of_kind(const paint_result_t *result,
                             robo_paint_op_kind_t kind,
                             size_t nth,
                             robo_paint_operation_t *out)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
            return 0;
        }
        if (operation.kind != kind) {
            continue;
        }
        if (seen == nth) {
            if (out) {
                *out = operation;
            }
            return 1;
        }
        seen++;
    }
    return 0;
}

/** How many operations of @p kind the pass produced. */
static inline size_t paint_count_of(const paint_result_t *result, robo_paint_op_kind_t kind)
{
    return robo_paint_list_count_of_kind(result->list, kind);
}

/** The first operation of @p kind, asserted to exist. */
static inline robo_paint_operation_t paint_require(const paint_result_t *result,
                                            robo_paint_op_kind_t kind)
{
    robo_paint_operation_t operation;

    robo_paint_operation_init(&operation);
    ROBO_CHECK_EQ_INT(paint_nth_of_kind(result, kind, 0u, &operation), 1);
    return operation;
}

/** The index of the first operation of @p kind, or `(size_t)-1`. */
static inline size_t paint_index_of_kind(const paint_result_t *result, robo_paint_op_kind_t kind)
{
    size_t index;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
            break;
        }
        if (operation.kind == kind) {
            return index;
        }
    }
    return (size_t)-1;
}

/* ------------------------------------------------------------------ */
/* Assertions                                                          */
/* ------------------------------------------------------------------ */

static inline void paint_rect_eq(robo_layout_rect_t actual,
                                 robo_layout_rect_t expected,
                                 const char *what)
{
    if (actual.x != expected.x || actual.y != expected.y ||
        actual.width != expected.width || actual.height != expected.height) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: expected %d,%d %dx%d, got %d,%d %dx%d", what, expected.x,
                       expected.y, expected.width, expected.height, actual.x, actual.y,
                       actual.width, actual.height);
    }
}

static inline void paint_rect_is(int32_t x, int32_t y, int32_t w, int32_t h,
                                 robo_layout_rect_t actual, const char *what)
{
    robo_layout_rect_t expected;

    expected.x = x;
    expected.y = y;
    expected.width = w;
    expected.height = h;
    paint_rect_eq(actual, expected, what);
}

/** Assert two colours are the same record. */
static inline void paint_color_eq(robo_css_color_t actual,
                                  robo_css_color_t expected,
                                  const char *what)
{
    if (!robo_css_color_equals(actual, expected)) {
        char actual_text[ROBO_CSS_COLOR_TEXT_MAX + 1u];
        char expected_text[ROBO_CSS_COLOR_TEXT_MAX + 1u];
        size_t length = 0u;

        (void)robo_css_color_write(actual, actual_text, sizeof(actual_text), &length, NULL);
        (void)robo_css_color_write(expected, expected_text, sizeof(expected_text), &length,
                                   NULL);
        robo_test_fail(__FILE__, __LINE__, "%s: expected %s, got %s", what, expected_text,
                       actual_text);
    }
}

/**
 * Assert that @p operation's boxes, turned back into layout coordinates, are exactly
 * what RoboLayout wrote for that box.
 *
 * This is the phase's central claim pinned as one call: the operation carries both
 * coordinate spaces, and the two must agree with the tree. A consumer that had only the
 * list and re-walked the tree to recover the other space is the "recalculate in paint"
 * this module exists to prevent, and this assertion is what makes carrying both safe
 * rather than a second opinion.
 */
static inline void paint_local_boxes_match_layout(paint_fixture_doc_t *fixture,
                                           robo_paint_operation_t operation)
{
    robo_paint_boxes_t local;
    robo_paint_boxes_t from_layout;

    memset(&local, 0, sizeof(local));
    memset(&from_layout, 0, sizeof(from_layout));
    ROBO_CHECK_EQ_INT(robo_paint_operation_local_boxes(&operation, &local, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_of(fixture->tree, operation.node, &from_layout, NULL),
                      ROBO_OK);
    if (!robo_paint_boxes_equal(&local, &from_layout)) {
        robo_test_fail(__FILE__, __LINE__,
                       "operation %d's local boxes are not what layout wrote for node %d",
                       (int)operation.kind, (int)operation.node);
    }
}

#endif /* ROBO_PAINT_TEST_SUPPORT_H */
