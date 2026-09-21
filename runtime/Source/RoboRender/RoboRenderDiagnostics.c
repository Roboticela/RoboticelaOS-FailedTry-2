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

static const char *const render_domain_names[ROBO_RENDER_DOMAIN_COUNT] = {
    "none",
    "target",
    "list",
    "frame",
    "dump"
};

int robo_render_domain_is_known(robo_render_diagnostic_domain_t domain)
{
    size_t index = (size_t)domain;

    return index < (size_t)ROBO_RENDER_DOMAIN_COUNT;
}

const char *robo_render_domain_name(robo_render_diagnostic_domain_t domain)
{
    if (!robo_render_domain_is_known(domain)) {
        return "unknown";
    }
    return render_domain_names[(size_t)domain];
}

void robo_render_diag_reset(robo_render_diag_t *diag)
{
    if (diag) {
        diag->code = ROBO_OK;
        diag->domain = ROBO_RENDER_DOMAIN_NONE;
        diag->message = NULL;
        diag->file = NULL;
        diag->line = 0;
    }
}

robo_result_t robo_render_diag_set(robo_render_diag_t *diag,
                                   robo_result_t code,
                                   robo_render_diagnostic_domain_t domain,
                                   const char *message,
                                   const char *file,
                                   int line)
{
    if (!diag) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (code == ROBO_OK) {
        robo_render_diag_reset(diag);
        return ROBO_OK;
    }
    diag->code = code;
    diag->domain = domain;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

robo_result_t robo_render_diag_record(robo_render_diag_t *diag,
                                      robo_result_t code,
                                      robo_render_diagnostic_domain_t domain,
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
