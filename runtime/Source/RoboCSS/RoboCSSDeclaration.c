/*
 * RoboCSS declaration model (Phase 9.1) — see RoboCSSDeclaration.h.
 *
 * Ownership is one-directional and never shared: a declaration owns exactly one
 * value tree, a list owns its declarations, and both hand ownership over on
 * append (the appended object is left NONE). The only borrowed part of a
 * declaration is its name slice, which points into the source the caller
 * attached to the stylesheet.
 */

#include "RoboCSS/RoboCSSDeclaration.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

int robo_css_declaration_state_is_known(robo_css_declaration_state_t state)
{
    return state >= ROBO_CSS_DECLARATION_STATE_NONE &&
           state < (robo_css_declaration_state_t)ROBO_CSS_DECLARATION_STATE_COUNT;
}

const char *robo_css_declaration_state_name(robo_css_declaration_state_t state)
{
    switch (state) {
    case ROBO_CSS_DECLARATION_STATE_NONE:
        return "none";
    case ROBO_CSS_DECLARATION_STATE_PARSED:
        return "parsed";
    case ROBO_CSS_DECLARATION_STATE_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}

void robo_css_declaration_init(robo_css_declaration_t *declaration)
{
    if (!declaration) {
        return;
    }
    declaration->state = ROBO_CSS_DECLARATION_STATE_NONE;
    declaration->name = robo_css_slice_empty();
    declaration->property = ROBO_CSS_PROPERTY_ID_NONE;
    declaration->important = 0;
    declaration->range = robo_css_range_at(0u);
    declaration->name_range = robo_css_range_at(0u);
    declaration->value_range = robo_css_range_at(0u);
    robo_css_value_init(&declaration->value);
}

void robo_css_declaration_dispose(robo_css_declaration_t *declaration)
{
    if (!declaration) {
        return;
    }
    robo_css_value_dispose(&declaration->value);
    robo_css_declaration_init(declaration);
}

robo_result_t robo_css_declaration_make(robo_css_declaration_t *declaration,
                                       robo_css_slice_t name,
                                       robo_css_declaration_state_t state,
                                       robo_css_diag_t *diag)
{
    if (!declaration) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DECLARATION, ROBO_INVALID_ARGUMENT,
                             "declaration is NULL");
    }
    if (state != ROBO_CSS_DECLARATION_STATE_PARSED &&
        state != ROBO_CSS_DECLARATION_STATE_INVALID) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DECLARATION, ROBO_INVALID_ARGUMENT,
                             "a declaration is made in the parsed or invalid state");
    }
    if (state == ROBO_CSS_DECLARATION_STATE_PARSED && robo_css_slice_is_empty(name)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DECLARATION, ROBO_INVALID_ARGUMENT,
                             "a parsed declaration needs a name");
    }
    robo_css_declaration_dispose(declaration);
    declaration->state = state;
    declaration->name = name;
    return ROBO_OK;
}

robo_result_t robo_css_declaration_set_important(robo_css_declaration_t *declaration,
                                                int important,
                                                robo_css_diag_t *diag)
{
    if (!declaration) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DECLARATION, ROBO_INVALID_ARGUMENT,
                             "declaration is NULL");
    }
    declaration->important = important ? 1 : 0;
    return ROBO_OK;
}

robo_result_t robo_css_declaration_set_value(robo_css_declaration_t *declaration,
                                            robo_css_value_t *value,
                                            robo_css_diag_t *diag)
{
    if (!declaration || !value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DECLARATION, ROBO_INVALID_ARGUMENT,
                             "declaration or value is NULL");
    }
    if (declaration->state == ROBO_CSS_DECLARATION_STATE_NONE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DECLARATION, ROBO_INVALID_ARGUMENT,
                             "declaration has not been made");
    }
    return robo_css_value_move(value, &declaration->value, diag);
}

robo_result_t robo_css_declaration_set_ranges(robo_css_declaration_t *declaration,
                                             robo_css_range_t range,
                                             robo_css_range_t name_range,
                                             robo_css_range_t value_range,
                                             robo_css_diag_t *diag)
{
    if (!declaration) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DECLARATION, ROBO_INVALID_ARGUMENT,
                             "declaration is NULL");
    }
    declaration->range = range;
    declaration->name_range = name_range;
    declaration->value_range = value_range;
    return ROBO_OK;
}

robo_css_property_id_t robo_css_declaration_resolve_property(
    robo_css_declaration_t *declaration,
    const robo_css_property_table_t *table)
{
    if (!declaration || !table) {
        return ROBO_CSS_PROPERTY_ID_NONE;
    }
    declaration->property = robo_css_property_table_lookup_slice(table,
                                                                 declaration->name);
    return declaration->property;
}

int robo_css_declaration_has_value(const robo_css_declaration_t *declaration)
{
    return declaration && !robo_css_value_is_empty(&declaration->value);
}

int robo_css_declaration_is_usable(const robo_css_declaration_t *declaration)
{
    if (!declaration) {
        return 0;
    }
    if (declaration->state != ROBO_CSS_DECLARATION_STATE_PARSED) {
        return 0;
    }
    return !robo_css_slice_is_empty(declaration->name);
}

robo_css_slice_t robo_css_declaration_name(const robo_css_declaration_t *declaration)
{
    if (!declaration) {
        return robo_css_slice_empty();
    }
    return declaration->name;
}

const robo_css_value_t *robo_css_declaration_value(
    const robo_css_declaration_t *declaration)
{
    if (!declaration) {
        return NULL;
    }
    return &declaration->value;
}

/* ------------------------------------------------------------------ */
/* Declaration lists                                                   */
/* ------------------------------------------------------------------ */

void robo_css_declaration_list_init(robo_css_declaration_list_t *list)
{
    if (!list) {
        return;
    }
    list->items = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

void robo_css_declaration_list_clear(robo_css_declaration_list_t *list)
{
    size_t i;

    if (!list) {
        return;
    }
    for (i = 0u; i < list->count; i++) {
        robo_css_declaration_dispose(&list->items[i]);
    }
    list->count = 0u;
}

void robo_css_declaration_list_dispose(robo_css_declaration_list_t *list)
{
    if (!list) {
        return;
    }
    robo_css_declaration_list_clear(list);
    if (list->items) {
        robo_free(list->items);
    }
    robo_css_declaration_list_init(list);
}

robo_result_t robo_css_declaration_list_append(robo_css_declaration_list_t *list,
                                               robo_css_declaration_t *declaration,
                                               robo_css_diag_t *diag)
{
    if (!list || !declaration) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DECLARATION, ROBO_INVALID_ARGUMENT,
                             "list or declaration is NULL");
    }
    if (declaration->state == ROBO_CSS_DECLARATION_STATE_NONE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DECLARATION, ROBO_INVALID_ARGUMENT,
                             "declaration has not been made");
    }
    if (!robo_css_grow((void **)&list->items, &list->capacity, list->count,
                       sizeof(robo_css_declaration_t))) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DECLARATION, ROBO_OUT_OF_MEMORY,
                             "could not grow the declaration list");
    }
    list->items[list->count] = *declaration;
    list->count++;
    robo_css_declaration_init(declaration);
    return ROBO_OK;
}

size_t robo_css_declaration_list_count(const robo_css_declaration_list_t *list)
{
    return list ? list->count : 0u;
}

const robo_css_declaration_t *robo_css_declaration_list_at(
    const robo_css_declaration_list_t *list,
    size_t index)
{
    if (!list || index >= list->count) {
        return NULL;
    }
    return &list->items[index];
}

int robo_css_declaration_list_is_empty(const robo_css_declaration_list_t *list)
{
    return !list || list->count == 0u;
}

int robo_css_declaration_list_find_first(const robo_css_declaration_list_t *list,
                                        robo_css_property_id_t property,
                                        size_t *out_index)
{
    size_t i;

    if (!list || property == ROBO_CSS_PROPERTY_ID_NONE) {
        return 0;
    }
    for (i = 0u; i < list->count; i++) {
        if (list->items[i].property == property) {
            if (out_index) {
                *out_index = i;
            }
            return 1;
        }
    }
    return 0;
}

int robo_css_declaration_list_find_last(const robo_css_declaration_list_t *list,
                                       robo_css_property_id_t property,
                                       size_t *out_index)
{
    size_t i;

    if (!list || property == ROBO_CSS_PROPERTY_ID_NONE) {
        return 0;
    }
    i = list->count;
    while (i > 0u) {
        i--;
        if (list->items[i].property == property) {
            if (out_index) {
                *out_index = i;
            }
            return 1;
        }
    }
    return 0;
}

size_t robo_css_declaration_list_count_of(const robo_css_declaration_list_t *list,
                                         robo_css_property_id_t property)
{
    size_t i;
    size_t count = 0u;

    if (!list || property == ROBO_CSS_PROPERTY_ID_NONE) {
        return 0u;
    }
    for (i = 0u; i < list->count; i++) {
        if (list->items[i].property == property) {
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Inline-style boundary                                               */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_declaration_list_parse_text(const char *text,
                                                   size_t length,
                                                   robo_css_origin_t origin,
                                                   robo_css_declaration_list_t *out_list,
                                                   robo_css_error_t *out_error)
{
    if (!out_list) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!text && length > 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* The list is initialised even though nothing is parsed into it, so a
     * caller can dispose it unconditionally and never has to guess. */
    robo_css_declaration_list_init(out_list);
    (void)origin; /* recorded by the caller; provenance does not change the text */
    robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_UNSUPPORTED,
                       ROBO_NOT_SUPPORTED, NULL,
                       "declaration parsing arrives with the CSS parser (Goal 9.3)");
    return ROBO_NOT_SUPPORTED;
}
