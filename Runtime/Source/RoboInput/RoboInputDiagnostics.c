/*
 * RoboInput diagnostics (Phase 12.1) — see RoboInputDiagnostics.h.
 *
 * The record and its eight-row vocabulary, copied in shape from RoboDesktop's and RoboRender's own files
 * because the shape is what a caller holding two diags from two modules has to be able to read. The domain
 * names are this file's own, and they name **which boundary refused** rather than which object failed -- a
 * split that only makes sense for this module: an input event crosses four boundaries between a platform and
 * a document, and "the event is malformed" and "the queue is full" send a developer to two entirely
 * different places. `device` is the eighth (13.2): the table a descriptor is connected to is a boundary of
 * its own, because a hotplug refusal and a malformed event are fixed in different places.
 */

#include "RoboInput/RoboInputDiagnostics.h"

#include <stddef.h>

static const char *const inputDomainNames[RoboInputDomainCount] = {
    "none",
    "event",
    "space",
    "queue",
    "target",
    "bridge",
    "dump",
    "device"
};

int InputDomainIsKnown(InputDiagnosticDomain domain)
{
    size_t index = (size_t)domain;

    return index < (size_t)RoboInputDomainCount;
}

const char *InputDomainName(InputDiagnosticDomain domain)
{
    if (!InputDomainIsKnown(domain)) {
        return "unknown";
    }
    return inputDomainNames[(size_t)domain];
}

void InputDiagReset(InputDiag *diag)
{
    if (diag) {
        diag->code = RoboOk;
        diag->domain = RoboInputDomainNone;
        diag->message = NULL;
        diag->file = NULL;
        diag->line = 0;
    }
}

Result InputDiagSet(InputDiag *diag,
                                  Result code,
                                  InputDiagnosticDomain domain,
                                  const char *message,
                                  const char *file,
                                  int line)
{
    if (!diag) {
        return RoboInvalidArgument;
    }
    if (code == RoboOk) {
        InputDiagReset(diag);
        return RoboOk;
    }
    diag->code = code;
    diag->domain = domain;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

Result InputDiagRecord(InputDiag *diag,
                                     Result code,
                                     InputDiagnosticDomain domain,
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
