/*
 * RoboHTML text-node model (Phase 8.7) — see RoboHTMLText.h.
 *
 * File shape:
 *   1. text-entry resolution and connectedness
 *   2. text creation (the single construction seam)
 *   3. text read side (kind test, view, content, length, parent)
 *   4. text mutation (set replaces, append extends)
 *
 * Design notes worth keeping visible:
 *
 * - There is no per-node string and no refcount. Content lives in the
 *   document's bump-allocated payload arena exactly as every other node
 *   payload does, so the byte run moves only when the arena moves and is
 *   freed exactly once, with the document.
 *
 * - set writes a fresh payload instead of editing in place. That is the same
 *   choice the attribute model's mutation makes and for the same reason: the
 *   old bytes become dead arena space (bounded, reclaimed wholesale) rather
 *   than something to splice around, and a payload is never resized under a
 *   borrowed pointer. Node identity, parent, siblings and child order are
 *   untouched, so a set survives document finalization — 8.4 closed tree
 *   *construction*, not the document's readability.
 *
 * - append is one operation with two implementations. Growing in place is
 *   the parser's hot path (adjacent character tokens arrive while the run is
 *   still the arena tail); the copying path exists so a DOM append after the
 *   tree was built — when other payloads sit behind the run — behaves
 *   identically. Content bytes are never truncated or split silently.
 */

#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTMLInternal.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. Resolution                                                       */
/* ------------------------------------------------------------------ */

/**
 * The registry entry behind a live text handle, or NULL.
 *
 * Returns NULL for an ill-formed handle, an unknown document, an unknown or
 * stale node id, and a live node of any other kind — so every caller gets
 * "this is not text" from one place instead of repeating the four checks.
 * The public accessors turn that into their documented "not text" answer
 * rather than an error, which is what keeps a stale handle harmless.
 */
static const HtmlNodeEntry *TextEntry(
    const HtmlNodeHandle *h)
{
    const HtmlNodeEntry *e;

    if (!HtmlNodeHandleIsWellformed(h)) {
        return NULL;
    }
    e = HtmlNodeEntryGet(h->documentId, h->nodeId);
    if (!e || e->generation != h->generation) {
        return NULL;
    }
    if (e->kind != RoboHtmlNodeKindText) {
        return NULL;
    }
    return e;
}

/**
 * Is @p entry reachable from the document root?
 *
 * Walked up the parent links rather than cached as a flag, for the same
 * reason the element model walks: a flag would need every link/unlink path to
 * maintain it and would go stale the moment a future DOM moves a subtree,
 * while the walk costs one step per ancestor and is bounded by the node
 * ceiling. Text is the deepest node kind, so the chain is short.
 */
static int TextIsConnected(const HtmlDocumentSlot *slot,
                             const HtmlNodeEntry *entry)
{
    HtmlNodeId id = entry->parent;
    size_t guard = 0u;

    while (id != RoboHtmlNodeIdNone &&
           guard++ < (size_t)RoboHtmlMaxNodesPerDocument) {
        const HtmlNodeEntry *up =
            HtmlNodeEntryGet(slot->id, id);

        if (!up) {
            return 0;
        }
        if (up->kind == RoboHtmlNodeKindDocument &&
            id == slot->root.nodeId) {
            return 1;
        }
        id = up->parent;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 2. Creation                                                         */
/* ------------------------------------------------------------------ */

void HtmlTextCreateInfoInit(HtmlTextCreateInfo *info)
{
    if (!info) {
        return;
    }
    HtmlNodeHandleClear(&info->parent);
    info->origin = RoboHtmlNodeOriginNone;
    info->content = NULL;
    info->length = 0u;
}

Result HtmlTextCreate(
    HtmlDocumentId documentId,
    const HtmlTextCreateInfo *info,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlNodeHandle h;
    HtmlNodeEntry *entry;
    Result r;

    if (out) {
        HtmlNodeHandleClear(out);
    }
    if (!out || !info) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText,
                              RoboInvalidArgument, "out or info is NULL");
    }
    if (!info->content && info->length > 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText,
                              RoboInvalidArgument,
                              "content is NULL with a non-zero length");
    }
    /*
     * The origin is the text descriptor's only enumerator, so validating it
     * here gives the rejection the text domain rather than the node
     * registry's. Registration would catch it too, one layer down.
     */
    if (!HtmlNodeOriginIsKnown(info->origin) ||
        info->origin == RoboHtmlNodeOriginNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText,
                              RoboInvalidArgument,
                              "text origin is not a live creation origin");
    }
    if (!HtmlNodeHandleIsWellformed(&info->parent)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText,
                              RoboInvalidArgument,
                              "parent handle is ill-formed");
    }
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "unknown document id");
    }
    /*
     * The whole cross-document rule in one place: the parent must name *this*
     * document and that document must actually own it. Checking the claim
     * before resolving is what turns a handle from another document into a
     * detected error instead of an aliased node — two documents may issue the
     * same id/generation pair legitimately.
     */
    if (info->parent.documentId != documentId) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText,
                              RoboInvalidArgument,
                              "parent belongs to another document");
    }
    if (!HtmlDocumentOwnsNode(documentId, &info->parent)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "parent is not a live node of this document");
    }

    r = HtmlDocumentRegisterNode(documentId, RoboHtmlNodeKindText,
                                         info->origin, &h, diag);
    if (r != RoboOk) {
        return r;
    }
    entry = HtmlNodeEntryMut(documentId, h.nodeId);
    if (!entry) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "registered text node vanished from the registry");
    }
    r = HtmlPayloadWriteText(slot, entry, info->content, info->length,
                                     diag);
    if (r != RoboOk) {
        return r;
    }
    r = HtmlNodeLinkChild(documentId, info->parent.nodeId, h.nodeId,
                                  diag);
    if (r != RoboOk) {
        return r;
    }
    *out = h;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 3. Read side                                                        */
/* ------------------------------------------------------------------ */

int HtmlTextIsText(const HtmlNodeHandle *h)
{
    return TextEntry(h) != NULL ? 1 : 0;
}

Result HtmlTextViewGet(const HtmlNodeHandle *text,
                                      HtmlTextView *out,
                                      HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (!text || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText,
                              RoboInvalidArgument, "text or out is NULL");
    }
    memset(out, 0, sizeof(*out));
    slot = HtmlDocumentSlotFind(text->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "unknown document id");
    }
    e = TextEntry(text);
    if (!e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "handle does not resolve to a live text node");
    }
    out->handle = *text;
    out->content = HtmlPayloadText(slot, e, &out->length);
    if (e->parent != RoboHtmlNodeIdNone) {
        const HtmlNodeEntry *p =
            HtmlNodeEntryGet(slot->id, e->parent);

        if (p) {
            out->parent = HtmlNodeHandleMake(slot->id, p);
        }
    }
    out->state = RoboHtmlTextStateNone;
    if (e->origin == (uint32_t)RoboHtmlNodeOriginParser) {
        out->state |= RoboHtmlTextStateParserCreated;
    }
    if (TextIsConnected(slot, e)) {
        out->state |= RoboHtmlTextStateConnected;
    }
    return RoboOk;
}

const char *HtmlTextContent(const HtmlNodeHandle *text,
                                   size_t *outLength)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (outLength) {
        *outLength = 0u;
    }
    if (!text) {
        return NULL;
    }
    slot = HtmlDocumentSlotFind(text->documentId);
    e = TextEntry(text);
    if (!slot || !e) {
        return NULL;
    }
    return HtmlPayloadText(slot, e, outLength);
}

size_t HtmlTextLength(const HtmlNodeHandle *text)
{
    size_t length = 0u;

    (void)HtmlTextContent(text, &length);
    return length;
}

int HtmlTextIsEmpty(const HtmlNodeHandle *text)
{
    size_t length = 0u;

    if (!HtmlTextIsText(text)) {
        return 0;
    }
    (void)HtmlTextContent(text, &length);
    return length == 0u ? 1 : 0;
}

Result HtmlTextParent(const HtmlNodeHandle *text,
                                    HtmlNodeHandle *out,
                                    HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;
    const HtmlNodeEntry *p;

    if (out) {
        HtmlNodeHandleClear(out);
    }
    if (!text || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText,
                              RoboInvalidArgument, "text or out is NULL");
    }
    slot = HtmlDocumentSlotFind(text->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "unknown document id");
    }
    e = TextEntry(text);
    if (!e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "handle does not resolve to a live text node");
    }
    if (e->parent == RoboHtmlNodeIdNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "text node has no parent");
    }
    p = HtmlNodeEntryGet(slot->id, e->parent);
    if (!p) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "text node's parent link does not resolve");
    }
    *out = HtmlNodeHandleMake(slot->id, p);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 4. Mutation                                                         */
/* ------------------------------------------------------------------ */

Result HtmlTextSet(const HtmlNodeHandle *text,
                                 const char *bytes,
                                 size_t length,
                                 HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlNodeEntry *e;

    /*
     * Only a NULL handle is an argument error. A well-formed handle that names
     * no live text — including a cleared or stale one — is a lookup that
     * missed, which is what everything else in this header reports too.
     */
    if (!text) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText,
                              RoboInvalidArgument, "text handle is NULL");
    }
    if (!bytes && length > 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText,
                              RoboInvalidArgument,
                              "bytes is NULL with a non-zero length");
    }
    slot = HtmlDocumentSlotFind(text->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "unknown document id");
    }
    if (!TextEntry(text)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "handle does not resolve to a live text node");
    }
    e = HtmlNodeEntryMut(text->documentId, text->nodeId);
    if (!e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "text node does not resolve for mutation");
    }
    /* A fresh payload; the previous run becomes dead arena space. */
    return HtmlPayloadWriteText(slot, e, bytes, length, diag);
}

Result HtmlTextAppend(const HtmlNodeHandle *text,
                                    const char *bytes,
                                    size_t length,
                                    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlNodeEntry *e;
    Result r;

    if (!text) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText,
                              RoboInvalidArgument, "text handle is NULL");
    }
    if (!bytes && length > 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText,
                              RoboInvalidArgument,
                              "bytes is NULL with a non-zero length");
    }
    if (length == 0u) {
        /* Nothing to add: a successful no-op, but still a kind check. */
        if (!TextEntry(text)) {
            return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                                  "handle does not resolve to a live text node");
        }
        return RoboOk;
    }
    slot = HtmlDocumentSlotFind(text->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "unknown document id");
    }
    if (!TextEntry(text)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "handle does not resolve to a live text node");
    }
    e = HtmlNodeEntryMut(text->documentId, text->nodeId);
    if (!e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                              "text node does not resolve for mutation");
    }

    /* Fast path: the run still ends the arena, so it grows without a copy. */
    r = HtmlPayloadAppendText(slot, e, bytes, length, NULL);
    if (r == RoboOk) {
        return RoboOk;
    }
    if (r != RoboNotSupported) {
        return RoboHtmlDiag(diag, RoboHtmlDomainText, r,
                              "text run could not be extended");
    }

    /*
     * Slow path: other payloads now sit behind the run, so the extended run
     * has to be written somewhere else. Copy both parts once into one buffer
     * and let the payload writer own the result.
     */
    {
        size_t oldLength = 0u;
        const char *old = HtmlPayloadText(slot, e, &oldLength);
        char *joined;
        size_t joinedLength;

        if (!old) {
            return RoboHtmlDiag(diag, RoboHtmlDomainText, RoboNotFound,
                                  "text payload does not resolve");
        }
        if (oldLength > (size_t)0xFFFFFFFFu - length) {
            return RoboHtmlDiag(diag, RoboHtmlDomainText,
                                  RoboNotSupported,
                                  "text run exceeds one payload");
        }
        joinedLength = oldLength + length;
        joined = (char *)Malloc(joinedLength > 0u ? joinedLength : 1u);
        if (!joined) {
            return RoboHtmlDiag(diag, RoboHtmlDomainText,
                                  RoboOutOfMemory,
                                  "no memory to extend the text run");
        }
        if (oldLength > 0u) {
            memcpy(joined, old, oldLength);
        }
        memcpy(joined + oldLength, bytes, length);
        r = HtmlPayloadWriteText(slot, e, joined, joinedLength, diag);
        RoboFree(joined);
        return r;
    }
}
