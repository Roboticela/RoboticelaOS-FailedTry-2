/*
 * RoboCSS diagnostics (Phase 9.1).
 *
 * The same discipline RoboHTML uses: every public call returns a plain
 * robo_result_t, and an optional caller-owned record refines a failure with a
 * domain, a message and the call site. There is no global mutable error state;
 * the record is filled by the failing call and owned by the caller.
 *
 * A diag answers "which call failed and why". A robo_css_error_t
 * (RoboCSSError.h) answers "what is wrong with this CSS" and is the record the
 * parser hands back for input problems, including the recoverable ones that
 * are not failures at all. A call can therefore fail with a diag and no CSS
 * error (a NULL argument), and a parse can hand back a recoverable CSS error
 * with no failure (malformed CSS that still produced a stylesheet).
 */

#ifndef ROBO_CSS_DIAGNOSTICS_H
#define ROBO_CSS_DIAGNOSTICS_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Caller-owned RoboCSS diagnostic record. `message` is a static string and is
 * never freed by RoboCSS; `file`/`line` name the reporting call site.
 */
typedef struct robo_css_diag {
    robo_result_t code;
    robo_css_diagnostic_domain_t domain;
    const char *message; /* static; do not free */
    const char *file;    /* __FILE__ of the reporting call site */
    int line;
} robo_css_diag_t;

/** Clear @p diag to success / empty. Safe on NULL (no-op). */
void robo_css_diag_reset(robo_css_diag_t *diag);

/**
 * Fill @p diag. NULL @p diag → ROBO_INVALID_ARGUMENT.
 * @p code should normally be a failure; ROBO_OK clears like reset.
 * ROBO_CSS_DOMAIN_NONE is accepted (a generic RoboCSS failure).
 */
robo_result_t robo_css_diag_set(robo_css_diag_t *diag,
                               robo_result_t code,
                               robo_css_diagnostic_domain_t domain,
                               const char *message,
                               const char *file,
                               int line);

/**
 * Non-strict variant used by the ROBO_CSS_DIAG macro: always returns @p code,
 * filling @p diag only when it is non-NULL. Diagnostics are optional; the
 * result code is authoritative.
 */
robo_result_t robo_css_diag_record(robo_css_diag_t *diag,
                                  robo_result_t code,
                                  robo_css_diagnostic_domain_t domain,
                                  const char *message,
                                  const char *file,
                                  int line);

/**
 * Convenience wrapper used by RoboCSS implementation call sites:
 *   ROBO_CSS_DIAG(&diag, ROBO_CSS_DOMAIN_SOURCE, ROBO_INVALID_ARGUMENT,
 *                 "offset is past the end of the source");
 * Evaluates to @p code whether or not @p diag is NULL.
 */
#define ROBO_CSS_DIAG(diag, dom, code, msg)                                    \
    robo_css_diag_record((diag), (code), (dom), (msg), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_DIAGNOSTICS_H */
