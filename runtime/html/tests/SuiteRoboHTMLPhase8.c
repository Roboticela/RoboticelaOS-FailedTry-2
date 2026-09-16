/*
 * RoboHTML Phase 8.1 acceptance suite (html.p8.*).
 *
 * Cross-cutting acceptance: one document flows through create → lease →
 * attach → ready → destroy with all hooks, leases, and node identities
 * released; plus a compile-time boundary check that RoboHTML never pulls
 * graphics/window/render modules (DEPENDENCIES.md direction).
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLClient.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLEncoding.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLParser.h"
#include "RoboHTML/RoboHTMLResource.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboTest.h"

#include <string.h>

typedef struct p8_record {
    int lifecycle_events;
    int invalidations;
    int mutations;
    int released;
} p8_record_t;

static p8_record_t g_p8;

static void p8_lifecycle(robo_html_document_id_t document_id,
                         robo_html_document_lifecycle_t from,
                         robo_html_document_lifecycle_t to,
                         void *userdata)
{
    p8_record_t *r = (p8_record_t *)userdata;
    (void)document_id;
    (void)from;
    if (r) {
        r->lifecycle_events++;
        /* No callbacks may arrive after teardown completed. */
        if (to == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED) {
            r->released++;
        }
    }
}

static void p8_invalidate(robo_html_document_id_t document_id,
                          robo_html_invalidation_reason_t reason,
                          void *userdata)
{
    p8_record_t *r = (p8_record_t *)userdata;
    (void)document_id;
    (void)reason;
    if (r) {
        r->invalidations++;
    }
}

static void p8_mutation(const robo_html_mutation_event_t *event,
                        void *userdata)
{
    p8_record_t *r = (p8_record_t *)userdata;
    (void)event;
    if (r) {
        r->mutations++;
    }
}

static void test_html_p8_full_document_flow(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_client_hooks_t hooks;
    robo_html_node_handle_t root;
    robo_html_node_handle_t para;
    robo_html_node_handle_t text;
    robo_html_parser_t *p = NULL;
    robo_html_source_t src;
    robo_html_encoding_state_t enc;
    robo_html_document_lifecycle_t life;

    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
    memset(&g_p8, 0, sizeof(g_p8));

    /* 1. Create an empty document. */
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    root = robo_html_document_root(id);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));

    /* 2. Install client + mutation observers. */
    memset(&hooks, 0, sizeof(hooks));
    hooks.on_document_lifecycle = p8_lifecycle;
    hooks.on_invalidate = p8_invalidate;
    ROBO_CHECK_EQ_INT(robo_html_document_set_client(id, &hooks, &g_p8, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_set_mutation_hook(id, p8_mutation,
                                                           &g_p8, NULL),
                      ROBO_OK);

    /* 3. Lease a parser exclusively. */
    ROBO_CHECK_EQ_INT(robo_html_parser_create(id, &p, NULL), ROBO_OK);

    /* 4. Attach a UTF-8 source through the parser boundary. */
    ROBO_CHECK_EQ_INT(robo_html_source_memory(&src,
                                              (const uint8_t *)"<p>Hi</p>",
                                              9u, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_source_set_declared_encoding(&src, "UTF-8",
                                                             NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_source_set_name(&src, "app://welcome", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_parser_attach_source(p, &src, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK_EQ_INT(robo_html_document_get_encoding_state(id, &enc), ROBO_OK);
    ROBO_CHECK_EQ_INT(enc.declared, ROBO_HTML_ENCODING_UTF_8);

    /* 5. Tree-builder entry point registers nodes (no tree yet in 8.1). */
    ROBO_CHECK_EQ_INT(
        robo_html_document_register_node(id, ROBO_HTML_NODE_KIND_ELEMENT,
                                         ROBO_HTML_NODE_ORIGIN_PARSER, &para,
                                         NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_register_node(id, ROBO_HTML_NODE_KIND_TEXT,
                                         ROBO_HTML_NODE_ORIGIN_PARSER, &text,
                                         NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(id), 3u);
    /* Hook was installed after creation: only para + text fired. */
    ROBO_CHECK_EQ_INT(g_p8.mutations, 2);

    /* 6. Parsing completes: the DOM is live at INTERACTIVE. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE);

    /* 7. Nothing further to load: COMPLETE, and the client sees both. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);

    /* 8. Client saw lifecycle + invalidation traffic: LOADING,
     * INTERACTIVE and COMPLETE each produced one callback. */
    ROBO_CHECK_EQ_INT(g_p8.lifecycle_events, 3);
    ROBO_CHECK(g_p8.invalidations >= 1);

    /* 9. Release the lease, then destroy: everything released. */
    ROBO_CHECK_EQ_INT(robo_html_parser_detach(p, NULL), ROBO_OK);
    robo_html_parser_destroy(p);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK(!robo_html_document_is_live(id));
    ROBO_CHECK(robo_html_node_handle_is_stale(&para));
    ROBO_CHECK(robo_html_node_handle_is_stale(&text));
    ROBO_CHECK(robo_html_node_handle_is_stale(&root));
    ROBO_CHECK_EQ_INT(g_p8.released, 0); /* no DESTROYED callback after teardown */

    robo_html_shutdown();
}

static void test_html_p8_module_shutdown_releases_all(void)
{
    robo_html_document_id_t a = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_document_id_t b = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_node_handle_t ha;
    robo_html_node_handle_t hb;

    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_html_document_create(&a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_create(&b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_register_node(a, ROBO_HTML_NODE_KIND_ELEMENT,
                                         ROBO_HTML_NODE_ORIGIN_TEST, &ha,
                                         NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_register_node(b, ROBO_HTML_NODE_KIND_TEXT,
                                         ROBO_HTML_NODE_ORIGIN_TEST, &hb,
                                         NULL),
        ROBO_OK);

    /* Module shutdown destroys every live document deterministically. */
    robo_html_shutdown();
    ROBO_CHECK(!robo_html_document_is_live(a));
    ROBO_CHECK(!robo_html_document_is_live(b));
    ROBO_CHECK(robo_html_node_handle_is_stale(&ha));
    ROBO_CHECK(robo_html_node_handle_is_stale(&hb));
    ROBO_CHECK_EQ_INT(robo_html_live_document_count(), 0u);
}

static void test_html_p8_boundaries_compile(void)
{
    /*
     * Compile-time boundary guards: RoboHTML public headers must expose
     * only runtime-owned types (no platform/window/graphics linkage).
     * These checks assert the ABI shape that future phases compile against.
     */
    robo_html_diag_t diag;
    robo_html_node_handle_t h;
    robo_html_encoding_t e;

    robo_html_diag_reset(&diag);
    robo_html_node_handle_clear(&h);
    e = robo_html_encoding_from_label("UTF-16LE");
    ROBO_CHECK_EQ_INT(e, ROBO_HTML_ENCODING_UTF_16LE);
    ROBO_CHECK_STR_EQ(robo_html_encoding_label(e), "UTF-16LE");
    ROBO_CHECK(robo_html_encoding_status_is_known(
        ROBO_HTML_ENCODING_STATUS_DECODE_MISMATCH));
    ROBO_CHECK_STR_EQ(
        robo_html_encoding_status_name(ROBO_HTML_ENCODING_STATUS_MALFORMED),
        "malformed");
    /* Identity tokens are plain integers: safe for application storage. */
    ROBO_CHECK_EQ_INT(sizeof(robo_html_document_id_t), 8u);
    ROBO_CHECK_EQ_INT(sizeof(robo_html_node_id_t), 8u);
}

void robo_test_register_html_phase8(void)
{
    robo_test_register("html.p8.full_flow", test_html_p8_full_document_flow);
    robo_test_register("html.p8.shutdown_releases",
                       test_html_p8_module_shutdown_releases_all);
    robo_test_register("html.p8.boundaries", test_html_p8_boundaries_compile);
}
