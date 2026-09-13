/*
 * RoboFont cmap mapping suite (Goal 5.6).
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboTest.h"

#include <string.h>

static void put_u8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static void put_u16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void put_i16(uint8_t **p, int16_t v) { put_u16(p, (uint16_t)v); }
static void put_u32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}

typedef struct {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} synth_table_t;

static void write_search_fields(uint8_t *hdr, uint16_t num_tables)
{
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint8_t *p;
    while ((uint16_t)(pow2 * 2u) <= num_tables) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = hdr + 4;
    put_u16(&p, num_tables);
    put_u16(&p, (uint16_t)(pow2 * 16u));
    put_u16(&p, entry);
    put_u16(&p, (uint16_t)(num_tables * 16u - pow2 * 16u));
}

static size_t build_sfnt(uint8_t *out,
                         size_t out_cap,
                         uint32_t version,
                         const synth_table_t *tables,
                         uint32_t n)
{
    size_t dir_end = 12u + (size_t)n * 16u;
    size_t cursor;
    size_t need = dir_end;
    uint32_t i;
    uint8_t *p;

    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > out_cap) {
        return 0u;
    }
    memset(out, 0, need);
    p = out;
    put_u32(&p, version);
    write_search_fields(out, (uint16_t)n);
    cursor = dir_end;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *rec = out + 12u + (size_t)i * 16u;
        p = rec;
        put_u32(&p, tables[i].tag);
        put_u32(&p, 0u);
        put_u32(&p, (uint32_t)cursor);
        put_u32(&p, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

static size_t build_maxp_v05(uint8_t *out, uint16_t num_glyphs)
{
    uint8_t *p = out;
    put_u32(&p, 0x00005000u);
    put_u16(&p, num_glyphs);
    return (size_t)(p - out);
}

/* cmap header + one encoding record pointing at offset 12 */
static void cmap_begin(uint8_t **p, uint16_t platform, uint16_t encoding)
{
    put_u16(p, 0); /* version */
    put_u16(p, 1); /* numTables */
    put_u16(p, platform);
    put_u16(p, encoding);
    put_u32(p, 12u); /* subtable offset */
}

static void test_cmap_scalar(void)
{
    ROBO_CHECK(robo_font_cmap_is_unicode_scalar(0x41u));
    ROBO_CHECK(robo_font_cmap_is_unicode_scalar(0x10FFFFu));
    ROBO_CHECK(!robo_font_cmap_is_unicode_scalar(0xD800u));
    ROBO_CHECK(!robo_font_cmap_is_unicode_scalar(0xDFFFu));
    ROBO_CHECK(!robo_font_cmap_is_unicode_scalar(0x110000u));
}

static void test_cmap_format0(void)
{
    uint8_t buf[280];
    uint8_t *p = buf;
    robo_font_cmap_map_t *map = NULL;
    robo_font_cmap_lookup_result_t r;
    int i;

    cmap_begin(&p, 1, 0); /* Mac Roman — still lookupable */
    put_u16(&p, 0);       /* format */
    put_u16(&p, 262);     /* length */
    put_u16(&p, 0);       /* language */
    for (i = 0; i < 256; i++) {
        put_u8(&p, (uint8_t)(i == 65 ? 1 : 0));
    }

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, (size_t)(p - buf)},
                                            16u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cmap_map_selected_format(map), 0);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 65u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_MAPPED);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 1);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 66u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_MAPPED);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 0); /* explicit .notdef in format 0 */
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x100u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_UNMAPPED);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0xD800u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_INVALID_CODE_POINT);
    robo_font_cmap_map_release(map);
}

static void test_cmap_format2(void)
{
    uint8_t buf[600];
    uint8_t *p = buf;
    uint8_t *len_ptr;
    uint8_t *sub_start;
    robo_font_cmap_map_t *map = NULL;
    robo_font_cmap_lookup_result_t r;
    int i;

    cmap_begin(&p, 3, 1);
    sub_start = p;
    put_u16(&p, 2);
    len_ptr = p;
    put_u16(&p, 0);
    put_u16(&p, 0);
    for (i = 0; i < 256; i++) {
        put_u16(&p, 0); /* all high bytes → subheader 0 */
    }
    /* subheader 0: map low byte 0x41 via idDelta */
    put_u16(&p, 0x41);
    put_u16(&p, 1);
    put_i16(&p, (int16_t)(4 - 0x41));
    put_u16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - sub_start);
        len_ptr[0] = (uint8_t)(len >> 8);
        len_ptr[1] = (uint8_t)len;
    }

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, (size_t)(p - buf)},
                                            16u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cmap_map_selected_format(map), 2);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x41u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_MAPPED);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 4);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x42u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_UNMAPPED);
    robo_font_cmap_map_release(map);

    /* Range-offset path: glyphIndexArray after one subheader */
    p = buf;
    cmap_begin(&p, 3, 1);
    sub_start = p;
    put_u16(&p, 2);
    len_ptr = p;
    put_u16(&p, 0);
    put_u16(&p, 0);
    for (i = 0; i < 256; i++) {
        put_u16(&p, 0);
    }
    put_u16(&p, 0x41);
    put_u16(&p, 2); /* 'A','B' */
    put_i16(&p, 0);
    put_u16(&p, 2); /* idRangeOffset → next word is glyph array[0] */
    put_u16(&p, 8); /* 'A' → 8 */
    put_u16(&p, 0); /* 'B' unmapped via 0 */
    {
        uint16_t len = (uint16_t)(p - sub_start);
        len_ptr[0] = (uint8_t)(len >> 8);
        len_ptr[1] = (uint8_t)len;
    }
    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, (size_t)(p - buf)},
                                            16u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x41u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 8);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x42u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_UNMAPPED);
    robo_font_cmap_map_release(map);
}

static void test_cmap_format4(void)
{
    uint8_t buf[128];
    uint8_t *p = buf;
    uint8_t *len_ptr;
    robo_font_cmap_map_t *map = NULL;
    robo_font_cmap_lookup_result_t r;

    cmap_begin(&p, 3, 1);
    put_u16(&p, 4);
    len_ptr = p;
    put_u16(&p, 0); /* length placeholder */
    put_u16(&p, 0); /* language */
    put_u16(&p, 4); /* segCountX2 = 2 segments */
    put_u16(&p, 4); /* searchRange */
    put_u16(&p, 1); /* entrySelector */
    put_u16(&p, 0); /* rangeShift */
    put_u16(&p, 0x0041);
    put_u16(&p, 0xFFFF);
    put_u16(&p, 0); /* reservedPad */
    put_u16(&p, 0x0041);
    put_u16(&p, 0xFFFF);
    put_i16(&p, (int16_t)(1 - 0x41)); /* idDelta for A → 1 */
    put_i16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - (buf + 12));
        len_ptr[0] = (uint8_t)(len >> 8);
        len_ptr[1] = (uint8_t)len;
    }

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, (size_t)(p - buf)},
                                            16u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cmap_map_selected_format(map), 4);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x41u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_MAPPED);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 1);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x42u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_UNMAPPED);
    robo_font_cmap_map_release(map);
}

static void test_cmap_format6(void)
{
    uint8_t buf[64];
    uint8_t *p = buf;
    uint8_t *len_ptr;
    robo_font_cmap_map_t *map = NULL;
    robo_font_cmap_lookup_result_t r;

    cmap_begin(&p, 3, 1);
    put_u16(&p, 6);
    len_ptr = p;
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0x40); /* firstCode */
    put_u16(&p, 2);    /* entryCount */
    put_u16(&p, 0);    /* '@' unmapped */
    put_u16(&p, 7);    /* 'A' → 7 */
    {
        uint16_t len = (uint16_t)(p - (buf + 12));
        len_ptr[0] = (uint8_t)(len >> 8);
        len_ptr[1] = (uint8_t)len;
    }

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, (size_t)(p - buf)},
                                            16u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x41u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_MAPPED);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 7);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x40u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_MAPPED);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 0); /* .notdef still mapped */
    robo_font_cmap_map_release(map);
}

static void test_cmap_format10(void)
{
    uint8_t buf[48];
    uint8_t *p = buf;
    uint8_t *len_ptr;
    robo_font_cmap_map_t *map = NULL;
    robo_font_cmap_lookup_result_t r;

    cmap_begin(&p, 3, 10);
    put_u16(&p, 10);
    put_u16(&p, 0); /* reserved */
    len_ptr = p;
    put_u32(&p, 0);
    put_u32(&p, 0); /* language */
    put_u32(&p, 0x1F600u);
    put_u32(&p, 2u);
    put_u16(&p, 11);
    put_u16(&p, 12);
    {
        uint32_t len = (uint32_t)(p - (buf + 12));
        len_ptr[0] = (uint8_t)(len >> 24);
        len_ptr[1] = (uint8_t)(len >> 16);
        len_ptr[2] = (uint8_t)(len >> 8);
        len_ptr[3] = (uint8_t)len;
    }

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, (size_t)(p - buf)},
                                            32u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cmap_map_selected_format(map), 10);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x1F600u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_MAPPED);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 11);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x1F601u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 12);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x41u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_UNMAPPED);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0xD800u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_INVALID_CODE_POINT);
    robo_font_cmap_map_release(map);

    /* Overflowing numChars past U+10FFFF */
    {
        uint8_t bad[40];
        uint8_t *q = bad;
        uint8_t *lp;
        cmap_begin(&q, 3, 10);
        put_u16(&q, 10);
        put_u16(&q, 0);
        lp = q;
        put_u32(&q, 0);
        put_u32(&q, 0);
        put_u32(&q, 0x10FFFEu);
        put_u32(&q, 3u); /* end past plane */
        put_u16(&q, 1);
        put_u16(&q, 2);
        put_u16(&q, 3);
        {
            uint32_t len = (uint32_t)(q - (bad + 12));
            lp[0] = (uint8_t)(len >> 24);
            lp[1] = (uint8_t)(len >> 16);
            lp[2] = (uint8_t)(len >> 8);
            lp[3] = (uint8_t)len;
        }
        ROBO_CHECK(robo_font_cmap_map_create_from_span(
                       (robo_font_span_t){bad, (size_t)(q - bad)}, 16u, &map) !=
                   ROBO_OK);
    }
}

static void test_cmap_format12(void)
{
    uint8_t buf[80];
    uint8_t *p = buf;
    uint8_t *len_ptr;
    robo_font_cmap_map_t *map = NULL;
    robo_font_cmap_lookup_result_t r;

    cmap_begin(&p, 3, 10);
    put_u16(&p, 12);
    put_u16(&p, 0); /* reserved */
    len_ptr = p;
    put_u32(&p, 0); /* length */
    put_u32(&p, 0); /* language */
    put_u32(&p, 2); /* nGroups */
    put_u32(&p, 0x41u);
    put_u32(&p, 0x42u);
    put_u32(&p, 1u); /* A→1, B→2 */
    put_u32(&p, 0x1F600u);
    put_u32(&p, 0x1F600u);
    put_u32(&p, 10u);
    {
        uint32_t len = (uint32_t)(p - (buf + 12));
        len_ptr[0] = (uint8_t)(len >> 24);
        len_ptr[1] = (uint8_t)(len >> 16);
        len_ptr[2] = (uint8_t)(len >> 8);
        len_ptr[3] = (uint8_t)len;
    }

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, (size_t)(p - buf)},
                                            32u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cmap_map_selected_format(map), 12);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x41u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 1);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x42u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 2);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x1F600u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_MAPPED);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 10);
    robo_font_cmap_map_release(map);
}

static void test_cmap_format13(void)
{
    uint8_t buf[64];
    uint8_t *p = buf;
    uint8_t *len_ptr;
    robo_font_cmap_map_t *map = NULL;
    robo_font_cmap_lookup_result_t r;

    cmap_begin(&p, 3, 10);
    put_u16(&p, 13);
    put_u16(&p, 0);
    len_ptr = p;
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 1);
    put_u32(&p, 0x1000u);
    put_u32(&p, 0x1002u);
    put_u32(&p, 5u); /* all → glyph 5 */
    {
        uint32_t len = (uint32_t)(p - (buf + 12));
        len_ptr[0] = (uint8_t)(len >> 24);
        len_ptr[1] = (uint8_t)(len >> 16);
        len_ptr[2] = (uint8_t)(len >> 8);
        len_ptr[3] = (uint8_t)len;
    }

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, (size_t)(p - buf)},
                                            16u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x1001u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 5);
    robo_font_cmap_map_release(map);
}

static void test_cmap_format14(void)
{
    uint8_t c[160];
    uint8_t *q = c;
    uint32_t f4_off;
    uint32_t f14_off;
    uint32_t f14_start;
    uint32_t def_off_rel;
    uint8_t *f14_len_ptr;
    uint8_t *f4_len_ptr;
    robo_font_cmap_map_t *map = NULL;
    robo_font_cmap_uvs_result_t u;

    put_u16(&q, 0);
    put_u16(&q, 2);
    put_u16(&q, 3);
    put_u16(&q, 1);
    put_u32(&q, 0);
    put_u16(&q, 0);
    put_u16(&q, 5);
    put_u32(&q, 0);

    f4_off = (uint32_t)(q - c);
    c[8] = (uint8_t)(f4_off >> 24);
    c[9] = (uint8_t)(f4_off >> 16);
    c[10] = (uint8_t)(f4_off >> 8);
    c[11] = (uint8_t)f4_off;

    put_u16(&q, 4);
    f4_len_ptr = q;
    put_u16(&q, 0);
    put_u16(&q, 0);
    put_u16(&q, 4);
    put_u16(&q, 4);
    put_u16(&q, 1);
    put_u16(&q, 0);
    put_u16(&q, 0x41);
    put_u16(&q, 0xFFFF);
    put_u16(&q, 0);
    put_u16(&q, 0x41);
    put_u16(&q, 0xFFFF);
    put_i16(&q, (int16_t)(1 - 0x41));
    put_i16(&q, 1);
    put_u16(&q, 0);
    put_u16(&q, 0);
    {
        uint16_t len = (uint16_t)(q - (c + f4_off));
        f4_len_ptr[0] = (uint8_t)(len >> 8);
        f4_len_ptr[1] = (uint8_t)len;
    }

    f14_off = (uint32_t)(q - c);
    c[16] = (uint8_t)(f14_off >> 24);
    c[17] = (uint8_t)(f14_off >> 16);
    c[18] = (uint8_t)(f14_off >> 8);
    c[19] = (uint8_t)f14_off;
    f14_start = f14_off;
    put_u16(&q, 14);
    f14_len_ptr = q;
    put_u32(&q, 0);
    put_u32(&q, 1);
    put_u8(&q, 0x00);
    put_u8(&q, 0xFE);
    put_u8(&q, 0x0F);
    {
        uint8_t *offp = q;
        put_u32(&q, 0);
        put_u32(&q, 0);
        def_off_rel = (uint32_t)(q - (c + f14_start));
        offp[0] = (uint8_t)(def_off_rel >> 24);
        offp[1] = (uint8_t)(def_off_rel >> 16);
        offp[2] = (uint8_t)(def_off_rel >> 8);
        offp[3] = (uint8_t)def_off_rel;
        put_u32(&q, 1);
        put_u8(&q, 0x00);
        put_u8(&q, 0x00);
        put_u8(&q, 0x41);
        put_u8(&q, 0);
        {
            uint32_t len = (uint32_t)(q - (c + f14_start));
            f14_len_ptr[0] = (uint8_t)(len >> 24);
            f14_len_ptr[1] = (uint8_t)(len >> 16);
            f14_len_ptr[2] = (uint8_t)(len >> 8);
            f14_len_ptr[3] = (uint8_t)len;
        }
    }

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){c, (size_t)(q - c)}, 16u,
                                            &map),
        ROBO_OK);
    ROBO_CHECK(robo_font_cmap_map_has_uvs(map));
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup_uvs(map, 0x41u, 0xFE0Fu, &u), ROBO_OK);
    ROBO_CHECK_EQ_INT(u.status, ROBO_FONT_CMAP_UVS_DEFAULT);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup_uvs(map, 0x42u, 0xFE0Fu, &u), ROBO_OK);
    ROBO_CHECK_EQ_INT(u.status, ROBO_FONT_CMAP_UVS_UNSUPPORTED);
    robo_font_cmap_map_release(map);
}

static void test_cmap_selection_and_gid_validate(void)
{
    /* Prefer format 12 (3,10) over format 4 (3,1) */
    uint8_t buf[160];
    uint8_t *p = buf;
    uint32_t f4_off;
    uint32_t f12_off;
    uint8_t *f4_len;
    uint8_t *f12_len;
    robo_font_cmap_map_t *map = NULL;
    robo_font_cmap_lookup_result_t r;

    put_u16(&p, 0);
    put_u16(&p, 2);
    put_u16(&p, 3);
    put_u16(&p, 1);
    put_u32(&p, 0);
    put_u16(&p, 3);
    put_u16(&p, 10);
    put_u32(&p, 0);

    f4_off = (uint32_t)(p - buf);
    buf[8] = (uint8_t)(f4_off >> 24);
    buf[9] = (uint8_t)(f4_off >> 16);
    buf[10] = (uint8_t)(f4_off >> 8);
    buf[11] = (uint8_t)f4_off;
    put_u16(&p, 4);
    f4_len = p;
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 4);
    put_u16(&p, 4);
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 0x41);
    put_u16(&p, 0xFFFF);
    put_u16(&p, 0);
    put_u16(&p, 0x41);
    put_u16(&p, 0xFFFF);
    put_i16(&p, (int16_t)(2 - 0x41)); /* would map to 2 if selected */
    put_i16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - (buf + f4_off));
        f4_len[0] = (uint8_t)(len >> 8);
        f4_len[1] = (uint8_t)len;
    }

    f12_off = (uint32_t)(p - buf);
    buf[16] = (uint8_t)(f12_off >> 24);
    buf[17] = (uint8_t)(f12_off >> 16);
    buf[18] = (uint8_t)(f12_off >> 8);
    buf[19] = (uint8_t)f12_off;
    put_u16(&p, 12);
    put_u16(&p, 0);
    f12_len = p;
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 1);
    put_u32(&p, 0x41u);
    put_u32(&p, 0x41u);
    put_u32(&p, 9u); /* preferred mapping */
    {
        uint32_t len = (uint32_t)(p - (buf + f12_off));
        f12_len[0] = (uint8_t)(len >> 24);
        f12_len[1] = (uint8_t)(len >> 16);
        f12_len[2] = (uint8_t)(len >> 8);
        f12_len[3] = (uint8_t)len;
    }

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, (size_t)(p - buf)},
                                            16u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cmap_map_selected_format(map), 12);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x41u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 9);

    /* Out-of-range GID */
    robo_font_cmap_map_release(map);
    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, (size_t)(p - buf)},
                                            5u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cmap_lookup(map, 0x41u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.status, ROBO_FONT_CMAP_INVALID_FONT);
    robo_font_cmap_map_release(map);
}

static void test_cmap_face_and_fuzz(void)
{
    uint8_t maxp[8];
    uint8_t cmap[96];
    uint8_t font[256];
    uint8_t *p = cmap;
    uint8_t *len_ptr;
    size_t n;
    synth_table_t tabs[2];
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_cmap_lookup_result_t r;
    size_t maxp_len = build_maxp_v05(maxp, 16);

    cmap_begin(&p, 3, 1);
    put_u16(&p, 4);
    len_ptr = p;
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 4);
    put_u16(&p, 4);
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 0x41);
    put_u16(&p, 0xFFFF);
    put_u16(&p, 0);
    put_u16(&p, 0x41);
    put_u16(&p, 0xFFFF);
    put_i16(&p, (int16_t)(3 - 0x41));
    put_i16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - (cmap + 12));
        len_ptr[0] = (uint8_t)(len >> 8);
        len_ptr[1] = (uint8_t)len;
    }

    tabs[0].tag = ROBO_FONT_TAG_MAXP;
    tabs[0].data = maxp;
    tabs[0].length = (uint32_t)maxp_len;
    tabs[1].tag = ROBO_FONT_TAG_CMAP;
    tabs[1].data = cmap;
    tabs[1].length = (uint32_t)(p - cmap);
    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, 2u);
    ROBO_CHECK(n > 0u);

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_cmap(face), ROBO_OK);
    ROBO_CHECK(robo_font_face_cmap(face) != NULL);
    ROBO_CHECK_EQ_INT(robo_font_face_lookup_glyph(face, 0x41u, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)r.glyph_id, 3);
    {
        uint32_t gc = 0;
        ROBO_CHECK_EQ_INT(robo_font_face_glyph_count(face, &gc), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)gc, 16);
    }

    ROBO_CHECK_EQ_INT(robo_font_cmap_fuzz_parse(font, n), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cmap_fuzz_parse((const uint8_t *)"xxxx", 4), ROBO_OK);

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_cmap_malformed(void)
{
    uint8_t bad[] = {0, 0, 0, 1, 0, 3, 0, 1, 0, 0, 0, 100}; /* offset OOB */
    robo_font_cmap_map_t *map = NULL;
    ROBO_CHECK(robo_font_cmap_map_create_from_span(
                   (robo_font_span_t){bad, sizeof(bad)}, 16u, &map) != ROBO_OK);

    /* overlapping format 12 groups */
    {
        uint8_t buf[64];
        uint8_t *p = buf;
        uint8_t *len_ptr;
        cmap_begin(&p, 3, 10);
        put_u16(&p, 12);
        put_u16(&p, 0);
        len_ptr = p;
        put_u32(&p, 0);
        put_u32(&p, 0);
        put_u32(&p, 2);
        put_u32(&p, 10);
        put_u32(&p, 20);
        put_u32(&p, 1);
        put_u32(&p, 15); /* overlaps */
        put_u32(&p, 25);
        put_u32(&p, 2);
        {
            uint32_t len = (uint32_t)(p - (buf + 12));
            len_ptr[0] = (uint8_t)(len >> 24);
            len_ptr[1] = (uint8_t)(len >> 16);
            len_ptr[2] = (uint8_t)(len >> 8);
            len_ptr[3] = (uint8_t)len;
        }
        ROBO_CHECK(robo_font_cmap_map_create_from_span(
                       (robo_font_span_t){buf, (size_t)(p - buf)}, 16u, &map) !=
                   ROBO_OK);
    }
}

void robo_test_register_font_cmap(void)
{
    robo_test_register("font.cmap.scalar", test_cmap_scalar);
    robo_test_register("font.cmap.format0", test_cmap_format0);
    robo_test_register("font.cmap.format2", test_cmap_format2);
    robo_test_register("font.cmap.format4", test_cmap_format4);
    robo_test_register("font.cmap.format6", test_cmap_format6);
    robo_test_register("font.cmap.format10", test_cmap_format10);
    robo_test_register("font.cmap.format12", test_cmap_format12);
    robo_test_register("font.cmap.format13", test_cmap_format13);
    robo_test_register("font.cmap.format14", test_cmap_format14);
    robo_test_register("font.cmap.selection", test_cmap_selection_and_gid_validate);
    robo_test_register("font.cmap.face", test_cmap_face_and_fuzz);
    robo_test_register("font.cmap.malformed", test_cmap_malformed);
}
