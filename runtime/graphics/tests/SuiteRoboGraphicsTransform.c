/*
 * RoboGraphics Goal 6.9 — 2D affine transforms.
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <stdint.h>

static int nearly_eq(float a, float b, float eps)
{
    float d = a - b;
    if (d < 0.0f) {
        d = -d;
    }
    return d <= eps;
}

static void expect_point_i(robo_gfx_point_t p, int32_t x, int32_t y)
{
    ROBO_CHECK_EQ_INT(p.x, x);
    ROBO_CHECK_EQ_INT(p.y, y);
}

static void expect_point_f(robo_gfx_point_f_t p, float x, float y, float eps)
{
    ROBO_CHECK(nearly_eq(p.x, x, eps));
    ROBO_CHECK(nearly_eq(p.y, y, eps));
}

static void test_xform_identity(void)
{
    robo_gfx_affine_t id;
    robo_gfx_rect_t r, out;
    robo_gfx_path_t *path = NULL;
    robo_gfx_path_t *mapped = NULL;
    robo_gfx_path_command_t c;

    robo_gfx_affine_identity(&id);
    ROBO_CHECK(robo_gfx_affine_is_identity(&id));
    ROBO_CHECK(robo_gfx_affine_is_invertible(&id));

    expect_point_f(robo_gfx_affine_map_point_f(&id, robo_gfx_point_f(3.5f, -2.0f)),
                   3.5f, -2.0f, 0.0f);
    expect_point_i(robo_gfx_affine_map_point_i(&id, robo_gfx_point(7, 9)), 7, 9);

    r = robo_gfx_rect(2, 3, 4, 5);
    ROBO_CHECK_EQ_INT(robo_gfx_affine_map_rect(&id, r, &out), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_equal(r, out));

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(1, 2)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(3, 4)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_transformed(path, &id, &mapped), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(mapped), 2);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(mapped, 0u, &c), ROBO_OK);
    expect_point_i(c.point, 1, 2);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(path, 0u, &c), ROBO_OK);
    expect_point_i(c.point, 1, 2); /* source unchanged */

    robo_gfx_path_release(mapped);
    robo_gfx_path_release(path);
}

static void test_xform_translate(void)
{
    robo_gfx_affine_t t;
    robo_gfx_affine_t inv;
    robo_gfx_point_f_t p;

    robo_gfx_affine_translate(&t, 10.0f, -5.0f);
    p = robo_gfx_affine_map_point_f(&t, robo_gfx_point_f(1.0f, 2.0f));
    expect_point_f(p, 11.0f, -3.0f, 0.0f);

    robo_gfx_affine_translate(&t, 0.0f, 0.0f);
    ROBO_CHECK(robo_gfx_affine_is_identity(&t));

    robo_gfx_affine_translate(&t, -100.0f, 200.0f);
    ROBO_CHECK_EQ_INT(robo_gfx_affine_invert(&t, &inv), ROBO_OK);
    p = robo_gfx_affine_map_point_f(
        &inv, robo_gfx_affine_map_point_f(&t, robo_gfx_point_f(4.0f, 6.0f)));
    expect_point_f(p, 4.0f, 6.0f, 1.0e-5f);
}

static void test_xform_scale(void)
{
    robo_gfx_affine_t s;
    robo_gfx_affine_t inv;
    robo_gfx_point_f_t p;
    robo_gfx_rect_t r, out;

    robo_gfx_affine_scale(&s, 2.0f, 3.0f);
    p = robo_gfx_affine_map_point_f(&s, robo_gfx_point_f(4.0f, 5.0f));
    expect_point_f(p, 8.0f, 15.0f, 0.0f);

    robo_gfx_affine_scale(&s, 1.0f, 1.0f);
    ROBO_CHECK(robo_gfx_affine_is_identity(&s));

    /* Reflection */
    robo_gfx_affine_scale(&s, -1.0f, 1.0f);
    expect_point_i(robo_gfx_affine_map_point_i(&s, robo_gfx_point(5, 3)), -5,
                   3);
    r = robo_gfx_rect(1, 2, 4, 3); /* [1,5)×[2,5) */
    ROBO_CHECK_EQ_INT(robo_gfx_affine_map_rect(&s, r, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, -5);
    ROBO_CHECK_EQ_INT(out.y, 2);
    ROBO_CHECK_EQ_INT(out.width, 4);
    ROBO_CHECK_EQ_INT(out.height, 3);

    /* Degenerate scale — singular */
    robo_gfx_affine_scale(&s, 0.0f, 2.0f);
    ROBO_CHECK(!robo_gfx_affine_is_invertible(&s));
    ROBO_CHECK_EQ_INT(robo_gfx_affine_invert(&s, &inv), ROBO_INVALID_ARGUMENT);
    p = robo_gfx_affine_map_point_f(&s, robo_gfx_point_f(7.0f, 3.0f));
    expect_point_f(p, 0.0f, 6.0f, 0.0f);

    robo_gfx_affine_scale(&s, 2.0f, 4.0f);
    ROBO_CHECK_EQ_INT(robo_gfx_affine_invert(&s, &inv), ROBO_OK);
    p = robo_gfx_affine_map_point_f(
        &inv, robo_gfx_affine_map_point_f(&s, robo_gfx_point_f(3.0f, 8.0f)));
    expect_point_f(p, 3.0f, 8.0f, 1.0e-5f);
}

static void test_xform_rotate(void)
{
    robo_gfx_affine_t r;
    robo_gfx_affine_t inv;
    robo_gfx_point_t p;
    robo_gfx_point_f_t pf;

    robo_gfx_affine_rotate_deg(&r, 0.0f);
    ROBO_CHECK(robo_gfx_affine_is_identity(&r));

    robo_gfx_affine_rotate_deg(&r, 90.0f);
    p = robo_gfx_affine_map_point_i(&r, robo_gfx_point(10, 0));
    expect_point_i(p, 0, 10); /* (x,y)→(−y,x) */

    robo_gfx_affine_rotate_deg(&r, 180.0f);
    p = robo_gfx_affine_map_point_i(&r, robo_gfx_point(4, 5));
    expect_point_i(p, -4, -5);

    robo_gfx_affine_rotate_deg(&r, 270.0f);
    p = robo_gfx_affine_map_point_i(&r, robo_gfx_point(10, 0));
    expect_point_i(p, 0, -10); /* same as −90°: (x,y)→(y,−x) */

    robo_gfx_affine_rotate_deg(&r, -90.0f);
    p = robo_gfx_affine_map_point_i(&r, robo_gfx_point(10, 0));
    expect_point_i(p, 0, -10);

    robo_gfx_affine_rotate_deg(&r, 360.0f);
    ROBO_CHECK(robo_gfx_affine_is_identity(&r));

    robo_gfx_affine_rotate_deg(&r, 45.0f);
    ROBO_CHECK_EQ_INT(robo_gfx_affine_invert(&r, &inv), ROBO_OK);
    pf = robo_gfx_affine_map_point_f(
        &inv, robo_gfx_affine_map_point_f(&r, robo_gfx_point_f(2.0f, 3.0f)));
    expect_point_f(pf, 2.0f, 3.0f, 1.0e-4f);
}

static void test_xform_shear(void)
{
    robo_gfx_affine_t s;
    robo_gfx_affine_t inv;
    robo_gfx_point_f_t p;

    robo_gfx_affine_shear(&s, 2.0f, 0.0f); /* horizontal */
    p = robo_gfx_affine_map_point_f(&s, robo_gfx_point_f(1.0f, 3.0f));
    expect_point_f(p, 7.0f, 3.0f, 0.0f); /* x + 2*y */

    robo_gfx_affine_shear(&s, 0.0f, -1.5f); /* vertical */
    p = robo_gfx_affine_map_point_f(&s, robo_gfx_point_f(2.0f, 4.0f));
    expect_point_f(p, 2.0f, 1.0f, 1.0e-5f); /* y - 1.5*x */

    robo_gfx_affine_shear(&s, 0.0f, 0.0f);
    ROBO_CHECK(robo_gfx_affine_is_identity(&s));

    robo_gfx_affine_shear(&s, 1.0f, 0.5f);
    ROBO_CHECK_EQ_INT(robo_gfx_affine_invert(&s, &inv), ROBO_OK);
    p = robo_gfx_affine_map_point_f(
        &inv, robo_gfx_affine_map_point_f(&s, robo_gfx_point_f(3.0f, 5.0f)));
    expect_point_f(p, 3.0f, 5.0f, 1.0e-4f);
}

static void test_xform_compose_order(void)
{
    robo_gfx_affine_t T, S, TS, ST;
    robo_gfx_point_f_t p;
    robo_gfx_affine_t dst;

    /*
     * Existing Geometry smoke: multiply(translate, scale) = T*S
     * applies scale then translate.
     */
    robo_gfx_affine_translate(&T, 10.0f, 20.0f);
    robo_gfx_affine_scale(&S, 2.0f, 3.0f);
    ROBO_CHECK_EQ_INT(robo_gfx_affine_compose(&T, &S, &TS), ROBO_OK);
    p = robo_gfx_affine_map_point_f(&TS, robo_gfx_point_f(1.0f, 1.0f));
    expect_point_f(p, 12.0f, 23.0f, 1.0e-5f);

    ROBO_CHECK_EQ_INT(robo_gfx_affine_compose(&S, &T, &ST), ROBO_OK);
    p = robo_gfx_affine_map_point_f(&ST, robo_gfx_point_f(1.0f, 1.0f));
    /* scale(translate(1,1)) = scale(11,21) = (22, 63) */
    expect_point_f(p, 22.0f, 63.0f, 1.0e-5f);

    dst = T;
    ROBO_CHECK_EQ_INT(robo_gfx_affine_pre_concat(&dst, &S), ROBO_OK);
    ROBO_CHECK(robo_gfx_affine_equal(&dst, &TS));

    dst = T;
    ROBO_CHECK_EQ_INT(robo_gfx_affine_post_concat(&dst, &S), ROBO_OK);
    ROBO_CHECK(robo_gfx_affine_equal(&dst, &ST));
}

static void test_xform_compose_mixed(void)
{
    robo_gfx_affine_t R, T, RT;
    robo_gfx_point_t p;

    robo_gfx_affine_rotate_deg(&R, 90.0f);
    robo_gfx_affine_translate(&T, 5.0f, 0.0f);
    /* RT = R*T: translate then rotate */
    ROBO_CHECK_EQ_INT(robo_gfx_affine_compose(&R, &T, &RT), ROBO_OK);
    p = robo_gfx_affine_map_point_i(&RT, robo_gfx_point(1, 0));
    /* translate → (6,0); rotate 90 → (0,6) */
    expect_point_i(p, 0, 6);
}

static void test_xform_rect(void)
{
    robo_gfx_affine_t m;
    robo_gfx_rect_t r, out;

    r = robo_gfx_rect(0, 0, 10, 4);

    robo_gfx_affine_translate(&m, 3.0f, 5.0f);
    ROBO_CHECK_EQ_INT(robo_gfx_affine_map_rect(&m, r, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, 3);
    ROBO_CHECK_EQ_INT(out.y, 5);
    ROBO_CHECK_EQ_INT(out.width, 10);
    ROBO_CHECK_EQ_INT(out.height, 4);

    robo_gfx_affine_scale(&m, 2.0f, 2.0f);
    ROBO_CHECK_EQ_INT(robo_gfx_affine_map_rect(&m, r, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, 0);
    ROBO_CHECK_EQ_INT(out.y, 0);
    ROBO_CHECK_EQ_INT(out.width, 20);
    ROBO_CHECK_EQ_INT(out.height, 8);

    /* 90° rotation of [0,10)×[0,4) corners → AABB */
    robo_gfx_affine_rotate_deg(&m, 90.0f);
    ROBO_CHECK_EQ_INT(robo_gfx_affine_map_rect(&m, r, &out), ROBO_OK);
    /* corners (0,0)(10,0)(0,4)(10,4) → (0,0)(0,10)(-4,0)(-4,10) */
    ROBO_CHECK_EQ_INT(out.x, -4);
    ROBO_CHECK_EQ_INT(out.y, 0);
    ROBO_CHECK_EQ_INT(out.width, 4);
    ROBO_CHECK_EQ_INT(out.height, 10);

    robo_gfx_affine_shear(&m, 1.0f, 0.0f);
    ROBO_CHECK_EQ_INT(robo_gfx_affine_map_rect(&m, r, &out), ROBO_OK);
    /* (0,0)(10,0)(0,4)(10,4) → (0,0)(10,0)(4,4)(14,4) → [0,14)×[0,4) */
    ROBO_CHECK_EQ_INT(out.x, 0);
    ROBO_CHECK_EQ_INT(out.y, 0);
    ROBO_CHECK_EQ_INT(out.width, 14);
    ROBO_CHECK_EQ_INT(out.height, 4);

    /* Empty */
    ROBO_CHECK_EQ_INT(
        robo_gfx_affine_map_rect(&m, robo_gfx_rect_empty(), &out), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_is_empty(out));
}

static void test_xform_path(void)
{
    robo_gfx_affine_t m;
    robo_gfx_path_t *path = NULL;
    robo_gfx_path_t *out = NULL;
    robo_gfx_path_command_t c;
    robo_gfx_path_id_t id;

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);
    id = robo_gfx_path_id(path);
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(0, 0)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(10, 0)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_close(path), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(1, 1)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(2, 3)),
                      ROBO_OK);

    robo_gfx_affine_translate(&m, 5.0f, 7.0f);
    ROBO_CHECK_EQ_INT(robo_gfx_path_transformed(path, &m, &out), ROBO_OK);

    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(out), 5);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(out, 0u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.verb, ROBO_GFX_PATH_MOVE_TO);
    expect_point_i(c.point, 5, 7);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(out, 1u, &c), ROBO_OK);
    expect_point_i(c.point, 15, 7);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(out, 2u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.verb, ROBO_GFX_PATH_CLOSE);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(out, 3u, &c), ROBO_OK);
    expect_point_i(c.point, 6, 8);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(out, 4u, &c), ROBO_OK);
    expect_point_i(c.point, 7, 10);

    /* Source unchanged */
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(path, 0u, &c), ROBO_OK);
    expect_point_i(c.point, 0, 0);
    ROBO_CHECK(robo_gfx_path_id(path) == id);

    /* In-place */
    ROBO_CHECK_EQ_INT(robo_gfx_path_transform(path, &m), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(path, 0u, &c), ROBO_OK);
    expect_point_i(c.point, 5, 7);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(path, 2u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.verb, ROBO_GFX_PATH_CLOSE);

    robo_gfx_path_release(out);
    robo_gfx_path_release(path);
}

static void test_xform_inverse_singular(void)
{
    robo_gfx_affine_t m;
    robo_gfx_affine_t inv;
    robo_gfx_affine_t id;

    robo_gfx_affine_set(&m, 1.0f, 2.0f, 3.0f, 2.0f, 4.0f, 5.0f); /* det=0 */
    ROBO_CHECK(!robo_gfx_affine_is_invertible(&m));
    ROBO_CHECK_EQ_INT(robo_gfx_affine_invert(&m, &inv), ROBO_INVALID_ARGUMENT);

    robo_gfx_affine_identity(&id);
    ROBO_CHECK_EQ_INT(robo_gfx_affine_invert(&id, &inv), ROBO_OK);
    ROBO_CHECK(robo_gfx_affine_is_identity(&inv));
}

static void test_xform_numeric(void)
{
    robo_gfx_affine_t m;
    robo_gfx_point_f_t p;
    robo_gfx_rect_t out;

    robo_gfx_affine_translate(&m, -1.0e6f, 1.0e6f);
    p = robo_gfx_affine_map_point_f(&m, robo_gfx_point_f(1.0e6f, -1.0e6f));
    expect_point_f(p, 0.0f, 0.0f, 1.0f);

    robo_gfx_affine_scale(&m, -2.0f, -3.0f);
    p = robo_gfx_affine_map_point_f(&m, robo_gfx_point_f(-4.0f, 5.0f));
    expect_point_f(p, 8.0f, -15.0f, 0.0f);

    ROBO_CHECK_EQ_INT(robo_gfx_affine_map_rect(&m, robo_gfx_rect(1, 1, 0, 5),
                                               &out),
                      ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_is_empty(out));

    ROBO_CHECK_EQ_INT(robo_gfx_affine_invert(NULL, &m), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_path_transformed(NULL, &m, NULL),
                      ROBO_INVALID_ARGUMENT);
}

void robo_test_register_graphics_transform(void)
{
    robo_test_register("graphics.xform.identity", test_xform_identity);
    robo_test_register("graphics.xform.translate", test_xform_translate);
    robo_test_register("graphics.xform.scale", test_xform_scale);
    robo_test_register("graphics.xform.rotate", test_xform_rotate);
    robo_test_register("graphics.xform.shear", test_xform_shear);
    robo_test_register("graphics.xform.compose", test_xform_compose_order);
    robo_test_register("graphics.xform.compose_mixed",
                       test_xform_compose_mixed);
    robo_test_register("graphics.xform.rect", test_xform_rect);
    robo_test_register("graphics.xform.path", test_xform_path);
    robo_test_register("graphics.xform.inverse", test_xform_inverse_singular);
    robo_test_register("graphics.xform.numeric", test_xform_numeric);
}
