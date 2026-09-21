/*
 * RoboCSS property identity (Phase 9.1) — see RoboCSSProperty.h.
 *
 * Names are copied in and owned here, compared ASCII case-insensitively, and
 * stored exactly as given. Ids come from a cursor that only ever moves forward,
 * including across CssPropertyTableReset, so an id issued before a
 * clear can never name an entry registered after it.
 */

#include "RoboCSS/RoboCSSProperty.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

typedef struct CssPropertyEntry {
    CssPropertyId id;
    uint32_t flags;
    char *name; /* owned */
    size_t nameLength;
} CssPropertyEntry;

struct CssPropertyTable {
    CssPropertyEntry *entries; /* owned */
    size_t count;
    size_t capacity;
    CssPropertyId nextId;
};

int CssPropertyInfoIsSet(const CssPropertyInfo *info)
{
    return info && info->id != RoboCssPropertyIdNone && info->name != NULL;
}

int CssPropertyFlagsInherited(uint32_t flags)
{
    return (flags & (uint32_t)RoboCssPropertyFlagInherited) != 0u;
}

int CssPropertyFlagsShorthand(uint32_t flags)
{
    return (flags & (uint32_t)RoboCssPropertyFlagShorthand) != 0u;
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p name can be a CSS property name in this model: non-empty, at
 * most RoboCssPropertyNameMax bytes, and free of whitespace, `:` and `.`
 * (the characters that would make it something other than one name).
 */
static int PropertyNameIsUsable(const char *name)
{
    size_t i;
    size_t length;

    if (!name || name[0] == '\0') {
        return 0;
    }
    length = strlen(name);
    if (length > (size_t)RoboCssPropertyNameMax) {
        return 0;
    }
    for (i = 0u; i < length; i++) {
        char c = name[i];

        if (CssAsciiIsSpace((uint8_t)c) || c == ':' || c == '.') {
            return 0;
        }
    }
    return 1;
}

/** ASCII case-insensitive comparison of a stored name against a slice. */
static int NameEqualsSliceCi(const char *name, size_t nameLength,
                                CssSlice slice)
{
    size_t i;

    if (nameLength != slice.length) {
        return 0;
    }
    if (nameLength == 0u) {
        return 1;
    }
    if (!slice.bytes) {
        return 0;
    }
    for (i = 0u; i < nameLength; i++) {
        if (CssAsciiLower((uint8_t)name[i]) !=
            CssAsciiLower(slice.bytes[i])) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Table                                                               */
/* ------------------------------------------------------------------ */

Result CssPropertyTableCreate(CssPropertyTable **outTable,
                                            CssDiag *diag)
{
    CssPropertyTable *table;

    if (!outTable) {
        return RoboCssDiag(diag, RoboCssDomainProperty, RoboInvalidArgument,
                             "out_table is NULL");
    }
    *outTable = NULL;
    table = (CssPropertyTable *)RoboCalloc(1u, sizeof(*table));
    if (!table) {
        return RoboCssDiag(diag, RoboCssDomainProperty, RoboOutOfMemory,
                             "could not allocate the property table");
    }
    table->entries = NULL;
    table->count = 0u;
    table->capacity = 0u;
    /* 1, not 0: RoboCssPropertyIdNone has to keep meaning "no property". */
    table->nextId = 1u;
    *outTable = table;
    return RoboOk;
}

void CssPropertyTableReset(CssPropertyTable *table)
{
    size_t i;

    if (!table) {
        return;
    }
    for (i = 0u; i < table->count; i++) {
        if (table->entries[i].name) {
            RoboFree(table->entries[i].name);
        }
    }
    table->count = 0u;
    /* nextId deliberately survives: an id must never be reissued by a table. */
}

void CssPropertyTableDestroy(CssPropertyTable *table)
{
    if (!table) {
        return;
    }
    CssPropertyTableReset(table);
    if (table->entries) {
        RoboFree(table->entries);
    }
    RoboFree(table);
}

static CssPropertyEntry *PropertyFindById(CssPropertyTable *table,
                                                     CssPropertyId id)
{
    size_t i;

    if (!table || id == RoboCssPropertyIdNone) {
        return NULL;
    }
    for (i = 0u; i < table->count; i++) {
        if (table->entries[i].id == id) {
            return &table->entries[i];
        }
    }
    return NULL;
}

static void PropertyInfoFill(CssPropertyInfo *info,
                               const CssPropertyEntry *entry)
{
    info->id = entry->id;
    info->name = entry->name;
    info->nameLength = entry->nameLength;
    info->flags = entry->flags;
}

Result CssPropertyTableRegister(CssPropertyTable *table,
                                               const char *name,
                                               uint32_t flags,
                                               CssPropertyId *outId,
                                               CssDiag *diag)
{
    size_t i;
    size_t length;
    char *copy;
    CssPropertyEntry *entry;

    if (outId) {
        *outId = RoboCssPropertyIdNone;
    }
    if (!table || !outId) {
        return RoboCssDiag(diag, RoboCssDomainProperty, RoboInvalidArgument,
                             "table or output is NULL");
    }
    if (!PropertyNameIsUsable(name)) {
        return RoboCssDiag(diag, RoboCssDomainProperty, RoboInvalidArgument,
                             "not a usable property name");
    }
    length = strlen(name);
    for (i = 0u; i < table->count; i++) {
        if (NameEqualsSliceCi(table->entries[i].name, table->entries[i].nameLength,
                                 CssSliceFromText(name))) {
            /* Already registered: report it and hand back the existing id. The
             * stored flags are not overwritten — first registration wins, so a
             * property's nature cannot change underneath a caller that read it. */
            *outId = table->entries[i].id;
            return RoboCssDiag(diag, RoboCssDomainProperty, RoboAlreadyExists,
                                 "property is already registered");
        }
    }
    if (table->count >= (size_t)RoboCssPropertyTableMax) {
        return RoboCssDiag(diag, RoboCssDomainProperty, RoboOutOfMemory,
                             "property table is full");
    }
    copy = (char *)Malloc(length + 1u);
    if (!copy) {
        return RoboCssDiag(diag, RoboCssDomainProperty, RoboOutOfMemory,
                             "could not copy the property name");
    }
    if (!CssGrow((void **)&table->entries, &table->capacity, table->count,
                       sizeof(CssPropertyEntry))) {
        RoboFree(copy);
        return RoboCssDiag(diag, RoboCssDomainProperty, RoboOutOfMemory,
                             "could not grow the property table");
    }
    memcpy(copy, name, length + 1u);
    entry = &table->entries[table->count];
    entry->id = table->nextId++;
    entry->flags = flags;
    entry->name = copy;
    entry->nameLength = length;
    table->count++;
    *outId = entry->id;
    return RoboOk;
}

CssPropertyId CssPropertyTableLookup(
    const CssPropertyTable *table,
    const char *name)
{
    if (!table || !name) {
        return RoboCssPropertyIdNone;
    }
    return CssPropertyTableLookupSlice(table, CssSliceFromText(name));
}

CssPropertyId CssPropertyTableLookupSlice(
    const CssPropertyTable *table,
    CssSlice name)
{
    size_t i;

    if (!table || CssSliceIsEmpty(name)) {
        return RoboCssPropertyIdNone;
    }
    for (i = 0u; i < table->count; i++) {
        if (NameEqualsSliceCi(table->entries[i].name, table->entries[i].nameLength,
                                 name)) {
            return table->entries[i].id;
        }
    }
    return RoboCssPropertyIdNone;
}

int CssPropertyTableIsRegistered(const CssPropertyTable *table,
                                          CssPropertyId id)
{
    return PropertyFindById((CssPropertyTable *)table, id) != NULL;
}

Result CssPropertyTableInfo(const CssPropertyTable *table,
                                           CssPropertyId id,
                                           CssPropertyInfo *outInfo,
                                           CssDiag *diag)
{
    CssPropertyEntry *entry;

    if (!table || !outInfo) {
        return RoboCssDiag(diag, RoboCssDomainProperty, RoboInvalidArgument,
                             "table or output is NULL");
    }
    entry = PropertyFindById((CssPropertyTable *)table, id);
    if (!entry) {
        return RoboCssDiag(diag, RoboCssDomainProperty, RoboNotFound,
                             "property id is not in this table");
    }
    PropertyInfoFill(outInfo, entry);
    return RoboOk;
}

Result CssPropertyTableAt(const CssPropertyTable *table,
                                         size_t index,
                                         CssPropertyInfo *outInfo,
                                         CssDiag *diag)
{
    if (!table || !outInfo) {
        return RoboCssDiag(diag, RoboCssDomainProperty, RoboInvalidArgument,
                             "table or output is NULL");
    }
    if (index >= table->count) {
        return RoboCssDiag(diag, RoboCssDomainProperty, RoboNotFound,
                             "index is past the end of the table");
    }
    PropertyInfoFill(outInfo, &table->entries[index]);
    return RoboOk;
}

size_t CssPropertyTableCount(const CssPropertyTable *table)
{
    return table ? table->count : 0u;
}
