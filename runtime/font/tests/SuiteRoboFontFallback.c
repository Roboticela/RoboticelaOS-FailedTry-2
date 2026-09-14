/*
 * RoboFont fallback suite (Goal 5.15).
 * Synthetic registry fixtures — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
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
        if (tables[i].length) {
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
    put_i16(&p, 0); /* indexToLocFormat short */
    put_i16(&p, 0);
    return (size_t)(p - buf);
}

static size_t build_hhea(uint8_t *buf)
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
    put_u16(&p, 2); /* numberOfHMetrics — two glyphs */
    return (size_t)(p - buf);
}

static size_t build_maxp(uint8_t *buf)
{
    uint8_t *p = buf;
    int i;
    put_u32(&p, 0x00010000u);
    put_u16(&p, 2); /* .notdef + one mapped glyph */
    for (i = 0; i < 13; i++) {
        put_u16(&p, 0);
    }
    return (size_t)(p - buf);
}

static size_t build_hmtx(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 500);
    put_i16(&p, 0);
    put_u16(&p, 500);
    put_i16(&p, 0);
    return (size_t)(p - buf);
}

static size_t build_loca(uint8_t *buf)
{
    uint8_t *p = buf;
    /* short format: numGlyphs+1 offsets */
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
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

static size_t build_os2(uint8_t *buf, uint16_t weight)
{
    uint8_t *p = buf;
    int i;
    put_u16(&p, 4);
    put_i16(&p, 500);
    put_u16(&p, weight);
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

/* Format-4 single BMP mapping. */
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

static size_t build_font_with_cp(uint8_t *out, size_t cap, const char *family,
                                 const char *ps, uint16_t weight, uint16_t cp,
                                 uint16_t gid)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], loca[16], post[64], os2[128];
    uint8_t name[512], cmap[128];
    synth_table_t tabs[10];
    uint32_t nt = 0u;
    size_t nn = build_name_table(name, sizeof(name), family, ps);
    size_t nc;
    if (nn == 0u) {
        return 0u;
    }
    if (gid == 0u || gid > 1u) {
        /* Fixture only models .notdef + glyph 1. */
        gid = 1u;
    }
    nc = build_cmap_fmt4(cmap, cp, gid);
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HEAD, head, (uint32_t)build_head(head)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HHEA, hhea, (uint32_t)build_hhea(hhea)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_MAXP, maxp, (uint32_t)build_maxp(maxp)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HMTX, hmtx, (uint32_t)build_hmtx(hmtx)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_LOCA, loca, (uint32_t)build_loca(loca)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_GLYF, head, 0u};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_POST, post, (uint32_t)build_post(post)};
    tabs[nt++] =
        (synth_table_t){ROBO_FONT_TAG_OS2, os2, (uint32_t)build_os2(os2, weight)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_NAME, name, (uint32_t)nn};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_CMAP, cmap, (uint32_t)nc};
    return build_sfnt(out, cap, tabs, nt);
}

static robo_font_face_t *face_at(robo_font_registry_t *reg, uint32_t index)
{
    const robo_font_registration_t *rec = NULL;
    if (robo_font_registry_registration_at(reg, index, &rec) != ROBO_OK || !rec) {
        return NULL;
    }
    return rec->face;
}

static void test_primary_covers_no_fallback(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t sid = 0u;
    robo_font_discovery_result_t dr;
    robo_font_fallback_resolver_t *fb = NULL;
    robo_font_fallback_request_t req;
    robo_font_fallback_result_t res;
    uint32_t cp = 0x41u;

    n = build_font_with_cp(font, sizeof(font), "LatinFace", "LatinFace", 400, 0x41u, 1u);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "mem:latin", font, n, &sid), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, sid, 0u, &dr), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolver_create(reg, NULL, &fb), ROBO_OK);

    robo_font_fallback_request_init(&req);
    req.primary = face_at(reg, 0u);
    req.codepoints = &cp;
    req.codepoint_count = 1u;
    robo_font_fallback_result_init(&res);
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolve(fb, &req, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.found, 1);
    ROBO_CHECK_EQ_INT(res.used_primary, 1);
    ROBO_CHECK_EQ_INT(res.stage, ROBO_FONT_FALLBACK_STAGE_PRIMARY);
    ROBO_CHECK(res.face == req.primary);
    robo_font_fallback_result_clear(&res);
    robo_font_fallback_resolver_destroy(fb);
    robo_font_registry_destroy(reg);
}

static void test_script_fallback_arabic(void)
{
    uint8_t latin[4096], arab[4096];
    size_t nl, na;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t s1 = 0u, s2 = 0u;
    robo_font_discovery_result_t dr;
    robo_font_fallback_resolver_t *fb = NULL;
    robo_font_fallback_request_t req;
    robo_font_fallback_result_t res;
    uint32_t cp = 0x0627u; /* Arabic Alef */
    robo_font_face_t *primary;
    robo_font_face_t *arabic_face;

    nl = build_font_with_cp(latin, sizeof(latin), "OnlyLatin", "OnlyLatin", 400, 0x41u, 1u);
    na = build_font_with_cp(arab, sizeof(arab), "ArabScript", "ArabScript", 400, 0x0627u,
                            1u);
    ROBO_CHECK(nl > 0u && na > 0u);

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "mem:latin", latin, nl, &s1), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "mem:arab", arab, na, &s2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_all(reg, 0u, &dr), ROBO_OK);
    ROBO_CHECK((int)robo_font_registry_registration_count(reg) >= 2);

    primary = face_at(reg, 0u);
    arabic_face = face_at(reg, 1u);
    /* Order may vary — pick by family. */
    {
        uint32_t i;
        for (i = 0u; i < robo_font_registry_registration_count(reg); i++) {
            const robo_font_registration_t *rec = NULL;
            ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, i, &rec), ROBO_OK);
            if (rec && strcmp(rec->family, "OnlyLatin") == 0) {
                primary = rec->face;
            }
            if (rec && strcmp(rec->family, "ArabScript") == 0) {
                arabic_face = rec->face;
            }
        }
    }
    ROBO_CHECK(primary && arabic_face && primary != arabic_face);

    ROBO_CHECK_EQ_INT(robo_font_fallback_resolver_create(reg, NULL, &fb), ROBO_OK);
    robo_font_fallback_request_init(&req);
    req.primary = primary;
    req.codepoints = &cp;
    req.codepoint_count = 1u;
    req.has_script = 1;
    req.script = ROBO_U_SCRIPT_ARABIC;
    robo_font_fallback_result_init(&res);
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolve(fb, &req, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.found, 1);
    ROBO_CHECK_EQ_INT(res.used_primary, 0);
    ROBO_CHECK(res.face == arabic_face);
    ROBO_CHECK(res.stage == ROBO_FONT_FALLBACK_STAGE_SCRIPT ||
               res.stage == ROBO_FONT_FALLBACK_STAGE_GENERAL);
    robo_font_fallback_result_clear(&res);
    robo_font_fallback_resolver_destroy(fb);
    robo_font_registry_destroy(reg);
}

static void test_preferred_and_style_and_none(void)
{
    uint8_t a[4096], b[4096], c[4096];
    size_t na, nb, nc;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t s1 = 0u, s2 = 0u, s3 = 0u;
    robo_font_discovery_result_t dr;
    robo_font_fallback_policy_t pol;
    robo_font_fallback_resolver_t *fb = NULL;
    robo_font_fallback_request_t req;
    robo_font_fallback_result_t res;
    uint32_t cp = 0x0410u; /* Cyrillic A */
    const char *prefs[] = {"CyrilPref"};
    robo_font_face_t *primary = NULL;
    robo_font_face_t *pref = NULL;
    uint32_t i;

    na = build_font_with_cp(a, sizeof(a), "PrimaryLat", "PrimaryLat", 600, 0x41u, 1u);
    nb = build_font_with_cp(b, sizeof(b), "CyrilPref", "CyrilPref", 600, 0x0410u, 1u);
    nc = build_font_with_cp(c, sizeof(c), "CyrilOther", "CyrilOther", 400, 0x0410u, 1u);
    ROBO_CHECK(na && nb && nc);

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "m1", a, na, &s1),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "m2", b, nb, &s2),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "m3", c, nc, &s3),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_all(reg, 0u, &dr), ROBO_OK);

    for (i = 0u; i < robo_font_registry_registration_count(reg); i++) {
        const robo_font_registration_t *rec = NULL;
        ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, i, &rec), ROBO_OK);
        if (!strcmp(rec->family, "PrimaryLat")) {
            primary = rec->face;
        }
        if (!strcmp(rec->family, "CyrilPref")) {
            pref = rec->face;
        }
    }
    ROBO_CHECK(primary && pref);

    robo_font_fallback_policy_init(&pol);
    pol.preferred_families = prefs;
    pol.preferred_family_count = 1u;
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolver_create(reg, &pol, &fb), ROBO_OK);

    robo_font_fallback_request_init(&req);
    req.primary = primary;
    req.codepoints = &cp;
    req.codepoint_count = 1u;
    req.weight = 600;
    robo_font_fallback_result_init(&res);
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolve(fb, &req, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.found, 1);
    ROBO_CHECK(res.face == pref);
    ROBO_CHECK_EQ_INT(res.stage, ROBO_FONT_FALLBACK_STAGE_PREFERRED);
    robo_font_fallback_result_clear(&res);

    /* No fallback for uncovered scalar. */
    {
        uint32_t missing = 0x0E01u; /* Thai — none of our fonts */
        req.codepoints = &missing;
        ROBO_CHECK_EQ_INT(robo_font_fallback_resolve(fb, &req, &res), ROBO_OK);
        ROBO_CHECK_EQ_INT(res.found, 0);
        ROBO_CHECK_EQ_INT(res.stage, ROBO_FONT_FALLBACK_STAGE_NONE);
        ROBO_CHECK(res.face == NULL);
        robo_font_fallback_result_clear(&res);
    }

    robo_font_fallback_resolver_destroy(fb);
    robo_font_registry_destroy(reg);
}

static void test_determinism_cache_invalidate(void)
{
    uint8_t a[4096], b[4096];
    size_t na, nb;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t s1 = 0u, s2 = 0u;
    robo_font_discovery_result_t dr;
    robo_font_fallback_resolver_t *fb = NULL;
    robo_font_fallback_request_t req;
    robo_font_fallback_result_t r1, r2;
    uint32_t cp = 0x0391u; /* Greek */
    robo_font_face_t *primary = NULL;
    robo_font_face_id_t win1, win2;
    robo_font_fallback_cache_stats_t st;
    uint32_t i;

    na = build_font_with_cp(a, sizeof(a), "P", "P", 400, 0x41u, 1u);
    nb = build_font_with_cp(b, sizeof(b), "G", "G", 400, 0x0391u, 1u);
    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    /* Add in opposite order across two runs conceptually — same registry both. */
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "b", b, nb, &s2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "a", a, na, &s1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_all(reg, 0u, &dr), ROBO_OK);

    for (i = 0u; i < robo_font_registry_registration_count(reg); i++) {
        const robo_font_registration_t *rec = NULL;
        ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, i, &rec), ROBO_OK);
        if (!strcmp(rec->family, "P")) {
            primary = rec->face;
        }
    }
    ROBO_CHECK(primary);

    ROBO_CHECK_EQ_INT(robo_font_fallback_resolver_create(reg, NULL, &fb), ROBO_OK);
    robo_font_fallback_request_init(&req);
    req.primary = primary;
    req.codepoints = &cp;
    req.codepoint_count = 1u;

    robo_font_fallback_result_init(&r1);
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolve(fb, &req, &r1), ROBO_OK);
    ROBO_CHECK_EQ_INT(r1.found, 1);
    win1 = r1.face_id;
    robo_font_fallback_result_clear(&r1);

    robo_font_fallback_result_init(&r2);
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolve(fb, &req, &r2), ROBO_OK);
    ROBO_CHECK_EQ_INT(r2.from_cache, 1);
    win2 = r2.face_id;
    ROBO_CHECK_EQ_INT((int)win1, (int)win2);
    robo_font_fallback_result_clear(&r2);

    ROBO_CHECK_EQ_INT(robo_font_registry_remove_discovery_source(reg, s2), ROBO_OK);
    /* Generation bumped → cache miss / recompute → no fallback. */
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolve(fb, &req, &r1), ROBO_OK);
    ROBO_CHECK_EQ_INT(r1.found, 0);
    robo_font_fallback_result_clear(&r1);

    robo_font_fallback_resolver_cache_stats(fb, &st);
    ROBO_CHECK(st.hits >= 1u);

    /* Bench: primary-hit path. */
    {
        uint32_t ascii = 0x41u;
        volatile int hits = 0;
        req.codepoints = &ascii;
        for (i = 0u; i < 5000u; i++) {
            robo_font_fallback_result_init(&r1);
            ROBO_CHECK_EQ_INT(robo_font_fallback_resolve(fb, &req, &r1), ROBO_OK);
            hits += r1.used_primary;
            robo_font_fallback_result_clear(&r1);
        }
        ROBO_CHECK(hits == 5000);
        printf("font.fallback.bench.primary_hits=%d\n", hits);
    }

    robo_font_fallback_resolver_destroy(fb);
    robo_font_registry_destroy(reg);
}

static void test_sequence_and_language_and_fuzz(void)
{
    uint8_t a[4096], b[4096];
    size_t na, nb;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t s1 = 0u, s2 = 0u;
    robo_font_discovery_result_t dr;
    robo_font_fallback_policy_t pol;
    robo_font_fallback_resolver_t *fb = NULL;
    robo_font_fallback_request_t req;
    robo_font_fallback_result_t res;
    uint32_t seq[2] = {0x0627u, 0x064Bu}; /* alef + fatha — only first in font B */
    const char *lang_prefs[] = {"LangArab"};
    robo_font_face_t *primary = NULL;
    robo_font_face_t *lang_face = NULL;
    uint32_t i;
    static const uint8_t junk[] = {0x01, 0x41, 0xFF, 0x00};

    na = build_font_with_cp(a, sizeof(a), "Base", "Base", 400, 0x41u, 1u);
    nb = build_font_with_cp(b, sizeof(b), "LangArab", "LangArab", 400, 0x0627u, 1u);
    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "a", a, na, &s1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "b", b, nb, &s2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_all(reg, 0u, &dr), ROBO_OK);
    for (i = 0u; i < robo_font_registry_registration_count(reg); i++) {
        const robo_font_registration_t *rec = NULL;
        ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, i, &rec), ROBO_OK);
        if (!strcmp(rec->family, "Base")) {
            primary = rec->face;
        }
        if (!strcmp(rec->family, "LangArab")) {
            lang_face = rec->face;
        }
    }

    robo_font_fallback_policy_init(&pol);
    pol.language_preferred_families = lang_prefs;
    pol.language_preferred_family_count = 1u;
    /* REQUIRE_ALL: sequence fails because mark missing → no fallback. */
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolver_create(reg, &pol, &fb), ROBO_OK);
    robo_font_fallback_request_init(&req);
    req.primary = primary;
    req.codepoints = seq;
    req.codepoint_count = 2u;
    req.language = "ar";
    robo_font_fallback_result_init(&res);
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolve(fb, &req, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.found, 0); /* mark not covered */
    robo_font_fallback_result_clear(&res);

    /* Single base with language hint → LangArab. */
    req.codepoint_count = 1u;
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolve(fb, &req, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.found, 1);
    ROBO_CHECK(res.face == lang_face);
    robo_font_fallback_result_clear(&res);

    ROBO_CHECK_EQ_INT(robo_font_fallback_fuzz_parse(junk, sizeof(junk)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_fallback_fuzz_parse(NULL, 0u), ROBO_OK);

    robo_font_fallback_resolver_destroy(fb);
    robo_font_registry_destroy(reg);
}

void robo_test_register_font_fallback(void)
{
    robo_test_register("font.fallback.primary_hit", test_primary_covers_no_fallback);
    robo_test_register("font.fallback.script_arabic", test_script_fallback_arabic);
    robo_test_register("font.fallback.preferred_none", test_preferred_and_style_and_none);
    robo_test_register("font.fallback.cache_determinism",
                       test_determinism_cache_invalidate);
    robo_test_register("font.fallback.sequence_lang_fuzz",
                       test_sequence_and_language_and_fuzz);
}
