/*
 * RoboFont selection suite (Goal 5.11).
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <string.h>

#ifndef ROBO_FONT_NAME_ID_TYPO_FAMILY
#define ROBO_FONT_NAME_ID_TYPO_FAMILY 16u
#endif

static void put_u8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
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
static void put_i16(uint8_t **p, int16_t v) { put_u16(p, (uint16_t)v); }
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
    put_u32(&p, 0x00010000u);
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

static size_t build_hhea(uint8_t *b)
{
    uint8_t *p = b;
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
    put_u16(&p, 1);
    return (size_t)(p - b);
}

static size_t build_maxp(uint8_t *b)
{
    uint8_t *p = b;
    int i;
    put_u32(&p, 0x00010000u);
    put_u16(&p, 1);
    for (i = 0; i < 13; i++) {
        put_u16(&p, 0);
    }
    return (size_t)(p - b);
}

static size_t build_hmtx(uint8_t *b)
{
    uint8_t *p = b;
    put_u16(&p, 500);
    put_i16(&p, 0);
    return (size_t)(p - b);
}

static size_t build_loca(uint8_t *b)
{
    uint8_t *p = b;
    put_u16(&p, 0);
    put_u16(&p, 0);
    return (size_t)(p - b);
}

static size_t build_post(uint8_t *b, int32_t angle)
{
    uint8_t *p = b;
    put_u32(&p, 0x00030000u);
    put_u32(&p, (uint32_t)angle);
    put_i16(&p, -100);
    put_i16(&p, 50);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    return (size_t)(p - b);
}

static size_t build_os2(uint8_t *buf, uint16_t weight, uint16_t width, uint16_t fs)
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
    put_u16(&p, fs);
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

/**
 * name table with Windows Unicode records.
 * name_ids[] / strings[] parallel arrays; count records.
 */
static size_t build_name_table(uint8_t *buf, size_t cap, const uint16_t *name_ids,
                               const char *const *strings, uint32_t count)
{
    uint8_t *p = buf;
    uint32_t i;
    size_t storage;
    size_t header = 6u + (size_t)count * 12u;

    if (cap < header) {
        return 0u;
    }
    put_u16(&p, 0); /* format */
    put_u16(&p, (uint16_t)count);
    put_u16(&p, (uint16_t)header);
    storage = header;
    for (i = 0u; i < count; i++) {
        size_t slen = strlen(strings[i]) * 2u;
        uint8_t *rec = buf + 6u + (size_t)i * 12u;
        uint8_t *rp = rec;
        if (storage + slen > cap) {
            return 0u;
        }
        put_u16(&rp, 3); /* Windows */
        put_u16(&rp, 1); /* Unicode BMP */
        put_u16(&rp, 0x0409);
        put_u16(&rp, name_ids[i]);
        put_u16(&rp, (uint16_t)slen);
        put_u16(&rp, (uint16_t)(storage - header));
        {
            uint8_t *sp = buf + storage;
            put_utf16be_ascii(&sp, strings[i]);
        }
        storage += slen;
    }
    return storage;
}

#define SEL_FACE_FONT_CAP 2048u

typedef struct {
    uint8_t *font;
    size_t font_n;
    robo_font_source_t *src;
    robo_font_face_t *face;
} sel_face_t;

static void sel_face_release(sel_face_t *f)
{
    if (!f) {
        return;
    }
    if (f->face) {
        robo_font_face_release(f->face);
        f->face = NULL;
    }
    if (f->src) {
        robo_font_source_release(f->src);
        f->src = NULL;
    }
    if (f->font) {
        robo_free(f->font);
        f->font = NULL;
    }
    f->font_n = 0u;
}

static int sel_face_build(sel_face_t *f, const char *legacy_family, const char *typo_opt,
                          const char *wws_opt, uint16_t weight, uint16_t width,
                          uint16_t fs, int32_t post_angle)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[8], loca[8], post[64], os2[128], name[512];
    uint16_t ids[8];
    const char *strs[8];
    uint32_t nc = 0u;
    size_t nn;
    /* head/hhea/maxp/hmtx/loca/glyf/post/os2/name — nine tables */
    synth_table_t tabs[9];
    uint32_t nt = 0u;

    memset(f, 0, sizeof(*f));
    f->font = (uint8_t *)robo_calloc(SEL_FACE_FONT_CAP, 1u);
    if (!f->font) {
        return 0;
    }

    ids[nc] = ROBO_FONT_NAME_ID_FAMILY;
    strs[nc++] = legacy_family;
    ids[nc] = ROBO_FONT_NAME_ID_SUBFAMILY;
    strs[nc++] = "Regular";
    ids[nc] = ROBO_FONT_NAME_ID_FULL;
    strs[nc++] = legacy_family;
    ids[nc] = ROBO_FONT_NAME_ID_POSTSCRIPT;
    strs[nc++] = legacy_family;
    if (typo_opt) {
        ids[nc] = ROBO_FONT_NAME_ID_TYPO_FAMILY;
        strs[nc++] = typo_opt;
    }
    if (wws_opt) {
        ids[nc] = ROBO_FONT_NAME_ID_WWS_FAMILY;
        strs[nc++] = wws_opt;
    }
    nn = build_name_table(name, sizeof(name), ids, strs, nc);
    if (nn == 0u) {
        sel_face_release(f);
        return 0;
    }

    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HEAD, head, (uint32_t)build_head(head)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HHEA, hhea, (uint32_t)build_hhea(hhea)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_MAXP, maxp, (uint32_t)build_maxp(maxp)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HMTX, hmtx, (uint32_t)build_hmtx(hmtx)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_LOCA, loca, (uint32_t)build_loca(loca)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_GLYF, head, 0u};
    tabs[nt++] =
        (synth_table_t){ROBO_FONT_TAG_POST, post, (uint32_t)build_post(post, post_angle)};
    tabs[nt++] =
        (synth_table_t){ROBO_FONT_TAG_OS2, os2, (uint32_t)build_os2(os2, weight, width, fs)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_NAME, name, (uint32_t)nn};

    f->font_n = build_sfnt(f->font, SEL_FACE_FONT_CAP, tabs, nt);
    if (f->font_n == 0u) {
        sel_face_release(f);
        return 0;
    }
    if (robo_font_source_create_memory_copy(f->font, f->font_n, &f->src) != ROBO_OK) {
        sel_face_release(f);
        return 0;
    }
    if (robo_font_face_create(f->src, 0u, &f->face) != ROBO_OK) {
        sel_face_release(f);
        return 0;
    }
    return 1;
}

static void test_distance_helpers(void)
{
    ROBO_CHECK_EQ_INT(robo_font_select_stretch_dist(100, 100), 0);
    ROBO_CHECK(robo_font_select_stretch_dist(75, 50) <
               robo_font_select_stretch_dist(75, 100));
    ROBO_CHECK_EQ_INT(robo_font_select_weight_dist(400, 400), 0);
    ROBO_CHECK(robo_font_select_weight_dist(300, 200) <
               robo_font_select_weight_dist(300, 400));
    ROBO_CHECK_EQ_INT(
        robo_font_select_style_dist(ROBO_FONT_STYLE_ITALIC, ROBO_FONT_STYLE_ITALIC, 0, 0,
                                    0, 0),
        0);
    ROBO_CHECK(robo_font_select_style_dist(ROBO_FONT_STYLE_ITALIC, ROBO_FONT_STYLE_OBLIQUE,
                                           0, 0, 0, 0) <
               robo_font_select_style_dist(ROBO_FONT_STYLE_ITALIC, ROBO_FONT_STYLE_NORMAL,
                                           0, 0, 0, 0));
}

static void test_family_and_weight_select(void)
{
    sel_face_t a, b, c;
    robo_font_face_t *cands[3];
    robo_font_select_request_t req;
    robo_font_selection_t sel;

    ROBO_CHECK(sel_face_build(&a, "Probe", NULL, NULL, 400, 5, 0, 0));
    ROBO_CHECK(sel_face_build(&b, "Probe", NULL, NULL, 700, 5, ROBO_FONT_FS_BOLD, 0));
    ROBO_CHECK(sel_face_build(&c, "Other", NULL, NULL, 400, 5, 0, 0));

    cands[0] = a.face;
    cands[1] = b.face;
    cands[2] = c.face;

    robo_font_select_request_init(&req);
    req.family = "probe"; /* casefold */
    req.weight = 700;
    req.stretch = 100;
    req.style = ROBO_FONT_STYLE_NORMAL;

    ROBO_CHECK_EQ_INT(robo_font_select(&req, cands, 3u, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK(sel.face == b.face);
    ROBO_CHECK_EQ_INT(sel.selected.weight, 700);
    robo_font_selection_clear(&sel);

    req.family = "Missing";
    ROBO_CHECK_EQ_INT(robo_font_select(&req, cands, 3u, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 0);
    robo_font_selection_clear(&sel);

    sel_face_release(&a);
    sel_face_release(&b);
    sel_face_release(&c);
}

static void test_typo_vs_legacy_and_wws(void)
{
    sel_face_t typo, wws;
    robo_font_face_t *cands[2];
    robo_font_select_request_t req;
    robo_font_selection_t sel;

    ROBO_CHECK(sel_face_build(&typo, "LegacyFam", "TypoFam", NULL, 400, 5, 0, 0));
    ROBO_CHECK(sel_face_build(&wws, "LegacyFam2", NULL, "WwsFam", 400, 5,
                              ROBO_FONT_FS_WWS, 0));

    cands[0] = typo.face;
    cands[1] = wws.face;

    robo_font_select_request_init(&req);
    req.family = "TypoFam";
    ROBO_CHECK_EQ_INT(robo_font_select(&req, cands, 2u, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK(sel.face == typo.face);
    ROBO_CHECK_EQ_INT(sel.family_kind, ROBO_FONT_SELECT_FAMILY_TYPOGRAPHIC);
    robo_font_selection_clear(&sel);

    req.family = "WwsFam";
    req.flags |= ROBO_FONT_SELECT_FLAG_PREFER_WWS;
    ROBO_CHECK_EQ_INT(robo_font_select(&req, cands, 2u, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK(sel.face == wws.face);
    ROBO_CHECK_EQ_INT(sel.family_kind, ROBO_FONT_SELECT_FAMILY_WWS);
    robo_font_selection_clear(&sel);

    sel_face_release(&typo);
    sel_face_release(&wws);
}

static void test_style_italic_oblique(void)
{
    sel_face_t ital, obl, norm;
    robo_font_face_t *cands[3];
    robo_font_select_request_t req;
    robo_font_selection_t sel;

    ROBO_CHECK(sel_face_build(&ital, "Slope", NULL, NULL, 400, 5, ROBO_FONT_FS_ITALIC, 0));
    ROBO_CHECK(sel_face_build(&obl, "Slope", NULL, NULL, 400, 5, ROBO_FONT_FS_OBLIQUE,
                              -786432));
    ROBO_CHECK(sel_face_build(&norm, "Slope", NULL, NULL, 400, 5, 0, 0));

    cands[0] = norm.face;
    cands[1] = obl.face;
    cands[2] = ital.face;

    robo_font_select_request_init(&req);
    req.family = "Slope";
    req.style = ROBO_FONT_STYLE_ITALIC;
    ROBO_CHECK_EQ_INT(robo_font_select(&req, cands, 3u, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK(sel.face == ital.face);
    robo_font_selection_clear(&sel);

    req.style = ROBO_FONT_STYLE_OBLIQUE;
    ROBO_CHECK_EQ_INT(robo_font_select(&req, cands, 3u, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK(sel.face == obl.face);
    robo_font_selection_clear(&sel);

    sel_face_release(&ital);
    sel_face_release(&obl);
    sel_face_release(&norm);
}

static void test_stretch_nearest(void)
{
    sel_face_t condensed, normal, expanded;
    robo_font_face_t *cands[3];
    robo_font_select_request_t req;
    robo_font_selection_t sel;

    ROBO_CHECK(sel_face_build(&condensed, "Wide", NULL, NULL, 400, 3, 0, 0));
    ROBO_CHECK(sel_face_build(&normal, "Wide", NULL, NULL, 400, 5, 0, 0));
    ROBO_CHECK(sel_face_build(&expanded, "Wide", NULL, NULL, 400, 7, 0, 0));

    cands[0] = expanded.face;
    cands[1] = normal.face;
    cands[2] = condensed.face;

    robo_font_select_request_init(&req);
    req.family = "Wide";
    req.stretch = ROBO_FONT_STRETCH_CONDENSED; /* 75 */
    ROBO_CHECK_EQ_INT(robo_font_select(&req, cands, 3u, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK(sel.face == condensed.face);
    robo_font_selection_clear(&sel);

    sel_face_release(&condensed);
    sel_face_release(&normal);
    sel_face_release(&expanded);
}

static void test_determinism_order(void)
{
    sel_face_t a, b;
    robo_font_face_t *order1[2];
    robo_font_face_t *order2[2];
    robo_font_select_request_t req;
    robo_font_selection_t s1, s2;

    ROBO_CHECK(sel_face_build(&a, "Same", NULL, NULL, 500, 5, 0, 0));
    ROBO_CHECK(sel_face_build(&b, "Same", NULL, NULL, 500, 5, 0, 0));

    order1[0] = a.face;
    order1[1] = b.face;
    order2[0] = b.face;
    order2[1] = a.face;

    robo_font_select_request_init(&req);
    req.family = "Same";
    req.weight = 500;

    ROBO_CHECK_EQ_INT(robo_font_select(&req, order1, 2u, &s1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_select(&req, order2, 2u, &s2), ROBO_OK);
    ROBO_CHECK_EQ_INT(s1.found, 1);
    ROBO_CHECK_EQ_INT(s2.found, 1);
    ROBO_CHECK_EQ_INT((int)(s1.face_id == s2.face_id), 1);
    /* Lower face_id wins on perfect tie. */
    ROBO_CHECK(s1.face_id == robo_font_face_id(a.face) ||
               s1.face_id == robo_font_face_id(b.face));
    if (robo_font_face_id(a.face) < robo_font_face_id(b.face)) {
        ROBO_CHECK(s1.face == a.face);
    } else {
        ROBO_CHECK(s1.face == b.face);
    }

    robo_font_selection_clear(&s1);
    robo_font_selection_clear(&s2);
    sel_face_release(&a);
    sel_face_release(&b);
}

static void test_alias_and_empty(void)
{
    sel_face_t f;
    robo_font_face_t *cands[1];
    robo_font_select_request_t req;
    robo_font_selection_t sel;
    const char *aliases[] = {"AliasName"};

    ROBO_CHECK(sel_face_build(&f, "AliasName", NULL, NULL, 400, 5, 0, 0));
    cands[0] = f.face;

    robo_font_select_request_init(&req);
    req.family = "PrimaryMissing";
    req.aliases = aliases;
    req.alias_count = 1u;
    ROBO_CHECK_EQ_INT(robo_font_select(&req, cands, 1u, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    robo_font_selection_clear(&sel);

    ROBO_CHECK_EQ_INT(robo_font_select(&req, NULL, 0u, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 0);
    robo_font_selection_clear(&sel);

    req.family = NULL;
    ROBO_CHECK_EQ_INT(robo_font_select_request_validate(&req), ROBO_INVALID_ARGUMENT);

    sel_face_release(&f);
}

static void test_fuzz_selection(void)
{
    uint8_t junk[128];
    size_t i;
    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 41u + 7u);
    }
    ROBO_CHECK_EQ_INT(robo_font_selection_fuzz_parse(junk, sizeof(junk)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_selection_fuzz_parse(junk, 0u), ROBO_OK);
}

void robo_test_register_font_selection(void)
{
    robo_test_register("font.select.distances", test_distance_helpers);
    robo_test_register("font.select.family_weight", test_family_and_weight_select);
    robo_test_register("font.select.typo_wws", test_typo_vs_legacy_and_wws);
    robo_test_register("font.select.style", test_style_italic_oblique);
    robo_test_register("font.select.stretch", test_stretch_nearest);
    robo_test_register("font.select.determinism", test_determinism_order);
    robo_test_register("font.select.alias_empty", test_alias_and_empty);
    robo_test_register("font.select.fuzz", test_fuzz_selection);
}
