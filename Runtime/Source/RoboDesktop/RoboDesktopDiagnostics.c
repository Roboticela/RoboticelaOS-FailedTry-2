/*
 * RoboDesktop diagnostics (Phase 11.13) — see RoboDesktopDiagnostics.h.
 *
 * The record and its six-row vocabulary, copied in shape from RoboRender's own file because the shape is
 * what a caller holding two diags from two modules has to be able to read. The domain names are this
 * file's own and they are the object a failure came from rather than the stage a frame was in -- which is
 * the one place this module's vocabulary differs from the two below it, and for a reason: a document
 * layer's failure is *not* a desktop failure, and a caller that saw "the desktop failed" for a markup
 * error it wrote would go looking in this module for something that is not here.
 */

#include "RoboDesktop/RoboDesktopDiagnostics.h"

static const char *const desktopDomainNames[RoboDesktopDomainCount] = {
    "none",
    "shell",
    "window",
    "surface",
    "document",
    "dump"
};

int DesktopDomainIsKnown(DesktopDiagnosticDomain domain)
{
    size_t index = (size_t)domain;

    return index < (size_t)RoboDesktopDomainCount;
}

const char *DesktopDomainName(DesktopDiagnosticDomain domain)
{
    if (!DesktopDomainIsKnown(domain)) {
        return "unknown";
    }
    return desktopDomainNames[(size_t)domain];
}

void DesktopDiagReset(DesktopDiag *diag)
{
    if (diag) {
        diag->code = RoboOk;
        diag->domain = RoboDesktopDomainNone;
        diag->message = NULL;
        diag->file = NULL;
        diag->line = 0;
    }
}

Result DesktopDiagSet(DesktopDiag *diag,
                                   Result code,
                                   DesktopDiagnosticDomain domain,
                                   const char *message,
                                   const char *file,
                                   int line)
{
    if (!diag) {
        return RoboInvalidArgument;
    }
    if (code == RoboOk) {
        DesktopDiagReset(diag);
        return RoboOk;
    }
    diag->code = code;
    diag->domain = domain;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

Result DesktopDiagRecord(DesktopDiag *diag,
                                      Result code,
                                      DesktopDiagnosticDomain domain,
                                      const char *message,
                                      const char *file,
                                      int line)
{
    if (diag && code != RoboOk) {
        diag->code = code;
        diag->domain = domain;
        diag->message = message;
        diag->file = file;
        diag->line = line;
    }
    return code;
}
