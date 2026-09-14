/*
 * RoboGraphics Goal 6.8 — path foundation (geometry / command model).
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <limits.h>
#include <stdint.h>

static void expect_cmd(const robo_gfx_path_command_t *c,
                       robo_gfx_path_verb_t verb,
                       int32_t x,
                       int32_t y)
{
    ROBO_CHECK_EQ_INT(c->verb, verb);
    if (verb == ROBO_GFX_PATH_MOVE_TO || verb == ROBO_GFX_PATH_LINE_TO) {
        ROBO_CHECK_EQ_INT(c->point.x, x);
        ROBO_CHECK_EQ_INT(c->point.y, y);
    }
}

static void test_path_empty(void)
{
    robo_gfx_path_t *path = NULL;
    robo_gfx_rect_t b;
    robo_gfx_path_iter_t it;
    robo_gfx_path_command_t cmd;

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);
    ROBO_CHECK(path != NULL);
    ROBO_CHECK(robo_gfx_path_is_empty(path));
    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(path), 0);
    ROBO_CHECK(!robo_gfx_path_has_current_point(path));
    ROBO_CHECK(!robo_gfx_path_has_open_contour(path));
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(path, &b), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_is_empty(b));

    robo_gfx_path_iter_begin(path, &it);
    ROBO_CHECK(!robo_gfx_path_iter_next(&it, &cmd));

    robo_gfx_path_release(path);
}

static void test_path_move_line_close(void)
{
    robo_gfx_path_t *path = NULL;
    robo_gfx_path_command_t c;
    robo_gfx_point_t cur;

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(1, 2)),
                      ROBO_OK);
    ROBO_CHECK(robo_gfx_path_has_open_contour(path));
    ROBO_CHECK_EQ_INT(robo_gfx_path_current_point(path, &cur), ROBO_OK);
    ROBO_CHECK_EQ_INT(cur.x, 1);
    ROBO_CHECK_EQ_INT(cur.y, 2);

    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(5, 2)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(5, 8)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_close(path), ROBO_OK);
    ROBO_CHECK(!robo_gfx_path_has_open_contour(path));
    ROBO_CHECK_EQ_INT(robo_gfx_path_current_point(path, &cur), ROBO_OK);
    ROBO_CHECK_EQ_INT(cur.x, 1);
    ROBO_CHECK_EQ_INT(cur.y, 2);

    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(path), 4);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(path, 0u, &c), ROBO_OK);
    expect_cmd(&c, ROBO_GFX_PATH_MOVE_TO, 1, 2);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(path, 1u, &c), ROBO_OK);
    expect_cmd(&c, ROBO_GFX_PATH_LINE_TO, 5, 2);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(path, 2u, &c), ROBO_OK);
    expect_cmd(&c, ROBO_GFX_PATH_LINE_TO, 5, 8);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(path, 3u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.verb, ROBO_GFX_PATH_CLOSE);

    robo_gfx_path_release(path);
}

static void test_path_iteration_order(void)
{
    robo_gfx_path_t *path = NULL;
    robo_gfx_path_iter_t it;
    robo_gfx_path_command_t c;
    int n = 0;

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(0, 0)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(10, 0)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_close(path), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(20, 20)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(30, 40)),
                      ROBO_OK);

    robo_gfx_path_iter_begin(path, &it);
    ROBO_CHECK(robo_gfx_path_iter_next(&it, &c));
    expect_cmd(&c, ROBO_GFX_PATH_MOVE_TO, 0, 0);
    n++;
    ROBO_CHECK(robo_gfx_path_iter_next(&it, &c));
    expect_cmd(&c, ROBO_GFX_PATH_LINE_TO, 10, 0);
    n++;
    ROBO_CHECK(robo_gfx_path_iter_next(&it, &c));
    ROBO_CHECK_EQ_INT(c.verb, ROBO_GFX_PATH_CLOSE);
    n++;
    ROBO_CHECK(robo_gfx_path_iter_next(&it, &c));
    expect_cmd(&c, ROBO_GFX_PATH_MOVE_TO, 20, 20);
    n++;
    ROBO_CHECK(robo_gfx_path_iter_next(&it, &c));
    expect_cmd(&c, ROBO_GFX_PATH_LINE_TO, 30, 40);
    n++;
    ROBO_CHECK(!robo_gfx_path_iter_next(&it, &c));
    ROBO_CHECK_EQ_INT(n, 5);

    robo_gfx_path_release(path);
}

static void test_path_multiple_subpaths(void)
{
    robo_gfx_path_t *path = NULL;

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);
    /* Open subpath */
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(0, 0)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(1, 0)),
                      ROBO_OK);
    /* New MoveTo without Close — open contour ends without Close verb */
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(10, 10)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(11, 10)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_close(path), ROBO_OK);

    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(path), 5);
    ROBO_CHECK(robo_gfx_path_has_current_point(path));
    ROBO_CHECK(!robo_gfx_path_has_open_contour(path));

    robo_gfx_path_release(path);
}

static void test_path_close_behavior(void)
{
    robo_gfx_path_t *path = NULL;
    robo_gfx_path_command_t c;

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);

    /* Close with no open contour: no-op */
    ROBO_CHECK_EQ_INT(robo_gfx_path_close(path), ROBO_OK);
    ROBO_CHECK(robo_gfx_path_is_empty(path));

    /* Close after MoveTo alone */
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(3, 4)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_close(path), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(path), 2);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(path, 1u, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.verb, ROBO_GFX_PATH_CLOSE);

    /* Repeated close: no-op */
    ROBO_CHECK_EQ_INT(robo_gfx_path_close(path), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(path), 2);

    /* LineTo after Close is invalid until MoveTo */
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(9, 9)),
                      ROBO_INVALID_ARGUMENT);

    /* New subpath after Close */
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(7, 8)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(9, 8)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(path), 4);

    robo_gfx_path_release(path);
}

static void test_path_line_without_move(void)
{
    robo_gfx_path_t *path = NULL;

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(1, 1)),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(robo_gfx_path_is_empty(path));
    robo_gfx_path_release(path);
}

static void test_path_bounds(void)
{
    robo_gfx_path_t *path = NULL;
    robo_gfx_rect_t b;

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);

    /* Empty */
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(path, &b), ROBO_OK);
    ROBO_CHECK(robo_gfx_rect_is_empty(b));

    /* Single point (MoveTo only) → 1×1 covering that pixel */
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(5, 7)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(path, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(b.x, 5);
    ROBO_CHECK_EQ_INT(b.y, 7);
    ROBO_CHECK_EQ_INT(b.width, 1);
    ROBO_CHECK_EQ_INT(b.height, 1);

    robo_gfx_path_reset(path);

    /* Horizontal */
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(2, 4)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(8, 4)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(path, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(b.x, 2);
    ROBO_CHECK_EQ_INT(b.y, 4);
    ROBO_CHECK_EQ_INT(b.width, 7);
    ROBO_CHECK_EQ_INT(b.height, 1);

    robo_gfx_path_reset(path);

    /* Vertical */
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(3, 1)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(3, 6)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(path, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(b.x, 3);
    ROBO_CHECK_EQ_INT(b.y, 1);
    ROBO_CHECK_EQ_INT(b.width, 1);
    ROBO_CHECK_EQ_INT(b.height, 6);

    robo_gfx_path_reset(path);

    /* Diagonal + Close (Close adds no new extrema) */
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(0, 0)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(4, 3)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_close(path), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(path, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(b.x, 0);
    ROBO_CHECK_EQ_INT(b.y, 0);
    ROBO_CHECK_EQ_INT(b.width, 5);
    ROBO_CHECK_EQ_INT(b.height, 4);

    robo_gfx_path_reset(path);

    /* Negatives + multi-subpath */
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(-10, -5)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(-2, 0)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(1, 8)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(3, 2)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(path, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(b.x, -10);
    ROBO_CHECK_EQ_INT(b.y, -5);
    ROBO_CHECK_EQ_INT(b.width, 14);  /* [-10, 3] → 14 */
    ROBO_CHECK_EQ_INT(b.height, 14); /* [-5, 8] → 14 */

    robo_gfx_path_release(path);
}

static void test_path_degenerate(void)
{
    robo_gfx_path_t *path = NULL;
    robo_gfx_rect_t b;

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);

    /* Zero-length segment */
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(4, 4)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(4, 4)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(path), 2);
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(path, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(b.width, 1);
    ROBO_CHECK_EQ_INT(b.height, 1);

    /* Repeated MoveTo */
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(4, 4)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(9, 9)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(path), 4);

    robo_gfx_path_release(path);
}

static void test_path_numeric(void)
{
    robo_gfx_path_t *path = NULL;
    robo_gfx_rect_t b;

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);

    ROBO_CHECK_EQ_INT(
        robo_gfx_path_move_to(path, robo_gfx_point(INT32_MIN, INT32_MIN)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_path_line_to(path, robo_gfx_point(INT32_MIN + 1, INT32_MIN)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(path, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(b.x, INT32_MIN);
    ROBO_CHECK_EQ_INT(b.y, INT32_MIN);
    ROBO_CHECK_EQ_INT(b.width, 2);
    ROBO_CHECK_EQ_INT(b.height, 1);

    robo_gfx_path_reset(path);
    ROBO_CHECK_EQ_INT(
        robo_gfx_path_move_to(path, robo_gfx_point(INT32_MAX, 0)), ROBO_OK);
    /* Exclusive right = INT32_MAX+1 cannot fit int32 */
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(path, &b), ROBO_INVALID_ARGUMENT);

    robo_gfx_path_reset(path);
    ROBO_CHECK_EQ_INT(
        robo_gfx_path_move_to(path, robo_gfx_point(INT32_MAX - 1, 0)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(path, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(b.x, INT32_MAX - 1);
    ROBO_CHECK_EQ_INT(b.width, 1);

    robo_gfx_path_release(path);
}

static void test_path_lifecycle(void)
{
    robo_gfx_path_t *path = NULL;
    robo_gfx_path_id_t id;

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);
    id = robo_gfx_path_id(path);
    ROBO_CHECK(id != ROBO_GFX_PATH_ID_NONE);

    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(1, 1)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(path, robo_gfx_point(2, 2)),
                      ROBO_OK);
    ROBO_CHECK(!robo_gfx_path_is_empty(path));

    robo_gfx_path_reset(path);
    ROBO_CHECK(robo_gfx_path_is_empty(path));
    ROBO_CHECK(robo_gfx_path_id(path) == id); /* id stable */

    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(0, 0)),
                      ROBO_OK);
    robo_gfx_path_clear(path);
    ROBO_CHECK(robo_gfx_path_is_empty(path));
    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(path), 0);

    /* Reuse after clear */
    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(path, robo_gfx_point(8, 9)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(path), 1);

    robo_gfx_path_retain(path);
    robo_gfx_path_release(path);
    robo_gfx_path_release(path);
}

static void test_path_errors(void)
{
    robo_gfx_path_t *path = NULL;
    robo_gfx_path_command_t c;
    robo_gfx_rect_t b;
    robo_gfx_point_t p;

    ROBO_CHECK_EQ_INT(robo_gfx_path_move_to(NULL, robo_gfx_point(0, 0)),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_path_line_to(NULL, robo_gfx_point(0, 0)),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_path_close(NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(NULL, &b), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_bounds(path, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(path, 0u, &c),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_path_current_point(path, &p),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_path_command_at(path, 0u, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_gfx_path_release(path);
    robo_gfx_path_release(NULL);
    robo_gfx_path_retain(NULL);
    robo_gfx_path_reset(NULL);
    robo_gfx_path_clear(NULL);
}

void robo_test_register_graphics_path(void)
{
    robo_test_register("graphics.path.empty", test_path_empty);
    robo_test_register("graphics.path.move_line_close",
                       test_path_move_line_close);
    robo_test_register("graphics.path.iteration", test_path_iteration_order);
    robo_test_register("graphics.path.subpaths", test_path_multiple_subpaths);
    robo_test_register("graphics.path.close", test_path_close_behavior);
    robo_test_register("graphics.path.line_no_move",
                       test_path_line_without_move);
    robo_test_register("graphics.path.bounds", test_path_bounds);
    robo_test_register("graphics.path.degenerate", test_path_degenerate);
    robo_test_register("graphics.path.numeric", test_path_numeric);
    robo_test_register("graphics.path.lifecycle", test_path_lifecycle);
    robo_test_register("graphics.path.errors", test_path_errors);
}
