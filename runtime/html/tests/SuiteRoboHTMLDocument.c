/*
 * RoboHTML Phase 8.1 document suite:
 * creation, empty state, source attachment, encoding boundary,
 * node registration, teardown behavior.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLResource.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboTest.h"

#include <string.h>

static robo_html_diag_t g_diag;

static void html_doc_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
    robo_html_diag_reset(&g_diag);
}

static void html_doc_fixture_end(void)
{
    robo_html_shutdown();
}

static void test_html_doc_create_empty(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_document_lifecycle_t life;
    robo_html_node_handle_t root;

    html_doc_fixture_begin();

    ROBO_CHECK_EQ_INT(robo_html_document_create(NULL, &g_diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(g_diag.domain, ROBO_HTML_DOMAIN_DOCUMENT);

    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, &g_diag), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    ROBO_CHECK_EQ_INT(g_diag.code, ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);
    ROBO_CHECK(robo_html_document_is_live(id));
    ROBO_CHECK(robo_html_document_is_empty(id));
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(id), 1u); /* root only */
    ROBO_CHECK_EQ_INT(robo_html_live_document_count(), 1u);

    root = robo_html_document_root(id);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    {
        robo_html_node_kind_t kind = (robo_html_node_kind_t)0;
        ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&root, &kind),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(kind, ROBO_HTML_NODE_KIND_DOCUMENT);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, &g_diag), ROBO_OK);
    html_doc_fixture_end();
}

static void test_html_doc_source_attach(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_document_lifecycle_t life;
    robo_html_source_t src;
    static const uint8_t bytes[] = "<html></html>";

    html_doc_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);

    /* NULL / unknown args. */
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(
                          ROBO_HTML_DOCUMENT_ID_NONE, &src, &g_diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, NULL, &g_diag),
                      ROBO_INVALID_ARGUMENT);

    /*
     * Empty source: settles through INTERACTIVE to COMPLETE, because there
     * is nothing to parse and so nothing can be outstanding. The document
     * stays empty either way.
     */
    ROBO_CHECK_EQ_INT(robo_html_source_empty(&src, &g_diag), ROBO_OK);
    ROBO_CHECK(robo_html_source_is_empty(&src));
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, &g_diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK(robo_html_document_is_complete(id));
    ROBO_CHECK(!robo_html_document_is_interactive(id));
    ROBO_CHECK(robo_html_document_is_settled(id));
    ROBO_CHECK(robo_html_document_is_empty(id));

    /* COMPLETE accepts a replacement source (COMPLETE → LOADING). */
    ROBO_CHECK_EQ_INT(robo_html_source_memory(&src, bytes, sizeof(bytes) - 1u,
                                              &g_diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_source_set_name(&src, "app://main",
                                                &g_diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, &g_diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK_STR_EQ(robo_html_document_source_name(id), "app://main");
    /* Non-empty source stays LOADING until a parse finishes. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, &g_diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE);
    ROBO_CHECK(robo_html_document_is_interactive(id));
    ROBO_CHECK(!robo_html_document_is_complete(id));
    /* INTERACTIVE → INTERACTIVE is refused (nothing changed). */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, &g_diag),
                      ROBO_INVALID_ARGUMENT);
    /* The DOM is live at INTERACTIVE, so completion is allowed. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, &g_diag), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    /* Completion happens once. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, &g_diag),
                      ROBO_INVALID_ARGUMENT);

    /* Detach on a settled document is a no-op success. */
    ROBO_CHECK_EQ_INT(robo_html_document_detach_source(id, &g_diag), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    html_doc_fixture_end();
}

static void test_html_doc_encoding_boundary(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_encoding_state_t st;

    html_doc_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_html_document_get_encoding_state(id, &st),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(st.declared, ROBO_HTML_ENCODING_UNKNOWN);
    ROBO_CHECK_EQ_INT(st.actual, ROBO_HTML_ENCODING_UNKNOWN);
    ROBO_CHECK_EQ_INT(st.status, ROBO_HTML_ENCODING_STATUS_OK);

    /* Known label: declared recorded, actual defaults to UTF-8. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_input_encoding_declared(id, "UTF-8", &g_diag),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_encoding_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.declared, ROBO_HTML_ENCODING_UTF_8);
    ROBO_CHECK_EQ_INT(st.actual, ROBO_HTML_ENCODING_UTF_8);

    /* Case-insensitive label + alias. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_input_encoding_declared(id, "utf8", &g_diag),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_encoding_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.declared, ROBO_HTML_ENCODING_UTF_8);

    /* Valid ASCII but unsupported label: recorded, not fatal. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_input_encoding_declared(id, "klingon-8", &g_diag),
        ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_html_document_get_encoding_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.status, ROBO_HTML_ENCODING_STATUS_UNSUPPORTED_LABEL);
    /* Previous declaration survives an unsupported attempt. */
    ROBO_CHECK_EQ_INT(st.declared, ROBO_HTML_ENCODING_UTF_8);

    /* Non-ASCII label is a boundary failure. */
    {
        const char bad[] = { (char)0xC3, (char)0xA9, 'x', '\0' };
        ROBO_CHECK_EQ_INT(
            robo_html_document_input_encoding_declared(id, bad, &g_diag),
            ROBO_PARSE_ERROR);
        ROBO_CHECK_EQ_INT(g_diag.domain, ROBO_HTML_DOMAIN_ENCODING);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    html_doc_fixture_end();
}

static void test_html_doc_node_registration(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    robo_html_node_handle_t b;
    robo_html_node_kind_t kind;

    html_doc_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    root = robo_html_document_root(id);

    /* Kinds and origins are validated. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_register_node(
            id, (robo_html_node_kind_t)50, ROBO_HTML_NODE_ORIGIN_TEST, &a,
            &g_diag),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(g_diag.domain, ROBO_HTML_DOMAIN_NODE);

    /* Registration hands out unique, resolvable handles. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_register_node(id, ROBO_HTML_NODE_KIND_ELEMENT,
                                         ROBO_HTML_NODE_ORIGIN_TEST, &a,
                                         &g_diag),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_register_node(id, ROBO_HTML_NODE_KIND_TEXT,
                                         ROBO_HTML_NODE_ORIGIN_TEST, &b,
                                         &g_diag),
        ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_equal(&a, &b));
    ROBO_CHECK(!robo_html_node_handle_equal(&a, &root));
    ROBO_CHECK_EQ_INT(a.document_id, id);
    ROBO_CHECK_EQ_INT(b.document_id, id);
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(id), 3u);

    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&a, &kind), ROBO_OK);
    ROBO_CHECK_EQ_INT(kind, ROBO_HTML_NODE_KIND_ELEMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&b, &kind), ROBO_OK);
    ROBO_CHECK_EQ_INT(kind, ROBO_HTML_NODE_KIND_TEXT);

    /* Registering into a destroyed document is refused. */
    {
        robo_html_document_id_t id2 = ROBO_HTML_DOCUMENT_ID_NONE;
        robo_html_node_handle_t h;
        ROBO_CHECK_EQ_INT(robo_html_document_create(&id2, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id2, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_html_document_register_node(id2, ROBO_HTML_NODE_KIND_TEXT,
                                             ROBO_HTML_NODE_ORIGIN_TEST, &h,
                                             &g_diag),
            ROBO_NOT_FOUND);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    html_doc_fixture_end();
}

static void test_html_doc_teardown(void)
{
    robo_html_document_id_t a = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_document_id_t b = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_document_lifecycle_t life;
    robo_html_node_handle_t a_node;

    html_doc_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_create(&b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_register_node(a, ROBO_HTML_NODE_KIND_ELEMENT,
                                         ROBO_HTML_NODE_ORIGIN_TEST, &a_node,
                                         NULL),
        ROBO_OK);

    /* Valid destroy. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(a, &g_diag), ROBO_OK);
    ROBO_CHECK(!robo_html_document_is_live(a));

    /* Repeated teardown is detected, never undefined. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(a, &g_diag), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(a, &life),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(ROBO_HTML_DOCUMENT_ID_NONE,
                                                 &g_diag),
                      ROBO_NOT_FOUND);

    /* Sibling documents are unaffected. */
    ROBO_CHECK(robo_html_document_is_live(b));
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(b), 1u);

    /* Handles of destroyed documents are stale, not resolvable. */
    ROBO_CHECK(robo_html_node_handle_is_stale(&a_node));
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&a_node, NULL),
                      ROBO_NOT_FOUND);

    /* And a *fresh* node from a still-live document stays resolvable. */
    {
        robo_html_node_handle_t b_node;
        ROBO_CHECK_EQ_INT(
            robo_html_document_register_node(b, ROBO_HTML_NODE_KIND_COMMENT,
                                             ROBO_HTML_NODE_ORIGIN_TEST,
                                             &b_node, NULL),
            ROBO_OK);
        ROBO_CHECK(!robo_html_node_handle_is_stale(&b_node));
        ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&b_node, NULL),
                          ROBO_OK);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_live_document_count(), 0u);
    html_doc_fixture_end();
}

static void test_html_doc_identity_monotonic(void)
{
    robo_html_document_id_t ids[ROBO_HTML_MAX_DOCUMENTS];
    int i;
    int ok = 1;

    html_doc_fixture_begin();
    for (i = 0; i < (int)ROBO_HTML_MAX_DOCUMENTS; i++) {
        ids[i] = ROBO_HTML_DOCUMENT_ID_NONE;
        ROBO_CHECK_EQ_INT(robo_html_document_create(&ids[i], NULL), ROBO_OK);
    }
    /* Table exhaustion is a reported error, not a crash. */
    {
        robo_html_document_id_t extra = ROBO_HTML_DOCUMENT_ID_NONE;
        ROBO_CHECK_EQ_INT(robo_html_document_create(&extra, &g_diag),
                          ROBO_OUT_OF_MEMORY);
        ROBO_CHECK_EQ_INT(extra, ROBO_HTML_DOCUMENT_ID_NONE);
    }
    /* All live ids distinct and non-zero. */
    for (i = 0; i < (int)ROBO_HTML_MAX_DOCUMENTS; i++) {
        int j;
        if (ids[i] == ROBO_HTML_DOCUMENT_ID_NONE) {
            ok = 0;
        }
        for (j = 0; j < i; j++) {
            if (ids[i] == ids[j]) {
                ok = 0;
            }
        }
    }
    ROBO_CHECK(ok);
    for (i = 0; i < (int)ROBO_HTML_MAX_DOCUMENTS; i++) {
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(ids[i], NULL), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_html_live_document_count(), 0u);
    html_doc_fixture_end();
}

void robo_test_register_html_document(void)
{
    robo_test_register("html.doc.create_empty", test_html_doc_create_empty);
    robo_test_register("html.doc.source_attach", test_html_doc_source_attach);
    robo_test_register("html.doc.encoding", test_html_doc_encoding_boundary);
    robo_test_register("html.doc.nodes", test_html_doc_node_registration);
    robo_test_register("html.doc.teardown", test_html_doc_teardown);
    robo_test_register("html.doc.identity", test_html_doc_identity_monotonic);
}
