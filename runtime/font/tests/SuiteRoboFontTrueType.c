/*
 * RoboFont TrueType table suite (Goal 5.3).
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboTest.h"

#include <string.h>

static void put_u8(uint8_t **p, uint8_t v)
{
    *(*p)++ = v;
}

static void put_u16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}

static void put_i16(uint8_t **p, int16_t v)
{
    put_u16(p, (uint16_t)v);
}

static void put_u32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}

static void put_i64(uint8_t **p, int64_t v)
{
    put_u32(p, (uint32_t)((uint64_t)v >> 32));
    put_u32(p, (uint32_t)(uint64_t)v);
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
        uint8_t *rec = out + 12u + (size_t)i * 16u;
        uint32_t checksum;
        size_t padded = ((size_t)tables[i].length + 3u) & ~(size_t)3u;
        if (tables[i].length && tables[i].data) {
            memcpy(out + cursor, tables[i].data, tables[i].length);
        }
        checksum = robo_font_sfnt_checksum(out + cursor, tables[i].length);
        p = rec;
        put_u32(&p, tables[i].tag);
        put_u32(&p, checksum);
        put_u32(&p, (uint32_t)cursor);
        put_u32(&p, tables[i].length);
        cursor += padded;
    }
    return cursor;
}

/* ---- table builders ---- */

static size_t build_head(uint8_t *b, uint16_t upem, int16_t loca_fmt)
{
    uint8_t *p = b;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u32(&p, 0x00010000u);
    put_u32(&p, 0u); /* checksumAdjustment */
    put_u32(&p, ROBO_FONT_HEAD_MAGIC);
    put_u16(&p, 0u);
    put_u16(&p, upem);
    put_i64(&p, 0);
    put_i64(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, -100);
    put_i16(&p, 1000);
    put_i16(&p, 800);
    put_u16(&p, 0u);
    put_u16(&p, 8u);
    put_i16(&p, 2);
    put_i16(&p, loca_fmt);
    put_i16(&p, 0);
    return (size_t)(p - b);
}

static size_t build_hhea(uint8_t *b, uint16_t n_h_metrics)
{
    uint8_t *p = b;
    int i;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_i16(&p, 800);
    put_i16(&p, -200);
    put_i16(&p, 90);
    put_u16(&p, 1000);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 1000);
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

static size_t build_maxp_tt(uint8_t *b, uint16_t num_glyphs)
{
    uint8_t *p = b;
    put_u32(&p, 0x00010000u);
    put_u16(&p, num_glyphs);
    put_u16(&p, 8);  /* maxPoints */
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
    put_u16(&p, 4); /* maxComponentElements */
    put_u16(&p, 4); /* maxComponentDepth */
    return (size_t)(p - b);
}

static size_t build_hmtx(uint8_t *b, uint16_t n_metrics, uint16_t num_glyphs)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i < n_metrics; i++) {
        put_u16(&p, (uint16_t)(500u + i));
        put_i16(&p, 10);
    }
    for (i = n_metrics; i < num_glyphs; i++) {
        put_i16(&p, 5);
    }
    return (size_t)(p - b);
}

static size_t build_loca_long(uint8_t *b, const uint32_t *offs, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i <= n; i++) {
        put_u32(&p, offs[i]);
    }
    return (size_t)(p - b);
}

static size_t build_loca_short(uint8_t *b, const uint32_t *byte_offs, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i <= n; i++) {
        put_u16(&p, (uint16_t)(byte_offs[i] / 2u));
    }
    return (size_t)(p - b);
}

/* Empty glyph = 0 length. Simple one-contour square: 4 points. */
static size_t build_simple_glyf(uint8_t *b)
{
    uint8_t *p = b;
    /* header */
    put_i16(&p, 1); /* contours */
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 100);
    put_i16(&p, 100);
    put_u16(&p, 3); /* endPts: last point index 3 */
    put_u16(&p, 0); /* no instructions */
    /* flags: all on-curve, short vectors positive */
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                          ROBO_FONT_GLYF_FLAG_Y_SHORT | ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                          ROBO_FONT_GLYF_FLAG_Y_SHORT | ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                          ROBO_FONT_GLYF_FLAG_Y_SHORT | ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                          ROBO_FONT_GLYF_FLAG_Y_SHORT | ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    /* x deltas */
    put_u8(&p, 0);
    put_u8(&p, 100);
    put_u8(&p, 0);
    put_u8(&p, 100); /* wait - for square we need proper deltas */
    /* Actually redo: start (0,0), (100,0), (100,100), (0,100) */
    /* x: 0, +100, 0, -100 — but we used all positive short. Rebuild flags. */
    (void)0;
    return (size_t)(p - b);
}

/* Cleaner simple glyph builder */
static size_t build_simple_square(uint8_t *b)
{
    uint8_t *p = b;
    put_i16(&p, 1);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 100);
    put_i16(&p, 100);
    put_u16(&p, 3);
    put_u16(&p, 0);
    /* point0: (0,0) — both same (zero delta) */
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    /* point1: (+100, 0) */
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                          ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    /* point2: (0, +100) relative → (100,100) */
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_Y_SHORT |
                          ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    /* point3: (-100, 0) → (0,100) */
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    /* x stream: p1=100, p3=100 (negative because X_SAME clear) */
    put_u8(&p, 100);
    put_u8(&p, 100);
    /* y stream: p2=100 */
    put_u8(&p, 100);
    return (size_t)(p - b);
}

static size_t build_composite_glyf(uint8_t *b, uint16_t component_gid)
{
    uint8_t *p = b;
    put_i16(&p, -1);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 100);
    put_i16(&p, 100);
    /* flags: ARGS_ARE_XY | ARGS_WORDS (no MORE) */
    put_u16(&p, (uint16_t)(ROBO_FONT_GLYF_COMP_ARGS_WORDS | ROBO_FONT_GLYF_COMP_ARGS_XY));
    put_u16(&p, component_gid);
    put_i16(&p, 10);
    put_i16(&p, 20);
    return (size_t)(p - b);
}

static size_t build_post(uint8_t *b)
{
    uint8_t *p = b;
    put_u32(&p, 0x00030000u);
    put_u32(&p, 0u); /* italicAngle */
    put_i16(&p, -100);
    put_i16(&p, 50);
    put_u32(&p, 0u);
    put_u32(&p, 0u);
    put_u32(&p, 0u);
    put_u32(&p, 0u);
    put_u32(&p, 0u);
    return (size_t)(p - b);
}

static size_t build_os2_v0(uint8_t *b)
{
    uint8_t *p = b;
    int i;
    put_u16(&p, 0);
    put_i16(&p, 500);
    put_u16(&p, 400);
    put_u16(&p, 5);
    put_u16(&p, 0);
    for (i = 0; i < 10; i++) {
        put_i16(&p, 0); /* sub/super/strike — 10 int16s = 5 pairs... actually 10 fields */
    }
    /* We over-wrote — rebuild carefully */
    (void)0;
    return 0;
}

static size_t build_os2_v1(uint8_t *buf)
{
    uint8_t *p = buf;
    int i;
    put_u16(&p, 1);
    put_i16(&p, 500);
    put_u16(&p, 700); /* bold */
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
    put_i16(&p, 50);
    put_i16(&p, 300);
    put_i16(&p, 0); /* sFamilyClass */
    for (i = 0; i < 10; i++) {
        put_u8(&p, 0);
    }
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u8(&p, 'T');
    put_u8(&p, 'E');
    put_u8(&p, 'S');
    put_u8(&p, 'T');
    put_u16(&p, 0x0020u); /* fsSelection: BOLD */
    put_u16(&p, 32);
    put_u16(&p, 126);
    put_i16(&p, 800);
    put_i16(&p, -200);
    put_i16(&p, 90);
    put_u16(&p, 800);
    put_u16(&p, 200);
    put_u32(&p, 1);
    put_u32(&p, 0);
    return (size_t)(p - buf);
}

static size_t build_name_family(uint8_t *buf, size_t cap)
{
    /* format 0, 1 record: Windows Unicode "Test" */
    static const uint8_t u16be[] = {0x00, 'T', 0x00, 'e', 0x00, 's', 0x00, 't'};
    uint8_t *p = buf;
    size_t storage_off = 6u + 12u;
    if (cap < storage_off + sizeof(u16be)) {
        return 0u;
    }
    put_u16(&p, 0);
    put_u16(&p, 1);
    put_u16(&p, (uint16_t)storage_off);
    put_u16(&p, 3);    /* platform Windows */
    put_u16(&p, 1);    /* Unicode BMP */
    put_u16(&p, 0x0409);
    put_u16(&p, ROBO_FONT_NAME_ID_FAMILY);
    put_u16(&p, (uint16_t)sizeof(u16be));
    put_u16(&p, 0);
    memcpy(buf + storage_off, u16be, sizeof(u16be));
    return storage_off + sizeof(u16be);
}

/* Full minimal TT font: 2 glyphs (empty + simple), long loca */
static size_t build_min_tt_font(uint8_t *out, size_t cap, int short_loca)
{
    uint8_t head[64], hhea[40], maxp[40], hmtx[32], loca[32], glyf[128];
    uint8_t post[40], os2[128], name[64];
    size_t head_n, hhea_n, maxp_n, hmtx_n, loca_n, glyf_n, post_n, os2_n, name_n;
    uint32_t offs[3];
    size_t g0 = 0;
    size_t g1;
    synth_table_t tabs[9];
    uint32_t ntabs = 0;

    head_n = build_head(head, 1000, short_loca ? 0 : 1);
    hhea_n = build_hhea(hhea, 1);
    maxp_n = build_maxp_tt(maxp, 2);
    hmtx_n = build_hmtx(hmtx, 1, 2);
    g1 = build_simple_square(glyf);
    /* pad g1 to even for short loca friendliness */
    while ((g1 & 1u) != 0u) {
        glyf[g1++] = 0;
    }
    glyf_n = g1;
    offs[0] = 0;
    offs[1] = 0; /* glyph 0 empty */
    offs[2] = (uint32_t)glyf_n;
    /* Move simple glyph to offset 0 actually — glyph0 empty means offs[0]==offs[1]==0,
     * glyf data starts at 0 for glyph1. So copy square at 0 and set offs[1]=0, offs[2]=len.
     * Wait: empty then square: offs = {0, 0, len} means glyph1 is [0,len). Good. */
    loca_n = short_loca ? build_loca_short(loca, offs, 2) : build_loca_long(loca, offs, 2);
    post_n = build_post(post);
    os2_n = build_os2_v1(os2);
    name_n = build_name_family(name, sizeof(name));

    tabs[ntabs].tag = ROBO_FONT_TAG_HEAD;
    tabs[ntabs].data = head;
    tabs[ntabs].length = (uint32_t)head_n;
    ntabs++;
    tabs[ntabs].tag = ROBO_FONT_TAG_HHEA;
    tabs[ntabs].data = hhea;
    tabs[ntabs].length = (uint32_t)hhea_n;
    ntabs++;
    tabs[ntabs].tag = ROBO_FONT_TAG_MAXP;
    tabs[ntabs].data = maxp;
    tabs[ntabs].length = (uint32_t)maxp_n;
    ntabs++;
    tabs[ntabs].tag = ROBO_FONT_TAG_HMTX;
    tabs[ntabs].data = hmtx;
    tabs[ntabs].length = (uint32_t)hmtx_n;
    ntabs++;
    tabs[ntabs].tag = ROBO_FONT_TAG_LOCA;
    tabs[ntabs].data = loca;
    tabs[ntabs].length = (uint32_t)loca_n;
    ntabs++;
    tabs[ntabs].tag = ROBO_FONT_TAG_GLYF;
    tabs[ntabs].data = glyf;
    tabs[ntabs].length = (uint32_t)glyf_n;
    ntabs++;
    tabs[ntabs].tag = ROBO_FONT_TAG_POST;
    tabs[ntabs].data = post;
    tabs[ntabs].length = (uint32_t)post_n;
    ntabs++;
    tabs[ntabs].tag = ROBO_FONT_TAG_OS2;
    tabs[ntabs].data = os2;
    tabs[ntabs].length = (uint32_t)os2_n;
    ntabs++;
    tabs[ntabs].tag = ROBO_FONT_TAG_NAME;
    tabs[ntabs].data = name;
    tabs[ntabs].length = (uint32_t)name_n;
    ntabs++;

    (void)g0;
    (void)build_simple_glyf;
    (void)build_os2_v0;
    return build_sfnt(out, cap, ROBO_FONT_SFNT_VERSION_TRUE, tabs, ntabs);
}

static void test_tt_head_valid_invalid(void)
{
    uint8_t head[64];
    size_t n = build_head(head, 2048, 1);
    robo_font_span_t span = {head, n};
    robo_font_head_t h;
    ROBO_CHECK_EQ_INT(robo_font_head_parse(span, &h), ROBO_OK);
    ROBO_CHECK_EQ_INT(h.units_per_em, 2048);
    ROBO_CHECK_EQ_INT(h.index_to_loc_format, 1);

    head[12] = 0xDEu;
    head[13] = 0xADu;
    head[14] = 0xBEu;
    head[15] = 0xEFu;
    ROBO_CHECK_EQ_INT(robo_font_head_parse(span, &h), ROBO_PARSE_ERROR);

    n = build_head(head, 8, 0); /* bad upem */
    span.size = n;
    ROBO_CHECK_EQ_INT(robo_font_head_parse(span, &h), ROBO_PARSE_ERROR);

    span.size = 20;
    ROBO_CHECK_EQ_INT(robo_font_head_parse(span, &h), ROBO_PARSE_ERROR);
}

static void test_tt_integrated_font(void)
{
    uint8_t font[2048];
    size_t n = build_min_tt_font(font, sizeof(font), 0);
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_tt_t *tt = NULL;
    robo_font_glyf_glyph_t g;
    uint16_t aw = 0;
    int16_t lsb = 0;
    uint32_t off = 0;
    uint32_t len = 0;
    char namebuf[64];
    size_t namelen = 0;
    robo_font_metrics_t metrics;
    robo_font_face_t *face = NULL;
    uint32_t gcount = 0;

    ROBO_CHECK(n > 100u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_tt_parse(sfnt, ROBO_FONT_TT_FLAGS_REQUIRE_GLYF, &tt),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_tt_num_glyphs(tt), 2);
    ROBO_CHECK_EQ_INT(robo_font_tt_advance_width(tt, 0, &aw), ROBO_OK);
    ROBO_CHECK_EQ_INT(aw, 500);
    ROBO_CHECK_EQ_INT(robo_font_tt_advance_width(tt, 1, &aw), ROBO_OK);
    ROBO_CHECK_EQ_INT(aw, 500); /* last advance reused */
    ROBO_CHECK_EQ_INT(robo_font_tt_left_side_bearing(tt, 1, &lsb), ROBO_OK);
    ROBO_CHECK_EQ_INT(lsb, 5);

    ROBO_CHECK_EQ_INT(robo_font_tt_glyph_length(tt, 0, &len), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)len, 0);
    ROBO_CHECK_EQ_INT(robo_font_tt_glyph_offset(tt, 1, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_tt_glyph_length(tt, 1, &len), ROBO_OK);
    ROBO_CHECK(len > 10u);

    memset(&g, 0, sizeof(g));
    ROBO_CHECK_EQ_INT(robo_font_tt_parse_glyph(tt, 0, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)g.kind, (int)ROBO_FONT_GLYF_EMPTY);
    robo_font_glyf_glyph_clear(&g);
    ROBO_CHECK_EQ_INT(robo_font_tt_parse_glyph(tt, 1, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)g.kind, (int)ROBO_FONT_GLYF_SIMPLE);
    ROBO_CHECK_EQ_INT(g.point_count, 4);
    ROBO_CHECK_EQ_INT(g.x_coords[0], 0);
    ROBO_CHECK_EQ_INT(g.x_coords[1], 100);
    ROBO_CHECK_EQ_INT(g.y_coords[2], 100);
    ROBO_CHECK_EQ_INT(g.x_coords[3], 0);
    robo_font_glyf_glyph_clear(&g);

    ROBO_CHECK(robo_font_tt_name(tt) != NULL);
    ROBO_CHECK_EQ_INT(
        robo_font_name_get_utf8(robo_font_tt_name(tt), ROBO_FONT_NAME_ID_FAMILY, namebuf,
                                sizeof(namebuf), &namelen),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(namebuf, "Test");

    ROBO_CHECK(robo_font_tt_os2(tt) != NULL);
    ROBO_CHECK(robo_font_tt_post(tt) != NULL);
    ROBO_CHECK_EQ_INT(robo_font_tt_metrics(tt, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(metrics.units_per_em, 1000);
    ROBO_CHECK_EQ_INT(metrics.valid, 1);

    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, ROBO_FONT_TT_FLAGS_REQUIRE_GLYF),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_count(face, &gcount), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)gcount, 2);

    robo_font_face_release(face);
    robo_font_tt_release(tt);
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
}

static void test_tt_short_loca(void)
{
    uint8_t font[2048];
    size_t n = build_min_tt_font(font, sizeof(font), 1);
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_tt_t *tt = NULL;
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_borrow(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_tt_parse(sfnt, ROBO_FONT_TT_FLAGS_REQUIRE_GLYF, &tt),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_tt_head(tt)->index_to_loc_format, 0);
    robo_font_tt_release(tt);
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
}

static void test_tt_composite(void)
{
    uint8_t head[64], hhea[40], maxp[40], hmtx[16], loca[24], glyf[256];
    size_t head_n, hhea_n, maxp_n, hmtx_n, loca_n, g0n, g1n;
    uint32_t offs[3];
    synth_table_t tabs[6];
    uint8_t font[1024];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_tt_t *tt = NULL;
    robo_font_glyf_glyph_t g;

    head_n = build_head(head, 1000, 1);
    hhea_n = build_hhea(hhea, 2);
    maxp_n = build_maxp_tt(maxp, 2);
    hmtx_n = build_hmtx(hmtx, 2, 2);
    g0n = build_simple_square(glyf);
    while ((g0n & 3u) != 0u) {
        glyf[g0n++] = 0;
    }
    g1n = build_composite_glyf(glyf + g0n, 0);
    offs[0] = 0;
    offs[1] = (uint32_t)g0n;
    offs[2] = (uint32_t)(g0n + g1n);
    loca_n = build_loca_long(loca, offs, 2);

    tabs[0] = (synth_table_t){ROBO_FONT_TAG_HEAD, head, (uint32_t)head_n};
    tabs[1] = (synth_table_t){ROBO_FONT_TAG_HHEA, hhea, (uint32_t)hhea_n};
    tabs[2] = (synth_table_t){ROBO_FONT_TAG_MAXP, maxp, (uint32_t)maxp_n};
    tabs[3] = (synth_table_t){ROBO_FONT_TAG_HMTX, hmtx, (uint32_t)hmtx_n};
    tabs[4] = (synth_table_t){ROBO_FONT_TAG_LOCA, loca, (uint32_t)loca_n};
    tabs[5] = (synth_table_t){ROBO_FONT_TAG_GLYF, glyf, (uint32_t)(g0n + g1n)};
    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, 6);

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_tt_parse(sfnt, ROBO_FONT_TT_FLAGS_REQUIRE_GLYF, &tt),
                      ROBO_OK);
    memset(&g, 0, sizeof(g));
    ROBO_CHECK_EQ_INT(robo_font_tt_parse_glyph(tt, 1, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)g.kind, (int)ROBO_FONT_GLYF_COMPOSITE);
    ROBO_CHECK_EQ_INT(g.component_count, 1);
    ROBO_CHECK_EQ_INT(g.components[0].glyph_index, 0);
    ROBO_CHECK_EQ_INT(g.components[0].arg1, 10);
    ROBO_CHECK_EQ_INT(robo_font_tt_validate_composite_closure(tt, 1), ROBO_OK);
    robo_font_glyf_glyph_clear(&g);

    robo_font_tt_release(tt);
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
}

static void test_tt_malformed(void)
{
    uint8_t head[54];
    robo_font_span_t span;
    robo_font_head_t h;
    robo_font_hhea_t hh;
    robo_font_maxp_t mp;
    uint8_t hhea[36];
    uint8_t maxp[6];
    uint8_t *p;

    /* truncated maxp v1 */
    p = maxp;
    put_u32(&p, 0x00010000u);
    put_u16(&p, 10);
    span.data = maxp;
    span.size = 6;
    ROBO_CHECK_EQ_INT(robo_font_maxp_parse(span, &mp), ROBO_PARSE_ERROR);

    build_head(head, 1000, 2); /* bad loca format */
    span.data = head;
    span.size = 54;
    {
        uint8_t *q = head + 50;
        put_i16(&q, 2);
    }
    ROBO_CHECK_EQ_INT(robo_font_head_parse(span, &h), ROBO_PARSE_ERROR);

    build_hhea(hhea, 5);
    span.data = hhea;
    span.size = 20;
    ROBO_CHECK_EQ_INT(robo_font_hhea_parse(span, &hh), ROBO_PARSE_ERROR);

    /* short hmtx via integrated parse */
    {
        uint8_t font[2048];
        size_t n = build_min_tt_font(font, sizeof(font), 0);
        robo_font_source_t *src = NULL;
        robo_font_sfnt_t *sfnt = NULL;
        robo_font_tt_t *tt = NULL;
        /* Shrink hmtx length in directory — find hmtx record and set length 2 */
        ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
        /* Corrupt after SFNT parse by rebuilding with bad hmtx — easier: */
        robo_font_sfnt_release(sfnt);
        robo_font_source_release(src);
        {
            uint8_t headb[64], hheab[40], maxpb[40], hmtxb[4], locab[32], glyfb[128];
            size_t hn, hen, mn, hmn, ln, gn;
            uint32_t offs[3] = {0, 0, 0};
            synth_table_t t[6];
            hn = build_head(headb, 1000, 1);
            hen = build_hhea(hheab, 2);
            mn = build_maxp_tt(maxpb, 2);
            hmn = 2; /* too short */
            hmtxb[0] = 0;
            hmtxb[1] = 1;
            gn = build_simple_square(glyfb);
            offs[2] = (uint32_t)gn;
            ln = build_loca_long(locab, offs, 2);
            t[0] = (synth_table_t){ROBO_FONT_TAG_HEAD, headb, (uint32_t)hn};
            t[1] = (synth_table_t){ROBO_FONT_TAG_HHEA, hheab, (uint32_t)hen};
            t[2] = (synth_table_t){ROBO_FONT_TAG_MAXP, maxpb, (uint32_t)mn};
            t[3] = (synth_table_t){ROBO_FONT_TAG_HMTX, hmtxb, (uint32_t)hmn};
            t[4] = (synth_table_t){ROBO_FONT_TAG_LOCA, locab, (uint32_t)ln};
            t[5] = (synth_table_t){ROBO_FONT_TAG_GLYF, glyfb, (uint32_t)gn};
            n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, t, 6);
            ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_font_tt_parse(sfnt, ROBO_FONT_TT_FLAGS_REQUIRE_GLYF, &tt),
                              ROBO_PARSE_ERROR);
            robo_font_sfnt_release(sfnt);
            robo_font_source_release(src);
        }
    }

    /* non-monotonic loca */
    {
        uint8_t loca[12];
        uint8_t *lp = loca;
        uint32_t *dummy = NULL;
        put_u32(&lp, 0);
        put_u32(&lp, 100);
        put_u32(&lp, 50); /* regress */
        {
            robo_font_span_t ls = {loca, 12};
            /* parse_loca is static — trigger via full font */
            uint8_t headb[64], hheab[40], maxpb[40], hmtxb[16], glyfb[4];
            synth_table_t t[6];
            uint8_t font[512];
            size_t n;
            robo_font_source_t *src = NULL;
            robo_font_sfnt_t *sfnt = NULL;
            robo_font_tt_t *tt = NULL;
            size_t hn = build_head(headb, 1000, 1);
            size_t hen = build_hhea(hheab, 2);
            size_t mn = build_maxp_tt(maxpb, 2);
            size_t hmn = build_hmtx(hmtxb, 2, 2);
            glyfb[0] = 0;
            t[0] = (synth_table_t){ROBO_FONT_TAG_HEAD, headb, (uint32_t)hn};
            t[1] = (synth_table_t){ROBO_FONT_TAG_HHEA, hheab, (uint32_t)hen};
            t[2] = (synth_table_t){ROBO_FONT_TAG_MAXP, maxpb, (uint32_t)mn};
            t[3] = (synth_table_t){ROBO_FONT_TAG_HMTX, hmtxb, (uint32_t)hmn};
            t[4] = (synth_table_t){ROBO_FONT_TAG_LOCA, loca, 12};
            t[5] = (synth_table_t){ROBO_FONT_TAG_GLYF, glyfb, 4};
            n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, t, 6);
            ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_font_tt_parse(sfnt, ROBO_FONT_TT_FLAGS_REQUIRE_GLYF, &tt),
                              ROBO_PARSE_ERROR);
            robo_font_sfnt_release(sfnt);
            robo_font_source_release(src);
            (void)dummy;
            (void)ls;
        }
    }

    /* bad name string range */
    {
        uint8_t name[32];
        uint8_t *np = name;
        robo_font_name_table_t nt;
        put_u16(&np, 0);
        put_u16(&np, 1);
        put_u16(&np, 18);
        put_u16(&np, 3);
        put_u16(&np, 1);
        put_u16(&np, 0);
        put_u16(&np, 1);
        put_u16(&np, 100); /* length past end */
        put_u16(&np, 0);
        {
            robo_font_span_t ns = {name, 18};
            ROBO_CHECK_EQ_INT(robo_font_name_parse(ns, &nt), ROBO_PARSE_ERROR);
        }
    }

    /* composite invalid glyph id */
    {
        uint8_t gbuf[64];
        size_t gn = build_composite_glyf(gbuf, 99);
        robo_font_glyf_glyph_t g;
        robo_font_span_t gs = {gbuf, gn};
        memset(&g, 0, sizeof(g));
        ROBO_CHECK_EQ_INT(robo_font_tt_parse_glyph_at(gs, 0, (uint32_t)gn, 2, &g),
                          ROBO_PARSE_ERROR);
    }

    ROBO_CHECK(robo_font_tt_fuzz_parse((const uint8_t *)"notafont", 8) != ROBO_OK);
}

static void test_tt_flag_repeat(void)
{
    uint8_t b[128];
    uint8_t *p = b;
    robo_font_glyf_glyph_t g;
    robo_font_span_t span;
    put_i16(&p, 1);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 10);
    put_i16(&p, 10);
    put_u16(&p, 2); /* 3 points */
    put_u16(&p, 0);
    /* one flag with repeat 2 */
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_REPEAT |
                          ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, 2);
    span.data = b;
    span.size = (size_t)(p - b);
    memset(&g, 0, sizeof(g));
    ROBO_CHECK_EQ_INT(
        robo_font_tt_parse_glyph_at(span, 0, (uint32_t)span.size, 1, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(g.point_count, 3);
    robo_font_glyf_glyph_clear(&g);

    /* excess repeat */
    p = b + 12;
    put_u16(&p, 1); /* endPts → 2 points */
    put_u16(&p, 0);
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_REPEAT | ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, 5); /* too many */
    span.size = (size_t)(p - b);
    /* rewrite header endPts for 2 points: offset 10 */
    {
        uint8_t *e = b + 10;
        put_u16(&e, 1);
    }
    ROBO_CHECK_EQ_INT(
        robo_font_tt_parse_glyph_at(span, 0, (uint32_t)span.size, 1, &g), ROBO_PARSE_ERROR);
}

void robo_test_register_font_tt(void)
{
    robo_test_register("font.tt.head", test_tt_head_valid_invalid);
    robo_test_register("font.tt.integrated", test_tt_integrated_font);
    robo_test_register("font.tt.short_loca", test_tt_short_loca);
    robo_test_register("font.tt.composite", test_tt_composite);
    robo_test_register("font.tt.malformed", test_tt_malformed);
    robo_test_register("font.tt.flag_repeat", test_tt_flag_repeat);
}
