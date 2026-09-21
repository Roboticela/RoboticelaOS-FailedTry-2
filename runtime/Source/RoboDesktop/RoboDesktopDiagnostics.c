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

static const char *const desktop_domain_names[ROBO_DESKTOP_DOMAIN_COUNT] = {
    "none",
    "shell",
    "window",
    "surface",
    "document",
    "dump"
};

int robo_desktop_domain_is_known(robo_desktop_diagnostic_domain_t domain)
{
    size_t index = (size_t)domain;

    return index < (size_t)ROBO_DESKTOP_DOMAIN_COUNT;
}

const char *robo_desktop_domain_name(robo_desktop_diagnostic_domain_t domain)
{
    if (!robo_desktop_domain_is_known(domain)) {
        return "unknown";
    }
    return desktop_domain_names[(size_t)domain];
}

void robo_desktop_diag_reset(robo_desktop_diag_t *diag)
{
    if (diag) {
        diag->code = ROBO_OK;
        diag->domain = ROBO_DESKTOP_DOMAIN_NONE;
        diag->message = NULL;
        diag->file = NULL;
        diag->line = 0;
    }
}

robo_result_t robo_desktop_diag_set(robo_desktop_diag_t *diag,
                                   robo_result_t code,
                                   robo_desktop_diagnostic_domain_t domain,
                                   const char *message,
                                   const char *file,
                                   int line)
{
    if (!diag) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (code == ROBO_OK) {
        robo_desktop_diag_reset(diag);
        return ROBO_OK;
    }
    diag->code = code;
    diag->domain = domain;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

robo_result_t robo_desktop_diag_record(robo_desktop_diag_t *diag,
                                      robo_result_t code,
                                      robo_desktop_diagnostic_domain_t domain,
                                      const char *message,
                                      const char *file,
                                      int line)
{
    if (diag && code != ROBO_OK) {
        diag->code = code;
        diag->domain = domain;
        diag->message = message;
        diag->file = file;
        diag->line = line;
    }
    return code;
}
