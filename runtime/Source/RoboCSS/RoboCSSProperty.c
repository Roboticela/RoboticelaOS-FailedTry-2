/*
 * RoboCSS property identity (Phase 9.1) — see RoboCSSProperty.h.
 *
 * Names are copied in and owned here, compared ASCII case-insensitively, and
 * stored exactly as given. Ids come from a cursor that only ever moves forward,
 * including across robo_css_property_table_reset, so an id issued before a
 * clear can never name an entry registered after it.
 */

#include "RoboCSS/RoboCSSProperty.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

typedef struct robo_css_property_entry {
    robo_css_property_id_t id;
    uint32_t flags;
    char *name; /* owned */
    size_t name_length;
} robo_css_property_entry_t;

struct robo_css_property_table {
    robo_css_property_entry_t *entries; /* owned */
    size_t count;
    size_t capacity;
    robo_css_property_id_t next_id;
};

int robo_css_property_info_is_set(const robo_css_property_info_t *info)
{
    return info && info->id != ROBO_CSS_PROPERTY_ID_NONE && info->name != NULL;
}

int robo_css_property_flags_inherited(uint32_t flags)
{
    return (flags & (uint32_t)ROBO_CSS_PROPERTY_FLAG_INHERITED) != 0u;
}

int robo_css_property_flags_shorthand(uint32_t flags)
{
    return (flags & (uint32_t)ROBO_CSS_PROPERTY_FLAG_SHORTHAND) != 0u;
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p name can be a CSS property name in this model: non-empty, at
 * most ROBO_CSS_PROPERTY_NAME_MAX bytes, and free of whitespace, `:` and `.`
 * (the characters that would make it something other than one name).
 */
static int property_name_is_usable(const char *name)
{
    size_t i;
    size_t length;

    if (!name || name[0] == '\0') {
        return 0;
    }
    length = strlen(name);
    if (length > (size_t)ROBO_CSS_PROPERTY_NAME_MAX) {
        return 0;
    }
    for (i = 0u; i < length; i++) {
        char c = name[i];

        if (robo_css_ascii_is_space((uint8_t)c) || c == ':' || c == '.') {
            return 0;
        }
    }
    return 1;
}

/** ASCII case-insensitive comparison of a stored name against a slice. */
static int name_equals_slice_ci(const char *name, size_t name_length,
                                robo_css_slice_t slice)
{
    size_t i;

    if (name_length != slice.length) {
        return 0;
    }
    if (name_length == 0u) {
        return 1;
    }
    if (!slice.bytes) {
        return 0;
    }
    for (i = 0u; i < name_length; i++) {
        if (robo_css_ascii_lower((uint8_t)name[i]) !=
            robo_css_ascii_lower(slice.bytes[i])) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Table                                                               */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_property_table_create(robo_css_property_table_t **out_table,
                                            robo_css_diag_t *diag)
{
    robo_css_property_table_t *table;

    if (!out_table) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY, ROBO_INVALID_ARGUMENT,
                             "out_table is NULL");
    }
    *out_table = NULL;
    table = (robo_css_property_table_t *)robo_calloc(1u, sizeof(*table));
    if (!table) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY, ROBO_OUT_OF_MEMORY,
                             "could not allocate the property table");
    }
    table->entries = NULL;
    table->count = 0u;
    table->capacity = 0u;
    /* 1, not 0: ROBO_CSS_PROPERTY_ID_NONE has to keep meaning "no property". */
    table->next_id = 1u;
    *out_table = table;
    return ROBO_OK;
}

void robo_css_property_table_reset(robo_css_property_table_t *table)
{
    size_t i;

    if (!table) {
        return;
    }
    for (i = 0u; i < table->count; i++) {
        if (table->entries[i].name) {
            robo_free(table->entries[i].name);
        }
    }
    table->count = 0u;
    /* next_id deliberately survives: an id must never be reissued by a table. */
}

void robo_css_property_table_destroy(robo_css_property_table_t *table)
{
    if (!table) {
        return;
    }
    robo_css_property_table_reset(table);
    if (table->entries) {
        robo_free(table->entries);
    }
    robo_free(table);
}

static robo_css_property_entry_t *property_find_by_id(robo_css_property_table_t *table,
                                                     robo_css_property_id_t id)
{
    size_t i;

    if (!table || id == ROBO_CSS_PROPERTY_ID_NONE) {
        return NULL;
    }
    for (i = 0u; i < table->count; i++) {
        if (table->entries[i].id == id) {
            return &table->entries[i];
        }
    }
    return NULL;
}

static void property_info_fill(robo_css_property_info_t *info,
                               const robo_css_property_entry_t *entry)
{
    info->id = entry->id;
    info->name = entry->name;
    info->name_length = entry->name_length;
    info->flags = entry->flags;
}

robo_result_t robo_css_property_table_register(robo_css_property_table_t *table,
                                               const char *name,
                                               uint32_t flags,
                                               robo_css_property_id_t *out_id,
                                               robo_css_diag_t *diag)
{
    size_t i;
    size_t length;
    char *copy;
    robo_css_property_entry_t *entry;

    if (out_id) {
        *out_id = ROBO_CSS_PROPERTY_ID_NONE;
    }
    if (!table || !out_id) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY, ROBO_INVALID_ARGUMENT,
                             "table or output is NULL");
    }
    if (!property_name_is_usable(name)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY, ROBO_INVALID_ARGUMENT,
                             "not a usable property name");
    }
    length = strlen(name);
    for (i = 0u; i < table->count; i++) {
        if (name_equals_slice_ci(table->entries[i].name, table->entries[i].name_length,
                                 robo_css_slice_from_text(name))) {
            /* Already registered: report it and hand back the existing id. The
             * stored flags are not overwritten — first registration wins, so a
             * property's nature cannot change underneath a caller that read it. */
            *out_id = table->entries[i].id;
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY, ROBO_ALREADY_EXISTS,
                                 "property is already registered");
        }
    }
    if (table->count >= (size_t)ROBO_CSS_PROPERTY_TABLE_MAX) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY, ROBO_OUT_OF_MEMORY,
                             "property table is full");
    }
    copy = (char *)robo_malloc(length + 1u);
    if (!copy) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY, ROBO_OUT_OF_MEMORY,
                             "could not copy the property name");
    }
    if (!robo_css_grow((void **)&table->entries, &table->capacity, table->count,
                       sizeof(robo_css_property_entry_t))) {
        robo_free(copy);
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY, ROBO_OUT_OF_MEMORY,
                             "could not grow the property table");
    }
    memcpy(copy, name, length + 1u);
    entry = &table->entries[table->count];
    entry->id = table->next_id++;
    entry->flags = flags;
    entry->name = copy;
    entry->name_length = length;
    table->count++;
    *out_id = entry->id;
    return ROBO_OK;
}

robo_css_property_id_t robo_css_property_table_lookup(
    const robo_css_property_table_t *table,
    const char *name)
{
    if (!table || !name) {
        return ROBO_CSS_PROPERTY_ID_NONE;
    }
    return robo_css_property_table_lookup_slice(table, robo_css_slice_from_text(name));
}

robo_css_property_id_t robo_css_property_table_lookup_slice(
    const robo_css_property_table_t *table,
    robo_css_slice_t name)
{
    size_t i;

    if (!table || robo_css_slice_is_empty(name)) {
        return ROBO_CSS_PROPERTY_ID_NONE;
    }
    for (i = 0u; i < table->count; i++) {
        if (name_equals_slice_ci(table->entries[i].name, table->entries[i].name_length,
                                 name)) {
            return table->entries[i].id;
        }
    }
    return ROBO_CSS_PROPERTY_ID_NONE;
}

int robo_css_property_table_is_registered(const robo_css_property_table_t *table,
                                          robo_css_property_id_t id)
{
    return property_find_by_id((robo_css_property_table_t *)table, id) != NULL;
}

robo_result_t robo_css_property_table_info(const robo_css_property_table_t *table,
                                           robo_css_property_id_t id,
                                           robo_css_property_info_t *out_info,
                                           robo_css_diag_t *diag)
{
    robo_css_property_entry_t *entry;

    if (!table || !out_info) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY, ROBO_INVALID_ARGUMENT,
                             "table or output is NULL");
    }
    entry = property_find_by_id((robo_css_property_table_t *)table, id);
    if (!entry) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY, ROBO_NOT_FOUND,
                             "property id is not in this table");
    }
    property_info_fill(out_info, entry);
    return ROBO_OK;
}

robo_result_t robo_css_property_table_at(const robo_css_property_table_t *table,
                                         size_t index,
                                         robo_css_property_info_t *out_info,
                                         robo_css_diag_t *diag)
{
    if (!table || !out_info) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY, ROBO_INVALID_ARGUMENT,
                             "table or output is NULL");
    }
    if (index >= table->count) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PROPERTY, ROBO_NOT_FOUND,
                             "index is past the end of the table");
    }
    property_info_fill(out_info, &table->entries[index]);
    return ROBO_OK;
}

size_t robo_css_property_table_count(const robo_css_property_table_t *table)
{
    return table ? table->count : 0u;
}
