/*
 * RoboGraphics pixel-format suite (Goal 6.2).
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <stddef.h>
#include <string.h>

static void test_pf_metadata_table(void)
{
    static const struct {
        robo_gfx_pixel_format_t format;
        uint8_t bpp;
        uint8_t channels;
        uint8_t alpha;
        uint8_t gray;
        const char *name;
    } k_rows[] = {
        {ROBO_GFX_FORMAT_RGBA8, 4, 4, 1, 0, "RGBA8"},
        {ROBO_GFX_FORMAT_BGRA8, 4, 4, 1, 0, "BGRA8"},
        {ROBO_GFX_FORMAT_RGB8, 3, 3, 0, 0, "RGB8"},
        {ROBO_GFX_FORMAT_GRAY8, 1, 1, 0, 1, "GRAY8"},
        {ROBO_GFX_FORMAT_ALPHA8, 1, 1, 1, 0, "ALPHA8"},
    };
    size_t i;

    for (i = 0u; i < sizeof(k_rows) / sizeof(k_rows[0]); i++) {
        robo_gfx_pixel_format_info_t info;
        uint8_t bpp = 0u;
        ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_info(k_rows[i].format, &info), ROBO_OK);
        ROBO_CHECK(robo_gfx_pixel_format_is_known(k_rows[i].format));
        ROBO_CHECK(robo_gfx_pixel_format_is_implemented(k_rows[i].format));
        ROBO_CHECK_EQ_INT(info.bytes_per_pixel, k_rows[i].bpp);
        ROBO_CHECK_EQ_INT(info.channel_count, k_rows[i].channels);
        ROBO_CHECK_EQ_INT(info.has_alpha, k_rows[i].alpha);
        ROBO_CHECK_EQ_INT(info.alpha_stored, k_rows[i].alpha);
        ROBO_CHECK_EQ_INT(info.is_grayscale, k_rows[i].gray);
        ROBO_CHECK_EQ_INT(info.channel_bits, 8);
        ROBO_CHECK_EQ_INT(info.is_normalized_int, 1);
        ROBO_CHECK_EQ_INT(info.is_byte_addressable, 1);
        ROBO_CHECK_STR_EQ(info.name, k_rows[i].name);
        ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_bytes_per_pixel(k_rows[i].format, &bpp),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(bpp, k_rows[i].bpp);
    }

    ROBO_CHECK(!robo_gfx_pixel_format_is_known(ROBO_GFX_FORMAT_UNKNOWN));
    ROBO_CHECK(!robo_gfx_pixel_format_is_implemented(ROBO_GFX_FORMAT_UNKNOWN));
    ROBO_CHECK(robo_gfx_pixel_format_is_known(ROBO_GFX_FORMAT_RGBA16));
    ROBO_CHECK(!robo_gfx_pixel_format_is_implemented(ROBO_GFX_FORMAT_RGBA16));
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_bytes_per_pixel(ROBO_GFX_FORMAT_UNKNOWN, NULL),
                      ROBO_INVALID_ARGUMENT);
}

static void test_pf_row_and_offset(void)
{
    size_t stride = 0u;
    size_t bytes = 0u;
    size_t off = 0u;

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_row_bytes(ROBO_GFX_FORMAT_RGBA8, 1u, &stride),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)stride, 4);
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_row_bytes(ROBO_GFX_FORMAT_RGBA8, 2u, &stride),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)stride, 8);
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_row_bytes(ROBO_GFX_FORMAT_RGB8, 10u, &stride),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)stride, 30);

    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_format_validate_stride(ROBO_GFX_FORMAT_RGBA8, 4u, 16u), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_format_validate_stride(ROBO_GFX_FORMAT_RGBA8, 4u, 15u),
        ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_format_buffer_bytes(ROBO_GFX_FORMAT_RGBA8, 4u, 3u, 0u, &bytes),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)bytes, 48);

    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_format_pixel_offset(ROBO_GFX_FORMAT_RGBA8, 16u, 0u, 0u, &off),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)off, 0);
    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_format_pixel_offset(ROBO_GFX_FORMAT_RGBA8, 16u, 1u, 0u, &off),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)off, 4);
    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_format_pixel_offset(ROBO_GFX_FORMAT_RGBA8, 16u, 0u, 1u, &off),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)off, 16);
    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_format_pixel_offset(ROBO_GFX_FORMAT_RGBA8, 16u, 3u, 2u, &off),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)off, 44);
}

static void test_pf_overflow(void)
{
    size_t stride = 0u;
    size_t bytes = 0u;
    size_t off = 0u;

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_min_stride(ROBO_GFX_FORMAT_RGBA8, 0u, &stride),
                      ROBO_INVALID_ARGUMENT);
    /* height × stride overflow */
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_buffer_bytes(ROBO_GFX_FORMAT_RGBA8, 1u, 2u,
                                                         (SIZE_MAX / 2u) + 1u, &bytes),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_buffer_bytes(ROBO_GFX_FORMAT_RGBA8, 4u, 0u, 0u,
                                                         &bytes),
                      ROBO_INVALID_ARGUMENT);
    /* stride too small for x */
    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_format_pixel_offset(ROBO_GFX_FORMAT_RGBA8, 4u, 1u, 0u, &off),
        ROBO_INVALID_ARGUMENT);
}

static void test_pf_rgba_bgra(void)
{
    uint8_t rgba[4];
    uint8_t bgra[4];
    robo_gfx_color_rgba8_t c;
    robo_gfx_color_rgba8_t out;

    c = robo_gfx_rgba8(10, 20, 30, 40);
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_pack(ROBO_GFX_FORMAT_RGBA8, c, rgba, sizeof(rgba)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(rgba[0], 10);
    ROBO_CHECK_EQ_INT(rgba[1], 20);
    ROBO_CHECK_EQ_INT(rgba[2], 30);
    ROBO_CHECK_EQ_INT(rgba[3], 40);

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_pack(ROBO_GFX_FORMAT_BGRA8, c, bgra, sizeof(bgra)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(bgra[0], 30);
    ROBO_CHECK_EQ_INT(bgra[1], 20);
    ROBO_CHECK_EQ_INT(bgra[2], 10);
    ROBO_CHECK_EQ_INT(bgra[3], 40);

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_convert(ROBO_GFX_FORMAT_RGBA8, rgba, 4, ROBO_GFX_FORMAT_BGRA8,
                                             bgra, 4),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(bgra[0], 30);
    ROBO_CHECK_EQ_INT(bgra[2], 10);
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_unpack(ROBO_GFX_FORMAT_BGRA8, bgra, 4, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.r, 10);
    ROBO_CHECK_EQ_INT(out.g, 20);
    ROBO_CHECK_EQ_INT(out.b, 30);
    ROBO_CHECK_EQ_INT(out.a, 40);
}

static void test_pf_rgb_alpha_rules(void)
{
    uint8_t rgba[4] = {10, 20, 30, 40};
    uint8_t rgb[3];
    uint8_t rgba2[4];
    robo_gfx_color_rgba8_t out;

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_convert(ROBO_GFX_FORMAT_RGBA8, rgba, 4, ROBO_GFX_FORMAT_RGB8,
                                             rgb, 3),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(rgb[0], 10);
    ROBO_CHECK_EQ_INT(rgb[1], 20);
    ROBO_CHECK_EQ_INT(rgb[2], 30);

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_convert(ROBO_GFX_FORMAT_RGB8, rgb, 3, ROBO_GFX_FORMAT_RGBA8,
                                             rgba2, 4),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(rgba2[0], 10);
    ROBO_CHECK_EQ_INT(rgba2[1], 20);
    ROBO_CHECK_EQ_INT(rgba2[2], 30);
    ROBO_CHECK_EQ_INT(rgba2[3], 255); /* opaque alpha */

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_unpack(ROBO_GFX_FORMAT_RGB8, rgb, 3, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.a, 255);
}

static void test_pf_gray(void)
{
    uint8_t rgba[4] = {100, 150, 200, 255};
    uint8_t gray[1];
    uint8_t rgba2[4];
    uint8_t y;

    y = robo_gfx_color_rgba8_luminance8(robo_gfx_rgba8(100, 150, 200, 255));
    ROBO_CHECK_EQ_INT(y, (int)((100u * 30u + 150u * 59u + 200u * 11u) / 100u));

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_convert(ROBO_GFX_FORMAT_RGBA8, rgba, 4, ROBO_GFX_FORMAT_GRAY8,
                                             gray, 1),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(gray[0], y);

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_convert(ROBO_GFX_FORMAT_GRAY8, gray, 1, ROBO_GFX_FORMAT_RGBA8,
                                             rgba2, 4),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(rgba2[0], y);
    ROBO_CHECK_EQ_INT(rgba2[1], y);
    ROBO_CHECK_EQ_INT(rgba2[2], y);
    ROBO_CHECK_EQ_INT(rgba2[3], 255);
}

static void test_pf_alpha8(void)
{
    uint8_t a0[1] = {0};
    uint8_t a127[1] = {127};
    uint8_t a255[1] = {255};
    uint8_t rgba[4];
    uint8_t out_a[1];
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_unpack(ROBO_GFX_FORMAT_ALPHA8, a0, 1, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 0);
    ROBO_CHECK_EQ_INT(c.g, 0);
    ROBO_CHECK_EQ_INT(c.b, 0);
    ROBO_CHECK_EQ_INT(c.a, 0);

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_unpack(ROBO_GFX_FORMAT_ALPHA8, a127, 1, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.a, 127);

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_unpack(ROBO_GFX_FORMAT_ALPHA8, a255, 1, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.a, 255);

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_pack(ROBO_GFX_FORMAT_ALPHA8, robo_gfx_rgba8(1, 2, 3, 200),
                                          out_a, 1),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(out_a[0], 200);

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_convert(ROBO_GFX_FORMAT_RGBA8,
                                             (const uint8_t *)"\x01\x02\x03\x04", 4,
                                             ROBO_GFX_FORMAT_ALPHA8, out_a, 1),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(out_a[0], 4);

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_convert(ROBO_GFX_FORMAT_ALPHA8, a127, 1, ROBO_GFX_FORMAT_RGBA8,
                                             rgba, 4),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(rgba[0], 0);
    ROBO_CHECK_EQ_INT(rgba[3], 127);
}

static void test_pf_invalid(void)
{
    uint8_t buf[4];
    robo_gfx_color_rgba8_t c;
    uint8_t bpp = 0u;

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_info(ROBO_GFX_FORMAT_UNKNOWN, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_bytes_per_pixel((robo_gfx_pixel_format_t)999, &bpp),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_pack(ROBO_GFX_FORMAT_RGBA16, robo_gfx_rgb8(1, 2, 3), buf, 8),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_unpack(ROBO_GFX_FORMAT_RGBA_F32, buf, 16, &c),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_pack(ROBO_GFX_FORMAT_RGBA8, robo_gfx_rgb8(1, 2, 3), buf, 3),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_convert(ROBO_GFX_FORMAT_RGBA8, buf, 4, ROBO_GFX_FORMAT_RGBA16,
                                             buf, 8),
                      ROBO_NOT_SUPPORTED);
}

static void test_pf_surface_alpha8(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;
    const uint8_t *px;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 2;
    desc.height = 1;
    desc.format = ROBO_GFX_FORMAT_ALPHA8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear_rgba8(s, robo_gfx_rgba8(0, 0, 0, 200)), ROBO_OK);
    px = robo_gfx_surface_pixels_const(s);
    ROBO_CHECK_EQ_INT(px[0], 200);
    ROBO_CHECK_EQ_INT(px[1], 200);
    robo_gfx_surface_release(s);
}

void robo_test_register_graphics_pixel_format(void)
{
    robo_test_register("graphics.pf.meta", test_pf_metadata_table);
    robo_test_register("graphics.pf.row", test_pf_row_and_offset);
    robo_test_register("graphics.pf.overflow", test_pf_overflow);
    robo_test_register("graphics.pf.rgba_bgra", test_pf_rgba_bgra);
    robo_test_register("graphics.pf.rgb_alpha", test_pf_rgb_alpha_rules);
    robo_test_register("graphics.pf.gray", test_pf_gray);
    robo_test_register("graphics.pf.alpha8", test_pf_alpha8);
    robo_test_register("graphics.pf.invalid", test_pf_invalid);
    robo_test_register("graphics.pf.surface", test_pf_surface_alpha8);
}
