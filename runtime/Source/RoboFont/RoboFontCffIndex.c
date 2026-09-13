/*
 * CFF INDEX + offset helpers (Goal 5.5).
 */

#include "RoboFont/RoboFontCff.h"

#include "RoboMem/RoboMem.h"

#include <stdint.h>
#include <string.h>

robo_result_t robo_font_cff_read_offset(robo_font_reader_t *r,
                                        uint8_t off_size,
                                        uint32_t *out)
{
    uint32_t v = 0u;
    uint8_t i;
    uint8_t b;

    if (!r || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (off_size < 1u || off_size > 4u) {
        return ROBO_PARSE_ERROR;
    }
    for (i = 0u; i < off_size; i++) {
        if (robo_font_read_u8(r, &b) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        v = (v << 8) | (uint32_t)b;
    }
    *out = v;
    return ROBO_OK;
}

void robo_font_cff_index_clear(robo_font_cff_index_t *idx)
{
    if (!idx) {
        return;
    }
    robo_free(idx->data_offsets);
    robo_free(idx->data_lengths);
    memset(idx, 0, sizeof(*idx));
}

static robo_result_t index_parse_common(robo_font_span_t table,
                                        size_t offset,
                                        uint32_t count,
                                        size_t after_count,
                                        robo_font_cff_index_t *out,
                                        size_t *out_end)
{
    robo_font_reader_t r;
    uint8_t off_size;
    uint32_t *raw_offs = NULL;
    uint32_t i;
    uint32_t data_size;
    size_t offset_array_bytes;
    size_t data_base;
    size_t end;

    if (!out || !out_end) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_cff_index_clear(out);

    if (count == 0u) {
        out->count = 0u;
        out->table = table;
        out->data_base = after_count;
        *out_end = after_count;
        return ROBO_OK;
    }

    if (count > ROBO_FONT_CFF_MAX_INDEX_COUNT) {
        return ROBO_PARSE_ERROR;
    }

    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, after_count) != ROBO_OK ||
        robo_font_read_u8(&r, &off_size) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (off_size < 1u || off_size > 4u) {
        return ROBO_PARSE_ERROR;
    }

    /* count+1 offsets */
    if ((size_t)(count + 1u) > (SIZE_MAX / (size_t)off_size)) {
        return ROBO_PARSE_ERROR;
    }
    offset_array_bytes = (size_t)(count + 1u) * (size_t)off_size;
    if (robo_font_checked_range(table.size, after_count + 1u, offset_array_bytes) !=
        ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }

    raw_offs = (uint32_t *)robo_calloc(count + 1u, sizeof(uint32_t));
    if (!raw_offs) {
        return ROBO_OUT_OF_MEMORY;
    }

    for (i = 0u; i <= count; i++) {
        if (robo_font_cff_read_offset(&r, off_size, &raw_offs[i]) != ROBO_OK) {
            robo_free(raw_offs);
            return ROBO_PARSE_ERROR;
        }
        if (i > 0u && raw_offs[i] < raw_offs[i - 1u]) {
            robo_free(raw_offs);
            return ROBO_PARSE_ERROR;
        }
        if (raw_offs[i] == 0u) {
            robo_free(raw_offs);
            return ROBO_PARSE_ERROR; /* offsets are 1-based */
        }
    }

    data_size = raw_offs[count] - 1u;
    if (data_size > ROBO_FONT_CFF_MAX_INDEX_OBJECT * (count > 0u ? count : 1u) &&
        data_size > (1u << 24)) {
        /* soft bound: total data region */
        if (data_size > (32u << 20)) {
            robo_free(raw_offs);
            return ROBO_PARSE_ERROR;
        }
    }

    data_base = after_count + 1u + offset_array_bytes;
    if (robo_font_checked_range(table.size, data_base, data_size) != ROBO_OK) {
        robo_free(raw_offs);
        return ROBO_PARSE_ERROR;
    }
    end = data_base + data_size;

    out->data_offsets = (uint32_t *)robo_calloc(count, sizeof(uint32_t));
    out->data_lengths = (uint32_t *)robo_calloc(count, sizeof(uint32_t));
    if (!out->data_offsets || !out->data_lengths) {
        robo_free(raw_offs);
        robo_font_cff_index_clear(out);
        return ROBO_OUT_OF_MEMORY;
    }

    for (i = 0u; i < count; i++) {
        uint32_t start = raw_offs[i] - 1u;
        uint32_t stop = raw_offs[i + 1u] - 1u;
        uint32_t len = stop - start;
        if (len > ROBO_FONT_CFF_MAX_INDEX_OBJECT) {
            robo_free(raw_offs);
            robo_font_cff_index_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->data_offsets[i] = (uint32_t)(data_base + start);
        out->data_lengths[i] = len;
    }

    robo_free(raw_offs);
    out->count = count;
    out->off_size = off_size;
    out->table = table;
    out->data_base = data_base;
    *out_end = end;
    (void)offset;
    return ROBO_OK;
}

robo_result_t robo_font_cff_index_parse(robo_font_span_t table,
                                        size_t offset,
                                        robo_font_cff_index_t *out,
                                        size_t *out_end)
{
    robo_font_reader_t r;
    uint16_t count16;

    if (!out || !out_end) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (offset > table.size || table.size - offset < 2u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u16be(&r, &count16) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    return index_parse_common(table, offset, (uint32_t)count16, offset + 2u, out,
                              out_end);
}

robo_result_t robo_font_cff2_index_parse(robo_font_span_t table,
                                         size_t offset,
                                         robo_font_cff_index_t *out,
                                         size_t *out_end)
{
    robo_font_reader_t r;
    uint32_t count32;

    if (!out || !out_end) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (offset > table.size || table.size - offset < 4u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u32be(&r, &count32) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    return index_parse_common(table, offset, count32, offset + 4u, out, out_end);
}

uint32_t robo_font_cff_index_count(const robo_font_cff_index_t *idx)
{
    return idx ? idx->count : 0u;
}

robo_result_t robo_font_cff_index_get(const robo_font_cff_index_t *idx,
                                      uint32_t i,
                                      robo_font_span_t *out)
{
    if (!idx || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (i >= idx->count) {
        return ROBO_PARSE_ERROR;
    }
    return robo_font_span_slice(idx->table, idx->data_offsets[i],
                                idx->data_lengths[i], out);
}

robo_result_t robo_font_cff_index_get_offset(const robo_font_cff_index_t *idx,
                                             uint32_t i,
                                             uint32_t *out_abs)
{
    if (!idx || !out_abs) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (i >= idx->count) {
        return ROBO_PARSE_ERROR;
    }
    *out_abs = idx->data_offsets[i];
    return ROBO_OK;
}

robo_result_t robo_font_cff_index_get_length(const robo_font_cff_index_t *idx,
                                             uint32_t i,
                                             uint32_t *out_len)
{
    if (!idx || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (i >= idx->count) {
        return ROBO_PARSE_ERROR;
    }
    *out_len = idx->data_lengths[i];
    return ROBO_OK;
}

uint32_t robo_font_cff_subr_bias(uint32_t n_subrs)
{
    if (n_subrs < 1240u) {
        return 107u;
    }
    if (n_subrs < 33900u) {
        return 1131u;
    }
    return 32768u;
}
