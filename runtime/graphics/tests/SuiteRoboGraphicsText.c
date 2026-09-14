/*
 * RoboGraphics Goal 6.12 — text / glyph bridge.
 * Synthetic RoboFont fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

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
    return need;
}

static size_t build_head(uint8_t *b, uint16_t upem)
{
    uint8_t *p = b;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_u32(&p, 0x00010000u);
    put_u32(&p, 0);
    put_u32(&p, 0x5F0F3CF5u);
    put_u16(&p, 0);
    put_u16(&p, upem);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_u32(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 100);
    put_i16(&p, 100);
    put_u16(&p, 0u);
    put_u16(&p, 8u);
    put_i16(&p, 2);
    put_i16(&p, 1);
    put_i16(&p, 0);
    return (size_t)(p - b);
}

static size_t build_hhea(uint8_t *b, uint16_t n_h)
{
    uint8_t *p = b;
    int i;
    put_u16(&p, 1);
    put_u16(&p, 0);
    put_i16(&p, 800);
    put_i16(&p, -200);
    put_i16(&p, 0);
    put_u16(&p, 1000);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 100);
    put_i16(&p, 1);
    put_i16(&p, 0);
    put_i16(&p, 0);
    for (i = 0; i < 4; i++) {
        put_i16(&p, 0);
    }
    put_i16(&p, 0);
    put_u16(&p, n_h);
    return (size_t)(p - b);
}

static size_t build_maxp(uint8_t *b, uint16_t ng)
{
    uint8_t *p = b;
    put_u32(&p, 0x00010000u);
    put_u16(&p, ng);
    put_u16(&p, 8);
    put_u16(&p, 2);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 2);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 0);
    put_u16(&p, 4);
    put_u16(&p, 4);
    return (size_t)(p - b);
}

static size_t build_hmtx(uint8_t *b, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i < n; i++) {
        put_u16(&p, 200);
        put_i16(&p, 10);
    }
    return (size_t)(p - b);
}

static size_t build_square_glyf(uint8_t *b)
{
    uint8_t *p = b;
    put_i16(&p, 1);
    put_i16(&p, 0);
    put_i16(&p, 0);
    put_i16(&p, 100);
    put_i16(&p, 100);
    put_u16(&p, 3);
    put_u16(&p, 0);
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                          ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_Y_SHORT |
                          ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                          ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    put_u8(&p, 100);
    put_u8(&p, 100);
    put_u8(&p, 100);
    return (size_t)(p - b);
}

static robo_result_t make_square_face(robo_font_face_t **out_face,
                                      robo_font_source_t **out_src)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], loca[16], glyf[64];
    uint8_t font[384];
    synth_table_t tabs[6];
    size_t gn = build_square_glyf(glyf);
    uint8_t *lp = loca;
    size_t n;

    put_u32(&lp, 0);
    put_u32(&lp, (uint32_t)gn);
    put_u32(&lp, (uint32_t)gn);

    tabs[0].tag = ROBO_FONT_TAG_HEAD;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)build_head(head, 1000);
    tabs[1].tag = ROBO_FONT_TAG_HHEA;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)build_hhea(hhea, 2);
    tabs[2].tag = ROBO_FONT_TAG_MAXP;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)build_maxp(maxp, 2);
    tabs[3].tag = ROBO_FONT_TAG_HMTX;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)build_hmtx(hmtx, 2);
    tabs[4].tag = ROBO_FONT_TAG_LOCA;
    tabs[4].data = loca;
    tabs[4].length = 12u;
    tabs[5].tag = ROBO_FONT_TAG_GLYF;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)gn;

    n = build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tabs, 6);
    if (n == 0u) {
        return ROBO_OUT_OF_MEMORY;
    }
    if (robo_font_source_create_memory_copy(font, n, out_src) != ROBO_OK) {
        return ROBO_OUT_OF_MEMORY;
    }
    return robo_font_face_create(*out_src, 0, out_face);
}

static robo_result_t make_surface(uint32_t w,
                                  uint32_t h,
                                  robo_gfx_surface_t **out)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_desc_init(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    return robo_gfx_surface_create(&desc, out);
}

static void test_text_coords(void)
{
    robo_gfx_point_f_t d;
    float scale = 0.0f;
    robo_gfx_point_t snap;

    ROBO_CHECK_EQ_INT(robo_gfx_font_scale(0u, 10.0f, &scale),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_font_scale(1000u, 0.0f, &scale),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_font_scale(1000u, 10.0f, &scale), ROBO_OK);
    ROBO_CHECK(scale > 0.009f && scale < 0.011f);

    /* font (0,100) at origin (50,80), size 10, upem 1000 → (50, 79) */
    ROBO_CHECK_EQ_INT(
        robo_gfx_font_point_to_device(0, 100, 1000u, 10.0f,
                                     robo_gfx_point_f(50.0f, 80.0f), &d),
        ROBO_OK);
    ROBO_CHECK(d.x > 49.9f && d.x < 50.1f);
    ROBO_CHECK(d.y > 78.9f && d.y < 79.1f);

    /* below baseline */
    ROBO_CHECK_EQ_INT(
        robo_gfx_font_point_to_device(0, -50, 1000u, 10.0f,
                                     robo_gfx_point_f(0.0f, 0.0f), &d),
        ROBO_OK);
    ROBO_CHECK(d.y > 0.49f && d.y < 0.51f);

    snap = robo_gfx_glyph_snap_point(robo_gfx_point_f(1.5f, -1.5f));
    ROBO_CHECK_EQ_INT(snap.x, 2);
    ROBO_CHECK_EQ_INT(snap.y, -2);
}

static void test_text_metrics(void)
{
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_gfx_glyph_metrics_device_t m;

    ROBO_CHECK_EQ_INT(make_square_face(&face, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_glyph_metrics_device(NULL, 0, 10.0f, &m),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_glyph_metrics_device(face, 0, 0.0f, &m),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_glyph_metrics_device(face, 0, 10.0f, &m), ROBO_OK);
    /* advance 200 FUnits @ 10px / 1000 = 2px; lsb 10 → 0.1 */
    ROBO_CHECK(m.advance_width > 1.9f && m.advance_width < 2.1f);
    ROBO_CHECK(m.left_side_bearing > 0.09f && m.left_side_bearing < 0.11f);
    ROBO_CHECK(m.has_bounds);
    ROBO_CHECK(m.bounds.width > 0.9f && m.bounds.width < 1.1f);

    ROBO_CHECK_EQ_INT(robo_gfx_glyph_metrics_device(face, 99u, 10.0f, &m),
                      ROBO_INVALID_ARGUMENT);

    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_text_outline_path(void)
{
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_gfx_path_t *path = NULL;
    robo_gfx_path_command_t cmd;
    size_t n;
    int saw_move = 0;
    int saw_line = 0;
    int saw_close = 0;
    size_t i;

    ROBO_CHECK_EQ_INT(make_square_face(&face, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_glyph_outline_to_path(face, 0, 10.0f,
                                       robo_gfx_point_f(20.0f, 40.0f), path),
        ROBO_OK);
    n = robo_gfx_path_command_count(path);
    ROBO_CHECK(n >= 4u);
    for (i = 0u; i < n; i++) {
        ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(path, i, &cmd), ROBO_OK);
        if (cmd.verb == ROBO_GFX_PATH_MOVE_TO) {
            saw_move = 1;
        }
        if (cmd.verb == ROBO_GFX_PATH_LINE_TO) {
            saw_line = 1;
        }
        if (cmd.verb == ROBO_GFX_PATH_CLOSE) {
            saw_close = 1;
        }
    }
    ROBO_CHECK(saw_move);
    ROBO_CHECK(saw_line);
    ROBO_CHECK(saw_close);

    /* empty glyph */
    ROBO_CHECK_EQ_INT(
        robo_gfx_glyph_outline_to_path(face, 1, 10.0f,
                                       robo_gfx_point_f(0.0f, 0.0f), path),
        ROBO_OK);
    ROBO_CHECK(robo_gfx_path_is_empty(path));

    ROBO_CHECK_EQ_INT(
        robo_gfx_glyph_outline_to_path(NULL, 0, 10.0f,
                                       robo_gfx_point_f(0.0f, 0.0f), path),
        ROBO_INVALID_ARGUMENT);

    robo_gfx_path_release(path);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_text_draw_glyph(void)
{
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_glyph_draw_t draw;
    robo_gfx_color_rgba8_t c;
    uint32_t marked = 0u;
    uint32_t y;
    uint32_t x;

    ROBO_CHECK_EQ_INT(make_square_face(&face, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(64, 64, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 0)), ROBO_OK);

    robo_gfx_glyph_draw_init(&draw);
    draw.face = face;
    draw.glyph_id = 0;
    draw.size_px = 100.0f; /* 100 FUnit square → 10 device units */
    draw.origin = robo_gfx_point_f(10.0f, 50.0f);
    draw.color = robo_gfx_rgb8(255, 0, 0);

    ROBO_CHECK_EQ_INT(robo_gfx_raster_draw_glyph(r, &draw), ROBO_OK);
    for (y = 0u; y < 64u; y++) {
        for (x = 0u; x < 64u; x++) {
            ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, x, y, &c), ROBO_OK);
            if (c.r == 255u && c.g == 0u && c.b == 0u) {
                marked++;
            }
        }
    }
    ROBO_CHECK(marked > 0u);

    /* off-screen → no-op */
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 0)), ROBO_OK);
    draw.origin = robo_gfx_point_f(5000.0f, 5000.0f);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_draw_glyph(r, &draw), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 0);

    /* clipped */
    ROBO_CHECK_EQ_INT(robo_gfx_raster_set_clip(r, robo_gfx_rect(0, 0, 5, 5)),
                      ROBO_OK);
    draw.origin = robo_gfx_point_f(10.0f, 50.0f);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_draw_glyph(r, &draw), ROBO_OK);

    /* empty glyph */
    ROBO_CHECK_EQ_INT(robo_gfx_raster_reset_clip(r), ROBO_OK);
    draw.glyph_id = 1;
    ROBO_CHECK_EQ_INT(robo_gfx_raster_draw_glyph(r, &draw), ROBO_OK);

    /* unsupported mode */
    draw.glyph_id = 0;
    draw.mode = ROBO_GFX_GLYPH_MODE_ALPHA;
    ROBO_CHECK_EQ_INT(robo_gfx_raster_draw_glyph(r, &draw), ROBO_NOT_SUPPORTED);

    ROBO_CHECK_EQ_INT(robo_gfx_raster_draw_glyph(NULL, &draw),
                      ROBO_INVALID_ARGUMENT);
    draw.face = NULL;
    draw.mode = ROBO_GFX_GLYPH_MODE_OUTLINE_STROKE;
    ROBO_CHECK_EQ_INT(robo_gfx_raster_draw_glyph(r, &draw),
                      ROBO_INVALID_ARGUMENT);

    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_text_layout_metrics(void)
{
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_metrics_t layout;

    ROBO_CHECK_EQ_INT(make_square_face(&face, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_layout_metrics(face, &layout), ROBO_OK);
    ROBO_CHECK(layout.valid);
    ROBO_CHECK_EQ_INT((int)layout.units_per_em, 1000);
    ROBO_CHECK(layout.ascent > 0);
    ROBO_CHECK(layout.descent >= 0);

    robo_font_face_release(face);
    robo_font_source_release(src);
}

void robo_test_register_graphics_text(void)
{
    robo_test_register("graphics.text.coords", test_text_coords);
    robo_test_register("graphics.text.metrics", test_text_metrics);
    robo_test_register("graphics.text.outline", test_text_outline_path);
    robo_test_register("graphics.text.draw", test_text_draw_glyph);
    robo_test_register("graphics.text.layout", test_text_layout_metrics);
}
