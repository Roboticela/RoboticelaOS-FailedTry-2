/*
 * RoboGraphics Goal 6.14 — screenshot / framebuffer export.
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static robo_result_t make_surface(uint32_t w,
                                  uint32_t h,
                                  robo_gfx_pixel_format_t fmt,
                                  size_t stride,
                                  robo_gfx_surface_t **out)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_desc_init(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = fmt;
    desc.stride_bytes = stride;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    return robo_gfx_surface_create(&desc, out);
}

static void expect_color(robo_gfx_color_rgba8_t got,
                         uint8_t r,
                         uint8_t g,
                         uint8_t b,
                         uint8_t a)
{
    ROBO_CHECK_EQ_INT(got.r, r);
    ROBO_CHECK_EQ_INT(got.g, g);
    ROBO_CHECK_EQ_INT(got.b, b);
    ROBO_CHECK_EQ_INT(got.a, a);
}

static void test_shot_full_rgba(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_capture_options_t opts;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK(make_surface(1, 1, ROBO_GFX_FORMAT_RGBA8, 0, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 0, 0,
                                          robo_gfx_rgba8(10, 20, 30, 40)) ==
               ROBO_OK);

    robo_gfx_capture_options_init(&opts);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &opts, &shot) == ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_width(shot), 1);
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_height(shot), 1);
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_format(shot), ROBO_GFX_FORMAT_RGBA8);
    ROBO_CHECK(robo_gfx_surface_get_pixel(robo_gfx_screenshot_surface(shot), 0,
                                          0, &c) == ROBO_OK);
    expect_color(c, 10, 20, 30, 40);

    robo_gfx_screenshot_release(shot);
    robo_gfx_surface_release(s);
}

static void test_shot_immutability(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_capture_options_t opts;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, 0, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_clear(s, robo_gfx_rgba8(1, 2, 3, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 1, 1,
                                          robo_gfx_rgba8(9, 8, 7, 6)) ==
               ROBO_OK);

    robo_gfx_capture_options_init(&opts);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &opts, &shot) == ROBO_OK);

    ROBO_CHECK(robo_gfx_surface_clear(s, robo_gfx_rgba8(255, 0, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_get_pixel(robo_gfx_screenshot_surface(shot), 1,
                                          1, &c) == ROBO_OK);
    expect_color(c, 9, 8, 7, 6);
    ROBO_CHECK(robo_gfx_surface_get_pixel(robo_gfx_screenshot_surface(shot), 0,
                                          0, &c) == ROBO_OK);
    expect_color(c, 1, 2, 3, 255);

    robo_gfx_screenshot_release(shot);
    robo_gfx_surface_release(s);
}

static void test_shot_region_policies(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_capture_options_t opts;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, 0, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_clear(s, robo_gfx_rgba8(0, 0, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 2, 1,
                                          robo_gfx_rgba8(11, 22, 33, 44)) ==
               ROBO_OK);

    /* Interior STRICT */
    robo_gfx_capture_options_init(&opts);
    opts.use_full_extent = 0;
    opts.region_policy = ROBO_GFX_CAPTURE_REGION_STRICT;
    opts.region = robo_gfx_rect(1, 1, 2, 2);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &opts, &shot) == ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_width(shot), 2);
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_height(shot), 2);
    ROBO_CHECK(robo_gfx_surface_get_pixel(robo_gfx_screenshot_surface(shot), 1,
                                          0, &c) == ROBO_OK);
    expect_color(c, 11, 22, 33, 44);
    robo_gfx_screenshot_release(shot);

    /* Edge one-pixel */
    opts.region = robo_gfx_rect(3, 3, 1, 1);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &opts, &shot) == ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_width(shot), 1);
    robo_gfx_screenshot_release(shot);

    /* Empty STRICT */
    opts.region = robo_gfx_rect(0, 0, 0, 1);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &opts, &shot) ==
               ROBO_INVALID_ARGUMENT);

    /* Outside STRICT */
    opts.region = robo_gfx_rect(3, 3, 2, 2);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &opts, &shot) ==
               ROBO_INVALID_ARGUMENT);

    /* Partial CLIP → intersection */
    opts.region_policy = ROBO_GFX_CAPTURE_REGION_CLIP;
    opts.region = robo_gfx_rect(2, 2, 8, 8);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &opts, &shot) == ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_width(shot), 2);
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_height(shot), 2);
    robo_gfx_screenshot_release(shot);

    /* Fully outside CLIP → empty fail */
    opts.region = robo_gfx_rect(10, 10, 2, 2);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &opts, &shot) ==
               ROBO_INVALID_ARGUMENT);

    /* Negative origin CLIP intersecting */
    opts.region = robo_gfx_rect(-1, 0, 2, 1);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &opts, &shot) == ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_width(shot), 1);
    robo_gfx_screenshot_release(shot);

    robo_gfx_surface_release(s);
}

static void test_shot_framebuffer_and_stride(void)
{
    robo_gfx_framebuffer_desc_t fb_desc;
    robo_gfx_framebuffer_t *fb = NULL;
    robo_gfx_surface_t *s = NULL;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_capture_options_t opts;
    robo_gfx_color_rgba8_t c;
    uint32_t x;
    uint32_t y;

    robo_gfx_framebuffer_desc_init(&fb_desc);
    fb_desc.width = 5;
    fb_desc.height = 3;
    fb_desc.format = ROBO_GFX_FORMAT_RGB8;
    fb_desc.stride_bytes = 5u * 3u + 7u;
    ROBO_CHECK(robo_gfx_framebuffer_create(&fb_desc, &fb) == ROBO_OK);

    ROBO_CHECK(make_surface(5, 3, ROBO_GFX_FORMAT_RGB8, fb_desc.stride_bytes,
                            &s) == ROBO_OK);
    /* Paint via surface then copy path from FB separately */
    for (y = 0u; y < 3u; y++) {
        for (x = 0u; x < 5u; x++) {
            ROBO_CHECK(robo_gfx_surface_set_pixel(
                           s, x, y,
                           robo_gfx_rgb8((uint8_t)(x * 10u), (uint8_t)(y * 20u),
                                         99)) == ROBO_OK);
        }
    }
    /* Mirror into framebuffer bytes */
    {
        size_t bpp = 3u;
        size_t stride = robo_gfx_framebuffer_stride(fb);
        uint8_t *dst = robo_gfx_framebuffer_pixels(fb);
        const uint8_t *src = robo_gfx_surface_pixels_const(s);
        size_t src_stride = robo_gfx_surface_stride(s);
        for (y = 0u; y < 3u; y++) {
            memcpy(dst + y * stride, src + y * src_stride, 5u * bpp);
        }
    }

    robo_gfx_capture_options_init(&opts);
    ROBO_CHECK(robo_gfx_screenshot_capture_framebuffer(fb, &opts, &shot) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_get_pixel(robo_gfx_screenshot_surface(shot), 4,
                                          2, &c) == ROBO_OK);
    expect_color(c, 40, 40, 99, 255);
    robo_gfx_screenshot_release(shot);

    opts.use_full_extent = 0;
    opts.region_policy = ROBO_GFX_CAPTURE_REGION_STRICT;
    opts.region = robo_gfx_rect(1, 1, 2, 1);
    ROBO_CHECK(robo_gfx_screenshot_capture(
                   ROBO_GFX_CAPTURE_FRAMEBUFFER, NULL, fb, &opts, &shot) ==
               ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_width(shot), 2);
    ROBO_CHECK(robo_gfx_surface_get_pixel(robo_gfx_screenshot_surface(shot), 0,
                                          0, &c) == ROBO_OK);
    expect_color(c, 10, 20, 99, 255);
    robo_gfx_screenshot_release(shot);

    robo_gfx_surface_release(s);
    robo_gfx_framebuffer_release(fb);
}

static void test_shot_buffer_selection(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_capture_options_t opts;

    ROBO_CHECK(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, 0, &s) == ROBO_OK);
    robo_gfx_capture_options_init(&opts);

    ROBO_CHECK(robo_gfx_screenshot_capture(ROBO_GFX_CAPTURE_FRONT_BUFFER, s,
                                           NULL, &opts, &shot) ==
               ROBO_NOT_SUPPORTED);
    ROBO_CHECK(robo_gfx_screenshot_capture(ROBO_GFX_CAPTURE_BACK_BUFFER, s, NULL,
                                           &opts, &shot) ==
               ROBO_NOT_SUPPORTED);
    ROBO_CHECK(robo_gfx_screenshot_capture(ROBO_GFX_CAPTURE_SURFACE, NULL, NULL,
                                           &opts, &shot) ==
               ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(robo_gfx_screenshot_capture(ROBO_GFX_CAPTURE_SURFACE, s, NULL,
                                           &opts, &shot) == ROBO_OK);
    robo_gfx_screenshot_release(shot);
    robo_gfx_surface_release(s);
}

static void test_shot_export_deterministic(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_capture_options_t caps;
    robo_gfx_encode_options_t enc;
    robo_gfx_encoded_buffer_t a;
    robo_gfx_encoded_buffer_t b;
    robo_gfx_encoded_buffer_t c;
    robo_gfx_image_t *img = NULL;

    ROBO_CHECK(make_surface(3, 2, ROBO_GFX_FORMAT_RGBA8, 0, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 0, 0,
                                          robo_gfx_rgba8(255, 0, 0, 128)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 1, 0,
                                          robo_gfx_rgba8(0, 255, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 2, 1,
                                          robo_gfx_rgba8(0, 0, 255, 0)) ==
               ROBO_OK);

    robo_gfx_capture_options_init(&caps);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &caps, &shot) == ROBO_OK);

    robo_gfx_encode_options_init(&enc);
    ROBO_CHECK(robo_gfx_screenshot_export(shot, &enc, &a) == ROBO_OK);
    ROBO_CHECK(robo_gfx_screenshot_export(shot, &enc, &b) == ROBO_OK);
    ROBO_CHECK(a.size == b.size);
    ROBO_CHECK(memcmp(a.data, b.data, a.size) == 0);

    enc.container = ROBO_GFX_ENCODE_BMP;
    ROBO_CHECK(robo_gfx_screenshot_export(shot, &enc, &c) == ROBO_OK);
    ROBO_CHECK(c.size >= 14u);
    ROBO_CHECK(c.data[0] == 'B' && c.data[1] == 'M');

    ROBO_CHECK(robo_gfx_screenshot_to_image(shot, &img) == ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_image_width(img), 3);

    ROBO_CHECK(robo_gfx_surface_export_screenshot(s, &caps, &enc, &a) ==
               ROBO_OK);
    ROBO_CHECK(a.size > 0u);

    robo_gfx_encoded_buffer_release(&a);
    robo_gfx_encoded_buffer_release(&b);
    robo_gfx_encoded_buffer_release(&c);
    robo_gfx_image_release(img);
    robo_gfx_screenshot_release(shot);
    robo_gfx_surface_release(s);
}

static void test_shot_alpha_and_channels(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_capture_options_t opts;
    robo_gfx_encode_options_t enc;
    robo_gfx_encoded_buffer_t buf;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK(make_surface(2, 1, ROBO_GFX_FORMAT_BGRA8, 0, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 0, 0,
                                          robo_gfx_rgba8(1, 2, 3, 4)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 1, 0,
                                          robo_gfx_rgba8(255, 255, 255, 0)) ==
               ROBO_OK);

    robo_gfx_capture_options_init(&opts);
    ROBO_CHECK(robo_gfx_screenshot_capture_surface(s, &opts, &shot) == ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_format(shot), ROBO_GFX_FORMAT_BGRA8);
    ROBO_CHECK(robo_gfx_surface_get_pixel(robo_gfx_screenshot_surface(shot), 0,
                                          0, &c) == ROBO_OK);
    expect_color(c, 1, 2, 3, 4);

    robo_gfx_encode_options_init(&enc);
    enc.layout = ROBO_GFX_ENCODE_LAYOUT_RGB8;
    ROBO_CHECK(robo_gfx_screenshot_export(shot, &enc, &buf) ==
               ROBO_INVALID_ARGUMENT);
    enc.background_set = 1;
    enc.background = robo_gfx_rgb8(0, 0, 0);
    ROBO_CHECK(robo_gfx_screenshot_export(shot, &enc, &buf) == ROBO_OK);

    robo_gfx_encoded_buffer_release(&buf);
    robo_gfx_screenshot_release(shot);
    robo_gfx_surface_release(s);
}

static void test_shot_safety(void)
{
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_capture_options_t opts;
    robo_gfx_encode_options_t enc;
    robo_gfx_encoded_buffer_t buf;

    robo_gfx_capture_options_init(&opts);
    robo_gfx_encode_options_init(&enc);

    ROBO_CHECK(robo_gfx_screenshot_capture_surface(NULL, &opts, &shot) ==
               ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(robo_gfx_screenshot_capture_framebuffer(NULL, &opts, &shot) ==
               ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(robo_gfx_screenshot_export(NULL, &enc, &buf) ==
               ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(robo_gfx_surface_export_screenshot(NULL, &opts, &enc, &buf) ==
               ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_width(NULL), 0);
    ROBO_CHECK(robo_gfx_screenshot_surface(NULL) == NULL);
}

void robo_test_register_graphics_screenshot(void)
{
    robo_test_register("graphics.shot.full_rgba", test_shot_full_rgba);
    robo_test_register("graphics.shot.immutable", test_shot_immutability);
    robo_test_register("graphics.shot.region", test_shot_region_policies);
    robo_test_register("graphics.shot.framebuffer_stride",
                       test_shot_framebuffer_and_stride);
    robo_test_register("graphics.shot.buffer_select",
                       test_shot_buffer_selection);
    robo_test_register("graphics.shot.export", test_shot_export_deterministic);
    robo_test_register("graphics.shot.alpha_channels",
                       test_shot_alpha_and_channels);
    robo_test_register("graphics.shot.safety", test_shot_safety);
}
