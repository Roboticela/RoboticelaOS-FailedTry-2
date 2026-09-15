/*
 * RoboHTML document (Phase 8.1) — ownership root: creation, source
 * attachment, node registration, lifecycle transitions, teardown.
 */

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTMLInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

static robo_html_document_id_t g_next_document_id = 1u;

robo_result_t robo_html_document_apply_transition(
    robo_html_document_slot_t *slot,
    robo_html_document_lifecycle_t to)
{
    robo_html_document_lifecycle_t from;

    if (!slot) {
        return ROBO_INVALID_ARGUMENT;
    }
    from = slot->lifecycle;
    if (!robo_html_document_lifecycle_can_transition(from, to)) {
        robo_log_error("robo_html: illegal lifecycle %s -> %s",
                       robo_html_document_lifecycle_name(from),
                       robo_html_document_lifecycle_name(to));
        return ROBO_INVALID_ARGUMENT;
    }
    if (from == to) {
        return ROBO_OK;
    }
    slot->previous_lifecycle = from;
    slot->lifecycle = to;
    robo_html_emit_lifecycle(slot, from, to);
    if (to == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING) {
        robo_html_emit_invalidate(slot, ROBO_HTML_INVALIDATION_SOURCE);
    }
    robo_log_debug("robo_html: doc=%llu %s -> %s",
                   (unsigned long long)slot->id,
                   robo_html_document_lifecycle_name(from),
                   robo_html_document_lifecycle_name(to));
    return ROBO_OK;
}

robo_result_t robo_html_document_create(robo_html_document_id_t *out_id,
                                        robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_document_id_t id;

    if (!out_id) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_INVALID_ARGUMENT, "out_id is NULL");
    }
    *out_id = ROBO_HTML_DOCUMENT_ID_NONE;
    if (!robo_html_is_initialized()) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_INIT_ERROR,
                              "RoboHTML module not initialized");
    }
    slot = robo_html_document_slot_alloc();
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_OUT_OF_MEMORY,
                              "document table exhausted");
    }
    memset(slot, 0, sizeof(*slot));
    id = g_next_document_id++;
    slot->in_use = 1;
    slot->id = id;
    slot->lifecycle = ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED;
    slot->previous_lifecycle = ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED;
    slot->fail_domain = ROBO_HTML_DOMAIN_NONE;
    slot->fail_message = NULL;
    slot->source_kind = ROBO_HTML_SOURCE_KIND_NONE;
    slot->source_length = 0u;
    slot->source_name[0] = '\0';
    robo_html_encoding_state_init(&slot->encoding);
    robo_html_node_registry_init(&slot->nodes);
    slot->leased_parser = NULL;
    memset(&slot->client, 0, sizeof(slot->client));
    slot->client_userdata = NULL;
    slot->mutation_hook = NULL;
    slot->mutation_userdata = NULL;
    slot->load_phase = ROBO_HTML_LOAD_PHASE_NONE;
    slot->loader = NULL;

    /* Enter CREATED and register the document node (future tree root). */
    slot->lifecycle = ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED;
    {
        robo_html_node_handle_t root;
        if (robo_html_document_register_node(id, ROBO_HTML_NODE_KIND_DOCUMENT,
                                             ROBO_HTML_NODE_ORIGIN_DOCUMENT_ROOT,
                                             &root, NULL) != ROBO_OK) {
            slot->in_use = 0;
            slot->id = ROBO_HTML_DOCUMENT_ID_NONE;
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                                  ROBO_ERROR, "root node registration failed");
        }
        slot->root = root;
    }
    robo_log_info("robo_html: document created id=%llu",
                  (unsigned long long)id);
    (void)robo_html_diag_set(diag, ROBO_OK, ROBO_HTML_DOMAIN_DOCUMENT,
                             "document created", __FILE__, __LINE__);
    *out_id = id;
    return ROBO_OK;
}

robo_result_t robo_html_document_attach_source(
    robo_html_document_id_t document_id,
    const robo_html_source_t *src,
    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    if (!src) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_INVALID_ARGUMENT, "source is NULL");
    }
    if (slot->lifecycle == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_LIFECYCLE, ROBO_BUSY,
                              "a source is already attached");
    }
    if (!robo_html_document_lifecycle_allows_source_attach(slot->lifecycle)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_LIFECYCLE, ROBO_ERROR,
                              "lifecycle does not allow source attach");
    }
    if (slot->load_phase == ROBO_HTML_LOAD_PHASE_ACQUIRING) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_RESOURCE, ROBO_BUSY,
                              "resource acquisition outstanding");
    }
    /* Source metadata is copied; bytes stay application-owned. */
    slot->source_kind = src->kind;
    slot->source_length = robo_html_source_length(src);
    {
        size_t n = 0u;
        while (src->name[n] != '\0' && n < sizeof(slot->source_name) - 1u) {
            slot->source_name[n] = src->name[n];
            n++;
        }
        slot->source_name[n] = '\0';
    }
    (void)robo_html_document_apply_transition(slot,
                                              ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    /* Empty source completes immediately (8.1: nothing to parse). */
    if (slot->source_length == 0u) {
        (void)robo_html_document_apply_transition(
            slot, ROBO_HTML_DOCUMENT_LIFECYCLE_READY);
    }
    (void)robo_html_diag_set(diag, ROBO_OK, ROBO_HTML_DOMAIN_SOURCE,
                             "source attached", __FILE__, __LINE__);
    return ROBO_OK;
}

robo_result_t robo_html_document_detach_source(
    robo_html_document_id_t document_id,
    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    if (slot->lifecycle != ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING) {
        return ROBO_OK;
    }
    slot->source_kind = ROBO_HTML_SOURCE_KIND_NONE;
    slot->source_length = 0u;
    slot->source_name[0] = '\0';
    robo_html_encoding_state_init(&slot->encoding);
    (void)robo_html_document_apply_transition(slot,
                                              ROBO_HTML_DOCUMENT_LIFECYCLE_READY);
    (void)robo_html_diag_set(diag, ROBO_OK, ROBO_HTML_DOMAIN_SOURCE,
                             "source detached", __FILE__, __LINE__);
    return ROBO_OK;
}

robo_result_t robo_html_document_mark_ready(
    robo_html_document_id_t document_id,
    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    if (slot->lifecycle != ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_LIFECYCLE,
                              ROBO_INVALID_ARGUMENT,
                              "only a loading document can become ready");
    }
    return robo_html_document_apply_transition(
        slot, ROBO_HTML_DOCUMENT_LIFECYCLE_READY);
}

robo_result_t robo_html_document_mark_failed(
    robo_html_document_id_t document_id,
    robo_html_diagnostic_domain_t domain,
    const char *message,
    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);
    robo_result_t r;

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    if (slot->lifecycle == ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED) {
        return ROBO_OK; /* Sticky: first failure wins. */
    }
    if (slot->lifecycle != ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED &&
        slot->lifecycle != ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING &&
        slot->lifecycle != ROBO_HTML_DOCUMENT_LIFECYCLE_READY) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_LIFECYCLE,
                              ROBO_INVALID_ARGUMENT,
                              "lifecycle cannot fail");
    }
    slot->fail_domain = domain;
    slot->fail_message = message;
    r = robo_html_document_apply_transition(
        slot, ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED);
    if (r == ROBO_OK) {
        robo_log_warn("robo_html: doc=%llu failed domain=%s msg=%s",
                      (unsigned long long)document_id,
                      robo_html_diagnostic_domain_name(domain),
                      message ? message : "-");
    }
    return r;
}

robo_result_t robo_html_document_get_lifecycle(
    robo_html_document_id_t document_id,
    robo_html_document_lifecycle_t *out_lifecycle)
{
    const robo_html_document_slot_t *slot =
        robo_html_document_slot_find(document_id);

    if (!slot || !out_lifecycle) {
        return ROBO_NOT_FOUND;
    }
    *out_lifecycle = slot->lifecycle;
    return ROBO_OK;
}

robo_result_t robo_html_document_get_encoding_state(
    robo_html_document_id_t document_id,
    robo_html_encoding_state_t *out_state)
{
    const robo_html_document_slot_t *slot =
        robo_html_document_slot_find(document_id);

    if (!slot || !out_state) {
        return ROBO_NOT_FOUND;
    }
    *out_state = slot->encoding;
    return ROBO_OK;
}

robo_result_t robo_html_document_input_encoding_declared(
    robo_html_document_id_t document_id,
    const char *label,
    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    if (!robo_html_document_lifecycle_allows_content(slot->lifecycle)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ENCODING, ROBO_ERROR,
                              "lifecycle does not allow encoding update");
    }
    return robo_html_encoding_declare(&slot->encoding, label, diag);
}

int robo_html_document_is_empty(robo_html_document_id_t document_id)
{
    const robo_html_document_slot_t *slot =
        robo_html_document_slot_find(document_id);

    if (!slot) {
        return 0;
    }
    /*
     * Phase 8.1: documents carry only the root node; the document is empty
     * until the tree builder (8.3) adds children.
     */
    return 1;
}

const char *robo_html_document_source_name(robo_html_document_id_t document_id)
{
    const robo_html_document_slot_t *slot =
        robo_html_document_slot_find(document_id);

    if (!slot) {
        return NULL;
    }
    return slot->source_name;
}

size_t robo_html_document_node_count(robo_html_document_id_t document_id)
{
    const robo_html_document_slot_t *slot =
        robo_html_document_slot_find(document_id);

    if (!slot) {
        return 0u;
    }
    return robo_html_node_registry_count(&slot->nodes);
}

robo_html_node_handle_t robo_html_document_root(
    robo_html_document_id_t document_id)
{
    const robo_html_document_slot_t *slot =
        robo_html_document_slot_find(document_id);

    if (!slot) {
        robo_html_node_handle_t invalid;
        robo_html_node_handle_clear(&invalid);
        return invalid;
    }
    return slot->root;
}

robo_result_t robo_html_document_register_node(
    robo_html_document_id_t document_id,
    robo_html_node_kind_t kind,
    robo_html_node_origin_t origin,
    robo_html_node_handle_t *out_handle,
    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);
    robo_html_node_entry_t *e = NULL;
    robo_html_node_handle_t h;
    size_t i;

    (void)origin; /* Diagnostic metadata only in 8.1. */
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    if (!robo_html_node_kind_is_known(kind)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "unknown node kind");
    }
    if (!robo_html_document_lifecycle_allows_content(slot->lifecycle)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_ERROR,
                              "lifecycle does not allow node registration");
    }
    if (slot->nodes.used >= ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_OUT_OF_MEMORY,
                              "node registry exhausted");
    }
    for (i = 0u; i < ROBO_HTML_MAX_NODES_PER_DOCUMENT; i++) {
        if (!slot->nodes.slots[i].in_use) {
            e = &slot->nodes.slots[i];
            break;
        }
    }
    if (!e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_OUT_OF_MEMORY, "node registry exhausted");
    }
    h.document_id = document_id;
    h.node_id = slot->nodes.next_id++;
    h.generation = slot->nodes.next_generation;
    e->in_use = 1;
    e->id = h.node_id;
    e->generation = h.generation;
    e->kind = kind;
    slot->nodes.used++;
    slot->nodes.next_generation++;
    if (out_handle) {
        *out_handle = h;
    }
    {
        robo_html_mutation_event_t ev;
        ev.kind = ROBO_HTML_MUTATION_NODE_REGISTERED;
        ev.document_id = document_id;
        ev.node = h;
        ev.lifecycle = slot->lifecycle;
        robo_html_emit_mutation(&ev, slot);
    }
    return ROBO_OK;
}

robo_result_t robo_html_document_destroy(robo_html_document_id_t document_id,
                                         robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "unknown or already destroyed document");
    }
    /* Release the parser lease first (parser never outlives document). */
    if (slot->leased_parser) {
        (void)robo_html_parser_detach(slot->leased_parser, NULL);
        slot->leased_parser = NULL;
    }
    (void)robo_html_document_apply_transition(
        slot, ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING);
    memset(&slot->client, 0, sizeof(slot->client));
    slot->client_userdata = NULL;
    slot->mutation_hook = NULL;
    slot->mutation_userdata = NULL;
    slot->load_phase = ROBO_HTML_LOAD_PHASE_NONE;
    slot->loader = NULL;
    robo_html_node_registry_init(&slot->nodes);
    robo_html_node_handle_clear(&slot->root);
    slot->lifecycle = ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED;
    slot->in_use = 0;
    slot->id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_log_debug("robo_html: document destroyed id=%llu",
                   (unsigned long long)document_id);
    (void)robo_html_diag_set(diag, ROBO_OK, ROBO_HTML_DOMAIN_DOCUMENT,
                             "document destroyed", __FILE__, __LINE__);
    return ROBO_OK;
}

int robo_html_document_is_live(robo_html_document_id_t document_id)
{
    return robo_html_document_slot_find(document_id) != NULL;
}
