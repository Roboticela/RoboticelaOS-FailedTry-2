/*
 * RoboHTML element model (Phase 8.5) — see RoboHTMLElement.h.
 *
 * File shape:
 *   1. name policy (strict, application-facing: validate + normalize)
 *   2. element classification (namespace + tag name → one small vocabulary)
 *   3. element creation (the single construction seam)
 *   4. element read side (identity, view, state, attributes)
 *
 * Note on the name policy: the strict normalizer validates as it lowercases
 * and is for names an application supplies. The parser path deliberately
 * does not validate, because a malformed tag name must be tolerated rather
 * than rejected — so element creation lowercases and stores whatever it is
 * given instead of calling the strict helper.
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
    out->classification = robo_html_element_classify(out->namespace_id, out->name);

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

typedef struct element_class_row {
    const char *name;
    robo_html_element_class_t cls;
} element_class_row_t;

/*
 * The whole HTML vocabulary this phase recognises, grouped by class. One
 * linear table rather than a generated hash: classification runs when an
 * element's view is read, not per painted pixel, so the scan is
 * unmeasurable while the vocabulary stays reviewable in one place. An
 * element that is not here is GENERIC — never an error — which is what
 * keeps custom elements, SVG and future elements safe.
 */
static const element_class_row_t k_element_classes[] = {
    {"html", ROBO_HTML_ELEMENT_CLASS_ROOT},

    {"base", ROBO_HTML_ELEMENT_CLASS_METADATA},
    {"basefont", ROBO_HTML_ELEMENT_CLASS_METADATA},
    {"bgsound", ROBO_HTML_ELEMENT_CLASS_METADATA},
    {"head", ROBO_HTML_ELEMENT_CLASS_METADATA},
    {"link", ROBO_HTML_ELEMENT_CLASS_METADATA},
    {"meta", ROBO_HTML_ELEMENT_CLASS_METADATA},
    {"noscript", ROBO_HTML_ELEMENT_CLASS_METADATA},
    {"script", ROBO_HTML_ELEMENT_CLASS_METADATA},
    {"style", ROBO_HTML_ELEMENT_CLASS_METADATA},
    {"template", ROBO_HTML_ELEMENT_CLASS_METADATA},
    {"title", ROBO_HTML_ELEMENT_CLASS_METADATA},

    {"address", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"article", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"aside", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"blockquote", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"body", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"dd", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"details", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"div", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"dl", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"dt", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"fieldset", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"figcaption", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"figure", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"footer", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"header", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"hgroup", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"hr", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"legend", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"li", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"main", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"menu", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"nav", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"ol", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"pre", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"section", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"summary", ROBO_HTML_ELEMENT_CLASS_SECTION},
    {"ul", ROBO_HTML_ELEMENT_CLASS_SECTION},

    {"h1", ROBO_HTML_ELEMENT_CLASS_HEADING},
    {"h2", ROBO_HTML_ELEMENT_CLASS_HEADING},
    {"h3", ROBO_HTML_ELEMENT_CLASS_HEADING},
    {"h4", ROBO_HTML_ELEMENT_CLASS_HEADING},
    {"h5", ROBO_HTML_ELEMENT_CLASS_HEADING},
    {"h6", ROBO_HTML_ELEMENT_CLASS_HEADING},

    {"p", ROBO_HTML_ELEMENT_CLASS_PARAGRAPH},

    {"a", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"abbr", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"b", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"bdi", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"bdo", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"br", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"cite", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"code", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"data", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"del", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"dfn", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"em", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"i", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"ins", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"kbd", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"mark", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"q", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"rp", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"rt", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"ruby", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"s", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"samp", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"small", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"span", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"strong", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"sub", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"sup", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"time", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"u", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"var", ROBO_HTML_ELEMENT_CLASS_TEXT},
    {"wbr", ROBO_HTML_ELEMENT_CLASS_TEXT},

    {"area", ROBO_HTML_ELEMENT_CLASS_REPLACED},
    {"audio", ROBO_HTML_ELEMENT_CLASS_REPLACED},
    {"canvas", ROBO_HTML_ELEMENT_CLASS_REPLACED},
    {"embed", ROBO_HTML_ELEMENT_CLASS_REPLACED},
    {"iframe", ROBO_HTML_ELEMENT_CLASS_REPLACED},
    {"img", ROBO_HTML_ELEMENT_CLASS_REPLACED},
    {"map", ROBO_HTML_ELEMENT_CLASS_REPLACED},
    {"object", ROBO_HTML_ELEMENT_CLASS_REPLACED},
    {"param", ROBO_HTML_ELEMENT_CLASS_REPLACED},
    {"picture", ROBO_HTML_ELEMENT_CLASS_REPLACED},
    {"source", ROBO_HTML_ELEMENT_CLASS_REPLACED},
    {"track", ROBO_HTML_ELEMENT_CLASS_REPLACED},
    {"video", ROBO_HTML_ELEMENT_CLASS_REPLACED},

    {"button", ROBO_HTML_ELEMENT_CLASS_CONTROL},
    {"datalist", ROBO_HTML_ELEMENT_CLASS_CONTROL},
    {"form", ROBO_HTML_ELEMENT_CLASS_CONTROL},
    {"input", ROBO_HTML_ELEMENT_CLASS_CONTROL},
    {"label", ROBO_HTML_ELEMENT_CLASS_CONTROL},
    {"meter", ROBO_HTML_ELEMENT_CLASS_CONTROL},
    {"optgroup", ROBO_HTML_ELEMENT_CLASS_CONTROL},
    {"option", ROBO_HTML_ELEMENT_CLASS_CONTROL},
    {"output", ROBO_HTML_ELEMENT_CLASS_CONTROL},
    {"progress", ROBO_HTML_ELEMENT_CLASS_CONTROL},
    {"select", ROBO_HTML_ELEMENT_CLASS_CONTROL},
    {"textarea", ROBO_HTML_ELEMENT_CLASS_CONTROL},
};

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

robo_html_element_class_t robo_html_element_classify(
    robo_html_namespace_t ns,
    const char *local_name)
{
    size_t i;

    if (!local_name || local_name[0] == '\0') {
        return ROBO_HTML_ELEMENT_CLASS_GENERIC;
    }
    /* Only the HTML vocabulary is recognised in this phase: an SVG <a> is
     * not an HTML <a>, so classifying it would be a lie. */
    if (ns != ROBO_HTML_NAMESPACE_HTML) {
        return ROBO_HTML_ELEMENT_CLASS_GENERIC;
    }
    for (i = 0u; i < sizeof(k_element_classes) / sizeof(k_element_classes[0]);
         i++) {
        if (robo_html_name_equal(k_element_classes[i].name, local_name)) {
            return k_element_classes[i].cls;
        }
    }
    return ROBO_HTML_ELEMENT_CLASS_GENERIC;
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
