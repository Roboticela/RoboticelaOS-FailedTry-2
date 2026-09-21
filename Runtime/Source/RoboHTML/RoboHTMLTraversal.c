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
static const HtmlNodeEntry *TraversalEntry(
    const HtmlNodeHandle *h)
{
    const HtmlNodeEntry *e;

    if (!h || !HtmlNodeHandleIsWellformed(h)) {
        return NULL;
    }
    e = HtmlNodeEntryGet(h->documentId, h->nodeId);
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
static Result AscentTop(HtmlDocumentId documentId,
                                const HtmlNodeEntry *entry,
                                const HtmlNodeEntry **outTop,
                                size_t *outDepth,
                                HtmlDiag *diag)
{
    const HtmlNodeEntry *current = entry;
    HtmlNodeId id = entry->parent;
    size_t depth = 0u;

    while (id != RoboHtmlNodeIdNone) {
        const HtmlNodeEntry *up;

        if (depth >= (size_t)RoboHtmlMaxNodesPerDocument) {
            return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboError,
                                  "parent chain does not end: the link graph "
                                  "is not a tree");
        }
        up = HtmlNodeEntryGet(documentId, id);
        if (!up) {
            return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboError,
                                  "parent link does not resolve to a live node");
        }
        current = up;
        id = up->parent;
        depth++;
    }
    if (outTop) {
        *outTop = current;
    }
    if (outDepth) {
        *outDepth = depth;
    }
    return RoboOk;
}

static Result NodeResolve(const HtmlNodeHandle *node,
                                  const HtmlNodeEntry **out,
                                  HtmlDiag *diag)
{
    const HtmlNodeEntry *e;

    if (!node || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                              RoboInvalidArgument, "node or out is NULL");
    }
    if (!HtmlDocumentSlotFind(node->documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboNotFound,
                              "unknown document id");
    }
    e = TraversalEntry(node);
    if (!e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboNotFound,
                              "node handle does not resolve to a live node");
    }
    *out = e;
    return RoboOk;
}

Result HtmlNodeDepth(const HtmlNodeHandle *node,
                                   size_t *outDepth,
                                   HtmlDiag *diag)
{
    const HtmlNodeEntry *e;
    Result r;

    if (!outDepth) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                              RoboInvalidArgument, "out_depth is NULL");
    }
    *outDepth = 0u;
    r = NodeResolve(node, &e, diag);
    if (r != RoboOk) {
        return r;
    }
    return AscentTop(node->documentId, e, NULL, outDepth, diag);
}

Result HtmlNodeRoot(const HtmlNodeHandle *node,
                                  HtmlNodeHandle *out,
                                  HtmlDiag *diag)
{
    const HtmlNodeEntry *e;
    const HtmlNodeEntry *top = NULL;
    Result r;

    if (out) {
        HtmlNodeHandleClear(out);
    }
    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                              RoboInvalidArgument, "out is NULL");
    }
    r = NodeResolve(node, &e, diag);
    if (r != RoboOk) {
        return r;
    }
    r = AscentTop(node->documentId, e, &top, NULL, diag);
    if (r != RoboOk) {
        return r;
    }
    *out = HtmlNodeHandleMake(node->documentId, top);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 2. Single-link accessors                                            */
/* ------------------------------------------------------------------ */

typedef enum traversalLink {
    LINK_PARENT = 0,
    LINK_FIRST_CHILD = 1,
    LINK_LAST_CHILD = 2,
    LINK_NEXT_SIBLING = 3,
    LINK_PREV_SIBLING = 4
} TraversalLink;

static HtmlNodeId EntryLink(const HtmlNodeEntry *e,
                                      TraversalLink which)
{
    switch (which) {
    case LINK_PARENT:
        return e->parent;
    case LINK_FIRST_CHILD:
        return e->firstChild;
    case LINK_LAST_CHILD:
        return e->lastChild;
    case LINK_NEXT_SIBLING:
        return e->NextSibling;
    case LINK_PREV_SIBLING:
    default:
        return e->prevSibling;
    }
}

static const char *LinkMissingMessage(TraversalLink which)
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
static Result ReadLink(const HtmlNodeHandle *node,
                               TraversalLink which,
                               HtmlNodeHandle *out,
                               HtmlDiag *diag)
{
    const HtmlNodeEntry *e;
    const HtmlNodeEntry *target;
    HtmlNodeId id;
    Result r;

    if (out) {
        HtmlNodeHandleClear(out);
    }
    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                              RoboInvalidArgument, "out is NULL");
    }
    r = NodeResolve(node, &e, diag);
    if (r != RoboOk) {
        return r;
    }
    id = EntryLink(e, which);
    if (id == RoboHtmlNodeIdNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboNotFound,
                              LinkMissingMessage(which));
    }
    target = HtmlNodeEntryGet(node->documentId, id);
    if (!target) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboNotFound,
                              "tree link does not resolve to a live node");
    }
    *out = HtmlNodeHandleMake(node->documentId, target);
    return RoboOk;
}

Result HtmlNodeParent(const HtmlNodeHandle *node,
                                    HtmlNodeHandle *out,
                                    HtmlDiag *diag)
{
    return ReadLink(node, LINK_PARENT, out, diag);
}

Result HtmlNodeFirstChild(const HtmlNodeHandle *node,
                                         HtmlNodeHandle *out,
                                         HtmlDiag *diag)
{
    return ReadLink(node, LINK_FIRST_CHILD, out, diag);
}

Result HtmlNodeLastChild(const HtmlNodeHandle *node,
                                        HtmlNodeHandle *out,
                                        HtmlDiag *diag)
{
    return ReadLink(node, LINK_LAST_CHILD, out, diag);
}

Result HtmlNodeNextSibling(const HtmlNodeHandle *node,
                                          HtmlNodeHandle *out,
                                          HtmlDiag *diag)
{
    return ReadLink(node, LINK_NEXT_SIBLING, out, diag);
}

Result HtmlNodePrevSibling(const HtmlNodeHandle *node,
                                          HtmlNodeHandle *out,
                                          HtmlDiag *diag)
{
    return ReadLink(node, LINK_PREV_SIBLING, out, diag);
}

/* ------------------------------------------------------------------ */
/* 3. Child cursor                                                     */
/* ------------------------------------------------------------------ */

void HtmlChildCursorInit(HtmlChildCursor *cursor)
{
    if (!cursor) {
        return;
    }
    cursor->documentId = RoboHtmlDocumentIdNone;
    cursor->nextId = RoboHtmlNodeIdNone;
    cursor->remaining = 0u;
    cursor->version = 0u;
}

Result HtmlChildCursorOpen(HtmlChildCursor *cursor,
                                          const HtmlNodeHandle *parent,
                                          HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;
    Result r;

    if (!cursor) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                              RoboInvalidArgument, "cursor is NULL");
    }
    HtmlChildCursorInit(cursor);
    r = NodeResolve(parent, &e, diag);
    if (r != RoboOk) {
        return r;
    }
    slot = HtmlDocumentSlotFind(parent->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboNotFound,
                              "unknown document id");
    }
    cursor->documentId = parent->documentId;
    cursor->nextId = e->firstChild;
    /*
     * A child list cannot be longer than the registry, so this is the budget
     * for the whole iteration — a sibling cycle runs out instead of looping.
     */
    cursor->remaining = slot->nodes.used;
    cursor->version = slot->treeVersion;
    return RoboOk;
}

Result HtmlChildCursorNext(HtmlChildCursor *cursor,
                                          HtmlNodeHandle *out,
                                          HtmlDiag *diag)
{
    const HtmlNodeEntry *e;

    if (out) {
        HtmlNodeHandleClear(out);
    }
    if (!cursor || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                              RoboInvalidArgument, "cursor or out is NULL");
    }
    if (cursor->nextId == RoboHtmlNodeIdNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboNotFound,
                              "cursor is exhausted");
    }
    {
        const HtmlDocumentSlot *slot =
            HtmlDocumentSlotFind(cursor->documentId);

        if (!slot) {
            /* The document went away; the cursor is inert, not dangerous. */
            HtmlChildCursorInit(cursor);
            return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                                  RoboNotFound,
                                  "document was destroyed during iteration");
        }
        /*
         * The child list has been rewritten since this cursor was opened, so
         * @c nextId may name a node that is no longer a child of the node
         * the cursor came from. Handing it out would report a child the
         * parent does not have; stopping says so instead.
         */
        if (slot->treeVersion != cursor->version) {
            HtmlChildCursorInit(cursor);
            return RoboHtmlDiag(diag, RoboHtmlDomainMutation, RoboError,
                                  "the tree was structurally changed during "
                                  "iteration; restart it");
        }
    }
    e = HtmlNodeEntryGet(cursor->documentId, cursor->nextId);
    if (!e) {
        HtmlChildCursorInit(cursor);
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboNotFound,
                              "child link does not resolve to a live node");
    }
    if (cursor->remaining == 0u) {
        HtmlChildCursorInit(cursor);
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboError,
                              "child list does not end: the link graph is not "
                              "a tree");
    }
    cursor->remaining--;
    cursor->nextId = e->NextSibling;
    *out = HtmlNodeHandleMake(cursor->documentId, e);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 4. Filters                                                          */
/* ------------------------------------------------------------------ */

int HtmlNodeFilterIsKnown(HtmlNodeFilter filter)
{
    switch (filter) {
    case RoboHtmlNodeFilterAll:
    case RoboHtmlNodeFilterElements:
    case RoboHtmlNodeFilterText:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlNodeFilterName(HtmlNodeFilter filter)
{
    switch (filter) {
    case RoboHtmlNodeFilterAll:
        return "all";
    case RoboHtmlNodeFilterElements:
        return "elements";
    case RoboHtmlNodeFilterText:
        return "text";
    default:
        return "unknown";
    }
}

/** The rule, on a node kind the caller has already resolved. */
static int KindMatches(HtmlNodeKind kind,
                        HtmlNodeFilter filter)
{
    switch (filter) {
    case RoboHtmlNodeFilterAll:
        return 1;
    case RoboHtmlNodeFilterElements:
        return kind == RoboHtmlNodeKindElement;
    case RoboHtmlNodeFilterText:
        return kind == RoboHtmlNodeKindText;
    default:
        return 0;
    }
}

int HtmlNodeMatchesFilter(const HtmlNodeHandle *node,
                                  HtmlNodeFilter filter)
{
    const HtmlNodeEntry *e = TraversalEntry(node);

    if (!e) {
        return 0;
    }
    return KindMatches(e->kind, filter);
}

/* ------------------------------------------------------------------ */
/* 5. The walker                                                       */
/* ------------------------------------------------------------------ */

/**
 * The pre-order successor of @p current inside the subtree rooted at
 * @p startId, maintaining @p depth.
 *
 * @return RoboOk (with *outNext set), RoboNotFound when the subtree is
 *         exhausted, RoboError when an ascent leaves the node ceiling — the
 *         only way this cannot terminate is a corrupted graph, so that is
 *         reported rather than guessed at.
 */
static Result advance(HtmlDocumentId documentId,
                             HtmlNodeId startId,
                             const HtmlNodeEntry *current,
                             size_t *depth,
                             HtmlNodeId *outNext,
                             HtmlDiag *diag)
{
    const HtmlNodeEntry *e = current;
    size_t ascents = 0u;

    if (current->firstChild != RoboHtmlNodeIdNone) {
        *outNext = current->firstChild;
        (*depth)++;
        return RoboOk;
    }
    for (;;) {
        if (e->id == startId) {
            return RoboNotFound; /* back at the traversal root: done */
        }
        if (e->NextSibling != RoboHtmlNodeIdNone) {
            *outNext = e->NextSibling;
            return RoboOk;
        }
        if (e->parent == RoboHtmlNodeIdNone) {
            return RoboNotFound;
        }
        if (ascents++ >= (size_t)RoboHtmlMaxNodesPerDocument) {
            return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboError,
                                  "ascent does not end: the link graph is not "
                                  "a tree");
        }
        e = HtmlNodeEntryGet(documentId, e->parent);
        if (!e) {
            return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboError,
                                  "parent link does not resolve to a live node");
        }
        if (*depth == 0u) {
            return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboError,
                                  "ascent left the traversal root");
        }
        (*depth)--;
    }
}

/**
 * The shared walk. @p includeStart decides whether @p start itself is
 * visited; it is always stepped from, so the enumeration is the same
 * sequence either way.
 */
static Result walk(const HtmlNodeHandle *start,
                          int includeStart,
                          HtmlNodeFilter filter,
                          HtmlNodeVisitFn visit,
                          void *user,
                          HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *startEntry;
    const HtmlNodeEntry *current;
    HtmlNodeId startId;
    size_t depth = 0u;
    size_t budget;
    uint32_t version;
    size_t steps = 0u;
    int atStart = 1;
    Result r;

    if (!visit) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                              RoboInvalidArgument, "visit is NULL");
    }
    if (!HtmlNodeFilterIsKnown(filter)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                              RoboInvalidArgument,
                              "filter is not a defined enumerator");
    }
    r = NodeResolve(start, &startEntry, diag);
    if (r != RoboOk) {
        return r;
    }
    slot = HtmlDocumentSlotFind(start->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboNotFound,
                              "unknown document id");
    }
    /* Depth is reported from the document node, so establish it once. */
    r = AscentTop(start->documentId, startEntry, NULL, &depth, diag);
    if (r != RoboOk) {
        return r;
    }

    startId = startEntry->id;
    current = startEntry;
    /*
     * A correct pre-order walk visits each live node at most once, so the
     * registry size is a hard upper bound. Tripping it means the links form a
     * cycle: the walk stops with an error rather than handing the caller a
     * silently partial tree.
     */
    budget = slot->nodes.used + 1u;
    /* The shape this walk is reading; see the post-visitor check below. */
    version = HtmlDocumentTreeVersion(start->documentId);

    for (;;) {
        HtmlNodeId nextId;

        if (steps++ >= budget) {
            return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboError,
                                  "walk exceeded the node ceiling: the link "
                                  "graph is not a tree");
        }
        /*
         * Re-checked every step because a visitor may destroy or reset the
         * document. The registry is module-owned fixed storage, so `current`
         * stays readable, but the tree it describes is gone and continuing
         * would report nodes that no longer exist.
         */
        if (!HtmlDocumentSlotFind(start->documentId)) {
            return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                                  RoboNotFound,
                                  "document was destroyed during traversal");
        }
        /* Skipping the start is a decision about visiting, not stepping. */
        if (!(atStart && !includeStart) &&
            KindMatches(current->kind, filter)) {
            HtmlNodeHandle h =
                HtmlNodeHandleMake(start->documentId, current);
            HtmlNodeView view;

            r = HtmlNodeGetView(&h, &view, diag);
            if (r != RoboOk) {
                return r;
            }
            r = visit(&view, depth, user);
            if (r != RoboOk) {
                return r;
            }
            /*
             * Re-checked immediately after the visitor too, not only at the
             * top of the loop: a visitor that destroyed the document must be
             * reported here rather than after a step that reads links
             * belonging to a document that no longer exists.
             */
            if (!HtmlDocumentSlotFind(start->documentId)) {
                return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                                      RoboNotFound,
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
            if (HtmlDocumentTreeVersion(start->documentId) !=
                version) {
                return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                                      RoboError,
                                      "the tree was structurally changed "
                                      "during traversal; restart it");
            }
        }
        atStart = 0;

        r = advance(start->documentId, startId, current, &depth, &nextId,
                    diag);
        if (r == RoboNotFound) {
            /* "Finished" is only true while the document still owns the
             * tree that was walked. */
            if (!HtmlDocumentSlotFind(start->documentId)) {
                return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                                      RoboNotFound,
                                      "document was destroyed during "
                                      "traversal");
            }
            return RoboOk;
        }
        if (r != RoboOk) {
            return r;
        }
        current = HtmlNodeEntryGet(start->documentId, nextId);
        if (!current) {
            return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                                  RoboNotFound,
                                  "tree link does not resolve to a live node");
        }
    }
}

Result HtmlNodeWalkPreOrder(
    const HtmlNodeHandle *start,
    HtmlNodeFilter filter,
    HtmlNodeVisitFn visit,
    void *user,
    HtmlDiag *diag)
{
    return walk(start, 1, filter, visit, user, diag);
}

Result HtmlNodeWalkDescendants(
    const HtmlNodeHandle *start,
    HtmlNodeFilter filter,
    HtmlNodeVisitFn visit,
    void *user,
    HtmlDiag *diag)
{
    return walk(start, 0, filter, visit, user, diag);
}

/* --- search -------------------------------------------------------- */

/*
 * The predicate adapter. A found node stops the walk through the visitor's
 * own abort channel; RoboBusy is the internal "stop, and this is success"
 * sentinel, translated below and never surfaced to a caller.
 */
typedef struct findState {
    HtmlNodePredicateFn match;
    void *user;
    HtmlNodeHandle found;
} FindState;

static Result FindVisitor(const HtmlNodeView *view,
                                  size_t depth,
                                  void *user)
{
    FindState *state = (FindState *)user;

    (void)depth;
    if (state->match && !state->match(view, state->user)) {
        return RoboOk;
    }
    state->found = view->handle;
    return RoboBusy;
}

static Result FindFrom(const HtmlNodeHandle *start,
                               int includeStart,
                               HtmlNodeFilter filter,
                               HtmlNodePredicateFn match,
                               void *user,
                               HtmlNodeHandle *out,
                               HtmlDiag *diag)
{
    FindState state;
    Result r;

    if (out) {
        HtmlNodeHandleClear(out);
    }
    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal,
                              RoboInvalidArgument, "out is NULL");
    }
    HtmlNodeHandleClear(&state.found);
    state.match = match;
    state.user = user;

    r = walk(start, includeStart, filter, FindVisitor, &state, diag);
    if (r == RoboBusy) {
        *out = state.found;
        return RoboOk;
    }
    if (r != RoboOk) {
        return r;
    }
    return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboNotFound,
                          "no node matched");
}

Result HtmlNodeFindFirst(
    const HtmlNodeHandle *start,
    HtmlNodeFilter filter,
    HtmlNodePredicateFn match,
    void *user,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    return FindFrom(start, 1, filter, match, user, out, diag);
}

/*
 * Search restricted to the descendants of @p start, excluding @p start. This
 * is HtmlNodeFindElement's documented contract, and it is the reason
 * the shared helper takes the flag: a name search started on a matching
 * element must not answer with that element.
 */
Result HtmlNodeFindDescendant(
    const HtmlNodeHandle *start,
    HtmlNodeFilter filter,
    HtmlNodePredicateFn match,
    void *user,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    return FindFrom(start, 0, filter, match, user, out, diag);
}
