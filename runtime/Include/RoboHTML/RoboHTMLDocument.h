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
 * CREATED → LOADING. An empty source is valid and completes immediately
 * back to READY with the document still empty.
 * Replacement on READY → LOADING is allowed (architecture for later
 * phases). While LOADING → ROBO_BUSY.
 * The source bytes must stay valid until detach/destroy (borrow view).
 */
robo_result_t robo_html_document_attach_source(
    robo_html_document_id_t document_id,
    const robo_html_source_t *src,
    robo_html_diag_t *diag);

/**
 * Detach any attached source and return a LOADING document to READY.
 * No-op with ROBO_OK when nothing is attached. Encoding state resets to
 * the boundary defaults.
 */
robo_result_t robo_html_document_detach_source(
    robo_html_document_id_t document_id,
    robo_html_diag_t *diag);

/**
 * Mark @p document READY (LOADING → READY). Empty-source attachment
 * performs this automatically; later phases call it when parsing
 * completes. READY → READY is an error.
 */
robo_result_t robo_html_document_mark_ready(
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
