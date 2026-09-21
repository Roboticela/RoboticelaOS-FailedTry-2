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
 *   - Every accessor here takes and returns HtmlNodeHandle. A handle
 *     is inert identity: it names a node, it does not grant access to one, and
 *     it cannot outlive the document that issued it in a meaningful way —
 *     a stale one simply fails to resolve.
 *   - Because handles are values, traversal never exposes the registry, the
 *     payload arena, the sibling links, or any container layout. A caller
 *     cannot learn how children are stored, which is what lets that storage
 *     change later without touching a single caller.
 *
 * Not-a-node is an answer, not a crash
 *   - Single-link accessors return RoboNotFound when the link does not
 *     exist (no parent, no previous sibling, past the last child) and clear
 *     @p out. That mirrors HtmlNodeChildAt's out-of-range contract, so
 *     "nothing there" is never a zeroed handle returned as success.
 *   - A NULL argument or a NULL output is RoboInvalidArgument; a handle that
 *     does not resolve is RoboNotFound.
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
 *     a cycle from corrupt links. That is reported as RoboError with a
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
 *     RoboError carrying a MUTATION-domain diagnostic the moment it moves.
 *     The answer is "restart the iteration", never a silently wrong node.
 *   - Mutation of a different document cannot affect an iteration, because
 *     the version is per-document; and a *refused* mutation does not move it,
 *     so an iteration is unaffected by a call that changed nothing.
 *   - A destroy or reset from inside a visitor is still reported as
 *     RoboNotFound (the document is gone entirely), which is why the two
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
 * The view snapshot (HtmlNodeGetView) carries the same links in one
 * call and is the better choice when a caller wants several; these exist for
 * the case where a caller wants exactly one step and would rather not decode
 * a node's name and text to get it.
 */

/**
 * Parent of @p node (any kind). The document node has no parent, so this
 * returns RoboNotFound for the root — "the document is the top" is an
 * answer, not a special case callers have to know about.
 *
 * Note the kind-checked siblings of this call: HtmlElementParent and
 * HtmlTextParent in RoboHTMLElement.h / RoboHTMLText.h fail on a node
 * of the wrong kind. Use this one when any node will do.
 */
Result HtmlNodeParent(const HtmlNodeHandle *node,
                                    HtmlNodeHandle *out,
                                    HtmlDiag *diag);

/** First child, or RoboNotFound when @p node has none. */
Result HtmlNodeFirstChild(const HtmlNodeHandle *node,
                                         HtmlNodeHandle *out,
                                         HtmlDiag *diag);

/** Last child, or RoboNotFound when @p node has none. */
Result HtmlNodeLastChild(const HtmlNodeHandle *node,
                                        HtmlNodeHandle *out,
                                        HtmlDiag *diag);

/** Next sibling, or RoboNotFound at the end of a child list. */
Result HtmlNodeNextSibling(const HtmlNodeHandle *node,
                                          HtmlNodeHandle *out,
                                          HtmlDiag *diag);

/** Previous sibling, or RoboNotFound at the start of a child list. */
Result HtmlNodePrevSibling(const HtmlNodeHandle *node,
                                          HtmlNodeHandle *out,
                                          HtmlDiag *diag);

/**
 * Depth of @p node, measured from the document node: the document node is 0,
 * its element child is 1, and so on. This is the same depth a walk reports.
 *
 * Computed by walking the parent links, so it costs one step per ancestor and
 * needs no stored field to keep in step. A parent chain that does not reach
 * the document node within the node ceiling is a corrupted link graph and is
 * reported as RoboError rather than looping.
 */
Result HtmlNodeDepth(const HtmlNodeHandle *node,
                                   size_t *outDepth,
                                   HtmlDiag *diag);

/**
 * Rootmost ancestor of @p node, i.e. the document node it belongs to. For a
 * node already at the top this is that node itself.
 *
 * Bounded like the depth walk, and for the same reason. This is the safe
 * "which document is this in?" question: the answer is a live node of the
 * document that owns it, so a caller can then ask
 * HtmlNodeHandleDocument for the identity without trusting the
 * handle's claim.
 */
Result HtmlNodeRoot(const HtmlNodeHandle *node,
                                  HtmlNodeHandle *out,
                                  HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Child iteration                                                     */
/* ------------------------------------------------------------------ */

/**
 * A one-pass cursor over a node's children, in sibling order.
 *
 * Why a cursor when childCount / child_at already exist: child_at resolves
 * child index 0 and then follows NextSibling @p index times, so iterating a
 * child list by index costs O(n²) over the list. The cursor carries the id of
 * the next child instead, so a full pass is O(n) with one registry lookup per
 * child and no index bookkeeping in the caller.
 *
 * The cursor holds a document id and a node id — no pointer, no borrowed
 * reference, nothing to free. It may safely be copied, and a copy continues
 * independently. It is exhausted when _next returns RoboNotFound, which is
 * also what it returns once the owning document is gone, so an abandoned
 * cursor is inert rather than dangerous.
 *
 * A cursor does not pin the node it walks. It does, however, remember the
 * tree version it was opened at (Phase 8.9), so a structural change during
 * the iteration is detected and reported rather than followed: the node the
 * cursor was about to hand out may have been detached, and a detached node
 * is not a child.
 */
typedef struct HtmlChildCursor {
    /** Owning document (RoboHtmlDocumentIdNone before _open). */
    HtmlDocumentId documentId;
    /** Next child to hand out; RoboHtmlNodeIdNone when exhausted. */
    HtmlNodeId nextId;
    /** Remaining steps before the walk is judged impossible (cycle guard). */
    size_t remaining;
    /**
     * Document tree version at _open (0 = not opened). _next refuses to
     * continue once it differs, because the child list it is reading has
     * been rewritten.
     */
    uint32_t version;
} HtmlChildCursor;

/** Clear @p cursor to the exhausted state. Safe on NULL. */
void HtmlChildCursorInit(HtmlChildCursor *cursor);

/**
 * Point @p cursor at the first child of @p parent.
 *
 * Opening a childless node SUCCEEDS and produces an immediately exhausted
 * cursor — an empty iteration is a normal result, so callers do not need a
 * special case before their loop.
 *
 * @return RoboOk; RoboInvalidArgument (NULL args); RoboNotFound
 *         (@p parent does not resolve).
 */
Result HtmlChildCursorOpen(HtmlChildCursor *cursor,
                                          const HtmlNodeHandle *parent,
                                          HtmlDiag *diag);

/**
 * Advance @p cursor: sets @p out to the next child and advances.
 * RoboNotFound when the cursor is exhausted (and for a cursor that was
 * never opened or whose document is gone); @p out is cleared in that case.
 *
 * RoboError means the iteration can no longer be trusted, and the
 * diagnostic's domain says which: TREE_MUTATED (a MUTATION-domain
 * diagnostic) means the tree was structurally changed while the cursor was
 * open — restart the iteration; a TRAVERSAL-domain diagnostic means the
 * child list did not end within the node ceiling, which is a corrupted link
 * graph rather than an empty list. The cursor is invalidated in both cases.
 */
Result HtmlChildCursorNext(HtmlChildCursor *cursor,
                                          HtmlNodeHandle *out,
                                          HtmlDiag *diag);

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
typedef enum HtmlNodeFilter {
    /** Every node: document, doctype, element, text, comment, fragment. */
    RoboHtmlNodeFilterAll = 0,
    /** Elements only. The foundation future CSS and DOM queries filter on. */
    RoboHtmlNodeFilterElements = 1,
    /** Text nodes only. The foundation text/layout consumers filter on. */
    RoboHtmlNodeFilterText = 2
} HtmlNodeFilter;

/** Non-zero if @p filter is a defined filter enumerator. */
int HtmlNodeFilterIsKnown(HtmlNodeFilter filter);

/** Human-readable filter name ("all", "elements", "text"); never NULL. */
const char *HtmlNodeFilterName(HtmlNodeFilter filter);

/**
 * Non-zero when @p node is a live node @p filter selects. Exposed so a caller
 * implementing its own loop applies the same rule the walkers do rather than
 * a second copy of it. False for NULL, stale and unknown-filter alike.
 */
int HtmlNodeMatchesFilter(const HtmlNodeHandle *node,
                                  HtmlNodeFilter filter);

/* ------------------------------------------------------------------ */
/* Walking                                                             */
/* ------------------------------------------------------------------ */

/**
 * Visitor for an enumeration walk.
 *
 * Receives the node as a @c HtmlNodeView — the snapshot the node
 * read side already returns, so a visitor gets kind, name, text, links and
 * child count from one resolve, and reaches attributes or kind-checked
 * operations through @c view->handle. The view is borrowed for the duration
 * of the call only; a visitor that must keep a value copies it.
 *
 * @p depth is the node's depth from the document node (document = 0), the
 * same number HtmlNodeDepth returns. Receiving it here is free: the
 * walk already knows whether it descended or ascended, so a visitor does not
 * have to walk the parent chain to indent, bucket or bound its work.
 *
 * Return RoboOk to continue, or any other code to stop the walk — that code
 * becomes the walk's return value. Stopping early is therefore a normal,
 * deliberate outcome and needs no separate "cancel" channel.
 */
typedef Result (*HtmlNodeVisitFn)(
    const HtmlNodeView *view,
    size_t depth,
    void *user);

/**
 * Predicate for a search. Non-zero means "this is the one".
 * @p user is the caller's context, unchanged.
 */
typedef int (*HtmlNodePredicateFn)(const HtmlNodeView *view,
                                           void *user);

/**
 * Walk @p start and its descendants in document order, visiting every node
 * @p filter selects (including @p start when it matches).
 *
 * @p visit must be non-NULL; @p user is passed through untouched (NULL is
 * allowed). @p diag is optional and unused on success.
 *
 * @return RoboOk when the walk completed; the visitor's own code when it
 *         stopped the walk; RoboInvalidArgument (NULL @p start or @p visit,
 *         unknown @p filter); RoboNotFound (@p start does not resolve, or
 *         the document was destroyed by the visitor); RoboError (the walk
 *         cannot be trusted — a MUTATION-domain diagnostic means the tree
 *         was structurally changed during the walk, a TRAVERSAL-domain one
 *         that it exceeded the node ceiling and so the link graph is not a
 *         tree).
 *
 * Reentrancy: a visitor may start its own walk — the walker holds no
 * document-wide state. What it must not do is change what it is walking:
 * destroying or resetting the document is reported as RoboNotFound, and a
 * structural mutation (RoboHTMLMutation.h) as RoboError with a
 * MUTATION-domain diagnostic, because the remaining steps would describe a
 * tree that no longer exists.
 */
Result HtmlNodeWalkPreOrder(
    const HtmlNodeHandle *start,
    HtmlNodeFilter filter,
    HtmlNodeVisitFn visit,
    void *user,
    HtmlDiag *diag);

/**
 * Same as HtmlNodeWalkPreOrder but skips @p start itself: the
 * "everything inside this node" walk. @p start must still resolve, so an
 * invalid start is an error rather than an empty walk — a caller that meant
 * "the contents of this node" gets told when there is no such node.
 */
Result HtmlNodeWalkDescendants(
    const HtmlNodeHandle *start,
    HtmlNodeFilter filter,
    HtmlNodeVisitFn visit,
    void *user,
    HtmlDiag *diag);

/**
 * First node at or below @p start that @p filter selects and @p match accepts,
 * in document order.
 *
 * @p match may be NULL, in which case the filter alone decides — which makes
 * this the "first element / first text node in this subtree" query. @p out is
 * cleared first and set only on RoboOk.
 *
 * @return RoboOk (found); RoboNotFound (nothing matched, or @p start does
 *         not resolve); RoboInvalidArgument; RoboError (the walk exceeded
 *         the node ceiling — see the walkers).
 *
 * This is what element-name search is built on, so a name search and a
 * filter search can never disagree about document order.
 */
Result HtmlNodeFindFirst(
    const HtmlNodeHandle *start,
    HtmlNodeFilter filter,
    HtmlNodePredicateFn match,
    void *user,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/**
 * As HtmlNodeFindFirst, but searching only the DESCENDANTS of
 * @p start, never @p start itself.
 *
 * The two exist because both questions are real and they differ exactly when
 * the start node would match: "the first element at or below here" and "the
 * first element inside here". HtmlNodeFindElement is the second, so a
 * name search started on an element of that name finds the one inside it
 * rather than answering with the node the caller already holds.
 *
 * @p start must still resolve — an invalid start is an error, not an empty
 * result — and the walk is bounded and ordered exactly as the walkers are.
 */
Result HtmlNodeFindDescendant(
    const HtmlNodeHandle *start,
    HtmlNodeFilter filter,
    HtmlNodePredicateFn match,
    void *user,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TRAVERSAL_H */
