/*
 * RoboGraphics Goal 6.7 — one-pixel line rasterization.
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

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

static int pixel_is(robo_gfx_surface_t *s,
                    uint32_t x,
                    uint32_t y,
                    uint8_t r,
                    uint8_t g,
                    uint8_t b,
                    uint8_t a)
{
    robo_gfx_color_rgba8_t c;
    if (robo_gfx_surface_get_pixel(s, x, y, &c) != ROBO_OK) {
        return 0;
    }
    return c.r == r && c.g == g && c.b == b && c.a == a;
}

static int pixel_marked(robo_gfx_surface_t *s, uint32_t x, uint32_t y)
{
    return pixel_is(s, x, y, 255, 0, 0, 255);
}

static int pixel_clear(robo_gfx_surface_t *s, uint32_t x, uint32_t y)
{
    return pixel_is(s, x, y, 0, 0, 0, 0);
}

static void clear_black(robo_gfx_surface_t *s)
{
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(s, robo_gfx_rgba8(0, 0, 0, 0)),
                      ROBO_OK);
}

static const robo_gfx_color_rgba8_t k_red = {255, 0, 0, 255};

static void expect_only(robo_gfx_surface_t *s,
                        const int32_t *pts,
                        unsigned n)
{
    uint32_t w = robo_gfx_surface_width(s);
    uint32_t h = robo_gfx_surface_height(s);
    uint32_t x;
    uint32_t y;
    unsigned i;

    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x++) {
            int want = 0;
            for (i = 0u; i < n; i++) {
                if (pts[i * 2] == (int32_t)x && pts[i * 2 + 1] == (int32_t)y) {
                    want = 1;
                    break;
                }
            }
            if (want) {
                ROBO_CHECK(pixel_marked(s, x, y));
            } else {
                ROBO_CHECK(pixel_clear(s, x, y));
            }
        }
    }
}

static void test_line_single(void)
{
    robo_gfx_surface_t *s = NULL;
    static const int32_t pts[] = {2, 2};

    ROBO_CHECK_EQ_INT(make_surface(5, 5, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(2, 2), robo_gfx_point(2, 2),
                                   k_red),
        ROBO_OK);
    expect_only(s, pts, 1u);
    robo_gfx_surface_release(s);
}

static void test_line_horizontal(void)
{
    robo_gfx_surface_t *s = NULL;
    static const int32_t ltr[] = {2, 5, 3, 5, 4, 5, 5, 5, 6, 5, 7, 5, 8, 5};
    static const int32_t rtl[] = {2, 5, 3, 5, 4, 5, 5, 5, 6, 5, 7, 5, 8, 5};

    ROBO_CHECK_EQ_INT(make_surface(12, 10, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(2, 5), robo_gfx_point(8, 5),
                                   k_red),
        ROBO_OK);
    expect_only(s, ltr, 7u);

    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(8, 5), robo_gfx_point(2, 5),
                                   k_red),
        ROBO_OK);
    expect_only(s, rtl, 7u);
    robo_gfx_surface_release(s);
}

static void test_line_vertical(void)
{
    robo_gfx_surface_t *s = NULL;
    static const int32_t pts[] = {5, 2, 5, 3, 5, 4, 5, 5, 5, 6, 5, 7, 5, 8};

    ROBO_CHECK_EQ_INT(make_surface(10, 12, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(5, 2), robo_gfx_point(5, 8),
                                   k_red),
        ROBO_OK);
    expect_only(s, pts, 7u);

    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(5, 8), robo_gfx_point(5, 2),
                                   k_red),
        ROBO_OK);
    expect_only(s, pts, 7u);
    robo_gfx_surface_release(s);
}

static void test_line_diagonal(void)
{
    robo_gfx_surface_t *s = NULL;
    static const int32_t d1[] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5};
    static const int32_t d2[] = {0, 5, 1, 4, 2, 3, 3, 2, 4, 1, 5, 0};

    ROBO_CHECK_EQ_INT(make_surface(6, 6, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);

    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(0, 0), robo_gfx_point(5, 5),
                                   k_red),
        ROBO_OK);
    expect_only(s, d1, 6u);

    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(5, 5), robo_gfx_point(0, 0),
                                   k_red),
        ROBO_OK);
    expect_only(s, d1, 6u);

    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(0, 5), robo_gfx_point(5, 0),
                                   k_red),
        ROBO_OK);
    expect_only(s, d2, 6u);

    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(5, 0), robo_gfx_point(0, 5),
                                   k_red),
        ROBO_OK);
    expect_only(s, d2, 6u);

    robo_gfx_surface_release(s);
}

static void test_line_shallow_steep(void)
{
    robo_gfx_surface_t *s = NULL;
    /* (0,0)->(4,2): (0,0)(1,0)(2,1)(3,1)(4,2) */
    static const int32_t shallow[] = {0, 0, 1, 0, 2, 1, 3, 1, 4, 2};
    /* (0,0)->(2,4): (0,0)(0,1)(1,2)(1,3)(2,4) */
    static const int32_t steep[] = {0, 0, 0, 1, 1, 2, 1, 3, 2, 4};

    ROBO_CHECK_EQ_INT(make_surface(6, 6, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);

    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(0, 0), robo_gfx_point(4, 2),
                                   k_red),
        ROBO_OK);
    expect_only(s, shallow, 5u);

    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(4, 2), robo_gfx_point(0, 0),
                                   k_red),
        ROBO_OK);
    expect_only(s, shallow, 5u);

    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(0, 0), robo_gfx_point(2, 4),
                                   k_red),
        ROBO_OK);
    expect_only(s, steep, 5u);

    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(2, 4), robo_gfx_point(0, 0),
                                   k_red),
        ROBO_OK);
    expect_only(s, steep, 5u);

    robo_gfx_surface_release(s);
}

static void test_line_clip(void)
{
    robo_gfx_surface_t *s = NULL;
    uint32_t x;
    uint32_t y;
    unsigned marked;

    ROBO_CHECK_EQ_INT(make_surface(8, 8, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);

    /* Completely outside */
    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(-20, -20),
                                   robo_gfx_point(-5, -5), k_red),
        ROBO_OK);
    for (y = 0u; y < 8u; y++) {
        for (x = 0u; x < 8u; x++) {
            ROBO_CHECK(pixel_clear(s, x, y));
        }
    }

    /* Horizontal crossing: (-10,3) → (20,3) → pixels (0..7, 3) */
    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(-10, 3),
                                   robo_gfx_point(20, 3), k_red),
        ROBO_OK);
    marked = 0u;
    for (x = 0u; x < 8u; x++) {
        ROBO_CHECK(pixel_marked(s, x, 3u));
        marked++;
        for (y = 0u; y < 8u; y++) {
            if (y != 3u) {
                ROBO_CHECK(pixel_clear(s, x, y));
            }
        }
    }
    ROBO_CHECK_EQ_INT((int)marked, 8);

    /* Partial diagonal from outside */
    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(-2, -2),
                                   robo_gfx_point(3, 3), k_red),
        ROBO_OK);
    ROBO_CHECK(pixel_marked(s, 0u, 0u));
    ROBO_CHECK(pixel_marked(s, 3u, 3u));

    /* Edges / corners */
    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(0, 0), robo_gfx_point(7, 0),
                                   k_red),
        ROBO_OK);
    ROBO_CHECK(pixel_marked(s, 0u, 0u));
    ROBO_CHECK(pixel_marked(s, 7u, 0u));

    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(0, 0), robo_gfx_point(0, 7),
                                   k_red),
        ROBO_OK);
    ROBO_CHECK(pixel_marked(s, 0u, 7u));

    clear_black(s);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(s, robo_gfx_point(7, 7), robo_gfx_point(0, 7),
                                   k_red),
        ROBO_OK);
    ROBO_CHECK(pixel_marked(s, 7u, 7u));

    robo_gfx_surface_release(s);
}

static void test_line_formats(void)
{
    static const robo_gfx_pixel_format_t formats[] = {
        ROBO_GFX_FORMAT_RGBA8, ROBO_GFX_FORMAT_BGRA8, ROBO_GFX_FORMAT_RGB8,
        ROBO_GFX_FORMAT_GRAY8, ROBO_GFX_FORMAT_ALPHA8};
    size_t i;
    robo_gfx_color_rgba8_t ink = robo_gfx_rgba8(10, 20, 30, 200);
    robo_gfx_color_rgba8_t out;

    for (i = 0u; i < sizeof(formats) / sizeof(formats[0]); i++) {
        robo_gfx_surface_t *s = NULL;
        uint8_t packed[8];
        robo_gfx_color_rgba8_t expect;

        ROBO_CHECK_EQ_INT(make_surface(4, 4, formats[i], &s), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(s, robo_gfx_rgba8(0, 0, 0, 0)),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_draw_line(s, robo_gfx_point(0, 1),
                                       robo_gfx_point(3, 1), ink),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_pixel_pack(formats[i], ink, packed, 8),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_gfx_pixel_unpack(formats[i], packed,
                                  robo_gfx_surface_bytes_per_pixel(s), &expect),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 1u, &out), ROBO_OK);
        ROBO_CHECK_EQ_INT(out.r, expect.r);
        ROBO_CHECK_EQ_INT(out.g, expect.g);
        ROBO_CHECK_EQ_INT(out.b, expect.b);
        ROBO_CHECK_EQ_INT(out.a, expect.a);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 3u, 1u, &out), ROBO_OK);
        ROBO_CHECK_EQ_INT(out.r, expect.r);
        robo_gfx_surface_release(s);
    }
}

static void test_line_seg_and_errors(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_surface_desc_t desc;
    robo_gfx_line_t line =
        robo_gfx_line(robo_gfx_point(0, 0), robo_gfx_point(2, 0));

    ROBO_CHECK_EQ_INT(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    clear_black(s);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_draw_line_seg(s, line, k_red), ROBO_OK);
    ROBO_CHECK(pixel_marked(s, 0u, 0u));
    ROBO_CHECK(pixel_marked(s, 2u, 0u));

    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_draw_line(NULL, robo_gfx_point(0, 0),
                                   robo_gfx_point(1, 1), k_red),
        ROBO_INVALID_ARGUMENT);

    robo_gfx_surface_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ;
    {
        robo_gfx_surface_t *ro = NULL;
        ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &ro), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_draw_line(ro, robo_gfx_point(0, 0),
                                       robo_gfx_point(1, 0), k_red),
            ROBO_INVALID_ARGUMENT);
        robo_gfx_surface_release(ro);
    }

    robo_gfx_surface_release(s);
}

static void test_line_smoke_many(void)
{
    robo_gfx_surface_t *s = NULL;
    int i;

    ROBO_CHECK_EQ_INT(make_surface(64, 64, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    clear_black(s);
    for (i = 0; i < 2000; i++) {
        int32_t x0 = (int32_t)(i % 64);
        int32_t y0 = (int32_t)((i / 3) % 64);
        int32_t x1 = (int32_t)((i * 7) % 64);
        int32_t y1 = (int32_t)((i * 11) % 64);
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_draw_line(s, robo_gfx_point(x0, y0),
                                       robo_gfx_point(x1, y1), k_red),
            ROBO_OK);
    }
    /* Spot-check a few pixels remain valid / addressable. */
    {
        robo_gfx_color_rgba8_t c;
        ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 63u, 63u, &c), ROBO_OK);
    }
    robo_gfx_surface_release(s);
}

void robo_test_register_graphics_line(void)
{
    robo_test_register("graphics.line.single", test_line_single);
    robo_test_register("graphics.line.horizontal", test_line_horizontal);
    robo_test_register("graphics.line.vertical", test_line_vertical);
    robo_test_register("graphics.line.diagonal", test_line_diagonal);
    robo_test_register("graphics.line.slope", test_line_shallow_steep);
    robo_test_register("graphics.line.clip", test_line_clip);
    robo_test_register("graphics.line.formats", test_line_formats);
    robo_test_register("graphics.line.errors", test_line_seg_and_errors);
    robo_test_register("graphics.line.smoke", test_line_smoke_many);
}
