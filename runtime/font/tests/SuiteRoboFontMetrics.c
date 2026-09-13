/*
 * RoboFont metrics suite (Goal 5.7).
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
    put_i16(&p, -50);
    put_i16(&p, -200);
    put_i16(&p, 1000);
    put_i16(&p, 800);
    put_u16(&p, 0u);
    put_u16(&p, 8u);
    put_i16(&p, 2);
    put_i16(&p, 1); /* long loca */
    put_i16(&p, 0);
    return (size_t)(p - b);
}

static size_t build_hhea(uint8_t *b, uint16_t n_h_metrics, int16_t asc, int16_t desc,
                         int16_t gap)
{
    uint8_t *p = b;
    int i;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_i16(&p, asc);
    put_i16(&p, desc);
    put_i16(&p, gap);
    put_u16(&p, 900);
    put_i16(&p, -10);
    put_i16(&p, -20);
    put_i16(&p, 880);
    put_i16(&p, 1);
    put_i16(&p, 0);
    put_i16(&p, 0);
    for (i = 0; i < 4; i++) {
        put_i16(&p, 0);
    }
    put_i16(&p, 0);
    put_u16(&p, n_h_metrics);
    return (size_t)(p - b);
}

static size_t build_maxp(uint8_t *b, uint16_t num_glyphs)
{
    uint8_t *p = b;
    put_u32(&p, 0x00010000u);
    put_u16(&p, num_glyphs);
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
    put_u16(&p, 0);
    put_u16(&p, 0);
    return (size_t)(p - b);
}

static size_t build_hmtx_full(uint8_t *b, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i < n; i++) {
        put_u16(&p, (uint16_t)(100u + i * 10u));
        put_i16(&p, (int16_t)(i + 1));
    }
    return (size_t)(p - b);
}

static size_t build_hmtx_compact(uint8_t *b, uint16_t n_metrics, uint16_t num_glyphs)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i < n_metrics; i++) {
        put_u16(&p, (uint16_t)(500u + i));
        put_i16(&p, 10);
    }
    for (i = n_metrics; i < num_glyphs; i++) {
        put_i16(&p, (int16_t)(20 + i));
    }
    return (size_t)(p - b);
}

static size_t build_post(uint8_t *b)
{
    uint8_t *p = b;
    put_u32(&p, 0x00030000u);
    put_u32(&p, 0u);
    put_i16(&p, -125);
    put_i16(&p, 50);
    put_u32(&p, 0u);
    put_u32(&p, 0u);
    put_u32(&p, 0u);
    put_u32(&p, 0u);
    put_u32(&p, 0u);
    return (size_t)(p - b);
}

static size_t build_os2(uint8_t *buf, uint16_t version, uint16_t fs_selection,
                        int16_t typo_asc, int16_t typo_desc, int16_t typo_gap)
{
    uint8_t *p = buf;
    int i;
    put_u16(&p, version);
    put_i16(&p, 480); /* xAvgCharWidth */
    put_u16(&p, 400);
    put_u16(&p, 5);
    put_u16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 40);  /* strikeout size */
    put_i16(&p, 250); /* strikeout pos */
    put_i16(&p, 0);
    for (i = 0; i < 10; i++) {
        put_u8(&p, 0);
    }
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u8(&p, 'R');
    put_u8(&p, 'O');
    put_u8(&p, 'B');
    put_u8(&p, 'O');
    put_u16(&p, fs_selection);
    put_u16(&p, 32);
    put_u16(&p, 126);
    put_i16(&p, typo_asc);
    put_i16(&p, typo_desc);
    put_i16(&p, typo_gap);
    put_u16(&p, 850);
    put_u16(&p, 250);
    if (version >= 1u) {
        put_u32(&p, 1);
        put_u32(&p, 0);
    }
    if (version >= 2u) {
        put_i16(&p, 500); /* sxHeight */
        put_i16(&p, 700); /* sCapHeight */
        put_u16(&p, 0);
        put_u16(&p, 32);
        put_u16(&p, 0);
    }
    return (size_t)(p - buf);
}

static size_t build_vhea(uint8_t *b, uint16_t n_v)
{
    uint8_t *p = b;
    int i;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_i16(&p, 800);
    put_i16(&p, -200);
    put_i16(&p, 0);
    put_i16(&p, 1000);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 1000);
    put_i16(&p, 0);
    put_i16(&p, 1);
    put_i16(&p, 0);
    for (i = 0; i < 4; i++) {
        put_i16(&p, 0);
    }
    put_i16(&p, 0);
    put_u16(&p, n_v);
    return (size_t)(p - b);
}

static size_t build_vmtx(uint8_t *b, uint16_t n_metrics, uint16_t num_glyphs)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i < n_metrics; i++) {
        put_u16(&p, (uint16_t)(1000u + i));
        put_i16(&p, 5);
    }
    for (i = n_metrics; i < num_glyphs; i++) {
        put_i16(&p, 7);
    }
    return (size_t)(p - b);
}

static size_t build_empty_loca_glyf(uint8_t *loca, uint8_t *glyf, uint16_t n,
                                    size_t *glyf_n)
{
    uint8_t *lp = loca;
    uint16_t i;
    for (i = 0u; i <= n; i++) {
        put_u32(&lp, 0u);
    }
    *glyf_n = 0u;
    (void)glyf;
    return (size_t)(lp - loca);
}

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

static void test_metrics_scale(void)
{
    float px = 0.0f;
    ROBO_CHECK(robo_font_units_per_em_is_valid(1000u));
    ROBO_CHECK(!robo_font_units_per_em_is_valid(0u));
    ROBO_CHECK(!robo_font_units_per_em_is_valid(20000u));
    ROBO_CHECK_EQ_INT(robo_font_scale_font_units(500, 1000u, 20.0f,
                                                 ROBO_FONT_ROUND_NEAREST, &px),
                      ROBO_OK);
    ROBO_CHECK(px > 9.5f && px < 10.5f);
    ROBO_CHECK_EQ_INT(robo_font_scale_font_units(1, 1000u, 16.0f, ROBO_FONT_ROUND_FLOOR, &px),
                      ROBO_OK);
    ROBO_CHECK(px == 0.0f);
    ROBO_CHECK(robo_font_scale_font_units(1, 0u, 16.0f, ROBO_FONT_ROUND_NEAREST, &px) !=
               ROBO_OK);
}

static void test_metrics_global_and_typo(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], post[40], os2[128];
    uint8_t loca[32], glyf[8];
    uint8_t font[512];
    size_t glyf_n = 0;
    size_t n;
    synth_table_t tabs[8];
    uint32_t nt = 0;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_metrics_t layout;
    const robo_font_metrics_global_t *g;

    size_t head_n = build_head(head, 1000);
    size_t hhea_n = build_hhea(hhea, 2, 800, -200, 90);
    size_t maxp_n = build_maxp(maxp, 2);
    size_t hmtx_n = build_hmtx_full(hmtx, 2);
    size_t post_n = build_post(post);
    size_t os2_n =
        build_os2(os2, 2, ROBO_FONT_OS2_USE_TYPO_METRICS, 750, -180, 50);
    size_t loca_n = build_empty_loca_glyf(loca, glyf, 2, &glyf_n);

    tabs[nt].tag = ROBO_FONT_TAG_HEAD;
    tabs[nt].data = head;
    tabs[nt].length = (uint32_t)head_n;
    nt++;
    tabs[nt].tag = ROBO_FONT_TAG_HHEA;
    tabs[nt].data = hhea;
    tabs[nt].length = (uint32_t)hhea_n;
    nt++;
    tabs[nt].tag = ROBO_FONT_TAG_MAXP;
    tabs[nt].data = maxp;
    tabs[nt].length = (uint32_t)maxp_n;
    nt++;
    tabs[nt].tag = ROBO_FONT_TAG_HMTX;
    tabs[nt].data = hmtx;
    tabs[nt].length = (uint32_t)hmtx_n;
    nt++;
    tabs[nt].tag = ROBO_FONT_TAG_LOCA;
    tabs[nt].data = loca;
    tabs[nt].length = (uint32_t)loca_n;
    nt++;
    tabs[nt].tag = ROBO_FONT_TAG_GLYF;
    tabs[nt].data = glyf;
    tabs[nt].length = 0u;
    nt++;
    tabs[nt].tag = ROBO_FONT_TAG_POST;
    tabs[nt].data = post;
    tabs[nt].length = (uint32_t)post_n;
    nt++;
    tabs[nt].tag = ROBO_FONT_TAG_OS2;
    tabs[nt].data = os2;
    tabs[nt].length = (uint32_t)os2_n;
    nt++;

    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, nt);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face), ROBO_OK);
    g = robo_font_metrics_info_global(robo_font_face_metrics_info(face));
    ROBO_CHECK(g != NULL);
    ROBO_CHECK_EQ_INT((int)g->units_per_em, 1000);
    ROBO_CHECK_EQ_INT((int)g->layout_source, (int)ROBO_FONT_LINE_FROM_OS2_TYPO);
    ROBO_CHECK_EQ_INT(robo_font_face_layout_metrics(face, &layout), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)layout.ascent, 750);
    ROBO_CHECK_EQ_INT((int)layout.descent, 180);
    ROBO_CHECK_EQ_INT((int)layout.line_gap, 50);
    ROBO_CHECK_EQ_INT((int)robo_font_metrics_line_height(&layout), 980);
    ROBO_CHECK_EQ_INT((int)g->cap_height.avail, (int)ROBO_FONT_METRIC_PRESENT);
    ROBO_CHECK_EQ_INT((int)g->cap_height.value, 700);
    ROBO_CHECK_EQ_INT((int)g->underline_position.avail, (int)ROBO_FONT_METRIC_PRESENT);
    ROBO_CHECK_EQ_INT((int)g->underline_position.value, -125);
    ROBO_CHECK_EQ_INT((int)g->strikeout_thickness.value, 40);
    ROBO_CHECK_EQ_INT((int)g->win_ascent, 850);
    ROBO_CHECK_EQ_INT((int)g->font_bbox_avail, (int)ROBO_FONT_METRIC_PRESENT);

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_metrics_hhea_default_line(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[32];
    uint8_t font[256];
    synth_table_t tabs[4];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_metrics_t layout;
    const robo_font_metrics_global_t *g;

    tabs[0].tag = ROBO_FONT_TAG_HEAD;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)build_head(head, 2048);
    tabs[1].tag = ROBO_FONT_TAG_HHEA;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)build_hhea(hhea, 1, 1800, -400, 100);
    tabs[2].tag = ROBO_FONT_TAG_MAXP;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)build_maxp(maxp, 1);
    tabs[3].tag = ROBO_FONT_TAG_HMTX;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)build_hmtx_full(hmtx, 1);

    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, 4);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face), ROBO_OK);
    g = robo_font_metrics_info_global(robo_font_face_metrics_info(face));
    ROBO_CHECK_EQ_INT((int)g->layout_source, (int)ROBO_FONT_LINE_FROM_HHEA);
    ROBO_CHECK_EQ_INT(robo_font_face_layout_metrics(face, &layout), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)layout.ascent, 1800);
    ROBO_CHECK_EQ_INT((int)layout.descent, 400);
    ROBO_CHECK_EQ_INT((int)g->underline_position.avail, (int)ROBO_FONT_METRIC_FALLBACK);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_metrics_compact_hmtx(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64];
    uint8_t font[256];
    synth_table_t tabs[4];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_glyph_metrics_t gm;

    tabs[0].tag = ROBO_FONT_TAG_HEAD;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)build_head(head, 1000);
    tabs[1].tag = ROBO_FONT_TAG_HHEA;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)build_hhea(hhea, 1, 800, -200, 0); /* 1 of 3 */
    tabs[2].tag = ROBO_FONT_TAG_MAXP;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)build_maxp(maxp, 3);
    tabs[3].tag = ROBO_FONT_TAG_HMTX;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)build_hmtx_compact(hmtx, 1, 3);

    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, 4);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_font_face_glyph_metrics(face, 0, &gm), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)gm.status, (int)ROBO_FONT_GLYPH_METRICS_OK);
    ROBO_CHECK_EQ_INT((int)gm.advance_width, 500);
    ROBO_CHECK_EQ_INT((int)gm.left_side_bearing, 10);

    ROBO_CHECK_EQ_INT(robo_font_face_glyph_metrics(face, 2, &gm), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)gm.advance_width, 500); /* reused */
    ROBO_CHECK_EQ_INT((int)gm.left_side_bearing, 22);

    ROBO_CHECK_EQ_INT(robo_font_face_glyph_metrics(face, 99, &gm), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)gm.status, (int)ROBO_FONT_GLYPH_METRICS_INVALID_ID);

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_metrics_glyph_bbox_rsb(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], loca[16], glyf[64];
    uint8_t font[384];
    synth_table_t tabs[6];
    uint32_t offs[3];
    size_t gn;
    size_t n;
    uint8_t *lp;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_glyph_metrics_t gm;

    gn = build_square_glyf(glyf);
    offs[0] = 0;
    offs[1] = (uint32_t)gn;
    offs[2] = (uint32_t)gn; /* glyph 1 empty */
    lp = loca;
    put_u32(&lp, offs[0]);
    put_u32(&lp, offs[1]);
    put_u32(&lp, offs[2]);

    /* hmtx: gid0 advance 200, lsb 10 → RSB = 200-10-100 = 90 */
    {
        uint8_t *p = hmtx;
        put_u16(&p, 200);
        put_i16(&p, 10);
        put_u16(&p, 50);
        put_i16(&p, 0);
    }

    tabs[0].tag = ROBO_FONT_TAG_HEAD;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)build_head(head, 1000);
    tabs[1].tag = ROBO_FONT_TAG_HHEA;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)build_hhea(hhea, 2, 800, -200, 0);
    tabs[2].tag = ROBO_FONT_TAG_MAXP;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)build_maxp(maxp, 2);
    tabs[3].tag = ROBO_FONT_TAG_HMTX;
    tabs[3].data = hmtx;
    tabs[3].length = 8u;
    tabs[4].tag = ROBO_FONT_TAG_LOCA;
    tabs[4].data = loca;
    tabs[4].length = 12u;
    tabs[5].tag = ROBO_FONT_TAG_GLYF;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)gn;

    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, 6);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_metrics(face, 0, &gm), ROBO_OK);
    ROBO_CHECK(gm.has_bbox);
    ROBO_CHECK(gm.has_rsb);
    ROBO_CHECK_EQ_INT((int)gm.right_side_bearing, 90);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_metrics(face, 1, &gm), ROBO_OK);
    ROBO_CHECK(gm.has_bbox);
    ROBO_CHECK(gm.has_rsb);
    ROBO_CHECK_EQ_INT((int)gm.right_side_bearing, 50); /* empty: adv-lsb */

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_metrics_vertical(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], vhea[64], vmtx[16];
    uint8_t font[384];
    synth_table_t tabs[6];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_vertical_glyph_metrics_t vm;
    const robo_font_metrics_global_t *g;

    tabs[0].tag = ROBO_FONT_TAG_HEAD;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)build_head(head, 1000);
    tabs[1].tag = ROBO_FONT_TAG_HHEA;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)build_hhea(hhea, 2, 800, -200, 0);
    tabs[2].tag = ROBO_FONT_TAG_MAXP;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)build_maxp(maxp, 2);
    tabs[3].tag = ROBO_FONT_TAG_HMTX;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)build_hmtx_full(hmtx, 2);
    tabs[4].tag = ROBO_FONT_TAG_VHEA;
    tabs[4].data = vhea;
    tabs[4].length = (uint32_t)build_vhea(vhea, 1);
    tabs[5].tag = ROBO_FONT_TAG_VMTX;
    tabs[5].data = vmtx;
    tabs[5].length = (uint32_t)build_vmtx(vmtx, 1, 2);

    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, 6);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face), ROBO_OK);
    g = robo_font_metrics_info_global(robo_font_face_metrics_info(face));
    ROBO_CHECK(g->has_vertical);
    ROBO_CHECK(robo_font_metrics_info_vhea(robo_font_face_metrics_info(face)) != NULL);
    ROBO_CHECK_EQ_INT(
        robo_font_metrics_info_glyph_vertical(robo_font_face_metrics_info(face), 0, &vm),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)vm.advance_height, 1000);
    ROBO_CHECK_EQ_INT(
        robo_font_metrics_info_glyph_vertical(robo_font_face_metrics_info(face), 1, &vm),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)vm.advance_height, 1000);
    ROBO_CHECK_EQ_INT((int)vm.top_side_bearing, 7);

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_metrics_instance_and_fuzz(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16];
    uint8_t font[256];
    synth_table_t tabs[4];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_instance_t *inst = NULL;
    robo_font_metrics_t m;
    float px = 0.0f;

    tabs[0].tag = ROBO_FONT_TAG_HEAD;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)build_head(head, 1000);
    tabs[1].tag = ROBO_FONT_TAG_HHEA;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)build_hhea(hhea, 1, 800, -200, 0);
    tabs[2].tag = ROBO_FONT_TAG_MAXP;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)build_maxp(maxp, 1);
    tabs[3].tag = ROBO_FONT_TAG_HMTX;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)build_hmtx_full(hmtx, 1);

    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, 4);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_instance_create(face, 16.0f, NULL, &inst), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_instance_metrics(inst, &m), ROBO_OK);
    ROBO_CHECK(m.valid);
    ROBO_CHECK_EQ_INT(robo_font_instance_scale_advance(inst, 500, ROBO_FONT_ROUND_NEAREST, &px),
                      ROBO_OK);
    ROBO_CHECK(px > 7.5f && px < 8.5f);

    ROBO_CHECK_EQ_INT(robo_font_metrics_fuzz_parse(font, n), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_metrics_fuzz_parse((const uint8_t *)"xxxx", 4), ROBO_OK);

    robo_font_instance_release(inst);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

void robo_test_register_font_metrics(void)
{
    robo_test_register("font.metrics.scale", test_metrics_scale);
    robo_test_register("font.metrics.global_typo", test_metrics_global_and_typo);
    robo_test_register("font.metrics.hhea_line", test_metrics_hhea_default_line);
    robo_test_register("font.metrics.compact_hmtx", test_metrics_compact_hmtx);
    robo_test_register("font.metrics.glyph_bbox", test_metrics_glyph_bbox_rsb);
    robo_test_register("font.metrics.vertical", test_metrics_vertical);
    robo_test_register("font.metrics.instance_fuzz", test_metrics_instance_and_fuzz);
}
