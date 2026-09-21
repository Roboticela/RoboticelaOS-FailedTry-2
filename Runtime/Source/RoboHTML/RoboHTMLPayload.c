/*
 * RoboHTML node payload encoding/decoding (Phase 8.3; attribute model 8.6).
 *
 * One definition of the byte layout, shared by the writers (tree builder),
 * the readers (node/element accessors) and the attribute mutator. Layout:
 *
 *   header                  HtmlPayloadHeader
 *   header.length           payload bytes following the header
 *   header.attrCount       attribute entries following the name
 *   header.flags            ROBO_HTML_PAYLOAD_FLAG_*
 *   header.ns               element namespace (HtmlNamespace)
 *
 *   ELEMENT        name '\0'
 *                  then attrCount × ([u32 nameLen][u32 valueLen] name '\0' value '\0')
 *   TEXT/COMMENT   bytes '\0'
 *   DOCUMENT_TYPE  name '\0' public '\0' system '\0'
 *
 * Every string is NUL-terminated so readers can hand out borrowed C
 * strings, while the explicit u32 lengths let the walk skip a string
 * without scanning it — an attribute value containing a NUL byte from a
 * malformed source still round-trips without desynchronising the reader.
 *
 * The attribute table is the *stored* form of HtmlAttribute: same
 * fields, encoded. `nameLen`/`valueLen` are the authority, so the decoder
 * never has to scan; the writers accept either a measured length or the
 * convention that 0 means "measure with strlen", which is what lets a
 * caller hand over a plain C string without counting first.
 *
 * Payloads are written once and never edited, except two cases: appending to
 * a text payload that currently ends the arena grows it in place, and an
 * attribute mutation rewrites the element payload into fresh arena space
 * (see HtmlPayloadMutateAttributes).
 */

#include "RoboHTMLInternal.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

#define PAYLOAD_HEADER_SIZE ((size_t)sizeof(HtmlPayloadHeader))
#define PAYLOAD_ATTR_FIXED 8u /* two u32 lengths */

static size_t RoboStringSize(size_t length)
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
static size_t AttrEntrySize(size_t nameLength, size_t valueLength)
{
    return (size_t)PAYLOAD_ATTR_FIXED + RoboStringSize(nameLength) +
           RoboStringSize(valueLength);
}

static char AsciiLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/**
 * ASCII whitespace as HTML defines it in a reference: space, tab, newline,
 * form feed and carriage return. Deliberately not isspace(): the C locale's
 * extras (vertical tab, and anything a locale adds) are not HTML whitespace,
 * and a validity rule must not depend on the host locale.
 */
static int SpanIsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

int HtmlSpanIsUsable(const char *text, size_t length)
{
    size_t i;

    if (!text) {
        return 0;
    }
    for (i = 0u; i < length; i++) {
        if (!SpanIsSpace(text[i])) {
            return 1;
        }
    }
    return 0;
}

int HtmlNameEqual(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    return HtmlNameEqualSpan(a, strlen(a), b);
}

int HtmlNameEqualSpan(const char *a, size_t aLength, const char *b)
{
    size_t i;

    if (!a || !b) {
        return 0;
    }
    for (i = 0u; i < aLength; i++) {
        if (b[i] == '\0') {
            return 0; /* b is a prefix of a */
        }
        if (AsciiLower(a[i]) != AsciiLower(b[i])) {
            return 0;
        }
    }
    return b[aLength] == '\0';
}

/** Attribute table size for @p attrCount attributes. */
static size_t AttrTableSize(const HtmlAttribute *attrs,
                              size_t attrCount)
{
    size_t total = 0u;
    size_t i;

    for (i = 0u; i < attrCount; i++) {
        total += AttrEntrySize(measured(attrs[i].name, attrs[i].nameLength),
                                 measured(attrs[i].value,
                                          attrs[i].valueLength));
    }
    return total;
}

/** Bytes an element payload needs for @p name with no attributes. */
static size_t ElementNameSize(const char *name, size_t nameLength)
{
    return RoboStringSize(measured(name, nameLength));
}

size_t HtmlPayloadElementSize(const char *name,
                                      size_t nameLength,
                                      const HtmlAttribute *attrs,
                                      size_t attrCount)
{
    return ElementNameSize(name, nameLength) +
           AttrTableSize(attrs, attrCount ? attrCount : 0u);
}

/** Little-endian u32 store (payload bytes never depend on host endianness). */
static void StoreU32(char *dst, uint32_t v)
{
    dst[0] = (char)(v & 0xffu);
    dst[1] = (char)((v >> 8) & 0xffu);
    dst[2] = (char)((v >> 16) & 0xffu);
    dst[3] = (char)((v >> 24) & 0xffu);
}

static uint32_t LoadU32(const char *src)
{
    return (uint32_t)(unsigned char)src[0] |
           ((uint32_t)(unsigned char)src[1] << 8) |
           ((uint32_t)(unsigned char)src[2] << 16) |
           ((uint32_t)(unsigned char)src[3] << 24);
}

/** Borrowed payload pointer, or NULL when the range does not describe one. */
static const HtmlPayloadHeader *PayloadGet(
    const HtmlDocumentSlot *slot,
    const HtmlNodeEntry *entry)
{
    const HtmlPayloadHeader *hdr;

    if (!slot || !entry || entry->payloadLength < PAYLOAD_HEADER_SIZE) {
        return NULL;
    }
    hdr = (const HtmlPayloadHeader *)HtmlDocumentArenaAt(
        slot, entry->payloadOffset, entry->payloadLength);
    if (!hdr) {
        return NULL;
    }
    if (PAYLOAD_HEADER_SIZE + (size_t)hdr->length != entry->payloadLength) {
        return NULL; /* range does not describe a well-formed payload */
    }
    return hdr;
}

static char *PayloadMut(HtmlDocumentSlot *slot,
                         HtmlNodeEntry *entry)
{
    if (!slot || !entry || entry->payloadLength < PAYLOAD_HEADER_SIZE) {
        return NULL;
    }
    if ((size_t)entry->payloadOffset + entry->payloadLength >
        slot->payloadUsed) {
        return NULL;
    }
    return slot->payload + entry->payloadOffset;
}

/**
 * Encode the body (name + attribute table) at @p cursor, returning the
 * cursor past it. Shared by the element writer and the attribute mutator so
 * the two can never disagree about the encoding.
 */
static char *WriteElementBody(char *cursor,
                                const char *name,
                                size_t nameLength,
                                const HtmlAttribute *attrs,
                                size_t attrCount)
{
    size_t i;

    memcpy(cursor, name, nameLength);
    cursor += RoboStringSize(nameLength);
    for (i = 0u; i < attrCount; i++) {
        size_t alen = measured(attrs[i].name, attrs[i].nameLength);
        size_t vlen = measured(attrs[i].value, attrs[i].valueLength);

        StoreU32(cursor, (uint32_t)alen);
        StoreU32(cursor + 4, (uint32_t)vlen);
        cursor += PAYLOAD_ATTR_FIXED;
        if (alen > 0u) {
            memcpy(cursor, attrs[i].name, alen);
        }
        cursor += RoboStringSize(alen);
        if (vlen > 0u) {
            memcpy(cursor, attrs[i].value, vlen);
        }
        cursor += RoboStringSize(vlen);
    }
    return cursor;
}

Result HtmlPayloadWriteElement(
    HtmlDocumentSlot *slot,
    HtmlNodeEntry *entry,
    const char *name,
    size_t nameLength,
    const HtmlAttribute *attrs,
    size_t attrCount,
    int selfClosing,
    HtmlNamespace ns,
    HtmlDiag *diag)
{
    const char *safeName = name ? name : "";
    size_t effective;
    size_t body;
    size_t total;
    uint32_t offset = 0u;
    void *raw = NULL;
    char *cursor;
    HtmlPayloadHeader *hdr;

    if (!slot || !entry) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "slot or entry is NULL");
    }
    effective = measured(safeName, nameLength);
    body = HtmlPayloadElementSize(safeName, effective, attrs,
                                          attrCount);
    total = PAYLOAD_HEADER_SIZE + body;
    if (HtmlDocumentArenaAlloc(slot, total, &offset, &raw, diag) !=
            RoboOk ||
        !raw) {
        return diag ? diag->code : RoboOutOfMemory;
    }
    memset(raw, 0, total);
    cursor = WriteElementBody((char *)raw + PAYLOAD_HEADER_SIZE, safeName,
                                effective, attrs, attrCount);
    (void)cursor;

    hdr = (HtmlPayloadHeader *)raw;
    hdr->length = (uint32_t)body;
    hdr->attrCount = (uint32_t)attrCount;
    hdr->flags = selfClosing ? RoboHtmlPayloadFlagSelfClosing : 0u;
    hdr->ns = (uint32_t)ns;
    HtmlNodeSetPayload(entry, offset, (uint32_t)total);
    return RoboOk;
}

Result HtmlPayloadWriteText(HtmlDocumentSlot *slot,
                                           HtmlNodeEntry *entry,
                                           const char *bytes,
                                           size_t length,
                                           HtmlDiag *diag)
{
    size_t body = RoboStringSize(length);
    size_t total = PAYLOAD_HEADER_SIZE + body;
    uint32_t offset = 0u;
    void *raw = NULL;
    HtmlPayloadHeader *hdr;

    if (!slot || !entry || (!bytes && length > 0u)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "bad text payload args");
    }
    if (total > 0xFFFFFFFFu) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotSupported,
                              "text run exceeds one payload");
    }
    if (HtmlDocumentArenaAlloc(slot, total, &offset, &raw, diag) !=
            RoboOk ||
        !raw) {
        return diag ? diag->code : RoboOutOfMemory;
    }
    memset(raw, 0, total);
    if (length > 0u) {
        memcpy((char *)raw + PAYLOAD_HEADER_SIZE, bytes, length);
    }
    hdr = (HtmlPayloadHeader *)raw;
    hdr->length = (uint32_t)body;
    hdr->attrCount = 0u;
    hdr->flags = 0u;
    hdr->ns = (uint32_t)RoboHtmlNamespaceNone;
    HtmlNodeSetPayload(entry, offset, (uint32_t)total);
    return RoboOk;
}

Result HtmlPayloadAppendText(HtmlDocumentSlot *slot,
                                            HtmlNodeEntry *entry,
                                            const char *bytes,
                                            size_t length,
                                            HtmlDiag *diag)
{
    char *base;
    HtmlPayloadHeader *hdr;

    if (!slot || !entry || (!bytes && length > 0u)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "bad text append args");
    }
    if (length == 0u) {
        return RoboOk;
    }
    base = PayloadMut(slot, entry);
    if (!base) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "text payload does not resolve");
    }
    /* Only a payload that currently ends the arena can grow in place. */
    if ((size_t)entry->payloadOffset + entry->payloadLength !=
        slot->payloadUsed) {
        return RoboNotSupported;
    }
    if (PayloadGet(slot, entry) == NULL) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "text payload header is inconsistent");
    }
    if ((size_t)entry->payloadLength + length > 0xFFFFFFFFu) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotSupported,
                              "text run exceeds one payload");
    }
    {
        void *grown = NULL;
        if (HtmlDocumentArenaExtendTail(slot, entry->payloadOffset,
                                                 entry->payloadLength, length,
                                                 &grown, diag) != RoboOk) {
            return diag ? diag->code : RoboOutOfMemory;
        }
        base = (char *)grown;
    }
    hdr = (HtmlPayloadHeader *)base;
    /* The terminator currently sits at header+length; write over it. */
    memcpy(base + PAYLOAD_HEADER_SIZE + (size_t)hdr->length - 1u, bytes,
           length);
    hdr->length += (uint32_t)length;
    base[PAYLOAD_HEADER_SIZE + (size_t)hdr->length - 1u] = '\0';
    entry->payloadLength += (uint32_t)length;
    return RoboOk;
}

Result HtmlPayloadWriteDoctype(HtmlDocumentSlot *slot,
                                              HtmlNodeEntry *entry,
                                              const char *name,
                                              const char *publicId,
                                              const char *systemId,
                                              HtmlDiag *diag)
{
    const char *n = name ? name : "";
    const char *p = publicId ? publicId : "";
    const char *s = systemId ? systemId : "";
    size_t body = RoboStringSize(strlen(n)) + RoboStringSize(strlen(p)) +
                  RoboStringSize(strlen(s));
    size_t total = PAYLOAD_HEADER_SIZE + body;
    uint32_t offset = 0u;
    void *raw = NULL;
    char *cursor;
    HtmlPayloadHeader *hdr;

    if (!slot || !entry) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "slot or entry is NULL");
    }
    if (HtmlDocumentArenaAlloc(slot, total, &offset, &raw, diag) !=
            RoboOk ||
        !raw) {
        return diag ? diag->code : RoboOutOfMemory;
    }
    memset(raw, 0, total);
    cursor = (char *)raw + PAYLOAD_HEADER_SIZE;
    memcpy(cursor, n, strlen(n));
    cursor += RoboStringSize(strlen(n));
    memcpy(cursor, p, strlen(p));
    cursor += RoboStringSize(strlen(p));
    memcpy(cursor, s, strlen(s));
    hdr = (HtmlPayloadHeader *)raw;
    hdr->length = (uint32_t)body;
    hdr->attrCount = 0u;
    hdr->flags = 0u;
    hdr->ns = (uint32_t)RoboHtmlNamespaceNone;
    HtmlNodeSetPayload(entry, offset, (uint32_t)total);
    return RoboOk;
}

/**
 * Walk to the attribute table (past the element name).
 * @return cursor at the first [u32 nameLen], or NULL when the payload is
 *         not an element payload.
 */
static const char *ElementAttrCursor(
    const HtmlDocumentSlot *slot,
    const HtmlNodeEntry *entry,
    const char **outName,
    size_t *outNameLength)
{
    const HtmlPayloadHeader *hdr = PayloadGet(slot, entry);
    const char *bytes;
    const char *name;

    if (outName) {
        *outName = "";
    }
    if (outNameLength) {
        *outNameLength = 0u;
    }
    if (!hdr || entry->kind != RoboHtmlNodeKindElement) {
        return NULL;
    }
    bytes = ((const char *)hdr) + PAYLOAD_HEADER_SIZE;
    name = bytes;
    if (outName) {
        *outName = name;
    }
    if (outNameLength) {
        *outNameLength = strlen(name);
    }
    return name + strlen(name) + 1u;
}

const char *HtmlPayloadElementName(
    const HtmlDocumentSlot *slot,
    const HtmlNodeEntry *entry,
    size_t *outLength)
{
    const char *name = "";
    size_t length = 0u;

    (void)ElementAttrCursor(slot, entry, &name, &length);
    if (outLength) {
        *outLength = length;
    }
    return name ? name : "";
}

size_t HtmlPayloadAttrCount(const HtmlDocumentSlot *slot,
                                    const HtmlNodeEntry *entry)
{
    const HtmlPayloadHeader *hdr = PayloadGet(slot, entry);

    if (!hdr || entry->kind != RoboHtmlNodeKindElement) {
        return 0u;
    }
    return (size_t)hdr->attrCount;
}

/**
 * Resolve attribute @p index to borrowed bytes.
 *
 * @p outEntry receives the entry's first byte (its pair of u32 lengths) so
 * a caller that wants to copy the entry verbatim can do exactly that.
 * Lengths are the authority, so every step is arithmetic on the header — no
 * scan for a terminator, and a value containing a NUL byte cannot
 * desynchronise the walk.
 *
 * @return non-zero when the entry exists and lies inside @p limit.
 */
static int AttrEntryAt(const char *table,
                         const char *limit,
                         size_t index,
                         const char **outEntry,
                         const char **outName,
                         size_t *outNameLength,
                         const char **outValue,
                         size_t *outValueLength)
{
    const char *cursor = table;
    size_t i;

    for (i = 0u; i <= index; i++) {
        uint32_t nameLen;
        uint32_t valueLen;
        const char *name;
        const char *value;

        if (cursor + PAYLOAD_ATTR_FIXED > limit) {
            return 0;
        }
        nameLen = LoadU32(cursor);
        valueLen = LoadU32(cursor + 4);
        name = cursor + PAYLOAD_ATTR_FIXED;
        value = name + RoboStringSize(nameLen);
        if (value + valueLen + 1u > limit) {
            return 0;
        }
        if (i == index) {
            if (outEntry) {
                *outEntry = cursor;
            }
            if (outName) {
                *outName = name;
            }
            if (outNameLength) {
                *outNameLength = nameLen;
            }
            if (outValue) {
                *outValue = value;
            }
            if (outValueLength) {
                *outValueLength = valueLen;
            }
            return 1;
        }
        cursor += AttrEntrySize(nameLen, valueLen);
    }
    return 0;
}

/** Start of an element payload's attribute table, or NULL. */
static const char *ElementTable(const HtmlDocumentSlot *slot,
                                 const HtmlNodeEntry *entry,
                                 const HtmlPayloadHeader **outHdr,
                                 const char **outName,
                                 size_t *outNameLength)
{
    const HtmlPayloadHeader *hdr = PayloadGet(slot, entry);
    const char *table;

    if (!hdr || entry->kind != RoboHtmlNodeKindElement) {
        return NULL;
    }
    table = ElementAttrCursor(slot, entry, outName, outNameLength);
    if (outHdr) {
        *outHdr = hdr;
    }
    return table;
}

Result HtmlPayloadAttrAt(const HtmlDocumentSlot *slot,
                                        const HtmlNodeEntry *entry,
                                        size_t index,
                                        HtmlAttribute *out)
{
    const HtmlPayloadHeader *hdr = NULL;
    const char *table;
    const char *name = NULL;
    const char *value = NULL;
    size_t nameLength = 0u;
    size_t valueLength = 0u;

    if (!out) {
        return RoboNotFound;
    }
    HtmlAttributeInit(out);
    table = ElementTable(slot, entry, &hdr, NULL, NULL);
    if (!table) {
        return RoboNotFound;
    }
    if (!AttrEntryAt(table,
                       ((const char *)hdr) + PAYLOAD_HEADER_SIZE + hdr->length,
                       index, NULL, &name, &nameLength, &value,
                       &valueLength)) {
        return RoboNotFound;
    }
    out->name = name;
    out->nameLength = nameLength;
    out->value = value;
    out->valueLength = valueLength;
    out->namespaceId = RoboHtmlNamespaceNone;
    return RoboOk;
}

Result HtmlPayloadAttrFind(const HtmlDocumentSlot *slot,
                                          const HtmlNodeEntry *entry,
                                          const char *name,
                                          HtmlAttribute *out)
{
    const HtmlPayloadHeader *hdr = NULL;
    const char *table;
    const char *limit;
    size_t count;
    size_t i;

    if (!name || !out) {
        return RoboNotFound;
    }
    HtmlAttributeInit(out);
    table = ElementTable(slot, entry, &hdr, NULL, NULL);
    if (!table) {
        return RoboNotFound;
    }
    limit = ((const char *)hdr) + PAYLOAD_HEADER_SIZE + hdr->length;
    count = (size_t)hdr->attrCount;
    for (i = 0u; i < count; i++) {
        const char *an = NULL;
        const char *av = NULL;
        size_t anLength = 0u;
        size_t avLength = 0u;

        if (!AttrEntryAt(table, limit, i, NULL, &an, &anLength, &av,
                           &avLength)) {
            break;
        }
        if (!HtmlNameEqualSpan(an, anLength, name)) {
            continue;
        }
        out->name = an;
        out->nameLength = anLength;
        out->value = av;
        out->valueLength = avLength;
        out->namespaceId = RoboHtmlNamespaceNone;
        return RoboOk;
    }
    return RoboNotFound;
}

Result HtmlPayloadMutateAttributes(
    HtmlDocumentSlot *slot,
    HtmlNodeEntry *entry,
    HtmlAttrMutation mutation,
    const char *name,
    const char *value,
    HtmlDiag *diag)
{
    const HtmlPayloadHeader *hdr = NULL;
    const char *elementName = "";
    const char *table;
    const char *limit;
    const char *safeValue = value ? value : "";
    size_t elementNameLength = 0u;
    size_t nameLength;
    size_t valueLength;
    size_t entrySize;
    size_t oldTableSize;
    size_t scratchSize;
    size_t used;
    size_t oldCount;
    size_t newCount = 0u;
    size_t i;
    uint32_t oldFlags;
    uint32_t oldNs;
    int appended = 0;
    int replaced = 0;
    int matched = 0;
    char *scratch;
    char *cursor;
    uint32_t offset = 0u;
    void *raw = NULL;
    HtmlPayloadHeader *outHdr;
    Result r;

    if (!slot || !entry || !name || name[0] == '\0') {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboInvalidArgument,
                              "slot, entry or name is missing");
    }
    if (mutation != RoboHtmlAttrMutationAdd &&
        mutation != RoboHtmlAttrMutationSet &&
        mutation != RoboHtmlAttrMutationRemove) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboInvalidArgument, "unknown mutation");
    }
    table = ElementTable(slot, entry, &hdr, &elementName,
                          &elementNameLength);
    if (!table) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement, RoboNotFound,
                              "node is not a live element payload");
    }
    limit = ((const char *)hdr) + PAYLOAD_HEADER_SIZE + hdr->length;
    oldCount = (size_t)hdr->attrCount;
    oldTableSize = (size_t)hdr->length -
                     ElementNameSize(elementName, elementNameLength);
    oldFlags = hdr->flags;
    oldNs = hdr->ns;

    nameLength = strlen(name);
    valueLength = strlen(safeValue);
    entrySize = AttrEntrySize(nameLength, valueLength);

    /* Pass 1 — decide what survives, so the cap is enforced before any
     * bytes are written and a no-op REMOVE costs nothing. */
    for (i = 0u; i < oldCount; i++) {
        const char *an = NULL;
        size_t anLength = 0u;
        int isMatch;

        if (!AttrEntryAt(table, limit, i, NULL, &an, &anLength, NULL,
                           NULL)) {
            break;
        }
        isMatch = HtmlNameEqualSpan(an, anLength, name);
        if (isMatch) {
            matched = 1;
            if (mutation == RoboHtmlAttrMutationRemove) {
                continue;
            }
            if (mutation == RoboHtmlAttrMutationSet) {
                if (replaced) {
                    continue; /* collapse the rest onto the first match */
                }
                replaced = 1;
            }
        }
        newCount++;
    }
    if (mutation == RoboHtmlAttrMutationAdd ||
        (mutation == RoboHtmlAttrMutationSet && !replaced)) {
        appended = 1;
        newCount++;
    }
    if (mutation == RoboHtmlAttrMutationRemove && !matched) {
        return RoboOk; /* absent: removeAttribute is a no-op, not an error */
    }
    if (newCount > (size_t)RoboHtmlMaxAttrsPerElement) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboNotSupported,
                              "attribute limit for one element reached");
    }

    /* Pass 2 — build the replacement body in scratch memory. One entry is
     * the most a mutation can add, so old + one bounds every case. */
    scratchSize = ElementNameSize(elementName, elementNameLength) +
                   oldTableSize + entrySize;
    scratch = (char *)Malloc(scratchSize);
    if (!scratch) {
        return RoboHtmlDiag(diag, RoboHtmlDomainElement,
                              RoboOutOfMemory,
                              "could not stage the attribute rewrite");
    }
    /*
     * Zeroed before writing, like the payload writer is: every string in the
     * layout ends at a NUL the writer never copies, so the buffer's contents
     * are part of the encoding. Leaving them as heap litter is how a name
     * picks up a stray byte and every following offset drifts.
     */
    memset(scratch, 0, scratchSize);
    memcpy(scratch, elementName, elementNameLength);
    cursor = scratch + RoboStringSize(elementNameLength);
    replaced = 0;
    for (i = 0u; i < oldCount; i++) {
        const char *rawEntry = NULL;
        const char *an = NULL;
        const char *av = NULL;
        size_t anLength = 0u;
        size_t avLength = 0u;
        int isMatch;

        if (!AttrEntryAt(table, limit, i, &rawEntry, &an, &anLength, &av,
                           &avLength)) {
            break;
        }
        isMatch = HtmlNameEqualSpan(an, anLength, name);
        if (isMatch && mutation == RoboHtmlAttrMutationRemove) {
            continue;
        }
        if (isMatch && mutation == RoboHtmlAttrMutationSet) {
            if (replaced) {
                continue;
            }
            replaced = 1;
            StoreU32(cursor, (uint32_t)nameLength);
            StoreU32(cursor + 4, (uint32_t)valueLength);
            cursor += PAYLOAD_ATTR_FIXED;
            memcpy(cursor, name, nameLength);
            cursor += RoboStringSize(nameLength);
            memcpy(cursor, safeValue, valueLength);
            cursor += RoboStringSize(valueLength);
            continue;
        }
        memcpy(cursor, rawEntry, AttrEntrySize(anLength, avLength));
        cursor += AttrEntrySize(anLength, avLength);
    }
    if (appended) {
        StoreU32(cursor, (uint32_t)nameLength);
        StoreU32(cursor + 4, (uint32_t)valueLength);
        cursor += PAYLOAD_ATTR_FIXED;
        memcpy(cursor, name, nameLength);
        cursor += RoboStringSize(nameLength);
        memcpy(cursor, safeValue, valueLength);
        cursor += RoboStringSize(valueLength);
    }
    used = (size_t)(cursor - scratch);

    r = HtmlDocumentArenaAlloc(slot, PAYLOAD_HEADER_SIZE + used,
                                       &offset, &raw, diag);
    if (r != RoboOk || !raw) {
        RoboFree(scratch);
        return diag ? diag->code : RoboOutOfMemory;
    }
    memset(raw, 0, PAYLOAD_HEADER_SIZE);
    outHdr = (HtmlPayloadHeader *)raw;
    outHdr->length = (uint32_t)used;
    outHdr->attrCount = (uint32_t)newCount;
    outHdr->flags = oldFlags;
    outHdr->ns = oldNs;
    memcpy((char *)raw + PAYLOAD_HEADER_SIZE, scratch, used);
    RoboFree(scratch);
    HtmlNodeSetPayload(entry, offset,
                              (uint32_t)(PAYLOAD_HEADER_SIZE + used));
    return RoboOk;
}

const char *HtmlPayloadText(const HtmlDocumentSlot *slot,
                                   const HtmlNodeEntry *entry,
                                   size_t *outLength)
{
    const HtmlPayloadHeader *hdr = PayloadGet(slot, entry);

    if (outLength) {
        *outLength = 0u;
    }
    if (!hdr || (entry->kind != RoboHtmlNodeKindText &&
                 entry->kind != RoboHtmlNodeKindComment)) {
        return "";
    }
    if (outLength) {
        *outLength = hdr->length > 0u ? (size_t)hdr->length - 1u : 0u;
    }
    return ((const char *)hdr) + PAYLOAD_HEADER_SIZE;
}

int HtmlPayloadSelfClosing(const HtmlDocumentSlot *slot,
                                   const HtmlNodeEntry *entry)
{
    const HtmlPayloadHeader *hdr = PayloadGet(slot, entry);

    if (!hdr || entry->kind != RoboHtmlNodeKindElement) {
        return 0;
    }
    return (hdr->flags & RoboHtmlPayloadFlagSelfClosing) ? 1 : 0;
}

HtmlNamespace HtmlPayloadNamespace(
    const HtmlDocumentSlot *slot,
    const HtmlNodeEntry *entry)
{
    const HtmlPayloadHeader *hdr = PayloadGet(slot, entry);

    if (!hdr || entry->kind != RoboHtmlNodeKindElement) {
        return RoboHtmlNamespaceNone;
    }
    if (!HtmlNamespaceIsKnown((HtmlNamespace)hdr->ns)) {
        return RoboHtmlNamespaceNone;
    }
    return (HtmlNamespace)hdr->ns;
}

void HtmlPayloadDoctype(const HtmlDocumentSlot *slot,
                               const HtmlNodeEntry *entry,
                               const char **outName,
                               const char **outPublicId,
                               const char **outSystemId)
{
    const HtmlPayloadHeader *hdr = PayloadGet(slot, entry);
    const char *cursor;

    if (outName) {
        *outName = "";
    }
    if (outPublicId) {
        *outPublicId = "";
    }
    if (outSystemId) {
        *outSystemId = "";
    }
    if (!hdr || entry->kind != RoboHtmlNodeKindDocumentType) {
        return;
    }
    cursor = ((const char *)hdr) + PAYLOAD_HEADER_SIZE;
    if (outName) {
        *outName = cursor;
    }
    cursor += strlen(cursor) + 1u;
    if (outPublicId) {
        *outPublicId = cursor;
    }
    cursor += strlen(cursor) + 1u;
    if (outSystemId) {
        *outSystemId = cursor;
    }
}
