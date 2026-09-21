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

int HtmlDocumentOwnsNode(HtmlDocumentId documentId,
                                 const HtmlNodeHandle *h)
{
    const HtmlNodeEntry *e;

    if (!HtmlNodeHandleIsWellformed(h)) {
        return 0;
    }
    /*
     * The document field is checked first and by value: this one comparison
     * is what makes a cross-document handle a plain "no" instead of a
     * lookup that could alias a same-numbered node in the other document.
     */
    if (h->documentId != documentId) {
        return 0;
    }
    if (!HtmlDocumentSlotFind(documentId)) {
        return 0;
    }
    e = HtmlNodeEntryGet(documentId, h->nodeId);
    if (!e) {
        return 0;
    }
    return e->generation == h->generation;
}

Result HtmlDocumentNodeById(
    HtmlDocumentId documentId,
    HtmlNodeId nodeId,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTree,
                              RoboInvalidArgument, "out is NULL");
    }
    /* Cleared up front so a failure can never leak a usable-looking handle. */
    HtmlNodeHandleClear(out);
    if (nodeId == RoboHtmlNodeIdNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTree,
                              RoboInvalidArgument, "node id is none");
    }
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboNotFound, "unknown document id");
    }
    e = HtmlNodeEntryGet(documentId, nodeId);
    if (!e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTree, RoboNotFound,
                              "node id is not live in this document");
    }
    *out = HtmlNodeHandleMake(documentId, e);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 2. Metadata snapshot                                                */
/* ------------------------------------------------------------------ */

Result GetHtmlDocumentMetadata(
    HtmlDocumentId documentId,
    HtmlDocumentMetadata *out,
    size_t *outKindCount,
    HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    size_t i;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboInvalidArgument, "out is NULL");
    }
    memset(out, 0, sizeof(*out));
    if (outKindCount) {
        for (i = 0u; i < (size_t)RoboHtmlNodeKindCount; i++) {
            outKindCount[i] = 0u;
        }
    }
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboNotFound, "unknown document id");
    }

    out->id = slot->id;
    out->lifecycle = slot->lifecycle;
    out->previousLifecycle = slot->previousLifecycle;
    out->finalized = slot->finalized;
    out->isEmpty = HtmlDocumentIsEmpty(documentId);
    out->sourceKind = slot->sourceKind;
    out->sourceLength = slot->sourceLength;
    out->sourceName = slot->sourceName;
    out->encoding = slot->encoding;
    out->failDomain = slot->failDomain;
    out->failMessage = slot->failMessage;

    out->rootOk = 0;
    if (HtmlNodeHandleIsWellformed(&slot->root)) {
        const HtmlNodeEntry *rootE =
            HtmlNodeEntryGet(documentId, slot->root.nodeId);
        out->rootOk = (rootE != NULL &&
                        rootE->generation == slot->root.generation &&
                        rootE->kind == RoboHtmlNodeKindDocument &&
                        rootE->parent == RoboHtmlNodeIdNone)
                           ? 1
                           : 0;
    }

    for (i = 0u; i < (size_t)RoboHtmlMaxNodesPerDocument; i++) {
        const HtmlNodeEntry *e = &slot->nodes.slots[i];

        if (!e->inUse) {
            continue;
        }
        out->nodeCount++;
        if (outKindCount && (size_t)e->kind <
                                  (size_t)RoboHtmlNodeKindCount) {
            outKindCount[(size_t)e->kind]++;
        }
        switch (e->kind) {
        case RoboHtmlNodeKindElement:
            out->elementCount++;
            break;
        case RoboHtmlNodeKindText:
            out->textCount++;
            break;
        case RoboHtmlNodeKindComment:
            out->commentCount++;
            break;
        case RoboHtmlNodeKindDocumentType:
            out->doctypeCount++;
            break;
        default:
            break;
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 3. Construction closure and reuse                                   */
/* ------------------------------------------------------------------ */

Result HtmlDocumentFinalize(HtmlDocumentId documentId,
                                          HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown document id");
    }
    if (!HtmlDocumentLifecycleAllowsContent(slot->lifecycle)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainLifecycle, RoboError,
                              "lifecycle cannot accept document content");
    }
    if (slot->finalized) {
        return RoboOk; /* Idempotent: finalizing twice is not a failure. */
    }
    slot->finalized = 1;
    LogDebug("robo_html: doc=%llu finalized nodes=%llu",
                   (unsigned long long)documentId,
                   (unsigned long long)HtmlNodeRegistryCount(
                       &slot->nodes));
    (void)HtmlDiagSet(diag, RoboOk, RoboHtmlDomainDocument,
                             "document finalized", __FILE__, __LINE__);
    return RoboOk;
}

int HtmlDocumentIsFinalized(HtmlDocumentId documentId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return 0;
    }
    return slot->finalized;
}

int HtmlDocumentAllowsConstruction(
    HtmlDocumentId documentId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return 0;
    }
    return !slot->finalized &&
           HtmlDocumentLifecycleAllowsContent(slot->lifecycle);
}

Result HtmlDocumentReset(HtmlDocumentId documentId,
                                       HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);
    HtmlDocumentLifecycle life;
    Result r;

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown document id");
    }
    /*
     * A live parser or tree builder holds the tree's invariants in its own
     * state (the open-element stack, the trailing text run). Dropping the
     * tree under it would leave it linking into freed ids, so reset is
     * refused until the lease is released.
     */
    if (slot->leasedParser) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboBusy,
                              "a parser still holds the document lease");
    }
    if (!HtmlDocumentLifecycleAllowsContent(slot->lifecycle)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainLifecycle, RoboError,
                              "lifecycle cannot accept document content");
    }

    /* Drops the payload arena, every node, and reopens construction. */
    r = HtmlDocumentClearTree(slot, diag);
    if (r != RoboOk) {
        return r;
    }

    slot->sourceKind = RoboHtmlSourceKindNone;
    slot->sourceLength = 0u;
    slot->sourceName[0] = '\0';
    HtmlEncodingStateInit(&slot->encoding);
    slot->failDomain = RoboHtmlDomainNone;
    slot->failMessage = NULL;

    /*
     * Back to the "no source attached" resting state. CREATED is unreachable
     * from LOADING/INTERACTIVE/COMPLETE by design (the machine has no edge
     * back to CREATED), so a document that has loaded something settles
     * with an empty tree — which is exactly the state an empty source
     * produces, and that state is COMPLETE: nothing is attached and nothing
     * can arrive. A document that never loaded stays CREATED. Both accept a
     * new source; neither is FAILED or DESTROYED.
     *
     * A reset abandons whatever was in flight, so a loading document steps
     * through INTERACTIVE rather than jumping: it really does have a live
     * (empty) DOM before it is complete.
     *
     * Each step is asked for only when it is still outstanding. A document
     * that is *already* interactive needs the last step alone — re-requesting
     * INTERACTIVE is not an edge the machine has, and asking for it would fail
     * the reset after the tree had already been dropped, leaving the document
     * cleared but still marked interactive.
     */
    life = slot->lifecycle;
    if (life == RoboHtmlDocumentLifecycleLoading) {
        if (HtmlDocumentApplyTransition(
                slot, RoboHtmlDocumentLifecycleInteractive) != RoboOk) {
            return RoboHtmlDiag(diag, RoboHtmlDomainLifecycle, RoboError,
                                  "reset could not settle the document");
        }
        life = RoboHtmlDocumentLifecycleInteractive;
    }
    if (life == RoboHtmlDocumentLifecycleInteractive) {
        if (HtmlDocumentApplyTransition(
                slot, RoboHtmlDocumentLifecycleComplete) != RoboOk) {
            return RoboHtmlDiag(diag, RoboHtmlDomainLifecycle, RoboError,
                                  "reset could not settle the document");
        }
    }
    LogDebug("robo_html: doc=%llu reset (lifecycle=%s)",
                   (unsigned long long)documentId,
                   HtmlDocumentLifecycleName(slot->lifecycle));
    (void)HtmlDiagSet(diag, RoboOk, RoboHtmlDomainDocument,
                             "document reset", __FILE__, __LINE__);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 4. Tree invariant audit                                             */
/* ------------------------------------------------------------------ */

/** Registry index of @p e, for the scratch reachability bitmap. */
static size_t ModelEntryIndex(const HtmlDocumentSlot *slot,
                                const HtmlNodeEntry *e)
{
    return (size_t)(e - &slot->nodes.slots[0]);
}

static void ModelAuditChildren(const HtmlDocumentSlot *slot,
                                 const HtmlNodeEntry *e,
                                 HtmlTreeReport *rep);

/**
 * Mark and audit the detached subtree rooted at @p rootE (an unreached
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
static void ModelAuditDetachedSubtree(const HtmlDocumentSlot *slot,
                                        const HtmlNodeEntry *rootE,
                                        uint8_t *seen,
                                        HtmlTreeReport *rep)
{
    const HtmlNodeEntry *cur = rootE;
    size_t guard = 0u;

    seen[ModelEntryIndex(slot, rootE)] = 1u;
    for (;;) {
        const HtmlNodeEntry *next = NULL;

        if (guard++ > (size_t)RoboHtmlMaxNodesPerDocument) {
            rep->cycles++;
            return;
        }
        if (cur->firstChild != RoboHtmlNodeIdNone) {
            next = HtmlNodeEntryGet(slot->id, cur->firstChild);
        } else {
            const HtmlNodeEntry *up = cur;

            while (up != NULL && up->id != rootE->id) {
                if (up->NextSibling != RoboHtmlNodeIdNone) {
                    next = HtmlNodeEntryGet(slot->id, up->NextSibling);
                    break;
                }
                if (up->parent == RoboHtmlNodeIdNone) {
                    break;
                }
                up = HtmlNodeEntryGet(slot->id, up->parent);
            }
        }
        if (!next) {
            return;
        }
        {
            size_t idx = ModelEntryIndex(slot, next);

            if (seen[idx]) {
                /* Already reached: a cycle, or a node inside the document
                 * tree being claimed by a detached subtree. */
                rep->cycles++;
                return;
            }
            seen[idx] = 1u;
        }
        if (next->parent == RoboHtmlNodeIdNone) {
            rep->badParents++;
            return;
        }
        ModelAuditChildren(slot, next, rep);
        cur = next;
    }
}

/**
 * Verify @p e's child chain against its own bookkeeping links.
 * Reports into @p rep and returns nothing: an inconsistent chain is an
 * observation, not a failure of the audit.
 */
static void ModelAuditChildren(const HtmlDocumentSlot *slot,
                                 const HtmlNodeEntry *e,
                                 HtmlTreeReport *rep)
{
    HtmlNodeId prev = RoboHtmlNodeIdNone;
    HtmlNodeId c = e->firstChild;
    size_t count = 0u;

    while (c != RoboHtmlNodeIdNone &&
           count <= (size_t)RoboHtmlMaxNodesPerDocument) {
        const HtmlNodeEntry *ce =
            HtmlNodeEntryGet(slot->id, c);

        if (!ce) {
            rep->badLinks++;
            break;
        }
        if (ce->parent != e->id) {
            rep->badParents++;
        }
        if (ce->prevSibling != prev) {
            rep->siblingMismatches++;
        }
        prev = ce->id;
        c = ce->NextSibling;
        count++;
    }
    if (count != (size_t)e->childCount) {
        rep->badCounts++;
    }
    if (e->lastChild != prev) {
        rep->badCounts++;
    }
}

Result HtmlDocumentValidateTree(
    HtmlDocumentId documentId,
    HtmlTreeReport *out,
    HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *rootE;
    unsigned char *seen;
    HtmlNodeId id;
    size_t depth = 1u;
    size_t guard = 0u;
    size_t i;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTree,
                              RoboInvalidArgument, "out is NULL");
    }
    memset(out, 0, sizeof(*out));
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboNotFound, "unknown document id");
    }
    out->registered = HtmlNodeRegistryCount(&slot->nodes);

    /*
     * One byte per registry slot, so reachability is O(1) per visit and the
     * walk stays linear. Allocated rather than placed on the stack: the
     * ceiling is 4096 slots and this call must not risk the caller's stack.
     */
    seen = (unsigned char *)RoboCalloc(RoboHtmlMaxNodesPerDocument,
                                        sizeof(unsigned char));
    if (!seen) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTree,
                              RoboOutOfMemory,
                              "audit scratch allocation failed");
    }

    rootE = NULL;
    if (HtmlNodeHandleIsWellformed(&slot->root)) {
        rootE = HtmlNodeEntryGet(documentId, slot->root.nodeId);
        if (rootE && rootE->generation != slot->root.generation) {
            rootE = NULL;
        }
    }
    if (!rootE || rootE->kind != RoboHtmlNodeKindDocument ||
        rootE->parent != RoboHtmlNodeIdNone) {
        out->rootOk = 0;
        goto orphanSweep;
    }
    out->rootOk = 1;
    seen[ModelEntryIndex(slot, rootE)] = 1u;
    out->nodes = 1u;
    out->maxDepth = 0u;

    /*
     * Iterative pre-order walk over the child/sibling links. `depth` is
     * maintained incrementally: descending adds one, and ascending past a
     * node subtracts one — so a node's depth is always the number of
     * ancestor edges below the root. No recursion, and the iteration guard
     * means a corrupted link cycle terminates with @c cycles set.
     */
    id = rootE->firstChild;
    while (id != RoboHtmlNodeIdNone &&
           guard++ <= (size_t)RoboHtmlMaxNodesPerDocument) {
        const HtmlNodeEntry *e =
            HtmlNodeEntryGet(documentId, id);
        size_t idx;

        if (!e) {
            out->badLinks++;
            break;
        }
        idx = ModelEntryIndex(slot, e);
        if (seen[idx]) {
            /* Reached twice: a cycle, or two parents claiming one node. */
            out->cycles++;
            break;
        }
        seen[idx] = 1u;
        out->nodes++;
        if (depth > out->maxDepth) {
            out->maxDepth = depth;
        }
        if (e->parent == RoboHtmlNodeIdNone) {
            out->badParents++;
            break;
        }
        ModelAuditChildren(slot, e, out);

        if (e->firstChild != RoboHtmlNodeIdNone) {
            id = e->firstChild;
            depth++;
            continue;
        }
        /* Ascend to the next sibling at the nearest ancestor that has one. */
        {
            const HtmlNodeEntry *cur = e;

            id = RoboHtmlNodeIdNone;
            while (cur != NULL && cur->id != rootE->id) {
                if (cur->NextSibling != RoboHtmlNodeIdNone) {
                    id = cur->NextSibling;
                    break;
                }
                if (cur->parent == RoboHtmlNodeIdNone) {
                    break;
                }
                cur = HtmlNodeEntryGet(documentId, cur->parent);
                depth--;
                if (cur == NULL || cur->id == rootE->id) {
                    id = RoboHtmlNodeIdNone;
                    break;
                }
            }
        }
    }

orphanSweep:
    /*
     * Live nodes the root walk did not reach. A node with no parent is a
     * detached root: legal since Phase 8.9, where removing a child keeps the
     * node and its whole subtree alive and re-insertable. It is reported as
     * @c detached rather than as an inconsistency — but its subtree is
     * marked and audited, so a broken link *inside* a detached subtree is
     * still reported, and its descendants are not then miscounted.
     */
    for (i = 0u; i < (size_t)RoboHtmlMaxNodesPerDocument; i++) {
        const HtmlNodeEntry *e = &slot->nodes.slots[i];

        if (e->inUse && !seen[i] && e->parent == RoboHtmlNodeIdNone) {
            out->detached++;
            ModelAuditDetachedSubtree(slot, e, seen, out);
        }
    }
    /*
     * Whatever is still unseen claims a parent while nothing reachable (and
     * no detached root) lists it: a real orphan, and the shape a buggy
     * mutation leaves behind when it writes a parent link without putting
     * the node in a child list.
     */
    for (i = 0u; i < (size_t)RoboHtmlMaxNodesPerDocument; i++) {
        const HtmlNodeEntry *e = &slot->nodes.slots[i];

        if (e->inUse && !seen[i]) {
            out->orphans++;
        }
    }
    RoboFree(seen);

    out->ok = out->rootOk && out->cycles == 0u && out->badLinks == 0u &&
              out->badParents == 0u && out->badCounts == 0u &&
              out->siblingMismatches == 0u && out->orphans == 0u;
    if (!out->ok) {
        LogWarn(
            "robo_html: doc=%llu tree audit unclean: root_ok=%d "                  "registered=%llu nodes=%llu orphans=%llu detached=%llu "
                  "bad_links=%llu cycles=%llu bad_parents=%llu bad_counts=%llu "
                  "sibling=%llu",
            (unsigned long long)documentId, out->rootOk,
            (unsigned long long)out->registered,
            (unsigned long long)out->nodes, (unsigned long long)out->orphans,
            (unsigned long long)out->detached,
            (unsigned long long)out->badLinks, (unsigned long long)out->cycles,
            (unsigned long long)out->badParents,
            (unsigned long long)out->badCounts,
            (unsigned long long)out->siblingMismatches);
    }
    (void)HtmlDiagSet(diag, RoboOk, RoboHtmlDomainTree,
                             out->ok ? "tree invariants hold"
                                     : "tree invariants violated",
                             __FILE__, __LINE__);
    return RoboOk;
}
