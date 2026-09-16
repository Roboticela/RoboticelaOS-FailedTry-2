/*
 * RoboHTML Phase 8.8 traversal suite:
 * relationships in both directions, the child cursor, deterministic
 * pre-order/document order, the node filters, bounded search, parser
 * integration, Unicode and deep trees, defensive input, ownership and
 * lifetime, and the debug dump that now runs on the walker.
 *
 * The exact-order tests build the tree through the element and text models
 * rather than through the parser, because the parser's source position and
 * whitespace handling are the 8.3 suite's subject: here the tree is the
 * fixture, so every expected order is stated literally.
 *
 * The conceptual tree the order tests share (task spec section 33):
 *
 *   Document
 *     └── html
 *         └── body
 *             ├── Text("Hello ")
 *             ├── div
 *             │   ├── Text("Roboticela ")
 *             │   └── span
 *             │       └── Text("Runtime")
 *             └── Text("!")
 *
 * Note on aliasing: no accessor here is ever called with the same handle as
 * both input and output. A single-link accessor clears its output before it
 * resolves its input, so aliasing would silently turn every call into a miss
 * and make a wrong expectation look green.
 */

#include "RoboBase/RoboBase.h"
#include "RoboDemo/RoboDemoHtml.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void tv_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void tv_fixture_end(void)
{
    robo_html_shutdown();
}

static robo_html_document_id_t tv_doc(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

/** Build @p text into @p id with the one-shot API. */
static void tv_build(robo_html_document_id_t id, const char *text)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, NULL, NULL), ROBO_OK);
}

static robo_html_node_handle_t tv_root(robo_html_document_id_t id)
{
    robo_html_node_handle_t root = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    return root;
}

/** Append a child element named @p name under @p parent. */
static robo_html_node_handle_t tv_element(robo_html_document_id_t id,
                                          const robo_html_node_handle_t *parent,
                                          const char *name)
{
    robo_html_element_create_info_t info;
    robo_html_node_handle_t out;

    robo_html_element_create_info_init(&info);
    info.parent = *parent;
    info.origin = ROBO_HTML_NODE_ORIGIN_TEST;
    info.namespace_id = ROBO_HTML_NAMESPACE_HTML;
    info.local_name = name;

    robo_html_node_handle_clear(&out);
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&out));
    return out;
}

/** Append a text node carrying @p content under @p parent. */
static robo_html_node_handle_t tv_text(robo_html_document_id_t id,
                                       const robo_html_node_handle_t *parent,
                                       const char *content)
{
    robo_html_text_create_info_t info;
    robo_html_node_handle_t out;

    robo_html_text_create_info_init(&info);
    info.parent = *parent;
    info.origin = ROBO_HTML_NODE_ORIGIN_TEST;
    info.content = content;
    info.length = content ? strlen(content) : 0u;

    robo_html_node_handle_clear(&out);
    ROBO_CHECK_EQ_INT(robo_html_text_create(id, &info, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&out));
    return out;
}

/** The shared section-33 tree. */
typedef struct tv_tree {
    robo_html_document_id_t id;
    robo_html_node_handle_t document;
    robo_html_node_handle_t html;
    robo_html_node_handle_t body;
    robo_html_node_handle_t lead_text; /* "Hello " */
    robo_html_node_handle_t div;
    robo_html_node_handle_t inner_text; /* "Roboticela " */
    robo_html_node_handle_t span;
    robo_html_node_handle_t deep_text; /* "Runtime" */
    robo_html_node_handle_t tail_text; /* "!" */
} tv_tree_t;

static void tv_tree_build(tv_tree_t *tree)
{
    memset(tree, 0, sizeof(*tree));
    tree->id = tv_doc();
    tree->document = tv_root(tree->id);

    tree->html = tv_element(tree->id, &tree->document, "html");
    tree->body = tv_element(tree->id, &tree->html, "body");

    tree->lead_text = tv_text(tree->id, &tree->body, "Hello ");
    tree->div = tv_element(tree->id, &tree->body, "div");
    tree->inner_text = tv_text(tree->id, &tree->div, "Roboticela ");
    tree->span = tv_element(tree->id, &tree->div, "span");
    tree->deep_text = tv_text(tree->id, &tree->span, "Runtime");
    tree->tail_text = tv_text(tree->id, &tree->body, "!");
}

/** Handle of the first descendant element named @p name (invalid when none). */
static robo_html_node_handle_t tv_find(robo_html_document_id_t id,
                                       const char *name)
{
    robo_html_node_handle_t root = tv_root(id);
    robo_html_node_handle_t found;

    robo_html_node_handle_clear(&found);
    if (robo_html_node_find_element(&root, name, &found, NULL) != ROBO_OK) {
        robo_html_node_handle_clear(&found);
    }
    return found;
}

/** Bounded copy that never leaves @p out unterminated. */
static void tv_copy(char *out, size_t cap, const char *src, size_t length)
{
    size_t take = length;

    if (!out || cap == 0u) {
        return;
    }
    if (take > cap - 1u) {
        take = cap - 1u;
    }
    if (src && take > 0u) {
        memcpy(out, src, take);
    }
    out[take] = '\0';
}

/* ------------------------------------------------------------------ */
/* Walk recorder                                                       */
/* ------------------------------------------------------------------ */

#define TV_MAX_VISITS 64

typedef struct tv_log {
    size_t count;
    size_t recorded;
    robo_html_node_kind_t kinds[TV_MAX_VISITS];
    size_t depths[TV_MAX_VISITS];
    char names[TV_MAX_VISITS][32];
    char texts[TV_MAX_VISITS][80];
    /* stop the walk once count reaches this many visits (0 = never) */
    size_t stop_after;
    robo_result_t stop_code;
    /* if set, the first visit destroys the document instead of recording */
    int destroy_document;
} tv_log_t;

static void tv_log_init(tv_log_t *log)
{
    memset(log, 0, sizeof(*log));
}

static robo_result_t tv_visit(const robo_html_node_view_t *view,
                              size_t depth,
                              void *user)
{
    tv_log_t *log = (tv_log_t *)user;

    if (log->destroy_document) {
        log->destroy_document = 0;
        (void)robo_html_document_destroy(view->handle.document_id, NULL);
        return ROBO_OK;
    }
    if (log->count < TV_MAX_VISITS) {
        size_t slot = log->count;

        log->kinds[slot] = view->kind;
        log->depths[slot] = depth;
        tv_copy(log->names[slot], sizeof(log->names[slot]), view->name,
                view->name ? strlen(view->name) : 0u);
        tv_copy(log->texts[slot], sizeof(log->texts[slot]), view->text,
                view->text_length);
        log->recorded = slot + 1u;
    }
    log->count++;
    if (log->stop_after != 0u && log->count >= log->stop_after) {
        return log->stop_code;
    }
    return ROBO_OK;
}

/** Index of the first recorded visit whose text equals @p text, or -1. */
static long tv_index_of_text(const tv_log_t *log, const char *text)
{
    size_t i;

    for (i = 0u; i < log->recorded; i++) {
        if (log->kinds[i] == ROBO_HTML_NODE_KIND_TEXT &&
            strcmp(log->texts[i], text) == 0) {
            return (long)i;
        }
    }
    return -1;
}

/** Index of the first recorded visit whose element name equals @p name. */
static long tv_index_of_name(const tv_log_t *log, const char *name)
{
    size_t i;

    for (i = 0u; i < log->recorded; i++) {
        if (log->kinds[i] == ROBO_HTML_NODE_KIND_ELEMENT &&
            strcmp(log->names[i], name) == 0) {
            return (long)i;
        }
    }
    return -1;
}

/** Counting visitor with no upper bound (deep-tree test). */
typedef struct tv_counter {
    size_t count;
    size_t max_depth;
} tv_counter_t;

static robo_result_t tv_count_visit(const robo_html_node_view_t *view,
                                    size_t depth,
                                    void *user)
{
    tv_counter_t *counter = (tv_counter_t *)user;

    (void)view;
    counter->count++;
    if (depth > counter->max_depth) {
        counter->max_depth = depth;
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Relationships                                                       */
/* ------------------------------------------------------------------ */

static void test_tv_child_relationships(void)
{
    tv_tree_t tree;
    robo_html_node_handle_t out;
    robo_html_node_handle_t next;
    size_t count = 0u;

    tv_fixture_begin();
    tv_tree_build(&tree);

    /* body's children are exactly the three nodes the fixture created. */
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&tree.body, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.lead_text));
    ROBO_CHECK_EQ_INT(robo_html_node_child_count(&tree.body, &count, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)count, 3);
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&tree.body, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.tail_text));

    /* Text and element children are siblings on equal terms. */
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&tree.lead_text, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.div));
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&tree.div, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.tail_text));
    ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&tree.tail_text, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.div));

    /* The ends of a child list are misses, and the output is cleared. */
    ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&tree.lead_text, &out, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&out));
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&tree.tail_text, &out, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&out));

    /* Leaf nodes answer "no children" with a miss too. */
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&tree.deep_text, &out, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&tree.deep_text, &out, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_node_child_count(&tree.deep_text, &count, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)count, 0);

    /* Walking out and back in is symmetric. */
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&tree.div, &next, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&next, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.div));

    (void)robo_html_document_destroy(tree.id, NULL);
    tv_fixture_end();
}

static void test_tv_parent_and_root(void)
{
    tv_tree_t tree;
    robo_html_node_handle_t out;
    size_t depth = 0u;

    tv_fixture_begin();
    tv_tree_build(&tree);

    /* Text -> element, one step at a time. */
    ROBO_CHECK_EQ_INT(robo_html_node_parent(&tree.deep_text, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.span));
    ROBO_CHECK_EQ_INT(robo_html_node_parent(&tree.span, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.div));
    ROBO_CHECK_EQ_INT(robo_html_node_parent(&tree.div, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.body));
    ROBO_CHECK_EQ_INT(robo_html_node_parent(&tree.body, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.html));
    ROBO_CHECK_EQ_INT(robo_html_node_parent(&tree.html, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.document));

    /* The document node is the top: no parent is an answer, not an error. */
    ROBO_CHECK_EQ_INT(robo_html_node_parent(&tree.document, &out, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&out));

    /* Root traversal walks the same chain to the document node. */
    ROBO_CHECK_EQ_INT(robo_html_node_root(&tree.deep_text, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.document));
    ROBO_CHECK_EQ_INT(robo_html_node_root(&tree.document, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.document));
    /* A text node and its element ancestor share a root. */
    ROBO_CHECK_EQ_INT(robo_html_node_root(&tree.span, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.document));

    /* Depth is measured from the document node. */
    ROBO_CHECK_EQ_INT(robo_html_node_depth(&tree.document, &depth, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)depth, 0);
    ROBO_CHECK_EQ_INT(robo_html_node_depth(&tree.html, &depth, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)depth, 1);
    ROBO_CHECK_EQ_INT(robo_html_node_depth(&tree.body, &depth, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)depth, 2);
    ROBO_CHECK_EQ_INT(robo_html_node_depth(&tree.div, &depth, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)depth, 3);
    ROBO_CHECK_EQ_INT(robo_html_node_depth(&tree.span, &depth, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)depth, 4);
    ROBO_CHECK_EQ_INT(robo_html_node_depth(&tree.deep_text, &depth, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)depth, 5);

    (void)robo_html_document_destroy(tree.id, NULL);
    tv_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Document order                                                      */
/* ------------------------------------------------------------------ */

static void test_tv_document_order(void)
{
    tv_tree_t tree;
    tv_log_t log;
    tv_log_t again;

    tv_fixture_begin();
    tv_tree_build(&tree);
    tv_log_init(&log);

    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &tree.html, ROBO_HTML_NODE_FILTER_ALL, tv_visit,
                          &log, NULL),
                      ROBO_OK);

    /* Exactly the eight spec-section-33 nodes, in this order. */
    ROBO_CHECK_EQ_INT((int)log.count, 8);
    ROBO_CHECK_EQ_INT((int)log.recorded, 8);

    ROBO_CHECK_EQ_INT((int)log.kinds[0], ROBO_HTML_NODE_KIND_ELEMENT);
    ROBO_CHECK_STR_EQ(log.names[0], "html");
    ROBO_CHECK_EQ_INT((int)log.depths[0], 1);

    ROBO_CHECK_EQ_INT((int)log.kinds[1], ROBO_HTML_NODE_KIND_ELEMENT);
    ROBO_CHECK_STR_EQ(log.names[1], "body");
    ROBO_CHECK_EQ_INT((int)log.depths[1], 2);

    ROBO_CHECK_EQ_INT((int)log.kinds[2], ROBO_HTML_NODE_KIND_TEXT);
    ROBO_CHECK_STR_EQ(log.texts[2], "Hello ");
    ROBO_CHECK_EQ_INT((int)log.depths[2], 3);

    ROBO_CHECK_EQ_INT((int)log.kinds[3], ROBO_HTML_NODE_KIND_ELEMENT);
    ROBO_CHECK_STR_EQ(log.names[3], "div");
    ROBO_CHECK_EQ_INT((int)log.depths[3], 3);

    ROBO_CHECK_EQ_INT((int)log.kinds[4], ROBO_HTML_NODE_KIND_TEXT);
    ROBO_CHECK_STR_EQ(log.texts[4], "Roboticela ");
    ROBO_CHECK_EQ_INT((int)log.depths[4], 4);

    ROBO_CHECK_EQ_INT((int)log.kinds[5], ROBO_HTML_NODE_KIND_ELEMENT);
    ROBO_CHECK_STR_EQ(log.names[5], "span");
    ROBO_CHECK_EQ_INT((int)log.depths[5], 4);

    ROBO_CHECK_EQ_INT((int)log.kinds[6], ROBO_HTML_NODE_KIND_TEXT);
    ROBO_CHECK_STR_EQ(log.texts[6], "Runtime");
    ROBO_CHECK_EQ_INT((int)log.depths[6], 5);

    ROBO_CHECK_EQ_INT((int)log.kinds[7], ROBO_HTML_NODE_KIND_TEXT);
    ROBO_CHECK_STR_EQ(log.texts[7], "!");
    ROBO_CHECK_EQ_INT((int)log.depths[7], 3);

    /* Order is a pure function of the tree: a second walk is identical. */
    tv_log_init(&again);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &tree.html, ROBO_HTML_NODE_FILTER_ALL, tv_visit,
                          &again, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)again.count, (int)log.count);
    ROBO_CHECK_STR_EQ(again.texts[7], log.texts[7]);
    ROBO_CHECK_STR_EQ(again.names[5], log.names[5]);
    ROBO_CHECK_EQ_INT((int)again.depths[6], (int)log.depths[6]);

    (void)robo_html_document_destroy(tree.id, NULL);
    tv_fixture_end();
}

static void test_tv_pre_order_from_document(void)
{
    tv_tree_t tree;
    tv_log_t log;

    tv_fixture_begin();
    tv_tree_build(&tree);
    tv_log_init(&log);

    /* Starting at the document node includes it and everything below. */
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &tree.document, ROBO_HTML_NODE_FILTER_ALL, tv_visit,
                          &log, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)log.count, 9);
    ROBO_CHECK_EQ_INT((int)log.kinds[0], ROBO_HTML_NODE_KIND_DOCUMENT);
    ROBO_CHECK_EQ_INT((int)log.depths[0], 0);
    ROBO_CHECK_STR_EQ(log.names[1], "html");

    /* Descendants skips the start: one fewer visit, same order after it. */
    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                          &tree.document, ROBO_HTML_NODE_FILTER_ALL, tv_visit,
                          &log, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)log.count, 8);
    ROBO_CHECK_STR_EQ(log.names[0], "html");

    /* A subtree walk is bounded by its root, and still reports absolute
     * depth — a visitor can indent without walking the parent chain. */
    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &tree.span, ROBO_HTML_NODE_FILTER_ALL, tv_visit,
                          &log, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)log.count, 2);
    ROBO_CHECK_STR_EQ(log.names[0], "span");
    ROBO_CHECK_EQ_INT((int)log.depths[0], 4);
    ROBO_CHECK_STR_EQ(log.texts[1], "Runtime");
    ROBO_CHECK_EQ_INT((int)log.depths[1], 5);

    /* A lone node is a one-visit walk. */
    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &tree.deep_text, ROBO_HTML_NODE_FILTER_ALL, tv_visit,
                          &log, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)log.count, 1);
    ROBO_CHECK_EQ_INT((int)log.kinds[0], ROBO_HTML_NODE_KIND_TEXT);

    (void)robo_html_document_destroy(tree.id, NULL);
    tv_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Filters                                                             */
/* ------------------------------------------------------------------ */

static void test_tv_element_filter(void)
{
    tv_tree_t tree;
    tv_log_t log;

    tv_fixture_begin();
    tv_tree_build(&tree);
    tv_log_init(&log);

    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &tree.html, ROBO_HTML_NODE_FILTER_ELEMENTS, tv_visit,
                          &log, NULL),
                      ROBO_OK);

    /* Text nodes are excluded from the visit but still stepped through, so
     * the element order is exactly the document order of the elements. */
    ROBO_CHECK_EQ_INT((int)log.count, 4);
    ROBO_CHECK_STR_EQ(log.names[0], "html");
    ROBO_CHECK_STR_EQ(log.names[1], "body");
    ROBO_CHECK_STR_EQ(log.names[2], "div");
    ROBO_CHECK_STR_EQ(log.names[3], "span");
    ROBO_CHECK_EQ_INT((int)log.depths[3], 4);

    /* A text node between two elements cannot hide the second element: the
     * filter projects the same walk rather than skipping subtrees. */
    ROBO_CHECK_EQ_INT((int)log.kinds[2], ROBO_HTML_NODE_KIND_ELEMENT);
    ROBO_CHECK_EQ_INT((int)log.depths[2], 3);

    (void)robo_html_document_destroy(tree.id, NULL);
    tv_fixture_end();
}

static void test_tv_text_filter(void)
{
    tv_tree_t tree;
    tv_log_t log;

    tv_fixture_begin();
    tv_tree_build(&tree);
    tv_log_init(&log);

    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &tree.html, ROBO_HTML_NODE_FILTER_TEXT, tv_visit,
                          &log, NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT((int)log.count, 4);
    ROBO_CHECK_STR_EQ(log.texts[0], "Hello ");
    ROBO_CHECK_STR_EQ(log.texts[1], "Roboticela ");
    ROBO_CHECK_STR_EQ(log.texts[2], "Runtime");
    ROBO_CHECK_STR_EQ(log.texts[3], "!");
    ROBO_CHECK_EQ_INT((int)log.depths[0], 3);
    ROBO_CHECK_EQ_INT((int)log.depths[2], 5);

    /* Elements-only from the same start, to prove the filters differ. */
    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                          &tree.body, ROBO_HTML_NODE_FILTER_ELEMENTS, tv_visit,
                          &log, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)log.count, 2);
    ROBO_CHECK_STR_EQ(log.names[0], "div");
    ROBO_CHECK_STR_EQ(log.names[1], "span");

    (void)robo_html_document_destroy(tree.id, NULL);
    tv_fixture_end();
}

static void test_tv_filter_api(void)
{
    tv_tree_t tree;

    tv_fixture_begin();
    tv_tree_build(&tree);

    ROBO_CHECK_EQ_INT(robo_html_node_filter_is_known(ROBO_HTML_NODE_FILTER_ALL),
                      1);
    ROBO_CHECK_EQ_INT(
        robo_html_node_filter_is_known(ROBO_HTML_NODE_FILTER_ELEMENTS), 1);
    ROBO_CHECK_EQ_INT(robo_html_node_filter_is_known(ROBO_HTML_NODE_FILTER_TEXT),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_node_filter_is_known(
                          (robo_html_node_filter_t)99),
                      0);
    ROBO_CHECK_STR_EQ(robo_html_node_filter_name(ROBO_HTML_NODE_FILTER_ALL),
                      "all");
    ROBO_CHECK_STR_EQ(
        robo_html_node_filter_name(ROBO_HTML_NODE_FILTER_ELEMENTS), "elements");
    ROBO_CHECK_STR_EQ(robo_html_node_filter_name(ROBO_HTML_NODE_FILTER_TEXT),
                      "text");
    ROBO_CHECK_STR_EQ(robo_html_node_filter_name((robo_html_node_filter_t)99),
                      "unknown");

    /* The exposed predicate agrees with the walker's rule. */
    ROBO_CHECK_EQ_INT(
        robo_html_node_matches_filter(&tree.span, ROBO_HTML_NODE_FILTER_ELEMENTS),
        1);
    ROBO_CHECK_EQ_INT(
        robo_html_node_matches_filter(&tree.span, ROBO_HTML_NODE_FILTER_TEXT),
        0);
    ROBO_CHECK_EQ_INT(robo_html_node_matches_filter(
                          &tree.deep_text, ROBO_HTML_NODE_FILTER_TEXT),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_node_matches_filter(
                          &tree.deep_text, ROBO_HTML_NODE_FILTER_ELEMENTS),
                      0);
    ROBO_CHECK_EQ_INT(robo_html_node_matches_filter(&tree.deep_text,
                                                    ROBO_HTML_NODE_FILTER_ALL),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_node_matches_filter(
                          &tree.deep_text, (robo_html_node_filter_t)99),
                      0);
    ROBO_CHECK_EQ_INT(
        robo_html_node_matches_filter(NULL, ROBO_HTML_NODE_FILTER_ALL), 0);

    (void)robo_html_document_destroy(tree.id, NULL);
    tv_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Child cursor                                                        */
/* ------------------------------------------------------------------ */

static void test_tv_child_cursor(void)
{
    tv_tree_t tree;
    robo_html_child_cursor_t cursor;
    robo_html_child_cursor_t copy;
    robo_html_node_handle_t child;
    robo_html_node_handle_t by_index;
    size_t seen = 0u;

    tv_fixture_begin();
    tv_tree_build(&tree);

    ROBO_CHECK_EQ_INT(robo_html_child_cursor_open(&cursor, &tree.body, NULL),
                      ROBO_OK);
    while (robo_html_child_cursor_next(&cursor, &child, NULL) == ROBO_OK) {
        /* The cursor's order is child_at's order, one lookup per child. */
        ROBO_CHECK_EQ_INT(robo_html_node_child_at(&tree.body, seen, &by_index,
                                                  NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&child, &by_index));
        seen++;
    }
    ROBO_CHECK_EQ_INT((int)seen, 3);

    /* An exhausted cursor keeps answering NOT_FOUND. */
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &child, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&child));

    /* The three children, in order. */
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_open(&cursor, &tree.body, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &child, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&child, &tree.lead_text));
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &child, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&child, &tree.div));
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &child, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&child, &tree.tail_text));

    /* A childless node opens successfully and is immediately exhausted: an
     * empty iteration is a normal result, not an error. */
    ROBO_CHECK_EQ_INT(
        robo_html_child_cursor_open(&cursor, &tree.deep_text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &child, NULL),
                      ROBO_NOT_FOUND);

    /* A copy continues independently — the cursor owns no shared state. */
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_open(&cursor, &tree.body, NULL),
                      ROBO_OK);
    copy = cursor;
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&copy, &child, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&child, &tree.lead_text));
    /* The copy runs to the second child without dragging the original. */
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&copy, &child, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&child, &tree.div));
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &child, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&child, &tree.lead_text));
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &child, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&child, &tree.div));

    /* Init and NULL handling. */
    robo_html_child_cursor_init(&cursor);
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &child, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_open(NULL, &tree.body, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_open(&cursor, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_html_child_cursor_init(NULL); /* safe on NULL */

    /* An unresolvable parent is a lookup miss, not a silent empty cursor. */
    {
        robo_html_node_handle_t stale;
        robo_html_diag_t diag;

        robo_html_node_handle_clear(&stale);
        robo_html_diag_reset(&diag);
        ROBO_CHECK_EQ_INT(robo_html_child_cursor_open(&cursor, &stale, &diag),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_TRAVERSAL);
    }

    (void)robo_html_document_destroy(tree.id, NULL);
    tv_fixture_end();
}

static void test_tv_cursor_after_destroy(void)
{
    tv_tree_t tree;
    robo_html_child_cursor_t cursor;
    robo_html_node_handle_t child;

    tv_fixture_begin();
    tv_tree_build(&tree);

    ROBO_CHECK_EQ_INT(robo_html_child_cursor_open(&cursor, &tree.body, NULL),
                      ROBO_OK);
    (void)robo_html_document_destroy(tree.id, NULL);

    /* The cursor is inert, not dangerous: it reports the loss. */
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &child, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&child));
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &child, NULL),
                      ROBO_NOT_FOUND);

    tv_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Search                                                              */
/* ------------------------------------------------------------------ */

typedef struct name_query {
    const char *name;
} name_query_t;

static int name_matches(const robo_html_node_view_t *view, void *user)
{
    const name_query_t *query = (const name_query_t *)user;

    return strcmp(view->name, query->name) == 0;
}

static void test_tv_find(void)
{
    tv_tree_t tree;
    robo_html_node_handle_t out;
    name_query_t query;

    tv_fixture_begin();
    tv_tree_build(&tree);

    /* First element at or below, and first element inside: these differ
     * exactly when the start node itself matches. */
    query.name = "div";
    ROBO_CHECK_EQ_INT(robo_html_node_find_first(
                          &tree.div, ROBO_HTML_NODE_FILTER_ELEMENTS,
                          name_matches, &query, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.div));
    /* There is no nested div, so the inside-only search finds nothing. */
    ROBO_CHECK_EQ_INT(robo_html_node_find_descendant(
                          &tree.div, ROBO_HTML_NODE_FILTER_ELEMENTS,
                          name_matches, &query, &out, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&out));
    query.name = "span";
    ROBO_CHECK_EQ_INT(robo_html_node_find_descendant(
                          &tree.div, ROBO_HTML_NODE_FILTER_ELEMENTS,
                          name_matches, &query, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.span));

    /* A NULL predicate is the plain "first of this kind". */
    ROBO_CHECK_EQ_INT(robo_html_node_find_first(
                          &tree.div, ROBO_HTML_NODE_FILTER_TEXT, NULL, NULL,
                          &out, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.inner_text));
    ROBO_CHECK_EQ_INT(robo_html_node_find_first(
                          &tree.body, ROBO_HTML_NODE_FILTER_ELEMENTS, NULL,
                          NULL, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.body));
    ROBO_CHECK_EQ_INT(robo_html_node_find_descendant(
                          &tree.body, ROBO_HTML_NODE_FILTER_ELEMENTS, NULL,
                          NULL, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.div));

    query.name = "nothing-here";
    ROBO_CHECK_EQ_INT(robo_html_node_find_first(
                          &tree.div, ROBO_HTML_NODE_FILTER_ELEMENTS,
                          name_matches, &query, &out, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&out));

    /* An invalid start is an error, not an empty result. */
    {
        robo_html_node_handle_t stale;

        robo_html_node_handle_clear(&stale);
        ROBO_CHECK_EQ_INT(robo_html_node_find_first(
                              &stale, ROBO_HTML_NODE_FILTER_ALL, NULL, NULL,
                              &out, NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_html_node_find_descendant(
                              &stale, ROBO_HTML_NODE_FILTER_ALL, NULL, NULL,
                              &out, NULL),
                          ROBO_NOT_FOUND);
    }

    /* find_element keeps its documented "inside" contract. */
    ROBO_CHECK_EQ_INT(robo_html_node_find_element(&tree.div, "div", &out, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_node_find_element(&tree.div, "span", &out, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &tree.span));

    (void)robo_html_document_destroy(tree.id, NULL);
    tv_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Parser integration                                                  */
/* ------------------------------------------------------------------ */

static void test_tv_parser_document_order(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t div;
    robo_html_node_handle_t root;
    tv_log_t log;

    tv_fixture_begin();

    id = tv_doc();
    tv_build(id, "<div>Hello <span>Roboticela</span> Runtime</div>");
    div = tv_find(id, "div");
    ROBO_CHECK(robo_html_element_is_element(&div));

    /* div → Text("Hello ") → span → Text("Roboticela") → Text(" Runtime") */
    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                          &div, ROBO_HTML_NODE_FILTER_ALL, tv_visit, &log,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)log.count, 4);
    ROBO_CHECK_EQ_INT((int)log.kinds[0], ROBO_HTML_NODE_KIND_TEXT);
    ROBO_CHECK_STR_EQ(log.texts[0], "Hello ");
    ROBO_CHECK_EQ_INT((int)log.kinds[1], ROBO_HTML_NODE_KIND_ELEMENT);
    ROBO_CHECK_STR_EQ(log.names[1], "span");
    ROBO_CHECK_STR_EQ(log.texts[2], "Roboticela");
    ROBO_CHECK_STR_EQ(log.texts[3], " Runtime");

    /* Text nodes reached by traversal carry the same bytes subtree_text
     * concatenates, in the same order. */
    {
        char text[64];
        size_t length = 0u;

        ROBO_CHECK_EQ_INT(robo_html_node_subtree_text(&div, text,
                                                      sizeof(text), &length,
                                                      NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(text, "Hello Roboticela Runtime");
        ROBO_CHECK_EQ_INT((int)length, 24);
    }

    (void)robo_html_document_destroy(id, NULL);

    /* The document-order walk from the document node is the same sequence. */
    id = tv_doc();
    tv_build(id, "<html><body><p>Hello <b>Roboticela</b>!</p></body></html>");
    root = tv_root(id);
    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &root, ROBO_HTML_NODE_FILTER_TEXT, tv_visit, &log,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)log.count, 3);
    ROBO_CHECK_STR_EQ(log.texts[0], "Hello ");
    ROBO_CHECK_STR_EQ(log.texts[1], "Roboticela");
    ROBO_CHECK_STR_EQ(log.texts[2], "!");
    /* Text after an element is the element's next sibling, so document order
     * cannot be reconstructed from the elements alone. */
    {
        long hello = tv_index_of_text(&log, "Hello ");
        long robot = tv_index_of_text(&log, "Roboticela");

        ROBO_CHECK(hello >= 0);
        ROBO_CHECK(robot > hello);
    }
    /* The b element sits between the first two text nodes. */
    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                          &root, ROBO_HTML_NODE_FILTER_ELEMENTS, tv_visit,
                          &log, NULL),
                      ROBO_OK);
    ROBO_CHECK(tv_index_of_name(&log, "p") < tv_index_of_name(&log, "b"));

    (void)robo_html_document_destroy(id, NULL);
    tv_fixture_end();
}

static void test_tv_parser_attributes_reachable(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t div;
    robo_html_node_handle_t out;
    robo_html_attribute_t attr;
    tv_log_t log;
    size_t count = 0u;
    size_t attr_count = 0u;

    tv_fixture_begin();

    id = tv_doc();
    tv_build(id, "<div id=\"main\" class=\"container\">Hello</div>");
    div = tv_find(id, "div");
    ROBO_CHECK(robo_html_element_is_element(&div));

    /* Traversal hands back a handle on which the attribute model still
     * works: nothing is duplicated into traversal-specific storage. */
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&div, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_text_is_text(&out));
    ROBO_CHECK_STR_EQ(robo_html_text_content(&out, NULL), "Hello");
    ROBO_CHECK(robo_html_element_has_attribute(&div, "id"));
    ROBO_CHECK(robo_html_element_has_attribute(&div, "class"));
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_count(&div, &attr_count,
                                                        NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)attr_count, 2);

    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&div, 0u, &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.name, "id");
    ROBO_CHECK_STR_EQ(attr.value, "main");
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&div, 1u, &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.name, "class");
    ROBO_CHECK_STR_EQ(attr.value, "container");

    /* An element found through traversal has the same children a walk sees. */
    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                          &div, ROBO_HTML_NODE_FILTER_ALL, tv_visit, &log,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)log.count, 1);
    ROBO_CHECK_EQ_INT(robo_html_node_child_count(&div, &count, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)count, 1);

    (void)robo_html_document_destroy(id, NULL);
    tv_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Unicode                                                             */
/* ------------------------------------------------------------------ */

static void test_tv_unicode(void)
{
    static const char *const runs[] = {
        "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7", /* Arabic */
        "\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c", /* Persian */
        "\xd8\xa7\xd8\xb1\xd8\xaf\xd9\x88",         /* Urdu */
        "\xe0\xa4\xb9\xe0\xa4\xbf\xe0\xa4\xa8\xe0\xa5\x8d\xe0\xa4\xa6\xe0\xa5\x80",
        "\xe0\xa6\xac\xe0\xa6\xbe\xe0\xa6\x82\xe0\xa6\xb2\xe0\xa6\xbe",
        "\xe4\xb8\xad\xe6\x96\x87",       /* Chinese */
        "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", /* Japanese */
        "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4", /* Korean */
        "\xf0\x9f\x98\x80\xf0\x9f\x9a\x80",       /* emoji */
        "  padded  "
    };
    const size_t run_count = sizeof(runs) / sizeof(runs[0]);
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    tv_log_t log;
    size_t i;

    tv_fixture_begin();

    /* Each run in its own element, then walked back out byte-for-byte. */
    id = tv_doc();
    root = tv_root(id);
    for (i = 0u; i < run_count; i++) {
        robo_html_node_handle_t parent = tv_element(id, &root, "span");
        robo_html_node_handle_t text = tv_text(id, &parent, runs[i]);
        robo_html_node_handle_t back;

        ROBO_CHECK_EQ_INT(robo_html_node_first_child(&parent, &back, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&back, &text));
        ROBO_CHECK_STR_EQ(robo_html_text_content(&back, NULL), runs[i]);
        ROBO_CHECK(robo_html_node_matches_filter(&back,
                                                 ROBO_HTML_NODE_FILTER_TEXT));
    }
    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                          &root, ROBO_HTML_NODE_FILTER_TEXT, tv_visit, &log,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)log.count, (int)run_count);
    ROBO_CHECK_EQ_INT((int)log.recorded, (int)run_count);
    for (i = 0u; i < run_count; i++) {
        ROBO_CHECK_STR_EQ(log.texts[i], runs[i]);
    }
    (void)robo_html_document_destroy(id, NULL);

    /* The same bytes through the parser, reached by traversal. */
    id = tv_doc();
    tv_build(id, "<div><p>Hello \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7"
                 "</p></div>");
    {
        robo_html_node_handle_t paragraph = tv_find(id, "p");
        char text[64];

        ROBO_CHECK(robo_html_element_is_element(&paragraph));
        ROBO_CHECK_EQ_INT(robo_html_node_subtree_text(&paragraph, text,
                                                      sizeof(text), NULL, NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(text,
                          "Hello \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7");
    }
    (void)robo_html_document_destroy(id, NULL);

    tv_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Deep and empty trees                                                */
/* ------------------------------------------------------------------ */

static void test_tv_deep_tree(void)
{
    enum { DEPTH = 1000 };
    robo_html_document_id_t id;
    robo_html_node_handle_t parent;
    robo_html_node_handle_t leaf;
    robo_html_node_handle_t up;
    robo_html_node_handle_t next;
    robo_html_node_handle_t root;
    tv_counter_t counter;
    size_t depth = 0u;
    size_t steps = 0u;
    int i;

    tv_fixture_begin();

    id = tv_doc();
    parent = tv_root(id);
    for (i = 0; i < DEPTH; i++) {
        parent = tv_element(id, &parent, "div");
    }
    leaf = tv_text(id, &parent, "bottom");
    root = tv_root(id);

    counter.count = 0u;
    counter.max_depth = 0u;
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &parent, ROBO_HTML_NODE_FILTER_ALL, tv_count_visit,
                          &counter, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)counter.count, 2);
    /* Depth is absolute, not relative to the walk's start. */
    ROBO_CHECK_EQ_INT((int)counter.max_depth, DEPTH + 1);

    ROBO_CHECK_EQ_INT(robo_html_node_depth(&leaf, &depth, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)depth, DEPTH + 1);

    /* Parent traversal reaches the document node and terminates. */
    up = leaf;
    while (robo_html_node_parent(&up, &next, NULL) == ROBO_OK) {
        up = next;
        steps++;
    }
    ROBO_CHECK_EQ_INT((int)steps, DEPTH + 1);
    ROBO_CHECK(robo_html_node_handle_equal(&up, &root));

    /* The whole document walks in one bounded pass — 1000 levels of nesting
     * is a stack-overflow vector for a recursive walk and a non-event here. */
    counter.count = 0u;
    counter.max_depth = 0u;
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &root, ROBO_HTML_NODE_FILTER_ALL, tv_count_visit,
                          &counter, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)counter.count, DEPTH + 2);
    ROBO_CHECK_EQ_INT((int)counter.max_depth, DEPTH + 1);

    (void)robo_html_document_destroy(id, NULL);
    tv_fixture_end();
}

static void test_tv_empty_document(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t out;
    robo_html_child_cursor_t cursor;
    tv_log_t log;
    size_t depth = 99u;

    tv_fixture_begin();

    /* A document with nothing in it traverses as a single node. */
    id = tv_doc();
    root = tv_root(id);

    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &root, ROBO_HTML_NODE_FILTER_ALL, tv_visit, &log,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)log.count, 1);
    ROBO_CHECK_EQ_INT((int)log.kinds[0], ROBO_HTML_NODE_KIND_DOCUMENT);

    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                          &root, ROBO_HTML_NODE_FILTER_ALL, tv_visit, &log,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)log.count, 0);

    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&root, &out, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&root, &out, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_open(&cursor, &root, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &out, NULL),
                      ROBO_NOT_FOUND);

    /* A childless document is still its own root at depth 0. */
    ROBO_CHECK_EQ_INT(robo_html_node_root(&root, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&out, &root));
    ROBO_CHECK_EQ_INT(robo_html_node_depth(&root, &depth, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)depth, 0);

    /* Empty input still parses to a traversable document. */
    (void)robo_html_document_destroy(id, NULL);
    id = tv_doc();
    tv_build(id, "");
    root = tv_root(id);
    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &root, ROBO_HTML_NODE_FILTER_ALL, tv_visit, &log,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK((int)log.count >= 1);
    ROBO_CHECK_EQ_INT((int)log.kinds[0], ROBO_HTML_NODE_KIND_DOCUMENT);

    (void)robo_html_document_destroy(id, NULL);
    tv_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Defensive input                                                     */
/* ------------------------------------------------------------------ */

static void test_tv_defensive(void)
{
    tv_tree_t tree;
    robo_html_node_handle_t out;
    robo_html_diag_t diag;
    robo_html_node_handle_t stale;
    size_t depth = 0u;
    tv_log_t log;

    tv_fixture_begin();
    tv_tree_build(&tree);
    tv_log_init(&log);

    /* NULL arguments are argument errors. */
    ROBO_CHECK_EQ_INT(robo_html_node_parent(NULL, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&tree.body, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(NULL, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(NULL, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(NULL, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_depth(&tree.body, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_depth(NULL, &depth, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_root(&tree.body, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_root(NULL, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          NULL, ROBO_HTML_NODE_FILTER_ALL, tv_visit, &log,
                          NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &tree.body, ROBO_HTML_NODE_FILTER_ALL, NULL, &log,
                          NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &tree.body, (robo_html_node_filter_t)99, tv_visit,
                          &log, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_find_first(
                          &tree.body, ROBO_HTML_NODE_FILTER_ALL, NULL, NULL,
                          NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)log.count, 0);

    /* A cleared handle is a lookup that missed, not misuse. */
    robo_html_node_handle_clear(&stale);
    robo_html_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_html_node_parent(&stale, &out, &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_TRAVERSAL);
    ROBO_CHECK(diag.message != NULL);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &stale, ROBO_HTML_NODE_FILTER_ALL, tv_visit, &log,
                          NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)log.count, 0);

    /* A stale handle from a destroyed document resolves to nothing. */
    (void)robo_html_document_destroy(tree.id, NULL);
    ROBO_CHECK_EQ_INT(robo_html_node_parent(&tree.body, &out, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(
        robo_html_node_walk_pre_order(&tree.document, ROBO_HTML_NODE_FILTER_ALL,
                                      tv_visit, &log, NULL),
        ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)log.count, 0);
    ROBO_CHECK_EQ_INT(robo_html_node_depth(&tree.document, &depth, NULL),
                      ROBO_NOT_FOUND);

    tv_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Visitor control                                                     */
/* ------------------------------------------------------------------ */

static void test_tv_visitor_controls_walk(void)
{
    tv_tree_t tree;
    tv_log_t log;

    tv_fixture_begin();
    tv_tree_build(&tree);

    /* A visitor that stops early turns its own code into the walk's result. */
    tv_log_init(&log);
    log.stop_after = 3u;
    log.stop_code = ROBO_BUSY;
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &tree.html, ROBO_HTML_NODE_FILTER_ALL, tv_visit,
                          &log, NULL),
                      ROBO_BUSY);
    ROBO_CHECK_EQ_INT((int)log.count, 3);

    /* A visitor that refuses aborts with its own error code. */
    tv_log_init(&log);
    log.stop_after = 1u;
    log.stop_code = ROBO_ERROR;
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &tree.html, ROBO_HTML_NODE_FILTER_ALL, tv_visit,
                          &log, NULL),
                      ROBO_ERROR);

    /* A visitor may walk reentrantly: the walker holds no document state. */
    {
        tv_log_t inner;
        robo_html_node_handle_t span = tree.span;

        tv_log_init(&inner);
        ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                              &span, ROBO_HTML_NODE_FILTER_TEXT, tv_visit,
                              &inner, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)inner.count, 1);
        ROBO_CHECK_STR_EQ(inner.texts[0], "Runtime");
    }

    /* Destroying the document from inside a visitor is reported, not
     * dereferenced. */
    tv_log_init(&log);
    log.destroy_document = 1;
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &tree.html, ROBO_HTML_NODE_FILTER_ALL, tv_visit,
                          &log, NULL),
                      ROBO_NOT_FOUND);

    tv_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Debug dump                                                          */
/* ------------------------------------------------------------------ */

static robo_result_t tv_collect_sink(const char *text, size_t length,
                                     void *user)
{
    char *out = (char *)user;
    size_t used = strlen(out);

    if (used + length < 8192u) {
        memcpy(out + used, text, length);
        out[used + length] = '\0';
    }
    return ROBO_OK;
}

static void test_tv_tree_dump_on_walker(void)
{
    robo_html_document_id_t id;
    static char buffer[8192];
    size_t length = 0u;

    tv_fixture_begin();

    /* Newlines in the source become whitespace text nodes, which is the
     * case the dump's quoting and labelling exist for. */
    id = tv_doc();
    tv_build(id, "<!DOCTYPE html>\n<html>\n<head>\n"
                 "<title>RoboticelaOS Runtime</title>\n</head>\n<body>\n"
                 "<h1>Heading</h1>\n<p>Body text</p>\n"
                 "<div id=\"main\"><span>deep</span></div>\n"
                 "</body>\n</html>\n");

    buffer[0] = '\0';
    ROBO_CHECK_EQ_INT(robo_html_tree_dump(id, tv_collect_sink, buffer, NULL),
                      ROBO_OK);

    /* The walker supplies the order and the depth, so nesting is indented
     * and every parent appears before its children. */
    ROBO_CHECK(strstr(buffer, "Document\n") != NULL);
    ROBO_CHECK(strstr(buffer, "doctype html\n") != NULL);
    ROBO_CHECK(strstr(buffer, "  html\n") != NULL);
    ROBO_CHECK(strstr(buffer, "    head\n") != NULL);
    ROBO_CHECK(strstr(buffer, "      title\n") != NULL);
    ROBO_CHECK(strstr(buffer, "        Text: \"RoboticelaOS Runtime\"\n") !=
               NULL);
    ROBO_CHECK(strstr(buffer, "    body\n") != NULL);
    ROBO_CHECK(strstr(buffer, "      h1\n") != NULL);
    ROBO_CHECK(strstr(buffer, "        Text: \"Heading\"\n") != NULL);
    ROBO_CHECK(strstr(buffer, "        Text: \"Body text\"\n") != NULL);
    ROBO_CHECK(strstr(buffer, "      div id=\"main\"\n") != NULL);
    ROBO_CHECK(strstr(buffer, "        span\n") != NULL);
    ROBO_CHECK(strstr(buffer, "          Text: \"deep\"\n") != NULL);
    /* A newline-only run is escaped, so it is legible rather than silent. */
    ROBO_CHECK(strstr(buffer, "Text: \"\\n\"") != NULL);

    {
        const char *document = strstr(buffer, "Document\n");
        const char *div = strstr(buffer, "div id=\"main\"");
        const char *deep = strstr(buffer, "Text: \"deep\"");

        ROBO_CHECK(document != NULL);
        ROBO_CHECK(div != NULL);
        ROBO_CHECK(deep != NULL);
        ROBO_CHECK(document < div);
        ROBO_CHECK(div < deep);
    }

    /* Buffer variant still reports the untruncated length. */
    {
        char small[24];

        ROBO_CHECK_EQ_INT(robo_html_tree_dump_to_buffer(id, small,
                                                        sizeof(small), &length,
                                                        NULL),
                          ROBO_OK);
        ROBO_CHECK(length > sizeof(small));
        ROBO_CHECK_EQ_INT((int)strlen(small), (int)sizeof(small) - 1);
    }

    (void)robo_html_document_destroy(id, NULL);

    /* The same dump for an explicitly built tree shows the section-33 shape,
     * proving the dump reads the tree, not the parser's bookkeeping. */
    {
        tv_tree_t tree;

        tv_tree_build(&tree);
        buffer[0] = '\0';
        ROBO_CHECK_EQ_INT(
            robo_html_tree_dump(tree.id, tv_collect_sink, buffer, NULL),
            ROBO_OK);
        /* Document 0, html 1, body 2, text/div 3, span/text 4, text 5. */
        ROBO_CHECK(strstr(buffer, "  html\n") != NULL);
        ROBO_CHECK(strstr(buffer, "    body\n") != NULL);
        ROBO_CHECK(strstr(buffer, "      Text: \"Hello \"\n") != NULL);
        ROBO_CHECK(strstr(buffer, "      div\n") != NULL);
        ROBO_CHECK(strstr(buffer, "        Text: \"Roboticela \"\n") != NULL);
        ROBO_CHECK(strstr(buffer, "        span\n") != NULL);
        ROBO_CHECK(strstr(buffer, "          Text: \"Runtime\"\n") != NULL);
        ROBO_CHECK(strstr(buffer, "      Text: \"!\"\n") != NULL);
        (void)robo_html_document_destroy(tree.id, NULL);
    }

    tv_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Capability + diagnostics                                            */
/* ------------------------------------------------------------------ */

static void test_tv_capabilities_and_diag(void)
{
    robo_html_dom_capabilities_t caps;
    robo_html_diag_t diag;
    robo_html_node_handle_t stale;
    robo_html_node_handle_t out;

    tv_fixture_begin();

    ROBO_CHECK_EQ_INT(robo_html_dom_query_capabilities(&caps), ROBO_OK);
    ROBO_CHECK_EQ_INT(caps.traversal, 1);
    ROBO_CHECK_EQ_INT(caps.tree_building, 1);
    /*
     * Phase 8.9 added the mutation API, so the tree can now be changed —
     * which is exactly why the traversal above snapshots the tree version
     * rather than assuming it reads a frozen tree. Events are still later
     * phase work.
     */
    ROBO_CHECK_EQ_INT(caps.mutation, 1);
    ROBO_CHECK_EQ_INT(caps.events, 0);

    /* The traversal domain is a defined, documented one. */
    ROBO_CHECK_EQ_INT(
        robo_html_diagnostic_domain_is_known(ROBO_HTML_DOMAIN_TRAVERSAL), 1);
    ROBO_CHECK_STR_EQ(
        robo_html_diagnostic_domain_name(ROBO_HTML_DOMAIN_TRAVERSAL),
        "traversal");

    /* A failure records it. */
    robo_html_node_handle_clear(&stale);
    robo_html_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_html_node_root(&stale, &out, &diag), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.code, (int)ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_TRAVERSAL);
    ROBO_CHECK(diag.message != NULL);

    tv_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Visual demo document                                                */
/* ------------------------------------------------------------------ */

static void test_tv_demo_document(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t body;
    robo_html_node_handle_t first;
    robo_html_node_handle_t last;
    tv_log_t log;

    tv_fixture_begin();

    id = tv_doc();
    tv_build(id, robo_demo_default_html());

    body = tv_find(id, "body");
    ROBO_CHECK(robo_html_element_is_element(&body));

    /* Every visual block the demo paints is reachable through traversal. */
    tv_log_init(&log);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                          &body, ROBO_HTML_NODE_FILTER_ELEMENTS, tv_visit,
                          &log, NULL),
                      ROBO_OK);
    ROBO_CHECK(tv_index_of_name(&log, "h1") >= 0);
    ROBO_CHECK(tv_index_of_name(&log, "p") >= 0);
    ROBO_CHECK(tv_index_of_name(&log, "button") >= 0);
    ROBO_CHECK(tv_index_of_name(&log, "h1") < tv_index_of_name(&log, "button"));

    /* The text each block paints is a text node, reachable from the block. */
    {
        robo_html_node_handle_t heading = tv_find(id, "h1");
        robo_html_node_handle_t text;

        ROBO_CHECK_EQ_INT(robo_html_node_first_child(&heading, &text, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_text_is_text(&text));
        ROBO_CHECK_STR_EQ(robo_html_text_content(&text, NULL),
                          "RoboticelaOS Runtime");
    }

    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&body, &first, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&body, &last, NULL), ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_equal(&first, &last));
    /* The demo body's first node is the leading newline; its last is the
     * closing newline. Neither end has an outside neighbour. */
    {
        robo_html_node_handle_t neighbour;

        ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&first, &neighbour, NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&last, &neighbour, NULL),
                          ROBO_NOT_FOUND);
    }

    (void)robo_html_document_destroy(id, NULL);
    tv_fixture_end();
}

/* ------------------------------------------------------------------ */

void robo_test_register_html_traversal(void)
{
    robo_test_register("html.traverse.child_relationships",
                       test_tv_child_relationships);
    robo_test_register("html.traverse.parent_and_root",
                       test_tv_parent_and_root);
    robo_test_register("html.traverse.document_order", test_tv_document_order);
    robo_test_register("html.traverse.pre_order_from_document",
                       test_tv_pre_order_from_document);
    robo_test_register("html.traverse.element_filter", test_tv_element_filter);
    robo_test_register("html.traverse.text_filter", test_tv_text_filter);
    robo_test_register("html.traverse.filter_api", test_tv_filter_api);
    robo_test_register("html.traverse.child_cursor", test_tv_child_cursor);
    robo_test_register("html.traverse.cursor_after_destroy",
                       test_tv_cursor_after_destroy);
    robo_test_register("html.traverse.find", test_tv_find);
    robo_test_register("html.traverse.parser_document_order",
                       test_tv_parser_document_order);
    robo_test_register("html.traverse.parser_attributes_reachable",
                       test_tv_parser_attributes_reachable);
    robo_test_register("html.traverse.unicode", test_tv_unicode);
    robo_test_register("html.traverse.deep_tree", test_tv_deep_tree);
    robo_test_register("html.traverse.empty_document",
                       test_tv_empty_document);
    robo_test_register("html.traverse.defensive", test_tv_defensive);
    robo_test_register("html.traverse.visitor_controls_walk",
                       test_tv_visitor_controls_walk);
    robo_test_register("html.traverse.tree_dump_on_walker",
                       test_tv_tree_dump_on_walker);
    robo_test_register("html.traverse.capabilities_and_diag",
                       test_tv_capabilities_and_diag);
    robo_test_register("html.traverse.demo_document", test_tv_demo_document);
}
