/*
 * RoboGraphics basic bitmap operations suite (Goal 6.5).
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

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
    return robo_gfx_surface_create(&desc, out);
}

static void test_bmp_get_set_formats(void)
{
    static const robo_gfx_pixel_format_t k_formats[] = {
        ROBO_GFX_FORMAT_RGBA8, ROBO_GFX_FORMAT_BGRA8, ROBO_GFX_FORMAT_RGB8,
        ROBO_GFX_FORMAT_GRAY8, ROBO_GFX_FORMAT_ALPHA8};
    size_t i;

    for (i = 0u; i < sizeof(k_formats) / sizeof(k_formats[0]); i++) {
        robo_gfx_surface_t *s = NULL;
        robo_gfx_color_rgba8_t in = robo_gfx_rgba8(10, 20, 30, 40);
        robo_gfx_color_rgba8_t out;
        robo_gfx_color_rgba8_t expect;

        ROBO_CHECK_EQ_INT(make_surface(3, 3, k_formats[i], &s), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(s, 0u, 0u, in), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(s, 2u, 2u, in), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &out), ROBO_OK);

        /* Round-trip through pack/unpack semantics. */
        {
            uint8_t packed[8];
            ROBO_CHECK_EQ_INT(robo_gfx_pixel_pack(k_formats[i], in, packed, 8), ROBO_OK);
            ROBO_CHECK_EQ_INT(
                robo_gfx_pixel_unpack(k_formats[i], packed,
                                      robo_gfx_surface_bytes_per_pixel(s), &expect),
                ROBO_OK);
        }
        ROBO_CHECK_EQ_INT(out.r, expect.r);
        ROBO_CHECK_EQ_INT(out.g, expect.g);
        ROBO_CHECK_EQ_INT(out.b, expect.b);
        ROBO_CHECK_EQ_INT(out.a, expect.a);

        ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 3u, 0u, &out),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(s, 0u, 3u, in),
                          ROBO_INVALID_ARGUMENT);
        robo_gfx_surface_release(s);
    }
}

static void test_bmp_clear_small(void)
{
    static const uint32_t k_sizes[] = {1u, 2u, 3u, 4u};
    size_t i;
    uint32_t x;
    uint32_t y;

    for (i = 0u; i < sizeof(k_sizes) / sizeof(k_sizes[0]); i++) {
        robo_gfx_surface_t *s = NULL;
        robo_gfx_color_rgba8_t c;
        uint32_t n = k_sizes[i];

        ROBO_CHECK_EQ_INT(make_surface(n, n, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(s, robo_gfx_rgba8(7, 8, 9, 10)),
                          ROBO_OK);
        for (y = 0u; y < n; y++) {
            for (x = 0u; x < n; x++) {
                ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, x, y, &c), ROBO_OK);
                ROBO_CHECK_EQ_INT(c.r, 7);
                ROBO_CHECK_EQ_INT(c.g, 8);
                ROBO_CHECK_EQ_INT(c.b, 9);
                ROBO_CHECK_EQ_INT(c.a, 10);
            }
        }
        robo_gfx_surface_release(s);
    }
}

static void test_bmp_fill_rect(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(s, robo_gfx_rgba8(0, 0, 0, 0)), ROBO_OK);

    /* Single pixel */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, robo_gfx_rect(1, 1, 1, 1),
                                   robo_gfx_rgba8(1, 2, 3, 4)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 1u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 1);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 0);

    /* Center 2×2 */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, robo_gfx_rect(1, 1, 2, 2),
                                   robo_gfx_rgba8(9, 9, 9, 9)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 2u, 2u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 9);

    /* Full surface */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, robo_gfx_rect(0, 0, 4, 4),
                                   robo_gfx_rgba8(5, 5, 5, 5)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 3u, 3u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 5);

    /* Empty = no-op */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, robo_gfx_rect(0, 0, 0, 4),
                                   robo_gfx_rgba8(1, 1, 1, 1)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 5);

    /* Out of bounds */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, robo_gfx_rect(3, 3, 2, 1),
                                   robo_gfx_rgba8(1, 1, 1, 1)),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, robo_gfx_rect(-1, 0, 1, 1),
                                   robo_gfx_rgba8(1, 1, 1, 1)),
        ROBO_INVALID_ARGUMENT);

    robo_gfx_surface_release(s);
}

static void paint_abc_pattern(robo_gfx_surface_t *s)
{
    /* 3×3 distinct colors A..I */
    static const uint8_t vals[9] = {11, 22, 33, 44, 55, 66, 77, 88, 99};
    uint32_t i;
    for (i = 0u; i < 9u; i++) {
        uint8_t v = vals[i];
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_set_pixel(s, i % 3u, i / 3u, robo_gfx_rgba8(v, v, v, 255)),
            ROBO_OK);
    }
}

static void test_bmp_copy(void)
{
    robo_gfx_surface_t *src = NULL;
    robo_gfx_surface_t *dst = NULL;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(3, 3, ROBO_GFX_FORMAT_RGBA8, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(3, 3, ROBO_GFX_FORMAT_RGBA8, &dst), ROBO_OK);
    paint_abc_pattern(src);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_copy(src, dst), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 0u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 11);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 2u, 2u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 99);

    /* Partial rect */
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(dst, robo_gfx_rgba8(0, 0, 0, 0)), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_copy_rect(src, robo_gfx_rect(1, 1, 2, 2), dst,
                                   robo_gfx_point(0, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 0u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 55); /* E */
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 1u, 1u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 99); /* I */

    /* Format convert RGBA8 → BGRA8 */
    {
        robo_gfx_surface_t *bgra = NULL;
        ROBO_CHECK_EQ_INT(make_surface(3, 3, ROBO_GFX_FORMAT_BGRA8, &bgra), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_copy(src, bgra), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(bgra, 1u, 0u, &c), ROBO_OK);
        ROBO_CHECK_EQ_INT(c.r, 22);
        ROBO_CHECK_EQ_INT(c.g, 22);
        ROBO_CHECK_EQ_INT(c.b, 22);
        robo_gfx_surface_release(bgra);
    }

    /* Size mismatch */
    {
        robo_gfx_surface_t *small = NULL;
        ROBO_CHECK_EQ_INT(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, &small), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_copy(src, small), ROBO_INVALID_ARGUMENT);
        robo_gfx_surface_release(small);
    }

    robo_gfx_surface_release(src);
    robo_gfx_surface_release(dst);
}

static void test_bmp_overlap(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_color_rgba8_t c;
    uint32_t x;

    /* Horizontal row: A B C D  → scroll left by 1 within [0,0,4,1] */
    ROBO_CHECK_EQ_INT(make_surface(4, 1, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    for (x = 0u; x < 4u; x++) {
        uint8_t v = (uint8_t)(10u * (x + 1u));
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_set_pixel(s, x, 0u, robo_gfx_rgba8(v, 0, 0, 255)),
            ROBO_OK);
    }
    /* Copy [1,0,3,1] → (0,0): B C D overwrite A B C */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_copy_rect(s, robo_gfx_rect(1, 0, 3, 1), s, robo_gfx_point(0, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 20);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 30);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 2u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 40);
    robo_gfx_surface_release(s);

    /* Scroll right: rebuild A B C D, copy [0,0,3,1] → (1,0) */
    ROBO_CHECK_EQ_INT(make_surface(4, 1, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    for (x = 0u; x < 4u; x++) {
        uint8_t v = (uint8_t)(10u * (x + 1u));
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_set_pixel(s, x, 0u, robo_gfx_rgba8(v, 0, 0, 255)),
            ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_copy_rect(s, robo_gfx_rect(0, 0, 3, 1), s, robo_gfx_point(1, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 10);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 2u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 20);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 3u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 30);
    robo_gfx_surface_release(s);

    /* Vertical: 3×1 column pattern in 1×3 — use 2×3 for clarity */
    ROBO_CHECK_EQ_INT(make_surface(1, 4, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    for (x = 0u; x < 4u; x++) {
        uint8_t v = (uint8_t)(10u * (x + 1u));
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_set_pixel(s, 0u, x, robo_gfx_rgba8(v, 0, 0, 255)),
            ROBO_OK);
    }
    /* Move down: [0,0,1,3] → (0,1) */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_copy_rect(s, robo_gfx_rect(0, 0, 1, 3), s, robo_gfx_point(0, 1)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 1u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 10);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 3u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 30);
    robo_gfx_surface_release(s);

    /* Move up: rebuild, [0,1,1,3] → (0,0) */
    ROBO_CHECK_EQ_INT(make_surface(1, 4, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    for (x = 0u; x < 4u; x++) {
        uint8_t v = (uint8_t)(10u * (x + 1u));
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_set_pixel(s, 0u, x, robo_gfx_rgba8(v, 0, 0, 255)),
            ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_copy_rect(s, robo_gfx_rect(0, 1, 1, 3), s, robo_gfx_point(0, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 20);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 2u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 40);
    robo_gfx_surface_release(s);
}

static void test_bmp_errors(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_surface_desc_t desc;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(NULL, 0u, 0u, &c),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(s, 0u, 0u, robo_gfx_rgba8(1, 1, 1, 1)),
                      ROBO_OK);

    robo_gfx_surface_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ;
    {
        robo_gfx_surface_t *ro = NULL;
        ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &ro), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_set_pixel(ro, 0u, 0u, robo_gfx_rgba8(1, 1, 1, 1)),
            ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(ro, robo_gfx_rgba8(1, 1, 1, 1)),
                          ROBO_INVALID_ARGUMENT);
        robo_gfx_surface_release(ro);
    }

    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_copy_rect(s, robo_gfx_rect(0, 0, 2, 2), s,
                                   robo_gfx_point(1, 0)),
        ROBO_INVALID_ARGUMENT);

    robo_gfx_surface_release(s);
}

void robo_test_register_graphics_bitmap(void)
{
    robo_test_register("graphics.bmp.getset", test_bmp_get_set_formats);
    robo_test_register("graphics.bmp.clear", test_bmp_clear_small);
    robo_test_register("graphics.bmp.fill", test_bmp_fill_rect);
    robo_test_register("graphics.bmp.copy", test_bmp_copy);
    robo_test_register("graphics.bmp.overlap", test_bmp_overlap);
    robo_test_register("graphics.bmp.errors", test_bmp_errors);
}
