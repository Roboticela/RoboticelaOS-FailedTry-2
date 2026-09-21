/*
 * RoboPaint diagnostics (Phase 11.1) — see RoboPaintDiagnostics.h.
 *
 * Six lines of policy and no state: a record the caller owns, filled by the call that
 * failed. The one thing worth noting is the `IsKnown` guard on the domain: an
 * undefined domain is normalised to `NONE` rather than stored, so a reader never sees a
 * domain that has no name -- the same rule RoboLayout applies to a result flag it does
 * not know.
 */

#include "RoboPaint/RoboPaintDiagnostics.h"

void PaintDiagReset(PaintDiag *diag)
{
    if (!diag) {
        return;
    }
    diag->code = RoboOk;
    diag->domain = RoboPaintDomainNone;
    diag->message = NULL;
    diag->file = NULL;
    diag->line = 0;
}

Result PaintDiagSet(PaintDiag *diag,
                                  Result code,
                                  PaintDiagnosticDomain domain,
                                  const char *message,
                                  const char *file,
                                  int line)
{
    if (!diag) {
        return RoboInvalidArgument;
    }
    if (ResultIsOk(code)) {
        PaintDiagReset(diag);
        return code;
    }
    diag->code = code;
    diag->domain = PaintDomainIsKnown(domain) ? domain : RoboPaintDomainNone;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

Result PaintDiagRecord(PaintDiag *diag,
                                     Result code,
                                     PaintDiagnosticDomain domain,
                                     const char *message,
                                     const char *file,
                                     int line)
{
    if (diag) {
        (void)PaintDiagSet(diag, code, domain, message, file, line);
    }
    return code;
}
