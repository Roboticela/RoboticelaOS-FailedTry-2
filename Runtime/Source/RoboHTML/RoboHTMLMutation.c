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
static Result MutationEntry(const HtmlNodeHandle *h,
                                   HtmlDocumentId documentId,
                                   const HtmlNodeEntry **out,
                                   HtmlDiag *diag)
{
    const HtmlNodeEntry *e;

    if (!h || !HtmlNodeHandleIsWellformed(h)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboInvalidArgument,
                              "mutation operand is NULL or ill-formed");
    }
    /*
     * The claim, before the resolution: a handle from another document can
     * never be a valid operand, and saying so specifically is what makes the
     * refusal actionable (adoption is a different operation, not a bug in
     * the caller's ids).
     */
    if (h->documentId != documentId) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboNotSupported,
                              "cross-document mutation is not supported");
    }
    e = HtmlNodeEntryGet(documentId, h->nodeId);
    if (!e || e->generation != h->generation) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation, RoboNotFound,
                              "mutation operand does not resolve to a live "
                              "node");
    }
    *out = e;
    return RoboOk;
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
static Result CheckAttachment(const HtmlNodeEntry *parent,
                                     const HtmlNodeEntry *child,
                                     HtmlDocumentId documentId,
                                     HtmlDiag *diag)
{
    if (parent->id == child->id) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboInvalidArgument,
                              "a node cannot be its own child");
    }
    if (child->kind == RoboHtmlNodeKindDocument) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboNotSupported,
                              "the document node cannot become a child");
    }
    if (!HtmlNodeKindCanHoldChildren(parent->kind)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboNotSupported,
                              "node kind cannot have children");
    }
    if (child->parent != RoboHtmlNodeIdNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboAlreadyExists,
                              "node already has a parent");
    }
    if (HtmlNodeIsAncestorOf(documentId, child->id, parent->id)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboInvalidArgument,
                              "a node cannot be inserted beneath its own "
                              "descendant");
    }
    return RoboOk;
}

/** The document node's id: the operation anchor for a caller's operands. */
static HtmlDocumentId AnchorDocument(
    const HtmlNodeHandle *first)
{
    return (first && HtmlNodeHandleIsWellformed(first))
               ? first->documentId
               : RoboHtmlDocumentIdNone;
}

/** Emit the structural-change hook event for @p node. */
static void AnnounceMutation(HtmlDocumentId documentId,
                              const HtmlNodeHandle *node)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);
    HtmlMutationEvent ev;

    if (!slot) {
        return;
    }
    ev.kind = RoboHtmlMutationTreeMutated;
    ev.documentId = documentId;
    ev.node = node ? *node : slot->root;
    ev.lifecycle = slot->lifecycle;
    HtmlEmitMutation(&ev, slot);
}

/* ------------------------------------------------------------------ */
/* 2. Structural operations                                            */
/* ------------------------------------------------------------------ */

Result HtmlNodeAppendChild(const HtmlNodeHandle *parent,
                                          const HtmlNodeHandle *child,
                                          HtmlDiag *diag)
{
    HtmlDocumentId doc;
    const HtmlNodeEntry *p;
    const HtmlNodeEntry *c;
    Result r;

    doc = AnchorDocument(parent);
    r = MutationEntry(parent, doc, &p, diag);
    if (r != RoboOk) {
        return r;
    }
    r = MutationEntry(child, doc, &c, diag);
    if (r != RoboOk) {
        return r;
    }
    r = CheckAttachment(p, c, doc, diag);
    if (r != RoboOk) {
        return r;
    }
    r = HtmlNodeInsertRelative(doc, p->id, c->id,
                                       RoboHtmlNodeIdNone, diag);
    if (r != RoboOk) {
        return r;
    }
    AnnounceMutation(doc, child);
    return RoboOk;
}

Result HtmlNodeInsertBefore(
    const HtmlNodeHandle *parent,
    const HtmlNodeHandle *newChild,
    const HtmlNodeHandle *referenceChild,
    HtmlDiag *diag)
{
    HtmlDocumentId doc;
    const HtmlNodeEntry *p;
    const HtmlNodeEntry *n;
    const HtmlNodeEntry *ref;
    Result r;

    doc = AnchorDocument(parent);
    r = MutationEntry(parent, doc, &p, diag);
    if (r != RoboOk) {
        return r;
    }
    r = MutationEntry(newChild, doc, &n, diag);
    if (r != RoboOk) {
        return r;
    }
    /*
     * The reference is resolved here rather than left to the linking
     * primitive so that "the anchor is not a child of the parent" is
     * reported with the MUTATION domain and a message that names the
     * operation. The primitive enforces the same rule for every other
     * caller.
     */
    r = MutationEntry(referenceChild, doc, &ref, diag);
    if (r != RoboOk) {
        return r;
    }
    if (ref->parent != p->id) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation, RoboNotFound,
                              "reference child does not belong to the parent");
    }
    r = CheckAttachment(p, n, doc, diag);
    if (r != RoboOk) {
        return r;
    }
    r = HtmlNodeInsertRelative(doc, p->id, n->id, ref->id, diag);
    if (r != RoboOk) {
        return r;
    }
    AnnounceMutation(doc, newChild);
    return RoboOk;
}

Result HtmlNodeRemoveChild(const HtmlNodeHandle *parent,
                                          const HtmlNodeHandle *child,
                                          HtmlDiag *diag)
{
    HtmlDocumentId doc;
    const HtmlNodeEntry *p;
    const HtmlNodeEntry *c;
    Result r;

    doc = AnchorDocument(parent);
    r = MutationEntry(parent, doc, &p, diag);
    if (r != RoboOk) {
        return r;
    }
    r = MutationEntry(child, doc, &c, diag);
    if (r != RoboOk) {
        return r;
    }
    if (c->parent != p->id) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation, RoboNotFound,
                              "node is not a child of the given parent");
    }
    r = HtmlNodeUnlink(doc, c->id, diag);
    if (r != RoboOk) {
        return r;
    }
    AnnounceMutation(doc, child);
    return RoboOk;
}

Result HtmlNodeReplaceChild(
    const HtmlNodeHandle *parent,
    const HtmlNodeHandle *newChild,
    const HtmlNodeHandle *oldChild,
    HtmlDiag *diag)
{
    HtmlDocumentId doc;
    const HtmlNodeEntry *p;
    const HtmlNodeEntry *n;
    const HtmlNodeEntry *o;
    HtmlNodeId successor;
    Result r;

    doc = AnchorDocument(parent);
    r = MutationEntry(parent, doc, &p, diag);
    if (r != RoboOk) {
        return r;
    }
    r = MutationEntry(newChild, doc, &n, diag);
    if (r != RoboOk) {
        return r;
    }
    r = MutationEntry(oldChild, doc, &o, diag);
    if (r != RoboOk) {
        return r;
    }
    if (n->id == o->id) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboInvalidArgument,
                              "a node cannot replace itself");
    }
    if (o->parent != p->id) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation, RoboNotFound,
                              "node is not a child of the given parent");
    }
    /*
     * Every rule that could refuse the insertion is checked BEFORE the old
     * child is detached. A replacement that detached the old node and then
     * failed to insert the new one would have dropped a child on the floor,
     * which is exactly the half-applied mutation this ordering prevents.
     */
    r = CheckAttachment(p, n, doc, diag);
    if (r != RoboOk) {
        return r;
    }
    /* Captured before the detach: unlink clears the old node's own links. */
    successor = o->NextSibling;

    r = HtmlNodeUnlink(doc, o->id, diag);
    if (r != RoboOk) {
        return r;
    }
    /*
     * The successor is still a child of the parent — detaching the old node
     * only rewired its neighbours — so appending is the correct action when
     * the old node was last, and inserting before the successor reproduces
     * its exact former position otherwise.
     */
    r = HtmlNodeInsertRelative(doc, p->id, n->id, successor, diag);
    if (r != RoboOk) {
        return r;
    }
    AnnounceMutation(doc, newChild);
    return RoboOk;
}

Result HtmlNodeRemoveChildren(
    const HtmlNodeHandle *parent,
    size_t *outRemoved,
    HtmlDiag *diag)
{
    HtmlDocumentId doc;
    const HtmlNodeEntry *p;
    HtmlNodeHandle first;
    size_t removed = 0u;
    Result r;

    if (outRemoved) {
        *outRemoved = 0u;
    }
    doc = AnchorDocument(parent);
    r = MutationEntry(parent, doc, &p, diag);
    if (r != RoboOk) {
        return r;
    }
    if (!HtmlNodeKindCanHoldChildren(p->kind)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboNotSupported,
                              "node kind cannot have children");
    }
    /*
     * Always re-reading the parent's first child (rather than stepping the
     * chain) is what makes this safe: each detach repairs the list, so the
     * next first child is whatever remains. The loop is bounded by the node
     * ceiling so a corrupted chain cannot spin.
     */
    while (removed < (size_t)RoboHtmlMaxNodesPerDocument) {
        const HtmlNodeEntry *live =
            HtmlNodeEntryGet(doc, p->id);

        if (!live || live->firstChild == RoboHtmlNodeIdNone) {
            break;
        }
        {
            const HtmlNodeEntry *child =
                HtmlNodeEntryGet(doc, live->firstChild);

            if (!child) {
                break;
            }
            /* A handle needs the generation too, and only the entry has it. */
            first = HtmlNodeHandleMake(doc, child);
        }
        r = HtmlNodeUnlink(doc, first.nodeId, diag);
        if (r != RoboOk) {
            return r;
        }
        removed++;
    }
    if (outRemoved) {
        *outRemoved = removed;
    }
    if (removed > 0u) {
        AnnounceMutation(doc, parent);
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 3. Queries                                                          */
/* ------------------------------------------------------------------ */

Result HtmlNodeContains(const HtmlNodeHandle *ancestor,
                                      const HtmlNodeHandle *node,
                                      int *outContains,
                                      HtmlDiag *diag)
{
    HtmlDocumentId doc;
    const HtmlNodeEntry *a;
    const HtmlNodeEntry *n;
    Result r;

    if (outContains) {
        *outContains = 0;
    }
    if (!outContains) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboInvalidArgument, "out_contains is NULL");
    }
    doc = AnchorDocument(ancestor);
    r = MutationEntry(ancestor, doc, &a, diag);
    if (r != RoboOk) {
        return r;
    }
    r = MutationEntry(node, doc, &n, diag);
    if (r != RoboOk) {
        return r;
    }
    *outContains = HtmlNodeIsAncestorOf(doc, a->id, n->id) ? 1 : 0;
    return RoboOk;
}

Result HtmlNodeTreeVersion(
    HtmlDocumentId documentId,
    uint32_t *outVersion,
    HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;

    if (!outVersion) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboInvalidArgument, "out_version is NULL");
    }
    *outVersion = 0u;
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation, RoboNotFound,
                              "unknown document id");
    }
    *outVersion = slot->treeVersion;
    return RoboOk;
}
