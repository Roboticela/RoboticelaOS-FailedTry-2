/*
 * RoboHTML document lifecycle state machine (Phase 8.1).
 *
 * The document owns exactly one lifecycle:
 *
 *   UNINITIALIZED → CREATED → LOADING → READY → DESTROYING → DESTROYED
 *                        │         │
 *                        │         └──→ FAILED → DESTROYING → DESTROYED
 *                        └───────────────→ DESTROYING → DESTROYED
 *
 * Phase 8.1 constructs documents only as far as READY-with-empty-source
 * (an empty document is immediately ready); LOADING / FAILED states exist
 * in the model so 8.2/8.3 and the resource boundary can drive them without
 * redesigning the state machine.
 *
 * Rules:
 *   - DESTROYING is entered only through robo_html_document_destroy.
 *   - DESTROYED is terminal; the document id is retired (never reused).
 *   - Invalid transitions return ROBO_INVALID_ARGUMENT (deterministic).
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
    /** Source attached (or attach in progress in later phases). */
    ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING = 2,
    /** Document is valid and idle; parsing produced the current content. */
    ROBO_HTML_DOCUMENT_LIFECYCLE_READY = 3,
    /** Terminal failure before ready (source/encoding/parser boundary). */
    ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED = 4,
    /** Teardown in progress (inside robo_html_document_destroy). */
    ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING = 5,
    /** Destroyed; id retired; handle is stale. */
    ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED = 6
} robo_html_document_lifecycle_t;

/** Non-zero if @p life is a defined lifecycle enumerator. */
int robo_html_document_lifecycle_is_known(robo_html_document_lifecycle_t life);

/**
 * Non-zero if transition @p from → @p to is allowed by the state machine.
 * Allowed edges:
 *   UNINITIALIZED → CREATED
 *   CREATED       → LOADING, FAILED, DESTROYING
 *   LOADING       → READY, FAILED, DESTROYING
 *   READY         → LOADING (new source), DESTROYING
 *   FAILED        → DESTROYING
 *   DESTROYING    → DESTROYED
 *   DESTROYED     → DESTROYED (idempotent no-op edge)
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
 * Only CREATED / LOADING / READY (not FAILED / DESTROYING / DESTROYED /
 * UNINITIALIZED).
 */
int robo_html_document_lifecycle_allows_content(
    robo_html_document_lifecycle_t life);

/**
 * Non-zero if source may be attached in @p life.
 * CREATED (first source) and READY (replacement source) only.
 */
int robo_html_document_lifecycle_allows_source_attach(
    robo_html_document_lifecycle_t life);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_LIFECYCLE_H */
