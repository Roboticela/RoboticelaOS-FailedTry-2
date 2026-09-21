/*
 * RoboLayout diagnostics (Phase 10.1) — see RoboLayoutDiagnostics.h.
 */

#include "RoboLayout/RoboLayoutDiagnostics.h"

void LayoutDiagReset(LayoutDiag *diag)
{
    if (!diag) {
        return;
    }
    diag->code = RoboOk;
    diag->domain = RoboLayoutDomainNone;
    diag->message = NULL;
    diag->file = NULL;
    diag->line = 0;
}

Result LayoutDiagSet(LayoutDiag *diag,
                                   Result code,
                                   LayoutDiagnosticDomain domain,
                                   const char *message,
                                   const char *file,
                                   int line)
{
    if (!diag) {
        return RoboInvalidArgument;
    }
    if (ResultIsOk(code)) {
        LayoutDiagReset(diag);
        return code;
    }
    diag->code = code;
    diag->domain = LayoutDiagnosticDomainIsKnown(domain)
                       ? domain
                       : RoboLayoutDomainNone;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

Result LayoutDiagRecord(LayoutDiag *diag,
                                      Result code,
                                      LayoutDiagnosticDomain domain,
                                      const char *message,
                                      const char *file,
                                      int line)
{
    if (diag) {
        (void)LayoutDiagSet(diag, code, domain, message, file, line);
    }
    return code;
}
