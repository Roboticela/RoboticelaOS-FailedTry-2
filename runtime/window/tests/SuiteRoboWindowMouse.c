/*
 * RoboWindow mouse foundation suite (Goal 7.11).
 * Headless mock injection — no GUI required.
 */

#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

typedef struct mouse_mock {
    int init_count;
} mouse_mock_t;

static mouse_mock_t g_mm;
static robo_window_backend_caps_t g_mm_caps;
static robo_window_backend_vtbl_t g_mm_vtbl;

static robo_result_t mm_init(void *ud)
{
    mouse_mock_t *m = (mouse_mock_t *)ud;
    m->init_count++;
    return ROBO_OK;
}

static void mm_shutdown(void *ud)
{
    (void)ud;
}

static robo_result_t mm_create(void *ud, robo_window_id_t id,
                               const robo_window_config_t *cfg,
                               robo_window_native_handle_t *out)
{
    (void)ud;
    (void)id;
    (void)cfg;
    robo_window_native_handle_clear(out);
    return ROBO_OK;
}

static robo_result_t mm_destroy(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *n)
{
    (void)ud;
    (void)id;
    if (n) {
        robo_window_native_handle_invalidate(n);
    }
    return ROBO_OK;
}

static robo_result_t mm_ok(void *ud, robo_window_id_t id,
                           robo_window_native_handle_t *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return ROBO_OK;
}

static robo_result_t mm_title(void *ud, robo_window_id_t id,
                              robo_window_native_handle_t *n, const char *t)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)t;
    return ROBO_OK;
}

static robo_result_t mm_size(void *ud, robo_window_id_t id,
                             robo_window_native_handle_t *n, int32_t w,
                             int32_t h)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)w;
    (void)h;
    return ROBO_OK;
}

static robo_result_t mm_cons(void *ud, robo_window_id_t id,
                             robo_window_native_handle_t *n, int32_t a,
                             int32_t b, int32_t c, int32_t d)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return ROBO_OK;
}

static robo_result_t mm_pos(void *ud, robo_window_id_t id,
                            robo_window_native_handle_t *n, int32_t x,
                            int32_t y)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)x;
    (void)y;
    return ROBO_OK;
}

static robo_result_t mm_present(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *n,
                                robo_gfx_surface_t *s)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)s;
    return ROBO_NOT_SUPPORTED;
}

static robo_result_t mm_events(void *ud)
{
    (void)ud;
    return ROBO_OK;
}

static void mm_prepare(void)
{
    memset(&g_mm_caps, 0, sizeof(g_mm_caps));
    g_mm_caps.create_window = 1;
    g_mm_caps.destroy_window = 1;
    g_mm_caps.show_hide = 1;
    g_mm_caps.resize = 1;
    g_mm_caps.move = 1;
    g_mm_caps.set_title = 1;
    g_mm_caps.focus = 1;
    g_mm_caps.process_events = 1;
    g_mm_caps.size_constraints = 1;

    memset(&g_mm_vtbl, 0, sizeof(g_mm_vtbl));
    g_mm_vtbl.name = "mouse_mock";
    g_mm_vtbl.capabilities = &g_mm_caps;
    g_mm_vtbl.initialize = mm_init;
    g_mm_vtbl.shutdown = mm_shutdown;
    g_mm_vtbl.create_window = mm_create;
    g_mm_vtbl.destroy_window = mm_destroy;
    g_mm_vtbl.show = mm_ok;
    g_mm_vtbl.hide = mm_ok;
    g_mm_vtbl.set_title = mm_title;
    g_mm_vtbl.set_size = mm_size;
    g_mm_vtbl.set_size_constraints = mm_cons;
    g_mm_vtbl.set_position = mm_pos;
    g_mm_vtbl.focus = mm_ok;
    g_mm_vtbl.present_surface = mm_present;
    g_mm_vtbl.process_events = mm_events;
}

static void drain(void)
{
    robo_window_event_t ev;
    while (robo_window_event_poll(&ev) == ROBO_OK) {
    }
}

static void fixture(void)
{
    memset(&g_mm, 0, sizeof(g_mm));
    mm_prepare();
    robo_window_shutdown();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_mm_vtbl, &g_mm), ROBO_OK);
    drain();
}

static void fixture_end(void)
{
    robo_window_shutdown();
}

static robo_window_id_t make_focused(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain();
    ROBO_CHECK_EQ_INT(robo_window_observe_focus(id, 1), ROBO_OK);
    drain();
    return id;
}

static void inject_move(robo_window_id_t id, int32_t x, int32_t y,
                        robo_key_modifier_flags_t mods)
{
    robo_mouse_event_t me;
    robo_mouse_event_init(&me);
    me.type = ROBO_MOUSE_EVENT_MOVE;
    me.client_x = x;
    me.client_y = y;
    me.screen_x = x + 10;
    me.screen_y = y + 20;
    me.has_screen_coords = 1;
    me.modifiers = mods;
    me.timestamp_ms = 1000u;
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
}

static void inject_button(robo_window_id_t id, robo_mouse_event_type_t type,
                          robo_mouse_button_t btn, int32_t x, int32_t y,
                          int click_count, robo_key_modifier_flags_t mods,
                          uint64_t ts)
{
    robo_mouse_event_t me;
    robo_mouse_event_init(&me);
    me.type = type;
    me.button = btn;
    me.client_x = x;
    me.client_y = y;
    me.has_screen_coords = 0;
    me.click_count = click_count;
    me.modifiers = mods;
    me.timestamp_ms = ts;
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
}

static void inject_wheel(robo_window_id_t id, int32_t sx, int32_t sy,
                         robo_key_modifier_flags_t mods)
{
    robo_mouse_event_t me;
    robo_mouse_event_init(&me);
    me.type = ROBO_MOUSE_EVENT_WHEEL;
    me.client_x = 5;
    me.client_y = 5;
    me.scroll_x = sx;
    me.scroll_y = sy;
    me.scroll_unit = ROBO_MOUSE_SCROLL_UNIT_LINE;
    me.modifiers = mods;
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
}

static void test_button_model(void)
{
    ROBO_CHECK(robo_mouse_button_is_known(ROBO_MOUSE_BUTTON_LEFT));
    ROBO_CHECK(robo_mouse_button_is_known(ROBO_MOUSE_BUTTON_RIGHT));
    ROBO_CHECK(robo_mouse_button_is_known(ROBO_MOUSE_BUTTON_MIDDLE));
    ROBO_CHECK(robo_mouse_button_is_known(ROBO_MOUSE_BUTTON_X1));
    ROBO_CHECK(robo_mouse_button_is_known(ROBO_MOUSE_BUTTON_X2));
    ROBO_CHECK(!robo_mouse_button_is_known(ROBO_MOUSE_BUTTON_NONE));
    ROBO_CHECK(!robo_mouse_button_is_known(ROBO_MOUSE_BUTTON_UNKNOWN));
    ROBO_CHECK_STR_EQ(robo_mouse_button_name(ROBO_MOUSE_BUTTON_LEFT), "left");
    ROBO_CHECK_STR_EQ(robo_mouse_event_type_name(ROBO_MOUSE_EVENT_WHEEL),
                      "wheel");
}

static void test_move_and_delta(void)
{
    robo_window_id_t id;
    robo_window_event_t ev;
    robo_mouse_position_t pos;

    fixture();
    id = make_focused();

    inject_move(id, 10, 20, ROBO_KEY_MOD_NONE);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_MOUSE_MOVE);
    ROBO_CHECK_EQ_INT(ev.mouse.client_x, 10);
    ROBO_CHECK_EQ_INT(ev.mouse.client_y, 20);
    ROBO_CHECK_EQ_INT(ev.mouse.delta_x, 0);
    ROBO_CHECK_EQ_INT(ev.mouse.delta_y, 0);
    ROBO_CHECK(ev.mouse.has_screen_coords);
    ROBO_CHECK(robo_mouse_get_position(&pos));
    ROBO_CHECK_EQ_INT(pos.x, 10);
    ROBO_CHECK_EQ_INT(pos.y, 20);

    inject_move(id, 15, 28, ROBO_KEY_MOD_SHIFT);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT(ev.mouse.delta_x, 5);
    ROBO_CHECK_EQ_INT(ev.mouse.delta_y, 8);
    ROBO_CHECK(ev.mouse.modifiers & ROBO_KEY_MOD_SHIFT);

    /* Explicit delta from injector is preserved when non-zero. */
    {
        robo_mouse_event_t me;
        robo_mouse_event_init(&me);
        me.type = ROBO_MOUSE_EVENT_MOVE;
        me.client_x = 100;
        me.client_y = 100;
        me.delta_x = 3;
        me.delta_y = -1;
        ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
        ROBO_CHECK_EQ_INT(ev.mouse.delta_x, 3);
        ROBO_CHECK_EQ_INT(ev.mouse.delta_y, -1);
    }

    fixture_end();
}

static void test_buttons_and_state(void)
{
    robo_window_id_t id;
    robo_window_event_t ev;

    fixture();
    id = make_focused();

    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_DOWN, ROBO_MOUSE_BUTTON_LEFT, 1, 1,
                  1, ROBO_KEY_MOD_CTRL, 10u);
    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_DOWN, ROBO_MOUSE_BUTTON_RIGHT, 1, 1,
                  1, ROBO_KEY_MOD_CTRL, 11u);
    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_DOWN, ROBO_MOUSE_BUTTON_MIDDLE, 1,
                  1, 1, ROBO_KEY_MOD_NONE, 12u);
    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_DOWN, ROBO_MOUSE_BUTTON_X1, 1, 1, 1,
                  ROBO_KEY_MOD_NONE, 13u);
    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_DOWN, ROBO_MOUSE_BUTTON_X2, 1, 1, 1,
                  ROBO_KEY_MOD_NONE, 14u);

    ROBO_CHECK(robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_LEFT));
    ROBO_CHECK(robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_RIGHT));
    ROBO_CHECK(robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_MIDDLE));
    ROBO_CHECK(robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_X1));
    ROBO_CHECK(robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_X2));

    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN);
    ROBO_CHECK_EQ_INT((int)ev.mouse.button, (int)ROBO_MOUSE_BUTTON_LEFT);
    ROBO_CHECK(ev.mouse.modifiers & ROBO_KEY_MOD_CTRL);

    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_UP, ROBO_MOUSE_BUTTON_LEFT, 1, 1, 0,
                  ROBO_KEY_MOD_NONE, 20u);
    ROBO_CHECK(!robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_LEFT));
    ROBO_CHECK(robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_RIGHT));

    fixture_end();
}

static void test_unknown_button(void)
{
    robo_window_id_t id;
    robo_mouse_event_t me;
    robo_window_event_t ev;

    fixture();
    id = make_focused();

    robo_mouse_event_init(&me);
    me.type = ROBO_MOUSE_EVENT_BUTTON_DOWN;
    me.button = ROBO_MOUSE_BUTTON_UNKNOWN;
    me.client_x = 0;
    me.client_y = 0;
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_UNKNOWN));

    fixture_end();
}

static void test_click_count(void)
{
    robo_window_id_t id;
    robo_window_event_t ev;

    fixture();
    id = make_focused();

    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_DOWN, ROBO_MOUSE_BUTTON_LEFT, 50,
                  50, 0, ROBO_KEY_MOD_NONE, 100u);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT(ev.mouse.click_count, 1);

    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_UP, ROBO_MOUSE_BUTTON_LEFT, 50, 50,
                  0, ROBO_KEY_MOD_NONE, 110u);
    drain();

    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_DOWN, ROBO_MOUSE_BUTTON_LEFT, 51,
                  50, 0, ROBO_KEY_MOD_NONE, 200u);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT(ev.mouse.click_count, 2);

    /* Native double-click count preserved when provided. */
    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_DOWN, ROBO_MOUSE_BUTTON_RIGHT, 0, 0,
                  2, ROBO_KEY_MOD_NONE, 300u);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT(ev.mouse.click_count, 2);

    fixture_end();
}

static void test_wheel(void)
{
    robo_window_id_t id;
    robo_window_event_t ev;
    robo_mouse_event_t me;

    fixture();
    id = make_focused();

    inject_wheel(id, 0, 120, ROBO_KEY_MOD_SHIFT);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_MOUSE_WHEEL);
    ROBO_CHECK_EQ_INT(ev.mouse.scroll_y, 120);
    ROBO_CHECK_EQ_INT(ev.mouse.scroll_x, 0);
    ROBO_CHECK(ev.mouse.modifiers & ROBO_KEY_MOD_SHIFT);

    inject_wheel(id, -60, 0, ROBO_KEY_MOD_NONE);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT(ev.mouse.scroll_x, -60);

    /* High-resolution fraction of a notch. */
    inject_wheel(id, 0, 30, ROBO_KEY_MOD_NONE);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT(ev.mouse.scroll_y, 30);

    /* Zero delta ignored. */
    robo_mouse_event_init(&me);
    me.type = ROBO_MOUSE_EVENT_WHEEL;
    me.scroll_x = 0;
    me.scroll_y = 0;
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_NOT_FOUND);

    fixture_end();
}

static void test_targeting_and_capture(void)
{
    robo_window_id_t a;
    robo_window_id_t b;
    robo_window_config_t cfg;
    robo_mouse_event_t me;
    robo_window_event_t ev;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &b), ROBO_OK);
    drain();

    robo_mouse_event_init(&me);
    me.type = ROBO_MOUSE_EVENT_MOVE;
    me.client_x = 1;
    me.client_y = 1;
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(ROBO_WINDOW_ID_NONE, &me),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_window_observe_focus(a, 1), ROBO_OK);
    drain();
    inject_move(ROBO_WINDOW_ID_NONE, 2, 3, ROBO_KEY_MOD_NONE);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK(ev.window_id == a);

    /* Soft capture redirects NONE targeting even without native caps. */
    ROBO_CHECK_EQ_INT(robo_window_mouse_capture(b), ROBO_OK);
    ROBO_CHECK(robo_window_mouse_captured_id() == b);
    inject_move(ROBO_WINDOW_ID_NONE, 4, 5, ROBO_KEY_MOD_NONE);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK(ev.window_id == b);

    ROBO_CHECK_EQ_INT(robo_window_mouse_release_capture(), ROBO_OK);
    ROBO_CHECK(robo_window_mouse_captured_id() == ROBO_WINDOW_ID_NONE);

    ROBO_CHECK_EQ_INT(robo_window_destroy(b), ROBO_OK);
    drain();
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(b, &me), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(
        robo_window_observe_mouse((robo_window_id_t)99999u, &me),
        ROBO_NOT_FOUND);

    /* Resize does not invalidate subsequent mouse delivery. */
    ROBO_CHECK_EQ_INT(robo_window_observe_size(a, 640, 480), ROBO_OK);
    drain();
    inject_move(a, 9, 9, ROBO_KEY_MOD_NONE);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK(ev.window_id == a);

    ROBO_CHECK_EQ_INT(robo_window_destroy(a), ROBO_OK);
    fixture_end();
}

static void test_stuck_button_reset(void)
{
    robo_window_id_t id;

    fixture();
    id = make_focused();
    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_DOWN, ROBO_MOUSE_BUTTON_LEFT, 0, 0,
                  1, ROBO_KEY_MOD_NONE, 1u);
    ROBO_CHECK(robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_LEFT));

    ROBO_CHECK_EQ_INT(robo_window_observe_focus(id, 0), ROBO_OK);
    ROBO_CHECK(!robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_LEFT));

    ROBO_CHECK_EQ_INT(robo_window_observe_focus(id, 1), ROBO_OK);
    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_DOWN, ROBO_MOUSE_BUTTON_RIGHT, 0,
                  0, 1, ROBO_KEY_MOD_NONE, 2u);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK(!robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_RIGHT));

    fixture();
    id = make_focused();
    inject_button(id, ROBO_MOUSE_EVENT_BUTTON_DOWN, ROBO_MOUSE_BUTTON_MIDDLE, 0,
                  0, 1, ROBO_KEY_MOD_NONE, 3u);
    fixture_end();
    ROBO_CHECK(!robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_MIDDLE));
}

static void test_malformed(void)
{
    robo_window_id_t id;
    robo_mouse_event_t me;

    fixture();
    id = make_focused();

    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_mouse_event_init(&me);
    me.type = ROBO_MOUSE_EVENT_NONE;
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me),
                      ROBO_INVALID_ARGUMENT);

    fixture_end();
}

static void test_dpi_coord_semantics(void)
{
    robo_window_id_t id;
    robo_window_event_t ev;
    float scale = 0.0f;
    robo_gfx_size_t phys;

    fixture();
    id = make_focused();
    ROBO_CHECK_EQ_INT(robo_window_observe_size(id, 200, 100), ROBO_OK);
    drain();
    ROBO_CHECK_EQ_INT(robo_window_get_scale(id, &scale), ROBO_OK);
    ROBO_CHECK(scale > 0.0f);

    inject_move(id, 100, 50, ROBO_KEY_MOD_NONE);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    /* Client coords remain logical DIPs; physical size uses scale separately. */
    ROBO_CHECK_EQ_INT(ev.mouse.client_x, 100);
    ROBO_CHECK_EQ_INT(robo_window_get_size_physical(id, &phys), ROBO_OK);
    ROBO_CHECK(phys.width > 0);
    ROBO_CHECK(phys.height > 0);

    fixture_end();
}

void robo_test_register_window_mouse(void)
{
    robo_test_register("window.mouse.button_model", test_button_model);
    robo_test_register("window.mouse.move_delta", test_move_and_delta);
    robo_test_register("window.mouse.buttons_state", test_buttons_and_state);
    robo_test_register("window.mouse.unknown_button", test_unknown_button);
    robo_test_register("window.mouse.click_count", test_click_count);
    robo_test_register("window.mouse.wheel", test_wheel);
    robo_test_register("window.mouse.targeting_capture",
                       test_targeting_and_capture);
    robo_test_register("window.mouse.stuck_reset", test_stuck_button_reset);
    robo_test_register("window.mouse.malformed", test_malformed);
    robo_test_register("window.mouse.dpi_coords", test_dpi_coord_semantics);
}
