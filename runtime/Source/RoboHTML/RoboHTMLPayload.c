/*
 * RoboHTML node payload encoding/decoding (Phase 8.3).
 *
 * One definition of the byte layout, shared by the writers (tree builder)
 * and the readers (node/element accessors). Layout:
 *
 *   header                  robo_html_payload_header_t
 *   header.length           payload bytes following the header
 *   header.attr_count       attribute entries following the name
 *   header.flags            ROBO_HTML_PAYLOAD_FLAG_*
 *
 *   ELEMENT        name '\0'
 *                  then attr_count × ([u32 name_len][u32 value_len] name '\0' value '\0')
 *   TEXT/COMMENT   bytes '\0'
 *   DOCUMENT_TYPE  name '\0' public '\0' system '\0'
 *
 * Every string is NUL-terminated so readers can hand out borrowed C
 * strings, while the explicit u32 lengths let the walk skip a string
 * without scanning it — an attribute value containing a NUL byte from a
 * malformed source still round-trips without desynchronising the reader.
 *
 * Payloads are written once and never edited, except text: appending to a
 * text payload that currently ends the arena grows it in place, which is
 * how adjacent character tokens collapse into a single text node instead
 * of one node per token.
 */

#include "RoboHTMLInternal.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

#define PAYLOAD_HEADER_SIZE ((size_t)sizeof(robo_html_payload_header_t))
#define PAYLOAD_ATTR_FIXED 8u /* two u32 lengths */

static size_t string_size(size_t length)
{
    return length + 1u; /* bytes plus terminator */
}

/** Attribute table size for @p attr_count attributes. */
static size_t attr_table_size(const robo_html_attr_view_t *attrs,
                              size_t attr_count)
{
    size_t total = 0u;
    size_t i;

    for (i = 0u; i < attr_count; i++) {
        const char *name = attrs[i].name ? attrs[i].name : "";
        const char *value = attrs[i].value ? attrs[i].value : "";
        total += (size_t)PAYLOAD_ATTR_FIXED + string_size(strlen(name)) +
                 string_size(strlen(value));
    }
    return total;
}

size_t robo_html_payload_element_size(const char *name,
                                      size_t name_length,
                                      const robo_html_attr_view_t *attrs,
                                      size_t attr_count)
{
    const char *safe_name = name ? name : "";
    size_t effective = name_length;

    if (effective == 0u) {
        effective = strlen(safe_name);
    }
    return string_size(effective) +
           attr_table_size(attrs, attr_count ? attr_count : 0u);
}

/** Little-endian u32 store (payload bytes never depend on host endianness). */
static void store_u32(char *dst, uint32_t v)
{
    dst[0] = (char)(v & 0xffu);
    dst[1] = (char)((v >> 8) & 0xffu);
    dst[2] = (char)((v >> 16) & 0xffu);
    dst[3] = (char)((v >> 24) & 0xffu);
}

static uint32_t load_u32(const char *src)
{
    return (uint32_t)(unsigned char)src[0] |
           ((uint32_t)(unsigned char)src[1] << 8) |
           ((uint32_t)(unsigned char)src[2] << 16) |
           ((uint32_t)(unsigned char)src[3] << 24);
}

/** Borrowed payload pointer, or NULL when the range does not describe one. */
static const robo_html_payload_header_t *payload_get(
    const robo_html_document_slot_t *slot,
    const robo_html_node_entry_t *entry)
{
    const robo_html_payload_header_t *hdr;

    if (!slot || !entry || entry->payload_length < PAYLOAD_HEADER_SIZE) {
        return NULL;
    }
    hdr = (const robo_html_payload_header_t *)robo_html_document_arena_at(
        slot, entry->payload_offset, entry->payload_length);
    if (!hdr) {
        return NULL;
    }
    if (PAYLOAD_HEADER_SIZE + (size_t)hdr->length != entry->payload_length) {
        return NULL; /* range does not describe a well-formed payload */
    }
    return hdr;
}

static char *payload_mut(robo_html_document_slot_t *slot,
                         robo_html_node_entry_t *entry)
{
    if (!slot || !entry || entry->payload_length < PAYLOAD_HEADER_SIZE) {
        return NULL;
    }
    if ((size_t)entry->payload_offset + entry->payload_length >
        slot->payload_used) {
        return NULL;
    }
    return slot->payload + entry->payload_offset;
}

robo_result_t robo_html_payload_write_element(
    robo_html_document_slot_t *slot,
    robo_html_node_entry_t *entry,
    const char *name,
    size_t name_length,
    const robo_html_attr_view_t *attrs,
    size_t attr_count,
    int self_closing,
    robo_html_diag_t *diag)
{
    const char *safe_name = name ? name : "";
    size_t effective = name_length;
    size_t body;
    size_t total;
    uint32_t offset = 0u;
    void *raw = NULL;
    char *cursor;
    robo_html_payload_header_t *hdr;
    size_t i;

    if (!slot || !entry) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "slot or entry is NULL");
    }
    if (effective == 0u) {
        effective = strlen(safe_name);
    }
    body = robo_html_payload_element_size(safe_name, effective, attrs,
                                          attr_count);
    total = PAYLOAD_HEADER_SIZE + body;
    if (robo_html_document_arena_alloc(slot, total, &offset, &raw, diag) !=
            ROBO_OK ||
        !raw) {
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }
    memset(raw, 0, total);
    cursor = (char *)raw + PAYLOAD_HEADER_SIZE;

    memcpy(cursor, safe_name, effective);
    cursor += string_size(effective);

    for (i = 0u; i < attr_count; i++) {
        const char *aname = attrs[i].name ? attrs[i].name : "";
        const char *avalue = attrs[i].value ? attrs[i].value : "";
        size_t alen = strlen(aname);
        size_t vlen = strlen(avalue);

        store_u32(cursor, (uint32_t)alen);
        store_u32(cursor + 4, (uint32_t)vlen);
        cursor += PAYLOAD_ATTR_FIXED;
        memcpy(cursor, aname, alen);
        cursor += string_size(alen);
        memcpy(cursor, avalue, vlen);
        cursor += string_size(vlen);
    }

    hdr = (robo_html_payload_header_t *)raw;
    hdr->length = (uint32_t)body;
    hdr->attr_count = (uint32_t)attr_count;
    hdr->flags = self_closing ? ROBO_HTML_PAYLOAD_FLAG_SELF_CLOSING : 0u;
    hdr->reserved = 0u;
    robo_html_node_set_payload(entry, offset, (uint32_t)total);
    return ROBO_OK;
}

robo_result_t robo_html_payload_write_text(robo_html_document_slot_t *slot,
                                           robo_html_node_entry_t *entry,
                                           const char *bytes,
                                           size_t length,
                                           robo_html_diag_t *diag)
{
    size_t body = string_size(length);
    size_t total = PAYLOAD_HEADER_SIZE + body;
    uint32_t offset = 0u;
    void *raw = NULL;
    robo_html_payload_header_t *hdr;

    if (!slot || !entry || (!bytes && length > 0u)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "bad text payload args");
    }
    if (total > 0xFFFFFFFFu) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_SUPPORTED,
                              "text run exceeds one payload");
    }
    if (robo_html_document_arena_alloc(slot, total, &offset, &raw, diag) !=
            ROBO_OK ||
        !raw) {
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }
    memset(raw, 0, total);
    if (length > 0u) {
        memcpy((char *)raw + PAYLOAD_HEADER_SIZE, bytes, length);
    }
    hdr = (robo_html_payload_header_t *)raw;
    hdr->length = (uint32_t)body;
    hdr->attr_count = 0u;
    hdr->flags = 0u;
    hdr->reserved = 0u;
    robo_html_node_set_payload(entry, offset, (uint32_t)total);
    return ROBO_OK;
}

robo_result_t robo_html_payload_append_text(robo_html_document_slot_t *slot,
                                            robo_html_node_entry_t *entry,
                                            const char *bytes,
                                            size_t length,
                                            robo_html_diag_t *diag)
{
    char *base;
    robo_html_payload_header_t *hdr;

    if (!slot || !entry || (!bytes && length > 0u)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "bad text append args");
    }
    if (length == 0u) {
        return ROBO_OK;
    }
    base = payload_mut(slot, entry);
    if (!base) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "text payload does not resolve");
    }
    /* Only a payload that currently ends the arena can grow in place. */
    if ((size_t)entry->payload_offset + entry->payload_length !=
        slot->payload_used) {
        return ROBO_NOT_SUPPORTED;
    }
    if (payload_get(slot, entry) == NULL) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "text payload header is inconsistent");
    }
    if ((size_t)entry->payload_length + length > 0xFFFFFFFFu) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_SUPPORTED,
                              "text run exceeds one payload");
    }
    {
        void *grown = NULL;
        if (robo_html_document_arena_extend_tail(slot, entry->payload_offset,
                                                 entry->payload_length, length,
                                                 &grown, diag) != ROBO_OK) {
            return diag ? diag->code : ROBO_OUT_OF_MEMORY;
        }
        base = (char *)grown;
    }
    hdr = (robo_html_payload_header_t *)base;
    /* The terminator currently sits at header+length; write over it. */
    memcpy(base + PAYLOAD_HEADER_SIZE + (size_t)hdr->length - 1u, bytes,
           length);
    hdr->length += (uint32_t)length;
    base[PAYLOAD_HEADER_SIZE + (size_t)hdr->length - 1u] = '\0';
    entry->payload_length += (uint32_t)length;
    return ROBO_OK;
}

robo_result_t robo_html_payload_write_doctype(robo_html_document_slot_t *slot,
                                              robo_html_node_entry_t *entry,
                                              const char *name,
                                              const char *public_id,
                                              const char *system_id,
                                              robo_html_diag_t *diag)
{
    const char *n = name ? name : "";
    const char *p = public_id ? public_id : "";
    const char *s = system_id ? system_id : "";
    size_t body = string_size(strlen(n)) + string_size(strlen(p)) +
                  string_size(strlen(s));
    size_t total = PAYLOAD_HEADER_SIZE + body;
    uint32_t offset = 0u;
    void *raw = NULL;
    char *cursor;
    robo_html_payload_header_t *hdr;

    if (!slot || !entry) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "slot or entry is NULL");
    }
    if (robo_html_document_arena_alloc(slot, total, &offset, &raw, diag) !=
            ROBO_OK ||
        !raw) {
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }
    memset(raw, 0, total);
    cursor = (char *)raw + PAYLOAD_HEADER_SIZE;
    memcpy(cursor, n, strlen(n));
    cursor += string_size(strlen(n));
    memcpy(cursor, p, strlen(p));
    cursor += string_size(strlen(p));
    memcpy(cursor, s, strlen(s));
    hdr = (robo_html_payload_header_t *)raw;
    hdr->length = (uint32_t)body;
    hdr->attr_count = 0u;
    hdr->flags = 0u;
    hdr->reserved = 0u;
    robo_html_node_set_payload(entry, offset, (uint32_t)total);
    return ROBO_OK;
}

/**
 * Walk to the attribute table (past the element name).
 * @return cursor at the first [u32 name_len], or NULL when the payload is
 *         not an element payload.
 */
static const char *element_attr_cursor(
    const robo_html_document_slot_t *slot,
    const robo_html_node_entry_t *entry,
    const char **out_name,
    size_t *out_name_length)
{
    const robo_html_payload_header_t *hdr = payload_get(slot, entry);
    const char *bytes;
    const char *name;

    if (out_name) {
        *out_name = "";
    }
    if (out_name_length) {
        *out_name_length = 0u;
    }
    if (!hdr || entry->kind != ROBO_HTML_NODE_KIND_ELEMENT) {
        return NULL;
    }
    bytes = ((const char *)hdr) + PAYLOAD_HEADER_SIZE;
    name = bytes;
    if (out_name) {
        *out_name = name;
    }
    if (out_name_length) {
        *out_name_length = strlen(name);
    }
    return name + strlen(name) + 1u;
}

const char *robo_html_payload_element_name(
    const robo_html_document_slot_t *slot,
    const robo_html_node_entry_t *entry,
    size_t *out_length)
{
    const char *name = "";
    size_t length = 0u;

    (void)element_attr_cursor(slot, entry, &name, &length);
    if (out_length) {
        *out_length = length;
    }
    return name ? name : "";
}

size_t robo_html_payload_attr_count(const robo_html_document_slot_t *slot,
                                    const robo_html_node_entry_t *entry)
{
    const robo_html_payload_header_t *hdr = payload_get(slot, entry);

    if (!hdr || entry->kind != ROBO_HTML_NODE_KIND_ELEMENT) {
        return 0u;
    }
    return (size_t)hdr->attr_count;
}

robo_result_t robo_html_payload_attr_at(const robo_html_document_slot_t *slot,
                                        const robo_html_node_entry_t *entry,
                                        size_t index,
                                        const char **out_name,
                                        const char **out_value)
{
    const robo_html_payload_header_t *hdr = payload_get(slot, entry);
    const char *cursor;
    const char *limit;
    size_t i;

    if (!hdr || !out_name || !out_value ||
        entry->kind != ROBO_HTML_NODE_KIND_ELEMENT) {
        return ROBO_NOT_FOUND;
    }
    cursor = element_attr_cursor(slot, entry, NULL, NULL);
    if (!cursor) {
        return ROBO_NOT_FOUND;
    }
    limit = ((const char *)hdr) + PAYLOAD_HEADER_SIZE + hdr->length;
    for (i = 0u; i < index; i++) {
        uint32_t name_len;
        uint32_t value_len;

        if (cursor + PAYLOAD_ATTR_FIXED > limit) {
            return ROBO_NOT_FOUND;
        }
        name_len = load_u32(cursor);
        value_len = load_u32(cursor + 4);
        cursor += PAYLOAD_ATTR_FIXED + string_size(name_len) +
                  string_size(value_len);
    }
    if (cursor + PAYLOAD_ATTR_FIXED > limit) {
        return ROBO_NOT_FOUND;
    }
    {
        uint32_t name_len = load_u32(cursor);
        uint32_t value_len = load_u32(cursor + 4);
        const char *name = cursor + PAYLOAD_ATTR_FIXED;
        const char *value = name + string_size(name_len);

        if (value + value_len > limit) {
            return ROBO_NOT_FOUND;
        }
        *out_name = name;
        *out_value = value;
    }
    return ROBO_OK;
}

const char *robo_html_payload_text(const robo_html_document_slot_t *slot,
                                   const robo_html_node_entry_t *entry,
                                   size_t *out_length)
{
    const robo_html_payload_header_t *hdr = payload_get(slot, entry);

    if (out_length) {
        *out_length = 0u;
    }
    if (!hdr || (entry->kind != ROBO_HTML_NODE_KIND_TEXT &&
                 entry->kind != ROBO_HTML_NODE_KIND_COMMENT)) {
        return "";
    }
    if (out_length) {
        *out_length = hdr->length > 0u ? (size_t)hdr->length - 1u : 0u;
    }
    return ((const char *)hdr) + PAYLOAD_HEADER_SIZE;
}

int robo_html_payload_self_closing(const robo_html_document_slot_t *slot,
                                   const robo_html_node_entry_t *entry)
{
    const robo_html_payload_header_t *hdr = payload_get(slot, entry);

    if (!hdr || entry->kind != ROBO_HTML_NODE_KIND_ELEMENT) {
        return 0;
    }
    return (hdr->flags & ROBO_HTML_PAYLOAD_FLAG_SELF_CLOSING) ? 1 : 0;
}

void robo_html_payload_doctype(const robo_html_document_slot_t *slot,
                               const robo_html_node_entry_t *entry,
                               const char **out_name,
                               const char **out_public_id,
                               const char **out_system_id)
{
    const robo_html_payload_header_t *hdr = payload_get(slot, entry);
    const char *cursor;

    if (out_name) {
        *out_name = "";
    }
    if (out_public_id) {
        *out_public_id = "";
    }
    if (out_system_id) {
        *out_system_id = "";
    }
    if (!hdr || entry->kind != ROBO_HTML_NODE_KIND_DOCUMENT_TYPE) {
        return;
    }
    cursor = ((const char *)hdr) + PAYLOAD_HEADER_SIZE;
    if (out_name) {
        *out_name = cursor;
    }
    cursor += strlen(cursor) + 1u;
    if (out_public_id) {
        *out_public_id = cursor;
    }
    cursor += strlen(cursor) + 1u;
    if (out_system_id) {
        *out_system_id = cursor;
    }
}
