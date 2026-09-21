/*
 * RoboCSS diagnostics (Phase 9.1).
 *
 * The same discipline RoboHTML uses: every public call returns a plain
 * Result, and an optional caller-owned record refines a failure with a
 * domain, a message and the call site. There is no global mutable error state;
 * the record is filled by the failing call and owned by the caller.
 *
 * A diag answers "which call failed and why". A CssError
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
typedef struct CssDiag {
    Result code;
    CssDiagnosticDomain domain;
    const char *message; /* static; do not free */
    const char *file;    /* __FILE__ of the reporting call site */
    int line;
} CssDiag;

/** Clear @p diag to success / empty. Safe on NULL (no-op). */
void CssDiagReset(CssDiag *diag);

/**
 * Fill @p diag. NULL @p diag → RoboInvalidArgument.
 * @p code should normally be a failure; RoboOk clears like reset.
 * RoboCssDomainNone is accepted (a generic RoboCSS failure).
 */
Result CssDiagSet(CssDiag *diag,
                               Result code,
                               CssDiagnosticDomain domain,
                               const char *message,
                               const char *file,
                               int line);

/**
 * Non-strict variant used by the RoboCssDiag macro: always returns @p code,
 * filling @p diag only when it is non-NULL. Diagnostics are optional; the
 * result code is authoritative.
 */
Result CssDiagRecord(CssDiag *diag,
                                  Result code,
                                  CssDiagnosticDomain domain,
                                  const char *message,
                                  const char *file,
                                  int line);

/**
 * Convenience wrapper used by RoboCSS implementation call sites:
 *   RoboCssDiag(&diag, RoboCssDomainSource, RoboInvalidArgument,
 *                 "offset is past the end of the source");
 * Evaluates to @p code whether or not @p diag is NULL.
 */
#define RoboCssDiag(diag, dom, code, msg)                                    \
    CssDiagRecord((diag), (code), (dom), (msg), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_DIAGNOSTICS_H */
