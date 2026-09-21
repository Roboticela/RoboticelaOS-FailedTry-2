/*
 * RoboInput diagnostics (Phase 12.1) — see RoboInputDiagnostics.h.
 *
 * The record and its seven-row vocabulary, copied in shape from RoboDesktop's and RoboRender's own files
 * because the shape is what a caller holding two diags from two modules has to be able to read. The domain
 * names are this file's own, and they name **which boundary refused** rather than which object failed -- a
 * split that only makes sense for this module: an input event crosses four boundaries between a platform and
 * a document, and "the event is malformed" and "the queue is full" send a developer to two entirely
 * different places.
 */

#include "RoboInput/RoboInputDiagnostics.h"

#include <stddef.h>

static const char *const input_domain_names[ROBO_INPUT_DOMAIN_COUNT] = {
    "none",
    "event",
    "space",
    "queue",
    "target",
    "bridge",
    "dump"
};

int robo_input_domain_is_known(robo_input_diagnostic_domain_t domain)
{
    size_t index = (size_t)domain;

    return index < (size_t)ROBO_INPUT_DOMAIN_COUNT;
}

const char *robo_input_domain_name(robo_input_diagnostic_domain_t domain)
{
    if (!robo_input_domain_is_known(domain)) {
        return "unknown";
    }
    return input_domain_names[(size_t)domain];
}

void robo_input_diag_reset(robo_input_diag_t *diag)
{
    if (diag) {
        diag->code = ROBO_OK;
        diag->domain = ROBO_INPUT_DOMAIN_NONE;
        diag->message = NULL;
        diag->file = NULL;
        diag->line = 0;
    }
}

robo_result_t robo_input_diag_set(robo_input_diag_t *diag,
                                  robo_result_t code,
                                  robo_input_diagnostic_domain_t domain,
                                  const char *message,
                                  const char *file,
                                  int line)
{
    if (!diag) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (code == ROBO_OK) {
        robo_input_diag_reset(diag);
        return ROBO_OK;
    }
    diag->code = code;
    diag->domain = domain;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

robo_result_t robo_input_diag_record(robo_input_diag_t *diag,
                                     robo_result_t code,
                                     robo_input_diagnostic_domain_t domain,
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
