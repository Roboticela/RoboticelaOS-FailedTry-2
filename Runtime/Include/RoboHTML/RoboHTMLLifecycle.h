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
 *   DESTROYING  teardown in progress (inside HtmlDocumentDestroy).
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
 *   - DESTROYING is entered only through HtmlDocumentDestroy.
 *   - DESTROYED is terminal; the document id is retired (never reused).
 *   - Invalid transitions return RoboInvalidArgument (deterministic) and
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

typedef enum HtmlDocumentLifecycle {
    RoboHtmlDocumentLifecycleUninitialized = 0,
    /** Created and registered; owned state valid; no source attached. */
    RoboHtmlDocumentLifecycleCreated = 1,
    /** Source attached (or attach in progress); input being consumed. */
    RoboHtmlDocumentLifecycleLoading = 2,
    /** Parsing finished; the DOM is live, readable and mutable. */
    RoboHtmlDocumentLifecycleInteractive = 3,
    /** Settled: no further input can arrive. */
    RoboHtmlDocumentLifecycleComplete = 4,
    /** Terminal failure before settling (source/encoding/parser boundary). */
    RoboHtmlDocumentLifecycleFailed = 5,
    /** Teardown in progress (inside HtmlDocumentDestroy). */
    RoboHtmlDocumentLifecycleDestroying = 6,
    /** Destroyed; id retired; handle is stale. */
    RoboHtmlDocumentLifecycleDestroyed = 7
} HtmlDocumentLifecycle;

/** Non-zero if @p life is a defined lifecycle enumerator. */
int HtmlDocumentLifecycleIsKnown(HtmlDocumentLifecycle life);

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
int HtmlDocumentLifecycleCanTransition(
    HtmlDocumentLifecycle from,
    HtmlDocumentLifecycle to);

/**
 * Validate a transition. RoboOk or RoboInvalidArgument.
 * DESTROYED → DESTROYED returns RoboOk (idempotent stay).
 */
Result HtmlDocumentLifecycleCheckTransition(
    HtmlDocumentLifecycle from,
    HtmlDocumentLifecycle to);

/** Human-readable lifecycle name; never NULL; "unknown" when undefined. */
const char *HtmlDocumentLifecycleName(
    HtmlDocumentLifecycle life);

/**
 * Non-zero if content/mutation operations are allowed in @p life.
 * CREATED / LOADING / INTERACTIVE / COMPLETE — that is, everything except
 * the failure and teardown states. Completion never closes the DOM.
 */
int HtmlDocumentLifecycleAllowsContent(
    HtmlDocumentLifecycle life);

/**
 * Non-zero if source may be attached in @p life.
 * CREATED (first source), INTERACTIVE and COMPLETE (replacement source).
 * A LOADING document already has input in flight, so attaching again is
 * RoboBusy rather than a silent discard.
 */
int HtmlDocumentLifecycleAllowsSourceAttach(
    HtmlDocumentLifecycle life);

/**
 * Non-zero if @p life is a settled state: the document has a live DOM and
 * no input in flight (INTERACTIVE or COMPLETE).
 */
int HtmlDocumentLifecycleIsSettled(
    HtmlDocumentLifecycle life);

/** Non-zero if @p life is a failure or teardown state. */
int HtmlDocumentLifecycleIsTerminal(
    HtmlDocumentLifecycle life);

/** Non-zero if @p life is the destroyed (retired) state. */
int HtmlDocumentLifecycleIsDestroyed(
    HtmlDocumentLifecycle life);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_LIFECYCLE_H */
