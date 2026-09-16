/*
 * RoboHTML document lifecycle state machine (Phase 8.1, extended by 8.10).
 *
 * The document owns exactly one lifecycle:
 *
 *   UNINITIALIZED → CREATED → LOADING → INTERACTIVE → COMPLETE
 *                        │         │           │          │
 *                        └─────────┴───────────┴──────────┴──→ FAILED
 *                                                                 │
 *                                    ┌────────────────────────────┘
 *                                    ▼
 *                              DESTROYING → DESTROYED
 *
 * The vocabulary is the web platform's own document readyState, because
 * that is the state a document is in and callers already know what it
 * means:
 *
 *   CREATED     registered and owned, no source attached, nothing parsed.
 *   LOADING     input has been attached and is being consumed. The tree
 *               may be partially built.
 *   INTERACTIVE parsing has finished; the DOM is live and complete enough
 *               to read, traverse and mutate. This is the state a document
 *               fed from a stream settles in, because more input may
 *               still arrive.
 *   COMPLETE    the document has settled and no further input can arrive:
 *               the attached source was complete, or nothing was ever
 *               attached. Reading, traversal and mutation all continue to
 *               work exactly as in INTERACTIVE — completion describes
 *               input, not permission.
 *   FAILED      a boundary failed before the document settled. Sticky.
 *   DESTROYING  teardown in progress (inside robo_html_document_destroy).
 *   DESTROYED   terminal; the document id is retired and never reused.
 *
 * Phase 8.10 rules:
 *   - The only way to reach INTERACTIVE is for a parse to finish (or for
 *     a source to be detached / an empty source to settle), so
 *     COMPLETE can never be reached without passing through it. A
 *     document that has not finished parsing cannot be completed.
 *   - COMPLETE and INTERACTIVE differ only by what may arrive next, never
 *     by what may be done: allows_content and allows_source_attach are
 *     true for both.
 *   - DESTROYING is entered only through robo_html_document_destroy.
 *   - DESTROYED is terminal; the document id is retired (never reused).
 *   - Invalid transitions return ROBO_INVALID_ARGUMENT (deterministic) and
 *     leave the current state untouched.
 *   - FAILED is sticky until destroy; a failed document rejects further
 *     source attachment but still supports destruction and diagnostics.
 */

#ifndef ROBO_HTML_LIFECYCLE_H
#define ROBO_HTML_LIFECYCLE_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_html_document_lifecycle {
    ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED = 0,
    /** Created and registered; owned state valid; no source attached. */
    ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED = 1,
    /** Source attached (or attach in progress); input being consumed. */
    ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING = 2,
    /** Parsing finished; the DOM is live, readable and mutable. */
    ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE = 3,
    /** Settled: no further input can arrive. */
    ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE = 4,
    /** Terminal failure before settling (source/encoding/parser boundary). */
    ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED = 5,
    /** Teardown in progress (inside robo_html_document_destroy). */
    ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING = 6,
    /** Destroyed; id retired; handle is stale. */
    ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED = 7
} robo_html_document_lifecycle_t;

/** Non-zero if @p life is a defined lifecycle enumerator. */
int robo_html_document_lifecycle_is_known(robo_html_document_lifecycle_t life);

/**
 * Non-zero if transition @p from → @p to is allowed by the state machine.
 * Allowed edges:
 *   UNINITIALIZED → CREATED
 *   CREATED       → LOADING, FAILED, DESTROYING
 *   LOADING       → INTERACTIVE, FAILED, DESTROYING
 *   INTERACTIVE   → COMPLETE, LOADING (replacement source), FAILED,
 *                   DESTROYING
 *   COMPLETE      → LOADING (reload), FAILED, DESTROYING
 *   FAILED        → DESTROYING
 *   DESTROYING    → DESTROYED
 *   DESTROYED     → DESTROYED (idempotent no-op edge)
 *
 * Note the two edges that only exist in one direction: LOADING →
 * INTERACTIVE (a parse finished) and INTERACTIVE → COMPLETE (input
 * settled). There is no COMPLETE → INTERACTIVE and no LOADING →
 * COMPLETE: completion is a one-way consequence of having parsed, so
 * neither a half-parsed document nor a settled one can fake it.
 */
int robo_html_document_lifecycle_can_transition(
    robo_html_document_lifecycle_t from,
    robo_html_document_lifecycle_t to);

/**
 * Validate a transition. ROBO_OK or ROBO_INVALID_ARGUMENT.
 * DESTROYED → DESTROYED returns ROBO_OK (idempotent stay).
 */
robo_result_t robo_html_document_lifecycle_check_transition(
    robo_html_document_lifecycle_t from,
    robo_html_document_lifecycle_t to);

/** Human-readable lifecycle name; never NULL; "unknown" when undefined. */
const char *robo_html_document_lifecycle_name(
    robo_html_document_lifecycle_t life);

/**
 * Non-zero if content/mutation operations are allowed in @p life.
 * CREATED / LOADING / INTERACTIVE / COMPLETE — that is, everything except
 * the failure and teardown states. Completion never closes the DOM.
 */
int robo_html_document_lifecycle_allows_content(
    robo_html_document_lifecycle_t life);

/**
 * Non-zero if source may be attached in @p life.
 * CREATED (first source), INTERACTIVE and COMPLETE (replacement source).
 * A LOADING document already has input in flight, so attaching again is
 * ROBO_BUSY rather than a silent discard.
 */
int robo_html_document_lifecycle_allows_source_attach(
    robo_html_document_lifecycle_t life);

/**
 * Non-zero if @p life is a settled state: the document has a live DOM and
 * no input in flight (INTERACTIVE or COMPLETE).
 */
int robo_html_document_lifecycle_is_settled(
    robo_html_document_lifecycle_t life);

/** Non-zero if @p life is a failure or teardown state. */
int robo_html_document_lifecycle_is_terminal(
    robo_html_document_lifecycle_t life);

/** Non-zero if @p life is the destroyed (retired) state. */
int robo_html_document_lifecycle_is_destroyed(
    robo_html_document_lifecycle_t life);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_LIFECYCLE_H */
