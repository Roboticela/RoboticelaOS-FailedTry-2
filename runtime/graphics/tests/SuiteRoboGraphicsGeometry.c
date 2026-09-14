/*
 * RoboGraphics Goal 6.6 — pixels, points, sizes, rectangles, clipping.
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <stdint.h>

#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif
#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif

static void test_geom_point(void)
{
    robo_gfx_point_t a = robo_gfx_point(3, -4);
    robo_gfx_point_t b = robo_gfx_point(5, 6);
    robo_gfx_point_t out;

    ROBO_CHECK(robo_gfx_point_equal(a, robo_gfx_point(3, -4)));
    ROBO_CHECK(!robo_gfx_point_equal(a, b));

    ROBO_CHECK_EQ_INT(robo_gfx_point_add(a, b, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, 8);
    ROBO_CHECK_EQ_INT(out.y, 2);

    ROBO_CHECK_EQ_INT(robo_gfx_point_sub(b, a, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, 2);
    ROBO_CHECK_EQ_INT(out.y, 10);

    ROBO_CHECK_EQ_INT(robo_gfx_point_translate(a, -1, 4, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, 2);
    ROBO_CHECK_EQ_INT(out.y, 0);

    ROBO_CHECK_EQ_INT(robo_gfx_point_add(robo_gfx_point(INT32_MAX, 0),
                                         robo_gfx_point(1, 0), &out),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_point_sub(robo_gfx_point(INT32_MIN, 0),
                                         robo_gfx_point(1, 0), &out),
                      ROBO_INVALID_ARGUMENT);
}

static void test_geom_size(void)
{
    uint64_t area = 99u;
    robo_gfx_size_t s = robo_gfx_size(4, 5);

    ROBO_CHECK(robo_gfx_size_equal(s, robo_gfx_size(4, 5)));
    ROBO_CHECK(!robo_gfx_size_is_empty(s));
    ROBO_CHECK(robo_gfx_size_is_valid(s));
    ROBO_CHECK(robo_gfx_size_is_empty(robo_gfx_size(0, 5)));
    ROBO_CHECK(robo_gfx_size_is_empty(robo_gfx_size(3, -1)));
    ROBO_CHECK(!robo_gfx_size_is_valid(robo_gfx_size(-1, 2)));

    ROBO_CHECK_EQ_INT(robo_gfx_size_area(s, &area), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)area, 20);
    ROBO_CHECK_EQ_INT(robo_gfx_size_area(robo_gfx_size(0, 10), &area), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)area, 0);
    ROBO_CHECK_EQ_INT(robo_gfx_size_area(robo_gfx_size(-1, 2), &area),
                      ROBO_INVALID_ARGUMENT);
}

static void test_geom_rect_construct(void)
{
    robo_gfx_rect_t r;
    robo_gfx_rect_t e = robo_gfx_rect_empty();

    ROBO_CHECK(robo_gfx_rect_is_empty(e));
    ROBO_CHECK_EQ_INT(robo_gfx_rect_from_point_size(robo_gfx_point(-20, -10),
                                                    robo_gfx_size(100, 80), &r),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(r.x, -20);
    ROBO_CHECK_EQ_INT(r.y, -10);
    ROBO_CHECK_EQ_INT(r.width, 100);
    ROBO_CHECK_EQ_INT(r.height, 80);

    ROBO_CHECK_EQ_INT(robo_gfx_rect_from_ltrb(10, 20, 110, 70, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.x, 10);
    ROBO_CHECK_EQ_INT(r.y, 20);
    ROBO_CHECK_EQ_INT(r.width, 100);
    ROBO_CHECK_EQ_INT(r.height, 50);

    ROBO_CHECK_EQ_INT(robo_gfx_rect_from_ltrb(5, 5, 5, 10, &r), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_is_empty(r));

    ROBO_CHECK_EQ_INT(robo_gfx_rect_from_ltrb(10, 20, 5, 70, &r),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_gfx_rect_from_point_size(robo_gfx_point(0, 0),
                                      robo_gfx_size(-1, 1), &r),
        ROBO_INVALID_ARGUMENT);

    r = robo_gfx_rect(0, 0, 10, 10);
    ROBO_CHECK_EQ_INT(robo_gfx_rect_left(r), 0);
    ROBO_CHECK_EQ_INT(robo_gfx_rect_top(r), 0);
    ROBO_CHECK_EQ_INT(robo_gfx_rect_width(r), 10);
    ROBO_CHECK_EQ_INT(robo_gfx_rect_height(r), 10);
    ROBO_CHECK_EQ_INT(robo_gfx_rect_right(r), 10);
    ROBO_CHECK_EQ_INT(robo_gfx_rect_bottom(r), 10);
}

static void test_geom_contains(void)
{
    robo_gfx_rect_t r = robo_gfx_rect(10, 20, 100, 50);

    ROBO_CHECK(robo_gfx_rect_contains_point(r, robo_gfx_point(10, 20)));
    ROBO_CHECK(robo_gfx_rect_contains_point(r, robo_gfx_point(109, 69)));
    ROBO_CHECK(!robo_gfx_rect_contains_point(r, robo_gfx_point(110, 20)));
    ROBO_CHECK(!robo_gfx_rect_contains_point(r, robo_gfx_point(10, 70)));
    ROBO_CHECK(!robo_gfx_rect_contains_point(r, robo_gfx_point(9, 20)));
    ROBO_CHECK(!robo_gfx_rect_contains_point(robo_gfx_rect_empty(),
                                             robo_gfx_point(0, 0)));

    ROBO_CHECK(robo_gfx_rect_encloses(r, robo_gfx_rect(10, 20, 1, 1)));
    ROBO_CHECK(robo_gfx_rect_encloses(r, robo_gfx_rect(10, 20, 100, 50)));
    ROBO_CHECK(!robo_gfx_rect_encloses(r, robo_gfx_rect(10, 20, 101, 50)));
    ROBO_CHECK(robo_gfx_rect_encloses(r, robo_gfx_rect_empty()));
}

static void test_geom_intersect_union(void)
{
    robo_gfx_rect_t out;
    robo_gfx_rect_t a = robo_gfx_rect(0, 0, 10, 10);
    robo_gfx_rect_t b = robo_gfx_rect(5, 5, 10, 10);
    robo_gfx_rect_t c = robo_gfx_rect(20, 20, 5, 5);
    robo_gfx_rect_t d = robo_gfx_rect(10, 0, 5, 5); /* edge-touching a */
    robo_gfx_rect_t neg = robo_gfx_rect(-20, -10, 30, 25);

    ROBO_CHECK_EQ_INT(robo_gfx_rect_intersect(a, b, &out), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(out, robo_gfx_rect(5, 5, 5, 5)));

    ROBO_CHECK_EQ_INT(robo_gfx_rect_intersect(a, a, &out), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(out, a));

    ROBO_CHECK_EQ_INT(robo_gfx_rect_intersect(a, c, &out), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_is_empty(out));
    ROBO_CHECK(!robo_gfx_rect_intersects(a, c));

    ROBO_CHECK_EQ_INT(robo_gfx_rect_intersect(a, d, &out), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_is_empty(out)); /* half-open: touching ≠ overlap */

    ROBO_CHECK_EQ_INT(robo_gfx_rect_intersect(a, neg, &out), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(out, robo_gfx_rect(0, 0, 10, 10)));

    ROBO_CHECK_EQ_INT(robo_gfx_rect_bounding(a, b, &out), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(out, robo_gfx_rect(0, 0, 15, 15)));

    ROBO_CHECK_EQ_INT(robo_gfx_rect_bounding(a, robo_gfx_rect_empty(), &out),
                      ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(out, a));

    ROBO_CHECK_EQ_INT(
        robo_gfx_rect_bounding(robo_gfx_rect_empty(), robo_gfx_rect_empty(),
                               &out),
        ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_is_empty(out));

    ROBO_CHECK_EQ_INT(robo_gfx_rect_bounding(neg, a, &out), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(out, robo_gfx_rect(-20, -10, 30, 25)));
}

static void test_geom_translate_clip_inset(void)
{
    robo_gfx_rect_t out;
    robo_gfx_rect_t r = robo_gfx_rect(-20, -10, 100, 80);
    robo_gfx_rect_t bounds = robo_gfx_rect(0, 0, 50, 40);

    ROBO_CHECK_EQ_INT(robo_gfx_rect_translate(r, 5, -3, &out), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(out, robo_gfx_rect(-15, -13, 100, 80)));

    ROBO_CHECK_EQ_INT(robo_gfx_rect_translate(robo_gfx_rect(INT32_MAX - 1, 0, 1, 1),
                                              2, 0, &out),
                      ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_gfx_rect_clip(r, bounds, &out), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(out, robo_gfx_rect(0, 0, 50, 40)));

    ROBO_CHECK_EQ_INT(
        robo_gfx_rect_clip(robo_gfx_rect(5, 5, 10, 10), bounds, &out), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(out, robo_gfx_rect(5, 5, 10, 10)));

    ROBO_CHECK_EQ_INT(
        robo_gfx_rect_clip(robo_gfx_rect(100, 100, 10, 10), bounds, &out),
        ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_is_empty(out));

    ROBO_CHECK_EQ_INT(
        robo_gfx_rect_clip(robo_gfx_rect(-10, -10, 200, 200), bounds, &out),
        ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(out, bounds));

    ROBO_CHECK_EQ_INT(robo_gfx_rect_inset(robo_gfx_rect(0, 0, 10, 10), 2, &out),
                      ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(out, robo_gfx_rect(2, 2, 6, 6)));

    ROBO_CHECK_EQ_INT(robo_gfx_rect_inset(robo_gfx_rect(0, 0, 4, 4), 3, &out),
                      ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_is_empty(out));

    ROBO_CHECK_EQ_INT(robo_gfx_rect_outset(robo_gfx_rect(2, 2, 4, 4), 2, &out),
                      ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(out, robo_gfx_rect(0, 0, 8, 8)));
}

static void test_geom_overflow(void)
{
    robo_gfx_rect_t out;
    int32_t edge = 0;

    ROBO_CHECK_EQ_INT(
        robo_gfx_rect_right_checked(robo_gfx_rect(INT32_MAX - 1, 0, 2, 1), &edge),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_gfx_rect_bottom_checked(robo_gfx_rect(0, INT32_MAX - 1, 1, 2),
                                     &edge),
        ROBO_INVALID_ARGUMENT);

    /* Saturating getters never wrap. */
    ROBO_CHECK_EQ_INT(robo_gfx_rect_right(robo_gfx_rect(INT32_MAX - 1, 0, 2, 1)),
                      INT32_MAX);

    ROBO_CHECK_EQ_INT(
        robo_gfx_rect_from_ltrb(INT32_MIN, 0, INT32_MAX, 1, &out),
        ROBO_INVALID_ARGUMENT); /* width does not fit int32 */
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

static void test_geom_bitmap_integration(void)
{
    /* 4x4 labeled pattern A..P as distinct colors (r = index+1). */
    static const uint8_t expect[4][4] = {
        {1, 2, 3, 4},
        {5, 6, 7, 8},
        {9, 10, 11, 12},
        {13, 14, 15, 16}};
    robo_gfx_surface_t *s = NULL;
    robo_gfx_rect_t bounds;
    robo_gfx_rect_t clipped;
    robo_gfx_color_rgba8_t c;
    uint32_t x;
    uint32_t y;
    int idx;

    ROBO_CHECK_EQ_INT(make_surface(4, 4, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_pixel_bounds(s, &bounds), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(bounds, robo_gfx_rect(0, 0, 4, 4)));

    idx = 1;
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            ROBO_CHECK_EQ_INT(
                robo_gfx_surface_set_pixel_at(
                    s, robo_gfx_point((int32_t)x, (int32_t)y),
                    robo_gfx_rgba8((uint8_t)idx, 0, 0, 255)),
                ROBO_OK);
            idx++;
        }
    }

    /* Interior fill */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, robo_gfx_rect(1, 1, 2, 2),
                                   robo_gfx_rgba8(100, 0, 0, 255)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel_at(s, robo_gfx_point(1, 1), &c),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 100);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 1);

    /* Edge rect */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, robo_gfx_rect(0, 3, 4, 1),
                                   robo_gfx_rgba8(200, 0, 0, 255)),
        ROBO_OK);

    /* Empty no-op */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, robo_gfx_rect(0, 0, 0, 4),
                                   robo_gfx_rgba8(1, 2, 3, 4)),
        ROBO_OK);

    /* Negative geometry rejected by fill without clip */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, robo_gfx_rect(-1, 0, 2, 2),
                                   robo_gfx_rgba8(1, 1, 1, 1)),
        ROBO_INVALID_ARGUMENT);

    /* Clip negative/partial then fill */
    ROBO_CHECK_EQ_INT(
        robo_gfx_rect_clip(robo_gfx_rect(-2, -1, 4, 3), bounds, &clipped),
        ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(clipped, robo_gfx_rect(0, 0, 2, 2)));
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, clipped, robo_gfx_rgba8(50, 0, 0, 255)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 0u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 50);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, 1u, 1u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 50);

    /* Completely outside clip → empty fill ok */
    ROBO_CHECK_EQ_INT(
        robo_gfx_rect_clip(robo_gfx_rect(10, 10, 2, 2), bounds, &clipped),
        ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_is_empty(clipped));
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_fill_rect(s, clipped, robo_gfx_rgba8(9, 9, 9, 9)),
        ROBO_OK);

    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_get_pixel_at(s, robo_gfx_point(-1, 0), &c),
        ROBO_INVALID_ARGUMENT);

    (void)expect;
    robo_gfx_surface_release(s);
}

static void test_geom_pattern_copy(void)
{
    robo_gfx_surface_t *src = NULL;
    robo_gfx_surface_t *dst = NULL;
    robo_gfx_color_rgba8_t c;
    uint32_t x;
    uint32_t y;
    uint8_t v;

    ROBO_CHECK_EQ_INT(make_surface(4, 4, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(4, 4, &dst), ROBO_OK);

    v = 1u;
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            ROBO_CHECK_EQ_INT(
                robo_gfx_surface_set_pixel(src, x, y,
                                           robo_gfx_rgba8(v, 0, 0, 255)),
                ROBO_OK);
            v++;
        }
    }
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(dst, robo_gfx_rgba8(0, 0, 0, 0)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_copy_rect(src, robo_gfx_rect(1, 1, 2, 2), dst,
                                   robo_gfx_point(0, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 0u, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 6); /* B=2 → index 6 */
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dst, 1u, 1u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.r, 11);

    robo_gfx_surface_release(src);
    robo_gfx_surface_release(dst);
}

void robo_test_register_graphics_geometry(void)
{
    robo_test_register("graphics.geom.point", test_geom_point);
    robo_test_register("graphics.geom.size", test_geom_size);
    robo_test_register("graphics.geom.construct", test_geom_rect_construct);
    robo_test_register("graphics.geom.contains", test_geom_contains);
    robo_test_register("graphics.geom.intersect", test_geom_intersect_union);
    robo_test_register("graphics.geom.clip", test_geom_translate_clip_inset);
    robo_test_register("graphics.geom.overflow", test_geom_overflow);
    robo_test_register("graphics.geom.bmp", test_geom_bitmap_integration);
    robo_test_register("graphics.geom.pattern", test_geom_pattern_copy);
}
