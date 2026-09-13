#include "RoboFont/RoboFontSfnt.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <stdint.h>

struct robo_font_sfnt {
    uint32_t refs;
    robo_font_source_t *source;
    robo_font_sfnt_header_t header;
    robo_font_sfnt_table_record_t *tables; /* num_tables entries, sorted by tag */
    uint32_t table_count;
    size_t sfnt_base; /* offset of this face's SFNT header in source */
    uint32_t flags;
};

int robo_font_sfnt_version_is_supported(uint32_t version)
{
    return version == ROBO_FONT_SFNT_VERSION_TRUE ||
           version == ROBO_FONT_SFNT_VERSION_TRUE_TAG ||
           version == ROBO_FONT_SFNT_VERSION_TYP1 ||
           version == ROBO_FONT_SFNT_VERSION_OTTO;
}

uint32_t robo_font_sfnt_checksum(const uint8_t *data, size_t length)
{
    uint32_t sum = 0u;
    size_t i;
    if (!data && length != 0u) {
        return 0u;
    }
    for (i = 0u; i + 4u <= length; i += 4u) {
        sum += ((uint32_t)data[i] << 24) | ((uint32_t)data[i + 1u] << 16) |
               ((uint32_t)data[i + 2u] << 8) | (uint32_t)data[i + 3u];
    }
    if (i < length) {
        uint32_t word = 0u;
        size_t rem = length - i;
        if (rem >= 1u) {
            word |= (uint32_t)data[i] << 24;
        }
        if (rem >= 2u) {
            word |= (uint32_t)data[i + 1u] << 16;
        }
        if (rem >= 3u) {
            word |= (uint32_t)data[i + 2u] << 8;
        }
        sum += word;
    }
    return sum;
}

int robo_font_sfnt_search_fields_consistent(const robo_font_sfnt_header_t *h)
{
    uint16_t n;
    uint16_t pow2;
    uint16_t entry;
    uint16_t search;
    uint16_t shift;
    if (!h) {
        return 0;
    }
    n = h->num_tables;
    if (n == 0u) {
        return h->search_range == 0u && h->entry_selector == 0u && h->range_shift == 0u;
    }
    pow2 = 1u;
    entry = 0u;
    while ((uint16_t)(pow2 * 2u) <= n) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
        if (entry > 15u) {
            break;
        }
    }
    search = (uint16_t)(pow2 * 16u);
    shift = (uint16_t)(n * 16u - search);
    return h->search_range == search && h->entry_selector == entry &&
           h->range_shift == shift;
}

robo_result_t robo_font_tag_format(robo_font_tag_t tag, char *buf, size_t buf_cap)
{
    int i;
    if (!buf || buf_cap < 5u) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0; i < 4; i++) {
        uint8_t c = (uint8_t)((tag >> (24 - 8 * i)) & 0xFFu);
        buf[i] = (c >= 0x20u && c <= 0x7Eu) ? (char)c : '.';
    }
    buf[4] = '\0';
    return ROBO_OK;
}

/* Insertion sort — table count is small (≤4095). */
static void sort_tables(robo_font_sfnt_table_record_t *t, uint32_t n)
{
    uint32_t i;
    for (i = 1u; i < n; i++) {
        robo_font_sfnt_table_record_t key = t[i];
        uint32_t j = i;
        while (j > 0u && t[j - 1u].tag > key.tag) {
            t[j] = t[j - 1u];
            j--;
        }
        t[j] = key;
    }
}

static int has_duplicate_tags(const robo_font_sfnt_table_record_t *t, uint32_t n)
{
    uint32_t i;
    for (i = 1u; i < n; i++) {
        if (t[i].tag == t[i - 1u].tag) {
            return 1;
        }
    }
    return 0;
}

typedef struct {
    uint32_t offset;
    uint8_t is_end;
} edge_t;

/* Ends before starts at the same offset so adjacent (non-overlapping) tables pass. */
static int edge_less(const edge_t *a, const edge_t *b)
{
    if (a->offset != b->offset) {
        return a->offset < b->offset;
    }
    return a->is_end > b->is_end;
}

static void sort_edges(edge_t *e, uint32_t n)
{
    uint32_t i;
    for (i = 1u; i < n; i++) {
        edge_t key = e[i];
        uint32_t j = i;
        while (j > 0u && edge_less(&key, &e[j - 1u])) {
            e[j] = e[j - 1u];
            j--;
        }
        e[j] = key;
    }
}

static robo_result_t reject_overlaps(const robo_font_sfnt_table_record_t *tables,
                                     uint32_t n)
{
    edge_t *edges;
    uint32_t i;
    uint32_t active;
    uint32_t en;
    uint32_t ecount;

    if (n == 0u) {
        return ROBO_OK;
    }
    en = n * 2u;
    edges = (edge_t *)robo_calloc(en, sizeof(edge_t));
    if (!edges) {
        return ROBO_OUT_OF_MEMORY;
    }
    ecount = 0u;
    for (i = 0u; i < n; i++) {
        uint32_t end = tables[i].offset + tables[i].length;
        if (end < tables[i].offset) {
            robo_free(edges);
            return ROBO_PARSE_ERROR;
        }
        /* Zero-length tables occupy no byte range. */
        if (tables[i].length == 0u) {
            continue;
        }
        edges[ecount].offset = tables[i].offset;
        edges[ecount].is_end = 0u;
        ecount++;
        edges[ecount].offset = end;
        edges[ecount].is_end = 1u;
        ecount++;
    }
    sort_edges(edges, ecount);
    active = 0u;
    for (i = 0u; i < ecount; i++) {
        if (edges[i].is_end) {
            if (active == 0u) {
                robo_free(edges);
                return ROBO_PARSE_ERROR;
            }
            active--;
        } else {
            active++;
            if (active > 1u) {
                robo_free(edges);
                return ROBO_PARSE_ERROR;
            }
        }
    }
    robo_free(edges);
    return ROBO_OK;
}

static robo_result_t parse_sfnt_at(robo_font_source_t *source,
                                   size_t base,
                                   uint32_t flags,
                                   robo_font_sfnt_t **out)
{
    robo_font_reader_t r;
    robo_font_sfnt_t *sfnt;
    const uint8_t *bytes;
    size_t size;
    uint32_t version;
    uint16_t num_tables;
    uint16_t search_range;
    uint16_t entry_selector;
    uint16_t range_shift;
    uint32_t i;
    size_t dir_end;
    size_t header_end;
    robo_result_t res;

    if (!out || !source) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    bytes = robo_font_source_bytes(source);
    size = robo_font_source_size(source);
    if (!bytes || size == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (size > ROBO_FONT_SFNT_MAX_SOURCE_SIZE) {
        return ROBO_PARSE_ERROR;
    }
    if (base > size || size - base < 12u) {
        return ROBO_PARSE_ERROR;
    }

    robo_font_reader_init(&r, bytes, size);
    if (robo_font_reader_seek(&r, base) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }

    if (robo_font_read_u32be(&r, &version) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (!robo_font_sfnt_version_is_supported(version)) {
        return ROBO_NOT_SUPPORTED;
    }
    if (robo_font_read_u16be(&r, &num_tables) != ROBO_OK ||
        robo_font_read_u16be(&r, &search_range) != ROBO_OK ||
        robo_font_read_u16be(&r, &entry_selector) != ROBO_OK ||
        robo_font_read_u16be(&r, &range_shift) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }

    if (num_tables == 0u || num_tables > ROBO_FONT_SFNT_MAX_TABLES) {
        return ROBO_PARSE_ERROR;
    }

    header_end = base + 12u;
    /* directory size = num_tables * 16, check overflow */
    if ((size_t)num_tables > (SIZE_MAX / 16u)) {
        return ROBO_PARSE_ERROR;
    }
    dir_end = header_end + (size_t)num_tables * 16u;
    if (dir_end < header_end || dir_end > size) {
        return ROBO_PARSE_ERROR;
    }

    sfnt = (robo_font_sfnt_t *)robo_calloc(1, sizeof(*sfnt));
    if (!sfnt) {
        return ROBO_OUT_OF_MEMORY;
    }
    sfnt->tables = (robo_font_sfnt_table_record_t *)robo_calloc(
        num_tables, sizeof(robo_font_sfnt_table_record_t));
    if (!sfnt->tables) {
        robo_free(sfnt);
        return ROBO_OUT_OF_MEMORY;
    }

    for (i = 0u; i < num_tables; i++) {
        uint32_t tag;
        uint32_t checksum;
        uint32_t offset;
        uint32_t length;
        if (robo_font_read_u32be(&r, &tag) != ROBO_OK ||
            robo_font_read_u32be(&r, &checksum) != ROBO_OK ||
            robo_font_read_u32be(&r, &offset) != ROBO_OK ||
            robo_font_read_u32be(&r, &length) != ROBO_OK) {
            robo_free(sfnt->tables);
            robo_free(sfnt);
            return ROBO_PARSE_ERROR;
        }
        /* Offset must be 4-byte aligned per OpenType. */
        if ((offset & 3u) != 0u) {
            robo_free(sfnt->tables);
            robo_free(sfnt);
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_checked_range(size, offset, length) != ROBO_OK) {
            robo_free(sfnt->tables);
            robo_free(sfnt);
            return ROBO_PARSE_ERROR;
        }
        /* Reject tables that overlap this face's header + directory.
         * Earlier absolute offsets are allowed (TTC shared tables). */
        if (length != 0u) {
            size_t tend = (size_t)offset + (size_t)length;
            if ((size_t)offset < dir_end && base < tend) {
                robo_free(sfnt->tables);
                robo_free(sfnt);
                return ROBO_PARSE_ERROR;
            }
        } else if ((size_t)offset >= base && (size_t)offset < dir_end) {
            robo_free(sfnt->tables);
            robo_free(sfnt);
            return ROBO_PARSE_ERROR;
        }
        sfnt->tables[i].tag = tag;
        sfnt->tables[i].checksum = checksum;
        sfnt->tables[i].offset = offset;
        sfnt->tables[i].length = length;
    }

    res = reject_overlaps(sfnt->tables, num_tables);
    if (res != ROBO_OK) {
        robo_free(sfnt->tables);
        robo_free(sfnt);
        return res;
    }

    sort_tables(sfnt->tables, num_tables);
    if (has_duplicate_tags(sfnt->tables, num_tables)) {
        robo_free(sfnt->tables);
        robo_free(sfnt);
        return ROBO_PARSE_ERROR;
    }

    if ((flags & ROBO_FONT_SFNT_FLAGS_VERIFY_CHECKSUMS) != 0u) {
        for (i = 0u; i < num_tables; i++) {
            const robo_font_sfnt_table_record_t *tr = &sfnt->tables[i];
            uint32_t calc =
                robo_font_sfnt_checksum(bytes + tr->offset, (size_t)tr->length);
            /* head checksumAdjustment is not applied here (Goal 5.3+). */
            if (calc != tr->checksum) {
                robo_free(sfnt->tables);
                robo_free(sfnt);
                return ROBO_PARSE_ERROR;
            }
        }
    }

    sfnt->refs = 1u;
    robo_font_source_retain(source);
    sfnt->source = source;
    sfnt->header.sfnt_version = version;
    sfnt->header.num_tables = num_tables;
    sfnt->header.search_range = search_range;
    sfnt->header.entry_selector = entry_selector;
    sfnt->header.range_shift = range_shift;
    sfnt->table_count = num_tables;
    sfnt->sfnt_base = base;
    sfnt->flags = flags;
    *out = sfnt;
    return ROBO_OK;
}

static robo_result_t resolve_face_base(robo_font_source_t *source,
                                       uint32_t face_index,
                                       size_t *out_base)
{
    robo_font_reader_t r;
    const uint8_t *bytes;
    size_t size;
    uint32_t tag;
    uint32_t ttc_version;
    uint32_t num_fonts;
    uint32_t offset;

    if (!out_base) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_base = 0u;
    bytes = robo_font_source_bytes(source);
    size = robo_font_source_size(source);
    if (!bytes || size < 4u) {
        return ROBO_PARSE_ERROR;
    }

    robo_font_reader_init(&r, bytes, size);
    if (robo_font_peek_u32be(&r, &tag) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }

    if (tag != ROBO_FONT_SFNT_VERSION_TTCF) {
        if (face_index != 0u) {
            return ROBO_NOT_FOUND;
        }
        *out_base = 0u;
        return ROBO_OK;
    }

    /* TTC header: 'ttcf', uint32 version, uint32 numFonts, offsets... */
    if (size < 12u) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_read_u32be(&r, &tag) != ROBO_OK ||
        robo_font_read_u32be(&r, &ttc_version) != ROBO_OK ||
        robo_font_read_u32be(&r, &num_fonts) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (ttc_version != 0x00010000u && ttc_version != 0x00020000u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (num_fonts == 0u || num_fonts > ROBO_FONT_SFNT_MAX_TABLES) {
        return ROBO_PARSE_ERROR;
    }
    if (face_index >= num_fonts) {
        return ROBO_NOT_FOUND;
    }
    if (robo_font_checked_range(size, 12u, (size_t)num_fonts * 4u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_reader_seek(&r, 12u + (size_t)face_index * 4u) != ROBO_OK ||
        robo_font_read_u32be(&r, &offset) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if ((size_t)offset > size || size - (size_t)offset < 12u) {
        return ROBO_PARSE_ERROR;
    }
    *out_base = (size_t)offset;
    return ROBO_OK;
}

robo_result_t robo_font_sfnt_parse(robo_font_source_t *source,
                                   uint32_t face_index,
                                   uint32_t flags,
                                   robo_font_sfnt_t **out)
{
    size_t base = 0u;
    robo_result_t res;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (!source) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_font_source_size(source) > ROBO_FONT_SFNT_MAX_SOURCE_SIZE) {
        return ROBO_PARSE_ERROR;
    }

    res = resolve_face_base(source, face_index, &base);
    if (res != ROBO_OK) {
        return res;
    }
    return parse_sfnt_at(source, base, flags, out);
}

void robo_font_sfnt_retain(robo_font_sfnt_t *sfnt)
{
    if (!sfnt) {
        return;
    }
    sfnt->refs++;
}

void robo_font_sfnt_release(robo_font_sfnt_t *sfnt)
{
    if (!sfnt) {
        return;
    }
    if (sfnt->refs == 0u) {
        return;
    }
    sfnt->refs--;
    if (sfnt->refs != 0u) {
        return;
    }
    robo_font_source_release(sfnt->source);
    sfnt->source = NULL;
    robo_free(sfnt->tables);
    sfnt->tables = NULL;
    robo_free(sfnt);
}

const robo_font_source_t *robo_font_sfnt_source(const robo_font_sfnt_t *sfnt)
{
    return sfnt ? sfnt->source : NULL;
}

const robo_font_sfnt_header_t *robo_font_sfnt_header(const robo_font_sfnt_t *sfnt)
{
    return sfnt ? &sfnt->header : NULL;
}

uint32_t robo_font_sfnt_table_count(const robo_font_sfnt_t *sfnt)
{
    return sfnt ? sfnt->table_count : 0u;
}

static const robo_font_sfnt_table_record_t *find_rec(const robo_font_sfnt_t *sfnt,
                                                     robo_font_tag_t tag)
{
    uint32_t lo;
    uint32_t hi;
    if (!sfnt || !sfnt->tables) {
        return NULL;
    }
    lo = 0u;
    hi = sfnt->table_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (sfnt->tables[mid].tag < tag) {
            lo = mid + 1u;
        } else if (sfnt->tables[mid].tag > tag) {
            hi = mid;
        } else {
            return &sfnt->tables[mid];
        }
    }
    return NULL;
}

int robo_font_sfnt_has_table(const robo_font_sfnt_t *sfnt, robo_font_tag_t tag)
{
    return find_rec(sfnt, tag) != NULL;
}

robo_result_t robo_font_sfnt_find_table(const robo_font_sfnt_t *sfnt,
                                        robo_font_tag_t tag,
                                        robo_font_sfnt_table_record_t *out)
{
    const robo_font_sfnt_table_record_t *rec;
    if (!sfnt) {
        return ROBO_INVALID_ARGUMENT;
    }
    rec = find_rec(sfnt, tag);
    if (!rec) {
        return ROBO_NOT_FOUND;
    }
    if (out) {
        *out = *rec;
    }
    return ROBO_OK;
}

robo_result_t robo_font_sfnt_table_bytes(const robo_font_sfnt_t *sfnt,
                                         robo_font_tag_t tag,
                                         robo_font_span_t *out)
{
    const robo_font_sfnt_table_record_t *rec;
    robo_font_span_t parent;
    if (!sfnt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->data = NULL;
    out->size = 0u;
    rec = find_rec(sfnt, tag);
    if (!rec) {
        return ROBO_NOT_FOUND;
    }
    parent.data = robo_font_source_bytes(sfnt->source);
    parent.size = robo_font_source_size(sfnt->source);
    return robo_font_span_slice(parent, rec->offset, rec->length, out);
}

robo_result_t robo_font_sfnt_table_at(const robo_font_sfnt_t *sfnt,
                                      uint32_t index,
                                      robo_font_sfnt_table_record_t *out)
{
    if (!sfnt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (index >= sfnt->table_count) {
        return ROBO_NOT_FOUND;
    }
    *out = sfnt->tables[index];
    return ROBO_OK;
}

robo_result_t robo_font_sfnt_verify_table_checksum(const robo_font_sfnt_t *sfnt,
                                                   robo_font_tag_t tag)
{
    const robo_font_sfnt_table_record_t *rec;
    const uint8_t *bytes;
    uint32_t calc;
    if (!sfnt) {
        return ROBO_INVALID_ARGUMENT;
    }
    rec = find_rec(sfnt, tag);
    if (!rec) {
        return ROBO_NOT_FOUND;
    }
    bytes = robo_font_source_bytes(sfnt->source);
    if (!bytes) {
        return ROBO_PARSE_ERROR;
    }
    calc = robo_font_sfnt_checksum(bytes + rec->offset, (size_t)rec->length);
    if (calc != rec->checksum) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

robo_result_t robo_font_sfnt_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_result_t res;

    if (!data || size == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (size > ROBO_FONT_SFNT_MAX_SOURCE_SIZE) {
        return ROBO_PARSE_ERROR;
    }
    res = robo_font_source_create_memory_borrow(data, size, &src);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_sfnt_parse(src, 0u, ROBO_FONT_SFNT_FLAGS_NONE, &sfnt);
    if (sfnt) {
        robo_font_sfnt_release(sfnt);
    }
    robo_font_source_release(src);
    return res;
}
