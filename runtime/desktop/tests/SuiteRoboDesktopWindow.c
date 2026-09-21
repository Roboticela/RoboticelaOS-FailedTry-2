/*
 * RoboDesktop shell window suite (Phase 11.13) — `desktop.window.*`.
 *
 * What a window *is*, without a frame: where it lands, how it is clamped, what its client rectangle is,
 * its title and contents, its z-order and focus, its close, and what a point resolves to. The frame suite
 * beside it is what a window *looks like*; this one is the model.
 *
 * Three of these cases are the phase's §11 read as assertions rather than as prose:
 *
 *   a caller that does not know the work area can ask for a window and get a placed one
 *   (`desktop.window.add.defaults`, which pins the placement arithmetic end to end);
 *   a window smaller than its own decorations is **grown** rather than refused
 *   (`desktop.window.add.minimum`), because the minimum is a property of the decorations the shell
 *   draws, not of the caller's request;
 *   and closing is a **request**: the window stops painting, stays in the list, and the caller destroys
 *   it (`desktop.window.close`), which is what lets an application decide what a close means.
 */

#include "RoboDesktopTestSupport.h"

static size_t visible_windows(const robo_desktop_t *desktop)
{
    size_t count = robo_desktop_window_count(desktop);
    size_t index;
    size_t visible = 0u;

    for (index = 0u; index < count; index++) {
        robo_desktop_window_id_t id = ROBO_DESKTOP_WINDOW_ID_NONE;

        if (robo_desktop_window_at(desktop, index, &id) == ROBO_OK && robo_desktop_window_is_visible(desktop, id)) {
            visible++;
        }
    }
    return visible;
}

static void assert_bounds(robo_desktop_t *desktop, robo_desktop_window_id_t id, int32_t x, int32_t y,
                          int32_t w, int32_t h, const char *what)
{
    robo_gfx_rect_t bounds;

    ROBO_CHECK_EQ_INT(robo_desktop_window_bounds(desktop, id, &bounds), ROBO_OK);
    if (bounds.x != x || bounds.y != y || bounds.width != w || bounds.height != h) {
        robo_test_fail(__FILE__, __LINE__, "%s: expected %d,%d %dx%d, got %d,%d %dx%d", what, (int)x,
                       (int)y, (int)w, (int)h, (int)bounds.x, (int)bounds.y, (int)bounds.width,
                       (int)bounds.height);
    }
}

/* ------------------------------------------------------------------ */
/* 1. Creation and placement                                           */
/* ------------------------------------------------------------------ */

static void test_window_add_defaults(void)
{
    robo_desktop_t *desktop = desktop_open(1280, 800);
    robo_desktop_window_desc_t desc;
    robo_desktop_window_id_t id = ROBO_DESKTOP_WINDOW_ID_NONE;
    robo_gfx_rect_t client;
    robo_desktop_stats_t stats;

    robo_desktop_window_desc_init(&desc);
    ROBO_CHECK(desc.x < 0 && desc.y < 0);
    ROBO_CHECK(desc.visible == 1);
    ROBO_CHECK(desc.title == NULL && desc.content == NULL && desc.content_face == NULL);
    robo_desktop_window_desc_init(NULL);
    ROBO_CHECK_EQ_INT(robo_desktop_window_desc_validate(&desc), ROBO_OK);
    desc.width = -1;
    ROBO_CHECK_EQ_INT(robo_desktop_window_desc_validate(&desc), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_window_desc_validate(NULL), ROBO_INVALID_ARGUMENT);
    desc.width = 560;

    ROBO_CHECK_EQ_INT(robo_desktop_window_add(desktop, &desc, &id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_DESKTOP_WINDOW_ID_NONE);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_count(desktop), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_window_exists(desktop, id), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_window_exists(desktop, 99u), 0);
    ROBO_CHECK_EQ_INT(robo_desktop_window_exists(NULL, id), 0);

    /* "Place me" is the deterministic centred-nudged-up placement, then the work-area clamp. */
    assert_bounds(desktop, id, 360, 166, 560, 360, "placed window");

    /* The client rectangle is the decorations subtracted, which is the door a document is built for. */
    ROBO_CHECK_EQ_INT(robo_desktop_window_client_bounds(desktop, id, &client), ROBO_OK);
    ROBO_CHECK_EQ_INT(client.x, 361);
    ROBO_CHECK_EQ_INT(client.y, 166 + 1 + 24);
    ROBO_CHECK_EQ_INT(client.width, 558);
    ROBO_CHECK_EQ_INT(client.height, 360 - 2 - 24);

    /* A new window is focused and front: on a desktop with no window manager that is the only defensible
     * default. */
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_focused(desktop), (int)id);
    ROBO_CHECK_EQ_INT(robo_desktop_window_is_visible(desktop, id), 1);
    ROBO_CHECK_EQ_INT(visible_windows(desktop), 1u);

    ROBO_CHECK_EQ_INT(robo_desktop_stats(desktop, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)stats.windows_added, 1);

    ROBO_CHECK_EQ_INT(robo_desktop_window_add(desktop, NULL, &id, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_window_add(desktop, &desc, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_window_add(NULL, &desc, &id, NULL), ROBO_INVALID_ARGUMENT);

    desktop_close(desktop);
}

static void test_window_add_minimum_and_bounds(void)
{
    robo_desktop_window_metrics_t metrics = desktop_window_metrics();
    robo_desktop_t *desktop = desktop_open(1280, 800);
    robo_desktop_window_id_t id;
    robo_gfx_rect_t bounds;

    desktop_fixture();
    /* A four-by-four window is asking for something the shell cannot draw: the honest answer is the
     * smallest window that can be drawn, not an error and not a frame whose title bar is outside itself. */
    id = desktop_add_window(desktop, "Tiny", 100, 100, 4, 4);
    assert_bounds(desktop, id, 100, 100, metrics.min_width, metrics.min_height, "grown to the minimum");

    /* A window larger than the work area is shrunk to it, and one past its edges is moved inside. */
    ROBO_CHECK_EQ_INT(robo_desktop_window_set_bounds(desktop, id, robo_gfx_rect(10, 40, 4000, 4000),
                                                     NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_window_bounds(desktop, id, &bounds), ROBO_OK);
    ROBO_CHECK(bounds.width <= 1280 && bounds.height <= 770);
    ROBO_CHECK(bounds.x >= 0 && bounds.y >= 30);

    ROBO_CHECK_EQ_INT(robo_desktop_window_set_bounds(desktop, id, robo_gfx_rect(1270, 780, 300, 200),
                                                     NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_window_bounds(desktop, id, &bounds), ROBO_OK);
    ROBO_CHECK(bounds.x + bounds.width <= 1280);
    ROBO_CHECK(bounds.y + bounds.height <= 800);

    ROBO_CHECK_EQ_INT(robo_desktop_window_set_bounds(desktop, id, robo_gfx_rect(0, 0, -5, 10), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_window_set_bounds(desktop, 4242u, robo_gfx_rect(0, 40, 10, 10), NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_desktop_window_bounds(desktop, 4242u, &bounds), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_desktop_window_client_bounds(desktop, 4242u, &bounds), ROBO_NOT_FOUND);

    desktop_close(desktop);
}

/*
 * A work area too thin to place a window in is refused rather than filled with a window the shell cannot
 * draw, and `ROBO_DESKTOP_MIN_DIMENSION` is the threshold -- tested at the threshold from both sides,
 * because a rule asserted only on its failing side is a rule that could be a different rule.
 */
static void test_window_no_work_area(void)
{
    robo_desktop_window_desc_t desc;
    robo_desktop_diag_t diag;
    robo_desktop_window_id_t id = ROBO_DESKTOP_WINDOW_ID_NONE;
    robo_desktop_t *thin;
    robo_desktop_t *exact;

    desktop_fixture();
    robo_desktop_window_desc_init(&desc);
    robo_desktop_diag_reset(&diag);

    /* 64x40 leaves a 64x10 work area: ten pixels is less than the smallest a window may be placed in,
     * so the shell refuses and says which object refused -- `SHELL`, because the *screen* is too small
     * rather than the window being wrong. */
    thin = desktop_open(64, 40);
    ROBO_CHECK_EQ_INT(robo_desktop_window_add(thin, &desc, &id, &diag), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_DESKTOP_DOMAIN_SHELL);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_count(thin), 0);
    ROBO_CHECK_EQ_INT((int)id, (int)ROBO_DESKTOP_WINDOW_ID_NONE);
    desktop_close(thin);

    /* 64x46 leaves exactly 64x16: the threshold itself is placeable, so the rule is a bound and not an
     * approximation. The window is grown to its decoration minimum and clamped into the work area. */
    exact = desktop_open(64, 46);
    ROBO_CHECK_EQ_INT(robo_desktop_window_add(exact, &desc, &id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_DESKTOP_WINDOW_ID_NONE);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_count(exact), 1);
    desktop_close(exact);
}

static void test_window_capacity(void)
{
    robo_desktop_t *desktop = desktop_open(800, 600);
    robo_desktop_window_desc_t desc;
    size_t index;

    robo_desktop_window_desc_init(&desc);
    for (index = 0u; index < ROBO_DESKTOP_MAX_WINDOWS; index++) {
        robo_desktop_window_id_t id = ROBO_DESKTOP_WINDOW_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_desktop_window_add(desktop, &desc, &id, NULL), ROBO_OK);
        ROBO_CHECK(id != ROBO_DESKTOP_WINDOW_ID_NONE);
    }
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_count(desktop), (int)ROBO_DESKTOP_MAX_WINDOWS);
    ROBO_CHECK_EQ_INT(robo_desktop_window_add(desktop, &desc, &(robo_desktop_window_id_t){ 0 }, NULL),
                      ROBO_ALREADY_EXISTS);
    /* Every id is distinct: a wrap that handed out the "none" token would be a window nobody could name. */
    for (index = 0u; index < ROBO_DESKTOP_MAX_WINDOWS; index++) {
        robo_desktop_window_id_t id = ROBO_DESKTOP_WINDOW_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_desktop_window_at(desktop, index, &id), ROBO_OK);
        ROBO_CHECK(id != ROBO_DESKTOP_WINDOW_ID_NONE);
    }
    ROBO_CHECK_EQ_INT(robo_desktop_window_at(desktop, ROBO_DESKTOP_MAX_WINDOWS,
                                            &(robo_desktop_window_id_t){ 0 }),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_desktop_window_at(desktop, 0u, NULL), ROBO_INVALID_ARGUMENT);

    desktop_close(desktop);
}

/* ------------------------------------------------------------------ */
/* 2. Title, contents, visibility                                      */
/* ------------------------------------------------------------------ */

static void test_window_title_and_content(void)
{
    robo_desktop_t *desktop = desktop_open(800, 600);
    robo_desktop_window_id_t id = desktop_add_window(desktop, "Hello", 40, 40, 400, 300);
    char too_long[ROBO_DESKTOP_TITLE_MAX + 2u];
    size_t index;

    ROBO_CHECK(strcmp(robo_desktop_window_title(desktop, id), "Hello") == 0);
    ROBO_CHECK(strcmp(robo_desktop_window_title(desktop, 4242u), "") == 0);
    ROBO_CHECK(strcmp(robo_desktop_window_title(NULL, id), "") == 0);

    ROBO_CHECK_EQ_INT(robo_desktop_window_set_title(desktop, id, "Roboticela Demo", NULL), ROBO_OK);
    ROBO_CHECK(strcmp(robo_desktop_window_title(desktop, id), "Roboticela Demo") == 0);
    ROBO_CHECK_EQ_INT(robo_desktop_window_set_title(desktop, id, NULL, NULL), ROBO_OK);
    ROBO_CHECK(strcmp(robo_desktop_window_title(desktop, id), "") == 0);

    for (index = 0u; index < sizeof(too_long) - 1u; index++) {
        too_long[index] = 'x';
    }
    too_long[sizeof(too_long) - 1u] = '\0';
    ROBO_CHECK_EQ_INT(robo_desktop_window_set_title(desktop, id, "Kept", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_window_set_title(desktop, id, too_long, NULL), ROBO_INVALID_ARGUMENT);
    /* Nothing was copied on the refusal, so the caller still holds its old title. */
    ROBO_CHECK(strcmp(robo_desktop_window_title(desktop, id), "Kept") == 0);

    /* Contents are borrowed: this desktop has no document, and a window with no contents is a window. */
    ROBO_CHECK(robo_desktop_window_content(desktop, id) == NULL);
    ROBO_CHECK(robo_desktop_window_content(desktop, 4242u) == NULL);
    ROBO_CHECK_EQ_INT(robo_desktop_window_set_content(desktop, 4242u, NULL, NULL, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_desktop_window_set_content(NULL, id, NULL, NULL, NULL), ROBO_INVALID_ARGUMENT);

    /* Visibility: a hidden window keeps its bounds, contents and z-order and is not painted. */
    ROBO_CHECK_EQ_INT(robo_desktop_window_set_visible(desktop, id, 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_window_is_visible(desktop, id), 0);
    ROBO_CHECK_EQ_INT(visible_windows(desktop), 0u);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_count(desktop), 1);
    assert_bounds(desktop, id, 40, 40, 400, 300, "hidden window keeps its bounds");
    ROBO_CHECK_EQ_INT(robo_desktop_window_set_visible(desktop, id, 1, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_window_is_visible(desktop, id), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_window_set_visible(desktop, 4242u, 1, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_desktop_window_is_visible(desktop, 4242u), 0);

    desktop_close(desktop);
}

/* ------------------------------------------------------------------ */
/* 3. Z-order and focus                                                */
/* ------------------------------------------------------------------ */

static void test_window_raise_and_focus(void)
{
    robo_desktop_t *desktop = desktop_open(1000, 700);
    robo_desktop_window_id_t first = desktop_add_window(desktop, "First", 60, 60, 300, 200);
    robo_desktop_window_id_t second = desktop_add_window(desktop, "Second", 80, 80, 300, 200);
    robo_desktop_window_id_t front = ROBO_DESKTOP_WINDOW_ID_NONE;

    ROBO_CHECK_EQ_INT((int)robo_desktop_window_focused(desktop), (int)second);
    ROBO_CHECK_EQ_INT(robo_desktop_window_at(desktop, 0u, &front), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)front, (int)first);
    ROBO_CHECK_EQ_INT(robo_desktop_window_at(desktop, 1u, &front), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)front, (int)second);

    /* The last window in the list is the front one, which is the whole of this build's z-order. */
    ROBO_CHECK_EQ_INT(robo_desktop_window_raise(desktop, first), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_window_at(desktop, 1u, &front), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)front, (int)first);
    /* Focus follows the raise, so the two cannot drift. */
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_focused(desktop), (int)first);

    /* Focusing without raising: the accent border can be moved without reordering. */
    ROBO_CHECK_EQ_INT(robo_desktop_window_focus(desktop, second), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_focused(desktop), (int)second);
    ROBO_CHECK_EQ_INT(robo_desktop_window_at(desktop, 1u, &front), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)front, (int)first);

    ROBO_CHECK_EQ_INT(robo_desktop_window_raise(desktop, 4242u), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_desktop_window_focus(desktop, 4242u), ROBO_NOT_FOUND);
    /* A NULL desktop is a caller mistake rather than a missing window, and the two answers differ. */
    ROBO_CHECK_EQ_INT(robo_desktop_window_raise(NULL, first), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_focused(NULL), (int)ROBO_DESKTOP_WINDOW_ID_NONE);

    desktop_close(desktop);
}

static void test_window_close_and_destroy(void)
{
    robo_desktop_t *desktop = desktop_open(1000, 700);
    robo_desktop_window_id_t first = desktop_add_window(desktop, "First", 60, 60, 300, 200);
    robo_desktop_window_id_t second = desktop_add_window(desktop, "Second", 80, 80, 300, 200);
    robo_desktop_window_state_t state = ROBO_DESKTOP_WINDOW_OPEN;
    robo_desktop_stats_t stats;

    ROBO_CHECK_EQ_INT(robo_desktop_window_state(desktop, first, &state), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)state, (int)ROBO_DESKTOP_WINDOW_OPEN);

    /* Close is a **request**: the window stops being painted and stays in the list for its owner. */
    ROBO_CHECK_EQ_INT(robo_desktop_window_request_close(desktop, second), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_window_state(desktop, second, &state), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)state, (int)ROBO_DESKTOP_WINDOW_CLOSING);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_count(desktop), 2);
    ROBO_CHECK_EQ_INT(visible_windows(desktop), 1u);
    /* Idempotent, like the request it is. */
    ROBO_CHECK_EQ_INT(robo_desktop_window_request_close(desktop, second), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_window_request_close(desktop, 4242u), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_desktop_window_state(desktop, 4242u, &state), ROBO_NOT_FOUND);

    /* Destroying the focused window moves focus to the front one that is left. */
    ROBO_CHECK_EQ_INT(robo_desktop_window_destroy(desktop, second), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_window_destroy(desktop, second), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_count(desktop), 1);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_focused(desktop), (int)first);
    ROBO_CHECK_EQ_INT(robo_desktop_window_exists(desktop, second), 0);
    ROBO_CHECK_EQ_INT(robo_desktop_window_destroy(NULL, first), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_desktop_stats(desktop, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)stats.windows_added, 2);
    ROBO_CHECK_EQ_INT((int)stats.windows_closed, 1);
    ROBO_CHECK_EQ_INT((int)stats.windows_destroyed, 1);

    /* Destroying the last window leaves a desktop with no focused window rather than a stale index. */
    ROBO_CHECK_EQ_INT(robo_desktop_window_destroy(desktop, first), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_focused(desktop), (int)ROBO_DESKTOP_WINDOW_ID_NONE);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_count(desktop), 0);

    desktop_close(desktop);
}

/* ------------------------------------------------------------------ */
/* 4. Hit testing and the pointer                                      */
/* ------------------------------------------------------------------ */

static void test_window_hit_test(void)
{
    robo_desktop_t *desktop = desktop_open(1280, 800);
    robo_desktop_window_id_t id = desktop_add_window(desktop, "Target", 100, 100, 400, 300);
    robo_desktop_geometry_t geometry = robo_desktop_geometry_of(desktop);
    robo_desktop_hit_t hit;

    robo_desktop_hit_init(&hit);
    ROBO_CHECK_EQ_INT((int)hit.area, (int)ROBO_DESKTOP_AREA_NONE);
    ROBO_CHECK_EQ_INT(hit.inside, 0);
    robo_desktop_hit_init(NULL);

    /* The title bar: `bounds.y + border` through the title's height, minus the close button. */
    ROBO_CHECK_EQ_INT(robo_desktop_hit_test(desktop, robo_gfx_point(160, 104), &hit), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)hit.area, (int)ROBO_DESKTOP_AREA_WINDOW_TITLE);
    ROBO_CHECK_EQ_INT((int)hit.window, (int)id);
    ROBO_CHECK_EQ_INT(hit.inside, 1);

    /* The close button: ten pixels square, six in from the frame's right edge, centred in the title bar. */
    ROBO_CHECK_EQ_INT(robo_desktop_hit_test(desktop, robo_gfx_point(400 + 100 - 12, 100 + 8 + 5), &hit),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)hit.area, (int)ROBO_DESKTOP_AREA_WINDOW_CLOSE);
    ROBO_CHECK_EQ_INT((int)hit.window, (int)id);

    /* The client area. */
    ROBO_CHECK_EQ_INT(robo_desktop_hit_test(desktop, robo_gfx_point(300, 300), &hit), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)hit.area, (int)ROBO_DESKTOP_AREA_WINDOW_CONTENT);

    /* The panel, then the desktop's own surface, then outside the screen entirely -- which is not a
     * failure, because a pointer can leave a window. */
    ROBO_CHECK_EQ_INT(robo_desktop_hit_test(desktop, robo_gfx_point(10, 10), &hit), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)hit.area, (int)ROBO_DESKTOP_AREA_PANEL);
    ROBO_CHECK_EQ_INT((int)hit.window, (int)ROBO_DESKTOP_WINDOW_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_desktop_hit_test(desktop, robo_gfx_point(geometry.work_area.width - 1,
                                                                   geometry.work_area.height + 20), &hit),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)hit.area, (int)ROBO_DESKTOP_AREA_BACKGROUND);
    ROBO_CHECK_EQ_INT(hit.inside, 1);
    ROBO_CHECK_EQ_INT(robo_desktop_hit_test(desktop, robo_gfx_point(-5, -5), &hit), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)hit.area, (int)ROBO_DESKTOP_AREA_NONE);
    ROBO_CHECK_EQ_INT(hit.inside, 0);

    /* Front to back: a point over two windows belongs to the front one, which is the answer a user would
     * give. */
    {
        robo_desktop_window_id_t front = desktop_add_window(desktop, "Front", 100, 100, 400, 300);

        ROBO_CHECK_EQ_INT(robo_desktop_hit_test(desktop, robo_gfx_point(300, 300), &hit), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)hit.window, (int)front);
    }

    ROBO_CHECK_EQ_INT(robo_desktop_hit_test(desktop, robo_gfx_point(0, 0), NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_hit_test(NULL, robo_gfx_point(0, 0), &hit), ROBO_INVALID_ARGUMENT);

    desktop_close(desktop);
}

static void test_window_pointer_actions(void)
{
    robo_desktop_t *desktop = desktop_open(1280, 800);
    robo_desktop_window_id_t first = desktop_add_window(desktop, "First", 100, 100, 400, 300);
    robo_desktop_window_id_t second = desktop_add_window(desktop, "Second", 150, 150, 400, 300);
    robo_desktop_pointer_t pointer;
    robo_desktop_stats_t stats;

    /* A move records where the pointer is and what is under it. */
    ROBO_CHECK_EQ_INT(robo_desktop_observe_pointer(desktop, robo_gfx_point(300, 300), 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_pointer(desktop, &pointer), ROBO_OK);
    ROBO_CHECK_EQ_INT(pointer.point.x, 300);
    ROBO_CHECK_EQ_INT(pointer.point.y, 300);
    ROBO_CHECK_EQ_INT(pointer.inside, 1);
    ROBO_CHECK_EQ_INT(pointer.pressed, 0);
    ROBO_CHECK_EQ_INT((int)pointer.area, (int)ROBO_DESKTOP_AREA_WINDOW_CONTENT);
    ROBO_CHECK_EQ_INT((int)pointer.window, (int)second);

    /* A press on a window's title bar raises it -- read at a point in `first`'s title band that `second`
     * does not cover, which is what makes the assertion about the press rather than about z-order. Its
     * title band is rows 101..124; `second` starts at (150, 150). */
    ROBO_CHECK_EQ_INT(robo_desktop_observe_pointer(desktop, robo_gfx_point(200, 110), 1, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_focused(desktop), (int)first);
    ROBO_CHECK_EQ_INT(robo_desktop_pointer(desktop, &pointer), ROBO_OK);
    ROBO_CHECK_EQ_INT(pointer.pressed, 1);
    ROBO_CHECK_EQ_INT((int)pointer.area, (int)ROBO_DESKTOP_AREA_WINDOW_TITLE);
    ROBO_CHECK_EQ_INT((int)pointer.window, (int)first);
    /* And a *release* is the same door with `pressed` 0. */
    ROBO_CHECK_EQ_INT(robo_desktop_observe_pointer(desktop, robo_gfx_point(200, 110), 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_pointer(desktop, &pointer), ROBO_OK);
    ROBO_CHECK_EQ_INT(pointer.pressed, 0);

    /* A press on a close button requests that window's close: `second` is at (150, 150, 400, 300), so its
     * button is ten pixels square with its left edge at 150 + 400 - 1 - 6 - 10. */
    ROBO_CHECK_EQ_INT(robo_desktop_observe_pointer(desktop, robo_gfx_point(537, 162), 1, NULL), ROBO_OK);
    {
        robo_desktop_window_state_t state = ROBO_DESKTOP_WINDOW_OPEN;

        ROBO_CHECK_EQ_INT(robo_desktop_window_state(desktop, second, &state), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)state, (int)ROBO_DESKTOP_WINDOW_CLOSING);
    }

    /* A press that hit nothing is a legitimate press: a shell that reported an error for a click on its
     * own surface would be reporting an error for the most ordinary thing a user does. */
    ROBO_CHECK_EQ_INT(robo_desktop_observe_pointer(desktop, robo_gfx_point(900, 700), 1, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_pointer(desktop, &pointer), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)pointer.area, (int)ROBO_DESKTOP_AREA_BACKGROUND);
    ROBO_CHECK_EQ_INT(pointer.inside, 1);

    ROBO_CHECK_EQ_INT(robo_desktop_stats(desktop, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)stats.pointer_actions, 2);
    ROBO_CHECK_EQ_INT((int)stats.pointer_moves >= 2, 1);

    ROBO_CHECK_EQ_INT(robo_desktop_observe_pointer(NULL, robo_gfx_point(0, 0), 0, NULL),
                      ROBO_INVALID_ARGUMENT);

    desktop_close(desktop);
}

void robo_test_register_desktop_window(void)
{
    robo_test_register("desktop.window.add.defaults", test_window_add_defaults);
    robo_test_register("desktop.window.add.minimum_and_bounds", test_window_add_minimum_and_bounds);
    robo_test_register("desktop.window.no_work_area", test_window_no_work_area);
    robo_test_register("desktop.window.capacity", test_window_capacity);
    robo_test_register("desktop.window.title_and_content", test_window_title_and_content);
    robo_test_register("desktop.window.raise_and_focus", test_window_raise_and_focus);
    robo_test_register("desktop.window.close_and_destroy", test_window_close_and_destroy);
    robo_test_register("desktop.window.hit_test", test_window_hit_test);
    robo_test_register("desktop.window.pointer_actions", test_window_pointer_actions);
}
