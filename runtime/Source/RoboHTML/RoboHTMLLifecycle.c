#include "RoboHTML/RoboHTMLLifecycle.h"

int robo_html_document_lifecycle_is_known(robo_html_document_lifecycle_t life)
{
    switch (life) {
    case ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED:
    case ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED:
    case ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING:
    case ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE:
    case ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE:
    case ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED:
    case ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING:
    case ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED:
        return 1;
    default:
        return 0;
    }
}

int robo_html_document_lifecycle_can_transition(
    robo_html_document_lifecycle_t from,
    robo_html_document_lifecycle_t to)
{
    if (!robo_html_document_lifecycle_is_known(from) ||
        !robo_html_document_lifecycle_is_known(to)) {
        return 0;
    }
    if (from == ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED) {
        return to == ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED;
    }
    if (from == ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED) {
        return to == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING;
    }
    if (from == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING) {
        return to == ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING;
    }
    if (from == ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE) {
        return to == ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING;
    }
    if (from == ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE) {
        return to == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING;
    }
    if (from == ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED) {
        return to == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING;
    }
    if (from == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING) {
        return to == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED;
    }
    /* DESTROYED → DESTROYED is the idempotent stay. */
    return to == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED;
}

robo_result_t robo_html_document_lifecycle_check_transition(
    robo_html_document_lifecycle_t from,
    robo_html_document_lifecycle_t to)
{
    return robo_html_document_lifecycle_can_transition(from, to)
               ? ROBO_OK
               : ROBO_INVALID_ARGUMENT;
}

const char *robo_html_document_lifecycle_name(
    robo_html_document_lifecycle_t life)
{
    switch (life) {
    case ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED:
        return "uninitialized";
    case ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED:
        return "created";
    case ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING:
        return "loading";
    case ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE:
        return "interactive";
    case ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE:
        return "complete";
    case ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED:
        return "failed";
    case ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING:
        return "destroying";
    case ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED:
        return "destroyed";
    default:
        return "unknown";
    }
}

int robo_html_document_lifecycle_allows_content(
    robo_html_document_lifecycle_t life)
{
    return life == ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED ||
           life == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING ||
           life == ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE ||
           life == ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE;
}

int robo_html_document_lifecycle_allows_source_attach(
    robo_html_document_lifecycle_t life)
{
    return life == ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED ||
           life == ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE ||
           life == ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE;
}

int robo_html_document_lifecycle_is_settled(robo_html_document_lifecycle_t life)
{
    return life == ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE ||
           life == ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE;
}

int robo_html_document_lifecycle_is_terminal(robo_html_document_lifecycle_t life)
{
    return life == ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED ||
           life == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING ||
           life == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED;
}

int robo_html_document_lifecycle_is_destroyed(
    robo_html_document_lifecycle_t life)
{
    return life == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED;
}
