/*
 * RoboFont validation & security suite (Goal 5.20).
 * Synthetic SFNT fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
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

/* ---- Security regression corpus (permanent regressions) ---- */

static const uint8_t k_regress_empty[] = {0u};

static const uint8_t k_regress_four_zeros[4] = {0u, 0u, 0u, 0u};

static const uint8_t k_regress_truncated_sfnt[8] = {
    0x00, 0x01, 0x00, 0x00, /* TrueType signature */
    0x00, 0x01,              /* numTables = 1 */
    0x00, 0x10               /* truncated directory */
};

/* Two directory entries claim the same payload offset (overlap). */
static const uint8_t k_regress_overlap_dir[] = {
    0x00, 0x01, 0x00, 0x00,
    0x00, 0x02,
    0x00, 0x20, 0x00, 0x00,
    0x00, 0x01,
    0x00, 0x00,
    'A', 'A', 'A', 'A', 0, 0, 0, 0, 0, 0, 0, 28, 0, 0, 0, 8,
    'B', 'B', 'B', 'B', 0, 0, 0, 0, 0, 0, 0, 28, 0, 0, 0, 8,
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE
};

/* numTables claims 255 but directory is truncated. */
static const uint8_t k_regress_huge_table_count[16] = {
    0x00, 0x01, 0x00, 0x00,
    0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00
};

/* ---- Binary helpers ---- */

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

/* Synthetic builder — directory checksums left zero (typical for tests). */
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
        if (tables[i].length) {
            memcpy(out + cursor, tables[i].data, tables[i].length);
        }
        cursor += pad;
    }
    return cursor;
}

/* Builder with correct per-table checksums (for checksum policy tests). */
static size_t build_sfnt_checked(uint8_t *out, size_t out_cap, const synth_table_t *tables,
                                 uint32_t n)
{
    size_t dir_end;
    size_t cursor;
    size_t need;
    uint32_t i;

    dir_end = 12u + (size_t)n * 16u;
    need = dir_end;
    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > out_cap) {
        return 0u;
    }
    memset(out, 0, need);
    {
        uint8_t *hp = out;
        put_u32(&hp, ROBO_FONT_SFNT_VERSION_TRUE);
    }
    write_search_fields(out, (uint16_t)n);

    cursor = dir_end;
    for (i = 0u; i < n; i++) {
        uint32_t checksum;
        uint32_t pad;
        uint8_t *rec = out + 12u + (size_t)i * 16u;
        uint8_t *p = rec;

        if (tables[i].length > 0u && tables[i].data) {
            memcpy(out + cursor, tables[i].data, tables[i].length);
        }
        checksum = robo_font_sfnt_checksum(
            tables[i].length ? (out + cursor) : NULL, tables[i].length);
        put_u32(&p, tables[i].tag);
        put_u32(&p, checksum);
        put_u32(&p, (uint32_t)cursor);
        put_u32(&p, tables[i].length);
        pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
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
        put_i16(&p, 0);
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

static size_t build_cmap_fmt4_ascii(uint8_t *buf, uint16_t cp, uint16_t gid)
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

static size_t build_cpal_2x2(uint8_t *buf)
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

static size_t build_colr_v0_layers(uint8_t *buf)
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

typedef struct {
    const char *family;
    const char *ps;
    uint16_t num_glyphs;
    int with_color;
} minimal_font_spec_t;

static size_t build_minimal_font(uint8_t *out, size_t cap, const minimal_font_spec_t *spec)
{
    uint8_t head[64];
    uint8_t hhea[64];
    uint8_t maxp[64];
    uint8_t hmtx[256];
    uint8_t loca[256];
    uint8_t post[64];
    uint8_t os2[128];
    uint8_t name[512];
    uint8_t cmap[256];
    uint8_t cpal[64];
    uint8_t colr[64];
    synth_table_t tabs[16];
    uint32_t nt = 0u;
    size_t nn;
    size_t nc;
    uint16_t glyphs;

    if (!spec || spec->num_glyphs < 1u) {
        return 0u;
    }
    glyphs = spec->num_glyphs;
    nn = build_name_table(name, sizeof(name), spec->family, spec->ps);
    nc = build_cmap_fmt4_ascii(cmap, 0x41u, 1u);
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

    if (spec->with_color) {
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_CPAL, cpal,
                                      (uint32_t)build_cpal_2x2(cpal)};
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_COLR, colr,
                                      (uint32_t)build_colr_v0_layers(colr)};
    }
    return build_sfnt(out, cap, tabs, nt);
}

static void load_face(const uint8_t *font, size_t n, robo_font_source_t **src,
                      robo_font_face_t **face)
{
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(*src, 0u, face), ROBO_OK);
}

static int find_issue_code(const robo_font_validation_result_t *result,
                           robo_font_validation_issue_code_t code,
                           robo_font_validation_issue_t *out)
{
    uint32_t i;
    robo_font_validation_issue_t issue;

    if (!result) {
        return 0;
    }
    for (i = 0u; i < robo_font_validation_result_issue_count(result); i++) {
        if (robo_font_validation_result_issue(result, i, &issue) != ROBO_OK) {
            continue;
        }
        if (issue.code == code) {
            if (out) {
                *out = issue;
            }
            return 1;
        }
    }
    return 0;
}

static int issue_has_fatal_code(const robo_font_validation_result_t *result,
                                robo_font_validation_issue_code_t code)
{
    uint32_t i;
    robo_font_validation_issue_t issue;

    if (!result) {
        return 0;
    }
    for (i = 0u; i < robo_font_validation_result_issue_count(result); i++) {
        if (robo_font_validation_result_issue(result, i, &issue) != ROBO_OK) {
            continue;
        }
        if (issue.code == code &&
            issue.severity == ROBO_FONT_VALIDATION_SEVERITY_FATAL) {
            return 1;
        }
    }
    return 0;
}

/* ---- Tests ---- */

static void test_policy_limits(void)
{
    robo_font_validation_policy_t pol;
    robo_font_validation_limits_t lim;
    const char *s;

    robo_font_validation_policy_init(&pol);
    ROBO_CHECK_EQ_INT((int)pol.level, (int)ROBO_FONT_VALIDATION_BASIC);
    ROBO_CHECK_EQ_INT((int)pol.checksum_policy, (int)ROBO_FONT_CHECKSUM_WARN);
    ROBO_CHECK(pol.reject_oversized != 0u);
    ROBO_CHECK(pol.stop_on_fatal != 0u);

    memset(&lim, 0, sizeof(lim));
    lim.max_source_size = UINT64_MAX;
    lim.max_tables = UINT32_MAX;
    lim.max_glyphs = UINT32_MAX;
    lim.max_allocation = UINT32_MAX;
    lim.max_work_budget = UINT32_MAX;
    robo_font_validation_limits_clamp(&lim);
    ROBO_CHECK(lim.max_source_size <= ROBO_FONT_VALIDATION_HARD_MAX_SOURCE_SIZE);
    ROBO_CHECK(lim.max_tables <= ROBO_FONT_VALIDATION_HARD_MAX_TABLES);
    ROBO_CHECK(lim.max_glyphs <= ROBO_FONT_VALIDATION_HARD_MAX_GLYPHS);
    ROBO_CHECK(lim.max_allocation <= ROBO_FONT_VALIDATION_HARD_MAX_ALLOC);
    ROBO_CHECK(lim.max_work_budget <= ROBO_FONT_VALIDATION_HARD_MAX_WORK);
    ROBO_CHECK(lim.max_tables >= 1u);
    ROBO_CHECK(lim.max_glyphs >= 1u);

    ROBO_CHECK_EQ_INT((int)robo_font_validation_level_from_loader(ROBO_FONT_VALIDATE_TRUSTED),
                      (int)ROBO_FONT_VALIDATION_NONE);
    ROBO_CHECK_EQ_INT((int)robo_font_validation_level_from_loader(ROBO_FONT_VALIDATE_NORMAL),
                      (int)ROBO_FONT_VALIDATION_BASIC);
    ROBO_CHECK_EQ_INT((int)robo_font_validation_level_from_loader(ROBO_FONT_VALIDATE_STRICT),
                      (int)ROBO_FONT_VALIDATION_STRICT);
    ROBO_CHECK_EQ_INT((int)robo_font_validation_level_from_loader(ROBO_FONT_VALIDATE_PARANOID),
                      (int)ROBO_FONT_VALIDATION_PARANOID);

    robo_font_validation_policy_from_loader(ROBO_FONT_VALIDATE_PARANOID, &pol);
    ROBO_CHECK_EQ_INT((int)pol.level, (int)ROBO_FONT_VALIDATION_PARANOID);
    ROBO_CHECK_EQ_INT((int)pol.checksum_policy, (int)ROBO_FONT_CHECKSUM_REJECT);

    ROBO_CHECK_EQ_INT((int)robo_font_validation_level_to_loader(ROBO_FONT_VALIDATION_PARANOID),
                      (int)ROBO_FONT_VALIDATE_PARANOID);

    s = robo_font_validation_issue_code_string(ROBO_FONT_VALIDATION_CHECKSUM_MISMATCH);
    ROBO_CHECK(s != NULL);
    ROBO_CHECK(strcmp(s, "checksum_mismatch") == 0);
    s = robo_font_validation_severity_string(ROBO_FONT_VALIDATION_SEVERITY_FATAL);
    ROBO_CHECK(s != NULL);
    ROBO_CHECK(strcmp(s, "fatal") == 0);
    s = robo_font_validation_stage_string(ROBO_FONT_VALIDATION_STAGE_CONTAINER);
    ROBO_CHECK(s != NULL);
    ROBO_CHECK(strcmp(s, "container") == 0);
}

static void test_valid_sfnt(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_validation_policy_t pol;
    robo_font_validation_result_t *result = NULL;
    minimal_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ValidateSans";
    spec.ps = "ValidateSans";
    spec.num_glyphs = 2u;
    n = build_minimal_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);

    robo_font_validation_policy_init(&pol);
    pol.level = ROBO_FONT_VALIDATION_BASIC;
    pol.checksum_policy = ROBO_FONT_CHECKSUM_ACCEPT;

    ROBO_CHECK_EQ_INT(robo_font_validate_source(src, 0u, &pol, &result, NULL), ROBO_OK);
    ROBO_CHECK(result != NULL);
    ROBO_CHECK(robo_font_validation_result_ok(result));
    ROBO_CHECK(!robo_font_validation_result_has_fatal(result));
    robo_font_validation_result_release(result);
    result = NULL;

    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_validate_face(face, &pol, &result), ROBO_OK);
    ROBO_CHECK(result != NULL);
    ROBO_CHECK(robo_font_validation_result_ok(result));
    ROBO_CHECK(!robo_font_validation_result_has_fatal(result));

    robo_font_validation_result_release(result);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_bad_signature(void)
{
    static const uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33};
    robo_font_source_t *src = NULL;
    robo_font_validation_policy_t pol;
    robo_font_validation_result_t *result = NULL;

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(garbage, sizeof(garbage), &src),
                      ROBO_OK);
    robo_font_validation_policy_init(&pol);
    pol.level = ROBO_FONT_VALIDATION_BASIC;

    ROBO_CHECK_EQ_INT(robo_font_validate_source(src, 0u, &pol, &result, NULL),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK(result != NULL);
    ROBO_CHECK(robo_font_validation_result_has_fatal(result));
    ROBO_CHECK(!robo_font_validation_result_ok(result));
    ROBO_CHECK(find_issue_code(result, ROBO_FONT_VALIDATION_INVALID_SIGNATURE, NULL) ||
               find_issue_code(result, ROBO_FONT_VALIDATION_PARSE_FAILED, NULL));

    robo_font_validation_result_release(result);
    robo_font_source_release(src);
}

static void test_table_count(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_validation_policy_t pol;
    robo_font_validation_result_t *result = NULL;
    minimal_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "TableLimit";
    spec.ps = "TableLimit";
    spec.num_glyphs = 2u;
    n = build_minimal_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);

    robo_font_validation_policy_init(&pol);
    pol.level = ROBO_FONT_VALIDATION_BASIC;
    pol.checksum_policy = ROBO_FONT_CHECKSUM_ACCEPT;
    pol.limits.max_tables = 1u;
    robo_font_validation_limits_clamp(&pol.limits);

    ROBO_CHECK_EQ_INT(robo_font_validate_source(src, 0u, &pol, &result, NULL),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK(result != NULL);
    ROBO_CHECK(robo_font_validation_result_has_fatal(result));
    ROBO_CHECK(issue_has_fatal_code(result, ROBO_FONT_VALIDATION_TABLE_COUNT) ||
               issue_has_fatal_code(result, ROBO_FONT_VALIDATION_SOURCE_TOO_LARGE));

    robo_font_validation_result_release(result);
    robo_font_source_release(src);
}

static void test_checksum(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_validation_policy_t warn_pol;
    robo_font_validation_policy_t reject_pol;
    robo_font_validation_result_t *warn_result = NULL;
    robo_font_validation_result_t *reject_result = NULL;
    robo_font_validation_issue_t issue;
    minimal_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ChecksumTest";
    spec.ps = "ChecksumTest";
    spec.num_glyphs = 2u;
    n = build_minimal_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);

    robo_font_validation_policy_init(&warn_pol);
    warn_pol.level = ROBO_FONT_VALIDATION_BASIC;
    warn_pol.checksum_policy = ROBO_FONT_CHECKSUM_WARN;

    ROBO_CHECK_EQ_INT(robo_font_validate_source(src, 0u, &warn_pol, &warn_result, NULL),
                      ROBO_OK);
    ROBO_CHECK(warn_result != NULL);
    ROBO_CHECK(robo_font_validation_result_ok(warn_result));
    ROBO_CHECK(find_issue_code(warn_result, ROBO_FONT_VALIDATION_CHECKSUM_MISMATCH, &issue));
    ROBO_CHECK_EQ_INT((int)issue.severity, (int)ROBO_FONT_VALIDATION_SEVERITY_WARNING);
    ROBO_CHECK(strcmp(robo_font_validation_issue_code_string(issue.code),
                      "checksum_mismatch") == 0);
    robo_font_validation_result_release(warn_result);
    warn_result = NULL;

    robo_font_validation_policy_init(&reject_pol);
    reject_pol.level = ROBO_FONT_VALIDATION_BASIC;
    reject_pol.checksum_policy = ROBO_FONT_CHECKSUM_REJECT;

    ROBO_CHECK_EQ_INT(
        robo_font_validate_source(src, 0u, &reject_pol, &reject_result, NULL),
        ROBO_PARSE_ERROR);
    ROBO_CHECK(reject_result != NULL);
    ROBO_CHECK(robo_font_validation_result_has_fatal(reject_result));
    ROBO_CHECK(find_issue_code(reject_result, ROBO_FONT_VALIDATION_CHECKSUM_MISMATCH,
                               &issue));
    ROBO_CHECK_EQ_INT((int)issue.severity, (int)ROBO_FONT_VALIDATION_SEVERITY_FATAL);

    robo_font_validation_result_release(reject_result);
    robo_font_source_release(src);

    /* Deliberately wrong directory checksum on otherwise valid table bytes. */
    {
        static const uint8_t payload[] = {0x11, 0x22, 0x33, 0x44};
        synth_table_t tabs[1];
        uint8_t checked[128];
        size_t cn;

        tabs[0].tag = ROBO_FONT_TAG_CMAP;
        tabs[0].data = payload;
        tabs[0].length = 4u;
        cn = build_sfnt_checked(checked, sizeof(checked), tabs, 1u);
        ROBO_CHECK(cn > 0u);
        checked[16] ^= 0xFFu; /* corrupt directory checksum field */

        ROBO_CHECK_EQ_INT(
            robo_font_source_create_memory_copy(checked, cn, &src), ROBO_OK);
        robo_font_validation_policy_init(&reject_pol);
        reject_pol.level = ROBO_FONT_VALIDATION_BASIC;
        reject_pol.checksum_policy = ROBO_FONT_CHECKSUM_REJECT;
        ROBO_CHECK_EQ_INT(
            robo_font_validate_source(src, 0u, &reject_pol, &reject_result, NULL),
            ROBO_PARSE_ERROR);
        ROBO_CHECK(find_issue_code(reject_result, ROBO_FONT_VALIDATION_CHECKSUM_MISMATCH,
                                   NULL));
        robo_font_validation_result_release(reject_result);
        robo_font_source_release(src);
        src = NULL;
    }
}

static void test_glyph_limit(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_validation_policy_t pol;
    robo_font_validation_result_t *result = NULL;
    minimal_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "GlyphLimit";
    spec.ps = "GlyphLimit";
    spec.num_glyphs = 2u;
    n = build_minimal_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);

    robo_font_validation_policy_init(&pol);
    pol.level = ROBO_FONT_VALIDATION_BASIC;
    pol.checksum_policy = ROBO_FONT_CHECKSUM_ACCEPT;
    pol.limits.max_glyphs = 1u;
    robo_font_validation_limits_clamp(&pol.limits);

    ROBO_CHECK_EQ_INT(robo_font_validate_source(src, 0u, &pol, &result, NULL),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK(result != NULL);
    ROBO_CHECK(robo_font_validation_result_has_fatal(result));
    ROBO_CHECK(issue_has_fatal_code(result, ROBO_FONT_VALIDATION_GLYPH_COUNT));

    robo_font_validation_result_release(result);
    robo_font_source_release(src);
}

static void test_loader(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_validation_result_t *result = NULL;
    minimal_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "LoaderValidate";
    spec.ps = "LoaderValidate";
    spec.num_glyphs = 2u;
    n = build_minimal_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    load_face(font, n, &src, &face);

    ROBO_CHECK_EQ_INT(robo_font_validate_for_load(face, ROBO_FONT_VALIDATE_NORMAL, &result),
                      ROBO_OK);
    ROBO_CHECK(result != NULL);
    ROBO_CHECK(robo_font_validation_result_ok(result));
    robo_font_validation_result_release(result);
    result = NULL;

    /* PARANOID may warn on synthetic checksums — must not crash. */
    (void)robo_font_validate_for_load(face, ROBO_FONT_VALIDATE_PARANOID, &result);
    if (result) {
        robo_font_validation_result_release(result);
        result = NULL;
    }

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_color_paranoid(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_validation_policy_t pol;
    robo_font_validation_result_t *result = NULL;
    minimal_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ColorParanoid";
    spec.ps = "ColorParanoid";
    spec.num_glyphs = 2u;
    spec.with_color = 1;
    n = build_minimal_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    load_face(font, n, &src, &face);

    robo_font_validation_policy_init(&pol);
    pol.level = ROBO_FONT_VALIDATION_PARANOID;
    pol.checksum_policy = ROBO_FONT_CHECKSUM_WARN;

    ROBO_CHECK_EQ_INT(robo_font_validate_face(face, &pol, &result), ROBO_OK);
    ROBO_CHECK(result != NULL);
    ROBO_CHECK(!robo_font_validation_result_has_fatal(result));

    robo_font_validation_result_release(result);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_corpus(void)
{
    static const struct {
        const uint8_t *data;
        size_t size;
    } blobs[] = {
        {k_regress_empty, sizeof(k_regress_empty)},
        {k_regress_four_zeros, sizeof(k_regress_four_zeros)},
        {k_regress_truncated_sfnt, sizeof(k_regress_truncated_sfnt)},
        {k_regress_overlap_dir, sizeof(k_regress_overlap_dir)},
        {k_regress_huge_table_count, sizeof(k_regress_huge_table_count)},
    };
    robo_font_validation_policy_t pol;
    uint32_t i;
    int any_not_ok = 0;

    robo_font_validation_policy_init(&pol);
    pol.level = ROBO_FONT_VALIDATION_BASIC;
    pol.checksum_policy = ROBO_FONT_CHECKSUM_ACCEPT;

    for (i = 0u; i < (uint32_t)(sizeof(blobs) / sizeof(blobs[0])); i++) {
        robo_font_source_t *src = NULL;
        robo_font_validation_result_t *result = NULL;

        ROBO_CHECK_EQ_INT(robo_font_validate_fuzz_parse(blobs[i].data, blobs[i].size),
                          ROBO_OK);

        if (robo_font_source_create_memory_copy(blobs[i].data, blobs[i].size, &src) ==
            ROBO_OK) {
            (void)robo_font_validate_source(src, 0u, &pol, &result, NULL);
            if (result) {
                if (!robo_font_validation_result_ok(result)) {
                    any_not_ok = 1;
                }
                robo_font_validation_result_release(result);
            }
            robo_font_source_release(src);
        }
    }
    ROBO_CHECK(any_not_ok);
}

static void test_diagnostics(void)
{
    static const uint8_t garbage[] = {0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02, 0x03, 0x04};
    robo_font_source_t *src = NULL;
    robo_font_validation_policy_t pol;
    robo_font_validation_result_t *result_a = NULL;
    robo_font_validation_result_t *result_b = NULL;
    robo_font_validation_issue_t issue_a;
    robo_font_validation_issue_t issue_b;
    const char *code_s;
    const char *sev_s;
    const char *stage_s;

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(garbage, sizeof(garbage), &src),
                      ROBO_OK);
    robo_font_validation_policy_init(&pol);
    pol.level = ROBO_FONT_VALIDATION_BASIC;

    ROBO_CHECK_EQ_INT(robo_font_validate_source(src, 0u, &pol, &result_a, NULL),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK(result_a != NULL);
    ROBO_CHECK(robo_font_validation_result_issue_count(result_a) >= 1u);
    ROBO_CHECK_EQ_INT(robo_font_validation_result_issue(result_a, 0u, &issue_a), ROBO_OK);

    code_s = robo_font_validation_issue_code_string(issue_a.code);
    sev_s = robo_font_validation_severity_string(issue_a.severity);
    stage_s = robo_font_validation_stage_string(issue_a.stage);
    ROBO_CHECK(code_s != NULL && code_s[0] != '\0');
    ROBO_CHECK(sev_s != NULL && sev_s[0] != '\0');
    ROBO_CHECK(stage_s != NULL && stage_s[0] != '\0');
    ROBO_CHECK_EQ_INT((int)issue_a.severity, (int)ROBO_FONT_VALIDATION_SEVERITY_FATAL);

    ROBO_CHECK_EQ_INT(robo_font_validate_source(src, 0u, &pol, &result_b, NULL),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_font_validation_result_issue(result_b, 0u, &issue_b), ROBO_OK);
    ROBO_CHECK(strcmp(issue_a.message, issue_b.message) == 0);

    robo_font_validation_result_release(result_a);
    robo_font_validation_result_release(result_b);
    robo_font_source_release(src);
}

static void test_fuzz(void)
{
    uint8_t font[4096];
    uint8_t patterned[512];
    size_t n;
    uint32_t i;
    minimal_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "FuzzValidate";
    spec.ps = "FuzzValidate";
    spec.num_glyphs = 2u;
    n = build_minimal_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);

    ROBO_CHECK_EQ_INT(robo_font_validate_fuzz_parse(NULL, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_validate_fuzz_parse(font, n), ROBO_OK);

    for (i = 0u; i < sizeof(patterned); i++) {
        patterned[i] = (uint8_t)(0xA5u ^ (uint8_t)(i * 17u + 3u));
    }
    memcpy(patterned, font, n < sizeof(patterned) ? n : sizeof(patterned));
    ROBO_CHECK_EQ_INT(robo_font_validate_fuzz_parse(patterned, sizeof(patterned)),
                      ROBO_OK);

    for (i = 0u; i < 32u; i++) {
        font[i % n] ^= (uint8_t)(i * 13u + 7u);
        ROBO_CHECK_EQ_INT(robo_font_validate_fuzz_parse(font, n), ROBO_OK);
    }
}

static void test_bench(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_validation_policy_t pol;
    int hits = 0;
    int i;
    minimal_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "BenchValidate";
    spec.ps = "BenchValidate";
    spec.num_glyphs = 2u;
    n = build_minimal_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);

    robo_font_validation_policy_init(&pol);
    pol.level = ROBO_FONT_VALIDATION_BASIC;
    pol.checksum_policy = ROBO_FONT_CHECKSUM_ACCEPT;

    for (i = 0; i < 100; i++) {
        robo_font_validation_result_t *result = NULL;
        if (robo_font_validate_source(src, 0u, &pol, &result, NULL) == ROBO_OK &&
            result && robo_font_validation_result_ok(result)) {
            hits++;
        }
        if (result) {
            robo_font_validation_result_release(result);
        }
    }
    printf("font.validate.bench hits=%d\n", hits);
    ROBO_CHECK(hits >= 100);

    robo_font_source_release(src);
}

void robo_test_register_font_validate(void)
{
    robo_test_register("font.validate.policy_limits", test_policy_limits);
    robo_test_register("font.validate.valid_sfnt", test_valid_sfnt);
    robo_test_register("font.validate.bad_signature", test_bad_signature);
    robo_test_register("font.validate.table_count", test_table_count);
    robo_test_register("font.validate.checksum", test_checksum);
    robo_test_register("font.validate.glyph_limit", test_glyph_limit);
    robo_test_register("font.validate.loader", test_loader);
    robo_test_register("font.validate.color_paranoid", test_color_paranoid);
    robo_test_register("font.validate.corpus", test_corpus);
    robo_test_register("font.validate.diagnostics", test_diagnostics);
    robo_test_register("font.validate.fuzz", test_fuzz);
    robo_test_register("font.validate.bench", test_bench);
}
