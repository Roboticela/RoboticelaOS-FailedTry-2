/*
 * RoboLayout's DOM + computed-style bridge (Phase 10.1) — see RoboLayoutDom.h.
 *
 * Two passes and four callbacks. The four callbacks are the whole of the
 * RoboCSS↔RoboHTML seam: what the element is (a view), and how it relates to its
 * parent and its element siblings. Everything else in this file is the storage
 * those callbacks need to stay valid for as long as RoboCSS is asking.
 */

#include "RoboLayout/RoboLayoutDom.h"

#include "RoboCSS/RoboCSSError.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTypes.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/**
 * One captured element.
 *
 * `classes` and `attributes` are arrays the bridge owns; the slices inside them
 * borrow the document's payload arena, which stops moving once the tree is built
 * -- so a view filled from this record stays valid for as long as the document
 * lives, which is what the matcher's recursion needs.
 */
typedef struct layoutDomElement {
    HtmlNodeHandle handle;
    /** The opaque reference RoboCSS sees: the DOM node id. */
    CssElementRef ref;
    CssSlice tag;
    int hasId;
    CssSlice id;
    CssSlice *classes;
    size_t classCount;
    CssAttribute *attributes;
    size_t attributeCount;
    /** Owned; NULL when nothing could be computed for this element. */
    CssComputedStyle *style;
} LayoutDomElement;

struct LayoutDom {
    CssStyleContext *context;
    HtmlDocumentId document;
    LayoutDomElement *elements;
    size_t count;
    size_t capacity;
    /**
     * node id -> element slot + 1, or 0. Allocated once and never moved, which is
     * what makes the four callbacks O(1) instead of a scan each: matching runs
     * callbacks in the thousands on a document of a few dozen elements.
     */
    size_t *byNode;
    size_t byNodeCapacity;
    size_t withoutStyle;
    int ready;
};

/* ------------------------------------------------------------------ */
/* Lookup                                                             */
/* ------------------------------------------------------------------ */

/** The slot @p ref names, or NULL. */
static LayoutDomElement *LayoutDomSlot(LayoutDom *dom,
                                             CssElementRef ref)
{
    size_t node;

    if (!dom || !dom->byNode || ref == RoboCssElementRefNone) {
        return NULL;
    }
    node = (size_t)ref;
    if (node >= dom->byNodeCapacity) {
        return NULL;
    }
    if (dom->byNode[node] == 0u) {
        return NULL;
    }
    return &dom->elements[dom->byNode[node] - 1u];
}

/* ------------------------------------------------------------------ */
/* The four RoboCSS callbacks                                         */
/* ------------------------------------------------------------------ */

static Result LayoutDomViewOf(void *userdata,
                                        CssElementRef ref,
                                        CssElementView *out)
{
    LayoutDom *dom = (LayoutDom *)userdata;
    LayoutDomElement *element;

    if (!out) {
        return RoboInvalidArgument;
    }
    element = LayoutDomSlot(dom, ref);
    if (!element) {
        /* An element that is gone. RoboCSS documents this as the honest answer
         * for a document that mutated under a resolution. */
        return RoboNotFound;
    }
    CssElementViewInit(out);
    if (CssElementViewSetElement(out, element->ref, element->tag, NULL) != RoboOk) {
        return RoboError;
    }
    if (element->hasId &&
        CssElementViewSetId(out, element->id, NULL) != RoboOk) {
        return RoboError;
    }
    if (CssElementViewSetClasses(out, element->classes, element->classCount,
                                          NULL) != RoboOk) {
        return RoboError;
    }
    if (CssElementViewSetAttributes(out, element->attributes,
                                             element->attributeCount, NULL) != RoboOk) {
        return RoboError;
    }
    return RoboOk;
}

/** The nearest element ancestor of @p node, or NONE at the top of the tree. */
static CssElementRef LayoutDomAncestorRef(const LayoutDom *dom,
                                                      HtmlNodeHandle node)
{
    size_t steps = 0u;
    size_t ceiling = dom->byNodeCapacity + 2u;

    while (steps++ < ceiling) {
        HtmlNodeHandle parent;
        HtmlNodeKind kind;

        if (HtmlNodeParent(&node, &parent, NULL) != RoboOk) {
            return RoboCssElementRefNone;
        }
        if (HtmlNodeHandleResolve(&parent, &kind) != RoboOk) {
            return RoboCssElementRefNone;
        }
        if (kind == RoboHtmlNodeKindElement) {
            return (CssElementRef)parent.nodeId;
        }
        node = parent;
    }
    return RoboCssElementRefNone;
}

/** The nearest element sibling of @p node in one direction, or NONE. */
static CssElementRef LayoutDomSiblingRef(const LayoutDom *dom,
                                                     HtmlNodeHandle node,
                                                     int forward)
{
    size_t steps = 0u;
    size_t ceiling = dom->byNodeCapacity + 2u;

    /*
     * Text and comments are stepped over, because `+` and `:first-child` count
     * elements -- and a document written one element per line has a text node
     * between every pair of them, so counting nodes would find no adjacent pair at
     * all. Filtering is this side's job precisely because RoboCSS cannot see the
     * nodes it would have to filter.
     */
    while (steps++ < ceiling) {
        HtmlNodeHandle next;
        HtmlNodeKind kind;
        Result found;

        found = forward ? HtmlNodeNextSibling(&node, &next, NULL)
                        : HtmlNodePrevSibling(&node, &next, NULL);
        if (found != RoboOk) {
            return RoboCssElementRefNone;
        }
        if (HtmlNodeHandleResolve(&next, &kind) != RoboOk) {
            return RoboCssElementRefNone;
        }
        if (kind == RoboHtmlNodeKindElement) {
            return (CssElementRef)next.nodeId;
        }
        node = next;
    }
    return RoboCssElementRefNone;
}

static CssElementRef LayoutDomParentOf(void *userdata,
                                                   CssElementRef ref)
{
    LayoutDom *dom = (LayoutDom *)userdata;
    LayoutDomElement *element = LayoutDomSlot(dom, ref);

    if (!element) {
        return RoboCssElementRefNone;
    }
    return LayoutDomAncestorRef(dom, element->handle);
}

static CssElementRef LayoutDomPreviousOf(void *userdata,
                                                     CssElementRef ref)
{
    LayoutDom *dom = (LayoutDom *)userdata;
    LayoutDomElement *element = LayoutDomSlot(dom, ref);

    if (!element) {
        return RoboCssElementRefNone;
    }
    return LayoutDomSiblingRef(dom, element->handle, 0);
}

static CssElementRef LayoutDomNextOf(void *userdata,
                                                 CssElementRef ref)
{
    LayoutDom *dom = (LayoutDom *)userdata;
    LayoutDomElement *element = LayoutDomSlot(dom, ref);

    if (!element) {
        return RoboCssElementRefNone;
    }
    return LayoutDomSiblingRef(dom, element->handle, 1);
}

static Result LayoutDomInlineStyle(void *userdata,
                                             CssElementRef ref,
                                             CssSlice *outText)
{
    LayoutDom *dom = (LayoutDom *)userdata;
    LayoutDomElement *element = LayoutDomSlot(dom, ref);
    const char *value;
    size_t length = 0u;

    if (!element || !outText) {
        return RoboInvalidArgument;
    }
    /*
     * The `style` attribute's *text* is the whole inline-style boundary: RoboCSS
     * parses it, and the DOM keeps owning it. NULL means "no such attribute",
     * which is a different answer from an empty one -- an empty `style=""` is a
     * declaration list with no declarations.
     */
    value = HtmlElementAttributeValue(&element->handle, "style", &length);
    if (!value) {
        return RoboNotFound;
    }
    *outText = CssSliceFromBytes((const uint8_t *)value, length);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Capture                                                            */
/* ------------------------------------------------------------------ */

/**
 * Make the capture big enough for a document of @p nodeCount nodes.
 *
 * One element per node is a true upper bound, and sizing to it means the element
 * array never moves while RoboCSS holds a view into it -- which is the same reason
 * the 9.4 DOM suite sizes its host that way. The node index needs one slot per
 * node id, so both arrays are sized together and both are replaced together.
 *
 * Both arrays are replaced rather than reallocated *and* the node index is copied:
 * a refresh keeps the ids it already mapped, and `realloc` is not used because the
 * two arrays must grow atomically -- a new index beside an old element array would
 * resolve an id to the wrong slot.
 */
static Result LayoutDomReserve(LayoutDom *dom,
                                        size_t nodeCount,
                                        LayoutDiag *diag)
{
    size_t wanted = nodeCount + 1u;
    LayoutDomElement *elements;
    size_t *byNode;
    size_t i;

    if (wanted <= dom->capacity && wanted <= dom->byNodeCapacity) {
        return RoboOk;
    }
    /* One and a half times, so that a document that grows node by node does not
     * reallocate on every build. */
    wanted = wanted + wanted / 2u + 8u;

    elements = (LayoutDomElement *)RoboCalloc(wanted, sizeof(LayoutDomElement));
    byNode = (size_t *)RoboCalloc(wanted, sizeof(size_t));
    if (!elements || !byNode) {
        RoboFree(elements);
        RoboFree(byNode);
        return RoboLayoutDiag(diag, RoboLayoutDomainDom, RoboOutOfMemory,
                                "could not allocate the element capture");
    }
    for (i = 0u; i < dom->count; i++) {
        elements[i] = dom->elements[i];
    }
    if (dom->byNode && dom->byNodeCapacity > 0u) {
        memcpy(byNode, dom->byNode, dom->byNodeCapacity * sizeof(size_t));
    }
    RoboFree(dom->elements);
    RoboFree(dom->byNode);
    dom->elements = elements;
    dom->capacity = wanted;
    dom->byNode = byNode;
    dom->byNodeCapacity = wanted;
    return RoboOk;
}

/** Release one captured element's owned arrays and style. */
static void LayoutDomElementClear(LayoutDomElement *element)
{
    RoboFree(element->classes);
    RoboFree(element->attributes);
    CssComputedStyleDestroy(element->style);
    memset(element, 0, sizeof(*element));
}

/** What one walk visits. */
static Result LayoutDomCaptureVisitor(const HtmlNodeView *view,
                                                size_t depth,
                                                void *user)
{
    LayoutDom *dom = (LayoutDom *)user;
    LayoutDomElement *element;
    size_t index;
    size_t attributeCount = 0u;
    size_t i;

    (void)depth;
    if (dom->count >= dom->capacity) {
        return RoboError;
    }
    index = dom->count;
    element = &dom->elements[index];
    memset(element, 0, sizeof(*element));
    element->handle = view->handle;
    element->ref = (CssElementRef)view->handle.nodeId;
    element->tag = CssSliceFromText(view->name ? view->name : "");

    if (HtmlElementAttributeCount(&view->handle, &attributeCount, NULL) != RoboOk) {
        return RoboError;
    }
    if (attributeCount > RoboLayoutMaxAttributesPerElement) {
        return RoboError;
    }
    if (attributeCount > 0u) {
        element->attributes = (CssAttribute *)RoboCalloc(
            attributeCount, sizeof(CssAttribute));
        if (!element->attributes) {
            return RoboOutOfMemory;
        }
    }

    for (i = 0u; i < attributeCount; i++) {
        HtmlAttribute attribute;

        if (HtmlElementAttributeAt(&view->handle, i, &attribute, NULL) != RoboOk) {
            return RoboError;
        }
        if (attribute.nameLength == 2u && attribute.name[0] == 'i' &&
            attribute.name[1] == 'd') {
            /* `id` is answered through the view's dedicated field, so it is not
             * also an attribute: RoboCSS's `[id=x]` and `#x` would otherwise be
             * two paths to one fact, and only one of them is the one the matcher
             * checks in its fast path. */
            if (!element->hasId) {
                element->hasId = 1;
                element->id = CssSliceFromBytes(
                    (const uint8_t *)attribute.value, attribute.valueLength);
            }
            continue;
        }
        if (attribute.nameLength == 5u && memcmp(attribute.name, "class", 5u) == 0) {
            CssSlice value = CssSliceFromBytes(
                (const uint8_t *)attribute.value, attribute.valueLength);
            int truncated = 0;

            element->classes = (CssSlice *)RoboCalloc(
                RoboLayoutMaxClassesPerElement, sizeof(CssSlice));
            if (!element->classes) {
                return RoboOutOfMemory;
            }
            element->classCount = CssSplitAsciiWhitespace(
                value, element->classes, RoboLayoutMaxClassesPerElement, &truncated);
            if (truncated) {
                /* Refused rather than truncated: a class list the matcher cannot
                 * see whole would turn `.x` into a wrong answer. */
                return RoboError;
            }
            continue;
        }
        element->attributes[element->attributeCount].name =
            CssSliceFromBytes((const uint8_t *)attribute.name,
                                      attribute.nameLength);
        element->attributes[element->attributeCount].value =
            CssSliceFromBytes((const uint8_t *)attribute.value,
                                      attribute.valueLength);
        element->attributeCount++;
    }

    dom->byNode[(size_t)element->ref] = index + 1u;
    dom->count = index + 1u;
    return RoboOk;
}

/** Compute every captured element's style. */
static Result LayoutDomComputeAll(LayoutDom *dom,
                                            LayoutDiag *diag)
{
    size_t i;

    dom->withoutStyle = 0u;
    for (i = 0u; i < dom->count; i++) {
        LayoutDomElement *element = &dom->elements[i];
        CssComputedStyle *style = NULL;
        CssError error = RoboCssCleared;
        Result computed;

        if (CssComputedStyleCreate(&style, NULL) != RoboOk) {
            return RoboLayoutDiag(diag, RoboLayoutDomainDom, RoboOutOfMemory,
                                    "could not allocate a computed style");
        }
        computed = CssStyleContextComputeStyle(dom->context, element->ref,
                                                        style, &error, NULL);
        if (computed != RoboOk) {
            /*
             * A style that could not be computed is dropped rather than kept
             * half-filled, and it is *counted*: "this element has no style" is a
             * fact the tree records as STYLE_MISSING, and a bridge that quietly
             * substituted an empty style would hide it.
             */
            CssComputedStyleDestroy(style);
            dom->withoutStyle++;
            continue;
        }
        element->style = style;
        if (CssComputedStyleCount(style) == 0u) {
            dom->withoutStyle++;
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The style source                                                   */
/* ------------------------------------------------------------------ */

static const CssComputedStyle *LayoutDomStyleOf(void *userdata,
                                                            const HtmlNodeHandle *element)
{
    LayoutDom *dom = (LayoutDom *)userdata;
    LayoutDomElement *slot;

    if (!dom || !element) {
        return NULL;
    }
    slot = LayoutDomSlot(dom, (CssElementRef)element->nodeId);
    if (!slot) {
        return NULL;
    }
    return slot->style;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

Result LayoutDomCreate(CssStyleContext *context,
                                     HtmlDocumentId document,
                                     LayoutDom **outDom,
                                     LayoutDiag *diag)
{
    LayoutDom *dom;
    CssElementHost host;
    size_t nodeCount;

    if (outDom) {
        *outDom = NULL;
    }
    if (!context || !outDom) {
        return RoboLayoutDiag(diag, RoboLayoutDomainDom, RoboInvalidArgument,
                                "context or out_dom is NULL");
    }
    if (document == RoboHtmlDocumentIdNone || !HtmlDocumentIsLive(document)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainDom, RoboNotFound,
                                "document is not live");
    }

    CssDiag cssDiag = RoboCssCleared;

    dom = (LayoutDom *)RoboCalloc(1u, sizeof(*dom));
    if (!dom) {
        return RoboLayoutDiag(diag, RoboLayoutDomainDom, RoboOutOfMemory,
                                "could not allocate the DOM bridge");
    }
    dom->context = context;
    dom->document = document;

    nodeCount = HtmlDocumentNodeCount(document);
    if (LayoutDomReserve(dom, nodeCount, diag) != RoboOk) {
        LayoutDomDestroy(dom);
        return diag ? diag->code : RoboOutOfMemory;
    }

    /* Install the seam. The host is one userdata for all four callbacks because
     * they come from one place: this bridge. */
    CssElementHostInit(&host);
    host.userdata = dom;
    host.view = LayoutDomViewOf;
    host.parent = LayoutDomParentOf;
    host.PreviousSibling = LayoutDomPreviousOf;
    host.NextSibling = LayoutDomNextOf;
    /* The two seams below are RoboCSS calls, so they report through a RoboCSS
     * diag and not through this module's record. Translating rather than passing
     * `diag` is what keeps the two error vocabularies apart: a caller reading a
     * RoboLayout diag must never find a CSS domain in it. */
    if (CssStyleContextSetElementHost(context, &host, &cssDiag) != RoboOk) {
        LayoutDomDestroy(dom);
        return RoboLayoutDiag(diag, RoboLayoutDomainDom,
                                cssDiag.code == RoboOk ? RoboError : cssDiag.code,
                                "the style context refused the element host");
    }
    if (CssStyleContextSetInlineStyle(context, LayoutDomInlineStyle, dom,
                                               &cssDiag) != RoboOk) {
        LayoutDomDestroy(dom);
        return RoboLayoutDiag(diag, RoboLayoutDomainDom,
                                cssDiag.code == RoboOk ? RoboError : cssDiag.code,
                                "the style context refused the inline-style provider");
    }

    *outDom = dom;
    return RoboOk;
}

void LayoutDomDestroy(LayoutDom *dom)
{
    size_t i;

    if (!dom) {
        return;
    }
    for (i = 0u; i < dom->count; i++) {
        LayoutDomElementClear(&dom->elements[i]);
    }
    RoboFree(dom->elements);
    RoboFree(dom->byNode);
    /* Document, context and stylesheets belong to the caller. */
    RoboFree(dom);
}

Result LayoutDomRefresh(LayoutDom *dom,
                                      LayoutDiag *diag)
{
    HtmlNodeHandle root;
    size_t i;
    size_t nodeCount;

    if (!dom) {
        return RoboLayoutDiag(diag, RoboLayoutDomainDom, RoboInvalidArgument,
                                "dom is NULL");
    }
    if (!HtmlDocumentIsLive(dom->document)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainDom, RoboNotFound,
                                "the document is gone");
    }

    /* Release the previous generation first: a second refresh describes one
     * document, not two. */
    for (i = 0u; i < dom->count; i++) {
        LayoutDomElementClear(&dom->elements[i]);
    }
    dom->count = 0u;
    dom->ready = 0;
    dom->withoutStyle = 0u;
    if (dom->byNode && dom->byNodeCapacity > 0u) {
        memset(dom->byNode, 0, dom->byNodeCapacity * sizeof(size_t));
    }

    nodeCount = HtmlDocumentNodeCount(dom->document);
    if (LayoutDomReserve(dom, nodeCount, diag) != RoboOk) {
        return diag ? diag->code : RoboOutOfMemory;
    }

    /* Pass one: capture. Every element must be answerable before any style is
     * computed, because computing one walks its ancestors. */
    root = HtmlDocumentRoot(dom->document);
    if (HtmlNodeHandleIsWellformed(&root)) {
        Result walked = HtmlNodeWalkPreOrder(&root,
                                                             RoboHtmlNodeFilterElements,
                                                             LayoutDomCaptureVisitor, dom,
                                                             NULL);

        if (walked != RoboOk) {
            return RoboLayoutDiag(diag, RoboLayoutDomainDom,
                                    walked == RoboOutOfMemory ? RoboOutOfMemory
                                                                 : RoboError,
                                    "could not capture the document's elements");
        }
    }

    /* Pass two: compute. */
    if (LayoutDomComputeAll(dom, diag) != RoboOk) {
        return diag ? diag->code : RoboError;
    }
    dom->ready = dom->count > 0u;
    return RoboOk;
}

size_t LayoutDomElementCount(const LayoutDom *dom)
{
    return dom ? dom->count : 0u;
}

int LayoutDomIsReady(const LayoutDom *dom)
{
    return dom && dom->ready;
}

Result LayoutDomElementAt(const LayoutDom *dom,
                                         size_t index,
                                         HtmlNodeHandle *out,
                                         LayoutDiag *diag)
{
    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainDom, RoboInvalidArgument,
                                "out is NULL");
    }
    HtmlNodeHandleClear(out);
    if (!dom) {
        return RoboLayoutDiag(diag, RoboLayoutDomainDom, RoboInvalidArgument,
                                "dom is NULL");
    }
    if (index >= dom->count) {
        return RoboLayoutDiag(diag, RoboLayoutDomainDom, RoboNotFound,
                                "the index is past the captured element count");
    }
    *out = dom->elements[index].handle;
    return RoboOk;
}

const CssComputedStyle *LayoutDomStyleFor(
    const LayoutDom *dom,
    const HtmlNodeHandle *element)
{
    LayoutDomElement *slot;

    if (!dom || !element) {
        return NULL;
    }
    /* The lookup is read-only, but it is typed through the mutable struct because
     * the node index is storage rather than state; the cast is the one place that
     * is visible, and it is here rather than in every caller. */
    slot = LayoutDomSlot((LayoutDom *)dom,
                           (CssElementRef)element->nodeId);
    return slot ? slot->style : NULL;
}

size_t LayoutDomWithoutStyleCount(const LayoutDom *dom)
{
    return dom ? dom->withoutStyle : 0u;
}

HtmlDocumentId LayoutDomDocument(const LayoutDom *dom)
{
    return dom ? dom->document : RoboHtmlDocumentIdNone;
}

Result LayoutDomStyleSource(LayoutDom *dom,
                                           LayoutStyleSource *out,
                                           LayoutDiag *diag)
{
    if (!dom || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainDom, RoboInvalidArgument,
                                "dom or out is NULL");
    }
    LayoutStyleSourceInit(out);
    out->userdata = dom;
    out->ComputedStyleFor = LayoutDomStyleOf;
    return RoboOk;
}
