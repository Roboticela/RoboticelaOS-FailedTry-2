/*
 * RoboFont discovery / registry suite (Goal 5.12).
 * Synthetic + virtual-IO fixtures only — no Chromium / system fonts.
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

static size_t build_post(uint8_t *b)
{
    uint8_t *p = b;
    put_u32(&p, 0x00030000u);
    put_u32(&p, 0);
    put_i16(&p, -100);
    put_i16(&p, 50);
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

static void test_classify_and_ext(void)
{
    uint8_t ttf[4] = {0x00, 0x01, 0x00, 0x00};
    uint8_t otto[4] = {'O', 'T', 'T', 'O'};
    uint8_t ttc[4] = {'t', 't', 'c', 'f'};
    uint8_t junk[4] = {1, 2, 3, 4};
    ROBO_CHECK_EQ_INT(robo_font_discovery_classify_bytes(ttf, 4), ROBO_FONT_CANDIDATE_TTF);
    ROBO_CHECK_EQ_INT(robo_font_discovery_classify_bytes(otto, 4), ROBO_FONT_CANDIDATE_OTF);
    ROBO_CHECK_EQ_INT(robo_font_discovery_classify_bytes(ttc, 4),
                      ROBO_FONT_CANDIDATE_COLLECTION);
    ROBO_CHECK_EQ_INT(robo_font_discovery_classify_bytes(junk, 4),
                      ROBO_FONT_CANDIDATE_UNSUPPORTED);
    ROBO_CHECK(robo_font_discovery_extension_is_font("A.TTF"));
    ROBO_CHECK(robo_font_discovery_extension_is_font("/x/y.otc"));
    ROBO_CHECK(!robo_font_discovery_extension_is_font("readme.txt"));
}

static void test_memory_register_and_select(void)
{
    uint8_t font[2048];
    size_t n;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t sid = 0u;
    robo_font_discovery_result_t result;
    robo_font_face_t **faces = NULL;
    uint32_t face_n = 0u;
    robo_font_select_request_t req;
    robo_font_selection_t sel;
    const robo_font_registration_t *rec = NULL;

    n = build_named_font(font, sizeof(font), "Discover", "Discover-Regular", 400);
    ROBO_CHECK(n > 0u);

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "mem:Discover", font, n, &sid), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, sid, 0u, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.registered, 1);
    ROBO_CHECK_EQ_INT((int)robo_font_registry_registration_count(reg), 1);

    ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, 0u, &rec), ROBO_OK);
    ROBO_CHECK(rec->face != NULL);
    ROBO_CHECK(rec->face_id != 0u);
    ROBO_CHECK(rec->id != 0u);

    ROBO_CHECK_EQ_INT(robo_font_registry_collect_faces(reg, &faces, &face_n), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)face_n, 1);

    robo_font_select_request_init(&req);
    robo_font_selection_init(&sel);
    req.family = "discover";
    req.weight = 400;
    ROBO_CHECK_EQ_INT(robo_font_select(&req, faces, face_n, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK(sel.face == faces[0]);
    robo_font_selection_clear(&sel);
    robo_free(faces);

    robo_font_registry_destroy(reg);
}

static void test_virtual_directory(void)
{
    uint8_t a[2048], b[2048], junk[8] = {0};
    size_t na, nb;
    robo_font_discovery_virtual_io_t *vio = NULL;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t sid = 0u;
    robo_font_discovery_result_t result;

    na = build_named_font(a, sizeof(a), "Alpha", "Alpha-Regular", 400);
    nb = build_named_font(b, sizeof(b), "Beta", "Beta-Bold", 700);
    ROBO_CHECK(na > 0u && nb > 0u);

    ROBO_CHECK_EQ_INT(robo_font_discovery_virtual_io_create(&vio), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_discovery_virtual_add_dir(vio, "/Fonts"), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_discovery_virtual_add_file(vio, "/Fonts/Alpha.ttf", a, na),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_discovery_virtual_add_file(vio, "/Fonts/Beta.otf", b, nb),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_discovery_virtual_add_file(vio, "/Fonts/readme.txt", junk, sizeof(junk)),
        ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    robo_font_registry_set_io(reg, robo_font_discovery_virtual_io_ops(vio));
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_directory_source(reg, "/Fonts", 0, &sid), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, sid, 0u, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.registered, 2);
    ROBO_CHECK(result.skipped_filter >= 1u);
    ROBO_CHECK_EQ_INT((int)robo_font_registry_registration_count(reg), 2);

    robo_font_registry_destroy(reg);
    robo_font_discovery_virtual_io_destroy(vio);
}

static void test_ttc_collection(void)
{
    uint8_t ttc[4096];
    size_t hdr = 12u + 8u; /* ttcf + ver + numFonts + 2 offsets */
    size_t na, nb;
    uint32_t count = 0u;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t sid = 0u;
    robo_font_discovery_result_t result;
    uint8_t *p;
    const robo_font_registration_t *r0 = NULL;
    const robo_font_registration_t *r1 = NULL;

    memset(ttc, 0, sizeof(ttc));
    p = ttc;
    put_u32(&p, ROBO_FONT_SFNT_VERSION_TTCF);
    put_u32(&p, 0x00010000u);
    put_u32(&p, 2u);
    put_u32(&p, (uint32_t)hdr); /* face 0 offset placeholder */
    put_u32(&p, 0u);            /* face 1 offset placeholder */

    na = build_named_font_at(ttc + hdr, sizeof(ttc) - hdr, (uint32_t)hdr, "CollA", "CollA",
                             400);
    ROBO_CHECK(na > 0u);
    p = ttc + 12;
    put_u32(&p, (uint32_t)hdr);
    put_u32(&p, (uint32_t)(hdr + na));
    nb = build_named_font_at(ttc + hdr + na, sizeof(ttc) - (hdr + na),
                             (uint32_t)(hdr + na), "CollB", "CollB", 700);
    ROBO_CHECK(nb > 0u);

    ROBO_CHECK_EQ_INT(robo_font_sfnt_probe_face_count(ttc, hdr + na + nb, &count),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)count, 2);

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "coll.ttc", ttc, hdr + na + nb, &sid),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, sid, 0u, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.registered, 2);
    ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, 0u, &r0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, 1u, &r1), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)r0->is_collection, 1);
    ROBO_CHECK_EQ_INT((int)r1->is_collection, 1);
    ROBO_CHECK(r0->face_id != r1->face_id);
    ROBO_CHECK(r0->face_index != r1->face_index);
    robo_font_registry_destroy(reg);
}

static void test_duplicate_and_refresh(void)
{
    uint8_t font[2048];
    size_t n;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t s1 = 0u, s2 = 0u;
    robo_font_discovery_result_t result;

    n = build_named_font(font, sizeof(font), "DupFam", "DupFam-Regular", 400);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "a", font, n, &s1), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "b", font, n, &s2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, s1, 0u, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.registered, 1);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, s2, 0u, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.registered, 0);
    ROBO_CHECK(result.skipped_duplicate >= 1u);
    ROBO_CHECK_EQ_INT((int)robo_font_registry_registration_count(reg), 1);

    ROBO_CHECK_EQ_INT(robo_font_registry_refresh_source(reg, s1, 0u, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.registered, 1);
    ROBO_CHECK_EQ_INT((int)robo_font_registry_registration_count(reg), 1);

    robo_font_registry_destroy(reg);
}

static void test_remove_and_unsupported(void)
{
    uint8_t font[2048];
    uint8_t bad[16] = {0};
    size_t n;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_source_id_t sid = 0u;
    robo_font_discovery_result_t result;
    const robo_font_registration_t *rec = NULL;
    robo_font_registration_id_t rid;

    n = build_named_font(font, sizeof(font), "Gone", "Gone", 400);
    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "gone", font, n, &sid), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, sid, 0u, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, 0u, &rec), ROBO_OK);
    rid = rec->id;
    ROBO_CHECK_EQ_INT(robo_font_registry_remove_registration(reg, rid), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_registry_registration_count(reg), 0);

    ROBO_CHECK_EQ_INT(
        robo_font_registry_add_memory_source(reg, "bad", bad, sizeof(bad), &sid), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_source(reg, sid, 0u, &result), ROBO_OK);
    ROBO_CHECK(result.skipped_unsupported >= 1u);

    robo_font_registry_destroy(reg);
}

static void test_fuzz_discovery(void)
{
    uint8_t junk[64];
    size_t i;
    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 19u + 3u);
    }
    ROBO_CHECK_EQ_INT(robo_font_discovery_fuzz_parse(junk, sizeof(junk)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_discovery_fuzz_parse(NULL, 0u), ROBO_OK);
}

void robo_test_register_font_discovery(void)
{
    robo_test_register("font.discover.classify", test_classify_and_ext);
    robo_test_register("font.discover.memory_select", test_memory_register_and_select);
    robo_test_register("font.discover.virtual_dir", test_virtual_directory);
    robo_test_register("font.discover.ttc", test_ttc_collection);
    robo_test_register("font.discover.duplicate_refresh", test_duplicate_and_refresh);
    robo_test_register("font.discover.remove_unsupported", test_remove_and_unsupported);
    robo_test_register("font.discover.fuzz", test_fuzz_discovery);
}
