/*
 * RoboHTML DOM completion (Phase 17.1).
 *
 * Reads the contract in RoboHTMLDomQuery.h. Everything here is written on the
 * registry, the payload arena, the one linking primitive and the traversal the
 * rest of the module already uses: there is no second node identity and no
 * second tree. The only new seam is the selector matcher, which exists because
 * the CSS selector grammar lives in the module above this one.
 */

#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLText.h"

#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static int IsAsciiSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}

/*
 * One whitespace-separated token of @p text starting at @p *cursor: returns
 * the token start and advances @p *cursor past it. A trailing position yields
 * a length of 0.
 */
static const char *NextToken(const char *text,
                             size_t *cursor,
                             size_t *outLength)
{
    size_t i = *cursor;
    const char *start;
    size_t length = 0u;

    while (text[i] != '\0' && IsAsciiSpace(text[i])) {
        i++;
    }
    start = text + i;
    while (text[i] != '\0' && !IsAsciiSpace(text[i])) {
        i++;
        length++;
    }
    *cursor = i;
    *outLength = length;
    return start;
}

/* Non-zero when @p element's `class` contains @p token as a whole token. */
static int ElementHasClassToken(const HtmlNodeHandle *element,
                                const char *token,
                                size_t tokenLength)
{
    size_t valueLength = 0u;
    const char *value;
    size_t cursor = 0u;

    if (tokenLength == 0u) {
        return 0;
    }
    value = HtmlElementAttributeValue(element, "class", &valueLength);
    if (!value) {
        return 0;
    }
    for (;;) {
        size_t candidateLength = 0u;
        const char *candidate = NextToken(value, &cursor, &candidateLength);

        if (candidateLength == 0u) {
            break;
        }
        if (candidateLength == tokenLength &&
            strncmp(candidate, token, tokenLength) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Non-zero when @p element has every class named in @p classes. */
static int ElementHasAllClasses(const HtmlNodeHandle *element,
                                const char *classes)
{
    size_t cursor = 0u;

    if (!classes || classes[0] == '\0') {
        return 0;
    }
    for (;;) {
        size_t tokenLength = 0u;
        const char *token = NextToken(classes, &cursor, &tokenLength);

        if (tokenLength == 0u) {
            break;
        }
        if (!ElementHasClassToken(element, token, tokenLength)) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Node creation                                                       */
/* ------------------------------------------------------------------ */

static Result CreateCharacterNode(HtmlDocumentId documentId,
                                  HtmlNodeKind kind,
                                  const char *text,
                                  HtmlNodeHandle *out,
                                  HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlNodeEntry *entry;
    HtmlNodeHandle handle;
    Result status;
    const char *bytes = text ? text : "";

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                              "an output handle is required");
    }
    HtmlNodeHandleClear(out);
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown document id");
    }
    status = HtmlDocumentRegisterNodeLive(documentId, kind, RoboHtmlNodeOriginDom,
                                          &handle, diag);
    if (status != RoboOk) {
        return status;
    }
    entry = HtmlNodeEntryMut(documentId, handle.nodeId);
    if (!entry) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "the created node did not resolve");
    }
    status = HtmlPayloadWriteText(slot, entry, bytes, strlen(bytes), diag);
    if (status != RoboOk) {
        return status;
    }
    *out = handle;
    return RoboOk;
}

Result HtmlDocumentCreateElement(
    HtmlDocumentId documentId,
    const char *name,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    char normalized[RoboHtmlNameMax];
    HtmlDocumentSlot *slot;
    HtmlNodeEntry *entry;
    HtmlNodeHandle handle;
    Result status;

    if (!out || !name) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboInvalidArgument,
                              "an output handle and a name are required");
    }
    HtmlNodeHandleClear(out);
    status = HtmlElementNameNormalize(name, normalized, diag);
    if (status != RoboOk) {
        return status;
    }
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown document id");
    }
    status = HtmlDocumentRegisterNodeLive(documentId, RoboHtmlNodeKindElement,
                                          RoboHtmlNodeOriginDom, &handle, diag);
    if (status != RoboOk) {
        return status;
    }
    entry = HtmlNodeEntryMut(documentId, handle.nodeId);
    if (!entry) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "the created node did not resolve");
    }
    status = HtmlPayloadWriteElement(slot, entry, normalized, strlen(normalized),
                                     NULL, 0u, 0, RoboHtmlNamespaceHtml, diag);
    if (status != RoboOk) {
        return status;
    }
    *out = handle;
    return RoboOk;
}

Result HtmlDocumentCreateTextNode(
    HtmlDocumentId documentId,
    const char *text,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    return CreateCharacterNode(documentId, RoboHtmlNodeKindText, text, out, diag);
}

Result HtmlDocumentCreateComment(
    HtmlDocumentId documentId,
    const char *text,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    return CreateCharacterNode(documentId, RoboHtmlNodeKindComment, text, out,
                               diag);
}

Result HtmlDocumentCreateFragment(
    HtmlDocumentId documentId,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                              "an output handle is required");
    }
    HtmlNodeHandleClear(out);
    return HtmlDocumentRegisterNodeLive(documentId, RoboHtmlNodeKindFragment,
                                        RoboHtmlNodeOriginDom, out, diag);
}

/* ------------------------------------------------------------------ */
/* textContent                                                         */
/* ------------------------------------------------------------------ */

Result HtmlNodeTextContentGet(
    const HtmlNodeHandle *node,
    char *out,
    size_t outCap,
    size_t *outLength,
    HtmlDiag *diag)
{
    HtmlNodeKind kind;

    if (!out || !outLength || outCap == 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboInvalidArgument,
                              "an output buffer, its capacity and a length are required");
    }
    out[0] = '\0';
    *outLength = 0u;
    if (HtmlNodeHandleResolve(node, &kind) != RoboOk) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "node does not resolve");
    }
    if (kind == RoboHtmlNodeKindText || kind == RoboHtmlNodeKindComment) {
        HtmlNodeView view;

        if (HtmlNodeGetView(node, &view, diag) != RoboOk) {
            return diag ? diag->code : RoboNotFound;
        }
        *outLength = view.textLength;
        if (view.textLength + 1u <= outCap) {
            memcpy(out, view.text, view.textLength);
            out[view.textLength] = '\0';
        } else {
            memcpy(out, view.text, outCap - 1u);
            out[outCap - 1u] = '\0';
        }
        return RoboOk;
    }
    return HtmlNodeSubtreeText(node, out, outCap, outLength, diag);
}

Result HtmlNodeTextContentSet(
    const HtmlNodeHandle *node,
    const char *text,
    HtmlDiag *diag)
{
    HtmlNodeKind kind;

    if (!node || !text) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                              "a node and non-NULL text are required");
    }
    if (HtmlNodeHandleResolve(node, &kind) != RoboOk) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "node does not resolve");
    }
    if (kind == RoboHtmlNodeKindDocument) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                              "the document node's root cannot be replaced");
    }
    if (kind == RoboHtmlNodeKindText) {
        return HtmlTextSet(node, text, strlen(text), diag);
    }
    if (kind == RoboHtmlNodeKindComment) {
        HtmlDocumentSlot *slot = HtmlDocumentSlotFind(node->documentId);
        HtmlNodeEntry *entry;

        if (!slot) {
            return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                                  "unknown document id");
        }
        entry = HtmlNodeEntryMut(node->documentId, node->nodeId);
        if (!entry) {
            return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                                  "the comment did not resolve");
        }
        return HtmlPayloadWriteText(slot, entry, text, strlen(text), diag);
    }
    /* Element or fragment: drop every child, then one text node when non-empty. */
    {
        Result status = HtmlNodeRemoveChildren(node, NULL, diag);

        if (status != RoboOk) {
            return status;
        }
        if (text[0] == '\0') {
            return RoboOk;
        }
        {
            HtmlNodeHandle child;

            status = HtmlDocumentCreateTextNode(node->documentId, text, &child,
                                                diag);
            if (status != RoboOk) {
                return status;
            }
            return HtmlNodeAppendChild(node, &child, diag);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Placement: prepend                                                  */
/* ------------------------------------------------------------------ */

Result HtmlNodePrependChild(
    const HtmlNodeHandle *parent,
    const HtmlNodeHandle *child,
    HtmlDiag *diag)
{
    HtmlNodeHandle first;

    if (!parent || !child) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation, RoboInvalidArgument,
                              "a parent and a child are required");
    }
    if (HtmlNodeFirstChild(parent, &first, diag) == RoboOk) {
        return HtmlNodeInsertBefore(parent, child, &first, diag);
    }
    /* No children: append is the same operation with no reference child. */
    return HtmlNodeAppendChild(parent, child, diag);
}

/* ------------------------------------------------------------------ */
/* Cloning                                                             */
/* ------------------------------------------------------------------ */

static Result CloneNodeRecursive(HtmlDocumentId documentId,
                                 const HtmlNodeHandle *source,
                                 int deep,
                                 HtmlNodeHandle *out,
                                 HtmlDiag *diag);

static Result CloneChildrenInto(HtmlDocumentId documentId,
                                const HtmlNodeHandle *source,
                                const HtmlNodeHandle *destination,
                                HtmlDiag *diag)
{
    size_t count = 0u;
    size_t index;

    if (HtmlNodeChildCount(source, &count, diag) != RoboOk) {
        return diag ? diag->code : RoboNotFound;
    }
    for (index = 0u; index < count; index++) {
        HtmlNodeHandle child;
        HtmlNodeHandle copy;
        Result status;

        if (HtmlNodeChildAt(source, index, &child, diag) != RoboOk) {
            return diag ? diag->code : RoboNotFound;
        }
        status = CloneNodeRecursive(documentId, &child, 1, &copy, diag);
        if (status != RoboOk) {
            return status;
        }
        status = HtmlNodeAppendChild(destination, &copy, diag);
        if (status != RoboOk) {
            return status;
        }
    }
    return RoboOk;
}

static Result CloneNodeRecursive(HtmlDocumentId documentId,
                                 const HtmlNodeHandle *source,
                                 int deep,
                                 HtmlNodeHandle *out,
                                 HtmlDiag *diag)
{
    HtmlNodeKind kind;
    Result status;

    if (HtmlNodeHandleResolve(source, &kind) != RoboOk) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "node does not resolve");
    }
    switch (kind) {
    case RoboHtmlNodeKindElement: {
        HtmlElementView view;

        if (HtmlElementViewGet(source, &view, diag) != RoboOk) {
            return diag ? diag->code : RoboNotFound;
        }
        status = HtmlDocumentCreateElement(documentId, view.name, out, diag);
        if (status != RoboOk) {
            return status;
        }
        {
            size_t index;

            for (index = 0u; index < view.attributeCount; index++) {
                HtmlAttribute attribute;

                if (HtmlElementAttributeAt(source, index, &attribute, diag) !=
                    RoboOk) {
                    return diag ? diag->code : RoboNotFound;
                }
                status = HtmlElementAddAttribute(out, attribute.name,
                                                 attribute.value, diag);
                if (status != RoboOk) {
                    return status;
                }
            }
        }
        break;
    }
    case RoboHtmlNodeKindText:
    case RoboHtmlNodeKindComment: {
        HtmlNodeView view;

        if (HtmlNodeGetView(source, &view, diag) != RoboOk) {
            return diag ? diag->code : RoboNotFound;
        }
        if (kind == RoboHtmlNodeKindText) {
            status = HtmlDocumentCreateTextNode(documentId, view.text, out, diag);
        } else {
            status = HtmlDocumentCreateComment(documentId, view.text, out, diag);
        }
        if (status != RoboOk) {
            return status;
        }
        break;
    }
    case RoboHtmlNodeKindFragment:
        status = HtmlDocumentCreateFragment(documentId, out, diag);
        if (status != RoboOk) {
            return status;
        }
        break;
    case RoboHtmlNodeKindDocument:
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotSupported,
                              "the document node cannot be cloned");
    case RoboHtmlNodeKindDocumentType:
    default:
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotSupported,
                              "this node kind cannot be cloned");
    }
    if (deep) {
        status = CloneChildrenInto(documentId, source, out, diag);
        if (status != RoboOk) {
            return status;
        }
    }
    return RoboOk;
}

Result HtmlNodeClone(
    const HtmlNodeHandle *node,
    int deep,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    if (!node || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboInvalidArgument,
                              "a node and an output handle are required");
    }
    HtmlNodeHandleClear(out);
    return CloneNodeRecursive(node->documentId, node, deep ? 1 : 0, out, diag);
}

/* ------------------------------------------------------------------ */
/* Lookup walkers                                                      */
/* ------------------------------------------------------------------ */

typedef struct QueryContext {
    const char *name;      /* tag name, or NULL when not a tag query */
    const char *classes;   /* class list, or NULL */
    const char *id;        /* id, or NULL */
    const char *selector;  /* selector text, or NULL */
    HtmlSelectorMatchFn matcher;
    void *matcherUser;
    HtmlNodeVisitFn visit;
    void *visitUser;
    size_t count;
} QueryContext;

static int ElementMatchesQuery(const HtmlNodeHandle *element,
                               const HtmlNodeView *view,
                               const QueryContext *ctx)
{
    if (ctx->id) {
        const char *value = HtmlElementAttributeValue(element, "id", NULL);

        return value && strcmp(value, ctx->id) == 0;
    }
    if (ctx->name) {
        if (ctx->name[0] == '*' && ctx->name[1] == '\0') {
            return 1;
        }
        return HtmlNameEqual(view->name, ctx->name);
    }
    if (ctx->classes) {
        return ElementHasAllClasses(element, ctx->classes);
    }
    if (ctx->selector) {
        if (!ctx->matcher) {
            return 0;
        }
        return ctx->matcher(element, ctx->selector, strlen(ctx->selector),
                            ctx->matcherUser, NULL) != 0;
    }
    return 0;
}

static Result QueryVisit(const HtmlNodeView *view, size_t depth, void *user)
{
    QueryContext *ctx = (QueryContext *)user;

    if (ElementMatchesQuery(&view->handle, view, ctx)) {
        ctx->count++;
        if (ctx->visit) {
            return ctx->visit(view, depth, ctx->visitUser);
        }
    }
    return RoboOk;
}

static int QueryPredicate(const HtmlNodeView *view, void *user)
{
    return ElementMatchesQuery(&view->handle, view, (const QueryContext *)user)
               ? 1
               : 0;
}

/* Collect every match under @p root (or every match in the document). */
static Result CollectQuery(const HtmlNodeHandle *root,
                           QueryContext *ctx,
                           size_t *outCount,
                           HtmlDiag *diag)
{
    Result status;

    if (outCount) {
        *outCount = 0u;
    }
    status = HtmlNodeWalkDescendants(root, RoboHtmlNodeFilterElements, QueryVisit,
                                     ctx, diag);
    if (status != RoboOk) {
        return status;
    }
    if (outCount) {
        *outCount = ctx->count;
    }
    return RoboOk;
}

static Result DocumentRootOrError(HtmlDocumentId documentId,
                                  HtmlNodeHandle *outRoot,
                                  HtmlDiag *diag)
{
    *outRoot = HtmlDocumentRoot(documentId);
    if (!HtmlNodeHandleIsWellformed(outRoot)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown or destroyed document");
    }
    return RoboOk;
}

Result HtmlDocumentGetElementById(
    HtmlDocumentId documentId,
    const char *id,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    QueryContext ctx;
    HtmlNodeHandle root;
    Result status;

    if (!out || !id) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboInvalidArgument,
                              "an output handle and an id are required");
    }
    HtmlNodeHandleClear(out);
    status = DocumentRootOrError(documentId, &root, diag);
    if (status != RoboOk) {
        return status;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.id = id;
    return HtmlNodeFindDescendant(&root, RoboHtmlNodeFilterElements,
                                  QueryPredicate, &ctx, out, diag);
}

Result HtmlDocumentGetElementsByTagName(
    HtmlDocumentId documentId,
    const char *name,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag)
{
    QueryContext ctx;
    HtmlNodeHandle root;
    Result status;

    if (!name) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboInvalidArgument,
                              "a tag name is required");
    }
    status = DocumentRootOrError(documentId, &root, diag);
    if (status != RoboOk) {
        if (outCount) {
            *outCount = 0u;
        }
        return status;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.name = name;
    ctx.visit = visit;
    ctx.visitUser = user;
    return CollectQuery(&root, &ctx, outCount, diag);
}

Result HtmlElementGetElementsByTagName(
    const HtmlNodeHandle *root,
    const char *name,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag)
{
    QueryContext ctx;

    if (!root || !name) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboInvalidArgument,
                              "a root and a tag name are required");
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.name = name;
    ctx.visit = visit;
    ctx.visitUser = user;
    return CollectQuery(root, &ctx, outCount, diag);
}

Result HtmlDocumentGetElementsByClassName(
    HtmlDocumentId documentId,
    const char *classes,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag)
{
    QueryContext ctx;
    HtmlNodeHandle root;
    Result status;

    if (!classes) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboInvalidArgument,
                              "a class list is required");
    }
    status = DocumentRootOrError(documentId, &root, diag);
    if (status != RoboOk) {
        if (outCount) {
            *outCount = 0u;
        }
        return status;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.classes = classes;
    ctx.visit = visit;
    ctx.visitUser = user;
    return CollectQuery(&root, &ctx, outCount, diag);
}

Result HtmlElementGetElementsByClassName(
    const HtmlNodeHandle *root,
    const char *classes,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag)
{
    QueryContext ctx;

    if (!root || !classes) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboInvalidArgument,
                              "a root and a class list are required");
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.classes = classes;
    ctx.visit = visit;
    ctx.visitUser = user;
    return CollectQuery(root, &ctx, outCount, diag);
}

/* ------------------------------------------------------------------ */
/* Selector matching seam                                              */
/* ------------------------------------------------------------------ */

Result HtmlDocumentSetSelectorMatcher(
    HtmlDocumentId documentId,
    HtmlSelectorMatchFn match,
    void *userdata,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown or destroyed document");
    }
    slot->selectorMatch = match;
    slot->selectorUserdata = match ? userdata : NULL;
    return RoboOk;
}

int HtmlDocumentHasSelectorMatcher(HtmlDocumentId documentId)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    return (slot && slot->selectorMatch) ? 1 : 0;
}

static Result SelectorContextFor(const HtmlNodeHandle *root,
                                 const char *selector,
                                 QueryContext *ctx,
                                 HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(root->documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown or destroyed document");
    }
    if (!slot->selectorMatch) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboNotSupported,
                              "no selector matcher is installed");
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->selector = selector;
    ctx->matcher = slot->selectorMatch;
    ctx->matcherUser = slot->selectorUserdata;
    return RoboOk;
}

Result HtmlDocumentQuerySelector(
    HtmlDocumentId documentId,
    const char *selector,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    QueryContext ctx;
    HtmlNodeHandle root;
    Result status;

    if (!out || !selector) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboInvalidArgument,
                              "an output handle and a selector are required");
    }
    HtmlNodeHandleClear(out);
    status = DocumentRootOrError(documentId, &root, diag);
    if (status != RoboOk) {
        return status;
    }
    status = SelectorContextFor(&root, selector, &ctx, diag);
    if (status != RoboOk) {
        return status;
    }
    return HtmlNodeFindDescendant(&root, RoboHtmlNodeFilterElements,
                                  QueryPredicate, &ctx, out, diag);
}

Result HtmlElementQuerySelector(
    const HtmlNodeHandle *root,
    const char *selector,
    HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    QueryContext ctx;
    Result status;

    if (!root || !out || !selector) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboInvalidArgument,
                              "a root, an output handle and a selector are required");
    }
    HtmlNodeHandleClear(out);
    status = SelectorContextFor(root, selector, &ctx, diag);
    if (status != RoboOk) {
        return status;
    }
    return HtmlNodeFindDescendant(root, RoboHtmlNodeFilterElements,
                                  QueryPredicate, &ctx, out, diag);
}

Result HtmlDocumentQuerySelectorAll(
    HtmlDocumentId documentId,
    const char *selector,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag)
{
    QueryContext ctx;
    HtmlNodeHandle root;
    Result status;

    if (!selector) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboInvalidArgument,
                              "a selector is required");
    }
    if (outCount) {
        *outCount = 0u;
    }
    status = DocumentRootOrError(documentId, &root, diag);
    if (status != RoboOk) {
        return status;
    }
    status = SelectorContextFor(&root, selector, &ctx, diag);
    if (status != RoboOk) {
        return status;
    }
    ctx.visit = visit;
    ctx.visitUser = user;
    return CollectQuery(&root, &ctx, outCount, diag);
}

Result HtmlElementQuerySelectorAll(
    const HtmlNodeHandle *root,
    const char *selector,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag)
{
    QueryContext ctx;
    Result status;

    if (!root || !selector) {
        return RoboHtmlDiag(diag, RoboHtmlDomainTraversal, RoboInvalidArgument,
                              "a root and a selector are required");
    }
    if (outCount) {
        *outCount = 0u;
    }
    status = SelectorContextFor(root, selector, &ctx, diag);
    if (status != RoboOk) {
        return status;
    }
    ctx.visit = visit;
    ctx.visitUser = user;
    return CollectQuery(root, &ctx, outCount, diag);
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

Result HtmlDomCompletionGetCapabilities(HtmlDomCompletionCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->creation = 1;
    out->cloning = 1;
    out->textContent = 1;
    out->prepend = 1;
    out->lookupById = 1;
    out->lookupByTag = 1;
    out->lookupByClass = 1;
    out->selectorSeam = 1;
    /*
     * The engine that *parses* a selector and evaluates combinators is
     * RoboCSS's; this module only holds the seam, so this is 0 by design and
     * must stay 0 — a build that made it 1 would have grown a second matcher.
     */
    out->selectorEngine = 0;
    /* Serialization needs the parser; that is a later phase. */
    out->innerHtml = 0;
    return RoboOk;
}
