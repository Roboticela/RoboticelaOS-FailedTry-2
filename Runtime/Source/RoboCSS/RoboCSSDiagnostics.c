/*
 * RoboCSS diagnostics (Phase 9.1) — see RoboCSSDiagnostics.h.
 */

#include "RoboCSS/RoboCSSDiagnostics.h"

void CssDiagReset(CssDiag *diag)
{
    if (!diag) {
        return;
    }
    diag->code = RoboOk;
    diag->domain = RoboCssDomainNone;
    diag->message = NULL;
    diag->file = NULL;
    diag->line = 0;
}

Result CssDiagSet(CssDiag *diag,
                               Result code,
                               CssDiagnosticDomain domain,
                               const char *message,
                               const char *file,
                               int line)
{
    if (!diag) {
        return RoboInvalidArgument;
    }
    if (ResultIsOk(code)) {
        CssDiagReset(diag);
        return code;
    }
    diag->code = code;
    diag->domain = CssDiagnosticDomainIsKnown(domain) ? domain
                                                               : RoboCssDomainNone;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

Result CssDiagRecord(CssDiag *diag,
                                  Result code,
                                  CssDiagnosticDomain domain,
                                  const char *message,
                                  const char *file,
                                  int line)
{
    if (diag) {
        (void)CssDiagSet(diag, code, domain, message, file, line);
    }
    return code;
}
