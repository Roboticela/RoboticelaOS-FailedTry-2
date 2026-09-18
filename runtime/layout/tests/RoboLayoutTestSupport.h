/*
 * Shared RoboLayout test fixtures (Phase 10.1).
 *
 * Every suite in layout/tests includes this. It is not a framework -- the runtime
 * has exactly one of those (Tests/Framework/RoboTest.h) and this phase adds none,
 * on purpose. It holds the four things a suite would otherwise copy:
 *
 *   fixtures   bring RoboBase, RoboHTML, RoboCSS and RoboLayout up, and build a
 *              real document from a literal
 *   a fake     a style source keyed by tag name, so that the *structural* suites
 *              can prove "a display: none element generates no box" without
 *              standing up a style context, a property table, a registry and a
 *              sheet
 *   a real one a style context with the standard property registry and an author
 *              sheet, for the suites that drive the DOM bridge
 *   helpers    one expression for "how many blocks does the allocator have live",
 *              which is how a leak is caught rather than suspected
 *
 * The CSS half is *shared with css/tests on purpose*. RoboCssTestSupport.h already
 * owns the sheet-parsing fixture and the fixture bring-up, and a second copy here
 * would be a second thing to keep in step with a subsystem this module merely
 * consumes -- the same reasoning the 9.4 DOM suite records for sharing it.
 */

#ifndef ROBO_LAYOUT_TEST_SUPPORT_H
#define ROBO_LAYOUT_TEST_SUPPORT_H

#include "RoboBase/RoboBase.h"
#include "RoboCSS/RoboCSS.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboHTML/RoboHTMLTypes.h"
#include "RoboLayout/RoboLayout.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

/* The CSS fixture helper (sheet parsing and the CSS-side bring-up), shared rather
 * than copied -- see the header comment. */
#include "RoboCssTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Bring-up                                                           */
/* ------------------------------------------------------------------ */

/** Bring every layer a layout test needs up (once) and leave them up. */
static inline void layout_fixture(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
    if (!robo_css_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_css_init(), ROBO_OK);
    }
    if (!robo_layout_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_layout_init(), ROBO_OK);
    }
}

/** Live allocator blocks: the number a suite asserts returns to its baseline. */
static inline size_t layout_live_blocks(void)
{
    return robo_mem_debug_live_count();
}

/** The maximum number of attributes any suite fixture sets. */
#define LAYOUT_TEST_MAX_ATTRIBUTES 8u

/**
 * A real document built from @p html by the real tree builder.
 *
 * The tree is built and *not* finalized, which is what the 9.4 DOM suite does too:
 * finalization closes construction for the parser, and a test that later wants to
 * create an element through the DOM seam (RoboHTMLElement.h) must not have closed
 * it.
 */
typedef struct layout_document {
    robo_html_document_id_t id;
} layout_document_t;

/* `robo_layout_diag_t diag` appears in these helpers and is passed to RoboHTML
 * doors that take a `robo_html_diag_t *`. The two records are different types on
 * purpose -- see RoboLayoutDiagnostics.h -- so the helpers below pass NULL to the
 * DOM and keep the failure as a result code, which is the fact a suite asserts. */

static inline void layout_document_build(layout_document_t *doc, const char *html)
{
    robo_html_source_t source;

    layout_fixture();
    doc->id = ROBO_HTML_DOCUMENT_ID_NONE;
    ROBO_CHECK_EQ_INT(robo_html_document_create(&doc->id, NULL), ROBO_OK);
    /* The source *borrows* @p html and owns nothing, so there is nothing to
     * release -- which is also why the fixture's markup must outlive the tree
     * builder's call, and why every fixture below passes a literal or a buffer
     * that outlives it. */
    robo_html_source_init(&source);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&source, html, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(doc->id, &source, NULL, NULL), ROBO_OK);
}

static inline void layout_document_release(layout_document_t *doc)
{
    if (robo_html_document_is_live(doc->id)) {
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc->id, NULL), ROBO_OK);
    }
    doc->id = ROBO_HTML_DOCUMENT_ID_NONE;
}

/**
 * The layout root's element: the first *element* child of the document node.
 *
 * Deliberately the same walk the builder performs rather than a search by tag
 * name, so a fixture whose root element is not `<html>` (a fragment, a bare
 * `<div>`) resolves the same way the builder would resolve it. A name-based
 * helper here would agree with the builder on every document this suite writes
 * and disagree on the one document that matters, which is the one where the
 * suite would be wrong rather than the builder.
 */
static inline robo_html_node_handle_t layout_document_element(const layout_document_t *doc)
{
    robo_html_node_handle_t document = robo_html_document_root(doc->id);
    robo_html_node_view_t document_view;
    robo_html_node_handle_t node;

    robo_html_node_handle_clear(&node);
    if (robo_html_node_get_view(&document, &document_view, NULL) != ROBO_OK) {
        return node;
    }
    /* The document node is not an element, so the walk starts at its first
     * *child* -- the document element -- exactly as the builder's does. */
    node = document_view.first_child;
    while (robo_html_node_handle_is_wellformed(&node)) {
        robo_html_node_view_t view;

        if (robo_html_node_get_view(&node, &view, NULL) != ROBO_OK) {
            break;
        }
        if (view.kind == ROBO_HTML_NODE_KIND_ELEMENT) {
            return node;
        }
        node = view.next_sibling;
    }
    robo_html_node_handle_clear(&node);
    return node;
}

/** Counts elements, for the walk below. */
typedef struct layout_element_counter {
    size_t elements;
    uint32_t depth_max;
} layout_element_counter_t;

static inline robo_result_t layout_count_element(const robo_html_node_view_t *view,
                                                 size_t depth,
                                                 void *user)
{
    layout_element_counter_t *counter = (layout_element_counter_t *)user;

    if (view && view->kind == ROBO_HTML_NODE_KIND_ELEMENT) {
        counter->elements++;
        if ((uint32_t)depth > counter->depth_max) {
            counter->depth_max = (uint32_t)depth;
        }
    }
    return ROBO_OK;
}

/**
 * How many elements the *document* has, by the DOM's own walk.
 *
 * The number a build's `elements_seen` must equal, and the reason these suites
 * ask rather than hardcode: RoboHTML's tree builder inserts the elements a
 * well-formed document requires (an implicit `<head>`, and `<html>`/`<body>`
 * around a bare fragment), so the element count of a fixture is a fact about the
 * *parser*. A suite that hardcoded its own guess would test its guess; one that
 * hardcodes the number *and* says which elements it is counting tests the parser
 * and the builder against each other, which is what is wanted.
 */
/**
 * The deepest box in @p tree, by walking it -- the number a build's `depth_max`
 * must equal and a pass's `depth_max` must equal.
 *
 * Separate from the report on purpose: comparing the report against a walk of the
 * tree is a check that the builder and the tree agree, where comparing it against
 * a number written in the test only checks the number.
 */
static inline uint32_t layout_tree_max_depth(const robo_layout_tree_t *tree)
{
    uint32_t deepest = 0u;
    size_t index;

    for (index = 0u; index < robo_layout_tree_object_count(tree); index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
        uint32_t depth = 0u;

        if (robo_layout_object_id_at(tree, index, &id) != ROBO_OK) {
            break;
        }
        if (robo_layout_object_depth(tree, id, &depth) != ROBO_OK) {
            break;
        }
        if (depth > deepest) {
            deepest = depth;
        }
    }
    return deepest;
}

static inline size_t layout_document_element_count(layout_document_t *doc)
{
    robo_html_node_handle_t root = robo_html_document_root(doc->id);
    layout_element_counter_t counter;

    counter.elements = 0u;
    counter.depth_max = 0u;
    if (!robo_html_node_handle_is_wellformed(&root)) {
        return 0u;
    }
    if (robo_html_node_walk_pre_order(&root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                      layout_count_element, &counter, NULL) != ROBO_OK) {
        return 0u;
    }
    return counter.elements;
}

/**
 * The element with @p tag anywhere in @p doc, or an unformed handle.
 *
 * The suites that need one specific element ("the `.hidden` div") out of a
 * fixture need this; a suite that asserts *structure* should walk the built tree
 * instead, because the tree is what is under test.
 */
static inline robo_html_node_handle_t layout_document_find(const layout_document_t *doc,
                                                           const char *tag)
{
    robo_html_node_handle_t root = robo_html_document_root(doc->id);
    robo_html_node_handle_t found;

    robo_html_node_handle_clear(&found);
    if (!robo_html_node_handle_is_wellformed(&root)) {
        return found;
    }
    if (robo_html_node_find_element(&root, tag, &found, NULL) != ROBO_OK) {
        robo_html_node_handle_clear(&found);
    }
    return found;
}

/* ------------------------------------------------------------------ */
/* A fake style source                                                */
/* ------------------------------------------------------------------ */

/** One scripted answer: an element with this tag name computes to this display. */
#define LAYOUT_FAKE_MAX_RULES 8u

typedef struct layout_fake_style {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
} layout_fake_style_t;

typedef struct layout_fake_source {
    layout_fake_style_t rules[LAYOUT_FAKE_MAX_RULES];
    size_t count;
    size_t answers;
} layout_fake_source_t;

static inline const robo_css_computed_style_t *layout_fake_lookup(void *userdata,
                                                                  const robo_html_node_handle_t *element)
{
    layout_fake_source_t *source = (layout_fake_source_t *)userdata;
    const char *name;
    size_t i;

    if (!source || !element) {
        return NULL;
    }
    name = robo_html_element_name(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (i = 0u; i < source->count; i++) {
        if (strcmp(source->rules[i].tag, name) == 0) {
            source->answers++;
            return source->rules[i].style;
        }
    }
    return NULL;
}

static inline void layout_fake_init(layout_fake_source_t *source)
{
    memset(source, 0, sizeof(*source));
}

static inline void layout_fake_dispose(layout_fake_source_t *source)
{
    size_t i;

    for (i = 0u; i < source->count; i++) {
        robo_css_computed_style_destroy(source->rules[i].style);
        source->rules[i].style = NULL;
    }
    source->count = 0u;
    source->answers = 0u;
}

/**
 * Answer for every @p tag with @p display.
 *
 * A *real* computed style holding a real DISPLAY value, built through 9.8's own
 * maker and container rather than through a hand-written struct: a fake that
 * fabricated the value would prove the builder reads a struct, not that it reads
 * a CSS computed value.
 */
static inline void layout_fake_add(layout_fake_source_t *source,
                                   const char *tag,
                                   robo_css_display_t display)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;

    layout_fixture();
    ROBO_CHECK_EQ_INT(source->count < LAYOUT_FAKE_MAX_RULES, 1);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_display(&value, display, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("display"),
                                                       0u, &specified, &value, NULL),
                      ROBO_OK);
    strncpy(source->rules[source->count].tag, tag,
            sizeof(source->rules[source->count].tag) - 1u);
    source->rules[source->count].tag[sizeof(source->rules[source->count].tag) - 1u] = '\0';
    source->rules[source->count].style = style;
    source->count++;
}

/** A source struct pointing at @p source. The struct is a copy by value. */
static inline robo_layout_style_source_t layout_fake_source_of(layout_fake_source_t *source)
{
    robo_layout_style_source_t styles;

    robo_layout_style_source_init(&styles);
    styles.userdata = source;
    styles.computed_style_for = layout_fake_lookup;
    return styles;
}

/* ------------------------------------------------------------------ */
/* A real CSS context                                                 */
/* ------------------------------------------------------------------ */

/** Everything the DOM bridge needs from RoboCSS, and nothing it owns. */
typedef struct layout_css_fixture {
    robo_css_style_context_t *context;
    robo_css_property_table_t *table;
    robo_css_property_registry_t *registry;
    robo_css_stylesheet_id_t sheet;
} layout_css_fixture_t;

static inline void layout_css_begin(layout_css_fixture_t *fixture)
{
    fixture->context = NULL;
    fixture->table = NULL;
    fixture->registry = NULL;
    fixture->sheet = ROBO_CSS_STYLESHEET_ID_NONE;

    layout_fixture();
    ROBO_CHECK_EQ_INT(robo_css_style_context_create(&fixture->context, NULL), ROBO_OK);
    /* The element host is installed by the bridge, which is the point of the
     * bridge: a caller that had to answer four callbacks to use this module would
     * be writing the adapter itself. */
    ROBO_CHECK_EQ_INT(robo_css_property_table_create(&fixture->table, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&fixture->registry, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_apply_to_table(fixture->registry,
                                                               fixture->table, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_property_table(fixture->context,
                                                               fixture->table, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_property_registry(fixture->context,
                                                                  fixture->registry, NULL),
                      ROBO_OK);
}

/** Parse @p css as an author sheet, parse its selectors, add it to the context. */
static inline void layout_css_sheet(layout_css_fixture_t *fixture, const char *css)
{
    robo_css_stylesheet_id_t id = css_parse_sheet(css, NULL);
    size_t index;

    for (index = 0u; index < robo_css_stylesheet_rule_count(id); index++) {
        robo_css_style_rule_t *rule = (robo_css_style_rule_t *)robo_css_rule_style(
            robo_css_stylesheet_rule(id, index));

        if (rule) {
            robo_css_error_t error = ROBO_CSS_CLEARED;

            ROBO_CHECK_EQ_INT(robo_css_style_rule_parse_selectors(
                                  rule, robo_css_stylesheet_source(id), &error, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 0);
        }
    }
    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(fixture->context,
                                                      ROBO_CSS_ORIGIN_AUTHOR, id, NULL),
                      ROBO_OK);
    fixture->sheet = id;
}

static inline void layout_css_end(layout_css_fixture_t *fixture)
{
    robo_css_style_context_destroy(fixture->context);
    fixture->context = NULL;
    robo_css_property_registry_destroy(fixture->registry);
    fixture->registry = NULL;
    robo_css_property_table_destroy(fixture->table);
    fixture->table = NULL;
    /* The sheet belongs to the module, which is what a stylesheet id means. */
    if (fixture->sheet != ROBO_CSS_STYLESHEET_ID_NONE) {
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(fixture->sheet, NULL), ROBO_OK);
        fixture->sheet = ROBO_CSS_STYLESHEET_ID_NONE;
    }
}

#endif /* ROBO_LAYOUT_TEST_SUPPORT_H */
