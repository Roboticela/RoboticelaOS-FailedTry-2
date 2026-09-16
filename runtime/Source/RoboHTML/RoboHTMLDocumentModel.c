/*
 * RoboHTML document model (Phase 8.4).
 *
 * Phase 8.1 defined the document as the ownership root; Phase 8.3 built a
 * real tree under it. This file is what makes that ownership *usable*: the
 * document answers who owns what, what state it is in, whether its tree is
 * internally consistent, and how it is closed and reused.
 *
 * Layout of this file:
 *   1. identity and ownership queries
 *   2. the metadata snapshot
 *   3. construction closure (finalize) and reuse (reset)
 *   4. tree invariant audit
 *
 * Everything here is read-side or state-side; no function in this file
 * builds tree content. Tree construction lives in RoboHTMLTreeBuilder.c and
 * the payload writers in RoboHTMLPayload.c, and those obey the flags this
 * file maintains.
 */

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTMLInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. Identity and ownership                                           */
/* ------------------------------------------------------------------ */

int robo_html_document_owns_node(robo_html_document_id_t document_id,
                                 const robo_html_node_handle_t *h)
{
    const robo_html_node_entry_t *e;

    if (!robo_html_node_handle_is_wellformed(h)) {
        return 0;
    }
    /*
     * The document field is checked first and by value: this one comparison
     * is what makes a cross-document handle a plain "no" instead of a
     * lookup that could alias a same-numbered node in the other document.
     */
    if (h->document_id != document_id) {
        return 0;
    }
    if (!robo_html_document_slot_find(document_id)) {
        return 0;
    }
    e = robo_html_node_entry_get(document_id, h->node_id);
    if (!e) {
        return 0;
    }
    return e->generation == h->generation;
}

robo_result_t robo_html_document_node_by_id(
    robo_html_document_id_t document_id,
    robo_html_node_id_t node_id,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TREE,
                              ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    /* Cleared up front so a failure can never leak a usable-looking handle. */
    robo_html_node_handle_clear(out);
    if (node_id == ROBO_HTML_NODE_ID_NONE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TREE,
                              ROBO_INVALID_ARGUMENT, "node id is none");
    }
    slot = robo_html_document_slot_find(document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_NOT_FOUND, "unknown document id");
    }
    e = robo_html_node_entry_get(document_id, node_id);
    if (!e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TREE, ROBO_NOT_FOUND,
                              "node id is not live in this document");
    }
    *out = robo_html_node_handle_make(document_id, e);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* 2. Metadata snapshot                                                */
/* ------------------------------------------------------------------ */

robo_result_t robo_html_document_metadata(
    robo_html_document_id_t document_id,
    robo_html_document_metadata_t *out,
    size_t *out_kind_count,
    robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    size_t i;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    memset(out, 0, sizeof(*out));
    if (out_kind_count) {
        for (i = 0u; i < (size_t)ROBO_HTML_NODE_KIND_COUNT; i++) {
            out_kind_count[i] = 0u;
        }
    }
    slot = robo_html_document_slot_find(document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_NOT_FOUND, "unknown document id");
    }

    out->id = slot->id;
    out->lifecycle = slot->lifecycle;
    out->previous_lifecycle = slot->previous_lifecycle;
    out->finalized = slot->finalized;
    out->is_empty = robo_html_document_is_empty(document_id);
    out->source_kind = slot->source_kind;
    out->source_length = slot->source_length;
    out->source_name = slot->source_name;
    out->encoding = slot->encoding;
    out->fail_domain = slot->fail_domain;
    out->fail_message = slot->fail_message;

    out->root_ok = 0;
    if (robo_html_node_handle_is_wellformed(&slot->root)) {
        const robo_html_node_entry_t *root_e =
            robo_html_node_entry_get(document_id, slot->root.node_id);
        out->root_ok = (root_e != NULL &&
                        root_e->generation == slot->root.generation &&
                        root_e->kind == ROBO_HTML_NODE_KIND_DOCUMENT &&
                        root_e->parent == ROBO_HTML_NODE_ID_NONE)
                           ? 1
                           : 0;
    }

    for (i = 0u; i < (size_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT; i++) {
        const robo_html_node_entry_t *e = &slot->nodes.slots[i];

        if (!e->in_use) {
            continue;
        }
        out->node_count++;
        if (out_kind_count && (size_t)e->kind <
                                  (size_t)ROBO_HTML_NODE_KIND_COUNT) {
            out_kind_count[(size_t)e->kind]++;
        }
        switch (e->kind) {
        case ROBO_HTML_NODE_KIND_ELEMENT:
            out->element_count++;
            break;
        case ROBO_HTML_NODE_KIND_TEXT:
            out->text_count++;
            break;
        case ROBO_HTML_NODE_KIND_COMMENT:
            out->comment_count++;
            break;
        case ROBO_HTML_NODE_KIND_DOCUMENT_TYPE:
            out->doctype_count++;
            break;
        default:
            break;
        }
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* 3. Construction closure and reuse                                   */
/* ------------------------------------------------------------------ */

robo_result_t robo_html_document_finalize(robo_html_document_id_t document_id,
                                          robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    if (!robo_html_document_lifecycle_allows_content(slot->lifecycle)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_LIFECYCLE, ROBO_ERROR,
                              "lifecycle cannot accept document content");
    }
    if (slot->finalized) {
        return ROBO_OK; /* Idempotent: finalizing twice is not a failure. */
    }
    slot->finalized = 1;
    robo_log_debug("robo_html: doc=%llu finalized nodes=%llu",
                   (unsigned long long)document_id,
                   (unsigned long long)robo_html_node_registry_count(
                       &slot->nodes));
    (void)robo_html_diag_set(diag, ROBO_OK, ROBO_HTML_DOMAIN_DOCUMENT,
                             "document finalized", __FILE__, __LINE__);
    return ROBO_OK;
}

int robo_html_document_is_finalized(robo_html_document_id_t document_id)
{
    const robo_html_document_slot_t *slot =
        robo_html_document_slot_find(document_id);

    if (!slot) {
        return 0;
    }
    return slot->finalized;
}

int robo_html_document_allows_construction(
    robo_html_document_id_t document_id)
{
    const robo_html_document_slot_t *slot =
        robo_html_document_slot_find(document_id);

    if (!slot) {
        return 0;
    }
    return !slot->finalized &&
           robo_html_document_lifecycle_allows_content(slot->lifecycle);
}

robo_result_t robo_html_document_reset(robo_html_document_id_t document_id,
                                       robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);
    robo_html_document_lifecycle_t life;
    robo_result_t r;

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    /*
     * A live parser or tree builder holds the tree's invariants in its own
     * state (the open-element stack, the trailing text run). Dropping the
     * tree under it would leave it linking into freed ids, so reset is
     * refused until the lease is released.
     */
    if (slot->leased_parser) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_BUSY,
                              "a parser still holds the document lease");
    }
    if (!robo_html_document_lifecycle_allows_content(slot->lifecycle)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_LIFECYCLE, ROBO_ERROR,
                              "lifecycle cannot accept document content");
    }

    /* Drops the payload arena, every node, and reopens construction. */
    r = robo_html_document_clear_tree(slot, diag);
    if (r != ROBO_OK) {
        return r;
    }

    slot->source_kind = ROBO_HTML_SOURCE_KIND_NONE;
    slot->source_length = 0u;
    slot->source_name[0] = '\0';
    robo_html_encoding_state_init(&slot->encoding);
    slot->fail_domain = ROBO_HTML_DOMAIN_NONE;
    slot->fail_message = NULL;

    /*
     * Back to the "no source attached" resting state. CREATED is unreachable
     * from LOADING/INTERACTIVE/COMPLETE by design (the machine has no edge
     * back to CREATED), so a document that has loaded something settles
     * with an empty tree — which is exactly the state an empty source
     * produces, and that state is COMPLETE: nothing is attached and nothing
     * can arrive. A document that never loaded stays CREATED. Both accept a
     * new source; neither is FAILED or DESTROYED.
     *
     * A reset abandons whatever was in flight, so it steps through
     * INTERACTIVE rather than jumping: the document really does have a live
     * (empty) DOM before it is complete.
     */
    life = slot->lifecycle;
    if (life == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING ||
        life == ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE) {
        /*
         * Abandoning input first makes the DOM live, then settles the
         * document. Both steps are stated separately because the graph has
         * no LOADING → COMPLETE edge: a reset document is genuinely
         * interactive (its empty DOM is readable and mutable) before it is
         * complete.
         */
        if (robo_html_document_apply_transition(
                slot, ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE) != ROBO_OK ||
            robo_html_document_apply_transition(
                slot, ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE) != ROBO_OK) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_LIFECYCLE, ROBO_ERROR,
                                  "reset could not settle the document");
        }
    }
    robo_log_debug("robo_html: doc=%llu reset (lifecycle=%s)",
                   (unsigned long long)document_id,
                   robo_html_document_lifecycle_name(slot->lifecycle));
    (void)robo_html_diag_set(diag, ROBO_OK, ROBO_HTML_DOMAIN_DOCUMENT,
                             "document reset", __FILE__, __LINE__);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* 4. Tree invariant audit                                             */
/* ------------------------------------------------------------------ */

/** Registry index of @p e, for the scratch reachability bitmap. */
static size_t model_entry_index(const robo_html_document_slot_t *slot,
                                const robo_html_node_entry_t *e)
{
    return (size_t)(e - &slot->nodes.slots[0]);
}

static void model_audit_children(const robo_html_document_slot_t *slot,
                                 const robo_html_node_entry_t *e,
                                 robo_html_tree_report_t *rep);

/**
 * Mark and audit the detached subtree rooted at @p root_e (an unreached
 * live node with no parent).
 *
 * The reason this exists rather than "unreachable means orphan": since
 * Phase 8.9 a node can be legitimately detached — remove_child keeps it and
 * everything below it alive — so its children must not be reported as
 * orphans merely for being below a detached root. They are marked in
 * @p seen, and their own links are audited, so a real inconsistency inside
 * a detached subtree is still reported; and because @p seen is shared with
 * the root walk, a detached subtree that reaches into the document tree is
 * reported as the cycle/two-parents case it is.
 */
static void model_audit_detached_subtree(const robo_html_document_slot_t *slot,
                                        const robo_html_node_entry_t *root_e,
                                        uint8_t *seen,
                                        robo_html_tree_report_t *rep)
{
    const robo_html_node_entry_t *cur = root_e;
    size_t guard = 0u;

    seen[model_entry_index(slot, root_e)] = 1u;
    for (;;) {
        const robo_html_node_entry_t *next = NULL;

        if (guard++ > (size_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
            rep->cycles++;
            return;
        }
        if (cur->first_child != ROBO_HTML_NODE_ID_NONE) {
            next = robo_html_node_entry_get(slot->id, cur->first_child);
        } else {
            const robo_html_node_entry_t *up = cur;

            while (up != NULL && up->id != root_e->id) {
                if (up->next_sibling != ROBO_HTML_NODE_ID_NONE) {
                    next = robo_html_node_entry_get(slot->id, up->next_sibling);
                    break;
                }
                if (up->parent == ROBO_HTML_NODE_ID_NONE) {
                    break;
                }
                up = robo_html_node_entry_get(slot->id, up->parent);
            }
        }
        if (!next) {
            return;
        }
        {
            size_t idx = model_entry_index(slot, next);

            if (seen[idx]) {
                /* Already reached: a cycle, or a node inside the document
                 * tree being claimed by a detached subtree. */
                rep->cycles++;
                return;
            }
            seen[idx] = 1u;
        }
        if (next->parent == ROBO_HTML_NODE_ID_NONE) {
            rep->bad_parents++;
            return;
        }
        model_audit_children(slot, next, rep);
        cur = next;
    }
}

/**
 * Verify @p e's child chain against its own bookkeeping links.
 * Reports into @p rep and returns nothing: an inconsistent chain is an
 * observation, not a failure of the audit.
 */
static void model_audit_children(const robo_html_document_slot_t *slot,
                                 const robo_html_node_entry_t *e,
                                 robo_html_tree_report_t *rep)
{
    robo_html_node_id_t prev = ROBO_HTML_NODE_ID_NONE;
    robo_html_node_id_t c = e->first_child;
    size_t count = 0u;

    while (c != ROBO_HTML_NODE_ID_NONE &&
           count <= (size_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        const robo_html_node_entry_t *ce =
            robo_html_node_entry_get(slot->id, c);

        if (!ce) {
            rep->bad_links++;
            break;
        }
        if (ce->parent != e->id) {
            rep->bad_parents++;
        }
        if (ce->prev_sibling != prev) {
            rep->sibling_mismatches++;
        }
        prev = ce->id;
        c = ce->next_sibling;
        count++;
    }
    if (count != (size_t)e->child_count) {
        rep->bad_counts++;
    }
    if (e->last_child != prev) {
        rep->bad_counts++;
    }
}

robo_result_t robo_html_document_validate_tree(
    robo_html_document_id_t document_id,
    robo_html_tree_report_t *out,
    robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *root_e;
    unsigned char *seen;
    robo_html_node_id_t id;
    size_t depth = 1u;
    size_t guard = 0u;
    size_t i;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TREE,
                              ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    memset(out, 0, sizeof(*out));
    slot = robo_html_document_slot_find(document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_NOT_FOUND, "unknown document id");
    }
    out->registered = robo_html_node_registry_count(&slot->nodes);

    /*
     * One byte per registry slot, so reachability is O(1) per visit and the
     * walk stays linear. Allocated rather than placed on the stack: the
     * ceiling is 4096 slots and this call must not risk the caller's stack.
     */
    seen = (unsigned char *)robo_calloc(ROBO_HTML_MAX_NODES_PER_DOCUMENT,
                                        sizeof(unsigned char));
    if (!seen) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TREE,
                              ROBO_OUT_OF_MEMORY,
                              "audit scratch allocation failed");
    }

    root_e = NULL;
    if (robo_html_node_handle_is_wellformed(&slot->root)) {
        root_e = robo_html_node_entry_get(document_id, slot->root.node_id);
        if (root_e && root_e->generation != slot->root.generation) {
            root_e = NULL;
        }
    }
    if (!root_e || root_e->kind != ROBO_HTML_NODE_KIND_DOCUMENT ||
        root_e->parent != ROBO_HTML_NODE_ID_NONE) {
        out->root_ok = 0;
        goto orphan_sweep;
    }
    out->root_ok = 1;
    seen[model_entry_index(slot, root_e)] = 1u;
    out->nodes = 1u;
    out->max_depth = 0u;

    /*
     * Iterative pre-order walk over the child/sibling links. `depth` is
     * maintained incrementally: descending adds one, and ascending past a
     * node subtracts one — so a node's depth is always the number of
     * ancestor edges below the root. No recursion, and the iteration guard
     * means a corrupted link cycle terminates with @c cycles set.
     */
    id = root_e->first_child;
    while (id != ROBO_HTML_NODE_ID_NONE &&
           guard++ <= (size_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        const robo_html_node_entry_t *e =
            robo_html_node_entry_get(document_id, id);
        size_t idx;

        if (!e) {
            out->bad_links++;
            break;
        }
        idx = model_entry_index(slot, e);
        if (seen[idx]) {
            /* Reached twice: a cycle, or two parents claiming one node. */
            out->cycles++;
            break;
        }
        seen[idx] = 1u;
        out->nodes++;
        if (depth > out->max_depth) {
            out->max_depth = depth;
        }
        if (e->parent == ROBO_HTML_NODE_ID_NONE) {
            out->bad_parents++;
            break;
        }
        model_audit_children(slot, e, out);

        if (e->first_child != ROBO_HTML_NODE_ID_NONE) {
            id = e->first_child;
            depth++;
            continue;
        }
        /* Ascend to the next sibling at the nearest ancestor that has one. */
        {
            const robo_html_node_entry_t *cur = e;

            id = ROBO_HTML_NODE_ID_NONE;
            while (cur != NULL && cur->id != root_e->id) {
                if (cur->next_sibling != ROBO_HTML_NODE_ID_NONE) {
                    id = cur->next_sibling;
                    break;
                }
                if (cur->parent == ROBO_HTML_NODE_ID_NONE) {
                    break;
                }
                cur = robo_html_node_entry_get(document_id, cur->parent);
                depth--;
                if (cur == NULL || cur->id == root_e->id) {
                    id = ROBO_HTML_NODE_ID_NONE;
                    break;
                }
            }
        }
    }

orphan_sweep:
    /*
     * Live nodes the root walk did not reach. A node with no parent is a
     * detached root: legal since Phase 8.9, where removing a child keeps the
     * node and its whole subtree alive and re-insertable. It is reported as
     * @c detached rather than as an inconsistency — but its subtree is
     * marked and audited, so a broken link *inside* a detached subtree is
     * still reported, and its descendants are not then miscounted.
     */
    for (i = 0u; i < (size_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT; i++) {
        const robo_html_node_entry_t *e = &slot->nodes.slots[i];

        if (e->in_use && !seen[i] && e->parent == ROBO_HTML_NODE_ID_NONE) {
            out->detached++;
            model_audit_detached_subtree(slot, e, seen, out);
        }
    }
    /*
     * Whatever is still unseen claims a parent while nothing reachable (and
     * no detached root) lists it: a real orphan, and the shape a buggy
     * mutation leaves behind when it writes a parent link without putting
     * the node in a child list.
     */
    for (i = 0u; i < (size_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT; i++) {
        const robo_html_node_entry_t *e = &slot->nodes.slots[i];

        if (e->in_use && !seen[i]) {
            out->orphans++;
        }
    }
    robo_free(seen);

    out->ok = out->root_ok && out->cycles == 0u && out->bad_links == 0u &&
              out->bad_parents == 0u && out->bad_counts == 0u &&
              out->sibling_mismatches == 0u && out->orphans == 0u;
    if (!out->ok) {
        robo_log_warn(
            "robo_html: doc=%llu tree audit unclean: root_ok=%d "                  "registered=%llu nodes=%llu orphans=%llu detached=%llu "
                  "bad_links=%llu cycles=%llu bad_parents=%llu bad_counts=%llu "
                  "sibling=%llu",
            (unsigned long long)document_id, out->root_ok,
            (unsigned long long)out->registered,
            (unsigned long long)out->nodes, (unsigned long long)out->orphans,
            (unsigned long long)out->detached,
            (unsigned long long)out->bad_links, (unsigned long long)out->cycles,
            (unsigned long long)out->bad_parents,
            (unsigned long long)out->bad_counts,
            (unsigned long long)out->sibling_mismatches);
    }
    (void)robo_html_diag_set(diag, ROBO_OK, ROBO_HTML_DOMAIN_TREE,
                             out->ok ? "tree invariants hold"
                                     : "tree invariants violated",
                             __FILE__, __LINE__);
    return ROBO_OK;
}
