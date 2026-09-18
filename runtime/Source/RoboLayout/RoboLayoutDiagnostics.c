/*
 * RoboLayout diagnostics (Phase 10.1) — see RoboLayoutDiagnostics.h.
 */

#include "RoboLayout/RoboLayoutDiagnostics.h"

void robo_layout_diag_reset(robo_layout_diag_t *diag)
{
    if (!diag) {
        return;
    }
    diag->code = ROBO_OK;
    diag->domain = ROBO_LAYOUT_DOMAIN_NONE;
    diag->message = NULL;
    diag->file = NULL;
    diag->line = 0;
}

robo_result_t robo_layout_diag_set(robo_layout_diag_t *diag,
                                   robo_result_t code,
                                   robo_layout_diagnostic_domain_t domain,
                                   const char *message,
                                   const char *file,
                                   int line)
{
    if (!diag) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_result_is_ok(code)) {
        robo_layout_diag_reset(diag);
        return code;
    }
    diag->code = code;
    diag->domain = robo_layout_diagnostic_domain_is_known(domain)
                       ? domain
                       : ROBO_LAYOUT_DOMAIN_NONE;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

robo_result_t robo_layout_diag_record(robo_layout_diag_t *diag,
                                      robo_result_t code,
                                      robo_layout_diagnostic_domain_t domain,
                                      const char *message,
                                      const char *file,
                                      int line)
{
    if (diag) {
        (void)robo_layout_diag_set(diag, code, domain, message, file, line);
    }
    return code;
}
