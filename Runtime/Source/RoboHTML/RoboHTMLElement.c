/*
 * RoboHTML element model (Phase 8.5) — see RoboHTMLElement.h.
 *
 * File shape:
 *   1. name policy (strict, application-facing: validate + normalize)
 *   2. element classification (delegates to the Phase 8.11 tag vocabulary)
 *   3. element creation (the single construction seam)
 *   4. element read side (identity, view, state, attributes, tag)
 *
 * The element *type* vocabulary lives in RoboHTMLTag.c; this file owns the
 * element *instance* — the payload read side over the tree. Tag accessors
 * therefore read here (they need the node registry and the payload), while
 * the vocabulary they resolve against is defined there. One table, read from
 * two directions.
 *
 * Note on the name policy: the strict normalizer validates as it lowercases
 * and is for names an application supplies. The parser path deliberately
 * does not validate, because a malformed tag name must be tolerated rather
 * than rejected — so element creation lowercases and stores whatever it is
 * given instead of calling the strict helper.
 */

#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

void HtmlElementBoundaryInit(HtmlElementBoundary *b)
{
    if (!b) {
        return;
    }
    b->reserved = 0u;
}

static int NameCharOk(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

Result HtmlElementNameValidate(const char *name,
                                              HtmlDiag *diag)
{
    size_t i;
    size_t n = 0u;

    if (!name) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboInvalidArgument, "name is NULL");
    }
    while (name[n] != '\0') {
        n++;
    }
    if (n == 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboInvalidArgument, "name is empty");
    }
    for (i = 0u; i < n; i++) {
        if (!NameCharOk((unsigned char)name[i])) {
            return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                                  RoboParseError,
                                  "name contains a forbidden character");
        }
    }
    if (n > RoboHtmlNameMax) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboNotSupported, "name too long");
    }
    return RoboOk;
}

Result HtmlElementNameNormalize(const char *name,
                                               char *out,
                                               HtmlDiag *diag)
{
    size_t n = 0u;
    size_t i;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboInvalidArgument, "out is NULL");
    }
    if (HtmlElementNameValidate(name, diag) != RoboOk) {
        return diag ? diag->code : RoboInvalidArgument;
    }
    while (name[n] != '\0') {
        n++;
    }
    if (n >= RoboHtmlNameMax) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboNotSupported, "name too long");
    }
    for (i = 0u; i < n; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        out[i] = c;
    }
    out[n] = '\0';
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Element/attribute storage read side (Phase 8.3)                     */
/* ------------------------------------------------------------------ */

/** Resolve @p h to a live entry whose kind is ELEMENT. */
static const HtmlNodeEntry *ElementEntry(
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
    if (e->kind != RoboHtmlNodeKindElement) {
        return NULL;
    }
    return e;
}

/**
 * Is @p entry reachable from the document root?
 *
 * Walked up the parent links rather than stored as a flag: a flag would have
 * to be maintained by every link/unlink path (and would go stale the moment
 * a future DOM moves a subtree), while the walk is bounded by the node
 * ceiling and costs one step per ancestor. The loop is additionally guarded
 * so a corrupted parent chain terminates instead of spinning.
 */
static int ElementIsConnected(const HtmlDocumentSlot *slot,
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

Result HtmlElementViewGet(
    const HtmlNodeHandle *element,
    HtmlElementView *out,
    HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (!element || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "element or out is NULL");
    }
    memset(out, 0, sizeof(*out));
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "unknown document id");
    }
    e = ElementEntry(element);
    if (!e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "handle does not resolve to a live element");
    }
    out->name = HtmlPayloadElementName(slot, e, &out->nameLength);
    out->attributeCount = HtmlPayloadAttrCount(slot, e);
    out->selfClosing = HtmlPayloadSelfClosing(slot, e);
    out->namespaceId = HtmlPayloadNamespace(slot, e);
    out->tag = HtmlTagFromName(out->namespaceId, out->name);
    out->classification = HtmlTagClass(out->tag);

    out->state = RoboHtmlElementStateNone;
    if (e->origin == (uint32_t)RoboHtmlNodeOriginParser) {
        out->state |= RoboHtmlElementStateParserCreated;
    }
    if (ElementIsConnected(slot, e)) {
        out->state |= RoboHtmlElementStateConnected;
    }
    if (out->selfClosing) {
        out->state |= RoboHtmlElementStateSelfClosing;
    }
    return RoboOk;
}

void HtmlAttributeInit(HtmlAttribute *attribute)
{
    if (!attribute) {
        return;
    }
    attribute->name = "";
    attribute->nameLength = 0u;
    attribute->value = "";
    attribute->valueLength = 0u;
    attribute->namespaceId = RoboHtmlNamespaceNone;
}

Result HtmlElementAttributeAt(
    const HtmlNodeHandle *element,
    size_t index,
    HtmlAttribute *out,
    HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (!element || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "element or out is NULL");
    }
    HtmlAttributeInit(out);
    slot = HtmlDocumentSlotFind(element->documentId);
    e = ElementEntry(element);
    if (!slot || !e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "handle does not resolve to a live element");
    }
    if (HtmlPayloadAttrAt(slot, e, index, out) != RoboOk) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "attribute index out of range");
    }
    return RoboOk;
}

Result HtmlElementFindAttribute(
    const HtmlNodeHandle *element,
    const char *name,
    HtmlAttribute *out,
    HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (!element || !name || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "NULL argument");
    }
    HtmlAttributeInit(out);
    slot = HtmlDocumentSlotFind(element->documentId);
    e = ElementEntry(element);
    if (!slot || !e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "handle does not resolve to a live element");
    }
    if (HtmlPayloadAttrFind(slot, e, name, out) != RoboOk) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "no such attribute");
    }
    return RoboOk;
}

const char *HtmlElementAttributeValue(
    const HtmlNodeHandle *element,
    const char *name,
    size_t *outLength)
{
    HtmlAttribute found;

    if (outLength) {
        *outLength = 0u;
    }
    if (HtmlElementFindAttribute(element, name, &found, NULL) !=
        RoboOk) {
        return NULL;
    }
    if (outLength) {
        *outLength = found.valueLength;
    }
    return found.value;
}

Result HtmlElementAttributeCount(
    const HtmlNodeHandle *element,
    size_t *outCount,
    HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (!element || !outCount) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboInvalidArgument, "NULL argument");
    }
    *outCount = 0u;
    slot = HtmlDocumentSlotFind(element->documentId);
    e = ElementEntry(element);
    if (!slot || !e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "handle does not resolve to a live element");
    }
    *outCount = HtmlPayloadAttrCount(slot, e);
    return RoboOk;
}

int HtmlElementHasAttribute(const HtmlNodeHandle *element,
                                    const char *name)
{
    HtmlAttribute ignored;

    if (!element || !name || name[0] == '\0') {
        return 0;
    }
    /* Presence is a different question from "has a value": <input disabled>
     * is present with an empty value, so the lookup result is the answer. */
    return HtmlElementFindAttribute(element, name, &ignored, NULL) ==
                   RoboOk
               ? 1
               : 0;
}

/* ------------------------------------------------------------------ */
/* Attribute mutation (Phase 8.6)                                      */
/* ------------------------------------------------------------------ */

/**
 * Shared body of the three write verbs: validate the name the way an
 * application-supplied name must be validated, lowercase it so storage and
 * lookup agree, then hand the payload its rewrite.
 *
 * @p value may be NULL (the empty string), which is what makes a
 * boolean-style attribute writable: presence is the record, not the value.
 */
static Result MutateAttribute(const HtmlNodeHandle *element,
                                      const char *name,
                                      const char *value,
                                      HtmlAttrMutation mutation,
                                      HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlNodeEntry *e;
    char normalized[RoboHtmlNameMax];
    HtmlDiag local;
    /*
     * Phase 17.9: the old value is captured *before* the rewrite, in a local
     * buffer, because the payload the borrowed pointer names is about to be
     * rewritten and a custom element's callback is told both sides of the
     * change. `add` reports the value it is shadowing (if any) as the old
     * one, which is what makes a duplicate add visible rather than silent.
     */
    char oldValue[RoboHtmlAttrValueMax];
    const char *oldValueOut = NULL;
    const char *newValueOut = NULL;
    Result mutated;
    /* The strict normalizer reports why a name was refused through the diag
     * only, so give it one to write into even when the caller passed none —
     * otherwise every refusal collapses to INVALID_ARGUMENT and a caller
     * cannot tell a bad character from a name that is too long. */
    HtmlDiag *where = diag ? diag : &local;

    if (!element || !name) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboInvalidArgument, "element or name is NULL");
    }
    if (HtmlElementNameNormalize(name, normalized, where) != RoboOk) {
        return where->code;
    }
    /*
     * Phase 17.10: an attribute write is part of the DOM edit surface, so it
     * is gated with structural mutation — and, like structural mutation, the
     * *parser* path is untouched, because the tree builder writes attributes
     * through the payload rather than through these verbs.
     */
    {
        /*
         * The gate's own Result is propagated rather than folded into one
         * code: PermissionDenied means "this document was not granted it",
         * while NotFound means the document is gone — and a caller that
         * cannot tell a lifetime bug from a policy refusal will chase the
         * wrong one.
         */
        Result gate = HtmlSecurityRequireCapability(
            element->documentId, RoboHtmlCapabilityDomMutation,
            "the document was not granted DOM mutation", diag);

        if (gate != RoboOk) {
            return gate; /* the gate's own diag is already the right one */
        }
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "unknown document id");
    }
    /* The mutable resolver applies the same generation and kind tests as
     * ElementEntry, so a stale or non-element handle stops here too. */
    e = HtmlNodeEntryMut(element->documentId, element->nodeId);
    if (!e || e->generation != element->generation ||
        e->kind != RoboHtmlNodeKindElement) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "handle does not resolve to a live element");
    }
    {
        size_t oldLength = 0u;
        const char *existing =
            HtmlElementAttributeValue(element, normalized, &oldLength);

        /* Read for every verb — `remove` reports the value it took away and
         * `set` the value it replaced, so neither can be inferred later. */
        if (existing) {
            size_t copy = oldLength < (RoboHtmlAttrValueMax - 1u)
                              ? oldLength
                              : (RoboHtmlAttrValueMax - 1u);

            memcpy(oldValue, existing, copy);
            oldValue[copy] = '\0';
            oldValueOut = oldValue;
        }
    }
    mutated = HtmlPayloadMutateAttributes(slot, e, mutation, normalized,
                                          value ? value : "", diag);
    if (mutated == RoboOk) {
        if (mutation != RoboHtmlAttrMutationRemove) {
            newValueOut = value ? value : "";
        }
        HtmlCustomElementNotifyAttributeChanged(element->documentId, element,
                                                normalized, oldValueOut,
                                                newValueOut);
    }
    return mutated;
}

Result HtmlElementSetAttribute(
    const HtmlNodeHandle *element,
    const char *name,
    const char *value,
    HtmlDiag *diag)
{
    return MutateAttribute(element, name, value, RoboHtmlAttrMutationSet,
                            diag);
}

Result HtmlElementAddAttribute(
    const HtmlNodeHandle *element,
    const char *name,
    const char *value,
    HtmlDiag *diag)
{
    return MutateAttribute(element, name, value, RoboHtmlAttrMutationAdd,
                            diag);
}

Result HtmlElementRemoveAttribute(
    const HtmlNodeHandle *element,
    const char *name,
    HtmlDiag *diag)
{
    return MutateAttribute(element, name, NULL, RoboHtmlAttrMutationRemove,
                            diag);
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

/*
 * Classification is a property of the element *type*, so the tag vocabulary
 * defines it and nothing else does — see RoboHTMLTag.c. Before Phase 8.11
 * this file kept its own name-to-class table; that table and the tag table
 * would have been two answers to "what kind of element is <div>?", so the
 * table moved and this function became a delegation. An element whose name
 * the tag table does not know — a custom element, an SVG element, an element
 * from a future version of HTML — is GENERIC, which is a supported answer
 * and not an error.
 */
HtmlElementClass HtmlElementClassify(
    HtmlNamespace ns,
    const char *localName)
{
    return HtmlTagClass(HtmlTagFromName(ns, localName));
}

int HtmlElementClassIsKnown(HtmlElementClass cls)
{
    return cls >= RoboHtmlElementClassGeneric &&
           cls < RoboHtmlElementClassCount;
}

const char *HtmlElementClassName(HtmlElementClass cls)
{
    switch (cls) {
    case RoboHtmlElementClassGeneric:
        return "generic";
    case RoboHtmlElementClassRoot:
        return "root";
    case RoboHtmlElementClassMetadata:
        return "metadata";
    case RoboHtmlElementClassSection:
        return "section";
    case RoboHtmlElementClassHeading:
        return "heading";
    case RoboHtmlElementClassParagraph:
        return "paragraph";
    case RoboHtmlElementClassText:
        return "text";
    case RoboHtmlElementClassReplaced:
        return "replaced";
    case RoboHtmlElementClassControl:
        return "control";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

void HtmlElementCreateInfoInit(HtmlElementCreateInfo *info)
{
    if (!info) {
        return;
    }
    HtmlNodeHandleClear(&info->parent);
    info->origin = RoboHtmlNodeOriginNone;
    info->namespaceId = RoboHtmlNamespaceNone;
    info->localName = NULL;
    info->attributes = NULL;
    info->attributeCount = 0u;
    info->selfClosing = 0;
}

/**
 * ASCII-lowercase @p src into @p dst, which must hold @p cap bytes.
 * @return the number of bytes written, or @p cap when the name does not fit
 * (the caller then stores the name verbatim rather than truncating it —
 * shrinking a tag name would silently change the element's identity).
 */
static size_t LowerInto(const char *src, char *dst, size_t cap)
{
    size_t i;

    for (i = 0u; src[i] != '\0'; i++) {
        char c = src[i];

        if (i + 1u >= cap) {
            return cap;
        }
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        dst[i] = c;
    }
    dst[i] = '\0';
    return i;
}

Result HtmlElementCreate(
    HtmlDocumentId documentId,
    const HtmlElementCreateInfo *info,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlNodeHandle h;
    HtmlNodeEntry *entry;
    char lowered[RoboHtmlNameMax];
    const char *name;
    size_t nameLength;
    Result r;

    if (out) {
        HtmlNodeHandleClear(out);
    }
    if (!out || !info) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboInvalidArgument, "out or info is NULL");
    }
    if (!info->localName || info->localName[0] == '\0') {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboInvalidArgument, "element name is empty");
    }
    if (!HtmlNamespaceIsKnown(info->namespaceId) ||
        info->namespaceId == RoboHtmlNamespaceNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboInvalidArgument,
                              "element namespace is not a live namespace");
    }
    if (info->attributeCount > 0u && !info->attributes) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboInvalidArgument,
                              "attribute count without an attribute array");
    }
    if (!HtmlNodeHandleIsWellformed(&info->parent)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboInvalidArgument,
                              "parent handle is ill-formed");
    }
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "unknown document id");
    }
    /*
     * The whole cross-document rule in one place: parent and child must name
     * the same document, and that document must actually own the parent.
     * Checking the claimed document id before resolving is what makes a
     * handle from another document a detected error instead of an aliased
     * node.
     */
    if (info->parent.documentId != documentId) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboInvalidArgument,
                              "parent belongs to another document");
    }
    if (!HtmlDocumentOwnsNode(documentId, &info->parent)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "parent is not a live node of this document");
    }

    name = info->localName;
    if (LowerInto(info->localName, lowered, sizeof(lowered)) <
        sizeof(lowered)) {
        name = lowered;
    }
    nameLength = strlen(name);

    r = HtmlDocumentRegisterNode(
        documentId, RoboHtmlNodeKindElement, info->origin, &h, diag);
    if (r != RoboOk) {
        return r;
    }
    entry = HtmlNodeEntryMut(documentId, h.nodeId);
    if (!entry) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "registered element vanished from the registry");
    }
    r = HtmlPayloadWriteElement(slot, entry, name, nameLength,
                                        info->attributes,
                                        info->attributeCount,
                                        info->selfClosing,
                                        info->namespaceId, diag);
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
/* Read side                                                           */
/* ------------------------------------------------------------------ */

int HtmlElementIsElement(const HtmlNodeHandle *h)
{
    return ElementEntry(h) != NULL ? 1 : 0;
}

const char *HtmlElementName(const HtmlNodeHandle *element,
                                   size_t *outLength)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (outLength) {
        *outLength = 0u;
    }
    if (!element) {
        return "";
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    e = ElementEntry(element);
    if (!slot || !e) {
        return "";
    }
    return HtmlPayloadElementName(slot, e, outLength);
}

HtmlNamespace HtmlElementNamespace(
    const HtmlNodeHandle *element)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (!element) {
        return RoboHtmlNamespaceNone;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    e = ElementEntry(element);
    if (!slot || !e) {
        return RoboHtmlNamespaceNone;
    }
    return HtmlPayloadNamespace(slot, e);
}

HtmlElementClass HtmlElementClassification(
    const HtmlNodeHandle *element)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (!element) {
        return RoboHtmlElementClassGeneric;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    e = ElementEntry(element);
    if (!slot || !e) {
        return RoboHtmlElementClassGeneric;
    }
    return HtmlElementClassify(HtmlPayloadNamespace(slot, e),
                                      HtmlPayloadElementName(slot, e,
                                                                     NULL));
}

int HtmlElementIsNamed(const HtmlNodeHandle *element,
                               const char *name)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (!element || !name || name[0] == '\0') {
        return 0;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    e = ElementEntry(element);
    if (!slot || !e) {
        return 0;
    }
    return HtmlNameEqual(HtmlPayloadElementName(slot, e, NULL),
                            name);
}

HtmlTag HtmlElementTag(const HtmlNodeHandle *element)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (!element) {
        return RoboHtmlTagUnknown;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    e = ElementEntry(element);
    if (!slot || !e) {
        return RoboHtmlTagUnknown;
    }
    return HtmlTagFromName(HtmlPayloadNamespace(slot, e),
                                   HtmlPayloadElementName(slot, e,
                                                                  NULL));
}

int HtmlElementIsTag(const HtmlNodeHandle *element,
                             HtmlTag tag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    /*
     * A non-element handle must satisfy no tag, including UNKNOWN: "no
     * recognised identity" is a statement about an element, so it cannot be
     * answered for a text node or a stale id. Resolving the element first is
     * what makes `is_tag(x, UNKNOWN)` mean exactly "x is a live element with
     * no recognised type" rather than "x is not a live element".
     */
    if (!element) {
        return 0;
    }
    /* UNKNOWN is a real question and is answered below; any *other* undefined
     * enumerator can only be a bug, so it is refused instead of quietly
     * matching every unrecognised element. */
    if (tag != RoboHtmlTagUnknown && !HtmlTagIsKnown(tag)) {
        return 0;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    e = ElementEntry(element);
    if (!slot || !e) {
        return 0;
    }
    return HtmlTagFromName(HtmlPayloadNamespace(slot, e),
                                   HtmlPayloadElementName(slot, e,
                                                                  NULL)) ==
           tag;
}

Result HtmlElementParent(const HtmlNodeHandle *element,
                                       HtmlNodeHandle *out,
                                       HtmlDiag *diag)
{
    const HtmlDocumentSlot *slot;
    const HtmlNodeEntry *e;

    if (out) {
        HtmlNodeHandleClear(out);
    }
    if (!element || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboInvalidArgument, "NULL argument");
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    e = ElementEntry(element);
    if (!slot || !e) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "handle does not resolve to a live element");
    }
    if (e->parent == RoboHtmlNodeIdNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "element has no parent");
    }
    return HtmlDocumentNodeById(slot->id, e->parent, out, diag);
}
