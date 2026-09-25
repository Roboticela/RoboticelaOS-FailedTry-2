/*
 * RoboJS's DOM bindings (Phase 16.10) — see `RoboJS/RoboJSDom.h` for the contract.
 *
 * The file is five pieces, in the order a script meets them:
 *
 *   **the wrapper table**  one interning table keyed by the host's node id, so `el.parentNode ===
 *                          el.parentNode` is true and a script cannot make the binding allocate by
 *                          asking the same question twice;
 *   **the prototypes**     `Node.prototype`, `Element.prototype` and `Document.prototype`, with the
 *                          method set this build binds and nothing else;
 *   **the computed half**  `JsHostOps`: `id`, `className`, `textContent`, the sibling/parent links,
 *                          `children`, `attributes`, `style` — every one of them answered by asking
 *                          the host, because a DOM wrapper stores no tree;
 *   **the selectors**      a deliberately small grammar (a comma list of `tag`/`#id`/`.class`/`*`
 *                          compounds) with a validating front door, so an unsupported selector is a
 *                          located `SyntaxError` and not a silent no-match;
 *   **the events**         `addEventListener`/`removeEventListener`/`dispatchEvent`, which are three
 *                          thin doors onto 16.11 rather than a second listener table.
 *
 * Nothing here includes RoboHTML: the host is the vtable, and `RoboJSDomHtml.c` is the one file that
 * knows what a `HtmlDocumentId` is.
 */

#include "RoboJSInternal.h"

#include "RoboBase/RoboBase.h"
#include "RoboJS/RoboJSException.h"
#include "RoboJS/RoboJSFunction.h"
#include "RoboJS/RoboJSHeap.h"
#include "RoboJS/RoboJSObject.h"
#include "RoboJS/RoboJSValue.h"

#include <stdio.h>
#include <string.h>

/* One wrapper per live node, bounded: a script cannot make the binding allocate by walking a tree
 * it already walked. A refusal is counted rather than silent. */
#define JsDomMaxWrappers 1024
/* A selector's compounds. Bounded for the same reason a wrapper is. */
#define JsDomMaxCompounds 16
#define JsDomNameCapacity 64
#define JsDomValueCapacity 4096
#define JsDomPathCapacity 64
#define JsDomNodeTypeElement 1
#define JsDomNodeTypeText 3
#define JsDomNodeTypeDocument 9
/* `JsObject.h`'s `hostKind` for a DOM wrapper, a style object and a host record. Distinct from the
 * event object's 1 so a mis-cast is a miss rather than a wrong answer. */
#define JsDomWrapperHostKind 7u
#define JsDomStyleHostKind 8u

/* ------------------------------------------------------------------ */
/* The per-wrapper state                                               */
/* ------------------------------------------------------------------ */

typedef struct JsDomWrapperState {
    JsRuntime *runtime;
    JsDomNodeId node;
    JsObject *styleObject;
} JsDomWrapperState;

/* ------------------------------------------------------------------ */
/* Selectors                                                           */
/* ------------------------------------------------------------------ */

typedef struct JsDomCompound {
    char tag[JsDomNameCapacity];
    char id[JsDomNameCapacity];
    char klass[JsDomNameCapacity];
} JsDomCompound;

typedef struct JsDomSelector {
    int count;
    JsDomCompound compounds[JsDomMaxCompounds];
} JsDomSelector;

static int JsDomIsNameStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
}

static int JsDomIsNameChar(char c)
{
    return JsDomIsNameStart(c) || (c >= '0' && c <= '9');
}

static int JsDomCopyBounded(char *destination, size_t capacity, const char *source, int32_t length)
{
    if (length < 0) {
        length = (int32_t)strlen(source);
    }
    if ((size_t)length + 1u > capacity) {
        return 0;
    }
    memcpy(destination, source, (size_t)length);
    destination[length] = '\0';
    return 1;
}

static int JsDomLowerEquals(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;

        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        a += 1;
        b += 1;
    }
    return *a == '\0' && *b == '\0';
}

int JsDomSelectorValidate(const char *selector, char *errBuf, int32_t errCap)
{
    const char *cursor = selector;
    int compounds = 0;

    if (errBuf != NULL && errCap > 0) {
        errBuf[0] = '\0';
    }
    if (selector == NULL) {
        if (errBuf != NULL && errCap > 0) {
            snprintf(errBuf, (size_t)errCap, "selector is empty");
        }
        return 0;
    }
    for (;;) {
        int sawAnything = 0;

        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
            cursor += 1;
        }
        if (*cursor == '\0') {
            return compounds > 0;
        }
        if (*cursor == ',' || *cursor == '>' || *cursor == '+' || *cursor == '~' || *cursor == '[' ||
            *cursor == ':') {
            if (errBuf != NULL && errCap > 0) {
                snprintf(errBuf, (size_t)errCap, "unsupported selector syntax at '%c'", *cursor);
            }
            return 0;
        }
        /* One compound: an optional tag or `*`, #id and .class in any order, each at most once. */
        while (*cursor != '\0' && *cursor != ',' && *cursor != ' ' && *cursor != '\t' &&
               *cursor != '\n' && *cursor != '\r') {
            if (*cursor == '#') {
                cursor += 1;
                if (!JsDomIsNameChar(*cursor)) {
                    if (errBuf != NULL && errCap > 0) {
                        snprintf(errBuf, (size_t)errCap, "'#' must be followed by a name");
                    }
                    return 0;
                }
                while (JsDomIsNameChar(*cursor)) {
                    cursor += 1;
                }
            } else if (*cursor == '.') {
                cursor += 1;
                if (!JsDomIsNameChar(*cursor)) {
                    if (errBuf != NULL && errCap > 0) {
                        snprintf(errBuf, (size_t)errCap, "'.' must be followed by a class name");
                    }
                    return 0;
                }
                while (JsDomIsNameChar(*cursor)) {
                    cursor += 1;
                }
            } else if (*cursor == '*') {
                cursor += 1;
            } else if (JsDomIsNameStart(*cursor)) {
                while (JsDomIsNameChar(*cursor)) {
                    cursor += 1;
                }
            } else {
                if (errBuf != NULL && errCap > 0) {
                    snprintf(errBuf, (size_t)errCap, "unsupported selector syntax at '%c'", *cursor);
                }
                return 0;
            }
            sawAnything = 1;
        }
        if (!sawAnything) {
            if (errBuf != NULL && errCap > 0) {
                snprintf(errBuf, (size_t)errCap, "a selector compound may not be empty");
            }
            return 0;
        }
        compounds += 1;
        if (compounds > JsDomMaxCompounds) {
            if (errBuf != NULL && errCap > 0) {
                snprintf(errBuf, (size_t)errCap, "a selector may name at most %d compounds",
                         JsDomMaxCompounds);
            }
            return 0;
        }
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
            /* A descendant combinator is refused: this build matches a compound against one node,
             * and `div p` would need an ancestor walk this grammar does not state. */
            const char *lookahead = cursor;

            while (*lookahead == ' ' || *lookahead == '\t' || *lookahead == '\n' ||
                   *lookahead == '\r') {
                lookahead += 1;
            }
            if (*lookahead != ',' && *lookahead != '\0') {
                if (errBuf != NULL && errCap > 0) {
                    snprintf(errBuf, (size_t)errCap,
                             "descendant/child combinators are not supported");
                }
                return 0;
            }
            break;
        }
        if (*cursor == ',') {
            cursor += 1;
        }
    }
}

static int JsDomSelectorParse(const char *selector, JsDomSelector *out)
{
    const char *cursor = selector;

    out->count = 0;
    for (;;) {
        JsDomCompound *compound;

        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
            cursor += 1;
        }
        if (*cursor == '\0') {
            return out->count > 0;
        }
        if (out->count >= JsDomMaxCompounds) {
            return 0;
        }
        compound = &out->compounds[out->count];
        compound->tag[0] = '\0';
        compound->id[0] = '\0';
        compound->klass[0] = '\0';

        while (*cursor != '\0' && *cursor != ',' && *cursor != ' ' && *cursor != '\t' &&
               *cursor != '\n' && *cursor != '\r') {
            const char *start;
            int32_t length;
            char *destination = compound->tag;

            if (*cursor == '#') {
                destination = compound->id;
                cursor += 1;
            } else if (*cursor == '.') {
                destination = compound->klass;
                cursor += 1;
            } else if (*cursor == '*') {
                cursor += 1;
                continue;
            }
            start = cursor;
            while (JsDomIsNameChar(*cursor)) {
                cursor += 1;
            }
            length = (int32_t)(cursor - start);
            if (!JsDomCopyBounded(destination, JsDomNameCapacity, start, length)) {
                return 0;
            }
        }
        out->count += 1;
        if (*cursor == ',') {
            cursor += 1;
        }
    }
}

static int JsDomNodeNameOf(JsRuntime *runtime, JsDomNodeId node, char *buffer, int32_t capacity);

static int JsDomClassListContains(const char *classes, const char *name)
{
    const char *cursor = classes;

    while (*cursor != '\0') {
        const char *start;

        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r' ||
               *cursor == '\f') {
            cursor += 1;
        }
        start = cursor;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' && *cursor != '\n' &&
               *cursor != '\r' && *cursor != '\f') {
            cursor += 1;
        }
        if ((int32_t)(cursor - start) == (int32_t)strlen(name) &&
            memcmp(start, name, (size_t)(cursor - start)) == 0) {
            return 1;
        }
    }
    return 0;
}

static int JsDomCompoundMatches(JsRuntime *runtime, JsDomNodeId node, const JsDomCompound *compound)
{
    char buffer[JsDomValueCapacity];
    const JsDomHost *host = JsDomHostOf(runtime);

    if (host == NULL || JsDomNodeKindOf(runtime, node) != (uint32_t)RoboJsDomNodeElement) {
        return 0;
    }
    if (compound->tag[0] != '\0') {
        if (JsDomNodeNameOf(runtime, node, buffer, (int32_t)sizeof(buffer)) < 0) {
            return 0;
        }
        if (!JsDomLowerEquals(buffer, compound->tag)) {
            return 0;
        }
    }
    if (compound->id[0] != '\0') {
        int32_t length = host->attribute(host->userdata, node, "id", buffer, sizeof(buffer));

        if (length < 0 || strcmp(buffer, compound->id) != 0) {
            return 0;
        }
    }
    if (compound->klass[0] != '\0') {
        int32_t length = host->attribute(host->userdata, node, "class", buffer, sizeof(buffer));

        if (length < 0 || !JsDomClassListContains(buffer, compound->klass)) {
            return 0;
        }
    }
    return 1;
}

int JsDomSelectorMatches(JsRuntime *runtime,
                         JsDomNodeId node,
                         const char *selector,
                         char *errBuf,
                         int32_t errCap)
{
    JsDomSelector parsed;
    int32_t i;

    if (runtime == NULL) {
        return 0;
    }
    if (!JsDomSelectorValidate(selector, errBuf, errCap)) {
        return 0;
    }
    if (!JsDomSelectorParse(selector, &parsed)) {
        return 0;
    }
    for (i = 0; i < parsed.count; ++i) {
        if (JsDomCompoundMatches(runtime, node, &parsed.compounds[i])) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The wrapper table                                                   */
/* ------------------------------------------------------------------ */

const JsDomHost *JsDomHostOf(JsRuntime *runtime)
{
    if (runtime == NULL || runtime->domInstalled == 0) {
        return NULL;
    }
    return &runtime->domHost;
}

int JsDomIsInstalled(JsRuntime *runtime)
{
    return runtime != NULL && runtime->domInstalled != 0;
}

int32_t JsDomWrapperCount(JsRuntime *runtime)
{
    return runtime == NULL ? 0 : runtime->wrapperCount;
}

int JsDomNodeIsLive(JsRuntime *runtime, JsDomNodeId node)
{
    const JsDomHost *host = JsDomHostOf(runtime);

    if (host == NULL || node == 0) {
        return 0;
    }
    if (host->isLive != NULL && !host->isLive(host->userdata)) {
        return 0;
    }
    return JsDomNodeKindOf(runtime, node) != (uint32_t)RoboJsDomNodeNone;
}

uint32_t JsDomNodeKindOf(JsRuntime *runtime, JsDomNodeId node)
{
    const JsDomHost *host = JsDomHostOf(runtime);

    if (host == NULL || node == 0 || host->nodeKind == NULL) {
        return (uint32_t)RoboJsDomNodeNone;
    }
    return host->nodeKind(host->userdata, node);
}

/* 1 when a mutating door may run: the DOM is installed and its document is still alive. Every
 * mutation counts a refusal against a dead document rather than touching freed state. */
static int JsDomCanMutate(JsRuntime *runtime)
{
    const JsDomHost *host = JsDomHostOf(runtime);

    if (host == NULL) {
        return 0;
    }
    if (host->isLive != NULL && !host->isLive(host->userdata)) {
        runtime->counters.refusals += 1;
        return 0;
    }
    return 1;
}

static JsObject *JsDomPrototypeForNode(JsRuntime *runtime, JsDomNodeId node)
{
    switch (JsDomNodeKindOf(runtime, node)) {
    case RoboJsDomNodeDocument:
        return runtime->documentPrototype;
    case RoboJsDomNodeText:
        return runtime->nodePrototype;
    default:
        return runtime->elementPrototype;
    }
}

JsObject *JsDomWrapperForNode(JsRuntime *runtime, JsDomNodeId node)
{
    JsDomWrapperState *state;
    JsObject *object;
    int32_t i;

    if (runtime == NULL || runtime->domInstalled == 0 || node == 0) {
        return NULL;
    }
    for (i = 0; i < runtime->wrapperCount; ++i) {
        if (runtime->wrappers[i].node == node) {
            return runtime->wrappers[i].object;
        }
    }
    if (runtime->wrapperCount >= runtime->wrapperCapacity) {
        runtime->counters.refusals += 1;
        return NULL;
    }
    state = (JsDomWrapperState *)JsHeapAlloc(runtime->heap, sizeof(JsDomWrapperState));
    if (state == NULL) {
        return NULL;
    }
    state->runtime = runtime;
    state->node = node;
    state->styleObject = NULL;

    object = JsObjectCreateHost(runtime->heap,
                                JsDomPrototypeForNode(runtime, node),
                                JsDomWrapperHostKind,
                                node,
                                state);
    if (object == NULL) {
        return NULL;
    }
    object->hostOps = &runtime->nodeOps;
    runtime->wrappers[runtime->wrapperCount].node = node;
    runtime->wrappers[runtime->wrapperCount].object = object;
    runtime->wrapperCount += 1;
    return object;
}

/**
 * Retarget an interned wrapper from one node id to another -- the materialisation half of the DOM
 * adapter's pending records.
 *
 * A created-but-unattached element is a synthetic id, because RoboHTML has no orphan node: a node is
 * born linked under the parent it is created with. When a pending record becomes real, the *wrapper*
 * the script is already holding must become the wrapper for the real node rather than being dropped.
 * The script's `el` after `parent.appendChild(el)` is then still the element it built, so `===`
 * against a later `getElementById` answers the same object and a saved reference keeps working --
 * which is what a DOM client does with a node it created.
 *
 * Internal on purpose: the public DOM surface has no concept of a node changing identity, and this is
 * the one place in the module where the interning table is rewritten rather than appended to.
 */
Result JsDomRetargetWrapper(JsRuntime *runtime, JsDomNodeId from, JsDomNodeId to)
{
    int32_t i;

    if (runtime == NULL || from == 0 || to == 0 || from == to) {
        return RoboInvalidArgument;
    }
    for (i = 0; i < runtime->wrapperCount; ++i) {
        if (runtime->wrappers[i].node == from) {
            JsObject *object = runtime->wrappers[i].object;
            JsDomWrapperState *state =
                object != NULL ? (JsDomWrapperState *)object->hostData : NULL;

            runtime->wrappers[i].node = to;
            if (object != NULL) {
                object->hostId = (uint64_t)to;
                object->prototype = JsDomPrototypeForNode(runtime, to);
            }
            if (state != NULL) {
                state->node = to;
                /* The `style` facade is cached per wrapper, and it was built over the old id. */
                state->styleObject = NULL;
            }
            return RoboOk;
        }
    }
    return RoboNotFound;
}

Result JsDomNodeOfValue(const JsValue *value, JsDomNodeId *outNode)
{
    JsObject *object;

    if (value == NULL || outNode == NULL) {
        return RoboInvalidArgument;
    }
    object = JsValueAsObject(value);
    if (object == NULL || object->hostKind != JsDomWrapperHostKind) {
        return RoboInvalidArgument;
    }
    *outNode = (JsDomNodeId)object->hostId;
    return RoboOk;
}

JsObject *JsDomDocumentObject(JsRuntime *runtime)
{
    if (runtime == NULL) {
        return NULL;
    }
    return runtime->documentObject;
}

/* ------------------------------------------------------------------ */
/* Reading the host                                                    */
/* ------------------------------------------------------------------ */

static int JsDomNodeNameOf(JsRuntime *runtime, JsDomNodeId node, char *buffer, int32_t capacity)
{
    const JsDomHost *host = JsDomHostOf(runtime);

    if (capacity <= 0) {
        return -1;
    }
    buffer[0] = '\0';
    if (host == NULL || host->nodeName == NULL) {
        return -1;
    }
    return host->nodeName(host->userdata, node, buffer, capacity);
}

/* The attribute's value, or NULL when the attribute does not exist. */
static const char *JsDomAttributeInto(JsRuntime *runtime,
                                      JsDomNodeId node,
                                      const char *name,
                                      char *buffer,
                                      int32_t capacity)
{
    const JsDomHost *host = JsDomHostOf(runtime);
    int32_t length;

    if (host == NULL || host->attribute == NULL || capacity <= 0) {
        return NULL;
    }
    length = host->attribute(host->userdata, node, name, buffer, capacity);
    if (length < 0) {
        return NULL;
    }
    return buffer;
}

static Result JsDomElementOfThis(JsRuntime *runtime, JsObject *thisObject, JsDomNodeId *out)
{
    JsDomWrapperState *state;

    if (runtime == NULL || thisObject == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    state = (JsDomWrapperState *)thisObject->hostData;
    if (thisObject->hostKind != JsDomWrapperHostKind || state == NULL) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "not a DOM node");
        return RoboError;
    }
    *out = state->node;
    return RoboOk;
}

/* A wrapper value for a node, or `null` for node 0 — the door that keeps "no parent" from being a
 * zero-length object. */
static JsValue JsDomReferenceValue(JsRuntime *runtime, JsDomNodeId node)
{
    if (node == 0) {
        return JsValueNull();
    }
    return JsValueObjectOrUndefined(JsDomWrapperForNode(runtime, node));
}

/* ------------------------------------------------------------------ */
/* Computed properties                                                 */
/* ------------------------------------------------------------------ */

static Result JsDomSetTextContentNow(JsRuntime *runtime, JsDomNodeId node, const char *text)
{
    const JsDomHost *host = JsDomHostOf(runtime);
    Result status;

    if (host == NULL || host->setTextContent == NULL) {
        return RoboNotSupported;
    }
    if (!JsDomCanMutate(runtime)) {
        return RoboInvalidArgument;
    }
    status = host->setTextContent(host->userdata, node, text, (int32_t)strlen(text));
    if (status == RoboOk) {
        runtime->counters.textWrites += 1;
        if (host->invalidate != NULL) {
            host->invalidate(host->userdata, node);
        }
        return RoboOk;
    }
    /* The refusal is named here rather than surfacing as the object model's anonymous "a property
     * write failed": a `textContent` write on an element is "replace the children with one text
     * node", and a document whose construction is closed refuses exactly that. A caller reading the
     * message should learn which of the two halves of the write was refused. */
    if (!JsRuntimeHasException(runtime)) {
        if (status == RoboNotSupported) {
            JsRuntimeThrow(runtime, RoboJsErrorType, NULL,
                           "textContent could not replace the children: this document accepts no new nodes");
        } else {
            JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "textContent could not be written (%s)",
                           ErrorString(status));
        }
    }
    return status;
}

static JsValue JsDomTextValue(JsRuntime *runtime, JsDomNodeId node)
{
    const JsDomHost *host = JsDomHostOf(runtime);
    char stack[JsDomValueCapacity];
    int32_t needed;

    if (host == NULL || host->textContent == NULL) {
        return JsValueString(runtime->heap, "", 0);
    }
    needed = host->textContent(host->userdata, node, stack, (int32_t)sizeof(stack));
    if (needed < 0) {
        return JsValueString(runtime->heap, "", 0);
    }
    if (needed >= (int32_t)sizeof(stack)) {
        char *grown = (char *)JsHeapAlloc(runtime->heap, (size_t)needed + 1u);

        if (grown == NULL) {
            return JsValueString(runtime->heap, "", 0);
        }
        host->textContent(host->userdata, node, grown, needed + 1);
        return JsValueString(runtime->heap, grown, needed);
    }
    return JsValueString(runtime->heap, stack, needed);
}

static JsValue JsDomChildrenArray(JsRuntime *runtime, JsDomNodeId node, int elementsOnly)
{
    const JsDomHost *host = JsDomHostOf(runtime);
    JsObject *array;
    int32_t count;
    int32_t i;

    array = JsArrayCreate(runtime->heap, runtime->arrayPrototype, 0);
    if (array == NULL || host == NULL || host->childCount == NULL || host->childAt == NULL) {
        return JsValueObjectOrUndefined(array);
    }
    count = host->childCount(host->userdata, node);
    for (i = 0; i < count; ++i) {
        JsDomNodeId child = host->childAt(host->userdata, node, i);
        JsValue value;

        if (child == 0) {
            continue;
        }
        if (elementsOnly && JsDomNodeKindOf(runtime, child) != (uint32_t)RoboJsDomNodeElement) {
            continue;
        }
        value = JsValueObjectOrUndefined(JsDomWrapperForNode(runtime, child));
        JsArrayPush(runtime->heap, array, &value);
    }
    return JsValueObject(array);
}

static JsValue JsDomAttributesArray(JsRuntime *runtime, JsDomNodeId node)
{
    const JsDomHost *host = JsDomHostOf(runtime);
    JsObject *array;
    int32_t count;
    int32_t i;

    array = JsArrayCreate(runtime->heap, runtime->arrayPrototype, 0);
    if (array == NULL || host == NULL || host->attributeCount == NULL ||
        host->attributeNameAt == NULL) {
        return JsValueObjectOrUndefined(array);
    }
    count = host->attributeCount(host->userdata, node);
    for (i = 0; i < count; ++i) {
        char name[JsDomNameCapacity];
        char value[JsDomValueCapacity];
        JsObject *record;
        JsValue entry;

        if (host->attributeNameAt(host->userdata, node, i, name, (int32_t)sizeof(name)) < 0) {
            continue;
        }
        record = JsObjectCreate(runtime->heap, runtime->objectPrototype);
        if (record == NULL) {
            continue;
        }
        if (JsDomAttributeInto(runtime, node, name, value, (int32_t)sizeof(value)) == NULL) {
            value[0] = '\0';
        }
        JsObjectSetString(runtime->heap, record, "name", name);
        JsObjectSetString(runtime->heap, record, "value", value);
        entry = JsValueObject(record);
        JsArrayPush(runtime->heap, array, &entry);
    }
    return JsValueObject(array);
}

static JsObject *JsDomStyleObject(JsRuntime *runtime, JsDomNodeId node);

/**
 * The first element at or below @p node, in document order -- `documentElement`'s rule.
 *
 * A host's `documentRoot` is whatever the host calls the tree's root: for RoboHTML that is the
 * document node, for the fixture it is the `<html>` element. Asking for the first *element* rather
 * than the root itself is what makes `document.documentElement` mean the same thing on both, without
 * either host having to know which of the two it handed over. The walk is bounded by the sibling chain
 * and by the host's own `nextSibling`, so a host that reports a cycle stops here rather than spinning.
 */
static JsDomNodeId JsDomFirstElementBelow(JsRuntime *runtime, JsDomNodeId node)
{
    const JsDomHost *host = JsDomHostOf(runtime);
    JsDomNodeId cursor = node;
    int32_t guard = 0;

    while (cursor != 0 && guard < 4096) {
        if (JsDomNodeKindOf(runtime, cursor) == (uint32_t)RoboJsDomNodeElement) {
            return cursor;
        }
        if (host == NULL || host->firstChild == NULL) {
            return 0;
        }
        /* Depth-first: descend while there is a child, otherwise step sideways. A `documentRoot` that
         * is the document node has exactly one relevant branch, so this reaches the `<html>` element
         * in two steps. */
        {
            JsDomNodeId child = host->firstChild(host->userdata, cursor);

            if (child != 0) {
                cursor = child;
                guard += 1;
                continue;
            }
        }
        if (host->nextSibling == NULL) {
            return 0;
        }
        cursor = host->nextSibling(host->userdata, cursor);
        guard += 1;
    }
    return 0;
}

static Result JsDomNodeGetProperty(JsRuntime *runtime,
                                   JsDomNodeId node,
                                   const char *key,
                                   JsValue *out)
{
    const JsDomHost *host = JsDomHostOf(runtime);

    if (host == NULL) {
        return RoboNotFound;
    }
    if (strcmp(key, "id") == 0 || strcmp(key, "className") == 0 || strcmp(key, "value") == 0) {
        char buffer[JsDomValueCapacity];
        const char *name = strcmp(key, "className") == 0 ? "class" : key;
        const char *text = JsDomAttributeInto(runtime, node, name, buffer, (int32_t)sizeof(buffer));

        if (text == NULL) {
            /* An element's `value` falls back to its text, which is what a control's value means
             * for a build with no form state. Everything else is the empty string. */
            if (strcmp(key, "value") == 0 && JsDomNodeKindOf(runtime, node) ==
                                                 (uint32_t)RoboJsDomNodeText) {
                *out = JsDomTextValue(runtime, node);
                return RoboOk;
            }
            *out = JsValueString(runtime->heap, "", 0);
            return RoboOk;
        }
        *out = JsValueString(runtime->heap, text, -1);
        return RoboOk;
    }
    if (strcmp(key, "textContent") == 0 || strcmp(key, "innerText") == 0 ||
        strcmp(key, "nodeValue") == 0 || strcmp(key, "data") == 0) {
        *out = JsDomTextValue(runtime, node);
        return RoboOk;
    }
    if (strcmp(key, "nodeName") == 0 || strcmp(key, "tagName") == 0) {
        char buffer[JsDomValueCapacity];
        int32_t length = JsDomNodeNameOf(runtime, node, buffer, (int32_t)sizeof(buffer));
        int32_t i;

        if (length < 0) {
            buffer[0] = '\0';
            length = 0;
        }
        if (strcmp(key, "tagName") == 0 &&
            JsDomNodeKindOf(runtime, node) == (uint32_t)RoboJsDomNodeElement) {
            for (i = 0; i < length; ++i) {
                if (buffer[i] >= 'a' && buffer[i] <= 'z') {
                    buffer[i] = (char)(buffer[i] - 'a' + 'A');
                }
            }
        }
        *out = JsValueString(runtime->heap, buffer, length);
        return RoboOk;
    }
    if (strcmp(key, "nodeType") == 0) {
        switch (JsDomNodeKindOf(runtime, node)) {
        case RoboJsDomNodeDocument:
            *out = JsValueNumber(JsDomNodeTypeDocument);
            break;
        case RoboJsDomNodeText:
            *out = JsValueNumber(JsDomNodeTypeText);
            break;
        default:
            *out = JsValueNumber(JsDomNodeTypeElement);
            break;
        }
        return RoboOk;
    }
    if (strcmp(key, "parentNode") == 0 || strcmp(key, "firstChild") == 0 ||
        strcmp(key, "lastChild") == 0 || strcmp(key, "nextSibling") == 0 ||
        strcmp(key, "previousSibling") == 0) {
        JsDomNodeId answer = 0;

        if (host->parent != NULL && strcmp(key, "parentNode") == 0) {
            answer = host->parent(host->userdata, node);
        } else if (host->firstChild != NULL && strcmp(key, "firstChild") == 0) {
            answer = host->firstChild(host->userdata, node);
        } else if (host->lastChild != NULL && strcmp(key, "lastChild") == 0) {
            answer = host->lastChild(host->userdata, node);
        } else if (host->nextSibling != NULL && strcmp(key, "nextSibling") == 0) {
            answer = host->nextSibling(host->userdata, node);
        } else if (host->previousSibling != NULL && strcmp(key, "previousSibling") == 0) {
            answer = host->previousSibling(host->userdata, node);
        }
        *out = JsDomReferenceValue(runtime, answer);
        return RoboOk;
    }
    if (strcmp(key, "childNodes") == 0) {
        *out = JsDomChildrenArray(runtime, node, 0);
        return RoboOk;
    }
    if (strcmp(key, "children") == 0) {
        *out = JsDomChildrenArray(runtime, node, 1);
        return RoboOk;
    }
    if (strcmp(key, "childElementCount") == 0) {
        JsValue children = JsDomChildrenArray(runtime, node, 1);
        JsObject *array = JsValueAsObject(&children);

        *out = JsValueNumber(array == NULL ? 0 : (double)array->length);
        return RoboOk;
    }
    if (strcmp(key, "firstElementChild") == 0 || strcmp(key, "lastElementChild") == 0) {
        JsValue children = JsDomChildrenArray(runtime, node, 1);
        JsObject *array = JsValueAsObject(&children);

        if (array == NULL || array->length == 0) {
            *out = JsValueNull();
        } else {
            int32_t index = strcmp(key, "firstElementChild") == 0 ? 0 : array->length - 1;

            JsArrayGet(array, index, out);
        }
        return RoboOk;
    }
    if (strcmp(key, "attributes") == 0) {
        *out = JsDomAttributesArray(runtime, node);
        return RoboOk;
    }
    if (strcmp(key, "style") == 0) {
        *out = JsValueObjectOrUndefined(JsDomStyleObject(runtime, node));
        return RoboOk;
    }
    if (strcmp(key, "ownerDocument") == 0) {
        if (JsDomNodeKindOf(runtime, node) == (uint32_t)RoboJsDomNodeDocument) {
            *out = JsDomReferenceValue(runtime, node);
        } else {
            *out = JsValueObjectOrUndefined(runtime->documentObject);
        }
        return RoboOk;
    }
    if (strcmp(key, "body") == 0 || strcmp(key, "documentElement") == 0) {
        JsDomNodeId answer = 0;

        if (JsDomNodeKindOf(runtime, node) == (uint32_t)RoboJsDomNodeDocument) {
            if (strcmp(key, "body") == 0 && host->documentBody != NULL) {
                answer = host->documentBody(host->userdata);
            } else if (strcmp(key, "documentElement") == 0 && host->documentRoot != NULL) {
                /* `documentRoot` is the *tree's* root, which for a parsed document is the document
                 * node itself. `documentElement` is the DOM's name for the root's first element child,
                 * so the two are not the same node and a host is not asked to know which is which: the
                 * descent is the same walk `children` does, performed until an element turns up. */
                answer = JsDomFirstElementBelow(runtime, host->documentRoot(host->userdata));
            }
        }
        *out = JsDomReferenceValue(runtime, answer);
        return RoboOk;
    }
    if (strcmp(key, "title") == 0) {
        /* `document.title` is the `<title>` element's text, read and written through the element's
         * own text rather than stored anywhere. The write goes through `JsDomSetTextContentNow`, so a
         * document whose construction is closed *still* answers it when the title already has a text
         * node -- the same in-place rewrite `el.textContent` takes -- and refuses only the shape that
         * would need a node created. */
        JsDomNodeId titleNode = 0;

        if (JsDomNodeKindOf(runtime, node) != (uint32_t)RoboJsDomNodeDocument) {
            return RoboNotFound;
        }
        if (host->findByTag != NULL && host->documentRoot != NULL) {
            JsDomNodeId root = host->documentRoot(host->userdata);

            titleNode = host->findByTag(host->userdata, root, "title");
            if (titleNode == 0 && host->documentBody != NULL) {
                /* A `<title>` outside `<body>`'s subtree is the normal case (it lives in `<head>`),
                 * so the root-relative search is the one that finds it. The body-relative search is
                 * the fallback for a host whose root is a document fragment. */
                titleNode = host->findByTag(host->userdata, 0, "title");
            }
        }
        *out = titleNode != 0 ? JsDomTextValue(runtime, titleNode) : JsValueString(runtime->heap, "", 0);
        return RoboOk;
    }
    return RoboNotFound;
}

static Result JsDomNodeSetProperty(JsRuntime *runtime,
                                   JsDomNodeId node,
                                   const char *key,
                                   const JsValue *value)
{
    char text[JsDomValueCapacity];
    const JsDomHost *host = JsDomHostOf(runtime);

    JsValueToString(value, text, sizeof(text));
    if (strcmp(key, "title") == 0 && JsDomNodeKindOf(runtime, node) ==
                                           (uint32_t)RoboJsDomNodeDocument) {
        /* The write half of `document.title`: the `<title>` element's own text, through the same door
         * `el.textContent` uses. */
        const JsDomHost *titleHost = JsDomHostOf(runtime);
        JsDomNodeId titleNode = 0;

        if (titleHost == NULL || titleHost->findByTag == NULL || !JsDomCanMutate(runtime)) {
            return RoboInvalidArgument;
        }
        if (titleHost->documentRoot != NULL) {
            titleNode = titleHost->findByTag(titleHost->userdata,
                                             titleHost->documentRoot(titleHost->userdata),
                                             "title");
        }
        if (titleNode == 0) {
            titleNode = titleHost->findByTag(titleHost->userdata, 0, "title");
        }
        if (titleNode == 0) {
            JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "this document has no title element");
            return RoboNotFound;
        }
        return JsDomSetTextContentNow(runtime, titleNode, text);
    }
    if (strcmp(key, "id") == 0 || strcmp(key, "className") == 0 || strcmp(key, "value") == 0) {
        const char *name = strcmp(key, "className") == 0 ? "class" : key;
        Result status;

        if (host == NULL || host->setAttribute == NULL || !JsDomCanMutate(runtime)) {
            return RoboInvalidArgument;
        }
        status = host->setAttribute(host->userdata, node, name, text);
        if (status == RoboOk) {
            runtime->counters.attributeWrites += 1;
            if (host->invalidate != NULL) {
                host->invalidate(host->userdata, node);
            }
        }
        return status;
    }
    if (strcmp(key, "textContent") == 0 || strcmp(key, "innerText") == 0 ||
        strcmp(key, "nodeValue") == 0 || strcmp(key, "data") == 0) {
        return JsDomSetTextContentNow(runtime, node, text);
    }
    if (strcmp(key, "style") == 0) {
        Result status;

        if (host == NULL || host->setAttribute == NULL || !JsDomCanMutate(runtime)) {
            return RoboInvalidArgument;
        }
        status = host->setAttribute(host->userdata, node, "style", text);
        if (status == RoboOk) {
            runtime->counters.attributeWrites += 1;
            if (host->invalidate != NULL) {
                host->invalidate(host->userdata, node);
            }
        }
        return status;
    }
    /* Anything else becomes an ordinary own property, which is what a script setting `el.custom`
     * expects. Returning `RoboNotFound` here is what tells the object model to do that. */
    return RoboNotFound;
}

static Result JsDomComputedGet(JsObject *object, const char *key, JsValue *out)
{
    JsDomWrapperState *state = (JsDomWrapperState *)object->hostData;

    if (state == NULL || state->runtime == NULL) {
        return RoboNotFound;
    }
    return JsDomNodeGetProperty(state->runtime, state->node, key, out);
}

static Result JsDomComputedSet(JsObject *object, const char *key, const JsValue *value)
{
    JsDomWrapperState *state = (JsDomWrapperState *)object->hostData;

    if (state == NULL || state->runtime == NULL) {
        return RoboNotFound;
    }
    return JsDomNodeSetProperty(state->runtime, state->node, key, value);
}

/* ------------------------------------------------------------------ */
/* `style`: a write door onto one attribute                            */
/* ------------------------------------------------------------------ */

static JsObject *JsDomStyleObject(JsRuntime *runtime, JsDomNodeId node)
{
    JsDomWrapperState *wrapperState = NULL;
    JsObject *style;
    JsDomWrapperState *state;
    int32_t i;

    if (runtime == NULL || !JsDomIsInstalled(runtime)) {
        return NULL;
    }
    for (i = 0; i < runtime->wrapperCount; ++i) {
        if (runtime->wrappers[i].node == node) {
            wrapperState = (JsDomWrapperState *)runtime->wrappers[i].object->hostData;
            break;
        }
    }
    if (wrapperState == NULL) {
        if (JsDomWrapperForNode(runtime, node) == NULL) {
            return NULL;
        }
        for (i = 0; i < runtime->wrapperCount; ++i) {
            if (runtime->wrappers[i].node == node) {
                wrapperState = (JsDomWrapperState *)runtime->wrappers[i].object->hostData;
                break;
            }
        }
    }
    if (wrapperState == NULL) {
        return NULL;
    }
    if (wrapperState->styleObject != NULL) {
        return wrapperState->styleObject;
    }
    state = (JsDomWrapperState *)JsHeapAlloc(runtime->heap, sizeof(JsDomWrapperState));
    if (state == NULL) {
        return NULL;
    }
    state->runtime = runtime;
    state->node = node;
    state->styleObject = NULL;

    style = JsObjectCreateHost(runtime->heap,
                               runtime->stylePrototype,
                               JsDomStyleHostKind,
                               node,
                               state);
    if (style == NULL) {
        return NULL;
    }
    style->hostOps = &runtime->styleOps;
    wrapperState->styleObject = style;
    return style;
}

static JsDomNodeId JsDomStyleNodeOf(JsObject *object)
{
    if (object->hostKind != JsDomStyleHostKind) {
        return 0;
    }
    return (JsDomNodeId)object->hostId;
}

/* Read the `style` attribute and find `name:`'s value. Returns 0 when absent. */
static int JsDomStyleFind(const char *text, const char *name, char *out, int32_t outCapacity)
{
    const char *cursor = text;
    size_t nameLength = strlen(name);

    while (*cursor != '\0') {
        const char *declarationStart;
        const char *colon;
        const char *valueStart;
        const char *valueEnd;

        while (*cursor == ';' || *cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
               *cursor == '\r') {
            cursor += 1;
        }
        declarationStart = cursor;
        colon = NULL;
        while (*cursor != '\0' && *cursor != ';') {
            if (*cursor == ':' && colon == NULL) {
                colon = cursor;
            }
            cursor += 1;
        }
        if (colon == NULL) {
            continue;
        }
        while (declarationStart < colon &&
               (declarationStart[0] == ' ' || declarationStart[0] == '\t')) {
            declarationStart += 1;
        }
        if ((size_t)(colon - declarationStart) != nameLength ||
            memcmp(declarationStart, name, nameLength) != 0) {
            continue;
        }
        valueStart = colon + 1;
        while (*valueStart == ' ' || *valueStart == '\t') {
            valueStart += 1;
        }
        valueEnd = valueStart;
        while (valueEnd < cursor && *valueEnd != '\0') {
            valueEnd += 1;
        }
        while (valueEnd > valueStart && (valueEnd[-1] == ' ' || valueEnd[-1] == '\t')) {
            valueEnd -= 1;
        }
        JsDomCopyBounded(out, (size_t)outCapacity, valueStart, (int32_t)(valueEnd - valueStart));
        return 1;
    }
    out[0] = '\0';
    return 0;
}

/* Rebuild the `style` attribute from `text`, leaving every declaration that is not `name` alone. */
static Result JsDomStyleRewrite(JsRuntime *runtime,
                                JsDomNodeId node,
                                const char *text,
                                const char *name,
                                const char *value)
{
    const JsDomHost *host = JsDomHostOf(runtime);
    char rebuilt[JsDomValueCapacity];
    const char *cursor = text;
    size_t nameLength = strlen(name);
    size_t used = 0;

    rebuilt[0] = '\0';
    while (*cursor != '\0') {
        const char *declarationStart = cursor;
        const char *colon = NULL;
        const char *end;

        while (*cursor != '\0' && *cursor != ';') {
            if (*cursor == ':' && colon == NULL) {
                colon = cursor;
            }
            cursor += 1;
        }
        end = cursor;
        if (*cursor == ';') {
            cursor += 1;
        }
        if (colon != NULL) {
            const char *trimmed = declarationStart;

            while (trimmed < colon && (trimmed[0] == ' ' || trimmed[0] == '\t')) {
                trimmed += 1;
            }
            if ((size_t)(colon - trimmed) == nameLength &&
                memcmp(trimmed, name, nameLength) == 0) {
                continue; /* the declaration being replaced or removed */
            }
        }
        if (end > declarationStart) {
            size_t length = (size_t)(end - declarationStart);
            const char *start = declarationStart;

            while (length > 0 && (*start == ' ' || *start == '\t')) {
                start += 1;
                length -= 1;
            }
            while (length > 0 && (start[length - 1] == ' ' || start[length - 1] == '\t')) {
                length -= 1;
            }
            if (length > 0 && used + length + 2u < sizeof(rebuilt)) {
                if (used > 0) {
                    rebuilt[used++] = ';';
                }
                memcpy(rebuilt + used, start, length);
                used += length;
                rebuilt[used] = '\0';
            }
        }
    }
    if (value != NULL && value[0] != '\0') {
        size_t valueLength = strlen(value);

        if (used + nameLength + valueLength + 3u < sizeof(rebuilt)) {
            if (used > 0) {
                rebuilt[used++] = ';';
            }
            memcpy(rebuilt + used, name, nameLength);
            used += nameLength;
            rebuilt[used++] = ':';
            memcpy(rebuilt + used, value, valueLength);
            used += valueLength;
            rebuilt[used] = '\0';
        }
    }
    if (host == NULL || host->setAttribute == NULL || !JsDomCanMutate(runtime)) {
        return RoboInvalidArgument;
    }
    if (host->setAttribute(host->userdata, node, "style", rebuilt) != RoboOk) {
        return RoboError;
    }
    runtime->counters.attributeWrites += 1;
    if (host->invalidate != NULL) {
        host->invalidate(host->userdata, node);
    }
    return RoboOk;
}

static Result JsDomStyleGetProperty(JsObject *object, const char *key, JsValue *out)
{
    JsDomNodeId node = JsDomStyleNodeOf(object);
    JsRuntime *runtime = NULL;
    JsDomWrapperState *state = (JsDomWrapperState *)object->hostData;
    char text[JsDomValueCapacity];
    char value[JsDomValueCapacity];

    if (state != NULL) {
        runtime = state->runtime;
    }
    if (runtime == NULL || node == 0) {
        return RoboNotFound;
    }
    if (strcmp(key, "cssText") == 0) {
        if (JsDomAttributeInto(runtime, node, "style", text, (int32_t)sizeof(text)) == NULL) {
            text[0] = '\0';
        }
        *out = JsValueString(runtime->heap, text, -1);
        return RoboOk;
    }
    if (JsDomAttributeInto(runtime, node, "style", text, (int32_t)sizeof(text)) == NULL) {
        text[0] = '\0';
    }
    if (!JsDomStyleFind(text, key, value, (int32_t)sizeof(value))) {
        *out = JsValueString(runtime->heap, "", 0);
        return RoboOk;
    }
    *out = JsValueString(runtime->heap, value, -1);
    return RoboOk;
}

static Result JsDomStyleSetProperty(JsObject *object, const char *key, const JsValue *value)
{
    JsDomNodeId node = JsDomStyleNodeOf(object);
    JsRuntime *runtime = NULL;
    JsDomWrapperState *state = (JsDomWrapperState *)object->hostData;
    char text[JsDomValueCapacity];
    char newValue[JsDomValueCapacity];

    if (state != NULL) {
        runtime = state->runtime;
    }
    if (runtime == NULL || node == 0) {
        return RoboNotFound;
    }
    if (JsDomAttributeInto(runtime, node, "style", text, (int32_t)sizeof(text)) == NULL) {
        text[0] = '\0';
    }
    if (strcmp(key, "cssText") == 0) {
        const JsDomHost *host = JsDomHostOf(runtime);

        JsValueToString(value, newValue, sizeof(newValue));
        if (host == NULL || host->setAttribute == NULL || !JsDomCanMutate(runtime)) {
            return RoboInvalidArgument;
        }
        if (host->setAttribute(host->userdata, node, "style", newValue) != RoboOk) {
            return RoboError;
        }
        runtime->counters.attributeWrites += 1;
        if (host->invalidate != NULL) {
            host->invalidate(host->userdata, node);
        }
        return RoboOk;
    }
    JsValueToString(value, newValue, sizeof(newValue));
    return JsDomStyleRewrite(runtime, node, text, key, newValue);
}

/* ------------------------------------------------------------------ */
/* Native bindings                                                     */
/* ------------------------------------------------------------------ */

static Result JsDomNativeGetAttribute(JsRuntime *runtime,
                                      JsObject *thisObject,
                                      const JsValue *args,
                                      int32_t argCount,
                                      JsValue *out,
                                      void *userdata)
{
    JsDomNodeId node;
    char name[JsDomNameCapacity];
    char value[JsDomValueCapacity];

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &node) != RoboOk) {
        return RoboError;
    }
    if (argCount < 1 || !JsValueIsString(&args[0])) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "getAttribute needs a name");
        return RoboError;
    }
    JsValueToString(&args[0], name, sizeof(name));
    if (JsDomAttributeInto(runtime, node, name, value, (int32_t)sizeof(value)) == NULL) {
        *out = JsValueNull();
        return RoboOk;
    }
    *out = JsValueString(runtime->heap, value, -1);
    return RoboOk;
}

static Result JsDomNativeSetAttribute(JsRuntime *runtime,
                                      JsObject *thisObject,
                                      const JsValue *args,
                                      int32_t argCount,
                                      JsValue *out,
                                      void *userdata)
{
    JsDomNodeId node;
    char name[JsDomNameCapacity];
    char value[JsDomValueCapacity];
    const JsDomHost *host;

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &node) != RoboOk) {
        return RoboError;
    }
    if (argCount < 1) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "setAttribute needs a name");
        return RoboError;
    }
    JsValueToString(&args[0], name, sizeof(name));
    value[0] = '\0';
    if (argCount >= 2) {
        JsValueToString(&args[1], value, sizeof(value));
    }
    host = JsDomHostOf(runtime);
    if (host == NULL || host->setAttribute == NULL || !JsDomCanMutate(runtime)) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "the document is not available");
        return RoboError;
    }
    if (host->setAttribute(host->userdata, node, name, value) != RoboOk) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "setAttribute('%s') failed", name);
        return RoboError;
    }
    runtime->counters.attributeWrites += 1;
    if (host->invalidate != NULL) {
        host->invalidate(host->userdata, node);
    }
    *out = JsValueUndefined();
    return RoboOk;
}

static Result JsDomNativeRemoveAttribute(JsRuntime *runtime,
                                         JsObject *thisObject,
                                         const JsValue *args,
                                         int32_t argCount,
                                         JsValue *out,
                                         void *userdata)
{
    JsDomNodeId node;
    char name[JsDomNameCapacity];
    const JsDomHost *host;

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &node) != RoboOk) {
        return RoboError;
    }
    if (argCount < 1) {
        *out = JsValueUndefined();
        return RoboOk;
    }
    JsValueToString(&args[0], name, sizeof(name));
    host = JsDomHostOf(runtime);
    if (host == NULL || host->removeAttribute == NULL || !JsDomCanMutate(runtime)) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "the document is not available");
        return RoboError;
    }
    /* `removeAttribute` on an attribute that does not exist is not an error, which is why the
     * host's own answer is passed through rather than turned into a throw. */
    host->removeAttribute(host->userdata, node, name);
    runtime->counters.attributeWrites += 1;
    if (host->invalidate != NULL) {
        host->invalidate(host->userdata, node);
    }
    *out = JsValueUndefined();
    return RoboOk;
}

static Result JsDomNativeHasAttribute(JsRuntime *runtime,
                                      JsObject *thisObject,
                                      const JsValue *args,
                                      int32_t argCount,
                                      JsValue *out,
                                      void *userdata)
{
    JsDomNodeId node;
    char name[JsDomNameCapacity];
    char value[JsDomValueCapacity];

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &node) != RoboOk) {
        return RoboError;
    }
    if (argCount < 1) {
        *out = JsValueBool(0);
        return RoboOk;
    }
    JsValueToString(&args[0], name, sizeof(name));
    *out = JsValueBool(JsDomAttributeInto(runtime, node, name, value, (int32_t)sizeof(value)) !=
                       NULL);
    return RoboOk;
}

static Result JsDomNativeAppendChild(JsRuntime *runtime,
                                     JsObject *thisObject,
                                     const JsValue *args,
                                     int32_t argCount,
                                     JsValue *out,
                                     void *userdata)
{
    JsDomNodeId parent;
    JsDomNodeId child;
    const JsDomHost *host;

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &parent) != RoboOk) {
        return RoboError;
    }
    if (argCount < 1 || JsDomNodeOfValue(&args[0], &child) != RoboOk || child == 0) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "appendChild needs a node");
        return RoboError;
    }
    host = JsDomHostOf(runtime);
    if (host == NULL || host->appendChild == NULL || !JsDomCanMutate(runtime)) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "the document is not available");
        return RoboError;
    }
    {
        Result status = host->appendChild(host->userdata, parent, child);

        if (status != RoboOk) {
            /* A host that knows *why* it refused gets to say so -- a new node cannot be linked into a
             * document that accepts no new nodes, and `appendChild refused the node` would not tell a
             * caller the difference between that and a malformed operand. */
            if (status == RoboNotSupported) {
                JsRuntimeThrow(runtime, RoboJsErrorType, NULL,
                               "appendChild could not add a new node: this document accepts no new nodes");
            } else {
                JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "appendChild refused the node");
            }
            return RoboError;
        }
    }
    runtime->counters.appends += 1;
    if (host->invalidate != NULL) {
        host->invalidate(host->userdata, parent);
    }
    *out = args[0];
    return RoboOk;
}

static Result JsDomNativeInsertBefore(JsRuntime *runtime,
                                      JsObject *thisObject,
                                      const JsValue *args,
                                      int32_t argCount,
                                      JsValue *out,
                                      void *userdata)
{
    JsDomNodeId parent;
    JsDomNodeId child;
    JsDomNodeId before = 0;
    const JsDomHost *host;

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &parent) != RoboOk) {
        return RoboError;
    }
    if (argCount < 1 || JsDomNodeOfValue(&args[0], &child) != RoboOk || child == 0) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "insertBefore needs a node");
        return RoboError;
    }
    if (argCount >= 2 && !JsValueIsNullOrUndefined(&args[1])) {
        if (JsDomNodeOfValue(&args[1], &before) != RoboOk) {
            JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "insertBefore needs a node to precede");
            return RoboError;
        }
    }
    host = JsDomHostOf(runtime);
    if (host == NULL || host->insertBefore == NULL || !JsDomCanMutate(runtime)) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "the document is not available");
        return RoboError;
    }
    {
        Result status = host->insertBefore(host->userdata, parent, child, before);

        if (status != RoboOk) {
            if (status == RoboNotSupported) {
                JsRuntimeThrow(runtime, RoboJsErrorType, NULL,
                               "insertBefore could not add a new node: this document accepts no new nodes");
            } else {
                JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "insertBefore refused the node");
            }
            return RoboError;
        }
    }
    runtime->counters.appends += 1;
    if (host->invalidate != NULL) {
        host->invalidate(host->userdata, parent);
    }
    *out = args[0];
    return RoboOk;
}

static Result JsDomNativeRemoveChild(JsRuntime *runtime,
                                     JsObject *thisObject,
                                     const JsValue *args,
                                     int32_t argCount,
                                     JsValue *out,
                                     void *userdata)
{
    JsDomNodeId parent;
    JsDomNodeId child;
    const JsDomHost *host;

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &parent) != RoboOk) {
        return RoboError;
    }
    if (argCount < 1 || JsDomNodeOfValue(&args[0], &child) != RoboOk || child == 0) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "removeChild needs a node");
        return RoboError;
    }
    host = JsDomHostOf(runtime);
    if (host == NULL || host->removeChild == NULL || !JsDomCanMutate(runtime)) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "the document is not available");
        return RoboError;
    }
    if (host->removeChild(host->userdata, parent, child) != RoboOk) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "removeChild refused the node");
        return RoboError;
    }
    /* A removed node's listeners go with it: a listener on a node no event can reach is a leak
     * with a friendly name. */
    JsEventDropListeners(runtime, child);
    if (host->invalidate != NULL) {
        host->invalidate(host->userdata, parent);
    }
    *out = args[0];
    return RoboOk;
}

static Result JsDomNativeRemove(JsRuntime *runtime,
                                JsObject *thisObject,
                                const JsValue *args,
                                int32_t argCount,
                                JsValue *out,
                                void *userdata)
{
    JsDomNodeId node;
    JsDomNodeId parent;
    const JsDomHost *host;

    (void)args;
    (void)argCount;
    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &node) != RoboOk) {
        return RoboError;
    }
    host = JsDomHostOf(runtime);
    if (host == NULL || host->parent == NULL || host->removeChild == NULL || !JsDomCanMutate(runtime)) {
        *out = JsValueUndefined();
        return RoboOk;
    }
    parent = host->parent(host->userdata, node);
    if (parent != 0) {
        host->removeChild(host->userdata, parent, node);
        JsEventDropListeners(runtime, node);
        if (host->invalidate != NULL) {
            host->invalidate(host->userdata, parent);
        }
    }
    *out = JsValueUndefined();
    return RoboOk;
}

static Result JsDomNativeContains(JsRuntime *runtime,
                                 JsObject *thisObject,
                                 const JsValue *args,
                                 int32_t argCount,
                                 JsValue *out,
                                 void *userdata)
{
    JsDomNodeId node;
    JsDomNodeId other;
    const JsDomHost *host;

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &node) != RoboOk) {
        return RoboError;
    }
    if (argCount < 1 || JsDomNodeOfValue(&args[0], &other) != RoboOk) {
        *out = JsValueBool(0);
        return RoboOk;
    }
    host = JsDomHostOf(runtime);
    if (host == NULL || host->contains == NULL) {
        *out = JsValueBool(node == other);
        return RoboOk;
    }
    *out = JsValueBool(host->contains(host->userdata, node, other));
    return RoboOk;
}

static Result JsDomNativeMatches(JsRuntime *runtime,
                                 JsObject *thisObject,
                                 const JsValue *args,
                                 int32_t argCount,
                                 JsValue *out,
                                 void *userdata)
{
    JsDomNodeId node;
    char selector[JsDomValueCapacity];
    char errBuf[128];

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &node) != RoboOk) {
        return RoboError;
    }
    if (argCount < 1) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "matches needs a selector");
        return RoboError;
    }
    JsValueToString(&args[0], selector, sizeof(selector));
    if (!JsDomSelectorValidate(selector, errBuf, (int32_t)sizeof(errBuf))) {
        JsRuntimeThrow(runtime, RoboJsErrorSyntax, NULL, "'%s': %s", selector, errBuf);
        return RoboError;
    }
    *out = JsValueBool(JsDomSelectorMatches(runtime, node, selector, NULL, 0));
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* querySelector / querySelectorAll / getElementsByTagName             */
/* ------------------------------------------------------------------ */

typedef struct JsDomSearchContext {
    JsRuntime *runtime;
    const char *selector;
    const char *tagName; /* lowercase; NULL when matching by selector */
    JsObject *collection;
    JsDomNodeId found;
    int32_t matches;
    int32_t limit;
    int32_t visited;
    int stopAtFirst;
    int failed;
} JsDomSearchContext;

static void JsDomCollect(JsDomSearchContext *context, JsDomNodeId node)
{
    if (context->failed || context->visited >= context->limit) {
        return;
    }
    context->visited += 1;
    if (context->tagName != NULL) {
        char name[JsDomNameCapacity];

        if (JsDomNodeKindOf(context->runtime, node) == (uint32_t)RoboJsDomNodeElement &&
            JsDomNodeNameOf(context->runtime, node, name, (int32_t)sizeof(name)) >= 0 &&
            JsDomLowerEquals(name, context->tagName)) {
            context->matches += 1;
            if (context->found == 0) {
                context->found = node;
            }
            if (context->collection != NULL) {
                JsValue entry = JsValueObjectOrUndefined(
                    JsDomWrapperForNode(context->runtime, node));

                JsArrayPush(context->runtime->heap, context->collection, &entry);
            }
        }
    } else if (JsDomSelectorMatches(context->runtime, node, context->selector, NULL, 0)) {
        context->matches += 1;
        if (context->found == 0) {
            context->found = node;
        }
        if (context->collection != NULL) {
            JsValue entry = JsValueObjectOrUndefined(JsDomWrapperForNode(context->runtime, node));

            JsArrayPush(context->runtime->heap, context->collection, &entry);
        }
    }
    if (context->stopAtFirst && context->found != 0) {
        return;
    }
}

/* Iterative pre-order walk with an explicit stack, so a deep tree cannot blow the C stack. */
/*
 * A pre-order walk in **document order**, with the ancestors on @p path (`JsDomPathCapacity` deep).
 *
 * Document order is the whole point: `querySelector("li")` must answer the *first* `li`, and
 * `querySelectorAll("li")[0].nextSibling` must be the second one. A worklist that pushes a node's
 * children and pops them visits those children in reverse, which answers the last match first — a
 * defect no count-based assertion sees, because the set of matches is the same.
 *
 * The vtable has `firstChild`/`nextSibling` and no parent link, so the ancestors are kept here: the
 * walk descends into a first child, and climbs through siblings (popping the path) when a subtree is
 * finished. That costs one `JsDomNodeId` per level and no per-node allocation, which is why the
 * caller's buffer is `JsDomPathCapacity` and not a node count.
 */
static void JsDomSearchTree(JsRuntime *runtime,
                            JsDomNodeId root,
                            JsDomSearchContext *context,
                            JsDomNodeId *path,
                            int32_t pathCapacity)
{
    const JsDomHost *host = JsDomHostOf(runtime);
    JsDomNodeId current;
    int32_t pathLength = 0;

    if (host == NULL || host->firstChild == NULL || host->nextSibling == NULL || root == 0) {
        return;
    }
    current = root;
    for (;;) {
        JsDomNodeId child;

        if (context->visited >= context->limit) {
            context->failed = 1;
            return;
        }
        /* The root is never a match: `el.querySelectorAll("div")` answers the element's descendants,
         * not the element itself (and the document node is not an element, so a document-wide search
         * is unaffected either way). */
        if (current != root) {
            JsDomCollect(context, current);
            if (context->stopAtFirst && context->found != 0) {
                return;
            }
        }
        child = host->firstChild(host->userdata, current);
        if (child != 0) {
            if (pathLength >= pathCapacity) {
                context->failed = 1;
                return;
            }
            path[pathLength++] = current;
            current = child;
            continue;
        }
        /* No children: the next place to be is the nearest ancestor that still has a right sibling.
         * The climb stops **at the root**: its siblings are the caller's parent's children, outside the
         * subtree that was asked about. Taking one answered `app.querySelectorAll("li")` with the whole
         * document's list, which is the shape of defect a count-only assertion cannot see. */
        for (;;) {
            JsDomNodeId sibling;

            if (current == root) {
                return;
            }
            sibling = host->nextSibling(host->userdata, current);
            if (sibling != 0) {
                current = sibling;
                break;
            }
            if (pathLength == 0) {
                return;
            }
            current = path[--pathLength];
        }
    }
}

static JsDomNodeId JsDomSearchRoot(JsRuntime *runtime, JsDomNodeId node)
{
    const JsDomHost *host = JsDomHostOf(runtime);

    if (JsDomNodeKindOf(runtime, node) == (uint32_t)RoboJsDomNodeDocument) {
        return host != NULL && host->documentRoot != NULL ? host->documentRoot(host->userdata) : node;
    }
    return node;
}

static Result JsDomQuery(JsRuntime *runtime,
                         JsObject *thisObject,
                         const JsValue *args,
                         int32_t argCount,
                         JsValue *out,
                         int all)
{
    JsDomNodeId node;
    JsDomNodeId root;
    char selector[JsDomValueCapacity];
    char errBuf[128];
    JsDomNodeId stack[JsDomPathCapacity];
    JsDomSearchContext context;

    if (JsDomElementOfThis(runtime, thisObject, &node) != RoboOk) {
        return RoboError;
    }
    if (argCount < 1) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "a selector is required");
        return RoboError;
    }
    JsValueToString(&args[0], selector, sizeof(selector));
    if (!JsDomSelectorValidate(selector, errBuf, (int32_t)sizeof(errBuf))) {
        JsRuntimeThrow(runtime, RoboJsErrorSyntax, NULL, "'%s': %s", selector, errBuf);
        return RoboError;
    }
    root = JsDomSearchRoot(runtime, node);
    memset(&context, 0, sizeof(context));
    context.runtime = runtime;
    context.selector = selector;
    context.limit = 4096;
    context.stopAtFirst = all ? 0 : 1;
    if (all) {
        context.collection = JsArrayCreate(runtime->heap, runtime->arrayPrototype, 0);
        if (context.collection == NULL) {
            return RoboOutOfMemory;
        }
    }
    JsDomSearchTree(runtime, root, &context, stack, JsDomPathCapacity);
    if (!all) {
        *out = JsDomReferenceValue(runtime, context.found);
        return RoboOk;
    }
    *out = JsValueObject(context.collection);
    return RoboOk;
}

static Result JsDomNativeQuerySelector(JsRuntime *runtime,
                                       JsObject *thisObject,
                                       const JsValue *args,
                                       int32_t argCount,
                                       JsValue *out,
                                       void *userdata)
{
    (void)userdata;
    return JsDomQuery(runtime, thisObject, args, argCount, out, 0);
}

static Result JsDomNativeQuerySelectorAll(JsRuntime *runtime,
                                          JsObject *thisObject,
                                          const JsValue *args,
                                          int32_t argCount,
                                          JsValue *out,
                                          void *userdata)
{
    (void)userdata;
    return JsDomQuery(runtime, thisObject, args, argCount, out, 1);
}

static Result JsDomNativeGetElementsByTagName(JsRuntime *runtime,
                                              JsObject *thisObject,
                                              const JsValue *args,
                                              int32_t argCount,
                                              JsValue *out,
                                              void *userdata)
{
    JsDomNodeId node;
    JsDomNodeId root;
    char tag[JsDomNameCapacity];
    JsDomNodeId stack[JsDomPathCapacity];
    JsDomSearchContext context;
    int32_t i;

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &node) != RoboOk) {
        return RoboError;
    }
    tag[0] = '\0';
    if (argCount >= 1) {
        JsValueToString(&args[0], tag, sizeof(tag));
    }
    for (i = 0; tag[i] != '\0'; ++i) {
        if (tag[i] >= 'A' && tag[i] <= 'Z') {
            tag[i] = (char)(tag[i] - 'A' + 'a');
        }
    }
    root = JsDomSearchRoot(runtime, node);
    memset(&context, 0, sizeof(context));
    context.runtime = runtime;
    context.tagName = tag;
    context.limit = 4096;
    context.collection = JsArrayCreate(runtime->heap, runtime->arrayPrototype, 0);
    if (context.collection == NULL) {
        return RoboOutOfMemory;
    }
    JsDomSearchTree(runtime, root, &context, stack, JsDomPathCapacity);
    *out = JsValueObject(context.collection);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* document's own doors                                                */
/* ------------------------------------------------------------------ */

static Result JsDomNativeGetElementById(JsRuntime *runtime,
                                        JsObject *thisObject,
                                        const JsValue *args,
                                        int32_t argCount,
                                        JsValue *out,
                                        void *userdata)
{
    const JsDomHost *host = JsDomHostOf(runtime);
    char id[JsDomValueCapacity];

    (void)thisObject;
    (void)userdata;
    if (host == NULL || host->findById == NULL) {
        *out = JsValueNull();
        return RoboOk;
    }
    if (argCount < 1) {
        *out = JsValueNull();
        return RoboOk;
    }
    JsValueToString(&args[0], id, sizeof(id));
    *out = JsDomReferenceValue(runtime, host->findById(host->userdata, id));
    return RoboOk;
}

static Result JsDomNativeCreateElement(JsRuntime *runtime,
                                       JsObject *thisObject,
                                       const JsValue *args,
                                       int32_t argCount,
                                       JsValue *out,
                                       void *userdata)
{
    const JsDomHost *host = JsDomHostOf(runtime);
    char tag[JsDomNameCapacity];
    JsDomNodeId node;

    (void)thisObject;
    (void)userdata;
    if (argCount < 1) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "createElement needs a tag name");
        return RoboError;
    }
    JsValueToString(&args[0], tag, sizeof(tag));
    if (tag[0] == '\0') {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "createElement needs a tag name");
        return RoboError;
    }
    if (host == NULL || host->createElement == NULL || !JsDomCanMutate(runtime)) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "the document is not available");
        return RoboError;
    }
    node = host->createElement(host->userdata, tag);
    if (node == 0) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "createElement('%s') failed", tag);
        return RoboError;
    }
    runtime->counters.created += 1;
    *out = JsDomReferenceValue(runtime, node);
    return RoboOk;
}

static Result JsDomNativeCreateTextNode(JsRuntime *runtime,
                                        JsObject *thisObject,
                                        const JsValue *args,
                                        int32_t argCount,
                                        JsValue *out,
                                        void *userdata)
{
    const JsDomHost *host = JsDomHostOf(runtime);
    char text[JsDomValueCapacity];
    JsDomNodeId node;

    (void)thisObject;
    (void)userdata;
    text[0] = '\0';
    if (argCount >= 1) {
        JsValueToString(&args[0], text, sizeof(text));
    }
    if (host == NULL || host->createTextNode == NULL || !JsDomCanMutate(runtime)) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "the document is not available");
        return RoboError;
    }
    node = host->createTextNode(host->userdata, text, (int32_t)strlen(text));
    if (node == 0) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "createTextNode failed");
        return RoboError;
    }
    runtime->counters.textNodesCreated += 1;
    *out = JsDomReferenceValue(runtime, node);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* style's methods                                                     */
/* ------------------------------------------------------------------ */

static Result JsDomNativeStyleSetProperty(JsRuntime *runtime,
                                          JsObject *thisObject,
                                          const JsValue *args,
                                          int32_t argCount,
                                          JsValue *out,
                                          void *userdata)
{
    JsDomNodeId node = JsDomStyleNodeOf(thisObject);
    JsDomWrapperState *state = (JsDomWrapperState *)thisObject->hostData;
    char text[JsDomValueCapacity];
    char name[JsDomNameCapacity];
    char value[JsDomValueCapacity];

    (void)userdata;
    if (state == NULL || node == 0) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "not a style object");
        return RoboError;
    }
    if (argCount < 2) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "setProperty needs a name and a value");
        return RoboError;
    }
    JsValueToString(&args[0], name, sizeof(name));
    JsValueToString(&args[1], value, sizeof(value));
    if (JsDomAttributeInto(runtime, node, "style", text, (int32_t)sizeof(text)) == NULL) {
        text[0] = '\0';
    }
    if (JsDomStyleRewrite(runtime, node, text, name, value) != RoboOk) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "the document is not available");
        return RoboError;
    }
    *out = JsValueUndefined();
    return RoboOk;
}

static Result JsDomNativeStyleGetPropertyValue(JsRuntime *runtime,
                                               JsObject *thisObject,
                                               const JsValue *args,
                                               int32_t argCount,
                                               JsValue *out,
                                               void *userdata)
{
    JsDomNodeId node = JsDomStyleNodeOf(thisObject);
    char text[JsDomValueCapacity];
    char name[JsDomNameCapacity];
    char value[JsDomValueCapacity];

    (void)userdata;
    value[0] = '\0';
    if (argCount >= 1) {
        JsValueToString(&args[0], name, sizeof(name));
        if (JsDomAttributeInto(runtime, node, "style", text, (int32_t)sizeof(text)) != NULL) {
            JsDomStyleFind(text, name, value, (int32_t)sizeof(value));
        }
    }
    *out = JsValueString(runtime->heap, value, -1);
    return RoboOk;
}

static Result JsDomNativeStyleRemoveProperty(JsRuntime *runtime,
                                             JsObject *thisObject,
                                             const JsValue *args,
                                             int32_t argCount,
                                             JsValue *out,
                                             void *userdata)
{
    JsDomNodeId node = JsDomStyleNodeOf(thisObject);
    char text[JsDomValueCapacity];
    char name[JsDomNameCapacity];
    char value[JsDomValueCapacity];

    (void)userdata;
    value[0] = '\0';
    if (argCount >= 1) {
        JsValueToString(&args[0], name, sizeof(name));
        if (JsDomAttributeInto(runtime, node, "style", text, (int32_t)sizeof(text)) != NULL) {
            JsDomStyleFind(text, name, value, (int32_t)sizeof(value));
            JsDomStyleRewrite(runtime, node, text, name, NULL);
        }
    }
    *out = JsValueString(runtime->heap, value, -1);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

static Result JsDomNativeAddEventListener(JsRuntime *runtime,
                                          JsObject *thisObject,
                                          const JsValue *args,
                                          int32_t argCount,
                                          JsValue *out,
                                          void *userdata)
{
    JsDomNodeId node;
    char type[JsDomNameCapacity];

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &node) != RoboOk) {
        return RoboError;
    }
    if (argCount < 2) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "addEventListener needs a type and a listener");
        return RoboError;
    }
    JsValueToString(&args[0], type, sizeof(type));
    if (!JsValueIsFunction(&args[1])) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "addEventListener needs a function");
        return RoboError;
    }
    if (JsEventAddListener(runtime, node, type, &args[1]) != RoboOk) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "addEventListener('%s') failed", type);
        return RoboError;
    }
    *out = JsValueUndefined();
    return RoboOk;
}

static Result JsDomNativeRemoveEventListener(JsRuntime *runtime,
                                             JsObject *thisObject,
                                             const JsValue *args,
                                             int32_t argCount,
                                             JsValue *out,
                                             void *userdata)
{
    JsDomNodeId node;
    char type[JsDomNameCapacity];

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &node) != RoboOk) {
        return RoboError;
    }
    if (argCount < 2) {
        *out = JsValueUndefined();
        return RoboOk;
    }
    JsValueToString(&args[0], type, sizeof(type));
    /* A listener that was never registered is not an error, per the DOM. */
    JsEventRemoveListener(runtime, node, type, &args[1]);
    *out = JsValueUndefined();
    return RoboOk;
}

static Result JsDomNativeDispatchEvent(JsRuntime *runtime,
                                       JsObject *thisObject,
                                       const JsValue *args,
                                       int32_t argCount,
                                       JsValue *out,
                                       void *userdata)
{
    JsDomNodeId node;
    JsObject *event;
    JsEventRecord record;
    JsEventResult result;
    JsValue type;
    JsValue field;
    char typeText[JsDomNameCapacity];

    (void)userdata;
    if (JsDomElementOfThis(runtime, thisObject, &node) != RoboOk) {
        return RoboError;
    }
    if (argCount < 1) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "dispatchEvent needs an event");
        return RoboError;
    }
    event = JsValueAsObject(&args[0]);
    if (event == NULL) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "dispatchEvent needs an event object");
        return RoboError;
    }
    typeText[0] = '\0';
    if (JsObjectGet(event, "type", &type) == RoboOk) {
        JsValueToString(&type, typeText, sizeof(typeText));
    }
    if (typeText[0] == '\0') {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "the event has no type");
        return RoboError;
    }
    if (JsEventRecordInit(&record, typeText, node) != RoboOk) {
        return RoboError;
    }
    if (JsObjectGet(event, "clientX", &field) == RoboOk) {
        record.clientX = JsValueToNumber(&field);
    }
    if (JsObjectGet(event, "clientY", &field) == RoboOk) {
        record.clientY = JsValueToNumber(&field);
    }
    if (JsObjectGet(event, "button", &field) == RoboOk) {
        record.button = JsValueToInt32(&field);
    }
    if (JsObjectGet(event, "modifiers", &field) == RoboOk) {
        record.modifiers = (uint32_t)JsValueToInt32(&field);
    }
    record.timestampMs = JsRuntimeNowMs(runtime);

    if (JsEventDispatchNow(runtime, &record, &result) != RoboOk) {
        return RoboError;
    }
    if (result.defaultPrevented) {
        JsObjectSetBool(runtime->heap, event, "defaultPrevented", 1);
    }
    *out = JsValueBool(result.defaultPrevented);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Installation                                                        */
/* ------------------------------------------------------------------ */

static Result JsDomRegisterNodeMethods(JsRuntime *runtime,
                                       JsObject *prototype,
                                       const char *name,
                                       JsNativeFn fn)
{
    return JsObjectDefineNative(runtime, prototype, name, 0, fn, NULL);
}

static int JsDomHostIsComplete(const JsDomHost *host)
{
    if (host == NULL) {
        return 0;
    }
    if (host->documentRoot == NULL || host->nodeKind == NULL || host->nodeName == NULL ||
        host->textContent == NULL || host->setTextContent == NULL || host->attribute == NULL ||
        host->setAttribute == NULL || host->appendChild == NULL) {
        return 0;
    }
    return 1;
}

Result JsDomInstall(JsRuntime *runtime, const JsDomHost *host)
{
    JsObject *nodePrototype;
    JsObject *elementPrototype;
    JsObject *documentPrototype;
    JsObject *stylePrototype;
    JsDomNodeId root;
    JsValue documentValue;
    JsValue windowValue;

    if (runtime == NULL || !JsDomHostIsComplete(host)) {
        return RoboInvalidArgument;
    }
    if (runtime->domInstalled != 0) {
        if (runtime->domHost.userdata == host->userdata &&
            runtime->domHost.documentRoot == host->documentRoot) {
            return RoboOk;
        }
        JsDomUninstall(runtime);
    }
    if (host->isLive != NULL && !host->isLive(host->userdata)) {
        return RoboInvalidArgument;
    }
    root = host->documentRoot(host->userdata);
    if (root == 0) {
        return RoboInvalidArgument;
    }
    if (runtime->wrappers == NULL) {
        runtime->wrappers = (JsWrapper *)JsHeapAlloc(runtime->heap,
                                                     sizeof(JsWrapper) * JsDomMaxWrappers);
        if (runtime->wrappers == NULL) {
            return RoboOutOfMemory;
        }
        runtime->wrapperCapacity = JsDomMaxWrappers;
    }
    runtime->wrapperCount = 0;
    runtime->domHost = *host;
    runtime->nodeOps.get = JsDomComputedGet;
    runtime->nodeOps.set = JsDomComputedSet;
    runtime->domInstalled = 1;

    nodePrototype = JsObjectCreate(runtime->heap, runtime->objectPrototype);
    elementPrototype = JsObjectCreate(runtime->heap, nodePrototype);
    documentPrototype = JsObjectCreate(runtime->heap, nodePrototype);
    stylePrototype = JsObjectCreate(runtime->heap, runtime->objectPrototype);
    if (nodePrototype == NULL || elementPrototype == NULL || documentPrototype == NULL ||
        stylePrototype == NULL) {
        return RoboOutOfMemory;
    }
    runtime->nodePrototype = nodePrototype;
    runtime->elementPrototype = elementPrototype;
    runtime->documentPrototype = documentPrototype;
    runtime->stylePrototype = stylePrototype;

    if (JsDomRegisterNodeMethods(runtime, nodePrototype, "getAttribute",
                                 JsDomNativeGetAttribute) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "setAttribute",
                                 JsDomNativeSetAttribute) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "removeAttribute",
                                 JsDomNativeRemoveAttribute) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "hasAttribute",
                                 JsDomNativeHasAttribute) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "appendChild",
                                 JsDomNativeAppendChild) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "insertBefore",
                                 JsDomNativeInsertBefore) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "removeChild",
                                 JsDomNativeRemoveChild) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "remove", JsDomNativeRemove) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "contains", JsDomNativeContains) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "matches", JsDomNativeMatches) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "querySelector",
                                 JsDomNativeQuerySelector) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "querySelectorAll",
                                 JsDomNativeQuerySelectorAll) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "getElementsByTagName",
                                 JsDomNativeGetElementsByTagName) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "addEventListener",
                                 JsDomNativeAddEventListener) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "removeEventListener",
                                 JsDomNativeRemoveEventListener) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, nodePrototype, "dispatchEvent",
                                 JsDomNativeDispatchEvent) != RoboOk) {
        return RoboOutOfMemory;
    }
    if (JsDomRegisterNodeMethods(runtime, documentPrototype, "getElementById",
                                 JsDomNativeGetElementById) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, documentPrototype, "createElement",
                                 JsDomNativeCreateElement) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, documentPrototype, "createTextNode",
                                 JsDomNativeCreateTextNode) != RoboOk) {
        return RoboOutOfMemory;
    }
    if (JsDomRegisterNodeMethods(runtime, stylePrototype, "setProperty",
                                 JsDomNativeStyleSetProperty) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, stylePrototype, "getPropertyValue",
                                 JsDomNativeStyleGetPropertyValue) != RoboOk ||
        JsDomRegisterNodeMethods(runtime, stylePrototype, "removeProperty",
                                 JsDomNativeStyleRemoveProperty) != RoboOk) {
        return RoboOutOfMemory;
    }
    /* `styleOps` is the style object's own two-field table; the node table above is a different
     * pointer, so the two never collide. */
    runtime->styleOps.get = JsDomStyleGetProperty;
    runtime->styleOps.set = JsDomStyleSetProperty;

    if (JsEventInstall(runtime) != RoboOk) {
        return RoboOutOfMemory;
    }
    runtime->documentObject = JsDomWrapperForNode(runtime, root);
    if (runtime->documentObject == NULL) {
        return RoboOutOfMemory;
    }
    documentValue = JsValueObject(runtime->documentObject);
    if (JsRuntimeDefineGlobal(runtime, "document", &documentValue) != RoboOk) {
        return RoboOutOfMemory;
    }
    /* `window` **is** the global object, which is what makes `window.foo = 1` and `foo` one thing
     * rather than two that can disagree. */
    windowValue = JsValueObject(runtime->globalObject);
    if (JsRuntimeDefineGlobal(runtime, "window", &windowValue) != RoboOk) {
        return RoboOutOfMemory;
    }
    return RoboOk;
}

int32_t JsDomUninstall(JsRuntime *runtime)
{
    int32_t dropped;

    if (runtime == NULL) {
        return 0;
    }
    dropped = runtime->wrapperCount;
    runtime->wrapperCount = 0;
    runtime->documentObject = NULL;
    runtime->nodePrototype = NULL;
    runtime->elementPrototype = NULL;
    runtime->documentPrototype = NULL;
    runtime->stylePrototype = NULL;
    runtime->domInstalled = 0;
    memset(&runtime->domHost, 0, sizeof(runtime->domHost));
    memset(&runtime->nodeOps, 0, sizeof(runtime->nodeOps));
    memset(&runtime->styleOps, 0, sizeof(runtime->styleOps));
    /* Both readers of the name, or `typeof document` would still answer through the environment
     * binding after the global object's property went away (`JsRuntimeDeleteGlobal`). */
    JsRuntimeDeleteGlobal(runtime, "document");
    JsRuntimeDeleteGlobal(runtime, "window");
    return dropped;
}
