/*
 * RoboHTML DOM traversal (Phase 8.8) — see RoboHTMLTraversal.h.
 *
 * File shape:
 *   1. handle resolution and the two upward walks (depth, root)
 *   2. single-link accessors
 *   3. the child cursor
 *   4. filters
 *   5. the walker (the one pre-order successor) and search
 *
 * The whole phase rests on one routine — @c advance — which is the
 * constant-space successor in pre-order:
 *
 *   if the node has a child, the successor is that child (descend);
 *   otherwise walk up until a node has a next sibling, which is the
 *   successor (ascend), and stop when the traversal root is reached.
 *
 * That needs no stack and no recursion, which is the point: a tree can be
 * 4096 nodes deep, so a recursive walk is a stack-overflow vector that an
 * attacker-controlled document could aim at, while an explicit stack would
 * need an allocation and therefore a way to fail mid-walk. Both of those
 * were already avoided ad hoc in three places (element search, subtree text,
 * the debug dump); this is the one implementation they now share.
 */

#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. Resolution and the upward walks                                  */
/* ------------------------------------------------------------------ */

/**
 * The registry entry behind a live handle, or NULL.
 *
 * Mirrors the kind-checked resolvers in the element and text models, minus
 * the kind check: traversal is kind-agnostic by construction, because a
 * caller walking a tree must be able to step through nodes it does not want
 * to visit.
 */
static const robo_html_node_entry_t *traversal_entry(
    const robo_html_node_handle_t *h)
{
    const robo_html_node_entry_t *e;

    if (!h || !robo_html_node_handle_is_wellformed(h)) {
        return NULL;
    }
    e = robo_html_node_entry_get(h->document_id, h->node_id);
    if (!e || e->generation != h->generation) {
        return NULL;
    }
    return e;
}

/**
 * Topmost ancestor of @p entry, reachable by parent links.
 *
 * The step bound is the node ceiling: a parent chain cannot legitimately be
 * longer than the registry, so exceeding it means the links form a cycle and
 * the walk stops with an error instead of spinning. This is the guard that
 * makes depth and root safe on a corrupted graph — and it is the only reason
 * those two can promise termination rather than hope for it.
 */
static robo_result_t ascent_top(robo_html_document_id_t document_id,
                                const robo_html_node_entry_t *entry,
                                const robo_html_node_entry_t **out_top,
                                size_t *out_depth,
                                robo_html_diag_t *diag)
{
    const robo_html_node_entry_t *current = entry;
    robo_html_node_id_t id = entry->parent;
    size_t depth = 0u;

    while (id != ROBO_HTML_NODE_ID_NONE) {
        const robo_html_node_entry_t *up;

        if (depth >= (size_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_ERROR,
                                  "parent chain does not end: the link graph "
                                  "is not a tree");
        }
        up = robo_html_node_entry_get(document_id, id);
        if (!up) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_ERROR,
                                  "parent link does not resolve to a live node");
        }
        current = up;
        id = up->parent;
        depth++;
    }
    if (out_top) {
        *out_top = current;
    }
    if (out_depth) {
        *out_depth = depth;
    }
    return ROBO_OK;
}

static robo_result_t node_resolve(const robo_html_node_handle_t *node,
                                  const robo_html_node_entry_t **out,
                                  robo_html_diag_t *diag)
{
    const robo_html_node_entry_t *e;

    if (!node || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                              ROBO_INVALID_ARGUMENT, "node or out is NULL");
    }
    if (!robo_html_document_slot_find(node->document_id)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    e = traversal_entry(node);
    if (!e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_NOT_FOUND,
                              "node handle does not resolve to a live node");
    }
    *out = e;
    return ROBO_OK;
}

robo_result_t robo_html_node_depth(const robo_html_node_handle_t *node,
                                   size_t *out_depth,
                                   robo_html_diag_t *diag)
{
    const robo_html_node_entry_t *e;
    robo_result_t r;

    if (!out_depth) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                              ROBO_INVALID_ARGUMENT, "out_depth is NULL");
    }
    *out_depth = 0u;
    r = node_resolve(node, &e, diag);
    if (r != ROBO_OK) {
        return r;
    }
    return ascent_top(node->document_id, e, NULL, out_depth, diag);
}

robo_result_t robo_html_node_root(const robo_html_node_handle_t *node,
                                  robo_html_node_handle_t *out,
                                  robo_html_diag_t *diag)
{
    const robo_html_node_entry_t *e;
    const robo_html_node_entry_t *top = NULL;
    robo_result_t r;

    if (out) {
        robo_html_node_handle_clear(out);
    }
    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                              ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    r = node_resolve(node, &e, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = ascent_top(node->document_id, e, &top, NULL, diag);
    if (r != ROBO_OK) {
        return r;
    }
    *out = robo_html_node_handle_make(node->document_id, top);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* 2. Single-link accessors                                            */
/* ------------------------------------------------------------------ */

typedef enum traversal_link {
    LINK_PARENT = 0,
    LINK_FIRST_CHILD = 1,
    LINK_LAST_CHILD = 2,
    LINK_NEXT_SIBLING = 3,
    LINK_PREV_SIBLING = 4
} traversal_link_t;

static robo_html_node_id_t entry_link(const robo_html_node_entry_t *e,
                                      traversal_link_t which)
{
    switch (which) {
    case LINK_PARENT:
        return e->parent;
    case LINK_FIRST_CHILD:
        return e->first_child;
    case LINK_LAST_CHILD:
        return e->last_child;
    case LINK_NEXT_SIBLING:
        return e->next_sibling;
    case LINK_PREV_SIBLING:
    default:
        return e->prev_sibling;
    }
}

static const char *link_missing_message(traversal_link_t which)
{
    switch (which) {
    case LINK_PARENT:
        return "node has no parent";
    case LINK_FIRST_CHILD:
        return "node has no children";
    case LINK_LAST_CHILD:
        return "node has no children";
    case LINK_NEXT_SIBLING:
        return "node is the last sibling";
    case LINK_PREV_SIBLING:
    default:
        return "node is the first sibling";
    }
}

/**
 * Read one link as a handle.
 *
 * The target id is looked up rather than trusted: a link that does not
 * resolve (a corrupted graph, or a slot retired by a tree clear) is reported
 * as a miss instead of being turned into a handle that would resolve to
 * whatever node next occupies that id.
 */
static robo_result_t read_link(const robo_html_node_handle_t *node,
                               traversal_link_t which,
                               robo_html_node_handle_t *out,
                               robo_html_diag_t *diag)
{
    const robo_html_node_entry_t *e;
    const robo_html_node_entry_t *target;
    robo_html_node_id_t id;
    robo_result_t r;

    if (out) {
        robo_html_node_handle_clear(out);
    }
    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                              ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    r = node_resolve(node, &e, diag);
    if (r != ROBO_OK) {
        return r;
    }
    id = entry_link(e, which);
    if (id == ROBO_HTML_NODE_ID_NONE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_NOT_FOUND,
                              link_missing_message(which));
    }
    target = robo_html_node_entry_get(node->document_id, id);
    if (!target) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_NOT_FOUND,
                              "tree link does not resolve to a live node");
    }
    *out = robo_html_node_handle_make(node->document_id, target);
    return ROBO_OK;
}

robo_result_t robo_html_node_parent(const robo_html_node_handle_t *node,
                                    robo_html_node_handle_t *out,
                                    robo_html_diag_t *diag)
{
    return read_link(node, LINK_PARENT, out, diag);
}

robo_result_t robo_html_node_first_child(const robo_html_node_handle_t *node,
                                         robo_html_node_handle_t *out,
                                         robo_html_diag_t *diag)
{
    return read_link(node, LINK_FIRST_CHILD, out, diag);
}

robo_result_t robo_html_node_last_child(const robo_html_node_handle_t *node,
                                        robo_html_node_handle_t *out,
                                        robo_html_diag_t *diag)
{
    return read_link(node, LINK_LAST_CHILD, out, diag);
}

robo_result_t robo_html_node_next_sibling(const robo_html_node_handle_t *node,
                                          robo_html_node_handle_t *out,
                                          robo_html_diag_t *diag)
{
    return read_link(node, LINK_NEXT_SIBLING, out, diag);
}

robo_result_t robo_html_node_prev_sibling(const robo_html_node_handle_t *node,
                                          robo_html_node_handle_t *out,
                                          robo_html_diag_t *diag)
{
    return read_link(node, LINK_PREV_SIBLING, out, diag);
}

/* ------------------------------------------------------------------ */
/* 3. Child cursor                                                     */
/* ------------------------------------------------------------------ */

void robo_html_child_cursor_init(robo_html_child_cursor_t *cursor)
{
    if (!cursor) {
        return;
    }
    cursor->document_id = ROBO_HTML_DOCUMENT_ID_NONE;
    cursor->next_id = ROBO_HTML_NODE_ID_NONE;
    cursor->remaining = 0u;
    cursor->version = 0u;
}

robo_result_t robo_html_child_cursor_open(robo_html_child_cursor_t *cursor,
                                          const robo_html_node_handle_t *parent,
                                          robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;
    robo_result_t r;

    if (!cursor) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                              ROBO_INVALID_ARGUMENT, "cursor is NULL");
    }
    robo_html_child_cursor_init(cursor);
    r = node_resolve(parent, &e, diag);
    if (r != ROBO_OK) {
        return r;
    }
    slot = robo_html_document_slot_find(parent->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    cursor->document_id = parent->document_id;
    cursor->next_id = e->first_child;
    /*
     * A child list cannot be longer than the registry, so this is the budget
     * for the whole iteration — a sibling cycle runs out instead of looping.
     */
    cursor->remaining = slot->nodes.used;
    cursor->version = slot->tree_version;
    return ROBO_OK;
}

robo_result_t robo_html_child_cursor_next(robo_html_child_cursor_t *cursor,
                                          robo_html_node_handle_t *out,
                                          robo_html_diag_t *diag)
{
    const robo_html_node_entry_t *e;

    if (out) {
        robo_html_node_handle_clear(out);
    }
    if (!cursor || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                              ROBO_INVALID_ARGUMENT, "cursor or out is NULL");
    }
    if (cursor->next_id == ROBO_HTML_NODE_ID_NONE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_NOT_FOUND,
                              "cursor is exhausted");
    }
    {
        const robo_html_document_slot_t *slot =
            robo_html_document_slot_find(cursor->document_id);

        if (!slot) {
            /* The document went away; the cursor is inert, not dangerous. */
            robo_html_child_cursor_init(cursor);
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                                  ROBO_NOT_FOUND,
                                  "document was destroyed during iteration");
        }
        /*
         * The child list has been rewritten since this cursor was opened, so
         * @c next_id may name a node that is no longer a child of the node
         * the cursor came from. Handing it out would report a child the
         * parent does not have; stopping says so instead.
         */
        if (slot->tree_version != cursor->version) {
            robo_html_child_cursor_init(cursor);
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION, ROBO_ERROR,
                                  "the tree was structurally changed during "
                                  "iteration; restart it");
        }
    }
    e = robo_html_node_entry_get(cursor->document_id, cursor->next_id);
    if (!e) {
        robo_html_child_cursor_init(cursor);
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_NOT_FOUND,
                              "child link does not resolve to a live node");
    }
    if (cursor->remaining == 0u) {
        robo_html_child_cursor_init(cursor);
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_ERROR,
                              "child list does not end: the link graph is not "
                              "a tree");
    }
    cursor->remaining--;
    cursor->next_id = e->next_sibling;
    *out = robo_html_node_handle_make(cursor->document_id, e);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* 4. Filters                                                          */
/* ------------------------------------------------------------------ */

int robo_html_node_filter_is_known(robo_html_node_filter_t filter)
{
    switch (filter) {
    case ROBO_HTML_NODE_FILTER_ALL:
    case ROBO_HTML_NODE_FILTER_ELEMENTS:
    case ROBO_HTML_NODE_FILTER_TEXT:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_node_filter_name(robo_html_node_filter_t filter)
{
    switch (filter) {
    case ROBO_HTML_NODE_FILTER_ALL:
        return "all";
    case ROBO_HTML_NODE_FILTER_ELEMENTS:
        return "elements";
    case ROBO_HTML_NODE_FILTER_TEXT:
        return "text";
    default:
        return "unknown";
    }
}

/** The rule, on a node kind the caller has already resolved. */
static int kind_matches(robo_html_node_kind_t kind,
                        robo_html_node_filter_t filter)
{
    switch (filter) {
    case ROBO_HTML_NODE_FILTER_ALL:
        return 1;
    case ROBO_HTML_NODE_FILTER_ELEMENTS:
        return kind == ROBO_HTML_NODE_KIND_ELEMENT;
    case ROBO_HTML_NODE_FILTER_TEXT:
        return kind == ROBO_HTML_NODE_KIND_TEXT;
    default:
        return 0;
    }
}

int robo_html_node_matches_filter(const robo_html_node_handle_t *node,
                                  robo_html_node_filter_t filter)
{
    const robo_html_node_entry_t *e = traversal_entry(node);

    if (!e) {
        return 0;
    }
    return kind_matches(e->kind, filter);
}

/* ------------------------------------------------------------------ */
/* 5. The walker                                                       */
/* ------------------------------------------------------------------ */

/**
 * The pre-order successor of @p current inside the subtree rooted at
 * @p start_id, maintaining @p depth.
 *
 * @return ROBO_OK (with *out_next set), ROBO_NOT_FOUND when the subtree is
 *         exhausted, ROBO_ERROR when an ascent leaves the node ceiling — the
 *         only way this cannot terminate is a corrupted graph, so that is
 *         reported rather than guessed at.
 */
static robo_result_t advance(robo_html_document_id_t document_id,
                             robo_html_node_id_t start_id,
                             const robo_html_node_entry_t *current,
                             size_t *depth,
                             robo_html_node_id_t *out_next,
                             robo_html_diag_t *diag)
{
    const robo_html_node_entry_t *e = current;
    size_t ascents = 0u;

    if (current->first_child != ROBO_HTML_NODE_ID_NONE) {
        *out_next = current->first_child;
        (*depth)++;
        return ROBO_OK;
    }
    for (;;) {
        if (e->id == start_id) {
            return ROBO_NOT_FOUND; /* back at the traversal root: done */
        }
        if (e->next_sibling != ROBO_HTML_NODE_ID_NONE) {
            *out_next = e->next_sibling;
            return ROBO_OK;
        }
        if (e->parent == ROBO_HTML_NODE_ID_NONE) {
            return ROBO_NOT_FOUND;
        }
        if (ascents++ >= (size_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_ERROR,
                                  "ascent does not end: the link graph is not "
                                  "a tree");
        }
        e = robo_html_node_entry_get(document_id, e->parent);
        if (!e) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_ERROR,
                                  "parent link does not resolve to a live node");
        }
        if (*depth == 0u) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_ERROR,
                                  "ascent left the traversal root");
        }
        (*depth)--;
    }
}

/**
 * The shared walk. @p include_start decides whether @p start itself is
 * visited; it is always stepped from, so the enumeration is the same
 * sequence either way.
 */
static robo_result_t walk(const robo_html_node_handle_t *start,
                          int include_start,
                          robo_html_node_filter_t filter,
                          robo_html_node_visit_fn visit,
                          void *user,
                          robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *start_entry;
    const robo_html_node_entry_t *current;
    robo_html_node_id_t start_id;
    size_t depth = 0u;
    size_t budget;
    uint32_t version;
    size_t steps = 0u;
    int at_start = 1;
    robo_result_t r;

    if (!visit) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                              ROBO_INVALID_ARGUMENT, "visit is NULL");
    }
    if (!robo_html_node_filter_is_known(filter)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                              ROBO_INVALID_ARGUMENT,
                              "filter is not a defined enumerator");
    }
    r = node_resolve(start, &start_entry, diag);
    if (r != ROBO_OK) {
        return r;
    }
    slot = robo_html_document_slot_find(start->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    /* Depth is reported from the document node, so establish it once. */
    r = ascent_top(start->document_id, start_entry, NULL, &depth, diag);
    if (r != ROBO_OK) {
        return r;
    }

    start_id = start_entry->id;
    current = start_entry;
    /*
     * A correct pre-order walk visits each live node at most once, so the
     * registry size is a hard upper bound. Tripping it means the links form a
     * cycle: the walk stops with an error rather than handing the caller a
     * silently partial tree.
     */
    budget = slot->nodes.used + 1u;
    /* The shape this walk is reading; see the post-visitor check below. */
    version = robo_html_document_tree_version(start->document_id);

    for (;;) {
        robo_html_node_id_t next_id;

        if (steps++ >= budget) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_ERROR,
                                  "walk exceeded the node ceiling: the link "
                                  "graph is not a tree");
        }
        /*
         * Re-checked every step because a visitor may destroy or reset the
         * document. The registry is module-owned fixed storage, so `current`
         * stays readable, but the tree it describes is gone and continuing
         * would report nodes that no longer exist.
         */
        if (!robo_html_document_slot_find(start->document_id)) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                                  ROBO_NOT_FOUND,
                                  "document was destroyed during traversal");
        }
        /* Skipping the start is a decision about visiting, not stepping. */
        if (!(at_start && !include_start) &&
            kind_matches(current->kind, filter)) {
            robo_html_node_handle_t h =
                robo_html_node_handle_make(start->document_id, current);
            robo_html_node_view_t view;

            r = robo_html_node_get_view(&h, &view, diag);
            if (r != ROBO_OK) {
                return r;
            }
            r = visit(&view, depth, user);
            if (r != ROBO_OK) {
                return r;
            }
            /*
             * Re-checked immediately after the visitor too, not only at the
             * top of the loop: a visitor that destroyed the document must be
             * reported here rather than after a step that reads links
             * belonging to a document that no longer exists.
             */
            if (!robo_html_document_slot_find(start->document_id)) {
                return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                                      ROBO_NOT_FOUND,
                                      "document was destroyed during "
                                      "traversal");
            }
            /*
             * A visitor may mutate the tree (that is the 8.9 phase's whole
             * point), which invalidates the walk: the links this loop is
             * about to step through describe the old shape, so continuing
             * would visit a node that has moved — or, if the visitor
             * detached the subtree, walk straight out of the traversal root.
             * Checked here, right where the mutation can only have come
             * from, so the report names the real cause.
             */
            if (robo_html_document_tree_version(start->document_id) !=
                version) {
                return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_MUTATION,
                                      ROBO_ERROR,
                                      "the tree was structurally changed "
                                      "during traversal; restart it");
            }
        }
        at_start = 0;

        r = advance(start->document_id, start_id, current, &depth, &next_id,
                    diag);
        if (r == ROBO_NOT_FOUND) {
            /* "Finished" is only true while the document still owns the
             * tree that was walked. */
            if (!robo_html_document_slot_find(start->document_id)) {
                return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                                      ROBO_NOT_FOUND,
                                      "document was destroyed during "
                                      "traversal");
            }
            return ROBO_OK;
        }
        if (r != ROBO_OK) {
            return r;
        }
        current = robo_html_node_entry_get(start->document_id, next_id);
        if (!current) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                                  ROBO_NOT_FOUND,
                                  "tree link does not resolve to a live node");
        }
    }
}

robo_result_t robo_html_node_walk_pre_order(
    const robo_html_node_handle_t *start,
    robo_html_node_filter_t filter,
    robo_html_node_visit_fn visit,
    void *user,
    robo_html_diag_t *diag)
{
    return walk(start, 1, filter, visit, user, diag);
}

robo_result_t robo_html_node_walk_descendants(
    const robo_html_node_handle_t *start,
    robo_html_node_filter_t filter,
    robo_html_node_visit_fn visit,
    void *user,
    robo_html_diag_t *diag)
{
    return walk(start, 0, filter, visit, user, diag);
}

/* --- search -------------------------------------------------------- */

/*
 * The predicate adapter. A found node stops the walk through the visitor's
 * own abort channel; ROBO_BUSY is the internal "stop, and this is success"
 * sentinel, translated below and never surfaced to a caller.
 */
typedef struct find_state {
    robo_html_node_predicate_fn match;
    void *user;
    robo_html_node_handle_t found;
} find_state_t;

static robo_result_t find_visitor(const robo_html_node_view_t *view,
                                  size_t depth,
                                  void *user)
{
    find_state_t *state = (find_state_t *)user;

    (void)depth;
    if (state->match && !state->match(view, state->user)) {
        return ROBO_OK;
    }
    state->found = view->handle;
    return ROBO_BUSY;
}

static robo_result_t find_from(const robo_html_node_handle_t *start,
                               int include_start,
                               robo_html_node_filter_t filter,
                               robo_html_node_predicate_fn match,
                               void *user,
                               robo_html_node_handle_t *out,
                               robo_html_diag_t *diag)
{
    find_state_t state;
    robo_result_t r;

    if (out) {
        robo_html_node_handle_clear(out);
    }
    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL,
                              ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    robo_html_node_handle_clear(&state.found);
    state.match = match;
    state.user = user;

    r = walk(start, include_start, filter, find_visitor, &state, diag);
    if (r == ROBO_BUSY) {
        *out = state.found;
        return ROBO_OK;
    }
    if (r != ROBO_OK) {
        return r;
    }
    return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_TRAVERSAL, ROBO_NOT_FOUND,
                          "no node matched");
}

robo_result_t robo_html_node_find_first(
    const robo_html_node_handle_t *start,
    robo_html_node_filter_t filter,
    robo_html_node_predicate_fn match,
    void *user,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag)
{
    return find_from(start, 1, filter, match, user, out, diag);
}

/*
 * Search restricted to the descendants of @p start, excluding @p start. This
 * is robo_html_node_find_element's documented contract, and it is the reason
 * the shared helper takes the flag: a name search started on a matching
 * element must not answer with that element.
 */
robo_result_t robo_html_node_find_descendant(
    const robo_html_node_handle_t *start,
    robo_html_node_filter_t filter,
    robo_html_node_predicate_fn match,
    void *user,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag)
{
    return find_from(start, 0, filter, match, user, out, diag);
}
