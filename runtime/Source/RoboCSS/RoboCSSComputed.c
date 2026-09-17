/*
 * RoboCSS computed-style container (Phase 9.1) — see RoboCSSComputed.h.
 *
 * A container, not a computation. Values are moved in and owned here; setting
 * an existing property replaces in place so that iteration order reflects the
 * order properties were first introduced rather than how often they were
 * written, which is the difference between a deterministic container and one
 * that merely happens to be deterministic in simple tests.
 */

#include "RoboCSS/RoboCSSComputed.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

typedef struct robo_css_computed_entry {
    robo_css_property_id_t property;
    robo_css_value_t value; /* owns its children */
} robo_css_computed_entry_t;

struct robo_css_computed_style {
    robo_css_computed_entry_t *entries; /* owned */
    size_t count;
    size_t capacity;
};

robo_result_t robo_css_computed_style_create(robo_css_computed_style_t **out_style,
                                             robo_css_diag_t *diag)
{
    robo_css_computed_style_t *style;

    if (!out_style) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "out_style is NULL");
    }
    *out_style = NULL;
    style = (robo_css_computed_style_t *)robo_calloc(1u, sizeof(*style));
    if (!style) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                             "could not allocate the computed style");
    }
    *out_style = style;
    return ROBO_OK;
}

void robo_css_computed_style_clear(robo_css_computed_style_t *style)
{
    size_t i;

    if (!style) {
        return;
    }
    for (i = 0u; i < style->count; i++) {
        robo_css_value_dispose(&style->entries[i].value);
    }
    style->count = 0u;
}

void robo_css_computed_style_destroy(robo_css_computed_style_t *style)
{
    if (!style) {
        return;
    }
    robo_css_computed_style_clear(style);
    if (style->entries) {
        robo_free(style->entries);
    }
    robo_free(style);
}

/** Entry for @p property, or NULL when it is not present. */
static robo_css_computed_entry_t *computed_find(robo_css_computed_style_t *style,
                                                robo_css_property_id_t property)
{
    size_t i;

    if (!style || property == ROBO_CSS_PROPERTY_ID_NONE) {
        return NULL;
    }
    for (i = 0u; i < style->count; i++) {
        if (style->entries[i].property == property) {
            return &style->entries[i];
        }
    }
    return NULL;
}

robo_result_t robo_css_computed_style_set(robo_css_computed_style_t *style,
                                          robo_css_property_id_t property,
                                          robo_css_value_t *value,
                                          robo_css_diag_t *diag)
{
    robo_css_computed_entry_t *entry;

    if (!style || !value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "style or value is NULL");
    }
    if (property == ROBO_CSS_PROPERTY_ID_NONE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a computed value needs a property identity");
    }
    entry = computed_find(style, property);
    if (entry) {
        return robo_css_value_move(value, &entry->value, diag);
    }
    if (!robo_css_grow((void **)&style->entries, &style->capacity, style->count,
                       sizeof(robo_css_computed_entry_t))) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                             "could not grow the computed style");
    }
    entry = &style->entries[style->count];
    entry->property = property;
    robo_css_value_init(&entry->value);
    style->count++;
    return robo_css_value_move(value, &entry->value, diag);
}

const robo_css_value_t *robo_css_computed_style_get(
    const robo_css_computed_style_t *style,
    robo_css_property_id_t property)
{
    robo_css_computed_entry_t *entry =
        computed_find((robo_css_computed_style_t *)style, property);

    if (!entry) {
        return NULL;
    }
    return &entry->value;
}

int robo_css_computed_style_has(const robo_css_computed_style_t *style,
                                robo_css_property_id_t property)
{
    return robo_css_computed_style_get(style, property) != NULL;
}

robo_result_t robo_css_computed_style_remove(robo_css_computed_style_t *style,
                                             robo_css_property_id_t property,
                                             robo_css_diag_t *diag)
{
    size_t i;

    if (!style) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "style is NULL");
    }
    for (i = 0u; i < style->count; i++) {
        size_t j;

        if (style->entries[i].property != property) {
            continue;
        }
        robo_css_value_dispose(&style->entries[i].value);
        /* Move the tail down so the relative order of everything else is kept. */
        for (j = i + 1u; j < style->count; j++) {
            style->entries[j - 1u] = style->entries[j];
            style->entries[j].property = ROBO_CSS_PROPERTY_ID_NONE;
            robo_css_value_init(&style->entries[j].value);
        }
        style->count--;
        return ROBO_OK;
    }
    return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_FOUND,
                         "property is not in this computed style");
}

size_t robo_css_computed_style_count(const robo_css_computed_style_t *style)
{
    return style ? style->count : 0u;
}

robo_result_t robo_css_computed_style_property_at(
    const robo_css_computed_style_t *style,
    size_t index,
    robo_css_property_id_t *out_property,
    robo_css_diag_t *diag)
{
    if (!style || !out_property) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "style or output is NULL");
    }
    if (index >= style->count) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_FOUND,
                             "index is past the end of the computed style");
    }
    *out_property = style->entries[index].property;
    return ROBO_OK;
}

const robo_css_value_t *robo_css_computed_style_value_at(
    const robo_css_computed_style_t *style,
    size_t index)
{
    if (!style || index >= style->count) {
        return NULL;
    }
    return &style->entries[index].value;
}
