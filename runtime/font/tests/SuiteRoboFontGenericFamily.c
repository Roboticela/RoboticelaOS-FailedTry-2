/*
 * RoboFont generic-family suite (Goal 5.16).
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
    put_i16(&p, 0);
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
    put_u16(&p, 2);
    return (size_t)(p - buf);
}

static size_t build_maxp(uint8_t *buf)
{
    uint8_t *p = buf;
    int i;
    put_u32(&p, 0x00010000u);
    put_u16(&p, 2);
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
                                 const char *ps, uint16_t weight, uint16_t cp)
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
    nc = build_cmap_fmt4(cmap, cp, 1u);
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

static void test_ids_and_names(void)
{
    robo_font_generic_family_t g = ROBO_FONT_GENERIC_UNKNOWN;
    unsigned i;
    ROBO_CHECK(!robo_font_generic_family_is_valid(ROBO_FONT_GENERIC_UNKNOWN));
    ROBO_CHECK(!robo_font_generic_family_is_valid(ROBO_FONT_GENERIC_COUNT));
    for (i = 1u; i < (unsigned)ROBO_FONT_GENERIC_COUNT; i++) {
        ROBO_CHECK(robo_font_generic_family_is_valid((robo_font_generic_family_t)i));
        ROBO_CHECK(robo_font_generic_family_name((robo_font_generic_family_t)i) != NULL);
        ROBO_CHECK_EQ_INT(
            robo_font_generic_family_from_name(
                robo_font_generic_family_name((robo_font_generic_family_t)i), 0u, &g),
            ROBO_OK);
        ROBO_CHECK_EQ_INT((int)g, (int)i);
    }
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("Sans-Serif", 0u, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)g, (int)ROBO_FONT_GENERIC_SANS_SERIF);
    ROBO_CHECK_EQ_INT(robo_font_generic_family_from_name("nope", 0u, &g), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)robo_font_generic_related(ROBO_FONT_GENERIC_UI_SANS_SERIF),
                      (int)ROBO_FONT_GENERIC_SANS_SERIF);
    ROBO_CHECK_EQ_INT((int)robo_font_generic_related(ROBO_FONT_GENERIC_SYSTEM_UI),
                      (int)ROBO_FONT_GENERIC_UNKNOWN);
    ROBO_CHECK_EQ_INT((int)robo_font_generic_related(ROBO_FONT_GENERIC_EMOJI),
                      (int)ROBO_FONT_GENERIC_UNKNOWN);
    ROBO_CHECK_EQ_INT((int)robo_font_generic_related(ROBO_FONT_GENERIC_MATH),
                      (int)ROBO_FONT_GENERIC_UNKNOWN);
}

static void test_config_precedence_and_empty(void)
{
    robo_font_registry_t *reg = NULL;
    robo_font_generic_config_t *plat = NULL;
    robo_font_generic_config_t *run = NULL;
    robo_font_generic_config_t *app = NULL;
    robo_font_generic_resolver_t *r = NULL;
    robo_font_generic_resolver_options_t opts;
    robo_font_generic_request_t req;
    robo_font_generic_result_t res;
    const char *plat_f[] = {"PlatformSans"};
    const char *run_f[] = {"RuntimeSans"};
    const char *app_f[] = {"AppSans", "AppSansB"};
    uint64_t gen0;

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_config_create_empty_platform(&plat), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_config_create(&run), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_config_create(&app), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_font_generic_config_set_mapping(
                          plat, ROBO_FONT_GENERIC_SANS_SERIF, plat_f, 1u),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_generic_config_set_mapping(run, ROBO_FONT_GENERIC_SANS_SERIF, run_f, 1u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_generic_config_set_mapping(app, ROBO_FONT_GENERIC_SANS_SERIF, app_f, 2u),
        ROBO_OK);

    robo_font_generic_resolver_options_init(&opts);
    opts.platform = plat;
    opts.runtime = run;
    opts.application = app;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolver_create(reg, &opts, &r), ROBO_OK);

    robo_font_generic_request_init(&req);
    req.generic = ROBO_FONT_GENERIC_SANS_SERIF;
    robo_font_generic_result_init(&res);
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.resolved, 1);
    ROBO_CHECK_EQ_INT((int)res.layer, (int)ROBO_FONT_GENERIC_LAYER_APPLICATION);
    ROBO_CHECK_EQ_INT((int)res.family_count, 2);
    ROBO_CHECK_STR_EQ(res.families[0], "AppSans");
    ROBO_CHECK_STR_EQ(res.families[1], "AppSansB");
    gen0 = res.config_generation;
    robo_font_generic_result_clear(&res);

    /* Clear application → runtime wins. */
    ROBO_CHECK_EQ_INT(robo_font_generic_resolver_set_application(r, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.resolved, 1);
    ROBO_CHECK_EQ_INT((int)res.layer, (int)ROBO_FONT_GENERIC_LAYER_RUNTIME);
    ROBO_CHECK_STR_EQ(res.families[0], "RuntimeSans");
    ROBO_CHECK(res.config_generation != gen0);
    robo_font_generic_result_clear(&res);

    ROBO_CHECK_EQ_INT(robo_font_generic_resolver_set_runtime(r, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.resolved, 1);
    ROBO_CHECK_EQ_INT((int)res.layer, (int)ROBO_FONT_GENERIC_LAYER_PLATFORM);
    ROBO_CHECK_STR_EQ(res.families[0], "PlatformSans");
    robo_font_generic_result_clear(&res);

    /* Unmapped generic → unresolved (no invented font). */
    req.generic = ROBO_FONT_GENERIC_EMOJI;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.resolved, 0);
    ROBO_CHECK_EQ_INT((int)res.layer, (int)ROBO_FONT_GENERIC_LAYER_NONE);
    robo_font_generic_result_clear(&res);

    /* Invalid generic. */
    req.generic = ROBO_FONT_GENERIC_UNKNOWN;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &res), ROBO_INVALID_ARGUMENT);

    robo_font_generic_resolver_destroy(r);
    robo_font_generic_config_release(plat);
    robo_font_generic_config_release(run);
    robo_font_generic_config_release(app);
    robo_font_registry_destroy(reg);
}

static void test_related_and_specials(void)
{
    robo_font_registry_t *reg = NULL;
    robo_font_generic_config_t *cfg = NULL;
    robo_font_generic_resolver_t *r = NULL;
    robo_font_generic_resolver_options_t opts;
    robo_font_generic_request_t req;
    robo_font_generic_result_t res;
    const char *sans[] = {"UIBaseSans"};
    const char *sys[] = {"SystemUIFace"};
    const char *emoji[] = {"EmojiFace"};
    const char *math[] = {"MathFace"};
    const char *uiround[] = {"RoundFace"};

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_config_create(&cfg), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_generic_config_set_mapping(cfg, ROBO_FONT_GENERIC_SANS_SERIF, sans, 1u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_generic_config_set_mapping(cfg, ROBO_FONT_GENERIC_SYSTEM_UI, sys, 1u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_generic_config_set_mapping(cfg, ROBO_FONT_GENERIC_EMOJI, emoji, 1u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_generic_config_set_mapping(cfg, ROBO_FONT_GENERIC_MATH, math, 1u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_config_set_mapping(
                          cfg, ROBO_FONT_GENERIC_UI_ROUNDED, uiround, 1u),
                      ROBO_OK);

    robo_font_generic_resolver_options_init(&opts);
    opts.runtime = cfg;
    opts.flags = ROBO_FONT_GENERIC_FLAG_ALLOW_RELATED | ROBO_FONT_GENERIC_FLAG_SKIP_MISSING;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolver_create(reg, &opts, &r), ROBO_OK);

    robo_font_generic_request_init(&req);
    req.generic = ROBO_FONT_GENERIC_UI_SANS_SERIF; /* no direct map → related */
    req.flags = ROBO_FONT_GENERIC_FLAG_ALLOW_RELATED;
    robo_font_generic_result_init(&res);
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.resolved, 1);
    ROBO_CHECK_EQ_INT(res.used_related, 1);
    ROBO_CHECK_EQ_INT((int)res.effective, (int)ROBO_FONT_GENERIC_SANS_SERIF);
    ROBO_CHECK_STR_EQ(res.families[0], "UIBaseSans");
    robo_font_generic_result_clear(&res);

    req.generic = ROBO_FONT_GENERIC_SYSTEM_UI;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.resolved, 1);
    ROBO_CHECK_STR_EQ(res.families[0], "SystemUIFace");
    ROBO_CHECK(strcmp(res.families[0], "UIBaseSans") != 0); /* not == sans-serif */
    robo_font_generic_result_clear(&res);

    req.generic = ROBO_FONT_GENERIC_EMOJI;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.families[0], "EmojiFace");
    robo_font_generic_result_clear(&res);

    req.generic = ROBO_FONT_GENERIC_MATH;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.families[0], "MathFace");
    robo_font_generic_result_clear(&res);

    req.generic = ROBO_FONT_GENERIC_UI_ROUNDED;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.families[0], "RoundFace");
    robo_font_generic_result_clear(&res);

    robo_font_generic_resolver_destroy(r);
    robo_font_generic_config_release(cfg);
    robo_font_registry_destroy(reg);
}

static void test_select_and_fallback_integration(void)
{
    uint8_t a[4096], b[4096], c[4096];
    size_t na, nb, nc;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t s1 = 0u, s2 = 0u, s3 = 0u;
    robo_font_discovery_result_t dr;
    robo_font_generic_config_t *cfg = NULL;
    robo_font_generic_resolver_t *gr = NULL;
    robo_font_fallback_resolver_t *fb = NULL;
    robo_font_generic_resolver_options_t opts;
    robo_font_generic_request_t greq;
    robo_font_select_request_t sreq;
    robo_font_generic_result_t gres;
    robo_font_selection_t sel;
    robo_font_fallback_result_t fres;
    const char *sans[] = {"MissingFirst", "SansA", "SansB"};
    uint32_t cp_ok = 0x41u;
    uint32_t cp_miss = 0x0627u;

    na = build_font_with_cp(a, sizeof(a), "SansA", "SansA", 400, 0x41u);
    nb = build_font_with_cp(b, sizeof(b), "SansB", "SansB", 700, 0x41u);
    nc = build_font_with_cp(c, sizeof(c), "ArabFB", "ArabFB", 400, 0x0627u);
    ROBO_CHECK(na > 0u && nb > 0u && nc > 0u);

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "a", a, na, &s1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "b", b, nb, &s2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "c", c, nc, &s3), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_all(reg, 0u, &dr), ROBO_OK);
    ROBO_CHECK((int)robo_font_registry_registration_count(reg) >= 3);

    ROBO_CHECK_EQ_INT(robo_font_generic_config_create(&cfg), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_generic_config_set_mapping(cfg, ROBO_FONT_GENERIC_SANS_SERIF, sans, 3u),
        ROBO_OK);

    robo_font_generic_resolver_options_init(&opts);
    opts.application = cfg;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolver_create(reg, &opts, &gr), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolver_create(reg, NULL, &fb), ROBO_OK);

    robo_font_generic_request_init(&greq);
    greq.generic = ROBO_FONT_GENERIC_SANS_SERIF;
    robo_font_select_request_init(&sreq);
    sreq.weight = 400;
    sreq.stretch = ROBO_FONT_STRETCH_NORMAL;
    sreq.style = ROBO_FONT_STYLE_NORMAL;

    robo_font_generic_result_init(&gres);
    robo_font_selection_init(&sel);
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve_select(gr, &greq, &sreq, &gres, &sel),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(gres.resolved, 1);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK(sel.face != NULL);
    /* MissingFirst skipped; SansA preferred over SansB for weight 400. */
    {
        const robo_font_registration_t *rec = NULL;
        ROBO_CHECK_EQ_INT(
            robo_font_registry_find_by_face_id(reg, sel.face_id, &rec), ROBO_OK);
        ROBO_CHECK(rec && strcmp(rec->family, "SansA") == 0);
    }
    robo_font_selection_clear(&sel);
    robo_font_generic_result_clear(&gres);

    /* Weight 700 prefers SansB when walking — first with faces is SansA though.
     * Priority is family order first, then style match within that family.
     * So SansA still wins. Verify bold request still finds a face. */
    sreq.weight = 700;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve_select(gr, &greq, &sreq, &gres, &sel),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    robo_font_selection_clear(&sel);
    robo_font_generic_result_clear(&gres);

    /* Fallback: Arabic missing from sans primary → ArabFB via preferred list
     * won't include ArabFB; registry general scan should find it. */
    sreq.weight = 400;
    robo_font_fallback_result_init(&fres);
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve_fallback(gr, fb, &greq, &sreq, &cp_miss,
                                                         1u, &gres, &sel, &fres),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK_EQ_INT(fres.found, 1);
    ROBO_CHECK_EQ_INT(fres.used_primary, 0);
    {
        const robo_font_registration_t *rec = NULL;
        ROBO_CHECK_EQ_INT(
            robo_font_registry_find_by_face_id(reg, fres.face_id, &rec), ROBO_OK);
        ROBO_CHECK(rec && strcmp(rec->family, "ArabFB") == 0);
    }
    robo_font_fallback_result_clear(&fres);
    robo_font_selection_clear(&sel);
    robo_font_generic_result_clear(&gres);

    /* Primary hit path. */
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve_fallback(gr, fb, &greq, &sreq, &cp_ok, 1u,
                                                         NULL, NULL, &fres),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(fres.found, 1);
    ROBO_CHECK_EQ_INT(fres.used_primary, 1);
    robo_font_fallback_result_clear(&fres);

    robo_font_fallback_resolver_destroy(fb);
    robo_font_generic_resolver_destroy(gr);
    robo_font_generic_config_release(cfg);
    robo_font_registry_destroy(reg);
}

static void test_cache_language_fuzz(void)
{
    robo_font_registry_t *reg = NULL;
    robo_font_generic_config_t *cfg = NULL;
    robo_font_generic_resolver_t *r = NULL;
    robo_font_generic_resolver_options_t opts;
    robo_font_generic_request_t req;
    robo_font_generic_result_t r1, r2;
    robo_font_generic_cache_stats_t st;
    const char *def_f[] = {"DefaultSerif"};
    const char *ar_f[] = {"ArabicSerif"};
    uint8_t junk[32];
    uint32_t i;

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_config_create(&cfg), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_generic_config_set_mapping(cfg, ROBO_FONT_GENERIC_SERIF, def_f, 1u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_config_set_mapping_for_language(
                          cfg, ROBO_FONT_GENERIC_SERIF, "ar", 0u, ar_f, 1u),
                      ROBO_OK);

    robo_font_generic_resolver_options_init(&opts);
    opts.runtime = cfg;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolver_create(reg, &opts, &r), ROBO_OK);

    robo_font_generic_request_init(&req);
    req.generic = ROBO_FONT_GENERIC_SERIF;
    robo_font_generic_result_init(&r1);
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &r1), ROBO_OK);
    ROBO_CHECK_STR_EQ(r1.families[0], "DefaultSerif");

    robo_font_generic_result_init(&r2);
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &r2), ROBO_OK);
    ROBO_CHECK_EQ_INT(r2.from_cache, 1);
    ROBO_CHECK_STR_EQ(r2.families[0], "DefaultSerif");
    robo_font_generic_result_clear(&r1);
    robo_font_generic_result_clear(&r2);

    req.language = "ar";
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &r1), ROBO_OK);
    ROBO_CHECK_STR_EQ(r1.families[0], "ArabicSerif");
    robo_font_generic_result_clear(&r1);

    /* Config mutation invalidates. */
    ROBO_CHECK_EQ_INT(
        robo_font_generic_config_set_mapping(cfg, ROBO_FONT_GENERIC_SERIF, def_f, 1u),
        ROBO_OK);
    robo_font_generic_resolver_cache_clear(r);
    /* Generation bump via set_runtime re-retain. */
    ROBO_CHECK_EQ_INT(robo_font_generic_resolver_set_runtime(r, cfg), ROBO_OK);
    req.language = NULL;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &r1), ROBO_OK);
    ROBO_CHECK_EQ_INT(r1.from_cache, 0);
    robo_font_generic_result_clear(&r1);

    robo_font_generic_resolver_cache_stats(r, &st);
    ROBO_CHECK(st.hits >= 1u);

    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 17u);
    }
    ROBO_CHECK_EQ_INT(robo_font_generic_fuzz_parse(junk, sizeof(junk)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_generic_fuzz_parse(NULL, 0u), ROBO_OK);

    /* Duplicate entries keep deterministic order. */
    {
        const char *dup[] = {"A", "B", "A"};
        ROBO_CHECK_EQ_INT(robo_font_generic_config_set_mapping(
                              cfg, ROBO_FONT_GENERIC_MONOSPACE, dup, 3u),
                          ROBO_OK);
        req.generic = ROBO_FONT_GENERIC_MONOSPACE;
        ROBO_CHECK_EQ_INT(robo_font_generic_resolve(r, &req, &r1), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)r1.family_count, 3);
        ROBO_CHECK_STR_EQ(r1.families[0], "A");
        ROBO_CHECK_STR_EQ(r1.families[1], "B");
        ROBO_CHECK_STR_EQ(r1.families[2], "A");
        robo_font_generic_result_clear(&r1);
    }

    robo_font_generic_resolver_destroy(r);
    robo_font_generic_config_release(cfg);
    robo_font_registry_destroy(reg);
}

void robo_test_register_font_generic(void)
{
    robo_test_register("font.generic.ids_names", test_ids_and_names);
    robo_test_register("font.generic.config_precedence", test_config_precedence_and_empty);
    robo_test_register("font.generic.related_specials", test_related_and_specials);
    robo_test_register("font.generic.select_fallback",
                       test_select_and_fallback_integration);
    robo_test_register("font.generic.cache_lang_fuzz", test_cache_language_fuzz);
}
