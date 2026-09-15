/*
 * RoboWindow keyboard foundation suite (Goal 7.10).
 * Headless mock injection — no GUI required.
 */

#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

typedef struct kb_mock {
    int init_count;
} kb_mock_t;

static kb_mock_t g_km;
static robo_window_backend_caps_t g_km_caps;
static robo_window_backend_vtbl_t g_km_vtbl;

static robo_result_t km_init(void *ud)
{
    kb_mock_t *m = (kb_mock_t *)ud;
    m->init_count++;
    return ROBO_OK;
}

static void km_shutdown(void *ud)
{
    (void)ud;
}

static robo_result_t km_create(void *ud, robo_window_id_t id,
                               const robo_window_config_t *cfg,
                               robo_window_native_handle_t *out)
{
    (void)ud;
    (void)id;
    (void)cfg;
    robo_window_native_handle_clear(out);
    return ROBO_OK;
}

static robo_result_t km_destroy(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *n)
{
    (void)ud;
    (void)id;
    if (n) {
        robo_window_native_handle_invalidate(n);
    }
    return ROBO_OK;
}

static robo_result_t km_ok(void *ud, robo_window_id_t id,
                           robo_window_native_handle_t *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return ROBO_OK;
}

static robo_result_t km_title(void *ud, robo_window_id_t id,
                              robo_window_native_handle_t *n, const char *t)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)t;
    return ROBO_OK;
}

static robo_result_t km_size(void *ud, robo_window_id_t id,
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

static robo_result_t km_cons(void *ud, robo_window_id_t id,
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

static robo_result_t km_pos(void *ud, robo_window_id_t id,
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

static robo_result_t km_present(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *n,
                                robo_gfx_surface_t *s)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)s;
    return ROBO_NOT_SUPPORTED;
}

static robo_result_t km_events(void *ud)
{
    (void)ud;
    return ROBO_OK;
}

static void km_prepare(void)
{
    memset(&g_km_caps, 0, sizeof(g_km_caps));
    g_km_caps.create_window = 1;
    g_km_caps.destroy_window = 1;
    g_km_caps.show_hide = 1;
    g_km_caps.resize = 1;
    g_km_caps.move = 1;
    g_km_caps.set_title = 1;
    g_km_caps.focus = 1;
    g_km_caps.process_events = 1;
    g_km_caps.size_constraints = 1;

    memset(&g_km_vtbl, 0, sizeof(g_km_vtbl));
    g_km_vtbl.name = "kb_mock";
    g_km_vtbl.capabilities = &g_km_caps;
    g_km_vtbl.initialize = km_init;
    g_km_vtbl.shutdown = km_shutdown;
    g_km_vtbl.create_window = km_create;
    g_km_vtbl.destroy_window = km_destroy;
    g_km_vtbl.show = km_ok;
    g_km_vtbl.hide = km_ok;
    g_km_vtbl.set_title = km_title;
    g_km_vtbl.set_size = km_size;
    g_km_vtbl.set_size_constraints = km_cons;
    g_km_vtbl.set_position = km_pos;
    g_km_vtbl.focus = km_ok;
    g_km_vtbl.present_surface = km_present;
    g_km_vtbl.process_events = km_events;
}

static void drain(void)
{
    robo_window_event_t ev;
    while (robo_window_event_poll(&ev) == ROBO_OK) {
    }
}

static void fixture(void)
{
    memset(&g_km, 0, sizeof(g_km));
    km_prepare();
    robo_window_shutdown();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_km_vtbl, &g_km), ROBO_OK);
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

static void inject_down(robo_window_id_t id, robo_physical_key_t phys,
                        robo_logical_key_t log, robo_key_modifier_flags_t mods,
                        int repeat)
{
    robo_keyboard_event_t ke;
    robo_keyboard_event_init(&ke);
    ke.action = ROBO_KEY_ACTION_DOWN;
    ke.physical_key = phys;
    ke.logical_key = log;
    ke.modifiers = mods;
    ke.is_repeat = repeat;
    ke.timestamp_ms = 1234u;
    ke.window_id = id;
    ROBO_CHECK_EQ_INT(robo_window_observe_key(id, &ke), ROBO_OK);
}

static void inject_up(robo_window_id_t id, robo_physical_key_t phys,
                      robo_logical_key_t log, robo_key_modifier_flags_t mods)
{
    robo_keyboard_event_t ke;
    robo_keyboard_event_init(&ke);
    ke.action = ROBO_KEY_ACTION_UP;
    ke.physical_key = phys;
    ke.logical_key = log;
    ke.modifiers = mods;
    ke.window_id = id;
    ROBO_CHECK_EQ_INT(robo_window_observe_key(id, &ke), ROBO_OK);
}

static void test_key_model(void)
{
    ROBO_CHECK(robo_physical_key_is_known(ROBO_PHYSICAL_KEY_A));
    ROBO_CHECK(robo_logical_key_is_known(ROBO_LOGICAL_KEY_ENTER));
    ROBO_CHECK(!robo_physical_key_is_known(ROBO_PHYSICAL_KEY_UNKNOWN));
    ROBO_CHECK(!robo_logical_key_is_known(ROBO_LOGICAL_KEY_UNKNOWN));
    ROBO_CHECK_STR_EQ(robo_physical_key_name(ROBO_PHYSICAL_KEY_SPACE), "Space");
    ROBO_CHECK_STR_EQ(robo_logical_key_name(ROBO_LOGICAL_KEY_ESCAPE), "Escape");
    ROBO_CHECK_STR_EQ(robo_key_action_name(ROBO_KEY_ACTION_DOWN), "down");
    /* Physical A vs logical A are distinct enum namespaces (same ordinal OK). */
    ROBO_CHECK(ROBO_PHYSICAL_KEY_A != ROBO_PHYSICAL_KEY_UNKNOWN);
    ROBO_CHECK(ROBO_LOGICAL_KEY_A != ROBO_LOGICAL_KEY_UNKNOWN);
}

static void test_press_release_order(void)
{
    robo_window_id_t id;
    robo_window_event_t ev;

    fixture();
    id = make_focused();

    inject_down(id, ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A, ROBO_KEY_MOD_NONE,
                0);
    inject_down(id, ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A, ROBO_KEY_MOD_NONE,
                1);
    inject_up(id, ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A, ROBO_KEY_MOD_NONE);

    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_KEY_DOWN);
    ROBO_CHECK_EQ_INT((int)ev.key.physical_key, (int)ROBO_PHYSICAL_KEY_A);
    ROBO_CHECK_EQ_INT(ev.key.is_repeat, 0);
    ROBO_CHECK_EQ_INT((int)ev.key.timestamp_ms, 1234);

    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_KEY_DOWN);
    ROBO_CHECK_EQ_INT(ev.key.is_repeat, 1);

    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_KEY_UP);

    ROBO_CHECK(robo_keyboard_is_physical_pressed(ROBO_PHYSICAL_KEY_A) == 0);
    fixture_end();
}

static void test_modifiers(void)
{
    robo_window_id_t id;

    fixture();
    id = make_focused();

    inject_down(id, ROBO_PHYSICAL_KEY_SHIFT_LEFT, ROBO_LOGICAL_KEY_SHIFT_LEFT,
                ROBO_KEY_MOD_SHIFT | ROBO_KEY_MOD_SHIFT_LEFT |
                    ROBO_KEY_MOD_CAPS_LOCK,
                0);
    ROBO_CHECK(robo_keyboard_modifier_active(ROBO_KEY_MOD_SHIFT));
    ROBO_CHECK(robo_keyboard_modifier_active(ROBO_KEY_MOD_SHIFT_LEFT));
    ROBO_CHECK(robo_keyboard_modifier_active(ROBO_KEY_MOD_CAPS_LOCK));
    ROBO_CHECK(robo_keyboard_is_physical_pressed(ROBO_PHYSICAL_KEY_SHIFT_LEFT));

    inject_up(id, ROBO_PHYSICAL_KEY_SHIFT_LEFT, ROBO_LOGICAL_KEY_SHIFT_LEFT,
              ROBO_KEY_MOD_CAPS_LOCK);
    ROBO_CHECK(!robo_keyboard_modifier_active(ROBO_KEY_MOD_SHIFT));
    ROBO_CHECK(robo_keyboard_modifier_active(ROBO_KEY_MOD_CAPS_LOCK));

    inject_down(id, ROBO_PHYSICAL_KEY_CONTROL_LEFT, ROBO_LOGICAL_KEY_CONTROL_LEFT,
                ROBO_KEY_MOD_CTRL | ROBO_KEY_MOD_CTRL_LEFT | ROBO_KEY_MOD_ALT |
                    ROBO_KEY_MOD_ALT_LEFT | ROBO_KEY_MOD_META |
                    ROBO_KEY_MOD_META_LEFT | ROBO_KEY_MOD_NUM_LOCK |
                    ROBO_KEY_MOD_SCROLL_LOCK,
                0);
    ROBO_CHECK(robo_keyboard_modifier_active(ROBO_KEY_MOD_CTRL));
    ROBO_CHECK(robo_keyboard_modifier_active(ROBO_KEY_MOD_ALT));
    ROBO_CHECK(robo_keyboard_modifier_active(ROBO_KEY_MOD_META));
    ROBO_CHECK(robo_keyboard_modifier_active(ROBO_KEY_MOD_NUM_LOCK));
    ROBO_CHECK(robo_keyboard_modifier_active(ROBO_KEY_MOD_SCROLL_LOCK));
    fixture_end();
}

static void test_targeting(void)
{
    robo_window_id_t a;
    robo_window_id_t b;
    robo_window_config_t cfg;
    robo_keyboard_event_t ke;
    robo_window_event_t ev;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &b), ROBO_OK);
    drain();

    /* No focused window + NONE target → NOT_FOUND (state may still update). */
    robo_keyboard_event_init(&ke);
    ke.action = ROBO_KEY_ACTION_DOWN;
    ke.physical_key = ROBO_PHYSICAL_KEY_B;
    ke.logical_key = ROBO_LOGICAL_KEY_B;
    ROBO_CHECK_EQ_INT(robo_window_observe_key(ROBO_WINDOW_ID_NONE, &ke),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_window_observe_focus(a, 1), ROBO_OK);
    drain();
    inject_down(ROBO_WINDOW_ID_NONE, ROBO_PHYSICAL_KEY_C, ROBO_LOGICAL_KEY_C,
                ROBO_KEY_MOD_NONE, 0);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK(ev.window_id == a);

    /* Focus change */
    ROBO_CHECK_EQ_INT(robo_window_observe_focus(a, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_observe_focus(b, 1), ROBO_OK);
    drain();
    inject_down(ROBO_WINDOW_ID_NONE, ROBO_PHYSICAL_KEY_D, ROBO_LOGICAL_KEY_D,
                ROBO_KEY_MOD_NONE, 0);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK(ev.window_id == b);

    /* Destroyed / stale */
    ROBO_CHECK_EQ_INT(robo_window_destroy(b), ROBO_OK);
    drain();
    ROBO_CHECK_EQ_INT(robo_window_observe_key(b, &ke), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(
        robo_window_observe_key((robo_window_id_t)99999u, &ke), ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_window_destroy(a), ROBO_OK);
    fixture_end();
}

static void test_stuck_key_reset(void)
{
    robo_window_id_t id;

    fixture();
    id = make_focused();
    inject_down(id, ROBO_PHYSICAL_KEY_E, ROBO_LOGICAL_KEY_E, ROBO_KEY_MOD_NONE,
                0);
    ROBO_CHECK(robo_keyboard_is_physical_pressed(ROBO_PHYSICAL_KEY_E));

    ROBO_CHECK_EQ_INT(robo_window_observe_focus(id, 0), ROBO_OK);
    ROBO_CHECK(!robo_keyboard_is_physical_pressed(ROBO_PHYSICAL_KEY_E));

    ROBO_CHECK_EQ_INT(robo_window_observe_focus(id, 1), ROBO_OK);
    inject_down(id, ROBO_PHYSICAL_KEY_F, ROBO_LOGICAL_KEY_F, ROBO_KEY_MOD_NONE,
                0);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK(!robo_keyboard_is_physical_pressed(ROBO_PHYSICAL_KEY_F));

    fixture();
    id = make_focused();
    inject_down(id, ROBO_PHYSICAL_KEY_G, ROBO_LOGICAL_KEY_G, ROBO_KEY_MOD_NONE,
                0);
    fixture_end();
    ROBO_CHECK(!robo_keyboard_is_physical_pressed(ROBO_PHYSICAL_KEY_G));
}

static void test_unknown_and_errors(void)
{
    robo_window_id_t id;
    robo_keyboard_event_t ke;

    fixture();
    id = make_focused();

    robo_keyboard_event_init(&ke);
    ke.action = ROBO_KEY_ACTION_DOWN;
    ke.physical_key = ROBO_PHYSICAL_KEY_UNKNOWN;
    ke.logical_key = ROBO_LOGICAL_KEY_UNKNOWN;
    ROBO_CHECK_EQ_INT(robo_window_observe_key(id, &ke), ROBO_OK);

    ke.action = ROBO_KEY_ACTION_NONE;
    ke.physical_key = ROBO_PHYSICAL_KEY_A;
    ke.logical_key = ROBO_LOGICAL_KEY_A;
    ROBO_CHECK_EQ_INT(robo_window_observe_key(id, &ke), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_window_observe_key(id, NULL), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_window_thread_bind_owner(1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_thread_enter(2), ROBO_OK);
    ke.action = ROBO_KEY_ACTION_DOWN;
    ROBO_CHECK_EQ_INT(robo_window_observe_key(id, &ke), ROBO_PERMISSION_DENIED);
    fixture_end();
}

static void test_phys_logical_distinction(void)
{
    robo_window_id_t id;
    robo_window_event_t ev;

    fixture();
    id = make_focused();
    /* Physical Q position reporting logical A (layout remap simulation). */
    inject_down(id, ROBO_PHYSICAL_KEY_Q, ROBO_LOGICAL_KEY_A, ROBO_KEY_MOD_NONE,
                0);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.key.physical_key, (int)ROBO_PHYSICAL_KEY_Q);
    ROBO_CHECK_EQ_INT((int)ev.key.logical_key, (int)ROBO_LOGICAL_KEY_A);
    ROBO_CHECK(robo_keyboard_is_physical_pressed(ROBO_PHYSICAL_KEY_Q));
    ROBO_CHECK(robo_keyboard_is_logical_pressed(ROBO_LOGICAL_KEY_A));
    fixture_end();
}

void robo_test_register_window_keyboard(void)
{
    robo_test_register("window.key.model", test_key_model);
    robo_test_register("window.key.press_release", test_press_release_order);
    robo_test_register("window.key.modifiers", test_modifiers);
    robo_test_register("window.key.targeting", test_targeting);
    robo_test_register("window.key.reset", test_stuck_key_reset);
    robo_test_register("window.key.errors", test_unknown_and_errors);
    robo_test_register("window.key.phys_logical", test_phys_logical_distinction);
}
