/*
 * RoboHTML diagnostics (Phase 8.1).
 *
 * Integrates with RoboError / RoboLog conventions: every public call keeps
 * returning plain robo_result_t, and an optional caller-owned info record
 * refines failures with a RoboHTML diagnostic domain and message.
 *
 * No global mutable error state — the info record is filled by the failing
 * call and owned by the caller, matching robo_error_info_t discipline.
 */

#ifndef ROBO_HTML_DIAGNOSTICS_H
#define ROBO_HTML_DIAGNOSTICS_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Caller-owned RoboHTML diagnostic record (non-owning string fields,
 * typically string literals or document-internal labels valid until the
 * next call on the same document).
 */
typedef struct robo_html_diag {
    robo_result_t code;
    robo_html_diagnostic_domain_t domain;
    const char *message; /* static or document-scoped; do not free */
    const char *file;    /* __FILE__ of the reporting call site */
    int line;
} robo_html_diag_t;

/** Clear @p diag to success / empty. Safe on NULL (no-op). */
void robo_html_diag_reset(robo_html_diag_t *diag);

/**
 * Fill @p diag. NULL @p diag → ROBO_INVALID_ARGUMENT.
 * @p code should normally be a failure; ROBO_OK clears like reset.
 * @p domain ROBO_HTML_DOMAIN_NONE is accepted (generic RoboHTML failure).
 */
robo_result_t robo_html_diag_set(robo_html_diag_t *diag,
                                 robo_result_t code,
                                 robo_html_diagnostic_domain_t domain,
                                 const char *message,
                                 const char *file,
                                 int line);

/**
 * Non-strict variant used by the ROBO_HTML_DIAG macro inside RoboHTML
 * implementations: always returns @p code, filling @p diag only when it is
 * non-NULL (diagnostics are optional; the result code is authoritative).
 */
robo_result_t robo_html_diag_record(robo_html_diag_t *diag,
                                    robo_result_t code,
                                    robo_html_diagnostic_domain_t domain,
                                    const char *message,
                                    const char *file,
                                    int line);

/**
 * Convenience wrapper used by RoboHTML implementation call sites:
 *   ROBO_HTML_DIAG(&diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_INIT_ERROR,
 *                  "module not initialized");
 * Evaluates to @p code regardless of whether @p diag is NULL.
 */
#define ROBO_HTML_DIAG(diag, dom, code, msg)                                   \
    robo_html_diag_record((diag), (code), (dom), (msg), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_DIAGNOSTICS_H */
