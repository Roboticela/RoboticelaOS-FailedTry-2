/*
 * RoboWindow native event loop suite (Goal 7.9).
 *
 * Headless mock backend — injects lifecycle/state events without a GUI.
 */

#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

typedef enum {
    INJ_NONE = 0,
    INJ_CLOSE,
    INJ_RESIZE,
    INJ_MOVE,
    INJ_SHOW,
    INJ_HIDE,
    INJ_FOCUS_GAIN,
    INJ_FOCUS_LOST
} inj_kind_t;

typedef struct inj_item {
    inj_kind_t kind;
    robo_window_id_t id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} inj_item_t;

#define INJ_CAP 32

typedef struct loop_mock {
    int init_count;
    int fail_init;
    int fail_events;
    int fail_wait;
    int fail_wakeup;
    int wait_calls;
    int wakeup_calls;
    int process_calls;
    int nest_poll_on_process;
    volatile int wake_flag;
    size_t inj_count;
    inj_item_t inj[INJ_CAP];
} loop_mock_t;

static loop_mock_t g_lm;
static robo_window_backend_caps_t g_lm_caps;
static robo_window_backend_vtbl_t g_lm_vtbl;

static void inj_push(inj_kind_t kind, robo_window_id_t id, int32_t x, int32_t y,
                     int32_t w, int32_t h)
{
    if (g_lm.inj_count >= INJ_CAP) {
        return;
    }
    g_lm.inj[g_lm.inj_count].kind = kind;
    g_lm.inj[g_lm.inj_count].id = id;
    g_lm.inj[g_lm.inj_count].x = x;
    g_lm.inj[g_lm.inj_count].y = y;
    g_lm.inj[g_lm.inj_count].w = w;
    g_lm.inj[g_lm.inj_count].h = h;
    g_lm.inj_count++;
}

static void apply_inj(const inj_item_t *it)
{
    switch (it->kind) {
    case INJ_CLOSE:
        (void)robo_window_observe_close_request(it->id);
        break;
    case INJ_RESIZE:
        (void)robo_window_observe_size(it->id, it->w, it->h);
        break;
    case INJ_MOVE:
        (void)robo_window_observe_position(it->id, it->x, it->y);
        break;
    case INJ_SHOW:
        (void)robo_window_observe_visibility(it->id, 1);
        break;
    case INJ_HIDE:
        (void)robo_window_observe_visibility(it->id, 0);
        break;
    case INJ_FOCUS_GAIN:
        (void)robo_window_observe_focus(it->id, 1);
        break;
    case INJ_FOCUS_LOST:
        (void)robo_window_observe_focus(it->id, 0);
        break;
    default:
        break;
    }
}

static robo_result_t lm_init(void *ud)
{
    loop_mock_t *m = (loop_mock_t *)ud;
    if (m->fail_init) {
        return ROBO_INIT_ERROR;
    }
    m->init_count++;
    m->wake_flag = 0;
    return ROBO_OK;
}

static void lm_shutdown(void *ud)
{
    (void)ud;
}

static robo_result_t lm_create(void *ud, robo_window_id_t id,
                               const robo_window_config_t *cfg,
                               robo_window_native_handle_t *out)
{
    (void)ud;
    (void)id;
    (void)cfg;
    robo_window_native_handle_clear(out);
    return ROBO_OK;
}

static robo_result_t lm_destroy(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *native)
{
    (void)ud;
    (void)id;
    if (native) {
        robo_window_native_handle_invalidate(native);
    }
    return ROBO_OK;
}

static robo_result_t lm_show(void *ud, robo_window_id_t id,
                             robo_window_native_handle_t *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return ROBO_OK;
}

static robo_result_t lm_hide(void *ud, robo_window_id_t id,
                             robo_window_native_handle_t *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return ROBO_OK;
}

static robo_result_t lm_title(void *ud, robo_window_id_t id,
                              robo_window_native_handle_t *n, const char *t)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)t;
    return ROBO_OK;
}

static robo_result_t lm_size(void *ud, robo_window_id_t id,
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

static robo_result_t lm_constraints(void *ud, robo_window_id_t id,
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

static robo_result_t lm_pos(void *ud, robo_window_id_t id,
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

static robo_result_t lm_focus(void *ud, robo_window_id_t id,
                              robo_window_native_handle_t *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return ROBO_OK;
}

static robo_result_t lm_present(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *n,
                                robo_gfx_surface_t *s)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)s;
    return ROBO_NOT_SUPPORTED;
}

static robo_result_t lm_process(void *ud)
{
    loop_mock_t *m = (loop_mock_t *)ud;
    size_t i;
    m->process_calls++;
    if (m->fail_events) {
        return ROBO_IO_ERROR;
    }
    if (m->nest_poll_on_process) {
        m->nest_poll_on_process = 0;
        ROBO_CHECK_EQ_INT(robo_window_event_loop_poll(), ROBO_BUSY);
    }
    for (i = 0u; i < m->inj_count; i++) {
        apply_inj(&m->inj[i]);
    }
    m->inj_count = 0u;
    return ROBO_OK;
}

static robo_result_t lm_wait(void *ud, uint32_t timeout_ms)
{
    loop_mock_t *m = (loop_mock_t *)ud;
    uint32_t waited = 0u;
    m->wait_calls++;
    if (m->fail_wait) {
        return ROBO_IO_ERROR;
    }
    while (!m->wake_flag) {
        if (timeout_ms != ROBO_WINDOW_EVENT_WAIT_FOREVER &&
            waited >= timeout_ms) {
            break;
        }
#if defined(_WIN32)
        Sleep(1);
#else
        /* Host tests are Win32; keep a portable busy slice. */
        {
            volatile int spin = 0;
            for (; spin < 10000; spin++) {
            }
        }
#endif
        waited++;
        if (timeout_ms == 0u) {
            break;
        }
    }
    m->wake_flag = 0;
    return ROBO_OK;
}

static robo_result_t lm_wakeup(void *ud)
{
    loop_mock_t *m = (loop_mock_t *)ud;
    m->wakeup_calls++;
    if (m->fail_wakeup) {
        return ROBO_IO_ERROR;
    }
    m->wake_flag = 1;
    return ROBO_OK;
}

static void lm_prepare_vtbl(void)
{
    memset(&g_lm_caps, 0, sizeof(g_lm_caps));
    g_lm_caps.create_window = 1;
    g_lm_caps.destroy_window = 1;
    g_lm_caps.show_hide = 1;
    g_lm_caps.resize = 1;
    g_lm_caps.move = 1;
    g_lm_caps.set_title = 1;
    g_lm_caps.focus = 1;
    g_lm_caps.process_events = 1;
    g_lm_caps.size_constraints = 1;
    g_lm_caps.wait_events = 1;
    g_lm_caps.wakeup = 1;

    memset(&g_lm_vtbl, 0, sizeof(g_lm_vtbl));
    g_lm_vtbl.name = "loop_mock";
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
    g_lm_vtbl.process_events = lm_process;
    g_lm_vtbl.wait_events = lm_wait;
    g_lm_vtbl.wakeup = lm_wakeup;
}

static void drain(void)
{
    robo_window_event_t ev;
    while (robo_window_event_poll(&ev) == ROBO_OK) {
    }
}

static void fixture(void)
{
    memset(&g_lm, 0, sizeof(g_lm));
    lm_prepare_vtbl();
    robo_window_shutdown();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_lm_vtbl, &g_lm), ROBO_OK);
    drain();
}

static void fixture_end(void)
{
    robo_window_shutdown();
}

static int poll_find(robo_window_event_type_t type, robo_window_id_t id,
                     robo_window_event_t *out)
{
    robo_window_event_t ev;
    while (robo_window_event_poll(&ev) == ROBO_OK) {
        if (ev.type == type && (id == ROBO_WINDOW_ID_NONE || ev.window_id == id)) {
            if (out) {
                *out = ev;
            }
            return 1;
        }
    }
    return 0;
}

static void test_queue_basics(void)
{
    robo_window_event_t ev;
    robo_window_event_t peek;
    size_t i;

    fixture();
    ROBO_CHECK(robo_window_event_queue_is_empty());
    ROBO_CHECK_EQ_INT((int)robo_window_event_queue_size(), 0);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_event_peek(&peek), ROBO_NOT_FOUND);

    robo_window_event_init(&ev);
    ev.type = ROBO_WINDOW_EVENT_SHOWN;
    ev.window_id = (robo_window_id_t)7u;
    ROBO_CHECK_EQ_INT(robo_window_event_push(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_window_event_queue_size(), 1);
    ROBO_CHECK_EQ_INT(robo_window_event_peek(&peek), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)peek.type, (int)ROBO_WINDOW_EVENT_SHOWN);
    ROBO_CHECK_EQ_INT((int)robo_window_event_queue_size(), 1);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.window_id, 7);

    for (i = 0u; i < ROBO_WINDOW_EVENT_QUEUE_CAPACITY; i++) {
        robo_window_event_init(&ev);
        ev.type = ROBO_WINDOW_EVENT_MOVED;
        ev.window_id = (robo_window_id_t)(i + 1u);
        ROBO_CHECK_EQ_INT(robo_window_event_push(&ev), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_window_event_push(&ev), ROBO_BUSY);
    robo_window_event_queue_clear();
    ROBO_CHECK(robo_window_event_queue_is_empty());
    fixture_end();
}

static void test_queue_ordering_multi(void)
{
    robo_window_id_t a = ROBO_WINDOW_ID_NONE;
    robo_window_id_t b = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_event_t ev;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &b), ROBO_OK);
    drain();

    inj_push(INJ_MOVE, a, 1, 2, 0, 0);
    inj_push(INJ_RESIZE, a, 0, 0, 100, 80);
    inj_push(INJ_MOVE, b, 9, 8, 0, 0);
    inj_push(INJ_CLOSE, a, 0, 0, 0, 0);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_poll(), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_MOVED);
    ROBO_CHECK(ev.window_id == a);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_RESIZED);
    ROBO_CHECK(ev.window_id == a);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_MOVED);
    ROBO_CHECK(ev.window_id == b);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_CLOSE_REQUESTED);
    ROBO_CHECK(ev.window_id == a);

    ROBO_CHECK_EQ_INT(robo_window_destroy(a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(b), ROBO_OK);
    fixture_end();
}

static void test_targeting(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_id_t stale;
    robo_window_config_t cfg;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain();
    stale = id;

    ROBO_CHECK_EQ_INT(robo_window_observe_size(ROBO_WINDOW_ID_NONE, 10, 10),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_observe_size((robo_window_id_t)99999u, 10, 10),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_observe_close_request(stale), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_observe_size(stale, 20, 20), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_observe_focus(stale, 1), ROBO_NOT_FOUND);
    fixture_end();
}

static void test_close_path(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_lifecycle_t life;
    robo_window_event_t ev;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain();

    inj_push(INJ_CLOSE, id, 0, 0, 0, 0);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_poll(), ROBO_OK);
    ROBO_CHECK(poll_find(ROBO_WINDOW_EVENT_CLOSE_REQUESTED, id, &ev));
    ROBO_CHECK_EQ_INT(robo_window_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)life, (int)ROBO_WINDOW_LIFECYCLE_CLOSING);
    ROBO_CHECK(robo_window_id_is_live(id));

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK(poll_find(ROBO_WINDOW_EVENT_CLOSED, id, &ev));
    ROBO_CHECK_EQ_INT(robo_window_observe_close_request(id), ROBO_NOT_FOUND);
    fixture_end();
}

static void test_resize_move_visibility_focus(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_state_t st;
    robo_window_event_t ev;

    fixture();
    robo_window_config_init(&cfg);
    cfg.width = 200;
    cfg.height = 150;
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain();

    ROBO_CHECK_EQ_INT(robo_window_observe_size(id, 0, 10), ROBO_INVALID_ARGUMENT);

    inj_push(INJ_RESIZE, id, 0, 0, 333, 222);
    inj_push(INJ_RESIZE, id, 0, 0, 333, 222); /* repeated — no second event */
    inj_push(INJ_MOVE, id, -40, -20, 0, 0);
    inj_push(INJ_SHOW, id, 0, 0, 0, 0);
    inj_push(INJ_SHOW, id, 0, 0, 0, 0);
    inj_push(INJ_FOCUS_GAIN, id, 0, 0, 0, 0);
    inj_push(INJ_FOCUS_GAIN, id, 0, 0, 0, 0);
    inj_push(INJ_FOCUS_LOST, id, 0, 0, 0, 0);
    inj_push(INJ_HIDE, id, 0, 0, 0, 0);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_poll(), ROBO_OK);

    ROBO_CHECK(poll_find(ROBO_WINDOW_EVENT_RESIZED, id, &ev));
    ROBO_CHECK_EQ_INT(ev.width, 333);
    ROBO_CHECK_EQ_INT(ev.height, 222);
    ROBO_CHECK(poll_find(ROBO_WINDOW_EVENT_MOVED, id, &ev));
    ROBO_CHECK_EQ_INT(ev.x, -40);
    ROBO_CHECK_EQ_INT(ev.y, -20);
    ROBO_CHECK(poll_find(ROBO_WINDOW_EVENT_SHOWN, id, &ev));
    ROBO_CHECK(poll_find(ROBO_WINDOW_EVENT_FOCUS_GAINED, id, &ev));
    ROBO_CHECK(poll_find(ROBO_WINDOW_EVENT_FOCUS_LOST, id, &ev));
    ROBO_CHECK(poll_find(ROBO_WINDOW_EVENT_HIDDEN, id, &ev));

    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.width, 333);
    ROBO_CHECK_EQ_INT(st.height, 222);
    ROBO_CHECK_EQ_INT(st.x, -40);
    ROBO_CHECK_EQ_INT(st.y, -20);
    ROBO_CHECK_EQ_INT(st.visible, 0);
    ROBO_CHECK_EQ_INT(st.focused, 0);

    /* Direct observe path (no backend queue). */
    ROBO_CHECK_EQ_INT(robo_window_observe_size(id, 400, 300), ROBO_OK);
    ROBO_CHECK(poll_find(ROBO_WINDOW_EVENT_RESIZED, id, &ev));

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
}

static void test_loop_controls(void)
{
    fixture();
    ROBO_CHECK_EQ_INT(robo_window_event_loop_is_running(), 0);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_start(), ROBO_OK);
    ROBO_CHECK(robo_window_event_loop_is_running());
    ROBO_CHECK_EQ_INT(robo_window_event_loop_run_once(0), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_lm.process_calls > 0, 1);

    g_lm.process_calls = 0;
    ROBO_CHECK_EQ_INT(robo_window_event_loop_run_once(5), ROBO_OK);
    ROBO_CHECK(g_lm.wait_calls >= 1);
    ROBO_CHECK(g_lm.process_calls >= 1);

    ROBO_CHECK_EQ_INT(robo_window_event_loop_wakeup(), ROBO_OK);
    ROBO_CHECK(g_lm.wakeup_calls >= 1);

    ROBO_CHECK_EQ_INT(robo_window_event_loop_request_stop(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_is_running(), 0);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_run_once(ROBO_WINDOW_EVENT_WAIT_FOREVER),
                      ROBO_OK);
    fixture_end();
}

static void test_reentrancy(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain();

    g_lm.nest_poll_on_process = 1;
    ROBO_CHECK_EQ_INT(robo_window_event_loop_poll(), ROBO_OK);

    /* Destroy from "handler" after close inject. */
    inj_push(INJ_CLOSE, id, 0, 0, 0, 0);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_poll(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_observe_size(id, 10, 10), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_observe_position(id, 1, 1), ROBO_NOT_FOUND);
    fixture_end();
}

static void test_threading(void)
{
    fixture();
    ROBO_CHECK_EQ_INT(robo_window_thread_bind_owner(1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_thread_enter(2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_poll(), ROBO_PERMISSION_DENIED);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_start(), ROBO_PERMISSION_DENIED);
    ROBO_CHECK_EQ_INT(robo_window_thread_enter(1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_poll(), ROBO_OK);
    fixture_end();
}

static void test_backend_failures(void)
{
    fixture();
    g_lm.fail_events = 1;
    ROBO_CHECK_EQ_INT(robo_window_event_loop_poll(), ROBO_IO_ERROR);
    g_lm.fail_events = 0;

    g_lm.fail_wait = 1;
    ROBO_CHECK_EQ_INT(robo_window_event_loop_run_once(10), ROBO_IO_ERROR);
    g_lm.fail_wait = 0;

    g_lm.fail_wakeup = 1;
    ROBO_CHECK_EQ_INT(robo_window_event_loop_wakeup(), ROBO_IO_ERROR);
    g_lm.fail_wakeup = 0;

    g_lm.fail_init = 1;
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_lm_vtbl, &g_lm),
                      ROBO_INIT_ERROR);
    fixture_end();
}

static void test_direct_vs_queued(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_event_t ev;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    drain();

    /* Direct observe (no process_events). */
    ROBO_CHECK_EQ_INT(robo_window_observe_position(id, 5, 6), ROBO_OK);
    ROBO_CHECK(poll_find(ROBO_WINDOW_EVENT_MOVED, id, &ev));
    ROBO_CHECK_EQ_INT(ev.x, 5);
    ROBO_CHECK_EQ_INT(ev.y, 6);

    /* Queued via backend process_events. */
    inj_push(INJ_MOVE, id, 7, 8, 0, 0);
    ROBO_CHECK_EQ_INT(robo_window_process_events(), ROBO_OK);
    ROBO_CHECK(poll_find(ROBO_WINDOW_EVENT_MOVED, id, &ev));
    ROBO_CHECK_EQ_INT(ev.x, 7);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
}

void robo_test_register_window_event_loop(void)
{
    robo_test_register("window.loop.queue", test_queue_basics);
    robo_test_register("window.loop.order", test_queue_ordering_multi);
    robo_test_register("window.loop.target", test_targeting);
    robo_test_register("window.loop.close", test_close_path);
    robo_test_register("window.loop.state", test_resize_move_visibility_focus);
    robo_test_register("window.loop.controls", test_loop_controls);
    robo_test_register("window.loop.reentrancy", test_reentrancy);
    robo_test_register("window.loop.thread", test_threading);
    robo_test_register("window.loop.backend_fail", test_backend_failures);
    robo_test_register("window.loop.direct_queued", test_direct_vs_queued);
}
