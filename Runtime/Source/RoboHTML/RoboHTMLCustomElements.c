/*
 * RoboHTML Phase 17.9 — custom elements.
 *
 * Shape of the file:
 *
 *   1. name validation         one place, shared by define and by the public
 *                              probe, so "is this valid?" and "is this
 *                              accepted?" cannot disagree
 *   2. the registry            a per-document table of definitions
 *   3. upgrade                 the steps an element runs when it becomes a
 *                              custom element (attributes, then connection)
 *   4. lifecycle dispatch      the three notifications the mutation and
 *                              attribute layers call
 *
 * The dispatch functions are deliberately *collect-then-invoke*: a callback
 * may mutate the tree, and a walk refuses to continue once the tree it is
 * walking has changed — so every element to notify is gathered first, and the
 * callbacks then run over the gathered list.
 *
 * Connection is asked of the tree (`HtmlNodeIsReachableFromDocument`, 17.8)
 * rather than remembered per element, which is what makes insert-into-a-
 * detached-subtree-then-attach fire exactly one `connected`.
 */

#include "RoboHTML/RoboHTMLCustomElements.h"

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLShadow.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLVersion.h"

#include "RoboMem/RoboMem.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

/* The deepest chain of callbacks-triggering-callbacks this module will run
 * before it stops dispatching. A definition whose callback re-enters the
 * registry is legal; an unbounded recursion through it is not. */
#define CustomDispatchMaxDepth 8u

/* ------------------------------------------------------------------ */
/* 1. Name validation                                                  */
/* ------------------------------------------------------------------ */

static int AsciiLower(char c)
{
    return c >= 'a' && c <= 'z';
}

static int AsciiDigit(char c)
{
    return c >= '0' && c <= '9';
}

/* The names the HTML standard sets aside so a future built-in cannot collide
 * with a page's custom element. */
static int IsReservedName(const char *name)
{
    static const char *const kReserved[] = {
        "annotation-xml", "color-profile", "font-face", "font-face-src",
        "font-face-uri", "font-face-format", "font-face-name", "missing-glyph",
    };
    size_t i;

    for (i = 0u; i < sizeof(kReserved) / sizeof(kReserved[0]); i++) {
        if (strcmp(name, kReserved[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

Result HtmlCustomElementNameValidate(const char *name, HtmlDiag *diag)
{
    size_t length = 0u;
    size_t i;
    int sawHyphen = 0;

    if (!name) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                            "a custom element name is required");
    }
    while (name[length] != '\0') {
        length++;
    }
    if (length == 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                            "a custom element name cannot be empty");
    }
    if (length > RoboHtmlNameMax) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotSupported,
                            "custom element name is too long");
    }
    /* A lowercase letter first excludes "-foo", "_foo", "1foo" and "Foo" in
     * one rule, which is exactly what the standard's grammar says. */
    if (!AsciiLower(name[0])) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboParseError,
                            "a custom element name starts with a lowercase "
                            "letter");
    }
    for (i = 0u; i < length; i++) {
        char c = name[i];

        if (c == '-') {
            sawHyphen = 1;
            continue;
        }
        if (AsciiLower(c) || AsciiDigit(c) || c == '_' || c == '.') {
            continue;
        }
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboParseError,
                            "a custom element name uses only lowercase "
                            "letters, digits, '-', '_' and '.'");
    }
    /*
     * The reserved and built-in checks come *before* the hyphen rule, because
     * they answer a different question: `div` is a perfectly well-formed
     * element name this runtime simply will not hand out, while `myrow` is
     * not a custom element name at all. Reporting "no hyphen" for `div` would
     * tell a caller their name was malformed when it was only unavailable.
     */
    if (IsReservedName(name)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotSupported,
                            "the name is reserved by the HTML standard");
    }
    /* A built-in's name is not a custom element name: `define("div")` would
     * otherwise let a document redefine an element it did not create. */
    if (HtmlTagIsKnown(HtmlTagFromName(RoboHtmlNamespaceHtml, name))) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotSupported,
                            "the name belongs to a built-in element");
    }
    if (!sawHyphen) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboParseError,
                            "a custom element name contains a '-'");
    }
    return RoboOk;
}

int HtmlCustomElementNameIsValid(const char *name)
{
    return HtmlCustomElementNameValidate(name, NULL) == RoboOk;
}

/* ------------------------------------------------------------------ */
/* 2. The registry                                                     */
/* ------------------------------------------------------------------ */

struct HtmlCustomEntry {
    char name[RoboHtmlNameMax + 1u];
    size_t nameLength;
    char observed[RoboHtmlCustomElementMaxObserved][RoboHtmlNameMax + 1u];
    /*
     * The pointer array a caller receives from
     * HtmlDocumentCustomElementDefinitionGet. It lives in the entry so the
     * returned `observedAttributes` borrows from the registry rather than
     * from a static buffer that a second call would overwrite.
     */
    const char *observedPtrs[RoboHtmlCustomElementMaxObserved];
    size_t observedCount;
    HtmlCustomElementCallbacks callbacks;
    void *userdata;
};

void HtmlDocumentCustomsFree(HtmlDocumentSlot *slot)
{
    if (!slot) {
        return;
    }
    if (slot->customs) {
        RoboFree(slot->customs);
    }
    slot->customs = NULL;
    slot->customCount = 0u;
    slot->customCapacity = 0u;
    slot->customDispatchDepth = 0u;
}

static struct HtmlCustomEntry *CustomFind(HtmlDocumentSlot *slot,
                                          const char *name)
{
    size_t i;

    if (!slot || !name) {
        return NULL;
    }
    for (i = 0u; i < slot->customCount; i++) {
        if (strcmp(slot->customs[i].name, name) == 0) {
            return &slot->customs[i];
        }
    }
    return NULL;
}

void HtmlCustomElementDefinitionInit(HtmlCustomElementDefinition *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

/*
 * Resolve an element and hand back its borrowed name. The liveness check
 * comes before the kind check, so a stale handle is NotFound rather than
 * "not an element" — the same ordering every other 17.x gate uses.
 */
static Result ElementNameGate(const HtmlNodeHandle *element,
                              const char **outName,
                              HtmlDiag *diag)
{
    HtmlNodeKind kind = RoboHtmlNodeKindDocument;

    if (!element || !outName) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                            "null element or out");
    }
    *outName = NULL;
    if (!HtmlIsInitialized() || !HtmlDocumentIsLive(element->documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                            "document does not resolve");
    }
    if (HtmlNodeHandleResolve(element, &kind) != RoboOk ||
        kind != RoboHtmlNodeKindElement) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                            "handle does not resolve to an element");
    }
    *outName = HtmlElementName(element, NULL);
    if (!*outName) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                            "the element has no name");
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 3. Upgrade                                                          */
/* ------------------------------------------------------------------ */

/*
 * The attribute order a definition's `attributeChanged` callbacks run in is
 * the element's own attribute order, which is deterministic and is what a
 * caller reads back with `HtmlElementAttributeAt`.
 */
static void CustomRunAttributeChanged(const struct HtmlCustomEntry *entry,
                                      const HtmlNodeHandle *element)
{
    size_t count = 0u;
    size_t i;

    if (!entry->callbacks.attributeChanged) {
        return;
    }
    if (HtmlElementAttributeCount(element, &count, NULL) != RoboOk) {
        return;
    }
    for (i = 0u; i < count; i++) {
        HtmlAttribute attribute;
        size_t o;

        memset(&attribute, 0, sizeof(attribute));
        if (HtmlElementAttributeAt(element, i, &attribute, NULL) != RoboOk) {
            continue;
        }
        if (!attribute.name) {
            continue;
        }
        for (o = 0u; o < entry->observedCount; o++) {
            if (strlen(entry->observed[o]) == attribute.nameLength &&
                memcmp(entry->observed[o], attribute.name,
                       attribute.nameLength) == 0) {
                /* Upgrade reports the value as *new*: there was no earlier
                 * value the element could have observed. */
                (void)entry->callbacks.attributeChanged(
                    element, entry->observed[o], NULL, attribute.value,
                    entry->userdata, NULL);
                break;
            }
        }
    }
}

/*
 * Is @p node connected?
 *
 * Connected means "in a document tree" — and a **shadow tree** counts, which
 * is why this is not simply HtmlNodeIsReachableFromDocument. The walk goes up
 * the parent chain; at a parentless node it asks what that root is: the
 * document node means connected, a shadow root means the answer is whatever
 * its host's answer is, and anything else (a template's content, a detached
 * subtree) means not connected.
 *
 * The step counter bounds a corrupted link graph; the same ceiling every other
 * upward walk in this module uses.
 */
static int CustomIsConnected(const HtmlNodeHandle *node)
{
    HtmlNodeHandle current = *node;
    size_t steps = 0u;

    while (steps++ < (size_t)RoboHtmlMaxNodesPerDocument) {
        HtmlNodeHandle parent;
        Result parentResult = HtmlNodeParent(&current, &parent, NULL);

        if (parentResult == RoboOk) {
            current = parent;
            continue;
        }
        if (parentResult != RoboNotFound) {
            return 0;
        }
        {
            HtmlNodeKind kind = RoboHtmlNodeKindDocument;

            if (HtmlNodeHandleResolve(&current, &kind) != RoboOk) {
                return 0;
            }
            if (kind == RoboHtmlNodeKindDocument) {
                return 1;
            }
        }
        {
            int isShadowRoot = 0;
            HtmlNodeHandle host;

            if (HtmlNodeIsShadowRoot(&current, &isShadowRoot, NULL) != RoboOk
                || !isShadowRoot) {
                return 0;
            }
            if (HtmlShadowRootHost(&current, &host, NULL) != RoboOk) {
                return 0;
            }
            current = host;
        }
    }
    return 0;
}

static void CustomRunConnected(const struct HtmlCustomEntry *entry,
                               const HtmlNodeHandle *element)
{
    if (!entry->callbacks.connected) {
        return;
    }
    if (!CustomIsConnected(element)) {
        return;
    }
    (void)entry->callbacks.connected(element, entry->userdata, NULL);
}

static Result CustomUpgradeOne(HtmlDocumentSlot *slot,
                               const HtmlNodeHandle *element)
{
    struct HtmlCustomEntry *entry;
    const char *name;
    size_t length = 0u;

    name = HtmlElementName(element, &length);
    if (!name || length == 0u) {
        return RoboNotFound;
    }
    entry = CustomFind(slot, name);
    if (!entry) {
        return RoboNotFound;
    }
    CustomRunAttributeChanged(entry, element);
    CustomRunConnected(entry, element);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 4. Lifecycle dispatch (called by the mutation and attribute layers) */
/* ------------------------------------------------------------------ */

typedef struct CustomCollector {
    HtmlDocumentId documentId;
    HtmlNodeHandle *found;
    size_t count;
    size_t capacity;
    int outOfMemory;
    /** Shadow-tree recursion depth (bounded by the visitor). */
    size_t depth;
} CustomCollector;

static void CustomCollectSubtree(HtmlDocumentId documentId,
                                 const HtmlNodeHandle *node,
                                 CustomCollector *collector,
                                 size_t depth);

static Result CustomCollectVisitor(const HtmlNodeView *view, size_t depth,
                                   void *user)
{
    CustomCollector *collector = (CustomCollector *)user;
    HtmlDocumentSlot *slot;
    const char *name = HtmlElementName(&view->handle, NULL);

    (void)depth;
    if (!name) {
        return RoboOk;
    }
    /*
     * A shadow tree is part of the element's reach: its host becoming
     * connected connects it (Phase 17.8's boundary is about *lookup*, not
     * about lifecycle). Recursing here is a nested walk, which the walker
     * explicitly allows, and the depth guard is what stops a pathological
     * host chain.
     */
    {
        int isShadowHost = 0;

        if (collector->depth < 8u &&
            HtmlElementIsShadowHost(&view->handle, &isShadowHost, NULL) ==
                RoboOk &&
            isShadowHost) {
            HtmlNodeHandle shadow;

            if (HtmlElementShadowRoot(&view->handle, &shadow, NULL) ==
                RoboOk) {
                collector->depth++;
                CustomCollectSubtree(collector->documentId, &shadow,
                                     collector, collector->depth);
                collector->depth--;
            }
        }
    }
    slot = HtmlDocumentSlotFind(collector->documentId);
    if (!CustomFind(slot, name)) {
        return RoboOk;
    }
    if (collector->count == collector->capacity) {
        size_t next = collector->capacity ? collector->capacity * 2u : 4u;
        HtmlNodeHandle *grown = (HtmlNodeHandle *)Realloc(
            collector->found, next * sizeof(collector->found[0]));

        if (!grown) {
            collector->outOfMemory = 1;
            return RoboOk;
        }
        collector->found = grown;
        collector->capacity = next;
    }
    collector->found[collector->count] = view->handle;
    collector->count++;
    return RoboOk;
}

static void CustomForEachCustomElement(HtmlDocumentId documentId,
                                       const HtmlNodeHandle *node,
                                       CustomCollector *collector)
{
    collector->documentId = documentId;
    collector->found = NULL;
    collector->count = 0u;
    collector->capacity = 0u;
    collector->outOfMemory = 0;
    collector->depth = 0u;
    CustomCollectSubtree(documentId, node, collector, 0u);
}

static void CustomCollectSubtree(HtmlDocumentId documentId,
                                 const HtmlNodeHandle *node,
                                 CustomCollector *collector,
                                 size_t depth)
{
    collector->documentId = documentId;
    collector->depth = depth;
    if (!node) {
        return;
    }
    /* A failed walk leaves whatever was gathered usable: the elements found
     * so far are still elements that changed state. */
    (void)HtmlNodeWalkPreOrder(node, RoboHtmlNodeFilterElements,
                              CustomCollectVisitor, collector, NULL);
}

void HtmlCustomElementNotifyInserted(HtmlDocumentId documentId,
                                     const HtmlNodeHandle *node)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);
    CustomCollector collector;
    size_t i;

    if (!slot || slot->customCount == 0u || !node) {
        return;
    }
    if (slot->customDispatchDepth >= CustomDispatchMaxDepth) {
        return;
    }
    CustomForEachCustomElement(documentId, node, &collector);
    if (collector.count == 0u) {
        if (collector.found) {
            RoboFree(collector.found);
        }
        return;
    }
    slot->customDispatchDepth++;
    for (i = 0u; i < collector.count; i++) {
        struct HtmlCustomEntry *entry;
        const char *name = HtmlElementName(&collector.found[i], NULL);

        if (!name) {
            continue;
        }
        entry = CustomFind(slot, name);
        if (!entry) {
            continue;
        }
        CustomRunConnected(entry, &collector.found[i]);
    }
    slot->customDispatchDepth--;
    RoboFree(collector.found);
}

void HtmlCustomElementNotifyRemoving(HtmlDocumentId documentId,
                                     const HtmlNodeHandle *node)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);
    CustomCollector collector;
    size_t i;

    if (!slot || slot->customCount == 0u || !node) {
        return;
    }
    if (slot->customDispatchDepth >= CustomDispatchMaxDepth) {
        return;
    }
    CustomForEachCustomElement(documentId, node, &collector);
    if (collector.count == 0u) {
        if (collector.found) {
            RoboFree(collector.found);
        }
        return;
    }
    slot->customDispatchDepth++;
    for (i = 0u; i < collector.count; i++) {
        struct HtmlCustomEntry *entry;
        const char *name = HtmlElementName(&collector.found[i], NULL);

        if (!name) {
            continue;
        }
        entry = CustomFind(slot, name);
        if (!entry || !entry->callbacks.disconnected) {
            continue;
        }
        /* Still connected — the unlink has not run yet — so a subtree
         * detached from a detached parent fires nothing. */
        if (!CustomIsConnected(&collector.found[i])) {
            continue;
        }
        (void)entry->callbacks.disconnected(&collector.found[i],
                                            entry->userdata, NULL);
    }
    slot->customDispatchDepth--;
    RoboFree(collector.found);
}

void HtmlCustomElementNotifyAttributeChanged(HtmlDocumentId documentId,
                                             const HtmlNodeHandle *element,
                                             const char *name,
                                             const char *oldValue,
                                             const char *newValue)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);
    struct HtmlCustomEntry *entry;
    const char *elementName;
    size_t o;

    if (!slot || slot->customCount == 0u || !element || !name) {
        return;
    }
    if (slot->customDispatchDepth >= CustomDispatchMaxDepth) {
        return;
    }
    elementName = HtmlElementName(element, NULL);
    if (!elementName) {
        return;
    }
    entry = CustomFind(slot, elementName);
    if (!entry || !entry->callbacks.attributeChanged) {
        return;
    }
    for (o = 0u; o < entry->observedCount; o++) {
        if (strcmp(entry->observed[o], name) == 0) {
            slot->customDispatchDepth++;
            (void)entry->callbacks.attributeChanged(element, name, oldValue,
                                                    newValue,
                                                    entry->userdata, NULL);
            slot->customDispatchDepth--;
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Registration and queries                                            */
/* ------------------------------------------------------------------ */

Result HtmlDocumentDefineCustomElement(
    HtmlDocumentId documentId,
    const HtmlCustomElementDefinition *definition,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    struct HtmlCustomEntry *entry;
    size_t i;
    Result res;

    if (!definition || !definition->name) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                            "a definition with a name is required");
    }
    if (definition->observedCount > 0u && !definition->observedAttributes) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                            "observed attributes are NULL");
    }
    if (definition->observedCount > RoboHtmlCustomElementMaxObserved) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotSupported,
                            "too many observed attributes");
    }
    for (i = 0u; i < definition->observedCount; i++) {
        if (!definition->observedAttributes[i]) {
            return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                                RoboInvalidArgument,
                                "a NULL observed attribute");
        }
        if (strlen(definition->observedAttributes[i]) > RoboHtmlNameMax) {
            return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotSupported,
                                "an observed attribute name is too long");
        }
    }
    res = HtmlCustomElementNameValidate(definition->name, diag);
    if (res != RoboOk) {
        return res;
    }
    if (!HtmlDocumentIsLive(documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                            "no such live document");
    }
    /* Phase 17.10: defining an element type is a capability, so a host that
     * embeds untrusted markup can refuse it while still parsing that
     * markup. */
    {
        /* The gate's own Result, propagated so a destroyed document answers
         * NotFound rather than the policy refusal it never made. */
        Result gate = HtmlSecurityRequireCapability(
            documentId, RoboHtmlCapabilityCustomElements,
            "the document was not granted custom elements", diag);

        if (gate != RoboOk) {
            return gate;
        }
    }
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                            "no such live document");
    }
    if (CustomFind(slot, definition->name)) {
        /* Replacement would silently change the callbacks of every instance
         * already upgraded against the old definition. */
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboAlreadyExists,
                            "the name is already defined");
    }
    if (slot->customCount == slot->customCapacity) {
        size_t next = slot->customCapacity ? slot->customCapacity * 2u : 4u;
        struct HtmlCustomEntry *grown = (struct HtmlCustomEntry *)Realloc(
            slot->customs, next * sizeof(slot->customs[0]));

        if (!grown) {
            return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboOutOfMemory,
                                "custom element registry growth");
        }
        slot->customs = grown;
        slot->customCapacity = next;
    }
    entry = &slot->customs[slot->customCount];
    memset(entry, 0, sizeof(*entry));
    entry->nameLength = strlen(definition->name);
    memcpy(entry->name, definition->name, entry->nameLength + 1u);
    entry->callbacks = definition->callbacks;
    entry->userdata = definition->userdata;
    entry->observedCount = definition->observedCount;
    for (i = 0u; i < entry->observedCount; i++) {
        size_t length = strlen(definition->observedAttributes[i]);

        memcpy(entry->observed[i], definition->observedAttributes[i],
               length + 1u);
        entry->observedPtrs[i] = entry->observed[i];
    }
    slot->customCount++;

    /* Upgrade what is already there. This is the one moment an element that
     * existed before its definition learns it is a custom element. */
    (void)HtmlDocumentUpgradeCustomElements(documentId, NULL, NULL);
    return RoboOk;
}

int HtmlDocumentCustomElementIsDefined(HtmlDocumentId documentId,
                                       const char *name)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    return CustomFind(slot, name) ? 1 : 0;
}

size_t HtmlDocumentCustomElementCount(HtmlDocumentId documentId)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    return slot ? slot->customCount : 0u;
}

Result HtmlDocumentCustomElementDefinitionGet(
    HtmlDocumentId documentId,
    const char *name,
    HtmlCustomElementDefinition *out,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    struct HtmlCustomEntry *entry;

    if (!name || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                            "a name and an output are required");
    }
    HtmlCustomElementDefinitionInit(out);
    slot = HtmlDocumentSlotFind(documentId);
    entry = CustomFind(slot, name);
    if (!entry) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                            "the name is not defined");
    }
    out->name = entry->name;
    out->observedAttributes = entry->observedCount ? entry->observedPtrs
                                                   : NULL;
    out->observedCount = entry->observedCount;
    out->callbacks = entry->callbacks;
    out->userdata = entry->userdata;
    return RoboOk;
}

Result HtmlElementIsCustomElement(const HtmlNodeHandle *element,
                                  int *outIsCustom,
                                  HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    const char *name;
    Result res;

    if (!outIsCustom) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                            "null out");
    }
    *outIsCustom = 0;
    res = ElementNameGate(element, &name, diag);
    if (res != RoboOk) {
        return res;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    *outIsCustom = CustomFind(slot, name) ? 1 : 0;
    return RoboOk;
}

Result HtmlElementUpgradeCustomElement(const HtmlNodeHandle *element,
                                       HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    const char *name;
    Result res;

    res = ElementNameGate(element, &name, diag);
    if (res != RoboOk) {
        return res;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!CustomFind(slot, name)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                            "the element's name is not defined");
    }
    return CustomUpgradeOne(slot, element);
}

Result HtmlDocumentUpgradeCustomElements(HtmlDocumentId documentId,
                                        size_t *outUpgraded,
                                        HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlNodeHandle root;
    CustomCollector collector;
    size_t i;
    size_t upgraded = 0u;

    if (outUpgraded) {
        *outUpgraded = 0u;
    }
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                            "no such live document");
    }
    if (slot->customCount == 0u) {
        return RoboOk;
    }
    root = HtmlDocumentRoot(documentId);
    if (!HtmlNodeHandleIsWellformed(&root)) {
        return RoboOk;
    }
    CustomForEachCustomElement(documentId, &root, &collector);
    for (i = 0u; i < collector.count; i++) {
        if (CustomUpgradeOne(slot, &collector.found[i]) == RoboOk) {
            upgraded++;
        }
    }
    if (collector.found) {
        RoboFree(collector.found);
    }
    if (outUpgraded) {
        *outUpgraded = upgraded;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

Result HtmlCustomElementGetCapabilities(HtmlCustomElementCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->registry = 1;
    out->nameValidation = 1;
    out->definedQuery = 1;
    out->definitionQuery = 1;
    out->createElement = 1;
    out->connectedCallback = 1;
    out->disconnectedCallback = 1;
    out->attributeChangedCallback = 1;
    out->upgradeOnDefine = 1;
    out->reachabilityDriven = 1;
    out->constructors = 0;
    out->reactionQueue = 0;
    out->customisedBuiltins = 0;
    out->scopedRegistries = 0;
    out->definedSelector = 0;
    out->callbackResults = 0;
    out->version = RoboHtmlVersionString;
    return RoboOk;
}
