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

#include "RoboCSS/RoboCSSInherit.h"
#include "RoboCSS/RoboCSSStyle.h"
#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

typedef struct robo_css_computed_entry {
    robo_css_property_id_t property;
    /** Owned, NUL-terminated spelling; NULL when the entry has none. */
    char *name;
    size_t name_length;
    /** Bitmask of robo_css_computed_flag_t. */
    uint32_t flags;
    /** The specified value; owns its children. */
    robo_css_value_t value;
    /** The typed computed value; KIND_NONE when nobody computed it. */
    robo_css_computed_value_t computed;
} robo_css_computed_entry_t;

struct robo_css_computed_style {
    robo_css_computed_entry_t *entries; /* owned */
    size_t count;
    size_t capacity;
    /** Non-zero when the walk behind this style hit its ancestor bound. */
    int truncated;
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
        robo_css_computed_value_dispose(&style->entries[i].computed);
        robo_free(style->entries[i].name);
        style->entries[i].name = NULL;
        style->entries[i].name_length = 0u;
        style->entries[i].flags = ROBO_CSS_COMPUTED_FLAG_NONE;
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

/**
 * Does @p entry name the property @p name is looking for?
 *
 * CSS's own rule, stated once: an ordinary property name is compared ASCII
 * case-insensitively, a custom property byte-exactly. The property registry and
 * the inheritance model use the same rule, and they have to — a lookup that
 * disagreed with registration about whether `--X` and `--x` are one property
 * would find the wrong entry, or none.
 */
static int computed_entry_matches(const robo_css_computed_entry_t *entry,
                                  robo_css_slice_t name)
{
    robo_css_slice_t stored;

    if (!entry->name) {
        return 0;
    }
    stored = robo_css_slice_from_bytes((const uint8_t *)entry->name, entry->name_length);
    if (robo_css_is_custom_property_name(name) ||
        robo_css_is_custom_property_name(stored)) {
        return robo_css_slice_equals(stored, name);
    }
    return robo_css_slice_equals_ci(stored, name);
}

/** The entry for @p name, or NULL. */
static robo_css_computed_entry_t *computed_find_name(
    const robo_css_computed_style_t *style,
    robo_css_slice_t name)
{
    size_t i;

    if (!style || robo_css_slice_is_absent(name)) {
        return NULL;
    }
    for (i = 0u; i < style->count; i++) {
        if (computed_entry_matches(&style->entries[i], name)) {
            return &style->entries[i];
        }
    }
    return NULL;
}

/** Borrow a fresh entry for @p property, appending it. NULL on failure. */
static robo_css_computed_entry_t *computed_claim(robo_css_computed_style_t *style,
                                                 robo_css_property_id_t property)
{
    if (!robo_css_grow((void **)&style->entries, &style->capacity, style->count,
                       sizeof(robo_css_computed_entry_t))) {
        return NULL;
    }
    memset(&style->entries[style->count], 0, sizeof(robo_css_computed_entry_t));
    style->entries[style->count].property = property;
    style->entries[style->count].flags = ROBO_CSS_COMPUTED_FLAG_NONE;
    robo_css_value_init(&style->entries[style->count].value);
    robo_css_computed_value_init(&style->entries[style->count].computed);
    style->count++;
    return &style->entries[style->count - 1u];
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
    if (!entry) {
        entry = computed_claim(style, property);
        if (!entry) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                                 "could not grow the computed style");
        }
    }
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
        robo_css_computed_value_dispose(&style->entries[i].computed);
        robo_free(style->entries[i].name);
        /* Move the tail down so the relative order of everything else is kept. */
        for (j = i + 1u; j < style->count; j++) {
            style->entries[j - 1u] = style->entries[j];
            style->entries[j].property = ROBO_CSS_PROPERTY_ID_NONE;
            style->entries[j].name = NULL;
            style->entries[j].name_length = 0u;
            style->entries[j].flags = ROBO_CSS_COMPUTED_FLAG_NONE;
            robo_css_value_init(&style->entries[j].value);
            robo_css_computed_value_init(&style->entries[j].computed);
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

/* ------------------------------------------------------------------ */
/* The computed half of an entry (9.8)                                 */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_computed_style_set_entry(
    robo_css_computed_style_t *style,
    robo_css_property_id_t property,
    robo_css_slice_t name,
    uint32_t flags,
    robo_css_value_t *specified,
    robo_css_computed_value_t *computed,
    robo_css_diag_t *diag)
{
    robo_css_computed_entry_t *entry;
    char *name_copy = NULL;

    if (!style) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "style is NULL");
    }
    if (property == ROBO_CSS_PROPERTY_ID_NONE && robo_css_slice_is_absent(name)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "an entry needs an identity or a name");
    }

    /* Find or claim first, but copy the name *before* moving anything in, so a
     * failure to allocate the name leaves the style exactly as it was rather
     * than with half an entry. */
    entry = property != ROBO_CSS_PROPERTY_ID_NONE ? computed_find(style, property)
                                                  : computed_find_name(style, name);
    if (!entry) {
        entry = computed_claim(style, property);
        if (!entry) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                                 "could not grow the computed style");
        }
    }
    if (!robo_css_slice_is_absent(name)) {
        name_copy = (char *)robo_malloc(name.length + 1u);
        if (!name_copy) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                                 "could not copy the property name");
        }
        if (name.length > 0u) {
            memcpy(name_copy, name.bytes, name.length);
        }
        name_copy[name.length] = '\0';
        robo_free(entry->name);
        entry->name = name_copy;
        entry->name_length = name.length;
    }
    if (property != ROBO_CSS_PROPERTY_ID_NONE) {
        entry->property = property;
    }
    entry->flags = flags;
    if (specified) {
        robo_result_t result = robo_css_value_move(specified, &entry->value, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }
    if (computed) {
        return robo_css_computed_value_move(computed, &entry->computed, diag);
    }
    return ROBO_OK;
}

size_t robo_css_computed_style_find(const robo_css_computed_style_t *style,
                                    robo_css_slice_t name)
{
    size_t i;

    if (!style || robo_css_slice_is_absent(name)) {
        return (size_t)-1;
    }
    for (i = 0u; i < style->count; i++) {
        if (computed_entry_matches(&style->entries[i], name)) {
            return i;
        }
    }
    return (size_t)-1;
}

robo_css_slice_t robo_css_computed_style_name_at(
    const robo_css_computed_style_t *style,
    size_t index)
{
    if (!style || index >= style->count || !style->entries[index].name) {
        return robo_css_slice_empty();
    }
    return robo_css_slice_from_bytes((const uint8_t *)style->entries[index].name,
                                     style->entries[index].name_length);
}

uint32_t robo_css_computed_style_flags_at(const robo_css_computed_style_t *style,
                                          size_t index)
{
    if (!style || index >= style->count) {
        return ROBO_CSS_COMPUTED_FLAG_NONE;
    }
    return style->entries[index].flags;
}

const robo_css_computed_value_t *robo_css_computed_style_computed_at(
    const robo_css_computed_style_t *style,
    size_t index)
{
    if (!style || index >= style->count ||
        style->entries[index].computed.kind == ROBO_CSS_COMPUTED_VALUE_NONE) {
        return NULL;
    }
    return &style->entries[index].computed;
}

const robo_css_computed_value_t *robo_css_computed_style_computed(
    const robo_css_computed_style_t *style,
    robo_css_property_id_t property)
{
    robo_css_computed_entry_t *entry =
        computed_find((robo_css_computed_style_t *)style, property);

    if (!entry || entry->computed.kind == ROBO_CSS_COMPUTED_VALUE_NONE) {
        return NULL;
    }
    return &entry->computed;
}

const robo_css_computed_value_t *robo_css_computed_style_computed_by_name(
    const robo_css_computed_style_t *style,
    robo_css_slice_t name)
{
    robo_css_computed_entry_t *entry = computed_find_name(style, name);

    if (!entry || entry->computed.kind == ROBO_CSS_COMPUTED_VALUE_NONE) {
        return NULL;
    }
    return &entry->computed;
}

uint32_t robo_css_computed_style_flags(const robo_css_computed_style_t *style,
                                       robo_css_slice_t name)
{
    robo_css_computed_entry_t *entry = computed_find_name(style, name);

    return entry ? entry->flags : ROBO_CSS_COMPUTED_FLAG_NONE;
}

const robo_css_value_t *robo_css_computed_style_specified_by_name(
    const robo_css_computed_style_t *style,
    robo_css_slice_t name)
{
    robo_css_computed_entry_t *entry = computed_find_name(style, name);

    if (!entry || entry->value.kind == ROBO_CSS_VALUE_KIND_NONE) {
        return NULL;
    }
    return &entry->value;
}

int robo_css_computed_style_has_flag(const robo_css_computed_style_t *style,
                                     robo_css_slice_t name,
                                     uint32_t flag)
{
    return (robo_css_computed_style_flags(style, name) & flag) != 0u;
}

int robo_css_computed_style_has_invalid(const robo_css_computed_style_t *style)
{
    size_t i;

    if (!style) {
        return 0;
    }
    for (i = 0u; i < style->count; i++) {
        if ((style->entries[i].flags & ROBO_CSS_COMPUTED_FLAG_INVALID) != 0u) {
            return 1;
        }
    }
    return 0;
}

int robo_css_computed_style_is_truncated(const robo_css_computed_style_t *style)
{
    return style ? style->truncated : 0;
}

void robo_css_computed_style_mark_truncated(robo_css_computed_style_t *style)
{
    if (style) {
        style->truncated = 1;
    }
}

size_t robo_css_computed_style_incomplete_count(
    const robo_css_computed_style_t *style)
{
    size_t i;
    size_t count = 0u;

    if (!style) {
        return 0u;
    }
    for (i = 0u; i < style->count; i++) {
        uint32_t flags = style->entries[i].flags;

        if ((flags & (ROBO_CSS_COMPUTED_FLAG_INVALID |
                      ROBO_CSS_COMPUTED_FLAG_UNRESOLVED)) != 0u) {
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Moving a whole style                                               */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_computed_style_take(robo_css_computed_style_t *dest,
                                           robo_css_computed_style_t *src,
                                           robo_css_diag_t *diag)
{
    if (!dest || !src || dest == src) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a computed style cannot take from itself");
    }
    /* Room first, clearing second: a caller that ignores the result code still
     * owns whatever it owned before. */
    while (dest->capacity < src->count) {
        if (!robo_css_grow((void **)&dest->entries, &dest->capacity,
                           src->count - 1u, sizeof(robo_css_computed_entry_t))) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                                 "the computed style could not be enlarged");
        }
    }
    robo_css_computed_style_clear(dest);
    dest->truncated = src->truncated;
    if (src->count > 0u) {
        memcpy(dest->entries, src->entries,
               src->count * sizeof(robo_css_computed_entry_t));
        dest->count = src->count;
        /* The source keeps its array but owns none of its contents any more. */
        memset(src->entries, 0, src->count * sizeof(robo_css_computed_entry_t));
        src->count = 0u;
    }
    src->truncated = 0;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Computing one resolved style (9.8)                                 */
/* ------------------------------------------------------------------ */

/** The flags @p source implies, before anything about the value is known. */
static uint32_t computed_flags_of_source(robo_css_inherited_source_t source)
{
    switch (source) {
    case ROBO_CSS_INHERITED_SOURCE_DECLARED:
        return ROBO_CSS_COMPUTED_FLAG_DECLARED;
    case ROBO_CSS_INHERITED_SOURCE_PARENT:
        return ROBO_CSS_COMPUTED_FLAG_INHERITED;
    case ROBO_CSS_INHERITED_SOURCE_DEFAULT:
        return ROBO_CSS_COMPUTED_FLAG_DEFAULT;
    case ROBO_CSS_INHERITED_SOURCE_DEFERRED:
        /* `revert` is carried, not obeyed, and a carried keyword is not a value
         * this build can compute — so it is unresolved as well as deferred. */
        return ROBO_CSS_COMPUTED_FLAG_DEFERRED | ROBO_CSS_COMPUTED_FLAG_UNRESOLVED;
    default:
        return ROBO_CSS_COMPUTED_FLAG_NONE;
    }
}

/*
 * Resolving `currentColor` (9.9)
 *
 * `currentColor` is a colour that is not a colour yet: it is whatever the
 * element's `color` turns out to be, and *which* `color` that is depends on the
 * property it was written on. That makes this the only layer that can resolve
 * it — the value layer has no element and no parent — and the two rules are:
 *
 *   on `color` itself     the **parent's** computed `color`.
 *                         `color: currentColor` means "the colour I would have
 *                         inherited", which is CSS's own rule and the only
 *                         reading that terminates: the element's own `color` is
 *                         the thing being computed.
 *   on anything else      the element's own computed `color`.
 *                         `background-color: currentColor` is "my text colour",
 *                         so it resolves against a value that must already be
 *                         final — which is why `color` is resolved first.
 *
 * Both rules are applied in place, on a finished style: a `currentColor` becomes
 * the channels it stands for, and the UNRESOLVED bit comes off, so a consumer
 * reading the style sees a colour rather than a promise. When there is nothing to
 * resolve it against — no parent computed style, which is what a root element and
 * a caller that computed one style at a time both look like — the colour **stays**
 * CURRENT and **stays** flagged unresolved. That is the honest answer, and
 * inventing black would be exactly the mistake `currentColor` exists to prevent.
 *
 * Only the UNRESOLVED *bit* is cleared, never the whole flag set: an entry can be
 * declared and unresolved and inherited all at once, and the other facts about it
 * are still true after its colour is known. Clearing it is safe for a colour
 * specifically because a COLOR value's own unresolved flag is set in exactly one
 * place — the `currentColor` branch of the colour computation — and not by
 * anything else that could leave the bit meaning something different.
 */

/** The property `currentColor` defers to. */
#define COMPUTED_COLOR_PROPERTY "color"

/**
 * Replace one entry's contextual colour with @p color, when it resolves against
 * it.
 *
 * A no-op for every entry that is not a `currentColor`, which is what makes the
 * pass below read as one sentence.
 */
static void computed_resolve_entry_color(robo_css_computed_entry_t *entry,
                                         robo_css_color_t color)
{
    robo_css_color_t resolved;

    if (!entry || entry->computed.kind != ROBO_CSS_COMPUTED_VALUE_COLOR) {
        return;
    }
    if (!robo_css_color_is_current(entry->computed.color)) {
        return;
    }
    if (!robo_css_color_resolve_current(entry->computed.color, color, &resolved)) {
        /* `color` is not a resolved colour here either: the colour stays
         * contextual, and the flag keeps saying so rather than a default being
         * filled in behind the author's back. */
        return;
    }
    entry->computed.color = resolved;
    entry->computed.unresolved = 0;
    entry->flags &= ~(uint32_t)ROBO_CSS_COMPUTED_FLAG_UNRESOLVED;
}

/** The computed colour of the entry for @p name, or NONE when there is none. */
static robo_css_color_t computed_color_of(const robo_css_computed_style_t *style,
                                          const char *name)
{
    const robo_css_computed_value_t *value;

    if (!style) {
        return robo_css_color_none();
    }
    value = robo_css_computed_style_computed_by_name(style,
                                                     robo_css_slice_from_text(name));
    if (!value || value->kind != ROBO_CSS_COMPUTED_VALUE_COLOR) {
        return robo_css_color_none();
    }
    return value->color;
}

/**
 * Resolve every `currentColor` in @p out against @p parent_computed.
 *
 * The order is the rule: `color` first, against the parent, so that the element
 * has a final colour; then everything else, against that. Nothing here can read
 * a value it has not settled, which is what makes two properties that both say
 * `currentColor` agree no matter which was declared first.
 */
static void computed_resolve_current_colors(robo_css_computed_style_t *out,
                                            const robo_css_computed_style_t *parent_computed)
{
    robo_css_computed_entry_t *color_entry;
    robo_css_color_t element_color;
    size_t index;

    color_entry = computed_find_name(out, robo_css_slice_from_text(COMPUTED_COLOR_PROPERTY));
    computed_resolve_entry_color(color_entry, computed_color_of(parent_computed,
                                                               COMPUTED_COLOR_PROPERTY));
    element_color = computed_color_of(out, COMPUTED_COLOR_PROPERTY);
    if (!robo_css_color_is_resolved(element_color)) {
        return;
    }
    for (index = 0u; index < out->count; index++) {
        robo_css_computed_entry_t *entry = &out->entries[index];

        if (entry != color_entry) {
            computed_resolve_entry_color(entry, element_color);
        }
    }
}

/**
 * Compute the entries of @p resolved into @p out, one level of a tree.
 *
 * The shape of the pass is one entry in, one entry out, and the three questions
 * asked of each entry are asked in the order they matter:
 *
 *   1. is there a value at all?      an entry 9.7 carried as "the default is not
 *                                    known to this build" has none, and the
 *                                    computed style says so rather than
 *                                    inventing one;
 *   2. did the parent supply it?     then take the parent's *computed* value,
 *                                    which is what CSS inheritance means and
 *                                    what keeps a child's inherited value
 *                                    identical to the parent's own computed one
 *                                    instead of merely equal-looking;
 *   3. otherwise compute it here,    through the property's declared value shape
 *      from the value in hand.       (RoboCSSPropertyRegistry.h) or, for a
 *                                    property nothing classifies, by preserving
 *                                    the tokens.
 *
 * The specified value travels with the computed one, because the container is
 * the place both halves of "specified vs computed" live (RoboCSSComputed.h) and
 * a caller holding one should be able to see the other without re-resolving.
 */
robo_result_t robo_css_style_context_compute_resolved_style(
    const robo_css_style_context_t *context,
    const robo_css_inherited_style_t *resolved,
    const robo_css_computed_style_t *parent_computed,
    robo_css_computed_style_t *out,
    robo_css_error_t *out_error,
    robo_css_diag_t *diag)
{
    const robo_css_property_registry_t *registry;
    size_t index;

    if (!context || !resolved || !out) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_INVALID_ARGUMENT,
                           NULL, "context, resolved style or output is NULL");
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "context, resolved style or output is NULL");
    }
    robo_css_error_init(out_error);
    registry = robo_css_style_context_property_registry(context);

    for (index = 0u; index < resolved->count; index++) {
        const robo_css_inherited_property_t *entry = &resolved->items[index];
        robo_css_slice_t name = robo_css_inherited_property_name(entry);
        const robo_css_property_definition_t *definition;
        robo_css_property_computation_t computation =
            ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE;
        robo_css_value_t specified = ROBO_CSS_CLEARED;
        robo_css_computed_value_t computed = ROBO_CSS_CLEARED;
        uint32_t flags;
        int custom;
        int have_specified = 0;
        int have_computed = 0;
        robo_result_t rc;

        if (robo_css_slice_is_absent(name)) {
            continue;
        }
        custom = robo_css_is_custom_property_name(name);
        /* A custom property never has a definition: it is an opaque token
         * sequence by definition, and the PRESERVE it gets is not a fallback but
         * the correct computation for it. */
        definition = custom ? NULL : robo_css_property_registry_find_slice(registry, name);
        if (definition) {
            computation = definition->computation;
        }
        flags = computed_flags_of_source(entry->source);
        if (custom) {
            flags |= ROBO_CSS_COMPUTED_FLAG_CUSTOM;
        }

        robo_css_computed_value_init(&computed);
        if (entry->value.kind != ROBO_CSS_VALUE_KIND_NONE) {
            rc = robo_css_value_copy(&specified, &entry->value, diag);
            if (rc != ROBO_OK) {
                goto fail;
            }
            have_specified = 1;
        }

        if (entry->unresolved) {
            /* 9.7's typed "this build does not know the property's default".
             * There is nothing to compute, and the flag says why. */
            flags |= ROBO_CSS_COMPUTED_FLAG_UNRESOLVED;
        } else if (entry->source == ROBO_CSS_INHERITED_SOURCE_DEFERRED) {
            /* The `revert` keyword, carried. Preserved rather than dropped, so
             * the author's declaration survives to whoever can obey it. */
            rc = robo_css_computed_value_make_preserved(&computed, &specified, diag);
            if (rc != ROBO_OK) {
                goto fail;
            }
            computed.unresolved = 1;
            have_computed = computed.kind != ROBO_CSS_COMPUTED_VALUE_NONE;
        } else if (entry->source == ROBO_CSS_INHERITED_SOURCE_PARENT && parent_computed) {
            const robo_css_computed_value_t *inherited =
                robo_css_computed_style_computed_by_name(parent_computed, name);

            if (inherited) {
                rc = robo_css_computed_value_copy(&computed, inherited, diag);
                if (rc != ROBO_OK) {
                    goto fail;
                }
                /* The parent's value may itself have been the property's
                 * default; that fact is true of this value too. */
                flags |= (robo_css_computed_style_flags(parent_computed, name) &
                          ROBO_CSS_COMPUTED_FLAG_DEFAULT);
                have_computed = 1;
            }
        }

        if (!have_computed && !entry->unresolved) {
            if (!have_specified) {
                /* Nothing was specified and nothing was inherited: the entry
                 * exists (9.7 made it so) but has no value, which is a fact the
                 * computed style records rather than guesses past. */
                flags |= ROBO_CSS_COMPUTED_FLAG_UNRESOLVED;
            } else {
                rc = robo_css_computed_value_compute(computation, &specified,
                                                     &computed, diag);
                if (rc != ROBO_OK) {
                    goto fail;
                }
                if (computed.kind == ROBO_CSS_COMPUTED_VALUE_INVALID) {
                    flags |= ROBO_CSS_COMPUTED_FLAG_INVALID;
                }
                if (computed.unresolved) {
                    flags |= ROBO_CSS_COMPUTED_FLAG_UNRESOLVED;
                }
                have_computed = computed.kind != ROBO_CSS_COMPUTED_VALUE_NONE;
            }
        }

        rc = robo_css_computed_style_set_entry(
            out, entry->property, name, flags,
            have_specified ? &specified : NULL,
            have_computed ? &computed : NULL, diag);
        if (rc != ROBO_OK) {
            robo_css_value_dispose(&specified);
            robo_css_computed_value_dispose(&computed);
            goto fail;
        }
        /* set_entry moved whatever it took; anything left is ours to free. */
        robo_css_value_dispose(&specified);
        robo_css_computed_value_dispose(&computed);
        continue;

    fail:
        robo_css_value_dispose(&specified);
        robo_css_computed_value_dispose(&computed);
        robo_css_computed_style_clear(out);
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, rc, NULL,
                           "the computed style could not be built");
        return rc;
    }
    /* Every value is computed, so the one thing a value cannot know about itself
     * can now be settled: `currentColor` is the element's `color`, and on `color`
     * it is the parent's — see the note above. */
    computed_resolve_current_colors(out, parent_computed);
    return ROBO_OK;
}

robo_result_t robo_css_style_context_query_computed_value(
    const robo_css_style_context_t *context,
    const robo_css_computed_style_t *style,
    robo_css_slice_t name,
    robo_css_computed_value_t *out,
    int *out_from_initial,
    robo_css_diag_t *diag)
{
    const robo_css_computed_value_t *present;
    const robo_css_property_definition_t *definition;
    const robo_css_value_t *initial;
    robo_result_t rc;

    if (!context || !out || robo_css_slice_is_absent(name)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "context, name and output are required");
    }
    if (out_from_initial) {
        *out_from_initial = 0;
    }
    present = style ? robo_css_computed_style_computed_by_name(style, name) : NULL;
    if (present) {
        return robo_css_computed_value_copy(out, present, diag);
    }
    /* Nothing declared it here. For a property this build has no initial value
     * for — including every custom property, which CSS gives no initial value
     * at all — the honest answer is that there is none. */
    definition = robo_css_property_registry_find_slice(
        robo_css_style_context_property_registry(context), name);
    if (!definition || robo_css_is_custom_property_name(name)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_FOUND,
                             "no computed value and no initial value for the property");
    }
    initial = robo_css_property_definition_initial_value(definition);
    if (!initial) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_FOUND,
                             "this build has no initial value for the property");
    }
    if (out_from_initial) {
        *out_from_initial = 1;
    }
    rc = robo_css_computed_value_compute(definition->computation, initial, out, diag);
    if (rc != ROBO_OK) {
        return rc;
    }
    /* One initial value in the table is a *reference* rather than a value: every
     * `border-*-color` is `currentcolor`, so a border nothing declared is the
     * element's own colour. Nothing else can reach this: `background-color` is
     * `transparent`, which is a colour and not a reference. Resolving it here, the
     * way the pass above resolves what a style *holds*, is what keeps the two doors
     * answering one question the same way — an element that declared no border
     * colour and one whose shorthand reset it to the initial value have the same
     * computed colour, because they do. */
    if (out->kind == ROBO_CSS_COMPUTED_VALUE_COLOR &&
        robo_css_color_is_current(out->color)) {
        robo_css_color_t resolved;

        if (robo_css_color_resolve_current(out->color,
                                          computed_color_of(style, COMPUTED_COLOR_PROPERTY),
                                          &resolved)) {
            out->color = resolved;
            out->unresolved = 0;
        }
    }
    return ROBO_OK;
}
