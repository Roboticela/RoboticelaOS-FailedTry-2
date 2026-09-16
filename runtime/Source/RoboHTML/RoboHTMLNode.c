/*
 * RoboHTML node identity, registry, tree links and payload access
 * (Phase 8.1 identity/lifetime, Phase 8.3 tree).
 *
 * The registry lives inside the owning document. Node ids increase
 * monotonically per document and are never reused; generation bumps when
 * a registry slot is recycled, so handles copied before teardown resolve
 * as stale rather than aliasing a new node.
 *
 * Lookup cost: registration walks slots upward from empty and ids are
 * handed out in the same order, so in a normal document id == index + 1.
 * node_lookup therefore probes that slot first (O(1)) and only falls back
 * to a scan when the layout does not match — which keeps tree building
 * linear instead of quadratic without depending on an invariant it cannot
 * check.
 */

#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTMLInternal.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

void robo_html_node_handle_clear(robo_html_node_handle_t *h)
{
    if (!h) {
        return;
    }
    h->document_id = ROBO_HTML_DOCUMENT_ID_NONE;
    h->node_id = ROBO_HTML_NODE_ID_NONE;
    h->generation = 0u;
}

robo_html_document_id_t robo_html_node_handle_document(
    const robo_html_node_handle_t *h)
{
    if (!h) {
        return ROBO_HTML_DOCUMENT_ID_NONE;
    }
    return h->document_id;
}

int robo_html_node_handle_is_wellformed(const robo_html_node_handle_t *h)
{
    if (!h) {
        return 0;
    }
    return h->document_id != ROBO_HTML_DOCUMENT_ID_NONE &&
           h->node_id != ROBO_HTML_NODE_ID_NONE && h->generation != 0u;
}

int robo_html_node_handle_equal(const robo_html_node_handle_t *a,
                                const robo_html_node_handle_t *b)
{
    if (!a || !b) {
        return 0;
    }
    return a->document_id == b->document_id &&
           a->node_id == b->node_id && a->generation == b->generation;
}

robo_html_node_handle_t robo_html_node_handle_make(
    robo_html_document_id_t document_id,
    const robo_html_node_entry_t *entry)
{
    robo_html_node_handle_t h;

    if (!entry) {
        robo_html_node_handle_clear(&h);
        return h;
    }
    h.document_id = document_id;
    h.node_id = entry->id;
    h.generation = entry->generation;
    return h;
}

static const robo_html_node_entry_t *node_lookup(
    robo_html_document_id_t document_id,
    robo_html_node_id_t node_id)
{
    const robo_html_document_slot_t *slot =
        robo_html_document_slot_find(document_id);
    size_t i;

    if (!slot || node_id == ROBO_HTML_NODE_ID_NONE) {
        return NULL;
    }
    /* Hot path: ids are handed out in slot order, so id - 1 is the slot. */
    if (node_id <= (robo_html_node_id_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        const robo_html_node_entry_t *e =
            &slot->nodes.slots[(size_t)(node_id - 1u)];
        if (e->in_use && e->id == node_id) {
            return e;
        }
    }
    /* Cold path: any layout the hot path cannot describe. */
    for (i = 0u; i < ROBO_HTML_MAX_NODES_PER_DOCUMENT; i++) {
        const robo_html_node_entry_t *e = &slot->nodes.slots[i];
        if (e->in_use && e->id == node_id) {
            return e;
        }
    }
    return NULL;
}

const robo_html_node_entry_t *robo_html_node_entry_get(
    robo_html_document_id_t document_id,
    robo_html_node_id_t node_id)
{
    return node_lookup(document_id, node_id);
}

robo_html_node_entry_t *robo_html_node_entry_mut(
    robo_html_document_id_t document_id,
    robo_html_node_id_t node_id)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot || node_id == ROBO_HTML_NODE_ID_NONE) {
        return NULL;
    }
    if (node_id <= (robo_html_node_id_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        robo_html_node_entry_t *e =
            &slot->nodes.slots[(size_t)(node_id - 1u)];
        if (e->in_use && e->id == node_id) {
            return e;
        }
    }
    {
        size_t i;
        for (i = 0u; i < ROBO_HTML_MAX_NODES_PER_DOCUMENT; i++) {
            robo_html_node_entry_t *e = &slot->nodes.slots[i];
            if (e->in_use && e->id == node_id) {
                return e;
            }
        }
    }
    return NULL;
}

robo_result_t robo_html_node_handle_resolve(
    const robo_html_node_handle_t *h,
    robo_html_node_kind_t *out_kind)
{
    const robo_html_node_entry_t *e;

    if (!robo_html_node_handle_is_wellformed(h)) {
        return ROBO_INVALID_ARGUMENT;
    }
    e = node_lookup(h->document_id, h->node_id);
    if (!e) {
        return ROBO_NOT_FOUND;
    }
    if (e->generation != h->generation) {
        return ROBO_NOT_FOUND;
    }
    if (out_kind) {
        *out_kind = e->kind;
    }
    return ROBO_OK;
}

int robo_html_node_handle_is_stale(const robo_html_node_handle_t *h)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (!robo_html_node_handle_is_wellformed(h)) {
        return 0;
    }
    slot = robo_html_document_slot_find(h->document_id);
    if (!slot) {
        /* Well-formed handle pointing at a destroyed/unknown document. */
        return 1;
    }
    e = node_lookup(h->document_id, h->node_id);
    if (!e) {
        return 1;
    }
    /*
     * The generation must be compared, not just the id: node ids restart
     * when a document's tree is cleared, so an id that resolves today can
     * be a *different* node than the one this handle was made for. Without
     * this check a stale handle would report itself as live and disagree
     * with robo_html_node_handle_resolve.
     */
    return e->generation != h->generation;
}

void robo_html_node_registry_init(robo_html_node_registry_t *reg)
{
    if (!reg) {
        return;
    }
    {
        size_t i;
        for (i = 0u; i < ROBO_HTML_MAX_NODES_PER_DOCUMENT; i++) {
            robo_html_node_entry_t *e = &reg->slots[i];
            memset(e, 0, sizeof(*e));
            e->kind = ROBO_HTML_NODE_KIND_DOCUMENT;
            e->parent = ROBO_HTML_NODE_ID_NONE;
            e->first_child = ROBO_HTML_NODE_ID_NONE;
            e->last_child = ROBO_HTML_NODE_ID_NONE;
            e->prev_sibling = ROBO_HTML_NODE_ID_NONE;
            e->next_sibling = ROBO_HTML_NODE_ID_NONE;
        }
    }
    reg->used = 0u;
    reg->next_id = 1u;
    reg->next_generation = 1u;
}

size_t robo_html_node_registry_count(const robo_html_node_registry_t *reg)
{
    if (!reg) {
        return 0u;
    }
    return reg->used;
}

/* ------------------------------------------------------------------ */
/* Internal tree mutation                                              */
/* ------------------------------------------------------------------ */

void robo_html_node_set_payload(robo_html_node_entry_t *entry,
                                uint32_t offset,
                                uint32_t length)
{
    if (!entry) {
        return;
    }
    entry->payload_offset = offset;
    entry->payload_length = length;
}

int robo_html_node_kind_can_hold_children(robo_html_node_kind_t kind)
{
    return kind == ROBO_HTML_NODE_KIND_DOCUMENT ||
           kind == ROBO_HTML_NODE_KIND_ELEMENT ||
           kind == ROBO_HTML_NODE_KIND_FRAGMENT;
}

void robo_html_document_note_tree_change(robo_html_document_slot_t *slot)
{
    if (!slot) {
        return;
    }
    slot->tree_version++;
    if (slot->tree_version == 0u) {
        slot->tree_version = 1u; /* 0 stays reserved for "no document" */
    }
}

uint32_t robo_html_document_tree_version(robo_html_document_id_t document_id)
{
    const robo_html_document_slot_t *slot =
        robo_html_document_slot_find(document_id);

    return slot ? slot->tree_version : 0u;
}

int robo_html_node_is_ancestor_of(robo_html_document_id_t document_id,
                                  robo_html_node_id_t ancestor_id,
                                  robo_html_node_id_t node_id)
{
    const robo_html_node_entry_t *e;
    size_t steps = 0u;

    if (ancestor_id == ROBO_HTML_NODE_ID_NONE) {
        return 0;
    }
    e = robo_html_node_entry_get(document_id, node_id);
    if (!e) {
        return 0;
    }
    for (;;) {
        if (e->id == ancestor_id) {
            return 1;
        }
        if (e->parent == ROBO_HTML_NODE_ID_NONE) {
            return 0;
        }
        /* A parent chain cannot be longer than the registry; a longer one is
         * a corrupted graph, and answering "not an ancestor" is the honest
         * outcome rather than looping. */
        if (steps++ >= (size_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
            return 0;
        }
        e = robo_html_node_entry_get(document_id, e->parent);
        if (!e) {
            return 0;
        }
    }
}

robo_result_t robo_html_node_insert_relative(robo_html_document_id_t document_id,
                                             robo_html_node_id_t parent_id,
                                             robo_html_node_id_t child_id,
                                             robo_html_node_id_t reference_id,
                                             robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_node_entry_t *parent;
    robo_html_node_entry_t *child;
    robo_html_node_entry_t *ref = NULL;
    robo_html_node_entry_t *prev = NULL;

    if (parent_id == child_id) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT,
                              "a node cannot be its own child");
    }
    parent = robo_html_node_entry_mut(document_id, parent_id);
    if (!parent) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "parent node is not live");
    }
    child = robo_html_node_entry_mut(document_id, child_id);
    if (!child) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "child node is not live");
    }
    if (child->kind == ROBO_HTML_NODE_KIND_DOCUMENT) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_SUPPORTED,
                              "the document node cannot become a child");
    }
    if (!robo_html_node_kind_can_hold_children(parent->kind)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_SUPPORTED,
                              "node kind cannot have children");
    }
    if (child->parent != ROBO_HTML_NODE_ID_NONE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_ALREADY_EXISTS,
                              "child already has a parent");
    }
    /*
     * The cycle gate. Inserting a node beneath one of its own descendants
     * would make it reachable from itself, which is not a tree any consumer
     * of it could terminate on — so it is refused before any link is written.
     */
    if (robo_html_node_is_ancestor_of(document_id, child_id, parent_id)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_INVALID_ARGUMENT,
                              "a node cannot be inserted beneath its own "
                              "descendant");
    }

    if (reference_id == ROBO_HTML_NODE_ID_NONE) {
        prev = (parent->last_child == ROBO_HTML_NODE_ID_NONE)
                   ? NULL
                   : robo_html_node_entry_mut(document_id, parent->last_child);
    } else {
        ref = robo_html_node_entry_mut(document_id, reference_id);
        if (!ref) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                                  ROBO_NOT_FOUND,
                                  "reference child is not a live node");
        }
        if (ref->parent != parent_id) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                                  ROBO_NOT_FOUND,
                                  "reference child does not belong to the "
                                  "parent");
        }
        prev = (ref->prev_sibling == ROBO_HTML_NODE_ID_NONE)
                   ? NULL
                   : robo_html_node_entry_mut(document_id, ref->prev_sibling);
    }

    /* Nothing can fail from here: every rejection is behind us. */
    child->parent = parent_id;
    child->next_sibling = (ref != NULL) ? ref->id : ROBO_HTML_NODE_ID_NONE;
    child->prev_sibling = (prev != NULL) ? prev->id : ROBO_HTML_NODE_ID_NONE;
    if (prev != NULL) {
        prev->next_sibling = child->id;
    } else {
        parent->first_child = child->id;
    }
    if (ref != NULL) {
        ref->prev_sibling = child->id;
    } else {
        parent->last_child = child->id;
    }
    parent->child_count++;

    slot = robo_html_document_slot_find(document_id);
    robo_html_document_note_tree_change(slot);
    return ROBO_OK;
}

robo_result_t robo_html_node_link_child(robo_html_document_id_t document_id,
                                        robo_html_node_id_t parent_id,
                                        robo_html_node_id_t child_id,
                                        robo_html_diag_t *diag)
{
    if (!robo_html_document_allows_construction(document_id)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TREE, ROBO_ERROR,
                              "document tree is finalized");
    }
    return robo_html_node_insert_relative(document_id, parent_id, child_id,
                                          ROBO_HTML_NODE_ID_NONE, diag);
}

robo_result_t robo_html_node_unlink(robo_html_document_id_t document_id,
                                    robo_html_node_id_t node_id,
                                    robo_html_diag_t *diag)
{
    robo_html_node_entry_t *node = robo_html_node_entry_mut(document_id, node_id);
    robo_html_node_entry_t *parent;
    robo_html_node_entry_t *prev;
    robo_html_node_entry_t *next;

    if (!node) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "node is not live");
    }
    if (node->parent == ROBO_HTML_NODE_ID_NONE) {
        return ROBO_OK; /* already detached */
    }
    parent = robo_html_node_entry_mut(document_id, node->parent);
    prev = (node->prev_sibling == ROBO_HTML_NODE_ID_NONE)
               ? NULL
               : robo_html_node_entry_mut(document_id, node->prev_sibling);
    next = (node->next_sibling == ROBO_HTML_NODE_ID_NONE)
               ? NULL
               : robo_html_node_entry_mut(document_id, node->next_sibling);

    if (prev != NULL) {
        prev->next_sibling = node->next_sibling;
    }
    if (next != NULL) {
        next->prev_sibling = node->prev_sibling;
    }
    if (parent != NULL) {
        if (parent->first_child == node->id) {
            parent->first_child = node->next_sibling;
        }
        if (parent->last_child == node->id) {
            parent->last_child = node->prev_sibling;
        }
        if (parent->child_count > 0u) {
            parent->child_count--;
        }
        if (parent->first_child == ROBO_HTML_NODE_ID_NONE) {
            parent->last_child = ROBO_HTML_NODE_ID_NONE;
        }
    }
    node->parent = ROBO_HTML_NODE_ID_NONE;
    node->prev_sibling = ROBO_HTML_NODE_ID_NONE;
    node->next_sibling = ROBO_HTML_NODE_ID_NONE;
    robo_html_document_note_tree_change(robo_html_document_slot_find(document_id));
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Read-side accessors                                                 */
/* ------------------------------------------------------------------ */

/**
 * Resolve @p h to its entry, checking the generation.
 * @return entry, or NULL for any failure (callers convert to a result).
 */
static const robo_html_node_entry_t *node_entry_for(
    const robo_html_node_handle_t *h)
{
    const robo_html_node_entry_t *e;

    if (!robo_html_node_handle_is_wellformed(h)) {
        return NULL;
    }
    e = node_lookup(h->document_id, h->node_id);
    if (!e || e->generation != h->generation) {
        return NULL;
    }
    return e;
}

/** Borrowed payload header for @p e, or NULL when it has no payload. */
static const robo_html_payload_header_t *payload_of(
    const robo_html_document_slot_t *slot,
    const robo_html_node_entry_t *e)
{
    const robo_html_payload_header_t *hdr;

    if (!slot || !e || e->payload_length == 0u) {
        return NULL;
    }
    hdr = (const robo_html_payload_header_t *)robo_html_document_arena_at(
        slot, e->payload_offset, e->payload_length);
    if (!hdr || hdr->length + (uint32_t)sizeof(*hdr) != e->payload_length) {
        return NULL;
    }
    return hdr;
}

/** Bytes just after a payload header. */
static const char *payload_bytes(const robo_html_payload_header_t *hdr)
{
    return ((const char *)hdr) + sizeof(*hdr);
}

/**
 * Read the next NUL-terminated string from a payload cursor.
 * @p cursor is advanced past the terminator; the header's declared length
 * bounds the walk so a corrupt payload cannot run off the arena.
 */
static const char *payload_take_string(const char **cursor,
                                       const char *limit)
{
    const char *start = *cursor;
    const char *p = start;

    while (p < limit && *p != '\0') {
        p++;
    }
    if (p >= limit) {
        *cursor = limit;
        return start; /* unterminated: treated as ending at the limit */
    }
    *cursor = p + 1;
    return start;
}

robo_result_t robo_html_node_get_view(const robo_html_node_handle_t *h,
                                      robo_html_node_view_t *out,
                                      robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;
    const robo_html_payload_header_t *hdr;

    if (!h || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "handle or out is NULL");
    }
    memset(out, 0, sizeof(*out));
    slot = robo_html_document_slot_find(h->document_id);
    e = node_entry_for(h);
    if (!slot || !e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "node handle does not resolve");
    }

    out->handle = *h;
    out->kind = e->kind;
    out->child_count = e->child_count;
    if (e->parent != ROBO_HTML_NODE_ID_NONE) {
        const robo_html_node_entry_t *p =
            node_lookup(h->document_id, e->parent);
        if (p) {
            out->parent = robo_html_node_handle_make(h->document_id, p);
        }
    }
    if (e->first_child != ROBO_HTML_NODE_ID_NONE) {
        const robo_html_node_entry_t *c =
            node_lookup(h->document_id, e->first_child);
        if (c) {
            out->first_child = robo_html_node_handle_make(h->document_id, c);
        }
    }
    if (e->last_child != ROBO_HTML_NODE_ID_NONE) {
        const robo_html_node_entry_t *c =
            node_lookup(h->document_id, e->last_child);
        if (c) {
            out->last_child = robo_html_node_handle_make(h->document_id, c);
        }
    }
    if (e->prev_sibling != ROBO_HTML_NODE_ID_NONE) {
        const robo_html_node_entry_t *s =
            node_lookup(h->document_id, e->prev_sibling);
        if (s) {
            out->prev_sibling = robo_html_node_handle_make(h->document_id, s);
        }
    }
    if (e->next_sibling != ROBO_HTML_NODE_ID_NONE) {
        const robo_html_node_entry_t *s =
            node_lookup(h->document_id, e->next_sibling);
        if (s) {
            out->next_sibling = robo_html_node_handle_make(h->document_id, s);
        }
    }

    hdr = payload_of(slot, e);
    out->name = "";
    out->text = "";
    out->text_length = 0u;
    if (hdr) {
        const char *bytes = payload_bytes(hdr);
        const char *limit = bytes + hdr->length;
        const char *cursor = bytes;

        switch (e->kind) {
        case ROBO_HTML_NODE_KIND_ELEMENT:
        case ROBO_HTML_NODE_KIND_DOCUMENT_TYPE:
            out->name = payload_take_string(&cursor, limit);
            break;
        case ROBO_HTML_NODE_KIND_TEXT:
        case ROBO_HTML_NODE_KIND_COMMENT:
            out->text = bytes;
            out->text_length = hdr->length > 0u ? (size_t)hdr->length - 1u : 0u;
            if (out->text_length >= (size_t)hdr->length) {
                out->text_length = (size_t)hdr->length; /* unterminated */
            }
            break;
        default:
            break;
        }
        out->self_closing =
            (hdr->flags & ROBO_HTML_PAYLOAD_FLAG_SELF_CLOSING) ? 1 : 0;
    }
    return ROBO_OK;
}

robo_result_t robo_html_node_child_count(const robo_html_node_handle_t *h,
                                         size_t *out_count,
                                         robo_html_diag_t *diag)
{
    const robo_html_node_entry_t *e;

    if (!h || !out_count) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "handle or out is NULL");
    }
    e = node_entry_for(h);
    if (!e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "node handle does not resolve");
    }
    *out_count = e->child_count;
    return ROBO_OK;
}

robo_result_t robo_html_node_child_at(const robo_html_node_handle_t *h,
                                      size_t index,
                                      robo_html_node_handle_t *out,
                                      robo_html_diag_t *diag)
{
    const robo_html_node_entry_t *e;
    const robo_html_node_entry_t *child;
    size_t i;

    if (!h || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "handle or out is NULL");
    }
    robo_html_node_handle_clear(out);
    e = node_entry_for(h);
    if (!e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "node handle does not resolve");
    }
    child = (e->first_child == ROBO_HTML_NODE_ID_NONE)
                ? NULL
                : node_lookup(h->document_id, e->first_child);
    for (i = 0u; child != NULL && i < index; i++) {
        child = (child->next_sibling == ROBO_HTML_NODE_ID_NONE)
                    ? NULL
                    : node_lookup(h->document_id, child->next_sibling);
    }
    if (!child) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "child index out of range");
    }
    *out = robo_html_node_handle_make(h->document_id, child);
    return ROBO_OK;
}

static int node_name_is(const robo_html_document_slot_t *slot,
                        const robo_html_node_entry_t *e,
                        const char *name)
{
    const robo_html_payload_header_t *hdr = payload_of(slot, e);
    const char *bytes;
    const char *limit;
    const char *cursor;

    if (!hdr || e->kind != ROBO_HTML_NODE_KIND_ELEMENT) {
        return 0;
    }
    bytes = payload_bytes(hdr);
    limit = bytes + hdr->length;
    cursor = bytes;
    return strcmp(payload_take_string(&cursor, limit), name) == 0;
}

robo_result_t robo_html_node_find_child_by_name(
    const robo_html_node_handle_t *h,
    const char *name,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;
    const robo_html_node_entry_t *child;

    if (!h || !name || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "NULL argument");
    }
    robo_html_node_handle_clear(out);
    slot = robo_html_document_slot_find(h->document_id);
    e = node_entry_for(h);
    if (!slot || !e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "node handle does not resolve");
    }
    child = (e->first_child == ROBO_HTML_NODE_ID_NONE)
                ? NULL
                : node_lookup(h->document_id, e->first_child);
    while (child != NULL) {
        if (node_name_is(slot, child, name)) {
            *out = robo_html_node_handle_make(h->document_id, child);
            return ROBO_OK;
        }
        child = (child->next_sibling == ROBO_HTML_NODE_ID_NONE)
                    ? NULL
                    : node_lookup(h->document_id, child->next_sibling);
    }
    return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                          "no child element with that name");
}

/** Name query handed to the traversal predicate (no const cast needed). */
typedef struct element_name_query {
    const char *name;
} element_name_query_t;

/**
 * Predicate for element search. The traversal view already carries the
 * element's decoded local name, so matching does not reach into the payload
 * — the same value an element-name read would return.
 */
static int element_name_matches(const robo_html_node_view_t *view, void *user)
{
    const element_name_query_t *query = (const element_name_query_t *)user;

    return strcmp(view->name, query->name) == 0;
}

robo_result_t robo_html_node_find_element(const robo_html_node_handle_t *h,
                                          const char *name,
                                          robo_html_node_handle_t *out,
                                          robo_html_diag_t *diag)
{
    element_name_query_t query;

    if (!h || !name || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "NULL argument");
    }
    query.name = name;
    /*
     * Element search is the traversal search with an element filter: the
     * walk, the document order and the cycle bound live in one place now
     * rather than in a third copy of the pre-order successor (8.8).
     * "Descendant" keeps this call's contract of excluding @p h itself.
     */
    return robo_html_node_find_descendant(
        h, ROBO_HTML_NODE_FILTER_ELEMENTS, element_name_matches, &query, out,
        diag);
}

/** Accumulator for the subtree-text walk. */
typedef struct subtree_text_state {
    char *out;
    size_t cap;
    size_t written;
    size_t total;
} subtree_text_state_t;

/**
 * Append one visited text node's bytes, NUL-terminating as it goes.
 *
 * @c total counts every byte offered, @c written only those that fit, so a
 * truncated result still reports its full length — the contract callers use
 * to detect truncation. Written down once here, this is what both the
 * whole-node case and the walk below share.
 */
static robo_result_t subtree_text_visit(const robo_html_node_view_t *view,
                                        size_t depth,
                                        void *user)
{
    subtree_text_state_t *state = (subtree_text_state_t *)user;
    size_t i;

    (void)depth;
    for (i = 0u; i < view->text_length; i++) {
        if (state->total + 1u < state->cap) {
            state->out[state->written++] = view->text[i];
        }
        state->total++;
    }
    state->out[state->written] = '\0';
    return ROBO_OK;
}

robo_result_t robo_html_node_subtree_text(const robo_html_node_handle_t *h,
                                          char *out,
                                          size_t out_cap,
                                          size_t *out_length,
                                          robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *start;
    subtree_text_state_t state;
    robo_result_t r;

    if (!h || !out || out_cap == 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "out buffer is unusable");
    }
    out[0] = '\0';
    if (out_length) {
        *out_length = 0u;
    }
    slot = robo_html_document_slot_find(h->document_id);
    start = node_entry_for(h);
    if (!slot || !start) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "node handle does not resolve");
    }
    state.out = out;
    state.cap = out_cap;
    state.written = 0u;
    state.total = 0u;

    if (start->kind == ROBO_HTML_NODE_KIND_TEXT ||
        start->kind == ROBO_HTML_NODE_KIND_COMMENT) {
        /*
         * The node IS the text: reading it back verbatim matches what
         * robo_html_text_content returns for the same node, because the walk
         * below skips its start.
         */
        const robo_html_payload_header_t *hdr = payload_of(slot, start);
        if (hdr) {
            const char *bytes = payload_bytes(hdr);
            state.total = hdr->length > 0u ? (size_t)hdr->length - 1u : 0u;
            state.written = (state.total < out_cap - 1u)
                                ? state.total
                                : out_cap - 1u;
            memcpy(out, bytes, state.written);
            out[state.written] = '\0';
        }
    } else {
        /*
         * Text-only descendants, in document order. This replaces a third
         * copy of the pre-order successor (8.8): the order, the filter, the
         * cycle bound and the no-recursion rule now live in the walker.
         */
        r = robo_html_node_walk_descendants(h, ROBO_HTML_NODE_FILTER_TEXT,
                                            subtree_text_visit, &state, diag);
        if (r != ROBO_OK) {
            if (out_length) {
                *out_length = state.total;
            }
            return r;
        }
    }
    if (out_length) {
        *out_length = state.total;
    }
    return ROBO_OK;
}
