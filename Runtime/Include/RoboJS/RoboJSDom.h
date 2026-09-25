/*
 * RoboJS DOM bindings (Phase 16.10) — `document`, `window`, elements and text, over **the DOM that
 * already exists**.
 *
 * ## There is no second DOM here
 *
 * RoboJS does not store a tree. It stores *wrappers*: one `Host` object per host node, interned in a
 * table keyed by the host's node id, whose prototype carries the methods. Every question a script
 * asks — what is its `id`, what text is inside it, what are its children — is answered by calling the
 * host. That is what makes "JavaScript manipulates the real RoboHTML DOM" a fact rather than an
 * aspiration, and it is why a DOM mutation a script performs goes through the host's own mutation
 * door and therefore through whatever invalidation the host already hooked up (RoboHTML's
 * tree-mutated hook, in this tree).
 *
 * ## The seam is a vtable, so the engine does not include HTML
 *
 * `JsDomHost` is eight fields of function pointers and a `userdata`. `RoboJSDomHtml.h` is the one
 * file in this module that includes a RoboHTML header, and it implements this vtable over
 * `HtmlDocumentId`; a host test implements the same vtable over a fixture. That is the same
 * discipline `RoboPaintDispatch.c` follows for RoboInput: the *one* file that admits the dependency
 * is named, small, and replaceable.
 *
 * ## What is bound
 *
 * `document.getElementById`, `querySelector`, `querySelectorAll`, `getElementsByTagName`,
 * `createElement`, `createTextNode`, `documentElement`, `body`, plus the `document`/`window` globals
 * (`window` **is** the global object, so `window.foo = 1` and `foo` are one thing);
 * on a node: `id`, `className`, `tagName`, `nodeName`, `nodeType`, `textContent` (read and write),
 * `parentNode`, `firstChild`, `lastChild`, `nextSibling`, `previousSibling`, `childNodes`,
 * `children`, `childElementCount`, `appendChild`, `insertBefore`, `removeChild`,
 * `setAttribute`, `getAttribute`, `removeAttribute`, `hasAttribute`, `attributes` (as a name/value
 * list), `contains`, and the three event doors from 16.11;
 * on an element: `style` as an object with `setProperty`/`getPropertyValue`/`removeProperty` that
 * writes the **real** `style` attribute.
 *
 * ## What is deliberately not bound, by name
 *
 * `innerHTML` and `outerHTML`: there is no fragment parser and no serializer in this build, and a
 * setter that stored an unparsed string would be a lie about the DOM. `children`/`childNodes` are
 * arrays built on demand (a live `NodeList` is a later phase). `classList` is not here — `className`
 * is a string, and a token list is a separate object model. `style` is **not** a CSSOM: it is a write
 * door to one attribute, and computed style stays RoboCSS's. `getBoundingClientRect` and
 * `offsetWidth` are layout queries, and layout is not this module's (`Documentation/RoboJS/
 * DOMBindings.md` names the seam they would need).
 *
 * ## Selectors
 *
 * `querySelector`/`querySelectorAll`/`matches` support a deliberately small grammar — a comma list of
 * compound selectors over `tag`, `#id`, `.class` and `*`, with no combinators, no attribute
 * selectors, no pseudo-classes. Matching walks the host's tree through this vtable. An unsupported
 * selector is a located `SyntaxError` from the binding rather than a silent no-match.
 */

#ifndef ROBO_JS_DOM_H
#define ROBO_JS_DOM_H

#include "RoboJS/RoboJSTypes.h"
#include "RoboJS/RoboJSValue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A host node. `0` means "no node"; every door answers it rather than failing. */
typedef uint64_t JsDomNodeId;

/** What a host node is. A host that answers `None` for a live id is a host bug, not a script's. */
typedef enum JsDomNodeKind {
    RoboJsDomNodeNone = 0,
    RoboJsDomNodeDocument = 1,
    RoboJsDomNodeElement = 2,
    RoboJsDomNodeText = 3
} JsDomNodeKind;

/**
 * The host interface: everything RoboJS needs to know about a DOM, and nothing about how it stores
 * one. Every door must tolerate a `0` node id and answer the empty case (`0`, `-1`, or `RoboNotFound`)
 * rather than crashing — a script that kept a reference to a removed node will ask.
 *
 * A door that answers a length (`nodeName`, `textContent`, `attributeAn`) writes into the caller's
 * buffer, NUL-terminates when it can, and **answers the bytes a complete answer needs**, so a caller
 * can size a second attempt. `attribute` answers `-1` for "the attribute does not exist".
 */
typedef struct JsDomHost {
    void *userdata;

    /** 1 when the underlying document is still alive. A destroyed document's scripts must not be
     *  able to mutate a freed tree, so every mutating door is refused when this answers 0. */
    int (*isLive)(void *userdata);

    JsDomNodeId (*documentRoot)(void *userdata);
    JsDomNodeId (*documentBody)(void *userdata);
    /** First element in the document whose `id` attribute is @p id. */
    JsDomNodeId (*findById)(void *userdata, const char *id);
    /** First descendant element of @p root (exclusive) whose tag name is @p tag. */
    JsDomNodeId (*findByTag)(void *userdata, JsDomNodeId root, const char *tag);

    JsDomNodeId (*createElement)(void *userdata, const char *tag);
    JsDomNodeId (*createTextNode)(void *userdata, const char *text, int32_t length);

    JsDomNodeId (*parent)(void *userdata, JsDomNodeId node);
    JsDomNodeId (*firstChild)(void *userdata, JsDomNodeId node);
    JsDomNodeId (*lastChild)(void *userdata, JsDomNodeId node);
    JsDomNodeId (*nextSibling)(void *userdata, JsDomNodeId node);
    JsDomNodeId (*previousSibling)(void *userdata, JsDomNodeId node);
    int32_t (*childCount)(void *userdata, JsDomNodeId node);
    JsDomNodeId (*childAt)(void *userdata, JsDomNodeId node, int32_t index);

    uint32_t (*nodeKind)(void *userdata, JsDomNodeId node);
    /** A tag name for an element (`"div"`), `"#text"` for a text node, `"#document"` for the root. */
    int32_t (*nodeName)(void *userdata, JsDomNodeId node, char *buffer, int32_t capacity);
    int32_t (*textContent)(void *userdata, JsDomNodeId node, char *buffer, int32_t capacity);
    Result (*setTextContent)(void *userdata,
                             JsDomNodeId node,
                             const char *text,
                             int32_t length);

    int32_t (*attribute)(void *userdata,
                         JsDomNodeId node,
                         const char *name,
                         char *buffer,
                         int32_t capacity);
    Result (*setAttribute)(void *userdata,
                           JsDomNodeId node,
                           const char *name,
                           const char *value);
    Result (*removeAttribute)(void *userdata, JsDomNodeId node, const char *name);
    int32_t (*attributeCount)(void *userdata, JsDomNodeId node);
    /** The @p index'th attribute's name, in the host's own deterministic order. */
    int32_t (*attributeNameAt)(void *userdata,
                               JsDomNodeId node,
                               int32_t index,
                               char *buffer,
                               int32_t capacity);

    Result (*appendChild)(void *userdata, JsDomNodeId parent, JsDomNodeId child);
    Result (*insertBefore)(void *userdata,
                           JsDomNodeId parent,
                           JsDomNodeId child,
                           JsDomNodeId before);
    Result (*removeChild)(void *userdata, JsDomNodeId parent, JsDomNodeId child);

    /** 1 when @p node is @p ancestor or a descendant of it. */
    int (*contains)(void *userdata, JsDomNodeId ancestor, JsDomNodeId node);

    /**
     * Mark @p node dirty after a change the host's own mutation hook did not observe. A host that
     * has its own hook (RoboHTML's) may leave this NULL or make it a no-op.
     */
    void (*invalidate)(void *userdata, JsDomNodeId node);
} JsDomHost;

/**
 * Install @p host and the DOM globals on @p runtime. The struct is **copied**, so a caller may build
 * it on the stack. Idempotent for the same host; installing over a different host replaces it
 * (the wrapper table and the document object are rebuilt, because they belong to the document that
 * went away).
 *
 * @return RoboOk; RoboInvalidArgument for a NULL runtime/host or a host missing a required door
 *         (`documentRoot`, `nodeKind`, `nodeName`, `textContent`, `setTextContent`, `attribute`,
 *         `setAttribute`, `appendChild`).
 */
Result JsDomInstall(JsRuntime *runtime, const JsDomHost *host);

/** Remove the DOM bindings: the wrappers, the prototypes' node methods and the `document` global.
 *  Answers the number of wrappers dropped. */
int32_t JsDomUninstall(JsRuntime *runtime);

int JsDomIsInstalled(JsRuntime *runtime);
const JsDomHost *JsDomHostOf(JsRuntime *runtime);

/** The interned wrapper for @p node, created (and its prototype attached) on first ask. NULL when
 *  the runtime has no DOM, or when the node id is 0. */
JsObject *JsDomWrapperForNode(JsRuntime *runtime, JsDomNodeId node);

/** The node a wrapper value refers to. `RoboInvalidArgument` when the value is not a DOM wrapper. */
Result JsDomNodeOfValue(const JsValue *value, JsDomNodeId *outNode);

/** The `document` object (a wrapper for the host's document root), or NULL when no DOM is installed. */
JsObject *JsDomDocumentObject(JsRuntime *runtime);

/** What the host says @p node is; `RoboJsDomNodeNone` when the runtime has no DOM. */
uint32_t JsDomNodeKindOf(JsRuntime *runtime, JsDomNodeId node);

/** 1 when @p node is a live node of the installed host. */
int JsDomNodeIsLive(JsRuntime *runtime, JsDomNodeId node);

/** Wrappers interned so far. */
int32_t JsDomWrapperCount(JsRuntime *runtime);

/**
 * 1 when @p selector matches @p node. The grammar is in this header's comment; a selector this build
 * cannot parse answers 0 and, when @p error is non-NULL, an `errBuf` with the reason.
 */
int JsDomSelectorMatches(JsRuntime *runtime,
                         JsDomNodeId node,
                         const char *selector,
                         char *errBuf,
                         int32_t errCap);

/**
 * 1 when @p selector is inside the supported grammar. @p errBuf/`errCap` receive the reason when it is
 * not — the door the binding uses to turn a bad selector into a located `SyntaxError` **before** it
 * walks the tree.
 */
int JsDomSelectorValidate(const char *selector, char *errBuf, int32_t errCap);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_DOM_H */
