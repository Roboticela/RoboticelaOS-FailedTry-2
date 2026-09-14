/*
 * RoboFont Phase 5 final integration / API freeze suite (Goal 5.22).
 *
 * Freeze pin, end-to-end public-API acceptance, fuzz smoke, performance baseline.
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontVersion.h"
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

static size_t build_sfnt(uint8_t *out, size_t out_cap, const synth_table_t *tables,
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
    put_u32(&p, ROBO_FONT_SFNT_VERSION_TRUE);
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

static size_t build_os2(uint8_t *buf)
{
    uint8_t *p = buf;
    int i;
    put_u16(&p, 4);
    put_i16(&p, 500);
    put_u16(&p, 400);
    put_u16(&p, 5);
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

static size_t build_cmap_fmt4(uint8_t *buf, uint16_t cp, uint16_t gid)
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

static size_t build_cpal(uint8_t *buf)
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

static size_t build_colr(uint8_t *buf)
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

static size_t build_phase5_font(uint8_t *out, size_t cap, const char *family,
                                const char *ps, uint16_t cmap_cp, int with_color)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], loca[32], post[64], os2[128];
    uint8_t name[512], cmap[256], cpal[64], colr[64];
    synth_table_t tabs[16];
    uint32_t nt = 0u;
    size_t nn, nc;
    const uint16_t glyphs = 4u;

    nn = build_name_table(name, sizeof(name), family, ps);
    nc = build_cmap_fmt4(cmap, cmap_cp, 1u);
    if (nn == 0u || nc == 0u) {
        return 0u;
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
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_OS2, os2, (uint32_t)build_os2(os2)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_NAME, name, (uint32_t)nn};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_CMAP, cmap, (uint32_t)nc};
    if (with_color) {
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_CPAL, cpal, (uint32_t)build_cpal(cpal)};
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_COLR, colr, (uint32_t)build_colr(colr)};
    }
    return build_sfnt(out, cap, tabs, nt);
}

static void test_p5_freeze(void)
{
    ROBO_CHECK_EQ_INT(ROBO_FONT_API_FROZEN, 1);
    ROBO_CHECK_STR_EQ(robo_font_version_string(), "0.22.0");
    ROBO_CHECK_EQ_INT(ROBO_FONT_VERSION_MAJOR, 0);
    ROBO_CHECK_EQ_INT(ROBO_FONT_VERSION_MINOR, 22);
    ROBO_CHECK_EQ_INT(ROBO_FONT_VERSION_PATCH, 0);
    ROBO_CHECK_EQ_INT(robo_font_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_init(), ROBO_OK); /* idempotent */
    robo_font_shutdown();
}

static void test_p5_e2e_acceptance(void)
{
    uint8_t primary_bytes[4096];
    uint8_t fallback_bytes[4096];
    size_t np, nf;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_validation_result_t *vres = NULL;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t sid = 0u, sid2 = 0u;
    robo_font_discovery_result_t dr;
    const robo_font_registration_t *rec = NULL;
    const robo_font_metadata_t *meta;
    const robo_font_coverage_t *cov;
    robo_font_cmap_lookup_result_t lr;
    robo_font_metrics_t metrics;
    robo_font_glyph_metrics_t gm;
    robo_font_outline_t *outline = NULL;
    robo_font_select_request_t sreq;
    robo_font_selection_t sel;
    robo_font_face_t *cands[1];
    robo_font_fallback_resolver_t *fb = NULL;
    robo_font_fallback_request_t freq;
    robo_font_fallback_result_t fres;
    uint32_t cp = 0x41u;
    float norm = 0.0f;
    uint32_t i;
    robo_font_face_t *primary = NULL;
    robo_font_face_t *alt = NULL;

    np = build_phase5_font(primary_bytes, sizeof(primary_bytes), "Phase5Primary",
                           "Phase5Primary", 0x42u, 1);
    nf = build_phase5_font(fallback_bytes, sizeof(fallback_bytes), "Phase5Fallback",
                           "Phase5Fallback", 0x41u, 0);
    ROBO_CHECK(np > 0u && nf > 0u);

    /* 1–2: source + validate */
    ROBO_CHECK_EQ_INT(
        robo_font_source_create_memory_copy(primary_bytes, np, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_validate_for_load(face, ROBO_FONT_VALIDATE_NORMAL, &vres), ROBO_OK);
    if (vres) {
        ROBO_CHECK(robo_font_validation_result_ok(vres));
        robo_font_validation_result_release(vres);
        vres = NULL;
    }

    /* 3–4: register / discover */
    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "mem:p5-primary", primary_bytes, np,
                                             &sid),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "mem:p5-fallback", fallback_bytes, nf,
                                             &sid2),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_all(reg, 0u, &dr), ROBO_OK);
    ROBO_CHECK((int)robo_font_registry_registration_count(reg) >= 2);

    for (i = 0u; i < robo_font_registry_registration_count(reg); i++) {
        ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, i, &rec), ROBO_OK);
        if (rec && strcmp(rec->family, "Phase5Primary") == 0) {
            primary = rec->face;
        }
        if (rec && strcmp(rec->family, "Phase5Fallback") == 0) {
            alt = rec->face;
        }
    }
    ROBO_CHECK(primary != NULL && alt != NULL);

    /* 5–9: metadata, coverage, cmap, metrics, outline via frozen face APIs */
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(primary, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metadata(primary), ROBO_OK);
    meta = robo_font_face_metadata(primary);
    ROBO_CHECK(meta != NULL);
    ROBO_CHECK_STR_EQ(meta->family_name.utf8, "Phase5Primary");

    ROBO_CHECK_EQ_INT(robo_font_face_parse_cmap(primary), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_coverage(primary), ROBO_OK);
    cov = robo_font_face_coverage(primary);
    ROBO_CHECK(cov != NULL);
    ROBO_CHECK(robo_font_coverage_contains(cov, 0x42u));
    ROBO_CHECK(!robo_font_coverage_contains(cov, 0x41u));

    ROBO_CHECK_EQ_INT(robo_font_face_lookup_glyph(primary, 0x42u, &lr), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)lr.status, (int)ROBO_FONT_CMAP_MAPPED);
    ROBO_CHECK_EQ_INT((int)lr.glyph_id, 1);

    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(primary), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_layout_metrics(primary, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)metrics.units_per_em, 1000);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_metrics(primary, 1u, &gm), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)gm.advance_width, 500);

    ROBO_CHECK_EQ_INT(robo_font_face_parse_outlines(primary), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_outline(primary, 0u, &outline), ROBO_OK);
    /* Empty glyf → valid empty outline object or OK empty path. */
    if (outline) {
        robo_font_outline_release(outline);
        outline = NULL;
    }

    /* 10: selection */
    ROBO_CHECK_EQ_INT(robo_font_face_parse_classification(primary), ROBO_OK);
    cands[0] = primary;
    robo_font_select_request_init(&sreq);
    sreq.family = "phase5primary";
    sreq.weight = 400;
    sreq.stretch = 100;
    sreq.style = ROBO_FONT_STYLE_NORMAL;
    ROBO_CHECK_EQ_INT(robo_font_select(&sreq, cands, 1u, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK(sel.face == primary);
    robo_font_selection_clear(&sel);

    /* 11: fallback for U+0041 (primary lacks it) */
    ROBO_CHECK_EQ_INT(robo_font_face_parse_truetype(alt, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_cmap(alt), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_coverage(alt), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolver_create(reg, NULL, &fb), ROBO_OK);
    robo_font_fallback_request_init(&freq);
    freq.primary = primary;
    freq.codepoints = &cp;
    freq.codepoint_count = 1u;
    robo_font_fallback_result_init(&fres);
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolve(fb, &freq, &fres), ROBO_OK);
    ROBO_CHECK_EQ_INT(fres.found, 1);
    if (!fres.used_primary) {
        ROBO_CHECK(fres.face == alt);
    }
    robo_font_fallback_result_clear(&fres);
    robo_font_fallback_resolver_destroy(fb);

    /* 12: variation normalize foundation (no fvar required) */
    ROBO_CHECK_EQ_INT(
        robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 400.0f, &norm), ROBO_OK);
    ROBO_CHECK(norm == 0.0f);

    /* 13: color capabilities */
    ROBO_CHECK_EQ_INT(robo_font_face_parse_color(primary), ROBO_OK);
    ROBO_CHECK(robo_font_face_color(primary) != NULL);
    ROBO_CHECK(robo_font_color_info_is_color_capable(robo_font_face_color(primary)));

    /* 14–15: security gate already passed validate_for_load; re-probe STRICT OK */
    ROBO_CHECK_EQ_INT(
        robo_font_validate_for_load(face, ROBO_FONT_VALIDATE_STRICT, &vres), ROBO_OK);
    if (vres) {
        robo_font_validation_result_release(vres);
    }

    /* Generic family resolve must not invent a font without mapping */
    {
        robo_font_generic_family_t g = ROBO_FONT_GENERIC_UNKNOWN;
        ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("serif", 0u, &g), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)g, (int)ROBO_FONT_GENERIC_SERIF);
        ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("not-mapped-xyz", 0u, &g),
                          ROBO_NOT_FOUND);
    }

    robo_font_registry_destroy(reg);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_p5_matrix_smoke(void)
{
    static const uint8_t junk[4] = {'x', 'x', 'x', 'x'};
    /* Compact matrix: every major public surface remains callable. */
    ROBO_CHECK(robo_font_conform_category_string(ROBO_FONT_CONFORM_SECURITY) != NULL);
    ROBO_CHECK(robo_font_discovery_extension_is_font("A.ttf"));
    ROBO_CHECK_EQ_INT(robo_font_discovery_classify_bytes((const uint8_t *)"\0\1\0\0", 4),
                      ROBO_FONT_CANDIDATE_TTF);
    ROBO_CHECK_EQ_INT(robo_font_validate_fuzz_parse(NULL, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cmap_fuzz_parse(junk, sizeof(junk)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_conform_fuzz_smoke(NULL, 0u), ROBO_OK);
}

static void test_p5_chromium_independence(void)
{
    /*
     * Build.ps1 Assert-NoChromiumPath is the authoritative link-time fence.
     * This test asserts the frozen API functions without any Chromium types
     * in the call path (compile-time: only RoboFont / RoboBase / RoboMem).
     */
    ROBO_CHECK_STR_EQ(robo_font_version_string(), "0.22.0");
    ROBO_CHECK_EQ_INT(ROBO_FONT_API_FROZEN, 1);
}

static void test_p5_bench(void)
{
    uint8_t font[4096];
    size_t n;
    uint32_t i;
    uint32_t hits = 0u;

    n = build_phase5_font(font, sizeof(font), "BenchP5", "BenchP5", 0x41u, 0);
    ROBO_CHECK(n > 0u);
    for (i = 0u; i < 20u; i++) {
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
    printf("font.p5.bench hits=%u\n", (unsigned)hits);
    ROBO_CHECK_EQ_INT((int)hits, 20);
}

void robo_test_register_font_phase5(void)
{
    robo_test_register("font.p5.freeze", test_p5_freeze);
    robo_test_register("font.p5.e2e", test_p5_e2e_acceptance);
    robo_test_register("font.p5.matrix", test_p5_matrix_smoke);
    robo_test_register("font.p5.chromium", test_p5_chromium_independence);
    robo_test_register("font.p5.bench", test_p5_bench);
}
