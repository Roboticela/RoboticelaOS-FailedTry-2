/*
 * RoboFont cmap mapping engine (Goal 5.6).
 */

#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontTt.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/* ---- Internal subtable representations ---- */

typedef struct {
    uint16_t start;
    uint16_t end;
    int16_t id_delta;
    uint16_t id_range_offset; /* bytes; 0 = delta-only */
    uint32_t glyph_array_index; /* into fmt4 glyph array when id_range_offset != 0 */
} cmap_fmt4_seg_t;

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t start_glyph; /* format 12: start GID; format 13: constant GID */
} cmap_group_t;

typedef struct {
    uint32_t start_unicode;
    uint8_t additional_count; /* range covers start .. start+additional */
} cmap_uvs_range_t;

typedef struct {
    uint32_t unicode;
    uint16_t glyph_id;
} cmap_uvs_mapping_t;

typedef struct {
    uint32_t var_selector;
    uint32_t num_default_ranges;
    cmap_uvs_range_t *default_ranges;
    uint32_t num_non_default;
    cmap_uvs_mapping_t *non_default;
} cmap_uvs_selector_t;

typedef struct {
    uint16_t format;
    uint16_t platform;
    uint16_t encoding;
    uint32_t language;

    /* format 0 */
    uint8_t *fmt0_glyphs; /* 256 */

    /* format 2 — keep raw subtable for careful lookup */
    robo_font_span_t fmt2_span;

    /* format 4 */
    uint16_t fmt4_seg_count;
    cmap_fmt4_seg_t *fmt4_segs;
    uint16_t *fmt4_glyph_ids;
    uint32_t fmt4_glyph_count;

    /* format 6 */
    uint16_t fmt6_first;
    uint16_t fmt6_count;
    uint16_t *fmt6_glyphs;

    /* format 10 */
    uint32_t fmt10_start;
    uint32_t fmt10_count;
    uint16_t *fmt10_glyphs;

    /* format 12 / 13 */
    uint32_t group_count;
    cmap_group_t *groups;
    int groups_are_fmt13;

    /* format 14 */
    uint32_t uvs_count;
    cmap_uvs_selector_t *uvs;
} cmap_body_t;

struct robo_font_cmap_map {
    uint32_t refs;
    robo_font_sfnt_t *sfnt; /* may be NULL for span-only maps */
    uint32_t num_glyphs;
    cmap_body_t primary;
    cmap_body_t uvs_body; /* format 14 only; format field 14 or 0 */
    int has_primary;
    int has_uvs;
    robo_font_cmap_t structural; /* encoding list retained for introspection */
};

int robo_font_cmap_is_unicode_scalar(uint32_t code_point)
{
    if (code_point > 0x10FFFFu) {
        return 0;
    }
    if (code_point >= 0xD800u && code_point <= 0xDFFFu) {
        return 0;
    }
    return 1;
}

static void clear_uvs_selector(cmap_uvs_selector_t *s)
{
    if (!s) {
        return;
    }
    robo_free(s->default_ranges);
    robo_free(s->non_default);
    memset(s, 0, sizeof(*s));
}

static void clear_body(cmap_body_t *b)
{
    uint32_t i;
    if (!b) {
        return;
    }
    robo_free(b->fmt0_glyphs);
    robo_free(b->fmt4_segs);
    robo_free(b->fmt4_glyph_ids);
    robo_free(b->fmt6_glyphs);
    robo_free(b->fmt10_glyphs);
    robo_free(b->groups);
    if (b->uvs) {
        for (i = 0u; i < b->uvs_count; i++) {
            clear_uvs_selector(&b->uvs[i]);
        }
        robo_free(b->uvs);
    }
    memset(b, 0, sizeof(*b));
}

static int gid_ok(uint32_t num_glyphs, uint32_t gid)
{
    return gid < num_glyphs;
}

static robo_result_t finalize_gid(uint32_t num_glyphs,
                                  uint32_t gid,
                                  robo_font_cmap_lookup_result_t *out)
{
    if (!gid_ok(num_glyphs, gid)) {
        out->status = ROBO_FONT_CMAP_INVALID_FONT;
        out->glyph_id = 0u;
        return ROBO_OK;
    }
    if (gid == 0u) {
        /* Mapped to .notdef — still a successful mapping. */
        out->status = ROBO_FONT_CMAP_MAPPED;
        out->glyph_id = 0u;
        return ROBO_OK;
    }
    out->status = ROBO_FONT_CMAP_MAPPED;
    out->glyph_id = gid;
    return ROBO_OK;
}

/* ---- Format parsers ---- */

static robo_result_t parse_fmt0(robo_font_span_t sub, cmap_body_t *b)
{
    robo_font_reader_t r;
    uint16_t i;
    if (sub.size < 262u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, sub);
    if (robo_font_reader_seek(&r, 6u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    b->fmt0_glyphs = (uint8_t *)robo_calloc(256u, 1u);
    if (!b->fmt0_glyphs) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < 256u; i++) {
        if (robo_font_read_u8(&r, &b->fmt0_glyphs[i]) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
    }
    b->format = 0u;
    return ROBO_OK;
}

static robo_result_t parse_fmt2(robo_font_span_t sub, cmap_body_t *b)
{
    if (sub.size < 518u) { /* format+len+lang + 256*2 keys minimum-ish */
        return ROBO_PARSE_ERROR;
    }
    b->fmt2_span = sub;
    b->format = 2u;
    return ROBO_OK;
}

static robo_result_t lookup_fmt2(const cmap_body_t *b,
                                 uint32_t cp,
                                 uint32_t num_glyphs,
                                 robo_font_cmap_lookup_result_t *out)
{
    robo_font_reader_t r;
    uint16_t high;
    uint16_t low;
    uint16_t key;
    uint16_t header_keys_off = 6u;
    uint16_t subheader_index;
    size_t subheaders_off;
    size_t sh_off;
    uint16_t first_code;
    uint16_t entry_count;
    int16_t id_delta;
    uint16_t id_range_offset;
    uint32_t gid;

    if (cp > 0xFFFFu) {
        out->status = ROBO_FONT_CMAP_UNMAPPED;
        out->glyph_id = 0u;
        return ROBO_OK;
    }
    high = (uint16_t)((cp >> 8) & 0xFFu);
    low = (uint16_t)(cp & 0xFFu);
    robo_font_reader_init_span(&r, b->fmt2_span);
    if (robo_font_reader_seek(&r, header_keys_off + (size_t)high * 2u) != ROBO_OK ||
        robo_font_read_u16be(&r, &key) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    /* key is offset into subHeaders in bytes; /8 = index */
    if ((key % 8u) != 0u) {
        return ROBO_PARSE_ERROR;
    }
    subheader_index = (uint16_t)(key / 8u);
    subheaders_off = 6u + 256u * 2u;
    sh_off = subheaders_off + (size_t)subheader_index * 8u;
    if (sh_off + 8u > b->fmt2_span.size) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_reader_seek(&r, sh_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &first_code) != ROBO_OK ||
        robo_font_read_u16be(&r, &entry_count) != ROBO_OK ||
        robo_font_read_i16be(&r, &id_delta) != ROBO_OK ||
        robo_font_read_u16be(&r, &id_range_offset) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (low < first_code || (uint16_t)(low - first_code) >= entry_count) {
        out->status = ROBO_FONT_CMAP_UNMAPPED;
        out->glyph_id = 0u;
        return ROBO_OK;
    }
    if (id_range_offset == 0u) {
        gid = (uint32_t)((uint16_t)(low + (uint16_t)id_delta));
        return finalize_gid(num_glyphs, gid, out);
    }
    {
        /* glyphIndexAddress = idRangeOffset + 2*(low-firstCode) + &idRangeOffset */
        size_t id_range_field = sh_off + 6u;
        size_t glyph_off =
            id_range_field + (size_t)id_range_offset +
            (size_t)(low - first_code) * 2u;
        uint16_t g16;
        if (glyph_off + 2u > b->fmt2_span.size) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_reader_seek(&r, glyph_off) != ROBO_OK ||
            robo_font_read_u16be(&r, &g16) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (g16 == 0u) {
            out->status = ROBO_FONT_CMAP_UNMAPPED;
            out->glyph_id = 0u;
            return ROBO_OK;
        }
        gid = (uint32_t)((uint16_t)(g16 + (uint16_t)id_delta));
        return finalize_gid(num_glyphs, gid, out);
    }
}

static robo_result_t parse_fmt4(robo_font_span_t sub, cmap_body_t *b)
{
    robo_font_reader_t r;
    uint16_t seg_count_x2;
    uint16_t seg_count;
    uint16_t i;
    size_t end_off;
    size_t start_off;
    size_t delta_off;
    size_t range_off;
    uint32_t glyph_slots = 0u;

    if (sub.size < 16u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, sub);
    if (robo_font_reader_seek(&r, 6u) != ROBO_OK ||
        robo_font_read_u16be(&r, &seg_count_x2) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if ((seg_count_x2 & 1u) != 0u) {
        return ROBO_PARSE_ERROR;
    }
    seg_count = (uint16_t)(seg_count_x2 / 2u);
    if (seg_count == 0u || seg_count > ROBO_FONT_CMAP_MAX_SEGMENTS) {
        return ROBO_PARSE_ERROR;
    }
    /* skip searchRange, entrySelector, rangeShift */
    if (robo_font_reader_seek(&r, 14u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    end_off = 14u;
    start_off = end_off + (size_t)seg_count * 2u + 2u; /* + reservedPad */
    delta_off = start_off + (size_t)seg_count * 2u;
    range_off = delta_off + (size_t)seg_count * 2u;
    if (range_off + (size_t)seg_count * 2u > sub.size) {
        return ROBO_PARSE_ERROR;
    }

    b->fmt4_segs = (cmap_fmt4_seg_t *)robo_calloc(seg_count, sizeof(cmap_fmt4_seg_t));
    if (!b->fmt4_segs) {
        return ROBO_OUT_OF_MEMORY;
    }
    b->fmt4_seg_count = seg_count;

    for (i = 0u; i < seg_count; i++) {
        uint16_t end_c;
        if (robo_font_reader_seek(&r, end_off + (size_t)i * 2u) != ROBO_OK ||
            robo_font_read_u16be(&r, &end_c) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        b->fmt4_segs[i].end = end_c;
        if (i > 0u && end_c <= b->fmt4_segs[i - 1u].end && end_c != 0xFFFFu) {
            /* allow equal only for malformed rejection — must be increasing */
            if (end_c < b->fmt4_segs[i - 1u].end) {
                return ROBO_PARSE_ERROR;
            }
        }
    }
    for (i = 0u; i < seg_count; i++) {
        uint16_t start_c;
        int16_t delta;
        uint16_t ro;
        if (robo_font_reader_seek(&r, start_off + (size_t)i * 2u) != ROBO_OK ||
            robo_font_read_u16be(&r, &start_c) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_reader_seek(&r, delta_off + (size_t)i * 2u) != ROBO_OK ||
            robo_font_read_i16be(&r, &delta) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_reader_seek(&r, range_off + (size_t)i * 2u) != ROBO_OK ||
            robo_font_read_u16be(&r, &ro) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (start_c > b->fmt4_segs[i].end) {
            return ROBO_PARSE_ERROR;
        }
        b->fmt4_segs[i].start = start_c;
        b->fmt4_segs[i].id_delta = delta;
        b->fmt4_segs[i].id_range_offset = ro;
        if (ro != 0u && b->fmt4_segs[i].end != 0xFFFFu) {
            uint32_t span = (uint32_t)b->fmt4_segs[i].end - (uint32_t)start_c + 1u;
            if (glyph_slots > UINT32_MAX - span) {
                return ROBO_PARSE_ERROR;
            }
            b->fmt4_segs[i].glyph_array_index = glyph_slots;
            glyph_slots += span;
        }
    }
    if (seg_count == 0u || b->fmt4_segs[seg_count - 1u].end != 0xFFFFu) {
        return ROBO_PARSE_ERROR;
    }

    if (glyph_slots > ROBO_FONT_CMAP_MAX_GLYPH_ARRAY) {
        return ROBO_PARSE_ERROR;
    }
    if (glyph_slots > 0u) {
        uint32_t g;
        b->fmt4_glyph_ids =
            (uint16_t *)robo_calloc(glyph_slots, sizeof(uint16_t));
        if (!b->fmt4_glyph_ids) {
            return ROBO_OUT_OF_MEMORY;
        }
        b->fmt4_glyph_count = glyph_slots;
        /* Fill by reading from original glyphIdArray using OT formula. */
        for (i = 0u; i < seg_count; i++) {
            cmap_fmt4_seg_t *seg = &b->fmt4_segs[i];
            uint32_t c;
            if (seg->id_range_offset == 0u || seg->end == 0xFFFFu) {
                continue;
            }
            for (c = seg->start; c <= seg->end; c++) {
                size_t id_range_field = range_off + (size_t)i * 2u;
                size_t goff = id_range_field + (size_t)seg->id_range_offset +
                              (size_t)(c - seg->start) * 2u;
                uint16_t g16;
                uint32_t slot = seg->glyph_array_index + (c - seg->start);
                if (goff + 2u > sub.size || slot >= glyph_slots) {
                    return ROBO_PARSE_ERROR;
                }
                if (robo_font_reader_seek(&r, goff) != ROBO_OK ||
                    robo_font_read_u16be(&r, &g16) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                b->fmt4_glyph_ids[slot] = g16;
                (void)g;
            }
        }
    }
    b->format = 4u;
    return ROBO_OK;
}

static robo_result_t lookup_fmt4(const cmap_body_t *b,
                                 uint32_t cp,
                                 uint32_t num_glyphs,
                                 robo_font_cmap_lookup_result_t *out)
{
    uint16_t lo;
    uint16_t hi;
    uint16_t mid;
    const cmap_fmt4_seg_t *seg;
    uint32_t gid;

    if (cp > 0xFFFFu) {
        out->status = ROBO_FONT_CMAP_UNMAPPED;
        out->glyph_id = 0u;
        return ROBO_OK;
    }
    lo = 0u;
    hi = b->fmt4_seg_count;
    while (lo < hi) {
        mid = (uint16_t)(lo + (hi - lo) / 2u);
        if (cp > b->fmt4_segs[mid].end) {
            lo = (uint16_t)(mid + 1u);
        } else {
            hi = mid;
        }
    }
    if (lo >= b->fmt4_seg_count) {
        out->status = ROBO_FONT_CMAP_UNMAPPED;
        out->glyph_id = 0u;
        return ROBO_OK;
    }
    seg = &b->fmt4_segs[lo];
    if (cp < seg->start || cp > seg->end) {
        out->status = ROBO_FONT_CMAP_UNMAPPED;
        out->glyph_id = 0u;
        return ROBO_OK;
    }
    if (seg->end == 0xFFFFu && seg->start == 0xFFFFu) {
        out->status = ROBO_FONT_CMAP_UNMAPPED;
        out->glyph_id = 0u;
        return ROBO_OK;
    }
    if (seg->id_range_offset == 0u) {
        gid = (uint32_t)((uint16_t)((uint16_t)cp + (uint16_t)seg->id_delta));
        return finalize_gid(num_glyphs, gid, out);
    }
    {
        uint32_t slot = seg->glyph_array_index + ((uint16_t)cp - seg->start);
        uint16_t g16;
        if (slot >= b->fmt4_glyph_count) {
            return ROBO_PARSE_ERROR;
        }
        g16 = b->fmt4_glyph_ids[slot];
        if (g16 == 0u) {
            out->status = ROBO_FONT_CMAP_UNMAPPED;
            out->glyph_id = 0u;
            return ROBO_OK;
        }
        gid = (uint32_t)((uint16_t)(g16 + (uint16_t)seg->id_delta));
        return finalize_gid(num_glyphs, gid, out);
    }
}

static robo_result_t parse_fmt6(robo_font_span_t sub, cmap_body_t *b)
{
    robo_font_reader_t r;
    uint16_t first;
    uint16_t count;
    uint16_t i;
    if (sub.size < 10u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, sub);
    if (robo_font_reader_seek(&r, 6u) != ROBO_OK ||
        robo_font_read_u16be(&r, &first) != ROBO_OK ||
        robo_font_read_u16be(&r, &count) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if ((uint32_t)count > ROBO_FONT_CMAP_MAX_GLYPH_ARRAY) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(sub.size, 10u, (size_t)count * 2u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    b->fmt6_first = first;
    b->fmt6_count = count;
    if (count > 0u) {
        b->fmt6_glyphs = (uint16_t *)robo_calloc(count, sizeof(uint16_t));
        if (!b->fmt6_glyphs) {
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < count; i++) {
            if (robo_font_read_u16be(&r, &b->fmt6_glyphs[i]) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
        }
    }
    b->format = 6u;
    return ROBO_OK;
}

static robo_result_t lookup_fmt6(const cmap_body_t *b,
                                 uint32_t cp,
                                 uint32_t num_glyphs,
                                 robo_font_cmap_lookup_result_t *out)
{
    if (cp > 0xFFFFu || cp < b->fmt6_first ||
        (cp - b->fmt6_first) >= b->fmt6_count) {
        out->status = ROBO_FONT_CMAP_UNMAPPED;
        out->glyph_id = 0u;
        return ROBO_OK;
    }
    return finalize_gid(num_glyphs, b->fmt6_glyphs[cp - b->fmt6_first], out);
}

static robo_result_t parse_fmt10(robo_font_span_t sub, cmap_body_t *b)
{
    robo_font_reader_t r;
    uint32_t start;
    uint32_t count;
    uint32_t i;
    if (sub.size < 20u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, sub);
    if (robo_font_reader_seek(&r, 12u) != ROBO_OK ||
        robo_font_read_u32be(&r, &start) != ROBO_OK ||
        robo_font_read_u32be(&r, &count) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (count > ROBO_FONT_CMAP_MAX_GLYPH_ARRAY) {
        return ROBO_PARSE_ERROR;
    }
    if (start > 0x10FFFFu ||
        (count > 0u && start + count - 1u > 0x10FFFFu) ||
        (count > 0u && start + count < start)) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(sub.size, 20u, (size_t)count * 2u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    b->fmt10_start = start;
    b->fmt10_count = count;
    if (count > 0u) {
        b->fmt10_glyphs = (uint16_t *)robo_calloc(count, sizeof(uint16_t));
        if (!b->fmt10_glyphs) {
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < count; i++) {
            if (robo_font_read_u16be(&r, &b->fmt10_glyphs[i]) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
        }
    }
    b->format = 10u;
    return ROBO_OK;
}

static robo_result_t lookup_fmt10(const cmap_body_t *b,
                                  uint32_t cp,
                                  uint32_t num_glyphs,
                                  robo_font_cmap_lookup_result_t *out)
{
    if (cp < b->fmt10_start || (cp - b->fmt10_start) >= b->fmt10_count) {
        out->status = ROBO_FONT_CMAP_UNMAPPED;
        out->glyph_id = 0u;
        return ROBO_OK;
    }
    return finalize_gid(num_glyphs, b->fmt10_glyphs[cp - b->fmt10_start], out);
}

static robo_result_t parse_fmt12_or_13(robo_font_span_t sub, cmap_body_t *b, int is_13)
{
    robo_font_reader_t r;
    uint32_t n_groups;
    uint32_t i;
    if (sub.size < 16u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, sub);
    if (robo_font_reader_seek(&r, 12u) != ROBO_OK ||
        robo_font_read_u32be(&r, &n_groups) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (n_groups > ROBO_FONT_CMAP_MAX_GROUPS) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(sub.size, 16u, (size_t)n_groups * 12u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    b->group_count = n_groups;
    b->groups_are_fmt13 = is_13;
    if (n_groups == 0u) {
        b->format = is_13 ? 13u : 12u;
        return ROBO_OK;
    }
    b->groups = (cmap_group_t *)robo_calloc(n_groups, sizeof(cmap_group_t));
    if (!b->groups) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < n_groups; i++) {
        uint32_t start;
        uint32_t end;
        uint32_t g;
        if (robo_font_read_u32be(&r, &start) != ROBO_OK ||
            robo_font_read_u32be(&r, &end) != ROBO_OK ||
            robo_font_read_u32be(&r, &g) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (start > end || end > 0x10FFFFu) {
            return ROBO_PARSE_ERROR;
        }
        if ((start >= 0xD800u && start <= 0xDFFFu) ||
            (end >= 0xD800u && end <= 0xDFFFu) ||
            (start < 0xD800u && end > 0xDFFFu)) {
            return ROBO_PARSE_ERROR;
        }
        if (i > 0u && start <= b->groups[i - 1u].end) {
            return ROBO_PARSE_ERROR; /* must be sorted, non-overlapping */
        }
        if (!is_13) {
            /* startGlyph + (end-start) must not overflow uint32 meaningfully for GID */
            if (g > 0xFFFFFFFFu - (end - start)) {
                return ROBO_PARSE_ERROR;
            }
        }
        b->groups[i].start = start;
        b->groups[i].end = end;
        b->groups[i].start_glyph = g;
    }
    b->format = is_13 ? 13u : 12u;
    return ROBO_OK;
}

static robo_result_t lookup_fmt12_13(const cmap_body_t *b,
                                     uint32_t cp,
                                     uint32_t num_glyphs,
                                     robo_font_cmap_lookup_result_t *out)
{
    uint32_t lo = 0u;
    uint32_t hi = b->group_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (cp > b->groups[mid].end) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo >= b->group_count || cp < b->groups[lo].start || cp > b->groups[lo].end) {
        out->status = ROBO_FONT_CMAP_UNMAPPED;
        out->glyph_id = 0u;
        return ROBO_OK;
    }
    if (b->groups_are_fmt13) {
        return finalize_gid(num_glyphs, b->groups[lo].start_glyph, out);
    }
    return finalize_gid(num_glyphs,
                        b->groups[lo].start_glyph + (cp - b->groups[lo].start), out);
}

static robo_result_t parse_fmt14(robo_font_span_t sub, cmap_body_t *b)
{
    robo_font_reader_t r;
    uint32_t num_rec;
    uint32_t i;

    if (sub.size < 10u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, sub);
    if (robo_font_reader_seek(&r, 6u) != ROBO_OK ||
        robo_font_read_u32be(&r, &num_rec) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (num_rec > ROBO_FONT_CMAP_MAX_UVS_SELECTORS) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(sub.size, 10u, (size_t)num_rec * 11u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    b->uvs_count = num_rec;
    b->format = 14u;
    if (num_rec == 0u) {
        return ROBO_OK;
    }
    b->uvs = (cmap_uvs_selector_t *)robo_calloc(num_rec, sizeof(cmap_uvs_selector_t));
    if (!b->uvs) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < num_rec; i++) {
        uint8_t vs_bytes[3];
        uint32_t vs;
        uint32_t def_off;
        uint32_t nondef_off;
        uint32_t j;
        if (robo_font_read_u8(&r, &vs_bytes[0]) != ROBO_OK ||
            robo_font_read_u8(&r, &vs_bytes[1]) != ROBO_OK ||
            robo_font_read_u8(&r, &vs_bytes[2]) != ROBO_OK ||
            robo_font_read_u32be(&r, &def_off) != ROBO_OK ||
            robo_font_read_u32be(&r, &nondef_off) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        vs = ((uint32_t)vs_bytes[0] << 16) | ((uint32_t)vs_bytes[1] << 8) |
             (uint32_t)vs_bytes[2];
        b->uvs[i].var_selector = vs;

        if (def_off != 0u) {
            size_t abs = (size_t)def_off;
            robo_font_reader_t dr;
            uint32_t n_ranges;
            if (abs + 4u > sub.size) {
                return ROBO_PARSE_ERROR;
            }
            robo_font_reader_init_span(&dr, sub);
            if (robo_font_reader_seek(&dr, abs) != ROBO_OK ||
                robo_font_read_u32be(&dr, &n_ranges) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            if (n_ranges > ROBO_FONT_CMAP_MAX_UVS_RANGES) {
                return ROBO_PARSE_ERROR;
            }
            if (robo_font_checked_range(sub.size, abs + 4u, (size_t)n_ranges * 4u) !=
                ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            b->uvs[i].num_default_ranges = n_ranges;
            if (n_ranges > 0u) {
                b->uvs[i].default_ranges = (cmap_uvs_range_t *)robo_calloc(
                    n_ranges, sizeof(cmap_uvs_range_t));
                if (!b->uvs[i].default_ranges) {
                    return ROBO_OUT_OF_MEMORY;
                }
                for (j = 0u; j < n_ranges; j++) {
                    uint8_t u[3];
                    uint8_t add;
                    if (robo_font_read_u8(&dr, &u[0]) != ROBO_OK ||
                        robo_font_read_u8(&dr, &u[1]) != ROBO_OK ||
                        robo_font_read_u8(&dr, &u[2]) != ROBO_OK ||
                        robo_font_read_u8(&dr, &add) != ROBO_OK) {
                        return ROBO_PARSE_ERROR;
                    }
                    b->uvs[i].default_ranges[j].start_unicode =
                        ((uint32_t)u[0] << 16) | ((uint32_t)u[1] << 8) | u[2];
                    b->uvs[i].default_ranges[j].additional_count = add;
                }
            }
        }
        if (nondef_off != 0u) {
            size_t abs = (size_t)nondef_off;
            robo_font_reader_t nr;
            uint32_t n_map;
            if (abs + 4u > sub.size) {
                return ROBO_PARSE_ERROR;
            }
            robo_font_reader_init_span(&nr, sub);
            if (robo_font_reader_seek(&nr, abs) != ROBO_OK ||
                robo_font_read_u32be(&nr, &n_map) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            if (n_map > ROBO_FONT_CMAP_MAX_UVS_MAPPINGS) {
                return ROBO_PARSE_ERROR;
            }
            if (robo_font_checked_range(sub.size, abs + 4u, (size_t)n_map * 5u) !=
                ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            b->uvs[i].num_non_default = n_map;
            if (n_map > 0u) {
                b->uvs[i].non_default = (cmap_uvs_mapping_t *)robo_calloc(
                    n_map, sizeof(cmap_uvs_mapping_t));
                if (!b->uvs[i].non_default) {
                    return ROBO_OUT_OF_MEMORY;
                }
                for (j = 0u; j < n_map; j++) {
                    uint8_t u[3];
                    uint16_t gid;
                    if (robo_font_read_u8(&nr, &u[0]) != ROBO_OK ||
                        robo_font_read_u8(&nr, &u[1]) != ROBO_OK ||
                        robo_font_read_u8(&nr, &u[2]) != ROBO_OK ||
                        robo_font_read_u16be(&nr, &gid) != ROBO_OK) {
                        return ROBO_PARSE_ERROR;
                    }
                    b->uvs[i].non_default[j].unicode =
                        ((uint32_t)u[0] << 16) | ((uint32_t)u[1] << 8) | u[2];
                    b->uvs[i].non_default[j].glyph_id = gid;
                }
            }
        }
    }
    return ROBO_OK;
}

static robo_result_t parse_subtable_body(robo_font_span_t table,
                                         const robo_font_cmap_encoding_t *enc,
                                         cmap_body_t *b)
{
    robo_font_span_t sub;
    robo_result_t res;
    if (enc->length == 0u ||
        robo_font_span_slice(table, enc->subtable_offset, enc->length, &sub) !=
            ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    clear_body(b);
    b->platform = enc->platform_id;
    b->encoding = enc->encoding_id;
    b->language = enc->language;
    switch (enc->format) {
    case 0:
        res = parse_fmt0(sub, b);
        break;
    case 2:
        res = parse_fmt2(sub, b);
        break;
    case 4:
        res = parse_fmt4(sub, b);
        break;
    case 6:
        res = parse_fmt6(sub, b);
        break;
    case 10:
        res = parse_fmt10(sub, b);
        break;
    case 12:
        res = parse_fmt12_or_13(sub, b, 0);
        break;
    case 13:
        res = parse_fmt12_or_13(sub, b, 1);
        break;
    case 14:
        res = parse_fmt14(sub, b);
        break;
    default:
        return ROBO_NOT_SUPPORTED;
    }
    if (res != ROBO_OK) {
        clear_body(b);
    }
    return res;
}

static int encoding_score(const robo_font_cmap_encoding_t *e)
{
    int score = 0;
    uint16_t p = e->platform_id;
    uint16_t enc = e->encoding_id;
    uint16_t fmt = e->format;

    if (fmt == 14u || fmt == 8u) {
        return -1; /* not for primary Unicode lookup */
    }
    if (!robo_font_cmap_format_is_recognized(fmt)) {
        return -1;
    }

    /* Prefer modern Unicode over legacy / symbol. */
    if (p == 0u && (enc == 4u || enc == 6u)) {
        score += 1000;
    } else if (p == 3u && enc == 10u) {
        score += 1000;
    } else if (p == 0u && enc == 3u) {
        score += 500;
    } else if (p == 3u && enc == 1u) {
        score += 500;
    } else if (p == 0u && (enc == 0u || enc == 1u || enc == 2u)) {
        score += 400;
    } else if (p == 3u && enc == 0u) {
        score += 200; /* symbol */
    } else if (p == 1u && enc == 0u) {
        score += 100; /* Mac Roman */
    } else {
        score += 10;
    }

    if (fmt == 12u || fmt == 13u) {
        score += 50;
    } else if (fmt == 10u) {
        score += 40;
    } else if (fmt == 4u || fmt == 6u) {
        score += 30;
    } else if (fmt == 0u) {
        score += 10;
    } else if (fmt == 2u) {
        score += 5;
    }
    return score;
}

static int pick_primary(const robo_font_cmap_t *c, uint16_t *out_index)
{
    int best = -1;
    uint16_t best_i = 0u;
    uint16_t i;
    for (i = 0u; i < c->num_tables; i++) {
        int s = encoding_score(&c->encodings[i]);
        if (s > best) {
            best = s;
            best_i = i;
        }
    }
    if (best < 0) {
        return 0;
    }
    *out_index = best_i;
    return 1;
}

static int pick_uvs(const robo_font_cmap_t *c, uint16_t *out_index)
{
    uint16_t i;
    for (i = 0u; i < c->num_tables; i++) {
        if (c->encodings[i].format == 14u) {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}

static robo_result_t lookup_body(const cmap_body_t *b,
                                 uint32_t cp,
                                 uint32_t num_glyphs,
                                 robo_font_cmap_lookup_result_t *out)
{
    switch (b->format) {
    case 0:
        if (cp > 0xFFu) {
            out->status = ROBO_FONT_CMAP_UNMAPPED;
            out->glyph_id = 0u;
            return ROBO_OK;
        }
        return finalize_gid(num_glyphs, b->fmt0_glyphs[cp], out);
    case 2:
        return lookup_fmt2(b, cp, num_glyphs, out);
    case 4:
        return lookup_fmt4(b, cp, num_glyphs, out);
    case 6:
        return lookup_fmt6(b, cp, num_glyphs, out);
    case 10:
        return lookup_fmt10(b, cp, num_glyphs, out);
    case 12:
    case 13:
        return lookup_fmt12_13(b, cp, num_glyphs, out);
    default:
        out->status = ROBO_FONT_CMAP_INVALID_FONT;
        out->glyph_id = 0u;
        return ROBO_OK;
    }
}

static robo_result_t map_build_from_structural(robo_font_cmap_t *structural,
                                               uint32_t num_glyphs,
                                               robo_font_sfnt_t *sfnt,
                                               robo_font_cmap_map_t **out)
{
    robo_font_cmap_map_t *m;
    uint16_t pri = 0u;
    uint16_t uvs = 0u;
    robo_result_t res;

    if (!structural || !out || num_glyphs == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    m = (robo_font_cmap_map_t *)robo_calloc(1u, sizeof(*m));
    if (!m) {
        return ROBO_OUT_OF_MEMORY;
    }
    m->refs = 1u;
    m->num_glyphs = num_glyphs;
    m->structural = *structural;
    memset(structural, 0, sizeof(*structural)); /* ownership moved */
    if (sfnt) {
        robo_font_sfnt_retain(sfnt);
        m->sfnt = sfnt;
    }

    if (pick_primary(&m->structural, &pri)) {
        res = parse_subtable_body(m->structural.table, &m->structural.encodings[pri],
                                  &m->primary);
        if (res != ROBO_OK) {
            robo_font_cmap_map_release(m);
            return res;
        }
        m->has_primary = 1;
    }
    if (pick_uvs(&m->structural, &uvs)) {
        res = parse_subtable_body(m->structural.table, &m->structural.encodings[uvs],
                                  &m->uvs_body);
        if (res != ROBO_OK) {
            robo_font_cmap_map_release(m);
            return res;
        }
        m->has_uvs = 1;
    }
    if (!m->has_primary && !m->has_uvs) {
        robo_font_cmap_map_release(m);
        return ROBO_NOT_FOUND;
    }
    *out = m;
    return ROBO_OK;
}

robo_result_t robo_font_cmap_map_create_from_span(robo_font_span_t cmap_table,
                                                  uint32_t num_glyphs,
                                                  robo_font_cmap_map_t **out)
{
    robo_font_cmap_t structural;
    robo_result_t res;
    memset(&structural, 0, sizeof(structural));
    res = robo_font_cmap_parse(cmap_table, &structural);
    if (res != ROBO_OK) {
        return res;
    }
    return map_build_from_structural(&structural, num_glyphs, NULL, out);
}

robo_result_t robo_font_sfnt_num_glyphs(const robo_font_sfnt_t *sfnt,
                                        uint32_t *out_count)
{
    robo_font_span_t maxp;
    robo_font_maxp_t m;
    if (!sfnt || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_MAXP, &maxp) != ROBO_OK) {
        return ROBO_NOT_FOUND;
    }
    if (robo_font_maxp_parse(maxp, &m) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    *out_count = m.num_glyphs;
    return ROBO_OK;
}

robo_result_t robo_font_cmap_map_create(robo_font_sfnt_t *sfnt,
                                        uint32_t num_glyphs,
                                        robo_font_cmap_map_t **out)
{
    robo_font_span_t cmap;
    robo_font_cmap_t structural;
    robo_result_t res;
    if (!sfnt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (num_glyphs == 0u) {
        if (robo_font_sfnt_num_glyphs(sfnt, &num_glyphs) != ROBO_OK ||
            num_glyphs == 0u) {
            return ROBO_INVALID_ARGUMENT;
        }
    }
    if (robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_CMAP, &cmap) != ROBO_OK) {
        return ROBO_NOT_FOUND;
    }
    memset(&structural, 0, sizeof(structural));
    res = robo_font_cmap_parse(cmap, &structural);
    if (res != ROBO_OK) {
        return res;
    }
    return map_build_from_structural(&structural, num_glyphs, sfnt, out);
}

void robo_font_cmap_map_retain(robo_font_cmap_map_t *map)
{
    if (map) {
        map->refs++;
    }
}

void robo_font_cmap_map_release(robo_font_cmap_map_t *map)
{
    if (!map) {
        return;
    }
    if (map->refs > 1u) {
        map->refs--;
        return;
    }
    clear_body(&map->primary);
    clear_body(&map->uvs_body);
    robo_font_cmap_clear(&map->structural);
    if (map->sfnt) {
        robo_font_sfnt_release(map->sfnt);
    }
    robo_free(map);
}

uint32_t robo_font_cmap_map_num_glyphs(const robo_font_cmap_map_t *map)
{
    return map ? map->num_glyphs : 0u;
}

uint16_t robo_font_cmap_map_selected_format(const robo_font_cmap_map_t *map)
{
    return (map && map->has_primary) ? map->primary.format : 0u;
}

uint16_t robo_font_cmap_map_selected_platform(const robo_font_cmap_map_t *map)
{
    return (map && map->has_primary) ? map->primary.platform : 0u;
}

uint16_t robo_font_cmap_map_selected_encoding(const robo_font_cmap_map_t *map)
{
    return (map && map->has_primary) ? map->primary.encoding : 0u;
}

int robo_font_cmap_map_has_uvs(const robo_font_cmap_map_t *map)
{
    return map && map->has_uvs;
}

robo_result_t robo_font_cmap_lookup(const robo_font_cmap_map_t *map,
                                    uint32_t code_point,
                                    robo_font_cmap_lookup_result_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (!map || !map->has_primary) {
        out->status = ROBO_FONT_CMAP_INVALID_FONT;
        return ROBO_OK;
    }
    if (!robo_font_cmap_is_unicode_scalar(code_point)) {
        out->status = ROBO_FONT_CMAP_INVALID_CODE_POINT;
        return ROBO_OK;
    }
    return lookup_body(&map->primary, code_point, map->num_glyphs, out);
}

robo_glyph_id_t robo_font_cmap_lookup_glyph(const robo_font_cmap_map_t *map,
                                            uint32_t code_point,
                                            robo_font_cmap_lookup_status_t *out_status)
{
    robo_font_cmap_lookup_result_t r;
    if (robo_font_cmap_lookup(map, code_point, &r) != ROBO_OK) {
        if (out_status) {
            *out_status = ROBO_FONT_CMAP_INVALID_FONT;
        }
        return 0u;
    }
    if (out_status) {
        *out_status = r.status;
    }
    return (r.status == ROBO_FONT_CMAP_MAPPED) ? r.glyph_id : 0u;
}

robo_result_t robo_font_cmap_lookup_uvs(const robo_font_cmap_map_t *map,
                                        uint32_t base_code_point,
                                        uint32_t variation_selector,
                                        robo_font_cmap_uvs_result_t *out)
{
    uint32_t i;
    const cmap_uvs_selector_t *sel = NULL;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->base_code_point = base_code_point;
    out->variation_selector = variation_selector;
    if (!map) {
        out->status = ROBO_FONT_CMAP_UVS_INVALID_INPUT;
        return ROBO_OK;
    }
    if (!robo_font_cmap_is_unicode_scalar(base_code_point) ||
        !robo_font_cmap_is_unicode_scalar(variation_selector)) {
        out->status = ROBO_FONT_CMAP_UVS_INVALID_INPUT;
        return ROBO_OK;
    }
    if (!map->has_uvs) {
        out->status = ROBO_FONT_CMAP_UVS_NONE;
        return ROBO_OK;
    }
    for (i = 0u; i < map->uvs_body.uvs_count; i++) {
        if (map->uvs_body.uvs[i].var_selector == variation_selector) {
            sel = &map->uvs_body.uvs[i];
            break;
        }
    }
    if (!sel) {
        out->status = ROBO_FONT_CMAP_UVS_NONE;
        return ROBO_OK;
    }
    /* non-default first */
    {
        uint32_t lo = 0u;
        uint32_t hi = sel->num_non_default;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u;
            if (base_code_point > sel->non_default[mid].unicode) {
                lo = mid + 1u;
            } else {
                hi = mid;
            }
        }
        if (lo < sel->num_non_default &&
            sel->non_default[lo].unicode == base_code_point) {
            uint32_t gid = sel->non_default[lo].glyph_id;
            if (!gid_ok(map->num_glyphs, gid)) {
                out->status = ROBO_FONT_CMAP_UVS_INVALID_INPUT;
                return ROBO_OK;
            }
            out->status = ROBO_FONT_CMAP_UVS_NON_DEFAULT;
            out->glyph_id = gid;
            return ROBO_OK;
        }
    }
    /* default UVS ranges */
    for (i = 0u; i < sel->num_default_ranges; i++) {
        uint32_t start = sel->default_ranges[i].start_unicode;
        uint32_t end = start + (uint32_t)sel->default_ranges[i].additional_count;
        if (base_code_point >= start && base_code_point <= end) {
            out->status = ROBO_FONT_CMAP_UVS_DEFAULT;
            return ROBO_OK;
        }
    }
    out->status = ROBO_FONT_CMAP_UVS_UNSUPPORTED;
    return ROBO_OK;
}

robo_result_t robo_font_cmap_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_cmap_map_t *map = NULL;
    robo_result_t res;
    uint32_t nglyphs = 1u;

    if (!data && size > 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Try as full SFNT first. */
    res = robo_font_source_create_memory_copy(data, size, &src);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_sfnt_parse(src, 0u, 0u, &sfnt);
    if (res == ROBO_OK) {
        (void)robo_font_sfnt_num_glyphs(sfnt, &nglyphs);
        if (nglyphs == 0u) {
            nglyphs = 1u;
        }
        res = robo_font_cmap_map_create(sfnt, nglyphs, &map);
        if (map) {
            robo_font_cmap_lookup_result_t lr;
            (void)robo_font_cmap_lookup(map, 0x41u, &lr);
            robo_font_cmap_map_release(map);
        }
        robo_font_sfnt_release(sfnt);
        robo_font_source_release(src);
        return res == ROBO_OUT_OF_MEMORY ? res : ROBO_OK;
    }
    robo_font_source_release(src);
    /* Raw cmap span */
    {
        robo_font_span_t span = {data, size};
        res = robo_font_cmap_map_create_from_span(span, 256u, &map);
        if (map) {
            robo_font_cmap_map_release(map);
        }
        return res == ROBO_OUT_OF_MEMORY ? res : ROBO_OK;
    }
}
