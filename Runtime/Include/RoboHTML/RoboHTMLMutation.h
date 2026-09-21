/*
 * RoboHTML DOM mutation (Phase 8.9).
 *
 * This is the authoritative way to change the shape of the tree the 8.3
 * tree builder built and the 8.4 document owns. Before this header the tree
 * was built once by the parser and then frozen for reading: the only child
 * link in the runtime was the parser's own append, and it refuses once a
 * build finishes. Now there are four real operations on it —
 *
 *   append_child      child becomes the parent's last child
 *   insert_before     child is placed immediately before a named sibling
 *   remove_child      child is detached, keeping its own subtree intact
 *   replace_child     a sibling slot is handed from one node to another
 *
 * and every one of them goes through the same single linking primitive as
 * the parser's, so a parser-built tree and a DOM-built tree cannot have
 * different shapes for the same intent.
 *
 * What is a node, and what is a parent
 *   - Operands are HtmlNodeHandle, like every other accessor in the
 *     runtime: identity, not access. Nothing here exposes the registry, the
 *     sibling links, the payload arena or the child-count field, so the
 *     storage behind a child list stays free to change.
 *   - A parent is any live node whose kind can hold children — the document
 *     node, an element, or a fragment. Everything else refuses
 *     RoboNotSupported, the same answer the parser's link gives.
 *   - The document node can never become a child. A document has exactly
 *     one root, and the 8.4 model establishes it; nothing here can replace,
 *     move or detach it.
 *
 * One parent, and only one
 *   - A node has at most one parent. Inserting a node that is already
 *     attached is RoboAlreadyExists — this API never silently steals a
 *     node from its current parent. An ownership transfer is therefore two
 *     explicit steps (remove, then insert), which is what makes it visible
 *     in the caller's code and in the tree-version history.
 *   - Detachment is not destruction. A removed node stays live in its
 *     document with its own subtree, attributes and text intact, and can be
 *     inserted again; the document owns every node it ever registered, so
 *     there is no orphan to free and no lifetime to manage. A detached
 *     subtree is traversable as a tree in its own right.
 *   - Because the parser is not running and a mutation only rewrites links,
 *     mutation works on a *finalized* document. Finalization closes
 *     construction (see RoboHTMLDocument.h); it does not lock the tree.
 *     What it does still control is node *creation*: registering a new node
 *     stays a construction operation, so this phase rearranges the nodes a
 *     document already has. Creation of brand-new nodes belongs with the
 *     document-lifecycle work that owns the construction gate.
 *
 * The result is always a tree
 *   - Inserting a node beneath itself or beneath one of its own descendants
 *     is refused (RoboInvalidArgument). A cycle would make the node
 *     reachable from itself, and every consumer of the tree — traversal,
 *     layout, paint, a future query engine — assumes it can terminate.
 * - Every rejection happens before the first link is written, so a failed
 *     mutation leaves the tree *exactly* as it was: no half-updated sibling
 *     chain, no parent pointing at a node that does not list it. The tree
 *     version does not move either, so a caller can use it as proof that a
 *     refused call changed nothing.
 *
 * Cross-document mutation is refused, not guessed
 *   - Every operand must belong to the same document. A handle naming a
 *     different document is RoboNotSupported (adopting or importing a node
 *     into another document is a separate operation with its own rules, and
 *     this phase does not invent a partial version of it).
 *   - A handle that belongs to the right document but no longer resolves —
 *     stale generation, unknown id, dropped tree — is RoboNotFound, the
 *     same answer every other accessor gives.
 *
 * Structural change is observable, and it invalidates iteration
 *   - Every successful mutation bumps the document's tree version, a
 *     monotonic counter (see HtmlNodeTreeVersion). It exists because
 *     an iteration that is walking a child list or a subtree cannot survive
 *     the list being rewritten underneath it: the cursor would hand out a
 *     node that is no longer a child, or step into a detached subtree.
 *   - The rule, stated once: a traversal may not be live across a structural
 *     mutation. The 8.8 cursor and walkers snapshot the version and stop
 *     with a MUTATION-domain diagnostic the moment it moves, rather than
 *     returning a node that is no longer where it was read from. Mutation
 *     from inside a visitor is therefore detected immediately, and the
 *     answer is "restart the iteration", not a silent wrong answer.
 *   - Mutation of a *different* document cannot affect an iteration: the
 *     version is per-document.
 *
 * What this phase is NOT: createElement / createTextNode (node creation
 * stays with the parser and the construction gate), cloneNode, the DOM
 * `ChildNode`/`ParentNode` conveniences (prepend/after/replaceWith), the
 * HTML element behaviour phases, CSS, layout, paint, JavaScript bindings,
 * MutationObserver, Shadow DOM, Range and Selection. Those are later phases.
 */

#ifndef ROBO_HTML_MUTATION_H
#define ROBO_HTML_MUTATION_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Structural operations                                               */
/* ------------------------------------------------------------------ */

/**
 * Make @p child the last child of @p parent.
 *
 * @p child must be detached and must name the same document as @p parent.
 * Ordering, sibling links and the parent's child count are updated
 * together; the child keeps its own subtree, attributes and text.
 *
 * @return RoboOk;
 *         RoboInvalidArgument (NULL argument, ill-formed handle, @p child
 *         is @p parent, or @p child is an ancestor of @p parent);
 *         RoboNotFound (either handle does not resolve in its document);
 *         RoboNotSupported (@p child belongs to another document, @p
 *         child is the document node, or @p parent's kind cannot hold
 *         children);
 *         RoboAlreadyExists (@p child already has a parent).
 */
Result HtmlNodeAppendChild(const HtmlNodeHandle *parent,
                                          const HtmlNodeHandle *child,
                                          HtmlDiag *diag);

/**
 * Insert @p newChild into @p parent's child list immediately before
 * @p referenceChild.
 *
 * @p referenceChild must be a live child of @p parent — the call is
 * anchored to a node the parent actually lists, so "insert before" cannot
 * quietly become "append somewhere else". Inserting before the first child
 * makes @p newChild the new first child; before the last child leaves the
 * last child unchanged. All other rules match HtmlNodeAppendChild.
 *
 * @return RoboOk; the same codes as HtmlNodeAppendChild, plus
 *         RoboNotFound when @p referenceChild does not resolve or does
 *         not belong to @p parent.
 */
Result HtmlNodeInsertBefore(
    const HtmlNodeHandle *parent,
    const HtmlNodeHandle *newChild,
    const HtmlNodeHandle *referenceChild,
    HtmlDiag *diag);

/**
 * Detach @p child from @p parent.
 *
 * The parent must be the node that actually lists @p child; naming a
 * different parent is RoboNotFound rather than a silent removal. On
 * success the child is detached with its subtree, attributes and text
 * intact (nothing is destroyed — the document owns it until the document
 * is), the parent's first/last child links and child count are repaired,
 * and the child's former siblings close the gap.
 *
 * @return RoboOk;
 *         RoboInvalidArgument (NULL argument or ill-formed handle);
 *         RoboNotFound (either handle does not resolve, @p child is not a
 *         child of @p parent, or @p child is in another document).
 */
Result HtmlNodeRemoveChild(const HtmlNodeHandle *parent,
                                          const HtmlNodeHandle *child,
                                          HtmlDiag *diag);

/**
 * Replace @p oldChild with @p newChild in @p parent's child list.
 *
 * @p newChild takes over @p oldChild's exact position — first, middle or
 * last — and @p oldChild is detached (not destroyed). @p newChild must be
 * detached; if it is already attached the call is RoboAlreadyExists and
 * @p oldChild stays where it is, because a partially applied replacement
 * would lose a child.
 *
 * @return RoboOk; the same codes as HtmlNodeInsertBefore, plus
 *         RoboNotFound when @p oldChild does not belong to @p parent.
 */
Result HtmlNodeReplaceChild(
    const HtmlNodeHandle *parent,
    const HtmlNodeHandle *newChild,
    const HtmlNodeHandle *oldChild,
    HtmlDiag *diag);

/**
 * Detach every child of @p parent, leaving it valid and childless.
 *
 * The nodes are detached, never destroyed, and keep their own subtrees; a
 * caller can re-insert any of them. @p outRemoved is optional and receives
 * the number of children detached.
 *
 * This exists because "throw away this element's contents" is a real
 * operation (a future innerHTML, a re-render, a teardown) and doing it by
 * hand means re-reading the child list while emptying it.
 *
 * @return RoboOk (including a parent that was already childless);
 *         RoboInvalidArgument (NULL argument or ill-formed handle);
 *         RoboNotFound (unknown document, stale handle);
 *         RoboNotSupported (@p parent's kind cannot hold children).
 */
Result HtmlNodeRemoveChildren(
    const HtmlNodeHandle *parent,
    size_t *outRemoved,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

/**
 * Non-zero in @p outContains when @p node is @p ancestor or is reachable
 * from it by following child links.
 *
 * This is the predicate the cycle rule is built on, published so a caller
 * can ask *before* mutating rather than learn from a rejection — and so a
 * test can state an ancestry claim directly instead of inferring it from a
 * refusal. @p outContains is set to 0 first and only changed on RoboOk.
 *
 * @return RoboOk; RoboInvalidArgument (NULL argument or ill-formed
 *         handle); RoboNotFound (either handle does not resolve, or the
 *         two name different documents).
 */
Result HtmlNodeContains(const HtmlNodeHandle *ancestor,
                                      const HtmlNodeHandle *node,
                                      int *outContains,
                                      HtmlDiag *diag);

/**
 * Current structural version of @p documentId.
 *
 * Monotonic for the document's lifetime and bumped exactly once per
 * successful structural change: a parser link, a mutation above, a bulk
 * clear, or the tree being dropped. It is never 0 for a live document, so 0
 * unambiguously means "no such document".
 *
 * A consumer that walks a tree, caches a subtree, or holds a cursor can
 * record this and treat any change as "my view of the tree is stale" — that
 * is exactly what the 8.8 traversal does internally.
 *
 * @return RoboOk; RoboInvalidArgument (NULL @p outVersion);
 *         RoboNotFound (unknown or destroyed document).
 */
Result HtmlNodeTreeVersion(
    HtmlDocumentId documentId,
    uint32_t *outVersion,
    HtmlDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_MUTATION_H */
