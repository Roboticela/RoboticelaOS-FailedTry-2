/*
 * RoboWindow native-abstraction suite (Goal 7.2).
 * Headless mock backend — not a real OS window.
 */

#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

typedef struct rich_mock {
    int init_count;
    int shutdown_count;
    int create_count;
    int destroy_count;
    int show_count;
    int hide_count;
    int title_count;
    int size_count;
    int pos_count;
    int focus_count;
    int fail_destroy;
    int fail_show;
    uint32_t next_gen;
    char last_title[ROBO_WINDOW_TITLE_MAX];
    int32_t actual_w;
    int32_t actual_h;
    int32_t actual_x;
    int32_t actual_y;
    int visible;
    int focused;
} rich_mock_t;

static rich_mock_t g_rm;
static robo_window_backend_caps_t g_rich_caps;

static robo_result_t rm_init(void *userdata)
{
    rich_mock_t *m = (rich_mock_t *)userdata;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->init_count++;
    m->next_gen = 1u;
    return ROBO_OK;
}

static void rm_shutdown(void *userdata)
{
    rich_mock_t *m = (rich_mock_t *)userdata;
    if (m) {
        m->shutdown_count++;
    }
}

static robo_result_t rm_create(void *userdata,
                               robo_window_id_t id,
                               const robo_window_config_t *cfg,
                               robo_window_native_handle_t *out_native)
{
    rich_mock_t *m = (rich_mock_t *)userdata;
    uint32_t gen;
    (void)id;
    if (!m || !cfg || !out_native) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->create_count++;
    gen = m->next_gen++;
    m->actual_w = cfg->width;
    m->actual_h = cfg->height;
    m->actual_x = 0;
    m->actual_y = 0;
    m->visible = 0;
    m->focused = 0;
    if (cfg->title) {
        size_t i;
        for (i = 0u; i + 1u < sizeof(m->last_title) && cfg->title[i]; i++) {
            m->last_title[i] = cfg->title[i];
        }
        m->last_title[i] = '\0';
    } else {
        m->last_title[0] = '\0';
    }
    return robo_window_native_handle_assign(out_native, 0x52494348ull /* RICH */,
                                            gen);
}

static robo_result_t rm_destroy(void *userdata,
                                robo_window_id_t id,
                                robo_window_native_handle_t *native)
{
    rich_mock_t *m = (rich_mock_t *)userdata;
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

static robo_result_t rm_show(void *userdata,
                             robo_window_id_t id,
                             robo_window_native_handle_t *native)
{
    rich_mock_t *m = (rich_mock_t *)userdata;
    (void)id;
    (void)native;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (m->fail_show) {
        return ROBO_ERROR;
    }
    m->show_count++;
    m->visible = 1;
    return ROBO_OK;
}

static robo_result_t rm_hide(void *userdata,
                             robo_window_id_t id,
                             robo_window_native_handle_t *native)
{
    rich_mock_t *m = (rich_mock_t *)userdata;
    (void)id;
    (void)native;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->hide_count++;
    m->visible = 0;
    return ROBO_OK;
}

static robo_result_t rm_title(void *userdata,
                              robo_window_id_t id,
                              robo_window_native_handle_t *native,
                              const char *utf8_title)
{
    rich_mock_t *m = (rich_mock_t *)userdata;
    size_t i;
    (void)id;
    (void)native;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->title_count++;
    if (!utf8_title) {
        m->last_title[0] = '\0';
        return ROBO_OK;
    }
    for (i = 0u; i + 1u < sizeof(m->last_title) && utf8_title[i]; i++) {
        m->last_title[i] = utf8_title[i];
    }
    m->last_title[i] = '\0';
    return ROBO_OK;
}

static robo_result_t rm_size(void *userdata,
                             robo_window_id_t id,
                             robo_window_native_handle_t *native,
                             int32_t width,
                             int32_t height)
{
    rich_mock_t *m = (rich_mock_t *)userdata;
    (void)id;
    (void)native;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->size_count++;
    /* Report actual as request + 1 to exercise state sync. */
    m->actual_w = width + 1;
    m->actual_h = height + 1;
    return ROBO_OK;
}

static robo_result_t rm_pos(void *userdata,
                            robo_window_id_t id,
                            robo_window_native_handle_t *native,
                            int32_t x,
                            int32_t y)
{
    rich_mock_t *m = (rich_mock_t *)userdata;
    (void)id;
    (void)native;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->pos_count++;
    m->actual_x = x;
    m->actual_y = y;
    return ROBO_OK;
}

static robo_result_t rm_focus(void *userdata,
                              robo_window_id_t id,
                              robo_window_native_handle_t *native)
{
    rich_mock_t *m = (rich_mock_t *)userdata;
    (void)id;
    (void)native;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->focus_count++;
    m->focused = 1;
    return ROBO_OK;
}

static robo_result_t rm_query(void *userdata,
                              robo_window_id_t id,
                              const robo_window_native_handle_t *native,
                              robo_window_state_t *inout_state)
{
    rich_mock_t *m = (rich_mock_t *)userdata;
    (void)id;
    (void)native;
    if (!m || !inout_state) {
        return ROBO_INVALID_ARGUMENT;
    }
    inout_state->width = m->actual_w;
    inout_state->height = m->actual_h;
    inout_state->x = m->actual_x;
    inout_state->y = m->actual_y;
    inout_state->visible = m->visible;
    inout_state->focused = m->focused;
    return ROBO_OK;
}

static robo_result_t rm_present(void *userdata,
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

static robo_result_t rm_events(void *userdata)
{
    (void)userdata;
    return ROBO_OK;
}

static const robo_window_backend_vtbl_t g_rich_vtbl = {
    "rich_mock",
    &g_rich_caps,
    rm_init,
    rm_shutdown,
    rm_create,
    rm_destroy,
    rm_show,
    rm_hide,
    rm_title,
    rm_size,
    rm_pos,
    rm_focus,
    rm_query,
    rm_present,
    rm_events};

static void rich_caps_full(void)
{
    memset(&g_rich_caps, 0, sizeof(g_rich_caps));
    g_rich_caps.create_window = 1;
    g_rich_caps.destroy_window = 1;
    g_rich_caps.show_hide = 1;
    g_rich_caps.resize = 1;
    g_rich_caps.move = 1;
    g_rich_caps.set_title = 1;
    g_rich_caps.focus = 1;
    g_rich_caps.query_state = 1;
    g_rich_caps.present = 0;
    g_rich_caps.process_events = 1;
    g_rich_caps.reports_actual_state = 1;
}

static void drain(void)
{
    robo_window_event_t ev;
    while (robo_window_event_poll(&ev) == ROBO_OK) {
    }
}

static void fixture(void)
{
    memset(&g_rm, 0, sizeof(g_rm));
    rich_caps_full();
    robo_window_shutdown();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_rich_vtbl, &g_rm), ROBO_OK);
    drain();
}

static void test_native_handle_ops(void)
{
    robo_window_native_handle_t a;
    robo_window_native_handle_t b;

    robo_window_native_handle_clear(&a);
    ROBO_CHECK(robo_window_native_handle_is_empty(&a));
    ROBO_CHECK(!robo_window_native_handle_is_valid(&a));
    ROBO_CHECK_EQ_INT(robo_window_native_handle_assign(&a, 0u, 1u),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_native_handle_assign(&a, 42u, 0u),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_native_handle_assign(&a, 42u, 3u), ROBO_OK);
    ROBO_CHECK(robo_window_native_handle_is_valid(&a));
    b = a;
    ROBO_CHECK(robo_window_native_handle_equal(&a, &b));
    robo_window_native_handle_invalidate(&a);
    ROBO_CHECK(!robo_window_native_handle_equal(&a, &b));
    ROBO_CHECK(robo_window_native_handle_is_valid(&b));
}

static void test_native_create_handle(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_native_handle_t h;
    robo_window_native_handle_t stale;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    cfg.title = "Hello";
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_OK);
    ROBO_CHECK(robo_window_native_handle_is_valid(&h));
    stale = h;
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_NOT_FOUND);
    ROBO_CHECK(robo_window_native_handle_is_valid(&stale)); /* stale copy */
    ROBO_CHECK_EQ_INT(g_rm.destroy_count, 1);
    robo_window_shutdown();
}

static void test_native_title_unicode(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    const char *title = NULL;
    /* "café" in UTF-8 */
    static const char cafe[] = "caf\xC3\xA9";
    static const char bad[] = "\xFF\xFE";

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    cfg.title = "";
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_title(id, cafe), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_title(id, &title), ROBO_OK);
    ROBO_CHECK_STR_EQ(title, cafe);
    ROBO_CHECK_STR_EQ(g_rm.last_title, cafe);
    ROBO_CHECK_EQ_INT(robo_window_set_title(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_title(id, &title), ROBO_OK);
    ROBO_CHECK_STR_EQ(title, "");
    ROBO_CHECK_EQ_INT(robo_window_set_title(id, bad), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_native_size_sync(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_state_t st;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    cfg.width = 200;
    cfg.height = 100;
    cfg.min_width = 50;
    cfg.max_width = 400;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_request_size(id, 10, 100),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_request_size(id, 500, 100),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_request_size(id, 100, 80), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.requested_width, 100);
    ROBO_CHECK_EQ_INT(st.requested_height, 80);
    /* Mock reports actual = request + 1 */
    ROBO_CHECK_EQ_INT(st.width, 101);
    ROBO_CHECK_EQ_INT(st.height, 81);
    ROBO_CHECK_EQ_INT(robo_window_request_position(id, -10, 20), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.requested_x, -10);
    ROBO_CHECK_EQ_INT(st.x, -10);
    ROBO_CHECK_EQ_INT(st.y, 20);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_native_focus_caps(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_backend_caps_t caps;
    robo_window_state_t st;
    robo_window_event_t ev;

    fixture();
    ROBO_CHECK_EQ_INT(robo_window_backend_capabilities(&caps), ROBO_OK);
    ROBO_CHECK_EQ_INT(caps.focus, 1);
    ROBO_CHECK_EQ_INT(caps.present, 0);
    ROBO_CHECK_EQ_INT(caps.reports_actual_state, 1);

    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain();
    ROBO_CHECK_EQ_INT(robo_window_request_focus(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_FOCUS_GAINED);
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.focused, 1);
    ROBO_CHECK_EQ_INT(g_rm.focus_count, 1);

    /* Null backend: focus unsupported */
    ROBO_CHECK_EQ_INT(robo_window_backend_use_null(), ROBO_OK);
    {
        robo_window_id_t id2;
        robo_window_config_init(&cfg);
        cfg.visible = 0;
        ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id2), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_window_request_focus(id2), ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(robo_window_backend_capabilities(&caps), ROBO_OK);
        ROBO_CHECK_EQ_INT(caps.focus, 0);
        ROBO_CHECK_EQ_INT(robo_window_destroy(id2), ROBO_OK);
    }
    robo_window_shutdown();
}

static void test_native_destroy_fail(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    g_rm.fail_destroy = 1;
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_ERROR);
    ROBO_CHECK(robo_window_id_is_live(id));
    g_rm.fail_destroy = 0;
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_NOT_FOUND);
    robo_window_shutdown();
}

static void test_native_visibility(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK); /* idempotent */
    ROBO_CHECK_EQ_INT(g_rm.show_count, 2);
    ROBO_CHECK_EQ_INT(robo_window_hide(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_rm.hide_count, 1);
    g_rm.fail_show = 1;
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_ERROR);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_native_present_boundary(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surf = NULL;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, NULL), ROBO_INVALID_ARGUMENT);
    robo_gfx_surface_desc_init(&desc);
    desc.width = 4;
    desc.height = 4;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_present_surface(id, surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, NULL), ROBO_NOT_SUPPORTED);
    robo_gfx_surface_release(surf);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);

    robo_window_shutdown();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    {
        robo_window_backend_caps_t caps;
        ROBO_CHECK_EQ_INT(robo_window_backend_capabilities(&caps),
                          ROBO_NOT_SUPPORTED);
    }
    robo_window_shutdown();
}

static void test_native_threading(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;

    fixture();
    ROBO_CHECK_EQ_INT(robo_window_thread_bind_owner(1u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_thread_enter(1u), ROBO_OK);
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_thread_enter(2u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_PERMISSION_DENIED);
    ROBO_CHECK_EQ_INT(robo_window_thread_enter(1u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_thread_bind_owner(ROBO_WINDOW_THREAD_NONE),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_thread_enter(99u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_hide(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_native_caps_without_backend(void)
{
    robo_window_backend_caps_t caps;

    robo_window_shutdown();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_capabilities(&caps),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(caps.create_window, 0);
    robo_window_shutdown();
}

void robo_test_register_window_native(void)
{
    robo_test_register("window.native.handle", test_native_handle_ops);
    robo_test_register("window.native.create", test_native_create_handle);
    robo_test_register("window.native.title", test_native_title_unicode);
    robo_test_register("window.native.size_sync", test_native_size_sync);
    robo_test_register("window.native.focus_caps", test_native_focus_caps);
    robo_test_register("window.native.destroy_fail", test_native_destroy_fail);
    robo_test_register("window.native.visibility", test_native_visibility);
    robo_test_register("window.native.present", test_native_present_boundary);
    robo_test_register("window.native.thread", test_native_threading);
    robo_test_register("window.native.caps_none", test_native_caps_without_backend);
}
