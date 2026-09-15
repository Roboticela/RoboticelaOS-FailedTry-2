/*
 * RoboHTML future-DOM boundary (Phase 8.1) — capabilities, origins, and
 * the mutation hook foundation. No DOM behavior.
 */

#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

int robo_html_node_origin_is_known(robo_html_node_origin_t origin)
{
    switch (origin) {
    case ROBO_HTML_NODE_ORIGIN_NONE:
    case ROBO_HTML_NODE_ORIGIN_DOCUMENT_ROOT:
    case ROBO_HTML_NODE_ORIGIN_PARSER:
    case ROBO_HTML_NODE_ORIGIN_DOM:
    case ROBO_HTML_NODE_ORIGIN_TEST:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_node_origin_name(robo_html_node_origin_t origin)
{
    switch (origin) {
    case ROBO_HTML_NODE_ORIGIN_NONE:
        return "none";
    case ROBO_HTML_NODE_ORIGIN_DOCUMENT_ROOT:
        return "document-root";
    case ROBO_HTML_NODE_ORIGIN_PARSER:
        return "parser";
    case ROBO_HTML_NODE_ORIGIN_DOM:
        return "dom";
    case ROBO_HTML_NODE_ORIGIN_TEST:
        return "test";
    default:
        return "unknown";
    }
}

void robo_html_dom_capabilities_init(robo_html_dom_capabilities_t *caps)
{
    if (!caps) {
        return;
    }
    memset(caps, 0, sizeof(*caps));
}

robo_result_t robo_html_dom_query_capabilities(
    robo_html_dom_capabilities_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_html_dom_capabilities_init(out);
    /*
     * Phase 8.3: the tree builder constructs real document trees, so tree
     * construction is available. Node/element/attribute DOM APIs, mutation
     * and events are still later-phase work.
     */
    out->tree_building = 1;
    return ROBO_OK;
}

int robo_html_mutation_kind_is_known(robo_html_mutation_kind_t kind)
{
    switch (kind) {
    case ROBO_HTML_MUTATION_NONE:
    case ROBO_HTML_MUTATION_NODE_REGISTERED:
    case ROBO_HTML_MUTATION_DOCUMENT_LIFECYCLE:
        return 1;
    default:
        return 0;
    }
}

void robo_html_emit_mutation(const robo_html_mutation_event_t *event,
                             void *userdata)
{
    /* userdata carries the owning document slot (see RoboHTMLInternal.h). */
    robo_html_document_slot_t *slot = (robo_html_document_slot_t *)userdata;

    if (!event || !slot || !slot->mutation_hook) {
        return;
    }
    slot->mutation_hook(event, slot->mutation_userdata);
}

robo_result_t robo_html_document_set_mutation_hook(
    robo_html_document_id_t document_id,
    robo_html_mutation_hook_fn hook,
    void *userdata,
    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    if (hook && slot->mutation_hook) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_ALREADY_EXISTS,
                              "mutation hook already installed; clear first");
    }
    slot->mutation_hook = hook;
    slot->mutation_userdata = userdata;
    return ROBO_OK;
}
