/*
 * RoboHTML Phase 8.1 boundaries suite:
 * node identity edges, mutation hooks, client/render hooks,
 * resource boundary, DOM capability stubs, element name policy.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLClient.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLResource.h"
#include "RoboTest.h"

#include <string.h>

static void html_hooks_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void html_hooks_fixture_end(void)
{
    robo_html_shutdown();
}

/* --- node identity edges ------------------------------------------------ */

static void test_html_node_identity_edges(void)
{
    robo_html_node_handle_t h;

    robo_html_node_handle_clear(&h);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&h));
    ROBO_CHECK(!robo_html_node_handle_is_stale(&h)); /* ill-formed: not stale */
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&h, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* Clear is safe on NULL. */
    robo_html_node_handle_clear(NULL);

    /* A handle against a non-existent document is stale. */
    h.document_id = 0xDEADu;
    h.node_id = 1u;
    h.generation = 1u;
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&h));
    html_hooks_fixture_begin();
    ROBO_CHECK(robo_html_node_handle_is_stale(&h));
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&h, NULL), ROBO_NOT_FOUND);

    /* Zero generation is ill-formed even with ids set. */
    h.document_id = 1u;
    h.node_id = 1u;
    h.generation = 0u;
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&h));
    html_hooks_fixture_end();
}

/* --- mutation hooks ------------------------------------------------------ */

typedef struct mut_record {
    int node_registered;
    int lifecycle;
    robo_html_document_lifecycle_t last_life;
    robo_html_document_id_t last_doc;
} mut_record_t;

static mut_record_t g_mut;

static void mut_hook(const robo_html_mutation_event_t *event, void *userdata)
{
    mut_record_t *r = (mut_record_t *)userdata;
    if (!r || !event) {
        return;
    }
    if (event->kind == ROBO_HTML_MUTATION_NODE_REGISTERED) {
        r->node_registered++;
    } else if (event->kind == ROBO_HTML_MUTATION_DOCUMENT_LIFECYCLE) {
        r->lifecycle++;
        r->last_life = event->lifecycle;
    }
    r->last_doc = event->document_id;
}

static void test_html_mutation_hooks(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_node_handle_t h;

    html_hooks_fixture_begin();
    memset(&g_mut, 0, sizeof(g_mut));

    ROBO_CHECK_EQ_INT(robo_html_document_set_mutation_hook(
                          ROBO_HTML_DOCUMENT_ID_NONE, mut_hook, &g_mut, NULL),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_set_mutation_hook(id, mut_hook, &g_mut, NULL),
        ROBO_OK);
    /* Over-install refused. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_set_mutation_hook(id, mut_hook, &g_mut, NULL),
        ROBO_ALREADY_EXISTS);
    /* Clear then re-install. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_set_mutation_hook(id, NULL, NULL, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_set_mutation_hook(id, mut_hook, &g_mut, NULL),
        ROBO_OK);

    /* Registration after install fires the hook. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_register_node(id, ROBO_HTML_NODE_KIND_ELEMENT,
                                         ROBO_HTML_NODE_ORIGIN_DOM, &h, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(g_mut.node_registered, 1);
    ROBO_CHECK_EQ_INT(g_mut.last_doc, id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    html_hooks_fixture_end();
}

/* --- client/render hooks -------------------------------------------------- */

typedef struct client_record {
    int lifecycle_events;
    int invalidations;
    robo_html_invalidation_reason_t last_reason;
    robo_html_document_lifecycle_t last_from;
    robo_html_document_lifecycle_t last_to;
} client_record_t;

static client_record_t g_client_rec;

static void client_lifecycle(robo_html_document_id_t document_id,
                             robo_html_document_lifecycle_t from,
                             robo_html_document_lifecycle_t to,
                             void *userdata)
{
    client_record_t *r = (client_record_t *)userdata;
    (void)document_id;
    if (r) {
        r->lifecycle_events++;
        r->last_from = from;
        r->last_to = to;
    }
}

static void client_invalidate(robo_html_document_id_t document_id,
                              robo_html_invalidation_reason_t reason,
                              void *userdata)
{
    client_record_t *r = (client_record_t *)userdata;
    (void)document_id;
    if (r) {
        r->invalidations++;
        r->last_reason = reason;
    }
}

static void test_html_client_hooks(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_client_hooks_t hooks;
    robo_html_source_t src;

    html_hooks_fixture_begin();
    memset(&g_client_rec, 0, sizeof(g_client_rec));

    ROBO_CHECK_EQ_INT(robo_html_document_set_client(
                          ROBO_HTML_DOCUMENT_ID_NONE, &hooks, NULL, NULL),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(!robo_html_document_has_client(id));

    memset(&hooks, 0, sizeof(hooks));
    hooks.on_document_lifecycle = client_lifecycle;
    hooks.on_invalidate = client_invalidate;
    ROBO_CHECK_EQ_INT(robo_html_document_set_client(id, &hooks, &g_client_rec,
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_document_has_client(id));
    /* Over-install refused. */
    ROBO_CHECK_EQ_INT(robo_html_document_set_client(id, &hooks, NULL, NULL),
                      ROBO_ALREADY_EXISTS);

    /* Lifecycle + invalidation fire on source attach. */
    ROBO_CHECK_EQ_INT(robo_html_source_memory(&src, (const uint8_t *)"x", 1u,
                                              NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, NULL),
                      ROBO_OK);
    ROBO_CHECK(g_client_rec.lifecycle_events >= 1);
    ROBO_CHECK_EQ_INT(g_client_rec.last_from,
                      ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);
    ROBO_CHECK_EQ_INT(g_client_rec.last_to,
                      ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK_EQ_INT(g_client_rec.invalidations, 1);
    ROBO_CHECK_EQ_INT(g_client_rec.last_reason, ROBO_HTML_INVALIDATION_SOURCE);

    /* Clearing hooks stops notifications. */
    ROBO_CHECK_EQ_INT(robo_html_document_set_client(id, NULL, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK(!robo_html_document_has_client(id));
    {
        int before = g_client_rec.lifecycle_events;
        ROBO_CHECK_EQ_INT(robo_html_document_detach_source(id, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(g_client_rec.lifecycle_events, before);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    html_hooks_fixture_end();
}

/* --- resource boundary ---------------------------------------------------- */

static void test_html_resource_boundary(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_load_phase_t phase;
    robo_html_source_t src;
    robo_html_document_lifecycle_t life;

    html_hooks_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_resource_get_phase(
                          ROBO_HTML_DOCUMENT_ID_NONE, &phase),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(robo_html_load_phase_is_known(ROBO_HTML_LOAD_PHASE_ACQUIRING));
    ROBO_CHECK(!robo_html_load_phase_is_known((robo_html_load_phase_t)31));
    ROBO_CHECK_STR_EQ(robo_html_load_phase_name(ROBO_HTML_LOAD_PHASE_PARSING),
                      "parsing");

    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);

    /* Begin delegated acquisition; direct attach is refused while open. */
    ROBO_CHECK_EQ_INT(robo_html_resource_begin(id, NULL, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_resource_get_phase(id, &phase), ROBO_OK);
    ROBO_CHECK_EQ_INT(phase, ROBO_HTML_LOAD_PHASE_ACQUIRING);
    ROBO_CHECK_EQ_INT(robo_html_resource_begin(id, NULL, NULL), ROBO_BUSY);

    ROBO_CHECK_EQ_INT(robo_html_source_empty(&src, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, NULL),
                      ROBO_BUSY);

    /* Cancel returns to CREATED, attach now allowed. */
    ROBO_CHECK_EQ_INT(robo_html_resource_cancel(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_resource_get_phase(id, &phase), ROBO_OK);
    ROBO_CHECK_EQ_INT(phase, ROBO_HTML_LOAD_PHASE_NONE);
    ROBO_CHECK_EQ_INT(robo_html_resource_cancel(id, NULL), ROBO_OK); /* idempotent */

    /* Successful end also releases the slot. */
    ROBO_CHECK_EQ_INT(robo_html_resource_begin(id, NULL, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_resource_end(id, 1, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_resource_get_phase(id, &phase), ROBO_OK);
    ROBO_CHECK_EQ_INT(phase, ROBO_HTML_LOAD_PHASE_NONE);
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    /* An empty source settles immediately: nothing to parse, nothing out. */
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);

    /* Failed end marks the document FAILED (sticky). */
    {
        robo_html_document_id_t id2 = ROBO_HTML_DOCUMENT_ID_NONE;
        ROBO_CHECK_EQ_INT(robo_html_document_create(&id2, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_resource_begin(id2, NULL, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_resource_end(id2, 0, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id2, &life),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED);
        /* Failed documents refuse source attachment but support destroy. */
        ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id2, &src, NULL),
                          ROBO_ERROR);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id2, NULL), ROBO_OK);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    html_hooks_fixture_end();
}

/* --- DOM capability stubs -------------------------------------------------- */

static void test_html_dom_capabilities(void)
{
    robo_html_dom_capabilities_t caps;

    robo_html_dom_capabilities_init(&caps);
    ROBO_CHECK_EQ_INT(caps.dom_nodes, 0);
    ROBO_CHECK_EQ_INT(caps.attributes, 0);
    ROBO_CHECK_EQ_INT(caps.mutation, 0);
    ROBO_CHECK_EQ_INT(caps.element_tags, 0);
    ROBO_CHECK_EQ_INT(caps.images, 0);
    ROBO_CHECK_EQ_INT(caps.events, 0);
    ROBO_CHECK_EQ_INT(caps.tree_building, 0);
    ROBO_CHECK_EQ_INT(robo_html_dom_query_capabilities(NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_dom_query_capabilities(&caps), ROBO_OK);
    /*
     * Tree construction (8.3) and attribute storage (8.6) are available, and
     * since 8.9 so is DOM mutation — which is what this file's hook
     * foundation was built for — and since 8.11 so is element type identity.
     * Since 8.12 so is the image resource model: `images` means an `<img>`
     * holds real decoded pixels reachable through a resolver, not that the
     * runtime can reach a network. The node DOM and events are not. Each flag
     * reports the model its own phase landed, not a compatible DOM surface:
     * `attributes` is the attribute model rather than a NamedNodeMap,
     * `element_tags` is the tag vocabulary rather than a style or selector
     * engine, and `images` is a resource model rather than an `HTMLImageElement`.
     */
    ROBO_CHECK_EQ_INT(caps.dom_nodes, 0);
    ROBO_CHECK_EQ_INT(caps.attributes, 1);
    ROBO_CHECK_EQ_INT(caps.mutation, 1);
    ROBO_CHECK_EQ_INT(caps.element_tags, 1);
    ROBO_CHECK_EQ_INT(caps.images, 1);
    ROBO_CHECK_EQ_INT(caps.events, 0);
    ROBO_CHECK_EQ_INT(caps.tree_building, 1);

    ROBO_CHECK(robo_html_node_origin_is_known(ROBO_HTML_NODE_ORIGIN_PARSER));
    ROBO_CHECK(!robo_html_node_origin_is_known((robo_html_node_origin_t)19));
    ROBO_CHECK_STR_EQ(robo_html_node_origin_name(ROBO_HTML_NODE_ORIGIN_DOM),
                      "dom");
    ROBO_CHECK(robo_html_mutation_kind_is_known(
        ROBO_HTML_MUTATION_NODE_REGISTERED));
    ROBO_CHECK(!robo_html_mutation_kind_is_known(
        (robo_html_mutation_kind_t)8));
}

/* --- element/attribute boundary ------------------------------------------- */

static void test_html_element_name_policy(void)
{
    char out[ROBO_HTML_NAME_MAX];

    ROBO_CHECK_EQ_INT(robo_html_element_name_validate("div", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_name_validate("my-widget_2", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_name_validate(NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_name_validate("", NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_name_validate("a b", NULL),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_html_element_name_validate("<x>", NULL),
                      ROBO_PARSE_ERROR);
    {
        char huge[ROBO_HTML_NAME_MAX + 4u];
        memset(huge, 'a', sizeof(huge) - 1u);
        huge[sizeof(huge) - 1u] = '\0';
        ROBO_CHECK_EQ_INT(robo_html_element_name_validate(huge, NULL),
                          ROBO_NOT_SUPPORTED);
    }

    /* Normalization lowercases ASCII and copies. */
    ROBO_CHECK_EQ_INT(robo_html_element_name_normalize("DiV", out, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(out, "div");
    ROBO_CHECK_EQ_INT(robo_html_element_name_normalize("My-App", out, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(out, "my-app");
    ROBO_CHECK_EQ_INT(robo_html_element_name_normalize(NULL, out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_name_normalize("div", NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_html_element_boundary_init(NULL); /* safe no-op */
    {
        robo_html_element_boundary_t b;
        robo_html_element_boundary_init(&b);
        ROBO_CHECK_EQ_INT(b.reserved, 0u);
    }
}

void robo_test_register_html_hooks(void)
{
    robo_test_register("html.node.identity", test_html_node_identity_edges);
    robo_test_register("html.mutation.hooks", test_html_mutation_hooks);
    robo_test_register("html.client.hooks", test_html_client_hooks);
    robo_test_register("html.resource.boundary", test_html_resource_boundary);
    robo_test_register("html.dom.capabilities", test_html_dom_capabilities);
    robo_test_register("html.element.name_policy",
                       test_html_element_name_policy);
}
