/*
 * RoboRender diagnostics (Phase 11.12) — see RoboRenderDiagnostics.h.
 *
 * The record and its five-row vocabulary, copied in shape from RoboPaint's own file because the shape
 * is what a caller holding two diags from two modules has to be able to read: the same fields, the
 * same `__FILE__`/`__LINE__` capture, the same "the result code is authoritative and the record
 * refines it" rule.
 *
 * The domain names are the one thing that is this file's own, and they are deliberately **lowercase
 * in the dump and hyphenated where a name is two words** (`frame`, `list`, `target`, `dump`): a
 * domain appears inside a message a person reads, and a name that reads like an enumerator in a log
 * line is a name that gets skimmed rather than read.
 */

#include "RoboRender/RoboRenderDiagnostics.h"

static const char *const renderDomainNames[RoboRenderDomainCount] = {
    "none",
    "target",
    "list",
    "frame",
    "dump"
};

int RenderDomainIsKnown(RenderDiagnosticDomain domain)
{
    size_t index = (size_t)domain;

    return index < (size_t)RoboRenderDomainCount;
}

const char *RenderDomainName(RenderDiagnosticDomain domain)
{
    if (!RenderDomainIsKnown(domain)) {
        return "unknown";
    }
    return renderDomainNames[(size_t)domain];
}

void RenderDiagReset(RenderDiag *diag)
{
    if (diag) {
        diag->code = RoboOk;
        diag->domain = RoboRenderDomainNone;
        diag->message = NULL;
        diag->file = NULL;
        diag->line = 0;
    }
}

Result RenderDiagSet(RenderDiag *diag,
                                   Result code,
                                   RenderDiagnosticDomain domain,
                                   const char *message,
                                   const char *file,
                                   int line)
{
    if (!diag) {
        return RoboInvalidArgument;
    }
    if (code == RoboOk) {
        RenderDiagReset(diag);
        return RoboOk;
    }
    diag->code = code;
    diag->domain = domain;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

Result RenderDiagRecord(RenderDiag *diag,
                                      Result code,
                                      RenderDiagnosticDomain domain,
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
