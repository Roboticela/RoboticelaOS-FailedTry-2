/*
 * RoboHTML node identity/registry (Phase 8.1).
 *
 * The registry lives inside the owning document. Node ids increase
 * monotonically per document and are never reused; generation bumps when
 * a registry slot is recycled, so handles copied before teardown resolve
 * as stale rather than aliasing a new node.
 */

#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>

void robo_html_node_handle_clear(robo_html_node_handle_t *h)
{
    if (!h) {
        return;
    }
    h->document_id = ROBO_HTML_DOCUMENT_ID_NONE;
    h->node_id = ROBO_HTML_NODE_ID_NONE;
    h->generation = 0u;
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

static const robo_html_node_entry_t *node_lookup(
    robo_html_document_id_t document_id,
    robo_html_node_id_t node_id)
{
    const robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot) {
        return NULL;
    }
    {
        size_t i;
        for (i = 0u; i < ROBO_HTML_MAX_NODES_PER_DOCUMENT; i++) {
            const robo_html_node_entry_t *e = &slot->nodes.slots[i];
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

    if (!robo_html_node_handle_is_wellformed(h)) {
        return 0;
    }
    slot = robo_html_document_slot_find(h->document_id);
    if (!slot) {
        /* Well-formed handle pointing at a destroyed/unknown document. */
        return 1;
    }
    return node_lookup(h->document_id, h->node_id) == NULL;
}

void robo_html_node_registry_init(robo_html_node_registry_t *reg)
{
    if (!reg) {
        return;
    }
    {
        size_t i;
        for (i = 0u; i < ROBO_HTML_MAX_NODES_PER_DOCUMENT; i++) {
            reg->slots[i].in_use = 0;
            reg->slots[i].id = ROBO_HTML_NODE_ID_NONE;
            reg->slots[i].generation = 0u;
            reg->slots[i].kind = ROBO_HTML_NODE_KIND_DOCUMENT;
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
