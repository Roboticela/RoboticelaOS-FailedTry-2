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
 * robo_html_document_is_empty is true and stays true until parsing
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
 *   3. The root is explicit: robo_html_document_root returns the document
 *      node registered at creation, which is the tree's root and always
 *      has no parent. It is established by the document itself; nothing
 *      outside RoboHTML can replace it.
 *   4. Construction is opened and closed explicitly. register_node and the
 *      internal link primitive work while the document is not finalized;
 *      robo_html_document_finalize closes construction. Reading, traversal
 *      and (future) DOM mutation are unaffected by finalization.
 *   5. Invariants are verifiable on demand, not enforced on every access:
 *      robo_html_document_validate_tree audits the whole tree and reports
 *      what it found instead of silently trusting it.
 *   6. Documents are reusable through robo_html_document_reset (which
 *      drops the tree but keeps the identity) and disposable through
 *      robo_html_document_destroy. Destroy is the normal end of life; a
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
 * Requires an initialized module. On success *out_id is live in CREATED
 * lifecycle with an empty document node already registered (the future
 * tree root — see RoboHTMLDom.h origin DOCUMENT_ROOT).
 * @return ROBO_OK; ROBO_INIT_ERROR (module down); ROBO_INVALID_ARGUMENT
 *         (NULL out); ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_html_document_create(robo_html_document_id_t *out_id,
                                        robo_html_diag_t *diag);

/**
 * Attach @p src to @p document (copying metadata; never the bytes).
 * CREATED → LOADING. An empty source is valid and settles immediately
 * back to COMPLETE with the document still empty: there is nothing to
 * parse, so nothing can be outstanding.
 * Replacement on INTERACTIVE/COMPLETE → LOADING is allowed. While
 * LOADING → ROBO_BUSY.
 * The source bytes must stay valid until detach/destroy (borrow view).
 */
robo_result_t robo_html_document_attach_source(
    robo_html_document_id_t document_id,
    const robo_html_source_t *src,
    robo_html_diag_t *diag);

/**
 * Detach any attached source and return a LOADING document to
 * INTERACTIVE. Detaching abandons input rather than completing it, so the
 * document settles at INTERACTIVE and not COMPLETE: the tree is whatever
 * was parsed so far, and it is live.
 * No-op with ROBO_OK when nothing is attached. Encoding state resets to
 * the boundary defaults.
 */
robo_result_t robo_html_document_detach_source(
    robo_html_document_id_t document_id,
    robo_html_diag_t *diag);

/**
 * Mark @p document INTERACTIVE (LOADING → INTERACTIVE): parsing has
 * finished and the DOM is live. Called by the tree builder and parser when
 * a build completes; callers driving a document by hand call it too.
 * INTERACTIVE → INTERACTIVE is an error (nothing changed).
 */
robo_result_t robo_html_document_mark_interactive(
    robo_html_document_id_t document_id,
    robo_html_diag_t *diag);

/**
 * Mark @p document COMPLETE (INTERACTIVE → COMPLETE): the document has
 * settled and no further input can arrive.
 *
 * This is deliberately reachable only from INTERACTIVE. A document that
 * has not finished parsing cannot claim to be finished loading, so
 * CREATED/LOADING → COMPLETE is ROBO_INVALID_ARGUMENT rather than a
 * silent jump. COMPLETE → COMPLETE is also refused; completion happens
 * once.
 *
 * Completion does not close anything: reading, traversal, mutation, a
 * replacement source and reset all keep working.
 */
robo_result_t robo_html_document_mark_complete(
    robo_html_document_id_t document_id,
    robo_html_diag_t *diag);

/**
 * Mark @p document FAILED (CREATED/LOADING/READY → FAILED, sticky).
 * @p domain records which boundary failed; @p message optional.
 */
robo_result_t robo_html_document_mark_failed(
    robo_html_document_id_t document_id,
    robo_html_diagnostic_domain_t domain,
    const char *message,
    robo_html_diag_t *diag);

/** Copy current lifecycle; unknown/destroyed id → ROBO_NOT_FOUND. */
robo_result_t robo_html_document_get_lifecycle(
    robo_html_document_id_t document_id,
    robo_html_document_lifecycle_t *out_lifecycle);

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
int robo_html_document_is_loading(robo_html_document_id_t document_id);

/**
 * Non-zero when the document is exactly INTERACTIVE: parsing has finished
 * and input has not settled. Mirrors @c document.readyState ==
 * "interactive", so it becomes false once the document is COMPLETE —
 * use is_settled when the question is "is the DOM parsed and live".
 */
int robo_html_document_is_interactive(robo_html_document_id_t document_id);

/** Non-zero when the document is exactly COMPLETE. */
int robo_html_document_is_complete(robo_html_document_id_t document_id);

/**
 * Non-zero when the document has a parsed, live DOM and no input in
 * flight (INTERACTIVE or COMPLETE). This is the question most callers
 * actually mean, and the one the mutation and traversal paths run in.
 */
int robo_html_document_is_settled(robo_html_document_id_t document_id);

/** Copy the encoding state snapshot; unknown id → ROBO_NOT_FOUND. */
robo_result_t robo_html_document_get_encoding_state(
    robo_html_document_id_t document_id,
    robo_html_encoding_state_t *out_state);

/**
 * Boundary hook for the future encoding phase: record that the declared
 * input encoding changed (e.g. after a future <meta> scan). Only allowed
 * while content operations are allowed (CREATED/LOADING/READY).
 */
robo_result_t robo_html_document_input_encoding_declared(
    robo_html_document_id_t document_id,
    const char *label,
    robo_html_diag_t *diag);

/** Non-zero when the document has no content (always true in 8.1). */
int robo_html_document_is_empty(robo_html_document_id_t document_id);

/** Borrowed source name label ("" when unset); NULL when no source. */
const char *robo_html_document_source_name(robo_html_document_id_t document_id);

/** Number of live nodes registered in @p document; 0 when unknown. */
size_t robo_html_document_node_count(robo_html_document_id_t document_id);

/**
 * Root node handle of the document tree (the document node registered at
 * creation). Destroyed/unknown document → invalid handle.
 */
robo_html_node_handle_t robo_html_document_root(
    robo_html_document_id_t document_id);

/**
 * Register a new node in @p document (tree-builder entry point).
 * Phase 8.1: identity + registry only; no tree links, no content.
 * @p origin is diagnostic metadata (see RoboHTMLDom.h).
 * Requires a content-allowing lifecycle (CREATED/LOADING/READY).
 */
robo_result_t robo_html_document_register_node(
    robo_html_document_id_t document_id,
    robo_html_node_kind_t kind,
    robo_html_node_origin_t origin,
    robo_html_node_handle_t *out_handle,
    robo_html_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Document model (Phase 8.4)                                          */
/* ------------------------------------------------------------------ */

/**
 * Document metadata snapshot: identity, state, ownership and counts.
 *
 * This is a snapshot, not a live view. Anything borrowed (@c source_name,
 * @c fail_message) points into the document's own storage and stays valid
 * until the document is destroyed or reset — the same borrowed-string rule
 * as the node accessors. Counts are computed from the registry at call
 * time, so they are only as fresh as the call.
 */
typedef struct robo_html_document_metadata {
    robo_html_document_id_t id;
    robo_html_document_lifecycle_t lifecycle;
    robo_html_document_lifecycle_t previous_lifecycle;
    /** Non-zero once construction is closed (see finalize). */
    int finalized;
    /** Non-zero when the document holds nothing but its root node. */
    int is_empty;
    /** Non-zero when the root handle resolves to a live document node. */
    int root_ok;

    /** Tree shape, counted from the registry. */
    size_t node_count;      /* all live nodes, including the root */
    size_t element_count;
    size_t text_count;
    size_t comment_count;
    size_t doctype_count;

    /** Source association (0 / NONE / "" when nothing is attached). */
    robo_html_source_kind_t source_kind;
    size_t source_length;
    const char *source_name;

    robo_html_encoding_state_t encoding;

    /** Failure record; @c fail_message is NULL unless the document failed. */
    robo_html_diagnostic_domain_t fail_domain;
    const char *fail_message;
} robo_html_document_metadata_t;

/**
 * Read a metadata snapshot of @p document_id.
 * @p out_kind_count is optional; when given it must point at an array of
 * length ROBO_HTML_NODE_KIND_COUNT and receives the live node count per
 * node kind, indexed by robo_html_node_kind_t.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL out); ROBO_NOT_FOUND
 *         (unknown or destroyed document).
 */
robo_result_t robo_html_document_metadata(
    robo_html_document_id_t document_id,
    robo_html_document_metadata_t *out,
    size_t *out_kind_count,
    robo_html_diag_t *diag);

/**
 * Non-zero when @p h names a live node owned by @p document_id.
 *
 * This is the ownership check, and it answers two distinct questions at
 * once: does the handle claim this document, and does the claimed identity
 * still resolve to a live registry entry? A handle from another document,
 * a retired document, an unknown node id and a stale generation are all
 * simply "not owned" — never undefined behaviour.
 */
int robo_html_document_owns_node(robo_html_document_id_t document_id,
                                 const robo_html_node_handle_t *h);

/**
 * Resolve a bare node id inside @p document_id into an inert handle.
 * Node ids are identity, not storage: the caller needs this only when it
 * obtained an id from somewhere that had no handle (a diagnostics record,
 * a stack dump, a future DOM script binding).
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL out); ROBO_NOT_FOUND (no
 *         such live node, or unknown document — @p out is cleared first,
 *         so a failure can never leave a partially valid handle).
 */
robo_result_t robo_html_document_node_by_id(
    robo_html_document_id_t document_id,
    robo_html_node_id_t node_id,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag);

/**
 * Finalize @p document_id: the tree is complete and closed for
 * construction.
 *
 * After this call robo_html_document_register_node returns ROBO_ERROR with
 * a TREE-domain diagnostic, and the internal child-linking primitive does
 * likewise. Everything read-side — the root, traversal, payload access,
 * metadata, validation — keeps working, and so will DOM mutation, which is
 * a different operation from tree construction; this flag deliberately
 * does not lock the document against the DOM phase.
 *
 * Finalization is idempotent, and it is what the tree builder does at the
 * end of a completed build. Construction reopens through
 * robo_html_document_reset, through robo_html_document_attach_source (a
 * new source is about to be parsed), or through the tree builder's own
 * begin — so re-parsing a document is always a deliberate act.
 *
 * @return ROBO_OK; ROBO_NOT_FOUND (unknown document); ROBO_ERROR (the
 *         lifecycle (FAILED/DESTROYED) can no longer accept content).
 */
robo_result_t robo_html_document_finalize(robo_html_document_id_t document_id,
                                          robo_html_diag_t *diag);

/** Non-zero when @p document_id has been finalized. 0 when unknown. */
int robo_html_document_is_finalized(robo_html_document_id_t document_id);

/**
 * Non-zero when tree-construction operations are currently accepted:
 * the document exists, its lifecycle allows content, and construction has
 * not been closed by finalization.
 */
int robo_html_document_allows_construction(
    robo_html_document_id_t document_id);

/**
 * Reset @p document_id to a fresh, empty document without changing its
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
 * @return ROBO_OK; ROBO_NOT_FOUND (unknown document); ROBO_BUSY (a parser
 *         or tree builder still holds the document lease); ROBO_ERROR
 *         (lifecycle cannot accept content, e.g. FAILED).
 */
robo_result_t robo_html_document_reset(robo_html_document_id_t document_id,
                                       robo_html_diag_t *diag);

/**
 * Result of a whole-tree invariant audit (see validate_tree).
 *
 * Every counter is an independent observation, so a report is useful even
 * when it is not clean: it says *what* disagrees, not merely that
 * something does. All zero plus @c root_ok means @c ok.
 */
typedef struct robo_html_tree_report {
    /** Live registry entries at audit time. */
    size_t registered;
    /** Nodes reachable from the root through the child links. */
    size_t nodes;
    /** Deepest node depth below the root (root itself is depth 0). */
    size_t max_depth;
    /** Non-zero when the root handle, kind and parent link are correct. */
    int root_ok;

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
    size_t bad_links;
    size_t cycles;
    /** A child whose parent link is not the node that lists it. */
    size_t bad_parents;
    /** child_count / last_child disagreeing with the sibling chain. */
    size_t bad_counts;
    /** prev_sibling links that do not match the traversal order. */
    size_t sibling_mismatches;

    /** Non-zero when every observation above is clean. */
    int ok;
} robo_html_tree_report_t;

/**
 * Audit the document tree's invariants and report what was found.
 *
 * Checks: the root exists, is a document node and has no parent; every
 * node reachable from the root is reached exactly once (so a link cycle
 * terminates the walk with @c cycles set rather than hanging); every child
 * links back to the parent that lists it; child_count and last_child agree
 * with the child chain; and no live node is unreachable.
 *
 * This is a diagnostic/verification facility, deliberately separate from
 * the read paths so ordinary traversal never pays for a full audit. It is
 * safe on a half-built or malformed tree and allocates one scratch bitmap
 * bounded by ROBO_HTML_MAX_NODES_PER_DOCUMENT.
 *
 * An inconsistent tree is NOT an error return: the audit succeeded, and
 * the verdict is @c out->ok. @p out is zeroed even on failure.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL out); ROBO_NOT_FOUND
 *         (unknown document); ROBO_OUT_OF_MEMORY (scratch allocation).
 */
robo_result_t robo_html_document_validate_tree(
    robo_html_document_id_t document_id,
    robo_html_tree_report_t *out,
    robo_html_diag_t *diag);

/**
 * Destroy @p document and retire its id.
 * Fails any leased parser (see RoboHTMLParser.h), drops the client and
 * mutation hooks, releases all node registry state. DESTROYED is
 * terminal: further calls with the id return ROBO_NOT_FOUND.
 * Double destroy → ROBO_NOT_FOUND (never undefined behavior).
 */
robo_result_t robo_html_document_destroy(robo_html_document_id_t document_id,
                                         robo_html_diag_t *diag);

/** Non-zero when @p id is a live (created, not destroyed) document. */
int robo_html_document_is_live(robo_html_document_id_t document_id);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_DOCUMENT_H */
