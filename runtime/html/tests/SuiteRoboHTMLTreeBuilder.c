/*
 * RoboHTML Phase 8.3 tree-builder suite:
 * document structure, implied elements, nesting, text merging, comments,
 * DOCTYPE, attributes, void/self-closing elements, open-element stack,
 * insertion-mode transitions, recovery, ownership, teardown, tree dump,
 * tokenizer integration, and the visual-demo document's expected tree.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void tb_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void tb_fixture_end(void)
{
    robo_html_shutdown();
}

static robo_html_document_id_t tb_doc(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

/** Build @p text into @p id with the one-shot API; result optional. */
static void tb_build(robo_html_document_id_t id,
                     const char *text,
                     robo_html_tree_result_t *out)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, out, NULL), ROBO_OK);
}

static robo_html_node_handle_t tb_root(robo_html_document_id_t id)
{
    robo_html_node_handle_t root = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    return root;
}

/** Handle of the first descendant element named @p name. */
static robo_html_node_handle_t tb_find(robo_html_document_id_t id,
                                       const char *name)
{
    robo_html_node_handle_t root = tb_root(id);
    robo_html_node_handle_t found;

    robo_html_node_handle_clear(&found);
    if (robo_html_node_find_element(&root, name, &found, NULL) != ROBO_OK) {
        robo_html_node_handle_clear(&found);
    }
    return found;
}

/** Concatenated text of a subtree; "" when the node is unknown. */
static void tb_text(robo_html_node_handle_t h, char *buf, size_t cap)
{
    size_t length = 0u;

    buf[0] = '\0';
    if (robo_html_node_subtree_text(&h, buf, cap, &length, NULL) != ROBO_OK) {
        buf[0] = '\0';
    }
}

static size_t tb_child_count(robo_html_node_handle_t h)
{
    size_t count = 0u;

    ROBO_CHECK_EQ_INT(robo_html_node_child_count(&h, &count, NULL), ROBO_OK);
    return count;
}

/** Text of the first descendant @p name's subtree. */
static void tb_text_of(robo_html_document_id_t id,
                       const char *name,
                       char *buf,
                       size_t cap)
{
    robo_html_node_handle_t h = tb_find(id, name);

    tb_text(h, buf, cap);
}

/* ------------------------------------------------------------------ */
/* Module surface                                                      */
/* ------------------------------------------------------------------ */

static void test_tb_model_helpers(void)
{
    tb_fixture_begin();

    ROBO_CHECK(robo_html_insertion_mode_is_known(ROBO_HTML_MODE_INITIAL));
    ROBO_CHECK(robo_html_insertion_mode_is_known(ROBO_HTML_MODE_TEXT));
    ROBO_CHECK(!robo_html_insertion_mode_is_known((robo_html_insertion_mode_t)42));
    ROBO_CHECK_STR_EQ(robo_html_insertion_mode_name(ROBO_HTML_MODE_IN_BODY),
                      "in-body");
    ROBO_CHECK_STR_EQ(robo_html_insertion_mode_name(ROBO_HTML_MODE_TEXT), "text");
    ROBO_CHECK_STR_EQ(robo_html_insertion_mode_name((robo_html_insertion_mode_t)9),
                      "unknown");

    ROBO_CHECK(robo_html_text_mode_is_known(ROBO_HTML_TEXT_MODE_RCDATA));
    ROBO_CHECK(!robo_html_text_mode_is_known((robo_html_text_mode_t)9));
    ROBO_CHECK_STR_EQ(robo_html_text_mode_name(ROBO_HTML_TEXT_MODE_RAWTEXT),
                      "rawtext");
    ROBO_CHECK_STR_EQ(robo_html_text_mode_name((robo_html_text_mode_t)7),
                      "unknown");

    /* Token-kind helpers the tree builder's consume() depends on. */
    ROBO_CHECK(robo_html_token_kind_is_known(ROBO_HTML_TOKEN_START_TAG));
    ROBO_CHECK(!robo_html_token_kind_is_known((robo_html_token_kind_t)77));
    ROBO_CHECK_STR_EQ(robo_html_token_kind_name(ROBO_HTML_TOKEN_END_TAG),
                      "end-tag");
    ROBO_CHECK_STR_EQ(robo_html_token_kind_name((robo_html_token_kind_t)77),
                      "unknown");

    tb_fixture_end();
}

static void test_tb_create_misuse(void)
{
    robo_html_document_id_t id;
    robo_html_tree_builder_t *builder = NULL;

    tb_fixture_begin();
    id = tb_doc();

    ROBO_CHECK_EQ_INT(robo_html_tree_builder_create(id, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_html_tree_builder_create(ROBO_HTML_DOCUMENT_ID_NONE, &builder, NULL),
        ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(builder, 0);

    ROBO_CHECK_EQ_INT(robo_html_tree_builder_create(id, &builder, NULL), ROBO_OK);
    ROBO_CHECK(builder != NULL);

    /* The document lease is exclusive: a second builder is refused. */
    {
        robo_html_tree_builder_t *second = NULL;

        ROBO_CHECK_EQ_INT(robo_html_tree_builder_create(id, &second, NULL),
                          ROBO_BUSY);
        ROBO_CHECK_EQ_INT(second, 0);
    }

    /* Nothing works before begin(). */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_next(builder, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_html_tree_builder_consume(builder, NULL, NULL),
        ROBO_INVALID_ARGUMENT);

    robo_html_tree_builder_destroy(builder);
    /* A second destroy is a no-op, never undefined behaviour. */
    robo_html_tree_builder_destroy(builder);
    robo_html_tree_builder_destroy(NULL);

    /* The lease is gone, so a new builder can take it. */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_create(id, &builder, NULL), ROBO_OK);
    robo_html_tree_builder_destroy(builder);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    tb_fixture_end();
}

static void test_tb_begin_misuse(void)
{
    robo_html_document_id_t id;
    robo_html_tree_builder_t *builder = NULL;
    robo_html_source_t src;
    robo_html_tree_result_t result;
    robo_html_parser_state_t state;
    robo_html_insertion_mode_t mode;
    robo_html_text_mode_t text_mode;
    robo_html_node_handle_t node;

    tb_fixture_begin();
    id = tb_doc();
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_create(id, &builder, NULL), ROBO_OK);

    /* No source → refused. NONE-kind source → refused. */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_begin(builder, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_begin(builder, &src, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Introspection before begin. */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_state(builder, &state), ROBO_OK);
    ROBO_CHECK_EQ_INT(state, ROBO_HTML_PARSER_STATE_IDLE);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_mode(builder, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT(mode, ROBO_HTML_MODE_INITIAL);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_text_mode(builder, &text_mode),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(text_mode, ROBO_HTML_TEXT_MODE_NONE);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_result(builder, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT(result.node_count, 0);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_diagnostic_count(builder), 0);
    ROBO_CHECK_EQ_INT(
        robo_html_tree_builder_current_node(builder, &node), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&node, &(robo_html_node_handle_t){
                                                     id, 0u, 0u}) == 0);

    /* Text mode cannot change before the build starts. */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_set_text_mode(
                          builder, ROBO_HTML_TEXT_MODE_RCDATA, NULL),
                      ROBO_INIT_ERROR);

    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, "<p>x</p>", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_begin(builder, &src, NULL), ROBO_OK);

    /* Unknown text mode enumerator → rejected while building. */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_set_text_mode(
                          builder, (robo_html_text_mode_t)33, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* A consume() of an ill-formed token is refused, not applied. */
    {
        robo_html_token_t tok;

        robo_html_token_init(&tok);
        ROBO_CHECK_EQ_INT(robo_html_tree_builder_consume(builder, &tok, NULL),
                          ROBO_INVALID_ARGUMENT);
    }

    /* Re-entering begin() on an in-flight build is refused, not restarted. */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_begin(builder, &src, NULL),
                      ROBO_ERROR);

    /* That begin() attached the source, so drive it to completion. */
    {
        int i;

        for (i = 0; i < 64; i++) {
            robo_html_parser_state_t step = ROBO_HTML_PARSER_STATE_IDLE;

            ROBO_CHECK_EQ_INT(robo_html_tree_builder_next(builder, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_state(builder, &step),
                              ROBO_OK);
            if (step == ROBO_HTML_PARSER_STATE_COMPLETE) {
                break;
            }
        }
    }
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_state(builder, &state), ROBO_OK);
    ROBO_CHECK_EQ_INT(state, ROBO_HTML_PARSER_STATE_COMPLETE);
    /* finish() is idempotent, and build() re-parses from a complete state. */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_finish(builder, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_build(builder, &src, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_result(builder, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT(result.element_count, 4); /* html head body p */

    robo_html_tree_builder_destroy(builder);
    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Document structure                                                  */
/* ------------------------------------------------------------------ */

static void test_tb_empty_document(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;

    tb_fixture_begin();
    id = tb_doc();
    ROBO_CHECK(robo_html_document_is_empty(id));

    tb_build(id, "", &r);
    ROBO_CHECK(!robo_html_document_is_empty(id));
    /* Browsers synthesize html/head/body even for empty input. */
    ROBO_CHECK_EQ_INT(r.doctype_count, 0);
    ROBO_CHECK_EQ_INT(r.comment_count, 0);
    ROBO_CHECK_EQ_INT(r.text_count, 0);
    ROBO_CHECK_EQ_INT(r.element_count, 3);
    ROBO_CHECK_EQ_INT(r.node_count, 4);
    ROBO_CHECK_EQ_INT(r.max_depth, 2);
    ROBO_CHECK_EQ_INT(r.recovered_errors, 0);
    ROBO_CHECK_EQ_INT(r.truncated, 0);
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(id), 4);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    tb_fixture_end();
}

static void test_tb_basic_document(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    char buf[64];

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id,
             "<!DOCTYPE html><html><head><title>T</title></head>"
             "<body><h1>H</h1></body></html>",
             &r);

    ROBO_CHECK_EQ_INT(r.doctype_count, 1);
    ROBO_CHECK_EQ_INT(r.element_count, 5); /* html head title body h1 */
    ROBO_CHECK_EQ_INT(r.text_count, 2);
    ROBO_CHECK_EQ_INT(r.comment_count, 0);
    ROBO_CHECK_EQ_INT(r.recovered_errors, 0);
    ROBO_CHECK_EQ_INT(r.max_depth, 3); /* html > head > title */

    tb_text_of(id, "title", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "T");
    tb_text_of(id, "h1", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "H");
    tb_text_of(id, "head", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "T");
    tb_text_of(id, "body", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "H");

    /* The document node owns the tree, and the DOM boundary reports it. */
    {
        robo_html_dom_capabilities_t caps;

        ROBO_CHECK_EQ_INT(robo_html_dom_query_capabilities(&caps), ROBO_OK);
        ROBO_CHECK_EQ_INT(caps.tree_building, 1);
        ROBO_CHECK_EQ_INT(caps.dom_nodes, 0);
        ROBO_CHECK_EQ_INT(caps.events, 0);
    }

    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_implied_structural_elements(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    char buf[64];

    tb_fixture_begin();

    /* Missing <html>: implied around the content. */
    id = tb_doc();
    tb_build(id, "<p>hi</p>", &r);
    ROBO_CHECK_EQ_INT(r.element_count, 4); /* html head body p */
    tb_text_of(id, "body", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "hi");
    (void)robo_html_document_destroy(id, NULL);

    /* Missing <head>: implied before <body>. */
    id = tb_doc();
    tb_build(id, "<html><body><p>x</p></body></html>", &r);
    ROBO_CHECK_EQ_INT(r.element_count, 4);
    tb_text_of(id, "html", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "x");
    (void)robo_html_document_destroy(id, NULL);

    /* Missing <body>. */
    id = tb_doc();
    tb_build(id, "<p>y</p>", &r);
    ROBO_CHECK_EQ_INT(r.element_count, 4);
    tb_text_of(id, "body", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "y");
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

static void test_tb_text_before_html(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t body;
    robo_html_node_handle_t first;
    char buf[64];

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "stray<p>inside</p>", &r);

    body = tb_find(id, "body");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&body));
    ROBO_CHECK_EQ_INT(tb_child_count(body), 2);
    ROBO_CHECK_EQ_INT(
        robo_html_node_child_at(&body, 0u, &first, NULL), ROBO_OK);
    {
        robo_html_node_view_t view;

        ROBO_CHECK_EQ_INT(robo_html_node_get_view(&first, &view, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(view.kind, ROBO_HTML_NODE_KIND_TEXT);
        ROBO_CHECK_STR_EQ(view.text, "stray");
    }
    tb_text(body, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "strayinside");
    /* <head> exists but is empty of text. */
    tb_text_of(id, "head", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "");

    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_element_insertion_and_nesting(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t div;
    robo_html_node_handle_t section;
    robo_html_node_handle_t p;
    char buf[64];

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<div><section><p>a</p></section></div>", &r);

    ROBO_CHECK_EQ_INT(r.element_count, 6); /* html head body div section p */
    ROBO_CHECK_EQ_INT(r.max_depth, 5);     /* html > body > div > section > p */

    div = tb_find(id, "div");
    section = tb_find(id, "section");
    p = tb_find(id, "p");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&div));
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&section));
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&p));

    {
        robo_html_node_view_t view;

        /* Parent/child relationships line up. */
        ROBO_CHECK_EQ_INT(robo_html_node_get_view(&section, &view, NULL), ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&view.parent, &div));
        ROBO_CHECK_EQ_INT(robo_html_node_get_view(&p, &view, NULL), ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&view.parent, &section));
        ROBO_CHECK_EQ_INT(tb_child_count(div), 1);
        ROBO_CHECK_EQ_INT(tb_child_count(section), 1);
        ROBO_CHECK_EQ_INT(tb_child_count(p), 1);
    }
    tb_text(p, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "a");

    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_siblings_and_child_order(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t body;
    char texts[4][16];
    size_t i;

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<p>one</p><p>two</p><span>three</span>", &r);

    body = tb_find(id, "body");
    ROBO_CHECK_EQ_INT(tb_child_count(body), 3);
    for (i = 0u; i < 3u; i++) {
        robo_html_node_handle_t child;

        ROBO_CHECK_EQ_INT(robo_html_node_child_at(&body, i, &child, NULL),
                          ROBO_OK);
        texts[i][0] = '\0';
        {
            size_t length = 0u;

            ROBO_CHECK_EQ_INT(
                robo_html_node_subtree_text(&child, texts[i], sizeof(texts[i]),
                                            &length, NULL),
                ROBO_OK);
        }
    }
    ROBO_CHECK_STR_EQ(texts[0], "one");
    ROBO_CHECK_STR_EQ(texts[1], "two");
    ROBO_CHECK_STR_EQ(texts[2], "three");

    /* Out-of-range child lookups report NOT_FOUND, never a bogus handle. */
    {
        robo_html_node_handle_t bogus;

        ROBO_CHECK_EQ_INT(robo_html_node_child_at(&body, 9u, &bogus, NULL),
                          ROBO_NOT_FOUND);
    }

    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_text_nodes_and_merging(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t p;
    char buf[128];

    tb_fixture_begin();
    id = tb_doc();

    /* A character reference in the middle must not fragment the text run. */
    tb_build(id, "<p>Tom &amp; Jerry</p>", &r);
    ROBO_CHECK_EQ_INT(r.text_count, 1);
    p = tb_find(id, "p");
    ROBO_CHECK_EQ_INT(tb_child_count(p), 1);
    tb_text(p, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "Tom & Jerry");
    (void)robo_html_document_destroy(id, NULL);

    /* Empty text is never materialized as a node. */
    id = tb_doc();
    tb_build(id, "<p></p>", &r);
    ROBO_CHECK_EQ_INT(r.text_count, 0);
    p = tb_find(id, "p");
    ROBO_CHECK_EQ_INT(tb_child_count(p), 0);
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

static void test_tb_comments(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t div;
    robo_html_node_handle_t child;
    robo_html_node_view_t view;

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<!--lead--><div><!-- inner --></div><!--tail-->", &r);

    ROBO_CHECK_EQ_INT(r.comment_count, 3);
    /* Leading comment attaches to the document; trailing one too. */
    {
        robo_html_node_handle_t root = tb_root(id);
        robo_html_node_handle_t first;

        ROBO_CHECK_EQ_INT(robo_html_node_child_at(&root, 0u, &first, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_get_view(&first, &view, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(view.kind, ROBO_HTML_NODE_KIND_COMMENT);
        ROBO_CHECK_STR_EQ(view.text, "lead");
    }
    div = tb_find(id, "div");
    ROBO_CHECK_EQ_INT(tb_child_count(div), 1);
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&div, 0u, &child, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&child, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(view.kind, ROBO_HTML_NODE_KIND_COMMENT);
    ROBO_CHECK_STR_EQ(view.text, " inner ");

    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_doctype(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t root;
    robo_html_node_handle_t child;
    robo_html_node_view_t view;
    robo_html_element_view_t element;

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<!DOCTYPE html><p>x</p>", &r);
    ROBO_CHECK_EQ_INT(r.doctype_count, 1);

    root = tb_root(id);
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&root, 0u, &child, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&child, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(view.kind, ROBO_HTML_NODE_KIND_DOCUMENT_TYPE);
    ROBO_CHECK_STR_EQ(view.name, "html");
    (void)element;
    (void)robo_html_document_destroy(id, NULL);

    /* A malformed DOCTYPE is recovered, not fatal. */
    id = tb_doc();
    tb_build(id, "<!DOCTYPE><p>y</p>", &r);
    ROBO_CHECK_EQ_INT(r.element_count, 4);
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

static void test_tb_attributes(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t a;
    robo_html_element_view_t view;
    robo_html_attribute_t attr;

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<a href=\"/x\" target='_blank' disabled>link</a>", &r);

    ROBO_CHECK_EQ_INT(r.attribute_count, 3);
    a = tb_find(id, "a");
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&a, &view, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(view.name, "a");
    ROBO_CHECK_EQ_INT(view.attribute_count, 3);
    ROBO_CHECK_EQ_INT(view.self_closing, 0);

    ROBO_CHECK_EQ_INT(robo_html_element_find_attribute(&a, "href", &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.value, "/x");
    ROBO_CHECK_EQ_INT(
        robo_html_element_find_attribute(&a, "TARGET", &attr, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.value, "_blank");
    /* A valueless attribute stores an empty string. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_find_attribute(&a, "disabled", &attr, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.value, "");
    ROBO_CHECK_EQ_INT(
        robo_html_element_find_attribute(&a, "missing", &attr, NULL),
        ROBO_NOT_FOUND);

    /* Attribute order is source order. */
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&a, 0u, &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.name, "href");
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&a, 3u, &attr, NULL),
                      ROBO_NOT_FOUND);

    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_void_and_self_closing_elements(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t img;
    robo_html_element_view_t view;
    char buf[64];

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<p>a<img src=\"x.png\"/>b<br>c</p>", &r);

    img = tb_find(id, "img");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&img));
    ROBO_CHECK_EQ_INT(tb_child_count(img), 0);
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&img, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(view.self_closing, 1);

    /* Void elements never swallow following content. */
    tb_text_of(id, "p", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "abc");
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

static void test_tb_start_end_tag_matching(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t div;
    char buf[64];

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<div><p>a</p>b</div>", &r);

    div = tb_find(id, "div");
    ROBO_CHECK_EQ_INT(tb_child_count(div), 2);
    tb_text(div, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "ab");
    /* A matching </p> closes exactly the open <p>. */
    ROBO_CHECK_EQ_INT(r.recovered_errors, 0);
    (void)robo_html_document_destroy(id, NULL);

    /* A structural end tag pops back to it and drops inner elements. */
    id = tb_doc();
    tb_build(id, "<div><span>a</div>b", &r);
    div = tb_find(id, "div");
    tb_text(div, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "a");
    tb_text_of(id, "body", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "ab");
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

static void test_tb_unexpected_end_tags(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    char buf[64];

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "</div>a</span>", &r);

    ROBO_CHECK(r.recovered_errors >= 2);
    tb_text_of(id, "body", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "a");
    ROBO_CHECK_EQ_INT(r.element_count, 3); /* html head body */
    (void)robo_html_document_destroy(id, NULL);

    /* </br> is re-run as <br>, per the HTML model. */
    id = tb_doc();
    tb_build(id, "<p>a</br>b</p>", &r);
    ROBO_CHECK(r.recovered_errors >= 1);
    {
        robo_html_node_handle_t br = tb_find(id, "br");

        ROBO_CHECK(robo_html_node_handle_is_wellformed(&br));
    }
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

static void test_tb_unclosed_elements_at_eof(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t div;
    char buf[64];

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<div><p>x", &r);

    ROBO_CHECK(r.recovered_errors >= 2); /* <div> and <p> left open */
    div = tb_find(id, "div");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&div));
    tb_text(div, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "x");
    /* Unclosed elements still end up linked into the tree. */
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(id), 7u);
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

static void test_tb_duplicate_structural_tags(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<html><html><body><body><p>x</p></body></html>", &r);

    ROBO_CHECK_EQ_INT(r.element_count, 4); /* html head body p */
    ROBO_CHECK(r.recovered_errors >= 2);
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

static void test_tb_title_rcdata(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t title;
    char buf[64];

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<title>Roboticela &amp; Runtime</title><p>x</p>", &r);

    title = tb_find(id, "title");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&title));
    tb_text(title, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "Roboticela & Runtime");
    ROBO_CHECK_EQ_INT(r.text_count, 2);
    (void)robo_html_document_destroy(id, NULL);

    /* Markup inside RCDATA is text, not elements. */
    id = tb_doc();
    tb_build(id, "<title><b>not an element</b></title>", &r);
    ROBO_CHECK_EQ_INT(r.element_count, 4); /* html head title + implied body */
    tb_text_of(id, "title", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "<b>not an element</b>");
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

static void test_tb_div_span_button_structures(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t div;
    robo_html_node_handle_t span;
    robo_html_node_handle_t button;
    char buf[64];

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<div><span>t</span><button>Demo Button</button></div>", &r);

    div = tb_find(id, "div");
    span = tb_find(id, "span");
    button = tb_find(id, "button");
    ROBO_CHECK_EQ_INT(tb_child_count(div), 2);
    {
        robo_html_node_view_t view;

        ROBO_CHECK_EQ_INT(robo_html_node_get_view(&span, &view, NULL), ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&view.parent, &div));
    }
    tb_text(button, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "Demo Button");
    (void)robo_html_document_destroy(id, NULL);

    /* Nested <button> closes the outer one instead of nesting. */
    id = tb_doc();
    tb_build(id, "<button>a<button>b</button>", &r);
    ROBO_CHECK(r.recovered_errors >= 1);
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(id), 8u);
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

static void test_tb_anchor_image_form_input(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t form;
    robo_html_node_handle_t input;
    robo_html_element_view_t view;
    char buf[64];

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id,
             "<a href=\"/x\">link</a>"
             "<form action=\"/search\"><input name=\"q\" value=\"v\"></form>"
             "<img src=\"logo.png\" alt=\"logo\">",
             &r);

    {
        robo_html_node_handle_t a = tb_find(id, "a");

        ROBO_CHECK(robo_html_node_handle_is_wellformed(&a));
    }
    tb_text_of(id, "a", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "link");

    form = tb_find(id, "form");
    input = tb_find(id, "input");
    ROBO_CHECK_EQ_INT(tb_child_count(form), 1);
    {
        robo_html_node_view_t node_view;

        ROBO_CHECK_EQ_INT(robo_html_node_get_view(&input, &node_view, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&node_view.parent, &form));
    }
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&input, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(view.attribute_count, 2);
    ROBO_CHECK_EQ_INT(tb_child_count(input), 0);

    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_open_element_stack(void)
{
    robo_html_document_id_t id;
    robo_html_tree_builder_t *builder = NULL;
    robo_html_source_t src;
    robo_html_node_handle_t node;
    robo_html_node_view_t view;
    size_t count = 99u;

    tb_fixture_begin();
    id = tb_doc();
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_create(id, &builder, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, "<div><p>x</p></div>", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_begin(builder, &src, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_tree_builder_open_element_count(builder, &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 0);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_open_element(builder, 0u, &node),
                      ROBO_NOT_FOUND);

    /*
     * Drive token by token: <div> <p> x — the stack is html, body, div, p
     * with <p> as the current node.
     */
    {
        int i;

        for (i = 0; i < 3; i++) {
            ROBO_CHECK_EQ_INT(robo_html_tree_builder_next(builder, NULL), ROBO_OK);
        }
    }
    ROBO_CHECK_EQ_INT(
        robo_html_tree_builder_current_node(builder, &node), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&node, &view, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(view.name, "p");
    ROBO_CHECK_EQ_INT(
        robo_html_tree_builder_open_element_count(builder, &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 4); /* html, body, div, p */

    {
        robo_html_node_handle_t outer;

        ROBO_CHECK_EQ_INT(robo_html_tree_builder_open_element(builder, 0u, &outer),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_get_view(&outer, &view, NULL), ROBO_OK);
        ROBO_CHECK_STR_EQ(view.name, "html");
    }

    while (robo_html_tree_builder_next(builder, NULL) == ROBO_OK) {
        robo_html_parser_state_t state;

        ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_state(builder, &state),
                          ROBO_OK);
        if (state == ROBO_HTML_PARSER_STATE_COMPLETE) {
            break;
        }
    }
    ROBO_CHECK_EQ_INT(
        robo_html_tree_builder_open_element_count(builder, &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 0);

    robo_html_tree_builder_destroy(builder);
    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_insertion_mode_transitions(void)
{
    robo_html_document_id_t id;
    robo_html_tree_builder_t *builder = NULL;
    robo_html_source_t src;
    robo_html_insertion_mode_t mode;
    robo_html_text_mode_t text_mode;
    robo_html_tree_result_t result;

    tb_fixture_begin();
    id = tb_doc();
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_create(id, &builder, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_source_string(&src, "<html><head><title>T</title></head>"
                                      "<body><h1>H</h1></body></html>",
                                 NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_begin(builder, &src, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_mode(builder, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT(mode, ROBO_HTML_MODE_INITIAL);

    /* <html> */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_next(builder, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_mode(builder, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT(mode, ROBO_HTML_MODE_BEFORE_HEAD);
    /* <head> */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_next(builder, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_mode(builder, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT(mode, ROBO_HTML_MODE_IN_HEAD);
    /* <title> → TEXT/RCDATA */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_next(builder, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_mode(builder, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT(mode, ROBO_HTML_MODE_TEXT);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_text_mode(builder, &text_mode),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(text_mode, ROBO_HTML_TEXT_MODE_RCDATA);

    /*
     * Drive the rest of the stream. build() would re-begin the build, so
     * finish the in-flight one through next() instead.
     */
    {
        int i;

        for (i = 0; i < 64; i++) {
            robo_html_parser_state_t state;

            ROBO_CHECK_EQ_INT(robo_html_tree_builder_next(builder, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_state(builder, &state),
                              ROBO_OK);
            if (state == ROBO_HTML_PARSER_STATE_COMPLETE) {
                break;
            }
        }
    }
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_mode(builder, &mode), ROBO_OK);
    /* Trailing </html> leaves the builder in the final mode. */
    ROBO_CHECK_EQ_INT(mode, ROBO_HTML_MODE_AFTER_AFTER_BODY);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_text_mode(builder, &text_mode),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(text_mode, ROBO_HTML_TEXT_MODE_NONE);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_result(builder, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT(result.element_count, 5);
    {
        robo_html_document_lifecycle_t life;

        ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
        ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_READY);
    }

    robo_html_tree_builder_destroy(builder);
    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_ownership_and_stale_handles(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t h1;
    robo_html_node_kind_t kind = ROBO_HTML_NODE_KIND_FRAGMENT;

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<h1>H</h1>", &r);

    h1 = tb_find(id, "h1");
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&h1, &kind), ROBO_OK);
    ROBO_CHECK_EQ_INT(kind, ROBO_HTML_NODE_KIND_ELEMENT);
    ROBO_CHECK(!robo_html_node_handle_is_stale(&h1));

    /* Rebuilding the document retires every previously handed-out node. */
    tb_build(id, "<p>P</p>", &r);
    ROBO_CHECK(robo_html_node_handle_is_stale(&h1));
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&h1, &kind), ROBO_NOT_FOUND);

    /* Ill-formed handles are argument errors, not stale lookups. */
    {
        robo_html_node_handle_t bogus;

        robo_html_node_handle_clear(&bogus);
        ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&bogus, &kind),
                          ROBO_INVALID_ARGUMENT);        ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(NULL, &kind),
                          ROBO_INVALID_ARGUMENT);
        /* out_kind is optional, so a pure validation succeeds on a live node. */
        {
            robo_html_node_handle_t live = tb_find(id, "p");

            ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&live, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&h1, NULL),
                              ROBO_NOT_FOUND);
        }
        ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&(robo_html_node_handle_t){
                              9999u, 1u, 1u},
                                                          &kind),
                          ROBO_NOT_FOUND);
    }

    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_document_teardown(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    robo_html_node_handle_t root;
    robo_html_node_handle_t h1;

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<div><p>x</p></div>", &r);
    root = tb_root(id);
    h1 = tb_find(id, "div");

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    /* Every node handle is retired with its document. */
    ROBO_CHECK(robo_html_node_handle_is_stale(&root));
    ROBO_CHECK(robo_html_node_handle_is_stale(&h1));
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(id), 0);
    ROBO_CHECK(!robo_html_document_is_live(id));
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_NOT_FOUND);
    /* Building into a destroyed document fails cleanly. */
    {
        robo_html_source_t src;

        robo_html_source_init(&src);
        ROBO_CHECK_EQ_INT(robo_html_source_string(&src, "<p>x</p>", NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, NULL, NULL),
                          ROBO_NOT_FOUND);
    }

    tb_fixture_end();
}

static void test_tb_malformed_recovery(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    char buf[128];

    tb_fixture_begin();

    /* Unclosed div with nested span: closes on </div>, text continues. */
    id = tb_doc();
    tb_build(id, "<div><p>a<span>b</div>c", &r);
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(id), 10u);
    tb_text_of(id, "body", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "abc");
    (void)robo_html_document_destroy(id, NULL);

    /* Stray markup characters are text, not elements. */
    id = tb_doc();
    tb_build(id, "<p>1 < 2 > 3</p>", &r);
    tb_text_of(id, "p", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "1 < 2 > 3");
    (void)robo_html_document_destroy(id, NULL);

    /* An element that is never closed still carries its text. */
    id = tb_doc();
    tb_build(id, "<p>unterminated", &r);
    ROBO_CHECK_EQ_INT(r.element_count, 4);
    tb_text_of(id, "p", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "unterminated");
    (void)robo_html_document_destroy(id, NULL);

    /* Deeply-mismatched end tags still leave a usable tree. */
    id = tb_doc();
    tb_build(id, "</p></p></div></body></html></html>", &r);
    ROBO_CHECK(r.recovered_errors >= 1);
    ROBO_CHECK_EQ_INT(r.element_count, 3);
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

static void test_tb_large_nested_tree(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    char html[4096];
    size_t used = 0u;
    int i;

    tb_fixture_begin();
    id = tb_doc();
    for (i = 0; i < 200; i++) {
        html[used++] = '<';
        html[used++] = 'd';
        html[used++] = 'i';
        html[used++] = 'v';
        html[used++] = '>';
    }
    html[used++] = 'x';
    html[used] = '\0';

    tb_build(id, html, &r);
    ROBO_CHECK_EQ_INT(r.element_count, 203); /* html head body + 200 divs */
    ROBO_CHECK_EQ_INT(r.max_depth, 202);
    ROBO_CHECK_EQ_INT(r.truncated, 0);
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

static void test_tb_node_ceiling_truncates(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    char html[32768];
    size_t used = 0u;
    int i;

    tb_fixture_begin();
    id = tb_doc();
    for (i = 0; i < 4200 && used + 6u < sizeof(html); i++) {
        html[used++] = '<';
        html[used++] = 's';
        html[used++] = 'p';
        html[used++] = 'a';
        html[used++] = 'n';
        html[used++] = '>';
    }
    html[used] = '\0';

    /* Each <span> costs one node plus its element payload: over the ceiling. */
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &(robo_html_source_t){0}, NULL,
                                          NULL),
                      ROBO_INVALID_ARGUMENT);
    tb_build(id, html, &r);
    ROBO_CHECK_EQ_INT(r.truncated, 1);
    ROBO_CHECK(r.node_count <= ROBO_HTML_MAX_NODES_PER_DOCUMENT);
    ROBO_CHECK(robo_html_live_document_count() >= 1u);

    /* The document is still usable and still owns a valid tree. */
    {
        robo_html_node_handle_t root = tb_root(id);

        ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    }
    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_unicode_text(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    char buf[256];
    const char *text = "h\xc3\xa9llo \xe2\x80\x94 \xe4\xb8\x96\xe7\x95\x8c "
                       "\xf0\x9f\x98\x80";

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<p>h\xc3\xa9llo \xe2\x80\x94 \xe4\xb8\x96\xe7\x95\x8c "
                 "\xf0\x9f\x98\x80</p>",
             &r);

    tb_text_of(id, "p", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, text);
    ROBO_CHECK_EQ_INT(r.text_count, 1);
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Tree dump                                                           */
/* ------------------------------------------------------------------ */

static robo_result_t tb_collect_sink(const char *text, size_t length, void *user)
{
    char *buffer = (char *)user;
    size_t used = strlen(buffer);

    memcpy(buffer + used, text, length);
    buffer[used + length] = '\0';
    return ROBO_OK;
}

static robo_result_t tb_failing_sink(const char *text, size_t length, void *user)
{
    (void)text;
    (void)length;
    (void)user;
    return ROBO_ERROR;
}

static void test_tb_tree_dump(void)
{
    robo_html_document_id_t id;
    static char buffer[8192];
    size_t length = 0u;

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id, "<!DOCTYPE html><html><head><title>RoboticelaOS Runtime"
                 "</title></head><body><h1>Heading</h1><p>Body &amp; text</p>"
                 "<button>Demo Button</button><img src=\"a.png\"/></body></html>",
             NULL);

    buffer[0] = '\0';
    ROBO_CHECK_EQ_INT(robo_html_tree_dump(id, tb_collect_sink, buffer, NULL),
                      ROBO_OK);
    ROBO_CHECK(strstr(buffer, "Document") != NULL);
    ROBO_CHECK(strstr(buffer, "doctype html") != NULL);
    ROBO_CHECK(strstr(buffer, "  html\n") != NULL);
    ROBO_CHECK(strstr(buffer, "    head\n") != NULL);
    ROBO_CHECK(strstr(buffer, "        Text: \"RoboticelaOS Runtime\"\n") !=
               NULL);
    ROBO_CHECK(strstr(buffer, "    body\n") != NULL);
    ROBO_CHECK(strstr(buffer, "      h1\n") != NULL);
    ROBO_CHECK(strstr(buffer, "        Text: \"Heading\"") != NULL);
    ROBO_CHECK(strstr(buffer, "\"Body & text\"") != NULL);
    ROBO_CHECK(strstr(buffer, "button\n") != NULL);
    ROBO_CHECK(strstr(buffer, "\"Demo Button\"") != NULL);
    ROBO_CHECK(strstr(buffer, "img src=\"a.png\" /") != NULL);
    ROBO_CHECK(strstr(buffer, "#comment") == NULL);

    /* Buffer variant reports the untruncated length. */
    {
        char small[24];

        ROBO_CHECK_EQ_INT(
            robo_html_tree_dump_to_buffer(id, small, sizeof(small), &length,
                                          NULL),
            ROBO_OK);
        ROBO_CHECK(length > sizeof(small));
        ROBO_CHECK_EQ_INT((int)strlen(small), (int)sizeof(small) - 1);
    }

    /* Misuse: NULL sink, unknown document, empty buffer. */
    ROBO_CHECK_EQ_INT(robo_html_tree_dump(id, NULL, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump(ROBO_HTML_DOCUMENT_ID_NONE, tb_collect_sink, NULL,
                            NULL),
        ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_tree_dump_to_buffer(id, NULL, 32u, &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump_to_buffer(id, buffer, 0u, &length, NULL),
        ROBO_INVALID_ARGUMENT);
    /* A sink that refuses aborts the dump with its own code. */
    ROBO_CHECK_EQ_INT(robo_html_tree_dump(id, tb_failing_sink, NULL, NULL),
                      ROBO_ERROR);

    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Integration + the visual-demo document                              */
/* ------------------------------------------------------------------ */

static void test_tb_tokenizer_integration(void)
{
    robo_html_document_id_t id;
    robo_html_tree_builder_t *builder = NULL;
    robo_html_source_t src;
    robo_html_tree_result_t result;
    size_t diagnostics;
    size_t i;

    tb_fixture_begin();
    id = tb_doc();
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_create(id, &builder, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_source_string(&src,
                                "<!DOCTYPE html><div id=\"a\">one<!--c--></div>",
                                NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_begin(builder, &src, NULL), ROBO_OK);

    /* Bounded: never more iterations than the input can produce. */
    for (i = 0u; i < 64u; i++) {
        robo_html_parser_state_t state;

        ROBO_CHECK_EQ_INT(robo_html_tree_builder_next(builder, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_state(builder, &state),
                          ROBO_OK);
        if (state == ROBO_HTML_PARSER_STATE_COMPLETE) {
            break;
        }
    }
    ROBO_CHECK(i < 64u);

    ROBO_CHECK_EQ_INT(robo_html_tree_builder_result(builder, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT(result.doctype_count, 1);
    ROBO_CHECK_EQ_INT(result.element_count, 4); /* html head body div */
    ROBO_CHECK_EQ_INT(result.comment_count, 1);
    ROBO_CHECK_EQ_INT(result.attribute_count, 1);
    ROBO_CHECK_EQ_INT(result.recovered_errors, 0);

    diagnostics = robo_html_tree_builder_diagnostic_count(builder);
    for (i = 0u; i < diagnostics; i++) {
        robo_html_diag_t diag;

        ROBO_CHECK_EQ_INT(
            robo_html_tree_builder_get_diagnostic(builder, i, &diag), ROBO_OK);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_PARSER);
    }
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_diagnostic(builder, diagnostics,
                                                            &(robo_html_diag_t){0}),
                      ROBO_NOT_FOUND);

    robo_html_tree_builder_destroy(builder);

    /* Destroying the builder leaves the tree owned by the document. */
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(id), 8u);
    {
        robo_html_document_lifecycle_t life;

        ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
        ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_READY);
    }
    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_diagnostics_for_recovery(void)
{
    robo_html_document_id_t id;
    robo_html_tree_builder_t *builder = NULL;
    robo_html_source_t src;
    robo_html_diag_t diag;
    size_t count;

    tb_fixture_begin();
    id = tb_doc();
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_create(id, &builder, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, "</div><span>a", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_build(builder, &src, NULL), ROBO_OK);

    count = robo_html_tree_builder_diagnostic_count(builder);
    ROBO_CHECK(count >= 2u); /* stray end tag + unclosed <span> */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_diagnostic(builder, 0u, &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_PARSER);
    ROBO_CHECK(diag.message != NULL);

    robo_html_tree_builder_destroy(builder);
    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_demo_document_tree(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    char buf[256];

    tb_fixture_begin();
    id = tb_doc();
    tb_build(id,
             "<!DOCTYPE html>\n"
             "<html>\n"
             "<head>\n"
             "<title>RoboticelaOS Runtime</title>\n"
             "</head>\n"
             "<body>\n"
             "<h1>RoboticelaOS Runtime</h1>\n"
             "<p>Independent HTML rendering is working.</p>\n"
             "<p>RoboGraphics + RoboWindow + RoboHTML</p>\n"
             "<button>Demo Button</button>\n"
             "</body>\n"
             "</html>\n",
             &r);

    ROBO_CHECK_EQ_INT(r.doctype_count, 1);
    ROBO_CHECK_EQ_INT(r.element_count, 8); /* html head title body h1 p p button */
    ROBO_CHECK_EQ_INT(r.truncated, 0);
    ROBO_CHECK_EQ_INT(r.recovered_errors, 0);

    tb_text_of(id, "title", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "RoboticelaOS Runtime");
    tb_text_of(id, "h1", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "RoboticelaOS Runtime");
    tb_text_of(id, "button", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "Demo Button");

    /* The two paragraphs are siblings under body, in source order. */
    {
        robo_html_node_handle_t body = tb_find(id, "body");
        robo_html_node_handle_t child;
        int seen_p = 0;

        /* 6 text (body, before/after each block, and after </body>) + 4 elements */
        ROBO_CHECK_EQ_INT(tb_child_count(body), 10u);
        {
            size_t i;
            size_t children = tb_child_count(body);

            for (i = 0u; i < children; i++) {
                robo_html_element_view_t view;

                ROBO_CHECK_EQ_INT(robo_html_node_child_at(&body, i, &child, NULL),
                                  ROBO_OK);
                if (robo_html_element_view_get(&child, &view, NULL) == ROBO_OK &&
                    strcmp(view.name, "p") == 0) {
                    seen_p++;
                }
            }
        }
        ROBO_CHECK_EQ_INT(seen_p, 2);
    }

    (void)robo_html_document_destroy(id, NULL);
    tb_fixture_end();
}

static void test_tb_whitespace_is_preserved_as_text(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t r;
    char buf[32];

    tb_fixture_begin();
    /* Indentation between body children becomes real text nodes. */
    id = tb_doc();
    tb_build(id, "<body>\n  <p>a</p>\n  <p>b</p>\n</body>", &r);
    ROBO_CHECK_EQ_INT(r.element_count, 5); /* html head body p p */
    ROBO_CHECK_EQ_INT(r.text_count, 5);    /* "\n  " a "\n  " b "\n" */
    tb_text_of(id, "body", buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "\n  a\n  b\n");
    (void)robo_html_document_destroy(id, NULL);

    /* Whitespace before <html> is dropped, not stored. */
    id = tb_doc();
    tb_build(id, "\n\n  <p>x</p>", &r);
    ROBO_CHECK_EQ_INT(r.text_count, 1);
    (void)robo_html_document_destroy(id, NULL);

    tb_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_html_tree_builder(void)
{
    robo_test_register("html.tree.model_helpers", test_tb_model_helpers);
    robo_test_register("html.tree.create_misuse", test_tb_create_misuse);
    robo_test_register("html.tree.begin_misuse", test_tb_begin_misuse);
    robo_test_register("html.tree.empty_document", test_tb_empty_document);
    robo_test_register("html.tree.basic_document", test_tb_basic_document);
    robo_test_register("html.tree.implied_structure",
                       test_tb_implied_structural_elements);
    robo_test_register("html.tree.text_before_html", test_tb_text_before_html);
    robo_test_register("html.tree.nesting",
                       test_tb_element_insertion_and_nesting);
    robo_test_register("html.tree.siblings", test_tb_siblings_and_child_order);
    robo_test_register("html.tree.text_merge", test_tb_text_nodes_and_merging);
    robo_test_register("html.tree.comments", test_tb_comments);
    robo_test_register("html.tree.doctype", test_tb_doctype);
    robo_test_register("html.tree.attributes", test_tb_attributes);
    robo_test_register("html.tree.void_elements",
                       test_tb_void_and_self_closing_elements);
    robo_test_register("html.tree.tag_matching",
                       test_tb_start_end_tag_matching);
    robo_test_register("html.tree.unexpected_end_tags",
                       test_tb_unexpected_end_tags);
    robo_test_register("html.tree.unclosed_at_eof",
                       test_tb_unclosed_elements_at_eof);
    robo_test_register("html.tree.duplicate_structural",
                       test_tb_duplicate_structural_tags);
    robo_test_register("html.tree.title_rcdata", test_tb_title_rcdata);
    robo_test_register("html.tree.div_span_button",
                       test_tb_div_span_button_structures);
    robo_test_register("html.tree.anchor_image_form",
                       test_tb_anchor_image_form_input);
    robo_test_register("html.tree.open_element_stack",
                       test_tb_open_element_stack);
    robo_test_register("html.tree.insertion_modes",
                       test_tb_insertion_mode_transitions);
    robo_test_register("html.tree.ownership",
                       test_tb_ownership_and_stale_handles);
    robo_test_register("html.tree.teardown", test_tb_document_teardown);
    robo_test_register("html.tree.recovery", test_tb_malformed_recovery);
    robo_test_register("html.tree.large_tree", test_tb_large_nested_tree);
    robo_test_register("html.tree.node_ceiling", test_tb_node_ceiling_truncates);
    robo_test_register("html.tree.unicode", test_tb_unicode_text);
    robo_test_register("html.tree.dump", test_tb_tree_dump);
    robo_test_register("html.tree.tokenizer_integration",
                       test_tb_tokenizer_integration);
    robo_test_register("html.tree.diagnostics", test_tb_diagnostics_for_recovery);
    robo_test_register("html.tree.demo_document", test_tb_demo_document_tree);
    robo_test_register("html.tree.whitespace", test_tb_whitespace_is_preserved_as_text);
}
