/*
 * RoboHTML diagnostics (Phase 8.1).
 *
 * Integrates with RoboError / RoboLog conventions: every public call keeps
 * returning plain Result, and an optional caller-owned info record
 * refines failures with a RoboHTML diagnostic domain and message.
 *
 * No global mutable error state — the info record is filled by the failing
 * call and owned by the caller, matching ErrorInfo discipline.
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
typedef struct HtmlDiag {
    Result code;
    HtmlDiagnosticDomain domain;
    const char *message; /* static or document-scoped; do not free */
    const char *file;    /* __FILE__ of the reporting call site */
    int line;
} HtmlDiag;

/** Clear @p diag to success / empty. Safe on NULL (no-op). */
void HtmlDiagReset(HtmlDiag *diag);

/**
 * Fill @p diag. NULL @p diag → RoboInvalidArgument.
 * @p code should normally be a failure; RoboOk clears like reset.
 * @p domain RoboHtmlDomainNone is accepted (generic RoboHTML failure).
 */
Result HtmlDiagSet(HtmlDiag *diag,
                                 Result code,
                                 HtmlDiagnosticDomain domain,
                                 const char *message,
                                 const char *file,
                                 int line);

/**
 * Non-strict variant used by the RoboHtmlDiag macro inside RoboHTML
 * implementations: always returns @p code, filling @p diag only when it is
 * non-NULL (diagnostics are optional; the result code is authoritative).
 */
Result HtmlDiagRecord(HtmlDiag *diag,
                                    Result code,
                                    HtmlDiagnosticDomain domain,
                                    const char *message,
                                    const char *file,
                                    int line);

/**
 * Convenience wrapper used by RoboHTML implementation call sites:
 *   RoboHtmlDiag(&diag, RoboHtmlDomainDocument, RoboInitError,
 *                  "module not initialized");
 * Evaluates to @p code regardless of whether @p diag is NULL.
 */
#define RoboHtmlDiag(diag, dom, code, msg)                                   \
    HtmlDiagRecord((diag), (code), (dom), (msg), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_DIAGNOSTICS_H */
