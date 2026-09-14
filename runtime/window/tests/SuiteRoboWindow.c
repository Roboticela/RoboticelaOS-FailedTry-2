/*
 * RoboWindow architecture suite (Goal 7.1).
 * Headless — mock / null backend only; no OS window.
 */

#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

typedef struct mock_backend_data {
    int init_count;
    int shutdown_count;
    int create_count;
    int destroy_count;
    int fail_create;
    int fail_init;
    uint64_t last_native_token;
} mock_backend_data_t;

static mock_backend_data_t g_mock;

static robo_result_t mock_initialize(void *userdata)
{
    mock_backend_data_t *m = (mock_backend_data_t *)userdata;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (m->fail_init) {
        return ROBO_INIT_ERROR;
    }
    m->init_count++;
    return ROBO_OK;
}

static void mock_shutdown(void *userdata)
{
    mock_backend_data_t *m = (mock_backend_data_t *)userdata;
    if (m) {
        m->shutdown_count++;
    }
}

static robo_result_t mock_create_window(void *userdata,
                                        robo_window_id_t id,
                                        const robo_window_config_t *cfg,
                                        robo_window_native_handle_t *out_native)
{
    mock_backend_data_t *m = (mock_backend_data_t *)userdata;
    (void)id;
    (void)cfg;
    if (!m || !out_native) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (m->fail_create) {
        return ROBO_ERROR;
    }
    m->create_count++;
    m->last_native_token = (uint64_t)0x4D4F434Bu; /* "MOCK" */
    return robo_window_native_handle_assign(out_native, m->last_native_token, 1u);
}

static robo_result_t mock_destroy_window(void *userdata,
                                         robo_window_id_t id,
                                         robo_window_native_handle_t *native)
{
    mock_backend_data_t *m = (mock_backend_data_t *)userdata;
    (void)id;
    if (m) {
        m->destroy_count++;
    }
    if (native) {
        robo_window_native_handle_invalidate(native);
    }
    return ROBO_OK;
}

static robo_result_t mock_show(void *userdata,
                               robo_window_id_t id,
                               robo_window_native_handle_t *native)
{
    (void)userdata;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t mock_hide(void *userdata,
                               robo_window_id_t id,
                               robo_window_native_handle_t *native)
{
    (void)userdata;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t mock_set_title(void *userdata,
                                    robo_window_id_t id,
                                    robo_window_native_handle_t *native,
                                    const char *utf8_title)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)utf8_title;
    return ROBO_OK;
}

static robo_result_t mock_set_size(void *userdata,
                                   robo_window_id_t id,
                                   robo_window_native_handle_t *native,
                                   int32_t width,
                                   int32_t height)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)width;
    (void)height;
    return ROBO_OK;
}

static robo_result_t mock_set_position(void *userdata,
                                       robo_window_id_t id,
                                       robo_window_native_handle_t *native,
                                       int32_t x,
                                       int32_t y)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)x;
    (void)y;
    return ROBO_OK;
}

static robo_result_t mock_focus(void *userdata,
                                robo_window_id_t id,
                                robo_window_native_handle_t *native)
{
    (void)userdata;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t mock_present(void *userdata,
                                  robo_window_id_t id,
                                  robo_window_native_handle_t *native,
                                  robo_gfx_surface_t *surface)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)surface;
    return ROBO_NOT_SUPPORTED;
}

static robo_result_t mock_process_events(void *userdata)
{
    (void)userdata;
    return ROBO_OK;
}

static const robo_window_backend_caps_t g_mock_caps = {
    1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0u, 0u, 0u};

static const robo_window_backend_vtbl_t g_mock_vtbl = {
    "mock",
    &g_mock_caps,
    mock_initialize,
    mock_shutdown,
    mock_create_window,
    mock_destroy_window,
    mock_show,
    mock_hide,
    mock_set_title,
    mock_set_size,
    mock_set_position,
    mock_focus,
    NULL,
    mock_present,
    mock_process_events};

static void drain_events(void)
{
    robo_window_event_t ev;
    while (robo_window_event_poll(&ev) == ROBO_OK) {
    }
}

static void fixture_start(void)
{
    memset(&g_mock, 0, sizeof(g_mock));
    robo_window_shutdown();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_mock_vtbl, &g_mock),
                      ROBO_OK);
    drain_events();
}

static void fixture_end(void)
{
    robo_window_shutdown();
}

static void test_window_version_init(void)
{
    ROBO_CHECK_STR_EQ(robo_window_version_string(), "0.5.0");
    ROBO_CHECK_EQ_INT(ROBO_WINDOW_API_FROZEN, 0);
    robo_window_shutdown();
    ROBO_CHECK(!robo_window_is_initialized());
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK(robo_window_is_initialized());
    robo_window_shutdown();
    ROBO_CHECK(!robo_window_is_initialized());
}

static void test_window_backend_contract(void)
{
    fixture_start();
    ROBO_CHECK(robo_window_backend_is_registered());
    ROBO_CHECK_STR_EQ(robo_window_backend_name(), "mock");
    ROBO_CHECK_EQ_INT(g_mock.init_count, 1);

    ROBO_CHECK_EQ_INT(robo_window_backend_register(NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    g_mock.fail_init = 1;
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_mock_vtbl, &g_mock),
                      ROBO_INIT_ERROR);
    /* previous backend was cleared before failed init */
    ROBO_CHECK(!robo_window_backend_is_registered());

    g_mock.fail_init = 0;
    ROBO_CHECK_EQ_INT(robo_window_backend_use_null(), ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_window_backend_name(), "null");
    fixture_end();
}

static void test_window_no_backend(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id = 99u;

    robo_window_shutdown();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    robo_window_config_init(&cfg);
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)id, (int)ROBO_WINDOW_ID_NONE);
    fixture_end();
}

static void test_window_identity(void)
{
    robo_window_config_t cfg;
    robo_window_id_t a = ROBO_WINDOW_ID_NONE;
    robo_window_id_t b = ROBO_WINDOW_ID_NONE;

    fixture_start();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &b), ROBO_OK);
    ROBO_CHECK(a != ROBO_WINDOW_ID_NONE);
    ROBO_CHECK(b != ROBO_WINDOW_ID_NONE);
    ROBO_CHECK(a != b);
    ROBO_CHECK(robo_window_id_is_live(a));
    ROBO_CHECK_EQ_INT(robo_window_destroy(a), ROBO_OK);
    ROBO_CHECK(!robo_window_id_is_live(a));
    ROBO_CHECK_EQ_INT(robo_window_destroy(a), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_get_state(a, NULL), ROBO_INVALID_ARGUMENT);
    {
        robo_window_state_t st;
        ROBO_CHECK_EQ_INT(robo_window_get_state(a, &st), ROBO_NOT_FOUND);
    }
    ROBO_CHECK_EQ_INT(robo_window_destroy(b), ROBO_OK);
    fixture_end();
}

static void test_window_lifecycle(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_state_t st;
    robo_window_event_t ev;

    fixture_start();
    ROBO_CHECK_EQ_INT(
        robo_window_lifecycle_check_transition(ROBO_WINDOW_LIFECYCLE_CREATED,
                                               ROBO_WINDOW_LIFECYCLE_CREATED),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_lifecycle_check_transition(
                          ROBO_WINDOW_LIFECYCLE_CLOSED,
                          ROBO_WINDOW_LIFECYCLE_CREATED),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_lifecycle_check_transition(
                          ROBO_WINDOW_LIFECYCLE_CLOSED,
                          ROBO_WINDOW_LIFECYCLE_CLOSED),
                      ROBO_OK);

    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_CREATED);
    ROBO_CHECK_EQ_INT((int)ev.window_id, (int)id);

    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)st.lifecycle, (int)ROBO_WINDOW_LIFECYCLE_CREATED);
    ROBO_CHECK_EQ_INT(st.visible, 0);

    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_SHOWN);
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.visible, 1);

    ROBO_CHECK_EQ_INT(robo_window_hide(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_HIDDEN);

    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_CLOSE_REQUESTED);
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)st.lifecycle, (int)ROBO_WINDOW_LIFECYCLE_CLOSING);
    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_CLOSED);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_NOT_FOUND);
    fixture_end();
}

static void test_window_config(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    const char *title = NULL;
    robo_window_state_t st;

    fixture_start();
    robo_window_config_init(&cfg);
    ROBO_CHECK_EQ_INT(cfg.width, 800);
    ROBO_CHECK_EQ_INT(cfg.height, 600);
    ROBO_CHECK_EQ_INT(cfg.visible, 1);
    ROBO_CHECK_EQ_INT(cfg.resizable, 1);

    cfg.width = 0;
    ROBO_CHECK_EQ_INT(robo_window_config_validate(&cfg), ROBO_INVALID_ARGUMENT);
    cfg.width = 100;
    cfg.height = 50;
    cfg.min_width = 200;
    ROBO_CHECK_EQ_INT(robo_window_config_validate(&cfg), ROBO_INVALID_ARGUMENT);
    cfg.min_width = 10;
    cfg.max_width = 5;
    ROBO_CHECK_EQ_INT(robo_window_config_validate(&cfg), ROBO_INVALID_ARGUMENT);

    robo_window_config_init(&cfg);
    cfg.width = 320;
    cfg.height = 240;
    cfg.min_width = 100;
    cfg.min_height = 80;
    cfg.max_width = 1920;
    cfg.max_height = 1080;
    cfg.title = "Roboticela";
    cfg.visible = 1;
    cfg.fullscreen = 0;
    cfg.resizable = 1;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_title(id, &title), ROBO_OK);
    ROBO_CHECK_STR_EQ(title, "Roboticela");
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.width, 320);
    ROBO_CHECK_EQ_INT(st.height, 240);
    ROBO_CHECK_EQ_INT(st.visible, 1);
    ROBO_CHECK_EQ_INT(st.requested_width, 320);
    ROBO_CHECK_EQ_INT(robo_window_set_title(id, "Runtime"), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_title(id, &title), ROBO_OK);
    ROBO_CHECK_STR_EQ(title, "Runtime");
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
}

static void test_window_display(void)
{
    robo_display_info_t info;

    fixture_start();
    ROBO_CHECK_EQ_INT((int)robo_display_count(), 1);
    ROBO_CHECK_EQ_INT(robo_display_primary(&info), ROBO_OK);
    ROBO_CHECK(info.id != ROBO_DISPLAY_ID_NONE);
    ROBO_CHECK_EQ_INT(info.bounds.width, 1280);
    ROBO_CHECK_EQ_INT(info.bounds.height, 800);
    ROBO_CHECK_EQ_INT(info.work_area.width, 1280);
    ROBO_CHECK_EQ_INT(info.is_primary, 1);
    ROBO_CHECK(info.scale > 0.9f && info.scale < 1.1f);
    ROBO_CHECK_EQ_INT(info.physical_width_mm, 0);
    ROBO_CHECK_EQ_INT(robo_display_get(info.id, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_get(ROBO_DISPLAY_ID_NONE, &info),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_display_at(1u, &info), ROBO_NOT_FOUND);
    fixture_end();
}

static void test_window_events(void)
{
    robo_window_event_t ev;
    robo_window_event_t out;

    fixture_start();
    robo_window_event_init(&ev);
    ROBO_CHECK_EQ_INT(robo_window_event_push(&ev), ROBO_INVALID_ARGUMENT);
    ev.type = ROBO_WINDOW_EVENT_FOCUS_GAINED;
    ev.window_id = (robo_window_id_t)7u;
    ROBO_CHECK_EQ_INT(robo_window_event_push(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_window_event_queue_size(), 1);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out.type, (int)ROBO_WINDOW_EVENT_FOCUS_GAINED);
    ROBO_CHECK_EQ_INT((int)out.window_id, 7);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&out), ROBO_NOT_FOUND);
    ROBO_CHECK(robo_window_event_type_is_known(ROBO_WINDOW_EVENT_RESIZED));
    ROBO_CHECK(!robo_window_event_type_is_known(ROBO_WINDOW_EVENT_NONE));
    fixture_end();
}

static void test_window_present_contract(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surf = NULL;

    fixture_start();
    ROBO_CHECK_EQ_INT(robo_window_backend_use_null(), ROBO_OK);
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);

    robo_gfx_surface_desc_init(&desc);
    desc.width = 8;
    desc.height = 8;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_present_surface(id, surf), ROBO_OK);
    ROBO_CHECK(robo_window_get_present_surface(id) == surf);
    ROBO_CHECK_EQ_INT(robo_window_present(id, NULL), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_window_set_present_surface(id, NULL), ROBO_OK);
    ROBO_CHECK(robo_window_get_present_surface(id) == NULL);
    robo_gfx_surface_release(surf);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
}

static void test_window_mock_failure(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id = 1u;

    fixture_start();
    g_mock.fail_create = 1;
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_ERROR);
    ROBO_CHECK_EQ_INT((int)id, (int)ROBO_WINDOW_ID_NONE);
    ROBO_CHECK_EQ_INT((int)robo_window_live_count(), 0);
    fixture_end();
}

static void test_window_native_opaque(void)
{
    robo_window_native_handle_t h;

    robo_window_native_handle_clear(&h);
    ROBO_CHECK(robo_window_native_handle_is_empty(&h));
    ROBO_CHECK_EQ_INT(robo_window_native_handle_assign(&h, 1u, 1u), ROBO_OK);
    ROBO_CHECK(!robo_window_native_handle_is_empty(&h));
    robo_window_native_handle_invalidate(&h);
    ROBO_CHECK(robo_window_native_handle_is_empty(&h));
}

static void test_window_size_position(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_state_t st;
    robo_window_event_t ev;

    fixture_start();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain_events();
    ROBO_CHECK_EQ_INT(robo_window_request_size(id, 640, 480), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_RESIZED);
    ROBO_CHECK_EQ_INT(ev.width, 640);
    ROBO_CHECK_EQ_INT(robo_window_request_position(id, 10, 20), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.x, 10);
    ROBO_CHECK_EQ_INT(st.y, 20);
    ROBO_CHECK_EQ_INT(robo_window_request_size(id, 0, 10), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
}

void robo_test_register_window(void)
{
    robo_test_register("window.version", test_window_version_init);
    robo_test_register("window.backend", test_window_backend_contract);
    robo_test_register("window.no_backend", test_window_no_backend);
    robo_test_register("window.identity", test_window_identity);
    robo_test_register("window.lifecycle", test_window_lifecycle);
    robo_test_register("window.config", test_window_config);
    robo_test_register("window.display", test_window_display);
    robo_test_register("window.events", test_window_events);
    robo_test_register("window.present", test_window_present_contract);
    robo_test_register("window.mock_fail", test_window_mock_failure);
    robo_test_register("window.native", test_window_native_opaque);
    robo_test_register("window.geom", test_window_size_position);
}
