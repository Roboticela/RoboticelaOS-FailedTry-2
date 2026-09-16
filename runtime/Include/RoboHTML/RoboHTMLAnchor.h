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
 *        ├── attributes   robo_html_element_anchor_attributes
 *        │      href      what to navigate to
 *        │      target    which context should receive it
 *        │      rel       preserved; queryable, never interpreted here
 *        │      download  a save *intent*, not a save
 *        │      title     advisory text, kept verbatim
 *        │
 *        ├── target       robo_html_navigation_target_t  (owned, resolved)
 *        │      kind      NONE │ ROUTE │ FRAGMENT │ EXTERNAL │ INVALID
 *        │      resolved  the normalized path, or the URI verbatim
 *        │      fragment  the `#id` part, if there was one
 *        │      scheme    the URI scheme, if there was one
 *        │
 *        └── activation   robo_html_element_activate
 *               └── request  robo_html_navigation_request_t
 *                     └── handler   the delegated-execution seam
 *                           └── outcome  robo_html_navigation_outcome_t
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
 *   robo_html_navigation_handler_t, and *that* decides what an accepted
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
 * robo_html_element_attribute_value, which this is a typed view over.
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
 * robo_html_navigation_target_create and nowhere else.
 */
typedef struct robo_html_anchor_attributes {
    /** Non-zero when an `href` attribute record exists. */
    int href_present;
    /** Borrowed `href` bytes; NULL when absent. */
    const char *href;
    size_t href_length;
    /** Non-zero when `href` is present and names something (non-empty). */
    int href_usable;

    /** Non-zero when a `target` attribute record exists. */
    int target_present;
    /** Borrowed `target` bytes; NULL when absent. */
    const char *target;
    size_t target_length;

    int rel_present;
    const char *rel;
    size_t rel_length;

    int download_present;
    /** Borrowed `download` bytes; "" (length 0) for a bare `<a download>`. */
    const char *download;
    size_t download_length;

    int title_present;
    const char *title;
    size_t title_length;
} robo_html_anchor_attributes_t;

/**
 * Read the anchor attributes of @p element.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p element / @p out;
 *         ROBO_NOT_FOUND when @p element is not a live element;
 *         ROBO_NOT_SUPPORTED when the element is not an `<a>`. That last
 *         case is a refusal rather than an empty view on purpose: reporting
 *         "no href" for a `<p>` would let a caller mistake "not a link" for
 *         "a link with nothing to do".
 */
robo_result_t robo_html_element_anchor_attributes(
    const robo_html_node_handle_t *element,
    robo_html_anchor_attributes_t *out,
    robo_html_diag_t *diag);

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
typedef enum robo_html_target_kind {
    /** No usable reference: the attribute is absent, empty or whitespace. */
    ROBO_HTML_TARGET_NONE = 0,
    /** An application-internal path, resolved against the document's own. */
    ROBO_HTML_TARGET_ROUTE = 1,
    /** A fragment on the current document: `#Section`. */
    ROBO_HTML_TARGET_FRAGMENT = 2,
    /** An absolute URI with a scheme: `https:`, `mailto:`, `tel:`, ... */
    ROBO_HTML_TARGET_EXTERNAL = 3,
    /** A reference that cannot be made into a target at all. */
    ROBO_HTML_TARGET_INVALID = 4
} robo_html_target_kind_t;

/** Non-zero if @p kind is a defined enumerator. */
int robo_html_target_kind_is_known(robo_html_target_kind_t kind);

/** Stable lowercase name of @p kind; never NULL, never allocated. */
const char *robo_html_target_kind_name(robo_html_target_kind_t kind);

/**
 * Longest `href` this model accepts, in bytes.
 *
 * A reference longer than this is INVALID rather than truncated or
 * allocated: an unbounded attribute value should be refused at the boundary,
 * not carried into a resolution that has to size a buffer from it. 4 KiB is
 * far beyond any route a real application uses and small enough that a
 * hostile document cannot make resolution expensive.
 */
#define ROBO_HTML_TARGET_MAX_REFERENCE 4096u

/**
 * A resolved reference. Owned; release with robo_html_navigation_target_destroy.
 *
 * The strings it exposes are borrowed from the target and live exactly as
 * long as it does. They are NUL-terminated as well as length-counted, so a
 * caller can pass them to a C API without copying, but the length is the
 * authoritative size.
 */
typedef struct robo_html_navigation_target robo_html_navigation_target_t;

/**
 * Parse and resolve @p href against @p base.
 *
 * @p base is the document's own reference — the source name the document was
 * built from, as robo_html_document_source_name returns. It supplies the
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
 * Failure is a *kind*, not an error: an unusable reference yields ROBO_OK
 * with kind NONE, and an unparseable one ROBO_OK with kind INVALID.
 *
 * @return ROBO_OK and a target in @p out (which can be NONE or INVALID);
 *         ROBO_INVALID_ARGUMENT for NULL @p href / @p out, or @p href NULL
 *         with non-zero @p href_length; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_html_navigation_target_create(
    const char *href,
    size_t href_length,
    const char *base,
    size_t base_length,
    robo_html_navigation_target_t **out,
    robo_html_diag_t *diag);

/**
 * Resolve @p element's `href` against its own document's reference.
 *
 * @return ROBO_OK and a target in @p out — always a target, even for an
 *         anchor with no `href` (kind NONE), because "this link points
 *         nowhere" is an answer; ROBO_INVALID_ARGUMENT; ROBO_NOT_FOUND
 *         (not a live element, or its document is gone); ROBO_NOT_SUPPORTED
 *         (not an `<a>`); ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_html_element_navigation_target(
    const robo_html_node_handle_t *element,
    robo_html_navigation_target_t **out,
    robo_html_diag_t *diag);

/** Release @p target and everything it owns. Safe on NULL. */
void robo_html_navigation_target_destroy(robo_html_navigation_target_t *target);

/** What the reference turned out to be; NONE for a NULL target. */
robo_html_target_kind_t robo_html_navigation_target_kind(
    const robo_html_navigation_target_t *target);

/**
 * The reference exactly as the document wrote it, or NULL for a NULL target
 * or a NONE kind. @p out_length (optional) receives the byte count.
 */
const char *robo_html_navigation_target_reference(
    const robo_html_navigation_target_t *target,
    size_t *out_length);

/**
 * The resolved form: the normalized path for a ROUTE, the URI verbatim for
 * an EXTERNAL target, the base reference for a FRAGMENT. NULL when there is
 * nothing to resolve to (NONE, INVALID).
 */
const char *robo_html_navigation_target_resolved(
    const robo_html_navigation_target_t *target,
    size_t *out_length);

/**
 * The `#id` part with the `#` removed, or NULL when the reference had none.
 *
 * An *empty* fragment is a present fragment of length 0 — `href="#"`
 * navigates to the top of the target — so NULL versus "" matters here for
 * the same reason it does everywhere else in this runtime.
 */
const char *robo_html_navigation_target_fragment(
    const robo_html_navigation_target_t *target,
    size_t *out_length);

/** The lowercased scheme, or NULL when the reference had none. */
const char *robo_html_navigation_target_scheme(
    const robo_html_navigation_target_t *target,
    size_t *out_length);

/** Non-zero when the kind is ROUTE or FRAGMENT: stays inside this runtime. */
int robo_html_navigation_target_is_internal(
    const robo_html_navigation_target_t *target);

/* ------------------------------------------------------------------ */
/* Scheme policy                                                       */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p scheme is one this runtime can *name*.
 *
 * Knowing a scheme is not permission to use it — see
 * robo_html_navigation_scheme_is_permitted. The two questions are separate
 * because they answer different things: `file` is known and not permitted
 * ("we understand what you asked for and we refuse it"), while `weird` is
 * neither ("this runtime has never heard of that").
 */
int robo_html_navigation_scheme_is_known(const char *scheme, size_t length);

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
int robo_html_navigation_scheme_is_permitted(const char *scheme,
                                             size_t length);

/* ------------------------------------------------------------------ */
/* Fragment lookup                                                     */
/* ------------------------------------------------------------------ */

/**
 * Find the element in @p document_id whose `id` attribute is exactly
 * @p fragment, in document order.
 *
 * This is the one navigation this phase can complete, and it is deliberately
 * named for its caller. It is not `getElementById`: the id lookup every DOM
 * query will need is a later phase's API, with its own name, its own
 * caching question and its own error surface. What exists here is what a
 * fragment activation needs — first match, document order, exact
 * case-sensitive comparison, id *attribute* not node identity.
 *
 * @return ROBO_OK and the element in @p out; ROBO_INVALID_ARGUMENT for NULL
 *         @p out; ROBO_NOT_FOUND when nothing matches — including for an
 *         empty fragment, which by definition names the top of the document
 *         and therefore names no element; ROBO_ERROR when the walk could not
 *         be trusted (see the traversal walkers).
 */
robo_result_t robo_html_navigation_find_fragment(
    robo_html_document_id_t document_id,
    const char *fragment,
    size_t fragment_length,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag);

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
typedef enum robo_html_link_target_context {
    /** Absent, empty, or `_self`: this application's current context. */
    ROBO_HTML_LINK_TARGET_CONTEXT_CURRENT = 0,
    /** Any other name: a named context the application may recognise. */
    ROBO_HTML_LINK_TARGET_CONTEXT_NAMED = 1,
    /** `_blank`: a new context. Deliberately *not* a new tab or window. */
    ROBO_HTML_LINK_TARGET_CONTEXT_NEW = 2,
    /** `_parent` / `_top`: an enclosing context. */
    ROBO_HTML_LINK_TARGET_CONTEXT_PARENT = 3,
    ROBO_HTML_LINK_TARGET_CONTEXT_TOP = 4,
    /** A reserved `_`-prefixed name this runtime does not define. */
    ROBO_HTML_LINK_TARGET_CONTEXT_UNKNOWN = 5
} robo_html_link_target_context_t;

/** Non-zero if @p context is a defined enumerator. */
int robo_html_link_target_context_is_known(robo_html_link_target_context_t c);

/** Stable lowercase name of @p context; never NULL, never allocated. */
const char *robo_html_link_target_context_name(
    robo_html_link_target_context_t context);

/**
 * Classify a `target` attribute value. A NULL value is CURRENT (absent), and
 * matching is ASCII case-insensitive because HTML keyword matching is.
 */
robo_html_link_target_context_t robo_html_link_target_context_for(
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
int robo_html_link_rel_contains_token(const char *rel, size_t rel_length,
                                      const char *token);

/* ------------------------------------------------------------------ */
/* Navigation request, handler seam, outcome                           */
/* ------------------------------------------------------------------ */

/** Why an activation was requested. */
typedef enum robo_html_navigation_reason {
    ROBO_HTML_NAVIGATION_REASON_NONE = 0,
    /** A user activated the anchor (a pointer, or the keyboard equivalent). */
    ROBO_HTML_NAVIGATION_REASON_ACTIVATION = 1,
    /** An API caller asked for it; no pointer was involved. */
    ROBO_HTML_NAVIGATION_REASON_PROGRAMMATIC = 2
} robo_html_navigation_reason_t;

/** Non-zero if @p reason is a defined enumerator. */
int robo_html_navigation_reason_is_known(robo_html_navigation_reason_t reason);

/** Stable lowercase name of @p reason; never NULL, never allocated. */
const char *robo_html_navigation_reason_name(robo_html_navigation_reason_t r);

/**
 * Everything an activation decided, handed to the seam.
 *
 * Every borrowed pointer lives only for the duration of the handler call.
 * That is stated once and it is what keeps the seam from acquiring a
 * lifetime contract: a handler that must keep something copies it.
 */
typedef struct robo_html_navigation_request {
    /** Document the anchor belongs to. */
    robo_html_document_id_t document_id;
    /** The anchor itself, so a handler can read any other attribute. */
    robo_html_node_handle_t anchor;
    /** The resolved target; never NULL, may be kind NONE or INVALID. */
    const robo_html_navigation_target_t *target;
    /** The document's own reference, or "" when it has none. */
    const char *base;
    size_t base_length;
    /** Borrowed `target` value, or NULL when absent. */
    const char *target_attribute;
    size_t target_length;
    /** The classified context; CURRENT when absent. */
    robo_html_link_target_context_t target_context;
    /** Borrowed `rel` value, or NULL when absent. */
    const char *rel;
    size_t rel_length;
    /** Non-zero when the anchor carried a `download` attribute. */
    int download_requested;
    /** Borrowed `download` value; "" for a bare attribute. */
    const char *download_name;
    size_t download_name_length;
    /** Borrowed `title` value, or NULL when absent. */
    const char *title;
    size_t title_length;
    robo_html_navigation_reason_t reason;
} robo_html_navigation_request_t;

/**
 * Deterministic outcome of an activation.
 *
 * This vocabulary exists so that "nothing happened" can never be spelled
 * "success". There is no `NAVIGATED` kind, and there will not be one until
 * something can actually navigate.
 */
typedef enum robo_html_navigation_outcome_kind {
    /** Not an activation that produced anything (unused as a result). */
    ROBO_HTML_NAVIGATION_NONE = 0,
    /** No usable `href`: there was nothing to navigate to. Not an error. */
    ROBO_HTML_NAVIGATION_NO_TARGET = 1,
    /** An internal route, accepted by the handler. */
    ROBO_HTML_NAVIGATION_ROUTE = 2,
    /** A fragment that names an element in this document. */
    ROBO_HTML_NAVIGATION_FRAGMENT = 3,
    /** A fragment that names nothing here; the target is still reported. */
    ROBO_HTML_NAVIGATION_FRAGMENT_MISSING = 4,
    /** An external URI, handed to the seam. */
    ROBO_HTML_NAVIGATION_EXTERNAL = 5,
    /** No handler, or a scheme/route the seam does not take. */
    ROBO_HTML_NAVIGATION_UNSUPPORTED = 6,
    /** The anchor asked to save the target, and saving is a later phase. */
    ROBO_HTML_NAVIGATION_DOWNLOAD_PENDING = 7,
    /** The reference could not be made into a request (target INVALID). */
    ROBO_HTML_NAVIGATION_REJECTED = 8
} robo_html_navigation_outcome_kind_t;

/** Non-zero if @p kind is a defined enumerator. */
int robo_html_navigation_outcome_kind_is_known(
    robo_html_navigation_outcome_kind_t kind);

/** Stable lowercase name of @p kind; never NULL, never allocated. */
const char *robo_html_navigation_outcome_kind_name(
    robo_html_navigation_outcome_kind_t kind);

/**
 * The result of one activation.
 *
 * The outcome **owns** the resolved target it reports. That is the only
 * shape that works: activation resolves the reference itself, so if the
 * outcome merely borrowed, the target would die at the end of the call and
 * every reported string would dangle. Instead the target is handed to the
 * outcome, the convenience pointers below borrow from it, and
 * robo_html_navigation_outcome_clear releases it — one clear rule with no
 * lifetime trap, and a caller that wants the resolution itself can take
 * @c target and keep it.
 *
 * An outcome that was never filled (all zero) is safe to clear, and every
 * out-parameter path fills it, so "clear what you were given" is always
 * valid.
 */
typedef struct robo_html_navigation_outcome {
    robo_html_navigation_outcome_kind_t kind;
    /** The target's kind, so REJECTED and UNSUPPORTED can say which. */
    robo_html_target_kind_t target_kind;
    /**
     * The resolved target, owned by the outcome; NULL when the reference was
     * never resolved (a non-anchor, or an allocation failure). Release with
     * robo_html_navigation_outcome_clear.
     */
    robo_html_navigation_target_t *target;
    /** Borrowed from @c target: resolved form, NULL when there was none. */
    const char *resolved;
    size_t resolved_length;
    /** Borrowed from @c target: fragment, NULL when there was none. */
    const char *fragment;
    size_t fragment_length;
    /** Borrowed from @c target: scheme, NULL when there was none. */
    const char *scheme;
    size_t scheme_length;
    /** The element a fragment named; set only when fragment_found is 1. */
    robo_html_node_handle_t fragment_node;
    int fragment_found;
    /** Non-zero when the handler seam was consulted for this activation. */
    int handler_called;
    /** What the seam returned; ROBO_OK when it was not called. */
    robo_result_t handler_result;
} robo_html_navigation_outcome_t;

/**
 * Release what @p out owns and zero it. Safe on NULL and on a zeroed value,
 * so a caller can clear an outcome it was handed on a failure path without
 * knowing how far the call got.
 */
void robo_html_navigation_outcome_clear(robo_html_navigation_outcome_t *out);

/**
 * Decide what an accepted navigation means.
 *
 * Return ROBO_OK and set @p out_kind to accept — the outcome keeps the
 * kind the caller set. Return anything else to decline, which becomes the
 * outcome's handler_result and downgrades the outcome to UNSUPPORTED; that
 * is how a router says "not a route I know" without this module having to
 * understand routing.
 *
 * The handler is called synchronously, on the caller's thread, and must not
 * mutate the document it was handed — the request borrows the anchor.
 */
typedef robo_result_t (*robo_html_navigate_fn)(
    void *userdata,
    const robo_html_navigation_request_t *request,
    robo_html_navigation_outcome_kind_t *out_kind,
    robo_html_diag_t *diag);

/** A handler is a function and its opaque state; no ownership is implied. */
typedef struct robo_html_navigation_handler {
    robo_html_navigate_fn navigate;
    void *userdata;
} robo_html_navigation_handler_t;

/** Set up an empty handler (navigate NULL). */
void robo_html_navigation_handler_init(robo_html_navigation_handler_t *handler);

/** Non-zero when @p handler has a function and can be consulted. */
int robo_html_navigation_handler_is_usable(
    const robo_html_navigation_handler_t *handler);

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
 * @return ROBO_OK when the activation completed and @p out describes it —
 *         *including* NO_TARGET, REJECTED and UNSUPPORTED, which are
 *         outcomes and not errors; ROBO_INVALID_ARGUMENT for NULL
 *         @p element; ROBO_NOT_FOUND when @p element is not a live element
 *         or its document is gone; ROBO_NOT_SUPPORTED when it is not an
 *         `<a>`; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_html_element_activate(
    const robo_html_node_handle_t *element,
    const robo_html_navigation_handler_t *handler,
    robo_html_navigation_outcome_t *out,
    robo_html_diag_t *diag);

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
size_t robo_html_navigation_target_describe(
    const robo_html_navigation_target_t *target,
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
size_t robo_html_element_anchor_describe(
    const robo_html_node_handle_t *element,
    const robo_html_navigation_target_t *target,
    char *out,
    size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_ANCHOR_H */
