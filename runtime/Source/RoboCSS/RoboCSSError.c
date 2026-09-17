/*
 * RoboCSS error vocabulary (Phase 9.1) — see RoboCSSError.h.
 */

#include "RoboCSS/RoboCSSError.h"

int robo_css_error_kind_is_known(robo_css_error_kind_t kind)
{
    return kind >= ROBO_CSS_ERROR_KIND_NONE &&
           kind < (robo_css_error_kind_t)ROBO_CSS_ERROR_KIND_COUNT;
}

const char *robo_css_error_kind_name(robo_css_error_kind_t kind)
{
    switch (kind) {
    case ROBO_CSS_ERROR_KIND_NONE:
        return "none";
    case ROBO_CSS_ERROR_KIND_UNSUPPORTED:
        return "unsupported";
    case ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX:
        return "recoverable-syntax";
    case ROBO_CSS_ERROR_KIND_INVALID_TOKEN:
        return "invalid-token";
    case ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION:
        return "malformed-declaration";
    case ROBO_CSS_ERROR_KIND_MALFORMED_RULE:
        return "malformed-rule";
    case ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF:
        return "unexpected-eof";
    case ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY:
        return "out-of-memory";
    default:
        return "unknown";
    }
}

int robo_css_error_kind_is_error(robo_css_error_kind_t kind)
{
    return kind != ROBO_CSS_ERROR_KIND_NONE;
}

int robo_css_error_kind_is_recoverable(robo_css_error_kind_t kind)
{
    switch (kind) {
    case ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX:
    case ROBO_CSS_ERROR_KIND_INVALID_TOKEN:
    case ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION:
    case ROBO_CSS_ERROR_KIND_MALFORMED_RULE:
    case ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF:
        return 1;
    default:
        return 0;
    }
}

int robo_css_error_kind_is_fatal(robo_css_error_kind_t kind)
{
    return kind == ROBO_CSS_ERROR_KIND_UNSUPPORTED ||
           kind == ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY;
}

void robo_css_error_init(robo_css_error_t *error)
{
    if (!error) {
        return;
    }
    error->kind = ROBO_CSS_ERROR_KIND_NONE;
    error->code = ROBO_OK;
    error->position = robo_css_position_none();
    error->message = NULL;
}

robo_result_t robo_css_error_set(robo_css_error_t *error,
                                 robo_css_error_kind_t kind,
                                 robo_result_t code,
                                 const robo_css_position_t *position,
                                 const char *message)
{
    if (!error) {
        return ROBO_INVALID_ARGUMENT;
    }
    error->kind = robo_css_error_kind_is_known(kind) ? kind : ROBO_CSS_ERROR_KIND_NONE;
    error->code = code;
    error->position = position ? *position : robo_css_position_none();
    error->message = message;
    return code;
}

int robo_css_error_is_error(const robo_css_error_t *error)
{
    if (!error) {
        return 0;
    }
    return error->kind != ROBO_CSS_ERROR_KIND_NONE;
}

const char *robo_css_error_message(const robo_css_error_t *error)
{
    if (!error || !error->message) {
        return "";
    }
    return error->message;
}
