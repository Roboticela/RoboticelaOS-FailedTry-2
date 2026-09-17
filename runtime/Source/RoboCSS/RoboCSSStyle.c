/*
 * RoboCSS style boundary (Phase 9.1) — see RoboCSSStyle.h.
 *
 * The context borrows everything it is given: stylesheet ids stay owned by the
 * module (and the caller), the property table and the callback userdata stay
 * owned by the caller. Destroying a context therefore frees the context's own
 * per-origin id arrays and nothing else — no stylesheet is destroyed by
 * dropping the thing that referred to it, which is the difference between a
 * reference and an owner.
 */

#include "RoboCSS/RoboCSSStyle.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

/** ASCII case-insensitive slice comparison — how attribute names compare. */
static int slice_equals_ci(robo_css_slice_t a, robo_css_slice_t b)
{
    size_t i;

    if (a.length != b.length) {
        return 0;
    }
    if (a.length == 0u) {
        return 1;
    }
    if (!a.bytes || !b.bytes) {
        return 0;
    }
    for (i = 0u; i < a.length; i++) {
        if (robo_css_ascii_lower(a.bytes[i]) != robo_css_ascii_lower(b.bytes[i])) {
            return 0;
        }
    }
    return 1;
}

/** One origin's ordered list of borrowed stylesheet ids. */
typedef struct robo_css_origin_sheets {
    robo_css_stylesheet_id_t *items; /* owned */
    size_t count;
    size_t capacity;
} robo_css_origin_sheets_t;

struct robo_css_style_context {
    robo_css_origin_sheets_t origins[ROBO_CSS_ORIGIN_COUNT];
    const robo_css_property_table_t *property_table;
    robo_css_element_lookup_fn lookup;
    void *lookup_userdata;
    robo_css_inline_style_fn inline_style;
    void *inline_userdata;
};

/* ------------------------------------------------------------------ */
/* Element view                                                        */
/* ------------------------------------------------------------------ */

void robo_css_element_view_init(robo_css_element_view_t *view)
{
    if (!view) {
        return;
    }
    view->ref = ROBO_CSS_ELEMENT_REF_NONE;
    view->tag_name = robo_css_slice_empty();
    view->id = robo_css_slice_empty();
    view->classes = NULL;
    view->class_count = 0u;
    view->attributes = NULL;
    view->attribute_count = 0u;
}

robo_result_t robo_css_element_view_set_element(robo_css_element_view_t *view,
                                                robo_css_element_ref_t ref,
                                                robo_css_slice_t tag_name,
                                                robo_css_diag_t *diag)
{
    if (!view) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "view is NULL");
    }
    if (ref == ROBO_CSS_ELEMENT_REF_NONE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "an element view needs an element");
    }
    if (robo_css_slice_is_empty(tag_name)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "an element view needs a tag name");
    }
    view->ref = ref;
    view->tag_name = tag_name;
    return ROBO_OK;
}

robo_result_t robo_css_element_view_set_id(robo_css_element_view_t *view,
                                           robo_css_slice_t id,
                                           robo_css_diag_t *diag)
{
    if (!view) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "view is NULL");
    }
    /* `id=""` is a real, empty id and is recorded as such; only "there is no id
     * attribute" is the absent slice, and that is what the caller passes. */
    view->id = id;
    return ROBO_OK;
}

robo_result_t robo_css_element_view_set_classes(robo_css_element_view_t *view,
                                                const robo_css_slice_t *classes,
                                                size_t class_count,
                                                robo_css_diag_t *diag)
{
    if (!view) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "view is NULL");
    }
    if (!classes && class_count > 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "NULL class array with a non-zero count");
    }
    view->classes = classes;
    view->class_count = class_count;
    return ROBO_OK;
}

robo_result_t robo_css_element_view_set_attributes(
    robo_css_element_view_t *view,
    const robo_css_attribute_t *attributes,
    size_t attribute_count,
    robo_css_diag_t *diag)
{
    if (!view) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "view is NULL");
    }
    if (!attributes && attribute_count > 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "NULL attribute array with a non-zero count");
    }
    view->attributes = attributes;
    view->attribute_count = attribute_count;
    return ROBO_OK;
}

int robo_css_element_view_is_set(const robo_css_element_view_t *view)
{
    return view && view->ref != ROBO_CSS_ELEMENT_REF_NONE;
}

int robo_css_element_view_find_attribute(const robo_css_element_view_t *view,
                                        robo_css_slice_t name,
                                        robo_css_slice_t *out_value)
{
    size_t i;

    if (!view || !view->attributes || robo_css_slice_is_empty(name)) {
        return 0;
    }
    for (i = 0u; i < view->attribute_count; i++) {
        if (slice_equals_ci(view->attributes[i].name, name)) {
            if (out_value) {
                *out_value = view->attributes[i].value;
            }
            return 1;
        }
    }
    return 0;
}

int robo_css_element_view_has_class(const robo_css_element_view_t *view,
                                    robo_css_slice_t name)
{
    size_t i;

    if (!view || !view->classes || robo_css_slice_is_empty(name)) {
        return 0;
    }
    for (i = 0u; i < view->class_count; i++) {
        /* Class matching is byte-exact: HTML class names are case-sensitive. */
        if (robo_css_slice_equals(view->classes[i], name)) {
            return 1;
        }
    }
    return 0;
}

int robo_css_element_view_id_equals(const robo_css_element_view_t *view,
                                    robo_css_slice_t id)
{
    if (!view) {
        return 0;
    }
    return robo_css_slice_equals(view->id, id);
}

/* ------------------------------------------------------------------ */
/* Style context                                                       */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_style_context_create(robo_css_style_context_t **out_context,
                                            robo_css_diag_t *diag)
{
    robo_css_style_context_t *context;

    if (!out_context) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "out_context is NULL");
    }
    *out_context = NULL;
    context = (robo_css_style_context_t *)robo_calloc(1u, sizeof(*context));
    if (!context) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_OUT_OF_MEMORY,
                             "could not allocate the style context");
    }
    *out_context = context;
    return ROBO_OK;
}

void robo_css_style_context_destroy(robo_css_style_context_t *context)
{
    size_t i;

    if (!context) {
        return;
    }
    for (i = 0u; i < (size_t)ROBO_CSS_ORIGIN_COUNT; i++) {
        if (context->origins[i].items) {
            robo_free(context->origins[i].items);
        }
    }
    robo_free(context);
}

static int origin_is_indexable(robo_css_origin_t origin)
{
    return origin > ROBO_CSS_ORIGIN_NONE && origin < (robo_css_origin_t)ROBO_CSS_ORIGIN_COUNT;
}

robo_result_t robo_css_style_context_add_sheet(robo_css_style_context_t *context,
                                              robo_css_origin_t origin,
                                              robo_css_stylesheet_id_t sheet,
                                              robo_css_diag_t *diag)
{
    robo_css_origin_sheets_t *list;

    if (!context) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "context is NULL");
    }
    if (!robo_css_origin_is_stylesheet_origin(origin)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "only runtime, user and author origins hold stylesheets");
    }
    if (sheet == ROBO_CSS_STYLESHEET_ID_NONE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "no stylesheet id given");
    }
    if (!robo_css_stylesheet_is_live(sheet)) {
        /* A context that accepted a dead id would fail much later, during a
         * resolution, in a place that does not explain where the id came from. */
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_NOT_FOUND,
                             "stylesheet id does not resolve");
    }
    list = &context->origins[(size_t)origin];
    if (!robo_css_grow((void **)&list->items, &list->capacity, list->count,
                       sizeof(robo_css_stylesheet_id_t))) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_OUT_OF_MEMORY,
                             "could not grow the origin's stylesheet list");
    }
    list->items[list->count] = sheet;
    list->count++;
    return ROBO_OK;
}

size_t robo_css_style_context_sheet_count(const robo_css_style_context_t *context,
                                         robo_css_origin_t origin)
{
    if (!context || !origin_is_indexable(origin)) {
        return 0u;
    }
    return context->origins[(size_t)origin].count;
}

robo_css_stylesheet_id_t robo_css_style_context_sheet(
    const robo_css_style_context_t *context,
    robo_css_origin_t origin,
    size_t index)
{
    if (!context || !origin_is_indexable(origin)) {
        return ROBO_CSS_STYLESHEET_ID_NONE;
    }
    if (index >= context->origins[(size_t)origin].count) {
        return ROBO_CSS_STYLESHEET_ID_NONE;
    }
    return context->origins[(size_t)origin].items[index];
}

void robo_css_style_context_clear_origin(robo_css_style_context_t *context,
                                        robo_css_origin_t origin)
{
    if (!context || !origin_is_indexable(origin)) {
        return;
    }
    /* The ids are borrowed, so clearing the list releases nothing but the list. */
    context->origins[(size_t)origin].count = 0u;
}

void robo_css_style_context_clear(robo_css_style_context_t *context)
{
    size_t i;

    if (!context) {
        return;
    }
    for (i = 0u; i < (size_t)ROBO_CSS_ORIGIN_COUNT; i++) {
        context->origins[i].count = 0u;
    }
}

size_t robo_css_style_context_total_sheet_count(
    const robo_css_style_context_t *context)
{
    size_t i;
    size_t total = 0u;

    if (!context) {
        return 0u;
    }
    for (i = 0u; i < (size_t)ROBO_CSS_ORIGIN_COUNT; i++) {
        total += context->origins[i].count;
    }
    return total;
}

robo_result_t robo_css_style_context_set_property_table(
    robo_css_style_context_t *context,
    const robo_css_property_table_t *table,
    robo_css_diag_t *diag)
{
    if (!context) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "context is NULL");
    }
    context->property_table = table; /* borrowing: NULL clears it */
    return ROBO_OK;
}

const robo_css_property_table_t *robo_css_style_context_property_table(
    const robo_css_style_context_t *context)
{
    return context ? context->property_table : NULL;
}

robo_result_t robo_css_style_context_set_element_lookup(
    robo_css_style_context_t *context,
    robo_css_element_lookup_fn lookup,
    void *userdata,
    robo_css_diag_t *diag)
{
    if (!context) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "context is NULL");
    }
    context->lookup = lookup;
    context->lookup_userdata = userdata;
    return ROBO_OK;
}

robo_css_element_lookup_fn robo_css_style_context_element_lookup(
    const robo_css_style_context_t *context)
{
    return context ? context->lookup : NULL;
}

robo_result_t robo_css_style_context_set_inline_style(
    robo_css_style_context_t *context,
    robo_css_inline_style_fn provider,
    void *userdata,
    robo_css_diag_t *diag)
{
    if (!context) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "context is NULL");
    }
    context->inline_style = provider;
    context->inline_userdata = userdata;
    return ROBO_OK;
}

robo_css_inline_style_fn robo_css_style_context_inline_style(
    const robo_css_style_context_t *context)
{
    return context ? context->inline_style : NULL;
}

int robo_css_style_context_has_runtime_sheet(
    const robo_css_style_context_t *context)
{
    return robo_css_style_context_sheet_count(context, ROBO_CSS_ORIGIN_RUNTIME) > 0u;
}

/* ------------------------------------------------------------------ */
/* Resolution boundaries                                               */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_style_context_match_declarations(
    const robo_css_style_context_t *context,
    robo_css_element_ref_t element,
    robo_css_declaration_list_t *out,
    robo_css_error_t *out_error,
    robo_css_diag_t *diag)
{
    if (!context || !out) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "context or output is NULL");
        return ROBO_INVALID_ARGUMENT;
    }
    /* The output is initialised and left empty: a caller that ignores the
     * result still has a valid, disposable object rather than an uninitialised
     * one it must not touch. */
    robo_css_declaration_list_init(out);
    (void)element;
    robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_UNSUPPORTED, ROBO_NOT_SUPPORTED,
                       NULL,
                       "selector matching and the cascade arrive in Goals 9.4 and 9.5");
    return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_NOT_SUPPORTED,
                         "selector matching and the cascade arrive in Goals 9.4 and 9.5");
}

robo_result_t robo_css_style_context_compute_style(
    const robo_css_style_context_t *context,
    robo_css_element_ref_t element,
    robo_css_computed_style_t *out_style,
    robo_css_error_t *out_error,
    robo_css_diag_t *diag)
{
    if (!context || !out_style) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "context or output is NULL");
        return ROBO_INVALID_ARGUMENT;
    }
    (void)element;
    robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_UNSUPPORTED, ROBO_NOT_SUPPORTED,
                       NULL,
                       "computed values arrive in Goal 9.8");
    return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_NOT_SUPPORTED,
                         "computed values arrive in Goal 9.8");
}
