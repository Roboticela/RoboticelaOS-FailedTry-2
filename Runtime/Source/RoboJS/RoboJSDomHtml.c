/*
 * The RoboHTML adapter (Phase 16.10) — the one file in RoboJS that includes a RoboHTML header, and
 * therefore the one file that knows what a DOM is.
 *
 * `RoboJS/RoboJSDom.h` states the seam: the binding asks a `JsDomHost` what a node is and where it
 * goes. This file answers over `HtmlNodeHandle`s, so a document RoboHTML parsed is the tree a script
 * mutates — through `HtmlNodeAppendChild` and friends, which are the same doors the shell's own
 * mutation path uses, and therefore through whatever invalidation RoboHTML already hooked up.
 *
 * ## Pending construction records, and why they are not a second tree
 *
 * RoboHTML's element and text models have no detached state: `HtmlElementCreate` and `HtmlTextCreate`
 * link the node into a parent in the same call. A DOM needs `document.createElement("div")` to answer
 * a node *before* the script decides where it goes, so this adapter holds the tag, the attributes and
 * (optionally) the text of an unattached node in a small fixed record under a synthetic node id, and
 * materializes it through the real create door at the first `appendChild`/`insertBefore`.
 *
 * That record stores no children and no parent; it is a construction descriptor, and it ceases to
 * exist the moment the node joins the tree. `JsDomHtmlGetCounters().pendingCreated` is the number a
 * test asserts is zero at the end of a document's life.
 *
 * ## A dead document refuses rather than crashes
 *
 * `JsDomHtmlDetach` unbinds; a document destroyed underneath a still-attached runtime makes `isLive`
 * answer 0, and every mutating door then refuses rather than touching freed state
 * (`Documentation/RoboJS/DOMBindings.md` names the case that asserts it).
 */

#include "RoboJS/RoboJSDomHtml.h"

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboJS/RoboJSDom.h"
#include "RoboMem/RoboMem.h"

#include "RoboJSInternal.h"

#include <stdio.h>
#include <string.h>

/** Unattached constructions one document may hold. Bounded like every other table in RoboJS. */
#define JsDomHtmlMaxPending 64
/** Attributes a pending element may carry before it joins the tree. */
#define JsDomHtmlMaxPendingAttributes 16
/** Pending ids live above the host's own id space, which is dense and small. */
#define JsDomHtmlPendingBase 0x8000000000000000ull

typedef struct JsDomHtmlPendingAttribute {
    char name[48];
    char value[256];
} JsDomHtmlPendingAttribute;

typedef struct JsDomHtmlPending {
    int inUse;
    int isText;
    JsDomNodeId id;
    char tag[48];
    char text[256];
    JsDomHtmlPendingAttribute attributes[JsDomHtmlMaxPendingAttributes];
    int attributeCount;
} JsDomHtmlPending;

typedef struct JsDomHtmlState {
    JsRuntime *runtime;
    HtmlDocumentId document;
    int attached;
    JsDomHtmlPending pending[JsDomHtmlMaxPending];
    int32_t pendingCount;
} JsDomHtmlState;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static void JsDomHtmlLower(char *text)
{
    int32_t i;

    for (i = 0; text[i] != '\0'; ++i) {
        if (text[i] >= 'A' && text[i] <= 'Z') {
            text[i] = (char)(text[i] - 'A' + 'a');
        }
    }
}

static void JsDomHtmlCopy(char *destination, size_t capacity, const char *source, size_t length)
{
    size_t copy = length;

    if (capacity == 0) {
        return;
    }
    if (copy > capacity - 1u) {
        copy = capacity - 1u;
    }
    if (source != NULL && copy > 0) {
        memcpy(destination, source, copy);
    }
    destination[copy] = '\0';
}

static JsDomHtmlState *JsDomHtmlStateOf(JsRuntime *runtime)
{
    if (runtime == NULL || runtime->domUserdataOwned == NULL) {
        return NULL;
    }
    return (JsDomHtmlState *)runtime->domUserdataOwned;
}

static HtmlNodeHandle JsDomHtmlHandleOf(JsDomHtmlState *state, JsDomNodeId node)
{
    HtmlNodeHandle handle;

    HtmlNodeHandleClear(&handle);
    if (state == NULL || node == 0 || node >= JsDomHtmlPendingBase) {
        return handle;
    }
    if (HtmlDocumentNodeById(state->document, (HtmlNodeId)node, &handle, NULL) != RoboOk) {
        HtmlNodeHandleClear(&handle);
    }
    return handle;
}

/** The host node id behind a DOM node id, or 0 when the id names a pending record. */
static JsDomNodeId JsDomHtmlResolve(JsDomHtmlState *state, JsDomNodeId node)
{
    if (state == NULL || node >= JsDomHtmlPendingBase) {
        return 0;
    }
    return node;
}

static JsDomHtmlPending *JsDomHtmlPendingOf(JsDomHtmlState *state, JsDomNodeId node)
{
    int32_t i;

    if (state == NULL || node < JsDomHtmlPendingBase) {
        return NULL;
    }
    for (i = 0; i < JsDomHtmlMaxPending; ++i) {
        if (state->pending[i].inUse && state->pending[i].id == node) {
            return &state->pending[i];
        }
    }
    return NULL;
}

static JsDomHtmlPending *JsDomHtmlPendingFor(JsRuntime *runtime, JsDomNodeId node);
static JsDomNodeId JsDomHtmlMaterialize(JsRuntime *runtime, JsDomNodeId node, JsDomNodeId parent);

/* ------------------------------------------------------------------ */
/* The vtable's doors                                                  */
/* ------------------------------------------------------------------ */

static int JsDomHtmlHostLive(void *userdata)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;

    if (state == NULL || !state->attached) {
        return 0;
    }
    return HtmlDocumentIsLive(state->document) ? 1 : 0;
}

static JsDomNodeId JsDomHtmlHostDocumentRoot(void *userdata)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle root;

    if (!JsDomHtmlHostLive(userdata)) {
        return 0;
    }
    root = HtmlDocumentRoot(state->document);
    return (JsDomNodeId)root.nodeId;
}

static JsDomNodeId JsDomHtmlHostDocumentBody(void *userdata)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle root;
    HtmlNodeHandle body;

    if (!JsDomHtmlHostLive(userdata)) {
        return 0;
    }
    root = HtmlDocumentRoot(state->document);
    HtmlNodeHandleClear(&body);
    if (HtmlNodeFindElement(&root, "body", &body, NULL) != RoboOk) {
        return 0;
    }
    return (JsDomNodeId)body.nodeId;
}

typedef struct JsDomHtmlSearch {
    const char *attribute; /* NULL: match by tag */
    const char *tag;
    const char *value;
    JsDomNodeId found;
    int32_t visited;
    int32_t limit;
} JsDomHtmlSearch;

static void JsDomHtmlSearchSubtree(JsDomHtmlSearch *search, HtmlNodeHandle node)
{
    size_t count = 0;
    size_t i;

    if (search->found != 0 || search->visited >= search->limit) {
        return;
    }
    search->visited += 1;
    if (HtmlElementIsElement(&node)) {
        if (search->tag != NULL && HtmlElementIsNamed(&node, search->tag)) {
            search->found = (JsDomNodeId)node.nodeId;
            return;
        }
        if (search->attribute != NULL) {
            const char *value = HtmlElementAttributeValue(&node, search->attribute, NULL);

            if (value != NULL && strcmp(value, search->value) == 0) {
                search->found = (JsDomNodeId)node.nodeId;
                return;
            }
        }
    }
    HtmlNodeChildCount(&node, &count, NULL);
    for (i = 0; i < count; ++i) {
        HtmlNodeHandle child;

        HtmlNodeHandleClear(&child);
        if (HtmlNodeChildAt(&node, i, &child, NULL) != RoboOk) {
            continue;
        }
        JsDomHtmlSearchSubtree(search, child);
        if (search->found != 0) {
            return;
        }
    }
}

static JsDomNodeId JsDomHtmlHostFindById(void *userdata, const char *id)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    JsDomHtmlSearch search;
    HtmlNodeHandle root;

    if (!JsDomHtmlHostLive(userdata) || id == NULL) {
        return 0;
    }
    root = HtmlDocumentRoot(state->document);
    memset(&search, 0, sizeof(search));
    search.attribute = "id";
    search.value = id;
    search.limit = 8192;
    JsDomHtmlSearchSubtree(&search, root);
    return search.found;
}

static JsDomNodeId JsDomHtmlHostFindByTag(void *userdata, JsDomNodeId root, const char *tag)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    JsDomHtmlSearch search;
    HtmlNodeHandle handle;
    char lowered[48];

    if (!JsDomHtmlHostLive(userdata) || tag == NULL) {
        return 0;
    }
    if (root == 0) {
        /* A root of 0 means "the whole document", which is what a search with no starting node means
         * and what `getElementsByTagName` needs when it is called on `document`. */
        handle = HtmlDocumentRoot(state->document);
    } else {
        handle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, root));
    }
    if (!HtmlNodeHandleIsWellformed(&handle)) {
        return 0;
    }
    JsDomHtmlCopy(lowered, sizeof(lowered), tag, strlen(tag));
    JsDomHtmlLower(lowered);

    memset(&search, 0, sizeof(search));
    search.tag = lowered;
    search.limit = 8192;
    JsDomHtmlSearchSubtree(&search, handle);
    return search.found;
}

static JsDomHtmlPending *JsDomHtmlAllocatePending(JsRuntime *runtime, int isText)
{
    JsDomHtmlState *state = JsDomHtmlStateOf(runtime);
    int32_t i;

    if (state == NULL) {
        return NULL;
    }
    for (i = 0; i < JsDomHtmlMaxPending; ++i) {
        if (!state->pending[i].inUse) {
            memset(&state->pending[i], 0, sizeof(state->pending[i]));
            state->pending[i].inUse = 1;
            state->pending[i].isText = isText;
            state->pending[i].id = JsDomHtmlPendingBase + (JsDomNodeId)i + 1u;
            state->pendingCount += 1;
            runtime->counters.pendingCreated += 1;
            return &state->pending[i];
        }
    }
    runtime->counters.refusals += 1;
    return NULL;
}

static JsDomNodeId JsDomHtmlHostCreateElement(void *userdata, const char *tag)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    JsDomHtmlPending *pending;

    if (!JsDomHtmlHostLive(userdata) || tag == NULL || tag[0] == '\0' ||
        HtmlElementNameValidate(tag, NULL) != RoboOk) {
        return 0;
    }
    pending = JsDomHtmlAllocatePending(state->runtime, 0);
    if (pending == NULL) {
        return 0;
    }
    JsDomHtmlCopy(pending->tag, sizeof(pending->tag), tag, strlen(tag));
    JsDomHtmlLower(pending->tag);
    return pending->id;
}

static JsDomNodeId JsDomHtmlHostCreateTextNode(void *userdata,
                                              const char *text,
                                              int32_t length)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    JsDomHtmlPending *pending;

    if (!JsDomHtmlHostLive(userdata)) {
        return 0;
    }
    pending = JsDomHtmlAllocatePending(state->runtime, 1);
    if (pending == NULL) {
        return 0;
    }
    if (text == NULL || length < 0) {
        pending->text[0] = '\0';
    } else {
        JsDomHtmlCopy(pending->text, sizeof(pending->text), text, (size_t)length);
    }
    return pending->id;
}

static JsDomNodeId JsDomHtmlHostParent(void *userdata, JsDomNodeId node)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    HtmlNodeView view;

    if (!JsDomHtmlHostLive(userdata)) {
        return 0;
    }
    handle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, node));
    if (!HtmlNodeHandleIsWellformed(&handle) || HtmlNodeGetView(&handle, &view, NULL) != RoboOk) {
        return 0;
    }
    if (!HtmlNodeHandleIsWellformed(&view.parent)) {
        return 0;
    }
    return (JsDomNodeId)view.parent.nodeId;
}

static JsDomNodeId JsDomHtmlHostFirstChild(void *userdata, JsDomNodeId node)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    HtmlNodeView view;

    if (!JsDomHtmlHostLive(userdata)) {
        return 0;
    }
    handle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, node));
    if (!HtmlNodeHandleIsWellformed(&handle) || HtmlNodeGetView(&handle, &view, NULL) != RoboOk) {
        return 0;
    }
    return HtmlNodeHandleIsWellformed(&view.firstChild) ? (JsDomNodeId)view.firstChild.nodeId : 0;
}

static JsDomNodeId JsDomHtmlHostLastChild(void *userdata, JsDomNodeId node)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    HtmlNodeView view;

    if (!JsDomHtmlHostLive(userdata)) {
        return 0;
    }
    handle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, node));
    if (!HtmlNodeHandleIsWellformed(&handle) || HtmlNodeGetView(&handle, &view, NULL) != RoboOk) {
        return 0;
    }
    return HtmlNodeHandleIsWellformed(&view.lastChild) ? (JsDomNodeId)view.lastChild.nodeId : 0;
}

static JsDomNodeId JsDomHtmlHostNextSibling(void *userdata, JsDomNodeId node)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    HtmlNodeView view;

    if (!JsDomHtmlHostLive(userdata)) {
        return 0;
    }
    handle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, node));
    if (!HtmlNodeHandleIsWellformed(&handle) || HtmlNodeGetView(&handle, &view, NULL) != RoboOk) {
        return 0;
    }
    return HtmlNodeHandleIsWellformed(&view.NextSibling) ? (JsDomNodeId)view.NextSibling.nodeId : 0;
}

static JsDomNodeId JsDomHtmlHostPreviousSibling(void *userdata, JsDomNodeId node)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    HtmlNodeView view;

    if (!JsDomHtmlHostLive(userdata)) {
        return 0;
    }
    handle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, node));
    if (!HtmlNodeHandleIsWellformed(&handle) || HtmlNodeGetView(&handle, &view, NULL) != RoboOk) {
        return 0;
    }
    return HtmlNodeHandleIsWellformed(&view.prevSibling) ? (JsDomNodeId)view.prevSibling.nodeId : 0;
}

static int32_t JsDomHtmlHostChildCount(void *userdata, JsDomNodeId node)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    size_t count = 0;

    if (!JsDomHtmlHostLive(userdata)) {
        return 0;
    }
    handle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, node));
    if (!HtmlNodeHandleIsWellformed(&handle) || HtmlNodeChildCount(&handle, &count, NULL) != RoboOk) {
        return 0;
    }
    return (int32_t)count;
}

static JsDomNodeId JsDomHtmlHostChildAt(void *userdata, JsDomNodeId node, int32_t index)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    HtmlNodeHandle child;

    if (!JsDomHtmlHostLive(userdata) || index < 0) {
        return 0;
    }
    handle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, node));
    HtmlNodeHandleClear(&child);
    if (!HtmlNodeHandleIsWellformed(&handle) ||
        HtmlNodeChildAt(&handle, (size_t)index, &child, NULL) != RoboOk) {
        return 0;
    }
    return (JsDomNodeId)child.nodeId;
}

static uint32_t JsDomHtmlHostNodeKind(void *userdata, JsDomNodeId node)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    HtmlNodeKind kind = RoboHtmlNodeKindDocument;

    if (!JsDomHtmlHostLive(userdata) || node == 0) {
        return (uint32_t)RoboJsDomNodeNone;
    }
    if (node >= JsDomHtmlPendingBase) {
        JsDomHtmlPending *pending = JsDomHtmlPendingOf(state, node);

        if (pending == NULL) {
            return (uint32_t)RoboJsDomNodeNone;
        }
        return pending->isText ? (uint32_t)RoboJsDomNodeText : (uint32_t)RoboJsDomNodeElement;
    }
    handle = JsDomHtmlHandleOf(state, node);
    if (!HtmlNodeHandleIsWellformed(&handle) || HtmlNodeHandleResolve(&handle, &kind) != RoboOk) {
        return (uint32_t)RoboJsDomNodeNone;
    }
    switch (kind) {
    case RoboHtmlNodeKindDocument:
        return (uint32_t)RoboJsDomNodeDocument;
    case RoboHtmlNodeKindText:
    case RoboHtmlNodeKindComment:
        return (uint32_t)RoboJsDomNodeText;
    default:
        return (uint32_t)RoboJsDomNodeElement;
    }
}

static int32_t JsDomHtmlHostNodeName(void *userdata, JsDomNodeId node, char *buffer, int32_t capacity)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    const char *name = NULL;
    size_t length = 0;

    if (capacity <= 0) {
        return -1;
    }
    buffer[0] = '\0';
    if (!JsDomHtmlHostLive(userdata)) {
        return -1;
    }
    if (node >= JsDomHtmlPendingBase) {
        JsDomHtmlPending *pending = JsDomHtmlPendingOf(state, node);

        if (pending == NULL) {
            return -1;
        }
        name = pending->isText ? "#text" : pending->tag;
    } else {
        handle = JsDomHtmlHandleOf(state, node);
        if (!HtmlNodeHandleIsWellformed(&handle)) {
            return -1;
        }
        if (HtmlElementIsElement(&handle)) {
            name = HtmlElementName(&handle, &length);
        } else if (HtmlDocumentRoot(state->document).nodeId == (HtmlNodeId)node) {
            name = "#document";
        } else {
            name = "#text";
        }
    }
    if (name == NULL) {
        name = "";
    }
    if (length == 0) {
        length = strlen(name);
    }
    JsDomHtmlCopy(buffer, (size_t)capacity, name, length);
    return (int32_t)length;
}

static int32_t JsDomHtmlHostTextContent(void *userdata,
                                        JsDomNodeId node,
                                        char *buffer,
                                        int32_t capacity)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    size_t needed = 0;
    Result status;

    if (capacity <= 0) {
        return -1;
    }
    buffer[0] = '\0';
    if (!JsDomHtmlHostLive(userdata)) {
        return -1;
    }
    if (node >= JsDomHtmlPendingBase) {
        JsDomHtmlPending *pending = JsDomHtmlPendingOf(state, node);

        if (pending == NULL) {
            return -1;
        }
        JsDomHtmlCopy(buffer, (size_t)capacity, pending->text, strlen(pending->text));
        return (int32_t)strlen(pending->text);
    }
    handle = JsDomHtmlHandleOf(state, node);
    if (!HtmlNodeHandleIsWellformed(&handle)) {
        return -1;
    }
    if (HtmlTextIsText(&handle)) {
        const char *content = HtmlTextContent(&handle, &needed);

        if (content == NULL) {
            return 0;
        }
        JsDomHtmlCopy(buffer, (size_t)capacity, content, needed);
        return (int32_t)needed;
    }
    status = HtmlNodeSubtreeText(&handle, buffer, (size_t)capacity, &needed, NULL);
    if (status != RoboOk) {
        return -1;
    }
    /* A truncated read is a length report rather than an error: the caller sizes a second attempt
     * from the answer, which is the contract every length-answering door in this module follows. */
    return (int32_t)needed;
}

static Result JsDomHtmlHostSetTextContent(void *userdata,
                                          JsDomNodeId node,
                                          const char *text,
                                          int32_t length)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    HtmlNodeHandle created;
    size_t children = 0;
    HtmlTextCreateInfo info;

    if (!JsDomHtmlHostLive(userdata)) {
        if (state != NULL && state->runtime != NULL) {
            state->runtime->counters.refusals += 1;
        }
        return RoboInvalidArgument;
    }
    if (text == NULL) {
        length = 0;
        text = "";
    }
    if (node >= JsDomHtmlPendingBase) {
        JsDomHtmlPending *pending = JsDomHtmlPendingOf(state, node);

        if (pending == NULL) {
            return RoboNotFound;
        }
        JsDomHtmlCopy(pending->text, sizeof(pending->text), text, (size_t)length);
        return RoboOk;
    }
    handle = JsDomHtmlHandleOf(state, node);
    if (!HtmlNodeHandleIsWellformed(&handle)) {
        return RoboNotFound;
    }
    if (HtmlTextIsText(&handle)) {
        /* A text node's own write is `HtmlTextSet`, which is not a construction operation: it rewrites
         * a run the node already owns. So a script editing text a document already has keeps working on
         * a finalized document -- which is exactly the split the tree model draws. */
        return HtmlTextSet(&handle, text, (size_t)length, NULL);
    }
    /* An element's (or the document's) `textContent` write is "replace the children with one text
     * node". Two of the three shapes that can take are answerable without creating anything, and the
     * adapter takes them in the order that rewrites the least:
     *
     *   1. The element's lone child is already a text node (`<p>hello</p>`). The write is one
     *      `HtmlTextSet` -- a run rewrite, not a construction operation -- so it works on a document
     *      whose construction is closed.
     *   2. The element is empty and its own text may be created as a fresh node. Only a document that
     *      still accepts construction can answer this, and a parsed document does not (`RoboHTML's
     *      tree builder finalizes when its input is exhausted`, and Phase 8 pins the refusal in
     *      `SuiteRoboHTMLMutation`/`SuiteRoboHTMLElement`).
     *   3. Anything else (several children, or children of mixed kinds) needs both a removal and a
     *      creation, so it is case 2 or a refusal.
     *
     * **The order here is the whole point: the gate is asked before anything is destroyed.** Cases 1 and
     * 3 could be mistaken for "remove the children, then build the text", and doing that first would
     * delete an element's text in order to report that it could not be replaced -- a data-destroying
     * answer to a refused write. So every shape is classified first, and a shape this adapter cannot
     * perform leaves the tree exactly as it was. */
    if (HtmlNodeChildCount(&handle, &children, NULL) != RoboOk) {
        return RoboNotFound;
    }
    if (children == 1u) {
        HtmlNodeHandle only;
        HtmlNodeView view;

        memset(&view, 0, sizeof(view));
        if (HtmlNodeGetView(&handle, &view, NULL) != RoboOk) {
            return RoboNotFound;
        }
        if (HtmlNodeHandleIsWellformed(&view.firstChild) &&
            HtmlNodeHandleIsWellformed(&view.lastChild) &&
            view.firstChild.nodeId == view.lastChild.nodeId) {
            only = view.firstChild;
            if (HtmlTextIsText(&only)) {
                return HtmlTextSet(&only, text, (size_t)length, NULL);
            }
        }
    }
    if (!HtmlDocumentAllowsConstruction(state->document)) {
        state->runtime->counters.refusals += 1;
        return RoboNotSupported;
    }
    if (children > 0u) {
        if (HtmlNodeRemoveChildren(&handle, NULL, NULL) != RoboOk) {
            /* Nothing was removed, so the element still holds every child it had. */
            return RoboNotSupported;
        }
    }
    HtmlTextCreateInfoInit(&info);
    info.parent = handle;
    info.origin = RoboHtmlNodeOriginDom;
    info.content = text;
    info.length = (size_t)length;
    HtmlNodeHandleClear(&created);
    if (HtmlTextCreate(state->document, &info, &created, NULL) != RoboOk) {
        return RoboNotSupported;
    }
    /* Counted here, not by the caller: a text node this write built is a text node the adapter caused
     * to exist, and `JsDomHtmlCounters::textNodesCreated` is the one number that answers "did that
     * `textContent` write create anything?". */
    state->runtime->counters.textNodesCreated += 1;
    return RoboOk;
}

static int32_t JsDomHtmlHostAttribute(void *userdata,
                                     JsDomNodeId node,
                                     const char *name,
                                     char *buffer,
                                     int32_t capacity)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    const char *value = NULL;
    size_t length = 0;
    char lowered[48];
    int32_t i;

    if (capacity <= 0) {
        return -1;
    }
    buffer[0] = '\0';
    if (!JsDomHtmlHostLive(userdata) || name == NULL) {
        return -1;
    }
    JsDomHtmlCopy(lowered, sizeof(lowered), name, strlen(name));
    JsDomHtmlLower(lowered);

    if (node >= JsDomHtmlPendingBase) {
        JsDomHtmlPending *pending = JsDomHtmlPendingOf(state, node);

        if (pending == NULL) {
            return -1;
        }
        for (i = 0; i < pending->attributeCount; ++i) {
            if (strcmp(pending->attributes[i].name, lowered) == 0) {
                length = strlen(pending->attributes[i].value);
                JsDomHtmlCopy(buffer, (size_t)capacity, pending->attributes[i].value, length);
                return (int32_t)length;
            }
        }
        return -1;
    }
    handle = JsDomHtmlHandleOf(state, node);
    if (!HtmlNodeHandleIsWellformed(&handle)) {
        return -1;
    }
    value = HtmlElementAttributeValue(&handle, lowered, &length);
    if (value == NULL) {
        return -1;
    }
    JsDomHtmlCopy(buffer, (size_t)capacity, value, length);
    return (int32_t)length;
}

static Result JsDomHtmlHostSetAttribute(void *userdata,
                                        JsDomNodeId node,
                                        const char *name,
                                        const char *value)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    char lowered[48];
    int32_t i;

    if (!JsDomHtmlHostLive(userdata)) {
        if (state != NULL && state->runtime != NULL) {
            state->runtime->counters.refusals += 1;
        }
        return RoboInvalidArgument;
    }
    if (name == NULL || name[0] == '\0') {
        return RoboInvalidArgument;
    }
    JsDomHtmlCopy(lowered, sizeof(lowered), name, strlen(name));
    JsDomHtmlLower(lowered);
    if (value == NULL) {
        value = "";
    }
    if (node >= JsDomHtmlPendingBase) {
        JsDomHtmlPending *pending = JsDomHtmlPendingOf(state, node);

        if (pending == NULL) {
            return RoboNotFound;
        }
        for (i = 0; i < pending->attributeCount; ++i) {
            if (strcmp(pending->attributes[i].name, lowered) == 0) {
                JsDomHtmlCopy(pending->attributes[i].value,
                              sizeof(pending->attributes[i].value),
                              value,
                              strlen(value));
                return RoboOk;
            }
        }
        if (pending->attributeCount >= JsDomHtmlMaxPendingAttributes) {
            return RoboNotSupported;
        }
        JsDomHtmlCopy(pending->attributes[pending->attributeCount].name,
                      sizeof(pending->attributes[0].name),
                      lowered,
                      strlen(lowered));
        JsDomHtmlCopy(pending->attributes[pending->attributeCount].value,
                      sizeof(pending->attributes[0].value),
                      value,
                      strlen(value));
        pending->attributeCount += 1;
        return RoboOk;
    }
    handle = JsDomHtmlHandleOf(state, node);
    if (!HtmlNodeHandleIsWellformed(&handle)) {
        return RoboNotFound;
    }
    return HtmlElementSetAttribute(&handle, lowered, value, NULL);
}

static Result JsDomHtmlHostRemoveAttribute(void *userdata, JsDomNodeId node, const char *name)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    char lowered[48];
    int32_t i;

    if (!JsDomHtmlHostLive(userdata)) {
        if (state != NULL && state->runtime != NULL) {
            state->runtime->counters.refusals += 1;
        }
        return RoboInvalidArgument;
    }
    if (name == NULL) {
        return RoboInvalidArgument;
    }
    JsDomHtmlCopy(lowered, sizeof(lowered), name, strlen(name));
    JsDomHtmlLower(lowered);
    if (node >= JsDomHtmlPendingBase) {
        JsDomHtmlPending *pending = JsDomHtmlPendingOf(state, node);

        if (pending == NULL) {
            return RoboNotFound;
        }
        for (i = 0; i < pending->attributeCount; ++i) {
            if (strcmp(pending->attributes[i].name, lowered) == 0) {
                int32_t move;

                for (move = i; move + 1 < pending->attributeCount; ++move) {
                    pending->attributes[move] = pending->attributes[move + 1];
                }
                pending->attributeCount -= 1;
                memset(&pending->attributes[pending->attributeCount], 0,
                       sizeof(pending->attributes[0]));
                return RoboOk;
            }
        }
        return RoboOk;
    }
    handle = JsDomHtmlHandleOf(state, node);
    if (!HtmlNodeHandleIsWellformed(&handle)) {
        return RoboNotFound;
    }
    return HtmlElementRemoveAttribute(&handle, lowered, NULL);
}

static int32_t JsDomHtmlHostAttributeCount(void *userdata, JsDomNodeId node)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    size_t count = 0;

    if (!JsDomHtmlHostLive(userdata)) {
        return 0;
    }
    if (node >= JsDomHtmlPendingBase) {
        JsDomHtmlPending *pending = JsDomHtmlPendingOf(state, node);

        return pending == NULL ? 0 : pending->attributeCount;
    }
    handle = JsDomHtmlHandleOf(state, node);
    if (!HtmlNodeHandleIsWellformed(&handle) ||
        HtmlElementAttributeCount(&handle, &count, NULL) != RoboOk) {
        return 0;
    }
    return (int32_t)count;
}

static int32_t JsDomHtmlHostAttributeNameAt(void *userdata,
                                            JsDomNodeId node,
                                            int32_t index,
                                            char *buffer,
                                            int32_t capacity)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle handle;
    HtmlAttribute attribute;
    size_t length;

    if (capacity <= 0) {
        return -1;
    }
    buffer[0] = '\0';
    if (!JsDomHtmlHostLive(userdata) || index < 0) {
        return -1;
    }
    if (node >= JsDomHtmlPendingBase) {
        JsDomHtmlPending *pending = JsDomHtmlPendingOf(state, node);

        if (pending == NULL || index >= pending->attributeCount) {
            return -1;
        }
        length = strlen(pending->attributes[index].name);
        JsDomHtmlCopy(buffer, (size_t)capacity, pending->attributes[index].name, length);
        return (int32_t)length;
    }
    handle = JsDomHtmlHandleOf(state, node);
    HtmlAttributeInit(&attribute);
    if (!HtmlNodeHandleIsWellformed(&handle) ||
        HtmlElementAttributeAt(&handle, (size_t)index, &attribute, NULL) != RoboOk) {
        return -1;
    }
    JsDomHtmlCopy(buffer, (size_t)capacity, attribute.name, attribute.nameLength);
    return (int32_t)attribute.nameLength;
}

/* ------------------------------------------------------------------ */
/* Materialization                                                     */
/* ------------------------------------------------------------------ */

static JsDomHtmlPending *JsDomHtmlPendingFor(JsRuntime *runtime, JsDomNodeId node)
{
    return JsDomHtmlPendingOf(JsDomHtmlStateOf(runtime), node);
}

static JsDomNodeId JsDomHtmlMaterialize(JsRuntime *runtime, JsDomNodeId node, JsDomNodeId parent)
{
    JsDomHtmlState *state = JsDomHtmlStateOf(runtime);
    JsDomHtmlPending *pending;
    HtmlNodeHandle parentHandle;
    HtmlNodeHandle created;

    if (state == NULL) {
        return 0;
    }
    /* **This is a pass-through, not a filter.** A node that is already one of RoboHTML's needs no
     * materialising, and answering 0 for it would make `appendChild` of a node the document already
     * has -- the DOM's *move*, and a rearrangement Phase 8 supports -- fail in a way that looks like
     * a bad operand. Only a synthetic id has a record to convert. */
    if (node < JsDomHtmlPendingBase) {
        return node;
    }
    pending = JsDomHtmlPendingFor(runtime, node);
    if (pending == NULL) {
        return 0;
    }
    parentHandle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, parent));
    if (!HtmlNodeHandleIsWellformed(&parentHandle)) {
        return 0;
    }
    HtmlNodeHandleClear(&created);
    if (pending->isText) {
        HtmlTextCreateInfo info;

        HtmlTextCreateInfoInit(&info);
        info.parent = parentHandle;
        info.origin = RoboHtmlNodeOriginDom;
        info.content = pending->text;
        info.length = strlen(pending->text);
        if (HtmlTextCreate(state->document, &info, &created, NULL) != RoboOk) {
            return 0;
        }
        runtime->counters.textNodesCreated += 1;
    } else {
        HtmlElementCreateInfo info;
        HtmlAttribute attributes[JsDomHtmlMaxPendingAttributes];
        size_t count = 0;
        int32_t i;

        HtmlElementCreateInfoInit(&info);
        for (i = 0; i < pending->attributeCount && count < JsDomHtmlMaxPendingAttributes; ++i) {
            memset(&attributes[count], 0, sizeof(attributes[count]));
            attributes[count].name = pending->attributes[i].name;
            attributes[count].nameLength = strlen(pending->attributes[i].name);
            attributes[count].value = pending->attributes[i].value;
            attributes[count].valueLength = strlen(pending->attributes[i].value);
            attributes[count].namespaceId = RoboHtmlNamespaceNone;
            count += 1;
        }
        info.parent = parentHandle;
        info.origin = RoboHtmlNodeOriginDom;
        info.namespaceId = RoboHtmlNamespaceHtml;
        info.localName = pending->tag;
        info.attributes = count > 0 ? attributes : NULL;
        info.attributeCount = count;
        info.selfClosing = 0;
        if (HtmlElementCreate(state->document, &info, &created, NULL) != RoboOk) {
            return 0;
        }
        if (pending->text[0] != '\0') {
            HtmlTextCreateInfo textInfo;
            HtmlNodeHandle textNode;

            HtmlTextCreateInfoInit(&textInfo);
            textInfo.parent = created;
            textInfo.origin = RoboHtmlNodeOriginDom;
            textInfo.content = pending->text;
            textInfo.length = strlen(pending->text);
            HtmlNodeHandleClear(&textNode);
            if (HtmlTextCreate(state->document, &textInfo, &textNode, NULL) != RoboOk) {
                return 0;
            }
            runtime->counters.textNodesCreated += 1;
        }
    }
    {
        JsDomNodeId resolved = (JsDomNodeId)created.nodeId;

        /* **The wrapper the script is holding follows the node.** The record retires, and the interning
         * table's entry for the synthetic id is *moved* onto the real id rather than dropped -- so
         * `holder` after `document.appendChild(holder)` is still the element the script built, and
         * `holder.appendChild(text)` and a later `getElementById("holder") === holder` both work. A
         * build that dropped the entry would leave the script holding a wrapper whose id resolves to
         * nothing, which is the shape of bug that only shows up on the *second* operation. */
        JsDomRetargetWrapper(runtime, pending->id, resolved);
        memset(pending, 0, sizeof(*pending));
        state->pendingCount -= 1;
        runtime->counters.materialized += 1;
        return resolved;
    }
}

static Result JsDomHtmlHostAppendChild(void *userdata, JsDomNodeId parent, JsDomNodeId child)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    JsDomNodeId parentNode = JsDomHtmlResolve(state, parent);
    HtmlNodeHandle parentHandle;
    HtmlNodeHandle childHandle;
    HtmlNodeView view;
    Result status;

    if (!JsDomHtmlHostLive(userdata)) {
        state->runtime->counters.refusals += 1;
        return RoboInvalidArgument;
    }
    /* A detached element is a *pending record*, and linking it into the tree is a node **creation**:
     * `HtmlElementCreate`/`HtmlTextCreate` take the parent they link under, so there is no orphan node
     * to place later and materialisation has to happen here. That makes this the one door where a
     * closed construction gate is answerable with a named reason rather than a generic refusal -- the
     * child's own record is never touched, and the counters say which refusal it was. */
    if (child >= JsDomHtmlPendingBase && !HtmlDocumentAllowsConstruction(state->document)) {
        state->runtime->counters.refusals += 1;
        return RoboNotSupported;
    }
    child = JsDomHtmlMaterialize(state->runtime, child, parent);
    if (child == 0) {
        return RoboInvalidArgument;
    }
    parentHandle = JsDomHtmlHandleOf(state, parentNode);
    childHandle = JsDomHtmlHandleOf(state, child);
    if (!HtmlNodeHandleIsWellformed(&parentHandle) || !HtmlNodeHandleIsWellformed(&childHandle)) {
        return RoboNotFound;
    }
    /* DOM semantics: appending a node that already has a parent moves it. The tree model refuses to
     * steal a node (`RoboAlreadyExists`), so the move is two explicit steps here. */
    if (HtmlNodeGetView(&childHandle, &view, NULL) == RoboOk &&
        HtmlNodeHandleIsWellformed(&view.parent)) {
        HtmlNodeRemoveChild(&view.parent, &childHandle, NULL);
    }
    status = HtmlNodeAppendChild(&parentHandle, &childHandle, NULL);
    if (status != RoboOk) {
        return status;
    }
    return RoboOk;
}

static Result JsDomHtmlHostInsertBefore(void *userdata,
                                        JsDomNodeId parent,
                                        JsDomNodeId child,
                                        JsDomNodeId before)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    JsDomNodeId parentNode = JsDomHtmlResolve(state, parent);
    HtmlNodeHandle parentHandle;
    HtmlNodeHandle childHandle;
    HtmlNodeHandle beforeHandle;
    HtmlNodeView view;
    Result status;

    if (!JsDomHtmlHostLive(userdata)) {
        state->runtime->counters.refusals += 1;
        return RoboInvalidArgument;
    }
    if (child >= JsDomHtmlPendingBase && !HtmlDocumentAllowsConstruction(state->document)) {
        state->runtime->counters.refusals += 1;
        return RoboNotSupported;
    }
    child = JsDomHtmlMaterialize(state->runtime, child, parent);
    if (child == 0) {
        return RoboInvalidArgument;
    }
    parentHandle = JsDomHtmlHandleOf(state, parentNode);
    childHandle = JsDomHtmlHandleOf(state, child);
    HtmlNodeHandleClear(&beforeHandle);
    if (before != 0 && before < JsDomHtmlPendingBase) {
        beforeHandle = JsDomHtmlHandleOf(state, before);
    }
    if (!HtmlNodeHandleIsWellformed(&parentHandle) || !HtmlNodeHandleIsWellformed(&childHandle)) {
        return RoboNotFound;
    }
    if (HtmlNodeGetView(&childHandle, &view, NULL) == RoboOk &&
        HtmlNodeHandleIsWellformed(&view.parent)) {
        HtmlNodeRemoveChild(&view.parent, &childHandle, NULL);
    }
    if (!HtmlNodeHandleIsWellformed(&beforeHandle)) {
        /* `insertBefore(node, null)` is `appendChild`. */
        status = HtmlNodeAppendChild(&parentHandle, &childHandle, NULL);
        return status;
    }
    return HtmlNodeInsertBefore(&parentHandle, &childHandle, &beforeHandle, NULL);
}

static Result JsDomHtmlHostRemoveChild(void *userdata, JsDomNodeId parent, JsDomNodeId child)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle parentHandle;
    HtmlNodeHandle childHandle;

    if (!JsDomHtmlHostLive(userdata)) {
        state->runtime->counters.refusals += 1;
        return RoboInvalidArgument;
    }
    parentHandle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, parent));
    childHandle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, child));
    if (!HtmlNodeHandleIsWellformed(&parentHandle) || !HtmlNodeHandleIsWellformed(&childHandle)) {
        return RoboNotFound;
    }
    return HtmlNodeRemoveChild(&parentHandle, &childHandle, NULL);
}

static int JsDomHtmlHostContains(void *userdata, JsDomNodeId ancestor, JsDomNodeId node)
{
    JsDomHtmlState *state = (JsDomHtmlState *)userdata;
    HtmlNodeHandle ancestorHandle;
    HtmlNodeHandle nodeHandle;
    int contains = 0;

    if (!JsDomHtmlHostLive(userdata)) {
        return 0;
    }
    if (ancestor == node) {
        return 1;
    }
    ancestorHandle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, ancestor));
    nodeHandle = JsDomHtmlHandleOf(state, JsDomHtmlResolve(state, node));
    if (!HtmlNodeHandleIsWellformed(&ancestorHandle) || !HtmlNodeHandleIsWellformed(&nodeHandle)) {
        return 0;
    }
    if (HtmlNodeContains(&ancestorHandle, &nodeHandle, &contains, NULL) != RoboOk) {
        return 0;
    }
    return contains;
}

/* ------------------------------------------------------------------ */
/* Attach / detach                                                     */
/* ------------------------------------------------------------------ */

Result JsDomHtmlAttach(JsRuntime *runtime, HtmlDocumentId documentId)
{
    JsDomHtmlState *state;
    JsDomHost host;
    Result status;

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    if (!HtmlDocumentIsLive(documentId)) {
        return RoboInvalidArgument;
    }
    if (runtime->domUserdataOwned != NULL) {
        return RoboInvalidArgument;
    }
    state = (JsDomHtmlState *)RoboCalloc(1u, sizeof(JsDomHtmlState));
    if (state == NULL) {
        return RoboOutOfMemory;
    }
    state->runtime = runtime;
    state->document = documentId;
    state->attached = 1;
    state->pendingCount = 0;

    memset(&host, 0, sizeof(host));
    host.userdata = state;
    host.isLive = JsDomHtmlHostLive;
    host.documentRoot = JsDomHtmlHostDocumentRoot;
    host.documentBody = JsDomHtmlHostDocumentBody;
    host.findById = JsDomHtmlHostFindById;
    host.findByTag = JsDomHtmlHostFindByTag;
    host.createElement = JsDomHtmlHostCreateElement;
    host.createTextNode = JsDomHtmlHostCreateTextNode;
    host.parent = JsDomHtmlHostParent;
    host.firstChild = JsDomHtmlHostFirstChild;
    host.lastChild = JsDomHtmlHostLastChild;
    host.nextSibling = JsDomHtmlHostNextSibling;
    host.previousSibling = JsDomHtmlHostPreviousSibling;
    host.childCount = JsDomHtmlHostChildCount;
    host.childAt = JsDomHtmlHostChildAt;
    host.nodeKind = JsDomHtmlHostNodeKind;
    host.nodeName = JsDomHtmlHostNodeName;
    host.textContent = JsDomHtmlHostTextContent;
    host.setTextContent = JsDomHtmlHostSetTextContent;
    host.attribute = JsDomHtmlHostAttribute;
    host.setAttribute = JsDomHtmlHostSetAttribute;
    host.removeAttribute = JsDomHtmlHostRemoveAttribute;
    host.attributeCount = JsDomHtmlHostAttributeCount;
    host.attributeNameAt = JsDomHtmlHostAttributeNameAt;
    host.appendChild = JsDomHtmlHostAppendChild;
    host.insertBefore = JsDomHtmlHostInsertBefore;
    host.removeChild = JsDomHtmlHostRemoveChild;
    host.contains = JsDomHtmlHostContains;
    host.invalidate = NULL; /* RoboHTML's own mutation hook already owns invalidation */

    runtime->domUserdataOwned = state;
    status = JsDomInstall(runtime, &host);
    if (status != RoboOk) {
        runtime->domUserdataOwned = NULL;
        RoboFree(state);
        return status;
    }
    return RoboOk;
}

int32_t JsDomHtmlDetach(JsRuntime *runtime)
{
    JsDomHtmlState *state = JsDomHtmlStateOf(runtime);
    int32_t dropped;

    if (state == NULL) {
        return 0;
    }
    state->attached = 0;
    dropped = JsDomUninstall(runtime);
    runtime->domUserdataOwned = NULL;
    RoboFree(state);
    return dropped;
}

HtmlDocumentId JsDomHtmlDocument(JsRuntime *runtime)
{
    JsDomHtmlState *state = JsDomHtmlStateOf(runtime);

    if (state == NULL) {
        return RoboHtmlDocumentIdNone;
    }
    return state->document;
}

int JsDomHtmlIsLive(JsRuntime *runtime)
{
    return JsDomHtmlHostLive(JsDomHtmlStateOf(runtime));
}

Result JsDomHtmlGetCounters(JsRuntime *runtime, JsDomHtmlCounters *out)
{
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    out->created = runtime->counters.created;
    out->pendingCreated = runtime->counters.pendingCreated;
    out->materialized = runtime->counters.materialized;
    out->textNodesCreated = runtime->counters.textNodesCreated;
    out->appends = runtime->counters.appends;
    out->attributeWrites = runtime->counters.attributeWrites;
    out->textWrites = runtime->counters.textWrites;
    out->refusals = runtime->counters.refusals;
    return RoboOk;
}
