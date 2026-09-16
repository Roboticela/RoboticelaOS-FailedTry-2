/*
 * RoboHTML text-node model (Phase 8.7) — see RoboHTMLText.h.
 *
 * File shape:
 *   1. text-entry resolution and connectedness
 *   2. text creation (the single construction seam)
 *   3. text read side (kind test, view, content, length, parent)
 *   4. text mutation (set replaces, append extends)
 *
 * Design notes worth keeping visible:
 *
 * - There is no per-node string and no refcount. Content lives in the
 *   document's bump-allocated payload arena exactly as every other node
 *   payload does, so the byte run moves only when the arena moves and is
 *   freed exactly once, with the document.
 *
 * - set writes a fresh payload instead of editing in place. That is the same
 *   choice the attribute model's mutation makes and for the same reason: the
 *   old bytes become dead arena space (bounded, reclaimed wholesale) rather
 *   than something to splice around, and a payload is never resized under a
 *   borrowed pointer. Node identity, parent, siblings and child order are
 *   untouched, so a set survives document finalization — 8.4 closed tree
 *   *construction*, not the document's readability.
 *
 * - append is one operation with two implementations. Growing in place is
 *   the parser's hot path (adjacent character tokens arrive while the run is
 *   still the arena tail); the copying path exists so a DOM append after the
 *   tree was built — when other payloads sit behind the run — behaves
 *   identically. Content bytes are never truncated or split silently.
 */

#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTMLInternal.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. Resolution                                                       */
/* ------------------------------------------------------------------ */

/**
 * The registry entry behind a live text handle, or NULL.
 *
 * Returns NULL for an ill-formed handle, an unknown document, an unknown or
 * stale node id, and a live node of any other kind — so every caller gets
 * "this is not text" from one place instead of repeating the four checks.
 * The public accessors turn that into their documented "not text" answer
 * rather than an error, which is what keeps a stale handle harmless.
 */
static const robo_html_node_entry_t *text_entry(
    const robo_html_node_handle_t *h)
{
    const robo_html_node_entry_t *e;

    if (!robo_html_node_handle_is_wellformed(h)) {
        return NULL;
    }
    e = robo_html_node_entry_get(h->document_id, h->node_id);
    if (!e || e->generation != h->generation) {
        return NULL;
    }
    if (e->kind != ROBO_HTML_NODE_KIND_TEXT) {
        return NULL;
    }
    return e;
}

/**
 * Is @p entry reachable from the document root?
 *
 * Walked up the parent links rather than cached as a flag, for the same
 * reason the element model walks: a flag would need every link/unlink path to
 * maintain it and would go stale the moment a future DOM moves a subtree,
 * while the walk costs one step per ancestor and is bounded by the node
 * ceiling. Text is the deepest node kind, so the chain is short.
 */
static int text_is_connected(const robo_html_document_slot_t *slot,
                             const robo_html_node_entry_t *entry)
{
    robo_html_node_id_t id = entry->parent;
    size_t guard = 0u;

    while (id != ROBO_HTML_NODE_ID_NONE &&
           guard++ < (size_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        const robo_html_node_entry_t *up =
            robo_html_node_entry_get(slot->id, id);

        if (!up) {
            return 0;
        }
        if (up->kind == ROBO_HTML_NODE_KIND_DOCUMENT &&
            id == slot->root.node_id) {
            return 1;
        }
        id = up->parent;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 2. Creation                                                         */
/* ------------------------------------------------------------------ */

void robo_html_text_create_info_init(robo_html_text_create_info_t *info)
{
    if (!info) {
        return;
    }
    robo_html_node_handle_clear(&info->parent);
    info->origin = ROBO_HTML_NODE_ORIGIN_NONE;
    info->content = NULL;
    info->length = 0u;
}

robo_result_t robo_html_text_create(
    robo_html_document_id_t document_id,
    const robo_html_text_create_info_t *info,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_node_handle_t h;
    robo_html_node_entry_t *entry;
    robo_result_t r;

    if (out) {
        robo_html_node_handle_clear(out);
    }
    if (!out || !info) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                              ROBO_INVALID_ARGUMENT, "out or info is NULL");
    }
    if (!info->content && info->length > 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                              ROBO_INVALID_ARGUMENT,
                              "content is NULL with a non-zero length");
    }
    /*
     * The origin is the text descriptor's only enumerator, so validating it
     * here gives the rejection the text domain rather than the node
     * registry's. Registration would catch it too, one layer down.
     */
    if (!robo_html_node_origin_is_known(info->origin) ||
        info->origin == ROBO_HTML_NODE_ORIGIN_NONE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                              ROBO_INVALID_ARGUMENT,
                              "text origin is not a live creation origin");
    }
    if (!robo_html_node_handle_is_wellformed(&info->parent)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                              ROBO_INVALID_ARGUMENT,
                              "parent handle is ill-formed");
    }
    slot = robo_html_document_slot_find(document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    /*
     * The whole cross-document rule in one place: the parent must name *this*
     * document and that document must actually own it. Checking the claim
     * before resolving is what turns a handle from another document into a
     * detected error instead of an aliased node — two documents may issue the
     * same id/generation pair legitimately.
     */
    if (info->parent.document_id != document_id) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                              ROBO_INVALID_ARGUMENT,
                              "parent belongs to another document");
    }
    if (!robo_html_document_owns_node(document_id, &info->parent)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "parent is not a live node of this document");
    }

    r = robo_html_document_register_node(document_id, ROBO_HTML_NODE_KIND_TEXT,
                                         info->origin, &h, diag);
    if (r != ROBO_OK) {
        return r;
    }
    entry = robo_html_node_entry_mut(document_id, h.node_id);
    if (!entry) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "registered text node vanished from the registry");
    }
    r = robo_html_payload_write_text(slot, entry, info->content, info->length,
                                     diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_html_node_link_child(document_id, info->parent.node_id, h.node_id,
                                  diag);
    if (r != ROBO_OK) {
        return r;
    }
    *out = h;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* 3. Read side                                                        */
/* ------------------------------------------------------------------ */

int robo_html_text_is_text(const robo_html_node_handle_t *h)
{
    return text_entry(h) != NULL ? 1 : 0;
}

robo_result_t robo_html_text_view_get(const robo_html_node_handle_t *text,
                                      robo_html_text_view_t *out,
                                      robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (!text || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                              ROBO_INVALID_ARGUMENT, "text or out is NULL");
    }
    memset(out, 0, sizeof(*out));
    slot = robo_html_document_slot_find(text->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    e = text_entry(text);
    if (!e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "handle does not resolve to a live text node");
    }
    out->handle = *text;
    out->content = robo_html_payload_text(slot, e, &out->length);
    if (e->parent != ROBO_HTML_NODE_ID_NONE) {
        const robo_html_node_entry_t *p =
            robo_html_node_entry_get(slot->id, e->parent);

        if (p) {
            out->parent = robo_html_node_handle_make(slot->id, p);
        }
    }
    out->state = ROBO_HTML_TEXT_STATE_NONE;
    if (e->origin == (uint32_t)ROBO_HTML_NODE_ORIGIN_PARSER) {
        out->state |= ROBO_HTML_TEXT_STATE_PARSER_CREATED;
    }
    if (text_is_connected(slot, e)) {
        out->state |= ROBO_HTML_TEXT_STATE_CONNECTED;
    }
    return ROBO_OK;
}

const char *robo_html_text_content(const robo_html_node_handle_t *text,
                                   size_t *out_length)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (out_length) {
        *out_length = 0u;
    }
    if (!text) {
        return NULL;
    }
    slot = robo_html_document_slot_find(text->document_id);
    e = text_entry(text);
    if (!slot || !e) {
        return NULL;
    }
    return robo_html_payload_text(slot, e, out_length);
}

size_t robo_html_text_length(const robo_html_node_handle_t *text)
{
    size_t length = 0u;

    (void)robo_html_text_content(text, &length);
    return length;
}

int robo_html_text_is_empty(const robo_html_node_handle_t *text)
{
    size_t length = 0u;

    if (!robo_html_text_is_text(text)) {
        return 0;
    }
    (void)robo_html_text_content(text, &length);
    return length == 0u ? 1 : 0;
}

robo_result_t robo_html_text_parent(const robo_html_node_handle_t *text,
                                    robo_html_node_handle_t *out,
                                    robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;
    const robo_html_node_entry_t *p;

    if (out) {
        robo_html_node_handle_clear(out);
    }
    if (!text || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                              ROBO_INVALID_ARGUMENT, "text or out is NULL");
    }
    slot = robo_html_document_slot_find(text->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    e = text_entry(text);
    if (!e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "handle does not resolve to a live text node");
    }
    if (e->parent == ROBO_HTML_NODE_ID_NONE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "text node has no parent");
    }
    p = robo_html_node_entry_get(slot->id, e->parent);
    if (!p) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "text node's parent link does not resolve");
    }
    *out = robo_html_node_handle_make(slot->id, p);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* 4. Mutation                                                         */
/* ------------------------------------------------------------------ */

robo_result_t robo_html_text_set(const robo_html_node_handle_t *text,
                                 const char *bytes,
                                 size_t length,
                                 robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_node_entry_t *e;

    /*
     * Only a NULL handle is an argument error. A well-formed handle that names
     * no live text — including a cleared or stale one — is a lookup that
     * missed, which is what everything else in this header reports too.
     */
    if (!text) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                              ROBO_INVALID_ARGUMENT, "text handle is NULL");
    }
    if (!bytes && length > 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                              ROBO_INVALID_ARGUMENT,
                              "bytes is NULL with a non-zero length");
    }
    slot = robo_html_document_slot_find(text->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    if (!text_entry(text)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "handle does not resolve to a live text node");
    }
    e = robo_html_node_entry_mut(text->document_id, text->node_id);
    if (!e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "text node does not resolve for mutation");
    }
    /* A fresh payload; the previous run becomes dead arena space. */
    return robo_html_payload_write_text(slot, e, bytes, length, diag);
}

robo_result_t robo_html_text_append(const robo_html_node_handle_t *text,
                                    const char *bytes,
                                    size_t length,
                                    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_node_entry_t *e;
    robo_result_t r;

    if (!text) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                              ROBO_INVALID_ARGUMENT, "text handle is NULL");
    }
    if (!bytes && length > 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                              ROBO_INVALID_ARGUMENT,
                              "bytes is NULL with a non-zero length");
    }
    if (length == 0u) {
        /* Nothing to add: a successful no-op, but still a kind check. */
        if (!text_entry(text)) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                                  "handle does not resolve to a live text node");
        }
        return ROBO_OK;
    }
    slot = robo_html_document_slot_find(text->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    if (!text_entry(text)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "handle does not resolve to a live text node");
    }
    e = robo_html_node_entry_mut(text->document_id, text->node_id);
    if (!e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                              "text node does not resolve for mutation");
    }

    /* Fast path: the run still ends the arena, so it grows without a copy. */
    r = robo_html_payload_append_text(slot, e, bytes, length, NULL);
    if (r == ROBO_OK) {
        return ROBO_OK;
    }
    if (r != ROBO_NOT_SUPPORTED) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, r,
                              "text run could not be extended");
    }

    /*
     * Slow path: other payloads now sit behind the run, so the extended run
     * has to be written somewhere else. Copy both parts once into one buffer
     * and let the payload writer own the result.
     */
    {
        size_t old_length = 0u;
        const char *old = robo_html_payload_text(slot, e, &old_length);
        char *joined;
        size_t joined_length;

        if (!old) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT, ROBO_NOT_FOUND,
                                  "text payload does not resolve");
        }
        if (old_length > (size_t)0xFFFFFFFFu - length) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                                  ROBO_NOT_SUPPORTED,
                                  "text run exceeds one payload");
        }
        joined_length = old_length + length;
        joined = (char *)robo_malloc(joined_length > 0u ? joined_length : 1u);
        if (!joined) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TEXT,
                                  ROBO_OUT_OF_MEMORY,
                                  "no memory to extend the text run");
        }
        if (old_length > 0u) {
            memcpy(joined, old, old_length);
        }
        memcpy(joined + old_length, bytes, length);
        r = robo_html_payload_write_text(slot, e, joined, joined_length, diag);
        robo_free(joined);
        return r;
    }
}
