/*
 * RoboHTML DOM mutation (Phase 8.9) — see RoboHTMLMutation.h.
 *
 * File shape:
 *   1. operand resolution (well-formed → same document → live)
 *   2. the four structural operations
 *   3. the two queries (contains, tree version)
 *
 * The whole file is thin on purpose. Every tree rule that matters — the
 * kind check, the detached-child rule, the reference-child rule, the cycle
 * gate, the sibling rewiring and the tree-version bump — lives in the one
 * linking primitive in RoboHTMLNode.c, so the public API here is operand
 * validation plus the mutation hook, and a parser link cannot disagree with
 * a DOM link about what a tree is.
 *
 * The other deliberate property: validation is complete before the first
 * write. A refused mutation therefore leaves the tree byte-for-byte as it
 * was, including the tree version, so "this call changed nothing" is
 * something a caller can verify rather than assume.
 */

#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* 1. Operand resolution                                               */
/* ------------------------------------------------------------------ */

/*
 * Resolve one operand against the document the operation is anchored to,
 * distinguishing the three ways a handle can be unusable. The order matters:
 * a handle claiming another document is reported as cross-document even if
 * that document happens to have a live node with the same id, because the
 * caller's mistake is the ownership, not the identity.
 */
static robo_result_t mutation_entry(const robo_html_node_handle_t *h,
                                   robo_html_document_id_t document_id,
                                   const robo_html_node_entry_t **out,
                                   robo_html_diag_t *diag)
{
    const robo_html_node_entry_t *e;

    if (!h || !robo_html_node_handle_is_wellformed(h)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_INVALID_ARGUMENT,
                              "mutation operand is NULL or ill-formed");
    }
    /*
     * The claim, before the resolution: a handle from another document can
     * never be a valid operand, and saying so specifically is what makes the
     * refusal actionable (adoption is a different operation, not a bug in
     * the caller's ids).
     */
    if (h->document_id != document_id) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_NOT_SUPPORTED,
                              "cross-document mutation is not supported");
    }
    e = robo_html_node_entry_get(document_id, h->node_id);
    if (!e || e->generation != h->generation) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION, ROBO_NOT_FOUND,
                              "mutation operand does not resolve to a live "
                              "node");
    }
    *out = e;
    return ROBO_OK;
}

/**
 * The DOM's own rules for attaching @p child under @p parent, applied before
 * the shared linking primitive runs.
 *
 * The primitive enforces the same rules for every caller — and that is the
 * point, because it is what makes a parser link and a DOM link agree about
 * what a tree is — but it reports them in the node domain, since the parser
 * reaches it too. Doing the DOM's checks here means a mutation refusal is
 * always reported as a mutation, while the primitive stays the backstop
 * rather than the messenger.
 *
 * Order matters: every rejection is decided before the caller writes
 * anything, which is what makes a refused mutation provably inert.
 */
static robo_result_t check_attachment(const robo_html_node_entry_t *parent,
                                     const robo_html_node_entry_t *child,
                                     robo_html_document_id_t document_id,
                                     robo_html_diag_t *diag)
{
    if (parent->id == child->id) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_INVALID_ARGUMENT,
                              "a node cannot be its own child");
    }
    if (child->kind == ROBO_HTML_NODE_KIND_DOCUMENT) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_NOT_SUPPORTED,
                              "the document node cannot become a child");
    }
    if (!robo_html_node_kind_can_hold_children(parent->kind)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_NOT_SUPPORTED,
                              "node kind cannot have children");
    }
    if (child->parent != ROBO_HTML_NODE_ID_NONE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_ALREADY_EXISTS,
                              "node already has a parent");
    }
    if (robo_html_node_is_ancestor_of(document_id, child->id, parent->id)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_INVALID_ARGUMENT,
                              "a node cannot be inserted beneath its own "
                              "descendant");
    }
    return ROBO_OK;
}

/** The document node's id: the operation anchor for a caller's operands. */
static robo_html_document_id_t anchor_document(
    const robo_html_node_handle_t *first)
{
    return (first && robo_html_node_handle_is_wellformed(first))
               ? first->document_id
               : ROBO_HTML_DOCUMENT_ID_NONE;
}

/** Emit the structural-change hook event for @p node. */
static void announce_mutation(robo_html_document_id_t document_id,
                              const robo_html_node_handle_t *node)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);
    robo_html_mutation_event_t ev;

    if (!slot) {
        return;
    }
    ev.kind = ROBO_HTML_MUTATION_TREE_MUTATED;
    ev.document_id = document_id;
    ev.node = node ? *node : slot->root;
    ev.lifecycle = slot->lifecycle;
    robo_html_emit_mutation(&ev, slot);
}

/* ------------------------------------------------------------------ */
/* 2. Structural operations                                            */
/* ------------------------------------------------------------------ */

robo_result_t robo_html_node_append_child(const robo_html_node_handle_t *parent,
                                          const robo_html_node_handle_t *child,
                                          robo_html_diag_t *diag)
{
    robo_html_document_id_t doc;
    const robo_html_node_entry_t *p;
    const robo_html_node_entry_t *c;
    robo_result_t r;

    doc = anchor_document(parent);
    r = mutation_entry(parent, doc, &p, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = mutation_entry(child, doc, &c, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = check_attachment(p, c, doc, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_html_node_insert_relative(doc, p->id, c->id,
                                       ROBO_HTML_NODE_ID_NONE, diag);
    if (r != ROBO_OK) {
        return r;
    }
    announce_mutation(doc, child);
    return ROBO_OK;
}

robo_result_t robo_html_node_insert_before(
    const robo_html_node_handle_t *parent,
    const robo_html_node_handle_t *new_child,
    const robo_html_node_handle_t *reference_child,
    robo_html_diag_t *diag)
{
    robo_html_document_id_t doc;
    const robo_html_node_entry_t *p;
    const robo_html_node_entry_t *n;
    const robo_html_node_entry_t *ref;
    robo_result_t r;

    doc = anchor_document(parent);
    r = mutation_entry(parent, doc, &p, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = mutation_entry(new_child, doc, &n, diag);
    if (r != ROBO_OK) {
        return r;
    }
    /*
     * The reference is resolved here rather than left to the linking
     * primitive so that "the anchor is not a child of the parent" is
     * reported with the MUTATION domain and a message that names the
     * operation. The primitive enforces the same rule for every other
     * caller.
     */
    r = mutation_entry(reference_child, doc, &ref, diag);
    if (r != ROBO_OK) {
        return r;
    }
    if (ref->parent != p->id) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION, ROBO_NOT_FOUND,
                              "reference child does not belong to the parent");
    }
    r = check_attachment(p, n, doc, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_html_node_insert_relative(doc, p->id, n->id, ref->id, diag);
    if (r != ROBO_OK) {
        return r;
    }
    announce_mutation(doc, new_child);
    return ROBO_OK;
}

robo_result_t robo_html_node_remove_child(const robo_html_node_handle_t *parent,
                                          const robo_html_node_handle_t *child,
                                          robo_html_diag_t *diag)
{
    robo_html_document_id_t doc;
    const robo_html_node_entry_t *p;
    const robo_html_node_entry_t *c;
    robo_result_t r;

    doc = anchor_document(parent);
    r = mutation_entry(parent, doc, &p, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = mutation_entry(child, doc, &c, diag);
    if (r != ROBO_OK) {
        return r;
    }
    if (c->parent != p->id) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION, ROBO_NOT_FOUND,
                              "node is not a child of the given parent");
    }
    r = robo_html_node_unlink(doc, c->id, diag);
    if (r != ROBO_OK) {
        return r;
    }
    announce_mutation(doc, child);
    return ROBO_OK;
}

robo_result_t robo_html_node_replace_child(
    const robo_html_node_handle_t *parent,
    const robo_html_node_handle_t *new_child,
    const robo_html_node_handle_t *old_child,
    robo_html_diag_t *diag)
{
    robo_html_document_id_t doc;
    const robo_html_node_entry_t *p;
    const robo_html_node_entry_t *n;
    const robo_html_node_entry_t *o;
    robo_html_node_id_t successor;
    robo_result_t r;

    doc = anchor_document(parent);
    r = mutation_entry(parent, doc, &p, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = mutation_entry(new_child, doc, &n, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = mutation_entry(old_child, doc, &o, diag);
    if (r != ROBO_OK) {
        return r;
    }
    if (n->id == o->id) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_INVALID_ARGUMENT,
                              "a node cannot replace itself");
    }
    if (o->parent != p->id) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION, ROBO_NOT_FOUND,
                              "node is not a child of the given parent");
    }
    /*
     * Every rule that could refuse the insertion is checked BEFORE the old
     * child is detached. A replacement that detached the old node and then
     * failed to insert the new one would have dropped a child on the floor,
     * which is exactly the half-applied mutation this ordering prevents.
     */
    r = check_attachment(p, n, doc, diag);
    if (r != ROBO_OK) {
        return r;
    }
    /* Captured before the detach: unlink clears the old node's own links. */
    successor = o->next_sibling;

    r = robo_html_node_unlink(doc, o->id, diag);
    if (r != ROBO_OK) {
        return r;
    }
    /*
     * The successor is still a child of the parent — detaching the old node
     * only rewired its neighbours — so appending is the correct action when
     * the old node was last, and inserting before the successor reproduces
     * its exact former position otherwise.
     */
    r = robo_html_node_insert_relative(doc, p->id, n->id, successor, diag);
    if (r != ROBO_OK) {
        return r;
    }
    announce_mutation(doc, new_child);
    return ROBO_OK;
}

robo_result_t robo_html_node_remove_children(
    const robo_html_node_handle_t *parent,
    size_t *out_removed,
    robo_html_diag_t *diag)
{
    robo_html_document_id_t doc;
    const robo_html_node_entry_t *p;
    robo_html_node_handle_t first;
    size_t removed = 0u;
    robo_result_t r;

    if (out_removed) {
        *out_removed = 0u;
    }
    doc = anchor_document(parent);
    r = mutation_entry(parent, doc, &p, diag);
    if (r != ROBO_OK) {
        return r;
    }
    if (!robo_html_node_kind_can_hold_children(p->kind)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_NOT_SUPPORTED,
                              "node kind cannot have children");
    }
    /*
     * Always re-reading the parent's first child (rather than stepping the
     * chain) is what makes this safe: each detach repairs the list, so the
     * next first child is whatever remains. The loop is bounded by the node
     * ceiling so a corrupted chain cannot spin.
     */
    while (removed < (size_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        const robo_html_node_entry_t *live =
            robo_html_node_entry_get(doc, p->id);

        if (!live || live->first_child == ROBO_HTML_NODE_ID_NONE) {
            break;
        }
        {
            const robo_html_node_entry_t *child =
                robo_html_node_entry_get(doc, live->first_child);

            if (!child) {
                break;
            }
            /* A handle needs the generation too, and only the entry has it. */
            first = robo_html_node_handle_make(doc, child);
        }
        r = robo_html_node_unlink(doc, first.node_id, diag);
        if (r != ROBO_OK) {
            return r;
        }
        removed++;
    }
    if (out_removed) {
        *out_removed = removed;
    }
    if (removed > 0u) {
        announce_mutation(doc, parent);
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* 3. Queries                                                          */
/* ------------------------------------------------------------------ */

robo_result_t robo_html_node_contains(const robo_html_node_handle_t *ancestor,
                                      const robo_html_node_handle_t *node,
                                      int *out_contains,
                                      robo_html_diag_t *diag)
{
    robo_html_document_id_t doc;
    const robo_html_node_entry_t *a;
    const robo_html_node_entry_t *n;
    robo_result_t r;

    if (out_contains) {
        *out_contains = 0;
    }
    if (!out_contains) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_INVALID_ARGUMENT, "out_contains is NULL");
    }
    doc = anchor_document(ancestor);
    r = mutation_entry(ancestor, doc, &a, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = mutation_entry(node, doc, &n, diag);
    if (r != ROBO_OK) {
        return r;
    }
    *out_contains = robo_html_node_is_ancestor_of(doc, a->id, n->id) ? 1 : 0;
    return ROBO_OK;
}

robo_result_t robo_html_node_tree_version(
    robo_html_document_id_t document_id,
    uint32_t *out_version,
    robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;

    if (!out_version) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                              ROBO_INVALID_ARGUMENT, "out_version is NULL");
    }
    *out_version = 0u;
    slot = robo_html_document_slot_find(document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    *out_version = slot->tree_version;
    return ROBO_OK;
}
