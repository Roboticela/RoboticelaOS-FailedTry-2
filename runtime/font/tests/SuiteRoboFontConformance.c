/*
 * RoboFont conformance suite (Goal 5.21).
 *
 * Cross-cutting regression boundary for Goals 5.1–5.20.
 * Synthetic fixtures + documented corpus under tests/fonts/ — no Chromium /
 * system fonts. Filter: -Filter font.conform  (or font.conform.sfnt, …).
 */

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontConformance.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>

#ifndef ROBO_FONT_NAME_ID_FAMILY
#define ROBO_FONT_NAME_ID_FAMILY 1u
#endif
#ifndef ROBO_FONT_NAME_ID_SUBFAMILY
#define ROBO_FONT_NAME_ID_SUBFAMILY 2u
#endif
#ifndef ROBO_FONT_NAME_ID_FULL
#define ROBO_FONT_NAME_ID_FULL 4u
#endif
#ifndef ROBO_FONT_NAME_ID_POSTSCRIPT
#define ROBO_FONT_NAME_ID_POSTSCRIPT 6u
#endif

/* ---- Corpus polarity blobs (mirrored under tests/fonts/) ---- */

static const uint8_t k_corpus_truncated_sfnt[] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x10};
static const uint8_t k_corpus_huge_tables[] = {
    0x00, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* ---- Binary helpers ---- */

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
    put_u32(p, (uint32_t)((uint64_t)v >> 32));
    put_u32(p, (uint32_t)((uint64_t)v));
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
        if (tables[i].length && tables[i].data) {
            memcpy(out + cursor, tables[i].data, tables[i].length);
        }
        cursor += pad;
    }
    return cursor;
}

static size_t build_head(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u32(&p, 0x00010000u);
    put_u32(&p, 0);
    put_u32(&p, ROBO_FONT_HEAD_MAGIC);
    put_u16(&p, 0);
    put_u16(&p, 1000);
    put_i64(&p, 0);
    put_i64(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, -200);
    put_i16(&p, 1000);
    put_i16(&p, 800);
    put_u16(&p, 0);
    put_u16(&p, 9);
    put_i16(&p, 2);
    put_i16(&p, 0);
    put_i16(&p, 0);
    return (size_t)(p - buf);
}

static size_t build_hhea(uint8_t *buf, uint16_t num_metrics)
{
    uint8_t *p = buf;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_i16(&p, 800);
    put_i16(&p, -200);
    put_i16(&p, 90);
    put_u16(&p, 1000);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_u16(&p, num_metrics);
    return (size_t)(p - buf);
}

static size_t build_maxp(uint8_t *buf, uint16_t glyphs)
{
    uint8_t *p = buf;
    int i;
    put_u32(&p, 0x00010000u);
    put_u16(&p, glyphs);
    for (i = 0; i < 13; i++) {
        put_u16(&p, 0);
    }
    return (size_t)(p - buf);
}

static size_t build_hmtx(uint8_t *buf, uint16_t count)
{
    uint8_t *p = buf;
    uint16_t i;
    for (i = 0u; i < count; i++) {
        put_u16(&p, 500);
        put_i16(&p, 10);
    }
    return (size_t)(p - buf);
}

static size_t build_loca(uint8_t *buf, uint16_t glyphs)
{
    uint8_t *p = buf;
    uint16_t i;
    for (i = 0u; i <= glyphs; i++) {
        put_u16(&p, 0);
    }
    return (size_t)(p - buf);
}

static size_t build_post(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u32(&p, 0x00030000u);
    put_u32(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    return (size_t)(p - buf);
}

static size_t build_os2(uint8_t *buf, uint16_t weight, uint16_t width)
{
    uint8_t *p = buf;
    int i;
    put_u16(&p, 4);
    put_i16(&p, 500);
    put_u16(&p, weight);
    put_u16(&p, width);
    put_u16(&p, 0);
    for (i = 0; i < 10; i++) {
        put_i16(&p, 0);
    }
    put_i16(&p, 0);
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
    put_u16(&p, ROBO_FONT_FS_REGULAR);
    put_u16(&p, 32);
    put_u16(&p, 126);
    put_i16(&p, 800);
    put_i16(&p, -200);
    put_i16(&p, 90);
    put_u16(&p, 800);
    put_u16(&p, 200);
    put_u32(&p, 1);
    put_u32(&p, 0);
    put_i16(&p, 500);
    put_i16(&p, 700);
    put_u16(&p, 0);
    put_u16(&p, 32);
    put_u16(&p, 0);
    return (size_t)(p - buf);
}

static void put_utf16be_ascii(uint8_t **p, const char *s)
{
    while (*s) {
        put_u16(p, (uint16_t)(uint8_t)*s);
        s++;
    }
}

static size_t build_name_table(uint8_t *buf, size_t cap, const char *family,
                               const char *ps)
{
    const char *strs[4];
    uint16_t ids[4];
    uint32_t count = 4u;
    uint32_t i;
    size_t header = 6u + (size_t)count * 12u;
    size_t storage = header;
    uint8_t *p = buf;

    ids[0] = ROBO_FONT_NAME_ID_FAMILY;
    strs[0] = family;
    ids[1] = ROBO_FONT_NAME_ID_SUBFAMILY;
    strs[1] = "Regular";
    ids[2] = ROBO_FONT_NAME_ID_FULL;
    strs[2] = family;
    ids[3] = ROBO_FONT_NAME_ID_POSTSCRIPT;
    strs[3] = ps;

    if (cap < header) {
        return 0u;
    }
    put_u16(&p, 0);
    put_u16(&p, (uint16_t)count);
    put_u16(&p, (uint16_t)header);
    for (i = 0u; i < count; i++) {
        size_t slen = strlen(strs[i]) * 2u;
        uint8_t *rec = buf + 6u + (size_t)i * 12u;
        uint8_t *rp = rec;
        if (storage + slen > cap) {
            return 0u;
        }
        put_u16(&rp, 3);
        put_u16(&rp, 1);
        put_u16(&rp, 0x0409);
        put_u16(&rp, ids[i]);
        put_u16(&rp, (uint16_t)slen);
        put_u16(&rp, (uint16_t)(storage - header));
        {
            uint8_t *sp = buf + storage;
            put_utf16be_ascii(&sp, strs[i]);
        }
        storage += slen;
    }
    return storage;
}

static size_t build_cmap_fmt4_ascii(uint8_t *buf, uint16_t cp, uint16_t gid)
{
    uint8_t *p = buf;
    uint8_t *len_ptr;
    put_u16(&p, 0);
    put_u16(&p, 1);
    put_u16(&p, 3);
    put_u16(&p, 1);
    put_u32(&p, 12u);
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

static size_t build_cpal_2x2(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 0);
    put_u16(&p, 2);
    put_u16(&p, 2);
    put_u16(&p, 4);
    put_u32(&p, 16u);
    put_u16(&p, 0);
    put_u16(&p, 2);
    put_u8(&p, 0);
    put_u8(&p, 0);
    put_u8(&p, 255);
    put_u8(&p, 255);
    put_u8(&p, 255);
    put_u8(&p, 0);
    put_u8(&p, 0);
    put_u8(&p, 255);
    put_u8(&p, 255);
    put_u8(&p, 0);
    put_u8(&p, 0);
    put_u8(&p, 255);
    put_u8(&p, 0);
    put_u8(&p, 255);
    put_u8(&p, 255);
    put_u8(&p, 255);
    return (size_t)(p - buf);
}

static size_t build_colr_v0_layers(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 0);
    put_u16(&p, 1);
    put_u32(&p, 14u);
    put_u32(&p, 20u);
    put_u16(&p, 2);
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 2);
    put_u16(&p, 2);
    put_u16(&p, 0);
    put_u16(&p, 3);
    put_u16(&p, 1);
    return (size_t)(p - buf);
}

/* Minimal CFF INDEX/DICT probe bytes (not a full OTTO font). */
static size_t build_cff_index_probe(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 2u);
    put_u8(&p, 1u);
    put_u8(&p, 1u);
    put_u8(&p, 2u);
    put_u8(&p, 4u);
    put_u8(&p, 'A');
    put_u8(&p, 'B');
    put_u8(&p, 'C');
    return (size_t)(p - buf);
}

typedef struct {
    const char *family;
    const char *ps;
    uint16_t num_glyphs;
    uint16_t weight;
    uint16_t width;
    int with_color;
    int with_cmap;
    uint16_t cmap_cp;  /* 0 → U+0041 */
    uint16_t cmap_gid; /* 0 → 1 */
} conform_font_spec_t;

static size_t build_conform_font(uint8_t *out, size_t cap, const conform_font_spec_t *spec)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[256], loca[256], post[64], os2[128];
    uint8_t name[512], cmap[256], cpal[64], colr[64];
    synth_table_t tabs[16];
    uint32_t nt = 0u;
    size_t nn, nc = 0u;
    uint16_t glyphs;
    uint16_t map_cp;
    uint16_t map_gid;

    if (!spec || spec->num_glyphs < 1u) {
        return 0u;
    }
    glyphs = spec->num_glyphs;
    map_cp = spec->cmap_cp ? spec->cmap_cp : 0x41u;
    map_gid = spec->cmap_gid ? spec->cmap_gid : 1u;
    nn = build_name_table(name, sizeof(name), spec->family, spec->ps);
    if (nn == 0u) {
        return 0u;
    }
    if (spec->with_cmap) {
        nc = build_cmap_fmt4_ascii(cmap, map_cp, map_gid);
        if (nc == 0u) {
            return 0u;
        }
    }

    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HEAD, head, (uint32_t)build_head(head)};
    tabs[nt++] =
        (synth_table_t){ROBO_FONT_TAG_HHEA, hhea, (uint32_t)build_hhea(hhea, glyphs)};
    tabs[nt++] =
        (synth_table_t){ROBO_FONT_TAG_MAXP, maxp, (uint32_t)build_maxp(maxp, glyphs)};
    tabs[nt++] =
        (synth_table_t){ROBO_FONT_TAG_HMTX, hmtx, (uint32_t)build_hmtx(hmtx, glyphs)};
    tabs[nt++] =
        (synth_table_t){ROBO_FONT_TAG_LOCA, loca, (uint32_t)build_loca(loca, glyphs)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_GLYF, head, 0u};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_POST, post, (uint32_t)build_post(post)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_OS2, os2,
                                  (uint32_t)build_os2(os2, spec->weight, spec->width)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_NAME, name, (uint32_t)nn};
    if (spec->with_cmap) {
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_CMAP, cmap, (uint32_t)nc};
    }
    if (spec->with_color) {
        tabs[nt++] =
            (synth_table_t){ROBO_FONT_TAG_CPAL, cpal, (uint32_t)build_cpal_2x2(cpal)};
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_COLR, colr,
                                      (uint32_t)build_colr_v0_layers(colr)};
    }
    return build_sfnt(out, cap, ROBO_FONT_SFNT_VERSION_TRUE, tabs, nt);
}

static conform_font_spec_t make_spec(const char *family, const char *ps, uint16_t glyphs,
                                     uint16_t weight, uint16_t width, int color, int cmap)
{
    conform_font_spec_t s;
    memset(&s, 0, sizeof(s));
    s.family = family;
    s.ps = ps;
    s.num_glyphs = glyphs;
    s.weight = weight;
    s.width = width;
    s.with_color = color;
    s.with_cmap = cmap;
    return s;
}

static conform_font_spec_t make_spec_cmap(const char *family, const char *ps,
                                          uint16_t glyphs, uint16_t cp, uint16_t gid)
{
    conform_font_spec_t s = make_spec(family, ps, glyphs, 400u, 5u, 0, 1);
    s.cmap_cp = cp;
    s.cmap_gid = gid;
    return s;
}

static void load_face(const uint8_t *font, size_t n, robo_font_source_t **src,
                      robo_font_face_t **face)
{
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(*src, 0u, face), ROBO_OK);
}

/* ---- Tests ---- */

static void test_conform_architecture(void)
{
    robo_font_conform_result_t r;
    robo_font_golden_data_t g;
    robo_font_conform_case_t c;

    ROBO_CHECK_STR_EQ(robo_font_conform_category_string(ROBO_FONT_CONFORM_SFNT), "sfnt");
    ROBO_CHECK_STR_EQ(robo_font_conform_kind_string(ROBO_FONT_CONFORM_KIND_GOLDEN),
                      "golden");
    ROBO_CHECK_STR_EQ(robo_font_version_string(), "0.22.0");

    c.id = "font.conform.architecture";
    c.category = ROBO_FONT_CONFORM_GOLDEN;
    c.kind = ROBO_FONT_CONFORM_KIND_UNIT;
    c.fixture = "synthetic:none";
    c.purpose = "verify conformance helper API";
    ROBO_CHECK(c.id != NULL);

    robo_font_conform_result_init(&r);
    ROBO_CHECK(r.ok);
    ROBO_CHECK_EQ_INT(robo_font_conform_result_add(&r, "fx", "head", 1u, 0x41u, 0u, "1",
                                                   "2", "mismatch"),
                      ROBO_OK);
    ROBO_CHECK(!r.ok);
    ROBO_CHECK_EQ_INT((int)r.diagnostic_count, 1);
    ROBO_CHECK_STR_EQ(r.diagnostics[0].expected, "1");
    ROBO_CHECK_STR_EQ(r.diagnostics[0].actual, "2");

    robo_font_golden_init(&g);
    ROBO_CHECK_EQ_INT(robo_font_golden_append_u32(&g, "n", 3u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_golden_append_i32(&g, "d", -2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_golden_append_str(&g, "s", "ok"), ROBO_OK);
    ROBO_CHECK(robo_font_golden_equals_cstr(&g, "n=3\nd=-2\ns=ok\n"));
}

static void test_conform_sfnt(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_sfnt_table_record_t rec;
    robo_font_span_t span;
    conform_font_spec_t spec = make_spec("ConformSFNT", "ConformSFNT", 2u, 400u, 5u, 0, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0u, ROBO_FONT_SFNT_FLAGS_NONE, &sfnt),
                      ROBO_OK);
    ROBO_CHECK(robo_font_sfnt_table_count(sfnt) >= 10u);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_find_table(sfnt, ROBO_FONT_TAG_HEAD, &rec), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_HEAD, &span), ROBO_OK);
    ROBO_CHECK(span.size >= 54u);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_find_table(sfnt, ROBO_FONT_TAG_MAXP, &rec), ROBO_OK);
    ROBO_CHECK(robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_MAXP));
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);

    /* Malformed truncated directory must fail safely. */
    ROBO_CHECK_EQ_INT(
        robo_font_source_create_memory_copy(k_corpus_truncated_sfnt,
                                            sizeof(k_corpus_truncated_sfnt), &src),
        ROBO_OK);
    ROBO_CHECK(robo_font_sfnt_parse(src, 0u, ROBO_FONT_SFNT_FLAGS_NONE, &sfnt) != ROBO_OK);
    robo_font_source_release(src);
}

static void test_conform_truetype(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    uint32_t glyphs = 0u;
    robo_font_metrics_t m;
    conform_font_spec_t spec = make_spec("ConformTT", "ConformTT", 3u, 400u, 5u, 0, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, 0u), ROBO_OK);
    ROBO_CHECK(robo_font_face_truetype(face) != NULL);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_count(face, &glyphs), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)glyphs, 3);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_layout_metrics(face, &m), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)m.units_per_em, 1000);
    ROBO_CHECK_EQ_INT((int)m.ascent, 800);
    ROBO_CHECK_EQ_INT((int)m.descent, 200);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_conform_opentype(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    conform_font_spec_t spec = make_spec("ConformOT", "ConformOT", 2u, 400u, 5u, 0, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0u, ROBO_FONT_SFNT_FLAGS_NONE, &sfnt),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_ot_detect_outline(sfnt),
                      (int)ROBO_FONT_OUTLINE_TRUETYPE);
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
}

static void test_conform_cff(void)
{
    uint8_t buf[64];
    size_t n;
    robo_font_cff_index_t idx;
    robo_font_span_t span;
    robo_font_span_t obj;
    size_t end = 0u;

    n = build_cff_index_probe(buf);
    span.data = buf;
    span.size = n;
    memset(&idx, 0, sizeof(idx));
    ROBO_CHECK_EQ_INT(robo_font_cff_index_parse(span, 0, &idx, &end), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_cff_index_count(&idx), 2);
    ROBO_CHECK_EQ_INT(robo_font_cff_index_get(&idx, 0, &obj), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)obj.size, 1);
    ROBO_CHECK_EQ_INT(obj.data[0], 'A');
    robo_font_cff_index_clear(&idx);
}

static void test_conform_cmap(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_cmap_lookup_result_t lr;
    robo_font_golden_data_t g;
    conform_font_spec_t spec = make_spec("ConformCmap", "ConformCmap", 2u, 400u, 5u, 0, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_cmap(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_lookup_glyph(face, 0x41u, &lr), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)lr.status, (int)ROBO_FONT_CMAP_MAPPED);
    ROBO_CHECK_EQ_INT((int)lr.glyph_id, 1);
    ROBO_CHECK_EQ_INT(robo_font_face_lookup_glyph(face, 0x42u, &lr), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)lr.status, (int)ROBO_FONT_CMAP_UNMAPPED);

    /* Surrogate / out-of-range must not map. */
    ROBO_CHECK_EQ_INT(robo_font_face_lookup_glyph(face, 0xD800u, &lr), ROBO_OK);
    ROBO_CHECK((int)lr.status != (int)ROBO_FONT_CMAP_MAPPED);
    ROBO_CHECK_EQ_INT(robo_font_face_lookup_glyph(face, 0x110000u, &lr), ROBO_OK);
    ROBO_CHECK((int)lr.status != (int)ROBO_FONT_CMAP_MAPPED);

    robo_font_golden_init(&g);
    ROBO_CHECK_EQ_INT(robo_font_golden_append_u32(&g, "U+0041", 1u), ROBO_OK);
    ROBO_CHECK(robo_font_golden_equals_cstr(&g, "U+0041=1\n"));

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_conform_metrics(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_metrics_t m;
    robo_font_glyph_metrics_t gm;
    robo_font_golden_data_t g;
    conform_font_spec_t spec = make_spec("ConformMetrics", "ConformMetrics", 2u, 400u, 5u, 0, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_layout_metrics(face, &m), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_metrics(face, 1u, &gm), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)gm.status, (int)ROBO_FONT_GLYPH_METRICS_OK);
    ROBO_CHECK_EQ_INT((int)gm.advance_width, 500);
    ROBO_CHECK_EQ_INT((int)gm.left_side_bearing, 10);

    robo_font_golden_init(&g);
    ROBO_CHECK_EQ_INT(robo_font_golden_append_u32(&g, "upem", m.units_per_em), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_golden_append_i32(&g, "ascent", (int32_t)m.ascent),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_golden_append_i32(&g, "descent", (int32_t)m.descent),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_golden_append_u32(&g, "adv1", gm.advance_width), ROBO_OK);
    ROBO_CHECK(robo_font_golden_equals_cstr(
        &g, "upem=1000\nascent=800\ndescent=200\nadv1=500\n"));

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_conform_names(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    const robo_font_metadata_t *meta;
    conform_font_spec_t spec = make_spec("ConformName", "ConformNamePS", 2u, 400u, 5u, 0, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metadata(face), ROBO_OK);
    meta = robo_font_face_metadata(face);
    ROBO_CHECK(meta != NULL);
    ROBO_CHECK_STR_EQ(meta->family_name.utf8, "ConformName");
    ROBO_CHECK_STR_EQ(meta->postscript_name.utf8, "ConformNamePS");
    ROBO_CHECK_STR_EQ(meta->subfamily_name.utf8, "Regular");
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_conform_style(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    const robo_font_classification_t *cls;
    conform_font_spec_t spec = make_spec("ConformStyle", "ConformStyle", 2u, 700u, 5u, 0, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_classification(face), ROBO_OK);
    cls = robo_font_face_classification(face);
    ROBO_CHECK(cls != NULL);
    ROBO_CHECK_EQ_INT((int)cls->weight, 700);
    ROBO_CHECK_EQ_INT((int)cls->style, (int)ROBO_FONT_STYLE_NORMAL);
    ROBO_CHECK_EQ_INT((int)cls->stretch, 100);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_conform_selection(void)
{
    uint8_t fa[4096], fb[4096];
    size_t na, nb;
    robo_font_source_t *sa = NULL, *sb = NULL;
    robo_font_face_t *a = NULL, *b = NULL;
    robo_font_face_t *cands[2];
    robo_font_select_request_t req;
    robo_font_selection_t sel;
    conform_font_spec_t sa_spec = make_spec("SelProbe", "SelProbe", 2u, 400u, 5u, 0, 1);
    conform_font_spec_t sb_spec = make_spec("SelProbe", "SelProbeBold", 2u, 700u, 5u, 0, 1);

    na = build_conform_font(fa, sizeof(fa), &sa_spec);
    nb = build_conform_font(fb, sizeof(fb), &sb_spec);
    load_face(fa, na, &sa, &a);
    load_face(fb, nb, &sb, &b);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(a, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(b, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metadata(a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metadata(b), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_classification(a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_classification(b), ROBO_OK);

    cands[0] = a;
    cands[1] = b;
    robo_font_select_request_init(&req);
    req.family = "selprobe";
    req.weight = 700;
    req.stretch = 100;
    req.style = ROBO_FONT_STYLE_NORMAL;
    ROBO_CHECK_EQ_INT(robo_font_select(&req, cands, 2u, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK(sel.face == b);

    /* Determinism: same request twice → same face. */
    {
        robo_font_selection_t sel2;
        ROBO_CHECK_EQ_INT(robo_font_select(&req, cands, 2u, &sel2), ROBO_OK);
        ROBO_CHECK(sel2.face == sel.face);
        robo_font_selection_clear(&sel2);
    }
    robo_font_selection_clear(&sel);
    robo_font_face_release(a);
    robo_font_face_release(b);
    robo_font_source_release(sa);
    robo_font_source_release(sb);
}

static void test_conform_discovery(void)
{
    uint8_t ttf[4] = {0x00, 0x01, 0x00, 0x00};
    uint8_t otto[4] = {'O', 'T', 'T', 'O'};
    uint8_t ttc[4] = {'t', 't', 'c', 'f'};

    ROBO_CHECK_EQ_INT(robo_font_discovery_classify_bytes(ttf, 4), ROBO_FONT_CANDIDATE_TTF);
    ROBO_CHECK_EQ_INT(robo_font_discovery_classify_bytes(otto, 4), ROBO_FONT_CANDIDATE_OTF);
    ROBO_CHECK_EQ_INT(robo_font_discovery_classify_bytes(ttc, 4),
                      ROBO_FONT_CANDIDATE_COLLECTION);
    ROBO_CHECK(robo_font_discovery_extension_is_font("Fonts/Probe.ttf"));
    ROBO_CHECK(robo_font_discovery_extension_is_font("X.otc"));
    ROBO_CHECK(!robo_font_discovery_extension_is_font("readme.txt"));
}

static void test_conform_loading(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_validation_result_t *vres = NULL;
    robo_font_face_t *face = NULL;
    conform_font_spec_t spec = make_spec("ConformLoad", "ConformLoad", 2u, 400u, 5u, 0, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_validate_for_load(face, ROBO_FONT_VALIDATE_NORMAL, &vres), ROBO_OK);
    if (vres) {
        ROBO_CHECK(robo_font_validation_result_ok(vres));
        robo_font_validation_result_release(vres);
        vres = NULL;
    }
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, 0u), ROBO_OK);
    robo_font_face_release(face);
    robo_font_source_release(src);

    /* Malformed source must fail structural validate before trusted use. */
    ROBO_CHECK_EQ_INT(
        robo_font_source_create_memory_copy(k_corpus_truncated_sfnt,
                                            sizeof(k_corpus_truncated_sfnt), &src),
        ROBO_OK);
    {
        robo_font_validation_policy_t pol;
        robo_font_validation_policy_init(&pol);
        pol.level = ROBO_FONT_VALIDATION_STRICT;
        ROBO_CHECK(robo_font_validate_source(src, 0u, &pol, &vres, NULL) != ROBO_OK ||
                   (vres && !robo_font_validation_result_ok(vres)));
        if (vres) {
            robo_font_validation_result_release(vres);
        }
    }
    robo_font_source_release(src);
}

static void test_conform_coverage(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    const robo_font_coverage_t *cov;
    conform_font_spec_t spec = make_spec("ConformCov", "ConformCov", 2u, 400u, 5u, 0, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_cmap(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_coverage(face), ROBO_OK);
    cov = robo_font_face_coverage(face);
    ROBO_CHECK(cov != NULL);
    ROBO_CHECK(robo_font_coverage_contains(cov, 0x41u));
    ROBO_CHECK(!robo_font_coverage_contains(cov, 0x42u));
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_conform_fallback(void)
{
    uint8_t latin[4096], cover[4096];
    size_t nl, nc;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t s1 = 0u, s2 = 0u;
    robo_font_discovery_result_t dr;
    robo_font_fallback_resolver_t *resolver = NULL;
    robo_font_fallback_request_t req;
    robo_font_fallback_result_t res;
    uint32_t cp = 0x41u;
    robo_font_face_t *primary = NULL;
    robo_font_face_t *covered = NULL;
    /* Primary covers 'B' only; alt covers 'A' — request U+0041 must fall back. */
    conform_font_spec_t miss = make_spec_cmap("FbMiss", "FbMiss", 2u, 0x42u, 1u);
    conform_font_spec_t hit = make_spec_cmap("FbHit", "FbHit", 2u, 0x41u, 1u);
    uint32_t i;

    nl = build_conform_font(latin, sizeof(latin), &miss);
    nc = build_conform_font(cover, sizeof(cover), &hit);
    ROBO_CHECK(nl > 0u && nc > 0u);

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "mem:miss", latin, nl, &s1), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "mem:hit", cover, nc, &s2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_all(reg, 0u, &dr), ROBO_OK);
    ROBO_CHECK((int)robo_font_registry_registration_count(reg) >= 2);

    for (i = 0u; i < robo_font_registry_registration_count(reg); i++) {
        const robo_font_registration_t *rec = NULL;
        ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, i, &rec), ROBO_OK);
        if (rec && strcmp(rec->family, "FbMiss") == 0) {
            primary = rec->face;
        }
        if (rec && strcmp(rec->family, "FbHit") == 0) {
            covered = rec->face;
        }
    }
    ROBO_CHECK(primary != NULL && covered != NULL);

    ROBO_CHECK_EQ_INT(robo_font_fallback_resolver_create(reg, NULL, &resolver), ROBO_OK);
    robo_font_fallback_request_init(&req);
    req.primary = primary;
    req.codepoints = &cp;
    req.codepoint_count = 1u;
    robo_font_fallback_result_init(&res);
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolve(resolver, &req, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.found, 1);
    ROBO_CHECK(res.face == covered || res.used_primary);
    if (!res.used_primary) {
        ROBO_CHECK(res.face == covered);
    }
    robo_font_fallback_result_clear(&res);
    robo_font_fallback_resolver_destroy(resolver);
    robo_font_registry_destroy(reg);
}

static void test_conform_generic(void)
{
    robo_font_generic_family_t g = ROBO_FONT_GENERIC_UNKNOWN;
    uint32_t i;

    ROBO_CHECK(!robo_font_generic_family_is_valid(ROBO_FONT_GENERIC_UNKNOWN));
    for (i = 1u; i < (uint32_t)ROBO_FONT_GENERIC_COUNT; i++) {
        ROBO_CHECK(robo_font_generic_family_is_valid((robo_font_generic_family_t)i));
        ROBO_CHECK(robo_font_generic_family_name((robo_font_generic_family_t)i) != NULL);
    }
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("serif", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)g, (int)ROBO_FONT_GENERIC_SERIF);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("sans-serif", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)g, (int)ROBO_FONT_GENERIC_SANS_SERIF);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("monospace", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("system-ui", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("emoji", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("math", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("ui-serif", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("ui-sans-serif", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("ui-monospace", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("ui-rounded", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("cursive", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("fantasy", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("not-a-generic", 0u, &g),
                      ROBO_NOT_FOUND);
}

static void test_conform_emoji(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    conform_font_spec_t spec = make_spec("ConformEmoji", "ConformEmoji", 2u, 400u, 5u, 1, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_color(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_emoji(face), ROBO_OK);
    ROBO_CHECK(robo_font_face_emoji(face) != NULL);
    /* VS15/VS16 scalars are valid Unicode; classification must not crash. */
    {
        robo_font_cmap_lookup_result_t lr;
        (void)robo_font_face_parse_cmap(face);
        (void)robo_font_face_lookup_glyph(face, 0xFE0Eu, &lr);
        (void)robo_font_face_lookup_glyph(face, 0xFE0Fu, &lr);
    }
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_conform_variation(void)
{
    float out = 0.0f;
    robo_font_golden_data_t g;

    ROBO_CHECK_EQ_INT(
        robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 100.0f, &out), ROBO_OK);
    ROBO_CHECK(out == -1.0f);
    ROBO_CHECK_EQ_INT(
        robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 400.0f, &out), ROBO_OK);
    ROBO_CHECK(out == 0.0f);
    ROBO_CHECK_EQ_INT(
        robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 900.0f, &out), ROBO_OK);
    ROBO_CHECK(out == 1.0f);

    /* Property: clamp then normalize is monotonic for midpoints. */
    {
        float a = 0.0f, b = 0.0f;
        float ca, cb;
        ROBO_CHECK_EQ_INT(
            robo_font_variation_clamp_user(100.0f, 400.0f, 900.0f, 50.0f, &ca), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_font_variation_clamp_user(100.0f, 400.0f, 900.0f, 950.0f, &cb), ROBO_OK);
        ROBO_CHECK(ca == 100.0f);
        ROBO_CHECK(cb == 900.0f);
        ROBO_CHECK_EQ_INT(
            robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 250.0f, &a), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 650.0f, &b), ROBO_OK);
        ROBO_CHECK(a < 0.0f && b > 0.0f);
    }

    robo_font_golden_init(&g);
    ROBO_CHECK_EQ_INT(
        robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 400.0f, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_golden_append_f32(&g, "wght_def", out), ROBO_OK);
    ROBO_CHECK(robo_font_golden_equals_cstr(&g, "wght_def=0.000000\n"));
}

static void test_conform_color(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    const robo_font_color_info_t *ci;
    conform_font_spec_t spec = make_spec("ConformColor", "ConformColor", 4u, 400u, 5u, 1, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_color(face), ROBO_OK);
    ci = robo_font_face_color(face);
    ROBO_CHECK(ci != NULL);
    ROBO_CHECK(robo_font_color_info_is_color_capable(ci));
    ROBO_CHECK(robo_font_color_info_capabilities(ci) != 0u);
    /* SVG remains inert — no execute path exists; color info is data only. */
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_conform_security(void)
{
    robo_font_source_t *src = NULL;
    robo_font_validation_policy_t pol;
    robo_font_validation_result_t *vres = NULL;

    robo_font_validation_policy_init(&pol);
    pol.level = ROBO_FONT_VALIDATION_STRICT;

    ROBO_CHECK_EQ_INT(
        robo_font_source_create_memory_copy(k_corpus_truncated_sfnt,
                                            sizeof(k_corpus_truncated_sfnt), &src),
        ROBO_OK);
    ROBO_CHECK(robo_font_validate_source(src, 0u, &pol, &vres, NULL) != ROBO_OK ||
               (vres && !robo_font_validation_result_ok(vres)));
    if (vres) {
        robo_font_validation_result_release(vres);
        vres = NULL;
    }
    robo_font_source_release(src);

    ROBO_CHECK_EQ_INT(
        robo_font_source_create_memory_copy(k_corpus_huge_tables,
                                            sizeof(k_corpus_huge_tables), &src),
        ROBO_OK);
    ROBO_CHECK(robo_font_validate_source(src, 0u, &pol, &vres, NULL) != ROBO_OK ||
               (vres && !robo_font_validation_result_ok(vres)));
    if (vres) {
        robo_font_validation_result_release(vres);
    }
    robo_font_source_release(src);

    ROBO_CHECK_EQ_INT(robo_font_validate_fuzz_parse(k_corpus_truncated_sfnt,
                                                    sizeof(k_corpus_truncated_sfnt)),
                      ROBO_OK);
}

static void test_conform_limits(void)
{
    robo_font_validation_limits_t lim;
    robo_font_validation_policy_t pol;
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_validation_result_t *vres = NULL;
    conform_font_spec_t spec = make_spec("ConformLim", "ConformLim", 2u, 400u, 5u, 0, 1);

    robo_font_validation_limits_init(&lim);
    /* Below hard max: clamp leaves value. */
    lim.max_glyphs = 100u;
    robo_font_validation_limits_clamp(&lim);
    ROBO_CHECK_EQ_INT((int)lim.max_glyphs, 100);

    /* At hard max. */
    lim.max_glyphs = ROBO_FONT_VALIDATION_HARD_MAX_GLYPHS;
    robo_font_validation_limits_clamp(&lim);
    ROBO_CHECK_EQ_INT((int)lim.max_glyphs, (int)ROBO_FONT_VALIDATION_HARD_MAX_GLYPHS);

    /* Above hard max → clamped down. */
    lim.max_glyphs = ROBO_FONT_VALIDATION_HARD_MAX_GLYPHS + 1u;
    robo_font_validation_limits_clamp(&lim);
    ROBO_CHECK_EQ_INT((int)lim.max_glyphs, (int)ROBO_FONT_VALIDATION_HARD_MAX_GLYPHS);

    /* Tiny glyph budget rejects our 2-glyph font under STRICT. */
    n = build_conform_font(font, sizeof(font), &spec);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    robo_font_validation_policy_init(&pol);
    pol.level = ROBO_FONT_VALIDATION_STRICT;
    pol.reject_oversized = 1u;
    pol.limits = lim;
    pol.limits.max_glyphs = 1u;
    robo_font_validation_limits_clamp(&pol.limits);
    ROBO_CHECK(robo_font_validate_source(src, 0u, &pol, &vres, NULL) != ROBO_OK ||
               (vres && !robo_font_validation_result_ok(vres)));
    if (vres) {
        robo_font_validation_result_release(vres);
    }
    robo_font_source_release(src);
}

static void test_conform_cross_table(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    uint32_t glyphs = 0u;
    robo_font_glyph_metrics_t gm;
    conform_font_spec_t spec = make_spec("ConformX", "ConformX", 3u, 400u, 5u, 0, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_count(face, &glyphs), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)glyphs, 3);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face), ROBO_OK);
    /* maxp ↔ hmtx: every glyph id in range has metrics. */
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_metrics(face, 0u, &gm), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)gm.status, (int)ROBO_FONT_GLYPH_METRICS_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_metrics(face, 2u, &gm), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)gm.status, (int)ROBO_FONT_GLYPH_METRICS_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_metrics(face, 3u, &gm), ROBO_OK);
    ROBO_CHECK((int)gm.status != (int)ROBO_FONT_GLYPH_METRICS_OK);

    /* cmap ↔ maxp: mapped gid must be < numGlyphs. */
    ROBO_CHECK_EQ_INT(robo_font_face_parse_cmap(face), ROBO_OK);
    {
        robo_font_cmap_lookup_result_t lr;
        ROBO_CHECK_EQ_INT(robo_font_face_lookup_glyph(face, 0x41u, &lr), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)lr.status, (int)ROBO_FONT_CMAP_MAPPED);
        ROBO_CHECK(lr.glyph_id < glyphs);
    }
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_conform_golden(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    const robo_font_metadata_t *meta;
    robo_font_metrics_t m;
    robo_font_golden_data_t g;
    conform_font_spec_t spec = make_spec("GoldenFace", "GoldenFacePS", 2u, 400u, 5u, 0, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metadata(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face), ROBO_OK);
    meta = robo_font_face_metadata(face);
    ROBO_CHECK_EQ_INT(robo_font_face_layout_metrics(face, &m), ROBO_OK);

    robo_font_golden_init(&g);
    ROBO_CHECK_EQ_INT(robo_font_golden_append_str(&g, "family", meta->family_name.utf8),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_golden_append_str(&g, "ps", meta->postscript_name.utf8), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_golden_append_u32(&g, "upem", m.units_per_em), ROBO_OK);
    ROBO_CHECK(robo_font_golden_equals_cstr(
        &g, "family=GoldenFace\nps=GoldenFacePS\nupem=1000\n"));

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_conform_property(void)
{
    uint32_t i;
    float prev = -2.0f;

    /* Property: normalize(min..max) is non-decreasing for increasing user values. */
    for (i = 0u; i <= 10u; i++) {
        float user = 100.0f + (float)i * 80.0f;
        float nrm = 0.0f;
        ROBO_CHECK_EQ_INT(
            robo_font_variation_normalize(100.0f, 400.0f, 900.0f, user, &nrm), ROBO_OK);
        ROBO_CHECK(nrm >= prev);
        prev = nrm;
    }

    /* Property: coverage of unmapped codepoint is false when cmap present. */
    {
        uint8_t font[4096];
        size_t n;
        robo_font_source_t *src = NULL;
        robo_font_face_t *face = NULL;
        const robo_font_coverage_t *cov;
        conform_font_spec_t spec = make_spec("PropCov", "PropCov", 2u, 400u, 5u, 0, 1);
        n = build_conform_font(font, sizeof(font), &spec);
        load_face(font, n, &src, &face);
        ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(face, 0u), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_face_parse_cmap(face), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_face_parse_coverage(face), ROBO_OK);
        cov = robo_font_face_coverage(face);
        for (i = 0u; i < 26u; i++) {
            uint32_t cp = 0x61u + i; /* a-z unmapped */
            ROBO_CHECK(!robo_font_coverage_contains(cov, cp));
        }
        ROBO_CHECK(robo_font_coverage_contains(cov, 0x41u));
        robo_font_face_release(face);
        robo_font_source_release(src);
    }
}

static void test_conform_fuzz(void)
{
    uint8_t junk[64];
    uint32_t i;
    uint8_t font[4096];
    size_t n;
    conform_font_spec_t spec = make_spec("FuzzFace", "FuzzFace", 2u, 400u, 5u, 1, 1);

    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 37u);
    }
    ROBO_CHECK_EQ_INT(robo_font_conform_fuzz_smoke(NULL, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_conform_fuzz_smoke(junk, sizeof(junk)), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_conform_fuzz_smoke(k_corpus_truncated_sfnt, sizeof(k_corpus_truncated_sfnt)),
        ROBO_OK);

    n = build_conform_font(font, sizeof(font), &spec);
    ROBO_CHECK_EQ_INT(robo_font_conform_fuzz_smoke(font, n), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_color_fuzz_parse(font, n), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_variation_fuzz_parse(font, n), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_selection_fuzz_parse(font, n), ROBO_OK);
}

static void test_conform_bench(void)
{
    uint8_t font[4096];
    size_t n;
    uint32_t i;
    uint32_t hits = 0u;
    conform_font_spec_t spec = make_spec("BenchFace", "BenchFace", 2u, 400u, 5u, 0, 1);

    n = build_conform_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);

    for (i = 0u; i < 50u; i++) {
        robo_font_source_t *src = NULL;
        robo_font_face_t *face = NULL;
        robo_font_cmap_lookup_result_t lr;
        if (robo_font_source_create_memory_copy(font, n, &src) != ROBO_OK) {
            continue;
        }
        if (robo_font_face_create(src, 0u, &face) != ROBO_OK) {
            robo_font_source_release(src);
            continue;
        }
        if (robo_font_face_parse_truetype(face, 0u) == ROBO_OK &&
            robo_font_face_parse_cmap(face) == ROBO_OK &&
            robo_font_face_lookup_glyph(face, 0x41u, &lr) == ROBO_OK &&
            lr.status == ROBO_FONT_CMAP_MAPPED) {
            hits++;
        }
        robo_font_face_release(face);
        robo_font_source_release(src);
    }
    printf("font.conform.bench hits=%u\n", (unsigned)hits);
    ROBO_CHECK_EQ_INT((int)hits, 50);
}

void robo_test_register_font_conformance(void)
{
    robo_test_register("font.conform.architecture", test_conform_architecture);
    robo_test_register("font.conform.sfnt", test_conform_sfnt);
    robo_test_register("font.conform.truetype", test_conform_truetype);
    robo_test_register("font.conform.opentype", test_conform_opentype);
    robo_test_register("font.conform.cff", test_conform_cff);
    robo_test_register("font.conform.cmap", test_conform_cmap);
    robo_test_register("font.conform.metrics", test_conform_metrics);
    robo_test_register("font.conform.names", test_conform_names);
    robo_test_register("font.conform.style", test_conform_style);
    robo_test_register("font.conform.selection", test_conform_selection);
    robo_test_register("font.conform.discovery", test_conform_discovery);
    robo_test_register("font.conform.loading", test_conform_loading);
    robo_test_register("font.conform.coverage", test_conform_coverage);
    robo_test_register("font.conform.fallback", test_conform_fallback);
    robo_test_register("font.conform.generic", test_conform_generic);
    robo_test_register("font.conform.emoji", test_conform_emoji);
    robo_test_register("font.conform.variation", test_conform_variation);
    robo_test_register("font.conform.color", test_conform_color);
    robo_test_register("font.conform.security", test_conform_security);
    robo_test_register("font.conform.limits", test_conform_limits);
    robo_test_register("font.conform.cross_table", test_conform_cross_table);
    robo_test_register("font.conform.golden", test_conform_golden);
    robo_test_register("font.conform.property", test_conform_property);
    robo_test_register("font.conform.fuzz", test_conform_fuzz);
    robo_test_register("font.conform.bench", test_conform_bench);
}
