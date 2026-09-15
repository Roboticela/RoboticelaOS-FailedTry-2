/*
 * RoboWindow sizing suite (Goal 7.6).
 * Deterministic mock backends — headless-safe.
 */

#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

typedef struct size_mock {
    int init_count;
    int fail_resize;
    int clamp_mode; /* 0 = actual==request; 1 = +2; 2 = clamp to [min,max] stored */
    int32_t actual_w;
    int32_t actual_h;
    int32_t min_w;
    int32_t min_h;
    int32_t max_w;
    int32_t max_h;
    int constraints_count;
    int size_count;
} size_mock_t;

static size_mock_t g_sm;
static robo_window_backend_caps_t g_sm_caps;
static robo_window_backend_vtbl_t g_sm_vtbl;

static robo_result_t sm_init(void *ud)
{
    size_mock_t *m = (size_mock_t *)ud;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->init_count++;
    return ROBO_OK;
}

static void sm_shutdown(void *ud)
{
    (void)ud;
}

static robo_result_t sm_create(void *ud,
                               robo_window_id_t id,
                               const robo_window_config_t *cfg,
                               robo_window_native_handle_t *out)
{
    size_mock_t *m = (size_mock_t *)ud;
    (void)id;
    if (!m || !cfg || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->actual_w = cfg->width;
    m->actual_h = cfg->height;
    m->min_w = cfg->min_width;
    m->min_h = cfg->min_height;
    m->max_w = cfg->max_width;
    m->max_h = cfg->max_height;
    return robo_window_native_handle_assign(out, 0x53495A45ull, 1u);
}

static robo_result_t sm_destroy(void *ud,
                                robo_window_id_t id,
                                robo_window_native_handle_t *native)
{
    (void)ud;
    (void)id;
    if (native) {
        robo_window_native_handle_invalidate(native);
    }
    return ROBO_OK;
}

static robo_result_t sm_show(void *ud, robo_window_id_t id,
                             robo_window_native_handle_t *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return ROBO_OK;
}

static robo_result_t sm_hide(void *ud, robo_window_id_t id,
                             robo_window_native_handle_t *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return ROBO_OK;
}

static robo_result_t sm_title(void *ud, robo_window_id_t id,
                              robo_window_native_handle_t *n, const char *t)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)t;
    return ROBO_OK;
}

static robo_result_t sm_size(void *ud,
                             robo_window_id_t id,
                             robo_window_native_handle_t *n,
                             int32_t width,
                             int32_t height)
{
    size_mock_t *m = (size_mock_t *)ud;
    (void)id;
    (void)n;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (m->fail_resize) {
        return ROBO_IO_ERROR;
    }
    m->size_count++;
    if (m->clamp_mode == 1) {
        m->actual_w = width + 2;
        m->actual_h = height + 2;
    } else if (m->clamp_mode == 2) {
        m->actual_w = width;
        m->actual_h = height;
        if (m->min_w > 0 && m->actual_w < m->min_w) {
            m->actual_w = m->min_w;
        }
        if (m->min_h > 0 && m->actual_h < m->min_h) {
            m->actual_h = m->min_h;
        }
        if (m->max_w > 0 && m->actual_w > m->max_w) {
            m->actual_w = m->max_w;
        }
        if (m->max_h > 0 && m->actual_h > m->max_h) {
            m->actual_h = m->max_h;
        }
    } else {
        m->actual_w = width;
        m->actual_h = height;
    }
    return ROBO_OK;
}

static robo_result_t sm_constraints(void *ud,
                                    robo_window_id_t id,
                                    robo_window_native_handle_t *n,
                                    int32_t min_w,
                                    int32_t min_h,
                                    int32_t max_w,
                                    int32_t max_h)
{
    size_mock_t *m = (size_mock_t *)ud;
    (void)id;
    (void)n;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->constraints_count++;
    m->min_w = min_w;
    m->min_h = min_h;
    m->max_w = max_w;
    m->max_h = max_h;
    return ROBO_OK;
}

static robo_result_t sm_pos(void *ud, robo_window_id_t id,
                            robo_window_native_handle_t *n, int32_t x, int32_t y)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)x;
    (void)y;
    return ROBO_OK;
}

static robo_result_t sm_focus(void *ud, robo_window_id_t id,
                              robo_window_native_handle_t *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return ROBO_NOT_SUPPORTED;
}

static robo_result_t sm_query(void *ud,
                              robo_window_id_t id,
                              const robo_window_native_handle_t *n,
                              robo_window_state_t *st)
{
    size_mock_t *m = (size_mock_t *)ud;
    (void)id;
    (void)n;
    if (!m || !st) {
        return ROBO_INVALID_ARGUMENT;
    }
    st->width = m->actual_w;
    st->height = m->actual_h;
    return ROBO_OK;
}

static robo_result_t sm_present(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *n,
                                robo_gfx_surface_t *s)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)s;
    return ROBO_NOT_SUPPORTED;
}

static robo_result_t sm_events(void *ud)
{
    (void)ud;
    return ROBO_OK;
}

static void sm_caps(void)
{
    memset(&g_sm_caps, 0, sizeof(g_sm_caps));
    g_sm_caps.create_window = 1;
    g_sm_caps.destroy_window = 1;
    g_sm_caps.show_hide = 1;
    g_sm_caps.resize = 1;
    g_sm_caps.move = 1;
    g_sm_caps.set_title = 1;
    g_sm_caps.query_state = 1;
    g_sm_caps.process_events = 1;
    g_sm_caps.reports_actual_state = 1;
    g_sm_caps.size_constraints = 1;
}

static void sm_bind(void)
{
    memset(&g_sm_vtbl, 0, sizeof(g_sm_vtbl));
    g_sm_vtbl.name = "size_mock";
    g_sm_vtbl.capabilities = &g_sm_caps;
    g_sm_vtbl.initialize = sm_init;
    g_sm_vtbl.shutdown = sm_shutdown;
    g_sm_vtbl.create_window = sm_create;
    g_sm_vtbl.destroy_window = sm_destroy;
    g_sm_vtbl.show = sm_show;
    g_sm_vtbl.hide = sm_hide;
    g_sm_vtbl.set_title = sm_title;
    g_sm_vtbl.set_size = sm_size;
    g_sm_vtbl.set_size_constraints = sm_constraints;
    g_sm_vtbl.set_position = sm_pos;
    g_sm_vtbl.focus = sm_focus;
    g_sm_vtbl.query_state = sm_query;
    g_sm_vtbl.present_surface = sm_present;
    g_sm_vtbl.process_events = sm_events;
}

static void fixture(int clamp_mode)
{
    memset(&g_sm, 0, sizeof(g_sm));
    g_sm.clamp_mode = clamp_mode;
    sm_caps();
    sm_bind();
    robo_window_shutdown();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_sm_vtbl, &g_sm), ROBO_OK);
}

static void test_size_helpers(void)
{
    robo_window_size_constraints_t c;

    ROBO_CHECK_EQ_INT(robo_window_size_validate(0, 10), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_size_validate(-1, 10), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_size_validate(10, 0), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_size_validate(ROBO_WINDOW_MAX_DIM + 1, 10),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_size_validate(100, 80), ROBO_OK);

    robo_window_size_constraints_init(&c);
    ROBO_CHECK_EQ_INT(robo_window_size_constraints_validate(&c), ROBO_OK);
    c.min_width = 100;
    c.max_width = 50;
    ROBO_CHECK_EQ_INT(robo_window_size_constraints_validate(&c),
                      ROBO_INVALID_ARGUMENT);
    c.max_width = 100;
    ROBO_CHECK_EQ_INT(robo_window_size_constraints_validate(&c), ROBO_OK);
    c.min_width = -1;
    ROBO_CHECK_EQ_INT(robo_window_size_constraints_validate(&c),
                      ROBO_INVALID_ARGUMENT);

    robo_window_size_constraints_init(&c);
    c.min_width = 200;
    c.min_height = 100;
    c.max_width = 800;
    c.max_height = 600;
    ROBO_CHECK_EQ_INT(
        robo_window_size_validate_against_constraints(100, 200, &c),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_window_size_validate_against_constraints(200, 100, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_window_size_validate_against_constraints(900, 100, &c),
        ROBO_INVALID_ARGUMENT);
}

static void test_initial_size(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_size_t sz;
    robo_window_size_constraints_t c;

    fixture(0);
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    cfg.width = 640;
    cfg.height = 480;
    cfg.min_width = 100;
    cfg.min_height = 80;
    cfg.max_width = 1200;
    cfg.max_height = 900;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &sz), ROBO_OK);
    ROBO_CHECK_EQ_INT(sz.width, 640);
    ROBO_CHECK_EQ_INT(sz.height, 480);
    ROBO_CHECK_EQ_INT(robo_window_get_requested_size(id, &sz), ROBO_OK);
    ROBO_CHECK_EQ_INT(sz.width, 640);
    ROBO_CHECK_EQ_INT(robo_window_get_size_constraints(id, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.min_width, 100);
    ROBO_CHECK_EQ_INT(c.max_height, 900);

    cfg.width = 50; /* below min */
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_INVALID_ARGUMENT);
    cfg.width = 640;
    cfg.min_width = 900;
    cfg.max_width = 800;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_INVALID_ARGUMENT);
    robo_window_shutdown();
}

static void test_runtime_resize(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_size_t sz;
    robo_window_state_t st;
    int i;

    fixture(0);
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    cfg.width = 400;
    cfg.height = 300;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 500, 400), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &sz), ROBO_OK);
    ROBO_CHECK_EQ_INT(sz.width, 500);
    ROBO_CHECK_EQ_INT(sz.height, 400);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 500, 400), ROBO_OK); /* same */

    for (i = 0; i < 5; i++) {
        ROBO_CHECK_EQ_INT(robo_window_request_size(id, 410 + i, 310 + i),
                          ROBO_OK);
    }

    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 0, 100), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, -5, 100), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, ROBO_WINDOW_MAX_DIM + 1, 100),
                      ROBO_INVALID_ARGUMENT);

    g_sm.fail_resize = 1;
    ROBO_CHECK_EQ_INT(robo_window_get_requested_size(id, &sz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 450, 350), ROBO_IO_ERROR);
    ROBO_CHECK_EQ_INT(robo_window_get_requested_size(id, &sz), ROBO_OK);
    /* requested unchanged after failure */
    ROBO_CHECK_EQ_INT(sz.width, 414);
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK(st.width > 0);
    g_sm.fail_resize = 0;

    ROBO_CHECK_EQ_INT(robo_window_hide(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 320, 240), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 100, 100), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &sz), ROBO_NOT_FOUND);
    robo_window_shutdown();
}

static void test_min_max_constraints(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_window_size_constraints_t c;
    robo_gfx_size_t sz;

    fixture(0);
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    cfg.width = 400;
    cfg.height = 300;
    cfg.min_width = 200;
    cfg.min_height = 150;
    cfg.max_width = 800;
    cfg.max_height = 600;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 100, 200), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 200, 150), ROBO_OK); /* at min */
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 800, 600), ROBO_OK); /* at max */
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 900, 400), ROBO_INVALID_ARGUMENT);

    robo_window_size_constraints_init(&c);
    c.min_width = 300;
    c.min_height = 200;
    c.max_width = 700;
    c.max_height = 500;
    ROBO_CHECK_EQ_INT(robo_window_set_size_constraints(id, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_sm.constraints_count, 1);
    /* was 800x600 → corrective clamp to 700x500 */
    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &sz), ROBO_OK);
    ROBO_CHECK_EQ_INT(sz.width, 700);
    ROBO_CHECK_EQ_INT(sz.height, 500);

    c.min_width = 900;
    c.max_width = 100;
    ROBO_CHECK_EQ_INT(robo_window_set_size_constraints(id, &c),
                      ROBO_INVALID_ARGUMENT);

    /* equal min/max */
    robo_window_size_constraints_init(&c);
    c.min_width = 400;
    c.min_height = 300;
    c.max_width = 400;
    c.max_height = 300;
    ROBO_CHECK_EQ_INT(robo_window_set_size_constraints(id, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 400, 300), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 401, 300), ROBO_INVALID_ARGUMENT);

    /* clear constraints */
    robo_window_size_constraints_init(&c);
    ROBO_CHECK_EQ_INT(robo_window_set_size_constraints(id, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 50, 40), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_requested_vs_actual(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_size_t req;
    robo_gfx_size_t act;

    fixture(1); /* actual = request + 2 */
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    cfg.width = 200;
    cfg.height = 100;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 300, 200), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_requested_size(id, &req), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &act), ROBO_OK);
    ROBO_CHECK_EQ_INT(req.width, 300);
    ROBO_CHECK_EQ_INT(req.height, 200);
    ROBO_CHECK_EQ_INT(act.width, 302);
    ROBO_CHECK_EQ_INT(act.height, 202);
    ROBO_CHECK(req.width != act.width);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_observe_size(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_size_t sz;
    robo_window_event_t ev;

    fixture(0);
    /* Observe path is for OS-reported sizes without treating query as
     * authoritative overwrite in this test. */
    g_sm_caps.reports_actual_state = 0;
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    while (robo_window_event_poll(&ev) == ROBO_OK) {
    }
    ROBO_CHECK_EQ_INT(robo_window_observe_size(id, 111, 222), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &sz), ROBO_OK);
    ROBO_CHECK_EQ_INT(sz.width, 111);
    ROBO_CHECK_EQ_INT(sz.height, 222);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_RESIZED);
    /* requested unchanged */
    ROBO_CHECK_EQ_INT(robo_window_get_requested_size(id, &sz), ROBO_OK);
    ROBO_CHECK_EQ_INT(sz.width, 800);
    ROBO_CHECK_EQ_INT(robo_window_observe_size(id, 0, 10), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_size_threading(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;

    fixture(0);
    ROBO_CHECK_EQ_INT(robo_window_thread_bind_owner(7), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_thread_enter(7), ROBO_OK);
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 100, 100), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_thread_enter(8), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 120, 120),
                      ROBO_PERMISSION_DENIED);
    ROBO_CHECK_EQ_INT(robo_window_thread_enter(7), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

void robo_test_register_window_size(void)
{
    robo_test_register("window.size.helpers", test_size_helpers);
    robo_test_register("window.size.initial", test_initial_size);
    robo_test_register("window.size.runtime", test_runtime_resize);
    robo_test_register("window.size.constraints", test_min_max_constraints);
    robo_test_register("window.size.requested_actual", test_requested_vs_actual);
    robo_test_register("window.size.observe", test_observe_size);
    robo_test_register("window.size.thread", test_size_threading);
}
