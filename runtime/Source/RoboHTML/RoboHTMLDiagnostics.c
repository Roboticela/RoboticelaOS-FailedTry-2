#include "RoboHTML/RoboHTMLDiagnostics.h"

#include <stddef.h>

void robo_html_diag_reset(robo_html_diag_t *diag)
{
    if (!diag) {
        return;
    }
    diag->code = ROBO_OK;
    diag->domain = ROBO_HTML_DOMAIN_NONE;
    diag->message = NULL;
    diag->file = NULL;
    diag->line = 0;
}

robo_result_t robo_html_diag_set(robo_html_diag_t *diag,
                                 robo_result_t code,
                                 robo_html_diagnostic_domain_t domain,
                                 const char *message,
                                 const char *file,
                                 int line)
{
    if (!diag) {
        return ROBO_INVALID_ARGUMENT;
    }
    diag->code = code;
    diag->domain = domain;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

robo_result_t robo_html_diag_record(robo_html_diag_t *diag,
                                    robo_result_t code,
                                    robo_html_diagnostic_domain_t domain,
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
