/*
 * RoboGraphics framebuffer suite (Goal 6.3).
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void test_fb_create_formats(void)
{
    static const robo_gfx_pixel_format_t k_formats[] = {
        ROBO_GFX_FORMAT_RGBA8, ROBO_GFX_FORMAT_BGRA8, ROBO_GFX_FORMAT_RGB8,
        ROBO_GFX_FORMAT_GRAY8, ROBO_GFX_FORMAT_ALPHA8};
    size_t i;

    for (i = 0u; i < sizeof(k_formats) / sizeof(k_formats[0]); i++) {
        robo_gfx_framebuffer_desc_t desc;
        robo_gfx_framebuffer_t *fb = NULL;
        uint8_t bpp = 0u;
        size_t expect_stride = 0u;

        robo_gfx_framebuffer_desc_init(&desc);
        desc.width = 8;
        desc.height = 4;
        desc.format = k_formats[i];
        ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_bytes_per_pixel(k_formats[i], &bpp),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_gfx_pixel_format_min_stride(k_formats[i], desc.width, &expect_stride),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_OK);
        ROBO_CHECK(fb != NULL);
        ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_width(fb), 8);
        ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_height(fb), 4);
        ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_format(fb), (int)k_formats[i]);
        ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_bytes_per_pixel(fb), bpp);
        ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_stride(fb), (int)expect_stride);
        ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_byte_size(fb),
                          (int)(expect_stride * 4u));
        ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_ownership(fb),
                          (int)ROBO_GFX_FRAMEBUFFER_OWNED);
        ROBO_CHECK(robo_gfx_framebuffer_pixels(fb) != NULL);
        ROBO_CHECK(robo_gfx_framebuffer_id(fb) != ROBO_GFX_FRAMEBUFFER_ID_NONE);
        robo_gfx_framebuffer_release(fb);
    }
}

static void test_fb_sizes(void)
{
    robo_gfx_framebuffer_desc_t desc;
    robo_gfx_framebuffer_t *fb = NULL;

    robo_gfx_framebuffer_desc_init(&desc);
    desc.width = 1;
    desc.height = 1;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_stride(fb), 4);
    ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_byte_size(fb), 4);
    robo_gfx_framebuffer_release(fb);

    desc.width = 2;
    desc.height = 2;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_byte_size(fb), 16);
    robo_gfx_framebuffer_release(fb);

    desc.width = 800;
    desc.height = 600;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_stride(fb), 3200);
    ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_byte_size(fb), 1920000);
    robo_gfx_framebuffer_release(fb);
}

static void test_fb_zero_init(void)
{
    robo_gfx_framebuffer_desc_t desc;
    robo_gfx_framebuffer_t *fb = NULL;
    const uint8_t *px;
    size_t i;
    size_t n;

    robo_gfx_framebuffer_desc_init(&desc);
    desc.width = 3;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_OK);
    px = robo_gfx_framebuffer_pixels_const(fb);
    n = robo_gfx_framebuffer_byte_size(fb);
    for (i = 0u; i < n; i++) {
        ROBO_CHECK_EQ_INT(px[i], 0);
    }
    robo_gfx_framebuffer_release(fb);
}

static void test_fb_row_and_pixel(void)
{
    robo_gfx_framebuffer_desc_t desc;
    robo_gfx_framebuffer_t *fb = NULL;
    uint8_t *row0 = NULL;
    uint8_t *row1 = NULL;
    uint8_t *px = NULL;
    size_t off = 0u;
    const uint8_t *base;

    robo_gfx_framebuffer_desc_init(&desc);
    desc.width = 4;
    desc.height = 3;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_OK);
    base = robo_gfx_framebuffer_pixels_const(fb);

    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_row(fb, 0u, &row0), ROBO_OK);
    ROBO_CHECK(row0 == base);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_row(fb, 1u, &row1), ROBO_OK);
    ROBO_CHECK(row1 == base + 16);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_row(fb, 2u, &row0), ROBO_OK);
    ROBO_CHECK(row0 == base + 32);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_row(fb, 3u, &row0), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel_offset(fb, 0u, 0u, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)off, 0);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel_offset(fb, 1u, 0u, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)off, 4);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel_offset(fb, 0u, 1u, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)off, 16);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel_offset(fb, 3u, 2u, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)off, 44);

    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel(fb, 3u, 2u, &px), ROBO_OK);
    ROBO_CHECK(px == base + 44);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel(fb, 4u, 0u, &px), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel(fb, 0u, 3u, &px), ROBO_INVALID_ARGUMENT);

    robo_gfx_framebuffer_release(fb);
}

static void test_fb_write_pixels(void)
{
    robo_gfx_framebuffer_desc_t desc;
    robo_gfx_framebuffer_t *fb = NULL;
    uint8_t *p00 = NULL;
    uint8_t *p10 = NULL;
    uint8_t *p01 = NULL;
    uint8_t *p11 = NULL;
    uint8_t packed[4];

    robo_gfx_framebuffer_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel(fb, 0u, 0u, &p00), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel(fb, 1u, 0u, &p10), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel(fb, 0u, 1u, &p01), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel(fb, 1u, 1u, &p11), ROBO_OK);

    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_pack(ROBO_GFX_FORMAT_RGBA8, robo_gfx_rgba8(1, 2, 3, 4), packed, 4),
        ROBO_OK);
    memcpy(p00, packed, 4);
    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_pack(ROBO_GFX_FORMAT_RGBA8, robo_gfx_rgba8(5, 6, 7, 8), packed, 4),
        ROBO_OK);
    memcpy(p10, packed, 4);
    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_pack(ROBO_GFX_FORMAT_RGBA8, robo_gfx_rgba8(9, 10, 11, 12), packed, 4),
        ROBO_OK);
    memcpy(p01, packed, 4);
    ROBO_CHECK_EQ_INT(
        robo_gfx_pixel_pack(ROBO_GFX_FORMAT_RGBA8, robo_gfx_rgba8(13, 14, 15, 16), packed,
                            4),
        ROBO_OK);
    memcpy(p11, packed, 4);

    ROBO_CHECK_EQ_INT(p00[0], 1);
    ROBO_CHECK_EQ_INT(p10[0], 5);
    ROBO_CHECK_EQ_INT(p01[0], 9);
    ROBO_CHECK_EQ_INT(p11[0], 13);
    ROBO_CHECK(p00 + 4 == p10);
    ROBO_CHECK(p00 + 8 == p01);
    ROBO_CHECK(p00 + 12 == p11);

    robo_gfx_framebuffer_release(fb);
}

static void test_fb_custom_stride(void)
{
    robo_gfx_framebuffer_desc_t desc;
    robo_gfx_framebuffer_t *fb = NULL;
    uint8_t *row1 = NULL;

    robo_gfx_framebuffer_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.stride_bytes = 16; /* padded rows */
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_stride(fb), 16);
    ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_byte_size(fb), 32);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_row(fb, 1u, &row1), ROBO_OK);
    ROBO_CHECK(row1 == robo_gfx_framebuffer_pixels(fb) + 16);
    robo_gfx_framebuffer_release(fb);

    desc.stride_bytes = 7; /* too small for 2×RGBA8 */
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_INVALID_ARGUMENT);
}

static void test_fb_errors(void)
{
    robo_gfx_framebuffer_desc_t desc;
    robo_gfx_framebuffer_t *fb = NULL;

    robo_gfx_framebuffer_desc_init(&desc);
    desc.width = 0;
    desc.height = 1;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_INVALID_ARGUMENT);
    desc.width = 1;
    desc.height = 0;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_INVALID_ARGUMENT);
    desc.height = 1;
    desc.format = ROBO_GFX_FORMAT_UNKNOWN;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_INVALID_ARGUMENT);
    desc.format = ROBO_GFX_FORMAT_RGBA_F32;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_NOT_SUPPORTED);
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.width = ROBO_GFX_MAX_SURFACE_DIM + 1u;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(NULL, &fb), ROBO_INVALID_ARGUMENT);
}

static void test_fb_overflow(void)
{
    robo_gfx_framebuffer_desc_t desc;
    robo_gfx_framebuffer_t *fb = NULL;

    robo_gfx_framebuffer_desc_init(&desc);
    desc.width = 1;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.stride_bytes = (SIZE_MAX / 2u) + 1u;
    /* validate_stride needs stride >= min (4); then buffer height*stride overflows */
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(fb == NULL);
}

static void test_fb_borrowed(void)
{
    uint8_t storage[32];
    robo_gfx_framebuffer_desc_t desc;
    robo_gfx_framebuffer_t *fb = NULL;

    memset(storage, 0xCD, sizeof(storage));
    robo_gfx_framebuffer_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.stride_bytes = 16;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create_borrowed(&desc, storage, &fb), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_framebuffer_ownership(fb),
                      (int)ROBO_GFX_FRAMEBUFFER_BORROWED);
    ROBO_CHECK(robo_gfx_framebuffer_pixels(fb) == storage);
    robo_gfx_framebuffer_release(fb);
    ROBO_CHECK_EQ_INT(storage[0], 0xCD);
}

static void test_fb_oom(void)
{
    robo_gfx_framebuffer_desc_t desc;
    robo_gfx_framebuffer_t *fb = NULL;

    robo_gfx_framebuffer_desc_init(&desc);
    desc.width = 4;
    desc.height = 4;
    desc.format = ROBO_GFX_FORMAT_RGBA8;

    robo_mem_debug_fail_next(1);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_OUT_OF_MEMORY);
    ROBO_CHECK(fb == NULL);

    /* Object alloc succeeds, pixel alloc fails */
    robo_mem_debug_fail_next(2);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_OUT_OF_MEMORY);
    ROBO_CHECK(fb == NULL);
    robo_mem_debug_fail_next(0);
}

static void test_fb_lifetime(void)
{
    robo_gfx_framebuffer_desc_t desc;
    robo_gfx_framebuffer_t *fb = NULL;

    robo_gfx_framebuffer_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_GRAY8;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_OK);
    robo_gfx_framebuffer_retain(fb);
    robo_gfx_framebuffer_release(fb);
    robo_gfx_framebuffer_release(fb);
    robo_gfx_framebuffer_release(NULL);
}

void robo_test_register_graphics_framebuffer(void)
{
    robo_test_register("graphics.fb.create", test_fb_create_formats);
    robo_test_register("graphics.fb.sizes", test_fb_sizes);
    robo_test_register("graphics.fb.zero", test_fb_zero_init);
    robo_test_register("graphics.fb.address", test_fb_row_and_pixel);
    robo_test_register("graphics.fb.write", test_fb_write_pixels);
    robo_test_register("graphics.fb.stride", test_fb_custom_stride);
    robo_test_register("graphics.fb.errors", test_fb_errors);
    robo_test_register("graphics.fb.overflow", test_fb_overflow);
    robo_test_register("graphics.fb.borrowed", test_fb_borrowed);
    robo_test_register("graphics.fb.oom", test_fb_oom);
    robo_test_register("graphics.fb.lifetime", test_fb_lifetime);
}
