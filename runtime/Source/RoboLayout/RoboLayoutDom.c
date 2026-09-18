/*
 * RoboLayout's DOM + computed-style bridge (Phase 10.1) — see RoboLayoutDom.h.
 *
 * Two passes and four callbacks. The four callbacks are the whole of the
 * RoboCSS↔RoboHTML seam: what the element is (a view), and how it relates to its
 * parent and its element siblings. Everything else in this file is the storage
 * those callbacks need to stay valid for as long as RoboCSS is asking.
 */

#include "RoboLayout/RoboLayoutDom.h"

#include "RoboCSS/RoboCSSError.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTypes.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/**
 * One captured element.
 *
 * `classes` and `attributes` are arrays the bridge owns; the slices inside them
 * borrow the document's payload arena, which stops moving once the tree is built
 * -- so a view filled from this record stays valid for as long as the document
 * lives, which is what the matcher's recursion needs.
 */
typedef struct layout_dom_element {
    robo_html_node_handle_t handle;
    /** The opaque reference RoboCSS sees: the DOM node id. */
    robo_css_element_ref_t ref;
    robo_css_slice_t tag;
    int has_id;
    robo_css_slice_t id;
    robo_css_slice_t *classes;
    size_t class_count;
    robo_css_attribute_t *attributes;
    size_t attribute_count;
    /** Owned; NULL when nothing could be computed for this element. */
    robo_css_computed_style_t *style;
} layout_dom_element_t;

struct robo_layout_dom {
    robo_css_style_context_t *context;
    robo_html_document_id_t document;
    layout_dom_element_t *elements;
    size_t count;
    size_t capacity;
    /**
     * node id -> element slot + 1, or 0. Allocated once and never moved, which is
     * what makes the four callbacks O(1) instead of a scan each: matching runs
     * callbacks in the thousands on a document of a few dozen elements.
     */
    size_t *by_node;
    size_t by_node_capacity;
    size_t without_style;
    int ready;
};

/* ------------------------------------------------------------------ */
/* Lookup                                                             */
/* ------------------------------------------------------------------ */

/** The slot @p ref names, or NULL. */
static layout_dom_element_t *layout_dom_slot(robo_layout_dom_t *dom,
                                             robo_css_element_ref_t ref)
{
    size_t node;

    if (!dom || !dom->by_node || ref == ROBO_CSS_ELEMENT_REF_NONE) {
        return NULL;
    }
    node = (size_t)ref;
    if (node >= dom->by_node_capacity) {
        return NULL;
    }
    if (dom->by_node[node] == 0u) {
        return NULL;
    }
    return &dom->elements[dom->by_node[node] - 1u];
}

/* ------------------------------------------------------------------ */
/* The four RoboCSS callbacks                                         */
/* ------------------------------------------------------------------ */

static robo_result_t layout_dom_view_of(void *userdata,
                                        robo_css_element_ref_t ref,
                                        robo_css_element_view_t *out)
{
    robo_layout_dom_t *dom = (robo_layout_dom_t *)userdata;
    layout_dom_element_t *element;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    element = layout_dom_slot(dom, ref);
    if (!element) {
        /* An element that is gone. RoboCSS documents this as the honest answer
         * for a document that mutated under a resolution. */
        return ROBO_NOT_FOUND;
    }
    robo_css_element_view_init(out);
    if (robo_css_element_view_set_element(out, element->ref, element->tag, NULL) != ROBO_OK) {
        return ROBO_ERROR;
    }
    if (element->has_id &&
        robo_css_element_view_set_id(out, element->id, NULL) != ROBO_OK) {
        return ROBO_ERROR;
    }
    if (robo_css_element_view_set_classes(out, element->classes, element->class_count,
                                          NULL) != ROBO_OK) {
        return ROBO_ERROR;
    }
    if (robo_css_element_view_set_attributes(out, element->attributes,
                                             element->attribute_count, NULL) != ROBO_OK) {
        return ROBO_ERROR;
    }
    return ROBO_OK;
}

/** The nearest element ancestor of @p node, or NONE at the top of the tree. */
static robo_css_element_ref_t layout_dom_ancestor_ref(const robo_layout_dom_t *dom,
                                                      robo_html_node_handle_t node)
{
    size_t steps = 0u;
    size_t ceiling = dom->by_node_capacity + 2u;

    while (steps++ < ceiling) {
        robo_html_node_handle_t parent;
        robo_html_node_kind_t kind;

        if (robo_html_node_parent(&node, &parent, NULL) != ROBO_OK) {
            return ROBO_CSS_ELEMENT_REF_NONE;
        }
        if (robo_html_node_handle_resolve(&parent, &kind) != ROBO_OK) {
            return ROBO_CSS_ELEMENT_REF_NONE;
        }
        if (kind == ROBO_HTML_NODE_KIND_ELEMENT) {
            return (robo_css_element_ref_t)parent.node_id;
        }
        node = parent;
    }
    return ROBO_CSS_ELEMENT_REF_NONE;
}

/** The nearest element sibling of @p node in one direction, or NONE. */
static robo_css_element_ref_t layout_dom_sibling_ref(const robo_layout_dom_t *dom,
                                                     robo_html_node_handle_t node,
                                                     int forward)
{
    size_t steps = 0u;
    size_t ceiling = dom->by_node_capacity + 2u;

    /*
     * Text and comments are stepped over, because `+` and `:first-child` count
     * elements -- and a document written one element per line has a text node
     * between every pair of them, so counting nodes would find no adjacent pair at
     * all. Filtering is this side's job precisely because RoboCSS cannot see the
     * nodes it would have to filter.
     */
    while (steps++ < ceiling) {
        robo_html_node_handle_t next;
        robo_html_node_kind_t kind;
        robo_result_t found;

        found = forward ? robo_html_node_next_sibling(&node, &next, NULL)
                        : robo_html_node_prev_sibling(&node, &next, NULL);
        if (found != ROBO_OK) {
            return ROBO_CSS_ELEMENT_REF_NONE;
        }
        if (robo_html_node_handle_resolve(&next, &kind) != ROBO_OK) {
            return ROBO_CSS_ELEMENT_REF_NONE;
        }
        if (kind == ROBO_HTML_NODE_KIND_ELEMENT) {
            return (robo_css_element_ref_t)next.node_id;
        }
        node = next;
    }
    return ROBO_CSS_ELEMENT_REF_NONE;
}

static robo_css_element_ref_t layout_dom_parent_of(void *userdata,
                                                   robo_css_element_ref_t ref)
{
    robo_layout_dom_t *dom = (robo_layout_dom_t *)userdata;
    layout_dom_element_t *element = layout_dom_slot(dom, ref);

    if (!element) {
        return ROBO_CSS_ELEMENT_REF_NONE;
    }
    return layout_dom_ancestor_ref(dom, element->handle);
}

static robo_css_element_ref_t layout_dom_previous_of(void *userdata,
                                                     robo_css_element_ref_t ref)
{
    robo_layout_dom_t *dom = (robo_layout_dom_t *)userdata;
    layout_dom_element_t *element = layout_dom_slot(dom, ref);

    if (!element) {
        return ROBO_CSS_ELEMENT_REF_NONE;
    }
    return layout_dom_sibling_ref(dom, element->handle, 0);
}

static robo_css_element_ref_t layout_dom_next_of(void *userdata,
                                                 robo_css_element_ref_t ref)
{
    robo_layout_dom_t *dom = (robo_layout_dom_t *)userdata;
    layout_dom_element_t *element = layout_dom_slot(dom, ref);

    if (!element) {
        return ROBO_CSS_ELEMENT_REF_NONE;
    }
    return layout_dom_sibling_ref(dom, element->handle, 1);
}

static robo_result_t layout_dom_inline_style(void *userdata,
                                             robo_css_element_ref_t ref,
                                             robo_css_slice_t *out_text)
{
    robo_layout_dom_t *dom = (robo_layout_dom_t *)userdata;
    layout_dom_element_t *element = layout_dom_slot(dom, ref);
    const char *value;
    size_t length = 0u;

    if (!element || !out_text) {
        return ROBO_INVALID_ARGUMENT;
    }
    /*
     * The `style` attribute's *text* is the whole inline-style boundary: RoboCSS
     * parses it, and the DOM keeps owning it. NULL means "no such attribute",
     * which is a different answer from an empty one -- an empty `style=""` is a
     * declaration list with no declarations.
     */
    value = robo_html_element_attribute_value(&element->handle, "style", &length);
    if (!value) {
        return ROBO_NOT_FOUND;
    }
    *out_text = robo_css_slice_from_bytes((const uint8_t *)value, length);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Capture                                                            */
/* ------------------------------------------------------------------ */

/**
 * Make the capture big enough for a document of @p node_count nodes.
 *
 * One element per node is a true upper bound, and sizing to it means the element
 * array never moves while RoboCSS holds a view into it -- which is the same reason
 * the 9.4 DOM suite sizes its host that way. The node index needs one slot per
 * node id, so both arrays are sized together and both are replaced together.
 *
 * Both arrays are replaced rather than reallocated *and* the node index is copied:
 * a refresh keeps the ids it already mapped, and `realloc` is not used because the
 * two arrays must grow atomically -- a new index beside an old element array would
 * resolve an id to the wrong slot.
 */
static robo_result_t layout_dom_reserve(robo_layout_dom_t *dom,
                                        size_t node_count,
                                        robo_layout_diag_t *diag)
{
    size_t wanted = node_count + 1u;
    layout_dom_element_t *elements;
    size_t *by_node;
    size_t i;

    if (wanted <= dom->capacity && wanted <= dom->by_node_capacity) {
        return ROBO_OK;
    }
    /* One and a half times, so that a document that grows node by node does not
     * reallocate on every build. */
    wanted = wanted + wanted / 2u + 8u;

    elements = (layout_dom_element_t *)robo_calloc(wanted, sizeof(layout_dom_element_t));
    by_node = (size_t *)robo_calloc(wanted, sizeof(size_t));
    if (!elements || !by_node) {
        robo_free(elements);
        robo_free(by_node);
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM, ROBO_OUT_OF_MEMORY,
                                "could not allocate the element capture");
    }
    for (i = 0u; i < dom->count; i++) {
        elements[i] = dom->elements[i];
    }
    if (dom->by_node && dom->by_node_capacity > 0u) {
        memcpy(by_node, dom->by_node, dom->by_node_capacity * sizeof(size_t));
    }
    robo_free(dom->elements);
    robo_free(dom->by_node);
    dom->elements = elements;
    dom->capacity = wanted;
    dom->by_node = by_node;
    dom->by_node_capacity = wanted;
    return ROBO_OK;
}

/** Release one captured element's owned arrays and style. */
static void layout_dom_element_clear(layout_dom_element_t *element)
{
    robo_free(element->classes);
    robo_free(element->attributes);
    robo_css_computed_style_destroy(element->style);
    memset(element, 0, sizeof(*element));
}

/** What one walk visits. */
static robo_result_t layout_dom_capture_visitor(const robo_html_node_view_t *view,
                                                size_t depth,
                                                void *user)
{
    robo_layout_dom_t *dom = (robo_layout_dom_t *)user;
    layout_dom_element_t *element;
    size_t index;
    size_t attribute_count = 0u;
    size_t i;

    (void)depth;
    if (dom->count >= dom->capacity) {
        return ROBO_ERROR;
    }
    index = dom->count;
    element = &dom->elements[index];
    memset(element, 0, sizeof(*element));
    element->handle = view->handle;
    element->ref = (robo_css_element_ref_t)view->handle.node_id;
    element->tag = robo_css_slice_from_text(view->name ? view->name : "");

    if (robo_html_element_attribute_count(&view->handle, &attribute_count, NULL) != ROBO_OK) {
        return ROBO_ERROR;
    }
    if (attribute_count > ROBO_LAYOUT_MAX_ATTRIBUTES_PER_ELEMENT) {
        return ROBO_ERROR;
    }
    if (attribute_count > 0u) {
        element->attributes = (robo_css_attribute_t *)robo_calloc(
            attribute_count, sizeof(robo_css_attribute_t));
        if (!element->attributes) {
            return ROBO_OUT_OF_MEMORY;
        }
    }

    for (i = 0u; i < attribute_count; i++) {
        robo_html_attribute_t attribute;

        if (robo_html_element_attribute_at(&view->handle, i, &attribute, NULL) != ROBO_OK) {
            return ROBO_ERROR;
        }
        if (attribute.name_length == 2u && attribute.name[0] == 'i' &&
            attribute.name[1] == 'd') {
            /* `id` is answered through the view's dedicated field, so it is not
             * also an attribute: RoboCSS's `[id=x]` and `#x` would otherwise be
             * two paths to one fact, and only one of them is the one the matcher
             * checks in its fast path. */
            if (!element->has_id) {
                element->has_id = 1;
                element->id = robo_css_slice_from_bytes(
                    (const uint8_t *)attribute.value, attribute.value_length);
            }
            continue;
        }
        if (attribute.name_length == 5u && memcmp(attribute.name, "class", 5u) == 0) {
            robo_css_slice_t value = robo_css_slice_from_bytes(
                (const uint8_t *)attribute.value, attribute.value_length);
            int truncated = 0;

            element->classes = (robo_css_slice_t *)robo_calloc(
                ROBO_LAYOUT_MAX_CLASSES_PER_ELEMENT, sizeof(robo_css_slice_t));
            if (!element->classes) {
                return ROBO_OUT_OF_MEMORY;
            }
            element->class_count = robo_css_split_ascii_whitespace(
                value, element->classes, ROBO_LAYOUT_MAX_CLASSES_PER_ELEMENT, &truncated);
            if (truncated) {
                /* Refused rather than truncated: a class list the matcher cannot
                 * see whole would turn `.x` into a wrong answer. */
                return ROBO_ERROR;
            }
            continue;
        }
        element->attributes[element->attribute_count].name =
            robo_css_slice_from_bytes((const uint8_t *)attribute.name,
                                      attribute.name_length);
        element->attributes[element->attribute_count].value =
            robo_css_slice_from_bytes((const uint8_t *)attribute.value,
                                      attribute.value_length);
        element->attribute_count++;
    }

    dom->by_node[(size_t)element->ref] = index + 1u;
    dom->count = index + 1u;
    return ROBO_OK;
}

/** Compute every captured element's style. */
static robo_result_t layout_dom_compute_all(robo_layout_dom_t *dom,
                                            robo_layout_diag_t *diag)
{
    size_t i;

    dom->without_style = 0u;
    for (i = 0u; i < dom->count; i++) {
        layout_dom_element_t *element = &dom->elements[i];
        robo_css_computed_style_t *style = NULL;
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_result_t computed;

        if (robo_css_computed_style_create(&style, NULL) != ROBO_OK) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM, ROBO_OUT_OF_MEMORY,
                                    "could not allocate a computed style");
        }
        computed = robo_css_style_context_compute_style(dom->context, element->ref,
                                                        style, &error, NULL);
        if (computed != ROBO_OK) {
            /*
             * A style that could not be computed is dropped rather than kept
             * half-filled, and it is *counted*: "this element has no style" is a
             * fact the tree records as STYLE_MISSING, and a bridge that quietly
             * substituted an empty style would hide it.
             */
            robo_css_computed_style_destroy(style);
            dom->without_style++;
            continue;
        }
        element->style = style;
        if (robo_css_computed_style_count(style) == 0u) {
            dom->without_style++;
        }
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The style source                                                   */
/* ------------------------------------------------------------------ */

static const robo_css_computed_style_t *layout_dom_style_of(void *userdata,
                                                            const robo_html_node_handle_t *element)
{
    robo_layout_dom_t *dom = (robo_layout_dom_t *)userdata;
    layout_dom_element_t *slot;

    if (!dom || !element) {
        return NULL;
    }
    slot = layout_dom_slot(dom, (robo_css_element_ref_t)element->node_id);
    if (!slot) {
        return NULL;
    }
    return slot->style;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

robo_result_t robo_layout_dom_create(robo_css_style_context_t *context,
                                     robo_html_document_id_t document,
                                     robo_layout_dom_t **out_dom,
                                     robo_layout_diag_t *diag)
{
    robo_layout_dom_t *dom;
    robo_css_element_host_t host;
    size_t node_count;

    if (out_dom) {
        *out_dom = NULL;
    }
    if (!context || !out_dom) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM, ROBO_INVALID_ARGUMENT,
                                "context or out_dom is NULL");
    }
    if (document == ROBO_HTML_DOCUMENT_ID_NONE || !robo_html_document_is_live(document)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM, ROBO_NOT_FOUND,
                                "document is not live");
    }

    robo_css_diag_t css_diag = ROBO_CSS_CLEARED;

    dom = (robo_layout_dom_t *)robo_calloc(1u, sizeof(*dom));
    if (!dom) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM, ROBO_OUT_OF_MEMORY,
                                "could not allocate the DOM bridge");
    }
    dom->context = context;
    dom->document = document;

    node_count = robo_html_document_node_count(document);
    if (layout_dom_reserve(dom, node_count, diag) != ROBO_OK) {
        robo_layout_dom_destroy(dom);
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }

    /* Install the seam. The host is one userdata for all four callbacks because
     * they come from one place: this bridge. */
    robo_css_element_host_init(&host);
    host.userdata = dom;
    host.view = layout_dom_view_of;
    host.parent = layout_dom_parent_of;
    host.previous_sibling = layout_dom_previous_of;
    host.next_sibling = layout_dom_next_of;
    /* The two seams below are RoboCSS calls, so they report through a RoboCSS
     * diag and not through this module's record. Translating rather than passing
     * `diag` is what keeps the two error vocabularies apart: a caller reading a
     * RoboLayout diag must never find a CSS domain in it. */
    if (robo_css_style_context_set_element_host(context, &host, &css_diag) != ROBO_OK) {
        robo_layout_dom_destroy(dom);
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM,
                                css_diag.code == ROBO_OK ? ROBO_ERROR : css_diag.code,
                                "the style context refused the element host");
    }
    if (robo_css_style_context_set_inline_style(context, layout_dom_inline_style, dom,
                                               &css_diag) != ROBO_OK) {
        robo_layout_dom_destroy(dom);
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM,
                                css_diag.code == ROBO_OK ? ROBO_ERROR : css_diag.code,
                                "the style context refused the inline-style provider");
    }

    *out_dom = dom;
    return ROBO_OK;
}

void robo_layout_dom_destroy(robo_layout_dom_t *dom)
{
    size_t i;

    if (!dom) {
        return;
    }
    for (i = 0u; i < dom->count; i++) {
        layout_dom_element_clear(&dom->elements[i]);
    }
    robo_free(dom->elements);
    robo_free(dom->by_node);
    /* Document, context and stylesheets belong to the caller. */
    robo_free(dom);
}

robo_result_t robo_layout_dom_refresh(robo_layout_dom_t *dom,
                                      robo_layout_diag_t *diag)
{
    robo_html_node_handle_t root;
    size_t i;
    size_t node_count;

    if (!dom) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM, ROBO_INVALID_ARGUMENT,
                                "dom is NULL");
    }
    if (!robo_html_document_is_live(dom->document)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM, ROBO_NOT_FOUND,
                                "the document is gone");
    }

    /* Release the previous generation first: a second refresh describes one
     * document, not two. */
    for (i = 0u; i < dom->count; i++) {
        layout_dom_element_clear(&dom->elements[i]);
    }
    dom->count = 0u;
    dom->ready = 0;
    dom->without_style = 0u;
    if (dom->by_node && dom->by_node_capacity > 0u) {
        memset(dom->by_node, 0, dom->by_node_capacity * sizeof(size_t));
    }

    node_count = robo_html_document_node_count(dom->document);
    if (layout_dom_reserve(dom, node_count, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }

    /* Pass one: capture. Every element must be answerable before any style is
     * computed, because computing one walks its ancestors. */
    root = robo_html_document_root(dom->document);
    if (robo_html_node_handle_is_wellformed(&root)) {
        robo_result_t walked = robo_html_node_walk_pre_order(&root,
                                                             ROBO_HTML_NODE_FILTER_ELEMENTS,
                                                             layout_dom_capture_visitor, dom,
                                                             NULL);

        if (walked != ROBO_OK) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM,
                                    walked == ROBO_OUT_OF_MEMORY ? ROBO_OUT_OF_MEMORY
                                                                 : ROBO_ERROR,
                                    "could not capture the document's elements");
        }
    }

    /* Pass two: compute. */
    if (layout_dom_compute_all(dom, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_ERROR;
    }
    dom->ready = dom->count > 0u;
    return ROBO_OK;
}

size_t robo_layout_dom_element_count(const robo_layout_dom_t *dom)
{
    return dom ? dom->count : 0u;
}

int robo_layout_dom_is_ready(const robo_layout_dom_t *dom)
{
    return dom && dom->ready;
}

robo_result_t robo_layout_dom_element_at(const robo_layout_dom_t *dom,
                                         size_t index,
                                         robo_html_node_handle_t *out,
                                         robo_layout_diag_t *diag)
{
    if (!out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM, ROBO_INVALID_ARGUMENT,
                                "out is NULL");
    }
    robo_html_node_handle_clear(out);
    if (!dom) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM, ROBO_INVALID_ARGUMENT,
                                "dom is NULL");
    }
    if (index >= dom->count) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM, ROBO_NOT_FOUND,
                                "the index is past the captured element count");
    }
    *out = dom->elements[index].handle;
    return ROBO_OK;
}

const robo_css_computed_style_t *robo_layout_dom_style_for(
    const robo_layout_dom_t *dom,
    const robo_html_node_handle_t *element)
{
    layout_dom_element_t *slot;

    if (!dom || !element) {
        return NULL;
    }
    /* The lookup is read-only, but it is typed through the mutable struct because
     * the node index is storage rather than state; the cast is the one place that
     * is visible, and it is here rather than in every caller. */
    slot = layout_dom_slot((robo_layout_dom_t *)dom,
                           (robo_css_element_ref_t)element->node_id);
    return slot ? slot->style : NULL;
}

size_t robo_layout_dom_without_style_count(const robo_layout_dom_t *dom)
{
    return dom ? dom->without_style : 0u;
}

robo_html_document_id_t robo_layout_dom_document(const robo_layout_dom_t *dom)
{
    return dom ? dom->document : ROBO_HTML_DOCUMENT_ID_NONE;
}

robo_result_t robo_layout_dom_style_source(robo_layout_dom_t *dom,
                                           robo_layout_style_source_t *out,
                                           robo_layout_diag_t *diag)
{
    if (!dom || !out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_DOM, ROBO_INVALID_ARGUMENT,
                                "dom or out is NULL");
    }
    robo_layout_style_source_init(out);
    out->userdata = dom;
    out->computed_style_for = layout_dom_style_of;
    return ROBO_OK;
}
