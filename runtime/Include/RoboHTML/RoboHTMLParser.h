/*
 * RoboHTML parser boundary (Phase 8.1) — architecture only.
 *
 * The seam between HTML source and the future tokenizer (8.2) /
 * tree builder (8.3):
 *
 *   robo_html_source_t (RoboHTMLSource.h)
 *        │  attach
 *        ▼
 *   robo_html_parser_t  ── feeds ──▶  document node registry (RoboHTMLNode.h)
 *        │                             + future DOM (RoboHTMLDom.h)
 *        ▼
 *   parser state machine (see below)
 *
 * Design decisions frozen here so later phases plug in without redesign:
 *   - One parser serves one document at a time (exclusive lease).
 *   - Parsers are heap objects created against an existing document;
 *     they never outlive their document (document destroy fails a live
 *     parser lease by retiring the parser first).
 *   - Input modes distinguish COMPLETE (whole source attached once) from
 *     INCREMENTAL (chunks appended across calls; 8.2+).
 *   - The parser never decodes text; decoded input arrives through the
 *     encoding boundary (RoboHTMLEncoding.h).
 *
 * No tokenizer states, no character references, no parsing algorithms.
 */

#ifndef ROBO_HTML_PARSER_H
#define ROBO_HTML_PARSER_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_html_parser_mode {
    ROBO_HTML_PARSER_MODE_NONE = 0,
    /** Whole source supplied once; parse runs to completion. */
    ROBO_HTML_PARSER_MODE_COMPLETE = 1,
    /** Source appended in chunks (Phase 8.2+). */
    ROBO_HTML_PARSER_MODE_INCREMENTAL = 2
} robo_html_parser_mode_t;

/** Non-zero if @p mode is a defined parser mode enumerator. */
int robo_html_parser_mode_is_known(robo_html_parser_mode_t mode);

/** Human-readable parser mode name; never NULL. */
const char *robo_html_parser_mode_name(robo_html_parser_mode_t mode);

typedef enum robo_html_parser_state {
    ROBO_HTML_PARSER_STATE_IDLE = 0,
    /** Source attached; input pending (COMPLETE: ready to feed 8.2). */
    ROBO_HTML_PARSER_STATE_FEEDING = 1,
    /** Input exhausted; tree builder may finalize (8.3). */
    ROBO_HTML_PARSER_STATE_COMPLETE = 2,
    /** Parser hit a boundary failure (diagnostics carry the domain). */
    ROBO_HTML_PARSER_STATE_FAILED = 3,
    /** Detached from its document (parser object still owned by caller). */
    ROBO_HTML_PARSER_STATE_DETACHED = 4
} robo_html_parser_state_t;

/** Non-zero if @p state is a defined parser state enumerator. */
int robo_html_parser_state_is_known(robo_html_parser_state_t state);

/** Human-readable parser state name; never NULL. */
const char *robo_html_parser_state_name(robo_html_parser_state_t state);

/**
 * Opaque parser object. Layout is private (RoboHTMLInternal.h); public
 * code only holds the pointer. NULL is never a valid parser.
 */
typedef struct robo_html_parser robo_html_parser_t;

/**
 * Create a parser leased to @p document.
 * The document must be in CREATED, LOADING or READY lifecycle - i.e. any
 * state that accepts content. LOADING qualifies because it is the state
 * that means "a source is attached and awaiting parsing". The parser starts
 * in STATE_IDLE with MODE_NONE; attach_source picks the mode.
 * Ownership: caller owns the parser object and must eventually call
 * robo_html_parser_destroy. Destroying the document while a parser is
 * leased is rejected with ROBO_BUSY (lease must be released first).
 */
robo_result_t robo_html_parser_create(robo_html_document_id_t document_id,
                                      robo_html_parser_t **out_parser,
                                      robo_html_diag_t *diag);

/**
 * Attach @p src and transition to STATE_FEEDING.
 * COMPLETE sources move through FEEDING → COMPLETE on finish (8.2+);
 * in 8.1 attaching a COMPLETE source marks the document LOADING and the
 * parser stays FEEDING — feeding/finalization are later-phase work.
 * Re-attach while FEEDING → ROBO_BUSY. After FAILED → ROBO_INVALID_STATE
 * semantics via ROBO_ERROR (diagnostics record the parser domain).
 */
robo_result_t robo_html_parser_attach_source(robo_html_parser_t *parser,
                                             const robo_html_source_t *src,
                                             robo_html_diag_t *diag);

/**
 * Detach the parser from its document and release the document lease.
 * Document returns to READY (from LOADING). Parser → STATE_DETACHED.
 * Idempotent for an already-detached parser (ROBO_OK).
 */
robo_result_t robo_html_parser_detach(robo_html_parser_t *parser,
                                      robo_html_diag_t *diag);

/** Copy current parser state; NULL parser → ROBO_INVALID_ARGUMENT. */
robo_result_t robo_html_parser_get_state(const robo_html_parser_t *parser,
                                         robo_html_parser_state_t *out_state);

/** Copy current parser mode; NULL parser → ROBO_INVALID_ARGUMENT. */
robo_result_t robo_html_parser_get_mode(const robo_html_parser_t *parser,
                                        robo_html_parser_mode_t *out_mode);

/** Owning document id of @p parser (NONE when detached); NULL → NONE. */
robo_html_document_id_t robo_html_parser_document(
    const robo_html_parser_t *parser);

/**
 * Destroy a parser created by robo_html_parser_create. Detaches first if
 * still leased. Safe on NULL. After destroy the pointer must not be used.
 */
void robo_html_parser_destroy(robo_html_parser_t *parser);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_PARSER_H */
