/*
 * RoboHTML document (Phase 8.1).
 *
 * The document is the ownership root of the HTML subsystem:
 *   - owns the node registry (RoboHTMLNode.h) — every node belongs to
 *     exactly one document;
 *   - owns encoding state (RoboHTMLEncoding.h) and source metadata;
 *   - leases parsers exclusively (RoboHTMLParser.h);
 *   - carries the lifecycle state machine (RoboHTMLLifecycle.h);
 *   - hosts client/render hooks (RoboHTMLClient.h) and the mutation hook
 *     foundation (RoboHTMLDom.h).
 *
 * Identity: documents carry a runtime-owned id (never 0). Ids are never
 * reused during module lifetime. A document handle is inert; all state
 * queries go through robo_html_document_* calls with the id.
 *
 * Creation produces an empty, valid document in CREATED lifecycle with
 * no source attached. Empty-document state is a first-class state:
 * HtmlDocumentIsEmpty is true and stays true until parsing
 * phases populate the tree.
 *
 * Phase 8.4 turns that ownership root into the authoritative document
 * model. The rules, in one place:
 *
 *   1. The document owns the whole tree and every payload byte. There is
 *      no second owner, no reference counting, and no detached-node
 *      lifetime — a node lives exactly as long as its document.
 *   2. Node identity is (document id, node id, generation). A handle only
 *      ever resolves inside its own document, so a cross-document mistake
 *      is a detected error rather than an aliased node.
 *   3. The root is explicit: HtmlDocumentRoot returns the document
 *      node registered at creation, which is the tree's root and always
 *      has no parent. It is established by the document itself; nothing
 *      outside RoboHTML can replace it.
 *   4. Construction is opened and closed explicitly. RegisterNode and the
 *      internal link primitive work while the document is not finalized;
 *      HtmlDocumentFinalize closes construction. Reading, traversal
 *      and (future) DOM mutation are unaffected by finalization.
 *   5. Invariants are verifiable on demand, not enforced on every access:
 *      HtmlDocumentValidateTree audits the whole tree and reports
 *      what it found instead of silently trusting it.
 *   6. Documents are reusable through HtmlDocumentReset (which
 *      drops the tree but keeps the identity) and disposable through
 *      HtmlDocumentDestroy. Destroy is the normal end of life; a
 *      destroyed id is retired and never reused.
 *
 * See docs/DocumentModel.md.
 */

#ifndef ROBO_HTML_DOCUMENT_H
#define ROBO_HTML_DOCUMENT_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLClient.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLEncoding.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLParser.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create an empty document.
 * Requires an initialized module. On success *outId is live in CREATED
 * lifecycle with an empty document node already registered (the future
 * tree root — see RoboHTMLDom.h origin DOCUMENT_ROOT).
 * @return RoboOk; RoboInitError (module down); RoboInvalidArgument
 *         (NULL out); RoboOutOfMemory.
 */
Result HtmlDocumentCreate(HtmlDocumentId *outId,
                                        HtmlDiag *diag);

/**
 * Attach @p src to @p document (copying metadata; never the bytes).
 * CREATED → LOADING. An empty source is valid and settles immediately
 * back to COMPLETE with the document still empty: there is nothing to
 * parse, so nothing can be outstanding.
 * Replacement on INTERACTIVE/COMPLETE → LOADING is allowed. While
 * LOADING → RoboBusy.
 * The source bytes must stay valid until detach/destroy (borrow view).
 */
Result HtmlDocumentAttachSource(
    HtmlDocumentId documentId,
    const HtmlSource *src,
    HtmlDiag *diag);

/**
 * Detach any attached source and return a LOADING document to
 * INTERACTIVE. Detaching abandons input rather than completing it, so the
 * document settles at INTERACTIVE and not COMPLETE: the tree is whatever
 * was parsed so far, and it is live.
 * No-op with RoboOk when nothing is attached. Encoding state resets to
 * the boundary defaults.
 */
Result HtmlDocumentDetachSource(
    HtmlDocumentId documentId,
    HtmlDiag *diag);

/**
 * Mark @p document INTERACTIVE (LOADING → INTERACTIVE): parsing has
 * finished and the DOM is live. Called by the tree builder and parser when
 * a build completes; callers driving a document by hand call it too.
 * INTERACTIVE → INTERACTIVE is an error (nothing changed).
 */
Result HtmlDocumentMarkInteractive(
    HtmlDocumentId documentId,
    HtmlDiag *diag);

/**
 * Mark @p document COMPLETE (INTERACTIVE → COMPLETE): the document has
 * settled and no further input can arrive.
 *
 * This is deliberately reachable only from INTERACTIVE. A document that
 * has not finished parsing cannot claim to be finished loading, so
 * CREATED/LOADING → COMPLETE is RoboInvalidArgument rather than a
 * silent jump. COMPLETE → COMPLETE is also refused; completion happens
 * once.
 *
 * Completion does not close anything: reading, traversal, mutation, a
 * replacement source and reset all keep working.
 */
Result HtmlDocumentMarkComplete(
    HtmlDocumentId documentId,
    HtmlDiag *diag);

/**
 * Mark @p document FAILED (CREATED/LOADING/READY → FAILED, sticky).
 * @p domain records which boundary failed; @p message optional.
 */
Result HtmlDocumentMarkFailed(
    HtmlDocumentId documentId,
    HtmlDiagnosticDomain domain,
    const char *message,
    HtmlDiag *diag);

/** Copy current lifecycle; unknown/destroyed id → RoboNotFound. */
Result HtmlDocumentGetLifecycle(
    HtmlDocumentId documentId,
    HtmlDocumentLifecycle *outLifecycle);

/*
 * Lifecycle predicates (Phase 8.10).
 *
 * Convenience questions asked of a document id, so callers do not have to
 * fetch the enum and switch on it. Each is false for an unknown or
 * destroyed id — a retired id answers "no" to every state question
 * rather than reporting a state it no longer has. Use get_lifecycle when
 * you need to tell "not that state" apart from "not a document".
 */

/** Non-zero when the document is exactly LOADING (input in flight). */
int HtmlDocumentIsLoading(HtmlDocumentId documentId);

/**
 * Non-zero when the document is exactly INTERACTIVE: parsing has finished
 * and input has not settled. Mirrors @c document.readyState ==
 * "interactive", so it becomes false once the document is COMPLETE —
 * use is_settled when the question is "is the DOM parsed and live".
 */
int HtmlDocumentIsInteractive(HtmlDocumentId documentId);

/** Non-zero when the document is exactly COMPLETE. */
int HtmlDocumentIsComplete(HtmlDocumentId documentId);

/**
 * Non-zero when the document has a parsed, live DOM and no input in
 * flight (INTERACTIVE or COMPLETE). This is the question most callers
 * actually mean, and the one the mutation and traversal paths run in.
 */
int HtmlDocumentIsSettled(HtmlDocumentId documentId);

/** Copy the encoding state snapshot; unknown id → RoboNotFound. */
Result HtmlDocumentGetEncodingState(
    HtmlDocumentId documentId,
    HtmlEncodingState *outState);

/**
 * Boundary hook for the future encoding phase: record that the declared
 * input encoding changed (e.g. after a future <meta> scan). Only allowed
 * while content operations are allowed (CREATED/LOADING/READY).
 */
Result HtmlDocumentInputEncodingDeclared(
    HtmlDocumentId documentId,
    const char *label,
    HtmlDiag *diag);

/** Non-zero when the document has no content (always true in 8.1). */
int HtmlDocumentIsEmpty(HtmlDocumentId documentId);

/** Borrowed source name label ("" when unset); NULL when no source. */
const char *HtmlDocumentSourceName(HtmlDocumentId documentId);

/** Number of live nodes registered in @p document; 0 when unknown. */
size_t HtmlDocumentNodeCount(HtmlDocumentId documentId);

/**
 * Root node handle of the document tree (the document node registered at
 * creation). Destroyed/unknown document → invalid handle.
 */
HtmlNodeHandle HtmlDocumentRoot(
    HtmlDocumentId documentId);

/**
 * Register a new node in @p document (tree-builder entry point).
 * Phase 8.1: identity + registry only; no tree links, no content.
 * @p origin is diagnostic metadata (see RoboHTMLDom.h).
 * Requires a content-allowing lifecycle (CREATED/LOADING/READY).
 */
Result HtmlDocumentRegisterNode(
    HtmlDocumentId documentId,
    HtmlNodeKind kind,
    HtmlNodeOrigin origin,
    HtmlNodeHandle *outHandle,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Document model (Phase 8.4)                                          */
/* ------------------------------------------------------------------ */

/**
 * Document metadata snapshot: identity, state, ownership and counts.
 *
 * This is a snapshot, not a live view. Anything borrowed (@c sourceName,
 * @c failMessage) points into the document's own storage and stays valid
 * until the document is destroyed or reset — the same borrowed-string rule
 * as the node accessors. Counts are computed from the registry at call
 * time, so they are only as fresh as the call.
 */
typedef struct HtmlDocumentMetadata {
    HtmlDocumentId id;
    HtmlDocumentLifecycle lifecycle;
    HtmlDocumentLifecycle previousLifecycle;
    /** Non-zero once construction is closed (see finalize). */
    int finalized;
    /** Non-zero when the document holds nothing but its root node. */
    int isEmpty;
    /** Non-zero when the root handle resolves to a live document node. */
    int rootOk;

    /** Tree shape, counted from the registry. */
    size_t nodeCount;      /* all live nodes, including the root */
    size_t elementCount;
    size_t textCount;
    size_t commentCount;
    size_t doctypeCount;

    /** Source association (0 / NONE / "" when nothing is attached). */
    HtmlSourceKind sourceKind;
    size_t sourceLength;
    const char *sourceName;

    HtmlEncodingState encoding;

    /** Failure record; @c failMessage is NULL unless the document failed. */
    HtmlDiagnosticDomain failDomain;
    const char *failMessage;
} HtmlDocumentMetadata;

/**
 * Read a metadata snapshot of @p documentId.
 * @p outKindCount is optional; when given it must point at an array of
 * length RoboHtmlNodeKindCount and receives the live node count per
 * node kind, indexed by HtmlNodeKind.
 * @return RoboOk; RoboInvalidArgument (NULL out); RoboNotFound
 *         (unknown or destroyed document).
 */
Result GetHtmlDocumentMetadata(
    HtmlDocumentId documentId,
    HtmlDocumentMetadata *out,
    size_t *outKindCount,
    HtmlDiag *diag);

/**
 * Non-zero when @p h names a live node owned by @p documentId.
 *
 * This is the ownership check, and it answers two distinct questions at
 * once: does the handle claim this document, and does the claimed identity
 * still resolve to a live registry entry? A handle from another document,
 * a retired document, an unknown node id and a stale generation are all
 * simply "not owned" — never undefined behaviour.
 */
int HtmlDocumentOwnsNode(HtmlDocumentId documentId,
                                 const HtmlNodeHandle *h);

/**
 * Resolve a bare node id inside @p documentId into an inert handle.
 * Node ids are identity, not storage: the caller needs this only when it
 * obtained an id from somewhere that had no handle (a diagnostics record,
 * a stack dump, a future DOM script binding).
 * @return RoboOk; RoboInvalidArgument (NULL out); RoboNotFound (no
 *         such live node, or unknown document — @p out is cleared first,
 *         so a failure can never leave a partially valid handle).
 */
Result HtmlDocumentNodeById(
    HtmlDocumentId documentId,
    HtmlNodeId nodeId,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/**
 * Finalize @p documentId: the tree is complete and closed for
 * construction.
 *
 * After this call HtmlDocumentRegisterNode returns RoboError with
 * a TREE-domain diagnostic, and the internal child-linking primitive does
 * likewise. Everything read-side — the root, traversal, payload access,
 * metadata, validation — keeps working, and so will DOM mutation, which is
 * a different operation from tree construction; this flag deliberately
 * does not lock the document against the DOM phase.
 *
 * Finalization is idempotent, and it is what the tree builder does at the
 * end of a completed build. Construction reopens through
 * HtmlDocumentReset, through HtmlDocumentAttachSource (a
 * new source is about to be parsed), or through the tree builder's own
 * begin — so re-parsing a document is always a deliberate act.
 *
 * @return RoboOk; RoboNotFound (unknown document); RoboError (the
 *         lifecycle (FAILED/DESTROYED) can no longer accept content).
 */
Result HtmlDocumentFinalize(HtmlDocumentId documentId,
                                          HtmlDiag *diag);

/** Non-zero when @p documentId has been finalized. 0 when unknown. */
int HtmlDocumentIsFinalized(HtmlDocumentId documentId);

/**
 * Non-zero when tree-construction operations are currently accepted:
 * the document exists, its lifecycle allows content, and construction has
 * not been closed by finalization.
 */
int HtmlDocumentAllowsConstruction(
    HtmlDocumentId documentId);

/**
 * Reset @p documentId to a fresh, empty document without changing its
 * identity: the tree is dropped (every previously handed-out node handle
 * becomes stale), the source association and encoding state are cleared,
 * any failure record is cleared, and construction is reopened.
 *
 * Lifecycle on return: CREATED for a document that never loaded anything, and
 * COMPLETE for one that did — whatever it was in when it was reset, including
 * an interrupted (INTERACTIVE) load. Both mean the same thing to a caller —
 * a document with no source and no content that can accept a new source —
 * and neither is FAILED or DESTROYED. A FAILED document is rejected: the
 * 8.1 rule that failure is sticky until destroy still holds.
 *
 * @return RoboOk; RoboNotFound (unknown document); RoboBusy (a parser
 *         or tree builder still holds the document lease); RoboError
 *         (lifecycle cannot accept content, e.g. FAILED).
 */
Result HtmlDocumentReset(HtmlDocumentId documentId,
                                       HtmlDiag *diag);

/**
 * Result of a whole-tree invariant audit (see validate_tree).
 *
 * Every counter is an independent observation, so a report is useful even
 * when it is not clean: it says *what* disagrees, not merely that
 * something does. All zero plus @c rootOk means @c ok.
 */
typedef struct HtmlTreeReport {
    /** Live registry entries at audit time. */
    size_t registered;
    /** Nodes reachable from the root through the child links. */
    size_t nodes;
    /** Deepest node depth below the root (root itself is depth 0). */
    size_t maxDepth;
    /** Non-zero when the root handle, kind and parent link are correct. */
    int rootOk;

    /**
     * Live nodes that are not reachable from the root *and* claim a parent:
     * a child link (or a parent link) that does not describe the tree. This
     * is the corruption detector — a node that believes it is in the tree
     * while the tree cannot reach it.
     */
    size_t orphans;
    /**
     * Live nodes with no parent that the root walk did not reach: detached
     * subtrees (Phase 8.9 — remove_child keeps the node and its subtree
     * intact) and nodes registered but never linked. Detachment is a legal
     * state, so these do NOT make @c ok false; they are counted because a
     * document that meant to have them attached would otherwise look clean.
     * A detached subtree is counted once, at its root, and its own links are
     * audited like any other.
     */
    size_t detached;
    /** Links to non-live ids, and revisitings of an already-reached node. */
    size_t badLinks;
    size_t cycles;
    /** A child whose parent link is not the node that lists it. */
    size_t badParents;
    /** childCount / lastChild disagreeing with the sibling chain. */
    size_t badCounts;
    /** prevSibling links that do not match the traversal order. */
    size_t siblingMismatches;

    /** Non-zero when every observation above is clean. */
    int ok;
} HtmlTreeReport;

/**
 * Audit the document tree's invariants and report what was found.
 *
 * Checks: the root exists, is a document node and has no parent; every
 * node reachable from the root is reached exactly once (so a link cycle
 * terminates the walk with @c cycles set rather than hanging); every child
 * links back to the parent that lists it; childCount and lastChild agree
 * with the child chain; and no live node is unreachable.
 *
 * This is a diagnostic/verification facility, deliberately separate from
 * the read paths so ordinary traversal never pays for a full audit. It is
 * safe on a half-built or malformed tree and allocates one scratch bitmap
 * bounded by RoboHtmlMaxNodesPerDocument.
 *
 * An inconsistent tree is NOT an error return: the audit succeeded, and
 * the verdict is @c out->ok. @p out is zeroed even on failure.
 *
 * @return RoboOk; RoboInvalidArgument (NULL out); RoboNotFound
 *         (unknown document); RoboOutOfMemory (scratch allocation).
 */
Result HtmlDocumentValidateTree(
    HtmlDocumentId documentId,
    HtmlTreeReport *out,
    HtmlDiag *diag);

/**
 * Destroy @p document and retire its id.
 * Fails any leased parser (see RoboHTMLParser.h), drops the client and
 * mutation hooks, releases all node registry state. DESTROYED is
 * terminal: further calls with the id return RoboNotFound.
 * Double destroy → RoboNotFound (never undefined behavior).
 */
Result HtmlDocumentDestroy(HtmlDocumentId documentId,
                                         HtmlDiag *diag);

/** Non-zero when @p id is a live (created, not destroyed) document. */
int HtmlDocumentIsLive(HtmlDocumentId documentId);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_DOCUMENT_H */
