/*
 * RoboTypography diagnostics (Phase 12.3).
 *
 * The shape every module in this tree uses: a public call returns a plain `Result` and an optional
 * caller-owned record refines a failure with a domain, a message and the call site. The domains are
 * organised by **which object refused**, because the four send a reader to four different owners:
 *
 *   role       a role name or a role value this build does not define -- the caller's vocabulary;
 *   scale      a role this scale has no entry for; a scale whose numbers are not a type setting
 *              this build can draw;
 *   resolver   the resolver itself: no pack bound, a resolve with no eligible face;
 *   document   the generated document defaults: a sheet that did not fit the caller's buffer, or an
 *              element this build's default vocabulary does not name.
 */
#ifndef ROBO_TYPOGRAPHY_DIAGNOSTICS_H
#define ROBO_TYPOGRAPHY_DIAGNOSTICS_H

#include "RoboBase/RoboError.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Which object a typography failure came from. */
typedef enum TypographyDiagnosticDomain {
    /** Nothing: a cleared record's value. */
    RoboTypographyDomainNone = 0,
    /** A role name or value. */
    RoboTypographyDomainRole = 1,
    /** The centralized scale. */
    RoboTypographyDomainScale = 2,
    /** The resolver. */
    RoboTypographyDomainResolver = 3,
    /** Generated document defaults. */
    RoboTypographyDomainDocument = 4
} TypographyDiagnosticDomain;

/** Number of defined domains. Asserted positionally by `typography.conformance.*`. */
#define RoboTypographyDomainCount 5u

/** Non-zero if @p domain is a defined domain. */
int RoboTypographyDomainIsKnown(TypographyDiagnosticDomain domain);

/** Human-readable domain name; never NULL; `"unknown"` when undefined. */
const char *RoboTypographyDomainName(TypographyDiagnosticDomain domain);

/** Caller-owned record. `message` is static and never freed; `file`/`line` name the reporting site. */
typedef struct TypographyDiag {
    Result code;
    TypographyDiagnosticDomain domain;
    const char *message; /* static; do not free */
    const char *file;
    int line;
} TypographyDiag;

/** Clear @p diag to success / empty. Safe on NULL. */
void RoboTypographyDiagReset(TypographyDiag *diag);

/** Record @p code. @p code == RoboOk clears. NULL @p diag -> RoboInvalidArgument. */
Result RoboTypographyDiagSet(TypographyDiag *diag,
                             Result code,
                             TypographyDiagnosticDomain domain,
                             const char *message,
                             const char *file,
                             int line);

/** Non-strict variant used by the macro: always returns @p code. */
Result RoboTypographyDiagRecord(TypographyDiag *diag,
                                Result code,
                                TypographyDiagnosticDomain domain,
                                const char *message,
                                const char *file,
                                int line);

/** Implementation-site wrapper; evaluates to @p code whether or not @p diag is NULL. */
#define RoboTypographyDiag(diag, dom, code, msg)                                  \
    RoboTypographyDiagRecord((diag), (code), (dom), (msg), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TYPOGRAPHY_DIAGNOSTICS_H */
