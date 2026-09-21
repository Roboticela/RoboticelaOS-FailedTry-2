/*
 * RoboHTML node identity, registry, tree links and payload access
 * (Phase 8.1 identity/lifetime, Phase 8.3 tree).
 *
 * The registry lives inside the owning document. Node ids increase
 * monotonically per document and are never reused; generation bumps when
 * a registry slot is recycled, so handles copied before teardown resolve
 * as stale rather than aliasing a new node.
 *
 * Lookup cost: registration walks slots upward from empty and ids are
 * handed out in the same order, so in a normal document id == index + 1.
 * NodeLookup therefore probes that slot first (O(1)) and only falls back
 * to a scan when the layout does not match — which keeps tree building
 * linear instead of quadratic without depending on an invariant it cannot
 * check.
 */

#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTMLInternal.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

void HtmlNodeHandleClear(HtmlNodeHandle *h)
{
    if (!h) {
        return;
    }
    h->documentId = RoboHtmlDocumentIdNone;
    h->nodeId = RoboHtmlNodeIdNone;
    h->generation = 0u;
}

HtmlDocumentId HtmlNodeHandleDocument(
    const HtmlNodeHandle *h)
{
    if (!h) {
        return RoboHtmlDocumentIdNone;
    }
    return h->documentId;
}

int HtmlNodeHandleIsWellformed(const HtmlNodeHandle *h)
{
    if (!h) {
        return 0;
    }
    return h->documentId != RoboHtmlDocumentIdNone &&
           h->nodeId != RoboHtmlNodeIdNone && h->generation != 0u;
}

int HtmlNodeHandleEqual(const HtmlNodeHandle *a,
                                const HtmlNodeHandle *b)
{
    if (!a || !b) {
        return 0;
    }
    return a->documentId == b->documentId &&
           a->nodeId == b->nodeId && a->generation == b->generation;
}

HtmlNodeHandle HtmlNodeHandleMake(
    HtmlDocumentId documentId,
    const HtmlNodeEntry *entry)
{
    HtmlNodeHandle h;

    if (!entry) {
        HtmlNodeHandleClear(&h);
        return h;
    }
    h.documentId = documentId;
    h.nodeId = entry->id;
    h.generation = entry->generation;
    return h;
}

static const HtmlNodeEntry *NodeLookup(
    HtmlDocumentId documentId,
    HtmlNodeId nodeId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);
    size_t i;

    if (!slot || nodeId == RoboHtmlNodeIdNone) {
        return NULL;
    }
    /* Hot path: ids are handed out in slot order, so id - 1 is the slot. */
    if (nodeId <= (HtmlNodeId)RoboHtmlMaxNodesPerDocument) {
        const HtmlNodeEntry *e =
            &slot->nodes.slots[(size_t)(nodeId - 1u)];
        if (e->inUse && e->id == nodeId) {
            return e;
        }
    }
    /* Cold path: any layout the hot path cannot describe. */
    for (i = 0u; i < RoboHtmlMaxNodesPerDocument; i++) {
        const HtmlNodeEntry *e = &slot->nodes.slots[i];
        if (e->inUse && e->id == nodeId) {
            return e;
        }
    }
    return NULL;
}

const HtmlNodeEntry *HtmlNodeEntryGet(
    HtmlDocumentId documentId,
    HtmlNodeId nodeId)
{
    return NodeLookup(documentId, nodeId);
}

HtmlNodeEntry *HtmlNodeEntryMut(
    HtmlDocumentId documentId,
    HtmlNodeId nodeId)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot || nodeId == RoboHtmlNodeIdNone) {
        return NULL;
    }
    if (nodeId <= (HtmlNodeId)RoboHtmlMaxNodesPerDocument) {
        HtmlNodeEntry *e =
            &slot->nodes.slots[(size_t)(nodeId - 1u)];
        if (e->inUse && e->id == nodeId) {
            return e;
        }
    }
    {
        size_t i;
        for (i = 0u; i < RoboHtmlMaxNodesPerDocument; i++) {
            HtmlNodeEntry *e = &slot->nodes.slots[i];
            if (e->inUse && e->id == nodeId) {
                return e;
            }
        }
    }
    return NULL;
}

Result HtmlNodeHandleResolve(
    const HtmlNodeHandle *h,
    HtmlNodeKind *outKind)
{
    const HtmlNodeEntry *e;

    if (!HtmlNodeHandleIsWellformed(h)) {
        return RoboInvalidArgument;
    }
    e = NodeLookup(h->documentId, h->nodeId);
    if (!e) {
        return RoboNotFound;
    }
    if (e->generation != h->generation) {
        return RoboNotFound;
    }
    if (outKind) {
        *outKind = e->kind;
    }
    return RoboOk;
}

int HtmlNodeHandleIsStale(const HtmlNodeHandle *h)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (!HtmlNodeHandleIsWellformed(h)) {
        return 0;
    }
    slot = HtmlDocumentSlotFind(h->documentId);
    if (!slot) {
        /* Well-formed handle pointing at a destroyed/unknown document. */
        return 1;
    }
    e = NodeLookup(h->documentId, h->nodeId);
    if (!e) {
        return 1;
    }
    /*
     * The generation must be compared, not just the id: node ids restart
     * when a document's tree is cleared, so an id that resolves today can
     * be a *different* node than the one this handle was made for. Without
     * this check a stale handle would report itself as live and disagree
     * with HtmlNodeHandleResolve.
     */
    return e->generation != h->generation;
}

void HtmlNodeRegistryInit(HtmlNodeRegistry *reg)
{
    if (!reg) {
        return;
    }
    {
        size_t i;
        for (i = 0u; i < RoboHtmlMaxNodesPerDocument; i++) {
            HtmlNodeEntry *e = &reg->slots[i];
            memset(e, 0, sizeof(*e));
            e->kind = RoboHtmlNodeKindDocument;
            e->parent = RoboHtmlNodeIdNone;
            e->firstChild = RoboHtmlNodeIdNone;
            e->lastChild = RoboHtmlNodeIdNone;
            e->prevSibling = RoboHtmlNodeIdNone;
            e->NextSibling = RoboHtmlNodeIdNone;
        }
    }
    reg->used = 0u;
    reg->nextId = 1u;
    reg->nextGeneration = 1u;
}

size_t HtmlNodeRegistryCount(const HtmlNodeRegistry *reg)
{
    if (!reg) {
        return 0u;
    }
    return reg->used;
}

/* ------------------------------------------------------------------ */
/* Internal tree mutation                                              */
/* ------------------------------------------------------------------ */

void HtmlNodeSetPayload(HtmlNodeEntry *entry,
                                uint32_t offset,
                                uint32_t length)
{
    if (!entry) {
        return;
    }
    entry->payloadOffset = offset;
    entry->payloadLength = length;
}

int HtmlNodeKindCanHoldChildren(HtmlNodeKind kind)
{
    return kind == RoboHtmlNodeKindDocument ||
           kind == RoboHtmlNodeKindElement ||
           kind == RoboHtmlNodeKindFragment;
}

void HtmlDocumentNoteTreeChange(HtmlDocumentSlot *slot)
{
    if (!slot) {
        return;
    }
    slot->treeVersion++;
    if (slot->treeVersion == 0u) {
        slot->treeVersion = 1u; /* 0 stays reserved for "no document" */
    }
}

uint32_t HtmlDocumentTreeVersion(HtmlDocumentId documentId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    return slot ? slot->treeVersion : 0u;
}

int HtmlNodeIsAncestorOf(HtmlDocumentId documentId,
                                  HtmlNodeId ancestorId,
                                  HtmlNodeId nodeId)
{
    const HtmlNodeEntry *e;
    size_t steps = 0u;

    if (ancestorId == RoboHtmlNodeIdNone) {
        return 0;
    }
    e = HtmlNodeEntryGet(documentId, nodeId);
    if (!e) {
        return 0;
    }
    for (;;) {
        if (e->id == ancestorId) {
            return 1;
        }
        if (e->parent == RoboHtmlNodeIdNone) {
            return 0;
        }
        /* A parent chain cannot be longer than the registry; a longer one is
         * a corrupted graph, and answering "not an ancestor" is the honest
         * outcome rather than looping. */
        if (steps++ >= (size_t)RoboHtmlMaxNodesPerDocument) {
            return 0;
        }
        e = HtmlNodeEntryGet(documentId, e->parent);
        if (!e) {
            return 0;
        }
    }
}

Result HtmlNodeInsertRelative(HtmlDocumentId documentId,
                                             HtmlNodeId parentId,
                                             HtmlNodeId childId,
                                             HtmlNodeId referenceId,
                                             HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlNodeEntry *parent;
    HtmlNodeEntry *child;
    HtmlNodeEntry *ref = NULL;
    HtmlNodeEntry *prev = NULL;

    if (parentId == childId) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument,
                              "a node cannot be its own child");
    }
    parent = HtmlNodeEntryMut(documentId, parentId);
    if (!parent) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "parent node is not live");
    }
    child = HtmlNodeEntryMut(documentId, childId);
    if (!child) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "child node is not live");
    }
    if (child->kind == RoboHtmlNodeKindDocument) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotSupported,
                              "the document node cannot become a child");
    }
    if (!HtmlNodeKindCanHoldChildren(parent->kind)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotSupported,
                              "node kind cannot have children");
    }
    if (child->parent != RoboHtmlNodeIdNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboAlreadyExists,
                              "child already has a parent");
    }
    /*
     * The cycle gate. Inserting a node beneath one of its own descendants
     * would make it reachable from itself, which is not a tree any consumer
     * of it could terminate on — so it is refused before any link is written.
     */
    if (HtmlNodeIsAncestorOf(documentId, childId, parentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboInvalidArgument,
                              "a node cannot be inserted beneath its own "
                              "descendant");
    }

    if (referenceId == RoboHtmlNodeIdNone) {
        prev = (parent->lastChild == RoboHtmlNodeIdNone)
                   ? NULL
                   : HtmlNodeEntryMut(documentId, parent->lastChild);
    } else {
        ref = HtmlNodeEntryMut(documentId, referenceId);
        if (!ref) {
            return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                                  RoboNotFound,
                                  "reference child is not a live node");
        }
        if (ref->parent != parentId) {
            return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                                  RoboNotFound,
                                  "reference child does not belong to the "
                                  "parent");
        }
        prev = (ref->prevSibling == RoboHtmlNodeIdNone)
                   ? NULL
                   : HtmlNodeEntryMut(documentId, ref->prevSibling);
    }

    /* Nothing can fail from here: every rejection is behind us. */
    child->parent = parentId;
    child->NextSibling = (ref != NULL) ? ref->id : RoboHtmlNodeIdNone;
    child->prevSibling = (prev != NULL) ? prev->id : RoboHtmlNodeIdNone;
    if (prev != NULL) {
        prev->NextSibling = child->id;
    } else {
        parent->firstChild = child->id;
    }
    if (ref != NULL) {
        ref->prevSibling = child->id;
    } else {
        parent->lastChild = child->id;
    }
    parent->childCount++;

    slot = HtmlDocumentSlotFind(documentId);
    HtmlDocumentNoteTreeChange(slot);
    return RoboOk;
}

Result HtmlNodeLinkChild(HtmlDocumentId documentId,
                                        HtmlNodeId parentId,
                                        HtmlNodeId childId,
                                        HtmlDiag *diag)
{
    if (!HtmlDocumentAllowsConstruction(documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTree, RoboError,
                              "document tree is finalized");
    }
    return HtmlNodeInsertRelative(documentId, parentId, childId,
                                          RoboHtmlNodeIdNone, diag);
}

Result HtmlNodeUnlink(HtmlDocumentId documentId,
                                    HtmlNodeId nodeId,
                                    HtmlDiag *diag)
{
    HtmlNodeEntry *node = HtmlNodeEntryMut(documentId, nodeId);
    HtmlNodeEntry *parent;
    HtmlNodeEntry *prev;
    HtmlNodeEntry *next;

    if (!node) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "node is not live");
    }
    if (node->parent == RoboHtmlNodeIdNone) {
        return RoboOk; /* already detached */
    }
    parent = HtmlNodeEntryMut(documentId, node->parent);
    prev = (node->prevSibling == RoboHtmlNodeIdNone)
               ? NULL
               : HtmlNodeEntryMut(documentId, node->prevSibling);
    next = (node->NextSibling == RoboHtmlNodeIdNone)
               ? NULL
               : HtmlNodeEntryMut(documentId, node->NextSibling);

    if (prev != NULL) {
        prev->NextSibling = node->NextSibling;
    }
    if (next != NULL) {
        next->prevSibling = node->prevSibling;
    }
    if (parent != NULL) {
        if (parent->firstChild == node->id) {
            parent->firstChild = node->NextSibling;
        }
        if (parent->lastChild == node->id) {
            parent->lastChild = node->prevSibling;
        }
        if (parent->childCount > 0u) {
            parent->childCount--;
        }
        if (parent->firstChild == RoboHtmlNodeIdNone) {
            parent->lastChild = RoboHtmlNodeIdNone;
        }
    }
    node->parent = RoboHtmlNodeIdNone;
    node->prevSibling = RoboHtmlNodeIdNone;
    node->NextSibling = RoboHtmlNodeIdNone;
    HtmlDocumentNoteTreeChange(HtmlDocumentSlotFind(documentId));
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Read-side accessors                                                 */
/* ------------------------------------------------------------------ */

/**
 * Resolve @p h to its entry, checking the generation.
 * @return entry, or NULL for any failure (callers convert to a result).
 */
static const HtmlNodeEntry *NodeEntryFor(
    const HtmlNodeHandle *h)
{
    const HtmlNodeEntry *e;

    if (!HtmlNodeHandleIsWellformed(h)) {
        return NULL;
    }
    e = NodeLookup(h->documentId, h->nodeId);
    if (!e || e->generation != h->generation) {
        return NULL;
    }
    return e;
}

/** Borrowed payload header for @p e, or NULL when it has no payload. */
static const HtmlPayloadHeader *PayloadOf(
    const HtmlDocumentSlot *slot,
    const HtmlNodeEntry *e)
{
    const HtmlPayloadHeader *hdr;

    if (!slot || !e || e->payloadLength == 0u) {
        return NULL;
    }
    hdr = (const HtmlPayloadHeader *)HtmlDocumentArenaAt(
        slot, e->payloadOffset, e->payloadLength);
    if (!hdr || hdr->length + (uint32_t)sizeof(*hdr) != e->payloadLength) {
        return NULL;
    }
    return hdr;
}

/** Bytes just after a payload header. */
static const char *PayloadBytes(const HtmlPayloadHeader *hdr)
{
    return ((const char *)hdr) + sizeof(*hdr);
}

/**
 * Read the next NUL-terminated string from a payload cursor.
 * @p cursor is advanced past the terminator; the header's declared length
 * bounds the walk so a corrupt payload cannot run off the arena.
 */
static const char *PayloadTakeString(const char **cursor,
                                       const char *limit)
{
    const char *start = *cursor;
    const char *p = start;

    while (p < limit && *p != '\0') {
        p++;
    }
    if (p >= limit) {
        *cursor = limit;
        return start; /* unterminated: treated as ending at the limit */
    }
    *cursor = p + 1;
    return start;
}

Result HtmlNodeGetView(const HtmlNodeHandle *h,
                                      HtmlNodeView *out,
                                      HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;
    const HtmlPayloadHeader *hdr;

    if (!h || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "handle or out is NULL");
    }
    memset(out, 0, sizeof(*out));
    slot = HtmlDocumentSlotFind(h->documentId);
    e = NodeEntryFor(h);
    if (!slot || !e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "node handle does not resolve");
    }

    out->handle = *h;
    out->kind = e->kind;
    out->childCount = e->childCount;
    if (e->parent != RoboHtmlNodeIdNone) {
        const HtmlNodeEntry *p =
            NodeLookup(h->documentId, e->parent);
        if (p) {
            out->parent = HtmlNodeHandleMake(h->documentId, p);
        }
    }
    if (e->firstChild != RoboHtmlNodeIdNone) {
        const HtmlNodeEntry *c =
            NodeLookup(h->documentId, e->firstChild);
        if (c) {
            out->firstChild = HtmlNodeHandleMake(h->documentId, c);
        }
    }
    if (e->lastChild != RoboHtmlNodeIdNone) {
        const HtmlNodeEntry *c =
            NodeLookup(h->documentId, e->lastChild);
        if (c) {
            out->lastChild = HtmlNodeHandleMake(h->documentId, c);
        }
    }
    if (e->prevSibling != RoboHtmlNodeIdNone) {
        const HtmlNodeEntry *s =
            NodeLookup(h->documentId, e->prevSibling);
        if (s) {
            out->prevSibling = HtmlNodeHandleMake(h->documentId, s);
        }
    }
    if (e->NextSibling != RoboHtmlNodeIdNone) {
        const HtmlNodeEntry *s =
            NodeLookup(h->documentId, e->NextSibling);
        if (s) {
            out->NextSibling = HtmlNodeHandleMake(h->documentId, s);
        }
    }

    hdr = PayloadOf(slot, e);
    out->name = "";
    out->text = "";
    out->textLength = 0u;
    if (hdr) {
        const char *bytes = PayloadBytes(hdr);
        const char *limit = bytes + hdr->length;
        const char *cursor = bytes;

        switch (e->kind) {
        case RoboHtmlNodeKindElement:
        case RoboHtmlNodeKindDocumentType:
            out->name = PayloadTakeString(&cursor, limit);
            break;
        case RoboHtmlNodeKindText:
        case RoboHtmlNodeKindComment:
            out->text = bytes;
            out->textLength = hdr->length > 0u ? (size_t)hdr->length - 1u : 0u;
            if (out->textLength >= (size_t)hdr->length) {
                out->textLength = (size_t)hdr->length; /* unterminated */
            }
            break;
        default:
            break;
        }
        out->selfClosing =
            (hdr->flags & RoboHtmlPayloadFlagSelfClosing) ? 1 : 0;
    }
    return RoboOk;
}

Result HtmlNodeChildCount(const HtmlNodeHandle *h,
                                         size_t *outCount,
                                         HtmlDiag *diag)
{
    const HtmlNodeEntry *e;

    if (!h || !outCount) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "handle or out is NULL");
    }
    e = NodeEntryFor(h);
    if (!e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "node handle does not resolve");
    }
    *outCount = e->childCount;
    return RoboOk;
}

Result HtmlNodeChildAt(const HtmlNodeHandle *h,
                                      size_t index,
                                      HtmlNodeHandle *out,
                                      HtmlDiag *diag)
{
    const HtmlNodeEntry *e;
    const HtmlNodeEntry *child;
    size_t i;

    if (!h || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "handle or out is NULL");
    }
    HtmlNodeHandleClear(out);
    e = NodeEntryFor(h);
    if (!e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "node handle does not resolve");
    }
    child = (e->firstChild == RoboHtmlNodeIdNone)
                ? NULL
                : NodeLookup(h->documentId, e->firstChild);
    for (i = 0u; child != NULL && i < index; i++) {
        child = (child->NextSibling == RoboHtmlNodeIdNone)
                    ? NULL
                    : NodeLookup(h->documentId, child->NextSibling);
    }
    if (!child) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "child index out of range");
    }
    *out = HtmlNodeHandleMake(h->documentId, child);
    return RoboOk;
}

static int NodeNameIs(const HtmlDocumentSlot *slot,
                        const HtmlNodeEntry *e,
                        const char *name)
{
    const HtmlPayloadHeader *hdr = PayloadOf(slot, e);
    const char *bytes;
    const char *limit;
    const char *cursor;

    if (!hdr || e->kind != RoboHtmlNodeKindElement) {
        return 0;
    }
    bytes = PayloadBytes(hdr);
    limit = bytes + hdr->length;
    cursor = bytes;
    return strcmp(PayloadTakeString(&cursor, limit), name) == 0;
}

Result HtmlNodeFindChildByName(
    const HtmlNodeHandle *h,
    const char *name,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;
    const HtmlNodeEntry *child;

    if (!h || !name || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "NULL argument");
    }
    HtmlNodeHandleClear(out);
    slot = HtmlDocumentSlotFind(h->documentId);
    e = NodeEntryFor(h);
    if (!slot || !e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "node handle does not resolve");
    }
    child = (e->firstChild == RoboHtmlNodeIdNone)
                ? NULL
                : NodeLookup(h->documentId, e->firstChild);
    while (child != NULL) {
        if (NodeNameIs(slot, child, name)) {
            *out = HtmlNodeHandleMake(h->documentId, child);
            return RoboOk;
        }
        child = (child->NextSibling == RoboHtmlNodeIdNone)
                    ? NULL
                    : NodeLookup(h->documentId, child->NextSibling);
    }
    return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                          "no child element with that name");
}

/** Name query handed to the traversal predicate (no const cast needed). */
typedef struct elementNameQuery {
    const char *name;
} ElementNameQuery;

/**
 * Predicate for element search. The traversal view already carries the
 * element's decoded local name, so matching does not reach into the payload
 * — the same value an element-name read would return.
 */
static int ElementNameMatches(const HtmlNodeView *view, void *user)
{
    const ElementNameQuery *query = (const ElementNameQuery *)user;

    return strcmp(view->name, query->name) == 0;
}

Result HtmlNodeFindElement(const HtmlNodeHandle *h,
                                          const char *name,
                                          HtmlNodeHandle *out,
                                          HtmlDiag *diag)
{
    ElementNameQuery query;

    if (!h || !name || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "NULL argument");
    }
    query.name = name;
    /*
     * Element search is the traversal search with an element filter: the
     * walk, the document order and the cycle bound live in one place now
     * rather than in a third copy of the pre-order successor (8.8).
     * "Descendant" keeps this call's contract of excluding @p h itself.
     */
    return HtmlNodeFindDescendant(
        h, RoboHtmlNodeFilterElements, ElementNameMatches, &query, out,
        diag);
}

/** Accumulator for the subtree-text walk. */
typedef struct subtreeTextState {
    char *out;
    size_t cap;
    size_t written;
    size_t total;
} SubtreeTextState;

/**
 * Append one visited text node's bytes, NUL-terminating as it goes.
 *
 * @c total counts every byte offered, @c written only those that fit, so a
 * truncated result still reports its full length — the contract callers use
 * to detect truncation. Written down once here, this is what both the
 * whole-node case and the walk below share.
 */
static Result SubtreeTextVisit(const HtmlNodeView *view,
                                        size_t depth,
                                        void *user)
{
    SubtreeTextState *state = (SubtreeTextState *)user;
    size_t i;

    (void)depth;
    for (i = 0u; i < view->textLength; i++) {
        if (state->total + 1u < state->cap) {
            state->out[state->written++] = view->text[i];
        }
        state->total++;
    }
    state->out[state->written] = '\0';
    return RoboOk;
}

Result HtmlNodeSubtreeText(const HtmlNodeHandle *h,
                                          char *out,
                                          size_t outCap,
                                          size_t *outLength,
                                          HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *start;
    SubtreeTextState state;
    Result r;

    if (!h || !out || outCap == 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "out buffer is unusable");
    }
    out[0] = '\0';
    if (outLength) {
        *outLength = 0u;
    }
    slot = HtmlDocumentSlotFind(h->documentId);
    start = NodeEntryFor(h);
    if (!slot || !start) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "node handle does not resolve");
    }
    state.out = out;
    state.cap = outCap;
    state.written = 0u;
    state.total = 0u;

    if (start->kind == RoboHtmlNodeKindText ||
        start->kind == RoboHtmlNodeKindComment) {
        /*
         * The node IS the text: reading it back verbatim matches what
         * HtmlTextContent returns for the same node, because the walk
         * below skips its start.
         */
        const HtmlPayloadHeader *hdr = PayloadOf(slot, start);
        if (hdr) {
            const char *bytes = PayloadBytes(hdr);
            state.total = hdr->length > 0u ? (size_t)hdr->length - 1u : 0u;
            state.written = (state.total < outCap - 1u)
                                ? state.total
                                : outCap - 1u;
            memcpy(out, bytes, state.written);
            out[state.written] = '\0';
        }
    } else {
        /*
         * Text-only descendants, in document order. This replaces a third
         * copy of the pre-order successor (8.8): the order, the filter, the
         * cycle bound and the no-recursion rule now live in the walker.
         */
        r = HtmlNodeWalkDescendants(h, RoboHtmlNodeFilterText,
                                            SubtreeTextVisit, &state, diag);
        if (r != RoboOk) {
            if (outLength) {
                *outLength = state.total;
            }
            return r;
        }
    }
    if (outLength) {
        *outLength = state.total;
    }
    return RoboOk;
}
