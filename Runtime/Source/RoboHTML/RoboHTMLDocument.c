/*
 * RoboHTML document (Phase 8.1) — ownership root: creation, source
 * attachment, node registration, lifecycle transitions, teardown.
 */

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTMLInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

static HtmlDocumentId gNextDocumentId = 1u;

/* ------------------------------------------------------------------ */
/* Node payload arena (Phase 8.3)                                      */
/* ------------------------------------------------------------------ */

/**
 * Ensure @p extra more bytes fit at the arena tail.
 * Growth is geometric (256 B, then doubling) so building a document costs
 * O(nodes) allocations rather than one per node.
 */
static Result ArenaReserve(HtmlDocumentSlot *slot,
                                   size_t extra,
                                   HtmlDiag *diag)
{
    size_t need;
    size_t next;
    char *grown;

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "slot is NULL");
    }
    if (extra > SIZE_MAX - slot->payloadUsed) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboOutOfMemory,
                              "payload arena size overflow");
    }
    need = slot->payloadUsed + extra;
    if (need <= slot->payloadCapacity) {
        return RoboOk;
    }
    next = (slot->payloadCapacity == 0u) ? 256u : slot->payloadCapacity;
    while (next < need) {
        if (next > (SIZE_MAX / 2u)) {
            next = need;
            break;
        }
        next *= 2u;
    }
    grown = (char *)Realloc(slot->payload, next);
    if (!grown) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboOutOfMemory,
                              "payload arena allocation failed");
    }
    slot->payload = grown;
    slot->payloadCapacity = next;
    return RoboOk;
}

Result HtmlDocumentArenaAlloc(HtmlDocumentSlot *slot,
                                             size_t size,
                                             uint32_t *outOffset,
                                             void **outPtr,
                                             HtmlDiag *diag)
{
    size_t aligned;
    size_t padding;

    if (!slot || !outOffset) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "arena args are NULL");
    }
    if (size == 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "zero-size payload");
    }
    if (size > 0xFFFFFFFFu) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotSupported,
                              "payload exceeds one addressable range");
    }
    /*
     * Skip to the next aligned cursor before reserving. The payload header is
     * four `uint32_t`s, so a reservation that followed an odd-length name would
     * be misaligned, and accessing it there is undefined behaviour — harmless
     * on x86-64, a fault on stricter targets, and a real defect either way.
     * The padding is unreachable through any payload: no offset or length ever
     * describes it.
     */
    aligned = (slot->payloadUsed + (RoboHtmlPayloadAlignment - 1u)) &
              ~(size_t)(RoboHtmlPayloadAlignment - 1u);
    padding = aligned - slot->payloadUsed;
    if (padding + size > 0xFFFFFFFFu) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotSupported,
                              "payload exceeds one addressable range");
    }
    if (ArenaReserve(slot, padding + size, diag) != RoboOk) {
        return diag ? diag->code : RoboOutOfMemory;
    }
    *outOffset = (uint32_t)aligned;
    if (outPtr) {
        *outPtr = slot->payload + aligned;
    }
    slot->payloadUsed = aligned + size;
    return RoboOk;
}

Result HtmlDocumentArenaExtendTail(
    HtmlDocumentSlot *slot,
    uint32_t offset,
    size_t oldSize,
    size_t extra,
    void **outPtr,
    HtmlDiag *diag)
{
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "slot is NULL");
    }
    if ((size_t)offset + oldSize != slot->payloadUsed) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotSupported,
                              "only the arena tail can grow");
    }
    if (ArenaReserve(slot, extra, diag) != RoboOk) {
        return diag ? diag->code : RoboOutOfMemory;
    }
    if (outPtr) {
        *outPtr = slot->payload + offset;
    }
    slot->payloadUsed += extra;
    return RoboOk;
}

const void *HtmlDocumentArenaAt(const HtmlDocumentSlot *slot,
                                        uint32_t offset,
                                        uint32_t length)
{
    if (!slot || !slot->payload) {
        return NULL;
    }
    if ((size_t)offset + length > slot->payloadUsed) {
        return NULL;
    }
    return slot->payload + offset;
}

void HtmlDocumentArenaFree(HtmlDocumentSlot *slot)
{
    if (!slot) {
        return;
    }
    if (slot->payload) {
        RoboFree(slot->payload);
    }
    slot->payload = NULL;
    slot->payloadUsed = 0u;
    slot->payloadCapacity = 0u;
}

Result HtmlDocumentClearTree(HtmlDocumentSlot *slot,
                                            HtmlDiag *diag)
{
    int haveRoot = 0;
    uint32_t nextGeneration;

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboInvalidArgument, "slot is NULL");
    }
    if (!HtmlDocumentLifecycleAllowsContent(slot->lifecycle)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboError,
                              "lifecycle does not allow clearing the tree");
    }

    /*
     * Everything the tree owns is the arena: element names, attributes,
     * text, comments and DOCTYPE strings all live in one bump-allocated
     * block, so releasing it releases the whole tree's content at once and
     * a half-built tree cannot leak.
     */
    HtmlDocumentArenaFree(slot);
    /* Image resources are keyed by node identity, and every node those keys
     * could name is about to be retired. */
    HtmlDocumentImagesFree(slot);
    /* Form-control state is keyed the same way, and for the same reason: the
     * node ids and generations it was written against are being retired, so
     * the overrides they describe no longer describe anything. */
    HtmlDocumentControlsFree(slot);
    /* Template content is a set of node identities in the same registry, so
     * it is forgotten with them rather than kept pointing at retired ids. */
    HtmlDocumentTemplatesFree(slot);
    /* Shadow roots are keyed the same way, and for the same reason. */
    HtmlDocumentShadowsFree(slot);
    /* The custom element registry belongs to this document alone. */
    HtmlDocumentCustomsFree(slot);

    /* Is the document's own root node still usable? */
    if (HtmlNodeHandleIsWellformed(&slot->root)) {
        const HtmlNodeEntry *e =
            HtmlNodeEntryGet(slot->id, slot->root.nodeId);

        if (e && e->generation == slot->root.generation &&
            e->kind == RoboHtmlNodeKindDocument) {
            haveRoot = 1;
        }
    }

    /*
     * Reset the registry while keeping the *generation* cursor moving:
     * generations are what make a handle from the discarded tree detectable,
     * so they must never repeat for a reused slot.
     *
     * Node ids, by contrast, restart dense. Identity for a node is
     * (document, id, generation) and the generation is the guard, so
     * restarting ids costs nothing in safety while keeping id == slot + 1.
     * That correspondence is what keeps registration and lookup linear
     * however many times a document is re-parsed: with ids that only ever
     * grow, slot lookup would degrade to a full scan on every access after
     * the first rebuild.
     */
    nextGeneration = slot->nodes.nextGeneration;
    HtmlNodeRegistryInit(&slot->nodes);
    slot->nodes.nextGeneration = nextGeneration;

    if (!haveRoot) {
        /* No usable root (never created, or already torn down): build one. */
        HtmlNodeHandle root;
        Result r;

        HtmlNodeHandleClear(&slot->root);
        slot->finalized = 0;
        r = HtmlDocumentRegisterNode(
            slot->id, RoboHtmlNodeKindDocument,
            RoboHtmlNodeOriginDocumentRoot, &root, diag);
        if (r != RoboOk) {
            return r;
        }
        slot->root = root;
    } else {
        /*
         * Keep the root's document node, at slot 0 so ids stay dense, with a
         * fresh generation so handles to the *old* root are stale rather
         * than silently naming the new tree's root.
         */
        HtmlNodeEntry *re = &slot->nodes.slots[0];

        re->inUse = 1;
        re->id = 1u;
        /*
         * The root keeps its own generation: it is the same logical node
         * across the clear (the document node), so a holder's root handle
         * must survive. The cursor has already moved past it, and it only
         * ever moves forward, so no later node can collide with it.
         */
        re->generation = slot->root.generation;
        re->kind = RoboHtmlNodeKindDocument;
        re->parent = RoboHtmlNodeIdNone;
        re->firstChild = RoboHtmlNodeIdNone;
        re->lastChild = RoboHtmlNodeIdNone;
        re->prevSibling = RoboHtmlNodeIdNone;
        re->NextSibling = RoboHtmlNodeIdNone;
        re->payloadOffset = 0u;
        re->payloadLength = 0u;
        re->childCount = 0u;
        re->flags = 0u;
        slot->nodes.used = 1u;
        slot->nodes.nextId = 2u;
        slot->root.documentId = slot->id;
        slot->root.nodeId = 1u;
        slot->root.generation = re->generation;
    }

    /* A new tree is about to exist, so construction is open again. */
    slot->finalized = 0;
    /*
     * Dropping every node is a structural change like any other: a
     * traversal that snapshotted the old version must be able to see that
     * the tree it started on is gone. Handles from the old tree already
     * fail to resolve, so this is the belt to that pair of braces.
     */
    HtmlDocumentNoteTreeChange(slot);
    LogDebug("robo_html: doc=%llu tree cleared (root preserved=%d)",
                   (unsigned long long)slot->id, haveRoot);
    return RoboOk;
}

Result HtmlDocumentApplyTransition(
    HtmlDocumentSlot *slot,
    HtmlDocumentLifecycle to)
{
    HtmlDocumentLifecycle from;

    if (!slot) {
        return RoboInvalidArgument;
    }
    from = slot->lifecycle;
    if (!HtmlDocumentLifecycleCanTransition(from, to)) {
        LogError("robo_html: illegal lifecycle %s -> %s",
                       HtmlDocumentLifecycleName(from),
                       HtmlDocumentLifecycleName(to));
        return RoboInvalidArgument;
    }
    if (from == to) {
        return RoboOk;
    }
    slot->previousLifecycle = from;
    slot->lifecycle = to;
    HtmlEmitLifecycle(slot, from, to);
    if (to == RoboHtmlDocumentLifecycleLoading) {
        HtmlEmitInvalidate(slot, RoboHtmlInvalidationSource);
    }
    LogDebug("robo_html: doc=%llu %s -> %s",
                   (unsigned long long)slot->id,
                   HtmlDocumentLifecycleName(from),
                   HtmlDocumentLifecycleName(to));
    return RoboOk;
}

Result HtmlDocumentCreate(HtmlDocumentId *outId,
                                        HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlDocumentId id;

    if (!outId) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboInvalidArgument, "out_id is NULL");
    }
    *outId = RoboHtmlDocumentIdNone;
    if (!HtmlIsInitialized()) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboInitError,
                              "RoboHTML module not initialized");
    }
    slot = HtmlDocumentSlotAlloc();
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboOutOfMemory,
                              "document table exhausted");
    }
    memset(slot, 0, sizeof(*slot));
    id = gNextDocumentId++;
    slot->inUse = 1;
    slot->id = id;
    slot->lifecycle = RoboHtmlDocumentLifecycleUninitialized;
    slot->previousLifecycle = RoboHtmlDocumentLifecycleUninitialized;
    slot->failDomain = RoboHtmlDomainNone;
    slot->failMessage = NULL;
    slot->sourceKind = RoboHtmlSourceKindNone;
    slot->sourceLength = 0u;
    slot->sourceName[0] = '\0';
    HtmlEncodingStateInit(&slot->encoding);
    HtmlNodeRegistryInit(&slot->nodes);
    slot->leasedParser = NULL;
    memset(&slot->client, 0, sizeof(slot->client));
    slot->clientUserdata = NULL;
    slot->MutationHook = NULL;
    slot->mutationUserdata = NULL;
    slot->loadPhase = RoboHtmlLoadPhaseNone;
    slot->loader = NULL;
    slot->finalized = 0;
    /*
     * 1, not 0: the tree version doubles as "this document exists". A
     * traversal snapshots a live version, so 0 can only ever mean "no
     * document". Monotonic from here — a clear or a mutation only ever
     * moves it forward, so a snapshot can never be mistaken for a fresh
     * one.
     */
    slot->treeVersion = 1u;

    /* Enter CREATED and register the document node (future tree root). */
    slot->lifecycle = RoboHtmlDocumentLifecycleCreated;
    {
        HtmlNodeHandle root;
        if (HtmlDocumentRegisterNode(id, RoboHtmlNodeKindDocument,
                                             RoboHtmlNodeOriginDocumentRoot,
                                             &root, NULL) != RoboOk) {
            slot->inUse = 0;
            slot->id = RoboHtmlDocumentIdNone;
            return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                                  RoboError, "root node registration failed");
        }
        slot->root = root;
    }
    LogInfo("robo_html: document created id=%llu",
                  (unsigned long long)id);
    (void)HtmlDiagSet(diag, RoboOk, RoboHtmlDomainDocument,
                             "document created", __FILE__, __LINE__);
    *outId = id;
    return RoboOk;
}

Result HtmlDocumentAttachSource(
    HtmlDocumentId documentId,
    const HtmlSource *src,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown document id");
    }
    if (!src) {
        return RoboHtmlDiag(diag, RoboHtmlDomainSource,
                              RoboInvalidArgument, "source is NULL");
    }
    if (slot->lifecycle == RoboHtmlDocumentLifecycleLoading) {
        return RoboHtmlDiag(diag, RoboHtmlDomainLifecycle, RoboBusy,
                              "a source is already attached");
    }
    if (!HtmlDocumentLifecycleAllowsSourceAttach(slot->lifecycle)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainLifecycle, RoboError,
                              "lifecycle does not allow source attach");
    }
    /*
     * A newly attached source is about to be parsed into this document, so
     * construction reopens. Without this a doc that had been finalized and
     * then given a replacement source would sit in LOADING with its
     * construction API closed — a state no caller could interpret.
     */
    slot->finalized = 0;
    if (slot->loadPhase == RoboHtmlLoadPhaseAcquiring) {
        return RoboHtmlDiag(diag, RoboHtmlDomainResource, RoboBusy,
                              "resource acquisition outstanding");
    }
    /* Source metadata is copied; bytes stay application-owned. */
    slot->sourceKind = src->kind;
    slot->sourceLength = HtmlSourceLength(src);
    {
        size_t n = 0u;
        while (src->name[n] != '\0' && n < sizeof(slot->sourceName) - 1u) {
            slot->sourceName[n] = src->name[n];
            n++;
        }
        slot->sourceName[n] = '\0';
    }
    (void)HtmlDocumentApplyTransition(slot,
                                              RoboHtmlDocumentLifecycleLoading);
    /*
     * An empty source settles immediately: there is nothing to parse, so
     * nothing can be outstanding. It still passes through INTERACTIVE
     * rather than jumping, because "the DOM is live" is exactly as true of
     * an empty document and the graph has no LOADING → COMPLETE edge by
     * design — a document cannot be finished loading before it has
     * finished parsing.
     */
    if (slot->sourceLength == 0u) {
        (void)HtmlDocumentApplyTransition(
            slot, RoboHtmlDocumentLifecycleInteractive);
        (void)HtmlDocumentApplyTransition(
            slot, RoboHtmlDocumentLifecycleComplete);
    }
    (void)HtmlDiagSet(diag, RoboOk, RoboHtmlDomainSource,
                             "source attached", __FILE__, __LINE__);
    return RoboOk;
}

Result HtmlDocumentDetachSource(
    HtmlDocumentId documentId,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown document id");
    }
    if (slot->lifecycle != RoboHtmlDocumentLifecycleLoading) {
        return RoboOk;
    }
    slot->sourceKind = RoboHtmlSourceKindNone;
    slot->sourceLength = 0u;
    slot->sourceName[0] = '\0';
    HtmlEncodingStateInit(&slot->encoding);
    /*
     * Detaching abandons input rather than finishing it, so the document
     * settles at INTERACTIVE and not COMPLETE. The tree is whatever was
     * parsed before the detach, and it is live from here.
     */
    (void)HtmlDocumentApplyTransition(
        slot, RoboHtmlDocumentLifecycleInteractive);
    (void)HtmlDiagSet(diag, RoboOk, RoboHtmlDomainSource,
                             "source detached", __FILE__, __LINE__);
    return RoboOk;
}

Result HtmlDocumentMarkInteractive(
    HtmlDocumentId documentId,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown document id");
    }
    if (slot->lifecycle != RoboHtmlDocumentLifecycleLoading) {
        return RoboHtmlDiag(diag, RoboHtmlDomainLifecycle,
                              RoboInvalidArgument,
                              "only a loading document can become interactive");
    }
    return HtmlDocumentApplyTransition(
        slot, RoboHtmlDocumentLifecycleInteractive);
}

Result HtmlDocumentMarkComplete(
    HtmlDocumentId documentId,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown document id");
    }
    if (slot->lifecycle != RoboHtmlDocumentLifecycleInteractive) {
        return RoboHtmlDiag(
            diag, RoboHtmlDomainLifecycle, RoboInvalidArgument,
            "only an interactive document can complete");
    }
    return HtmlDocumentApplyTransition(
        slot, RoboHtmlDocumentLifecycleComplete);
}

Result HtmlDocumentMarkFailed(
    HtmlDocumentId documentId,
    HtmlDiagnosticDomain domain,
    const char *message,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);
    Result r;

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown document id");
    }
    if (slot->lifecycle == RoboHtmlDocumentLifecycleFailed) {
        return RoboOk; /* Sticky: first failure wins. */
    }
    if (slot->lifecycle != RoboHtmlDocumentLifecycleCreated &&
        slot->lifecycle != RoboHtmlDocumentLifecycleLoading &&
        slot->lifecycle != RoboHtmlDocumentLifecycleInteractive &&
        slot->lifecycle != RoboHtmlDocumentLifecycleComplete) {
        return RoboHtmlDiag(diag, RoboHtmlDomainLifecycle,
                              RoboInvalidArgument,
                              "lifecycle cannot fail");
    }
    slot->failDomain = domain;
    slot->failMessage = message;
    r = HtmlDocumentApplyTransition(
        slot, RoboHtmlDocumentLifecycleFailed);
    if (r == RoboOk) {
        LogWarn("robo_html: doc=%llu failed domain=%s msg=%s",
                      (unsigned long long)documentId,
                      HtmlDiagnosticDomainName(domain),
                      message ? message : "-");
    }
    return r;
}

Result HtmlDocumentGetLifecycle(
    HtmlDocumentId documentId,
    HtmlDocumentLifecycle *outLifecycle)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    if (!slot || !outLifecycle) {
        return RoboNotFound;
    }
    *outLifecycle = slot->lifecycle;
    return RoboOk;
}

int HtmlDocumentIsLoading(HtmlDocumentId documentId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    return slot != NULL &&
           slot->lifecycle == RoboHtmlDocumentLifecycleLoading;
}

int HtmlDocumentIsInteractive(HtmlDocumentId documentId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    return slot != NULL &&
           slot->lifecycle == RoboHtmlDocumentLifecycleInteractive;
}

int HtmlDocumentIsComplete(HtmlDocumentId documentId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    return slot != NULL &&
           slot->lifecycle == RoboHtmlDocumentLifecycleComplete;
}

int HtmlDocumentIsSettled(HtmlDocumentId documentId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    return slot != NULL &&
           HtmlDocumentLifecycleIsSettled(slot->lifecycle);
}

Result HtmlDocumentGetEncodingState(
    HtmlDocumentId documentId,
    HtmlEncodingState *outState)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    if (!slot || !outState) {
        return RoboNotFound;
    }
    *outState = slot->encoding;
    return RoboOk;
}

Result HtmlDocumentInputEncodingDeclared(
    HtmlDocumentId documentId,
    const char *label,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown document id");
    }
    if (!HtmlDocumentLifecycleAllowsContent(slot->lifecycle)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainEncoding, RoboError,
                              "lifecycle does not allow encoding update");
    }
    return HtmlEncodingDeclare(&slot->encoding, label, diag);
}

int HtmlDocumentIsEmpty(HtmlDocumentId documentId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return 0;
    }
    /* A document is empty while it holds nothing but its own root node. */
    return slot->nodes.used <= 1u;
}

const char *HtmlDocumentSourceName(HtmlDocumentId documentId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return NULL;
    }
    return slot->sourceName;
}

size_t HtmlDocumentNodeCount(HtmlDocumentId documentId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return 0u;
    }
    return HtmlNodeRegistryCount(&slot->nodes);
}

HtmlNodeHandle HtmlDocumentRoot(
    HtmlDocumentId documentId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    if (!slot) {
        HtmlNodeHandle invalid;
        HtmlNodeHandleClear(&invalid);
        return invalid;
    }
    return slot->root;
}

/*
 * The one place a node identity is allocated. `allowFinalized` is the whole
 * difference between the two doors below: the *parser*'s registration closes
 * when a build finalizes the tree, while the DOM's `createElement` (Phase 17.1)
 * must keep producing nodes for the whole life of a live document. Both still
 * respect the lifecycle gate (a failed or destroyed document accepts nothing)
 * and both emit the same registration event, so a consumer cannot tell which
 * door allocated a node from anything but its origin.
 */
static Result RegisterNodeCore(HtmlDocumentSlot *slot,
                               HtmlNodeKind kind,
                               HtmlNodeOrigin origin,
                               int allowFinalized,
                               HtmlNodeHandle *outHandle,
                               HtmlDiag *diag)
{
    HtmlNodeEntry *e = NULL;
    HtmlNodeHandle h;
    size_t i;

    if (!HtmlNodeOriginIsKnown(origin)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "unknown node origin");
    }
    if (!HtmlNodeKindIsKnown(kind)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "unknown node kind");
    }
    if (!HtmlDocumentLifecycleAllowsContent(slot->lifecycle)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboError,
                              "lifecycle does not allow node registration");
    }
    if (!allowFinalized && slot->finalized) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTree, RoboError,
                              "document tree is finalized");
    }
    if (slot->nodes.used >= RoboHtmlMaxNodesPerDocument) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboOutOfMemory,
                              "node registry exhausted");
    }
    for (i = 0u; i < RoboHtmlMaxNodesPerDocument; i++) {
        if (!slot->nodes.slots[i].inUse) {
            e = &slot->nodes.slots[i];
            break;
        }
    }
    if (!e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboOutOfMemory, "node registry exhausted");
    }
    h.documentId = slot->id;
    h.nodeId = slot->nodes.nextId++;
    h.generation = slot->nodes.nextGeneration;
    e->inUse = 1;
    e->id = h.nodeId;
    e->generation = h.generation;
    e->kind = kind;
    e->origin = (uint32_t)origin;
    slot->nodes.used++;
    slot->nodes.nextGeneration++;
    if (outHandle) {
        *outHandle = h;
    }
    {
        HtmlMutationEvent ev;
        ev.kind = RoboHtmlMutationNodeRegistered;
        ev.documentId = slot->id;
        ev.node = h;
        ev.lifecycle = slot->lifecycle;
        HtmlEmitMutation(&ev, slot);
    }
    return RoboOk;
}

Result HtmlDocumentRegisterNode(
    HtmlDocumentId documentId,
    HtmlNodeKind kind,
    HtmlNodeOrigin origin,
    HtmlNodeHandle *outHandle,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "unknown document id");
    }
    return RegisterNodeCore(slot, kind, origin, 0, outHandle, diag);
}

Result HtmlDocumentRegisterNodeLive(
    HtmlDocumentId documentId,
    HtmlNodeKind kind,
    HtmlNodeOrigin origin,
    HtmlNodeHandle *outHandle,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "unknown document id");
    }
    return RegisterNodeCore(slot, kind, origin, 1, outHandle, diag);
}

Result HtmlDocumentDestroy(HtmlDocumentId documentId,
                                         HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown or already destroyed document");
    }
    /* Release the parser lease first (parser never outlives document). */
    if (slot->leasedParser) {
        (void)HtmlParserDetach(slot->leasedParser, NULL);
        slot->leasedParser = NULL;
    }
    (void)HtmlDocumentApplyTransition(
        slot, RoboHtmlDocumentLifecycleDestroying);
    memset(&slot->client, 0, sizeof(slot->client));
    slot->clientUserdata = NULL;
    slot->MutationHook = NULL;
    slot->mutationUserdata = NULL;
    slot->loadPhase = RoboHtmlLoadPhaseNone;
    slot->loader = NULL;
    slot->finalized = 0;
    /*
     * 1, not 0: the tree version doubles as "this document exists". A
     * traversal snapshots a live version, so 0 can only ever mean "no
     * document". Monotonic from here — a clear or a mutation only ever
     * moves it forward, so a snapshot can never be mistaken for a fresh
     * one.
     */
    slot->treeVersion = 1u;
    /* Payloads belong to the document, so the tree dies with it. */
    HtmlDocumentArenaFree(slot);
    HtmlDocumentImagesFree(slot);
    HtmlDocumentControlsFree(slot);
    HtmlDocumentTemplatesFree(slot);
    HtmlDocumentShadowsFree(slot);
    HtmlDocumentCustomsFree(slot);
    HtmlNodeRegistryInit(&slot->nodes);
    HtmlNodeHandleClear(&slot->root);
    slot->lifecycle = RoboHtmlDocumentLifecycleDestroyed;
    slot->inUse = 0;
    slot->id = RoboHtmlDocumentIdNone;
    LogDebug("robo_html: document destroyed id=%llu",
                   (unsigned long long)documentId);
    (void)HtmlDiagSet(diag, RoboOk, RoboHtmlDomainDocument,
                             "document destroyed", __FILE__, __LINE__);
    return RoboOk;
}

int HtmlDocumentIsLive(HtmlDocumentId documentId)
{
    return HtmlDocumentSlotFind(documentId) != NULL;
}
