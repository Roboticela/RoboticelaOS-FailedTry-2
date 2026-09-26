/*
 * RoboHTML Shadow DOM (Phase 17.8) — the `open` foundation.
 *
 * A shadow root is a second tree attached to an element, and the whole point
 * is what it is *not*: it is not a child.
 *
 *   <div id="host">                  ← light tree: the host and its children
 *        ├── (light) child nodes     ← `host.childNodes`, the light DOM
 *        └── shadowRoot              ← reachable only through
 *              └── shadow children     HtmlElementShadowRoot(host)
 *
 * ## The representation, and why it is this small
 *
 * A shadow root is a `RoboHtmlNodeKindFragment` node — the same kind 17.7's
 * template content uses — plus one per-document table entry saying which host
 * it belongs to and what mode it was attached with. Three consequences fall
 * out of that choice rather than being implemented:
 *
 *   - **Light children are untouched.** The host keeps its own children; the
 *     shadow root is never parented, so `host.childNodes` cannot accidentally
 *     contain it and `HtmlNodeAppendChild` cannot create the two-tree anomaly.
 *   - **A boundary is a fact about reachability.** Document lookup walks from
 *     the document root, and a parentless shadow root is not on any path from
 *     it — so `getElementById`, `getElementsByTagName`,
 *     `getElementsByClassName` and every traversal from the root stop at the
 *     host, exactly as they stop at a template's content (17.7). That is the
 *     CSS/scoping boundary this phase can honestly claim: a query scoped to
 *     the *host* (`HtmlElementGetElementsByTagName`) does not descend into the
 *     shadow tree, and a query scoped to the shadow root sees only its own
 *     subtree.
 *   - **There is no second rendering tree.** The shadow root's nodes are
 *     ordinary nodes in the same document, drawn by whoever consumes the DOM.
 *     RoboHTML holds no renderer and adds none.
 *
 * ## Retargeting is the event boundary, and it is one rule
 *
 * With no DOM event system yet, "basic event boundary behavior" is the one
 * testable rule that matters: a target inside a shadow tree is reported to the
 * outside as the **host**. `HtmlShadowRootRetargetEvent` is that rule, and
 * `HtmlShadowRootRetargetFocus` the same crossing for focus. Both are honest
 * about being the *rule* rather than a dispatch: no event is created, queued
 * or delivered here.
 *
 * ## What this is NOT, named rather than implied
 *
 * `mode: "closed"` is refused rather than accepted-and-ignored: a closed root
 * that is still readable through every API here would be a lie, so the refusal
 * is the honest answer until encapsulation is real. Also absent: slotting and
 * `<slot>` assignment, the flat tree, `::slotted`, `:host`/`:host()`,
 * declarative shadow DOM (17.7's `shadowrootmode`), shadow-root *attachment* to
 * a template's content by the parser (a template's content is not a shadow
 * root), the host-candidate element list (the spec restricts which elements
 * may host; this build allows any element and says so), focus navigation
 * *within* a shadow tree, `ShadowRoot.mode`/`delegatesFocus` beyond the one
 * mode, and adoption of a shadow root between documents.
 */

#ifndef ROBO_HTML_SHADOW_H
#define ROBO_HTML_SHADOW_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The attachment mode. Only OPEN exists in this build — see the header
 * comment for why CLOSED is not spelled here: an enumerator that this runtime
 * refuses is a promise it does not keep, so the refusal is a Result.
 */
typedef enum HtmlShadowMode {
    /** `attachShadow({mode: "open"})`. */
    RoboHtmlShadowModeOpen = 1
} HtmlShadowMode;

/** Non-zero if @p mode is a defined enumerator. */
int HtmlShadowModeIsKnown(HtmlShadowMode mode);

/** Stable lowercase name of @p mode; never NULL, never allocated. */
const char *HtmlShadowModeName(HtmlShadowMode mode);

/**
 * Attach an `open` shadow root to @p element and return it.
 *
 * @p out is borrowed: the root is a registered node owned by the document, and
 * it stays valid for the document's life. It has **no parent**, which is the
 * representation of "this is a tree of its own".
 *
 * @return RoboOk; RoboInvalidArgument (NULL argument); RoboNotFound (not a
 *         live element, or its document is gone); RoboNotSupported (the node
 *         is not an element, or @p mode is not OPEN);
 *         RoboAlreadyExists (the element already has a shadow root — a host
 *         has at most one, and replacing it silently would orphan whatever a
 *         caller already held); RoboOutOfMemory.
 */
Result HtmlElementAttachShadow(
    const HtmlNodeHandle *element,
    HtmlShadowMode mode,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/**
 * @p element's shadow root.
 *
 * @return RoboOk with @p out set; RoboNotFound (the element has no shadow
 *         root, does not resolve, or is not an element);
 *         RoboInvalidArgument (NULL argument).
 */
Result HtmlElementShadowRoot(
    const HtmlNodeHandle *element,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/**
 * The host of shadow root @p root — the inverse of
 * HtmlElementShadowRoot, so a walker that arrives at a shadow tree from
 * inside can get back out to the light tree.
 *
 * @return RoboOk with @p out set; RoboNotFound (not a shadow root);
 *         RoboInvalidArgument.
 */
Result HtmlShadowRootHost(
    const HtmlNodeHandle *root,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/** The mode @p root was attached with. RoboNotFound when not a shadow root. */
Result HtmlShadowRootMode(
    const HtmlNodeHandle *root,
    HtmlShadowMode *out,
    HtmlDiag *diag);

/** Non-zero in @p outIsHost when @p element has a shadow root. */
Result HtmlElementIsShadowHost(
    const HtmlNodeHandle *element,
    int *outIsHost,
    HtmlDiag *diag);

/** Non-zero in @p outIsRoot when @p node is an attached shadow root. */
Result HtmlNodeIsShadowRoot(
    const HtmlNodeHandle *node,
    int *outIsRoot,
    HtmlDiag *diag);

/**
 * The event boundary: the node an event originating at @p target is reported
 * as, once it has crossed out of @p root's shadow tree.
 *
 * The rule is one step — a target inside the shadow root is seen from outside
 * as the **host** — because that is the whole boundary. A target that is
 * @p root or outside it is reported unchanged, and a target outside @p root is
 * not a boundary crossing at all.
 *
 * @return RoboOk with @p outRetargeted set; RoboNotFound (@p root is not a
 *         live shadow root, or @p target does not resolve);
 *         RoboInvalidArgument.
 */
Result HtmlShadowRootRetargetEvent(
    const HtmlNodeHandle *root,
    const HtmlNodeHandle *target,
    HtmlNodeHandle *outRetargeted,
    HtmlDiag *diag);

/**
 * The same crossing for focus: a focus request for @p target inside
 * @p root's tree is reported as the host.
 *
 * This is the *boundary* rule and not the focus-navigation algorithm: moving
 * focus *within* a shadow tree, `delegatesFocus`, and tab order across a
 * boundary are not implemented here and are named as absent in the header
 * comment.
 *
 * @return as HtmlShadowRootRetargetEvent.
 */
Result HtmlShadowRootRetargetFocus(
    const HtmlNodeHandle *root,
    const HtmlNodeHandle *target,
    HtmlNodeHandle *outRetargeted,
    HtmlDiag *diag);

/**
 * Non-zero in @p outReachable when @p node is reachable from the document root
 * — i.e. it is in the light tree rather than inside a shadow root (or any
 * other detached tree).
 *
 * This is the question every bounded walk answers implicitly, exposed so a
 * caller can ask it directly instead of inferring it from a miss.
 *
 * @return RoboOk; RoboInvalidArgument; RoboNotFound (no live document).
 */
Result HtmlNodeIsReachableFromDocument(
    const HtmlNodeHandle *node,
    int *outReachable,
    HtmlDiag *diag);

/**
 * What this surface actually does, field by field.
 *
 * The zeros are the phase's stated non-goals — every one of them is also
 * named in the tests, so an absence here is a checked absence rather than
 * prose.
 */
typedef struct HtmlShadowCapabilities {
    int attachShadow;      /* open mode */
    int shadowRootQuery;   /* element → root */
    int hostQuery;         /* root → host */
    int lightDomPreserved; /* the host's own children are untouched */
    int boundaryLookup;    /* document lookup does not enter a shadow tree */
    int scopedLookup;      /* a host-scoped query does not enter it either */
    int eventRetargeting;  /* inner target → host */
    int focusBoundary;     /* the same crossing for focus */
    int reachabilityQuery; /* light tree vs detached tree */
    int closedMode;        /* 0: refused, not accepted-and-ignored */
    int slotting;          /* 0: no slots, no flat tree */
    int hostSelectors;     /* 0: no `:host` / `::slotted` */
    int declarativeShadowDom; /* 0: no `shadowrootmode` attribute */
    int secondRenderingTree;  /* 0: shadow nodes are ordinary nodes */
    int hostCandidates;    /* 0: the spec's host-candidate list is not enforced */
    const char *version;   /* RoboHTML's version string */
} HtmlShadowCapabilities;

/** Fill @p out. RoboInvalidArgument on NULL. */
Result HtmlShadowGetCapabilities(HtmlShadowCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_SHADOW_H */
