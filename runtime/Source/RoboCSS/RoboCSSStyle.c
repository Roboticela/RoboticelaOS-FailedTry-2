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
/* For robo_css_cascade_init and robo_css_cascade_collect: this translation unit
 * owns the door, the cascade owns what comes out of it. */
#include "RoboCSS/RoboCSSCascade.h"
/* For robo_css_inherit_resolve and the resolved-style accessors: this
 * translation unit owns the door, inheritance owns what comes out of it. */
#include "RoboCSS/RoboCSSInherit.h"
#include "RoboMem/RoboMem.h"

/** One origin's ordered list of borrowed stylesheet ids. */
typedef struct robo_css_origin_sheets {
    robo_css_stylesheet_id_t *items; /* owned */
    size_t count;
    size_t capacity;
} robo_css_origin_sheets_t;

struct robo_css_style_context {
    robo_css_origin_sheets_t origins[ROBO_CSS_ORIGIN_COUNT];
    const robo_css_property_table_t *property_table;
    /* Borrowed, like the table: which properties are classified is the caller's
     * policy, and the registry is where that policy lives (9.7). */
    const robo_css_property_registry_t *property_registry;
    /* The one place a host is stored. set_element_lookup writes this too, so
     * there is a single answer to "which callbacks are installed" rather than a
     * lookup slot and a host that could disagree. */
    robo_css_element_host_t host;
    robo_css_inline_style_fn inline_style;
    void *inline_userdata;
};

/* ------------------------------------------------------------------ */
/* Element host                                                        */
/* ------------------------------------------------------------------ */

void robo_css_element_host_init(robo_css_element_host_t *host)
{
    if (!host) {
        return;
    }
    host->userdata = NULL;
    host->view = NULL;
    host->parent = NULL;
    host->previous_sibling = NULL;
    host->next_sibling = NULL;
}

int robo_css_element_host_is_usable(const robo_css_element_host_t *host)
{
    return host && host->view != NULL;
}

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
        if (robo_css_slice_equals_ci(view->attributes[i].name, name)) {
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

robo_result_t robo_css_style_context_set_property_registry(
    robo_css_style_context_t *context,
    const robo_css_property_registry_t *registry,
    robo_css_diag_t *diag)
{
    if (!context) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "context is NULL");
    }
    context->property_registry = registry; /* borrowing: NULL clears it */
    return ROBO_OK;
}

const robo_css_property_registry_t *robo_css_style_context_property_registry(
    const robo_css_style_context_t *context)
{
    return context ? context->property_registry : NULL;
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
    if (lookup) {
        context->host.view = lookup;
        context->host.userdata = userdata;
    } else {
        /* Clearing the lookup clears the host: a host installs its callbacks as
         * a set, and leaving relations behind that no longer have a view to
         * describe elements would be half a host. */
        robo_css_element_host_init(&context->host);
    }
    return ROBO_OK;
}

robo_css_element_lookup_fn robo_css_style_context_element_lookup(
    const robo_css_style_context_t *context)
{
    return context ? context->host.view : NULL;
}

robo_result_t robo_css_style_context_set_element_host(
    robo_css_style_context_t *context,
    const robo_css_element_host_t *host,
    robo_css_diag_t *diag)
{
    if (!context) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "context is NULL");
    }
    if (!host) {
        robo_css_element_host_init(&context->host);
        return ROBO_OK;
    }
    if (!host->view &&
        (host->parent || host->previous_sibling || host->next_sibling)) {
        /* A host that can say where an element sits but not what it is has
         * nothing a view could be filled from, so every selector would be
         * unevaluatable. Refusing here says so at the install site rather than
         * at the first match. */
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "a host with relations needs a view callback");
    }
    context->host = *host;
    return ROBO_OK;
}

robo_css_element_host_t robo_css_style_context_element_host(
    const robo_css_style_context_t *context)
{
    robo_css_element_host_t host;

    robo_css_element_host_init(&host);
    if (context) {
        host = context->host;
    }
    return host;
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

void *robo_css_style_context_inline_userdata(const robo_css_style_context_t *context)
{
    return context ? context->inline_userdata : NULL;
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
    robo_css_cascade_t *out,
    robo_css_error_t *out_error,
    robo_css_diag_t *diag)
{
    if (!context || !out) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "context or output is NULL");
        return ROBO_INVALID_ARGUMENT;
    }
    if (element == ROBO_CSS_ELEMENT_REF_NONE) {
        /* A cascade is collected *for* an element. Nothing applies to no
         * element, so answering with an empty result would be a plausible lie
         * rather than a refusal, and every counter on the result would be
         * zero for a reason that had nothing to do with the stylesheets. */
        robo_css_cascade_init(out);
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "a collection needs an element");
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "a collection needs an element");
    }
    /* The output is initialised first: a caller that ignores the result still
     * has a valid, disposable object rather than an uninitialised one. */
    return robo_css_cascade_collect(context, element, out, out_error, diag);
}

/**
 * Collect @p element's ancestor chain into @p chain, **leaf first**.
 *
 * The walk every resolution door in this file starts with, and the reason both
 * of them share it: "which elements are above this one" is a question about the
 * host, and a second answer to it would be a second chance to disagree with the
 * first about where a chain ends or when it is too long.
 *
 * Up first, resolve down afterwards — that order is what makes "the immediate
 * resolved parent" literal, because the level above is resolved before the
 * level below needs it.
 *
 * The chain is bounded by ROBO_CSS_INHERIT_MAX_DEPTH, and hitting the bound is
 * *reported* through @p out_capped rather than hidden: a DOM that reports a
 * cycle among parents (`a`'s parent is `b`, `b`'s is `a`) would otherwise walk
 * forever, and a truncated chain is not a root.
 *
 * @param capacity  entries @p chain can hold; must be at least 1.
 * @return the number of levels collected, at least 1 for a real element.
 */
static size_t style_chain_collect(const robo_css_style_context_t *context,
                                  robo_css_element_ref_t element,
                                  robo_css_element_ref_t *chain,
                                  size_t capacity,
                                  int *out_capped)
{
    robo_css_element_host_t host = robo_css_style_context_element_host(context);
    size_t chain_length = 0u;

    *out_capped = 0;
    chain[chain_length++] = element;
    while (host.parent) {
        robo_css_element_ref_t next;

        if (chain_length >= capacity) {
            /* Ask once more, so a chain that happens to be exactly the bound
             * long is not reported as truncated. */
            *out_capped = host.parent(host.userdata, chain[chain_length - 1u]) !=
                          ROBO_CSS_ELEMENT_REF_NONE;
            break;
        }
        next = host.parent(host.userdata, chain[chain_length - 1u]);
        if (next == ROBO_CSS_ELEMENT_REF_NONE) {
            break;
        }
        chain[chain_length++] = next;
    }
    return chain_length;
}

robo_result_t robo_css_style_context_resolve_inherited_style(
    const robo_css_style_context_t *context,
    robo_css_element_ref_t element,
    robo_css_inherited_style_t *out,
    robo_css_error_t *out_error,
    robo_css_diag_t *diag)
{
    /* One stack array rather than an allocation: the chain is bounded by the
     * same constant the walk is, and 64 element references is a size a frame
     * can hold. */
    robo_css_element_ref_t chain[ROBO_CSS_INHERIT_MAX_DEPTH];
    robo_css_inherited_style_t below = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t current = ROBO_CSS_CLEARED;
    size_t chain_length = 0u;
    size_t index;
    int capped = 0;
    int error_recorded = 0;
    robo_result_t rc = ROBO_OK;

    if (!context || !out) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "context or output is NULL");
        return ROBO_INVALID_ARGUMENT;
    }
    robo_css_inherited_style_init(out);
    robo_css_error_init(out_error);
    if (element == ROBO_CSS_ELEMENT_REF_NONE) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "a resolution needs an element");
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_INHERIT, ROBO_INVALID_ARGUMENT,
                             "a resolution needs an element");
    }

    chain_length = style_chain_collect(context, element, chain,
                                       (size_t)ROBO_CSS_INHERIT_MAX_DEPTH, &capped);

    for (index = chain_length; index-- > 0u;) {
        robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;

        rc = robo_css_cascade_collect(context, chain[index], &cascade, &error, diag);
        if (rc != ROBO_OK) {
            break;
        }
        /* The first CSS problem met anywhere on the chain, which is the same
         * rule every other door follows. */
        if (!error_recorded && robo_css_error_is_error(&error)) {
            if (out_error) {
                *out_error = error;
            }
            error_recorded = 1;
        }
        rc = robo_css_inherit_resolve(context, &cascade,
                                      index + 1u < chain_length ? &below : NULL,
                                      &current, diag);
        robo_css_cascade_dispose(&cascade);
        if (rc != ROBO_OK) {
            break;
        }
        robo_css_inherited_style_dispose(&below);
        below = current;
        robo_css_inherited_style_init(&current);
    }

    if (rc != ROBO_OK) {
        robo_css_inherited_style_dispose(&below);
        robo_css_inherited_style_dispose(&current);
        /* Nothing was produced, so the caller gets an empty style rather than a
         * partial one it might mistake for an answer. */
        robo_css_inherited_style_init(out);
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, rc, NULL,
                           "the inherited style could not be resolved");
        return rc;
    }

    *out = below;
    robo_css_inherited_style_init(&below);
    if (capped) {
        /* The ancestor chain was longer than the walk goes, so the level this
         * resolution treated as a root is not one. Reported rather than
         * hidden: every value inherited from above that point is a lower bound. */
        out->unsupported = 1;
    }
    return ROBO_OK;
}

robo_result_t robo_css_style_context_compute_style(
    const robo_css_style_context_t *context,
    robo_css_element_ref_t element,
    robo_css_computed_style_t *out_style,
    robo_css_error_t *out_error,
    robo_css_diag_t *diag)
{
    /* Two computed styles, swapped each level: the one just computed is the
     * parent for the next, and the other is scratch. Nothing accumulates — the
     * intermediate styles cost one level each and are gone the moment the level
     * below has read them, which is what keeps computing a deep tree linear
     * rather than quadratic. */
    robo_css_element_ref_t chain[ROBO_CSS_INHERIT_MAX_DEPTH];
    robo_css_inherited_style_t below = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t current = ROBO_CSS_CLEARED;
    robo_css_computed_style_t *upper = NULL;
    robo_css_computed_style_t *lower = NULL;
    size_t chain_length = 0u;
    size_t index;
    int capped = 0;
    int error_recorded = 0;
    robo_result_t rc;

    if (!context || !out_style) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "context or output is NULL");
        return ROBO_INVALID_ARGUMENT;
    }
    robo_css_error_init(out_error);
    robo_css_computed_style_clear(out_style);
    if (element == ROBO_CSS_ELEMENT_REF_NONE) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "a computation needs an element");
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, ROBO_INVALID_ARGUMENT,
                             "a computation needs an element");
    }

    chain_length = style_chain_collect(context, element, chain,
                                       (size_t)ROBO_CSS_INHERIT_MAX_DEPTH, &capped);
    rc = robo_css_computed_style_create(&upper, diag);
    if (rc == ROBO_OK) {
        rc = robo_css_computed_style_create(&lower, diag);
    }
    if (rc != ROBO_OK) {
        robo_css_computed_style_destroy(upper);
        robo_css_computed_style_destroy(lower);
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, rc, NULL,
                           "the computed style could not be built");
        return rc;
    }

    /* Root first, so every level below has its own computed parent in hand. */
    for (index = chain_length; index-- > 0u;) {
        robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;

        rc = robo_css_cascade_collect(context, chain[index], &cascade, &error, diag);
        if (rc != ROBO_OK) {
            break;
        }
        /* The first CSS problem met anywhere on the chain, which is the same
         * rule every other door follows. */
        if (!error_recorded && robo_css_error_is_error(&error)) {
            if (out_error) {
                *out_error = error;
            }
            error_recorded = 1;
        }
        rc = robo_css_inherit_resolve(context, &cascade,
                                      index + 1u < chain_length ? &below : NULL,
                                      &current, diag);
        robo_css_cascade_dispose(&cascade);
        if (rc != ROBO_OK) {
            break;
        }

        robo_css_computed_style_clear(lower);
        rc = robo_css_style_context_compute_resolved_style(context, &current, upper,
                                                           lower, &error, diag);
        if (rc != ROBO_OK) {
            break;
        }
        if (!error_recorded && robo_css_error_is_error(&error)) {
            if (out_error) {
                *out_error = error;
            }
            error_recorded = 1;
        }

        /* This level's computed style becomes the next level's parent. */
        {
            robo_css_computed_style_t *swap = upper;

            upper = lower;
            lower = swap;
        }
        robo_css_inherited_style_dispose(&below);
        below = current;
        robo_css_inherited_style_init(&current);
    }

    robo_css_inherited_style_dispose(&below);
    robo_css_inherited_style_dispose(&current);

    if (rc != ROBO_OK) {
        robo_css_computed_style_destroy(upper);
        robo_css_computed_style_destroy(lower);
        robo_css_computed_style_clear(out_style);
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, rc, NULL,
                           "the computed style could not be built");
        return rc;
    }

    /* The last level computed is the element's own, and it is `upper` after the
     * final swap. Moving rather than copying: the caller asked for this style
     * and there is no reason to duplicate every value to hand it over. */
    rc = robo_css_computed_style_take(out_style, upper, diag);
    if (rc != ROBO_OK) {
        robo_css_computed_style_destroy(upper);
        robo_css_computed_style_destroy(lower);
        robo_css_computed_style_clear(out_style);
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, rc, NULL,
                           "the computed style could not be handed over");
        return rc;
    }
    if (capped) {
        /* Every value inherited from above the point the walk stopped is a
         * lower bound, and saying so is what keeps the style from being read as
         * complete. */
        robo_css_computed_style_mark_truncated(out_style);
    }
    robo_css_computed_style_destroy(upper);
    robo_css_computed_style_destroy(lower);
    return ROBO_OK;
}
