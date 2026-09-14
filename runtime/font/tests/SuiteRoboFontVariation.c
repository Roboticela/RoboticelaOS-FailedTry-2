/*
 * RoboFont variable-font suite (Goal 5.18).
 * Synthetic SFNT fixtures — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <math.h>
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

#define VAR_AXIS_NAME_ID 256u
#define VAR_BOLD_NAME_ID 257u

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
static void put_i32(uint8_t **p, int32_t v) { put_u32(p, (uint32_t)v); }
static void put_i64(uint8_t **p, int64_t v)
{
    put_u32(p, (uint32_t)((uint64_t)v >> 32));
    put_u32(p, (uint32_t)((uint64_t)v));
}

static int32_t float_to_fixed(float v)
{
    return (int32_t)((double)v * 65536.0);
}

static int16_t float_to_f2dot14(float v)
{
    return (int16_t)lround((double)v * 16384.0);
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

static size_t build_name_table(uint8_t *buf, size_t cap, const uint16_t *ids,
                               const char *const *strs, uint32_t count)
{
    uint8_t *p = buf;
    uint32_t i;
    size_t header = 6u + (size_t)count * 12u;
    size_t storage = header;

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

static size_t build_var_name(uint8_t *buf, size_t cap, const char *family)
{
    uint16_t ids[6];
    const char *strs[6];
    ids[0] = ROBO_FONT_NAME_ID_FAMILY;
    strs[0] = family;
    ids[1] = ROBO_FONT_NAME_ID_SUBFAMILY;
    strs[1] = "Regular";
    ids[2] = ROBO_FONT_NAME_ID_FULL;
    strs[2] = family;
    ids[3] = ROBO_FONT_NAME_ID_POSTSCRIPT;
    strs[3] = "VarSans-Regular";
    ids[4] = (uint16_t)VAR_AXIS_NAME_ID;
    strs[4] = "Weight";
    ids[5] = (uint16_t)VAR_BOLD_NAME_ID;
    strs[5] = "Bold";
    return build_name_table(buf, cap, ids, strs, 6u);
}

/* fvar: one wght axis 100..400..900; Regular@400 + Bold@700. */
static size_t build_fvar_wght(uint8_t *buf, int duplicate_axis)
{
    uint8_t *p = buf;
    uint16_t axis_count = duplicate_axis ? 2u : 1u;
    uint16_t axis_size = 20u;
    uint16_t instance_count = 2u;
    uint16_t instance_size = (uint16_t)(4u + 4u * axis_count);
    uint16_t a;

    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 16);
    put_u16(&p, 0);
    put_u16(&p, axis_count);
    put_u16(&p, axis_size);
    put_u16(&p, instance_count);
    put_u16(&p, instance_size);
    for (a = 0u; a < axis_count; a++) {
        put_u32(&p, ROBO_FONT_TAG_WGHT);
        put_i32(&p, float_to_fixed(100.0f));
        put_i32(&p, float_to_fixed(400.0f));
        put_i32(&p, float_to_fixed(900.0f));
        put_u16(&p, 0);
        put_u16(&p, (uint16_t)VAR_AXIS_NAME_ID);
    }
    /* Regular */
    put_u16(&p, ROBO_FONT_NAME_ID_SUBFAMILY);
    put_u16(&p, 0);
    for (a = 0u; a < axis_count; a++) {
        put_i32(&p, float_to_fixed(400.0f));
    }
    /* Bold */
    put_u16(&p, (uint16_t)VAR_BOLD_NAME_ID);
    put_u16(&p, 0);
    for (a = 0u; a < axis_count; a++) {
        put_i32(&p, float_to_fixed(700.0f));
    }
    return (size_t)(p - buf);
}

/* Truncated fvar: claims huge axisCount but little payload. */
static size_t build_fvar_huge_truncated(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 16);
    put_u16(&p, 0);
    put_u16(&p, 500);
    put_u16(&p, 20);
    put_u16(&p, 0);
    put_u16(&p, 8);
    put_u32(&p, ROBO_FONT_TAG_WGHT);
    put_i32(&p, float_to_fixed(100.0f));
    put_i32(&p, float_to_fixed(400.0f));
    put_i32(&p, float_to_fixed(900.0f));
    put_u16(&p, 0);
    put_u16(&p, (uint16_t)VAR_AXIS_NAME_ID);
    return (size_t)(p - buf);
}

/* avar: (-1,-1),(0,0),(0.5,0.75),(1,1) */
static size_t build_avar_nonlinear(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 1);
    put_u16(&p, 4);
    put_i16(&p, float_to_f2dot14(-1.0f));
    put_i16(&p, float_to_f2dot14(-1.0f));
    put_i16(&p, float_to_f2dot14(0.0f));
    put_i16(&p, float_to_f2dot14(0.0f));
    put_i16(&p, float_to_f2dot14(0.5f));
    put_i16(&p, float_to_f2dot14(0.75f));
    put_i16(&p, float_to_f2dot14(1.0f));
    put_i16(&p, float_to_f2dot14(1.0f));
    return (size_t)(p - buf);
}

enum {
    VAR_FONT_FVAR = 1,
    VAR_FONT_AVAR = 2,
    VAR_FONT_DUP_AXIS = 4,
    VAR_FONT_HUGE_FVAR = 8,
    VAR_FONT_NO_FVAR = 16
};

static size_t build_var_font(uint8_t *out, size_t cap, const char *family,
                             uint16_t os2_weight, int flags)
{
    uint8_t head[64], hhea[40], maxp[40], hmtx[16], loca[16], post[40], os2[96];
    uint8_t name[512], fvar[128], avar[64];
    synth_table_t tabs[16];
    uint32_t nt = 0u;
    size_t nn;
    size_t fn = 0u;
    size_t an = 0u;

    nn = build_var_name(name, sizeof(name), family);
    if (nn == 0u) {
        return 0u;
    }
    if (!(flags & VAR_FONT_NO_FVAR)) {
        if (flags & VAR_FONT_HUGE_FVAR) {
            fn = build_fvar_huge_truncated(fvar);
        } else {
            fn = build_fvar_wght(fvar, (flags & VAR_FONT_DUP_AXIS) ? 1 : 0);
        }
    }
    if (flags & VAR_FONT_AVAR) {
        an = build_avar_nonlinear(avar);
    }

    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HEAD, head, (uint32_t)build_head(head)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HHEA, hhea, (uint32_t)build_hhea(hhea)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_MAXP, maxp, (uint32_t)build_maxp(maxp)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HMTX, hmtx, (uint32_t)build_hmtx(hmtx)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_LOCA, loca, (uint32_t)build_loca(loca)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_GLYF, head, 0u};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_POST, post, (uint32_t)build_post(post)};
    tabs[nt++] =
        (synth_table_t){ROBO_FONT_TAG_OS2, os2, (uint32_t)build_os2(os2, os2_weight)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_NAME, name, (uint32_t)nn};
    if (fn) {
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_FVAR, fvar, (uint32_t)fn};
    }
    if (an) {
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_AVAR, avar, (uint32_t)an};
    }
    return build_sfnt(out, cap, tabs, nt);
}

static int near_f(float a, float b, float eps)
{
    float d = a - b;
    if (d < 0.0f) {
        d = -d;
    }
    return d <= eps;
}

static void test_normalize(void)
{
    float out = 99.0f;
    float nanv;

    ROBO_CHECK_EQ_INT(
        robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 100.0f, &out), ROBO_OK);
    ROBO_CHECK(near_f(out, -1.0f, 0.001f));
    ROBO_CHECK_EQ_INT(
        robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 400.0f, &out), ROBO_OK);
    ROBO_CHECK(near_f(out, 0.0f, 0.001f));
    ROBO_CHECK_EQ_INT(
        robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 900.0f, &out), ROBO_OK);
    ROBO_CHECK(near_f(out, 1.0f, 0.001f));
    /* Midpoints below / above default. */
    ROBO_CHECK_EQ_INT(
        robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 250.0f, &out), ROBO_OK);
    ROBO_CHECK(near_f(out, -0.5f, 0.001f));
    ROBO_CHECK_EQ_INT(
        robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 650.0f, &out), ROBO_OK);
    ROBO_CHECK(near_f(out, 0.5f, 0.001f));

    ROBO_CHECK_EQ_INT(
        robo_font_variation_clamp_user(100.0f, 400.0f, 900.0f, 50.0f, &out), ROBO_OK);
    ROBO_CHECK(near_f(out, 100.0f, 0.001f));
    ROBO_CHECK_EQ_INT(
        robo_font_variation_clamp_user(100.0f, 400.0f, 900.0f, 950.0f, &out), ROBO_OK);
    ROBO_CHECK(near_f(out, 900.0f, 0.001f));

    nanv = nanf("");
    ROBO_CHECK_EQ_INT(
        robo_font_variation_clamp_user(100.0f, 400.0f, 900.0f, nanv, &out),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_font_variation_normalize(100.0f, 400.0f, 900.0f, nanv, &out),
        ROBO_INVALID_ARGUMENT);
}

static void test_fvar_parse(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    const robo_font_variation_model_t *m;
    const robo_font_variation_axis_t *ax;
    const robo_font_variation_instance_t *inst;
    robo_font_variation_model_t *dup_model = NULL;
    uint8_t dup_font[4096];
    size_t dn;
    robo_font_source_t *dsrc = NULL;
    robo_font_sfnt_t *dsfnt = NULL;

    n = build_var_font(font, sizeof(font), "VarSans", 400, VAR_FONT_FVAR);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_variation(face), ROBO_OK);
    m = robo_font_face_variation(face);
    ROBO_CHECK(m != NULL);
    ROBO_CHECK(robo_font_variation_model_is_variable(m));
    ROBO_CHECK_EQ_INT((int)robo_font_variation_model_axis_count(m), 1);
    ax = robo_font_variation_model_axis(m, 0u);
    ROBO_CHECK(ax != NULL);
    ROBO_CHECK_EQ_INT((int)ax->tag, (int)ROBO_FONT_TAG_WGHT);
    ROBO_CHECK(near_f(ax->min_value, 100.0f, 0.01f));
    ROBO_CHECK(near_f(ax->default_value, 400.0f, 0.01f));
    ROBO_CHECK(near_f(ax->max_value, 900.0f, 0.01f));
    ROBO_CHECK(ax->name_len > 0u);
    ROBO_CHECK(strcmp(ax->name_utf8, "Weight") == 0);
    ROBO_CHECK_EQ_INT((int)robo_font_variation_model_instance_count(m), 2);
    inst = robo_font_variation_model_instance(m, 0u);
    ROBO_CHECK(inst != NULL);
    ROBO_CHECK(inst->name_len > 0u);
    ROBO_CHECK(strcmp(inst->name_utf8, "Regular") == 0);
    ROBO_CHECK(near_f(inst->user_coords[0], 400.0f, 0.01f));
    inst = robo_font_variation_model_instance(m, 1u);
    ROBO_CHECK(inst != NULL);
    ROBO_CHECK(strcmp(inst->name_utf8, "Bold") == 0);
    ROBO_CHECK(near_f(inst->user_coords[0], 700.0f, 0.01f));

    /* Duplicate axis tags rejected. */
    dn = build_var_font(dup_font, sizeof(dup_font), "DupAxis", 400,
                        VAR_FONT_FVAR | VAR_FONT_DUP_AXIS);
    ROBO_CHECK(dn > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(dup_font, dn, &dsrc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(dsrc, 0u, 0u, &dsfnt), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_variation_model_create(dsfnt, NULL, NULL, &dup_model),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK(dup_model == NULL);
    robo_font_sfnt_release(dsfnt);
    robo_font_source_release(dsrc);

    /* Missing fvar → NOT_FOUND. */
    {
        uint8_t static_font[4096];
        size_t sn;
        robo_font_source_t *ssrc = NULL;
        robo_font_face_t *sface = NULL;
        sn = build_var_font(static_font, sizeof(static_font), "Static", 400,
                            VAR_FONT_NO_FVAR);
        ROBO_CHECK(sn > 0u);
        ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(static_font, sn, &ssrc),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_face_create(ssrc, 0u, &sface), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_face_parse_variation(sface), ROBO_NOT_FOUND);
        ROBO_CHECK(robo_font_face_variation(sface) == NULL);
        robo_font_face_release(sface);
        robo_font_source_release(ssrc);
    }

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_avar(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    const robo_font_variation_model_t *m;
    robo_font_variation_request_t req;
    robo_font_variation_coords_t *coords = NULL;
    float norm;
    float adj;

    n = build_var_font(font, sizeof(font), "AvarSans", 400, VAR_FONT_FVAR | VAR_FONT_AVAR);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_variation(face), ROBO_OK);
    m = robo_font_face_variation(face);
    ROBO_CHECK(m != NULL);
    ROBO_CHECK(robo_font_variation_model_has_avar(m));

    /* User mid-high 650 → normalized 0.5 → avar maps to 0.75. */
    ROBO_CHECK_EQ_INT(
        robo_font_variation_normalize(100.0f, 400.0f, 900.0f, 650.0f, &norm), ROBO_OK);
    ROBO_CHECK(near_f(norm, 0.5f, 0.001f));
    ROBO_CHECK_EQ_INT(robo_font_variation_avar_map(m, 0u, norm, &adj), ROBO_OK);
    ROBO_CHECK(near_f(adj, 0.75f, 0.02f));
    ROBO_CHECK(!near_f(adj, norm, 0.01f));

    robo_font_variation_request_init(&req);
    req.apply_style_axes = 1u;
    req.weight = 650;
    ROBO_CHECK_EQ_INT(robo_font_variation_coords_create(m, &req, &coords), ROBO_OK);
    ROBO_CHECK(near_f(robo_font_variation_coords_normalized(coords, 0u), 0.5f, 0.02f));
    ROBO_CHECK(near_f(robo_font_variation_coords_adjusted(coords, 0u), 0.75f, 0.02f));
    robo_font_variation_coords_release(coords);

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_coords_instance(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_variation_model_t *model = NULL;
    robo_font_variation_request_t req;
    robo_font_variation_coords_t *coords = NULL;
    robo_font_instance_t *a = NULL;
    robo_font_instance_t *b = NULL;
    robo_font_style_desc_t style;
    const robo_font_variation_coords_t *ca;
    const robo_font_variation_coords_t *cb;
    float user = 0.0f;

    n = build_var_font(font, sizeof(font), "CoordSans", 400, VAR_FONT_FVAR);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_variation_model_create_from_face(face, &model), ROBO_OK);
    ROBO_CHECK(model != NULL);

    robo_font_variation_request_init(&req);
    req.apply_style_axes = 1u;
    req.weight = 600;
    ROBO_CHECK_EQ_INT(robo_font_variation_coords_create(model, &req, &coords), ROBO_OK);
    ROBO_CHECK(near_f(robo_font_variation_coords_user(coords, 0u), 600.0f, 0.01f));
    /* (600-400)/(900-400) = 0.4 */
    ROBO_CHECK(near_f(robo_font_variation_coords_normalized(coords, 0u), 0.4f, 0.02f));
    ROBO_CHECK_EQ_INT(
        robo_font_variation_coords_user_for_tag(coords, ROBO_FONT_TAG_WGHT, &user),
        ROBO_OK);
    ROBO_CHECK(near_f(user, 600.0f, 0.01f));
    robo_font_variation_coords_release(coords);
    robo_font_variation_model_release(model);

    robo_font_style_desc_init(&style);
    style.weight = 300;
    ROBO_CHECK_EQ_INT(robo_font_instance_create(face, 16.0f, &style, &a), ROBO_OK);
    style.weight = 800;
    ROBO_CHECK_EQ_INT(robo_font_instance_create(face, 16.0f, &style, &b), ROBO_OK);
    ca = robo_font_instance_variation_coords(a);
    cb = robo_font_instance_variation_coords(b);
    ROBO_CHECK(ca != NULL && cb != NULL);
    ROBO_CHECK(ca != cb);
    ROBO_CHECK(near_f(robo_font_variation_coords_user(ca, 0u), 300.0f, 0.01f));
    ROBO_CHECK(near_f(robo_font_variation_coords_user(cb, 0u), 800.0f, 0.01f));
    ROBO_CHECK_EQ_INT((int)robo_font_instance_metrics_variation_state(a),
                      (int)ROBO_FONT_METRICS_VAR_PENDING);
    ROBO_CHECK_EQ_INT((int)robo_font_instance_metrics_variation_state(b),
                      (int)ROBO_FONT_METRICS_VAR_PENDING);

    robo_font_instance_release(a);
    robo_font_instance_release(b);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_select(void)
{
    uint8_t static_buf[4096];
    uint8_t var_buf[4096];
    size_t sn;
    size_t vn;
    robo_font_source_t *ssrc = NULL;
    robo_font_source_t *vsrc = NULL;
    robo_font_face_t *sface = NULL;
    robo_font_face_t *vface = NULL;
    robo_font_face_t *cands[2];
    robo_font_select_request_t req;
    robo_font_selection_t sel;
    robo_font_match_score_t score;

    sn = build_var_font(static_buf, sizeof(static_buf), "SelectFam", 400, VAR_FONT_NO_FVAR);
    vn = build_var_font(var_buf, sizeof(var_buf), "SelectFam", 400, VAR_FONT_FVAR);
    ROBO_CHECK(sn > 0u && vn > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(static_buf, sn, &ssrc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(var_buf, vn, &vsrc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(ssrc, 0u, &sface), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(vsrc, 0u, &vface), ROBO_OK);

    cands[0] = sface;
    cands[1] = vface;
    robo_font_select_request_init(&req);
    req.family = "SelectFam";
    req.weight = 700;
    req.stretch = 100;
    req.style = ROBO_FONT_STYLE_NORMAL;

    ROBO_CHECK_EQ_INT(robo_font_select_score_face(&req, vface, &score), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)score.weight_dist, 0);

    ROBO_CHECK_EQ_INT(robo_font_select(&req, cands, 2u, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK(sel.face == vface);
    ROBO_CHECK_EQ_INT((int)sel.score.weight_dist, 0);
    ROBO_CHECK_EQ_INT(sel.selected.weight, 700);
    robo_font_selection_clear(&sel);

    robo_font_face_release(sface);
    robo_font_face_release(vface);
    robo_font_source_release(ssrc);
    robo_font_source_release(vsrc);
}

static void test_fuzz(void)
{
    uint8_t junk[] = {0x00, 0x01, 0x00, 0x00, 0xFF, 0xFE, 0xFD, 0xFC};
    uint8_t empty[1] = {0};
    uint8_t font[4096];
    size_t n;
    uint8_t huge[4096];
    size_t hn;

    ROBO_CHECK_EQ_INT(robo_font_variation_fuzz_parse(NULL, 0u), ROBO_OK);
    (void)robo_font_variation_fuzz_parse(junk, sizeof(junk));
    (void)robo_font_variation_fuzz_parse(empty, 0u);

    n = build_var_font(font, sizeof(font), "FuzzOk", 400, VAR_FONT_FVAR | VAR_FONT_AVAR);
    ROBO_CHECK(n > 0u);
    (void)robo_font_variation_fuzz_parse(font, n);

    hn = build_var_font(huge, sizeof(huge), "FuzzHuge", 400,
                        VAR_FONT_FVAR | VAR_FONT_HUGE_FVAR);
    ROBO_CHECK(hn > 0u);
    (void)robo_font_variation_fuzz_parse(huge, hn);
    {
        robo_font_source_t *src = NULL;
        robo_font_sfnt_t *sfnt = NULL;
        robo_font_variation_model_t *model = NULL;
        ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(huge, hn, &src), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0u, 0u, &sfnt), ROBO_OK);
        ROBO_CHECK(robo_font_variation_model_create(sfnt, NULL, NULL, &model) != ROBO_OK);
        ROBO_CHECK(model == NULL);
        robo_font_sfnt_release(sfnt);
        robo_font_source_release(src);
    }
}

static void test_bench(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    const robo_font_variation_model_t *m;
    int hits = 0;
    int i;

    n = build_var_font(font, sizeof(font), "BenchSans", 400, VAR_FONT_FVAR);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_variation(face), ROBO_OK);
    m = robo_font_face_variation(face);
    ROBO_CHECK(m != NULL);

    for (i = 0; i < 200; i++) {
        float norm = 0.0f;
        float w = 100.0f + (float)(i % 801);
        robo_font_variation_request_t req;
        robo_font_variation_coords_t *coords = NULL;
        if (robo_font_variation_normalize(100.0f, 400.0f, 900.0f, w, &norm) == ROBO_OK) {
            hits++;
        }
        robo_font_variation_request_init(&req);
        req.apply_style_axes = 1u;
        req.weight = (robo_font_weight_t)(100 + (i % 900));
        if (robo_font_variation_coords_create(m, &req, &coords) == ROBO_OK && coords) {
            hits++;
            (void)robo_font_variation_coords_adjusted(coords, 0u);
            robo_font_variation_coords_release(coords);
        }
    }
    printf("font.variation.bench hits=%d\n", hits);
    ROBO_CHECK(hits >= 200);

    robo_font_face_release(face);
    robo_font_source_release(src);
}

void robo_test_register_font_variation(void)
{
    robo_test_register("font.variation.normalize", test_normalize);
    robo_test_register("font.variation.fvar_parse", test_fvar_parse);
    robo_test_register("font.variation.avar", test_avar);
    robo_test_register("font.variation.coords_instance", test_coords_instance);
    robo_test_register("font.variation.select", test_select);
    robo_test_register("font.variation.fuzz", test_fuzz);
    robo_test_register("font.variation.bench", test_bench);
}
