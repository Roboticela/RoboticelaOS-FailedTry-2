/*
 * RoboHTML Phase 17.8 — open Shadow DOM.
 *
 * A shadow root is a `Fragment` node (the same kind 17.7's template content
 * uses) plus one table entry: (host, root, mode). Nothing else is needed,
 * because "the root has no parent" already means "the document root cannot
 * reach it", which is the boundary, and "the host keeps its own children"
 * already means the light DOM is untouched.
 *
 * The table is keyed by node identity — document, node id and generation — for
 * the same reason the image, control and template tables are: a moved host
 * keeps its shadow root, and a recycled node id cannot inherit one.
 */

#include "RoboHTML/RoboHTMLShadow.h"

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLVersion.h"

#include "RoboMem/RoboMem.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Mode vocabulary                                                     */
/* ------------------------------------------------------------------ */

int HtmlShadowModeIsKnown(HtmlShadowMode mode)
{
    return mode == RoboHtmlShadowModeOpen;
}

const char *HtmlShadowModeName(HtmlShadowMode mode)
{
    switch (mode) {
    case RoboHtmlShadowModeOpen:
        return "open";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

struct HtmlShadowEntry {
    HtmlNodeHandle host;
    HtmlNodeHandle root;
    HtmlShadowMode mode;
};

void HtmlDocumentShadowsFree(HtmlDocumentSlot *slot)
{
    if (!slot) {
        return;
    }
    if (slot->shadows) {
        RoboFree(slot->shadows);
    }
    slot->shadows = NULL;
    slot->shadowCount = 0u;
    slot->shadowCapacity = 0u;
}

static struct HtmlShadowEntry *ShadowFindByHost(HtmlDocumentSlot *slot,
                                                const HtmlNodeHandle *host)
{
    size_t i;

    for (i = 0u; i < slot->shadowCount; i++) {
        if (HtmlNodeHandleEqual(&slot->shadows[i].host, host)) {
            return &slot->shadows[i];
        }
    }
    return NULL;
}

static struct HtmlShadowEntry *ShadowFindByRoot(HtmlDocumentSlot *slot,
                                                const HtmlNodeHandle *root)
{
    size_t i;

    for (i = 0u; i < slot->shadowCount; i++) {
        if (HtmlNodeHandleEqual(&slot->shadows[i].root, root)) {
            return &slot->shadows[i];
        }
    }
    return NULL;
}

static Result ShadowStore(HtmlDocumentSlot *slot,
                          const HtmlNodeHandle *host,
                          const HtmlNodeHandle *root,
                          HtmlShadowMode mode,
                          HtmlDiag *diag)
{
    if (slot->shadowCount == slot->shadowCapacity) {
        size_t next = slot->shadowCapacity ? slot->shadowCapacity * 2u : 4u;
        struct HtmlShadowEntry *grown = (struct HtmlShadowEntry *)Realloc(
            slot->shadows, next * sizeof(slot->shadows[0]));

        if (!grown) {
            return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboOutOfMemory,
                                "shadow table growth");
        }
        slot->shadows = grown;
        slot->shadowCapacity = next;
    }
    slot->shadows[slot->shadowCount].host = *host;
    slot->shadows[slot->shadowCount].root = *root;
    slot->shadows[slot->shadowCount].mode = mode;
    slot->shadowCount++;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* attachShadow                                                        */
/* ------------------------------------------------------------------ */

Result HtmlElementAttachShadow(const HtmlNodeHandle *element,
                               HtmlShadowMode mode,
                               HtmlNodeHandle *out,
                               HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlNodeHandle root;
    Result res;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null out");
    }
    HtmlNodeHandleClear(out);
    if (!element) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null element");
    }
    if (!HtmlShadowModeIsKnown(mode)) {
        /*
         * `mode: "closed"` is refused rather than accepted and ignored: this
         * build reads a shadow root through public doors, so a closed root
         * would be ordinary encapsulation theatre. Saying no is the honest
         * answer until the encapsulation is real.
         */
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotSupported,
                            "only an open shadow root is supported");
    }
    if (!HtmlIsInitialized() || !HtmlDocumentIsLive(element->documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    {
        /* The gate's own Result, propagated: a document that is gone answers
         * NotFound and one that was narrowed answers PermissionDenied. */
        Result gate = HtmlSecurityRequireCapability(
            element->documentId, RoboHtmlCapabilityShadowDom,
            "the document was not granted shadow DOM", diag);

        if (gate != RoboOk) {
            return gate;
        }
    }
    {
        HtmlNodeKind kind = RoboHtmlNodeKindDocument;

        if (HtmlNodeHandleResolve(element, &kind) != RoboOk ||
            kind != RoboHtmlNodeKindElement) {
            return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotSupported,
                                "only an element can host a shadow root");
        }
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    if (ShadowFindByHost(slot, element)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboAlreadyExists,
                            "the element already has a shadow root");
    }

    res = HtmlDocumentCreateFragment(element->documentId, &root, diag);
    if (res != RoboOk) {
        return res;
    }
    res = ShadowStore(slot, element, &root, mode, diag);
    if (res != RoboOk) {
        return res;
    }
    *out = root;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

Result HtmlElementShadowRoot(const HtmlNodeHandle *element,
                             HtmlNodeHandle *out,
                             HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    struct HtmlShadowEntry *entry;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null out");
    }
    HtmlNodeHandleClear(out);
    if (!element) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null element");
    }
    if (!HtmlIsInitialized() || !HtmlDocumentIsLive(element->documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    entry = ShadowFindByHost(slot, element);
    if (!entry) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "the element has no shadow root");
    }
    *out = entry->root;
    return RoboOk;
}

Result HtmlShadowRootHost(const HtmlNodeHandle *root,
                          HtmlNodeHandle *out,
                          HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    struct HtmlShadowEntry *entry;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null out");
    }
    HtmlNodeHandleClear(out);
    if (!root) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null root");
    }
    if (!HtmlIsInitialized() || !HtmlDocumentIsLive(root->documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    slot = HtmlDocumentSlotFind(root->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    entry = ShadowFindByRoot(slot, root);
    if (!entry) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "not a shadow root");
    }
    *out = entry->host;
    return RoboOk;
}

Result HtmlShadowRootMode(const HtmlNodeHandle *root,
                          HtmlShadowMode *out,
                          HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    struct HtmlShadowEntry *entry;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null out");
    }
    if (!root) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null root");
    }
    if (!HtmlIsInitialized() || !HtmlDocumentIsLive(root->documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    slot = HtmlDocumentSlotFind(root->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    entry = ShadowFindByRoot(slot, root);
    if (!entry) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "not a shadow root");
    }
    *out = entry->mode;
    return RoboOk;
}

Result HtmlElementIsShadowHost(const HtmlNodeHandle *element,
                               int *outIsHost,
                               HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;

    if (!outIsHost) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null out");
    }
    *outIsHost = 0;
    if (!element) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null element");
    }
    if (!HtmlIsInitialized() || !HtmlDocumentIsLive(element->documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    *outIsHost = ShadowFindByHost(slot, element) ? 1 : 0;
    return RoboOk;
}

Result HtmlNodeIsShadowRoot(const HtmlNodeHandle *node,
                            int *outIsRoot,
                            HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;

    if (!outIsRoot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null out");
    }
    *outIsRoot = 0;
    if (!node) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null node");
    }
    if (!HtmlIsInitialized() || !HtmlDocumentIsLive(node->documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    slot = HtmlDocumentSlotFind(node->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    *outIsRoot = ShadowFindByRoot(slot, node) ? 1 : 0;
    return RoboOk;
}

Result HtmlNodeIsReachableFromDocument(const HtmlNodeHandle *node,
                                       int *outReachable,
                                       HtmlDiag *diag)
{
    HtmlNodeHandle root;
    int contains = 0;

    if (!outReachable) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null out");
    }
    *outReachable = 0;
    if (!node) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null node");
    }
    if (!HtmlIsInitialized() || !HtmlDocumentIsLive(node->documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    root = HtmlDocumentRoot(node->documentId);
    if (!HtmlNodeHandleIsWellformed(&root)) {
        return RoboOk;
    }
    if (HtmlNodeContains(&root, node, &contains, NULL) != RoboOk) {
        return RoboOk;
    }
    *outReachable = contains ? 1 : 0;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Retargeting                                                         */
/* ------------------------------------------------------------------ */

/*
 * One shared implementation for the event and focus doors, because the rule
 * is the same crossing. They stay separate public names because a caller reads
 * "why am I asking" from the name, and because they will diverge when focus
 * navigation lands.
 */
static Result RetargetAtBoundary(const HtmlNodeHandle *root,
                                 const HtmlNodeHandle *target,
                                 HtmlNodeHandle *outRetargeted,
                                 HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    struct HtmlShadowEntry *entry;
    int inside = 0;

    if (!outRetargeted) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null out");
    }
    HtmlNodeHandleClear(outRetargeted);
    if (!root || !target) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null root or target");
    }
    if (!HtmlIsInitialized() || !HtmlDocumentIsLive(root->documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    slot = HtmlDocumentSlotFind(root->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    entry = ShadowFindByRoot(slot, root);
    if (!entry) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "not a shadow root");
    }
    if (target->documentId != root->documentId) {
        /* A target in another document is not inside this root, so it crosses
         * nothing — reported unchanged rather than refused. */
        *outRetargeted = *target;
        return RoboOk;
    }
    if (HtmlNodeContains(root, target, &inside, NULL) != RoboOk) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "target does not resolve");
    }
    /* A target outside the tree is unchanged: no boundary was crossed. */
    *outRetargeted = inside ? entry->host : *target;
    return RoboOk;
}

Result HtmlShadowRootRetargetEvent(const HtmlNodeHandle *root,
                                   const HtmlNodeHandle *target,
                                   HtmlNodeHandle *outRetargeted,
                                   HtmlDiag *diag)
{
    return RetargetAtBoundary(root, target, outRetargeted, diag);
}

Result HtmlShadowRootRetargetFocus(const HtmlNodeHandle *root,
                                   const HtmlNodeHandle *target,
                                   HtmlNodeHandle *outRetargeted,
                                   HtmlDiag *diag)
{
    return RetargetAtBoundary(root, target, outRetargeted, diag);
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

Result HtmlShadowGetCapabilities(HtmlShadowCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->attachShadow = 1;
    out->shadowRootQuery = 1;
    out->hostQuery = 1;
    out->lightDomPreserved = 1;
    out->boundaryLookup = 1;
    out->scopedLookup = 1;
    out->eventRetargeting = 1;
    out->focusBoundary = 1;
    out->reachabilityQuery = 1;
    out->closedMode = 0;
    out->slotting = 0;
    out->hostSelectors = 0;
    out->declarativeShadowDom = 0;
    out->secondRenderingTree = 0;
    out->hostCandidates = 0;
    out->version = RoboHtmlVersionString;
    return RoboOk;
}
