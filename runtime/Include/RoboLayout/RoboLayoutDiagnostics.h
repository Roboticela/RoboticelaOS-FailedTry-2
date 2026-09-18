/*
 * RoboLayout diagnostics (Phase 10.1).
 *
 * The same shape RoboHTML and RoboCSS use: every public call returns a plain
 * robo_result_t, and an optional caller-owned record refines a failure with a
 * domain, a message and the call site. The record is filled by the failing call
 * and owned by the caller; there is no global mutable error state.
 *
 * A diag answers "which call failed and why". It is deliberately *not* the place
 * a layout problem is reported: a malformed document is RoboHTML's error, a
 * malformed declaration is RoboCSS's error, and RoboLayout's failures are its
 * own -- a NULL argument, an id that names no box, a tree that was cleared under
 * a pass. Keeping the three kinds of report in the subsystems that own them is
 * what lets a caller tell "the DOM was wrong" from "layout was asked something
 * impossible".
 */

#ifndef ROBO_LAYOUT_DIAGNOSTICS_H
#define ROBO_LAYOUT_DIAGNOSTICS_H

#include "RoboBase/RoboError.h"
#include "RoboLayout/RoboLayoutTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Caller-owned RoboLayout diagnostic record. `message` is a static string and is
 * never freed by RoboLayout; `file`/`line` name the reporting call site.
 */
typedef struct robo_layout_diag {
    robo_result_t code;
    robo_layout_diagnostic_domain_t domain;
    const char *message; /* static; do not free */
    const char *file;    /* __FILE__ of the reporting call site */
    int line;
} robo_layout_diag_t;

/** Clear @p diag to success / empty. Safe on NULL (no-op). */
void robo_layout_diag_reset(robo_layout_diag_t *diag);

/**
 * Fill @p diag. NULL @p diag -> ROBO_INVALID_ARGUMENT.
 * @p code should normally be a failure; ROBO_OK clears like reset.
 * ROBO_LAYOUT_DOMAIN_NONE is accepted (a generic RoboLayout failure).
 */
robo_result_t robo_layout_diag_set(robo_layout_diag_t *diag,
                                   robo_result_t code,
                                   robo_layout_diagnostic_domain_t domain,
                                   const char *message,
                                   const char *file,
                                   int line);

/**
 * Non-strict variant used by the ROBO_LAYOUT_DIAG macro: always returns @p code,
 * filling @p diag only when it is non-NULL. Diagnostics are optional; the result
 * code is authoritative.
 */
robo_result_t robo_layout_diag_record(robo_layout_diag_t *diag,
                                      robo_result_t code,
                                      robo_layout_diagnostic_domain_t domain,
                                      const char *message,
                                      const char *file,
                                      int line);

/**
 * Convenience wrapper used by RoboLayout implementation call sites:
 *   ROBO_LAYOUT_DIAG(&diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_INVALID_ARGUMENT,
 *                    "parent id does not name a box");
 * Evaluates to @p code whether or not @p diag is NULL.
 */
#define ROBO_LAYOUT_DIAG(diag, dom, code, msg)                                 \
    robo_layout_diag_record((diag), (code), (dom), (msg), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_DIAGNOSTICS_H */
