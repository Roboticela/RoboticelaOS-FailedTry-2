/*
 * RoboFontColor — OpenType color fonts (Goal 5.19).
 */

#include "RoboFont/RoboFontColor.h"

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define INFO_COOKIE 0x434C5231u /* 'CLR1' */
#define NODE_NONE 0xFFFFFFFFu

typedef struct robo_font_color_colr_v0_base {
    uint16_t glyph_id;
    uint16_t first_layer;
    uint16_t num_layers;
} robo_font_color_colr_v0_base_t;

typedef struct robo_font_color_colr_v1_base {
    uint16_t glyph_id;
    uint32_t paint_offset;
} robo_font_color_colr_v1_base_t;

typedef struct robo_font_color_sbix_strike {
    uint16_t ppem_x;
    uint16_t ppem_y;
    size_t strike_offset;
} robo_font_color_sbix_strike_t;

typedef struct robo_font_color_cblc_strike {
    uint32_t index_subtable_array_offset;
    uint32_t index_tables_size;
    uint32_t num_index_subtables;
    uint16_t start_glyph;
    uint16_t end_glyph;
    uint16_t ppem_x;
    uint16_t ppem_y;
    uint8_t bit_depth;
    uint8_t flags;
} robo_font_color_cblc_strike_t;

typedef struct robo_font_color_paint_cache {
    robo_glyph_id_t glyph_id;
    uint32_t root_index;
    uint32_t node_count;
    uint8_t building;
    uint8_t has_variation;
    robo_font_color_paint_t *nodes;
} robo_font_color_paint_cache_t;

struct robo_font_color_info {
    uint32_t cookie;
    uint32_t refs;
    uint32_t capabilities;
    uint16_t colr_version;
    uint32_t num_glyphs;

    robo_font_sfnt_t *sfnt;
    const robo_font_variation_coords_t *coords;

    robo_font_span_t cpal_table;
    uint16_t cpal_version;
    uint16_t num_palette_entries;
    uint16_t palette_count;
    uint16_t num_color_records;
    size_t color_records_offset;
    robo_font_color_palette_t *palettes;
    robo_font_color_rgba_t *colors;

    robo_font_span_t colr_table;
    uint16_t colr_num_layers;
    size_t colr_layer_list_offset;
    uint32_t colr_v1_layer_count;
    size_t colr_item_var_store_offset;

    robo_font_color_colr_v0_base_t *colr_v0_bases;
    uint32_t colr_v0_base_count;
    robo_font_color_glyph_layer_t *colr_v0_layers;
    uint32_t colr_v0_layer_count;

    robo_font_color_colr_v1_base_t *colr_v1_bases;
    uint32_t colr_v1_base_count;

    robo_font_span_t sbix_table;
    uint16_t sbix_strike_count;
    robo_font_color_sbix_strike_t *sbix_strikes;

    robo_font_span_t cblc_table;
    robo_font_span_t cbdt_table;
    uint16_t cblc_strike_count;
    robo_font_color_cblc_strike_t *cblc_strikes;

    robo_font_span_t svg_table;
    robo_font_color_glyph_svg_t *svg_docs;
    uint32_t svg_doc_count;

    robo_font_color_paint_cache_t paint_cache[256u];
    uint32_t paint_cache_count;
};

typedef struct robo_font_color_paint_builder {
    robo_font_color_info_t *info;
    robo_font_span_t colr;
    robo_font_color_paint_t *nodes;
    uint32_t node_count;
    uint32_t visited_offsets[ROBO_FONT_COLOR_MAX_PAINT_NODES];
    uint32_t visited_count;
    robo_glyph_id_t colr_glyph_stack[ROBO_FONT_COLOR_MAX_PAINT_DEPTH];
    uint32_t colr_glyph_depth;
    uint8_t depth;
    uint8_t has_variation;
    robo_result_t error;
} robo_font_color_paint_builder_t;

static uint8_t f2dot14_alpha_hi(int16_t alpha)
{
    return (uint8_t)(((uint16_t)alpha >> 8) & 0xFFu);
}

static uint8_t f2dot14_alpha_to_u8(int16_t alpha)
{
    int32_t v = (int32_t)alpha;
    if (v <= 0) {
        return 0u;
    }
    if (v >= 16384) {
        return 255u;
    }
    return (uint8_t)((v * 255 + 8192) / 16384);
}

static robo_result_t read_offset24(robo_font_span_t table, size_t at, size_t *out_abs)
{
    uint32_t off;

    if (!out_abs) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_font_checked_range(table.size, at, 3u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    off = ((uint32_t)table.data[at] << 16) | ((uint32_t)table.data[at + 1u] << 8) |
          (uint32_t)table.data[at + 2u];
    return robo_font_ot_resolve_offset32(table, 0u, off, out_abs);
}

static int svg_has_external_ref_hint(const uint8_t *data, size_t size)
{
    size_t i;
    static const char k_http[] = "http://";
    static const char k_xlink[] = "xlink:href";
    static const char k_href[] = "href=";

    if (!data || size == 0u) {
        return 0;
    }
    for (i = 0u; i + 7u <= size; i++) {
        if (memcmp(data + i, k_http, 7u) == 0) {
            return 1;
        }
    }
    for (i = 0u; i + 10u <= size; i++) {
        if (memcmp(data + i, k_xlink, 10u) == 0) {
            return 1;
        }
    }
    for (i = 0u; i + 5u <= size; i++) {
        if (memcmp(data + i, k_href, 5u) == 0) {
            return 1;
        }
    }
    return 0;
}

static int32_t cmp_u16(uint16_t a, uint16_t b)
{
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static const robo_font_color_colr_v0_base_t *colr_v0_base_find(
    const robo_font_color_info_t *info,
    robo_glyph_id_t gid)
{
    uint32_t lo = 0u;
    uint32_t hi;
    if (!info || gid > 0xFFFFu || info->colr_v0_base_count == 0u) {
        return NULL;
    }
    hi = info->colr_v0_base_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const robo_font_color_colr_v0_base_t *b = &info->colr_v0_bases[mid];
        int32_t c = cmp_u16(b->glyph_id, (uint16_t)gid);
        if (c == 0) {
            return b;
        }
        if (c < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

static const robo_font_color_colr_v1_base_t *colr_v1_base_find(
    const robo_font_color_info_t *info,
    robo_glyph_id_t gid)
{
    uint32_t lo = 0u;
    uint32_t hi;
    if (!info || gid > 0xFFFFu || info->colr_v1_base_count == 0u) {
        return NULL;
    }
    hi = info->colr_v1_base_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const robo_font_color_colr_v1_base_t *b = &info->colr_v1_bases[mid];
        int32_t c = cmp_u16(b->glyph_id, (uint16_t)gid);
        if (c == 0) {
            return b;
        }
        if (c < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

static robo_font_color_paint_cache_t *paint_cache_find(
    robo_font_color_info_t *info,
    robo_glyph_id_t gid)
{
    uint32_t i;
    if (!info) {
        return NULL;
    }
    for (i = 0u; i < info->paint_cache_count; i++) {
        if (info->paint_cache[i].glyph_id == gid) {
            return &info->paint_cache[i];
        }
    }
    return NULL;
}

static robo_font_color_paint_cache_t *paint_cache_alloc(
    robo_font_color_info_t *info,
    robo_glyph_id_t gid)
{
    robo_font_color_paint_cache_t *e;
    if (!info || info->paint_cache_count >= 256u) {
        return NULL;
    }
    e = &info->paint_cache[info->paint_cache_count++];
    memset(e, 0, sizeof(*e));
    e->glyph_id = gid;
    e->root_index = NODE_NONE;
    return e;
}

static void clear_cpal_state(robo_font_color_info_t *info)
{
    if (!info) {
        return;
    }
    robo_free(info->palettes);
    robo_free(info->colors);
    info->palettes = NULL;
    info->colors = NULL;
    info->palette_count = 0u;
    info->num_palette_entries = 0u;
    info->num_color_records = 0u;
    info->color_records_offset = 0u;
    info->cpal_version = 0u;
    memset(&info->cpal_table, 0, sizeof(info->cpal_table));
}

static void clear_colr_state(robo_font_color_info_t *info)
{
    if (!info) {
        return;
    }
    robo_free(info->colr_v0_bases);
    robo_free(info->colr_v0_layers);
    robo_free(info->colr_v1_bases);
    info->colr_v0_bases = NULL;
    info->colr_v0_layers = NULL;
    info->colr_v1_bases = NULL;
    info->colr_v0_base_count = 0u;
    info->colr_v0_layer_count = 0u;
    info->colr_v1_base_count = 0u;
    info->colr_num_layers = 0u;
    info->colr_version = 0u;
    info->colr_layer_list_offset = 0u;
    info->colr_v1_layer_count = 0u;
    info->colr_item_var_store_offset = 0u;
    memset(&info->colr_table, 0, sizeof(info->colr_table));
}

static void info_free_owned(robo_font_color_info_t *info)
{
    uint32_t i;
    if (!info) {
        return;
    }
    robo_free(info->palettes);
    robo_free(info->colors);
    robo_free(info->colr_v0_bases);
    robo_free(info->colr_v0_layers);
    robo_free(info->colr_v1_bases);
    robo_free(info->sbix_strikes);
    robo_free(info->cblc_strikes);
    robo_free(info->svg_docs);
    for (i = 0u; i < info->paint_cache_count; i++) {
        robo_free(info->paint_cache[i].nodes);
    }
    if (info->coords) {
        robo_font_variation_coords_release((robo_font_variation_coords_t *)info->coords);
    }
    if (info->sfnt) {
        robo_font_sfnt_release(info->sfnt);
    }
    memset(info, 0, sizeof(*info));
}

static robo_result_t parse_cpal(robo_font_color_info_t *info, robo_font_span_t span)
{
    robo_font_reader_t r;
    robo_font_cpal_t cpal;
    uint16_t i;
    size_t palette_indices_off = 12u;
    robo_result_t res;

    if (!info) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_font_cpal_parse(span, &cpal);
    if (res != ROBO_OK) {
        return res;
    }
    if (cpal.num_palette > ROBO_FONT_COLOR_MAX_PALETTES ||
        cpal.num_color_records > ROBO_FONT_COLOR_MAX_PALETTE_ENTRIES) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(span.size, palette_indices_off,
                                (size_t)cpal.num_palette * 2u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }

    info->cpal_table = span;
    info->cpal_version = cpal.version;
    info->num_palette_entries = cpal.num_palettes_entries;
    info->palette_count = cpal.num_palette;
    info->num_color_records = cpal.num_color_records;
    info->color_records_offset = cpal.color_records_array_offset;

    if (cpal.num_palette > 0u) {
        info->palettes = (robo_font_color_palette_t *)robo_calloc(
            cpal.num_palette, sizeof(robo_font_color_palette_t));
        if (!info->palettes) {
            return ROBO_OUT_OF_MEMORY;
        }
    }
    if (cpal.num_color_records > 0u) {
        info->colors = (robo_font_color_rgba_t *)robo_calloc(cpal.num_color_records,
                                                             sizeof(robo_font_color_rgba_t));
        if (!info->colors) {
            return ROBO_OUT_OF_MEMORY;
        }
    }

    robo_font_reader_init_span(&r, span);
    for (i = 0u; i < cpal.num_color_records; i++) {
        size_t off = cpal.color_records_array_offset + (size_t)i * 4u;
        robo_font_reader_t cr;
        uint8_t b;
        uint8_t g;
        uint8_t red;
        uint8_t a;
        if (robo_font_checked_range(span.size, off, 4u) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        robo_font_reader_init_span(&cr, span);
        if (robo_font_reader_seek(&cr, off) != ROBO_OK ||
            robo_font_read_u8(&cr, &b) != ROBO_OK || robo_font_read_u8(&cr, &g) != ROBO_OK ||
            robo_font_read_u8(&cr, &red) != ROBO_OK || robo_font_read_u8(&cr, &a) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        info->colors[i].b = b;
        info->colors[i].g = g;
        info->colors[i].r = red;
        info->colors[i].a = a;
    }

    for (i = 0u; i < cpal.num_palette; i++) {
        uint16_t first;
        uint16_t entry_count;
        size_t poff = palette_indices_off + (size_t)i * 2u;
        robo_font_reader_t pr;
        robo_font_reader_init_span(&pr, span);
        if (robo_font_reader_seek(&pr, poff) != ROBO_OK ||
            robo_font_read_u16be(&pr, &first) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (i + 1u < cpal.num_palette) {
            uint16_t next;
            robo_font_reader_t nr;
            robo_font_reader_init_span(&nr, span);
            if (robo_font_reader_seek(&nr, palette_indices_off + (size_t)(i + 1u) * 2u) !=
                    ROBO_OK ||
                robo_font_read_u16be(&nr, &next) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            entry_count = (uint16_t)(next - first);
        } else {
            entry_count = (uint16_t)(cpal.num_color_records - first);
        }
        if (first > cpal.num_color_records || (uint32_t)first + entry_count > cpal.num_color_records) {
            return ROBO_PARSE_ERROR;
        }
        info->palettes[i].index = i;
        info->palettes[i].entry_count = entry_count;
        info->palettes[i].first_color_index = first;
        info->palettes[i].flags = 0u;
        info->palettes[i].name_id = 0xFFFFu;
        if (cpal.version >= 1u) {
            size_t meta_base = palette_indices_off + (size_t)cpal.num_palette * 2u;
            uint32_t types_off = 0u;
            uint32_t labels_off = 0u;
            if (meta_base + 12u <= span.size) {
                robo_font_reader_t mr;
                robo_font_reader_init_span(&mr, span);
                if (robo_font_reader_seek(&mr, meta_base) == ROBO_OK &&
                    robo_font_read_u32be(&mr, &types_off) == ROBO_OK &&
                    robo_font_read_u32be(&mr, &labels_off) == ROBO_OK) {
                    size_t tabs;
                    if (types_off != 0u &&
                        robo_font_ot_resolve_offset32(span, 0u, types_off, &tabs) == ROBO_OK &&
                        robo_font_checked_range(span.size, tabs + (size_t)i * 2u, 2u) == ROBO_OK) {
                        robo_font_reader_t tr;
                        robo_font_reader_init_span(&tr, span);
                        if (robo_font_reader_seek(&tr, tabs + (size_t)i * 2u) == ROBO_OK) {
                            (void)robo_font_read_u16be(&tr, &info->palettes[i].flags);
                        }
                    }
                    if (labels_off != 0u) {
                        size_t labs;
                        if (robo_font_ot_resolve_offset32(span, 0u, labels_off, &labs) == ROBO_OK &&
                            robo_font_checked_range(span.size, labs + (size_t)i * 2u, 2u) == ROBO_OK) {
                            robo_font_reader_t lr;
                            robo_font_reader_init_span(&lr, span);
                            if (robo_font_reader_seek(&lr, labs + (size_t)i * 2u) == ROBO_OK) {
                                (void)robo_font_read_u16be(&lr, &info->palettes[i].name_id);
                            }
                        }
                    }
                }
            }
        }
    }

    info->capabilities |= ROBO_FONT_COLOR_CAP_CPAL;
    return ROBO_OK;
}

static robo_result_t parse_colr_v0(robo_font_color_info_t *info, robo_font_span_t span)
{
    robo_font_colr_t colr;
    robo_result_t res;
    uint32_t i;

    res = robo_font_colr_parse(span, &colr);
    if (res != ROBO_OK || colr.version != 0u) {
        return res != ROBO_OK ? res : ROBO_NOT_SUPPORTED;
    }
    if (colr.num_base_glyph_records > ROBO_FONT_COLOR_MAX_LAYERS ||
        colr.num_layer_records > ROBO_FONT_COLOR_MAX_LAYERS) {
        return ROBO_PARSE_ERROR;
    }

    info->colr_table = span;
    info->colr_version = colr.version;
    info->colr_num_layers = colr.num_layer_records;

    if (colr.num_base_glyph_records > 0u) {
        info->colr_v0_bases = (robo_font_color_colr_v0_base_t *)robo_calloc(
            colr.num_base_glyph_records, sizeof(robo_font_color_colr_v0_base_t));
        if (!info->colr_v0_bases) {
            return ROBO_OUT_OF_MEMORY;
        }
        info->colr_v0_base_count = colr.num_base_glyph_records;
    }
    if (colr.num_layer_records > 0u) {
        info->colr_v0_layers = (robo_font_color_glyph_layer_t *)robo_calloc(
            colr.num_layer_records, sizeof(robo_font_color_glyph_layer_t));
        if (!info->colr_v0_layers) {
            return ROBO_OUT_OF_MEMORY;
        }
        info->colr_v0_layer_count = colr.num_layer_records;
    }

    for (i = 0u; i < colr.num_base_glyph_records; i++) {
        size_t off = colr.base_glyph_records_offset + (size_t)i * 6u;
        robo_font_reader_t br;
        uint16_t gid;
        uint16_t first;
        uint16_t count;
        robo_font_reader_init_span(&br, span);
        if (robo_font_reader_seek(&br, off) != ROBO_OK ||
            robo_font_read_u16be(&br, &gid) != ROBO_OK ||
            robo_font_read_u16be(&br, &first) != ROBO_OK ||
            robo_font_read_u16be(&br, &count) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if ((uint32_t)first + count > colr.num_layer_records) {
            return ROBO_PARSE_ERROR;
        }
        info->colr_v0_bases[i].glyph_id = gid;
        info->colr_v0_bases[i].first_layer = first;
        info->colr_v0_bases[i].num_layers = count;
    }

    for (i = 0u; i < colr.num_layer_records; i++) {
        size_t off = colr.layer_records_offset + (size_t)i * 4u;
        robo_font_reader_t lr;
        uint16_t gid;
        uint16_t pal;
        robo_font_reader_init_span(&lr, span);
        if (robo_font_reader_seek(&lr, off) != ROBO_OK ||
            robo_font_read_u16be(&lr, &gid) != ROBO_OK ||
            robo_font_read_u16be(&lr, &pal) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        info->colr_v0_layers[i].glyph_id = gid;
        info->colr_v0_layers[i].palette_index = pal;
    }

    info->capabilities |= ROBO_FONT_COLOR_CAP_COLR | ROBO_FONT_COLOR_CAP_COLR_V0;
    return ROBO_OK;
}

static robo_result_t parse_colr_v1(robo_font_color_info_t *info, robo_font_span_t span)
{
    robo_font_reader_t r;
    uint16_t version;
    uint16_t num_base;
    uint32_t base_off;
    uint32_t layer_off;
    uint16_t num_layer;
    uint32_t base_paint_off;
    uint32_t layer_list_off;
    uint32_t clip_list_off;
    uint32_t var_map_off;
    uint32_t var_store_off;
    size_t abs;
    uint32_t i;

    if (!info || span.size < 34u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &version) != ROBO_OK ||
        robo_font_read_u16be(&r, &num_base) != ROBO_OK ||
        robo_font_read_u32be(&r, &base_off) != ROBO_OK ||
        robo_font_read_u32be(&r, &layer_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &num_layer) != ROBO_OK ||
        robo_font_read_u32be(&r, &base_paint_off) != ROBO_OK ||
        robo_font_read_u32be(&r, &layer_list_off) != ROBO_OK ||
        robo_font_read_u32be(&r, &clip_list_off) != ROBO_OK ||
        robo_font_read_u32be(&r, &var_map_off) != ROBO_OK ||
        robo_font_read_u32be(&r, &var_store_off) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (version != 1u) {
        return ROBO_NOT_SUPPORTED;
    }

    info->colr_table = span;
    info->colr_version = version;
    info->colr_num_layers = num_layer;
    if (var_store_off != 0u) {
        info->capabilities |= ROBO_FONT_COLOR_CAP_VARIATION;
        if (robo_font_ot_resolve_offset32(span, 0u, var_store_off, &abs) == ROBO_OK) {
            info->colr_item_var_store_offset = abs;
        }
    }

    if (layer_list_off != 0u &&
        robo_font_ot_resolve_offset32(span, 0u, layer_list_off, &abs) == ROBO_OK &&
        abs + 4u <= span.size) {
        robo_font_reader_t lr;
        info->colr_layer_list_offset = abs;
        robo_font_reader_init_span(&lr, span);
        if (robo_font_reader_seek(&lr, abs) == ROBO_OK &&
            robo_font_read_u32be(&lr, &info->colr_v1_layer_count) == ROBO_OK) {
            if (info->colr_v1_layer_count > ROBO_FONT_COLOR_MAX_LAYERS) {
                return ROBO_PARSE_ERROR;
            }
        }
    }

    if (base_paint_off != 0u &&
        robo_font_ot_resolve_offset32(span, 0u, base_paint_off, &abs) == ROBO_OK) {
        if (robo_font_checked_range(span.size, abs, (size_t)num_base * 5u) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (num_base > 0u) {
            info->colr_v1_bases = (robo_font_color_colr_v1_base_t *)robo_calloc(
                num_base, sizeof(robo_font_color_colr_v1_base_t));
            if (!info->colr_v1_bases) {
                return ROBO_OUT_OF_MEMORY;
            }
            info->colr_v1_base_count = num_base;
        }
        for (i = 0u; i < num_base; i++) {
            size_t rec = abs + (size_t)i * 5u;
            robo_font_reader_t br;
            uint16_t gid;
            size_t paint_abs;
            robo_font_reader_init_span(&br, span);
            if (robo_font_reader_seek(&br, rec) != ROBO_OK ||
                robo_font_read_u16be(&br, &gid) != ROBO_OK ||
                read_offset24(span, rec + 2u, &paint_abs) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            info->colr_v1_bases[i].glyph_id = gid;
            info->colr_v1_bases[i].paint_offset = (uint32_t)paint_abs;
        }
    }

    (void)base_off;
    (void)layer_off;
    (void)clip_list_off;
    (void)var_map_off;
    info->capabilities |= ROBO_FONT_COLOR_CAP_COLR | ROBO_FONT_COLOR_CAP_COLR_V1;
    return ROBO_OK;
}

static robo_result_t parse_sbix(robo_font_color_info_t *info, robo_font_span_t span)
{
    robo_font_reader_t r;
    uint16_t version;
    uint16_t flags;
    uint32_t num_strikes;
    uint32_t i;

    (void)flags;
    if (!info || info->num_glyphs == 0u) {
        return ROBO_PARSE_ERROR;
    }
    if (span.size < 8u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &version) != ROBO_OK ||
        robo_font_read_u16be(&r, &flags) != ROBO_OK ||
        robo_font_read_u32be(&r, &num_strikes) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (version != 1u || num_strikes == 0u || num_strikes > ROBO_FONT_COLOR_MAX_STRIKES) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(span.size, 8u, (size_t)num_strikes * 4u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }

    info->sbix_table = span;
    info->sbix_strikes = (robo_font_color_sbix_strike_t *)robo_calloc(
        num_strikes, sizeof(robo_font_color_sbix_strike_t));
    if (!info->sbix_strikes) {
        return ROBO_OUT_OF_MEMORY;
    }
    info->sbix_strike_count = (uint16_t)num_strikes;

    for (i = 0u; i < num_strikes; i++) {
        uint32_t strike_off;
        size_t soff = 8u + (size_t)i * 4u;
        robo_font_reader_t sr;
        uint16_t ppem_x;
        uint16_t ppem_y;
        uint32_t data_off;
        size_t glyph_array_bytes;
        if (robo_font_checked_range(span.size, soff, 4u) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        robo_font_reader_init_span(&sr, span);
        if (robo_font_reader_seek(&sr, soff) != ROBO_OK ||
            robo_font_read_u32be(&sr, &strike_off) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (strike_off == 0u || strike_off + 8u > span.size) {
            return ROBO_PARSE_ERROR;
        }
        robo_font_reader_init_span(&sr, span);
        if (robo_font_reader_seek(&sr, strike_off) != ROBO_OK ||
            robo_font_read_u16be(&sr, &ppem_x) != ROBO_OK ||
            robo_font_read_u16be(&sr, &ppem_y) != ROBO_OK ||
            robo_font_read_u32be(&sr, &data_off) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (data_off == 0u || data_off + 4u > span.size) {
            return ROBO_PARSE_ERROR;
        }
        glyph_array_bytes = ((size_t)info->num_glyphs + 1u) * 4u;
        if (robo_font_checked_range(span.size, data_off, glyph_array_bytes) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        info->sbix_strikes[i].ppem_x = ppem_x;
        info->sbix_strikes[i].ppem_y = ppem_y ? ppem_y : ppem_x;
        info->sbix_strikes[i].strike_offset = strike_off;
    }

    info->capabilities |= ROBO_FONT_COLOR_CAP_SBIX;
    return ROBO_OK;
}

static robo_result_t parse_cblc(robo_font_color_info_t *info,
                                  robo_font_span_t cblc,
                                  robo_font_span_t cbdt)
{
    robo_font_reader_t r;
    uint16_t major;
    uint16_t minor;
    uint32_t num_sizes;
    uint32_t i;

    if (!info || cblc.size < 8u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, cblc);
    if (robo_font_read_u16be(&r, &major) != ROBO_OK ||
        robo_font_read_u16be(&r, &minor) != ROBO_OK ||
        robo_font_read_u32be(&r, &num_sizes) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (major != 2u && major != 3u) {
        return ROBO_NOT_SUPPORTED;
    }
    (void)minor;
    if (num_sizes == 0u || num_sizes > ROBO_FONT_COLOR_MAX_STRIKES) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(cblc.size, 8u, (size_t)num_sizes * 48u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }

    info->cblc_table = cblc;
    info->cbdt_table = cbdt;
    info->cblc_strikes = (robo_font_color_cblc_strike_t *)robo_calloc(
        num_sizes, sizeof(robo_font_color_cblc_strike_t));
    if (!info->cblc_strikes) {
        return ROBO_OUT_OF_MEMORY;
    }
    info->cblc_strike_count = (uint16_t)num_sizes;

    for (i = 0u; i < num_sizes; i++) {
        size_t off = 8u + (size_t)i * 48u;
        robo_font_reader_t sr;
        uint32_t index_array_off;
        uint32_t index_tables_size;
        uint32_t num_subtables;
        uint32_t color_ref;
        uint16_t start_glyph;
        uint16_t end_glyph;
        uint8_t ppem_x;
        uint8_t ppem_y;
        uint8_t bit_depth;
        uint8_t flags;
        (void)color_ref;
        robo_font_reader_init_span(&sr, cblc);
        if (robo_font_reader_seek(&sr, off) != ROBO_OK ||
            robo_font_read_u32be(&sr, &index_array_off) != ROBO_OK ||
            robo_font_read_u32be(&sr, &index_tables_size) != ROBO_OK ||
            robo_font_read_u32be(&sr, &num_subtables) != ROBO_OK ||
            robo_font_read_u32be(&sr, &color_ref) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_reader_seek(&sr, off + 32u) != ROBO_OK ||
            robo_font_read_u16be(&sr, &start_glyph) != ROBO_OK ||
            robo_font_read_u16be(&sr, &end_glyph) != ROBO_OK ||
            robo_font_read_u8(&sr, &ppem_x) != ROBO_OK ||
            robo_font_read_u8(&sr, &ppem_y) != ROBO_OK ||
            robo_font_read_u8(&sr, &bit_depth) != ROBO_OK ||
            robo_font_read_u8(&sr, &flags) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (index_array_off == 0u) {
            return ROBO_PARSE_ERROR;
        }
        {
            size_t index_abs;
            if (robo_font_ot_resolve_offset32(cblc, 0u, index_array_off, &index_abs) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            info->cblc_strikes[i].index_subtable_array_offset = (uint32_t)index_abs;
        }
        info->cblc_strikes[i].index_tables_size = index_tables_size;
        info->cblc_strikes[i].num_index_subtables = num_subtables;
        info->cblc_strikes[i].start_glyph = start_glyph;
        info->cblc_strikes[i].end_glyph = end_glyph;
        info->cblc_strikes[i].ppem_x = ppem_x;
        info->cblc_strikes[i].ppem_y = ppem_y ? ppem_y : ppem_x;
        info->cblc_strikes[i].bit_depth = bit_depth;
        info->cblc_strikes[i].flags = flags;
    }

    info->capabilities |= ROBO_FONT_COLOR_CAP_CBLC;
    if (cbdt.data && cbdt.size > 0u) {
        info->capabilities |= ROBO_FONT_COLOR_CAP_CBDT;
    }
    return ROBO_OK;
}

static robo_result_t parse_svg(robo_font_color_info_t *info, robo_font_span_t span)
{
    robo_font_reader_t r;
    uint16_t version;
    uint32_t doc_list_off;
    size_t abs;
    uint16_t num_entries;
    uint32_t i;

    if (!info || span.size < 6u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &version) != ROBO_OK ||
        robo_font_read_u32be(&r, &doc_list_off) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (version != 0u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (doc_list_off == 0u ||
        robo_font_ot_resolve_offset32(span, 0u, doc_list_off, &abs) != ROBO_OK ||
        abs + 2u > span.size) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_reader_seek(&r, abs) != ROBO_OK ||
        robo_font_read_u16be(&r, &num_entries) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (num_entries == 0u) {
        return ROBO_PARSE_ERROR;
    }
    if (num_entries > ROBO_FONT_COLOR_MAX_SVG_DOCS) {
        num_entries = (uint16_t)ROBO_FONT_COLOR_MAX_SVG_DOCS;
    }
    if (robo_font_checked_range(span.size, abs + 2u, (size_t)num_entries * 12u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }

    info->svg_table = span;
    info->svg_docs = (robo_font_color_glyph_svg_t *)robo_calloc(
        num_entries, sizeof(robo_font_color_glyph_svg_t));
    if (!info->svg_docs) {
        return ROBO_OUT_OF_MEMORY;
    }
    info->svg_doc_count = num_entries;

    for (i = 0u; i < num_entries; i++) {
        size_t rec = abs + 2u + (size_t)i * 12u;
        robo_font_reader_t dr;
        uint16_t start_gid;
        uint16_t end_gid;
        uint32_t payload_off;
        uint32_t payload_len_u32;
        size_t payload_abs;
        size_t payload_len;
        robo_font_reader_init_span(&dr, span);
        if (robo_font_reader_seek(&dr, rec) != ROBO_OK ||
            robo_font_read_u16be(&dr, &start_gid) != ROBO_OK ||
            robo_font_read_u16be(&dr, &end_gid) != ROBO_OK ||
            robo_font_read_u32be(&dr, &payload_off) != ROBO_OK ||
            robo_font_read_u32be(&dr, &payload_len_u32) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (end_gid < start_gid) {
            return ROBO_PARSE_ERROR;
        }
        if (payload_off == 0u || payload_len_u32 == 0u ||
            robo_font_ot_resolve_offset32(span, abs, payload_off, &payload_abs) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        payload_len = (size_t)payload_len_u32;
        if (payload_len > ROBO_FONT_COLOR_MAX_SVG_PAYLOAD) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_checked_range(span.size, payload_abs, payload_len) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        info->svg_docs[i].start_glyph_id = start_gid;
        info->svg_docs[i].end_glyph_id = end_gid;
        info->svg_docs[i].document.data = span.data + payload_abs;
        info->svg_docs[i].document.size = payload_len;
        info->svg_docs[i].has_external_ref_hint =
            (uint8_t)svg_has_external_ref_hint(span.data + payload_abs, payload_len);
    }

    info->capabilities |= ROBO_FONT_COLOR_CAP_SVG;
    return ROBO_OK;
}

static int paint_offset_seen(robo_font_color_paint_builder_t *b, uint32_t off)
{
    uint32_t i;
    for (i = 0u; i < b->visited_count; i++) {
        if (b->visited_offsets[i] == off) {
            return 1;
        }
    }
    return 0;
}

static void paint_offset_mark(robo_font_color_paint_builder_t *b, uint32_t off)
{
    if (b->visited_count < ROBO_FONT_COLOR_MAX_PAINT_NODES) {
        b->visited_offsets[b->visited_count++] = off;
    }
}

static int colr_glyph_visiting(robo_font_color_paint_builder_t *b, robo_glyph_id_t gid)
{
    uint32_t i;
    for (i = 0u; i < b->colr_glyph_depth; i++) {
        if (b->colr_glyph_stack[i] == gid) {
            return 1;
        }
    }
    return 0;
}

static void colr_glyph_push(robo_font_color_paint_builder_t *b, robo_glyph_id_t gid)
{
    if (b->colr_glyph_depth < ROBO_FONT_COLOR_MAX_PAINT_DEPTH) {
        b->colr_glyph_stack[b->colr_glyph_depth++] = gid;
    }
}

static void colr_glyph_pop(robo_font_color_paint_builder_t *b)
{
    if (b->colr_glyph_depth > 0u) {
        b->colr_glyph_depth--;
    }
}

static uint32_t paint_append(robo_font_color_paint_builder_t *b, robo_font_color_paint_t *n)
{
    if (b->error != ROBO_OK || b->node_count >= ROBO_FONT_COLOR_MAX_PAINT_NODES) {
        b->error = ROBO_NOT_SUPPORTED;
        return NODE_NONE;
    }
    b->nodes[b->node_count] = *n;
    return b->node_count++;
}

static robo_result_t paint_validate_colorline(robo_font_span_t colr, size_t at)
{
    size_t abs;
    uint16_t extend_mode;
    robo_font_reader_t r;
    if (read_offset24(colr, at, &abs) != ROBO_OK || abs + 2u > colr.size) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, colr);
    if (robo_font_reader_seek(&r, abs) != ROBO_OK ||
        robo_font_read_u16be(&r, &extend_mode) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    (void)extend_mode;
    return ROBO_OK;
}

static robo_result_t paint_expand_at(robo_font_color_paint_builder_t *b,
                                     uint32_t paint_off,
                                     uint32_t *out_first_index);

static robo_result_t paint_expand_colr_layers(robo_font_color_paint_builder_t *b,
                                              size_t paint_abs,
                                              uint8_t depth,
                                              uint32_t *out_first_index)
{
    robo_font_reader_t r;
    uint8_t num_layers;
    uint32_t first_layer = 0u;
    uint32_t i;
    robo_font_color_paint_t head;
    uint32_t head_index;
    uint32_t prev = NODE_NONE;
    uint32_t first_child = NODE_NONE;

    if (b->depth >= ROBO_FONT_COLOR_MAX_PAINT_DEPTH) {
        b->error = ROBO_NOT_SUPPORTED;
        return ROBO_NOT_SUPPORTED;
    }
    robo_font_reader_init_span(&r, b->colr);
    if (robo_font_reader_seek(&r, paint_abs) != ROBO_OK ||
        robo_font_read_u8(&r, &head.format) != ROBO_OK ||
        robo_font_read_u8(&r, &num_layers) != ROBO_OK) {
        b->error = ROBO_PARSE_ERROR;
        return ROBO_PARSE_ERROR;
    }
    {
        size_t first_layer_abs = paint_abs + 2u;
        uint32_t layer_index = 0u;
        if (robo_font_checked_range(b->colr.size, first_layer_abs, 3u) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        layer_index = ((uint32_t)b->colr.data[first_layer_abs] << 16) |
                      ((uint32_t)b->colr.data[first_layer_abs + 1u] << 8) |
                      (uint32_t)b->colr.data[first_layer_abs + 2u];
        first_layer = layer_index;
    }
    (void)head.format;
    if (b->info->colr_layer_list_offset == 0u ||
        first_layer + num_layers > b->info->colr_v1_layer_count) {
        b->error = ROBO_PARSE_ERROR;
        return ROBO_PARSE_ERROR;
    }

    memset(&head, 0, sizeof(head));
    head.op = ROBO_FONT_COLOR_PAINT_COLR_LAYERS;
    head.format = 1u;
    head.depth = depth;
    head.child_index = NODE_NONE;
    head.sibling_index = NODE_NONE;
    head_index = paint_append(b, &head);
    if (head_index == NODE_NONE) {
        return b->error;
    }
    first_child = head_index;

    for (i = 0u; i < num_layers; i++) {
        size_t layer_rec =
            b->info->colr_layer_list_offset + 4u + (size_t)(first_layer + i) * 3u;
        size_t child_abs;
        uint32_t child_index;
        if (read_offset24(b->colr, layer_rec, &child_abs) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        b->depth++;
        if (paint_expand_at(b, (uint32_t)child_abs, &child_index) != ROBO_OK) {
            b->depth--;
            return b->error;
        }
        b->depth--;
        if (child_index != NODE_NONE) {
            if (prev != NODE_NONE) {
                b->nodes[prev].sibling_index = child_index;
            } else {
                b->nodes[head_index].child_index = child_index;
            }
            prev = child_index;
        }
    }

    if (out_first_index) {
        *out_first_index = first_child;
    }
    return ROBO_OK;
}

static robo_result_t paint_expand_at(robo_font_color_paint_builder_t *b,
                                     uint32_t paint_off,
                                     uint32_t *out_first_index)
{
    robo_font_color_paint_t node;
    robo_font_reader_t r;
    uint8_t format;
    size_t paint_abs = paint_off;
    uint32_t node_index;
    uint32_t child_index = NODE_NONE;
    uint32_t sibling_index = NODE_NONE;
    size_t child_abs = 0u;
    robo_result_t res;

    if (b->error != ROBO_OK) {
        return b->error;
    }
    if (paint_abs >= b->colr.size) {
        b->error = ROBO_PARSE_ERROR;
        return ROBO_PARSE_ERROR;
    }
    if (paint_offset_seen(b, (uint32_t)paint_abs)) {
        b->error = ROBO_PARSE_ERROR;
        return ROBO_PARSE_ERROR;
    }
    paint_offset_mark(b, (uint32_t)paint_abs);

    robo_font_reader_init_span(&r, b->colr);
    if (robo_font_reader_seek(&r, paint_abs) != ROBO_OK ||
        robo_font_read_u8(&r, &format) != ROBO_OK) {
        b->error = ROBO_PARSE_ERROR;
        return ROBO_PARSE_ERROR;
    }

    memset(&node, 0, sizeof(node));
    node.format = format;
    node.depth = b->depth;
    node.child_index = NODE_NONE;
    node.sibling_index = NODE_NONE;

    switch (format) {
    case 1u:
        return paint_expand_colr_layers(b, paint_abs, b->depth, out_first_index);

    case 2u:
    case 3u:
        node.op = ROBO_FONT_COLOR_PAINT_SOLID;
        if (robo_font_read_u16be(&r, &node.palette_index) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        {
            int16_t alpha;
            if (robo_font_read_i16be(&r, &alpha) != ROBO_OK) {
                b->error = ROBO_PARSE_ERROR;
                return ROBO_PARSE_ERROR;
            }
            node.alpha_f2dot14_hi = f2dot14_alpha_hi(alpha);
            node.alpha_u8 = f2dot14_alpha_to_u8(alpha);
        }
        if (format == 3u) {
            node.has_variation = 1u;
            b->has_variation = 1u;
        }
        break;

    case 4u:
    case 6u:
    case 8u:
        node.op = (format == 4u)   ? ROBO_FONT_COLOR_PAINT_LINEAR_GRADIENT
                  : (format == 6u) ? ROBO_FONT_COLOR_PAINT_RADIAL_GRADIENT
                                   : ROBO_FONT_COLOR_PAINT_SWEEP_GRADIENT;
        if (paint_validate_colorline(b->colr, paint_abs + 1u) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        break;
    case 5u:
    case 7u:
    case 9u:
        node.op = (format == 5u)   ? ROBO_FONT_COLOR_PAINT_LINEAR_GRADIENT
                  : (format == 7u) ? ROBO_FONT_COLOR_PAINT_RADIAL_GRADIENT
                                   : ROBO_FONT_COLOR_PAINT_SWEEP_GRADIENT;
        node.has_variation = 1u;
        b->has_variation = 1u;
        if (paint_validate_colorline(b->colr, paint_abs + 1u) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        break;

    case 10u: {
        size_t child_abs;
        uint16_t gid16;
        node.op = ROBO_FONT_COLOR_PAINT_GLYPH;
        if (read_offset24(b->colr, paint_abs + 1u, &child_abs) != ROBO_OK ||
            robo_font_read_u16be(&r, &gid16) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        node.glyph_id = gid16;
        b->depth++;
        res = paint_expand_at(b, (uint32_t)child_abs, &child_index);
        b->depth--;
        if (res != ROBO_OK) {
            return res;
        }
        node.child_index = child_index;
        break;
    }

    case 11u: {
        const robo_font_color_colr_v1_base_t *base;
        uint16_t gid16;
        node.op = ROBO_FONT_COLOR_PAINT_COLR_GLYPH;
        if (robo_font_read_u16be(&r, &gid16) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        node.glyph_id = gid16;
        if (colr_glyph_visiting(b, node.glyph_id) ||
            b->depth >= ROBO_FONT_COLOR_MAX_PAINT_DEPTH) {
            node.unsupported = 1u;
            node.op = ROBO_FONT_COLOR_PAINT_UNSUPPORTED;
            break;
        }
        base = colr_v1_base_find(b->info, node.glyph_id);
        if (!base) {
            node.unsupported = 1u;
            node.op = ROBO_FONT_COLOR_PAINT_UNSUPPORTED;
            break;
        }
        colr_glyph_push(b, node.glyph_id);
        b->depth++;
        res = paint_expand_at(b, base->paint_offset, &child_index);
        b->depth--;
        colr_glyph_pop(b);
        if (res != ROBO_OK) {
            return res;
        }
        node.child_index = child_index;
        break;
    }

    case 12u:
    case 13u: {
        size_t child_abs;
        node.op = ROBO_FONT_COLOR_PAINT_TRANSFORM;
        if (read_offset24(b->colr, paint_abs + 1u, &child_abs) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        if (format == 13u) {
            node.has_variation = 1u;
            b->has_variation = 1u;
        }
        b->depth++;
        res = paint_expand_at(b, (uint32_t)child_abs, &child_index);
        b->depth--;
        if (res != ROBO_OK) {
            return res;
        }
        node.child_index = child_index;
        break;
    }

    case 14u:
    case 15u:
        node.op = ROBO_FONT_COLOR_PAINT_TRANSLATE;
        if (format == 15u) {
            node.has_variation = 1u;
            b->has_variation = 1u;
        }
        if (read_offset24(b->colr, paint_abs + 1u, &child_abs) != ROBO_OK ||
            robo_font_read_i16be(&r, &node.tx) != ROBO_OK ||
            robo_font_read_i16be(&r, &node.ty) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        b->depth++;
        res = paint_expand_at(b, (uint32_t)child_abs, &child_index);
        b->depth--;
        if (res != ROBO_OK) {
            return res;
        }
        node.child_index = child_index;
        break;

    case 16u:
    case 17u:
        node.op = ROBO_FONT_COLOR_PAINT_SCALE;
        if (format == 17u) {
            node.has_variation = 1u;
            b->has_variation = 1u;
        }
        if (read_offset24(b->colr, paint_abs + 1u, &child_abs) != ROBO_OK ||
            robo_font_read_i16be(&r, &node.xx) != ROBO_OK ||
            robo_font_read_i16be(&r, &node.yy) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        b->depth++;
        res = paint_expand_at(b, (uint32_t)child_abs, &child_index);
        b->depth--;
        if (res != ROBO_OK) {
            return res;
        }
        node.child_index = child_index;
        break;

    case 24u:
    case 25u:
        node.op = ROBO_FONT_COLOR_PAINT_ROTATE;
        if (format == 25u) {
            node.has_variation = 1u;
            b->has_variation = 1u;
        }
        if (read_offset24(b->colr, paint_abs + 1u, &child_abs) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        b->depth++;
        res = paint_expand_at(b, (uint32_t)child_abs, &child_index);
        b->depth--;
        if (res != ROBO_OK) {
            return res;
        }
        node.child_index = child_index;
        break;

    case 28u:
    case 29u:
        node.op = ROBO_FONT_COLOR_PAINT_SKEW;
        if (format == 29u) {
            node.has_variation = 1u;
            b->has_variation = 1u;
        }
        if (read_offset24(b->colr, paint_abs + 1u, &child_abs) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        b->depth++;
        res = paint_expand_at(b, (uint32_t)child_abs, &child_index);
        b->depth--;
        if (res != ROBO_OK) {
            return res;
        }
        node.child_index = child_index;
        break;

    case 32u: {
        size_t backdrop_abs;
        size_t source_abs;
        node.op = ROBO_FONT_COLOR_PAINT_COMPOSITE;
        if (read_offset24(b->colr, paint_abs + 1u, &backdrop_abs) != ROBO_OK ||
            read_offset24(b->colr, paint_abs + 4u, &source_abs) != ROBO_OK ||
            robo_font_read_u8(&r, &node.composite_mode) != ROBO_OK) {
            b->error = ROBO_PARSE_ERROR;
            return ROBO_PARSE_ERROR;
        }
        b->depth++;
        res = paint_expand_at(b, (uint32_t)backdrop_abs, &child_index);
        if (res == ROBO_OK) {
            res = paint_expand_at(b, (uint32_t)source_abs, &sibling_index);
        }
        b->depth--;
        if (res != ROBO_OK) {
            return res;
        }
        node.child_index = child_index;
        node.sibling_index = sibling_index;
        break;
    }

    default:
        node.op = ROBO_FONT_COLOR_PAINT_UNSUPPORTED;
        node.unsupported = 1u;
        break;
    }

    node_index = paint_append(b, &node);
    if (node_index == NODE_NONE) {
        return b->error;
    }
    if (out_first_index) {
        *out_first_index = node_index;
    }
    return ROBO_OK;
}

static robo_result_t paint_build_for_glyph(robo_font_color_info_t *info,
                                           robo_glyph_id_t gid,
                                           robo_font_color_paint_cache_t **out_cache)
{
    const robo_font_color_colr_v1_base_t *base;
    robo_font_color_paint_builder_t builder;
    robo_font_color_paint_cache_t *cache;
    uint32_t root = NODE_NONE;

    if (!info || !out_cache) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_cache = NULL;
    base = colr_v1_base_find(info, gid);
    if (!base || info->colr_table.size == 0u) {
        return ROBO_NOT_FOUND;
    }

    cache = paint_cache_find(info, gid);
    if (cache) {
        if (cache->building) {
            return ROBO_BUSY;
        }
        if (cache->nodes) {
            *out_cache = cache;
            return ROBO_OK;
        }
    } else {
        cache = paint_cache_alloc(info, gid);
        if (!cache) {
            return ROBO_OUT_OF_MEMORY;
        }
    }

    cache->building = 1u;
    memset(&builder, 0, sizeof(builder));
    builder.info = info;
    builder.colr = info->colr_table;
    builder.nodes = (robo_font_color_paint_t *)robo_calloc(
        ROBO_FONT_COLOR_MAX_PAINT_NODES, sizeof(robo_font_color_paint_t));
    if (!builder.nodes) {
        cache->building = 0u;
        return ROBO_OUT_OF_MEMORY;
    }
    builder.error = ROBO_OK;
    if (paint_expand_at(&builder, base->paint_offset, &root) != ROBO_OK || root == NODE_NONE) {
        robo_free(builder.nodes);
        cache->building = 0u;
        return builder.error != ROBO_OK ? builder.error : ROBO_NOT_SUPPORTED;
    }

    cache->nodes = (robo_font_color_paint_t *)robo_realloc(
        builder.nodes, (size_t)builder.node_count * sizeof(robo_font_color_paint_t));
    if (!cache->nodes) {
        robo_free(builder.nodes);
        cache->building = 0u;
        return ROBO_OUT_OF_MEMORY;
    }
    cache->root_index = root;
    cache->node_count = builder.node_count;
    cache->has_variation = builder.has_variation;
    cache->building = 0u;
    *out_cache = cache;
    return ROBO_OK;
}

static robo_result_t colr_v1_load_v0_layers(robo_font_color_info_t *info, robo_font_span_t span)
{
    robo_font_reader_t r;
    uint16_t version;
    uint16_t num_base;
    uint32_t base_off;
    uint32_t layer_off;
    uint16_t num_layer;
    size_t abs;
    uint32_t i;

    if (!info || span.size < 14u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &version) != ROBO_OK ||
        robo_font_read_u16be(&r, &num_base) != ROBO_OK ||
        robo_font_read_u32be(&r, &base_off) != ROBO_OK ||
        robo_font_read_u32be(&r, &layer_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &num_layer) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    (void)version;
    if (num_base > ROBO_FONT_COLOR_MAX_LAYERS || num_layer > ROBO_FONT_COLOR_MAX_LAYERS) {
        return ROBO_PARSE_ERROR;
    }
    if (num_base > 0u && !info->colr_v0_bases) {
        info->colr_v0_bases = (robo_font_color_colr_v0_base_t *)robo_calloc(
            num_base, sizeof(robo_font_color_colr_v0_base_t));
        if (!info->colr_v0_bases) {
            return ROBO_OUT_OF_MEMORY;
        }
        info->colr_v0_base_count = num_base;
    }
    if (num_layer > 0u && !info->colr_v0_layers) {
        info->colr_v0_layers = (robo_font_color_glyph_layer_t *)robo_calloc(
            num_layer, sizeof(robo_font_color_glyph_layer_t));
        if (!info->colr_v0_layers) {
            return ROBO_OUT_OF_MEMORY;
        }
        info->colr_v0_layer_count = num_layer;
        info->colr_num_layers = num_layer;
    }
    if (base_off != 0u && num_base > 0u &&
        robo_font_ot_resolve_offset32(span, 0u, base_off, &abs) == ROBO_OK) {
        for (i = 0u; i < num_base; i++) {
            size_t off = abs + (size_t)i * 6u;
            robo_font_reader_t br;
            uint16_t gid;
            uint16_t first;
            uint16_t count;
            robo_font_reader_init_span(&br, span);
            if (robo_font_reader_seek(&br, off) != ROBO_OK ||
                robo_font_read_u16be(&br, &gid) != ROBO_OK ||
                robo_font_read_u16be(&br, &first) != ROBO_OK ||
                robo_font_read_u16be(&br, &count) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            if ((uint32_t)first + count > num_layer) {
                return ROBO_PARSE_ERROR;
            }
            info->colr_v0_bases[i].glyph_id = gid;
            info->colr_v0_bases[i].first_layer = first;
            info->colr_v0_bases[i].num_layers = count;
        }
    }
    if (layer_off != 0u && num_layer > 0u &&
        robo_font_ot_resolve_offset32(span, 0u, layer_off, &abs) == ROBO_OK) {
        for (i = 0u; i < num_layer; i++) {
            size_t off = abs + (size_t)i * 4u;
            robo_font_reader_t lr;
            uint16_t gid;
            uint16_t pal;
            robo_font_reader_init_span(&lr, span);
            if (robo_font_reader_seek(&lr, off) != ROBO_OK ||
                robo_font_read_u16be(&lr, &gid) != ROBO_OK ||
                robo_font_read_u16be(&lr, &pal) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            info->colr_v0_layers[i].glyph_id = gid;
            info->colr_v0_layers[i].palette_index = pal;
        }
    }
    return ROBO_OK;
}

static int glyph_has_v1_paint(const robo_font_color_info_t *info, robo_glyph_id_t gid)
{
    return colr_v1_base_find(info, gid) != NULL;
}

static const robo_font_color_glyph_svg_t *svg_doc_for_glyph(
    const robo_font_color_info_t *info,
    robo_glyph_id_t gid)
{
    uint32_t i;
    if (!info) {
        return NULL;
    }
    for (i = 0u; i < info->svg_doc_count; i++) {
        if (gid >= info->svg_docs[i].start_glyph_id &&
            gid <= info->svg_docs[i].end_glyph_id) {
            return &info->svg_docs[i];
        }
    }
    return NULL;
}

static robo_result_t sbix_glyph_image(const robo_font_color_info_t *info,
                                      robo_glyph_id_t gid,
                                      uint16_t prefer_ppem,
                                      robo_font_color_glyph_image_t *out)
{
    uint16_t best = 0u;
    uint32_t best_diff = 0xFFFFFFFFu;
    uint16_t i;

    if (!info || !out || gid >= info->num_glyphs || info->sbix_strike_count == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < info->sbix_strike_count; i++) {
        uint32_t diff;
        uint16_t px = info->sbix_strikes[i].ppem_x;
        diff = px > prefer_ppem ? (uint32_t)(px - prefer_ppem) : (uint32_t)(prefer_ppem - px);
        if (diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }
    {
        const robo_font_color_sbix_strike_t *st = &info->sbix_strikes[best];
        robo_font_reader_t sr;
        uint32_t data_off;
        size_t glyph_array_off;
        uint32_t off0;
        uint32_t off1;
        size_t img_off;
        size_t img_len;
        uint32_t graphic_type = 0u;
        robo_font_span_t span = info->sbix_table;

        robo_font_reader_init_span(&sr, span);
        if (robo_font_reader_seek(&sr, st->strike_offset + 4u) != ROBO_OK ||
            robo_font_read_u32be(&sr, &data_off) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        glyph_array_off = data_off;
        if (robo_font_checked_range(span.size, glyph_array_off + (size_t)gid * 4u, 8u) !=
            ROBO_OK) {
            return ROBO_NOT_FOUND;
        }
        robo_font_reader_init_span(&sr, span);
        if (robo_font_reader_seek(&sr, glyph_array_off + (size_t)gid * 4u) != ROBO_OK ||
            robo_font_read_u32be(&sr, &off0) != ROBO_OK ||
            robo_font_read_u32be(&sr, &off1) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (off0 == 0u && off1 == 0u) {
            return ROBO_NOT_FOUND;
        }
        img_off = glyph_array_off + (size_t)off0;
        img_len = (size_t)off1 - (size_t)off0;
        if (img_len < 4u ||
            robo_font_checked_range(span.size, img_off, img_len) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        graphic_type = ((uint32_t)span.data[img_off] << 24) |
                       ((uint32_t)span.data[img_off + 1u] << 16) |
                       ((uint32_t)span.data[img_off + 2u] << 8) |
                       (uint32_t)span.data[img_off + 3u];
        memset(out, 0, sizeof(*out));
        out->ppem_x = st->ppem_x;
        out->ppem_y = st->ppem_y;
        out->graphic_type = graphic_type;
        out->data.data = span.data + img_off + 4u;
        out->data.size = img_len - 4u;
        return ROBO_OK;
    }
}

static robo_result_t cblc_glyph_image(const robo_font_color_info_t *info,
                                      robo_glyph_id_t gid,
                                      uint16_t prefer_ppem,
                                      robo_font_color_glyph_image_t *out)
{
    uint16_t strike_i;
    uint16_t best = 0u;
    uint32_t best_diff = 0xFFFFFFFFu;

    if (!info || !out || gid > 0xFFFFu || info->cblc_strike_count == 0u ||
        info->cbdt_table.size == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }

    for (strike_i = 0u; strike_i < info->cblc_strike_count; strike_i++) {
        const robo_font_color_cblc_strike_t *st = &info->cblc_strikes[strike_i];
        uint32_t diff;
        uint16_t px = st->ppem_x;
        if ((uint16_t)gid < st->start_glyph || (uint16_t)gid > st->end_glyph) {
            continue;
        }
        diff = px > prefer_ppem ? (uint32_t)(px - prefer_ppem) : (uint32_t)(prefer_ppem - px);
        if (diff < best_diff) {
            best_diff = diff;
            best = strike_i;
        }
    }
    if (best_diff == 0xFFFFFFFFu) {
        return ROBO_NOT_FOUND;
    }

    {
        const robo_font_color_cblc_strike_t *st = &info->cblc_strikes[best];
        robo_font_span_t cblc = info->cblc_table;
        robo_font_span_t cbdt = info->cbdt_table;
        size_t array_off = st->index_subtable_array_offset;
        uint32_t sub_i;
        for (sub_i = 0u; sub_i < st->num_index_subtables; sub_i++) {
            size_t entry_off = array_off + (size_t)sub_i * 8u;
            robo_font_reader_t er;
            uint32_t first;
            uint32_t last;
            uint32_t add_off;
            size_t sub_abs;
            uint16_t index_format;
            uint16_t image_format;
            uint32_t image_data_off;
            size_t offset_array_off;
            uint32_t rel_off;
            size_t img_abs;
            size_t img_len;
            if (robo_font_checked_range(cblc.size, entry_off, 8u) != ROBO_OK) {
                continue;
            }
            robo_font_reader_init_span(&er, cblc);
            if (robo_font_reader_seek(&er, entry_off) != ROBO_OK ||
                robo_font_read_u32be(&er, &first) != ROBO_OK ||
                robo_font_read_u32be(&er, &last) != ROBO_OK ||
                robo_font_read_u32be(&er, &add_off) != ROBO_OK) {
                continue;
            }
            if (gid < first || gid > last) {
                continue;
            }
            if (robo_font_ot_resolve_offset32(cblc, array_off, add_off, &sub_abs) != ROBO_OK) {
                continue;
            }
            robo_font_reader_init_span(&er, cblc);
            if (robo_font_reader_seek(&er, sub_abs) != ROBO_OK ||
                robo_font_read_u16be(&er, &index_format) != ROBO_OK ||
                robo_font_read_u16be(&er, &image_format) != ROBO_OK) {
                continue;
            }
            if (index_format != 1u) {
                return ROBO_NOT_SUPPORTED;
            }
            if (robo_font_read_u32be(&er, &image_data_off) != ROBO_OK) {
                continue;
            }
            offset_array_off = robo_font_reader_pos(&er);
            if (robo_font_checked_range(cblc.size, offset_array_off + (size_t)(gid - first) * 4u,
                                        4u) != ROBO_OK) {
                continue;
            }
            robo_font_reader_init_span(&er, cblc);
            if (robo_font_reader_seek(&er, offset_array_off + (size_t)(gid - first) * 4u) !=
                    ROBO_OK ||
                robo_font_read_u32be(&er, &rel_off) != ROBO_OK) {
                continue;
            }
            img_abs = (size_t)image_data_off + (size_t)rel_off;
            if (img_abs >= cbdt.size) {
                continue;
            }
            img_len = cbdt.size - img_abs;
            memset(out, 0, sizeof(*out));
            out->ppem_x = st->ppem_x;
            out->ppem_y = st->ppem_y;
            out->format = (uint8_t)image_format;
            out->data.data = cbdt.data + img_abs;
            out->data.size = img_len;
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_font_color_info_create(robo_font_sfnt_t *sfnt,
                                          const robo_font_variation_coords_t *coords_opt,
                                          robo_font_color_info_t **out)
{
    robo_font_color_info_t *info;
    robo_font_span_t span;
    int has_colr;
    int has_cpal;
    int has_sbix;
    int has_cbdt;
    int has_cblc;
    int has_svg;
    uint32_t start_caps;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (!sfnt) {
        return ROBO_INVALID_ARGUMENT;
    }

    has_colr = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_COLR);
    has_cpal = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CPAL);
    has_sbix = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_SBIX);
    has_cbdt = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CBDT);
    has_cblc = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CBLC);
    has_svg = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_SVG);
    if (!has_colr && !has_cpal && !has_sbix && !has_cbdt && !has_cblc && !has_svg) {
        return ROBO_NOT_FOUND;
    }

    info = (robo_font_color_info_t *)robo_calloc(1, sizeof(robo_font_color_info_t));
    if (!info) {
        return ROBO_OUT_OF_MEMORY;
    }
    info->cookie = INFO_COOKIE;
    info->refs = 1u;
    robo_font_sfnt_retain(sfnt);
    info->sfnt = sfnt;
    if (coords_opt) {
        robo_font_variation_coords_retain((robo_font_variation_coords_t *)coords_opt);
        info->coords = coords_opt;
    }
    (void)robo_font_sfnt_num_glyphs(sfnt, &info->num_glyphs);

    if (has_cpal &&
        robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_CPAL, &span) == ROBO_OK) {
        start_caps = info->capabilities;
        if (parse_cpal(info, span) != ROBO_OK) {
            clear_cpal_state(info);
            info->capabilities = start_caps;
        }
    }

    if (has_colr && robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_COLR, &span) == ROBO_OK) {
        robo_font_reader_t r;
        uint16_t colr_version = 0u;
        start_caps = info->capabilities;
        if (span.size >= 2u) {
            robo_font_reader_init_span(&r, span);
            (void)robo_font_read_u16be(&r, &colr_version);
        }
        if (colr_version == 0u) {
            if (parse_colr_v0(info, span) != ROBO_OK) {
                clear_colr_state(info);
                info->capabilities = start_caps;
            }
        } else if (colr_version == 1u) {
            if (parse_colr_v1(info, span) != ROBO_OK) {
                clear_colr_state(info);
                info->capabilities = start_caps;
            } else {
                (void)colr_v1_load_v0_layers(info, span);
            }
        } else {
            /* COLR version > 1: skip COLR caps; other tables may still parse. */
        }
    }

    if (has_sbix && info->num_glyphs > 0u &&
        robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_SBIX, &span) == ROBO_OK) {
        start_caps = info->capabilities;
        if (parse_sbix(info, span) != ROBO_OK) {
            info->capabilities = start_caps;
        }
    }

    if (has_cblc && robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_CBLC, &span) == ROBO_OK) {
        robo_font_span_t cbdt_span = {0, 0};
        start_caps = info->capabilities;
        if (has_cbdt) {
            (void)robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_CBDT, &cbdt_span);
        }
        if (parse_cblc(info, span, cbdt_span) != ROBO_OK) {
            info->capabilities = start_caps;
        }
    }

    if (has_svg && robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_SVG, &span) == ROBO_OK) {
        start_caps = info->capabilities;
        if (parse_svg(info, span) != ROBO_OK) {
            info->capabilities = start_caps;
        }
    }

    if (info->capabilities == ROBO_FONT_COLOR_CAP_NONE) {
        info_free_owned(info);
        robo_free(info);
        return ROBO_NOT_FOUND;
    }

    *out = info;
    return ROBO_OK;
}

robo_result_t robo_font_color_info_create_from_face(
    robo_font_face_t *face,
    const robo_font_variation_coords_t *coords_opt,
    robo_font_color_info_t **out)
{
    robo_result_t res;
    const robo_font_sfnt_t *sfnt;

    if (!face || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    res = robo_font_face_parse_sfnt(face);
    if (res == ROBO_NOT_SUPPORTED || res == ROBO_PARSE_ERROR || res == ROBO_NOT_FOUND) {
        return ROBO_NOT_FOUND;
    }
    if (res != ROBO_OK) {
        return res;
    }
    sfnt = robo_font_face_sfnt(face);
    if (!sfnt) {
        return ROBO_PARSE_ERROR;
    }
    return robo_font_color_info_create((robo_font_sfnt_t *)sfnt, coords_opt, out);
}

void robo_font_color_info_retain(robo_font_color_info_t *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return;
    }
    info->refs++;
}

void robo_font_color_info_release(robo_font_color_info_t *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return;
    }
    if (info->refs == 0u) {
        return;
    }
    info->refs--;
    if (info->refs != 0u) {
        return;
    }
    info_free_owned(info);
    info->cookie = 0u;
    robo_free(info);
}

uint32_t robo_font_color_info_capabilities(const robo_font_color_info_t *info)
{
    return info && info->cookie == INFO_COOKIE ? info->capabilities : 0u;
}

int robo_font_color_info_is_color_capable(const robo_font_color_info_t *info)
{
    return info && info->cookie == INFO_COOKIE &&
           (info->capabilities & ~ROBO_FONT_COLOR_CAP_CPAL) != 0u;
}

uint16_t robo_font_color_info_colr_version(const robo_font_color_info_t *info)
{
    return info && info->cookie == INFO_COOKIE ? info->colr_version : 0u;
}

void robo_font_color_palette_selection_init(robo_font_color_palette_selection_t *sel)
{
    if (!sel) {
        return;
    }
    memset(sel, 0, sizeof(*sel));
}

uint16_t robo_font_color_info_palette_count(const robo_font_color_info_t *info)
{
    return info && info->cookie == INFO_COOKIE ? info->palette_count : 0u;
}

robo_result_t robo_font_color_info_palette(const robo_font_color_info_t *info,
                                           uint16_t palette_index,
                                           robo_font_color_palette_t *out)
{
    if (!info || info->cookie != INFO_COOKIE || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (palette_index >= info->palette_count || !info->palettes) {
        return ROBO_NOT_FOUND;
    }
    *out = info->palettes[palette_index];
    return ROBO_OK;
}

robo_result_t robo_font_color_info_palette_entry(const robo_font_color_info_t *info,
                                                 uint16_t palette_index,
                                                 uint16_t entry_index,
                                                 robo_font_color_rgba_t *out)
{
    robo_font_color_palette_t pal;
    robo_result_t res;
    uint16_t color_index;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_font_color_info_palette(info, palette_index, &pal);
    if (res != ROBO_OK) {
        return res;
    }
    if (entry_index >= pal.entry_count) {
        return ROBO_NOT_FOUND;
    }
    color_index = (uint16_t)(pal.first_color_index + entry_index);
    if (color_index >= info->num_color_records || !info->colors) {
        return ROBO_PARSE_ERROR;
    }
    *out = info->colors[color_index];
    return ROBO_OK;
}

robo_result_t robo_font_color_resolve_palette_color(
    const robo_font_color_info_t *info,
    const robo_font_color_palette_selection_t *sel,
    uint16_t palette_index,
    robo_font_color_rgba_t *out)
{
    robo_font_color_palette_t pal;
    robo_result_t res;
    uint16_t selected;
    uint16_t i;

    if (!info || info->cookie != INFO_COOKIE || !sel || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (palette_index == ROBO_FONT_COLOR_FOREGROUND_PALETTE) {
        return ROBO_NOT_FOUND;
    }
    selected = sel->palette_index;
    if (selected >= info->palette_count) {
        return ROBO_NOT_FOUND;
    }
    if (sel->has_overrides && sel->override_indices && sel->override_colors) {
        for (i = 0u; i < sel->override_count; i++) {
            if (sel->override_indices[i] == palette_index) {
                *out = sel->override_colors[i];
                return ROBO_OK;
            }
        }
    }
    res = robo_font_color_info_palette(info, selected, &pal);
    if (res != ROBO_OK) {
        return res;
    }
    if (palette_index >= pal.entry_count) {
        return ROBO_NOT_FOUND;
    }
    return robo_font_color_info_palette_entry(info, selected, palette_index, out);
}

robo_result_t robo_font_color_info_glyph(const robo_font_color_info_t *info,
                                         robo_glyph_id_t glyph_id,
                                         robo_font_color_glyph_t *out)
{
    const robo_font_color_colr_v0_base_t *v0;
    const robo_font_color_glyph_svg_t *svg;

    if (!info || info->cookie != INFO_COOKIE || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (info->num_glyphs > 0u && glyph_id >= info->num_glyphs) {
        return ROBO_NOT_FOUND;
    }
    memset(out, 0, sizeof(*out));
    out->glyph_id = glyph_id;
    out->paint_root = NODE_NONE;

    if (glyph_has_v1_paint(info, glyph_id)) {
        out->format = ROBO_FONT_COLOR_GLYPH_PAINT;
        return ROBO_OK;
    }
    v0 = colr_v0_base_find(info, glyph_id);
    if (v0) {
        out->format = ROBO_FONT_COLOR_GLYPH_LAYERS;
        out->layer_count = v0->num_layers;
        out->layers = &info->colr_v0_layers[v0->first_layer];
        return ROBO_OK;
    }
    if ((info->capabilities & ROBO_FONT_COLOR_CAP_SBIX) != 0u) {
        robo_font_color_glyph_image_t img;
        if (sbix_glyph_image(info, glyph_id, 0u, &img) == ROBO_OK) {
            out->format = ROBO_FONT_COLOR_GLYPH_BITMAP;
            out->has_image = 1u;
            return ROBO_OK;
        }
    }
    if ((info->capabilities & ROBO_FONT_COLOR_CAP_CBLC) != 0u &&
        (info->capabilities & ROBO_FONT_COLOR_CAP_CBDT) != 0u) {
        robo_font_color_glyph_image_t img;
        if (cblc_glyph_image(info, glyph_id, 0u, &img) == ROBO_OK) {
            out->format = ROBO_FONT_COLOR_GLYPH_BITMAP;
            out->has_image = 1u;
            return ROBO_OK;
        }
    }
    svg = svg_doc_for_glyph(info, glyph_id);
    if (svg) {
        out->format = ROBO_FONT_COLOR_GLYPH_SVG;
        out->has_svg = 1u;
        return ROBO_OK;
    }
    out->format = ROBO_FONT_COLOR_GLYPH_MONOCHROME;
    return ROBO_OK;
}

robo_result_t robo_font_color_info_glyph_layers(const robo_font_color_info_t *info,
                                                robo_glyph_id_t glyph_id,
                                                const robo_font_color_glyph_layer_t **out_layers,
                                                uint32_t *out_count)
{
    const robo_font_color_colr_v0_base_t *v0;

    if (!info || info->cookie != INFO_COOKIE) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (info->num_glyphs > 0u && glyph_id >= info->num_glyphs) {
        return ROBO_NOT_FOUND;
    }
    v0 = colr_v0_base_find(info, glyph_id);
    if (!v0) {
        return ROBO_NOT_FOUND;
    }
    if (out_layers) {
        *out_layers = &info->colr_v0_layers[v0->first_layer];
    }
    if (out_count) {
        *out_count = v0->num_layers;
    }
    return ROBO_OK;
}

robo_result_t robo_font_color_info_glyph_paint(const robo_font_color_info_t *info,
                                               robo_glyph_id_t glyph_id,
                                               const robo_font_color_paint_t **out_nodes,
                                               uint32_t *out_node_count,
                                               uint32_t *out_root_index)
{
    robo_font_color_info_t *mut = (robo_font_color_info_t *)info;
    robo_font_color_paint_cache_t *cache;
    robo_result_t res;

    if (!info || info->cookie != INFO_COOKIE) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (info->num_glyphs > 0u && glyph_id >= info->num_glyphs) {
        return ROBO_NOT_FOUND;
    }
    if (!colr_v1_base_find(info, glyph_id)) {
        return ROBO_NOT_FOUND;
    }
    res = paint_build_for_glyph(mut, glyph_id, &cache);
    if (res != ROBO_OK) {
        return res;
    }
    if (out_nodes) {
        *out_nodes = cache->nodes;
    }
    if (out_node_count) {
        *out_node_count = cache->node_count;
    }
    if (out_root_index) {
        *out_root_index = cache->root_index;
    }
    return ROBO_OK;
}

robo_result_t robo_font_color_info_glyph_image(const robo_font_color_info_t *info,
                                               robo_glyph_id_t glyph_id,
                                               uint16_t prefer_ppem,
                                               robo_font_color_glyph_image_t *out)
{
    robo_result_t res;

    if (!info || info->cookie != INFO_COOKIE || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (info->num_glyphs > 0u && glyph_id >= info->num_glyphs) {
        return ROBO_NOT_FOUND;
    }
    if ((info->capabilities & ROBO_FONT_COLOR_CAP_SBIX) != 0u) {
        res = sbix_glyph_image(info, glyph_id, prefer_ppem, out);
        if (res == ROBO_OK) {
            return ROBO_OK;
        }
        if (res != ROBO_NOT_FOUND && res != ROBO_INVALID_ARGUMENT) {
            return res;
        }
    }
    if ((info->capabilities & ROBO_FONT_COLOR_CAP_CBLC) != 0u &&
        (info->capabilities & ROBO_FONT_COLOR_CAP_CBDT) != 0u) {
        return cblc_glyph_image(info, glyph_id, prefer_ppem, out);
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_font_color_info_glyph_svg(const robo_font_color_info_t *info,
                                             robo_glyph_id_t glyph_id,
                                             robo_font_color_glyph_svg_t *out)
{
    const robo_font_color_glyph_svg_t *doc;

    if (!info || info->cookie != INFO_COOKIE || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (info->num_glyphs > 0u && glyph_id >= info->num_glyphs) {
        return ROBO_NOT_FOUND;
    }
    doc = svg_doc_for_glyph(info, glyph_id);
    if (!doc) {
        return ROBO_NOT_FOUND;
    }
    *out = *doc;
    return ROBO_OK;
}

const robo_font_variation_coords_t *robo_font_color_info_variation_coords(
    const robo_font_color_info_t *info)
{
    return info && info->cookie == INFO_COOKIE ? info->coords : NULL;
}

uint16_t robo_font_color_info_sbix_strike_count(const robo_font_color_info_t *info)
{
    return info && info->cookie == INFO_COOKIE ? info->sbix_strike_count : 0u;
}

uint16_t robo_font_color_info_cblc_strike_count(const robo_font_color_info_t *info)
{
    return info && info->cookie == INFO_COOKIE ? info->cblc_strike_count : 0u;
}

uint32_t robo_font_color_info_svg_document_count(const robo_font_color_info_t *info)
{
    return info && info->cookie == INFO_COOKIE ? info->svg_doc_count : 0u;
}

robo_result_t robo_font_color_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_color_info_t *info = NULL;
    robo_font_color_glyph_t glyph;
    robo_font_color_glyph_image_t img;
    robo_font_color_glyph_svg_t svg;
    robo_font_color_paint_t const *nodes = NULL;
    uint32_t node_count = 0u;
    uint32_t root = 0u;
    robo_glyph_id_t gid;

    if (!data && size != 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (size == 0u) {
        return ROBO_OK;
    }
    if (size > 256u * 1024u) {
        size = 256u * 1024u;
    }
    if (robo_font_source_create_memory_copy(data, size, &src) != ROBO_OK) {
        return ROBO_OK;
    }
    if (robo_font_sfnt_parse(src, 0u, 0u, &sfnt) != ROBO_OK) {
        robo_font_source_release(src);
        return ROBO_OK;
    }
    if (robo_font_color_info_create(sfnt, NULL, &info) == ROBO_OK && info) {
        gid = (robo_glyph_id_t)(data[0] | ((uint32_t)data[(size > 1u ? 1u : 0u)] << 8));
        (void)robo_font_color_info_capabilities(info);
        (void)robo_font_color_info_palette_count(info);
        (void)robo_font_color_info_glyph(info, gid, &glyph);
        (void)robo_font_color_info_glyph_layers(info, gid, NULL, NULL);
        (void)robo_font_color_info_glyph_paint(info, gid, &nodes, &node_count, &root);
        (void)robo_font_color_info_glyph_image(info, gid, data[0], &img);
        (void)robo_font_color_info_glyph_svg(info, gid, &svg);
        robo_font_color_info_release(info);
    }
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
    return ROBO_OK;
}
