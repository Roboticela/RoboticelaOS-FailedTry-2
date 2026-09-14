/*
 * RoboFont emoji-font suite (Goal 5.17).
 * Synthetic registry fixtures â€” no Chromium / system fonts.
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

static size_t build_cmap_fmt12_flat(uint8_t *buf, size_t cap, uint32_t first,
                                    uint32_t last, uint32_t gid)
{
    uint8_t *p = buf;
    uint32_t n;
    uint32_t i;
    uint32_t body;
    if (last < first) {
        return 0u;
    }
    n = last - first + 1u;
    body = 16u + n * 12u;
    if (cap < 12u + body) {
        return 0u;
    }
    put_u16(&p, 0);
    put_u16(&p, 1);
    put_u16(&p, 3);
    put_u16(&p, 10);
    put_u32(&p, 12u);
    put_u16(&p, 12);
    put_u16(&p, 0);
    put_u32(&p, body);
    put_u32(&p, 0);
    put_u32(&p, n);
    for (i = 0u; i < n; i++) {
        uint32_t cp = first + i;
        put_u32(&p, cp);
        put_u32(&p, cp);
        put_u32(&p, gid);
    }
    return (size_t)(p - buf);
}

static size_t build_cmap_fmt12_range(uint8_t *buf, size_t cap, uint32_t first,
                                     uint32_t last, uint32_t gid)
{
    return build_cmap_fmt12_flat(buf, cap, first, last, gid);
}

static size_t build_minimal_colr(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 0); /* version */
    put_u16(&p, 0); /* numBaseGlyphRecords */
    put_u32(&p, 14u); /* baseGlyphRecordsOffset */
    put_u32(&p, 14u); /* layerRecordsOffset */
    put_u16(&p, 0); /* numLayerRecords */
    return (size_t)(p - buf);
}

static size_t build_minimal_cpal(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 0); /* version */
    put_u16(&p, 1); /* numPaletteEntries */
    put_u16(&p, 1); /* numPalettes */
    put_u16(&p, 1); /* numColorRecords */
    put_u32(&p, 12u); /* colorRecordsArrayOffset */
    put_u16(&p, 0); /* colorRecordIndices[0] */
    put_u8(&p, 0);
    put_u8(&p, 0);
    put_u8(&p, 0);
    put_u8(&p, 255); /* BGRA */
    return (size_t)(p - buf);
}

typedef struct {
    int with_color;
    int with_sbix;
    const char *family;
    const char *ps;
    uint32_t cmap_first;
    uint32_t cmap_last;
    int ascii_only;
    uint16_t ascii_cp;
} emoji_font_spec_t;

static size_t build_emoji_font(uint8_t *out, size_t cap, const emoji_font_spec_t *spec)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], loca[16], post[64], os2[128];
    uint8_t name[512], cmap[1024], colr[32], cpal[32], sbix[8];
    synth_table_t tabs[16];
    uint32_t nt = 0u;
    size_t nn;
    size_t nc;
    uint16_t glyphs = 2u;

    if (!spec) {
        return 0u;
    }
    nn = build_name_table(name, sizeof(name), spec->family, spec->ps);
    if (nn == 0u) {
        return 0u;
    }
    if (spec->ascii_only) {
        nc = build_cmap_fmt4_ascii(cmap, spec->ascii_cp, 1u);
    } else {
        /* Flat groups → GID 1 so maxp/loca stay consistent. */
        nc = build_cmap_fmt12_range(cmap, sizeof(cmap), spec->cmap_first,
                                    spec->cmap_last, 1u);
    }
    if (nc == 0u) {
        return 0u;
    }

    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HEAD, head, (uint32_t)build_head(head)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HHEA, hhea, (uint32_t)build_hhea(hhea)};
    tabs[nt++] =
        (synth_table_t){ROBO_FONT_TAG_MAXP, maxp, (uint32_t)build_maxp(maxp, glyphs)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_HMTX, hmtx, (uint32_t)build_hmtx(hmtx)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_LOCA, loca, (uint32_t)build_loca(loca)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_GLYF, head, 0u};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_POST, post, (uint32_t)build_post(post)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_OS2, os2, (uint32_t)build_os2(os2)};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_NAME, name, (uint32_t)nn};
    tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_CMAP, cmap, (uint32_t)nc};
    if (spec->with_color) {
        tabs[nt++] =
            (synth_table_t){ROBO_FONT_TAG_COLR, colr, (uint32_t)build_minimal_colr(colr)};
        tabs[nt++] =
            (synth_table_t){ROBO_FONT_TAG_CPAL, cpal, (uint32_t)build_minimal_cpal(cpal)};
    }
    if (spec->with_sbix) {
        sbix[0] = 0;
        sbix[1] = 0;
        sbix[2] = 0;
        sbix[3] = 1; /* version-ish stub */
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_SBIX, sbix, 4u};
    }
    return build_sfnt(out, cap, tabs, nt);
}

static void test_classify_kinds(void)
{
    uint8_t buf[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_emoji_info_t *info = NULL;
    robo_font_emoji_analyze_options_t opt;
    emoji_font_spec_t spec;

    /* Name hint alone â€” not an emoji font. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "CoolEmojiDisplay";
    spec.ps = "CoolEmojiDisplay";
    spec.ascii_only = 1;
    spec.ascii_cp = 0x41u;
    n = build_emoji_font(buf, sizeof(buf), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_info_create_from_face(face, NULL, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_emoji_info_kind(info),
                      (int)ROBO_FONT_EMOJI_KIND_NONE);
    ROBO_CHECK(!robo_font_emoji_info_is_classified_emoji(info));
    robo_font_emoji_info_release(info);
    robo_font_face_release(face);
    robo_font_source_release(src);
    info = NULL;
    face = NULL;
    src = NULL;

    /* Incidental: single emoji scalar. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "TextPlusGrin";
    spec.ps = "TextPlusGrin";
    spec.cmap_first = 0x1F600u;
    spec.cmap_last = 0x1F600u;
    n = build_emoji_font(buf, sizeof(buf), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_info_create_from_face(face, NULL, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_emoji_info_kind(info),
                      (int)ROBO_FONT_EMOJI_KIND_INCIDENTAL);
    ROBO_CHECK(robo_font_emoji_info_supports_emoji(info));
    ROBO_CHECK(!robo_font_emoji_info_is_classified_emoji(info));
    ROBO_CHECK(robo_font_emoji_covers_codepoint(info, 0x1F600u));
    robo_font_emoji_info_release(info);
    robo_font_face_release(face);
    robo_font_source_release(src);
    info = NULL;
    face = NULL;
    src = NULL;

    /* Monochrome emoji block. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "MonoEmoji";
    spec.ps = "MonoEmoji";
    spec.cmap_first = 0x1F600u;
    spec.cmap_last = 0x1F60Fu; /* 16 scalars */
    n = build_emoji_font(buf, sizeof(buf), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_emoji(face), ROBO_OK);
    ROBO_CHECK(robo_font_face_emoji(face) != NULL);
    ROBO_CHECK_EQ_INT((int)robo_font_emoji_info_kind(robo_font_face_emoji(face)),
                      (int)ROBO_FONT_EMOJI_KIND_MONOCHROME);
    ROBO_CHECK(robo_font_emoji_info_supports_monochrome_emoji(robo_font_face_emoji(face)));
    ROBO_CHECK(!robo_font_emoji_info_supports_color_emoji(robo_font_face_emoji(face)));
    robo_font_face_release(face);
    robo_font_source_release(src);
    face = NULL;
    src = NULL;

    /* Color COLR/CPAL + emoji coverage. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "ColorEmoji";
    spec.ps = "ColorEmoji";
    spec.cmap_first = 0x1F600u;
    spec.cmap_last = 0x1F603u;
    spec.with_color = 1;
    n = build_emoji_font(buf, sizeof(buf), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_info_create_from_face(face, NULL, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_emoji_info_kind(info),
                      (int)ROBO_FONT_EMOJI_KIND_COLOR);
    ROBO_CHECK(robo_font_emoji_info_supports_color_emoji(info));
    ROBO_CHECK((robo_font_emoji_info_capabilities(info) &
                ROBO_FONT_EMOJI_CAP_VECTOR_COLOR) != 0u);
    robo_font_emoji_info_release(info);
    robo_font_face_release(face);
    robo_font_source_release(src);
    info = NULL;
    face = NULL;
    src = NULL;

    /* Configured override. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "ConfiguredFace";
    spec.ps = "ConfiguredFace";
    spec.ascii_only = 1;
    spec.ascii_cp = 0x41u;
    n = build_emoji_font(buf, sizeof(buf), &spec);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    robo_font_emoji_analyze_options_init(&opt);
    opt.configured_as_emoji = 1;
    ROBO_CHECK_EQ_INT(robo_font_emoji_info_create_from_face(face, &opt, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_emoji_info_kind(info),
                      (int)ROBO_FONT_EMOJI_KIND_CONFIGURED);
    robo_font_emoji_info_release(info);
    robo_font_face_release(face);
    robo_font_source_release(src);

    /* Bitmap capability (sbix) + emoji. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "BitmapEmoji";
    spec.ps = "BitmapEmoji";
    spec.cmap_first = 0x1F600u;
    spec.cmap_last = 0x1F601u;
    spec.with_sbix = 1;
    n = build_emoji_font(buf, sizeof(buf), &spec);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_info_create_from_face(face, NULL, &info), ROBO_OK);
    ROBO_CHECK((robo_font_emoji_info_capabilities(info) & ROBO_FONT_EMOJI_CAP_BITMAP) !=
               0u);
    ROBO_CHECK_EQ_INT((int)robo_font_emoji_info_kind(info),
                      (int)ROBO_FONT_EMOJI_KIND_COLOR);
    robo_font_emoji_info_release(info);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_sequence_foundation(void)
{
    robo_font_emoji_sequence_t seq;
    uint32_t simple[] = {0x1F600u};
    uint32_t with_vs[] = {0x263Au, ROBO_FONT_EMOJI_VS_EMOJI};
    uint32_t skin[] = {0x1F44Du, 0x1F3FDu};
    uint32_t zwj[] = {0x1F469u, ROBO_FONT_EMOJI_ZWJ, 0x1F467u};
    uint32_t flag[] = {0x1F1E6u, 0x1F1E8u};
    uint32_t keycap[] = {0x0031u, 0xFE0Fu, ROBO_FONT_EMOJI_KEYCAP};

    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_set(&seq, simple, 1u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_analyze(&seq), ROBO_OK);
    ROBO_CHECK(robo_font_emoji_content_is_emoji_related(simple, 1u));

    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_set(&seq, with_vs, 2u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_analyze(&seq), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)seq.has_vs_emoji, 1);

    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_set(&seq, skin, 2u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_analyze(&seq), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)seq.has_modifier, 1);

    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_set(&seq, zwj, 3u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_analyze(&seq), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)seq.has_zwj, 1);

    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_set(&seq, flag, 2u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_analyze(&seq), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)seq.has_regional_indicator, 1);

    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_set(&seq, keycap, 3u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_analyze(&seq), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)seq.has_keycap, 1);

    ROBO_CHECK_EQ_INT(
        robo_font_emoji_sequence_set(&seq, simple, ROBO_FONT_EMOJI_MAX_SEQUENCE + 1u),
        ROBO_INVALID_ARGUMENT);
}

static void test_select_and_fallback(void)
{
    uint8_t a[4096], b[4096], c[4096];
    size_t na, nb, nc;
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_result_t dr;
    robo_font_discovery_source_id_t s1, s2, s3;
    robo_font_generic_config_t *cfg = NULL;
    robo_font_generic_resolver_t *gr = NULL;
    robo_font_generic_resolver_options_t gopts;
    robo_font_fallback_resolver_t *fb = NULL;
    robo_font_emoji_select_request_t req;
    robo_font_emoji_selection_t sel;
    robo_font_fallback_result_t fres;
    const char *const emoji_fams[2] = {"ColorEmoji", "MonoEmoji"};
    uint32_t cp = 0x1F600u;
    emoji_font_spec_t spec;
    robo_font_face_t *primary = NULL;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ColorEmoji";
    spec.ps = "ColorEmoji";
    spec.cmap_first = 0x1F600u;
    spec.cmap_last = 0x1F60Fu;
    spec.with_color = 1;
    na = build_emoji_font(a, sizeof(a), &spec);
    ROBO_CHECK(na > 0u);

    memset(&spec, 0, sizeof(spec));
    spec.family = "MonoEmoji";
    spec.ps = "MonoEmoji";
    spec.cmap_first = 0x1F600u;
    spec.cmap_last = 0x1F60Fu;
    nb = build_emoji_font(b, sizeof(b), &spec);
    ROBO_CHECK(nb > 0u);

    memset(&spec, 0, sizeof(spec));
    spec.family = "PlainSans";
    spec.ps = "PlainSans";
    spec.ascii_only = 1;
    spec.ascii_cp = 0x41u;
    nc = build_emoji_font(c, sizeof(c), &spec);
    ROBO_CHECK(nc > 0u);

    ROBO_CHECK_EQ_INT(robo_font_registry_create(&reg), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "a", a, na, &s1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "b", b, nb, &s2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_add_memory_source(reg, "c", c, nc, &s3), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_registry_scan_all(reg, 0u, &dr), ROBO_OK);
    ROBO_CHECK((int)robo_font_registry_registration_count(reg) >= 3);

    ROBO_CHECK_EQ_INT(robo_font_generic_config_create(&cfg), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_font_generic_config_set_mapping(cfg, ROBO_FONT_GENERIC_EMOJI, emoji_fams, 2u),
        ROBO_OK);
    robo_font_generic_resolver_options_init(&gopts);
    gopts.application = cfg;
    ROBO_CHECK_EQ_INT(robo_font_generic_resolver_create(reg, &gopts, &gr), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_fallback_resolver_create(reg, NULL, &fb), ROBO_OK);

    robo_font_emoji_select_request_init(&req);
    req.codepoints = &cp;
    req.codepoint_count = 1u;
    req.color_preference = ROBO_FONT_EMOJI_COLOR_PREFER_COLOR;
    req.use_generic_emoji = 1;
    robo_font_emoji_selection_init(&sel);
    ROBO_CHECK_EQ_INT(robo_font_emoji_select(reg, gr, &req, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK_STR_EQ(sel.family, "ColorEmoji");
    ROBO_CHECK_EQ_INT((int)sel.kind, (int)ROBO_FONT_EMOJI_KIND_COLOR);
    ROBO_CHECK((sel.capabilities & ROBO_FONT_EMOJI_CAP_CONFIGURED) != 0u);
    ROBO_CHECK((sel.capabilities & ROBO_FONT_EMOJI_CAP_COLOR) != 0u);
    robo_font_emoji_selection_clear(&sel);

    req.color_preference = ROBO_FONT_EMOJI_COLOR_REQUIRE_MONO;
    ROBO_CHECK_EQ_INT(robo_font_emoji_select(reg, gr, &req, &sel), ROBO_OK);
    ROBO_CHECK_EQ_INT(sel.found, 1);
    ROBO_CHECK_STR_EQ(sel.family, "MonoEmoji");
    robo_font_emoji_selection_clear(&sel);

    /* Unsupported preference: require color when only mono configured. */
    {
        const char *mono_only[] = {"MonoEmoji"};
        ROBO_CHECK_EQ_INT(robo_font_generic_config_set_mapping(
                              cfg, ROBO_FONT_GENERIC_EMOJI, mono_only, 1u),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_generic_resolver_set_application(gr, cfg), ROBO_OK);
        req.color_preference = ROBO_FONT_EMOJI_COLOR_REQUIRE_COLOR;
        ROBO_CHECK_EQ_INT(robo_font_emoji_select(reg, gr, &req, &sel), ROBO_OK);
        /* ColorEmoji remains in the registry and can still win via coverage. */
        if (sel.found) {
            ROBO_CHECK((sel.capabilities & ROBO_FONT_EMOJI_CAP_COLOR) != 0u);
        }
        robo_font_emoji_selection_clear(&sel);
        ROBO_CHECK_EQ_INT(robo_font_generic_config_set_mapping(
                              cfg, ROBO_FONT_GENERIC_EMOJI, emoji_fams, 2u),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_generic_resolver_set_application(gr, cfg), ROBO_OK);
    }

    /* Fallback: primary PlainSans missing emoji â†’ emoji family. */
    {
        uint32_t i;
        uint32_t n = robo_font_registry_registration_count(reg);
        for (i = 0u; i < n; i++) {
            const robo_font_registration_t *rec = NULL;
            ROBO_CHECK_EQ_INT(robo_font_registry_registration_at(reg, i, &rec), ROBO_OK);
            if (rec && strcmp(rec->family, "PlainSans") == 0) {
                primary = rec->face;
                break;
            }
        }
    }
    ROBO_CHECK(primary != NULL);
    req.color_preference = ROBO_FONT_EMOJI_COLOR_ANY;
    robo_font_fallback_result_init(&fres);
    ROBO_CHECK_EQ_INT(
        robo_font_emoji_resolve_fallback(reg, gr, fb, primary, &req, &sel, &fres),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(fres.found, 1);
    ROBO_CHECK_EQ_INT(fres.used_primary, 0);
    {
        const robo_font_registration_t *rec = NULL;
        ROBO_CHECK_EQ_INT(robo_font_registry_find_by_face_id(reg, fres.face_id, &rec),
                          ROBO_OK);
        ROBO_CHECK(rec != NULL);
        ROBO_CHECK(strcmp(rec->family, "ColorEmoji") == 0 ||
                   strcmp(rec->family, "MonoEmoji") == 0);
    }
    robo_font_fallback_result_clear(&fres);
    robo_font_emoji_selection_clear(&sel);

    /* Bench: repeated select. */
    {
        uint32_t i;
        for (i = 0u; i < 200u; i++) {
            ROBO_CHECK_EQ_INT(robo_font_emoji_select(reg, gr, &req, &sel), ROBO_OK);
            robo_font_emoji_selection_clear(&sel);
        }
        printf("font.emoji.bench.select hits=200\n");
    }

    robo_font_fallback_resolver_destroy(fb);
    robo_font_generic_resolver_destroy(gr);
    robo_font_generic_config_release(cfg);
    robo_font_registry_destroy(reg);
}

static void test_fuzz_and_seq_support(void)
{
    uint8_t junk[64];
    uint32_t i;
    uint8_t buf[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_emoji_info_t *info = NULL;
    robo_font_emoji_sequence_t seq;
    uint32_t zwj[] = {0x1F600u, ROBO_FONT_EMOJI_ZWJ, 0x1F602u};
    emoji_font_spec_t spec;

    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 19u + 3u);
    }
    ROBO_CHECK_EQ_INT(robo_font_emoji_fuzz_parse(junk, sizeof(junk)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_fuzz_parse(NULL, 0u), ROBO_OK);

    memset(&spec, 0, sizeof(spec));
    spec.family = "SeqFont";
    spec.ps = "SeqFont";
    spec.cmap_first = 0x1F600u;
    spec.cmap_last = 0x1F60Fu;
    n = build_emoji_font(buf, sizeof(buf), &spec);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_info_create_from_face(face, NULL, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_set(&seq, zwj, 3u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_analyze(&seq), ROBO_OK);
    /* ZWJ â†’ UNKNOWN even when components covered. */
    ROBO_CHECK_EQ_INT((int)robo_font_emoji_info_supports_sequence(info, &seq),
                      (int)ROBO_FONT_EMOJI_SEQ_SUPPORT_UNKNOWN);
    {
        uint32_t one[] = {0x1F600u};
        ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_set(&seq, one, 1u), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_emoji_sequence_analyze(&seq), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_font_emoji_info_supports_sequence(info, &seq),
                          (int)ROBO_FONT_EMOJI_SEQ_SUPPORT_COMPONENTS);
    }
    robo_font_emoji_info_release(info);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

void robo_test_register_font_emoji(void)
{
    robo_test_register("font.emoji.classify", test_classify_kinds);
    robo_test_register("font.emoji.sequence", test_sequence_foundation);
    robo_test_register("font.emoji.select_fallback", test_select_and_fallback);
    robo_test_register("font.emoji.fuzz_seq", test_fuzz_and_seq_support);
}
