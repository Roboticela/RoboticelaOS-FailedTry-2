/*
 * RoboPaint diagnostics (Phase 11.1) — see RoboPaintDiagnostics.h.
 *
 * Six lines of policy and no state: a record the caller owns, filled by the call that
 * failed. The one thing worth noting is the `is_known` guard on the domain: an
 * undefined domain is normalised to `NONE` rather than stored, so a reader never sees a
 * domain that has no name -- the same rule RoboLayout applies to a result flag it does
 * not know.
 */

#include "RoboPaint/RoboPaintDiagnostics.h"

void robo_paint_diag_reset(robo_paint_diag_t *diag)
{
    if (!diag) {
        return;
    }
    diag->code = ROBO_OK;
    diag->domain = ROBO_PAINT_DOMAIN_NONE;
    diag->message = NULL;
    diag->file = NULL;
    diag->line = 0;
}

robo_result_t robo_paint_diag_set(robo_paint_diag_t *diag,
                                  robo_result_t code,
                                  robo_paint_diagnostic_domain_t domain,
                                  const char *message,
                                  const char *file,
                                  int line)
{
    if (!diag) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_result_is_ok(code)) {
        robo_paint_diag_reset(diag);
        return code;
    }
    diag->code = code;
    diag->domain = robo_paint_domain_is_known(domain) ? domain : ROBO_PAINT_DOMAIN_NONE;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

robo_result_t robo_paint_diag_record(robo_paint_diag_t *diag,
                                     robo_result_t code,
                                     robo_paint_diagnostic_domain_t domain,
                                     const char *message,
                                     const char *file,
                                     int line)
{
    if (diag) {
        (void)robo_paint_diag_set(diag, code, domain, message, file, line);
    }
    return code;
}
