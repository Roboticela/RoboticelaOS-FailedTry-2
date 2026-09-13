/*
 * RoboFont OpenType structural suite (Goal 5.4).
 * Synthetic fixtures — no Chromium / system fonts. No shaping execution.
 */

#include "RoboFont/RoboFont.h"
#include "RoboTest.h"

#include <string.h>

static void put_u16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}

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

static size_t build_sfnt(uint8_t *out,
                         size_t out_cap,
                         uint32_t version,
                         const synth_table_t *tables,
                         uint32_t n)
{
    size_t dir_end = 12u + (size_t)n * 16u;
    size_t need = dir_end;
    size_t cursor;
    uint32_t i;
    uint8_t *p;
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;

    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > out_cap) {
        return 0u;
    }
    memset(out, 0, need);
    while ((uint16_t)(pow2 * 2u) <= (uint16_t)n) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = out;
    put_u32(&p, version);
    put_u16(&p, (uint16_t)n);
    put_u16(&p, (uint16_t)(pow2 * 16u));
    put_u16(&p, entry);
    put_u16(&p, (uint16_t)(n * 16u - pow2 * 16u));
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

static void test_ot_outline_detect(void)
{
    uint8_t maxp[6];
    uint8_t *p = maxp;
    synth_table_t tabs[2];
    uint8_t font[256];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;

    put_u32(&p, 0x00005000u);
    put_u16(&p, 1);
    tabs[0].tag = ROBO_FONT_TAG_MAXP;
    tabs[0].data = maxp;
    tabs[0].length = 6;
    tabs[1].tag = ROBO_FONT_TAG_CFF;
    tabs[1].data = (const uint8_t *)"CFF ";
    tabs[1].length = 4;
    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_OTTO, tabs, 2);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_borrow(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_ot_detect_outline(sfnt), (int)ROBO_FONT_OUTLINE_CFF);
    ROBO_CHECK_EQ_INT((int)robo_font_ot_table_family(ROBO_FONT_TAG_GSUB),
                      (int)ROBO_FONT_OT_FAMILY_LAYOUT);
    ROBO_CHECK_EQ_INT((int)robo_font_ot_table_family(ROBO_FONT_TAG_COLR),
                      (int)ROBO_FONT_OT_FAMILY_COLOR);
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
}

static void test_ot_coverage_classdef(void)
{
    uint8_t cov1[16];
    uint8_t cov2[20];
    uint8_t cls1[16];
    uint8_t cls2[20];
    uint8_t *p;
    robo_font_span_t span;
    robo_font_ot_coverage_t c;
    robo_font_ot_class_def_t d;
    uint16_t idx = 99;
    uint16_t cl = 99;

    p = cov1;
    put_u16(&p, 1);
    put_u16(&p, 3);
    put_u16(&p, 10);
    put_u16(&p, 20);
    put_u16(&p, 30);
    span.data = cov1;
    span.size = (size_t)(p - cov1);
    memset(&c, 0, sizeof(c));
    ROBO_CHECK_EQ_INT(robo_font_ot_coverage_parse(span, 0, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_ot_coverage_index(&c, 20, &idx), ROBO_OK);
    ROBO_CHECK_EQ_INT(idx, 1);
    ROBO_CHECK_EQ_INT(robo_font_ot_coverage_index(&c, 11, &idx), ROBO_NOT_FOUND);
    robo_font_ot_coverage_clear(&c);

    p = cov2;
    put_u16(&p, 2);
    put_u16(&p, 1);
    put_u16(&p, 100);
    put_u16(&p, 102);
    put_u16(&p, 0);
    span.data = cov2;
    span.size = (size_t)(p - cov2);
    ROBO_CHECK_EQ_INT(robo_font_ot_coverage_parse(span, 0, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_ot_coverage_index(&c, 101, &idx), ROBO_OK);
    ROBO_CHECK_EQ_INT(idx, 1);
    robo_font_ot_coverage_clear(&c);

    p = cls1;
    put_u16(&p, 1);
    put_u16(&p, 5);
    put_u16(&p, 2);
    put_u16(&p, 1);
    put_u16(&p, 2);
    span.data = cls1;
    span.size = (size_t)(p - cls1);
    memset(&d, 0, sizeof(d));
    ROBO_CHECK_EQ_INT(robo_font_ot_class_def_parse(span, 0, &d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_ot_class_def_get(&d, 5, &cl), ROBO_OK);
    ROBO_CHECK_EQ_INT(cl, 1);
    ROBO_CHECK_EQ_INT(robo_font_ot_class_def_get(&d, 4, &cl), ROBO_OK);
    ROBO_CHECK_EQ_INT(cl, 0);
    robo_font_ot_class_def_clear(&d);

    p = cls2;
    put_u16(&p, 2);
    put_u16(&p, 1);
    put_u16(&p, 10);
    put_u16(&p, 12);
    put_u16(&p, 3);
    span.data = cls2;
    span.size = (size_t)(p - cls2);
    ROBO_CHECK_EQ_INT(robo_font_ot_class_def_parse(span, 0, &d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_ot_class_def_get(&d, 11, &cl), ROBO_OK);
    ROBO_CHECK_EQ_INT(cl, 3);
    robo_font_ot_class_def_clear(&d);

    /* malformed: unsorted coverage */
    p = cov1;
    put_u16(&p, 1);
    put_u16(&p, 2);
    put_u16(&p, 30);
    put_u16(&p, 10);
    span.data = cov1;
    span.size = 8;
    ROBO_CHECK_EQ_INT(robo_font_ot_coverage_parse(span, 0, &c), ROBO_PARSE_ERROR);
}

static void test_ot_cmap_structural(void)
{
    /* cmap with one format 4 encoding (minimal header only for bounds) */
    uint8_t cmap[64];
    uint8_t *p = cmap;
    uint8_t *sub;
    robo_font_cmap_t c;
    size_t sub_off;

    put_u16(&p, 0); /* version */
    put_u16(&p, 1); /* numTables */
    put_u16(&p, 3); /* platform */
    put_u16(&p, 1); /* encoding */
    sub_off = 12u;
    put_u32(&p, (uint32_t)sub_off);
    sub = cmap + sub_off;
    put_u16(&sub, 4);  /* format */
    put_u16(&sub, 16); /* length — minimal stub (will fail if we required full fmt4) */
    put_u16(&sub, 0);  /* language */
    /* pad rest of claimed length */
    while ((size_t)(sub - cmap) < sub_off + 16u) {
        *sub++ = 0;
    }

    memset(&c, 0, sizeof(c));
    {
        robo_font_span_t span = {cmap, sub_off + 16u};
        ROBO_CHECK_EQ_INT(robo_font_cmap_parse(span, &c), ROBO_OK);
        ROBO_CHECK_EQ_INT(c.num_tables, 1);
        ROBO_CHECK_EQ_INT(c.encodings[0].format, 4);
        ROBO_CHECK(robo_font_cmap_format_is_recognized(4));
        ROBO_CHECK(robo_font_cmap_format_is_recognized(12));
        ROBO_CHECK(!robo_font_cmap_format_is_recognized(99));
        robo_font_cmap_clear(&c);
    }

    /* truncated encoding list */
    {
        robo_font_span_t span = {cmap, 8};
        ROBO_CHECK_EQ_INT(robo_font_cmap_parse(span, &c), ROBO_PARSE_ERROR);
    }
}

static size_t build_min_gsub(uint8_t *buf, size_t cap)
{
    /*
     * Layout:
     * 0: header v1.0
     * ScriptList at 10: count=1, script 'latn' -> Script at S
     * FeatureList at F: count=1, feature 'liga' -> Feature at FE
     * LookupList at L: count=1, Lookup at LU (type 1, flag 0, 1 subtable)
     */
    uint8_t *p = buf;
    size_t script_list = 10;
    size_t feature_list;
    size_t lookup_list;
    size_t script_table;
    size_t feature_table;
    size_t lookup_table;
    size_t subtable;

    if (cap < 128u) {
        return 0u;
    }
    memset(buf, 0, cap);

    /* ScriptList */
    p = buf + script_list;
    put_u16(&p, 1);
    put_u32(&p, ROBO_FONT_TAG('l', 'a', 't', 'n'));
    script_table = (size_t)(p - buf) + 2u; /* after offset field */
    put_u16(&p, (uint16_t)(script_table - script_list));

    /* Script: no default, 0 langsys */
    p = buf + script_table;
    put_u16(&p, 0); /* defaultLangSys */
    put_u16(&p, 0); /* langSysCount */

    feature_list = (size_t)(p - buf);
    /* FeatureList */
    put_u16(&p, 1);
    put_u32(&p, ROBO_FONT_TAG('l', 'i', 'g', 'a'));
    feature_table = (size_t)(p - buf) + 2u;
    put_u16(&p, (uint16_t)(feature_table - feature_list));

    p = buf + feature_table;
    put_u16(&p, 0); /* featureParams */
    put_u16(&p, 1); /* lookupIndexCount */
    put_u16(&p, 0); /* lookup index 0 */

    lookup_list = (size_t)(p - buf);
    put_u16(&p, 1);
    lookup_table = (size_t)(p - buf) + 2u;
    put_u16(&p, (uint16_t)(lookup_table - lookup_list));

    p = buf + lookup_table;
    put_u16(&p, 1); /* type SingleSubst */
    put_u16(&p, 0); /* flags */
    put_u16(&p, 1); /* subtableCount */
    subtable = (size_t)(p - buf) + 2u;
    put_u16(&p, (uint16_t)(subtable - lookup_table));
    /* stub subtable bytes */
    p = buf + subtable;
    put_u16(&p, 1); /* format */
    put_u16(&p, 0); /* coverage offset placeholder */
    put_u16(&p, 0); /* delta */

    /* Header at 0 */
    p = buf;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, (uint16_t)script_list);
    put_u16(&p, (uint16_t)feature_list);
    put_u16(&p, (uint16_t)lookup_list);

    return (size_t)(subtable + 6u);
}

static void test_ot_gsub_gpos_gdef(void)
{
    uint8_t gsub[256];
    uint8_t gdef[32];
    uint8_t *p;
    size_t gn;
    robo_font_ot_layout_t L;
    robo_font_gdef_t g;
    robo_font_span_t span;

    gn = build_min_gsub(gsub, sizeof(gsub));
    ROBO_CHECK(gn > 20u);
    span.data = gsub;
    span.size = gn;
    memset(&L, 0, sizeof(L));
    ROBO_CHECK_EQ_INT(robo_font_gsub_parse(span, &L), ROBO_OK);
    ROBO_CHECK_EQ_INT(L.scripts.script_count, 1);
    ROBO_CHECK_EQ_INT((int)L.scripts.scripts[0].script_tag,
                      (int)ROBO_FONT_TAG('l', 'a', 't', 'n'));
    ROBO_CHECK_EQ_INT(L.features.feature_count, 1);
    ROBO_CHECK_EQ_INT(L.lookups.lookup_count, 1);
    ROBO_CHECK_EQ_INT(L.lookups.lookups[0].lookup_type, 1);
    ROBO_CHECK(robo_font_gsub_lookup_type_is_known(4));
    ROBO_CHECK(robo_font_gpos_lookup_type_is_known(2));
    robo_font_ot_layout_clear(&L);

    /* Same bytes as GPOS should also parse (shared header). */
    ROBO_CHECK_EQ_INT(robo_font_gpos_parse(span, &L), ROBO_OK);
    /* But lookup type 1 is valid for GPOS too (single adjustment). */
    robo_font_ot_layout_clear(&L);

    /* GDEF with glyph class def format 2 empty */
    p = gdef;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 12); /* glyphClassDef offset */
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 2); /* ClassDef format 2 */
    put_u16(&p, 0); /* range count 0 */
    span.data = gdef;
    span.size = (size_t)(p - gdef);
    memset(&g, 0, sizeof(g));
    ROBO_CHECK_EQ_INT(robo_font_gdef_parse(span, &g), ROBO_OK);
    ROBO_CHECK(g.has_glyph_class_def);
    robo_font_gdef_clear(&g);

    /* truncated GSUB */
    span.size = 8;
    ROBO_CHECK_EQ_INT(robo_font_gsub_parse(span, &L), ROBO_PARSE_ERROR);
}

static void test_ot_extra_tables(void)
{
    uint8_t base[16];
    uint8_t math[16];
    uint8_t cpal[32];
    uint8_t colr[32];
    uint8_t stat[48];
    uint8_t kern[32];
    uint8_t *p;
    robo_font_span_t span;
    robo_font_base_t b;
    robo_font_math_t m;
    robo_font_cpal_t cp;
    robo_font_colr_t co;
    robo_font_stat_t st;
    robo_font_kern_t k;

    p = base;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 8); /* horiz Axis */
    put_u16(&p, 0);
    put_u16(&p, 0); /* BaseTagList offset stub */
    put_u16(&p, 0); /* BaseScriptList */
    span.data = base;
    span.size = 12;
    ROBO_CHECK_EQ_INT(robo_font_base_parse(span, &b), ROBO_OK);
    ROBO_CHECK(b.has_horiz);

    p = math;
    put_u32(&p, 0x00010000u);
    put_u16(&p, 10);
    put_u16(&p, 10);
    put_u16(&p, 10);
    span.data = math;
    span.size = 10;
    ROBO_CHECK_EQ_INT(robo_font_math_parse(span, &m), ROBO_OK);

    p = cpal;
    put_u16(&p, 0);
    put_u16(&p, 1); /* entries per palette */
    put_u16(&p, 1); /* num palettes */
    put_u16(&p, 1); /* num color records */
    put_u32(&p, 14); /* color records offset */
    put_u16(&p, 0);  /* palette 0 first color index */
    put_u32(&p, 0xFF0000FFu); /* BGRA */
    span.data = cpal;
    span.size = 18;
    ROBO_CHECK_EQ_INT(robo_font_cpal_parse(span, &cp), ROBO_OK);
    ROBO_CHECK_EQ_INT(cp.num_palette, 1);

    p = colr;
    put_u16(&p, 0);
    put_u16(&p, 1);
    put_u32(&p, 14);
    put_u32(&p, 20);
    put_u16(&p, 1);
    /* base glyph record */
    put_u16(&p, 5);
    put_u16(&p, 0);
    put_u16(&p, 1);
    /* layer */
    put_u16(&p, 6);
    put_u16(&p, 0);
    span.data = colr;
    span.size = 24;
    ROBO_CHECK_EQ_INT(robo_font_colr_parse(span, &co), ROBO_OK);

    p = stat;
    put_u16(&p, 1);
    put_u16(&p, 1);
    put_u16(&p, 8); /* axis size */
    put_u16(&p, 1);
    put_u32(&p, 20); /* axes offset */
    put_u16(&p, 0);
    put_u32(&p, 0);
    put_u16(&p, 1); /* elidedFallbackNameID */
    /* axis record at 20 */
    while ((size_t)(p - stat) < 20u) {
        *p++ = 0;
    }
    p = stat + 20;
    put_u32(&p, ROBO_FONT_TAG('w', 'g', 'h', 't'));
    put_u16(&p, 256);
    put_u16(&p, 0);
    span.data = stat;
    span.size = 28;
    memset(&st, 0, sizeof(st));
    ROBO_CHECK_EQ_INT(robo_font_stat_parse(span, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.design_axis_count, 1);
    ROBO_CHECK_EQ_INT((int)st.axes[0].tag, (int)ROBO_FONT_TAG('w', 'g', 'h', 't'));
    robo_font_stat_clear(&st);

    p = kern;
    put_u16(&p, 0);
    put_u16(&p, 1);
    put_u16(&p, 0);  /* subtable version */
    put_u16(&p, 8);  /* length */
    put_u16(&p, 1);  /* coverage */
    put_u16(&p, 0);  /* pad to length 8 from subtable start: version+len+cov = 6, need 2 more */
    span.data = kern;
    span.size = 12;
    memset(&k, 0, sizeof(k));
    ROBO_CHECK_EQ_INT(robo_font_kern_parse(span, &k), ROBO_OK);
    ROBO_CHECK_EQ_INT(k.n_tables, 1);
    robo_font_kern_clear(&k);
}

static void test_ot_integrated_bundle(void)
{
    uint8_t gsub[256];
    uint8_t cmap[64];
    uint8_t *cp;
    size_t gn;
    size_t cmap_n;
    synth_table_t tabs[2];
    uint8_t font[1024];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_ot_t *ot = NULL;

    gn = build_min_gsub(gsub, sizeof(gsub));
    cp = cmap;
    put_u16(&cp, 0);
    put_u16(&cp, 1);
    put_u16(&cp, 0);
    put_u16(&cp, 0);
    put_u32(&cp, 12);
    put_u16(&cp, 0); /* format 0 */
    put_u16(&cp, 262); /* length for format 0 is typically 262 */
    put_u16(&cp, 0);
    /* Don't claim full 262 — use truncated carefully: length must fit */
    {
        /* Use format 4 minimal instead */
        uint8_t *q = cmap;
        put_u16(&q, 0);
        put_u16(&q, 1);
        put_u16(&q, 3);
        put_u16(&q, 1);
        put_u32(&q, 12);
        put_u16(&q, 4);
        put_u16(&q, 16);
        put_u16(&q, 0);
        memset(q, 0, 10);
        cmap_n = 28;
    }

    tabs[0].tag = ROBO_FONT_TAG_GSUB;
    tabs[0].data = gsub;
    tabs[0].length = (uint32_t)gn;
    tabs[1].tag = ROBO_FONT_TAG_CMAP;
    tabs[1].data = cmap;
    tabs[1].length = (uint32_t)cmap_n;
    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_OTTO, tabs, 2);

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_ot_parse(sfnt, ROBO_FONT_OT_FLAGS_PARSE_ALL, &ot), ROBO_OK);
    ROBO_CHECK(robo_font_ot_gsub(ot) != NULL);
    ROBO_CHECK(robo_font_ot_cmap(ot) != NULL);
    ROBO_CHECK_EQ_INT(robo_font_ot_cmap(ot)->encodings[0].format, 4);
    ROBO_CHECK_EQ_INT(robo_font_ot_fuzz_parse(font, n), ROBO_OK);

    robo_font_ot_release(ot);
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
}

static void test_ot_malformed(void)
{
    uint8_t bad[8] = {0};
    robo_font_span_t span = {bad, 8};
    robo_font_ot_layout_t L;
    robo_font_ot_coverage_t c;
    memset(&L, 0, sizeof(L));
    memset(&c, 0, sizeof(c));
    ROBO_CHECK_EQ_INT(robo_font_gsub_parse(span, &L), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_font_ot_coverage_parse(span, 0, &c), ROBO_PARSE_ERROR);
    ROBO_CHECK(robo_font_ot_fuzz_parse((const uint8_t *)"xxxx", 4) != ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_ot_resolve_offset16(span, 0, 100, NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        size_t abs = 0;
        ROBO_CHECK_EQ_INT(robo_font_ot_resolve_offset16(span, 0, 100, &abs),
                          ROBO_PARSE_ERROR);
    }
}

void robo_test_register_font_ot(void)
{
    robo_test_register("font.ot.outline", test_ot_outline_detect);
    robo_test_register("font.ot.coverage_class", test_ot_coverage_classdef);
    robo_test_register("font.ot.cmap", test_ot_cmap_structural);
    robo_test_register("font.ot.layout", test_ot_gsub_gpos_gdef);
    robo_test_register("font.ot.extra", test_ot_extra_tables);
    robo_test_register("font.ot.bundle", test_ot_integrated_bundle);
    robo_test_register("font.ot.malformed", test_ot_malformed);
}
