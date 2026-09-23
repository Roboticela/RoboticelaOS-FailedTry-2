/*
 * RoboText diagnostics (Phase 12.4) — see RoboTextDiagnostics.h.
 *
 * The same seven lines of policy every module in this tree uses, and no state: a record the caller owns,
 * filled by the call that failed. The one decision worth naming is the `IsKnown` guard on the domain — an
 * undefined domain is normalised to `None` rather than stored, so a reader never sees a domain that has
 * no name. RoboPaint and RoboLayout apply the same rule to their own vocabularies.
 */

#include "RoboText/RoboTextDiagnostics.h"

#include <stddef.h>

int RoboTextDomainIsKnown(RoboTextDiagnosticDomain domain)
{
    /*
     * **Not** `>= 0`: `None` is not a domain, it is the *absence* of one, so a caller asking whether its
     * record's domain is known gets "no" for a cleared record. RoboTypography and RoboPaint draw the same
     * line at the same place, which is what makes `IsKnown` mean one thing across the tree.
     */
    return (int)domain > 0 && (int)domain < (int)RoboTextDomainCount;
}

const char *RoboTextDomainName(RoboTextDiagnosticDomain domain)
{
    switch (domain) {
        case RoboTextDomainBuffer:
            return "buffer";
        case RoboTextDomainRange:
            return "range";
        case RoboTextDomainFont:
            return "font";
        case RoboTextDomainRun:
            return "run";
        case RoboTextDomainGlyph:
            return "glyph";
        case RoboTextDomainBidi:
            return "bidi";
        default:
            return "unknown";
    }
}

void RoboTextDiagReset(RoboTextDiag *diag)
{
    if (!diag) {
        return;
    }
    diag->code = RoboOk;
    diag->domain = RoboTextDomainNone;
    diag->message = NULL;
    diag->file = NULL;
    diag->line = 0;
}

Result RoboTextDiagSet(RoboTextDiag *diag,
                       Result code,
                       RoboTextDiagnosticDomain domain,
                       const char *message,
                       const char *file,
                       int line)
{
    if (!diag) {
        return RoboInvalidArgument;
    }
    if (ResultIsOk(code)) {
        RoboTextDiagReset(diag);
        return code;
    }
    diag->code = code;
    diag->domain = RoboTextDomainIsKnown(domain) ? domain : RoboTextDomainNone;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

Result RoboTextDiagRecord(RoboTextDiag *diag,
                          Result code,
                          RoboTextDiagnosticDomain domain,
                          const char *message,
                          const char *file,
                          int line)
{
    if (diag) {
        (void)RoboTextDiagSet(diag, code, domain, message, file, line);
    }
    return code;
}
