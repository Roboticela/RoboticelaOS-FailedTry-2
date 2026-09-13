/*
 * RoboFont CFF / CFF2 suite (Goal 5.5).
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

static size_t build_sfnt(uint8_t *out,
                         size_t out_cap,
                         uint32_t version,
                         const synth_table_t *tables,
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
    put_u32(&p, version);
    write_search_fields(out, (uint16_t)n);
    cursor = dir_end;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint32_t sum = 0u;
        uint32_t j;
        uint8_t *rec = out + 12u + (size_t)i * 16u;
        p = rec;
        put_u32(&p, tables[i].tag);
        for (j = 0u; j + 3u < tables[i].length; j += 4u) {
            sum += ((uint32_t)tables[i].data[j] << 24) |
                   ((uint32_t)tables[i].data[j + 1u] << 16) |
                   ((uint32_t)tables[i].data[j + 2u] << 8) |
                   (uint32_t)tables[i].data[j + 3u];
        }
        put_u32(&p, sum);
        put_u32(&p, (uint32_t)cursor);
        put_u32(&p, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

/* Encode DICT integer in -107..107 range as one byte. */
static void dict_put_small(uint8_t **p, int32_t v)
{
    put_u8(p, (uint8_t)(v + 139));
}

static void dict_put_int(uint8_t **p, int32_t v)
{
    if (v >= -107 && v <= 107) {
        dict_put_small(p, v);
    } else if (v >= -32768 && v <= 32767) {
        put_u8(p, 28);
        put_u16(p, (uint16_t)(int16_t)v);
    } else {
        put_u8(p, 29);
        put_u32(p, (uint32_t)v);
    }
}

/* CFF1 INDEX with Card16 count, offSize 1 */
static size_t write_index1(uint8_t *buf, const uint8_t *obj, uint32_t obj_len)
{
    uint8_t *p = buf;
    put_u16(&p, 1u);
    put_u8(&p, 1u);
    put_u8(&p, 1u);
    put_u8(&p, (uint8_t)(1u + obj_len));
    memcpy(p, obj, obj_len);
    p += obj_len;
    return (size_t)(p - buf);
}

static size_t write_empty_index1(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 0u);
    return 2u;
}

/*
 * Minimal CFF1:
 *   header + Name + TopDICT + empty String + empty GSubr
 *   + CharStrings (1 glyph: endchar) + charset fmt0 + Private
 */
static size_t build_minimal_cff1(uint8_t *out, size_t cap)
{
    uint8_t name[] = {'T', 'e', 's', 't'};
    uint8_t cs[] = {0x0e};
    uint8_t topd[32];
    uint8_t *t;
    uint8_t privd[8];
    uint8_t *pv;
    size_t priv_len;
    size_t top_len;
    size_t pos;
    size_t cs_pos;
    size_t charset_pos;
    size_t priv_pos;
    size_t top_index_size;
    size_t base_after_indexes;
    size_t est_top = 20u;

    if (cap < 256u) {
        return 0u;
    }

    pv = privd;
    dict_put_small(&pv, 0);
    put_u8(&pv, 20);
    priv_len = (size_t)(pv - privd);

    top_index_size = 5u + est_top;
    base_after_indexes = 4u + (2u + 1u + 2u + 4u) + top_index_size + 2u + 2u;
    cs_pos = base_after_indexes;
    charset_pos = cs_pos + 5u + 1u;
    priv_pos = charset_pos + 1u;

    t = topd;
    dict_put_int(&t, (int32_t)charset_pos);
    put_u8(&t, 15);
    dict_put_int(&t, (int32_t)cs_pos);
    put_u8(&t, 17);
    dict_put_int(&t, (int32_t)priv_len);
    dict_put_int(&t, (int32_t)priv_pos);
    put_u8(&t, 18);
    top_len = (size_t)(t - topd);

    top_index_size = 5u + top_len;
    base_after_indexes = 4u + (2u + 1u + 2u + 4u) + top_index_size + 2u + 2u;
    cs_pos = base_after_indexes;
    charset_pos = cs_pos + 5u + 1u;
    priv_pos = charset_pos + 1u;

    t = topd;
    dict_put_int(&t, (int32_t)charset_pos);
    put_u8(&t, 15);
    dict_put_int(&t, (int32_t)cs_pos);
    put_u8(&t, 17);
    dict_put_int(&t, (int32_t)priv_len);
    dict_put_int(&t, (int32_t)priv_pos);
    put_u8(&t, 18);
    top_len = (size_t)(t - topd);

    memset(out, 0, cap);
    pos = 0u;
    out[pos++] = 1;
    out[pos++] = 0;
    out[pos++] = 4;
    out[pos++] = 1;
    pos += write_index1(out + pos, name, 4u);
    pos += write_index1(out + pos, topd, (uint32_t)top_len);
    pos += write_empty_index1(out + pos);
    pos += write_empty_index1(out + pos);
    if (pos != cs_pos) {
        return 0u;
    }
    pos += write_index1(out + pos, cs, 1u);
    if (pos != charset_pos) {
        return 0u;
    }
    out[pos++] = 0;
    if (pos != priv_pos) {
        return 0u;
    }
    memcpy(out + pos, privd, priv_len);
    pos += priv_len;
    return pos;
}

static void test_cff_index_dict(void)
{
    uint8_t buf[64];
    uint8_t *p = buf;
    robo_font_cff_index_t idx;
    robo_font_span_t span;
    robo_font_span_t obj;
    size_t end = 0u;
    robo_font_cff_dict_t dict;
    const robo_font_cff_dict_entry_t *ent;

    /* INDEX: count=2, offSize=1, objs "A","BC" */
    put_u16(&p, 2u);
    put_u8(&p, 1u);
    put_u8(&p, 1u); /* off0 */
    put_u8(&p, 2u); /* off1 */
    put_u8(&p, 4u); /* off2 */
    put_u8(&p, 'A');
    put_u8(&p, 'B');
    put_u8(&p, 'C');
    span.data = buf;
    span.size = (size_t)(p - buf);
    memset(&idx, 0, sizeof(idx));
    ROBO_CHECK_EQ_INT(robo_font_cff_index_parse(span, 0, &idx, &end), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_cff_index_count(&idx), 2);
    ROBO_CHECK_EQ_INT(robo_font_cff_index_get(&idx, 0, &obj), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)obj.size, 1);
    ROBO_CHECK_EQ_INT(obj.data[0], 'A');
    ROBO_CHECK_EQ_INT(robo_font_cff_index_get(&idx, 1, &obj), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)obj.size, 2);
    robo_font_cff_index_clear(&idx);

    /* DICT: FontBBox -10 -20 100 200 */
    p = buf;
    dict_put_int(&p, -10);
    dict_put_int(&p, -20);
    dict_put_int(&p, 100);
    dict_put_int(&p, 200);
    put_u8(&p, 5);
    span.size = (size_t)(p - buf);
    memset(&dict, 0, sizeof(dict));
    ROBO_CHECK_EQ_INT(robo_font_cff_dict_parse(span, &dict), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cff_dict_find(&dict, ROBO_CFF_OP_FONTBBOX, &ent), ROBO_OK);
    ROBO_CHECK_EQ_INT(ent->argc, 4);
    ROBO_CHECK_EQ_INT(ent->args[0].i, -10);
    ROBO_CHECK_EQ_INT(ent->args[3].i, 200);
    robo_font_cff_dict_clear(&dict);

    ROBO_CHECK_EQ_INT((int)robo_font_cff_subr_bias(10u), 107);
    ROBO_CHECK_EQ_INT((int)robo_font_cff_subr_bias(2000u), 1131);
}

static void test_cff_charstring(void)
{
    uint8_t cs[] = {
        /* 10 20 rmoveto  30 0 rlineto  endchar */
        (uint8_t)(10 + 139), (uint8_t)(20 + 139), 21, (uint8_t)(30 + 139),
        (uint8_t)(0 + 139), 5, 0x0e};
    robo_font_span_t span = {cs, sizeof(cs)};
    robo_font_cff_charstring_t out;
    memset(&out, 0, sizeof(out));
    ROBO_CHECK_EQ_INT(
        robo_font_cff_charstring_parse(span, NULL, 0.0, 0.0, 0, &out), ROBO_OK);
    ROBO_CHECK(out.op_count >= 3u);
    ROBO_CHECK_EQ_INT(out.ops[out.op_count - 1u].op, 14);
    robo_font_cff_charstring_clear(&out);

    /* stack overflow */
    {
        uint8_t bad[64];
        uint8_t *p = bad;
        int i;
        for (i = 0; i < 50; i++) {
            put_u8(&p, (uint8_t)(139)); /* push 0 */
        }
        put_u8(&p, 0x0e);
        span.data = bad;
        span.size = (size_t)(p - bad);
        ROBO_CHECK(robo_font_cff_charstring_parse(span, NULL, 0.0, 0.0, 0, &out) !=
                   ROBO_OK);
        robo_font_cff_charstring_clear(&out);
    }
}

static void test_cff_charset_encoding_fdselect(void)
{
    uint8_t buf[40];
    uint8_t *p;
    robo_font_span_t span;
    robo_font_cff_charset_t cs;
    robo_font_cff_encoding_t enc;
    robo_font_cff_fdselect_t fd;
    uint16_t sid;
    uint8_t fdi;

    /* Place payload at offset 10 so parsers treat it as a real table offset. */
    memset(buf, 0, sizeof(buf));
    p = buf + 10;
    put_u8(&p, 0);
    put_u16(&p, 1);
    put_u16(&p, 2);
    span.data = buf;
    span.size = (size_t)(p - buf);
    memset(&cs, 0, sizeof(cs));
    ROBO_CHECK_EQ_INT(robo_font_cff_charset_parse(span, 10, 3u, 0, &cs), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cff_charset_sid(&cs, 1, &sid), ROBO_OK);
    ROBO_CHECK_EQ_INT(sid, 1);
    robo_font_cff_charset_clear(&cs);

    memset(buf, 0, sizeof(buf));
    p = buf + 10;
    put_u8(&p, 0);
    put_u8(&p, 2);
    put_u8(&p, 65);
    put_u8(&p, 66);
    span.size = (size_t)(p - buf);
    memset(&enc, 0, sizeof(enc));
    ROBO_CHECK_EQ_INT(robo_font_cff_encoding_parse(span, 10, 3u, &enc), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)enc.n_codes, 2);
    ROBO_CHECK_EQ_INT(enc.codes[0], 65);
    robo_font_cff_encoding_clear(&enc);

    memset(buf, 0, sizeof(buf));
    p = buf + 10;
    put_u8(&p, 3);
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u8(&p, 0);
    put_u16(&p, 3);
    span.size = (size_t)(p - buf);
    memset(&fd, 0, sizeof(fd));
    ROBO_CHECK_EQ_INT(robo_font_cff_fdselect_parse(span, 10, 3u, 1u, &fd), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_cff_fdselect_get(&fd, 2, &fdi), ROBO_OK);
    ROBO_CHECK_EQ_INT(fdi, 0);
    robo_font_cff_fdselect_clear(&fd);
}

static void test_cff_font_bundle(void)
{
    uint8_t cff[256];
    uint8_t font[512];
    size_t cff_len;
    size_t n;
    synth_table_t tabs[1];
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_cff_font_t *cf = NULL;
    robo_font_cff_charstring_t glyph;

    cff_len = build_minimal_cff1(cff, sizeof(cff));
    ROBO_CHECK(cff_len > 0u);
    tabs[0].tag = ROBO_FONT_TAG_CFF;
    tabs[0].data = cff;
    tabs[0].length = (uint32_t)cff_len;
    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_OTTO, tabs, 1u);
    ROBO_CHECK(n > 0u);

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_ot_detect_outline(sfnt), (int)ROBO_FONT_OUTLINE_CFF);
    ROBO_CHECK_EQ_INT(robo_font_cff_parse(sfnt, ROBO_FONT_CFF_FLAGS_PARSE_ALL, &cf),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_cff_outline_tech(cf), (int)ROBO_FONT_OUTLINE_CFF);
    ROBO_CHECK_EQ_INT((int)robo_font_cff_glyph_count(cf), 1);
    ROBO_CHECK_EQ_INT(cf->major, 1);
    ROBO_CHECK(cf->top.charstrings >= 0);
    memset(&glyph, 0, sizeof(glyph));
    ROBO_CHECK_EQ_INT(robo_font_cff_parse_glyph(cf, 0, &glyph), ROBO_OK);
    ROBO_CHECK(glyph.op_count >= 1u);
    robo_font_cff_charstring_clear(&glyph);

    ROBO_CHECK_EQ_INT(robo_font_cff_fuzz_parse(font, n), ROBO_OK);

    robo_font_cff_release(cf);
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
}

static void test_cff2_and_vstore(void)
{
    uint8_t cff2[256];
    uint8_t top[40];
    uint8_t font_dict[16];
    uint8_t fdselect[16];
    uint8_t font[512];
    uint8_t *t;
    uint8_t *fd;
    uint8_t *fs;
    uint8_t *p;
    size_t top_len;
    size_t fd_len;
    size_t fds_len;
    size_t hdr = 5u;
    size_t top_est = 32u;
    size_t gsubr_off;
    size_t cs_off;
    size_t fd_off;
    size_t fdselect_off;
    size_t priv_off;
    size_t vstore_off;
    size_t cs_size = 8u; /* Card32 INDEX with 1×endchar */
    size_t g_empty = 4u;
    size_t fds_size = 8u;
    size_t priv_size = 0u;
    size_t n;
    synth_table_t tabs[1];
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_cff_font_t *cf = NULL;

    gsubr_off = hdr + top_est;
    cs_off = gsubr_off + g_empty;
    fd_off = cs_off + cs_size;
    /* FD dict size estimate then refine */
    fd = font_dict;
    dict_put_int(&fd, 0);
    dict_put_int(&fd, 100); /* placeholder priv */
    put_u8(&fd, 18);
    fd_len = (size_t)(fd - font_dict);
    fdselect_off = fd_off + 4u + 1u + 2u + fd_len;
    priv_off = fdselect_off + fds_size;
    vstore_off = priv_off + priv_size;

    fd = font_dict;
    dict_put_int(&fd, (int32_t)priv_size);
    dict_put_int(&fd, (int32_t)priv_off);
    put_u8(&fd, 18);
    fd_len = (size_t)(fd - font_dict);
    fdselect_off = fd_off + 4u + 1u + 2u + fd_len;
    priv_off = fdselect_off + fds_size;
    vstore_off = priv_off + priv_size;

    t = top;
    dict_put_int(&t, (int32_t)cs_off);
    put_u8(&t, 17);
    dict_put_int(&t, (int32_t)fd_off);
    put_u8(&t, 12);
    put_u8(&t, 36);
    dict_put_int(&t, (int32_t)fdselect_off);
    put_u8(&t, 12);
    put_u8(&t, 37);
    dict_put_int(&t, (int32_t)vstore_off);
    put_u8(&t, 12);
    put_u8(&t, 24);
    top_len = (size_t)(t - top);

    if (top_len != top_est) {
        {
            int32_t d = (int32_t)top_len - (int32_t)top_est;
            cs_off = (size_t)((int32_t)cs_off + d);
            fd_off = (size_t)((int32_t)fd_off + d);
            fdselect_off = (size_t)((int32_t)fdselect_off + d);
            priv_off = (size_t)((int32_t)priv_off + d);
            vstore_off = (size_t)((int32_t)vstore_off + d);
            gsubr_off = (size_t)((int32_t)gsubr_off + d);
        }
        fd = font_dict;
        dict_put_int(&fd, (int32_t)priv_size);
        dict_put_int(&fd, (int32_t)priv_off);
        put_u8(&fd, 18);
        fd_len = (size_t)(fd - font_dict);
        t = top;
        dict_put_int(&t, (int32_t)cs_off);
        put_u8(&t, 17);
        dict_put_int(&t, (int32_t)fd_off);
        put_u8(&t, 12);
        put_u8(&t, 36);
        dict_put_int(&t, (int32_t)fdselect_off);
        put_u8(&t, 12);
        put_u8(&t, 37);
        dict_put_int(&t, (int32_t)vstore_off);
        put_u8(&t, 12);
        put_u8(&t, 24);
        top_len = (size_t)(t - top);
    }

    fs = fdselect;
    put_u8(&fs, 3);
    put_u16(&fs, 1);
    put_u16(&fs, 0);
    put_u8(&fs, 0);
    put_u16(&fs, 1);
    fds_len = (size_t)(fs - fdselect);

    memset(cff2, 0, sizeof(cff2));
    p = cff2;
    put_u8(&p, 2);
    put_u8(&p, 0);
    put_u8(&p, 5);
    put_u16(&p, (uint16_t)top_len);
    memcpy(p, top, top_len);
    p += top_len;
    put_u32(&p, 0u); /* empty global subrs */
    put_u32(&p, 1u);
    put_u8(&p, 1u);
    put_u8(&p, 1u);
    put_u8(&p, 2u);
    put_u8(&p, 0x0e);
    put_u32(&p, 1u);
    put_u8(&p, 1u);
    put_u8(&p, 1u);
    put_u8(&p, (uint8_t)(1u + fd_len));
    memcpy(p, font_dict, fd_len);
    p += fd_len;
    memcpy(p, fdselect, fds_len);
    p += fds_len;
    /* vstore */
    put_u16(&p, 1);
    put_u32(&p, 8);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
    n = (size_t)(p - cff2);

    tabs[0].tag = ROBO_FONT_TAG_CFF2;
    tabs[0].data = cff2;
    tabs[0].length = (uint32_t)n;
    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_OTTO, tabs, 1u);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0, 0, &sfnt), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_ot_detect_outline(sfnt),
                      (int)ROBO_FONT_OUTLINE_CFF2);
    ROBO_CHECK_EQ_INT(robo_font_cff_parse(sfnt, ROBO_FONT_CFF_FLAGS_PARSE_ALL, &cf),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(cf->major, 2);
    ROBO_CHECK_EQ_INT((int)robo_font_cff_glyph_count(cf), 1);
    ROBO_CHECK(cf->has_vstore);
    ROBO_CHECK_EQ_INT(cf->vstore.format, 1);
    ROBO_CHECK_EQ_INT((int)cf->fd_count, 1);

    robo_font_cff_release(cf);
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
}

static void test_cff_malformed(void)
{
    uint8_t bad_ix[] = {0x00, 0x01, 0x05}; /* bad offSize */
    robo_font_span_t span = {bad_ix, sizeof(bad_ix)};
    robo_font_cff_index_t idx;
    size_t end = 0u;
    memset(&idx, 0, sizeof(idx));
    ROBO_CHECK(robo_font_cff_index_parse(span, 0, &idx, &end) != ROBO_OK);

    ROBO_CHECK(robo_font_cff_fuzz_parse((const uint8_t *)"xxxx", 4) != ROBO_OK);
    {
        uint8_t trunc[] = {1, 0, 4, 1, 0, 1}; /* header + truncated INDEX */
        span.data = trunc;
        span.size = sizeof(trunc);
        ROBO_CHECK(robo_font_cff_index_parse(span, 4, &idx, &end) != ROBO_OK);
    }
}

void robo_test_register_font_cff(void)
{
    robo_test_register("font.cff.index_dict", test_cff_index_dict);
    robo_test_register("font.cff.charstring", test_cff_charstring);
    robo_test_register("font.cff.charset_enc", test_cff_charset_encoding_fdselect);
    robo_test_register("font.cff.bundle", test_cff_font_bundle);
    robo_test_register("font.cff.cff2", test_cff2_and_vstore);
    robo_test_register("font.cff.malformed", test_cff_malformed);
}
