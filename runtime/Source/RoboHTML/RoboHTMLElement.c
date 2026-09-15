/*
 * RoboHTML element/attribute boundary (Phase 8.1 name policy, Phase 8.3
 * element/attribute storage read side).
 *
 * The name policy below is the single sanctioned place where ASCII
 * lowercasing happens: the tree builder calls it when it stores a name, and
 * every reader can therefore assume stored names are already lowercase.
 */

#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

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

/* ------------------------------------------------------------------ */
/* Element/attribute storage read side (Phase 8.3)                     */
/* ------------------------------------------------------------------ */

/** Resolve @p h to a live entry whose kind is ELEMENT. */
static const robo_html_node_entry_t *element_entry(
    const robo_html_node_handle_t *h)
{
    const robo_html_node_entry_t *e;

    if (!robo_html_node_handle_is_wellformed(h)) {
        return NULL;
    }
    e = robo_html_node_entry_get(h->document_id, h->node_id);
    if (!e || e->generation != h->generation) {
        return NULL;
    }
    if (e->kind != ROBO_HTML_NODE_KIND_ELEMENT) {
        return NULL;
    }
    return e;
}

robo_result_t robo_html_element_view_get(
    const robo_html_node_handle_t *element,
    robo_html_element_view_t *out,
    robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (!element || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "element or out is NULL");
    }
    memset(out, 0, sizeof(*out));
    slot = robo_html_document_slot_find(element->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    e = element_entry(element);
    if (!e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "handle does not resolve to a live element");
    }
    out->name = robo_html_payload_element_name(slot, e, &out->name_length);
    out->attribute_count = robo_html_payload_attr_count(slot, e);
    out->self_closing = robo_html_payload_self_closing(slot, e);
    return ROBO_OK;
}

robo_result_t robo_html_element_attribute_at(
    const robo_html_node_handle_t *element,
    size_t index,
    robo_html_attr_view_t *out,
    robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;
    const char *name = NULL;
    const char *value = NULL;

    if (!element || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "element or out is NULL");
    }
    out->name = "";
    out->value = "";
    slot = robo_html_document_slot_find(element->document_id);
    e = element_entry(element);
    if (!slot || !e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "handle does not resolve to a live element");
    }
    if (robo_html_payload_attr_at(slot, e, index, &name, &value) != ROBO_OK) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "attribute index out of range");
    }
    out->name = name;
    out->value = value;
    return ROBO_OK;
}

/**
 * ASCII case-insensitive equality.
 * Stored attribute names are lowercase, so a caller may pass any case
 * (see the case-insensitivity promised by RoboHTMLElement.h).
 */
static int attr_name_equal(const char *stored, const char *wanted)
{
    size_t i;

    if (!stored || !wanted) {
        return 0;
    }
    for (i = 0u; stored[i] != '\0' && wanted[i] != '\0'; i++) {
        unsigned char a = (unsigned char)stored[i];
        unsigned char b = (unsigned char)wanted[i];

        if (a >= 'A' && a <= 'Z') {
            a = (unsigned char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (unsigned char)(b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
    }
    return stored[i] == wanted[i];
}

robo_result_t robo_html_element_find_attribute(
    const robo_html_node_handle_t *element,
    const char *name,
    robo_html_attr_view_t *out,
    robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;
    size_t count;
    size_t i;

    if (!element || !name || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "NULL argument");
    }
    out->name = "";
    out->value = "";
    slot = robo_html_document_slot_find(element->document_id);
    e = element_entry(element);
    if (!slot || !e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "handle does not resolve to a live element");
    }
    count = robo_html_payload_attr_count(slot, e);
    for (i = 0u; i < count; i++) {
        const char *aname = NULL;
        const char *avalue = NULL;
        if (robo_html_payload_attr_at(slot, e, i, &aname, &avalue) != ROBO_OK) {
            break;
        }
        if (attr_name_equal(aname, name)) {
            out->name = aname;
            out->value = avalue;
            return ROBO_OK;
        }
    }
    return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                          "no such attribute");
}
