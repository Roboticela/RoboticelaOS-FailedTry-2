/*
 * RoboHTML DOM traversal (Phase 8.8).
 *
 * This is the authoritative way to navigate the tree the 8.3 tree builder
 * built and the 8.4 document owns: relationships in both directions,
 * deterministic enumeration, and search. Before this header, three separate
 * places reimplemented the same iterative pre-order successor — element
 * search (8.3), subtree text (8.3), and the debug dump (8.3, recursively) —
 * and child iteration by index was quadratic. Those are now one
 * implementation, and the consumers go through it.
 *
 *   Document
 *     └── html
 *         └── body
 *             ├── Text        element-free character data still traverses
 *             └── div
 *                 └── span
 *                     └── Text  → upward: span, div, body, html, Document
 *
 * Relationships are handles, not pointers
 *   - Every accessor here takes and returns robo_html_node_handle_t. A handle
 *     is inert identity: it names a node, it does not grant access to one, and
 *     it cannot outlive the document that issued it in a meaningful way —
 *     a stale one simply fails to resolve.
 *   - Because handles are values, traversal never exposes the registry, the
 *     payload arena, the sibling links, or any container layout. A caller
 *     cannot learn how children are stored, which is what lets that storage
 *     change later without touching a single caller.
 *
 * Not-a-node is an answer, not a crash
 *   - Single-link accessors return ROBO_NOT_FOUND when the link does not
 *     exist (no parent, no previous sibling, past the last child) and clear
 *     @p out. That mirrors robo_html_node_child_at's out-of-range contract, so
 *     "nothing there" is never a zeroed handle returned as success.
 *   - A NULL argument or a NULL output is ROBO_INVALID_ARGUMENT; a handle that
 *     does not resolve is ROBO_NOT_FOUND.
 *
 * Deterministic order
 *   - Enumeration is pre-order (document order): a node, then its children in
 *     sibling order, each followed by its own subtree — parent before
 *     descendants, left to right. The order is a pure function of the tree
 *     links, so it cannot depend on hash iteration, pointer values, allocator
 *     behaviour or memory layout. Two walks over the same tree visit the same
 *     nodes in the same order, which is what later layout, paint, accessibility
 *     and query layers need and what makes it testable.
 *
 * Iterative, bounded, allocation-free
 *   - The walker descends and ascends the existing links with a bounded step
 *     counter. No recursion, so an adversarial depth (a 4096-node-deep tree)
 *     cannot exhaust the C stack; no allocation, so a walk cannot fail
 *     halfway for memory reasons; O(n) over the nodes it visits.
 *   - The step bound is the document's live node count. A correct tree cannot
 *     be walked past it, so exceeding it means the link graph is not a tree —
 *     a cycle from corrupt links. That is reported as ROBO_ERROR with a
 *     diagnostic rather than silently truncating the walk, because handing a
 *     consumer a partial tree without saying so is worse than failing.
 *
 * Lifetime
 *   - A traversal borrows nothing. There is no iterator object that can dangle
 *     and no callback receives an owned reference: a visitor gets a view
 *     snapshot valid for the duration of its call, and the child cursor holds
 *     a document id, a node id, a step budget and a tree version.
 *   - Nothing here keeps a document alive, so a walk cannot extend a
 *     document's lifetime past what the caller already holds.
 *
 * Structural change ends an iteration (Phase 8.9)
 *   - A cursor or a walk cannot survive the child list it is reading being
 *     rewritten underneath it: the stored next-node id would name a node that
 *     is no longer a child (it was detached, or moved elsewhere), and the
 *     iteration would report a child the parent does not have.
 *   - So the rule is stated once and enforced: a traversal may not be live
 *     across a structural mutation. Both the cursor and the walkers snapshot
 *     the document's tree version (see RoboHTMLMutation.h) and stop with a
 *     ROBO_ERROR carrying a MUTATION-domain diagnostic the moment it moves.
 *     The answer is "restart the iteration", never a silently wrong node.
 *   - Mutation of a different document cannot affect an iteration, because
 *     the version is per-document; and a *refused* mutation does not move it,
 *     so an iteration is unaffected by a call that changed nothing.
 *   - A destroy or reset from inside a visitor is still reported as
 *     ROBO_NOT_FOUND (the document is gone entirely), which is why the two
 *     cases have distinct codes and domains rather than one "iteration
 *     broken" answer.
 *
 * What this phase is NOT: DOM mutation lives in RoboHTMLMutation.h — this
 * header reads, it does not write. Also absent: createElement, cloneNode,
 * CSS selectors, querySelector/All, the CSSOM, JavaScript bindings, mutation
 * observers, Range, Selection, Shadow DOM and Web Components.
 */

#ifndef ROBO_HTML_TRAVERSAL_H
#define ROBO_HTML_TRAVERSAL_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Relationships                                                       */
/* ------------------------------------------------------------------ */

/*
 * The single-link accessors. Each resolves the handle, reads one link and
 * returns it as a handle; each clears @p out before it can fail, so a caller
 * that ignores the return code still cannot mistake a stale handle for a
 * fresh one.
 *
 * The view snapshot (robo_html_node_get_view) carries the same links in one
 * call and is the better choice when a caller wants several; these exist for
 * the case where a caller wants exactly one step and would rather not decode
 * a node's name and text to get it.
 */

/**
 * Parent of @p node (any kind). The document node has no parent, so this
 * returns ROBO_NOT_FOUND for the root — "the document is the top" is an
 * answer, not a special case callers have to know about.
 *
 * Note the kind-checked siblings of this call: robo_html_element_parent and
 * robo_html_text_parent in RoboHTMLElement.h / RoboHTMLText.h fail on a node
 * of the wrong kind. Use this one when any node will do.
 */
robo_result_t robo_html_node_parent(const robo_html_node_handle_t *node,
                                    robo_html_node_handle_t *out,
                                    robo_html_diag_t *diag);

/** First child, or ROBO_NOT_FOUND when @p node has none. */
robo_result_t robo_html_node_first_child(const robo_html_node_handle_t *node,
                                         robo_html_node_handle_t *out,
                                         robo_html_diag_t *diag);

/** Last child, or ROBO_NOT_FOUND when @p node has none. */
robo_result_t robo_html_node_last_child(const robo_html_node_handle_t *node,
                                        robo_html_node_handle_t *out,
                                        robo_html_diag_t *diag);

/** Next sibling, or ROBO_NOT_FOUND at the end of a child list. */
robo_result_t robo_html_node_next_sibling(const robo_html_node_handle_t *node,
                                          robo_html_node_handle_t *out,
                                          robo_html_diag_t *diag);

/** Previous sibling, or ROBO_NOT_FOUND at the start of a child list. */
robo_result_t robo_html_node_prev_sibling(const robo_html_node_handle_t *node,
                                          robo_html_node_handle_t *out,
                                          robo_html_diag_t *diag);

/**
 * Depth of @p node, measured from the document node: the document node is 0,
 * its element child is 1, and so on. This is the same depth a walk reports.
 *
 * Computed by walking the parent links, so it costs one step per ancestor and
 * needs no stored field to keep in step. A parent chain that does not reach
 * the document node within the node ceiling is a corrupted link graph and is
 * reported as ROBO_ERROR rather than looping.
 */
robo_result_t robo_html_node_depth(const robo_html_node_handle_t *node,
                                   size_t *out_depth,
                                   robo_html_diag_t *diag);

/**
 * Rootmost ancestor of @p node, i.e. the document node it belongs to. For a
 * node already at the top this is that node itself.
 *
 * Bounded like the depth walk, and for the same reason. This is the safe
 * "which document is this in?" question: the answer is a live node of the
 * document that owns it, so a caller can then ask
 * robo_html_node_handle_document for the identity without trusting the
 * handle's claim.
 */
robo_result_t robo_html_node_root(const robo_html_node_handle_t *node,
                                  robo_html_node_handle_t *out,
                                  robo_html_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Child iteration                                                     */
/* ------------------------------------------------------------------ */

/**
 * A one-pass cursor over a node's children, in sibling order.
 *
 * Why a cursor when child_count / child_at already exist: child_at resolves
 * child index 0 and then follows next_sibling @p index times, so iterating a
 * child list by index costs O(n²) over the list. The cursor carries the id of
 * the next child instead, so a full pass is O(n) with one registry lookup per
 * child and no index bookkeeping in the caller.
 *
 * The cursor holds a document id and a node id — no pointer, no borrowed
 * reference, nothing to free. It may safely be copied, and a copy continues
 * independently. It is exhausted when _next returns ROBO_NOT_FOUND, which is
 * also what it returns once the owning document is gone, so an abandoned
 * cursor is inert rather than dangerous.
 *
 * A cursor does not pin the node it walks. It does, however, remember the
 * tree version it was opened at (Phase 8.9), so a structural change during
 * the iteration is detected and reported rather than followed: the node the
 * cursor was about to hand out may have been detached, and a detached node
 * is not a child.
 */
typedef struct robo_html_child_cursor {
    /** Owning document (ROBO_HTML_DOCUMENT_ID_NONE before _open). */
    robo_html_document_id_t document_id;
    /** Next child to hand out; ROBO_HTML_NODE_ID_NONE when exhausted. */
    robo_html_node_id_t next_id;
    /** Remaining steps before the walk is judged impossible (cycle guard). */
    size_t remaining;
    /**
     * Document tree version at _open (0 = not opened). _next refuses to
     * continue once it differs, because the child list it is reading has
     * been rewritten.
     */
    uint32_t version;
} robo_html_child_cursor_t;

/** Clear @p cursor to the exhausted state. Safe on NULL. */
void robo_html_child_cursor_init(robo_html_child_cursor_t *cursor);

/**
 * Point @p cursor at the first child of @p parent.
 *
 * Opening a childless node SUCCEEDS and produces an immediately exhausted
 * cursor — an empty iteration is a normal result, so callers do not need a
 * special case before their loop.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL args); ROBO_NOT_FOUND
 *         (@p parent does not resolve).
 */
robo_result_t robo_html_child_cursor_open(robo_html_child_cursor_t *cursor,
                                          const robo_html_node_handle_t *parent,
                                          robo_html_diag_t *diag);

/**
 * Advance @p cursor: sets @p out to the next child and advances.
 * ROBO_NOT_FOUND when the cursor is exhausted (and for a cursor that was
 * never opened or whose document is gone); @p out is cleared in that case.
 *
 * ROBO_ERROR means the iteration can no longer be trusted, and the
 * diagnostic's domain says which: TREE_MUTATED (a MUTATION-domain
 * diagnostic) means the tree was structurally changed while the cursor was
 * open — restart the iteration; a TRAVERSAL-domain diagnostic means the
 * child list did not end within the node ceiling, which is a corrupted link
 * graph rather than an empty list. The cursor is invalidated in both cases.
 */
robo_result_t robo_html_child_cursor_next(robo_html_child_cursor_t *cursor,
                                          robo_html_node_handle_t *out,
                                          robo_html_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Filters                                                             */
/* ------------------------------------------------------------------ */

/**
 * Which nodes a walk visits.
 *
 * A filter selects what is *visited*; it never changes what is *stepped
 * through*. Walking for elements still descends into text nodes' positions so
 * that a text node between two elements cannot hide the second element —
 * filtering is a projection of the same single traversal order, which is what
 * keeps every filtered walk a subsequence of the document-order walk.
 */
typedef enum robo_html_node_filter {
    /** Every node: document, doctype, element, text, comment, fragment. */
    ROBO_HTML_NODE_FILTER_ALL = 0,
    /** Elements only. The foundation future CSS and DOM queries filter on. */
    ROBO_HTML_NODE_FILTER_ELEMENTS = 1,
    /** Text nodes only. The foundation text/layout consumers filter on. */
    ROBO_HTML_NODE_FILTER_TEXT = 2
} robo_html_node_filter_t;

/** Non-zero if @p filter is a defined filter enumerator. */
int robo_html_node_filter_is_known(robo_html_node_filter_t filter);

/** Human-readable filter name ("all", "elements", "text"); never NULL. */
const char *robo_html_node_filter_name(robo_html_node_filter_t filter);

/**
 * Non-zero when @p node is a live node @p filter selects. Exposed so a caller
 * implementing its own loop applies the same rule the walkers do rather than
 * a second copy of it. False for NULL, stale and unknown-filter alike.
 */
int robo_html_node_matches_filter(const robo_html_node_handle_t *node,
                                  robo_html_node_filter_t filter);

/* ------------------------------------------------------------------ */
/* Walking                                                             */
/* ------------------------------------------------------------------ */

/**
 * Visitor for an enumeration walk.
 *
 * Receives the node as a @c robo_html_node_view_t — the snapshot the node
 * read side already returns, so a visitor gets kind, name, text, links and
 * child count from one resolve, and reaches attributes or kind-checked
 * operations through @c view->handle. The view is borrowed for the duration
 * of the call only; a visitor that must keep a value copies it.
 *
 * @p depth is the node's depth from the document node (document = 0), the
 * same number robo_html_node_depth returns. Receiving it here is free: the
 * walk already knows whether it descended or ascended, so a visitor does not
 * have to walk the parent chain to indent, bucket or bound its work.
 *
 * Return ROBO_OK to continue, or any other code to stop the walk — that code
 * becomes the walk's return value. Stopping early is therefore a normal,
 * deliberate outcome and needs no separate "cancel" channel.
 */
typedef robo_result_t (*robo_html_node_visit_fn)(
    const robo_html_node_view_t *view,
    size_t depth,
    void *user);

/**
 * Predicate for a search. Non-zero means "this is the one".
 * @p user is the caller's context, unchanged.
 */
typedef int (*robo_html_node_predicate_fn)(const robo_html_node_view_t *view,
                                           void *user);

/**
 * Walk @p start and its descendants in document order, visiting every node
 * @p filter selects (including @p start when it matches).
 *
 * @p visit must be non-NULL; @p user is passed through untouched (NULL is
 * allowed). @p diag is optional and unused on success.
 *
 * @return ROBO_OK when the walk completed; the visitor's own code when it
 *         stopped the walk; ROBO_INVALID_ARGUMENT (NULL @p start or @p visit,
 *         unknown @p filter); ROBO_NOT_FOUND (@p start does not resolve, or
 *         the document was destroyed by the visitor); ROBO_ERROR (the walk
 *         cannot be trusted — a MUTATION-domain diagnostic means the tree
 *         was structurally changed during the walk, a TRAVERSAL-domain one
 *         that it exceeded the node ceiling and so the link graph is not a
 *         tree).
 *
 * Reentrancy: a visitor may start its own walk — the walker holds no
 * document-wide state. What it must not do is change what it is walking:
 * destroying or resetting the document is reported as ROBO_NOT_FOUND, and a
 * structural mutation (RoboHTMLMutation.h) as ROBO_ERROR with a
 * MUTATION-domain diagnostic, because the remaining steps would describe a
 * tree that no longer exists.
 */
robo_result_t robo_html_node_walk_pre_order(
    const robo_html_node_handle_t *start,
    robo_html_node_filter_t filter,
    robo_html_node_visit_fn visit,
    void *user,
    robo_html_diag_t *diag);

/**
 * Same as robo_html_node_walk_pre_order but skips @p start itself: the
 * "everything inside this node" walk. @p start must still resolve, so an
 * invalid start is an error rather than an empty walk — a caller that meant
 * "the contents of this node" gets told when there is no such node.
 */
robo_result_t robo_html_node_walk_descendants(
    const robo_html_node_handle_t *start,
    robo_html_node_filter_t filter,
    robo_html_node_visit_fn visit,
    void *user,
    robo_html_diag_t *diag);

/**
 * First node at or below @p start that @p filter selects and @p match accepts,
 * in document order.
 *
 * @p match may be NULL, in which case the filter alone decides — which makes
 * this the "first element / first text node in this subtree" query. @p out is
 * cleared first and set only on ROBO_OK.
 *
 * @return ROBO_OK (found); ROBO_NOT_FOUND (nothing matched, or @p start does
 *         not resolve); ROBO_INVALID_ARGUMENT; ROBO_ERROR (the walk exceeded
 *         the node ceiling — see the walkers).
 *
 * This is what element-name search is built on, so a name search and a
 * filter search can never disagree about document order.
 */
robo_result_t robo_html_node_find_first(
    const robo_html_node_handle_t *start,
    robo_html_node_filter_t filter,
    robo_html_node_predicate_fn match,
    void *user,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag);

/**
 * As robo_html_node_find_first, but searching only the DESCENDANTS of
 * @p start, never @p start itself.
 *
 * The two exist because both questions are real and they differ exactly when
 * the start node would match: "the first element at or below here" and "the
 * first element inside here". robo_html_node_find_element is the second, so a
 * name search started on an element of that name finds the one inside it
 * rather than answering with the node the caller already holds.
 *
 * @p start must still resolve — an invalid start is an error, not an empty
 * result — and the walk is bounded and ordered exactly as the walkers are.
 */
robo_result_t robo_html_node_find_descendant(
    const robo_html_node_handle_t *start,
    robo_html_node_filter_t filter,
    robo_html_node_predicate_fn match,
    void *user,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TRAVERSAL_H */
