/*
 * CFF2 Item Variation Store foundation (Goal 5.5).
 * Structural parse only — no instance evaluation (Goal 5.18).
 */

#include "RoboFont/RoboFontCff.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

void robo_font_cff_vstore_clear(robo_font_cff_vstore_t *v)
{
    uint16_t i;
    if (!v) {
        return;
    }
    if (v->regions) {
        for (i = 0u; i < v->region_count; i++) {
            robo_free(v->regions[i].axes);
        }
        robo_free(v->regions);
    }
    if (v->data) {
        for (i = 0u; i < v->data_count; i++) {
            robo_free(v->data[i].region_indices);
        }
        robo_free(v->data);
    }
    memset(v, 0, sizeof(*v));
}

robo_result_t robo_font_cff_vstore_parse(robo_font_span_t table,
                                         size_t offset,
                                         robo_font_cff_vstore_t *out)
{
    robo_font_reader_t r;
    uint16_t format;
    uint32_t region_list_offset;
    uint16_t data_count;
    uint32_t *data_offsets = NULL;
    size_t region_list_abs;
    uint16_t axis_count;
    uint16_t region_count;
    uint16_t i;
    uint16_t j;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_cff_vstore_clear(out);

    if (offset + 8u > table.size) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u16be(&r, &format) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->format = format;
    if (format != 1u) {
        return ROBO_NOT_SUPPORTED;
    }

    {
        uint32_t tmp;
        if (robo_font_read_u32be(&r, &tmp) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        region_list_offset = tmp;
    }
    if (robo_font_read_u16be(&r, &data_count) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (data_count > ROBO_FONT_CFF_MAX_VAR_REGIONS) {
        return ROBO_PARSE_ERROR;
    }
    out->data_count = data_count;

    if (data_count > 0u) {
        data_offsets = (uint32_t *)robo_calloc(data_count, sizeof(uint32_t));
        if (!data_offsets) {
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < data_count; i++) {
            if (robo_font_read_u32be(&r, &data_offsets[i]) != ROBO_OK) {
                robo_free(data_offsets);
                return ROBO_PARSE_ERROR;
            }
        }
    }

    /* RegionList is relative to VariationStore start */
    region_list_abs = offset + (size_t)region_list_offset;
    if (region_list_abs + 4u > table.size) {
        robo_free(data_offsets);
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_reader_seek(&r, region_list_abs) != ROBO_OK ||
        robo_font_read_u16be(&r, &axis_count) != ROBO_OK ||
        robo_font_read_u16be(&r, &region_count) != ROBO_OK) {
        robo_free(data_offsets);
        return ROBO_PARSE_ERROR;
    }
    out->axis_count = axis_count;
    out->region_count = region_count;
    if (region_count > ROBO_FONT_CFF_MAX_VAR_REGIONS || axis_count > 64u) {
        robo_free(data_offsets);
        return ROBO_PARSE_ERROR;
    }

    if (region_count > 0u) {
        out->regions = (robo_font_cff_var_region_t *)robo_calloc(
            region_count, sizeof(robo_font_cff_var_region_t));
        if (!out->regions) {
            robo_free(data_offsets);
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < region_count; i++) {
            out->regions[i].axis_count = axis_count;
            if (axis_count > 0u) {
                out->regions[i].axes = (robo_font_cff_axis_coord_t *)robo_calloc(
                    axis_count, sizeof(robo_font_cff_axis_coord_t));
                if (!out->regions[i].axes) {
                    robo_free(data_offsets);
                    robo_font_cff_vstore_clear(out);
                    return ROBO_OUT_OF_MEMORY;
                }
                for (j = 0u; j < axis_count; j++) {
                    if (robo_font_read_i16be(&r, &out->regions[i].axes[j].startCoord) !=
                            ROBO_OK ||
                        robo_font_read_i16be(&r, &out->regions[i].axes[j].peakCoord) !=
                            ROBO_OK ||
                        robo_font_read_i16be(&r, &out->regions[i].axes[j].endCoord) !=
                            ROBO_OK) {
                        robo_free(data_offsets);
                        robo_font_cff_vstore_clear(out);
                        return ROBO_PARSE_ERROR;
                    }
                }
            }
        }
    }

    if (data_count > 0u) {
        out->data = (robo_font_cff_var_data_t *)robo_calloc(
            data_count, sizeof(robo_font_cff_var_data_t));
        if (!out->data) {
            robo_free(data_offsets);
            robo_font_cff_vstore_clear(out);
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < data_count; i++) {
            size_t abs = offset + (size_t)data_offsets[i];
            uint16_t item_count;
            uint16_t short_delta_count;
            uint16_t region_index_count;
            size_t delta_bytes;
            if (abs + 6u > table.size) {
                robo_free(data_offsets);
                robo_font_cff_vstore_clear(out);
                return ROBO_PARSE_ERROR;
            }
            if (robo_font_reader_seek(&r, abs) != ROBO_OK ||
                robo_font_read_u16be(&r, &item_count) != ROBO_OK ||
                robo_font_read_u16be(&r, &short_delta_count) != ROBO_OK ||
                robo_font_read_u16be(&r, &region_index_count) != ROBO_OK) {
                robo_free(data_offsets);
                robo_font_cff_vstore_clear(out);
                return ROBO_PARSE_ERROR;
            }
            out->data[i].item_count = item_count;
            out->data[i].short_delta_count = short_delta_count;
            out->data[i].region_index_count = region_index_count;
            if (region_index_count > region_count ||
                short_delta_count > region_index_count) {
                robo_free(data_offsets);
                robo_font_cff_vstore_clear(out);
                return ROBO_PARSE_ERROR;
            }
            if (region_index_count > 0u) {
                out->data[i].region_indices =
                    (uint16_t *)robo_calloc(region_index_count, sizeof(uint16_t));
                if (!out->data[i].region_indices) {
                    robo_free(data_offsets);
                    robo_font_cff_vstore_clear(out);
                    return ROBO_OUT_OF_MEMORY;
                }
                for (j = 0u; j < region_index_count; j++) {
                    if (robo_font_read_u16be(&r, &out->data[i].region_indices[j]) !=
                        ROBO_OK) {
                        robo_free(data_offsets);
                        robo_font_cff_vstore_clear(out);
                        return ROBO_PARSE_ERROR;
                    }
                    if (out->data[i].region_indices[j] >= region_count) {
                        robo_free(data_offsets);
                        robo_font_cff_vstore_clear(out);
                        return ROBO_PARSE_ERROR;
                    }
                }
            }
            /* DeltaSet: per item: shortDeltaCount*int16 + (regionCount-short)*int8 */
            delta_bytes =
                (size_t)item_count *
                ((size_t)short_delta_count * 2u +
                 (size_t)(region_index_count - short_delta_count));
            if (robo_font_span_slice(table, robo_font_reader_pos(&r), delta_bytes,
                                     &out->data[i].delta_sets) != ROBO_OK) {
                robo_free(data_offsets);
                robo_font_cff_vstore_clear(out);
                return ROBO_PARSE_ERROR;
            }
        }
    }

    robo_free(data_offsets);
    return ROBO_OK;
}
