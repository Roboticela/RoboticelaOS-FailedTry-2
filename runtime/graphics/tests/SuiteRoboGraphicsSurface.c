/*
 * RoboGraphics surface suite (Goal 6.4).
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void test_surf_create_formats(void)
{
    static const robo_gfx_pixel_format_t k_formats[] = {
        ROBO_GFX_FORMAT_RGBA8, ROBO_GFX_FORMAT_BGRA8, ROBO_GFX_FORMAT_RGB8,
        ROBO_GFX_FORMAT_GRAY8, ROBO_GFX_FORMAT_ALPHA8};
    size_t i;

    for (i = 0u; i < sizeof(k_formats) / sizeof(k_formats[0]); i++) {
        robo_gfx_surface_desc_t desc;
        robo_gfx_surface_t *s = NULL;
        uint8_t bpp = 0u;
        size_t expect_stride = 0u;

        robo_gfx_surface_desc_init(&desc);
        desc.width = 8;
        desc.height = 4;
        desc.format = k_formats[i];
        ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_bytes_per_pixel(k_formats[i], &bpp),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_gfx_pixel_format_min_stride(k_formats[i], desc.width, &expect_stride),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
        ROBO_CHECK(s != NULL);
        ROBO_CHECK_EQ_INT((int)robo_gfx_surface_width(s), 8);
        ROBO_CHECK_EQ_INT((int)robo_gfx_surface_height(s), 4);
        ROBO_CHECK_EQ_INT((int)robo_gfx_surface_format(s), (int)k_formats[i]);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_bytes_per_pixel(s), bpp);
        ROBO_CHECK_EQ_INT((int)robo_gfx_surface_stride(s), (int)expect_stride);
        ROBO_CHECK_EQ_INT((int)robo_gfx_surface_byte_size(s), (int)(expect_stride * 4u));
        ROBO_CHECK_EQ_INT((int)robo_gfx_surface_ownership(s),
                          (int)ROBO_GFX_SURFACE_OWNED);
        ROBO_CHECK(robo_gfx_surface_pixels_const(s) != NULL);
        ROBO_CHECK(robo_gfx_surface_framebuffer(s) != NULL);
        ROBO_CHECK(robo_gfx_surface_id(s) != ROBO_GFX_SURFACE_ID_NONE);
        ROBO_CHECK(!robo_gfx_surface_is_mapped(s));
        robo_gfx_surface_release(s);
    }
}

static void test_surf_sizes(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 1;
    desc.height = 1;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_byte_size(s), 4);
    robo_gfx_surface_release(s);

    desc.width = 2;
    desc.height = 2;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_byte_size(s), 16);
    robo_gfx_surface_release(s);

    desc.width = 800;
    desc.height = 600;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_stride(s), 3200);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_byte_size(s), 1920000);
    robo_gfx_surface_release(s);
}

static void test_surf_map(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;
    robo_gfx_surface_map_t map;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 3;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_gfx_surface_map(s, ROBO_GFX_SURFACE_ACCESS_READ, &map),
                      ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_is_mapped(s));
    ROBO_CHECK(map.pixels == NULL);
    ROBO_CHECK(map.pixels_const != NULL);
    ROBO_CHECK_EQ_INT((int)map.width, 3);
    ROBO_CHECK_EQ_INT((int)map.height, 2);
    ROBO_CHECK_EQ_INT((int)map.stride_bytes, 12);
    ROBO_CHECK_EQ_INT((int)map.byte_size, 24);
    ROBO_CHECK_EQ_INT((int)map.format, (int)ROBO_GFX_FORMAT_RGBA8);
    ROBO_CHECK_EQ_INT((int)map.access, (int)ROBO_GFX_SURFACE_ACCESS_READ);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_map(s, ROBO_GFX_SURFACE_ACCESS_WRITE, &map),
                      ROBO_BUSY);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_unmap(s), ROBO_OK);
    ROBO_CHECK(!robo_gfx_surface_is_mapped(s));
    ROBO_CHECK_EQ_INT(robo_gfx_surface_unmap(s), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_gfx_surface_map(s, ROBO_GFX_SURFACE_ACCESS_WRITE, &map),
                      ROBO_OK);
    ROBO_CHECK(map.pixels != NULL);
    ROBO_CHECK(map.pixels == map.pixels_const);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_unmap(s), ROBO_OK);

    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_map(s, ROBO_GFX_SURFACE_ACCESS_READ_WRITE, &map), ROBO_OK);
    ROBO_CHECK(map.pixels != NULL);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_unmap(s), ROBO_OK);

    robo_gfx_surface_release(s);
}

static void test_surf_access_modes(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;
    robo_gfx_surface_map_t map;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_pixels(s) == NULL);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_map(s, ROBO_GFX_SURFACE_ACCESS_WRITE, &map),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_map(s, ROBO_GFX_SURFACE_ACCESS_READ, &map),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_unmap(s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear_rgba8(s, robo_gfx_rgba8(1, 2, 3, 4)),
                      ROBO_INVALID_ARGUMENT);
    robo_gfx_surface_release(s);

    desc.access = ROBO_GFX_SURFACE_ACCESS_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_map(s, ROBO_GFX_SURFACE_ACCESS_READ, &map),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_map(s, ROBO_GFX_SURFACE_ACCESS_WRITE, &map),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_unmap(s), ROBO_OK);
    robo_gfx_surface_release(s);
}

static void test_surf_pixel_address(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;
    uint8_t *p00 = NULL;
    uint8_t *p10 = NULL;
    uint8_t *p01 = NULL;
    uint8_t *p11 = NULL;
    uint8_t *row1 = NULL;
    size_t off = 0u;
    uint8_t packed[4];

    robo_gfx_surface_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_gfx_surface_pixel_offset(s, 0u, 0u, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)off, 0);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_pixel_offset(s, 1u, 0u, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)off, 4);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_pixel_offset(s, 0u, 1u, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)off, 8);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_pixel_offset(s, 1u, 1u, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)off, 12);

    ROBO_CHECK_EQ_INT(robo_gfx_surface_row_checked(s, 1u, &row1), ROBO_OK);
    ROBO_CHECK(row1 == robo_gfx_surface_pixels(s) + 8);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_row_checked(s, 2u, &row1), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_gfx_surface_pixel(s, 0u, 0u, &p00), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_pixel(s, 1u, 0u, &p10), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_pixel(s, 0u, 1u, &p01), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_pixel(s, 1u, 1u, &p11), ROBO_OK);

    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_pack(ROBO_GFX_FORMAT_RGBA8, robo_gfx_rgba8(10, 20, 30, 40), packed,
                            4),
        ROBO_OK);
    memcpy(p00, packed, 4);
    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_pack(ROBO_GFX_FORMAT_RGBA8, robo_gfx_rgba8(50, 60, 70, 80), packed,
                            4),
        ROBO_OK);
    memcpy(p10, packed, 4);
    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_pack(ROBO_GFX_FORMAT_RGBA8, robo_gfx_rgba8(90, 100, 110, 120),
                            packed, 4),
        ROBO_OK);
    memcpy(p01, packed, 4);
    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_pack(ROBO_GFX_FORMAT_RGBA8, robo_gfx_rgba8(130, 140, 150, 160),
                            packed, 4),
        ROBO_OK);
    memcpy(p11, packed, 4);

    ROBO_CHECK_EQ_INT(p00[0], 10);
    ROBO_CHECK_EQ_INT(p10[0], 50);
    ROBO_CHECK_EQ_INT(p01[0], 90);
    ROBO_CHECK_EQ_INT(p11[0], 130);
    ROBO_CHECK(p00 + 4 == p10);
    ROBO_CHECK(p00 + 8 == p01);
    ROBO_CHECK(p00 + 12 == p11);

    ROBO_CHECK_EQ_INT(robo_gfx_surface_pixel(s, 2u, 0u, &p00), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_pixel(s, 0u, 2u, &p00), ROBO_INVALID_ARGUMENT);

    robo_gfx_surface_release(s);
}

static void test_surf_clear(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;
    const uint8_t *px;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 2;
    desc.height = 1;
    desc.format = ROBO_GFX_FORMAT_BGRA8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear_rgba8(s, robo_gfx_rgba8(1, 2, 3, 4)),
                      ROBO_OK);
    px = robo_gfx_surface_pixels_const(s);
    ROBO_CHECK_EQ_INT(px[0], 3); /* B */
    ROBO_CHECK_EQ_INT(px[1], 2);
    ROBO_CHECK_EQ_INT(px[2], 1); /* R */
    ROBO_CHECK_EQ_INT(px[3], 4);
    ROBO_CHECK_EQ_INT(px[4], 3);
    robo_gfx_surface_release(s);

    desc.format = ROBO_GFX_FORMAT_GRAY8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear_rgba8(s, robo_gfx_rgba8(100, 0, 0, 255)),
                      ROBO_OK);
    px = robo_gfx_surface_pixels_const(s);
    ROBO_CHECK_EQ_INT(px[0], (int)robo_gfx_color_rgba8_luminance8(
                                 robo_gfx_rgba8(100, 0, 0, 255)));
    robo_gfx_surface_release(s);

    desc.format = ROBO_GFX_FORMAT_ALPHA8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear_rgba8(s, robo_gfx_rgba8(0, 0, 0, 200)),
                      ROBO_OK);
    px = robo_gfx_surface_pixels_const(s);
    ROBO_CHECK_EQ_INT(px[0], 200);
    ROBO_CHECK_EQ_INT(px[1], 200);
    robo_gfx_surface_release(s);
}

static void test_surf_errors(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 0;
    desc.height = 1;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_INVALID_ARGUMENT);
    desc.width = 1;
    desc.height = 0;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_INVALID_ARGUMENT);
    desc.height = 1;
    desc.format = ROBO_GFX_FORMAT_UNKNOWN;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_INVALID_ARGUMENT);
    desc.format = ROBO_GFX_FORMAT_RGBA_F32;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_NOT_SUPPORTED);
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.width = ROBO_GFX_MAX_SURFACE_DIM + 1u;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_INVALID_ARGUMENT);
    desc.width = 1;
    desc.stride_bytes = (SIZE_MAX / 2u) + 1u;
    desc.height = 2;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(s == NULL);
}

static void test_surf_oom(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 4;
    desc.height = 4;
    desc.format = ROBO_GFX_FORMAT_RGBA8;

    /* framebuffer object alloc fails */
    robo_mem_debug_fail_next(1);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OUT_OF_MEMORY);
    ROBO_CHECK(s == NULL);

    /* fb object ok, pixels fail */
    robo_mem_debug_fail_next(2);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OUT_OF_MEMORY);
    ROBO_CHECK(s == NULL);

    /* fb fully ok, surface object fails */
    robo_mem_debug_fail_next(3);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OUT_OF_MEMORY);
    ROBO_CHECK(s == NULL);
    robo_mem_debug_fail_next(0);
}

static void test_surf_borrowed_and_lifetime(void)
{
    uint8_t storage[32];
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;
    robo_gfx_surface_map_t map;

    memset(storage, 0xAB, sizeof(storage));
    robo_gfx_surface_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.stride_bytes = 16;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create_borrowed(&desc, storage, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_ownership(s),
                      (int)ROBO_GFX_SURFACE_BORROWED);
    ROBO_CHECK(robo_gfx_surface_pixels(s) == storage);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_map(s, ROBO_GFX_SURFACE_ACCESS_READ_WRITE, &map),
                      ROBO_OK);
    ROBO_CHECK(map.pixels == storage);
    robo_gfx_surface_retain(s);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_unmap(s), ROBO_OK);
    robo_gfx_surface_release(s);
    robo_gfx_surface_release(s);
    ROBO_CHECK_EQ_INT(storage[0], 0xAB);
}

void robo_test_register_graphics_surface(void)
{
    robo_test_register("graphics.surf.create", test_surf_create_formats);
    robo_test_register("graphics.surf.sizes", test_surf_sizes);
    robo_test_register("graphics.surf.map", test_surf_map);
    robo_test_register("graphics.surf.access", test_surf_access_modes);
    robo_test_register("graphics.surf.address", test_surf_pixel_address);
    robo_test_register("graphics.surf.clear", test_surf_clear);
    robo_test_register("graphics.surf.errors", test_surf_errors);
    robo_test_register("graphics.surf.oom", test_surf_oom);
    robo_test_register("graphics.surf.lifetime", test_surf_borrowed_and_lifetime);
}
