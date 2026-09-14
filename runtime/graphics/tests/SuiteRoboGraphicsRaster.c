/*
 * RoboGraphics Goal 6.11 — CPU raster backend.
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

static void test_raster_init(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_rect_t clip;
    robo_gfx_surface_desc_t desc;

    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(NULL, &r), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(NULL, NULL), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(make_surface(8, 8, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
    ROBO_CHECK(robo_gfx_raster_target(r) == s);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_opacity(r), 255);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clip(r, &clip), ROBO_OK);
    ROBO_CHECK_EQ_INT(clip.x, 0);
    ROBO_CHECK_EQ_INT(clip.y, 0);
    ROBO_CHECK_EQ_INT(clip.width, 8);
    ROBO_CHECK_EQ_INT(clip.height, 8);
    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);

    /* zero-sized surfaces are rejected by framebuffer create */
    ROBO_CHECK_EQ_INT(make_surface(0, 0, ROBO_GFX_FORMAT_RGBA8, &s),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(s == NULL);

    /* read-only surface rejected */
    robo_gfx_surface_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_INVALID_ARGUMENT);
    robo_gfx_surface_release(s);
}

static void test_raster_formats(void)
{
    static const robo_gfx_pixel_format_t fmts[] = {
        ROBO_GFX_FORMAT_RGBA8, ROBO_GFX_FORMAT_BGRA8, ROBO_GFX_FORMAT_RGB8,
        ROBO_GFX_FORMAT_GRAY8, ROBO_GFX_FORMAT_ALPHA8};
    size_t i;
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_color_rgba8_t c;

    for (i = 0u; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
        ROBO_CHECK_EQ_INT(make_surface(3, 3, fmts[i], &s), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(10, 20, 30)),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_gfx_raster_draw_pixel(r, robo_gfx_point(1, 1),
                                       robo_gfx_rgb8(200, 100, 50)),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 1u, &c), ROBO_OK);
        ROBO_CHECK_EQ_INT(c.a, 255);
        robo_gfx_raster_destroy(r);
        robo_gfx_surface_release(s);
    }
}

static void test_raster_pixel(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 255)),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_pixel(r, robo_gfx_point(1, 2),
                                   robo_gfx_rgb8(255, 0, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 2u, &c), ROBO_OK);
    expect_color(c, 255u, 0u, 0u, 255u);

    /* outside / negative → no-op */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_pixel(r, robo_gfx_point(-1, 0),
                                   robo_gfx_rgb8(1, 1, 1)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_pixel(r, robo_gfx_point(99, 0),
                                   robo_gfx_rgb8(1, 1, 1)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 255u, 255u);

    /* translucent */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_pixel(r, robo_gfx_point(0, 0),
                                   robo_gfx_rgba8(255, 0, 0, 128)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    expect_color(c, 128u, 0u, 127u, 255u);

    /* transparent source */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_pixel(r, robo_gfx_point(3, 3),
                                   robo_gfx_rgba8(255, 255, 0, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 3u, 3u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 255u, 255u);

    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);
}

static void test_raster_rect(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(8, 8, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 0)), ROBO_OK);

    /* 1×1 */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(2, 3, 1, 1),
                                  robo_gfx_rgb8(10, 20, 30)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 2u, 3u, &c), ROBO_OK);
    expect_color(c, 10u, 20u, 30u, 255u);

    /* horizontal / vertical */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(0, 0, 5, 1),
                                  robo_gfx_rgb8(255, 0, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(0, 0, 1, 5),
                                  robo_gfx_rgb8(0, 255, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    expect_color(c, 0u, 255u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 4u, 0u, &c), ROBO_OK);
    expect_color(c, 255u, 0u, 0u, 255u);

    /* empty / outside / negative */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(0, 0, 0, 4),
                                  robo_gfx_rgb8(1, 1, 1)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(-10, -10, 4, 4),
                                  robo_gfx_rgb8(9, 9, 9)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(100, 100, 2, 2),
                                  robo_gfx_rgb8(9, 9, 9)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 1u, &c), ROBO_OK);
    expect_color(c, 0u, 255u, 0u, 255u);

    /* partial clip via negative origin */
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 0)), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(-2, -2, 4, 4),
                                  robo_gfx_rgb8(128, 0, 128)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    expect_color(c, 128u, 0u, 128u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 1u, &c), ROBO_OK);
    expect_color(c, 128u, 0u, 128u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 2u, 2u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 0u, 255u);

    /* invalid rect */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(0, 0, -1, 1),
                                  robo_gfx_rgb8(1, 1, 1)),
        ROBO_INVALID_ARGUMENT);

    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);
}

static void test_raster_clip(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_color_rgba8_t c;
    robo_gfx_rect_t clip;

    ROBO_CHECK_EQ_INT(make_surface(8, 8, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 0)), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_gfx_raster_set_clip(r, robo_gfx_rect(2, 2, 3, 3)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clip(r, &clip), ROBO_OK);
    ROBO_CHECK_EQ_INT(clip.x, 2);
    ROBO_CHECK_EQ_INT(clip.width, 3);

    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(0, 0, 8, 8),
                                  robo_gfx_rgb8(255, 0, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 1u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 2u, 2u, &c), ROBO_OK);
    expect_color(c, 255u, 0u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 4u, 4u, &c), ROBO_OK);
    expect_color(c, 255u, 0u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 5u, 5u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 0u, 255u);

    ROBO_CHECK_EQ_INT(robo_gfx_raster_intersect_clip(r, robo_gfx_rect(3, 3, 4, 4)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clip(r, &clip), ROBO_OK);
    ROBO_CHECK_EQ_INT(clip.x, 3);
    ROBO_CHECK_EQ_INT(clip.y, 3);
    ROBO_CHECK_EQ_INT(clip.width, 2);
    ROBO_CHECK_EQ_INT(clip.height, 2);

    ROBO_CHECK_EQ_INT(robo_gfx_raster_reset_clip(r), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_set_clip(r, robo_gfx_rect(-5, -5, 2, 2)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clip(r, &clip), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_is_empty(clip));

    ROBO_CHECK_EQ_INT(robo_gfx_raster_set_clip(r, robo_gfx_rect(0, 0, -1, 1)),
                      ROBO_INVALID_ARGUMENT);

    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);
}

static void test_raster_line(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(8, 8, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 0)), ROBO_OK);

    /* horizontal */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_line(r, robo_gfx_point(1, 1), robo_gfx_point(5, 1),
                                  robo_gfx_rgb8(255, 0, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 1u, &c), ROBO_OK);
    expect_color(c, 255u, 0u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 5u, 1u, &c), ROBO_OK);
    expect_color(c, 255u, 0u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 6u, 1u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 0u, 255u);

    /* vertical + reversed */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_line(r, robo_gfx_point(3, 6), robo_gfx_point(3, 2),
                                  robo_gfx_rgb8(0, 255, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 3u, 2u, &c), ROBO_OK);
    expect_color(c, 0u, 255u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 3u, 6u, &c), ROBO_OK);
    expect_color(c, 0u, 255u, 0u, 255u);

    /* diagonal */
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 0)), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_line(r, robo_gfx_point(0, 0), robo_gfx_point(3, 3),
                                  robo_gfx_rgb8(0, 0, 255)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 2u, 2u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 255u, 255u);

    /* clipped by raster clip */
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 0)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_set_clip(r, robo_gfx_rect(2, 0, 2, 8)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_line(r, robo_gfx_point(0, 4), robo_gfx_point(7, 4),
                                  robo_gfx_rgb8(255, 255, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 4u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 2u, 4u, &c), ROBO_OK);
    expect_color(c, 255u, 255u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 3u, 4u, &c), ROBO_OK);
    expect_color(c, 255u, 255u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 4u, 4u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 0u, 255u);

    /* fully outside */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_line(r, robo_gfx_point(-20, -20),
                                  robo_gfx_point(-10, -10),
                                  robo_gfx_rgb8(1, 1, 1)),
        ROBO_OK);

    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);
}

static void test_raster_opacity_composite(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 255)),
                      ROBO_OK);

    robo_gfx_raster_set_opacity(r, 0u);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(0, 0, 4, 4),
                                  robo_gfx_rgb8(255, 0, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 255u, 255u);

    robo_gfx_raster_set_opacity(r, 128u);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(0, 0, 2, 2),
                                  robo_gfx_rgb8(255, 0, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    expect_color(c, 128u, 0u, 127u, 255u);

    robo_gfx_raster_set_opacity(r, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 0)), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(0, 0, 1, 1),
                                  robo_gfx_rgba8(0, 0, 255, 128)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    /* 50% blue over opaque black → (0,0,128,255) */
    expect_color(c, 0u, 0u, 128u, 255u);

    /* translucent over transparent destination */
    ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(s, 1u, 1u,
                                                 robo_gfx_rgba8(0, 0, 0, 0)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_pixel(r, robo_gfx_point(1, 1),
                                   robo_gfx_rgba8(0, 0, 255, 128)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 1u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 255u, 128u);

    /* translucent over translucent */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_pixel(r, robo_gfx_point(0, 0),
                                   robo_gfx_rgba8(255, 0, 0, 128)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    ROBO_CHECK(c.a > 128u);
    ROBO_CHECK(c.r > 0u);

    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);
}

static void test_raster_ordering(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 0)), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(0, 0, 3, 3),
                                  robo_gfx_rgb8(255, 0, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(1, 1, 2, 2),
                                  robo_gfx_rgb8(0, 255, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_pixel(r, robo_gfx_point(1, 1),
                                   robo_gfx_rgb8(0, 0, 255)),
        ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    expect_color(c, 255u, 0u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 2u, 2u, &c), ROBO_OK);
    expect_color(c, 0u, 255u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 1u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 255u, 255u);

    /* repeat → deterministic */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_pixel(r, robo_gfx_point(1, 1),
                                   robo_gfx_rgb8(0, 0, 255)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 1u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 255u, 255u);

    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);
}

static void test_raster_path_boundary(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_path_t *path = NULL;

    ROBO_CHECK_EQ_INT(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(0, 0)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(3, 3)), ROBO_OK);

    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_path(r, path, robo_gfx_rgb8(1, 2, 3)),
        ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_stroke_path(r, path, robo_gfx_rgb8(1, 2, 3)),
        ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_fill_path(r, NULL, robo_gfx_rgb8(1, 2, 3)),
                      ROBO_INVALID_ARGUMENT);

    robo_gfx_path_release(path);
    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);
}

static void test_raster_numeric(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(16, 16, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(0, 0, 0)), ROBO_OK);

    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_line(r, robo_gfx_point(-1000, 8),
                                  robo_gfx_point(1000, 8),
                                  robo_gfx_rgb8(255, 255, 255)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 8u, &c), ROBO_OK);
    expect_color(c, 255u, 255u, 255u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 15u, 8u, &c), ROBO_OK);
    expect_color(c, 255u, 255u, 255u, 255u);

    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(-100, -100, 50, 50),
                                  robo_gfx_rgb8(1, 2, 3)),
        ROBO_OK);

    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);
}

static void test_raster_smoke(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;
    uint32_t y;
    uint32_t x;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(64, 64, ROBO_GFX_FORMAT_BGRA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clear(r, robo_gfx_rgb8(32, 32, 32)),
                      ROBO_OK);
    robo_gfx_raster_set_opacity(r, 200u);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(r, robo_gfx_rect(8, 8, 48, 48),
                                  robo_gfx_rgba8(0, 128, 255, 180)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_line(r, robo_gfx_point(0, 0), robo_gfx_point(63, 63),
                                  robo_gfx_rgb8(255, 255, 255)),
        ROBO_OK);
    for (y = 0u; y < 64u; y += 16u) {
        for (x = 0u; x < 64u; x += 16u) {
            ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, x, y, &c), ROBO_OK);
            ROBO_CHECK_EQ_INT(c.a, 255);
        }
    }
    ROBO_CHECK_EQ_INT(robo_gfx_present_null(s), ROBO_OK);
    robo_gfx_raster_destroy(r);
    robo_gfx_surface_release(s);
}

static void test_raster_errors(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_raster_t *r = NULL;

    ROBO_CHECK_EQ_INT(robo_gfx_raster_fill_rect(NULL, robo_gfx_rect(0, 0, 1, 1),
                                                robo_gfx_rgb8(1, 1, 1)),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(s, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_clip(r, NULL), ROBO_INVALID_ARGUMENT);
    robo_gfx_raster_destroy(r);
    robo_gfx_raster_destroy(NULL);
    robo_gfx_surface_release(s);
}

void robo_test_register_graphics_raster(void)
{
    robo_test_register("graphics.raster.init", test_raster_init);
    robo_test_register("graphics.raster.formats", test_raster_formats);
    robo_test_register("graphics.raster.pixel", test_raster_pixel);
    robo_test_register("graphics.raster.rect", test_raster_rect);
    robo_test_register("graphics.raster.clip", test_raster_clip);
    robo_test_register("graphics.raster.line", test_raster_line);
    robo_test_register("graphics.raster.opacity", test_raster_opacity_composite);
    robo_test_register("graphics.raster.order", test_raster_ordering);
    robo_test_register("graphics.raster.path", test_raster_path_boundary);
    robo_test_register("graphics.raster.numeric", test_raster_numeric);
    robo_test_register("graphics.raster.smoke", test_raster_smoke);
    robo_test_register("graphics.raster.errors", test_raster_errors);
}
