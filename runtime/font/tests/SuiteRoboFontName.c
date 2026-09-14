/*
 * RoboFont naming & metadata suite (Goal 5.9).
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
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

static void put_utf16be(uint8_t **p, const char *ascii)
{
    while (*ascii) {
        put_u8(p, 0);
        put_u8(p, (uint8_t)*ascii++);
    }
}

static size_t build_name_multi(uint8_t *buf, size_t cap)
{
    /* format 0, 4 records: family en-US, family de-DE, subfamily, PS, version, typo family */
    uint8_t storage[256];
    uint8_t *s = storage;
    uint8_t *p = buf;
    size_t storage_off;
    uint16_t off_test, off_probe, off_reg, off_ps, off_ver, off_typo;
    const char *ps = "TestPS";

    if (cap < 200u) {
        return 0u;
    }
    off_test = (uint16_t)(s - storage);
    put_utf16be(&s, "Test");
    off_probe = (uint16_t)(s - storage);
    put_utf16be(&s, "Probe");
    off_reg = (uint16_t)(s - storage);
    put_utf16be(&s, "Regular");
    off_ps = (uint16_t)(s - storage);
    put_utf16be(&s, ps);
    off_ver = (uint16_t)(s - storage);
    put_utf16be(&s, "Version 1.2.3");
    off_typo = (uint16_t)(s - storage);
    put_utf16be(&s, "TypoFamily");

    storage_off = 6u + 6u * 12u;
    put_u16(&p, 0);
    put_u16(&p, 6);
    put_u16(&p, (uint16_t)storage_off);

    /* en-US family */
    put_u16(&p, 3);
    put_u16(&p, 1);
    put_u16(&p, 0x0409);
    put_u16(&p, ROBO_FONT_NAME_ID_FAMILY);
    put_u16(&p, 8);
    put_u16(&p, off_test);
    /* de-DE family */
    put_u16(&p, 3);
    put_u16(&p, 1);
    put_u16(&p, 0x0407);
    put_u16(&p, ROBO_FONT_NAME_ID_FAMILY);
    put_u16(&p, 10);
    put_u16(&p, off_probe);
    /* subfamily */
    put_u16(&p, 3);
    put_u16(&p, 1);
    put_u16(&p, 0x0409);
    put_u16(&p, ROBO_FONT_NAME_ID_SUBFAMILY);
    put_u16(&p, 14);
    put_u16(&p, off_reg);
    /* PostScript */
    put_u16(&p, 3);
    put_u16(&p, 1);
    put_u16(&p, 0x0409);
    put_u16(&p, ROBO_FONT_NAME_ID_POSTSCRIPT);
    put_u16(&p, (uint16_t)(strlen(ps) * 2u));
    put_u16(&p, off_ps);
    /* version */
    put_u16(&p, 3);
    put_u16(&p, 1);
    put_u16(&p, 0x0409);
    put_u16(&p, ROBO_FONT_NAME_ID_VERSION);
    put_u16(&p, (uint16_t)(strlen("Version 1.2.3") * 2u));
    put_u16(&p, off_ver);
    /* typographic family */
    put_u16(&p, 3);
    put_u16(&p, 1);
    put_u16(&p, 0x0409);
    put_u16(&p, ROBO_FONT_NAME_ID_TYPO_FAMILY);
    put_u16(&p, (uint16_t)(strlen("TypoFamily") * 2u));
    put_u16(&p, off_typo);

    memcpy(buf + storage_off, storage, (size_t)(s - storage));
    return storage_off + (size_t)(s - storage);
}

static size_t build_name_mac_roman(uint8_t *buf, size_t cap)
{
    uint8_t *p = buf;
    size_t storage_off = 6u + 12u;
    const char *s = "MacFace";
    if (cap < storage_off + 8u) {
        return 0u;
    }
    put_u16(&p, 0);
    put_u16(&p, 1);
    put_u16(&p, (uint16_t)storage_off);
    put_u16(&p, 1); /* Mac */
    put_u16(&p, 0); /* Roman */
    put_u16(&p, 0); /* English */
    put_u16(&p, ROBO_FONT_NAME_ID_FAMILY);
    put_u16(&p, (uint16_t)strlen(s));
    put_u16(&p, 0);
    memcpy(buf + storage_off, s, strlen(s));
    return storage_off + strlen(s);
}

static size_t build_name_surrogate(uint8_t *buf, size_t cap)
{
    /* U+1F600 😀 as UTF-16BE surrogate pair in full name */
    uint8_t *p = buf;
    size_t storage_off = 6u + 12u;
    uint8_t emoji[] = {0xD8, 0x3D, 0xDE, 0x00}; /* wrong - 1F600 is D83D DE00 */
    if (cap < storage_off + 8u) {
        return 0u;
    }
    emoji[0] = 0xD8;
    emoji[1] = 0x3D;
    emoji[2] = 0xDE;
    emoji[3] = 0x00;
    put_u16(&p, 0);
    put_u16(&p, 1);
    put_u16(&p, (uint16_t)storage_off);
    put_u16(&p, 3);
    put_u16(&p, 1);
    put_u16(&p, 0x0409);
    put_u16(&p, ROBO_FONT_NAME_ID_FULL);
    put_u16(&p, 4);
    put_u16(&p, 0);
    memcpy(buf + storage_off, emoji, 4);
    return storage_off + 4u;
}

static void test_name_metadata_basic(void)
{
    uint8_t name[512];
    uint8_t font[1024];
    synth_table_t tabs[1];
    size_t nn = build_name_multi(name, sizeof(name));
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    const robo_font_metadata_t *meta;
    robo_font_meta_field_t pref;
    robo_i18n_locale_t de;
    const robo_font_name_entry_t *ent = NULL;
    robo_font_name_lookup_kind_t kind;

    ROBO_CHECK(nn > 0u);
    tabs[0].tag = ROBO_FONT_TAG_NAME;
    tabs[0].data = name;
    tabs[0].length = (uint32_t)nn;
    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, 1);
    ROBO_CHECK(n > 0u);

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metadata(face), ROBO_OK);
    meta = robo_font_face_metadata(face);
    ROBO_CHECK(meta != NULL);
    ROBO_CHECK_EQ_INT((int)meta->family_name.avail, (int)ROBO_FONT_NAME_PRESENT);
    ROBO_CHECK_STR_EQ(meta->family_name.utf8, "Test");
    ROBO_CHECK_STR_EQ(meta->subfamily_name.utf8, "Regular");
    ROBO_CHECK_STR_EQ(meta->postscript_name.utf8, "TestPS");
    ROBO_CHECK(meta->postscript_name_structurally_ok);
    ROBO_CHECK_STR_EQ(meta->typographic_family_name.utf8, "TypoFamily");
    ROBO_CHECK(meta->version_parsed.valid);
    ROBO_CHECK_EQ_INT((int)meta->version_parsed.major, 1);
    ROBO_CHECK_EQ_INT((int)meta->version_parsed.minor, 2);

    ROBO_CHECK_EQ_INT(robo_font_metadata_preferred_family(meta, &pref), ROBO_OK);
    ROBO_CHECK_STR_EQ(pref.utf8, "TypoFamily");
    ROBO_CHECK_EQ_INT((int)pref.avail, (int)ROBO_FONT_NAME_PRESENT);

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("de-DE", 5, &de), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_name_info_lookup(robo_font_face_name_info(face),
                                                 ROBO_FONT_NAME_ID_FAMILY, &de, &ent, &kind),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(ent->utf8, "Probe");
    ROBO_CHECK_EQ_INT((int)kind, (int)ROBO_FONT_NAME_LOOKUP_EXACT_LOCALE);

    ROBO_CHECK_EQ_INT(robo_font_name_fuzz_parse(font, n), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_name_fuzz_parse(name, nn), ROBO_OK);

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_name_mac_roman(void)
{
    uint8_t name[128];
    size_t nn = build_name_mac_roman(name, sizeof(name));
    robo_font_span_t span = {name, nn};
    robo_font_name_info_t *info = NULL;
    const robo_font_metadata_t *meta;

    ROBO_CHECK_EQ_INT(robo_font_name_info_from_span(span, NULL, &info), ROBO_OK);
    meta = robo_font_name_info_metadata(info);
    ROBO_CHECK_STR_EQ(meta->family_name.utf8, "MacFace");
    robo_font_name_info_release(info);
}

static void test_name_utf16_surrogate(void)
{
    uint8_t name[64];
    size_t nn = build_name_surrogate(name, sizeof(name));
    robo_font_span_t span = {name, nn};
    robo_font_name_info_t *info = NULL;
    const robo_font_metadata_t *meta;

    ROBO_CHECK_EQ_INT(robo_font_name_info_from_span(span, NULL, &info), ROBO_OK);
    meta = robo_font_name_info_metadata(info);
    ROBO_CHECK_EQ_INT((int)meta->full_name.avail, (int)ROBO_FONT_NAME_PRESENT);
    ROBO_CHECK(meta->full_name.utf8_len >= 4u); /* 4-byte UTF-8 emoji */
    robo_font_name_info_release(info);
}

static void test_name_malformed(void)
{
    uint8_t bad[] = {0x00, 0x00, 0xff, 0xff, 0x00, 0x06}; /* huge count */
    robo_font_span_t span = {bad, sizeof(bad)};
    robo_font_name_info_t *info = NULL;
    ROBO_CHECK(robo_font_name_info_from_span(span, NULL, &info) != ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_name_fuzz_parse(bad, sizeof(bad)), ROBO_OK);

    /* truncated UTF-16 */
    {
        uint8_t name[32];
        uint8_t *p = name;
        size_t storage_off = 6u + 12u;
        put_u16(&p, 0);
        put_u16(&p, 1);
        put_u16(&p, (uint16_t)storage_off);
        put_u16(&p, 3);
        put_u16(&p, 1);
        put_u16(&p, 0x0409);
        put_u16(&p, ROBO_FONT_NAME_ID_FAMILY);
        put_u16(&p, 3); /* odd length */
        put_u16(&p, 0);
        name[storage_off] = 0;
        name[storage_off + 1] = 'A';
        name[storage_off + 2] = 0;
        span.data = name;
        span.size = storage_off + 3u;
        /* parse may fail on odd length at record validation (length ok if storage fits)
         * length 3 with storage size 3 is OK structurally; decode → MALFORMED */
        if (robo_font_name_info_from_span(span, NULL, &info) == ROBO_OK) {
            const robo_font_name_entry_t *e = robo_font_name_info_entry(info, 0);
            ROBO_CHECK(e != NULL);
            ROBO_CHECK_EQ_INT((int)e->decode_status, (int)ROBO_FONT_NAME_MALFORMED);
            robo_font_name_info_release(info);
        }
    }
}

static void test_name_duplicates(void)
{
    /* two identical en-US family records — first wins among equal platform score */
    uint8_t name[128];
    uint8_t *p = name;
    uint8_t storage[16];
    uint8_t *s = storage;
    size_t storage_off = 6u + 24u;
    size_t i;
    robo_font_span_t span;
    robo_font_name_info_t *info = NULL;
    const robo_font_name_entry_t *ent = NULL;

    put_utf16be(&s, "One");
    put_utf16be(&s, "Two");
    put_u16(&p, 0);
    put_u16(&p, 2);
    put_u16(&p, (uint16_t)storage_off);
    for (i = 0u; i < 2u; i++) {
        put_u16(&p, 3);
        put_u16(&p, 1);
        put_u16(&p, 0x0409);
        put_u16(&p, ROBO_FONT_NAME_ID_FAMILY);
        put_u16(&p, 6);
        put_u16(&p, (uint16_t)(i * 6u));
    }
    memcpy(name + storage_off, storage, (size_t)(s - storage));
    span.data = name;
    span.size = storage_off + (size_t)(s - storage);
    ROBO_CHECK_EQ_INT(robo_font_name_info_from_span(span, NULL, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_name_info_lookup(info, ROBO_FONT_NAME_ID_FAMILY, NULL, &ent,
                                                 NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(ent->utf8, "One");
    robo_font_name_info_release(info);
}

static void test_name_unknown_id(void)
{
    uint8_t name[64];
    uint8_t *p = name;
    size_t storage_off = 6u + 12u;
    uint8_t bytes[] = {0x00, 'X'};
    robo_font_span_t span;
    robo_font_name_info_t *info = NULL;
    const robo_font_name_entry_t *ent = NULL;

    put_u16(&p, 0);
    put_u16(&p, 1);
    put_u16(&p, (uint16_t)storage_off);
    put_u16(&p, 3);
    put_u16(&p, 1);
    put_u16(&p, 0x0409);
    put_u16(&p, 300); /* font-specific / unknown */
    put_u16(&p, 2);
    put_u16(&p, 0);
    memcpy(name + storage_off, bytes, 2);
    span.data = name;
    span.size = storage_off + 2u;
    ROBO_CHECK_EQ_INT(robo_font_name_info_from_span(span, NULL, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_name_info_lookup(info, 300, NULL, &ent, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(ent->utf8, "X");
    robo_font_name_info_release(info);
}

void robo_test_register_font_name(void)
{
    robo_test_register("font.name.metadata_basic", test_name_metadata_basic);
    robo_test_register("font.name.mac_roman", test_name_mac_roman);
    robo_test_register("font.name.utf16_surrogate", test_name_utf16_surrogate);
    robo_test_register("font.name.malformed", test_name_malformed);
    robo_test_register("font.name.duplicates", test_name_duplicates);
    robo_test_register("font.name.unknown_id", test_name_unknown_id);
}
