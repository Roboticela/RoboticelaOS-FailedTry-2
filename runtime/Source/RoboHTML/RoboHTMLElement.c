/*
 * RoboHTML element model (Phase 8.5) — see RoboHTMLElement.h.
 *
 * File shape:
 *   1. name policy (strict, application-facing: validate + normalize)
 *   2. element classification (delegates to the Phase 8.11 tag vocabulary)
 *   3. element creation (the single construction seam)
 *   4. element read side (identity, view, state, attributes, tag)
 *
 * The element *type* vocabulary lives in RoboHTMLTag.c; this file owns the
 * element *instance* — the payload read side over the tree. Tag accessors
 * therefore read here (they need the node registry and the payload), while
 * the vocabulary they resolve against is defined there. One table, read from
 * two directions.
 *
 * Note on the name policy: the strict normalizer validates as it lowercases
 * and is for names an application supplies. The parser path deliberately
 * does not validate, because a malformed tag name must be tolerated rather
 * than rejected — so element creation lowercases and stores whatever it is
 * given instead of calling the strict helper.
 */

#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLTag.h"
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

/**
 * Is @p entry reachable from the document root?
 *
 * Walked up the parent links rather than stored as a flag: a flag would have
 * to be maintained by every link/unlink path (and would go stale the moment
 * a future DOM moves a subtree), while the walk is bounded by the node
 * ceiling and costs one step per ancestor. The loop is additionally guarded
 * so a corrupted parent chain terminates instead of spinning.
 */
static int element_is_connected(const robo_html_document_slot_t *slot,
                                const robo_html_node_entry_t *entry)
{
    robo_html_node_id_t id = entry->parent;
    size_t guard = 0u;

    while (id != ROBO_HTML_NODE_ID_NONE &&
           guard++ < (size_t)ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        const robo_html_node_entry_t *up =
            robo_html_node_entry_get(slot->id, id);

        if (!up) {
            return 0;
        }
        if (up->kind == ROBO_HTML_NODE_KIND_DOCUMENT &&
            id == slot->root.node_id) {
            return 1;
        }
        id = up->parent;
    }
    return 0;
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
    out->namespace_id = robo_html_payload_namespace(slot, e);
    out->tag = robo_html_tag_from_name(out->namespace_id, out->name);
    out->classification = robo_html_tag_class(out->tag);

    out->state = ROBO_HTML_ELEMENT_STATE_NONE;
    if (e->origin == (uint32_t)ROBO_HTML_NODE_ORIGIN_PARSER) {
        out->state |= ROBO_HTML_ELEMENT_STATE_PARSER_CREATED;
    }
    if (element_is_connected(slot, e)) {
        out->state |= ROBO_HTML_ELEMENT_STATE_CONNECTED;
    }
    if (out->self_closing) {
        out->state |= ROBO_HTML_ELEMENT_STATE_SELF_CLOSING;
    }
    return ROBO_OK;
}

void robo_html_attribute_init(robo_html_attribute_t *attribute)
{
    if (!attribute) {
        return;
    }
    attribute->name = "";
    attribute->name_length = 0u;
    attribute->value = "";
    attribute->value_length = 0u;
    attribute->namespace_id = ROBO_HTML_NAMESPACE_NONE;
}

robo_result_t robo_html_element_attribute_at(
    const robo_html_node_handle_t *element,
    size_t index,
    robo_html_attribute_t *out,
    robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (!element || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "element or out is NULL");
    }
    robo_html_attribute_init(out);
    slot = robo_html_document_slot_find(element->document_id);
    e = element_entry(element);
    if (!slot || !e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "handle does not resolve to a live element");
    }
    if (robo_html_payload_attr_at(slot, e, index, out) != ROBO_OK) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "attribute index out of range");
    }
    return ROBO_OK;
}

robo_result_t robo_html_element_find_attribute(
    const robo_html_node_handle_t *element,
    const char *name,
    robo_html_attribute_t *out,
    robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (!element || !name || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "NULL argument");
    }
    robo_html_attribute_init(out);
    slot = robo_html_document_slot_find(element->document_id);
    e = element_entry(element);
    if (!slot || !e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "handle does not resolve to a live element");
    }
    if (robo_html_payload_attr_find(slot, e, name, out) != ROBO_OK) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "no such attribute");
    }
    return ROBO_OK;
}

const char *robo_html_element_attribute_value(
    const robo_html_node_handle_t *element,
    const char *name,
    size_t *out_length)
{
    robo_html_attribute_t found;

    if (out_length) {
        *out_length = 0u;
    }
    if (robo_html_element_find_attribute(element, name, &found, NULL) !=
        ROBO_OK) {
        return NULL;
    }
    if (out_length) {
        *out_length = found.value_length;
    }
    return found.value;
}

robo_result_t robo_html_element_attribute_count(
    const robo_html_node_handle_t *element,
    size_t *out_count,
    robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (!element || !out_count) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_INVALID_ARGUMENT, "NULL argument");
    }
    *out_count = 0u;
    slot = robo_html_document_slot_find(element->document_id);
    e = element_entry(element);
    if (!slot || !e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT, ROBO_NOT_FOUND,
                              "handle does not resolve to a live element");
    }
    *out_count = robo_html_payload_attr_count(slot, e);
    return ROBO_OK;
}

int robo_html_element_has_attribute(const robo_html_node_handle_t *element,
                                    const char *name)
{
    robo_html_attribute_t ignored;

    if (!element || !name || name[0] == '\0') {
        return 0;
    }
    /* Presence is a different question from "has a value": <input disabled>
     * is present with an empty value, so the lookup result is the answer. */
    return robo_html_element_find_attribute(element, name, &ignored, NULL) ==
                   ROBO_OK
               ? 1
               : 0;
}

/* ------------------------------------------------------------------ */
/* Attribute mutation (Phase 8.6)                                      */
/* ------------------------------------------------------------------ */

/**
 * Shared body of the three write verbs: validate the name the way an
 * application-supplied name must be validated, lowercase it so storage and
 * lookup agree, then hand the payload its rewrite.
 *
 * @p value may be NULL (the empty string), which is what makes a
 * boolean-style attribute writable: presence is the record, not the value.
 */
static robo_result_t mutate_attribute(const robo_html_node_handle_t *element,
                                      const char *name,
                                      const char *value,
                                      robo_html_attr_mutation_t mutation,
                                      robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_node_entry_t *e;
    char normalized[ROBO_HTML_NAME_MAX];
    robo_html_diag_t local;
    /* The strict normalizer reports why a name was refused through the diag
     * only, so give it one to write into even when the caller passed none —
     * otherwise every refusal collapses to INVALID_ARGUMENT and a caller
     * cannot tell a bad character from a name that is too long. */
    robo_html_diag_t *where = diag ? diag : &local;

    if (!element || !name) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_INVALID_ARGUMENT, "element or name is NULL");
    }
    if (robo_html_element_name_normalize(name, normalized, where) != ROBO_OK) {
        return where->code;
    }
    slot = robo_html_document_slot_find(element->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    /* The mutable resolver applies the same generation and kind tests as
     * element_entry, so a stale or non-element handle stops here too. */
    e = robo_html_node_entry_mut(element->document_id, element->node_id);
    if (!e || e->generation != element->generation ||
        e->kind != ROBO_HTML_NODE_KIND_ELEMENT) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT, ROBO_NOT_FOUND,
                              "handle does not resolve to a live element");
    }
    return robo_html_payload_mutate_attributes(slot, e, mutation, normalized,
                                               value ? value : "", diag);
}

robo_result_t robo_html_element_set_attribute(
    const robo_html_node_handle_t *element,
    const char *name,
    const char *value,
    robo_html_diag_t *diag)
{
    return mutate_attribute(element, name, value, ROBO_HTML_ATTR_MUTATION_SET,
                            diag);
}

robo_result_t robo_html_element_add_attribute(
    const robo_html_node_handle_t *element,
    const char *name,
    const char *value,
    robo_html_diag_t *diag)
{
    return mutate_attribute(element, name, value, ROBO_HTML_ATTR_MUTATION_ADD,
                            diag);
}

robo_result_t robo_html_element_remove_attribute(
    const robo_html_node_handle_t *element,
    const char *name,
    robo_html_diag_t *diag)
{
    return mutate_attribute(element, name, NULL, ROBO_HTML_ATTR_MUTATION_REMOVE,
                            diag);
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

/*
 * Classification is a property of the element *type*, so the tag vocabulary
 * defines it and nothing else does — see RoboHTMLTag.c. Before Phase 8.11
 * this file kept its own name-to-class table; that table and the tag table
 * would have been two answers to "what kind of element is <div>?", so the
 * table moved and this function became a delegation. An element whose name
 * the tag table does not know — a custom element, an SVG element, an element
 * from a future version of HTML — is GENERIC, which is a supported answer
 * and not an error.
 */
robo_html_element_class_t robo_html_element_classify(
    robo_html_namespace_t ns,
    const char *local_name)
{
    return robo_html_tag_class(robo_html_tag_from_name(ns, local_name));
}

int robo_html_element_class_is_known(robo_html_element_class_t cls)
{
    return cls >= ROBO_HTML_ELEMENT_CLASS_GENERIC &&
           cls < ROBO_HTML_ELEMENT_CLASS_COUNT;
}

const char *robo_html_element_class_name(robo_html_element_class_t cls)
{
    switch (cls) {
    case ROBO_HTML_ELEMENT_CLASS_GENERIC:
        return "generic";
    case ROBO_HTML_ELEMENT_CLASS_ROOT:
        return "root";
    case ROBO_HTML_ELEMENT_CLASS_METADATA:
        return "metadata";
    case ROBO_HTML_ELEMENT_CLASS_SECTION:
        return "section";
    case ROBO_HTML_ELEMENT_CLASS_HEADING:
        return "heading";
    case ROBO_HTML_ELEMENT_CLASS_PARAGRAPH:
        return "paragraph";
    case ROBO_HTML_ELEMENT_CLASS_TEXT:
        return "text";
    case ROBO_HTML_ELEMENT_CLASS_REPLACED:
        return "replaced";
    case ROBO_HTML_ELEMENT_CLASS_CONTROL:
        return "control";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

void robo_html_element_create_info_init(robo_html_element_create_info_t *info)
{
    if (!info) {
        return;
    }
    robo_html_node_handle_clear(&info->parent);
    info->origin = ROBO_HTML_NODE_ORIGIN_NONE;
    info->namespace_id = ROBO_HTML_NAMESPACE_NONE;
    info->local_name = NULL;
    info->attributes = NULL;
    info->attribute_count = 0u;
    info->self_closing = 0;
}

/**
 * ASCII-lowercase @p src into @p dst, which must hold @p cap bytes.
 * @return the number of bytes written, or @p cap when the name does not fit
 * (the caller then stores the name verbatim rather than truncating it —
 * shrinking a tag name would silently change the element's identity).
 */
static size_t lower_into(const char *src, char *dst, size_t cap)
{
    size_t i;

    for (i = 0u; src[i] != '\0'; i++) {
        char c = src[i];

        if (i + 1u >= cap) {
            return cap;
        }
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        dst[i] = c;
    }
    dst[i] = '\0';
    return i;
}

robo_result_t robo_html_element_create(
    robo_html_document_id_t document_id,
    const robo_html_element_create_info_t *info,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_node_handle_t h;
    robo_html_node_entry_t *entry;
    char lowered[ROBO_HTML_NAME_MAX];
    const char *name;
    size_t name_length;
    robo_result_t r;

    if (out) {
        robo_html_node_handle_clear(out);
    }
    if (!out || !info) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_INVALID_ARGUMENT, "out or info is NULL");
    }
    if (!info->local_name || info->local_name[0] == '\0') {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_INVALID_ARGUMENT, "element name is empty");
    }
    if (!robo_html_namespace_is_known(info->namespace_id) ||
        info->namespace_id == ROBO_HTML_NAMESPACE_NONE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_INVALID_ARGUMENT,
                              "element namespace is not a live namespace");
    }
    if (info->attribute_count > 0u && !info->attributes) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_INVALID_ARGUMENT,
                              "attribute count without an attribute array");
    }
    if (!robo_html_node_handle_is_wellformed(&info->parent)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_INVALID_ARGUMENT,
                              "parent handle is ill-formed");
    }
    slot = robo_html_document_slot_find(document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    /*
     * The whole cross-document rule in one place: parent and child must name
     * the same document, and that document must actually own the parent.
     * Checking the claimed document id before resolving is what makes a
     * handle from another document a detected error instead of an aliased
     * node.
     */
    if (info->parent.document_id != document_id) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_INVALID_ARGUMENT,
                              "parent belongs to another document");
    }
    if (!robo_html_document_owns_node(document_id, &info->parent)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT, ROBO_NOT_FOUND,
                              "parent is not a live node of this document");
    }

    name = info->local_name;
    if (lower_into(info->local_name, lowered, sizeof(lowered)) <
        sizeof(lowered)) {
        name = lowered;
    }
    name_length = strlen(name);

    r = robo_html_document_register_node(
        document_id, ROBO_HTML_NODE_KIND_ELEMENT, info->origin, &h, diag);
    if (r != ROBO_OK) {
        return r;
    }
    entry = robo_html_node_entry_mut(document_id, h.node_id);
    if (!entry) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT, ROBO_NOT_FOUND,
                              "registered element vanished from the registry");
    }
    r = robo_html_payload_write_element(slot, entry, name, name_length,
                                        info->attributes,
                                        info->attribute_count,
                                        info->self_closing,
                                        info->namespace_id, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_html_node_link_child(document_id, info->parent.node_id, h.node_id,
                                  diag);
    if (r != ROBO_OK) {
        return r;
    }
    *out = h;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Read side                                                           */
/* ------------------------------------------------------------------ */

int robo_html_element_is_element(const robo_html_node_handle_t *h)
{
    return element_entry(h) != NULL ? 1 : 0;
}

const char *robo_html_element_name(const robo_html_node_handle_t *element,
                                   size_t *out_length)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (out_length) {
        *out_length = 0u;
    }
    if (!element) {
        return "";
    }
    slot = robo_html_document_slot_find(element->document_id);
    e = element_entry(element);
    if (!slot || !e) {
        return "";
    }
    return robo_html_payload_element_name(slot, e, out_length);
}

robo_html_namespace_t robo_html_element_namespace(
    const robo_html_node_handle_t *element)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (!element) {
        return ROBO_HTML_NAMESPACE_NONE;
    }
    slot = robo_html_document_slot_find(element->document_id);
    e = element_entry(element);
    if (!slot || !e) {
        return ROBO_HTML_NAMESPACE_NONE;
    }
    return robo_html_payload_namespace(slot, e);
}

robo_html_element_class_t robo_html_element_classification(
    const robo_html_node_handle_t *element)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (!element) {
        return ROBO_HTML_ELEMENT_CLASS_GENERIC;
    }
    slot = robo_html_document_slot_find(element->document_id);
    e = element_entry(element);
    if (!slot || !e) {
        return ROBO_HTML_ELEMENT_CLASS_GENERIC;
    }
    return robo_html_element_classify(robo_html_payload_namespace(slot, e),
                                      robo_html_payload_element_name(slot, e,
                                                                     NULL));
}

int robo_html_element_is_named(const robo_html_node_handle_t *element,
                               const char *name)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (!element || !name || name[0] == '\0') {
        return 0;
    }
    slot = robo_html_document_slot_find(element->document_id);
    e = element_entry(element);
    if (!slot || !e) {
        return 0;
    }
    return robo_html_name_equal(robo_html_payload_element_name(slot, e, NULL),
                            name);
}

robo_html_tag_t robo_html_element_tag(const robo_html_node_handle_t *element)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (!element) {
        return ROBO_HTML_TAG_UNKNOWN;
    }
    slot = robo_html_document_slot_find(element->document_id);
    e = element_entry(element);
    if (!slot || !e) {
        return ROBO_HTML_TAG_UNKNOWN;
    }
    return robo_html_tag_from_name(robo_html_payload_namespace(slot, e),
                                   robo_html_payload_element_name(slot, e,
                                                                  NULL));
}

int robo_html_element_is_tag(const robo_html_node_handle_t *element,
                             robo_html_tag_t tag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    /*
     * A non-element handle must satisfy no tag, including UNKNOWN: "no
     * recognised identity" is a statement about an element, so it cannot be
     * answered for a text node or a stale id. Resolving the element first is
     * what makes `is_tag(x, UNKNOWN)` mean exactly "x is a live element with
     * no recognised type" rather than "x is not a live element".
     */
    if (!element) {
        return 0;
    }
    /* UNKNOWN is a real question and is answered below; any *other* undefined
     * enumerator can only be a bug, so it is refused instead of quietly
     * matching every unrecognised element. */
    if (tag != ROBO_HTML_TAG_UNKNOWN && !robo_html_tag_is_known(tag)) {
        return 0;
    }
    slot = robo_html_document_slot_find(element->document_id);
    e = element_entry(element);
    if (!slot || !e) {
        return 0;
    }
    return robo_html_tag_from_name(robo_html_payload_namespace(slot, e),
                                   robo_html_payload_element_name(slot, e,
                                                                  NULL)) ==
           tag;
}

robo_result_t robo_html_element_parent(const robo_html_node_handle_t *element,
                                       robo_html_node_handle_t *out,
                                       robo_html_diag_t *diag)
{
    const robo_html_document_slot_t *slot;
    const robo_html_node_entry_t *e;

    if (out) {
        robo_html_node_handle_clear(out);
    }
    if (!element || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_INVALID_ARGUMENT, "NULL argument");
    }
    slot = robo_html_document_slot_find(element->document_id);
    e = element_entry(element);
    if (!slot || !e) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT, ROBO_NOT_FOUND,
                              "handle does not resolve to a live element");
    }
    if (e->parent == ROBO_HTML_NODE_ID_NONE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT, ROBO_NOT_FOUND,
                              "element has no parent");
    }
    return robo_html_document_node_by_id(slot->id, e->parent, out, diag);
}
