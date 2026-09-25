/*
 * Shared helpers for the RoboJS suites (Phase 16) — an evaluator shorthand, and a **fixture DOM**.
 *
 * ## Why the fixture exists
 *
 * `RoboJSDom.h` makes the DOM a vtable, and this header is the second implementation of it: a
 * thirty-two node tree in a struct, with no HTML parser, no layout and no paint. That is what lets
 * the DOM/event/timer suites assert *the binding's* behaviour — wrapper identity, `querySelector`,
 * listener order, `preventDefault` — without an HTML document in the picture, and it is the same
 * split the module's own header promises ("a host test implements the same vtable over a fixture").
 * `SuiteRoboJSIntegration.c` is where the RoboHTML adapter is exercised instead.
 *
 * ## Everything is `static inline`
 *
 * A suite that does not use the fixture pays nothing for including this header, and no translation
 * unit grows an unused-function warning — which in this tree is an error.
 */

#ifndef ROBO_JS_TEST_SUPPORT_H
#define ROBO_JS_TEST_SUPPORT_H

#include "RoboTest.h"

#include "RoboJS/RoboJS.h"
#include "RoboJS/RoboJSDom.h"
/* The adapter's counter record, which is the runtime's own counters behind one door. It is the one
 * RoboJS header that includes RoboHTML, so a suite that does not read it still compiles the same. */
#include "RoboJS/RoboJSDomHtml.h"

#include <stdio.h>
#include <string.h>

/*
 * `NaN` and `Infinity` without `<math.h>`.
 *
 * The guest libc has no math header (the module itself carries the same shim), and a suite is not
 * allowed to reach into a module's private include path. Two divisions from variables — never
 * constants, so no compiler folds a division by zero and no warning is an error.
 */
static inline double JsTestNan(void)
{
    double zero = 0.0;

    return zero / zero;
}

static inline double JsTestInfinity(void)
{
    double one = 1.0;
    double zero = 0.0;

    return one / zero;
}

#define JsTestDomMaxNodes 32
#define JsTestDomMaxAttributes 8

typedef struct JsTestDomAttribute {
    char name[32];
    char value[96];
} JsTestDomAttribute;

typedef struct JsTestDomNode {
    int inUse;
    uint32_t kind; /* RoboJsDomNode* */
    char name[32];
    char text[128];
    JsTestDomAttribute attributes[JsTestDomMaxAttributes];
    int attributeCount;
    JsDomNodeId parent;
    JsDomNodeId children[JsTestDomMaxNodes];
    int childCount;
} JsTestDomNode;

typedef struct JsTestDom {
    JsTestDomNode nodes[JsTestDomMaxNodes];
    int32_t liveNodes;
    int alive;      /* the `isLive` answer; 0 makes every mutating door refuse */
    int32_t mutations;
    int32_t invalidations;
} JsTestDom;

/* ------------------------------------------------------------------ */
/* Building the fixture                                                */
/* ------------------------------------------------------------------ */

static inline JsDomNodeId JsTestDomAdd(JsTestDom *dom,
                                      uint32_t kind,
                                      const char *name,
                                      const char *text,
                                      JsDomNodeId parent)
{
    int32_t i;

    for (i = 0; i < JsTestDomMaxNodes; ++i) {
        if (!dom->nodes[i].inUse) {
            JsTestDomNode *node = &dom->nodes[i];

            memset(node, 0, sizeof(*node));
            node->inUse = 1;
            node->kind = kind;
            if (name != NULL) {
                snprintf(node->name, sizeof(node->name), "%s", name);
            }
            if (text != NULL) {
                snprintf(node->text, sizeof(node->text), "%s", text);
            }
            node->parent = parent;
            dom->liveNodes += 1;
            if (parent != 0) {
                JsTestDomNode *parentNode = &dom->nodes[parent - 1u];

                if (parentNode->childCount < JsTestDomMaxNodes) {
                    parentNode->children[parentNode->childCount] = (JsDomNodeId)(i + 1);
                    parentNode->childCount += 1;
                }
            }
            return (JsDomNodeId)(i + 1);
        }
    }
    return 0;
}

static inline JsTestDomAttribute *JsTestDomAttributeOf(JsTestDom *dom,
                                                       JsDomNodeId node,
                                                       const char *name,
                                                       int create)
{
    JsTestDomNode *record = &dom->nodes[node - 1u];
    int32_t i;

    for (i = 0; i < record->attributeCount; ++i) {
        if (strcmp(record->attributes[i].name, name) == 0) {
            return &record->attributes[i];
        }
    }
    if (!create || record->attributeCount >= JsTestDomMaxAttributes) {
        return NULL;
    }
    snprintf(record->attributes[record->attributeCount].name,
             sizeof(record->attributes[0].name),
             "%s",
             name);
    record->attributes[record->attributeCount].value[0] = '\0';
    record->attributeCount += 1;
    return &record->attributes[record->attributeCount - 1];
}

/**
 * The fixture every DOM-ish suite uses:
 *
 *   #document
 *     html
 *       head
 *         title (text "RoboJS")
 *       body
 *         div#app
 *         p.intro (text "hello")
 *         ul
 *           li (text "one")
 *           li (text "two")
 */
static inline JsDomNodeId JsTestDomBuild(JsTestDom *dom)
{
    JsDomNodeId document;
    JsDomNodeId html;
    JsDomNodeId head;
    JsDomNodeId title;
    JsDomNodeId body;
    JsDomNodeId app;
    JsDomNodeId intro;
    JsDomNodeId list;
    JsDomNodeId item;
    JsTestDomAttribute *attribute;

    memset(dom, 0, sizeof(*dom));
    dom->alive = 1;
    document = JsTestDomAdd(dom, RoboJsDomNodeDocument, "#document", NULL, 0);
    html = JsTestDomAdd(dom, RoboJsDomNodeElement, "html", NULL, document);
    head = JsTestDomAdd(dom, RoboJsDomNodeElement, "head", NULL, html);
    title = JsTestDomAdd(dom, RoboJsDomNodeElement, "title", NULL, head);
    JsTestDomAdd(dom, RoboJsDomNodeText, "#text", "RoboJS", title);
    body = JsTestDomAdd(dom, RoboJsDomNodeElement, "body", NULL, html);
    app = JsTestDomAdd(dom, RoboJsDomNodeElement, "div", NULL, body);
    attribute = JsTestDomAttributeOf(dom, app, "id", 1);
    snprintf(attribute->value, sizeof(attribute->value), "app");
    intro = JsTestDomAdd(dom, RoboJsDomNodeElement, "p", NULL, body);
    attribute = JsTestDomAttributeOf(dom, intro, "class", 1);
    snprintf(attribute->value, sizeof(attribute->value), "intro lead");
    JsTestDomAdd(dom, RoboJsDomNodeText, "#text", "hello", intro);
    list = JsTestDomAdd(dom, RoboJsDomNodeElement, "ul", NULL, body);
    item = JsTestDomAdd(dom, RoboJsDomNodeElement, "li", NULL, list);
    JsTestDomAdd(dom, RoboJsDomNodeText, "#text", "one", item);
    item = JsTestDomAdd(dom, RoboJsDomNodeElement, "li", NULL, list);
    JsTestDomAdd(dom, RoboJsDomNodeText, "#text", "two", item);
    return document;
}

/* ------------------------------------------------------------------ */
/* The vtable                                                          */
/* ------------------------------------------------------------------ */

static inline int JsTestDomIsLive(void *userdata)
{
    return ((JsTestDom *)userdata)->alive;
}

static inline JsTestDomNode *JsTestDomNodeOf(JsTestDom *dom, JsDomNodeId node)
{
    if (dom == NULL || node == 0 || node > JsTestDomMaxNodes) {
        return NULL;
    }
    if (!dom->nodes[node - 1u].inUse) {
        return NULL;
    }
    return &dom->nodes[node - 1u];
}

static inline JsDomNodeId JsTestDomDocumentRoot(void *userdata)
{
    JsTestDom *dom = (JsTestDom *)userdata;

    return JsTestDomNodeOf(dom, 1) != NULL ? 1 : 0;
}

static inline JsDomNodeId JsTestDomDocumentBody(void *userdata)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    JsDomNodeId html = JsTestDomNodeOf(dom, 1) != NULL ? dom->nodes[0].children[0] : 0;
    int32_t i;

    for (i = 0; i < dom->nodes[html - 1u].childCount; ++i) {
        JsTestDomNode *child = JsTestDomNodeOf(dom, dom->nodes[html - 1u].children[i]);

        if (child != NULL && strcmp(child->name, "body") == 0) {
            return (JsDomNodeId)(child - dom->nodes + 1);
        }
    }
    return 0;
}

static inline JsDomNodeId JsTestDomFindById(void *userdata, const char *id)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    int32_t i;

    for (i = 0; i < JsTestDomMaxNodes; ++i) {
        JsTestDomAttribute *attribute;

        if (!dom->nodes[i].inUse || dom->nodes[i].kind != (uint32_t)RoboJsDomNodeElement) {
            continue;
        }
        attribute = JsTestDomAttributeOf(dom, (JsDomNodeId)(i + 1), "id", 0);
        if (attribute != NULL && strcmp(attribute->value, id) == 0) {
            return (JsDomNodeId)(i + 1);
        }
    }
    return 0;
}

static inline JsDomNodeId JsTestDomFindByTag(void *userdata, JsDomNodeId root, const char *tag)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    JsDomNodeId stack[JsTestDomMaxNodes];
    int32_t top = 0;

    if (root != 0) {
        stack[top++] = root;
    }
    while (top > 0) {
        JsDomNodeId current = stack[--top];
        JsTestDomNode *node = JsTestDomNodeOf(dom, current);
        int32_t i;

        if (node == NULL) {
            continue;
        }
        if (node->kind == (uint32_t)RoboJsDomNodeElement && strcmp(node->name, tag) == 0 &&
            current != root) {
            return current;
        }
        for (i = node->childCount - 1; i >= 0; --i) {
            if (top < JsTestDomMaxNodes) {
                stack[top++] = node->children[i];
            }
        }
    }
    return 0;
}

static inline JsDomNodeId JsTestDomCreateElement(void *userdata, const char *tag)
{
    JsTestDom *dom = (JsTestDom *)userdata;

    dom->mutations += 1;
    /* A created element is detached in the fixture too, which is the state the real adapter reaches
     * only through a pending record. */
    return JsTestDomAdd(dom, RoboJsDomNodeElement, tag, NULL, 0);
}

static inline JsDomNodeId JsTestDomCreateTextNode(void *userdata, const char *text, int32_t length)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    JsDomNodeId node = JsTestDomAdd(dom, RoboJsDomNodeText, "#text", "", 0);

    (void)length;
    if (node != 0) {
        snprintf(dom->nodes[node - 1u].text, sizeof(dom->nodes[0].text), "%s", text);
    }
    dom->mutations += 1;
    return node;
}

static inline JsDomNodeId JsTestDomParent(void *userdata, JsDomNodeId node)
{
    JsTestDomNode *record = JsTestDomNodeOf((JsTestDom *)userdata, node);

    return record == NULL ? 0 : record->parent;
}

static inline JsDomNodeId JsTestDomFirstChild(void *userdata, JsDomNodeId node)
{
    JsTestDomNode *record = JsTestDomNodeOf((JsTestDom *)userdata, node);

    return record == NULL || record->childCount == 0 ? 0 : record->children[0];
}

static inline JsDomNodeId JsTestDomLastChild(void *userdata, JsDomNodeId node)
{
    JsTestDomNode *record = JsTestDomNodeOf((JsTestDom *)userdata, node);

    return record == NULL || record->childCount == 0 ? 0 : record->children[record->childCount - 1];
}

static inline int32_t JsTestDomChildIndex(JsTestDom *dom, JsDomNodeId node)
{
    JsTestDomNode *record = JsTestDomNodeOf(dom, node);
    JsTestDomNode *parent;
    int32_t i;

    if (record == NULL || record->parent == 0) {
        return -1;
    }
    parent = JsTestDomNodeOf(dom, record->parent);
    if (parent == NULL) {
        return -1;
    }
    for (i = 0; i < parent->childCount; ++i) {
        if (parent->children[i] == node) {
            return i;
        }
    }
    return -1;
}

static inline JsDomNodeId JsTestDomNextSibling(void *userdata, JsDomNodeId node)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    int32_t index = JsTestDomChildIndex(dom, node);
    JsTestDomNode *parent;

    if (index < 0) {
        return 0;
    }
    parent = JsTestDomNodeOf(dom, JsTestDomNodeOf(dom, node)->parent);
    return index + 1 < parent->childCount ? parent->children[index + 1] : 0;
}

static inline JsDomNodeId JsTestDomPreviousSibling(void *userdata, JsDomNodeId node)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    int32_t index = JsTestDomChildIndex(dom, node);
    JsTestDomNode *parent;

    if (index <= 0) {
        return 0;
    }
    parent = JsTestDomNodeOf(dom, JsTestDomNodeOf(dom, node)->parent);
    return parent->children[index - 1];
}

static inline int32_t JsTestDomChildCount(void *userdata, JsDomNodeId node)
{
    JsTestDomNode *record = JsTestDomNodeOf((JsTestDom *)userdata, node);

    return record == NULL ? 0 : record->childCount;
}

static inline JsDomNodeId JsTestDomChildAt(void *userdata, JsDomNodeId node, int32_t index)
{
    JsTestDomNode *record = JsTestDomNodeOf((JsTestDom *)userdata, node);

    if (record == NULL || index < 0 || index >= record->childCount) {
        return 0;
    }
    return record->children[index];
}

static inline uint32_t JsTestDomNodeKind(void *userdata, JsDomNodeId node)
{
    JsTestDomNode *record = JsTestDomNodeOf((JsTestDom *)userdata, node);

    return record == NULL ? (uint32_t)RoboJsDomNodeNone : record->kind;
}

static inline int32_t JsTestDomNodeName(void *userdata,
                                       JsDomNodeId node,
                                       char *buffer,
                                       int32_t capacity)
{
    JsTestDomNode *record = JsTestDomNodeOf((JsTestDom *)userdata, node);
    int32_t length;

    if (capacity <= 0 || record == NULL) {
        return -1;
    }
    length = (int32_t)strlen(record->name);
    snprintf(buffer, (size_t)capacity, "%s", record->name);
    return length;
}

static inline int32_t JsTestDomTextContent(void *userdata,
                                           JsDomNodeId node,
                                           char *buffer,
                                           int32_t capacity)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    JsTestDomNode *record = JsTestDomNodeOf(dom, node);
    size_t used = 0;
    int32_t i;

    if (capacity <= 0 || record == NULL) {
        return -1;
    }
    buffer[0] = '\0';
    if (record->kind == (uint32_t)RoboJsDomNodeText) {
        snprintf(buffer, (size_t)capacity, "%s", record->text);
        return (int32_t)strlen(record->text);
    }
    for (i = 0; i < record->childCount; ++i) {
        char piece[256];
        int32_t written;

        written = JsTestDomTextContent(dom, record->children[i], piece, (int32_t)sizeof(piece));
        if (written <= 0) {
            continue;
        }
        if (used + (size_t)written + 1u < (size_t)capacity) {
            memcpy(buffer + used, piece, (size_t)written);
            used += (size_t)written;
            buffer[used] = '\0';
        }
    }
    return (int32_t)used;
}

static inline int32_t JsTestDomAttributeCountOf(void *userdata, JsDomNodeId node)
{
    JsTestDomNode *record = JsTestDomNodeOf((JsTestDom *)userdata, node);

    return record == NULL ? 0 : record->attributeCount;
}

static inline int32_t JsTestDomRemoveChildren(JsTestDom *dom, JsDomNodeId node)
{
    JsTestDomNode *record = JsTestDomNodeOf(dom, node);
    int32_t removed;

    if (record == NULL) {
        return 0;
    }
    removed = record->childCount;
    while (record->childCount > 0) {
        JsTestDomNode *child = JsTestDomNodeOf(dom, record->children[record->childCount - 1]);

        if (child != NULL) {
            child->parent = 0;
            child->inUse = 0;
            dom->liveNodes -= 1;
        }
        record->childCount -= 1;
    }
    return removed;
}

static inline Result JsTestDomSetTextContent(void *userdata,
                                             JsDomNodeId node,
                                             const char *text,
                                             int32_t length)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    JsTestDomNode *record = JsTestDomNodeOf(dom, node);
    JsDomNodeId child;

    if (record == NULL) {
        return RoboNotFound;
    }
    if (record->kind == (uint32_t)RoboJsDomNodeText) {
        snprintf(record->text, sizeof(record->text), "%.*s", (int)length, text);
        dom->mutations += 1;
        return RoboOk;
    }
    JsTestDomRemoveChildren(dom, node);
    child = JsTestDomAdd(dom, RoboJsDomNodeText, "#text", "", node);
    if (child == 0) {
        return RoboOutOfMemory;
    }
    snprintf(dom->nodes[child - 1u].text,
             sizeof(dom->nodes[0].text),
             "%.*s",
             (int)length,
             text);
    dom->mutations += 1;
    return RoboOk;
}

static inline int32_t JsTestDomAttributeGet(void *userdata,
                                            JsDomNodeId node,
                                            const char *name,
                                            char *buffer,
                                            int32_t capacity)
{
    JsTestDomAttribute *attribute = JsTestDomAttributeOf((JsTestDom *)userdata, node, name, 0);
    int32_t length;

    if (capacity <= 0) {
        return -1;
    }
    buffer[0] = '\0';
    if (attribute == NULL) {
        return -1;
    }
    length = (int32_t)strlen(attribute->value);
    snprintf(buffer, (size_t)capacity, "%s", attribute->value);
    return length;
}

static inline Result JsTestDomSetAttribute(void *userdata,
                                           JsDomNodeId node,
                                           const char *name,
                                           const char *value)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    JsTestDomAttribute *attribute = JsTestDomAttributeOf(dom, node, name, 1);

    if (attribute == NULL) {
        return RoboNotSupported;
    }
    snprintf(attribute->value, sizeof(attribute->value), "%s", value);
    dom->mutations += 1;
    return RoboOk;
}

static inline Result JsTestDomRemoveAttribute(void *userdata, JsDomNodeId node, const char *name)
{
    JsTestDomNode *record = JsTestDomNodeOf((JsTestDom *)userdata, node);
    int32_t i;

    if (record == NULL) {
        return RoboNotFound;
    }
    for (i = 0; i < record->attributeCount; ++i) {
        if (strcmp(record->attributes[i].name, name) == 0) {
            int32_t move;

            for (move = i; move + 1 < record->attributeCount; ++move) {
                record->attributes[move] = record->attributes[move + 1];
            }
            record->attributeCount -= 1;
            ((JsTestDom *)userdata)->mutations += 1;
            return RoboOk;
        }
    }
    return RoboNotFound;
}

static inline int32_t JsTestDomAttributeNameAt(void *userdata,
                                               JsDomNodeId node,
                                               int32_t index,
                                               char *buffer,
                                               int32_t capacity)
{
    JsTestDomNode *record = JsTestDomNodeOf((JsTestDom *)userdata, node);
    int32_t length;

    if (capacity <= 0 || record == NULL || index < 0 || index >= record->attributeCount) {
        return -1;
    }
    length = (int32_t)strlen(record->attributes[index].name);
    snprintf(buffer, (size_t)capacity, "%s", record->attributes[index].name);
    return length;
}

static inline Result JsTestDomAppendChild(void *userdata, JsDomNodeId parent, JsDomNodeId child)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    JsTestDomNode *parentNode = JsTestDomNodeOf(dom, parent);
    JsTestDomNode *childNode = JsTestDomNodeOf(dom, child);
    JsDomNodeId previous;

    if (parentNode == NULL || childNode == NULL) {
        return RoboNotFound;
    }
    previous = childNode->parent;
    if (previous != 0) {
        JsTestDomNode *previousNode = JsTestDomNodeOf(dom, previous);
        int32_t index = JsTestDomChildIndex(dom, child);
        int32_t move;

        if (previousNode != NULL && index >= 0) {
            for (move = index; move + 1 < previousNode->childCount; ++move) {
                previousNode->children[move] = previousNode->children[move + 1];
            }
            previousNode->childCount -= 1;
        }
    }
    if (parentNode->childCount >= JsTestDomMaxNodes) {
        return RoboNotSupported;
    }
    parentNode->children[parentNode->childCount] = child;
    parentNode->childCount += 1;
    childNode->parent = parent;
    dom->mutations += 1;
    return RoboOk;
}

static inline Result JsTestDomInsertBefore(void *userdata,
                                           JsDomNodeId parent,
                                           JsDomNodeId child,
                                           JsDomNodeId before)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    JsTestDomNode *parentNode = JsTestDomNodeOf(dom, parent);
    int32_t index;
    int32_t move;

    if (parentNode == NULL) {
        return RoboNotFound;
    }
    if (before == 0) {
        return JsTestDomAppendChild(userdata, parent, child);
    }
    index = JsTestDomChildIndex(dom, before);
    if (index < 0 || parentNode->childCount >= JsTestDomMaxNodes) {
        return RoboNotFound;
    }
    for (move = parentNode->childCount; move > index; --move) {
        parentNode->children[move] = parentNode->children[move - 1];
    }
    parentNode->children[index] = child;
    parentNode->childCount += 1;
    if (JsTestDomNodeOf(dom, child) != NULL) {
        JsTestDomNodeOf(dom, child)->parent = parent;
    }
    dom->mutations += 1;
    return RoboOk;
}

static inline Result JsTestDomRemoveChild(void *userdata, JsDomNodeId parent, JsDomNodeId child)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    JsTestDomNode *parentNode = JsTestDomNodeOf(dom, parent);
    int32_t index = JsTestDomChildIndex(dom, child);
    int32_t move;

    if (parentNode == NULL || index < 0) {
        return RoboNotFound;
    }
    for (move = index; move + 1 < parentNode->childCount; ++move) {
        parentNode->children[move] = parentNode->children[move + 1];
    }
    parentNode->childCount -= 1;
    if (JsTestDomNodeOf(dom, child) != NULL) {
        JsTestDomNodeOf(dom, child)->parent = 0;
    }
    dom->mutations += 1;
    return RoboOk;
}

static inline int JsTestDomContains(void *userdata, JsDomNodeId ancestor, JsDomNodeId node)
{
    JsTestDom *dom = (JsTestDom *)userdata;
    JsDomNodeId cursor = node;
    int32_t guard = 0;

    while (cursor != 0 && guard < JsTestDomMaxNodes) {
        if (cursor == ancestor) {
            return 1;
        }
        cursor = JsTestDomParent(dom, cursor);
        guard += 1;
    }
    return 0;
}

static inline void JsTestDomInvalidate(void *userdata, JsDomNodeId node)
{
    (void)node;
    ((JsTestDom *)userdata)->invalidations += 1;
}

static inline void JsTestDomHostInit(JsDomHost *host, JsTestDom *dom)
{
    memset(host, 0, sizeof(*host));
    host->userdata = dom;
    host->isLive = JsTestDomIsLive;
    host->documentRoot = JsTestDomDocumentRoot;
    host->documentBody = JsTestDomDocumentBody;
    host->findById = JsTestDomFindById;
    host->findByTag = JsTestDomFindByTag;
    host->createElement = JsTestDomCreateElement;
    host->createTextNode = JsTestDomCreateTextNode;
    host->parent = JsTestDomParent;
    host->firstChild = JsTestDomFirstChild;
    host->lastChild = JsTestDomLastChild;
    host->nextSibling = JsTestDomNextSibling;
    host->previousSibling = JsTestDomPreviousSibling;
    host->childCount = JsTestDomChildCount;
    host->childAt = JsTestDomChildAt;
    host->nodeKind = JsTestDomNodeKind;
    host->nodeName = JsTestDomNodeName;
    host->textContent = JsTestDomTextContent;
    host->setTextContent = JsTestDomSetTextContent;
    host->attribute = JsTestDomAttributeGet;
    host->setAttribute = JsTestDomSetAttribute;
    host->removeAttribute = JsTestDomRemoveAttribute;
    host->attributeCount = JsTestDomAttributeCountOf;
    host->attributeNameAt = JsTestDomAttributeNameAt;
    host->appendChild = JsTestDomAppendChild;
    host->insertBefore = JsTestDomInsertBefore;
    host->removeChild = JsTestDomRemoveChild;
    host->contains = JsTestDomContains;
    host->invalidate = JsTestDomInvalidate;
}

/* ------------------------------------------------------------------ */
/* Evaluating, with the failure printed                                */
/* ------------------------------------------------------------------ */

static inline char *JsTestMessage(void)
{
    static char message[256];

    message[0] = '\0';
    return message;
}

static inline Result JsTestEvalNamed(JsRuntime *runtime,
                                     const char *name,
                                     const char *source,
                                     JsValue *out);

/** Evaluate @p source as a program. Answers the result; @p out may be NULL. */
static inline Result JsTestEval(JsRuntime *runtime, const char *source, JsValue *out)
{
    return JsTestEvalNamed(runtime, "test", source, out);
}

/**
 * `JsTestEval` with the caller's own evaluation name, which is the label a diagnostic reports.
 *
 * The name is what makes `JsRuntimeDiagnosticAt` usable from a suite: `"step-2"` says which of a
 * checkpoint's twenty evaluations failed, where `"test"` says only that one did.
 */
static inline Result JsTestEvalNamed(JsRuntime *runtime,
                                     const char *name,
                                     const char *source,
                                     JsValue *out)
{
    JsValue ignored = JsValueUndefined();
    Result status = JsRuntimeEvaluate(runtime, name, source, -1, out != NULL ? out : &ignored);

    if (status != RoboOk) {
        JsDiag diag;
        char scratch[256];
        int32_t count = JsRuntimeDiagnosticCount(runtime);

        scratch[0] = '\0';
        if (count > 0 && JsRuntimeDiagnosticAt(runtime, count - 1, &diag) == RoboOk) {
            snprintf(scratch, sizeof(scratch), " (%s at %d:%d)", diag.message, diag.line, diag.column);
        }
        TestFail(__FILE__,
                 __LINE__,
                 "evaluate failed: %s%s\n    source: %s",
                 ErrorString(status),
                 scratch,
                 source);
    }
    return status;
}

/** Evaluate and answer the numeric value of @p globalName. NaN when anything failed. */
static inline double JsTestGlobalNumber(JsRuntime *runtime, const char *source, const char *globalName)
{
    JsValue value;

    if (JsTestEval(runtime, source, NULL) != RoboOk) {
        return 0.0;
    }
    value = JsValueUndefined();
    if (JsRuntimeGetGlobal(runtime, globalName, &value) != RoboOk) {
        TestFail(__FILE__, __LINE__, "global '%s' is not defined after: %s", globalName, source);
        return 0.0;
    }
    /* The number itself is not printed: `RoboCheckEqInt` already reports both sides on failure, and a
     * note per check would bury the failures it is meant to make readable. */
    return JsValueToNumber(&value);
}

/** Evaluate and answer a global's string form (the tests' one way to read `typeof`). */
static inline const char *JsTestGlobalString(JsRuntime *runtime,
                                            const char *source,
                                            const char *globalName,
                                            char *buffer,
                                            size_t capacity)
{
    JsValue value;

    buffer[0] = '\0';
    if (JsTestEval(runtime, source, NULL) != RoboOk) {
        return buffer;
    }
    value = JsValueUndefined();
    if (JsRuntimeGetGlobal(runtime, globalName, &value) != RoboOk) {
        TestFail(__FILE__, __LINE__, "global '%s' is not defined after: %s", globalName, source);
        return buffer;
    }
    JsValueToString(&value, buffer, capacity);
    return buffer;
}

/**
 * Evaluate @p source expecting it to **fail**, and answer the status.
 *
 * A suite that means "this is refused" must not report the refusal as a failure of its own, so this
 * is `JsRuntimeEvaluate` without the `TestFail` -- the opposite of `JsTestEvalNamed`, and the door
 * every boundary case uses. The pending exception is left set for the caller to read and clear.
 */
static inline Result JsTestEvalExpectError(JsRuntime *runtime, const char *name, const char *source)
{
    JsValue ignored = JsValueUndefined();
    Result status = JsRuntimeEvaluate(runtime, name, source, -1, &ignored);

    if (status == RoboOk) {
        TestFail(__FILE__, __LINE__, "evaluate unexpectedly succeeded: %s", source);
    }
    return status;
}

/**
 * The pending exception's message, or the empty string when nothing is pending.
 *
 * A refused write is only half a fact; the other half is *why*, and this is how a suite reads it.
 * The message is copied out, so calling this does not clear the exception -- `JsRuntimeClearException`
 * is still the caller's move, which is what lets a case assert the message and then continue.
 */
static inline const char *JsTestExceptionMessage(JsRuntime *runtime, char *buffer, size_t capacity)
{
    JsValue pending = JsValueUndefined();

    if (buffer == NULL || capacity == 0u) {
        return "";
    }
    buffer[0] = '\0';
    if (JsRuntimePeekException(runtime, &pending) != RoboOk) {
        return buffer;
    }
    JsExceptionMessage(&pending, buffer, capacity);
    return buffer;
}

/** A conforming `WebIDL`-shaped runtime: deterministic clock, console captured. */
static inline JsRuntime *JsTestRuntimeCreate(JsRuntimeConfig *config)
{
    JsRuntimeConfigInit(config);
    config->deterministic = 1;
    config->enableDiagnostics = 1;
    return JsRuntimeCreate(config);
}

#endif /* ROBO_JS_TEST_SUPPORT_H */
