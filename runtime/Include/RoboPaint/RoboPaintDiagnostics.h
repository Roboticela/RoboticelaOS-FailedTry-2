/*
 * RoboPaint diagnostics (Phase 11.1).
 *
 * The same shape RoboBase, RoboHTML, RoboCSS and RoboLayout use: every public call
 * returns a plain robo_result_t and an optional caller-owned record refines a failure
 * with a domain, a message and the call site.
 *
 * A diag answers "which call failed and why", and keeping RoboPaint's failures in
 * RoboPaint's own record is what lets a caller tell three different things apart that
 * a single error would merge:
 *
 *   the **document** was wrong -- RoboHTML's error, or RoboCSS's;
 *   the **tree** is not laid out -- RoboLayout's answer, and the one RoboPaint turns
 *   into a *refusal* rather than an empty list (`RoboPaintContext.h`);
 *   the **list** could not grow, or an operation was asked to be something it cannot
 *   be -- this module's, and nobody else can report it.
 *
 * A painter that reported the first two as its own would send a reader to the wrong
 * module; one that reported the third as a layout problem would send them to the
 * wrong phase.
 */
#ifndef ROBO_PAINT_DIAGNOSTICS_H
#define ROBO_PAINT_DIAGNOSTICS_H

#include "RoboBase/RoboError.h"
#include "RoboPaint/RoboPaintTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Caller-owned RoboPaint diagnostic record. `message` is a static string and is never
 * freed by RoboPaint; `file`/`line` name the reporting call site.
 */
typedef struct robo_paint_diag {
    robo_result_t code;
    robo_paint_diagnostic_domain_t domain;
    const char *message; /* static; do not free */
    const char *file;    /* __FILE__ of the reporting call site */
    int line;
} robo_paint_diag_t;

/** Clear @p diag to success / empty. Safe on NULL (no-op). */
void robo_paint_diag_reset(robo_paint_diag_t *diag);

/**
 * Fill @p diag. NULL @p diag -> ROBO_INVALID_ARGUMENT.
 * @p code should normally be a failure; ROBO_OK clears like reset.
 * ROBO_PAINT_DOMAIN_NONE is accepted (a generic RoboPaint failure).
 */
robo_result_t robo_paint_diag_set(robo_paint_diag_t *diag,
                                  robo_result_t code,
                                  robo_paint_diagnostic_domain_t domain,
                                  const char *message,
                                  const char *file,
                                  int line);

/**
 * Non-strict variant used by the ROBO_PAINT_DIAG macro: always returns @p code, filling
 * @p diag only when it is non-NULL. Diagnostics are optional; the result code is
 * authoritative.
 */
robo_result_t robo_paint_diag_record(robo_paint_diag_t *diag,
                                     robo_result_t code,
                                     robo_paint_diagnostic_domain_t domain,
                                     const char *message,
                                     const char *file,
                                     int line);

/**
 * Convenience wrapper used by RoboPaint implementation call sites:
 *   ROBO_PAINT_DIAG(&diag, ROBO_PAINT_DOMAIN_LIST, ROBO_OUT_OF_MEMORY, "...");
 * Evaluates to @p code whether or not @p diag is NULL.
 */
#define ROBO_PAINT_DIAG(diag, dom, code, msg)                                  \
    robo_paint_diag_record((diag), (code), (dom), (msg), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_DIAGNOSTICS_H */
