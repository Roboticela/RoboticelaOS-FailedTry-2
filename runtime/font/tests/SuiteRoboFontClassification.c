/*
 * RoboFont style / weight / stretch classification suite (Goal 5.10).
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboTest.h"

#include <string.h>

static void raw_clear(robo_font_class_raw_t *r)
{
    memset(r, 0, sizeof(*r));
}

static void test_weight_categories(void)
{
    ROBO_CHECK_EQ_INT(robo_font_weight_category(100), ROBO_FONT_WEIGHT_CAT_THIN);
    ROBO_CHECK_EQ_INT(robo_font_weight_category(200), ROBO_FONT_WEIGHT_CAT_EXTRA_LIGHT);
    ROBO_CHECK_EQ_INT(robo_font_weight_category(300), ROBO_FONT_WEIGHT_CAT_LIGHT);
    ROBO_CHECK_EQ_INT(robo_font_weight_category(400), ROBO_FONT_WEIGHT_CAT_NORMAL);
    ROBO_CHECK_EQ_INT(robo_font_weight_category(500), ROBO_FONT_WEIGHT_CAT_MEDIUM);
    ROBO_CHECK_EQ_INT(robo_font_weight_category(600), ROBO_FONT_WEIGHT_CAT_SEMI_BOLD);
    ROBO_CHECK_EQ_INT(robo_font_weight_category(700), ROBO_FONT_WEIGHT_CAT_BOLD);
    ROBO_CHECK_EQ_INT(robo_font_weight_category(800), ROBO_FONT_WEIGHT_CAT_EXTRA_BOLD);
    ROBO_CHECK_EQ_INT(robo_font_weight_category(900), ROBO_FONT_WEIGHT_CAT_BLACK);
    ROBO_CHECK_EQ_INT(robo_font_weight_category(1000), ROBO_FONT_WEIGHT_CAT_EXTRA_BLACK);
    ROBO_CHECK_EQ_INT(robo_font_weight_category(0), ROBO_FONT_WEIGHT_CAT_UNKNOWN);
    ROBO_CHECK_EQ_INT(robo_font_weight_category(1001), ROBO_FONT_WEIGHT_CAT_UNKNOWN);
}

static void test_weight_os2_and_bold_bit(void)
{
    robo_font_class_raw_t raw;
    robo_font_classification_t cls;

    raw_clear(&raw);
    raw.has_os2 = 1u;
    raw.us_weight_class = 350u;
    raw.us_width_class = 5u;
    raw.fs_selection = ROBO_FONT_FS_BOLD; /* must NOT override valid weight */
    ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
    ROBO_CHECK_EQ_INT(cls.weight, 350);
    ROBO_CHECK_EQ_INT(cls.weight_source, ROBO_FONT_CLASS_SRC_OS2);
    ROBO_CHECK_EQ_INT(cls.weight_avail, ROBO_FONT_CLASS_PRESENT);
    ROBO_CHECK_EQ_INT(cls.conflict_bold_bit_vs_weight, 1);

    raw_clear(&raw);
    raw.has_os2 = 1u;
    raw.us_weight_class = 0u; /* invalid */
    raw.us_width_class = 5u;
    raw.fs_selection = ROBO_FONT_FS_BOLD;
    ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
    ROBO_CHECK_EQ_INT(cls.weight, ROBO_FONT_WEIGHT_BOLD);
    ROBO_CHECK_EQ_INT(cls.weight_source, ROBO_FONT_CLASS_SRC_FS_SELECTION);
    ROBO_CHECK_EQ_INT(cls.weight_avail, ROBO_FONT_CLASS_FALLBACK);
    ROBO_CHECK_EQ_INT(cls.raw.us_weight_class, 0);

    raw_clear(&raw);
    raw.has_os2 = 1u;
    raw.us_weight_class = 2000u; /* invalid preserved */
    raw.us_width_class = 5u;
    ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
    ROBO_CHECK_EQ_INT(cls.raw.us_weight_class, 2000);
    ROBO_CHECK_EQ_INT(cls.weight, ROBO_FONT_WEIGHT_NORMAL);
    ROBO_CHECK_EQ_INT(cls.weight_avail, ROBO_FONT_CLASS_INVALID);
}

static void test_weight_head_fallback(void)
{
    robo_font_class_raw_t raw;
    robo_font_classification_t cls;

    raw_clear(&raw);
    raw.has_head = 1u;
    raw.mac_style = ROBO_FONT_MAC_BOLD;
    ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
    ROBO_CHECK_EQ_INT(cls.weight, ROBO_FONT_WEIGHT_BOLD);
    ROBO_CHECK_EQ_INT(cls.weight_source, ROBO_FONT_CLASS_SRC_HEAD);
}

static void test_stretch_classes(void)
{
    uint16_t wc;
    robo_font_class_raw_t raw;
    robo_font_classification_t cls;

    for (wc = 1u; wc <= 9u; wc++) {
        raw_clear(&raw);
        raw.has_os2 = 1u;
        raw.us_weight_class = 400u;
        raw.us_width_class = wc;
        ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
        ROBO_CHECK_EQ_INT(cls.width_class, (int)wc);
        ROBO_CHECK_EQ_INT(cls.stretch, robo_font_stretch_from_width_class(wc));
        ROBO_CHECK_EQ_INT(cls.stretch_avail, ROBO_FONT_CLASS_PRESENT);
    }

    raw_clear(&raw);
    raw.has_os2 = 1u;
    raw.us_weight_class = 400u;
    raw.us_width_class = 99u;
    ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
    ROBO_CHECK_EQ_INT(cls.raw.us_width_class, 99);
    ROBO_CHECK_EQ_INT(cls.stretch, ROBO_FONT_STRETCH_NORMAL);
    ROBO_CHECK_EQ_INT(cls.stretch_avail, ROBO_FONT_CLASS_INVALID);
    ROBO_CHECK_EQ_INT(cls.width_class, ROBO_FONT_WIDTH_UNKNOWN);
}

static void test_style_italic_oblique(void)
{
    robo_font_class_raw_t raw;
    robo_font_classification_t cls;

    raw_clear(&raw);
    raw.has_os2 = 1u;
    raw.us_weight_class = 400u;
    raw.us_width_class = 5u;
    raw.fs_selection = ROBO_FONT_FS_ITALIC;
    ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
    ROBO_CHECK_EQ_INT(cls.style, ROBO_FONT_STYLE_ITALIC);
    ROBO_CHECK_EQ_INT(cls.style_source, ROBO_FONT_CLASS_SRC_OS2);

    raw.fs_selection = ROBO_FONT_FS_OBLIQUE;
    ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
    ROBO_CHECK_EQ_INT(cls.style, ROBO_FONT_STYLE_OBLIQUE);

    raw.fs_selection = (uint16_t)(ROBO_FONT_FS_ITALIC | ROBO_FONT_FS_OBLIQUE);
    ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
    ROBO_CHECK_EQ_INT(cls.style, ROBO_FONT_STYLE_OBLIQUE);
    ROBO_CHECK_EQ_INT(cls.both_italic_and_oblique, 1);

    raw.fs_selection =
        (uint16_t)(ROBO_FONT_FS_ITALIC | ROBO_FONT_FS_REGULAR);
    ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
    ROBO_CHECK_EQ_INT(cls.style, ROBO_FONT_STYLE_ITALIC);
    ROBO_CHECK_EQ_INT(cls.conflict_regular_and_slope, 1);

    raw_clear(&raw);
    raw.has_post = 1u;
    raw.italic_angle_fixed = -786432; /* -12 degrees Fixed */
    ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
    ROBO_CHECK_EQ_INT(cls.style, ROBO_FONT_STYLE_OBLIQUE);
    ROBO_CHECK_EQ_INT(cls.style_source, ROBO_FONT_CLASS_SRC_POST);
    ROBO_CHECK_EQ_INT(cls.has_italic_angle, 1);
    ROBO_CHECK(cls.italic_angle_millidegrees < 0);
}

static void test_wws_flag_and_name_layers(void)
{
    robo_font_class_raw_t raw;
    robo_font_classification_t cls;
    robo_font_metadata_t meta;

    raw_clear(&raw);
    raw.has_os2 = 1u;
    raw.us_weight_class = 400u;
    raw.us_width_class = 5u;
    raw.fs_selection = ROBO_FONT_FS_WWS;
    ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
    ROBO_CHECK_EQ_INT(cls.wws_flag, 1);

    memset(&meta, 0, sizeof(meta));
    meta.family_name.avail = ROBO_FONT_NAME_PRESENT;
    meta.subfamily_name.avail = ROBO_FONT_NAME_PRESENT;
    meta.typographic_family_name.avail = ROBO_FONT_NAME_PRESENT;
    meta.wws_family_name.avail = ROBO_FONT_NAME_PRESENT;
    meta.wws_subfamily_name.avail = ROBO_FONT_NAME_PRESENT;

    {
        robo_font_classification_info_t *info = NULL;
        /* Build via classify after manually filling name flags: */
        raw.has_name_meta = 1u;
        raw.name_has_legacy_family = 1u;
        raw.name_has_legacy_subfamily = 1u;
        raw.name_has_typo_family = 1u;
        raw.name_has_wws_family = 1u;
        raw.name_has_wws_subfamily = 1u;
        ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
        ROBO_CHECK_EQ_INT(cls.raw.name_has_legacy_family, 1);
        ROBO_CHECK_EQ_INT(cls.raw.name_has_typo_family, 1);
        ROBO_CHECK_EQ_INT(cls.raw.name_has_wws_family, 1);
        ROBO_CHECK_EQ_INT(cls.raw.name_has_wws_subfamily, 1);
        (void)info;
        (void)meta;
    }
}

static void test_stat_axis_tags_retained(void)
{
    /* Structural STAT axes do not override OS/2 numerics. */
    robo_font_class_raw_t raw;
    robo_font_classification_t cls;

    raw_clear(&raw);
    raw.has_os2 = 1u;
    raw.us_weight_class = 450u;
    raw.us_width_class = 7u;
    raw.has_stat = 1u;
    raw.stat_axis_count = 3u;
    raw.stat_has_wght = 1u;
    raw.stat_has_wdth = 1u;
    raw.stat_has_ital = 1u;
    ROBO_CHECK_EQ_INT(robo_font_classify(&raw, &cls), ROBO_OK);
    ROBO_CHECK_EQ_INT(cls.weight, 450);
    ROBO_CHECK_EQ_INT(cls.weight_source, ROBO_FONT_CLASS_SRC_OS2);
    ROBO_CHECK_EQ_INT(cls.width_class, ROBO_FONT_WIDTH_EXPANDED);
    ROBO_CHECK_EQ_INT(cls.raw.stat_has_wght, 1);
    ROBO_CHECK_EQ_INT(cls.raw.stat_has_wdth, 1);
    ROBO_CHECK_EQ_INT(cls.raw.stat_has_ital, 1);
}

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

static void put_i64(uint8_t **p, int64_t v)
{
    put_u32(p, (uint32_t)((uint64_t)v >> 32));
    put_u32(p, (uint32_t)((uint64_t)v));
}

static size_t build_head_ok(uint8_t *buf, uint16_t mac_style)
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
    put_u16(&p, mac_style);
    put_u16(&p, 9);
    put_i16(&p, 2);
    put_i16(&p, 0); /* short loca */
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
    put_u16(&p, 1); /* numGlyphs */
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

static size_t build_glyf(uint8_t *b)
{
    (void)b;
    return 0u;
}

static size_t build_post(uint8_t *b, int32_t angle_fixed)
{
    uint8_t *p = b;
    put_u32(&p, 0x00030000u);
    put_u32(&p, (uint32_t)angle_fixed);
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
    put_u16(&p, 4); /* version with OBLIQUE bit defined */
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

static size_t build_stat_wght(uint8_t *buf)
{
    uint8_t *p = buf;
    /* version 1.1 header + 1 axis wght */
    put_u16(&p, 1);
    put_u16(&p, 1);
    put_u16(&p, 8); /* designAxisSize */
    put_u16(&p, 1); /* designAxisCount */
    put_u32(&p, 20); /* offset to axes from table start */
    put_u16(&p, 0); /* axisValueCount */
    put_u32(&p, 0); /* offsetToAxisValueOffsets */
    put_u16(&p, 2); /* elidedFallbackNameID */
    /* axis at offset 20 */
    put_u32(&p, ROBO_FONT_TAG('w', 'g', 'h', 't'));
    put_u16(&p, 256);
    put_u16(&p, 0);
    return (size_t)(p - buf);
}

static void test_face_classification_integration(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[8], loca[8], post[64], os2[128], stat[64];
    uint8_t font[512];
    size_t n;
    size_t nh, nhea, nm, nhm, nl, np, no, ns;
    synth_table_t tables[8];
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    const robo_font_classification_t *cls;

    nh = build_head_ok(head, 0);
    nhea = build_hhea(hhea);
    nm = build_maxp(maxp);
    nhm = build_hmtx(hmtx);
    nl = build_loca(loca);
    np = build_post(post, 0);
    no = build_os2(os2, 600, 3, ROBO_FONT_FS_ITALIC);
    ns = build_stat_wght(stat);

    tables[0].tag = ROBO_FONT_TAG_HEAD;
    tables[0].data = head;
    tables[0].length = (uint32_t)nh;
    tables[1].tag = ROBO_FONT_TAG_HHEA;
    tables[1].data = hhea;
    tables[1].length = (uint32_t)nhea;
    tables[2].tag = ROBO_FONT_TAG_MAXP;
    tables[2].data = maxp;
    tables[2].length = (uint32_t)nm;
    tables[3].tag = ROBO_FONT_TAG_HMTX;
    tables[3].data = hmtx;
    tables[3].length = (uint32_t)nhm;
    tables[4].tag = ROBO_FONT_TAG_LOCA;
    tables[4].data = loca;
    tables[4].length = (uint32_t)nl;
    tables[5].tag = ROBO_FONT_TAG_GLYF;
    tables[5].data = head; /* empty glyf: length 0 */
    tables[5].length = 0u;
    tables[6].tag = ROBO_FONT_TAG_POST;
    tables[6].data = post;
    tables[6].length = (uint32_t)np;
    tables[7].tag = ROBO_FONT_TAG_OS2;
    tables[7].data = os2;
    tables[7].length = (uint32_t)no;

    n = build_sfnt(font, sizeof(font), 0x00010000u, tables, 8u);
    ROBO_CHECK(n > 0u);

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_classification(face), ROBO_OK);
    cls = robo_font_face_classification(face);
    ROBO_CHECK(cls != NULL);
    ROBO_CHECK_EQ_INT(cls->weight, 600);
    ROBO_CHECK_EQ_INT(cls->weight_category, ROBO_FONT_WEIGHT_CAT_SEMI_BOLD);
    ROBO_CHECK_EQ_INT(cls->width_class, ROBO_FONT_WIDTH_CONDENSED);
    ROBO_CHECK_EQ_INT(cls->style, ROBO_FONT_STYLE_ITALIC);
    ROBO_CHECK_EQ_INT(cls->raw.us_weight_class, 600);
    ROBO_CHECK_EQ_INT(cls->raw.us_width_class, 3);

    /* Add STAT in a second font to verify axis retention without override */
    {
        synth_table_t t2[9];
        uint8_t font2[640];
        size_t n2;
        memcpy(t2, tables, sizeof(tables));
        t2[8].tag = ROBO_FONT_TAG_STAT;
        t2[8].data = stat;
        t2[8].length = (uint32_t)ns;
        n2 = build_sfnt(font2, sizeof(font2), 0x00010000u, t2, 9u);
        ROBO_CHECK(n2 > 0u);
        robo_font_source_release(src);
        robo_font_face_release(face);
        src = NULL;
        face = NULL;
        ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font2, n2, &src), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_face_parse_classification(face), ROBO_OK);
        cls = robo_font_face_classification(face);
        ROBO_CHECK(cls != NULL);
        ROBO_CHECK_EQ_INT(cls->weight, 600);
        ROBO_CHECK_EQ_INT(cls->raw.stat_has_wght, 1);
        ROBO_CHECK_EQ_INT(cls->weight_source, ROBO_FONT_CLASS_SRC_OS2);
    }

    robo_font_face_release(face);
    robo_font_source_release(src);
    (void)build_glyf;
}

static void test_fuzz_classification(void)
{
    uint8_t junk[64];
    size_t i;
    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 37u);
    }
    ROBO_CHECK_EQ_INT(robo_font_classification_fuzz_parse(junk, sizeof(junk)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_classification_fuzz_parse(junk, 0u), ROBO_OK);
}

void robo_test_register_font_classification(void)
{
    robo_test_register("font.class.weight_categories", test_weight_categories);
    robo_test_register("font.class.weight_os2_bold", test_weight_os2_and_bold_bit);
    robo_test_register("font.class.weight_head", test_weight_head_fallback);
    robo_test_register("font.class.stretch", test_stretch_classes);
    robo_test_register("font.class.style", test_style_italic_oblique);
    robo_test_register("font.class.wws_name", test_wws_flag_and_name_layers);
    robo_test_register("font.class.stat_axes", test_stat_axis_tags_retained);
    robo_test_register("font.class.face", test_face_classification_integration);
    robo_test_register("font.class.fuzz", test_fuzz_classification);
}
