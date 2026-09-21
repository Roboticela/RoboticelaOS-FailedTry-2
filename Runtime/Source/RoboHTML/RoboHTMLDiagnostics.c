#include "RoboHTML/RoboHTMLDiagnostics.h"

#include <stddef.h>

void HtmlDiagReset(HtmlDiag *diag)
{
    if (!diag) {
        return;
    }
    diag->code = RoboOk;
    diag->domain = RoboHtmlDomainNone;
    diag->message = NULL;
    diag->file = NULL;
    diag->line = 0;
}

Result HtmlDiagSet(HtmlDiag *diag,
                                 Result code,
                                 HtmlDiagnosticDomain domain,
                                 const char *message,
                                 const char *file,
                                 int line)
{
    if (!diag) {
        return RoboInvalidArgument;
    }
    diag->code = code;
    diag->domain = domain;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

Result HtmlDiagRecord(HtmlDiag *diag,
                                    Result code,
                                    HtmlDiagnosticDomain domain,
                                    const char *message,
                                    const char *file,
                                    int line)
{
    if (diag) {
        diag->code = code;
        diag->domain = domain;
        diag->message = message;
        diag->file = file;
        diag->line = line;
    }
    return code;
}
