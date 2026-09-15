#include "RoboHTML/RoboHTMLLifecycle.h"

int robo_html_document_lifecycle_is_known(robo_html_document_lifecycle_t life)
{
    switch (life) {
    case ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED:
    case ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED:
    case ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING:
    case ROBO_HTML_DOCUMENT_LIFECYCLE_READY:
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
    if (from == ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED &&
        to == ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED) {
        return 1;
    }
    if (from == ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED) {
        return to == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING;
    }
    if (from == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING) {
        return to == ROBO_HTML_DOCUMENT_LIFECYCLE_READY ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED ||
               to == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING;
    }
    if (from == ROBO_HTML_DOCUMENT_LIFECYCLE_READY) {
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
    if (from == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED) {
        return to == ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED;
    }
    return 0;
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
    case ROBO_HTML_DOCUMENT_LIFECYCLE_READY:
        return "ready";
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
           life == ROBO_HTML_DOCUMENT_LIFECYCLE_READY;
}

int robo_html_document_lifecycle_allows_source_attach(
    robo_html_document_lifecycle_t life)
{
    return life == ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED ||
           life == ROBO_HTML_DOCUMENT_LIFECYCLE_READY;
}
