/*
 * RoboWindow close & lifecycle suite (Goal 7.7).
 * Headless mock — simulates close request, destroy failure, shutdown.
 */

#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

typedef struct life_mock {
    int init_count;
    int shutdown_count;
    int create_count;
    int destroy_count;
    int show_count;
    int hide_count;
    int size_count;
    int present_count;
    int fail_destroy;
    int fail_create;
    uint32_t next_gen;
    uint64_t last_token;
} life_mock_t;

static life_mock_t g_lm;
static robo_window_backend_caps_t g_lm_caps;
static robo_window_backend_vtbl_t g_lm_vtbl;

static robo_result_t lm_init(void *ud)
{
    life_mock_t *m = (life_mock_t *)ud;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->init_count++;
    m->next_gen = 1u;
    return ROBO_OK;
}

static void lm_shutdown(void *ud)
{
    life_mock_t *m = (life_mock_t *)ud;
    if (m) {
        m->shutdown_count++;
    }
}

static robo_result_t lm_create(void *ud,
                               robo_window_id_t id,
                               const robo_window_config_t *cfg,
                               robo_window_native_handle_t *out_native)
{
    life_mock_t *m = (life_mock_t *)ud;
    (void)id;
    (void)cfg;
    if (!m || !out_native) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (m->fail_create) {
        return ROBO_ERROR;
    }
    m->create_count++;
    m->last_token = 0x4C494645ull; /* LIFE */
    return robo_window_native_handle_assign(out_native, m->last_token,
                                            m->next_gen++);
}

static robo_result_t lm_destroy(void *ud,
                                robo_window_id_t id,
                                robo_window_native_handle_t *native)
{
    life_mock_t *m = (life_mock_t *)ud;
    (void)id;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (m->fail_destroy) {
        return ROBO_ERROR;
    }
    m->destroy_count++;
    if (native) {
        robo_window_native_handle_invalidate(native);
    }
    return ROBO_OK;
}

static robo_result_t lm_show(void *ud,
                             robo_window_id_t id,
                             robo_window_native_handle_t *native)
{
    life_mock_t *m = (life_mock_t *)ud;
    (void)id;
    (void)native;
    if (m) {
        m->show_count++;
    }
    return ROBO_OK;
}

static robo_result_t lm_hide(void *ud,
                             robo_window_id_t id,
                             robo_window_native_handle_t *native)
{
    life_mock_t *m = (life_mock_t *)ud;
    (void)id;
    (void)native;
    if (m) {
        m->hide_count++;
    }
    return ROBO_OK;
}

static robo_result_t lm_title(void *ud,
                              robo_window_id_t id,
                              robo_window_native_handle_t *native,
                              const char *utf8_title)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)utf8_title;
    return ROBO_OK;
}

static robo_result_t lm_size(void *ud,
                             robo_window_id_t id,
                             robo_window_native_handle_t *native,
                             int32_t width,
                             int32_t height)
{
    life_mock_t *m = (life_mock_t *)ud;
    (void)id;
    (void)native;
    (void)width;
    (void)height;
    if (m) {
        m->size_count++;
    }
    return ROBO_OK;
}

static robo_result_t lm_constraints(void *ud,
                                    robo_window_id_t id,
                                    robo_window_native_handle_t *native,
                                    int32_t min_w,
                                    int32_t min_h,
                                    int32_t max_w,
                                    int32_t max_h)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)min_w;
    (void)min_h;
    (void)max_w;
    (void)max_h;
    return ROBO_OK;
}

static robo_result_t lm_pos(void *ud,
                            robo_window_id_t id,
                            robo_window_native_handle_t *native,
                            int32_t x,
                            int32_t y)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)x;
    (void)y;
    return ROBO_OK;
}

static robo_result_t lm_focus(void *ud,
                              robo_window_id_t id,
                              robo_window_native_handle_t *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t lm_present(void *ud,
                                robo_window_id_t id,
                                robo_window_native_handle_t *native,
                                robo_gfx_surface_t *surface)
{
    life_mock_t *m = (life_mock_t *)ud;
    (void)id;
    (void)native;
    (void)surface;
    if (m) {
        m->present_count++;
    }
    return ROBO_OK;
}

static robo_result_t lm_events(void *ud)
{
    (void)ud;
    return ROBO_OK;
}

static void life_caps_bind(void)
{
    memset(&g_lm_caps, 0, sizeof(g_lm_caps));
    g_lm_caps.create_window = 1;
    g_lm_caps.destroy_window = 1;
    g_lm_caps.show_hide = 1;
    g_lm_caps.set_title = 1;
    g_lm_caps.resize = 1;
    g_lm_caps.size_constraints = 1;
    g_lm_caps.move = 1;
    g_lm_caps.focus = 1;
    g_lm_caps.present = 1;
    g_lm_caps.process_events = 1;
    g_lm_caps.present_format_mask = ROBO_WINDOW_PRESENT_FMT_CPU_DEFAULT;
}

static void life_vtbl_bind(void)
{
    memset(&g_lm_vtbl, 0, sizeof(g_lm_vtbl));
    g_lm_vtbl.name = "life_mock";
    g_lm_vtbl.capabilities = &g_lm_caps;
    g_lm_vtbl.initialize = lm_init;
    g_lm_vtbl.shutdown = lm_shutdown;
    g_lm_vtbl.create_window = lm_create;
    g_lm_vtbl.destroy_window = lm_destroy;
    g_lm_vtbl.show = lm_show;
    g_lm_vtbl.hide = lm_hide;
    g_lm_vtbl.set_title = lm_title;
    g_lm_vtbl.set_size = lm_size;
    g_lm_vtbl.set_size_constraints = lm_constraints;
    g_lm_vtbl.set_position = lm_pos;
    g_lm_vtbl.focus = lm_focus;
    g_lm_vtbl.present_surface = lm_present;
    g_lm_vtbl.process_events = lm_events;
}

static void fixture(void)
{
    memset(&g_lm, 0, sizeof(g_lm));
    life_caps_bind();
    life_vtbl_bind();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_lm_vtbl, &g_lm), ROBO_OK);
}

static void fixture_end(void)
{
    robo_window_shutdown();
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
    return robo_gfx_surface_create(&desc, out);
}

static void drain_created(robo_window_id_t id)
{
    robo_window_event_t ev;
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_CREATED);
    ROBO_CHECK_EQ_INT((int)ev.window_id, (int)id);
}

static void test_life_transitions(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_lifecycle_t life;
    robo_window_event_t ev;
    robo_window_native_handle_t h;

    fixture();
    ROBO_CHECK_EQ_INT(robo_window_lifecycle_allows_mutation(
                          ROBO_WINDOW_LIFECYCLE_CREATED),
                      1);
    ROBO_CHECK_EQ_INT(robo_window_lifecycle_allows_mutation(
                          ROBO_WINDOW_LIFECYCLE_CLOSING),
                      0);
    ROBO_CHECK_EQ_INT(robo_window_lifecycle_check_transition(
                          ROBO_WINDOW_LIFECYCLE_CLOSING,
                          ROBO_WINDOW_LIFECYCLE_CREATED),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_lifecycle_check_transition(
                          ROBO_WINDOW_LIFECYCLE_CREATED,
                          ROBO_WINDOW_LIFECYCLE_CLOSING),
                      ROBO_OK);

    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain_created(id);
    ROBO_CHECK_EQ_INT(robo_window_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)life, (int)ROBO_WINDOW_LIFECYCLE_CREATED);
    ROBO_CHECK_EQ_INT(robo_window_is_closing(id), 0);

    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_SHOWN);
    ROBO_CHECK_EQ_INT(robo_window_hide(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_HIDDEN);

    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_CLOSE_REQUESTED);
    ROBO_CHECK_EQ_INT(robo_window_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)life, (int)ROBO_WINDOW_LIFECYCLE_CLOSING);
    ROBO_CHECK_EQ_INT(robo_window_is_closing(id), 1);
    ROBO_CHECK(robo_window_id_is_live(id));
    ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_OK);
    ROBO_CHECK(robo_window_native_handle_is_valid(&h));

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_CLOSED);
    ROBO_CHECK_EQ_INT(robo_window_get_lifecycle(id, &life), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_is_closing(id), 0);
    ROBO_CHECK_EQ_INT(robo_window_id_is_live(id), 0);
    ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_NOT_FOUND);
    fixture_end();
}

static void test_life_close_request(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_event_t ev;
    robo_window_lifecycle_t life;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain_created(id);

    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_CLOSE_REQUESTED);
    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_ALREADY_EXISTS);
    ROBO_CHECK_EQ_INT(robo_window_observe_close_request(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_observe_close_request(id), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_get_lifecycle(id, &life), ROBO_NOT_FOUND);

    /* Close while hidden */
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain_created(id);
    ROBO_CHECK_EQ_INT(robo_window_hide(id), ROBO_OK);
    (void)robo_window_event_poll(&ev);
    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
}

static void test_life_observe_native_close(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_event_t ev;
    robo_window_lifecycle_t life;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain_created(id);

    /* Simulate native WM_CLOSE injection path. */
    ROBO_CHECK_EQ_INT(robo_window_observe_close_request(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_CLOSE_REQUESTED);
    ROBO_CHECK_EQ_INT(robo_window_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)life, (int)ROBO_WINDOW_LIFECYCLE_CLOSING);
    ROBO_CHECK_EQ_INT(robo_window_observe_close_request(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_lm.destroy_count, 0);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
}

static void test_life_destroy(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_event_t ev;
    robo_window_native_handle_t h;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;

    /* Destroy without close request */
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain_created(id);
    ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_OK);
    ROBO_CHECK(robo_window_native_handle_is_valid(&h));
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_CLOSED);
    ROBO_CHECK_EQ_INT(g_lm.destroy_count, 1);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(g_lm.destroy_count, 1);

    /* Destroy after close */
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain_created(id);
    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    (void)robo_window_event_poll(&ev);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_CLOSED);
    ROBO_CHECK_EQ_INT(g_lm.destroy_count, 2);

    /* Backend destroy failure leaves window live */
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain_created(id);
    g_lm.fail_destroy = 1;
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_ERROR);
    ROBO_CHECK(robo_window_id_is_live(id));
    ROBO_CHECK_EQ_INT((int)robo_window_is_closing(id), 0);
    g_lm.fail_destroy = 0;
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_CLOSED);
    fixture_end();
}

static void test_life_ops_restricted(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_event_t ev;
    robo_gfx_surface_t *surf = NULL;
    robo_window_size_constraints_t c;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain_created(id);
    ROBO_CHECK_EQ_INT(make_surface(4, 4, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_present_surface(id, surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    (void)robo_window_event_poll(&ev);

    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_hide(id), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 320, 240), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_set_title(id, "x"), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_request_position(id, 1, 1),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_request_focus(id), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_set_present_surface(id, surf),
                      ROBO_INVALID_ARGUMENT);
    robo_window_size_constraints_init(&c);
    c.min_width = 100;
    c.min_height = 100;
    ROBO_CHECK_EQ_INT(robo_window_set_size_constraints(id, &c),
                      ROBO_INVALID_ARGUMENT);
    /* Reads still allowed while CLOSING */
    {
        robo_gfx_size_t sz;
        ROBO_CHECK_EQ_INT(robo_window_get_size(id, &sz), ROBO_OK);
    }

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    (void)robo_window_event_poll(&ev);

    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 320, 240), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_NOT_FOUND);

    robo_gfx_surface_release(surf);
    fixture_end();
}

static void test_life_shutdown_live(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_native_handle_t h;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain_created(id);
    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    ROBO_CHECK(robo_window_live_count() >= 1u);
    ROBO_CHECK_EQ_INT(g_lm.destroy_count, 0);

    fixture_end();
    ROBO_CHECK(g_lm.shutdown_count >= 1);
    ROBO_CHECK(g_lm.destroy_count >= 1);
    ROBO_CHECK_EQ_INT(robo_window_id_is_live(id), 0);
    ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_INIT_ERROR);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_INIT_ERROR);
}

static void test_life_callbacks(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_event_t ev;
    int close_n = 0;
    int closed_n = 0;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain_created(id);

    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    while (robo_window_event_poll(&ev) == ROBO_OK) {
        if (ev.type == ROBO_WINDOW_EVENT_CLOSE_REQUESTED) {
            close_n++;
        }
        if (ev.type == ROBO_WINDOW_EVENT_CLOSED) {
            closed_n++;
        }
    }
    ROBO_CHECK_EQ_INT(close_n, 1);
    ROBO_CHECK_EQ_INT(closed_n, 0);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    while (robo_window_event_poll(&ev) == ROBO_OK) {
        if (ev.type == ROBO_WINDOW_EVENT_CLOSE_REQUESTED) {
            close_n++;
        }
        if (ev.type == ROBO_WINDOW_EVENT_CLOSED) {
            closed_n++;
        }
    }
    ROBO_CHECK_EQ_INT(close_n, 1);
    ROBO_CHECK_EQ_INT(closed_n, 1);

    /* No further callbacks after destroy */
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_NOT_FOUND);
    fixture_end();
}

static void test_life_threading(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;

    fixture();
    ROBO_CHECK_EQ_INT(robo_window_thread_bind_owner(3u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_thread_enter(3u), ROBO_OK);

    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain_created(id);

    ROBO_CHECK_EQ_INT(robo_window_thread_enter(4u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_PERMISSION_DENIED);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_PERMISSION_DENIED);

    ROBO_CHECK_EQ_INT(robo_window_thread_enter(3u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_thread_bind_owner(ROBO_WINDOW_THREAD_NONE),
                      ROBO_OK);
    fixture_end();
}

void robo_test_register_window_lifecycle(void)
{
    robo_test_register("window.life.transitions", test_life_transitions);
    robo_test_register("window.life.close_request", test_life_close_request);
    robo_test_register("window.life.observe_native", test_life_observe_native_close);
    robo_test_register("window.life.destroy", test_life_destroy);
    robo_test_register("window.life.ops_restricted", test_life_ops_restricted);
    robo_test_register("window.life.shutdown_live", test_life_shutdown_live);
    robo_test_register("window.life.callbacks", test_life_callbacks);
    robo_test_register("window.life.thread", test_life_threading);
}
