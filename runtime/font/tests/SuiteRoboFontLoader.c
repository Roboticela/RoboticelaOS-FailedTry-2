/*
 * RoboFont loader / cache suite (Goal 5.13).
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

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
                         uint32_t n, uint32_t base_offset)
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
        put_u32(&p, (uint32_t)(base_offset + cursor));
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
    put_u32(&p, 0x00010000u);
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

static size_t build_post(uint8_t *b)
{
    uint8_t *p = b;
    put_u32(&p, 0x00030000u);
    put_u32(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    return (size_t)(p - b);
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

static size_t build_named_font_at(uint8_t *out, size_t cap, uint32_t base_offset,
                                  const char *family, const char *ps, uint16_t weight)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[8], loca[8], post[64], os2[128], name[512];
    synth_table_t tabs[9];
    uint32_t nt = 0u;
    size_t nn = build_name_table(name, sizeof(name), family, ps);
    if (nn == 0u) {
        return 0u;
    }
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
    return build_sfnt(out, cap, tabs, nt, base_offset);
}

static size_t build_named_font(uint8_t *out, size_t cap, const char *family,
                               const char *ps, uint16_t weight)
{
    return build_named_font_at(out, cap, 0u, family, ps, weight);
}

static void test_lazy_load_and_cache_hit(void)
{
    uint8_t font[2048];
    size_t n;
    robo_font_registry_t *reg = NULL;
    robo_font_loader_t *loader = NULL;
    robo_font_discovery_source_id_t sid = 0u;
    robo_font_discovery_result_t scan;
    const robo_font_registration_t *rec = NULL;
    robo_font_load_result_t a, b;
    const robo_font_cache_stats_t *st;

    n = build_named_font(font, sizeof(font), "LoadFam", "LoadFam-Regular", 400);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_loader_create(reg, NULL, &loader), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "mem:LoadFam", font, n, &sid), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, sid, 0u, &scan), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, 0u, &rec), ROBO_OK);

    /* Discovery registered metadata; metrics not yet loaded. */
    ROBO_CHECK(robo_font_face_metrics_info(rec->face) == NULL);
    ROBO_CHECK_EQ_INT(robo_font_loader_state(loader, rec->id), ROBO_FONT_LOAD_UNLOADED);

    robo_font_load_result_init(&a);
    ROBO_CHECK_EQ_INT(robo_font_loader_load_registration(loader, rec->id, &a), ROBO_OK);
    ROBO_CHECK_EQ_INT(a.state, ROBO_FONT_LOAD_LOADED);
    ROBO_CHECK_EQ_INT((int)a.from_cache, 0);
    ROBO_CHECK(a.face != NULL);
    ROBO_CHECK(robo_font_face_metrics_info(a.face) != NULL);
    ROBO_CHECK_EQ_INT(robo_font_loader_state(loader, rec->id), ROBO_FONT_LOAD_LOADED);

    robo_font_load_result_init(&b);
    ROBO_CHECK_EQ_INT(robo_font_loader_load_registration(loader, rec->id, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)b.from_cache, 1);
    ROBO_CHECK(b.face == a.face);

    st = robo_font_cache_stats(robo_font_loader_cache(loader));
    ROBO_CHECK(st->hits >= 1u);
    ROBO_CHECK(st->misses >= 1u);
    ROBO_CHECK(st->entries >= 1u);

    {
        robo_font_face_t **faces = NULL;
        uint32_t fn = 0u;
        robo_font_select_request_t req;
        robo_font_selection_t sel;
        faces = (robo_font_face_t **)robo_calloc(1, sizeof(*faces));
        ROBO_CHECK(faces != NULL);
        faces[0] = a.face;
        fn = 1u;
        robo_font_select_request_init(&req);
        robo_font_selection_init(&sel);
        req.family = "loadfam";
        ROBO_CHECK_EQ_INT(robo_font_select(&req, faces, fn, &sel), ROBO_OK);
        ROBO_CHECK_EQ_INT(sel.found, 1);
        robo_font_selection_clear(&sel);
        robo_free(faces);
    }

    robo_font_load_result_clear(&a);
    robo_font_load_result_clear(&b);
    robo_font_loader_destroy(loader);
    robo_font_registry_destroy(reg);
}

static void test_cache_evict_and_invalidate(void)
{
    uint8_t f0[2048], f1[2048], f2[2048];
    size_t n0, n1, n2;
    robo_font_registry_t *reg = NULL;
    robo_font_loader_t *loader = NULL;
    robo_font_loader_config_t cfg;
    robo_font_discovery_source_id_t s0 = 0u, s1 = 0u, s2 = 0u;
    robo_font_discovery_result_t scan;
    const robo_font_registration_t *r0 = NULL, *r1 = NULL, *r2 = NULL;
    robo_font_load_result_t lr;
    robo_font_registration_id_t id0, id1;

    n0 = build_named_font(f0, sizeof(f0), "E0", "E0", 400);
    n1 = build_named_font(f1, sizeof(f1), "E1", "E1", 400);
    n2 = build_named_font(f2, sizeof(f2), "E2", "E2", 400);
    ROBO_CHECK(n0 > 0u && n1 > 0u && n2 > 0u);

    robo_font_loader_config_init(&cfg);
    cfg.max_faces = 2u;
    cfg.max_bytes = ROBO_FONT_CACHE_DEFAULT_MAX_BYTES;

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_loader_create(reg, &cfg, &loader), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "e0", f0, n0, &s0),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "e1", f1, n1, &s1),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "e2", f2, n2, &s2),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, s0, 0u, &scan), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, s1, 0u, &scan), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, s2, 0u, &scan), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, 0u, &r0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, 1u, &r1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, 2u, &r2), ROBO_OK);
    id0 = r0->id;
    id1 = r1->id;

    robo_font_load_result_init(&lr);
    ROBO_CHECK_EQ_INT(robo_font_loader_load_registration(loader, id0, &lr), ROBO_OK);
    robo_font_load_result_clear(&lr);
    ROBO_CHECK_EQ_INT(robo_font_loader_load_registration(loader, id1, &lr), ROBO_OK);
    robo_font_load_result_clear(&lr);
    ROBO_CHECK_EQ_INT(robo_font_loader_load_registration(loader, r2->id, &lr), ROBO_OK);
    robo_font_load_result_clear(&lr);
    ROBO_CHECK(robo_font_cache_entry_count(robo_font_loader_cache(loader)) <= 2u);

    ROBO_CHECK_EQ_INT(
        robo_font_cache_invalidate_registration(robo_font_loader_cache(loader), r2->id),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_loader_state(loader, r2->id), ROBO_FONT_LOAD_UNLOADED);

    ROBO_CHECK_EQ_INT(robo_font_registry_remove_registration(reg, id0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_loader_state(loader, id0), ROBO_FONT_LOAD_UNLOADED);

    robo_font_cache_clear(robo_font_loader_cache(loader));
    ROBO_CHECK_EQ_INT((int)robo_font_cache_entry_count(robo_font_loader_cache(loader)),
                      0);

    robo_font_loader_destroy(loader);
    robo_font_registry_destroy(reg);
}

static void test_ttc_load_and_failures(void)
{
    uint8_t ttc[4096];
    size_t hdr = 12u + 8u;
    size_t na, nb;
    uint8_t *p;
    robo_font_registry_t *reg = NULL;
    robo_font_loader_t *loader = NULL;
    robo_font_discovery_source_id_t sid = 0u;
    robo_font_discovery_result_t scan;
    const robo_font_registration_t *r0 = NULL, *r1 = NULL;
    robo_font_load_result_t lr;
    uint8_t bad[32];

    memset(ttc, 0, sizeof(ttc));
    p = ttc;
    put_u32(&p, ROBO_FONT_SFNT_VERSION_TTCF);
    put_u32(&p, 0x00010000u);
    put_u32(&p, 2u);
    put_u32(&p, (uint32_t)hdr);
    put_u32(&p, 0u);
    na = build_named_font_at(ttc + hdr, sizeof(ttc) - hdr, (uint32_t)hdr, "LCollA",
                             "LCollA", 400);
    ROBO_CHECK(na > 0u);
    p = ttc + 12;
    put_u32(&p, (uint32_t)hdr);
    put_u32(&p, (uint32_t)(hdr + na));
    nb = build_named_font_at(ttc + hdr + na, sizeof(ttc) - (hdr + na),
                             (uint32_t)(hdr + na), "LCollB", "LCollB", 700);
    ROBO_CHECK(nb > 0u);

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_loader_create(reg, NULL, &loader), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "c.ttc", ttc, hdr + na + nb, &sid),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, sid, 0u, &scan), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)scan.registered, 2);
    ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, 0u, &r0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, 1u, &r1), ROBO_OK);

    robo_font_load_result_init(&lr);
    ROBO_CHECK_EQ_INT(robo_font_loader_load_registration(loader, r0->id, &lr), ROBO_OK);
    ROBO_CHECK(lr.face != NULL);
    ROBO_CHECK_EQ_INT((int)robo_font_face_index(lr.face), (int)r0->face_index);
    robo_font_load_result_clear(&lr);
    ROBO_CHECK_EQ_INT(robo_font_loader_load_registration(loader, r1->id, &lr), ROBO_OK);
    ROBO_CHECK(lr.face != NULL);
    ROBO_CHECK(robo_font_face_source(lr.face) == robo_font_face_source(r0->face) ||
               robo_font_face_source(lr.face) != NULL);
    robo_font_load_result_clear(&lr);

    /* Missing registration */
    ROBO_CHECK_EQ_INT(robo_font_loader_load_registration(loader, 99999u, &lr),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(lr.state, ROBO_FONT_LOAD_FAILED);

    /* Unsupported / malformed memory scan + load path via fuzz */
    memset(bad, 0xA5, sizeof(bad));
    ROBO_CHECK_EQ_INT(robo_font_loader_fuzz_parse(bad, sizeof(bad)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_loader_fuzz_parse(NULL, 0u), ROBO_OK);

    robo_font_loader_destroy(loader);
    robo_font_registry_destroy(reg);
}

static void test_virtual_file_load_and_bench(void)
{
    uint8_t font[2048];
    size_t n;
    robo_font_discovery_virtual_io_t *vio = NULL;
    robo_font_registry_t *reg = NULL;
    robo_font_loader_t *loader = NULL;
    robo_font_discovery_source_id_t sid = 0u;
    robo_font_discovery_result_t scan;
    const robo_font_registration_t *rec = NULL;
    robo_font_load_result_t lr;
    uint32_t i;
    uint32_t cold_hits = 0u;
    const robo_font_cache_stats_t *st;

    n = build_named_font(font, sizeof(font), "VirtLoad", "VirtLoad", 400);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_discovery_virtual_io_create(&vio), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_discovery_virtual_add_file(vio, "/Fonts/VirtLoad.ttf", font, n),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    robo_font_registry_set_io(reg, robo_font_discovery_virtual_io_ops(vio));
    ROBO_CHECK_EQ_INT(robo_font_loader_create(reg, NULL, &loader), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_file_source(reg, "/Fonts/VirtLoad.ttf", &sid),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, sid, 0u, &scan), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, 0u, &rec), ROBO_OK);

    robo_font_load_result_init(&lr);
    ROBO_CHECK_EQ_INT(robo_font_loader_load_registration(loader, rec->id, &lr), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)lr.from_cache, 0);
    robo_font_load_result_clear(&lr);

    for (i = 0u; i < 64u; i++) {
        ROBO_CHECK_EQ_INT(robo_font_loader_load_registration(loader, rec->id, &lr),
                          ROBO_OK);
        cold_hits += lr.from_cache;
        robo_font_load_result_clear(&lr);
    }
    ROBO_CHECK(cold_hits == 64u);
    st = robo_font_cache_stats(robo_font_loader_cache(loader));
    ROBO_CHECK(st->hits >= 64u);

    /* Bypass: load without requiring a new cache insert (hit OK if already cached). */
    {
        robo_font_load_request_t req;
        uint32_t before = robo_font_cache_entry_count(robo_font_loader_cache(loader));
        robo_font_load_request_init(&req);
        req.registration_id = rec->id;
        req.cache_policy = ROBO_FONT_CACHE_POLICY_BYPASS;
        ROBO_CHECK_EQ_INT(robo_font_loader_load(loader, &req, &lr), ROBO_OK);
        ROBO_CHECK(lr.face != NULL);
        ROBO_CHECK_EQ_INT((int)robo_font_cache_entry_count(robo_font_loader_cache(loader)),
                          (int)before);
        robo_font_load_result_clear(&lr);
    }

    robo_font_loader_destroy(loader);
    robo_font_registry_destroy(reg);
    robo_font_discovery_virtual_io_destroy(vio);
}

static void test_refresh_invalidates_cache(void)
{
    uint8_t font[2048];
    size_t n;
    robo_font_registry_t *reg = NULL;
    robo_font_loader_t *loader = NULL;
    robo_font_discovery_source_id_t sid = 0u;
    robo_font_discovery_result_t scan;
    const robo_font_registration_t *rec = NULL;
    robo_font_load_result_t lr;
    robo_font_registration_id_t old_id;

    n = build_named_font(font, sizeof(font), "RefFam", "RefFam", 400);
    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_loader_create(reg, NULL, &loader), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "ref", font, n, &sid), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, sid, 0u, &scan), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, 0u, &rec), ROBO_OK);
    old_id = rec->id;
    ROBO_CHECK_EQ_INT(robo_font_loader_load_registration(loader, old_id, &lr), ROBO_OK);
    robo_font_load_result_clear(&lr);
    ROBO_CHECK(robo_font_cache_entry_count(robo_font_loader_cache(loader)) >= 1u);

    ROBO_CHECK_EQ_INT(robo_font_registry_refresh_source(reg, sid, 0u, &scan), ROBO_OK);
    /* Refresh removes old registrations → invalidate hook clears cache entry. */
    ROBO_CHECK_EQ_INT(robo_font_loader_state(loader, old_id), ROBO_FONT_LOAD_UNLOADED);

    robo_font_loader_destroy(loader);
    robo_font_registry_destroy(reg);
}

void robo_test_register_font_loader(void)
{
    robo_test_register("font.loader.lazy_cache", test_lazy_load_and_cache_hit);
    robo_test_register("font.loader.evict_invalidate", test_cache_evict_and_invalidate);
    robo_test_register("font.loader.ttc_fail_fuzz", test_ttc_load_and_failures);
    robo_test_register("font.loader.virtual_bench", test_virtual_file_load_and_bench);
    robo_test_register("font.loader.refresh", test_refresh_invalidates_cache);
}
