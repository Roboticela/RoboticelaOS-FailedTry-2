/*
 * RoboHTML anchors and navigation (Phase 8.13).
 *
 * 8.11 gave `a` a tag identity and a text-level role, so the parser was
 * already building anchors before this phase began. What was missing is
 * everything after that: an `<a>` is only interesting because of what its
 * `href` *names*, and this header is that name's model.
 *
 *   <a href="/Settings" target="_self" rel="noopener" download>Settings</a>
 *        │
 *        ├── attributes   HtmlElementAnchorAttributes
 *        │      href      what to navigate to
 *        │      target    which context should receive it
 *        │      rel       preserved; queryable, never interpreted here
 *        │      download  a save *intent*, not a save
 *        │      title     advisory text, kept verbatim
 *        │
 *        ├── target       HtmlNavigationTarget  (owned, resolved)
 *        │      kind      NONE │ ROUTE │ FRAGMENT │ EXTERNAL │ INVALID
 *        │      resolved  the normalized path, or the URI verbatim
 *        │      fragment  the `#id` part, if there was one
 *        │      scheme    the URI scheme, if there was one
 *        │
 *        └── activation   HtmlElementActivate
 *               └── request  HtmlNavigationRequest
 *                     └── handler   the delegated-execution seam
 *                           └── outcome  HtmlNavigationOutcome
 *
 * RoboticelaOS is an application runtime, not a browser
 *   There is no origin, no cookie jar, no session, no tab strip and no
 *   same-origin policy in this model, because there is nothing here that
 *   would use them. A document belongs to an application; navigation is a
 *   request that the application's own router either takes or does not.
 *   What a browser would express through cookies and sessions, RoboticelaOS
 *   expresses through the application identity and the capability model it
 *   already has — so this phase adds no security model of its own and
 *   invents no privileged bypass. It refuses, and says why.
 *
 * Nothing here performs I/O
 *   The 8.1 boundary said RoboHTML never acquires bytes, and 8.12's image
 *   model kept it. This is the same shape one level up: a caller installs a
 *   HtmlNavigationHandler, and *that* decides what an accepted
 *   route means. Two consequences are load-bearing:
 *
 *     - an activation never reports success for navigation that did not
 *       happen. With no handler installed, a route is UNSUPPORTED, which is
 *       the truth: this runtime cannot perform that navigation yet.
 *     - a scheme the runtime does not permit never reaches the handler at
 *       all. `javascript:`, `file:` and `data:` are refused by policy before
 *       the seam is consulted, so an anchor cannot be used to reach a
 *       subsystem the document was never granted.
 *
 * Fragments resolve locally, because they need no I/O
 *   `#Section` names an element in this document, and finding one is a tree
 *   walk — so a fragment activation is answered here rather than delegated,
 *   and reports FRAGMENT or FRAGMENT_MISSING. That is the one navigation
 *   this phase can complete honestly.
 *
 * What this phase is NOT: CSS, link styling as a permanent rule, layout,
 * click/event DOM APIs, the History API, Fetch, HTTP, WebSocket, cookies,
 * CORS, service workers, a download manager, browser tabs or windows, or a
 * native protocol launcher. The demo adapter paints a link distinguishably
 * and hit-tests it so the chain can be seen end to end; that is temporary
 * adapter behaviour, not a style engine.
 *
 * See html/docs/Links.md.
 */

#ifndef ROBO_HTML_ANCHOR_H
#define ROBO_HTML_ANCHOR_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Anchor attributes (no resolution, no navigation)                    */
/* ------------------------------------------------------------------ */

/**
 * The navigation-relevant attributes of one element, read verbatim.
 *
 * Every string is borrowed from the element and stays valid only while the
 * element's attributes are unchanged — the same rule as
 * HtmlElementAttributeValue, which this is a typed view over.
 *
 * `present` and `usable` are separate facts and the split is the point:
 * `present` means the attribute is there at all (so `<a download>` is
 * present with a zero-length name), and `usable` means it names something
 * this runtime can act on (non-empty after surrounding ASCII whitespace is
 * ignored). That is what lets a caller tell `<a href>` from `<a href="">`
 * from `<a href="   ">` from `<a>` without guessing.
 *
 * Nothing is decoded, resolved, trimmed-in-place or normalized. `href` is
 * the bytes the document wrote; resolution happens in
 * HtmlNavigationTargetCreate and nowhere else.
 */
typedef struct HtmlAnchorAttributes {
    /** Non-zero when an `href` attribute record exists. */
    int hrefPresent;
    /** Borrowed `href` bytes; NULL when absent. */
    const char *href;
    size_t hrefLength;
    /** Non-zero when `href` is present and names something (non-empty). */
    int hrefUsable;

    /** Non-zero when a `target` attribute record exists. */
    int targetPresent;
    /** Borrowed `target` bytes; NULL when absent. */
    const char *target;
    size_t targetLength;

    int relPresent;
    const char *rel;
    size_t relLength;

    int downloadPresent;
    /** Borrowed `download` bytes; "" (length 0) for a bare `<a download>`. */
    const char *download;
    size_t downloadLength;

    int titlePresent;
    const char *title;
    size_t titleLength;
} HtmlAnchorAttributes;

/**
 * Read the anchor attributes of @p element.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element / @p out;
 *         RoboNotFound when @p element is not a live element;
 *         RoboNotSupported when the element is not an `<a>`. That last
 *         case is a refusal rather than an empty view on purpose: reporting
 *         "no href" for a `<p>` would let a caller mistake "not a link" for
 *         "a link with nothing to do".
 */
Result HtmlElementAnchorAttributes(
    const HtmlNodeHandle *element,
    HtmlAnchorAttributes *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* The navigation target                                               */
/* ------------------------------------------------------------------ */

/**
 * What a reference turned out to be.
 *
 * The kinds describe the *reference*, not what the runtime will do with it —
 * executing an EXTERNAL target is a later phase's job and this phase never
 * claims otherwise.
 */
typedef enum HtmlTargetKind {
    /** No usable reference: the attribute is absent, empty or whitespace. */
    RoboHtmlTargetNone = 0,
    /** An application-internal path, resolved against the document's own. */
    RoboHtmlTargetRoute = 1,
    /** A fragment on the current document: `#Section`. */
    RoboHtmlTargetFragment = 2,
    /** An absolute URI with a scheme: `https:`, `mailto:`, `tel:`, ... */
    RoboHtmlTargetExternal = 3,
    /** A reference that cannot be made into a target at all. */
    RoboHtmlTargetInvalid = 4
} HtmlTargetKind;

/** Non-zero if @p kind is a defined enumerator. */
int HtmlTargetKindIsKnown(HtmlTargetKind kind);

/** Stable lowercase name of @p kind; never NULL, never allocated. */
const char *HtmlTargetKindName(HtmlTargetKind kind);

/**
 * Longest `href` this model accepts, in bytes.
 *
 * A reference longer than this is INVALID rather than truncated or
 * allocated: an unbounded attribute value should be refused at the boundary,
 * not carried into a resolution that has to size a buffer from it. 4 KiB is
 * far beyond any route a real application uses and small enough that a
 * hostile document cannot make resolution expensive.
 */
#define RoboHtmlTargetMaxReference 4096u

/**
 * A resolved reference. Owned; release with HtmlNavigationTargetDestroy.
 *
 * The strings it exposes are borrowed from the target and live exactly as
 * long as it does. They are NUL-terminated as well as length-counted, so a
 * caller can pass them to a C API without copying, but the length is the
 * authoritative size.
 */
typedef struct HtmlNavigationTarget HtmlNavigationTarget;

/**
 * Parse and resolve @p href against @p base.
 *
 * @p base is the document's own reference — the source name the document was
 * built from, as HtmlDocumentSourceName returns. It supplies the
 * directory a relative reference resolves against; NULL or empty means "no
 * base", in which case `x` stays `x` and `../x` is clamped to `x` rather
 * than refused, because a document with no name is a normal in-memory
 * document and not an error.
 *
 * Resolution is deliberately small, and deliberately total:
 *
 *   /Path          root-relative   → resolved "/Path"
 *   Path           relative       → resolved "<base dir>/Path"
 *   ./Path         relative       → same
 *   ../Path        relative       → one segment popped from the base dir
 *   Path#Fragment  route + fragment
 *   #Fragment      fragment on the current document
 *   scheme:rest    absolute URI, carried verbatim in `resolved`
 *
 * A `..` that would escape above the root is clamped to the root, matching
 * what a URL does; it is not an error. A query string is carried verbatim in
 * `resolved` and is not broken out into a field of its own, because nothing
 * in this phase reads it. No percent-encoding, no case folding of the path
 * and no IDNA: those belong to a URL library, and half-implementing them
 * here would be a behavioural claim this phase cannot back.
 *
 * Failure is a *kind*, not an error: an unusable reference yields RoboOk
 * with kind NONE, and an unparseable one RoboOk with kind INVALID.
 *
 * @return RoboOk and a target in @p out (which can be NONE or INVALID);
 *         RoboInvalidArgument for NULL @p href / @p out, or @p href NULL
 *         with non-zero @p hrefLength; RoboOutOfMemory.
 */
Result HtmlNavigationTargetCreate(
    const char *href,
    size_t hrefLength,
    const char *base,
    size_t baseLength,
    HtmlNavigationTarget **out,
    HtmlDiag *diag);

/**
 * Resolve @p element's `href` against its own document's reference.
 *
 * @return RoboOk and a target in @p out — always a target, even for an
 *         anchor with no `href` (kind NONE), because "this link points
 *         nowhere" is an answer; RoboInvalidArgument; RoboNotFound
 *         (not a live element, or its document is gone); RoboNotSupported
 *         (not an `<a>`); RoboOutOfMemory.
 */
Result HtmlElementNavigationTarget(
    const HtmlNodeHandle *element,
    HtmlNavigationTarget **out,
    HtmlDiag *diag);

/** Release @p target and everything it owns. Safe on NULL. */
void HtmlNavigationTargetDestroy(HtmlNavigationTarget *target);

/** What the reference turned out to be; NONE for a NULL target. */
HtmlTargetKind HtmlNavigationTargetKind(
    const HtmlNavigationTarget *target);

/**
 * The reference exactly as the document wrote it, or NULL for a NULL target
 * or a NONE kind. @p outLength (optional) receives the byte count.
 */
const char *HtmlNavigationTargetReference(
    const HtmlNavigationTarget *target,
    size_t *outLength);

/**
 * The resolved form: the normalized path for a ROUTE, the URI verbatim for
 * an EXTERNAL target, the base reference for a FRAGMENT. NULL when there is
 * nothing to resolve to (NONE, INVALID).
 */
const char *HtmlNavigationTargetResolved(
    const HtmlNavigationTarget *target,
    size_t *outLength);

/**
 * The `#id` part with the `#` removed, or NULL when the reference had none.
 *
 * An *empty* fragment is a present fragment of length 0 — `href="#"`
 * navigates to the top of the target — so NULL versus "" matters here for
 * the same reason it does everywhere else in this runtime.
 */
const char *HtmlNavigationTargetFragment(
    const HtmlNavigationTarget *target,
    size_t *outLength);

/** The lowercased scheme, or NULL when the reference had none. */
const char *HtmlNavigationTargetScheme(
    const HtmlNavigationTarget *target,
    size_t *outLength);

/** Non-zero when the kind is ROUTE or FRAGMENT: stays inside this runtime. */
int HtmlNavigationTargetIsInternal(
    const HtmlNavigationTarget *target);

/* ------------------------------------------------------------------ */
/* Scheme policy                                                       */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p scheme is one this runtime can *name*.
 *
 * Knowing a scheme is not permission to use it — see
 * HtmlNavigationSchemeIsPermitted. The two questions are separate
 * because they answer different things: `file` is known and not permitted
 * ("we understand what you asked for and we refuse it"), while `weird` is
 * neither ("this runtime has never heard of that").
 */
int HtmlNavigationSchemeIsKnown(const char *scheme, size_t length);

/**
 * Non-zero when @p scheme may be handed to a navigation handler at all.
 *
 * Permitted today: `http`, `https`, `mailto` and `tel`. Everything else is
 * refused before the handler is consulted, and that refusal is the phase's
 * central security statement rather than an unimplemented case:
 *
 *   javascript:  never reaches a handler — an anchor must not run script
 *   data:        never reaches a handler — the runtime does not decode it
 *   file:        never reaches a handler — a document has no filesystem
 *                capability, and an attribute cannot grant one
 *   anything else   unrecognised, so there is nothing to delegate to
 *
 * Permitted does not mean reachable: with no handler installed, even `https`
 * reports UNSUPPORTED, because the stack that would serve it is a later
 * phase. This function only says "the seam may see it".
 */
int HtmlNavigationSchemeIsPermitted(const char *scheme,
                                             size_t length);

/* ------------------------------------------------------------------ */
/* Fragment lookup                                                     */
/* ------------------------------------------------------------------ */

/**
 * Find the element in @p documentId whose `id` attribute is exactly
 * @p fragment, in document order.
 *
 * This is the one navigation this phase can complete, and it is deliberately
 * named for its caller. It is not `getElementById`: the id lookup every DOM
 * query will need is a later phase's API, with its own name, its own
 * caching question and its own error surface. What exists here is what a
 * fragment activation needs — first match, document order, exact
 * case-sensitive comparison, id *attribute* not node identity.
 *
 * @return RoboOk and the element in @p out; RoboInvalidArgument for NULL
 *         @p out; RoboNotFound when nothing matches — including for an
 *         empty fragment, which by definition names the top of the document
 *         and therefore names no element; RoboError when the walk could not
 *         be trusted (see the traversal walkers).
 */
Result HtmlNavigationFindFragment(
    HtmlDocumentId documentId,
    const char *fragment,
    size_t fragmentLength,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Link context (`target`) and `rel`                                   */
/* ------------------------------------------------------------------ */

/**
 * Which context an anchor wants its target delivered to.
 *
 * Recognised, not implemented. RoboticelaOS has applications and windows,
 * not a tab strip, so `_blank` does not create anything: it is a distinct
 * context that a future window/application layer may act on, and reporting
 * it as CURRENT would throw away the information that the document asked
 * for a new context. The value is preserved and classified; that is all this
 * phase claims.
 */
typedef enum HtmlLinkTargetContext {
    /** Absent, empty, or `_self`: this application's current context. */
    RoboHtmlLinkTargetContextCurrent = 0,
    /** Any other name: a named context the application may recognise. */
    RoboHtmlLinkTargetContextNamed = 1,
    /** `_blank`: a new context. Deliberately *not* a new tab or window. */
    RoboHtmlLinkTargetContextNew = 2,
    /** `_parent` / `_top`: an enclosing context. */
    RoboHtmlLinkTargetContextParent = 3,
    RoboHtmlLinkTargetContextTop = 4,
    /** A reserved `_`-prefixed name this runtime does not define. */
    RoboHtmlLinkTargetContextUnknown = 5
} HtmlLinkTargetContext;

/** Non-zero if @p context is a defined enumerator. */
int HtmlLinkTargetContextIsKnown(HtmlLinkTargetContext c);

/** Stable lowercase name of @p context; never NULL, never allocated. */
const char *HtmlLinkTargetContextName(
    HtmlLinkTargetContext context);

/**
 * Classify a `target` attribute value. A NULL value is CURRENT (absent), and
 * matching is ASCII case-insensitive because HTML keyword matching is.
 */
HtmlLinkTargetContext HtmlLinkTargetContextFor(
    const char *target,
    size_t length);

/**
 * Non-zero when @p rel contains @p token as a whole ASCII-case-insensitive
 * space-separated token.
 *
 * The whole of this phase's `rel` behaviour: *preserve and answer*, never
 * interpret. `nofollow`, `noopener`, `noreferrer` and `external` all change
 * what a browser's network layer does and mean nothing to a runtime with no
 * network, so inventing consequences for them here would be inventing
 * behaviour. Answering "does this link ask for X?" is useful, honest and
 * complete; `rel` travels on the request so a later phase can act on it
 * without this API changing.
 */
int HtmlLinkRelContainsToken(const char *rel, size_t relLength,
                                      const char *token);

/* ------------------------------------------------------------------ */
/* Navigation request, handler seam, outcome                           */
/* ------------------------------------------------------------------ */

/** Why an activation was requested. */
typedef enum HtmlNavigationReason {
    RoboHtmlNavigationReasonNone = 0,
    /** A user activated the anchor (a pointer, or the keyboard equivalent). */
    RoboHtmlNavigationReasonActivation = 1,
    /** An API caller asked for it; no pointer was involved. */
    RoboHtmlNavigationReasonProgrammatic = 2
} HtmlNavigationReason;

/** Non-zero if @p reason is a defined enumerator. */
int HtmlNavigationReasonIsKnown(HtmlNavigationReason reason);

/** Stable lowercase name of @p reason; never NULL, never allocated. */
const char *HtmlNavigationReasonName(HtmlNavigationReason r);

/**
 * Everything an activation decided, handed to the seam.
 *
 * Every borrowed pointer lives only for the duration of the handler call.
 * That is stated once and it is what keeps the seam from acquiring a
 * lifetime contract: a handler that must keep something copies it.
 */
typedef struct HtmlNavigationRequest {
    /** Document the anchor belongs to. */
    HtmlDocumentId documentId;
    /** The anchor itself, so a handler can read any other attribute. */
    HtmlNodeHandle anchor;
    /** The resolved target; never NULL, may be kind NONE or INVALID. */
    const HtmlNavigationTarget *target;
    /** The document's own reference, or "" when it has none. */
    const char *base;
    size_t baseLength;
    /** Borrowed `target` value, or NULL when absent. */
    const char *targetAttribute;
    size_t targetLength;
    /** The classified context; CURRENT when absent. */
    HtmlLinkTargetContext targetContext;
    /** Borrowed `rel` value, or NULL when absent. */
    const char *rel;
    size_t relLength;
    /** Non-zero when the anchor carried a `download` attribute. */
    int downloadRequested;
    /** Borrowed `download` value; "" for a bare attribute. */
    const char *downloadName;
    size_t downloadNameLength;
    /** Borrowed `title` value, or NULL when absent. */
    const char *title;
    size_t titleLength;
    HtmlNavigationReason reason;
} HtmlNavigationRequest;

/**
 * Deterministic outcome of an activation.
 *
 * This vocabulary exists so that "nothing happened" can never be spelled
 * "success". There is no `NAVIGATED` kind, and there will not be one until
 * something can actually navigate.
 */
typedef enum HtmlNavigationOutcomeKind {
    /** Not an activation that produced anything (unused as a result). */
    RoboHtmlNavigationNone = 0,
    /** No usable `href`: there was nothing to navigate to. Not an error. */
    RoboHtmlNavigationNoTarget = 1,
    /** An internal route, accepted by the handler. */
    RoboHtmlNavigationRoute = 2,
    /** A fragment that names an element in this document. */
    RoboHtmlNavigationFragment = 3,
    /** A fragment that names nothing here; the target is still reported. */
    RoboHtmlNavigationFragmentMissing = 4,
    /** An external URI, handed to the seam. */
    RoboHtmlNavigationExternal = 5,
    /** No handler, or a scheme/route the seam does not take. */
    RoboHtmlNavigationUnsupported = 6,
    /** The anchor asked to save the target, and saving is a later phase. */
    RoboHtmlNavigationDownloadPending = 7,
    /** The reference could not be made into a request (target INVALID). */
    RoboHtmlNavigationRejected = 8
} HtmlNavigationOutcomeKind;

/** Non-zero if @p kind is a defined enumerator. */
int HtmlNavigationOutcomeKindIsKnown(
    HtmlNavigationOutcomeKind kind);

/** Stable lowercase name of @p kind; never NULL, never allocated. */
const char *HtmlNavigationOutcomeKindName(
    HtmlNavigationOutcomeKind kind);

/**
 * The result of one activation.
 *
 * The outcome **owns** the resolved target it reports. That is the only
 * shape that works: activation resolves the reference itself, so if the
 * outcome merely borrowed, the target would die at the end of the call and
 * every reported string would dangle. Instead the target is handed to the
 * outcome, the convenience pointers below borrow from it, and
 * HtmlNavigationOutcomeClear releases it — one clear rule with no
 * lifetime trap, and a caller that wants the resolution itself can take
 * @c target and keep it.
 *
 * An outcome that was never filled (all zero) is safe to clear, and every
 * out-parameter path fills it, so "clear what you were given" is always
 * valid.
 */
typedef struct HtmlNavigationOutcome {
    HtmlNavigationOutcomeKind kind;
    /** The target's kind, so REJECTED and UNSUPPORTED can say which. */
    HtmlTargetKind targetKind;
    /**
     * The resolved target, owned by the outcome; NULL when the reference was
     * never resolved (a non-anchor, or an allocation failure). Release with
     * HtmlNavigationOutcomeClear.
     */
    HtmlNavigationTarget *target;
    /** Borrowed from @c target: resolved form, NULL when there was none. */
    const char *resolved;
    size_t resolvedLength;
    /** Borrowed from @c target: fragment, NULL when there was none. */
    const char *fragment;
    size_t fragmentLength;
    /** Borrowed from @c target: scheme, NULL when there was none. */
    const char *scheme;
    size_t schemeLength;
    /** The element a fragment named; set only when fragmentFound is 1. */
    HtmlNodeHandle fragmentNode;
    int fragmentFound;
    /** Non-zero when the handler seam was consulted for this activation. */
    int handlerCalled;
    /** What the seam returned; RoboOk when it was not called. */
    Result handlerResult;
} HtmlNavigationOutcome;

/**
 * Release what @p out owns and zero it. Safe on NULL and on a zeroed value,
 * so a caller can clear an outcome it was handed on a failure path without
 * knowing how far the call got.
 */
void HtmlNavigationOutcomeClear(HtmlNavigationOutcome *out);

/**
 * Decide what an accepted navigation means.
 *
 * Return RoboOk and set @p outKind to accept — the outcome keeps the
 * kind the caller set. Return anything else to decline, which becomes the
 * outcome's handlerResult and downgrades the outcome to UNSUPPORTED; that
 * is how a router says "not a route I know" without this module having to
 * understand routing.
 *
 * The handler is called synchronously, on the caller's thread, and must not
 * mutate the document it was handed — the request borrows the anchor.
 */
typedef Result (*HtmlNavigateFn)(
    void *userdata,
    const HtmlNavigationRequest *request,
    HtmlNavigationOutcomeKind *outKind,
    HtmlDiag *diag);

/** A handler is a function and its opaque state; no ownership is implied. */
typedef struct HtmlNavigationHandler {
    HtmlNavigateFn navigate;
    void *userdata;
} HtmlNavigationHandler;

/** Set up an empty handler (navigate NULL). */
void HtmlNavigationHandlerInit(HtmlNavigationHandler *handler);

/** Non-zero when @p handler has a function and can be consulted. */
int HtmlNavigationHandlerIsUsable(
    const HtmlNavigationHandler *handler);

/**
 * Activate @p element as a link.
 *
 * The whole point of the phase in one call, in this order:
 *
 *   1. refuse anything that is not a live `<a>`;
 *   2. read `href` and resolve it against the document's own reference;
 *   3. build the request — target, context, rel, download intent, title;
 *   4. decide, without consulting anything, whether there is navigation to do:
 *      no usable `href` → NO_TARGET; an unparseable reference → REJECTED;
 *      a `download` intent → DOWNLOAD_PENDING, because saving needs a
 *      filesystem capability this runtime does not have;
 *   5. a fragment is resolved here, in this document — FRAGMENT or
 *      FRAGMENT_MISSING — and never delegated, because it needs no I/O;
 *   6. an external URI whose scheme the runtime does not permit → UNSUPPORTED
 *      *without consulting the handler*; this is the security rule;
 *   7. otherwise the handler is asked, and its refusal is UNSUPPORTED while
 *      its acceptance keeps the kind. With no handler installed, a route or
 *      a permitted URI is UNSUPPORTED — the honest answer, and the one that
 *      keeps this phase from claiming navigation it cannot perform.
 *
 * @p out is filled on every path that produces an outcome, including the
 * refusals, and it owns the target it reports — clear it when done. Pass
 * @p out NULL to activate for the side effect alone.
 *
 * @return RoboOk when the activation completed and @p out describes it —
 *         *including* NO_TARGET, REJECTED and UNSUPPORTED, which are
 *         outcomes and not errors; RoboInvalidArgument for NULL
 *         @p element; RoboNotFound when @p element is not a live element
 *         or its document is gone; RoboNotSupported when it is not an
 *         `<a>`; RoboOutOfMemory.
 */
Result HtmlElementActivate(
    const HtmlNodeHandle *element,
    const HtmlNavigationHandler *handler,
    HtmlNavigationOutcome *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Debug output                                                        */
/* ------------------------------------------------------------------ */

/**
 * Render one target as a debug line, e.g.
 *   @c target kind=route reference="/Settings" resolved="/Settings"
 *   @c target kind=external reference="https://example.com" scheme=https
 * Returns the length written (excluding the terminator); truncates rather
 * than overruns and always terminates when @p capacity is non-zero.
 */
size_t HtmlNavigationTargetDescribe(
    const HtmlNavigationTarget *target,
    char *out,
    size_t capacity);

/**
 * Render one anchor as a debug line, e.g.
 *   @c a href="/Settings" resolved="/Settings" target=current rel="" state=activatable
 *
 * @p target is optional: pass the resolved target to include `resolved=`, or
 * NULL to omit it. RoboHTML's debug paths do not allocate, so a caller that
 * wants the resolution reported resolves it first — which also means the
 * line cannot report a resolution that the caller did not perform.
 *
 * Always terminates when @p capacity is non-zero; returns the length written
 * excluding the terminator.
 */
size_t HtmlElementAnchorDescribe(
    const HtmlNodeHandle *element,
    const HtmlNavigationTarget *target,
    char *out,
    size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_ANCHOR_H */
