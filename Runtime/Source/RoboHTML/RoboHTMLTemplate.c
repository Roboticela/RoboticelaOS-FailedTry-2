/*
 * RoboHTML Phase 17.7 — `<template>` and its inert content.
 *
 * Two things, and only two:
 *
 *   1. A per-document table mapping a template element to its content
 *      fragment. The table is keyed by node identity — document, node id and
 *      generation — for the same reason the image and control tables are: a
 *      moved element keeps its content, and a recycled node id cannot inherit
 *      one, because a fresh node gets a new generation.
 *
 *   2. The adoption pass the tree builder ends with: move each template
 *      element's light children into a fresh fragment. This is what makes a
 *      parsed `<template>` behave like a template without teaching the tree
 *      builder an "in template" insertion mode — the *post-condition* is the
 *      standard one, and the parse itself is unchanged.
 *
 * The reparent is done with the public mutation API (remove, then append),
 * which means the one-parent and acyclic invariants are enforced by the same
 * code every other mutation goes through. Nothing here links nodes by hand.
 */

#include "RoboHTML/RoboHTMLTemplate.h"

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLVersion.h"

#include "RoboMem/RoboMem.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

struct HtmlTemplateEntry {
    HtmlNodeHandle element; /* the `<template>` */
    HtmlNodeHandle content; /* its content fragment (a parentless root) */
};

void HtmlDocumentTemplatesFree(HtmlDocumentSlot *slot)
{
    if (!slot) {
        return;
    }
    if (slot->templates) {
        RoboFree(slot->templates);
    }
    slot->templates = NULL;
    slot->templateCount = 0u;
    slot->templateCapacity = 0u;
}

static int TemplateHandleSame(const HtmlNodeHandle *a,
                              const HtmlNodeHandle *b)
{
    return HtmlNodeHandleEqual(a, b);
}

static struct HtmlTemplateEntry *TemplateFind(HtmlDocumentSlot *slot,
                                              const HtmlNodeHandle *element)
{
    size_t i;

    for (i = 0u; i < slot->templateCount; i++) {
        if (TemplateHandleSame(&slot->templates[i].element, element)) {
            return &slot->templates[i];
        }
    }
    return NULL;
}

static Result TemplateStore(HtmlDocumentSlot *slot,
                            const HtmlNodeHandle *element,
                            const HtmlNodeHandle *content,
                            HtmlDiag *diag)
{
    struct HtmlTemplateEntry *existing;

    if (!content) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "a content fragment is required");
    }
    existing = TemplateFind(slot, element);
    if (existing) {
        existing->content = *content;
        return RoboOk;
    }
    if (slot->templateCount == slot->templateCapacity) {
        size_t next = slot->templateCapacity ? slot->templateCapacity * 2u
                                             : 4u;
        struct HtmlTemplateEntry *grown = (struct HtmlTemplateEntry *)Realloc(
            slot->templates, next * sizeof(slot->templates[0]));

        if (!grown) {
            return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboOutOfMemory,
                                "template table growth");
        }
        slot->templates = grown;
        slot->templateCapacity = next;
    }
    slot->templates[slot->templateCount].element = *element;
    slot->templates[slot->templateCount].content = *content;
    slot->templateCount++;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The gate                                                            */
/* ------------------------------------------------------------------ */

static Result TemplateGate(const HtmlNodeHandle *element, HtmlDiag *diag)
{
    if (!element) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null element");
    }
    if (!HtmlIsInitialized() || !HtmlDocumentIsLive(element->documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    if (HtmlElementTag(element) != RoboHtmlTagTemplate) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotSupported,
                            "element is not a template");
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* template.content                                                    */
/* ------------------------------------------------------------------ */

Result HtmlElementTemplateContent(const HtmlNodeHandle *element,
                                  HtmlNodeHandle *out,
                                  HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    struct HtmlTemplateEntry *entry;
    Result res;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null out");
    }
    HtmlNodeHandleClear(out);
    res = TemplateGate(element, diag);
    if (res != RoboOk) {
        return res;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    entry = TemplateFind(slot, element);
    if (entry) {
        /*
         * The fragment may itself have been recycled with the document in the
         * meantime; resolving is cheap and answers "is this content still
         * real" rather than handing back a handle that cannot resolve.
         */
        if (HtmlNodeHandleResolve(&entry->content, NULL) == RoboOk) {
            *out = entry->content;
            return RoboOk;
        }
        entry = NULL;
    }

    res = HtmlDocumentCreateFragment(element->documentId, out, diag);
    if (res != RoboOk) {
        return res;
    }
    return TemplateStore(slot, element, out, diag);
}

Result HtmlElementTemplateSetContent(const HtmlNodeHandle *element,
                                     const HtmlNodeHandle *fragment,
                                     HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlNodeKind kind;
    HtmlNodeHandle parent;
    Result res;

    res = TemplateGate(element, diag);
    if (res != RoboOk) {
        return res;
    }
    if (!fragment || !HtmlNodeHandleIsWellformed(fragment)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null or ill-formed fragment");
    }
    if (fragment->documentId != element->documentId) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotSupported,
                            "cross-document content is not supported");
    }
    if (HtmlNodeHandleResolve(fragment, &kind) != RoboOk ||
        kind != RoboHtmlNodeKindFragment) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "content must be a fragment");
    }
    /* Content is a root: a fragment that is somebody's child is a subtree of
     * the document, which is exactly what a template's content must not be. */
    if (HtmlNodeParent(fragment, &parent, NULL) == RoboOk) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboAlreadyExists,
                            "fragment already has a parent");
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "document does not resolve");
    }
    return TemplateStore(slot, element, fragment, diag);
}

Result HtmlElementTemplateCloneContent(const HtmlNodeHandle *element,
                                       int deep,
                                       HtmlNodeHandle *out,
                                       HtmlDiag *diag)
{
    HtmlNodeHandle content;
    Result res;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null out");
    }
    HtmlNodeHandleClear(out);
    res = HtmlElementTemplateContent(element, &content, diag);
    if (res != RoboOk) {
        return res;
    }
    return HtmlNodeClone(&content, deep ? 1 : 0, out, diag);
}

Result HtmlElementTemplateIsInert(const HtmlNodeHandle *element,
                                  int *outInert,
                                  HtmlDiag *diag)
{
    HtmlNodeHandle content;
    HtmlNodeHandle root;
    int contains = 0;
    Result res;

    if (!outInert) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                            "null out");
    }
    *outInert = 1;
    res = HtmlElementTemplateContent(element, &content, diag);
    if (res != RoboOk) {
        return res;
    }
    /*
     * Asked of the tree, not read off a flag: the content is inert exactly
     * when the *document's* root cannot reach it. The content's own root is
     * not the question — a parentless node is its own root, so asking it
     * would answer "reachable" for every fragment and every detached subtree
     * alike. A document root that cannot answer does not mean the content
     * became reachable, so the inert answer stands.
     */
    root = HtmlDocumentRoot(element->documentId);
    if (!HtmlNodeHandleIsWellformed(&root)) {
        return RoboOk;
    }
    if (HtmlNodeContains(&root, &content, &contains, NULL) != RoboOk) {
        return RoboOk;
    }
    *outInert = contains ? 0 : 1;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Adoption                                                            */
/* ------------------------------------------------------------------ */

typedef struct TemplateCollector {
    HtmlDocumentId documentId;
    HtmlNodeHandle *found;
    size_t count;
    size_t capacity;
    int failed;
} TemplateCollector;

static Result TemplateCollectVisitor(const HtmlNodeView *view,
                                     size_t depth,
                                     void *user)
{
    TemplateCollector *collector = (TemplateCollector *)user;

    (void)depth;
    if (!HtmlElementIsTag(&view->handle, RoboHtmlTagTemplate)) {
        return RoboOk;
    }
    if (collector->count == collector->capacity) {
        size_t next = collector->capacity ? collector->capacity * 2u : 4u;
        HtmlNodeHandle *grown = (HtmlNodeHandle *)Realloc(
            collector->found, next * sizeof(collector->found[0]));

        if (!grown) {
            collector->failed = 1;
            /* Stopping the walk with a non-Ok code would be reported as the
             * walk's failure; the collector keeps that distinction instead. */
            return RoboOk;
        }
        collector->found = grown;
        collector->capacity = next;
    }
    collector->found[collector->count] = view->handle;
    collector->count++;
    return RoboOk;
}

/*
 * Move @p element's light children into @p content.
 *
 * Always re-reading the element's first child is the same shape
 * HtmlNodeRemoveChildren uses: each removal repairs the list, so the next
 * first child is whatever is left.
 */
static Result TemplateMoveChildren(const HtmlNodeHandle *element,
                                   const HtmlNodeHandle *content,
                                   HtmlDiag *diag)
{
    Result firstChild;

    for (;;) {
        HtmlNodeHandle child;
        Result res;

        firstChild = HtmlNodeFirstChild(element, &child, diag);
        if (firstChild == RoboNotFound) {
            return RoboOk;
        }
        if (firstChild != RoboOk) {
            return firstChild;
        }
        res = HtmlNodeRemoveChild(element, &child, diag);
        if (res != RoboOk) {
            return res;
        }
        res = HtmlNodeAppendChild(content, &child, diag);
        if (res != RoboOk) {
            return res;
        }
    }
}

Result HtmlDocumentAdoptTemplates(HtmlDocumentId documentId,
                                  size_t *outAdopted,
                                  HtmlDiag *diag)
{
    TemplateCollector collector;
    HtmlDocumentSlot *slot;
    HtmlNodeHandle root;
    size_t adopted = 0u;
    size_t i;
    Result res;

    if (outAdopted) {
        *outAdopted = 0u;
    }
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                            "no such live document");
    }
    root = HtmlDocumentRoot(documentId);
    if (!HtmlNodeHandleIsWellformed(&root)) {
        /* No tree to adopt into — a document that never parsed. */
        return RoboOk;
    }

    collector.documentId = documentId;
    collector.found = NULL;
    collector.count = 0u;
    collector.capacity = 0u;
    collector.failed = 0;

    /*
     * Collect first, mutate after. A walk refuses to continue once the tree it
     * is walking has been structurally changed, so adopting inside the visitor
     * would be reported as a failed walk — which is the walker keeping its
     * promise, not a limitation to work around.
     */
    res = HtmlNodeWalkPreOrder(&root, RoboHtmlNodeFilterElements,
                              TemplateCollectVisitor, &collector, diag);
    if (res != RoboOk || collector.failed) {
        if (collector.found) {
            RoboFree(collector.found);
        }
        if (collector.failed) {
            return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboOutOfMemory,
                                "template collection growth");
        }
        return res;
    }
    if (collector.count == 0u) {
        /* The common case: no templates, no allocation, no work. */
        if (collector.found) {
            RoboFree(collector.found);
        }
        return RoboOk;
    }

    for (i = 0u; i < collector.count; i++) {
        HtmlNodeHandle element = collector.found[i];
        HtmlNodeHandle content;
        HtmlNodeHandle first;
        struct HtmlTemplateEntry *entry;

        if (HtmlDocumentSlotFind(documentId) != slot) {
            break;
        }
        entry = TemplateFind(slot, &element);
        if (entry) {
            continue; /* already adopted */
        }
        /* Only elements with light children need adopting; a template that
         * holds nothing gets its fragment lazily from `content`. */
        if (HtmlNodeFirstChild(&element, &first, NULL) != RoboOk) {
            continue;
        }
        res = HtmlDocumentCreateFragment(documentId, &content, diag);
        if (res != RoboOk) {
            RoboFree(collector.found);
            return res;
        }
        res = TemplateStore(slot, &element, &content, diag);
        if (res != RoboOk) {
            RoboFree(collector.found);
            return res;
        }
        res = TemplateMoveChildren(&element, &content, diag);
        if (res != RoboOk) {
            RoboFree(collector.found);
            return res;
        }
        adopted++;
    }

    RoboFree(collector.found);
    if (outAdopted) {
        *outAdopted = adopted;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

Result HtmlTemplateGetCapabilities(HtmlTemplateCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->contentFragment = 1;
    out->parserAdoption = 1;
    out->inertContent = 1;
    out->cloning = 1;
    out->fragmentInsertion = 1;
    out->setContent = 1;
    out->inertnessQuery = 1;
    out->insertionMode = 0;
    out->declarativeShadowDom = 0;
    out->scriptExecution = 0;
    out->version = RoboHtmlVersionString;
    return RoboOk;
}
