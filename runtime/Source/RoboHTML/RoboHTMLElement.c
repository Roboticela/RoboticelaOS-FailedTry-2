/*
 * RoboHTML element/attribute boundary (Phase 8.1) — name policy only.
 */

#include "RoboHTML/RoboHTMLElement.h"

#include <stddef.h>

void robo_html_element_boundary_init(robo_html_element_boundary_t *b)
{
    if (!b) {
        return;
    }
    b->reserved = 0u;
}

static int name_char_ok(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

robo_result_t robo_html_element_name_validate(const char *name,
                                              robo_html_diag_t *diag)
{
    size_t i;
    size_t n = 0u;

    if (!name) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_INVALID_ARGUMENT, "name is NULL");
    }
    while (name[n] != '\0') {
        n++;
    }
    if (n == 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_INVALID_ARGUMENT, "name is empty");
    }
    for (i = 0u; i < n; i++) {
        if (!name_char_ok((unsigned char)name[i])) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                                  ROBO_PARSE_ERROR,
                                  "name contains a forbidden character");
        }
    }
    if (n > ROBO_HTML_NAME_MAX) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_NOT_SUPPORTED, "name too long");
    }
    return ROBO_OK;
}

robo_result_t robo_html_element_name_normalize(const char *name,
                                               char *out,
                                               robo_html_diag_t *diag)
{
    size_t n = 0u;
    size_t i;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    if (robo_html_element_name_validate(name, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_INVALID_ARGUMENT;
    }
    while (name[n] != '\0') {
        n++;
    }
    if (n >= ROBO_HTML_NAME_MAX) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_NOT_SUPPORTED, "name too long");
    }
    for (i = 0u; i < n; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        out[i] = c;
    }
    out[n] = '\0';
    return ROBO_OK;
}
