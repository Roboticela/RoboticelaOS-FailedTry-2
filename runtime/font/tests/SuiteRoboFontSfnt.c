/*
 * RoboFont SFNT container suite (Goal 5.2).
 *
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void put_u16be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_u32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void write_search_fields(uint8_t *hdr, uint16_t num_tables)
{
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint16_t search;
    uint16_t shift;
    while ((uint16_t)(pow2 * 2u) <= num_tables) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    search = (uint16_t)(pow2 * 16u);
    shift = (uint16_t)(num_tables * 16u - search);
    put_u16be(hdr + 4, num_tables);
    put_u16be(hdr + 6, search);
    put_u16be(hdr + 8, entry);
    put_u16be(hdr + 10, shift);
}

typedef struct {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} synth_table_t;

/*
 * Build a minimal SFNT: header + directory + table payloads (4-byte padded gaps).
 * Returns total size written into @p out (must be large enough).
 */
/*
 * @p offset_base is added to every table offset (0 for single fonts; face
 * absolute base for TTC members, where offsets are file-absolute).
 */
static size_t build_sfnt(uint8_t *out,
                         size_t out_cap,
                         uint32_t version,
                         const synth_table_t *tables,
                         uint32_t n,
                         uint32_t offset_base)
{
    size_t dir_end;
    size_t cursor;
    size_t need;
    uint32_t i;

    dir_end = 12u + (size_t)n * 16u;
    need = dir_end;
    for (i = 0u; i < n; i++) {
        size_t padded = ((size_t)tables[i].length + 3u) & ~(size_t)3u;
        if (tables[i].length != 0u) {
            need += padded;
        }
    }
    if (need > out_cap) {
        return 0u;
    }
    memset(out, 0, need);
    put_u32be(out, version);
    write_search_fields(out, (uint16_t)n);

    cursor = dir_end;
    for (i = 0u; i < n; i++) {
        uint32_t offset;
        uint32_t checksum;
        size_t padded;
        uint8_t *rec = out + 12u + (size_t)i * 16u;

        offset = (uint32_t)cursor + offset_base;
        if (tables[i].length > 0u && tables[i].data) {
            memcpy(out + cursor, tables[i].data, tables[i].length);
        }
        checksum = robo_font_sfnt_checksum(
            tables[i].length ? (out + cursor) : NULL, tables[i].length);
        put_u32be(rec, tables[i].tag);
        put_u32be(rec + 4, checksum);
        put_u32be(rec + 8, offset);
        put_u32be(rec + 12, tables[i].length);
        padded = ((size_t)tables[i].length + 3u) & ~(size_t)3u;
        if (tables[i].length != 0u) {
            cursor += padded;
        }
    }
    return cursor;
}

static void test_binary_reader(void)
{
    static const uint8_t bytes[] = {0x01, 0x02, 0x03, 0x04, 0x80, 0x00, 0xFF, 0xFE,
                                    0xFD, 0xFC};
    robo_font_reader_t r;
    uint8_t u8 = 0;
    uint16_t u16 = 0;
    int16_t i16 = 0;
    uint32_t u32 = 0;
    int32_t i32 = 0;
    robo_font_span_t slice;

    robo_font_reader_init(&r, bytes, sizeof(bytes));
    ROBO_CHECK_EQ_INT(robo_font_read_u8(&r, &u8), ROBO_OK);
    ROBO_CHECK_EQ_INT(u8, 0x01);
    ROBO_CHECK_EQ_INT(robo_font_read_u16be(&r, &u16), ROBO_OK);
    ROBO_CHECK_EQ_INT(u16, 0x0203);
    ROBO_CHECK_EQ_INT(robo_font_read_u32be(&r, &u32), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)u32, (int)0x048000FFu);
    ROBO_CHECK_EQ_INT(robo_font_read_i16be(&r, &i16), ROBO_OK);
    ROBO_CHECK_EQ_INT(i16, (int16_t)0xFEFD);
    ROBO_CHECK_EQ_INT(robo_font_read_i32be(&r, &i32), ROBO_PARSE_ERROR); /* truncated */

    ROBO_CHECK_EQ_INT(robo_font_checked_range(10, 8, 3), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_font_checked_range(10, 8, 2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_checked_range(SIZE_MAX, SIZE_MAX - 1u, 3u),
                      ROBO_PARSE_ERROR);

    {
        robo_font_span_t parent = {bytes, sizeof(bytes)};
        ROBO_CHECK_EQ_INT(robo_font_span_slice(parent, 2, 4, &slice), ROBO_OK);
        ROBO_CHECK(slice.data == bytes + 2);
        ROBO_CHECK_EQ_INT((int)slice.size, 4);
        ROBO_CHECK_EQ_INT(robo_font_span_slice(parent, 8, 8, &slice), ROBO_PARSE_ERROR);
    }
}

static void test_sfnt_minimal_true(void)
{
    uint8_t buf[128];
    static const uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD};
    synth_table_t tables[1];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    const robo_font_sfnt_header_t *hdr;
    robo_font_span_t span;
    robo_font_sfnt_table_record_t rec;

    tables[0].tag = ROBO_FONT_TAG_CMAP;
    tables[0].data = payload;
    tables[0].length = 4u;
    n = build_sfnt(buf, sizeof(buf), ROBO_FONT_SFNT_VERSION_TRUE, tables, 1u, 0u);
    ROBO_CHECK(n >= 28u);

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, ROBO_FONT_SFNT_FLAGS_NONE, &sfnt),
                      ROBO_OK);
    hdr = robo_font_sfnt_header(sfnt);
    ROBO_CHECK(hdr != NULL);
    ROBO_CHECK_EQ_INT((int)hdr->sfnt_version, (int)ROBO_FONT_SFNT_VERSION_TRUE);
    ROBO_CHECK_EQ_INT(hdr->num_tables, 1);
    ROBO_CHECK(robo_font_sfnt_search_fields_consistent(hdr));
    ROBO_CHECK_EQ_INT((int)robo_font_sfnt_table_count(sfnt), 1);
    ROBO_CHECK(robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CMAP));
    ROBO_CHECK(!robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_HEAD));
    ROBO_CHECK_EQ_INT(robo_font_sfnt_find_table(sfnt, ROBO_FONT_TAG_CMAP, &rec), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)rec.length, 4);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_CMAP, &span), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)span.size, 4);
    ROBO_CHECK(memcmp(span.data, payload, 4) == 0);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_verify_table_checksum(sfnt, ROBO_FONT_TAG_CMAP),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_fuzz_parse(buf, n), ROBO_OK);

    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
}

static void test_sfnt_signatures(void)
{
    uint8_t buf[128];
    static const uint8_t payload[] = {1, 2, 3, 4};
    synth_table_t tables[1];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    uint32_t versions[3] = {ROBO_FONT_SFNT_VERSION_TRUE_TAG, ROBO_FONT_SFNT_VERSION_TYP1,
                            ROBO_FONT_SFNT_VERSION_OTTO};
    int i;

    tables[0].tag = ROBO_FONT_TAG_CFF;
    tables[0].data = payload;
    tables[0].length = 4u;

    for (i = 0; i < 3; i++) {
        n = build_sfnt(buf, sizeof(buf), versions[i], tables, 1u, 0u);
        ROBO_CHECK_EQ_INT(robo_font_source_create_memory_borrow(buf, n, &src), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_font_sfnt_header(sfnt)->sfnt_version, (int)versions[i]);
        robo_font_sfnt_release(sfnt);
        robo_font_source_release(src);
        src = NULL;
        sfnt = NULL;
    }
}

static void test_sfnt_multi_table_enum(void)
{
    uint8_t buf[256];
    static const uint8_t a[] = {0x11, 0x22, 0x33, 0x44};
    static const uint8_t b[] = {0x55, 0x66, 0x77};
    static const uint8_t c[] = {0x01};
    synth_table_t tables[3];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_sfnt_table_record_t rec;
    uint32_t i;
    char tagbuf[5];

    tables[0].tag = ROBO_FONT_TAG_NAME;
    tables[0].data = a;
    tables[0].length = 4u;
    tables[1].tag = ROBO_FONT_TAG_MAXP;
    tables[1].data = b;
    tables[1].length = 3u;
    tables[2].tag = ROBO_FONT_TAG_OS2;
    tables[2].data = c;
    tables[2].length = 1u;

    n = build_sfnt(buf, sizeof(buf), ROBO_FONT_SFNT_VERSION_TRUE, tables, 3u, 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_sfnt_parse(src, 0, ROBO_FONT_SFNT_FLAGS_VERIFY_CHECKSUMS, &sfnt),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_sfnt_table_count(sfnt), 3);

    /* Enumeration is sorted by tag. */
    for (i = 0u; i < 3u; i++) {
        ROBO_CHECK_EQ_INT(robo_font_sfnt_table_at(sfnt, i, &rec), ROBO_OK);
        if (i > 0u) {
            robo_font_sfnt_table_record_t prev;
            ROBO_CHECK_EQ_INT(robo_font_sfnt_table_at(sfnt, i - 1u, &prev), ROBO_OK);
            ROBO_CHECK(prev.tag < rec.tag);
        }
        ROBO_CHECK_EQ_INT(robo_font_tag_format(rec.tag, tagbuf, sizeof(tagbuf)), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)strlen(tagbuf), 4);
    }
    ROBO_CHECK_EQ_INT(robo_font_sfnt_table_at(sfnt, 3, &rec), ROBO_NOT_FOUND);
    ROBO_CHECK(robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_OS2));
    ROBO_CHECK_EQ_INT(robo_font_sfnt_checksum(b, 3),
                      ((uint32_t)0x55 << 24) | ((uint32_t)0x66 << 16) |
                          ((uint32_t)0x77 << 8));

    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
}

static void test_sfnt_zero_length_table(void)
{
    uint8_t buf[128];
    static const uint8_t payload[] = {9, 8, 7, 6};
    synth_table_t tables[2];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_span_t span;

    tables[0].tag = ROBO_FONT_TAG_LOCA;
    tables[0].data = NULL;
    tables[0].length = 0u;
    tables[1].tag = ROBO_FONT_TAG_GLYF;
    tables[1].data = payload;
    tables[1].length = 4u;

    n = build_sfnt(buf, sizeof(buf), ROBO_FONT_SFNT_VERSION_TRUE, tables, 2u, 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_borrow(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_LOCA, &span), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)span.size, 0);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_GLYF, &span), ROBO_OK);
    ROBO_CHECK(memcmp(span.data, payload, 4) == 0);

    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
}

static void test_sfnt_malformed(void)
{
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    uint8_t trunc[8] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x10};
    uint8_t bad_sig[12];
    uint8_t buf[128];
    static const uint8_t payload[] = {1, 2, 3, 4};
    synth_table_t tables[1];
    size_t n;

    /* Truncated header */
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_borrow(trunc, sizeof(trunc), &src),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_PARSE_ERROR);
    ROBO_CHECK(sfnt == NULL);
    robo_font_source_release(src);

    /* Invalid signature */
    memset(bad_sig, 0, sizeof(bad_sig));
    put_u32be(bad_sig, 0x12345678u);
    put_u16be(bad_sig + 4, 1);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_borrow(bad_sig, sizeof(bad_sig), &src),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_NOT_SUPPORTED);
    robo_font_source_release(src);

    /* Truncated directory (claims 2 tables, only room for header) */
    {
        uint8_t short_dir[12];
        memset(short_dir, 0, sizeof(short_dir));
        put_u32be(short_dir, ROBO_FONT_SFNT_VERSION_TRUE);
        put_u16be(short_dir + 4, 2);
        put_u16be(short_dir + 6, 32);
        put_u16be(short_dir + 8, 1);
        put_u16be(short_dir + 10, 0);
        ROBO_CHECK_EQ_INT(
            robo_font_source_create_memory_borrow(short_dir, sizeof(short_dir), &src),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_PARSE_ERROR);
        robo_font_source_release(src);
    }

    /* Excessive table count (directory would overflow source) */
    {
        uint8_t huge[12];
        memset(huge, 0, sizeof(huge));
        put_u32be(huge, ROBO_FONT_SFNT_VERSION_TRUE);
        put_u16be(huge + 4, 4095);
        ROBO_CHECK_EQ_INT(robo_font_source_create_memory_borrow(huge, sizeof(huge), &src),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_PARSE_ERROR);
        robo_font_source_release(src);
    }

    /* Table outside source */
    tables[0].tag = ROBO_FONT_TAG_HEAD;
    tables[0].data = payload;
    tables[0].length = 4u;
    n = build_sfnt(buf, sizeof(buf), ROBO_FONT_SFNT_VERSION_TRUE, tables, 1u, 0u);
    /* Corrupt length to extend past end */
    put_u32be(buf + 12 + 12, 0xFFFFFFFFu);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_PARSE_ERROR);
    robo_font_source_release(src);

    /* Unaligned offset */
    n = build_sfnt(buf, sizeof(buf), ROBO_FONT_SFNT_VERSION_TRUE, tables, 1u, 0u);
    put_u32be(buf + 12 + 8, 29u); /* not multiple of 4 */
    put_u32be(buf + 12 + 12, 1u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_PARSE_ERROR);
    robo_font_source_release(src);

    /* Duplicate tags */
    {
        synth_table_t dup[2];
        dup[0].tag = ROBO_FONT_TAG_CMAP;
        dup[0].data = payload;
        dup[0].length = 4u;
        dup[1].tag = ROBO_FONT_TAG_CMAP;
        dup[1].data = payload;
        dup[1].length = 4u;
        n = build_sfnt(buf, sizeof(buf), ROBO_FONT_SFNT_VERSION_TRUE, dup, 2u, 0u);
        ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_PARSE_ERROR);
        robo_font_source_release(src);
    }

    /* Overlapping regions */
    {
        synth_table_t ov[2];
        ov[0].tag = ROBO_FONT_TAG_HHEA;
        ov[0].data = payload;
        ov[0].length = 4u;
        ov[1].tag = ROBO_FONT_TAG_HMTX;
        ov[1].data = payload;
        ov[1].length = 4u;
        n = build_sfnt(buf, sizeof(buf), ROBO_FONT_SFNT_VERSION_TRUE, ov, 2u, 0u);
        {
            uint32_t o0 =
                ((uint32_t)buf[12 + 8] << 24) | ((uint32_t)buf[12 + 9] << 16) |
                ((uint32_t)buf[12 + 10] << 8) | (uint32_t)buf[12 + 11];
            put_u32be(buf + 12 + 16 + 8, o0);
            put_u32be(buf + 12 + 16 + 12, 4u);
        }
        ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_PARSE_ERROR);
        robo_font_source_release(src);
    }

    /* Checksum mismatch with verify flag */
    n = build_sfnt(buf, sizeof(buf), ROBO_FONT_SFNT_VERSION_TRUE, tables, 1u, 0u);
    put_u32be(buf + 12 + 4, 0xDEADBEEFu);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_sfnt_parse(src, 0, ROBO_FONT_SFNT_FLAGS_VERIFY_CHECKSUMS, &sfnt),
        ROBO_PARSE_ERROR);
    /* Without flag, still parses */
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, ROBO_FONT_SFNT_FLAGS_NONE, &sfnt),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_verify_table_checksum(sfnt, ROBO_FONT_TAG_HEAD),
                      ROBO_PARSE_ERROR);
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);

    /* Fuzz entry must not crash on garbage */
    ROBO_CHECK(robo_font_sfnt_fuzz_parse((const uint8_t *)"garbage!!", 9) != ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_fuzz_parse(NULL, 0), ROBO_INVALID_ARGUMENT);
}

static void test_sfnt_ttc_basic(void)
{
    /* ttcf + version 1.0 + 1 font + offset to SFNT */
    uint8_t buf[160];
    static const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    synth_table_t tables[1];
    size_t face_size;
    size_t face_off = 16u; /* after ttcf header + 1 offset */
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;

    memset(buf, 0, sizeof(buf));
    put_u32be(buf, ROBO_FONT_SFNT_VERSION_TTCF);
    put_u32be(buf + 4, 0x00010000u);
    put_u32be(buf + 8, 1u);
    put_u32be(buf + 12, (uint32_t)face_off);

    tables[0].tag = ROBO_FONT_TAG_POST;
    tables[0].data = payload;
    tables[0].length = 4u;
    /* Table offsets in a collection are absolute from the file start. */
    face_size = build_sfnt(buf + face_off, sizeof(buf) - face_off,
                           ROBO_FONT_SFNT_VERSION_TRUE, tables, 1u, (uint32_t)face_off);
    ROBO_CHECK(face_size > 0u);

    ROBO_CHECK_EQ_INT(
        robo_font_source_create_memory_copy(buf, face_off + face_size, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
    ROBO_CHECK(robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_POST));
    robo_font_sfnt_release(sfnt);
    sfnt = NULL;
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 1, 0, &sfnt), ROBO_NOT_FOUND);
    ROBO_CHECK(sfnt == NULL);
    robo_font_source_release(src);
}

static void test_sfnt_lifetime(void)
{
    uint8_t buf[128];
    static const uint8_t payload[] = {1, 2, 3, 4};
    synth_table_t tables[1];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;

    tables[0].tag = ROBO_FONT_TAG_GSUB;
    tables[0].data = payload;
    tables[0].length = 4u;
    n = build_sfnt(buf, sizeof(buf), ROBO_FONT_SFNT_VERSION_OTTO, tables, 1u, 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
    robo_font_source_release(src); /* SFNT still holds a ref */
    ROBO_CHECK(robo_font_sfnt_source(sfnt) != NULL);
    ROBO_CHECK_EQ_INT((int)robo_font_source_size(robo_font_sfnt_source(sfnt)), (int)n);
    robo_font_sfnt_retain(sfnt);
    robo_font_sfnt_release(sfnt);
    robo_font_sfnt_release(sfnt);
    robo_font_sfnt_release(NULL);
}

void robo_test_register_font_sfnt(void)
{
    robo_test_register("font.sfnt.binary_reader", test_binary_reader);
    robo_test_register("font.sfnt.minimal_true", test_sfnt_minimal_true);
    robo_test_register("font.sfnt.signatures", test_sfnt_signatures);
    robo_test_register("font.sfnt.multi_table", test_sfnt_multi_table_enum);
    robo_test_register("font.sfnt.zero_length", test_sfnt_zero_length_table);
    robo_test_register("font.sfnt.malformed", test_sfnt_malformed);
    robo_test_register("font.sfnt.ttc", test_sfnt_ttc_basic);
    robo_test_register("font.sfnt.lifetime", test_sfnt_lifetime);
}
