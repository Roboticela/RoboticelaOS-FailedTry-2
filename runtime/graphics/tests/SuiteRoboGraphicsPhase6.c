/*
 * RoboGraphics Phase 6 final validation / API freeze (Goal 6.15).
 *
 * Integration, visual fixtures, determinism, safety, and smoke performance.
 * Does not add new graphics features. Synthetic RoboFont fixtures only.
 */

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

/* ---- Synthetic square-glyph font (same pattern as graphics.text) ---- */

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
    put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE |
                          ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
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
                                  robo_gfx_pixel_format_t fmt,
                                  robo_gfx_surface_t **out)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_desc_init(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = fmt;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    return robo_gfx_surface_create(&desc, out);
}

static void expect_rgba(robo_gfx_color_rgba8_t c,
                        uint8_t r,
                        uint8_t g,
                        uint8_t b,
                        uint8_t a)
{
    ROBO_CHECK_EQ_INT(c.r, r);
    ROBO_CHECK_EQ_INT(c.g, g);
    ROBO_CHECK_EQ_INT(c.b, b);
    ROBO_CHECK_EQ_INT(c.a, a);
}

static void paint_fixture_scene(robo_gfx_surface_t *s)
{
    robo_gfx_raster_t *r = NULL;
    ROBO_CHECK(robo_gfx_surface_clear(s, robo_gfx_rgba8(0, 0, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_raster_create(s, &r) == ROBO_OK);
    ROBO_CHECK(robo_gfx_raster_fill_rect(r, robo_gfx_rect(1, 1, 2, 2),
                                         robo_gfx_rgba8(255, 0, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_raster_draw_line(r, robo_gfx_point(0, 0),
                                         robo_gfx_point(3, 0),
                                         robo_gfx_rgba8(0, 255, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_raster_draw_pixel(r, robo_gfx_point(3, 3),
                                          robo_gfx_rgba8(0, 0, 255, 255)) ==
               ROBO_OK);
    robo_gfx_raster_destroy(r);
}

static void test_p6_freeze(void)
{
    robo_gfx_capabilities_t caps;

    ROBO_CHECK_EQ_INT(ROBO_GRAPHICS_API_FROZEN, 1);
    /* Additive decode module only; the frozen API surface is unchanged. */
    ROBO_CHECK_STR_EQ(robo_graphics_version_string(), "0.16.0");
    ROBO_CHECK_EQ_INT(robo_graphics_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_query_capabilities(&caps), ROBO_OK);
    ROBO_CHECK(caps.cpu_raster_available);
    ROBO_CHECK(!caps.gpu_backend_available);
    ROBO_CHECK(!caps.native_window_available);
    ROBO_CHECK(!caps.framebuffer_present_available);
    robo_graphics_shutdown();
}

static void test_p6_visual_fixture(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_color_rgba8_t c;
    static const uint8_t expect[4][4][4] = {
        {{0, 255, 0, 255},
         {0, 255, 0, 255},
         {0, 255, 0, 255},
         {0, 255, 0, 255}},
        {{0, 0, 0, 255},
         {255, 0, 0, 255},
         {255, 0, 0, 255},
         {0, 0, 0, 255}},
        {{0, 0, 0, 255},
         {255, 0, 0, 255},
         {255, 0, 0, 255},
         {0, 0, 0, 255}},
        {{0, 0, 0, 255},
         {0, 0, 0, 255},
         {0, 0, 0, 255},
         {0, 0, 255, 255}},
    };
    uint32_t x;
    uint32_t y;

    ROBO_CHECK(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    paint_fixture_scene(s);
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            ROBO_CHECK(robo_gfx_surface_get_pixel(s, x, y, &c) == ROBO_OK);
            expect_rgba(c, expect[y][x][0], expect[y][x][1], expect[y][x][2],
                        expect[y][x][3]);
        }
    }
    robo_gfx_surface_release(s);
}

static void test_p6_raster_shot_encode(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_capture_options_t caps;
    robo_gfx_encode_options_t enc;
    robo_gfx_encoded_buffer_t a;
    robo_gfx_encoded_buffer_t b;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    paint_fixture_scene(s);

    robo_gfx_capture_options_init(&caps);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &caps, &shot) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_get_pixel(robo_gfx_screenshot_surface(shot), 1,
                                          1, &c) == ROBO_OK);
    expect_rgba(c, 255, 0, 0, 255);

    ROBO_CHECK(robo_gfx_surface_clear(s, robo_gfx_rgba8(1, 1, 1, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_get_pixel(robo_gfx_screenshot_surface(shot), 1,
                                          1, &c) == ROBO_OK);
    expect_rgba(c, 255, 0, 0, 255);

    robo_gfx_encode_options_init(&enc);
    ROBO_CHECK(robo_gfx_screenshot_export(shot, &enc, &a) == ROBO_OK);
    ROBO_CHECK(robo_gfx_screenshot_export(shot, &enc, &b) == ROBO_OK);
    ROBO_CHECK(a.size == b.size);
    ROBO_CHECK(memcmp(a.data, b.data, a.size) == 0);
    ROBO_CHECK(a.size >= 8u);
    ROBO_CHECK(a.data[0] == 0x89u && a.data[1] == 'P');

    robo_gfx_encoded_buffer_release(&a);
    robo_gfx_encoded_buffer_release(&b);
    robo_gfx_screenshot_release(shot);
    robo_gfx_surface_release(s);
}

static void test_p6_path_transform(void)
{
    robo_gfx_path_t *path = NULL;
    robo_gfx_affine_t xf;
    robo_gfx_rect_t bounds;
    robo_gfx_path_iter_t it;
    robo_gfx_path_command_t cmd;
    int saw_line = 0;

    ROBO_CHECK(robo_gfx_path_create(&path) == ROBO_OK);
    ROBO_CHECK(robo_gfx_path_move_to(path, robo_gfx_point(0, 0)) == ROBO_OK);
    ROBO_CHECK(robo_gfx_path_line_to(path, robo_gfx_point(10, 0)) == ROBO_OK);
    ROBO_CHECK(robo_gfx_path_line_to(path, robo_gfx_point(10, 10)) == ROBO_OK);
    ROBO_CHECK(robo_gfx_path_close(path) == ROBO_OK);

    robo_gfx_affine_translate(&xf, 5.0f, 7.0f);
    ROBO_CHECK(robo_gfx_path_transform(path, &xf) == ROBO_OK);
    ROBO_CHECK(robo_gfx_path_bounds(path, &bounds) == ROBO_OK);
    ROBO_CHECK_EQ_INT(bounds.x, 5);
    ROBO_CHECK_EQ_INT(bounds.y, 7);
    ROBO_CHECK_EQ_INT(bounds.width, 11);
    ROBO_CHECK_EQ_INT(bounds.height, 11);

    robo_gfx_path_iter_begin(path, &it);
    while (robo_gfx_path_iter_next(&it, &cmd)) {
        if (cmd.verb == ROBO_GFX_PATH_LINE_TO) {
            saw_line = 1;
            ROBO_CHECK(cmd.point.x >= 5);
        }
    }
    ROBO_CHECK(saw_line);

    robo_gfx_path_release(path);
}

static void test_p6_transform_raster(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_affine_t xf;
    robo_gfx_point_f_t pf;
    robo_gfx_point_t pi;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK(make_surface(16, 16, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_clear(s, robo_gfx_rgba8(0, 0, 0, 255)) ==
               ROBO_OK);

    robo_gfx_affine_scale(&xf, 2.0f, 2.0f);
    pf = robo_gfx_affine_map_point_f(&xf, robo_gfx_point_f(3.0f, 4.0f));
    pi = robo_gfx_point_f_floor(pf);
    ROBO_CHECK_EQ_INT(pi.x, 6);
    ROBO_CHECK_EQ_INT(pi.y, 8);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, (uint32_t)pi.x, (uint32_t)pi.y,
                                          robo_gfx_rgba8(200, 100, 50, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_get_pixel(s, 6, 8, &c) == ROBO_OK);
    expect_rgba(c, 200, 100, 50, 255);

    robo_gfx_surface_release(s);
}

static void test_p6_font_text_bridge(void)
{
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_gfx_path_t *path = NULL;
    robo_gfx_point_f_t d;
    robo_gfx_glyph_metrics_device_t m;
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_glyph_draw_t draw;

    ROBO_CHECK(robo_gfx_font_point_to_device(
                   0, 100, 1000u, 10.0f, robo_gfx_point_f(50.0f, 80.0f),
                   &d) == ROBO_OK);
    ROBO_CHECK(d.x > 49.9f && d.x < 50.1f);
    ROBO_CHECK(d.y > 78.9f && d.y < 79.1f);

    ROBO_CHECK(make_square_face(&face, &src) == ROBO_OK);
    ROBO_CHECK(robo_gfx_glyph_metrics_device(face, 0, 10.0f, &m) == ROBO_OK);
    ROBO_CHECK(m.advance_width > 1.9f && m.advance_width < 2.1f);

    ROBO_CHECK(robo_gfx_path_create(&path) == ROBO_OK);
    ROBO_CHECK(robo_gfx_glyph_outline_to_path(face, 0u, 10.0f,
                                              robo_gfx_point_f(0.0f, 20.0f),
                                              path) == ROBO_OK);
    ROBO_CHECK(!robo_gfx_path_is_empty(path));

    ROBO_CHECK(make_surface(32, 32, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_raster_create(s, &r) == ROBO_OK);
    robo_gfx_glyph_draw_init(&draw);
    draw.face = face;
    draw.glyph_id = 0u;
    draw.size_px = 10.0f;
    draw.origin = robo_gfx_point_f(4.0f, 20.0f);
    draw.color = robo_gfx_rgba8(255, 255, 255, 255);
    draw.mode = ROBO_GFX_GLYPH_MODE_OUTLINE_STROKE;
    ROBO_CHECK(robo_gfx_raster_draw_glyph(r, &draw) == ROBO_OK);

    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);
    robo_gfx_path_release(path);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_p6_alpha_overlap(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_color_rgba8_t c;
    robo_gfx_color_rgba8_t expected;

    ROBO_CHECK(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_clear(s, robo_gfx_rgba8(0, 255, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_composite_pixel(
                   s, 0, 0, robo_gfx_rgba8(255, 0, 0, 128), 255) == ROBO_OK);
    expected = robo_gfx_composite_src_over(robo_gfx_rgba8(255, 0, 0, 128),
                                           robo_gfx_rgba8(0, 255, 0, 255));
    ROBO_CHECK(robo_gfx_surface_get_pixel(s, 0, 0, &c) == ROBO_OK);
    expect_rgba(c, expected.r, expected.g, expected.b, expected.a);
    robo_gfx_surface_release(s);
}

static void test_p6_determinism(void)
{
    robo_gfx_surface_t *a = NULL;
    robo_gfx_surface_t *b = NULL;
    robo_gfx_encode_options_t enc;
    robo_gfx_encoded_buffer_t ea;
    robo_gfx_encoded_buffer_t eb;
    size_t bytes;
    const uint8_t *pa;
    const uint8_t *pb;

    ROBO_CHECK(make_surface(8, 8, ROBO_GFX_FORMAT_RGBA8, &a) == ROBO_OK);
    ROBO_CHECK(make_surface(8, 8, ROBO_GFX_FORMAT_RGBA8, &b) == ROBO_OK);
    paint_fixture_scene(a);
    paint_fixture_scene(b);

    pa = robo_gfx_surface_pixels_const(a);
    pb = robo_gfx_surface_pixels_const(b);
    bytes = robo_gfx_surface_byte_size(a);
    ROBO_CHECK(pa && pb);
    ROBO_CHECK(memcmp(pa, pb, bytes) == 0);

    robo_gfx_encode_options_init(&enc);
    ROBO_CHECK(robo_gfx_surface_encode(a, &enc, &ea) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_encode(b, &enc, &eb) == ROBO_OK);
    ROBO_CHECK(ea.size == eb.size);
    ROBO_CHECK(memcmp(ea.data, eb.data, ea.size) == 0);

    robo_gfx_encoded_buffer_release(&ea);
    robo_gfx_encoded_buffer_release(&eb);
    robo_gfx_surface_release(a);
    robo_gfx_surface_release(b);
}

static void test_p6_safety(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_capture_options_t caps;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_encode_options_t enc;
    robo_gfx_encoded_buffer_t buf;

    ROBO_CHECK(robo_gfx_raster_create(NULL, &r) == ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_raster_create(s, &r) == ROBO_OK);
    ROBO_CHECK(robo_gfx_raster_fill_rect(r, robo_gfx_rect(0, 0, -1, 1),
                                         robo_gfx_rgb8(1, 2, 3)) ==
               ROBO_INVALID_ARGUMENT);
    robo_gfx_raster_destroy(r);

    robo_gfx_capture_options_init(&caps);
    caps.use_full_extent = 0;
    caps.region_policy = ROBO_GFX_CAPTURE_REGION_STRICT;
    caps.region = robo_gfx_rect(0, 0, 0, 0);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &caps, &shot) ==
               ROBO_INVALID_ARGUMENT);

    robo_gfx_encode_options_init(&enc);
    ROBO_CHECK(robo_gfx_surface_encode(NULL, &enc, &buf) ==
               ROBO_INVALID_ARGUMENT);

    robo_gfx_surface_release(s);
}

static void test_p6_perf_smoke(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_encode_options_t enc;
    robo_gfx_encoded_buffer_t buf;
    clock_t t0;
    clock_t t1;
    int i;

    ROBO_CHECK(make_surface(64, 64, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_raster_create(s, &r) == ROBO_OK);

    t0 = clock();
    for (i = 0; i < 200; i++) {
        ROBO_CHECK(robo_gfx_raster_fill_rect(
                       r, robo_gfx_rect(0, 0, 64, 64),
                       robo_gfx_rgba8((uint8_t)i, 40, 80, 255)) == ROBO_OK);
        ROBO_CHECK(robo_gfx_raster_draw_line(
                       r, robo_gfx_point(0, 0), robo_gfx_point(63, 63),
                       robo_gfx_rgba8(255, 255, 255, 255)) == ROBO_OK);
        ROBO_CHECK(robo_gfx_surface_composite_pixel(
                       s, 32, 32, robo_gfx_rgba8(0, 0, 255, 128), 255) ==
                   ROBO_OK);
    }
    robo_gfx_encode_options_init(&enc);
    ROBO_CHECK(robo_gfx_surface_encode(s, &enc, &buf) == ROBO_OK);
    t1 = clock();
    ROBO_CHECK(t1 >= t0);
    ROBO_CHECK((t1 - t0) < 30 * CLOCKS_PER_SEC);

    robo_gfx_encoded_buffer_release(&buf);
    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);
}

static void test_p6_chromium_independence(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_capabilities_t caps;

    ROBO_CHECK(robo_gfx_query_capabilities(&caps) == ROBO_OK);
    ROBO_CHECK(!caps.gpu_backend_available);
    ROBO_CHECK(!caps.native_window_available);
    ROBO_CHECK(make_surface(1, 1, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_present_null(s) == ROBO_OK);
    robo_gfx_surface_release(s);
}

void robo_test_register_graphics_phase6(void)
{
    robo_test_register("graphics.p6.freeze", test_p6_freeze);
    robo_test_register("graphics.p6.visual_fixture", test_p6_visual_fixture);
    robo_test_register("graphics.p6.raster_shot_encode",
                       test_p6_raster_shot_encode);
    robo_test_register("graphics.p6.path_transform", test_p6_path_transform);
    robo_test_register("graphics.p6.transform_raster",
                       test_p6_transform_raster);
    robo_test_register("graphics.p6.font_text_bridge",
                       test_p6_font_text_bridge);
    robo_test_register("graphics.p6.alpha_overlap", test_p6_alpha_overlap);
    robo_test_register("graphics.p6.determinism", test_p6_determinism);
    robo_test_register("graphics.p6.safety", test_p6_safety);
    robo_test_register("graphics.p6.perf_smoke", test_p6_perf_smoke);
    robo_test_register("graphics.p6.chromium", test_p6_chromium_independence);
}
