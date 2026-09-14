/*
 * RoboFont color-font suite (Goal 5.19).
 * Synthetic SFNT fixtures — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontColor.h"
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
static void put_i32(uint8_t **p, int32_t v) { put_u32(p, (uint32_t)v); }
static void put_i64(uint8_t **p, int64_t v)
{
    put_u32(p, (uint32_t)((uint64_t)v >> 32));
    put_u32(p, (uint32_t)((uint64_t)v));
}
static void put_offset24(uint8_t **p, uint32_t off)
{
    put_u8(p, (uint8_t)(off >> 16));
    put_u8(p, (uint8_t)(off >> 8));
    put_u8(p, (uint8_t)off);
}
static void put_tag(uint8_t **p, uint32_t tag)
{
    put_u8(p, (uint8_t)(tag >> 24));
    put_u8(p, (uint8_t)(tag >> 16));
    put_u8(p, (uint8_t)(tag >> 8));
    put_u8(p, (uint8_t)tag);
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

static size_t build_cmap_fmt12_range(uint8_t *buf, size_t cap, uint32_t first,
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

/* CPAL: 2 palettes × 2 entries; known BGRA colors. */
static size_t build_cpal_2x2(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 0); /* version */
    put_u16(&p, 2); /* numPaletteEntries */
    put_u16(&p, 2); /* numPalettes */
    put_u16(&p, 4); /* numColorRecords */
    put_u32(&p, 16u); /* colorRecordsArrayOffset */
    put_u16(&p, 0); /* palette 0 first index */
    put_u16(&p, 2); /* palette 1 first index */
    /* BGRA records */
    put_u8(&p, 0);
    put_u8(&p, 0);
    put_u8(&p, 255);
    put_u8(&p, 255); /* red */
    put_u8(&p, 255);
    put_u8(&p, 0);
    put_u8(&p, 0);
    put_u8(&p, 255); /* green */
    put_u8(&p, 255);
    put_u8(&p, 0);
    put_u8(&p, 0);
    put_u8(&p, 255); /* blue */
    put_u8(&p, 0);
    put_u8(&p, 255);
    put_u8(&p, 255);
    put_u8(&p, 255); /* yellow */
    return (size_t)(p - buf);
}

/* CPAL with invalid palette index array. */
static size_t build_cpal_bad(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 0);
    put_u16(&p, 2);
    put_u16(&p, 2);
    put_u16(&p, 2);
    put_u32(&p, 16u);
    put_u16(&p, 0);
    put_u16(&p, 5); /* starts past numColorRecords */
    put_u8(&p, 0);
    put_u8(&p, 0);
    put_u8(&p, 255);
    put_u8(&p, 255);
    put_u8(&p, 0);
    put_u8(&p, 255);
    put_u8(&p, 0);
    put_u8(&p, 255);
    return (size_t)(p - buf);
}

/* COLR v0: gid 1 → 2 layers. */
static size_t build_colr_v0_layers(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 0); /* version */
    put_u16(&p, 1); /* numBaseGlyphRecords */
    put_u32(&p, 14u); /* baseGlyphRecordsOffset */
    put_u32(&p, 20u); /* layerRecordsOffset */
    put_u16(&p, 2); /* numLayerRecords */
    /* base glyph record */
    put_u16(&p, 1); /* gid */
    put_u16(&p, 0); /* firstLayerIndex */
    put_u16(&p, 2); /* numLayers */
    /* layer records */
    put_u16(&p, 2); /* layer gid */
    put_u16(&p, 0); /* palette index */
    put_u16(&p, 3); /* layer gid */
    put_u16(&p, 1); /* palette index */
    return (size_t)(p - buf);
}

/* COLR v1: gid 1 → PaintSolid (format 2). */
static size_t build_colr_v1_solid(uint8_t *buf, int recursive_colr_glyph)
{
    uint8_t *p = buf;
    const uint32_t base_list_off = 34u;
    const uint32_t paint_off = 39u;
    put_u16(&p, 1); /* version */
    put_u16(&p, 1); /* numBaseGlyphRecords */
    put_u32(&p, 14u);
    put_u32(&p, 14u);
    put_u16(&p, 0); /* numLayerRecords */
    put_u32(&p, base_list_off); /* baseGlyphListOffset */
    put_u32(&p, 0); /* layerListOffset */
    put_u32(&p, 0); /* clipListOffset */
    put_u32(&p, 0); /* varIndexMapOffset */
    put_u32(&p, 0); /* itemVariationStoreOffset */
    /* base glyph list record */
    put_u16(&p, 1); /* gid */
    put_offset24(&p, paint_off);
    /* paint */
    if (recursive_colr_glyph) {
        put_u8(&p, 11); /* PaintColrGlyph */
        put_u16(&p, 1); /* self gid */
    } else {
        put_u8(&p, 2); /* PaintSolid */
        put_u16(&p, 0); /* paletteIndex */
        put_i16(&p, 0x4000); /* alpha = 1.0 */
    }
    return (size_t)(p - buf);
}

/* sbix: version 1, 1 strike ppem=32, numGlyphs=2, glyph 1 has png stub. */
static size_t build_sbix_png(uint8_t *buf, uint16_t num_glyphs)
{
    uint8_t *p = buf;
    const uint32_t strike_off = 12u;
    const uint32_t glyph_array_off = 20u;
    const uint32_t image_off = 32u;
    const uint32_t image_end = 44u;
    put_u16(&p, 1); /* version */
    put_u16(&p, 1); /* flags */
    put_u32(&p, 1u); /* numStrikes */
    put_u32(&p, strike_off);
    /* strike */
    put_u16(&p, 32); /* ppem */
    put_u16(&p, 72); /* ppi */
    put_u32(&p, glyph_array_off);
    /* glyph offset array (numGlyphs + 1) */
    {
        uint16_t i;
        for (i = 0u; i <= num_glyphs; i++) {
            if (i == 0u) {
                put_u32(&p, 0u);
            } else if (i == 1u) {
                put_u32(&p, image_off - glyph_array_off);
            } else {
                put_u32(&p, image_end - glyph_array_off);
            }
        }
    }
    /* glyph 1 image */
    put_tag(&p, ROBO_FONT_TAG('p', 'n', 'g', ' '));
    put_u8(&p, 0x89);
    put_u8(&p, 'P');
    put_u8(&p, 'N');
    put_u8(&p, 'G');
    put_u8(&p, 0x0D);
    put_u8(&p, 0x0A);
    put_u8(&p, 0x1A);
    put_u8(&p, 0x0A);
    return (size_t)(p - buf);
}

/* Minimal CBLC: header + one 48-byte BitmapSize + stub IndexSubtableList. */
static size_t build_cblc_min(uint8_t *buf)
{
    uint8_t *p = buf;
    const uint32_t index_array_off = 56u; /* after header(8) + BitmapSize(48) */
    uint8_t i;

    put_u16(&p, 3); /* majorVersion (OpenType CBLC = 3) */
    put_u16(&p, 0); /* minor */
    put_u32(&p, 1u); /* numSizes */

    /* BitmapSize (48 bytes) */
    put_u32(&p, index_array_off); /* indexSubtableListOffset */
    put_u32(&p, 8u);              /* indexSubtableListSize */
    put_u32(&p, 1u);              /* numberOfIndexSubtables */
    put_u32(&p, 0u);              /* colorRef */
    for (i = 0u; i < 24u; i++) {  /* hori + vert SbitLineMetrics */
        put_u8(&p, 0);
    }
    put_u16(&p, 0); /* startGlyphIndex */
    put_u16(&p, 1); /* endGlyphIndex */
    put_u8(&p, 32); /* ppemX */
    put_u8(&p, 32); /* ppemY */
    put_u8(&p, 32); /* bitDepth (BGRA) */
    put_u8(&p, 1);  /* flags */

    /* IndexSubtableList stub: numTables=1 + one IndexSubHeader offset */
    put_u32(&p, 1u); /* numTables (some fonts use uint32; OT uses uint16 —
                      * keep 8 bytes total as declared size) */
    put_u32(&p, 0u);
    return (size_t)(p - buf);
}

static size_t build_cbdt_stub(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u8(&p, 0x01);
    put_u8(&p, 0x02);
    put_u8(&p, 0x03);
    put_u8(&p, 0x04);
    return (size_t)(p - buf);
}

static size_t build_svg_doc(uint8_t *buf, size_t cap, const char *svg_bytes,
                            size_t svg_len, int oversized_claim)
{
    uint8_t *p = buf;
    const uint32_t doc_list_off = 6u;
    /* DocumentList at 6: numEntries(2) + record(12) = 14 → payload at 20 */
    const uint32_t payload_off_from_list = 14u;
    size_t need;
    if (!svg_bytes || cap < 20u + svg_len) {
        return 0u;
    }
    need = 20u + svg_len;
    put_u16(&p, 0); /* version */
    put_u32(&p, doc_list_off);
    put_u16(&p, 1); /* numEntries */
    put_u16(&p, 1); /* startGlyphID */
    put_u16(&p, 1); /* endGlyphID */
    put_u32(&p, payload_off_from_list); /* offset from DocumentList */
    if (oversized_claim) {
        put_u32(&p, (uint32_t)(ROBO_FONT_COLOR_MAX_SVG_PAYLOAD + 1u));
        return 20u; /* truncated payload */
    }
    put_u32(&p, (uint32_t)svg_len);
    memcpy(buf + 20u, svg_bytes, svg_len);
    return need;
}

static size_t build_fvar_stub(uint8_t *buf)
{
    uint8_t *p = buf;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u16(&p, 16);
    put_u16(&p, 0);
    put_u16(&p, 1); /* axisCount */
    put_u16(&p, 20);
    put_u16(&p, 0);
    put_u16(&p, 4);
    put_u32(&p, ROBO_FONT_TAG_WGHT);
    put_i32(&p, 6553600); /* 100 */
    put_i32(&p, 26214400); /* 400 */
    put_i32(&p, 58982400); /* 900 */
    put_u16(&p, 0);
    put_u16(&p, 256);
    return (size_t)(p - buf);
}

enum {
    COLOR_FONT_NONE = 0,
    COLOR_FONT_CPAL = 1u << 0,
    COLOR_FONT_COLR_V0 = 1u << 1,
    COLOR_FONT_COLR_V1 = 1u << 2,
    COLOR_FONT_COLR_V1_RECURSE = 1u << 3,
    COLOR_FONT_SBIX = 1u << 4,
    COLOR_FONT_CBLC = 1u << 5,
    COLOR_FONT_CBDT = 1u << 6,
    COLOR_FONT_SVG = 1u << 7,
    COLOR_FONT_EMOJI_CMAP = 1u << 8,
    COLOR_FONT_FVAR = 1u << 9,
    COLOR_FONT_SVG_OVERSIZED = 1u << 10,
    COLOR_FONT_BAD_CPAL = 1u << 11
};

typedef struct {
    const char *family;
    const char *ps;
    uint16_t num_glyphs;
    uint32_t flags;
    uint32_t emoji_first;
    uint32_t emoji_last;
} color_font_spec_t;

static size_t build_color_font(uint8_t *out, size_t cap, const color_font_spec_t *spec)
{
    uint8_t head[64];
    uint8_t hhea[64];
    uint8_t maxp[64];
    uint8_t hmtx[256];
    uint8_t loca[256];
    uint8_t post[64];
    uint8_t os2[128];
    uint8_t name[512];
    uint8_t cmap[1024];
    uint8_t cpal[64];
    uint8_t colr[128];
    uint8_t sbix[128];
    uint8_t cblc[128];
    uint8_t cbdt[32];
    uint8_t svg[512];
    uint8_t fvar[128];
    synth_table_t tabs[24];
    uint32_t nt = 0u;
    size_t nn;
    size_t nc = 0u;
    uint16_t glyphs;
    static const char k_svg[] = "<svg xmlns=\"http://www.w3.org/2000/svg\"/>";

    if (!spec || spec->num_glyphs < 2u) {
        return 0u;
    }
    glyphs = spec->num_glyphs;
    nn = build_name_table(name, sizeof(name), spec->family, spec->ps);
    if (nn == 0u) {
        return 0u;
    }
    if (spec->flags & COLOR_FONT_EMOJI_CMAP) {
        nc = build_cmap_fmt12_range(cmap, sizeof(cmap), spec->emoji_first,
                                    spec->emoji_last, 1u);
    } else {
        nc = build_cmap_fmt4_ascii(cmap, 0x41u, 1u);
    }
    if (nc == 0u) {
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

    if (spec->flags & (COLOR_FONT_CPAL | COLOR_FONT_BAD_CPAL)) {
        size_t cpal_len = (spec->flags & COLOR_FONT_BAD_CPAL)
                              ? build_cpal_bad(cpal)
                              : build_cpal_2x2(cpal);
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_CPAL, cpal, (uint32_t)cpal_len};
    }
    if (spec->flags & COLOR_FONT_COLR_V0) {
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_COLR, colr,
                                      (uint32_t)build_colr_v0_layers(colr)};
    }
    if (spec->flags & (COLOR_FONT_COLR_V1 | COLOR_FONT_COLR_V1_RECURSE)) {
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_COLR, colr,
                                      (uint32_t)build_colr_v1_solid(
                                          colr, (spec->flags & COLOR_FONT_COLR_V1_RECURSE) ? 1 : 0)};
    }
    if (spec->flags & COLOR_FONT_SBIX) {
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_SBIX, sbix,
                                      (uint32_t)build_sbix_png(sbix, glyphs)};
    }
    if (spec->flags & COLOR_FONT_CBLC) {
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_CBLC, cblc,
                                      (uint32_t)build_cblc_min(cblc)};
    }
    if (spec->flags & COLOR_FONT_CBDT) {
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_CBDT, cbdt,
                                      (uint32_t)build_cbdt_stub(cbdt)};
    }
    if (spec->flags & COLOR_FONT_SVG) {
        size_t sn = build_svg_doc(svg, sizeof(svg), k_svg, sizeof(k_svg) - 1u,
                                  (spec->flags & COLOR_FONT_SVG_OVERSIZED) ? 1 : 0);
        if (sn == 0u) {
            return 0u;
        }
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_SVG, svg, (uint32_t)sn};
    }
    if (spec->flags & COLOR_FONT_FVAR) {
        tabs[nt++] = (synth_table_t){ROBO_FONT_TAG_FVAR, fvar,
                                      (uint32_t)build_fvar_stub(fvar)};
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

static void test_mono(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_color_info_t *info = NULL;
    color_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "MonoSans";
    spec.ps = "MonoSans";
    spec.num_glyphs = 2u;
    spec.flags = COLOR_FONT_NONE;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    load_face(font, n, &src, &face);

    ROBO_CHECK_EQ_INT(robo_font_face_parse_color(face), ROBO_NOT_FOUND);
    ROBO_CHECK(robo_font_face_color(face) == NULL);

    ROBO_CHECK_EQ_INT(robo_font_face_parse_sfnt(face), ROBO_OK);
    sfnt = (robo_font_sfnt_t *)robo_font_face_sfnt(face);
    ROBO_CHECK(sfnt != NULL);
    ROBO_CHECK_EQ_INT(robo_font_color_info_create(sfnt, NULL, &info), ROBO_NOT_FOUND);
    ROBO_CHECK(info == NULL);

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_colr_cpal(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_color_info_t *info = NULL;
    robo_font_color_palette_t pal;
    robo_font_color_rgba_t rgba;
    robo_font_color_glyph_t glyph;
    const robo_font_color_glyph_layer_t *layers = NULL;
    uint32_t layer_count = 0u;
    robo_font_color_palette_selection_t sel;
    color_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ColrCpal";
    spec.ps = "ColrCpal";
    spec.num_glyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_color_info_create_from_face(face, NULL, &info), ROBO_OK);
    ROBO_CHECK(info != NULL);

    ROBO_CHECK((robo_font_color_info_capabilities(info) & ROBO_FONT_COLOR_CAP_COLR) != 0u);
    ROBO_CHECK((robo_font_color_info_capabilities(info) & ROBO_FONT_COLOR_CAP_CPAL) != 0u);
    ROBO_CHECK((robo_font_color_info_capabilities(info) & ROBO_FONT_COLOR_CAP_COLR_V0) != 0u);
    ROBO_CHECK(robo_font_color_info_is_color_capable(info));
    ROBO_CHECK_EQ_INT((int)robo_font_color_info_colr_version(info), 0);
    ROBO_CHECK_EQ_INT((int)robo_font_color_info_palette_count(info), 2);

    ROBO_CHECK_EQ_INT(robo_font_color_info_palette(info, 0u, &pal), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)pal.entry_count, 2);
    ROBO_CHECK_EQ_INT(robo_font_color_info_palette_entry(info, 0u, 0u, &rgba), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)rgba.r, 255);
    ROBO_CHECK_EQ_INT((int)rgba.g, 0);
    ROBO_CHECK_EQ_INT((int)rgba.b, 0);
    ROBO_CHECK_EQ_INT((int)rgba.a, 255);
    ROBO_CHECK_EQ_INT(robo_font_color_info_palette_entry(info, 1u, 1u, &rgba), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)rgba.r, 255);
    ROBO_CHECK_EQ_INT((int)rgba.g, 255);
    ROBO_CHECK_EQ_INT((int)rgba.b, 0);

    ROBO_CHECK_EQ_INT(robo_font_color_info_glyph(info, 1u, &glyph), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)glyph.format, (int)ROBO_FONT_COLOR_GLYPH_LAYERS);
    ROBO_CHECK_EQ_INT((int)glyph.layer_count, 2);
    ROBO_CHECK_EQ_INT(robo_font_color_info_glyph_layers(info, 1u, &layers, &layer_count),
                      ROBO_OK);
    ROBO_CHECK(layers != NULL);
    ROBO_CHECK_EQ_INT((int)layer_count, 2);
    ROBO_CHECK_EQ_INT((int)layers[0].glyph_id, 2);
    ROBO_CHECK_EQ_INT((int)layers[1].glyph_id, 3);

    robo_font_color_palette_selection_init(&sel);
    sel.palette_index = 0u;
    ROBO_CHECK_EQ_INT(robo_font_color_resolve_palette_color(info, &sel, 1u, &rgba), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)rgba.g, 0);
    ROBO_CHECK_EQ_INT(
        robo_font_color_resolve_palette_color(info, &sel, ROBO_FONT_COLOR_FOREGROUND_PALETTE,
                                              &rgba),
        ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_font_color_info_palette(info, 99u, &pal), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_font_color_info_palette_entry(info, 0u, 99u, &rgba), ROBO_NOT_FOUND);

    robo_font_color_info_release(info);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_colr_v1_paint(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_color_info_t *info = NULL;
    robo_font_color_glyph_t glyph;
    const robo_font_color_paint_t *nodes = NULL;
    uint32_t node_count = 0u;
    uint32_t root = 0u;
    color_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ColrV1";
    spec.ps = "ColrV1";
    spec.num_glyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V1;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_color_info_create_from_face(face, NULL, &info), ROBO_OK);

    ROBO_CHECK((robo_font_color_info_capabilities(info) & ROBO_FONT_COLOR_CAP_COLR_V1) != 0u);
    ROBO_CHECK_EQ_INT((int)robo_font_color_info_colr_version(info), 1);
    ROBO_CHECK_EQ_INT(robo_font_color_info_glyph(info, 1u, &glyph), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)glyph.format, (int)ROBO_FONT_COLOR_GLYPH_PAINT);

    ROBO_CHECK_EQ_INT(robo_font_color_info_glyph_paint(info, 1u, &nodes, &node_count, &root),
                      ROBO_OK);
    ROBO_CHECK(nodes != NULL);
    ROBO_CHECK(node_count >= 1u);
    ROBO_CHECK(root < node_count);
    ROBO_CHECK_EQ_INT((int)nodes[root].op, (int)ROBO_FONT_COLOR_PAINT_SOLID);
    ROBO_CHECK_EQ_INT((int)nodes[root].format, 2);

    robo_font_color_info_release(info);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_sbix(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_color_info_t *info = NULL;
    robo_font_color_glyph_t glyph;
    robo_font_color_glyph_image_t img;
    color_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "SbixFont";
    spec.ps = "SbixFont";
    spec.num_glyphs = 2u;
    spec.flags = COLOR_FONT_SBIX;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_color_info_create_from_face(face, NULL, &info), ROBO_OK);

    ROBO_CHECK((robo_font_color_info_capabilities(info) & ROBO_FONT_COLOR_CAP_SBIX) != 0u);
    ROBO_CHECK_EQ_INT((int)robo_font_color_info_sbix_strike_count(info), 1);
    ROBO_CHECK_EQ_INT(robo_font_color_info_glyph_image(info, 1u, 32u, &img), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)img.ppem_x, 32);
    ROBO_CHECK_EQ_INT((int)img.graphic_type, (int)ROBO_FONT_TAG('p', 'n', 'g', ' '));
    ROBO_CHECK(img.data.size > 0u);

    ROBO_CHECK_EQ_INT(robo_font_color_info_glyph(info, 1u, &glyph), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)glyph.format, (int)ROBO_FONT_COLOR_GLYPH_BITMAP);
    ROBO_CHECK_EQ_INT((int)glyph.has_image, 1);

    robo_font_color_info_release(info);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_cblc_cbdt(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_color_info_t *info = NULL;
    robo_font_color_glyph_image_t img;
    color_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "CbdtFont";
    spec.ps = "CbdtFont";
    spec.num_glyphs = 2u;
    spec.flags = COLOR_FONT_CBLC | COLOR_FONT_CBDT;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_color_info_create_from_face(face, NULL, &info), ROBO_OK);

    ROBO_CHECK((robo_font_color_info_capabilities(info) & ROBO_FONT_COLOR_CAP_CBLC) != 0u);
    ROBO_CHECK((robo_font_color_info_capabilities(info) & ROBO_FONT_COLOR_CAP_CBDT) != 0u);
    ROBO_CHECK_EQ_INT((int)robo_font_color_info_cblc_strike_count(info), 1);
    /* Image lookup may be limited; must not crash. */
    (void)robo_font_color_info_glyph_image(info, 1u, 32u, &img);

    /* Malformed CBLC: corrupt last byte; must not crash. */
    {
        uint8_t bad[4096];
        size_t bn;
        robo_font_source_t *bsrc = NULL;
        robo_font_sfnt_t *sfnt = NULL;
        robo_font_color_info_t *binfo = NULL;
        memset(&spec, 0, sizeof(spec));
        spec.family = "BadCblc";
        spec.ps = "BadCblc";
        spec.num_glyphs = 2u;
        spec.flags = COLOR_FONT_CBLC | COLOR_FONT_CBDT;
        bn = build_color_font(bad, sizeof(bad), &spec);
        ROBO_CHECK(bn > 0u);
        bad[bn - 1u] = 0xFFu;
        ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(bad, bn, &bsrc), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(bsrc, 0u, 0u, &sfnt), ROBO_OK);
        (void)robo_font_color_info_create(sfnt, NULL, &binfo);
        if (binfo) {
            robo_font_color_info_release(binfo);
        }
        robo_font_sfnt_release(sfnt);
        robo_font_source_release(bsrc);
    }

    robo_font_color_info_release(info);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_svg(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_color_info_t *info = NULL;
    robo_font_color_glyph_t glyph;
    robo_font_color_glyph_svg_t svg;
    color_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "SvgFont";
    spec.ps = "SvgFont";
    spec.num_glyphs = 2u;
    spec.flags = COLOR_FONT_SVG;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_color_info_create_from_face(face, NULL, &info), ROBO_OK);

    ROBO_CHECK((robo_font_color_info_capabilities(info) & ROBO_FONT_COLOR_CAP_SVG) != 0u);
    ROBO_CHECK_EQ_INT((int)robo_font_color_info_svg_document_count(info), 1);
    ROBO_CHECK_EQ_INT(robo_font_color_info_glyph_svg(info, 1u, &svg), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)svg.start_glyph_id, 1);
    ROBO_CHECK_EQ_INT((int)svg.end_glyph_id, 1);
    ROBO_CHECK(svg.document.size > 0u);
    ROBO_CHECK_EQ_INT((int)svg.has_external_ref_hint, 1);

    ROBO_CHECK_EQ_INT(robo_font_color_info_glyph(info, 1u, &glyph), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)glyph.format, (int)ROBO_FONT_COLOR_GLYPH_SVG);

    /* Oversized / truncated SVG payload handled without crash. */
    {
        uint8_t big[4096];
        size_t bn;
        robo_font_source_t *bsrc = NULL;
        robo_font_face_t *bface = NULL;
        robo_font_color_info_t *binfo = NULL;
        memset(&spec, 0, sizeof(spec));
        spec.family = "SvgBig";
        spec.ps = "SvgBig";
        spec.num_glyphs = 2u;
        spec.flags = COLOR_FONT_SVG | COLOR_FONT_SVG_OVERSIZED;
        bn = build_color_font(big, sizeof(big), &spec);
        ROBO_CHECK(bn > 0u);
        load_face(big, bn, &bsrc, &bface);
        (void)robo_font_color_info_create_from_face(bface, NULL, &binfo);
        if (binfo) {
            (void)robo_font_color_info_glyph_svg(binfo, 1u, &svg);
            robo_font_color_info_release(binfo);
        }
        robo_font_face_release(bface);
        robo_font_source_release(bsrc);
    }

    /* Malformed SVG header. */
    {
        uint8_t junk[256];
        robo_font_source_t *jsrc = NULL;
        robo_font_sfnt_t *sfnt = NULL;
        robo_font_color_info_t *bad_info = NULL;
        synth_table_t tabs[2];
        uint8_t svg_bad[8];
        uint8_t *sp = svg_bad;
        size_t jn;
        put_u16(&sp, 0);
        put_u32(&sp, 9999u);
        tabs[0] = (synth_table_t){ROBO_FONT_TAG_HEAD, junk, 0u};
        tabs[1] = (synth_table_t){ROBO_FONT_TAG_SVG, svg_bad, 6u};
        jn = build_sfnt(junk, sizeof(junk), tabs, 2u);
        ROBO_CHECK(jn > 0u);
        ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(junk, jn, &jsrc), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(jsrc, 0u, 0u, &sfnt), ROBO_OK);
        (void)robo_font_color_info_create(sfnt, NULL, &bad_info);
        if (bad_info) {
            robo_font_color_info_release(bad_info);
        }
        robo_font_sfnt_release(sfnt);
        robo_font_source_release(jsrc);
    }

    robo_font_color_info_release(info);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_multi(void)
{
    uint8_t font[8192];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_color_info_t *info = NULL;
    uint32_t caps;
    color_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "MultiColor";
    spec.ps = "MultiColor";
    spec.num_glyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0 | COLOR_FONT_SBIX;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_color(face), ROBO_OK);
    ROBO_CHECK(robo_font_face_color(face) != NULL);

    info = (robo_font_color_info_t *)robo_font_face_color(face);
    caps = robo_font_color_info_capabilities(info);
    ROBO_CHECK((caps & ROBO_FONT_COLOR_CAP_COLR) != 0u);
    ROBO_CHECK((caps & ROBO_FONT_COLOR_CAP_CPAL) != 0u);
    ROBO_CHECK((caps & ROBO_FONT_COLOR_CAP_SBIX) != 0u);
    ROBO_CHECK(robo_font_color_info_is_color_capable(info));

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_emoji(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_emoji_info_t *emoji = NULL;
    robo_font_color_info_t *color = NULL;
    color_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ColorEmoji";
    spec.ps = "ColorEmoji";
    spec.num_glyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0 | COLOR_FONT_EMOJI_CMAP;
    spec.emoji_first = 0x1F600u;
    spec.emoji_last = 0x1F603u;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    load_face(font, n, &src, &face);

    ROBO_CHECK_EQ_INT(robo_font_emoji_info_create_from_face(face, NULL, &emoji), ROBO_OK);
    ROBO_CHECK(robo_font_emoji_info_supports_color_emoji(emoji));
    ROBO_CHECK_EQ_INT((int)robo_font_emoji_info_kind(emoji),
                      (int)ROBO_FONT_EMOJI_KIND_COLOR);

    ROBO_CHECK_EQ_INT(robo_font_face_parse_color(face), ROBO_OK);
    ROBO_CHECK(robo_font_face_color(face) != NULL);
    color = (robo_font_color_info_t *)robo_font_face_color(face);
    ROBO_CHECK(robo_font_color_info_is_color_capable(color));

    robo_font_emoji_info_release(emoji);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_variation(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_color_info_t *info = NULL;
    const robo_font_variation_coords_t *coords;
    color_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "VarColor";
    spec.ps = "VarColor";
    spec.num_glyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0 | COLOR_FONT_FVAR;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    load_face(font, n, &src, &face);

    ROBO_CHECK_EQ_INT(robo_font_color_info_create_from_face(face, NULL, &info), ROBO_OK);
    coords = robo_font_color_info_variation_coords(info);
    ROBO_CHECK(coords == NULL);
    robo_font_color_info_release(info);
    info = NULL;

    ROBO_CHECK_EQ_INT(robo_font_face_parse_color(face), ROBO_OK);
    ROBO_CHECK(robo_font_face_color(face) != NULL);
    ROBO_CHECK(robo_font_color_info_variation_coords(robo_font_face_color(face)) == NULL);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_malformed(void)
{
    uint8_t font[4096];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_color_info_t *info = NULL;
    const robo_font_color_paint_t *nodes = NULL;
    uint32_t node_count = 0u;
    uint32_t root = 0u;
    color_font_spec_t spec;

    /* Truncated COLR. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "TruncColr";
    spec.ps = "TruncColr";
    spec.num_glyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 4u);
    font[n - 4u] = 0u;
    font[n - 3u] = 0u;
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0u, 0u, &sfnt), ROBO_OK);
    (void)robo_font_color_info_create(sfnt, NULL, &info);
    if (info) {
        robo_font_color_info_release(info);
        info = NULL;
    }
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
    src = NULL;
    sfnt = NULL;

    /* Bad CPAL palette indices (second palette starts past color records). */
    memset(&spec, 0, sizeof(spec));
    spec.family = "BadCpal";
    spec.ps = "BadCpal";
    spec.num_glyphs = 2u;
    spec.flags = COLOR_FONT_BAD_CPAL | COLOR_FONT_COLR_V0;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_sfnt_parse(src, 0u, 0u, &sfnt), ROBO_OK);
    (void)robo_font_color_info_create(sfnt, NULL, &info);
    if (info) {
        robo_font_color_info_release(info);
        info = NULL;
    }
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
    src = NULL;
    sfnt = NULL;

    /* Recursive COLR v1 paint — no crash. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "RecurPaint";
    spec.ps = "RecurPaint";
    spec.num_glyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V1 | COLOR_FONT_COLR_V1_RECURSE;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, NULL), ROBO_INVALID_ARGUMENT);
    {
        robo_font_face_t *face = NULL;
        ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0u, &face), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_color_info_create_from_face(face, NULL, &info), ROBO_OK);
        (void)robo_font_color_info_glyph_paint(info, 1u, &nodes, &node_count, &root);
        robo_font_color_info_release(info);
        info = NULL;
        robo_font_face_release(face);
    }
    robo_font_source_release(src);
}

static void test_fuzz(void)
{
    uint8_t junk[128];
    uint8_t font[8192];
    size_t n;
    uint32_t i;
    color_font_spec_t spec;

    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 37u + 11u);
    }
    ROBO_CHECK_EQ_INT(robo_font_color_fuzz_parse(NULL, 0u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_color_fuzz_parse(junk, sizeof(junk)), ROBO_OK);

    memset(&spec, 0, sizeof(spec));
    spec.family = "FuzzColor";
    spec.ps = "FuzzColor";
    spec.num_glyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0 | COLOR_FONT_SBIX;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_color_fuzz_parse(font, n), ROBO_OK);

    /* Random-ish buffer: perturb valid font. */
    for (i = 0u; i < 64u; i++) {
        font[i % n] ^= (uint8_t)(i * 13u + 7u);
        (void)robo_font_color_fuzz_parse(font, n);
    }
}

static void test_bench(void)
{
    uint8_t font[8192];
    size_t n;
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_color_info_t *info = NULL;
    robo_font_color_rgba_t rgba;
    robo_font_color_glyph_t glyph;
    const robo_font_color_paint_t *nodes = NULL;
    uint32_t node_count = 0u;
    uint32_t root = 0u;
    robo_font_color_palette_selection_t sel;
    int hits = 0;
    int i;
    color_font_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "BenchColor";
    spec.ps = "BenchColor";
    spec.num_glyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0 | COLOR_FONT_SBIX;
    n = build_color_font(font, sizeof(font), &spec);
    ROBO_CHECK(n > 0u);
    load_face(font, n, &src, &face);
    ROBO_CHECK_EQ_INT(robo_font_color_info_create_from_face(face, NULL, &info), ROBO_OK);
    robo_font_color_palette_selection_init(&sel);

    for (i = 0; i < 200; i++) {
        if (robo_font_color_info_palette_count(info) > 0u) {
            hits++;
        }
        sel.palette_index = (uint16_t)(i % 2u);
        if (robo_font_color_info_palette_entry(info, sel.palette_index, 0u, &rgba) == ROBO_OK) {
            hits++;
        }
        if (robo_font_color_resolve_palette_color(info, &sel, 0u, &rgba) == ROBO_OK) {
            hits++;
        }
        if (robo_font_color_info_glyph(info, 1u, &glyph) == ROBO_OK) {
            hits++;
        }
        if (robo_font_color_info_glyph_layers(info, 1u, NULL, NULL) == ROBO_OK) {
            hits++;
        }
        if (robo_font_color_info_glyph_paint(info, 1u, &nodes, &node_count, &root) == ROBO_OK) {
            hits++;
        }
    }
    printf("font.color.bench hits=%d\n", hits);
    ROBO_CHECK(hits >= 200);

    robo_font_color_info_release(info);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

void robo_test_register_font_color(void)
{
    robo_test_register("font.color.mono", test_mono);
    robo_test_register("font.color.colr_cpal", test_colr_cpal);
    robo_test_register("font.color.colr_v1_paint", test_colr_v1_paint);
    robo_test_register("font.color.sbix", test_sbix);
    robo_test_register("font.color.cblc_cbdt", test_cblc_cbdt);
    robo_test_register("font.color.svg", test_svg);
    robo_test_register("font.color.multi", test_multi);
    robo_test_register("font.color.emoji", test_emoji);
    robo_test_register("font.color.variation", test_variation);
    robo_test_register("font.color.malformed", test_malformed);
    robo_test_register("font.color.fuzz", test_fuzz);
    robo_test_register("font.color.bench", test_bench);
}
