/*
 * RoboHTML DOM completion — creation, cloning, text and queries (Phase 17.1).
 *
 * Phase 8 built a tree a *parser* could produce and a caller could read and
 * edit; what it deliberately left out is the half of the DOM that an
 * application (or RoboJS on its behalf) needs to build a document of its own:
 *
 *   creation      document.createElement / createTextNode / createComment /
 *                 createDocumentFragment — a detached node, owned by the
 *                 document, that a mutation call then places;
 *   cloning       node.cloneNode(deep) — an independent copy, detached;
 *   text          node.textContent, read and written, with the DOM's
 *                 replace-all-children semantics for an element;
 *   placement     parent.prepend(child) — the ParentNode convenience that
 *                 Phase 8.9's four structural operations did not cover;
 *   lookup        getElementById / getElementsByTagName /
 *                 getElementsByClassName / querySelector(All).
 *
 * Everything here is built on the same registry, the same payload arena, the
 * same linking primitive and the same traversal the rest of RoboHTML uses.
 * There is no second tree and no second node identity: a created node is an
 * ordinary node whose origin is `RoboHtmlNodeOriginDom`, and once inserted it
 * is indistinguishable from a parsed one except by that origin.
 *
 * ## Detached by construction
 *
 * A created node has no parent. That is a legal state (Phase 8.9 models
 * detachment explicitly), and it is the only state an element can be created
 * in without knowing where it will go. Insertion is a separate, explicit call
 * through RoboHTMLMutation.h, exactly as the DOM's own two-step shape is.
 *
 * ## Selector matching is a seam, not a second engine
 *
 * `getElementById`, `getElementsByTagName` and `getElementsByClassName` are
 * answered here — they need only attribute reads, which this module owns.
 * `querySelector`/`querySelectorAll` are *not*, because the CSS selector
 * grammar and its matching engine live in RoboCSS (`RoboCSSMatcher.h`), and
 * RoboHTML must never depend on the layer above it. So the DOM provides the
 * *walk* and a caller installs the *matcher* (the same shape as the image
 * resolver and the anchor handler): `HtmlDocumentSetSelectorMatcher`. With no
 * matcher installed, a selector query reports `RoboNotSupported` rather than
 * guessing — an unimplemented engine must be visible, never silent.
 *
 * ## What this phase is not
 *
 * No events (a later phase), no MutationObserver, no Range/Selection, no
 * Shadow DOM, no Web Components, no `innerHTML`/`outerHTML` (which need the
 * parser — a separate phase), no CSSOM, no live JavaScript wrappers. A query
 * allocates nothing: the `…All` doors hand each match to a visitor and report
 * a count, so a caller decides whether it stores them.
 */

#ifndef ROBO_HTML_DOM_QUERY_H
#define ROBO_HTML_DOM_QUERY_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Node creation                                                       */
/* ------------------------------------------------------------------ */

/**
 * Create a detached element named @p name in @p documentId.
 *
 * The name is validated and normalized by the same rule the parser's element
 * creation uses (`HtmlElementNameValidate`), the namespace is HTML, and the
 * node's origin is `RoboHtmlNodeOriginDom`. Unlike `HtmlElementCreate` there
 * is no parent: the element is created unattached, which is what
 * `document.createElement` means and what lets a caller build a subtree before
 * placing it with one insert.
 *
 * Creation works on a *finalized* document, because a live document is exactly
 * the one an application mutates; it still refuses a document whose lifecycle
 * has failed or been destroyed, and it refuses once the node ceiling is
 * reached.
 *
 * @return RoboOk; RoboInvalidArgument (NULL @p out or @p name, empty name);
 *         RoboNotFound (unknown/destroyed document); RoboError (lifecycle
 *         refuses content); RoboOutOfMemory (registry exhausted).
 */
Result HtmlDocumentCreateElement(
    HtmlDocumentId documentId,
    const char *name,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/** Create a detached text node holding a copy of @p text (NUL-terminated). */
Result HtmlDocumentCreateTextNode(
    HtmlDocumentId documentId,
    const char *text,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/** Create a detached comment node holding a copy of @p text. */
Result HtmlDocumentCreateComment(
    HtmlDocumentId documentId,
    const char *text,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/**
 * Create a detached document fragment: a child-holding node with no payload
 * and no name. A fragment is a container and nothing else — it is never a
 * rendered element, and inserting its *children* is what a caller means (the
 * DOM's `appendChild(fragment)` inserts the fragment's children; this build
 * links the fragment itself, and a caller that wants the children inserted
 * moves them, which is stated rather than implied).
 */
Result HtmlDocumentCreateFragment(
    HtmlDocumentId documentId,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Placement: prepend                                                  */
/* ------------------------------------------------------------------ */

/**
 * Make @p child the *first* child of @p parent (the ParentNode `prepend`, for
 * one node). A parent with no children makes @p child its only child.
 *
 * Same rules as every other insertion in RoboHTMLMutation.h: @p child must be
 * detached and belong to the same document, and @p parent must be able to hold
 * children. Implemented on top of `HtmlNodeInsertBefore`/`HtmlNodeAppendChild`
 * so there is one linker, not two.
 *
 * @return as `HtmlNodeAppendChild` / `HtmlNodeInsertBefore`.
 */
Result HtmlNodePrependChild(
    const HtmlNodeHandle *parent,
    const HtmlNodeHandle *child,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Cloning                                                             */
/* ------------------------------------------------------------------ */

/**
 * Copy @p node, detached, into the same document.
 *
 * @p deep non-zero copies the whole subtree; zero copies only the node itself
 * (an element without children, a text/comment node with its content, an empty
 * fragment). An element's clone carries the same name, namespace and
 * attributes. The clone is a *new* node with a new identity: it has no parent,
 * and the original and the copy are independent from that moment on.
 *
 * The document node cannot be cloned (`RoboNotSupported`) — there is exactly
 * one document node per document and this call cannot make a second.
 *
 * @return RoboOk; RoboInvalidArgument (NULL @p out, ill-formed handle);
 *         RoboNotFound (@p node does not resolve); RoboNotSupported (the node
 *         is the document node); RoboOutOfMemory (registry exhausted).
 */
Result HtmlNodeClone(
    const HtmlNodeHandle *node,
    int deep,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* textContent                                                         */
/* ------------------------------------------------------------------ */

/**
 * Read `node.textContent` into @p out.
 *
 * For an element, fragment or the document node this is the concatenation of
 * every descendant text node in document order (comments are not text). For a
 * text or comment node it is that node's own data. The bytes are NUL-terminated
 * and truncated to @p outCap - 1; @p outLength receives the *untruncated* byte
 * length, so `outLength >= outCap` means the answer did not fit.
 *
 * @return RoboOk; RoboInvalidArgument (NULL @p out, NULL @p outLength, zero
 *         capacity); RoboNotFound (@p node does not resolve).
 */
Result HtmlNodeTextContentGet(
    const HtmlNodeHandle *node,
    char *out,
    size_t outCap,
    size_t *outLength,
    HtmlDiag *diag);

/**
 * Write `node.textContent`.
 *
 * For a text or comment node this replaces its data. For an element, fragment
 * or document node it removes every child and, when @p text is non-empty,
 * appends one text node holding it — which is the DOM's rule and the reason a
 * caller cannot end up with an element textContent that is partly old.
 *
 * A NULL @p text is refused rather than read as the empty string, because the
 * two mean different things to a caller and a sentinel is not a value.
 *
 * @return RoboOk; RoboInvalidArgument (NULL argument, ill-formed handle,
 *         @p node is the document node — its children include the root, which
 *         the 8.4 model does not let this call drop); RoboNotFound;
 *         RoboOutOfMemory.
 */
Result HtmlNodeTextContentSet(
    const HtmlNodeHandle *node,
    const char *text,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Lookup: id, tag, class                                              */
/* ------------------------------------------------------------------ */

/**
 * First element in document order whose `id` attribute equals @p id.
 *
 * The id is matched exactly (case-sensitively, as HTML ids are) against the
 * element's stored `id` attribute. @p out is cleared first and set only on
 * RoboOk.
 *
 * @return RoboOk (found); RoboNotFound (no match, or your document is gone);
 *         RoboInvalidArgument (NULL @p out or @p id); RoboNotFound for an
 *         unknown document.
 */
Result HtmlDocumentGetElementById(
    HtmlDocumentId documentId,
    const char *id,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/**
 * Collect every element whose name matches @p name, in document order.
 *
 * @p name is matched case-insensitively (ASCII); `"*"` matches every element,
 * which is the DOM's own wildcard. @p visit may be NULL, in which case the
 * walk only counts. @p outCount is optional and receives the number of matches
 * (not of elements visited). A visitor that returns anything other than
 * `RoboOk` stops the walk and that code becomes the return value.
 *
 * @return RoboOk; the visitor's code when it stopped; RoboInvalidArgument;
 *         RoboNotFound (unknown document).
 */
Result HtmlDocumentGetElementsByTagName(
    HtmlDocumentId documentId,
    const char *name,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag);

/** As above, searching only the *descendants* of @p root (an element). */
Result HtmlElementGetElementsByTagName(
    const HtmlNodeHandle *root,
    const char *name,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag);

/**
 * Collect every element whose `class` attribute contains *every* class in the
 * whitespace-separated list @p classes, in document order.
 *
 * The list is treated as a set of tokens split on ASCII whitespace; an element
 * matches when each token appears as a whole class token (so `class="a b"`
 * matches `"a"` and `"a b"` but never `"ab"`). An empty @p classes matches
 * nothing — `getElementsByClassName("")` is not a wildcard.
 *
 * @return as `HtmlDocumentGetElementsByTagName`.
 */
Result HtmlDocumentGetElementsByClassName(
    HtmlDocumentId documentId,
    const char *classes,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag);

/** As above, searching only the descendants of @p root. */
Result HtmlElementGetElementsByClassName(
    const HtmlNodeHandle *root,
    const char *classes,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Selector matching seam (RoboCSS installs the engine)                */
/* ------------------------------------------------------------------ */

/**
 * A selector matcher: answer whether @p element satisfies @p selector.
 *
 * Return non-zero for a match, 0 for a miss. The matcher is installed per
 * document and is the *only* way a selector query is answered, because the
 * engine that knows the CSS grammar lives in the module above this one. The
 * callback borrows @p element and @p selector for the duration of the call
 * only; it must not mutate the document.
 */
typedef int (*HtmlSelectorMatchFn)(const HtmlNodeHandle *element,
                                   const char *selector,
                                   size_t selectorLength,
                                   void *userdata,
                                   HtmlDiag *diag);

/**
 * Install (or clear, with NULL) the document's selector matcher.
 *
 * Unlike the mutation hook this may be replaced at any time, because a matcher
 * is a pure function of (element, selector) and carries no subscription state.
 * Until one is installed every selector query reports `RoboNotSupported`,
 * which is how "no engine here" stays visible instead of looking like a
 * document with no matching element.
 *
 * @return RoboOk; RoboInvalidArgument (NULL @p element-host document id is
 *         none); RoboNotFound (unknown/destroyed document).
 */
Result HtmlDocumentSetSelectorMatcher(
    HtmlDocumentId documentId,
    HtmlSelectorMatchFn match,
    void *userdata,
    HtmlDiag *diag);

/** Non-zero when a selector matcher is installed for @p documentId. */
int HtmlDocumentHasSelectorMatcher(HtmlDocumentId documentId);

/**
 * First element in document order satisfying @p selector, or RoboNotFound when
 * nothing matches. `document.querySelector`.
 *
 * @return RoboOk; RoboNotFound (no match, or no such document);
 *         RoboInvalidArgument (NULL args); RoboNotSupported (no matcher
 *         installed).
 */
Result HtmlDocumentQuerySelector(
    HtmlDocumentId documentId,
    const char *selector,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/** As above but searching only the descendants of @p root. */
Result HtmlElementQuerySelector(
    const HtmlNodeHandle *root,
    const char *selector,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/**
 * Visit every element in document order satisfying @p selector.
 *
 * The static-list semantics of `querySelectorAll` are met with the walker
 * this module already has: each match is handed to @p visit in document order
 * and @p outCount reports how many there were. A visitor may stop the walk by
 * returning a non-RoboOk code, which becomes the result.
 *
 * @return as `HtmlDocumentGetElementsByTagName`, plus RoboNotSupported when no
 *         matcher is installed.
 */
Result HtmlDocumentQuerySelectorAll(
    HtmlDocumentId documentId,
    const char *selector,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag);

/** As above but searching only the descendants of @p root. */
Result HtmlElementQuerySelectorAll(
    const HtmlNodeHandle *root,
    const char *selector,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag);

/**
 * Capability snapshot of the Phase 17.1 DOM-completion surface.
 *
 * Every field is what this build actually does, not what the DOM specification
 * says: `selectorEngine` is 0 because the engine lives in RoboCSS and this
 * module only holds the seam, and `innerHtml` is 0 because serialization needs
 * the parser.
 */
typedef struct HtmlDomCompletionCapabilities {
    int creation;        /* createElement / createTextNode / createComment / fragment */
    int cloning;         /* cloneNode, deep and shallow */
    int textContent;     /* read and write */
    int prepend;         /* parent.prepend(child) */
    int lookupById;      /* getElementById */
    int lookupByTag;     /* getElementsByTagName */
    int lookupByClass;   /* getElementsByClassName */
    int selectorSeam;    /* querySelector/All route to an installed matcher */
    int selectorEngine;  /* a matcher ships *in this module*: always 0 by design */
    int innerHtml;       /* innerHTML/outerHTML: 0 (needs the parser) */
} HtmlDomCompletionCapabilities;

/** Fill @p out with the capabilities above. RoboInvalidArgument on NULL. */
Result HtmlDomCompletionGetCapabilities(HtmlDomCompletionCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_DOM_QUERY_H */
