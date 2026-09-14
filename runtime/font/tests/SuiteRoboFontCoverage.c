/*
 * RoboFont coverage suite (Goal 5.14).
 * Synthetic cmap fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdio.h>
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

static void cmap_begin(uint8_t **p, uint16_t platform, uint16_t encoding)
{
    put_u16(p, 0);
    put_u16(p, 1);
    put_u16(p, platform);
    put_u16(p, encoding);
    put_u32(p, 12u);
}

static size_t build_fmt4_single(uint8_t *buf, uint16_t cp, uint16_t gid)
{
    uint8_t *p = buf;
    uint8_t *len_ptr;
    cmap_begin(&p, 3, 1);
    put_u16(&p, 4);
    len_ptr = p;
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 4);
    put_u16(&p, 4);
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, cp);
    put_u16(&p, 0xFFFF);
    put_u16(&p, 0);
    put_u16(&p, cp);
    put_u16(&p, 0xFFFF);
    put_i16(&p, (int16_t)((int)gid - (int)cp));
    put_i16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - (buf + 12));
        len_ptr[0] = (uint8_t)(len >> 8);
        len_ptr[1] = (uint8_t)len;
    }
    return (size_t)(p - buf);
}

static size_t build_fmt4_range(uint8_t *buf, uint16_t first, uint16_t last,
                               uint16_t start_gid)
{
    uint8_t *p = buf;
    uint8_t *len_ptr;
    cmap_begin(&p, 3, 1);
    put_u16(&p, 4);
    len_ptr = p;
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 4);
    put_u16(&p, 4);
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, last);
    put_u16(&p, 0xFFFF);
    put_u16(&p, 0);
    put_u16(&p, first);
    put_u16(&p, 0xFFFF);
    put_i16(&p, (int16_t)((int)start_gid - (int)first));
    put_i16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - (buf + 12));
        len_ptr[0] = (uint8_t)(len >> 8);
        len_ptr[1] = (uint8_t)len;
    }
    return (size_t)(p - buf);
}

static size_t build_fmt12_groups(uint8_t *buf, const uint32_t *starts,
                                 const uint32_t *ends, const uint32_t *gids,
                                 uint32_t n)
{
    uint8_t *p = buf;
    uint8_t *len_ptr;
    uint32_t i;
    cmap_begin(&p, 3, 10);
    put_u16(&p, 12);
    put_u16(&p, 0);
    len_ptr = p;
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, n);
    for (i = 0u; i < n; i++) {
        put_u32(&p, starts[i]);
        put_u32(&p, ends[i]);
        put_u32(&p, gids[i]);
    }
    {
        uint32_t len = (uint32_t)(p - (buf + 12));
        len_ptr[0] = (uint8_t)(len >> 24);
        len_ptr[1] = (uint8_t)(len >> 16);
        len_ptr[2] = (uint8_t)(len >> 8);
        len_ptr[3] = (uint8_t)len;
    }
    return (size_t)(p - buf);
}

static void test_coverage_unicode_scalars(void)
{
    uint8_t buf[128];
    size_t n = build_fmt4_single(buf, 0x41u, 1u);
    robo_font_cmap_map_t *map = NULL;
    robo_font_coverage_t *cov = NULL;
    robo_font_coverage_query_result_t q;

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, n}, 16u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_coverage_create_from_cmap(map, NULL, &cov), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_font_coverage_query(cov, 0x41u, &q), ROBO_OK);
    ROBO_CHECK_EQ_INT(q.status, ROBO_FONT_COVERAGE_MAPPED_USABLE);
    ROBO_CHECK(robo_font_coverage_contains(cov, 0x41u));
    ROBO_CHECK(!robo_font_coverage_contains(cov, 0xD800u));
    ROBO_CHECK_EQ_INT(robo_font_coverage_query(cov, 0xD800u, &q), ROBO_OK);
    ROBO_CHECK_EQ_INT(q.status, ROBO_FONT_COVERAGE_INVALID_CODE_POINT);
    ROBO_CHECK_EQ_INT(robo_font_coverage_query(cov, 0x110000u, &q), ROBO_OK);
    ROBO_CHECK_EQ_INT(q.status, ROBO_FONT_COVERAGE_INVALID_CODE_POINT);
    ROBO_CHECK_EQ_INT(robo_font_coverage_query(cov, 0x10FFFFu, &q), ROBO_OK);
    ROBO_CHECK_EQ_INT(q.status, ROBO_FONT_COVERAGE_UNSUPPORTED);

    robo_font_coverage_release(cov);
    robo_font_cmap_map_release(map);
}

static void test_coverage_notdef_and_ranges(void)
{
    uint8_t buf[64];
    uint8_t *p = buf;
    uint8_t *len_ptr;
    robo_font_cmap_map_t *map = NULL;
    robo_font_coverage_t *cov = NULL;
    const robo_font_coverage_stats_t *st;
    const robo_font_coverage_range_t *r;
    robo_font_coverage_query_result_t q;
    robo_glyph_id_t gid = 99u;

    cmap_begin(&p, 3, 1);
    put_u16(&p, 6);
    len_ptr = p;
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0x40);
    put_u16(&p, 3);
    put_u16(&p, 0); /* '@' → .notdef */
    put_u16(&p, 7); /* 'A' */
    put_u16(&p, 8); /* 'B' */
    {
        uint16_t len = (uint16_t)(p - (buf + 12));
        len_ptr[0] = (uint8_t)(len >> 8);
        len_ptr[1] = (uint8_t)len;
    }

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, (size_t)(p - buf)},
                                            16u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_coverage_create_from_cmap(map, NULL, &cov), ROBO_OK);
    st = robo_font_coverage_stats(cov);
    ROBO_CHECK(st != NULL);
    ROBO_CHECK_EQ_INT((int)st->mapped_scalars, 3);
    ROBO_CHECK_EQ_INT((int)st->usable_mappings, 2);
    ROBO_CHECK_EQ_INT((int)st->notdef_mappings, 1);
    ROBO_CHECK_EQ_INT((int)st->range_count, 1);
    r = robo_font_coverage_range(cov, 0);
    ROBO_CHECK(r != NULL);
    ROBO_CHECK_EQ_INT((int)r->first, 0x41);
    ROBO_CHECK_EQ_INT((int)r->last, 0x42);
    ROBO_CHECK(!robo_font_coverage_contains(cov, 0x40u));
    ROBO_CHECK_EQ_INT(robo_font_coverage_query(cov, 0x40u, &q), ROBO_OK);
    ROBO_CHECK_EQ_INT(q.status, ROBO_FONT_COVERAGE_MAPPED_NOTDEF);
    ROBO_CHECK_EQ_INT(robo_font_coverage_glyph(cov, 0x40u, &gid), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)gid, 0);
    ROBO_CHECK_EQ_INT(robo_font_coverage_glyph(cov, 0x41u, &gid), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)gid, 7);

    robo_font_coverage_release(cov);
    robo_font_cmap_map_release(map);
}

static void test_coverage_sparse_merge(void)
{
    uint8_t buf[128];
    size_t n;
    uint8_t *p;
    uint8_t *len_ptr;
    robo_font_cmap_map_t *map = NULL;
    robo_font_coverage_t *cov = NULL;
    const robo_font_coverage_stats_t *st;

    /* Two non-contiguous singles via format 4 segments: A and C */
    p = buf;
    cmap_begin(&p, 3, 1);
    put_u16(&p, 4);
    len_ptr = p;
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 6); /* 3 segs */
    put_u16(&p, 4);
    put_u16(&p, 1);
    put_u16(&p, 2);
    put_u16(&p, 0x41);
    put_u16(&p, 0x43);
    put_u16(&p, 0xFFFF);
    put_u16(&p, 0);
    put_u16(&p, 0x41);
    put_u16(&p, 0x43);
    put_u16(&p, 0xFFFF);
    put_i16(&p, (int16_t)(1 - 0x41));
    put_i16(&p, (int16_t)(2 - 0x43));
    put_i16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - (buf + 12));
        len_ptr[0] = (uint8_t)(len >> 8);
        len_ptr[1] = (uint8_t)len;
    }
    n = (size_t)(p - buf);

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, n}, 16u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_coverage_create_from_cmap(map, NULL, &cov), ROBO_OK);
    st = robo_font_coverage_stats(cov);
    ROBO_CHECK_EQ_INT((int)st->usable_mappings, 2);
    ROBO_CHECK_EQ_INT((int)st->range_count, 2);
    ROBO_CHECK(robo_font_coverage_contains(cov, 0x41u));
    ROBO_CHECK(!robo_font_coverage_contains(cov, 0x42u));
    ROBO_CHECK(robo_font_coverage_contains(cov, 0x43u));
    /* Must not claim whole Latin block from one letter. */
    ROBO_CHECK(!robo_font_coverage_contains(cov, 0x7Au));

    robo_font_coverage_release(cov);
    robo_font_cmap_map_release(map);
}

static void test_coverage_scripts(void)
{
    struct {
        uint16_t cp;
        robo_unicode_script_t script;
    } cases[] = {
        {0x0041u, ROBO_U_SCRIPT_LATIN},
        {0x0410u, ROBO_U_SCRIPT_CYRILLIC},
        {0x0391u, ROBO_U_SCRIPT_GREEK},
        {0x0627u, ROBO_U_SCRIPT_ARABIC},
        {0x05D0u, ROBO_U_SCRIPT_HEBREW},
        {0x0905u, ROBO_U_SCRIPT_DEVANAGARI},
        {0x0985u, ROBO_U_SCRIPT_BENGALI},
        {0x0E01u, ROBO_U_SCRIPT_THAI},
    };
    size_t i;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t buf[128];
        size_t n = build_fmt4_single(buf, cases[i].cp, 1u);
        robo_font_cmap_map_t *map = NULL;
        robo_font_coverage_t *cov = NULL;
        ROBO_CHECK_EQ_INT(
            robo_font_cmap_map_create_from_span((robo_font_span_t){buf, n}, 16u, &map),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_coverage_create_from_cmap(map, NULL, &cov),
                          ROBO_OK);
        ROBO_CHECK(robo_font_coverage_has_script(cov, cases[i].script));
        /* Script coverage is not language support. */
        ROBO_CHECK(robo_font_coverage_script_count(cov) >= 1u);
        robo_font_coverage_release(cov);
        robo_font_cmap_map_release(map);
    }
}

static void test_coverage_cjk_supplementary(void)
{
    uint8_t buf[96];
    uint32_t starts[2] = {0x4E00u, 0x1F600u};
    uint32_t ends[2] = {0x4E05u, 0x1F601u};
    uint32_t gids[2] = {1u, 10u};
    size_t n = build_fmt12_groups(buf, starts, ends, gids, 2u);
    robo_font_cmap_map_t *map = NULL;
    robo_font_coverage_t *cov = NULL;
    const robo_font_coverage_stats_t *st;
    robo_glyph_id_t gid = 0u;

    ROBO_CHECK_EQ_INT(
        robo_font_cmap_map_create_from_span((robo_font_span_t){buf, n}, 32u, &map),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_coverage_create_from_cmap(map, NULL, &cov), ROBO_OK);
    st = robo_font_coverage_stats(cov);
    ROBO_CHECK_EQ_INT((int)st->usable_mappings, 8); /* 6 Han + 2 emoji */
    ROBO_CHECK(robo_font_coverage_contains(cov, 0x4E00u));
    ROBO_CHECK(robo_font_coverage_contains(cov, 0x4E05u));
    ROBO_CHECK(!robo_font_coverage_contains(cov, 0x4E06u));
    ROBO_CHECK(robo_font_coverage_has_script(cov, ROBO_U_SCRIPT_HAN));
    ROBO_CHECK_EQ_INT(robo_font_coverage_glyph(cov, 0x1F600u, &gid), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)gid, 10);
    ROBO_CHECK(st->emoji_scalars >= 1u);
    ROBO_CHECK_EQ_INT((int)st->min_code_point, 0x4E00);
    ROBO_CHECK_EQ_INT((int)st->max_code_point, 0x1F601);

    robo_font_coverage_release(cov);
    robo_font_cmap_map_release(map);
}

static void test_coverage_uvs(void)
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
    robo_font_coverage_t *cov = NULL;
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
    ROBO_CHECK_EQ_INT(robo_font_coverage_create_from_cmap(map, NULL, &cov), ROBO_OK);
    ROBO_CHECK(robo_font_coverage_stats(cov)->has_uvs);
    ROBO_CHECK(robo_font_coverage_contains(cov, 0x41u));
    ROBO_CHECK_EQ_INT(robo_font_coverage_query_uvs(cov, 0x41u, 0xFE0Fu, &u), ROBO_OK);
    ROBO_CHECK_EQ_INT(u.status, ROBO_FONT_CMAP_UVS_DEFAULT);
    ROBO_CHECK_EQ_INT(robo_font_coverage_query_uvs(cov, 0x42u, 0xFE0Fu, &u), ROBO_OK);
    ROBO_CHECK_EQ_INT(u.status, ROBO_FONT_CMAP_UVS_UNSUPPORTED);
    ROBO_CHECK_EQ_INT(robo_font_coverage_query_uvs(cov, 0x41u, 0xFE0Eu, &u), ROBO_OK);
    ROBO_CHECK_EQ_INT(u.status, ROBO_FONT_CMAP_UVS_NONE);

    robo_font_coverage_release(cov);
    robo_font_cmap_map_release(map);
}

static void test_coverage_face_lazy_and_bench(void)
{
    uint8_t sfnt[512];
    uint8_t maxp[6];
    uint8_t cmap[128];
    uint8_t head[54];
    size_t cmap_n;
    size_t i;
    typedef struct {
        uint32_t tag;
        const uint8_t *data;
        uint32_t length;
    } synth_table_t;
    uint8_t *p;
    size_t dir_end;
    size_t cursor;
    synth_table_t tables[3];

    memset(head, 0, sizeof(head));
    p = head;
    put_u32(&p, 0x00010000u);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0x5F0F3CF5u);
    put_u16(&p, 0x0800);
    put_u16(&p, 2048);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_i16(&p, 0);
    put_u16(&p, 0);
    put_i16(&p, 0);

    p = maxp;
    put_u32(&p, 0x00005000u);
    put_u16(&p, 128); /* enough GIDs for U+0020..U+007E → 1..95 */

    cmap_n = build_fmt4_range(cmap, 0x20u, 0x7Eu, 1u);
    tables[0].tag = 0x68656164u;
    tables[0].data = head;
    tables[0].length = 54u;
    tables[1].tag = 0x6D617870u;
    tables[1].data = maxp;
    tables[1].length = 6u;
    tables[2].tag = 0x636D6170u;
    tables[2].data = cmap;
    tables[2].length = (uint32_t)cmap_n;

    dir_end = 12u + 3u * 16u;
    cursor = dir_end;
    memset(sfnt, 0, sizeof(sfnt));
    p = sfnt;
    put_u32(&p, 0x00010000u);
    put_u16(&p, 3);
    put_u16(&p, 32);
    put_u16(&p, 1);
    put_u16(&p, 16);
    for (i = 0u; i < 3u; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *rec = sfnt + 12u + i * 16u;
        p = rec;
        put_u32(&p, tables[i].tag);
        put_u32(&p, 0u);
        put_u32(&p, (uint32_t)cursor);
        put_u32(&p, tables[i].length);
        memcpy(sfnt + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }

    {
        robo_font_source_t *src = NULL;
        robo_font_face_t *face = NULL;
        const robo_font_coverage_t *cov;
        volatile int hits = 0;
        ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(sfnt, cursor, &src),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
        ROBO_CHECK(robo_font_face_coverage(face) == NULL);
        ROBO_CHECK_EQ_INT(robo_font_face_parse_coverage(face), ROBO_OK);
        cov = robo_font_face_coverage(face);
        ROBO_CHECK(cov != NULL);
        ROBO_CHECK(robo_font_coverage_contains(cov, 0x41u));
        ROBO_CHECK_EQ_INT(robo_font_face_parse_coverage(face), ROBO_OK);

        for (i = 0u; i < 10000u; i++) {
            hits += robo_font_coverage_contains(cov, (uint32_t)(0x20u + (i % 95u)));
        }
        ROBO_CHECK(hits > 0);
        printf("font.coverage.bench.lookup hits=%d\n", hits);

        robo_font_face_release(face);
        robo_font_source_release(src);
    }
}

static void test_coverage_fuzz_edge(void)
{
    static const uint8_t junk[] = {0x00, 0x01, 0xFF, 0xFE, 0x41, 0x00};
    uint8_t empty_cmap[16];
    uint8_t *p = empty_cmap;
    robo_font_cmap_map_t *map = NULL;
    robo_font_coverage_t *cov = NULL;

    ROBO_CHECK_EQ_INT(robo_font_coverage_fuzz_parse(junk, sizeof(junk)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_coverage_fuzz_parse(NULL, 0u), ROBO_OK);

    /* Header claiming one table but no body — create may fail safely. */
    put_u16(&p, 0);
    put_u16(&p, 1);
    put_u16(&p, 3);
    put_u16(&p, 1);
    put_u32(&p, 12u);
    (void)robo_font_cmap_map_create_from_span(
        (robo_font_span_t){empty_cmap, sizeof(empty_cmap)}, 16u, &map);
    if (map) {
        (void)robo_font_coverage_create_from_cmap(map, NULL, &cov);
        if (cov) {
            robo_font_coverage_release(cov);
        }
        robo_font_cmap_map_release(map);
    }
}

void robo_test_register_font_coverage(void)
{
    robo_test_register("font.coverage.unicode", test_coverage_unicode_scalars);
    robo_test_register("font.coverage.notdef_ranges", test_coverage_notdef_and_ranges);
    robo_test_register("font.coverage.sparse", test_coverage_sparse_merge);
    robo_test_register("font.coverage.scripts", test_coverage_scripts);
    robo_test_register("font.coverage.cjk_emoji", test_coverage_cjk_supplementary);
    robo_test_register("font.coverage.uvs", test_coverage_uvs);
    robo_test_register("font.coverage.face_bench", test_coverage_face_lazy_and_bench);
    robo_test_register("font.coverage.fuzz", test_coverage_fuzz_edge);
}
