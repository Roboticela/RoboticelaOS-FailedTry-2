/*
 * RoboHTML node payload encoding/decoding (Phase 8.3; attribute model 8.6).
 *
 * One definition of the byte layout, shared by the writers (tree builder),
 * the readers (node/element accessors) and the attribute mutator. Layout:
 *
 *   header                  robo_html_payload_header_t
 *   header.length           payload bytes following the header
 *   header.attr_count       attribute entries following the name
 *   header.flags            ROBO_HTML_PAYLOAD_FLAG_*
 *   header.ns               element namespace (robo_html_namespace_t)
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
 * The attribute table is the *stored* form of robo_html_attribute_t: same
 * fields, encoded. `name_len`/`value_len` are the authority, so the decoder
 * never has to scan; the writers accept either a measured length or the
 * convention that 0 means "measure with strlen", which is what lets a
 * caller hand over a plain C string without counting first.
 *
 * Payloads are written once and never edited, except two cases: appending to
 * a text payload that currently ends the arena grows it in place, and an
 * attribute mutation rewrites the element payload into fresh arena space
 * (see robo_html_payload_mutate_attributes).
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

/**
 * Effective byte length of a borrowed string: the declared length when the
 * caller measured it, otherwise strlen. Values and names are NUL-terminated
 * by contract, so an unmeasured 0 can only mean "count it for me" or the
 * genuinely empty string — both answers are the same number.
 */
static size_t measured(const char *s, size_t declared)
{
    if (!s) {
        return 0u;
    }
    return declared > 0u ? declared : strlen(s);
}

/** Encoded size of one attribute entry. */
static size_t attr_entry_size(size_t name_length, size_t value_length)
{
    return (size_t)PAYLOAD_ATTR_FIXED + string_size(name_length) +
           string_size(value_length);
}

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

int robo_html_name_equal(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    return robo_html_name_equal_span(a, strlen(a), b);
}

int robo_html_name_equal_span(const char *a, size_t a_length, const char *b)
{
    size_t i;

    if (!a || !b) {
        return 0;
    }
    for (i = 0u; i < a_length; i++) {
        if (b[i] == '\0') {
            return 0; /* b is a prefix of a */
        }
        if (ascii_lower(a[i]) != ascii_lower(b[i])) {
            return 0;
        }
    }
    return b[a_length] == '\0';
}

/** Attribute table size for @p attr_count attributes. */
static size_t attr_table_size(const robo_html_attribute_t *attrs,
                              size_t attr_count)
{
    size_t total = 0u;
    size_t i;

    for (i = 0u; i < attr_count; i++) {
        total += attr_entry_size(measured(attrs[i].name, attrs[i].name_length),
                                 measured(attrs[i].value,
                                          attrs[i].value_length));
    }
    return total;
}

/** Bytes an element payload needs for @p name with no attributes. */
static size_t element_name_size(const char *name, size_t name_length)
{
    return string_size(measured(name, name_length));
}

size_t robo_html_payload_element_size(const char *name,
                                      size_t name_length,
                                      const robo_html_attribute_t *attrs,
                                      size_t attr_count)
{
    return element_name_size(name, name_length) +
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

/**
 * Encode the body (name + attribute table) at @p cursor, returning the
 * cursor past it. Shared by the element writer and the attribute mutator so
 * the two can never disagree about the encoding.
 */
static char *write_element_body(char *cursor,
                                const char *name,
                                size_t name_length,
                                const robo_html_attribute_t *attrs,
                                size_t attr_count)
{
    size_t i;

    memcpy(cursor, name, name_length);
    cursor += string_size(name_length);
    for (i = 0u; i < attr_count; i++) {
        size_t alen = measured(attrs[i].name, attrs[i].name_length);
        size_t vlen = measured(attrs[i].value, attrs[i].value_length);

        store_u32(cursor, (uint32_t)alen);
        store_u32(cursor + 4, (uint32_t)vlen);
        cursor += PAYLOAD_ATTR_FIXED;
        if (alen > 0u) {
            memcpy(cursor, attrs[i].name, alen);
        }
        cursor += string_size(alen);
        if (vlen > 0u) {
            memcpy(cursor, attrs[i].value, vlen);
        }
        cursor += string_size(vlen);
    }
    return cursor;
}

robo_result_t robo_html_payload_write_element(
    robo_html_document_slot_t *slot,
    robo_html_node_entry_t *entry,
    const char *name,
    size_t name_length,
    const robo_html_attribute_t *attrs,
    size_t attr_count,
    int self_closing,
    robo_html_namespace_t ns,
    robo_html_diag_t *diag)
{
    const char *safe_name = name ? name : "";
    size_t effective;
    size_t body;
    size_t total;
    uint32_t offset = 0u;
    void *raw = NULL;
    char *cursor;
    robo_html_payload_header_t *hdr;

    if (!slot || !entry) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "slot or entry is NULL");
    }
    effective = measured(safe_name, name_length);
    body = robo_html_payload_element_size(safe_name, effective, attrs,
                                          attr_count);
    total = PAYLOAD_HEADER_SIZE + body;
    if (robo_html_document_arena_alloc(slot, total, &offset, &raw, diag) !=
            ROBO_OK ||
        !raw) {
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }
    memset(raw, 0, total);
    cursor = write_element_body((char *)raw + PAYLOAD_HEADER_SIZE, safe_name,
                                effective, attrs, attr_count);
    (void)cursor;

    hdr = (robo_html_payload_header_t *)raw;
    hdr->length = (uint32_t)body;
    hdr->attr_count = (uint32_t)attr_count;
    hdr->flags = self_closing ? ROBO_HTML_PAYLOAD_FLAG_SELF_CLOSING : 0u;
    hdr->ns = (uint32_t)ns;
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
    hdr->ns = (uint32_t)ROBO_HTML_NAMESPACE_NONE;
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
    hdr->ns = (uint32_t)ROBO_HTML_NAMESPACE_NONE;
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

/**
 * Resolve attribute @p index to borrowed bytes.
 *
 * @p out_entry receives the entry's first byte (its pair of u32 lengths) so
 * a caller that wants to copy the entry verbatim can do exactly that.
 * Lengths are the authority, so every step is arithmetic on the header — no
 * scan for a terminator, and a value containing a NUL byte cannot
 * desynchronise the walk.
 *
 * @return non-zero when the entry exists and lies inside @p limit.
 */
static int attr_entry_at(const char *table,
                         const char *limit,
                         size_t index,
                         const char **out_entry,
                         const char **out_name,
                         size_t *out_name_length,
                         const char **out_value,
                         size_t *out_value_length)
{
    const char *cursor = table;
    size_t i;

    for (i = 0u; i <= index; i++) {
        uint32_t name_len;
        uint32_t value_len;
        const char *name;
        const char *value;

        if (cursor + PAYLOAD_ATTR_FIXED > limit) {
            return 0;
        }
        name_len = load_u32(cursor);
        value_len = load_u32(cursor + 4);
        name = cursor + PAYLOAD_ATTR_FIXED;
        value = name + string_size(name_len);
        if (value + value_len + 1u > limit) {
            return 0;
        }
        if (i == index) {
            if (out_entry) {
                *out_entry = cursor;
            }
            if (out_name) {
                *out_name = name;
            }
            if (out_name_length) {
                *out_name_length = name_len;
            }
            if (out_value) {
                *out_value = value;
            }
            if (out_value_length) {
                *out_value_length = value_len;
            }
            return 1;
        }
        cursor += attr_entry_size(name_len, value_len);
    }
    return 0;
}

/** Start of an element payload's attribute table, or NULL. */
static const char *element_table(const robo_html_document_slot_t *slot,
                                 const robo_html_node_entry_t *entry,
                                 const robo_html_payload_header_t **out_hdr,
                                 const char **out_name,
                                 size_t *out_name_length)
{
    const robo_html_payload_header_t *hdr = payload_get(slot, entry);
    const char *table;

    if (!hdr || entry->kind != ROBO_HTML_NODE_KIND_ELEMENT) {
        return NULL;
    }
    table = element_attr_cursor(slot, entry, out_name, out_name_length);
    if (out_hdr) {
        *out_hdr = hdr;
    }
    return table;
}

robo_result_t robo_html_payload_attr_at(const robo_html_document_slot_t *slot,
                                        const robo_html_node_entry_t *entry,
                                        size_t index,
                                        robo_html_attribute_t *out)
{
    const robo_html_payload_header_t *hdr = NULL;
    const char *table;
    const char *name = NULL;
    const char *value = NULL;
    size_t name_length = 0u;
    size_t value_length = 0u;

    if (!out) {
        return ROBO_NOT_FOUND;
    }
    robo_html_attribute_init(out);
    table = element_table(slot, entry, &hdr, NULL, NULL);
    if (!table) {
        return ROBO_NOT_FOUND;
    }
    if (!attr_entry_at(table,
                       ((const char *)hdr) + PAYLOAD_HEADER_SIZE + hdr->length,
                       index, NULL, &name, &name_length, &value,
                       &value_length)) {
        return ROBO_NOT_FOUND;
    }
    out->name = name;
    out->name_length = name_length;
    out->value = value;
    out->value_length = value_length;
    out->namespace_id = ROBO_HTML_NAMESPACE_NONE;
    return ROBO_OK;
}

robo_result_t robo_html_payload_attr_find(const robo_html_document_slot_t *slot,
                                          const robo_html_node_entry_t *entry,
                                          const char *name,
                                          robo_html_attribute_t *out)
{
    const robo_html_payload_header_t *hdr = NULL;
    const char *table;
    const char *limit;
    size_t count;
    size_t i;

    if (!name || !out) {
        return ROBO_NOT_FOUND;
    }
    robo_html_attribute_init(out);
    table = element_table(slot, entry, &hdr, NULL, NULL);
    if (!table) {
        return ROBO_NOT_FOUND;
    }
    limit = ((const char *)hdr) + PAYLOAD_HEADER_SIZE + hdr->length;
    count = (size_t)hdr->attr_count;
    for (i = 0u; i < count; i++) {
        const char *an = NULL;
        const char *av = NULL;
        size_t an_length = 0u;
        size_t av_length = 0u;

        if (!attr_entry_at(table, limit, i, NULL, &an, &an_length, &av,
                           &av_length)) {
            break;
        }
        if (!robo_html_name_equal_span(an, an_length, name)) {
            continue;
        }
        out->name = an;
        out->name_length = an_length;
        out->value = av;
        out->value_length = av_length;
        out->namespace_id = ROBO_HTML_NAMESPACE_NONE;
        return ROBO_OK;
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_html_payload_mutate_attributes(
    robo_html_document_slot_t *slot,
    robo_html_node_entry_t *entry,
    robo_html_attr_mutation_t mutation,
    const char *name,
    const char *value,
    robo_html_diag_t *diag)
{
    const robo_html_payload_header_t *hdr = NULL;
    const char *element_name = "";
    const char *table;
    const char *limit;
    const char *safe_value = value ? value : "";
    size_t element_name_length = 0u;
    size_t name_length;
    size_t value_length;
    size_t entry_size;
    size_t old_table_size;
    size_t scratch_size;
    size_t used;
    size_t old_count;
    size_t new_count = 0u;
    size_t i;
    uint32_t old_flags;
    uint32_t old_ns;
    int appended = 0;
    int replaced = 0;
    int matched = 0;
    char *scratch;
    char *cursor;
    uint32_t offset = 0u;
    void *raw = NULL;
    robo_html_payload_header_t *out_hdr;
    robo_result_t r;

    if (!slot || !entry || !name || name[0] == '\0') {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_INVALID_ARGUMENT,
                              "slot, entry or name is missing");
    }
    if (mutation != ROBO_HTML_ATTR_MUTATION_ADD &&
        mutation != ROBO_HTML_ATTR_MUTATION_SET &&
        mutation != ROBO_HTML_ATTR_MUTATION_REMOVE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_INVALID_ARGUMENT, "unknown mutation");
    }
    table = element_table(slot, entry, &hdr, &element_name,
                          &element_name_length);
    if (!table) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT, ROBO_NOT_FOUND,
                              "node is not a live element payload");
    }
    limit = ((const char *)hdr) + PAYLOAD_HEADER_SIZE + hdr->length;
    old_count = (size_t)hdr->attr_count;
    old_table_size = (size_t)hdr->length -
                     element_name_size(element_name, element_name_length);
    old_flags = hdr->flags;
    old_ns = hdr->ns;

    name_length = strlen(name);
    value_length = strlen(safe_value);
    entry_size = attr_entry_size(name_length, value_length);

    /* Pass 1 — decide what survives, so the cap is enforced before any
     * bytes are written and a no-op REMOVE costs nothing. */
    for (i = 0u; i < old_count; i++) {
        const char *an = NULL;
        size_t an_length = 0u;
        int is_match;

        if (!attr_entry_at(table, limit, i, NULL, &an, &an_length, NULL,
                           NULL)) {
            break;
        }
        is_match = robo_html_name_equal_span(an, an_length, name);
        if (is_match) {
            matched = 1;
            if (mutation == ROBO_HTML_ATTR_MUTATION_REMOVE) {
                continue;
            }
            if (mutation == ROBO_HTML_ATTR_MUTATION_SET) {
                if (replaced) {
                    continue; /* collapse the rest onto the first match */
                }
                replaced = 1;
            }
        }
        new_count++;
    }
    if (mutation == ROBO_HTML_ATTR_MUTATION_ADD ||
        (mutation == ROBO_HTML_ATTR_MUTATION_SET && !replaced)) {
        appended = 1;
        new_count++;
    }
    if (mutation == ROBO_HTML_ATTR_MUTATION_REMOVE && !matched) {
        return ROBO_OK; /* absent: removeAttribute is a no-op, not an error */
    }
    if (new_count > (size_t)ROBO_HTML_MAX_ATTRS_PER_ELEMENT) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_NOT_SUPPORTED,
                              "attribute limit for one element reached");
    }

    /* Pass 2 — build the replacement body in scratch memory. One entry is
     * the most a mutation can add, so old + one bounds every case. */
    scratch_size = element_name_size(element_name, element_name_length) +
                   old_table_size + entry_size;
    scratch = (char *)robo_malloc(scratch_size);
    if (!scratch) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ELEMENT,
                              ROBO_OUT_OF_MEMORY,
                              "could not stage the attribute rewrite");
    }
    /*
     * Zeroed before writing, like the payload writer is: every string in the
     * layout ends at a NUL the writer never copies, so the buffer's contents
     * are part of the encoding. Leaving them as heap litter is how a name
     * picks up a stray byte and every following offset drifts.
     */
    memset(scratch, 0, scratch_size);
    memcpy(scratch, element_name, element_name_length);
    cursor = scratch + string_size(element_name_length);
    replaced = 0;
    for (i = 0u; i < old_count; i++) {
        const char *raw_entry = NULL;
        const char *an = NULL;
        const char *av = NULL;
        size_t an_length = 0u;
        size_t av_length = 0u;
        int is_match;

        if (!attr_entry_at(table, limit, i, &raw_entry, &an, &an_length, &av,
                           &av_length)) {
            break;
        }
        is_match = robo_html_name_equal_span(an, an_length, name);
        if (is_match && mutation == ROBO_HTML_ATTR_MUTATION_REMOVE) {
            continue;
        }
        if (is_match && mutation == ROBO_HTML_ATTR_MUTATION_SET) {
            if (replaced) {
                continue;
            }
            replaced = 1;
            store_u32(cursor, (uint32_t)name_length);
            store_u32(cursor + 4, (uint32_t)value_length);
            cursor += PAYLOAD_ATTR_FIXED;
            memcpy(cursor, name, name_length);
            cursor += string_size(name_length);
            memcpy(cursor, safe_value, value_length);
            cursor += string_size(value_length);
            continue;
        }
        memcpy(cursor, raw_entry, attr_entry_size(an_length, av_length));
        cursor += attr_entry_size(an_length, av_length);
    }
    if (appended) {
        store_u32(cursor, (uint32_t)name_length);
        store_u32(cursor + 4, (uint32_t)value_length);
        cursor += PAYLOAD_ATTR_FIXED;
        memcpy(cursor, name, name_length);
        cursor += string_size(name_length);
        memcpy(cursor, safe_value, value_length);
        cursor += string_size(value_length);
    }
    used = (size_t)(cursor - scratch);

    r = robo_html_document_arena_alloc(slot, PAYLOAD_HEADER_SIZE + used,
                                       &offset, &raw, diag);
    if (r != ROBO_OK || !raw) {
        robo_free(scratch);
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }
    memset(raw, 0, PAYLOAD_HEADER_SIZE);
    out_hdr = (robo_html_payload_header_t *)raw;
    out_hdr->length = (uint32_t)used;
    out_hdr->attr_count = (uint32_t)new_count;
    out_hdr->flags = old_flags;
    out_hdr->ns = old_ns;
    memcpy((char *)raw + PAYLOAD_HEADER_SIZE, scratch, used);
    robo_free(scratch);
    robo_html_node_set_payload(entry, offset,
                              (uint32_t)(PAYLOAD_HEADER_SIZE + used));
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

robo_html_namespace_t robo_html_payload_namespace(
    const robo_html_document_slot_t *slot,
    const robo_html_node_entry_t *entry)
{
    const robo_html_payload_header_t *hdr = payload_get(slot, entry);

    if (!hdr || entry->kind != ROBO_HTML_NODE_KIND_ELEMENT) {
        return ROBO_HTML_NAMESPACE_NONE;
    }
    if (!robo_html_namespace_is_known((robo_html_namespace_t)hdr->ns)) {
        return ROBO_HTML_NAMESPACE_NONE;
    }
    return (robo_html_namespace_t)hdr->ns;
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
