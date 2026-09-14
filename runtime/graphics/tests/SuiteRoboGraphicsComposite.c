/*
 * RoboGraphics Goal 6.10 — source-over alpha compositing.
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

static int color_eq(robo_gfx_color_rgba8_t a, robo_gfx_color_rgba8_t b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
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

static void test_comp_mul_div255(void)
{
    ROBO_CHECK_EQ_INT(robo_gfx_mul_div255(0u, 255u), 0);
    ROBO_CHECK_EQ_INT(robo_gfx_mul_div255(255u, 255u), 255);
    ROBO_CHECK_EQ_INT(robo_gfx_mul_div255(255u, 128u), 128);
    ROBO_CHECK_EQ_INT(robo_gfx_mul_div255(128u, 128u), 64);
    ROBO_CHECK_EQ_INT(robo_gfx_mul_div255(1u, 1u), 0); /* rounds down */
}

static void test_comp_premul_roundtrip(void)
{
    robo_gfx_color_rgba8_t s = robo_gfx_rgba8(200u, 100u, 50u, 255u);
    robo_gfx_color_rgba8_t p = robo_gfx_color_premultiply(s);
    robo_gfx_color_rgba8_t u = robo_gfx_color_unpremultiply(p);

    ROBO_CHECK(color_eq(s, p));
    ROBO_CHECK(color_eq(s, u));

    s = robo_gfx_rgba8(255u, 0u, 0u, 0u);
    p = robo_gfx_color_premultiply(s);
    expect_color(p, 0u, 0u, 0u, 0u);

    s = robo_gfx_rgba8(255u, 128u, 64u, 128u);
    p = robo_gfx_color_premultiply(s);
    expect_color(p, 128u, 64u, 32u, 128u);
    u = robo_gfx_color_unpremultiply(p);
    expect_color(u, 255u, 128u, 64u, 128u);
}

static void test_comp_src_over_basics(void)
{
    robo_gfx_color_rgba8_t src;
    robo_gfx_color_rgba8_t dst;
    robo_gfx_color_rgba8_t out;

    dst = robo_gfx_rgba8(10u, 20u, 30u, 255u);
    src = robo_gfx_rgba8(255u, 0u, 0u, 0u);
    out = robo_gfx_composite_src_over(src, dst);
    ROBO_CHECK(color_eq(out, dst));

    src = robo_gfx_rgba8(1u, 2u, 3u, 255u);
    out = robo_gfx_composite_src_over(src, dst);
    ROBO_CHECK(color_eq(out, src));

    dst = robo_gfx_rgba8(0u, 0u, 0u, 0u);
    src = robo_gfx_rgba8(255u, 0u, 0u, 128u);
    out = robo_gfx_composite_src_over(src, dst);
    expect_color(out, 255u, 0u, 0u, 128u);

    /* 50% red over opaque white → (255,127,127,255) */
    src = robo_gfx_rgba8(255u, 0u, 0u, 128u);
    dst = robo_gfx_rgba8(255u, 255u, 255u, 255u);
    out = robo_gfx_composite_src_over(src, dst);
    expect_color(out, 255u, 127u, 127u, 255u);

    /* identical translucent colors */
    src = robo_gfx_rgba8(80u, 80u, 80u, 200u);
    dst = src;
    out = robo_gfx_composite_src_over(src, dst);
    ROBO_CHECK_EQ_INT(out.a, (int)(200u + robo_gfx_mul_div255(200u, 55u)));
}

static void test_comp_opacity(void)
{
    robo_gfx_color_rgba8_t src = robo_gfx_rgba8(255u, 0u, 0u, 255u);
    robo_gfx_color_rgba8_t dst = robo_gfx_rgba8(0u, 0u, 255u, 255u);
    robo_gfx_color_rgba8_t out;

    out = robo_gfx_composite_src_over_opacity(src, dst, 0u);
    ROBO_CHECK(color_eq(out, dst));

    out = robo_gfx_composite_src_over_opacity(src, dst, 255u);
    ROBO_CHECK(color_eq(out, src));

    /* 50% global on opaque red over blue */
    out = robo_gfx_composite_src_over_opacity(src, dst, 128u);
    expect_color(out, 128u, 0u, 127u, 255u);

    /* translucent source × global */
    src = robo_gfx_rgba8(255u, 255u, 0u, 128u);
    dst = robo_gfx_rgba8(0u, 0u, 0u, 255u);
    out = robo_gfx_composite_src_over_opacity(src, dst, 128u);
    /* sa = mul(128,128)=64 */
    ROBO_CHECK_EQ_INT(out.a, 255);
    ROBO_CHECK(out.r > 0u);
    ROBO_CHECK(out.g > 0u);
}

static void test_comp_pixel_surface(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(s, robo_gfx_rgb8(0u, 0u, 255u)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_composite_pixel(s, 1u, 1u,
                                         robo_gfx_rgba8(255u, 0u, 0u, 128u),
                                         255u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 1u, &c), ROBO_OK);
    /* 50% red over opaque blue → (128, 0, 127, 255) */
    expect_color(c, 128u, 0u, 127u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 255u, 255u);

    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_composite_pixel(s, 99u, 0u, c, 255u),
        ROBO_INVALID_ARGUMENT);
    robo_gfx_surface_release(s);
}

static void test_comp_formats(void)
{
    static const robo_gfx_pixel_format_t fmts[] = {
        ROBO_GFX_FORMAT_RGBA8, ROBO_GFX_FORMAT_BGRA8, ROBO_GFX_FORMAT_RGB8,
        ROBO_GFX_FORMAT_GRAY8, ROBO_GFX_FORMAT_ALPHA8};
    size_t i;
    robo_gfx_surface_t *dst = NULL;
    robo_gfx_surface_t *src = NULL;
    robo_gfx_color_rgba8_t c;

    for (i = 0u; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
        ROBO_CHECK_EQ_INT(make_surface(3, 3, fmts[i], &dst), ROBO_OK);
        ROBO_CHECK_EQ_INT(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, &src),
                          ROBO_OK);
        if (fmts[i] == ROBO_GFX_FORMAT_ALPHA8) {
            ROBO_CHECK_EQ_INT(
                robo_gfx_surface_clear(dst, robo_gfx_rgba8(0u, 0u, 0u, 0u)),
                ROBO_OK);
        } else {
            ROBO_CHECK_EQ_INT(
                robo_gfx_surface_clear(dst, robo_gfx_rgba8(0u, 0u, 0u, 255u)),
                ROBO_OK);
        }
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_clear(src, robo_gfx_rgba8(255u, 0u, 0u, 128u)),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_composite(src, dst, robo_gfx_point(0, 0), 255u),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 0u, 0u, &c), ROBO_OK);
        if (fmts[i] == ROBO_GFX_FORMAT_ALPHA8) {
            /* RGBA src unpacks; pack to ALPHA8 keeps result A (128 over 0). */
            ROBO_CHECK_EQ_INT(c.a, 128);
        } else if (fmts[i] == ROBO_GFX_FORMAT_GRAY8) {
            ROBO_CHECK_EQ_INT(c.a, 255);
        } else if (fmts[i] == ROBO_GFX_FORMAT_RGB8) {
            ROBO_CHECK_EQ_INT(c.a, 255);
            ROBO_CHECK(c.r > 0u);
        } else {
            /* 50% red over opaque black → (128,0,0,255) */
            ROBO_CHECK_EQ_INT(c.a, 255);
            ROBO_CHECK_EQ_INT(c.r, 128);
            ROBO_CHECK_EQ_INT(c.g, 0);
            ROBO_CHECK_EQ_INT(c.b, 0);
        }
        robo_gfx_surface_release(src);
        robo_gfx_surface_release(dst);
        src = NULL;
        dst = NULL;
    }
}

static void test_comp_bounds(void)
{
    robo_gfx_surface_t *dst = NULL;
    robo_gfx_surface_t *src = NULL;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(8, 8, ROBO_GFX_FORMAT_RGBA8, &dst), ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(dst, robo_gfx_rgb8(0u, 255u, 0u)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_clear(src, robo_gfx_rgba8(255u, 0u, 0u, 255u)),
        ROBO_OK);

    /* Fully outside */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_composite(src, dst, robo_gfx_point(100, 100), 255u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 0u, 0u, &c), ROBO_OK);
    expect_color(c, 0u, 255u, 0u, 255u);

    /* Negative origin — partial clip */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_composite(src, dst, robo_gfx_point(-2, -2), 255u),
        ROBO_OK);
    /* Source (2,2) lands at dest (0,0) */
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 0u, 0u, &c), ROBO_OK);
    expect_color(c, 255u, 0u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 7u, 7u, &c), ROBO_OK);
    expect_color(c, 0u, 255u, 0u, 255u);

    /* Zero-sized */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_composite_rect(src, robo_gfx_rect(0, 0, 0, 4), dst,
                                        robo_gfx_point(0, 0), 255u),
        ROBO_OK);

    robo_gfx_surface_release(src);
    robo_gfx_surface_release(dst);
}

static void test_comp_rect_blit(void)
{
    robo_gfx_surface_t *dst = NULL;
    robo_gfx_surface_t *src = NULL;
    robo_gfx_color_rgba8_t c;
    uint32_t x;

    ROBO_CHECK_EQ_INT(make_surface(10, 6, ROBO_GFX_FORMAT_RGBA8, &dst),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(5, 3, ROBO_GFX_FORMAT_RGBA8, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(dst, robo_gfx_rgb8(255u, 255u, 255u)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_clear(src, robo_gfx_rgba8(0u, 0u, 255u, 128u)),
        ROBO_OK);

    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_composite_rect(src, robo_gfx_rect(0, 0, 5, 1), dst,
                                        robo_gfx_point(2, 2), 255u),
        ROBO_OK);
    for (x = 2u; x < 7u; x++) {
        ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, x, 2u, &c), ROBO_OK);
        expect_color(c, 127u, 127u, 255u, 255u);
    }
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 1u, 2u, &c), ROBO_OK);
    expect_color(c, 255u, 255u, 255u, 255u);

    /* Vertical span onto transparent destination */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_clear(dst, robo_gfx_rgba8(0u, 0u, 0u, 0u)), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_composite_rect(src, robo_gfx_rect(0, 0, 1, 3), dst,
                                        robo_gfx_point(0, 0), 255u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 0u, 0u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 255u, 128u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 0u, 2u, &c), ROBO_OK);
    expect_color(c, 0u, 0u, 255u, 128u);

    robo_gfx_surface_release(src);
    robo_gfx_surface_release(dst);
}

static void test_comp_self(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(s, robo_gfx_rgb8(0u, 0u, 255u)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, robo_gfx_rect(0, 0, 2, 2),
                                   robo_gfx_rgba8(255u, 0u, 0u, 128u)),
        ROBO_OK);

    /* Overlapping self-composite: red square over itself shifted */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_composite_rect(s, robo_gfx_rect(0, 0, 2, 2), s,
                                        robo_gfx_point(1, 1), 255u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 1u, &c), ROBO_OK);
    /* Must have blended, not garbage */
    ROBO_CHECK(c.a > 0u);
    ROBO_CHECK(c.r > 0u);

    /* Non-overlapping regions on same surface */
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(s, robo_gfx_rgb8(0u, 255u, 0u)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, robo_gfx_rect(0, 0, 1, 1),
                                   robo_gfx_rgba8(255u, 0u, 0u, 255u)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_composite_rect(s, robo_gfx_rect(0, 0, 1, 1), s,
                                        robo_gfx_point(3, 3), 255u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 3u, 3u, &c), ROBO_OK);
    expect_color(c, 255u, 0u, 0u, 255u);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    expect_color(c, 255u, 0u, 0u, 255u);

    robo_gfx_surface_release(s);
}

static void test_comp_translucent_over_translucent(void)
{
    robo_gfx_color_rgba8_t src = robo_gfx_rgba8(255u, 0u, 0u, 128u);
    robo_gfx_color_rgba8_t dst = robo_gfx_rgba8(0u, 0u, 255u, 128u);
    robo_gfx_color_rgba8_t out = robo_gfx_composite_src_over(src, dst);

    /* Both a=128 → out_a = 128 + mul(128,127) = 128+64 = 192 */
    ROBO_CHECK_EQ_INT(out.a, 192);
    ROBO_CHECK(out.r > out.b);
}

static void test_comp_smoke(void)
{
    robo_gfx_surface_t *dst = NULL;
    robo_gfx_surface_t *src = NULL;
    uint32_t y;
    uint32_t x;
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(make_surface(64, 64, ROBO_GFX_FORMAT_RGBA8, &dst),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(32, 32, ROBO_GFX_FORMAT_BGRA8, &src),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(dst, robo_gfx_rgb8(255u, 255u, 255u)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_clear(src, robo_gfx_rgba8(0u, 128u, 255u, 64u)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_composite(src, dst, robo_gfx_point(16, 16), 200u),
        ROBO_OK);
    for (y = 16u; y < 48u; y += 8u) {
        for (x = 16u; x < 48u; x += 8u) {
            ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, x, y, &c),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(c.a, 255);
        }
    }
    robo_gfx_surface_release(src);
    robo_gfx_surface_release(dst);
}

static void test_comp_errors(void)
{
    robo_gfx_surface_t *s = NULL;

    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_composite_rect(NULL, robo_gfx_rect(0, 0, 1, 1), NULL,
                                        robo_gfx_point(0, 0), 255u),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_composite_rect(s, robo_gfx_rect(0, 0, -1, 1), s,
                                        robo_gfx_point(0, 0), 255u),
        ROBO_INVALID_ARGUMENT);
    robo_gfx_surface_release(s);
}

void robo_test_register_graphics_composite(void)
{
    robo_test_register("graphics.comp.mul", test_comp_mul_div255);
    robo_test_register("graphics.comp.premul", test_comp_premul_roundtrip);
    robo_test_register("graphics.comp.src_over", test_comp_src_over_basics);
    robo_test_register("graphics.comp.opacity", test_comp_opacity);
    robo_test_register("graphics.comp.pixel", test_comp_pixel_surface);
    robo_test_register("graphics.comp.formats", test_comp_formats);
    robo_test_register("graphics.comp.bounds", test_comp_bounds);
    robo_test_register("graphics.comp.rect", test_comp_rect_blit);
    robo_test_register("graphics.comp.self", test_comp_self);
    robo_test_register("graphics.comp.translucent",
                       test_comp_translucent_over_translucent);
    robo_test_register("graphics.comp.smoke", test_comp_smoke);
    robo_test_register("graphics.comp.errors", test_comp_errors);
}
