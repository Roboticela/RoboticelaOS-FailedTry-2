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

/* ------------------------------------------------------------------ */
/* Node payload arena (Phase 8.3)                                      */
/* ------------------------------------------------------------------ */

/**
 * Ensure @p extra more bytes fit at the arena tail.
 * Growth is geometric (256 B, then doubling) so building a document costs
 * O(nodes) allocations rather than one per node.
 */
static robo_result_t arena_reserve(robo_html_document_slot_t *slot,
                                   size_t extra,
                                   robo_html_diag_t *diag)
{
    size_t need;
    size_t next;
    char *grown;

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "slot is NULL");
    }
    if (extra > SIZE_MAX - slot->payload_used) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_OUT_OF_MEMORY,
                              "payload arena size overflow");
    }
    need = slot->payload_used + extra;
    if (need <= slot->payload_capacity) {
        return ROBO_OK;
    }
    next = (slot->payload_capacity == 0u) ? 256u : slot->payload_capacity;
    while (next < need) {
        if (next > (SIZE_MAX / 2u)) {
            next = need;
            break;
        }
        next *= 2u;
    }
    grown = (char *)robo_realloc(slot->payload, next);
    if (!grown) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_OUT_OF_MEMORY,
                              "payload arena allocation failed");
    }
    slot->payload = grown;
    slot->payload_capacity = next;
    return ROBO_OK;
}

robo_result_t robo_html_document_arena_alloc(robo_html_document_slot_t *slot,
                                             size_t size,
                                             uint32_t *out_offset,
                                             void **out_ptr,
                                             robo_html_diag_t *diag)
{
    if (!slot || !out_offset) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "arena args are NULL");
    }
    if (size == 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "zero-size payload");
    }
    if (size > 0xFFFFFFFFu) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_SUPPORTED,
                              "payload exceeds one addressable range");
    }
    if (arena_reserve(slot, size, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }
    *out_offset = (uint32_t)slot->payload_used;
    if (out_ptr) {
        *out_ptr = slot->payload + slot->payload_used;
    }
    slot->payload_used += size;
    return ROBO_OK;
}

robo_result_t robo_html_document_arena_extend_tail(
    robo_html_document_slot_t *slot,
    uint32_t offset,
    size_t old_size,
    size_t extra,
    void **out_ptr,
    robo_html_diag_t *diag)
{
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "slot is NULL");
    }
    if ((size_t)offset + old_size != slot->payload_used) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_SUPPORTED,
                              "only the arena tail can grow");
    }
    if (arena_reserve(slot, extra, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }
    if (out_ptr) {
        *out_ptr = slot->payload + offset;
    }
    slot->payload_used += extra;
    return ROBO_OK;
}

const void *robo_html_document_arena_at(const robo_html_document_slot_t *slot,
                                        uint32_t offset,
                                        uint32_t length)
{
    if (!slot || !slot->payload) {
        return NULL;
    }
    if ((size_t)offset + length > slot->payload_used) {
        return NULL;
    }
    return slot->payload + offset;
}

void robo_html_document_arena_free(robo_html_document_slot_t *slot)
{
    if (!slot) {
        return;
    }
    if (slot->payload) {
        robo_free(slot->payload);
    }
    slot->payload = NULL;
    slot->payload_used = 0u;
    slot->payload_capacity = 0u;
}

robo_result_t robo_html_document_clear_tree(robo_html_document_slot_t *slot,
                                            robo_html_diag_t *diag)
{
    int have_root = 0;
    uint32_t next_generation;

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_INVALID_ARGUMENT, "slot is NULL");
    }
    if (!robo_html_document_lifecycle_allows_content(slot->lifecycle)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_ERROR,
                              "lifecycle does not allow clearing the tree");
    }

    /*
     * Everything the tree owns is the arena: element names, attributes,
     * text, comments and DOCTYPE strings all live in one bump-allocated
     * block, so releasing it releases the whole tree's content at once and
     * a half-built tree cannot leak.
     */
    robo_html_document_arena_free(slot);

    /* Is the document's own root node still usable? */
    if (robo_html_node_handle_is_wellformed(&slot->root)) {
        const robo_html_node_entry_t *e =
            robo_html_node_entry_get(slot->id, slot->root.node_id);

        if (e && e->generation == slot->root.generation &&
            e->kind == ROBO_HTML_NODE_KIND_DOCUMENT) {
            have_root = 1;
        }
    }

    /*
     * Reset the registry while keeping the *generation* cursor moving:
     * generations are what make a handle from the discarded tree detectable,
     * so they must never repeat for a reused slot.
     *
     * Node ids, by contrast, restart dense. Identity for a node is
     * (document, id, generation) and the generation is the guard, so
     * restarting ids costs nothing in safety while keeping id == slot + 1.
     * That correspondence is what keeps registration and lookup linear
     * however many times a document is re-parsed: with ids that only ever
     * grow, slot lookup would degrade to a full scan on every access after
     * the first rebuild.
     */
    next_generation = slot->nodes.next_generation;
    robo_html_node_registry_init(&slot->nodes);
    slot->nodes.next_generation = next_generation;

    if (!have_root) {
        /* No usable root (never created, or already torn down): build one. */
        robo_html_node_handle_t root;
        robo_result_t r;

        robo_html_node_handle_clear(&slot->root);
        slot->finalized = 0;
        r = robo_html_document_register_node(
            slot->id, ROBO_HTML_NODE_KIND_DOCUMENT,
            ROBO_HTML_NODE_ORIGIN_DOCUMENT_ROOT, &root, diag);
        if (r != ROBO_OK) {
            return r;
        }
        slot->root = root;
    } else {
        /*
         * Keep the root's document node, at slot 0 so ids stay dense, with a
         * fresh generation so handles to the *old* root are stale rather
         * than silently naming the new tree's root.
         */
        robo_html_node_entry_t *re = &slot->nodes.slots[0];

        re->in_use = 1;
        re->id = 1u;
        /*
         * The root keeps its own generation: it is the same logical node
         * across the clear (the document node), so a holder's root handle
         * must survive. The cursor has already moved past it, and it only
         * ever moves forward, so no later node can collide with it.
         */
        re->generation = slot->root.generation;
        re->kind = ROBO_HTML_NODE_KIND_DOCUMENT;
        re->parent = ROBO_HTML_NODE_ID_NONE;
        re->first_child = ROBO_HTML_NODE_ID_NONE;
        re->last_child = ROBO_HTML_NODE_ID_NONE;
        re->prev_sibling = ROBO_HTML_NODE_ID_NONE;
        re->next_sibling = ROBO_HTML_NODE_ID_NONE;
        re->payload_offset = 0u;
        re->payload_length = 0u;
        re->child_count = 0u;
        re->flags = 0u;
        slot->nodes.used = 1u;
        slot->nodes.next_id = 2u;
        slot->root.document_id = slot->id;
        slot->root.node_id = 1u;
        slot->root.generation = re->generation;
    }

    /* A new tree is about to exist, so construction is open again. */
    slot->finalized = 0;
    robo_log_debug("robo_html: doc=%llu tree cleared (root preserved=%d)",
                   (unsigned long long)slot->id, have_root);
    return ROBO_OK;
}

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
    slot->finalized = 0;

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
    /*
     * A newly attached source is about to be parsed into this document, so
     * construction reopens. Without this a doc that had been finalized and
     * then given a replacement source would sit in LOADING with its
     * construction API closed — a state no caller could interpret.
     */
    slot->finalized = 0;
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
    /* A document is empty while it holds nothing but its own root node. */
    return slot->nodes.used <= 1u;
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
    if (slot->finalized) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TREE, ROBO_ERROR,
                              "document tree is finalized");
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
    slot->finalized = 0;
    /* Payloads belong to the document, so the tree dies with it. */
    robo_html_document_arena_free(slot);
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
