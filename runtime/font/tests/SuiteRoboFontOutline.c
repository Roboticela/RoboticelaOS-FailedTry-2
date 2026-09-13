/*
 * RoboFont outline suite (Goal 5.8).
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
static void put_i64(uint8_t **p, int64_t v)
{
    int i;
    for (i = 7; i >= 0; i--) {
        put_u8(p, (uint8_t)((uint64_t)v >> (i * 8)));
    }
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

static size_t build_sfnt(uint8_t *out, size_t out_cap, uint32_t version,
                         const synth_table_t *tables, uint32_t n)
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

static size_t build_head(uint8_t *b, uint16_t upem)
{
    uint8_t *p = b;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u32(&p, 0x00010000u);
    put_u32(&p, 0u);
    put_u32(&p, ROBO_FONT_HEAD_MAGIC);
    put_u16(&p, 0u);
    put_u16(&p, upem);
    put_i64(&p, 0);
    put_i64(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 100);
    put_i16(&p, 100);
    put_u16(&p, 0u);
    put_u16(&p, 8u);
    put_i16(&p, 2);
    put_i16(&p, 1);
    put_i16(&p, 0);
    return (size_t)(p - b);
}

static size_t build_hhea(uint8_t *b, uint16_t n_h)
{
    uint8_t *p = b;
    int i;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_i16(&p, 800);
    put_i16(&p, -200);
    put_i16(&p, 0);
    put_u16(&p, 1000);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 100);
    put_i16(&p, 1);
    put_i16(&p, 0);
    put_i16(&p, 0);
    for (i = 0; i < 4; i++) {
        put_i16(&p, 0);
    }
    put_i16(&p, 0);
    put_u16(&p, n_h);
    return (size_t)(p - b);
}

static size_t build_maxp(uint8_t *b, uint16_t ng)
{
    uint8_t *p = b;
    put_u32(&p, 0x00010000u);
    put_u16(&p, ng);
    put_u16(&p, 8);
    put_u16(&p, 2);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 2);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 4);
    put_u16(&p, 4);
    return (size_t)(p - b);
}

static size_t build_hmtx(uint8_t *b, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i < n; i++) {
        put_u16(&p, 200);
        put_i16(&p, 0);
    }
    return (size_t)(p - b);
}

/* One-contour square: on-curve corners only → lines */
static size_t build_square_glyf(uint8_t *b)
{
    uint8_t *p = b;
    put_i16(&p, 1);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 100);
    put_i16(&p, 100);
    put_u16(&p, 3);
    put_u16(&p, 0);
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                          ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_Y_SHORT |
                          ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, 100);
    put_u8(&p, 100);
    put_u8(&p, 100);
    return (size_t)(p - b);
}

/* Contour with consecutive off-curve points → implied midpoints / quads */
static size_t build_off_curve_glyf(uint8_t *b)
{
    uint8_t *p = b;
    /* 3 points: on, off, off — with wrap will insert mids */
    put_i16(&p, 1);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 100);
    put_i16(&p, 100);
    put_u16(&p, 2); /* endPts last index 2 */
    put_u16(&p, 0);
    /* p0 on (0,0), p1 off (50,100), p2 off (100,0) — absolute via deltas */
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_X_SHORT | ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SHORT | ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_X_SHORT | ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SHORT)); /* y negative */
    put_u8(&p, 50);  /* x +50 */
    put_u8(&p, 50);  /* x +50 → 100 */
    put_u8(&p, 100); /* y +100 */
    put_u8(&p, 100); /* y -100 → 0 */
    return (size_t)(p - b);
}

static void test_outline_tt_simple(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], loca[16], glyf[64];
    uint8_t font[384];
    synth_table_t tabs[6];
    size_t gn = build_square_glyf(glyf);
    uint8_t *lp = loca;
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_outline_t *o = NULL;
    char dump[1024];
    robo_font_bbox_t bb;

    put_u32(&lp, 0);
    put_u32(&lp, (uint32_t)gn);
    put_u32(&lp, (uint32_t)gn); /* glyph1 empty */

    tabs[0].tag = ROBO_FONT_TAG_HEAD;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)build_head(head, 1000);
    tabs[1].tag = ROBO_FONT_TAG_HHEA;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)build_hhea(hhea, 2);
    tabs[2].tag = ROBO_FONT_TAG_MAXP;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)build_maxp(maxp, 2);
    tabs[3].tag = ROBO_FONT_TAG_HMTX;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)build_hmtx(hmtx, 2);
    tabs[4].tag = ROBO_FONT_TAG_LOCA;
    tabs[4].data = loca;
    tabs[4].length = 12u;
    tabs[5].tag = ROBO_FONT_TAG_GLYF;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)gn;

    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, 6);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_outlines(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_outline(face, 0, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_outline_status(o), (int)ROBO_FONT_OUTLINE_STATUS_OK);
    ROBO_CHECK(robo_font_outline_contour_count(o) >= 1u);
    ROBO_CHECK(robo_font_outline_has_bounds(o));
    ROBO_CHECK_EQ_INT(robo_font_outline_bounds(o, &bb), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)bb.x_min, 0);
    ROBO_CHECK_EQ_INT((int)bb.x_max, 100);
    ROBO_CHECK_EQ_INT(robo_font_outline_dump(o, dump, sizeof(dump), NULL), ROBO_OK);
    ROBO_CHECK(strstr(dump, "MOVE") != NULL);
    ROBO_CHECK(strstr(dump, "CLOSE") != NULL);
    robo_font_outline_release(o);

    ROBO_CHECK_EQ_INT(robo_font_face_glyph_outline(face, 1, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_outline_status(o), (int)ROBO_FONT_OUTLINE_STATUS_EMPTY);
    robo_font_outline_release(o);

    ROBO_CHECK_EQ_INT(robo_font_outline_fuzz_parse(font, n), ROBO_OK);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_outline_tt_implied(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[8], loca[12], glyf[64];
    uint8_t font[384];
    synth_table_t tabs[6];
    size_t gn = build_off_curve_glyf(glyf);
    uint8_t *lp = loca;
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_outline_t *o = NULL;
    char dump[2048];

    put_u32(&lp, 0);
    put_u32(&lp, (uint32_t)gn);

    tabs[0].tag = ROBO_FONT_TAG_HEAD;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)build_head(head, 1000);
    tabs[1].tag = ROBO_FONT_TAG_HHEA;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)build_hhea(hhea, 1);
    tabs[2].tag = ROBO_FONT_TAG_MAXP;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)build_maxp(maxp, 1);
    tabs[3].tag = ROBO_FONT_TAG_HMTX;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)build_hmtx(hmtx, 1);
    tabs[4].tag = ROBO_FONT_TAG_LOCA;
    tabs[4].data = loca;
    tabs[4].length = 8u;
    tabs[5].tag = ROBO_FONT_TAG_GLYF;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)gn;

    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, 6);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_outlines(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_outline(face, 0, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_outline_status(o), (int)ROBO_FONT_OUTLINE_STATUS_OK);
    ROBO_CHECK_EQ_INT(robo_font_outline_dump(o, dump, sizeof(dump), NULL), ROBO_OK);
    ROBO_CHECK(strstr(dump, "QUAD") != NULL);
    robo_font_outline_release(o);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_outline_tt_composite_cycle(void)
{
    /* gid0 composite → gid0 (cycle) */
    uint8_t head[64], hhea[64], maxp[64], hmtx[8], loca[12], glyf[32];
    uint8_t font[384];
    synth_table_t tabs[6];
    uint8_t *p = glyf;
    uint8_t *lp = loca;
    size_t gn;
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_outline_t *o = NULL;

    put_i16(&p, -1);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_u16(&p, (uint16_t)(ROBO_FONT_GLYF_COMP_ARGS_WORDS | ROBO_FONT_GLYF_COMP_ARGS_XY));
    put_u16(&p, 0); /* self */
    put_i16(&p, 0);
    put_i16(&p, 0);
    gn = (size_t)(p - glyf);
    put_u32(&lp, 0);
    put_u32(&lp, (uint32_t)gn);

    tabs[0].tag = ROBO_FONT_TAG_HEAD;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)build_head(head, 1000);
    tabs[1].tag = ROBO_FONT_TAG_HHEA;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)build_hhea(hhea, 1);
    tabs[2].tag = ROBO_FONT_TAG_MAXP;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)build_maxp(maxp, 1);
    tabs[3].tag = ROBO_FONT_TAG_HMTX;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)build_hmtx(hmtx, 1);
    tabs[4].tag = ROBO_FONT_TAG_LOCA;
    tabs[4].data = loca;
    tabs[4].length = 8u;
    tabs[5].tag = ROBO_FONT_TAG_GLYF;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)gn;

    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, 6);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_outlines(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_outline(face, 0, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_outline_status(o), (int)ROBO_FONT_OUTLINE_STATUS_MALFORMED);
    robo_font_outline_release(o);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_outline_affine(void)
{
    robo_font_affine_t a, b, c;
    robo_font_unit_t x, y;
    robo_font_affine_identity(&a);
    ROBO_CHECK_EQ_INT(robo_font_affine_apply(&a, 10, 20, &x, &y), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)x, 10);
    ROBO_CHECK_EQ_INT((int)y, 20);
    a.xscale = 0x2000; /* 0.5 */
    a.yscale = 0x2000;
    a.dx = 5;
    ROBO_CHECK_EQ_INT(robo_font_affine_apply(&a, 10, 20, &x, &y), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)x, 10); /* 5+5 */
    ROBO_CHECK_EQ_INT((int)y, 10);
    robo_font_affine_identity(&b);
    b.dx = 3;
    ROBO_CHECK_EQ_INT(robo_font_affine_multiply(&a, &b, &c), ROBO_OK);
}

void robo_test_register_font_outline(void)
{
    robo_test_register("font.outline.tt_simple", test_outline_tt_simple);
    robo_test_register("font.outline.tt_implied", test_outline_tt_implied);
    robo_test_register("font.outline.tt_cycle", test_outline_tt_composite_cycle);
    robo_test_register("font.outline.affine", test_outline_affine);
}
